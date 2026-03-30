# KnowWhereItBreaks — Developer Test Utility for librdkFwupdateMgr.so

## Overview

`KnowWhereItBreaks` is a **comprehensive developer test utility** that exercises every code path in `librdkFwupdateMgr.so` and the `rdkFwupdateMgr` daemon.

It is **NOT** the `example_plugin` (which stays clean as a reference for external teams).  
It is your personal weapon for finding bugs before they find you.

**39 test cases** covering input validation, library guards, daemon rejection, happy paths, callback correctness, rapid retry, cross-API interaction, and full lifecycle.

---

## Build & Install

The binary is built alongside `example_plugin` via the top-level `Makefile.am`:

```bash
# Standard build (Autotools)
./configure && make

# Binary produced: KnowWhereItBreaks
# Installed to rootfs alongside example_plugin, rdkFwupdateMgr, etc.
```

No separate build step needed — it compiles and installs as part of the standard build.

---

## Usage

```bash
# Prerequisites: daemon must be running for happy path tests
systemctl start rdkFwupdateMgr

# Interactive menu — explore and run tests one by one
./KnowWhereItBreaks

# Automated: error/validation tests (fast, most don't need daemon)
./KnowWhereItBreaks --auto-error

# Automated: happy path tests (daemon REQUIRED)
./KnowWhereItBreaks --auto-happy

# Automated: full lifecycle tests (daemon REQUIRED)
./KnowWhereItBreaks --full-lifecycle

# Automated: run EVERYTHING
./KnowWhereItBreaks --auto-all

# Exit code: 0 = all passed, 1 = at least one failure
echo $?
```

---

## Test Case Catalog

### Category 1: Register / Unregister (TC01–TC04)

| TC ID | Name | Daemon? | Description |
|-------|------|:-------:|-------------|
| **TC01** | Register Happy Path | ✅ | Call `registerProcess("KnowWhereItBreaks", "1.0.0")`. Verify it returns a non-NULL, non-empty handle string. This proves the daemon is running, D-Bus communication works, and the daemon successfully allocated a handler ID for this client. |
| **TC02** | Unregister Happy Path | ✅ | After a successful registration, call `unregisterProcess(handle)`. Verify it completes without crash. This proves the daemon accepts the unregistration and deallocates the handler. |
| **TC03** | Unregister NULL Handle | ❌ | Call `unregisterProcess(NULL)`. Per the API contract ("safe to call with NULL"), this must not crash or cause undefined behavior. Validates the library's NULL guard in the unregister path. |
| **TC04** | Double Register | ✅ | Call `registerProcess()` twice with different process names. Both should succeed with different handles. Proves the daemon supports multiple simultaneous clients. Both handles are unregistered after the test. |

### Category 2: CheckForUpdate (TC05–TC11)

| TC ID | Name | Daemon? | Description |
|-------|------|:-------:|-------------|
| **TC05** | CheckForUpdate Happy Path | ✅ | Call `checkForUpdate(handle, callback)` with a valid handle. Verify it returns `CHECK_FOR_UPDATE_SUCCESS`. Wait up to 130s for the callback. Verify the callback fires. This exercises the full path: library creates worker thread → D-Bus method call → daemon queries XConf → daemon emits CheckForUpdateComplete signal → worker thread receives signal → worker parses GVariant `(tiissss)` → callback fires with `FwInfoData`. |
| **TC06** | NULL Handle | ❌ | Call `checkForUpdate(NULL, callback)`. Must return `CHECK_FOR_UPDATE_FAIL` immediately. Validates the library's input validation at the very first line of `checkForUpdate()` in `rdkFwupdateMgr_api.c`. No D-Bus call, no thread created. |
| **TC07** | NULL Callback | ❌ | Call `checkForUpdate(handle, NULL)`. Must return `CHECK_FOR_UPDATE_FAIL`. A NULL callback would mean no way to deliver results — the library correctly rejects this before spawning a worker thread. |
| **TC08** | Empty Handle | ❌ | Call `checkForUpdate("", callback)`. Must return `CHECK_FOR_UPDATE_FAIL`. An empty string handle is not a valid session ID — the daemon would reject it anyway, but the library catches it early. |
| **TC09** | Duplicate (Same Process) | ✅ | Call `checkForUpdate()` twice in rapid succession from the same process. The first call should return SUCCESS. The second call (made while the first worker thread is still active) should return FAIL. This validates the `internal_begin_check()` guard: the library sets `g_check_in_progress = true` atomically, and the second call sees it and rejects. After the first completes, we wait for the callback so state is clean for subsequent tests. |
| **TC10** | Rapid Retry | ✅ | Call `checkForUpdate()`, wait for callback, then immediately call it again. The second call should succeed (the guard was cleared when the first worker thread called `internal_end_check()` in cleanup). Validates that the in-progress flag is properly reset after operation completes — no permanent lockout. |
| **TC11** | Callback Data Validation | ✅ | Call `checkForUpdate()` and examine the callback's data: (a) callback fired exactly once (not zero, not twice), (b) `status` field is a valid `CheckForUpdateStatus` enum value (0–5). This validates signal parsing, GVariant deserialization, and the `internal_map_status_code()` mapping. |

### Category 3: DownloadFirmware (TC12–TC22)

| TC ID | Name | Daemon? | Description |
|-------|------|:-------:|-------------|
| **TC12** | Download Happy Path | ✅ | Call `downloadFirmware(handle, &req, callback)` with valid inputs. Verify it returns `RDKFW_DWNL_SUCCESS` (meaning the daemon accepted the download request via synchronous D-Bus reply). Wait for terminal callback (`DWNL_COMPLETED` or `DWNL_ERROR`). This exercises: library creates worker thread → D-Bus `DownloadFirmware` method call (synchronous, reads `(sss)` reply) → condvar handshake → worker enters event loop → daemon emits DownloadProgress signals `(tsuss)` → callback fires multiple times → loop quits on terminal status → cleanup. |
| **TC13** | NULL Handle | ❌ | `downloadFirmware(NULL, &req, cb)` → `RDKFW_DWNL_FAILED`. Library validation. |
| **TC14** | NULL Request | ❌ | `downloadFirmware(handle, NULL, cb)` → `RDKFW_DWNL_FAILED`. Library validation. |
| **TC15** | NULL Callback | ❌ | `downloadFirmware(handle, &req, NULL)` → `RDKFW_DWNL_FAILED`. Library validation. |
| **TC16** | NULL Firmware Name | ❌ | `req.firmwareName = NULL` → `RDKFW_DWNL_FAILED`. Library checks `firmwareName != NULL`. |
| **TC17** | Empty Firmware Name | ❌ | `req.firmwareName = ""` → `RDKFW_DWNL_FAILED`. Library checks `firmwareName[0] != '\0'`. |
| **TC18** | Duplicate (Same Process) | ✅ | Two `downloadFirmware()` calls in rapid succession. First returns SUCCESS. Second returns FAILED (`internal_begin_download()` sees `g_dwnl_in_progress == true`). Validates the per-API in-progress guard is working independently from CheckForUpdate's guard. |
| **TC19** | Rapid Retry | ✅ | Download → wait for completion → immediately download again. Second call should succeed (guard cleared by `internal_end_download()` in first worker's cleanup). |
| **TC20** | Progress Monotonicity | ✅ | After TC12 runs, examine the recorded progress values. Verify: (a) multiple callbacks fired (not just one), (b) progress values never decreased. A non-monotonic progress indicates a daemon bug (library just relays what daemon sends). |
| **TC21** | Terminal Status | ✅ | After TC12 runs, verify the final callback had a terminal status (`DWNL_COMPLETED` or `DWNL_ERROR`), not `DWNL_IN_PROGRESS`. This validates that `on_download_signal_handler()` correctly identifies terminal states and quits the event loop. |
| **TC22** | Empty Handle | ❌ | `downloadFirmware("", &req, cb)` → `RDKFW_DWNL_FAILED`. |

### Category 4: UpdateFirmware (TC23–TC33)

| TC ID | Name | Daemon? | Description |
|-------|------|:-------:|-------------|
| **TC23** | Update Happy Path | ✅ | Call `updateFirmware(handle, &req, callback)` with valid inputs. Verify `RDKFW_UPDATE_SUCCESS` return (daemon accepted). Wait for terminal callback (`UPDATE_COMPLETED`). Exercises: worker thread → synchronous D-Bus `UpdateFirmware` call → `(sss)` reply parsing → condvar handshake → event loop → `UpdateProgress` signals `(tsiis)` → callback N times → terminal quit → cleanup. |
| **TC24** | NULL Handle | ❌ | → `RDKFW_UPDATE_FAILED`. |
| **TC25** | NULL Request | ❌ | → `RDKFW_UPDATE_FAILED`. |
| **TC26** | NULL Callback | ❌ | → `RDKFW_UPDATE_FAILED`. |
| **TC27** | NULL Firmware Name | ❌ | `req.firmwareName = NULL` → `RDKFW_UPDATE_FAILED`. |
| **TC28** | Empty Firmware Name | ❌ | `req.firmwareName = ""` → `RDKFW_UPDATE_FAILED`. |
| **TC29** | Duplicate (Same Process) | ✅ | Two `updateFirmware()` calls. Second rejected by `internal_begin_update()`. |
| **TC30** | Rapid Retry | ✅ | Update → wait → update again. Second succeeds (guard cleared). |
| **TC31** | Progress Monotonicity | ✅ | Multiple callbacks, progress never decreases. |
| **TC32** | Terminal Status | ✅ | Final callback = `UPDATE_COMPLETED` or `UPDATE_ERROR`. |
| **TC33** | Empty Handle | ❌ | → `RDKFW_UPDATE_FAILED`. |

### Category 5: Unregister During Active Operations (TC34–TC36)

| TC ID | Name | Daemon? | Description |
|-------|------|:-------:|-------------|
| **TC34** | Unregister During Check | ✅ | Start `checkForUpdate()`, then immediately call `unregisterProcess()`. The library's `unregisterProcess()` checks `internal_is_check_in_progress()` — if active, the unregister should be blocked (no-op). Verify by checking that the check callback still fires (handle was not invalidated). This proves the session guard in `rdkFwupdateMgr_process.c` prevents mid-operation handle destruction. |
| **TC35** | Unregister During Download | ✅ | Same pattern as TC34 but with `downloadFirmware()`. Start download → immediately unregister → verify download terminal callback still fires. |
| **TC36** | Unregister During Update | ✅ | Same pattern but with `updateFirmware()`. Start update → immediately unregister → verify update terminal callback still fires. |

### Category 6: Full Lifecycle & Cross-API (TC37–TC39)

| TC ID | Name | Daemon? | Description |
|-------|------|:-------:|-------------|
| **TC37** | Full Lifecycle | ✅ | **The integration test.** Register → CheckForUpdate (wait for callback) → DownloadFirmware (wait for completion) → UpdateFirmware (wait for completion) → Unregister. All five steps must succeed sequentially. This validates the entire library + daemon interaction end-to-end, including: handle lifetime, worker thread creation/destruction for all three APIs, D-Bus signal subscription/unsubscription, condvar handshake accuracy, callback delivery, and cleanup. |
| **TC38** | Lifecycle No Sleeps | ✅ | Same as TC37 but with NO `sleep()` between API calls. After checkForUpdate callback fires, immediately call downloadFirmware (no 1s pause). After download completes, immediately call updateFirmware. This stress-tests worker thread cleanup timing: the previous worker must have called `internal_end_*()` and fully exited before the next call's `internal_begin_*()` succeeds. If the library has a cleanup race, this test will catch it. |
| **TC39** | Check + Download Simultaneous | ✅ | Start `checkForUpdate()`, then immediately start `downloadFirmware()` (while check worker is still active). Both should succeed because they use **independent** in-progress guards (`g_check_in_progress` vs `g_dwnl_in_progress`). If the library incorrectly uses a single global guard, the second call would be rejected. |

---

## Test Summary

| Category | TCs | Tests | What's validated |
|----------|:---:|:-----:|-----------------|
| Register/Unregister | TC01–TC04 | 4 | Handle creation, cleanup, NULL safety, multi-client |
| CheckForUpdate | TC05–TC11 | 7 | Happy path, input validation, guard, retry, callback data |
| DownloadFirmware | TC12–TC22 | 11 | Happy path, input validation, guard, retry, progress, terminal |
| UpdateFirmware | TC23–TC33 | 11 | Happy path, input validation, guard, retry, progress, terminal |
| Unregister Guards | TC34–TC36 | 3 | Session protection during active operations |
| Full Lifecycle | TC37–TC39 | 3 | End-to-end integration, timing, cross-API independence |
| **Total** | | **39** | |

---

## Automated Modes

### `--auto-error` (Fast — most tests don't need daemon)

Runs: TC03, TC06, TC08, TC13, TC22, TC24, TC33, TC07, TC14–TC17, TC25–TC28, TC09, TC18, TC29, TC34–TC36

Tests library-level input validation and in-progress guards. The NULL/empty/missing-field tests don't create worker threads or D-Bus connections. The duplicate and unregister-during-operation tests need the daemon.

### `--auto-happy` (Daemon required)

Runs: TC01, TC04, TC05, TC11, TC10, TC12, TC20, TC21, TC19, TC23, TC31, TC32, TC30, TC02

Full happy path with callback validation and retry tests. Takes longer (waits for daemon responses).

### `--full-lifecycle` (Daemon required)

Runs: TC37, TC38, TC39

End-to-end integration tests.

### `--auto-all`

Runs all three suites sequentially. Use for pre-commit validation.

---

## Output Format

```
══════════════════════════════════════════════════════════════
  ERROR TESTS — INPUT VALIDATION (library-level, fast)
══════════════════════════════════════════════════════════════

--- TC03: Unregister NULL Handle ---
  [PASS] TC03 — unregisterProcess(NULL) did not crash

--- TC06: CheckForUpdate NULL Handle ---
  [PASS] TC06 — NULL handle rejected

--- TC09: CheckForUpdate Duplicate (Same Process) ---
  [PASS] TC09 — duplicate call rejected by library guard
  [INFO] Waiting for first check to complete...
    [CB:Check] #1 status=0 current='v1.0.0'

══════════════════════════════════════════════════════════════
  KnowWhereItBreaks — TEST RESULTS
══════════════════════════════════════════════════════════════
  Total:   39
  Passed:  37
  Failed:  1
  Skipped: 1
══════════════════════════════════════════════════════════════
  ❌ 1 TEST(S) FAILED
══════════════════════════════════════════════════════════════
```

Exit code: `0` = all passed, `1` = at least one failure.

---

## Manual-Only Scenarios (Not in Automated Tests)

These require external actions that cannot be automated in KnowWhereItBreaks:

| Scenario | How to Test |
|----------|-------------|
| **Daemon down** | Stop daemon → `./KnowWhereItBreaks --auto-happy` → all happy paths should return FAIL. No crashes, no hangs. |
| **Daemon crash mid-download** | Start download (menu option 50) → in another terminal: `kill -9 $(pidof rdkFwupdateMgr)` → verify callback eventually fires with `DWNL_ERROR` (timeout path). |
| **Daemon crash mid-update** | Same but with update (menu option 60). |
| **Cross-process rejection** | Run two instances of KnowWhereItBreaks. Both register. Both try to download simultaneously. One should succeed, the other should get `RDKFW_DWNL_FAILED` (daemon rejects). |
| **Memory leak check** | `valgrind --leak-check=full ./KnowWhereItBreaks --auto-all` — expect 0 bytes lost (GLib "still reachable" is normal). |
| **Thread sanitizer** | Rebuild with `-fsanitize=thread`, run `--auto-all`, expect no data race warnings. |
| **Network failure during download** | Disconnect network after download starts → verify `DWNL_ERROR` callback fires. |

---

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                   KnowWhereItBreaks                     │
│                                                         │
│  ┌──────────┐  ┌──────────────┐  ┌──────────────────┐  │
│  │ 39 Test  │  │ Result Track │  │ Callback Tracking│  │
│  │ Cases    │  │ PASS/FAIL/   │  │ volatile bools   │  │
│  │ TC01..39 │  │ SKIP counts  │  │ fired? status?   │  │
│  └──────────┘  └──────────────┘  │ progress? count? │  │
│       │                          └──────────────────┘  │
│       │ Public API calls                                │
│       ▼                                                 │
│  ┌──────────────────────┐                               │
│  │ librdkFwupdateMgr.so │ ← Library under test         │
│  └──────────┬───────────┘                               │
│             │ D-Bus                                     │
│  ┌──────────▼───────────┐                               │
│  │  rdkFwupdateMgr      │ ← Daemon                     │
│  │  (daemon process)    │                               │
│  └──────────────────────┘                               │
└─────────────────────────────────────────────────────────┘
```

---

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| All happy path tests return FAIL | Daemon not running | `systemctl start rdkFwupdateMgr` |
| Test hangs on "Waiting for callback" | Daemon not sending expected signal | Check `/opt/logs/rdkFwupdateMgr.log` |
| TC09 duplicate test passes when it shouldn't | First check completed before second call (fast daemon) | Normal — guard worked, operation was just fast |
| TC34–TC36 unregister guard tests fail | `unregisterProcess()` missing in-progress checks | Fix `rdkFwupdateMgr_process.c` |
| TC20/TC31 progress not monotonic | Daemon sending non-monotonic values | Daemon bug — library relays accurately |
| TC38 no-sleep lifecycle fails | Worker thread cleanup race | Check `internal_end_*()` call timing in worker cleanup |

---

## Comparison with example_plugin

| Aspect | example_plugin | KnowWhereItBreaks |
|--------|---------------|-------------------|
| **Audience** | External teams (reference app) | Internal developer (testing) |
| **Lines** | ~700 | ~1100 |
| **Test cases** | 0 (it's an example) | 39 |
| **Error injection** | None | NULL/empty/invalid for every API |
| **Automation** | None (single run) | 4 automated modes + interactive menu |
| **Result tracking** | None | PASS/FAIL/SKIP with final summary |
| **Guard testing** | None | Duplicate calls, unregister during ops |
| **Retry testing** | None | Rapid retry after completion |
| **Exit code** | 0 or 1 | 0 = all pass, 1 = failures |
