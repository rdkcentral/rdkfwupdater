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
 * Service Name:   org.rdkfwupdater.Service
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
 * - All errors logged via FWUPMGR_* macros (rdkv_cdl_log_wrapper backend)
 * - NULL checks on all pointer parameters
 * - D-Bus errors caught and handled gracefully
 * - Registration failures return NULL (safe to check)
 * - Unregistration failures are silent (best-effort cleanup)
 */

#include "rdkFwupdateMgr_client.h"
#include "rdkFwupdateMgr_async_internal.h"
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
#define DBUS_SERVICE_NAME       "org.rdkfwupdater.Service"

/** D-Bus object path */
#define DBUS_OBJECT_PATH        "/org/rdkfwupdater/Service"

/** D-Bus interface name */
#define DBUS_INTERFACE_NAME     "org.rdkfwupdater.Interface"

/** Maximum length for process name (enforced by daemon) */
#define MAX_PROCESS_NAME_LEN    256

/** Maximum length for library version string */
#define MAX_LIB_VERSION_LEN     64

/* DBUS_SYNC_TIMEOUT_MS is defined in rdkFwupdateMgr_async_internal.h (10s) */

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
//typedef struct _FirmwareInterfaceContext {
 //   uint64_t handler_id;          // Handler ID from daemon
  //  char handler_id_str[32];      // String representation (this is the handle)
//} FirmwareInterfaceContext;

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
        // GLib doesn't guarantee *error is set in all failure cases
        // (e.g., extreme out-of-memory conditions). Guard against NULL.
        FWUPMGR_ERROR("Failed to connect to D-Bus system bus: %s\n",
                (error && *error) ? (*error)->message : "unknown error (GError not set)");
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
        // GLib doesn't guarantee *error is set in all failure cases.
        FWUPMGR_ERROR("Failed to create D-Bus proxy: %s\n",
                (error && *error) ? (*error)->message : "unknown error (GError not set)");
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

/* PUBLIC API IMPLEMENTATION */

/*
 * registerProcess - Register a client process with the firmware update daemon.
 *
 * OVERVIEW
 *
 * PURPOSE:
 *   This is the MANDATORY first call before using any other library API.
 *   It establishes a session with the rdkFwupdateMgr daemon by:
 *     1. Sending the client's name and version to the daemon via D-Bus
 *     2. Receiving a unique numeric handler_id from the daemon
 *     3. Converting that ID into a string "handle" returned to caller
 *     4. Spinning up the library's internal async engine (BG thread,
 *        callback registries, D-Bus signal subscriptions)
 *
 * WHAT THE CALLER GETS BACK:
 *   A FirmwareInterfaceHandle (which is just a typedef for char*).
 *   Example: "1", "42", "1023" -- it's the daemon-assigned handler_id
 *   as a decimal string. This handle must be passed to ALL subsequent
 *   API calls (checkForUpdate, downloadFirmware, updateFirmware) and
 *   eventually to unregisterProcess() to clean up.
 *
 * THREADING MODEL:
 *   - This function runs entirely on the CALLER'S thread
 *   - It BLOCKS (synchronous D-Bus call) for up to DBUS_SYNC_TIMEOUT_MS (10s)
 *   - At the end, it spawns a background thread for signal reception
 *   - After return: 2 threads exist (caller's + library BG thread)
 *
 * D-BUS WIRE PROTOCOL:
 *   Method: "RegisterProcess"
 *   Input:  GVariant type "(ss)" -- two strings: processName, libVersion
 *   Output: GVariant type "(t)" -- one uint64: handler_id
 *   The call creates an EPHEMERAL D-Bus connection (new connection each
 *   time, destroyed after use). The daemon identifies us by handler_id,
 *   NOT by D-Bus sender address.
 *
 * MEMORY CONTRACT:
 *   - Library OWNS the returned handle (malloc'd internally)
 *   - Caller must NEVER free() it directly
 *   - Caller must call unregisterProcess(handle) to release it
 *
 * POSSIBLE RETURN VALUES:
 *   Non-NULL string -- Success. Use this handle for all subsequent calls.
 *   NULL            -- Failure. Check logs. Daemon might not be running.
 *
 * EXECUTION FLOW (step numbers match code comments below):
 *
 *   [1] Log entry + parameter echo
 *   [2] Validate processName (NULL? empty? too long?)
 *   [3] Validate libVersion (NULL? too long?)
 *   [4] Create ephemeral D-Bus proxy to daemon
 *   [5] Call "RegisterProcess" method synchronously (BLOCKS here)
 *   [6] Extract handler_id (uint64) from daemon's reply
 *   [7] Allocate 32-byte string buffer on heap
 *   [8] Convert handler_id to decimal string
 *   [9] Initialize internal async engine:
 *       - 3 callback registries (check, download, update)
 *       - 4 mutexes
 *       - 1 background thread (subscribes to D-Bus signals)
 *   [10] Return handle to caller
 *
 * @param processName  A human-readable name identifying this client process.
 *                     Must be non-NULL, non-empty, max 256 chars.
 *                     Examples: "example_plugin", "tr069_agent", "webui_service"
 *                     The daemon enforces UNIQUENESS -- two different processes
 *                     cannot register with the same name simultaneously.
 *
 * @param libVersion   The version string of this library the client was built
 *                     against. Must be non-NULL, max 64 chars (empty string OK).
 *                     Typically pass the LIB_VERSION macro from the public header
 *                     (currently "1.0.0"). Used by daemon for compatibility tracking.
 *
 * @return FirmwareInterfaceHandle (char*) on success -- the session handle.
 *         NULL on any failure (validation, D-Bus, daemon rejection, OOM).
 *
 * Note: This function is THREAD-SAFE for concurrent calls with different
 *       process names. Do NOT call it twice with the same processName --
 *       the daemon will reject the second registration.
 *
 * Warning: After this returns non-NULL, a background thread is running.
 *          You MUST call unregisterProcess() before process exit, or the
 *          thread will be forcibly killed by the OS (potential resource leak).
 *
 * See also: unregisterProcess() -- The cleanup counterpart to this function.
 * See also: internal_system_init() -- The async engine startup called at the end.
 * See also: checkForUpdate() -- First API you'd typically call after registration.
 */
FirmwareInterfaceHandle registerProcess(const char *processName, const char *libVersion)
{
    GDBusProxy *proxy = NULL;
    GError *error = NULL;
    GVariant *result = NULL;
    guint64 handler_id = 0;
    char *handle_str = NULL;

    /*  Log entry */
    FWUPMGR_INFO("registerProcess() called\n");
    FWUPMGR_INFO("  processName: '%s'\n", processName ? processName : "NULL");
    FWUPMGR_INFO("  libVersion:  '%s'\n", libVersion ? libVersion : "NULL");

    /* Validate processName */
    if (!validate_process_name(processName)) {
        return NULL;
    }

    /* Validate libVersion */
    if (!validate_lib_version(libVersion)) {
        return NULL;
    }

    /* Create ephemeral D-Bus proxy */
    proxy = create_dbus_proxy(&error);
    if (!proxy) {
        if (error) {
            g_error_free(error);
        }
        return NULL;
    }

    FWUPMGR_INFO("D-Bus proxy created successfully\n");

    /* Synchronous D-Bus call: RegisterProcess(processName, libVersion) → handler_id */
    FWUPMGR_INFO("Calling RegisterProcess D-Bus method...\n");
    result = g_dbus_proxy_call_sync(
        proxy,
        "RegisterProcess",
        g_variant_new("(ss)", processName, libVersion),
        G_DBUS_CALL_FLAGS_NONE,
        DBUS_SYNC_TIMEOUT_MS,
        NULL,                       /* GCancellable */
        &error
    );

    if (!result) {
        FWUPMGR_ERROR("RegisterProcess D-Bus call failed: %s\n",
                error->message);
        g_error_free(error);
        g_object_unref(proxy);
        return NULL;
    }

    /* Extract handler_id from daemon reply "(t)" and free D-Bus resources */
    g_variant_get(result, "(t)", &handler_id);
    g_variant_unref(result);
    g_object_unref(proxy);

    FWUPMGR_INFO("Registration successful\n");
    FWUPMGR_INFO("  handler_id: %"G_GUINT64_FORMAT"\n", handler_id);

    /* Allocate handle string (32 bytes: enough for max uint64 decimal) */
    handle_str = (char*)malloc(32);
    if (!handle_str) {
        FWUPMGR_ERROR("Failed to allocate memory for handle\n");
        
        /* CRITICAL: Registration succeeded on daemon, but we can't return handle.
         * Must unregister to prevent resource leak on daemon side. */
        FWUPMGR_ERROR("Attempting best-effort cleanup: UnregisterProcess(%" PRIu64 ")\n", 
                      handler_id);
        
        /* Create new proxy for cleanup call (previous one was already freed) */
        GError *cleanup_error = NULL;
        GDBusProxy *cleanup_proxy = create_dbus_proxy(&cleanup_error);
        if (cleanup_proxy) {
            GVariant *cleanup_result = g_dbus_proxy_call_sync(
                cleanup_proxy,
                "UnregisterProcess",
                g_variant_new("(t)", handler_id),
                G_DBUS_CALL_FLAGS_NONE,
                DBUS_SYNC_TIMEOUT_MS,
                NULL,
                &cleanup_error
            );
            
            if (cleanup_result) {
                FWUPMGR_INFO("Cleanup successful: process unregistered\n");
                g_variant_unref(cleanup_result);
            } else {
                FWUPMGR_ERROR("Cleanup failed: %s (registration may be leaked)\n",
                              cleanup_error ? cleanup_error->message : "unknown");
                if (cleanup_error) g_error_free(cleanup_error);
            }
            g_object_unref(cleanup_proxy);
        } else {
            FWUPMGR_ERROR("Cleanup proxy creation failed (registration leaked)\n");
            if (cleanup_error) g_error_free(cleanup_error);
        }
        
        return NULL;
    }

    /* Convert handler_id to decimal string */
    snprintf(handle_str, 32, "%" PRIu64, handler_id);
    FWUPMGR_INFO("Handle created: '%s'\n", handle_str);

    /*  Initialize async engine: registries, mutexes, BG thread */
    FWUPMGR_INFO("=== rdkFwupdateMgr Creating thread for listen ===\n");
    if (internal_system_init() != 0) {
        FWUPMGR_ERROR("rdkFwupdateMgr_lib_init: internal_system_init FAILED\n");
        GError *cleanup_error = NULL;
        GDBusProxy *cleanup_proxy = create_dbus_proxy(&cleanup_error);
        if (cleanup_proxy) {
            GVariant *cleanup_result = g_dbus_proxy_call_sync(
                cleanup_proxy,
                "UnregisterProcess",
                g_variant_new("(t)", handler_id),
                G_DBUS_CALL_FLAGS_NONE,
                DBUS_SYNC_TIMEOUT_MS,
                NULL,
                &cleanup_error
            );
            if (cleanup_result) {
                FWUPMGR_INFO("Cleanup successful: process unregistered\n");
                g_variant_unref(cleanup_result);
            } else {
                FWUPMGR_ERROR("Cleanup failed: %s (registration may be leaked)\n",
                              cleanup_error ? cleanup_error->message : "unknown");
                if (cleanup_error) g_error_free(cleanup_error);
            }
            g_object_unref(cleanup_proxy);
        } else {
            FWUPMGR_ERROR("Cleanup proxy creation failed (registration leaked)\n");
            if (cleanup_error) g_error_free(cleanup_error);
        }

        free(handle_str);
        return NULL;
    }
    FWUPMGR_INFO("=== rdkFwupdateMgr Creating thread for listen successful ===\n");

    /* Return handle to caller */
    return (FirmwareInterfaceHandle)handle_str;
}

/*
 * unregisterProcess - Tear down the library and deregister from the daemon.
 *
 * OVERVIEW
 *
 * PURPOSE:
 *   This is the MANDATORY last call before the client process exits.
 *   It is the mirror image of registerProcess(). Where registerProcess()
 *   builds up the machinery (thread, mutexes, registries, D-Bus connection),
 *   this function tears it ALL down in the correct order.
 *
 *   Specifically, it does three things:
 *     1. Shuts down the library's internal async engine (kills the BG thread,
 *        destroys all 4 mutexes, frees all 3 callback registries)
 *     2. Tells the daemon "I'm leaving" via a D-Bus call (best-effort)
 *     3. Frees the handle string that registerProcess() malloc'd
 *
 * WHY THE ORDER MATTERS:
 *   LOCAL cleanup (step 1) happens BEFORE the D-Bus call (step 2).
 *   This is deliberate:
 *     - After we tell the daemon "I'm leaving", it stops sending signals.
 *     - If the BG thread was still alive, it would sit forever in
 *       g_main_loop_run() waiting for signals that never come.
 *     - pthread_join() would block indefinitely.
 *   So we kill the BG thread FIRST, then tell the daemon.
 *   The D-Bus call is best-effort anyway -- if it fails, the daemon
 *   eventually cleans up stale registrations on its own.
 *
 * THREADING MODEL:
 *   - This function runs on the CALLER'S thread (main thread of example_app)
 *   - BEFORE this call: 2 threads (caller's + BG thread)
 *   - AFTER this call:  1 thread (caller's only -- BG thread joined and dead)
 *   - The D-Bus call (step 2) is synchronous, blocks up to 5 seconds
 *
 * D-BUS WIRE PROTOCOL:
 *   Method: "UnregisterProcess"
 *   Input:  GVariant type "(t)" -- one uint64: handler_id
 *   Output: GVariant type "(b)" -- one boolean: success
 *   This creates an EPHEMERAL D-Bus connection (different sender ID from
 *   every other call). The daemon matches by handler_id, not sender.
 *
 * MEMORY CONTRACT:
 *   - This function FREES the handle string (the pointer becomes invalid)
 *   - Caller must NOT use the handle after this call returns
 *   - Caller should set their local copy to NULL as defensive practice
 *
 * RETURN VALUE:
 *   void -- this function always succeeds from the caller's perspective.
 *   All errors are logged but swallowed. Best-effort cleanup.
 *
 * SAFE TO CALL WITH NULL:
 *   Passing NULL is a no-op. This allows the caller to do:
 *     unregisterProcess(handle);   (where handle might be NULL)
 *   without needing a NULL check at every call site.
 *
 * EXECUTION FLOW (step numbers match code comments below):
 *
 *   [1] NULL check -- if NULL, return immediately (no-op)
 *   [2] Parse handle string "1" to uint64 handler_id = 1
 *       Uses strtoull() with strict validation (reject garbage)
 *   [3] internal_system_deinit() -- tear down the async engine:
 *       - g_main_loop_quit() wakes BG thread from g_main_loop_run()
 *       - pthread_join() waits for BG thread to exit
 *       - Free GMainLoop and GMainContext
 *       - Destroy g_bg_thread.mutex
 *       - Free download registry (handle_keys + mutex)
 *       - Free update registry (handle_keys + mutex)
 *       - Free check registry (handle_keys + mutex)
 *       After this: 1 thread, 0 mutexes, 0 D-Bus connections
 *   [4] Create ephemeral D-Bus proxy (best-effort)
 *   [5] Call "UnregisterProcess" on daemon (best-effort, blocks up to 10s)
 *   [6] Extract success boolean from daemon reply
 *   [7] free(handler) -- always, regardless of D-Bus outcome
 *
 * @param handler  The handle returned by registerProcess(). May be NULL.
 *                 After this call returns, this pointer is INVALID (freed).
 *
 * Note: This function is deliberately tolerant of errors. Every failure
 *       path still frees the handle and returns cleanly. The daemon's
 *       ProcessInfo entry may be orphaned if the D-Bus call fails, but
 *       that is the daemon's responsibility to clean up.
 *
 * Warning: Do NOT call this from multiple threads with the same handle.
 *          Do NOT use the handle after this call returns.
 *
 * See also: registerProcess() -- the setup counterpart to this function.
 * See also: internal_system_deinit() -- the async engine teardown.
 */
void unregisterProcess(FirmwareInterfaceHandle handler)
{
    GDBusProxy *proxy = NULL;
    GError *error = NULL;
    GVariant *result = NULL;
    guint64 handler_id = 0;
    gboolean success = FALSE;

    /* NULL handle is a safe no-op */
    if (!handler) {
        FWUPMGR_INFO("unregisterProcess() called with NULL handle (no-op)\n");
        return;
    }

    FWUPMGR_INFO("unregisterProcess() called\n");
    FWUPMGR_INFO("  handle: '%s'\n", handler);

    /* Parse handle string to uint64 handler_id (strict validation) */
    errno = 0;
    char *endptr = NULL;
    handler_id = strtoull(handler, &endptr, 10);

    if (errno != 0) {
        FWUPMGR_ERROR("Invalid handle: numeric overflow/underflow in '%s'\n", handler);
        free(handler);
        return;
    }

    if (endptr == handler) {
        FWUPMGR_ERROR("Invalid handle: no digits found in '%s'\n", handler);
        free(handler);
        return;
    }

    if (*endptr != '\0') {
        FWUPMGR_ERROR("Invalid handle: garbage characters after number in '%s' "
                      "(parsed %" PRIu64 ", but '%s' remains)\n", 
                      handler, handler_id, endptr);
        free(handler);
        return;
    }

    if (handler_id == 0) {
        FWUPMGR_ERROR("Invalid handle: handler_id cannot be 0\n");
        free(handler);
        return;
    }

    FWUPMGR_INFO("  handler_id: %"G_GUINT64_FORMAT"\n", handler_id);

    /* Tear down async engine: kill BG thread, destroy mutexes, free registries */
    FWUPMGR_INFO("=== rdkFwupdateMgr destroy thread unloading ===\n");
    internal_system_deinit();
    FWUPMGR_INFO("=== rdkFwupdateMgr destroy thread ===\n");

    /*Create D-Bus proxy (best-effort -- local cleanup already done) */
    proxy = create_dbus_proxy(&error);
    if (!proxy) {
        FWUPMGR_WARN("Failed to create D-Bus proxy for unregister\n");
        if (error) {
            FWUPMGR_WARN("  Error: %s\n", error->message);
            g_error_free(error);
        }
        free(handler);
        return;
    }

    /* Best-effort D-Bus call: UnregisterProcess(handler_id) */
    FWUPMGR_INFO("Calling UnregisterProcess D-Bus method...\n");
    result = g_dbus_proxy_call_sync(
        proxy,
        "UnregisterProcess",
        g_variant_new("(t)", handler_id),
        G_DBUS_CALL_FLAGS_NONE,
        DBUS_SYNC_TIMEOUT_MS,
        NULL,                       /* GCancellable */
        &error
    );

    if (!result) {
        FWUPMGR_WARN("UnregisterProcess D-Bus call failed: %s\n",
                error ? error->message : "unknown error (GError not set)");
        FWUPMGR_WARN("  (This is OK if daemon already cleaned up)\n");
        if (error) {
            g_error_free(error);
        }
        g_object_unref(proxy);
        free(handler);
        return;
    }

    /* Extract success flag from daemon reply "(b)" */
    g_variant_get(result, "(b)", &success);
    g_variant_unref(result);
    g_object_unref(proxy);

    if (success) {
        FWUPMGR_INFO("Unregistration successful\n");
    } else {
        FWUPMGR_WARN("Daemon reported unregistration failure\n");
        FWUPMGR_WARN("  (Handler may have already been unregistered)\n");
    }

    /* Free the handle string (caller must not use handle after this) */
    free(handler);
    FWUPMGR_INFO("Handle memory freed\n");
}

