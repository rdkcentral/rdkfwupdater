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
    /*
     * [STEP 1] INPUT VALIDATION
     *
     * Reject obviously bad inputs before touching D-Bus or the registry.
     * This is the library's input boundary -- validate everything here.
     * downloadFirmware has MORE validation than checkForUpdate because
     * it also validates the request struct fields (not just handle+callback).
     */

    /*
     * Check 1a: handle must not be NULL and must not be empty "".
     *
     * handle is the string "1" from registerProcess(). If the caller
     * passes NULL (forgot to register first) or an empty string,
     * reject immediately. The daemon would reject it too, but we
     * save the D-Bus round-trip.
     */
    if (handle == NULL || handle[0] == '\0') {
        FWUPMGR_ERROR("downloadFirmware: invalid handle (NULL or empty)\n");
        return RDKFW_DWNL_FAILED;
    }

    /*
     * Check 1b: the request struct pointer must not be NULL.
     *
     * This catches the case where the caller passes NULL instead of
     * &download_req. Dereferencing NULL would crash.
     */
    if (fwdwnlreq == NULL) {
        FWUPMGR_ERROR("downloadFirmware: fwdwnlreq is NULL\n");
        return RDKFW_DWNL_FAILED;
    }

    /*
     * Check 1c: firmwareName pointer must not be NULL.
     *
     * FwDwnlReq.firmwareName is a const char* -- it could be NULL if
     * the caller forgot to set it. We need a filename to tell the
     * daemon WHAT to download.
     */
    if (fwdwnlreq->firmwareName == NULL) {
        FWUPMGR_ERROR("downloadFirmware: firmwareName is NULL\n");
        return RDKFW_DWNL_FAILED;
    }

    /*
     * Check 1d: firmwareName must not be an empty string "".
     *
     * An empty filename is meaningless -- the daemon can't download "".
     * This catches the case where the caller did:
     *   download_req.firmwareName = "";   // accident
     */
    if (fwdwnlreq->firmwareName[0] == '\0') {
        FWUPMGR_ERROR("downloadFirmware: firmwareName is empty\n");
        return RDKFW_DWNL_FAILED;
    }

    /*
     * Check 1e: callback must not be NULL.
     *
     * Without a callback, the app can't receive progress updates.
     * It would never know when the download finishes. That's always
     * a programming error.
     */
    if (callback == NULL) {
        FWUPMGR_ERROR("downloadFirmware: callback is NULL\n");
        return RDKFW_DWNL_FAILED;
    }

    /*
     * Log what we're about to download. The ternary expressions handle
     * optional fields: TypeOfFirmware and downloadUrl may be NULL
     * (they're optional in FwDwnlReq).
     */
    FWUPMGR_INFO("downloadFirmware: handle='%s' firmware='%s' type='%s' url='%s'\n",
                 handle, 
                 fwdwnlreq->firmwareName,
                 (fwdwnlreq->TypeOfFirmware && fwdwnlreq->TypeOfFirmware[0]) ? fwdwnlreq->TypeOfFirmware : "(none)",
                 (fwdwnlreq->downloadUrl && fwdwnlreq->downloadUrl[0]) ? fwdwnlreq->downloadUrl : "(use XConf)");

    /*
     * [STEP 2] CREATE EPHEMERAL D-BUS CONNECTION
     *
     * g_bus_get_sync(G_BUS_TYPE_SYSTEM, ...) opens a new connection to
     * the system D-Bus bus. Gets a unique sender name like :1.143.
     *
     * Why a NEW connection instead of reusing the BG thread's :1.141?
     *   The BG thread's connection is attached to the BG thread's
     *   GMainContext. Using it from the main thread would require
     *   cross-thread GLib context management -- complex and fragile.
     *   A fresh per-call connection is simpler and safe.
     *
     * Why BEFORE registering the callback?
     *   If D-Bus is down (dbus-daemon crashed, socket missing), this
     *   call fails. We want to fail BEFORE polluting the download
     *   registry with an ACTIVE entry that will never be dispatched.
     *   Clean failure: no registry entry, no dangling state.
     *
     * Cost: ~2ms for the D-Bus handshake. Negligible for a firmware
     * download that takes minutes.
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
         * Return FAILED -- no registry entry created, nothing to clean up.
         */
        FWUPMGR_ERROR("downloadFirmware: D-Bus connect failed: %s\n",
                      error ? error->message : "unknown");
        if (error) g_error_free(error);
        return RDKFW_DWNL_FAILED;
    }

    /*
     * [STEP 3] REGISTER CALLBACK IN THE DOWNLOAD REGISTRY (g_dwnl_registry)
     *
     * internal_dwnl_register_callback() does the following (see _async.c):
     *   1. Locks g_dwnl_registry.mutex
     *   2. Scans all 30 DwnlCallbackEntry slots for:
     *      a. An existing ACTIVE entry with the same handle (dedup/overwrite)
     *      b. The first IDLE slot (free slot)
     *   3. If same handle found: overwrites it (prevents stale callbacks)
     *      If free slot found: uses it
     *      If neither: returns false (registry full -- 30 concurrent downloads!)
     *   4. Populates the slot:
     *      - handle_key = strdup(handle)    -- "1" (heap copy)
     *      - callback = our function pointer
     *      - state = DWNL_CB_STATE_ACTIVE   -- NOTE: ACTIVE, not PENDING!
     *      - registered_time = current unix timestamp
     *   5. Unlocks g_dwnl_registry.mutex
     *   6. Returns true
     *
     * After this call, the registry has one ACTIVE entry. When the BG
     * thread receives DownloadProgress signals, it will find this entry
     * and invoke the callback on EVERY signal.
     *
     * STATE DIFFERENCE FROM checkForUpdate:
     *   checkForUpdate sets state = CB_STATE_PENDING  (fires once)
     *   downloadFirmware sets state = DWNL_CB_STATE_ACTIVE (fires repeatedly)
     *   There is NO "DISPATCHED" intermediate state for download.
     *
     * Why BEFORE the D-Bus call?
     *   Race condition prevention. If the daemon starts downloading
     *   instantly (file already cached), the BG thread would receive
     *   the first DownloadProgress signal before we registered.
     *   dispatch_all_dwnl_active() would find zero ACTIVE entries
     *   and silently discard the signal. Our callback would never fire.
     *
     * Failure case: registry full (30 concurrent pending downloads).
     *   In practice never happens -- a device downloads one firmware
     *   at a time. If it does, clean up and fail.
     */
    if (!internal_dwnl_register_callback(handle, callback)) {
        FWUPMGR_ERROR("downloadFirmware: registry full, handle='%s'\n", handle);
        g_object_unref(conn);
        return RDKFW_DWNL_FAILED;
    }

    /*
     * [STEP 4] SEND FIRE-AND-FORGET D-BUS METHOD CALL
     *
     * g_dbus_connection_call() sends a D-Bus method call to the daemon.
     *
     * Parameters to g_dbus_connection_call():
     *   conn              -- our ephemeral connection :1.143
     *   DBUS_SERVICE_NAME -- "org.rdkfwupdater.Service" (daemon's well-known name)
     *   DBUS_OBJECT_PATH  -- "/org/rdkfwupdater/Service" (object path)
     *   DBUS_INTERFACE_NAME -- "org.rdkfwupdater.Interface"
     *   DBUS_METHOD_DOWNLOAD -- "DownloadFirmware" (the method name)
     *   g_variant_new("(ssss)", ...) -- 4-string argument tuple:
     *     "(ssss)" means a tuple containing four strings
     *     string 1: handle -- "1" (which registered client is asking)
     *     string 2: firmwareName -- "firmware_v8.bin" (what to download)
     *     string 3: downloadUrl -- URL or "" (where to download from)
     *     string 4: TypeOfFirmware -- "PCI" or "" (firmware type category)
     *   NULL              -- expected reply type: we don't care
     *   G_DBUS_CALL_FLAGS_NONE -- no special flags
     *   DBUS_TIMEOUT_MS   -- 5000ms (only for message queueing, not reply)
     *   NULL              -- GCancellable: no cancellation support
     *   NULL              -- GAsyncReadyCallback: no reply callback
     *   NULL              -- user_data for reply callback: N/A
     *
     * The three trailing NULLs make this fire-and-forget. GLib queues
     * the D-Bus message in the kernel's socket buffer and returns.
     *
     * NULL-COALESCING for optional fields:
     *   fwdwnlreq->downloadUrl ? fwdwnlreq->downloadUrl : ""
     *   If downloadUrl is NULL (caller didn't set it), we send ""
     *   to the daemon. The daemon treats "" as "use the XConf URL
     *   that was returned during checkForUpdate." Same for TypeOfFirmware.
     *
     * WHAT HAPPENS ON THE DAEMON SIDE:
     *   1. Daemon receives "DownloadFirmware" with 4 string arguments
     *   2. Validates handler "1" is registered (from registerProcess)
     *   3. Starts downloading firmware_v8.bin from the URL
     *   4. As download progresses, broadcasts DownloadProgress signals:
     *      - (1, "firmware_v8.bin", 0, "NOTSTARTED", "Download queued")
     *      - (1, "firmware_v8.bin", 10, "INPROGRESS", "10% downloaded")
     *      - (1, "firmware_v8.bin", 50, "INPROGRESS", "50% downloaded")
     *      - (1, "firmware_v8.bin", 100, "COMPLETED", "Download complete")
     *   5. Our BG thread catches each signal and fires our callback
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

    /*
     * [STEP 5] CLOSE EPHEMERAL CONNECTION AND RETURN
     *
     * g_object_unref(conn) closes our ephemeral D-Bus connection :1.143.
     * The D-Bus message is already in the kernel socket buffer -- closing
     * our end doesn't prevent delivery to the daemon.
     *
     * After this, the state of the world is:
     *
     *   Main thread:
     *     - Returns RDKFW_DWNL_SUCCESS to the caller
     *     - Caller enters pthread_cond_timedwait (typically 300s timeout)
     *     - Connection :1.143 is DEAD (just closed)
     *
     *   BG thread:
     *     - Still sleeping in g_main_loop_run() on connection :1.141
     *     - g_dwnl_registry.entries[0] has our callback in ACTIVE state
     *     - Will wake up on EVERY DownloadProgress signal
     *     - Will call our callback MULTIPLE TIMES
     *
     *   Daemon:
     *     - Received our request, started downloading the firmware
     *     - Will broadcast DownloadProgress signals as download progresses
     *
     *   g_dwnl_registry (download-specific, separate from g_registry):
     *     entries[0] = { state=ACTIVE, handle_key="1",
     *                    callback=on_download_progress_callback }
     *     entries[1..29] = IDLE
     *
     *   D-Bus connections:
     *     :1.141 -- BG thread persistent (ALIVE, listening for signals)
     *     :1.143 -- this downloadFirmware ephemeral (DEAD, just closed)
     *
     * SUCCESS here means: "I sent the request and registered your callback."
     * It does NOT mean: "Download started." or "File exists on server."
     * The callback will fire later with actual progress.
     */
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
    /*
     * [STEP 1] INPUT VALIDATION
     *
     * Reject obviously bad inputs before touching D-Bus or the registry.
     * This is the library's input boundary -- validate everything here.
     *
     * updateFirmware has the MOST validation of all three APIs:
     *   checkForUpdate:   2 checks (handle, callback)
     *   downloadFirmware: 5 checks (handle, struct, firmwareName, empty, callback)
     *   updateFirmware:   7 checks (handle, struct, firmwareName, firmwareName
     *                               empty, TypeOfFirmware, TypeOfFirmware empty,
     *                               callback)
     *
     * TypeOfFirmware is required here (unlike downloadFirmware where it's
     * optional) because the daemon needs to know HOW to flash the image
     * (different flash paths for PCI vs PDRI vs PERIPHERAL).
     */

    /*
     * Check 1a: handle must not be NULL and must not be empty "".
     *
     * handle is the string returned by registerProcess(), e.g. "1".
     * If the caller passes NULL (forgot to register first) or an
     * empty string, reject immediately. The daemon would reject it
     * too, but we save the D-Bus round-trip.
     */
    if (handle == NULL || handle[0] == '\0') {
        FWUPMGR_ERROR("updateFirmware: invalid handle (NULL or empty)\n");
        return RDKFW_UPDATE_FAILED;
    }

    /*
     * Check 1b: the request struct pointer must not be NULL.
     *
     * This catches the case where the caller passes NULL instead of
     * &update_req. Dereferencing NULL would crash the process.
     */
    if (fwupdatereq == NULL) {
        FWUPMGR_ERROR("updateFirmware: fwupdatereq is NULL\n");
        return RDKFW_UPDATE_FAILED;
    }

    /*
     * Check 1c: firmwareName pointer must not be NULL.
     *
     * FwUpdateReq.firmwareName is a const char* -- it could be NULL if
     * the caller forgot to set it. We need a filename to tell the
     * daemon WHAT image to flash.
     */
    if (fwupdatereq->firmwareName == NULL) {
        FWUPMGR_ERROR("updateFirmware: firmwareName is NULL\n");
        return RDKFW_UPDATE_FAILED;
    }

    /*
     * Check 1d: firmwareName must not be an empty string "".
     *
     * An empty filename is meaningless -- the daemon can't flash "".
     * This catches the case where the caller did:
     *   update_req.firmwareName = "";   // accident
     */
    if (fwupdatereq->firmwareName[0] == '\0') {
        FWUPMGR_ERROR("updateFirmware: firmwareName is empty\n");
        return RDKFW_UPDATE_FAILED;
    }

    /*
     * Check 1e: TypeOfFirmware pointer must not be NULL.
     *
     * Unlike downloadFirmware (where TypeOfFirmware is optional),
     * updateFirmware REQUIRES TypeOfFirmware because the daemon
     * uses it to select the correct flash mechanism:
     *   "PCI"        -- flash to the main chipset
     *   "PDRI"       -- flash to PDRI (Platform Data Recovery Image)
     *   "PERIPHERAL" -- flash to a connected peripheral device
     * Without this, the daemon doesn't know HOW to flash the image.
     */
    if (fwupdatereq->TypeOfFirmware == NULL) {
        FWUPMGR_ERROR("updateFirmware: TypeOfFirmware is NULL\n");
        return RDKFW_UPDATE_FAILED;
    }

    /*
     * Check 1f: TypeOfFirmware must not be empty "".
     *
     * Same reasoning as check 1e -- an empty string provides no
     * flash-type information. Catches:
     *   update_req.TypeOfFirmware = "";   // accident
     */
    if (fwupdatereq->TypeOfFirmware[0] == '\0') {
        FWUPMGR_ERROR("updateFirmware: TypeOfFirmware is empty\n");
        return RDKFW_UPDATE_FAILED;
    }

    /*
     * Check 1g: callback must not be NULL.
     *
     * Without a callback, the app can't receive flash progress updates.
     * It would never know when the update finishes (or if it failed).
     * That's always a programming error.
     */
    if (callback == NULL) {
        FWUPMGR_ERROR("updateFirmware: callback is NULL\n");
        return RDKFW_UPDATE_FAILED;
    }

    /*
     * Log what we're about to flash. The ternary expressions handle
     * optional fields:
     *   LocationOfFirmware may be NULL (it's optional in FwUpdateReq).
     *   If NULL or empty, the daemon uses the path from device.properties.
     *   rebootImmediately is a bool, logged as "yes"/"no" for clarity.
     */
    FWUPMGR_INFO("updateFirmware: handle='%s' firmware='%s' type='%s' "
                 "location='%s' reboot=%s\n",
                 handle,
                 fwupdatereq->firmwareName,
                 fwupdatereq->TypeOfFirmware,
                 (fwupdatereq->LocationOfFirmware && fwupdatereq->LocationOfFirmware[0])
                     ? fwupdatereq->LocationOfFirmware
                     : "(use device.properties path)",
                 fwupdatereq->rebootImmediately ? "yes" : "no");

    /*
     * [STEP 2] CREATE EPHEMERAL D-BUS CONNECTION
     *
     * g_bus_get_sync(G_BUS_TYPE_SYSTEM, ...) opens a new connection to
     * the system D-Bus bus. Gets a unique sender name like :1.143.
     *
     * Why a NEW connection instead of reusing the BG thread's :1.141?
     *   The BG thread's connection is attached to the BG thread's
     *   GMainContext. Using it from the main thread would require
     *   cross-thread GLib context management -- complex and fragile.
     *   A fresh per-call connection is simpler and safe.
     *
     * Why BEFORE registering the callback?
     *   If D-Bus is down (dbus-daemon crashed, socket missing), this
     *   call fails. We want to fail BEFORE polluting the update
     *   registry with an ACTIVE entry that will never be dispatched.
     *   Clean failure: no registry entry, no dangling state.
     *
     * Cost: ~2ms for the D-Bus handshake. Negligible for a firmware
     * update that takes minutes.
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
         * Return FAILED -- no registry entry created, nothing to clean up.
         */
        FWUPMGR_ERROR("updateFirmware: D-Bus connect failed: %s\n",
                      error ? error->message : "unknown");
        if (error) g_error_free(error);
        return RDKFW_UPDATE_FAILED;
    }

    /*
     * [STEP 3] REGISTER CALLBACK IN THE UPDATE REGISTRY (g_update_registry)
     *
     * internal_update_register_callback() does the following (see _async.c):
     *   1. Locks g_update_registry.mutex
     *   2. Scans all 30 UpdateCbEntry slots for:
     *      a. An existing ACTIVE entry with the same handle (dedup/overwrite)
     *      b. The first IDLE slot (free slot)
     *   3. If same handle found: overwrites it (prevents stale callbacks)
     *      If free slot found: uses it
     *      If neither: returns false (registry full -- 30 concurrent updates!)
     *   4. Populates the slot:
     *      - handle_key = strdup(handle)    -- "1" (heap copy)
     *      - callback = our function pointer
     *      - state = UPDATE_CB_STATE_ACTIVE -- NOTE: ACTIVE, not PENDING!
     *      - registered_time = current unix timestamp
     *   5. Unlocks g_update_registry.mutex
     *   6. Returns true
     *
     * After this call, the registry has one ACTIVE entry. When the BG
     * thread receives UpdateProgress signals, it will find this entry
     * and invoke the callback on EVERY signal.
     *
     * STATE DIFFERENCE FROM THE OTHER TWO REGISTRIES:
     *   checkForUpdate:   g_registry       -- state = CB_STATE_PENDING (fires ONCE)
     *   downloadFirmware: g_dwnl_registry  -- state = DWNL_CB_STATE_ACTIVE (fires MANY)
     *   updateFirmware:   g_update_registry -- state = UPDATE_CB_STATE_ACTIVE (fires MANY)
     *   Download and update both use the ACTIVE-until-terminal pattern.
     *   checkForUpdate uses the PENDING->DISPATCHED->IDLE one-shot pattern.
     *
     * Why BEFORE the D-Bus call?
     *   Race condition prevention. If the daemon starts flashing
     *   instantly, the BG thread would receive the first UpdateProgress
     *   signal before we registered. dispatch_all_update_active() would
     *   find zero ACTIVE entries and silently discard the signal. Our
     *   callback would never fire. The app would hang forever.
     *
     * Failure case: registry full (30 concurrent pending updates).
     *   In practice never happens -- a device flashes one firmware
     *   at a time. If it does, clean up the D-Bus connection and fail.
     */
    if (!internal_update_register_callback(handle, callback)) {
        FWUPMGR_ERROR("updateFirmware: registry full, handle='%s'\n", handle);
        /*
         * Clean up: close the D-Bus connection we opened in Step 2.
         * No registry entry was created, so no registry cleanup needed.
         */
        g_object_unref(conn);
        return RDKFW_UPDATE_FAILED;
    }

    /*
     * [STEP 4] SEND FIRE-AND-FORGET D-BUS METHOD CALL
     *
     * g_dbus_connection_call() sends a D-Bus method call to the daemon.
     *
     * Parameters to g_dbus_connection_call():
     *   conn              -- our ephemeral connection :1.143
     *   DBUS_SERVICE_NAME -- "org.rdkfwupdater.Service" (daemon's well-known name)
     *   DBUS_OBJECT_PATH  -- "/org/rdkfwupdater/Service" (object path)
     *   DBUS_INTERFACE_NAME -- "org.rdkfwupdater.Interface"
     *   DBUS_METHOD_UPDATE -- "UpdateFirmware" (the method name)
     *   g_variant_new("(sssss)", ...) -- 5-string argument tuple:
     *     "(sssss)" means a tuple containing five strings
     *     string 1: handle              -- "1" (which registered client is asking)
     *     string 2: firmwareName        -- "firmware_v8.bin" (what to flash)
     *     string 3: LocationOfFirmware  -- path to image or "" for default
     *     string 4: TypeOfFirmware      -- "PCI" / "PDRI" / "PERIPHERAL"
     *     string 5: rebootImmediately   -- "true" or "false" (see note below)
     *   NULL              -- expected reply type: we don't care
     *   G_DBUS_CALL_FLAGS_NONE -- no special flags
     *   DBUS_TIMEOUT_MS   -- 5000ms (only for message queueing, not reply)
     *   NULL              -- GCancellable: no cancellation support
     *   NULL              -- GAsyncReadyCallback: no reply callback
     *   NULL              -- user_data for reply callback: N/A
     *
     * The three trailing NULLs make this fire-and-forget. GLib queues
     * the D-Bus message in the kernel's socket buffer and returns.
     *
     * WHY 5 STRINGS (NOT 4 LIKE downloadFirmware):
     *   updateFirmware sends an extra argument: rebootImmediately.
     *   The FwUpdateReq struct has rebootImmediately as a bool (true/false),
     *   but the daemon's D-Bus interface is defined with all-string
     *   arguments. So we convert: true -> "true", false -> "false"
     *   using the ternary: fwupdatereq->rebootImmediately ? "true" : "false"
     *
     * NULL-COALESCING for optional field:
     *   fwupdatereq->LocationOfFirmware ? fwupdatereq->LocationOfFirmware : ""
     *   If LocationOfFirmware is NULL (caller didn't set it), we send ""
     *   to the daemon. The daemon treats "" as "use the path from
     *   device.properties." TypeOfFirmware is NOT coalesced because it's
     *   required (validated in Step 1e/1f above).
     *
     * WHAT HAPPENS ON THE DAEMON SIDE:
     *   1. Daemon receives "UpdateFirmware" with 5 string arguments
     *   2. Validates handler "1" is registered (from registerProcess)
     *   3. Starts flashing the firmware image using the appropriate method
     *   4. As flashing progresses, broadcasts UpdateProgress signals:
     *      - (1, "firmware_v8.bin", 0,  0, "Flash started")
     *      - (1, "firmware_v8.bin", 25, 0, "Writing partition 1")
     *      - (1, "firmware_v8.bin", 50, 0, "Writing partition 2")
     *      - (1, "firmware_v8.bin", 100, 1, "Flash complete")
     *      Signal format: "(tsiis)" where:
     *        t = handler_id (uint64), s = firmware_name,
     *        i = progress (int32 0-100), i = status_code (0/1/2),
     *        s = message
     *   5. Our BG thread catches each signal and fires our callback
     */

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

    /*
     * [STEP 5] CLOSE EPHEMERAL CONNECTION AND RETURN
     *
     * g_object_unref(conn) closes our ephemeral D-Bus connection :1.143.
     * The D-Bus message is already in the kernel socket buffer -- closing
     * our end doesn't prevent delivery to the daemon.
     *
     * After this, the state of the world is:
     *
     *   Main thread:
     *     - Returns RDKFW_UPDATE_SUCCESS to the caller
     *     - Caller enters pthread_cond_timedwait (typically 300s+ timeout)
     *     - Connection :1.143 is DEAD (just closed)
     *
     *   BG thread:
     *     - Still sleeping in g_main_loop_run() on connection :1.141
     *     - g_update_registry.entries[0] has our callback in ACTIVE state
     *     - Will wake up on EVERY UpdateProgress signal
     *     - Will call our callback MULTIPLE TIMES
     *
     *   Daemon:
     *     - Received our request, started flashing the firmware
     *     - Will broadcast UpdateProgress signals as flashing progresses
     *
     *   g_update_registry (update-specific, third registry):
     *     entries[0] = { state=ACTIVE, handle_key="1",
     *                    callback=on_update_progress_callback }
     *     entries[1..29] = IDLE
     *
     *   D-Bus connections:
     *     :1.141 -- BG thread persistent (ALIVE, listening for signals)
     *     :1.143 -- this updateFirmware ephemeral (DEAD, just closed)
     *
     * SUCCESS here means: "I sent the request and registered your callback."
     * It does NOT mean: "Flashing started." or "Image is valid."
     * The callback will fire later with actual progress.
     */
    g_object_unref(conn);

    FWUPMGR_INFO("updateFirmware: D-Bus call sent, returning SUCCESS. "
                 "handle='%s'\n", handle);

    return RDKFW_UPDATE_SUCCESS;
}
