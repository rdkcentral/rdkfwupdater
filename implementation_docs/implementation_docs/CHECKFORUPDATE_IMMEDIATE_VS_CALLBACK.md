# CheckForUpdate: What App Gets Immediately vs. Callback Flow

## 🔴 CRITICAL FINDING: Two-Stage Delivery Mechanism!

Your current implementation uses a **dual-response pattern**:
1. **Immediate synchronous response** (what you get when `checkForUpdate()` returns)
2. **Delayed signal-based callback** (fired later via background thread)

---

## What Happens Step-by-Step

### Stage 1: App Calls checkForUpdate()

```c
// Your app's code
CheckForUpdateResult result = checkForUpdate(handle, on_firmware_event);
//                                                     ↑
//                                            This callback is registered
```

---

### Stage 2: Library Sends D-Bus Request to Daemon

**File**: `librdkFwupdateMgr/src/rdkFwupdateMgr_api.c` (Line 113)

```c
GVariant *reply = g_dbus_connection_call_sync(
    conn,
    "org.rdkfwupdater.Service",
    "/org/rdkfwupdater/Service",
    "org.rdkfwupdater.Interface",
    "CheckForUpdate",
    g_variant_new("(s)", handle),   // Send: your handle
    NULL,
    G_DBUS_CALL_FLAGS_NONE,
    30000,   // 30 second timeout
    NULL,
    &error
);
```

**Your thread BLOCKS here** waiting for daemon's response.

---

### Stage 3: Daemon's IMMEDIATE Response

**File**: `src/dbus/rdkv_dbus_server.c` (Line 729)

```c
// 4. SEND IMMEDIATE FIRMWARE_CHECK_ERROR RESPONSE
g_dbus_method_invocation_return_value(resp_ctx,
    g_variant_new("(issssi)",
        0,   // api_result: CHECK_FOR_UPDATE_SUCCESS
        "",  // current_version: EMPTY (not known yet)
        "",  // available_version: EMPTY (not known yet)  
        "",  // update_details: EMPTY (not known yet)
        "Firmware check in progress - checking XConf server",  // status_message
        3)); // status_code: FIRMWARE_CHECK_ERROR (3 = check in progress)
```

**🔴 Key Point**: The daemon responds **immediately** with:
- **Status code: 3** (`FIRMWARE_CHECK_ERROR`)
- **Empty version strings**
- **Message**: "Firmware check in progress"

This response means: **"I received your request, now wait for the signal"**

---

### Stage 4: Library Receives Immediate Response

**File**: `librdkFwupdateMgr/src/rdkFwupdateMgr_api.c` (Lines 141-192)

```c
// Parse daemon's immediate response
gint32  api_result   = 0;
gint32  status_code  = 0;
gchar  *curr_ver     = NULL;
gchar  *avail_ver    = NULL;
gchar  *upd_details  = NULL;
gchar  *status_msg   = NULL;

g_variant_get(reply, "(issssi)",
              &api_result,      // 0 (success)
              &curr_ver,        // "" (empty!)
              &avail_ver,       // "" (empty!)
              &upd_details,     // "" (empty!)
              &status_msg,      // "Firmware check in progress..."
              &status_code);    // 3 (FIRMWARE_CHECK_ERROR)

// Check if daemon accepted the request
if (api_result != 0) {
    return CHECK_FOR_UPDATE_FAIL;  // Daemon rejected request
}

// Build event data from IMMEDIATE (incomplete) response
CheckForUpdateStatus status = internal_map_status_code(status_code);
// status = FIRMWARE_CHECK_ERROR (3)

FwUpdateEventData event_data = {
    .status            = FIRMWARE_CHECK_ERROR,  // 3
    .current_version   = "",                    // Empty!
    .available_version = "",                    // Empty!
    .status_message    = "Firmware check in progress...",
    .update_available  = false                  // Not FIRMWARE_AVAILABLE
};

// FIRE YOUR CALLBACK with this INCOMPLETE data!
callback(handle, &event_data);

// Free temporary strings
g_free(curr_ver);
g_free(avail_ver);
g_free(upd_details);
g_free(status_msg);

return CHECK_FOR_UPDATE_SUCCESS;  // Returns to your app
```

---

### Stage 5: Your App Gets Immediate Return

```c
// Your app's thread unblocks here
CheckForUpdateResult result = checkForUpdate(handle, on_firmware_event);
//                                                     ↑
//                              on_firmware_event() ALREADY CALLED ONCE
//                              with FIRMWARE_CHECK_ERROR status!

if (result == CHECK_FOR_UPDATE_SUCCESS) {
    printf("API call succeeded\n");
    // But callback only got "check in progress" status!
}
```

**What your callback received**:
```c
void on_firmware_event(FirmwareInterfaceHandle handle,
                       const FwUpdateEventData *event_data)
{
    // event_data->status = FIRMWARE_CHECK_ERROR (3)
    // event_data->current_version = "" (empty)
    // event_data->available_version = "" (empty)
    // event_data->status_message = "Firmware check in progress..."
    // event_data->update_available = false
}
```

---

### Stage 6: Daemon Processes Request in Background

**Meanwhile, back in the daemon** (while your app already returned):

**File**: `src/dbus/rdkv_dbus_server.c` (Lines 800+)

```c
// After sending immediate response, daemon spawns worker thread
AsyncXconfFetchContext *async_ctx = g_new0(AsyncXconfFetchContext, 1);
async_ctx->handler_id = g_strdup(handler_process_name);
async_ctx->connection = connection;

// Launch worker thread to do actual XConf query
GTask *task = g_task_new(NULL, NULL, rdkfw_xconf_fetch_done, async_ctx);
g_task_set_task_data(task, async_ctx, NULL);
g_task_run_in_thread(task, rdkfw_xconf_fetch_worker);
//                          ↑
//                   This does the REAL work (5-30 seconds)
```

**Worker thread** (`rdkfw_xconf_fetch_worker`):
```c
// This runs in DAEMON's worker thread, NOT your app!
CheckUpdateResponse response = rdkFwupdateMgr_checkForUpdate(ctx->handler_id);
// Queries XConf server (SLOW - network I/O)
// Gets real firmware info: versions, URLs, etc.
```

---

### Stage 7: Worker Thread Completes (5-30 seconds later)

**File**: `src/dbus/rdkv_dbus_server.c` (Line 2217+)

```c
// Completion callback (runs on daemon's main loop)
static void rdkfw_xconf_fetch_done(GObject *source, GAsyncResult *res, gpointer user_data)
{
    // Get REAL result from worker thread
    CheckUpdateResponse *response = g_task_propagate_pointer(G_TASK(res), NULL);
    
    // Broadcast CheckForUpdateComplete SIGNAL to ALL waiting clients
    g_dbus_connection_emit_signal(
        ctx->connection,
        NULL,  // destination: NULL = broadcast to all
        "/org/rdkfwupdater/Service",
        "org.rdkfwupdater.Interface",
        "CheckForUpdateComplete",
        g_variant_new("(tiissss)",
            handler_id_num,           // Handler ID
            api_result,               // 0 = success
            status_code,              // 0 = FIRMWARE_AVAILABLE or 1 = NOT_AVAILABLE
            current_version,          // "2024.01.15" (real version!)
            available_version,        // "2024.03.01" (real version!)
            update_details_json,      // JSON with URL, filename
            status_message),          // "Firmware available" or "No update"
        &error
    );
}
```

**🟢 This is the REAL result with complete data!**

---

### Stage 8: Library's Background Thread Receives Signal

**File**: `librdkFwupdateMgr/src/rdkFwupdateMgr_async.c` (Line 252+)

```c
// Background thread in YOUR APP receives D-Bus signal
static void on_check_complete_signal(GDBusConnection *conn,
                                     const gchar *sender,
                                     const gchar *object_path,
                                     const gchar *interface_name,
                                     const gchar *signal_name,
                                     GVariant *parameters,
                                     gpointer user_data)
{
    FWUPMGR_INFO("on_check_complete_signal: received\n");
    
    // Parse REAL result from signal
    guint64 handler_id;
    gint32  api_result;
    gint32  status_code;
    gchar  *curr_ver = NULL;
    gchar  *avail_ver = NULL;
    gchar  *upd_details = NULL;
    gchar  *status_msg = NULL;
    
    g_variant_get(parameters, "(tiissss)",
                  &handler_id,
                  &api_result,
                  &status_code,
                  &curr_ver,      // "2024.01.15" (real data!)
                  &avail_ver,     // "2024.03.01" (real data!)
                  &upd_details,   // JSON
                  &status_msg);   // "New firmware available"
    
    // Build REAL event data
    InternalSignalData signal_data = {
        .handler_id        = handler_id,
        .api_result        = api_result,
        .status_code       = status_code,
        .current_version   = curr_ver,
        .available_version = avail_ver,
        .status_message    = status_msg
    };
    
    // Dispatch to all registered callbacks
    dispatch_all_pending(&signal_data);
    
    g_free(curr_ver);
    g_free(avail_ver);
    g_free(upd_details);
    g_free(status_msg);
}
```

---

### Stage 9: Callback Fires AGAIN (Second Time!)

**File**: `librdkFwupdateMgr/src/rdkFwupdateMgr_async.c` (dispatch logic)

```c
static void dispatch_all_pending(const InternalSignalData *signal_data)
{
    pthread_mutex_lock(&g_registry.mutex);
    
    for (int i = 0; i < MAX_PENDING_CALLBACKS; i++) {
        CallbackEntry *entry = &g_registry.entries[i];
        
        if (entry->state != CALLBACK_STATE_PENDING) {
            continue;  // Skip unused/completed slots
        }
        
        // Match handler ID
        if (strcmp(entry->handle_key, handle_str) == 0) {
            // Build REAL event data with complete info
            FwUpdateEventData event_data = {
                .status            = internal_map_status_code(signal_data->status_code),
                .current_version   = signal_data->current_version,    // Real version!
                .available_version = signal_data->available_version,  // Real version!
                .status_message    = signal_data->status_message,
                .update_available  = (status == FIRMWARE_AVAILABLE)
            };
            
            // FIRE YOUR CALLBACK AGAIN (second time!)
            entry->callback(entry->handle_key, &event_data);
            
            // Mark as completed
            entry->state = CALLBACK_STATE_COMPLETED;
        }
    }
    
    pthread_mutex_unlock(&g_registry.mutex);
}
```

**Your callback fires AGAIN** with REAL data:
```c
void on_firmware_event(FirmwareInterfaceHandle handle,
                       const FwUpdateEventData *event_data)
{
    // SECOND CALL (from signal):
    // event_data->status = FIRMWARE_AVAILABLE (0) or FIRMWARE_NOT_AVAILABLE (1)
    // event_data->current_version = "2024.01.15" (real!)
    // event_data->available_version = "2024.03.01" (real!)
    // event_data->status_message = "New firmware available"
    // event_data->update_available = true (if update exists)
}
```

---

## Timeline Visualization

```
Time →
════════════════════════════════════════════════════════════════════════════

t=0s    Your App:
        ├─ checkForUpdate(handle, callback)
        │  └─ [BLOCKS - waiting for daemon]
        
t=0.1s  Daemon:
        ├─ Receives request
        ├─ Validates handle
        ├─ Sends IMMEDIATE response:
        │  └─ status=FIRMWARE_CHECK_ERROR (3)
        │  └─ message="Firmware check in progress"
        │  └─ versions="" (empty)
        └─ Spawns worker thread for XConf query
        
t=0.2s  Your App:
        ├─ [UNBLOCKS - received daemon's immediate response]
        ├─ Parses response
        ├─ Fires callback (FIRST TIME):
        │  └─ on_firmware_event(handle, &event_data)
        │     └─ status = FIRMWARE_CHECK_ERROR (3)
        │     └─ versions = "" (empty)
        │     └─ message = "Firmware check in progress"
        └─ checkForUpdate() RETURNS CHECK_FOR_UPDATE_SUCCESS
        
        Your app continues execution...
        printf("checkForUpdate returned\n");
        
        ┌─────────────────────────────────────────────┐
        │ Your app is FREE to do other work now!     │
        │ But the callback will fire AGAIN later...  │
        └─────────────────────────────────────────────┘

t=0.2s  Daemon (in parallel):
 to     ├─ Worker thread queries XConf server
t=10s   │  └─ Network I/O (slow)
        │  └─ Parse response
        │  └─ Build complete firmware info
        
t=10s   Daemon:
        ├─ Worker thread completes
        └─ Broadcasts CheckForUpdateComplete SIGNAL:
           └─ status=FIRMWARE_AVAILABLE (0) or FIRMWARE_NOT_AVAILABLE (1)
           └─ current_version="2024.01.15"
           └─ available_version="2024.03.01"
           └─ message="New firmware available"

t=10.1s Your App (background thread):
        ├─ Library's background thread receives signal
        ├─ Dispatches to registered callbacks
        └─ Fires callback (SECOND TIME):
           └─ on_firmware_event(handle, &event_data)
              └─ status = FIRMWARE_AVAILABLE (0)
              └─ current_version = "2024.01.15"
              └─ available_version = "2024.03.01"
              └─ message = "New firmware available"
```

---

## Summary Table

| Event | Time | Thread | Status | Versions | Message |
|-------|------|--------|--------|----------|---------|
| **checkForUpdate() called** | t=0s | Main | - | - | - |
| **Daemon sends immediate response** | t=0.1s | Main (via D-Bus) | `FIRMWARE_CHECK_ERROR` (3) | Empty (`""`) | "Check in progress" |
| **Callback fires (1st time)** | t=0.2s | Main | `FIRMWARE_CHECK_ERROR` (3) | Empty (`""`) | "Check in progress" |
| **checkForUpdate() returns** | t=0.2s | Main | Returns `SUCCESS` | - | - |
| **App continues execution** | t=0.2s+ | Main | - | - | - |
| **Daemon queries XConf** | t=0.2s - t=10s | Daemon worker | - | - | - |
| **Daemon broadcasts signal** | t=10s | Daemon main loop | `FIRMWARE_AVAILABLE` (0) or `NOT_AVAILABLE` (1) | Real versions | Real message |
| **Callback fires (2nd time)** | t=10.1s | Library background | `FIRMWARE_AVAILABLE` (0) | Real (`"2024.01.15"`) | "New firmware available" |

---

## What Your App Actually Experiences

### Scenario 1: You Only Check Return Value (❌ Wrong)

```c
result = checkForUpdate(handle, on_firmware_event);

if (result == CHECK_FOR_UPDATE_SUCCESS) {
    printf("Got firmware info!\n");  // ❌ WRONG - you only got "check in progress"
}
```

**Problem**: You don't know if firmware is actually available! You only know the daemon accepted your request.

---

### Scenario 2: You Only Use First Callback (❌ Wrong)

```c
void on_firmware_event(FirmwareInterfaceHandle handle,
                       const FwUpdateEventData *event_data)
{
    // First call: status=FIRMWARE_CHECK_ERROR, versions=""
    if (event_data->status == FIRMWARE_AVAILABLE) {
        printf("Firmware available!\n");  // Never reaches here on first call
    }
}

result = checkForUpdate(handle, on_firmware_event);
// Callback already fired with FIRMWARE_CHECK_ERROR
// Returns immediately

unregisterProcess(handle);  // ❌ Unregisters before signal arrives!
```

**Problem**: You unregister before the REAL result arrives via signal!

---

### Scenario 3: Correct Usage (✅ Right)

```c
// Global flag to track state
static volatile bool g_got_final_result = false;
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cond = PTHREAD_COND_INITIALIZER;

void on_firmware_event(FirmwareInterfaceHandle handle,
                       const FwUpdateEventData *event_data)
{
    if (event_data->status == FIRMWARE_CHECK_ERROR) {
        printf("First callback: Check in progress...\n");
        // Don't do anything yet - wait for real result
        return;
    }
    
    // Second callback: Real result!
    printf("Second callback: Got real result!\n");
    printf("Status: %d\n", event_data->status);
    printf("Current version: %s\n", event_data->current_version);
    printf("Available version: %s\n", event_data->available_version);
    
    if (event_data->status == FIRMWARE_AVAILABLE) {
        printf("NEW FIRMWARE AVAILABLE!\n");
        // Schedule download...
    }
    
    // Signal main thread that we got final result
    pthread_mutex_lock(&g_mutex);
    g_got_final_result = true;
    pthread_cond_signal(&g_cond);
    pthread_mutex_unlock(&g_mutex);
}

int main() {
    handle = registerProcess("MyApp", "1.0");
    
    result = checkForUpdate(handle, on_firmware_event);
    // Callback fires immediately with FIRMWARE_CHECK_ERROR
    
    if (result != CHECK_FOR_UPDATE_SUCCESS) {
        printf("API call failed\n");
        return 1;
    }
    
    printf("Waiting for final result...\n");
    
    // Wait for second callback with real result
    pthread_mutex_lock(&g_mutex);
    while (!g_got_final_result) {
        pthread_cond_wait(&g_cond, &g_mutex);  // Blocks until signal arrives
    }
    pthread_mutex_unlock(&g_mutex);
    
    printf("Got final result! Cleaning up...\n");
    
    unregisterProcess(handle);
    return 0;
}
```

---

## How Callback is Actually Fired (Both Times)

### First Call (Immediate - In Your Thread)

**Location**: `librdkFwupdateMgr/src/rdkFwupdateMgr_api.c` Line 192

```c
// Inside checkForUpdate(), before returning
callback(handle, &event_data);  // Direct function call
```

**Thread**: Your main thread (the one that called `checkForUpdate()`)
**Data**: Incomplete (status=3, empty versions)

---

### Second Call (Delayed - In Background Thread)

**Location**: `librdkFwupdateMgr/src/rdkFwupdateMgr_async.c` Line 400+

```c
// Inside dispatch_all_pending(), called by signal handler
entry->callback(entry->handle_key, &event_data);  // Direct function call
```

**Thread**: Library's background thread (created during library init)
**Data**: Complete (real status, real versions, real message)

---

## Why This Design?

### Advantage: Non-Blocking API

```
App calls checkForUpdate() → Returns quickly (0.2s)
                            → App continues execution
                            → Callback fires later with real result (10s)
```

**Benefit**: Your app doesn't freeze for 10+ seconds waiting for XConf.

### Disadvantage: Two Callbacks (Confusing!)

**Problem**: Callback fires TWICE with different data:
1. **First**: Incomplete "check in progress" notification
2. **Second**: Real result

**Many developers miss the second callback!**

---

## Recommendations for Your example_app.c

### Option 1: Ignore First Callback, Wait for Second

```c
void on_firmware_event(FirmwareInterfaceHandle handle,
                       const FwUpdateEventData *event_data)
{
    // Skip interim status
    if (event_data->status == FIRMWARE_CHECK_ERROR &&
        event_data->current_version == NULL) {
        printf("Checking... (interim callback)\n");
        return;  // Ignore
    }
    
    // Process real result
    printf("Final result: %d\n", event_data->status);
    // ... handle real data ...
}
```

### Option 2: Show Progress, Then Final Result

```c
void on_firmware_event(FirmwareInterfaceHandle handle,
                       const FwUpdateEventData *event_data)
{
    if (event_data->status == FIRMWARE_CHECK_ERROR) {
        printf("⏳ Checking for updates...\n");
        return;
    }
    
    // Real result
    if (event_data->status == FIRMWARE_AVAILABLE) {
        printf("✅ Update available: %s\n", event_data->available_version);
    } else {
        printf("ℹ️  Already on latest version\n");
    }
}
```

### Option 3: Add Counter to Distinguish Calls

```c
static int callback_count = 0;

void on_firmware_event(FirmwareInterfaceHandle handle,
                       const FwUpdateEventData *event_data)
{
    callback_count++;
    
    printf("\n=== Callback #%d ===\n", callback_count);
    printf("Status: %d\n", event_data->status);
    printf("Current: %s\n", event_data->current_version ?: "(none)");
    printf("Available: %s\n", event_data->available_version ?: "(none)");
    
    if (callback_count == 1) {
        printf("(This is the immediate response)\n");
    } else {
        printf("(This is the final result from XConf)\n");
    }
}
```

---

## The Real Question: Is This a Bug?

**This is a DESIGN CHOICE**, not a bug, but it's confusing!

### Alternative (Better) Design Would Be:

**Option A: Truly Synchronous (Block Until Complete)**
```c
// Block until XConf query completes (10-30s)
result = checkForUpdate(handle, callback);
// Callback fires ONCE with real data before returning
```

**Option B: Truly Asynchronous (Return Immediately, Callback Once Later)**
```c
// Return immediately (no blocking)
result = checkForUpdate(handle, callback);
// Callback fires ONCE later with real data (not called immediately)
```

**Current Implementation: Hybrid (Confusing!)**
```c
// Returns quickly but callback fires TWICE:
result = checkForUpdate(handle, callback);
// 1. Callback fires immediately with "check in progress"
// 2. Callback fires later with real result
```

---

## Files to Check

| Component | File | Function |
|-----------|------|----------|
| Library API | `librdkFwupdateMgr/src/rdkFwupdateMgr_api.c` | `checkForUpdate()` - First callback call |
| Async Engine | `librdkFwupdateMgr/src/rdkFwupdateMgr_async.c` | `dispatch_all_pending()` - Second callback call |
| Daemon Handler | `src/dbus/rdkv_dbus_server.c` | Line 729 - Immediate response |
| Daemon Worker | `src/dbus/rdkv_dbus_server.c` | `rdkfw_xconf_fetch_worker()` - Real XConf query |
| Daemon Completion | `src/dbus/rdkv_dbus_server.c` | `rdkfw_xconf_fetch_done()` - Signal broadcast |

---

## Bottom Line

### What App Gets Immediately:
✅ Return value: `CHECK_FOR_UPDATE_SUCCESS` or `CHECK_FOR_UPDATE_FAIL`
✅ First callback with status `FIRMWARE_CHECK_ERROR` (3) and empty versions
❌ NOT the real firmware check result!

### How Callback is Utilized:
1. **First time** (immediate): Called directly in your thread with "check in progress" status
2. **Second time** (later): Called from background thread with real XConf result

### Key Takeaway:
**Your callback fires TWICE!** You must handle both calls and wait for the second one to get real firmware information.
