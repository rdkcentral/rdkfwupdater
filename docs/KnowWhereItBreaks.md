# KnowWhereItBreaks — Comprehensive Test Utility for librdkFwupdateMgr.so

## Document Version

| Version | Date       | Author | Description                              |
|---------|------------|--------|------------------------------------------|
| 1.0     | 2026-03-27 | —      | Initial design, test plan, and implementation guide |

---

## Table of Contents

1. [Purpose](#1-purpose)
2. [Architecture](#2-architecture)
3. [Build & Run](#3-build--run)
4. [Test Categories & Scenarios](#4-test-categories--scenarios)
5. [Error Path Deep Dive](#5-error-path-deep-dive)
6. [Interactive Menu Reference](#6-interactive-menu-reference)
7. [Automated Mode Reference](#7-automated-mode-reference)
8. [Test Result Tracking](#8-test-result-tracking)
9. [How Each Test Works Internally](#9-how-each-test-works-internally)
10. [Manual-Only Test Scenarios](#10-manual-only-test-scenarios)
11. [Troubleshooting](#11-troubleshooting)

---

## 1. Purpose

`KnowWhereItBreaks` is a **developer test utility** for exercising every code path
in `librdkFwupdateMgr.so` and the `rdkFwupdateMgr` daemon.

It is **NOT** the example_plugin (which stays clean for external teams).
It is your personal weapon for finding bugs before they find you.

### What it tests

| Layer | What gets exercised |
|-------|-------------------|
| **Library input validation** | NULL handles, NULL callbacks, NULL requests, empty strings |
| **Library guards** | Duplicate same-process calls (in-progress rejection) |
| **Library session guards** | Unregister blocked during active operations |
| **Condvar handshake** | Caller gets accurate SUCCESS/FAILED from daemon reply |
| **Worker thread lifecycle** | Thread creates, runs event loop, cleans up, exits |
| **D-Bus method calls** | RegisterProcess, CheckForUpdate, DownloadFirmware, UpdateFirmware, UnregisterProcess |
| **D-Bus signal reception** | CheckForUpdateComplete, DownloadProgress, UpdateProgress |
| **Daemon rejection paths** | Already in progress, invalid handle, unknown firmware |
| **Timeout paths** | Worker thread timeout when daemon doesn't respond |
| **Callback correctness** | Right data in callback, right number of invocations |
| **Full lifecycle** | Register → Check → Download → Update → Unregister |
| **Rapid retry** | Call again immediately after previous completes/fails |
| **Cross-API interaction** | Download + Check simultaneously, unregister during ops |

### What it does NOT test (needs manual testing)

| Scenario | Why manual |
|----------|-----------|
| Daemon crash mid-operation | Requires `kill -9` of daemon process at right moment |
| Cross-process rejection | Requires two separate processes running simultaneously |
| Disk full during download | Requires filling filesystem |
| Network failure during download | Requires network manipulation |
| Library unload during active operation | Requires `dlclose()` test program |

---

## 2. Architecture

```
┌─────────────────────────────────────────────────────────┐
│                   KnowWhereItBreaks                     │
│                                                         │
│  ┌─────────────┐  ┌──────────────┐  ┌───────────────┐  │
│  │ Test Engine  │  │ Result Track │  │ Callback Track│  │
│  │             │  │              │  │               │  │
│  │ run_test()  │  │ PASS/FAIL/   │  │ fired?        │  │
│  │ TEST_PASS() │  │ SKIP counts  │  │ status?       │  │
│  │ TEST_FAIL() │  │ final report │  │ progress?     │  │
│  │ TEST_SKIP() │  │              │  │ message?      │  │
│  └─────────────┘  └──────────────┘  └───────────────┘  │
│                         │                               │
│                    Public API calls                      │
│                         │                               │
│              ┌──────────▼──────────┐                    │
│              │ librdkFwupdateMgr.so │                   │
│              │  (library under test)│                    │
│              └──────────┬──────────┘                    │
│                         │ D-Bus                         │
│              ┌──────────▼──────────┐                    │
│              │  rdkFwupdateMgr     │                    │
│              │  (daemon)           │                    │
│              └─────────────────────┘                    │
└─────────────────────────────────────────────────────────┘
```

### Key Design Principles

1. **Each test is self-contained** — sets up its own state, cleans up after itself
2. **Callback tracking via volatile globals** — worker threads fire callbacks on
   different threads, `volatile` ensures visibility
3. **Wait-with-timeout** — never blocks forever; every wait has a max timeout
4. **Test isolation** — each test resets callback state before running
5. **Ordered execution** — tests run in dependency order (register before check, etc.)
6. **Three modes** — interactive menu, automated suites, command-line flags

---

## 3. Build & Run

### 3.1 Directory Structure

```
rdkfwupdater/
├── example_plugin/              ← Clean example for external teams (UNCHANGED)
│   ├── CMakeLists.txt
│   └── src/
│       └── example_plugin.c
│
├── KnowWhereItBreaks/           ← NEW: Developer test utility
│   ├── CMakeLists.txt
│   └── src/
│       └── KnowWhereItBreaks.c
│
├── librdkFwupdateMgr/           ← Library under test
└── src/                         ← Daemon
```

### 3.2 CMakeLists.txt

```cmake
# KnowWhereItBreaks/CMakeLists.txt
project(KnowWhereItBreaks)
cmake_minimum_required(VERSION 3.10)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0 gio-2.0)

add_executable(KnowWhereItBreaks src/KnowWhereItBreaks.c)

target_include_directories(KnowWhereItBreaks PRIVATE
    ${CMAKE_SOURCE_DIR}/librdkFwupdateMgr/include
    ${GLIB_INCLUDE_DIRS}
)

target_link_libraries(KnowWhereItBreaks PRIVATE
    rdkFwupdateMgr
    ${GLIB_LIBRARIES}
    pthread
)
```

### 3.3 Running

```bash
# Prerequisites: daemon must be running for happy path tests
systemctl start rdkFwupdateMgr

# Interactive menu (explore tests one by one)
./KnowWhereItBreaks

# Automated: only error/validation tests (daemon NOT needed for most)
./KnowWhereItBreaks --auto-error

# Automated: happy path tests (daemon REQUIRED)
./KnowWhereItBreaks --auto-happy

# Automated: full lifecycle (daemon REQUIRED)
./KnowWhereItBreaks --full-lifecycle

# Automated: run EVERYTHING
./KnowWhereItBreaks --auto-all

# Exit code: 0 = all passed, 1 = at least one failure
echo $?
```

---

## 4. Test Categories & Scenarios

### Category 1: RegisterProcess / UnregisterProcess (9 tests)

| ID | Test Name | Needs Daemon | What It Validates |
|----|-----------|:---:|-------------------|
| 1.1 | `register_happy_path` | ✅ | `registerProcess()` returns SUCCESS, handle is non-NULL non-empty |
| 1.2 | `register_daemon_down` | ❌ | Stop daemon → `registerProcess()` returns FAILED |
| 1.3 | `double_register` | ✅ | Call `registerProcess()` twice → both succeed with different handles |
| 1.4 | `unregister_happy_path` | ✅ | `unregisterProcess(handle)` returns SUCCESS |
| 1.5 | `unregister_invalid_handle` | ✅ | `unregisterProcess("invalid_99")` returns FAILED |
| 1.6 | `unregister_null_handle` | ❌ | `unregisterProcess(NULL)` returns FAILED (library validation) |
| 1.7 | `unregister_during_check` | ✅ | Start check → unregister → FAILED (guard blocks it) |
| 1.8 | `unregister_during_download` | ✅ | Start download → unregister → FAILED (guard blocks it) |
| 1.9 | `unregister_during_update` | ✅ | Start update → unregister → FAILED (guard blocks it) |

### Category 2: CheckForUpdate (7 tests)

| ID | Test Name | Needs Daemon | What It Validates |
|----|-----------|:---:|-------------------|
| 2.1 | `check_happy_path` | ✅ | Returns SUCCESS, callback fires with valid data |
| 2.2 | `check_null_handle` | ❌ | `checkForUpdate(NULL, cb)` returns FAILED |
| 2.3 | `check_null_callback` | ❌ | `checkForUpdate(handle, NULL)` returns FAILED |
| 2.4 | `check_duplicate_same_process` | ✅ | First call SUCCESS, immediate second call FAILED (library guard) |
| 2.5 | `check_rapid_retry` | ✅ | Call → wait → call again → SUCCESS (guard cleared) |
| 2.6 | `check_unregistered_handle` | ✅ | `checkForUpdate("bad_handle", cb)` returns FAILED |
| 2.7 | `check_callback_data_valid` | ✅ | Callback data has non-garbage version strings |

### Category 3: DownloadFirmware (12 tests)

| ID | Test Name | Needs Daemon | What It Validates |
|----|-----------|:---:|-------------------|
| 3.1 | `download_happy_path` | ✅ | Returns SUCCESS, progress callbacks fire, terminal = COMPLETED |
| 3.2 | `download_null_handle` | ❌ | Returns FAILED |
| 3.3 | `download_null_request` | ❌ | Returns FAILED |
| 3.4 | `download_null_callback` | ❌ | Returns FAILED |
| 3.5 | `download_empty_firmware_name` | ❌ | Returns FAILED (library validates non-empty) |
| 3.6 | `download_empty_url` | ❌ | Returns FAILED (library validates non-empty) |
| 3.7 | `download_duplicate_same_process` | ✅ | Second call FAILED (library guard) |
| 3.8 | `download_rapid_retry` | ✅ | After completion → retry → SUCCESS |
| 3.9 | `download_daemon_rejects` | ✅ | Invalid firmware → daemon rejects → FAILED return |
| 3.10 | `download_progress_increments` | ✅ | Multiple callbacks fire, progress values increase |
| 3.11 | `download_terminal_status` | ✅ | Final callback has COMPLETED or ERROR (not INPROGRESS) |
| 3.12 | `download_unregistered_handle` | ✅ | Returns FAILED |

### Category 4: UpdateFirmware (12 tests)

| ID | Test Name | Needs Daemon | What It Validates |
|----|-----------|:---:|-------------------|
| 4.1 | `update_happy_path` | ✅ | Returns SUCCESS, progress callbacks fire, terminal = COMPLETED |
| 4.2 | `update_null_handle` | ❌ | Returns FAILED |
| 4.3 | `update_null_request` | ❌ | Returns FAILED |
| 4.4 | `update_null_callback` | ❌ | Returns FAILED |
| 4.5 | `update_empty_firmware_name` | ❌ | Returns FAILED |
| 4.6 | `update_duplicate_same_process` | ✅ | Second call FAILED (library guard) |
| 4.7 | `update_rapid_retry` | ✅ | After completion → retry → SUCCESS |
| 4.8 | `update_daemon_rejects` | ✅ | Already updating → daemon rejects → FAILED |
| 4.9 | `update_progress_increments` | ✅ | Multiple callbacks, progress increases |
| 4.10 | `update_terminal_status` | ✅ | Final callback = COMPLETED or ERROR |
| 4.11 | `update_reboot_flag_false` | ✅ | No reboot after update |
| 4.12 | `update_unregistered_handle` | ✅ | Returns FAILED |

### Category 5: Cross-API / Lifecycle (5 tests)

| ID | Test Name | Needs Daemon | What It Validates |
|----|-----------|:---:|-------------------|
| 5.1 | `full_lifecycle` | ✅ | Register → Check → Download → Update → Unregister (all succeed) |
| 5.2 | `full_lifecycle_no_sleeps` | ✅ | Same as 5.1 but no sleep() between calls |
| 5.3 | `check_and_download_simultaneous` | ✅ | Both calls succeed (different guards) |
| 5.4 | `download_then_update_sequential` | ✅ | Download completes, then update succeeds |
| 5.5 | `double_register_full_lifecycle` | ✅ | Register twice, run ops on both handles |

**Total: 45 automated tests**

---

## 5. Error Path Deep Dive

### 5.1 Library-Level Error Paths (No Daemon Needed)

These are caught by the library's input validation BEFORE any D-Bus call:

```
┌───────────────────────────────────────────────────────────┐
│  Library Input Validation Layer                           │
│                                                           │
│  checkForUpdate():                                        │
│    ├─ handle == NULL           → RDKFW_UPDATE_FAILED      │
│    ├─ callback == NULL         → RDKFW_UPDATE_FAILED      │
│    └─ handle not registered    → RDKFW_UPDATE_FAILED      │
│                                                           │
│  downloadFirmware():                                      │
│    ├─ handle == NULL           → RDKFW_UPDATE_FAILED      │
│    ├─ request == NULL          → RDKFW_UPDATE_FAILED      │
│    ├─ callback == NULL         → RDKFW_UPDATE_FAILED      │
│    ├─ firmwareName empty       → RDKFW_UPDATE_FAILED      │
│    ├─ downloadUrl empty        → RDKFW_UPDATE_FAILED      │
│    └─ handle not registered    → RDKFW_UPDATE_FAILED      │
│                                                           │
│  updateFirmware():                                        │
│    ├─ handle == NULL           → RDKFW_UPDATE_FAILED      │
│    ├─ request == NULL          → RDKFW_UPDATE_FAILED      │
│    ├─ callback == NULL         → RDKFW_UPDATE_FAILED      │
│    ├─ firmwareName empty       → RDKFW_UPDATE_FAILED      │
│    └─ handle not registered    → RDKFW_UPDATE_FAILED      │
│                                                           │
│  unregisterProcess():                                     │
│    ├─ handle == NULL           → RDKFW_UPDATE_FAILED      │
│    ├─ check in progress        → RDKFW_UPDATE_FAILED      │
│    ├─ download in progress     → RDKFW_UPDATE_FAILED      │
│    └─ update in progress       → RDKFW_UPDATE_FAILED      │
└───────────────────────────────────────────────────────────┘
```

### 5.2 Library Guard Paths (No Daemon Needed)

These are caught by the per-API in-progress guards:

```
┌───────────────────────────────────────────────────────────┐
│  Library In-Progress Guards                               │
│                                                           │
│  checkForUpdate() while check active:                     │
│    internal_begin_check() → false → RDKFW_UPDATE_FAILED   │
│                                                           │
│  downloadFirmware() while download active:                │
│    internal_begin_download() → false → RDKFW_UPDATE_FAILED│
│                                                           │
│  updateFirmware() while update active:                    │
│    internal_begin_update() → false → RDKFW_UPDATE_FAILED  │
└───────────────────────────────────────────────────────────┘
```

### 5.3 Daemon-Level Rejection Paths (Daemon Needed)

The daemon receives the D-Bus method call and may reject it:

```
┌───────────────────────────────────────────────────────────┐
│  Daemon Rejection Paths                                   │
│                                                           │
│  DownloadFirmware:                                        │
│    ├─ Another download already active                     │
│    │   → reply (sss): "RDKFW_DWNL_FAILED",               │
│    │                   "REJECTED",                        │
│    │                   "Download already in progress"     │
│    │   → library returns RDKFW_UPDATE_FAILED              │
│    │                                                      │
│    └─ Invalid/unknown parameters                          │
│        → daemon may accept but download fails later       │
│        → DownloadProgress signal with ERROR status        │
│                                                           │
│  UpdateFirmware:                                          │
│    ├─ Another update already active                       │
│    │   → reply (sss): "RDKFW_UPDATE_FAILED",              │
│    │                   "REJECTED",                        │
│    │                   "Update already in progress"       │
│    │   → library returns RDKFW_UPDATE_FAILED              │
│    │                                                      │
│    └─ No firmware downloaded yet                          │
│        → daemon rejects or update fails                   │
│        → UpdateProgress signal with ERROR status          │
└───────────────────────────────────────────────────────────┘
```

### 5.4 Worker Thread Error Paths

```
┌───────────────────────────────────────────────────────────┐
│  Worker Thread Internal Error Paths                       │
│                                                           │
│  pthread_create() fails:                                  │
│    → internal_abort_*() clears in-progress flag           │
│    → ctx freed by caller                                  │
│    → return RDKFW_UPDATE_FAILED                           │
│                                                           │
│  g_bus_get_sync() fails (daemon not running):             │
│    → init_failed = true                                   │
│    → cond_signal(ready)                                   │
│    → skip g_main_loop_run()                               │
│    → cleanup, free ctx, thread exits                      │
│    → caller returns RDKFW_UPDATE_FAILED                   │
│                                                           │
│  g_dbus_connection_call_sync() fails (D-Bus error):       │
│    → init_failed = true                                   │
│    → cond_signal(ready)                                   │
│    → skip loop, cleanup, exit                             │
│    → caller returns RDKFW_UPDATE_FAILED                   │
│                                                           │
│  Timeout (no signal received within 120s/3600s):          │
│    → timeout callback fires                               │
│    → build error response, fire client callback           │
│    → g_main_loop_quit()                                   │
│    → cleanup, free, exit                                  │
└───────────────────────────────────────────────────────────┘
```

---

## 6. Interactive Menu Reference

```
┌──────────────────────────────────────────────────────────────┐
│  KnowWhereItBreaks — librdkFwupdateMgr Test Utility         │
├──────────────────────────────────────────────────────────────┤
│  Handle: (not registered)                                    │
├──────────────────────────────────────────────────────────────┤
│  INDIVIDUAL OPERATIONS                                       │
│    1  Register                    6  CheckForUpdate           │
│    2  Unregister                  7  DownloadFirmware         │
│    3  Full Lifecycle              8  UpdateFirmware           │
├──────────────────────────────────────────────────────────────┤
│  AUTOMATED SUITES                                            │
│   10  All Error/Validation Tests (fast, no daemon needed)    │
│   11  All Happy Path Tests (daemon required)                 │
│   12  Full Lifecycle Test (daemon required)                  │
│   13  ALL Tests (everything)                                 │
├──────────────────────────────────────────────────────────────┤
│  INPUT VALIDATION TESTS                                      │
│   20  NULL handle tests (all APIs)                           │
│   21  NULL callback tests (all APIs)                         │
│   22  NULL request tests (download + update)                 │
│   23  Empty string tests (firmware name, URL)                │
│   24  Unregistered handle tests (all APIs)                   │
├──────────────────────────────────────────────────────────────┤
│  LIBRARY GUARD TESTS                                         │
│   30  Duplicate CheckForUpdate (same process)                │
│   31  Duplicate Download (same process)                      │
│   32  Duplicate Update (same process)                        │
│   33  Unregister during Check                                │
│   34  Unregister during Download                             │
│   35  Unregister during Update                               │
├──────────────────────────────────────────────────────────────┤
│  RAPID RETRY TESTS                                           │
│   40  Check → complete → Check again                         │
│   41  Download → complete → Download again                   │
│   42  Update → complete → Update again                       │
├──────────────────────────────────────────────────────────────┤
│  CALLBACK VALIDATION TESTS                                   │
│   50  Check callback data validation                         │
│   51  Download progress increments                           │
│   52  Download terminal status validation                    │
│   53  Update progress increments                             │
│   54  Update terminal status validation                      │
├──────────────────────────────────────────────────────────────┤
│  CROSS-API TESTS                                             │
│   60  Check + Download simultaneously                        │
│   61  Download then Update sequentially                      │
│   62  Double register + parallel ops                         │
├──────────────────────────────────────────────────────────────┤
│    0  Exit (prints results summary)                          │
└──────────────────────────────────────────────────────────────┘
```

---

## 7. Automated Mode Reference

### 7.1 `--auto-error` (No daemon needed for most tests)

Runs all tests that validate **library-level input validation and guards**.
These tests do NOT make D-Bus calls (or expect them to fail gracefully):

```
Error Path Tests (no registration needed):
  ├─ 1.6  unregister_null_handle
  ├─ 1.5  unregister_invalid_handle
  ├─ 2.2  check_null_handle
  ├─ 2.3  check_null_callback
  ├─ 3.2  download_null_handle
  ├─ 3.3  download_null_request
  ├─ 3.4  download_null_callback
  ├─ 4.2  update_null_handle
  ├─ 4.3  update_null_request
  └─ 4.4  update_null_callback

Error Path Tests (with registration — daemon needed):
  ├─ [register]
  ├─ 3.5  download_empty_firmware_name
  ├─ 3.6  download_empty_url
  ├─ 4.5  update_empty_firmware_name
  ├─ 2.4  check_duplicate_same_process
  ├─ 3.7  download_duplicate_same_process
  ├─ 4.6  update_duplicate_same_process
  ├─ 1.7  unregister_during_check
  ├─ 1.8  unregister_during_download
  ├─ 1.9  unregister_during_update
  └─ [unregister]
```

### 7.2 `--auto-happy` (Daemon required)

```
Happy Path Tests:
  ├─ [register]
  ├─ 2.1  check_happy_path
  ├─ 2.7  check_callback_data_valid
  ├─ 2.5  check_rapid_retry
  ├─ 3.1  download_happy_path
  ├─ 3.10 download_progress_increments
  ├─ 3.11 download_terminal_status
  ├─ 3.8  download_rapid_retry
  ├─ 4.1  update_happy_path
  ├─ 4.9  update_progress_increments
  ├─ 4.10 update_terminal_status
  ├─ 4.7  update_rapid_retry
  └─ [unregister]
```

### 7.3 `--full-lifecycle` (Daemon required)

```
Full Lifecycle:
  ├─ 5.1  Register → Check → Download → Update → Unregister
  ├─ 5.2  Same but with no sleep() between calls
  └─ 5.4  Download → Update sequential (verify update works after download)
```

### 7.4 `--auto-all` (Daemon required)

Runs `--auto-error` then `--auto-happy` then `--full-lifecycle`.
Prints combined results at end.

---

## 8. Test Result Tracking

### Output Format

```
══════════════════════════════════════════════════════════════
  INPUT VALIDATION TESTS
══════════════════════════════════════════════════════════════

--- Test 1.6: Unregister NULL Handle ---
  [PASS] unregister_null_handle — unregisterProcess(NULL) returned FAILED as expected

--- Test 2.2: CheckForUpdate NULL Handle ---
  [PASS] check_null_handle — checkForUpdate(NULL, cb) returned FAILED as expected

--- Test 3.5: DownloadFirmware Empty Firmware Name ---
  [PASS] download_empty_firmware_name — Rejected empty firmwareName

--- Test 2.4: Duplicate CheckForUpdate ---
  [PASS] check_duplicate_same_process — Second call rejected by library guard
  [INFO] Waiting for first check to complete...
  [CB:Check] status=0, available=v2.0, current=v1.0
  [PASS] First check completed cleanly

══════════════════════════════════════════════════════════════
  TEST RESULTS
══════════════════════════════════════════════════════════════
  Total:   45
  Passed:  43
  Failed:  1
  Skipped: 1
══════════════════════════════════════════════════════════════
  ❌ 1 TEST(S) FAILED
══════════════════════════════════════════════════════════════
```

### Exit Code

| Code | Meaning |
|------|---------|
| 0 | All tests passed (or only skipped) |
| 1 | At least one test failed |

---

## 9. How Each Test Works Internally

### 9.1 Input Validation Test Pattern

```c
// Every input validation test follows this pattern:
static void test_check_null_handle(void)
{
    reset_callback_state();                          // Clear all volatile flags
    RdkFwUpdateStatus ret = checkForUpdate(NULL, check_callback);
    if (ret == RDKFW_UPDATE_FAILED) {
        TEST_PASS("check_null_handle");              // Library rejected it
    } else {
        TEST_FAIL("check_null_handle",
                  "Expected FAILED but got SUCCESS"); // BUG: library didn't validate
    }
}
```

**Why it works:** The library's `checkForUpdate()` checks `handle == NULL` and
returns `RDKFW_UPDATE_FAILED` before creating any thread or D-Bus connection.
No daemon interaction occurs. Fast. Deterministic.

### 9.2 Library Guard Test Pattern

```c
static void test_check_duplicate_same_process(void)
{
    reset_callback_state();

    // First call — should succeed and start worker thread
    RdkFwUpdateStatus ret1 = checkForUpdate(g_handle, check_callback);
    assert(ret1 == RDKFW_UPDATE_SUCCESS);

    // IMMEDIATELY call again — worker thread is still active
    // internal_begin_check() will see g_check_in_progress == true
    RdkFwUpdateStatus ret2 = checkForUpdate(g_handle, check_callback);

    if (ret2 == RDKFW_UPDATE_FAILED) {
        TEST_PASS("duplicate rejected");  // Library guard working
    } else {
        TEST_FAIL("duplicate NOT rejected"); // BUG: guard missing
    }

    // Wait for first to complete (cleanup)
    wait_for_callback(&g_check_callback_fired, 130);
}
```

**Why it works:** The first `checkForUpdate()` call sets `g_check_in_progress = true`
via `internal_begin_check()` and starts the worker thread. When the second call
arrives (microseconds later), `internal_begin_check()` sees `g_check_in_progress == true`
and returns `false`, causing the second call to return `RDKFW_UPDATE_FAILED`.

### 9.3 Happy Path Test Pattern

```c
static void test_download_happy_path(void)
{
    reset_callback_state();

    DownloadRequest req = { .firmwareName = "test.bin", .downloadUrl = "http://..." };

    // This call blocks briefly (condvar wait), then returns daemon's decision
    RdkFwUpdateStatus ret = downloadFirmware(g_handle, &req, download_callback);

    if (ret != RDKFW_UPDATE_SUCCESS) {
        TEST_FAIL("download_happy_path", "Daemon rejected");
        return;
    }

    // Worker thread is now running g_main_loop_run(), receiving DownloadProgress signals.
    // Our download_callback fires on each signal.
    // Wait for terminal callback (COMPLETED or ERROR).

    if (wait_for_callback(&g_dwnl_callback_fired, 3700)) {
        if (g_dwnl_status == RDKFW_DWNL_COMPLETED) {
            TEST_PASS("download completed");
        } else {
            TEST_FAIL("download_happy_path", "Terminal was ERROR not COMPLETED");
        }
    } else {
        TEST_FAIL("download_happy_path", "Timeout — no terminal callback");
    }
}
```

**Why it works:** `downloadFirmware()` returns `SUCCESS` only if the daemon
accepted the request (condvar handshake confirms daemon's reply). Then the
worker thread runs the GLib event loop, receiving `DownloadProgress` D-Bus signals.
Each signal fires `download_callback()`, which updates the volatile globals.
`wait_for_callback()` polls `g_dwnl_callback_fired` every 100ms until the terminal
callback sets it to `true`.

### 9.4 Callback Tracking

```c
// Global tracking state (volatile for cross-thread visibility)
static volatile bool g_dwnl_callback_fired   = false;   // Terminal callback received
static volatile int  g_dwnl_callback_count   = 0;       // Total callback invocations
static volatile int  g_dwnl_last_progress     = -1;      // Last progress value
static volatile int  g_dwnl_status            = -1;      // Last status value
static volatile bool g_dwnl_progress_monotonic = true;   // Progress only increased

static void download_callback(DownloadResponse *response)
{
    int prev = g_dwnl_last_progress;
    g_dwnl_callback_count++;
    g_dwnl_last_progress = response->progress;
    g_dwnl_status = response->status;

    // Track monotonicity: progress should never decrease
    if (prev >= 0 && (int)response->progress < prev) {
        g_dwnl_progress_monotonic = false;
    }

    printf("    [CB:Download] #%d progress=%u%% status=%d msg=%s\n",
           g_dwnl_callback_count, response->progress,
           response->status, response->statusMessage);

    // Mark terminal
    if (response->status == RDKFW_DWNL_COMPLETED ||
        response->status == RDKFW_DWNL_ERROR) {
        g_dwnl_callback_fired = true;
    }
}
```

### 9.5 Unregister-During-Operation Test Pattern

```c
static void test_unregister_during_download(void)
{
    reset_callback_state();

    DownloadRequest req = { ... };
    RdkFwUpdateStatus ret1 = downloadFirmware(g_handle, &req, download_callback);
    // ret1 == SUCCESS means worker thread is active, download is happening

    // IMMEDIATELY try to unregister (should be blocked)
    RdkFwUpdateStatus ret2 = unregisterProcess(g_handle);

    if (ret2 == RDKFW_UPDATE_FAILED) {
        TEST_PASS("unregister_during_download blocked");
    } else {
        TEST_FAIL("unregister_during_download",
                  "Unregister was NOT blocked — handle removed while download active!");
    }

    // Let download finish cleanly
    wait_for_callback(&g_dwnl_callback_fired, 3700);
}
```

**Why it works:** `unregisterProcess()` calls `internal_is_dwnl_in_progress()`,
which checks `g_dwnl_in_progress` under mutex. Since the download worker thread
is still running, this returns `true`, and `unregisterProcess()` returns `FAILED`.

### 9.6 Rapid Retry Test Pattern

```c
static void test_check_rapid_retry(void)
{
    // First call
    reset_callback_state();
    checkForUpdate(g_handle, check_callback);
    wait_for_callback(&g_check_callback_fired, 130);
    // Worker thread sets g_check_in_progress = false in cleanup

    sleep(1);  // Brief pause to let thread fully exit

    // Retry — should succeed because in-progress was cleared
    reset_callback_state();
    RdkFwUpdateStatus ret = checkForUpdate(g_handle, check_callback);

    if (ret == RDKFW_UPDATE_SUCCESS) {
        TEST_PASS("rapid_retry accepted");
        wait_for_callback(&g_check_callback_fired, 130);
    } else {
        TEST_FAIL("rapid_retry", "in-progress flag was not cleared after completion");
    }
}
```

**Why it works:** After the first check completes, the worker thread calls
`internal_end_check()` which sets `g_check_in_progress = false`. The 1-second
sleep ensures the worker thread has fully exited (joined). The second call's
`internal_begin_check()` sees `false` and succeeds.

---

## 10. Manual-Only Test Scenarios

These cannot be automated in KnowWhereItBreaks because they require external
actions. Document the steps for manual execution:

### 10.1 Daemon Crash During Download

```bash
# Terminal 1: Start download
./KnowWhereItBreaks
> 1   (register)
> 7   (download — enter valid firmware/URL)
# Download starts, progress callbacks appear...

# Terminal 2: Kill daemon mid-download
kill -9 $(pidof rdkFwupdateMgr)

# Terminal 1: Observe
# Expected: After timeout (up to 3600s — or sooner if D-Bus detects disconnect),
#           callback fires with ERROR status.
#           Worker thread cleans up and exits.
#           g_dwnl_in_progress is cleared.
#           Subsequent operations work after daemon restart.
```

### 10.2 Cross-Process Rejection

```bash
# Terminal 1:
./KnowWhereItBreaks
> 1   (register)
> 7   (download — starts downloading)

# Terminal 2 (simultaneously):
./KnowWhereItBreaks
> 1   (register — succeeds, different handle)
> 7   (download — same firmware)
# Expected: downloadFirmware() returns FAILED
#           (daemon rejects: "Download already in progress")
```

### 10.3 Daemon Down (All APIs)

```bash
# Stop daemon
systemctl stop rdkFwupdateMgr

# Run error tests
./KnowWhereItBreaks --auto-happy

# Expected: ALL happy path tests should return FAILED (D-Bus connect fails)
#           No crashes, no hangs, no memory leaks
```

### 10.4 Valgrind Memory Check

```bash
valgrind --leak-check=full --track-origins=yes --show-leak-kinds=all \
    ./KnowWhereItBreaks --auto-all

# Expected: 0 bytes lost
# Note: GLib may show "still reachable" blocks — these are GLib's global
#       type system caches and are NOT leaks.
```

### 10.5 Thread Sanitizer

```bash
# Rebuild with -fsanitize=thread
./KnowWhereItBreaks --auto-all

# Expected: No data race warnings
```

---

## 11. Troubleshooting

### Test hangs on wait_for_callback()

**Cause:** Worker thread is stuck in `g_main_loop_run()` — daemon never sent
the expected signal.

**Fix:** Check daemon logs (`/opt/logs/rdkFwupdateMgr.log`). The daemon may
have crashed, rejected the request, or the signal format changed.

### All happy path tests return FAILED

**Cause:** Daemon is not running.

**Fix:** `systemctl start rdkFwupdateMgr` or run daemon manually.

### Duplicate call test passes but shouldn't

**Cause:** The first `checkForUpdate()` completed so fast (before the second call)
that `g_check_in_progress` was already cleared.

**Fix:** This is actually correct behavior — the guard works, the operation was
just fast. On embedded devices with slower D-Bus, the timing will be more reliable.

### Unregister-during-operation test fails

**Cause:** `unregisterProcess()` does not check `internal_is_*_in_progress()`.

**Fix:** Verify `rdkFwupdateMgr_process.c` has the in-progress guards for all
three APIs (check, download, update).

### Progress is not monotonically increasing

**Cause:** Daemon sent progress=50 then progress=30. This is a daemon bug,
not a library bug.

**Fix:** Report to daemon team. Library correctly relays whatever daemon sends.

### Callback data has garbage values

**Cause:** Signal parse function has wrong GVariant format string.

**Fix:** Verify:
- CheckForUpdateComplete: `(tiissss)`
- DownloadProgress: `(tsuss)` 
- UpdateProgress: `(tsiis)`

Match these against daemon's `g_variant_new()` calls.
