# Process Registration API - Implementation Documentation

## Overview

This document describes the implementation of the `registerProcess()` and `unregisterProcess()` APIs for the librdkFwupdateMgr client library.

## Files Created

### 1. Public Header
**Location:** `librdkFwupdateMgr/include/rdkFwupdateMgr_process.h`

Defines:
- `FirmwareInterfaceHandle` - Opaque handle type (char*)
- `FIRMWARE_INVALID_HANDLE` - NULL constant for error checking
- `registerProcess()` - Process registration API
- `unregisterProcess()` - Process cleanup API

### 2. Implementation
**Location:** `librdkFwupdateMgr/src/rdkFwupdateMgr_process.c`

Implements:
- D-Bus client communication with rdkFwupdateMgr daemon
- Synchronous D-Bus method calls (RegisterProcess, UnregisterProcess)
- Input validation and error handling
- Memory management for handles

### 3. Example Code
**Location:** `librdkFwupdateMgr/examples/example_plugin_registration.c`

Demonstrates:
- Basic registration pattern
- Error handling
- Multiple registrations
- Cleanup patterns (atexit handler)
- Idempotent unregister

## API Design

### Handle Type: FirmwareInterfaceHandle

```c
typedef char* FirmwareInterfaceHandle;
```

**Design Rationale:**
- **Opaque:** Plugins cannot inspect internal structure
- **ABI Stable:** char* interface won't change across versions
- **Simple:** Just a string encoding of handler_id (e.g., "12345")
- **Lightweight:** No need to carry D-Bus connection/proxy
- **Stateless:** Each API call creates fresh D-Bus proxy

**Alternative Considered (Rejected):**
```c
// Rejected: Requires struct definition in public header
typedef struct _FirmwareInterfaceContext* FirmwareInterfaceHandle;
```
This approach would require exposing struct internals and creates ABI fragility.

### registerProcess()

```c
FirmwareInterfaceHandle registerProcess(const char *processName, 
                                        const char *libVersion);
```

**Parameters:**
- `processName` - Unique identifier for the process (required, non-empty)
- `libVersion` - Version string for logging (required, can be empty "")

**Returns:**
- Valid handle string on success (e.g., "12345")
- `FIRMWARE_INVALID_HANDLE` (NULL) on failure

**Behavior:**
1. Validates inputs (NULL checks, length checks)
2. Creates D-Bus proxy to daemon
3. Calls `RegisterProcess` D-Bus method
4. Receives handler_id (uint64) from daemon
5. Converts handler_id to string
6. Returns string as handle

**Error Conditions:**
- NULL/empty processName → Returns NULL
- NULL libVersion → Returns NULL
- D-Bus connection failure → Returns NULL
- Daemon rejects (duplicate name) → Returns NULL
- Out of memory → Returns NULL

**Thread Safety:**
- NOT thread-safe for same processName
- Thread-safe for different processNames
- No internal locking (GDBus handles thread dispatch)

### unregisterProcess()

```c
void unregisterProcess(FirmwareInterfaceHandle handler);
```

**Parameters:**
- `handler` - Handle from registerProcess() (can be NULL)

**Returns:**
- void (no error reporting)

**Behavior:**
1. NULL check (no-op if NULL)
2. Parse handler_id from string
3. Create D-Bus proxy to daemon
4. Call `UnregisterProcess` D-Bus method
5. Free handle memory (always, even if D-Bus fails)

**Error Handling:**
- Best-effort cleanup
- Errors logged to stderr but not propagated
- Memory always freed
- Safe to call multiple times

**Thread Safety:**
- NOT thread-safe for same handle
- Safe to call from different thread than registerProcess()

## D-Bus Protocol

### Service Details
```
Service Name:   org.rdkfwupdater.Interface
Object Path:    /org/rdkfwupdater/Service
Interface:      org.rdkfwupdater.Interface
```

### RegisterProcess Method
```xml
<method name='RegisterProcess'>
    <arg type='s' name='processName' direction='in'/>
    <arg type='s' name='libVersion' direction='in'/>
    <arg type='t' name='handler_id' direction='out'/>
</method>
```

**D-Bus Signature:** `(ss) -> (t)`

**Flow:**
1. Client calls with (processName, libVersion)
2. Daemon validates uniqueness
3. Daemon creates ProcessInfo entry
4. Daemon assigns unique handler_id
5. Daemon returns handler_id (uint64)

**Daemon-Side Validation:**
- Process name must be unique system-wide
- D-Bus sender_id must not be already registered
- Both validated in `add_process_to_tracking()`

### UnregisterProcess Method
```xml
<method name='UnregisterProcess'>
    <arg type='t' name='handler_id' direction='in'/>
    <arg type='b' name='success' direction='out'/>
</method>
```

**D-Bus Signature:** `(t) -> (b)`

**Flow:**
1. Client calls with handler_id
2. Daemon validates ownership (sender_id matches)
3. Daemon removes ProcessInfo entry
4. Daemon returns success boolean

**Daemon-Side Security:**
- Validates sender_id owns handler_id
- Prevents unauthorized unregistration
- Implemented in `remove_process_from_tracking()`

## Memory Management

### Handle Lifetime

```
registerProcess() ──> malloc(32) ──> handle string ──> unregisterProcess() ──> free()
         ↑                                    ↓
         │                            Plugin uses handle
         │                            for firmware APIs
         └────────────────────────────────────┘
```

**Rules:**
1. **Library owns handle** - Plugin must NOT call free()
2. **Valid until unregister** - Handle remains valid after registerProcess()
3. **Single free** - unregisterProcess() frees memory
4. **Set to NULL after unregister** - Best practice to avoid use-after-free

### GDBus Object Management

**D-Bus Proxy Lifecycle:**
```c
// Created per API call (stateless)
GDBusProxy *proxy = create_dbus_proxy(...);

// Used for single method call
g_dbus_proxy_call_sync(proxy, "RegisterProcess", ...);

// Immediately freed
g_object_unref(proxy);  // Also frees connection
```

**Why Not Persistent?**
- Registration APIs are infrequent (once per plugin lifetime)
- No performance benefit from caching
- Simpler error handling (no stale connection issues)
- No need for connection lifecycle management

## Error Handling Strategy

### registerProcess() Errors

**Philosophy:** Fail fast, return NULL

```c
// Input validation
if (!processName || !libVersion) → NULL
if (strlen(processName) == 0) → NULL
if (strlen(processName) > MAX) → NULL

// D-Bus errors
if (connection fails) → NULL
if (proxy creation fails) → NULL
if (daemon rejects) → NULL

// Memory allocation
if (malloc fails) → NULL
```

All errors logged to stderr for debugging.

### unregisterProcess() Errors

**Philosophy:** Best-effort cleanup, no error propagation

```c
// NULL handle
if (!handler) → no-op, return immediately

// Invalid format
if (parse fails) → free memory, return

// D-Bus errors
if (connection fails) → free memory, return
if (daemon rejects) → free memory, return

// Always free memory regardless of D-Bus success
```

Rationale:
- Cleanup should always succeed (can't retry)
- Daemon may already have cleaned up (connection lost)
- Plugin needs to proceed with shutdown
- Errors logged but not propagated

## Thread Safety

### GDBus Thread Safety

GDBus (GLib D-Bus bindings) provides:
- **Thread-safe method calls** - Can call from any thread
- **Main loop serialization** - Signals dispatched on main loop
- **Connection sharing** - Multiple threads can share connection

### Our Implementation

**registerProcess():**
- No internal locks
- Safe to call from multiple threads IF different processNames
- NOT safe to call concurrently with same processName (daemon rejects)

**unregisterProcess():**
- No internal locks
- Safe to call from different thread than registration
- NOT safe to call concurrently with same handle (double-free risk)

**Best Practice:**
```c
// Main thread
FirmwareInterfaceHandle g_handle = NULL;
GMutex g_handle_mutex;

// Thread-safe registration
void register_safely() {
    g_mutex_lock(&g_handle_mutex);
    if (g_handle == NULL) {
        g_handle = registerProcess("MyPlugin", "1.0");
    }
    g_mutex_unlock(&g_handle_mutex);
}

// Thread-safe unregistration
void unregister_safely() {
    g_mutex_lock(&g_handle_mutex);
    if (g_handle != NULL) {
        unregisterProcess(g_handle);
        g_handle = NULL;
    }
    g_mutex_unlock(&g_handle_mutex);
}
```

## Integration with Existing Code

### Daemon Side (Already Implemented)

**Location:** `src/dbus/rdkv_dbus_server.c`

Already implements:
- `process_app_request()` - D-Bus method dispatcher
- `add_process_to_tracking()` - Handles RegisterProcess
- `remove_process_from_tracking()` - Handles UnregisterProcess
- `registered_processes` - GHashTable for tracking

**No changes needed to daemon** - It already supports the protocol.

### Client Library Integration

**Add to librdkFwupdateMgr build:**

In `librdkFwupdateMgr/Makefile.am` (or equivalent):
```makefile
# Public headers
include_HEADERS = \
    include/rdkFwupdateMgr_client.h \
    include/rdkFwupdateMgr_process.h

# Source files
lib_LTLIBRARIES = librdkFwupdateMgr.la
librdkFwupdateMgr_la_SOURCES = \
    src/rdkFwupdateMgr_client.c \
    src/rdkFwupdateMgr_process.c

# Dependencies
librdkFwupdateMgr_la_CFLAGS = $(GLIB_CFLAGS) $(GIO_CFLAGS)
librdkFwupdateMgr_la_LIBADD = $(GLIB_LIBS) $(GIO_LIBS)
```

### Usage in Existing Client Code

**Before (old pattern):**
```c
// Old code directly called CheckForUpdate without registration
checkForUpdate(current_version, ...);
```

**After (new pattern):**
```c
// Register first
FirmwareInterfaceHandle handle = registerProcess("MyPlugin", "1.0");
if (handle == NULL) {
    // Handle error
    return;
}

// Now call firmware APIs with handle
checkForUpdate(handle, current_version, ...);

// Cleanup
unregisterProcess(handle);
```

## Build & Test

### Build Library

```bash
cd librdkFwupdateMgr
autoreconf -fi
./configure
make
sudo make install
```

### Build Example

```bash
cd librdkFwupdateMgr/examples
gcc -o example example_plugin_registration.c \
    -I../include \
    -L../lib -lrdkFwupdateMgr \
    $(pkg-config --cflags --libs glib-2.0 gio-2.0)
```

### Run Tests

```bash
# Start daemon
sudo systemctl start rdkFwupdateMgr.service

# Run example
./example

# Expected output:
# [Plugin] Registering...
# [rdkFwupdateMgr] Registration successful
# [rdkFwupdateMgr]   handler_id: 12345
# [Plugin] Registration successful: 12345
# ...
```

### Verify D-Bus Communication

```bash
# Monitor D-Bus traffic
dbus-monitor --system "sender='org.rdkfwupdater.Interface'"

# In another terminal, run example
./example

# Should see:
# method call sender=:1.123 -> destination=org.rdkfwupdater.Interface
#   path=/org/rdkfwupdater/Service; interface=org.rdkfwupdater.Interface;
#   member=RegisterProcess
#   string "VideoPlayerPlugin"
#   string "2.0.1"
# method return sender=:1.1 -> destination=:1.123
#   uint64 12345
```

## Best Practices for Plugin Teams

### 1. Register Once at Init

```c
// Good: Register at plugin initialization
FirmwareInterfaceHandle g_fw_handle = NULL;

int plugin_init() {
    g_fw_handle = registerProcess("MyPlugin", PLUGIN_VERSION);
    if (g_fw_handle == NULL) {
        log_error("Failed to register with firmware daemon");
        return ERROR_INIT;
    }
    return SUCCESS;
}
```

### 2. Always Check Return Value

```c
// Good: Check for NULL
FirmwareInterfaceHandle handle = registerProcess("MyPlugin", "1.0");
if (handle == FIRMWARE_INVALID_HANDLE) {
    // Handle error gracefully
    return ERROR;
}

// Bad: Don't assume success
FirmwareInterfaceHandle bad = registerProcess("MyPlugin", "1.0");
checkForUpdate(bad, ...);  // May crash if bad == NULL
```

### 3. Cleanup at Shutdown

```c
// Good: Cleanup in destructor/atexit
void plugin_cleanup() {
    if (g_fw_handle != NULL) {
        unregisterProcess(g_fw_handle);
        g_fw_handle = NULL;
    }
}

// Register cleanup handler
atexit(plugin_cleanup);
```

### 4. Use Descriptive Process Names

```c
// Good: Descriptive, unique
registerProcess("VideoPlayerService", "1.0");
registerProcess("EPGDownloadManager", "2.1");

// Bad: Generic, likely to conflict
registerProcess("Plugin", "1.0");
registerProcess("Service", "1.0");
```

### 5. Thread-Safe Access

```c
// Good: Protect shared handle with mutex
GMutex handle_mutex;
FirmwareInterfaceHandle shared_handle = NULL;

void thread_safe_check_update() {
    g_mutex_lock(&handle_mutex);
    if (shared_handle != NULL) {
        checkForUpdate(shared_handle, ...);
    }
    g_mutex_unlock(&handle_mutex);
}
```

## Common Issues & Solutions

### Issue 1: Registration Fails with "Process name already registered"

**Cause:** Another instance of your plugin already registered

**Solution:**
1. Ensure only one instance runs
2. Use unique process names per instance
3. Check if previous instance didn't unregister

```c
// Add instance ID to process name
char process_name[256];
snprintf(process_name, sizeof(process_name), "MyPlugin_%d", getpid());
handle = registerProcess(process_name, "1.0");
```

### Issue 2: D-Bus Connection Fails

**Cause:** Daemon not running or D-Bus permission denied

**Solution:**
1. Check daemon status: `systemctl status rdkFwupdateMgr`
2. Verify D-Bus permissions in `/etc/dbus-1/system.d/`
3. Check logs: `journalctl -u rdkFwupdateMgr`

### Issue 3: Handle Becomes Invalid After Daemon Restart

**Cause:** Daemon restart clears all registrations

**Solution:** Implement auto-reconnect logic
```c
FirmwareInterfaceHandle get_handle() {
    if (g_handle == NULL) {
        g_handle = registerProcess("MyPlugin", "1.0");
    }
    return g_handle;
}

// Use wrapper for all APIs
int safe_check_update() {
    FirmwareInterfaceHandle h = get_handle();
    if (h == NULL) return ERROR;
    return checkForUpdate(h, ...);
}
```

## Performance Characteristics

### registerProcess()
- **Latency:** ~10-50ms (D-Bus round-trip)
- **Memory:** 32 bytes (handle string)
- **CPU:** Minimal (single D-Bus call)
- **Blocking:** Yes (synchronous call)

### unregisterProcess()
- **Latency:** ~10-50ms (D-Bus round-trip)
- **Memory:** Frees 32 bytes
- **CPU:** Minimal (single D-Bus call)
- **Blocking:** Yes (synchronous call)

### Recommendations
- Call once at init/cleanup (not in hot path)
- No caching needed (daemon-side handles it)
- No performance concerns for infrequent operations

## Future Enhancements

### 1. Async Registration (Optional)
```c
void registerProcessAsync(const char *processName,
                         const char *libVersion,
                         void (*callback)(FirmwareInterfaceHandle, void*),
                         void *user_data);
```

### 2. Auto-Reconnect on Daemon Restart
```c
// Library automatically re-registers if daemon restarts
// Transparent to plugin code
```

### 3. Registration Watchdog
```c
// Daemon periodically pings registered processes
// Auto-cleanup if process crashes
```

## Summary

✅ **Implemented:**
- Public header with clean API
- D-Bus client implementation
- Full error handling
- Memory management
- Example code
- Documentation

✅ **ABI Stable:** Opaque char* handle

✅ **Thread Safe:** With proper mutex usage

✅ **Production Ready:** Proper error handling, logging, validation

✅ **No Daemon Changes:** Works with existing rdkv_dbus_server.c

✅ **Plugin Friendly:** Simple API, clear examples, comprehensive docs
