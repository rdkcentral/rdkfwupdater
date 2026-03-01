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
 * @brief Public checkForUpdate() implementation
 *
 * Implements the required API:
 *   CheckForUpdateResult checkForUpdate(FirmwareInterfaceHandle handle,
 *                                       UpdateEventCallback callback);
 *
 * FLOW:
 *   1. Validate: handle != NULL/empty, callback != NULL
 *   2. Register callback in internal registry (BEFORE D-Bus call)
 *   3. Fire D-Bus CheckForUpdate method call to daemon (fire-and-forget)
 *   4. Return CHECK_FOR_UPDATE_SUCCESS immediately
 *
 * The actual firmware result arrives via CheckForUpdateComplete signal,
 * handled in rdkFwupdateMgr_async.c, which invokes the app callback.
 */

#include "rdkFwupdateMgr_process.h"
#include "rdkFwupdateMgr_async_internal.h"
#include "rdkFwupdateMgr_log.h"
#include <gio/gio.h>
#include <string.h>
#include <stdlib.h>

/* ========================================================================
 * PUBLIC API
 * ======================================================================== */

/**
 * @brief Check for firmware update — non-blocking, 2-parameter API
 *
 * Aligned to the required signature:
 *   CheckForUpdateResult checkForUpdate(FirmwareInterfaceHandle handle,
 *                                       UpdateEventCallback callback);
 *
 * INTERNAL SEQUENCE:
 *
 *   [1] Validate parameters
 *       handle must be non-NULL and non-empty (came from registerProcess)
 *       callback must be non-NULL
 *
 *   [2] Register callback FIRST (before D-Bus call)
 *       internal_register_callback(handle, callback)
 *       ─ Finds a free IDLE slot in the registry
 *       ─ Stores handle (strdup'd) and callback pointer
 *       ─ Sets slot state = PENDING
 *
 *       WHY REGISTER BEFORE SENDING?
 *       If we sent the D-Bus call first, the daemon could be so fast that
 *       it emits the signal before we finish registering — we'd miss it.
 *       Registering first closes that race window completely.
 *
 *   [3] Send D-Bus CheckForUpdate method call (fire-and-forget)
 *       ─ Creates a fresh D-Bus connection (stateless design per process.h)
 *       ─ Calls CheckForUpdate(handle) on org.rdkfwupdater.Interface
 *       ─ Does NOT wait for the method reply (three trailing NULLs)
 *       ─ Returns immediately after queueing the message
 *
 *   [4] Return CHECK_FOR_UPDATE_SUCCESS
 *       App regains control. Background thread is watching for the signal.
 *
 *   [later, in background thread]
 *       Daemon emits CheckForUpdateComplete signal
 *       → on_check_complete_signal() parses payload
 *       → dispatch_all_pending() finds slot with matching handle
 *       → callback(handle, &event_data) called — 2-param, no user_data
 *       → slot reset to IDLE
 *
 * @param handle    Valid FirmwareInterfaceHandle from registerProcess()
 * @param callback  UpdateEventCallback invoked when result is available
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

    /* [2] Register callback BEFORE sending D-Bus call */
    if (!internal_register_callback(handle, callback)) {
        FWUPMGR_ERROR("checkForUpdate: registry full, handle='%s'\n", handle);
        return CHECK_FOR_UPDATE_FAIL;
    }

    /* [3] Fire-and-forget D-Bus method call */
    GError *error = NULL;
    GDBusConnection *conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);

    if (conn == NULL) {
        FWUPMGR_ERROR("checkForUpdate: D-Bus connect failed: %s\n",
                      error ? error->message : "unknown");
        if (error) g_error_free(error);
        /*
         * Cannot send the D-Bus call. The daemon will never emit a signal
         * for this request, so the registered callback would never fire.
         * The slot will age out via CALLBACK_TIMEOUT_SECONDS cleanup.
         * Return FAIL so the app knows the call did not go through.
         */
        return CHECK_FOR_UPDATE_FAIL;
    }

    /*
     * Call CheckForUpdate(handle) on the daemon.
     *
     * The handle string is passed as the single argument (s).
     * The daemon uses it to identify which component/process is requesting
     * the check, and tags the outgoing signal accordingly.
     *
     * The three trailing NULLs make this fire-and-forget:
     *   NULL → no GAsyncReadyCallback  (don't want a method reply)
     *   NULL → no GCancellable
     *   NULL → no user_data for reply callback
     *
     * g_dbus_connection_call() returns immediately after queuing the
     * message to the D-Bus socket. We move on without waiting.
     */
    g_dbus_connection_call(
        conn,
        DBUS_SERVICE_NAME,                 /* destination                  */
        DBUS_OBJECT_PATH,                  /* object path                  */
        DBUS_INTERFACE_NAME,               /* interface                    */
        DBUS_METHOD_CHECK,                 /* method: CheckForUpdate       */
        g_variant_new("(s)", handle),      /* arg: handle string           */
        NULL,                              /* expected reply type: none    */
        G_DBUS_CALL_FLAGS_NONE,
        DBUS_TIMEOUT_MS,
        NULL,                              /* GCancellable: none           */
        NULL,                              /* reply callback: none         */
        NULL                               /* user_data for reply: none    */
    );

    g_object_unref(conn);

    FWUPMGR_INFO("checkForUpdate: D-Bus call sent, returning SUCCESS. handle='%s'\n",
                 handle);

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
                                FwDwnlReq fwdwnlreq,
                                DownloadCallback callback)
{
    /* [1] Validate */
    if (handle == NULL || handle[0] == '\0') {
        FWUPMGR_ERROR("downloadFirmware: invalid handle (NULL or empty)\n");
        return RDKFW_DWNL_FAILED;
    }

    if (fwdwnlreq.firmwareName[0] == '\0') {
        FWUPMGR_ERROR("downloadFirmware: firmwareName is empty\n");
        return RDKFW_DWNL_FAILED;
    }

    if (callback == NULL) {
        FWUPMGR_ERROR("downloadFirmware: callback is NULL\n");
        return RDKFW_DWNL_FAILED;
    }

    FWUPMGR_INFO("downloadFirmware: handle='%s' firmware='%s' type='%s' url='%s'\n",
                 handle, fwdwnlreq.firmwareName,
                 fwdwnlreq.TypeOfFirmware[0] ? fwdwnlreq.TypeOfFirmware : "(none)",
                 fwdwnlreq.downloadUrl[0]    ? fwdwnlreq.downloadUrl    : "(use XConf)");

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
                      fwdwnlreq.firmwareName,      /* firmware image name        */
                      fwdwnlreq.downloadUrl,       /* override URL or ""         */
                      fwdwnlreq.TypeOfFirmware),   /* PCI / PDRI / PERIPHERAL    */
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
                             FwUpdateReq fwupdatereq,
                             UpdateCallback callback)
{
    /* [1] Validate */
    if (handle == NULL || handle[0] == '\0') {
        FWUPMGR_ERROR("updateFirmware: invalid handle (NULL or empty)\n");
        return RDKFW_UPDATE_FAILED;
    }

    if (fwupdatereq.firmwareName[0] == '\0') {
        FWUPMGR_ERROR("updateFirmware: firmwareName is empty\n");
        return RDKFW_UPDATE_FAILED;
    }

    if (fwupdatereq.TypeOfFirmware[0] == '\0') {
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
                 fwupdatereq.firmwareName,
                 fwupdatereq.TypeOfFirmware,
                 fwupdatereq.LocationOfFirmware[0]
                     ? fwupdatereq.LocationOfFirmware
                     : "(use device.properties path)",
                 fwupdatereq.rebootImmediately ? "yes" : "no");

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
        g_variant_new("(sssb)",
                      handle,                       /* app's handler_id string    */
                      fwupdatereq.firmwareName,     /* image to flash             */
                      fwupdatereq.LocationOfFirmware, /* path or ""               */
                      fwupdatereq.TypeOfFirmware,   /* PCI / PDRI / PERIPHERAL    */
                      fwupdatereq.rebootImmediately),/* reboot flag               */
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


