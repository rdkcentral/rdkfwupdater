# CheckForUpdate API Documentation

> **📋 Documentation Status**: This documentation is **current and accurate** as of 2024. The implementation has been updated to match the industry-standard async pattern described here (callback fires once, 5s timeout for daemon ACK only).

## Overview

The `checkForUpdate()` API provides an **asynchronous** method for client applications to query the firmware update daemon for available firmware updates. This is the first step in the firmware update workflow.

### Key Characteristics

- **Asynchronous Design**: Returns immediately after initiating the check; result delivered via callback
- **Non-Blocking**: Does not wait for XConf server queries (daemon handles that independently)
- **Single Callback**: Callback fires **once** with the actual result (industry-standard async pattern)
- **Thread-Safe**: Can be called from any thread; callback may execute on a background thread
- **Long-Running Backend**: XConf queries can take up to 2 hours; library only waits for daemon ACK (~5s)

## API Signature

```c
CheckForUpdateResult checkForUpdate(
    FirmwareInterfaceHandle handle,
    UpdateEventCallback callback
);
```

### Behavior

1. **Validates** handle and callback parameters
2. **Registers** the callback in the internal registry
3. **Sends** D-Bus method call to daemon (synchronous, ~5s timeout)
4. **Returns immediately** with SUCCESS/FAIL based on daemon ACK
5. **Callback fires later** (asynchronously) when daemon completes XConf query

> **⚠️ Important**: The callback fires **only once** with the real result. Do NOT expect an immediate "check in progress" callback.

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

| Value | Meaning | Callback Fired? | Description |
|-------|---------|----------------|-------------|
| **`CHECK_FOR_UPDATE_SUCCESS`** (0) | Request accepted | **Yes (later)** | Daemon ACK received; callback will fire when XConf query completes |
| **`CHECK_FOR_UPDATE_FAIL`** (1) | Request rejected | **No** | Invalid parameters, D-Bus error, or daemon not running |

### What SUCCESS Means

- Daemon has **acknowledged** the request
- Callback **will be invoked** when the firmware check completes
- The check may take **seconds to hours** depending on XConf server response time
- The library has returned; your code can continue

### What FAIL Means

Common failure reasons:
- **NULL handle**: Handle not obtained from `registerProcess()`
- **NULL callback**: Must provide a valid callback function
- **Unregistered handle**: Handle was never registered or already unregistered
- **D-Bus communication error**: Daemon not responding or D-Bus not available
- **Daemon not running**: `rdkFwupdateMgr.service` is not active

## Callback Mechanism

### Callback Signature

```c
typedef void (*UpdateEventCallback)(const FwInfoData *fwinfodata);
```

### When Does the Callback Fire?

The callback fires **exactly once** when the daemon completes the firmware check:

1. **Daemon queries XConf** (can take 1s to 2 hours)
2. **Daemon parses response** and determines firmware availability
3. **Daemon sends D-Bus signal** (`UpdateEventSignal`)
4. **Library catches signal** in background thread (GLib main loop)
5. **Library invokes your callback** with the `FwInfoData` structure

### Threading Model

- **Callback thread**: Background thread managed by GLib (`g_main_context_iteration`)
- **Not safe to call APIs**: Do NOT call `checkForUpdate()`, `downloadFirmware()`, etc. from inside the callback
- **Data lifetime**: `FwInfoData` is valid **only during the callback**; copy data if you need it later

### Callback Registration

Internally, the library maintains a callback registry:

```c
// Registry entry (one per handle)
typedef struct {
    char *handler_id;           // Handle (e.g., "12345")
    UpdateEventCallback func;   // Your callback function
    // ... other fields ...
} CallbackEntry;
```

- **One callback per handle**: Subsequent calls to `checkForUpdate()` replace the previous callback
- **Automatic cleanup**: Callback removed after firing or on `unregisterProcess()`
- **Signal routing**: D-Bus signal includes handler ID; library uses it to find the correct callback

## Callback Data Structure

### `FwInfoData` Structure

```c
typedef struct {
    char CurrFWVersion[MAX_FW_VERSION_SIZE];  /* Current firmware version (e.g., "1.2.3") */
    UpdateDetails *UpdateDetails;              /* Pointer to update details (NULL if no update) */
    CheckForUpdateStatus status;               /* Status code indicating result */
} FwInfoData;
```

**Key Points:**
- `CurrFWVersion`: Always populated with the device's current firmware version
- `UpdateDetails`: **Only non-NULL** if `status == FIRMWARE_AVAILABLE`
- `status`: Determines how to interpret the result (see below)

### `CheckForUpdateStatus` Enum

| Value | Code | Meaning | UpdateDetails? |
|-------|------|---------|----------------|
| **`FIRMWARE_AVAILABLE`** | 0 | New firmware available | **Yes** (non-NULL) |
| **`FIRMWARE_NOT_AVAILABLE`** | 1 | Already on latest version | No (NULL) |
| **`UPDATE_NOT_ALLOWED`** | 2 | Firmware incompatible with device model | No (NULL) |
| **`FIRMWARE_CHECK_ERROR`** | 3 | Error during XConf query or parsing | No (NULL) |
| **`IGNORE_OPTOUT`** | 4 | Download blocked by user opt-out setting | No (NULL) |
| **`BYPASS_OPTOUT`** | 5 | Update available but requires user consent | Maybe |

### `UpdateDetails` Structure

```c
typedef struct {
    char FwFileName[MAX_FW_FILENAME_SIZE];              /* Firmware image filename */
    char FwUrl[MAX_FW_URL_SIZE];                        /* HTTP/HTTPS download URL */
    char FwVersion[MAX_FW_VERSION_SIZE];                /* Target firmware version */
    char RebootImmediately[MAX_REBOOT_IMMEDIATELY_SIZE]; /* "true" or "false" */
    char DelayDownload[MAX_DELAY_DOWNLOAD_SIZE];        /* "true" or "false" */
    char PDRIVersion[MAX_PDRI_VERSION_LEN];             /* PDRI version (optional) */
    char PeripheralFirmwares[MAX_PERIPHERAL_VERSION_LEN]; /* Peripheral FW (optional) */
} UpdateDetails;
```

**Field Descriptions:**
- `FwFileName`: Name of the firmware image file (e.g., `"device_v2.0.bin"`)
- `FwUrl`: Full URL to download the firmware (e.g., `"https://xconf.example.com/fw/..."`)
- `FwVersion`: Target version string (e.g., `"2.0.1"`)
- `RebootImmediately`: Whether device should reboot after flash (`"true"` / `"false"`)
- `DelayDownload`: Whether to defer download to maintenance window (`"true"` / `"false"`)
- `PDRIVersion`: PDRI (Peripheral Device Reboot Info) version, if applicable
- `PeripheralFirmwares`: Comma-separated list of peripheral firmware versions

## Usage Example

### Basic Example

```c
#include "rdkFwupdateMgr_client.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// Global state for async operation
static volatile int update_check_complete = 0;
static FwInfoData saved_fwinfo;

// Callback function (runs in background thread)
void on_firmware_check(const FwInfoData *fwinfo) {
    if (!fwinfo) {
        fprintf(stderr, "ERROR: NULL firmware info in callback\n");
        update_check_complete = 1;
        return;
    }

    printf("\n=== Firmware Check Complete ===\n");
    printf("Current Version: %s\n", fwinfo->CurrFWVersion);
    printf("Status Code: %d\n", fwinfo->status);

    // Copy data before callback returns (FwInfoData is temporary!)
    strncpy(saved_fwinfo.CurrFWVersion, fwinfo->CurrFWVersion, 
            sizeof(saved_fwinfo.CurrFWVersion) - 1);
    saved_fwinfo.status = fwinfo->status;

    switch (fwinfo->status) {
        case FIRMWARE_AVAILABLE:
            if (fwinfo->UpdateDetails) {
                printf("✓ New Version Available: %s\n", 
                       fwinfo->UpdateDetails->FwVersion);
                printf("  Download URL: %s\n", fwinfo->UpdateDetails->FwUrl);
                printf("  Filename: %s\n", fwinfo->UpdateDetails->FwFileName);
                printf("  Reboot Immediately: %s\n", 
                       fwinfo->UpdateDetails->RebootImmediately);
                
                // Next step: Call downloadFirmware() from main thread
            } else {
                fprintf(stderr, "WARNING: FIRMWARE_AVAILABLE but UpdateDetails is NULL\n");
            }
            break;

        case FIRMWARE_NOT_AVAILABLE:
            printf("✓ Already on latest firmware version\n");
            break;

        case UPDATE_NOT_ALLOWED:
            printf("✗ Firmware update not allowed for this device model\n");
            break;

        case FIRMWARE_CHECK_ERROR:
            printf("✗ Error occurred during firmware check\n");
            break;

        case IGNORE_OPTOUT:
            printf("⚠ Download blocked by user opt-out setting\n");
            break;

        case BYPASS_OPTOUT:
            printf("⚠ Update requires explicit user consent\n");
            break;

        default:
            printf("? Unknown status code: %d\n", fwinfo->status);
            break;
    }

    update_check_complete = 1;
}

int main() {
    FirmwareInterfaceHandle handle = NULL;
    CheckForUpdateResult result;

    // Step 1: Register with daemon
    printf("Registering with firmware daemon...\n");
    handle = registerProcess("ExampleApp", "1.0.0");
    if (handle == NULL) {
        fprintf(stderr, "FAIL: registerProcess() returned NULL\n");
        return 1;
    }
    printf("SUCCESS: Registered with handle: %s\n", handle);

    // Step 2: Check for updates (asynchronous)
    printf("\nInitiating firmware check...\n");
    result = checkForUpdate(handle, on_firmware_check);
    
    if (result != CHECK_FOR_UPDATE_SUCCESS) {
        fprintf(stderr, "FAIL: checkForUpdate() returned %d\n", result);
        unregisterProcess(handle);
        return 1;
    }
    printf("SUCCESS: Firmware check initiated (callback will fire when ready)\n");

    // Step 3: Wait for callback (with timeout)
    printf("Waiting for callback");
    int timeout_seconds = 120;  // 2 minutes (adjust based on your needs)
    for (int i = 0; i < timeout_seconds && !update_check_complete; i++) {
        sleep(1);
        if (i % 5 == 0) printf(".");
        fflush(stdout);
    }
    printf("\n");

    if (!update_check_complete) {
        fprintf(stderr, "TIMEOUT: Callback did not fire within %d seconds\n", 
                timeout_seconds);
        // Note: Callback may still fire later; cleanup required
    }

    // Step 4: Process result (if callback fired)
    if (update_check_complete && saved_fwinfo.status == FIRMWARE_AVAILABLE) {
        printf("\nNext step: Call downloadFirmware() to get the update\n");
        // Example: downloadFirmware(handle, &req, on_download_callback);
    }

    // Step 5: Cleanup
    printf("\nUnregistering from daemon...\n");
    unregisterProcess(handle);
    printf("Done.\n");

    return 0;
}
```


## Asynchronous Workflow

### Overview

The `checkForUpdate()` API follows the **industry-standard async pattern**:

1. **Request Phase**: Client calls `checkForUpdate()` → returns immediately with SUCCESS/FAIL
2. **Processing Phase**: Daemon queries XConf (background, can take up to 2 hours)
3. **Result Phase**: Daemon sends D-Bus signal → Library fires callback **once** with actual result

### Detailed Flow Diagram

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                       checkForUpdate() Async Flow                             │
└──────────────────────────────────────────────────────────────────────────────┘

╔═══════════════════════════════════════════════════════════════════════════╗
║ PHASE 1: INITIATE REQUEST (Synchronous, ~5s)                             ║
╚═══════════════════════════════════════════════════════════════════════════╝

Client Application Thread:
  │
  ├─▶ checkForUpdate(handle, callback)
  │   │
  │   ├─▶ [1] Validate handle and callback
  │   ├─▶ [2] Register callback in internal registry
  │   ├─▶ [3] Create D-Bus proxy for daemon
  │   ├─▶ [4] Call D-Bus method: CheckForUpdate(handler_id)
  │   │       ├─ Timeout: 5000ms (library waits for daemon ACK only)
  │   │       ├─ Blocking: YES (but short-lived)
  │   │       └─ Result: Daemon acknowledges "request accepted"
  │   │
  │   └─▶ [5] Return CHECK_FOR_UPDATE_SUCCESS
  │
  └─▶ Application continues (callback will fire later)

─────────────────────────────────────────────────────────────────────────────

╔═══════════════════════════════════════════════════════════════════════════╗
║ PHASE 2: DAEMON PROCESSING (Asynchronous, 1s to 2 hours)                 ║
╚═══════════════════════════════════════════════════════════════════════════╝

rdkFwupdateMgr Daemon (separate process):
  │
  ├─▶ Receive CheckForUpdate D-Bus call
  │   │
  │   ├─▶ [1] Validate handler is registered
  │   ├─▶ [2] Check cache: /tmp/xconf_response_thunder.txt
  │   │       ├─ Cache hit: Use cached data (fast, ~100ms)
  │   │       └─ Cache miss: Query XConf server (slow, 1s to 2 hours)
  │   │
  │   ├─▶ [3] Parse XConf response (JSON)
  │   │       ├─ Extract: firmware URL, version, filename
  │   │       ├─ Compare: current version vs available version
  │   │       └─ Determine status (AVAILABLE, NOT_AVAILABLE, etc.)
  │   │
  │   ├─▶ [4] Send ACK to client library (immediate)
  │   │       └─ This is what checkForUpdate() waits for (~5s)
  │   │
  │   └─▶ [5] Emit D-Bus signal: UpdateEventSignal
  │           ├─ Signal name: "UpdateEventSignal"
  │           ├─ Parameters: (handler_id, fwinfo JSON)
  │           └─ Broadcast to all library instances
  │
  └─▶ Daemon continues monitoring

─────────────────────────────────────────────────────────────────────────────

╔═══════════════════════════════════════════════════════════════════════════╗
║ PHASE 3: CALLBACK INVOCATION (Asynchronous)                              ║
╚═══════════════════════════════════════════════════════════════════════════╝

Library Background Thread (GLib main loop):
  │
  ├─▶ Catch D-Bus signal: UpdateEventSignal
  │   │
  │   ├─▶ [1] Extract handler_id from signal parameters
  │   ├─▶ [2] Lookup callback in registry using handler_id
  │   ├─▶ [3] Parse fwinfo JSON into FwInfoData structure
  │   │       ├─ Allocate UpdateDetails (if FIRMWARE_AVAILABLE)
  │   │       └─ Populate all fields
  │   │
  │   ├─▶ [4] Invoke callback: callback(&fwinfo)
  │   │       ├─ Thread: Background thread (NOT main thread)
  │   │       ├─ Timing: Once XConf query completes
  │   │       └─ Count: EXACTLY ONCE per checkForUpdate() call
  │   │
  │   ├─▶ [5] Free FwInfoData after callback returns
  │   └─▶ [6] Remove callback from registry
  │
  └─▶ Library continues event loop

─────────────────────────────────────────────────────────────────────────────

Client Callback Function:
  │
  ├─▶ on_firmware_check(fwinfo)
  │   │
  │   ├─▶ [1] Check fwinfo->status
  │   ├─▶ [2] If FIRMWARE_AVAILABLE: Copy UpdateDetails
  │   ├─▶ [3] Set flag/signal for main thread
  │   └─▶ [4] Return (DO NOT call other library APIs)
  │
  └─▶ Main thread resumes based on flag/signal
```

### Key Timing Characteristics

| Phase | Duration | Blocking? | Thread |
|-------|----------|-----------|--------|
| `checkForUpdate()` call | ~5s | Yes (D-Bus call) | Caller's thread |
| Daemon XConf query | 1s to 2 hours | No (daemon handles it) | Daemon process |
| Callback invocation | <1ms | No (async signal) | Library background thread |

### Important Notes

1. **Single Callback**: Your callback fires **exactly once** when the daemon completes the check
2. **No Progress Updates**: You do NOT receive "check in progress" notifications
3. **Library Returns Fast**: `checkForUpdate()` blocks only for ~5s (daemon ACK), not for XConf query
4. **Daemon Handles Long Wait**: XConf queries (up to 2 hours) happen in daemon, not library
5. **Thread Safety**: Callback executes in a background thread; use proper synchronization

## D-Bus Protocol Details

### D-Bus Method Call

The library uses synchronous D-Bus method calls to communicate with the daemon:

- **Service Name**: `org.rdkfwupdater.Interface`
- **Object Path**: `/org/rdkfwupdater/Service`
- **Interface**: `org.rdkfwupdater.Interface`
- **Method Name**: `CheckForUpdate`
- **Timeout**: 5000ms (5 seconds) - waits for daemon ACK only

**Input Arguments:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `handler_process_name` | string | Handler ID obtained from `registerProcess()` |

**Output Arguments:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `result` | int32 | API result (0=SUCCESS, 1=FAIL) |
| `fwdata_version` | string | Current firmware version (may be empty if error) |
| `fwdata_availableVersion` | string | Available version from XConf (empty if not available) |
| `fwdata_updateDetails` | string | Comma-separated key:value pairs (UpdateDetails fields) |
| `fwdata_status` | string | Human-readable status message |
| `fwdata_status_code` | int32 | Status code (0-5, see `CheckForUpdateStatus` enum) |

### D-Bus Signal (Async Result)

After processing the XConf query, the daemon emits a signal to notify the library:

- **Signal Name**: `UpdateEventSignal`
- **Interface**: `org.rdkfwupdater.Interface`
- **Broadcast**: System bus (all clients receive it)

**Signal Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `handler_id` | string | Handler ID (library uses this to route to correct callback) |
| `fwinfo_json` | string | JSON-encoded FwInfoData (status, version, UpdateDetails) |

**Signal Handling:**
1. Library subscribes to `UpdateEventSignal` during initialization
2. Background thread (GLib main loop) catches all signals
3. Library filters by `handler_id` to match registered callbacks
4. Library parses JSON and invokes the application's callback

### Why Two Communication Paths?

| Mechanism | Purpose | When | Timeout |
|-----------|---------|------|---------|
| **D-Bus Method Call** | Request initiation + ACK | Synchronous (during `checkForUpdate()` call) | 5s |
| **D-Bus Signal** | Async result delivery | Asynchronous (after XConf query completes) | N/A (signal) |

This design allows the library to return quickly while the daemon handles long-running XConf queries in the background.

## Timeout and Error Handling

### Library-Side Timeouts

| Operation | Timeout | Configurable? | What Happens on Timeout |
|-----------|---------|---------------|-------------------------|
| D-Bus method call | 5000ms | Yes (in code) | Returns `CHECK_FOR_UPDATE_FAIL` |
| Callback wait | N/A | **App-level** | App must implement its own timeout logic |

**Recommended App-Level Timeout:**
- **Normal case**: 60-120 seconds (for typical XConf queries)
- **Worst case**: Up to 2 hours (if XConf server is slow/unavailable)
- **Production**: Implement exponential backoff retry with user notification

### Common Error Scenarios

| Error Scenario | Detected When | Return Value | Callback Invoked? | Recommended Action |
|----------------|---------------|--------------|-------------------|--------------------|
| NULL handle | `checkForUpdate()` call | `FAIL` | No | Verify `registerProcess()` succeeded |
| NULL callback | `checkForUpdate()` call | `FAIL` | No | Provide valid callback function |
| Unregistered handle | `checkForUpdate()` call | `FAIL` | No | Call `registerProcess()` first |
| Daemon not running | `checkForUpdate()` call | `FAIL` | No | Start `rdkFwupdateMgr.service` |
| D-Bus timeout (5s) | `checkForUpdate()` call | `FAIL` | No | Check daemon health, logs |
| Invalid handler (daemon) | Callback | `SUCCESS` | Yes (status=ERROR) | Re-register with daemon |
| XConf server error | Callback | `SUCCESS` | Yes (status=ERROR) | Retry later, check network |
| XConf timeout (2h) | Callback | `SUCCESS` | Yes (status=ERROR) | May indicate server issue |
| No callback after 2h | App-level timeout | `SUCCESS` | No | Assume daemon/XConf failure, retry |

### Error Handling Best Practices

```c
// ✅ GOOD: Handle all error paths
CheckForUpdateResult result = checkForUpdate(handle, on_update);
if (result != CHECK_FOR_UPDATE_SUCCESS) {
    fprintf(stderr, "checkForUpdate failed - daemon issue\n");
    // Take corrective action: check logs, retry, alert user
    return;
}

// Set up app-level timeout (callback might never fire)
int timeout_sec = 120;
for (int i = 0; i < timeout_sec && !callback_fired; i++) {
    sleep(1);
}

if (!callback_fired) {
    fprintf(stderr, "Callback timeout - XConf issue likely\n");
    // Take corrective action: retry, alert user
}

// ❌ BAD: Assume callback always fires
checkForUpdate(handle, on_update);
while (!callback_fired) {
    sleep(1);  // Infinite wait - dangerous!
}
```

### Debugging Commands

```bash
# Check daemon status (systemd)
systemctl status rdkFwupdateMgr.service

# View daemon logs
tail -f /opt/logs/rdkFwupdateMgr.log

# Monitor D-Bus traffic (method calls and signals)
dbus-monitor --system "interface='org.rdkfwupdater.Interface'"

# Check library logs (if available)
tail -f /opt/logs/rdkFwupdateMgr.log | grep "librdkFwupdateMgr"

# Verify handler registration (daemon logs)
grep "RegisterProcess" /opt/logs/rdkFwupdateMgr.log

# Check XConf cache
cat /tmp/xconf_response_thunder.txt

# Test D-Bus connection
dbus-send --system --print-reply \
  --dest=org.rdkfwupdater.Interface \
  /org/rdkfwupdater/Service \
  org.freedesktop.DBus.Introspectable.Introspect
```


## Thread Safety and Concurrency

### Thread Model

| Component | Thread | Notes |
|-----------|--------|-------|
| `checkForUpdate()` call | Caller's thread | Blocks for ~5s (D-Bus method call) |
| D-Bus signal reception | GLib main loop thread | Background thread managed by library |
| Callback invocation | GLib main loop thread | **NOT** the caller's thread |
| `unregisterProcess()` | Caller's thread | Cleans up callback registry |

### Thread Safety Guarantees

1. **Calling `checkForUpdate()`**: Thread-safe (multiple threads can call simultaneously)
2. **Callback execution**: Thread-safe (library ensures serialization per handle)
3. **Data access**: `FwInfoData` is **read-only** during callback (no modifications allowed)
4. **Reentrancy**: **NOT reentrant** - do NOT call `checkForUpdate()` from inside the callback

### Data Lifetime Rules

| Data | Lifetime | Ownership | What To Do |
|------|----------|-----------|------------|
| `FwInfoData *fwinfo` | Callback duration only | Library | **Copy** data if needed later |
| `UpdateDetails *` | Callback duration only | Library | **Copy** fields before callback returns |
| `handle` | Until `unregisterProcess()` | Application | Can be used after callback |

### Safe Data Handling Patterns

```c
// ✅ CORRECT: Copy data out of callback
typedef struct {
    char current_version[64];
    char new_version[64];
    char download_url[256];
    int status;
    pthread_mutex_t lock;
} AppFirmwareState;

AppFirmwareState g_fw_state;

void on_update_check(const FwInfoData *fwinfo) {
    pthread_mutex_lock(&g_fw_state.lock);
    
    // Copy scalar values
    g_fw_state.status = fwinfo->status;
    strncpy(g_fw_state.current_version, fwinfo->CurrFWVersion, 
            sizeof(g_fw_state.current_version) - 1);
    
    // Copy UpdateDetails if available
    if (fwinfo->status == FIRMWARE_AVAILABLE && fwinfo->UpdateDetails) {
        strncpy(g_fw_state.new_version, fwinfo->UpdateDetails->FwVersion,
                sizeof(g_fw_state.new_version) - 1);
        strncpy(g_fw_state.download_url, fwinfo->UpdateDetails->FwUrl,
                sizeof(g_fw_state.download_url) - 1);
    }
    
    pthread_mutex_unlock(&g_fw_state.lock);
    
    // Signal main thread (semaphore, condition variable, etc.)
    sem_post(&update_complete_sem);
}

// Main thread waits
sem_wait(&update_complete_sem);
pthread_mutex_lock(&g_fw_state.lock);
if (g_fw_state.status == FIRMWARE_AVAILABLE) {
    printf("Update available: %s\n", g_fw_state.new_version);
}
pthread_mutex_unlock(&g_fw_state.lock);
```

```c
// ❌ INCORRECT: Storing pointers to callback data
const FwInfoData *g_saved_fwinfo = NULL;  // DANGER!

void on_update_check(const FwInfoData *fwinfo) {
    g_saved_fwinfo = fwinfo;  // BAD: Pointer becomes invalid after callback!
}

// Later in main thread...
if (g_saved_fwinfo->status == FIRMWARE_AVAILABLE) {  // CRASH or garbage data!
    // ...
}
```

```c
// ❌ INCORRECT: Calling library APIs from callback
void on_update_check(const FwInfoData *fwinfo) {
    if (fwinfo->status == FIRMWARE_AVAILABLE) {
        // WRONG: May cause deadlock or unexpected behavior
        DownloadRequest req = { /* ... */ };
        downloadFirmware(g_handle, &req, on_download_complete);
    }
}

// ✅ CORRECT: Signal main thread to proceed
void on_update_check(const FwInfoData *fwinfo) {
    if (fwinfo->status == FIRMWARE_AVAILABLE) {
        // Copy data, set flag, and let main thread handle next API call
        save_firmware_info(fwinfo);
        g_proceed_to_download = 1;
    }
}
```

### Multiple Concurrent Checks

Can you call `checkForUpdate()` multiple times for the same handle?

**Answer**: Yes, but **not recommended**. Each call replaces the previous callback:

```c
// Handle #1
checkForUpdate(handle1, callback_A);  // callback_A registered
checkForUpdate(handle1, callback_B);  // callback_A replaced by callback_B!

// Only callback_B will fire for handle1
```

**Best Practice**: Wait for the first callback before issuing another check.

## Performance and Optimization

### Typical Performance Characteristics

| Scenario | Library Latency | Daemon Processing | Callback Latency | Total Time |
|----------|----------------|-------------------|------------------|------------|
| **Cache hit** | ~5s | <100ms | <1ms | ~5s |
| **Cache miss (fast XConf)** | ~5s | 1-5s | <1ms | 6-10s |
| **Cache miss (slow XConf)** | ~5s | 10s-2h | <1ms | 15s-2h |
| **XConf timeout** | ~5s | ~2h | <1ms | ~2h |

### Caching Behavior

The daemon caches XConf responses to improve performance:

- **Cache File**: `/tmp/xconf_response_thunder.txt`
- **Cache Duration**: Daemon-configurable (default: 24 hours)
- **Cache Invalidation**: Manual deletion or daemon restart
- **Cache Hit**: Daemon returns result in <100ms (no XConf query)
- **Cache Miss**: Daemon queries XConf server (1s to 2 hours)

### Optimization Tips

1. **Check once, cache result**: Don't call `checkForUpdate()` repeatedly
2. **Implement exponential backoff**: If XConf is slow, increase retry intervals
3. **User notification**: Inform users that checks may take time (especially first run)
4. **Background checks**: Run `checkForUpdate()` in a maintenance window, not on user interaction
5. **Timeout strategy**: Implement reasonable timeouts (2 minutes for user-triggered, longer for background)

### Network Considerations

- **XConf Server**: External dependency; latency varies by region and server load
- **No local data**: Daemon must query XConf for each cache miss
- **Offline mode**: If network unavailable, expect `FIRMWARE_CHECK_ERROR` status in callback

## Integration with Firmware Update Workflow

### Complete Workflow Example

```c
#include "rdkFwupdateMgr_client.h"
#include <stdio.h>
#include <semaphore.h>

// Global state
FirmwareInterfaceHandle g_handle = NULL;
sem_t g_check_complete;
int g_firmware_available = 0;
char g_firmware_url[256];

// Step 1: Callback for firmware check
void on_check_complete(const FwInfoData *fwinfo) {
    if (fwinfo && fwinfo->status == FIRMWARE_AVAILABLE && fwinfo->UpdateDetails) {
        g_firmware_available = 1;
        strncpy(g_firmware_url, fwinfo->UpdateDetails->FwUrl, sizeof(g_firmware_url) - 1);
    }
    sem_post(&g_check_complete);
}

// Step 2: Callback for download progress
void on_download_progress(const DownloadStatusData *status) {
    printf("Download: %d%% complete\n", status->percentage);
}

// Step 3: Callback for flash completion
void on_flash_complete(const UpdateStatusData *status) {
    printf("Flash status: %s\n", status->status_message);
}

int main() {
    sem_init(&g_check_complete, 0, 0);
    
    // 1. Register with daemon
    g_handle = registerProcess("MyFirmwareApp", "2.0");
    if (!g_handle) {
        fprintf(stderr, "Failed to register\n");
        return 1;
    }
    
    // 2. Check for updates
    if (checkForUpdate(g_handle, on_check_complete) != CHECK_FOR_UPDATE_SUCCESS) {
        fprintf(stderr, "Failed to initiate firmware check\n");
        unregisterProcess(g_handle);
        return 1;
    }
    
    // 3. Wait for callback (with timeout)
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 120;  // 2 minute timeout
    
    if (sem_timedwait(&g_check_complete, &timeout) != 0) {
        fprintf(stderr, "Timeout waiting for firmware check\n");
        unregisterProcess(g_handle);
        return 1;
    }
    
    // 4. If firmware available, download it
    if (g_firmware_available) {
        printf("Downloading firmware from: %s\n", g_firmware_url);
        
        DownloadRequest download_req = {
            .url = g_firmware_url,
            // ... other fields ...
        };
        
        if (downloadFirmware(g_handle, &download_req, on_download_progress) 
            != DOWNLOAD_SUCCESS) {
            fprintf(stderr, "Failed to start download\n");
            unregisterProcess(g_handle);
            return 1;
        }
        
        // Wait for download to complete...
        // (Similar pattern: wait for callback)
        
        // 5. Flash the firmware
        UpdateRequest update_req = {
            .firmware_path = "/tmp/downloaded_firmware.bin",
            // ... other fields ...
        };
        
        if (updateFirmware(g_handle, &update_req, on_flash_complete) 
            != UPDATE_SUCCESS) {
            fprintf(stderr, "Failed to start flash\n");
            unregisterProcess(g_handle);
            return 1;
        }
        
        // Wait for flash to complete...
    } else {
        printf("No firmware update available\n");
    }
    
    // 6. Cleanup
    unregisterProcess(g_handle);
    sem_destroy(&g_check_complete);
    
    return 0;
}
```

### Workflow State Machine

```
┌─────────────────────┐
│   UNREGISTERED      │
└──────────┬──────────┘
           │ registerProcess()
           ▼
┌─────────────────────┐
│    REGISTERED       │◀─────────────────────┐
└──────────┬──────────┘                      │
           │ checkForUpdate()                │
           ▼                                 │
┌─────────────────────┐                      │
│  CHECKING_UPDATE    │                      │
└──────────┬──────────┘                      │
           │ callback(FIRMWARE_AVAILABLE)    │
           ▼                                 │
┌─────────────────────┐                      │
│ FIRMWARE_AVAILABLE  │                      │
└──────────┬──────────┘                      │
           │ downloadFirmware()              │
           ▼                                 │
┌─────────────────────┐                      │
│    DOWNLOADING      │                      │
└──────────┬──────────┘                      │
           │ callback(DOWNLOAD_COMPLETE)     │
           ▼                                 │
┌─────────────────────┐                      │
│ FIRMWARE_DOWNLOADED │                      │
└──────────┬──────────┘                      │
           │ updateFirmware()                │
           ▼                                 │
┌─────────────────────┐                      │
│     FLASHING        │                      │
└──────────┬──────────┘                      │
           │ callback(UPDATE_COMPLETE)       │
           ▼                                 │
┌─────────────────────┐                      │
│  UPDATE_COMPLETE    │──────────────────────┘
└──────────┬──────────┘  (can check again)
           │ unregisterProcess()
           ▼
┌─────────────────────┐
│   UNREGISTERED      │
└─────────────────────┘
```

## Troubleshooting Guide

### Problem: `checkForUpdate()` returns FAIL

**Possible Causes:**
1. Handle is NULL or invalid
2. Callback is NULL
3. Daemon not running
4. D-Bus communication error

**Solutions:**
```bash
# 1. Verify handle is valid
if (handle == NULL) {
    printf("ERROR: Handle is NULL - call registerProcess() first\n");
}

# 2. Check daemon status
systemctl status rdkFwupdateMgr.service

# 3. Restart daemon if needed
systemctl restart rdkFwupdateMgr.service

# 4. Check D-Bus
dbus-send --system --print-reply \
  --dest=org.rdkfwupdater.Interface \
  /org/rdkfwupdater/Service \
  org.freedesktop.DBus.Introspectable.Introspect
```

### Problem: Callback never fires

**Possible Causes:**
1. XConf server is down or slow
2. Network connectivity issue
3. Daemon crashed after accepting request
4. Application exited before callback could fire

**Solutions:**
```bash
# 1. Check daemon logs for XConf errors
tail -f /opt/logs/rdkFwupdateMgr.log | grep -i xconf

# 2. Check network connectivity
ping xconf.example.com  # Replace with actual XConf server

# 3. Verify daemon is running
ps aux | grep rdkFwupdateMgr

# 4. Increase app-level timeout
int timeout_sec = 300;  // 5 minutes instead of 2
```

### Problem: Callback fires with ERROR status

**Possible Causes:**
1. XConf returned invalid data
2. Handler not registered on daemon side
3. XConf server returned error response
4. Network timeout

**Solutions:**
```c
void on_update(const FwInfoData *fwinfo) {
    if (fwinfo->status == FIRMWARE_CHECK_ERROR) {
        fprintf(stderr, "Firmware check error - retry later\n");
        fprintf(stderr, "Current version: %s\n", fwinfo->CurrFWVersion);
        
        // Implement retry logic with exponential backoff
        schedule_retry_after(60);  // Retry after 1 minute
    }
}
```

### Problem: Callback fires on wrong thread

**This is expected behavior!** The callback always executes on the library's background thread.

**Solution:**
```c
// Use synchronization to communicate with main thread
pthread_mutex_t lock;
pthread_cond_t cond;
int callback_done = 0;

void on_update(const FwInfoData *fwinfo) {
    pthread_mutex_lock(&lock);
    // Process fwinfo...
    callback_done = 1;
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&lock);
}

// Main thread
pthread_mutex_lock(&lock);
while (!callback_done) {
    pthread_cond_wait(&cond, &lock);
}
pthread_mutex_unlock(&lock);
```

## See Also

### Related Documentation

- **[CHECKFORUPDATE_DESIGN_REVIEW.md](CHECKFORUPDATE_DESIGN_REVIEW.md)**: In-depth design analysis and recommendations
- **[CHECKFORUPDATE_TIMEOUT_ANALYSIS.md](CHECKFORUPDATE_TIMEOUT_ANALYSIS.md)**: Timeout handling strategies
- **[CALLBACK_REGISTRATION_AND_FIRING.md](CALLBACK_REGISTRATION_AND_FIRING.md)**: Internal callback mechanism details
- **[PROCESS_REGISTRATION_API.md](PROCESS_REGISTRATION_API.md)**: Process registration API documentation
- **[BUILD_AND_TEST.md](BUILD_AND_TEST.md)**: Build instructions and testing guide
- **[QUICK_START.md](QUICK_START.md)**: Quick start guide for new developers

### Related APIs

- **`registerProcess()`**: Register client application with daemon (prerequisite)
- **`unregisterProcess()`**: Cleanup and unregister from daemon
- **`downloadFirmware()`**: Download firmware image after `checkForUpdate()` finds one
- **`updateFirmware()`**: Flash downloaded firmware to device
- **`getCurrentVersion()`**: Query current firmware version without checking for updates

### External References

- [GLib Main Loop](https://docs.gtk.org/glib/main-loop.html): GLib event loop used for async operations
- [D-Bus Specification](https://dbus.freedesktop.org/doc/dbus-specification.html): D-Bus protocol documentation
- [XConf Protocol](https://github.com/rdkcentral/xconf): RDK XConf firmware query protocol

---

**Document Version**: 2.0  
**Last Updated**: 2024  
**Maintainer**: rdkfwupdater team
