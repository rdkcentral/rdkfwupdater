/*
 * Copyright 2026 Comcast Cable Communications Management, LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file rdkFwupdateMgr_async_internal.h
 * @brief Internal data structures and definitions for async CheckForUpdate API
 *
 * This header defines the internal data structures used for implementing
 * the asynchronous CheckForUpdate API. These structures are NOT part of
 * the public API and should only be used within the library implementation.
 *
 * ARCHITECTURE:
 * =============
 * - Multi-callback registry supporting up to MAX_CALLBACKS concurrent operations
 * - State machine for each callback (IDLE, WAITING, COMPLETED, etc.)
 * - Thread-safe access using pthread mutex
 * - Reference counting for safe cleanup
 * - Background thread with GLib event loop for signal processing
 *
 * THREAD SAFETY:
 * ==============
 * - All registry operations protected by g_async_registry_mutex
 * - Atomic operations used for reference counting
 * - Signal handler runs in background thread context
 * - Callbacks invoked with registry lock released
 *
 * MEMORY MANAGEMENT:
 * ==================
 * - CallbackContext structures live in fixed-size array (no malloc/free)
 * - RdkUpdateInfo strings are malloc'd and must be freed after callback
 * - Reference counting prevents use-after-free during concurrent access
 */

#ifndef RDKFWUPDATEMGR_ASYNC_INTERNAL_H
#define RDKFWUPDATEMGR_ASYNC_INTERNAL_H

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

/** Maximum number of concurrent async callbacks supported */
#define MAX_ASYNC_CALLBACKS 64

/** Timeout for async operations in seconds */
#define ASYNC_CALLBACK_TIMEOUT_SECONDS 60

/* ========================================================================
 * STATE MACHINE
 * ======================================================================== */

/**
 * @brief State of an async callback registration
 *
 * Each callback goes through a lifecycle:
 * IDLE → WAITING → (COMPLETED | CANCELLED | TIMEOUT | ERROR) → IDLE
 */
typedef enum {
    CALLBACK_STATE_IDLE = 0,        /**< Slot not in use (initial state) */
    CALLBACK_STATE_WAITING,         /**< Waiting for CheckForUpdateComplete signal */
    CALLBACK_STATE_COMPLETED,       /**< Callback invoked successfully */
    CALLBACK_STATE_CANCELLED,       /**< Cancelled by user via cancel API */
    CALLBACK_STATE_TIMEOUT,         /**< Operation timed out */
    CALLBACK_STATE_ERROR            /**< Error occurred during processing */
} CallbackState;

/* ========================================================================
 * DATA STRUCTURES
 * ======================================================================== */

/**
 * @brief Update information delivered to user callback
 *
 * This structure contains the parsed data from the CheckForUpdateComplete
 * D-Bus signal. All string fields are dynamically allocated and must be
 * freed after the callback completes.
 *
 * MEMORY OWNERSHIP:
 * - Library allocates all strings (malloc/strdup)
 * - Library frees all strings after callback returns
 * - User MUST NOT free any fields
 * - User MUST copy data if needed after callback returns
 */
typedef struct {
    /* API result: 0 = success, non-zero = error */
    int32_t result;
    
    /* Status code (see CheckForUpdateStatus enum in public header) */
    int32_t status_code;
    
    /* Current firmware version (malloc'd, may be NULL) */
    char *current_version;
    
    /* Available firmware version (malloc'd, may be NULL or empty) */
    char *available_version;
    
    /* Raw update details string from daemon (malloc'd, may be NULL) */
    char *update_details;
    
    /* Human-readable status message (malloc'd, may be NULL) */
    char *status_message;
    
    /* Convenience flag: true if update is available */
    bool update_available;
} RdkUpdateInfo;

/**
 * @brief User callback function type for async update checks
 *
 * This callback is invoked when the CheckForUpdateComplete signal is received.
 * The callback runs in the context of the background signal handler thread.
 *
 * IMPORTANT:
 * - The RdkUpdateInfo structure is ONLY valid during the callback
 * - Do NOT store pointers to any fields - copy data if needed
 * - Keep callback execution time short (no blocking operations)
 * - Callback may be invoked from a different thread than the caller
 *
 * @param info Update information (valid only during callback)
 * @param user_data User data provided during registration
 */
typedef void (*RdkUpdateCallback)(const RdkUpdateInfo *info, void *user_data);

/**
 * @brief Internal context for a registered async callback
 *
 * Each entry in the callback registry contains one of these structures.
 * The registry is a fixed-size array of MAX_ASYNC_CALLBACKS entries.
 *
 * LIFECYCLE:
 * - Allocated statically in global registry array
 * - Reused when state transitions back to IDLE
 * - Never malloc'd or free'd individually
 *
 * THREAD SAFETY:
 * - All access must be protected by g_async_registry_mutex
 * - ref_count uses atomic operations
 * - state may be read/written with mutex held
 */
typedef struct {
    /* Unique callback ID (0 = invalid/unused, >0 = valid) */
    uint32_t id;
    
    /* Current state in lifecycle */
    CallbackState state;
    
    /* User's callback function */
    RdkUpdateCallback callback;
    
    /* User's opaque data pointer */
    void *user_data;
    
    /* Timestamp when callback was registered (for timeout detection) */
    time_t registered_time;
    
    /* Reference count for safe concurrent access (atomic operations) */
    volatile int ref_count;
} AsyncCallbackContext;

/**
 * @brief Global registry for all async callbacks
 *
 * This structure holds all active and idle callback contexts.
 * There is one global instance of this structure in the library.
 *
 * SYNCHRONIZATION:
 * - Protected by mutex for all operations
 * - Lock must be held when accessing any field
 * - Lock released before invoking user callbacks
 */
typedef struct {
    /* Array of callback contexts (fixed size) */
    AsyncCallbackContext contexts[MAX_ASYNC_CALLBACKS];
    
    /* Mutex protecting the entire registry */
    pthread_mutex_t mutex;
    
    /* Next ID to assign (monotonically increasing, wraps around) */
    uint32_t next_id;
    
    /* Initialization flag */
    bool initialized;
} AsyncCallbackRegistry;

/**
 * @brief Background thread state
 *
 * Manages the GLib event loop thread used for processing D-Bus signals.
 *
 * LIFECYCLE:
 * - Started during library initialization
 * - Runs until library deinitialization
 * - Signal subscription happens in this thread's context
 */
typedef struct {
    /* Thread handle */
    pthread_t thread;
    
    /* GLib main loop (runs in background thread) */
    GMainLoop *main_loop;
    
    /* GLib main context (isolated from default context) */
    GMainContext *context;
    
    /* D-Bus connection for signal subscription */
    GDBusConnection *connection;
    
    /* Signal subscription ID (for unsubscribing) */
    guint signal_subscription_id;
    
    /* Running flag (protected by mutex) */
    bool running;
    
    /* Mutex protecting state changes */
    pthread_mutex_t mutex;
} AsyncBackgroundThread;

/* ========================================================================
 * HELPER MACROS
 * ======================================================================== */

/**
 * @brief Check if a callback context is in use
 */
#define CALLBACK_IS_ACTIVE(ctx) ((ctx)->state != CALLBACK_STATE_IDLE && (ctx)->id != 0)

/**
 * @brief Check if a callback is waiting for signal
 */
#define CALLBACK_IS_WAITING(ctx) ((ctx)->state == CALLBACK_STATE_WAITING)

/* ========================================================================
 * FUNCTION DECLARATIONS
 * ======================================================================== */

/**
 * @brief Initialize the async callback system
 *
 * This function must be called during library initialization.
 * It initializes the registry, starts the background thread, and
 * subscribes to D-Bus signals.
 *
 * @return 0 on success, -1 on error
 */
int async_system_init(void);

/**
 * @brief Shutdown the async callback system
 *
 * This function must be called during library deinitialization.
 * It cancels all pending callbacks, stops the background thread,
 * and cleans up all resources.
 */
void async_system_deinit(void);

/**
 * @brief Register a new async callback
 *
 * Allocates a callback context, assigns a unique ID, and sets
 * the state to WAITING.
 *
 * @param callback User callback function (required, must not be NULL)
 * @param user_data User data pointer (optional, may be NULL)
 * @return Callback ID (>0) on success, 0 on error (registry full)
 */
uint32_t async_register_callback(RdkUpdateCallback callback, void *user_data);

/**
 * @brief Cancel a pending async callback
 *
 * Changes the callback state to CANCELLED. The callback will not
 * be invoked when the signal arrives.
 *
 * @param callback_id ID returned from async_register_callback()
 * @return 0 on success, -1 on error (not found or already completed)
 */
int async_cancel_callback(uint32_t callback_id);

/**
 * @brief Increment reference count (thread-safe)
 *
 * Must be called before accessing a callback context outside the
 * registry mutex.
 *
 * @param ctx Callback context (must not be NULL)
 */
void async_context_ref(AsyncCallbackContext *ctx);

/**
 * @brief Decrement reference count and cleanup if zero (thread-safe)
 *
 * Must be called after done accessing a callback context.
 * If ref_count reaches zero, the context is reset to IDLE.
 *
 * @param ctx Callback context (must not be NULL)
 */
void async_context_unref(AsyncCallbackContext *ctx);

/**
 * @brief Parse CheckForUpdateComplete signal data
 *
 * Extracts data from the GVariant and populates RdkUpdateInfo.
 * All strings are duplicated (malloc'd) and must be freed later.
 *
 * @param parameters GVariant from D-Bus signal (signature: tiissss)
 * @param info Output structure (must not be NULL)
 * @return true on success, false on parse error
 */
bool async_parse_signal_data(GVariant *parameters, RdkUpdateInfo *info);

/**
 * @brief Free all memory in RdkUpdateInfo
 *
 * Frees all malloc'd strings and resets structure to zero.
 *
 * @param info Structure to cleanup (must not be NULL)
 */
void async_cleanup_update_info(RdkUpdateInfo *info);

#ifdef __cplusplus
}
#endif

#endif /* RDKFWUPDATEMGR_ASYNC_INTERNAL_H */
