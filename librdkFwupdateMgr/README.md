# librdkFwupdateMgr - RDK Firmware Update Manager Client Library

## Overview

A production-grade C client library for communicating with the RDK Firmware Updater daemon via D-Bus. This library provides a simple, thread-safe API for applications to register with the firmware update service and manage firmware updates.

## Features

- **Thread-safe**: All APIs are thread-safe with internal mutex protection
- **Memory-safe**: Careful memory management with validation
- **Async callbacks**: Non-blocking operations with progress notifications
- **Handle-based**: Opaque handle design prevents client misuse
- **Versioned**: Semantic versioning with SONAME support

## Project Structure

```
librdkFwupdateMgr/
├── include/
│   └── rdkFwupdateMgr_client.h    # Public API header
├── src/
│   ├── handle_mgr.h                # Internal handle management (private)
│   └── handle_mgr.c                # Handle implementation
├── test/
│   └── test_handle.c               # Test program for handle management
├── build/                          # Build artifacts (generated)
├── lib/                            # Compiled shared library (generated)
├── Makefile                        # Build system
└── README.md                       # This file
```

## Building

### Prerequisites

- GCC compiler
- pthread library
- make

### Build Commands

```bash
# Build the shared library
make

# Build and run tests
make test

# Install to system (requires sudo)
sudo make install

# Clean build artifacts
make clean

# Show help
make help
```

### Build Outputs

After building, you will find:
- `lib/librdkFwupdateMgr.so.1.0.0` - Versioned shared library
- `lib/librdkFwupdateMgr.so.1` - SONAME symlink
- `lib/librdkFwupdateMgr.so` - Development symlink
- `build/test_handle` - Test executable

## API Reference

### Core APIs

1. **registerProcess()** - Register with the firmware update daemon
   ```c
   FirmwareInterfaceHandle registerProcess(const char *processName, 
                                           const char *libVersion);
   ```

2. **unregisterProcess()** - Unregister and cleanup
   ```c
   void unregisterProcess(FirmwareInterfaceHandle handle);
   ```

3. **checkForUpdate()** - Check if firmware update is available
   ```c
   CheckForUpdateResult checkForUpdate(FirmwareInterfaceHandle handle,
                                       UpdateEventCallback callback,
                                       void *userData);
   ```

4. **downloadFirmware()** - Download firmware image
   ```c
   DownloadResult downloadFirmware(FirmwareInterfaceHandle handle,
                                   const FwDwnlReq *request,
                                   DownloadCallback callback,
                                   void *userData);
   ```

5. **updateFirmware()** - Flash firmware and optionally reboot
   ```c
   UpdateResult updateFirmware(FirmwareInterfaceHandle handle,
                               const FwUpdateReq *request,
                               UpdateCallback callback,
                               void *userData);
   ```

### Example Usage

```c
#include "rdkFwupdateMgr_client.h"

void update_callback(const FwInfoData *info, void *userData) {
    printf("Firmware version: %s\n", info->version);
}

int main() {
    // Register
    FirmwareInterfaceHandle handle = registerProcess("MyApp", "1.0.0");
    if (!handle) {
        fprintf(stderr, "Failed to register\n");
        return 1;
    }
    
    // Check for updates
    CheckForUpdateResult result = checkForUpdate(handle, update_callback, NULL);
    if (result == CHECK_FOR_UPDATE_SUCCESS) {
        printf("Check succeeded\n");
    }
    
    // Unregister
    unregisterProcess(handle);
    return 0;
}
```

### Linking

```bash
# Compile with library
gcc myapp.c -o myapp -I./include -L./lib -lrdkFwupdateMgr -lpthread

# Run with library path
LD_LIBRARY_PATH=./lib ./myapp
```

## Current Implementation Status

### ✅ Completed (SUBTASK-1: Project Setup & Handle Management)

- [x] Directory structure created
- [x] Public API header (`rdkFwupdateMgr_client.h`)
- [x] Internal handle management (`handle_mgr.h`, `handle_mgr.c`)
- [x] Handle creation/destruction with magic number validation
- [x] Thread-safe handle operations with mutex
- [x] String memory management (strdup/free)
- [x] Makefile for building shared library
- [x] Test program (`test_handle.c`) with 10 test cases

### 🚧 In Progress

- [ ] D-Bus integration (SUBTASK-2)
- [ ] API implementation (SUBTASK-3-7)
- [ ] Client example program (SUBTASK-8)
- [ ] Memory validation with Valgrind (SUBTASK-9)
- [ ] API documentation (SUBTASK-10)

## Testing

The test suite validates:
- Valid handle creation
- NULL parameter handling
- Handle validation (magic number)
- Handle destruction
- Use-after-free detection
- Multiple handle instances
- Corrupted handle detection

Run tests with:
```bash
make test
```

Expected output:
```
========================================
  RDK FW Update Manager - Handle Tests
========================================

[PASS] Create valid handle
[PASS] Create handle with NULL processName (should fail)
[PASS] Create handle with NULL libVersion (should fail)
...

========================================
  Test Summary
========================================
  Total:  10
  Passed: 10
  Failed: 0
========================================
```

## Memory Safety

The library follows these memory safety principles:
- All allocated memory is tracked and freed
- Use-after-free is detected via magic number validation
- Double-free is prevented by NULL checks
- String copies use `strdup()` to avoid buffer overflows
- Mutex is properly initialized and destroyed

## Thread Safety

- Each handle has its own mutex
- All handle operations are protected
- Callbacks are invoked with handle lock released (to avoid deadlock)
- Client must not call library APIs from within callbacks

## Future Enhancements

- [ ] D-Bus connection pooling
- [ ] Retry logic for daemon communication
- [ ] Async API variants (non-blocking)
- [ ] Event loop integration (GLib/libuv)
- [ ] Python bindings (via ctypes/CFFI)

## License

[Insert license information]

## Contact

[Insert contact information]

## References

- RDK Firmware Update Manager D-Bus specification
- RDK documentation: [URL]
- JIRA: [Project link]
