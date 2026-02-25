/*
 * Copyright 2025 Comcast Cable Communications Management, LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file rdkFwupdateMgr_api.c
 * @brief Implementation of firmware update APIs (checkForUpdate, downloadFirmware, updateFirmware)
 *
 * This file implements the client-side firmware management APIs that communicate
 * with the rdkFwupdateMgr daemon via D-Bus.
 *
 * ARCHITECTURE:
 * =============
 * - Synchronous D-Bus calls for checkForUpdate (fast, cache-based on daemon side)
 * - Asynchronous operations with callback mechanism for download/update progress
 * - D-Bus signals used for async notifications (CheckForUpdateComplete, etc.)
 * - Thread-safe callback invocation using GLib main context
 *
 * D-BUS PROTOCOL:
 * ===============
 * Service Name:   org.rdkfwupdater.Interface
 * Object Path:    /org/rdkfwupdater/Service
 * Interface:      org.rdkfwupdater.Interface
 *
 * Methods:
 *   CheckForUpdate(handler_process_name: s) -> (result: i, fwdata_*: various)
 *   DownloadFirmware(handlerId: s, firmwareName: s, ...) -> (result: s, status: s, message: s)
 *   UpdateFirmware(handlerId: s, firmwareName: s, ...) -> (UpdateResult: s, UpdateStatus: s, message: s)
 *
 * Signals:
 *   CheckForUpdateComplete(handlerId: t, result: i, statusCode: i, ...)
 *   DownloadProgress(handlerId: t, progress: i, status: i)
 *   UpdateProgress(handlerId: t, progress: i, status: i)
 *
 * CALLBACK MECHANISM:
 * ===================
 * - Callbacks are stored in a global registry (thread-safe with mutex)
 * - D-Bus signal handlers lookup callback by handler_id
 * - Callbacks are invoked on caller's thread (via GLib main context)
 * - FwInfoData structure allocated on heap, passed to callback, then freed
 *
 * MEMORY MANAGEMENT:
 * ==================
 * - FwInfoData and UpdateDetails allocated on heap, freed after callback
 * - Strings from D-Bus responses copied and freed appropriately
 * - Caller must not free callback parameters (library owns them)
 *
 * THREAD SAFETY:
 * ==============
 * - GDBus is thread-safe for method calls
 * - Callback registry protected by mutex
 * - Signal handlers run on D-Bus thread, callbacks queued to caller's thread
 */

#include "rdkFwupdateMgr_client.h"
#include "rdkFwupdateMgr_log.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <gio/gio.h>
#include <pthread.h>

/* ========================================================================
 * CONSTANTS
 * ======================================================================== */

/** D-Bus service name for firmware update daemon */
#define DBUS_SERVICE_NAME       "org.rdkfwupdater.Interface"

/** D-Bus object path */
#define DBUS_OBJECT_PATH        "/org/rdkfwupdater/Service"

/** D-Bus interface name */
#define DBUS_INTERFACE_NAME     "org.rdkfwupdater.Interface"

/** Default D-Bus call timeout in milliseconds (30 seconds for CheckForUpdate) */
#define DBUS_TIMEOUT_MS         30000

/* ========================================================================
 * CALLBACK REGISTRY
 * ======================================================================== */

/**
 * Callback context stored for async operations
 */
typedef struct {
    FirmwareInterfaceHandle handle;
    UpdateEventCallback update_callback;
    DownloadCallback download_callback;
    UpdateCallback firmware_update_callback;
} CallbackContext;

// Global callback registry (simple single-entry for now)
// TODO: Extend to hash table for multi-handler support
static CallbackContext g_callback_ctx = {0};
static pthread_mutex_t g_callback_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ========================================================================
 * HELPER FUNCTIONS
 * ======================================================================== */

/**
 * @brief Create D-Bus proxy for firmware update daemon
 *
 * @return GDBusProxy* on success, NULL on failure
 */
static GDBusProxy* create_dbus_proxy(void) {
    GError *error = NULL;
    GDBusProxy *proxy = NULL;

    // Connect to system bus
    GDBusConnection *connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    if (!connection) {
        FWUPMGR_ERROR("Failed to connect to D-Bus system bus: %s\n",
                      error ? error->message : "unknown error");
        if (error) g_error_free(error);
        return NULL;
    }

    // Create proxy for firmware update daemon
    proxy = g_dbus_proxy_new_sync(
        connection,
        G_DBUS_PROXY_FLAGS_NONE,
        NULL,                       // GDBusInterfaceInfo
        DBUS_SERVICE_NAME,
        DBUS_OBJECT_PATH,
        DBUS_INTERFACE_NAME,
        NULL,                       // GCancellable
        &error
    );

    if (!proxy) {
        FWUPMGR_ERROR("Failed to create D-Bus proxy: %s\n",
                      error ? error->message : "unknown error");
        if (error) g_error_free(error);
        g_object_unref(connection);
        return NULL;
    }

    g_object_unref(connection);
    return proxy;
}

/**
 * @brief Parse UpdateDetails from D-Bus response string
 *
 * Expected format: "FwFileName:xxx,FwUrl:xxx,FwVersion:xxx,RebootImmediately:xxx,..."
 *
 * @param details_str D-Bus update details string
 * @param details Output UpdateDetails structure
 */
static void parse_update_details(const char *details_str, UpdateDetails *details) {
    if (!details_str || !details) {
        return;
    }

    // Initialize all fields to empty strings
    memset(details, 0, sizeof(UpdateDetails));

    // Parse comma-separated key:value pairs
    char *str_copy = strdup(details_str);
    if (!str_copy) {
        FWUPMGR_ERROR("Failed to allocate memory for parsing update details\n");
        return;
    }

    char *token = strtok(str_copy, ",");
    while (token) {
        char *colon = strchr(token, ':');
        if (colon) {
            *colon = '\0';  // Split key and value
            const char *key = token;
            const char *value = colon + 1;

            if (strcmp(key, "FwFileName") == 0) {
                strncpy(details->FwFileName, value, MAX_FW_FILENAME_SIZE - 1);
            } else if (strcmp(key, "FwUrl") == 0) {
                strncpy(details->FwUrl, value, MAX_FW_URL_SIZE - 1);
            } else if (strcmp(key, "FwVersion") == 0) {
                strncpy(details->FwVersion, value, MAX_FW_VERSION_SIZE - 1);
            } else if (strcmp(key, "RebootImmediately") == 0) {
                strncpy(details->RebootImmediately, value, MAX_REBOOT_IMMEDIATELY_SIZE - 1);
            } else if (strcmp(key, "DelayDownload") == 0) {
                strncpy(details->DelayDownload, value, MAX_DELAY_DOWNLOAD_SIZE - 1);
            } else if (strcmp(key, "PDRIVersion") == 0) {
                strncpy(details->PDRIVersion, value, MAX_PDRI_VERSION_LEN - 1);
            } else if (strcmp(key, "PeripheralFirmwares") == 0) {
                strncpy(details->PeripheralFirmwares, value, MAX_PERIPHERAL_VERSION_LEN - 1);
            }
        }
        token = strtok(NULL, ",");
    }

    free(str_copy);
}

/* ========================================================================
 * PUBLIC API IMPLEMENTATION
 * ======================================================================== */

/**
 * @brief Check for available firmware updates
 *
 * Sends CheckForUpdate D-Bus method call to daemon. The daemon responds
 * synchronously with firmware information (cached or live XConf query).
 *
 * Flow:
 * 1. Validate handle and callback
 * 2. Create D-Bus proxy
 * 3. Call CheckForUpdate method with handler_id
 * 4. Parse response (result, version, updateDetails, status_code)
 * 5. Build FwInfoData structure
 * 6. Invoke callback immediately (synchronous operation)
 * 7. Clean up resources
 *
 * @param handle Handler ID from registerProcess()
 * @param callback Callback to receive firmware info
 * @return CHECK_FOR_UPDATE_SUCCESS or CHECK_FOR_UPDATE_FAIL
 */
CheckForUpdateResult checkForUpdate(FirmwareInterfaceHandle handle, UpdateEventCallback callback) {
    FWUPMGR_INFO("checkForUpdate() called\n");

    // Validate parameters
    if (!handle) {
        FWUPMGR_ERROR("checkForUpdate: handle is NULL\n");
        return CHECK_FOR_UPDATE_FAIL;
    }

    if (!callback) {
        FWUPMGR_ERROR("checkForUpdate: callback is NULL\n");
        return CHECK_FOR_UPDATE_FAIL;
    }

    FWUPMGR_INFO("checkForUpdate: handle=%s\n", handle);

    // Store callback for potential async signal handling
    pthread_mutex_lock(&g_callback_mutex);
    g_callback_ctx.handle = handle;
    g_callback_ctx.update_callback = callback;
    pthread_mutex_unlock(&g_callback_mutex);

    // Create D-Bus proxy
    GDBusProxy *proxy = create_dbus_proxy();
    if (!proxy) {
        FWUPMGR_ERROR("checkForUpdate: Failed to create D-Bus proxy\n");
        return CHECK_FOR_UPDATE_FAIL;
    }

    // Call CheckForUpdate D-Bus method
    FWUPMGR_INFO("checkForUpdate: Calling D-Bus method CheckForUpdate with handler_id=%s\n", handle);
    
    GError *error = NULL;
    GVariant *result = g_dbus_proxy_call_sync(
        proxy,
        "CheckForUpdate",
        g_variant_new("(s)", handle),  // handler_process_name
        G_DBUS_CALL_FLAGS_NONE,
        DBUS_TIMEOUT_MS,
        NULL,  // GCancellable
        &error
    );

    if (error) {
        FWUPMGR_ERROR("checkForUpdate: D-Bus call failed: %s\n", error->message);
        g_error_free(error);
        g_object_unref(proxy);
        return CHECK_FOR_UPDATE_FAIL;
    }

    if (!result) {
        FWUPMGR_ERROR("checkForUpdate: D-Bus call returned NULL result\n");
        g_object_unref(proxy);
        return CHECK_FOR_UPDATE_FAIL;
    }

    // Parse D-Bus response
    // CheckForUpdate returns: (result: i, fwdata_version: s, fwdata_availableVersion: s,
    //                          fwdata_updateDetails: s, fwdata_status: s, fwdata_status_code: i)
    gint32 api_result = 0;
    gchar *curr_version = NULL;
    gchar *avail_version = NULL;
    gchar *update_details_str = NULL;
    gchar *status_str = NULL;
    gint32 status_code = 0;

    g_variant_get(result, "(issssi)",
                  &api_result,
                  &curr_version,
                  &avail_version,
                  &update_details_str,
                  &status_str,
                  &status_code);

    FWUPMGR_INFO("checkForUpdate: D-Bus response received\n");
    FWUPMGR_INFO("  api_result=%d (0=SUCCESS, 1=FAIL)\n", api_result);
    FWUPMGR_INFO("  curr_version=%s\n", curr_version ? curr_version : "(null)");
    FWUPMGR_INFO("  avail_version=%s\n", avail_version ? avail_version : "(null)");
    FWUPMGR_INFO("  update_details=%s\n", update_details_str ? update_details_str : "(null)");
    FWUPMGR_INFO("  status_str=%s\n", status_str ? status_str : "(null)");
    FWUPMGR_INFO("  status_code=%d\n", status_code);

    // Check if API call succeeded
    if (api_result != CHECK_FOR_UPDATE_SUCCESS) {
        FWUPMGR_ERROR("checkForUpdate: Daemon returned FAIL result\n");
        g_free(curr_version);
        g_free(avail_version);
        g_free(update_details_str);
        g_free(status_str);
        g_variant_unref(result);
        g_object_unref(proxy);
        return CHECK_FOR_UPDATE_FAIL;
    }

    // Allocate FwInfoData structure
    FwInfoData *fwinfo = (FwInfoData *)malloc(sizeof(FwInfoData));
    if (!fwinfo) {
        FWUPMGR_ERROR("checkForUpdate: Failed to allocate FwInfoData\n");
        g_free(curr_version);
        g_free(avail_version);
        g_free(update_details_str);
        g_free(status_str);
        g_variant_unref(result);
        g_object_unref(proxy);
        return CHECK_FOR_UPDATE_FAIL;
    }

    // Allocate UpdateDetails structure
    UpdateDetails *update_details = (UpdateDetails *)malloc(sizeof(UpdateDetails));
    if (!update_details) {
        FWUPMGR_ERROR("checkForUpdate: Failed to allocate UpdateDetails\n");
        free(fwinfo);
        g_free(curr_version);
        g_free(avail_version);
        g_free(update_details_str);
        g_free(status_str);
        g_variant_unref(result);
        g_object_unref(proxy);
        return CHECK_FOR_UPDATE_FAIL;
    }

    // Fill FwInfoData structure
    memset(fwinfo, 0, sizeof(FwInfoData));
    if (curr_version) {
        strncpy(fwinfo->CurrFWVersion, curr_version, MAX_FW_VERSION_SIZE - 1);
    }
    fwinfo->status = (CheckForUpdateStatus)status_code;
    fwinfo->UpdateDetails = update_details;

    // Parse and fill UpdateDetails
    parse_update_details(update_details_str, update_details);

    // Invoke callback immediately (synchronous operation)
    FWUPMGR_INFO("checkForUpdate: Invoking callback with status=%d\n", fwinfo->status);
    callback(fwinfo);

    // Clean up
    free(update_details);
    free(fwinfo);
    g_free(curr_version);
    g_free(avail_version);
    g_free(update_details_str);
    g_free(status_str);
    g_variant_unref(result);
    g_object_unref(proxy);

    FWUPMGR_INFO("checkForUpdate: Completed successfully\n");
    return CHECK_FOR_UPDATE_SUCCESS;
}

/**
 * @brief Download firmware image (stub implementation)
 *
 * TODO: Implement DownloadFirmware D-Bus method call
 *
 * @param handle Handler ID from registerProcess()
 * @param fwdwnlreq Download request details
 * @param callback Progress callback
 * @return RDKFW_DWNL_SUCCESS or RDKFW_DWNL_FAILED
 */
DownloadResult downloadFirmware(FirmwareInterfaceHandle handle,
                                const FwDwnlReq *fwdwnlreq,
                                DownloadCallback callback) {
    FWUPMGR_INFO("downloadFirmware() called (stub)\n");
    
    if (!handle || !fwdwnlreq || !callback) {
        FWUPMGR_ERROR("downloadFirmware: Invalid parameters\n");
        return RDKFW_DWNL_FAILED;
    }

    // TODO: Implement D-Bus call to DownloadFirmware
    FWUPMGR_WARN("downloadFirmware: Not implemented yet\n");
    return RDKFW_DWNL_FAILED;
}

/**
 * @brief Flash firmware to device (stub implementation)
 *
 * TODO: Implement UpdateFirmware D-Bus method call
 *
 * @param handle Handler ID from registerProcess()
 * @param fwupdatereq Update request details
 * @param callback Progress callback
 * @return RDKFW_UPDATE_SUCCESS or RDKFW_UPDATE_FAILED
 */
UpdateResult updateFirmware(FirmwareInterfaceHandle handle,
                            const FwUpdateReq *fwupdatereq,
                            UpdateCallback callback) {
    FWUPMGR_INFO("updateFirmware() called (stub)\n");
    
    if (!handle || !fwupdatereq || !callback) {
        FWUPMGR_ERROR("updateFirmware: Invalid parameters\n");
        return RDKFW_UPDATE_FAILED;
    }

    // TODO: Implement D-Bus call to UpdateFirmware
    FWUPMGR_WARN("updateFirmware: Not implemented yet\n");
    return RDKFW_UPDATE_FAILED;
}
