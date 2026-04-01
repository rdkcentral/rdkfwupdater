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
 * @file rdkFwupdateMgr_async_internal.h
 * @brief Internal types and declarations — NOT part of public API
 *
 * ARCHITECTURE OVERVIEW (Phase 1+2+3 — All APIs use on-demand worker threads):
 * ==============================================================================
 *
 * CheckForUpdate (ON-DEMAND WORKER THREAD — Phase 1):
 *
 *  App calls checkForUpdate(handle, callback)
 *        │
 *        ├─ Allocate CheckRequestContext on heap
 *        ├─ pthread_create(internal_check_worker_thread, ctx)
 *        │        │
 *        │        ├─ New GMainContext + GMainLoop (isolated)
 *        │        ├─ g_bus_get_sync() → D-Bus connection
 *        │        ├─ Subscribe to CheckForUpdateComplete signal
 *        │        ├─ Send CheckForUpdate D-Bus method call
 *        │        ├─ Signal caller "ready" via condvar
 *        │        ├─ g_main_loop_run() — waits for signal or 120s timeout
 *        │        ├─ Signal arrives → parse → callback(&fwinfo_data)
 *        │        └─ Cleanup everything, free(ctx), thread exits
 *        │
 *        ├─ pthread_cond_wait() for ready signal
 *        └─ Return SUCCESS or FAIL
 *
 * DownloadFirmware (ON-DEMAND WORKER THREAD — Phase 2):
 *
 *  App calls downloadFirmware(handle, request, callback)
 *        │
 *        ├─ Allocate DownloadRequestContext on heap
 *        ├─ pthread_create(internal_download_worker_thread, ctx)
 *        │        │
 *        │        ├─ New GMainContext + GMainLoop (isolated)
 *        │        ├─ g_bus_get_sync() → D-Bus connection
 *        │        ├─ Subscribe to DownloadProgress signal
 *        │        ├─ g_dbus_connection_call_sync("DownloadFirmware") — SYNCHRONOUS
 *        │        │   → reads daemon's (sss) reply: accept or reject
 *        │        ├─ Signal caller "ready" via condvar
 *        │        ├─ g_main_loop_run() — waits for DownloadProgress signals
 *        │        │   → callback fires MULTIPLE times (per progress signal)
 *        │        │   → quits loop ONLY on COMPLETED/ERROR (terminal status)
 *        │        └─ Cleanup everything, free(ctx), thread exits
 *        │
 *        ├─ pthread_cond_wait() for ready signal (includes daemon reply)
 *        └─ Return SUCCESS or FAIL (accurate — reflects daemon's accept/reject)
 *
 * UpdateFirmware (ON-DEMAND WORKER THREAD — Phase 3):
 *
 *  App calls updateFirmware(handle, request, callback)
 *        │
 *        ├─ Allocate UpdateRequestContext on heap
 *        ├─ pthread_create(internal_update_worker_thread, ctx)
 *        │        │
 *        │        ├─ New GMainContext + GMainLoop (isolated)
 *        │        ├─ g_bus_get_sync() → D-Bus connection
 *        │        ├─ Subscribe to UpdateProgress signal
 *        │        ├─ g_dbus_connection_call_sync("UpdateFirmware") — SYNCHRONOUS
 *        │        │   → reads daemon's (sss) reply: accept or reject
 *        │        ├─ Signal caller "ready" via condvar
 *        │        ├─ g_main_loop_run() — waits for UpdateProgress signals
 *        │        │   → callback fires MULTIPLE times (per progress signal)
 *        │        │   → quits loop ONLY on COMPLETED/ERROR (terminal status)
 *        │        └─ Cleanup everything, free(ctx), thread exits
 *        │
 *        ├─ pthread_cond_wait() for ready signal (includes daemon reply)
 *        └─ Return SUCCESS or FAIL (accurate — reflects daemon's accept/reject)
 *
 * THREAD SAFETY:
 * ==============
 *   CheckForUpdate: per-request ctx protected by ctx->ready_mutex (handshake),
 *                   g_check_in_progress protected by g_check_in_progress_mutex.
 *   DownloadFirmware: per-request ctx protected by ctx->ready_mutex (handshake),
 *                     g_dwnl_in_progress protected by g_dwnl_in_progress_mutex.
 *   UpdateFirmware: per-request ctx protected by ctx->ready_mutex (handshake),
 *                   g_update_in_progress protected by g_update_in_progress_mutex.
 *   Callbacks invoked with mutex RELEASED (deadlock prevention).
 */

#ifndef RDKFWUPDATEMGR_ASYNC_INTERNAL_H
#define RDKFWUPDATEMGR_ASYNC_INTERNAL_H

#include "rdkFwupdateMgr_client.h"
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>
#include <gio/gio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * CONSTANTS
 * ======================================================================== */

#define DBUS_SERVICE_NAME        "org.rdkfwupdater.Service"
#define DBUS_OBJECT_PATH         "/org/rdkfwupdater/Service"
#define DBUS_INTERFACE_NAME      "org.rdkfwupdater.Interface"
#define DBUS_METHOD_CHECK        "CheckForUpdate"
#define DBUS_SIGNAL_COMPLETE     "CheckForUpdateComplete"
#define DBUS_TIMEOUT_MS          5000

/* Timeout for worker thread waiting for daemon signal (seconds) */
#define CHECK_SIGNAL_TIMEOUT_SECONDS 120

/* ========================================================================
 * CHECKFORUPDATE — ON-DEMAND WORKER THREAD CONTEXT (Phase 1)
 * ======================================================================== */

/**
 * @brief Per-request context for on-demand CheckForUpdate worker thread.
 *
 * Lifecycle:
 *   - Allocated by checkForUpdate() (caller thread) via calloc
 *   - Ownership transferred to worker thread after condvar handshake
 *   - Freed by worker thread after callback fires (or timeout/error)
 *
 * Memory: ~100 bytes (excluding GLib objects)
 */
typedef struct {
    /* Condvar handshake: worker signals "I'm ready" to caller */
    pthread_mutex_t   ready_mutex;
    pthread_cond_t    ready_cond;
    bool              is_ready;       /**< true = worker finished setup      */
    bool              init_failed;    /**< true = D-Bus connect/subscribe failed */

    /**
     * Handshake lifetime ownership flag.
     *
     * When true, the CALLER (API function) owns cleanup of the sync primitives
     * (ready_mutex, ready_cond) and the ctx allocation itself. The worker thread
     * must NOT destroy the mutex/cond or free(ctx) — it only cleans up GLib
     * resources and strdup'd strings.
     *
     * Set to true by the worker BEFORE signaling ready on init-failure paths.
     * This prevents the race where the worker destroys the mutex/cond/ctx while
     * the caller is still inside pthread_cond_timedwait or about to call
     * pthread_mutex_unlock.
     *
     * On success paths, this remains false — the worker owns everything after
     * the handshake completes and the caller never touches ctx again.
     */
    bool              caller_owns_cleanup;

    /* GLib event loop (isolated, per-thread) */
    GMainContext     *context;
    GMainLoop        *main_loop;
    GDBusConnection  *connection;
    guint             subscription_id;

    /* Request data */
    char             *handle_key;     /**< strdup of FirmwareInterfaceHandle */
    UpdateEventCallback callback;     /**< Client's callback function ptr    */

    /* Thread handle (for join in destructor) */
    pthread_t         thread;
} CheckRequestContext;

/* ========================================================================
 * INTERNAL SIGNAL DATA
 * ======================================================================== */

/**
 * @brief Raw parsed payload from CheckForUpdateComplete D-Bus signal
 *
 * All strings are malloc'd copies. Freed by internal_cleanup_signal_data()
 * after all callbacks have been dispatched.
 */
typedef struct {
    int32_t  result_code;         /**< Raw result from daemon                */
    int32_t  status_code;         /**< Maps to CheckForUpdateStatus enum     */
    char    *current_version;     /**< malloc'd, may be NULL                 */
    char    *available_version;   /**< malloc'd, may be NULL                 */
    char    *update_details;      /**< malloc'd, may be NULL                 */
    char    *status_message;      /**< malloc'd, may be NULL                 */
} InternalSignalData;

/* ========================================================================
 * INTERNAL FUNCTION DECLARATIONS — CheckForUpdate
 * ======================================================================== */

/**
 * @brief Worker thread entry point for on-demand CheckForUpdate.
 *
 * Creates an isolated GLib event loop, connects to D-Bus, subscribes to
 * CheckForUpdateComplete signal, sends CheckForUpdate D-Bus method call,
 * then waits for the signal (with 120s timeout). Fires the client's
 * callback when signal arrives, then cleans up all resources and exits.
 *
 * @param arg  CheckRequestContext* (ownership transferred from caller)
 * @return NULL
 */
void *internal_check_worker_thread(void *arg);

/**
 * @brief Query whether a checkForUpdate() operation is currently in progress.
 *
 * Used by unregisterProcess() to enforce the session-state invariant:
 * a client cannot unregister while it has outstanding operations.
 *
 * Thread-safe: protected by internal mutex.
 *
 * @return true if a checkForUpdate worker thread is active, false otherwise.
 */
bool internal_is_check_in_progress(void);

/**
 * @brief Atomically begin a checkForUpdate session and track the context.
 *
 * Sets g_check_in_progress = true and stores ctx in g_active_check_ctx.
 * If a check is already in progress, returns false without modifying state.
 *
 * TL;DR: Replaces direct extern access to g_check_in_progress + g_active_check_ctx.
 * All mutex handling is internal — callers never touch the mutex.
 *
 * @param ctx  The newly allocated CheckRequestContext to track.
 * @return true if session started, false if another check is already active.
 */
bool internal_begin_check(CheckRequestContext *ctx);

/**
 * @brief Atomically end the checkForUpdate session and untrack the context.
 *
 * Sets g_check_in_progress = false and g_active_check_ctx = NULL.
 * Called by the worker thread in cleanup, BEFORE freeing ctx.
 */
void internal_end_check(void);

/**
 * @brief Atomically clear in-progress state on error paths.
 *
 * Same as internal_end_check() but used when checkForUpdate() itself fails
 * (e.g., pthread_create fails after internal_begin_check succeeded).
 * The caller will free ctx directly.
 */
void internal_abort_check(void);

/**
 * @brief Cancel all active checkForUpdate worker threads and join them.
 *
 * Called from library destructor to ensure no threads are running
 * when library code is unmapped.
 */
void internal_cancel_all_active_check_threads(void);

/**
 * @brief Parse GVariant signal into InternalSignalData
 *
 * Expected GVariant signature: (tiissss)
 *   t  handler_id (uint64)
 *   i  result_code
 *   i  status_code
 *   s  current_version
 *   s  available_version
 *   s  update_details
 *   s  status_message
 *
 * @param parameters  GVariant from D-Bus signal
 * @param out_data    Output (must be zeroed before call)
 * @return true on success, false on parse error
 */
bool internal_parse_signal_data(GVariant *parameters,
                                 InternalSignalData *out_data);

/**
 * @brief Free all malloc'd strings in InternalSignalData
 */
void internal_cleanup_signal_data(InternalSignalData *data);

/**
 * @brief Map raw integer status_code to CheckForUpdateStatus enum
 */
CheckForUpdateStatus internal_map_status_code(int32_t status_code);

/* ========================================================================
 * DOWNLOAD FIRMWARE — ON-DEMAND WORKER THREAD (Phase 2)
 * ========================================================================
 *
 * ARCHITECTURE:
 *
 *  App calls downloadFirmware(handle, request, callback)
 *        │
 *        ├─ Allocate DownloadRequestContext on heap
 *        ├─ internal_begin_download(ctx) — reject if already active
 *        ├─ pthread_create(internal_download_worker_thread, ctx)
 *        │        │
 *        │        ├─ New GMainContext + GMainLoop (isolated)
 *        │        ├─ g_bus_get_sync() → D-Bus connection
 *        │        ├─ Subscribe to DownloadProgress signal
 *        │        ├─ g_dbus_connection_call_sync("DownloadFirmware")
 *        │        │   → daemon reply (sss): result, status, message
 *        │        │   → if FAILED: set init_failed, signal ready, cleanup
 *        │        │   → if SUCCESS: set daemon_accepted
 *        │        ├─ Add 3600s timeout
 *        │        ├─ Signal caller "ready" via condvar
 *        │        ├─ g_main_loop_run() — receives DownloadProgress signals
 *        │        │   → callback fires MULTIPLE times (per-signal)
 *        │        │   → quits loop ONLY on COMPLETED/ERROR
 *        │        └─ Cleanup everything, free(ctx), thread exits
 *        │
 *        ├─ pthread_cond_wait() for ready signal
 *        └─ Return SUCCESS or FAIL (accurate — reflects daemon reply)
 *
 * KEY DIFFERENCE FROM CheckForUpdate:
 *   CheckForUpdate: callback fires ONCE, then thread exits.
 *   DownloadFirmware: callback fires MULTIPLE TIMES (per progress signal),
 *                     thread exits only on terminal status (COMPLETED/ERROR).
 *
 * ======================================================================== */

#define DBUS_METHOD_DOWNLOAD         "DownloadFirmware"
#define DBUS_SIGNAL_DWNL_PROGRESS    "DownloadProgress"

/* Timeout for download worker thread (seconds) — 1 hour */
#define DWNL_SIGNAL_TIMEOUT_SECONDS  3600

/**
 * @brief Parsed payload from DownloadProgress D-Bus signal
 *
 * Daemon emits this repeatedly as download progresses.
 * GVariant signature: (tsuss)
 *   t  handlerId         (uint64 - handler ID)
 *   s  firmwareName      (string - firmware filename)
 *   u  progress          (uint32 - 0-100 percent)
 *   s  status            (string - "INPROGRESS", "COMPLETED", "ERROR")
 *   s  message           (string - human-readable message)
 */
typedef struct {
    uint64_t handler_id;         /**< Handler ID from daemon               */
    char    *firmware_name;      /**< Firmware filename (needs g_free)     */
    uint32_t progress_percent;   /**< 0–100                                */
    char    *status_string;      /**< Status string (needs g_free)         */
    char    *message;            /**< Message string (needs g_free)        */
} InternalDwnlSignalData;

/**
 * @brief Per-request context for on-demand DownloadFirmware worker thread.
 *
 * Lifecycle:
 *   - Allocated in downloadFirmware() (caller thread) via calloc
 *   - Ownership transferred to worker thread after condvar handshake
 *   - Freed by worker thread after download completes/fails (or timeout)
 *
 * Key differences from CheckRequestContext:
 *   - callback fires MULTIPLE times (per-progress-signal), not just once
 *   - worker quits loop ONLY on terminal status (COMPLETED/ERROR)
 *   - daemon_accepted: worker reads daemon's synchronous reply
 *   - longer timeout (3600s vs 120s)
 *
 * Memory: ~200 bytes (excluding GLib objects)
 */
typedef struct {
    /* Condvar handshake: worker signals "I'm ready" to caller */
    pthread_mutex_t   ready_mutex;
    pthread_cond_t    ready_cond;
    bool              is_ready;          /**< true = worker finished setup           */
    bool              init_failed;       /**< true = D-Bus failed or daemon rejected */

    /**
     * Handshake lifetime ownership flag.
     *
     * When true, the CALLER (API function) owns cleanup of the sync primitives
     * (ready_mutex, ready_cond) and the ctx allocation itself. The worker thread
     * must NOT destroy the mutex/cond or free(ctx).
     *
     * Set to true by the worker BEFORE signaling ready on init-failure paths.
     * This prevents the race where the worker destroys the mutex/cond/ctx while
     * the caller is still inside pthread_cond_timedwait or pthread_mutex_unlock.
     *
     * On success paths, this remains false — the worker owns everything.
     */
    bool              caller_owns_cleanup;

    /* GLib event loop (isolated, per-thread) */
    GMainContext     *context;
    GMainLoop        *main_loop;
    GDBusConnection  *connection;
    guint             subscription_id;

    /* Request data (all strdup'd — owned by worker thread) */
    char             *handle_key;        /**< strdup of FirmwareInterfaceHandle      */
    char             *firmware_name;     /**< strdup of request->firmwareName         */
    char             *firmware_url;      /**< strdup of request->downloadUrl          */
    char             *firmware_type;     /**< strdup of request->TypeOfFirmware       */
    DownloadCallback  callback;          /**< Client's callback function ptr          */

    /* Daemon reply (from synchronous D-Bus method return) */
    bool              daemon_accepted;   /**< true if daemon returned RDKFW_DWNL_SUCCESS */
    char             *daemon_reject_message; /**< strdup of daemon's error message (if rejected) */

    /* Timeout tracking */
    GSource          *timeout_source;    /**< For cancellation in cleanup             */

    /* Thread handle (for join in destructor) */
    pthread_t         thread;
} DownloadRequestContext;

/* ---- Download internal function declarations ---- */

/**
 * @brief Worker thread entry point for on-demand DownloadFirmware.
 *
 * Creates an isolated GLib event loop, connects to D-Bus, subscribes to
 * DownloadProgress signal, sends DownloadFirmware D-Bus method call
 * synchronously, then waits for progress signals (with 3600s timeout).
 * Fires the client's callback on every progress signal, quits loop on
 * COMPLETED or ERROR, then cleans up all resources and exits.
 *
 * @param arg  DownloadRequestContext* (ownership transferred from caller)
 * @return NULL
 */
void *internal_download_worker_thread(void *arg);

/**
 * @brief Atomically begin a downloadFirmware session and track the context.
 *
 * Sets g_dwnl_in_progress = true and stores ctx in g_active_dwnl_ctx.
 * If a download is already in progress, returns false without modifying state.
 *
 * @param ctx  The newly allocated DownloadRequestContext to track.
 * @return true if session started, false if another download is already active.
 */
bool internal_begin_download(DownloadRequestContext *ctx);

/**
 * @brief Atomically end the downloadFirmware session and untrack the context.
 *
 * Sets g_dwnl_in_progress = false and g_active_dwnl_ctx = NULL.
 * Called by the worker thread in cleanup, BEFORE freeing ctx.
 */
void internal_end_download(void);

/**
 * @brief Atomically clear download in-progress state on error paths.
 *
 * Same as internal_end_download() but used when downloadFirmware() itself
 * fails (e.g., pthread_create fails after internal_begin_download succeeded).
 */
void internal_abort_download(void);

/**
 * @brief Query whether a downloadFirmware() operation is currently in progress.
 *
 * Used by unregisterProcess() to enforce the session-state invariant.
 * Thread-safe: protected by internal mutex.
 *
 * @return true if a download worker thread is active, false otherwise.
 */
bool internal_is_dwnl_in_progress(void);

/**
 * @brief Cancel all active download worker threads and join them.
 *
 * Called from library destructor to ensure no threads are running
 * when library code is unmapped.
 */
void internal_cancel_all_active_download_threads(void);

/**
 * @brief Parse GVariant DownloadProgress signal payload
 *
 * Expected GVariant signature: (tsuss)
 *
 * @param parameters  GVariant from D-Bus signal
 * @param out_data    Output (must be zeroed before call)
 * @return true on success, false on parse error
 */
bool internal_parse_dwnl_signal_data(GVariant *parameters,
                                      InternalDwnlSignalData *out_data);

/**
 * @brief Map raw integer to DownloadStatus enum
 */
DownloadStatus internal_map_dwnl_status_code(int32_t status_code);

/* ========================================================================
 * UPDATE FIRMWARE — ON-DEMAND WORKER THREAD (Phase 3)
 * ========================================================================
 *
 * ARCHITECTURE:
 *
 *  App calls updateFirmware(handle, request, callback)
 *        │
 *        ├─ Allocate UpdateRequestContext on heap
 *        ├─ internal_begin_update(ctx) — reject if already active
 *        ├─ pthread_create(internal_update_worker_thread, ctx)
 *        │        │
 *        │        ├─ New GMainContext + GMainLoop (isolated)
 *        │        ├─ g_bus_get_sync() → D-Bus connection
 *        │        ├─ Subscribe to UpdateProgress signal
 *        │        ├─ g_dbus_connection_call_sync("UpdateFirmware")
 *        │        │   → daemon reply (sss): result, status, message
 *        │        │   → if FAILED: set init_failed, signal ready, cleanup
 *        │        │   → if SUCCESS: set daemon_accepted
 *        │        ├─ Add 3600s timeout
 *        │        ├─ Signal caller "ready" via condvar
 *        │        ├─ g_main_loop_run() — receives UpdateProgress signals
 *        │        │   → callback fires MULTIPLE times (per-signal)
 *        │        │   → quits loop ONLY on COMPLETED/ERROR
 *        │        └─ Cleanup everything, free(ctx), thread exits
 *        │
 *        ├─ pthread_cond_wait() for ready signal
 *        └─ Return SUCCESS or FAIL (accurate — reflects daemon reply)
 *
 * KEY SIMILARITY TO DownloadFirmware:
 *   Both APIs: callback fires MULTIPLE TIMES (per progress signal),
 *              thread exits only on terminal status (COMPLETED/ERROR).
 *   Both use condvar handshake with daemon synchronous reply for accurate return.
 *
 * ======================================================================== */

#define DBUS_METHOD_UPDATE              "UpdateFirmware"
#define DBUS_SIGNAL_UPDATE_PROGRESS     "UpdateProgress"

/* Timeout for update worker thread (seconds) — 1 hour */
#define UPDATE_SIGNAL_TIMEOUT_SECONDS   3600

/**
 * @brief Parsed payload from UpdateProgress D-Bus signal
 *
 * Daemon emits this repeatedly as firmware flashing progresses.
 * GVariant signature: (tsiis)
 *   t  handlerId         (uint64 - handler ID)
 *   s  firmwareName      (string - firmware filename)
 *   i  progress          (int32 - 0-100 percent)
 *   i  status            (int32 - status code)
 *   s  message           (string - human-readable message)
 */
typedef struct {
    uint64_t handler_id;         /**< Handler ID from daemon               */
    char    *firmware_name;      /**< Firmware filename (needs g_free)     */
    int32_t  progress_percent;   /**< 0–100                                */
    int32_t  status_code;        /**< Status code (maps to UpdateStatus)   */
    char    *message;            /**< Message string (needs g_free)        */
} InternalUpdateSignalData;

/**
 * @brief Per-request context for on-demand UpdateFirmware worker thread.
 *
 * Lifecycle:
 *   - Allocated in updateFirmware() (caller thread) via calloc
 *   - Ownership transferred to worker thread after condvar handshake
 *   - Freed by worker thread after update completes/fails (or timeout)
 *
 * Same pattern as DownloadRequestContext:
 *   - callback fires MULTIPLE times (per-progress-signal)
 *   - worker quits loop ONLY on terminal status (COMPLETED/ERROR)
 *   - daemon_accepted: worker reads daemon's synchronous reply
 *   - 3600s timeout
 *
 * Memory: ~200 bytes (excluding GLib objects)
 */
typedef struct {
    /* Condvar handshake: worker signals "I'm ready" to caller */
    pthread_mutex_t   ready_mutex;
    pthread_cond_t    ready_cond;
    bool              is_ready;          /**< true = worker finished setup           */
    bool              init_failed;       /**< true = D-Bus failed or daemon rejected */

    /**
     * Handshake lifetime ownership flag.
     *
     * When true, the CALLER (API function) owns cleanup of the sync primitives
     * (ready_mutex, ready_cond) and the ctx allocation itself. The worker thread
     * must NOT destroy the mutex/cond or free(ctx).
     *
     * Set to true by the worker BEFORE signaling ready on init-failure paths.
     * This prevents the race where the worker destroys the mutex/cond/ctx while
     * the caller is still inside pthread_cond_timedwait or pthread_mutex_unlock.
     *
     * On success paths, this remains false — the worker owns everything.
     */
    bool              caller_owns_cleanup;

    /* GLib event loop (isolated, per-thread) */
    GMainContext     *context;
    GMainLoop        *main_loop;
    GDBusConnection  *connection;
    guint             subscription_id;

    /* Request data (all strdup'd — owned by worker thread) */
    char             *handle_key;        /**< strdup of FirmwareInterfaceHandle      */
    char             *firmware_name;     /**< strdup of request->firmwareName         */
    char             *firmware_location; /**< strdup of request->LocationOfFirmware   */
    char             *firmware_type;     /**< strdup of request->TypeOfFirmware       */
    char             *reboot_flag;       /**< "true" or "false" string                */
    UpdateCallback    callback;          /**< Client's callback function ptr          */

    /* Daemon reply (from synchronous D-Bus method return) */
    bool              daemon_accepted;   /**< true if daemon returned RDKFW_UPDATE_SUCCESS */
    char             *daemon_reject_message; /**< strdup of daemon's error message (if rejected) */

    /* Timeout tracking */
    GSource          *timeout_source;    /**< For cancellation in cleanup             */

    /* Thread handle (for join in destructor) */
    pthread_t         thread;
} UpdateRequestContext;

/* ---- Update internal function declarations ---- */

/**
 * @brief Worker thread entry point for on-demand UpdateFirmware.
 *
 * Creates an isolated GLib event loop, connects to D-Bus, subscribes to
 * UpdateProgress signal, sends UpdateFirmware D-Bus method call
 * synchronously, then waits for progress signals (with 3600s timeout).
 * Fires the client's callback on every progress signal, quits loop on
 * COMPLETED or ERROR, then cleans up all resources and exits.
 *
 * @param arg  UpdateRequestContext* (ownership transferred from caller)
 * @return NULL
 */
void *internal_update_worker_thread(void *arg);

/**
 * @brief Atomically begin an updateFirmware session and track the context.
 *
 * Sets g_update_in_progress = true and stores ctx in g_active_update_ctx.
 * If an update is already in progress, returns false without modifying state.
 *
 * @param ctx  The newly allocated UpdateRequestContext to track.
 * @return true if session started, false if another update is already active.
 */
bool internal_begin_update(UpdateRequestContext *ctx);

/**
 * @brief Atomically end the updateFirmware session and untrack the context.
 *
 * Sets g_update_in_progress = false and g_active_update_ctx = NULL.
 * Called by the worker thread in cleanup, BEFORE freeing ctx.
 */
void internal_end_update(void);

/**
 * @brief Atomically clear update in-progress state on error paths.
 *
 * Same as internal_end_update() but used when updateFirmware() itself
 * fails (e.g., pthread_create fails after internal_begin_update succeeded).
 */
void internal_abort_update(void);

/**
 * @brief Query whether an updateFirmware() operation is currently in progress.
 *
 * Used by unregisterProcess() to enforce the session-state invariant.
 * Thread-safe: protected by internal mutex.
 *
 * @return true if an update worker thread is active, false otherwise.
 */
bool internal_is_update_in_progress(void);

/**
 * @brief Cancel all active update worker threads and join them.
 *
 * Called from library destructor to ensure no threads are running
 * when library code is unmapped.
 */
void internal_cancel_all_active_update_threads(void);

/**
 * @brief Parse GVariant UpdateProgress signal payload
 *
 * Expected GVariant signature: (tsiis)
 *   t  handlerId         (uint64)
 *   s  firmwareName      (string)
 *   i  progressPercent   (int32)
 *   i  statusCode        (int32)
 *   s  message           (string)
 *
 * @param parameters  GVariant from D-Bus signal
 * @param out_data    Output (must be zeroed before call)
 * @return true on success, false on parse error
 */
bool internal_parse_update_signal_data(GVariant *parameters,
                                        InternalUpdateSignalData *out_data);

/**
 * @brief Map raw integer to UpdateStatus enum
 */
UpdateStatus internal_map_update_status_code(int32_t status_code);


#ifdef __cplusplus
}
#endif

#endif /* RDKFWUPDATEMGR_ASYNC_INTERNAL_H */
