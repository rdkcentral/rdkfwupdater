# Process Registration API - Quick Start Guide

## What You Need to Know

### 1. New Requirement: Registration Before Use

**OLD WAY (deprecated):**
```c
// This will no longer work
checkForUpdate(...);
```

**NEW WAY (required):**
```c
// Step 1: Register
FirmwareInterfaceHandle handle = registerProcess("MyPlugin", "1.0.0");

// Step 2: Use handle for all operations
checkForUpdate(handle, ...);
downloadFirmware(handle, ...);

// Step 3: Cleanup
unregisterProcess(handle);
```

## Quick Integration (5 Minutes)

### Step 1: Include New Header

```c
#include "rdkFwupdateMgr_client.h"  // This now includes rdkFwupdateMgr_process.h automatically
```

### Step 2: Register at Plugin Init

```c
// Global handle (one per plugin)
static FirmwareInterfaceHandle g_fw_handle = NULL;

int my_plugin_init(void) {
    // Register with unique process name
    g_fw_handle = registerProcess("MyPluginName", "1.0.0");
    
    if (g_fw_handle == NULL) {
        fprintf(stderr, "Failed to register with firmware daemon\n");
        return ERROR_INIT_FAILED;
    }
    
    return SUCCESS;
}
```

### Step 3: Update API Calls

**Before:**
```c
checkForUpdate(current_version, available_version, ...);
```

**After:**
```c
checkForUpdate(g_fw_handle, current_version, available_version, ...);
```

### Step 4: Cleanup at Shutdown

```c
void my_plugin_cleanup(void) {
    if (g_fw_handle != NULL) {
        unregisterProcess(g_fw_handle);
        g_fw_handle = NULL;
    }
}

// Register cleanup handler
atexit(my_plugin_cleanup);
```

## API Reference

### registerProcess()

```c
FirmwareInterfaceHandle registerProcess(const char *processName, 
                                        const char *libVersion);
```

**Returns:** Handle string (e.g., "12345") or NULL on error

**Usage:**
```c
FirmwareInterfaceHandle handle = registerProcess("VideoPlayer", "2.0.1");
if (handle == NULL) {
    // Handle error
}
```

### unregisterProcess()

```c
void unregisterProcess(FirmwareInterfaceHandle handler);
```

**Usage:**
```c
unregisterProcess(handle);
handle = NULL;  // Best practice
```

## Common Patterns

### Pattern 1: Simple Plugin

```c
#include "rdkFwupdateMgr_client.h"

static FirmwareInterfaceHandle g_handle = NULL;

// Init
void init() {
    g_handle = registerProcess("SimplePlugin", "1.0");
    assert(g_handle != NULL);
}

// Use
void check_update() {
    if (g_handle) {
        checkForUpdate(g_handle, ...);
    }
}

// Cleanup
void cleanup() {
    if (g_handle) {
        unregisterProcess(g_handle);
        g_handle = NULL;
    }
}
```

### Pattern 2: Thread-Safe Plugin

```c
#include "rdkFwupdateMgr_client.h"
#include <pthread.h>

static FirmwareInterfaceHandle g_handle = NULL;
static pthread_mutex_t g_handle_mutex = PTHREAD_MUTEX_INITIALIZER;

// Thread-safe registration
void register_fw() {
    pthread_mutex_lock(&g_handle_mutex);
    if (g_handle == NULL) {
        g_handle = registerProcess("ThreadedPlugin", "1.0");
    }
    pthread_mutex_unlock(&g_handle_mutex);
}

// Thread-safe usage
void check_update_safe() {
    pthread_mutex_lock(&g_handle_mutex);
    if (g_handle) {
        checkForUpdate(g_handle, ...);
    }
    pthread_mutex_unlock(&g_handle_mutex);
}

// Thread-safe cleanup
void unregister_fw() {
    pthread_mutex_lock(&g_handle_mutex);
    if (g_handle) {
        unregisterProcess(g_handle);
        g_handle = NULL;
    }
    pthread_mutex_unlock(&g_handle_mutex);
}
```

### Pattern 3: Auto-Cleanup with atexit

```c
static FirmwareInterfaceHandle g_handle = NULL;

void cleanup_handler(void) {
    if (g_handle) {
        unregisterProcess(g_handle);
        g_handle = NULL;
    }
}

void init() {
    g_handle = registerProcess("AutoCleanup", "1.0");
    if (g_handle) {
        atexit(cleanup_handler);  // Auto cleanup on exit
    }
}
```

## Error Handling

### Registration Errors

```c
FirmwareInterfaceHandle handle = registerProcess("MyPlugin", "1.0");

if (handle == NULL) {
    // Possible causes:
    // 1. Daemon not running → Check: systemctl status rdkFwupdateMgr
    // 2. Process name already in use → Use unique name
    // 3. D-Bus permission denied → Check /etc/dbus-1/system.d/
    // 4. Invalid parameters → Check NULL/empty strings
    
    fprintf(stderr, "Registration failed!\n");
    return ERROR;
}
```

### Daemon Disconnect Handling

```c
// If daemon restarts, handle becomes invalid
// Implement reconnect logic:

FirmwareInterfaceHandle get_or_reconnect() {
    if (g_handle == NULL) {
        g_handle = registerProcess("MyPlugin", "1.0");
    }
    return g_handle;
}

void safe_check_update() {
    FirmwareInterfaceHandle h = get_or_reconnect();
    if (h) {
        checkForUpdate(h, ...);
    }
}
```

## Build Instructions

### Compiler Flags

```bash
gcc -o myplugin myplugin.c \
    -I/usr/include/rdkFwupdateMgr \
    -lrdkFwupdateMgr \
    $(pkg-config --cflags --libs glib-2.0 gio-2.0)
```

### pkg-config (if available)

```bash
gcc -o myplugin myplugin.c \
    $(pkg-config --cflags --libs rdkFwupdateMgr)
```

## Testing

### Manual Test

```c
// test_registration.c
#include "rdkFwupdateMgr_process.h"
#include <stdio.h>

int main() {
    printf("Testing registration...\n");
    
    FirmwareInterfaceHandle h = registerProcess("TestPlugin", "1.0");
    if (h == NULL) {
        printf("FAIL: Registration failed\n");
        return 1;
    }
    
    printf("PASS: Registered with handle: %s\n", h);
    
    unregisterProcess(h);
    printf("PASS: Unregistered successfully\n");
    
    return 0;
}
```

### Build & Run Test

```bash
gcc -o test_registration test_registration.c \
    -I../include -L../lib -lrdkFwupdateMgr \
    $(pkg-config --cflags --libs glib-2.0 gio-2.0)

# Ensure daemon is running
sudo systemctl start rdkFwupdateMgr.service

# Run test
./test_registration
```

## Troubleshooting

### Problem: "Failed to connect to D-Bus system bus"

**Solution:**
```bash
# Check if daemon is running
systemctl status rdkFwupdateMgr.service

# If not running, start it
sudo systemctl start rdkFwupdateMgr.service

# Check D-Bus is running
ps aux | grep dbus-daemon
```

### Problem: "Registration rejected: Process name already registered"

**Solution:**
```c
// Use unique process name (include PID if needed)
char name[256];
snprintf(name, sizeof(name), "MyPlugin_%d", getpid());
handle = registerProcess(name, "1.0");
```

### Problem: Handle becomes NULL after daemon restart

**Solution:**
```c
// Implement auto-reconnect (see pattern above)
FirmwareInterfaceHandle get_handle() {
    if (g_handle == NULL) {
        g_handle = registerProcess("MyPlugin", "1.0");
    }
    return g_handle;
}
```

## Migration Checklist

- [ ] Include rdkFwupdateMgr_process.h (or just rdkFwupdateMgr_client.h)
- [ ] Add registerProcess() call at plugin init
- [ ] Save returned handle in global variable
- [ ] Update all firmware API calls to pass handle as first parameter
- [ ] Add unregisterProcess() call at plugin cleanup
- [ ] Add NULL checks for handle
- [ ] Test registration success/failure paths
- [ ] Verify cleanup on plugin shutdown
- [ ] Test with daemon restart scenario

## Complete Example

```c
/* complete_example.c - Full plugin integration example */

#include "rdkFwupdateMgr_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// Global handle
static FirmwareInterfaceHandle g_fw_handle = NULL;

// Cleanup handler
void cleanup_firmware() {
    if (g_fw_handle != NULL) {
        printf("Cleaning up firmware registration...\n");
        unregisterProcess(g_fw_handle);
        g_fw_handle = NULL;
    }
}

// Initialize firmware client
int init_firmware() {
    printf("Initializing firmware client...\n");
    
    // Register with daemon
    g_fw_handle = registerProcess("MyPlugin", "1.0.0");
    if (g_fw_handle == NULL) {
        fprintf(stderr, "ERROR: Failed to register with firmware daemon\n");
        return -1;
    }
    
    printf("Successfully registered! Handle: %s\n", g_fw_handle);
    
    // Register cleanup handler
    atexit(cleanup_firmware);
    
    return 0;
}

// Check for firmware update
void check_for_update() {
    if (g_fw_handle == NULL) {
        fprintf(stderr, "ERROR: Not registered\n");
        return;
    }
    
    printf("Checking for firmware update...\n");
    // TODO: Call checkForUpdate(g_fw_handle, ...);
}

// Main
int main() {
    printf("=== Plugin Starting ===\n");
    
    // Initialize
    if (init_firmware() != 0) {
        return 1;
    }
    
    // Do work
    check_for_update();
    
    // Simulate plugin running
    printf("Plugin running...\n");
    sleep(2);
    
    printf("=== Plugin Exiting ===\n");
    // Cleanup handled by atexit
    
    return 0;
}
```

## Next Steps

1. **Read Full Documentation:** See `PROCESS_REGISTRATION_API.md`
2. **Review Examples:** See `examples/example_plugin_registration.c`
3. **Update Your Code:** Follow migration checklist above
4. **Test Thoroughly:** Run with and without daemon
5. **Check Logs:** Monitor daemon logs during testing

## Support

For issues or questions:
- Check daemon logs: `journalctl -u rdkFwupdateMgr -f`
- Review examples in `librdkFwupdateMgr/examples/`
- Read full API docs in header files
- Contact: [Your team's support channel]
