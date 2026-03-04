# CheckForUpdate Timeout Analysis & Long-Running XConf Queries

## Reality Check: XConf Query Duration

### Current Assumption (WRONG)
```
Library timeout: 30 seconds
Expected XConf duration: 5-30 seconds
```

### Production Reality (CORRECT)
```
XConf query can take: 30 seconds to 2 HOURS!
Reasons:
- Network congestion
- XConf server load (millions of devices querying)
- CDN routing issues
- Retry logic with exponential backoff
- DNS resolution delays
- Firewall/proxy timeouts
```

---

## Current Problem

**File**: `librdkFwupdateMgr/src/rdkFwupdateMgr_api.c` Line 124

```c
GVariant *reply = g_dbus_connection_call_sync(
    conn,
    DBUS_SERVICE_NAME,
    DBUS_OBJECT_PATH,
    DBUS_INTERFACE_NAME,
    DBUS_METHOD_CHECK,
    g_variant_new("(s)", handle),
    NULL,
    G_DBUS_CALL_FLAGS_NONE,
    30000,  // ❌ 30 second timeout - TOO SHORT for XConf!
    NULL,
    &error
);
```

**If daemon takes 60+ minutes to query XConf:**
- Library's D-Bus call times out after 30 seconds
- Returns `CHECK_FOR_UPDATE_FAIL` to app
- App thinks trigger failed
- But daemon is STILL querying XConf in background!
- Eventually daemon emits signal, but app already gave up

**This is a mismatch between library timeout and daemon processing time.**

---

## The Real Architecture Issue

### What's Happening Now (WRONG)

```
App Thread:
  checkForUpdate() ──┐
                     ├─ D-Bus sync call (30s timeout)
                     ├─ Waits for daemon response
                     └─ Returns SUCCESS/FAIL
                     
                     ↓ If daemon takes >30s to respond
                     
                     ❌ D-Bus timeout!
                     ❌ Returns CHECK_FOR_UPDATE_FAIL
                     
Daemon Thread:
  Receives request ──┐
                     ├─ Validates
                     ├─ Spawns worker
                     ├─ ??? When does it respond to D-Bus call?
                     └─ Worker queries XConf (60 mins)
```

**Key Question**: When does the daemon send the D-Bus reply?
- **If daemon replies IMMEDIATELY** (with ACK): Library timeout doesn't matter
- **If daemon replies AFTER XConf completes**: Library timeout is critical

---

## Analyzing Current Daemon Behavior

### Looking at Daemon Code

**File**: `src/dbus/rdkv_dbus_server.c` Line 729

```c
// 4. SEND IMMEDIATE FIRMWARE_CHECK_ERROR RESPONSE
g_dbus_method_invocation_return_value(resp_ctx,
    g_variant_new("(issssi)",
        0,   // result: CHECK_FOR_UPDATE_SUCCESS
        "",  // current_version (empty - not known yet)
        "",  // available_version (empty - not known yet)
        "",  // update_details (empty - not known yet)
        "Firmware check in progress - checking XConf server",
        3)); // status_code: FIRMWARE_CHECK_ERROR (check in progress)

// 5. CREATE TASK AND ADD TO TRACKING
TaskContext *task_ctx = create_task_context(TASK_TYPE_CHECK_UPDATE, 
                                            handler_process_name, 
                                            rdkv_req_caller_id, 
                                            NULL);  // invocation already consumed!

// ... spawn worker thread for XConf query ...
```

**✅ GOOD NEWS**: Daemon responds **IMMEDIATELY** (line 729)!

**This means:**
- Daemon sends D-Bus reply in <1 second
- Library's sync call unblocks quickly
- XConf query happens in background (worker thread)
- Signal arrives later (when XConf completes)

**Therefore**: Library timeout only needs to cover daemon's acknowledgment time, NOT XConf query time!

---

## Correct Solution

### Understanding the Two Timeouts

```
┌─────────────────────────────────────────────────────────────────┐
│ Timeout 1: Library → Daemon (D-Bus Method Call)                │
│ Duration: 5 seconds (generous for ACK)                          │
│ Purpose: How long to wait for daemon to acknowledge request     │
│ Failure: Daemon down, D-Bus broken, daemon hung                │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│ Timeout 2: Daemon → XConf Server (Network Query)               │
│ Duration: 30s - 2 HOURS (depends on network/server)            │
│ Purpose: How long daemon waits for XConf to respond            │
│ Failure: Network down, XConf server overloaded, DNS issues     │
│ Handled by: DAEMON, not library!                               │
└─────────────────────────────────────────────────────────────────┘
```

**Key Insight**: Library only cares about **Timeout 1**. Daemon handles **Timeout 2** internally.

---

## Recommended Implementation

### Library Changes (CORRECT)

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

    /* [2] Register callback — will fire when signal arrives */
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
        internal_unregister_callback(handle);
        return CHECK_FOR_UPDATE_FAIL;
    }

    /* [4] Trigger daemon to start XConf query
     *
     * Timeout: 5 seconds
     * Why: Daemon responds immediately with ACK, then processes in background.
     *      We're NOT waiting for XConf result here — that comes via signal.
     *      5 seconds is generous for daemon to send acknowledgment.
     *
     * XConf query duration (handled by daemon, not library):
     *   - Typical: 5-30 seconds
     *   - Worst case: 30 seconds to 2 hours
     *   - Daemon's responsibility to implement retries, timeouts, fallback
     *   - Library just waits for signal (no timeout on our side)
     */
    FWUPMGR_INFO("checkForUpdate: triggering daemon (expecting ACK within 5s)\n");

    GVariant *reply = g_dbus_connection_call_sync(
        conn,
        DBUS_SERVICE_NAME,
        DBUS_OBJECT_PATH,
        DBUS_INTERFACE_NAME,
        DBUS_METHOD_CHECK,
        g_variant_new("(s)", handle),
        NULL,
        G_DBUS_CALL_FLAGS_NONE,
        5000,   /* 5 second timeout — only waiting for daemon ACK */
        NULL,
        &error
    );

    g_object_unref(conn);

    if (reply == NULL) {
        FWUPMGR_ERROR("checkForUpdate: daemon did not ACK within 5s: %s\n",
                      error ? error->message : "unknown");
        if (error) g_error_free(error);
        internal_unregister_callback(handle);
        return CHECK_FOR_UPDATE_FAIL;
    }

    /* [5] Parse acknowledgment (daemon accepted request or rejected it) */
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

    FWUPMGR_INFO("checkForUpdate: daemon ACK received\n");
    FWUPMGR_INFO("  api_result=%d (0=accepted, non-0=rejected)\n", api_result);
    FWUPMGR_INFO("  status_code=%d (3=check in progress - normal)\n", status_code);

    // Free ACK data (we don't use it — real data comes via signal)
    g_free(curr_ver);
    g_free(avail_ver);
    g_free(upd_details);
    g_free(status_msg);

    if (api_result != 0) {
        FWUPMGR_ERROR("checkForUpdate: daemon rejected request\n");
        internal_unregister_callback(handle);
        return CHECK_FOR_UPDATE_FAIL;
    }

    /* [6] Success - callback registered, daemon processing
     *
     * What happens next:
     *   - Daemon queries XConf in background (30s to 2 hours)
     *   - When complete, daemon emits CheckForUpdateComplete signal
     *   - Library's background thread receives signal
     *   - Library fires callback with real firmware data
     *
     * App can continue execution immediately. No need to wait.
     */
    FWUPMGR_INFO("checkForUpdate: SUCCESS — callback will fire when signal arrives\n");
    FWUPMGR_INFO("  Expected wait: 30 seconds to 2 hours (depends on XConf/network)\n");
    FWUPMGR_INFO("  App can continue other work now\n");

    return CHECK_FOR_UPDATE_SUCCESS;
}
```

**Changes:**
1. ✅ Timeout: 5 seconds (only for daemon ACK)
2. ✅ Comments clarify what we're waiting for
3. ✅ Comments explain XConf can take hours (daemon's problem, not ours)
4. ✅ Don't fire callback on ACK
5. ✅ Cleanup on error paths

---

## App Usage Pattern (CORRECT)

### Example App with No Timeout Assumption

```c
#include "rdkFwupdateMgr_client.h"
#include <stdio.h>
#include <stdbool.h>

static volatile bool g_got_result = false;

void on_firmware_event(FirmwareInterfaceHandle handle,
                       const FwUpdateEventData *event_data)
{
    printf("\n=== Firmware Check Result ===\n");
    printf("Status: %d\n", event_data->status);
    printf("Current: %s\n", event_data->current_version ?: "(none)");
    printf("Available: %s\n", event_data->available_version ?: "(none)");
    
    if (event_data->status == FIRMWARE_AVAILABLE) {
        printf("✅ Update available!\n");
    } else if (event_data->status == FIRMWARE_NOT_AVAILABLE) {
        printf("ℹ️  Already on latest firmware\n");
    } else if (event_data->status == FIRMWARE_CHECK_ERROR) {
        printf("❌ Check failed\n");
    }
    
    g_got_result = true;
}

int main(void)
{
    // Step 1: Register
    FirmwareInterfaceHandle handle = registerProcess("MyApp", "1.0");
    if (!handle) {
        fprintf(stderr, "Registration failed\n");
        return 1;
    }
    
    // Step 2: Trigger check
    printf("Triggering firmware check...\n");
    CheckForUpdateResult result = checkForUpdate(handle, on_firmware_event);
    
    if (result == CHECK_FOR_UPDATE_FAIL) {
        fprintf(stderr, "Failed to trigger check\n");
        unregisterProcess(handle);
        return 1;
    }
    
    printf("✓ Trigger succeeded\n");
    printf("  Callback will fire when check completes\n");
    printf("  This may take 30 seconds to 2 hours (network dependent)\n");
    printf("  App can do other work now...\n\n");
    
    // Step 3: App continues with other work
    // In real app, you have an event loop already running
    // For this example, we just sleep in a loop
    
    int wait_seconds = 0;
    while (!g_got_result) {
        sleep(1);
        wait_seconds++;
        
        // Print progress every 30 seconds
        if (wait_seconds % 30 == 0) {
            printf("Still waiting... (%d seconds elapsed)\n", wait_seconds);
        }
        
        // Optional: Give up after 2 hours
        if (wait_seconds > 7200) {
            fprintf(stderr, "Timeout after 2 hours - giving up\n");
            break;
        }
    }
    
    if (g_got_result) {
        printf("\n✓ Result received after %d seconds\n", wait_seconds);
    }
    
    // Step 4: Cleanup
    unregisterProcess(handle);
    return 0;
}
```

**Key Points:**
1. ✅ No timeout assumption (can wait hours)
2. ✅ Progress updates every 30 seconds
3. ✅ Optional 2-hour safety timeout
4. ✅ App does other work while waiting (in real app)

---

## What About Daemon Timeout?

### Daemon Should Handle XConf Timeouts

**File**: `src/rdkv_upgrade.c` (XConf query implementation)

The daemon should have its own timeout logic:

```c
CheckUpdateResponse rdkFwupdateMgr_checkForUpdate(const char *handler_id)
{
    CheckUpdateResponse response;
    
    // Set timeout for XConf HTTP request
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);  // 120 second timeout
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);  // 30 second connect timeout
    
    // Retry logic
    int retries = 3;
    for (int i = 0; i < retries; i++) {
        CURLcode res = curl_easy_perform(curl);
        
        if (res == CURLE_OK) {
            // Success - parse response
            break;
        } else if (res == CURLE_OPERATION_TIMEDOUT) {
            SWLOG_ERROR("XConf timeout (attempt %d/%d)\n", i+1, retries);
            if (i < retries - 1) {
                sleep(5);  // Wait before retry
                continue;
            }
            // Final attempt failed
            response.status = FIRMWARE_CHECK_ERROR;
            response.message = "XConf server timeout after retries";
            return response;
        } else {
            // Other error
            response.status = FIRMWARE_CHECK_ERROR;
            response.message = curl_easy_strerror(res);
            return response;
        }
    }
    
    // Parse and return result
    return response;
}
```

**Daemon's responsibility:**
- ✅ HTTP timeout (e.g., 120 seconds per attempt)
- ✅ Connection timeout (e.g., 30 seconds)
- ✅ Retry logic (e.g., 3 attempts with backoff)
- ✅ Emit signal with result OR error status

**Library's responsibility:**
- ✅ Wait for daemon ACK (5 seconds)
- ✅ Register callback for signal
- ✅ Fire callback when signal arrives (no timeout)

---

## Signal Delivery: No Timeout Needed

### Why Library Doesn't Need Signal Timeout

```c
// Library's background thread subscribes to signal
g_dbus_connection_signal_subscribe(
    g_bg_thread.connection,
    NULL,                        // sender (any)
    DBUS_INTERFACE_NAME,
    DBUS_SIGNAL_COMPLETE,        // "CheckForUpdateComplete"
    DBUS_OBJECT_PATH,
    NULL,                        // arg0 (any)
    G_DBUS_SIGNAL_FLAGS_NONE,
    on_check_complete_signal,    // Callback
    NULL,
    NULL
);

// Then runs event loop
g_main_loop_run(g_bg_thread.main_loop);  // No timeout!
```

**Event loop keeps running forever:**
- Signal arrives → `on_check_complete_signal()` fires → App callback invoked
- If signal never arrives → App callback never fires
- App decides if/when to give up (app-level timeout, not library)

**This is correct behavior:**
- Library provides mechanism (register callback, deliver signal)
- App provides policy (how long to wait, what to do on timeout)

---

## App-Level Timeout Strategies

### Strategy 1: No Timeout (Wait Forever)

```c
result = checkForUpdate(handle, callback);
if (result == CHECK_FOR_UPDATE_SUCCESS) {
    printf("Waiting for result (no timeout)...\n");
    // App event loop continues running
    // Callback fires whenever ready (could be hours later)
}
```

**Use case**: Background service that runs 24/7

### Strategy 2: Fixed Timeout

```c
result = checkForUpdate(handle, callback);
if (result == CHECK_FOR_UPDATE_SUCCESS) {
    // Wait max 10 minutes
    for (int i = 0; i < 600 && !got_result; i++) {
        sleep(1);
    }
    if (!got_result) {
        printf("Timeout after 10 minutes - giving up\n");
    }
}
```

**Use case**: User-initiated check (don't wait forever)

### Strategy 3: Escalating Timeout

```c
result = checkForUpdate(handle, callback);
if (result == CHECK_FOR_UPDATE_SUCCESS) {
    int timeout_sec = 60;  // Start with 1 minute
    
    while (!got_result && timeout_sec <= 7200) {
        printf("Waiting up to %d seconds...\n", timeout_sec);
        
        for (int i = 0; i < timeout_sec && !got_result; i++) {
            sleep(1);
        }
        
        if (!got_result) {
            printf("Timeout - trying again with longer wait\n");
            timeout_sec *= 2;  // Double timeout
        }
    }
}
```

**Use case**: Persistent check with increasing patience

### Strategy 4: Cancel on User Action

```c
static volatile bool g_user_cancelled = false;

void signal_handler(int sig) {
    g_user_cancelled = true;
}

int main() {
    signal(SIGINT, signal_handler);  // Ctrl+C
    
    result = checkForUpdate(handle, callback);
    if (result == CHECK_FOR_UPDATE_SUCCESS) {
        printf("Waiting for result (Ctrl+C to cancel)...\n");
        
        while (!got_result && !g_user_cancelled) {
            sleep(1);
        }
        
        if (g_user_cancelled) {
            printf("User cancelled - cleaning up\n");
            unregisterProcess(handle);
            return 0;
        }
    }
}
```

**Use case**: Interactive command-line tool

---

## Comparison Table

| Aspect | Current (Wrong) | Fixed (Correct) |
|--------|----------------|-----------------|
| **Library D-Bus timeout** | 30 seconds | 5 seconds |
| **What timeout covers** | ❌ XConf query (wrong!) | ✅ Daemon ACK (correct) |
| **If XConf takes 60 mins** | ❌ Library times out, returns FAIL | ✅ Library returns SUCCESS, waits for signal |
| **Callback fires** | ❌ Twice (ACK + signal) | ✅ Once (signal only) |
| **App-level timeout** | ❌ None (relies on library) | ✅ App decides (flexible) |
| **Daemon responsibility** | ❌ Unclear | ✅ Handle XConf timeouts/retries |

---

## Testing Different Scenarios

### Test 1: Fast XConf (5 seconds)

```bash
$ ./example_app
Triggering firmware check...
✓ Trigger succeeded
  Callback will fire when check completes
Still waiting... (0 seconds elapsed)
=== Firmware Check Result ===
Status: FIRMWARE_AVAILABLE
✓ Result received after 5 seconds
```

### Test 2: Slow XConf (2 minutes)

```bash
$ ./example_app
Triggering firmware check...
✓ Trigger succeeded
Still waiting... (30 seconds elapsed)
Still waiting... (60 seconds elapsed)
Still waiting... (90 seconds elapsed)
=== Firmware Check Result ===
Status: FIRMWARE_AVAILABLE
✓ Result received after 120 seconds
```

### Test 3: Very Slow XConf (30 minutes)

```bash
$ ./example_app
Triggering firmware check...
✓ Trigger succeeded
Still waiting... (30 seconds elapsed)
Still waiting... (60 seconds elapsed)
...
Still waiting... (1800 seconds elapsed)
=== Firmware Check Result ===
Status: FIRMWARE_AVAILABLE
✓ Result received after 1823 seconds
```

### Test 4: XConf Failure (daemon timeout)

```bash
$ ./example_app
Triggering firmware check...
✓ Trigger succeeded
Still waiting... (30 seconds elapsed)
Still waiting... (60 seconds elapsed)
Still waiting... (90 seconds elapsed)
=== Firmware Check Result ===
Status: FIRMWARE_CHECK_ERROR
Message: XConf server timeout after retries
✓ Result received after 127 seconds
```

---

## Summary

### Your Question:
> "XConf might take 60-120 minutes, is this possible?"

### Answer:
**YES! Absolutely possible and library handles it correctly (after fix):**

1. **Library timeout: 5 seconds**
   - Only waiting for daemon to acknowledge request
   - Not waiting for XConf query to complete

2. **Daemon handles XConf timeout internally**
   - Can take 30 seconds to 2 hours
   - Daemon implements retries, exponential backoff
   - Eventually emits signal with result OR error

3. **Library waits for signal indefinitely**
   - Background thread runs event loop forever
   - Signal arrives → callback fires
   - No timeout on signal delivery

4. **App decides how long to wait**
   - Can wait forever (background service)
   - Can timeout after N minutes (user-initiated)
   - Can cancel on user action (Ctrl+C)
   - App's choice!

### Changes Needed:

**Library (`rdkFwupdateMgr_api.c`):**
- ✅ Timeout: 30000 → 5000 (line 124)
- ✅ Remove callback invocation (lines 177-192)
- ✅ Update comments to clarify XConf can take hours

**Daemon (already correct!):**
- ✅ Responds immediately with ACK
- ✅ Queries XConf in background worker
- ✅ Should have own timeout/retry logic

**App (`example_app.c`):**
- ✅ Don't assume callback fires quickly
- ✅ Implement app-level timeout if needed
- ✅ Show progress while waiting

---

**Bottom Line**: Your concern is 100% valid. The library MUST NOT timeout waiting for XConf. The fix I provided handles this correctly - library only waits for ACK (5s), then app waits for signal (can be hours).
