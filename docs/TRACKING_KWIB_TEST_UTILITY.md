# Tracking: KnowWhereItBreaks (kwib_test_utility) — Developer Test Utility

> **Created:** 2026-03-27  
> **Last updated:** 2026-03-31  
> **Design doc:** [`KnowWhereItBreaks.md`](./KnowWhereItBreaks.md)  
> **Usage doc:** [`kwib_src/USAGE_KWIB.md`](../kwib_src/USAGE_KWIB.md)  
> **Source:** [`kwib_src/KnowWhereItBreaks.c`](../kwib_src/KnowWhereItBreaks.c)  
> **Binary:** `kwib_test_utility` (installed to `/usr/bin/`)  
> **Prerequisites:** Phase 1 (CheckForUpdate) ✅, Phase 2 (Download) ✅, Phase 3 (Update) ✅

---

## Objective

Build a comprehensive developer test utility that exercises **every code path**
in `librdkFwupdateMgr.so` and the `rdkFwupdateMgr` daemon. The utility must:

- Cover all 5 public API functions (register, unregister, check, download, update)
- Test every input validation guard (NULL, empty, missing fields)
- Test every in-progress guard (duplicate call rejection)
- Test every session guard (unregister blocked during active ops)
- Test rapid retry (call again immediately after previous completes)
- Test cross-API independence (check + download simultaneously)
- Test full lifecycle end-to-end (register → check → download → update → unregister)
- Compile and install exactly like `example_plugin` via `Makefile.am`
- Support both interactive menu and automated CI modes
- Report PASS/FAIL/SKIP with CI-friendly exit codes

---

## Implementation Checklist

### Step 1 — Design & Planning
| Item | Status |
|------|--------|
| Define test categories and test case IDs (TC01–TC39) | ✅ Done |
| Map each TC to the specific code path it exercises | ✅ Done |
| Define callback tracking strategy (volatile globals) | ✅ Done |
| Define wait-with-timeout strategy (`wait_flag()` polling) | ✅ Done |
| Define automated mode CLI flags | ✅ Done |
| **Estimated:** 1h · **Actual:** 1h | |

### Step 2 — Test Infrastructure (in KnowWhereItBreaks.c)
| Item | Status |
|------|--------|
| `TestResults` struct (total, passed, failed, skipped) | ✅ Done |
| `TEST_PASS(name)` macro — green output, increments passed | ✅ Done |
| `TEST_FAIL(name, reason)` macro — red output, increments failed | ✅ Done |
| `TEST_SKIP(name, reason)` macro — yellow output, increments skipped | ✅ Done |
| `TEST_INFO(fmt, ...)` macro — informational output | ✅ Done |
| `wait_flag(volatile bool*, timeout_sec)` — poll with 100ms interval | ✅ Done |
| `reset_all()` — clears all callback tracking state | ✅ Done |
| `ensure_registered()` — auto-register if no handle | ✅ Done |
| `ensure_unregistered()` — auto-unregister if handle exists | ✅ Done |
| `print_results()` — final summary with colors and emoji | ✅ Done |
| **Estimated:** 1h · **Actual:** 1h | |

### Step 3 — Callback Tracking
| Item | Status |
|------|--------|
| CheckForUpdate: `g_check_cb_fired`, `g_check_cb_count`, `g_check_status`, `g_check_current_ver` | ✅ Done |
| DownloadFirmware: `g_dwnl_cb_terminal`, `g_dwnl_cb_count`, `g_dwnl_status`, `g_dwnl_last_progress`, `g_dwnl_progress_mono` | ✅ Done |
| UpdateFirmware: `g_update_cb_terminal`, `g_update_cb_count`, `g_update_status`, `g_update_last_progress`, `g_update_progress_mono` | ✅ Done |
| All tracking variables are `volatile` (callbacks fire from worker threads) | ✅ Done |
| Progress monotonicity tracking (detects non-increasing progress) | ✅ Done |
| `check_callback()` — logs, stores status, sets `cb_fired` | ✅ Done |
| `download_callback()` — logs, tracks progress, sets `cb_terminal` on COMPLETED/ERROR | ✅ Done |
| `update_callback()` — logs, tracks progress, sets `cb_terminal` on COMPLETED/ERROR | ✅ Done |
| **Estimated:** 1h · **Actual:** 0.5h | |

### Step 4 — Register/Unregister Tests (TC01–TC04)
| Item | Status |
|------|--------|
| TC01: `registerProcess()` happy path — non-NULL, non-empty handle | ✅ Done |
| TC02: `unregisterProcess()` happy path — no crash | ✅ Done |
| TC03: `unregisterProcess(NULL)` — no crash (NULL guard) | ✅ Done |
| TC04: Double register — two handles, both valid, different | ✅ Done |
| **Estimated:** 0.5h · **Actual:** 0.5h | |

### Step 5 — CheckForUpdate Tests (TC05–TC11)
| Item | Status |
|------|--------|
| TC05: Happy path — SUCCESS return, callback fires within 130s | ✅ Done |
| TC06: NULL handle → FAIL | ✅ Done |
| TC07: NULL callback → FAIL | ✅ Done |
| TC08: Empty handle → FAIL | ✅ Done |
| TC09: Duplicate (same process) — second call rejected by guard | ✅ Done |
| TC10: Rapid retry — call again after previous completes, succeeds | ✅ Done |
| TC11: Callback data validation — fires once, status in [0..5] | ✅ Done |
| **Estimated:** 1.5h · **Actual:** 1.5h | |

### Step 6 — DownloadFirmware Tests (TC12–TC22)
| Item | Status |
|------|--------|
| TC12: Happy path — SUCCESS return, terminal callback fires | ✅ Done |
| TC13: NULL handle → FAILED | ✅ Done |
| TC14: NULL request → FAILED | ✅ Done |
| TC15: NULL callback → FAILED | ✅ Done |
| TC16: NULL firmwareName → FAILED | ✅ Done |
| TC17: Empty firmwareName → FAILED | ✅ Done |
| TC18: Duplicate (same process) — second call rejected | ✅ Done |
| TC19: Rapid retry — second call after completion succeeds | ✅ Done |
| TC20: Progress monotonicity — multiple callbacks, never decreases | ✅ Done |
| TC21: Terminal status — final callback is COMPLETED or ERROR | ✅ Done |
| TC22: Empty handle → FAILED | ✅ Done |
| **Estimated:** 2h · **Actual:** 2h | |

### Step 7 — UpdateFirmware Tests (TC23–TC33)
| Item | Status |
|------|--------|
| TC23: Happy path — SUCCESS return, terminal callback fires | ✅ Done |
| TC24: NULL handle → FAILED | ✅ Done |
| TC25: NULL request → FAILED | ✅ Done |
| TC26: NULL callback → FAILED | ✅ Done |
| TC27: NULL firmwareName → FAILED | ✅ Done |
| TC28: Empty firmwareName → FAILED | ✅ Done |
| TC29: Duplicate (same process) — second call rejected | ✅ Done |
| TC30: Rapid retry — second call after completion succeeds | ✅ Done |
| TC31: Progress monotonicity — multiple callbacks, never decreases | ✅ Done |
| TC32: Terminal status — final callback is COMPLETED or ERROR | ✅ Done |
| TC33: Empty handle → FAILED | ✅ Done |
| **Estimated:** 2h · **Actual:** 1.5h | |

### Step 8 — Unregister Guard Tests (TC34–TC36)
| Item | Status |
|------|--------|
| TC34: Unregister during active checkForUpdate — blocked, callback still fires | ✅ Done |
| TC35: Unregister during active download — blocked, terminal callback still fires | ✅ Done |
| TC36: Unregister during active update — blocked, terminal callback still fires | ✅ Done |
| **Estimated:** 1h · **Actual:** 1h | |

### Step 9 — Full Lifecycle & Cross-API Tests (TC37–TC39)
| Item | Status |
|------|--------|
| TC37: Full lifecycle — register → check → download → update → unregister, all succeed | ✅ Done |
| TC38: Lifecycle no sleeps — same as TC37, no sleep() between calls, stress-tests cleanup | ✅ Done |
| TC39: Simultaneous check + download — both succeed (independent guards) | ✅ Done |
| **Estimated:** 1.5h · **Actual:** 1.5h | |

### Step 10 — Interactive Menu
| Item | Status |
|------|--------|
| Menu layout with all 39 TCs grouped by category | ✅ Done |
| Handle status display in menu header | ✅ Done |
| Automated suite shortcuts (10=error, 11=happy, 12=lifecycle, 13=all) | ✅ Done |
| Exit with results summary (choice 0) | ✅ Done |
| **Estimated:** 0.5h · **Actual:** 0.5h | |

### Step 11 — Automated Modes (CLI)
| Item | Status |
|------|--------|
| `--auto-error` — error/validation + guard tests | ✅ Done |
| `--auto-happy` — happy path + retry tests | ✅ Done |
| `--full-lifecycle` — TC37, TC38, TC39 | ✅ Done |
| `--auto-all` — all three suites sequentially | ✅ Done |
| Exit code: 0 = all pass, 1 = failures | ✅ Done |
| Unknown flag → usage message | ✅ Done |
| **Estimated:** 0.5h · **Actual:** 0.5h | |

### Step 12 — Build System Integration (Makefile.am)
| Item | Status |
|------|--------|
| Added `bin_PROGRAMS += kwib_test_utility` | ✅ Done |
| Source: `${top_srcdir}/kwib_src/KnowWhereItBreaks.c` | ✅ Done |
| CFLAGS: `-I librdkFwupdateMgr/include`, AM_CFLAGS, GLIB_CFLAGS | ✅ Done |
| LDADD: `librdkFwupdateMgr.la`, GLIB_LIBS, -lpthread | ✅ Done |
| LDFLAGS: `-L$(PKG_CONFIG_SYSROOT_DIR)/$(libdir)` | ✅ Done |
| Binary name collision fix (binary = `kwib_test_utility`, source dir = `kwib_src/`) | ✅ Done |
| Pattern matches `example_plugin` build rules exactly | ✅ Done |
| Installs to `/usr/bin/` alongside `example_plugin` | ✅ Done |
| **Estimated:** 0.5h · **Actual:** 0.5h | |

### Step 13 — Documentation
| Item | Status |
|------|--------|
| `kwib_src/KnowWhereItBreaks_README.md` — technical reference, test catalog, architecture | ✅ Done |
| `kwib_src/USAGE_KWIB.md` — usage guide, quick start, troubleshooting, CI examples | ✅ Done |
| `docs/KnowWhereItBreaks.md` — design doc, deep dive on every TC, internal architecture | ✅ Done |
| `docs/TRACKING_KWIB_TEST_UTILITY.md` — **this file** | ✅ Done |
| **Estimated:** 2h · **Actual:** 2h | |

---

## Test Execution Status

### Error/Validation Tests (no daemon needed for most)

| TC | Name | Code Done | Compiles | Runs on Device | Result |
|----|------|:---------:|:--------:|:--------------:|:------:|
| TC03 | Unregister NULL | ✅ | ✅ | ⬜ Pending | — |
| TC06 | Check: NULL handle | ✅ | ✅ | ⬜ Pending | — |
| TC07 | Check: NULL callback | ✅ | ✅ | ⬜ Pending | — |
| TC08 | Check: Empty handle | ✅ | ✅ | ⬜ Pending | — |
| TC13 | Download: NULL handle | ✅ | ✅ | ⬜ Pending | — |
| TC14 | Download: NULL request | ✅ | ✅ | ⬜ Pending | — |
| TC15 | Download: NULL callback | ✅ | ✅ | ⬜ Pending | — |
| TC16 | Download: NULL fw name | ✅ | ✅ | ⬜ Pending | — |
| TC17 | Download: Empty fw name | ✅ | ✅ | ⬜ Pending | — |
| TC22 | Download: Empty handle | ✅ | ✅ | ⬜ Pending | — |
| TC24 | Update: NULL handle | ✅ | ✅ | ⬜ Pending | — |
| TC25 | Update: NULL request | ✅ | ✅ | ⬜ Pending | — |
| TC26 | Update: NULL callback | ✅ | ✅ | ⬜ Pending | — |
| TC27 | Update: NULL fw name | ✅ | ✅ | ⬜ Pending | — |
| TC28 | Update: Empty fw name | ✅ | ✅ | ⬜ Pending | — |
| TC33 | Update: Empty handle | ✅ | ✅ | ⬜ Pending | — |

### Guard Tests (daemon needed)

| TC | Name | Code Done | Compiles | Runs on Device | Result |
|----|------|:---------:|:--------:|:--------------:|:------:|
| TC09 | Check: Duplicate rejected | ✅ | ✅ | ⬜ Pending | — |
| TC18 | Download: Duplicate rejected | ✅ | ✅ | ⬜ Pending | — |
| TC29 | Update: Duplicate rejected | ✅ | ✅ | ⬜ Pending | — |
| TC34 | Unreg during check → blocked | ✅ | ✅ | ⬜ Pending | — |
| TC35 | Unreg during download → blocked | ✅ | ✅ | ⬜ Pending | — |
| TC36 | Unreg during update → blocked | ✅ | ✅ | ⬜ Pending | — |

### Happy Path Tests (daemon needed)

| TC | Name | Code Done | Compiles | Runs on Device | Result |
|----|------|:---------:|:--------:|:--------------:|:------:|
| TC01 | Register happy | ✅ | ✅ | ⬜ Pending | — |
| TC02 | Unregister happy | ✅ | ✅ | ⬜ Pending | — |
| TC04 | Double register | ✅ | ✅ | ⬜ Pending | — |
| TC05 | Check happy | ✅ | ✅ | ⬜ Pending | — |
| TC10 | Check rapid retry | ✅ | ✅ | ⬜ Pending | — |
| TC11 | Check callback data | ✅ | ✅ | ⬜ Pending | — |
| TC12 | Download happy | ✅ | ✅ | ⬜ Pending | — |
| TC19 | Download rapid retry | ✅ | ✅ | ⬜ Pending | — |
| TC20 | Download progress mono | ✅ | ✅ | ⬜ Pending | — |
| TC21 | Download terminal status | ✅ | ✅ | ⬜ Pending | — |
| TC23 | Update happy | ✅ | ✅ | ⬜ Pending | — |
| TC30 | Update rapid retry | ✅ | ✅ | ⬜ Pending | — |
| TC31 | Update progress mono | ✅ | ✅ | ⬜ Pending | — |
| TC32 | Update terminal status | ✅ | ✅ | ⬜ Pending | — |

### Lifecycle & Cross-API Tests (daemon needed)

| TC | Name | Code Done | Compiles | Runs on Device | Result |
|----|------|:---------:|:--------:|:--------------:|:------:|
| TC37 | Full lifecycle | ✅ | ✅ | ⬜ Pending | — |
| TC38 | Lifecycle no sleeps | ✅ | ✅ | ⬜ Pending | — |
| TC39 | Check+Download simultaneous | ✅ | ✅ | ⬜ Pending | — |

---

## Summary

| Step | Description | Effort | Status |
|------|-------------|--------|--------|
| 1 | Design & planning | 1h | ✅ Done |
| 2 | Test infrastructure | 1h | ✅ Done |
| 3 | Callback tracking | 0.5h | ✅ Done |
| 4 | Register/Unregister tests (TC01–TC04) | 0.5h | ✅ Done |
| 5 | CheckForUpdate tests (TC05–TC11) | 1.5h | ✅ Done |
| 6 | DownloadFirmware tests (TC12–TC22) | 2h | ✅ Done |
| 7 | UpdateFirmware tests (TC23–TC33) | 1.5h | ✅ Done |
| 8 | Unregister guard tests (TC34–TC36) | 1h | ✅ Done |
| 9 | Full lifecycle tests (TC37–TC39) | 1.5h | ✅ Done |
| 10 | Interactive menu | 0.5h | ✅ Done |
| 11 | Automated modes (CLI) | 0.5h | ✅ Done |
| 12 | Build system integration | 0.5h | ✅ Done |
| 13 | Documentation | 2h | ✅ Done |
| 14 | Device testing — error/validation | 1h | ⬜ Pending |
| 15 | Device testing — happy path | 2h | ⬜ Pending |
| 16 | Device testing — lifecycle | 1h | ⬜ Pending |
| 17 | Manual-only scenarios | 2h | ⬜ Pending |
| **Total** | | **~19h** | **13/17 done** |

---

## Device Testing Procedure

### Pre-test checklist

```bash
# 1. Verify build succeeded
ls -l /usr/bin/kwib_test_utility

# 2. Verify library installed
ls -l /usr/lib/librdkFwupdateMgr.so*

# 3. Start daemon
systemctl start rdkFwupdateMgr
systemctl status rdkFwupdateMgr

# 4. Verify D-Bus
dbus-monitor --system "interface='org.rdkfwupdater.Interface'" &
```

### Test execution order

```bash
# Phase A: Error tests (fast, ~2 min)
kwib_test_utility --auto-error

# Phase B: Happy path (slower, ~10 min with daemon waits)
kwib_test_utility --auto-happy

# Phase C: Lifecycle (slowest, ~15 min)
kwib_test_utility --full-lifecycle

# Phase D: All at once (for CI)
kwib_test_utility --auto-all
echo "Exit code: $?"
```

### Post-test

```bash
# Check for memory leaks
valgrind --leak-check=full kwib_test_utility --auto-all 2>&1 | tee /tmp/kwib_valgrind.log

# Check for data races
# (requires rebuild with -fsanitize=thread)
kwib_test_utility --auto-all 2>&1 | tee /tmp/kwib_tsan.log
```

---

## Manual-Only Test Scenarios

| # | Scenario | Steps | Expected | Status |
|---|----------|-------|----------|--------|
| M1 | Daemon down | Stop daemon → `kwib_test_utility --auto-happy` | All happy paths FAIL, no crash, no hang, exit code 1 | ⬜ Pending |
| M2 | Daemon crash mid-check | Menu → TC05 → `kill -9 $(pidof rdkFwupdateMgr)` | Check callback timeout (130s), no crash | ⬜ Pending |
| M3 | Daemon crash mid-download | Menu → TC12 → kill daemon | Download callback fires `DWNL_ERROR` or timeout | ⬜ Pending |
| M4 | Daemon crash mid-update | Menu → TC23 → kill daemon | Update callback fires `UPDATE_ERROR` or timeout | ⬜ Pending |
| M5 | Cross-process rejection | Two instances → both TC12 | One succeeds, other gets `RDKFW_DWNL_FAILED` | ⬜ Pending |
| M6 | Memory leak check | `valgrind --leak-check=full kwib_test_utility --auto-all` | 0 bytes definitely lost | ⬜ Pending |
| M7 | Thread sanitizer | Rebuild with `-fsanitize=thread` → `--auto-all` | No data race warnings | ⬜ Pending |
| M8 | Network failure mid-download | Disconnect network during TC12 | `DWNL_ERROR` callback fires | ⬜ Pending |

---

## Risk Register

| Risk | Likelihood | Impact | Mitigation | Status |
|------|-----------|--------|------------|--------|
| Binary name collision with source directory | **Happened** | Build fails | Renamed binary to `kwib_test_utility`, source to `kwib_src/` | ✅ Fixed |
| Volatile globals insufficient for thread sync | Low | Medium | Only used for polling (wait_flag), not for mutual exclusion | Accepted |
| Test hangs if daemon doesn't respond | Low | Medium | All waits have timeouts (130s check, 600s download/update) | ✅ Implemented |
| Test state leak between TCs | Low | Low | `reset_all()` clears all tracking state before each TC | ✅ Implemented |
| False PASS on TC09/18/29 (duplicate guard) | Low | Low | If operation completes before second call, guard was never tested | Documented in troubleshooting |
| TC38 exposes real cleanup race | Medium | High | This is intentional — the test exists to find it | By design |

---

## File Inventory

| File | Lines | Purpose |
|------|:-----:|---------|
| `kwib_src/KnowWhereItBreaks.c` | ~1329 | Source: 39 test cases, callbacks, menu, automation |
| `kwib_src/KnowWhereItBreaks_README.md` | ~273 | Technical reference: test catalog, architecture, comparison |
| `kwib_src/USAGE_KWIB.md` | ~450 | Usage guide: build, run, interpret, troubleshoot, CI |
| `docs/KnowWhereItBreaks.md` | ~862 | Design doc: deep dive, internal logic, edge cases |
| `docs/TRACKING_KWIB_TEST_UTILITY.md` | this file | **Progress tracking** |
| `Makefile.am` (lines 275–293) | 18 | Build rule: `kwib_test_utility` target |

---

## Dependencies

| Dependency | Status | Notes |
|-----------|--------|-------|
| Phase 1 (CheckForUpdate on-demand thread) | ✅ Complete | TC05–TC11 exercise this |
| Phase 2 (DownloadFirmware on-demand thread) | ✅ Complete | TC12–TC22 exercise this |
| Phase 3 (UpdateFirmware on-demand thread) | ✅ Complete | TC23–TC33 exercise this |
| `librdkFwupdateMgr.so` (built library) | ✅ Built | Linked at compile time |
| `rdkFwupdateMgr` daemon | ✅ Available | Required for happy path tests |
| GLib/GIO system libraries | ✅ Available | Standard on target |
| Target device cross-compilation toolchain | ✅ Available | Build succeeds |

---

## Related Documents

| Document | Description |
|----------|-------------|
| [`KnowWhereItBreaks.md`](./KnowWhereItBreaks.md) | Full design: test logic, edge cases, internal deep dive |
| [`TRACKING_CHECKFORUPDATE_REDESIGN.md`](./TRACKING_CHECKFORUPDATE_REDESIGN.md) | Phase 1 tracking — library code tested by TC05–TC11 |
| [`TRACKING_DOWNLOADFIRMWARE_REDESIGN.md`](./TRACKING_DOWNLOADFIRMWARE_REDESIGN.md) | Phase 2 tracking — library code tested by TC12–TC22 |
| [`DESIGN_CHECKFORUPDATE_ON_DEMAND_THREAD.md`](./DESIGN_CHECKFORUPDATE_ON_DEMAND_THREAD.md) | Phase 1 design |
| [`DESIGN_DOWNLOAD_FIRMWARE_ON_DEMAND_THREAD.md`](./DESIGN_DOWNLOAD_FIRMWARE_ON_DEMAND_THREAD.md) | Phase 2 design |
| [`DESIGN_UPDATEFIRMWARE_ON_DEMAND_THREAD.md`](./DESIGN_UPDATEFIRMWARE_ON_DEMAND_THREAD.md) | Phase 3 design |
| [`CHECKFORUPDATE_PROGRESS.md`](./CHECKFORUPDATE_PROGRESS.md) | Phase 1 progress |
| [`DOWNLOADFIRMWARE_PROGRESS.md`](./DOWNLOADFIRMWARE_PROGRESS.md) | Phase 2 progress |
