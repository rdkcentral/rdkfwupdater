# How to Use `kwib_test_utility` (KnowWhereItBreaks)

`kwib_test_utility` is the compiled binary of `KnowWhereItBreaks.c`. It is a
**comprehensive developer test utility** that exercises every code path in
`librdkFwupdateMgr.so` and the `rdkFwupdateMgr` daemon — 39 test cases
covering input validation, library guards, callback correctness, rapid retry,
cross-API independence, and full end-to-end lifecycle.

> **Not the example.** `example_plugin` is the clean reference for external
> teams. `kwib_test_utility` is your developer weapon for finding bugs before
> they find you.

---

## Prerequisites

Before running `kwib_test_utility`, three things must be true on the target
device (same as `example_plugin`).

### 1. The daemon is running

```bash
systemctl status rdkFwupdateMgr
```

If it is not running:

```bash
systemctl start rdkFwupdateMgr
```

If it has never been enabled:

```bash
systemctl enable --now rdkFwupdateMgr
```

> **Note:** Error/validation tests (TC03, TC06–TC08, TC13–TC17, TC22,
> TC24–TC28, TC33) do NOT need the daemon. They test the library's input
> guards locally. All other tests require the daemon to be running.

### 2. The library is installed and visible

`librdkFwupdateMgr.so` must be findable at runtime:

```bash
# Confirm it is installed
ls -l /usr/lib/librdkFwupdateMgr.so*

# If library is in a non-standard path
export LD_LIBRARY_PATH=/path/to/librdkFwupdateMgr:$LD_LIBRARY_PATH
```

### 3. D-Bus system bus is running

```bash
systemctl status dbus
```

---

## Build & Install

The binary is compiled and installed **exactly like `example_plugin`** — same
`bin_PROGRAMS` list, same CFLAGS pattern, same rootfs destination.

In `Makefile.am`:

```makefile
bin_PROGRAMS += kwib_test_utility

kwib_test_utility_SOURCES = \
    ${top_srcdir}/KnowWhereItBreaks/KnowWhereItBreaks.c

kwib_test_utility_CFLAGS = \
    -I${top_srcdir}/librdkFwupdateMgr/include \
    $(AM_CFLAGS) \
    $(GLIB_CFLAGS)

kwib_test_utility_LDADD = \
    librdkFwupdateMgr.la \
    $(GLIB_LIBS) \
    -lpthread

kwib_test_utility_LDFLAGS = \
    -L$(PKG_CONFIG_SYSROOT_DIR)/$(libdir)
```

Standard build:

```bash
./configure && make
```

The binary is produced as `kwib_test_utility` and installed to `/usr/bin/`
alongside `example_plugin`, `rdkFwupdateMgr`, `rdkvfwupgrader`, etc.

> **Why `kwib_test_utility` instead of `KnowWhereItBreaks`?**
> The source lives in the `KnowWhereItBreaks/` directory. Automake produces
> binaries in the top-level build directory, and a file cannot have the same
> name as an existing directory. The binary is therefore named
> `kwib_test_utility`.

---

## Quick Start

```bash
# 1. Make sure daemon is running
systemctl start rdkFwupdateMgr

# 2. Run in interactive mode — pick tests from the menu
kwib_test_utility

# 3. Or run all error/validation tests (fast, mostly no daemon)
kwib_test_utility --auto-error

# 4. Or run all happy-path tests (daemon required)
kwib_test_utility --auto-happy

# 5. Or run full lifecycle tests (daemon required)
kwib_test_utility --full-lifecycle

# 6. Or run EVERYTHING
kwib_test_utility --auto-all

# 7. Check exit code (CI-friendly)
echo $?   # 0 = all passed, 1 = at least one failure
```

---

## Running Modes

### Interactive Mode (no arguments)

```bash
kwib_test_utility
```

Displays a menu showing all 39 test cases grouped by category. Type a number
and press Enter to run that test. The menu re-displays after each test. Type
`0` to exit and see the final results summary.

The menu shows:

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
│    3  TC03 Unregister NULL          7  TC07 NULL callback    │
│    4  TC04 Double register          8  TC08 Empty handle     │
│                                     9  TC09 Duplicate        │
│                                    40  TC10 Rapid retry      │
│                                    41  TC11 Callback data    │
├──────────────────────────────────────────────────────────────┤
│  DOWNLOAD FIRMWARE                 UPDATE FIRMWARE            │
│   50  TC12 Download happy          60  TC23 Update happy     │
│   51  TC13 NULL handle             61  TC24 NULL handle      │
│   52  TC14 NULL request            62  TC25 NULL request     │
│   53  TC15 NULL callback           63  TC26 NULL callback    │
│   54  TC16 NULL fw name            64  TC27 NULL fw name     │
│   55  TC17 Empty fw name           65  TC28 Empty fw name    │
│   56  TC18 Duplicate               66  TC29 Duplicate        │
│   57  TC19 Rapid retry             67  TC30 Rapid retry      │
│   58  TC20 Progress mono           68  TC31 Progress mono    │
│   59  TC21 Terminal status         69  TC32 Terminal status  │
│                                    70  TC33 Empty handle     │
├──────────────────────────────────────────────────────────────┤
│  GUARDS / LIFECYCLE                                          │
│   80  TC34 Unreg during check      90  TC37 Full lifecycle   │
│   81  TC35 Unreg during download   91  TC38 No-sleep lifecy  │
│   82  TC36 Unreg during update     92  TC39 Check+Dwnl sim  │
├──────────────────────────────────────────────────────────────┤
│    0  Exit (print results)                                   │
└──────────────────────────────────────────────────────────────┘
  Choice:
```

**Typical interactive workflow:**

1. Type `1` → Register (get a handle)
2. Type `5` → CheckForUpdate happy path
3. Type `6` → CheckForUpdate NULL handle (error path)
4. Type `9` → Duplicate check (guard test)
5. Type `0` → Exit and see results

### `--auto-error` (Fast — most don't need daemon)

```bash
kwib_test_utility --auto-error
```

**Runs:** TC03, TC06, TC08, TC13, TC22, TC24, TC33, TC07, TC14–TC17,
TC25–TC28, TC09, TC18, TC29, TC34–TC36

Tests library-level input validation and in-progress guards. The NULL/empty
tests don't create worker threads or D-Bus connections. The duplicate and
unregister-during-operation tests need the daemon.

**When to use:** After any change to input validation logic in
`rdkFwupdateMgr_api.c` or guard logic in `rdkFwupdateMgr_async.c`.

### `--auto-happy` (Daemon required)

```bash
kwib_test_utility --auto-happy
```

**Runs:** TC01, TC04, TC05, TC11, TC10, TC12, TC20, TC21, TC19, TC23,
TC31, TC32, TC30, TC02

Full happy path with callback validation and retry tests. Takes longer
because it waits for real daemon responses.

**When to use:** After any change to the async worker thread logic, D-Bus
method calls, signal handlers, or callback delivery.

### `--full-lifecycle` (Daemon required)

```bash
kwib_test_utility --full-lifecycle
```

**Runs:** TC37, TC38, TC39

End-to-end integration tests: complete register→check→download→update→unregister
sequences plus cross-API simultaneous operation.

**When to use:** Before any release or after any structural refactoring.

### `--auto-all` (Everything)

```bash
kwib_test_utility --auto-all
```

Runs all three suites sequentially: error → happy → lifecycle.

**When to use:** Pre-commit validation or CI pipeline.

---

## Output Format

Every test prints a colored result line:

```
  [PASS] TC06 — NULL handle rejected           ← Green: test passed
  [FAIL] TC05 — checkForUpdate() — Callback never fired (130s timeout)   ← Red: test failed
  [SKIP] TC12 — No handle                      ← Yellow: skipped (prerequisite missing)
  [INFO] Waiting for callback (max 130s)...     ← Informational
```

Callback activity is printed in real-time:

```
    [CB:Check]  #1 status=0 current='v1.0.0'
    [CB:Dwnl]   #3 progress=60% status=0
    [CB:Update]  #5 progress=100% status=1
```

Final summary at exit:

```
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

**Exit code:** `0` = all passed, `1` = at least one failure. Use in CI:

```bash
kwib_test_utility --auto-all || echo "TESTS FAILED"
```

---

## Test Case Catalog — Complete Reference

### Category 1: Register / Unregister (TC01–TC04)

| TC | Menu | Name | Daemon? | What It Tests |
|----|:----:|------|:-------:|---------------|
| **TC01** | 1 | Register Happy Path | ✅ | Calls `registerProcess("KnowWhereItBreaks", "1.0.0")`. Verifies it returns a non-NULL, non-empty handle string. **Proves:** daemon is running, D-Bus round-trip works, daemon allocated a handler ID. |
| **TC02** | 2 | Unregister Happy Path | ✅ | After successful registration, calls `unregisterProcess(handle)`. Verifies no crash. **Proves:** daemon accepts the unregistration and deallocates the handler cleanly. |
| **TC03** | 3 | Unregister NULL Handle | ❌ | Calls `unregisterProcess(NULL)`. Must not crash. **Proves:** library NULL guard in the unregister path (`rdkFwupdateMgr_process.c`). |
| **TC04** | 4 | Double Register | ✅ | Calls `registerProcess()` twice with different process names. Both must succeed with different handles. **Proves:** daemon supports multiple simultaneous clients. Both handles are cleaned up after test. |

### Category 2: CheckForUpdate (TC05–TC11)

| TC | Menu | Name | Daemon? | What It Tests |
|----|:----:|------|:-------:|---------------|
| **TC05** | 5 | CheckForUpdate Happy Path | ✅ | Calls `checkForUpdate(handle, callback)` with valid inputs. Verifies `CHECK_FOR_UPDATE_SUCCESS` return. Waits up to 130s for callback. **Proves:** full path works — library creates worker thread → D-Bus method call → daemon queries XConf → daemon emits `CheckForUpdateComplete` signal → worker receives signal → worker parses GVariant `(tiissss)` → callback fires with `FwInfoData`. |
| **TC06** | 6 | NULL Handle | ❌ | `checkForUpdate(NULL, callback)` → must return `CHECK_FOR_UPDATE_FAIL` immediately. **Proves:** library input validation in `checkForUpdate()` at the very first line — no D-Bus call made, no thread created. |
| **TC07** | 7 | NULL Callback | ❌ | `checkForUpdate(handle, NULL)` → must return `CHECK_FOR_UPDATE_FAIL`. **Proves:** library rejects NULL callback before spawning worker thread (no way to deliver results). |
| **TC08** | 8 | Empty Handle | ❌ | `checkForUpdate("", callback)` → must return `CHECK_FOR_UPDATE_FAIL`. **Proves:** empty string is caught early by library (daemon would reject it too, but library is faster). |
| **TC09** | 9 | Duplicate (Same Process) | ✅ | Calls `checkForUpdate()` twice in rapid succession. First returns SUCCESS. Second (while first worker is active) returns FAIL. **Proves:** `internal_begin_check()` guard works — sets `g_check_in_progress = true` atomically, second call sees it and rejects. Waits for first callback to clean state. |
| **TC10** | 40 | Rapid Retry | ✅ | Calls `checkForUpdate()`, waits for callback, then immediately calls again. Second call must succeed. **Proves:** in-progress flag is properly reset by `internal_end_check()` in worker cleanup — no permanent lockout after operation completes. |
| **TC11** | 41 | Callback Data Validation | ✅ | Calls `checkForUpdate()` and examines callback data: (a) callback fired exactly once, (b) `status` field is valid `CheckForUpdateStatus` enum (0–5). **Proves:** signal parsing, GVariant deserialization, and `internal_map_status_code()` mapping are correct. |

### Category 3: DownloadFirmware (TC12–TC22)

| TC | Menu | Name | Daemon? | What It Tests |
|----|:----:|------|:-------:|---------------|
| **TC12** | 50 | Download Happy Path | ✅ | Calls `downloadFirmware(handle, &req, callback)` with valid inputs. Verifies `RDKFW_DWNL_SUCCESS` (daemon accepted via sync D-Bus reply). Waits for terminal callback. **Proves:** library creates worker → D-Bus `DownloadFirmware` method call → `(sss)` reply → condvar handshake → event loop → daemon emits `DownloadProgress` signals `(tsuss)` → callback fires N times → loop quits on terminal status → cleanup. |
| **TC13** | 51 | NULL Handle | ❌ | `downloadFirmware(NULL, &req, cb)` → `RDKFW_DWNL_FAILED`. **Proves:** library input validation. |
| **TC14** | 52 | NULL Request | ❌ | `downloadFirmware(handle, NULL, cb)` → `RDKFW_DWNL_FAILED`. **Proves:** library NULL-checks the request struct pointer. |
| **TC15** | 53 | NULL Callback | ❌ | `downloadFirmware(handle, &req, NULL)` → `RDKFW_DWNL_FAILED`. **Proves:** library rejects NULL callback. |
| **TC16** | 54 | NULL Firmware Name | ❌ | `req.firmwareName = NULL` → `RDKFW_DWNL_FAILED`. **Proves:** library validates individual struct fields, not just the struct pointer. |
| **TC17** | 55 | Empty Firmware Name | ❌ | `req.firmwareName = ""` → `RDKFW_DWNL_FAILED`. **Proves:** library checks `firmwareName[0] != '\0'`, not just `!= NULL`. |
| **TC18** | 56 | Duplicate (Same Process) | ✅ | Two `downloadFirmware()` calls in rapid succession. Second returns FAILED. **Proves:** `internal_begin_download()` guard — `g_dwnl_in_progress` flag works independently from CheckForUpdate's guard. |
| **TC19** | 57 | Rapid Retry | ✅ | Download → wait for completion → immediately download again. Second succeeds. **Proves:** `internal_end_download()` properly clears guard in worker cleanup — no permanent lockout. |
| **TC20** | 58 | Progress Monotonicity | ✅ | After TC12, examines recorded progress values. Checks: (a) multiple callbacks fired, (b) progress never decreased. **Proves:** daemon sends monotonically increasing progress. A regression here means daemon bug (library just relays). |
| **TC21** | 59 | Terminal Status | ✅ | After TC12, verifies final callback had terminal status (`DWNL_COMPLETED` or `DWNL_ERROR`), not `DWNL_IN_PROGRESS`. **Proves:** `on_download_signal_handler()` correctly identifies terminal states and quits the GLib event loop. |
| **TC22** | — | Empty Handle | ❌ | `downloadFirmware("", &req, cb)` → `RDKFW_DWNL_FAILED`. **Proves:** empty string validation (same as TC08 pattern for download). |

### Category 4: UpdateFirmware (TC23–TC33)

| TC | Menu | Name | Daemon? | What It Tests |
|----|:----:|------|:-------:|---------------|
| **TC23** | 60 | Update Happy Path | ✅ | Calls `updateFirmware(handle, &req, callback)` with valid inputs. Verifies `RDKFW_UPDATE_SUCCESS`. Waits for terminal callback. **Proves:** worker thread → sync D-Bus `UpdateFirmware` call → `(sss)` reply → condvar handshake → event loop → `UpdateProgress` signals `(tsiis)` → callback N times → terminal quit → cleanup. |
| **TC24** | 61 | NULL Handle | ❌ | → `RDKFW_UPDATE_FAILED`. **Proves:** input validation. |
| **TC25** | 62 | NULL Request | ❌ | → `RDKFW_UPDATE_FAILED`. **Proves:** NULL request struct check. |
| **TC26** | 63 | NULL Callback | ❌ | → `RDKFW_UPDATE_FAILED`. **Proves:** NULL callback check. |
| **TC27** | 64 | NULL Firmware Name | ❌ | `req.firmwareName = NULL` → `RDKFW_UPDATE_FAILED`. **Proves:** field-level validation. |
| **TC28** | 65 | Empty Firmware Name | ❌ | `req.firmwareName = ""` → `RDKFW_UPDATE_FAILED`. **Proves:** empty-string check. |
| **TC29** | 66 | Duplicate (Same Process) | ✅ | Two `updateFirmware()` calls. Second rejected by `internal_begin_update()`. **Proves:** `g_update_in_progress` guard. |
| **TC30** | 67 | Rapid Retry | ✅ | Update → wait → update again. Second succeeds. **Proves:** `internal_end_update()` clears guard. |
| **TC31** | 68 | Progress Monotonicity | ✅ | Multiple callbacks, progress never decreases. **Proves:** daemon sends correct progress sequence. |
| **TC32** | 69 | Terminal Status | ✅ | Final callback = `UPDATE_COMPLETED` or `UPDATE_ERROR`. **Proves:** event loop quit logic for update signals. |
| **TC33** | 70 | Empty Handle | ❌ | → `RDKFW_UPDATE_FAILED`. **Proves:** empty string validation for update path. |

### Category 5: Unregister During Active Operations (TC34–TC36)

| TC | Menu | Name | Daemon? | What It Tests |
|----|:----:|------|:-------:|---------------|
| **TC34** | 80 | Unregister During Check | ✅ | Starts `checkForUpdate()`, then immediately calls `unregisterProcess()`. If session guard works, unregister is blocked (no-op) and callback still fires. **Proves:** `unregisterProcess()` calls `internal_is_check_in_progress()` and refuses to destroy the handle while a check worker thread is active. Prevents use-after-free. |
| **TC35** | 81 | Unregister During Download | ✅ | Same pattern with `downloadFirmware()`. Start download → immediately unregister → verify terminal callback still fires. **Proves:** download in-progress guard blocks premature handle destruction. |
| **TC36** | 82 | Unregister During Update | ✅ | Same pattern with `updateFirmware()`. Start update → immediately unregister → verify terminal callback still fires. **Proves:** update in-progress guard blocks premature handle destruction. |

### Category 6: Full Lifecycle & Cross-API (TC37–TC39)

| TC | Menu | Name | Daemon? | What It Tests |
|----|:----:|------|:-------:|---------------|
| **TC37** | 90 | Full Lifecycle | ✅ | **The integration test.** Register → CheckForUpdate (wait) → DownloadFirmware (wait) → UpdateFirmware (wait) → Unregister. All five steps must succeed. **Proves:** entire library + daemon interaction end-to-end — handle lifetime, worker thread create/destroy for all 3 APIs, D-Bus signal subscribe/unsubscribe, condvar handshake accuracy, callback delivery, and cleanup. If anything leaks or leaves stale state, this catches it. |
| **TC38** | 91 | Lifecycle No Sleeps | ✅ | Same as TC37 but with **NO `sleep()` between API calls**. After check callback fires → immediately call download. After download completes → immediately call update. **Proves:** worker thread cleanup timing — previous worker must have called `internal_end_*()` and fully exited before next `internal_begin_*()` succeeds. If the library has a cleanup race condition, this test will catch it. |
| **TC39** | 92 | Check + Download Simultaneous | ✅ | Starts `checkForUpdate()`, then immediately starts `downloadFirmware()` while check is still active. Both must succeed. **Proves:** the three APIs use **independent** in-progress guards (`g_check_in_progress` vs `g_dwnl_in_progress`). If library incorrectly uses a single global lock, the second call would be rejected. |

---

## Test Summary Table

| Category | TC Range | Count | What's Validated |
|----------|:--------:|:-----:|-----------------|
| Register / Unregister | TC01–TC04 | 4 | Handle creation, cleanup, NULL safety, multi-client |
| CheckForUpdate | TC05–TC11 | 7 | Happy path, input validation, guard, retry, callback data |
| DownloadFirmware | TC12–TC22 | 11 | Happy path, input validation, guard, retry, progress, terminal |
| UpdateFirmware | TC23–TC33 | 11 | Happy path, input validation, guard, retry, progress, terminal |
| Unregister Guards | TC34–TC36 | 3 | Session protection during active operations |
| Full Lifecycle | TC37–TC39 | 3 | End-to-end integration, timing, cross-API independence |
| **Total** | | **39** | |

---

## Timeout Configuration

| Operation | Timeout | Why |
|-----------|:-------:|-----|
| `checkForUpdate` callback | 130 seconds | XConf query time varies by network |
| `downloadFirmware` terminal | 600 seconds | Depends on firmware size and network speed |
| `updateFirmware` terminal | 600 seconds | Depends on flash hardware speed |
| Flag poll interval | 100ms | Balance between responsiveness and CPU usage |

These are hardcoded in `KnowWhereItBreaks.c` in the `wait_flag()` function
and the `wait_flag()` call sites. Adjust if your environment is slower.

---

## Manual-Only Test Scenarios

These require external actions that cannot be automated inside the utility:

| Scenario | How to Run | Expected Behavior |
|----------|-----------|-------------------|
| **Daemon down** | Stop daemon → `kwib_test_utility --auto-happy` | All happy paths return FAIL. No crashes, no hangs. Exit code 1. |
| **Daemon crash mid-download** | Start TC12 (menu 50) → in another terminal: `kill -9 $(pidof rdkFwupdateMgr)` | Callback eventually fires with `DWNL_ERROR` via timeout path. |
| **Daemon crash mid-update** | Start TC23 (menu 60) → kill daemon | Callback eventually fires with `UPDATE_ERROR`. |
| **Cross-process rejection** | Run two instances of `kwib_test_utility`. Both register. Both try TC12 simultaneously. | One succeeds, the other gets `RDKFW_DWNL_FAILED` (daemon-level rejection). |
| **Memory leak check** | `valgrind --leak-check=full kwib_test_utility --auto-all` | 0 bytes definitely lost. GLib "still reachable" is expected and harmless. |
| **Thread sanitizer** | Rebuild with `-fsanitize=thread`, run `--auto-all` | No data race warnings. |
| **Network failure during download** | Disconnect network after TC12 starts | `DWNL_ERROR` callback fires. |

---

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│               kwib_test_utility (process)                │
│                                                          │
│  ┌───────────┐  ┌───────────────┐  ┌─────────────────┐  │
│  │ 39 Test   │  │ Result Track  │  │ Callback Track  │  │
│  │ Functions │  │ PASS / FAIL / │  │ volatile bools  │  │
│  │ tc01()    │  │ SKIP counters │  │ cb_fired?       │  │
│  │ ...       │  │               │  │ status?         │  │
│  │ tc39()    │  │ g_results     │  │ progress?       │  │
│  └─────┬─────┘  └───────────────┘  │ count?          │  │
│        │                            └─────────────────┘  │
│        │ Public API calls                                │
│        ▼                                                 │
│  ┌──────────────────────────┐                            │
│  │  librdkFwupdateMgr.so   │  ← Library under test      │
│  │  (linked at build time)  │                            │
│  └────────────┬─────────────┘                            │
│               │ D-Bus (system bus)                       │
│  ┌────────────▼─────────────┐                            │
│  │  rdkFwupdateMgr          │  ← Daemon process          │
│  │  (separate process)      │                            │
│  └──────────────────────────┘                            │
└──────────────────────────────────────────────────────────┘
```

**Data flow for a happy-path test (e.g., TC05):**

```
kwib_test_utility                 librdkFwupdateMgr.so        rdkFwupdateMgr daemon
─────────────────                 ────────────────────        ────────────────────
tc05_check_happy()
  │
  ├─ checkForUpdate(handle, cb) ──► input validation
  │                                 internal_begin_check()
  │                                 pthread_create(worker)
  │                                   │
  │  ◄── CHECK_FOR_UPDATE_SUCCESS ───┘
  │                                   worker thread:
  │                                     g_bus_get_sync()
  │                                     g_dbus_connection_call_sync() ──► CheckForUpdate method
  │                                                                       │
  │                                     g_dbus_connection_signal_subscribe()
  │                                     g_main_loop_run()                  ◄── XConf query
  │                                       │                                    │
  │                                       │  ◄── CheckForUpdateComplete signal ┘
  │                                       │
  │                                     on_check_signal_handler()
  │                                       parse GVariant (tiissss)
  │                                       cb(&fwInfoData)  ──────────────────┐
  │                                                                          │
  │  check_callback() fires  ◄──────────────────────────────────────────────┘
  │    g_check_cb_fired = true
  │    g_check_status = status
  │
  ├─ wait_flag(&g_check_cb_fired)
  │    ... polling 100ms ...
  │    flag is true!
  │
  ├─ TEST_PASS("TC05")
  │
```

---

## Troubleshooting

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| All happy-path tests return FAIL | Daemon not running | `systemctl start rdkFwupdateMgr` |
| Binary not found after `make` | Forgot to re-run `./configure` after Makefile.am change | `autoreconf -fi && ./configure [flags] && make` |
| `rm: cannot remove 'KnowWhereItBreaks': Is a directory` | Old Makefile still has `bin_PROGRAMS += KnowWhereItBreaks` | Run `make clean && autoreconf -fi && ./configure [flags] && make` |
| Test hangs on "Waiting for callback" | Daemon not sending expected signal | Check `/opt/logs/rdkFwupdateMgr.log` or `journalctl -u rdkFwupdateMgr -f` |
| TC09/TC18/TC29 duplicate test PASS when shouldn't | First operation completed before second call (fast daemon) | Normal — guard worked, operation was just fast. Not a bug. |
| TC34–TC36 unregister guard tests FAIL | `unregisterProcess()` missing in-progress checks | Fix guard logic in `rdkFwupdateMgr_process.c` |
| TC20/TC31 progress not monotonic | Daemon sending non-monotonic values | Daemon bug — library just relays accurately |
| TC38 no-sleep lifecycle FAIL | Worker thread cleanup race | Check `internal_end_*()` call timing in worker cleanup path |
| TC39 simultaneous check+download FAIL | Library using single global guard instead of per-API guards | Fix `rdkFwupdateMgr_async.c` — each API needs its own `g_*_in_progress` |
| TC10/TC19/TC30 rapid retry FAIL | `internal_end_*()` not being called in cleanup | Check worker thread cleanup path calls `internal_end_*()` in all exit branches |
| `error while loading shared libraries` | `librdkFwupdateMgr.so` not in linker path | `export LD_LIBRARY_PATH=/usr/lib:$LD_LIBRARY_PATH` or run `ldconfig` |

---

## Comparison: kwib_test_utility vs example_plugin

| Aspect | `example_plugin` | `kwib_test_utility` |
|--------|------------------|---------------------|
| **Purpose** | Clean reference for external teams | Developer test utility for finding bugs |
| **Source** | `librdkFwupdateMgr/examples/example_app.c` | `KnowWhereItBreaks/KnowWhereItBreaks.c` |
| **Audience** | Plugin/app developers | Library/daemon developers |
| **Test cases** | 0 (it's an example workflow) | 39 |
| **Error injection** | None | NULL/empty/invalid for every API parameter |
| **Automation** | None (single one-shot run) | 4 automated modes + interactive menu |
| **Result tracking** | None | PASS/FAIL/SKIP with colored summary |
| **Guard testing** | None | Duplicate calls, unregister during active ops |
| **Retry testing** | None | Rapid retry after completion |
| **Cross-API testing** | None | Simultaneous check+download |
| **Lifecycle testing** | Single workflow | Multiple lifecycle patterns (with/without sleeps) |
| **CI integration** | N/A | Exit code 0/1, `--auto-all` for pipelines |
| **Lines** | ~700 | ~1300 |
| **Build rule** | `bin_PROGRAMS += example_plugin` | `bin_PROGRAMS += kwib_test_utility` |
| **Installed to** | `/usr/bin/example_plugin` | `/usr/bin/kwib_test_utility` |
| **Links against** | `librdkFwupdateMgr.la` | `librdkFwupdateMgr.la` |

---

## Files

| File | Purpose |
|------|---------|
| `KnowWhereItBreaks/KnowWhereItBreaks.c` | Source code — all 39 test cases, callbacks, menu, automation |
| `KnowWhereItBreaks/KnowWhereItBreaks_README.md` | Technical reference — test case catalog, architecture, internals |
| `KnowWhereItBreaks/USAGE_KWIB.md` | **This file** — how to build, run, and interpret results |
| `Makefile.am` | Build rule: `kwib_test_utility` target (lines 275–293) |

---

## CI / Scripting Examples

### Run in CI pipeline (fail build on test failure)

```bash
#!/bin/bash
systemctl start rdkFwupdateMgr
sleep 2

kwib_test_utility --auto-all
exit_code=$?

if [ $exit_code -ne 0 ]; then
    echo "ERROR: kwib_test_utility reported test failures"
    exit 1
fi

echo "All kwib tests passed"
```

### Run only error tests (no daemon needed for most)

```bash
kwib_test_utility --auto-error
```

### Run with valgrind

```bash
valgrind --leak-check=full --show-leak-kinds=all \
    kwib_test_utility --auto-all 2>&1 | tee valgrind_kwib.log
```

### Run with thread sanitizer

```bash
# Rebuild with sanitizer
export CFLAGS="-fsanitize=thread -g"
make clean && make

kwib_test_utility --auto-all 2>&1 | tee tsan_kwib.log
```

---

**Version**: 1.0
**Binary**: `kwib_test_utility`
**Source**: `KnowWhereItBreaks/KnowWhereItBreaks.c`
**Last Updated**: March 2026
**Status**: Complete — 39 test cases covering all public API paths
