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
 * @file rdkFwupdateMgr_process.h
 * @brief Process registration API for firmware update clients
 *
 * This header defines the process registration/unregistration APIs that
 * plugin implementation teams use to communicate with the rdkFwupdateMgr daemon.
 *
 * USAGE PATTERN:
 * ===============
 * 1. Call registerProcess() at plugin initialization
 * 2. Save the returned handle for all subsequent API calls
 * 3. Use handle for CheckForUpdate, DownloadFirmware, UpdateFirmware APIs
 * 4. Call unregisterProcess() at plugin cleanup/shutdown
 *
 * EXAMPLE:
 * ========
 * @code
 *   // At plugin initialization
 *   FirmwareInterfaceHandle handle = registerProcess("VideoApp", "1.0.0");
 *   if (handle == NULL) {
 *       // Handle error - registration failed
 *       return ERROR;
 *   }
 *
 *   // Use handle for firmware operations
 *   checkForUpdate(handle, ...);
 *   downloadFirmware(handle, ...);
 *
 *   // At plugin shutdown
 *   unregisterProcess(handle);
 *   handle = NULL;  // Mark as invalid
 * @endcode
 *
 * THREAD SAFETY:
 * ==============
 * - registerProcess() and unregisterProcess() are NOT thread-safe individually
 * - Each thread that needs firmware APIs MUST call registerProcess() with a unique process name
 * - DO NOT call unregisterProcess() from multiple threads with the same handle
 * - The returned handle can be used from any thread (D-Bus handles thread dispatch)
 *
 * MEMORY MANAGEMENT:
 * ==================
 * - The library owns all returned FirmwareInterfaceHandle strings
 * - DO NOT call free() on FirmwareInterfaceHandle
 * - Handle becomes invalid after unregisterProcess() - set to NULL to avoid use-after-free
 * - Library automatically cleans up on daemon disconnect
 *
 * ABI STABILITY:
 * ==============
 * - Opaque handle design (char*) provides ABI stability
 * - Internal representation can change without breaking binary compatibility
 * - DO NOT cast or inspect handle internals
 */

#ifndef RDKFWUPDATEMGR_PROCESS_H
#define RDKFWUPDATEMGR_PROCESS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * TYPE DEFINITIONS
 * ======================================================================== */

/**
 * @brief Opaque handle for firmware interface registration
 *
 * This handle represents a registered client session with the firmware
 * update daemon. It's essentially a string-encoded handler ID that the
 * daemon assigns during registration.
 *
 * IMPLEMENTATION NOTES:
 * - Internally: String representation of uint64 handler_id (e.g., "12345")
 * - Externally: Opaque pointer (plugins don't know internal format)
 * - Lifetime: Valid from registerProcess() until unregisterProcess()
 * - Ownership: Library-managed (don't free)
 *
 * DESIGN RATIONALE:
 * - char* provides ABI stability across library versions
 * - String encoding allows easy logging/debugging
 * - Opaque type prevents plugins from inspecting internals
 * - Daemon can change ID format without breaking clients
 */
typedef char* FirmwareInterfaceHandle;

/**
 * @brief Invalid handle constant
 *
 * This value indicates registration failure or an invalid handle state.
 * Check return values against this constant.
 *
 * Example:
 * @code
 *   FirmwareInterfaceHandle handle = registerProcess(...);
 *   if (handle == FIRMWARE_INVALID_HANDLE) {
 *       // Handle error
 *   }
 * @endcode
 */
#define FIRMWARE_INVALID_HANDLE ((FirmwareInterfaceHandle)NULL)

/* ========================================================================
 * PUBLIC API - PROCESS REGISTRATION
 * ======================================================================== */

/**
 * @brief Register a process with the firmware update daemon
 *
 * Establishes a connection to the rdkFwupdateMgr daemon via D-Bus and
 * registers the calling process. The returned handle must be used for
 * all subsequent firmware API calls.
 *
 * BEHAVIOR:
 * - Connects to D-Bus system bus (org.rdkfwupdater.Interface)
 * - Sends RegisterProcess D-Bus method with processName + libVersion
 * - Daemon validates uniqueness (one registration per process name)
 * - Returns valid handle on success, NULL on failure
 *
 * REGISTRATION RULES:
 * - Each process name can only register once system-wide
 * - Same D-Bus connection cannot register multiple times (unless different process names)
 * - If registration fails, retry is NOT automatic (caller must handle)
 * - Process name should be descriptive and unique (e.g., "VideoApp", "AudioService")
 *
 * ERROR CONDITIONS:
 * - Returns NULL if:
 *   * processName is NULL or empty
 *   * libVersion is NULL (empty string is allowed)
 *   * D-Bus connection fails
 *   * Daemon rejects registration (duplicate process name, access denied)
 *   * Out of memory
 *
 * THREAD SAFETY:
 * - NOT thread-safe: Do not call concurrently from multiple threads
 * - Best practice: Call once at plugin initialization from main thread
 * - If multi-threaded access needed: Use different process names per thread
 *
 * MEMORY OWNERSHIP:
 * - processName: Caller-owned, copied by library (can free after return)
 * - libVersion: Caller-owned, copied by library (can free after return)
 * - Return handle: Library-owned, DO NOT FREE, valid until unregisterProcess()
 *
 * @param processName Unique process identifier (e.g., "VideoApp", "EPGService")
 *                    Must be non-NULL, non-empty, and unique system-wide.
 *                    Recommended: Use component name from your plugin.
 *                    Max length: 256 characters (daemon enforces)
 *
 * @param libVersion  Library version string (e.g., "1.0.0", "2.1.5-beta")
 *                    Must be non-NULL (can be empty string "")
 *                    Used for daemon logging/debugging, not validated
 *                    Max length: 64 characters (daemon enforces)
 *
 * @return Valid FirmwareInterfaceHandle on success
 *         FIRMWARE_INVALID_HANDLE (NULL) on failure
 *
 * @note Call unregisterProcess() before plugin unload to clean up resources
 * @note Handle remains valid even if daemon restarts (auto-reconnect not implemented)
 * @note Daemon tracks registration by D-Bus unique name (e.g., ":1.42")
 *
 * @see unregisterProcess()
 *
 * EXAMPLE USAGE:
 * @code
 *   // Good: Unique process name, proper error handling
 *   FirmwareInterfaceHandle handle = registerProcess("VideoPlayer", "2.0.1");
 *   if (handle == FIRMWARE_INVALID_HANDLE) {
 *       fprintf(stderr, "Failed to register with firmware daemon\n");
 *       return ERROR_INIT_FAILED;
 *   }
 *   // ... use handle for firmware operations ...
 *   unregisterProcess(handle);
 *
 *   // Bad: Empty process name (will fail)
 *   FirmwareInterfaceHandle bad = registerProcess("", "1.0");  // Returns NULL
 *
 *   // Bad: NULL process name (will fail)
 *   FirmwareInterfaceHandle bad2 = registerProcess(NULL, "1.0");  // Returns NULL
 *
 *   // OK: Empty version string (daemon accepts)
 *   FirmwareInterfaceHandle ok = registerProcess("AudioService", "");
 * @endcode
 */
FirmwareInterfaceHandle registerProcess(const char *processName, const char *libVersion);

/**
 * @brief Unregister a previously registered process
 *
 * Disconnects from the firmware update daemon and releases all resources
 * associated with the provided handle.
 *
 * BEHAVIOR:
 * - Sends UnregisterProcess D-Bus method to daemon
 * - Daemon removes process from tracking and frees server-side resources
 * - Handle becomes INVALID after this call (set to NULL)
 * - Idempotent: Safe to call multiple times (subsequent calls are no-op)
 *
 * CLEANUP RULES:
 * - MUST call before plugin unload/shutdown
 * - Safe to call with FIRMWARE_INVALID_HANDLE (NULL) - no-op
 * - After unregister, all firmware APIs with this handle will fail
 * - DO NOT use handle after unregister (undefined behavior)
 *
 * ERROR CONDITIONS:
 * - Silently succeeds if:
 *   * handler is NULL/invalid (already unregistered)
 *   * Daemon connection lost (local cleanup still happens)
 *   * Daemon already unregistered this handler
 * - No error return because:
 *   * Cleanup is best-effort (daemon may already be gone)
 *   * Caller shouldn't retry on failure (cleanup is final)
 *   * Always safe to proceed with plugin shutdown
 *
 * THREAD SAFETY:
 * - NOT thread-safe: Do not call concurrently with same handle
 * - Safe to call from different thread than registerProcess()
 * - Best practice: Call once at plugin cleanup from main thread
 *
 * MEMORY OWNERSHIP:
 * - handler: Library-owned, automatically freed during unregister
 * - After return: Caller MUST set handle pointer to NULL
 * - DO NOT call free() on handler yourself
 *
 * @param handler Handle returned by registerProcess()
 *                Can be NULL (no-op in that case)
 *                Must not be used after this call
 *
 * @return void (no error reporting - best-effort cleanup)
 *
 * @note Always call this before plugin unload, even if firmware APIs weren't used
 * @note Sets internal state to unregistered (safe to call multiple times)
 * @note Daemon side cleanup happens even if D-Bus call fails
 *
 * @see registerProcess()
 *
 * EXAMPLE USAGE:
 * @code
 *   // Typical usage: Register -> Use -> Unregister
 *   FirmwareInterfaceHandle handle = registerProcess("EPGService", "1.5");
 *   if (handle != FIRMWARE_INVALID_HANDLE) {
 *       // ... use handle for firmware operations ...
 *       unregisterProcess(handle);
 *       handle = NULL;  // Good practice: Mark as invalid
 *   }
 *
 *   // Safe: Unregister NULL handle (no-op)
 *   FirmwareInterfaceHandle null_handle = NULL;
 *   unregisterProcess(null_handle);  // Does nothing, safe
 *
 *   // Safe: Unregister twice (second call is no-op)
 *   FirmwareInterfaceHandle handle2 = registerProcess("AudioService", "1.0");
 *   unregisterProcess(handle2);
 *   unregisterProcess(handle2);  // No-op, safe
 *   handle2 = NULL;
 *
 *   // Cleanup pattern in destructor/atexit handler
 *   void cleanup() {
 *       if (g_fw_handle != NULL) {
 *           unregisterProcess(g_fw_handle);
 *           g_fw_handle = NULL;
 *       }
 *   }
 * @endcode
 */
void unregisterProcess(FirmwareInterfaceHandle handler);

/* ========================================================================
 * IMPLEMENTATION NOTES (DO NOT USE DIRECTLY)
 * ======================================================================== */

/**
 * @brief Implementation detail: Stateless design
 *
 * FirmwareInterfaceHandle is simply a string representation of the handler_id.
 * 
 * NO PERSISTENT STATE IS MAINTAINED:
 * - No D-Bus connection stored
 * - No D-Bus proxy stored
 * - No internal context structure
 * 
 * ACTUAL IMPLEMENTATION:
 * - FirmwareInterfaceHandle = malloc'd string (e.g., "12345")
 * - Each API call creates fresh D-Bus proxy
 * - D-Bus proxy is destroyed immediately after use
 * - Only the handler_id string persists
 * 
 * RATIONALE FOR STATELESS DESIGN:
 * - Registration APIs are infrequent (once per plugin lifetime)
 * - No performance benefit from caching D-Bus connection
 * - Simpler lifecycle management (no stale connections)
 * - Thread-safe by design (no shared state)
 * - Reconnect-friendly (daemon restart doesn't break handle)
 * 
 * MEMORY FOOTPRINT:
 * - Handle: 32 bytes (string buffer)
 * - Per API call: ~100 bytes transient (proxy created/destroyed)
 * - Total persistent: 32 bytes
 * 
 * @internal
 */

#ifdef __cplusplus
}
#endif

#endif /* RDKFWUPDATEMGR_PROCESS_H */
