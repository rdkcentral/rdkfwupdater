# API Documentation: updateFirmware

## Overview

The `updateFirmware()` API initiates firmware flashing (writing firmware to device's flash storage) via the RDK Firmware Update Manager daemon. This is a **non-blocking, fire-and-forget** API that returns immediately while the daemon performs the actual flashing operation in the background.

---

## Function Signature

```c
UpdateResult updateFirmware(
    FirmwareInterfaceHandle handle,
    const FwUpdateReq *fwupdatereq,
    UpdateCallback callback
);
```

---

## Purpose

Flashes a previously downloaded firmware image to the device's storage. The daemon handles low-level hardware interactions via HAL (Hardware Abstraction Layer), partition management, verification, and progress reporting. The client application receives progress updates via multiple callback invocations.

**⚠️ CRITICAL**: This operation writes to flash storage and typically requires a device reboot to complete. Ensure power stability and do not interrupt the process.

---

## Parameters

### `handle` (FirmwareInterfaceHandle)
- **Type**: `char*` (string)
- **Description**: Valid session identifier obtained from `registerProcess()`
- **Requirements**:
  - Must not be `NULL`
  - Must not be empty string
  - Must be a currently registered handle
- **Example**: `"12345"`

### `fwupdatereq` (const FwUpdateReq*)
- **Type**: Pointer to firmware update request structure
- **Description**: Specifies which firmware to flash and how to flash it
- **Requirements**: Must not be `NULL`

#### FwUpdateReq Structure

```c
typedef struct {
    const char *firmwareName;         // Firmware filename (REQUIRED)
    const char *TypeOfFirmware;       // Firmware type (REQUIRED)
    const char *LocationOfFirmware;   // Path to firmware file (OPTIONAL)
    bool rebootImmediately;           // Auto-reboot flag (REQUIRED)
} FwUpdateReq;
```

##### `firmwareName` (const char*)
- **Required**: YES
- **Description**: Name of the firmware image file to flash
- **Requirements**:
  - Must not be `NULL`
  - Must not be empty string
  - Must match a successfully downloaded firmware file
  - Should match `UpdateDetails.FwFileName` from checkForUpdate
- **Example**: `"firmware_v2.1.0.bin"`, `"image_rdkv_12345.bin"`

##### `TypeOfFirmware` (const char*)
- **Required**: YES
- **Description**: Category/type of firmware being flashed
- **Valid Values**: 
  - `"PCI"` - Primary system firmware (most common)
  - `"PDRI"` - Platform Diagnostic Recovery Image
  - `"PERIPHERAL"` - Peripheral device firmware (Bluetooth, WiFi, etc.)
- **Requirements**:
  - Must not be `NULL`
  - Must not be empty string
  - Must match the type used in `downloadFirmware()`
- **Purpose**: Routes firmware to correct flash partition or peripheral update handler

##### `LocationOfFirmware` (const char*)
- **Required**: NO (but HIGHLY RECOMMENDED to specify explicitly)
- **Description**: Absolute filesystem path where firmware image is located
- **Default Behavior**: If `NULL` or empty, daemon uses path from `/etc/device.properties`
- **Recommended Value**: `"/opt/CDL/"` (standard download location)
- **Requirements**:
  - Must be an absolute path (start with `/`)
  - Directory must exist and be readable by daemon
  - Firmware file must exist at: `<LocationOfFirmware>/<firmwareName>`
- **Examples**:
  - `"/opt/CDL/"` - Standard location (RECOMMENDED)
  - `"/tmp/firmware/"` - Testing/development location
  - `NULL` - Use device.properties default (NOT RECOMMENDED - may fail)

##### `rebootImmediately` (bool)
- **Required**: YES
- **Description**: Whether to automatically reboot device after successful flash
- **Values**:
  - `true` - Daemon triggers reboot immediately after flash completes
  - `false` - Application is responsible for rebooting manually
- **Typical Usage**:
  - `true` - Automated/unattended updates
  - `false` - User-controlled updates, allow cleanup before reboot
- **Important**: Some firmware types (especially system firmware) REQUIRE a reboot to activate. Check `UpdateDetails.RebootImmediately` from checkForUpdate response.

### `callback` (UpdateCallback)
- **Type**: Function pointer
- **Signature**: `void (*UpdateCallback)(int update_progress, UpdateStatus fwupdatestatus)`
- **Description**: User-provided callback function invoked repeatedly during flashing
- **Requirements**:
  - Must not be `NULL`
  - Must be thread-safe (called from background thread)
- **Execution Context**: Background GLib main loop thread

---

## Return Value

### `UpdateResult` (enum)

| Value | Meaning |
|-------|---------|
| `RDKFW_UPDATE_SUCCESS` (0) | Flash request initiated successfully; callback will be invoked |
| `RDKFW_UPDATE_FAILED` (1) | Flash request failed immediately (see error conditions) |

**Important**: A return value of `RDKFW_UPDATE_SUCCESS` only means the request was successfully queued. The actual flash outcome is delivered asynchronously via callbacks.

---

## Callback Details

### UpdateCallback Function

```c
void updateCallback(int update_progress, UpdateStatus fwupdatestatus);
```

This callback is invoked **multiple times** during the flashing lifecycle.

#### Callback Parameters

##### `update_progress` (int)
- **Type**: Integer percentage (0-100)
- **Description**: Current flash operation completion percentage
- **Progression**: Increases monotonically: 0% → 25% → 50% → 75% → 100%
- **Notes**: 
  - Granularity depends on HAL implementation (may not be smooth)
  - Always reaches 100% before `UPDATE_COMPLETED` status
  - For multi-partition updates, represents overall progress

##### `fwupdatestatus` (UpdateStatus enum)

| Status | Value | Meaning | Callback Continuation |
|--------|-------|---------|----------------------|
| `UPDATE_IN_PROGRESS` | 0 | Flash operation actively progressing | Callback will fire again |
| `UPDATE_COMPLETED` | 1 | Flash finished successfully | FINAL callback - no more calls |
| `UPDATE_ERROR` | 2 | Flash failed (verification, I/O error, etc.) | FINAL callback - no more calls |

#### ⚠️ API Stability Notice

The signature and behavior of `UpdateCallback` **may change in future versions** when HAL (Hardware Abstraction Layer) APIs become available. Expected future enhancements:

- Additional status codes (e.g., `UPDATE_VERIFYING`, `UPDATE_PREPARING`)
- Device-specific progress information (e.g., per-partition progress)
- Extended error codes with failure reasons

Client applications should be prepared for potential signature changes in **major version updates**.

---

## Threading and Execution Model

### Call Flow Diagram

```
Application Thread                   Background Thread               Daemon Process
==================                   =================               ==============
                                                                    
updateFirmware(req)                                                 
    |                                                               
    ├─ Validate handle                                              
    ├─ Validate request                                             
    ├─ Connect to D-Bus                                             
    ├─ Register callback                                            
    |  in update registry                                           
    |                                                               
    ├─ Send D-Bus method ────────────────────────────────────────> UpdateFirmware(handle, ...)
    |  "UpdateFirmware"                                             |
    |                                                               ├─ Validate request
    └─ Return SUCCESS                                               ├─ Verify file exists
                                                                    ├─ Open HAL interface
    [Application continues]                                         ├─ Start flash operation
                                                                    |  (1-10 min typical)
                                                                    |
                                                                    ├─ Progress: 0% ───────────> UpdateProgress(0, IN_PROGRESS)
                                        Signal arrives <─────────────────────────────────────────┘
                                        on_update_progress_signal()
                                        |
                                        └─ Invoke ──────────────────> UpdateCallback(0, UPDATE_IN_PROGRESS)
                                                                    
                                                                    ├─ Progress: 25% ──────────> UpdateProgress(25, IN_PROGRESS)
                                        Signal arrives <─────────────────────────────────────────┘
                                        |
                                        └─ Invoke ──────────────────> UpdateCallback(25, UPDATE_IN_PROGRESS)
                                        
                                                                    [... similar for 50%, 75% ...]
                                                                    
                                                                    ├─ Progress: 100% ─────────> UpdateProgress(100, COMPLETED)
                                                                    ├─ Verify flash
                                                                    └─ [Reboot if requested]
                                        Signal arrives <─────────────────────────────────────────┘
                                        on_update_progress_signal()
                                        |
                                        ├─ Mark callback INACTIVE
                                        └─ Invoke ──────────────────> UpdateCallback(100, UPDATE_COMPLETED)
                                                                      |
                                                                      └─ [User: device may reboot]
```

### Key Timing Characteristics

- **API Call Duration**: < 10 milliseconds
- **Total Flash Time**: 1-10 minutes (depends on flash speed, image size, verification)
- **Progress Update Interval**: Daemon sends updates every 10-30 seconds (varies by HAL)
- **Callback Thread**: Background GLib main loop thread (not caller's thread)
- **Reboot Delay**: If `rebootImmediately=true`, reboot occurs 5-10 seconds after completion

---

## Error Conditions

The API fails immediately (returns `RDKFW_UPDATE_FAILED`) in these cases:

| Condition | Error Message Logged |
|-----------|----------------------|
| `handle` is `NULL` or empty | "updateFirmware: invalid handle (NULL or empty)" |
| `fwupdatereq` is `NULL` | "updateFirmware: fwupdatereq is NULL" |
| `firmwareName` is `NULL` | "updateFirmware: firmwareName is NULL" |
| `firmwareName` is empty | "updateFirmware: firmwareName is empty" |
| `TypeOfFirmware` is `NULL` | "updateFirmware: TypeOfFirmware is NULL" |
| `TypeOfFirmware` is empty | "updateFirmware: TypeOfFirmware is empty" |
| `callback` is `NULL` | "updateFirmware: callback is NULL" |
| D-Bus connection fails | "updateFirmware: D-Bus connect failed: \<reason\>" |
| Callback registry full | "updateFirmware: registry full, handle='\<handle\>'" |

### Flash-Time Errors (via callback with UPDATE_ERROR)

The callback receives `UPDATE_ERROR` status for these runtime failures:

- **File not found**: Firmware image missing from specified location
- **Checksum mismatch**: Image corrupted on disk (re-download required)
- **HAL failure**: Hardware abstraction layer reports I/O error
- **Flash write error**: Physical flash storage failure (hardware issue)
- **Verification failure**: Written data doesn't match expected (flash corrupted)
- **Insufficient space**: Flash partition too small for image
- **Permission denied**: Daemon lacks permission to access flash device
- **Device busy**: Another process is accessing flash storage

---

## Usage Example

### Basic Usage (Standard Workflow)

```c
#include "rdkFwupdateMgr_client.h"
#include <stdio.h>
#include <stdbool.h>

/* Flash progress callback - fires multiple times */
void myUpdateCallback(int progress, UpdateStatus status) {
    printf("Flash Progress: %d%%\n", progress);
    
    switch (status) {
        case UPDATE_IN_PROGRESS:
            printf("  Status: Writing to flash...\n");
            break;
            
        case UPDATE_COMPLETED:
            printf("  Status: ✅ Flash complete!\n");
            printf("  Device will reboot shortly.\n");
            break;
            
        case UPDATE_ERROR:
            printf("  Status: ❌ Flash failed!\n");
            printf("  Check logs for details.\n");
            break;
    }
}

int main() {
    FirmwareInterfaceHandle handle = registerProcess("MyApp", NULL);
    if (handle == NULL) {
        fprintf(stderr, "Registration failed\n");
        return 1;
    }
    
    /* Prepare flash request */
    FwUpdateReq request = {
        .firmwareName = "firmware_v2.1.0.bin",  // Must match downloaded file
        .TypeOfFirmware = "PCI",                 // Primary firmware
        .LocationOfFirmware = "/opt/CDL/",       // ✅ Explicit path (RECOMMENDED)
        .rebootImmediately = true                // Auto-reboot after flash
    };
    
    /* Initiate flashing */
    UpdateResult result = updateFirmware(handle, &request, myUpdateCallback);
    
    if (result == RDKFW_UPDATE_SUCCESS) {
        printf("Flash started. Device will reboot when complete.\n");
    } else {
        fprintf(stderr, "Failed to start flash operation\n");
    }
    
    /* Keep app running until reboot */
    sleep(600);  // 10 minutes max
    
    /* Note: Device reboots before we reach here if rebootImmediately=true */
    unregisterProcess(handle);
    return 0;
}
```

### Manual Reboot Control

```c
/* User controls when device reboots */
FwUpdateReq request = {
    .firmwareName = "firmware_v2.1.0.bin",
    .TypeOfFirmware = "PCI",
    .LocationOfFirmware = "/opt/CDL/",
    .rebootImmediately = false  // ✅ Manual reboot
};

UpdateResult result = updateFirmware(handle, &request, myUpdateCallback);

/* In callback: */
void myUpdateCallback(int progress, UpdateStatus status) {
    if (status == UPDATE_COMPLETED) {
        /* Flash done - but NO automatic reboot */
        showUserPrompt("Firmware updated. Reboot now?");
        /* User clicks "Yes" → call system("reboot") */
    }
}
```

### Peripheral Firmware Flash

```c
/* Flash Bluetooth controller firmware */
FwUpdateReq request = {
    .firmwareName = "bluetooth_fw_v1.5.bin",
    .TypeOfFirmware = "PERIPHERAL",
    .LocationOfFirmware = "/opt/CDL/",
    .rebootImmediately = false  // Peripheral updates may not need reboot
};

UpdateResult result = updateFirmware(handle, &request, myUpdateCallback);
```

### Development/Testing with Custom Location

```c
/* Flash from custom test location */
FwUpdateReq request = {
    .firmwareName = "test_firmware.bin",
    .TypeOfFirmware = "PCI",
    .LocationOfFirmware = "/tmp/test_firmware/",  // Custom path
    .rebootImmediately = false
};

UpdateResult result = updateFirmware(handle, &request, myUpdateCallback);
```

---

## Best Practices

### 1. **Always Specify LocationOfFirmware Explicitly**

```c
/* ✅ GOOD - explicit path, no ambiguity */
FwUpdateReq request = {
    .firmwareName = "firmware.bin",
    .TypeOfFirmware = "PCI",
    .LocationOfFirmware = "/opt/CDL/",  // ✅ Explicit
    .rebootImmediately = true
};

/* ⚠️ RISKY - relies on device.properties, may fail */
FwUpdateReq request = {
    .firmwareName = "firmware.bin",
    .TypeOfFirmware = "PCI",
    .LocationOfFirmware = NULL,  // ⚠️ Implicit - daemon guesses
    .rebootImmediately = true
};
```

### 2. **Wait for Download Completion Before Flashing**

```c
/* ✅ GOOD - ensure download finished */
void downloadCallback(int progress, DownloadStatus status) {
    if (status == DWNL_COMPLETED) {
        /* NOW safe to flash */
        FwUpdateReq req = { ... };
        updateFirmware(handle, &req, updateCallback);
    }
}

/* ❌ BAD - flash before download done */
downloadFirmware(handle, &dwnlReq, downloadCallback);
FwUpdateReq req = { ... };
updateFirmware(handle, &req, updateCallback);  // File doesn't exist yet!
```

### 3. **Monitor Progress and Handle Errors Gracefully**

```c
void updateCallback(int progress, UpdateStatus status) {
    /* ✅ GOOD - comprehensive error handling */
    switch (status) {
        case UPDATE_IN_PROGRESS:
            updateUI_FlashProgress(progress);
            if (progress == 0) {
                logInfo("Flash started");
            }
            break;
            
        case UPDATE_COMPLETED:
            logInfo("Flash completed successfully");
            if (manualReboot) {
                showRebootPrompt();
            } else {
                showMessage("Device rebooting...");
            }
            break;
            
        case UPDATE_ERROR:
            logError("Flash failed at %d%%", progress);
            showErrorDialog("Update failed. Please contact support.");
            /* Save state for retry or rollback */
            saveFailureState(progress);
            break;
    }
}
```

### 4. **Respect RebootImmediately Recommendation**

```c
/* ✅ GOOD - honor daemon's recommendation */
void checkCallback(const FwInfoData *info) {
    if (info->status == FIRMWARE_AVAILABLE && info->UpdateDetails) {
        bool needsReboot = (strcmp(info->UpdateDetails->RebootImmediately, "true") == 0);
        
        FwUpdateReq req = {
            .firmwareName = info->UpdateDetails->FwFileName,
            .TypeOfFirmware = "PCI",
            .LocationOfFirmware = "/opt/CDL/",
            .rebootImmediately = needsReboot  // ✅ Use daemon's advice
        };
        
        updateFirmware(handle, &req, updateCallback);
    }
}
```

### 5. **Don't Block in Callback**

```c
/* ❌ BAD - blocks background thread */
void badCallback(int progress, UpdateStatus status) {
    sleep(5);  // Delays other signal handlers!
    processProgress(progress);
}

/* ✅ GOOD - quick processing */
void goodCallback(int progress, UpdateStatus status) {
    atomic_store(&globalProgress, progress);
    if (status == UPDATE_COMPLETED) {
        signalMainThread(FLASH_DONE);  // Async notification
    }
}
```

### 6. **Ensure Power Stability**

```c
/* ✅ GOOD - check battery/power before flashing */
bool isSafeToFlash() {
    /* Check battery level, AC power, etc. */
    int batteryPercent = getBatteryLevel();
    bool isPluggedIn = isACPowerConnected();
    
    return (batteryPercent > 50 || isPluggedIn);
}

if (isSafeToFlash()) {
    updateFirmware(handle, &req, updateCallback);
} else {
    showError("Please connect power before updating firmware");
}
```

---

## Internal Implementation Details

### D-Bus Method Call Details

**Service**: `com.comcast.xconf_firmware_mgr`  
**Object Path**: `/com/comcast/xconf_firmware_mgr`  
**Interface**: `com.comcast.xconf_firmware_mgr`  
**Method**: `UpdateFirmware`  
**Arguments**: `(sssss)` - 5 strings
  1. `handle` - Application identifier
  2. `firmwareName` - Image filename
  3. `LocationOfFirmware` - Path to image or empty string
  4. `TypeOfFirmware` - "PCI", "PDRI", or "PERIPHERAL"
  5. `rebootImmediately` - "true" or "false" (daemon expects string)  
**Reply**: None (fire-and-forget)

### D-Bus Signal Details

**Signal Name**: `UpdateProgress`  
**Signature**: `(ii)` - (int32, int32)  
**Arguments**:
  1. `update_progress` - Percentage (0-100)
  2. `fwupdatestatus` - UpdateStatus enum value (0/1/2)

### Flash Locations

- **Primary Firmware (PCI)**: Typically MTD partition `/dev/mtd0` or `/dev/mtdblock0`
- **PDRI Firmware**: Separate recovery partition (device-specific)
- **Peripheral Firmware**: Varies by peripheral type (Bluetooth, WiFi, etc.)

---

## Safety Considerations

### ⚠️ Critical Warnings

1. **Power Loss During Flash = Bricked Device**
   - Ensure AC power or battery > 50%
   - Disable sleep/suspend modes
   - Prevent user from powering off

2. **Do Not Flash Multiple Times Simultaneously**
   - Only one flash operation per device
   - Wait for `UPDATE_COMPLETED` before starting another

3. **Verify Firmware Compatibility**
   - Check `UpdateDetails` from `checkForUpdate()` first
   - Never flash firmware for a different device model
   - Validate checksums before flashing

4. **Reboot Is Typically Required**
   - System firmware usually needs reboot to activate
   - Peripheral firmware may work without reboot
   - Check `UpdateDetails.RebootImmediately` for guidance

---

## Troubleshooting

### Problem: Callback Never Fires

**Possible Causes**:
1. Application exits before flash starts
2. Daemon not running (`systemctl status rdkFwupdateMgr`)
3. Daemon crashed during flash (check logs)

**Debugging**:
```bash
# Check daemon logs
journalctl -u rdkFwupdateMgr -f | grep -i update

# Monitor D-Bus signals
dbus-monitor --system "type='signal',interface='com.comcast.xconf_firmware_mgr'"
```

### Problem: UPDATE_ERROR Immediately

**Common Causes**:
- Firmware file not found at specified location
- `LocationOfFirmware` path invalid or empty
- Daemon lacks permission to read file

**Check**:
```bash
# Verify file exists
ls -lh /opt/CDL/firmware_v2.1.0.bin

# Check permissions
sudo -u rdkfwupdatemgr cat /opt/CDL/firmware_v2.1.0.bin
```

### Problem: Flash Hangs at X%

**Common Causes**:
- HAL driver issue (kernel module)
- Physical flash hardware failure
- MTD partition locked

**Action**: Contact hardware vendor or check kernel logs:
```bash
dmesg | grep -i "mtd\|flash"
```

### Problem: Device Won't Boot After Flash

**Cause**: Flash operation failed but wasn't detected, or wrong firmware flashed

**Recovery**:
1. Enter recovery mode (device-specific key combo)
2. Flash PDRI image (recovery firmware)
3. Contact device manufacturer support

---

## Workflow Integration

### Complete Update Flow

```
1. registerProcess()           → get handle
2. checkForUpdate()            → discover new firmware
3. [User approves update]
4. downloadFirmware()          → download image
5. [Wait for DWNL_COMPLETED]
6. updateFirmware()            → flash downloaded image (THIS API)
7. [Wait for UPDATE_COMPLETED]
8. [Device reboots automatically or manually]
9. [After reboot: verify new version]
10. unregisterProcess()        → cleanup (if app restarts post-boot)
```

### Example State Machine

```c
typedef enum {
    STATE_IDLE,
    STATE_CHECKING,
    STATE_UPDATE_AVAILABLE,
    STATE_DOWNLOADING,
    STATE_DOWNLOAD_COMPLETE,
    STATE_FLASHING,
    STATE_FLASH_COMPLETE,
    STATE_REBOOTING
} UpdateState;

UpdateState state = STATE_IDLE;

void updateProgressHandler(int progress, UpdateStatus status) {
    if (status == UPDATE_COMPLETED && state == STATE_FLASHING) {
        state = STATE_FLASH_COMPLETE;
        if (autoReboot) {
            state = STATE_REBOOTING;
            notifyUI("Rebooting...");
        } else {
            notifyUI("Flash complete. Please reboot.");
        }
    } else if (status == UPDATE_ERROR) {
        state = STATE_IDLE;
        notifyUI("Flash failed. Please try again.");
    }
}
```

---

## See Also

- [downloadFirmware API Documentation](API_DOCUMENTATION_downloadFirmware.md) - Required before updateFirmware
- [checkForUpdate API Documentation](API_DOCUMENTATION_checkForUpdate.md) - Discover available updates
- [registerProcess API Documentation](API_DOCUMENTATION_registerProcess.md) - Required first step
- [unregisterProcess API Documentation](API_DOCUMENTATION_unregisterProcess.md) - Cleanup after update
- [Threading and Async Model](ASYNC_API_QUICK_REFERENCE.md) - Deep dive into async architecture

---

## Change History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2025-01-XX | Initial API documentation with HAL evolution notice |
