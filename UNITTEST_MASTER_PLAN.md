# 🎯 RDK Firmware Updater - Unit Test Master Plan

**Project:** RDK Firmware Updater  
**Goal:** Achieve 90-95% unit test coverage for business logic  
**Approach:** Phased, batch-wise implementation (10 tests per batch)  
**Created:** December 25, 2025  
**Status:** Phase 1 Batch 1 Complete ✅

---

## 📋 Table of Contents

1. [Overview](#overview)
2. [Current Status](#current-status)
3. [Phase Structure](#phase-structure)
4. [Implementation Guidelines](#implementation-guidelines)
5. [Detailed Phase Plans](#detailed-phase-plans)
6. [Progress Tracking](#progress-tracking)

---

## 🎯 Overview

### **Objective**
Achieve 90-95% unit test coverage for the core business logic in:
- `rdkfwupdater/src/rdkFwupdateMgr.c` (1,283 lines)
- `rdkfwupdater/src/dbus/rdkFwupdateMgr_handlers.c` (1,957 lines)

### **Current Coverage**
- **rdkFwupdateMgr.c**: ~70-80% ✅
- **rdkFwupdateMgr_handlers.c**: ~80-85% ✅
- **Target**: 90-95% for both files

### **Strategy**
- **Iterative Process**: Plan → Review → Execute → Validate → Commit → Summarize
- **Batch-wise Implementation**: 10 tests per batch
- **No Duplication**: Avoid duplicating existing tests or mocks
- **Runnable Tests**: All tests must pass via `./run_ut.sh`

---

## 📊 Current Status

### **Completed** ✅
- ✅ **Phase 1, Batch 1**: `fetch_xconf_firmware_info()` tests (6 tests)
  - Status: Implementation complete, awaiting validation
  - Coverage: All testable paths for XConf fetching
  - Documentation: Full implementation summary created

### **In Progress** 🔄
- 🔄 **Phase 1, Batch 2**: Cache helper functions (Next)
  - Status: Planned, ready to implement after Batch 1 validation

### **Pending** ⏳
- Phase 1: Batches 3-8 (See detailed plans below)
- Phase 2: Integration & async operations
- Phase 3: Download & flash operations
- Phase 4: Code quality & edge cases

---

## 🏗️ Phase Structure

### **Phase 1: Business Logic Deep Coverage** 🎯
**Goal**: Increase coverage from ~80% to ~90-95%  
**Target**: `rdkFwupdateMgr_handlers.c` internal functions  
**Batches**: 8 batches, 10 tests each  
**Duration**: 4-6 weeks  

### **Phase 2: Integration & Async Operations** 🔗
**Goal**: Test multi-function workflows and async operations  
**Target**: Function interactions, state transitions  
**Batches**: 4 batches, 10 tests each  
**Duration**: 2-3 weeks  

### **Phase 3: Download & Flash Operations** 📥
**Goal**: Test download and flash workflows (if in scope)  
**Target**: `handle_download_firmware()`, flash operations  
**Batches**: 4 batches, 10 tests each  
**Duration**: 2-3 weeks  

### **Phase 4: Code Quality & Edge Cases** 🔍
**Goal**: Refactoring, edge cases, stress testing  
**Target**: Memory leaks, concurrency, boundary conditions  
**Batches**: 3 batches, 10 tests each  
**Duration**: 2 weeks  

---

## 📐 Implementation Guidelines

### **Batch-wise Process**

```
For each batch:
  1. Plan (Create BATCH_P<phase>_B<batch>_PLAN.md)
  2. Review (Review plan with team/AI)
  3. Execute (Implement tests)
  4. Validate (Run ./run_ut.sh, check coverage)
  5. Commit (Git commit with clear message)
  6. Summarize (Create BATCH_P<phase>_B<batch>_SUMMARY.md)
  7. Next (Move to next batch)
```

### **Test Naming Convention**

```cpp
TEST_F(TestFixture, FunctionName_Scenario_ExpectedResult)
{
    // Arrange
    // Act
    // Assert
}
```

### **Documentation Requirements**

For each batch:
- ✅ **Plan Document**: `BATCH_P<phase>_B<batch>_PLAN.md`
  - Functions to test
  - Test scenarios
  - Mock requirements
  - Expected outcomes

- ✅ **Summary Document**: `BATCH_P<phase>_B<batch>_SUMMARY.md`
  - Tests implemented
  - Coverage increase
  - Issues encountered
  - Next steps

### **Quality Gates**

Before moving to next batch:
- ✅ All tests pass via `./run_ut.sh`
- ✅ No compilation errors
- ✅ No memory leaks (if valgrind used)
- ✅ Code review complete
- ✅ Documentation updated

---

## 📚 Detailed Phase Plans

### **Phase 1: Business Logic Deep Coverage**
**See**: [PHASE1_BATCH_PLANS.md](./PHASE1_BATCH_PLANS.md)

**Summary:**
- Batch 1: XConf fetching (✅ Complete)
- Batch 2: Cache helper functions
- Batch 3: Response builders & validators
- Batch 4: Version comparison logic
- Batch 5: CheckForUpdate edge cases
- Batch 6: Error handling paths
- Batch 7: Memory management
- Batch 8: Utility functions

### **Phase 2: Integration & Async Operations**
**See**: [PHASE2_INTEGRATION_PLANS.md](./PHASE2_INTEGRATION_PLANS.md)

**Summary:**
- Batch 1: CheckForUpdate full workflow
- Batch 2: Cache + XConf integration
- Batch 3: Async task coordination
- Batch 4: Multi-client scenarios

### **Phase 3: Download & Flash Operations**
**See**: [PHASE3_DOWNLOAD_FLASH_PLANS.md](./PHASE3_DOWNLOAD_FLASH_PLANS.md)

**Summary:**
- Batch 1: Download handler basics
- Batch 2: Progress monitoring
- Batch 3: Flash operations
- Batch 4: Rollback & recovery

### **Phase 4: Code Quality & Edge Cases**
**See**: [PHASE4_QUALITY_PLANS.md](./PHASE4_QUALITY_PLANS.md)

**Summary:**
- Batch 1: Memory leak testing
- Batch 2: Concurrency & race conditions
- Batch 3: Boundary conditions & stress tests

---

## 📈 Progress Tracking

### **Coverage Progress**

| Phase | Start Coverage | Target Coverage | Current Coverage | Status |
|-------|----------------|-----------------|------------------|--------|
| **Phase 1** | 80-85% | 90-95% | 80-85% | 🔄 In Progress |
| **Phase 2** | 90-95% | 95%+ | - | ⏳ Pending |
| **Phase 3** | 0% (excluded) | TBD | - | ⏳ Pending |
| **Phase 4** | 95%+ | 95%+ | - | ⏳ Pending |

### **Batch Progress**

| Batch | Functions | Tests | Status | Coverage Δ |
|-------|-----------|-------|--------|------------|
| **P1-B1** | `fetch_xconf_firmware_info()` | 6 | ✅ Complete | +5% (est) |
| **P1-B2** | Cache helpers | 10 | ⏳ Planned | +3% (est) |
| **P1-B3** | Response builders | 10 | ⏳ Planned | +2% (est) |
| **P1-B4** | Version comparison | 10 | ⏳ Planned | +3% (est) |
| **P1-B5** | CheckForUpdate edge cases | 10 | ⏳ Planned | +2% (est) |
| **P1-B6** | Error handling | 10 | ⏳ Planned | +3% (est) |
| **P1-B7** | Memory management | 10 | ⏳ Planned | +2% (est) |
| **P1-B8** | Utility functions | 10 | ⏳ Planned | +2% (est) |

### **Test Count Progress**

| Component | Existing Tests | Target Tests | Current Tests | Remaining |
|-----------|----------------|--------------|---------------|-----------|
| **rdkFwupdateMgr_handlers.c** | ~43 | ~120 | 49 (est) | ~71 |
| **rdkFwupdateMgr.c** | ~30 | ~50 | 30 | ~20 |

---

## 🚀 Getting Started

### **1. Validate Phase 1, Batch 1**

```bash
cd rdkfwupdater
./run_ut.sh

# Check for any errors
# Verify new tests pass:
# - FetchXconfFirmwareInfo_UrlAllocationFails_ReturnsError
# - FetchXconfFirmwareInfo_HttpGetSuccess_ReturnsTrue
# - FetchXconfFirmwareInfo_HttpGetFails_ReturnsFalse
# - FetchXconfFirmwareInfo_HttpGet404_ReturnsFalse
# - FetchXconfFirmwareInfo_ParseError_ReturnsFalse
# - FetchXconfFirmwareInfo_CacheSaveSuccess_SavesCacheFile
```

### **2. Review Batch 1 Summary**

```bash
cat BATCH_P1_B1_SUMMARY.md
# Review implementation details
# Check for any issues or improvements needed
```

### **3. Start Phase 1, Batch 2**

```bash
cat PHASE1_BATCH_PLANS.md
# Read Batch 2 plan
# Confirm scope and approach
# Begin implementation
```

---

## 📖 Documentation Index

### **Master Plans**
- [`UNITTEST_MASTER_PLAN.md`](./UNITTEST_MASTER_PLAN.md) ← You are here
- [`PHASE1_BATCH_PLANS.md`](./PHASE1_BATCH_PLANS.md)
- [`PHASE2_INTEGRATION_PLANS.md`](./PHASE2_INTEGRATION_PLANS.md)
- [`PHASE3_DOWNLOAD_FLASH_PLANS.md`](./PHASE3_DOWNLOAD_FLASH_PLANS.md)
- [`PHASE4_QUALITY_PLANS.md`](./PHASE4_QUALITY_PLANS.md)

### **Batch Plans (Phase 1)**
- [`BATCH_P1_B1_PLAN.md`](./BATCH_P1_B1_PLAN.md) ✅
- [`BATCH_P1_B2_PLAN.md`](./BATCH_P1_B2_PLAN.md) ⏳
- [`BATCH_P1_B3_PLAN.md`](./BATCH_P1_B3_PLAN.md) ⏳
- ... (See PHASE1_BATCH_PLANS.md for full list)

### **Batch Summaries (Phase 1)**
- [`BATCH_P1_B1_SUMMARY.md`](./BATCH_P1_B1_SUMMARY.md) ✅
- [`BATCH_P1_B2_SUMMARY.md`](./BATCH_P1_B2_SUMMARY.md) ⏳ (After implementation)

### **Analysis & Reference**
- [`DBUS_UNITTEST_COVERAGE_ANALYSIS.md`](../DBUS_UNITTEST_COVERAGE_ANALYSIS.md)
- [`PHASE1_COMPLETE_ROADMAP.md`](../PHASE1_COMPLETE_ROADMAP.md)

---

## 🎯 Success Criteria

### **Phase 1 Success** ✅
- Coverage: 90-95% for `rdkFwupdateMgr_handlers.c`
- All tests pass via `./run_ut.sh`
- No memory leaks detected
- All 8 batches complete with documentation

### **Overall Project Success** ✅
- Coverage: 90-95% for business logic
- Integration tests passing
- CI/CD integration (if applicable)
- Full documentation complete
- Code review approved

---

## 🤝 Team Collaboration

### **Roles**
- **Developer**: Implement tests following plans
- **Reviewer**: Review plans and implementation
- **QA**: Validate tests run successfully
- **Architect**: Approve design decisions

### **Communication**
- Use batch plans for async review
- Update summaries after each batch
- Raise blockers immediately
- Weekly sync on progress

---

## 📝 Notes

### **Design Decisions**
- Mock only external dependencies, not internal functions
- Use `GTEST_ENABLE` to expose static functions
- Prefer real file I/O over mocked filesystem
- Keep tests focused (one function aspect per test)

### **Known Limitations**
- D-Bus infrastructure not in scope (integration tests exist)
- Download/flash operations excluded (Phase 3 TBD)
- `main()` function too complex for unit tests (integration tests)

### **Future Enhancements**
- Code coverage reports (gcov/lcov)
- Performance benchmarking
- Fuzz testing for parsers
- CI/CD pipeline integration

---

**Last Updated:** December 25, 2025  
**Maintained By:** RDK Firmware Update Team  
**Next Review:** After Phase 1, Batch 2 completion
