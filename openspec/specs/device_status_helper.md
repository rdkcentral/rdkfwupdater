# Module Spec: device_status_helper & download_status_helper

## Identity

- **Module**: `device_status_helper` / `download_status_helper`
- **Source**: `src/device_status_helper.c`, `src/download_status_helper.c`
- **Headers**: `src/include/device_status_helper.h`, `src/include/download_status_helper.h`
- **Type**: Compiled into main binaries

## Purpose

Track and report device state and firmware download progress. Provides utility functions for querying current running instance, DNS resolution status, and managing persistent download status files.

## device_status_helper Responsibilities

1. Check if current running instance is the active firmware
2. Verify DNS resolution availability
3. Query device state (maintenance window, download allowed, etc.)
4. Check file presence and accessibility

## download_status_helper Responsibilities

1. Update firmware download status in persistent status file
2. Notify download status to external consumers (IARM, rbus)
3. Track download progress percentage
4. Manage download state transitions

## Key Functions

### device_status_helper

```c
int CurrentRunningInst(void);        // Check active instance
int isDnsResolve(void);              // DNS resolution check
int filePresentCheck(const char *path); // File existence check
int isStateRedSupported(void);       // State Red support query
int isInStateRed(void);             // Current State Red status
void checkAndEnterStateRed(void);   // Enter State Red if supported
```

### download_status_helper

```c
void updateFWDownloadStatus(const char *status); // Write status to file
void notifyDwnlStatus(int status);               // Broadcast status event
```

## Status File

- Path: Device-specific status file location
- Format: Text-based status strings
- Updated at each download state transition

## Dependencies

- File system access for status persistence
- IARM bus for status broadcasting (via iarmInterface)

## Test Coverage

- `unittest/device_status_helper_gtest.cpp`
- Mocked in `unittest/miscellaneous_mock.cpp` for other test suites
