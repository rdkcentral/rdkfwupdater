# 🔍 Phase 4: Code Quality & Edge Cases - Detailed Plans

**Project:** RDK Firmware Updater  
**Phase:** 4 - Code Quality & Edge Cases  
**Goal:** Refactoring, stress testing, edge case coverage  
**Prerequisite:** Phase 1 & 2 complete (Phase 3 optional)  
**Duration:** 2 weeks  
**Batches:** 3 batches × 10 tests each = 30 tests  

---

## 📋 Table of Contents

1. [Phase Overview](#phase-overview)
2. [Batch 1: Memory Leak Testing](#batch-1-memory-leak-testing)
3. [Batch 2: Concurrency & Race Conditions](#batch-2-concurrency--race-conditions)
4. [Batch 3: Boundary Conditions & Stress Tests](#batch-3-boundary-conditions--stress-tests)
5. [Success Metrics](#success-metrics)

---

## 🎯 Phase Overview

### **Objective**
After achieving functional coverage in Phases 1-3, Phase 4 focuses on:
- **Memory Safety**: Leak detection, proper cleanup
- **Concurrency**: Race conditions, deadlocks
- **Stress Testing**: High load, edge cases
- **Code Quality**: Refactoring opportunities

### **Approach**
- **Focus**: Non-functional requirements
- **Strategy**: Stress, fuzz, boundary testing
- **Quality**: Run tests 1000+ times to catch rare issues
- **Tools**: Valgrind, ThreadSanitizer, AddressSanitizer

### **Difference from Previous Phases**
| Aspect | Phases 1-3 | Phase 4 |
|--------|------------|---------|
| **Scope** | Functional correctness | Non-functional quality |
| **Testing** | Normal scenarios | Edge cases, stress |
| **Tools** | GTest only | GTest + sanitizers |
| **Duration** | Short iterations | Long-running tests |

### **Success Metrics**
- ✅ Zero memory leaks (valgrind clean)
- ✅ No race conditions (TSan clean)
- ✅ No buffer overflows (ASan clean)
- ✅ Stress tests pass (1000+ iterations)
- ✅ Coverage maintained at 90-95%

---

## 💾 Batch 1: Memory Leak Testing

**Status:** Planned (after Phases 1-2 complete)  
**Focus:** Memory management, leak detection, cleanup  
**Estimated Tests:** 10 tests  
**Tools:** Valgrind, AddressSanitizer  

### **Testing Strategy**

#### **1. Leak Detection**
Run all existing tests under Valgrind to detect leaks.

**Test Approach:**
```bash
# Run all tests with Valgrind
valgrind --leak-check=full --show-leak-kinds=all \
         --track-origins=yes --verbose \
         ./unittest/rdkFwupdateMgr_gtest
```

**Expected Result:**
```
HEAP SUMMARY:
    in use at exit: 0 bytes in 0 blocks
  total heap usage: 1,234 allocs, 1,234 frees, 456,789 bytes allocated

All heap blocks were freed -- no leaks are possible
```

#### **2. Specific Leak Scenarios**
Create tests that specifically check cleanup.

**Test Scenarios:**
1. **Test 1**: Allocate firmware_info → Free → No leaks
2. **Test 2**: Allocate multiple infos → Free all → No leaks
3. **Test 3**: Error during allocation → Partial cleanup → No leaks
4. **Test 4**: Exception thrown → Cleanup via RAII/defer → No leaks
5. **Test 5**: Repeated alloc/free (10000 times) → No leaks

#### **3. Resource Cleanup**
Test that all resources are properly released.

**Test Scenarios:**
6. **Test 6**: Open file → Close → No FD leaks
7. **Test 7**: Open network connection → Close → No socket leaks
8. **Test 8**: Allocate D-Bus resources → Free → No D-Bus leaks
9. **Test 9**: Create threads → Join → No thread leaks
10. **Test 10**: Register callbacks → Unregister → No callback leaks

### **Test Structure**
```cpp
TEST_F(RdkFwupdateMgrMemoryTest, FirmwareInfo_AllocFree_NoLeaks)
{
    // Run this test 10000 times to detect leaks
    for (int i = 0; i < 10000; i++) {
        // Arrange
        firmware_info_t *info = allocate_firmware_info();
        info->version = strdup("1.0.0");
        info->url = strdup("http://example.com");
        
        // Act
        free_firmware_info(info);
        
        // Assert (implicit - valgrind checks for leaks)
    }
}

TEST_F(RdkFwupdateMgrMemoryTest, ErrorPath_PartialAlloc_NoLeaks)
{
    // Arrange
    MockMallocFailure(3); // Fail on 3rd malloc
    
    // Act
    firmware_info_t *info = fetch_xconf_firmware_info();
    
    // Assert
    EXPECT_EQ(info, nullptr);
    // Valgrind will verify no leaks from partial allocations
}
```

### **Tools & Configuration**

#### **Valgrind**
```bash
# Create valgrind suppression file for known false positives
cat > valgrind.supp << EOF
{
   GLib-GHashTable
   Memcheck:Leak
   ...
   fun:g_hash_table_new
}
EOF

# Run tests
valgrind --suppressions=valgrind.supp \
         --leak-check=full \
         ./unittest/rdkFwupdateMgr_gtest
```

#### **AddressSanitizer (ASan)**
```bash
# Compile with ASan
export CFLAGS="-fsanitize=address -g"
export CXXFLAGS="-fsanitize=address -g"
export LDFLAGS="-fsanitize=address"

# Rebuild and run
./autogen.sh && ./configure && make clean && make
./unittest/rdkFwupdateMgr_gtest
```

### **Success Criteria**
- ✅ Valgrind reports 0 leaks
- ✅ ASan reports no errors
- ✅ All cleanup functions tested
- ✅ Stress tests pass (10000+ iterations)
- ✅ Documentation complete

### **Estimated Effort**
- **Planning**: 1 hour
- **Implementation**: 6-8 hours
- **Testing & Validation**: 8-10 hours (valgrind is slow)
- **Documentation**: 2 hours
- **Total**: 2-3 days

---

## 🔄 Batch 2: Concurrency & Race Conditions

**Status:** Planned  
**Focus:** Thread safety, race conditions, deadlocks  
**Estimated Tests:** 10 tests  
**Tools:** ThreadSanitizer (TSan), stress testing  

### **Testing Strategy**

#### **1. Race Condition Detection**
Use ThreadSanitizer to detect data races.

**Test Approach:**
```bash
# Compile with TSan
export CFLAGS="-fsanitize=thread -g"
export CXXFLAGS="-fsanitize=thread -g"
export LDFLAGS="-fsanitize=thread"

# Rebuild and run
./autogen.sh && ./configure && make clean && make
./unittest/rdkFwupdateMgr_gtest
```

#### **2. Concurrent Access Tests**
Test scenarios with multiple threads accessing shared state.

**Test Scenarios:**
1. **Test 1**: Two threads call CheckForUpdate → Properly serialized
2. **Test 2**: Read cache while writing cache → No corruption
3. **Test 3**: Multiple threads read global state → No crashes
4. **Test 4**: Thread A updates state, Thread B reads → Consistent view
5. **Test 5**: 10 threads hammer API → No crashes, all succeed/queue

#### **3. Deadlock Detection**
Test for potential deadlocks.

**Test Scenarios:**
6. **Test 6**: Lock order test → No deadlocks
7. **Test 7**: Recursive lock test → Handled correctly
8. **Test 8**: Timeout on lock acquisition → Returns error, no hang
9. **Test 9**: Lock held during callback → No deadlock
10. **Test 10**: Shutdown while lock held → Proper cleanup

### **Test Structure**
```cpp
TEST_F(RdkFwupdateMgrConcurrencyTest, ConcurrentCheckForUpdate_MultipleThreads_NoRace)
{
    const int NUM_THREADS = 10;
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};
    std::atomic<int> busy_count{0};
    
    // Arrange
    MockXConfResponse("2.0.0");
    
    // Act - Launch 10 threads calling CheckForUpdate
    for (int i = 0; i < NUM_THREADS; i++) {
        threads.emplace_back([&]() {
            auto result = handle_checkforupdate(invocation, NULL);
            if (IsSuccess(result)) success_count++;
            else if (IsBusy(result)) busy_count++;
        });
    }
    
    // Wait for all threads
    for (auto &t : threads) t.join();
    
    // Assert
    EXPECT_EQ(success_count, 1); // Only one should succeed
    EXPECT_EQ(busy_count, NUM_THREADS - 1); // Others should get "busy"
    
    // TSan will report any races
}

TEST_F(RdkFwupdateMgrConcurrencyTest, CacheReadWrite_Concurrent_NoCorruption)
{
    std::atomic<bool> keep_running{true};
    std::atomic<int> read_count{0};
    std::atomic<int> write_count{0};
    
    // Writer thread
    std::thread writer([&]() {
        while (keep_running) {
            save_xconf_to_cache(&test_firmware_info);
            write_count++;
            usleep(10000); // 10ms
        }
    });
    
    // Reader threads (5 of them)
    std::vector<std::thread> readers;
    for (int i = 0; i < 5; i++) {
        readers.emplace_back([&]() {
            while (keep_running) {
                firmware_info_t info;
                if (load_xconf_from_cache(&info)) {
                    read_count++;
                }
                usleep(5000); // 5ms
            }
        });
    }
    
    // Run for 5 seconds
    sleep(5);
    keep_running = false;
    
    // Wait for all threads
    writer.join();
    for (auto &t : readers) t.join();
    
    // Assert
    EXPECT_GT(write_count, 0);
    EXPECT_GT(read_count, 0);
    
    // Verify cache is not corrupt
    firmware_info_t final_info;
    EXPECT_TRUE(load_xconf_from_cache(&final_info));
}
```

### **Tools & Configuration**

#### **ThreadSanitizer**
```bash
# Run with TSan
export TSAN_OPTIONS="halt_on_error=1 history_size=7"
./unittest/rdkFwupdateMgr_gtest
```

#### **Helgrind (Valgrind's thread checker)**
```bash
valgrind --tool=helgrind ./unittest/rdkFwupdateMgr_gtest
```

### **Success Criteria**
- ✅ TSan reports no races
- ✅ Helgrind reports no races
- ✅ No deadlocks detected
- ✅ Concurrent stress tests pass
- ✅ Documentation complete

### **Estimated Effort**
- **Planning**: 2 hours
- **Implementation**: 8-10 hours
- **Testing & Validation**: 10-12 hours (TSan is slow)
- **Documentation**: 2 hours
- **Total**: 3-4 days

---

## 🎯 Batch 3: Boundary Conditions & Stress Tests

**Status:** Planned  
**Focus:** Edge cases, stress testing, robustness  
**Estimated Tests:** 10 tests  
**Tools:** Custom stress test harness  

### **Testing Strategy**

#### **1. Boundary Value Testing**
Test edge values for all inputs.

**Test Scenarios:**
1. **Test 1**: Empty strings → Handled gracefully
2. **Test 2**: Very long strings (10000 chars) → Truncated/rejected
3. **Test 3**: NULL pointers → Error, no crash
4. **Test 4**: Integer overflow → Detected, error returned
5. **Test 5**: Float precision limits → Handled correctly

#### **2. Stress Testing**
Run operations repeatedly to find rare bugs.

**Test Scenarios:**
6. **Test 6**: CheckForUpdate 10000 times → No crashes, no leaks
7. **Test 7**: Allocate max memory → Handled gracefully
8. **Test 8**: Max file size download → Progress correct
9. **Test 9**: Rapid start/stop operations → Cleanup correct
10. **Test 10**: Fuzz test parsers → No crashes

### **Test Structure**

#### **Boundary Tests**
```cpp
TEST_F(RdkFwupdateMgrBoundaryTest, EmptyVersion_Handled)
{
    // Arrange
    firmware_info_t info;
    info.version = "";
    
    // Act
    bool valid = validate_firmware_info(&info);
    
    // Assert
    EXPECT_FALSE(valid);
}

TEST_F(RdkFwupdateMgrBoundaryTest, VeryLongUrl_Truncated)
{
    // Arrange
    std::string long_url(10000, 'a'); // 10000 char URL
    firmware_info_t info;
    info.url = long_url.c_str();
    
    // Act
    bool result = fetch_xconf_firmware_info();
    
    // Assert
    EXPECT_FALSE(result); // Should reject overly long URLs
}

TEST_F(RdkFwupdateMgrBoundaryTest, NullPointer_NoSegfault)
{
    // Act & Assert - should not crash
    EXPECT_FALSE(validate_firmware_info(NULL));
    EXPECT_EQ(fetch_xconf_firmware_info(NULL), false);
    free_firmware_info(NULL); // Should be safe
}
```

#### **Stress Tests**
```cpp
TEST_F(RdkFwupdateMgrStressTest, CheckForUpdate_10000Times_NoFailure)
{
    // Arrange
    MockXConfResponse("2.0.0");
    int success_count = 0;
    
    // Act
    for (int i = 0; i < 10000; i++) {
        ClearState(); // Reset state between calls
        auto result = handle_checkforupdate(invocation, NULL);
        if (IsSuccess(result)) success_count++;
        
        // Every 1000 iterations, check memory
        if (i % 1000 == 0) {
            EXPECT_LT(GetMemoryUsage(), 100*1024*1024); // <100MB
        }
    }
    
    // Assert
    EXPECT_EQ(success_count, 10000);
}

TEST_F(RdkFwupdateMgrStressTest, FuzzParser_RandomInput_NoCrash)
{
    // Act - Feed random data to JSON parser
    for (int i = 0; i < 1000; i++) {
        std::string random_json = GenerateRandomString(1024);
        
        // Should not crash, just return error
        firmware_info_t info;
        bool result = parse_xconf_response(random_json.c_str(), &info);
        
        // Most will fail, but none should crash
        // (ASan will catch any buffer overflows)
    }
}
```

#### **3. Soak Testing**
Run for extended periods.

**Test Approach:**
```cpp
TEST_F(RdkFwupdateMgrSoakTest, DISABLED_CheckForUpdate_24Hours_Stable)
{
    // This test runs for 24 hours, disabled by default
    // Enable manually: ./unittest --gtest_also_run_disabled_tests
    
    auto start = std::chrono::steady_clock::now();
    auto end = start + std::chrono::hours(24);
    int iterations = 0;
    
    while (std::chrono::steady_clock::now() < end) {
        // Run CheckForUpdate
        handle_checkforupdate(invocation, NULL);
        
        // Sleep 10 seconds between calls
        sleep(10);
        iterations++;
        
        // Log progress every hour
        if (iterations % 360 == 0) {
            LOG("Soak test: %d hours complete", iterations / 360);
        }
    }
    
    EXPECT_GT(iterations, 8640); // At least 24h worth of calls
}
```

### **Fuzz Testing**

#### **JSON Parser Fuzzing**
```cpp
TEST_F(RdkFwupdateMgrFuzzTest, JsonParser_Fuzz_NoCrash)
{
    // Test cases
    std::vector<std::string> fuzz_inputs = {
        "",                          // Empty
        "not json",                  // Invalid
        "{",                         // Incomplete
        "{}",                        // Empty object
        "{\"version\":null}",        // Null value
        "{\"version\":123}",         // Wrong type
        "{\"version\":\"" + std::string(10000, 'A') + "\"}", // Very long
        std::string(10000, '{'),     // Deep nesting
        "\x00\x01\x02",              // Binary data
        // ... more fuzz cases
    };
    
    for (const auto &input : fuzz_inputs) {
        firmware_info_t info;
        // Should not crash, just return false
        parse_xconf_response(input.c_str(), &info);
    }
}
```

### **Success Criteria**
- ✅ All boundary tests pass
- ✅ Stress tests pass (10000+ iterations)
- ✅ Fuzz tests pass (no crashes)
- ✅ Soak tests run successfully (if enabled)
- ✅ ASan reports no buffer overflows
- ✅ Documentation complete

### **Estimated Effort**
- **Planning**: 2 hours
- **Implementation**: 8-10 hours
- **Testing & Validation**: 12-16 hours (stress tests take time)
- **Documentation**: 2 hours
- **Total**: 3-4 days

---

## 📊 Phase 4 Summary

### **Total Deliverables**
- **Tests**: 30 tests (10 per batch × 3 batches)
- **Coverage Type**: Non-functional quality
- **Focus**: Memory, concurrency, stress
- **Tools**: Valgrind, TSan, ASan, fuzz testing
- **Documentation**: 9 files (3 per batch × 3 batches)

### **Timeline**
- **Batch 1**: 2-3 days (memory)
- **Batch 2**: 3-4 days (concurrency)
- **Batch 3**: 3-4 days (stress)
- **Total Duration**: 2 weeks

### **Prerequisites**
- ✅ Phase 1 complete (90-95% function coverage)
- ✅ Phase 2 complete (integration tested)
- ✅ All sanitizers installed (valgrind, TSan, ASan)

### **Success Metrics**
- ✅ Zero memory leaks
- ✅ Zero race conditions
- ✅ Zero buffer overflows
- ✅ All stress tests pass
- ✅ Code quality high

### **Next Steps After Phase 4**
1. ✅ Generate final quality report
2. ✅ Create project completion summary
3. ✅ Document best practices learned
4. ✅ Plan maintenance strategy

---

## 🚀 How to Use This Document

### **For Developers**
1. **Wait for Phases 1-2 completion** (prerequisite)
2. Start with Batch 1 (memory testing)
3. Install all sanitizers (valgrind, TSan, ASan)
4. Run tests with sanitizers enabled
5. Fix all issues before moving to next batch
6. Document findings

### **For Reviewers**
1. Review sanitizer reports carefully
2. Verify stress test results
3. Check for false positives in reports
4. Approve suppression files if needed
5. Sign off before next batch

### **For Project Managers**
1. Track quality metrics
2. Ensure sanitizer CI/CD integration
3. Monitor long-running tests
4. Review final quality report

---

## 🛠️ Tools Setup

### **Install Sanitizers**
```bash
# Ubuntu/Debian
sudo apt-get install valgrind

# Compiler support (GCC/Clang)
# ASan, TSan built into modern GCC/Clang
gcc --version  # Should be 7.0+
clang --version # Should be 5.0+
```

### **CI/CD Integration**
```yaml
# Example GitHub Actions
sanitizers:
  runs-on: ubuntu-latest
  steps:
    - name: Run with ASan
      run: |
        export CFLAGS="-fsanitize=address"
        ./autogen.sh && ./configure && make
        ./unittest/rdkFwupdateMgr_gtest
    
    - name: Run with TSan
      run: |
        export CFLAGS="-fsanitize=thread"
        make clean && make
        ./unittest/rdkFwupdateMgr_gtest
    
    - name: Run with Valgrind
      run: |
        valgrind --leak-check=full ./unittest/rdkFwupdateMgr_gtest
```

---

## ⚠️ Known Challenges

### **Sanitizer Performance**
- **Challenge**: Tests run 10-100x slower with sanitizers
- **Mitigation**: Run sanitizer tests in separate CI job

### **False Positives**
- **Challenge**: External libraries may report leaks
- **Mitigation**: Use suppression files

### **Concurrency Testing**
- **Challenge**: Race conditions hard to reproduce
- **Mitigation**: Run tests 1000+ times, use TSan

---

**Document Version:** 1.0  
**Created:** December 25, 2025  
**Last Updated:** December 25, 2025  
**Next Review:** After Phases 1-2 completion  
**Maintained By:** RDK Firmware Update Team
