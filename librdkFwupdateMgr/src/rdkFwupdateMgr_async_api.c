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
 * @file rdkFwupdateMgr_async_api.c
 * @brief Public API wrappers for async CheckForUpdate
 *
 * This file provides the public API functions that applications use.
 * It acts as a bridge between the public API (in rdkFwupdateMgr_client.h)
 * and the internal implementation (in rdkFwupdateMgr_async.c).
 *
 * KEY RESPONSIBILITIES:
 * ====================
 * 1. Type conversion between public and internal types
 * 2. Parameter validation
 * 3. Triggering D-Bus method call to daemon
 * 4. Mapping internal callbacks to user callbacks
 * 5. Library initialization/deinitialization hooks
 */

#include "rdkFwupdateMgr_client.h"
#include "rdkFwupdateMgr_async_internal.h"
#include "rdkFwupdateMgr_log.h"
#include <gio/gio.h>
#include <stdlib.h>
#include <string.h>

/* ========================================================================
 * D-BUS CONSTANTS (reused from rdkFwupdateMgr_api.c)
 * ======================================================================== */

#define DBUS_SERVICE_NAME       "org.rdkfwupdater.Interface"
#define DBUS_OBJECT_PATH        "/org/rdkfwupdater/Service"
#define DBUS_INTERFACE_NAME     "org.rdkfwupdater.Interface"
#define DBUS_TIMEOUT_MS         30000

/* ========================================================================
 * INTERNAL CALLBACK WRAPPER
 * ======================================================================== */

/**
 * @brief Wrapper structure for user callback and data
 * 
 * This structure is passed as user_data to the internal async system.
 * It contains the user's callback and user_data, which we forward to
 * when the internal callback is invoked.
 */
typedef struct {
    AsyncUpdateCallback user_callback;
    void *user_data;
} CallbackWrapper;

/**
 * @brief Internal callback that converts types and calls user callback
 * 
 * This is the callback registered with the internal async system.
 * It converts from internal RdkUpdateInfo to public AsyncUpdateInfo
 * and forwards to the user's callback.
 * 
 * @param internal_info Internal update info
 * @param user_data CallbackWrapper containing user callback and data
 */
static void internal_callback_wrapper(const RdkUpdateInfo *internal_info, void *user_data) {
    CallbackWrapper *wrapper = (CallbackWrapper *)user_data;
    
    if (wrapper == NULL || wrapper->user_callback == NULL) {
        FWUPMGR_ERROR("internal_callback_wrapper: Invalid wrapper or callback\n");
        return;
    }
    
    /* Convert internal RdkUpdateInfo to public AsyncUpdateInfo */
    AsyncUpdateInfo public_info = {
        .result = internal_info->result,
        .status_code = internal_info->status_code,
        .current_version = internal_info->current_version,
        .available_version = internal_info->available_version,
        .update_details = internal_info->update_details,
        .status_message = internal_info->status_message,
        .update_available = internal_info->update_available
    };
    
    /* Call user's callback */
    wrapper->user_callback(&public_info, wrapper->user_data);
    
    /* Free wrapper (allocated in checkForUpdate_async) */
    free(wrapper);
}

/* ========================================================================
 * PUBLIC API IMPLEMENTATION
 * ======================================================================== */

/**
 * @brief Check for firmware update asynchronously (public API)
 * 
 * See rdkFwupdateMgr_client.h for full documentation.
 * 
 * This function:
 * 1. Validates parameters
 * 2. Allocates callback wrapper
 * 3. Registers callback with internal async system
 * 4. Sends D-Bus method call to daemon
 * 5. Returns callback ID
 * 
 * @param callback User callback function (required)
 * @param user_data User data pointer (optional)
 * @return Callback ID on success, 0 on error
 */
AsyncCallbackId checkForUpdate_async(AsyncUpdateCallback callback, void *user_data) {
    if (callback == NULL) {
        FWUPMGR_ERROR("checkForUpdate_async: callback is NULL\n");
        return 0;
    }
    
    FWUPMGR_INFO("checkForUpdate_async: Starting async check\n");
    
    /* Allocate callback wrapper */
    CallbackWrapper *wrapper = (CallbackWrapper *)malloc(sizeof(CallbackWrapper));
    if (wrapper == NULL) {
        FWUPMGR_ERROR("checkForUpdate_async: Failed to allocate callback wrapper\n");
        return 0;
    }
    
    wrapper->user_callback = callback;
    wrapper->user_data = user_data;
    
    /* Register with internal async system */
    uint32_t callback_id = async_register_callback(internal_callback_wrapper, wrapper);
    if (callback_id == 0) {
        FWUPMGR_ERROR("checkForUpdate_async: Failed to register callback\n");
        free(wrapper);
        return 0;
    }
    
    /* Send D-Bus method call to daemon (async, non-blocking) */
    /* Note: We don't wait for the response - the signal will tell us the result */
    GError *error = NULL;
    GDBusConnection *connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    if (connection == NULL) {
        FWUPMGR_ERROR("checkForUpdate_async: Failed to connect to D-Bus: %s\n",
                      error ? error->message : "unknown");
        if (error) g_error_free(error);
        
        /* Cancel the registered callback */
        async_cancel_callback(callback_id);
        return 0;
    }
    
    /* Call CheckForUpdate method (async) */
    /* We use "LibraryAsyncClient" as the process name since we don't have a registered handle */
    g_dbus_connection_call(
        connection,
        DBUS_SERVICE_NAME,
        DBUS_OBJECT_PATH,
        DBUS_INTERFACE_NAME,
        "CheckForUpdate",
        g_variant_new("(s)", "LibraryAsyncClient"),  /* Process name */
        NULL,                                        /* Expected reply type */
        G_DBUS_CALL_FLAGS_NONE,
        DBUS_TIMEOUT_MS,
        NULL,                                        /* Cancellable */
        NULL,                                        /* Async callback (we don't need it) */
        NULL                                         /* User data */
    );
    
    g_object_unref(connection);
    
    FWUPMGR_INFO("checkForUpdate_async: D-Bus call sent, callback_id=%u\n", callback_id);
    
    return callback_id;
}

/**
 * @brief Cancel a pending async operation (public API)
 * 
 * See rdkFwupdateMgr_client.h for full documentation.
 * 
 * This simply forwards to the internal async_cancel_callback function.
 * 
 * @param callback_id ID returned by checkForUpdate_async
 * @return 0 on success, -1 on error
 */
int checkForUpdate_async_cancel(AsyncCallbackId callback_id) {
    FWUPMGR_INFO("checkForUpdate_async_cancel: Cancelling callback_id=%u\n", callback_id);
    return async_cancel_callback(callback_id);
}

/* ========================================================================
 * LIBRARY INITIALIZATION HOOKS
 * ======================================================================== */

/**
 * @brief Initialize async system (called from library init)
 * 
 * This function should be called during library initialization
 * (e.g., from a constructor attribute function).
 * 
 * @return 0 on success, -1 on error
 */
int __attribute__((constructor)) rdkFwupdateMgr_async_lib_init(void) {
    FWUPMGR_INFO("=== Initializing Async Firmware Update Library ===\n");
    
    int ret = async_system_init();
    if (ret != 0) {
        FWUPMGR_ERROR("Failed to initialize async system\n");
        return -1;
    }
    
    FWUPMGR_INFO("=== Async Library Initialized Successfully ===\n");
    return 0;
}

/**
 * @brief Shutdown async system (called from library deinit)
 * 
 * This function should be called during library deinitialization
 * (e.g., from a destructor attribute function).
 */
void __attribute__((destructor)) rdkFwupdateMgr_async_lib_deinit(void) {
    FWUPMGR_INFO("=== Shutting Down Async Firmware Update Library ===\n");
    
    async_system_deinit();
    
    FWUPMGR_INFO("=== Async Library Shutdown Complete ===\n");
}

/* ========================================================================
 * END OF FILE
 * ======================================================================== */
