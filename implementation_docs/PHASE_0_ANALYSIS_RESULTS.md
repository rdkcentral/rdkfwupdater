# Phase 0: Analysis & Validation - COMPLETED

**Completion Date:** February 25, 2026  
**Status:** ✅ COMPLETED

---

## Sub-Phase 0.1: Daemon Signal Format Analysis

### Signal Definition (from D-Bus Introspection XML)

**Location:** `src/dbus/rdkv_dbus_server.c`, lines 227-234

```xml
<signal name='CheckForUpdateComplete'>
    <arg type='t' name='handlerId'/>
    <arg type='i' name='result'/>
    <arg type='i' name='statusCode'/>
    <arg type='s' name='currentVersion'/>
    <arg type='s' name='availableVersion'/>
    <arg type='s' name='updateDetails'/>
    <arg type='s' name='statusMessage'/>
</signal>
```

### Signal Emission Code

**Location:** `src/dbus/rdkv_dbus_server.c`, lines 2390-2397

```c
gboolean signal_sent = g_dbus_connection_emit_signal(
    ctx->connection,
    NULL,  // NULL destination = broadcast to all listeners
    OBJECT_PATH,  // "/org/rdkfwupdater/Service"
    "org.rdkfwupdater.Interface",
    "CheckForUpdateComplete",
    result,  // GVariant with signature "(tiissss)"
    &error
);
```

### Signal Parameters (Confirmed)

| Position | Type | Name | Description | Notes |
|----------|------|------|-------------|-------|
| 0 | `t` (uint64) | handlerId | Handler ID (numeric) | Always 0 in current implementation |
| 1 | `i` (int32) | result | API result code | 0=SUCCESS, 1=FAIL |
| 2 | `i` (int32) | statusCode | Firmware status | See CheckForUpdateStatus enum |
| 3 | `s` (string) | currentVersion | Current firmware version | e.g., "V1.2.3" |
| 4 | `s` (string) | availableVersion | Available firmware version | e.g., "V1.3.0" or "" |
| 5 | `s` (string) | updateDetails | Comma-separated update info | "FwFileName:xxx,FwUrl:xxx,..." |
| 6 | `s` (string) | statusMessage | Human-readable message | e.g., "New firmware available" |

**GVariant Signature:** `(tiissss)`

### Result Codes

#### API Result (position 1)
- `0` = CHECK_FOR_UPDATE_SUCCESS
- `1` = CHECK_FOR_UPDATE_FAIL

#### Status Code (position 2)
- `0` = FIRMWARE_AVAILABLE
- `1` = FIRMWARE_NOT_AVAILABLE
- `2` = UPDATE_NOT_ALLOWED
- `3` = FIRMWARE_CHECK_ERROR
- `4` = IGNORE_OPTOUT
- `5` = BYPASS_OPTOUT

### Key Findings

1. **✅ NO HANDLER_ID IN SIGNAL**: The `handlerId` field is always 0 (confirmed in code analysis)
2. **✅ BROADCAST TO ALL**: Signal destination is `NULL` (broadcast to all D-Bus listeners)
3. **✅ SINGLE SIGNAL**: Daemon emits ONE signal that all waiting clients must process
4. **✅ ALL CLIENTS GET SAME DATA**: Every subscribed client receives identical signal payload

### updateDetails String Format

**Format:** Comma-separated key-value pairs
```
"FwFileName:abc.bin,FwUrl:https://...,FwVersion:1.2.3,RebootImmediately:true,..."
```

**Parsed Fields:**
- `FwFileName` → firmware binary filename
- `FwUrl` → download URL
- `FwVersion` → new version string
- `RebootImmediately` → "true" or "false"
- `DelayDownload` → "true" or "false"
- `PDRIVersion` → PDRI image version
- `PeripheralFirmwares` → peripheral firmware versions

---

## Sub-Phase 0.2: D-Bus Interface Validation

### D-Bus Interface Details

| Property | Value |
|----------|-------|
| **Service Name** | `org.rdkfwupdater.Interface` |
| **Object Path** | `/org/rdkfwupdater/Service` |
| **Interface Name** | `org.rdkfwupdater.Interface` |
| **Bus Type** | System Bus |

### Method Call: CheckForUpdate

**Signature:** `CheckForUpdate(s) → (iissss...)`

**Input:**
- `s` (string): handler_process_name (e.g., "MyPlugin")

**Output:** (Returns immediately with current cached data)
- Multiple fields with firmware information

**Note:** The method returns synchronously. The async signal comes later when daemon completes XConf check.

### Signal Subscription Mechanism

**Existing Client Code Example** (`src/test/testClient.c`, line 154):

```c
g_dbus_connection_signal_subscribe(
    ctx->connection,
    DBUS_SERVICE_NAME,           // "org.rdkfwupdater.Interface"
    DBUS_INTERFACE_NAME,         // "org.rdkfwupdater.Interface"
    "CheckForUpdateComplete",     // Signal name
    DBUS_OBJECT_PATH,            // "/org/rdkfwupdater/Service"
    NULL,                        // arg0 (no filtering)
    G_DBUS_SIGNAL_FLAGS_NONE,
    on_check_complete,           // Callback function
    ctx,                         // User data
    NULL                         // Destroy notify
);
```

### Validation Results

✅ **D-Bus interface validated**  
✅ **Service name confirmed:** `org.rdkfwupdater.Interface`  
✅ **Object path confirmed:** `/org/rdkfwupdater/Service`  
✅ **Signal subscription is possible using `g_dbus_connection_signal_subscribe()`**  
✅ **No special permissions or capabilities required**

---

## Library Architecture Analysis

### Current Library Structure

**Files:**
- `librdkFwupdateMgr/src/rdkFwupdateMgr_api.c` - Main API implementation
- `librdkFwupdateMgr/src/rdkFwupdateMgr_process.c` - Process registration
- `librdkFwupdateMgr/src/rdkFwupdateMgr_log.c` - Logging utilities
- `librdkFwupdateMgr/include/rdkFwupdateMgr_client.h` - Public API header
- `librdkFwupdateMgr/include/rdkFwupdateMgr_process.h` - Process registration header

### Existing Callback Mechanism

**Current Implementation:**
```c
typedef struct {
    FirmwareInterfaceHandle handle;
    UpdateEventCallback update_callback;
    DownloadCallback download_callback;
    UpdateCallback firmware_update_callback;
} CallbackContext;

static CallbackContext g_callback_ctx = {0};
static pthread_mutex_t g_callback_mutex = PTHREAD_MUTEX_INITIALIZER;
```

**Note:** Currently only supports ONE callback at a time (global singleton)

### Existing D-Bus Connection

The library creates D-Bus connections using:
```c
GDBusConnection *connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
GDBusProxy *proxy = g_dbus_proxy_new_sync(...);
```

**Key Point:** Currently no signal subscription - only synchronous method calls!

---

## Implementation Strategy

### What We Need to Add

1. **New Data Structures:**
   - Multi-callback registry (array-based, MAX_CALLBACKS=64)
   - Callback state machine (IDLE, WAITING, COMPLETED, CANCELLED, TIMEOUT)
   - Thread-safe registry with pthread mutex

2. **Background Thread:**
   - Dedicated GLib event loop thread
   - Signal handler running in background thread context
   - Thread lifecycle management

3. **Signal Subscription:**
   - Subscribe to `CheckForUpdateComplete` signal
   - Parse signal data (GVariant with signature `(tiissss)`)
   - Match signal to waiting callbacks

4. **New Async API:**
   - `rdkFwupdateMgr_checkForUpdate_async(callback, user_data) → callback_id`
   - `rdkFwupdateMgr_checkForUpdate_cancel(callback_id) → result`

### What We Must NOT Touch

❌ **DO NOT MODIFY:**
- Daemon code (`src/dbus/rdkv_dbus_server.c`)
- Daemon signal emission logic
- D-Bus interface XML
- Existing synchronous APIs
- Process registration mechanism

✅ **ONLY MODIFY:**
- Client library files in `librdkFwupdateMgr/src/`
- Public header `librdkFwupdateMgr/include/rdkFwupdateMgr_client.h`
- Add new example code
- Add unit tests

---

## Design Decisions Confirmed

### Decision 1: No Handler ID Matching
**Rationale:** Signal always contains `handlerId=0`, so we cannot filter by handler.  
**Solution:** All WAITING callbacks receive the signal.

### Decision 2: Array-Based Registry
**Rationale:** Fixed-size array is simpler, more predictable, and Coverity-friendly.  
**Implementation:** `CallbackContext contexts[64]` with mutex protection.

### Decision 3: Background Thread Required
**Rationale:** Signal handlers must run in a GLib event loop context.  
**Implementation:** Dedicated pthread running `GMainLoop` in background.

### Decision 4: State Machine Per Callback
**Rationale:** Multiple concurrent async calls require state tracking.  
**States:** IDLE, WAITING, COMPLETED, CANCELLED, TIMEOUT, ERROR

### Decision 5: Reference Counting
**Rationale:** Prevent use-after-free when callback is executing while being cancelled.  
**Implementation:** Atomic ref_count in CallbackContext, increment before invoke, decrement after.

---

## Acceptance Criteria - ALL MET ✅

- [x] Signal parameter list documented (7 parameters, signature `(tiissss)`)
- [x] Signal emission code reviewed (broadcasts to all, NULL destination)
- [x] No handler_id confirmed (always 0)
- [x] D-Bus interface name documented (`org.rdkfwupdater.Interface`)
- [x] Object path documented (`/org/rdkfwupdater/Service`)
- [x] Method and signal signatures confirmed
- [x] Existing library architecture analyzed
- [x] Implementation strategy defined
- [x] Design decisions documented

---

## Next Steps

**Proceed to Phase 1:** Core Data Structures Implementation

**Files to Modify:**
1. Create new internal header: `librdkFwupdateMgr/src/rdkFwupdateMgr_async_internal.h`
2. Create implementation file: `librdkFwupdateMgr/src/rdkFwupdateMgr_async.c`
3. Update public header: `librdkFwupdateMgr/include/rdkFwupdateMgr_client.h`

**Phase 1 Tasks:**
- Sub-Phase 1.1: Define state enum and structures
- Sub-Phase 1.2: Implement callback registry
- Sub-Phase 1.3: Add registry helper functions

---

**END OF PHASE 0 ANALYSIS**
