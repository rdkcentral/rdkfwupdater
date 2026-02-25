# CheckForUpdate Asynchronous Implementation Plan

## Document Version
- **Version**: 1.0
- **Date**: February 25, 2026
- **Status**: PLANNING
- **Last Updated**: February 25, 2026

---

## Table of Contents
1. [Executive Summary](#executive-summary)
2. [Architecture Overview](#architecture-overview)
3. [Implementation Phases](#implementation-phases)
4. [Phase Tracking](#phase-tracking)
5. [Quality Assurance](#quality-assurance)
6. [Testing Strategy](#testing-strategy)

---

## Executive Summary

### Objective
Implement asynchronous `checkForUpdate()` API in `librdkFwupdateMgr` that:
- Calls D-Bus method `CheckForUpdate` (returns immediately)
- Subscribes to D-Bus signal `CheckForUpdateComplete`
- Invokes plugin callback when signal arrives
- Supports multiple concurrent plugins/requests
- Is production-ready with zero defects

### Key Requirements
- ✅ Asynchronous callback invocation via D-Bus signal
- ✅ Multi-plugin support (all waiting plugins get notified)
- ✅ Thread-safe implementation
- ✅ Zero memory leaks
- ✅ Zero Coverity issues
- ✅ Zero segmentation faults
- ✅ Production-grade error handling

### Architecture Constraints
- **Signal Format**: No handler_id in signal (broadcast to all)
- **Daemon Behavior**: Queues concurrent requests, emits ONE signal for all
- **State Management**: Each library instance tracks its own callbacks
- **GLib Event Loop**: Required for signal processing

---

## Architecture Overview

### High-Level Flow

```
┌─────────────────────────────────────────────────────────────────┐
│                       Plugin Application                         │
│  checkForUpdate(handle, callback) → returns immediately         │
│  [Plugin continues working...]                                   │
│  [Signal arrives] → callback(fwinfo) invoked automatically       │
└──────────────────────┬──────────────────────────────────────────┘
                       │
                       ↓ D-Bus Method Call
┌──────────────────────────────────────────────────────────────────┐
│                  librdkFwupdateMgr.so                            │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │ Callback Registry (Thread-Safe)                            │ │
│  │  - HashMap: handler_id → CallbackContext                   │ │
│  │  - Mutex-protected access                                  │ │
│  │  - State tracking: IDLE/WAITING/COMPLETED                  │ │
│  └────────────────────────────────────────────────────────────┘ │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │ Signal Listener (Background Thread)                        │ │
│  │  - GLib main loop running                                  │ │
│  │  - Subscribed to CheckForUpdateComplete                    │ │
│  │  - Parses signal, invokes callbacks                        │ │
│  └────────────────────────────────────────────────────────────┘ │
└──────────────────────┬──────────────────────────────────────────┘
                       │
                       ↓ D-Bus Signal
┌──────────────────────────────────────────────────────────────────┐
│                   rdkFwupdateMgr Daemon                          │
│  - Receives CheckForUpdate method calls                          │
│  - Queues concurrent requests                                    │
│  - Fetches from XConf server                                     │
│  - Emits ONE signal for all queued requests                      │
└──────────────────────────────────────────────────────────────────┘
```

### D-Bus Protocol

#### Method Call: CheckForUpdate
```
Method: org.rdkfwupdater.Interface.CheckForUpdate
Input:  (s) handler_process_name
Output: (i) immediate_result (0=SUCCESS, 1=FAIL)
```

#### Signal: CheckForUpdateComplete
```
Signal: org.rdkfwupdater.Interface.CheckForUpdateComplete
Parameters:
  (s) result_str       - "0"=SUCCESS, "1"=FAIL (API result)
  (i) status_code      - 0-5 (FIRMWARE_AVAILABLE, NOT_AVAILABLE, etc.)
  (s) curr_version     - Current firmware version string
  (s) avail_version    - Available firmware version string
  (s) update_details   - Comma-separated key:value pairs
  (s) status_msg       - Human-readable status message
```

### State Machine (Per Callback)

```
┌──────────────────────────────────────────────────────────────┐
│                    Callback Lifecycle                         │
└──────────────────────────────────────────────────────────────┘

          registerProcess()
                 ↓
          [CALLBACK_STATE_IDLE]
                 ↓
          checkForUpdate(callback)
                 ↓
       Store in registry with state=WAITING
                 ↓
          [CALLBACK_STATE_WAITING]
                 ↓
          Signal arrives
                 ↓
    Check: state == WAITING? YES
                 ↓
       Invoke callback(fwinfo)
                 ↓
       Set state = COMPLETED
                 ↓
          [CALLBACK_STATE_COMPLETED]
                 ↓
    ┌────────────┴────────────┐
    ↓                         ↓
checkForUpdate()      unregisterProcess()
called again?               called?
    ↓                         ↓
Reset to WAITING         Remove from registry
    ↓                         ↓
[CALLBACK_STATE_WAITING]  [CLEANUP]
```

---

## Implementation Phases

### PHASE 0: Prerequisites and Setup
**Status**: NOT STARTED

#### Sub-Phase 0.1: Analyze Daemon Signal Implementation
**Objective**: Verify signal format and behavior by examining daemon code

**Tasks**:
1. Read `src/dbus/rdkv_dbus_server.c` - Signal emission code
2. Locate `CheckForUpdateComplete` signal definition in XML introspection
3. Verify signal parameters match expected format
4. Document actual signal signature

**Deliverables**:
- [ ] Signal parameter verification document
- [ ] D-Bus introspection XML snippet
- [ ] Confirmation of signal format

**Quality Gates**:
- Signal parameters match design assumptions
- No handler_id in signal (confirmed)
- Broadcast behavior confirmed

---

### PHASE 1: Data Structures and State Management
**Status**: NOT STARTED

#### Sub-Phase 1.1: Define Callback State Enum
**Objective**: Create state machine enum for callback lifecycle

**Implementation**:
```c
/**
 * @brief Callback state for async operations
 * 
 * Tracks lifecycle of callback registration through signal processing.
 * Thread-safe state transitions protected by g_registry_mutex.
 */
typedef enum {
    CALLBACK_STATE_IDLE = 0,        /**< No pending request */
    CALLBACK_STATE_WAITING = 1,     /**< Waiting for signal */
    CALLBACK_STATE_COMPLETED = 2    /**< Signal received, callback invoked */
} CallbackState;
```

**Location**: `librdkFwupdateMgr/src/rdkFwupdateMgr_process.c`

**Quality Requirements**:
- ✅ No magic numbers (use enum)
- ✅ Clear documentation
- ✅ Explicit initial state (IDLE = 0)

---

#### Sub-Phase 1.2: Define CallbackContext Structure
**Objective**: Create context structure to store callback information

**Implementation**:
```c
/**
 * @brief Context for registered callback
 * 
 * Stores all information needed to invoke callback when signal arrives.
 * Lifetime: Created in checkForUpdate(), destroyed in unregisterProcess().
 * Access: Protected by g_registry_mutex.
 * 
 * MEMORY MANAGEMENT:
 * - handle: strdup'd, must free
 * - callback: Function pointer, no free needed
 * - All fields initialized to zero/NULL on creation
 */
typedef struct _CallbackContext {
    char *handle;                    /**< String handler ID (e.g., "12345") - OWNED */
    guint64 handler_id;              /**< Numeric handler ID for comparison */
    UpdateEventCallback callback;    /**< Plugin's callback function pointer */
    CallbackState state;             /**< Current state in lifecycle */
    guint64 request_timestamp;       /**< When checkForUpdate was called (for debugging) */
} CallbackContext;
```

**Location**: `librdkFwupdateMgr/src/rdkFwupdateMgr_process.c`

**Memory Safety**:
- ✅ Clear ownership documented (`handle` is OWNED)
- ✅ Initialization protocol defined
- ✅ Cleanup protocol defined

**Coverity Requirements**:
- ✅ No uninitialized fields
- ✅ No dangling pointers
- ✅ No double-free scenarios

---

#### Sub-Phase 1.3: Create Global Callback Registry
**Objective**: Implement thread-safe hash table for callback storage

**Implementation**:
```c
/* ========================================================================
 * GLOBAL STATE - SIGNAL HANDLING
 * ======================================================================== */

/**
 * @brief Global callback registry
 * 
 * Maps handler_id (uint64) to CallbackContext*
 * Access: MUST hold g_registry_mutex before read/write
 * Lifetime: Created on first use, never destroyed (process lifetime)
 * 
 * THREAD SAFETY:
 * - All access protected by g_registry_mutex
 * - Use pthread_mutex_lock/unlock around all operations
 * - No nested locks (avoid deadlock)
 * 
 * MEMORY MANAGEMENT:
 * - Key: GUINT_TO_POINTER(handler_id) - no allocation
 * - Value: CallbackContext* - allocated with malloc, freed on removal
 */
static GHashTable *g_callback_registry = NULL;

/**
 * @brief Mutex protecting callback registry
 * 
 * Protects concurrent access to g_callback_registry from:
 * - Main thread (checkForUpdate, unregisterProcess)
 * - Signal thread (on_check_update_signal)
 * 
 * DEADLOCK PREVENTION:
 * - Always acquire in same order (no nested locks)
 * - Hold for minimal time (release before callback invocation)
 * - Never call external functions while holding lock
 */
static pthread_mutex_t g_registry_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief Flag indicating registry initialization
 * 
 * Ensures registry is created exactly once.
 * Access: Protected by g_registry_mutex
 */
static bool g_registry_initialized = false;
```

**Location**: `librdkFwupdateMgr/src/rdkFwupdateMgr_process.c`

**Thread Safety Requirements**:
- ✅ Mutex initialized statically (no race condition)
- ✅ All hash table operations protected
- ✅ Documented lock ordering (deadlock prevention)
- ✅ Lock held for minimal duration

**Memory Management**:
- ✅ Hash table created with proper destroy functions
- ✅ CallbackContext freed on removal
- ✅ No memory leaks on library unload

---

#### Sub-Phase 1.4: Implement Registry Helper Functions
**Objective**: Create thread-safe helper functions for registry operations

**Functions to Implement**:

##### 1. `ensure_registry_initialized()`
```c
/**
 * @brief Initialize callback registry (idempotent)
 * 
 * Creates GHashTable on first call, subsequent calls are no-op.
 * 
 * THREAD SAFETY: Caller MUST hold g_registry_mutex
 * 
 * @pre g_registry_mutex is locked
 * @post g_callback_registry is initialized (non-NULL)
 */
static void ensure_registry_initialized(void)
{
    if (g_registry_initialized) {
        return;  // Already initialized
    }
    
    g_callback_registry = g_hash_table_new_full(
        g_direct_hash,           // Key hash function (handler_id as uint64)
        g_direct_equal,          // Key compare function
        NULL,                    // Key destroy function (no allocation)
        free_callback_context    // Value destroy function (frees CallbackContext)
    );
    
    g_registry_initialized = true;
    FWUPMGR_INFO("Callback registry initialized\n");
}
```

##### 2. `free_callback_context()`
```c
/**
 * @brief Free CallbackContext and all owned resources
 * 
 * Called by GHashTable when entry is removed.
 * 
 * MEMORY SAFETY:
 * - Frees handle string (strdup'd)
 * - Sets callback to NULL (defensive)
 * - Frees context structure itself
 * 
 * @param ctx CallbackContext* to free (can be NULL)
 */
static void free_callback_context(gpointer ctx)
{
    if (!ctx) {
        return;
    }
    
    CallbackContext *context = (CallbackContext*)ctx;
    
    // Free owned string
    if (context->handle) {
        free(context->handle);
        context->handle = NULL;
    }
    
    // Clear callback pointer (defensive)
    context->callback = NULL;
    
    // Free structure
    free(context);
}
```

##### 3. `add_callback_to_registry()`
```c
/**
 * @brief Add callback to registry
 * 
 * THREAD SAFETY: Caller MUST hold g_registry_mutex
 * 
 * @param handler_id Numeric handler ID (key)
 * @param ctx CallbackContext* (value, ownership transferred)
 * 
 * @pre g_registry_mutex is locked
 * @pre ctx is non-NULL and fully initialized
 * @post ctx is owned by registry (don't free manually)
 */
static void add_callback_to_registry(guint64 handler_id, CallbackContext *ctx)
{
    ensure_registry_initialized();
    g_hash_table_insert(g_callback_registry, GUINT_TO_POINTER(handler_id), ctx);
    FWUPMGR_INFO("Callback added to registry: handler_id=%lu\n", handler_id);
}
```

##### 4. `remove_callback_from_registry()`
```c
/**
 * @brief Remove callback from registry
 * 
 * THREAD SAFETY: Caller MUST hold g_registry_mutex
 * 
 * @param handler_id Numeric handler ID
 * 
 * @pre g_registry_mutex is locked
 * @post CallbackContext is freed (if exists)
 */
static void remove_callback_from_registry(guint64 handler_id)
{
    if (!g_callback_registry) {
        return;
    }
    
    gboolean removed = g_hash_table_remove(g_callback_registry, GUINT_TO_POINTER(handler_id));
    if (removed) {
        FWUPMGR_INFO("Callback removed from registry: handler_id=%lu\n", handler_id);
    }
}
```

**Quality Requirements**:
- ✅ All functions clearly document thread safety requirements
- ✅ Preconditions and postconditions specified
- ✅ Memory ownership clearly documented
- ✅ Defensive NULL checks

**Coverity Prevention**:
- ✅ No operations without lock held (documented in preconditions)
- ✅ No double-free (destroy function clears pointers)
- ✅ No use-after-free (ownership transfer documented)

---

### PHASE 2: Background Thread and Event Loop
**Status**: NOT STARTED

#### Sub-Phase 2.1: Define Thread Management Globals
**Objective**: Create globals for background thread lifecycle management

**Implementation**:
```c
/* ========================================================================
 * BACKGROUND THREAD - SIGNAL PROCESSING
 * ======================================================================== */

/**
 * @brief Background thread for GLib event loop
 * 
 * Runs GLib main loop to process D-Bus signals.
 * Lifetime: Created on first checkForUpdate(), runs until library unload.
 * 
 * THREAD SAFETY:
 * - Creation protected by g_signal_thread_mutex
 * - Only created once (checked by g_signal_thread_created)
 */
static pthread_t g_signal_thread;

/**
 * @brief Flag indicating thread creation
 * 
 * Ensures thread is created exactly once.
 * Access: Protected by g_signal_thread_mutex
 */
static bool g_signal_thread_created = false;

/**
 * @brief Mutex protecting thread creation
 * 
 * Prevents race condition when multiple threads call checkForUpdate()
 * simultaneously for first time.
 */
static pthread_mutex_t g_signal_thread_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief GLib main loop for signal processing
 * 
 * Created and run by background thread.
 * Stopped during library cleanup.
 * 
 * THREAD SAFETY:
 * - Created on signal thread
 * - Accessed only from signal thread (g_main_loop_quit in cleanup)
 */
static GMainLoop *g_signal_main_loop = NULL;

/**
 * @brief D-Bus connection for signal subscription
 * 
 * System bus connection used to subscribe to signals.
 * Lifetime: Created on signal thread, unref'd during cleanup.
 * 
 * MEMORY MANAGEMENT:
 * - Created with g_bus_get_sync()
 * - Freed with g_object_unref()
 */
static GDBusConnection *g_signal_dbus_connection = NULL;

/**
 * @brief Signal subscription ID
 * 
 * Returned by g_dbus_connection_signal_subscribe().
 * Used to unsubscribe during cleanup.
 * 0 = not subscribed
 */
static guint g_signal_subscription_id = 0;
```

**Quality Requirements**:
- ✅ Clear lifetime documentation
- ✅ Thread ownership specified
- ✅ Memory management strategy documented

---

#### Sub-Phase 2.2: Implement Signal Handler Function
**Objective**: Create D-Bus signal handler that invokes callbacks

**Implementation**:
```c
/**
 * @brief D-Bus signal handler for CheckForUpdateComplete
 * 
 * Called by GLib main loop when signal arrives.
 * Runs on background thread (g_signal_thread).
 * 
 * BEHAVIOR:
 * 1. Parse signal parameters
 * 2. Find ALL callbacks in WAITING state
 * 3. Build FwInfoData structure ONCE
 * 4. Invoke all waiting callbacks with same data
 * 5. Update callback states to COMPLETED
 * 6. Free allocated memory
 * 
 * THREAD SAFETY:
 * - Runs on signal thread (not main thread)
 * - Acquires g_registry_mutex to access callbacks
 * - Releases mutex BEFORE invoking callbacks (avoid deadlock)
 * - Callbacks execute on signal thread context
 * 
 * MEMORY MANAGEMENT:
 * - Parses signal parameters (strings are GLib-owned, don't free)
 * - Allocates FwInfoData and UpdateDetails on heap
 * - Frees structures after all callbacks invoked
 * - Uses g_free() for GLib strings, free() for malloc'd memory
 * 
 * @param connection D-Bus connection (unused)
 * @param sender_name Sender bus name (unused)
 * @param object_path Object path (unused)
 * @param interface_name Interface name (unused)
 * @param signal_name Signal name (unused)
 * @param parameters Signal parameters (GVariant tuple)
 * @param user_data User data (unused)
 */
static void on_check_update_signal(
    GDBusConnection *connection,
    const gchar *sender_name,
    const gchar *object_path,
    const gchar *interface_name,
    const gchar *signal_name,
    GVariant *parameters,
    gpointer user_data)
{
    FWUPMGR_INFO("Signal received: CheckForUpdateComplete\n");
    
    // Parse signal parameters (strings are GLib-owned, use g_free later)
    const gchar *result_str = NULL;
    gint32 status_code = 0;
    gchar *curr_version = NULL;
    gchar *avail_version = NULL;
    gchar *update_details_str = NULL;
    gchar *status_msg = NULL;
    
    g_variant_get(parameters, "(&si&s&s&s&s)",
                  &result_str,           // & = no copy (borrowed)
                  &status_code,
                  &curr_version,         // & = no copy
                  &avail_version,        // & = no copy
                  &update_details_str,   // & = no copy
                  &status_msg);          // & = no copy
    
    FWUPMGR_INFO("Signal data: result=%s, status=%d, curr=%s, avail=%s\n",
                 result_str ? result_str : "(null)",
                 status_code,
                 curr_version ? curr_version : "(null)",
                 avail_version ? avail_version : "(null)");
    
    // Find all callbacks in WAITING state
    pthread_mutex_lock(&g_registry_mutex);
    
    if (!g_callback_registry) {
        FWUPMGR_WARN("Signal received but registry not initialized\n");
        pthread_mutex_unlock(&g_registry_mutex);
        return;
    }
    
    // Collect callbacks to invoke (copy to list to avoid holding lock)
    GSList *callbacks_to_invoke = NULL;
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, g_callback_registry);
    
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        CallbackContext *ctx = (CallbackContext*)value;
        
        if (ctx && ctx->state == CALLBACK_STATE_WAITING) {
            FWUPMGR_INFO("Found waiting callback: handler_id=%lu\n", ctx->handler_id);
            callbacks_to_invoke = g_slist_append(callbacks_to_invoke, ctx);
            
            // Update state while holding lock
            ctx->state = CALLBACK_STATE_COMPLETED;
        }
    }
    
    pthread_mutex_unlock(&g_registry_mutex);
    
    // If no callbacks waiting, nothing to do
    if (!callbacks_to_invoke) {
        FWUPMGR_INFO("No callbacks waiting for this signal\n");
        return;
    }
    
    // Build FwInfoData structure (allocate on heap)
    FwInfoData *fwinfo = (FwInfoData*)malloc(sizeof(FwInfoData));
    UpdateDetails *details = (UpdateDetails*)malloc(sizeof(UpdateDetails));
    
    if (!fwinfo || !details) {
        FWUPMGR_ERROR("Failed to allocate memory for FwInfoData\n");
        g_slist_free(callbacks_to_invoke);
        free(fwinfo);
        free(details);
        return;
    }
    
    // Initialize structures
    memset(fwinfo, 0, sizeof(FwInfoData));
    memset(details, 0, sizeof(UpdateDetails));
    
    // Fill FwInfoData
    if (curr_version) {
        strncpy(fwinfo->CurrFWVersion, curr_version, MAX_FW_VERSION_SIZE - 1);
        fwinfo->CurrFWVersion[MAX_FW_VERSION_SIZE - 1] = '\0';  // Ensure null termination
    }
    fwinfo->status = (CheckForUpdateStatus)status_code;
    fwinfo->UpdateDetails = details;
    
    // Parse and fill UpdateDetails
    if (update_details_str && strlen(update_details_str) > 0) {
        parse_update_details(update_details_str, details);
    }
    
    // Invoke all waiting callbacks with same data
    GSList *node;
    int callback_count = 0;
    for (node = callbacks_to_invoke; node != NULL; node = node->next) {
        CallbackContext *ctx = (CallbackContext*)node->data;
        
        if (ctx && ctx->callback) {
            FWUPMGR_INFO("Invoking callback #%d for handler=%s\n", 
                         ++callback_count, ctx->handle ? ctx->handle : "(null)");
            
            // Invoke callback (runs on signal thread!)
            ctx->callback(fwinfo);
        }
    }
    
    FWUPMGR_INFO("Invoked %d callbacks\n", callback_count);
    
    // Cleanup
    g_slist_free(callbacks_to_invoke);
    free(details);
    free(fwinfo);
    
    // Note: Signal parameters are borrowed references, no need to free
}
```

**Coverity Requirements**:
- ✅ NULL checks before dereference
- ✅ Bounds checking on string copies (strncpy with explicit null termination)
- ✅ Memory freed on all paths (error and success)
- ✅ No use-after-free (callbacks_to_invoke is list of valid pointers)
- ✅ Lock released before callbacks (deadlock prevention)

**Thread Safety**:
- ✅ Lock held only during registry access
- ✅ Lock released before callback invocation (callbacks may be slow)
- ✅ State updated while holding lock (atomic transition)

---

#### Sub-Phase 2.3: Implement Background Thread Entry Point
**Objective**: Create thread function that runs GLib event loop

**Implementation**:
```c
/**
 * @brief Background thread entry point
 * 
 * Runs GLib main loop to process D-Bus signals.
 * Thread lifetime: Until g_main_loop_quit() is called during cleanup.
 * 
 * INITIALIZATION:
 * 1. Connect to D-Bus system bus
 * 2. Subscribe to CheckForUpdateComplete signal
 * 3. Create GLib main loop
 * 4. Run main loop (blocks until quit)
 * 
 * CLEANUP:
 * - Unsubscribe from signal
 * - Unref D-Bus connection
 * - Unref main loop
 * 
 * @param arg Unused thread argument
 * @return NULL always
 */
static void* signal_thread_func(void *arg)
{
    (void)arg;  // Unused
    
    FWUPMGR_INFO("Signal thread started\n");
    
    // Connect to D-Bus system bus
    GError *error = NULL;
    g_signal_dbus_connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    
    if (!g_signal_dbus_connection) {
        FWUPMGR_ERROR("Signal thread: Failed to connect to D-Bus: %s\n",
                      error ? error->message : "unknown error");
        if (error) {
            g_error_free(error);
        }
        return NULL;
    }
    
    FWUPMGR_INFO("Signal thread: Connected to D-Bus\n");
    
    // Subscribe to CheckForUpdateComplete signal
    g_signal_subscription_id = g_dbus_connection_signal_subscribe(
        g_signal_dbus_connection,
        "org.rdkfwupdater.Interface",      // Sender (NULL = any sender)
        "org.rdkfwupdater.Interface",      // Interface
        "CheckForUpdateComplete",          // Member (signal name)
        "/org/rdkfwupdater/Service",       // Object path
        NULL,                              // arg0 filter (none)
        G_DBUS_SIGNAL_FLAGS_NONE,
        on_check_update_signal,            // Callback
        NULL,                              // User data
        NULL                               // User data free function
    );
    
    if (g_signal_subscription_id == 0) {
        FWUPMGR_ERROR("Signal thread: Failed to subscribe to signal\n");
        g_object_unref(g_signal_dbus_connection);
        g_signal_dbus_connection = NULL;
        return NULL;
    }
    
    FWUPMGR_INFO("Signal thread: Subscribed to CheckForUpdateComplete (id=%u)\n",
                 g_signal_subscription_id);
    
    // Create GLib main loop
    g_signal_main_loop = g_main_loop_new(NULL, FALSE);
    
    if (!g_signal_main_loop) {
        FWUPMGR_ERROR("Signal thread: Failed to create main loop\n");
        g_dbus_connection_signal_unsubscribe(g_signal_dbus_connection, g_signal_subscription_id);
        g_object_unref(g_signal_dbus_connection);
        g_signal_dbus_connection = NULL;
        g_signal_subscription_id = 0;
        return NULL;
    }
    
    FWUPMGR_INFO("Signal thread: Starting main loop\n");
    
    // Run main loop (blocks until g_main_loop_quit)
    g_main_loop_run(g_signal_main_loop);
    
    FWUPMGR_INFO("Signal thread: Main loop stopped\n");
    
    // Cleanup
    if (g_signal_subscription_id != 0) {
        g_dbus_connection_signal_unsubscribe(g_signal_dbus_connection, g_signal_subscription_id);
        g_signal_subscription_id = 0;
    }
    
    if (g_signal_dbus_connection) {
        g_object_unref(g_signal_dbus_connection);
        g_signal_dbus_connection = NULL;
    }
    
    if (g_signal_main_loop) {
        g_main_loop_unref(g_signal_main_loop);
        g_signal_main_loop = NULL;
    }
    
    FWUPMGR_INFO("Signal thread exiting\n");
    return NULL;
}
```

**Error Handling**:
- ✅ All initialization steps checked
- ✅ Cleanup on failure paths
- ✅ Resources freed before return

**Memory Management**:
- ✅ D-Bus connection ref'd and unref'd
- ✅ Main loop created and destroyed
- ✅ Subscription ID tracked and unsubscribed

---

#### Sub-Phase 2.4: Implement Thread Startup Function
**Objective**: Create function to start background thread (idempotent)

**Implementation**:
```c
/**
 * @brief Ensure signal thread is running (idempotent)
 * 
 * Creates and starts background thread on first call.
 * Subsequent calls are no-op.
 * 
 * THREAD SAFETY:
 * - Protected by g_signal_thread_mutex
 * - Only creates thread once (checked by g_signal_thread_created)
 * 
 * @return true if thread is running (or successfully started)
 * @return false if thread creation failed
 */
static bool ensure_signal_thread_running(void)
{
    pthread_mutex_lock(&g_signal_thread_mutex);
    
    if (g_signal_thread_created) {
        // Thread already running
        pthread_mutex_unlock(&g_signal_thread_mutex);
        return true;
    }
    
    FWUPMGR_INFO("Creating signal processing thread\n");
    
    // Create detached thread (no need to join)
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    
    int ret = pthread_create(&g_signal_thread, &attr, signal_thread_func, NULL);
    pthread_attr_destroy(&attr);
    
    if (ret != 0) {
        FWUPMGR_ERROR("Failed to create signal thread: %s\n", strerror(ret));
        pthread_mutex_unlock(&g_signal_thread_mutex);
        return false;
    }
    
    g_signal_thread_created = true;
    
    pthread_mutex_unlock(&g_signal_thread_mutex);
    
    FWUPMGR_INFO("Signal thread created successfully\n");
    
    // Give thread time to initialize (avoid race with signal subscription)
    usleep(100000);  // 100ms
    
    return true;
}
```

**Quality Requirements**:
- ✅ Idempotent (safe to call multiple times)
- ✅ Thread-safe (mutex-protected)
- ✅ Proper pthread attribute cleanup
- ✅ Detached thread (no resource leak on exit)

---

### PHASE 3: checkForUpdate() Implementation
**Status**: NOT STARTED

#### Sub-Phase 3.1: Update checkForUpdate() Function
**Objective**: Modify checkForUpdate() to be asynchronous with signal handling

**Implementation**:
```c
/**
 * @brief Check for available firmware updates (ASYNCHRONOUS)
 * 
 * Initiates firmware update check. Returns immediately.
 * Callback is invoked later when D-Bus signal arrives.
 * 
 * FLOW:
 * 1. Validate parameters
 * 2. Ensure signal thread is running
 * 3. Store callback in registry with state=WAITING
 * 4. Call D-Bus method CheckForUpdate
 * 5. Return SUCCESS (callback will be invoked asynchronously)
 * 
 * CALLBACK INVOCATION:
 * - Callback is invoked on background thread (signal thread)
 * - Callback should be thread-safe or use synchronization
 * - Callback should NOT call checkForUpdate() (reentrant)
 * - Callback receives FwInfoData* (valid only during callback)
 * 
 * CONCURRENCY:
 * - Multiple plugins can call checkForUpdate() simultaneously
 * - Each gets queued by daemon, ONE signal emitted for all
 * - All waiting callbacks invoked with same firmware data
 * 
 * @param handle Handler ID from registerProcess()
 * @param callback Callback function to invoke when signal arrives
 * @return CHECK_FOR_UPDATE_SUCCESS if request initiated
 * @return CHECK_FOR_UPDATE_FAIL if validation failed or D-Bus error
 */
CheckForUpdateResult checkForUpdate(FirmwareInterfaceHandle handle, 
                                    UpdateEventCallback callback)
{
    FWUPMGR_INFO("checkForUpdate() called\n");

    // Validate parameters
    if (!handle) {
        FWUPMGR_ERROR("checkForUpdate: handle is NULL\n");
        return CHECK_FOR_UPDATE_FAIL;
    }

    if (!callback) {
        FWUPMGR_ERROR("checkForUpdate: callback is NULL\n");
        return CHECK_FOR_UPDATE_FAIL;
    }

    FWUPMGR_INFO("checkForUpdate: handle=%s\n", handle);

    // Ensure signal thread is running
    if (!ensure_signal_thread_running()) {
        FWUPMGR_ERROR("checkForUpdate: Failed to start signal thread\n");
        return CHECK_FOR_UPDATE_FAIL;
    }

    // Parse handler_id from handle string
    errno = 0;
    guint64 handler_id = strtoull(handle, NULL, 10);
    if (errno != 0 || handler_id == 0) {
        FWUPMGR_ERROR("checkForUpdate: Invalid handle format\n");
        return CHECK_FOR_UPDATE_FAIL;
    }

    // Create callback context
    CallbackContext *ctx = (CallbackContext*)malloc(sizeof(CallbackContext));
    if (!ctx) {
        FWUPMGR_ERROR("checkForUpdate: Failed to allocate CallbackContext\n");
        return CHECK_FOR_UPDATE_FAIL;
    }

    // Initialize context
    memset(ctx, 0, sizeof(CallbackContext));
    ctx->handle = strdup(handle);
    if (!ctx->handle) {
        FWUPMGR_ERROR("checkForUpdate: Failed to duplicate handle string\n");
        free(ctx);
        return CHECK_FOR_UPDATE_FAIL;
    }
    ctx->handler_id = handler_id;
    ctx->callback = callback;
    ctx->state = CALLBACK_STATE_WAITING;
    ctx->request_timestamp = (guint64)time(NULL);

    // Add to registry (thread-safe)
    pthread_mutex_lock(&g_registry_mutex);
    add_callback_to_registry(handler_id, ctx);
    pthread_mutex_unlock(&g_registry_mutex);

    FWUPMGR_INFO("checkForUpdate: Callback registered (handler_id=%lu, state=WAITING)\n", handler_id);

    // Create D-Bus proxy
    GError *error = NULL;
    GDBusProxy *proxy = create_dbus_proxy(&error);
    if (!proxy) {
        FWUPMGR_ERROR("checkForUpdate: Failed to create D-Bus proxy\n");
        if (error) {
            g_error_free(error);
        }
        
        // Remove from registry on failure
        pthread_mutex_lock(&g_registry_mutex);
        remove_callback_from_registry(handler_id);
        pthread_mutex_unlock(&g_registry_mutex);
        
        return CHECK_FOR_UPDATE_FAIL;
    }

    // Call CheckForUpdate D-Bus method (returns immediately with status)
    FWUPMGR_INFO("checkForUpdate: Calling D-Bus method\n");
    
    GVariant *result = g_dbus_proxy_call_sync(
        proxy,
        "CheckForUpdate",
        g_variant_new("(s)", handle),
        G_DBUS_CALL_FLAGS_NONE,
        DBUS_TIMEOUT_MS,
        NULL,
        &error
    );

    if (error) {
        FWUPMGR_ERROR("checkForUpdate: D-Bus call failed: %s\n", error->message);
        g_error_free(error);
        g_object_unref(proxy);
        
        // Remove from registry on failure
        pthread_mutex_lock(&g_registry_mutex);
        remove_callback_from_registry(handler_id);
        pthread_mutex_unlock(&g_registry_mutex);
        
        return CHECK_FOR_UPDATE_FAIL;
    }

    if (!result) {
        FWUPMGR_ERROR("checkForUpdate: D-Bus call returned NULL\n");
        g_object_unref(proxy);
        
        // Remove from registry on failure
        pthread_mutex_lock(&g_registry_mutex);
        remove_callback_from_registry(handler_id);
        pthread_mutex_unlock(&g_registry_mutex);
        
        return CHECK_FOR_UPDATE_FAIL;
    }

    // Parse immediate result (daemon's acceptance of request)
    gint32 immediate_result = 0;
    g_variant_get(result, "(i)", &immediate_result);
    
    FWUPMGR_INFO("checkForUpdate: D-Bus method returned result=%d\n", immediate_result);
    
    g_variant_unref(result);
    g_object_unref(proxy);

    if (immediate_result != 0) {
        FWUPMGR_ERROR("checkForUpdate: Daemon rejected request\n");
        
        // Remove from registry on daemon rejection
        pthread_mutex_lock(&g_registry_mutex);
        remove_callback_from_registry(handler_id);
        pthread_mutex_unlock(&g_registry_mutex);
        
        return CHECK_FOR_UPDATE_FAIL;
    }

    FWUPMGR_INFO("checkForUpdate: Request accepted, waiting for signal\n");
    return CHECK_FOR_UPDATE_SUCCESS;
}
```

**Key Changes from Current Implementation**:
- ✅ No longer synchronous (doesn't invoke callback immediately)
- ✅ Creates and stores CallbackContext in registry
- ✅ Ensures signal thread is running
- ✅ Returns immediately after D-Bus method call
- ✅ Callback invoked later by signal handler

**Error Handling**:
- ✅ Cleanup on all failure paths (remove from registry)
- ✅ No resource leaks
- ✅ Clear error logging

---

#### Sub-Phase 3.2: Update unregisterProcess() for Cleanup
**Objective**: Ensure unregisterProcess() removes callbacks from registry

**Implementation Changes**:
```c
void unregisterProcess(FirmwareInterfaceHandle handler)
{
    // ...existing code...
    
    FWUPMGR_INFO("unregisterProcess() called\n");
    FWUPMGR_INFO("  handle: '%s'\n", handler);

    // Parse handler_id from string handle
    errno = 0;
    handler_id = strtoull(handler, NULL, 10);
    if (errno != 0 || handler_id == 0) {
        FWUPMGR_ERROR("Invalid handle format (not a valid handler_id)\n");
        free(handler);
        return;
    }

    FWUPMGR_INFO("  handler_id: %"G_GUINT64_FORMAT"\n", handler_id);

    // **NEW: Remove callback from registry**
    pthread_mutex_lock(&g_registry_mutex);
    remove_callback_from_registry(handler_id);
    pthread_mutex_unlock(&g_registry_mutex);
    FWUPMGR_INFO("Callback removed from registry (if existed)\n");

    // ...existing D-Bus unregister code...
    
    free(handler);
    FWUPMGR_INFO("Handle memory freed\n");
}
```

**Quality Requirements**:
- ✅ Remove callback before D-Bus unregister (prevent signal processing after unregister)
- ✅ Thread-safe removal
- ✅ Idempotent (safe if callback already removed)

---

### PHASE 4: Helper Functions
**Status**: NOT STARTED

#### Sub-Phase 4.1: Update parse_update_details()
**Objective**: Ensure parse_update_details() is production-ready

**Current Implementation Review**:
```c
static void parse_update_details(const char *details_str, UpdateDetails *details)
```

**Quality Improvements Needed**:
- ✅ Add NULL checks
- ✅ Add bounds checking
- ✅ Handle malformed input gracefully
- ✅ Prevent buffer overflows

**Updated Implementation**:
```c
/**
 * @brief Parse UpdateDetails from comma-separated string
 * 
 * Expected format: "key1:value1,key2:value2,..."
 * Example: "FwFileName:fw.bin,FwUrl:http://...,FwVersion:1.0"
 * 
 * PARSING RULES:
 * - Case-sensitive keys
 * - Values copied with bounds checking
 * - Unknown keys ignored (forward compatibility)
 * - Malformed entries ignored (no error)
 * 
 * MEMORY SAFETY:
 * - All string copies use strncpy with explicit null termination
 * - Input string not modified (strdup'd before strtok)
 * - Output structure zeroed before writing
 * 
 * @param details_str Input string (can be NULL or empty)
 * @param details Output structure (must be non-NULL)
 * 
 * @pre details is non-NULL
 * @post details is filled with parsed values (or zeros if parse fails)
 */
static void parse_update_details(const char *details_str, UpdateDetails *details)
{
    // Defensive: NULL check on output structure
    if (!details) {
        FWUPMGR_ERROR("parse_update_details: details is NULL\n");
        return;
    }

    // Initialize to zeros
    memset(details, 0, sizeof(UpdateDetails));

    // NULL or empty input: nothing to parse
    if (!details_str || strlen(details_str) == 0) {
        return;
    }

    // strdup for strtok (doesn't modify original)
    char *str_copy = strdup(details_str);
    if (!str_copy) {
        FWUPMGR_ERROR("parse_update_details: strdup failed\n");
        return;
    }

    // Parse comma-separated entries
    char *saveptr = NULL;  // For thread-safe strtok_r
    char *token = strtok_r(str_copy, ",", &saveptr);
    
    while (token) {
        // Find colon separator
        char *colon = strchr(token, ':');
        if (colon) {
            *colon = '\0';  // Split into key and value
            const char *key = token;
            const char *value = colon + 1;

            // Copy value to appropriate field with bounds checking
            if (strcmp(key, "FwFileName") == 0) {
                strncpy(details->FwFileName, value, MAX_FW_FILENAME_SIZE - 1);
                details->FwFileName[MAX_FW_FILENAME_SIZE - 1] = '\0';
            } else if (strcmp(key, "FwUrl") == 0) {
                strncpy(details->FwUrl, value, MAX_FW_URL_SIZE - 1);
                details->FwUrl[MAX_FW_URL_SIZE - 1] = '\0';
            } else if (strcmp(key, "FwVersion") == 0) {
                strncpy(details->FwVersion, value, MAX_FW_VERSION_SIZE - 1);
                details->FwVersion[MAX_FW_VERSION_SIZE - 1] = '\0';
            } else if (strcmp(key, "RebootImmediately") == 0) {
                strncpy(details->RebootImmediately, value, MAX_REBOOT_IMMEDIATELY_SIZE - 1);
                details->RebootImmediately[MAX_REBOOT_IMMEDIATELY_SIZE - 1] = '\0';
            } else if (strcmp(key, "DelayDownload") == 0) {
                strncpy(details->DelayDownload, value, MAX_DELAY_DOWNLOAD_SIZE - 1);
                details->DelayDownload[MAX_DELAY_DOWNLOAD_SIZE - 1] = '\0';
            } else if (strcmp(key, "PDRIVersion") == 0) {
                strncpy(details->PDRIVersion, value, MAX_PDRI_VERSION_LEN - 1);
                details->PDRIVersion[MAX_PDRI_VERSION_LEN - 1] = '\0';
            } else if (strcmp(key, "PeripheralFirmwares") == 0) {
                strncpy(details->PeripheralFirmwares, value, MAX_PERIPHERAL_VERSION_LEN - 1);
                details->PeripheralFirmwares[MAX_PERIPHERAL_VERSION_LEN - 1] = '\0';
            }
            // Unknown keys silently ignored (forward compatibility)
        }
        
        token = strtok_r(NULL, ",", &saveptr);
    }

    free(str_copy);
}
```

**Coverity Requirements**:
- ✅ NULL check on details parameter
- ✅ strtok_r instead of strtok (thread-safe)
- ✅ Explicit null termination after strncpy (prevent Coverity warning)
- ✅ No buffer overflow (strncpy with size-1)

---

### PHASE 5: Testing and Validation
**Status**: NOT STARTED

#### Sub-Phase 5.1: Unit Testing Plan

**Test Cases**:

1. **Single Plugin, Single Request**
   - Plugin calls checkForUpdate()
   - Verify callback invoked when signal arrives
   - Verify state transitions (IDLE → WAITING → COMPLETED)

2. **Multiple Plugins, Concurrent Requests**
   - Plugin A calls checkForUpdate()
   - Plugin B calls checkForUpdate()
   - Daemon emits ONE signal
   - Verify BOTH callbacks invoked

3. **Same Plugin, Multiple Requests**
   - Plugin calls checkForUpdate() twice (different handlers)
   - Verify both callbacks invoked
   - Verify state management per handler

4. **Callback Already Completed**
   - Plugin calls checkForUpdate(), signal arrives, callback invoked
   - Another signal arrives (spurious/retry)
   - Verify callback NOT invoked again (state=COMPLETED)

5. **Unregister Before Signal**
   - Plugin calls checkForUpdate()
   - Plugin calls unregisterProcess() before signal
   - Signal arrives
   - Verify callback NOT invoked (removed from registry)

6. **Thread Safety Stress Test**
   - 10 threads calling checkForUpdate() simultaneously
   - Verify no race conditions
   - Verify no crashes
   - Verify all callbacks invoked exactly once

---

#### Sub-Phase 5.2: Memory Leak Testing

**Tools**:
- Valgrind (memcheck)
- AddressSanitizer (ASan)
- LeakSanitizer (LSan)

**Test Scenarios**:
1. Register → CheckForUpdate → Signal → Unregister → Repeat 1000x
2. Register → CheckForUpdate → Unregister (without signal) → Repeat 1000x
3. Multiple concurrent checkForUpdate calls
4. Thread creation/destruction

**Expected Results**:
- ✅ 0 bytes lost (definitely lost)
- ✅ 0 bytes lost (indirectly lost)
- ✅ 0 bytes still reachable (after cleanup)

---

#### Sub-Phase 5.3: Coverity Static Analysis

**Run Coverity Scan**:
```bash
cov-build --dir cov-int make
cov-analyze --dir cov-int
cov-format-errors --dir cov-int
```

**Target Defects**: 0

**Critical Issues to Prevent**:
- ✅ Use-after-free
- ✅ Double-free
- ✅ Memory leak
- ✅ Null pointer dereference
- ✅ Buffer overflow
- ✅ Uninitialized variable
- ✅ Resource leak (file descriptors, mutexes)
- ✅ Deadlock potential

---

### PHASE 6: Documentation Updates
**Status**: NOT STARTED

#### Sub-Phase 6.1: Update API Documentation

**Files to Update**:
1. `librdkFwupdateMgr/include/rdkFwupdateMgr_client.h`
   - Update checkForUpdate() documentation
   - Add async behavior notes
   - Document callback thread context

2. `librdkFwupdateMgr/CHECK_FOR_UPDATE_API.md`
   - Update flow diagram (async)
   - Add signal subscription notes
   - Document multi-plugin behavior

3. `BUILD_CHECKFORUPDATE.md`
   - Update expected behavior
   - Add async testing notes

---

#### Sub-Phase 6.2: Update Example Programs

**File**: `librdkFwupdateMgr/examples/example_checkforupdate.c`

**Changes Needed**:
1. Add sleep/wait after checkForUpdate() (callback is async)
2. Add flag to track callback invocation
3. Add timeout handling
4. Document async behavior in comments

**Example Update**:
```c
static volatile int g_callback_received = 0;

void on_update_event(const FwInfoData *fwinfodata) {
    printf("Callback invoked!\n");
    // ... print firmware info ...
    g_callback_received = 1;
}

int main() {
    checkForUpdate(handle, on_update_event);
    
    // Wait for callback (async)
    printf("Waiting for callback...\n");
    int timeout_sec = 30;
    for (int i = 0; i < timeout_sec && !g_callback_received; i++) {
        sleep(1);
        printf("Still waiting... (%d/%d)\n", i+1, timeout_sec);
    }
    
    if (!g_callback_received) {
        printf("Timeout: Callback not received\n");
    }
    
    unregisterProcess(handle);
}
```

---

## Phase Tracking

### Phase Status Legend
- 🔴 **NOT STARTED**: Phase not begun
- 🟡 **IN PROGRESS**: Phase partially completed
- 🟢 **IMPLEMENTED**: Phase complete, tested, documented
- ⚠️ **BLOCKED**: Phase blocked by dependency

---

### Current Status

| Phase | Sub-Phase | Status | Notes |
|-------|-----------|--------|-------|
| **PHASE 0** | Prerequisites | 🔴 NOT STARTED | |
| 0.1 | Analyze Daemon Signal | 🔴 NOT STARTED | Verify signal format |
| **PHASE 1** | Data Structures | 🔴 NOT STARTED | |
| 1.1 | Define State Enum | 🔴 NOT STARTED | |
| 1.2 | Define CallbackContext | 🔴 NOT STARTED | |
| 1.3 | Create Global Registry | 🔴 NOT STARTED | |
| 1.4 | Implement Registry Helpers | 🔴 NOT STARTED | |
| **PHASE 2** | Background Thread | 🔴 NOT STARTED | |
| 2.1 | Define Thread Globals | 🔴 NOT STARTED | |
| 2.2 | Implement Signal Handler | 🔴 NOT STARTED | Critical path |
| 2.3 | Implement Thread Entry | 🔴 NOT STARTED | |
| 2.4 | Implement Thread Startup | 🔴 NOT STARTED | |
| **PHASE 3** | checkForUpdate() | 🔴 NOT STARTED | |
| 3.1 | Update checkForUpdate() | 🔴 NOT STARTED | Depends on PHASE 1, 2 |
| 3.2 | Update unregisterProcess() | 🔴 NOT STARTED | |
| **PHASE 4** | Helper Functions | 🔴 NOT STARTED | |
| 4.1 | Update parse_update_details() | 🔴 NOT STARTED | |
| **PHASE 5** | Testing | 🔴 NOT STARTED | |
| 5.1 | Unit Testing | 🔴 NOT STARTED | |
| 5.2 | Memory Leak Testing | 🔴 NOT STARTED | |
| 5.3 | Coverity Analysis | 🔴 NOT STARTED | Must be 0 defects |
| **PHASE 6** | Documentation | 🔴 NOT STARTED | |
| 6.1 | Update API Docs | 🔴 NOT STARTED | |
| 6.2 | Update Examples | 🔴 NOT STARTED | |

---

## Quality Assurance

### Code Quality Checklist

**Per Function**:
- [ ] NULL checks on all pointer parameters
- [ ] Bounds checking on all string operations
- [ ] Error handling on all allocation/system calls
- [ ] Resource cleanup on all paths (success and error)
- [ ] Locks acquired and released in same scope
- [ ] No nested locks (deadlock prevention)
- [ ] Thread safety documented in comment header

**Per File**:
- [ ] All static variables initialized
- [ ] All globals documented with lifetime/ownership
- [ ] Consistent error logging (FWUPMGR_ERROR)
- [ ] Consistent info logging (FWUPMGR_INFO)

---

### Coverity Defect Prevention

**Memory Management**:
- ✅ Every malloc has corresponding free
- ✅ Every strdup has corresponding free
- ✅ Every g_object_new/ref has corresponding unref
- ✅ No double-free scenarios
- ✅ No use-after-free scenarios

**String Safety**:
- ✅ Use strncpy, not strcpy
- ✅ Explicit null termination after strncpy
- ✅ Buffer size = MAX_SIZE - 1 (room for null)
- ✅ Use strtok_r, not strtok (thread-safe)

**Threading**:
- ✅ All shared data protected by mutex
- ✅ Mutex locked/unlocked in same function
- ✅ No lock held during callback invocation
- ✅ pthread_create checks return value

**GLib**:
- ✅ g_variant_get parameters match format string
- ✅ g_free for GLib strings, free for malloc'd
- ✅ g_object_unref for GObject types
- ✅ GError freed after use

---

### Thread Safety Validation

**Shared Resources**:
1. `g_callback_registry` (GHashTable)
   - Protected by: `g_registry_mutex`
   - Accessed from: Main thread, signal thread
   - Lock duration: Minimal (no callbacks while holding)

2. `g_signal_thread_created` (bool)
   - Protected by: `g_signal_thread_mutex`
   - Accessed from: Main thread only (checkForUpdate)
   - Lock duration: Minimal (thread creation check)

**Lock Ordering** (Deadlock Prevention):
- Only ONE lock acquired at a time (no nested locks)
- Lock held for minimal duration
- No external calls while holding lock

---

## Testing Strategy

### Test Environment
- **OS**: Linux (Ubuntu 20.04+)
- **Compiler**: GCC 9.4+ with -Wall -Wextra -Werror
- **Tools**: Valgrind, Coverity, GDB
- **Daemon**: rdkFwupdateMgr running locally

### Test Execution Plan

**Phase 1: Compilation**
```bash
make clean
make CFLAGS="-Wall -Wextra -Werror -g -O0"
# Expected: 0 warnings, 0 errors
```

**Phase 2: Unit Tests**
```bash
./example_checkforupdate
# Expected: Callback invoked, proper data displayed
```

**Phase 3: Memory Leak Check**
```bash
valgrind --leak-check=full --show-leak-kinds=all ./example_checkforupdate
# Expected: 0 bytes lost
```

**Phase 4: Stress Test**
```bash
# Run 100 iterations
for i in {1..100}; do
    echo "Iteration $i"
    ./example_checkforupdate || exit 1
done
# Expected: All iterations pass
```

**Phase 5: Concurrent Test**
```bash
# Run 5 plugins simultaneously
./example_checkforupdate &
./example_checkforupdate &
./example_checkforupdate &
./example_checkforupdate &
./example_checkforupdate &
wait
# Expected: All callbacks invoked, no crashes
```

**Phase 6: Coverity Scan**
```bash
cov-build --dir cov-int make
cov-analyze --dir cov-int
cov-format-errors --dir cov-int --html-output cov-html
# Expected: 0 defects
```

---

## Implementation Notes

### Key Design Decisions

1. **Stateless Handle Design (Unchanged)**
   - FirmwareInterfaceHandle remains string-based
   - No breaking changes to existing API

2. **Broadcast Signal Processing**
   - All libraries receive same signal
   - State-based filtering (only WAITING callbacks invoked)
   - No handler_id matching needed

3. **Background Thread with GLib Event Loop**
   - Single background thread per library instance
   - Runs GLib main loop to process signals
   - Created lazily on first checkForUpdate()
   - Detached (no explicit join needed)

4. **Thread-Safe Registry**
   - GHashTable for O(1) lookup
   - Mutex-protected access
   - Minimal lock duration (released before callbacks)

5. **Memory Management Strategy**
   - GLib strings: Use g_free()
   - malloc'd strings: Use free()
   - GObjects: Use g_object_unref()
   - CallbackContext: Freed by hash table destroy function

### Potential Pitfalls (Avoided)

❌ **Pitfall 1**: Invoking callback while holding mutex
- **Impact**: Deadlock if callback calls library function
- **Prevention**: Release mutex before callback invocation

❌ **Pitfall 2**: Double-free of CallbackContext
- **Impact**: Crash
- **Prevention**: Hash table owns context, destroy function handles free

❌ **Pitfall 3**: Use-after-free in signal handler
- **Impact**: Crash
- **Prevention**: Copy callback list, check context validity

❌ **Pitfall 4**: Race condition in thread creation
- **Impact**: Multiple signal threads
- **Prevention**: Mutex-protected creation flag

❌ **Pitfall 5**: Missing null termination after strncpy
- **Impact**: Coverity warning, potential overflow
- **Prevention**: Explicit null termination on every strncpy

---

## Success Criteria

### Must Have (Go/No-Go)
- ✅ 0 Coverity defects
- ✅ 0 memory leaks (Valgrind clean)
- ✅ 0 crashes in stress test (100 iterations)
- ✅ All callbacks invoked in multi-plugin test
- ✅ Proper state transitions (IDLE → WAITING → COMPLETED)
- ✅ Thread-safe operation (no race conditions)

### Nice to Have (Quality)
- ✅ Response time < 100ms (signal to callback)
- ✅ Memory footprint < 1MB (background thread + registry)
- ✅ Comprehensive logging (FWUPMGR_INFO/ERROR)
- ✅ Example programs demonstrate async behavior
- ✅ Documentation updated and accurate

---

## Rollback Plan

If implementation fails or introduces regressions:

1. **Revert to Synchronous Implementation**
   - Keep existing synchronous checkForUpdate()
   - Document as "synchronous mode" in API docs
   - Remove background thread code

2. **Fallback Files**
   - Backup current rdkFwupdateMgr_process.c before changes
   - Tag commit before starting Phase 1

3. **Known Limitations Document**
   - Document why async is hard (no handler_id in signal)
   - Propose daemon changes for future

---

## Appendix: Code Review Checklist

**Before Marking Phase IMPLEMENTED**:

### Memory Safety
- [ ] All malloc() calls checked for NULL
- [ ] All malloc'd memory has explicit free() path
- [ ] All strdup() calls checked for NULL
- [ ] All g_object_new/get_sync calls checked for NULL
- [ ] All GError* variables freed with g_error_free()
- [ ] No double-free scenarios possible
- [ ] No use-after-free scenarios possible

### Thread Safety
- [ ] All shared data access protected by mutex
- [ ] Mutex locked and unlocked in same function
- [ ] No locks held during callback invocation
- [ ] No nested locks (deadlock prevention)
- [ ] pthread_create return value checked
- [ ] Thread attributes destroyed after pthread_create

### String Safety
- [ ] No strcpy (use strncpy)
- [ ] All strncpy calls have explicit null termination
- [ ] Buffer sizes account for null terminator (size-1)
- [ ] No strtok (use strtok_r for thread safety)

### Error Handling
- [ ] All error paths log to FWUPMGR_ERROR
- [ ] All error paths clean up resources
- [ ] No silent failures
- [ ] All return values checked (D-Bus, malloc, pthread)

### Documentation
- [ ] Function header comments complete
- [ ] Thread safety documented
- [ ] Memory ownership documented
- [ ] Preconditions and postconditions specified

### Testing
- [ ] Compiles without warnings (-Wall -Wextra -Werror)
- [ ] Valgrind clean (0 leaks)
- [ ] Example programs work
- [ ] Stress test passes (100 iterations)
- [ ] Coverity scan clean (0 defects)

---

**END OF IMPLEMENTATION PLAN**

**To resume implementation**: Read this document, check Phase Tracking section for current status, and continue from next NOT STARTED phase.

**Document will be updated** after each phase completion to mark status as IMPLEMENTED.
