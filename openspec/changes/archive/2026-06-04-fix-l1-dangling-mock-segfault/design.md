## Context

`rdkfw_main_gtest` contains ~80 standalone `TEST()` cases in `basic_rdkv_main_gtest.cpp`. Many tests create a stack-local `DeviceUtilsMock DeviceMock`, assign `g_DeviceUtilsMock = &DeviceMock;`, and then let `DeviceMock` go out of scope without resetting the global pointer to `nullptr`. This was harmless until the Direct CDN work added an `isDirectCDNEnabled()` call inside `getXconfRespData()` (json_process.c). The mock dispatcher for `isDirectCDNEnabled()` (deviceutils_mock.cpp:224) performs a NULL check on `g_DeviceUtilsMock` then calls a virtual method on it — if the pointer is dangling, the vtable lookup segfaults.

The immediately preceding test `flashImageTestMaintFalse` (line 1325) is the direct trigger: it sets `g_DeviceUtilsMock = &DeviceMock;`, only resets `global_mockexternal_ptr = NULL;`, and leaves `g_DeviceUtilsMock` dangling. The next test `getXconfResTest` (line 1340) calls `getXconfRespData()` → `isDirectCDNEnabled()` → crash.

A secondary build error exists: `bin_PROGRAMS` in `unittest/Makefile.am` lists `rdkFwupdateMgr_async_main_flow_gtest` and `rdkFwupdateMgr_async_handlers_gtest` without `_SOURCES` definitions (introduced by commit ed0760d6). This causes a make error but does not block the tests that were already built.

## Goals / Non-Goals

**Goals:**
- Eliminate the segfault in `rdkfw_main_gtest` by resetting `g_DeviceUtilsMock = nullptr` in all `flashImage*` tests that use stack-local mocks without cleanup.
- Remove phantom `bin_PROGRAMS` entries to clear the secondary build error.
- Unblock the L1 CI gate for PR #249.

**Non-Goals:**
- Refactoring tests to use gtest fixtures (TEST_F with SetUp/TearDown). This is a broader cleanup best done separately.
- Modifying production code (`json_process.c`, `directcdn.c`, `deviceutils.c`, etc.).
- Auditing the full test suite for other mock-leakage patterns beyond `g_DeviceUtilsMock` in this file.
- Adding new test coverage for Direct CDN — that already exists in the same file.

## Decisions

### D1: Reset `g_DeviceUtilsMock` in all same-pattern `flashImage*` tests, not just the directly crashing one

**Rationale:** Only `flashImageTestMaintFalse` is the *direct* trigger today, but six other `flashImage*` tests (lines 1197–1310) share the identical pattern: `g_DeviceUtilsMock = &DeviceMock;` + no cleanup. Any reordering of tests (e.g. `--gtest_shuffle`) or addition of a new `isDirectCDNEnabled()` call downstream would cause the same crash. Fixing them all (adding one line each) is trivially reviewable and prevents regression.

**Alternative considered:** Fix only `flashImageTestMaintFalse`. Rejected because it creates a known latent defect in the adjacent tests and is likely to be flagged in review.

### D2: Do NOT convert tests to TEST_F fixtures

**Rationale:** Introducing a fixture class (`FlashImageTest` with `TearDown()`) is cleaner long-term but broadens the patch scope, touches many more lines, and risks rebasing conflicts with other in-flight PRs. The single-line `g_DeviceUtilsMock = nullptr;` cleanup is strictly additive and minimal.

### D3: Include Makefile phantom cleanup in this patch

**Rationale:** The two phantom `bin_PROGRAMS` entries (`rdkFwupdateMgr_async_main_flow_gtest`, `rdkFwupdateMgr_async_handlers_gtest`) are a 1-line Makefile edit and cause a visible build error in the same CI log. Reviewers would expect them to be addressed together. However, if the PR owner prefers, this can be split out — it's mechanically independent.

### D4: No production code changes

The segfault is purely a test-isolation defect. The `isDirectCDNEnabled()` mock implementation already has a NULL guard; the problem is that the dangling pointer bypasses it. No changes to `json_process.c`, `directcdn.c`, `rfcinterface.c`, or any `src/` file are needed.

## Risks / Trade-offs

- **[Risk] Other tests may have the same dangling-mock pattern for other globals (e.g. `global_mockexternal_ptr`).** → Mitigation: Out of scope. `global_mockexternal_ptr` is already reset to NULL in the affected tests. A broader audit can follow separately.
- **[Risk] Fixing multiple tests instead of one increases diff size slightly.** → Mitigation: Each change is a single identical line (`g_DeviceUtilsMock = nullptr;`), trivially reviewable.
- **[Risk] Makefile cleanup may conflict with other PRs adding `_SOURCES` for the phantom targets.** → Mitigation: If those targets are intended, the correct fix is adding `_SOURCES` rather than removing them. Check with the ed0760d6 author if unclear.
