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

/* DBUS_TIMEOUT_MS is defined in rdkFwupdateMgr_async_internal.h */

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
 *   - It BLOCKS (synchronous D-Bus call) for up to DBUS_TIMEOUT_MS (5s)
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
    /*
     * LOCAL VARIABLE DECLARATIONS
     *
     * Why declare all at the top?
     *   - Makes it easy to see ALL resources this function manages
     *   - Every non-NULL pointer here must be freed/unreffed on every exit path
     */

    /* GDBusProxy: A GLib object representing a remote D-Bus interface.
     * Think of it as a "remote method caller" -- it knows the bus name,
     * object path, and interface, and can invoke methods on the daemon.
     * Must be freed with g_object_unref() when done. */
    GDBusProxy *proxy = NULL;

    /* GError: GLib's error reporting mechanism. When a GLib function fails,
     * it allocates a GError and fills in a human-readable message + error code.
     * We pass &error to functions, they set it on failure.
     * Must be freed with g_error_free() if non-NULL. */
    GError *error = NULL;

    /* GVariant: GLib's type-safe, immutable, reference-counted value container.
     * D-Bus messages are encoded as GVariants. The daemon's reply will be a
     * GVariant with type "(t)" -- a tuple containing one uint64.
     * Must be freed with g_variant_unref() when done. */
    GVariant *result = NULL;

    /* handler_id: The daemon's response -- a 64-bit unsigned integer that
     * uniquely identifies our registration. The daemon increments a counter
     * (starting at 1) for each new registration. So the first client gets 1,
     * second gets 2, etc. We'll convert this to a string for the handle. */
    guint64 handler_id = 0;

    /* handle_str: The actual string we return to the caller. Heap-allocated,
     * 32 bytes (enough for the decimal representation of any uint64 value,
     * since max uint64 = 18446744073709551615 = 20 digits + null terminator).
     * This pointer IS the FirmwareInterfaceHandle. */
    char *handle_str = NULL;

    /*
     * [STEP 1] LOGGING -- Entry point trace
     *
     * Why log the parameters?
     *   - For field debugging: if something goes wrong later, the log shows
     *     exactly what the client passed in.
     *   - The ternary (?:) guards against NULL dereference in printf.
     *     Without it, printf("%s", NULL) is UNDEFINED BEHAVIOR in C
     *     (crashes on some platforms, prints "(null)" on others).
     */
    FWUPMGR_INFO("registerProcess() called\n");
    FWUPMGR_INFO("  processName: '%s'\n", processName ? processName : "NULL");
    FWUPMGR_INFO("  libVersion:  '%s'\n", libVersion ? libVersion : "NULL");

    /*
     * [STEP 2] VALIDATE processName
     *
     * Why validate here (at the library boundary) instead of letting the
     * daemon validate?
     *   1. Fail fast -- no point creating a D-Bus connection for bad input
     *   2. Saves a round-trip to the daemon (D-Bus call has ~5ms overhead)
     *   3. Defense in depth -- even if daemon validates too, we catch it early
     *   4. Better error messages (we know the context; daemon doesn't know
     *      which client messed up)
     *
     * validate_process_name() checks:
     *   - processName != NULL
     *   - strlen(processName) > 0  (not empty "")
     *   - strlen(processName) <= 256  (MAX_PROCESS_NAME_LEN)
     * Returns false (and logs FWUPMGR_ERROR) if any check fails.
     */
    if (!validate_process_name(processName)) {
        /* Validation failed -- error already logged inside validate_process_name().
         * Return NULL immediately. No resources to clean up (we haven't
         * allocated anything yet). */
        return NULL;
    }

    /*
     * [STEP 3] VALIDATE libVersion
     *
     * validate_lib_version() checks:
     *   - libVersion != NULL  (empty string "" IS allowed -- means "unknown")
     *   - strlen(libVersion) <= 64  (MAX_LIB_VERSION_LEN)
     */
    if (!validate_lib_version(libVersion)) {
        return NULL;
    }

    /*
     * [STEP 4] CREATE D-BUS PROXY (ephemeral connection)
     *
     * What happens inside create_dbus_proxy():
     *   1. g_bus_get_sync(G_BUS_TYPE_SYSTEM) -- connects to the system D-Bus.
     *      This is a BLOCKING call. The D-Bus daemon (dbus-daemon) assigns us
     *      a unique connection name like ":1.140".
     *   2. g_dbus_proxy_new_sync() -- creates a proxy object that can call
     *      methods on "org.rdkfwupdater.Service" at "/org/rdkfwupdater/Service"
     *      on interface "org.rdkfwupdater.Interface".
     *   3. The raw GDBusConnection is unreffed (proxy keeps its own internal ref).
     *
     * Why "ephemeral"?
     *   This connection lives only for THIS function call. After we get the
     *   daemon's response, we destroy the proxy (and its connection) immediately.
     *   Next API call (checkForUpdate, etc.) will create a brand new connection
     *   with a DIFFERENT sender ID. The daemon tracks us by handler_id, not
     *   by D-Bus sender address -- that's why this stateless model works.
     *
     * Common failure causes:
     *   - dbus-daemon not running (container/chroot without D-Bus)
     *   - D-Bus policy denying our connection (security policy file)
     *   - System bus socket not accessible (/var/run/dbus/system_bus_socket)
     */
    proxy = create_dbus_proxy(&error);
    if (!proxy) {
        /* create_dbus_proxy() already logged the specific error.
         * We just need to clean up the GError (if it was allocated).
         *
         * Why check 'error' for NULL?
         *   In extreme OOM conditions, GLib may fail without setting the
         *   GError pointer. Calling g_error_free(NULL) would crash. */
        if (error) {
            g_error_free(error);
        }
        return NULL;
    }

    FWUPMGR_INFO("D-Bus proxy created successfully\n");

    /*
     * [STEP 5] SEND "RegisterProcess" D-BUS METHOD CALL
     *
     * This is the CORE of the function -- the actual IPC with the daemon.
     *
     * g_dbus_proxy_call_sync() does:
     *   1. Marshals our arguments into D-Bus wire format
     *   2. Sends the message to the daemon via dbus-daemon
     *   3. BLOCKS until the daemon replies (or timeout expires)
     *   4. Returns the reply as a GVariant, or NULL + GError on failure
     *
     * Parameters explained:
     *   proxy             -- The proxy we just created (knows where to send)
     *   "RegisterProcess" -- The D-Bus method name on the daemon's interface
     *   g_variant_new("(ss)", processName, libVersion)
     *                     -- The arguments, encoded as a GVariant TUPLE of
     *                        two strings. "(ss)" is the D-Bus TYPE SIGNATURE:
     *                        '(' = start tuple, 's' = string, 's' = string, ')' = end
     *                        GLib takes ownership of this GVariant (don't free it!)
     *   G_DBUS_CALL_FLAGS_NONE -- No special flags (could use NO_AUTO_START
     *                             to prevent D-Bus activation, but we want
     *                             the daemon to auto-start if not running)
     *   DBUS_TIMEOUT_MS   -- 5000 ms (5 seconds). If daemon doesn't reply
     *                         within this time, the call fails with timeout error.
     *   NULL              -- No GCancellable (we can't cancel this operation)
     *   &error            -- Where to store error details on failure
     *
     * WHAT THE DAEMON DOES WHEN IT RECEIVES THIS:
     *   1. Extracts processName and libVersion from the message
     *   2. Calls add_process_to_tracking() which:
     *      a. Checks if any existing registration has the same process_name
     *         -- REJECTS if name is already taken by different client
     *      b. Checks if this client (sender_id) is already registered
     *         -- REJECTS if trying to register a second name
     *         -- Returns existing handler_id if re-registering same name (idempotent)
     *      c. Otherwise: allocates ProcessInfo, assigns next_process_id++,
     *         stores in hash table, returns the new handler_id
     *   3. Sends reply: GVariant "(t)" containing the handler_id (uint64)
     *
     * BLOCKING BEHAVIOR:
     *   Our thread is SUSPENDED here until:
     *   a) Daemon replies (typically <10ms) -- we get 'result'
     *   b) 5-second timeout expires -- we get NULL + timeout GError
     *   c) D-Bus daemon signals an error -- we get NULL + error GError
     */
    FWUPMGR_INFO("Calling RegisterProcess D-Bus method...\n");
    result = g_dbus_proxy_call_sync(
        proxy,
        "RegisterProcess",
        g_variant_new("(ss)", processName, libVersion),
        G_DBUS_CALL_FLAGS_NONE,
        DBUS_TIMEOUT_MS,
        NULL,                       /* GCancellable */
        &error
    );

    if (!result) {
        /* D-Bus call failed. Common causes:
         *   - Daemon not running ("The name was not provided by any .service files")
         *   - Daemon crashed during handling
         *   - 5-second timeout expired (daemon overloaded)
         *   - Daemon explicitly rejected us (process name conflict, D-Bus error reply)
         *
         * RESOURCE CLEANUP:
         *   We must free: error (GError) + proxy (GDBusProxy)
         *   We do NOT have: result, handle_str, handler_id (never obtained) */
        FWUPMGR_ERROR("RegisterProcess D-Bus call failed: %s\n",
                error->message);
        g_error_free(error);
        g_object_unref(proxy);
        return NULL;
    }

    /*
     * [STEP 6] EXTRACT handler_id FROM DAEMON'S REPLY
     *
     * The daemon replied with GVariant type "(t)":
     *   '(' = tuple start
     *   't' = uint64 (guint64 in GLib)
     *   ')' = tuple end
     *
     * g_variant_get() deserializes the GVariant into our C variable.
     * The format string "(t)" must EXACTLY match what the daemon sent,
     * or we get undefined behavior (buffer overread/corruption).
     *
     * After extraction, we free the GVariant (result) and the proxy.
     * At this point:
     *   - We have the handler_id (e.g., 1)
     *   - The D-Bus connection is GONE (proxy unreffed = connection closed)
     *   - The daemon holds a ProcessInfo record for us in its hash table
     *   - No way to communicate with daemon anymore until we create
     *     another connection (which checkForUpdate etc. will do)
     */
    g_variant_get(result, "(t)", &handler_id);
    g_variant_unref(result);    /* Free the GVariant container -- we extracted the value */
    g_object_unref(proxy);      /* Free the proxy -- closes underlying D-Bus connection */

    FWUPMGR_INFO("Registration successful\n");
    FWUPMGR_INFO("  handler_id: %"G_GUINT64_FORMAT"\n", handler_id);

    /*
     * [STEP 7] ALLOCATE HANDLE STRING ON HEAP
     *
     * Why 32 bytes?
     *   - Max uint64 decimal: 18446744073709551615 (20 chars)
     *   - Plus null terminator: 21 bytes needed
     *   - We allocate 32 for alignment and future-proofing
     *
     * Why malloc() and not a struct or stack buffer?
     *   - The handle must OUTLIVE this function (returned to caller)
     *   - Stack allocation would be invalid after return (dangling pointer!)
     *   - A simple string is the simplest possible handle format:
     *     - No struct versioning issues
     *     - Easy to log/print for debugging
     *     - Trivial to pass across thread boundaries
     *     - Can be validated with strtoull() in unregisterProcess()
     *
     * Why (char*) cast?
     *   - malloc() returns void*. In C this is implicitly convertible to
     *     char*, but the cast makes intent clear and satisfies C++ compilers
     *     if this file is ever compiled as C++ (unlikely but defensive).
     */
    handle_str = (char*)malloc(32);  /* Enough for uint64 as decimal string */
    if (!handle_str) {
        /*
         * OOM (Out Of Memory) RECOVERY PATH
         *
         * SITUATION: We SUCCESSFULLY registered with the daemon (it holds
         * a ProcessInfo for us), but we can't allocate 32 bytes of RAM.
         *
         * PROBLEM: If we just return NULL, the daemon keeps our registration
         * forever (resource leak). The client can't call unregisterProcess()
         * because they don't have a handle. Nobody will clean this up.
         *
         * SOLUTION: Best-effort UnregisterProcess call to the daemon.
         * "Best-effort" means: if this cleanup also fails (e.g., because
         * the OOM is so severe we can't even create a D-Bus proxy), we
         * log it and accept the leak. This is an extremely rare edge case.
         *
         * Why create a NEW proxy? Because we already unreffed the old one
         * in Step 6. We can't reuse it.
         */
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
                DBUS_TIMEOUT_MS,
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

    /*
     * [STEP 8] CONVERT handler_id TO DECIMAL STRING
     *
     * snprintf(buffer, size, format, ...):
     *   - Writes at most 'size' bytes (including null terminator)
     *   - GUARANTEED null-terminated (unlike strncpy!)
     *   - Returns number of chars that WOULD have been written (for truncation detection)
     *
     * "%" PRIu64 expands to the platform-correct format specifier for uint64_t.
     * On Linux/GLib: PRIu64 = "lu" (unsigned long), so format = "%lu"
     * On Windows:    PRIu64 = "I64u"
     * Using PRIu64 instead of "%lu" ensures portability.
     *
     * After this line: handle_str = "1" (for the first registration ever)
     *
     * Example values: "1", "2", "42", "18446744073709551615"
     */
    snprintf(handle_str, 32, "%" PRIu64, handler_id);
    FWUPMGR_INFO("Handle created: '%s'\n", handle_str);

    /*
     * [STEP 9] START THE INTERNAL ASYNC ENGINE
     *
     * This is where the library's internal machinery comes to life.
     * BEFORE this call: Only 1 thread exists (the caller's main thread).
     * AFTER this call:  2 threads exist (caller's + library BG thread).
     *
     * internal_system_init() does ALL of the following:
     *
     * Phase A: Initialize Check Callback Registry
     *   memset(&g_registry, 0, sizeof(g_registry));
     *   pthread_mutex_init(&g_registry.mutex, NULL);
     *   g_registry.initialized = true;
     *
     *   Result: 30-slot array, all CB_STATE_IDLE, protected by mutex
     *   Purpose: Will store callbacks for checkForUpdate() later
     *
     * Phase B: Initialize Background Thread Sync
     *   memset(&g_bg_thread, 0, sizeof(g_bg_thread));
     *   pthread_mutex_init(&g_bg_thread.mutex, NULL);
     *   g_bg_thread.running = false;
     *
     *   This mutex protects ONE bool: g_bg_thread.running
     *   Used for startup handshake between main thread and BG thread
     *
     * Phase C: Create ISOLATED GLib Event Loop
     *   g_bg_thread.context   = g_main_context_new();
     *   g_bg_thread.main_loop = g_main_loop_new(context, FALSE);
     *
     *   WHY a new context (not the default)?
     *   If the client app uses GTK or its own GMainLoop, we'd be
     *   injecting our signal handlers into THEIR event loop. Their UI
     *   callbacks and our firmware callbacks would run interleaved on
     *   the same thread -- causing thread-safety bugs. Our own context
     *   guarantees our signals fire ONLY in our BG thread.
     *
     * Phase D: Spawn Background Thread
     *   pthread_create(&g_bg_thread.thread, NULL,
     *                  background_thread_func, NULL);
     *
     *   The BG thread immediately:
     *     1. Pushes our GMainContext as its thread-default
     *     2. Creates a PERSISTENT D-Bus connection (different from ours!)
     *     3. Subscribes to 3 signals:
     *        - CheckForUpdateComplete -> on_check_complete_signal()
     *        - DownloadProgress       -> on_download_progress_signal()
     *        - UpdateProgress         -> on_update_progress_signal()
     *     4. Sets g_bg_thread.running = true (under mutex)
     *     5. Calls g_main_loop_run() -- BLOCKS FOREVER waiting for signals
     *
     * Phase E: Main Thread Spin-Waits for BG Thread Readiness
     *   for (int i = 0; i < 50; i++) {     max 50 x 100ms = 5 seconds
     *       lock(g_bg_thread.mutex);
     *       bool ready = g_bg_thread.running;
     *       unlock(g_bg_thread.mutex);
     *       if (ready) break;
     *       nanosleep(100ms);
     *   }
     *
     *   WHY spin-wait instead of condvar?
     *   Simplicity. This is a one-time startup. A condvar would add
     *   complexity for ~200ms of waiting. Not worth it.
     *
     * Phase F: Initialize Download and Update Registries
     *   memset(&g_dwnl_registry, 0, sizeof(...));
     *   pthread_mutex_init(&g_dwnl_registry.mutex, NULL);
     *   g_dwnl_registry.initialized = true;
     *
     *   memset(&g_update_registry, 0, sizeof(...));
     *   pthread_mutex_init(&g_update_registry.mutex, NULL);
     *   g_update_registry.initialized = true;
     *
     * AFTER internal_system_init() returns successfully:
     *   THREADS:    2 (caller's + BG thread blocked in g_main_loop_run)
     *   MUTEXES:    4 (g_registry.mutex, g_bg_thread.mutex,
     *                   g_dwnl_registry.mutex, g_update_registry.mutex)
     *   REGISTRIES: 3 (all 30 slots IDLE, ready to accept callbacks)
     *   D-BUS:      1 persistent connection in BG thread (for signal reception)
     *
     * If internal_system_init() returns non-zero (failure):
     *   - We log an error but STILL return the handle
     *   - The handle is valid for the daemon, but callbacks won't work
     *   - This is a degraded state (TODO: consider returning NULL here)
     */
    FWUPMGR_INFO("=== rdkFwupdateMgr Creating thread for listen ===\n");
    if (internal_system_init() != 0) {
        FWUPMGR_ERROR("rdkFwupdateMgr_lib_init: internal_system_init FAILED\n");
    }
    FWUPMGR_INFO("=== rdkFwupdateMgr Creating thread for listen successfull ===\n");

    /*
     * [STEP 10] RETURN THE HANDLE TO THE CALLER
     *
     * FirmwareInterfaceHandle is typedef'd as (char*) in the public header.
     * We cast here to make the type explicit, even though char* to char*
     * doesn't technically need a cast. It documents intent.
     *
     * What the caller receives: A pointer to a heap-allocated string like "1".
     *
     * STATE OF THE WORLD after this return:
     *
     *   Caller's process:
     *     Main thread: running (has the handle, about to call checkForUpdate etc.)
     *     BG thread: BLOCKED in g_main_loop_run(), waiting for D-Bus signals
     *       Owns persistent D-Bus connection (e.g., :1.141)
     *       Subscribed to CheckForUpdateComplete, DownloadProgress, UpdateProgress
     *
     *   Daemon process:
     *     registered_processes hash table contains:
     *       key=1 -> ProcessInfo { handler_id=1, process_name="example_plugin",
     *                              lib_version="1.0.0", sender_id=":1.140" }
     *
     *   D-Bus connections:
     *     :1.140 -- DEAD (was our ephemeral connection, already closed)
     *     :1.141 -- ALIVE (BG thread's persistent connection for signals)
     */
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
 *   [5] Call "UnregisterProcess" on daemon (best-effort, blocks up to 5s)
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
    /*
     * LOCAL VARIABLE DECLARATIONS
     *
     * Same pattern as registerProcess(): declare all at the top so you
     * can see every resource this function manages at a glance.
     */

    /* GDBusProxy for the D-Bus call to the daemon.
     * Created in step 4, freed with g_object_unref() in step 6.
     * May be NULL if proxy creation fails (D-Bus down, daemon gone). */
    GDBusProxy *proxy = NULL;

    /* GError for capturing GLib/D-Bus error details.
     * Must be freed with g_error_free() if non-NULL. */
    GError *error = NULL;

    /* GVariant holding the daemon's reply to UnregisterProcess.
     * Type "(b)" -- a tuple containing one boolean (success/failure).
     * Must be freed with g_variant_unref() when done. */
    GVariant *result = NULL;

    /* The numeric handler_id we'll extract from the handle string.
     * This is what the daemon uses to look up our ProcessInfo entry.
     * Parsed from the handle string "1" -> handler_id = 1. */
    guint64 handler_id = 0;

    /* The daemon's reply: TRUE if it found and removed our registration,
     * FALSE if handler_id was not found (already removed, or invalid). */
    gboolean success = FALSE;

    /*
     * [STEP 1] NULL CHECK -- safe no-op for NULL handles
     *
     * Why allow NULL?
     *   Client code often has cleanup paths like:
     *     cleanup:
     *         unregisterProcess(handle);
     *   If registerProcess() failed, handle is NULL. Making this a safe
     *   no-op avoids the caller needing "if (handle != NULL)" every time.
     *
     * No resources to clean up here -- we haven't allocated anything.
     */
    if (!handler) {
        FWUPMGR_INFO("unregisterProcess() called with NULL handle (no-op)\n");
        return;
    }

    FWUPMGR_INFO("unregisterProcess() called\n");
    FWUPMGR_INFO("  handle: '%s'\n", handler);

    /*
     * [STEP 2] PARSE THE HANDLE STRING TO uint64
     *
     * The handle is a string like "1" or "42". We need to convert it
     * back to a uint64 handler_id for the D-Bus call to the daemon.
     *
     * Why strtoull() instead of atoi() or sscanf()?
     *   - atoi() has NO error detection. atoi("abc") returns 0 silently.
     *   - sscanf() doesn't detect trailing garbage: sscanf("123abc", "%llu")
     *     happily returns 123 and ignores "abc".
     *   - strtoull() with endptr checking is the ONLY way in C to do
     *     strict numeric parsing:
     *     - Sets errno on overflow
     *     - endptr tells you exactly where parsing stopped
     *     - You can reject partial parses ("123abc") by checking *endptr
     *
     * Why set errno = 0 before the call?
     *   strtoull() only sets errno on error. If errno was already non-zero
     *   from some earlier unrelated call, we'd get a false positive.
     *   Always clear errno before calling strto* functions.
     *
     * What is endptr?
     *   After strtoull("123abc", &endptr, 10):
     *     - Return value = 123
     *     - endptr points to 'a' (first character it couldn't parse)
     *   After strtoull("123", &endptr, 10):
     *     - Return value = 123
     *     - endptr points to '\0' (end of string -- everything parsed)
     *   After strtoull("abc", &endptr, 10):
     *     - Return value = 0
     *     - endptr == handler (didn't move -- nothing parsed)
     */
    errno = 0;
    char *endptr = NULL;
    handler_id = strtoull(handler, &endptr, 10);

    /*
     * VALIDATION CHECK 1: Numeric overflow
     *
     * If the string represents a number larger than ULLONG_MAX
     * (18446744073709551615), strtoull() returns ULLONG_MAX and sets
     * errno to ERANGE. This catches "99999999999999999999999".
     */
    if (errno != 0) {
        FWUPMGR_ERROR("Invalid handle: numeric overflow/underflow in '%s'\n", handler);
        free(handler);
        return;
    }

    /*
     * VALIDATION CHECK 2: No digits at all
     *
     * If endptr == handler, strtoull() didn't find any digits.
     * This catches "abc", "", and other non-numeric strings.
     *
     * Why is this separate from the *endptr check below?
     *   strtoull("abc") returns 0 with endptr pointing to 'a'.
     *   *endptr != '\0' would also catch it, but "endptr == handler"
     *   gives a more specific error message: "no digits found"
     *   vs "garbage after number".
     */
    if (endptr == handler) {
        FWUPMGR_ERROR("Invalid handle: no digits found in '%s'\n", handler);
        free(handler);
        return;
    }

    /*
     * VALIDATION CHECK 3: Trailing garbage after the number
     *
     * If *endptr is not the null terminator, there are characters
     * after the valid number. This catches:
     *   "123abc"  (endptr points to 'a')
     *   "123 "    (endptr points to ' ')
     *   " 123"    (strtoull skips leading whitespace, so this actually
     *              parses as 123 with endptr at '\0' -- PASSES this check.
     *              But our registerProcess() never creates handles with
     *              leading spaces, so this is academic.)
     *
     * Why is this important?
     *   If the handle string is corrupted (memory corruption elsewhere),
     *   it might look like "1\x03garbage". We don't want to silently
     *   parse it as handler_id=1 and proceed -- that could mask a bug.
     */
    if (*endptr != '\0') {
        FWUPMGR_ERROR("Invalid handle: garbage characters after number in '%s' "
                      "(parsed %" PRIu64 ", but '%s' remains)\n", 
                      handler, handler_id, endptr);
        free(handler);
        return;
    }

    /*
     * VALIDATION CHECK 4: handler_id must be > 0
     *
     * The daemon assigns handler_ids starting at 1 (next_process_id = 1,
     * post-increment). A handler_id of 0 is NEVER valid. If we got 0,
     * either the string was literally "0" or something went wrong.
     *
     * The daemon also rejects handler_id == 0 on its side (returns
     * D-Bus error), but we catch it here to avoid a wasted round-trip.
     */
    if (handler_id == 0) {
        FWUPMGR_ERROR("Invalid handle: handler_id cannot be 0\n");
        free(handler);
        return;
    }

    /*
     * NOTE ON free(handler) IN ALL ERROR PATHS ABOVE:
     *
     * Every validation failure path calls free(handler) before returning.
     * This is critical: the handle string was malloc'd by registerProcess().
     * If we return without freeing it, that's a memory leak. The caller
     * will likely set their pointer to NULL after this call, so nobody
     * else will free it.
     *
     * Also note: internal_system_deinit() is NOT called in these paths.
     * If the handle is corrupt, we don't know what state the system is in.
     * The BG thread and mutexes leak, but that's acceptable -- handle
     * corruption means something catastrophic happened, and the OS will
     * reclaim everything when the process exits anyway.
     */

    FWUPMGR_INFO("  handler_id: %"G_GUINT64_FORMAT"\n", handler_id);

    /*
     * [STEP 3] TEAR DOWN THE INTERNAL ASYNC ENGINE
     *
     * This is the most critical step. We tear down EVERYTHING that
     * internal_system_init() created during registerProcess().
     *
     * BEFORE this call:
     *   Threads:    2 (main + BG)
     *   Mutexes:    4 (g_registry, g_bg_thread, g_dwnl_registry, g_update_registry)
     *   Registries: 3 (check, download, update -- all 30 slots each)
     *   D-Bus:      1 persistent connection in BG thread (:1.141)
     *   GLib:       1 GMainLoop + 1 GMainContext (owned by BG thread)
     *
     * AFTER this call:
     *   Threads:    1 (main only -- BG thread joined and dead)
     *   Mutexes:    0 (all 4 destroyed)
     *   Registries: 3 (handle_key strings freed, but struct memory is static)
     *   D-Bus:      0 (BG thread closed its connection during cleanup)
     *   GLib:       0 (main_loop and context unref'd)
     *
     * internal_system_deinit() does the following, in this exact order:
     *
     * 1. g_main_loop_quit(g_bg_thread.main_loop)
     *    Sends a "quit" signal to the GLib event loop that the BG thread
     *    is blocking in. This is THREAD-SAFE -- GLib explicitly allows
     *    calling quit from a different thread than the one running the loop.
     *    Internally, GLib writes to a wakeup pipe/eventfd.
     *
     *    When quit fires, the BG thread's g_main_loop_run() returns.
     *    The BG thread then:
     *      a. Unsubscribes from all D-Bus signals (no more callbacks)
     *      b. g_object_unref(connection) -- closes :1.141
     *      c. g_main_context_pop_thread_default() -- detaches context
     *      d. return NULL -- pthread exits
     *
     * 2. pthread_join(g_bg_thread.thread, NULL)
     *    BLOCKS the main thread until the BG thread has fully exited.
     *    After this returns:
     *      - The BG thread is DEAD (its stack is freed by the OS)
     *      - No more signal callbacks can fire
     *      - No more mutex contention on registries
     *      - It is safe to destroy mutexes
     *
     *    WHY is pthread_join essential?
     *    If we skipped it and went straight to mutex_destroy, the BG
     *    thread might still be holding g_registry.mutex while dispatching
     *    a late-arriving signal. pthread_mutex_destroy on a locked mutex
     *    is UNDEFINED BEHAVIOR (potential crash or silent corruption).
     *
     * 3. g_main_loop_unref() + g_main_context_unref()
     *    Free the GLib event loop objects. The BG thread already popped
     *    the context, so these are the final references.
     *
     * 4. pthread_mutex_destroy(&g_bg_thread.mutex)
     *    Destroy the mutex that protected g_bg_thread.running.
     *    Nobody uses it anymore -- we just joined the only other thread.
     *    Mutexes remaining: 3
     *
     * 5. internal_dwnl_system_deinit()
     *    Lock g_dwnl_registry.mutex, iterate all 30 download slots,
     *    free() any non-NULL handle_key strings (from strdup during
     *    downloadFirmware calls), unlock, then destroy the mutex.
     *    Mutexes remaining: 2
     *
     *    Why lock even though only 1 thread exists? Defensive coding.
     *    If someone refactors and this runs while threads are alive,
     *    the lock prevents a race.
     *
     * 6. internal_update_system_deinit()
     *    Same pattern as download. Lock, free handle_keys, destroy mutex.
     *    Mutexes remaining: 1
     *
     * 7. Lock g_registry.mutex, free all check-registry handle_keys,
     *    unlock, destroy mutex.
     *    Mutexes remaining: 0
     *
     * After internal_system_deinit() returns, the library is in a
     * "dormant" state: no threads, no mutexes, no D-Bus connections.
     * Only the handle string and handler_id still exist.
     */
    FWUPMGR_INFO("=== rdkFwupdateMgr destroy thred unloading ===\n");
    internal_system_deinit();
    FWUPMGR_INFO("=== rdkFwupdateMgr destory thread ===\n");

    /*
     * [STEP 4] CREATE D-BUS PROXY (best-effort)
     *
     * Same as registerProcess() -- create_dbus_proxy() opens a NEW
     * ephemeral D-Bus connection, gets a new unique sender name
     * (e.g., :1.145), and creates a GDBusProxy for method calls.
     *
     * Why "best-effort"?
     *   If the daemon has crashed, the D-Bus bus is down, or the system
     *   bus socket is inaccessible, proxy creation fails. That's OK.
     *   The important cleanup (BG thread, mutexes, memory) was already
     *   done in step 3. The D-Bus call is just a courtesy to the daemon.
     *
     * Why FWUPMGR_WARN and not FWUPMGR_ERROR?
     *   WARN means "something unexpected happened but we can continue."
     *   ERROR means "we're returning a failure code to the caller."
     *   Since unregisterProcess() is void and always succeeds from the
     *   caller's perspective, failures here are warnings, not errors.
     *
     * Note: free(handler) is called even when proxy creation fails.
     *   The handle MUST be freed on every path. No exceptions.
     */
    proxy = create_dbus_proxy(&error);
    if (!proxy) {
        FWUPMGR_WARN("Failed to create D-Bus proxy for unregister\n");
        if (error) {
            FWUPMGR_WARN("  Error: %s\n", error->message);
            g_error_free(error);
        }
        /* Continue with cleanup even if D-Bus call fails */
        free(handler);
        return;
    }

    /*
     * [STEP 5] SEND "UnregisterProcess" D-BUS METHOD CALL
     *
     * g_dbus_proxy_call_sync() -- same as in registerProcess(), but
     * with a different method name and argument type.
     *
     * Wire format:
     *   Method name: "UnregisterProcess"
     *   Arguments:   g_variant_new("(t)", handler_id)
     *     "(t)" = a tuple containing one uint64
     *     We send handler_id = 1
     *
     * Expected reply:
     *   "(b)" = a tuple containing one boolean
     *   TRUE  = daemon found the registration and removed it
     *   FALSE = daemon didn't find handler_id (already removed, or unknown)
     *
     * BLOCKING BEHAVIOR:
     *   Main thread blocks here for up to DBUS_TIMEOUT_MS (5 seconds).
     *   Typical response time: <5ms (just a hash table lookup + remove).
     *
     * WHAT THE DAEMON DOES:
     *   1. Extracts handler_id from the message: g_variant_get("(t)", &handler)
     *   2. Validates handler != 0 (rejects with D-Bus error if 0)
     *   3. Looks up ProcessInfo in registered_processes hash table
     *   4. Calls remove_process_from_tracking(handler_id):
     *      - g_hash_table_lookup(registered_processes, handler_id)
     *      - If found: g_hash_table_remove() which also calls g_free()
     *        on the ProcessInfo struct (freeing process_name, lib_version,
     *        sender_id strings that were g_strdup'd during registration)
     *      - Returns TRUE if found and removed, FALSE if not found
     *   5. Sends reply: g_variant_new("(b)", TRUE/FALSE)
     *
     * NOTE: The daemon ignores the sender_id of this call.
     *   Our registerProcess was sent from :1.140, but this unregister
     *   comes from :1.145 (different ephemeral connection). The daemon
     *   matches ONLY by handler_id. The sender_id parameter in
     *   remove_process_from_tracking() is unused (cast to void).
     *
     * FAILURE HANDLING:
     *   If this call fails (timeout, daemon crashed, bus error):
     *   - Log a WARNING (not error -- best-effort)
     *   - Still free the proxy, still free the handle
     *   - Return -- caller's perspective: unregister succeeded
     *   The daemon will eventually clean up its stale ProcessInfo entry
     *   through its own periodic cleanup or on next daemon restart.
     */
    FWUPMGR_INFO("Calling UnregisterProcess D-Bus method...\n");
    result = g_dbus_proxy_call_sync(
        proxy,
        "UnregisterProcess",
        g_variant_new("(t)", handler_id),
        G_DBUS_CALL_FLAGS_NONE,
        DBUS_TIMEOUT_MS,
        NULL,                       /* GCancellable */
        &error
    );

    if (!result) {
        /*
         * D-Bus call failed. Common causes:
         *   - Daemon not running (crashed or was stopped)
         *   - 5-second timeout expired
         *   - D-Bus bus itself restarted
         *
         * This is logged as WARN, not ERROR, because:
         *   1. unregisterProcess() is void -- no error code to return
         *   2. The important cleanup (thread, mutexes) already succeeded
         *   3. The daemon's stale entry is the daemon's problem, not ours
         *
         * "This is OK if daemon already cleaned up" -- if the daemon
         * crashed, it already lost all its ProcessInfo entries. When it
         * restarts, it starts fresh. Our registration is already gone.
         */
        FWUPMGR_WARN("UnregisterProcess D-Bus call failed: %s\n",
                error->message);
        FWUPMGR_WARN("  (This is OK if daemon already cleaned up)\n");
        g_error_free(error);
        g_object_unref(proxy);
        /* Continue with local cleanup */
        free(handler);
        return;
    }

    /*
     * [STEP 6] EXTRACT THE SUCCESS FLAG FROM DAEMON'S REPLY
     *
     * The daemon replied with GVariant type "(b)":
     *   '(' = tuple start
     *   'b' = gboolean (TRUE or FALSE)
     *   ')' = tuple end
     *
     * g_variant_get() deserializes into our 'success' variable.
     *
     * Then we free the GVariant reply and the proxy. The ephemeral
     * D-Bus connection :1.145 is now closed.
     *
     * Possible values:
     *   success == TRUE:  Daemon found handler_id=1, removed ProcessInfo,
     *                     freed process_name/lib_version/sender_id strings.
     *                     registered_processes is now empty (0 entries).
     *
     *   success == FALSE: Daemon didn't find handler_id=1. This can happen if:
     *                     - We already unregistered (double call)
     *                     - Daemon restarted and lost its in-memory state
     *                     - handler_id was somehow wrong
     *                     We log a warning but don't treat it as fatal.
     */
    g_variant_get(result, "(b)", &success);
    g_variant_unref(result);
    g_object_unref(proxy);

    if (success) {
        FWUPMGR_INFO("Unregistration successful\n");
    } else {
        FWUPMGR_WARN("Daemon reported unregistration failure\n");
        FWUPMGR_WARN("  (Handler may have already been unregistered)\n");
    }

    /*
     * [STEP 7] FREE THE HANDLE STRING
     *
     * This frees the 32-byte malloc'd string that registerProcess()
     * created in its Step 7 (e.g., the string "1").
     *
     * After this line, the 'handler' pointer is INVALID. Dereferencing
     * it is undefined behavior (use-after-free). The caller must NOT
     * use the handle after unregisterProcess() returns.
     *
     * Good practice in the caller:
     *   unregisterProcess(g_handle);
     *   g_handle = NULL;    // prevent accidental use-after-free
     *
     * This free() happens on EVERY code path:
     *   - Normal success path (here)
     *   - D-Bus call failure (step 5 failure branch)
     *   - Proxy creation failure (step 4 failure branch)
     *   - Handle parse failure (step 2 failure branches)
     *   The ONLY path that doesn't free is the NULL check (step 1),
     *   because there's nothing to free.
     *
     * STATE OF THE WORLD AFTER THIS RETURNS:
     *
     *   example_app process:
     *     Threads:    1 (main thread only)
     *     BG thread:  DEAD (joined in step 3)
     *     Mutexes:    0 (all 4 destroyed in step 3)
     *     Registries: wiped (handle_keys freed)
     *     D-Bus:      0 connections (all closed)
     *     Handle:     FREED and INVALID
     *
     *   Daemon process:
     *     registered_processes: empty (ProcessInfo for "example_plugin" removed)
     *     next_process_id: 2 (monotonically increasing, never resets)
     *
     *   D-Bus connections (all dead):
     *     :1.140 -- was registerProcess ephemeral (dead since registration)
     *     :1.141 -- was BG thread persistent (closed in step 3)
     *     :1.145 -- was this unregister ephemeral (just closed above)
     *
     *   The library is back to "UNLINKED" state. If needed, the caller
     *   could call registerProcess() again to start a new session.
     *   The daemon would assign handler_id=2 this time.
     */
    free(handler);
    FWUPMGR_INFO("Handle memory freed\n");
}

