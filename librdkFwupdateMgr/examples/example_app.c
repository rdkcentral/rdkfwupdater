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
 * @brief Example application using rdkFwupdateMgr client library
 *
 * COMPLETE FLOW:
 *   1. registerProcess()   → synchronous D-Bus call → get handle from daemon
 *   2. checkForUpdate()    → non-blocking, returns SUCCESS/FAIL immediately
 *   3. [background thread] → library receives CheckForUpdateComplete signal
 *   4. on_firmware_event() → callback fires, prints result
 *   5. unregisterProcess() → synchronous cleanup with daemon
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

#include "rdkFwupdateMgr_process.h"   /* registerProcess(), unregisterProcess(),
                                         FirmwareInterfaceHandle               */
//#include "rdkFwupdateMgr_client.h"    /* checkForUpdate(), CheckForUpdateResult,
 //                                        CheckForUpdateStatus, FwUpdateEventData */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>

/* ========================================================================
 * SYNCHRONISATION
 *
 * checkForUpdate() returns immediately. The callback fires later from the
 * library's background thread. We use a mutex+condvar so main() can wait
 * until the callback has completed before calling unregisterProcess().
 *
 * In a real plugin you do NOT need this — you return to your event loop
 * and let the callback arrive naturally. This is only here to keep the
 * example process alive and sequenced correctly.
 * ======================================================================== */

static pthread_mutex_t g_done_mutex    = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_done_cond     = PTHREAD_COND_INITIALIZER;
static int             g_callback_done = 0;   /* 0 = waiting, 1 = received */

/* ========================================================================
 * CALLBACK
 * ======================================================================== */

/**
 * @brief Invoked by the library when CheckForUpdateComplete signal arrives
 *
 * This function runs in the library's background D-Bus signal thread,
 * NOT in the main thread.
 *
 * Signature is exactly UpdateEventCallback:
 *   void fn(FirmwareInterfaceHandle, const FwUpdateEventData *)
 *
 * @param handle     The handle that initiated the checkForUpdate call
 * @param event_data Firmware check result — valid ONLY during this call
 */
static void on_firmware_event(FirmwareInterfaceHandle handle,
                               const FwUpdateEventData *event_data)
{
    printf("\n");
    printf("┌─────────────────────────────────────────────────────┐\n");
    printf("│         CheckForUpdate Callback Received            │\n");
    printf("└─────────────────────────────────────────────────────┘\n");

    /* Handle — the handler_id string from registerProcess */
    printf("  Handle (handler_id) : %s\n", handle ? handle : "(null)");
    printf("  Update Available    : %s\n",
           event_data->update_available ? "YES" : "NO");

    /* Map status enum to readable string */
    const char *status_str = "UNKNOWN";
    switch (event_data->status) {
        case FIRMWARE_AVAILABLE:     status_str = "FIRMWARE_AVAILABLE";     break;
        case FIRMWARE_NOT_AVAILABLE: status_str = "FIRMWARE_NOT_AVAILABLE"; break;
        case UPDATE_NOT_ALLOWED:     status_str = "UPDATE_NOT_ALLOWED";     break;
        case FIRMWARE_CHECK_ERROR:   status_str = "FIRMWARE_CHECK_ERROR";   break;
        case IGNORE_OPTOUT:          status_str = "IGNORE_OPTOUT";          break;
        case BYPASS_OPTOUT:          status_str = "BYPASS_OPTOUT";          break;
    }
    printf("  Status              : %s (%d)\n", status_str, event_data->status);

    /* Version strings — may be NULL if daemon did not provide them */
    printf("  Current Version     : %s\n",
           event_data->current_version   ? event_data->current_version   : "(not provided)");
    printf("  Available Version   : %s\n",
           event_data->available_version ? event_data->available_version : "(not provided)");
    printf("  Status Message      : %s\n",
           event_data->status_message    ? event_data->status_message    : "(not provided)");

    /* What the app should do next based on status */
    printf("\n  Next Action:\n");
    switch (event_data->status) {
        case FIRMWARE_AVAILABLE:
            printf("  → New firmware available. Schedule downloadFirmware().\n");
            break;
        case FIRMWARE_NOT_AVAILABLE:
            printf("  → Already on latest firmware. No action needed.\n");
            break;
        case UPDATE_NOT_ALLOWED:
            printf("  → Device is in exclusion list. Update blocked by policy.\n");
            break;
        case FIRMWARE_CHECK_ERROR:
            printf("  → Check failed. Will retry on next scheduled interval.\n");
            break;
        case IGNORE_OPTOUT:
        case BYPASS_OPTOUT:
            printf("  → Device has opted out. Download not allowed.\n");
            break;
    }

    printf("└─────────────────────────────────────────────────────┘\n\n");

    /*
     * IMPORTANT: Do NOT use event_data after this function returns.
     * The library frees all string fields immediately after this call.
     * If you need any strings later, strdup() them here.
     *
     * Example: char *ver = strdup(event_data->available_version);
     */

    /* Wake up main() so it can proceed to unregisterProcess() */
    pthread_mutex_lock(&g_done_mutex);
    g_callback_done = 1;
    pthread_cond_signal(&g_done_cond);
    pthread_mutex_unlock(&g_done_mutex);
}

/* ========================================================================
 * MAIN
 * ======================================================================== */

int main(void)
{
    int exit_code = EXIT_SUCCESS;

    printf("┌─────────────────────────────────────────────────────┐\n");
    printf("│      RDK Firmware Update Manager — Example App     │\n");
    printf("└─────────────────────────────────────────────────────┘\n\n");

    /* ----------------------------------------------------------------
     * STEP 1: Register with the daemon
     *
     * registerProcess() is SYNCHRONOUS.
     * It sends RegisterProcess(processName, libVersion) over D-Bus and
     * waits for the daemon to respond with a uint64 handler_id.
     *
     * The returned handle is a malloc'd string like "12345".
     *   - Library-owned: do NOT call free() on it yourself.
     *   - It is freed internally inside unregisterProcess().
     *   - Set to NULL after unregisterProcess() to avoid use-after-free.
     * ---------------------------------------------------------------- */
    printf("[Step 1] Registering with firmware daemon...\n");
    printf("         processName = 'ExampleApp'\n");
    printf("         libVersion  = '1.0.0'\n");

    FirmwareInterfaceHandle handle = registerProcess("ExampleApp", "1.0.0");

    if (handle == FIRMWARE_INVALID_HANDLE) {
        fprintf(stderr, "\n[ERROR] registerProcess() returned NULL.\n");
        fprintf(stderr, "        Ensure rdkFwupdateMgr daemon is running.\n");
        fprintf(stderr, "        Check: systemctl status rdkFwupdateMgr\n");
        return EXIT_FAILURE;
    }

    printf("[Step 1] ✓ Registered successfully.\n");
    printf("           handle (handler_id string) = '%s'\n\n", handle);

    /* ----------------------------------------------------------------
     * STEP 2: Call checkForUpdate (non-blocking)
     *
     * checkForUpdate(handle, callback) does two things and returns:
     *   a) Registers on_firmware_event in the library's internal registry
     *      keyed by the handle string.
     *   b) Sends CheckForUpdate(handle) D-Bus method call to daemon
     *      (fire-and-forget — does not wait for reply).
     *
     * Returns CHECK_FOR_UPDATE_SUCCESS or CHECK_FOR_UPDATE_FAIL.
     * The actual firmware result comes later via the callback.
     * ---------------------------------------------------------------- */
    printf("[Step 2] Calling checkForUpdate()...\n");

    CheckForUpdateResult cfu_result = checkForUpdate(handle, on_firmware_event);

    if (cfu_result == CHECK_FOR_UPDATE_FAIL) {
        fprintf(stderr, "\n[ERROR] checkForUpdate() returned FAIL.\n");
        fprintf(stderr, "        Possible reasons:\n");
        fprintf(stderr, "          - D-Bus connection error\n");
        fprintf(stderr, "          - Library callback registry is full\n");
        exit_code = EXIT_FAILURE;
        goto unregister;
    }

    printf("[Step 2] ✓ checkForUpdate() returned CHECK_FOR_UPDATE_SUCCESS.\n");
    printf("           Callback registered. Waiting for daemon signal...\n\n");

    /* ----------------------------------------------------------------
     * STEP 3: Wait for the callback
     *
     * The library's background thread is subscribed to the
     * CheckForUpdateComplete D-Bus signal. When the daemon emits it,
     * on_firmware_event() fires and signals g_done_cond.
     *
     * We wait with a 60-second timeout as a safety net.
     *
     * In a real plugin: return to your event loop here.
     * The callback arrives whenever the daemon is ready.
     * ---------------------------------------------------------------- */
    printf("[Step 3] Waiting for CheckForUpdateComplete signal (timeout: 60s)...\n");

    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += 60;

    pthread_mutex_lock(&g_done_mutex);
    while (!g_callback_done) {
        int rc = pthread_cond_timedwait(&g_done_cond, &g_done_mutex, &deadline);
        if (rc != 0) {
            printf("[Step 3] Timed out — daemon did not respond within 60 seconds.\n");
            pthread_mutex_unlock(&g_done_mutex);
            exit_code = EXIT_FAILURE;
            goto unregister;
        }
    }
    pthread_mutex_unlock(&g_done_mutex);

    printf("[Step 3] ✓ Callback completed.\n\n");

    /* ----------------------------------------------------------------
     * STEP 4: Unregister from the daemon
     *
     * unregisterProcess() is SYNCHRONOUS.
     * It sends UnregisterProcess(handler_id) to the daemon and then
     * frees the handle memory internally.
     *
     * After this call:
     *   - handle memory is FREED — do not dereference it.
     *   - Set handle = NULL immediately.
     *   - Any further checkForUpdate() with this handle will fail.
     * ---------------------------------------------------------------- */
unregister:
    printf("[Step 4] Unregistering from firmware daemon...\n");

    unregisterProcess(handle);
    handle = NULL;     /* ← MUST set to NULL — handle is freed inside */

    printf("[Step 4] ✓ Unregistered. Handle set to NULL.\n\n");

    if (exit_code == EXIT_SUCCESS) {
        printf("┌─────────────────────────────────────────────────────┐\n");
        printf("│                 App completed OK                   │\n");
        printf("└─────────────────────────────────────────────────────┘\n");
    } else {
        printf("┌─────────────────────────────────────────────────────┐\n");
        printf("│              App completed with errors              │\n");
        printf("└─────────────────────────────────────────────────────┘\n");
    }

    return exit_code;
}

/* ═══════════════════════════════════════════════════════════════════════
 * SEQUENCE DIAGRAM
 * ═══════════════════════════════════════════════════════════════════════
 *
 *  main thread                  Library internals            Daemon
 *  ───────────                  ─────────────────            ──────
 *
 *  registerProcess("ExampleApp", "1.0.0")
 *    │─── RegisterProcess("ExampleApp","1.0.0") ────────────► │
 *    │◄── (t) handler_id = 12345 ───────────────────────────── │
 *    handle = "12345"
 *
 *  checkForUpdate("12345", on_firmware_event)
 *    │  registry: slot[0] = { PENDING, "12345", on_firmware_event }
 *    │─── CheckForUpdate("12345") ──────────────────────────► │
 *    │  (returns immediately)                                  │
 *  result = CHECK_FOR_UPDATE_SUCCESS                           │
 *                                                              │  [XConf query...]
 *  pthread_cond_timedwait()  ← main blocks here               │
 *                                                              │
 *                     bg thread ◄── CheckForUpdateComplete signal ──│
 *                       parse signal data
 *                       dispatch_all_pending():
 *                         on_firmware_event("12345", &event_data)
 *                           [prints result]
 *                           [pthread_cond_signal()]
 *                         slot[0] → IDLE
 *
 *  [main wakes up]
 *
 *  unregisterProcess("12345")
 *    │─── UnregisterProcess(t=12345) ───────────────────────► │
 *    │◄── (b) success = TRUE ───────────────────────────────── │
 *    free("12345")
 *  handle = NULL
 *
 * ═══════════════════════════════════════════════════════════════════════
 */




/* ========================================================================
 * DOWNLOAD FIRMWARE EXAMPLE
 * ========================================================================
 *
 * Demonstrates calling downloadFirmware() after a successful checkForUpdate.
 * In a real plugin, you would call this from your on_firmware_event callback
 * when status == FIRMWARE_AVAILABLE.
 *
 * This section is a standalone function you can call from main()
 * after checkForUpdate confirms an update is available.
 * ======================================================================== */

/* Progress tracking — module level (no user_data in DownloadCallback) */
static int             g_download_done   = 0;
static pthread_mutex_t g_dwnl_mutex      = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_dwnl_cond       = PTHREAD_COND_INITIALIZER;

/**
 * @brief DownloadCallback — fires on every DownloadProgress signal
 *
 * Strict signature: void fn(int progress_per, DownloadStatus fwdwnlstatus)
 * No handle, no user_data — matches the DownloadCallback typedef exactly.
 *
 * Called from background thread. Do not block here.
 */
static void on_download_progress(int progress_per, DownloadStatus fwdwnlstatus)
{
    const char *status_str = "UNKNOWN";
    switch (fwdwnlstatus) {
        case DWNL_IN_PROGRESS: status_str = "DWNL_IN_PROGRESS"; break;
        case DWNL_COMPLETED:   status_str = "DWNL_COMPLETED";   break;
        case DWNL_ERROR:       status_str = "DWNL_ERROR";       break;
    }

    /* Simple progress bar */
    int bar_filled = progress_per / 5;   /* 20 chars = 100% */
    printf("  [");
    for (int i = 0; i < 20; i++) printf(i < bar_filled ? "█" : "░");
    printf("] %3d%%  %s\n", progress_per, status_str);

    /* Wake main() when download ends */
    if (fwdwnlstatus == DWNL_COMPLETED || fwdwnlstatus == DWNL_ERROR) {
        if (fwdwnlstatus == DWNL_COMPLETED) {
            printf("\n  ✓ Download complete!\n");
        } else {
            printf("\n  ✗ Download failed.\n");
        }

        pthread_mutex_lock(&g_dwnl_mutex);
        g_download_done = 1;
        pthread_cond_signal(&g_dwnl_cond);
        pthread_mutex_unlock(&g_dwnl_mutex);
    }
}

/**
 * @brief Run the full downloadFirmware flow
 *
 * Call this from main() after checkForUpdate returns FIRMWARE_AVAILABLE.
 * Demonstrates: fill FwDwnlReq → call downloadFirmware → wait for callbacks.
 *
 * @param handle  The same handle used for checkForUpdate
 */
void run_download_example(FirmwareInterfaceHandle handle)
{
    printf("\n┌─────────────────────────────────────────────────────┐\n");
    printf("│              downloadFirmware Example               │\n");
    printf("└─────────────────────────────────────────────────────┘\n\n");

    /* ---- Fill FwDwnlReq ---- */
    FwDwnlReq req;
    memset(&req, 0, sizeof(req));

    /* firmwareName: required */
    strncpy(req.firmwareName, "RNGUX_4.5.1_VBN.bin", sizeof(req.firmwareName) - 1);

    /* downloadUrl: optional — leave empty to use XConf URL */
    req.downloadUrl[0] = '\0';   /* "" = use daemon's XConf-provided URL */

    /* TypeOfFirmware: PCI | PDRI | PERIPHERAL */
    strncpy(req.TypeOfFirmware, "PCI", sizeof(req.TypeOfFirmware) - 1);

    printf("[Download] firmwareName   = '%s'\n", req.firmwareName);
    printf("[Download] downloadUrl    = '%s'\n",
           req.downloadUrl[0] ? req.downloadUrl : "(empty — use XConf URL)");
    printf("[Download] TypeOfFirmware = '%s'\n\n", req.TypeOfFirmware);

    /* ---- Call downloadFirmware (non-blocking) ---- */
    printf("[Download] Calling downloadFirmware()...\n");

    DownloadResult result = downloadFirmware(handle, req, on_download_progress);

    if (result == RDKFW_DWNL_FAILED) {
        fprintf(stderr, "[Download] ERROR: downloadFirmware() returned FAIL\n");
        return;
    }

    printf("[Download] Returned RDKFW_DWNL_SUCCESS — waiting for progress signals...\n\n");
    printf("  Progress:\n");

    /* ---- Wait for download to complete ---- */
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += 300;   /* 5 minute timeout for download */

    pthread_mutex_lock(&g_dwnl_mutex);
    while (!g_download_done) {
        int rc = pthread_cond_timedwait(&g_dwnl_cond, &g_dwnl_mutex, &deadline);
        if (rc != 0) {
            printf("\n[Download] Timed out waiting for download completion.\n");
            pthread_mutex_unlock(&g_dwnl_mutex);
            return;
        }
    }
    pthread_mutex_unlock(&g_dwnl_mutex);

    printf("[Download] Download flow complete.\n");
}

/*
 * ═══════════════════════════════════════════════════════════════
 * HOW TO USE run_download_example() FROM main():
 * ═══════════════════════════════════════════════════════════════
 *
 *   // After checkForUpdate callback confirms FIRMWARE_AVAILABLE:
 *   run_download_example(handle);
 *
 * OR integrate directly into on_firmware_event():
 *
 *   static void on_firmware_event(FirmwareInterfaceHandle handle,
 *                                  const FwUpdateEventData *event_data)
 *   {
 *       if (event_data->status == FIRMWARE_AVAILABLE) {
 *           // Do NOT call downloadFirmware() directly here —
 *           // this is the background thread and you'd block it.
 *           // Instead: set a flag and call it from your main event loop.
 *           g_firmware_available = 1;
 *           pthread_cond_signal(&g_fw_available_cond);
 *       }
 *   }
 * ═══════════════════════════════════════════════════════════════
 *
 * DOWNLOAD SIGNAL FLOW:
 * ═══════════════════════════════════════════════════════════════
 *
 *  main thread           Library bg thread         Daemon
 *  ───────────           ─────────────────         ──────
 *
 *  downloadFirmware()
 *    register callback ──► dwnl_registry slot=ACTIVE
 *    D-Bus call ───────────────────────────────────► DownloadFirmware(...)
 *    return RDKFW_DWNL_SUCCESS
 *
 *  [main waits on condvar]
 *                                                    [downloading... 1%]
 *                        ◄── DownloadProgress(1, IN_PROGRESS) signal ──
 *                        on_download_progress(1, DWNL_IN_PROGRESS)
 *                        → prints progress bar     slot stays ACTIVE
 *
 *                                                    [downloading... 50%]
 *                        ◄── DownloadProgress(50, IN_PROGRESS) signal ──
 *                        on_download_progress(50, DWNL_IN_PROGRESS)
 *                        → prints progress bar     slot stays ACTIVE
 *
 *                                                    [download done]
 *                        ◄── DownloadProgress(100, COMPLETED) signal ──
 *                        on_download_progress(100, DWNL_COMPLETED)
 *                        → prints "complete"
 *                        → pthread_cond_signal()   slot → IDLE
 *
 *  [main wakes up]
 *  unregisterProcess()
 * ═══════════════════════════════════════════════════════════════
 */
/* ========================================================================
 * UPDATE FIRMWARE EXAMPLE
 * ========================================================================
 *
 * Demonstrates calling updateFirmware() after downloadFirmware completes.
 *
 * Full sequence in a real app:
 *   checkForUpdate  → FIRMWARE_AVAILABLE
 *   downloadFirmware → DWNL_COMPLETED
 *   updateFirmware   → UPDATE_COMPLETED  ← this section
 * ======================================================================== */

/* Synchronisation — same pattern used for checkForUpdate and downloadFirmware */
static int             g_update_done  = 0;
static pthread_mutex_t g_upd_mutex    = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_upd_cond     = PTHREAD_COND_INITIALIZER;

/**
 * @brief UpdateCallback — fires on every UpdateProgress signal
 *
 * Strict signature: void fn(int progress_per, UpdateStatus fwupdatestatus)
 * Runs in library background thread. Do not block here.
 *
 * @param progress_per      Flash completion percentage (0–100)
 * @param fwupdatestatus    Current flashing state
 */
static void on_update_progress(int progress_per, UpdateStatus fwupdatestatus)
{
    const char *status_str = "UNKNOWN";
    switch (fwupdatestatus) {
        case UPDATE_IN_PROGRESS: status_str = "UPDATE_IN_PROGRESS"; break;
        case UPDATE_COMPLETED:   status_str = "UPDATE_COMPLETED";   break;
        case UPDATE_ERROR:       status_str = "UPDATE_ERROR";       break;
    }

    /* Progress bar — 20 chars wide = 100% */
    int filled = progress_per / 5;
    printf("  [");
    for (int i = 0; i < 20; i++) printf(i < filled ? "▓" : "░");
    printf("] %3d%%  %s\n", progress_per, status_str);

    /* On terminal states, wake main() */
    if (fwupdatestatus == UPDATE_COMPLETED || fwupdatestatus == UPDATE_ERROR) {
        if (fwupdatestatus == UPDATE_COMPLETED)
            printf("\n  ✓ Firmware flashed successfully!\n");
        else
            printf("\n  ✗ Firmware flash failed.\n");

        pthread_mutex_lock(&g_upd_mutex);
        g_update_done = 1;
        pthread_cond_signal(&g_upd_cond);
        pthread_mutex_unlock(&g_upd_mutex);
    }
}

/**
 * @brief Run the full updateFirmware flow
 *
 * Call this from main() after downloadFirmware completes with DWNL_COMPLETED.
 *
 * @param handle    The same handle used for checkForUpdate / downloadFirmware
 */
void run_update_example(FirmwareInterfaceHandle handle)
{
    printf("\n┌─────────────────────────────────────────────────────┐\n");
    printf("│               updateFirmware Example               │\n");
    printf("└─────────────────────────────────────────────────────┘\n\n");

    /* ---- Fill FwUpdateReq ---- */
    FwUpdateReq req;
    memset(&req, 0, sizeof(req));

    /* firmwareName: required — must match what was downloaded */
    strncpy(req.firmwareName, "RNGUX_4.5.1_VBN.bin", sizeof(req.firmwareName) - 1);

    /* TypeOfFirmware: required — PCI | PDRI | PERIPHERAL */
    strncpy(req.TypeOfFirmware, "PCI", sizeof(req.TypeOfFirmware) - 1);

    /*
     * LocationOfFirmware: OPTIONAL
     *   - Empty string ("") → daemon uses default path from /etc/device.properties
     *   - Set a path if the image is in a non-default location
     */
    req.LocationOfFirmware[0] = '\0';   /* "" = use /etc/device.properties path */

    /*
     * rebootImmediately:
     *   true  → daemon reboots device immediately after flash completes
     *   false → app decides when to reboot
     */
    req.rebootImmediately = true;

    printf("[Update] firmwareName       = '%s'\n", req.firmwareName);
    printf("[Update] TypeOfFirmware     = '%s'\n", req.TypeOfFirmware);
    printf("[Update] LocationOfFirmware = '%s'\n",
           req.LocationOfFirmware[0]
               ? req.LocationOfFirmware
               : "(empty — daemon uses /etc/device.properties)");
    printf("[Update] rebootImmediately  = %s\n\n",
           req.rebootImmediately ? "true" : "false");

    /* ---- Call updateFirmware (non-blocking) ---- */
    printf("[Update] Calling updateFirmware()...\n");

    UpdateResult result = updateFirmware(handle, req, on_update_progress);

    if (result == RDKFW_UPDATE_FAILED) {
        fprintf(stderr, "[Update] ERROR: updateFirmware() returned FAIL\n");
        fprintf(stderr, "         Possible reasons:\n");
        fprintf(stderr, "           - Invalid handle\n");
        fprintf(stderr, "           - Empty firmwareName or TypeOfFirmware\n");
        fprintf(stderr, "           - Library update registry is full\n");
        fprintf(stderr, "           - D-Bus connection error\n");
        return;
    }

    printf("[Update] Returned RDKFW_UPDATE_SUCCESS — "
           "waiting for flash progress signals...\n\n");
    printf("  Flash Progress:\n");

    /* ---- Wait for flash to complete ---- */
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += 600;   /* 10 minute timeout — flashing can be slow */

    pthread_mutex_lock(&g_upd_mutex);
    while (!g_update_done) {
        int rc = pthread_cond_timedwait(&g_upd_cond, &g_upd_mutex, &deadline);
        if (rc != 0) {
            printf("\n[Update] Timed out waiting for flash completion (10 min).\n");
            pthread_mutex_unlock(&g_upd_mutex);
            return;
        }
    }
    pthread_mutex_unlock(&g_upd_mutex);

    printf("[Update] updateFirmware flow complete.\n");
    if (req.rebootImmediately) {
        printf("[Update] rebootImmediately=true — "
               "device will reboot shortly.\n");
        printf("[Update] Do not call unregisterProcess() — "
               "process will not survive the reboot.\n");
    }
}

/*
 * ═══════════════════════════════════════════════════════════════
 * HOW TO USE run_update_example() FROM main():
 * ═══════════════════════════════════════════════════════════════
 *
 *   // After downloadFirmware callback confirms DWNL_COMPLETED:
 *   run_update_example(handle);
 *
 * ═══════════════════════════════════════════════════════════════
 * COMPLETE THREE-STEP SEQUENCE IN main():
 * ═══════════════════════════════════════════════════════════════
 *
 *   FirmwareInterfaceHandle handle = registerProcess("MyApp", "1.0.0");
 *
 *   // Step 1 — check
 *   checkForUpdate(handle, on_firmware_event);
 *   // [wait] on_firmware_event → status == FIRMWARE_AVAILABLE
 *
 *   // Step 2 — download
 *   run_download_example(handle);
 *   // [wait] on_download_progress(100, DWNL_COMPLETED)
 *
 *   // Step 3 — flash
 *   run_update_example(handle);
 *   // [wait] on_update_progress(100, UPDATE_COMPLETED)
 *   // → device reboots (if rebootImmediately = true)
 *
 *   unregisterProcess(handle);   // only if rebootImmediately = false
 *   handle = NULL;
 *
 * ═══════════════════════════════════════════════════════════════
 * UPDATE SIGNAL FLOW:
 * ═══════════════════════════════════════════════════════════════
 *
 *  main thread           Library bg thread           Daemon
 *  ───────────           ─────────────────           ──────
 *
 *  updateFirmware()
 *    register callback ──► update_registry slot=ACTIVE
 *    D-Bus call ──────────────────────────────────► UpdateFirmware(...)
 *    return RDKFW_UPDATE_SUCCESS
 *
 *  [main waits on condvar]
 *                                                    [flashing... 1%]
 *                        ◄── UpdateProgress(1, UPDATE_IN_PROGRESS) ──
 *                        on_update_progress(1, UPDATE_IN_PROGRESS)
 *                        → prints progress bar       slot stays ACTIVE
 *
 *                                                    [flashing... 50%]
 *                        ◄── UpdateProgress(50, UPDATE_IN_PROGRESS) ──
 *                        on_update_progress(50, UPDATE_IN_PROGRESS)
 *                        → prints progress bar       slot stays ACTIVE
 *
 *                                                    [flash done]
 *                        ◄── UpdateProgress(100, UPDATE_COMPLETED) ──
 *                        on_update_progress(100, UPDATE_COMPLETED)
 *                        → prints "flashed"
 *                        → pthread_cond_signal()     slot → IDLE
 *
 *  [main wakes up]
 *  (device reboots if rebootImmediately=true)
 * ═══════════════════════════════════════════════════════════════
 */

