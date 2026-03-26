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
 * @file rdkFwupdateMgr_async.c
 * @brief Internal engine: CheckForUpdate, DownloadFirmware, UpdateFirmware
 *        — all use on-demand worker threads (Phase 1+2+3)
 *
 * ARCHITECTURE (Phase 3 — all APIs on-demand):
 *
 * CheckForUpdate — ON-DEMAND WORKER THREAD (Phase 1):
 *   - internal_check_worker_thread(): spawned per checkForUpdate() call
 *   - on_check_signal_handler(): fires client callback directly
 *   - on_check_timeout(): 120s safety net
 *   - internal_is_check_in_progress(): query for session-state enforcement
 *   - internal_cancel_all_active_check_threads(): destructor cleanup
 *
 * DownloadFirmware — ON-DEMAND WORKER THREAD (Phase 2):
 *   - internal_download_worker_thread(): spawned per downloadFirmware() call
 *   - on_download_signal_handler(): fires client callback, quits on terminal
 *   - on_download_timeout(): 3600s safety net, fires DWNL_ERROR callback
 *   - internal_is_dwnl_in_progress(): query for session-state enforcement
 *   - internal_cancel_all_active_download_threads(): destructor cleanup
 *
 * UpdateFirmware — ON-DEMAND WORKER THREAD (Phase 3):
 *   - internal_update_worker_thread(): spawned per updateFirmware() call
 *   - on_update_signal_handler(): fires client callback, quits on terminal
 *   - on_update_timeout(): 3600s safety net, fires UPDATE_ERROR callback
 *   - internal_is_update_in_progress(): query for session-state enforcement
 *   - internal_cancel_all_active_update_threads(): destructor cleanup
 *
 * NO persistent background thread. NO registries.
 * Zero resource cost when idle.
 *
 * Apps never interact with this file directly.
 * All entry points are through rdkFwupdateMgr_api.c.
 */

#include "rdkFwupdateMgr_async_internal.h"
#include "rdkFwupdateMgr_log.h"
#include "rdkFwupdateMgr_client.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <inttypes.h>  /* For PRIu64 */

/* ========================================================================
 * GLOBAL STATE
 * ======================================================================== */

/* ---- CheckForUpdate on-demand thread state ---- */
/*
 * TL;DR: These are STATIC — only accessible through accessor functions below.
 * This prevents other files from touching the mutex/flag/pointer directly,
 * which would be fragile and race-prone. All access goes through:
 *   internal_is_check_in_progress()        — query
 *   internal_begin_check()                 — set in-progress, track ctx
 *   internal_end_check()                   — clear in-progress, untrack ctx
 *   internal_cancel_all_active_check_threads() — destructor cleanup
 */
static pthread_mutex_t g_check_in_progress_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool            g_check_in_progress = false;
static CheckRequestContext *g_active_check_ctx = NULL;

/* ---- DownloadFirmware on-demand thread state (Phase 2) ---- */
/*
 * Same encapsulation pattern as CheckForUpdate. All access goes through:
 *   internal_is_dwnl_in_progress()         — query
 *   internal_begin_download()              — set in-progress, track ctx
 *   internal_end_download()                — clear in-progress, untrack ctx
 *   internal_abort_download()              — clear on error paths in downloadFirmware()
 *   internal_cancel_all_active_download_threads() — destructor cleanup
 */
static pthread_mutex_t g_dwnl_in_progress_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool            g_dwnl_in_progress = false;
static DownloadRequestContext *g_active_dwnl_ctx = NULL;

/* ---- UpdateFirmware on-demand thread state (Phase 3) ---- */
/*
 * Same encapsulation pattern as Check and Download. All access goes through:
 *   internal_is_update_in_progress()       — query
 *   internal_begin_update()                — set in-progress, track ctx
 *   internal_end_update()                  — clear in-progress, untrack ctx
 *   internal_abort_update()                — clear on error paths in updateFirmware()
 *   internal_cancel_all_active_update_threads() — destructor cleanup
 */
static pthread_mutex_t g_update_in_progress_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool            g_update_in_progress = false;
static UpdateRequestContext *g_active_update_ctx = NULL;

/* ========================================================================
 * FORWARD DECLARATIONS
 * ======================================================================== */

/* Forward declarations — CheckForUpdate on-demand worker thread */
static void  on_check_signal_handler(GDBusConnection *conn,
                                     const gchar *sender,
                                     const gchar *object_path,
                                     const gchar *interface_name,
                                     const gchar *signal_name,
                                     GVariant *parameters,
                                     gpointer user_data);
static gboolean on_check_timeout(gpointer user_data);

/* Forward declarations — DownloadFirmware on-demand worker thread (Phase 2) */
static void  on_download_signal_handler(GDBusConnection *conn,
                                        const gchar *sender,
                                        const gchar *object_path,
                                        const gchar *interface_name,
                                        const gchar *signal_name,
                                        GVariant *parameters,
                                        gpointer user_data);
static gboolean on_download_timeout(gpointer user_data);
static DownloadStatus map_dwnl_status_string(const char *status_str);

/* Forward declarations — UpdateFirmware on-demand worker thread (Phase 3) */
static void  on_update_signal_handler(GDBusConnection *conn,
                                      const gchar *sender,
                                      const gchar *object_path,
                                      const gchar *interface_name,
                                      const gchar *signal_name,
                                      GVariant *parameters,
                                      gpointer user_data);
static gboolean on_update_timeout(gpointer user_data);

static bool  parse_update_details(const char *update_details_str,
                                   UpdateDetails *out_details);

/* ========================================================================
 * CHECKFORUPDATE — ON-DEMAND WORKER THREAD ENGINE (Phase 1)
 * ========================================================================
 *
 * Replaces the old registry-based signal dispatch for CheckForUpdate.
 * Each checkForUpdate() call spawns a short-lived worker thread that:
 *   1. Creates isolated GLib event loop
 *   2. Subscribes to CheckForUpdateComplete signal
 *   3. Sends CheckForUpdate D-Bus method call
 *   4. Waits for signal (with 120s timeout)
 *   5. Fires client callback directly
 *   6. Cleans up and exits
 *
 * At most ONE worker thread per process (enforced by g_check_in_progress).
 * ======================================================================== */

/**
 * @brief Query whether a checkForUpdate() is currently in progress.
 *
 * Thread-safe: protected by g_check_in_progress_mutex.
 */
bool internal_is_check_in_progress(void)
{
    pthread_mutex_lock(&g_check_in_progress_mutex);
    bool result = g_check_in_progress;
    pthread_mutex_unlock(&g_check_in_progress_mutex);
    return result;
}

/**
 * @brief Atomically try to begin a checkForUpdate session and track the context.
 *
 * TL;DR: This is the single entry point for transitioning from "idle" to
 * "check in progress." It combines the duplicate-rejection check, the flag
 * set, and the context tracking into ONE mutex-protected operation. The caller
 * (checkForUpdate in _api.c) never touches the mutex or globals directly.
 *
 * @param ctx  The newly allocated CheckRequestContext to track.
 * @return true if the check was started (no other check was active),
 *         false if a check was already in progress (caller should return FAIL).
 */
bool internal_begin_check(CheckRequestContext *ctx)
{
    pthread_mutex_lock(&g_check_in_progress_mutex);
    if (g_check_in_progress) {
        pthread_mutex_unlock(&g_check_in_progress_mutex);
        return false;  /* already in progress — reject */
    }
    g_check_in_progress = true;
    g_active_check_ctx = ctx;
    pthread_mutex_unlock(&g_check_in_progress_mutex);
    return true;
}

/**
 * @brief Atomically end the checkForUpdate session and untrack the context.
 *
 * TL;DR: Called by the worker thread in cleanup_common, right before freeing
 * ctx. After this returns, g_active_check_ctx is NULL and g_check_in_progress
 * is false — the next checkForUpdate() call will be accepted.
 *
 * IMPORTANT: Must be called BEFORE free(ctx). The mutex ensures the destructor
 * cannot read g_active_check_ctx while we're freeing it.
 */
void internal_end_check(void)
{
    pthread_mutex_lock(&g_check_in_progress_mutex);
    g_check_in_progress = false;
    g_active_check_ctx = NULL;
    pthread_mutex_unlock(&g_check_in_progress_mutex);
}

/**
 * @brief Atomically clear in-progress flag WITHOUT untracking context.
 *
 * TL;DR: Used only on error paths in checkForUpdate() (in _api.c) when
 * the context was never successfully tracked (e.g., calloc/strdup/mutex_init
 * failed before internal_begin_check was called) or when pthread_create fails
 * after begin_check. The caller will free ctx itself.
 */
void internal_abort_check(void)
{
    pthread_mutex_lock(&g_check_in_progress_mutex);
    g_check_in_progress = false;
    g_active_check_ctx = NULL;
    pthread_mutex_unlock(&g_check_in_progress_mutex);
}

/**
 * @brief Cancel all active checkForUpdate worker threads and join them.
 *
 * Called from library destructor. Quits the worker's event loop so it
 * exits cleanly, then joins the thread to ensure no code is executing
 * in library memory when dlclose() unmaps us.
 */
void internal_cancel_all_active_check_threads(void)
{
    pthread_mutex_lock(&g_check_in_progress_mutex);
    CheckRequestContext *ctx = g_active_check_ctx;
    pthread_mutex_unlock(&g_check_in_progress_mutex);

    if (ctx == NULL) {
        FWUPMGR_INFO("internal_cancel_all_active_check_threads: no active worker\n");
        return;
    }

    FWUPMGR_INFO("internal_cancel_all_active_check_threads: "
                 "stopping active worker thread\n");

    /* Quit the worker's event loop — this causes g_main_loop_run() to return */
    if (ctx->main_loop != NULL) {
        g_main_loop_quit(ctx->main_loop);
    }

    /* Wait for worker thread to finish cleanup and exit */
    pthread_join(ctx->thread, NULL);

    FWUPMGR_INFO("internal_cancel_all_active_check_threads: "
                 "worker thread joined\n");
}

/**
 * @brief Timeout handler for the worker thread's GMainLoop.
 *
 * Fires after CHECK_SIGNAL_TIMEOUT_SECONDS if the daemon never sends
 * the CheckForUpdateComplete signal. Quits the event loop so the worker
 * can proceed to cleanup.
 *
 * @param user_data  CheckRequestContext* (NOT freed here — worker does it)
 * @return G_SOURCE_REMOVE (fire once only)
 */
static gboolean on_check_timeout(gpointer user_data)
{
    CheckRequestContext *ctx = (CheckRequestContext *)user_data;

    FWUPMGR_WARN("on_check_timeout: %ds timeout expired, "
                 "daemon did not respond. handle='%s'\n",
                 CHECK_SIGNAL_TIMEOUT_SECONDS,
                 ctx->handle_key ? ctx->handle_key : "(null)");

    if (ctx->main_loop != NULL) {
        g_main_loop_quit(ctx->main_loop);
    }

    return G_SOURCE_REMOVE;
}

/**
 * @brief Signal handler for CheckForUpdateComplete — fires client callback.
 *
 * Called by GLib in the worker thread's GMainContext when the daemon emits
 * the CheckForUpdateComplete signal. Parses the payload, builds FwInfoData,
 * invokes the client callback, then quits the event loop.
 *
 * @param user_data  CheckRequestContext* (NOT freed here — worker does it)
 */
static void on_check_signal_handler(GDBusConnection *conn,
                                    const gchar *sender,
                                    const gchar *object_path,
                                    const gchar *interface_name,
                                    const gchar *signal_name,
                                    GVariant *parameters,
                                    gpointer user_data)
{
    (void)conn; (void)sender; (void)object_path;
    (void)interface_name; (void)signal_name;

    CheckRequestContext *ctx = (CheckRequestContext *)user_data;

    FWUPMGR_INFO("on_check_signal_handler: received CheckForUpdateComplete "
                 "for handle='%s'\n",
                 ctx->handle_key ? ctx->handle_key : "(null)");

    /* Parse signal payload */
    InternalSignalData signal_data;
    memset(&signal_data, 0, sizeof(signal_data));

    if (!internal_parse_signal_data(parameters, &signal_data)) {
        FWUPMGR_ERROR("on_check_signal_handler: parse failed\n");
        /* Quit loop even on parse failure — don't hang forever */
        if (ctx->main_loop != NULL) {
            g_main_loop_quit(ctx->main_loop);
        }
        return;
    }

    /* Build FwInfoData for the callback */
    CheckForUpdateStatus status = internal_map_status_code(signal_data.status_code);

    FwInfoData fwinfo_data;
    memset(&fwinfo_data, 0, sizeof(fwinfo_data));

    /* Copy current firmware version */
    if (signal_data.current_version) {
        strncpy(fwinfo_data.CurrFWVersion, signal_data.current_version,
                sizeof(fwinfo_data.CurrFWVersion) - 1);
        fwinfo_data.CurrFWVersion[sizeof(fwinfo_data.CurrFWVersion) - 1] = '\0';
    }

    fwinfo_data.status = status;

    /* Parse UpdateDetails if firmware is available */
    UpdateDetails update_details;
    if (status == FIRMWARE_AVAILABLE && signal_data.update_details) {
        memset(&update_details, 0, sizeof(update_details));

        if (parse_update_details(signal_data.update_details, &update_details)) {
            fwinfo_data.UpdateDetails = &update_details;
            FWUPMGR_INFO("on_check_signal_handler: UpdateDetails populated\n");
        } else {
            fwinfo_data.UpdateDetails = NULL;
            FWUPMGR_ERROR("on_check_signal_handler: parse_update_details failed\n");
        }
    } else {
        fwinfo_data.UpdateDetails = NULL;
    }

    /* Fire the client's callback
     *
     * TL;DR: This is THE moment — deliver the firmware check result to the app.
     * The callback runs in the worker thread, NOT the app's main thread.
     * After this call returns, we quit the event loop and clean up.
     */
    FWUPMGR_INFO("on_check_signal_handler: invoking callback for handle='%s'\n",
                 ctx->handle_key ? ctx->handle_key : "(null)");

    ctx->callback(&fwinfo_data);

    FWUPMGR_INFO("on_check_signal_handler: callback returned\n");

    /* Cleanup parsed signal data — free strdup'd strings */
    internal_cleanup_signal_data(&signal_data);

    /* Quit the event loop — worker proceeds to cleanup.
     * TL;DR: Break out of g_main_loop_run() in the worker thread. */
    if (ctx->main_loop != NULL) {
        g_main_loop_quit(ctx->main_loop);
    }
}

/* ========================================================================
 * DOWNLOAD FIRMWARE — ON-DEMAND WORKER THREAD ENGINE (Phase 2)
 * ========================================================================
 *
 * Replaces the old registry-based signal dispatch for DownloadFirmware.
 * Each downloadFirmware() call spawns a worker thread that:
 *   1. Creates isolated GLib event loop
 *   2. Subscribes to DownloadProgress signal
 *   3. Sends DownloadFirmware D-Bus method call SYNCHRONOUSLY
 *   4. Reads daemon's (sss) reply: accept or reject
 *   5. If accepted: runs event loop, fires callback on each progress signal
 *   6. Quits loop on COMPLETED/ERROR/timeout
 *   7. Cleans up and exits
 *
 * At most ONE download worker thread per process (enforced by g_dwnl_in_progress).
 * ======================================================================== */

/**
 * @brief Query whether a downloadFirmware() is currently in progress.
 *
 * Thread-safe: protected by g_dwnl_in_progress_mutex.
 */
bool internal_is_dwnl_in_progress(void)
{
    pthread_mutex_lock(&g_dwnl_in_progress_mutex);
    bool result = g_dwnl_in_progress;
    pthread_mutex_unlock(&g_dwnl_in_progress_mutex);
    return result;
}

/**
 * @brief Atomically try to begin a downloadFirmware session and track the context.
 */
bool internal_begin_download(DownloadRequestContext *ctx)
{
    pthread_mutex_lock(&g_dwnl_in_progress_mutex);
    if (g_dwnl_in_progress) {
        pthread_mutex_unlock(&g_dwnl_in_progress_mutex);
        return false;  /* already in progress — reject */
    }
    g_dwnl_in_progress = true;
    g_active_dwnl_ctx = ctx;
    pthread_mutex_unlock(&g_dwnl_in_progress_mutex);
    return true;
}

/**
 * @brief Atomically end the downloadFirmware session and untrack the context.
 *
 * Called by worker thread in cleanup, BEFORE freeing ctx.
 */
void internal_end_download(void)
{
    pthread_mutex_lock(&g_dwnl_in_progress_mutex);
    g_dwnl_in_progress = false;
    g_active_dwnl_ctx = NULL;
    pthread_mutex_unlock(&g_dwnl_in_progress_mutex);
}

/**
 * @brief Atomically clear download in-progress state on error paths.
 */
void internal_abort_download(void)
{
    pthread_mutex_lock(&g_dwnl_in_progress_mutex);
    g_dwnl_in_progress = false;
    g_active_dwnl_ctx = NULL;
    pthread_mutex_unlock(&g_dwnl_in_progress_mutex);
}

/**
 * @brief Cancel all active download worker threads and join them.
 *
 * Called from library destructor. Quits the worker's event loop so it
 * exits cleanly, then joins the thread to ensure no code is executing
 * in library memory when dlclose() unmaps us.
 */
void internal_cancel_all_active_download_threads(void)
{
    pthread_mutex_lock(&g_dwnl_in_progress_mutex);
    DownloadRequestContext *ctx = g_active_dwnl_ctx;
    pthread_mutex_unlock(&g_dwnl_in_progress_mutex);

    if (ctx == NULL) {
        FWUPMGR_INFO("internal_cancel_all_active_download_threads: no active worker\n");
        return;
    }

    FWUPMGR_INFO("internal_cancel_all_active_download_threads: "
                 "stopping active worker thread\n");

    /* Quit the worker's event loop — this causes g_main_loop_run() to return */
    if (ctx->main_loop != NULL) {
        g_main_loop_quit(ctx->main_loop);
    }

    /* Wait for worker thread to finish cleanup and exit */
    pthread_join(ctx->thread, NULL);

    FWUPMGR_INFO("internal_cancel_all_active_download_threads: "
                 "worker thread joined\n");
}

/**
 * @brief Timeout handler for the download worker thread's GMainLoop.
 *
 * Fires after DWNL_SIGNAL_TIMEOUT_SECONDS (3600s) if the download never
 * completes or errors. Fires DWNL_ERROR callback so the client knows,
 * then quits the event loop.
 */
static gboolean on_download_timeout(gpointer user_data)
{
    DownloadRequestContext *ctx = (DownloadRequestContext *)user_data;

    FWUPMGR_ERROR("on_download_timeout: %ds timeout expired, "
                  "download did not complete. handle='%s'\n",
                  DWNL_SIGNAL_TIMEOUT_SECONDS,
                  ctx->handle_key ? ctx->handle_key : "(null)");

    /* Fire error callback so client knows the download failed/stalled */
    if (ctx->callback != NULL) {
        ctx->callback(0, DWNL_ERROR);
    }

    if (ctx->main_loop != NULL) {
        g_main_loop_quit(ctx->main_loop);
    }

    return G_SOURCE_REMOVE;
}

/**
 * @brief Signal handler for DownloadProgress — fires client callback.
 *
 * Called by GLib in the worker thread's GMainContext when the daemon emits
 * a DownloadProgress signal. Parses the payload, maps status, invokes
 * the client's callback. Quits the event loop ONLY on terminal status
 * (COMPLETED or ERROR).
 *
 * KEY DIFFERENCE FROM CheckForUpdate:
 *   CheckForUpdate: one signal → callback → quit loop → thread exits
 *   DownloadFirmware: many signals → callback each time → quit only on terminal
 */
static void on_download_signal_handler(GDBusConnection *conn,
                                       const gchar *sender,
                                       const gchar *object_path,
                                       const gchar *interface_name,
                                       const gchar *signal_name,
                                       GVariant *parameters,
                                       gpointer user_data)
{
    (void)conn; (void)sender; (void)object_path;
    (void)interface_name; (void)signal_name;

    DownloadRequestContext *ctx = (DownloadRequestContext *)user_data;

    /* Parse signal payload */
    InternalDwnlSignalData signal_data;
    memset(&signal_data, 0, sizeof(signal_data));

    if (!internal_parse_dwnl_signal_data(parameters, &signal_data)) {
        FWUPMGR_ERROR("on_download_signal_handler: parse failed\n");
        return;  /* Don't quit loop on parse failure — wait for next signal */
    }

    FWUPMGR_INFO("on_download_signal_handler: handler=%" PRIu64
                 " firmware='%s' progress=%u%% status='%s' handle='%s'\n",
                 signal_data.handler_id,
                 signal_data.firmware_name ? signal_data.firmware_name : "(null)",
                 signal_data.progress_percent,
                 signal_data.status_string ? signal_data.status_string : "(null)",
                 ctx->handle_key ? ctx->handle_key : "(null)");

    /* Map status string to enum */
    DownloadStatus status = map_dwnl_status_string(signal_data.status_string);

    /* Fire the client's callback with progress and status */
    if (ctx->callback != NULL) {
        ctx->callback((int)signal_data.progress_percent, status);
    }

    /* Free parsed signal data strings (allocated by g_variant_get) */
    g_free(signal_data.firmware_name);
    g_free(signal_data.status_string);
    g_free(signal_data.message);

    /* Quit loop ONLY on terminal status — otherwise wait for next signal */
    if (status == DWNL_COMPLETED || status == DWNL_ERROR) {
        FWUPMGR_INFO("on_download_signal_handler: terminal status (%s), "
                     "quitting loop. handle='%s'\n",
                     (status == DWNL_COMPLETED) ? "COMPLETED" : "ERROR",
                     ctx->handle_key ? ctx->handle_key : "(null)");

        if (ctx->main_loop != NULL) {
            g_main_loop_quit(ctx->main_loop);
        }
    }
}

/* ========================================================================
 * UPDATE FIRMWARE — ON-DEMAND WORKER THREAD ENGINE (Phase 3)
 * ========================================================================
 *
 * Same pattern as DownloadFirmware. Each updateFirmware() call spawns a
 * worker thread that:
 *   1. Creates isolated GLib event loop
 *   2. Subscribes to UpdateProgress signal
 *   3. Sends UpdateFirmware D-Bus method call SYNCHRONOUSLY
 *   4. Reads daemon's (sss) reply: accept or reject
 *   5. If accepted: runs event loop, fires callback on each progress signal
 *   6. Quits loop on COMPLETED/ERROR/timeout
 *   7. Cleans up and exits
 *
 * At most ONE update worker thread per process (enforced by g_update_in_progress).
 * ======================================================================== */

/**
 * @brief Query whether an updateFirmware() is currently in progress.
 *
 * Thread-safe: protected by g_update_in_progress_mutex.
 */
bool internal_is_update_in_progress(void)
{
    pthread_mutex_lock(&g_update_in_progress_mutex);
    bool result = g_update_in_progress;
    pthread_mutex_unlock(&g_update_in_progress_mutex);
    return result;
}

/**
 * @brief Atomically try to begin an updateFirmware session and track the context.
 *
 * TL;DR: This is the single entry point for transitioning from "idle" to
 * "update in progress." It combines the duplicate-rejection check, the flag
 * set, and the context tracking into ONE mutex-protected operation.
 *
 * @param ctx  The newly allocated UpdateRequestContext to track.
 * @return true if the update was started (no other update was active),
 *         false if an update was already in progress (caller should return FAIL).
 */
bool internal_begin_update(UpdateRequestContext *ctx)
{
    pthread_mutex_lock(&g_update_in_progress_mutex);
    if (g_update_in_progress) {
        pthread_mutex_unlock(&g_update_in_progress_mutex);
        return false;  /* already in progress — reject */
    }
    g_update_in_progress = true;
    g_active_update_ctx = ctx;
    pthread_mutex_unlock(&g_update_in_progress_mutex);
    return true;
}

/**
 * @brief Atomically end the updateFirmware session and untrack the context.
 *
 * Called by worker thread in cleanup, BEFORE freeing ctx.
 * After this returns, g_active_update_ctx is NULL and g_update_in_progress
 * is false — the next updateFirmware() call will be accepted.
 */
void internal_end_update(void)
{
    pthread_mutex_lock(&g_update_in_progress_mutex);
    g_update_in_progress = false;
    g_active_update_ctx = NULL;
    pthread_mutex_unlock(&g_update_in_progress_mutex);
}

/**
 * @brief Atomically clear update in-progress state on error paths.
 *
 * Used when updateFirmware() itself fails (e.g., pthread_create fails
 * after internal_begin_update succeeded). The caller will free ctx directly.
 */
void internal_abort_update(void)
{
    pthread_mutex_lock(&g_update_in_progress_mutex);
    g_update_in_progress = false;
    g_active_update_ctx = NULL;
    pthread_mutex_unlock(&g_update_in_progress_mutex);
}

/**
 * @brief Cancel all active update worker threads and join them.
 *
 * Called from library destructor. Quits the worker's event loop so it
 * exits cleanly, then joins the thread to ensure no code is executing
 * in library memory when dlclose() unmaps us.
 */
void internal_cancel_all_active_update_threads(void)
{
    pthread_mutex_lock(&g_update_in_progress_mutex);
    UpdateRequestContext *ctx = g_active_update_ctx;
    pthread_mutex_unlock(&g_update_in_progress_mutex);

    if (ctx == NULL) {
        FWUPMGR_INFO("internal_cancel_all_active_update_threads: no active worker\n");
        return;
    }

    FWUPMGR_INFO("internal_cancel_all_active_update_threads: "
                 "stopping active worker thread\n");

    /* Quit the worker's event loop — this causes g_main_loop_run() to return */
    if (ctx->main_loop != NULL) {
        g_main_loop_quit(ctx->main_loop);
    }

    /* Wait for worker thread to finish cleanup and exit */
    pthread_join(ctx->thread, NULL);

    FWUPMGR_INFO("internal_cancel_all_active_update_threads: "
                 "worker thread joined\n");
}

/**
 * @brief Timeout handler for the update worker thread's GMainLoop.
 *
 * Fires after UPDATE_SIGNAL_TIMEOUT_SECONDS (3600s) if the update never
 * completes or errors. Fires UPDATE_ERROR callback so the client knows,
 * then quits the event loop.
 */
static gboolean on_update_timeout(gpointer user_data)
{
    UpdateRequestContext *ctx = (UpdateRequestContext *)user_data;

    FWUPMGR_ERROR("on_update_timeout: %ds timeout expired, "
                  "update did not complete. handle='%s'\n",
                  UPDATE_SIGNAL_TIMEOUT_SECONDS,
                  ctx->handle_key ? ctx->handle_key : "(null)");

    /* Fire error callback so client knows the update failed/stalled */
    if (ctx->callback != NULL) {
        ctx->callback(0, UPDATE_ERROR);
    }

    if (ctx->main_loop != NULL) {
        g_main_loop_quit(ctx->main_loop);
    }

    return G_SOURCE_REMOVE;
}

/**
 * @brief Signal handler for UpdateProgress — fires client callback.
 *
 * Called by GLib in the worker thread's GMainContext when the daemon emits
 * an UpdateProgress signal. Parses the (tsiis) payload, maps status, invokes
 * the client's callback. Quits the event loop ONLY on terminal status
 * (UPDATE_COMPLETED or UPDATE_ERROR).
 *
 * Same pattern as on_download_signal_handler:
 *   Many signals → callback each time → quit only on terminal
 */
static void on_update_signal_handler(GDBusConnection *conn,
                                     const gchar *sender,
                                     const gchar *object_path,
                                     const gchar *interface_name,
                                     const gchar *signal_name,
                                     GVariant *parameters,
                                     gpointer user_data)
{
    (void)conn; (void)sender; (void)object_path;
    (void)interface_name; (void)signal_name;

    UpdateRequestContext *ctx = (UpdateRequestContext *)user_data;

    /* Parse signal payload — correct (tsiis) format */
    InternalUpdateSignalData signal_data;
    memset(&signal_data, 0, sizeof(signal_data));

    if (!internal_parse_update_signal_data(parameters, &signal_data)) {
        FWUPMGR_ERROR("on_update_signal_handler: parse failed\n");
        return;  /* Don't quit loop on parse failure — wait for next signal */
    }

    FWUPMGR_INFO("on_update_signal_handler: handler=%" PRIu64
                 " firmware='%s' progress=%d%% status=%d handle='%s'\n",
                 signal_data.handler_id,
                 signal_data.firmware_name ? signal_data.firmware_name : "(null)",
                 signal_data.progress_percent,
                 signal_data.status_code,
                 ctx->handle_key ? ctx->handle_key : "(null)");

    /* Map status code to enum */
    UpdateStatus status = internal_map_update_status_code(signal_data.status_code);

    /* Fire the client's callback with progress and status */
    if (ctx->callback != NULL) {
        ctx->callback(signal_data.progress_percent, status);
    }

    /* Free parsed signal data strings (allocated by g_variant_get) */
    g_free(signal_data.firmware_name);
    g_free(signal_data.message);

    /* Quit loop ONLY on terminal status — otherwise wait for next signal */
    if (status == UPDATE_COMPLETED || status == UPDATE_ERROR) {
        FWUPMGR_INFO("on_update_signal_handler: terminal status (%s), "
                     "quitting loop. handle='%s'\n",
                     (status == UPDATE_COMPLETED) ? "COMPLETED" : "ERROR",
                     ctx->handle_key ? ctx->handle_key : "(null)");

        if (ctx->main_loop != NULL) {
            g_main_loop_quit(ctx->main_loop);
        }
    }
}

/* ========================================================================
 * WORKER THREAD IMPLEMENTATIONS
 * ========================================================================
 * These are the actual thread entry points spawned by checkForUpdate(),
 * downloadFirmware(), and updateFirmware() in rdkFwupdateMgr_api.c.
 *
 * Each worker:
 *   1. Creates an isolated GLib event loop (per-thread GMainContext)
 *   2. Connects to D-Bus (system bus)
 *   3. Subscribes to the appropriate D-Bus signal
 *   4. Sends the D-Bus method call (fire-and-forget or synchronous)
 *   5. Signals the caller "ready" via condvar
 *   6. Runs g_main_loop_run() to wait for signals (with timeout)
 *   7. Cleans up ALL resources and exits
 *
 * OWNERSHIP: The caller (api.c) transfers ctx ownership to the worker.
 *            After condvar handshake, the caller never touches ctx again.
 *            The worker is responsible for freeing ctx and all its members.
 *
 * CLEANUP ORDER (critical for no-leak, no-crash):
 *   1. Unsubscribe from D-Bus signal (subscription_id)
 *   2. Destroy timeout source (if any)
 *   3. Quit and unref GMainLoop
 *   4. Unref GMainContext (pop thread-default first)
 *   5. Close D-Bus connection (g_object_unref — NOT g_dbus_connection_close)
 *   6. internal_end_*() — clear in-progress flag BEFORE freeing ctx
 *   7. Destroy condvar, mutex
 *   8. Free all strdup'd strings
 *   9. free(ctx) — last step
 * ======================================================================== */

/* ========================================================================
 * internal_check_worker_thread — Phase 1: CheckForUpdate
 * ======================================================================== */

void *internal_check_worker_thread(void *arg)
{
    CheckRequestContext *ctx = (CheckRequestContext *)arg;

    FWUPMGR_INFO("internal_check_worker_thread: starting for handle='%s'\n",
                 ctx->handle_key ? ctx->handle_key : "(null)");

    /* ---- Step 1: Create isolated GLib event loop ---- */
    ctx->context = g_main_context_new();
    g_main_context_push_thread_default(ctx->context);
    ctx->main_loop = g_main_loop_new(ctx->context, FALSE);

    /* ---- Step 2: Connect to D-Bus system bus ---- */
    GError *error = NULL;
    ctx->connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);

    if (ctx->connection == NULL) {
        FWUPMGR_ERROR("internal_check_worker_thread: D-Bus connect failed: %s\n",
                      error ? error->message : "unknown");
        if (error) g_error_free(error);

        /* Signal caller: init failed */
        pthread_mutex_lock(&ctx->ready_mutex);
        ctx->init_failed = true;
        ctx->is_ready = true;
        pthread_cond_signal(&ctx->ready_cond);
        pthread_mutex_unlock(&ctx->ready_mutex);

        goto cleanup;
    }

    FWUPMGR_INFO("internal_check_worker_thread: D-Bus connected\n");

    /* ---- Step 3: Subscribe to CheckForUpdateComplete signal ---- */
    ctx->subscription_id = g_dbus_connection_signal_subscribe(
        ctx->connection,
        DBUS_SERVICE_NAME,               /* sender (daemon's bus name) */
        DBUS_INTERFACE_NAME,             /* interface */
        DBUS_SIGNAL_COMPLETE,            /* signal name */
        DBUS_OBJECT_PATH,               /* object path */
        NULL,                            /* arg0 match (none) */
        G_DBUS_SIGNAL_FLAGS_NONE,
        on_check_signal_handler,         /* handler */
        ctx,                             /* user_data */
        NULL);                           /* user_data free func (we free manually) */

    FWUPMGR_INFO("internal_check_worker_thread: subscribed to %s (id=%u)\n",
                 DBUS_SIGNAL_COMPLETE, ctx->subscription_id);

    /* ---- Step 4: Send CheckForUpdate D-Bus method call (fire-and-forget) ----
     *
     * CheckForUpdate takes (s handler_process_name) and returns (issssi).
     * We don't use the method return — the real result comes via signal.
     * We use async call (fire-and-forget) to avoid blocking the condvar handshake.
     */
    g_dbus_connection_call(
        ctx->connection,
        DBUS_SERVICE_NAME,
        DBUS_OBJECT_PATH,
        DBUS_INTERFACE_NAME,
        DBUS_METHOD_CHECK,
        g_variant_new("(s)", ctx->handle_key),
        NULL,                            /* reply type (don't care) */
        G_DBUS_CALL_FLAGS_NONE,
        DBUS_TIMEOUT_MS,
        NULL,                            /* cancellable */
        NULL,                            /* callback (fire-and-forget) */
        NULL);                           /* user_data */

    FWUPMGR_INFO("internal_check_worker_thread: CheckForUpdate method sent\n");

    /* ---- Step 5: Add timeout source ---- */
    GSource *timeout_source = g_timeout_source_new_seconds(CHECK_SIGNAL_TIMEOUT_SECONDS);
    g_source_set_callback(timeout_source, on_check_timeout, ctx, NULL);
    g_source_attach(timeout_source, ctx->context);
    g_source_unref(timeout_source);  /* context holds ref now */

    /* ---- Step 6: Signal caller "ready" ---- */
    pthread_mutex_lock(&ctx->ready_mutex);
    ctx->is_ready = true;
    pthread_cond_signal(&ctx->ready_cond);
    pthread_mutex_unlock(&ctx->ready_mutex);

    FWUPMGR_INFO("internal_check_worker_thread: signaled ready, entering event loop\n");

    /* ---- Step 7: Run event loop — wait for signal or timeout ---- */
    g_main_loop_run(ctx->main_loop);

    FWUPMGR_INFO("internal_check_worker_thread: event loop exited\n");

cleanup:
    /* ---- Cleanup: release all resources in correct order ---- */
    FWUPMGR_INFO("internal_check_worker_thread: cleaning up\n");

    /* Unsubscribe from signal */
    if (ctx->connection && ctx->subscription_id > 0) {
        g_dbus_connection_signal_unsubscribe(ctx->connection, ctx->subscription_id);
    }

    /* Quit and unref main loop */
    if (ctx->main_loop) {
        if (g_main_loop_is_running(ctx->main_loop)) {
            g_main_loop_quit(ctx->main_loop);
        }
        g_main_loop_unref(ctx->main_loop);
        ctx->main_loop = NULL;
    }

    /* Pop and unref context */
    if (ctx->context) {
        g_main_context_pop_thread_default(ctx->context);
        g_main_context_unref(ctx->context);
        ctx->context = NULL;
    }

    /* Release D-Bus connection (shared connection — unref only, don't close) */
    if (ctx->connection) {
        g_object_unref(ctx->connection);
        ctx->connection = NULL;
    }

    /* Clear in-progress flag BEFORE freeing ctx */
    internal_end_check();

    /* Destroy synchronization primitives */
    pthread_mutex_destroy(&ctx->ready_mutex);
    pthread_cond_destroy(&ctx->ready_cond);

    /* Free strdup'd strings */
    free(ctx->handle_key);

    /* Free context */
    free(ctx);

    FWUPMGR_INFO("internal_check_worker_thread: thread exiting\n");
    return NULL;
}

/* ========================================================================
 * internal_download_worker_thread — Phase 2: DownloadFirmware
 * ======================================================================== */

void *internal_download_worker_thread(void *arg)
{
    DownloadRequestContext *ctx = (DownloadRequestContext *)arg;

    FWUPMGR_INFO("internal_download_worker_thread: starting for handle='%s' "
                 "firmware='%s'\n",
                 ctx->handle_key ? ctx->handle_key : "(null)",
                 ctx->firmware_name ? ctx->firmware_name : "(null)");

    /* ---- Step 1: Create isolated GLib event loop ---- */
    ctx->context = g_main_context_new();
    g_main_context_push_thread_default(ctx->context);
    ctx->main_loop = g_main_loop_new(ctx->context, FALSE);

    /* ---- Step 2: Connect to D-Bus system bus ---- */
    GError *error = NULL;
    ctx->connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);

    if (ctx->connection == NULL) {
        FWUPMGR_ERROR("internal_download_worker_thread: D-Bus connect failed: %s\n",
                      error ? error->message : "unknown");
        if (error) g_error_free(error);

        pthread_mutex_lock(&ctx->ready_mutex);
        ctx->init_failed = true;
        ctx->is_ready = true;
        pthread_cond_signal(&ctx->ready_cond);
        pthread_mutex_unlock(&ctx->ready_mutex);

        goto cleanup;
    }

    FWUPMGR_INFO("internal_download_worker_thread: D-Bus connected\n");

    /* ---- Step 3: Subscribe to DownloadProgress signal ---- */
    ctx->subscription_id = g_dbus_connection_signal_subscribe(
        ctx->connection,
        DBUS_SERVICE_NAME,
        DBUS_INTERFACE_NAME,
        DBUS_SIGNAL_DWNL_PROGRESS,
        DBUS_OBJECT_PATH,
        NULL,
        G_DBUS_SIGNAL_FLAGS_NONE,
        on_download_signal_handler,
        ctx,
        NULL);

    FWUPMGR_INFO("internal_download_worker_thread: subscribed to %s (id=%u)\n",
                 DBUS_SIGNAL_DWNL_PROGRESS, ctx->subscription_id);

    /* ---- Step 4: Send DownloadFirmware D-Bus method call SYNCHRONOUSLY ----
     *
     * D-Bus signature IN: (ssss) — handlerId, firmwareName, downloadUrl, typeOfFirmware
     * D-Bus signature OUT: (sss) — result, status, message
     *
     * We call synchronously so we can read the daemon's accept/reject reply
     * BEFORE signaling the caller. This gives the caller an ACCURATE return value.
     */
    FWUPMGR_INFO("internal_download_worker_thread: calling DownloadFirmware "
                 "synchronously...\n");

    GVariant *reply = g_dbus_connection_call_sync(
        ctx->connection,
        DBUS_SERVICE_NAME,
        DBUS_OBJECT_PATH,
        DBUS_INTERFACE_NAME,
        DBUS_METHOD_DOWNLOAD,
        g_variant_new("(ssss)",
                      ctx->handle_key,
                      ctx->firmware_name,
                      ctx->firmware_url ? ctx->firmware_url : "",
                      ctx->firmware_type ? ctx->firmware_type : ""),
        G_VARIANT_TYPE("(sss)"),          /* expected reply signature */
        G_DBUS_CALL_FLAGS_NONE,
        30000,                            /* 30s timeout for method call itself */
        NULL,                             /* cancellable */
        &error);

    if (reply == NULL) {
        FWUPMGR_ERROR("internal_download_worker_thread: D-Bus call failed: %s\n",
                      error ? error->message : "unknown");
        if (error) g_error_free(error);

        pthread_mutex_lock(&ctx->ready_mutex);
        ctx->init_failed = true;
        ctx->is_ready = true;
        pthread_cond_signal(&ctx->ready_cond);
        pthread_mutex_unlock(&ctx->ready_mutex);

        goto cleanup;
    }

    /* Parse daemon's (sss) reply: result, status, message */
    const gchar *result_str = NULL;
    const gchar *status_str = NULL;
    const gchar *message_str = NULL;
    g_variant_get(reply, "(&s&s&s)", &result_str, &status_str, &message_str);

    FWUPMGR_INFO("internal_download_worker_thread: daemon reply: "
                 "result='%s' status='%s' message='%s'\n",
                 result_str ? result_str : "(null)",
                 status_str ? status_str : "(null)",
                 message_str ? message_str : "(null)");

    /* Check if daemon accepted or rejected */
    if (result_str && strcmp(result_str, "RDKFW_DWNL_SUCCESS") == 0) {
        ctx->daemon_accepted = true;
        FWUPMGR_INFO("internal_download_worker_thread: daemon ACCEPTED download\n");
    } else {
        ctx->daemon_accepted = false;
        ctx->daemon_reject_message = (message_str && message_str[0])
                                     ? strdup(message_str) : NULL;
        FWUPMGR_WARN("internal_download_worker_thread: daemon REJECTED download: %s\n",
                     message_str ? message_str : "(no message)");
    }

    g_variant_unref(reply);

    /* If daemon rejected, signal caller with failure and exit */
    if (!ctx->daemon_accepted) {
        pthread_mutex_lock(&ctx->ready_mutex);
        ctx->init_failed = true;
        ctx->is_ready = true;
        pthread_cond_signal(&ctx->ready_cond);
        pthread_mutex_unlock(&ctx->ready_mutex);

        goto cleanup;
    }

    /* ---- Step 5: Add timeout source (3600s) ---- */
    ctx->timeout_source = g_timeout_source_new_seconds(DWNL_SIGNAL_TIMEOUT_SECONDS);
    g_source_set_callback(ctx->timeout_source, on_download_timeout, ctx, NULL);
    g_source_attach(ctx->timeout_source, ctx->context);
    g_source_unref(ctx->timeout_source);  /* context holds ref now */
    ctx->timeout_source = NULL;           /* don't double-unref in cleanup */

    /* ---- Step 6: Signal caller "ready" with success ---- */
    pthread_mutex_lock(&ctx->ready_mutex);
    ctx->is_ready = true;
    pthread_cond_signal(&ctx->ready_cond);
    pthread_mutex_unlock(&ctx->ready_mutex);

    FWUPMGR_INFO("internal_download_worker_thread: signaled ready, "
                 "entering event loop\n");

    /* ---- Step 7: Run event loop — wait for DownloadProgress signals ---- */
    g_main_loop_run(ctx->main_loop);

    FWUPMGR_INFO("internal_download_worker_thread: event loop exited\n");

cleanup:
    FWUPMGR_INFO("internal_download_worker_thread: cleaning up\n");

    /* Unsubscribe from signal */
    if (ctx->connection && ctx->subscription_id > 0) {
        g_dbus_connection_signal_unsubscribe(ctx->connection, ctx->subscription_id);
    }

    /* Quit and unref main loop */
    if (ctx->main_loop) {
        if (g_main_loop_is_running(ctx->main_loop)) {
            g_main_loop_quit(ctx->main_loop);
        }
        g_main_loop_unref(ctx->main_loop);
        ctx->main_loop = NULL;
    }

    /* Pop and unref context */
    if (ctx->context) {
        g_main_context_pop_thread_default(ctx->context);
        g_main_context_unref(ctx->context);
        ctx->context = NULL;
    }

    /* Release D-Bus connection */
    if (ctx->connection) {
        g_object_unref(ctx->connection);
        ctx->connection = NULL;
    }

    /* Clear in-progress flag BEFORE freeing ctx */
    internal_end_download();

    /* Destroy synchronization primitives */
    pthread_mutex_destroy(&ctx->ready_mutex);
    pthread_cond_destroy(&ctx->ready_cond);

    /* Free strdup'd strings */
    free(ctx->handle_key);
    free(ctx->firmware_name);
    free(ctx->firmware_url);
    free(ctx->firmware_type);
    free(ctx->daemon_reject_message);

    /* Free context */
    free(ctx);

    FWUPMGR_INFO("internal_download_worker_thread: thread exiting\n");
    return NULL;
}

/* ========================================================================
 * internal_update_worker_thread — Phase 3: UpdateFirmware
 * ======================================================================== */

void *internal_update_worker_thread(void *arg)
{
    UpdateRequestContext *ctx = (UpdateRequestContext *)arg;

    FWUPMGR_INFO("internal_update_worker_thread: starting for handle='%s' "
                 "firmware='%s' type='%s' location='%s' reboot='%s'\n",
                 ctx->handle_key ? ctx->handle_key : "(null)",
                 ctx->firmware_name ? ctx->firmware_name : "(null)",
                 ctx->firmware_type ? ctx->firmware_type : "(null)",
                 ctx->firmware_location ? ctx->firmware_location : "(default)",
                 ctx->reboot_flag ? ctx->reboot_flag : "(null)");

    /* ---- Step 1: Create isolated GLib event loop ---- */
    ctx->context = g_main_context_new();
    g_main_context_push_thread_default(ctx->context);
    ctx->main_loop = g_main_loop_new(ctx->context, FALSE);

    /* ---- Step 2: Connect to D-Bus system bus ---- */
    GError *error = NULL;
    ctx->connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);

    if (ctx->connection == NULL) {
        FWUPMGR_ERROR("internal_update_worker_thread: D-Bus connect failed: %s\n",
                      error ? error->message : "unknown");
        if (error) g_error_free(error);

        pthread_mutex_lock(&ctx->ready_mutex);
        ctx->init_failed = true;
        ctx->is_ready = true;
        pthread_cond_signal(&ctx->ready_cond);
        pthread_mutex_unlock(&ctx->ready_mutex);

        goto cleanup;
    }

    FWUPMGR_INFO("internal_update_worker_thread: D-Bus connected\n");

    /* ---- Step 3: Subscribe to UpdateProgress signal ---- */
    ctx->subscription_id = g_dbus_connection_signal_subscribe(
        ctx->connection,
        DBUS_SERVICE_NAME,
        DBUS_INTERFACE_NAME,
        DBUS_SIGNAL_UPDATE_PROGRESS,
        DBUS_OBJECT_PATH,
        NULL,
        G_DBUS_SIGNAL_FLAGS_NONE,
        on_update_signal_handler,
        ctx,
        NULL);

    FWUPMGR_INFO("internal_update_worker_thread: subscribed to %s (id=%u)\n",
                 DBUS_SIGNAL_UPDATE_PROGRESS, ctx->subscription_id);

    /* ---- Step 4: Send UpdateFirmware D-Bus method call SYNCHRONOUSLY ----
     *
     * D-Bus signature IN: (sssss) — handlerId, firmwareName, LocationOfFirmware,
     *                                TypeOfFirmware, rebootImmediately
     * D-Bus signature OUT: (sss) — UpdateResult, UpdateStatus, message
     *
     * We call synchronously so we can read the daemon's accept/reject reply
     * BEFORE signaling the caller. This gives the caller an ACCURATE return value.
     */
    FWUPMGR_INFO("internal_update_worker_thread: calling UpdateFirmware "
                 "synchronously...\n");

    GVariant *reply = g_dbus_connection_call_sync(
        ctx->connection,
        DBUS_SERVICE_NAME,
        DBUS_OBJECT_PATH,
        DBUS_INTERFACE_NAME,
        DBUS_METHOD_UPDATE,
        g_variant_new("(sssss)",
                      ctx->handle_key,
                      ctx->firmware_name,
                      ctx->firmware_location ? ctx->firmware_location : "",
                      ctx->firmware_type ? ctx->firmware_type : "",
                      ctx->reboot_flag ? ctx->reboot_flag : "false"),
        G_VARIANT_TYPE("(sss)"),          /* expected reply signature */
        G_DBUS_CALL_FLAGS_NONE,
        30000,                            /* 30s timeout for method call itself */
        NULL,                             /* cancellable */
        &error);

    if (reply == NULL) {
        FWUPMGR_ERROR("internal_update_worker_thread: D-Bus call failed: %s\n",
                      error ? error->message : "unknown");
        if (error) g_error_free(error);

        pthread_mutex_lock(&ctx->ready_mutex);
        ctx->init_failed = true;
        ctx->is_ready = true;
        pthread_cond_signal(&ctx->ready_cond);
        pthread_mutex_unlock(&ctx->ready_mutex);

        goto cleanup;
    }

    /* Parse daemon's (sss) reply: UpdateResult, UpdateStatus, message */
    const gchar *result_str = NULL;
    const gchar *status_str = NULL;
    const gchar *message_str = NULL;
    g_variant_get(reply, "(&s&s&s)", &result_str, &status_str, &message_str);

    FWUPMGR_INFO("internal_update_worker_thread: daemon reply: "
                 "result='%s' status='%s' message='%s'\n",
                 result_str ? result_str : "(null)",
                 status_str ? status_str : "(null)",
                 message_str ? message_str : "(null)");

    /* Check if daemon accepted or rejected */
    if (result_str && strcmp(result_str, "RDKFW_UPDATE_SUCCESS") == 0) {
        ctx->daemon_accepted = true;
        FWUPMGR_INFO("internal_update_worker_thread: daemon ACCEPTED update\n");
    } else {
        ctx->daemon_accepted = false;
        ctx->daemon_reject_message = (message_str && message_str[0])
                                     ? strdup(message_str) : NULL;
        FWUPMGR_WARN("internal_update_worker_thread: daemon REJECTED update: %s\n",
                     message_str ? message_str : "(no message)");
    }

    g_variant_unref(reply);

    /* If daemon rejected, signal caller with failure and exit */
    if (!ctx->daemon_accepted) {
        pthread_mutex_lock(&ctx->ready_mutex);
        ctx->init_failed = true;
        ctx->is_ready = true;
        pthread_cond_signal(&ctx->ready_cond);
        pthread_mutex_unlock(&ctx->ready_mutex);

        goto cleanup;
    }

    /* ---- Step 5: Add timeout source (3600s) ---- */
    ctx->timeout_source = g_timeout_source_new_seconds(UPDATE_SIGNAL_TIMEOUT_SECONDS);
    g_source_set_callback(ctx->timeout_source, on_update_timeout, ctx, NULL);
    g_source_attach(ctx->timeout_source, ctx->context);
    g_source_unref(ctx->timeout_source);  /* context holds ref now */
    ctx->timeout_source = NULL;           /* don't double-unref in cleanup */

    /* ---- Step 6: Signal caller "ready" with success ---- */
    pthread_mutex_lock(&ctx->ready_mutex);
    ctx->is_ready = true;
    pthread_cond_signal(&ctx->ready_cond);
    pthread_mutex_unlock(&ctx->ready_mutex);

    FWUPMGR_INFO("internal_update_worker_thread: signaled ready, "
                 "entering event loop\n");

    /* ---- Step 7: Run event loop — wait for UpdateProgress signals ---- */
    g_main_loop_run(ctx->main_loop);

    FWUPMGR_INFO("internal_update_worker_thread: event loop exited\n");

cleanup:
    FWUPMGR_INFO("internal_update_worker_thread: cleaning up\n");

    /* Unsubscribe from signal */
    if (ctx->connection && ctx->subscription_id > 0) {
        g_dbus_connection_signal_unsubscribe(ctx->connection, ctx->subscription_id);
    }

    /* Quit and unref main loop */
    if (ctx->main_loop) {
        if (g_main_loop_is_running(ctx->main_loop)) {
            g_main_loop_quit(ctx->main_loop);
        }
        g_main_loop_unref(ctx->main_loop);
        ctx->main_loop = NULL;
    }

    /* Pop and unref context */
    if (ctx->context) {
        g_main_context_pop_thread_default(ctx->context);
        g_main_context_unref(ctx->context);
        ctx->context = NULL;
    }

    /* Release D-Bus connection */
    if (ctx->connection) {
        g_object_unref(ctx->connection);
        ctx->connection = NULL;
    }

    /* Clear in-progress flag BEFORE freeing ctx */
    internal_end_update();

    /* Destroy synchronization primitives */
    pthread_mutex_destroy(&ctx->ready_mutex);
    pthread_cond_destroy(&ctx->ready_cond);

    /* Free strdup'd strings */
    free(ctx->handle_key);
    free(ctx->firmware_name);
    free(ctx->firmware_location);
    free(ctx->firmware_type);
    free(ctx->reboot_flag);
    free(ctx->daemon_reject_message);

    /* Free context */
    free(ctx);

    FWUPMGR_INFO("internal_update_worker_thread: thread exiting\n");
    return NULL;
}

/* ========================================================================
 * SIGNAL DATA HELPERS
 * ======================================================================== */

/**
 * @brief Parse GVariant into InternalSignalData
 *
 * Expected signature: (tiissss)
 *   t  handler_id (uint64) - identifies which client this is for
 *   i  result_code
 *   i  status_code
 *   s  current_version
 *   s  available_version
 *   s  update_details
 *   s  status_message
 */
bool internal_parse_signal_data(GVariant *parameters, InternalSignalData *out_data)
{
    if (parameters == NULL || out_data == NULL) return false;

    const gchar *sig = g_variant_get_type_string(parameters);
    if (strcmp(sig, "(tiissss)") != 0) {
        FWUPMGR_ERROR("internal_parse_signal_data: unexpected signature '%s'\n", sig);
        return false;
    }

    const gchar *cur = NULL, *avail = NULL, *details = NULL, *msg = NULL;
    guint64 handler_id = 0;
    gint32 result = 0, status = 0;

    g_variant_get(parameters, "(tiissss)",
                  &handler_id, &result, &status, &cur, &avail, &details, &msg);

    out_data->result_code       = (int32_t)result;
    out_data->status_code       = (int32_t)status;
    out_data->current_version   = cur     ? strdup(cur)     : NULL;
    out_data->available_version = avail   ? strdup(avail)   : NULL;
    out_data->update_details    = details ? strdup(details) : NULL;
    out_data->status_message    = msg     ? strdup(msg)     : NULL;

    return true;
}

void internal_cleanup_signal_data(InternalSignalData *data)
{
    free(data->current_version);
    free(data->available_version);
    free(data->update_details);
    free(data->status_message);
    memset(data, 0, sizeof(InternalSignalData));
}

CheckForUpdateStatus internal_map_status_code(int32_t status_code)
{
    switch (status_code) {
        case 0:  return FIRMWARE_AVAILABLE;
        case 1:  return FIRMWARE_NOT_AVAILABLE;
        case 2:  return UPDATE_NOT_ALLOWED;
        case 3:  return FIRMWARE_CHECK_ERROR;
        case 4:  return IGNORE_OPTOUT;
        case 5:  return BYPASS_OPTOUT;
        default:
            FWUPMGR_ERROR("internal_map_status_code: unknown %d → FIRMWARE_CHECK_ERROR\n",
                          status_code);
            return FIRMWARE_CHECK_ERROR;
    }
}


/* ========================================================================
 * DOWNLOAD SIGNAL DATA HELPERS
 * ======================================================================== */

/**
 * @brief Parse GVariant DownloadProgress payload
 *
 * Expected GVariant signature: (tsuss)
 *   t  handlerId         (uint64)
 *   s  firmwareName      (string)
 *   u  progressPercent   (uint32)
 *   s  status            (string - "INPROGRESS", "COMPLETED", "ERROR")
 *   s  message           (string)
 */
bool internal_parse_dwnl_signal_data(GVariant *parameters,
                                      InternalDwnlSignalData *out_data)
{
    if (parameters == NULL || out_data == NULL) return false;

    const gchar *sig = g_variant_get_type_string(parameters);
    if (strcmp(sig, "(tsuss)") != 0) {
        FWUPMGR_ERROR("internal_parse_dwnl_signal_data: "
                      "unexpected signature '%s' (expected '(tsuss)')\n", sig);
        return false;
    }

    guint64  handler_id = 0;
    gchar   *firmware_name = NULL;
    guint32  progress = 0;
    gchar   *status_str = NULL;
    gchar   *message_str = NULL;

    g_variant_get(parameters, "(tsuss)",
                  &handler_id,
                  &firmware_name,
                  &progress,
                  &status_str,
                  &message_str);

    out_data->handler_id       = handler_id;
    out_data->firmware_name    = firmware_name;    /* Caller must g_free */
    out_data->progress_percent = progress;
    out_data->status_string    = status_str;       /* Caller must g_free */
    out_data->message          = message_str;      /* Caller must g_free */

    return true;
}

/**
 * @brief Map status string from daemon to DownloadStatus enum
 */
static DownloadStatus map_dwnl_status_string(const char *status_str)
{
    if (status_str == NULL) {
        return DWNL_ERROR;
    }

    if (strcmp(status_str, "INPROGRESS") == 0 || strcmp(status_str, "NOTSTARTED") == 0) {
        return DWNL_IN_PROGRESS;
    } else if (strcmp(status_str, "COMPLETED") == 0) {
        return DWNL_COMPLETED;
    } else if (strcmp(status_str, "ERROR") == 0 || strcmp(status_str, "DWNL_ERROR") == 0) {
        return DWNL_ERROR;
    }

    FWUPMGR_ERROR("map_dwnl_status_string: unknown status '%s'\n", status_str);
    return DWNL_ERROR;
}

/* ========================================================================
 * UPDATE SIGNAL DATA HELPERS
 * ======================================================================== */

/**
 * @brief Parse GVariant UpdateProgress payload
 *
 * Expected GVariant signature: (tsiis)
 *   t  handlerId         (uint64)
 *   s  firmwareName      (string)
 *   i  progressPercent   (int32)
 *   i  statusCode        (int32)
 *   s  message           (string)
 */
bool internal_parse_update_signal_data(GVariant *parameters,
                                        InternalUpdateSignalData *out_data)
{
    if (parameters == NULL || out_data == NULL) return false;

    const gchar *sig = g_variant_get_type_string(parameters);
    if (strcmp(sig, "(tsiis)") != 0) {
        FWUPMGR_ERROR("internal_parse_update_signal_data: "
                      "unexpected signature '%s' (expected '(tsiis)')\n", sig);
        return false;
    }

    guint64  handler_id = 0;
    gchar   *firmware_name = NULL;
    gint32   progress = 0;
    gint32   status = 0;
    gchar   *message_str = NULL;

    g_variant_get(parameters, "(tsiis)", 
                  &handler_id, 
                  &firmware_name, 
                  &progress, 
                  &status, 
                  &message_str);

    out_data->handler_id       = handler_id;
    out_data->firmware_name    = firmware_name;   // Caller must g_free
    out_data->progress_percent = progress;
    out_data->status_code      = status;
    out_data->message          = message_str;     // Caller must g_free

    return true;
}

/**
 * @brief Map raw integer to UpdateStatus enum
 */
UpdateStatus internal_map_update_status_code(int32_t status_code)
{
    switch (status_code) {
        case 0:  return UPDATE_IN_PROGRESS;
        case 1:  return UPDATE_COMPLETED;
        case 2:  return UPDATE_ERROR;
        default:
            FWUPMGR_ERROR("internal_map_update_status_code: "
                          "unknown %d → UPDATE_ERROR\n", status_code);
            return UPDATE_ERROR;
    }
}

/* ========================================================================
 * UPDATE DETAILS PARSING
 * ======================================================================== */

/**
 * @brief Parse update_details string into UpdateDetails structure
 *
 * The update_details string from the daemon is a pipe-separated key:value format:
 *   "File:filename.bin|Location:https://...|Version:1.0|..."
 *
 * This function safely parses it and populates the UpdateDetails structure.
 *
 * @param update_details_str   Pipe-separated string from daemon (may be NULL)
 * @param out_details          Output UpdateDetails structure (must be allocated)
 * @return true if parsing succeeded (even if string was NULL/empty),
 *         false only on critical errors
 *
 * Thread safety: Safe - operates on local data only
 * Memory: out_details is caller-allocated, this function fills arrays
 */
static bool parse_update_details(const char *update_details_str,
                                   UpdateDetails *out_details)
{
    if (out_details == NULL) {
        FWUPMGR_ERROR("parse_update_details: out_details is NULL\n");
        return false;
    }

    /* Zero-initialize the output structure */
    memset(out_details, 0, sizeof(UpdateDetails));

    /* Empty or NULL input is valid - just means no details available */
    if (update_details_str == NULL || update_details_str[0] == '\0') {
        FWUPMGR_INFO("parse_update_details: empty input, returning zeroed structure\n");
        return true;
    }

    FWUPMGR_INFO("parse_update_details: parsing '%s'\n", update_details_str);

    /* Make a working copy since strtok modifies the string */
    char *work_str = strdup(update_details_str);
    if (work_str == NULL) {
        FWUPMGR_ERROR("parse_update_details: strdup failed\n");
        return false;
    }

    /* Parse pipe-separated key:value pairs (daemon uses | not ,) */
    char *saveptr = NULL;
    char *token = strtok_r(work_str, "|", &saveptr);

    while (token != NULL) {
        /* Split on ':' to get key and value */
        char *colon = strchr(token, ':');
        if (colon == NULL) {
            /* Malformed token, skip it */
            FWUPMGR_ERROR("parse_update_details: malformed token '%s' (no colon)\n", token);
            token = strtok_r(NULL, "|", &saveptr);
            continue;
        }

        /* Null-terminate the key and get the value */
        *colon = '\0';
        const char *key = token;
        const char *value = colon + 1;

        /* Match keys and copy values into appropriate fields
         * Daemon uses: File, Location, Version, Reboot, Delay, PDRI, Peripherals
         * We map them to our struct fields */
        if (strcmp(key, "File") == 0) {
            strncpy(out_details->FwFileName, value, 
                    sizeof(out_details->FwFileName) - 1);
        }
        else if (strcmp(key, "Location") == 0 || strcmp(key, "IPv6Location") == 0) {
            /* Use Location if not empty, fallback to IPv6Location */
            if (strcmp(value, "N/A") != 0 && value[0] != '\0') {
                strncpy(out_details->FwUrl, value,
                        sizeof(out_details->FwUrl) - 1);
            }
        }
        else if (strcmp(key, "Version") == 0) {
            strncpy(out_details->FwVersion, value,
                    sizeof(out_details->FwVersion) - 1);
        }
        else if (strcmp(key, "Reboot") == 0) {
            strncpy(out_details->RebootImmediately, value,
                    sizeof(out_details->RebootImmediately) - 1);
        }
        else if (strcmp(key, "Delay") == 0) {
            strncpy(out_details->DelayDownload, value,
                    sizeof(out_details->DelayDownload) - 1);
        }
        else if (strcmp(key, "PDRI") == 0) {
            if (strcmp(value, "N/A") != 0) {
                strncpy(out_details->PDRIVersion, value,
                        sizeof(out_details->PDRIVersion) - 1);
            }
        }
        else if (strcmp(key, "Peripherals") == 0) {
            if (strcmp(value, "N/A") != 0) {
                strncpy(out_details->PeripheralFirmwares, value,
                        sizeof(out_details->PeripheralFirmwares) - 1);
            }
        }
        else if (strcmp(key, "Protocol") == 0 || strcmp(key, "CertBundle") == 0) {
            /* These fields exist in daemon format but not in our struct - ignore */
            FWUPMGR_INFO("parse_update_details: skipping field '%s'='%s'\n", key, value);
        }
        else {
            /* Unknown key - log but don't fail */
            FWUPMGR_INFO("parse_update_details: unknown key '%s', ignoring\n", key);
        }

        token = strtok_r(NULL, "|", &saveptr);
    }

    free(work_str);

    FWUPMGR_INFO("parse_update_details: parsed successfully\n");
    FWUPMGR_INFO("  FwFileName: %s\n", out_details->FwFileName);
    FWUPMGR_INFO("  FwVersion: %s\n", out_details->FwVersion);

    return true;
}


