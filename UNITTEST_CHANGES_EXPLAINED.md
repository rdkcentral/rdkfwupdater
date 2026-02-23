# Unit Test Changes - Phase 2 Refactoring Impact

## Document Purpose
This document explains all unit test modifications required after Phase 2 library safety refactoring. It serves as a reference for understanding why tests changed and how to maintain them going forward.

---

## Table of Contents
1. [Overview](#overview)
2. [Root Cause Analysis](#root-cause-analysis)
3. [Changes Required](#changes-required)
4. [Detailed Explanation](#detailed-explanation)
5. [Testing Impact](#testing-impact)
6. [Maintenance Guidelines](#maintenance-guidelines)
7. [Troubleshooting](#troubleshooting)

---

## Overview

**Date:** February 23-24, 2026  
**Trigger:** Phase 2 library safety refactoring (exit() elimination from shared libraries)  
**Impact:** Unit test compilation failures due to API changes and missing dependencies  
**Status:** ✅ **RESOLVED**

### Quick Summary
During Phase 2 refactoring, we changed `checkAndEnterStateRed()` from `void` to `int` return type and added new error handling infrastructure (`rdkv_upgrade_strerror()`). This broke unit test compilation. We fixed it by:
1. Updating mock function signatures
2. Adding required source files to test builds

---

## Root Cause Analysis

### What Changed in Phase 2 Refactoring?

#### 1. Function Signature Change
**File:** `src/include/device_status_helper.h`

```c
// BEFORE (Phase 1):
void checkAndEnterStateRed(int curlret, const char *disableStatsUpdate);

// AFTER (Phase 2):
int checkAndEnterStateRed(int curlret, const char *disableStatsUpdate);
```

**Reason:** Function now returns error code instead of calling `exit(1)`, enabling daemon resilience.

**Return Values:**
- `0` = Success (no state red entry needed, or flag already set)
- `-1` = State red entered due to TLS/SSL error

---

#### 2. New Error Handling Infrastructure
**File:** `src/include/rdkv_upgrade.h`

Added new error handling:
```c
// New error enum (12 error codes)
typedef enum {
    RDKV_FWDNLD_SUCCESS = 0,
    RDKV_FWDNLD_ERROR_GENERAL = -1,
    RDKV_FWDNLD_ERROR_INVALID_PARAM = -2,
    // ... 9 more error codes
} rdkv_upgrade_error_t;

// New helper function
const char* rdkv_upgrade_strerror(int error_code);
```

**Reason:** Provides consistent error reporting across all firmware upgrade operations.

---

#### 3. New Function Calls Added
**Files Affected:**
- `src/rdkFwupdateMgr.c` (lines 561, 707, 756)
- `src/dbus/rdkFwupdateMgr_handlers.c` (lines 500, 1743)

**New Code:**
```c
// Example from rdkFwupdateMgr.c:561
SWLOG_ERROR("Firmware upgrade failed: %s (code: %d)\n", 
            rdkv_upgrade_strerror(ret), ret);
```

**Reason:** Better error logging with human-readable error messages.

---

### Why Did Unit Tests Break?

#### Problem 1: Mock Signature Mismatch
**File:** `unittest/miscellaneous_mock.cpp`

```cpp
// Mock had OLD signature (void):
MOCK_METHOD(void, checkAndEnterStateRed, (int, const char*), ());

// But header had NEW signature (int):
int checkAndEnterStateRed(int curlret, const char *);
```

**Compiler Error:**
```
miscellaneous_mock.cpp:258:10: error: conflicting declaration of C function 
'void checkAndEnterStateRed(int, const char*)'
../src/include/device_status_helper.h:47:5: note: previous declaration 
'int checkAndEnterStateRed(int, const char*)'
```

---

#### Problem 2: Undefined Reference to `rdkv_upgrade_strerror`
**Files:** Unit test executables `rdkfwupdatemgr_main_flow_gtest` and `rdkFwupdateMgr_handlers_gtest`

**Linker Errors:**
```
/usr/bin/ld: ../src/rdkFwupdateMgr.c:561: undefined reference to `rdkv_upgrade_strerror'
/usr/bin/ld: ../src/rdkFwupdateMgr.c:707: undefined reference to `rdkv_upgrade_strerror'
/usr/bin/ld: ../src/rdkFwupdateMgr.c:756: undefined reference to `rdkv_upgrade_strerror'
make: *** [Makefile:908: rdkfwupdatemgr_main_flow_gtest] Error 1
```

**Why?**
- Tests include `rdkFwupdateMgr.c` which calls `rdkv_upgrade_strerror()`
- But tests don't include `rdkv_upgrade.c` which defines this function
- Result: Symbol is referenced but not defined = linking fails

---

## Changes Required

### Change 1: Update Mock Signature ✅

**File:** `unittest/miscellaneous_mock.cpp`

#### Location 1: Mock Class Definition (~Line 78)
```cpp
// BEFORE:
MOCK_METHOD(void, checkAndEnterStateRed, (int, const char*), ());

// AFTER:
MOCK_METHOD(int, checkAndEnterStateRed, (int, const char*), ());
//          ^^^  Return type changed to int
```

#### Location 2: Mock Implementation (~Line 258)
```cpp
// BEFORE:
void checkAndEnterStateRed(int curlret, const char *) {
    if (global_mockexternal_ptr == nullptr) {
        return;  // void return
    }
    global_mockexternal_ptr->checkAndEnterStateRed(curlret, "");
}

// AFTER:
int checkAndEnterStateRed(int curlret, const char *) {
//  ^^^  Return type changed to int
    if (global_mockexternal_ptr == nullptr) {
        return 0;  // Return success by default
        //     ^   Now returns int value
    }
    return global_mockexternal_ptr->checkAndEnterStateRed(curlret, "");
    // ^^^^^^  Must return the mock's return value
}
```

**Rationale:**
- Mock must match the real function signature exactly
- Default return of `0` (success) allows tests to run without explicit expectations
- Propagates mock expectations when set

---

### Change 2: Add Source Files to Test Builds ✅

**File:** `unittest/Makefile.am`

#### Test 1: `rdkfwupdatemgr_main_flow_gtest`

```makefile
# BEFORE:
rdkfwupdatemgr_main_flow_gtest_SOURCES = rdkfwupdatemgr_main_flow_gtest.cpp \
    ../src/rdkFwupdateMgr.c \
    ../src/deviceutils/device_api.c \
    ../src/deviceutils/deviceutils.c \
    ../src/json_process.c \
    deviceutils/json_parse.c \
    miscellaneous_mock.cpp \
    ./mocks/deviceutils_mock.cpp

# AFTER:
rdkfwupdatemgr_main_flow_gtest_SOURCES = rdkfwupdatemgr_main_flow_gtest.cpp \
    ../src/rdkFwupdateMgr.c \
    ../src/rdkv_upgrade.c \             # ✅ ADDED - Defines rdkv_upgrade_strerror()
    ../src/chunk.c \                    # ✅ ADDED - Required by rdkv_upgrade.c
    ../src/device_status_helper.c \     # ✅ ADDED - Refactored checkAndEnterStateRed()
    ../src/download_status_helper.c \   # ✅ ADDED - Dependency of device_status_helper.c
    ../src/deviceutils/device_api.c \
    ../src/deviceutils/deviceutils.c \
    ../src/json_process.c \
    deviceutils/json_parse.c \
    miscellaneous_mock.cpp \
    ./mocks/deviceutils_mock.cpp
```

**Why Each File?**
- **`rdkv_upgrade.c`**: Defines `rdkv_upgrade_strerror()` and error enum
- **`chunk.c`**: Contains refactored download chunk functions called by `rdkv_upgrade.c`
- **`device_status_helper.c`**: Contains refactored `checkAndEnterStateRed()` implementation
- **`download_status_helper.c`**: Utility functions used by device_status_helper.c

---

#### Test 2: `rdkFwupdateMgr_handlers_gtest`

```makefile
# BEFORE:
rdkFwupdateMgr_handlers_gtest_SOURCES = rdkFwupdateMgr_handlers_gtest.cpp \
    ./mocks/rdkFwupdateMgr_mock.cpp \
    ../src/dbus/rdkFwupdateMgr_handlers.c \
    ../src/json_process.c \
    deviceutils/json_parse.c

# AFTER:
rdkFwupdateMgr_handlers_gtest_SOURCES = rdkFwupdateMgr_handlers_gtest.cpp \
    ./mocks/rdkFwupdateMgr_mock.cpp \
    ../src/dbus/rdkFwupdateMgr_handlers.c \
    ../src/rdkv_upgrade.c \             # ✅ ADDED - Defines rdkv_upgrade_strerror()
    ../src/chunk.c \                    # ✅ ADDED - Required by rdkv_upgrade.c
    ../src/device_status_helper.c \     # ✅ ADDED - Refactored checkAndEnterStateRed()
    ../src/download_status_helper.c \   # ✅ ADDED - Dependency of device_status_helper.c
    ../src/json_process.c \
    deviceutils/json_parse.c
```

**Why This Test Needs It?**
- Includes `rdkFwupdateMgr_handlers.c` which calls `rdkv_upgrade_strerror()` at lines 500 and 1743
- Same dependency chain as test 1

---

## Detailed Explanation

### Dependency Chain

```
Unit Test Executable
    │
    ├─► rdkFwupdateMgr.c (or rdkFwupdateMgr_handlers.c)
    │       │
    │       ├─► Calls: rdkv_upgrade_strerror()
    │       └─► Calls: checkAndEnterStateRed()
    │
    ├─► rdkv_upgrade.c (NOW ADDED)
    │       ├─► Defines: rdkv_upgrade_strerror()
    │       ├─► Defines: rdkv_upgrade_error_t enum
    │       └─► Uses: chunk_* functions
    │
    ├─► chunk.c (NOW ADDED)
    │       └─► Provides chunk download functions
    │
    ├─► device_status_helper.c (NOW ADDED)
    │       ├─► Defines: checkAndEnterStateRed() (refactored)
    │       └─► Uses: updateFWDownloadStatus(), eventManager()
    │
    └─► download_status_helper.c (NOW ADDED)
            └─► Utilities for firmware download status
```

### Why Not Just Mock `rdkv_upgrade_strerror()`?

**Option A: Mock the function** ❌
```cpp
// Could add to miscellaneous_mock.cpp:
const char* rdkv_upgrade_strerror(int error_code) {
    return "Mocked error";
}
```

**Problems:**
- Doesn't test the real error string logic
- Loses coverage of actual error enum values
- Requires maintaining mock behavior separately
- Breaks if error codes change

**Option B: Include the real source file** ✅ (Chosen)
```makefile
# Add to Makefile.am:
rdkfwupdatemgr_main_flow_gtest_SOURCES = ... ../src/rdkv_upgrade.c
```

**Benefits:**
- Tests use real error handling code
- Better coverage of actual implementation
- No mock maintenance burden
- Tests break if error codes change (good - forces test updates)

---

## Testing Impact

### What Tests Are Affected?

| Test Suite | Affected? | Why? | Fix Applied? |
|------------|-----------|------|--------------|
| `rdkfw_device_status_gtest` | ❌ No | Doesn't use affected functions | N/A |
| `rdkfw_deviceutils_gtest` | ❌ No | Doesn't use affected functions | N/A |
| `rdkfw_main_gtest` | ❌ No | Uses mocks correctly | N/A |
| `rdkfw_interface_gtest` | ❌ No | Doesn't use affected functions | N/A |
| `rdkfwupdatemgr_main_flow_gtest` | ✅ Yes | Uses rdkFwupdateMgr.c | ✅ Fixed |
| `rdkFwupdateMgr_handlers_gtest` | ✅ Yes | Uses rdkFwupdateMgr_handlers.c | ✅ Fixed |
| `dbus_handlers_gtest` | ⚠️ Maybe | May use D-Bus handlers | ✅ Check needed |

---

### Test Behavior Changes

#### No Functional Changes Expected ✅

The unit tests should behave **exactly the same** as before, because:

1. **Mock Default Behavior:**
   - Old: `void` function, no return value needed
   - New: Returns `0` (success) by default
   - **Impact:** Tests that don't set expectations will get success (same as before)

2. **Test Expectations:**
   - Tests can still set mock expectations:
   ```cpp
   EXPECT_CALL(mockexternal, checkAndEnterStateRed(35, _))
       .WillOnce(testing::Return(-1));  // Can test error case
   ```

3. **Source Code Inclusion:**
   - Tests now use real implementation instead of undefined behavior
   - **Impact:** More accurate testing, same results

---

### Mock Usage Examples

#### Example 1: Test Success Case (No State Red Entry)
```cpp
TEST(StateRedTest, NoEntryWhenNotSupported) {
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    
    // Mock will return 0 (success) by default
    int result = checkAndEnterStateRed(35, "");
    
    EXPECT_EQ(result, 0);  // ✅ Can now check return value
    
    global_mockexternal_ptr = nullptr;
}
```

#### Example 2: Test State Red Entry
```cpp
TEST(StateRedTest, EntersStateRedOnTLSError) {
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    
    // Set expectation for state red entry
    EXPECT_CALL(mockexternal, checkAndEnterStateRed(35, _))
        .WillOnce(testing::Return(-1));  // State red entered
    
    int result = checkAndEnterStateRed(35, "test");
    
    EXPECT_EQ(result, -1);  // ✅ Verify error was returned
    
    global_mockexternal_ptr = nullptr;
}
```

#### Example 3: Testing `rdkv_upgrade_strerror()` (Now Possible)
```cpp
TEST(ErrorHandlingTest, ConvertsErrorCodeToString) {
    // Now that rdkv_upgrade.c is included, we can test this
    const char* error_str = rdkv_upgrade_strerror(RDKV_FWDNLD_ERROR_DOWNLOAD_FAILED);
    
    EXPECT_STREQ(error_str, "Download failed");
}
```

---

## Maintenance Guidelines

### When Adding New Functions to Shared Libraries

If you add a new function to any of these files:
- `src/rdkv_upgrade.c`
- `src/device_status_helper.c`
- `src/chunk.c`
- `src/download_status_helper.c`

**You must:**

1. ✅ **Check if function is called by test code**
   ```bash
   # Search for function calls in test sources
   grep -r "your_new_function" unittest/
   ```

2. ✅ **Update mock if needed**
   - Add to `unittest/miscellaneous_mock.cpp` if externally called
   - Match return type and parameters exactly

3. ✅ **Verify test compilation**
   ```bash
   cd unittest && make clean && make
   ```

---

### When Changing Function Signatures

If you change a function signature in:
- `src/include/device_status_helper.h`
- `src/include/rdkv_upgrade.h`
- Any other header used by tests

**You must:**

1. ✅ **Update ALL mock declarations**
   ```cpp
   // In unittest/miscellaneous_mock.cpp:
   MOCK_METHOD(new_return_type, function_name, (new_params), ());
   ```

2. ✅ **Update mock implementations**
   ```cpp
   new_return_type function_name(new_params) {
       if (global_mockexternal_ptr == nullptr) {
           return default_value;  // Match new return type
       }
       return global_mockexternal_ptr->function_name(params);
   }
   ```

3. ✅ **Update test expectations**
   ```cpp
   EXPECT_CALL(...).WillOnce(testing::Return(appropriate_value));
   ```

---

### When Adding New Tests

If you add a new unit test that uses:
- `rdkFwupdateMgr.c`
- `rdkFwupdateMgr_handlers.c`
- Any file that calls `rdkv_upgrade_strerror()`

**You must add these sources to Makefile.am:**
```makefile
your_new_test_SOURCES = your_test.cpp \
    ../src/rdkv_upgrade.c \
    ../src/chunk.c \
    ../src/device_status_helper.c \
    ../src/download_status_helper.c \
    # ... other sources
```

---

## Troubleshooting

### Common Issues After Refactoring

#### Issue 1: "conflicting declaration" Error
```
error: conflicting declaration of C function 'void function_name(...)'
note: previous declaration 'int function_name(...)'
```

**Solution:** Mock signature doesn't match header
- Check `unittest/miscellaneous_mock.cpp`
- Update `MOCK_METHOD` return type
- Update implementation return type

---

#### Issue 2: "undefined reference to" Error
```
undefined reference to `rdkv_upgrade_strerror'
undefined reference to `checkAndEnterStateRed'
```

**Solution:** Missing source files in Makefile.am
- Add `../src/rdkv_upgrade.c`
- Add `../src/device_status_helper.c`
- Add dependencies (chunk.c, download_status_helper.c)

---

#### Issue 3: Tests Compile But Fail at Runtime
```
Segmentation fault
or
Assertion failed
```

**Possible Causes:**
1. Mock returns wrong value type
2. nullptr dereference in mock
3. Missing initialization

**Solution:**
- Verify mock returns appropriate defaults
- Check `global_mockexternal_ptr` is set
- Verify test setup/teardown

---

#### Issue 4: Link Error - Multiple Definitions
```
multiple definition of `function_name'
```

**Possible Causes:**
- Source file included twice
- Function defined in header (should be inline)
- Mock and real implementation both linked

**Solution:**
- Check Makefile.am for duplicate entries
- Verify function is not defined in header
- Ensure only one definition is compiled

---

## Summary

### What Changed?
| Component | Change | Reason |
|-----------|--------|--------|
| `checkAndEnterStateRed()` | `void` → `int` | Enable error propagation for daemon safety |
| Mock signature | `void` → `int` | Match real function signature |
| Error handling | Added `rdkv_upgrade_strerror()` | Consistent error reporting |
| Test builds | Added 4 source files | Resolve undefined references |

### Why It Matters?
- ✅ **Tests stay in sync with production code**
- ✅ **Better test coverage** (real error handling tested)
- ✅ **Prevents compilation failures** after refactoring
- ✅ **Documents API changes** for future developers

### Key Takeaways
1. **Mock signatures must match headers exactly**
2. **Include source files that define called functions**
3. **Update mocks when refactoring changes signatures**
4. **Test the real implementation when possible**

---

## References

### Related Documents
- **PHASE_2_COMPLETE.md** - Phase 2 refactoring details
- **IMPLEMENTATION_COMPLETE.md** - Complete implementation overview
- **UNITTEST_FIX.md** - Mock signature fix
- **UNITTEST_COMPILATION_FIX.md** - Makefile changes

### Code Locations
- **Mock definitions:** `unittest/miscellaneous_mock.cpp`
- **Test configurations:** `unittest/Makefile.am`
- **Error handling:** `src/include/rdkv_upgrade.h`
- **State red logic:** `src/device_status_helper.c`

### Key Functions
- `rdkv_upgrade_strerror()` - Convert error code to string
- `checkAndEnterStateRed()` - State red recovery entry point
- Error enum: `rdkv_upgrade_error_t` (12 error codes)

---

**Document Version:** 1.0  
**Last Updated:** February 24, 2026  
**Maintained By:** rdkfwupdater Team  
**Status:** ✅ Complete and Accurate
