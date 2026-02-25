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

#ifndef RDKFWUPDATEMGR_CLIENT_H
#define RDKFWUPDATEMGR_CLIENT_H

#include <stdint.h>
#include <stdbool.h>

// Include process registration API
#include "rdkFwupdateMgr_process.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * IMPORTANT: PROCESS REGISTRATION REQUIRED
 * ======================================================================== */

/**
 * BEFORE using any firmware APIs in this header, you MUST:
 * 
 * 1. Call registerProcess() to get a FirmwareInterfaceHandle
 * 2. Use that handle for all subsequent API calls
 * 3. Call unregisterProcess() when done
 * 
 * See rdkFwupdateMgr_process.h for details on:
 *   - registerProcess()
 *   - unregisterProcess()
 *   - FirmwareInterfaceHandle
 * 
 * Example:
 *   FirmwareInterfaceHandle handle = registerProcess("MyPlugin", "1.0");
 *   if (handle == NULL) {
 *       // Handle error
 *   }
 *   checkForUpdate(handle, ...);
 *   unregisterProcess(handle);
 */

/* ========================================================================
 * HANDLE TYPE (defined in rdkFwupdateMgr_process.h)
 * ======================================================================== */

/**
 * FirmwareInterfaceHandle
 * 
 * This is a string ID that the daemon gives you when you register.
 * Think of it like a session ID or ticket number (e.g., "12345").
 * 
 * You get this from registerProcess() and use it for all other API calls.
 * The library owns this string - don't free() it yourself.
 * It becomes invalid after you call unregisterProcess().
 */
typedef char* FirmwareInterfaceHandle;


/* ========================================================================
 * STATUS ENUMS
 * ======================================================================== */

/**
 * CheckForUpdateStatus
 * 
 * What happened when we checked for updates?
 */
typedef enum {
    FIRMWARE_AVAILABLE = 0,       /* New firmware is available to download */
    FIRMWARE_NOT_AVAILABLE = 1,   /* You're already on the latest version */
    UPDATE_NOT_ALLOWED = 2,       /* Firmware not compatible with this device model */
    FIRMWARE_CHECK_ERROR = 3,     /* Something went wrong checking for updates */
    IGNORE_OPTOUT = 4,            /* User has opted out and the update is blocked */
    BYPASS_OPTOUT = 5             /* Update available but requires explicit user consent before installation */ 
} CheckForUpdateStatus;

/**
 * DownloadStatus
 * 
 * Where are we in the download?
 */
typedef enum {
    DWNL_IN_PROGRESS = 0,         /* Download is happening now */
    DWNL_COMPLETED = 1,           /* Download finished successfully */
    DWNL_ERROR = 2                /* Download failed */
} DownloadStatus;

/**
 * UpdateStatus
 * 
 * Where are we in flashing the firmware?
 */
typedef enum {
    UPDATE_IN_PROGRESS = 0,       /* Firmware is being flashed now */
    UPDATE_COMPLETED = 1,         /* Firmware flash finished successfully */
    UPDATE_ERROR = 2              /* Firmware flash failed */
} UpdateStatus;


/* ========================================================================
 * DATA STRUCTURES
 * ======================================================================== */

/* UpdateDetails field size definitions */
#define MAX_FW_FILENAME_SIZE 128
#define MAX_FW_URL_SIZE 512
#define MAX_FW_VERSION_SIZE 64
#define MAX_REBOOT_IMMEDIATELY_SIZE 12
#define MAX_DELAY_DOWNLOAD_SIZE 8
#define MAX_PDRI_VERSION_LEN 64
#define MAX_PERIPHERAL_VERSION_LEN 256

typedef struct {
    char FwFileName[MAX_FW_FILENAME_SIZE];  /* Firmware file name  */
    char FwUrl[MAX_FW_URL_SIZE];       /* Download URL  */
    char FwVersion[MAX_FW_VERSION_SIZE];    /* Firmware version string */
    char RebootImmediately[MAX_REBOOT_IMMEDIATELY_SIZE]; /*Reboot flag ("true" or "false")*/
    char DelayDownload[MAX_DELAY_DOWNLOAD_SIZE];  /* Delay download flag ("true" or "false") */
    char PDRIVersion[MAX_PDRI_VERSION_LEN];       /* PDRI image version.*/
    char PeripheralFirmwares[MAX_PERIPHERAL_VERSION_LEN]; /* Peripheral image version; may be null if not configured*/
} UpdateDetails;

/**
 * FwInfoData
 * 
 * Information about firmware that's available (or not).
 * You get this in your UpdateEventCallback after calling checkForUpdate().
 */
typedef struct {
    char CurrFWVersion[MAX_FW_VERSION_SIZE];           /* Version string  */
    UpdateDetails *UpdateDetails;     /* details of the update available*/
    CheckForUpdateStatus status;   /* Did we find an update or not? */
} FwInfoData;

/**
 * FwDwnlReq
 * 
 * What firmware do you want to download?
 * Fill this out before calling downloadFirmware().
 */
typedef struct {
    const char *firmwareName;      /* Filename like "firmware_v2.bin" */
    const char *downloadUrl;       /* Where to download from (NULL = let daemon decide) */
    const char *TypeOfFirmware;    /* "PCI", "PDRI", or "PERIPHERAL" */
} FwDwnlReq;

/**
 * FwUpdateReq
 * 
 * Instructions for flashing firmware.
 * Fill this out before calling updateFirmware().
 */
typedef struct {
    const char *firmwareName;         /* Filename like "firmware_v2.bin" */
    const char *TypeOfFirmware;       /* "PCI", "PDRI", or "PERIPHERAL" */
    const char *LocationOfFirmware;   /* Where file is (NULL = use /etc/device.properties default) */
    bool rebootImmediately;           /* true = reboot right after flash, false = you'll reboot manually */
} FwUpdateReq;


/* ========================================================================
 * API RESULT CODES
 * ======================================================================== */

/**
 * Did the API call succeed or fail?
 * Note: This just means the call started successfully.
 * Actual results come through callbacks.
 */
typedef enum {
    CHECK_FOR_UPDATE_SUCCESS = 0,
    CHECK_FOR_UPDATE_FAIL = 1
} CheckForUpdateResult;

typedef enum {
    RDKFW_DWNL_SUCCESS = 0,
    RDKFW_DWNL_FAILED = 1
} DownloadResult;

typedef enum {
    RDKFW_UPDATE_SUCCESS = 0,
    RDKFW_UPDATE_FAILED = 1
} UpdateResult;


/* ========================================================================
 * CALLBACKS
 * ======================================================================== */

/**
 * UpdateEventCallback
 * 
 * Your function that gets called when checkForUpdate() finishes.
 * The library calls this from a background thread when it knows if firmware is available.
 * 
 * Parameters:
 *   fwinfodata - Pointer to update info (version, details, status)
 * 
 * Important notes:
 *   - The pointer and strings inside are only valid during this callback
 *   - If you need the data later, copy it with strdup()
 *   - Don't call other library functions from inside this callback
 *   - This runs in a background thread, not your main thread
 * 
 
 */
typedef void (*UpdateEventCallback)(const FwInfoData *fwinfodata);

/**
 * DownloadCallback
 * 
 * Your function that gets called repeatedly while firmware downloads.
 * The library calls this from a background thread to report progress.
 * 
 * Parameters:
 *   progress_per - Percentage complete (0 to 100)
 *   fwdwnlstatus - What's happening (IN_PROGRESS, COMPLETED, or ERROR)
 * 
 * Important notes:
 *   - This gets called multiple times (0%, 25%, 50%, 75%, 100%)
 *   - Don't call other library functions from inside this callback
 *   - This runs in a background thread, not your main thread
 * 
 */
typedef void (*DownloadCallback)(int download_progress, DownloadStatus fwdwnlstatus);

/**
 * UpdateCallback
 * 
 * Your function that gets called repeatedly while firmware flashes.
 * The library calls this from a background thread to report progress.
 * 
 * Parameters:
 *   progress_per - Percentage complete (0 to 100)
 *   fwupdatestatus - What's happening (IN_PROGRESS, COMPLETED, or ERROR)
 * 
 * Important notes:
 *   - This gets called multiple times (0%, 25%, 50%, 75%, 100%)
 *   - Don't call other library functions from inside this callback
 *   - This runs in a background thread, not your main thread
 *
 * API Stability Notice:
 * The signature and behavior of this callback may change in future versions
 * when HAL (Hardware Abstraction Layer) APIs become available. The daemon
 * will provide more granular progress information and device-specific status
 * updates once the underlying HAL interface is implemented. Client applications
 * should be prepared for potential signature changes in major version updates.
 *
 */
typedef void (*UpdateCallback)(int update_progress, UpdateStatus fwupdatestatus);


/* ========================================================================
 * PUBLIC API FUNCTIONS
 * ======================================================================== */

/**
 * registerProcess
 * 
 * Connect to the firmware daemon. This is the first thing you call.
 * 
 * Parameters:
 *   processName - Your app's name (like "VideoPlayer" or "MyApp")
 *   libVersion - Your app's version (like "1.0" or "2.3.1")
 * 
 * Returns:
 *   A string ID from the daemon (like "12345") if successful
 *   NULL if it fails (daemon not running, D-Bus error, etc.)
 * 
 * Important notes:
 *   - The returned string belongs to the library - don't free() it
 *   - Save this ID and use it for all other API calls
 *   - The ID becomes invalid after you call unregisterProcess()
 * 
 */
#define LIB_VERSION "1.0.0"
FirmwareInterfaceHandle registerProcess(const char *processName, const char *libVersion);

/**
 * unregisterProcess
 * 
 * Disconnect from the firmware daemon. Call this before your app exits.
 * 
 * Parameters:
 *   handler - The ID you got from registerProcess()
 * 
 * Important notes:
 *   - Always call this before exiting your app
 *   - After this, your handle becomes invalid (don't use it anymore)
 *   - Safe to call with NULL (does nothing)
 * 
 */
void unregisterProcess(FirmwareInterfaceHandle handler);

/**
 * checkForUpdate
 * 
 * Ask the daemon "Is there new firmware available?"
 * This is async - it returns immediately and calls your callback later.
 * 
 * Parameters:
 *   handle - Your ID from registerProcess()
 *   callback - Your function that handles the result
 * 
 * Returns:
 *   CHECK_FOR_UPDATE_SUCCESS - Request started OK
 *   CHECK_FOR_UPDATE_FAIL - Couldn't start (bad handle, NULL callback, etc.)
 * 
 * Important notes:
 *   - This returns right away (doesn't wait for answer)
 *   - Your callback gets called later in a background thread
 *   - The return code just means we started the check successfully
 *   - Actual firmware info comes through your callback
 * 
 */
CheckForUpdateResult checkForUpdate(FirmwareInterfaceHandle handle,UpdateEventCallback callback);

/**
 * downloadFirmware
 * 
 * Download new firmware from the server.
 * This is async - it returns immediately and calls your callback as download progresses.
 * 
 * Parameters:
 *   handle - Your ID from registerProcess()
 *   fwdwnlreq - Details about what to download (name, URL, type)
 *   callback - Your function that tracks download progress
 * 
 * Returns:
 *   RDKFW_DWNL_SUCCESS - Download started OK
 *   RDKFW_DWNL_FAILED - Couldn't start (bad handle, NULL callback, invalid request, etc.)
 * 
 * Important notes:
 *   - This returns right away (doesn't wait for download)
 *   - Your callback gets called multiple times (0%, 50%, 100%, etc.)
 *   - Set downloadUrl to NULL to let daemon use its default URL
 * 
 */
DownloadResult downloadFirmware(FirmwareInterfaceHandle handle,const FwDwnlReq *fwdwnlreq,DownloadCallback callback);

/**
 * updateFirmware
 * 
 * Flash the downloaded firmware to the device.
 * This is async - it returns immediately and calls your callback as flash progresses.
 * WARNING: This modifies your device's firmware. Make sure you downloaded the right file!
 * 
 * Parameters:
 *   handle - Your ID from registerProcess()
 *   fwupdatereq - Details about what to flash (name, type, location, reboot flag)
 *   callback - Your function that tracks flash progress
 * 
 * Returns:
 *   RDKFW_UPDATE_SUCCESS - Flash started OK
 *   RDKFW_UPDATE_FAILED - Couldn't start (bad handle, NULL callback, invalid request, etc.)
 * 
 * Important notes:
 *   - This returns right away (doesn't wait for flash to complete)
 *   - Your callback gets called multiple times (0%, 50%, 100%, etc.)
 *   - Set LocationOfFirmware to NULL to use daemon's default path
 *   - If rebootImmediately is true, device reboots when flash completes
 *   - This operation is irreversible - double-check your firmware file!
 * 
 */
UpdateResult updateFirmware(FirmwareInterfaceHandle handle,const FwUpdateReq *fwupdatereq,UpdateCallback callback);

/* ========================================================================
 * ASYNC CHECK FOR UPDATE API (NEW)
 * ======================================================================== */

/**
 * @brief Callback ID type for async operations
 * 
 * This is a unique identifier returned by async APIs.
 * Use this ID to cancel pending operations if needed.
 * A value of 0 indicates an invalid/error ID.
 */
typedef uint32_t AsyncCallbackId;

/**
 * @brief Update information structure for async callbacks
 * 
 * This structure contains the results of an async firmware update check.
 * It is provided to your callback when the check completes.
 * 
 * IMPORTANT MEMORY RULES:
 * - All string pointers are ONLY valid during the callback
 * - Do NOT free any fields yourself
 * - Do NOT store pointers - copy data with strdup() if needed
 * - The library manages all memory
 */
typedef struct {
    /** API result: 0 = success, non-zero = error */
    int32_t result;
    
    /** Status code (see CheckForUpdateStatus enum) */
    int32_t status_code;
    
    /** Current firmware version (e.g., "V1.2.3") */
    const char *current_version;
    
    /** Available firmware version (e.g., "V1.3.0" or NULL if none) */
    const char *available_version;
    
    /** Raw update details string from daemon (comma-separated key:value pairs) */
    const char *update_details;
    
    /** Human-readable status message (e.g., "New firmware available") */
    const char *status_message;
    
    /** Convenience flag: true if new firmware is available to download */
    bool update_available;
} AsyncUpdateInfo;

/**
 * @brief Async callback function type
 * 
 * Your callback function must match this signature.
 * It will be called when the firmware update check completes.
 * 
 * IMPORTANT THREADING NOTES:
 * - This callback runs in a BACKGROUND THREAD (not your thread!)
 * - Keep callback execution short - no blocking operations
 * - Don't call other library functions from inside callback
 * - Use proper synchronization if accessing shared data
 * 
 * MEMORY LIFETIME:
 * - The AsyncUpdateInfo structure is ONLY valid during this callback
 * - All string pointers are ONLY valid during this callback
 * - Copy any data you need with strdup() if you need it later
 * 
 * @param info Pointer to update information (valid only during callback)
 * @param user_data Your opaque data pointer (from checkForUpdate_async call)
 */
typedef void (*AsyncUpdateCallback)(const AsyncUpdateInfo *info, void *user_data);

/**
 * @brief Check for firmware update asynchronously (non-blocking)
 * 
 * This is the modern, non-blocking version of checkForUpdate().
 * It returns immediately and calls your callback when the result is ready.
 * 
 * HOW IT WORKS:
 * 1. You call this function with your callback
 * 2. Function returns immediately with a callback ID
 * 3. Library sends D-Bus request to daemon
 * 4. Your code continues running (not blocked)
 * 5. Later, when daemon responds, library calls your callback
 * 6. Callback receives firmware update information
 * 
 * ADVANTAGES OVER checkForUpdate():
 * - Non-blocking: Your code keeps running
 * - No handler registration needed
 * - Simpler API: Just provide callback and optional user data
 * - Multiple concurrent calls supported (up to 64 at once)
 * - Automatic timeout handling (60 seconds)
 * 
 * Parameters:
 *   callback - Your function to call when check completes (REQUIRED, cannot be NULL)
 *   user_data - Your opaque data pointer, passed back to callback (OPTIONAL, can be NULL)
 * 
 * Returns:
 *   AsyncCallbackId (>0) - Success, callback registered and will be invoked later
 *   0 - Failure (invalid callback, system not initialized, or registry full)
 * 
 * Example usage:
 * @code
 *   void my_callback(const AsyncUpdateInfo *info, void *user_data) {
 *       printf("Update available: %s\n", info->update_available ? "YES" : "NO");
 *       if (info->update_available) {
 *           printf("New version: %s\n", info->available_version);
 *       }
 *   }
 *   
 *   AsyncCallbackId id = checkForUpdate_async(my_callback, NULL);
 *   if (id == 0) {
 *       fprintf(stderr, "Failed to start async check\n");
 *   } else {
 *       printf("Check started, callback ID: %u\n", id);
 *       // Your code continues running here...
 *   }
 * @endcode
 * 
 * IMPORTANT NOTES:
 * - Callback is invoked in a BACKGROUND THREAD (not your thread!)
 * - Callback must be thread-safe if it accesses shared data
 * - Library handles all memory allocation and cleanup
 * - If daemon doesn't respond within 60 seconds, callback is invoked with timeout error
 * - You can cancel pending checks with checkForUpdate_async_cancel()
 * - Maximum 64 concurrent async calls allowed
 * 
 * WHEN TO USE THIS vs checkForUpdate():
 * - Use this if you want non-blocking behavior
 * - Use checkForUpdate() if you need synchronous/blocking behavior (rare)
 * - This is the recommended API for new code
 */
AsyncCallbackId checkForUpdate_async(AsyncUpdateCallback callback, void *user_data);

/**
 * @brief Cancel a pending async firmware update check
 * 
 * Cancels a pending async operation started with checkForUpdate_async().
 * If the operation is still waiting for the daemon to respond, your callback
 * will NOT be invoked. If the daemon has already responded and the callback
 * is being invoked or has completed, this function returns an error.
 * 
 * Parameters:
 *   callback_id - The ID returned by checkForUpdate_async()
 * 
 * Returns:
 *   0 - Success, callback was cancelled and will not be invoked
 *   -1 - Failure (invalid ID, not found, or already completed/cancelled)
 * 
 * Example usage:
 * @code
 *   AsyncCallbackId id = checkForUpdate_async(my_callback, NULL);
 *   
 *   // ... later, if you want to cancel ...
 *   if (checkForUpdate_async_cancel(id) == 0) {
 *       printf("Callback cancelled successfully\n");
 *   } else {
 *       printf("Callback already completed or not found\n");
 *   }
 * @endcode
 * 
 * IMPORTANT NOTES:
 * - Safe to call from any thread
 * - Cannot cancel if callback is already executing or completed
 * - After cancellation, the callback ID becomes invalid
 * - No harm in calling this on an already-completed operation (just returns -1)
 */
int checkForUpdate_async_cancel(AsyncCallbackId callback_id);

#ifdef __cplusplus
}
#endif

#endif /* RDKFWUPDATEMGR_CLIENT_H */
