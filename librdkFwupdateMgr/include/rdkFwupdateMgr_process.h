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


/* ========================================================================
 * RESULT AND STATUS ENUMERATIONS
 * ======================================================================== */

/**
 * @brief Return value of the checkForUpdate() API call
 *
 * Reflects whether the API call itself succeeded.
 * Does NOT reflect whether firmware is available — that comes via callback.
 */
typedef enum {
    CHECK_FOR_UPDATE_SUCCESS =  0,  /**< Call succeeded; callback WILL fire  */
    CHECK_FOR_UPDATE_FAIL    = -1   /**< Call failed;    callback will NOT fire */
} CheckForUpdateResult;

/**
 * @brief Firmware check outcome — delivered asynchronously via callback
 *
 * Received when the daemon emits the CheckForUpdateComplete D-Bus signal.
 */
typedef enum {
    FIRMWARE_AVAILABLE     = 0,  /**< Update is available for this handler      */
    FIRMWARE_NOT_AVAILABLE = 1,  /**< No update available                       */
    UPDATE_NOT_ALLOWED     = 2,  /**< Device is in the exclusion list           */
    FIRMWARE_CHECK_ERROR   = 3,  /**< Error occurred while checking for updates */
    IGNORE_OPTOUT          = 4,  /**< Firmware download not allowed (opt-out)   */
    BYPASS_OPTOUT          = 5   /**< Firmware download not allowed (bypass)    */
} CheckForUpdateStatus;

/* ========================================================================
 * CALLBACK EVENT DATA
 * ======================================================================== */

/**
 * @brief Firmware update event data delivered to UpdateEventCallback
 *
 * Populated from the CheckForUpdateComplete D-Bus signal payload.
 *
 * MEMORY RULES:
 *   All string pointers are valid ONLY during the callback invocation.
 *   - DO NOT store these pointers after the callback returns.
 *   - DO NOT free any fields (library owns all memory).
 *   - COPY strings with strdup() if you need them after the callback.
 */
typedef struct {
    CheckForUpdateStatus  status;            /**< Firmware check outcome                  */
    const char           *current_version;   /**< Currently running version  (may be NULL) */
    const char           *available_version; /**< Newer version if available (may be NULL) */
    const char           *status_message;    /**< Human-readable detail      (may be NULL) */
    bool                  update_available;  /**< Convenience: true iff FIRMWARE_AVAILABLE */
} FwUpdateEventData;


/* ========================================================================
 * CALLBACK TYPE
 * ======================================================================== */

/**
 * @brief Callback invoked when CheckForUpdateComplete signal is received
 *
 * Fired from the library's background D-Bus signal thread.
 * NOT called from the app's thread.
 *
 * @param handle     Handle that initiated the checkForUpdate() call.
 *                   Identifies which app's check completed.
 *                   Valid only during callback — do not store.
 *
 * @param event_data Firmware check result.
 *                   Valid only during callback — copy fields if needed.
 *
 * CONSTRAINTS:
 *   - Do NOT block, sleep, or perform heavy I/O inside the callback.
 *   - Do NOT call checkForUpdate() from within the callback (deadlock risk).
 *   - Do NOT free handle or any event_data fields.
 *   - Copy strings before returning if you need them later.
 */
typedef void (*UpdateEventCallback)(FirmwareInterfaceHandle handle,
                                    const FwUpdateEventData *event_data);

/* ========================================================================
 * PUBLIC API
 * ======================================================================== */

/**
 * @brief Check for firmware update availability (non-blocking)
 *
 * Registers the callback in the library's internal registry and sends a
 * CheckForUpdate D-Bus method call to the daemon. Returns immediately.
 *
 * The actual firmware check result is delivered asynchronously via
 * UpdateEventCallback when the daemon emits CheckForUpdateComplete.
 *
 * PRECONDITIONS:
 *   - handle must be a valid non-NULL handle from registerProcess()
 *   - callback must not be NULL
 *
 * BEHAVIOR:
 *   SUCCESS path:
 *     1. Callback registered (keyed by handle)
 *     2. D-Bus CheckForUpdate method call sent to daemon
 *     3. Returns CHECK_FOR_UPDATE_SUCCESS immediately
 *     4. [later] Daemon emits signal → library invokes callback
 **   FAILURE path:
 *     - Returns CHECK_FOR_UPDATE_FAIL
 *     - Callback is NOT registered — will NOT fire
 *     - Reason: NULL handle, NULL callback, full registry, D-Bus error
 *
 * MULTI-APP:
 *   Multiple apps call checkForUpdate() with different handles.
 *   Daemon emits ONE CheckForUpdateComplete signal for all.
 *   Library dispatches each app's callback based on its handle.
 *
 * @param handle    Valid handle from registerProcess()
 * @param callback  Function invoked when daemon signals the result
 *
 * @return CHECK_FOR_UPDATE_SUCCESS  or  CHECK_FOR_UPDATE_FAIL
 */
CheckForUpdateResult checkForUpdate(FirmwareInterfaceHandle handle,
                                    UpdateEventCallback callback);

/* ========================================================================
 * DOWNLOAD FIRMWARE — TYPES AND API
 * ========================================================================
 *
 * USAGE FLOW:
 *   1. App already has FirmwareInterfaceHandle from registerProcess()
 *   2. App fills FwDwnlReq with firmware name, optional URL, type
 *   3. App calls downloadFirmware(handle, fwdwnlreq, callback)
 *   4. downloadFirmware returns RDKFW_DWNL_SUCCESS immediately
 *   5. Daemon emits DownloadProgress signals repeatedly (1%...50%...100%)
 *   6. Library fires DownloadCallback for each progress signal
 *   7. App tracks progress and acts on DWNL_COMPLETED or DWNL_ERROR
 * ======================================================================== */

/**
 * @brief Return value of the downloadFirmware() API call itself
 *
 * Reflects whether the download was INITIATED successfully.
 * Does NOT reflect whether the download completed — that comes via callback.
 */
typedef enum {
    RDKFW_DWNL_SUCCESS = 0,   /**< Download initiated; callbacks WILL fire  */
    RDKFW_DWNL_FAILED  = -1   /**< Initiation failed;  callbacks will NOT fire */
} DownloadResult;

/**
 * @brief Download progress status — delivered via DownloadCallback
 *
 * Received on every DownloadProgress D-Bus signal from the daemon.
 * Daemon emits this signal repeatedly as download progresses.
 */
typedef enum {
    DWNL_IN_PROGRESS = 0,  /**< Download is ongoing  (progress_per < 100)  */
    DWNL_COMPLETED   = 1,  /**< Download finished successfully              */
    DWNL_ERROR       = 2   /**< Download failed — do not expect more signals */
} DownloadStatus;

/**
 * @brief Firmware download request parameters
 *
 * Passed by value to downloadFirmware(). Library copies all fields
 * internally before the function returns — caller may free or modify
 * the struct after downloadFirmware() returns.
 *
 * FIELDS:
 *   firmwareName   — filename of the firmware image to download (required)
 *   downloadUrl    — override URL (optional; use "" or NULL to use XConf URL)
 *   TypeOfFirmware — one of: "PCI", "PDRI", "PERIPHERAL"
 */
typedef struct {
    char firmwareName[256];    /**< Firmware filename e.g. "RNGUX_4.5.1_VBN.bin" */
    char downloadUrl[512];     /**< Override URL — empty string = use XConf URL   */
    char TypeOfFirmware[32];   /**< "PCI" | "PDRI" | "PERIPHERAL"                 */
} FwDwnlReq;

/**
 * @brief Callback invoked on every DownloadProgress signal from the daemon
 *
 * Fired from the library's background D-Bus signal thread.
 * Called MULTIPLE TIMES — once per progress update from the daemon.
 *
 * @param progress_per    Download completion percentage (0–100)
 * @param fwdwnlstatus    Current download state
 *
 * CONSTRAINTS:
 *   - Do NOT block or sleep inside the callback.
 *   - Do NOT call downloadFirmware() from inside the callback (deadlock risk).
 *   - Callback fires from background thread — NOT the app's thread.
 *   - When fwdwnlstatus == DWNL_COMPLETED or DWNL_ERROR, no more callbacks
 *     will fire for this download session.
 */
typedef void (*DownloadCallback)(int progress_per, DownloadStatus fwdwnlstatus);

/**
 * @brief Initiate firmware download (non-blocking)
 *
 * Registers the callback and sends a DownloadFirmware D-Bus method call
 * to the daemon. Returns immediately — does NOT block.
 *
 * Progress is delivered asynchronously via DownloadCallback each time
 * the daemon emits a DownloadProgress D-Bus signal.
 *
 * PRECONDITIONS:
 *   - handle must be a valid non-NULL handle from registerProcess()
 *   - fwdwnlreq.firmwareName must be non-empty
 *   - callback must not be NULL
 *
 * MULTI-APP BEHAVIOR:
 *   Multiple apps may call downloadFirmware() concurrently.
 *   Daemon emits ONE DownloadProgress signal for each progress update.
 *   Library dispatches to ALL registered DownloadCallbacks on each signal.
 *   Each callback receives the same progress_per and status.
 *
 * @param handle      Valid handle from registerProcess()
 * @param fwdwnlreq   Download request (firmware name, optional URL, type)
 * @param callback    Invoked on each DownloadProgress signal
 *
 * @return RDKFW_DWNL_SUCCESS  Download initiated; callbacks will fire
 *         RDKFW_DWNL_FAILED   Invalid params, registry full, or D-Bus error
 */
DownloadResult downloadFirmware(FirmwareInterfaceHandle handle,
                                FwDwnlReq fwdwnlreq,
                                DownloadCallback callback);


/* ========================================================================
 * UPDATE FIRMWARE — TYPES AND API
 * ========================================================================
 *
 * USAGE FLOW:
 *   1. App already has FirmwareInterfaceHandle from registerProcess()
 *   2. App fills FwUpdateReq: firmware name, type, optional path, reboot flag
 *   3. App calls updateFirmware(handle, fwupdatereq, callback)
 *   4. updateFirmware returns RDKFW_UPDATE_SUCCESS immediately
 *   5. Daemon emits UpdateProgress signals repeatedly (1%...50%...100%)
 *   6. Library fires UpdateCallback for each progress signal
 *   7. App tracks progress and acts on UPDATE_COMPLETED or UPDATE_ERROR
 *
 * NOTE:
 *   UpdateFirmware flashes (writes) the firmware image to the device.
 *   This is the step AFTER DownloadFirmware completes.
 *   Sequence: checkForUpdate → downloadFirmware → updateFirmware
 * ======================================================================== */

/**
 * @brief Return value of the updateFirmware() API call itself
 *
 * Reflects whether the update was INITIATED successfully.
 * Does NOT reflect whether flashing completed — that comes via callback.
 */
typedef enum {
    RDKFW_UPDATE_SUCCESS = 0,   /**< Update initiated; callbacks WILL fire   */
    RDKFW_UPDATE_FAILED  = -1   /**< Initiation failed; callbacks will NOT fire */
} UpdateResult;

/**
 * @brief Firmware flash progress status — delivered via UpdateCallback
 *
 * Received on every UpdateProgress D-Bus signal from the daemon.
 * Daemon emits this signal repeatedly as flashing progresses.
 */
typedef enum {
    UPDATE_IN_PROGRESS = 0,  /**< Flashing ongoing (progress_per < 100)    */
    UPDATE_COMPLETED   = 1,  /**< Flashing finished successfully            */
    UPDATE_ERROR       = 2   /**< Flashing failed — no more signals expected */
} UpdateStatus;

/**
 * @brief Firmware update request parameters
 *
 * Passed by value to updateFirmware(). Library copies all fields
 * internally — caller may free or modify the struct after the call returns.
 *
 * FIELDS:
 *   firmwareName        — filename of the image to flash (required)
 *   TypeOfFirmware      — "PCI" | "PDRI" | "PERIPHERAL" (required)
 *   LocationOfFirmware  — absolute path to image file (optional;
 *                         empty string = use default path from
 *                         /etc/device.properties)
 *   rebootImmediately   — true = daemon reboots device after flashing
 */
typedef struct {
    char firmwareName[256];        /**< Firmware image filename (required)       */
    char TypeOfFirmware[32];       /**< "PCI" | "PDRI" | "PERIPHERAL" (required) */
    char LocationOfFirmware[512];  /**< Path to image; "" = use device.properties*/
    bool rebootImmediately;        /**< true = reboot after flash completes      */
} FwUpdateReq;

/**
 * @brief Callback invoked on every UpdateProgress signal from the daemon
 *
 * Fired from the library's background D-Bus signal thread.
 * Called MULTIPLE TIMES — once per progress update from the daemon.
 *
 * @param progress_per      Flash completion percentage (0–100)
 * @param fwupdatestatus    Current update state
 *
 * CONSTRAINTS:
 *   - Do NOT block or sleep inside the callback.
 *   - Do NOT call updateFirmware() from inside the callback (deadlock risk).
 *   - Callback fires from background thread — NOT the app's thread.
 *   - When fwupdatestatus == UPDATE_COMPLETED or UPDATE_ERROR, no more
 *     callbacks will fire for this update session.
 */
typedef void (*UpdateCallback)(int progress_per, UpdateStatus fwupdatestatus);

/**
 * @brief Initiate firmware flashing (non-blocking)
 *
 * Registers the callback and sends an UpdateFirmware D-Bus method call
 * to the daemon. Returns immediately — does NOT block.
 *
 * Flash progress is delivered asynchronously via UpdateCallback each time
 * the daemon emits an UpdateProgress D-Bus signal.
 *
 * PRECONDITIONS:
 *   - handle must be a valid non-NULL handle from registerProcess()
 *   - fwupdatereq.firmwareName must be non-empty
 *   - fwupdatereq.TypeOfFirmware must be non-empty
 *   - callback must not be NULL
 *
 * MULTI-APP BEHAVIOR:
 *   Multiple apps may call updateFirmware() concurrently.
 *   Daemon emits ONE UpdateProgress signal per progress step.
 *   Library dispatches to ALL registered UpdateCallbacks on each signal.
 *
 * @param handle        Valid handle from registerProcess()
 * @param fwupdatereq   Update request (name, type, optional path, reboot flag)
 * @param callback      Invoked on each UpdateProgress signal
 *
 * @return RDKFW_UPDATE_SUCCESS  Update initiated; callbacks will fire
 *         RDKFW_UPDATE_FAILED   Invalid params, registry full, or D-Bus error
 */
UpdateResult updateFirmware(FirmwareInterfaceHandle handle,
                             FwUpdateReq fwupdatereq,
                             UpdateCallback callback);
#ifdef __cplusplus
}
#endif

#endif /* RDKFWUPDATEMGR_PROCESS_H */
