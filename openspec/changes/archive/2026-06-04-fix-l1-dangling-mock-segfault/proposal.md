## Why

The L1 CI gate for PR #249 (`topic/RDKEMW-9150`) fails because `rdkfw_main_gtest` segfaults in `MainHelperFunctionTest.getXconfResTest`. The crash is caused by a dangling `g_DeviceUtilsMock` pointer left behind by a preceding test (`flashImageTestMaintFalse`). The recently added `isDirectCDNEnabled()` call in `getXconfRespData()` now dereferences that pointer, triggering a use-after-free. This is a test-isolation defect, not a production bug — the fix is strictly test-only.

## What Changes

- Reset `g_DeviceUtilsMock` to `nullptr` at the end of `flashImageTestMaintFalse` and the small set of adjacent `flashImage*` tests that share the same dangling-pointer pattern.
- Remove the phantom `rdkFwupdateMgr_async_main_flow_gtest` and `rdkFwupdateMgr_async_handlers_gtest` entries from `bin_PROGRAMS` in `unittest/Makefile.am` (no `_SOURCES` defined; causes a secondary build error).
- No production source changes. No new tests. No refactoring.

## Capabilities

### New Capabilities

- `test-mock-isolation`: Ensure stack-local mock objects in `basic_rdkv_main_gtest.cpp` do not leave dangling global pointers after test completion.

### Modified Capabilities

(none — no spec-level behaviour changes)

## Impact

- **Code**: `unittest/basic_rdkv_main_gtest.cpp` (5–7 tests gain a cleanup line), `unittest/Makefile.am` (remove 2 phantom program names).
- **APIs**: None.
- **Dependencies**: None.
- **Systems**: Unblocks the L1 CI gate for PR #249.
