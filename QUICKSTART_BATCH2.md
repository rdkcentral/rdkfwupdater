# ⚡ Quick Start Guide - RDK Firmware Updater Unit Testing

**For:** Developers ready to continue the project  
**Status:** Phase 1 Batch 1 Complete ✅  
**Next:** Validate Batch 1 → Start Batch 2  
**Time Required:** 30 minutes validation + 1-2 days for Batch 2  

---

## 🎯 Current Status - At a Glance

```
✅ DONE:     Phase 1 Batch 1 (6 tests for fetch_xconf_firmware_info)
🔄 VALIDATE: Run tests, check coverage, review implementation
⏭️ NEXT:     Phase 1 Batch 2 (10 tests for cache helpers)
```

---

## 🚀 Immediate Actions (Do This Now!)

### **Step 1: Validate Batch 1 Tests** (10 minutes)

```bash
# Navigate to rdkfwupdater directory
cd c:\Users\mkn472\Desktop\SRC_CODE_RDKFW_UPGRADER\DEAMONIZATION\rdkfwupdater

# Run all unit tests
./run_ut.sh
```

**Expected Output:**
```
[==========] Running 49 tests from X test suites.
[----------] Global test environment set-up.
...
[  PASSED  ] 49 tests.
[==========] 49 tests from X test suites ran. (XXX ms total)
```

**✅ Success Criteria:**
- All tests pass (49/49)
- New tests visible in output:
  - `FetchXconfFirmwareInfo_UrlAllocationFails_ReturnsError`
  - `FetchXconfFirmwareInfo_HttpGetSuccess_ReturnsTrue`
  - `FetchXconfFirmwareInfo_HttpGetFails_ReturnsFalse`
  - `FetchXconfFirmwareInfo_HttpGet404_ReturnsFalse`
  - `FetchXconfFirmwareInfo_ParseError_ReturnsFalse`
  - `FetchXconfFirmwareInfo_CacheSaveSuccess_SavesCacheFile`
- No compilation errors
- No segfaults

**❌ If Tests Fail:**
1. Note the failing test names
2. Check error messages
3. Report findings
4. We'll fix before proceeding

---

### **Step 2: Generate Coverage Report** (5 minutes)

```bash
# Generate coverage (if coverage tools installed)
./run_ut.sh --coverage

# Or manually with gcov/lcov
make coverage

# View report
firefox coverage/index.html   # Linux
open coverage/index.html      # macOS
start coverage/index.html     # Windows
```

**What to Check:**
- Navigate to `rdkFwupdateMgr_handlers.c`
- Find `fetch_xconf_firmware_info()` function
- Check line coverage: **Target 85-90%**
- Overall file coverage: **Should increase by ~5%**

**📊 Expected Results:**
- `fetch_xconf_firmware_info()`: 85-90% line coverage
- `rdkFwupdateMgr_handlers.c`: 82-87% overall (up from ~80%)
- All testable paths covered (allocation, HTTP, parse, cache)

**⚠️ If Coverage Below Target:**
- List uncovered lines
- We'll add supplementary tests

---

### **Step 3: Quick Review** (5 minutes)

```bash
# Review what was implemented
cat BATCH_P1_B1_IMPLEMENTATION_SUMMARY.md

# See quick reference
cat BATCH_P1_B1_QUICK_REFERENCE.md

# Check code changes
git log --oneline -1
git diff HEAD~1 --stat
```

---

### **Step 4: Decision Point** (5 minutes)

**✅ All Tests Pass + Coverage Good?**
→ **Proceed to Batch 2** (see below)

**❌ Tests Fail or Coverage Low?**
→ **Report issues, we'll fix first**

**📝 Suggestions/Improvements?**
→ **Document and discuss**

---

## ⏭️ Next: Phase 1 Batch 2 - Cache Helpers

### **Quick Overview**

**Functions to Test:**
- `load_xconf_from_cache()` - Load cached firmware info
- `save_xconf_to_cache()` - Save firmware info to cache

**Test Count:** 10 tests  
**Estimated Time:** 1-2 days  
**Coverage Goal:** +3% (total ~85-88%)  

### **Test Scenarios (Summary)**

1. Cache file exists and valid → Returns cached data ✅
2. Cache file missing → Returns NULL ✅
3. Cache file corrupt → Returns NULL ✅
4. Cache file expired → Returns NULL ✅
5. Cache file unreadable → Returns NULL ✅
6. Valid data + write permissions → Cache file created ✅
7. NULL firmware_info → Returns false ✅
8. Write permission denied → Returns false ✅
9. Disk full → Returns false ✅
10. Cache directory missing → Creates directory + file ✅

**Detailed Plan:** See [`PHASE1_BATCH_PLANS.md`](./PHASE1_BATCH_PLANS.md#batch-2-cache-helper-functions)

---

## 📚 Essential Reading

### **Must Read Before Starting Batch 2** (15 minutes)

1. **Batch 2 Plan**
   ```bash
   cat PHASE1_BATCH_PLANS.md | grep -A 100 "Batch 2: Cache Helper Functions"
   ```
   - Read all test scenarios
   - Understand mock requirements
   - Review code changes needed

2. **Master Plan**
   ```bash
   cat UNITTEST_MASTER_PLAN.md
   ```
   - Understand overall strategy
   - Review quality gates
   - Check documentation requirements

3. **Complete Roadmap**
   ```bash
   cat COMPLETE_PROJECT_ROADMAP.md
   ```
   - See big picture
   - Understand all phases
   - Review success metrics

---

## 🛠️ Implementing Batch 2 - Step by Step

### **Step 1: Expose Cache Functions** (15 minutes)

**Edit:** `src/dbus/rdkFwupdateMgr_handlers.c`

**Find these functions and expose them:**

```cpp
// Before (static)
static bool load_xconf_from_cache(firmware_info_t *firmware_info)
{
    // ... existing code ...
}

static bool save_xconf_to_cache(const firmware_info_t *firmware_info)
{
    // ... existing code ...
}

// After (exposed for testing)
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

**Edit:** `src/dbus/rdkFwupdateMgr_handlers.h`

**Add declarations under GTEST_ENABLE:**

```cpp
#ifdef GTEST_ENABLE
// Exposed for unit testing - cache helpers
bool load_xconf_from_cache(firmware_info_t *firmware_info);
bool save_xconf_to_cache(const firmware_info_t *firmware_info);
#endif
```

---

### **Step 2: Implement Tests** (4-6 hours)

**Edit:** `unittest/rdkFwupdateMgr_handlers_gtest.cpp`

**Add 10 tests following this template:**

```cpp
//=============================================================================
// Phase 1 Batch 2: Cache Helper Functions Tests
//=============================================================================

// Test 1: Valid cache file
TEST_F(RdkFwupdateMgrHandlersTest, LoadXconfFromCache_ValidCache_ReturnsData)
{
    // Arrange
    firmware_info_t expected_info = {
        .version = "1.0.0",
        .url = "http://example.com/fw.bin",
        // ... other fields
    };
    CreateValidCacheFile(expected_info);
    
    firmware_info_t actual_info;
    memset(&actual_info, 0, sizeof(actual_info));
    
    // Act
    bool result = load_xconf_from_cache(&actual_info);
    
    // Assert
    EXPECT_TRUE(result);
    EXPECT_STREQ(actual_info.version, "1.0.0");
    EXPECT_STREQ(actual_info.url, "http://example.com/fw.bin");
    
    // Cleanup
    free_firmware_info(&actual_info);
}

// Test 2: Missing cache file
TEST_F(RdkFwupdateMgrHandlersTest, LoadXconfFromCache_MissingCache_ReturnsNull)
{
    // Arrange
    RemoveCacheFile();
    firmware_info_t info;
    memset(&info, 0, sizeof(info));
    
    // Act
    bool result = load_xconf_from_cache(&info);
    
    // Assert
    EXPECT_FALSE(result);
}

// ... 8 more tests (follow pattern from Batch 1)
```

**Implementation Tips:**
- **Reuse Patterns**: Copy test structure from Batch 1 tests
- **Helper Functions**: Create `CreateValidCacheFile()`, `RemoveCacheFile()` helpers
- **Temp Directory**: Use temp directory pattern from Batch 1
- **Cleanup**: Always clean up after each test (in TearDown)

---

### **Step 3: Compile and Test Iteratively** (2-3 hours)

**After Each Test:**

```bash
# Rebuild
make clean && make

# Run tests
./run_ut.sh

# Or run specific test
./unittest/rdkFwupdateMgr_gtest --gtest_filter="*LoadXconfFromCache*"
```

**Fix any compilation errors immediately!**

---

### **Step 4: Validate Batch 2** (1 hour)

**Run Full Test Suite:**
```bash
./run_ut.sh
```

**Expected:**
- All 59 tests pass (49 old + 10 new)
- No errors or warnings
- Tests complete in reasonable time

**Generate Coverage:**
```bash
./run_ut.sh --coverage
firefox coverage/index.html
```

**Check:**
- `load_xconf_from_cache()`: 90%+ coverage
- `save_xconf_to_cache()`: 90%+ coverage
- Overall file: ~85-88% (up from ~82-87%)

---

### **Step 5: Document Batch 2** (1-2 hours)

**Create:** `BATCH_P1_B2_IMPLEMENTATION_SUMMARY.md`

```markdown
# Phase 1 Batch 2: Cache Helper Functions - Implementation Summary

**Status:** Complete ✅  
**Tests Implemented:** 10  
**Coverage Gain:** ~3% (82% → 85%)  
**Date:** [Current Date]  

## Tests Implemented
1. LoadXconfFromCache_ValidCache_ReturnsData ✅
2. LoadXconfFromCache_MissingCache_ReturnsNull ✅
... (list all 10)

## Coverage Results
- `load_xconf_from_cache()`: 92% (target: 90%)
- `save_xconf_to_cache()`: 94% (target: 90%)
- Overall: 85% (up from 82%)

## Issues Encountered
(List any issues and how they were resolved)

## Next Steps
- Proceed to Batch 3: Response Builders
```

**Create:** `BATCH_P1_B2_QUICK_REFERENCE.md`

```markdown
# Batch 2 Quick Reference

## Functions Tested
- load_xconf_from_cache()
- save_xconf_to_cache()

## Test Count: 10

## Run Tests
```bash
./run_ut.sh --gtest_filter="*Cache*"
```

## Coverage: 85% (+3%)
```

---

## 📋 Batch 2 Checklist

Use this checklist while implementing:

### **Before Starting**
- [ ] Batch 1 validated (all tests pass)
- [ ] Coverage report reviewed
- [ ] Batch 2 plan read and understood
- [ ] Development environment ready

### **Code Changes**
- [ ] `rdkFwupdateMgr_handlers.c`: Exposed `load_xconf_from_cache()`
- [ ] `rdkFwupdateMgr_handlers.c`: Exposed `save_xconf_to_cache()`
- [ ] `rdkFwupdateMgr_handlers.h`: Added function declarations
- [ ] No other unintended changes

### **Tests Implementation**
- [ ] Test 1: LoadXconfFromCache_ValidCache_ReturnsData
- [ ] Test 2: LoadXconfFromCache_MissingCache_ReturnsNull
- [ ] Test 3: LoadXconfFromCache_CorruptCache_ReturnsNull
- [ ] Test 4: LoadXconfFromCache_ExpiredCache_ReturnsNull
- [ ] Test 5: LoadXconfFromCache_UnreadableCache_ReturnsNull
- [ ] Test 6: SaveXconfToCache_ValidData_CacheCreated
- [ ] Test 7: SaveXconfToCache_NullInfo_ReturnsFalse
- [ ] Test 8: SaveXconfToCache_NoWritePermission_ReturnsFalse
- [ ] Test 9: SaveXconfToCache_DiskFull_ReturnsFalse
- [ ] Test 10: SaveXconfToCache_MissingDirectory_CreatesDir

### **Validation**
- [ ] All tests compile without errors
- [ ] All tests pass (59/59)
- [ ] No warnings during compilation
- [ ] Coverage meets target (90%+ for cache functions)
- [ ] No memory leaks (if valgrind available)

### **Documentation**
- [ ] `BATCH_P1_B2_IMPLEMENTATION_SUMMARY.md` created
- [ ] `BATCH_P1_B2_QUICK_REFERENCE.md` created
- [ ] Test results documented
- [ ] Coverage results documented
- [ ] Issues/learnings documented

### **Commit**
- [ ] Code changes committed
- [ ] Documentation committed
- [ ] Clear commit message
- [ ] All files added to git

---

## 🎯 Success Criteria

### **Batch 2 Complete When:**
- ✅ All 10 tests implemented
- ✅ All tests pass via `./run_ut.sh`
- ✅ Coverage: 90%+ for cache functions, 85-88% overall
- ✅ Documentation complete
- ✅ No compilation warnings
- ✅ Ready to proceed to Batch 3

---

## 🆘 Troubleshooting

### **Tests Don't Compile**
```bash
# Check for typos in function names
# Verify #ifdef GTEST_ENABLE blocks are correct
# Ensure header file updated
# Check for missing includes
```

### **Tests Fail**
```bash
# Run with verbose output
./unittest/rdkFwupdateMgr_gtest --gtest_filter="*YourTest*" --gtest_verbose

# Check test assumptions
# Verify mock setup
# Check file paths (temp directory?)
# Review error messages carefully
```

### **Coverage Too Low**
```bash
# Check coverage report for uncovered lines
# Add tests for uncovered branches
# Verify error paths are tested
# Check if some lines are unreachable
```

---

## 📞 Need Help?

### **Resources**
- **Batch 2 Plan**: `PHASE1_BATCH_PLANS.md` (section "Batch 2")
- **Master Plan**: `UNITTEST_MASTER_PLAN.md`
- **Complete Roadmap**: `COMPLETE_PROJECT_ROADMAP.md`
- **Batch 1 Reference**: `BATCH_P1_B1_IMPLEMENTATION_SUMMARY.md`

### **Commands Reference**
```bash
# Build
make clean && make

# Run all tests
./run_ut.sh

# Run specific test
./unittest/rdkFwupdateMgr_gtest --gtest_filter="*Cache*"

# Coverage
./run_ut.sh --coverage

# Valgrind (if available)
valgrind ./unittest/rdkFwupdateMgr_gtest
```

---

## 🎉 After Batch 2

**When Batch 2 is complete:**
1. ✅ Celebrate! 2 batches done, 6 to go in Phase 1
2. ✅ Update `UNITTEST_MASTER_PLAN.md` progress
3. ✅ Commit everything
4. ✅ Review Batch 3 plan
5. ✅ Start Batch 3 when ready

**Progress After Batch 2:**
- **Coverage**: ~85-88% (target: 90-95%)
- **Tests**: 59/~120 (49%)
- **Batches**: 2/8 Phase 1 (25%)
- **Remaining**: 6 batches in Phase 1

---

**Good luck with Batch 2! 🚀**

---

**Document Version:** 1.0  
**Created:** December 25, 2025  
**For:** Immediate next steps  
**Next Update:** After Batch 2 completion
