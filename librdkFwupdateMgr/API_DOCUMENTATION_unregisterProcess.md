# API Documentation: `unregisterProcess()`

**Library:** librdkFwupdateMgr  
**File:** `src/rdkFwupdateMgr_process.c`  
**Date:** March 10, 2026

---

## Overview

`unregisterProcess()` is the **cleanup function** that unregisters a client process from the rdkFwupdateMgr daemon and frees the handle memory. It is the counterpart to `registerProcess()` and **must be called** before the application exits to prevent resource leaks.

### Function Signature
```c
void unregisterProcess(FirmwareInterfaceHandle handler);
```

### Parameters
- **`handler`** (FirmwareInterfaceHandle): Handle returned by `registerProcess()`
  - Can be NULL (treated as no-op)
  - Must be valid handle (not already unregistered)
  - Must not be garbage/random pointer

### Return Value
- **void** - No return value
- Errors are logged but not propagated
- Best-effort cleanup (continues even if D-Bus call fails)

---

## Complete Execution Flow

### Entry Point: `unregisterProcess()` (Line 375)

```
APP CALLS: unregisterProcess(handle)  // handle = "12345"
    │
    ├─► [STEP 1] NULL Check
    │   │
    │   └─► if (!handler) {
    │           Log: "unregisterProcess() called with NULL handle (no-op)"
    │           return immediately
    │       }
    │
    ├─► [STEP 2] Parse Handle String → Extract handler_id
    │   │
    │   │   Input: handler = "12345" (string)
    │   │
    │   ├─► strtoull(handler, &endptr, 10)
    │   │   - Converts string "12345" to uint64: 12345
    │   │   - Base 10 (decimal)
    │   │   - endptr points to first non-digit character
    │   │
    │   ├─► VALIDATION CHECKS:
    │   │   │
    │   │   ├─► if (errno != 0)
    │   │   │   → Overflow/underflow occurred
    │   │   │   → Log: "Invalid handle: numeric overflow/underflow"
    │   │   │   → free(handler), return
    │   │   │
    │   │   ├─► if (endptr == handler)
    │   │   │   → No digits parsed (e.g., "abc")
    │   │   │   → Log: "Invalid handle: no digits found"
    │   │   │   → free(handler), return
    │   │   │
    │   │   ├─► if (*endptr != '\0')
    │   │   │   → Garbage after number (e.g., "123abc")
    │   │   │   → Log: "Invalid handle: garbage characters after number"
    │   │   │   → free(handler), return
    │   │   │
    │   │   └─► if (handler_id == 0)
    │   │       → Handler ID cannot be zero
    │   │       → Log: "Invalid handle: handler_id cannot be 0"
    │   │       → free(handler), return
    │   │
    │   └─► Result: handler_id = 12345 (uint64)
    │
    ├─► [STEP 3] Create D-Bus Proxy
    │   │
    │   └─► create_dbus_proxy(&error)
    │       │
    │       ├─► SUCCESS:
    │       │   → proxy != NULL
    │       │   → Continue to step 4
    │       │
    │       └─► FAILURE:
    │           → proxy == NULL
    │           → Log: "Failed to create D-Bus proxy for unregister"
    │           → Log: "Error: <error message>"
    │           → g_error_free(error)
    │           → free(handler)  // Still free handle memory
    │           → return  // Best-effort cleanup
    │
    ├─► [STEP 4] Call Daemon UnregisterProcess Method
    │   │
    │   └─► g_dbus_proxy_call_sync()
    │       - Method: "UnregisterProcess"
    │       - Args:   (t) = (handler_id)
    │       - **BLOCKS** until daemon responds
    │       - Timeout: 10 seconds
    │       │
    │       ├─► [DAEMON SIDE]
    │       │   - Looks up handler_id in registration table
    │       │   - Removes registration if found
    │       │   - Returns boolean success flag
    │       │
    │       ├─► SUCCESS (result != NULL):
    │       │   │
    │       │   ├─► g_variant_get(result, "(b)", &success)
    │       │   │   - Extracts boolean from result
    │       │   │
    │       │   ├─► if (success == TRUE)
    │       │   │   → Log: "Unregistration successful"
    │       │   │
    │       │   └─► else (success == FALSE)
    │       │       → Log: "Daemon reported unregistration failure"
    │       │       → Log: "(Handler may have already been unregistered)"
    │       │
    │       └─► FAILURE (result == NULL):
    │           → D-Bus call failed
    │           → Log: "UnregisterProcess D-Bus call failed: <error>"
    │           → Log: "(This is OK if daemon already cleaned up)"
    │           → g_error_free(error)
    │           → Continue to step 5 (still free handle)
    │
    ├─► [STEP 5] Cleanup
    │   │
    │   ├─► g_variant_unref(result)  // If result != NULL
    │   └─► g_object_unref(proxy)    // If proxy != NULL
    │
    └─► [STEP 6] Free Handle Memory (ALWAYS EXECUTED)
        │
        ├─► free(handler)
        │   - Frees the 32-byte string allocated by registerProcess()
        │   - This happens REGARDLESS of D-Bus call success/failure
        │
        └─► Log: "Handle memory freed"
            - Confirms cleanup completed
```

---

## Detailed Function Call Tree

### 1. NULL Check (Line 386)
**Purpose:** Allow safe no-op unregister of NULL handles

**Logic:**
```c
if (!handler) {
    FWUPMGR_INFO("unregisterProcess() called with NULL handle (no-op)\n");
    return;
}
```

**Use Case:**
```c
FirmwareInterfaceHandle handle = NULL;

// Some condition where registration failed
if (some_error) {
    handle = NULL;
}

// Safe to call even if handle is NULL
unregisterProcess(handle);  // No-op, no crash
```

---

### 2. Handle String Parsing (Lines 392-437)
**Purpose:** Convert string handle back to uint64 handler_id with strict validation

**Parsing Logic:**
```c
errno = 0;                                    // Reset errno before call
char *endptr = NULL;                          // Will point to first non-digit
handler_id = strtoull(handler, &endptr, 10);  // Parse base-10 uint64
```

**Validation Rules:**

#### Rule 1: Check for Overflow/Underflow
```c
if (errno != 0) {
    // String represents number too large/small for uint64
    // Examples: "18446744073709551616" (UINT64_MAX + 1)
    FWUPMGR_ERROR("Invalid handle: numeric overflow/underflow in '%s'\n", handler);
    free(handler);
    return;
}
```

#### Rule 2: Check for No Digits Parsed
```c
if (endptr == handler) {
    // No conversion performed (no digits found)
    // Examples: "abc", "", "xyz"
    FWUPMGR_ERROR("Invalid handle: no digits found in '%s'\n", handler);
    free(handler);
    return;
}
```

#### Rule 3: Check for Garbage After Number
```c
if (*endptr != '\0') {
    // Conversion stopped before end of string
    // Examples: "123abc", "456 ", "789xyz"
    FWUPMGR_ERROR("Invalid handle: garbage characters after number in '%s' "
                  "(parsed %" PRIu64 ", but '%s' remains)\n", 
                  handler, handler_id, endptr);
    free(handler);
    return;
}
```

#### Rule 4: Check for Zero (Invalid Handler ID)
```c
if (handler_id == 0) {
    // Daemon never issues handler_id = 0
    // Zero is reserved as "invalid"
    FWUPMGR_ERROR("Invalid handle: handler_id cannot be 0\n");
    free(handler);
    return;
}
```

**Valid Examples:**
- `"12345"` → handler_id = 12345 ✅
- `"1"` → handler_id = 1 ✅
- `"18446744073709551615"` → handler_id = UINT64_MAX ✅

**Invalid Examples:**
- `"123abc"` → garbage after number ❌
- `" 123"` → leading whitespace (no digits at start) ❌
- `"123 "` → trailing whitespace ❌
- `"abc"` → no digits ❌
- `""` → empty string (no digits) ❌
- `"0"` → zero is invalid ❌

---

### 3. Create D-Bus Proxy (Line 440)
**Purpose:** Establish D-Bus connection to call UnregisterProcess

**Logic:**
```c
proxy = create_dbus_proxy(&error);

if (!proxy) {
    // Connection failed - log warning but continue cleanup
    FWUPMGR_WARN("Failed to create D-Bus proxy for unregister\n");
    if (error) {
        FWUPMGR_WARN("  Error: %s\n", error->message);
        g_error_free(error);
    }
    
    // Still free handle memory even if D-Bus unavailable
    free(handler);
    return;
}
```

**Why "WARN" not "ERROR":**
- Unregister is best-effort cleanup
- D-Bus failure doesn't prevent handle cleanup
- Daemon will eventually detect client disconnect and clean up server-side

**Common Scenarios:**
- Daemon crashed → Proxy creation fails → Continue with local cleanup
- D-Bus unavailable → Proxy creation fails → Continue with local cleanup
- Normal case → Proxy creation succeeds → Proceed to D-Bus call

---

### 4. UnregisterProcess D-Bus Call (Line 454)
**Purpose:** Notify daemon to remove this process from registration table

**D-Bus Method Details:**
- **Method Name:** `"UnregisterProcess"`
- **Input Signature:** `(t)` = (uint64)
  - Argument: handler_id
- **Output Signature:** `(b)` = (boolean)
  - Return value: success flag

**Call Parameters:**
```c
result = g_dbus_proxy_call_sync(
    proxy,
    "UnregisterProcess",           // Method name
    g_variant_new("(t)", handler_id), // Input: (handler_id)
    G_DBUS_CALL_FLAGS_NONE,
    DBUS_TIMEOUT_MS,                // 10 seconds
    NULL,                           // No cancellable
    &error
);
```

**Daemon Processing:**
```
[DAEMON]
  ├─► Receive UnregisterProcess(handler_id=12345)
  ├─► Look up 12345 in registration table
  │   │
  │   ├─► FOUND:
  │   │   ├─► Remove entry from table
  │   │   ├─► Cancel any pending operations for this process
  │   │   ├─► Free server-side resources
  │   │   └─► Reply: (success=true)
  │   │
  │   └─► NOT FOUND:
  │       ├─► Handler already unregistered OR invalid
  │       └─► Reply: (success=false)
  │
  └─► Send reply to client
```

**Client Processing:**
```c
if (!result) {
    // D-Bus call failed (timeout, connection lost, etc.)
    FWUPMGR_WARN("UnregisterProcess D-Bus call failed: %s\n", error->message);
    FWUPMGR_WARN("  (This is OK if daemon already cleaned up)\n");
    g_error_free(error);
    g_object_unref(proxy);
    
    // Still free handle memory
    free(handler);
    return;
}

// Extract success flag
gboolean success = FALSE;
g_variant_get(result, "(b)", &success);

if (success) {
    FWUPMGR_INFO("Unregistration successful\n");
} else {
    FWUPMGR_WARN("Daemon reported unregistration failure\n");
    FWUPMGR_WARN("  (Handler may have already been unregistered)\n");
}
```

---

### 5. Handle Memory Cleanup (Line 483)
**Purpose:** Free the 32-byte handle string allocated by registerProcess()

**Always Executed:**
```c
free(handler);
FWUPMGR_INFO("Handle memory freed\n");
```

**Why Always:**
- Handle is heap-allocated by registerProcess()
- Must be freed regardless of D-Bus call success
- Prevents memory leak in client process

**Memory Leak Prevention:**
- Even if D-Bus fails, local memory is cleaned up
- Even if daemon fails to unregister, local memory is cleaned up
- No memory leak on client side

---

## Error Handling: Complete Failure Paths

### Failure Path 1: NULL Handle
```
unregisterProcess(NULL)
  → NULL check passes
    → Log: "unregisterProcess() called with NULL handle (no-op)"
    → return immediately
  → No cleanup needed
```

### Failure Path 2: Invalid Handle Format (Garbage)
```
unregisterProcess("abc123")
  → strtoull("abc123", ...) → parses "0", endptr points to "abc123"
  → Validation: endptr == handler (no digits parsed)
    → Log: "Invalid handle: no digits found in 'abc123'"
    → free(handler)
    → return
```

### Failure Path 3: Invalid Handle Format (Trailing Garbage)
```
unregisterProcess("123abc")
  → strtoull("123abc", ...) → parses "123", endptr points to "abc"
  → Validation: *endptr != '\0' (garbage remains)
    → Log: "Invalid handle: garbage characters after number in '123abc' (parsed 123, but 'abc' remains)"
    → free(handler)
    → return
```

### Failure Path 4: Handler ID Zero
```
unregisterProcess("0")
  → strtoull("0", ...) → parses 0, endptr points to '\0'
  → Validation: handler_id == 0
    → Log: "Invalid handle: handler_id cannot be 0"
    → free(handler)
    → return
```

### Failure Path 5: D-Bus Connection Failure
```
unregisterProcess("12345")
  → Parse handle successfully → handler_id = 12345
  → create_dbus_proxy() fails (daemon not running)
    → Log: "Failed to create D-Bus proxy for unregister"
    → Log: "Error: Connection refused"
    → free(handler)  // Still clean up local memory
    → return
  → Server-side registration MAY still exist
  → Daemon will eventually clean up on disconnect detection
```

### Failure Path 6: UnregisterProcess D-Bus Call Fails
```
unregisterProcess("12345")
  → Parse handle successfully → handler_id = 12345
  → create_dbus_proxy() succeeds
  → g_dbus_proxy_call_sync() fails (daemon timeout)
    → Log: "UnregisterProcess D-Bus call failed: Timeout"
    → Log: "(This is OK if daemon already cleaned up)"
    → g_error_free(error)
    → g_object_unref(proxy)
    → free(handler)  // Still clean up local memory
    → return
```

### Failure Path 7: Daemon Reports Unregistration Failure
```
unregisterProcess("12345")
  → Parse handle successfully → handler_id = 12345
  → create_dbus_proxy() succeeds
  → g_dbus_proxy_call_sync() succeeds
    → Daemon returns success=false (handler not found)
    → Log: "Daemon reported unregistration failure"
    → Log: "(Handler may have already been unregistered)"
    → g_variant_unref(result)
    → g_object_unref(proxy)
    → free(handler)  // Still clean up local memory
    → return
```

**Common Cause:** Handle already unregistered (double-unregister)

---

## Best-Effort Cleanup Philosophy

### Design Principle
`unregisterProcess()` uses **best-effort cleanup**:
- Errors are logged as warnings (not errors)
- Cleanup continues even if D-Bus calls fail
- Handle memory is always freed
- No error propagation to caller (void return)

### Why Best-Effort?
1. **Unregister is cleanup code** - often called in error paths or destructors
2. **Caller can't do anything about failure** - no reasonable recovery action
3. **Local cleanup must always happen** - prevent client memory leak
4. **Server cleanup will eventually happen** - daemon detects disconnect

### Guarantees
✅ **Handle memory will be freed** (no client-side leak)  
✅ **D-Bus call will be attempted** (if connection available)  
⚠️ **Server-side cleanup may be delayed** (if D-Bus unavailable)

---

## Memory Management

### Allocations Freed by unregisterProcess()
1. **char* handler** (32 bytes)
   - Allocated by: registerProcess() via malloc(32)
   - Freed by: unregisterProcess() via free(handler)
   - **Always freed**, regardless of D-Bus call success

### Temporary Allocations (freed before returning)
1. **GDBusProxy*** (via create_dbus_proxy)
   - Freed by: g_object_unref(proxy)
   
2. **GVariant*** result (via g_dbus_proxy_call_sync)
   - Freed by: g_variant_unref(result)
   
3. **GError*** error (if set)
   - Freed by: g_error_free(error)

### Memory Leak Prevention
- Handle memory freed in **all code paths** (even error paths)
- Freed **before** any early returns
- No memory leaks on client side

---

## Thread Safety

### Is unregisterProcess() Thread-Safe?
**YES**, with caveats:

1. **GDBus is thread-safe** for synchronous calls
   - g_bus_get_sync(), g_dbus_proxy_call_sync() can be called from any thread

2. **No shared state** in unregisterProcess()
   - All variables are stack-local
   - free() is thread-safe (libc guarantee)

3. **Multiple threads can unregister concurrently** (different handles)
   - Each handle is independent
   - No race conditions

### Caveats
- **Same handle from multiple threads:** Undefined behavior (double-free)
- **Caller must ensure:** Handle used by only one thread at a time
- **After unregisterProcess():** Handle is invalid, must not be used

### Safe Pattern
```c
// Thread A
FirmwareInterfaceHandle handle_A = registerProcess("AppA", "1.0.0");
// ... use handle_A ...
unregisterProcess(handle_A);  // Safe

// Thread B (concurrent with Thread A)
FirmwareInterfaceHandle handle_B = registerProcess("AppB", "1.0.0");
// ... use handle_B ...
unregisterProcess(handle_B);  // Safe (different handle)
```

### Unsafe Pattern
```c
// Thread A and Thread B both have same handle
FirmwareInterfaceHandle shared_handle = registerProcess("App", "1.0.0");

// Thread A                          // Thread B
unregisterProcess(shared_handle);    unregisterProcess(shared_handle);
// ❌ UNDEFINED BEHAVIOR: Double-free of handle memory
```

---

## Performance Characteristics

### Typical Execution Time
- **Fast path:** 10-30ms
  - D-Bus connect: 5-20ms
  - UnregisterProcess call: 5-10ms

### Worst Case
- **10 seconds** (DBUS_TIMEOUT_MS)
  - If daemon is hung/not responding
  - Very rare in practice

### Blocking Behavior
- **SYNCHRONOUS** call
- Calling thread **BLOCKS** until completion
- Acceptable for cleanup code (called during shutdown)

---

## Logging Output Example

### Successful Unregistration
```
[librdkFwupdateMgr] INFO: unregisterProcess() called
[librdkFwupdateMgr] INFO:   handle: '12345'
[librdkFwupdateMgr] INFO:   handler_id: 12345
[librdkFwupdateMgr] INFO: Calling UnregisterProcess D-Bus method...
[librdkFwupdateMgr] INFO: Unregistration successful
[librdkFwupdateMgr] INFO: Handle memory freed
```

### Unregistration with NULL Handle
```
[librdkFwupdateMgr] INFO: unregisterProcess() called with NULL handle (no-op)
```

### Unregistration with Invalid Handle
```
[librdkFwupdateMgr] INFO: unregisterProcess() called
[librdkFwupdateMgr] INFO:   handle: '123abc'
[librdkFwupdateMgr] ERROR: Invalid handle: garbage characters after number in '123abc' (parsed 123, but 'abc' remains)
```

### Unregistration with D-Bus Failure (Best-Effort)
```
[librdkFwupdateMgr] INFO: unregisterProcess() called
[librdkFwupdateMgr] INFO:   handle: '12345'
[librdkFwupdateMgr] INFO:   handler_id: 12345
[librdkFwupdateMgr] WARN: Failed to create D-Bus proxy for unregister
[librdkFwupdateMgr] WARN:   Error: Connection refused
[librdkFwupdateMgr] INFO: Handle memory freed
```

---

## Common Usage Patterns

### Pattern 1: Simple Cleanup
```c
FirmwareInterfaceHandle handle = registerProcess("MyApp", "1.0.0");
if (!handle) {
    return -1;
}

// Use APIs...
checkForUpdate(handle, callback);

// Cleanup
unregisterProcess(handle);
handle = NULL;  // Prevent accidental reuse
```

### Pattern 2: Cleanup on Error Path
```c
FirmwareInterfaceHandle handle = registerProcess("MyApp", "1.0.0");
if (!handle) {
    return -1;
}

if (some_error) {
    unregisterProcess(handle);  // Cleanup even on error
    return -1;
}

// Normal path
unregisterProcess(handle);
return 0;
```

### Pattern 3: RAII-style Cleanup (C++ only)
```cpp
class FirmwareHandle {
public:
    FirmwareHandle(const char* name, const char* ver) {
        handle_ = registerProcess(name, ver);
        if (!handle_) throw std::runtime_error("Registration failed");
    }
    
    ~FirmwareHandle() {
        if (handle_) {
            unregisterProcess(handle_);
            handle_ = nullptr;
        }
    }
    
    // Prevent copying
    FirmwareHandle(const FirmwareHandle&) = delete;
    FirmwareHandle& operator=(const FirmwareHandle&) = delete;
    
    operator FirmwareInterfaceHandle() const { return handle_; }
    
private:
    FirmwareInterfaceHandle handle_;
};

// Usage
{
    FirmwareHandle handle("MyApp", "1.0.0");
    checkForUpdate(handle, callback);
} // Automatic unregister on scope exit (RAII)
```

### Pattern 4: Global Handle with atexit()
```c
static FirmwareInterfaceHandle g_handle = NULL;

static void cleanup_firmware_handle(void) {
    if (g_handle) {
        unregisterProcess(g_handle);
        g_handle = NULL;
    }
}

int main() {
    g_handle = registerProcess("MyApp", "1.0.0");
    if (!g_handle) {
        return 1;
    }
    
    atexit(cleanup_firmware_handle);  // Automatic cleanup on exit
    
    // ... main logic ...
    
    return 0;
}
```

---

## Integration with Other APIs

### Handle Lifecycle
```
registerProcess() → handle created (valid)
    ↓
    ├─► checkForUpdate(handle)      ✅ OK
    ├─► downloadFirmware(handle)    ✅ OK
    ├─► updateFirmware(handle)      ✅ OK
    ↓
unregisterProcess(handle) → handle freed (invalid)
    ↓
    ├─► checkForUpdate(handle)      ❌ UNDEFINED BEHAVIOR
    ├─► downloadFirmware(handle)    ❌ UNDEFINED BEHAVIOR
    ├─► updateFirmware(handle)      ❌ UNDEFINED BEHAVIOR
```

### Must Be Last API Called
```
App Startup:
    registerProcess()       ← First
    ↓
    checkForUpdate()
    ↓
    downloadFirmware()
    ↓
    updateFirmware()
    ↓
    unregisterProcess()     ← Last
    
App Shutdown
```

**CRITICAL:** Using any API after unregisterProcess() is undefined behavior

---

## D-Bus Protocol Details

### Message Flow
```
CLIENT                                    DAEMON
  │                                          │
  ├──── UnregisterProcess(12345) ───────────►│
  │                                          │ [Lookup handler_id=12345]
  │                                          │ [Remove from table]
  │                                          │ [Cancel pending operations]
  │◄───── Reply: (success=true) ─────────────┤
  │                                          │
```

### Wire Format (Binary)
- uint64 handler_id: 8 bytes, native byte order
- boolean success: 4 bytes (GLib convention)

### D-Bus Introspection (daemon side)
```xml
<method name="UnregisterProcess">
  <arg name="handler_id" type="t" direction="in"/>
  <arg name="success" type="b" direction="out"/>
</method>
```

---

## FAQs

### Q: Can I call unregisterProcess() twice on the same handle?
**A:** No, undefined behavior. First call frees the memory, second call uses freed memory (use-after-free).

### Q: What happens if I don't call unregisterProcess()?
**A:** 
- Client-side: Memory leak (32 bytes)
- Server-side: Registration remains until daemon detects disconnect
- Daemon will eventually cleanup (when socket closes)

### Q: Can I call unregisterProcess() from signal handler?
**A:** Not safe. Uses malloc/free and D-Bus (non-async-signal-safe). Set flag and unregister in main thread.

### Q: What if daemon crashes before I unregister?
**A:** unregisterProcess() will fail gracefully (log warning), handle memory is still freed.

### Q: Can I reuse a handle after unregistering?
**A:** No, handle is freed. Must call registerProcess() again to get new handle.

### Q: What if I pass a random string to unregisterProcess()?
**A:** Validation catches it:
- Non-numeric → "no digits found"
- Has garbage → "garbage characters after number"
- Handle is freed (assuming it was malloc'd, otherwise undefined)

### Q: Is it safe to call from destructor/cleanup code?
**A:** Yes, that's the intended use case. Best-effort cleanup with no error propagation.

---

## See Also
- `API_DOCUMENTATION_registerProcess.md` - Registration counterpart
- `rdkFwupdateMgr.h` - Public API header
- `rdkFwupdateMgr_process.c` - Implementation source
