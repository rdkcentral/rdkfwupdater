# CheckForUpdate API Documentation

## Overview

The `checkForUpdate()` API allows client applications to query the firmware update daemon for available firmware updates. This is the first step in the firmware update workflow.

## API Signature

```c
CheckForUpdateResult checkForUpdate(
    FirmwareInterfaceHandle handle,
    UpdateEventCallback callback
);
```

## Parameters

### `handle` (FirmwareInterfaceHandle)
- **Type**: String handle
- **Description**: Handler ID obtained from `registerProcess()`
- **Requirements**: Must be non-NULL and valid
- **Example**: `"12345"` (string representation of handler ID)

### `callback` (UpdateEventCallback)
- **Type**: Function pointer
- **Signature**: `void (*UpdateEventCallback)(const FwInfoData *fwinfodata)`
- **Description**: Callback function invoked when firmware check completes
- **Requirements**: Must be non-NULL
- **Thread Safety**: May be called from a background thread
- **Important**: Do NOT call other library functions from inside the callback

## Return Values

### `CheckForUpdateResult` enum
- **`CHECK_FOR_UPDATE_SUCCESS`** (0): API call started successfully
  - Callback will be invoked with firmware information
  - Return immediately after initiating check
- **`CHECK_FOR_UPDATE_FAIL`** (1): API call failed
  - Invalid handle (NULL or unregistered)
  - NULL callback
  - D-Bus communication error
  - Daemon not running

## Callback Parameters

### `FwInfoData` structure
```c
typedef struct {
    char CurrFWVersion[MAX_FW_VERSION_SIZE];  /* Current firmware version */
    UpdateDetails *UpdateDetails;              /* Update details (if available) */
    CheckForUpdateStatus status;               /* Status code */
} FwInfoData;
```

### `CheckForUpdateStatus` enum
- **`FIRMWARE_AVAILABLE`** (0): New firmware available for download
- **`FIRMWARE_NOT_AVAILABLE`** (1): Already on latest version
- **`UPDATE_NOT_ALLOWED`** (2): Firmware not compatible with device model
- **`FIRMWARE_CHECK_ERROR`** (3): Error occurred during check
- **`IGNORE_OPTOUT`** (4): Download blocked by user opt-out
- **`BYPASS_OPTOUT`** (5): Update requires explicit user consent

### `UpdateDetails` structure
```c
typedef struct {
    char FwFileName[MAX_FW_FILENAME_SIZE];              /* Filename */
    char FwUrl[MAX_FW_URL_SIZE];                        /* Download URL */
    char FwVersion[MAX_FW_VERSION_SIZE];                /* Version string */
    char RebootImmediately[MAX_REBOOT_IMMEDIATELY_SIZE]; /* "true" or "false" */
    char DelayDownload[MAX_DELAY_DOWNLOAD_SIZE];        /* "true" or "false" */
    char PDRIVersion[MAX_PDRI_VERSION_LEN];             /* PDRI version */
    char PeripheralFirmwares[MAX_PERIPHERAL_VERSION_LEN]; /* Peripheral versions */
} UpdateDetails;
```

## Usage Example

```c
#include "rdkFwupdateMgr_client.h"
#include <stdio.h>

// Callback function
void on_firmware_check(const FwInfoData *fwinfo) {
    if (!fwinfo) {
        printf("Error: NULL firmware info\n");
        return;
    }

    printf("Current Version: %s\n", fwinfo->CurrFWVersion);
    printf("Status: %d\n", fwinfo->status);

    if (fwinfo->status == FIRMWARE_AVAILABLE && fwinfo->UpdateDetails) {
        printf("New Version: %s\n", fwinfo->UpdateDetails->FwVersion);
        printf("Download URL: %s\n", fwinfo->UpdateDetails->FwUrl);
        printf("Filename: %s\n", fwinfo->UpdateDetails->FwFileName);
        
        // Next: Call downloadFirmware() to get the update
    } else if (fwinfo->status == FIRMWARE_NOT_AVAILABLE) {
        printf("Already on latest firmware\n");
    }
}

int main() {
    // Step 1: Register with daemon
    FirmwareInterfaceHandle handle = registerProcess("MyApp", "1.0");
    if (handle == NULL) {
        fprintf(stderr, "Registration failed\n");
        return 1;
    }

    // Step 2: Check for updates
    CheckForUpdateResult result = checkForUpdate(handle, on_firmware_check);
    if (result != CHECK_FOR_UPDATE_SUCCESS) {
        fprintf(stderr, "checkForUpdate failed\n");
        unregisterProcess(handle);
        return 1;
    }

    // Wait for callback (implementation-specific)
    sleep(2);

    // Step 3: Cleanup
    unregisterProcess(handle);
    return 0;
}
```

## Workflow

```
┌─────────────────────────────────────────────────────────────────┐
│                      checkForUpdate Flow                         │
└─────────────────────────────────────────────────────────────────┘

1. Client Application
   └── checkForUpdate(handle, callback)
       │
       ├─→ Validate parameters (handle, callback)
       ├─→ Store callback in registry
       ├─→ Create D-Bus proxy
       │
2. D-Bus Communication
   └── Call CheckForUpdate method
       ├─→ Send: handler_process_name (string)
       ├─→ Receive: (result, version, availableVersion, 
       │             updateDetails, status, status_code)
       │
3. Daemon Processing
   └── rdkFwupdateMgr daemon
       ├─→ Validate handler is registered
       ├─→ Check XConf cache (/tmp/xconf_response_thunder.txt)
       ├─→ If cache hit: Parse and validate firmware
       ├─→ If cache miss: Fetch from XConf server
       ├─→ Compare current vs available version
       ├─→ Return firmware info
       │
4. Client Library
   └── Parse D-Bus response
       ├─→ Build FwInfoData structure
       ├─→ Parse UpdateDetails from string
       ├─→ Invoke callback immediately
       ├─→ Free allocated memory
       │
5. Client Callback
   └── on_firmware_check(fwinfo)
       ├─→ Check status code
       ├─→ If FIRMWARE_AVAILABLE: Proceed to download
       ├─→ If FIRMWARE_NOT_AVAILABLE: No action
       ├─→ If error: Handle error condition
```

## D-Bus Protocol Details

### Method Call
- **Method Name**: `CheckForUpdate`
- **Input Args**:
  - `handler_process_name` (string): Handler ID from registration
- **Output Args**:
  - `result` (int32): API result (0=SUCCESS, 1=FAIL)
  - `fwdata_version` (string): Current firmware version
  - `fwdata_availableVersion` (string): Available version from server
  - `fwdata_updateDetails` (string): Comma-separated key:value pairs
  - `fwdata_status` (string): Human-readable status message
  - `fwdata_status_code` (int32): Status code (0-5)

### D-Bus Interface
- **Service Name**: `org.rdkfwupdater.Interface`
- **Object Path**: `/org/rdkfwupdater/Service`
- **Interface**: `org.rdkfwupdater.Interface`
- **Timeout**: 30 seconds

## Error Handling

### Common Errors

| Error Scenario | Return Value | Callback Invoked? | Action |
|---|---|---|---|
| NULL handle | `CHECK_FOR_UPDATE_FAIL` | No | Check registration |
| NULL callback | `CHECK_FOR_UPDATE_FAIL` | No | Provide valid callback |
| Daemon not running | `CHECK_FOR_UPDATE_FAIL` | No | Start daemon |
| D-Bus error | `CHECK_FOR_UPDATE_FAIL` | No | Check logs |
| Invalid handler ID | `CHECK_FOR_UPDATE_SUCCESS` | Yes (ERROR status) | Re-register |
| XConf fetch error | `CHECK_FOR_UPDATE_SUCCESS` | Yes (ERROR status) | Retry later |

### Debugging Tips

```bash
# Check daemon status
systemctl status rdkFwupdateMgr.service

# View daemon logs
tail -f /opt/logs/rdkFwupdateMgr.log

# Monitor D-Bus traffic
dbus-monitor --system "sender='org.rdkfwupdater.Interface'"

# Check library logs
tail -f /opt/logs/rdkFwupdateMgr.log | grep "librdkFwupdateMgr"

# Verify registration
# (daemon logs will show RegisterProcess call)
```

## Thread Safety

- **API Call**: Thread-safe (GDBus handles synchronization)
- **Callback**: May be invoked from library thread
- **Data Lifetime**: FwInfoData valid only during callback
- **Reentrancy**: Do NOT call checkForUpdate() from inside callback

### Best Practices

```c
// ✅ GOOD: Copy data if needed later
void on_update(const FwInfoData *fwinfo) {
    char version[MAX_FW_VERSION_SIZE];
    strncpy(version, fwinfo->CurrFWVersion, sizeof(version)-1);
    version[sizeof(version)-1] = '\0';
    // Now 'version' can be used after callback returns
}

// ❌ BAD: Don't store pointer to callback data
void on_update(const FwInfoData *fwinfo) {
    g_saved_fwinfo = fwinfo;  // DANGLING POINTER after callback!
}

// ❌ BAD: Don't call other APIs from callback
void on_update(const FwInfoData *fwinfo) {
    if (fwinfo->status == FIRMWARE_AVAILABLE) {
        downloadFirmware(handle, ...);  // DON'T DO THIS!
    }
}
```

## Performance Considerations

- **Typical Latency**: 100-500ms (cache hit), 1-5s (cache miss + XConf fetch)
- **Blocking**: Synchronous D-Bus call (blocks until daemon responds)
- **Cache**: Daemon caches XConf responses for fast subsequent checks
- **Network**: XConf server fetch happens on daemon side (not client)

## Integration with Firmware Update Workflow

```c
// Complete firmware update workflow
void firmware_update_workflow() {
    // 1. Register
    FirmwareInterfaceHandle handle = registerProcess("MyApp", "1.0");
    
    // 2. Check for updates
    checkForUpdate(handle, on_update_callback);
    
    // 3. Download (if available) - triggered from callback
    //    downloadFirmware(handle, &req, on_download_callback);
    
    // 4. Flash (after download completes)
    //    updateFirmware(handle, &req, on_flash_callback);
    
    // 5. Unregister
    unregisterProcess(handle);
}
```

## See Also

- [`registerProcess()`](rdkFwupdateMgr_process.h) - Process registration
- [`downloadFirmware()`](rdkFwupdateMgr_client.h) - Download firmware image
- [`updateFirmware()`](rdkFwupdateMgr_client.h) - Flash firmware to device
- [Build and Test Guide](BUILD_AND_TEST.md) - Compilation instructions
- [Quick Reference](../QUICK_REFERENCE.md) - API quick reference
