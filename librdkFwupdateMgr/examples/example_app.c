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

#include "rdkFwupdateMgr_process.h"   /* registerProcess(), unregisterProcess() */
#include "rdkFwupdateMgr_client.h"    /* checkForUpdate(), downloadFirmware(), 
                                         updateFirmware(), all callbacks/enums  */
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
 * Signature: void fn(FirmwareInterfaceHandle, const FwUpdateEventData*)
 *
 * @param handle     Handler ID that initiated the check
 * @param event_data Firmware check result (valid only during callback)
 */
static void on_firmware_check_callback(FirmwareInterfaceHandle handle,
                                         const FwUpdateEventData *event_data)
{
    printf("\n");
    printf("┌──────────────────────────────────────────────────────┐\n");
    printf("│  ✓ checkForUpdate Callback Received                 │\n");
    printf("└──────────────────────────────────────────────────────┘\n");

    if (!event_data) {
        fprintf(stderr, "[ERROR] event_data is NULL in callback!\n");
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

    printf("  Handle              : %s\n", handle ? handle : "(null)");
    printf("  Status              : %s (%d)\n", status_str, event_data->status);
    printf("  Update Available    : %s\n", event_data->update_available ? "YES" : "NO");
    printf("  Current Version     : %s\n", 
           event_data->current_version ? event_data->current_version : "(not provided)");
    printf("  Available Version   : %s\n", 
           event_data->available_version ? event_data->available_version : "(not provided)");
    printf("  Status Message      : %s\n",
           event_data->status_message ? event_data->status_message : "(not provided)");

    /* Copy data to global state (data is only valid during this callback!) */
    pthread_mutex_lock(&g_check_mutex);
    
    g_check_status = event_data->status;
    
    if (event_data->current_version) {
        strncpy(g_fw_current_version, event_data->current_version, 
                sizeof(g_fw_current_version) - 1);
    }
    
    if (event_data->available_version) {
        strncpy(g_fw_available_version, event_data->available_version,
                sizeof(g_fw_available_version) - 1);
    }
    
    g_check_done = 1;
    pthread_cond_signal(&g_check_cond);
    pthread_mutex_unlock(&g_check_mutex);

    printf("\n  → Firmware check data saved. Main thread will proceed.\n");
    printf("└──────────────────────────────────────────────────────┘\n\n");
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

    /* Print progress bar: [████████░░░░░░░░░░░░] 40%  DWNL_IN_PROGRESS */
    int bar_filled = progress_per / 5;  /* 20 characters = 100% */
    printf("    [");
    for (int i = 0; i < 20; i++) {
        printf(i < bar_filled ? "█" : "░");
    }
    printf("] %3d%%  %s\n", progress_per, status_str);

    /* On terminal states (COMPLETED or ERROR), wake main thread */
    if (fwdwnlstatus == DWNL_COMPLETED || fwdwnlstatus == DWNL_ERROR) {
        pthread_mutex_lock(&g_download_mutex);
        g_download_status = fwdwnlstatus;
        g_download_done = 1;
        pthread_cond_signal(&g_download_cond);
        pthread_mutex_unlock(&g_download_mutex);

        if (fwdwnlstatus == DWNL_COMPLETED) {
            printf("\n    ✓ Download completed successfully!\n\n");
        } else {
            printf("\n    ✗ Download failed!\n\n");
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

    /* Print progress bar: [████████░░░░░░░░░░░░] 40%  UPDATE_IN_PROGRESS */
    int bar_filled = progress_per / 5;  /* 20 characters = 100% */
    printf("    [");
    for (int i = 0; i < 20; i++) {
        printf(i < bar_filled ? "▓" : "░");
    }
    printf("] %3d%%  %s\n", progress_per, status_str);

    /* On terminal states (COMPLETED or ERROR), wake main thread */
    if (fwupdatestatus == UPDATE_COMPLETED || fwupdatestatus == UPDATE_ERROR) {
        pthread_mutex_lock(&g_update_mutex);
        g_update_status = fwupdatestatus;
        g_update_done = 1;
        pthread_cond_signal(&g_update_cond);
        pthread_mutex_unlock(&g_update_mutex);

        if (fwupdatestatus == UPDATE_COMPLETED) {
            printf("\n    ✓ Firmware flash completed successfully!\n\n");
        } else {
            printf("\n    ✗ Firmware flash failed!\n\n");
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

    printf("\n");
    printf("╔═══════════════════════════════════════════════════════╗\n");
    printf("║   RDK Firmware Update Manager - Complete Workflow    ║\n");
    printf("╚═══════════════════════════════════════════════════════╝\n\n");

    /* ====================================================================
     * STEP 1: Register Process with Daemon
     * ==================================================================== */
    printf("┌─────────────────────────────────────────────────────┐\n");
    printf("│ STEP 1: Register with firmware daemon              │\n");
    printf("└─────────────────────────────────────────────────────┘\n");
    printf("  Process Name : ExampleApp\n");
    printf("  Lib Version  : 1.0.0\n\n");

    g_handle = registerProcess("ExampleApp", "1.0.0");

    if (g_handle == FIRMWARE_INVALID_HANDLE) {
        fprintf(stderr, "[ERROR] registerProcess() failed!\n");
        fprintf(stderr, "        Ensure rdkFwupdateMgr daemon is running:\n");
        fprintf(stderr, "        systemctl status rdkFwupdateMgr.service\n\n");
        return EXIT_FAILURE;
    }

    printf("  ✓ Registered successfully\n");
    printf("    Handle: '%s'\n\n", g_handle);

    /* ====================================================================
     * STEP 2: Check for Firmware Updates (Async)
     * ==================================================================== */
    printf("┌─────────────────────────────────────────────────────┐\n");
    printf("│ STEP 2: Check for firmware updates                 │\n");
    printf("└─────────────────────────────────────────────────────┘\n");
    printf("  Calling checkForUpdate()...\n");
    printf("  (API returns immediately; callback fires when XConf query completes)\n\n");

    CheckForUpdateResult cfu_result = checkForUpdate(g_handle, on_firmware_check_callback);

    if (cfu_result != CHECK_FOR_UPDATE_SUCCESS) {
        fprintf(stderr, "[ERROR] checkForUpdate() returned FAIL!\n");
        fprintf(stderr, "        Possible reasons:\n");
        fprintf(stderr, "          - D-Bus connection error\n");
        fprintf(stderr, "          - Daemon not responding\n");
        fprintf(stderr, "          - Invalid handle\n\n");
        g_exit_code = EXIT_FAILURE;
        goto cleanup_unregister;
    }

    printf("  ✓ checkForUpdate() returned SUCCESS\n");
    printf("    (Daemon ACK received - waiting for actual firmware data...)\n\n");

    /* Wait for callback with timeout (2 minutes for XConf query) */
    printf("  Waiting for firmware check callback");
    fflush(stdout);

    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 120;  /* 2 minute timeout */

    pthread_mutex_lock(&g_check_mutex);
    while (!g_check_done) {
        rc = pthread_cond_timedwait(&g_check_cond, &g_check_mutex, &timeout);
        if (rc != 0) {
            pthread_mutex_unlock(&g_check_mutex);
            fprintf(stderr, "\n[ERROR] Timeout waiting for checkForUpdate callback (120s)\n");
            fprintf(stderr, "        XConf query may be taking longer than expected.\n\n");
            g_exit_code = EXIT_FAILURE;
            goto cleanup_unregister;
        }
    }
    pthread_mutex_unlock(&g_check_mutex);

    /* Check result */
    printf("\n");
    if (g_check_status != FIRMWARE_AVAILABLE) {
        printf("  ⚠ No firmware update available\n");
        printf("    Status: %d\n", g_check_status);
        printf("    Current Version: %s\n", g_fw_current_version);
        
        if (g_check_status == FIRMWARE_NOT_AVAILABLE) {
            printf("    → Already on latest version. No action needed.\n\n");
            g_exit_code = EXIT_SUCCESS;
        } else {
            printf("    → Cannot proceed with update.\n\n");
            g_exit_code = EXIT_FAILURE;
        }
        goto cleanup_unregister;
    }

    printf("  ✓ Firmware update available!\n");
    printf("    Current Version  : %s\n", g_fw_current_version);
    printf("    Available Version: %s\n", g_fw_available_version);
    printf("    → Proceeding to download...\n\n");

    /* ====================================================================
     * STEP 3: Download Firmware (Async)
     * ==================================================================== */
    printf("┌─────────────────────────────────────────────────────┐\n");
    printf("│ STEP 3: Download firmware image                    │\n");
    printf("└─────────────────────────────────────────────────────┘\n");

    /* Prepare download request - use available version as firmware name */
    FwDwnlReq download_req;
    memset(&download_req, 0, sizeof(download_req));

    /* Firmware name: Use a default or derive from available version */
    snprintf(download_req.firmwareName, sizeof(download_req.firmwareName),
             "firmware_%s.bin", g_fw_available_version);
    
    /* Download URL: Empty = use daemon's XConf-provided URL */
    download_req.downloadUrl[0] = '\0';
    
    /* Type of firmware */
    strncpy(download_req.TypeOfFirmware, "PCI", sizeof(download_req.TypeOfFirmware) - 1);

    printf("  Firmware Name : %s\n", download_req.firmwareName);
    printf("  Download URL  : (use XConf URL)\n");
    printf("  Firmware Type : %s\n\n", download_req.TypeOfFirmware);

    printf("  Calling downloadFirmware()...\n\n");

    DownloadResult dl_result = downloadFirmware(g_handle, download_req, 
                                                 on_download_progress_callback);

    if (dl_result != RDKFW_DWNL_SUCCESS) {
        fprintf(stderr, "[ERROR] downloadFirmware() returned FAIL!\n\n");
        g_exit_code = EXIT_FAILURE;
        goto cleanup_unregister;
    }

    printf("  ✓ downloadFirmware() returned SUCCESS\n");
    printf("    Waiting for download progress...\n\n");
    printf("  Download Progress:\n");

    /* Wait for download completion with timeout (5 minutes) */
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 300;  /* 5 minute timeout for download */

    pthread_mutex_lock(&g_download_mutex);
    while (!g_download_done) {
        rc = pthread_cond_timedwait(&g_download_cond, &g_download_mutex, &timeout);
        if (rc != 0) {
            pthread_mutex_unlock(&g_download_mutex);
            fprintf(stderr, "[ERROR] Timeout waiting for download completion (5 min)\n\n");
            g_exit_code = EXIT_FAILURE;
            goto cleanup_unregister;
        }
    }
    pthread_mutex_unlock(&g_download_mutex);

    /* Check download result */
    if (g_download_status != DWNL_COMPLETED) {
        fprintf(stderr, "[ERROR] Download failed (status=%d)\n\n", g_download_status);
        g_exit_code = EXIT_FAILURE;
        goto cleanup_unregister;
    }

    printf("  → Download complete. Proceeding to flash...\n\n");

    /* ====================================================================
     * STEP 4: Update/Flash Firmware (Async)
     * ==================================================================== */
    printf("┌─────────────────────────────────────────────────────┐\n");
    printf("│ STEP 4: Flash firmware to device                   │\n");
    printf("└─────────────────────────────────────────────────────┘\n");

    /* Prepare update request */
    FwUpdateReq update_req;
    memset(&update_req, 0, sizeof(update_req));

    /* Must match what was downloaded */
    strncpy(update_req.firmwareName, download_req.firmwareName,
            sizeof(update_req.firmwareName) - 1);
    
    /* Must match download request */
    strncpy(update_req.TypeOfFirmware, download_req.TypeOfFirmware,
            sizeof(update_req.TypeOfFirmware) - 1);
    
    /* Location: Empty = use /etc/device.properties default path */
    update_req.LocationOfFirmware[0] = '\0';
    
    /* Reboot after flash: false for this example (so we can unregister cleanly) */
    update_req.rebootImmediately = false;

    printf("  Firmware Name  : %s\n", update_req.firmwareName);
    printf("  Firmware Type  : %s\n", update_req.TypeOfFirmware);
    printf("  Location       : (use daemon default)\n");
    printf("  Reboot Now     : %s\n\n", update_req.rebootImmediately ? "true" : "false");

    printf("  Calling updateFirmware()...\n\n");

    UpdateResult upd_result = updateFirmware(g_handle, update_req,
                                              on_update_progress_callback);

    if (upd_result != RDKFW_UPDATE_SUCCESS) {
        fprintf(stderr, "[ERROR] updateFirmware() returned FAIL!\n\n");
        g_exit_code = EXIT_FAILURE;
        goto cleanup_unregister;
    }

    printf("  ✓ updateFirmware() returned SUCCESS\n");
    printf("    Waiting for flash progress...\n\n");
    printf("  Flash Progress:\n");

    /* Wait for flash completion with timeout (10 minutes) */
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 600;  /* 10 minute timeout for flashing */

    pthread_mutex_lock(&g_update_mutex);
    while (!g_update_done) {
        rc = pthread_cond_timedwait(&g_update_cond, &g_update_mutex, &timeout);
        if (rc != 0) {
            pthread_mutex_unlock(&g_update_mutex);
            fprintf(stderr, "[ERROR] Timeout waiting for flash completion (10 min)\n\n");
            g_exit_code = EXIT_FAILURE;
            goto cleanup_unregister;
        }
    }
    pthread_mutex_unlock(&g_update_mutex);

    /* Check flash result */
    if (g_update_status != UPDATE_COMPLETED) {
        fprintf(stderr, "[ERROR] Firmware flash failed (status=%d)\n\n", g_update_status);
        g_exit_code = EXIT_FAILURE;
        goto cleanup_unregister;
    }

    printf("  → Flash complete!\n\n");

    /* ====================================================================
     * STEP 5: Unregister and Cleanup
     * ==================================================================== */
cleanup_unregister:
    printf("┌─────────────────────────────────────────────────────┐\n");
    printf("│ STEP 5: Unregister from daemon                     │\n");
    printf("└─────────────────────────────────────────────────────┘\n");

    if (g_handle != NULL) {
        printf("  Calling unregisterProcess()...\n");
        unregisterProcess(g_handle);
        g_handle = NULL;
        printf("  ✓ Unregistered successfully\n\n");
    }

    /* ====================================================================
     * Final Status
     * ==================================================================== */
    printf("╔═══════════════════════════════════════════════════════╗\n");
    if (g_exit_code == EXIT_SUCCESS) {
        printf("║   ✓ FIRMWARE UPDATE WORKFLOW COMPLETED               ║\n");
        printf("╚═══════════════════════════════════════════════════════╝\n\n");
        if (g_update_status == UPDATE_COMPLETED) {
            printf("  ⚠ NOTE: Firmware flashed successfully.\n");
            printf("          System reboot required to activate new firmware.\n");
            printf("          Use: systemctl reboot\n\n");
        }
    } else {
        printf("║   ✗ FIRMWARE UPDATE WORKFLOW FAILED                  ║\n");
        printf("╚═══════════════════════════════════════════════════════╝\n\n");
        printf("  Check logs for details:\n");
        printf("    tail -f /opt/logs/rdkFwupdateMgr.log\n\n");
    }

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
