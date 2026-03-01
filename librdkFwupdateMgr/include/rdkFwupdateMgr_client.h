/*
 * Copyright 2026 Comcast Cable Communications Management, LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file rdkFwupdateMgr_client.h
 * @brief Public API for RDK Firmware Update Manager Client Library
 *
 * USAGE FLOW:
 * ===========
 *   1. App calls registerProcess() → gets FirmwareInterfaceHandle
 *   2. App calls checkForUpdate(handle, callback)
 *   3. checkForUpdate returns immediately: CHECK_FOR_UPDATE_SUCCESS or FAIL
 *   4. Daemon emits CheckForUpdateComplete signal
 *   5. Library fires UpdateEventCallback with CheckForUpdateStatus
 *   6. App calls unregisterProcess(handle) at shutdown
 *
 * STRICT API SIGNATURE:
 * =====================
 *   CheckForUpdateResult checkForUpdate(FirmwareInterfaceHandle handle,
 *                                       UpdateEventCallback callback);
 *
 *   Two parameters only. No user_data.
 *   Apps needing context should use module-level/static variables.
 */

#ifndef RDKFWUPDATEMGR_CLIENT_H
#define RDKFWUPDATEMGR_CLIENT_H

#include "rdkFwupdateMgr_process.h"   /* FirmwareInterfaceHandle,
 //                                        registerProcess(), unregisterProcess() */
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

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
 *
 *   FAILURE path:
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

#endif /* RDKFWUPDATEMGR_CLIENT_H */
