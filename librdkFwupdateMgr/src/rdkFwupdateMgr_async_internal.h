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
 * ARCHITECTURE OVERVIEW:
 * ======================
 *
 *  App A ──checkForUpdate(hdl_A, cb_A)──┐
 *  App B ──checkForUpdate(hdl_B, cb_B)──┼──► Registry (keyed by handle)
 *  App C ──checkForUpdate(hdl_C, cb_C)──┘          │
 *                                                   │  background thread
 *                                                   │  watches D-Bus
 *                                                   ▼
 *                        Daemon emits CheckForUpdateComplete signal (ONCE)
 *                                                   │
 *                                    on_check_complete_signal()
 *                                                   │
 *                            dispatch_all_pending() │
 *                             ├── cb_A(hdl_A, &event_data)
 *                             ├── cb_B(hdl_B, &event_data)
 *                             └── cb_C(hdl_C, &event_data)
 *
 * REGISTRY KEY:
 * =============
 *   Each entry keyed by FirmwareInterfaceHandle (string from registerProcess).
 *   One handle → one pending callback at a time.
 *
 * THREAD SAFETY:
 * ==============
 *   Registry protected by pthread_mutex.
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

#define MAX_PENDING_CALLBACKS    64
#define CALLBACK_TIMEOUT_SECONDS 60

#define DBUS_SERVICE_NAME        "org.rdkfwupdater.Interface"
#define DBUS_OBJECT_PATH         "/org/rdkfwupdater/Service"
#define DBUS_INTERFACE_NAME      "org.rdkfwupdater.Interface"
#define DBUS_METHOD_CHECK        "CheckForUpdate"
#define DBUS_SIGNAL_COMPLETE     "CheckForUpdateComplete"
#define DBUS_TIMEOUT_MS          5000

/* ========================================================================
 * CALLBACK ENTRY STATE
 * ======================================================================== */

/**
 * @brief Lifecycle of one registry slot
 *
 *   IDLE ──(register)──► PENDING ──(signal)──► DISPATCHED ──► IDLE
 *                            └──(timeout)──► TIMED_OUT ──► IDLE
 */
typedef enum {
    CB_STATE_IDLE       = 0,
    CB_STATE_PENDING    = 1,
    CB_STATE_DISPATCHED = 2,
    CB_STATE_TIMED_OUT  = 3
} CallbackEntryState;

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
 * CALLBACK REGISTRY ENTRY
 * ======================================================================== */

/**
 * @brief One slot in the callback registry
 *
 * Keyed by handle_key (strdup of app's FirmwareInterfaceHandle).
 * No user_data — aligned to 2-param callback signature.
 *
 * MEMORY:
 *   handle_key is strdup'd on registration, freed on slot reset to IDLE.
 */
typedef struct {
    CallbackEntryState   state;            /**< Current lifecycle state       */
    char                *handle_key;       /**< strdup of app's handle        */
    UpdateEventCallback  callback;         /**< App's 2-param callback        */
    time_t               registered_time;  /**< For timeout detection         */
} CallbackEntry;

/* ========================================================================
 * CALLBACK REGISTRY
 * ======================================================================== */

/**
 * @brief Global registry — one instance per library load
 */
typedef struct {
    CallbackEntry    entries[MAX_PENDING_CALLBACKS];
    pthread_mutex_t  mutex;
    bool             initialized;
} CallbackRegistry;

/* ========================================================================
 * BACKGROUND THREAD
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
 * @brief Initialize registry and start background thread
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
 * @brief Register a pending callback keyed by handle
 *
 * No user_data — matches the 2-param UpdateEventCallback signature.
 *
 * @param handle    App's FirmwareInterfaceHandle (will be strdup'd)
 * @param callback  App's UpdateEventCallback (2-param)
 * @return true on success, false if registry is full
 */
bool internal_register_callback(FirmwareInterfaceHandle handle,
                                 UpdateEventCallback callback);

/**
 * @brief Parse GVariant signal into InternalSignalData
 *
 * Expected GVariant signature: (iissss)
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
 * GVariant signature: (ii)
 *   i  progress_percent   (0–100)
 *   i  status_code        (maps to DownloadStatus enum)
 */
typedef struct {
    int32_t progress_percent;   /**< 0–100                                 */
    int32_t status_code;        /**< Maps to DownloadStatus enum           */
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
 * GVariant signature: (ii)
 *   i  progress_percent   (0–100)
 *   i  status_code        (maps to UpdateStatus enum)
 */
typedef struct {
    int32_t progress_percent;   /**< 0–100                                   */
    int32_t status_code;        /**< Maps to UpdateStatus enum               */
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

/**
 * @brief Initialize update registry and subscribe to UpdateProgress signal
 * Called from internal_system_init() after background thread is ready.
 * @return 0 on success, -1 on error
 */
int internal_update_system_init(void);

/**
 * @brief Cleanup update registry — called from internal_system_deinit()
 */
void internal_update_system_deinit(void);

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
