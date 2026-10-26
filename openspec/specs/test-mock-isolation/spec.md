# Subsystem Specification: test-mock-isolation

> **Subsystem:** Test Mock Isolation  
> **Type:** Testing Infrastructure  
> **Scope:** Unit test suite  
> **Evidence Level:** Verified from `unittest/basic_rdkv_main_gtest.cpp`, `unittest/Makefile.am`

---

## 1. Purpose

The `test-mock-isolation` specification defines the contracts for proper mock lifecycle management in the L1 unit test suite, ensuring that global mock pointers do not leak between tests and that build artifacts are coherent.

---

## 2. Requirements

### Requirement: Stack-local DeviceUtilsMock cleanup
Every TEST() in `basic_rdkv_main_gtest.cpp` that assigns `g_DeviceUtilsMock` to a stack-local `DeviceUtilsMock` object SHALL reset `g_DeviceUtilsMock = nullptr` before the test function returns.

#### Scenario: flashImageTestMaintFalse does not leave dangling pointer
- **WHEN** `flashImageTestMaintFalse` completes
- **THEN** `g_DeviceUtilsMock` SHALL be `nullptr`

#### Scenario: getXconfResTest succeeds after flashImage tests
- **WHEN** `getXconfResTest` runs after any `flashImage*` test
- **THEN** `isDirectCDNEnabled()` SHALL encounter `g_DeviceUtilsMock == nullptr`, return `false`, and the test SHALL pass without segfault

#### Scenario: All flashImage tests reset mock pointer
- **WHEN** any of `flashImageTest`, `flashImageTestRedState`, `flashImageTestFail`, `flashImageTestFail1`, `flashImageTestFail2`, `flashImageTestRebootTrue`, `flashImageTestPdri`, `flashImageTestMaintTrue`, or `flashImageTestMaintFalse` completes
- **THEN** `g_DeviceUtilsMock` SHALL be `nullptr`

### Requirement: No phantom bin_PROGRAMS without sources
Every entry in `bin_PROGRAMS` in `unittest/Makefile.am` SHALL have a corresponding `_SOURCES` definition.

#### Scenario: Phantom programs removed
- **WHEN** `make` is invoked in the unittest directory
- **THEN** automake SHALL NOT emit "No rule to make target" errors for any program listed in `bin_PROGRAMS`
