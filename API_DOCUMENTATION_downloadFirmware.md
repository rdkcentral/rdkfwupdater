# API Documentation: downloadFirmware

## Overview

The `downloadFirmware()` API initiates a firmware image download from the RDK Firmware Update Manager daemon. This is a **non-blocking, fire-and-forget** API that returns immediately while the daemon performs the actual download in the background.

---

## Function Signature

```c
DownloadResult downloadFirmware(
    FirmwareInterfaceHandle handle,
    const FwDwnlReq *fwdwnlreq,
    DownloadCallback callback
);
```

---

## Purpose

Downloads a firmware image file from a remote server (typically from URL provided by XConf or a custom URL). The daemon handles HTTP/HTTPS transfers, retry logic, checksum validation, and progress reporting. The client application receives progress updates via multiple callback invocations.

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

### `fwdwnlreq` (const FwDwnlReq*)
- **Type**: Pointer to firmware download request structure
- **Description**: Specifies what firmware to download and from where
- **Requirements**: Must not be `NULL`

#### FwDwnlReq Structure

```c
typedef struct {
    const char *firmwareName;      // Firmware filename (REQUIRED)
    const char *downloadUrl;       // Download URL (OPTIONAL - can be NULL or empty)
    const char *TypeOfFirmware;    // Firmware type (OPTIONAL - can be NULL or empty)
} FwDwnlReq;
```

##### `firmwareName` (const char*)
- **Required**: YES
- **Description**: Name of the firmware image file
- **Requirements**:
  - Must not be `NULL`
  - Must not be empty string
  - Should match the filename from `UpdateDetails.FwFileName` (from checkForUpdate)
- **Example**: `"firmware_v2.1.0.bin"`, `"image_rdkv_12345.bin"`

##### `downloadUrl` (const char*)
- **Required**: NO
- **Description**: Custom download URL (overrides XConf-provided URL)
- **Default Behavior**: If `NULL` or empty, daemon uses URL from XConf server
- **Use Cases**:
  - Override XConf URL for testing
  - Use local mirror or CDN
  - Specify URL from custom update server
- **Example**: 
  - `"https://firmware.example.com/releases/image.bin"`
  - `NULL` → daemon uses URL from previous checkForUpdate response

##### `TypeOfFirmware` (const char*)
- **Required**: NO
- **Description**: Type/category of firmware being downloaded
- **Valid Values**: `"PCI"`, `"PDRI"`, `"PERIPHERAL"`, or `NULL`/empty
- **Purpose**: Helps daemon route to correct flash partition or peripheral update logic
- **Default**: If `NULL` or empty, daemon treats as primary/PCI firmware

### `callback` (DownloadCallback)
- **Type**: Function pointer
- **Signature**: `void (*DownloadCallback)(int download_progress, DownloadStatus fwdwnlstatus)`
- **Description**: User-provided callback function invoked repeatedly during download
- **Requirements**:
  - Must not be `NULL`
  - Must be thread-safe (called from background thread)
- **Execution Context**: Background GLib main loop thread

---

## Return Value

### `DownloadResult` (enum)

| Value | Meaning |
|-------|---------|
| `RDKFW_DWNL_SUCCESS` (0) | Download request initiated successfully; callback will be invoked |
| `RDKFW_DWNL_FAILED` (1) | Download request failed immediately (see error conditions) |

**Important**: A return value of `RDKFW_DWNL_SUCCESS` only means the request was successfully queued. The actual download outcome is delivered asynchronously via callbacks.

---

## Callback Details

### DownloadCallback Function

```c
void downloadCallback(int download_progress, DownloadStatus fwdwnlstatus);
```

This callback is invoked **multiple times** during the download lifecycle.

#### Callback Parameters

##### `download_progress` (int)
- **Type**: Integer percentage (0-100)
- **Description**: Current download completion percentage
- **Progression**: Increases monotonically: 0% → 25% → 50% → 75% → 100%
- **Notes**: 
  - May not increment smoothly (depends on daemon's progress reporting)
  - Always reaches 100% before `DWNL_COMPLETED` status

##### `fwdwnlstatus` (DownloadStatus enum)

| Status | Value | Meaning | Callback Continuation |
|--------|-------|---------|----------------------|
| `DWNL_IN_PROGRESS` | 0 | Download is actively progressing | Callback will fire again |
| `DWNL_COMPLETED` | 1 | Download finished successfully | FINAL callback - no more calls |
| `DWNL_ERROR` | 2 | Download failed (network, checksum, disk full, etc.) | FINAL callback - no more calls |

---

## Threading and Execution Model

### Call Flow Diagram

```
Application Thread                   Background Thread               Daemon Process
==================                   =================               ==============
                                                                    
downloadFirmware(req)                                               
    |                                                               
    ├─ Validate handle                                              
    ├─ Validate request                                             
    ├─ Connect to D-Bus                                             
    ├─ Register callback                                            
    |  in download registry                                         
    |                                                               
    ├─ Send D-Bus method ────────────────────────────────────────> DownloadFirmware(handle, ...)
    |  "DownloadFirmware"                                           |
    |                                                               ├─ Validate request
    └─ Return SUCCESS                                               ├─ Start HTTP download
                                                                    |  (30 sec - 10 min typical)
    [Application continues]                                         |
                                                                    ├─ Progress: 0% ───────────> DownloadProgress(0, IN_PROGRESS)
                                        Signal arrives <─────────────────────────────────────────┘
                                        on_download_progress_signal()
                                        |
                                        └─ Invoke ──────────────────> DownloadCallback(0, DWNL_IN_PROGRESS)
                                                                    
                                                                    ├─ Progress: 25% ──────────> DownloadProgress(25, IN_PROGRESS)
                                        Signal arrives <─────────────────────────────────────────┘
                                        |
                                        └─ Invoke ──────────────────> DownloadCallback(25, DWNL_IN_PROGRESS)
                                        
                                                                    [... similar for 50%, 75% ...]
                                                                    
                                                                    ├─ Progress: 100% ─────────> DownloadProgress(100, COMPLETED)
                                                                    └─ Verify checksum
                                        Signal arrives <─────────────────────────────────────────┘
                                        on_download_progress_signal()
                                        |
                                        ├─ Mark callback INACTIVE
                                        └─ Invoke ──────────────────> DownloadCallback(100, DWNL_COMPLETED)
                                                                      |
                                                                      └─ [User: ready to call updateFirmware()]
```

### Key Timing Characteristics

- **API Call Duration**: < 10 milliseconds
- **Total Download Time**: 30 seconds to 10 minutes (depends on file size and network speed)
- **Progress Update Interval**: Daemon sends updates every 5-10 seconds (approximately)
- **Callback Thread**: Background GLib main loop thread (not caller's thread)

---

## Error Conditions

The API fails immediately (returns `RDKFW_DWNL_FAILED`) in these cases:

| Condition | Error Message Logged |
|-----------|----------------------|
| `handle` is `NULL` or empty | "downloadFirmware: invalid handle (NULL or empty)" |
| `fwdwnlreq` is `NULL` | "downloadFirmware: fwdwnlreq is NULL" |
| `firmwareName` is `NULL` | "downloadFirmware: firmwareName is NULL" |
| `firmwareName` is empty | "downloadFirmware: firmwareName is empty" |
| `callback` is `NULL` | "downloadFirmware: callback is NULL" |
| D-Bus connection fails | "downloadFirmware: D-Bus connect failed: \<reason\>" |
| Callback registry full | "downloadFirmware: registry full, handle='\<handle\>'" |

### Download-Time Errors (via callback with DWNL_ERROR)

The callback receives `DWNL_ERROR` status for these runtime failures:

- **Network errors**: Connection timeout, DNS failure, HTTP 404/500
- **Checksum mismatch**: Downloaded file corrupted or tampered
- **Disk full**: Insufficient storage for firmware image
- **Permission denied**: Cannot write to download location (typically `/opt/CDL/`)
- **Daemon crash**: Firmware daemon terminated during download

---

## Usage Example

### Basic Usage (Use XConf URL)

```c
#include "rdkFwupdateMgr_client.h"
#include <stdio.h>

/* Progress callback - fires multiple times */
void myDownloadCallback(int progress, DownloadStatus status) {
    printf("Download Progress: %d%%\n", progress);
    
    switch (status) {
        case DWNL_IN_PROGRESS:
            printf("  Status: In progress...\n");
            break;
            
        case DWNL_COMPLETED:
            printf("  Status: ✅ Download complete!\n");
            printf("  Firmware ready for flashing.\n");
            break;
            
        case DWNL_ERROR:
            printf("  Status: ❌ Download failed!\n");
            break;
    }
}

int main() {
    FirmwareInterfaceHandle handle = registerProcess("MyApp", NULL);
    if (handle == NULL) {
        fprintf(stderr, "Registration failed\n");
        return 1;
    }
    
    /* Prepare download request - let daemon use XConf URL */
    FwDwnlReq request = {
        .firmwareName = "firmware_v2.1.0.bin",  // From checkForUpdate result
        .downloadUrl = NULL,                     // NULL = use XConf URL
        .TypeOfFirmware = "PCI"                  // Primary firmware
    };
    
    /* Initiate download */
    DownloadResult result = downloadFirmware(handle, &request, myDownloadCallback);
    
    if (result == RDKFW_DWNL_SUCCESS) {
        printf("Download started. Monitor progress via callback...\n");
    } else {
        fprintf(stderr, "Failed to start download\n");
    }
    
    /* Keep app running to receive callbacks */
    sleep(600);  // 10 minutes max
    
    unregisterProcess(handle);
    return 0;
}
```

### Advanced Usage (Custom URL Override)

```c
/* Override XConf URL - useful for testing or custom firmware sources */
FwDwnlReq request = {
    .firmwareName = "custom_firmware.bin",
    .downloadUrl = "https://mycdn.example.com/firmware/custom_firmware.bin",
    .TypeOfFirmware = "PCI"
};

DownloadResult result = downloadFirmware(handle, &request, myDownloadCallback);
```

### Peripheral Firmware Download

```c
/* Download peripheral device firmware (e.g., Bluetooth controller) */
FwDwnlReq request = {
    .firmwareName = "bluetooth_fw_v1.5.bin",
    .downloadUrl = NULL,  // Use XConf URL
    .TypeOfFirmware = "PERIPHERAL"
};

DownloadResult result = downloadFirmware(handle, &request, myDownloadCallback);
```

---

## Best Practices

### 1. **Monitor Progress and Handle Errors**

```c
void downloadCallback(int progress, DownloadStatus status) {
    /* ✅ GOOD - handle all status codes */
    switch (status) {
        case DWNL_IN_PROGRESS:
            updateProgressBar(progress);  // Update UI
            break;
            
        case DWNL_COMPLETED:
            enableFlashButton();  // Allow user to proceed
            break;
            
        case DWNL_ERROR:
            showErrorDialog("Download failed. Please try again.");
            logDownloadFailure(progress);  // Diagnostic logging
            break;
    }
}
```

### 2. **Use XConf URL Unless You Have a Specific Reason**

```c
/* ✅ GOOD - trust XConf server */
FwDwnlReq request = {
    .firmwareName = updateDetails->FwFileName,  // From checkForUpdate
    .downloadUrl = NULL,                        // Daemon knows best URL
    .TypeOfFirmware = "PCI"
};

/* ❌ RARELY NEEDED - only for testing or custom deployments */
FwDwnlReq customRequest = {
    .firmwareName = "firmware.bin",
    .downloadUrl = "https://my-test-server.local/firmware.bin",  // Override
    .TypeOfFirmware = "PCI"
};
```

### 3. **Don't Block in Callback**

```c
/* ❌ BAD - blocks background thread */
void badCallback(int progress, DownloadStatus status) {
    sleep(5);  // Delays other signal handlers!
    processProgress(progress);
}

/* ✅ GOOD - quick processing */
void goodCallback(int progress, DownloadStatus status) {
    atomic_store(&globalProgress, progress);  // Fast update
    if (status == DWNL_COMPLETED) {
        signalMainThread(DOWNLOAD_DONE);  // Async notification
    }
}
```

### 4. **Keep Application Alive During Download**

```c
/* ✅ GOOD - event loop keeps app running */
downloadFirmware(handle, &request, myCallback);
g_main_loop_run(mainLoop);  // Or other event loop

/* ❌ BAD - app exits before download finishes */
downloadFirmware(handle, &request, myCallback);
return 0;  // Callbacks never fire!
```

### 5. **Wait for DWNL_COMPLETED Before Flashing**

```c
/* ✅ GOOD - state machine pattern */
typedef enum { IDLE, DOWNLOADING, READY_TO_FLASH, FLASHING } State;
State state = IDLE;

void downloadCallback(int progress, DownloadStatus status) {
    if (status == DWNL_COMPLETED) {
        state = READY_TO_FLASH;
        /* NOW safe to call updateFirmware() */
    }
}

void userClickedFlashButton() {
    if (state == READY_TO_FLASH) {
        updateFirmware(handle, &updateReq, updateCallback);
        state = FLASHING;
    } else {
        showError("Download not complete yet!");
    }
}
```

---

## Internal Implementation Details

### D-Bus Method Call Details

**Service**: `com.comcast.xconf_firmware_mgr`  
**Object Path**: `/com/comcast/xconf_firmware_mgr`  
**Interface**: `com.comcast.xconf_firmware_mgr`  
**Method**: `DownloadFirmware`  
**Arguments**: `(ssss)` - 4 strings
  1. `handle` - Application identifier
  2. `firmwareName` - Image filename
  3. `downloadUrl` - Custom URL or empty string
  4. `TypeOfFirmware` - "PCI", "PDRI", "PERIPHERAL", or empty  
**Reply**: None (fire-and-forget)

### D-Bus Signal Details

**Signal Name**: `DownloadProgress`  
**Signature**: `(ii)` - (int32, int32)  
**Arguments**:
  1. `download_progress` - Percentage (0-100)
  2. `fwdwnlstatus` - DownloadStatus enum value (0/1/2)

### Download Location

- **Primary Firmware**: `/opt/CDL/` (configurable via `/etc/device.properties`)
- **Peripheral Firmware**: May use different paths based on device configuration

---

## Troubleshooting

### Problem: Callback Never Fires

**Possible Causes**:
1. Application exits before download starts
2. Daemon not running (`systemctl status rdkFwupdateMgr`)
3. Network connectivity lost

**Debugging**:
```bash
# Check daemon logs
journalctl -u rdkFwupdateMgr -f | grep -i download

# Monitor D-Bus signals
dbus-monitor --system "type='signal',interface='com.comcast.xconf_firmware_mgr'"
```

### Problem: Download Stops at X%

**Common Causes**:
- Network timeout (daemon may retry automatically)
- Server throttling/rate limiting
- Disk space exhausted

**Check**:
```bash
# Check available disk space
df -h /opt/CDL/

# Check daemon logs for retry attempts
journalctl -u rdkFwupdateMgr | grep -A 10 "Download"
```

### Problem: DWNL_ERROR with 100% Progress

**Cause**: Checksum validation failed after download completed

**Action**: File corrupted in transit. Daemon will typically retry automatically. Check logs for checksum mismatch errors.

---

## Workflow Integration

### Typical Update Flow

```
1. registerProcess()           → get handle
2. checkForUpdate()            → discover new firmware
3. [User approves update]
4. downloadFirmware()          → download image (THIS API)
5. [Wait for DWNL_COMPLETED]
6. updateFirmware()            → flash downloaded image
7. [Wait for UPDATE_COMPLETED]
8. [Device reboots]
9. unregisterProcess()         → cleanup
```

### Example State Machine

```c
typedef enum {
    STATE_REGISTERED,
    STATE_CHECKING,
    STATE_UPDATE_AVAILABLE,
    STATE_DOWNLOADING,
    STATE_DOWNLOAD_COMPLETE,
    STATE_FLASHING,
    STATE_COMPLETE
} UpdateState;

UpdateState currentState = STATE_REGISTERED;

void downloadProgressHandler(int progress, DownloadStatus status) {
    if (status == DWNL_COMPLETED && currentState == STATE_DOWNLOADING) {
        currentState = STATE_DOWNLOAD_COMPLETE;
        notifyUI("Download complete. Ready to flash.");
    }
}
```

---

## See Also

- [checkForUpdate API Documentation](API_DOCUMENTATION_checkForUpdate.md) - Precursor to download
- [updateFirmware API Documentation](API_DOCUMENTATION_updateFirmware.md) - Next step after download
- [registerProcess API Documentation](API_DOCUMENTATION_registerProcess.md) - Required first step
- [Threading and Async Model](ASYNC_API_QUICK_REFERENCE.md) - Deep dive into async architecture

---

## Change History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2025-01-XX | Initial API documentation |
