/*
 * Copyright 2026 Comcast Cable Communications Management, LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file rdkFwupdateMgr_api.c
 * @brief Public API implementations: checkForUpdate, downloadFirmware, updateFirmware
 *
 * CHECKFORUPDATE DESIGN:
 * =====================
 * CheckForUpdate is a SYNCHRONOUS D-Bus method on the daemon.
 * The daemon processes the XConf query and returns the result directly
 * in the method reply — no separate signal needed for the basic result.
 *
 * The CheckForUpdateComplete SIGNAL also exists on the daemon and fires
 * to all connected clients. We subscribe to it in async.c for multi-app
 * broadcast delivery, but the primary result is read from the sync reply.
 *
 * FLOW:
 *   1. Validate: handle != NULL/empty, callback != NULL
 *   2. Register callback in registry (so signal-based delivery also works)
 *   3. Call CheckForUpdate(handle) SYNCHRONOUSLY — wait for reply
 *   4. Parse reply: (issssi) → result, versions, details, status_code
 *   5. Build FwUpdateEventData, fire callback immediately
 *   6. Return CHECK_FOR_UPDATE_SUCCESS
 *
 * DOWNLOAD / UPDATE FIRMWARE DESIGN:
 * ====================================
 * DownloadFirmware and UpdateFirmware are also synchronous calls but the
 * daemon additionally emits progress signals (DownloadProgress, UpdateProgress)
 * repeatedly. We register the callback, call sync to initiate, then the
 * background thread delivers progress via signals.
 */

#include "rdkFwupdateMgr_client.h"
#include "rdkFwupdateMgr_async_internal.h"
#include "rdkFwupdateMgr_log.h"
#include <gio/gio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

/* ========================================================================
 * checkForUpdate — SYNCHRONOUS implementation
 * ======================================================================== */

/**
 * @brief Check for firmware update — calls daemon synchronously, fires callback
 *
 * Sends CheckForUpdate(handle) and blocks until the daemon replies.
 * The daemon returns the result directly: (issssi)
 *   i  result        (0=success, non-0=fail)
 *   s  currentVersion
 *   s  availableVersion
 *   s  updateDetails
 *   s  statusMessage
 *   i  statusCode    (maps to CheckForUpdateStatus enum)
 *
 * On success, builds FwUpdateEventData and fires the callback immediately
 * in the caller's thread — no signal, no background thread needed.
 *
 * Also registers callback in the async registry so the
 * CheckForUpdateComplete broadcast signal (if the daemon emits it) also
 * delivers to this callback.
 *
 * @param handle    Valid FirmwareInterfaceHandle from registerProcess()
 * @param callback  Invoked with result before this function returns
 * @return CHECK_FOR_UPDATE_SUCCESS or CHECK_FOR_UPDATE_FAIL
 */
CheckForUpdateResult checkForUpdate(FirmwareInterfaceHandle handle,
                                    UpdateEventCallback callback)
{
    /* [1] Validate */
    if (handle == NULL || handle[0] == '\0') {
        FWUPMGR_ERROR("checkForUpdate: invalid handle (NULL or empty)\n");
        return CHECK_FOR_UPDATE_FAIL;
    }
    if (callback == NULL) {
        FWUPMGR_ERROR("checkForUpdate: callback is NULL\n");
        return CHECK_FOR_UPDATE_FAIL;
    }

    FWUPMGR_INFO("checkForUpdate: handle='%s'\n", handle);

    /* [2] Register in async registry catches CheckForUpdateComplete signal too */
    if (!internal_register_callback(handle, callback)) {
        FWUPMGR_ERROR("checkForUpdate: registry full, handle='%s'\n", handle);
        return CHECK_FOR_UPDATE_FAIL;
    }

    /* [3] Connect to system D-Bus */
    GError *error = NULL;
    GDBusConnection *conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    if (conn == NULL) {
        FWUPMGR_ERROR("checkForUpdate: D-Bus connect failed: %s\n",
                      error ? error->message : "unknown");
        if (error) g_error_free(error);
        return CHECK_FOR_UPDATE_FAIL;
    }

    /* [4] Call CheckForUpdate synchronously - 5s timeout for daemon ACK only */
    FWUPMGR_INFO("checkForUpdate: calling CheckForUpdate on daemon, handle='%s'\n",
                 handle);

    GVariant *reply = g_dbus_connection_call_sync(
        conn,
        DBUS_SERVICE_NAME,
        DBUS_OBJECT_PATH,
        DBUS_INTERFACE_NAME,
        DBUS_METHOD_CHECK,
        g_variant_new("(s)", handle),   /* handler_process_name */
        NULL,                           /* expected reply type: any */
        G_DBUS_CALL_FLAGS_NONE,
        5000,                           /* 5s - only waiting for daemon ACK, not XConf result*/
        NULL,                           /* GCancellable */
        &error
    );

    g_object_unref(conn);

    if (reply == NULL) {
        FWUPMGR_ERROR("checkForUpdate: D-Bus call failed: %s\n",
                      error ? error->message : "unknown");
        if (error) g_error_free(error);
        return CHECK_FOR_UPDATE_FAIL;
    }

    /* [5] Parse reply: (issssi)
     *   i  api_result       0 = success
     *   s  currentVersion
     *   s  availableVersion
     *   s  updateDetails    (unused by FwUpdateEventData but logged)
     *   s  statusMessage
     *   i  statusCode       maps to CheckForUpdateStatus enum
     */
    gint32  api_result   = 0;
    gint32  status_code  = 0;
    gchar  *curr_ver     = NULL;
    gchar  *avail_ver    = NULL;
    gchar  *upd_details  = NULL;
    gchar  *status_msg   = NULL;

    g_variant_get(reply, "(issssi)",
                  &api_result,
                  &curr_ver,
                  &avail_ver,
                  &upd_details,
                  &status_msg,
                  &status_code);
    g_variant_unref(reply);

    FWUPMGR_INFO("checkForUpdate: reply received — api_result=%d status_code=%d\n",
                 api_result, status_code);
    FWUPMGR_INFO("  currentVersion   = %s\n", curr_ver    ? curr_ver    : "(null)");
    FWUPMGR_INFO("  availableVersion = %s\n", avail_ver   ? avail_ver   : "(null)");
    FWUPMGR_INFO("  updateDetails    = %s\n", upd_details ? upd_details : "(null)");
    FWUPMGR_INFO("  statusMessage    = %s\n", status_msg  ? status_msg  : "(null)");

    if (api_result != 0) {
        FWUPMGR_ERROR("checkForUpdate: daemon returned failure (api_result=%d)\n",
                      api_result);
        g_free(curr_ver); g_free(avail_ver);
        g_free(upd_details); g_free(status_msg);
        return CHECK_FOR_UPDATE_FAIL;
    }

    /* [6] Build FwUpdateEventData and fire callback in caller's thread */
//    CheckForUpdateStatus status = internal_map_status_code(status_code);
/*
    FwUpdateEventData event_data = {
       .status            = status,
        .current_version   = curr_ver,
        .available_version = avail_ver,
        .status_message    = status_msg,
        .update_available  = (status == FIRMWARE_AVAILABLE)
    };

    FWUPMGR_INFO("checkForUpdate: firing callback — status=%d update_available=%d\n",
                 status, event_data.update_available);

    callback(handle, &event_data);
*/
    /* [6] Don't fire callback here - daemon response is just an ACK.
 * Callback will fire later when CheckForUpdateComplete signal arrives
 * with real firmware data from XConf (5-30 seconds). */

    g_free(curr_ver);
    g_free(avail_ver);
    g_free(upd_details);
    g_free(status_msg);

    if (api_result != 0) {
	    FWUPMGR_ERROR("checkForUpdate: daemon rejected (api_result=%d)\n", api_result);
	    return CHECK_FOR_UPDATE_FAIL;
    }

    FWUPMGR_INFO("checkForUpdate: trigger succeeded\n");
    FWUPMGR_INFO("  Callback will fire when signal arrives \n");

    FWUPMGR_INFO("checkForUpdate: done. handle='%s'\n", handle);
    return CHECK_FOR_UPDATE_SUCCESS;
}

/* ========================================================================
 * LIBRARY LIFECYCLE
 * ======================================================================== */

/**
 * @brief Library constructor — auto-called when .so is loaded
 *
 * Initializes the internal async engine (registry + background thread)
 * before any app code runs.
 */
__attribute__((constructor))
static void rdkFwupdateMgr_lib_init(void)
{
    FWUPMGR_INFO("=== rdkFwupdateMgr library loading ===\n");
    if (internal_system_init() != 0) {
        FWUPMGR_ERROR("rdkFwupdateMgr_lib_init: internal_system_init FAILED\n");
    }
    FWUPMGR_INFO("=== rdkFwupdateMgr library ready ===\n");
}

/**
 * @brief Library destructor — auto-called when .so is unloaded
 *
 * Stops background thread and frees all resources cleanly.
 */
__attribute__((destructor))
static void rdkFwupdateMgr_lib_deinit(void)
{
    FWUPMGR_INFO("=== rdkFwupdateMgr library unloading ===\n");
    internal_system_deinit();
    FWUPMGR_INFO("=== rdkFwupdateMgr library unloaded ===\n");
}

/* ========================================================================
 * DOWNLOAD FIRMWARE PUBLIC API
 * ========================================================================
 *
 * Implements:
 *   DownloadResult downloadFirmware(FirmwareInterfaceHandle handle,
 *                                   FwDwnlReq fwdwnlreq,
 *                                   DownloadCallback callback);
 *
 * FLOW:
 *   1. Validate: handle not NULL/empty, firmwareName not empty, callback not NULL
 *   2. Register callback in download registry (BEFORE D-Bus call)
 *   3. Fire DownloadFirmware D-Bus method call to daemon (fire-and-forget)
 *   4. Return RDKFW_DWNL_SUCCESS immediately
 *
 *   [later — fires multiple times as download progresses]
 *   Daemon emits DownloadProgress(progress%, status) signal repeatedly
 *   → on_download_progress_signal() fires in background thread
 *   → dispatch_all_dwnl_active() calls every ACTIVE DownloadCallback
 *   → slot stays ACTIVE until DWNL_COMPLETED or DWNL_ERROR
 * ======================================================================== */

/**
 * @brief Initiate firmware download — non-blocking, returns immediately
 *
 * @param handle      Valid FirmwareInterfaceHandle from registerProcess()
 * @param fwdwnlreq   Download request (passed by value, library copies it)
 * @param callback    Invoked on each DownloadProgress signal
 * @return RDKFW_DWNL_SUCCESS or RDKFW_DWNL_FAILED
 */
DownloadResult downloadFirmware(FirmwareInterfaceHandle handle,
                                const FwDwnlReq *fwdwnlreq,
                                DownloadCallback callback)
{
    /* [1] Validate */
    if (handle == NULL || handle[0] == '\0') {
        FWUPMGR_ERROR("downloadFirmware: invalid handle (NULL or empty)\n");
        return RDKFW_DWNL_FAILED;
    }

    if (fwdwnlreq->firmwareName[0] == '\0') {
        FWUPMGR_ERROR("downloadFirmware: firmwareName is empty\n");
        return RDKFW_DWNL_FAILED;
    }

    if (callback == NULL) {
        FWUPMGR_ERROR("downloadFirmware: callback is NULL\n");
        return RDKFW_DWNL_FAILED;
    }

    FWUPMGR_INFO("downloadFirmware: handle='%s' firmware='%s' type='%s' url='%s'\n",
                 handle, fwdwnlreq->firmwareName,
                 fwdwnlreq->TypeOfFirmware[0] ? fwdwnlreq->TypeOfFirmware : "(none)",
                 fwdwnlreq->downloadUrl[0]    ? fwdwnlreq->downloadUrl    : "(use XConf)");

    /* [2] Register callback BEFORE sending D-Bus call
     *
     * Same reason as checkForUpdate: if the daemon is fast enough to emit
     * the first DownloadProgress signal before we register, we'd miss it.
     * Register first, then send.
     */
    if (!internal_dwnl_register_callback(handle, callback)) {
        FWUPMGR_ERROR("downloadFirmware: registry full, handle='%s'\n", handle);
        return RDKFW_DWNL_FAILED;
    }

    /* [3] Fire-and-forget D-Bus DownloadFirmware method call
     *
     * Arguments: (ssss)
     *   s handle          — identifies this app to the daemon
     *   s firmwareName    — firmware image filename
     *   s downloadUrl     — override URL or "" for XConf URL
     *   s TypeOfFirmware  — "PCI" | "PDRI" | "PERIPHERAL"
     *
     * Three trailing NULLs = fire and forget (no reply waited for).
     * g_dbus_connection_call() returns immediately.
     */
    GError *error = NULL;
    GDBusConnection *conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);

    if (conn == NULL) {
        FWUPMGR_ERROR("downloadFirmware: D-Bus connect failed: %s\n",
                      error ? error->message : "unknown");
        if (error) g_error_free(error);
        return RDKFW_DWNL_FAILED;
    }

    g_dbus_connection_call(
        conn,
        DBUS_SERVICE_NAME,
        DBUS_OBJECT_PATH,
        DBUS_INTERFACE_NAME,
        DBUS_METHOD_DOWNLOAD,                      /* method: DownloadFirmware   */
        g_variant_new("(ssss)",
                      handle,                      /* app's handler_id string    */
                      fwdwnlreq->firmwareName,      /* firmware image name        */
                      fwdwnlreq->downloadUrl,       /* override URL or ""         */
                      fwdwnlreq->TypeOfFirmware),   /* PCI / PDRI / PERIPHERAL    */
        NULL,                                      /* expected reply type: none  */
        G_DBUS_CALL_FLAGS_NONE,
        DBUS_TIMEOUT_MS,
        NULL,                                      /* GCancellable: none         */
        NULL,                                      /* reply callback: none       */
        NULL                                       /* user_data: none            */
    );

    g_object_unref(conn);

    FWUPMGR_INFO("downloadFirmware: D-Bus call sent, returning SUCCESS. handle='%s'\n",
                 handle);

    /* [4] Return immediately — app is unblocked */
    return RDKFW_DWNL_SUCCESS;
}

/* ========================================================================
 * UPDATE FIRMWARE PUBLIC API
 * ========================================================================
 *
 * Implements:
 *   UpdateResult updateFirmware(FirmwareInterfaceHandle handle,
 *                               FwUpdateReq fwupdatereq,
 *                               UpdateCallback callback);
 *
 * FLOW:
 *   1. Validate: handle not NULL/empty, firmwareName not empty,
 *                TypeOfFirmware not empty, callback not NULL
 *   2. Register callback in update registry (BEFORE D-Bus call)
 *   3. Fire UpdateFirmware D-Bus method call to daemon (fire-and-forget)
 *   4. Return RDKFW_UPDATE_SUCCESS immediately
 *
 *   [later — fires multiple times as flashing progresses]
 *   Daemon emits UpdateProgress(progress%, status) signal repeatedly
 *   → on_update_progress_signal() fires in background thread
 *   → dispatch_all_update_active() calls every ACTIVE UpdateCallback
 *   → slot stays ACTIVE until UPDATE_COMPLETED or UPDATE_ERROR
 * ======================================================================== */

/**
 * @brief Initiate firmware flashing — non-blocking, returns immediately
 *
 * D-Bus arguments sent to daemon: (sssb)
 *   s  handle               — identifies this app
 *   s  firmwareName         — image filename to flash
 *   s  LocationOfFirmware   — path to image ("" = use device.properties)
 *   s  TypeOfFirmware       — "PCI" | "PDRI" | "PERIPHERAL"
 *   b  rebootImmediately    — reboot after flash?
 *
 * @param handle        Valid FirmwareInterfaceHandle from registerProcess()
 * @param fwupdatereq   Update request (passed by value, library copies it)
 * @param callback      Invoked on each UpdateProgress signal
 * @return RDKFW_UPDATE_SUCCESS or RDKFW_UPDATE_FAILED
 */
UpdateResult updateFirmware(FirmwareInterfaceHandle handle,
                             const FwUpdateReq *fwupdatereq,
                             UpdateCallback callback)
{
    /* [1] Validate */
    if (handle == NULL || handle[0] == '\0') {
        FWUPMGR_ERROR("updateFirmware: invalid handle (NULL or empty)\n");
        return RDKFW_UPDATE_FAILED;
    }

    if (fwupdatereq->firmwareName[0] == '\0') {
        FWUPMGR_ERROR("updateFirmware: firmwareName is empty\n");
        return RDKFW_UPDATE_FAILED;
    }

    if (fwupdatereq->TypeOfFirmware[0] == '\0') {
        FWUPMGR_ERROR("updateFirmware: TypeOfFirmware is empty\n");
        return RDKFW_UPDATE_FAILED;
    }

    if (callback == NULL) {
        FWUPMGR_ERROR("updateFirmware: callback is NULL\n");
        return RDKFW_UPDATE_FAILED;
    }

    FWUPMGR_INFO("updateFirmware: handle='%s' firmware='%s' type='%s' "
                 "location='%s' reboot=%s\n",
                 handle,
                 fwupdatereq->firmwareName,
                 fwupdatereq->TypeOfFirmware,
                 fwupdatereq->LocationOfFirmware[0]
                     ? fwupdatereq->LocationOfFirmware
                     : "(use device.properties path)",
                 fwupdatereq->rebootImmediately ? "yes" : "no");

    /* [2] Register callback BEFORE sending D-Bus call */
    if (!internal_update_register_callback(handle, callback)) {
        FWUPMGR_ERROR("updateFirmware: registry full, handle='%s'\n", handle);
        return RDKFW_UPDATE_FAILED;
    }

    /* [3] Fire-and-forget D-Bus UpdateFirmware method call
     *
     * Arguments: (sssb)
     *   s handle                — app's handler_id string
     *   s firmwareName          — image to flash
     *   s LocationOfFirmware    — path or "" for device.properties default
     *   s TypeOfFirmware        — PCI / PDRI / PERIPHERAL
     *   b rebootImmediately     — reboot after flash?
     *
     * Three trailing NULLs = fire and forget.
     */
    GError *error = NULL;
    GDBusConnection *conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);

    if (conn == NULL) {
        FWUPMGR_ERROR("updateFirmware: D-Bus connect failed: %s\n",
                      error ? error->message : "unknown");
        if (error) g_error_free(error);
        return RDKFW_UPDATE_FAILED;
    }

    g_dbus_connection_call(
        conn,
        DBUS_SERVICE_NAME,
        DBUS_OBJECT_PATH,
        DBUS_INTERFACE_NAME,
        DBUS_METHOD_UPDATE,                         /* method: UpdateFirmware     */
        g_variant_new("(ssss)",
                      handle,                       /* app's handler_id string    */
                      fwupdatereq->firmwareName,     /* image to flash             */
                      fwupdatereq->LocationOfFirmware, /* path or ""               */
                      fwupdatereq->TypeOfFirmware,   /* PCI / PDRI / PERIPHERAL    */
                      fwupdatereq->rebootImmediately ? "true" : "false"), /* 's' not 'b' — daemon expects string */
        NULL,                                       /* expected reply: none       */
        G_DBUS_CALL_FLAGS_NONE,
        DBUS_TIMEOUT_MS,
        NULL,                                       /* GCancellable: none         */
        NULL,                                       /* reply callback: none       */
        NULL                                        /* user_data: none            */
    );

    g_object_unref(conn);

    FWUPMGR_INFO("updateFirmware: D-Bus call sent, returning SUCCESS. "
                 "handle='%s'\n", handle);

    /* [4] Return immediately — app is unblocked */
    return RDKFW_UPDATE_SUCCESS;
}
