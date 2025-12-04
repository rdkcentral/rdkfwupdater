# RDK Firmware Updater - Implementation Notes

## Summary

This implements the D-Bus interface for the RDK Firmware Updater daemon, enabling client applications to check for firmware updates. The implementation includes three core operations: RegisterProcess, CheckForUpdate, and UnregisterProcess.

## What's Included in This PR

### Files Modified

1. D-Bus Interface Headers
   - src/dbus/rdkFwupdateMgr_handlers.h - Handler function declarations
   - src/dbus/rdkv_dbus_server.h - D-Bus server interface and structures

2. D-Bus Implementation
   - src/dbus/rdkFwupdateMgr_handlers.c - Core firmware update logic
   - src/dbus/rdkv_dbus_server.c - D-Bus service and async task management

3. Main Daemon
   - src/rdkFwupdateMgr.c - Main entry point and state machine

### Documentation Added
- Comprehensive inline documentation for all functions
- Detailed comments explaining design decisions
- Flow documentation for complex operations

## Architecture Overview

### D-Bus Service
- Bus Name: org.rdkfwupdater.Interface
- Object Path: /org/rdkfwupdater/Service
- Service Type: System bus

### Methods Exposed
1. RegisterProcess(process_name, lib_version) returns handler_id
2. CheckForUpdate(handler_id) returns (version, available_version, details, status, code)
3. UnregisterProcess(handler_id) returns success

### Signals Emitted
- CheckForUpdateComplete - Broadcast when async XConf query completes

## Key Design Decisions

### 1. Client Registration System

Problem: Need to track which applications are using the service.

Solution: Handler ID system with conflict prevention.

Rules:
- Same client + same process name: Returns existing handler_id (idempotent)
- Same client + different process name: REJECT (one registration per client)
- Different client + same process name: REJECT (unique process names)
- New client + new process name: ALLOW (create new registration)

Rationale: Prevents confusion, enables cleanup tracking, supports idempotent re-registration.

### 2. Cache-First CheckForUpdate

Problem: XConf queries take time sometimes. D-Bus method calls should be fast.

Solution: Two-path approach.

Path 1 - Cache Hit (Fast):
  CheckForUpdate checks cache, loads from disk, returns immediately.

Path 2 - Cache Miss (Async):
  CheckForUpdate returns UPDATE_ERROR immediately
  Background: Query XConf in worker thread
  Broadcast signal when complete

Rationale:
- D-Bus methods can't block for seconds without timeout risk
- Client gets immediate feedback (UPDATE_ERROR indicates query in progress)
- Real result delivered via signal when ready
- Main loop stays responsive during network I/O

### 3. Async Task Architecture (GTask)

Problem: XConf queries involve blocking network I/O.

Solution: GTask worker threads.

Flow:
1. D-Bus method handler creates GTask
2. GTask spawns worker thread for blocking XConf call
3. Worker thread executes rdkFwupdateMgr_checkForUpdate()
4. Worker thread returns result to main loop via callback
5. Callback broadcasts D-Bus signal to all waiting clients

Rationale: Main event loop never blocks, daemon stays responsive.

### 4. Single Concurrent XConf Query

Problem: Multiple clients calling CheckForUpdate simultaneously would cause N network calls.

Solution: Queue system with shared result.

Behavior:
- First request triggers XConf query
- Additional requests queued, return UPDATE_ERROR immediately
- When XConf completes, ALL waiting clients get same signal
- One network call serves multiple clients

Rationale: Efficient, reduces XConf server load, all clients get consistent answer.

### 5. XConf Response Validation

Validation steps:
1. Parse JSON response (getXconfRespData)
2. Check firmware filename contains device model name (processJsonResponse)
3. Compare firmware version with current version
4. Cache successful response for future calls

Rationale: Prevents flashing wrong firmware to device, ensures response integrity.

## Data Structures

### ProcessInfo
```c
typedef struct {
    guint64 handler_id;        // Unique ID for this registration
    gchar *process_name;       // Client process name
    gchar *lib_version;        // Client library version
    gchar *sender_id;          // D-Bus unique connection name
    gint64 registration_time;  // When registered
} ProcessInfo;
```

### TaskContext (Union-based)
```c
typedef struct {
    TaskType type;
    gchar *process_name;
    gchar *sender_id;
    GDBusMethodInvocation *invocation;
    
    union {
        CheckUpdateTaskData check_update;
        DownloadTaskData download;
        UpdateTaskData update;
    } data;
} TaskContext;
```

Note: Union-based design allows different task types to share memory efficiently.

### CheckUpdateResponse
```c
typedef struct {
    CheckForUpdateResult result_code;  // 0=available, 1=not_available, 2=error
    gchar *current_img_version;        // Detected current version
    gchar *available_version;          // Version from XConf
    gchar *update_details;             // Pipe-delimited metadata
    gchar *status_message;             // Human-readable status
} CheckUpdateResponse;
```

---

## Implementation Details

### RegisterProcess Implementation

File: src/dbus/rdkv_dbus_server.c
Function: add_process_to_tracking()

Logic:
```c
// Iterate existing registrations
foreach (existing_registration) {
    if (sender_id matches) existing_same_client = found;
    if (process_name matches) existing_same_process = found;
}

// Apply 4 rules
if (same_client && same_process) return existing_handler_id;
if (same_client && different_process) return ERROR;
if (different_client && same_process) return ERROR;
// else create new registration
```

### CheckForUpdate Implementation

File: src/dbus/rdkFwupdateMgr_handlers.c
Function: rdkFwupdateMgr_checkForUpdate()

Logic:
```c
// Try cache first
if (xconf_cache_exists()) {
    load_xconf_from_cache(&response);
    // Validate and return immediately
} else {
    // Fetch from XConf (blocking call)
    fetch_xconf_firmware_info(&response, HTTP_XCONF_DIRECT, &http_code);
    // Validate response
    processJsonResponse(&response, current_version, device_model, maint_status);
    // Cache for future use
    save_xconf_to_cache(response_json, http_code);
}

// Compare versions
if (current_version != available_version) {
    return UPDATE_AVAILABLE;
} else {
    return UPDATE_NOT_AVAILABLE;
}
```

### Async CheckForUpdate (Cache Miss)

File: src/dbus/rdkv_dbus_server.c

Functions:
- process_app_request() - D-Bus method handler
- async_xconf_fetch_task() - Worker thread function
- async_xconf_fetch_complete() - Completion callback

Flow:
```c
// Main thread (D-Bus handler)
process_app_request() {
    if (cache_hit) {
        // Fast path - immediate response
        result = rdkFwupdateMgr_checkForUpdate();
        send_dbus_response(result);
    } else {
        // Slow path - async
        send_dbus_response(UPDATE_ERROR);  // Immediate
        
        // Create async task
        GTask *task = g_task_new();
        g_task_set_task_data(task, context);
        g_task_run_in_thread(task, async_xconf_fetch_task);
    }
}

// Worker thread
async_xconf_fetch_task() {
    // Safe to block here - not on main loop
    result = rdkFwupdateMgr_checkForUpdate();
    g_task_return_pointer(task, result);
}

// Main thread (callback)
async_xconf_fetch_complete() {
    result = g_task_propagate_pointer(task);
    
    // Broadcast to ALL waiting clients
    g_dbus_connection_emit_signal(
        connection,
        NULL,  // NULL = broadcast
        OBJECT_PATH,
        "org.rdkfwupdater.Interface",
        "CheckForUpdateComplete",
        result
    );
    
    // Cleanup all queued tasks
    cleanup_waiting_tasks();
    IsCheckUpdateInProgress = FALSE;
}
```

## Error Handling

### Client Not Registered
```
Error: "Handler not registered. Call RegisterProcess first."
Code: G_DBUS_ERROR_ACCESS_DENIED
```

### Network Failure
```
Result Code: 2 (UPDATE_ERROR)
Status: "Network error - unable to reach update server"
```

### Invalid XConf Response
```
Result Code: 2 (UPDATE_ERROR)
Status: "Update check failed - server communication error"
```

### Firmware Validation Failed
```
Result Code: 1 (UPDATE_NOT_AVAILABLE)
Status: "Firmware validation failed - not for this device model"
```

## Concurrency Control

### Global Flags
- `IsCheckUpdateInProgress` - Only one XConf query at a time
- `IsDownloadInProgress` - Only one download at a time (future)

### Queue Lists
- `waiting_checkUpdate_ids` - Task IDs waiting for XConf result
- `waiting_download_ids` - Task IDs waiting for download (future)

### Hash Tables
- `active_tasks` - All active async tasks (task_id → TaskContext)
- `registered_processes` - All registered clients (handler_id → ProcessInfo)

### Cache Operations
- `xconf_cache_exists()` - Check if cache file exists
- `load_xconf_from_cache()` - Parse cached JSON into XCONFRES struct
- `save_xconf_to_cache()` - Write raw XConf response to file

### Cache Validity
- No TTL implemented (cache persists until cleared)
- Cache cleared on:
  - System reboot
  - Manual deletion
  - New successful XConf query overwrites

## State Machine

### Daemon States
```
STATE_INIT
  ↓
STATE_INIT_VALIDATION
  ↓
STATE_IDLE (operational)
  ↓
cleanup_and_exit
```

### STATE_INIT
- Initialize logging
- Set up signal handlers
- Initialize D-Bus server
- Create GLib main loop
- Initialize device info (model, version, serial)
- Connect to IARM
- Fetch RFC values

### STATE_INIT_VALIDATION
- Validate device configuration
- Check for in-progress operations

### STATE_IDLE
- Run GLib main loop
- Handle D-Bus requests
- Dispatch async tasks
- Remains here until shutdown signal


## Known Limitations

### Current Implementation
1. **No download implementation** - DownloadFirmware is placeholder
2. **No update implementation** - UpdateFirmware is placeholder
3. **No cache TTL** - Cache never expires (cleared on reboot)
4. **No progress tracking** - Download progress not reported
5. **Single XConf query at a time** - Clients wait, no parallel queries

## Dependencies

### System Libraries
- **GLib/GIO** - D-Bus, main loop, async tasks
- **libcurl** - HTTP/HTTPS downloads
- **libjson-c** - JSON parsing
- **IARM** - Inter-process communication
- **systemd** - Service management

### Internal Libraries
- **downloadUtil** - Download helpers
- **json_process** - XConf response parsing
- **device_status_helper** - Device info retrieval
- rdkv_cdl_log_wrapper - Logging facade

## Build Configuration

### Autotools
```bash
./configure --enable-dbus
make
make install
```

### Systemd Service
```
[Unit]
Description=RDK Firmware Updater Daemon
After=network.target

[Service]
Type=dbus
BusName=org.rdkfwupdater.Interface
ExecStart=/usr/bin/rdkvfwupgrader
Restart=on-failure

[Install]
WantedBy=multi-user.target
```

## Performance Characteristics

### RegisterProcess
- **Time:** < 1ms
- **Blocking:** No
- **Network:** No

### CheckForUpdate (Cache Hit)
- **Time:** 10-50ms
- **Blocking:** Disk I/O only
- **Network:** No

### CheckForUpdate (Cache Miss)
- **Immediate Response:** 5-10ms
- **Signal Delivery:** Depends on the time taken by Xconf communication
- **Blocking:** Main loop not blocked
- **Network:** Yes (worker thread)

### UnregisterProcess
- Time: < 1ms
- Blocking: No
- Network: No

## Logging

### Log Levels
- **INFO:** Normal operations, state transitions
- **ERROR:** Failures, validation errors

### Key Log Markers
- `[REGISTER]` - Registration operations
- `[UNREGISTER]` - Unregistration operations
- `[CHECK_UPDATE]` - Update check operations
- `[ASYNC_FETCH]` - Worker thread operations
- `[COMPLETE]` - Completion callback operations
- `[PROCESS_TRACKING]` - Process management
- `[CACHE]` - Cache operations


## Client Usage Example

```c
#include <gio/gio.h>

// 1. Register with daemon
GDBusProxy *proxy = g_dbus_proxy_new_for_bus_sync(
    G_BUS_TYPE_SYSTEM,
    G_DBUS_PROXY_FLAGS_NONE,
    NULL,
    "org.rdkfwupdater.Interface",
    "/org/rdkfwupdater/Service",
    "org.rdkfwupdater.Interface",
    NULL, NULL
);

GVariant *result = g_dbus_proxy_call_sync(
    proxy, "RegisterProcess",
    g_variant_new("(ss)", "MyApp", "1.0.0"),
    G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL
);

guint64 handler_id;
g_variant_get(result, "(t)", &handler_id);
g_variant_unref(result);

// 2. Check for updates
result = g_dbus_proxy_call_sync(
    proxy, "CheckForUpdate",
    g_variant_new("(s)", handler_id_str),
    G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL
);

gchar *version, *available, *details, *status;
gint32 code;
g_variant_get(result, "(ssssi)", &version, &available, &details, &status, &code);

if (code == 0) {
    printf("Update available: %s -> %s\n", version, available);
} else if (code == 2) {
    // Wait for signal
    subscribe_to_signal("CheckForUpdateComplete");
}

// 3. Unregister when done
g_dbus_proxy_call_sync(
    proxy, "UnregisterProcess",
    g_variant_new("(t)", handler_id),
    G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL
);
```
---
