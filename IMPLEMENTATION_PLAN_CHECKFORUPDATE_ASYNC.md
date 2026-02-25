# CheckForUpdate Async API - Implementation Plan

**Document Version:** 1.0  
**Created:** February 25, 2026  
**Status:** NOT STARTED  
**Objective:** Implement production-quality asynchronous CheckForUpdate API in librdkFwupdateMgr.so

---

## Table of Contents

1. [Overview](#overview)
2. [Architecture Summary](#architecture-summary)
3. [Phase Tracking](#phase-tracking)
4. [Phase 0: Analysis & Validation](#phase-0-analysis--validation)
5. [Phase 1: Core Data Structures](#phase-1-core-data-structures)
6. [Phase 2: Thread Safety Infrastructure](#phase-2-thread-safety-infrastructure)
7. [Phase 3: D-Bus Signal Handler](#phase-3-d-bus-signal-handler)
8. [Phase 4: Background Event Loop](#phase-4-background-event-loop)
9. [Phase 5: Async API Implementation](#phase-5-async-api-implementation)
10. [Phase 6: Memory Management](#phase-6-memory-management)
11. [Phase 7: Error Handling](#phase-7-error-handling)
12. [Phase 8: Testing & Validation](#phase-8-testing--validation)
13. [Phase 9: Documentation](#phase-9-documentation)
14. [Production Quality Requirements](#production-quality-requirements)
15. [Rollback Strategy](#rollback-strategy)

---

## Overview

### Problem Statement

The current `checkForUpdate()` API in librdkFwupdateMgr is synchronous and blocks until the daemon responds. This is unsuitable for production use where:
- Multiple plugins may call the API concurrently
- UI responsiveness must be maintained
- The operation may take significant time (network calls, etc.)

### Solution

Implement an asynchronous API that:
1. Returns immediately after sending the D-Bus method call
2. Processes the daemon's `CheckForUpdateComplete` signal in a background thread
3. Invokes user-provided callbacks with update results
4. Supports multiple concurrent requests from different plugins/handlers
5. Is thread-safe, memory-safe, and production-ready

### Key Design Decisions

1. **Single Signal, Multiple Callbacks**: The daemon emits one `CheckForUpdateComplete` signal that all waiting clients must process
2. **State Machine**: Each callback registration has a state (IDLE, WAITING, COMPLETED, CANCELLED, ERROR)
3. **Callback Registry**: Thread-safe registry of pending callbacks
4. **Background Thread**: Dedicated GLib event loop thread for signal processing
5. **No Handler ID**: Signal does not contain handler_id; all WAITING callbacks are invoked

---

## Architecture Summary

### Components

```
┌─────────────────────────────────────────────────────────────┐
│                    Plugin/Application                        │
│  ┌────────────────────────────────────────────────────────┐ │
│  │  Call: rdkFwupdateMgr_checkForUpdate_async()          │ │
│  │  Provide: callback function + user_data                │ │
│  └────────────────────────────────────────────────────────┘ │
└──────────────────────────┬──────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────────┐
│           librdkFwupdateMgr.so (Client Library)             │
│  ┌────────────────────────────────────────────────────────┐ │
│  │  Main Thread:                                          │ │
│  │  • Register callback in registry (with mutex)          │ │
│  │  • Set state = WAITING                                 │ │
│  │  • Send D-Bus method call (async)                      │ │
│  │  • Return immediately                                  │ │
│  └────────────────────────────────────────────────────────┘ │
│  ┌────────────────────────────────────────────────────────┐ │
│  │  Background Thread (GLib Event Loop):                  │ │
│  │  • Listen for CheckForUpdateComplete signal            │ │
│  │  • Lock registry                                       │ │
│  │  • Find all WAITING callbacks                          │ │
│  │  • Parse signal data                                   │ │
│  │  • Invoke each callback                                │ │
│  │  • Update state to COMPLETED                           │ │
│  │  • Clean up (optional: deregister or mark for reuse)   │ │
│  │  • Unlock registry                                     │ │
│  └────────────────────────────────────────────────────────┘ │
└──────────────────────────┬──────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────────┐
│              rdkFwupdateMgr Daemon (D-Bus)                  │
│  • Receives CheckForUpdate method call                      │
│  • Performs update check (network, XConf, etc.)             │
│  • Emits CheckForUpdateComplete signal with results         │
└─────────────────────────────────────────────────────────────┘
```

### Data Flow

1. **Registration**: Plugin calls async API → callback stored in registry with WAITING state
2. **Method Call**: Async D-Bus method call sent to daemon
3. **Signal Receipt**: Background thread receives CheckForUpdateComplete signal
4. **Callback Invocation**: All WAITING callbacks invoked with parsed data
5. **Cleanup**: Callbacks marked COMPLETED and optionally removed from registry

---

## Phase Tracking

| Phase | Sub-Phase | Description | Status | Completion Date |
|-------|-----------|-------------|--------|-----------------|
| 0 | 0.1 | Analyze daemon signal format | ✅ IMPLEMENTED | 2026-02-25 |
| 0 | 0.2 | Validate D-Bus interface definition | ✅ IMPLEMENTED | 2026-02-25 |
| 1 | 1.1 | Define state enum and structures | ✅ IMPLEMENTED | 2026-02-25 |
| 1 | 1.2 | Implement callback registry | ✅ IMPLEMENTED | 2026-02-25 |
| 1 | 1.3 | Add registry helper functions | ✅ IMPLEMENTED | 2026-02-25 |
| 2 | 2.1 | Add pthread mutex for registry | ✅ IMPLEMENTED | 2026-02-25 |
| 2 | 2.2 | Implement lock/unlock wrappers | ✅ IMPLEMENTED | 2026-02-25 |
| 2 | 2.3 | Add atomic operations where needed | ✅ IMPLEMENTED | 2026-02-25 |
| 3 | 3.1 | Implement signal handler function | ✅ IMPLEMENTED | 2026-02-25 |
| 3 | 3.2 | Parse signal data safely | ✅ IMPLEMENTED | 2026-02-25 |
| 3 | 3.3 | Connect signal handler to D-Bus | ✅ IMPLEMENTED | 2026-02-25 |
| 4 | 4.1 | Create background thread | ✅ IMPLEMENTED | 2026-02-25 |
| 4 | 4.2 | Initialize GLib event loop | ✅ IMPLEMENTED | 2026-02-25 |
| 4 | 4.3 | Implement thread lifecycle management | ✅ IMPLEMENTED | 2026-02-25 |
| 5 | 5.1 | Implement async API function | ✅ IMPLEMENTED | 2026-02-25 |
| 5 | 5.2 | Add cancellation API | ✅ IMPLEMENTED | 2026-02-25 |
| 5 | 5.3 | Update public header | ✅ IMPLEMENTED | 2026-02-25 |
| 6 | 6.1 | Implement reference counting for contexts | 🔄 IN PROGRESS | 2026-02-25 |
| 6 | 6.2 | Add cleanup on deinitialization | NOT STARTED | - |
| 6 | 6.3 | Handle memory in signal parsing | NOT STARTED | - |
| 7 | 7.1 | Add error codes and messages | NOT STARTED | - |
| 7 | 7.2 | Implement timeout handling | NOT STARTED | - |
| 7 | 7.3 | Add logging for debug/production | NOT STARTED | - |
| 8 | 8.1 | Unit tests for data structures | NOT STARTED | - |
| 8 | 8.2 | Stress test (concurrent calls) | NOT STARTED | - |
| 8 | 8.3 | Memory leak testing (valgrind) | NOT STARTED | - |
| 8 | 8.4 | Coverity static analysis | NOT STARTED | - |
| 9 | 9.1 | Update API documentation | NOT STARTED | - |
| 9 | 9.2 | Create example code | NOT STARTED | - |
| 9 | 9.3 | Update README/integration guide | NOT STARTED | - |

---

## Phase 0: Analysis & Validation

**Objective:** Understand the daemon's signal format and validate D-Bus interface before implementing.

### Sub-Phase 0.1: Analyze Daemon Signal Format

**Files to Analyze:**
- `src/dbus/rdkFwupdateMgr_handlers.c` (daemon side)
- `src/dbus/xconf_comm_status.c` (daemon side)

**Tasks:**
1. Locate the code that emits `CheckForUpdateComplete` signal
2. Document the exact signal signature (parameters, types, order)
3. Verify whether any handler_id or request_id is included
4. Document any error codes or status values emitted

**Expected Signal Format (to be confirmed):**
```c
// Signal: CheckForUpdateComplete
// Parameters:
//   - status (int): 0 = success, non-zero = error
//   - message (string): Human-readable message
//   - update_available (boolean): true if update available
//   - version (string): New version string (if available)
//   - ... (other fields to be confirmed)
```

**Acceptance Criteria:**
- [ ] Signal parameter list documented
- [ ] Signal emission code reviewed
- [ ] No handler_id confirmed (or alternative routing mechanism documented)

### Sub-Phase 0.2: Validate D-Bus Interface Definition

**Files to Check:**
- `librdkFwupdateMgr/src/rdkFwupdateMgr_process.c` (client D-Bus proxy)
- D-Bus XML interface files (if any)

**Tasks:**
1. Verify the D-Bus interface name and object path
2. Confirm method call signature for `CheckForUpdate`
3. Ensure signal subscription is possible

**Acceptance Criteria:**
- [ ] D-Bus interface name documented
- [ ] Object path documented
- [ ] Method and signal signatures confirmed

---

## Phase 1: Core Data Structures

**Objective:** Define all data structures needed for async operation.

### Sub-Phase 1.1: Define State Enum and Structures

**File:** `librdkFwupdateMgr/src/rdkFwupdateMgr_process.c` (or new internal header)

**Implementation:**

```c
// Callback state enum
typedef enum {
    CALLBACK_STATE_IDLE = 0,        // Not in use
    CALLBACK_STATE_WAITING,          // Waiting for signal
    CALLBACK_STATE_COMPLETED,        // Callback invoked
    CALLBACK_STATE_CANCELLED,        // Cancelled by user
    CALLBACK_STATE_ERROR,            // Error occurred
    CALLBACK_STATE_TIMEOUT           // Timed out
} CallbackState;

// Update information structure (parsed from signal)
typedef struct {
    int status;                      // 0 = success, non-zero = error
    char *message;                   // Human-readable message (malloc'd)
    bool update_available;           // True if update is available
    char *version;                   // New version string (malloc'd, NULL if not available)
    char *download_url;              // Download URL (malloc'd, NULL if not available)
    // Add other fields as needed based on Phase 0 analysis
} RdkUpdateInfo;

// User callback function type
typedef void (*RdkUpdateCallback)(RdkUpdateInfo *info, void *user_data);

// Internal callback context
typedef struct {
    uint32_t id;                     // Unique identifier
    CallbackState state;             // Current state
    RdkUpdateCallback callback;      // User's callback function
    void *user_data;                 // User's data pointer
    time_t registered_time;          // When registered (for timeout)
    int ref_count;                   // Reference count for safe cleanup
} CallbackContext;
```

**Acceptance Criteria:**
- [ ] All enums and structs defined
- [ ] No Coverity issues with struct layout
- [ ] Documentation comments added

### Sub-Phase 1.2: Implement Callback Registry

**File:** `librdkFwupdateMgr/src/rdkFwupdateMgr_process.c`

**Implementation:**

```c
// Global callback registry
#define MAX_CALLBACKS 64  // Maximum concurrent callbacks

typedef struct {
    CallbackContext contexts[MAX_CALLBACKS];
    pthread_mutex_t mutex;           // Protects the entire registry
    uint32_t next_id;                // Next ID to assign
    bool initialized;                // Registry initialized flag
} CallbackRegistry;

static CallbackRegistry g_callback_registry = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .next_id = 1,
    .initialized = false
};
```

**Design Decisions:**
- Fixed-size array for simplicity and predictable memory usage
- Mutex protects all access to the registry
- IDs start at 1 (0 reserved for invalid/error)

**Acceptance Criteria:**
- [ ] Registry structure defined
- [ ] Global instance created
- [ ] Mutex initialized correctly
- [ ] No memory leaks in initialization

### Sub-Phase 1.3: Add Registry Helper Functions

**File:** `librdkFwupdateMgr/src/rdkFwupdateMgr_process.c`

**Functions to Implement:**

```c
// Initialize the callback registry (called once during library init)
static int registry_init(void);

// Clean up the callback registry (called during library deinit)
static void registry_cleanup(void);

// Register a new callback (returns callback ID, or 0 on error)
static uint32_t registry_add_callback(RdkUpdateCallback callback, void *user_data);

// Remove a callback by ID
static int registry_remove_callback(uint32_t id);

// Find a callback by ID (returns pointer or NULL)
// Caller must hold mutex
static CallbackContext* registry_find_callback(uint32_t id);

// Get all callbacks in WAITING state
// Caller must hold mutex
// Returns count, fills array up to max_count
static int registry_get_waiting_callbacks(CallbackContext **out_array, int max_count);

// Update callback state
// Caller must hold mutex
static void registry_set_state(uint32_t id, CallbackState new_state);

// Increment reference count (for safe multi-threaded access)
static void context_ref(CallbackContext *ctx);

// Decrement reference count and cleanup if zero
static void context_unref(CallbackContext *ctx);
```

**Implementation Notes:**
- All functions must be thread-safe or document mutex requirements
- Error handling: return codes or NULL on failure
- Logging: use existing logging macros for debugging

**Acceptance Criteria:**
- [ ] All helper functions implemented
- [ ] Thread safety verified
- [ ] No buffer overflows or out-of-bounds access
- [ ] Coverity clean
- [ ] Unit tests written (Phase 8)

---

## Phase 2: Thread Safety Infrastructure

**Objective:** Ensure all registry operations are thread-safe.

### Sub-Phase 2.1: Add pthread Mutex for Registry

**File:** `librdkFwupdateMgr/src/rdkFwupdateMgr_process.c`

**Implementation:**

Already included in Phase 1.2 (CallbackRegistry structure includes mutex).

**Tasks:**
1. Ensure mutex is initialized during library initialization
2. Add mutex destruction during library cleanup
3. Verify no deadlocks possible

**Code Pattern:**
```c
// In rdkFwupdateMgr_init() or equivalent
if (pthread_mutex_init(&g_callback_registry.mutex, NULL) != 0) {
    // Log error
    return -1;
}
g_callback_registry.initialized = true;

// In rdkFwupdateMgr_deinit() or equivalent
if (g_callback_registry.initialized) {
    pthread_mutex_destroy(&g_callback_registry.mutex);
    g_callback_registry.initialized = false;
}
```

**Acceptance Criteria:**
- [ ] Mutex initialized correctly
- [ ] Mutex destroyed on cleanup
- [ ] No resource leaks
- [ ] Coverity clean

### Sub-Phase 2.2: Implement Lock/Unlock Wrappers

**File:** `librdkFwupdateMgr/src/rdkFwupdateMgr_process.c`

**Implementation:**

```c
// Lock the registry (blocks until acquired)
static inline void registry_lock(void) {
    pthread_mutex_lock(&g_callback_registry.mutex);
}

// Unlock the registry
static inline void registry_unlock(void) {
    pthread_mutex_unlock(&g_callback_registry.mutex);
}

// Try to lock the registry (non-blocking)
// Returns 0 on success, non-zero if already locked
static inline int registry_trylock(void) {
    return pthread_mutex_trylock(&g_callback_registry.mutex);
}
```

**Usage Pattern:**
```c
registry_lock();
// ... critical section ...
registry_unlock();
```

**Acceptance Criteria:**
- [ ] Wrappers implemented
- [ ] Used consistently throughout code
- [ ] No unlock without lock
- [ ] No double lock

### Sub-Phase 2.3: Add Atomic Operations Where Needed

**File:** `librdkFwupdateMgr/src/rdkFwupdateMgr_process.c`

**Use Cases:**
- Incrementing/decrementing reference counts
- Updating next_id

**Implementation:**

Use GLib atomic operations (already used in codebase) or C11 atomics:

```c
// Reference count operations
static void context_ref(CallbackContext *ctx) {
    if (ctx) {
        __sync_fetch_and_add(&ctx->ref_count, 1);
    }
}

static void context_unref(CallbackContext *ctx) {
    if (ctx && __sync_sub_and_fetch(&ctx->ref_count, 1) == 0) {
        // Free resources
        if (ctx->user_data) {
            // User is responsible for user_data cleanup
        }
        memset(ctx, 0, sizeof(CallbackContext));
        ctx->state = CALLBACK_STATE_IDLE;
    }
}
```

**Acceptance Criteria:**
- [ ] Atomic operations used for ref counts
- [ ] No race conditions
- [ ] Coverity clean

---

## Phase 3: D-Bus Signal Handler

**Objective:** Implement the signal handler that receives `CheckForUpdateComplete`.

### Sub-Phase 3.1: Implement Signal Handler Function

**File:** `librdkFwupdateMgr/src/rdkFwupdateMgr_process.c`

**Implementation:**

```c
// Signal handler for CheckForUpdateComplete
// Called in background thread context (GLib event loop)
static void on_check_for_update_complete_signal(
    GDBusProxy *proxy,
    const gchar *sender_name,
    const gchar *signal_name,
    GVariant *parameters,
    gpointer user_data)
{
    // Validate signal name
    if (g_strcmp0(signal_name, "CheckForUpdateComplete") != 0) {
        return;
    }

    // Parse signal parameters (Phase 3.2)
    RdkUpdateInfo info = {0};
    if (!parse_signal_parameters(parameters, &info)) {
        // Log error
        return;
    }

    // Lock registry
    registry_lock();

    // Find all WAITING callbacks
    CallbackContext *waiting_callbacks[MAX_CALLBACKS];
    int count = registry_get_waiting_callbacks(waiting_callbacks, MAX_CALLBACKS);

    // Increment reference counts while holding lock
    for (int i = 0; i < count; i++) {
        context_ref(waiting_callbacks[i]);
    }

    // Unlock registry (callbacks can execute without lock)
    registry_unlock();

    // Invoke all callbacks
    for (int i = 0; i < count; i++) {
        CallbackContext *ctx = waiting_callbacks[i];
        if (ctx && ctx->callback) {
            ctx->callback(&info, ctx->user_data);
        }

        // Lock to update state
        registry_lock();
        ctx->state = CALLBACK_STATE_COMPLETED;
        registry_unlock();

        // Release reference
        context_unref(ctx);
    }

    // Clean up parsed data
    cleanup_update_info(&info);
}
```

**Acceptance Criteria:**
- [ ] Signal handler implemented
- [ ] Thread-safe callback invocation
- [ ] No memory leaks
- [ ] Coverity clean

### Sub-Phase 3.2: Parse Signal Data Safely

**File:** `librdkFwupdateMgr/src/rdkFwupdateMgr_process.c`

**Implementation:**

```c
// Parse signal parameters into RdkUpdateInfo
// Returns true on success, false on error
static bool parse_signal_parameters(GVariant *parameters, RdkUpdateInfo *info)
{
    if (!parameters || !info) {
        return false;
    }

    // Clear output structure
    memset(info, 0, sizeof(RdkUpdateInfo));

    // Expected format (to be confirmed in Phase 0):
    // (isbs) = (status, update_available, version, message)
    // Adjust based on actual signal format

    gint status = 0;
    gboolean update_available = FALSE;
    gchar *version = NULL;
    gchar *message = NULL;

    g_variant_get(parameters, "(isbs)", &status, &message, &update_available, &version);

    info->status = status;
    info->update_available = update_available;
    
    // Duplicate strings (GVariant data is not owned by us)
    if (message) {
        info->message = strdup(message);
    }
    if (version) {
        info->version = strdup(version);
    }

    return true;
}

// Clean up RdkUpdateInfo (free malloc'd strings)
static void cleanup_update_info(RdkUpdateInfo *info)
{
    if (!info) return;

    if (info->message) {
        free(info->message);
        info->message = NULL;
    }
    if (info->version) {
        free(info->version);
        info->version = NULL;
    }
    if (info->download_url) {
        free(info->download_url);
        info->download_url = NULL;
    }
}
```

**Error Handling:**
- Validate GVariant type before parsing
- Handle NULL or malformed data gracefully
- Log parsing errors for debugging

**Acceptance Criteria:**
- [ ] Parsing function implemented
- [ ] All string allocations checked
- [ ] No buffer overflows
- [ ] Cleanup function frees all memory
- [ ] Coverity clean

### Sub-Phase 3.3: Connect Signal Handler to D-Bus

**File:** `librdkFwupdateMgr/src/rdkFwupdateMgr_process.c`

**Implementation:**

```c
// Connect to the CheckForUpdateComplete signal
// Called during library initialization or first async call
static int connect_signal_handler(void)
{
    static bool connected = false;
    if (connected) {
        return 0;  // Already connected
    }

    // Get the D-Bus proxy (existing code should have this)
    GDBusProxy *proxy = get_dbus_proxy();  // Placeholder - use actual function
    if (!proxy) {
        // Log error
        return -1;
    }

    // Connect signal handler
    g_signal_connect(proxy,
                     "g-signal",
                     G_CALLBACK(on_check_for_update_complete_signal),
                     NULL);

    connected = true;
    return 0;
}
```

**Acceptance Criteria:**
- [ ] Signal handler connected
- [ ] Connection happens once
- [ ] Error handling for connection failure
- [ ] Coverity clean

---

## Phase 4: Background Event Loop

**Objective:** Create a background thread running a GLib event loop for signal processing.

### Sub-Phase 4.1: Create Background Thread

**File:** `librdkFwupdateMgr/src/rdkFwupdateMgr_process.c`

**Implementation:**

```c
// Background thread state
static struct {
    pthread_t thread;
    GMainLoop *main_loop;
    GMainContext *context;
    bool running;
    pthread_mutex_t mutex;
} g_bg_thread = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .running = false
};

// Background thread entry point
static void* background_thread_func(void *arg)
{
    pthread_mutex_lock(&g_bg_thread.mutex);
    g_bg_thread.context = g_main_context_new();
    g_bg_thread.main_loop = g_main_loop_new(g_bg_thread.context, FALSE);
    g_bg_thread.running = true;
    pthread_mutex_unlock(&g_bg_thread.mutex);

    // Run the event loop (blocks until quit)
    g_main_loop_run(g_bg_thread.main_loop);

    // Cleanup
    pthread_mutex_lock(&g_bg_thread.mutex);
    g_main_loop_unref(g_bg_thread.main_loop);
    g_main_context_unref(g_bg_thread.context);
    g_bg_thread.main_loop = NULL;
    g_bg_thread.context = NULL;
    g_bg_thread.running = false;
    pthread_mutex_unlock(&g_bg_thread.mutex);

    return NULL;
}

// Start the background thread
static int start_background_thread(void)
{
    pthread_mutex_lock(&g_bg_thread.mutex);
    if (g_bg_thread.running) {
        pthread_mutex_unlock(&g_bg_thread.mutex);
        return 0;  // Already running
    }
    pthread_mutex_unlock(&g_bg_thread.mutex);

    int ret = pthread_create(&g_bg_thread.thread, NULL, background_thread_func, NULL);
    if (ret != 0) {
        // Log error
        return -1;
    }

    // Wait for thread to initialize
    while (true) {
        pthread_mutex_lock(&g_bg_thread.mutex);
        bool running = g_bg_thread.running;
        pthread_mutex_unlock(&g_bg_thread.mutex);
        if (running) break;
        usleep(1000);  // Sleep 1ms
    }

    return 0;
}

// Stop the background thread
static void stop_background_thread(void)
{
    pthread_mutex_lock(&g_bg_thread.mutex);
    if (!g_bg_thread.running) {
        pthread_mutex_unlock(&g_bg_thread.mutex);
        return;
    }
    
    if (g_bg_thread.main_loop) {
        g_main_loop_quit(g_bg_thread.main_loop);
    }
    pthread_mutex_unlock(&g_bg_thread.mutex);

    // Wait for thread to exit
    pthread_join(g_bg_thread.thread, NULL);
}
```

**Acceptance Criteria:**
- [ ] Thread creation successful
- [ ] Event loop runs in background
- [ ] Thread can be stopped cleanly
- [ ] No resource leaks
- [ ] Coverity clean

### Sub-Phase 4.2: Initialize GLib Event Loop

**Notes:**
- Implemented in Sub-Phase 4.1 (GMainLoop creation)
- Event loop runs in dedicated thread
- D-Bus signals are processed in this loop's context

**Acceptance Criteria:**
- [ ] GMainLoop created correctly
- [ ] GMainContext isolated (not default context)
- [ ] No interference with main thread

### Sub-Phase 4.3: Implement Thread Lifecycle Management

**File:** `librdkFwupdateMgr/src/rdkFwupdateMgr_process.c`

**Integration Points:**

```c
// In library initialization function
int rdkFwupdateMgr_init(void)
{
    // ... existing initialization ...

    // Initialize callback registry
    if (registry_init() != 0) {
        return -1;
    }

    // Start background thread
    if (start_background_thread() != 0) {
        registry_cleanup();
        return -1;
    }

    // Connect signal handler
    if (connect_signal_handler() != 0) {
        stop_background_thread();
        registry_cleanup();
        return -1;
    }

    return 0;
}

// In library cleanup function
void rdkFwupdateMgr_deinit(void)
{
    // Stop background thread
    stop_background_thread();

    // Cancel all pending callbacks
    registry_lock();
    for (int i = 0; i < MAX_CALLBACKS; i++) {
        if (g_callback_registry.contexts[i].state == CALLBACK_STATE_WAITING) {
            g_callback_registry.contexts[i].state = CALLBACK_STATE_CANCELLED;
        }
    }
    registry_unlock();

    // Clean up registry
    registry_cleanup();

    // ... existing cleanup ...
}
```

**Acceptance Criteria:**
- [ ] Thread started during init
- [ ] Thread stopped during deinit
- [ ] No crashes on cleanup
- [ ] All resources released

---

## Phase 5: Async API Implementation

**Objective:** Implement the public async API function.

### Sub-Phase 5.1: Implement Async API Function

**File:** `librdkFwupdateMgr/src/rdkFwupdateMgr_process.c`

**Implementation:**

```c
// Async CheckForUpdate API
// Returns callback ID on success, 0 on error
uint32_t rdkFwupdateMgr_checkForUpdate_async(
    RdkUpdateCallback callback,
    void *user_data)
{
    // Validate input
    if (!callback) {
        // Log error: callback is required
        return 0;
    }

    // Ensure library is initialized
    if (!g_callback_registry.initialized) {
        // Log error: library not initialized
        return 0;
    }

    // Register callback
    uint32_t callback_id = registry_add_callback(callback, user_data);
    if (callback_id == 0) {
        // Log error: registry full or error
        return 0;
    }

    // Send async D-Bus method call
    GError *error = NULL;
    GDBusProxy *proxy = get_dbus_proxy();
    if (!proxy) {
        // Log error and remove callback
        registry_remove_callback(callback_id);
        return 0;
    }

    // Call CheckForUpdate method (async, non-blocking)
    g_dbus_proxy_call(proxy,
                      "CheckForUpdate",
                      NULL,  // No parameters (or add as needed)
                      G_DBUS_CALL_FLAGS_NONE,
                      -1,    // Default timeout
                      NULL,  // No cancellable
                      NULL,  // No callback for method return (we use signal)
                      NULL);

    // Return callback ID to user
    return callback_id;
}
```

**Error Handling:**
- Validate all inputs
- Check library initialization state
- Handle D-Bus proxy errors
- Clean up on failure

**Acceptance Criteria:**
- [ ] Function implemented
- [ ] Input validation complete
- [ ] D-Bus method call sent asynchronously
- [ ] Callback ID returned
- [ ] No memory leaks on error paths
- [ ] Coverity clean

### Sub-Phase 5.2: Add Cancellation API

**File:** `librdkFwupdateMgr/src/rdkFwupdateMgr_process.c`

**Implementation:**

```c
// Cancel a pending async operation
// Returns 0 on success, -1 on error
int rdkFwupdateMgr_checkForUpdate_cancel(uint32_t callback_id)
{
    if (callback_id == 0) {
        return -1;
    }

    registry_lock();

    CallbackContext *ctx = registry_find_callback(callback_id);
    if (!ctx) {
        registry_unlock();
        return -1;  // Not found
    }

    if (ctx->state != CALLBACK_STATE_WAITING) {
        registry_unlock();
        return -1;  // Not in waiting state
    }

    // Mark as cancelled
    ctx->state = CALLBACK_STATE_CANCELLED;

    registry_unlock();

    return 0;
}
```

**Acceptance Criteria:**
- [ ] Cancellation function implemented
- [ ] State updated correctly
- [ ] Callback not invoked after cancellation
- [ ] Thread-safe

### Sub-Phase 5.3: Update Public Header

**File:** `librdkFwupdateMgr/include/rdkFwupdateMgr_client.h`

**Implementation:**

```c
// Add to header file

/**
 * @brief Update information structure
 */
typedef struct {
    int status;                      /**< Status code: 0 = success, non-zero = error */
    char *message;                   /**< Human-readable message (do not free) */
    bool update_available;           /**< True if update is available */
    char *version;                   /**< New version string (do not free, may be NULL) */
    char *download_url;              /**< Download URL (do not free, may be NULL) */
} RdkUpdateInfo;

/**
 * @brief Callback function type for async update checks
 * 
 * @param info Update information (valid only during callback)
 * @param user_data User data provided during registration
 */
typedef void (*RdkUpdateCallback)(RdkUpdateInfo *info, void *user_data);

/**
 * @brief Check for firmware update asynchronously
 * 
 * This function returns immediately after sending the request to the daemon.
 * The callback will be invoked when the daemon responds with update information.
 * 
 * @param callback Function to call when update check completes (required)
 * @param user_data User data to pass to callback (optional, can be NULL)
 * @return Callback ID on success, 0 on error
 * 
 * @note The callback may be invoked from a different thread
 * @note The RdkUpdateInfo structure is only valid during the callback
 * @note Multiple concurrent calls are supported
 */
uint32_t rdkFwupdateMgr_checkForUpdate_async(
    RdkUpdateCallback callback,
    void *user_data);

/**
 * @brief Cancel a pending async update check
 * 
 * @param callback_id ID returned by rdkFwupdateMgr_checkForUpdate_async()
 * @return 0 on success, -1 on error (not found or already completed)
 */
int rdkFwupdateMgr_checkForUpdate_cancel(uint32_t callback_id);
```

**Acceptance Criteria:**
- [ ] Header updated with new API
- [ ] Documentation comments complete
- [ ] Backward compatibility maintained (old sync API unchanged)

---

## Phase 6: Memory Management

**Objective:** Ensure no memory leaks and proper resource cleanup.

### Sub-Phase 6.1: Implement Reference Counting for Contexts

**Note:** Partially implemented in Phase 2.3 (`context_ref` and `context_unref`).

**Additional Tasks:**
- Verify reference counting is used correctly in all code paths
- Ensure no double-free or use-after-free

**Acceptance Criteria:**
- [ ] Reference counting correct
- [ ] No memory corruption
- [ ] Valgrind clean

### Sub-Phase 6.2: Add Cleanup on Deinitialization

**File:** `librdkFwupdateMgr/src/rdkFwupdateMgr_process.c`

**Implementation:**

Already covered in Phase 4.3 (`rdkFwupdateMgr_deinit()`).

**Additional Checks:**
- All malloc'd strings in `RdkUpdateInfo` are freed
- All callback contexts are cleaned up
- No dangling pointers

**Acceptance Criteria:**
- [ ] All resources freed on deinit
- [ ] No memory leaks (valgrind)
- [ ] No crashes during cleanup

### Sub-Phase 6.3: Handle Memory in Signal Parsing

**File:** `librdkFwupdateMgr/src/rdkFwupdateMgr_process.c`

**Implementation:**

Already covered in Phase 3.2 (`parse_signal_parameters` and `cleanup_update_info`).

**Additional Checks:**
- String duplication is correct (strdup)
- Cleanup function is called in all paths
- No leaks if callback throws exception (should not happen in C, but defensive)

**Acceptance Criteria:**
- [ ] All strings properly allocated and freed
- [ ] No leaks in error paths
- [ ] Coverity clean

---

## Phase 7: Error Handling

**Objective:** Robust error handling and logging.

### Sub-Phase 7.1: Add Error Codes and Messages

**File:** `librdkFwupdateMgr/include/rdkFwupdateMgr_client.h`

**Implementation:**

```c
// Error codes for async API
#define RDK_UPDATE_ERROR_NONE           0
#define RDK_UPDATE_ERROR_INVALID_PARAM  -1
#define RDK_UPDATE_ERROR_NOT_INITIALIZED -2
#define RDK_UPDATE_ERROR_REGISTRY_FULL  -3
#define RDK_UPDATE_ERROR_DBUS_FAILURE   -4
#define RDK_UPDATE_ERROR_TIMEOUT        -5
#define RDK_UPDATE_ERROR_CANCELLED      -6
```

**Usage:**
- Set error code in `RdkUpdateInfo.status`
- Provide descriptive message in `RdkUpdateInfo.message`

**Acceptance Criteria:**
- [ ] Error codes defined
- [ ] Used consistently in code
- [ ] Documented in header

### Sub-Phase 7.2: Implement Timeout Handling

**File:** `librdkFwupdateMgr/src/rdkFwupdateMgr_process.c`

**Implementation:**

```c
#define CALLBACK_TIMEOUT_SECONDS 60  // Timeout after 60 seconds

// Timeout check function (called periodically from event loop)
static gboolean check_callback_timeouts(gpointer user_data)
{
    time_t now = time(NULL);

    registry_lock();

    for (int i = 0; i < MAX_CALLBACKS; i++) {
        CallbackContext *ctx = &g_callback_registry.contexts[i];
        if (ctx->state == CALLBACK_STATE_WAITING) {
            if (difftime(now, ctx->registered_time) > CALLBACK_TIMEOUT_SECONDS) {
                // Timeout occurred
                ctx->state = CALLBACK_STATE_TIMEOUT;

                // Invoke callback with timeout error
                if (ctx->callback) {
                    RdkUpdateInfo info = {
                        .status = RDK_UPDATE_ERROR_TIMEOUT,
                        .message = "Update check timed out",
                        .update_available = false,
                        .version = NULL,
                        .download_url = NULL
                    };
                    ctx->callback(&info, ctx->user_data);
                }
            }
        }
    }

    registry_unlock();

    return G_SOURCE_CONTINUE;  // Keep timer running
}

// Add timeout timer to event loop
static void add_timeout_timer(void)
{
    // Add a timer that runs every 5 seconds
    g_timeout_add_seconds(5, check_callback_timeouts, NULL);
}
```

**Integration:**
- Call `add_timeout_timer()` after starting background thread

**Acceptance Criteria:**
- [ ] Timeout mechanism implemented
- [ ] Callbacks invoked with timeout error
- [ ] No infinite waiting

### Sub-Phase 7.3: Add Logging for Debug/Production

**File:** `librdkFwupdateMgr/src/rdkFwupdateMgr_process.c`

**Implementation:**

Use existing logging macros in the codebase (e.g., `RDK_LOG`).

**Logging Points:**
- API entry/exit
- Callback registration/removal
- Signal reception
- Callback invocation
- Errors and warnings

**Example:**
```c
RDK_LOG(RDK_LOG_INFO, "RDK_FWUPDATE", "Async checkForUpdate called, callback_id=%u\n", callback_id);
```

**Acceptance Criteria:**
- [ ] Logging added at key points
- [ ] No excessive logging (performance impact)
- [ ] Logs helpful for debugging

---

## Phase 8: Testing & Validation

**Objective:** Comprehensive testing to ensure production quality.

### Sub-Phase 8.1: Unit Tests for Data Structures

**File:** `unittest/rdkFwupdateMgr_async_gtest.cpp` (new file)

**Tests to Implement:**
1. Registry initialization and cleanup
2. Callback registration and removal
3. State transitions
4. Thread safety (concurrent add/remove)
5. Reference counting
6. Timeout handling

**Tools:**
- Google Test framework (already in project)
- Mock D-Bus responses

**Acceptance Criteria:**
- [ ] All data structure tests pass
- [ ] Code coverage > 90%
- [ ] No test failures

### Sub-Phase 8.2: Stress Test (Concurrent Calls)

**File:** `unittest/rdkFwupdateMgr_async_stress_gtest.cpp` (new file)

**Tests to Implement:**
1. 100+ concurrent async calls
2. Rapid register/cancel cycles
3. Signal received while registering new callbacks
4. Memory usage under load

**Acceptance Criteria:**
- [ ] No crashes under stress
- [ ] No deadlocks
- [ ] All callbacks invoked correctly
- [ ] Memory usage stable

### Sub-Phase 8.3: Memory Leak Testing (Valgrind)

**Command:**
```bash
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./test_async_api
```

**Tests:**
1. Run all unit tests under Valgrind
2. Check for:
   - Definite leaks
   - Possible leaks
   - Invalid memory access
   - Use-after-free

**Acceptance Criteria:**
- [ ] Zero memory leaks reported
- [ ] No invalid memory access
- [ ] Valgrind clean run

### Sub-Phase 8.4: Coverity Static Analysis

**Process:**
1. Run Coverity on modified code
2. Review all issues flagged
3. Fix all high/medium priority issues
4. Document false positives (if any)

**Common Issues to Watch:**
- Null pointer dereferences
- Buffer overflows
- Resource leaks
- Concurrency issues
- Dead code

**Acceptance Criteria:**
- [ ] Coverity analysis completed
- [ ] Zero high-priority issues
- [ ] Zero medium-priority issues
- [ ] All issues documented or fixed

---

## Phase 9: Documentation

**Objective:** Complete documentation for users and maintainers.

### Sub-Phase 9.1: Update API Documentation

**File:** `librdkFwupdateMgr/README.md` (or API doc)

**Content to Add:**
1. Async API overview
2. Usage examples
3. Thread safety notes
4. Error handling guide
5. Migration guide from sync to async API

**Acceptance Criteria:**
- [ ] Documentation complete
- [ ] Examples tested
- [ ] Reviewed by peer

### Sub-Phase 9.2: Create Example Code

**File:** `librdkFwupdateMgr/examples/example_checkforupdate_async.c` (new file)

**Implementation:**

```c
#include "rdkFwupdateMgr_client.h"
#include <stdio.h>
#include <unistd.h>

// Callback function
void update_callback(RdkUpdateInfo *info, void *user_data)
{
    const char *handler_name = (const char *)user_data;
    
    printf("[%s] Update check completed:\n", handler_name);
    printf("  Status: %d\n", info->status);
    printf("  Message: %s\n", info->message ? info->message : "N/A");
    printf("  Update Available: %s\n", info->update_available ? "Yes" : "No");
    if (info->version) {
        printf("  New Version: %s\n", info->version);
    }
}

int main(void)
{
    // Initialize library
    if (rdkFwupdateMgr_init() != 0) {
        fprintf(stderr, "Failed to initialize library\n");
        return 1;
    }

    // Register async callback
    uint32_t id = rdkFwupdateMgr_checkForUpdate_async(update_callback, "MainApp");
    if (id == 0) {
        fprintf(stderr, "Failed to register callback\n");
        rdkFwupdateMgr_deinit();
        return 1;
    }

    printf("Callback registered with ID: %u\n", id);
    printf("Waiting for update check to complete...\n");

    // Wait for callback (in real app, do other work here)
    sleep(10);

    // Optional: cancel if needed
    // rdkFwupdateMgr_checkForUpdate_cancel(id);

    // Cleanup
    rdkFwupdateMgr_deinit();

    return 0;
}
```

**Acceptance Criteria:**
- [ ] Example compiles and runs
- [ ] Demonstrates proper usage
- [ ] Includes error handling
- [ ] Comments explain each step

### Sub-Phase 9.3: Update README/Integration Guide

**Files:**
- `librdkFwupdateMgr/README.md`
- `INTEGRATION_GUIDE.md` (if exists)

**Content to Update:**
1. Add async API to feature list
2. Update build instructions (if needed)
3. Add troubleshooting section
4. Update changelog

**Acceptance Criteria:**
- [ ] README updated
- [ ] Integration guide updated
- [ ] Changelog entry added

---

## Production Quality Requirements

### Must-Have Criteria (All Phases)

1. **No Coverity Issues:**
   - Zero high-priority issues
   - Zero medium-priority issues
   - Document false positives

2. **No Threading Issues:**
   - Proper mutex usage
   - No race conditions
   - No deadlocks
   - Thread-safe data structures

3. **No Crashes:**
   - All error paths tested
   - Null pointer checks
   - Boundary condition handling

4. **No Segmentation Faults:**
   - No buffer overflows
   - No out-of-bounds access
   - No use-after-free

5. **No Memory Leaks:**
   - Valgrind clean
   - All malloc/free pairs correct
   - Reference counting correct

6. **Code Quality:**
   - Consistent style
   - Meaningful variable names
   - Comments for complex logic
   - Error messages clear

### Verification Checklist (Before Phase Completion)

For each phase, verify:
- [ ] Code compiles without warnings
- [ ] Unit tests pass
- [ ] Valgrind shows no leaks
- [ ] Coverity analysis clean
- [ ] Code reviewed
- [ ] Documentation updated

---

## Rollback Strategy

If critical issues are found late in implementation:

1. **Identify Issue Severity:**
   - Critical: Crashes, data corruption, security issues
   - High: Memory leaks, threading issues
   - Medium: Performance degradation
   - Low: Minor bugs, cosmetic issues

2. **Rollback Procedure:**
   - Revert to last known good phase
   - Document issue in this plan
   - Fix issue before proceeding

3. **Fallback Option:**
   - Keep old synchronous API available
   - Mark async API as experimental
   - Provide migration path

---

## Implementation Notes

### General Guidelines

1. **Incremental Development:**
   - Complete each phase fully before moving to next
   - Test each phase thoroughly
   - Update this document after each phase

2. **Code Review:**
   - Review code before marking phase complete
   - Use peer review if available
   - Check against production quality requirements

3. **Testing:**
   - Write tests early (TDD where possible)
   - Run tests frequently
   - Automate testing where possible

4. **Documentation:**
   - Update documentation as code changes
   - Keep examples up-to-date
   - Document design decisions

### Common Pitfalls to Avoid

1. **Callback Thread Context:**
   - User callbacks run in background thread
   - User must handle thread safety in their code
   - Document this clearly

2. **Signal Race Conditions:**
   - Signal may arrive before callback is fully registered
   - Use proper locking

3. **Memory Lifetime:**
   - RdkUpdateInfo only valid during callback
   - User must copy data if needed later

4. **Error Propagation:**
   - Always check return values
   - Propagate errors to user via callback

---

## Appendix: File Locations

### Source Files to Modify

| File | Purpose |
|------|---------|
| `librdkFwupdateMgr/src/rdkFwupdateMgr_process.c` | Main implementation |
| `librdkFwupdateMgr/include/rdkFwupdateMgr_client.h` | Public API header |
| `librdkFwupdateMgr/examples/example_checkforupdate_async.c` | Example code (new) |

### Test Files to Create

| File | Purpose |
|------|---------|
| `unittest/rdkFwupdateMgr_async_gtest.cpp` | Unit tests |
| `unittest/rdkFwupdateMgr_async_stress_gtest.cpp` | Stress tests |

### Documentation Files to Update

| File | Purpose |
|------|---------|
| `librdkFwupdateMgr/README.md` | API documentation |
| `CHANGELOG.md` | Change log |
| `IMPLEMENTATION_PLAN_CHECKFORUPDATE_ASYNC.md` | This document |

---

## Revision History

| Version | Date | Changes | Author |
|---------|------|---------|--------|
| 1.0 | 2026-02-25 | Initial creation | AI Assistant |

---

**END OF DOCUMENT**

**Next Step:** Wait for explicit confirmation to begin implementation.

**How to Resume:** If context is lost, read this document from the beginning. Check the Phase Tracking table to see which phases are complete, then continue from the next NOT STARTED phase.
