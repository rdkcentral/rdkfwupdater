# Quick Reference - librdkFwupdateMgr

## Build Commands
```bash
./configure && make                     # Build everything
make librdkFwupdateMgr.la               # Build library only
make example_plugin_registration        # Build example
sudo make install                       # Install library
```

## Run Example
```bash
sudo systemctl start rdkFwupdateMgr.service  # Start daemon
./example_plugin_registration                 # Run example
tail -f /opt/logs/rdkFwupdateMgr.log         # View logs
```

## API Usage
```c
#include "rdkFwupdateMgr_process.h"

// Register
FirmwareInterfaceHandle h = registerProcess("MyPlugin", "1.0");
if (h == NULL) { /* error */ }

// Use
checkForUpdate(h, ...);
downloadFirmware(h, ...);

// Cleanup
unregisterProcess(h);
h = NULL;
```

## Logging Macros
```c
FWUPMGR_INFO("Info message\n");
FWUPMGR_ERROR("Error: %s\n", msg);
FWUPMGR_DEBUG("Debug info\n");
FWUPMGR_WARN("Warning\n");
FWUPMGR_FATAL("Fatal error\n");
```

## Log File
```bash
# View logs
tail -f /opt/logs/rdkFwupdateMgr.log

# Filter library logs
tail -f /opt/logs/rdkFwupdateMgr.log | grep librdkFwupdateMgr
```

## Files
- **Library:** `.libs/librdkFwupdateMgr.so`
- **Headers:** `librdkFwupdateMgr/include/*.h`
- **Example:** `./example_plugin_registration`
- **Logs:** `/opt/logs/rdkFwupdateMgr.log`
- **Docs:** `librdkFwupdateMgr/*.md`

## Troubleshooting
```bash
# Daemon not running
sudo systemctl start rdkFwupdateMgr.service

# Check daemon status
systemctl status rdkFwupdateMgr.service

# View daemon logs
journalctl -u rdkFwupdateMgr -f

# Monitor D-Bus
dbus-monitor --system "sender='org.rdkfwupdater.Interface'"

# Library not found
export LD_LIBRARY_PATH=.libs:$LD_LIBRARY_PATH
# or
sudo ldconfig
```

## Documentation
- **Quick Start:** `librdkFwupdateMgr/QUICK_START.md`
- **API Details:** `librdkFwupdateMgr/PROCESS_REGISTRATION_API.md`
- **Build Guide:** `librdkFwupdateMgr/BUILD_AND_TEST.md`
- **Complete:** `IMPLEMENTATION_COMPLETE.md`
