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
 * ARCHITECTURE OVERVIEW (Phase 1 — CheckForUpdate on-demand thread):
 * ==================================================================
 *
 * CheckForUpdate (ON-DEMAND WORKER THREAD — new):
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
 * Download / Update (PERSISTENT BG THREAD — unchanged):
 *
 *  App ──downloadFirmware(hdl, req, cb)──► DwnlRegistry ─┐
 *  App ──updateFirmware(hdl, req, cb)───► UpdateRegistry ─┼─► BG thread
 *                                                          │   watches D-Bus
 *                                                          ▼
 *                Daemon emits DownloadProgress / UpdateProgress
 *                        → dispatch to all ACTIVE callbacks
 *
 * THREAD SAFETY:
 * ==============
 *   CheckForUpdate: per-request ctx protected by ctx->ready_mutex (handshake),
 *                   g_check_in_progress protected by g_check_in_progress_mutex.
 *   Download/Update: registries protected by their own pthread_mutex.
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

#define MAX_PENDING_CALLBACKS    30  /* Reduced from 64 to keep stack usage < 10KB - Need to discuss the max number ; for now kept to 30 to resolve coverity issues*/

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
 * BACKGROUND THREAD (for Download/Update only — Phase 1)
 * ======================================================================== */

/**
 * @brief State for the background GLib event loop thread
 *
 * Started at library load. Subscribes to CheckForUpdateComplete signal.
 * Runs until library unload.
 */
typedef struct {
    pthread_t         thread;
    GMainLoop        *main_loop;
    GMainContext     *context;
    GDBusConnection  *connection;
    guint             subscription_id;
    bool              running;
    pthread_mutex_t   mutex;
} BackgroundThread;

/* ========================================================================
 * INTERNAL FUNCTION DECLARATIONS
 * ======================================================================== */

/**
 * @brief Initialize download/update registries and start background thread
 * Called from library __attribute__((constructor)).
 * @return 0 on success, -1 on error
 */
int  internal_system_init(void);

/**
 * @brief Stop background thread and free all resources
 * Called from library __attribute__((destructor)).
 */
void internal_system_deinit(void);

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
 * DOWNLOAD FIRMWARE — INTERNAL TYPES AND DECLARATIONS
 * ========================================================================
 *
 * ARCHITECTURE:
 *
 *  App A ──downloadFirmware(hdl_A, req_A, cb_A)──┐
 *  App B ──downloadFirmware(hdl_B, req_B, cb_B)──┼──► DwnlRegistry (keyed by handle)
 *  App C ──downloadFirmware(hdl_C, req_C, cb_C)──┘          │
 *                                                            │  same background thread
 *                                                            │  now also subscribed to
 *                                                            │  DownloadProgress signal
 *                                                            ▼
 *                   Daemon emits DownloadProgress(progress%, status) REPEATEDLY
 *                                                            │
 *                                      on_download_progress_signal()
 *                                                            │
 *                                    dispatch_all_dwnl_pending() │
 *                                      ├── cb_A(progress%, status)
 *                                      ├── cb_B(progress%, status)
 *                                      └── cb_C(progress%, status)
 *
 * KEY DIFFERENCE FROM CheckForUpdate:
 *   CheckForUpdate registry: slot goes PENDING → DISPATCHED → IDLE  (fires ONCE)
 *   Download registry:       slot stays ACTIVE until DWNL_COMPLETED or DWNL_ERROR
 *                            (fires MULTIPLE TIMES — once per progress signal)
 *
 * ======================================================================== */

#define DBUS_METHOD_DOWNLOAD        "DownloadFirmware"
#define DBUS_SIGNAL_DWNL_PROGRESS   "DownloadProgress"

/**
 * @brief Lifecycle state of one download callback registry slot
 *
 *   IDLE ──(register)──► ACTIVE ──(COMPLETED/ERROR signal)──► IDLE
 *                            │
 *                            │  (fires callback on EVERY DownloadProgress signal
 *                            │   while in ACTIVE state)
 *                            │
 *                            └──(timeout)──► TIMED_OUT ──► IDLE
 */
typedef enum {
    DWNL_CB_STATE_IDLE      = 0,  /**< Slot free and reusable              */
    DWNL_CB_STATE_ACTIVE    = 1,  /**< Receiving progress signals          */
    DWNL_CB_STATE_TIMED_OUT = 2   /**< Timed out waiting for completion    */
} DwnlCallbackState;

/**
 * @brief Parsed payload from DownloadProgress D-Bus signal
 *
 * Daemon emits this repeatedly as download progresses.
 * GVariant signature: (tsuss)
 *   t  handlerId         (uint64 - handler ID)
 *   s  firmwareName      (string - firmware filename)
 *   u  progress          (uint32 - 0-100 percent)
 *   s  status            (string - "INPROGRESS", "COMPLETED", "NOTSTARTED")
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
 * @brief One slot in the download callback registry
 *
 * Keyed by handle_key (strdup of FirmwareInterfaceHandle).
 * Stays ACTIVE across multiple DownloadProgress signal deliveries.
 * Reset to IDLE only when DWNL_COMPLETED or DWNL_ERROR is received.
 */
typedef struct {
    DwnlCallbackState  state;            /**< IDLE or ACTIVE                */
    char              *handle_key;       /**< strdup of app's handle        */
    DownloadCallback   callback;         /**< App's progress callback       */
    time_t             registered_time;  /**< For timeout detection         */
} DwnlCallbackEntry;

/**
 * @brief Global registry for all active download callbacks
 *
 * Separate from the CheckForUpdate registry — different lifecycle.
 * Protected by its own mutex.
 */
typedef struct {
    DwnlCallbackEntry  entries[MAX_PENDING_CALLBACKS];
    pthread_mutex_t    mutex;
    bool               initialized;
} DwnlCallbackRegistry;

/* ---- Download internal function declarations ---- */

/* ========================================================================
 * DOWNLOAD CALLBACK REGISTRATION
 * ======================================================================== */

/**
 * @brief Register a download callback keyed by handle
 *
 * @param handle    App's FirmwareInterfaceHandle (strdup'd internally)
 * @param callback  App's DownloadCallback
 * @return true on success, false if registry full
 */
bool internal_dwnl_register_callback(FirmwareInterfaceHandle handle,
                                      DownloadCallback callback);

/**
 * @brief Parse GVariant DownloadProgress signal payload
 *
 * Expected GVariant signature: (ii)
 *   i  progress_percent
 *   i  status_code
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
 * UPDATE FIRMWARE — INTERNAL TYPES AND DECLARATIONS
 * ========================================================================
 *
 * ARCHITECTURE:
 *
 *  App A ──updateFirmware(hdl_A, req_A, cb_A)──┐
 *  App B ──updateFirmware(hdl_B, req_B, cb_B)──┼──► UpdateRegistry (keyed by handle)
 *  App C ──updateFirmware(hdl_C, req_C, cb_C)──┘          │
 *                                                          │  same background thread
 *                                                          │  subscribed to UpdateProgress
 *                                                          ▼
 *                Daemon emits UpdateProgress(progress%, status) REPEATEDLY
 *                                                          │
 *                                  on_update_progress_signal()
 *                                                          │
 *                                dispatch_all_update_active() │
 *                                  ├── cb_A(progress%, status)
 *                                  ├── cb_B(progress%, status)
 *                                  └── cb_C(progress%, status)
 *
 * IDENTICAL LIFECYCLE TO DOWNLOAD:
 *   Slot stays ACTIVE across multiple signals.
 *   Reset to IDLE only on UPDATE_COMPLETED or UPDATE_ERROR.
 * ======================================================================== */

#define DBUS_METHOD_UPDATE          "UpdateFirmware"
#define DBUS_SIGNAL_UPDATE_PROGRESS "UpdateProgress"

/**
 * @brief Lifecycle state of one update callback registry slot
 *
 *   IDLE ──(register)──► ACTIVE ──(COMPLETED/ERROR signal)──► IDLE
 *                            │
 *                            │  (fires callback on EVERY UpdateProgress signal)
 *                            └──(timeout)──► TIMED_OUT ──► IDLE
 */
typedef enum {
    UPDATE_CB_STATE_IDLE      = 0,  /**< Slot free and reusable              */
    UPDATE_CB_STATE_ACTIVE    = 1,  /**< Receiving update progress signals   */
    UPDATE_CB_STATE_TIMED_OUT = 2   /**< Timed out waiting for completion    */
} UpdateCbState;

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
 * @brief One slot in the update callback registry
 *
 * Keyed by handle_key. Stays ACTIVE until UPDATE_COMPLETED or UPDATE_ERROR.
 */
typedef struct {
    UpdateCbState   state;            /**< IDLE or ACTIVE                    */
    char           *handle_key;       /**< strdup of app's handle            */
    UpdateCallback  callback;         /**< App's progress callback           */
    time_t          registered_time;  /**< For timeout detection             */
} UpdateCbEntry;

/**
 * @brief Global registry for all active update callbacks
 */
typedef struct {
    UpdateCbEntry    entries[MAX_PENDING_CALLBACKS];
    pthread_mutex_t  mutex;
    bool             initialized;
} UpdateCbRegistry;

/* ---- Update internal function declarations ---- */

/* ========================================================================
 * UPDATE CALLBACK REGISTRATION
 * ======================================================================== */

/**
 * @brief Register an update callback keyed by handle
 *
 * @param handle    App's FirmwareInterfaceHandle (strdup'd internally)
 * @param callback  App's UpdateCallback
 * @return true on success, false if registry full
 */
bool internal_update_register_callback(FirmwareInterfaceHandle handle,
                                        UpdateCallback callback);

/**
 * @brief Parse GVariant UpdateProgress signal payload
 *
 * Expected GVariant signature: (ii)
 *   i  progress_percent
 *   i  status_code
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
