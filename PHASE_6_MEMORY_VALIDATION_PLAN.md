# Phase 6: Memory Management Validation Plan

**Phase:** 6 - Memory Management Validation  
**Status:** IN PROGRESS  
**Started:** 2026-02-25  
**Objective:** Validate and stress-test memory management, ensure no leaks, corruption, or threading issues

---

## Overview

Phase 6 focuses on validating the memory management implementation from Phases 1-5. While the core implementation includes:
- Reference counting for callback contexts
- String memory management in signal parsing
- Cleanup on deinitialization

This phase will:
1. Create comprehensive stress tests
2. Add memory validation utilities
3. Run Valgrind and AddressSanitizer tests
4. Document error paths and edge cases
5. Create automation scripts for continuous validation

---

## Sub-Phase 6.1: Reference Counting Validation

**Status:** NOT STARTED  
**Objective:** Ensure reference counting is correct in all code paths

### Tasks

#### 6.1.1 Review Reference Counting Logic
- [x] Review `context_ref()` implementation in rdkFwupdateMgr_async.c
- [x] Review `context_unref()` implementation
- [ ] Trace all code paths where contexts are referenced
- [ ] Verify no double-free scenarios
- [ ] Verify no use-after-free scenarios

#### 6.1.2 Create Reference Counting Unit Tests
**File:** `unittest/rdkFwupdateMgr_async_refcount_gtest.cpp` (new)

Test cases:
- [ ] Initial reference count = 0
- [ ] context_ref() increments correctly
- [ ] context_unref() decrements correctly
- [ ] context_unref() at 0 does not underflow
- [ ] Concurrent ref/unref from multiple threads
- [ ] Verify cleanup called only when refcount reaches 0

#### 6.1.3 Static Analysis for Reference Issues
- [ ] Run Coverity on reference counting code
- [ ] Check for RESOURCE_LEAK warnings
- [ ] Check for USE_AFTER_FREE warnings
- [ ] Document any false positives

**Acceptance Criteria:**
- [ ] All reference counting paths verified
- [ ] Unit tests pass
- [ ] No Coverity issues
- [ ] Documentation updated

---

## Sub-Phase 6.2: Cleanup and Deinitialization

**Status:** NOT STARTED  
**Objective:** Ensure all resources freed on library shutdown

### Tasks

#### 6.2.1 Review Cleanup Code Paths
Files to review:
- `rdkFwupdateMgr_async.c` - `async_cleanup()`
- Background thread shutdown
- Registry cleanup

Verify:
- [ ] All WAITING callbacks are cancelled or completed
- [ ] All malloc'd strings are freed
- [ ] All pthread resources destroyed
- [ ] GLib resources (main loop, context) freed
- [ ] D-Bus connection closed properly

#### 6.2.2 Add Cleanup Unit Tests
**File:** `unittest/rdkFwupdateMgr_async_cleanup_gtest.cpp` (new)

Test cases:
- [ ] Cleanup with no active callbacks
- [ ] Cleanup with pending callbacks (should cancel them)
- [ ] Cleanup with completed callbacks
- [ ] Multiple init/cleanup cycles
- [ ] Cleanup while signal is being processed

#### 6.2.3 Valgrind Cleanup Test
Create test program that:
1. Initializes library
2. Registers multiple callbacks
3. Some complete, some cancelled, some timeout
4. Calls cleanup
5. Exits

Run under Valgrind:
```bash
valgrind --leak-check=full --show-leak-kinds=all \
         --track-origins=yes --verbose \
         ./test_async_cleanup
```

Expected result: 0 bytes leaked

**Acceptance Criteria:**
- [ ] All cleanup paths tested
- [ ] Valgrind shows 0 leaks
- [ ] No crashes during cleanup
- [ ] Can init/cleanup multiple times

---

## Sub-Phase 6.3: Signal Parsing Memory Management

**Status:** NOT STARTED  
**Objective:** Validate memory handling in signal parsing

### Tasks

#### 6.3.1 Review String Allocation in Signal Handler
File: `rdkFwupdateMgr_async.c` - `parse_signal_data()`

Verify:
- [ ] All strings are duplicated with strdup()
- [ ] NULL checks before strdup()
- [ ] Error paths free all allocated strings
- [ ] cleanup_update_info() frees all strings

#### 6.3.2 Create Signal Parsing Tests
**File:** `unittest/rdkFwupdateMgr_async_signal_gtest.cpp` (new)

Test cases:
- [ ] Parse valid signal with all fields
- [ ] Parse signal with NULL strings
- [ ] Parse signal with empty strings
- [ ] Parse signal with very long strings (1MB+)
- [ ] Rapid signal parsing (100+ signals)
- [ ] Memory usage stays constant after cleanup

#### 6.3.3 Inject Malformed Signals
Test error paths:
- [ ] Signal with wrong signature
- [ ] Signal with missing parameters
- [ ] Signal with NULL GVariant
- [ ] Signal with corrupted data

Verify:
- [ ] No crashes
- [ ] No memory leaks on error paths
- [ ] Proper error logging

**Acceptance Criteria:**
- [ ] All signal parsing paths tested
- [ ] No leaks in any code path
- [ ] Graceful handling of malformed data
- [ ] Valgrind clean

---

## Sub-Phase 6.4: Stress Testing

**Status:** NOT STARTED  
**Objective:** Test system under high load and concurrent access

### Task 6.4.1: Create Stress Test Suite
**File:** `unittest/rdkFwupdateMgr_async_stress_gtest.cpp` (new)

#### Test 1: Concurrent Registration
```cpp
// Register 1000 callbacks concurrently from 10 threads
// Verify all succeed or fail gracefully
```

#### Test 2: Rapid Register/Cancel
```cpp
// Register and immediately cancel 10000 callbacks
// Verify no memory leaks, no crashes
```

#### Test 3: Signal Flood
```cpp
// Simulate 1000 signals arriving rapidly
// Verify all callbacks invoked correctly
// Verify memory usage stable
```

#### Test 4: Registry Exhaustion
```cpp
// Register MAX_ASYNC_CALLBACKS callbacks
// Verify next registration fails gracefully
// Complete some, register more
// Verify registry slots are reused
```

#### Test 5: Long-Running Test
```cpp
// Run for 1 hour with random operations:
// - Register callbacks
// - Cancel some
// - Simulate signals
// - Check for leaks periodically
```

**Metrics to Track:**
- Memory usage (RSS, heap)
- CPU usage
- Number of active callbacks
- Success/failure rates
- Latency of callback invocation

**Acceptance Criteria:**
- [ ] All stress tests pass
- [ ] No memory growth over time
- [ ] No crashes or deadlocks
- [ ] Performance meets requirements (< 10ms callback latency)

---

## Sub-Phase 6.5: Thread Safety Validation

**Status:** NOT STARTED  
**Objective:** Ensure no race conditions or threading bugs

### Tasks

#### 6.5.1 Thread Sanitizer Testing
Build with ThreadSanitizer:
```bash
cmake -DCMAKE_C_FLAGS="-fsanitize=thread -g" \
      -DCMAKE_CXX_FLAGS="-fsanitize=thread -g" ..
make
./unittest/rdkFwupdateMgr_async_tests
```

Check for:
- [ ] Data races
- [ ] Lock order violations
- [ ] Deadlocks

#### 6.5.2 Helgrind Testing
```bash
valgrind --tool=helgrind ./test_async_api
```

Check for:
- [ ] Race conditions
- [ ] Lock/unlock mismatches
- [ ] Inconsistent lock ordering

#### 6.5.3 Concurrent Access Tests
**File:** `unittest/rdkFwupdateMgr_async_threading_gtest.cpp` (new)

Test cases:
- [ ] Multiple threads registering simultaneously
- [ ] Registration while signal handler runs
- [ ] Cancellation while signal handler runs
- [ ] Cleanup while callbacks are active
- [ ] Timeout checker runs while registration occurs

**Acceptance Criteria:**
- [ ] No TSan warnings
- [ ] No Helgrind warnings
- [ ] All concurrent tests pass
- [ ] No deadlocks observed

---

## Sub-Phase 6.6: Edge Cases and Error Paths

**Status:** NOT STARTED  
**Objective:** Test unusual scenarios and error conditions

### Test Cases

#### Initialization Errors
- [ ] Library not initialized before API call
- [ ] Double initialization
- [ ] Init failure (D-Bus unavailable)

#### Runtime Errors
- [ ] D-Bus connection lost during operation
- [ ] Daemon crashes mid-operation
- [ ] System out of memory (simulate with limits)
- [ ] Thread creation failure

#### Boundary Conditions
- [ ] Callback ID wraps around (UINT32_MAX → 1)
- [ ] Maximum callbacks registered
- [ ] Zero callbacks registered (signal arrives anyway)
- [ ] Very long timeouts (hours)
- [ ] Very short timeouts (< 1 second)

#### Callback Errors
- [ ] Callback function crashes (if possible to handle)
- [ ] Callback takes very long time (blocking)
- [ ] Callback registers new callback

**Acceptance Criteria:**
- [ ] All edge cases tested
- [ ] Graceful degradation on errors
- [ ] No undefined behavior
- [ ] Proper error reporting

---

## Sub-Phase 6.7: Memory Tools and Automation

**Status:** NOT STARTED  
**Objective:** Create automation scripts for continuous validation

### Task 6.7.1: Create Valgrind Test Script
**File:** `test/memory_validation.sh` (new)

```bash
#!/bin/bash
# Run all async tests under Valgrind

VALGRIND="valgrind --leak-check=full --show-leak-kinds=all \
          --track-origins=yes --error-exitcode=1"

echo "Running async unit tests..."
$VALGRIND ./unittest/rdkFwupdateMgr_async_tests || exit 1

echo "Running stress tests..."
$VALGRIND ./unittest/rdkFwupdateMgr_async_stress_tests || exit 1

echo "Running cleanup tests..."
$VALGRIND ./unittest/rdkFwupdateMgr_async_cleanup_tests || exit 1

echo "All memory tests passed!"
```

### Task 6.7.2: Create AddressSanitizer Build
**File:** `CMakeLists.txt` or `Makefile.am` (update)

Add option to build with ASan:
```cmake
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
if(ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address)
endif()
```

### Task 6.7.3: Create Memory Profiling Script
**File:** `test/memory_profile.sh` (new)

Use Massif for heap profiling:
```bash
valgrind --tool=massif --massif-out-file=massif.out ./test_async_api
ms_print massif.out > memory_profile.txt
```

### Task 6.7.4: Integrate into CI/CD
- [ ] Add memory tests to run_ut.sh
- [ ] Create nightly stress test job
- [ ] Set up leak detection in CI pipeline
- [ ] Configure alerts for new leaks

**Acceptance Criteria:**
- [ ] Automation scripts created
- [ ] ASan build working
- [ ] CI integration complete
- [ ] Documentation updated

---

## Sub-Phase 6.8: Documentation and Reporting

**Status:** NOT STARTED  
**Objective:** Document memory management and validation results

### Task 6.8.1: Create Memory Management Guide
**File:** `docs/ASYNC_MEMORY_MANAGEMENT.md` (new)

Content:
- Memory ownership rules
- Reference counting explanation
- String allocation policy
- Cleanup procedures
- Best practices for users

### Task 6.8.2: Document Validation Results
**File:** `PHASE_6_VALIDATION_RESULTS.md` (new)

Content:
- Test execution summary
- Valgrind results
- Stress test metrics
- Known issues (if any)
- Performance benchmarks

### Task 6.8.3: Update Implementation Plan
Update `IMPLEMENTATION_PLAN_CHECKFORUPDATE_ASYNC.md`:
- Mark Phase 6 tasks as complete
- Add links to new documentation
- Document any deviations from plan

**Acceptance Criteria:**
- [ ] All documentation complete
- [ ] Results documented
- [ ] Plan updated

---

## Phase 6 Completion Checklist

### Code Review
- [ ] All reference counting reviewed
- [ ] All cleanup paths verified
- [ ] All error paths tested
- [ ] Code coverage > 90%

### Memory Validation
- [ ] Valgrind: 0 leaks, 0 errors
- [ ] ASan: 0 errors
- [ ] Massif: no unbounded growth
- [ ] TSan: no races
- [ ] Helgrind: no threading issues

### Stress Testing
- [ ] Concurrent registration: PASS
- [ ] Rapid register/cancel: PASS
- [ ] Signal flood: PASS
- [ ] Registry exhaustion: PASS
- [ ] Long-running test: PASS (1+ hours)

### Edge Cases
- [ ] All initialization errors handled
- [ ] All runtime errors handled
- [ ] All boundary conditions tested
- [ ] All callback errors handled

### Documentation
- [ ] Memory management guide complete
- [ ] Validation results documented
- [ ] Implementation plan updated
- [ ] Code comments reviewed

### Integration
- [ ] CI/CD integration complete
- [ ] Automation scripts working
- [ ] Monitoring set up

---

## Success Criteria

Phase 6 is complete when:

1. ✅ **Zero Memory Leaks**: Valgrind reports 0 bytes leaked
2. ✅ **Zero Memory Errors**: ASan, Valgrind report no errors
3. ✅ **Zero Threading Issues**: TSan, Helgrind clean
4. ✅ **Stress Tests Pass**: All concurrent/load tests pass
5. ✅ **Edge Cases Handled**: All error paths tested
6. ✅ **Documentation Complete**: All guides written
7. ✅ **Automation Ready**: CI/CD integration working
8. ✅ **Code Review Approved**: Peer review complete

---

## Next Phase

After Phase 6 completion, proceed to:
- **Phase 7**: Error Handling Refinement
- **Phase 8**: Testing & Validation (integration with daemon)
- **Phase 9**: Documentation (API docs, migration guide)

---

## Notes for Future Developers

### Context Recovery
If you're picking up this work:

1. Read `IMPLEMENTATION_PLAN_CHECKFORUPDATE_ASYNC.md` for overall architecture
2. Read `PHASE_0_ANALYSIS_RESULTS.md` for daemon signal analysis
3. Review Phase 6 tasks in this document
4. Check which sub-phases are marked NOT STARTED
5. Run existing tests first to validate current state
6. Follow the task list sequentially

### Key Files
- Implementation: `librdkFwupdateMgr/src/rdkFwupdateMgr_async.c`
- Header: `librdkFwupdateMgr/src/rdkFwupdateMgr_async_internal.h`
- Tests: `unittest/rdkFwupdateMgr_async_*_gtest.cpp`
- Scripts: `test/memory_validation.sh`

### Tools Required
- Valgrind (memory leak detection)
- AddressSanitizer (memory error detection)
- ThreadSanitizer (race detection)
- Helgrind (threading issues)
- Google Test (unit tests)
- GLib 2.0 (event loop)
- D-Bus (IPC)

---

**Document Version:** 1.0  
**Last Updated:** 2026-02-25  
**Status:** Ready for implementation
