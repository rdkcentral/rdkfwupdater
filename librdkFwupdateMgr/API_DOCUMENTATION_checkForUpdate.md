# API Documentation: checkForUpdate

## Overview

The `checkForUpdate()` API initiates a firmware update check with the RDK Firmware Update Manager daemon. This is a **non-blocking, fire-and-forget** API that returns immediately while the daemon performs the actual update check in the background.

---

## Function Signature

```c
CheckForUpdateResult checkForUpdate(
    FirmwareInterfaceHandle handle,
    UpdateEventCallback callback
);
```

---

## Purpose

Queries the XConf server (or configured firmware update server) to determine if a newer firmware version is available for the device. The daemon performs the network query asynchronously and notifies the client application via a D-Bus signal when complete.

---

## Parameters

### `handle` (FirmwareInterfaceHandle)
- **Type**: `char*` (string)
- **Description**: Valid session identifier obtained from `registerProcess()`
- **Requirements**:
  - Must not be `NULL`
  - Must not be empty string
  - Must be a currently registered handle (not yet unregistered)
- **Example**: `"12345"` (daemon-assigned ID)

### `callback` (UpdateEventCallback)
- **Type**: Function pointer
- **Signature**: `void (*UpdateEventCallback)(const FwInfoData *fwinfodata)`
- **Description**: User-provided callback function invoked when the update check completes
- **Requirements**:
  - Must not be `NULL`
  - Must be thread-safe (called from background thread)
- **Execution Context**: Background GLib main loop thread (not the caller's thread)

---

## Return Value

### `CheckForUpdateResult` (enum)

| Value | Meaning |
|-------|---------|
| `CHECK_FOR_UPDATE_SUCCESS` (0) | API call initiated successfully; callback will be invoked later |
| `CHECK_FOR_UPDATE_FAIL` (1) | API call failed immediately (see error conditions below) |

**Important**: A return value of `CHECK_FOR_UPDATE_SUCCESS` only means the request was successfully queued. The actual result of the update check is delivered asynchronously via the callback.

---

## Callback Details

### UpdateEventCallback Function

```c
void updateEventCallback(const FwInfoData *fwinfodata);
```

#### Callback Parameters

##### `fwinfodata` (const FwInfoData*)

Pointer to a structure containing firmware update information:

```c
typedef struct {
    char CurrFWVersion[64];           // Current firmware version running on device
    UpdateDetails *UpdateDetails;      // Details of available update (may be NULL)
    CheckForUpdateStatus status;       // Result status code
} FwInfoData;
```

##### `CheckForUpdateStatus` (enum)

| Status | Value | Meaning |
|--------|-------|---------|
| `FIRMWARE_AVAILABLE` | 0 | New firmware is available for download |
| `FIRMWARE_NOT_AVAILABLE` | 1 | Device is already running the latest firmware |
| `UPDATE_NOT_ALLOWED` | 2 | Firmware update is not compatible with this device model |
| `FIRMWARE_CHECK_ERROR` | 3 | Error occurred while checking for updates (network, server, config) |
| `IGNORE_OPTOUT` | 4 | User has opted out and the update is blocked |
| `BYPASS_OPTOUT` | 5 | Update available but requires explicit user consent before installation |

##### `UpdateDetails` Structure

```c
typedef struct {
    char FwFileName[128];              // Firmware image filename (e.g., "image_v2.bin")
    char FwUrl[512];                   // Download URL for the firmware
    char FwVersion[64];                // Version string of new firmware (e.g., "2.1.0")
    char RebootImmediately[12];        // "true" or "false" - reboot requirement
    char DelayDownload[8];             // "true" or "false" - download delay flag
    char PDRIVersion[64];              // PDRI image version (if applicable)
    char PeripheralFirmwares[256];     // Peripheral firmware versions (if applicable)
} UpdateDetails;
```

**Note**: When `status` is not `FIRMWARE_AVAILABLE`, the `UpdateDetails` pointer may be `NULL` or contain empty/default values.

---

## Threading and Execution Model

### Call Flow Diagram

```
Application Thread                   Background Thread               Daemon Process
==================                   =================               ==============
                                                                    
checkForUpdate()                                                    
    |                                                               
    ├─ Validate handle                                              
    ├─ Validate callback                                            
    ├─ Connect to D-Bus                                             
    ├─ Register callback                                            
    |  in async registry                                            
    |                                                               
    ├─ Send D-Bus method ────────────────────────────────────────> CheckForUpdate(handle)
    |  "CheckForUpdate"                                             |
    |                                                               ├─ Query XConf server
    └─ Return SUCCESS                                               |  (5-30 seconds typical)
                                                                    |
    [Application continues]                                         ├─ Parse response
                                                                    |
                                                                    └─ Emit signal ─────────> CheckForUpdateComplete
                                                                                              |
                                        Signal arrives <─────────────────────────────────────┘
                                        on_check_complete_signal()
                                        |
                                        ├─ Parse FwInfoData
                                        ├─ Lookup callback in registry
                                        └─ Invoke ──────────────────> UpdateEventCallback(fwinfodata)
                                                                      |
                                                                      └─ [User code processes result]
```

### Key Timing Characteristics

- **API Call Duration**: < 10 milliseconds (just validates and sends D-Bus message)
- **Callback Delay**: Typically 5-30 seconds (depends on network and server response time)
- **Callback Thread**: Background GLib main loop thread (not caller's thread)

---

## Error Conditions

The API fails immediately (returns `CHECK_FOR_UPDATE_FAIL`) in these cases:

| Condition | Error Message Logged |
|-----------|----------------------|
| `handle` is `NULL` | "checkForUpdate: invalid handle (NULL or empty)" |
| `handle` is empty string | "checkForUpdate: invalid handle (NULL or empty)" |
| `callback` is `NULL` | "checkForUpdate: callback is NULL" |
| D-Bus connection fails | "checkForUpdate: D-Bus connect failed: \<reason\>" |
| Callback registry full | "checkForUpdate: registry full, handle='\<handle\>'" |

**Registry Full**: The internal callback registry has a fixed capacity (default: 64 entries). If all slots are occupied, the API call fails. This is extremely rare in normal operation.

---

## Usage Example

```c
#include "rdkFwupdateMgr_client.h"
#include <stdio.h>
#include <string.h>

/* Callback function invoked when update check completes */
void myUpdateCallback(const FwInfoData *info) {
    printf("=== Update Check Complete ===\n");
    printf("Current Version: %s\n", info->CurrFWVersion);
    
    switch (info->status) {
        case FIRMWARE_AVAILABLE:
            printf("Status: New firmware available\n");
            if (info->UpdateDetails) {
                printf("  New Version: %s\n", info->UpdateDetails->FwVersion);
                printf("  Filename: %s\n", info->UpdateDetails->FwFileName);
                printf("  URL: %s\n", info->UpdateDetails->FwUrl);
                printf("  Reboot Required: %s\n", info->UpdateDetails->RebootImmediately);
            }
            break;
            
        case FIRMWARE_NOT_AVAILABLE:
            printf("Status: Already on latest firmware\n");
            break;
            
        case UPDATE_NOT_ALLOWED:
            printf("Status: Update not allowed for this device\n");
            break;
            
        case FIRMWARE_CHECK_ERROR:
            printf("Status: Error checking for updates\n");
            break;
            
        case IGNORE_OPTOUT:
            printf("Status: Update blocked by user opt-out\n");
            break;
            
        case BYPASS_OPTOUT:
            printf("Status: Update available but requires user consent\n");
            break;
    }
}

int main() {
    /* Step 1: Register with daemon */
    FirmwareInterfaceHandle handle = registerProcess("MyApp", NULL);
    if (handle == NULL) {
        fprintf(stderr, "Failed to register\n");
        return 1;
    }
    
    printf("Registered with handle: %s\n", handle);
    
    /* Step 2: Check for updates */
    CheckForUpdateResult result = checkForUpdate(handle, myUpdateCallback);
    
    if (result == CHECK_FOR_UPDATE_SUCCESS) {
        printf("Update check initiated. Waiting for callback...\n");
        /* Callback will be invoked in background thread when check completes */
    } else {
        fprintf(stderr, "Failed to initiate update check\n");
    }
    
    /* Keep application running to receive callback */
    sleep(60);  /* In real app, use proper event loop */
    
    /* Step 3: Clean up */
    unregisterProcess(handle);
    
    return 0;
}
```

---

## Best Practices

### 1. **Don't Block in Callback**
The callback runs in a background thread. Avoid long-running operations:

```c
/* ❌ BAD - blocks background thread */
void badCallback(const FwInfoData *info) {
    sleep(10);  // Blocks other signal handlers!
    processUpdate(info);
}

/* ✅ GOOD - quick processing or async handoff */
void goodCallback(const FwInfoData *info) {
    if (info->status == FIRMWARE_AVAILABLE) {
        queueUpdateTask(info);  // Hand off to worker thread
    }
}
```

### 2. **Copy Data If Needed Later**
The `FwInfoData` pointer and its contents are only valid during the callback:

```c
void myCallback(const FwInfoData *info) {
    /* ❌ BAD - pointer becomes invalid after callback returns */
    globalPointer = info->UpdateDetails;
    
    /* ✅ GOOD - copy the data */
    if (info->UpdateDetails) {
        strncpy(savedVersion, info->UpdateDetails->FwVersion, sizeof(savedVersion));
        strncpy(savedUrl, info->UpdateDetails->FwUrl, sizeof(savedUrl));
    }
}
```

### 3. **Don't Call Library Functions from Callback**
Avoid re-entering the library from the callback (may cause deadlock or undefined behavior):

```c
/* ❌ BAD - calling library from callback */
void badCallback(const FwInfoData *info) {
    if (info->status == FIRMWARE_AVAILABLE) {
        downloadFirmware(handle, &req, downloadCallback);  // Re-entry risk!
    }
}

/* ✅ GOOD - signal main thread to call library */
void goodCallback(const FwInfoData *info) {
    if (info->status == FIRMWARE_AVAILABLE) {
        signalMainThread(UPDATE_AVAILABLE);  // Main thread calls downloadFirmware()
    }
}
```

### 4. **Handle All Status Codes**
Always check the `status` field before accessing `UpdateDetails`:

```c
void myCallback(const FwInfoData *info) {
    /* ✅ GOOD - defensive programming */
    if (info->status == FIRMWARE_AVAILABLE && info->UpdateDetails != NULL) {
        processUpdate(info->UpdateDetails);
    } else {
        handleNoUpdate(info->status);
    }
}
```

### 5. **Keep Application Alive**
The callback won't fire if your application exits before the daemon responds:

```c
/* ✅ GOOD - use event loop or wait mechanism */
checkForUpdate(handle, myCallback);
g_main_loop_run(mainLoop);  // Or other event loop

/* ❌ BAD - exits too early */
checkForUpdate(handle, myCallback);
return 0;  // App exits, callback never fires!
```

---

## Internal Implementation Details

### Race Condition Prevention

The callback is registered in the internal registry **before** sending the D-Bus method call:

```c
/* Sequence guarantees no missed signals */
1. Register callback in internal registry
2. Send D-Bus method call
3. Return to caller
```

This ensures that if the daemon responds quickly, the signal handler can find the callback.

### D-Bus Method Call Details

**Service**: `com.comcast.xconf_firmware_mgr`  
**Object Path**: `/com/comcast/xconf_firmware_mgr`  
**Interface**: `com.comcast.xconf_firmware_mgr`  
**Method**: `CheckForUpdate`  
**Arguments**: `(s)` - single string (handle)  
**Reply**: None (fire-and-forget)

### D-Bus Signal Details

**Signal Name**: `CheckForUpdateComplete`  
**Signature**: `(sa{ss}i)` - (string, dictionary, int32)  
**Arguments**:
- `s`: Current firmware version
- `a{ss}`: Dictionary of update details (keys mapped to UpdateDetails fields)
- `i`: Status code (CheckForUpdateStatus enum)

---

## Troubleshooting

### Problem: Callback Never Fires

**Possible Causes**:
1. Application exits before daemon responds (add event loop or sleep)
2. D-Bus daemon not running (check `systemctl status dbus`)
3. Firmware daemon not running (check `systemctl status rdkFwupdateMgr`)
4. Network connectivity issues (daemon can't reach XConf server)

**Debugging Steps**:
```bash
# Check daemon logs
journalctl -u rdkFwupdateMgr -f

# Monitor D-Bus traffic
dbus-monitor --system "type='method_call',interface='com.comcast.xconf_firmware_mgr'"
```

### Problem: Callback Fires with FIRMWARE_CHECK_ERROR

**Common Reasons**:
- XConf server unreachable (check network connectivity)
- Invalid XConf configuration (check `/etc/device.properties`)
- Server returned error response (check daemon logs for details)

---

## See Also

- [registerProcess API Documentation](API_DOCUMENTATION_registerProcess.md) - Required before calling checkForUpdate
- [unregisterProcess API Documentation](API_DOCUMENTATION_unregisterProcess.md) - Clean up when done
- [downloadFirmware API Documentation](API_DOCUMENTATION_downloadFirmware.md) - Next step after update available
- [Threading and Async Model](ASYNC_API_QUICK_REFERENCE.md) - Deep dive into async architecture

---

## Change History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2025-01-XX | Initial API documentation |
