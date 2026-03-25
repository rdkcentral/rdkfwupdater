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
 * CHECKFORUPDATE (Phase 1 - on-demand worker thread):
 * ====================================================
 * 1. Validate handle and callback
 * 2. Reject if another checkForUpdate is already in progress
 * 3. Allocate CheckRequestContext on heap
 * 4. Spawn worker thread (internal_check_worker_thread)
 * 5. Wait for worker to signal "ready" via condvar (~10-100ms)
 * 6. Return SUCCESS or FAIL immediately
 *
 * [Later - typically 5-30 seconds, max 120 seconds]
 * Worker thread receives CheckForUpdateComplete signal from daemon
 * → Parses payload → Fires client callback with FwInfoData
 * → Cleans up all resources → Thread exits
 *
 * DOWNLOAD FIRMWARE (Phase 2 - on-demand worker thread):
 * =======================================================
 * 1. Validate handle, request, and callback
 * 2. Reject if another downloadFirmware is already in progress
 * 3. Allocate DownloadRequestContext on heap
 * 4. Spawn worker thread (internal_download_worker_thread)
 * 5. Wait for worker to signal "ready" via condvar (~50-200ms, includes daemon reply)
 * 6. Return SUCCESS or FAIL (accurate — reflects daemon's accept/reject)
 *
 * [Later - typically 1-30 minutes, max 3600 seconds]
 * Worker thread receives DownloadProgress signals from daemon
 * → Fires client callback MULTIPLE TIMES (per progress signal)
 * → Quits loop on COMPLETED/ERROR → Cleans up → Thread exits
 *
 * UPDATE FIRMWARE (unchanged — persistent BG thread):
 * =====================================================
 * Same fire-and-forget pattern as before.
 * Callbacks registered in registry, dispatched from background thread.
 */

#include "rdkFwupdateMgr_client.h"
#include "rdkFwupdateMgr_async_internal.h"
#include "rdkFwupdateMgr_log.h"
#include <gio/gio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

/* No extern globals needed — all CheckForUpdate state is accessed through
 * internal_begin_check() / internal_end_check() / internal_abort_check()
 * / internal_is_check_in_progress() declared in rdkFwupdateMgr_async_internal.h.
 * The mutex and state variables are static inside rdkFwupdateMgr_async.c.
 */

/* ========================================================================
 * checkForUpdate - ON-DEMAND WORKER THREAD implementation (Phase 1)
 * ======================================================================== */

/**
 * @brief Check for firmware update - spawns on-demand worker thread
 *
 * Allocates a CheckRequestContext, spawns a worker thread that connects
 * to D-Bus, subscribes to CheckForUpdateComplete signal, sends the
 * CheckForUpdate method call, and waits for the response. The caller
 * blocks briefly (typically <100ms) until the worker signals "ready",
 * then returns immediately. The callback fires asynchronously in the
 * worker thread when the daemon responds (5s to 2min+).
 *
 * INVARIANTS:
 *   - At most one checkForUpdate() in progress per process
 *   - Callback fires exactly once (on signal) or zero times (on timeout/error)
 *   - Worker thread is self-contained: creates and destroys all its resources
 *   - No interaction with the persistent background thread
 *
 * @param handle    Valid FirmwareInterfaceHandle from registerProcess()
 * @param callback  Invoked when CheckForUpdateComplete signal arrives
 * @return CHECK_FOR_UPDATE_SUCCESS or CHECK_FOR_UPDATE_FAIL
 */
CheckForUpdateResult checkForUpdate(FirmwareInterfaceHandle handle,
                                    UpdateEventCallback callback)
{
    /* [1] Validate handle - must be non-NULL and non-empty (daemon would reject it anyway) */
    if (handle == NULL || handle[0] == '\0') {
        FWUPMGR_ERROR("checkForUpdate: invalid handle (NULL or empty)\n");
        return CHECK_FOR_UPDATE_FAIL;
    }

    /* [2] Validate callback - NULL callback means we'd have no way to deliver results */
    if (callback == NULL) {
        FWUPMGR_ERROR("checkForUpdate: callback is NULL\n");
        return CHECK_FOR_UPDATE_FAIL;
    }

    FWUPMGR_INFO("checkForUpdate: handle='%s'\n", handle);

    /* [3] Reject duplicate: only one checkForUpdate at a time per process.
     *
     * If a worker thread is already running, a second checkForUpdate()
     * would create two threads both listening for the same D-Bus signal.
     * wasteful and confusing (the app would get duplicate callbacks with
     * identical data). So we reject it immediately.
     */

    /* [4] Allocate per-request context on heap
     *
     * TL;DR: We allocate FIRST, then call internal_begin_check() to atomically
     * set in-progress + track ctx. This way there's no window where in-progress
     * is true but ctx isn't ready. If allocation fails, we just free and return
     * without touching any global state.
     */
    CheckRequestContext *ctx = calloc(1, sizeof(CheckRequestContext));
    if (ctx == NULL) {
        FWUPMGR_ERROR("checkForUpdate: calloc failed for ctx\n");
        return CHECK_FOR_UPDATE_FAIL;
    }

    ctx->handle_key = strdup(handle);
    if (ctx->handle_key == NULL) {
        FWUPMGR_ERROR("checkForUpdate: strdup failed for handle\n");
        free(ctx);
        return CHECK_FOR_UPDATE_FAIL;
    }

    ctx->callback    = callback;
    ctx->is_ready    = false;
    ctx->init_failed = false;

    if (pthread_mutex_init(&ctx->ready_mutex, NULL) != 0) {
        FWUPMGR_ERROR("checkForUpdate: ready_mutex init failed\n");
        free(ctx->handle_key);
        free(ctx);
        return CHECK_FOR_UPDATE_FAIL;
    }

    if (pthread_cond_init(&ctx->ready_cond, NULL) != 0) {
        FWUPMGR_ERROR("checkForUpdate: ready_cond init failed\n");
        pthread_mutex_destroy(&ctx->ready_mutex);
        free(ctx->handle_key);
        free(ctx);
        return CHECK_FOR_UPDATE_FAIL;
    }

    /* [5] Atomically begin the check session: set in-progress + track ctx.
     *
     * TL;DR: internal_begin_check() does the duplicate rejection AND the
     * context tracking in one mutex-protected operation. If another check
     * is already active, it returns false and we clean up locally. The
     * globals are never in an inconsistent state.
     */
    if (!internal_begin_check(ctx)) {
        FWUPMGR_WARN("checkForUpdate: already in progress, rejecting. "
                     "handle='%s'\n", handle);
        pthread_cond_destroy(&ctx->ready_cond);
        pthread_mutex_destroy(&ctx->ready_mutex);
        free(ctx->handle_key);
        free(ctx);
        return CHECK_FOR_UPDATE_FAIL;
    }

    /* [7] Spawn worker thread — ownership of ctx transfers to worker
     *
     * The worker thread will set up D-Bus, subscribe to signals,
     * send the CheckForUpdate request, and wait for the daemon's response.
     * If pthread_create fails, we undo the begin_check and return FAIL.
     */
    if (pthread_create(&ctx->thread, NULL, internal_check_worker_thread, ctx) != 0) {
        FWUPMGR_ERROR("checkForUpdate: pthread_create failed\n");
        internal_abort_check();
        pthread_cond_destroy(&ctx->ready_cond);
        pthread_mutex_destroy(&ctx->ready_mutex);
        free(ctx->handle_key);
        free(ctx);
        return CHECK_FOR_UPDATE_FAIL;
    }

    /* [8] Wait for worker to signal ready (no timeout — see §5.1)
     *
     * This blocks the caller for ~10-100ms while the worker sets up
     * its D-Bus connection and signal subscription. The worker signals
     * is_ready=true when it's either ready or has failed to init.
     */
    pthread_mutex_lock(&ctx->ready_mutex);
    while (!ctx->is_ready) {
        pthread_cond_wait(&ctx->ready_cond, &ctx->ready_mutex);
    }
    bool failed = ctx->init_failed;
    pthread_mutex_unlock(&ctx->ready_mutex);

    /* [9] Check if worker failed to initialize
     *
     * The worker tried to connect to D-Bus and subscribe to signals.
     * If that failed (D-Bus dead, system error), init_failed is true.
     * We join the worker (it's already exiting) and return FAIL to the app.
     * The worker handles its own cleanup - we just wait for it to finish.
     */
    if (failed) {
        FWUPMGR_ERROR("checkForUpdate: worker thread failed to initialize. "
                     "handle='%s'\n", handle);
        /*
         * Worker thread will clean itself up (free ctx, reset g_check_in_progress).
         * We just need to join it to avoid a zombie thread.
         * But the worker signals ready BEFORE going to cleanup, so we must
         * wait for it to actually exit.
         */
        pthread_join(ctx->thread, NULL);
        return CHECK_FOR_UPDATE_FAIL;
    }

    /* [10] Worker is running and listening. Return success to caller.
     *
     * From this point, the caller NEVER touches ctx again.
     * The worker thread is the sole owner and will free it after
     * the callback fires or the timeout expires.
     */
    FWUPMGR_INFO("checkForUpdate: worker thread started, returning SUCCESS. "
                 "Callback will fire when daemon responds. handle='%s'\n",
                 handle);

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
 * Stops any active CheckForUpdate and DownloadFirmware worker threads,
 * then stops the persistent background thread and frees all resources cleanly.
 */
__attribute__((destructor))
static void rdkFwupdateMgr_lib_deinit(void)
{
    FWUPMGR_INFO("=== rdkFwupdateMgr library unloading ===\n");

    /* Phase 1: Cancel and join any active CheckForUpdate worker thread.
     *
     * Must happen BEFORE internal_system_deinit() because the worker
     * may be using a D-Bus connection. If we tore down the BG thread
     * first, ordering issues could arise.
     */
    internal_cancel_all_active_check_threads();

    /* Phase 2: Cancel and join any active DownloadFirmware worker thread.
     *
     * Same rationale — must join before library code is unmapped.
     * Download workers can be long-lived (up to 1 hour) so this may
     * block briefly while the worker cleans up after g_main_loop_quit().
     */
    internal_cancel_all_active_download_threads();

    /* Phase 3 (future): Cancel active UpdateFirmware worker */
    /* internal_cancel_all_active_update_threads(); */

    /* Persistent BG thread cleanup (still needed for Update in Phase 2) */
    internal_system_deinit();
    FWUPMGR_INFO("=== rdkFwupdateMgr library unloaded ===\n");
}

/* ========================================================================
 * DOWNLOAD FIRMWARE PUBLIC API — ON-DEMAND WORKER THREAD (Phase 2)
 * ========================================================================
 *
 * Implements:
 *   DownloadResult downloadFirmware(FirmwareInterfaceHandle handle,
 *                                   const FwDwnlReq *fwdwnlreq,
 *                                   DownloadCallback callback);
 *
 * FLOW:
 *   1. Validate: handle, request fields, callback
 *   2. Allocate DownloadRequestContext on heap
 *   3. internal_begin_download(ctx) — reject if already active
 *   4. Spawn worker thread (internal_download_worker_thread)
 *   5. Wait for condvar — worker sets up D-Bus + calls daemon synchronously
 *   6. Check daemon reply: accepted → SUCCESS, rejected → FAIL
 *
 *   [later — fires multiple times over 1-30 minutes]
 *   Worker receives DownloadProgress signals → fires callback each time
 *   → quits loop on COMPLETED/ERROR → cleanup → thread exits
 * ======================================================================== */

/**
 * @brief Initiate firmware download — spawns on-demand worker thread
 *
 * Allocates a DownloadRequestContext, spawns a worker thread that connects
 * to D-Bus, subscribes to DownloadProgress signal, sends DownloadFirmware
 * method call SYNCHRONOUSLY, and reads the daemon's reply. The caller
 * blocks briefly (~50-200ms) until the worker signals "ready", then
 * returns immediately with an ACCURATE result reflecting the daemon's
 * accept/reject decision.
 *
 * INVARIANTS:
 *   - At most one downloadFirmware() in progress per process
 *   - Callback fires N times (per progress signal) or 0 times (on error)
 *   - Worker thread is self-contained: creates and destroys all its resources
 *   - No interaction with the persistent background thread
 *
 * @param handle      Valid FirmwareInterfaceHandle from registerProcess()
 * @param fwdwnlreq   Download request details (firmware name, URL, type)
 * @param callback    Invoked on each DownloadProgress signal
 * @return RDKFW_DWNL_SUCCESS or RDKFW_DWNL_FAILED
 */
DownloadResult downloadFirmware(FirmwareInterfaceHandle handle,
                                const FwDwnlReq *fwdwnlreq,
                                DownloadCallback callback)
{
    /* [1] Validate handle */
    if (handle == NULL || handle[0] == '\0') {
        FWUPMGR_ERROR("downloadFirmware: invalid handle (NULL or empty)\n");
        return RDKFW_DWNL_FAILED;
    }

    /* [2] Validate request */
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

    /* [3] Validate callback */
    if (callback == NULL) {
        FWUPMGR_ERROR("downloadFirmware: callback is NULL\n");
        return RDKFW_DWNL_FAILED;
    }

    FWUPMGR_INFO("downloadFirmware: handle='%s' firmware='%s' type='%s' url='%s'\n",
                 handle,
                 fwdwnlreq->firmwareName,
                 (fwdwnlreq->TypeOfFirmware && fwdwnlreq->TypeOfFirmware[0]) ? fwdwnlreq->TypeOfFirmware : "(none)",
                 (fwdwnlreq->downloadUrl && fwdwnlreq->downloadUrl[0]) ? fwdwnlreq->downloadUrl : "(use XConf)");

    /* [4] Allocate per-request context on heap
     *
     * We allocate FIRST, then call internal_begin_download() to atomically
     * set in-progress + track ctx. This way there's no window where in-progress
     * is true but ctx isn't ready.
     */
    DownloadRequestContext *ctx = calloc(1, sizeof(DownloadRequestContext));
    if (ctx == NULL) {
        FWUPMGR_ERROR("downloadFirmware: calloc failed for ctx\n");
        return RDKFW_DWNL_FAILED;
    }

    ctx->handle_key = strdup(handle);
    if (ctx->handle_key == NULL) {
        FWUPMGR_ERROR("downloadFirmware: strdup failed for handle\n");
        free(ctx);
        return RDKFW_DWNL_FAILED;
    }

    ctx->firmware_name = strdup(fwdwnlreq->firmwareName);
    ctx->firmware_url  = (fwdwnlreq->downloadUrl != NULL) ? strdup(fwdwnlreq->downloadUrl) : NULL;
    ctx->firmware_type = (fwdwnlreq->TypeOfFirmware != NULL) ? strdup(fwdwnlreq->TypeOfFirmware) : NULL;
    ctx->callback      = callback;
    ctx->is_ready      = false;
    ctx->init_failed   = false;
    ctx->daemon_accepted = false;
    ctx->daemon_reject_message = NULL;

    if (pthread_mutex_init(&ctx->ready_mutex, NULL) != 0) {
        FWUPMGR_ERROR("downloadFirmware: ready_mutex init failed\n");
        free(ctx->handle_key);
        free(ctx->firmware_name);
        free(ctx->firmware_url);
        free(ctx->firmware_type);
        free(ctx);
        return RDKFW_DWNL_FAILED;
    }

    if (pthread_cond_init(&ctx->ready_cond, NULL) != 0) {
        FWUPMGR_ERROR("downloadFirmware: ready_cond init failed\n");
        pthread_mutex_destroy(&ctx->ready_mutex);
        free(ctx->handle_key);
        free(ctx->firmware_name);
        free(ctx->firmware_url);
        free(ctx->firmware_type);
        free(ctx);
        return RDKFW_DWNL_FAILED;
    }

    /* [5] Atomically begin the download session: set in-progress + track ctx.
     *
     * internal_begin_download() does the duplicate rejection AND the
     * context tracking in one mutex-protected operation. If another download
     * is already active, it returns false and we clean up locally.
     */
    if (!internal_begin_download(ctx)) {
        FWUPMGR_WARN("downloadFirmware: already in progress, rejecting. "
                     "handle='%s'\n", handle);
        pthread_cond_destroy(&ctx->ready_cond);
        pthread_mutex_destroy(&ctx->ready_mutex);
        free(ctx->handle_key);
        free(ctx->firmware_name);
        free(ctx->firmware_url);
        free(ctx->firmware_type);
        free(ctx);
        return RDKFW_DWNL_FAILED;
    }

    /* [6] Spawn worker thread — ownership of ctx transfers to worker
     *
     * The worker thread will set up D-Bus, subscribe to signals,
     * call daemon synchronously, and wait for progress signals.
     * Thread is joinable (NOT detached) so destructor can join it.
     */
    if (pthread_create(&ctx->thread, NULL, internal_download_worker_thread, ctx) != 0) {
        FWUPMGR_ERROR("downloadFirmware: pthread_create failed\n");
        internal_abort_download();
        pthread_cond_destroy(&ctx->ready_cond);
        pthread_mutex_destroy(&ctx->ready_mutex);
        free(ctx->handle_key);
        free(ctx->firmware_name);
        free(ctx->firmware_url);
        free(ctx->firmware_type);
        free(ctx);
        return RDKFW_DWNL_FAILED;
    }

    /* [7] Wait for worker to signal ready (includes daemon reply)
     *
     * This blocks the caller for ~50-200ms while the worker sets up
     * its D-Bus connection, subscribes to signals, and calls the daemon
     * synchronously. The worker signals is_ready=true when it's either
     * ready (daemon accepted) or has failed (D-Bus error or daemon rejected).
     */
    pthread_mutex_lock(&ctx->ready_mutex);
    while (!ctx->is_ready) {
        pthread_cond_wait(&ctx->ready_cond, &ctx->ready_mutex);
    }
    bool failed = ctx->init_failed;
    pthread_mutex_unlock(&ctx->ready_mutex);

    /* [8] Check if worker failed to initialize or daemon rejected
     *
     * If init_failed is true, either D-Bus setup failed or the daemon
     * rejected the download request. The worker thread is already
     * cleaning itself up. We join it to avoid a zombie thread.
     */
    if (failed) {
        FWUPMGR_ERROR("downloadFirmware: worker init failed or daemon rejected. "
                     "handle='%s'\n", handle);
        /* Worker thread will clean itself up (free ctx, reset g_dwnl_in_progress).
         * We just need to join it to wait for cleanup to finish. */
        pthread_join(ctx->thread, NULL);
        return RDKFW_DWNL_FAILED;
    }

    /* [9] Worker is running and listening for DownloadProgress signals.
     *
     * From this point, the caller NEVER touches ctx again.
     * The worker thread is the sole owner and will free it after
     * the download completes, errors, or times out.
     */
    FWUPMGR_INFO("downloadFirmware: worker thread started, returning SUCCESS. "
                 "Callback will fire as download progresses. handle='%s'\n",
                 handle);

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
