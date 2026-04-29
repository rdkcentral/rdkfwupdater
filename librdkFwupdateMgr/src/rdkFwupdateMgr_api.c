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
    /*
     * [STEP 1] INPUT VALIDATION
     *
     * Reject obviously bad inputs before touching D-Bus or the registry.
     * This is the library's input boundary -- validate everything here.
     */

    /*
     * Check 1a: handle must not be NULL and must not be empty "".
     *
     * handle is the string "1" from registerProcess(). If the caller
     * passes NULL (forgot to check registerProcess return value) or
     * somehow has an empty string, reject immediately.
     *
     * handle[0] == '\0' catches the empty string case that a simple
     * NULL check would miss. An empty handle would cause the daemon
     * to reject the request anyway, but we catch it here to avoid
     * a wasted D-Bus round-trip.
     */
    if (handle == NULL || handle[0] == '\0') {
        FWUPMGR_ERROR("checkForUpdate: invalid handle (NULL or empty)\n");
        return CHECK_FOR_UPDATE_FAIL;
    }

    /*
     * Check 1b: callback must not be NULL.
     *
     * If the caller passes NULL, they'll never receive the firmware
     * check result. That's a programming error -- they probably forgot
     * to pass their callback function. Catch it here with a clear
     * error message rather than crashing later when we try to call
     * through a NULL function pointer.
     */
    if (callback == NULL) {
        FWUPMGR_ERROR("checkForUpdate: callback is NULL\n");
        return CHECK_FOR_UPDATE_FAIL;
    }

    FWUPMGR_INFO("checkForUpdate: handle='%s'\n", handle);

    /*
     * [STEP 2] CREATE EPHEMERAL D-BUS CONNECTION
     *
     * g_bus_get_sync(G_BUS_TYPE_SYSTEM, ...) opens a new connection to
     * the system D-Bus bus. This connection gets a unique sender name
     * like :1.142 -- different from every other connection.
     *
     * Why a NEW connection instead of reusing the BG thread's :1.141?
     *   The BG thread's connection is attached to the BG thread's
     *   GMainContext. Using it from the main thread would require
     *   cross-thread GLib context management -- complex and fragile.
     *   A fresh per-call connection is simpler and avoids any
     *   thread-safety issues with GLib internals.
     *
     * Why BEFORE registering the callback?
     *   If D-Bus is down (dbus-daemon crashed, socket missing), this
     *   call fails. We want to fail BEFORE polluting the callback
     *   registry with a PENDING entry that will never be dispatched.
     *   Clean failure: no registry entry, no dangling state.
     *
     * Cost: ~2ms for the D-Bus handshake. Negligible for a firmware
     * check that takes 5-30 seconds total.
     */
    GError *error = NULL;
    GDBusConnection *conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);

    if (conn == NULL) {
        /*
         * D-Bus connection failed. Common causes:
         *   - dbus-daemon not running
         *   - System bus socket missing (/var/run/dbus/system_bus_socket)
         *   - Permission denied (D-Bus policy rejects our user)
         *
         * Return FAIL -- the caller should check if the daemon is running.
         * No registry entry was created, so nothing to clean up.
         */
        FWUPMGR_ERROR("checkForUpdate: D-Bus connect failed: %s\n",
                      error ? error->message : "unknown");
        if (error) g_error_free(error);
        return CHECK_FOR_UPDATE_FAIL;
    }

    /*
     * [STEP 3] REGISTER CALLBACK IN THE CHECK-FOR-UPDATE REGISTRY
     *
     * internal_register_callback() does the following (see _async.c):
     *   1. Locks g_registry.mutex
     *   2. Scans all 30 slots for:
     *      a. An existing PENDING entry with the same handle (dedup)
     *      b. The first IDLE slot (free slot)
     *   3. If same handle found: overwrites it (prevents ghost callbacks)
     *      If free slot found: uses it
     *      If neither: returns false (registry full -- 30 concurrent checks!)
     *   4. Populates the slot:
     *      - handle_key = strdup(handle)  -- "1" (heap copy, freed on reset)
     *      - callback = our function pointer
     *      - state = CB_STATE_PENDING
     *      - registered_time = current unix timestamp
     *   5. Unlocks g_registry.mutex
     *   6. Returns true
     *
     * After this call, the registry has one PENDING entry. When the
     * BG thread receives the "CheckForUpdateComplete" signal, it will
     * find this entry and invoke the callback.
     *
     * Why BEFORE the D-Bus call?
     *   Race condition prevention. If the daemon responds faster than
     *   we can register (theoretically possible with cached results),
     *   the BG thread would find an empty registry and drop the signal.
     *   Registering first guarantees the callback is waiting.
     *
     * Failure case: registry full (30 concurrent pending checks).
     *   This means 30 different checkForUpdate() calls are all waiting
     *   for callbacks simultaneously. In practice this never happens --
     *   a single client typically has 1 pending check at a time.
     *   If it does happen, we clean up the D-Bus connection and fail.
     */
    if (!internal_register_callback(handle, callback)) {
        FWUPMGR_ERROR("checkForUpdate: registry full, handle='%s'\n", handle);
        g_object_unref(conn);
        return CHECK_FOR_UPDATE_FAIL;
    }

    /*
     * [STEP 4] SEND FIRE-AND-FORGET D-BUS METHOD CALL
     *
     * g_dbus_connection_call() sends a D-Bus method call to the daemon.
     * This is the ASYNC variant (not _sync). But we're not even using
     * the async callback -- all three trailing NULLs mean "don't tell
     * me what the daemon replied."
     *
     * Parameters to g_dbus_connection_call():
     *   conn              -- our ephemeral connection :1.142
     *   DBUS_SERVICE_NAME -- "org.rdkfwupdater.Service" (daemon's well-known name)
     *   DBUS_OBJECT_PATH  -- "/org/rdkfwupdater/Service" (daemon's object path)
     *   DBUS_INTERFACE_NAME -- "org.rdkfwupdater.Interface"
     *   DBUS_METHOD_CHECK -- "CheckForUpdate" (the method name)
     *   g_variant_new("(s)", handle) -- argument: the string "1"
     *     "(s)" means a tuple containing one string
     *     This tells the daemon which registered client is asking
     *   NULL              -- expected reply type: we don't care
     *   G_DBUS_CALL_FLAGS_NONE -- no special flags
     *   DBUS_TIMEOUT_MS   -- 5000ms (only for message queueing, not for reply)
     *   NULL              -- GCancellable: no cancellation support
     *   NULL              -- GAsyncReadyCallback: no reply callback
     *   NULL              -- user_data for reply callback: N/A
     *
     * The three trailing NULLs (cancellable, callback, user_data) are what
     * make this fire-and-forget. GLib queues the D-Bus message in the
     * kernel's socket buffer and returns immediately. The daemon's reply
     * (which it does send -- an immediate FIRMWARE_CHECK_ERROR meaning
     * "I'm working on it") arrives at :1.142 but nobody reads it because
     * we close the connection moments later.
     *
     * WHAT HAPPENS ON THE DAEMON SIDE (for context):
     *   1. Daemon receives "CheckForUpdate" with argument "1"
     *   2. Validates handler "1" is registered (yes, from registerProcess)
     *   3. Sends immediate method reply: FIRMWARE_CHECK_ERROR (= "check in progress")
     *      -- This reply is DISCARDED because we're fire-and-forget
     *   4. Creates a GTask and spawns a worker thread to query XConf
     *   5. Worker thread does HTTP GET to XConf server (5-30 seconds)
     *   6. When XConf responds, daemon broadcasts "CheckForUpdateComplete" signal
     *      -- THIS is what our BG thread is waiting for
     *
     * The daemon may also PIGGYBACK: if another client already triggered
     * an XConf fetch, our request joins the waiting queue and gets the
     * same result when the fetch completes. One HTTP request serves all.
     */
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

    /*
     * [STEP 5] CLOSE EPHEMERAL CONNECTION AND RETURN
     *
     * g_object_unref(conn) closes our ephemeral D-Bus connection :1.142.
     * The D-Bus message is already in the kernel socket buffer -- closing
     * our end doesn't prevent delivery to the daemon. Think of it like
     * dropping a letter in a mailbox and walking away.
     *
     * After this, the state of the world is:
     *
     *   Main thread:
     *     - Returns CHECK_FOR_UPDATE_SUCCESS to the caller
     *     - Caller typically enters pthread_cond_timedwait (120s timeout)
     *     - Connection :1.142 is DEAD (just closed)
     *
     *   BG thread:
     *     - Still sleeping in g_main_loop_run() on connection :1.141
     *     - g_registry.entries[0] has our callback in PENDING state
     *     - Will wake up when CheckForUpdateComplete signal arrives
     *
     *   Daemon:
     *     - Received our request, spawned XConf worker thread
     *     - Will broadcast signal when XConf responds (5-30s)
     *
     *   Registry:
     *     entries[0] = { state=PENDING, handle_key="1",
     *                    callback=on_firmware_check_callback }
     *
     *   D-Bus connections:
     *     :1.140 -- registerProcess ephemeral (DEAD since registration)
     *     :1.141 -- BG thread persistent (ALIVE, listening for signals)
     *     :1.142 -- this checkForUpdate ephemeral (DEAD, just closed)
     *
     * SUCCESS here means: "I sent the request and registered your callback."
     * It does NOT mean: "Firmware is available." or even "The daemon
     * received the request." (Though it almost certainly did.)
     *
     * The callback will fire later on the BG thread. If it never fires
     * (daemon crashed, XConf unreachable), the caller's condvar timeout
     * will eventually expire and the caller can handle the timeout.
     */
    g_object_unref(conn);

    FWUPMGR_INFO("checkForUpdate: D-Bus call sent, returning SUCCESS. "
                 "Callback will fire when CheckForUpdateComplete signal arrives. "
                 "handle='%s'\n", handle);

    return CHECK_FOR_UPDATE_SUCCESS;
}
#if 0
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
#endif
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
 *   2. Connect to D-Bus (fail early if connection fails)
 *   3. Register callback in download registry (AFTER D-Bus connection succeeds)
 *   4. Fire DownloadFirmware D-Bus method call to daemon (fire-and-forget)
 *   5. Return RDKFW_DWNL_SUCCESS immediately
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

    /* [2] Connect to D-Bus FIRST before registering callback
     *
     * This prevents stale registry entries if D-Bus connection fails.
     */
    GError *error = NULL;
    GDBusConnection *conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);

    if (conn == NULL) {
        FWUPMGR_ERROR("downloadFirmware: D-Bus connect failed: %s\n",
                      error ? error->message : "unknown");
        if (error) g_error_free(error);
        return RDKFW_DWNL_FAILED;
    }

    /* [3] Register callback AFTER D-Bus connection succeeds, BEFORE sending
     *
     * Register immediately before sending to avoid race condition where
     * the daemon responds before we're ready to receive the signal.
     */
    if (!internal_dwnl_register_callback(handle, callback)) {
        FWUPMGR_ERROR("downloadFirmware: registry full, handle='%s'\n", handle);
        g_object_unref(conn);
        return RDKFW_DWNL_FAILED;
    }

    /* [4] Fire-and-forget D-Bus DownloadFirmware method call
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
 *   2. Connect to D-Bus (fail early if connection fails)
 *   3. Register callback in update registry (AFTER D-Bus connection succeeds)
 *   4. Fire UpdateFirmware D-Bus method call to daemon (fire-and-forget)
 *   5. Return RDKFW_UPDATE_SUCCESS immediately
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
 * D-Bus arguments sent to daemon: (sssss)
 *   s  handle               — identifies this app
 *   s  firmwareName         — image filename to flash
 *   s  LocationOfFirmware   — path to image ("" = use device.properties)
 *   s  TypeOfFirmware       — "PCI" | "PDRI" | "PERIPHERAL"
 *   s  rebootImmediately    — "true" or "false" (daemon expects string)
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

    /* [2] Connect to D-Bus FIRST before registering callback
     *
     * This prevents stale registry entries if D-Bus connection fails.
     */
    GError *error = NULL;
    GDBusConnection *conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);

    if (conn == NULL) {
        FWUPMGR_ERROR("updateFirmware: D-Bus connect failed: %s\n",
                      error ? error->message : "unknown");
        if (error) g_error_free(error);
        return RDKFW_UPDATE_FAILED;
    }

    /* [3] Register callback AFTER D-Bus connection succeeds, BEFORE sending
     *
     * Register immediately before sending to avoid race condition where
     * the daemon responds before we're ready to receive the signal.
     */
    if (!internal_update_register_callback(handle, callback)) {
        FWUPMGR_ERROR("updateFirmware: registry full, handle='%s'\n", handle);
        g_object_unref(conn);
        return RDKFW_UPDATE_FAILED;
    }

    /* [4] Fire-and-forget D-Bus UpdateFirmware method call
     *
     * Arguments: (sssss)
     *   s handle                — app's handler_id string
     *   s firmwareName          — image to flash
     *   s LocationOfFirmware    — path or "" for device.properties default
     *   s TypeOfFirmware        — PCI / PDRI / PERIPHERAL
     *   s rebootImmediately     — "true" or "false" (daemon expects string)
     *
     * Three trailing NULLs = fire and forget.
     */

    g_dbus_connection_call(
        conn,
        DBUS_SERVICE_NAME,
        DBUS_OBJECT_PATH,
        DBUS_INTERFACE_NAME,
        DBUS_METHOD_UPDATE,                         /* method: UpdateFirmware     */
        g_variant_new("(sssss)",                                  /* ✅ 5 strings now! */
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

    g_object_unref(conn);

    FWUPMGR_INFO("updateFirmware: D-Bus call sent, returning SUCCESS. "
                 "handle='%s'\n", handle);

    /* [4] Return immediately — app is unblocked */
    return RDKFW_UPDATE_SUCCESS;
}
