# 📋 Phase 1: Business Logic Deep Coverage - Detailed Batch Plans

**Project:** RDK Firmware Updater  
**Phase:** 1 - Business Logic Deep Coverage  
**Goal:** Increase coverage from ~80-85% to 90-95%  
**Target Files:** `rdkFwupdateMgr_handlers.c`, `rdkFwupdateMgr.c`  
**Duration:** 4-6 weeks  
**Batches:** 8 batches × 10 tests each = 80 tests  

---

## 📋 Table of Contents

1. [Phase Overview](#phase-overview)
2. [Batch 1: XConf Fetching](#batch-1-xconf-fetching-) ✅
3. [Batch 2: Cache Helper Functions](#batch-2-cache-helper-functions)
4. [Batch 3: Response Builders & Validators](#batch-3-response-builders--validators)
5. [Batch 4: Version Comparison Logic](#batch-4-version-comparison-logic)
6. [Batch 5: CheckForUpdate Edge Cases](#batch-5-checkforupdate-edge-cases)
7. [Batch 6: Error Handling Paths](#batch-6-error-handling-paths)
8. [Batch 7: Memory Management](#batch-7-memory-management)
9. [Batch 8: Utility Functions](#batch-8-utility-functions)

---

## 🎯 Phase Overview

### **Objective**
Achieve comprehensive coverage of business logic in `rdkFwupdateMgr_handlers.c` by testing:
- Internal helper functions (currently untested)
- Edge cases in existing tested functions
- Error paths and boundary conditions
- Memory management scenarios

### **Approach**
- **Focus**: Internal static functions exposed via `GTEST_ENABLE`
- **Strategy**: Test one function family per batch
- **Quality**: Each test validates one specific scenario
- **Integration**: Tests must pass via `./run_ut.sh` before next batch

### **Success Metrics**
- ✅ Coverage increase from 80-85% → 90-95%
- ✅ All 80 tests pass in CI/CD
- ✅ No memory leaks detected
- ✅ Full documentation for each batch

---

## 🔧 Batch 1: XConf Fetching ✅

**Status:** Complete  
**Function:** `fetch_xconf_firmware_info()`  
**Tests Implemented:** 6 tests  
**Coverage Gain:** ~5% (estimated)  

### **Completed Tests**
1. ✅ `FetchXconfFirmwareInfo_UrlAllocationFails_ReturnsError`
2. ✅ `FetchXconfFirmwareInfo_HttpGetSuccess_ReturnsTrue`
3. ✅ `FetchXconfFirmwareInfo_HttpGetFails_ReturnsFalse`
4. ✅ `FetchXconfFirmwareInfo_HttpGet404_ReturnsFalse`
5. ✅ `FetchXconfFirmwareInfo_ParseError_ReturnsFalse`
6. ✅ `FetchXconfFirmwareInfo_CacheSaveSuccess_SavesCacheFile`

### **Documentation**
- ✅ [`BATCH_P1_B1_EXECUTION_PLAN.md`](./BATCH_P1_B1_EXECUTION_PLAN.md)
- ✅ [`BATCH_P1_B1_IMPLEMENTATION_SUMMARY.md`](./BATCH_P1_B1_IMPLEMENTATION_SUMMARY.md)
- ✅ [`BATCH_P1_B1_QUICK_REFERENCE.md`](./BATCH_P1_B1_QUICK_REFERENCE.md)

### **Next Steps**
- 🔄 User validation: Run `./run_ut.sh` and verify all tests pass
- 🔄 Generate coverage report to confirm ~5% gain
- ✅ Proceed to Batch 2 after validation

---

## 🗄️ Batch 2: Cache Helper Functions

**Status:** Planned  
**Functions:** Cache loading, saving, validation  
**Estimated Tests:** 10 tests  
**Estimated Coverage Gain:** ~3%  

### **Functions to Test**

#### **1. `load_xconf_from_cache()`**
Helper function that loads cached XConf data from filesystem.

**Test Scenarios:**
1. **Test 1**: Cache file exists and valid → Returns cached data
2. **Test 2**: Cache file missing → Returns NULL
3. **Test 3**: Cache file corrupt (invalid JSON) → Returns NULL
4. **Test 4**: Cache file too old (expired TTL) → Returns NULL
5. **Test 5**: Cache file read permission denied → Returns NULL

#### **2. `save_xconf_to_cache()`**
Helper function that saves XConf data to filesystem cache.

**Test Scenarios:**
6. **Test 6**: Valid data + write permissions → Cache file created
7. **Test 7**: NULL firmware_info → Returns false, no file created
8. **Test 8**: Write permission denied → Returns false
9. **Test 9**: Disk full scenario → Returns false
10. **Test 10**: Cache directory missing → Creates directory + cache file

### **Mock Requirements**
- ✅ **Existing Mocks**: `g_RdkFwupdateMgrMock` (no new mocks needed)
- **Filesystem**: Use real filesystem (temp dir pattern from Batch 1)
- **System Calls**: Real `access()`, `stat()`, `fopen()`, etc.

### **Code Changes Required**
```cpp
// rdkFwupdateMgr_handlers.h (add under GTEST_ENABLE)
#ifdef GTEST_ENABLE
bool load_xconf_from_cache(firmware_info_t *firmware_info);
bool save_xconf_to_cache(const firmware_info_t *firmware_info);
#endif

// rdkFwupdateMgr_handlers.c (expose static functions)
#ifdef GTEST_ENABLE
bool load_xconf_from_cache(firmware_info_t *firmware_info)
#else
static bool load_xconf_from_cache(firmware_info_t *firmware_info)
#endif
{
    // ... existing code ...
}

#ifdef GTEST_ENABLE
bool save_xconf_to_cache(const firmware_info_t *firmware_info)
#else
static bool save_xconf_to_cache(const firmware_info_t *firmware_info)
#endif
{
    // ... existing code ...
}
```

### **Test Structure**
```cpp
// File: rdkfwupdater/unittest/rdkFwupdateMgr_handlers_gtest.cpp

TEST_F(RdkFwupdateMgrHandlersTest, LoadXconfFromCache_ValidCache_ReturnsData)
{
    // Arrange: Create valid cache file with known data
    // Act: Call load_xconf_from_cache()
    // Assert: Verify returned data matches cache content
}

TEST_F(RdkFwupdateMgrHandlersTest, LoadXconfFromCache_MissingCache_ReturnsNull)
{
    // Arrange: Ensure cache file doesn't exist
    // Act: Call load_xconf_from_cache()
    // Assert: Returns NULL
}

// ... 8 more tests ...
```

### **Success Criteria**
- ✅ All 10 tests pass via `./run_ut.sh`
- ✅ Coverage for cache functions reaches 90%+
- ✅ No memory leaks in cache operations
- ✅ Batch summary document created

### **Estimated Effort**
- **Planning**: 1 hour
- **Implementation**: 4-6 hours
- **Testing & Validation**: 2-3 hours
- **Documentation**: 1-2 hours
- **Total**: 1-2 days

---

## 🔍 Batch 3: Response Builders & Validators

**Status:** Planned  
**Functions:** D-Bus response construction, validation  
**Estimated Tests:** 10 tests  
**Estimated Coverage Gain:** ~2%  

### **Functions to Test**

#### **1. `build_checkforupdate_response()`**
Constructs D-Bus response dictionary for CheckForUpdate.

**Test Scenarios:**
1. **Test 1**: Valid firmware_info → Builds complete response
2. **Test 2**: NULL firmware_info → Returns empty/error response
3. **Test 3**: Missing optional fields → Response with defaults
4. **Test 4**: All fields populated → Full response
5. **Test 5**: Special characters in strings → Properly escaped

#### **2. `validate_firmware_info()`**
Validates firmware_info structure before use.

**Test Scenarios:**
6. **Test 6**: Valid firmware_info → Returns true
7. **Test 7**: NULL firmware_info → Returns false
8. **Test 8**: Missing required field (version) → Returns false
9. **Test 9**: Invalid URL format → Returns false
10. **Test 10**: Empty version string → Returns false

### **Mock Requirements**
- ✅ **Existing Mocks**: `g_RdkFwupdateMgrMock`
- **D-Bus**: May need to mock `g_variant_builder_*` calls (check existing tests)

### **Code Changes Required**
```cpp
// rdkFwupdateMgr_handlers.h (add under GTEST_ENABLE)
#ifdef GTEST_ENABLE
GVariant* build_checkforupdate_response(const firmware_info_t *firmware_info);
bool validate_firmware_info(const firmware_info_t *firmware_info);
#endif
```

### **Success Criteria**
- ✅ All 10 tests pass
- ✅ Response builder coverage 90%+
- ✅ Validator coverage 100%
- ✅ Documentation complete

### **Estimated Effort**
- **Planning**: 1 hour
- **Implementation**: 4-5 hours
- **Testing & Validation**: 2 hours
- **Documentation**: 1 hour
- **Total**: 1 day

---

## 🔢 Batch 4: Version Comparison Logic

**Status:** Planned  
**Functions:** Version parsing, comparison  
**Estimated Tests:** 10 tests  
**Estimated Coverage Gain:** ~3%  

### **Functions to Test**

#### **1. `compare_versions()`**
Compares two firmware version strings.

**Test Scenarios:**
1. **Test 1**: v1 > v2 → Returns positive
2. **Test 2**: v1 < v2 → Returns negative
3. **Test 3**: v1 == v2 → Returns zero
4. **Test 4**: Different version formats (1.2.3 vs 1.2) → Handles correctly
5. **Test 5**: Non-numeric versions (v1.2-beta) → Handles correctly

#### **2. `parse_version_string()`**
Parses version string into comparable components.

**Test Scenarios:**
6. **Test 6**: Standard version "1.2.3" → Parses to major.minor.patch
7. **Test 7**: Version with suffix "1.2.3-rc1" → Parses correctly
8. **Test 8**: Invalid version string → Returns error
9. **Test 9**: Empty version string → Returns error
10. **Test 10**: Version with build metadata → Ignores metadata

### **Mock Requirements**
- ✅ **No new mocks needed** (pure logic functions)

### **Code Changes Required**
```cpp
// rdkFwupdateMgr_handlers.h
#ifdef GTEST_ENABLE
int compare_versions(const char *v1, const char *v2);
bool parse_version_string(const char *version, version_info_t *parsed);
#endif
```

### **Success Criteria**
- ✅ All 10 tests pass
- ✅ Version comparison logic 100% covered
- ✅ Edge cases handled (NULL, empty, malformed)
- ✅ Documentation complete

### **Estimated Effort**
- **Planning**: 0.5 hours
- **Implementation**: 3-4 hours
- **Testing & Validation**: 2 hours
- **Documentation**: 1 hour
- **Total**: 0.5-1 day

---

## 🚨 Batch 5: CheckForUpdate Edge Cases

**Status:** Planned  
**Functions:** CheckForUpdate handler edge cases  
**Estimated Tests:** 10 tests  
**Estimated Coverage Gain:** ~2%  

### **Focus Areas**

#### **1. Parameter Validation**
Test `handle_checkforupdate()` with various invalid inputs.

**Test Scenarios:**
1. **Test 1**: NULL invocation parameter → Returns error
2. **Test 2**: NULL user_data → Handles gracefully
3. **Test 3**: Invalid D-Bus method call → Returns error
4. **Test 4**: Missing parameters in method call → Returns error
5. **Test 5**: Extra unexpected parameters → Ignores safely

#### **2. State Machine Edge Cases**
Test state transitions and conflicts.

**Test Scenarios:**
6. **Test 6**: CheckForUpdate while download in progress → Returns busy
7. **Test 7**: CheckForUpdate while flash in progress → Returns busy
8. **Test 8**: Multiple concurrent CheckForUpdate calls → Queue/reject properly
9. **Test 9**: CheckForUpdate after failed download → Allows retry
10. **Test 10**: CheckForUpdate timeout scenario → Returns timeout error

### **Mock Requirements**
- ✅ **Existing Mocks**: `g_RdkFwupdateMgrMock`
- **State**: May need to set global state variables

### **Code Changes Required**
```cpp
// May need to expose state checking functions
#ifdef GTEST_ENABLE
bool is_update_in_progress(void);
update_state_t get_current_state(void);
#endif
```

### **Success Criteria**
- ✅ All 10 tests pass
- ✅ Edge cases covered
- ✅ No race conditions exposed
- ✅ Documentation complete

### **Estimated Effort**
- **Planning**: 1-2 hours
- **Implementation**: 5-6 hours
- **Testing & Validation**: 3 hours
- **Documentation**: 1 hour
- **Total**: 1-2 days

---

## ❌ Batch 6: Error Handling Paths

**Status:** Planned  
**Functions:** Error propagation, recovery  
**Estimated Tests:** 10 tests  
**Estimated Coverage Gain:** ~3%  

### **Focus Areas**

#### **1. Network Error Handling**
Test error paths in network operations.

**Test Scenarios:**
1. **Test 1**: XConf server timeout → Proper error reported
2. **Test 2**: XConf server returns 500 → Error handled
3. **Test 3**: Connection refused → Error handled
4. **Test 4**: DNS resolution failure → Error handled
5. **Test 5**: SSL/TLS error → Error handled

#### **2. Filesystem Error Handling**
Test error paths in file operations.

**Test Scenarios:**
6. **Test 6**: Cache directory not writable → Error handled
7. **Test 7**: Disk full during cache write → Error handled
8. **Test 8**: Config file missing → Uses defaults
9. **Test 9**: Config file corrupt → Error reported
10. **Test 10**: Log file write failure → Handled gracefully

### **Mock Requirements**
- ✅ **Existing Mocks**: `g_RdkFwupdateMgrMock`
- **System Calls**: Mock `errno` scenarios

### **Code Changes Required**
```cpp
// May need to expose error handling helpers
#ifdef GTEST_ENABLE
void handle_network_error(http_error_t error);
void handle_filesystem_error(fs_error_t error);
#endif
```

### **Success Criteria**
- ✅ All 10 tests pass
- ✅ Error paths covered
- ✅ No crashes on errors
- ✅ Proper error logging verified
- ✅ Documentation complete

### **Estimated Effort**
- **Planning**: 1 hour
- **Implementation**: 5-6 hours
- **Testing & Validation**: 3 hours
- **Documentation**: 1 hour
- **Total**: 1-2 days

---

## 💾 Batch 7: Memory Management

**Status:** Planned  
**Functions:** Allocation, deallocation, leak prevention  
**Estimated Tests:** 10 tests  
**Estimated Coverage Gain:** ~2%  

### **Focus Areas**

#### **1. Allocation Failure Handling**
Test behavior when memory allocation fails.

**Test Scenarios:**
1. **Test 1**: `malloc()` fails in `fetch_xconf_firmware_info()` → Error handled
2. **Test 2**: `strdup()` fails in URL construction → Error handled
3. **Test 3**: `calloc()` fails in structure allocation → Error handled
4. **Test 4**: Multiple allocation failures → All handled
5. **Test 5**: Partial allocation then failure → Cleanup proper

#### **2. Deallocation Correctness**
Test proper memory cleanup.

**Test Scenarios:**
6. **Test 6**: `free_firmware_info()` with NULL → No crash
7. **Test 7**: `free_firmware_info()` with partial data → All freed
8. **Test 8**: Double-free protection → No crash
9. **Test 9**: Cleanup after error path → No leaks
10. **Test 10**: Cleanup after successful operation → No leaks

### **Mock Requirements**
- ✅ **Existing Mocks**: `g_RdkFwupdateMgrMock`
- **Memory**: Mock `malloc()`, `calloc()`, `strdup()` to simulate failures

### **Code Changes Required**
```cpp
// May need to expose cleanup functions
#ifdef GTEST_ENABLE
void free_firmware_info(firmware_info_t *info);
void cleanup_xconf_data(xconf_data_t *data);
#endif
```

### **Success Criteria**
- ✅ All 10 tests pass
- ✅ No memory leaks (valgrind clean)
- ✅ Allocation failures handled
- ✅ Documentation complete

### **Estimated Effort**
- **Planning**: 1 hour
- **Implementation**: 4-5 hours
- **Testing & Validation**: 3-4 hours (valgrind)
- **Documentation**: 1 hour
- **Total**: 1-2 days

---

## 🛠️ Batch 8: Utility Functions

**Status:** Planned  
**Functions:** String helpers, parsers, validators  
**Estimated Tests:** 10 tests  
**Estimated Coverage Gain:** ~2%  

### **Functions to Test**

#### **1. String Utilities**
Test string manipulation helpers.

**Test Scenarios:**
1. **Test 1**: `trim_whitespace()` → Removes leading/trailing spaces
2. **Test 2**: `safe_strncpy()` → Doesn't overflow buffer
3. **Test 3**: `string_to_lower()` → Converts to lowercase
4. **Test 4**: `string_replace()` → Replaces substring
5. **Test 5**: `string_split()` → Splits into tokens

#### **2. Parsing Utilities**
Test parsing helpers.

**Test Scenarios:**
6. **Test 6**: `parse_url()` → Extracts protocol, host, path
7. **Test 7**: `parse_json_field()` → Extracts JSON value
8. **Test 8**: `parse_iso8601_date()` → Converts to timestamp
9. **Test 9**: `parse_boolean_string()` → "true"→true, "false"→false
10. **Test 10**: `parse_int_safe()` → Converts string to int safely

### **Mock Requirements**
- ✅ **No new mocks needed** (pure utility functions)

### **Code Changes Required**
```cpp
// rdkFwupdateMgr_handlers.h
#ifdef GTEST_ENABLE
char* trim_whitespace(char *str);
void safe_strncpy(char *dest, const char *src, size_t size);
// ... other utilities
#endif
```

### **Success Criteria**
- ✅ All 10 tests pass
- ✅ Utility functions 100% covered
- ✅ Edge cases handled (NULL, empty, overflow)
- ✅ Documentation complete

### **Estimated Effort**
- **Planning**: 0.5 hours
- **Implementation**: 3-4 hours
- **Testing & Validation**: 2 hours
- **Documentation**: 1 hour
- **Total**: 0.5-1 day

---

## 📊 Phase 1 Summary

### **Total Deliverables**
- **Tests**: 80 tests (10 per batch × 8 batches)
- **Coverage Gain**: ~20-25% (estimated)
- **Final Coverage**: 90-95% for `rdkFwupdateMgr_handlers.c`
- **Documentation**: 24 files (3 per batch × 8 batches)

### **Timeline**
- **Batch 1**: Complete ✅
- **Batches 2-8**: 1-2 days per batch
- **Total Duration**: 4-6 weeks

### **Next Steps After Phase 1**
1. ✅ Generate final coverage report
2. ✅ Review all documentation
3. ✅ Create Phase 1 completion summary
4. ✅ Plan Phase 2: Integration & Async Operations

---

## 🚀 How to Use This Document

### **For Developers**
1. Start with Batch 2 (Batch 1 complete)
2. Read batch plan carefully
3. Implement tests as specified
4. Run `./run_ut.sh` after each test
5. Create batch summary after completion
6. Move to next batch

### **For Reviewers**
1. Review plan before implementation starts
2. Check test implementation matches plan
3. Verify coverage gains meet estimates
4. Approve batch summary
5. Sign off before next batch

### **For Project Managers**
1. Track progress using batch status
2. Monitor coverage gains
3. Ensure timeline adherence
4. Review documentation completeness

---

**Document Version:** 1.0  
**Created:** December 25, 2025  
**Last Updated:** December 25, 2025  
**Next Review:** After Batch 2 completion  
**Maintained By:** RDK Firmware Update Team
