# Async API Memory Management Guide

**Document Version:** 1.0  
**Last Updated:** 2026-02-25  
**Audience:** Developers, Code Reviewers, QA Engineers

---

## Table of Contents

1. [Overview](#overview)
2. [Memory Ownership Model](#memory-ownership-model)
3. [Data Structures](#data-structures)
4. [Lifecycle Management](#lifecycle-management)
5. [Reference Counting](#reference-counting)
6. [String Memory Management](#string-memory-management)
7. [Thread Safety](#thread-safety)
8. [Common Pitfalls](#common-pitfalls)
9. [Best Practices](#best-practices)
10. [Testing and Validation](#testing-and-validation)

---

## Overview

The async CheckForUpdate API uses a carefully designed memory management strategy to ensure:

- **No memory leaks**: All allocated memory is properly freed
- **Thread safety**: Safe concurrent access from multiple threads
- **No use-after-free**: Reference counting prevents premature deallocation
- **Predictable behavior**: Fixed-size allocations, no unbounded growth

### Design Principles

1. **Fixed-Size Registry**: Use a fixed-size array (`MAX_ASYNC_CALLBACKS = 64`) instead of dynamic allocation
2. **Reference Counting**: Protect shared contexts from premature deallocation
3. **RAII Pattern**: Cleanup happens automatically when reference count reaches zero
4. **Mutex Protection**: All registry access protected by a single global mutex
5. **String Duplication**: All strings passed to callbacks are duplicated and freed after use

---

## Memory Ownership Model

### Library-Owned Memory

The following memory is owned and managed by the library:

1. **Global Registry**: `AsyncCallbackRegistry g_async_registry`
   - Static global, never freed (lives for entire process)
   - Contains fixed-size array of callback contexts

2. **Background Thread State**: `AsyncBackgroundThread g_bg_thread`
   - Static global, cleaned up on library deinitialization
   - GLib main loop and context freed on shutdown

3. **Callback Context Data**: `AsyncCallbackContext` in registry
   - Reused slots, cleaned when state transitions to COMPLETED/CANCELLED
   - Reference counting protects against concurrent access

4. **Signal Data Strings**: Strings in `RdkFwupdateMgr_UpdateInfo`
   - Allocated with `strdup()` when parsing signal
   - Freed automatically after all callbacks complete

### User-Owned Memory

The following memory is owned by the calling code:

1. **Callback Function Pointer**: `checkForUpdate_async(callback, user_data)`
   - User provides function pointer (not copied or freed by library)
   - Must remain valid until callback is invoked or cancelled

2. **User Data**: `void *user_data`
   - User provides pointer (not copied or freed by library)
   - Lifetime managed by user code
   - Passed back to callback unchanged

---

## Data Structures

### AsyncCallbackContext

```c
typedef struct {
    uint32_t id;                     // Unique ID (never 0)
    AsyncCallbackState state;        // State machine
    checkForUpdate_async_callback_t callback;  // User's callback
    void *user_data;                 // User's data
    time_t registered_time;          // For timeout detection
    atomic_int ref_count;            // Reference count
    RdkFwupdateMgr_UpdateInfo update_info;  // Parsed signal data
} AsyncCallbackContext;
```

**Memory Management:**
- Context itself is part of fixed array, never malloc/free'd
- `update_info` strings (`message`, `version`, `download_url`) are strdup'd and freed
- Reference count protects against premature cleanup

### AsyncCallbackRegistry

```c
typedef struct {
    AsyncCallbackContext contexts[MAX_ASYNC_CALLBACKS];
    pthread_mutex_t mutex;           // Protects entire registry
    uint32_t next_id;                // Next ID to assign
    bool initialized;                // Initialization flag
} AsyncCallbackRegistry;
```

**Memory Management:**
- Static global, never freed
- Mutex initialized once with `PTHREAD_MUTEX_INITIALIZER`
- Array is fixed-size, slots reused after completion

---

## Lifecycle Management

### Context States

```
IDLE → WAITING → COMPLETED → IDLE (slot reused)
              ↘ CANCELLED → IDLE (slot reused)
              ↘ TIMEOUT → IDLE (slot reused)
              ↘ ERROR → IDLE (slot reused)
```

### State Transitions

1. **Registration** (`checkForUpdate_async`)
   ```
   IDLE → WAITING
   - Allocate new ID
   - Store callback and user_data
   - Set registered_time
   - Set ref_count = 1
   ```

2. **Signal Arrival** (background thread)
   ```
   WAITING → COMPLETED
   - Parse signal data (strdup strings)
   - Set ref_count based on WAITING callbacks count
   - Invoke all callbacks
   - Clean up strings after callbacks return
   ```

3. **Cancellation** (`checkForUpdate_async_cancel`)
   ```
   WAITING → CANCELLED
   - Update state
   - Callback will NOT be invoked
   - Slot can be reused immediately
   ```

4. **Timeout** (background thread)
   ```
   WAITING → TIMEOUT
   - Invoke callback with timeout error
   - Clean up context
   ```

5. **Cleanup** (ref_count reaches 0)
   ```
   COMPLETED/CANCELLED/TIMEOUT → IDLE
   - Free allocated strings
   - Reset context fields
   - Slot available for reuse
   ```

---

## Reference Counting

### Purpose

Reference counting prevents the following race condition:

```
Thread 1 (signal handler):        Thread 2 (user code):
- Lock registry                   
- Find WAITING callback           
- Unlock registry                 
- About to invoke callback        - Call cancel()
                                  - Context marked CANCELLED
- Invoke callback ← CRASH!        - Context reused for new callback
  (Context may have been reused)
```

### Implementation

```c
static void context_ref(AsyncCallbackContext *ctx) {
    if (ctx) {
        atomic_fetch_add(&ctx->ref_count, 1);
    }
}

static void context_unref(AsyncCallbackContext *ctx) {
    if (ctx && atomic_fetch_sub(&ctx->ref_count, 1) == 1) {
        // Last reference, clean up
        cleanup_context(ctx);
    }
}
```

### Usage Pattern

```c
// In signal handler:
registry_lock();
for (each WAITING callback) {
    context_ref(ctx);  // Increment before unlocking
}
registry_unlock();

// Now safe to use contexts outside lock
for (each ref'd callback) {
    invoke_callback(ctx);
    context_unref(ctx);  // Cleanup when done
}
```

### Invariants

1. **Initial State**: ref_count = 0 (IDLE contexts)
2. **After Registration**: ref_count = 1 (set by signal handler when found)
3. **During Callback**: ref_count ≥ 1 (protected)
4. **After Callback**: ref_count decremented
5. **Cleanup**: Triggered when ref_count → 0

---

## String Memory Management

### Signal Data Strings

All strings in `RdkFwupdateMgr_UpdateInfo` are dynamically allocated:

```c
typedef struct {
    int status;
    char *message;        // strdup'd
    bool update_available;
    char *version;        // strdup'd or NULL
    char *download_url;   // strdup'd or NULL
} RdkFwupdateMgr_UpdateInfo;
```

### Allocation

Strings are allocated in `parse_signal_data()`:

```c
const char *msg = NULL;
g_variant_get(parameters, "(tiissss)", 
              &timestamp, &status, &http_status, 
              &msg, &version, &download_url, &reboot_immediately);

if (msg) {
    info->message = strdup(msg);  // Duplicate
}
// ... similar for other strings
```

### Deallocation

Strings are freed in `cleanup_update_info()`:

```c
static void cleanup_update_info(RdkFwupdateMgr_UpdateInfo *info) {
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

**Called from:**
1. `context_unref()` when ref_count reaches 0
2. Error paths in signal parsing

### User Callback Contract

**IMPORTANT**: Strings in `RdkFwupdateMgr_UpdateInfo` are only valid during the callback:

```c
void my_callback(RdkFwupdateMgr_UpdateInfo *info, void *user_data) {
    // OK: Use strings during callback
    printf("Version: %s\n", info->version);
    
    // WRONG: Store pointer for later use
    // char *saved = info->version;  // WILL BE FREED AFTER CALLBACK RETURNS!
    
    // CORRECT: Duplicate if needed
    char *saved = info->version ? strdup(info->version) : NULL;
    // ... remember to free(saved) later
}
```

---

## Thread Safety

### Locking Strategy

**Single Global Mutex**: All registry access protected by `g_async_registry.mutex`

```c
static void registry_lock(void) {
    pthread_mutex_lock(&g_async_registry.mutex);
}

static void registry_unlock(void) {
    pthread_mutex_unlock(&g_async_registry.mutex);
}
```

### Critical Sections

Protected operations:
- Adding callback to registry
- Removing/cancelling callback
- Finding callbacks by state
- Updating context state
- Accessing shared context data

### Lock Ordering

**Rule**: Always acquire registry mutex before accessing contexts.

No nested locks or lock inversions (only one mutex in the system).

### Atomic Operations

Reference counting uses atomic operations to avoid locking:

```c
atomic_int ref_count;  // In AsyncCallbackContext

atomic_fetch_add(&ctx->ref_count, 1);  // Increment
atomic_fetch_sub(&ctx->ref_count, 1);  // Decrement
```

### Thread Contexts

1. **Main Thread**: Calls `checkForUpdate_async()` and `checkForUpdate_async_cancel()`
2. **Background Thread**: Processes signals, invokes callbacks
3. **User Threads**: Any thread can call async API (thread-safe)

---

## Common Pitfalls

### 1. Use-After-Free

**Problem**: Accessing callback context after it's been freed

**Example**:
```c
// WRONG:
registry_lock();
AsyncCallbackContext *ctx = find_callback(id);
registry_unlock();
// Context may be reused here!
invoke_callback(ctx);  // CRASH!
```

**Solution**: Use reference counting
```c
// CORRECT:
registry_lock();
AsyncCallbackContext *ctx = find_callback(id);
if (ctx) {
    context_ref(ctx);
}
registry_unlock();

if (ctx) {
    invoke_callback(ctx);
    context_unref(ctx);
}
```

### 2. Memory Leaks in Error Paths

**Problem**: Forgetting to free strings on error

**Example**:
```c
// WRONG:
char *msg = strdup(string);
if (some_error) {
    return -1;  // LEAK: msg not freed!
}
```

**Solution**: Always clean up before returning
```c
// CORRECT:
char *msg = strdup(string);
int ret = 0;
if (some_error) {
    ret = -1;
    goto cleanup;
}

// ... use msg ...

cleanup:
    if (msg) free(msg);
    return ret;
```

### 3. Double-Free

**Problem**: Freeing the same memory twice

**Example**:
```c
// WRONG:
free(info->message);
// ... later ...
free(info->message);  // CRASH!
```

**Solution**: Set pointers to NULL after freeing
```c
// CORRECT:
if (info->message) {
    free(info->message);
    info->message = NULL;
}
// ... later ...
if (info->message) {  // Safe: NULL check prevents double-free
    free(info->message);
    info->message = NULL;
}
```

### 4. Storing Callback Data Pointers

**Problem**: User stores pointers from `RdkFwupdateMgr_UpdateInfo` for later use

**Example**:
```c
// WRONG:
char *saved_version = NULL;
void my_callback(RdkFwupdateMgr_UpdateInfo *info, void *user_data) {
    saved_version = info->version;  // WRONG: Will be freed!
}
// ... later ...
printf("%s\n", saved_version);  // CRASH: freed memory
```

**Solution**: Duplicate strings if needed beyond callback scope
```c
// CORRECT:
char *saved_version = NULL;
void my_callback(RdkFwupdateMgr_UpdateInfo *info, void *user_data) {
    saved_version = info->version ? strdup(info->version) : NULL;
}
// ... later ...
if (saved_version) {
    printf("%s\n", saved_version);
    free(saved_version);  // Remember to free!
}
```

---

## Best Practices

### For Library Developers

1. **Always use reference counting** when accessing contexts outside the lock
2. **Always clean up on all code paths** (especially error paths)
3. **Set pointers to NULL after freeing** to prevent double-free
4. **Minimize critical section size** (hold locks only as long as necessary)
5. **Document memory ownership** in all public APIs
6. **Test with Valgrind and ASan** regularly

### For API Users

1. **Never store pointers from callback data** beyond callback scope
2. **Duplicate strings if needed** for later use
3. **Don't free callback data** (library manages it)
4. **Handle all error codes** returned by API
5. **Cancel pending callbacks** before cleanup/shutdown
6. **Keep callbacks short** to avoid blocking signal thread

### Code Review Checklist

- [ ] All `malloc`/`strdup` have corresponding `free`
- [ ] All error paths clean up allocated memory
- [ ] All freed pointers set to NULL
- [ ] No pointer access after free
- [ ] Reference counting used correctly
- [ ] All registry access protected by mutex
- [ ] No unbounded memory allocations
- [ ] Documentation updated

---

## Testing and Validation

### Unit Tests

See `unittest/rdkFwupdateMgr_async_refcount_gtest.cpp`:
- Reference counting correctness
- Thread-safe ref/unref operations
- No use-after-free
- No double-free
- Cleanup at zero refcount

### Stress Tests

See `unittest/rdkFwupdateMgr_async_stress_gtest.cpp`:
- Concurrent registrations (1000+)
- Registry exhaustion and recovery
- Rapid register/cancel cycles
- Memory stability under load

### Memory Validation Tools

1. **Valgrind Memcheck**: Detect memory leaks
   ```bash
   valgrind --leak-check=full --show-leak-kinds=all ./test_program
   ```

2. **AddressSanitizer**: Detect memory errors
   ```bash
   gcc -fsanitize=address -g test.c -o test
   ./test
   ```

3. **Valgrind Helgrind**: Detect race conditions
   ```bash
   valgrind --tool=helgrind ./test_program
   ```

4. **Massif**: Profile heap usage
   ```bash
   valgrind --tool=massif ./test_program
   ms_print massif.out.* > profile.txt
   ```

### Automation

See `test/memory_validation.sh`:
- Automated Valgrind testing
- ASan build and testing
- Memory profiling
- Report generation

Run with:
```bash
./test/memory_validation.sh --full
```

### Expected Results

- **Valgrind**: 0 bytes leaked, 0 errors
- **ASan**: No memory errors detected
- **Helgrind**: No race conditions (except known false positives)
- **Massif**: No unbounded memory growth

---

## Troubleshooting

### Valgrind Reports Leaks

1. Check if leak is in user code or library code
2. Review error path in failing test
3. Check if cleanup functions are called
4. Verify ref_count reaches 0

### ASan Reports Use-After-Free

1. Check if reference counting is used
2. Verify contexts are ref'd before using outside lock
3. Check for race conditions in state transitions
4. Review lock/unlock order

### Helgrind Reports Race Conditions

1. Check if reported race is in critical section
2. Verify mutex is held during all shared data access
3. Check atomic operations are used for ref_count
4. Review suppression file for known false positives

---

## References

- Implementation Plan: `IMPLEMENTATION_PLAN_CHECKFORUPDATE_ASYNC.md`
- Phase 6 Validation: `PHASE_6_MEMORY_VALIDATION_PLAN.md`
- API Documentation: `librdkFwupdateMgr/include/rdkFwupdateMgr_client.h`
- Test Code: `unittest/rdkFwupdateMgr_async_*_gtest.cpp`

---

**Document Version:** 1.0  
**Last Updated:** 2026-02-25  
**Maintained By:** RDK Firmware Update Team
