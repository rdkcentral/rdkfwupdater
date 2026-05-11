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
 * @file example_app.c
 * @brief Complete one-shot firmware update example
 *
 * COMPLETE WORKFLOW (One-Shot Binary):
 *   1. registerProcess()         → Get handle from daemon
 *   2. checkForUpdate()          → Returns immediately; callback fires later
 *   3. [Wait for callback]       → on_firmware_check_callback fires
 *   4. downloadFirmware()        → Download firmware image (if available)
 *   5. [Wait for download]       → on_download_progress_callback tracks progress
 *   6. updateFirmware()          → Flash firmware to device
 *   7. [Wait for flash]          → on_update_progress_callback tracks progress
 *   8. unregisterProcess()       → Cleanup
 *   9. Exit
 *
 * BUILD:
 *   gcc example_app.c \
 *       -o example_app \
 *       -lrdkFwupdateMgr \
 *       $(pkg-config --cflags --libs gio-2.0) \
 *       -lpthread
 *
 * RUN:
 *   ./example_app
 */

//#include "rdkFwupdateMgr_process.h"   /* registerProcess(), unregisterProcess() */
#include "rdkFwupdateMgr_client.h"    /* checkForUpdate(), downloadFirmware(), 
                                         updateFirmware(), all callbacks/enums  */
#include "rdkFwupdateMgr_log.h"       /* FWUPMGR_LOG() generic base macro */
#include "rdkv_cdl_log_wrapper.h"     /* log_init(), log_exit() */

/* ========================================================================
 * EXAMPLE_* logging macros  use FWUPMGR_LOG with LOG.RDK.EXAMPLE module.
 * Keeps example_plugin logs as [EXAMPLE], distinguishable from [FWUPMGR]
 * library logs and [FWUPG] daemon logs.
 * ======================================================================== */
#define EXAMPLE_DEBUG(format, ...) FWUPMGR_LOG(RDK_LOG_DEBUG, "LOG.RDK.EXAMPLE", format, ##__VA_ARGS__)
#define EXAMPLE_INFO(format, ...)  FWUPMGR_LOG(RDK_LOG_INFO,  "LOG.RDK.EXAMPLE", format, ##__VA_ARGS__)
#define EXAMPLE_WARN(format, ...)  FWUPMGR_LOG(RDK_LOG_WARN,  "LOG.RDK.EXAMPLE", format, ##__VA_ARGS__)
#define EXAMPLE_ERROR(format, ...) FWUPMGR_LOG(RDK_LOG_ERROR, "LOG.RDK.EXAMPLE", format, ##__VA_ARGS__)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>

/* ========================================================================
 * GLOBAL STATE FOR WORKFLOW COORDINATION
 * ========================================================================
 * Since callbacks don't support user_data, we use global variables to:
 *   - Store firmware info from checkForUpdate callback
 *   - Track workflow progress (check → download → flash)
 *   - Synchronize main thread with callback threads
 * ======================================================================== */

/* Global handle (used across all API calls) */
static FirmwareInterfaceHandle g_handle = NULL;

/* Firmware check state */
static pthread_mutex_t  g_check_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t   g_check_cond  = PTHREAD_COND_INITIALIZER;
static int              g_check_done  = 0;
static CheckForUpdateStatus g_check_status = FIRMWARE_CHECK_ERROR;
static char             g_fw_current_version[64]   = {0};
static char             g_fw_available_version[64] = {0};
static char             g_fw_filename[MAX_FW_FILENAME_SIZE]   = {0};
static char             g_fw_url[MAX_FW_URL_SIZE]              = {0};
static char             g_reboot_immediately[MAX_REBOOT_IMMEDIATELY_SIZE] = {0};
static char             g_delay_download[MAX_DELAY_DOWNLOAD_SIZE] = {0};

/* Download state */
static pthread_mutex_t  g_download_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t   g_download_cond  = PTHREAD_COND_INITIALIZER;
static int              g_download_done   = 0;
static DownloadStatus   g_download_status = DWNL_ERROR;

/* Update/flash state */
static pthread_mutex_t  g_update_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t   g_update_cond  = PTHREAD_COND_INITIALIZER;
static int              g_update_done   = 0;
static UpdateStatus     g_update_status = UPDATE_ERROR;

/* Exit code tracking */
static int g_exit_code = EXIT_SUCCESS;

/* ========================================================================
 * CALLBACK 1: checkForUpdate() Result
 * ========================================================================
 * This callback fires when the daemon emits UpdateEventSignal with the
 * actual firmware check result (after XConf query completes).
 * 
 * Runs in library's background thread - NOT main thread!
 * ======================================================================== */

/**
 * @brief Callback invoked when firmware check completes
 *
 * Signature: void fn(FirmwareInterfaceHandle handle, FwInfoData *event_data)
 *
 * @param handle      The firmware interface handle (session ID)
 * @param event_data  Firmware check result (valid only during callback)
 */
static void on_firmware_check_callback(const FwInfoData *event_data)
{
    EXAMPLE_INFO("checkForUpdate Callback Received\n");

    if (!event_data) {
        EXAMPLE_ERROR("event_data is NULL in callback!\n");
        pthread_mutex_lock(&g_check_mutex);
        g_check_status = FIRMWARE_CHECK_ERROR;
        g_check_done = 1;
        pthread_cond_signal(&g_check_cond);
        pthread_mutex_unlock(&g_check_mutex);
        return;
    }

    /* Map status to readable string */
    const char *status_str = "UNKNOWN";
    switch (event_data->status) {
        case FIRMWARE_AVAILABLE:     status_str = "FIRMWARE_AVAILABLE";     break;
        case FIRMWARE_NOT_AVAILABLE: status_str = "FIRMWARE_NOT_AVAILABLE"; break;
        case UPDATE_NOT_ALLOWED:     status_str = "UPDATE_NOT_ALLOWED";     break;
        case FIRMWARE_CHECK_ERROR:   status_str = "FIRMWARE_CHECK_ERROR";   break;
        case IGNORE_OPTOUT:          status_str = "IGNORE_OPTOUT";          break;
        case BYPASS_OPTOUT:          status_str = "BYPASS_OPTOUT";          break;
    }

    EXAMPLE_INFO("=== Basic Firmware Info ===\n");
    EXAMPLE_INFO("  Handle              : %s\n", g_handle ? g_handle : "(null)");
    EXAMPLE_INFO("  Status Code         : %s (%d)\n", status_str, event_data->status);
    EXAMPLE_INFO("  Current FW Version  : %s\n", 
           event_data->CurrFWVersion[0] ? event_data->CurrFWVersion : "(not provided)");

    /* Print UpdateDetails if available (only when status == FIRMWARE_AVAILABLE) */
    if (event_data->status == FIRMWARE_AVAILABLE && event_data->UpdateDetails) {
        EXAMPLE_INFO("=== Update Details (Available!) ===\n");
        EXAMPLE_INFO("  FwFileName          : %s\n", 
               event_data->UpdateDetails->FwFileName[0] ? 
               event_data->UpdateDetails->FwFileName : "null");
        EXAMPLE_INFO("  FwUrl               : %s\n", 
               event_data->UpdateDetails->FwUrl[0] ? 
               event_data->UpdateDetails->FwUrl : "null");
        EXAMPLE_INFO("  FwVersion           : %s\n", 
               event_data->UpdateDetails->FwVersion[0] ? 
               event_data->UpdateDetails->FwVersion : "null");
        EXAMPLE_INFO("  RebootImmediately   : %s\n", 
               event_data->UpdateDetails->RebootImmediately[0] ? 
               event_data->UpdateDetails->RebootImmediately : "null");
        EXAMPLE_INFO("  DelayDownload       : %s\n", 
               event_data->UpdateDetails->DelayDownload[0] ? 
               event_data->UpdateDetails->DelayDownload : "null");
        EXAMPLE_INFO("  PDRIVersion         : %s\n", 
               event_data->UpdateDetails->PDRIVersion[0] ? 
               event_data->UpdateDetails->PDRIVersion : "null");
        EXAMPLE_INFO("  PeripheralFirmwares : %s\n", 
               event_data->UpdateDetails->PeripheralFirmwares[0] ? 
               event_data->UpdateDetails->PeripheralFirmwares : "null");
    } else if (event_data->status == FIRMWARE_AVAILABLE && !event_data->UpdateDetails) {
        EXAMPLE_WARN("Status is FIRMWARE_AVAILABLE but UpdateDetails is NULL!\n");
    } else {
        EXAMPLE_INFO("No update details (status != FIRMWARE_AVAILABLE)\n");
    }

    /* Copy data to global state (data is only valid during this callback!) */
    pthread_mutex_lock(&g_check_mutex);
    
    g_check_status = event_data->status;
    
    /* Copy current version */
    if (event_data->CurrFWVersion[0]) {
        strncpy(g_fw_current_version, event_data->CurrFWVersion, 
                sizeof(g_fw_current_version) - 1);
        g_fw_current_version[sizeof(g_fw_current_version) - 1] = '\0';
    }
    
    /* Copy UpdateDetails if firmware is available */
    if (event_data->status == FIRMWARE_AVAILABLE && 
        event_data->UpdateDetails) {
        /* Save firmware version */
        if (event_data->UpdateDetails->FwVersion[0]) {
            strncpy(g_fw_available_version, event_data->UpdateDetails->FwVersion,
                    sizeof(g_fw_available_version) - 1);
            g_fw_available_version[sizeof(g_fw_available_version) - 1] = '\0';
        }
        
        /* Save firmware filename for download step */
        if (event_data->UpdateDetails->FwFileName[0]) {
            strncpy(g_fw_filename, event_data->UpdateDetails->FwFileName,
                    sizeof(g_fw_filename) - 1);
            g_fw_filename[sizeof(g_fw_filename) - 1] = '\0';
        }
        
        /* Save download URL */
        if (event_data->UpdateDetails->FwUrl[0]) {
            strncpy(g_fw_url, event_data->UpdateDetails->FwUrl,
                    sizeof(g_fw_url) - 1);
            g_fw_url[sizeof(g_fw_url) - 1] = '\0';
        }
        
        /* Save reboot flag */
        if (event_data->UpdateDetails->RebootImmediately[0]) {
            strncpy(g_reboot_immediately, event_data->UpdateDetails->RebootImmediately,
                    sizeof(g_reboot_immediately) - 1);
            g_reboot_immediately[sizeof(g_reboot_immediately) - 1] = '\0';
        }
        
        /* Save delay download flag */
        if (event_data->UpdateDetails->DelayDownload[0]) {
            strncpy(g_delay_download, event_data->UpdateDetails->DelayDownload,
                    sizeof(g_delay_download) - 1);
            g_delay_download[sizeof(g_delay_download) - 1] = '\0';
        }
    }
    
    g_check_done = 1;
    pthread_cond_signal(&g_check_cond);
    pthread_mutex_unlock(&g_check_mutex);

    EXAMPLE_INFO("Firmware check data saved. Main thread will proceed.\n");
}

/* ========================================================================
 * CALLBACK 2: downloadFirmware() Progress
 * ========================================================================
 * This callback fires repeatedly as download progresses (1%, 10%, 50%, 100%).
 * Runs in library's background thread.
 * ======================================================================== */

/**
 * @brief Callback invoked on each download progress update
 *
 * Signature: void fn(int progress_per, DownloadStatus fwdwnlstatus)
 *
 * @param progress_per   Download percentage (0-100)
 * @param fwdwnlstatus   Current download state
 */
static void on_download_progress_callback(int progress_per, DownloadStatus fwdwnlstatus)
{
    const char *status_str = "UNKNOWN";
    switch (fwdwnlstatus) {
        case DWNL_IN_PROGRESS: status_str = "DWNL_IN_PROGRESS"; break;
        case DWNL_COMPLETED:   status_str = "DWNL_COMPLETED";   break;
        case DWNL_ERROR:       status_str = "DWNL_ERROR";       break;
    }

    /* Print progress: 40%  DWNL_IN_PROGRESS */
    EXAMPLE_INFO("  Download: %3d%%  %s\n", progress_per, status_str);

    /* On terminal states (COMPLETED or ERROR), wake main thread */
    if (fwdwnlstatus == DWNL_COMPLETED || fwdwnlstatus == DWNL_ERROR) {
        pthread_mutex_lock(&g_download_mutex);
        g_download_status = fwdwnlstatus;
        g_download_done = 1;
        pthread_cond_signal(&g_download_cond);
        pthread_mutex_unlock(&g_download_mutex);

        if (fwdwnlstatus == DWNL_COMPLETED) {
            EXAMPLE_INFO("  Download completed successfully!\n");
        } else {
            EXAMPLE_ERROR("  Download failed!\n");
        }
    }
}

/* ========================================================================
 * CALLBACK 3: updateFirmware() Progress
 * ========================================================================
 * This callback fires repeatedly as flash progresses (1%, 10%, 50%, 100%).
 * Runs in library's background thread.
 * ======================================================================== */

/**
 * @brief Callback invoked on each firmware flash progress update
 *
 * Signature: void fn(int progress_per, UpdateStatus fwupdatestatus)
 *
 * @param progress_per    Flash percentage (0-100)
 * @param fwupdatestatus  Current flash state
 */
static void on_update_progress_callback(int progress_per, UpdateStatus fwupdatestatus)
{
    const char *status_str = "UNKNOWN";
    switch (fwupdatestatus) {
        case UPDATE_IN_PROGRESS: status_str = "UPDATE_IN_PROGRESS"; break;
        case UPDATE_COMPLETED:   status_str = "UPDATE_COMPLETED";   break;
        case UPDATE_ERROR:       status_str = "UPDATE_ERROR";       break;
    }

    /* Print progress: 40%  UPDATE_IN_PROGRESS */
    EXAMPLE_INFO("  Flash: %3d%%  %s\n", progress_per, status_str);

    /* On terminal states (COMPLETED or ERROR), wake main thread */
    if (fwupdatestatus == UPDATE_COMPLETED || fwupdatestatus == UPDATE_ERROR) {
        pthread_mutex_lock(&g_update_mutex);
        g_update_status = fwupdatestatus;
        g_update_done = 1;
        pthread_cond_signal(&g_update_cond);
        pthread_mutex_unlock(&g_update_mutex);

        if (fwupdatestatus == UPDATE_COMPLETED) {
            EXAMPLE_INFO("  Firmware flash completed successfully!\n");
        } else {
            EXAMPLE_ERROR("  Firmware flash failed!\n");
        }
    }
}

/* ========================================================================
 * MAIN - Complete One-Shot Firmware Update Workflow
 * ========================================================================
 * Executes the full sequence:
 *   1. Register with daemon
 *   2. Check for updates
 *   3. Download firmware (if available)
 *   4. Flash firmware
 *   5. Unregister and exit
 * ======================================================================== */

int main(void)
{
    struct timespec timeout;
    int rc;

    /* Initialize logging must be first.
     * All EXAMPLE_* and FWUPMGR_* log output goes to stdout/stderr.
     * Shell redirect puts it in the right file:
     *   example_plugin > /opt/logs/rdkFwupdateMgr.log 2>&1
     */
    log_init();

    EXAMPLE_INFO("==============================\n");
    EXAMPLE_INFO("Application starting, PID: %d\n", getpid());

    /* ====================================================================
     * STEP 1: Register Process with Daemon
     * ==================================================================== */
    EXAMPLE_INFO("STEP 1: Register with firmware daemon\n");
    EXAMPLE_INFO("  Process Name : ExampleApp\n");
    EXAMPLE_INFO("  Lib Version  : 1.0.0\n");

    g_handle = registerProcess("ExampleApp", "1.0.0");

    if (g_handle == NULL) {
        EXAMPLE_ERROR("registerProcess() failed!\n");
        EXAMPLE_ERROR("Ensure rdkFwupdateMgr daemon is running:\n");
        EXAMPLE_ERROR("systemctl status rdkFwupdateMgr.service\n");
        log_exit();
        return EXIT_FAILURE;
    }

    EXAMPLE_INFO("Registered successfully\n");
    EXAMPLE_INFO("  Handle: '%s'\n", g_handle);

    /* ====================================================================
     * STEP 2: Check for Firmware Updates (Async)
     * ==================================================================== */
    EXAMPLE_INFO("STEP 2: Check for firmware updates\n");
    EXAMPLE_INFO("  Calling checkForUpdate()...\n");
    EXAMPLE_INFO("  (API returns immediately; callback fires when XConf query completes)\n");

    CheckForUpdateResult cfu_result = checkForUpdate(g_handle, on_firmware_check_callback);

    if (cfu_result != CHECK_FOR_UPDATE_SUCCESS) {
        EXAMPLE_ERROR("checkForUpdate() returned FAIL!\n");
        EXAMPLE_ERROR("Possible reasons: D-Bus error, daemon not responding, invalid handle\n");
        g_exit_code = EXIT_FAILURE;
        goto cleanup_unregister;
    }

    EXAMPLE_INFO("checkForUpdate() returned SUCCESS\n");
    EXAMPLE_INFO("  (Daemon ACK received - waiting for actual firmware data...)\n");

    /* Wait for callback with timeout (2 minutes for XConf query) */
    EXAMPLE_INFO("Waiting for firmware check callback...\n");

    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 120;  /* 2 minute timeout */

    pthread_mutex_lock(&g_check_mutex);
    while (!g_check_done) {
        rc = pthread_cond_timedwait(&g_check_cond, &g_check_mutex, &timeout);
        if (rc != 0) {
            pthread_mutex_unlock(&g_check_mutex);
            EXAMPLE_ERROR("Timeout waiting for checkForUpdate callback (120s)\n");
            EXAMPLE_ERROR("XConf query may be taking longer than expected.\n");
            g_exit_code = EXIT_FAILURE;
            goto cleanup_unregister;
        }
    }
    pthread_mutex_unlock(&g_check_mutex);

    /* Check result */
    if (g_check_status != FIRMWARE_AVAILABLE) {
        EXAMPLE_WARN("No firmware update available\n");
        EXAMPLE_INFO("  Status: %d\n", g_check_status);
        EXAMPLE_INFO("  Current Version: %s\n", g_fw_current_version);
        
        if (g_check_status == FIRMWARE_NOT_AVAILABLE) {
            EXAMPLE_INFO("  Already on latest version. No action needed.\n");
            g_exit_code = EXIT_SUCCESS;
        } else {
            EXAMPLE_ERROR("  Cannot proceed with update.\n");
            g_exit_code = EXIT_FAILURE;
        }
        goto cleanup_unregister;
    }

    EXAMPLE_INFO("Firmware update available!\n");
    EXAMPLE_INFO("  Current Version  : %s\n", g_fw_current_version);
    EXAMPLE_INFO("  Available Version: %s\n", g_fw_available_version);
    EXAMPLE_INFO("  Proceeding to download...\n");

    /* ====================================================================
     * STEP 3: Download Firmware (Async)
     * ==================================================================== */
    EXAMPLE_INFO("STEP 3: Download firmware image\n");

    /* Prepare download request using data from checkForUpdate callback */
    FwDwnlReq download_req;
    memset(&download_req, 0, sizeof(download_req));

    /* Use firmware filename from UpdateDetails if available, otherwise construct one */
    const char *fw_name = (g_fw_filename[0] != '\0') ? g_fw_filename : "firmware_default.bin";
    const char *fw_url = (g_fw_url[0] != '\0') ? g_fw_url : "";  /* Empty = use XConf URL */
    
    download_req.firmwareName = fw_name;
    download_req.downloadUrl = fw_url;
    download_req.TypeOfFirmware = "PCI";  /* Default to PCI type */

    EXAMPLE_INFO("  Firmware Name : %s\n", download_req.firmwareName);
    EXAMPLE_INFO("  Download URL  : %s\n", download_req.downloadUrl[0] ? download_req.downloadUrl : "(use XConf URL)");
    EXAMPLE_INFO("  Firmware Type : %s\n", download_req.TypeOfFirmware);

    EXAMPLE_INFO("  Calling downloadFirmware()...\n");

    DownloadResult dl_result = downloadFirmware(g_handle, &download_req, 
                                                 on_download_progress_callback);

    if (dl_result != RDKFW_DWNL_SUCCESS) {
        EXAMPLE_ERROR("downloadFirmware() returned FAIL!\n");
        g_exit_code = EXIT_FAILURE;
        goto cleanup_unregister;
    }

    EXAMPLE_INFO("downloadFirmware() returned SUCCESS\n");
    EXAMPLE_INFO("  Waiting for download progress...\n");

    /* Wait for download completion with timeout (5 minutes) */
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 300;  /* 5 minute timeout for download */

    pthread_mutex_lock(&g_download_mutex);
    while (!g_download_done) {
        rc = pthread_cond_timedwait(&g_download_cond, &g_download_mutex, &timeout);
        if (rc != 0) {
            pthread_mutex_unlock(&g_download_mutex);
            EXAMPLE_ERROR("Timeout waiting for download completion (5 min)\n");
            g_exit_code = EXIT_FAILURE;
            goto cleanup_unregister;
        }
    }
    pthread_mutex_unlock(&g_download_mutex);

    /* Check download result */
    if (g_download_status != DWNL_COMPLETED) {
        EXAMPLE_ERROR("Download failed (status=%d)\n", g_download_status);
        g_exit_code = EXIT_FAILURE;
        goto cleanup_unregister;
    }

    EXAMPLE_INFO("Download complete. Proceeding to flash...\n");

    /* ====================================================================
     * STEP 4: Update/Flash Firmware (Async)
     * ==================================================================== */
    EXAMPLE_INFO("STEP 4: Flash firmware to device\n");

    /* Prepare update request */
    FwUpdateReq update_req;
    memset(&update_req, 0, sizeof(update_req));

    /* Must match what was downloaded */
    //strncpy(update_req.firmwareName, download_req.firmwareName,
    //        sizeof(update_req.firmwareName) - 1);
    update_req.firmwareName = download_req.firmwareName;
    
    /* Must match download request */
    //strncpy(update_req.TypeOfFirmware, download_req.TypeOfFirmware,
    //        sizeof(update_req.TypeOfFirmware) - 1);
    update_req.TypeOfFirmware = download_req.TypeOfFirmware;
    
    /* Location: Use /opt/CDL (default firmware download directory) */
    update_req.LocationOfFirmware = "/opt/CDL";
    
    /* Reboot after flash: false for this example (so we can unregister cleanly) */
    update_req.rebootImmediately = false;

    EXAMPLE_INFO("  Firmware Name  : %s\n", update_req.firmwareName);
    EXAMPLE_INFO("  Firmware Type  : %s\n", update_req.TypeOfFirmware);
    EXAMPLE_INFO("  Location       : %s\n", update_req.LocationOfFirmware);
    EXAMPLE_INFO("  Reboot Now     : %s\n", update_req.rebootImmediately ? "true" : "false");

    EXAMPLE_INFO("  Calling updateFirmware()...\n");

    UpdateResult upd_result = updateFirmware(g_handle, &update_req,
                                              on_update_progress_callback);

    if (upd_result != RDKFW_UPDATE_SUCCESS) {
        EXAMPLE_ERROR("updateFirmware() returned FAIL!\n");
        g_exit_code = EXIT_FAILURE;
        goto cleanup_unregister;
    }

    EXAMPLE_INFO("updateFirmware() returned SUCCESS\n");
    EXAMPLE_INFO("  Waiting for flash progress...\n");

    /* Wait for flash completion with timeout (10 minutes) */
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 600;  /* 10 minute timeout for flashing */

    pthread_mutex_lock(&g_update_mutex);
    while (!g_update_done) {
        rc = pthread_cond_timedwait(&g_update_cond, &g_update_mutex, &timeout);
        if (rc != 0) {
            pthread_mutex_unlock(&g_update_mutex);
            EXAMPLE_ERROR("Timeout waiting for flash completion (10 min)\n");
            g_exit_code = EXIT_FAILURE;
            goto cleanup_unregister;
        }
    }
    pthread_mutex_unlock(&g_update_mutex);

    /* Check flash result */
    if (g_update_status != UPDATE_COMPLETED) {
        EXAMPLE_ERROR("Firmware flash failed (status=%d)\n", g_update_status);
        g_exit_code = EXIT_FAILURE;
        goto cleanup_unregister;
    }

    EXAMPLE_INFO("Flash complete!\n");

    /* ====================================================================
     * STEP 5: Unregister and Cleanup
     * ==================================================================== */
cleanup_unregister:
    EXAMPLE_INFO("STEP 5: Unregister from daemon\n");

    if (g_handle != NULL) {
        EXAMPLE_INFO("  Calling unregisterProcess()...\n");
        unregisterProcess(g_handle);
        g_handle = NULL;
        EXAMPLE_INFO("  Unregistered successfully\n");
    }

    /* ====================================================================
     * Final Status
     * ==================================================================== */
    if (g_exit_code == EXIT_SUCCESS) {
        EXAMPLE_INFO("FIRMWARE UPDATE WORKFLOW COMPLETED\n");
        if (g_update_status == UPDATE_COMPLETED) {
            EXAMPLE_INFO("  Firmware flashed successfully.\n");
            EXAMPLE_INFO("  System reboot required to activate new firmware.\n");
        }
    } else {
        EXAMPLE_ERROR("FIRMWARE UPDATE WORKFLOW FAILED\n");
        EXAMPLE_INFO("  Check logs for details: tail -f /opt/logs/rdkFwupdateMgr.log\n");
    }

    log_exit();
    return g_exit_code;
}

/* ═══════════════════════════════════════════════════════════════════════
 * COMPLETE WORKFLOW SEQUENCE DIAGRAM
 * ═══════════════════════════════════════════════════════════════════════
 *
 *  Main Thread              Library BG Thread         Daemon Process
 *  ───────────              ─────────────────         ──────────────
 *
 *  [STEP 1: Register]
 *  registerProcess("ExampleApp", "1.0.0")
 *    │───── D-Bus: RegisterProcess ──────────────────► │
 *    │◄──── Returns handler_id=12345 ─────────────────┤
 *  g_handle = "12345"
 *
 *  [STEP 2: Check for Update]
 *  checkForUpdate(g_handle, on_firmware_check_callback)
 *    │ - Register callback                            │
 *    │───── D-Bus: CheckForUpdate(12345) ────────────► │
 *    │◄──── Returns ACK ──────────────────────────────┤
 *  Returns CHECK_FOR_UPDATE_SUCCESS                   │
 *                                                      │
 *  [Wait on condvar]                                   │ [Query XConf...]
 *  pthread_cond_wait(&g_check_cond)                    │ [Parse response]
 *                                                      │
 *                     ◄─── D-Bus Signal: UpdateEventSignal(12345, data) ──┤
 *                     Catch signal
 *                     Parse firmware data
 *                     on_firmware_check_callback(g_handle, event_data)
 *                       ├─ Copy firmware versions
 *                       ├─ Set g_check_done = 1
 *                       └─ pthread_cond_signal(&g_check_cond)
 *
 *  [Main wakes up]
 *  Check g_check_status == FIRMWARE_AVAILABLE
 *
 *  [STEP 3: Download Firmware]
 *  downloadFirmware(g_handle, download_req, on_download_progress_callback)
 *    │ - Register callback                            │
 *    │───── D-Bus: DownloadFirmware(...) ────────────► │
 *    │◄──── Returns ACK ──────────────────────────────┤
 *  Returns RDKFW_DWNL_SUCCESS                         │
 *                                                      │
 *  [Wait on condvar]                                   │ [Download: 1%]
 *  pthread_cond_wait(&g_download_cond)                 │
 *                     ◄─── Signal: DownloadProgress(1, IN_PROGRESS) ──────┤
 *                     on_download_progress_callback(1, DWNL_IN_PROGRESS)
 *                       └─ Print progress bar
 *                                                      │ [Download: 50%]
 *                     ◄─── Signal: DownloadProgress(50, IN_PROGRESS) ─────┤
 *                     on_download_progress_callback(50, DWNL_IN_PROGRESS)
 *                       └─ Print progress bar
 *                                                      │ [Download: 100%]
 *                     ◄─── Signal: DownloadProgress(100, COMPLETED) ──────┤
 *                     on_download_progress_callback(100, DWNL_COMPLETED)
 *                       ├─ Set g_download_done = 1
 *                       └─ pthread_cond_signal(&g_download_cond)
 *
 *  [Main wakes up]
 *  Check g_download_status == DWNL_COMPLETED
 *
 *  [STEP 4: Flash Firmware]
 *  updateFirmware(g_handle, update_req, on_update_progress_callback)
 *    │ - Register callback                            │
 *    │───── D-Bus: UpdateFirmware(...) ──────────────► │
 *    │◄──── Returns ACK ──────────────────────────────┤
 *  Returns RDKFW_UPDATE_SUCCESS                       │
 *                                                      │
 *  [Wait on condvar]                                   │ [Flash: 1%]
 *  pthread_cond_wait(&g_update_cond)                   │
 *                     ◄─── Signal: UpdateProgress(1, UPDATE_IN_PROGRESS) ─┤
 *                     on_update_progress_callback(1, UPDATE_IN_PROGRESS)
 *                       └─ Print progress bar
 *                                                      │ [Flash: 100%]
 *                     ◄─── Signal: UpdateProgress(100, UPDATE_COMPLETED) ─┤
 *                     on_update_progress_callback(100, UPDATE_COMPLETED)
 *                       ├─ Set g_update_done = 1
 *                       └─ pthread_cond_signal(&g_update_cond)
 *
 *  [Main wakes up]
 *  Check g_update_status == UPDATE_COMPLETED
 *
 *  [STEP 5: Cleanup]
 *  unregisterProcess(g_handle)
 *    │───── D-Bus: UnregisterProcess(12345) ────────► │
 *    │◄──── Returns success ──────────────────────────┤
 *    │ Free handle internally
 *  g_handle = NULL
 *
 *  Exit(EXIT_SUCCESS)
 *
 * ═══════════════════════════════════════════════════════════════════════
 * KEY TIMING NOTES:
 * ═══════════════════════════════════════════════════════════════════════
 *
 * 1. checkForUpdate():
 *    - API returns in ~5s (daemon ACK)
 *    - Callback fires 1s to 2 hours later (XConf query time)
 *    - We wait with 2 minute timeout (adjust for your network)
 *
 * 2. downloadFirmware():
 *    - API returns immediately
 *    - Callbacks fire repeatedly (progress updates)
 *    - We wait with 5 minute timeout (adjust for firmware size/network)
 *
 * 3. updateFirmware():
 *    - API returns immediately
 *    - Callbacks fire repeatedly (flash progress)
 *    - We wait with 10 minute timeout (adjust for flash speed)
 *
 * ═══════════════════════════════════════════════════════════════════════
 * ERROR HANDLING:
 * ═══════════════════════════════════════════════════════════════════════
 *
 * - Any API returning FAIL → goto cleanup_unregister
 * - Any callback timeout → goto cleanup_unregister
 * - Check status != AVAILABLE → exit with appropriate message
 * - Download or flash errors → detected in callback, set g_exit_code
 *
 * ═══════════════════════════════════════════════════════════════════════
 */
