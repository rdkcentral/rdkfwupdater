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

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * HANDLE TYPE
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
    IGNORE_OPTOUT = 4,            /* Download not allowed (opt-out related) */
    BYPASS_OPTOUT = 5             /* Download not allowed (opt-out bypass related) */
    /* Note: IGNORE_OPTOUT vs BYPASS_OPTOUT distinction is being clarified */  
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
#define MAX_FW_LOCATION_SIZE 512
#define MAX_FW_VERSION_SIZE 64
#define MAX_REBOOT_IMMEDIATELY_SIZE 12
#define MAX_DELAY_DOWNLOAD_SIZE 8
#define MAX_PDRI_VERSION_LEN 64
#define MAX_PERIPHERAL_VERSION_LEN 256

typedef struct {
    char FwFileName[MAX_FW_FILENAME_SIZE];  /* Firmware file name > **/
    char FwUrl[MAX_FW_LOCATION_SIZE];       /* Download URL  */
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
 * Example:
 *   void my_callback(const FwInfoData *info) {
 *       if (info->status == FIRMWARE_AVAILABLE) {
 *           printf("New version: %s\n", info->version);
 *       }
 *   }
 * 
 * Note: Currently using pass-by-pointer (more efficient than spec's pass-by-value).
 *       This may change if spec is updated.
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
 * Example:
 *   void download_progress(int percent, DownloadStatus status) {
 *       printf("Downloaded: %d%%\n", percent);
 *       if (status == DWNL_COMPLETED) {
 *           printf("Download done!\n");
 *       }
 *   }
 */
typedef void (*DownloadCallback)(int progress_per, DownloadStatus fwdwnlstatus);

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
 * Example:
 *   void update_progress(int percent, UpdateStatus status) {
 *       printf("Flashing: %d%%\n", percent);
 *       if (status == UPDATE_COMPLETED) {
 *           printf("Flash complete!\n");
 *       }
 *   }
 */
typedef void (*UpdateCallback)(int progress_per, UpdateStatus fwupdatestatus);


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
 * Example:
 *   FirmwareInterfaceHandle handle = registerProcess("MyApp", "1.0");
 *   if (!handle) {
 *       printf("Failed to connect to daemon\n");
 *       return -1;
 *   }
 *   printf("Connected, got ID: %s\n", handle);
 */
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
 * Example:
 *   unregisterProcess(handle);
 *   handle = NULL;  // Good practice
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
 * Example:
 *   void my_callback(const FwInfoData *info) {
 *       if (info->status == FIRMWARE_AVAILABLE) {
 *           printf("Update available: %s\n", info->version);
 *       } else {
 *           printf("No update available\n");
 *       }
 *   }
 *   
 *   CheckForUpdateResult result = checkForUpdate(handle, my_callback);
 *   if (result == CHECK_FOR_UPDATE_FAIL) {
 *       printf("Couldn't start update check\n");
 *   }
 */
CheckForUpdateResult checkForUpdate(
    FirmwareInterfaceHandle handle,
    UpdateEventCallback callback
);

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
 * Example:
 *   void download_progress(int percent, DownloadStatus status) {
 *       printf("Downloaded: %d%%\n", percent);
 *   }
 *   
 *   FwDwnlReq request = {
 *       .firmwareName = "firmware_v2.bin",
 *       .downloadUrl = NULL,  // Use daemon's default
 *       .TypeOfFirmware = "PCI"
 *   };
 *   
 *   DownloadResult result = downloadFirmware(handle, request, download_progress);
 *   if (result == RDKFW_DWNL_FAILED) {
 *       printf("Couldn't start download\n");
 *   }
 */
DownloadResult downloadFirmware(
    FirmwareInterfaceHandle handle,
    FwDwnlReq fwdwnlreq,
    DownloadCallback callback
);

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
 * Example:
 *   void update_progress(int percent, UpdateStatus status) {
 *       printf("Flashing: %d%%\n", percent);
 *   }
 *   
 *   FwUpdateReq request = {
 *       .firmwareName = "firmware_v2.bin",
 *       .TypeOfFirmware = "PCI",
 *       .LocationOfFirmware = NULL,  // Use daemon's default
 *       .rebootImmediately = false   // Don't reboot yet
 *   };
 *   
 *   UpdateResult result = updateFirmware(handle, request, update_progress);
 *   if (result == RDKFW_UPDATE_FAILED) {
 *       printf("Couldn't start firmware flash\n");
 *   }
 */
UpdateResult updateFirmware(
    FirmwareInterfaceHandle handle,
    FwUpdateReq fwupdatereq,
    UpdateCallback callback
);

#ifdef __cplusplus
}
#endif

#endif /* RDKFWUPDATEMGR_CLIENT_H */
