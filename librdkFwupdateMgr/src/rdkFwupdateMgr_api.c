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

/*
 * checkForUpdate - Initiate a non-blocking firmware availability check.
 *
 * OVERVIEW
 *
 * PURPOSE:
 *   This is the FIRST async API call in the firmware update workflow.
 *   After registerProcess() gives you a handle, you call this to ask
 *   the daemon "is there a new firmware version for this device?"
 *
 *   This function is NON-BLOCKING. It returns immediately (in ~3ms)
 *   with CHECK_FOR_UPDATE_SUCCESS, meaning "your request was accepted."
 *   The actual answer (FIRMWARE_AVAILABLE or FIRMWARE_NOT_AVAILABLE)
 *   arrives later (5-30 seconds) via your callback function, which is
 *   invoked by the library's background thread.
 *
 * WHAT "FIRE-AND-FORGET" MEANS:
 *   The D-Bus call to the daemon is fire-and-forget:
 *     - We send the message and do NOT wait for a reply
 *     - The daemon's method response is silently discarded (we already
 *       closed our ephemeral D-Bus connection by then)
 *     - The real answer comes as a BROADCAST D-Bus signal:
 *       "CheckForUpdateComplete" -- caught by the BG thread
 *
 *   Think of it like mailing a letter: you drop it in the mailbox
 *   (return SUCCESS) and walk away. The reply comes later by separate
 *   delivery (the callback).
 *
 * THREADING MODEL:
 *   - This function runs on the CALLER'S thread (main thread)
 *   - It does NOT block the caller
 *   - The callback fires on the BACKGROUND thread (created during
 *     registerProcess -> internal_system_init)
 *   - The caller typically sleeps on a condvar until the callback
 *     sets a flag and signals it
 *
 * D-BUS WIRE PROTOCOL:
 *   Method: "CheckForUpdate"
 *   Input:  GVariant type "(s)" -- one string: the handle (e.g., "1")
 *   Reply:  IGNORED (fire-and-forget -- three trailing NULLs)
 *   Signal: "CheckForUpdateComplete" type "(tiissss)"
 *     t  handler_id        (uint64)
 *     i  result_code       (int32)
 *     i  status_code       (int32: 0=available, 1=not available, 3=error)
 *     s  current_version   (e.g., "RDKV_7.0")
 *     s  available_version (e.g., "RDKV_8.0")
 *     s  update_details    (pipe-separated "Key:Value|Key:Value|...")
 *     s  status_message    (human-readable)
 *
 * CONNECTION MODEL:
 *   This creates an EPHEMERAL D-Bus connection (e.g., :1.142) that
 *   lives only for the duration of this function call. The BG thread
 *   has its own PERSISTENT connection (:1.141) for receiving signals.
 *   These are completely independent.
 *
 * CALLBACK CONTRACT:
 *   - Fires exactly ONCE per checkForUpdate() call
 *   - Fires on the BG thread, NOT the caller's thread
 *   - Receives a const FwInfoData* that is STACK-ALLOCATED in the
 *     dispatch function -- valid ONLY during the callback
 *   - If you need data after the callback returns, you MUST copy it
 *     (e.g., strncpy to your own buffers)
 *   - If the daemon crashes or the signal never arrives, the callback
 *     NEVER fires -- the caller should use a condvar timeout (120s)
 *
 * RETURN VALUES:
 *   CHECK_FOR_UPDATE_SUCCESS (0) -- Request sent. Callback will fire later.
 *   CHECK_FOR_UPDATE_FAIL    (1) -- Request failed. Callback will NOT fire.
 *     IMPORTANT: SUCCESS does NOT mean firmware is available. It means
 *     the request was accepted. Actual availability comes in the callback.
 *
 * EXECUTION FLOW (step numbers match code comments below):
 *
 *   [1] Validate handle and callback (reject NULL/empty)
 *   [2] Open ephemeral D-Bus connection (fail early if D-Bus is down)
 *   [3] Register callback in g_registry (mutex-protected)
 *       -- MUST happen BEFORE sending the D-Bus call to avoid race
 *   [4] Send fire-and-forget "CheckForUpdate" D-Bus method call
 *   [5] Close ephemeral connection, return SUCCESS
 *
 *   [Later, 5-30 seconds -- on BG thread:]
 *   Daemon broadcasts "CheckForUpdateComplete" signal
 *   BG thread receives it in on_check_complete_signal()
 *   dispatch_all_pending() finds our PENDING slot, invokes our callback
 *   Slot is reset to IDLE after callback returns
 *
 * WHY REGISTER BEFORE SEND (Step 3 before Step 4):
 *   If we sent the D-Bus call FIRST and the daemon responded instantly
 *   (e.g., cached result), the BG thread would receive the signal before
 *   we registered the callback. dispatch_all_pending() would scan the
 *   registry, find zero PENDING entries, and discard the signal.
 *   Our callback would never fire. The app would hang on condvar forever.
 *
 *   By registering FIRST, the callback is waiting in the registry before
 *   the daemon can possibly respond. Race condition eliminated.
 *
 * WHY CONNECT BEFORE REGISTER (Step 2 before Step 3):
 *   If we registered the callback FIRST and then D-Bus connection failed,
 *   we'd have a "ghost" PENDING entry that will never be dispatched
 *   (because the D-Bus call was never sent, so the signal will never
 *   arrive). The slot would stay PENDING forever, wasting 1 of 30 slots.
 *
 *   By connecting FIRST, we know D-Bus is up before we touch the registry.
 *   If connection fails, we return FAIL with a clean registry.
 *
 * @param handle    The handle returned by registerProcess(). Must be
 *                  non-NULL and non-empty. e.g., "1"
 * @param callback  Function pointer to invoke when the daemon's signal
 *                  arrives. Must be non-NULL. Signature:
 *                    void callback(const FwInfoData *fwinfodata)
 *
 * @return CHECK_FOR_UPDATE_SUCCESS or CHECK_FOR_UPDATE_FAIL
 *
 * See also: on_check_complete_signal()  -- BG thread signal handler
 * See also: dispatch_all_pending()      -- two-phase callback dispatch
 * See also: internal_register_callback() -- registry slot allocation
 */
CheckForUpdateResult checkForUpdate(FirmwareInterfaceHandle handle,
                                    UpdateEventCallback callback)
{
    /* Validate inputs */
    if (handle == NULL || handle[0] == '\0') {
        FWUPMGR_ERROR("checkForUpdate: invalid handle (NULL or empty)\n");
        return CHECK_FOR_UPDATE_FAIL;
    }

    if (callback == NULL) {
        FWUPMGR_ERROR("checkForUpdate: callback is NULL\n");
        return CHECK_FOR_UPDATE_FAIL;
    }

    FWUPMGR_INFO("checkForUpdate: handle='%s'\n", handle);

    /* Open ephemeral D-Bus connection (before registry to avoid ghost entries) */
    GError *error = NULL;
    GDBusConnection *conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);

    if (conn == NULL) {
        FWUPMGR_ERROR("checkForUpdate: D-Bus connect failed: %s\n",
                      error ? error->message : "unknown");
        if (error) g_error_free(error);
        return CHECK_FOR_UPDATE_FAIL;
    }

    /* Register callback BEFORE D-Bus call to prevent signal race */
    if (!internal_register_callback(handle, callback)) {
        FWUPMGR_ERROR("checkForUpdate: registry full, handle='%s'\n", handle);
        g_object_unref(conn);
        return CHECK_FOR_UPDATE_FAIL;
    }

    /* Fire-and-forget D-Bus call (3 trailing NULLs = no reply) */
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

    /* Close ephemeral connection; message already in kernel socket buffer */
    g_object_unref(conn);

    FWUPMGR_INFO("checkForUpdate: D-Bus call sent, returning SUCCESS. "
                 "Callback will fire when CheckForUpdateComplete signal arrives. "
                 "handle='%s'\n", handle);

    return CHECK_FOR_UPDATE_SUCCESS;
}
/*
 * downloadFirmware - Initiate a non-blocking firmware download.
 *
 * OVERVIEW
 *
 * PURPOSE:
 *   This is the SECOND async API call in the firmware update workflow.
 *   After checkForUpdate() confirmed firmware IS available (status ==
 *   FIRMWARE_AVAILABLE and you got the filename + URL from UpdateDetails),
 *   you call this to tell the daemon "start downloading that file."
 *
 *   This function is NON-BLOCKING. It returns immediately (in ~3ms) with
 *   RDKFW_DWNL_SUCCESS, meaning "your request was accepted." The actual
 *   download progress (0%, 10%, 50%, 100%) arrives later -- REPEATEDLY --
 *   via your callback function, which is invoked by the library's
 *   background thread each time the daemon emits a DownloadProgress signal.
 *
 * KEY DIFFERENCE FROM checkForUpdate():
 *   checkForUpdate callback fires ONCE (one signal, one callback, done).
 *   downloadFirmware callback fires MANY TIMES (one per progress report).
 *   The registry slot stays ACTIVE across all progress signals and only
 *   goes IDLE when the download ends (COMPLETED or ERROR).
 *
 * WHAT "FIRE-AND-FORGET" MEANS (SAME PATTERN AS checkForUpdate):
 *   We send the D-Bus method call and do NOT wait for a reply.
 *   The daemon's method response is discarded (connection already closed).
 *   The real data comes as BROADCAST D-Bus signals: "DownloadProgress"
 *   -- caught by the BG thread's on_download_progress_signal() handler.
 *
 * THREADING MODEL:
 *   - This function runs on the CALLER'S thread (main thread)
 *   - It does NOT block the caller
 *   - The callback fires on the BACKGROUND thread (the one created
 *     during registerProcess -> internal_system_init)
 *   - The BG thread fires the callback MULTIPLE TIMES (once per signal)
 *   - The caller typically sleeps on a condvar until the callback
 *     sets g_download_done=1 on a terminal status (COMPLETED/ERROR)
 *
 * D-BUS WIRE PROTOCOL:
 *   Method: "DownloadFirmware"
 *   Input:  GVariant type "(ssss)" -- four strings:
 *     s  handle           e.g., "1" (from registerProcess)
 *     s  firmwareName     e.g., "firmware_v8.bin"
 *     s  downloadUrl      e.g., "http://cdn.example.com/fw" or "" (use XConf)
 *     s  TypeOfFirmware   e.g., "PCI" or "PDRI" or "PERIPHERAL"
 *   Reply: IGNORED (fire-and-forget -- three trailing NULLs)
 *
 *   Signal (arrives later, MULTIPLE times):
 *     Name: "DownloadProgress"
 *     GVariant type "(tsuss)":
 *       t  handler_id       (uint64 - which client)
 *       s  firmware_name    (string - filename being downloaded)
 *       u  progress_percent (uint32 - 0 to 100)
 *       s  status_string    (string - "NOTSTARTED", "INPROGRESS", "COMPLETED", "ERROR")
 *       s  message          (string - human-readable message)
 *
 * CONNECTION MODEL:
 *   Creates an EPHEMERAL D-Bus connection (e.g., :1.143) that lives
 *   only for this function call. The BG thread has its own PERSISTENT
 *   connection (:1.141) for receiving signals. Completely independent.
 *
 * DOWNLOAD REGISTRY (g_dwnl_registry -- SEPARATE from g_registry):
 *   This API uses its OWN registry, independent from checkForUpdate's.
 *   g_dwnl_registry has its own mutex, its own 30 slots, its own
 *   state machine. The two registries never interfere with each other.
 *
 *   Slot lifecycle:  IDLE --> ACTIVE --> IDLE
 *     IDLE:   Slot free, no callback registered.
 *     ACTIVE: Callback registered. Fires on EVERY DownloadProgress signal.
 *             Stays ACTIVE across multiple signals (0%, 10%, 50%...).
 *     IDLE:   Reset when DWNL_COMPLETED or DWNL_ERROR is received.
 *
 *   Compare with checkForUpdate's lifecycle:
 *     IDLE --> PENDING --> DISPATCHED --> IDLE  (fires ONCE)
 *   Download has NO "DISPATCHED" state because the slot fires repeatedly.
 *
 * CALLBACK CONTRACT:
 *   - Fires MULTIPLE TIMES (once per DownloadProgress signal)
 *   - Fires on the BG thread, NOT the caller's thread
 *   - Signature: void callback(int progress_per, DownloadStatus status)
 *   - progress_per: 0 to 100 (percentage complete)
 *   - status: DWNL_IN_PROGRESS, DWNL_COMPLETED, or DWNL_ERROR
 *   - DWNL_COMPLETED means download finished successfully
 *   - DWNL_ERROR means download failed (network error, disk full, etc.)
 *   - After COMPLETED or ERROR, no more callbacks will fire
 *   - If daemon crashes mid-download, callback NEVER fires with
 *     COMPLETED/ERROR -- the caller's condvar timeout is the safety net
 *
 * RETURN VALUES:
 *   RDKFW_DWNL_SUCCESS (0) -- Request sent. Callbacks will fire later.
 *   RDKFW_DWNL_FAILED  (1) -- Request failed. Callback will NOT fire.
 *     IMPORTANT: SUCCESS does NOT mean download started. It means the
 *     request was accepted. Actual progress comes in the callbacks.
 *
 * EXECUTION FLOW (step numbers match code comments below):
 *
 *   [1] Validate handle, fwdwnlreq, firmwareName, callback (reject NULL/empty)
 *   [2] Open ephemeral D-Bus connection (fail early if D-Bus is down)
 *   [3] Register callback in g_dwnl_registry (state = ACTIVE)
 *       -- MUST happen BEFORE sending D-Bus call to avoid race
 *   [4] Send fire-and-forget "DownloadFirmware" D-Bus method call
 *   [5] Close ephemeral connection, return RDKFW_DWNL_SUCCESS
 *
 *   [Later, repeatedly -- on BG thread:]
 *   Daemon broadcasts "DownloadProgress" signal (multiple times)
 *   BG thread receives it in on_download_progress_signal()
 *   dispatch_all_dwnl_active() finds our ACTIVE slot, invokes callback
 *   If status == COMPLETED or ERROR: slot is reset to IDLE
 *   Otherwise: slot stays ACTIVE for next signal
 *
 * WHY REGISTER BEFORE SEND (Step 3 before Step 4):
 *   Same race condition as checkForUpdate. If the daemon responds
 *   instantly (e.g., file already cached locally), the BG thread
 *   would receive the signal before we registered. The dispatch
 *   would find zero ACTIVE entries and silently drop the signal.
 *   Our callback would never fire. The app would hang forever.
 *
 * WHY CONNECT BEFORE REGISTER (Step 2 before Step 3):
 *   If we registered first and D-Bus connection then failed, we'd
 *   have a ghost ACTIVE entry that never fires (because the D-Bus
 *   call was never sent). The slot would stay ACTIVE forever,
 *   wasting 1 of 30 slots and never being cleaned up.
 *
 * @param handle     The handle returned by registerProcess(). Must be
 *                   non-NULL and non-empty. e.g., "1"
 * @param fwdwnlreq  Pointer to download request struct. Must be non-NULL.
 *                   Contains firmwareName (required), downloadUrl (optional,
 *                   "" means use XConf URL), TypeOfFirmware (optional).
 * @param callback   Function pointer invoked on each DownloadProgress signal.
 *                   Must be non-NULL. Signature:
 *                     void callback(int progress_per, DownloadStatus status)
 *
 * @return RDKFW_DWNL_SUCCESS or RDKFW_DWNL_FAILED
 *
 * See also: on_download_progress_signal() -- BG thread signal handler
 * See also: dispatch_all_dwnl_active()    -- two-phase callback dispatch
 * See also: internal_dwnl_register_callback() -- registry slot allocation
 */
DownloadResult downloadFirmware(FirmwareInterfaceHandle handle,
                                const FwDwnlReq *fwdwnlreq,
                                DownloadCallback callback)
{
    /* Validate inputs */
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

    /* Open ephemeral D-Bus connection (before registry to avoid ghost entries) */
    GError *error = NULL;
    GDBusConnection *conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);

    if (conn == NULL) {
        FWUPMGR_ERROR("downloadFirmware: D-Bus connect failed: %s\n",
                      error ? error->message : "unknown");
        if (error) g_error_free(error);
        return RDKFW_DWNL_FAILED;
    }

    /* Register callback BEFORE D-Bus call to prevent signal race */
    if (!internal_dwnl_register_callback(handle, callback)) {
        FWUPMGR_ERROR("downloadFirmware: registry full, handle='%s'\n", handle);
        g_object_unref(conn);
        return RDKFW_DWNL_FAILED;
    }

    /* Fire-and-forget D-Bus call with 4 string args (handle, name, url, type) */

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

    /* Close ephemeral connection; message already in kernel socket buffer */
    g_object_unref(conn);

    FWUPMGR_INFO("downloadFirmware: D-Bus call sent, returning SUCCESS. handle='%s'\n",
                 handle);

    return RDKFW_DWNL_SUCCESS;
}

/* ========================================================================
 * UPDATE FIRMWARE PUBLIC API
 * ========================================================================
 *
 * updateFirmware -- Initiate firmware flashing (non-blocking)
 *
 * Implements:
 *   UpdateResult updateFirmware(FirmwareInterfaceHandle handle,
 *                               const FwUpdateReq *fwupdatereq,
 *                               UpdateCallback callback);
 *
 * PURPOSE:
 *   This is the third and final step in the firmware lifecycle:
 *     1. checkForUpdate()     -- ask the daemon if new firmware exists
 *     2. downloadFirmware()   -- download the firmware image
 *     3. updateFirmware()     -- flash the downloaded image onto the device
 *
 *   updateFirmware() sends a fire-and-forget D-Bus method call to the
 *   daemon instructing it to flash the specified firmware image. The
 *   function returns IMMEDIATELY -- the actual flashing happens on the
 *   daemon side and may take minutes. Progress is delivered through
 *   repeated callbacks on the BG thread.
 *
 * RETURN VALUES:
 *   RDKFW_UPDATE_SUCCESS (0) -- Request sent. Callbacks will fire later.
 *   RDKFW_UPDATE_FAILED  (1) -- Request failed. Callback will NOT fire.
 *     IMPORTANT: SUCCESS does NOT mean flashing started. It means the
 *     request was accepted. Actual progress comes in the callbacks.
 *
 * CALLBACK CONTRACT:
 *   The UpdateCallback is invoked MULTIPLE TIMES (like downloadFirmware,
 *   unlike checkForUpdate which fires once). Each invocation carries:
 *     int progress_per     -- 0 to 100 (percent complete)
 *     UpdateStatus status  -- UPDATE_IN_PROGRESS, UPDATE_COMPLETED, or
 *                             UPDATE_ERROR
 *   The callback fires on the BG thread (NOT the main thread). If the
 *   app needs to update UI, it must marshal the call to the main thread.
 *
 * EXECUTION FLOW (step numbers match code comments below):
 *
 *   [1] Validate handle, fwupdatereq, firmwareName, TypeOfFirmware,
 *       callback (reject NULL/empty). This has the MOST validations
 *       of all three APIs (7 checks vs 4 for checkForUpdate, 5 for
 *       downloadFirmware) because FwUpdateReq has more required fields.
 *   [2] Open ephemeral D-Bus connection (fail early if D-Bus is down)
 *   [3] Register callback in g_update_registry (state = ACTIVE)
 *       -- MUST happen BEFORE sending D-Bus call to avoid race
 *   [4] Send fire-and-forget "UpdateFirmware" D-Bus method call
 *   [5] Close ephemeral connection, return RDKFW_UPDATE_SUCCESS
 *
 *   [Later, repeatedly -- on BG thread:]
 *   Daemon broadcasts "UpdateProgress" signal (multiple times)
 *   BG thread receives it in on_update_progress_signal()
 *   dispatch_all_update_active() finds our ACTIVE slot, invokes callback
 *   If status == UPDATE_COMPLETED or UPDATE_ERROR: slot is reset to IDLE
 *   Otherwise: slot stays ACTIVE for next signal
 *
 * WHY REGISTER BEFORE SEND (Step 3 before Step 4):
 *   Same race condition as checkForUpdate and downloadFirmware. If the
 *   daemon responds instantly (e.g., trivial flash operation), the BG
 *   thread would receive the signal before we registered. The dispatch
 *   would find zero ACTIVE entries and silently drop the signal.
 *   Our callback would never fire. The app would hang forever.
 *
 * WHY CONNECT BEFORE REGISTER (Step 2 before Step 3):
 *   If we registered first and D-Bus connection then failed, we'd
 *   have a ghost ACTIVE entry that never fires (because the D-Bus
 *   call was never sent). The slot would stay ACTIVE forever,
 *   wasting 1 of 30 slots and never being cleaned up.
 *
 * D-BUS ARGUMENTS: (sssss) -- five strings
 *   This is the ONLY API that sends 5 strings. For comparison:
 *     checkForUpdate:   (s)     -- 1 string  (handle)
 *     downloadFirmware: (ssss)  -- 4 strings (handle, name, url, type)
 *     updateFirmware:   (sssss) -- 5 strings (handle, name, location,
 *                                              type, rebootImmediately)
 *
 *   The 5th argument, rebootImmediately, is a boolean in FwUpdateReq
 *   but is sent as a string "true"/"false" because the daemon's D-Bus
 *   interface expects all arguments as strings.
 *
 * REGISTRY DIFFERENCES FROM checkForUpdate AND downloadFirmware:
 *   - Uses g_update_registry (third separate registry, not g_registry
 *     or g_dwnl_registry)
 *   - Slot state = UPDATE_CB_STATE_ACTIVE (fires repeatedly, like download)
 *   - Signal format = "(tsiis)" not "(tsuss)" like download
 *     t = handler_id (uint64), s = firmware_name, i = progress (int32),
 *     i = status_code (int32), s = message
 *   - Status mapping uses INTEGER codes (0=IN_PROGRESS, 1=COMPLETED,
 *     2=ERROR) via internal_map_update_status_code(), not STRING codes
 *     like download's map_dwnl_status_string()
 *
 * @param handle     The handle returned by registerProcess(). Must be
 *                   non-NULL and non-empty. e.g., "1"
 * @param fwupdatereq  Pointer to update request struct. Must be non-NULL.
 *                   Contains firmwareName (required), LocationOfFirmware
 *                   (optional, "" means use device.properties path),
 *                   TypeOfFirmware (required, e.g. "PCI"),
 *                   rebootImmediately (bool, converted to "true"/"false").
 * @param callback   Function pointer invoked on each UpdateProgress signal.
 *                   Must be non-NULL. Signature:
 *                     void callback(int progress_per, UpdateStatus status)
 *
 * @return RDKFW_UPDATE_SUCCESS or RDKFW_UPDATE_FAILED
 *
 * See also: on_update_progress_signal()         -- BG thread signal handler
 * See also: dispatch_all_update_active()        -- two-phase callback dispatch
 * See also: internal_update_register_callback() -- registry slot allocation
 * See also: internal_map_update_status_code()   -- integer->enum mapping
 * ======================================================================== */
UpdateResult updateFirmware(FirmwareInterfaceHandle handle,
                             const FwUpdateReq *fwupdatereq,
                             UpdateCallback callback)
{
    /* Validate inputs (most checks of all 3 APIs -- TypeOfFirmware required) */
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

    /* Open ephemeral D-Bus connection (before registry to avoid ghost entries) */
    GError *error = NULL;
    GDBusConnection *conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);

    if (conn == NULL) {
        FWUPMGR_ERROR("updateFirmware: D-Bus connect failed: %s\n",
                      error ? error->message : "unknown");
        if (error) g_error_free(error);
        return RDKFW_UPDATE_FAILED;
    }

    /*Register callback BEFORE D-Bus call to prevent signal race */
    if (!internal_update_register_callback(handle, callback)) {
        FWUPMGR_ERROR("updateFirmware: registry full, handle='%s'\n", handle);
        g_object_unref(conn);
        return RDKFW_UPDATE_FAILED;
    }

    /* Fire-and-forget D-Bus call with 5 string args (handle, name, location, type, reboot) */

    g_dbus_connection_call(
        conn,
        DBUS_SERVICE_NAME,
        DBUS_OBJECT_PATH,
        DBUS_INTERFACE_NAME,
        DBUS_METHOD_UPDATE,                         /* method: UpdateFirmware     */
        g_variant_new("(sssss)",                    /* 5 strings (see above)      */
                      handle,                                     /* app's handler_id string    */
                      fwupdatereq->firmwareName,                  /* image to flash             */
                      fwupdatereq->LocationOfFirmware ? fwupdatereq->LocationOfFirmware : "", /* path or "" */
                      fwupdatereq->TypeOfFirmware,                /* PCI / PDRI / PERIPHERAL    */
                      fwupdatereq->rebootImmediately ? "true" : "false"), /* reboot flag sent to daemon */
        NULL,                                       /* expected reply: none       */
        G_DBUS_CALL_FLAGS_NONE,
        DBUS_TIMEOUT_MS,
        NULL,                                       /* GCancellable: none         */
        NULL,                                       /* reply callback: none       */
        NULL                                        /* user_data: none            */
    );

    /*  Close ephemeral connection; message already in kernel socket buffer */
    g_object_unref(conn);

    FWUPMGR_INFO("updateFirmware: D-Bus call sent, returning SUCCESS. "
                 "handle='%s'\n", handle);

    return RDKFW_UPDATE_SUCCESS;
}
