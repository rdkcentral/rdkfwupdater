# API Documentation: `registerProcess()`

**Library:** librdkFwupdateMgr  
**File:** `src/rdkFwupdateMgr_process.c`  
**Date:** March 10, 2026

---

## Overview

`registerProcess()` is the **entry point** for all client applications using the firmware update library. It registers a client process with the rdkFwupdateMgr daemon and returns an opaque handle that must be used for all subsequent API calls.

### Function Signature
```c
FirmwareInterfaceHandle registerProcess(const char *processName, const char *libVersion);
```

### Parameters
- **`processName`** (const char*): Name of the client process (e.g., "MyApp", "ThunderPlugin")
  - Must not be NULL
  - Must not be empty string
  - Maximum length: 256 characters
  
- **`libVersion`** (const char*): Version of the client library (e.g., "1.0.0")
  - Must not be NULL
  - Can be empty string
  - Maximum length: 64 characters

### Return Value
- **Success:** Opaque `FirmwareInterfaceHandle` (internally a string like "12345")
- **Failure:** `NULL`

---

## Complete Execution Flow

### Entry Point: `registerProcess()` (Line 262)

```
APP CALLS: registerProcess("ExampleApp", "1.0.0")
    │
    ├─► [STEP 1] Input Validation
    │   ├─► validate_process_name()
    │   │   - Check processName != NULL
    │   │   - Check processName not empty
    │   │   - Check length <= 256
    │   │
    │   └─► validate_lib_version()
    │       - Check libVersion != NULL
    │       - Check length <= 64
    │
    ├─► [STEP 2] Create D-Bus Proxy
    │   │
    │   └─► create_dbus_proxy()
    │       ├─► g_bus_get_sync(G_BUS_TYPE_SYSTEM)
    │       │   - Connect to system D-Bus
    │       │   - **BLOCKS** until connection established
    │       │   - Typical time: 5-20ms
    │       │
    │       └─► g_dbus_proxy_new_sync()
    │           - Create proxy for:
    │             Service: org.rdkfwupdater.Service
    │             Path:    /org/rdkfwupdater/Service
    │             Interface: org.rdkfwupdater.Interface
    │           - **BLOCKS** until proxy ready
    │           - Typical time: 5-10ms
    │
    ├─► [STEP 3] Call Daemon RegisterProcess Method
    │   │
    │   └─► g_dbus_proxy_call_sync()
    │       - Method: "RegisterProcess"
    │       - Args:   (ss) = (processName, libVersion)
    │       - **BLOCKS** until daemon responds
    │       - Timeout: 10 seconds (DBUS_TIMEOUT_MS)
    │       - Typical time: 5-15ms
    │       │
    │       ├─► [DAEMON SIDE]
    │       │   - Validates inputs
    │       │   - Generates unique handler_id (uint64)
    │       │   - Stores registration in internal table
    │       │   - Returns handler_id to client
    │       │
    │       └─► [CLIENT RECEIVES]
    │           - GVariant result: (t) = (handler_id)
    │           - Example: handler_id = 12345
    │
    ├─► [STEP 4] Extract handler_id from Result
    │   │
    │   └─► g_variant_get(result, "(t)", &handler_id)
    │       - Unpacks uint64 handler_id
    │       - Example: handler_id = 12345
    │
    ├─► [STEP 5] Convert to String Handle
    │   │
    │   ├─► malloc(32)  // Allocate string buffer
    │   │   - Heap allocation for handle string
    │   │   - 32 bytes sufficient for uint64 in decimal
    │   │
    │   └─► snprintf(handle_str, 32, "%"PRIu64, handler_id)
    │       - Converts uint64 to decimal string
    │       - Example: "12345"
    │       - This string IS the handle returned to app
    │
    ├─► [STEP 6] Cleanup
    │   │
    │   ├─► g_variant_unref(result)  // Free D-Bus result
    │   └─► g_object_unref(proxy)    // Release D-Bus proxy
    │
    └─► [STEP 7] Return Handle to App
        │
        └─► return (FirmwareInterfaceHandle)handle_str
            - App receives: "12345"
            - App must store this handle
            - App uses handle for all future API calls
            - App must call unregisterProcess(handle) when done
```

---

## Detailed Function Call Tree

### 1. validate_process_name() (Line 193)
**Purpose:** Ensure process name meets requirements

**Logic:**
```c
if (!processName)                              → Log error, return false
if (strlen(processName) == 0)                  → Log error, return false
if (strlen(processName) > MAX_PROCESS_NAME_LEN) → Log error, return false
return true
```

**Logged Errors:**
- `"processName is NULL"`
- `"processName is empty"`
- `"processName exceeds max length (256)"`

---

### 2. validate_lib_version() (Line 218)
**Purpose:** Ensure library version meets requirements

**Logic:**
```c
if (!libVersion)                              → Log error, return false
if (strlen(libVersion) > MAX_LIB_VERSION_LEN) → Log error, return false
return true
```

**Logged Errors:**
- `"libVersion is NULL"`
- `"libVersion exceeds max length (64)"`

**Note:** Empty string is allowed for libVersion (but NULL is not)

---

### 3. create_dbus_proxy() (Line 140)
**Purpose:** Establish D-Bus connection and create proxy for daemon communication

**Logic:**
```c
[A] g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error)
    - Connects to system D-Bus daemon
    - SYNCHRONOUS: Blocks until connected
    - Returns GDBusConnection* or NULL on failure
    - Typical latency: 5-20ms
    
[B] g_dbus_proxy_new_sync(
        connection,
        G_DBUS_PROXY_FLAGS_NONE,
        NULL,                       // No introspection data needed
        DBUS_SERVICE_NAME,          // "org.rdkfwupdater.Service"
        DBUS_OBJECT_PATH,           // "/org/rdkfwupdater/Service"
        DBUS_INTERFACE_NAME,        // "org.rdkfwupdater.Interface"
        NULL,                       // No cancellable
        &error
    )
    - Creates proxy object for method calls
    - SYNCHRONOUS: Blocks until proxy ready
    - Returns GDBusProxy* or NULL on failure
    - Typical latency: 5-10ms
    
[C] g_object_unref(connection)
    - Release connection reference
    - Proxy now owns the connection
    - Connection will be freed when proxy is freed
    
[D] return proxy
```

**Error Handling:**
- Connection failure → Log error, return NULL
- Proxy creation failure → Unref connection, log error, return NULL

---

### 4. g_dbus_proxy_call_sync() (Line 299)
**Purpose:** Call RegisterProcess method on daemon (synchronous RPC)

**D-Bus Method Details:**
- **Method Name:** `"RegisterProcess"`
- **Input Signature:** `(ss)` = (string, string)
  - Argument 1: processName
  - Argument 2: libVersion
- **Output Signature:** `(t)` = (uint64)
  - Return value: handler_id

**Call Parameters:**
```c
g_dbus_proxy_call_sync(
    proxy,                             // Proxy to daemon
    "RegisterProcess",                 // Method name
    g_variant_new("(ss)", 
                  processName, 
                  libVersion),         // Input arguments
    G_DBUS_CALL_FLAGS_NONE,           // No special flags
    DBUS_TIMEOUT_MS,                   // Timeout: 10000ms (10 seconds)
    NULL,                              // No cancellable
    &error                             // Error output
)
```

**What Happens:**
1. Serialize (processName, libVersion) to D-Bus wire format
2. Send message to daemon over D-Bus socket
3. **BLOCK** waiting for daemon reply
4. Daemon processes request:
   - Validates inputs
   - Generates unique handler_id (uint64)
   - Stores process in registration table
   - Sends reply: (handler_id)
5. Deserialize daemon reply
6. Return GVariant* containing (handler_id)

**Timeout Behavior:**
- If daemon doesn't respond within 10 seconds → GError set
- This timeout is very generous (daemon typically responds in <15ms)

**Possible Errors:**
- Daemon not running → `"org.freedesktop.DBus.Error.ServiceUnknown"`
- D-Bus connection lost → `"org.freedesktop.DBus.Error.NoReply"`
- Daemon internal error → Custom error message from daemon

---

### 5. Handle Creation (Lines 316-360)
**Purpose:** Convert daemon's uint64 handler_id to string handle for app

**Logic:**
```c
[A] Extract handler_id from D-Bus result
    g_variant_get(result, "(t)", &handler_id)
    - Unpacks tuple containing one uint64
    - Example: handler_id = 12345
    
[B] Allocate string buffer
    handle_str = (char*)malloc(32)
    - 32 bytes = enough for 20-digit uint64 + null terminator
    - Heap allocation (must be freed by unregisterProcess)
    
[C] Convert uint64 to decimal string
    snprintf(handle_str, 32, "%"PRIu64, handler_id)
    - Uses PRIu64 macro for portable uint64 formatting
    - Example result: "12345"
    
[D] Return handle string to app
    return (FirmwareInterfaceHandle)handle_str
    - Cast char* to opaque handle type
    - App receives pointer to heap-allocated string
```

**Memory Ownership:**
- Handle string is **owned by the app**
- App must **NOT** free it themselves
- Library will free it in `unregisterProcess()`

---

## Error Handling: Complete Failure Paths

### Failure Path 1: NULL processName
```
registerProcess(NULL, "1.0.0")
  → validate_process_name()
    → Log: "processName is NULL"
    → return false
  → registerProcess() returns NULL
```

### Failure Path 2: Empty processName
```
registerProcess("", "1.0.0")
  → validate_process_name()
    → Log: "processName is empty"
    → return false
  → registerProcess() returns NULL
```

### Failure Path 3: processName too long
```
registerProcess("<300 char string>", "1.0.0")
  → validate_process_name()
    → Log: "processName exceeds max length (256)"
    → return false
  → registerProcess() returns NULL
```

### Failure Path 4: NULL libVersion
```
registerProcess("MyApp", NULL)
  → validate_lib_version()
    → Log: "libVersion is NULL"
    → return false
  → registerProcess() returns NULL
```

### Failure Path 5: D-Bus connection failure
```
registerProcess("MyApp", "1.0.0")
  → create_dbus_proxy()
    → g_bus_get_sync() fails
      → Log: "Failed to connect to D-Bus system bus: <error>"
      → return NULL
  → registerProcess() returns NULL
```

### Failure Path 6: D-Bus proxy creation failure
```
registerProcess("MyApp", "1.0.0")
  → create_dbus_proxy()
    → g_bus_get_sync() succeeds
    → g_dbus_proxy_new_sync() fails
      → g_object_unref(connection)
      → Log: "Failed to create D-Bus proxy: <error>"
      → return NULL
  → registerProcess() returns NULL
```

### Failure Path 7: RegisterProcess D-Bus call failure
```
registerProcess("MyApp", "1.0.0")
  → create_dbus_proxy() succeeds
  → g_dbus_proxy_call_sync() fails (daemon not responding)
    → Log: "RegisterProcess D-Bus call failed: <error>"
    → g_error_free(error)
    → g_object_unref(proxy)
    → return NULL
  → registerProcess() returns NULL
```

### Failure Path 8: Handle allocation failure (CRITICAL)
**This is the most complex error path** because registration succeeded on daemon,  
but we can't return the handle to the app. Must perform cleanup to prevent leak.

```
registerProcess("MyApp", "1.0.0")
  → create_dbus_proxy() succeeds
  → g_dbus_proxy_call_sync() succeeds
    → Daemon returns handler_id = 12345
  → malloc(32) fails (out of memory)
    → Log: "Failed to allocate memory for handle"
    → Log: "Attempting best-effort cleanup: UnregisterProcess(12345)"
    
    [CLEANUP ATTEMPT]
    → create_dbus_proxy() again
    → g_dbus_proxy_call_sync("UnregisterProcess", 12345)
      ├─► Success case:
      │   → Log: "Cleanup successful: process unregistered"
      │   → g_variant_unref(cleanup_result)
      │   → g_object_unref(cleanup_proxy)
      │
      └─► Failure case:
          → Log: "Cleanup failed: <error> (registration may be leaked)"
          → Registration remains on daemon (resource leak)
          → Daemon will eventually timeout/cleanup when it detects client disconnect
    
  → registerProcess() returns NULL
```

**Why This Matters:**
- Daemon has consumed resources (memory, handler_id slot)
- Without cleanup, daemon thinks client is still registered
- Best-effort unregister prevents resource leak
- Even if cleanup fails, daemon's disconnect-detection will eventually clean up

---

## Memory Management

### Allocations Made by registerProcess()
1. **GDBusConnection*** (via g_bus_get_sync)
   - Freed by: g_object_unref(connection) in create_dbus_proxy()
   
2. **GDBusProxy*** (via g_dbus_proxy_new_sync)
   - Freed by: g_object_unref(proxy) before returning
   
3. **GVariant*** result (via g_dbus_proxy_call_sync)
   - Freed by: g_variant_unref(result) before returning
   
4. **char* handle_str** (via malloc)
   - **NOT FREED** in registerProcess()
   - Ownership transferred to caller
   - **MUST** be freed by unregisterProcess()

### Caller Responsibilities
- **Store handle:** Save returned handle for future API calls
- **Never free handle:** Library owns the memory
- **Call unregisterProcess():** Frees handle and unregisters from daemon

### Leak Prevention
- All GLib objects (connection, proxy, variant) are ref-counted
- All refs released before returning
- Only handle string survives (intentionally, owned by caller)

---

## Thread Safety

### Is registerProcess() Thread-Safe?
**YES**, with caveats:

1. **GDBus is thread-safe** for synchronous calls
   - g_bus_get_sync(), g_dbus_proxy_call_sync() can be called from any thread
   - GLib handles internal locking

2. **No shared state** in registerProcess()
   - All variables are stack-local
   - No global variables modified
   - Stateless design

3. **Multiple threads can register concurrently**
   - Each gets unique handler_id from daemon
   - No race conditions in client library

### Caveats
- **Different process names:** Each thread should register with unique processName
- **Same process name:** Daemon may reject or allow (daemon policy)
- **Handle storage:** Each thread must store its own handle

---

## Performance Characteristics

### Typical Execution Time
- **Fast path:** 15-50ms
  - D-Bus connect: 5-20ms
  - Proxy create: 5-10ms
  - RegisterProcess call: 5-20ms

### Worst Case
- **10 seconds** (DBUS_TIMEOUT_MS)
  - If daemon is hung/not responding
  - Very rare in practice

### Blocking Behavior
- **SYNCHRONOUS** call
- Calling thread **BLOCKS** until completion
- Not suitable for UI thread (if hard real-time required)
- Acceptable for most use cases (< 50ms typical)

---

## Logging Output Example

### Successful Registration
```
[librdkFwupdateMgr] INFO: registerProcess() called
[librdkFwupdateMgr] INFO:   processName: 'ExampleApp'
[librdkFwupdateMgr] INFO:   libVersion:  '1.0.0'
[rdkFwupdateMgr] D-Bus proxy created successfully
[rdkFwupdateMgr] Calling RegisterProcess D-Bus method...
[librdkFwupdateMgr] INFO: Registration successful
[librdkFwupdateMgr] INFO:   handler_id: 12345
[librdkFwupdateMgr] INFO: Handle created: '12345'
```

### Failed Registration (NULL processName)
```
[librdkFwupdateMgr] INFO: registerProcess() called
[librdkFwupdateMgr] INFO:   processName: 'NULL'
[librdkFwupdateMgr] INFO:   libVersion:  '1.0.0'
[librdkFwupdateMgr] ERROR: processName is NULL
```

### Failed Registration (D-Bus connection error)
```
[librdkFwupdateMgr] INFO: registerProcess() called
[librdkFwupdateMgr] INFO:   processName: 'ExampleApp'
[librdkFwupdateMgr] INFO:   libVersion:  '1.0.0'
[librdkFwupdateMgr] ERROR: Failed to connect to D-Bus system bus: Connection refused
```

---

## Common Usage Patterns

### Pattern 1: Simple Registration
```c
FirmwareInterfaceHandle handle = registerProcess("MyApp", "1.0.0");
if (!handle) {
    fprintf(stderr, "Registration failed!\n");
    return -1;
}

// Use handle for other APIs...
checkForUpdate(handle, my_callback);

// Clean up when done
unregisterProcess(handle);
```

### Pattern 2: Registration with Retry
```c
FirmwareInterfaceHandle handle = NULL;
int attempts = 0;

while (!handle && attempts < 3) {
    handle = registerProcess("MyApp", "1.0.0");
    if (!handle) {
        fprintf(stderr, "Registration failed, attempt %d/3\n", attempts + 1);
        sleep(1);
    }
    attempts++;
}

if (!handle) {
    fprintf(stderr, "Registration failed after 3 attempts\n");
    return -1;
}
```

### Pattern 3: RAII-style Wrapper (C++ only)
```cpp
class FirmwareHandle {
public:
    FirmwareHandle(const char* name, const char* ver) {
        handle_ = registerProcess(name, ver);
        if (!handle_) throw std::runtime_error("Registration failed");
    }
    ~FirmwareHandle() {
        if (handle_) unregisterProcess(handle_);
    }
    operator FirmwareInterfaceHandle() const { return handle_; }
private:
    FirmwareInterfaceHandle handle_;
};

// Usage
{
    FirmwareHandle handle("MyApp", "1.0.0");
    checkForUpdate(handle, callback);
} // Automatic unregister on scope exit
```

---

## Integration with Other APIs

`registerProcess()` is a **prerequisite** for all other APIs:

```
registerProcess()  ← Must call first
    ↓
    ├─► checkForUpdate()         ← Requires valid handle
    │       ↓
    │       └─► [callback receives firmware info]
    │               ↓
    ├─► downloadFirmware()       ← Requires valid handle
    │       ↓
    │       └─► [progress callbacks]
    │               ↓
    └─► updateFirmware()         ← Requires valid handle
            ↓
            └─► [progress callbacks]
                    ↓
unregisterProcess()  ← Must call last
```

### Handle Lifecycle
```
[APP START]
    │
    ├─► registerProcess() → Handle created
    │                         │
    │                         │ [Handle is valid]
    │                         │
    │   ┌─────────────────────┼─────────────────────┐
    │   │                     │                     │
    │   │ checkForUpdate(handle)  downloadFirmware(handle)  updateFirmware(handle)
    │   │                     │                     │
    │   └─────────────────────┼─────────────────────┘
    │                         │
    ├─► unregisterProcess() → Handle freed
    │                         │
    │                         │ [Handle is INVALID - DO NOT USE]
    │
[APP EXIT]
```

**CRITICAL:** Using handle after unregisterProcess() is **undefined behavior**

---

## D-Bus Protocol Details

### Message Flow
```
CLIENT                                    DAEMON
  │                                          │
  ├──── RegisterProcess("MyApp", "1.0.0") ──►│
  │                                          │ [Generate handler_id=12345]
  │                                          │ [Store: "MyApp" → 12345]
  │◄────── Reply: (12345) ───────────────────┤
  │                                          │
```

### Wire Format (Binary)
- Endianness: Little-endian (on most systems)
- String encoding: UTF-8 with null terminator
- uint64 encoding: 8 bytes, native byte order

### D-Bus Introspection (daemon side)
```xml
<method name="RegisterProcess">
  <arg name="processName" type="s" direction="in"/>
  <arg name="libVersion" type="s" direction="in"/>
  <arg name="handler_id" type="t" direction="out"/>
</method>
```

---

## Comparison with Similar APIs

### vs. Manual D-Bus Usage
```c
// Manual (error-prone, verbose)
GDBusConnection *conn = g_bus_get_sync(...);
GVariant *result = g_dbus_connection_call_sync(
    conn, "org.rdkfwupdater.Service", ..., 
    g_variant_new("(ss)", name, ver), ...);
guint64 id;
g_variant_get(result, "(t)", &id);
char *handle = malloc(32);
sprintf(handle, "%lu", id);
// ... cleanup ...

// vs. Wrapper (clean, safe)
FirmwareInterfaceHandle handle = registerProcess(name, ver);
```

**Benefits of Wrapper:**
- Input validation
- Error handling
- Memory management
- Logging
- Retry logic (in failure path 8)

---

## FAQs

### Q: Can I register the same process name twice?
**A:** Yes, daemon allows multiple registrations with same name. Each gets unique handler_id.

### Q: What happens if daemon crashes?
**A:** `registerProcess()` will fail with D-Bus error. App must handle gracefully.

### Q: Can I share a handle between threads?
**A:** Yes, but not recommended. Each thread should register separately for better isolation.

### Q: What if I never call unregisterProcess()?
**A:** Memory leak in client (32 bytes). Daemon will eventually detect disconnect and cleanup.

### Q: Can I call registerProcess() after unregisterProcess()?
**A:** Yes, you'll get a new handle. Old handle is invalid and must not be used.

### Q: What's the maximum number of concurrent registrations?
**A:** Daemon-dependent. Typically 100-1000. Library has no limit.

---

## See Also
- `API_DOCUMENTATION_unregisterProcess.md` - Cleanup counterpart
- `API_DOCUMENTATION_checkForUpdate.md` - First API to call after registration
- `rdkFwupdateMgr.h` - Public API header
- `rdkFwupdateMgr_process.c` - Implementation source
