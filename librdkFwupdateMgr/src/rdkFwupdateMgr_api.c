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
 * ALL THREE APIS USE THE SAME ASYNC PATTERN:
 * ===========================================
 * All APIs are NON-BLOCKING fire-and-forget calls that return immediately.
 * Results are delivered asynchronously via D-Bus signals to registered callbacks.
 *
 * CHECKFORUPDATE:
 * ---------------
 * 1. Validate handle and callback
 * 2. Register callback in registry (BEFORE D-Bus call to avoid race)
 * 3. Fire CheckForUpdate D-Bus method call (fire-and-forget)
 * 4. Return CHECK_FOR_UPDATE_SUCCESS immediately
 * 
 * [Later - typically 5-30 seconds]
 * Daemon queries XConf server and emits CheckForUpdateComplete signal
 * → on_check_complete_signal() fires in background thread
 * → dispatch_all_pending() calls registered UpdateEventCallback
 * → Callback receives FwInfoData with version info and update details
 *
 * DOWNLOAD / UPDATE FIRMWARE:
 * ============================
 * Same pattern but with progress signals:
 * - DownloadFirmware → DownloadProgress signals (multiple, 0%-100%)
 * - UpdateFirmware   → UpdateProgress signals (multiple, 0%-100%)
 * 
 * Callbacks fire repeatedly until COMPLETED or ERROR status.
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
 * @brief Check for firmware update — non-blocking, returns immediately
 *
 * Sends CheckForUpdate(handle) to the daemon and returns immediately.
 * The daemon will query the XConf server in the background (5-30 seconds)
 * and emit a CheckForUpdateComplete signal when done.
 *
 * The callback fires ONCE when the signal arrives with complete firmware info:
 *   - FwInfoData.status:        FIRMWARE_AVAILABLE, FIRMWARE_NOT_AVAILABLE, etc.
 *   - FwInfoData.CurrFWVersion: Current firmware version
 *   - FwInfoData.UpdateDetails: Details about available update (if any)
 *
 * The callback is registered in the async registry before sending the D-Bus call
 * to ensure the signal doesn't arrive before we're ready to receive it.
 *
 * @param handle    Valid FirmwareInterfaceHandle from registerProcess()
 * @param callback  Invoked when CheckForUpdateComplete signal arrives
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

    /* [3] Fire-and-forget D-Bus CheckForUpdate method call
     *
     * Arguments: (s)
     *   s handle  — identifies this app to the daemon
     *
     * Three trailing NULLs = fire and forget (no reply waited for).
     * g_dbus_connection_call() returns immediately.
     * Daemon will emit CheckForUpdateComplete signal when XConf query finishes.
     */
    GError *error = NULL;
    GDBusConnection *conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);

    if (conn == NULL) {
        FWUPMGR_ERROR("checkForUpdate: D-Bus connect failed: %s\n",
                      error ? error->message : "unknown");
        if (error) g_error_free(error);
        return CHECK_FOR_UPDATE_FAIL;
    }

    FWUPMGR_INFO("checkForUpdate: calling CheckForUpdate on daemon, handle='%s'\n",
                 handle);

    g_dbus_connection_call(
        conn,
        DBUS_SERVICE_NAME,
        DBUS_OBJECT_PATH,
        DBUS_INTERFACE_NAME,
        DBUS_METHOD_CHECK,                      /* method: CheckForUpdate     */
        g_variant_new("(s)", handle),           /* app's handler_id string    */
        NULL,                                   /* expected reply type: none  */
        G_DBUS_CALL_FLAGS_NONE,
        DBUS_TIMEOUT_MS,
        NULL,                                   /* GCancellable: none         */
        NULL,                                   /* reply callback: none       */
        NULL                                    /* user_data: none            */
    );

    g_object_unref(conn);

    FWUPMGR_INFO("checkForUpdate: D-Bus call sent, returning SUCCESS. "
                 "Callback will fire when CheckForUpdateComplete signal arrives. "
                 "handle='%s'\n", handle);

    /* [4] Return immediately — app is unblocked */
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

    if (fwdwnlreq == NULL) {
        FWUPMGR_ERROR("downloadFirmware: fwdwnlreq is NULL\n");
        return RDKFW_DWNL_FAILED;
    }

    if (fwdwnlreq->firmwareName == NULL) {
        FWUPMGR_ERROR("downloadFirmware: firmwareName is NULL\n");
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
                 handle, 
                 fwdwnlreq->firmwareName,
                 (fwdwnlreq->TypeOfFirmware && fwdwnlreq->TypeOfFirmware[0]) ? fwdwnlreq->TypeOfFirmware : "(none)",
                 (fwdwnlreq->downloadUrl && fwdwnlreq->downloadUrl[0]) ? fwdwnlreq->downloadUrl : "(use XConf)");

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
                      handle,                                     /* app's handler_id string    */
                      fwdwnlreq->firmwareName,                     /* firmware image name        */
                      fwdwnlreq->downloadUrl ? fwdwnlreq->downloadUrl : "",  /* override URL or ""  */
                      fwdwnlreq->TypeOfFirmware ? fwdwnlreq->TypeOfFirmware : ""),  /* PCI / PDRI / PERIPHERAL */
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

    if (fwupdatereq == NULL) {
        FWUPMGR_ERROR("updateFirmware: fwupdatereq is NULL\n");
        return RDKFW_UPDATE_FAILED;
    }

    if (fwupdatereq->firmwareName == NULL) {
        FWUPMGR_ERROR("updateFirmware: firmwareName is NULL\n");
        return RDKFW_UPDATE_FAILED;
    }

    if (fwupdatereq->firmwareName[0] == '\0') {
        FWUPMGR_ERROR("updateFirmware: firmwareName is empty\n");
        return RDKFW_UPDATE_FAILED;
    }

    if (fwupdatereq->TypeOfFirmware == NULL) {
        FWUPMGR_ERROR("updateFirmware: TypeOfFirmware is NULL\n");
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
                 (fwupdatereq->LocationOfFirmware && fwupdatereq->LocationOfFirmware[0])
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
                      handle,                                    /* app's handler_id string    */
                      fwupdatereq->firmwareName,                  /* image to flash             */
                      fwupdatereq->LocationOfFirmware ? fwupdatereq->LocationOfFirmware : "", /* path or "" */
                      fwupdatereq->TypeOfFirmware,                /* PCI / PDRI / PERIPHERAL    */
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
