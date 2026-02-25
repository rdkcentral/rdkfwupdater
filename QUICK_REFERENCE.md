# Quick Reference - librdkFwupdateMgr

## Build Commands
```bash
./configure && make                     # Build everything
make librdkFwupdateMgr.la               # Build library only
make example_plugin_registration        # Build registration example
make example_checkforupdate             # Build checkForUpdate example
sudo make install                       # Install library
```

## Run Examples
```bash
# Example 1: Process registration
sudo systemctl start rdkFwupdateMgr.service  # Start daemon
./example_plugin_registration                 # Run registration example
tail -f /opt/logs/rdkFwupdateMgr.log         # View logs

# Example 2: Check for updates
./example_checkforupdate                      # Run checkForUpdate example
```

## API Usage

### Registration
```c
#include "rdkFwupdateMgr_process.h"

// Register
FirmwareInterfaceHandle h = registerProcess("MyPlugin", "1.0");
if (h == NULL) { /* error */ }

// Cleanup
unregisterProcess(h);
h = NULL;
```

### Check for Updates
```c
#include "rdkFwupdateMgr_client.h"

// Callback function
void on_update(const FwInfoData *fwinfo) {
    printf("Status: %d\n", fwinfo->status);
    if (fwinfo->status == FIRMWARE_AVAILABLE) {
        printf("New version: %s\n", fwinfo->UpdateDetails->FwVersion);
    }
}

// Check for updates
CheckForUpdateResult result = checkForUpdate(h, on_update);
if (result != CHECK_FOR_UPDATE_SUCCESS) { /* error */ }
```

### Download & Update (Coming Soon)
```c
// Download
FwDwnlReq req = {...};
downloadFirmware(h, &req, download_callback);

// Flash
FwUpdateReq req = {...};
updateFirmware(h, &req, update_callback);
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
