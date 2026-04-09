# KnowWhereItBreaks (KWIB) — Developer Test Utility

**Source:** `librdkFwupdateMgr/examples/KnowWhereItBreaks.c`  
**Binary:** `KnowWhereItBreaks`  
**Version:** 1.0  
**Date:** April 2026

---

## Table of Contents

1. [What Is It?](#1-what-is-it)
2. [KWIB vs example\_app — What's the Difference?](#2-kwib-vs-example_app--whats-the-difference)
3. [Prerequisites](#3-prerequisites)
4. [Build](#4-build)
5. [Usage](#5-usage)
   - [Interactive Mode](#51-interactive-mode)
   - [Automated Modes (CI/Scripts)](#52-automated-modes-ciscripts)
6. [Test Suites](#6-test-suites)
   - [Error / Validation Tests](#61-error--validation-tests)
   - [Happy Path Tests](#62-happy-path-tests)
   - [Lifecycle Tests](#63-lifecycle-tests)
7. [Complete Test Case Catalog](#7-complete-test-case-catalog)
8. [Understanding the Output](#8-understanding-the-output)
9. [What Each Test Validates](#9-what-each-test-validates)
10. [Troubleshooting](#10-troubleshooting)
11. [Adding New Tests](#11-adding-new-tests)

---

## 1. What Is It?

**KnowWhereItBreaks** (KWIB) is a developer test binary that exercises every code path in `librdkFwupdateMgr.so` — the firmware update client library. It's your **find-the-bugs-before-they-find-you** tool.

Think of it this way:

| Tool | Purpose | Audience |
|------|---------|----------|
| `example_app` | "Here's how to use the library" | External teams, new developers |
| **`KnowWhereItBreaks`** | "Here's how to **break** the library" | Library developers, QA, CI |

KWIB systematically tests:

- ✅ **Input validation** — What happens when you pass NULL, empty strings, garbage?
- ✅ **Concurrency guards** — What happens when you call the same API twice simultaneously?
- ✅ **Session guards** — What happens when you unregister while an operation is running?
- ✅ **Happy paths** — Do register, check, download, and flash actually work end-to-end?
- ✅ **Callback correctness** — Does the callback fire the right number of times with valid data?
- ✅ **Progress monotonicity** — Do download/flash progress percentages always go up, never backward?
- ✅ **Rapid retry** — Can you immediately call an API again after the previous one completes?
- ✅ **Full lifecycle** — Register → Check → Download → Flash → Unregister in one shot

---

## 2. KWIB vs example_app — What's the Difference?

| Aspect | `example_app` | `KnowWhereItBreaks` |
|--------|--------------|---------------------|
| **Purpose** | Clean reference app for external teams | Comprehensive developer test utility |
| **Approach** | One happy path, start to finish | 39 test cases covering every edge case |
| **Error injection** | None — expects everything to work | Deliberately passes NULL, empty, duplicates |
| **Daemon required?** | Yes — always | Partially — error tests run without daemon |
| **Output** | Pretty workflow boxes | PASS/FAIL/SKIP with test counts |
| **Modes** | Run and watch | Interactive menu + 4 automated modes |
| **CI friendly?** | No (interactive) | Yes (`--auto-all` returns exit code 0/1) |
| **Tests guards?** | No | Yes — duplicate calls, unregister during op |
| **Tests callbacks?** | Informational print | Validates count, data, monotonicity |

**Rule of thumb:** Use `example_app` to *see how things work*. Use `KnowWhereItBreaks` to *make sure they still work after you change something*.

---

## 3. Prerequisites

### The firmware daemon must be running (for happy path / lifecycle tests)

```bash
systemctl status rdkFwupdateMgr
# If not running:
systemctl start rdkFwupdateMgr
```

> **Note:** Error/validation tests (TC03, TC06–TC08, TC13–TC17, TC22, TC24–TC28, TC33) run without the daemon. They only test the library's local input validation.

### The library must be installed and findable

```bash
ls -l /usr/lib/librdkFwupdateMgr.so*

# If in a non-standard path:
export LD_LIBRARY_PATH=/path/to/lib:$LD_LIBRARY_PATH
```

### D-Bus permissions must allow the call

```bash
cat /etc/dbus-1/system.d/rdkFwupdateMgr.conf
# Ensure your user (or root) is permitted
```

---

## 4. Build

### Quick native build on-device

```bash
cd librdkFwupdateMgr/examples

gcc KnowWhereItBreaks.c \
    -o KnowWhereItBreaks \
    -I../include \
    -L/usr/lib \
    -lrdkFwupdateMgr \
    $(pkg-config --cflags --libs gio-2.0) \
    -lpthread
```

### Through autotools (cross-build / Yocto)

```bash
make KnowWhereItBreaks
make install    # installs to $(DESTDIR)$(bindir)/KnowWhereItBreaks
```

---

## 5. Usage

### 5.1 Interactive Mode

```bash
./KnowWhereItBreaks
```

This shows a menu where you can run individual tests or full suites:

```
┌──────────────────────────────────────────────────────────────┐
│  KnowWhereItBreaks v1.0 — librdkFwupdateMgr Test Utility    │
├──────────────────────────────────────────────────────────────┤
│  Handle: (not registered)                                    │
├──────────────────────────────────────────────────────────────┤
│  AUTOMATED SUITES                                            │
│   10  All Error/Validation Tests (fast)                      │
│   11  All Happy Path Tests (daemon needed)                   │
│   12  Full Lifecycle Tests (daemon needed)                   │
│   13  ALL Tests (everything)                                 │
├──────────────────────────────────────────────────────────────┤
│  REGISTER / UNREGISTER            CHECKFORUPDATE             │
│    1  TC01 Register happy           5  TC05 Check happy      │
│    2  TC02 Unregister happy         6  TC06 NULL handle      │
│    ...                              ...                      │
├──────────────────────────────────────────────────────────────┤
│  DOWNLOAD FIRMWARE                 UPDATE FIRMWARE            │
│   50  TC12 Download happy          60  TC23 Update happy     │
│   ...                              ...                       │
├──────────────────────────────────────────────────────────────┤
│  GUARDS / LIFECYCLE                                          │
│   80  TC34 Unreg during check      90  TC37 Full lifecycle   │
│   ...                              ...                       │
├──────────────────────────────────────────────────────────────┤
│    0  Exit (print results)                                   │
└──────────────────────────────────────────────────────────────┘
  Choice: _
```

Type a number and press Enter to run that test. Type `0` to exit and see the summary.

### 5.2 Automated Modes (CI/Scripts)

For CI pipelines, cron jobs, or scripted testing, use command-line flags:

| Command | What It Runs | Daemon Needed? | Duration |
|---------|-------------|----------------|----------|
| `./KnowWhereItBreaks --auto-error` | All error/validation + guard tests | Partially (registration needs daemon) | ~30s |
| `./KnowWhereItBreaks --auto-happy` | All happy path tests | Yes | ~15–20 min |
| `./KnowWhereItBreaks --full-lifecycle` | End-to-end lifecycle tests | Yes | ~20–30 min |
| `./KnowWhereItBreaks --auto-all` | Everything (error + happy + lifecycle) | Yes | ~40–60 min |

**Exit code:**
- `0` = All tests passed (or skipped)
- `1` = One or more tests failed

**CI example:**
```bash
#!/bin/bash
systemctl start rdkFwupdateMgr
sleep 2
./KnowWhereItBreaks --auto-all
exit $?
```

---

## 6. Test Suites

### 6.1 Error / Validation Tests (`--auto-error`)

These tests verify the library **rejects bad inputs immediately** without crashing, leaking, or talking to the daemon unnecessarily.

**What runs (in order):**

| Phase | Tests | What It Checks |
|-------|-------|----------------|
| Input validation (no daemon) | TC03, TC06, TC08, TC13, TC22, TC24, TC33 | NULL handle, empty handle → FAIL |
| Input validation (with daemon) | TC07, TC14–TC17, TC25–TC28 | NULL callback, NULL request, NULL/empty firmware name → FAIL |
| Duplicate call guards | TC09, TC18, TC29 | Second concurrent call to same API → rejected |
| Unregister guards | TC34, TC35, TC36 | Unregister during active check/download/update → blocked |

**Typical time:** ~30 seconds (most tests return immediately)

### 6.2 Happy Path Tests (`--auto-happy`)

These tests verify **everything works correctly when inputs are valid** and the daemon is running.

**What runs (in order):**

| Phase | Tests | What It Checks |
|-------|-------|----------------|
| Register | TC01, TC04 | Single and double registration succeed |
| CheckForUpdate | TC05, TC11, TC10 | Callback fires once with valid data; rapid retry works |
| DownloadFirmware | TC12, TC20, TC21, TC19 | Download completes; progress is monotonic; terminal status correct; rapid retry works |
| UpdateFirmware | TC23, TC31, TC32, TC30 | Flash completes; progress is monotonic; terminal status correct; rapid retry works |
| Unregister | TC02 | Clean unregistration |

**Typical time:** 15–20 minutes (depends on firmware download/flash speed)

### 6.3 Lifecycle Tests (`--full-lifecycle`)

These tests verify the **complete workflow** works end-to-end.

| Test | What It Does |
|------|-------------|
| TC37 | Register → Check → Download → Flash → Unregister (with `sleep(1)` between steps) |
| TC38 | Same as TC37, but **no sleeps** between steps (stress test for worker cleanup timing) |
| TC39 | Start `checkForUpdate()` and `downloadFirmware()` simultaneously (independent guards) |

**Typical time:** 20–30 minutes

---

## 7. Complete Test Case Catalog

### Register / Unregister

| ID | Menu # | Test Name | Input | Expected Result | Daemon? |
|----|--------|-----------|-------|-----------------|---------|
| TC01 | 1 | Register Happy Path | Valid name + version | Non-NULL handle returned | Yes |
| TC02 | 2 | Unregister Happy Path | Valid handle | Completes without crash | Yes |
| TC03 | 3 | Unregister NULL Handle | `NULL` | No crash (no-op) | No |
| TC04 | 4 | Double Register | Two different process names | Both get unique handles | Yes |

### CheckForUpdate

| ID | Menu # | Test Name | Input | Expected Result | Daemon? |
|----|--------|-----------|-------|-----------------|---------|
| TC05 | 5 | Check Happy Path | Valid handle + callback | Callback fires with valid FwInfoData | Yes |
| TC06 | 6 | NULL Handle | `NULL` handle | Returns `CHECK_FOR_UPDATE_FAIL` | No |
| TC07 | 7 | NULL Callback | `NULL` callback | Returns `CHECK_FOR_UPDATE_FAIL` | Yes* |
| TC08 | 8 | Empty Handle | `""` | Returns `CHECK_FOR_UPDATE_FAIL` | No |
| TC09 | 9 | Duplicate Call | Call twice simultaneously | Second call returns FAIL | Yes |
| TC10 | 40 | Rapid Retry | Call again immediately after first completes | Second call succeeds | Yes |
| TC11 | 41 | Callback Data Validation | Normal check | Callback fires exactly once; status in valid range | Yes |

*\* Needs a registered handle, which needs daemon*

### DownloadFirmware

| ID | Menu # | Test Name | Input | Expected Result | Daemon? |
|----|--------|-----------|-------|-----------------|---------|
| TC12 | 50 | Download Happy Path | Valid request | Download completes (DWNL_COMPLETED) | Yes |
| TC13 | 51 | NULL Handle | `NULL` handle | Returns `RDKFW_DWNL_FAILED` | No |
| TC14 | 52 | NULL Request | `NULL` FwDwnlReq | Returns `RDKFW_DWNL_FAILED` | Yes* |
| TC15 | 53 | NULL Callback | `NULL` callback | Returns `RDKFW_DWNL_FAILED` | Yes* |
| TC16 | 54 | NULL Firmware Name | `firmwareName = NULL` | Returns `RDKFW_DWNL_FAILED` | Yes* |
| TC17 | 55 | Empty Firmware Name | `firmwareName = ""` | Returns `RDKFW_DWNL_FAILED` | Yes* |
| TC18 | 56 | Duplicate Call | Call twice simultaneously | Second call returns FAIL | Yes |
| TC19 | 57 | Rapid Retry | Call again after first completes | Second call succeeds | Yes |
| TC20 | 58 | Progress Monotonicity | (uses TC12 data) | Progress never decreases | — |
| TC21 | 59 | Terminal Status | (uses TC12 data) | Last callback has COMPLETED or ERROR | — |
| TC22 | — | Empty Handle | `""` handle | Returns `RDKFW_DWNL_FAILED` | No |

### UpdateFirmware

| ID | Menu # | Test Name | Input | Expected Result | Daemon? |
|----|--------|-----------|-------|-----------------|---------|
| TC23 | 60 | Update Happy Path | Valid request | Flash completes (UPDATE_COMPLETED) | Yes |
| TC24 | 61 | NULL Handle | `NULL` handle | Returns `RDKFW_UPDATE_FAILED` | No |
| TC25 | 62 | NULL Request | `NULL` FwUpdateReq | Returns `RDKFW_UPDATE_FAILED` | Yes* |
| TC26 | 63 | NULL Callback | `NULL` callback | Returns `RDKFW_UPDATE_FAILED` | Yes* |
| TC27 | 64 | NULL Firmware Name | `firmwareName = NULL` | Returns `RDKFW_UPDATE_FAILED` | Yes* |
| TC28 | 65 | Empty Firmware Name | `firmwareName = ""` | Returns `RDKFW_UPDATE_FAILED` | Yes* |
| TC29 | 66 | Duplicate Call | Call twice simultaneously | Second call returns FAIL | Yes |
| TC30 | 67 | Rapid Retry | Call again after first completes | Second call succeeds | Yes |
| TC31 | 68 | Progress Monotonicity | (uses TC23 data) | Progress never decreases | — |
| TC32 | 69 | Terminal Status | (uses TC23 data) | Last callback has COMPLETED or ERROR | — |
| TC33 | 70 | Empty Handle | `""` handle | Returns `RDKFW_UPDATE_FAILED` | No |

### Guard Tests

| ID | Menu # | Test Name | What It Does | Expected Result | Daemon? |
|----|--------|-----------|-------------|-----------------|---------|
| TC34 | 80 | Unregister During Check | Start check, then immediately unregister | Unregister blocked; callback still fires | Yes |
| TC35 | 81 | Unregister During Download | Start download, then immediately unregister | Unregister blocked; callbacks still fire | Yes |
| TC36 | 82 | Unregister During Update | Start update, then immediately unregister | Unregister blocked; callbacks still fire | Yes |

### Full Lifecycle

| ID | Menu # | Test Name | Steps | Daemon? |
|----|--------|-----------|-------|---------|
| TC37 | 90 | Full Lifecycle | Register → Check → Download → Flash → Unregister (with sleeps) | Yes |
| TC38 | 91 | No-Sleep Lifecycle | Same as TC37 but no sleeps between steps | Yes |
| TC39 | 92 | Simultaneous Check + Download | Start check and download at the same time | Yes |

---

## 8. Understanding the Output

### Test result indicators

```
  [PASS] TC06 — NULL handle rejected              ← Green: test passed
  [FAIL] TC05 — checkForUpdate() — Callback never fired (130s timeout)
                                                   ← Red: test failed (with reason)
  [SKIP] TC12 — No handle                         ← Yellow: test skipped (precondition not met)
  [INFO] Waiting for callback (max 130s)...        ← Informational message
```

### Callback trace

During active operations, you'll see real-time callback output:

```
    [CB:Check]  #1 status=0 current='RDKV_2.5.0'
    [CB:Dwnl]   #1 progress=0%   status=0
    [CB:Dwnl]   #2 progress=25%  status=0
    [CB:Dwnl]   #3 progress=50%  status=0
    [CB:Dwnl]   #4 progress=75%  status=0
    [CB:Dwnl]   #5 progress=100% status=1
    [CB:Update]  #1 progress=0%  status=0
    [CB:Update]  #2 progress=100% status=1
```

### Final summary

```
══════════════════════════════════════════════════════════════
  KnowWhereItBreaks — TEST RESULTS
══════════════════════════════════════════════════════════════
  Total:   39
  Passed:  37
  Failed:  0
  Skipped: 2
══════════════════════════════════════════════════════════════
  ✅ ALL TESTS PASSED
══════════════════════════════════════════════════════════════
```

---

## 9. What Each Test Validates

### Layer 1: Library Input Validation (no IPC at all)

These tests verify the library rejects bad inputs **before** any D-Bus call happens.

```
Your App                 Library                 Daemon
────────                 ───────                 ──────
checkForUpdate(NULL) ──▶ if (handle == NULL)
                          return FAIL;           (never contacted)
```

**Tests:** TC03, TC06, TC07, TC08, TC13–TC17, TC22, TC24–TC28, TC33

### Layer 2: Library Concurrency Guards

These tests verify the library rejects duplicate concurrent calls.

```
Thread A: checkForUpdate() → SUCCESS (worker running)
Thread B: checkForUpdate() → FAIL   (g_check_in_progress == true)
```

**Tests:** TC09, TC18, TC29

### Layer 3: Library Session Guards

These tests verify the library blocks `unregisterProcess()` while operations are active.

```
checkForUpdate() → SUCCESS (worker running)
unregisterProcess() → BLOCKED (internal_is_check_in_progress() == true)
                      Callback still fires normally.
```

**Tests:** TC34, TC35, TC36

### Layer 4: Condvar Handshake + Daemon Communication

These tests verify the worker thread starts correctly, talks to the daemon, and the return value accurately reflects the daemon's reply.

**Tests:** TC01, TC04, TC05, TC12, TC23

### Layer 5: Callback Correctness

These tests verify callbacks fire the right number of times with valid data.

| API | Expected callback count | Validated by |
|-----|------------------------|-------------|
| `checkForUpdate` | Exactly 1 | TC11 |
| `downloadFirmware` | Multiple (≥2), monotonically increasing progress | TC20, TC21 |
| `updateFirmware` | Multiple (≥2), monotonically increasing progress | TC31, TC32 |

### Layer 6: Worker Thread Cleanup

These tests verify that after an operation completes, the in-progress flag is properly cleared and a new call can proceed immediately.

**Tests:** TC10, TC19, TC30, TC38

### Layer 7: Full End-to-End

These tests verify the complete register → check → download → flash → unregister workflow.

**Tests:** TC37, TC38, TC39

---

## 10. Troubleshooting

### All tests show SKIP

**Cause:** Daemon is not running. `registerProcess()` returns NULL, so all subsequent tests are skipped.

**Fix:**
```bash
systemctl start rdkFwupdateMgr
```

### TC05/TC12/TC23 timeout (callback never fired)

**Possible causes:**
1. Daemon accepted the request but the operation failed silently
2. D-Bus signal was emitted but not received (subscription issue)
3. Worker thread crashed before firing the callback

**Debug steps:**
```bash
# Check daemon logs
journalctl -u rdkFwupdateMgr -f

# Check D-Bus traffic
sudo dbus-monitor --system "interface='org.rdkfwupdater.Interface'"

# Check for crashed worker threads
# (look for FWUPMGR_ERROR messages in stderr)
./KnowWhereItBreaks --auto-happy 2>&1 | grep -i error
```

### TC09/TC18/TC29 FAIL ("Second call was NOT rejected")

**Cause:** The in-progress guard is broken. The second call was accepted while the first is still running.

**This is a real bug.** Check `internal_begin_check()` / `internal_begin_download()` / `internal_begin_update()` in `rdkFwupdateMgr_async.c`.

### TC10/TC19/TC30 FAIL ("Rejected — in-progress flag not cleared?")

**Cause:** The worker thread from the previous operation didn't call `internal_end_*()` before exiting. The in-progress flag is stuck at `true`.

**This is a real bug.** Check the worker thread cleanup path in `rdkFwupdateMgr_async.c` — ensure `internal_end_*()` is called in ALL exit paths (success, failure, timeout).

### TC34/TC35/TC36 FAIL ("Callback never fired — unregister may have succeeded")

**Cause:** The `unregisterProcess()` guard (`internal_is_*_in_progress()`) didn't block the unregister, and the handle was freed while the worker was still using it.

**This is a real bug.** Check the guard sequence in `rdkFwupdateMgr_process.c` `unregisterProcess()`.

### TC20/TC31 FAIL ("Progress decreased at some point")

**Cause:** The daemon emitted a progress signal with a lower percentage than a previous one (e.g., went from 50% back to 25%).

**This is a daemon bug.** Check the progress monitoring logic in `rdkFwupdateMgr_handlers.c`.

---

## 11. Adding New Tests

To add a new test case:

### 1. Write the test function

```c
static void tc40_my_new_test(void)
{
    printf("\n--- TC40: My New Test ---\n");
    
    // Setup
    if (!ensure_registered()) { TEST_SKIP("TC40", "No handle"); return; }
    
    // Action
    // ... your test logic ...
    
    // Assertion
    if (/* success condition */)
        TEST_PASS("TC40 — description of what passed");
    else
        TEST_FAIL("TC40 — description", "what went wrong");
}
```

### 2. Add it to the appropriate suite runner

```c
static void run_happy_tests(void)
{
    // ...existing tests...
    tc40_my_new_test();    // ← add here
    sleep(2);
}
```

### 3. Add it to the interactive menu switch

```c
case 93: tc40_my_new_test(); break;
```

### 4. Update the menu display

```c
printf("│   93  TC40 My new test                                       │\n");
```

### Test naming convention

- `tcXX_<api>_<what>` — e.g., `tc40_download_null_url`
- Use `TEST_PASS` / `TEST_FAIL` / `TEST_SKIP` macros for consistent output
- Always call `reset_all()` before tests that use callbacks
- Always call `ensure_registered()` if the test needs a handle
- Use `wait_flag()` with appropriate timeouts for async operations

---

## Quick Reference

```
┌─────────────────────────────────────────────────────────────────────┐
│                   KnowWhereItBreaks Quick Reference                  │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  RUN MODES:                                                          │
│    ./KnowWhereItBreaks                    Interactive menu            │
│    ./KnowWhereItBreaks --auto-error       Error tests only (~30s)    │
│    ./KnowWhereItBreaks --auto-happy       Happy paths (~15-20min)    │
│    ./KnowWhereItBreaks --full-lifecycle   End-to-end (~20-30min)     │
│    ./KnowWhereItBreaks --auto-all         Everything (~40-60min)     │
│                                                                      │
│  EXIT CODES:                                                         │
│    0 = All tests passed       1 = One or more failed                 │
│                                                                      │
│  TEST COUNT:  39 test cases                                          │
│    TC01–TC04   Register / Unregister         (4 tests)               │
│    TC05–TC11   CheckForUpdate                (7 tests)               │
│    TC12–TC22   DownloadFirmware              (11 tests)              │
│    TC23–TC33   UpdateFirmware                (11 tests)              │
│    TC34–TC36   Unregister-during-op guards   (3 tests)               │
│    TC37–TC39   Full lifecycle                (3 tests)               │
│                                                                      │
│  WHAT IT TESTS (7 layers):                                           │
│    L1: Input validation (NULL, empty, bad args)                      │
│    L2: Concurrency guards (duplicate call rejection)                 │
│    L3: Session guards (unregister blocked during ops)                │
│    L4: Condvar handshake + daemon communication                      │
│    L5: Callback correctness (count, data, monotonicity)              │
│    L6: Worker cleanup (in-progress flag cleared for retry)           │
│    L7: Full end-to-end lifecycle                                     │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```
