## 1. Fix dangling g_DeviceUtilsMock in flashImage tests

- [x] 1.1 Add `g_DeviceUtilsMock = nullptr;` before the closing brace of `flashImageTest` (line ~1200)
- [x] 1.2 Add `g_DeviceUtilsMock = nullptr;` before the closing brace of `flashImageTestRedState` (line ~1215)
- [x] 1.3 Add `g_DeviceUtilsMock = nullptr;` before the closing brace of `flashImageTestFail` (line ~1231)
- [x] 1.4 Add `g_DeviceUtilsMock = nullptr;` before the closing brace of `flashImageTestFail1` (line ~1247)
- [x] 1.5 Add `g_DeviceUtilsMock = nullptr;` before the closing brace of `flashImageTestFail2` (line ~1263)
- [x] 1.6 Add `g_DeviceUtilsMock = nullptr;` before the closing brace of `flashImageTestRebootTrue` (line ~1279)
- [x] 1.7 Add `g_DeviceUtilsMock = nullptr;` before the closing brace of `flashImageTestPdri` (line ~1294)
- [x] 1.8 Add `g_DeviceUtilsMock = nullptr;` before the closing brace of `flashImageTestMaintTrue` (line ~1310)
- [x] 1.9 Add `g_DeviceUtilsMock = nullptr;` before the closing brace of `flashImageTestMaintFalse` (line ~1325)

## 2. Remove phantom bin_PROGRAMS entries

- [ ] 2.1 Remove `rdkFwupdateMgr_async_main_flow_gtest` and `rdkFwupdateMgr_async_handlers_gtest` from `bin_PROGRAMS` in `unittest/Makefile.am` (line 21)

## 3. Validate

- [ ] 3.1 Build `rdkfw_main_gtest` and confirm no compile errors
- [ ] 3.2 Run `./rdkfw_main_gtest --gtest_filter=MainHelperFunctionTest.getXconfResTest` — must pass
- [ ] 3.3 Run full `rdkfw_main_gtest` suite — all 123 tests must pass
- [ ] 3.4 Run `make` in unittest/ — confirm no "No rule to make target" errors
- [ ] 3.5 Run `./run_ut.sh` end-to-end — confirm "L1 TEST PASSED"
