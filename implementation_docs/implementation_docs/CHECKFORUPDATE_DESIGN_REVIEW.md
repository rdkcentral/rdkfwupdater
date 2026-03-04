# CheckForUpdate Design Review & Recommended Changes

## Executive Summary

**Current Implementation: ❌ INCORRECT**

Your library fires the callback **TWICE** which violates your requirements and industry standards.

**Required Implementation: ✅ What You Want**

The callback should fire **ONCE** - only when real firmware data is available from the signal.

---

## Current Implementation Analysis

### What Happens Now (WRONG)

```
1. App calls checkForUpdate(handle, callback)
2. Library registers callback in registry
3. Library sends D-Bus request to daemon (BLOCKS)
4. Daemon responds: api_result=0, status_code=3 (FIRMWARE_CHECK_ERROR), empty versions
5. ❌ Library FIRES CALLBACK (1st time) with incomplete data
6. Library returns CHECK_FOR_UPDATE_SUCCESS to app
7. App continues execution
8. [10s later] Daemon broadcasts signal with real data
9. ✅ Library fires callback (2nd time) with real data
```

**Problem**: Callback fires twice - once with garbage data, once with real data.

---

## Your Requirement (CORRECT)

```
1. App calls checkForUpdate(handle, callback)
2. Library registers callback in registry
3. Library sends D-Bus request to daemon (BLOCKS)
4. Daemon responds: api_result=0, status_code=3 (FIRMWARE_CHECK_ERROR), empty versions
5. ✅ Library DOES NOT fire callback (data is incomplete)
6. Library returns CHECK_FOR_UPDATE_SUCCESS to app (trigger succeeded)
7. App continues execution
8. [10s later] Daemon broadcasts signal with real data
9. ✅ Library fires callback (ONLY time) with real data
```

**Correct Behavior**: 
- Return value indicates **trigger success/failure**
- Callback fires **once** with **real data only**

---

## Industry Standards Review

### Standard Pattern: Asynchronous Callback-Based API

**Rule 1: Return value ≠ Callback data**
- **Return value**: Did the API call succeed in initiating the operation?
  - `SUCCESS`: Operation started successfully, callback will fire later
  - `FAIL`: Operation failed to start, callback will NOT fire
- **Callback**: Fires ONCE with the actual result when operation completes

**Examples from Industry:**

#### 1. Android LocationManager
```java
// Request location updates
int result = locationManager.requestLocationUpdates(
    LocationManager.GPS_PROVIDER, 
    1000, 
    0, 
    locationListener
);

// result: SUCCESS = request accepted, FAIL = request rejected
// locationListener.onLocationChanged() fires ONCE per location update
```

#### 2. Linux epoll
```c
int fd = epoll_create1(0);  // SUCCESS/FAIL: Did epoll_create succeed?
epoll_ctl(fd, EPOLL_CTL_ADD, socket_fd, &event);  // Register for events
// Later: callback fires ONCE per event
```

#### 3. Node.js fs.readFile
```javascript
fs.readFile('/path/to/file', (err, data) => {
    // Callback fires ONCE when file read completes
});
// Return value: void (synchronous trigger)
```

#### 4. D-Bus async call pattern (GIO)
```c
g_dbus_connection_call(
    connection,
    "org.example.Service",
    "/org/example/Object",
    "org.example.Interface",
    "Method",
    parameters,
    NULL,
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL,
    async_callback,  // Fires ONCE when reply received
    user_data
);
```

**None of these fire the callback multiple times with different data!**

---

## Root Cause Analysis

### The Bug: Lines 177-192 in rdkFwupdateMgr_api.c

```c
/* [6] Build FwUpdateEventData and fire callback in caller's thread */
CheckForUpdateStatus status = internal_map_status_code(status_code);

FwUpdateEventData event_data = {
    .status            = status,
    .current_version   = curr_ver,
    .available_version = avail_ver,
    .status_message    = status_msg,
    .update_available  = (status == FIRMWARE_AVAILABLE)
};

FWUPMGR_INFO("checkForUpdate: firing callback — status=%d update_available=%d\n",
             status, event_data.update_available);

callback(handle, &event_data);  // ❌ BUG: Should NOT fire here!

g_free(curr_ver);
g_free(avail_ver);
g_free(upd_details);
g_free(status_msg);

FWUPMGR_INFO("checkForUpdate: done. handle='%s'\n", handle);
return CHECK_FOR_UPDATE_SUCCESS;
```

**Why this is wrong:**

1. **Daemon sends incomplete data** (status=3, empty versions) as acknowledgment
2. **Library fires callback immediately** with this incomplete data
3. **App receives garbage**: "check in progress" status with no useful info
4. **10 seconds later**, real data arrives via signal
5. **Library fires callback again** with real data
6. **App must handle two callbacks**, distinguish between them

**This violates the principle: "One operation, one callback"**

---

## What Daemon Actually Returns

### Immediate D-Bus Reply (Line 729 in rdkv_dbus_server.c)

```c
g_dbus_method_invocation_return_value(resp_ctx,
    g_variant_new("(issssi)",
        0,   // api_result: CHECK_FOR_UPDATE_SUCCESS (call accepted)
        "",  // current_version: EMPTY
        "",  // available_version: EMPTY
        "",  // update_details: EMPTY
        "Firmware check in progress - checking XConf server",
        3)); // status_code: FIRMWARE_CHECK_ERROR (not real result!)
```

**Analysis:**
- `api_result=0`: "I accepted your request and started processing"
- `status_code=3`: "I'm still checking, wait for signal"
- Empty versions: No data yet

**This is NOT the real firmware check result!** It's just an ACK.

### Later Signal Broadcast (Line 2217+ in rdkv_dbus_server.c)

```c
g_dbus_connection_emit_signal(
    ctx->connection,
    NULL,
    "/org/rdkfwupdater/Service",
    "org.rdkfwupdater.Interface",
    "CheckForUpdateComplete",
    g_variant_new("(tiissss)",
        handler_id_num,
        0,                      // api_result: SUCCESS
        0 or 1,                 // status_code: FIRMWARE_AVAILABLE or NOT_AVAILABLE
        "2024.01.15",          // current_version: REAL DATA
        "2024.03.01",          // available_version: REAL DATA
        update_details_json,    // update_details: REAL DATA
        "New firmware available"),
    &error
);
```

**This is the REAL result!**

---

## Recommended Fix

### Option 1: Simple Fix (Minimal Change)

**Change**: Don't fire callback on immediate response. Only fire on signal.

**File**: `librdkFwupdateMgr/src/rdkFwupdateMgr_api.c`

```c
CheckForUpdateResult checkForUpdate(FirmwareInterfaceHandle handle,
                                    UpdateEventCallback callback)
{
    /* [1] Validate */
    if (handle == NULL || handle[0] == '\0') {
        FWUPMGR_ERROR("checkForUpdate: invalid handle (NULL or empty)\n");
        return CHECK_FOR_UPDATE_FAIL;
    }
    if (callback == NULL) {
        FWUPMGR_ERROR("checkForUpdate: callback is NULL\n");
        return CHECK_FOR_UPDATE_FAIL;
    }

    FWUPMGR_INFO("checkForUpdate: handle='%s'\n", handle);

    /* [2] Register in async registry — catches CheckForUpdateComplete signal */
    if (!internal_register_callback(handle, callback)) {
        FWUPMGR_ERROR("checkForUpdate: registry full, handle='%s'\n", handle);
        return CHECK_FOR_UPDATE_FAIL;
    }

    /* [3] Connect to system D-Bus */
    GError *error = NULL;
    GDBusConnection *conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    if (conn == NULL) {
        FWUPMGR_ERROR("checkForUpdate: D-Bus connect failed: %s\n",
                      error ? error->message : "unknown");
        if (error) g_error_free(error);
        internal_unregister_callback(handle);  // ← Cleanup registry
        return CHECK_FOR_UPDATE_FAIL;
    }

    /* [4] Call CheckForUpdate to trigger daemon processing */
    FWUPMGR_INFO("checkForUpdate: sending trigger to daemon, handle='%s'\n", handle);

    GVariant *reply = g_dbus_connection_call_sync(
        conn,
        DBUS_SERVICE_NAME,
        DBUS_OBJECT_PATH,
        DBUS_INTERFACE_NAME,
        DBUS_METHOD_CHECK,
        g_variant_new("(s)", handle),
        NULL,
        G_DBUS_CALL_FLAGS_NONE,
        5000,  // ← Reduced timeout: only waiting for ACK, not XConf
        NULL,
        &error
    );

    g_object_unref(conn);

    if (reply == NULL) {
        FWUPMGR_ERROR("checkForUpdate: D-Bus call failed: %s\n",
                      error ? error->message : "unknown");
        if (error) g_error_free(error);
        internal_unregister_callback(handle);  // ← Cleanup registry
        return CHECK_FOR_UPDATE_FAIL;
    }

    /* [5] Parse reply to check if daemon accepted the request */
    gint32  api_result   = 0;
    gint32  status_code  = 0;
    gchar  *curr_ver     = NULL;
    gchar  *avail_ver    = NULL;
    gchar  *upd_details  = NULL;
    gchar  *status_msg   = NULL;

    g_variant_get(reply, "(issssi)",
                  &api_result,
                  &curr_ver,
                  &avail_ver,
                  &upd_details,
                  &status_msg,
                  &status_code);
    g_variant_unref(reply);

    FWUPMGR_INFO("checkForUpdate: daemon response — api_result=%d status_code=%d\n",
                 api_result, status_code);
    FWUPMGR_INFO("  (This is just an acknowledgment, not the real result)\n");

    // Free the acknowledgment data (we don't use it)
    g_free(curr_ver);
    g_free(avail_ver);
    g_free(upd_details);
    g_free(status_msg);

    if (api_result != 0) {
        FWUPMGR_ERROR("checkForUpdate: daemon rejected request (api_result=%d)\n",
                      api_result);
        internal_unregister_callback(handle);  // ← Cleanup registry
        return CHECK_FOR_UPDATE_FAIL;
    }

    /* [6] ✅ DO NOT FIRE CALLBACK HERE! 
     * Callback will fire later when CheckForUpdateComplete signal arrives
     * with the REAL firmware data from XConf.
     */

    FWUPMGR_INFO("checkForUpdate: trigger succeeded, callback registered\n");
    FWUPMGR_INFO("  Callback will fire when CheckForUpdateComplete signal arrives\n");
    FWUPMGR_INFO("  Expected wait: 5-30 seconds (XConf network latency)\n");

    return CHECK_FOR_UPDATE_SUCCESS;
}
```

**Changes Made:**
1. ✅ Removed callback invocation at line 192
2. ✅ Changed timeout from 30s to 5s (only waiting for ACK)
3. ✅ Added cleanup: unregister callback on failure paths
4. ✅ Added comments explaining behavior
5. ✅ Free acknowledgment data immediately (not used)

**Result:**
- Return value: Indicates if trigger succeeded
- Callback: Fires ONCE when signal arrives with real data

---

### Option 2: Enhanced Fix (Better Error Handling)

**Additional improvements:**

```c
CheckForUpdateResult checkForUpdate(FirmwareInterfaceHandle handle,
                                    UpdateEventCallback callback)
{
    /* ... validation ... */

    /* Register callback FIRST - if this fails, don't even call daemon */
    if (!internal_register_callback(handle, callback)) {
        FWUPMGR_ERROR("checkForUpdate: registry full, handle='%s'\n", handle);
        return CHECK_FOR_UPDATE_FAIL;
    }

    /* ... D-Bus connection ... */

    GVariant *reply = g_dbus_connection_call_sync(
        conn,
        DBUS_SERVICE_NAME,
        DBUS_OBJECT_PATH,
        DBUS_INTERFACE_NAME,
        DBUS_METHOD_CHECK,
        g_variant_new("(s)", handle),
        G_VARIANT_TYPE("(issssi)"),  // ← Specify expected type
        G_DBUS_CALL_FLAGS_NONE,
        5000,  // 5 second timeout for acknowledgment
        NULL,
        &error
    );

    g_object_unref(conn);

    if (reply == NULL) {
        FWUPMGR_ERROR("checkForUpdate: D-Bus call failed: %s\n",
                      error ? error->message : "unknown");
        if (error) g_error_free(error);
        
        // Cleanup: remove registered callback
        internal_unregister_callback(handle);
        return CHECK_FOR_UPDATE_FAIL;
    }

    /* Parse acknowledgment */
    gint32 api_result = 0;
    g_variant_get(reply, "(issssi)",
                  &api_result,
                  NULL, NULL, NULL, NULL, NULL);  // Ignore other fields
    g_variant_unref(reply);

    if (api_result != 0) {
        FWUPMGR_ERROR("checkForUpdate: daemon rejected (api_result=%d)\n", api_result);
        internal_unregister_callback(handle);
        return CHECK_FOR_UPDATE_FAIL;
    }

    FWUPMGR_INFO("checkForUpdate: SUCCESS — callback will fire on signal\n");
    return CHECK_FOR_UPDATE_SUCCESS;
}
```

**Additional improvements:**
- Specify expected D-Bus reply type
- Use NULL for unused g_variant_get parameters
- Consistent error cleanup pattern

---

## Updated example_app.c

### Current example_app.c Issues

❌ **Comments are misleading**: Says callback fires before return (wrong!)
❌ **No handling for multiple callbacks**: Doesn't expect two calls
❌ **Incorrect synchronization**: Assumes callback fires before return

### Corrected example_app.c

```c
/*
 * Copyright 2026 Comcast Cable Communications Management, LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file example_app.c
 * @brief Example application demonstrating rdkFwupdateMgr async API usage
 *
 * ASYNC FLOW (CORRECT):
 *   1. registerProcess()   → Get handle from daemon (synchronous)
 *   2. checkForUpdate()    → Trigger firmware check (returns immediately)
 *      └─ Return: SUCCESS = trigger accepted, FAIL = trigger rejected
 *      └─ Callback: Fires LATER (5-30s) with real firmware data
 *   3. [App continues execution while daemon queries XConf]
 *   4. on_firmware_event() → Callback fires with result (background thread)
 *   5. unregisterProcess() → Cleanup (synchronous)
 *
 * BUILD:
 *   gcc example_app.c -o example_app -lrdkFwupdateMgr \
 *       $(pkg-config --cflags --libs gio-2.0) -lpthread
 *
 * RUN:
 *   ./example_app
 */

#include "rdkFwupdateMgr_process.h"
#include "rdkFwupdateMgr_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdbool.h>

/* ========================================================================
 * SYNCHRONIZATION
 *
 * checkForUpdate() returns immediately. The callback fires LATER (5-30s)
 * from the library's background thread when the XConf query completes.
 *
 * We use mutex+condvar to keep the example process alive until the
 * callback delivers the final result.
 *
 * In a real plugin/service:
 *   - You have an event loop already running
 *   - Just return after checkForUpdate() and let callback arrive naturally
 *   - No need for this synchronization code
 * ======================================================================== */

static pthread_mutex_t g_result_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_result_cond  = PTHREAD_COND_INITIALIZER;
static bool            g_result_received = false;

/* ========================================================================
 * CALLBACK
 * ======================================================================== */

/**
 * @brief Firmware check result callback
 *
 * Invoked by library's background thread when CheckForUpdateComplete
 * signal arrives from daemon (5-30 seconds after checkForUpdate returns).
 *
 * IMPORTANT:
 *   - Runs in library's background thread, NOT main thread
 *   - Do NOT block, sleep, or perform heavy I/O
 *   - Do NOT call checkForUpdate() from inside callback (deadlock)
 *   - Do NOT store event_data pointers (freed after callback returns)
 *
 * @param handle     Handler ID that initiated the check
 * @param event_data Firmware check result (valid only during this call)
 */
static void on_firmware_event(FirmwareInterfaceHandle handle,
                               const FwUpdateEventData *event_data)
{
    printf("\n");
    printf("┌─────────────────────────────────────────────────────┐\n");
    printf("│        Firmware Check Result (Callback)            │\n");
    printf("└─────────────────────────────────────────────────────┘\n");

    /* Handler ID */
    printf("  Handler ID: %s\n", handle ? handle : "(null)");

    /* Status */
    const char *status_str = "UNKNOWN";
    switch (event_data->status) {
        case FIRMWARE_AVAILABLE:     status_str = "FIRMWARE_AVAILABLE";     break;
        case FIRMWARE_NOT_AVAILABLE: status_str = "FIRMWARE_NOT_AVAILABLE"; break;
        case UPDATE_NOT_ALLOWED:     status_str = "UPDATE_NOT_ALLOWED";     break;
        case FIRMWARE_CHECK_ERROR:   status_str = "FIRMWARE_CHECK_ERROR";   break;
        case IGNORE_OPTOUT:          status_str = "IGNORE_OPTOUT";          break;
        case BYPASS_OPTOUT:          status_str = "BYPASS_OPTOUT";          break;
    }
    printf("  Status: %s (%d)\n", status_str, event_data->status);

    /* Version information */
    printf("  Current Version:   %s\n",
           event_data->current_version   ? event_data->current_version   : "(not provided)");
    printf("  Available Version: %s\n",
           event_data->available_version ? event_data->available_version : "(not provided)");
    printf("  Status Message:    %s\n",
           event_data->status_message    ? event_data->status_message    : "(not provided)");
    printf("  Update Available:  %s\n",
           event_data->update_available ? "YES" : "NO");

    /* Recommended action */
    printf("\n  Recommended Action:\n");
    switch (event_data->status) {
        case FIRMWARE_AVAILABLE:
            printf("    → Call downloadFirmware() to get the update\n");
            break;
        case FIRMWARE_NOT_AVAILABLE:
            printf("    → Already on latest firmware, no action needed\n");
            break;
        case UPDATE_NOT_ALLOWED:
            printf("    → Device excluded from update by policy\n");
            break;
        case FIRMWARE_CHECK_ERROR:
            printf("    → Check failed, will retry at next interval\n");
            break;
        case IGNORE_OPTOUT:
        case BYPASS_OPTOUT:
            printf("    → User opted out of updates\n");
            break;
    }

    printf("└─────────────────────────────────────────────────────┘\n\n");

    /* Signal main thread that we received the result */
    pthread_mutex_lock(&g_result_mutex);
    g_result_received = true;
    pthread_cond_signal(&g_result_cond);
    pthread_mutex_unlock(&g_result_mutex);
}

/* ========================================================================
 * MAIN
 * ======================================================================== */

int main(void)
{
    int exit_code = EXIT_SUCCESS;

    printf("┌─────────────────────────────────────────────────────┐\n");
    printf("│      RDK Firmware Update Manager — Example App     │\n");
    printf("└─────────────────────────────────────────────────────┘\n\n");

    /* ----------------------------------------------------------------
     * STEP 1: Register with daemon
     * ---------------------------------------------------------------- */
    printf("[Step 1] Registering with daemon...\n");

    FirmwareInterfaceHandle handle = registerProcess("ExampleApp", "1.0.0");

    if (handle == FIRMWARE_INVALID_HANDLE) {
        fprintf(stderr, "\n[ERROR] Registration failed.\n");
        fprintf(stderr, "        Is rdkFwupdateMgr daemon running?\n");
        fprintf(stderr, "        Check: systemctl status rdkFwupdateMgr\n");
        return EXIT_FAILURE;
    }

    printf("[Step 1] ✓ Registered successfully (handler_id: %s)\n\n", handle);

    /* ----------------------------------------------------------------
     * STEP 2: Trigger firmware check (ASYNC)
     *
     * checkForUpdate():
     *   - Registers callback for later delivery
     *   - Sends trigger request to daemon
     *   - Returns immediately (0.1-1 second)
     *   - Return value: SUCCESS = trigger accepted, FAIL = trigger rejected
     *
     * After this call returns:
     *   - Daemon is querying XConf server (5-30 seconds)
     *   - Callback will fire later with real result
     *   - App can continue other work in meantime
     * ---------------------------------------------------------------- */
    printf("[Step 2] Triggering firmware check (async)...\n");

    CheckForUpdateResult result = checkForUpdate(handle, on_firmware_event);

    if (result == CHECK_FOR_UPDATE_FAIL) {
        fprintf(stderr, "\n[ERROR] checkForUpdate() trigger failed.\n");
        fprintf(stderr, "        Possible reasons:\n");
        fprintf(stderr, "          - Invalid handler ID\n");
        fprintf(stderr, "          - D-Bus communication error\n");
        fprintf(stderr, "          - Daemon not responding\n");
        exit_code = EXIT_FAILURE;
        goto unregister;
    }

    printf("[Step 2] ✓ Trigger succeeded — callback will fire later\n");
    printf("         (Daemon is now querying XConf server...)\n");
    printf("         (Expected wait: 5-30 seconds)\n\n");

    /* ----------------------------------------------------------------
     * STEP 3: Wait for callback to deliver result
     *
     * In a real plugin/service, you would NOT need this wait.
     * Your event loop would keep running and callback would arrive
     * naturally. This is only needed to keep the example alive.
     * ---------------------------------------------------------------- */
    printf("[Step 3] Waiting for firmware check result...\n");
    printf("         (App can do other work here...)\n\n");

    pthread_mutex_lock(&g_result_mutex);
    while (!g_result_received) {
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += 60;  // 60 second timeout

        int wait_result = pthread_cond_timedwait(&g_result_cond,
                                                  &g_result_mutex,
                                                  &timeout);
        if (wait_result == ETIMEDOUT) {
            fprintf(stderr, "\n[ERROR] Timeout waiting for callback (60s)\n");
            fprintf(stderr, "        Daemon may be stuck or XConf unreachable\n");
            exit_code = EXIT_FAILURE;
            pthread_mutex_unlock(&g_result_mutex);
            goto unregister;
        }
    }
    pthread_mutex_unlock(&g_result_mutex);

    printf("[Step 3] ✓ Callback delivered result\n\n");

    /* ----------------------------------------------------------------
     * STEP 4: Unregister from daemon
     * ---------------------------------------------------------------- */
unregister:
    printf("[Step 4] Unregistering from daemon...\n");

    unregisterProcess(handle);
    handle = NULL;

    printf("[Step 4] ✓ Unregistered\n\n");

    /* ----------------------------------------------------------------
     * DONE
     * ---------------------------------------------------------------- */
    if (exit_code == EXIT_SUCCESS) {
        printf("┌─────────────────────────────────────────────────────┐\n");
        printf("│              Example completed successfully         │\n");
        printf("└─────────────────────────────────────────────────────┘\n");
    } else {
        printf("┌─────────────────────────────────────────────────────┐\n");
        printf("│           Example completed with errors             │\n");
        printf("└─────────────────────────────────────────────────────┘\n");
    }

    return exit_code;
}
```

---

## Additional Changes Needed

### 1. Add internal_unregister_callback() Function

**File**: `librdkFwupdateMgr/src/rdkFwupdateMgr_async_internal.h`

```c
/**
 * @brief Remove a callback from the registry
 *
 * Used to cleanup when checkForUpdate() fails after registering callback.
 *
 * @param handle Handler ID to unregister
 * @return true if found and removed, false otherwise
 */
bool internal_unregister_callback(const char *handle);
```

**File**: `librdkFwupdateMgr/src/rdkFwupdateMgr_async.c`

```c
bool internal_unregister_callback(const char *handle)
{
    if (handle == NULL) {
        return false;
    }

    pthread_mutex_lock(&g_registry.mutex);

    for (int i = 0; i < MAX_PENDING_CALLBACKS; i++) {
        CallbackEntry *entry = &g_registry.entries[i];

        if (entry->state != CALLBACK_STATE_PENDING) {
            continue;
        }

        if (entry->handle_key != NULL && strcmp(entry->handle_key, handle) == 0) {
            // Found it - free and reset
            free(entry->handle_key);
            entry->handle_key = NULL;
            entry->callback = NULL;
            entry->state = CALLBACK_STATE_IDLE;

            pthread_mutex_unlock(&g_registry.mutex);
            FWUPMGR_INFO("internal_unregister_callback: removed '%s'\n", handle);
            return true;
        }
    }

    pthread_mutex_unlock(&g_registry.mutex);
    FWUPMGR_WARN("internal_unregister_callback: '%s' not found\n", handle);
    return false;
}
```

---

## Testing Plan

### Test Case 1: Normal Flow

```bash
$ ./example_app
[Step 1] Registering...
[Step 1] ✓ Registered (handler_id: 12345)
[Step 2] Triggering firmware check...
[Step 2] ✓ Trigger succeeded — callback will fire later
[Step 3] Waiting for result...
[Callback] Firmware Check Result:
  Status: FIRMWARE_AVAILABLE (0)
  Current: 2024.01.15
  Available: 2024.03.01
[Step 3] ✓ Callback delivered result
[Step 4] Unregistering...
[Step 4] ✓ Unregistered
Example completed successfully
```

**Expected**:
- Callback fires ONCE
- Real firmware data received
- No "FIRMWARE_CHECK_ERROR" in callback

### Test Case 2: Trigger Fails (Daemon Down)

```bash
$ systemctl stop rdkFwupdateMgr
$ ./example_app
[Step 1] Registering...
[ERROR] Registration failed.
        Is rdkFwupdateMgr daemon running?
```

### Test Case 3: Timeout (XConf Unreachable)

```bash
# Daemon running but XConf server unreachable
$ ./example_app
[Step 1] ✓ Registered
[Step 2] ✓ Trigger succeeded
[Step 3] Waiting for result...
[ERROR] Timeout waiting for callback (60s)
        Daemon may be stuck or XConf unreachable
```

### Test Case 4: Multiple Concurrent Apps

```bash
# Terminal 1:
$ ./example_app &
[Step 2] ✓ Trigger succeeded

# Terminal 2 (immediately):
$ ./example_app &
[Step 2] ✓ Trigger succeeded

# Both should receive callback around same time (piggyback)
```

---

## Summary of Changes

### rdkFwupdateMgr_api.c

| Line | Change | Reason |
|------|--------|--------|
| 124 | Timeout: 30000 → 5000 | Only waiting for ACK, not XConf |
| 177-192 | **DELETE callback invocation** | Don't fire with incomplete data |
| 105, 130, 182 | Add `internal_unregister_callback()` | Cleanup on error paths |
| 160-180 | Simplify parsing | Don't build event_data, just check api_result |

### rdkFwupdateMgr_async.c

| Line | Change | Reason |
|------|--------|--------|
| +300 | Add `internal_unregister_callback()` | Allow cleanup from api.c |

### example_app.c

| Section | Change | Reason |
|---------|--------|--------|
| Comments | Rewrite to match async behavior | Old comments were misleading |
| Callback | Remove "already fired" comments | Callback fires LATER, not during call |
| main() | Add condvar wait with timeout | Must wait for async callback |
| main() | Remove "callback already fired" print | Incorrect assumption |

---

## Final Verdict

### Current Implementation: ❌ WRONG

**Problems:**
1. Callback fires twice with different data
2. First callback has garbage data (FIRMWARE_CHECK_ERROR, empty versions)
3. Violates "one operation, one callback" principle
4. Confuses developers
5. Blocks for 30 seconds even though daemon responds in <1 second

### After Fix: ✅ CORRECT

**Benefits:**
1. Callback fires ONCE with real data
2. Return value clearly indicates trigger success/failure
3. Non-blocking: returns in <1 second
4. Follows industry standards
5. Easy to understand and use

---

## Implementation Priority

**CRITICAL - Fix Immediately:**
1. Remove callback invocation from `rdkFwupdateMgr_api.c` line 192
2. Reduce timeout from 30s to 5s (line 124)
3. Add error cleanup with `internal_unregister_callback()`

**HIGH - Document for Users:**
4. Update `CHECK_FOR_UPDATE_API.md` to clarify async behavior
5. Fix `example_app.c` comments and logic

**MEDIUM - Code Quality:**
6. Add `internal_unregister_callback()` function
7. Specify D-Bus reply type explicitly
8. Add timeout to example's condvar wait

---

## Conclusion

Your requirement is **100% correct** and follows **industry standards**.

The current implementation is **wrong** because it fires the callback twice.

The fix is **simple**: Delete 15 lines of code (the callback invocation).

**Do this and your API will be clean, predictable, and standards-compliant.**
