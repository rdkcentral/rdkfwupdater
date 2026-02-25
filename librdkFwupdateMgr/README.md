# librdkFwupdateMgr - Firmware Update Client Library

## Overview

`librdkFwupdateMgr` is a client library that allows plugin applications to communicate with the `rdkFwupdateMgr` daemon for firmware update operations via D-Bus.

## Features

✅ **Process Registration** - Register/unregister with firmware daemon  
✅ **Firmware Check** - Query for available firmware updates  
✅ **FWUPMGR Logging** - Integrated logging to `/opt/logs/rdkFwupdateMgr.log`  
✅ **Thread-Safe** - Safe to use from multiple threads  
✅ **Clean API** - Simple, opaque handle-based design  
✅ **Production-Ready** - Comprehensive error handling and validation  

## Quick Start

### 1. Build the Library

```bash
cd /path/to/rdkfwupdater
autoreconf -if
./configure
make
sudo make install
```

### 2. Link Against the Library

```bash
# Compile your application
gcc -o myapp myapp.c \
    -I/usr/include/rdkFwupdateMgr \
    -lrdkFwupdateMgr \
    $(pkg-config --cflags --libs glib-2.0 gio-2.0)
```

### 3. Use the API

```c
#include <rdkFwupdateMgr/rdkFwupdateMgr_client.h>

void on_update_check(const FwInfoData *fwinfo) {
    if (fwinfo->status == FIRMWARE_AVAILABLE) {
        printf("New firmware: %s\n", fwinfo->UpdateDetails->FwVersion);
    }
}

int main() {
    // Register
    FirmwareInterfaceHandle handle = registerProcess("MyApp", "1.0");
    if (!handle) {
        return 1;
    }

    // Check for updates
    if (checkForUpdate(handle, on_update_check) == CHECK_FOR_UPDATE_SUCCESS) {
        // Callback will be invoked with results
    }

    // Cleanup
    unregisterProcess(handle);
    return 0;
}
```

## API Reference

### Registration APIs

#### `registerProcess()`
Register your application with the firmware daemon.

```c
FirmwareInterfaceHandle registerProcess(
    const char *processName,  // Your app name
    const char *libVersion    // Your app version
);

// Returns: Handle on success, NULL on failure
```

#### `unregisterProcess()`
Disconnect from the firmware daemon.

```c
void unregisterProcess(FirmwareInterfaceHandle handler);
```

### Firmware Update APIs

#### `checkForUpdate()`
Check if firmware updates are available.

```c
CheckForUpdateResult checkForUpdate(
    FirmwareInterfaceHandle handle,
    UpdateEventCallback callback
);

// Returns: CHECK_FOR_UPDATE_SUCCESS or CHECK_FOR_UPDATE_FAIL
```

**Callback Signature**:
```c
void callback(const FwInfoData *fwinfodata);
```

**Status Codes** (in `FwInfoData.status`):
- `FIRMWARE_AVAILABLE` (0) - Update available
- `FIRMWARE_NOT_AVAILABLE` (1) - Already on latest
- `UPDATE_NOT_ALLOWED` (2) - Not compatible with device
- `FIRMWARE_CHECK_ERROR` (3) - Error during check
- `IGNORE_OPTOUT` (4) - Blocked by opt-out
- `BYPASS_OPTOUT` (5) - Requires user consent

#### `downloadFirmware()` *(Coming Soon)*
Download firmware image.

```c
DownloadResult downloadFirmware(
    FirmwareInterfaceHandle handle,
    const FwDwnlReq *fwdwnlreq,
    DownloadCallback callback
);
```

#### `updateFirmware()` *(Coming Soon)*
Flash firmware to device.

```c
UpdateResult updateFirmware(
    FirmwareInterfaceHandle handle,
    const FwUpdateReq *fwupdatereq,
    UpdateCallback callback
);
```

## Examples

### Example 1: Process Registration

See [`librdkFwupdateMgr/examples/example_plugin_registration.c`](librdkFwupdateMgr/examples/example_plugin_registration.c)

```bash
./example_plugin_registration
```

### Example 2: Check for Updates

See [`librdkFwupdateMgr/examples/example_checkforupdate.c`](librdkFwupdateMgr/examples/example_checkforupdate.c)

```bash
./example_checkforupdate
```

## Documentation

| Document | Description |
|----------|-------------|
| [QUICK_REFERENCE.md](QUICK_REFERENCE.md) | Quick API reference |
| [CHECK_FOR_UPDATE_API.md](librdkFwupdateMgr/CHECK_FOR_UPDATE_API.md) | checkForUpdate() details |
| [PROCESS_REGISTRATION_API.md](librdkFwupdateMgr/PROCESS_REGISTRATION_API.md) | Registration API details |
| [BUILD_CHECKFORUPDATE.md](BUILD_CHECKFORUPDATE.md) | Build and test guide |
| [CHECKFORUPDATE_IMPLEMENTATION_SUMMARY.md](CHECKFORUPDATE_IMPLEMENTATION_SUMMARY.md) | Implementation details |

## Logging

All library operations log to `/opt/logs/rdkFwupdateMgr.log` using `FWUPMGR_*` macros:

```c
FWUPMGR_INFO("Info message\n");
FWUPMGR_ERROR("Error: %s\n", error_msg);
FWUPMGR_DEBUG("Debug info\n");
FWUPMGR_WARN("Warning\n");
```

View logs:
```bash
tail -f /opt/logs/rdkFwupdateMgr.log | grep librdkFwupdateMgr
```

## Requirements

### Build Dependencies
- GLib 2.0 (`libglib2.0-dev`)
- GIO 2.0 (`libgio-2.0-dev`)
- D-Bus (`libdbus-1-dev`)
- pthread

### Runtime Requirements
- `rdkFwupdateMgr` daemon running
- D-Bus system bus access

## Troubleshooting

### Issue: Registration Failed
```
ERROR: Registration failed!
```

**Solutions**:
1. Check daemon is running: `systemctl status rdkFwupdateMgr`
2. Start daemon: `sudo systemctl start rdkFwupdateMgr`
3. Check D-Bus permissions

### Issue: Library Not Found
```
error while loading shared libraries: librdkFwupdateMgr.so
```

**Solutions**:
```bash
export LD_LIBRARY_PATH=/usr/lib:$LD_LIBRARY_PATH
# or
sudo ldconfig
```

### Issue: checkForUpdate() Fails
```
ERROR: checkForUpdate() failed!
```

**Solutions**:
1. Verify handle is valid (non-NULL)
2. Check callback is non-NULL
3. Verify daemon is running
4. Check logs: `tail -f /opt/logs/rdkFwupdateMgr.log`

## Architecture

```
┌────────────────────────────────────────┐
│      Your Plugin Application           │
│  (VideoPlayer, AudioService, etc.)     │
└─────────────────┬──────────────────────┘
                  │
                  │ registerProcess()
                  │ checkForUpdate()
                  │ downloadFirmware()
                  │ updateFirmware()
                  │
┌─────────────────▼──────────────────────┐
│      librdkFwupdateMgr.so              │
│   (Client Library - This Component)    │
└─────────────────┬──────────────────────┘
                  │
                  │ D-Bus IPC
                  │ (org.rdkfwupdater.Interface)
                  │
┌─────────────────▼──────────────────────┐
│      rdkFwupdateMgr Daemon             │
│   (Firmware Update Service)            │
│   - XConf integration                  │
│   - Firmware validation                │
│   - Download/flash management          │
└────────────────────────────────────────┘
```

## Thread Safety

- ✅ **registerProcess()** - Call once per process
- ✅ **checkForUpdate()** - Thread-safe (GDBus handles locking)
- ✅ **Callbacks** - May be invoked from library thread
- ⚠️ **Don't** call APIs from inside callbacks

## Memory Management

| What | Who Owns It | When to Free |
|------|-------------|--------------|
| `FirmwareInterfaceHandle` | Library | Automatically on unregister |
| Callback `FwInfoData*` | Library | Valid only during callback |
| Callback `UpdateDetails*` | Library | Valid only during callback |

**Rule**: Never call `free()` on any pointers returned by the library.

## Build Targets

```bash
make                                    # Build everything
make librdkFwupdateMgr.la              # Build library only
make example_plugin_registration       # Build registration example
make example_checkforupdate            # Build checkForUpdate example
make install                           # Install library and headers
```

## Installation Paths

```
/usr/lib/librdkFwupdateMgr.so           # Shared library
/usr/include/rdkFwupdateMgr/            # Public headers
  ├── rdkFwupdateMgr_process.h          # Registration API
  └── rdkFwupdateMgr_client.h           # Firmware update APIs
/opt/logs/rdkFwupdateMgr.log            # Log file
```

## Version Information

- **Current Version**: 1.0.0
- **ABI Version**: 1:0:0
- **API Stability**: Public APIs are stable

## License

```
Copyright 2025 Comcast Cable Communications Management, LLC

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

SPDX-License-Identifier: Apache-2.0
```

## Support

For issues or questions:
1. Check documentation in `librdkFwupdateMgr/*.md`
2. Review example programs in `librdkFwupdateMgr/examples/`
3. Check logs: `/opt/logs/rdkFwupdateMgr.log`
4. Monitor D-Bus: `dbus-monitor --system "sender='org.rdkfwupdater.Interface'"`

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.
