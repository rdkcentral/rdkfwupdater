
---
marp: true
---
# L1 Test Automation — PHASE 1 Execution Plan

> **Status:** READY_FOR_APPROVAL  
> **Date:** 2026-08-17  
> **Ticket:** RDKEMW-14784

---

## 1) Input Summary

| Parameter | Value |
|---|---|
| **SOURCE_FOLDER** | `src` |
| **UNIT_TEST_FOLDER** | `unittest` |
| **BUILD_COMMAND** | `cd unittest && automake --add-missing && autoreconf --install && ./configure && make -j8` |
| **TEST_COMMAND** | `cd unittest && ./rdkfw_device_status_gtest && ./rdkfw_deviceutils_gtest && ./rdkfw_main_gtest && ./rdkfw_interface_gtest && ./rdkFwupdateMgr_handlers_gtest && ./rdkfwupdatemgr_main_flow_gtest` |
| **COVERAGE_COMMAND** | `cd src && lcov --capture --directory . --output-file coverage.info && lcov --remove coverage.info '/usr/*' --output-file coverage.filtered.info && lcov --summary coverage.filtered.info` |
| **LINE_COVERAGE_THRESHOLD** | 90% |
| **FUNCTION_COVERAGE_THRESHOLD** | 95% |

---

## 2) Baseline Understanding

| Item | Value |
|---|---|
| **Reference line coverage**: 62.6%
| **Reference function coverage**: 81.8%

Baseline note:
62.6% line / 81.8% function is the reference baseline obtained during
the repository dry run. It must not be assumed to be the execution-time
baseline. The execution agent must establish a fresh baseline from the
current repository state before generating or modifying tests.
| **Existing test infrastructure** | GoogleTest / GoogleMock; multiple fixtures, mock classes (`DeviceStatusMock`, `DeviceUtilsMock`, `FwDlInterfaceMock`, `RdkFwupdateMgrMock`), a miscellaneous stub file (`miscellaneous_mock.cpp`), and per-module mock `.cpp` files under `unittest/mocks/` |
| **Build system** | Autotools (`unittest/configure.ac` + `unittest/Makefile.am`); already bootstrapped and configured (generated `Makefile`, `.deps/`, `.libs/`, binaries present) |
| **Coverage tooling** | `gcov` / `lcov`; `.gcda`/`.gcno` files present in `src/` and `unittest/` from a prior build/test run |

### Relevant existing test binaries (run by TEST_COMMAND)

| Binary | Primary source(s) exercised |
|---|---|
| `rdkfw_device_status_gtest` | `src/device_status_helper.c`, `src/download_status_helper.c` |
| `rdkfw_deviceutils_gtest` | `src/deviceutils/device_api.c`, `src/deviceutils/deviceutils.c` |
| `rdkfw_main_gtest` | `src/rdkv_main.c`, `src/directcdn.c`, `src/rdkv_upgrade.c`, `src/chunk.c`, `src/flash.c`, `src/json_process.c` |
| `rdkfw_interface_gtest` | `src/rfcInterface/rfcinterface.c`, `src/iarmInterface/iarmInterface.c`, `src/rbusInterface/rbusInterface.c` |
| `rdkFwupdateMgr_handlers_gtest` | `src/dbus/rdkFwupdateMgr_handlers.c`, `src/json_process.c` |
| `rdkfwupdatemgr_main_flow_gtest` | `src/rdkFwupdateMgr.c`, `src/json_process.c` |

> **Note:** `src/dbus/xconf_comm_status.c` is NOT listed in any of the six TEST_COMMAND binary sources in `unittest/Makefile.am`, and its symbols are not present in any mock file. It is therefore zero-coverage.

---

2a. Execute one coverage area at a time.
    - Select ONE highest-priority uncovered function/branch cluster from the latest lcov results.
    - Add or update only the tests needed for that selected area.
    - Do not implement all candidate areas in a single iteration.
    - Build and run the affected test binary first.
    - Run the full TEST_COMMAND when appropriate to validate the combined test suite.
    - Run COVERAGE_COMMAND and evaluate the coverage change.
    - Only after reviewing the updated coverage should the next area be selected.
    - If the selected area provides little or no meaningful coverage improvement,
      do not continue adding speculative tests to that area. Re-evaluate the
      latest coverage data and select the next highest-value area.

---

## 3) Proposed Test Plan

The areas below are **candidate gaps only**. They are not a fixed or mandatory implementation list. During execution, each iteration must use the latest `lcov` results to select the highest-value uncovered production functions/branches next, prioritizing failure/recovery paths, state transitions, cleanup/resource handling, safety guards, and high-impact orchestration logic.

Before implementing any candidate area, the agent must verify its actual coverage status using the latest lcov results. Candidate areas must not be implemented solely because they are listed in this plan.
### Area 1 — `src/dbus/xconf_comm_status.c` — `initXConfCommStatus`, `getXConfCommStatus`, `setXConfCommStatus`, `trySetXConfCommStatus`, `cleanupXConfCommStatus`

| Item | Detail |
|---|---|
| **Production source** | `src/dbus/xconf_comm_status.c` |
| **Existing coverage** | Zero — file not compiled into any TEST_COMMAND binary |
| **Missing behavior** | Every public function is untested: double-init guard, pre-init read (error log path), set TRUE/FALSE under mutex, `trySetXConfCommStatus` CAS-like exclusion (returns FALSE when already in progress), cleanup + double-cleanup guard |
| **Proposed test scenarios** | (a) Init once → returns TRUE; (b) Init twice → second call returns FALSE (already initialized guard); (c) `getXConfCommStatus` before init → returns FALSE + logs critical error; (d) `setXConfCommStatus(TRUE)` then `getXConfCommStatus` → TRUE; (e) `trySetXConfCommStatus` when idle → TRUE and status is now set; (f) `trySetXConfCommStatus` when already TRUE → FALSE; (g) `cleanupXConfCommStatus` resets state; (h) `setXConfCommStatus(FALSE)` then `getXConfCommStatus` → FALSE |
| **Value** | Adds coverage for an entire untested module; thread-safety contract for the daemon's concurrent CheckForUpdate guard |

> **Required Makefile.am change:** Add `../src/dbus/xconf_comm_status.c` to `rdkfwupdatemgr_main_flow_gtest_SOURCES`. Tests go in `unittest/rdkfwupdatemgr_main_flow_gtest.cpp`.

---

### Area 2 — `src/rdkFwupdateMgr.c` — `interuptDwnl()` branches

| Item | Detail |
|---|---|
| **Production source** | `src/rdkFwupdateMgr.c` |
| **Existing coverage** | `rdkfwupdatemgr_main_flow_gtest` exercises `rdkFwupdateMgr.c` but `interuptDwnl()` tests appear in `basic_rdkv_main_gtest.cpp` (which targets `rdkv_main.c`). The identical function body in `rdkFwupdateMgr.c` may be partially or wholly uncovered |
| **Missing behavior** | (a) Background mode (`app_mode=0`), throttle="true", download in-progress, **speed > 0**, curl non-NULL, bytes > 0 → `doInteruptDwnl()` called; (b) Foreground mode (`app_mode=1`), throttle="true", download in-progress, curl non-NULL, bytes > 0 → `doInteruptDwnl()` called at speed 0; (c) `doInteruptDwnl()` returns `DWNL_UNPAUSE_FAIL` → `doStopDownload()` called, curl nulled; (d) throttle != "true" (else branch) |
| **Proposed test scenarios** | Use mocked `doGetDwnlBytes` (returns >0), `doInteruptDwnl` (returns DWNL_UNPAUSE_FAIL in one test, 0 in others), `doStopDownload`. Set `rfc_list.rfc_throttle="true"`, `rfc_list.rfc_topspeed="1000"`, DwnlState=RDKV_FWDNLD_DOWNLOAD_INPROGRESS, curl non-NULL |
| **Value** | These are real-world download-throttle and background-mode control paths; covering them exercises mutex-protected state and cleanup logic |

> Tests go in `unittest/rdkfwupdatemgr_main_flow_gtest.cpp`.

---

### Area 3 — `src/rdkFwupdateMgr.c` — `initialValidation()` + `prevCurUpdateInfo()` file-operation branches

| Item | Detail |
|---|---|
| **Production source** | `src/rdkFwupdateMgr.c` lines ~960–1090 |
| **Existing coverage** | Some tests for `prevCurUpdateInfo` exist in `rdkfwupdatemgr_main_flow_gtest.cpp` but the multi-branch file-state logic (CDL_FLASHED_IMAGE present vs. absent, PREVIOUS_FLASHED_IMAGE present vs. absent, combined) has multiple untested paths. `initialValidation()` early-exit path (`CurrentRunningInst` returns true) may be untested |
| **Missing behavior** | `prevCurUpdateInfo`: (a) CDL_FLASHED_IMAGE absent + PREVIOUS_FLASHED_IMAGE present → copy PREVIOUS to CURRENTLY_RUNNING; (b) CDL_FLASHED_IMAGE absent + PREVIOUS absent → no copy; (c) CDL_FLASHED_IMAGE present + PREVIOUS absent → two copies (CDL→PREVIOUS and CDL→CURRENTLY_RUNNING). `initialValidation`: (d) `CurrentRunningInst` returns TRUE → function returns -1 (already running) |
| **Proposed test scenarios** | Use `TestFileCreate()` helper (already in fixture) to stage files in `/tmp/` at the paths defined by `CDL_FLASHED_IMAGE`, `PREVIOUS_FLASHED_IMAGE`, `CURRENTLY_RUNNING_IMAGE`; verify `copyFile` side-effects |
| **Value** | These are startup validation branches critical to correct incremental update behaviour; they contain `copyFile` calls that drive file-system state for subsequent boots |

> Tests go in `unittest/rdkfwupdatemgr_main_flow_gtest.cpp`.

---

### Area 4 — `src/rdkv_upgrade.c` — `dwnlError()` mediaclient HTTP code branches

| Item | Detail |
|---|---|
| **Production source** | `src/rdkv_upgrade.c` — `dwnlError()` |
| **Existing coverage** | `basic_rdkv_main_gtest.cpp` tests `HandlesCurlCode0` and `HandlesCurlCode22` (plus likely curl=18/7 and curl=other). Device-type-specific branches for mediaclient (`http_code=404`, `495`, `500–511`, `0/other`) and non-mediaclient (`http_code=0`, `http_code=495`) are likely partially or wholly uncovered |
| **Missing behavior** | (a) `device_type="mediaclient"`, `http_code=404` → "Server not Found" failure reason; (b) `device_type="mediaclient"`, `http_code=495` → "Client certificate expired" + `checkAndEnterStateRed` called; (c) `device_type="mediaclient"`, `http_code=503` → "Error response from server"; (d) `device_type="hybrid"` (non-mediaclient), `http_code=0` → ESTB download failure; (e) `curl_code=18` telemetry path |
| **Proposed test scenarios** | Add parametrized or individual `TEST()` cases in `basic_rdkv_main_gtest.cpp`; pre-populate `device_info.dev_type` with "mediaclient" or "hybrid" as needed; mock `updateFWDownloadStatus`, `checkAndEnterStateRed`, `eventManager` |
| **Value** | Error reporting and state-red transitions are safety-critical; these branches represent direct failure paths in CDL |

> Tests go in `unittest/basic_rdkv_main_gtest.cpp`.

---

### Area 5 — `src/download_status_helper.c` — `updateFWDownloadStatus()` fopen-failure and disableStatsUpdate paths

| Item | Detail |
|---|---|
| **Production source** | `src/download_status_helper.c` — `updateFWDownloadStatus()` |
| **Existing coverage** | The success path (file written) is likely tested; the NULL-parameter guard is likely tested. The `strcmp(disableStatsUpdate,"yes")==0` early-return branch and the `fopen` failure branch may not be |
| **Missing behavior** | (a) `disableStatsUpdate="yes"` → returns SUCCESS without writing; (b) `fopen(STATUS_FILE,"w")` fails (e.g., STATUS_FILE set to unwritable path `/tmp/no_write_dir/status.txt`) → returns FAILURE |
| **Proposed test scenarios** | (a) Pass a populated `FWDownloadStatus` struct with `disableStatsUpdate="yes"`, expect SUCCESS and verify STATUS_FILE not written; (b) Point STATUS_FILE to a path under a non-existent directory, expect FAILURE return |
| **Value** | The "yes" skip-write path is a PDRI-upgrade guard that silences all status updates; the fopen failure path is an error-handling path that returns FAILURE instead of writing bad data |

> Tests go in `unittest/device_status_helper_gtest.cpp`.

---

### Area 6 — `src/device_status_helper.c` — `CurrentRunningInst()` process-name-match branches

| Item | Detail |
|---|---|
| **Production source** | `src/device_status_helper.c` — `CurrentRunningInst()` |
| **Existing coverage** | NULL parameter test likely exists. The path where the PID file is present and the cmdline file matches a known process name ("rdkFwupdateMgr") is likely not tested; the path where cmdline exists but does NOT match is also likely not tested |
| **Missing behavior** | (a) PID file present, cmdline file (`/tmp/cmdline.txt` under GTEST_ENABLE) contains "rdkvfwupgrader" → returns true; (b) PID file present, cmdline file contains unrelated process name → returns false; (c) PID file present but cmdline file absent → returns false with error log |
| **Proposed test scenarios** | Use `TestFileCreate()` to write a PID value to a temp file, write `/tmp/cmdline.txt` with a null-separated cmdline string matching one of the three known process names; verify return value |
| **Value** | This guard prevents concurrent instances of the firmware updater from running; an untested false-positive or false-negative here is a safety issue |

> Tests go in `unittest/device_status_helper_gtest.cpp`.

---

### Area 7 — `src/directcdn.c` — `DirectCDNDownload()` retry-loop and allocation-failure paths

| Item | Detail |
|---|---|
| **Production source** | `src/directcdn.c` — `DirectCDNDownload()` |
| **Existing coverage** | `rdkfw_main_gtest` includes `directcdn.c`; some entry-point tests may exist. The internal retry loop (`total_retry_cnt=3`), the `allocDowndLoadDataMem` failure path, the `pServURL` malloc failure path, and the PDRI/PERI sub-download branches are likely uncovered |
| **Missing behavior** | (a) `allocDowndLoadDataMem` returns non-zero → function returns early with `curl_ret_code=-1`; (b) `pServURL` malloc returns NULL → inner block skipped; (c) Retry loop exhaustion after 3 failures → returns last error code; (d) `pci_upgrade_status == DIRECT_CDN_SUCCESS` path exiting loop early |
| **Proposed test scenarios** | Mock `allocDowndLoadDataMem` to return failure; mock server URL helpers; use mock-driven retry scenarios via `EXPECT_CALL` with `Times(3)` returning failure; verify return codes |
| **Value** | `DirectCDNDownload` orchestrates the entire direct firmware download retry logic; covering its error and retry paths ensures resilience under poor network or server conditions |

> Tests go in `unittest/basic_rdkv_main_gtest.cpp`.

---

## 4) Proposed Execution Workflow

After approval, the execution phase would proceed as follows:

1. Inspect repository state
   - Inspect SOURCE_FOLDER and UNIT_TEST_FOLDER.
   - Determine whether the unit-test build has already been bootstrapped.
   - Do not assume generated Makefiles, test binaries, .gcda, .gcno, or
     coverage .info files exist.

2. Establish a fresh execution baseline
   - Build the existing unit-test suite using BUILD_COMMAND.
   - Run the existing TEST_COMMAND without adding or modifying tests.
   - Run COVERAGE_COMMAND to generate fresh coverage data.
   - Record the resulting line and function coverage as the execution-time
     baseline.
   - Do not use the historical 62.6% / 81.8% values as the execution baseline.

3. Baseline failure gate
   - If the existing test suite cannot be built or run from the current
     repository state, stop and report BASELINE_BUILD_FAILURE or
     BASELINE_TEST_FAILURE.
   - Do not generate or modify tests to hide or repair an existing baseline
     failure unless explicitly authorized.

4. Check quality gates
   - If fresh line coverage >= 90% AND fresh function coverage >= 95%,
     return SUCCESS_NO_ACTION.
   - Otherwise, continue with the approved test-generation workflow.

5. Scan source and existing tests
   - Inspect SOURCE_FOLDER for uncovered functions, branches, error paths,
     and state transitions.
   - Cross-reference the existing tests and latest lcov results.

### Incremental execution rule

The candidate areas listed in this plan are not to be implemented as one batch.

For each iteration, the agent must:

1. Select ONE highest-value uncovered area based on the latest lcov data.
2. Add or update only the tests required for that area.
3. Build.
4. Run the relevant test(s).
5. Run coverage.
6. Compare coverage against the previous iteration.
7. Only then select the next highest-value uncovered area.

The agent must not implement all candidate areas upfront.

6. **Generate/update tests only under `UNIT_TEST_FOLDER` (`unittest/`)**
   - Add or update tests only after confirming the selected coverage gap from the latest lcov results.
   - Reuse existing fixtures, mocks, helpers, and test binaries wherever possible.
   - Modify `unittest/Makefile.am` only when required to compile or link the selected tests.
   - If a production source file is not currently part of the relevant test binary, first verify that adding it is necessary and compatible with the existing test architecture.
   - Do not modify production source files or production headers under `SOURCE_FOLDER` (`src/`).

7. **Build using BUILD_COMMAND** — `cd unittest && automake --add-missing && autoreconf --install && ./configure && make -j8`

8. **Fix test compilation/link issues** — if new source files introduce undefined symbol errors (e.g., GLib types in `xconf_comm_status.c`), add the required CFLAGS (`GLIB_CFLAGS`) to the relevant target in `Makefile.am` only.

9. **Run TEST_COMMAND** — execute all 6 binaries; collect pass/fail per test case.

10. **Fix test defects if required** — if a new test fails due to incorrect expectations or fixture setup (not a production defect), correct the test only; do not alter production code.

11. **Run COVERAGE_COMMAND** — `cd src && lcov --capture --directory . --output-file coverage.info && lcov --remove coverage.info '/usr/*' --output-file coverage.filtered.info && lcov --summary coverage.filtered.info`

12. **Identify remaining high-value coverage gaps** — if line coverage < 90% or function coverage < 95%, analyse the lcov summary to find the next highest-ROI uncovered function clusters.

13. **Iterate up to the configured repair limit (maximum 4 repair iterations)** — repeat steps 3–9 targeting new gaps each iteration; stop adding tests that are redundant or that test only already-covered paths.

14. **Stop when quality gates are met or iteration limit is reached** — report final coverage numbers and any outstanding gaps. Never claim success without command evidence from build/test/coverage outputs.

---

## 5) Repository-Specific Risks / Assumptions

| Risk / Assumption | Detail |
|---|---|
| **Test/build modification boundary** | Unit-test and test-build updates are allowed under `UNIT_TEST_FOLDER` (`unittest/`), including `unittest/Makefile.am`, when required to build or link tests. Production source files and production headers under `SOURCE_FOLDER` (`src/`) must not be modified. |
| **Autotools-generated files** | `unittest/Makefile` is a generated file. Only `unittest/Makefile.am` will be modified. After any change to `Makefile.am`, the BUILD_COMMAND (`automake --add-missing && autoreconf --install && ./configure && make -j8`) regenerates `Makefile` automatically. |
| **Multiple GoogleTest binaries** | The TEST_COMMAND runs exactly 6 of the 9 binaries defined in `bin_PROGRAMS`. `dbus_handlers_gtest`, `rdkFwupdateMgr_async_main_flow_gtest`, and `rdkFwupdateMgr_async_handlers_gtest` are not in the TEST_COMMAND and their gcda files will NOT be generated. The COVERAGE_COMMAND captures all gcda in `src/`; only the 6 executed binaries contribute. All new tests must target one of the 6 existing binaries. |
| **Duplicate source symbols** | `rdkv_main.c` and `rdkFwupdateMgr.c` define functions with identical names (`setAppMode`, `interuptDwnl`, etc.). They are compiled into separate test binaries that link independently, so there is no ODR collision. New tests targeting `interuptDwnl` must be placed in the correct binary (`rdkfwupdatemgr_main_flow_gtest` for `rdkFwupdateMgr.c`). |
| **`xconf_comm_status.c` GLib dependency** | This file uses `GMutex` and GLib types. Adding it to `rdkfwupdatemgr_main_flow_gtest_SOURCES` requires the target's CPPFLAGS to include `$(GLIB_CFLAGS)` (already present for this target) and LDFLAGS to include `$(GLIB_LIBS)`. This is already satisfied in the current `Makefile.am` for that target. |
| **GTEST_ENABLE macro** | Several production source files use `#ifndef GTEST_ENABLE` guards to exclude system headers not available in the test environment. This is already handled by `COMMON_CPPFLAGS = -DGTEST_ENABLE`. No production-code changes are needed. |
| **`STATUS_FILE` write path in `updateFWDownloadStatus`** | The fopen-failure test requires pointing STATUS_FILE at an unwritable path. Since STATUS_FILE is a compile-time constant in `download_status_helper.c`, the test will need to use a non-existent directory path (e.g., `/tmp/nonexistent_dir/fwdl_status.txt`) to force the fopen failure, without changing the production constant. |
| **`/tmp/cmdline.txt` for `CurrentRunningInst`** | Under `GTEST_ENABLE`, the production code uses `/tmp/cmdline.txt` instead of `/proc/<pid>/cmdline`. The cmdline content must use null-byte delimiters (as `getdelim` reads with `\0` as delimiter). The test fixture must write a binary-format file. |
| **Coverage threshold feasibility** The 90% line / 95% function thresholds are the final quality gates.The 62.6% / 81.8% values are reference measurements from the dry run only. Actual execution-time coverage must be established from the current repository state. Coverage improvement estimates must therefore |be based on the fresh baseline rather than the historical reference values.|
| **Coverage threshold interpretation** | Final success requires both `line_coverage >= 90%` and `function_coverage >= 95%`. The 85% line value is a minimum progress/reporting floor only and must never be treated as a success criterion. |
| **Generated gcda/gcno artifacts** | Existing `.gcda`/`.gcno` files in `src/` and `unittest/` are build artifacts from a prior run. They will be overwritten or extended by the new test run. They must not be treated as source changes. |
| **No production-code modification** | If any production defect is discovered during testing (e.g., a crash triggered by a new test), the test will be suspended and the defect reported separately. Production code under `src/` will not be modified. |

---

## 6) Expected Stop Conditions

The execution phase will stop when **any** of the following conditions is true:
1. **Baseline build/test failure:**
   If the existing unit-test suite cannot be built or executed from the
   current repository state without modifying production code, stop and
   report BASELINE_BUILD_FAILURE or BASELINE_TEST_FAILURE.
2. **Quality gates satisfied:** `line_coverage >= 90%` AND `function_coverage >= 95%` — report `SUCCESS`.
3. **Iteration limit reached:** The configured maximum of 4 improve-build-test-coverage cycles is exhausted; report the final coverage and the remaining gap.
4. **Minimum progress floor:** If the final line coverage remains below 85% after the allowed iterations, report the result as significantly incomplete and identify the remaining coverage gaps. The 85% value is a progress/reporting indicator only and is not a success or alternate quality gate.
5. **Unresolvable build failure:** A compilation or link error in the test binary that cannot be fixed by modifying test or mock files alone (would require production-code changes); report `BLOCKED_BUILD_FAILURE` with the exact error.
6. **Unresolvable test failure caused by suspected production defect:** A test failure that reveals an apparent defect in production logic; stop modifying that code path, report evidence, and flag for separate review.
7. **Coverage stagnates:** Two consecutive iterations produce less than +0.5 pp line coverage gain despite adding new tests; report diminishing-returns stall and stop.

---

## 7) Execution Decision Rule

Before implementing any candidate test area:

1. Verify the function/file is actually uncovered or materially under-covered using the latest lcov data.
2. Confirm the selected test binary can exercise the production code.
3. Reuse existing mocks, fixtures, helpers, and test infrastructure where possible.
4. Make the smallest test-only change required.
5. Rebuild and re-measure coverage before selecting the next gap.

Candidate areas are not mandatory. The agent must not implement tests solely because an area appears in this plan.

---

## 8) Approval Gate

**READY_FOR_APPROVAL: YES**
