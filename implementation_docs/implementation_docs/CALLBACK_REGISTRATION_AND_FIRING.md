# Callback Registration and Firing: Complete Flow

## Your Question
> "Where does the callback registration or callback firing happen in `checkForUpdate(FirmwareInterfaceHandle handle, UpdateEventCallback callback)`?"

---

## Answer: Two Separate Locations

### 1. **Callback Registration** → `rdkFwupdateMgr_async.c`
### 2. **Callback Firing** → `rdkFwupdateMgr_async.c` (different function)

---

## Part 1: Callback Registration

### Where It's Called

**File**: `librdkFwupdateMgr/src/rdkFwupdateMgr_api.c` **Line 94**

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

    /* [2] Register callback in registry */
    if (!internal_register_callback(handle, callback)) {  // ← REGISTRATION HERE
        FWUPMGR_ERROR("checkForUpdate: registry full, handle='%s'\n", handle);
        return CHECK_FOR_UPDATE_FAIL;
    }

    /* [3] ... continue with D-Bus call ... */
```

**This is just a function call** - the actual work happens in `rdkFwupdateMgr_async.c`.

---

### Where It's Implemented

**File**: `librdkFwupdateMgr/src/rdkFwupdateMgr_async.c` **Line 399-449**

```c
/**
 * @brief Register a pending callback keyed by handle
 *
 * Stores the callback in a global registry so it can be invoked later
 * when the CheckForUpdateComplete signal arrives.
 *
 * REGISTRY STRUCTURE:
 *   - Array of MAX_PENDING_CALLBACKS slots (typically 32)
 *   - Each slot: {handle_key, callback, state, registered_time}
 *   - State: IDLE, PENDING, DISPATCHED
 *
 * SAME HANDLE TWICE:
 *   If the same handle is still PENDING from a previous call, its slot
 *   is overwritten. Prevents ghost callbacks accumulating.
 *
 * @param handle    App's FirmwareInterfaceHandle (will be strdup'd)
 * @param callback  App's 2-param UpdateEventCallback
 * @return true on success, false if registry is full
 */
bool internal_register_callback(FirmwareInterfaceHandle handle,
                                 UpdateEventCallback callback)
{
    pthread_mutex_lock(&g_registry.mutex);

    CallbackEntry *free_slot     = NULL;
    CallbackEntry *existing_slot = NULL;

    // Search for existing entry or free slot
    for (int i = 0; i < MAX_PENDING_CALLBACKS; i++) {
        CallbackEntry *e = &g_registry.entries[i];

        /* Existing pending entry for same handle → overwrite it */
        if (e->state == CB_STATE_PENDING &&
            e->handle_key != NULL &&
            strcmp(e->handle_key, handle) == 0) {
            existing_slot = e;
            break;
        }

        if (free_slot == NULL && e->state == CB_STATE_IDLE) {
            free_slot = e;
        }
    }

    CallbackEntry *target = existing_slot ? existing_slot : free_slot;

    if (target == NULL) {
        FWUPMGR_ERROR("internal_register_callback: registry full (max=%d)\n",
                      MAX_PENDING_CALLBACKS);
        pthread_mutex_unlock(&g_registry.mutex);
        return false;
    }

    if (existing_slot) {
        FWUPMGR_INFO("internal_register_callback: overwriting existing for handle='%s'\n",
                     handle);
        free(target->handle_key);
        target->handle_key = NULL;
    }

    // Store callback in registry
    target->handle_key      = strdup(handle);        // ← Store handle
    target->callback        = callback;              // ← Store callback pointer
    target->state           = CB_STATE_PENDING;      // ← Mark as pending
    target->registered_time = time(NULL);            // ← Timestamp

    pthread_mutex_unlock(&g_registry.mutex);

    FWUPMGR_INFO("internal_register_callback: registered handle='%s'\n", handle);
    return true;
}
```

**What happens:**
1. ✅ Lock mutex (thread-safe)
2. ✅ Search for existing entry with same handle OR find free slot
3. ✅ Store callback function pointer in registry
4. ✅ Store handle (strdup'd copy)
5. ✅ Mark state as PENDING
6. ✅ Unlock mutex
7. ✅ Return true (success)

**After this**: Callback is sitting in memory, waiting to be invoked when signal arrives.

---

## Part 2: Callback Firing

### The Trigger: D-Bus Signal Arrives

**File**: `librdkFwupdateMgr/src/rdkFwupdateMgr_async.c` **Line 252-283**

```c
/**
 * @brief Called by GLib when CheckForUpdateComplete signal arrives
 *
 * Runs in the background thread context (not main thread!).
 *
 *   1. Parse GVariant payload → InternalSignalData
 *   2. Dispatch to all PENDING registry entries
 *   3. Free parsed signal data
 */
static void on_check_complete_signal(GDBusConnection *conn,
                                     const gchar *sender,
                                     const gchar *object_path,
                                     const gchar *interface_name,
                                     const gchar *signal_name,
                                     GVariant *parameters,
                                     gpointer user_data)
{
    (void)conn; (void)sender; (void)object_path;
    (void)interface_name; (void)signal_name; (void)user_data;

    FWUPMGR_INFO("on_check_complete_signal: received\n");

    // Step 1: Parse signal data
    InternalSignalData signal_data;
    memset(&signal_data, 0, sizeof(signal_data));

    if (!internal_parse_signal_data(parameters, &signal_data)) {
        FWUPMGR_ERROR("on_check_complete_signal: parse failed\n");
        return;
    }

    // Step 2: Dispatch to all registered callbacks
    dispatch_all_pending(&signal_data);  // ← CALLBACK FIRING HAPPENS HERE

    // Step 3: Cleanup
    internal_cleanup_signal_data(&signal_data);
}
```

**This function is called automatically by GLib's event loop when the D-Bus signal arrives from daemon.**

---

### The Actual Callback Invocation

**File**: `librdkFwupdateMgr/src/rdkFwupdateMgr_async.c` **Line 287-377**

```c
/**
 * @brief Dispatch signal result to every PENDING callback
 *
 * TWO-PHASE DESIGN — avoids deadlock:
 *
 *   PHASE 1 (mutex held):
 *     Scan registry → snapshot all PENDING entries into local array.
 *     Mark each found entry as DISPATCHED.
 *     Release mutex.
 *
 *   PHASE 2 (mutex released):
 *     Build FwUpdateEventData from signal_data.
 *     Invoke each snapshot callback: callback(handle, &event_data)
 *     Re-acquire mutex briefly to reset each slot to IDLE.
 *
 * WHY RELEASE BEFORE CALLING CALLBACKS?
 *   If a callback called checkForUpdate() again, it would call
 *   internal_register_callback() which tries to lock the same mutex
 *   → deadlock. Releasing first makes re-entrant use safe.
 */
static void dispatch_all_pending(const InternalSignalData *signal_data)
{
    /* Local snapshot — avoids holding mutex during callback invocations */
    typedef struct {
        UpdateEventCallback  callback;
        char                 handle_copy[256];
        int                  slot_index;
    } Snapshot;

    Snapshot snapshots[MAX_PENDING_CALLBACKS];
    int      count = 0;

    /* ---- PHASE 1: Collect callbacks under mutex ---- */
    pthread_mutex_lock(&g_registry.mutex);

    for (int i = 0; i < MAX_PENDING_CALLBACKS; i++) {
        CallbackEntry *e = &g_registry.entries[i];
        if (e->state != CB_STATE_PENDING) continue;

        // Copy callback data to local stack
        snapshots[count].callback   = e->callback;        // ← Copy function pointer
        snapshots[count].slot_index = i;
        snprintf(snapshots[count].handle_copy,
                 sizeof(snapshots[count].handle_copy),
                 "%s", e->handle_key ? e->handle_key : "");  // ← Copy handle

        e->state = CB_STATE_DISPATCHED;  // Mark as dispatched
        count++;

        FWUPMGR_INFO("dispatch_all_pending: queued handle='%s'\n",
                     e->handle_key ? e->handle_key : "(null)");
    }

    pthread_mutex_unlock(&g_registry.mutex);  // ← RELEASE MUTEX BEFORE CALLBACKS

    FWUPMGR_INFO("dispatch_all_pending: %d callback(s) to fire\n", count);

    /* ---- PHASE 2: Invoke callbacks (no mutex held) ---- */

    // Map status code to enum
    CheckForUpdateStatus status = internal_map_status_code(signal_data->status_code);

    // Build event data structure
    FwUpdateEventData event_data = {
        .status            = status,                          // FIRMWARE_AVAILABLE, etc.
        .current_version   = signal_data->current_version,    // "2024.01.15"
        .available_version = signal_data->available_version,  // "2024.03.01"
        .status_message    = signal_data->status_message,     // "New firmware available"
        .update_available  = (status == FIRMWARE_AVAILABLE)   // true/false
    };

    // Fire each callback
    for (int i = 0; i < count; i++) {
        Snapshot *s = &snapshots[i];

        FWUPMGR_INFO("dispatch_all_pending: invoking callback for handle='%s'\n",
                     s->handle_copy);

        /*
         * ★★★ THIS IS THE ACTUAL CALLBACK INVOCATION ★★★
         *
         * Calls: your_callback(handle, &event_data)
         *
         * Two-parameter call:
         *   - Parameter 1: handle_copy (stack-local copy of handle string)
         *   - Parameter 2: &event_data (pointer to FwUpdateEventData struct)
         */
        s->callback(s->handle_copy, &event_data);  // ← YOUR CALLBACK FIRES HERE!

        /* Reset slot to IDLE after callback completes */
        pthread_mutex_lock(&g_registry.mutex);
        registry_reset_slot(&g_registry.entries[s->slot_index]);
        pthread_mutex_unlock(&g_registry.mutex);
    }
}
```

**What happens:**
1. ✅ Lock mutex, snapshot all PENDING callbacks to local array
2. ✅ Unlock mutex (important: avoid deadlock if callback calls checkForUpdate again)
3. ✅ Build `FwUpdateEventData` structure from signal data
4. ✅ Loop through snapshots
5. ✅ **FIRE CALLBACK**: `s->callback(s->handle_copy, &event_data)`
6. ✅ Reset registry slot to IDLE (ready for next use)

---

## Complete Flow Visualization

```
═══════════════════════════════════════════════════════════════════════════
TIME: t=0s — App calls checkForUpdate()
═══════════════════════════════════════════════════════════════════════════

┌─────────────────────────────────────────────────────────────────────────┐
│ FILE: rdkFwupdateMgr_api.c                                              │
│ FUNCTION: checkForUpdate()                                              │
│ LINE: 94                                                                 │
└─────────────────────────────────────────────────────────────────────────┘
                                   │
                                   │ Calls ↓
                                   │
┌─────────────────────────────────────────────────────────────────────────┐
│ FILE: rdkFwupdateMgr_async.c                                            │
│ FUNCTION: internal_register_callback()                                  │
│ LINE: 399-449                                                            │
│                                                                          │
│ ┌────────────────────────────────────────────────────────────────────┐ │
│ │ Registry (Global State):                                           │ │
│ │                                                                    │ │
│ │ CallbackEntry g_registry.entries[32];                             │ │
│ │                                                                    │ │
│ │ Slot 0: { handle="12345", callback=0x7f8a..., state=PENDING }    │ │
│ │         ↑                   ↑                                      │ │
│ │         Your handle         Your callback function pointer        │ │
│ │                                                                    │ │
│ │ Slot 1: { IDLE }                                                  │ │
│ │ Slot 2: { IDLE }                                                  │ │
│ │ ...                                                                │ │
│ └────────────────────────────────────────────────────────────────────┘ │
│                                                                          │
│ ✅ CALLBACK REGISTERED                                                  │
└─────────────────────────────────────────────────────────────────────────┘
                                   │
                                   │ Returns true ↑
                                   │
checkForUpdate() continues: sends D-Bus call, returns SUCCESS
App continues execution...

═══════════════════════════════════════════════════════════════════════════
TIME: t=0s to t=60m — Daemon queries XConf in background
═══════════════════════════════════════════════════════════════════════════

[App doing other work...]
[Daemon worker thread querying XConf server...]
[Registry still has callback stored, waiting...]

═══════════════════════════════════════════════════════════════════════════
TIME: t=60m — XConf query completes, daemon emits signal
═══════════════════════════════════════════════════════════════════════════

Daemon emits D-Bus signal: "CheckForUpdateComplete"
                │
                │ D-Bus delivers signal ↓
                │
┌─────────────────────────────────────────────────────────────────────────┐
│ FILE: rdkFwupdateMgr_async.c                                            │
│ FUNCTION: on_check_complete_signal()                                    │
│ LINE: 252-283                                                            │
│ THREAD: Library's background thread (GMainLoop)                         │
│                                                                          │
│ Steps:                                                                   │
│   1. Parse signal parameters (GVariant → InternalSignalData)           │
│   2. Call dispatch_all_pending(&signal_data)                            │
└─────────────────────────────────────────────────────────────────────────┘
                                   │
                                   │ Calls ↓
                                   │
┌─────────────────────────────────────────────────────────────────────────┐
│ FILE: rdkFwupdateMgr_async.c                                            │
│ FUNCTION: dispatch_all_pending()                                        │
│ LINE: 287-377                                                            │
│                                                                          │
│ PHASE 1: Scan registry (mutex locked)                                   │
│ ┌────────────────────────────────────────────────────────────────────┐ │
│ │ Find all PENDING entries:                                          │ │
│ │   Slot 0: state=PENDING, handle="12345", callback=0x7f8a...       │ │
│ │   Copy to local snapshot array                                     │ │
│ │   Mark slot as DISPATCHED                                          │ │
│ └────────────────────────────────────────────────────────────────────┘ │
│                                                                          │
│ Unlock mutex ← IMPORTANT: avoid deadlock                                │
│                                                                          │
│ PHASE 2: Invoke callbacks (mutex released)                              │
│ ┌────────────────────────────────────────────────────────────────────┐ │
│ │ Build FwUpdateEventData:                                           │ │
│ │   .status = FIRMWARE_AVAILABLE (0)                                 │ │
│ │   .current_version = "2024.01.15"                                  │ │
│ │   .available_version = "2024.03.01"                                │ │
│ │   .status_message = "New firmware available"                       │ │
│ │   .update_available = true                                         │ │
│ │                                                                    │ │
│ │ For each snapshot:                                                 │ │
│ │   ★★★ FIRE CALLBACK ★★★                                           │ │
│ │   snapshot[0].callback("12345", &event_data)                       │ │
│ │                ↑                                                   │ │
│ │                Calls your on_firmware_event()!                     │ │
│ └────────────────────────────────────────────────────────────────────┘ │
│                                                                          │
│ ✅ CALLBACK FIRED                                                       │
└─────────────────────────────────────────────────────────────────────────┘
                                   │
                                   │ Your callback executes ↓
                                   │
┌─────────────────────────────────────────────────────────────────────────┐
│ YOUR CODE: example_app.c                                                │
│ FUNCTION: on_firmware_event()                                           │
│ THREAD: Library's background thread                                     │
│                                                                          │
│ void on_firmware_event(FirmwareInterfaceHandle handle,                  │
│                        const FwUpdateEventData *event_data)              │
│ {                                                                        │
│     printf("Status: %d\n", event_data->status);                         │
│     printf("Current: %s\n", event_data->current_version);               │
│     printf("Available: %s\n", event_data->available_version);           │
│     // ... your code ...                                                │
│ }                                                                        │
│                                                                          │
│ ✅ YOUR CALLBACK COMPLETES                                              │
└─────────────────────────────────────────────────────────────────────────┘
                                   │
                                   │ Returns ↑
                                   │
dispatch_all_pending() resets registry slot to IDLE
Done!
```

---

## Key Data Structures

### CallbackRegistry (Global State)

```c
// FILE: rdkFwupdateMgr_async.c

typedef enum {
    CB_STATE_IDLE = 0,        // Slot unused
    CB_STATE_PENDING,         // Callback registered, waiting for signal
    CB_STATE_DISPATCHED       // Signal received, callback fired
} CallbackState;

typedef struct {
    char                *handle_key;       // strdup'd handle ("12345")
    UpdateEventCallback  callback;         // Function pointer to your callback
    CallbackState        state;            // IDLE, PENDING, or DISPATCHED
    time_t               registered_time;  // When registered
} CallbackEntry;

typedef struct {
    CallbackEntry  entries[MAX_PENDING_CALLBACKS];  // Array of 32 slots
    pthread_mutex_t mutex;                          // Thread-safe access
    bool            initialized;                    // Init flag
} CallbackRegistry;

static CallbackRegistry g_registry;  // ← GLOBAL STORAGE
```

### InternalSignalData (Signal Payload)

```c
// FILE: rdkFwupdateMgr_async_internal.h

typedef struct {
    int32_t  result_code;        // 0 = success, non-0 = error
    int32_t  status_code;        // 0=AVAILABLE, 1=NOT_AVAILABLE, 3=ERROR
    char    *current_version;    // "2024.01.15" (malloc'd)
    char    *available_version;  // "2024.03.01" (malloc'd)
    char    *update_details;     // JSON string (malloc'd)
    char    *status_message;     // "New firmware available" (malloc'd)
} InternalSignalData;
```

### FwUpdateEventData (App-Facing Data)

```c
// FILE: rdkFwupdateMgr_client.h

typedef struct {
    CheckForUpdateStatus  status;            // Enum: FIRMWARE_AVAILABLE, etc.
    const char           *current_version;   // Points to InternalSignalData string
    const char           *available_version; // Points to InternalSignalData string
    const char           *status_message;    // Points to InternalSignalData string
    bool                  update_available;  // Convenience: true if FIRMWARE_AVAILABLE
} FwUpdateEventData;
```

---

## Thread Context Analysis

| Location | Function | Thread | When |
|----------|----------|--------|------|
| **Registration** | `internal_register_callback()` | **Main thread** (your app's thread) | When you call `checkForUpdate()` |
| **Signal Handler** | `on_check_complete_signal()` | **Background thread** (library's GMainLoop) | When D-Bus signal arrives |
| **Dispatch** | `dispatch_all_pending()` | **Background thread** | Called by signal handler |
| **Callback Invocation** | `s->callback()` | **Background thread** | Inside dispatch function |
| **Your Code** | `on_firmware_event()` | **Background thread** ⚠️ | When callback fires |

**⚠️ IMPORTANT**: Your callback runs in the library's background thread, NOT your main thread!

**Implications:**
- ✅ Don't block in callback (quick processing only)
- ✅ Don't sleep in callback
- ✅ Use mutex if accessing shared app state
- ✅ Don't call `checkForUpdate()` from inside callback (causes complications)

---

## Summary

### Callback Registration Flow

```
checkForUpdate(handle, callback)
  ↓
internal_register_callback(handle, callback)  [Line 399, rdkFwupdateMgr_async.c]
  ↓
Lock mutex
  ↓
Find free slot in g_registry.entries[]
  ↓
Store: handle_key = strdup(handle)
       callback = callback pointer
       state = CB_STATE_PENDING
  ↓
Unlock mutex
  ↓
Return true
```

### Callback Firing Flow

```
[Daemon emits CheckForUpdateComplete signal]
  ↓
[D-Bus delivers to library's background thread]
  ↓
on_check_complete_signal(parameters)  [Line 252, rdkFwupdateMgr_async.c]
  ↓
Parse signal: parameters → InternalSignalData
  ↓
dispatch_all_pending(&signal_data)  [Line 287, rdkFwupdateMgr_async.c]
  ↓
PHASE 1: Lock mutex, snapshot all PENDING callbacks
  ↓
PHASE 2: Unlock mutex, build FwUpdateEventData
  ↓
For each snapshot:
  ★ snapshot.callback(handle, &event_data) ★  [Line 370, rdkFwupdateMgr_async.c]
    ↓
    [Your on_firmware_event() executes]
  ↓
Reset slot to IDLE
```

---

## Files Involved

| File | Functions | Purpose |
|------|-----------|---------|
| `rdkFwupdateMgr_api.c` | `checkForUpdate()` | Public API entry point, calls registration |
| `rdkFwupdateMgr_async.c` | `internal_register_callback()` | Stores callback in registry |
| `rdkFwupdateMgr_async.c` | `on_check_complete_signal()` | D-Bus signal handler (GLib callback) |
| `rdkFwupdateMgr_async.c` | `dispatch_all_pending()` | Fires all registered callbacks |
| `rdkFwupdateMgr_async_internal.h` | Type definitions | Data structures for registry, signal data |

---

## Conclusion

**Registration**: Happens immediately in `checkForUpdate()` at line 94 of `rdkFwupdateMgr_api.c`, implemented by `internal_register_callback()` at line 399 of `rdkFwupdateMgr_async.c`.

**Firing**: Happens later (5-120 minutes) when signal arrives, in `dispatch_all_pending()` at line 370 of `rdkFwupdateMgr_async.c`, inside library's background thread.

**Key Insight**: Registration is synchronous (immediate), firing is asynchronous (later, different thread).
