# 🔗 Phase 2: Integration & Async Operations - Detailed Plans

**Project:** RDK Firmware Updater  
**Phase:** 2 - Integration & Async Operations  
**Goal:** Test multi-function workflows and async operations  
**Prerequisite:** Phase 1 complete (90-95% coverage of individual functions)  
**Duration:** 2-3 weeks  
**Batches:** 4 batches × 10 tests each = 40 tests  

---

## 📋 Table of Contents

1. [Phase Overview](#phase-overview)
2. [Batch 1: CheckForUpdate Full Workflow](#batch-1-checkforupdate-full-workflow)
3. [Batch 2: Cache + XConf Integration](#batch-2-cache--xconf-integration)
4. [Batch 3: Async Task Coordination](#batch-3-async-task-coordination)
5. [Batch 4: Multi-Client Scenarios](#batch-4-multi-client-scenarios)
6. [Success Metrics](#success-metrics)

---

## 🎯 Phase Overview

### **Objective**
After achieving high coverage of individual functions in Phase 1, Phase 2 focuses on:
- **Integration Testing**: How functions work together
- **Workflow Testing**: End-to-end scenarios
- **Async Operations**: Background tasks, timers, callbacks
- **State Management**: State transitions across operations

### **Approach**
- **Focus**: Function interactions, not individual functions
- **Strategy**: Test realistic workflows from start to finish
- **Quality**: Verify state consistency across operations
- **Integration**: Tests must pass via `./run_ut.sh`

### **Difference from Phase 1**
| Aspect | Phase 1 | Phase 2 |
|--------|---------|---------|
| **Scope** | Individual functions | Multi-function workflows |
| **Mocking** | Heavy mocking | Less mocking, more real flow |
| **Coverage** | Line/branch coverage | Integration coverage |
| **Duration** | 1-2 days per batch | 2-3 days per batch |

### **Success Metrics**
- ✅ All workflows execute correctly
- ✅ State transitions verified
- ✅ Async operations don't leak/hang
- ✅ Multi-client scenarios handled
- ✅ Coverage maintained at 90-95%

---

## 🔄 Batch 1: CheckForUpdate Full Workflow

**Status:** Planned (after Phase 1 complete)  
**Focus:** End-to-end CheckForUpdate from D-Bus call to response  
**Estimated Tests:** 10 tests  
**Estimated Coverage Gain:** Integration coverage (not line coverage)  

### **Workflow Under Test**

```
D-Bus Method Call
    ↓
handle_checkforupdate()
    ↓
Check current state (busy?)
    ↓
Fetch from XConf (network)
    ↓
Parse response (JSON)
    ↓
Compare versions (logic)
    ↓
Save to cache (filesystem)
    ↓
Build response (D-Bus)
    ↓
Emit signal (if update available)
    ↓
Return to caller
```

### **Test Scenarios**

#### **Happy Path Workflows**
1. **Test 1**: Full workflow, update available → Signal emitted, cache saved
2. **Test 2**: Full workflow, no update available → No signal, cache saved
3. **Test 3**: Full workflow, same version → Status "current", cache updated

#### **Cache-First Workflows**
4. **Test 4**: Valid cache exists → XConf not called, cache used
5. **Test 5**: Expired cache → XConf called, cache refreshed
6. **Test 6**: Force flag set → Cache ignored, XConf called

#### **Error Recovery Workflows**
7. **Test 7**: XConf fails → Falls back to cache (if available)
8. **Test 8**: XConf fails, no cache → Returns error
9. **Test 9**: Network timeout → Proper timeout error
10. **Test 10**: Parse fails → Error reported, old cache retained

### **Mock Strategy**
- **Minimal Mocking**: Let real functions run
- **Mock Only**: Network (HTTP), D-Bus infrastructure
- **Real**: Cache I/O, parsing, version comparison

### **Test Structure**
```cpp
TEST_F(RdkFwupdateMgrIntegrationTest, CheckForUpdate_FullWorkflow_UpdateAvailable)
{
    // Arrange
    SetupMockXConfResponse("2.0.0", "http://example.com/fw.bin");
    SetCurrentVersion("1.0.0");
    ClearCache();
    
    // Act
    GVariant *result = handle_checkforupdate(invocation, NULL);
    
    // Assert
    EXPECT_TRUE(result != NULL);
    EXPECT_EQ(GetResponseStatus(result), "updateAvailable");
    EXPECT_TRUE(CacheFileExists());
    EXPECT_TRUE(SignalWasEmitted());
}
```

### **Success Criteria**
- ✅ All 10 workflow tests pass
- ✅ No state leaks between tests
- ✅ Async operations complete properly
- ✅ Documentation complete

### **Estimated Effort**
- **Planning**: 2 hours
- **Implementation**: 8-10 hours
- **Testing & Validation**: 4-5 hours
- **Documentation**: 2 hours
- **Total**: 2-3 days

---

## 🗄️ Batch 2: Cache + XConf Integration

**Status:** Planned  
**Focus:** Interaction between cache and XConf fetching  
**Estimated Tests:** 10 tests  
**Estimated Coverage Gain:** Integration coverage  

### **Integration Points**

```
CheckForUpdate Called
    ↓
Check Cache (load_xconf_from_cache)
    ├─ Valid Cache?
    │   ├─ Yes → Use cached data
    │   └─ No → Fetch from XConf
    ↓
Fetch from XConf (fetch_xconf_firmware_info)
    ↓
Parse Response
    ↓
Save to Cache (save_xconf_to_cache)
    ↓
Return Result
```

### **Test Scenarios**

#### **Cache Hit Scenarios**
1. **Test 1**: Fresh cache → XConf not called
2. **Test 2**: Cache hit → Response time < 100ms (performance)
3. **Test 3**: Cache hit + force flag → Cache ignored, XConf called

#### **Cache Miss Scenarios**
4. **Test 4**: No cache file → XConf called, cache created
5. **Test 5**: Expired cache → XConf called, cache updated
6. **Test 6**: Corrupt cache → XConf called, cache overwritten

#### **Cache Update Scenarios**
7. **Test 7**: XConf success → Cache updated with new data
8. **Test 8**: XConf fails → Old cache retained
9. **Test 9**: XConf timeout → Cache marked as stale
10. **Test 10**: Disk full during save → Operation succeeds, cache update fails gracefully

### **Mock Strategy**
- **Mock**: HTTP requests to XConf
- **Real**: Cache I/O, file parsing
- **Filesystem**: Use temp directory

### **Test Structure**
```cpp
TEST_F(RdkFwupdateMgrIntegrationTest, CacheXConf_FreshCache_SkipsXConf)
{
    // Arrange
    CreateFreshCache("1.0.0", GetCurrentTime() - 60); // 1 min old
    MockXConfShouldNotBeCalled();
    
    // Act
    firmware_info_t info;
    bool result = checkforupdate_logic(&info);
    
    // Assert
    EXPECT_TRUE(result);
    EXPECT_EQ(info.version, "1.0.0");
    EXPECT_FALSE(XConfWasCalled());
}
```

### **Success Criteria**
- ✅ Cache logic works as expected
- ✅ Performance requirements met (cache hit < 100ms)
- ✅ Fallback scenarios tested
- ✅ Documentation complete

### **Estimated Effort**
- **Planning**: 1-2 hours
- **Implementation**: 6-8 hours
- **Testing & Validation**: 3-4 hours
- **Documentation**: 1-2 hours
- **Total**: 2 days

---

## ⏱️ Batch 3: Async Task Coordination

**Status:** Planned  
**Focus:** Background tasks, timers, callbacks  
**Estimated Tests:** 10 tests  
**Estimated Coverage Gain:** Async operation coverage  

### **Async Operations Under Test**

1. **Periodic Check Timer** (auto-update checks)
2. **Network Timeouts** (XConf request timeout)
3. **Download Progress Callbacks** (if in scope)
4. **Signal Emissions** (async D-Bus signals)
5. **State Machine Transitions** (busy → idle → downloading)

### **Test Scenarios**

#### **Timer Operations**
1. **Test 1**: Periodic check timer fires → CheckForUpdate executed
2. **Test 2**: Timer disabled → No automatic checks
3. **Test 3**: Timer interval change → New interval respected

#### **Timeout Handling**
4. **Test 4**: XConf timeout (30s) → Operation aborts
5. **Test 5**: Download timeout → Partial download cleaned up
6. **Test 6**: Multiple timeouts → No resource leaks

#### **Callback Chains**
7. **Test 7**: Async XConf fetch → Callback invoked with result
8. **Test 8**: Callback error → Error propagated correctly
9. **Test 9**: Callback during shutdown → Handled gracefully

#### **State Transitions**
10. **Test 10**: Idle → CheckingForUpdate → Idle transition verified

### **Mock Strategy**
- **Mock**: Timers (GLib main loop), network async calls
- **Real**: State machine, callbacks
- **Testing**: Use test main loop for controllable timing

### **Test Structure**
```cpp
TEST_F(RdkFwupdateMgrAsyncTest, PeriodicTimer_Fires_TriggersCheckForUpdate)
{
    // Arrange
    EnablePeriodicCheck(60); // 60 second interval
    MockXConfResponse("2.0.0");
    
    // Act
    AdvanceTime(60); // Simulate 60 seconds passing
    RunMainLoopIteration();
    
    // Assert
    EXPECT_TRUE(CheckForUpdateWasCalled());
    EXPECT_EQ(GetLastCheckTime(), GetCurrentTime());
}
```

### **Success Criteria**
- ✅ All async operations tested
- ✅ No timer leaks
- ✅ Timeouts work correctly
- ✅ State machine verified
- ✅ Documentation complete

### **Estimated Effort**
- **Planning**: 2-3 hours (async is complex)
- **Implementation**: 10-12 hours
- **Testing & Validation**: 5-6 hours
- **Documentation**: 2 hours
- **Total**: 3-4 days

---

## 👥 Batch 4: Multi-Client Scenarios

**Status:** Planned  
**Focus:** Concurrent D-Bus clients, request queuing  
**Estimated Tests:** 10 tests  
**Estimated Coverage Gain:** Concurrency coverage  

### **Scenarios Under Test**

#### **Concurrent CheckForUpdate**
1. **Test 1**: Two clients call CheckForUpdate simultaneously → Second queued/rejected
2. **Test 2**: First completes → Second proceeds
3. **Test 3**: First fails → Second proceeds

#### **State Conflicts**
4. **Test 4**: CheckForUpdate during download → Returns "busy"
5. **Test 5**: Download called during CheckForUpdate → Returns "busy"
6. **Test 6**: Abort called during CheckForUpdate → CheckForUpdate aborted

#### **Resource Sharing**
7. **Test 7**: Multiple clients read cache simultaneously → All succeed
8. **Test 8**: Client A updates cache, Client B reads → B sees new data
9. **Test 9**: Network resource busy → Requests serialized

#### **Edge Cases**
10. **Test 10**: Client disconnects during operation → Operation completes, resources cleaned

### **Mock Strategy**
- **Mock**: D-Bus infrastructure, multiple client connections
- **Real**: State machine, request queuing
- **Threading**: Use test synchronization primitives

### **Test Structure**
```cpp
TEST_F(RdkFwupdateMgrMultiClientTest, ConcurrentCheckForUpdate_SecondQueued)
{
    // Arrange
    MockXConfSlowResponse(5000); // 5 second delay
    
    // Act
    auto result1 = CallCheckForUpdateAsync(client1);
    usleep(100000); // 100ms delay
    auto result2 = CallCheckForUpdate(client2); // Synchronous
    
    // Assert
    EXPECT_EQ(result2.status, "busy");
    WaitForAsync(result1);
    EXPECT_EQ(result1.status, "updateAvailable");
}
```

### **Success Criteria**
- ✅ Concurrency handled correctly
- ✅ No race conditions
- ✅ No deadlocks
- ✅ Resources cleaned up
- ✅ Documentation complete

### **Estimated Effort**
- **Planning**: 2 hours
- **Implementation**: 8-10 hours
- **Testing & Validation**: 4-5 hours (concurrency is tricky)
- **Documentation**: 2 hours
- **Total**: 2-3 days

---

## 📊 Phase 2 Summary

### **Total Deliverables**
- **Tests**: 40 integration tests (10 per batch × 4 batches)
- **Coverage Type**: Integration coverage (not line coverage)
- **Focus**: Workflows, async, concurrency
- **Documentation**: 12 files (3 per batch × 4 batches)

### **Timeline**
- **Batch 1**: 2-3 days
- **Batch 2**: 2 days
- **Batch 3**: 3-4 days
- **Batch 4**: 2-3 days
- **Total Duration**: 2-3 weeks

### **Prerequisites**
- ✅ Phase 1 complete (90-95% function coverage)
- ✅ All Phase 1 tests passing
- ✅ Mock infrastructure stable

### **Success Metrics**
- ✅ All 40 tests pass
- ✅ No state leaks between tests
- ✅ No concurrency issues detected
- ✅ Integration coverage complete
- ✅ Documentation complete

### **Next Steps After Phase 2**
1. ✅ Validate all integration tests
2. ✅ Review async operation stability
3. ✅ Create Phase 2 completion summary
4. ✅ Plan Phase 3: Download & Flash Operations (if in scope)

---

## 🚀 How to Use This Document

### **For Developers**
1. **Wait for Phase 1 completion** (prerequisite)
2. Start with Batch 1 (CheckForUpdate full workflow)
3. Read batch plan carefully
4. Implement tests as specified
5. Pay extra attention to async/concurrency issues
6. Run `./run_ut.sh` after each test
7. Create batch summary after completion
8. Move to next batch

### **For Reviewers**
1. Review plan before implementation starts
2. Pay special attention to timing issues
3. Check for race conditions in tests
4. Verify state cleanup between tests
5. Approve batch summary
6. Sign off before next batch

### **For Project Managers**
1. Track progress using batch status
2. Monitor for concurrency issues
3. Ensure timeline adherence (longer than Phase 1)
4. Review documentation completeness

---

## ⚠️ Known Challenges

### **Async Testing Complexity**
- **Challenge**: Timing-dependent tests can be flaky
- **Mitigation**: Use deterministic time simulation, not real sleep

### **Concurrency Testing**
- **Challenge**: Race conditions hard to reproduce
- **Mitigation**: Use stress testing, run tests 100+ times

### **State Management**
- **Challenge**: Tests may interfere with each other
- **Mitigation**: Reset all global state between tests

---

**Document Version:** 1.0  
**Created:** December 25, 2025  
**Last Updated:** December 25, 2025  
**Next Review:** After Phase 1 completion  
**Maintained By:** RDK Firmware Update Team
