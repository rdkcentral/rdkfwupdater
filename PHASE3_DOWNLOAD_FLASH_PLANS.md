# 📥 Phase 3: Download & Flash Operations - Detailed Plans

**Project:** RDK Firmware Updater  
**Phase:** 3 - Download & Flash Operations  
**Goal:** Test firmware download and flash workflows (if in scope)  
**Prerequisite:** Phase 1 & 2 complete  
**Duration:** 2-3 weeks  
**Batches:** 4 batches × 10 tests each = 40 tests  
**Status:** ⚠️ **Conditional** - Depends on project scope  

---

## 📋 Table of Contents

1. [Phase Overview](#phase-overview)
2. [Scope Determination](#scope-determination)
3. [Batch 1: Download Handler Basics](#batch-1-download-handler-basics)
4. [Batch 2: Progress Monitoring](#batch-2-progress-monitoring)
5. [Batch 3: Flash Operations](#batch-3-flash-operations)
6. [Batch 4: Rollback & Recovery](#batch-4-rollback--recovery)
7. [Success Metrics](#success-metrics)

---

## 🎯 Phase Overview

### **Objective**
If firmware download and flash operations are in scope for unit testing:
- **Download Testing**: HTTP download, resume, verification
- **Progress Monitoring**: Progress callbacks, D-Bus signals
- **Flash Operations**: Flash process, verification
- **Recovery**: Rollback, error recovery

### **⚠️ Important Note**
This phase may be **OUT OF SCOPE** for unit testing because:
1. **Download operations** are typically integration/system tests
2. **Flash operations** require hardware/firmware access
3. **Mock complexity** may exceed usefulness

**Decision Point:** Determine scope before starting this phase.

### **Approach (If In Scope)**
- **Download**: Mock HTTP, real file I/O
- **Flash**: Mock hardware interface
- **Progress**: Test callback mechanisms
- **Recovery**: Test error scenarios

---

## 🔍 Scope Determination

### **Questions to Answer**

#### **1. Download Operations**
- ❓ Should we unit test `handle_download_firmware()`?
- ❓ Or are download operations covered by integration tests?
- ❓ Do we mock HTTP downloads or use real downloads?

**Recommendation:**
- ✅ **Unit Test**: Progress reporting, resume logic, verification
- ❌ **Integration Test**: Actual HTTP downloads, network failures

#### **2. Flash Operations**
- ❓ Can we unit test flash operations without hardware?
- ❓ Is there a flash simulator/mock?
- ❓ Are flash operations part of this daemon?

**Recommendation:**
- ✅ **Unit Test**: Flash preparation, pre-flight checks
- ❌ **Integration/Hardware Test**: Actual flashing

#### **3. Overall Decision**

| Scenario | Unit Test | Integration Test | System Test |
|----------|-----------|------------------|-------------|
| Download handler logic | ✅ Yes | | |
| Progress reporting | ✅ Yes | | |
| HTTP download | | ✅ Yes | |
| Flash pre-checks | ✅ Yes | | |
| Actual flashing | | | ✅ Yes |
| Rollback logic | ✅ Yes | ✅ Yes | |

---

## 📥 Batch 1: Download Handler Basics

**Status:** Planned (conditional on scope)  
**Focus:** Download initiation, validation, state management  
**Estimated Tests:** 10 tests  

### **Functions Under Test**

#### **1. `handle_download_firmware()`**
Main download handler.

**Test Scenarios:**
1. **Test 1**: Valid download request → Download initiated
2. **Test 2**: Download while CheckForUpdate in progress → Returns busy
3. **Test 3**: Download without prior CheckForUpdate → Returns error
4. **Test 4**: Invalid firmware URL → Returns error
5. **Test 5**: Insufficient disk space → Returns error

#### **2. `validate_download_request()`**
Pre-download validation.

**Test Scenarios:**
6. **Test 6**: Valid request → Returns true
7. **Test 7**: NULL parameters → Returns false
8. **Test 8**: Invalid URL format → Returns false
9. **Test 9**: Firmware already downloaded → Skips download
10. **Test 10**: Version mismatch → Returns error

### **Mock Strategy**
- **Mock**: HTTP client, filesystem space checks
- **Real**: Validation logic, state machine
- **Filesystem**: Use temp directory for testing

### **Test Structure**
```cpp
TEST_F(RdkFwupdateMgrDownloadTest, HandleDownloadFirmware_ValidRequest_InitiatesDownload)
{
    // Arrange
    SetupFirmwareInfo("2.0.0", "http://example.com/fw.bin");
    MockHttpDownload("fw.bin", 1024*1024); // 1MB file
    MockDiskSpace(10*1024*1024); // 10MB free
    
    // Act
    GVariant *result = handle_download_firmware(invocation, NULL);
    
    // Assert
    EXPECT_TRUE(result != NULL);
    EXPECT_EQ(GetResponseStatus(result), "downloading");
    EXPECT_TRUE(DownloadStarted());
}
```

### **Success Criteria**
- ✅ All 10 tests pass
- ✅ Download validation complete
- ✅ State transitions verified
- ✅ Documentation complete

### **Estimated Effort**
- **Planning**: 2 hours
- **Implementation**: 8-10 hours
- **Testing & Validation**: 4 hours
- **Documentation**: 2 hours
- **Total**: 2-3 days

---

## 📊 Batch 2: Progress Monitoring

**Status:** Planned (conditional)  
**Focus:** Download progress callbacks, D-Bus signals  
**Estimated Tests:** 10 tests  

### **Features Under Test**

#### **1. Progress Callbacks**
Monitor download progress.

**Test Scenarios:**
1. **Test 1**: Download starts → Progress = 0%
2. **Test 2**: Download 50% → Progress callback fired with 50%
3. **Test 3**: Download complete → Progress = 100%
4. **Test 4**: Multiple callbacks → Progress monotonically increasing
5. **Test 5**: Progress callback error → Download continues

#### **2. D-Bus Progress Signals**
Emit progress signals to clients.

**Test Scenarios:**
6. **Test 6**: Progress update → D-Bus signal emitted
7. **Test 7**: Multiple clients subscribed → All receive signals
8. **Test 8**: Client disconnected → Signals stop for that client
9. **Test 9**: Signal emission fails → Download continues
10. **Test 10**: High-frequency updates → Throttled (max 10/sec)

### **Mock Strategy**
- **Mock**: HTTP download (simulate progress), D-Bus signals
- **Real**: Progress calculation, callback invocation
- **Timing**: Use controlled time advancement

### **Test Structure**
```cpp
TEST_F(RdkFwupdateMgrDownloadTest, DownloadProgress_50Percent_SignalEmitted)
{
    // Arrange
    StartMockDownload(1024*1024); // 1MB file
    SubscribeToProgressSignals(client1);
    
    // Act
    SimulateDownloadProgress(512*1024); // 50% downloaded
    
    // Assert
    EXPECT_EQ(GetLastProgressSignal(), 50);
    EXPECT_TRUE(SignalEmittedToClient(client1));
}
```

### **Success Criteria**
- ✅ Progress monitoring accurate
- ✅ Signals emitted correctly
- ✅ Throttling works
- ✅ Documentation complete

### **Estimated Effort**
- **Planning**: 1-2 hours
- **Implementation**: 6-8 hours
- **Testing & Validation**: 3-4 hours
- **Documentation**: 1-2 hours
- **Total**: 2 days

---

## 🔧 Batch 3: Flash Operations

**Status:** Planned (conditional - likely OUT OF SCOPE)  
**Focus:** Flash preparation, validation  
**Estimated Tests:** 10 tests  
**⚠️ Warning:** May require hardware mocks  

### **Functions Under Test (If Applicable)**

#### **1. `handle_flash_firmware()`**
Flash operation handler.

**Test Scenarios:**
1. **Test 1**: Valid flash request → Flash initiated
2. **Test 2**: Flash without download → Returns error
3. **Test 3**: Downloaded file corrupt → Returns error
4. **Test 4**: Checksum mismatch → Returns error
5. **Test 5**: Flash already in progress → Returns busy

#### **2. `validate_flash_preconditions()`**
Pre-flash validation.

**Test Scenarios:**
6. **Test 6**: All preconditions met → Returns true
7. **Test 7**: Insufficient power → Returns error
8. **Test 8**: Write protection enabled → Returns error
9. **Test 9**: Backup failed → Returns error
10. **Test 10**: Flash partition unavailable → Returns error

### **Mock Strategy**
- **Mock**: Flash hardware interface, power management, filesystem
- **Real**: Validation logic, checksum calculation
- **⚠️ Challenge**: Hardware dependencies hard to mock

### **Recommendation**
**Consider moving to integration/system tests** instead of unit tests:
- Flash operations are hardware-dependent
- Mocking adds little value
- Integration tests more realistic

### **Alternative: Pre-Flight Checks Only**
Test only the validation logic, not actual flashing:
```cpp
TEST_F(RdkFwupdateMgrFlashTest, ValidateFlashPreconditions_AllMet_ReturnsTrue)
{
    // Arrange
    SetupValidDownloadedFirmware();
    MockPowerLevel(100); // Full power
    MockWriteProtection(false);
    
    // Act
    bool result = validate_flash_preconditions();
    
    // Assert
    EXPECT_TRUE(result);
}
```

### **Success Criteria (If Implemented)**
- ✅ Validation logic tested
- ✅ Pre-flight checks complete
- ✅ Documentation complete
- ⚠️ **Note**: Actual flash testing likely out of scope

### **Estimated Effort**
- **Planning**: 2-3 hours (decide scope)
- **Implementation**: 6-8 hours (validation only)
- **Testing & Validation**: 3 hours
- **Documentation**: 1-2 hours
- **Total**: 2 days (validation) or **SKIP** (full flash)

---

## 🔄 Batch 4: Rollback & Recovery

**Status:** Planned (conditional)  
**Focus:** Error recovery, rollback logic  
**Estimated Tests:** 10 tests  

### **Features Under Test**

#### **1. Download Resume**
Resume interrupted downloads.

**Test Scenarios:**
1. **Test 1**: Download interrupted → Resume from last position
2. **Test 2**: Partial file exists → Resume download
3. **Test 3**: Partial file corrupt → Restart download
4. **Test 4**: Resume after reboot → State restored
5. **Test 5**: Multiple resume attempts → Eventually succeeds/fails

#### **2. Error Recovery**
Handle download/flash errors.

**Test Scenarios:**
6. **Test 6**: Network error during download → Retry 3 times
7. **Test 7**: Disk full during download → Cleanup, return error
8. **Test 8**: Checksum fail → Delete file, return error
9. **Test 9**: Flash fails → Rollback to previous version
10. **Test 10**: Rollback fails → System in safe mode

### **Mock Strategy**
- **Mock**: Network, filesystem, flash hardware
- **Real**: Retry logic, state persistence
- **State**: Use persistent test state (filesystem)

### **Test Structure**
```cpp
TEST_F(RdkFwupdateMgrRecoveryTest, DownloadResume_PartialFile_ResumesFromLastPosition)
{
    // Arrange
    CreatePartialDownload(512*1024); // 512KB downloaded
    SetupResumeableDownload(1024*1024); // 1MB total
    
    // Act
    bool result = resume_download();
    
    // Assert
    EXPECT_TRUE(result);
    EXPECT_EQ(GetResumePosition(), 512*1024);
    EXPECT_EQ(GetDownloadedBytes(), 1024*1024);
}
```

### **Success Criteria**
- ✅ Resume logic tested
- ✅ Error recovery tested
- ✅ Rollback logic verified (if applicable)
- ✅ Documentation complete

### **Estimated Effort**
- **Planning**: 2 hours
- **Implementation**: 8-10 hours
- **Testing & Validation**: 4-5 hours
- **Documentation**: 2 hours
- **Total**: 2-3 days

---

## 📊 Phase 3 Summary

### **Total Deliverables (If In Scope)**
- **Tests**: 40 tests (10 per batch × 4 batches)
- **Coverage Type**: Download/flash logic coverage
- **Focus**: Download, progress, validation, recovery
- **Documentation**: 12 files (3 per batch × 4 batches)

### **Timeline (If In Scope)**
- **Batch 1**: 2-3 days
- **Batch 2**: 2 days
- **Batch 3**: 2 days or **SKIP**
- **Batch 4**: 2-3 days
- **Total Duration**: 2-3 weeks (or less if some batches skipped)

### **⚠️ Scope Decision Required**

**Option A: Full Phase 3** (All 4 batches)
- **Pros**: Complete coverage of download/flash
- **Cons**: High mock complexity, limited value for hardware ops

**Option B: Partial Phase 3** (Batches 1-2 only)
- **Pros**: Test download logic, skip hardware
- **Cons**: Incomplete coverage

**Option C: Skip Phase 3** (Move to Phase 4)
- **Pros**: Focus on achievable unit tests
- **Cons**: Download/flash tested only in integration

**Recommendation:** **Option B or C**
- Unit test download logic and progress (Batches 1-2)
- Move flash operations to integration/system tests
- Proceed to Phase 4: Code Quality

---

## 🚀 How to Use This Document

### **For Developers**
1. **Wait for scope decision** (consult team/architect)
2. If approved, start with Batch 1
3. Focus on logic, not hardware
4. Heavily mock hardware dependencies
5. Consider integration tests for actual operations

### **For Reviewers**
1. **Review scope first** before approving phase
2. Challenge hardware-dependent tests
3. Ensure mocks add value
4. Consider alternative test strategies

### **For Project Managers**
1. **Decide scope** early in Phase 3
2. If skipped, document rationale
3. Ensure integration tests cover skipped areas
4. Update master plan with decision

---

## ⚠️ Known Challenges

### **Hardware Dependencies**
- **Challenge**: Flash operations need hardware
- **Mitigation**: Mock hardware or skip flash tests

### **Network Complexity**
- **Challenge**: Download testing complex
- **Mitigation**: Mock HTTP, focus on logic

### **State Persistence**
- **Challenge**: Resume logic needs persistent state
- **Mitigation**: Use filesystem for test state

---

## 🔀 Alternative: Integration Testing

If Phase 3 unit testing proves too complex, consider:

### **Integration Test Suite**
- Real HTTP downloads (to test server)
- Real file I/O
- Mock flash hardware only
- Run in isolated test environment

### **Benefits**
- More realistic testing
- Less mock complexity
- Better coverage of real scenarios

### **Drawbacks**
- Slower tests
- Requires test infrastructure (HTTP server)
- Harder to reproduce edge cases

---

**Document Version:** 1.0  
**Created:** December 25, 2025  
**Last Updated:** December 25, 2025  
**Status:** ⚠️ **Scope Decision Pending**  
**Next Review:** Before Phase 3 starts  
**Maintained By:** RDK Firmware Update Team
