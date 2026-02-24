/*
 * Copyright 2025 Comcast Cable Communications Management, LLC
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
 * @file rdkFwupdateMgr_process.c
 * @brief Implementation of process registration APIs for firmware update clients
 *
 * This file implements the D-Bus client-side logic for process registration
 * and unregistration with the rdkFwupdateMgr daemon.
 *
 * ARCHITECTURE:
 * =============
 * - Uses GDBus (GLib D-Bus bindings) for IPC with daemon
 * - Synchronous D-Bus calls (registration is fast, <10ms typically)
 * - No background threads (all operations on caller's thread)
 * - Stateless design: Handle encodes all state (handler_id as string)
 *
 * D-BUS PROTOCOL:
 * ===============
 * Service Name:   org.rdkfwupdater.Interface
 * Object Path:    /org/rdkfwupdater/Service
 * Interface:      org.rdkfwupdater.Interface
 *
 * RegisterProcess(processName: s, libVersion: s) -> (handler_id: t)
 *   - Registers client process
 *   - Returns uint64 handler_id on success
 *   - Throws D-Bus error on failure
 *
 * UnregisterProcess(handler_id: t) -> (success: b)
 *   - Unregisters client process
 *   - Returns boolean success status
 *
 * MEMORY MANAGEMENT:
 * ==================
 * - Handle is allocated with malloc() and freed by unregisterProcess()
 * - GDBus objects (connection, proxy) are freed with g_object_unref()
 * - Error handling ensures no leaks on failure paths
 * - Caller must never free() the returned handle themselves
 *
 * THREAD SAFETY:
 * ==============
 * - GDBus is thread-safe for synchronous calls
 * - No internal locks needed (stateless per-call)
 * - Multiple threads can call registerProcess() concurrently (different process names)
 * - Same handle should not be unregistered from multiple threads (undefined behavior)
 *
 * ERROR HANDLING:
 * ===============
 * - All errors logged via fprintf(stderr) for visibility
 * - NULL checks on all pointer parameters
 * - D-Bus errors caught and handled gracefully
 * - Registration failures return NULL (safe to check)
 * - Unregistration failures are silent (best-effort cleanup)
 */

#include "rdkFwupdateMgr_process.h"
#include "rdkFwupdateMgr_log.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <gio/gio.h>
#include <inttypes.h>
/* ========================================================================
 * CONSTANTS
 * ======================================================================== */

/** D-Bus service name for firmware update daemon */
#define DBUS_SERVICE_NAME       "org.rdkfwupdater.Interface"

/** D-Bus object path */
#define DBUS_OBJECT_PATH        "/org/rdkfwupdater/Service"

/** D-Bus interface name */
#define DBUS_INTERFACE_NAME     "org.rdkfwupdater.Interface"

/** Maximum length for process name (enforced by daemon) */
#define MAX_PROCESS_NAME_LEN    256

/** Maximum length for library version string */
#define MAX_LIB_VERSION_LEN     64

/** Default D-Bus call timeout in milliseconds (10 seconds) */
#define DBUS_TIMEOUT_MS         10000

/* ========================================================================
 * INTERNAL CONTEXT STRUCTURE
 * ======================================================================== */

/**
 * @brief Internal representation of FirmwareInterfaceHandle
 *
 * This structure is cast to/from the opaque FirmwareInterfaceHandle type.
 * The handle returned to callers is actually the handler_id_str field,
 * which is a string representation of the handler_id.
 *
 * DESIGN RATIONALE:
 * - Simple string handle provides ABI stability
 * - Handler ID is all that's needed for subsequent API calls
 * - No need to carry D-Bus connection/proxy in handle (stateless)
 * - Each API call creates fresh D-Bus proxy (overhead acceptable for infrequent calls)
 *
 * MEMORY LAYOUT:
 * - handler_id_str: Heap-allocated string (e.g., "12345")
 * - This IS the handle returned to caller
 * - freed by unregisterProcess()
 */
typedef struct _FirmwareInterfaceContext {
    uint64_t handler_id;          // Handler ID from daemon
    char handler_id_str[32];      // String representation (this is the handle)
} FirmwareInterfaceContext;

/* ========================================================================
 * INTERNAL HELPER FUNCTIONS
 * ======================================================================== */

/**
 * @brief Create a D-Bus proxy for communication with daemon
 *
 * Creates a GDBusProxy object for synchronous method calls to the
 * firmware update daemon.
 *
 * @param error Output parameter for GError (caller must free with g_error_free)
 * @return GDBusProxy* on success, NULL on failure
 */
static GDBusProxy* create_dbus_proxy(GError **error)
{
    GDBusConnection *connection = NULL;
    GDBusProxy *proxy = NULL;

    // Connect to system bus
    connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, error);
    if (!connection) {
        FWUPMGR_ERROR("Failed to connect to D-Bus system bus: %s\n",
                (*error)->message);
        return NULL;
    }

    // Create proxy for daemon interface
    proxy = g_dbus_proxy_new_sync(
        connection,
        G_DBUS_PROXY_FLAGS_NONE,
        NULL,                       // GDBusInterfaceInfo
        DBUS_SERVICE_NAME,
        DBUS_OBJECT_PATH,
        DBUS_INTERFACE_NAME,
        NULL,                       // GCancellable
        error
    );

    // Connection is owned by proxy, will be freed when proxy is unreffed
    g_object_unref(connection);

    if (!proxy) {
        FWUPMGR_ERROR("Failed to create D-Bus proxy: %s\n",
                (*error)->message);
        return NULL;
    }

    return proxy;
}

/**
 * @brief Validate process name parameter
 *
 * Ensures process name meets requirements:
 * - Not NULL
 * - Not empty string
 * - Within maximum length
 *
 * @param processName Process name to validate
 * @return true if valid, false otherwise
 */
static bool validate_process_name(const char *processName)
{
    if (!processName) {
        FWUPMGR_ERROR("processName is NULL\n");
        return false;
    }

    if (strlen(processName) == 0) {
        FWUPMGR_ERROR("processName is empty\n");
        return false;
    }

    if (strlen(processName) > MAX_PROCESS_NAME_LEN) {
        FWUPMGR_ERROR("processName exceeds max length (%d)\n",
                MAX_PROCESS_NAME_LEN);
        return false;
    }

    return true;
}

/**
 * @brief Validate library version parameter
 *
 * Ensures library version meets requirements:
 * - Not NULL (empty string is OK)
 * - Within maximum length
 *
 * @param libVersion Library version string to validate
 * @return true if valid, false otherwise
 */
static bool validate_lib_version(const char *libVersion)
{
    if (!libVersion) {
        FWUPMGR_ERROR("libVersion is NULL\n");
        return false;
    }

    if (strlen(libVersion) > MAX_LIB_VERSION_LEN) {
        FWUPMGR_ERROR("libVersion exceeds max length (%d)\n",
                MAX_LIB_VERSION_LEN);
        return false;
    }

    return true;
}

/* ========================================================================
 * PUBLIC API IMPLEMENTATION
 * ======================================================================== */

/**
 * @brief Register a process with the firmware update daemon
 *
 * See rdkFwupdateMgr_process.h for full API documentation.
 *
 * IMPLEMENTATION NOTES:
 * - Creates D-Bus proxy on-demand (no persistent connection)
 * - Synchronous D-Bus call (blocks until daemon responds)
 * - Timeout: 10 seconds (configurable via DBUS_TIMEOUT_MS)
 * - Returns string handle (handler_id as decimal string)
 *
 * ERROR HANDLING:
 * - Input validation: NULL/empty checks, length checks
 * - D-Bus errors: Connection failures, daemon errors
 * - Memory allocation failures
 * - All errors return NULL with descriptive stderr messages
 */
FirmwareInterfaceHandle registerProcess(const char *processName, const char *libVersion)
{
    GDBusProxy *proxy = NULL;
    GError *error = NULL;
    GVariant *result = NULL;
    guint64 handler_id = 0;
    char *handle_str = NULL;

    FWUPMGR_INFO("registerProcess() called\n");
    FWUPMGR_INFO("  processName: '%s'\n", processName ? processName : "NULL");
    FWUPMGR_INFO("  libVersion:  '%s'\n", libVersion ? libVersion : "NULL");

    // Validate inputs
    if (!validate_process_name(processName)) {
        return FIRMWARE_INVALID_HANDLE;
    }

    if (!validate_lib_version(libVersion)) {
        return FIRMWARE_INVALID_HANDLE;
    }

    // Create D-Bus proxy
    proxy = create_dbus_proxy(&error);
    if (!proxy) {
        // Error already logged by create_dbus_proxy
        if (error) {
            g_error_free(error);
        }
        return FIRMWARE_INVALID_HANDLE;
    }

    fprintf(stderr, "[rdkFwupdateMgr] D-Bus proxy created successfully\n");

    // Call RegisterProcess D-Bus method
    fprintf(stderr, "[rdkFwupdateMgr] Calling RegisterProcess D-Bus method...\n");
    result = g_dbus_proxy_call_sync(
        proxy,
        "RegisterProcess",
        g_variant_new("(ss)", processName, libVersion),
        G_DBUS_CALL_FLAGS_NONE,
        DBUS_TIMEOUT_MS,
        NULL,                       // GCancellable
        &error
    );

    if (!result) {
        FWUPMGR_ERROR("RegisterProcess D-Bus call failed: %s\n",
                error->message);
        g_error_free(error);
        g_object_unref(proxy);
        return FIRMWARE_INVALID_HANDLE;
    }

    // Extract handler_id from result
    g_variant_get(result, "(t)", &handler_id);
    g_variant_unref(result);
    g_object_unref(proxy);

    FWUPMGR_INFO("Registration successful\n");
    FWUPMGR_INFO("  handler_id: %"G_GUINT64_FORMAT"\n", handler_id);

    // Convert handler_id to string (this becomes the handle)
    handle_str = (char*)malloc(32);  // Enough for uint64 as decimal string
    if (!handle_str) {
        FWUPMGR_ERROR("Failed to allocate memory for handle\n");
        // Registration succeeded on daemon side, but we can't return handle
        // Caller should retry or handle gracefully
        return FIRMWARE_INVALID_HANDLE;
    }

    //snprintf(handle_str, 32, "" %PRIu64, handler_id);
    snprintf(handle_str, 32, "%" PRIu64, handler_id);
    FWUPMGR_INFO("Handle created: '%s'\n", handle_str);

    return (FirmwareInterfaceHandle)handle_str;
}

/**
 * @brief Unregister a previously registered process
 *
 * See rdkFwupdateMgr_process.h for full API documentation.
 *
 * IMPLEMENTATION NOTES:
 * - Best-effort cleanup (errors are logged but not propagated)
 * - Frees handle memory regardless of D-Bus call success
 * - Idempotent: Safe to call with NULL handle (no-op)
 * - Daemon may already have removed the registration (connection lost)
 *
 * ERROR HANDLING:
 * - NULL handle: No-op, returns immediately
 * - D-Bus errors: Logged to stderr, but cleanup continues
 * - Memory freed regardless of D-Bus call success
 */
void unregisterProcess(FirmwareInterfaceHandle handler)
{
    GDBusProxy *proxy = NULL;
    GError *error = NULL;
    GVariant *result = NULL;
    guint64 handler_id = 0;
    gboolean success = FALSE;

    // NULL check: Safe to unregister NULL handle (no-op)
    if (!handler) {
        FWUPMGR_INFO("unregisterProcess() called with NULL handle (no-op)\n");
        return;
    }

    FWUPMGR_INFO("unregisterProcess() called\n");
    FWUPMGR_INFO("  handle: '%s'\n", handler);

    // Parse handler_id from string handle
    errno = 0;
    handler_id = strtoull(handler, NULL, 10);
    if (errno != 0 || handler_id == 0) {
        FWUPMGR_ERROR("Invalid handle format (not a valid handler_id)\n");
        // Still free the handle memory (best-effort cleanup)
        free(handler);
        return;
    }

    FWUPMGR_INFO("  handler_id: %"G_GUINT64_FORMAT"\n", handler_id);

    // Create D-Bus proxy
    proxy = create_dbus_proxy(&error);
    if (!proxy) {
        FWUPMGR_WARN("Failed to create D-Bus proxy for unregister\n");
        if (error) {
            FWUPMGR_WARN("  Error: %s\n", error->message);
            g_error_free(error);
        }
        // Continue with cleanup even if D-Bus call fails
        free(handler);
        return;
    }

    // Call UnregisterProcess D-Bus method
    FWUPMGR_INFO("Calling UnregisterProcess D-Bus method...\n");
    result = g_dbus_proxy_call_sync(
        proxy,
        "UnregisterProcess",
        g_variant_new("(t)", handler_id),
        G_DBUS_CALL_FLAGS_NONE,
        DBUS_TIMEOUT_MS,
        NULL,                       // GCancellable
        &error
    );

    if (!result) {
        FWUPMGR_WARN("UnregisterProcess D-Bus call failed: %s\n",
                error->message);
        FWUPMGR_WARN("  (This is OK if daemon already cleaned up)\n");
        g_error_free(error);
        g_object_unref(proxy);
        // Continue with local cleanup
        free(handler);
        return;
    }

    // Extract success flag from result
    g_variant_get(result, "(b)", &success);
    g_variant_unref(result);
    g_object_unref(proxy);

    if (success) {
        FWUPMGR_INFO("Unregistration successful\n");
    } else {
        FWUPMGR_WARN("Daemon reported unregistration failure\n");
        FWUPMGR_WARN("  (Handler may have already been unregistered)\n");
    }

    // Free handle memory (always, regardless of D-Bus call success)
    free(handler);
    FWUPMGR_INFO("Handle memory freed\n");
}
