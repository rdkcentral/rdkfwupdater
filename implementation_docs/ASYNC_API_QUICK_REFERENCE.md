# Async CheckForUpdate API - Quick Reference

**Status:** ✅ Core Implementation Complete (Phases 0-5)  
**Date:** February 25, 2026

---

## What Was Implemented

### Core Features ✅
- **Non-blocking async API** for firmware update checks
- **Multi-callback support** (up to 64 concurrent operations)
- **Thread-safe** implementation with mutex and atomic operations
- **Background thread** with GLib event loop for D-Bus signal processing
- **Automatic timeout** detection (60 seconds)
- **Cancellation** support
- **Auto-init/deinit** using constructor/destructor attributes

### Files Created (7 new files)
1. `librdkFwupdateMgr/src/rdkFwupdateMgr_async_internal.h` - Internal data structures
2. `librdkFwupdateMgr/src/rdkFwupdateMgr_async.c` - Core implementation (~600 lines)
3. `librdkFwupdateMgr/src/rdkFwupdateMgr_async_api.c` - Public API wrappers
4. `librdkFwupdateMgr/examples/example_async_checkforupdate.c` - Example program
5. `PHASE_0_ANALYSIS_RESULTS.md` - Daemon analysis documentation
6. `IMPLEMENTATION_SUMMARY.md` - Comprehensive summary
7. `QUICK_REFERENCE.md` - This file

### Files Modified (3 files)
1. `librdkFwupdateMgr/include/rdkFwupdateMgr_client.h` - Added public async API
2. `Makefile.am` - Added new source files to build
3. `IMPLEMENTATION_PLAN_CHECKFORUPDATE_ASYNC.md` - Updated phase tracking

---

## Public API

### Function 1: Start Async Check
```c
AsyncCallbackId checkForUpdate_async(AsyncUpdateCallback callback, void *user_data);
```
- **Returns:** Callback ID (>0) on success, 0 on error
- **Non-blocking:** Returns immediately
- **Thread-safe:** Can be called from any thread

### Function 2: Cancel Pending Check
```c
int checkForUpdate_async_cancel(AsyncCallbackId callback_id);
```
- **Returns:** 0 on success, -1 on error
- **Thread-safe:** Can be called from any thread

### Callback Type
```c
typedef void (*AsyncUpdateCallback)(const AsyncUpdateInfo *info, void *user_data);
```
- **Thread:** Runs in background thread (NOT caller's thread!)
- **Lifetime:** `info` pointer only valid during callback
- **Action:** Copy any data you need with `strdup()`

### Data Structure
```c
typedef struct {
    int32_t result;                  // 0 = success, non-zero = error
    int32_t status_code;             // See CheckForUpdateStatus enum
    const char *current_version;     // Current FW version
    const char *available_version;   // New FW version (or NULL)
    const char *update_details;      // Raw details string
    const char *status_message;      // Human-readable message
    bool update_available;           // true if update available
} AsyncUpdateInfo;
```

---

## Minimal Example

```c
#include "rdkFwupdateMgr_client.h"
#include <stdio.h>
#include <unistd.h>

void my_callback(const AsyncUpdateInfo *info, void *user_data) {
    printf("Update available: %s\n", info->update_available ? "YES" : "NO");
}

int main(void) {
    AsyncCallbackId id = checkForUpdate_async(my_callback, NULL);
    if (id == 0) {
        fprintf(stderr, "Failed\n");
        return 1;
    }
    
    printf("Checking... (ID: %u)\n", id);
    sleep(10);  // Wait for callback
    return 0;
}
```

**Compile:**
```bash
gcc -o test test.c -lrdkFwupdateMgr `pkg-config --cflags --libs glib-2.0` -lpthread
```

---

## Important Notes

### ⚠️ Callback Thread Context
- **Your callback runs in a BACKGROUND THREAD**
- Use proper synchronization if accessing shared data
- Keep callback short (no blocking operations)

### ⚠️ Memory Lifetime
- All strings in `AsyncUpdateInfo` are **only valid during callback**
- Do NOT store pointers
- Copy data with `strdup()` if needed after callback

### ⚠️ Library Initialization
- Library auto-initializes on load (constructor attribute)
- Library auto-cleans up on unload (destructor attribute)
- No manual init/deinit needed

### ⚠️ Concurrent Calls
- Up to 64 concurrent `checkForUpdate_async()` calls supported
- All waiting callbacks receive same signal from daemon
- Each callback tracked independently with unique ID

### ⚠️ Timeout
- 60-second timeout per operation
- Callback invoked with error if timeout occurs
- Timeout checker runs every 5 seconds in background

---

## Status Codes

```c
typedef enum {
    FIRMWARE_AVAILABLE = 0,        // New firmware available
    FIRMWARE_NOT_AVAILABLE = 1,    // Already on latest
    UPDATE_NOT_ALLOWED = 2,        // Not compatible
    FIRMWARE_CHECK_ERROR = 3,      // Error checking
    IGNORE_OPTOUT = 4,             // User opted out
    BYPASS_OPTOUT = 5              // Requires consent
} CheckForUpdateStatus;
```

Check `info->status_code` in your callback.

---

## Architecture

```
Application Thread              Background Thread (GLib Loop)
─────────────────              ────────────────────────────────
checkForUpdate_async()
  ↓
Register callback
  ↓
Send D-Bus call (async)
  ↓
Return immediately                     ↓
  ↓                            Wait for D-Bus signal...
Continue running...                    ↓
  ↓                            Signal received!
  ↓                                    ↓
  ↓                            Parse signal data
  ↓                                    ↓
  ↓                            Find all WAITING callbacks
  ↓                                    ↓
  ← ← ← ← ← ← ← ← ← ← ← ← ←  Invoke your callback
  ↓
Callback executes
  ↓
Return from callback
```

---

## What's NOT Done Yet (Future Work)

### Phase 6: Memory Management
- Stress testing (valgrind)
- Verify no leaks in all error paths

### Phase 7: Error Handling
- Detailed error codes
- Improved logging
- D-Bus connection failure handling

### Phase 8: Testing
- Unit tests (Google Test)
- Stress tests (100+ concurrent)
- Integration tests with real daemon
- Coverity static analysis

### Phase 9: Documentation
- Comprehensive API documentation
- Integration guide
- Migration guide from sync to async
- Update README

---

## Key Files to Review

1. **For API usage:**
   - `librdkFwupdateMgr/include/rdkFwupdateMgr_client.h` - Public API
   - `librdkFwupdateMgr/examples/example_async_checkforupdate.c` - Examples

2. **For implementation details:**
   - `librdkFwupdateMgr/src/rdkFwupdateMgr_async_internal.h` - Data structures
   - `librdkFwupdateMgr/src/rdkFwupdateMgr_async.c` - Core logic

3. **For planning/documentation:**
   - `IMPLEMENTATION_PLAN_CHECKFORUPDATE_ASYNC.md` - Detailed plan
   - `IMPLEMENTATION_SUMMARY.md` - What's been done
   - `PHASE_0_ANALYSIS_RESULTS.md` - Daemon analysis

---

## Build Instructions

### Full build:
```bash
./configure
make
sudo make install
```

### Example only:
```bash
cd librdkFwupdateMgr/examples
gcc -o example example_async_checkforupdate.c \
    -I../include \
    -L/usr/local/lib \
    -lrdkFwupdateMgr \
    `pkg-config --cflags --libs glib-2.0` \
    -lpthread

LD_LIBRARY_PATH=/usr/local/lib ./example
```

---

## Troubleshooting

### "Failed to start async check"
- Library not initialized (should auto-init, check logs)
- Registry full (64 concurrent limit)
- Invalid callback (NULL pointer)

### "Callback never invoked"
- Daemon not running
- D-Bus connection failed
- Timeout occurred (check 60-second limit)
- Callback was cancelled

### "Segmentation fault"
- Accessing `info` fields outside callback
- Storing pointers from `info` and using later
- Not using proper synchronization in callback

---

## Next Steps

### To continue implementation:
1. Read `IMPLEMENTATION_SUMMARY.md`
2. Check `IMPLEMENTATION_PLAN_CHECKFORUPDATE_ASYNC.md` for remaining phases
3. Continue with Phase 6 (Memory validation)

### To use the API:
1. Include `rdkFwupdateMgr_client.h`
2. Link with `-lrdkFwupdateMgr -lglib-2.0 -lpthread`
3. Call `checkForUpdate_async()` with your callback
4. Handle results in callback (copy data if needed)

---

**END OF QUICK REFERENCE**
