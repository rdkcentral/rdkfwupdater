# CheckForUpdate API - Detailed Flow Explanation

## Quick Answer: Is CheckForUpdate Synchronous?

**YES! CheckForUpdate is SYNCHRONOUS between librdkFwupdateMgr and rdkFwupdateMgr daemon.**

The library **blocks and waits** for the daemon's response before returning. Your callback is fired **immediately in your calling thread** before `checkForUpdate()` returns.

---

## Complete Step-by-Step Flow

### Architecture Overview
```
┌─────────────────┐         ┌──────────────────────┐         ┌─────────────────┐
│                 │ D-Bus   │                      │ XConf   │                 │
│  Your App       │◄───────►│  rdkFwupdateMgr      │◄───────►│  XConf Server   │
│  (example_app)  │         │  Daemon              │         │  (Firmware DB)  │
│                 │         │                      │         │                 │
└─────────────────┘         └──────────────────────┘         └─────────────────┘
        │                            │                                │
        │                            │                                │
   Uses Library                 Main Thread                     Network I/O
        │                       + Worker Thread                       │
        ▼                            │                                │
┌─────────────────┐                 │                                │
│librdkFwupdateMgr│                 │                                │
│  (client lib)   │                 │                                │
└─────────────────┘                 │                                │
```

---

## Detailed Flow Breakdown

### Phase 1: Your App Calls `checkForUpdate()`

```c
CheckForUpdateResult result = checkForUpdate(handle, on_firmware_event);
```

**What happens inside the library (`rdkFwupdateMgr_api.c`):**

#### Step 1: Parameter Validation (Lines 83-89)
```c
// Library validates inputs
if (handle == NULL || handle[0] == '\0') {
    return CHECK_FOR_UPDATE_FAIL;  // Invalid handle
}
if (callback == NULL) {
    return CHECK_FOR_UPDATE_FAIL;  // No callback provided
}
```

**💡 Decision Point**: If invalid → returns `CHECK_FOR_UPDATE_FAIL` immediately

---

#### Step 2: Register Callback in Registry (Line 95)
```c
// Store your callback for later signal delivery
internal_register_callback(handle, callback);
```

**Why?** The daemon also broadcasts a `CheckForUpdateComplete` **signal** to all listeners. Even though we get the result synchronously, the library also subscribes to this signal for multi-app scenarios.

---

#### Step 3: Connect to D-Bus (Lines 101-107)
```c
GDBusConnection *conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
if (conn == NULL) {
    return CHECK_FOR_UPDATE_FAIL;  // Can't reach D-Bus
}
```

**💡 Decision Point**: If D-Bus unavailable → returns `CHECK_FOR_UPDATE_FAIL`

---

#### Step 4: **SYNCHRONOUS** D-Bus Call (Lines 113-132)
```c
// BLOCKS HERE - Waits up to 30 seconds for daemon to respond
GVariant *reply = g_dbus_connection_call_sync(
    conn,
    "org.rdkfwupdater.Service",        // Daemon service name
    "/org/rdkfwupdater/Service",       // Object path
    "org.rdkfwupdater.Interface",      // Interface name
    "CheckForUpdate",                  // Method name
    g_variant_new("(s)", handle),      // Input: your handle
    NULL,
    G_DBUS_CALL_FLAGS_NONE,
    30000,   // ⏱️ 30 SECOND TIMEOUT - XConf queries are slow
    NULL,
    &error
);
```

**🔴 CRITICAL: This is where your app BLOCKS!**
- The library **waits** for the daemon to respond
- Your thread **cannot continue** until the daemon replies
- Maximum wait: **30 seconds** (for slow XConf network queries)

**💡 Decision Point**: If timeout or error → returns `CHECK_FOR_UPDATE_FAIL`

---

### Phase 2: Daemon Processing (What Happens While You're Blocked)

**Location**: `src/dbus/rdkv_dbus_server.c`

#### Step 5: Daemon Receives Your Request (Line 663)

```c
// Daemon extracts your handle from D-Bus message
gchar *handler_process_name = NULL;
g_variant_get(rdkv_req_payload, "(s)", &handler_process_name);
```

---

#### Step 6: Daemon Validates Your Registration (Lines 699-712)
```c
// Check: Is this handle registered?
guint64 handler_id_numeric = g_ascii_strtoull(handler_process_name, NULL, 10);
gboolean is_registered = g_hash_table_contains(registered_processes, 
                                                GINT_TO_POINTER(handler_id_numeric));

if (!is_registered) {
    // Return error response immediately
    g_dbus_method_invocation_return_value(resp_ctx,
        g_variant_new("(issssi)",
            0,   // api_result = success (graceful handling)
            "",  // current_version (empty)
            "",  // available_version (empty)
            "",  // update_details (empty)
            "Handler not registered. Call RegisterProcess first.",
            3)); // status_code = FIRMWARE_CHECK_ERROR
    return;
}
```

**💡 Decision Point**: If not registered → daemon sends error, library gets reply, your `checkForUpdate()` returns `FAIL`

---

#### Step 7: Daemon Spawns Background Worker Thread (Lines 850-905)

**Why a worker thread?** XConf queries involve **network I/O** which can take seconds. The daemon doesn't want to block its main D-Bus processing loop.

```c
// Set global flag: XConf fetch in progress
setXConfCommStatus(TRUE);

// Create context for worker thread
AsyncXconfFetchContext *async_ctx = g_new0(AsyncXconfFetchContext, 1);
async_ctx->handler_id = g_strdup(handler_process_name);
async_ctx->connection = connection;

// Launch worker thread
GTask *task = g_task_new(NULL, NULL, rdkfw_xconf_fetch_done, async_ctx);
g_task_set_task_data(task, async_ctx, NULL);
g_task_run_in_thread(task, rdkfw_xconf_fetch_worker);
```

**Important**: The daemon does **NOT** respond to your D-Bus call yet! You're still blocked waiting.

---

#### Step 8: Worker Thread Queries XConf Server (Line 2155)

**Location**: `rdkfw_xconf_fetch_worker()` function (runs in separate thread)

```c
// THIS IS THE SLOW PART - Network I/O to XConf server
CheckUpdateResponse response = rdkFwupdateMgr_checkForUpdate(ctx->handler_id);
```

**What happens here:**
1. Daemon connects to XConf server (https://xconf.example.com/...)
2. Sends device model, current firmware version, MAC address, etc.
3. XConf server checks database: Is newer firmware available?
4. XConf responds with JSON (firmware details or "no update")
5. Daemon parses JSON into `CheckUpdateResponse` structure

**Time taken**: 5-30 seconds (depends on network speed, XConf load)

---

#### Step 9: Worker Thread Returns to Main Loop (Line 2217)

```c
// Completion callback (runs back on daemon's main loop)
static void rdkfw_xconf_fetch_done(GObject *source, GAsyncResult *res, gpointer user_data)
{
    AsyncXconfFetchContext *ctx = (AsyncXconfFetchContext*)user_data;
    
    // Get result from worker thread
    CheckUpdateResponse *response = g_task_propagate_pointer(G_TASK(res), NULL);
    
    // Build D-Bus response data
    // ... (parse response into strings) ...
}
```

---

#### Step 10: Daemon Sends Synchronous Response Back (Line 730+)

```c
// Respond to YOUR waiting D-Bus call
g_dbus_method_invocation_return_value(resp_ctx,
    g_variant_new("(issssi)",
        0,                     // api_result = 0 (success)
        current_version,       // e.g., "2024.01.15"
        available_version,     // e.g., "2024.03.01" or ""
        update_details_json,   // JSON string with URL, filename, etc.
        status_message,        // "Firmware available" or "No update"
        status_code));         // 0=AVAILABLE, 1=NOT_AVAILABLE, 3=ERROR, etc.
```

**🟢 This unblocks your library's `g_dbus_connection_call_sync()`!**

---

### Phase 3: Library Processes Daemon's Response

**Back in `rdkFwupdateMgr_api.c`, your blocked thread resumes**

#### Step 11: Parse D-Bus Reply (Lines 141-159)
```c
// Extract daemon's response
gint32  api_result   = 0;
gint32  status_code  = 0;
gchar  *curr_ver     = NULL;
gchar  *avail_ver    = NULL;
gchar  *upd_details  = NULL;
gchar  *status_msg   = NULL;

g_variant_get(reply, "(issssi)",
              &api_result,      // 0 = daemon succeeded
              &curr_ver,        // "2024.01.15"
              &avail_ver,       // "2024.03.01"
              &upd_details,     // JSON string
              &status_msg,      // "New firmware available"
              &status_code);    // 0 = FIRMWARE_AVAILABLE

if (api_result != 0) {
    // Daemon reported failure
    return CHECK_FOR_UPDATE_FAIL;
}
```

---

#### Step 12: Build Event Data Structure (Lines 177-185)
```c
CheckForUpdateStatus status = internal_map_status_code(status_code);

FwUpdateEventData event_data = {
    .status            = status,           // FIRMWARE_AVAILABLE (0)
    .current_version   = curr_ver,         // "2024.01.15"
    .available_version = avail_ver,        // "2024.03.01"
    .status_message    = status_msg,       // "New firmware available"
    .update_available  = (status == FIRMWARE_AVAILABLE)  // true
};
```

---

#### Step 13: **FIRE YOUR CALLBACK** (Line 192)
```c
// Call your on_firmware_event() function RIGHT NOW
// This happens in YOUR ORIGINAL THREAD (the one that called checkForUpdate)
callback(handle, &event_data);
```

**🎯 Your callback receives:**
```c
void on_firmware_event(FirmwareInterfaceHandle handle,
                       const FwUpdateEventData *event_data)
{
    // event_data->status = FIRMWARE_AVAILABLE (0)
    // event_data->current_version = "2024.01.15"
    // event_data->available_version = "2024.03.01"
    // event_data->update_available = true
    // ... your code prints this ...
}
```

---

#### Step 14: Cleanup and Return (Lines 194-201)
```c
// Free temporary strings
g_free(curr_ver);
g_free(avail_ver);
g_free(upd_details);
g_free(status_msg);

return CHECK_FOR_UPDATE_SUCCESS;  // 🟢 Finally returns to your app!
```

---

### Phase 4: Back in Your Application

```c
// Your original call FINALLY returns
CheckForUpdateResult result = checkForUpdate(handle, on_firmware_event);
//                                                    ↑
//                              on_firmware_event() was ALREADY CALLED above!

if (result == CHECK_FOR_UPDATE_SUCCESS) {
    printf("Success! Callback already fired.\n");
    // No need to wait - callback happened inside checkForUpdate()
}
```

---

## Timeline Visualization

```
Time →
════════════════════════════════════════════════════════════════════════

Your App Thread (main):
├─ checkForUpdate(handle, callback)  ← YOU CALL THIS
│  └─ [BLOCKED - waiting for daemon] ⏱️ 5-30 seconds
│     │
│     │  Meanwhile, daemon is working:
│     │  ┌─────────────────────────────────┐
│     │  │ Daemon Main Loop:               │
│     │  │  - Validates handle ✓           │
│     │  │  - Spawns worker thread ━━━━━▶  │
│     │  │                                  │
│     │  │ Daemon Worker Thread:            │
│     │  │  - Connect to XConf              │
│     │  │  - Query firmware DB             │
│     │  │  - Parse response                │
│     │  │  - Return to main loop ━━━━━▶    │
│     │  │                                  │
│     │  │ Daemon Main Loop:               │
│     │  │  - Send D-Bus response ━━━━━▶    │
│     │  └─────────────────────────────────┘
│     │
│  ◀─ [UNBLOCKED - daemon responded]
│  └─ Parse response
│  └─ callback(handle, &event_data)  ← YOUR CALLBACK FIRES HERE
│     └─ printf("Firmware available!") ← YOUR CODE RUNS
│  └─ return CHECK_FOR_UPDATE_SUCCESS
│
◀─ result = CHECK_FOR_UPDATE_SUCCESS  ← YOUR CALL RETURNS

printf("Done!\n");  ← CONTINUE EXECUTION
```

---

## Key Insights for Your example_app.c

### ✅ What This Means for Your Code:

1. **No Threading Needed**: Your callback runs in the same thread that called `checkForUpdate()`
   - No need for `pthread_mutex`, `pthread_cond_t`, or synchronization primitives
   - No background thread in your app

2. **Simple Sequential Flow**:
   ```c
   handle = registerProcess("MyApp", "1.0");
   
   result = checkForUpdate(handle, on_callback);  // BLOCKS HERE
   // When this returns, on_callback() already executed
   
   if (result == CHECK_FOR_UPDATE_SUCCESS) {
       printf("Callback already fired - we already know the result\n");
   }
   
   unregisterProcess(handle);
   ```

3. **Timeout Handling**: If XConf is unreachable:
   - Library waits up to **30 seconds**
   - Then returns `CHECK_FOR_UPDATE_FAIL`
   - Your callback **will NOT fire**

4. **Error Cases**:
   | Error Condition | Library Behavior | Callback Fires? |
   |----------------|------------------|----------------|
   | Invalid handle | Returns `FAIL` immediately | ❌ No |
   | Not registered | Returns `FAIL` after ~1s | ❌ No |
   | D-Bus error | Returns `FAIL` after ~1s | ❌ No |
   | XConf timeout | Returns `FAIL` after 30s | ❌ No |
   | XConf success | Returns `SUCCESS` after 5-30s | ✅ Yes |

---

## What About the Signal?

**Q: You mentioned `CheckForUpdateComplete` signal. Why?**

**A: Dual delivery mechanism for robustness:**

1. **Primary path** (what we just explained):
   - Synchronous D-Bus method call/reply
   - Fast, direct, reliable

2. **Secondary path** (broadcast signal):
   - Daemon **also** emits `CheckForUpdateComplete` signal
   - All registered apps listening on D-Bus receive it
   - Useful for monitoring apps that didn't initiate the check
   - Your app's library subscribes to this too (backup delivery)

**In practice**: Your callback gets the synchronous result. The signal is redundant but harmless.

---

## Example App Modifications You Can Make

### Option 1: Minimal Example (No Threading)
```c
int main() {
    FirmwareInterfaceHandle handle = registerProcess("MyApp", "1.0");
    
    printf("Checking for updates (this will block 5-30 seconds)...\n");
    
    CheckForUpdateResult result = checkForUpdate(handle, on_firmware_event);
    // ↑ on_firmware_event() already called when this returns
    
    if (result == CHECK_FOR_UPDATE_SUCCESS) {
        printf("Check completed. See callback output above.\n");
    } else {
        printf("Check failed.\n");
    }
    
    unregisterProcess(handle);
    return 0;
}
```

### Option 2: Non-Blocking with Progress Indicator
```c
// Use alarm() or a separate progress-printing thread
static void print_dots(int sig) {
    printf(".");
    fflush(stdout);
    alarm(1);  // Re-arm
}

int main() {
    handle = registerProcess("MyApp", "1.0");
    
    signal(SIGALRM, print_dots);
    alarm(1);
    
    printf("Checking for updates");
    result = checkForUpdate(handle, on_firmware_event);
    alarm(0);  // Cancel
    
    printf("\nDone!\n");
    unregisterProcess(handle);
}
```

### Option 3: Retry Logic
```c
int main() {
    handle = registerProcess("MyApp", "1.0");
    
    int retry = 0;
    while (retry < 3) {
        printf("Attempt %d of 3...\n", retry + 1);
        
        CheckForUpdateResult r = checkForUpdate(handle, on_firmware_event);
        if (r == CHECK_FOR_UPDATE_SUCCESS) {
            break;  // Success - callback already printed result
        }
        
        printf("Failed, retrying in 5 seconds...\n");
        sleep(5);
        retry++;
    }
    
    unregisterProcess(handle);
}
```

---

## Common Mistakes to Avoid

### ❌ DON'T: Wait for a signal that already arrived
```c
// WRONG - callback already fired!
result = checkForUpdate(handle, callback);
pthread_cond_wait(&cond, &mutex);  // ← Waits forever!
```

### ❌ DON'T: Store event_data pointers
```c
// WRONG - strings are freed after callback returns
const char *saved_version = event_data->available_version;
// ... later ...
printf("%s", saved_version);  // ← SEGFAULT! Memory freed
```

**Correct**:
```c
char *saved_version = strdup(event_data->available_version);
// ... later ...
printf("%s", saved_version);
free(saved_version);
```

### ❌ DON'T: Call checkForUpdate() from callback
```c
void on_firmware_event(...) {
    checkForUpdate(handle, on_firmware_event);  // ← DEADLOCK!
}
```

---

## Summary: Is It Synchronous?

| Question | Answer |
|----------|--------|
| Does `checkForUpdate()` block? | ✅ YES - up to 30 seconds |
| Does callback fire before return? | ✅ YES - in same thread |
| Does daemon use async processing internally? | ✅ YES - worker threads |
| Do I need threading in my app? | ❌ NO - library handles it |
| Can I continue execution after `checkForUpdate()` returns? | ✅ YES - result is already known |

**Bottom Line**: From your app's perspective, it's a **synchronous blocking call**. Call it, wait, get result, continue.

---

## File References

| Component | File Path | Key Functions |
|-----------|-----------|---------------|
| Library API | `librdkFwupdateMgr/src/rdkFwupdateMgr_api.c` | `checkForUpdate()` |
| Daemon Handler | `src/dbus/rdkv_dbus_server.c` | `rdkfw_xconf_fetch_worker()` |
| XConf Logic | `src/rdkv_upgrade.c` | `rdkFwupdateMgr_checkForUpdate()` |
| Example App | `librdkFwupdateMgr/examples/example_app.c` | `main()`, `on_firmware_event()` |
| API Header | `librdkFwupdateMgr/include/rdkFwupdateMgr_client.h` | Type definitions |

---

## Next Steps

1. **Understand the flow above** ✓
2. **Examine your current `example_app.c`** - look for unnecessary threading
3. **Simplify if needed** - remove mutex/condvar if present
4. **Test timeout behavior** - disconnect network and see 30s timeout
5. **Add retry logic** - handle `CHECK_FOR_UPDATE_FAIL` gracefully

---

**Questions? Check these docs:**
- `CHECK_FOR_UPDATE_API.md` - API documentation
- `ASYNC_API_QUICK_REFERENCE.md` - Async patterns (daemon-internal)
- `IMPLEMENTATION_COMPLETE.md` - Implementation details
