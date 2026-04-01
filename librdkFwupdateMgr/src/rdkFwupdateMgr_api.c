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
 * UPDATE FIRMWARE (Phase 3 - on-demand worker thread):
 * =====================================================
 * 1. Validate handle, request, and callback
 * 2. Reject if another updateFirmware is already in progress
 * 3. Allocate UpdateRequestContext on heap
 * 4. Spawn worker thread (internal_update_worker_thread)
 * 5. Wait for worker to signal "ready" via condvar (~50-200ms, includes daemon reply)
 * 6. Return SUCCESS or FAIL (accurate — reflects daemon's accept/reject)
 *
 * [Later - typically 5-60 minutes, max 3600 seconds]
 * Worker thread receives UpdateProgress signals from daemon
 * → Fires client callback MULTIPLE TIMES (per progress signal)
 * → Quits loop on COMPLETED/ERROR → Cleans up → Thread exits
 */

#include "rdkFwupdateMgr_client.h"
#include "rdkFwupdateMgr_async_internal.h"
#include "rdkFwupdateMgr_log.h"
#include <gio/gio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <time.h>
#include <errno.h>

/**
 * Maximum time (seconds) to wait for the worker thread to signal readiness.
 *
 * The worker normally signals in <200ms (D-Bus connect + subscribe + optional
 * sync method call). 10 seconds is extremely generous. If the worker hasn't
 * signaled by then, it's dead or wedged — treat it as init failure.
 */
#define WORKER_READY_TIMEOUT_SEC 10

/* No extern globals needed — all state is accessed through
 * internal_begin_*() / internal_end_*() / internal_abort_*()
 * / internal_is_*_in_progress() declared in rdkFwupdateMgr_async_internal.h.
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

    /* [8] Save thread handle locally BEFORE condvar wait.
     *
     * CRITICAL: On the init-failure path, the worker signals is_ready=true
     * and then immediately proceeds to cleanup (which destroys ready_mutex,
     * ready_cond, and free(ctx)). If we read ctx->thread AFTER the condvar
     * wake, ctx may already be freed → use-after-free.
     *
     * By copying pthread_t here (right after pthread_create, before any
     * race can occur), our join on the failure path uses the local copy
     * and never touches ctx again.
     */
    pthread_t worker_thread = ctx->thread;

    /* [8b] Wait for worker to signal ready (bounded timeout)
     *
     * This blocks the caller for ~10-100ms while the worker sets up
     * its D-Bus connection and signal subscription. The worker signals
     * is_ready=true when it's either ready or has failed to init.
     *
     * We use pthread_cond_timedwait to prevent infinite hang if the
     * worker crashes or exits without signaling.
     */
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += WORKER_READY_TIMEOUT_SEC;

    pthread_mutex_lock(&ctx->ready_mutex);
    int wait_rc = 0;
    while (!ctx->is_ready && wait_rc == 0) {
        wait_rc = pthread_cond_timedwait(&ctx->ready_cond, &ctx->ready_mutex,
                                         &deadline);
    }
    bool failed = ctx->init_failed || (wait_rc == ETIMEDOUT);
    pthread_mutex_unlock(&ctx->ready_mutex);

    if (wait_rc == ETIMEDOUT) {
        FWUPMGR_ERROR("checkForUpdate: worker thread did not signal ready "
                     "within %ds — treating as init failure. handle='%s'\n",
                     WORKER_READY_TIMEOUT_SEC, handle);
    }

    /* [9] Check if worker failed to initialize
     *
     * The worker tried to connect to D-Bus and subscribe to signals.
     * If that failed (D-Bus dead, system error), init_failed is true.
     * We join the worker (it's already exiting) and return FAIL to the app.
     * The worker handles its own cleanup — we just wait for it to finish.
     *
     * IMPORTANT: We use worker_thread (local copy), NOT ctx->thread.
     * After the condvar wake, the worker may have already freed ctx.
     */
    if (failed) {
        FWUPMGR_ERROR("checkForUpdate: worker thread failed to initialize. "
                     "handle='%s'\n", handle);
        pthread_join(worker_thread, NULL);
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
 * Phase 3: No async infrastructure init needed. All three APIs use on-demand
 * worker threads that create and destroy their own resources. The library is
 * ready to use immediately after loading.
 */
__attribute__((constructor))
static void rdkFwupdateMgr_lib_init(void)
{
    FWUPMGR_INFO("=== rdkFwupdateMgr library loading ===\n");
    /* No internal_system_init() needed — all APIs use on-demand worker threads.
     * Zero resource cost when idle: no background thread, no registries,
     * no D-Bus connections until an API is actually called.
     */
    FWUPMGR_INFO("=== rdkFwupdateMgr library ready ===\n");
}

/**
 * @brief Library destructor — auto-called when .so is unloaded
 *
 * Stops any active CheckForUpdate, DownloadFirmware, and UpdateFirmware
 * worker threads. No persistent background thread to stop (removed in Phase 3).
 */
__attribute__((destructor))
static void rdkFwupdateMgr_lib_deinit(void)
{
    FWUPMGR_INFO("=== rdkFwupdateMgr library unloading ===\n");

    /* Phase 1: Cancel and join any active CheckForUpdate worker thread. */
    internal_cancel_all_active_check_threads();

    /* Phase 2: Cancel and join any active DownloadFirmware worker thread. */
    internal_cancel_all_active_download_threads();

    /* Phase 3: Cancel and join any active UpdateFirmware worker thread. */
    internal_cancel_all_active_update_threads();

    /* No internal_system_deinit() needed — no persistent BG thread or registry. */

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
    if (ctx->firmware_name == NULL) {
        FWUPMGR_ERROR("downloadFirmware: strdup failed for firmwareName\n");
        free(ctx->handle_key);
        free(ctx);
        return RDKFW_DWNL_FAILED;
    }

    ctx->firmware_url = NULL;
    if (fwdwnlreq->downloadUrl != NULL) {
        ctx->firmware_url = strdup(fwdwnlreq->downloadUrl);
        if (ctx->firmware_url == NULL) {
            FWUPMGR_ERROR("downloadFirmware: strdup failed for downloadUrl\n");
            free(ctx->firmware_name);
            free(ctx->handle_key);
            free(ctx);
            return RDKFW_DWNL_FAILED;
        }
    }

    ctx->firmware_type = NULL;
    if (fwdwnlreq->TypeOfFirmware != NULL) {
        ctx->firmware_type = strdup(fwdwnlreq->TypeOfFirmware);
        if (ctx->firmware_type == NULL) {
            FWUPMGR_ERROR("downloadFirmware: strdup failed for TypeOfFirmware\n");
            free(ctx->firmware_url);
            free(ctx->firmware_name);
            free(ctx->handle_key);
            free(ctx);
            return RDKFW_DWNL_FAILED;
        }
    }

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

    /* [7] Save thread handle locally BEFORE condvar wait.
     *
     * CRITICAL: Same UAF prevention as checkForUpdate — on init failure,
     * the worker frees ctx after signaling ready. We must not read
     * ctx->thread after the condvar wake. Save it now.
     */
    pthread_t worker_thread = ctx->thread;

    /* [7b] Wait for worker to signal ready (bounded timeout)
     *
     * This blocks the caller for ~50-200ms while the worker sets up
     * its D-Bus connection, subscribes to signals, and calls the daemon
     * synchronously. The worker signals is_ready=true when it's either
     * ready (daemon accepted) or has failed (D-Bus error or daemon rejected).
     *
     * We use pthread_cond_timedwait to prevent infinite hang if the
     * worker crashes or exits without signaling.
     */
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += WORKER_READY_TIMEOUT_SEC;

    pthread_mutex_lock(&ctx->ready_mutex);
    int wait_rc = 0;
    while (!ctx->is_ready && wait_rc == 0) {
        wait_rc = pthread_cond_timedwait(&ctx->ready_cond, &ctx->ready_mutex,
                                         &deadline);
    }
    bool failed = ctx->init_failed || (wait_rc == ETIMEDOUT);
    pthread_mutex_unlock(&ctx->ready_mutex);

    if (wait_rc == ETIMEDOUT) {
        FWUPMGR_ERROR("downloadFirmware: worker thread did not signal ready "
                     "within %ds — treating as init failure. handle='%s'\n",
                     WORKER_READY_TIMEOUT_SEC, handle);
    }

    /* [8] Check if worker failed to initialize or daemon rejected
     *
     * If init_failed is true, either D-Bus setup failed or the daemon
     * rejected the download request. The worker thread is already
     * cleaning itself up. We join it to avoid a zombie thread.
     *
     * IMPORTANT: We use worker_thread (local copy), NOT ctx->thread.
     * After the condvar wake, the worker may have already freed ctx.
     */
    if (failed) {
        FWUPMGR_ERROR("downloadFirmware: worker init failed or daemon rejected. "
                     "handle='%s'\n", handle);
        pthread_join(worker_thread, NULL);
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
 * UPDATE FIRMWARE PUBLIC API — ON-DEMAND WORKER THREAD (Phase 3)
 * ========================================================================
 *
 * Implements:
 *   UpdateResult updateFirmware(FirmwareInterfaceHandle handle,
 *                               const FwUpdateReq *fwupdatereq,
 *                               UpdateCallback callback);
 *
 * FLOW:
 *   1. Validate: handle, request fields, callback
 *   2. Allocate UpdateRequestContext on heap
 *   3. internal_begin_update(ctx) — reject if already active
 *   4. Spawn worker thread (internal_update_worker_thread)
 *   5. Wait for condvar — worker sets up D-Bus + calls daemon synchronously
 *   6. Check daemon reply: accepted → SUCCESS, rejected → FAIL
 *
 *   [later — fires multiple times over 5-60 minutes]
 *   Worker receives UpdateProgress signals → fires callback each time
 *   → quits loop on COMPLETED/ERROR → cleanup → thread exits
 * ======================================================================== */

/**
 * @brief Initiate firmware flashing — spawns on-demand worker thread
 *
 * Allocates an UpdateRequestContext, spawns a worker thread that connects
 * to D-Bus, subscribes to UpdateProgress signal, sends UpdateFirmware
 * method call SYNCHRONOUSLY, and reads the daemon's reply. The caller
 * blocks briefly (~50-200ms) until the worker signals "ready", then
 * returns immediately with an ACCURATE result reflecting the daemon's
 * accept/reject decision.
 *
 * D-Bus arguments sent to daemon: (sssss)
 *   s  handle               — identifies this app
 *   s  firmwareName         — image filename to flash
 *   s  LocationOfFirmware   — path to image ("" = use device.properties)
 *   s  TypeOfFirmware       — "PCI" | "PDRI" | "PERIPHERAL"
 *   s  rebootImmediately    — "true" or "false" (daemon expects string)
 *
 * INVARIANTS:
 *   - At most one updateFirmware() in progress per process
 *   - Callback fires N times (per progress signal) or 0 times (on error)
 *   - Worker thread is self-contained: creates and destroys all its resources
 *
 * @param handle        Valid FirmwareInterfaceHandle from registerProcess()
 * @param fwupdatereq   Update request (firmware name, type, location, reboot flag)
 * @param callback      Invoked on each UpdateProgress signal
 * @return RDKFW_UPDATE_SUCCESS or RDKFW_UPDATE_FAILED
 */
UpdateResult updateFirmware(FirmwareInterfaceHandle handle,
                             const FwUpdateReq *fwupdatereq,
                             UpdateCallback callback)
{
    /* [1] Validate handle */
    if (handle == NULL || handle[0] == '\0') {
        FWUPMGR_ERROR("updateFirmware: invalid handle (NULL or empty)\n");
        return RDKFW_UPDATE_FAILED;
    }

    /* [2] Validate request */
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

    /* [3] Validate callback */
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

    /* [4] Allocate per-request context on heap
     *
     * We allocate FIRST, then call internal_begin_update() to atomically
     * set in-progress + track ctx. This way there's no window where in-progress
     * is true but ctx isn't ready.
     */
    UpdateRequestContext *ctx = calloc(1, sizeof(UpdateRequestContext));
    if (ctx == NULL) {
        FWUPMGR_ERROR("updateFirmware: calloc failed for ctx\n");
        return RDKFW_UPDATE_FAILED;
    }

    ctx->handle_key = strdup(handle);
    if (ctx->handle_key == NULL) {
        FWUPMGR_ERROR("updateFirmware: strdup failed for handle\n");
        free(ctx);
        return RDKFW_UPDATE_FAILED;
    }

    ctx->firmware_name = strdup(fwupdatereq->firmwareName);
    if (ctx->firmware_name == NULL) {
        FWUPMGR_ERROR("updateFirmware: strdup failed for firmwareName\n");
        free(ctx->handle_key);
        free(ctx);
        return RDKFW_UPDATE_FAILED;
    }

    ctx->firmware_location = NULL;
    if (fwupdatereq->LocationOfFirmware != NULL) {
        ctx->firmware_location = strdup(fwupdatereq->LocationOfFirmware);
        if (ctx->firmware_location == NULL) {
            FWUPMGR_ERROR("updateFirmware: strdup failed for LocationOfFirmware\n");
            free(ctx->firmware_name);
            free(ctx->handle_key);
            free(ctx);
            return RDKFW_UPDATE_FAILED;
        }
    }

    ctx->firmware_type = strdup(fwupdatereq->TypeOfFirmware);
    if (ctx->firmware_type == NULL) {
        FWUPMGR_ERROR("updateFirmware: strdup failed for TypeOfFirmware\n");
        free(ctx->firmware_location);
        free(ctx->firmware_name);
        free(ctx->handle_key);
        free(ctx);
        return RDKFW_UPDATE_FAILED;
    }

    ctx->reboot_flag = strdup(fwupdatereq->rebootImmediately ? "true" : "false");
    if (ctx->reboot_flag == NULL) {
        FWUPMGR_ERROR("updateFirmware: strdup failed for reboot_flag\n");
        free(ctx->firmware_type);
        free(ctx->firmware_location);
        free(ctx->firmware_name);
        free(ctx->handle_key);
        free(ctx);
        return RDKFW_UPDATE_FAILED;
    }

    ctx->callback          = callback;
    ctx->is_ready          = false;
    ctx->init_failed       = false;
    ctx->daemon_accepted   = false;
    ctx->daemon_reject_message = NULL;

    if (pthread_mutex_init(&ctx->ready_mutex, NULL) != 0) {
        FWUPMGR_ERROR("updateFirmware: ready_mutex init failed\n");
        free(ctx->handle_key);
        free(ctx->firmware_name);
        free(ctx->firmware_location);
        free(ctx->firmware_type);
        free(ctx->reboot_flag);
        free(ctx);
        return RDKFW_UPDATE_FAILED;
    }

    if (pthread_cond_init(&ctx->ready_cond, NULL) != 0) {
        FWUPMGR_ERROR("updateFirmware: ready_cond init failed\n");
        pthread_mutex_destroy(&ctx->ready_mutex);
        free(ctx->handle_key);
        free(ctx->firmware_name);
        free(ctx->firmware_location);
        free(ctx->firmware_type);
        free(ctx->reboot_flag);
        free(ctx);
        return RDKFW_UPDATE_FAILED;
    }

    /* [5] Atomically begin the update session: set in-progress + track ctx.
     *
     * internal_begin_update() does the duplicate rejection AND the
     * context tracking in one mutex-protected operation. If another update
     * is already active, it returns false and we clean up locally.
     */
    if (!internal_begin_update(ctx)) {
        FWUPMGR_WARN("updateFirmware: already in progress, rejecting. "
                     "handle='%s'\n", handle);
        pthread_cond_destroy(&ctx->ready_cond);
        pthread_mutex_destroy(&ctx->ready_mutex);
        free(ctx->handle_key);
        free(ctx->firmware_name);
        free(ctx->firmware_location);
        free(ctx->firmware_type);
        free(ctx->reboot_flag);
        free(ctx);
        return RDKFW_UPDATE_FAILED;
    }

    /* [6] Spawn worker thread — ownership of ctx transfers to worker
     *
     * The worker thread will set up D-Bus, subscribe to signals,
     * call daemon synchronously, and wait for progress signals.
     * Thread is joinable (NOT detached) so destructor can join it.
     */
    if (pthread_create(&ctx->thread, NULL, internal_update_worker_thread, ctx) != 0) {
        FWUPMGR_ERROR("updateFirmware: pthread_create failed\n");
        internal_abort_update();
        pthread_cond_destroy(&ctx->ready_cond);
        pthread_mutex_destroy(&ctx->ready_mutex);
        free(ctx->handle_key);
        free(ctx->firmware_name);
        free(ctx->firmware_location);
        free(ctx->firmware_type);
        free(ctx->reboot_flag);
        free(ctx);
        return RDKFW_UPDATE_FAILED;
    }

    /* [7] Save thread handle locally BEFORE condvar wait.
     *
     * CRITICAL: Same UAF prevention as checkForUpdate/downloadFirmware —
     * on init failure, the worker frees ctx after signaling ready.
     * We must not read ctx->thread after the condvar wake. Save it now.
     */
    pthread_t worker_thread = ctx->thread;

    /* [7b] Wait for worker to signal ready (bounded timeout)
     *
     * This blocks the caller for ~50-200ms while the worker sets up
     * its D-Bus connection, subscribes to signals, and calls the daemon
     * synchronously. The worker signals is_ready=true when it's either
     * ready (daemon accepted) or has failed (D-Bus error or daemon rejected).
     *
     * We use pthread_cond_timedwait to prevent infinite hang if the
     * worker crashes or exits without signaling.
     */
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += WORKER_READY_TIMEOUT_SEC;

    pthread_mutex_lock(&ctx->ready_mutex);
    int wait_rc = 0;
    while (!ctx->is_ready && wait_rc == 0) {
        wait_rc = pthread_cond_timedwait(&ctx->ready_cond, &ctx->ready_mutex,
                                         &deadline);
    }
    bool failed = ctx->init_failed || (wait_rc == ETIMEDOUT);
    pthread_mutex_unlock(&ctx->ready_mutex);

    if (wait_rc == ETIMEDOUT) {
        FWUPMGR_ERROR("updateFirmware: worker thread did not signal ready "
                     "within %ds — treating as init failure. handle='%s'\n",
                     WORKER_READY_TIMEOUT_SEC, handle);
    }

    /* [8] Check if worker failed to initialize or daemon rejected
     *
     * If init_failed is true, either D-Bus setup failed or the daemon
     * rejected the update request. The worker thread is already
     * cleaning itself up. We join it to avoid a zombie thread.
     *
     * IMPORTANT: We use worker_thread (local copy), NOT ctx->thread.
     * After the condvar wake, the worker may have already freed ctx.
     */
    if (failed) {
        FWUPMGR_ERROR("updateFirmware: worker init failed or daemon rejected. "
                     "handle='%s'\n", handle);
        pthread_join(worker_thread, NULL);
        return RDKFW_UPDATE_FAILED;
    }

    /* [9] Worker is running and listening for UpdateProgress signals.
     *
     * From this point, the caller NEVER touches ctx again.
     * The worker thread is the sole owner and will free it after
     * the update completes, errors, or times out.
     */
    FWUPMGR_INFO("updateFirmware: worker thread started, returning SUCCESS. "
                 "Callback will fire as update progresses. handle='%s'\n",
                 handle);

    return RDKFW_UPDATE_SUCCESS;
}
