# Phase 1 Batch 1: fetch_xconf_firmware_info() Unit Tests

## Decision: Option A - Expose Function for Direct Testing

**Rationale:**
- Maximizes code coverage by testing function directly
- Reduces test complexity (no need to set up full handler context)
- Follows best practices for unit testing (test units in isolation)
- Already established pattern with other internal functions exposed via GTEST_ENABLE

**Implementation:**
1. Change `static int fetch_xconf_firmware_info(...)` to non-static under `#ifdef GTEST_ENABLE`
2. Declare function in rdkFwupdateMgr_handlers.h under `#ifdef GTEST_ENABLE`
3. Create comprehensive unit tests in rdkFwupdateMgr_handlers_gtest.cpp

---

## Batch 1: 10 Comprehensive Tests for fetch_xconf_firmware_info()

### Test Coverage Strategy

The function has the following critical code paths:

1. **Memory Allocation Paths (3 allocations)**
   - allocDowndLoadDataMem() failure
   - pJSONStr malloc() failure  
   - pServURL malloc() failure

2. **Server URL Handling**
   - GetServURL() returns 0 (no URL)
   - GetServURL() returns valid URL

3. **XConf Communication**
   - rdkv_upgrade_request() success (ret=0, http_code=200, data received)
   - rdkv_upgrade_request() failure (ret != 0)
   - rdkv_upgrade_request() HTTP error (http_code != 200)
   - rdkv_upgrade_request() null data (DwnLoc.pvOut == NULL)

4. **Response Parsing**
   - getXconfRespData() success (ret=0)
   - getXconfRespData() failure (ret != 0)

5. **Caching**
   - save_xconf_to_cache() success/failure

6. **Edge Cases**
   - NULL pResponse parameter
   - NULL pHttp_code parameter
   - Invalid server_type values

---

## Test List (10 Tests)

### Test Suite: FetchXconfFirmwareInfoTest

#### Test 1: Success_Http200_ValidResponse_ParseSuccess
**Objective:** Happy path - all mocks return success
**Setup:**
- Mock allocDowndLoadDataMem() → return 0
- Mock GetServURL() → return strlen("http://xconf.example.com/"), copy URL
- Mock createJsonString() → return strlen(json), copy valid JSON
- Mock rdkv_upgrade_request() → return 0, set http_code=200, populate DwnLoc.pvOut with valid JSON
- Mock getXconfRespData() → return 0, populate pResponse with firmware data

**Expected:**
- Function returns 0
- *pHttp_code = 200
- pResponse populated with correct firmware version/location

---

#### Test 2: Failure_AllocDownloadDataMem_ReturnsError
**Objective:** Test memory allocation failure for download buffer
**Setup:**
- Mock allocDowndLoadDataMem() → return -1 (failure)

**Expected:**
- Function returns -1
- No other mocks called (early exit)

---

#### Test 3: Failure_MallocJsonString_ReturnsError
**Objective:** Test malloc failure for JSON string buffer
**Setup:**
- Mock allocDowndLoadDataMem() → return 0
- Simulate malloc failure for pJSONStr (this requires a wrapper or testing at integration level)
  - **Alternative approach:** Since we can't directly mock malloc, we test the code path indirectly via allocDowndLoadDataMem failure in Test 2

**Note:** This test is challenging without malloc() wrapping. We'll focus on other critical paths.
**Action:** SKIP this test, covered by integration testing. Replace with Test 3b below.

---

#### Test 3b: Failure_GetServURL_ReturnsZero_NoValidURL
**Objective:** Test when GetServURL returns 0 (no server URL configured)
**Setup:**
- Mock allocDowndLoadDataMem() → return 0
- Mock GetServURL() → return 0 (no URL)

**Expected:**
- Function returns -1
- SWLOG_ERROR logged: "no valid server URL"
- rdkv_upgrade_request() not called

---

#### Test 4: Failure_RdkvUpgradeRequest_ReturnsFail
**Objective:** Test when rdkv_upgrade_request() fails
**Setup:**
- Mock allocDowndLoadDataMem() → return 0
- Mock GetServURL() → return valid URL length
- Mock createJsonString() → return valid JSON length
- Mock rdkv_upgrade_request() → return -1 (failure)

**Expected:**
- Function returns -1
- SWLOG_ERROR logged: "XConf communication failed"

---

#### Test 5: Failure_RdkvUpgradeRequest_Http404
**Objective:** Test HTTP 404 error from XConf server
**Setup:**
- Mock allocDowndLoadDataMem() → return 0
- Mock GetServURL() → return valid URL
- Mock createJsonString() → return valid JSON
- Mock rdkv_upgrade_request() → return 0, set http_code=404

**Expected:**
- Function returns -1 (or appropriate error code)
- SWLOG_ERROR logged with http_code=404

---

#### Test 6: Failure_RdkvUpgradeRequest_Http500
**Objective:** Test HTTP 500 server error
**Setup:**
- Mock allocDowndLoadDataMem() → return 0
- Mock GetServURL() → return valid URL
- Mock createJsonString() → return valid JSON
- Mock rdkv_upgrade_request() → return 0, set http_code=500

**Expected:**
- Function returns -1
- SWLOG_ERROR logged with http_code=500

---

#### Test 7: Failure_RdkvUpgradeRequest_NullData
**Objective:** Test when rdkv_upgrade_request succeeds but returns NULL data
**Setup:**
- Mock allocDowndLoadDataMem() → return 0
- Mock GetServURL() → return valid URL
- Mock createJsonString() → return valid JSON
- Mock rdkv_upgrade_request() → return 0, set http_code=200, but leave DwnLoc.pvOut=NULL

**Expected:**
- Function returns -1 (fails validation check)
- SWLOG_ERROR logged: "DwnLoc.pvOut=NULL"

---

#### Test 8: Failure_GetXconfRespData_ParseFail
**Objective:** Test when XConf response parsing fails
**Setup:**
- Mock allocDowndLoadDataMem() → return 0
- Mock GetServURL() → return valid URL
- Mock createJsonString() → return valid JSON
- Mock rdkv_upgrade_request() → return 0, http_code=200, valid data
- Mock getXconfRespData() → return -1 (parse failure)

**Expected:**
- Function returns -1
- SWLOG_ERROR logged: "Failed to parse XConf response"

---

#### Test 9: Success_CacheSaveSuccess
**Objective:** Verify save_xconf_to_cache() is called after successful XConf fetch
**Setup:**
- Mock all dependencies for success path
- Monitor file I/O (check if /tmp/xconf_response_thunder.txt is written)

**Expected:**
- Function returns 0
- Cache file created with XConf response
- SWLOG_INFO logged: "XConf response cached successfully"

---

#### Test 10: Success_ServerTypeDirect_ValidResponse
**Objective:** Test with server_type=SERVER_DIRECT (vs SERVER_CODEBIG)
**Setup:**
- Call fetch_xconf_firmware_info() with server_type=SERVER_DIRECT (value 0 or 1, check rdkv_upgrade.h)
- Mock all dependencies for success

**Expected:**
- Function returns 0
- RdkUpgradeContext_t.server_type set correctly
- Response parsed successfully

---

## Test Implementation Notes

### Mocking Strategy

**Already Mocked (in miscellaneous_mock.cpp):**
- `rdkv_upgrade_request()` ✅

**Need to Mock (in rdkFwupdateMgr_mock.cpp):**
- `allocDowndLoadDataMem()` ✅ (already exists)
- `GetServURL()` ✅ (already exists)
- `createJsonString()` ✅ (already exists)
- `getXconfRespData()` ✅ (already exists)
- `getRFCSettings()` ✅ (already exists)

**GLib Functions (no mocking needed):**
- `g_file_set_contents()` - Real file I/O to /tmp (easier to test)
- `g_file_get_contents()` - Real file I/O

### Memory Management Verification

Each test MUST verify:
1. All allocated memory is freed (DwnLoc.pvOut, pJSONStr, pServURL)
2. No memory leaks via valgrind or ASAN
3. Proper cleanup on error paths

### Code Coverage Target

- **Lines covered:** ~95% of fetch_xconf_firmware_info()
- **Branches covered:** All critical if/else branches
- **Missing coverage:** malloc() failure paths (acceptable, tested via integration)

---

## Implementation Checklist

- [ ] 1. Modify rdkFwupdateMgr_handlers.c: Make fetch_xconf_firmware_info() non-static under GTEST_ENABLE
- [ ] 2. Modify rdkFwupdateMgr_handlers.h: Declare fetch_xconf_firmware_info() under GTEST_ENABLE
- [ ] 3. Update miscellaneous_mock.cpp: Enhance rdkv_upgrade_request() mock if needed
- [ ] 4. Implement Test 1: Success_Http200_ValidResponse_ParseSuccess
- [ ] 5. Implement Test 2: Failure_AllocDownloadDataMem_ReturnsError
- [ ] 6. Implement Test 3b: Failure_GetServURL_ReturnsZero_NoValidURL
- [ ] 7. Implement Test 4: Failure_RdkvUpgradeRequest_ReturnsFail
- [ ] 8. Implement Test 5: Failure_RdkvUpgradeRequest_Http404
- [ ] 9. Implement Test 6: Failure_RdkvUpgradeRequest_Http500
- [ ] 10. Implement Test 7: Failure_RdkvUpgradeRequest_NullData
- [ ] 11. Implement Test 8: Failure_GetXconfRespData_ParseFail
- [ ] 12. Implement Test 9: Success_CacheSaveSuccess
- [ ] 13. Implement Test 10: Success_ServerTypeDirect_ValidResponse
- [ ] 14. Run tests: `./run_ut.sh`
- [ ] 15. Verify coverage: Check lcov report for fetch_xconf_firmware_info()
- [ ] 16. Fix any compilation errors
- [ ] 17. Fix any test failures
- [ ] 18. Document results in BATCH_P1_B1_RESULTS.md

---

## Next Steps After Batch 1

1. Review coverage report
2. Identify any missed branches
3. Add supplementary tests if coverage < 90%
4. Move to Batch 2: Next 10 tests (likely for rdkFwupdateMgr_downloadFirmware or progress monitor)

---

## Success Criteria

✅ All 10 tests pass
✅ No compilation errors
✅ No memory leaks
✅ Coverage for fetch_xconf_firmware_info() ≥ 90%
✅ Tests executable via `./run_ut.sh`
✅ Tests integrated with rdkFwupdateMgr_handlers_gtest.cpp

---

**Status:** Ready to begin implementation
**ETA:** 2-3 hours for full implementation and validation
