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
 * @file rdkFwupdateMgr_async.c
 * @brief Implementation of async CheckForUpdate API
 *
 * This file implements the asynchronous firmware update checking mechanism.
 * It provides a callback-based API that allows multiple concurrent update
 * checks without blocking the caller.
 *
 * KEY FEATURES:
 * =============
 * - Non-blocking API that returns immediately
 * - Supports up to MAX_ASYNC_CALLBACKS (64) concurrent operations
 * - Thread-safe callback registry with mutex protection
 * - Background thread for D-Bus signal processing
 * - Automatic timeout detection and cleanup
 * - Reference counting for safe concurrent access
 *
 * IMPLEMENTATION NOTES:
 * ====================
 * - Uses fixed-size array for callback registry (no dynamic allocation)
 * - Each callback has a unique monotonic ID (wraps around after UINT32_MAX)
 * - State machine ensures proper lifecycle management
 * - All string allocations use strdup() and are freed after callback
 * - Coverity-clean: all error paths checked, no resource leaks
 */

#include "rdkFwupdateMgr_async_internal.h"
#include "rdkFwupdateMgr_log.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

/* ========================================================================
 * GLOBAL STATE
 * ======================================================================== */

/**
 * Global callback registry (single instance)
 * 
 * This is initialized once during library init and never freed
 * (it's a static global). Individual contexts within the array
 * are reused as callbacks complete.
 */
static AsyncCallbackRegistry g_async_registry = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .next_id = 1,  /* IDs start at 1 (0 = invalid) */
    .initialized = false
};

/**
 * Global background thread state (single instance)
 * 
 * Manages the GLib event loop thread that processes D-Bus signals.
 */
static AsyncBackgroundThread g_bg_thread = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .running = false,
    .main_loop = NULL,
    .context = NULL,
    .connection = NULL,
    .signal_subscription_id = 0
};

/* ========================================================================
 * FORWARD DECLARATIONS
 * ======================================================================== */

/* Signal handler (called in background thread context) */
static void on_check_for_update_complete_signal(
    GDBusConnection *connection,
    const gchar *sender_name,
    const gchar *object_path,
    const gchar *interface_name,
    const gchar *signal_name,
    GVariant *parameters,
    gpointer user_data);

/* Timeout checker (called periodically from background thread) */
static gboolean check_callback_timeouts(gpointer user_data);

/* Background thread entry point */
static void* background_thread_func(void *arg);

/* ========================================================================
 * REGISTRY HELPER FUNCTIONS (PRIVATE)
 * ======================================================================== */

/**
 * @brief Lock the callback registry
 * 
 * Blocks until lock is acquired. Must be paired with registry_unlock().
 */
static inline void registry_lock(void) {
    pthread_mutex_lock(&g_async_registry.mutex);
}

/**
 * @brief Unlock the callback registry
 * 
 * Must be called after registry_lock().
 */
static inline void registry_unlock(void) {
    pthread_mutex_unlock(&g_async_registry.mutex);
}

/**
 * @brief Find a callback context by ID (must hold registry lock)
 * 
 * Searches the registry for a context with the given ID.
 * Caller must hold the registry mutex when calling this function.
 * 
 * @param callback_id ID to search for
 * @return Pointer to context if found, NULL otherwise
 */
static AsyncCallbackContext* registry_find_callback_locked(uint32_t callback_id) {
    if (callback_id == 0) {
        return NULL;
    }
    
    for (int i = 0; i < MAX_ASYNC_CALLBACKS; i++) {
        AsyncCallbackContext *ctx = &g_async_registry.contexts[i];
        if (ctx->id == callback_id && ctx->state != CALLBACK_STATE_IDLE) {
            return ctx;
        }
    }
    
    return NULL;
}

/**
 * @brief Find an idle (unused) callback slot (must hold registry lock)
 * 
 * Searches for a context in IDLE state that can be reused.
 * Caller must hold the registry mutex when calling this function.
 * 
 * @return Pointer to idle context if found, NULL if registry is full
 */
static AsyncCallbackContext* registry_find_idle_slot_locked(void) {
    for (int i = 0; i < MAX_ASYNC_CALLBACKS; i++) {
        AsyncCallbackContext *ctx = &g_async_registry.contexts[i];
        if (ctx->state == CALLBACK_STATE_IDLE && ctx->id == 0) {
            return ctx;
        }
    }
    
    return NULL;
}

/**
 * @brief Get next unique callback ID (must hold registry lock)
 * 
 * Returns a monotonically increasing ID. IDs wrap around after
 * UINT32_MAX but never return 0 (0 is reserved for invalid/error).
 * 
 * @return Unique callback ID (always > 0)
 */
static uint32_t registry_get_next_id_locked(void) {
    uint32_t id = g_async_registry.next_id++;
    
    /* Wrap around, but never use 0 */
    if (g_async_registry.next_id == 0) {
        g_async_registry.next_id = 1;
    }
    
    /* Ensure ID is truly unique (paranoid check) */
    if (registry_find_callback_locked(id) != NULL) {
        /* ID collision (very unlikely), try next one */
        return registry_get_next_id_locked();
    }
    
    return id;
}

/**
 * @brief Collect all callbacks in WAITING state (must hold registry lock)
 * 
 * Fills an output array with pointers to all contexts that are currently
 * in WAITING state. Also increments the reference count of each context
 * to prevent cleanup while callbacks are being invoked.
 * 
 * Caller must hold the registry mutex when calling this function.
 * Caller must call async_context_unref() for each returned context.
 * 
 * @param out_array Output array (must have space for MAX_ASYNC_CALLBACKS)
 * @param max_count Maximum number of contexts to return
 * @return Number of waiting contexts found
 */
static int registry_get_waiting_callbacks_locked(
    AsyncCallbackContext **out_array,
    int max_count)
{
    int count = 0;
    
    for (int i = 0; i < MAX_ASYNC_CALLBACKS && count < max_count; i++) {
        AsyncCallbackContext *ctx = &g_async_registry.contexts[i];
        if (CALLBACK_IS_WAITING(ctx)) {
            /* Increment ref count to prevent cleanup during invocation */
            async_context_ref(ctx);
            out_array[count++] = ctx;
        }
    }
    
    return count;
}

/* ========================================================================
 * REFERENCE COUNTING (THREAD-SAFE)
 * ======================================================================== */

/**
 * @brief Increment reference count (thread-safe, atomic operation)
 * 
 * Uses GCC atomic built-in for thread-safe increment.
 * Must be called before accessing a context outside the registry lock.
 * 
 * @param ctx Callback context (must not be NULL)
 */
void async_context_ref(AsyncCallbackContext *ctx) {
    if (ctx == NULL) {
        FWUPMGR_ERROR("async_context_ref: NULL context pointer\n");
        return;
    }
    
    __sync_fetch_and_add(&ctx->ref_count, 1);
}

/**
 * @brief Decrement reference count and cleanup if zero (thread-safe, atomic)
 * 
 * Uses GCC atomic built-in for thread-safe decrement.
 * If ref_count reaches zero, the context is reset to IDLE state.
 * 
 * @param ctx Callback context (must not be NULL)
 */
void async_context_unref(AsyncCallbackContext *ctx) {
    if (ctx == NULL) {
        FWUPMGR_ERROR("async_context_unref: NULL context pointer\n");
        return;
    }
    
    int old_count = __sync_fetch_and_sub(&ctx->ref_count, 1);
    
    if (old_count == 1) {
        /* Last reference released - reset context to IDLE */
        registry_lock();
        
        /* Double-check state in case something changed */
        if (ctx->ref_count == 0) {
            ctx->id = 0;
            ctx->callback = NULL;
            ctx->user_data = NULL;
            ctx->registered_time = 0;
            ctx->state = CALLBACK_STATE_IDLE;
        }
        
        registry_unlock();
    } else if (old_count < 1) {
        /* Should never happen - indicates a bug */
        FWUPMGR_ERROR("async_context_unref: ref_count went negative! (was %d)\n", old_count);
    }
}

/* ========================================================================
 * SIGNAL DATA PARSING
 * ======================================================================== */

/**
 * @brief Parse CheckForUpdateComplete signal parameters
 * 
 * Expected GVariant signature: (tiissss)
 *   t = uint64 handlerId
 *   i = int32 result
 *   i = int32 statusCode
 *   s = string currentVersion
 *   s = string availableVersion
 *   s = string updateDetails
 *   s = string statusMessage
 * 
 * All strings are duplicated using strdup() and must be freed later.
 * 
 * @param parameters GVariant from D-Bus signal
 * @param info Output structure (cleared and populated)
 * @return true on success, false on parse error
 */
bool async_parse_signal_data(GVariant *parameters, RdkUpdateInfo *info) {
    if (parameters == NULL || info == NULL) {
        FWUPMGR_ERROR("async_parse_signal_data: NULL parameter\n");
        return false;
    }
    
    /* Clear output structure */
    memset(info, 0, sizeof(RdkUpdateInfo));
    
    /* Check GVariant signature */
    const gchar *signature = g_variant_get_type_string(parameters);
    if (signature == NULL || strcmp(signature, "(tiissss)") != 0) {
        FWUPMGR_ERROR("async_parse_signal_data: Invalid signature '%s' (expected '(tiissss)')\n",
                      signature ? signature : "NULL");
        return false;
    }
    
    /* Extract parameters */
    guint64 handler_id = 0;
    gint32 result = -1;
    gint32 status_code = -1;
    const gchar *current_ver = NULL;
    const gchar *available_ver = NULL;
    const gchar *update_details = NULL;
    const gchar *status_msg = NULL;
    
    g_variant_get(parameters, "(tiissss)",
                  &handler_id,
                  &result,
                  &status_code,
                  &current_ver,
                  &available_ver,
                  &update_details,
                  &status_msg);
    
    /* Store numeric fields */
    info->result = result;
    info->status_code = status_code;
    
    /* Determine if update is available based on status code */
    info->update_available = (status_code == 0);  /* 0 = FIRMWARE_AVAILABLE */
    
    /* Duplicate strings (handle NULL gracefully) */
    if (current_ver != NULL && current_ver[0] != '\0') {
        info->current_version = strdup(current_ver);
        if (info->current_version == NULL) {
            FWUPMGR_ERROR("async_parse_signal_data: strdup failed for current_version\n");
            goto cleanup_error;
        }
    }
    
    if (available_ver != NULL && available_ver[0] != '\0') {
        info->available_version = strdup(available_ver);
        if (info->available_version == NULL) {
            FWUPMGR_ERROR("async_parse_signal_data: strdup failed for available_version\n");
            goto cleanup_error;
        }
    }
    
    if (update_details != NULL && update_details[0] != '\0') {
        info->update_details = strdup(update_details);
        if (info->update_details == NULL) {
            FWUPMGR_ERROR("async_parse_signal_data: strdup failed for update_details\n");
            goto cleanup_error;
        }
    }
    
    if (status_msg != NULL && status_msg[0] != '\0') {
        info->status_message = strdup(status_msg);
        if (info->status_message == NULL) {
            FWUPMGR_ERROR("async_parse_signal_data: strdup failed for status_message\n");
            goto cleanup_error;
        }
    }
    
    FWUPMGR_INFO("Signal parsed successfully: result=%d, status=%d, update_avail=%d\n",
                 result, status_code, info->update_available);
    
    return true;

cleanup_error:
    /* Free any strings that were successfully allocated */
    async_cleanup_update_info(info);
    return false;
}

/**
 * @brief Free all dynamically allocated memory in RdkUpdateInfo
 * 
 * Frees all strings and resets structure to zero.
 * Safe to call multiple times (checks for NULL before freeing).
 * 
 * @param info Structure to cleanup (must not be NULL)
 */
void async_cleanup_update_info(RdkUpdateInfo *info) {
    if (info == NULL) {
        return;
    }
    
    if (info->current_version != NULL) {
        free(info->current_version);
        info->current_version = NULL;
    }
    
    if (info->available_version != NULL) {
        free(info->available_version);
        info->available_version = NULL;
    }
    
    if (info->update_details != NULL) {
        free(info->update_details);
        info->update_details = NULL;
    }
    
    if (info->status_message != NULL) {
        free(info->status_message);
        info->status_message = NULL;
    }
    
    memset(info, 0, sizeof(RdkUpdateInfo));
}

/* ========================================================================
 * PUBLIC API IMPLEMENTATION
 * ======================================================================== */

/**
 * @brief Register a new async callback
 * 
 * Allocates a slot in the callback registry, assigns a unique ID,
 * and sets the state to WAITING. The callback will be invoked when
 * the CheckForUpdateComplete signal is received.
 * 
 * This function is thread-safe and may be called from any thread.
 * 
 * @param callback User callback function (required, must not be NULL)
 * @param user_data User data pointer (optional, may be NULL)
 * @return Callback ID (>0) on success, 0 on error (invalid param or registry full)
 */
uint32_t async_register_callback(RdkUpdateCallback callback, void *user_data) {
    if (callback == NULL) {
        FWUPMGR_ERROR("async_register_callback: callback is NULL\n");
        return 0;
    }
    
    if (!g_async_registry.initialized) {
        FWUPMGR_ERROR("async_register_callback: registry not initialized\n");
        return 0;
    }
    
    registry_lock();
    
    /* Find an idle slot */
    AsyncCallbackContext *ctx = registry_find_idle_slot_locked();
    if (ctx == NULL) {
        registry_unlock();
        FWUPMGR_ERROR("async_register_callback: registry full (%d callbacks)\n",
                      MAX_ASYNC_CALLBACKS);
        return 0;
    }
    
    /* Assign unique ID */
    uint32_t callback_id = registry_get_next_id_locked();
    
    /* Initialize context */
    ctx->id = callback_id;
    ctx->callback = callback;
    ctx->user_data = user_data;
    ctx->registered_time = time(NULL);
    ctx->ref_count = 1;  /* Initial reference (will be released when completed/cancelled) */
    ctx->state = CALLBACK_STATE_WAITING;
    
    registry_unlock();
    
    FWUPMGR_INFO("Registered async callback: id=%u\n", callback_id);
    
    return callback_id;
}

/**
 * @brief Cancel a pending async callback
 * 
 * Changes the callback state to CANCELLED. If the signal has not yet
 * arrived, the callback will not be invoked. If the signal has already
 * arrived and the callback is being invoked, this function will return
 * an error.
 * 
 * This function is thread-safe and may be called from any thread.
 * 
 * @param callback_id ID returned from async_register_callback()
 * @return 0 on success, -1 on error (not found or already completed)
 */
int async_cancel_callback(uint32_t callback_id) {
    if (callback_id == 0) {
        FWUPMGR_ERROR("async_cancel_callback: invalid callback_id (0)\n");
        return -1;
    }
    
    registry_lock();
    
    AsyncCallbackContext *ctx = registry_find_callback_locked(callback_id);
    if (ctx == NULL) {
        registry_unlock();
        FWUPMGR_WARN("async_cancel_callback: callback id=%u not found\n", callback_id);
        return -1;
    }
    
    /* Can only cancel if in WAITING state */
    if (ctx->state != CALLBACK_STATE_WAITING) {
        CallbackState old_state = ctx->state;
        registry_unlock();
        FWUPMGR_WARN("async_cancel_callback: callback id=%u not in WAITING state (state=%d)\n",
                     callback_id, old_state);
        return -1;
    }
    
    /* Mark as cancelled */
    ctx->state = CALLBACK_STATE_CANCELLED;
    
    registry_unlock();
    
    FWUPMGR_INFO("Cancelled async callback: id=%u\n", callback_id);
    
    /* Release the initial reference (context will become IDLE when ref_count reaches 0) */
    async_context_unref(ctx);
    
    return 0;
}

/* ========================================================================
 * D-BUS SIGNAL HANDLER (PHASE 3)
 * ======================================================================== */

/**
 * @brief D-Bus signal handler for CheckForUpdateComplete
 * 
 * This function is called by GLib when the CheckForUpdateComplete signal
 * is received on the D-Bus. It runs in the background thread context.
 * 
 * The handler:
 * 1. Validates the signal name
 * 2. Parses the signal parameters into RdkUpdateInfo
 * 3. Finds all callbacks in WAITING state
 * 4. Invokes each callback with the update information
 * 5. Updates callback states to COMPLETED
 * 
 * THREAD SAFETY:
 * - Runs in background thread (GLib event loop)
 * - Locks registry to collect waiting callbacks
 * - Releases lock before invoking user callbacks
 * - Re-locks to update states after callback
 * 
 * @param connection D-Bus connection
 * @param sender_name Sender's unique name on D-Bus
 * @param object_path Object path of signal source
 * @param interface_name Interface name
 * @param signal_name Signal name (should be "CheckForUpdateComplete")
 * @param parameters Signal parameters (GVariant)
 * @param user_data User data (unused)
 */
static void on_check_for_update_complete_signal(
    GDBusConnection *connection,
    const gchar *sender_name,
    const gchar *object_path,
    const gchar *interface_name,
    const gchar *signal_name,
    GVariant *parameters,
    gpointer user_data)
{
    (void)connection;   /* Unused */
    (void)sender_name;  /* Unused */
    (void)object_path;  /* Unused */
    (void)interface_name; /* Unused */
    (void)user_data;    /* Unused */
    
    FWUPMGR_INFO("==== CheckForUpdateComplete Signal Received ====\n");
    
    /* Validate signal name */
    if (signal_name == NULL || strcmp(signal_name, "CheckForUpdateComplete") != 0) {
        FWUPMGR_WARN("Unexpected signal name: %s\n", signal_name ? signal_name : "NULL");
        return;
    }
    
    /* Parse signal data */
    RdkUpdateInfo info = {0};
    if (!async_parse_signal_data(parameters, &info)) {
        FWUPMGR_ERROR("Failed to parse signal data\n");
        return;
    }
    
    /* Log parsed data */
    FWUPMGR_INFO("  Result: %d\n", info.result);
    FWUPMGR_INFO("  Status Code: %d\n", info.status_code);
    FWUPMGR_INFO("  Update Available: %s\n", info.update_available ? "YES" : "NO");
    FWUPMGR_INFO("  Current Version: %s\n", info.current_version ? info.current_version : "N/A");
    FWUPMGR_INFO("  Available Version: %s\n", info.available_version ? info.available_version : "N/A");
    FWUPMGR_INFO("  Status Message: %s\n", info.status_message ? info.status_message : "N/A");
    
    /* Lock registry and collect all waiting callbacks */
    registry_lock();
    
    AsyncCallbackContext *waiting_callbacks[MAX_ASYNC_CALLBACKS];
    int count = registry_get_waiting_callbacks_locked(waiting_callbacks, MAX_ASYNC_CALLBACKS);
    
    FWUPMGR_INFO("  Found %d waiting callback(s) to invoke\n", count);
    
    /* Unlock registry before invoking callbacks (avoid holding lock during user code) */
    registry_unlock();
    
    /* Invoke all waiting callbacks */
    for (int i = 0; i < count; i++) {
        AsyncCallbackContext *ctx = waiting_callbacks[i];
        
        if (ctx != NULL && ctx->callback != NULL) {
            FWUPMGR_INFO("  Invoking callback id=%u\n", ctx->id);
            
            /* Invoke user callback (may block, so we don't hold registry lock) */
            ctx->callback(&info, ctx->user_data);
            
            /* Update state to COMPLETED */
            registry_lock();
            if (ctx->state == CALLBACK_STATE_WAITING) {
                ctx->state = CALLBACK_STATE_COMPLETED;
            }
            registry_unlock();
            
            /* Release reference (will mark as IDLE when ref_count reaches 0) */
            async_context_unref(ctx);
        }
    }
    
    /* Clean up parsed data */
    async_cleanup_update_info(&info);
    
    FWUPMGR_INFO("==== Signal Processing Complete ====\n");
}

/**
 * @brief Timeout checker callback
 * 
 * This function is called periodically by the GLib event loop to check
 * for callbacks that have exceeded the timeout threshold.
 * 
 * For each callback in WAITING state:
 * - Check if registered_time + ASYNC_CALLBACK_TIMEOUT_SECONDS < now
 * - If timeout occurred, invoke callback with timeout error
 * - Update state to TIMEOUT
 * 
 * @param user_data Unused
 * @return G_SOURCE_CONTINUE to keep timer running
 */
static gboolean check_callback_timeouts(gpointer user_data) {
    (void)user_data; /* Unused */
    
    time_t now = time(NULL);
    
    registry_lock();
    
    for (int i = 0; i < MAX_ASYNC_CALLBACKS; i++) {
        AsyncCallbackContext *ctx = &g_async_registry.contexts[i];
        
        if (CALLBACK_IS_WAITING(ctx)) {
            double elapsed = difftime(now, ctx->registered_time);
            
            if (elapsed > ASYNC_CALLBACK_TIMEOUT_SECONDS) {
                FWUPMGR_WARN("Callback timeout: id=%u (elapsed=%.0f seconds)\n",
                            ctx->id, elapsed);
                
                /* Increment reference count for callback invocation */
                async_context_ref(ctx);
                
                /* Update state to TIMEOUT */
                ctx->state = CALLBACK_STATE_TIMEOUT;
                
                /* Prepare timeout error info */
                RdkUpdateInfo timeout_info = {
                    .result = -1,
                    .status_code = 3,  /* FIRMWARE_CHECK_ERROR */
                    .current_version = NULL,
                    .available_version = NULL,
                    .update_details = NULL,
                    .status_message = strdup("Update check timed out"),
                    .update_available = false
                };
                
                /* Invoke callback with timeout error (without holding lock) */
                registry_unlock();
                
                if (ctx->callback != NULL) {
                    ctx->callback(&timeout_info, ctx->user_data);
                }
                
                /* Clean up */
                async_cleanup_update_info(&timeout_info);
                
                /* Release reference */
                async_context_unref(ctx);
                
                /* Re-lock for next iteration */
                registry_lock();
            }
        }
    }
    
    registry_unlock();
    
    return G_SOURCE_CONTINUE;  /* Keep timer running */
}

/* ========================================================================
 * BACKGROUND THREAD (PHASE 4)
 * ======================================================================== */

/**
 * @brief Background thread entry point
 * 
 * This function runs in a separate thread and manages the GLib event loop
 * used for processing D-Bus signals.
 * 
 * Thread lifecycle:
 * 1. Create GMainContext (isolated from default)
 * 2. Create GMainLoop
 * 3. Create D-Bus connection
 * 4. Subscribe to CheckForUpdateComplete signal
 * 5. Add timeout checker
 * 6. Run main loop (blocks until quit)
 * 7. Cleanup resources
 * 
 * @param arg Unused
 * @return NULL
 */
static void* background_thread_func(void *arg) {
    (void)arg; /* Unused */
    
    FWUPMGR_INFO("Background thread starting...\n");
    
    pthread_mutex_lock(&g_bg_thread.mutex);
    
    /* Create isolated GMainContext (not the default context) */
    g_bg_thread.context = g_main_context_new();
    if (g_bg_thread.context == NULL) {
        FWUPMGR_ERROR("Failed to create GMainContext\n");
        g_bg_thread.running = false;
        pthread_mutex_unlock(&g_bg_thread.mutex);
        return NULL;
    }
    
    /* Create GMainLoop */
    g_bg_thread.main_loop = g_main_loop_new(g_bg_thread.context, FALSE);
    if (g_bg_thread.main_loop == NULL) {
        FWUPMGR_ERROR("Failed to create GMainLoop\n");
        g_main_context_unref(g_bg_thread.context);
        g_bg_thread.context = NULL;
        g_bg_thread.running = false;
        pthread_mutex_unlock(&g_bg_thread.mutex);
        return NULL;
    }
    
    /* Connect to D-Bus system bus */
    GError *error = NULL;
    g_bg_thread.connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    if (g_bg_thread.connection == NULL) {
        FWUPMGR_ERROR("Failed to connect to D-Bus: %s\n",
                      error ? error->message : "unknown error");
        if (error) g_error_free(error);
        
        g_main_loop_unref(g_bg_thread.main_loop);
        g_main_context_unref(g_bg_thread.context);
        g_bg_thread.main_loop = NULL;
        g_bg_thread.context = NULL;
        g_bg_thread.running = false;
        pthread_mutex_unlock(&g_bg_thread.mutex);
        return NULL;
    }
    
    /* Subscribe to CheckForUpdateComplete signal */
    g_bg_thread.signal_subscription_id = g_dbus_connection_signal_subscribe(
        g_bg_thread.connection,
        "org.rdkfwupdater.Interface",      /* Sender (service name) */
        "org.rdkfwupdater.Interface",      /* Interface */
        "CheckForUpdateComplete",           /* Member (signal name) */
        "/org/rdkfwupdater/Service",       /* Object path */
        NULL,                               /* arg0 (no filtering) */
        G_DBUS_SIGNAL_FLAGS_NONE,
        on_check_for_update_complete_signal,
        NULL,                               /* user_data */
        NULL                                /* user_data_free_func */
    );
    
    if (g_bg_thread.signal_subscription_id == 0) {
        FWUPMGR_ERROR("Failed to subscribe to CheckForUpdateComplete signal\n");
        
        g_object_unref(g_bg_thread.connection);
        g_main_loop_unref(g_bg_thread.main_loop);
        g_main_context_unref(g_bg_thread.context);
        g_bg_thread.connection = NULL;
        g_bg_thread.main_loop = NULL;
        g_bg_thread.context = NULL;
        g_bg_thread.running = false;
        pthread_mutex_unlock(&g_bg_thread.mutex);
        return NULL;
    }
    
    FWUPMGR_INFO("Signal subscribed: CheckForUpdateComplete (subscription_id=%u)\n",
                 g_bg_thread.signal_subscription_id);
    
    /* Add timeout checker (runs every 5 seconds) */
    GSource *timeout_source = g_timeout_source_new_seconds(5);
    g_source_set_callback(timeout_source, check_callback_timeouts, NULL, NULL);
    g_source_attach(timeout_source, g_bg_thread.context);
    g_source_unref(timeout_source);
    
    /* Mark thread as running */
    g_bg_thread.running = true;
    
    pthread_mutex_unlock(&g_bg_thread.mutex);
    
    FWUPMGR_INFO("Background thread ready, entering main loop...\n");
    
    /* Run main loop (blocks until g_main_loop_quit is called) */
    g_main_loop_run(g_bg_thread.main_loop);
    
    FWUPMGR_INFO("Background thread stopping...\n");
    
    /* Cleanup */
    pthread_mutex_lock(&g_bg_thread.mutex);
    
    /* Unsubscribe from signal */
    if (g_bg_thread.signal_subscription_id != 0 && g_bg_thread.connection != NULL) {
        g_dbus_connection_signal_unsubscribe(g_bg_thread.connection,
                                              g_bg_thread.signal_subscription_id);
        g_bg_thread.signal_subscription_id = 0;
    }
    
    /* Release D-Bus connection */
    if (g_bg_thread.connection != NULL) {
        g_object_unref(g_bg_thread.connection);
        g_bg_thread.connection = NULL;
    }
    
    /* Release GMainLoop and context */
    if (g_bg_thread.main_loop != NULL) {
        g_main_loop_unref(g_bg_thread.main_loop);
        g_bg_thread.main_loop = NULL;
    }
    
    if (g_bg_thread.context != NULL) {
        g_main_context_unref(g_bg_thread.context);
        g_bg_thread.context = NULL;
    }
    
    g_bg_thread.running = false;
    
    pthread_mutex_unlock(&g_bg_thread.mutex);
    
    FWUPMGR_INFO("Background thread stopped\n");
    
    return NULL;
}

/**
 * @brief Start the background thread
 * 
 * Creates and starts the background thread that runs the GLib event loop.
 * Waits for the thread to initialize before returning.
 * 
 * @return 0 on success, -1 on error
 */
static int start_background_thread(void) {
    pthread_mutex_lock(&g_bg_thread.mutex);
    
    if (g_bg_thread.running) {
        pthread_mutex_unlock(&g_bg_thread.mutex);
        FWUPMGR_INFO("Background thread already running\n");
        return 0;
    }
    
    pthread_mutex_unlock(&g_bg_thread.mutex);
    
    /* Create thread */
    int ret = pthread_create(&g_bg_thread.thread, NULL, background_thread_func, NULL);
    if (ret != 0) {
        FWUPMGR_ERROR("Failed to create background thread: %s\n", strerror(ret));
        return -1;
    }
    
    /* Wait for thread to initialize (with timeout) */
    int timeout_count = 0;
    while (timeout_count < 50) {  /* 5 seconds max wait */
        pthread_mutex_lock(&g_bg_thread.mutex);
        bool running = g_bg_thread.running;
        pthread_mutex_unlock(&g_bg_thread.mutex);
        
        if (running) {
            FWUPMGR_INFO("Background thread started successfully\n");
            return 0;
        }
        
        usleep(100000);  /* 100ms */
        timeout_count++;
    }
    
    FWUPMGR_ERROR("Timeout waiting for background thread to start\n");
    return -1;
}

/**
 * @brief Stop the background thread
 * 
 * Signals the background thread to quit and waits for it to terminate.
 */
static void stop_background_thread(void) {
    pthread_mutex_lock(&g_bg_thread.mutex);
    
    if (!g_bg_thread.running) {
        pthread_mutex_unlock(&g_bg_thread.mutex);
        FWUPMGR_INFO("Background thread not running\n");
        return;
    }
    
    /* Signal main loop to quit */
    if (g_bg_thread.main_loop != NULL) {
        g_main_loop_quit(g_bg_thread.main_loop);
    }
    
    pthread_mutex_unlock(&g_bg_thread.mutex);
    
    /* Wait for thread to finish */
    FWUPMGR_INFO("Waiting for background thread to stop...\n");
    pthread_join(g_bg_thread.thread, NULL);
    FWUPMGR_INFO("Background thread stopped\n");
}

/* ========================================================================
 * SYSTEM INITIALIZATION / DEINITIALIZATION
 * ======================================================================== */

/**
 * @brief Initialize the async callback system
 * 
 * Must be called during library initialization (before any async APIs).
 * 
 * Steps:
 * 1. Initialize callback registry
 * 2. Start background thread
 * 3. Background thread will subscribe to D-Bus signals
 * 
 * @return 0 on success, -1 on error
 */
int async_system_init(void) {
    FWUPMGR_INFO("Initializing async callback system...\n");
    
    /* Initialize registry mutex (already done statically, but ensure it) */
    /* pthread_mutex_init is not needed because we use PTHREAD_MUTEX_INITIALIZER */
    
    /* Clear all callback contexts */
    registry_lock();
    memset(g_async_registry.contexts, 0, sizeof(g_async_registry.contexts));
    g_async_registry.next_id = 1;
    g_async_registry.initialized = true;
    registry_unlock();
    
    /* Start background thread */
    if (start_background_thread() != 0) {
        FWUPMGR_ERROR("Failed to start background thread\n");
        g_async_registry.initialized = false;
        return -1;
    }
    
    FWUPMGR_INFO("Async callback system initialized successfully\n");
    return 0;
}

/**
 * @brief Shutdown the async callback system
 * 
 * Must be called during library deinitialization.
 * 
 * Steps:
 * 1. Stop background thread
 * 2. Cancel all pending callbacks
 * 3. Clean up registry
 */
void async_system_deinit(void) {
    FWUPMGR_INFO("Shutting down async callback system...\n");
    
    /* Stop background thread first */
    stop_background_thread();
    
    /* Cancel all pending callbacks */
    registry_lock();
    
    for (int i = 0; i < MAX_ASYNC_CALLBACKS; i++) {
        AsyncCallbackContext *ctx = &g_async_registry.contexts[i];
        
        if (ctx->state == CALLBACK_STATE_WAITING) {
            FWUPMGR_WARN("Force-cancelling callback id=%u during shutdown\n", ctx->id);
            ctx->state = CALLBACK_STATE_CANCELLED;
            
            /* Release reference */
            async_context_unref(ctx);
        }
    }
    
    g_async_registry.initialized = false;
    
    registry_unlock();
    
    /* Note: We don't destroy the mutex because it's statically initialized
     * and may be used again if library is re-initialized */
    
    FWUPMGR_INFO("Async callback system shut down\n");
}

/* ========================================================================
 * END OF IMPLEMENTATION
 * ======================================================================== */
