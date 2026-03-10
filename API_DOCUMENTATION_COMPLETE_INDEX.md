# RDK Firmware Update Manager - Complete API Documentation Index

## Overview

This documentation suite provides comprehensive technical reference for the RDK Firmware Update Manager client library (`librdkFwupdateMgr`). It's designed to onboard new engineers and serve as a complete API reference for application developers integrating firmware update capabilities.

---

## Quick Start

**New to the library?** Start here:

1. **[QUICK_START.md](librdkFwupdateMgr/QUICK_START.md)** - Get up and running in 5 minutes
2. **[Example Application](librdkFwupdateMgr/examples/example_app.c)** - Working code to learn from
3. **[Build and Test Guide](librdkFwupdateMgr/BUILD_AND_TEST.md)** - Compile and run examples

---

## Core API Documentation

### Process Registration (Required First Step)

| API | Purpose | When to Use |
|-----|---------|-------------|
| **[registerProcess](API_DOCUMENTATION_registerProcess.md)** | Connect to daemon, get session handle | First call in every application |
| **[unregisterProcess](API_DOCUMENTATION_unregisterProcess.md)** | Disconnect from daemon, cleanup | Last call before exit or when done |

**Workflow**: Every application MUST call `registerProcess()` first to obtain a handle, then `unregisterProcess()` when finished.

---

### Firmware Update Workflow (Sequential)

These APIs form the typical update workflow and should be called in order:

| Step | API | Purpose | Documentation |
|------|-----|---------|---------------|
| **1** | **[checkForUpdate](API_DOCUMENTATION_checkForUpdate.md)** | Query if new firmware is available | [View Docs](API_DOCUMENTATION_checkForUpdate.md) |
| **2** | **[downloadFirmware](API_DOCUMENTATION_downloadFirmware.md)** | Download firmware image from server | [View Docs](API_DOCUMENTATION_downloadFirmware.md) |
| **3** | **[updateFirmware](API_DOCUMENTATION_updateFirmware.md)** | Flash firmware to device storage | [View Docs](API_DOCUMENTATION_updateFirmware.md) |

**Typical Flow**:
```
registerProcess() → checkForUpdate() → [wait for callback] →
downloadFirmware() → [wait for completion] →
updateFirmware() → [wait for completion] →
[device reboots] → unregisterProcess()
```

---

## API Documentation by Category

### Registration & Session Management
- **[registerProcess](API_DOCUMENTATION_registerProcess.md)** - Create session with daemon
  - Parameters: `processName`, `errorCallback`
  - Returns: `FirmwareInterfaceHandle` (session ID)
  - Threading: Synchronous call, initializes background infrastructure
  
- **[unregisterProcess](API_DOCUMENTATION_unregisterProcess.md)** - Terminate session
  - Parameters: `handle`
  - Returns: None (void)
  - Threading: Synchronous call, stops background thread cleanly

### Update Discovery
- **[checkForUpdate](API_DOCUMENTATION_checkForUpdate.md)** - Query for new firmware
  - Parameters: `handle`, `UpdateEventCallback`
  - Returns: `CheckForUpdateResult` (SUCCESS/FAIL)
  - Threading: Non-blocking, callback fires in background thread
  - Latency: Callback invoked 5-30 seconds later (network-dependent)

### Firmware Download
- **[downloadFirmware](API_DOCUMENTATION_downloadFirmware.md)** - Download firmware image
  - Parameters: `handle`, `FwDwnlReq`, `DownloadCallback`
  - Returns: `DownloadResult` (SUCCESS/FAIL)
  - Threading: Non-blocking, callback fires repeatedly during download
  - Latency: 30 seconds to 10 minutes (file size and network-dependent)
  - Progress: Multiple callbacks with 0%-100% progress

### Firmware Flashing
- **[updateFirmware](API_DOCUMENTATION_updateFirmware.md)** - Flash firmware to storage
  - Parameters: `handle`, `FwUpdateReq`, `UpdateCallback`
  - Returns: `UpdateResult` (SUCCESS/FAIL)
  - Threading: Non-blocking, callback fires repeatedly during flash
  - Latency: 1-10 minutes (flash speed-dependent)
  - Progress: Multiple callbacks with 0%-100% progress
  - **⚠️ CRITICAL**: May trigger device reboot

---

## Architecture & Design Documentation

### Threading and Async Model
- **[ASYNC_API_QUICK_REFERENCE.md](ASYNC_API_QUICK_REFERENCE.md)** - How non-blocking APIs work
- **[ASYNC_MEMORY_MANAGEMENT.md](ASYNC_MEMORY_MANAGEMENT.md)** - Memory ownership and lifecycle
- **[CHECKFORUPDATE_ASYNC_IMPLEMENTATION_PLAN.md](CHECKFORUPDATE_ASYNC_IMPLEMENTATION_PLAN.md)** - Async design rationale

### Implementation Details
- **[IMPLEMENTATION_COMPLETE.md](IMPLEMENTATION_COMPLETE.md)** - Full implementation overview
- **[IMPLEMENTATION_SUMMARY.md](IMPLEMENTATION_SUMMARY.md)** - High-level architecture
- **[COMPLETE_ANALYSIS_AND_IMPLEMENTATION.md](COMPLETE_ANALYSIS_AND_IMPLEMENTATION.md)** - Detailed technical analysis

### Recent Bug Fixes
- **[DBUS_SERVICE_NAME_FIX.md](DBUS_SERVICE_NAME_FIX.md)** - D-Bus connection fixes
- **[DBUS_SIGNAL_SIGNATURE_FIX.md](DBUS_SIGNAL_SIGNATURE_FIX.md)** - Signal parsing fixes
- **[UPDATE_DETAILS_PARSING_FIX.md](UPDATE_DETAILS_PARSING_FIX.md)** - JSON/struct parsing fixes
- **[SIGNAL_SUBSCRIPTION_FIX.md](SIGNAL_SUBSCRIPTION_FIX.md)** - Progress signal fixes
- **[DOWNLOAD_SIGNAL_THREADING_FIX.md](DOWNLOAD_SIGNAL_THREADING_FIX.md)** - Threading model fixes

---

## Data Structures Reference

### Handles and Sessions
```c
typedef char* FirmwareInterfaceHandle;
```
- Opaque session identifier (string)
- Obtained from `registerProcess()`
- Required for all other API calls
- Must pass to `unregisterProcess()` when done

### Status Enums

#### CheckForUpdateStatus
```c
typedef enum {
    FIRMWARE_AVAILABLE = 0,       // New firmware available
    FIRMWARE_NOT_AVAILABLE = 1,   // Already latest
    UPDATE_NOT_ALLOWED = 2,       // Incompatible device
    FIRMWARE_CHECK_ERROR = 3,     // Check failed
    IGNORE_OPTOUT = 4,            // User opted out
    BYPASS_OPTOUT = 5             // Needs consent
} CheckForUpdateStatus;
```

#### DownloadStatus
```c
typedef enum {
    DWNL_IN_PROGRESS = 0,         // Downloading...
    DWNL_COMPLETED = 1,           // Done ✅
    DWNL_ERROR = 2                // Failed ❌
} DownloadStatus;
```

#### UpdateStatus
```c
typedef enum {
    UPDATE_IN_PROGRESS = 0,       // Flashing...
    UPDATE_COMPLETED = 1,         // Done ✅
    UPDATE_ERROR = 2              // Failed ❌
} UpdateStatus;
```

### Request Structures

#### FwDwnlReq - Download Request
```c
typedef struct {
    const char *firmwareName;      // REQUIRED: Image filename
    const char *downloadUrl;       // OPTIONAL: Custom URL (NULL = use XConf)
    const char *TypeOfFirmware;    // OPTIONAL: "PCI", "PDRI", "PERIPHERAL"
} FwDwnlReq;
```

#### FwUpdateReq - Flash Request
```c
typedef struct {
    const char *firmwareName;         // REQUIRED: Image filename
    const char *TypeOfFirmware;       // REQUIRED: "PCI", "PDRI", "PERIPHERAL"
    const char *LocationOfFirmware;   // RECOMMENDED: "/opt/CDL/" (NULL = device.properties)
    bool rebootImmediately;           // REQUIRED: Auto-reboot flag
} FwUpdateReq;
```

### Response Structures

#### FwInfoData - Update Check Result
```c
typedef struct {
    char CurrFWVersion[64];           // Current version
    UpdateDetails *UpdateDetails;      // Available update info
    CheckForUpdateStatus status;       // Result status
} FwInfoData;
```

#### UpdateDetails - Firmware Metadata
```c
typedef struct {
    char FwFileName[128];              // Image filename
    char FwUrl[512];                   // Download URL
    char FwVersion[64];                // New version string
    char RebootImmediately[12];        // "true" or "false"
    char DelayDownload[8];             // "true" or "false"
    char PDRIVersion[64];              // PDRI version (if any)
    char PeripheralFirmwares[256];     // Peripheral versions
} UpdateDetails;
```

---

## Callback Reference

### UpdateEventCallback
```c
typedef void (*UpdateEventCallback)(const FwInfoData *fwinfodata);
```
- **When**: Fires ONCE after `checkForUpdate()` completes
- **Thread**: Background GLib main loop thread
- **Data Lifetime**: `fwinfodata` only valid during callback (copy if needed)
- **Usage**: Check `status`, access `UpdateDetails` if `FIRMWARE_AVAILABLE`

### DownloadCallback
```c
typedef void (*DownloadCallback)(int download_progress, DownloadStatus fwdwnlstatus);
```
- **When**: Fires MULTIPLE times during download (0%, 25%, 50%, 75%, 100%)
- **Thread**: Background GLib main loop thread
- **Termination**: Last call has status `DWNL_COMPLETED` or `DWNL_ERROR`
- **Usage**: Update UI progress bar, check for completion

### UpdateCallback
```c
typedef void (*UpdateCallback)(int update_progress, UpdateStatus fwupdatestatus);
```
- **When**: Fires MULTIPLE times during flash (0%, 25%, 50%, 75%, 100%)
- **Thread**: Background GLib main loop thread
- **Termination**: Last call has status `UPDATE_COMPLETED` or `UPDATE_ERROR`
- **Usage**: Update UI, prepare for reboot
- **⚠️ Note**: Signature may change in future versions with HAL updates

---

## Best Practices Summary

### ✅ DO
- Always call `registerProcess()` first
- Always call `unregisterProcess()` before exit
- Wait for download completion before calling `updateFirmware()`
- Specify `LocationOfFirmware = "/opt/CDL/"` explicitly in `updateFirmware()`
- Copy callback data if needed beyond callback scope
- Keep application alive (event loop) until callbacks fire
- Check battery/power before flashing firmware
- Handle all status codes in callbacks

### ❌ DON'T
- Call library functions from within callbacks (re-entry risk)
- Block/sleep in callbacks (delays other handlers)
- Access callback data pointers after callback returns
- Exit application before callbacks fire
- Flash without downloading first
- Flash during low battery or unstable power
- Call multiple flash operations simultaneously

---

## Error Handling

### Immediate Failures (Return Codes)
All APIs return a result code (`SUCCESS` or `FAIL`):
- `FAIL` means request rejected immediately (validation error, D-Bus failure)
- Check return codes and handle errors before waiting for callbacks

### Async Failures (Callback Status)
Actual operation results arrive via callbacks:
- `checkForUpdate()` → `FIRMWARE_CHECK_ERROR`
- `downloadFirmware()` → `DWNL_ERROR`
- `updateFirmware()` → `UPDATE_ERROR`

### Debugging Tools
```bash
# Monitor daemon logs
journalctl -u rdkFwupdateMgr -f

# Watch D-Bus traffic
dbus-monitor --system "interface='com.comcast.xconf_firmware_mgr'"

# Check library logs (if RDK_LOG enabled)
cat /opt/logs/rdkFwupdateMgr.log
```

---

## Example Workflows

### Simple Auto-Update
```c
// 1. Register
FirmwareInterfaceHandle handle = registerProcess("MyApp", NULL);

// 2. Check for update
checkForUpdate(handle, checkCallback);

// In checkCallback:
void checkCallback(const FwInfoData *info) {
    if (info->status == FIRMWARE_AVAILABLE) {
        // 3. Download
        FwDwnlReq dwnl = {
            .firmwareName = info->UpdateDetails->FwFileName,
            .downloadUrl = NULL,  // Use XConf URL
            .TypeOfFirmware = "PCI"
        };
        downloadFirmware(handle, &dwnl, downloadCallback);
    }
}

// In downloadCallback:
void downloadCallback(int progress, DownloadStatus status) {
    if (status == DWNL_COMPLETED) {
        // 4. Flash
        FwUpdateReq update = {
            .firmwareName = savedFirmwareName,
            .TypeOfFirmware = "PCI",
            .LocationOfFirmware = "/opt/CDL/",
            .rebootImmediately = true  // Auto-reboot
        };
        updateFirmware(handle, &update, updateCallback);
    }
}

// In updateCallback:
void updateCallback(int progress, UpdateStatus status) {
    if (status == UPDATE_COMPLETED) {
        // Device reboots automatically
        logInfo("Update complete, rebooting...");
    }
}

// 5. Cleanup (after reboot)
unregisterProcess(handle);
```

### User-Controlled Update
```c
// Same flow, but with user prompts:
void checkCallback(const FwInfoData *info) {
    if (info->status == FIRMWARE_AVAILABLE) {
        showDialog("Update available. Download now?");
        // User clicks Yes → call downloadFirmware()
    }
}

void downloadCallback(int progress, DownloadStatus status) {
    updateProgressBar(progress);
    if (status == DWNL_COMPLETED) {
        showDialog("Download complete. Install now?");
        // User clicks Yes → call updateFirmware()
    }
}

void updateCallback(int progress, UpdateStatus status) {
    if (status == UPDATE_COMPLETED) {
        showDialog("Update installed. Reboot now?");
        // User clicks Yes → reboot manually
    }
}
```

---

## Troubleshooting Guide

### Problem: API Returns FAIL Immediately
**Cause**: Validation error or D-Bus connection failure  
**Solution**: Check logs for specific error, verify daemon is running

### Problem: Callback Never Fires
**Cause**: Application exits too early or event loop not running  
**Solution**: Keep app alive with `sleep()` or proper event loop

### Problem: Download/Flash Stalls at X%
**Cause**: Network timeout, daemon hang, HAL driver issue  
**Solution**: Check daemon logs, verify network/hardware health

### Problem: UPDATE_ERROR After Flash
**Cause**: File not found, checksum failure, flash I/O error  
**Solution**: Verify firmware file exists at specified location, check daemon logs

---

## Platform Requirements

### Dependencies
- **GLib 2.0** - Event loop and D-Bus bindings
- **D-Bus** - Inter-process communication
- **System Daemon** - `rdkFwupdateMgr` service must be running

### Compiler Requirements
- GCC 4.9+ or Clang 3.5+
- C11 support recommended

### Runtime Environment
- Linux kernel 3.10+ (MTD flash support)
- Root privileges for flash operations (daemon runs as root)
- Network access for firmware downloads

---

## API Versioning and Stability

### Current Version: 1.0

### Stability Guarantees
- **Registration APIs** (`registerProcess`, `unregisterProcess`): STABLE
- **Update Workflow APIs** (`checkForUpdate`, `downloadFirmware`): STABLE
- **Flash API** (`updateFirmware`): EVOLVING (see HAL notice below)

### Future Changes (Breaking)
The `UpdateCallback` signature **may change** when HAL APIs are fully integrated:
- Additional status codes (e.g., `UPDATE_VERIFYING`)
- Extended error information
- Per-partition progress reporting

Applications should prepare for major version upgrades requiring code changes.

---

## Getting Help

### Resources
- **Example Code**: `librdkFwupdateMgr/examples/example_app.c`
- **Unit Tests**: `unittest/` directory (demonstrates API usage)
- **Build Guide**: `librdkFwupdateMgr/BUILD_AND_TEST.md`

### Support Channels
- GitHub Issues: [Report bugs](https://github.com/...)
- Developer Forum: [Ask questions](https://forum.rdk.com/...)
- Technical Contact: firmware-team@comcast.com

---

## Document Revision History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2025-01-XX | Initial comprehensive API documentation suite |

---

## Related Documentation

- **[README.md](README.md)** - Project overview and quick start
- **[CHANGELOG.md](CHANGELOG.md)** - Version history and changes
- **[CONTRIBUTING.md](CONTRIBUTING.md)** - How to contribute
- **[LICENSE](LICENSE)** - Apache 2.0 license

---

## License

Copyright 2025 Comcast Cable Communications Management, LLC

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this project except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

SPDX-License-Identifier: Apache-2.0
