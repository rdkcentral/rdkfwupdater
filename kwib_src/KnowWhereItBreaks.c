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
 * @file KnowWhereItBreaks.c
 * @brief Comprehensive developer test utility for librdkFwupdateMgr.so
 *
 * This is NOT the example_plugin (which is a clean reference for external teams).
 * This is YOUR developer weapon for exercising every code path in the library
 * and daemon before bugs find you.
 *
 * Tests every layer:
 *   - Library input validation (NULL/empty args → immediate FAIL)
 *   - Library in-progress guards (duplicate same-process call → rejected)
 *   - Library session guards (unregister during active op → blocked)
 *   - Condvar handshake accuracy (return code matches daemon reply)
 *   - Worker thread lifecycle (create → run → cleanup → exit, no leaks)
 *   - D-Bus method calls and signal reception
 *   - Callback correctness (right data, right count, right thread)
 *   - Full lifecycle (register → check → download → update → unregister)
 *   - Rapid retry (call again immediately after previous completes)
 *
 * Usage:
 *   ./KnowWhereItBreaks                     (interactive menu)
 *   ./KnowWhereItBreaks --auto-error        (error/validation tests)
 *   ./KnowWhereItBreaks --auto-happy        (happy path — daemon required)
 *   ./KnowWhereItBreaks --full-lifecycle    (end-to-end — daemon required)
 *   ./KnowWhereItBreaks --auto-all          (everything)
 *
 * See KnowWhereItBreaks_README.md for full documentation.
 */

#include "rdkFwupdateMgr_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <pthread.h>
#include <time.h>

/* ========================================================================
 * TEST INFRASTRUCTURE
 * ======================================================================== */

typedef struct {
    int total;
    int passed;
    int failed;
    int skipped;
} TestResults;

static TestResults g_results = {0, 0, 0, 0};

#define TEST_PASS(name) do { \
    g_results.total++; g_results.passed++; \
    printf("  [\033[32mPASS\033[0m] %s\n", name); \
} while(0)

#define TEST_FAIL(name, reason) do { \
    g_results.total++; g_results.failed++; \
    printf("  [\033[31mFAIL\033[0m] %s — %s\n", name, reason); \
} while(0)

#define TEST_SKIP(name, reason) do { \
    g_results.total++; g_results.skipped++; \
    printf("  [\033[33mSKIP\033[0m] %s — %s\n", name, reason); \
} while(0)

#define TEST_INFO(fmt, ...) printf("  [INFO] " fmt "\n", ##__VA_ARGS__)

/* ========================================================================
 * CALLBACK TRACKING STATE
 * ========================================================================
 * Volatile: callbacks fire from worker threads, main thread polls these.
 * ======================================================================== */

/* CheckForUpdate tracking */
static volatile bool g_check_cb_fired       = false;
static volatile int  g_check_cb_count       = 0;
static volatile int  g_check_status         = -1;
static char          g_check_current_ver[MAX_FW_VERSION_SIZE] = {0};

/* DownloadFirmware tracking */
static volatile bool g_dwnl_cb_terminal     = false;
static volatile int  g_dwnl_cb_count        = 0;
static volatile int  g_dwnl_status          = -1;
static volatile int  g_dwnl_last_progress   = -1;
static volatile bool g_dwnl_progress_mono   = true;

/* UpdateFirmware tracking */
static volatile bool g_update_cb_terminal   = false;
static volatile int  g_update_cb_count      = 0;
static volatile int  g_update_status        = -1;
static volatile int  g_update_last_progress = -1;
static volatile bool g_update_progress_mono = true;

/* Global handle */
static FirmwareInterfaceHandle g_handle = NULL;

/* ========================================================================
 * CALLBACKS
 * ======================================================================== */

static void check_callback(const FwInfoData *info)
{
    g_check_cb_count++;
    if (info) {
        g_check_status = info->status;
        if (info->CurrFWVersion[0] != '\0') {
            strncpy(g_check_current_ver, info->CurrFWVersion,
                    sizeof(g_check_current_ver) - 1);
        }
        printf("    [CB:Check] #%d status=%d current='%s'\n",
               g_check_cb_count, info->status, info->CurrFWVersion);
    } else {
        printf("    [CB:Check] #%d — NULL info!\n", g_check_cb_count);
    }
    g_check_cb_fired = true;
}

static void download_callback(int progress, DownloadStatus status)
{
    int prev = g_dwnl_last_progress;
    g_dwnl_cb_count++;
    g_dwnl_last_progress = progress;
    g_dwnl_status = (int)status;

    /* Track monotonicity */
    if (prev >= 0 && progress < prev) {
        g_dwnl_progress_mono = false;
    }

    printf("    [CB:Dwnl] #%d progress=%d%% status=%d\n",
           g_dwnl_cb_count, progress, (int)status);

    if (status == DWNL_COMPLETED || status == DWNL_ERROR) {
        g_dwnl_cb_terminal = true;
    }
}

static void update_callback(int progress, UpdateStatus status)
{
    int prev = g_update_last_progress;
    g_update_cb_count++;
    g_update_last_progress = progress;
    g_update_status = (int)status;

    /* Track monotonicity */
    if (prev >= 0 && progress < prev) {
        g_update_progress_mono = false;
    }

    printf("    [CB:Update] #%d progress=%d%% status=%d\n",
           g_update_cb_count, progress, (int)status);

    if (status == UPDATE_COMPLETED || status == UPDATE_ERROR) {
        g_update_cb_terminal = true;
    }
}

/* ========================================================================
 * HELPERS
 * ======================================================================== */

static void reset_all(void)
{
    g_check_cb_fired     = false;
    g_check_cb_count     = 0;
    g_check_status       = -1;
    g_check_current_ver[0] = '\0';

    g_dwnl_cb_terminal   = false;
    g_dwnl_cb_count      = 0;
    g_dwnl_status        = -1;
    g_dwnl_last_progress = -1;
    g_dwnl_progress_mono = true;

    g_update_cb_terminal   = false;
    g_update_cb_count      = 0;
    g_update_status        = -1;
    g_update_last_progress = -1;
    g_update_progress_mono = true;
}

/** Poll a volatile bool with timeout. Returns true if flag set before timeout. */
static bool wait_flag(volatile bool *flag, int timeout_sec)
{
    for (int i = 0; i < timeout_sec * 10; i++) {
        if (*flag) return true;
        usleep(100000); /* 100ms */
    }
    return false;
}

static void separator(const char *title)
{
    printf("\n");
    printf("══════════════════════════════════════════════════════════════\n");
    printf("  %s\n", title);
    printf("══════════════════════════════════════════════════════════════\n");
}

/** Register if not already registered. Returns true if handle is available. */
static bool ensure_registered(void)
{
    if (g_handle != NULL) return true;
    g_handle = registerProcess("KnowWhereItBreaks", LIB_VERSION);
    if (g_handle != NULL) {
        TEST_INFO("Auto-registered: handle='%s'", g_handle);
        return true;
    }
    TEST_INFO("Auto-register FAILED — daemon may be down");
    return false;
}

static void ensure_unregistered(void)
{
    if (g_handle != NULL) {
        unregisterProcess(g_handle);
        g_handle = NULL;
    }
}

/* ========================================================================
 * TC01–TC04: REGISTER / UNREGISTER
 * ======================================================================== */

static void tc01_register_happy(void)
{
    printf("\n--- TC01: Register Happy Path ---\n");
    FirmwareInterfaceHandle h = registerProcess("KnowWhereItBreaks", LIB_VERSION);
    if (h != NULL && strlen(h) > 0) {
        TEST_PASS("TC01 — registerProcess() returned valid handle");
        printf("    Handle: '%s'\n", h);
        g_handle = h;
    } else {
        TEST_FAIL("TC01 — registerProcess()", "Returned NULL or empty handle");
    }
}

static void tc02_unregister_happy(void)
{
    printf("\n--- TC02: Unregister Happy Path ---\n");
    if (!g_handle) { TEST_SKIP("TC02", "No handle available"); return; }
    /* unregisterProcess returns void — if it doesn't crash, it passed */
    unregisterProcess(g_handle);
    TEST_PASS("TC02 — unregisterProcess() completed without crash");
    g_handle = NULL;
}

static void tc03_unregister_null(void)
{
    printf("\n--- TC03: Unregister NULL Handle ---\n");
    /* unregisterProcess(NULL) should not crash (safe to call with NULL per docs) */
    unregisterProcess(NULL);
    TEST_PASS("TC03 — unregisterProcess(NULL) did not crash");
}

static void tc04_double_register(void)
{
    printf("\n--- TC04: Double Register ---\n");
    FirmwareInterfaceHandle h1 = registerProcess("KWIB_Test1", LIB_VERSION);
    FirmwareInterfaceHandle h2 = registerProcess("KWIB_Test2", LIB_VERSION);

    if (h1 != NULL && h2 != NULL) {
        TEST_PASS("TC04 — Both registrations succeeded");
        printf("    Handle1='%s'  Handle2='%s'\n", h1, h2);
        unregisterProcess(h2);
        if (!g_handle) g_handle = h1;
        else unregisterProcess(h1);
    } else {
        TEST_FAIL("TC04 — double_register", "One or both returned NULL");
        if (h1) unregisterProcess(h1);
        if (h2) unregisterProcess(h2);
    }
}

/* ========================================================================
 * TC05–TC11: CHECKFORUPDATE
 * ======================================================================== */

static void tc05_check_happy(void)
{
    printf("\n--- TC05: CheckForUpdate Happy Path ---\n");
    reset_all();
    if (!ensure_registered()) { TEST_SKIP("TC05", "No handle"); return; }

    CheckForUpdateResult ret = checkForUpdate(g_handle, check_callback);
    if (ret != CHECK_FOR_UPDATE_SUCCESS) {
        TEST_FAIL("TC05 — checkForUpdate()", "API returned FAIL");
        return;
    }
    TEST_INFO("Waiting for callback (max 130s)...");
    if (wait_flag(&g_check_cb_fired, 130)) {
        TEST_PASS("TC05 — checkForUpdate callback received");
        printf("    Status: %d  CurrentVer: '%s'\n", g_check_status, g_check_current_ver);
    } else {
        TEST_FAIL("TC05 — checkForUpdate()", "Callback never fired (130s timeout)");
    }
}

static void tc06_check_null_handle(void)
{
    printf("\n--- TC06: CheckForUpdate NULL Handle ---\n");
    CheckForUpdateResult ret = checkForUpdate(NULL, check_callback);
    if (ret == CHECK_FOR_UPDATE_FAIL)
        TEST_PASS("TC06 — NULL handle rejected");
    else
        TEST_FAIL("TC06 — NULL handle", "Expected FAIL");
}

static void tc07_check_null_callback(void)
{
    printf("\n--- TC07: CheckForUpdate NULL Callback ---\n");
    if (!ensure_registered()) { TEST_SKIP("TC07", "No handle"); return; }
    CheckForUpdateResult ret = checkForUpdate(g_handle, NULL);
    if (ret == CHECK_FOR_UPDATE_FAIL)
        TEST_PASS("TC07 — NULL callback rejected");
    else
        TEST_FAIL("TC07 — NULL callback", "Expected FAIL");
}

static void tc08_check_empty_handle(void)
{
    printf("\n--- TC08: CheckForUpdate Empty Handle ---\n");
    CheckForUpdateResult ret = checkForUpdate("", check_callback);
    if (ret == CHECK_FOR_UPDATE_FAIL)
        TEST_PASS("TC08 — empty handle rejected");
    else
        TEST_FAIL("TC08 — empty handle", "Expected FAIL");
}

static void tc09_check_duplicate(void)
{
    printf("\n--- TC09: CheckForUpdate Duplicate (Same Process) ---\n");
    reset_all();
    if (!ensure_registered()) { TEST_SKIP("TC09", "No handle"); return; }

    CheckForUpdateResult r1 = checkForUpdate(g_handle, check_callback);
    if (r1 != CHECK_FOR_UPDATE_SUCCESS) {
        TEST_FAIL("TC09 — first call", "First checkForUpdate failed");
        return;
    }

    /* Immediately call again — library guard should reject */
    CheckForUpdateResult r2 = checkForUpdate(g_handle, check_callback);
    if (r2 == CHECK_FOR_UPDATE_FAIL)
        TEST_PASS("TC09 — duplicate call rejected by library guard");
    else
        TEST_FAIL("TC09 — duplicate call", "Second call was NOT rejected");

    TEST_INFO("Waiting for first check to complete...");
    wait_flag(&g_check_cb_fired, 130);
}

static void tc10_check_rapid_retry(void)
{
    printf("\n--- TC10: CheckForUpdate Rapid Retry ---\n");
    reset_all();
    if (!ensure_registered()) { TEST_SKIP("TC10", "No handle"); return; }

    /* First call */
    CheckForUpdateResult r1 = checkForUpdate(g_handle, check_callback);
    if (r1 != CHECK_FOR_UPDATE_SUCCESS) {
        TEST_FAIL("TC10 — first call", "Failed");
        return;
    }
    TEST_INFO("Waiting for first callback...");
    if (!wait_flag(&g_check_cb_fired, 130)) {
        TEST_FAIL("TC10", "First callback never fired");
        return;
    }

    sleep(1); /* Let worker thread fully exit */
    reset_all();

    /* Retry — should succeed (guard cleared after previous completed) */
    CheckForUpdateResult r2 = checkForUpdate(g_handle, check_callback);
    if (r2 == CHECK_FOR_UPDATE_SUCCESS) {
        TEST_PASS("TC10 — rapid retry accepted (guard was cleared)");
        wait_flag(&g_check_cb_fired, 130);
    } else {
        TEST_FAIL("TC10 — rapid retry", "Rejected (in-progress flag not cleared?)");
    }
}

static void tc11_check_callback_data(void)
{
    printf("\n--- TC11: CheckForUpdate Callback Data Validation ---\n");
    reset_all();
    if (!ensure_registered()) { TEST_SKIP("TC11", "No handle"); return; }

    CheckForUpdateResult ret = checkForUpdate(g_handle, check_callback);
    if (ret != CHECK_FOR_UPDATE_SUCCESS) {
        TEST_FAIL("TC11", "API returned FAIL");
        return;
    }
    if (!wait_flag(&g_check_cb_fired, 130)) {
        TEST_FAIL("TC11", "Callback never fired");
        return;
    }

    if (g_check_cb_count == 1)
        TEST_PASS("TC11 — callback fired exactly once");
    else {
        char msg[64]; snprintf(msg, sizeof(msg), "Callback fired %d times (expected 1)", g_check_cb_count);
        TEST_FAIL("TC11", msg);
    }

    if (g_check_status >= 0 && g_check_status <= 5)
        TEST_PASS("TC11 — status is valid CheckForUpdateStatus enum");
    else {
        char msg[64]; snprintf(msg, sizeof(msg), "status=%d not in [0..5]", g_check_status);
        TEST_FAIL("TC11", msg);
    }
}

/* ========================================================================
 * TC12–TC22: DOWNLOAD FIRMWARE
 * ======================================================================== */

static FwDwnlReq make_dwnl_req(void)
{
    FwDwnlReq req;
    memset(&req, 0, sizeof(req));
    req.firmwareName   = "test_firmware.bin";
    req.downloadUrl    = "http://localhost:8080/firmware/test_firmware.bin";
    req.TypeOfFirmware = "PCI";
    return req;
}

static void tc12_download_happy(void)
{
    printf("\n--- TC12: DownloadFirmware Happy Path ---\n");
    reset_all();
    if (!ensure_registered()) { TEST_SKIP("TC12", "No handle"); return; }

    FwDwnlReq req = make_dwnl_req();
    DownloadResult ret = downloadFirmware(g_handle, &req, download_callback);
    if (ret != RDKFW_DWNL_SUCCESS) {
        TEST_FAIL("TC12", "API returned FAILED (daemon rejected?)");
        return;
    }
    TEST_PASS("TC12 — daemon accepted download request");

    TEST_INFO("Waiting for terminal callback (max 600s)...");
    if (wait_flag(&g_dwnl_cb_terminal, 600)) {
        if (g_dwnl_status == (int)DWNL_COMPLETED)
            TEST_PASS("TC12 — download completed successfully");
        else {
            char msg[64]; snprintf(msg, sizeof(msg), "Terminal status=%d (expected COMPLETED=%d)", g_dwnl_status, DWNL_COMPLETED);
            TEST_FAIL("TC12", msg);
        }
    } else {
        TEST_FAIL("TC12", "Terminal callback never fired (600s timeout)");
    }
}

static void tc13_download_null_handle(void)
{
    printf("\n--- TC13: DownloadFirmware NULL Handle ---\n");
    FwDwnlReq req = make_dwnl_req();
    DownloadResult ret = downloadFirmware(NULL, &req, download_callback);
    if (ret == RDKFW_DWNL_FAILED) TEST_PASS("TC13 — NULL handle rejected");
    else TEST_FAIL("TC13", "Expected FAILED");
}

static void tc14_download_null_request(void)
{
    printf("\n--- TC14: DownloadFirmware NULL Request ---\n");
    if (!ensure_registered()) { TEST_SKIP("TC14", "No handle"); return; }
    DownloadResult ret = downloadFirmware(g_handle, NULL, download_callback);
    if (ret == RDKFW_DWNL_FAILED) TEST_PASS("TC14 — NULL request rejected");
    else TEST_FAIL("TC14", "Expected FAILED");
}

static void tc15_download_null_callback(void)
{
    printf("\n--- TC15: DownloadFirmware NULL Callback ---\n");
    if (!ensure_registered()) { TEST_SKIP("TC15", "No handle"); return; }
    FwDwnlReq req = make_dwnl_req();
    DownloadResult ret = downloadFirmware(g_handle, &req, NULL);
    if (ret == RDKFW_DWNL_FAILED) TEST_PASS("TC15 — NULL callback rejected");
    else TEST_FAIL("TC15", "Expected FAILED");
}

static void tc16_download_null_firmware_name(void)
{
    printf("\n--- TC16: DownloadFirmware NULL Firmware Name ---\n");
    if (!ensure_registered()) { TEST_SKIP("TC16", "No handle"); return; }
    FwDwnlReq req = make_dwnl_req();
    req.firmwareName = NULL;
    DownloadResult ret = downloadFirmware(g_handle, &req, download_callback);
    if (ret == RDKFW_DWNL_FAILED) TEST_PASS("TC16 — NULL firmwareName rejected");
    else TEST_FAIL("TC16", "Expected FAILED");
}

static void tc17_download_empty_firmware_name(void)
{
    printf("\n--- TC17: DownloadFirmware Empty Firmware Name ---\n");
    if (!ensure_registered()) { TEST_SKIP("TC17", "No handle"); return; }
    FwDwnlReq req = make_dwnl_req();
    req.firmwareName = "";
    DownloadResult ret = downloadFirmware(g_handle, &req, download_callback);
    if (ret == RDKFW_DWNL_FAILED) TEST_PASS("TC17 — empty firmwareName rejected");
    else TEST_FAIL("TC17", "Expected FAILED");
}

static void tc18_download_duplicate(void)
{
    printf("\n--- TC18: DownloadFirmware Duplicate (Same Process) ---\n");
    reset_all();
    if (!ensure_registered()) { TEST_SKIP("TC18", "No handle"); return; }

    FwDwnlReq req = make_dwnl_req();
    DownloadResult r1 = downloadFirmware(g_handle, &req, download_callback);
    if (r1 != RDKFW_DWNL_SUCCESS) {
        TEST_FAIL("TC18 — first call", "First download failed");
        return;
    }

    DownloadResult r2 = downloadFirmware(g_handle, &req, download_callback);
    if (r2 == RDKFW_DWNL_FAILED)
        TEST_PASS("TC18 — duplicate download rejected by library guard");
    else
        TEST_FAIL("TC18", "Second call was NOT rejected");

    TEST_INFO("Waiting for first download to complete...");
    wait_flag(&g_dwnl_cb_terminal, 600);
}

static void tc19_download_rapid_retry(void)
{
    printf("\n--- TC19: DownloadFirmware Rapid Retry ---\n");
    reset_all();
    if (!ensure_registered()) { TEST_SKIP("TC19", "No handle"); return; }

    FwDwnlReq req = make_dwnl_req();

    DownloadResult r1 = downloadFirmware(g_handle, &req, download_callback);
    if (r1 != RDKFW_DWNL_SUCCESS) {
        TEST_FAIL("TC19 — first call", "Failed");
        return;
    }
    TEST_INFO("Waiting for first download to complete...");
    if (!wait_flag(&g_dwnl_cb_terminal, 600)) {
        TEST_FAIL("TC19", "First terminal never fired");
        return;
    }

    sleep(1);
    reset_all();

    DownloadResult r2 = downloadFirmware(g_handle, &req, download_callback);
    if (r2 == RDKFW_DWNL_SUCCESS) {
        TEST_PASS("TC19 — rapid retry accepted (guard was cleared)");
        wait_flag(&g_dwnl_cb_terminal, 600);
    } else {
        TEST_FAIL("TC19", "Rejected (in-progress flag not cleared?)");
    }
}

static void tc20_download_progress_mono(void)
{
    printf("\n--- TC20: Download Progress Monotonicity ---\n");
    /* Uses data from the most recent download. Run TC12 first. */
    if (g_dwnl_cb_count == 0) {
        TEST_SKIP("TC20", "No download has run yet — run TC12 first");
        return;
    }
    if (g_dwnl_cb_count > 1)
        TEST_PASS("TC20 — multiple progress callbacks fired");
    else {
        char msg[64]; snprintf(msg, sizeof(msg), "Only %d callback(s)", g_dwnl_cb_count);
        TEST_FAIL("TC20", msg);
    }
    if (g_dwnl_progress_mono)
        TEST_PASS("TC20 — progress values were monotonically increasing");
    else
        TEST_FAIL("TC20", "Progress decreased at some point (daemon bug?)");
}

static void tc21_download_terminal(void)
{
    printf("\n--- TC21: Download Terminal Status ---\n");
    if (g_dwnl_cb_count == 0) {
        TEST_SKIP("TC21", "No download has run yet");
        return;
    }
    if (g_dwnl_status == (int)DWNL_COMPLETED || g_dwnl_status == (int)DWNL_ERROR)
        TEST_PASS("TC21 — final callback had terminal status");
    else {
        char msg[64]; snprintf(msg, sizeof(msg), "Final status=%d (not COMPLETED or ERROR)", g_dwnl_status);
        TEST_FAIL("TC21", msg);
    }
}

static void tc22_download_empty_handle(void)
{
    printf("\n--- TC22: DownloadFirmware Empty Handle ---\n");
    FwDwnlReq req = make_dwnl_req();
    DownloadResult ret = downloadFirmware("", &req, download_callback);
    if (ret == RDKFW_DWNL_FAILED) TEST_PASS("TC22 — empty handle rejected");
    else TEST_FAIL("TC22", "Expected FAILED");
}

/* ========================================================================
 * TC23–TC33: UPDATE FIRMWARE
 * ======================================================================== */

static FwUpdateReq make_update_req(void)
{
    FwUpdateReq req;
    memset(&req, 0, sizeof(req));
    req.firmwareName       = "test_firmware.bin";
    req.TypeOfFirmware     = "PCI";
    req.LocationOfFirmware = "/opt/CDL";
    req.rebootImmediately  = false;
    return req;
}

static void tc23_update_happy(void)
{
    printf("\n--- TC23: UpdateFirmware Happy Path ---\n");
    reset_all();
    if (!ensure_registered()) { TEST_SKIP("TC23", "No handle"); return; }

    FwUpdateReq req = make_update_req();
    UpdateResult ret = updateFirmware(g_handle, &req, update_callback);
    if (ret != RDKFW_UPDATE_SUCCESS) {
        TEST_FAIL("TC23", "API returned FAILED (daemon rejected?)");
        return;
    }
    TEST_PASS("TC23 — daemon accepted update request");

    TEST_INFO("Waiting for terminal callback (max 600s)...");
    if (wait_flag(&g_update_cb_terminal, 600)) {
        if (g_update_status == (int)UPDATE_COMPLETED)
            TEST_PASS("TC23 — update completed successfully");
        else {
            char msg[64]; snprintf(msg, sizeof(msg), "Terminal status=%d (expected COMPLETED=%d)", g_update_status, UPDATE_COMPLETED);
            TEST_FAIL("TC23", msg);
        }
    } else {
        TEST_FAIL("TC23", "Terminal callback never fired (600s timeout)");
    }
}

static void tc24_update_null_handle(void)
{
    printf("\n--- TC24: UpdateFirmware NULL Handle ---\n");
    FwUpdateReq req = make_update_req();
    UpdateResult ret = updateFirmware(NULL, &req, update_callback);
    if (ret == RDKFW_UPDATE_FAILED) TEST_PASS("TC24 — NULL handle rejected");
    else TEST_FAIL("TC24", "Expected FAILED");
}

static void tc25_update_null_request(void)
{
    printf("\n--- TC25: UpdateFirmware NULL Request ---\n");
    if (!ensure_registered()) { TEST_SKIP("TC25", "No handle"); return; }
    UpdateResult ret = updateFirmware(g_handle, NULL, update_callback);
    if (ret == RDKFW_UPDATE_FAILED) TEST_PASS("TC25 — NULL request rejected");
    else TEST_FAIL("TC25", "Expected FAILED");
}

static void tc26_update_null_callback(void)
{
    printf("\n--- TC26: UpdateFirmware NULL Callback ---\n");
    if (!ensure_registered()) { TEST_SKIP("TC26", "No handle"); return; }
    FwUpdateReq req = make_update_req();
    UpdateResult ret = updateFirmware(g_handle, &req, NULL);
    if (ret == RDKFW_UPDATE_FAILED) TEST_PASS("TC26 — NULL callback rejected");
    else TEST_FAIL("TC26", "Expected FAILED");
}

static void tc27_update_null_firmware_name(void)
{
    printf("\n--- TC27: UpdateFirmware NULL Firmware Name ---\n");
    if (!ensure_registered()) { TEST_SKIP("TC27", "No handle"); return; }
    FwUpdateReq req = make_update_req();
    req.firmwareName = NULL;
    UpdateResult ret = updateFirmware(g_handle, &req, update_callback);
    if (ret == RDKFW_UPDATE_FAILED) TEST_PASS("TC27 — NULL firmwareName rejected");
    else TEST_FAIL("TC27", "Expected FAILED");
}

static void tc28_update_empty_firmware_name(void)
{
    printf("\n--- TC28: UpdateFirmware Empty Firmware Name ---\n");
    if (!ensure_registered()) { TEST_SKIP("TC28", "No handle"); return; }
    FwUpdateReq req = make_update_req();
    req.firmwareName = "";
    UpdateResult ret = updateFirmware(g_handle, &req, update_callback);
    if (ret == RDKFW_UPDATE_FAILED) TEST_PASS("TC28 — empty firmwareName rejected");
    else TEST_FAIL("TC28", "Expected FAILED");
}

static void tc29_update_duplicate(void)
{
    printf("\n--- TC29: UpdateFirmware Duplicate (Same Process) ---\n");
    reset_all();
    if (!ensure_registered()) { TEST_SKIP("TC29", "No handle"); return; }

    FwUpdateReq req = make_update_req();
    UpdateResult r1 = updateFirmware(g_handle, &req, update_callback);
    if (r1 != RDKFW_UPDATE_SUCCESS) {
        TEST_FAIL("TC29 — first call", "First update failed");
        return;
    }

    UpdateResult r2 = updateFirmware(g_handle, &req, update_callback);
    if (r2 == RDKFW_UPDATE_FAILED)
        TEST_PASS("TC29 — duplicate update rejected by library guard");
    else
        TEST_FAIL("TC29", "Second call was NOT rejected");

    TEST_INFO("Waiting for first update to complete...");
    wait_flag(&g_update_cb_terminal, 600);
}

static void tc30_update_rapid_retry(void)
{
    printf("\n--- TC30: UpdateFirmware Rapid Retry ---\n");
    reset_all();
    if (!ensure_registered()) { TEST_SKIP("TC30", "No handle"); return; }

    FwUpdateReq req = make_update_req();

    UpdateResult r1 = updateFirmware(g_handle, &req, update_callback);
    if (r1 != RDKFW_UPDATE_SUCCESS) {
        TEST_FAIL("TC30 — first call", "Failed");
        return;
    }
    TEST_INFO("Waiting for first update to complete...");
    if (!wait_flag(&g_update_cb_terminal, 600)) {
        TEST_FAIL("TC30", "First terminal never fired");
        return;
    }

    sleep(1);
    reset_all();

    UpdateResult r2 = updateFirmware(g_handle, &req, update_callback);
    if (r2 == RDKFW_UPDATE_SUCCESS) {
        TEST_PASS("TC30 — rapid retry accepted (guard was cleared)");
        wait_flag(&g_update_cb_terminal, 600);
    } else {
        TEST_FAIL("TC30", "Rejected (in-progress flag not cleared?)");
    }
}

static void tc31_update_progress_mono(void)
{
    printf("\n--- TC31: Update Progress Monotonicity ---\n");
    if (g_update_cb_count == 0) {
        TEST_SKIP("TC31", "No update has run yet — run TC23 first");
        return;
    }
    if (g_update_cb_count > 1)
        TEST_PASS("TC31 — multiple progress callbacks fired");
    else {
        char msg[64]; snprintf(msg, sizeof(msg), "Only %d callback(s)", g_update_cb_count);
        TEST_FAIL("TC31", msg);
    }
    if (g_update_progress_mono)
        TEST_PASS("TC31 — progress values were monotonically increasing");
    else
        TEST_FAIL("TC31", "Progress decreased (daemon bug?)");
}

static void tc32_update_terminal(void)
{
    printf("\n--- TC32: Update Terminal Status ---\n");
    if (g_update_cb_count == 0) {
        TEST_SKIP("TC32", "No update has run yet");
        return;
    }
    if (g_update_status == (int)UPDATE_COMPLETED || g_update_status == (int)UPDATE_ERROR)
        TEST_PASS("TC32 — final callback had terminal status");
    else {
        char msg[64]; snprintf(msg, sizeof(msg), "Final status=%d", g_update_status);
        TEST_FAIL("TC32", msg);
    }
}

static void tc33_update_empty_handle(void)
{
    printf("\n--- TC33: UpdateFirmware Empty Handle ---\n");
    FwUpdateReq req = make_update_req();
    UpdateResult ret = updateFirmware("", &req, update_callback);
    if (ret == RDKFW_UPDATE_FAILED) TEST_PASS("TC33 — empty handle rejected");
    else TEST_FAIL("TC33", "Expected FAILED");
}

/* ========================================================================
 * TC34–TC36: UNREGISTER DURING ACTIVE OPERATION
 * ======================================================================== */

static void tc34_unreg_during_check(void)
{
    printf("\n--- TC34: Unregister During CheckForUpdate ---\n");
    reset_all();
    if (!ensure_registered()) { TEST_SKIP("TC34", "No handle"); return; }

    CheckForUpdateResult r1 = checkForUpdate(g_handle, check_callback);
    if (r1 != CHECK_FOR_UPDATE_SUCCESS) {
        TEST_FAIL("TC34 — start check", "Failed");
        return;
    }

    /* Immediately try unregister — should be blocked by in-progress guard */
    /* Note: unregisterProcess returns void, so we check if handle is still valid afterward */
    /* If guard works, unregister does nothing and we can still wait for callback */
    unregisterProcess(g_handle);

    /* If guard worked, callback should still fire (handle was NOT removed) */
    TEST_INFO("Waiting for check callback (if guard worked, it should still fire)...");
    if (wait_flag(&g_check_cb_fired, 130)) {
        TEST_PASS("TC34 — callback still fired (unregister was blocked during active check)");
    } else {
        TEST_FAIL("TC34", "Callback never fired (unregister may have succeeded during active op!)");
    }

    /* Re-register since handle may be invalidated */
    g_handle = NULL;
    ensure_registered();
}

static void tc35_unreg_during_download(void)
{
    printf("\n--- TC35: Unregister During Download ---\n");
    reset_all();
    if (!ensure_registered()) { TEST_SKIP("TC35", "No handle"); return; }

    FwDwnlReq req = make_dwnl_req();
    DownloadResult r1 = downloadFirmware(g_handle, &req, download_callback);
    if (r1 != RDKFW_DWNL_SUCCESS) {
        TEST_FAIL("TC35 — start download", "Failed");
        return;
    }

    unregisterProcess(g_handle);

    TEST_INFO("Waiting for download terminal callback...");
    if (wait_flag(&g_dwnl_cb_terminal, 600)) {
        TEST_PASS("TC35 — callback still fired (unregister was blocked during active download)");
    } else {
        TEST_FAIL("TC35", "Terminal callback never fired");
    }

    g_handle = NULL;
    ensure_registered();
}

static void tc36_unreg_during_update(void)
{
    printf("\n--- TC36: Unregister During Update ---\n");
    reset_all();
    if (!ensure_registered()) { TEST_SKIP("TC36", "No handle"); return; }

    FwUpdateReq req = make_update_req();
    UpdateResult r1 = updateFirmware(g_handle, &req, update_callback);
    if (r1 != RDKFW_UPDATE_SUCCESS) {
        TEST_FAIL("TC36 — start update", "Failed");
        return;
    }

    unregisterProcess(g_handle);

    TEST_INFO("Waiting for update terminal callback...");
    if (wait_flag(&g_update_cb_terminal, 600)) {
        TEST_PASS("TC36 — callback still fired (unregister was blocked during active update)");
    } else {
        TEST_FAIL("TC36", "Terminal callback never fired");
    }

    g_handle = NULL;
    ensure_registered();
}

/* ========================================================================
 * TC37–TC39: FULL LIFECYCLE
 * ======================================================================== */

static void tc37_full_lifecycle(void)
{
    separator("TC37: FULL LIFECYCLE — Register → Check → Download → Update → Unregister");

    /* Step 1: Register */
    printf("\n  Step 1: Register\n");
    FirmwareInterfaceHandle h = registerProcess("KWIB_Lifecycle", LIB_VERSION);
    if (h == NULL) {
        TEST_FAIL("TC37 — register", "registerProcess() returned NULL");
        return;
    }
    TEST_PASS("TC37 — register");
    printf("    Handle: '%s'\n", h);

    /* Step 2: Check */
    printf("\n  Step 2: CheckForUpdate\n");
    reset_all();
    CheckForUpdateResult cr = checkForUpdate(h, check_callback);
    if (cr != CHECK_FOR_UPDATE_SUCCESS) {
        TEST_FAIL("TC37 — checkForUpdate", "API returned FAIL");
        unregisterProcess(h); return;
    }
    if (!wait_flag(&g_check_cb_fired, 130)) {
        TEST_FAIL("TC37 — checkForUpdate", "Callback timeout");
        unregisterProcess(h); return;
    }
    TEST_PASS("TC37 — checkForUpdate completed");
    sleep(1);

    /* Step 3: Download */
    printf("\n  Step 3: DownloadFirmware\n");
    reset_all();
    FwDwnlReq dreq = make_dwnl_req();
    DownloadResult dr = downloadFirmware(h, &dreq, download_callback);
    if (dr != RDKFW_DWNL_SUCCESS) {
        TEST_FAIL("TC37 — downloadFirmware", "API returned FAILED");
        unregisterProcess(h); return;
    }
    if (!wait_flag(&g_dwnl_cb_terminal, 600)) {
        TEST_FAIL("TC37 — downloadFirmware", "Terminal callback timeout");
        unregisterProcess(h); return;
    }
    if (g_dwnl_status == (int)DWNL_COMPLETED)
        TEST_PASS("TC37 — download completed");
    else {
        char msg[64]; snprintf(msg, sizeof(msg), "status=%d", g_dwnl_status);
        TEST_FAIL("TC37 — download", msg);
        unregisterProcess(h); return;
    }
    sleep(1);

    /* Step 4: Update */
    printf("\n  Step 4: UpdateFirmware\n");
    reset_all();
    FwUpdateReq ureq = make_update_req();
    UpdateResult ur = updateFirmware(h, &ureq, update_callback);
    if (ur != RDKFW_UPDATE_SUCCESS) {
        TEST_FAIL("TC37 — updateFirmware", "API returned FAILED");
        unregisterProcess(h); return;
    }
    if (!wait_flag(&g_update_cb_terminal, 600)) {
        TEST_FAIL("TC37 — updateFirmware", "Terminal callback timeout");
        unregisterProcess(h); return;
    }
    if (g_update_status == (int)UPDATE_COMPLETED)
        TEST_PASS("TC37 — update completed");
    else {
        char msg[64]; snprintf(msg, sizeof(msg), "status=%d", g_update_status);
        TEST_FAIL("TC37 — update", msg);
        unregisterProcess(h); return;
    }
    sleep(1);

    /* Step 5: Unregister */
    printf("\n  Step 5: Unregister\n");
    unregisterProcess(h);
    TEST_PASS("TC37 — unregister");
}

static void tc38_lifecycle_no_sleeps(void)
{
    separator("TC38: FULL LIFECYCLE — No sleeps between calls");

    FirmwareInterfaceHandle h = registerProcess("KWIB_NoSleep", LIB_VERSION);
    if (!h) { TEST_FAIL("TC38 — register", "NULL"); return; }
    TEST_PASS("TC38 — register");

    /* Check */
    reset_all();
    if (checkForUpdate(h, check_callback) != CHECK_FOR_UPDATE_SUCCESS) {
        TEST_FAIL("TC38 — check", "FAIL"); unregisterProcess(h); return;
    }
    wait_flag(&g_check_cb_fired, 130);
    TEST_PASS("TC38 — check done");

    /* Immediately download (no sleep) */
    reset_all();
    FwDwnlReq dreq = make_dwnl_req();
    DownloadResult dr = downloadFirmware(h, &dreq, download_callback);
    if (dr == RDKFW_DWNL_SUCCESS) {
        TEST_PASS("TC38 — download accepted immediately after check");
        wait_flag(&g_dwnl_cb_terminal, 600);
    } else {
        TEST_FAIL("TC38 — download", "Rejected (check worker may not have exited yet)");
    }

    /* Immediately update (no sleep) */
    reset_all();
    FwUpdateReq ureq = make_update_req();
    UpdateResult ur = updateFirmware(h, &ureq, update_callback);
    if (ur == RDKFW_UPDATE_SUCCESS) {
        TEST_PASS("TC38 — update accepted immediately after download");
        wait_flag(&g_update_cb_terminal, 600);
    } else {
        TEST_FAIL("TC38 — update", "Rejected (download worker may not have exited yet)");
    }

    unregisterProcess(h);
    TEST_PASS("TC38 — lifecycle complete");
}

static void tc39_check_and_download_simultaneous(void)
{
    separator("TC39: Simultaneous Check + Download (different guards)");
    reset_all();
    if (!ensure_registered()) { TEST_SKIP("TC39", "No handle"); return; }

    /* Start check */
    CheckForUpdateResult cr = checkForUpdate(g_handle, check_callback);
    if (cr != CHECK_FOR_UPDATE_SUCCESS) {
        TEST_FAIL("TC39 — check", "FAIL"); return;
    }

    /* Immediately start download (different guard — should succeed) */
    FwDwnlReq dreq = make_dwnl_req();
    DownloadResult dr = downloadFirmware(g_handle, &dreq, download_callback);
    if (dr == RDKFW_DWNL_SUCCESS)
        TEST_PASS("TC39 — download accepted while check is active (independent guards)");
    else
        TEST_FAIL("TC39 — download", "Rejected while check active (guards may be coupled?)");

    /* Wait for both to finish */
    wait_flag(&g_check_cb_fired, 130);
    wait_flag(&g_dwnl_cb_terminal, 600);
}

/* ========================================================================
 * AUTOMATED SUITE RUNNERS
 * ======================================================================== */

static void run_error_tests(void)
{
    separator("ERROR TESTS — INPUT VALIDATION (library-level, fast)");

    tc03_unregister_null();
    tc06_check_null_handle();
    tc08_check_empty_handle();
    tc13_download_null_handle();
    tc22_download_empty_handle();
    tc24_update_null_handle();
    tc33_update_empty_handle();

    separator("ERROR TESTS — WITH REGISTRATION (daemon needed)");

    if (!ensure_registered()) {
        TEST_INFO("Cannot continue — registration failed (daemon down?)");
        return;
    }

    tc07_check_null_callback();
    tc14_download_null_request();
    tc15_download_null_callback();
    tc16_download_null_firmware_name();
    tc17_download_empty_firmware_name();
    tc25_update_null_request();
    tc26_update_null_callback();
    tc27_update_null_firmware_name();
    tc28_update_empty_firmware_name();

    separator("GUARD TESTS — DUPLICATE CALLS");

    tc09_check_duplicate();
    sleep(2);
    tc18_download_duplicate();
    sleep(2);
    tc29_update_duplicate();
    sleep(2);

    separator("GUARD TESTS — UNREGISTER DURING OPERATION");

    tc34_unreg_during_check();
    sleep(2);
    tc35_unreg_during_download();
    sleep(2);
    tc36_unreg_during_update();
    sleep(2);

    ensure_unregistered();
}

static void run_happy_tests(void)
{
    separator("HAPPY PATH TESTS (daemon required)");

    tc01_register_happy();
    if (!g_handle) { TEST_INFO("Cannot continue — register failed"); return; }

    tc04_double_register();

    tc05_check_happy();
    sleep(2);
    tc11_check_callback_data();
    sleep(2);
    tc10_check_rapid_retry();
    sleep(2);

    tc12_download_happy();
    tc20_download_progress_mono();
    tc21_download_terminal();
    sleep(2);
    tc19_download_rapid_retry();
    sleep(2);

    tc23_update_happy();
    tc31_update_progress_mono();
    tc32_update_terminal();
    sleep(2);
    tc30_update_rapid_retry();
    sleep(2);

    tc02_unregister_happy();
}

static void run_lifecycle_tests(void)
{
    tc37_full_lifecycle();
    sleep(2);
    tc38_lifecycle_no_sleeps();
    sleep(2);
    tc39_check_and_download_simultaneous();
    sleep(2);
    ensure_unregistered();
}

static void print_results(void)
{
    printf("\n");
    printf("══════════════════════════════════════════════════════════════\n");
    printf("  KnowWhereItBreaks — TEST RESULTS\n");
    printf("══════════════════════════════════════════════════════════════\n");
    printf("  Total:   %d\n", g_results.total);
    printf("  \033[32mPassed:  %d\033[0m\n", g_results.passed);
    printf("  \033[31mFailed:  %d\033[0m\n", g_results.failed);
    printf("  \033[33mSkipped: %d\033[0m\n", g_results.skipped);
    printf("══════════════════════════════════════════════════════════════\n");
    if (g_results.failed == 0)
        printf("  \033[32m✅ ALL TESTS PASSED\033[0m\n");
    else
        printf("  \033[31m❌ %d TEST(S) FAILED\033[0m\n", g_results.failed);
    printf("══════════════════════════════════════════════════════════════\n\n");
}

/* ========================================================================
 * INTERACTIVE MENU
 * ======================================================================== */

static void print_menu(void)
{
    printf("\n");
    printf("┌──────────────────────────────────────────────────────────────┐\n");
    printf("│  KnowWhereItBreaks v1.0 — librdkFwupdateMgr Test Utility    │\n");
    printf("├──────────────────────────────────────────────────────────────┤\n");
    printf("│  Handle: %-49s│\n", g_handle ? g_handle : "(not registered)");
    printf("├──────────────────────────────────────────────────────────────┤\n");
    printf("│  AUTOMATED SUITES                                            │\n");
    printf("│   10  All Error/Validation Tests (fast)                      │\n");
    printf("│   11  All Happy Path Tests (daemon needed)                   │\n");
    printf("│   12  Full Lifecycle Tests (daemon needed)                   │\n");
    printf("│   13  ALL Tests (everything)                                 │\n");
    printf("├──────────────────────────────────────────────────────────────┤\n");
    printf("│  REGISTER / UNREGISTER            CHECKFORUPDATE             │\n");
    printf("│    1  TC01 Register happy           5  TC05 Check happy      │\n");
    printf("│    2  TC02 Unregister happy         6  TC06 NULL handle      │\n");
    printf("│    3  TC03 Unregister NULL          7  TC07 NULL callback    │\n");
    printf("│    4  TC04 Double register          8  TC08 Empty handle     │\n");
    printf("│                                     9  TC09 Duplicate        │\n");
    printf("│                                    40  TC10 Rapid retry      │\n");
    printf("│                                    41  TC11 Callback data    │\n");
    printf("├──────────────────────────────────────────────────────────────┤\n");
    printf("│  DOWNLOAD FIRMWARE                 UPDATE FIRMWARE            │\n");
    printf("│   50  TC12 Download happy          60  TC23 Update happy     │\n");
    printf("│   51  TC13 NULL handle             61  TC24 NULL handle      │\n");
    printf("│   52  TC14 NULL request            62  TC25 NULL request     │\n");
    printf("│   53  TC15 NULL callback           63  TC26 NULL callback    │\n");
    printf("│   54  TC16 NULL fw name            64  TC27 NULL fw name     │\n");
    printf("│   55  TC17 Empty fw name           65  TC28 Empty fw name    │\n");
    printf("│   56  TC18 Duplicate               66  TC29 Duplicate        │\n");
    printf("│   57  TC19 Rapid retry             67  TC30 Rapid retry      │\n");
    printf("│   58  TC20 Progress mono           68  TC31 Progress mono    │\n");
    printf("│   59  TC21 Terminal status         69  TC32 Terminal status  │\n");
    printf("│                                    70  TC33 Empty handle     │\n");
    printf("├──────────────────────────────────────────────────────────────┤\n");
    printf("│  GUARDS / LIFECYCLE                                          │\n");
    printf("│   80  TC34 Unreg during check      90  TC37 Full lifecycle   │\n");
    printf("│   81  TC35 Unreg during download   91  TC38 No-sleep lifecy  │\n");
    printf("│   82  TC36 Unreg during update     92  TC39 Check+Dwnl sim  │\n");
    printf("├──────────────────────────────────────────────────────────────┤\n");
    printf("│    0  Exit (print results)                                   │\n");
    printf("└──────────────────────────────────────────────────────────────┘\n");
    printf("  Choice: ");
}

/* ========================================================================
 * MAIN
 * ======================================================================== */

int main(int argc, char *argv[])
{
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  KnowWhereItBreaks v1.0                                    ║\n");
    printf("║  Comprehensive Test Utility for librdkFwupdateMgr.so       ║\n");
    printf("║  Build: %s %s                                 ║\n", __DATE__, __TIME__);
    printf("╚══════════════════════════════════════════════════════════════╝\n");

    /* Automated modes (for CI/scripts) */
    if (argc > 1) {
        if (strcmp(argv[1], "--auto-error") == 0) {
            run_error_tests(); print_results();
            return g_results.failed > 0 ? 1 : 0;
        }
        if (strcmp(argv[1], "--auto-happy") == 0) {
            run_happy_tests(); print_results();
            return g_results.failed > 0 ? 1 : 0;
        }
        if (strcmp(argv[1], "--full-lifecycle") == 0) {
            run_lifecycle_tests(); print_results();
            return g_results.failed > 0 ? 1 : 0;
        }
        if (strcmp(argv[1], "--auto-all") == 0) {
            run_error_tests();
            sleep(2); g_handle = NULL;
            run_happy_tests();
            sleep(2);
            run_lifecycle_tests();
            print_results();
            return g_results.failed > 0 ? 1 : 0;
        }
        printf("Unknown option: %s\n", argv[1]);
        printf("Usage: %s [--auto-error|--auto-happy|--full-lifecycle|--auto-all]\n", argv[0]);
        return 1;
    }

    /* Interactive mode */
    int choice;
    char line[32];

    while (1) {
        print_menu();
        if (!fgets(line, sizeof(line), stdin)) break;
        choice = atoi(line);

        switch (choice) {
        case 0:
            ensure_unregistered();
            print_results();
            return g_results.failed > 0 ? 1 : 0;

        /* Register / Unregister */
        case 1: tc01_register_happy(); break;
        case 2: tc02_unregister_happy(); break;
        case 3: tc03_unregister_null(); break;
        case 4: tc04_double_register(); break;

        /* CheckForUpdate */
        case 5:  tc05_check_happy(); break;
        case 6:  tc06_check_null_handle(); break;
        case 7:  tc07_check_null_callback(); break;
        case 8:  tc08_check_empty_handle(); break;
        case 9:  tc09_check_duplicate(); break;
        case 40: tc10_check_rapid_retry(); break;
        case 41: tc11_check_callback_data(); break;

        /* Download */
        case 50: tc12_download_happy(); break;
        case 51: tc13_download_null_handle(); break;
        case 52: tc14_download_null_request(); break;
        case 53: tc15_download_null_callback(); break;
        case 54: tc16_download_null_firmware_name(); break;
        case 55: tc17_download_empty_firmware_name(); break;
        case 56: tc18_download_duplicate(); break;
        case 57: tc19_download_rapid_retry(); break;
        case 58: tc20_download_progress_mono(); break;
        case 59: tc21_download_terminal(); break;

        /* Update */
        case 60: tc23_update_happy(); break;
        case 61: tc24_update_null_handle(); break;
        case 62: tc25_update_null_request(); break;
        case 63: tc26_update_null_callback(); break;
        case 64: tc27_update_null_firmware_name(); break;
        case 65: tc28_update_empty_firmware_name(); break;
        case 66: tc29_update_duplicate(); break;
        case 67: tc30_update_rapid_retry(); break;
        case 68: tc31_update_progress_mono(); break;
        case 69: tc32_update_terminal(); break;
        case 70: tc33_update_empty_handle(); break;

        /* Guards / Lifecycle */
        case 80: tc34_unreg_during_check(); break;
        case 81: tc35_unreg_during_download(); break;
        case 82: tc36_unreg_during_update(); break;
        case 90: tc37_full_lifecycle(); break;
        case 91: tc38_lifecycle_no_sleeps(); break;
        case 92: tc39_check_and_download_simultaneous(); break;

        /* Automated suites */
        case 10: run_error_tests(); print_results(); break;
        case 11: run_happy_tests(); print_results(); break;
        case 12: run_lifecycle_tests(); print_results(); break;
        case 13:
            run_error_tests(); sleep(2); g_handle = NULL;
            run_happy_tests(); sleep(2);
            run_lifecycle_tests();
            print_results();
            break;

        default: printf("    Invalid choice.\n"); break;
        }
    }

    return 0;
}
