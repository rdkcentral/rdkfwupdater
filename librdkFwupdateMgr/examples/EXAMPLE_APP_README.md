# Example Application - Complete Firmware Update Workflow

## Overview

The `example_app.c` demonstrates a **complete one-shot firmware update workflow** using the rdkFwupdateMgr client library. When you run this binary, it executes all steps from checking for updates to flashing firmware automatically.

## What It Does

This example performs the following sequence:

1. **Register** with the firmware daemon
2. **Check** for available firmware updates (async - waits for callback)
3. **Download** firmware if update is available (async - shows progress)
4. **Flash** firmware to device (async - shows progress)
5. **Unregister** and exit cleanly

## Features

✅ **One-Shot Execution**: Run once, handles complete workflow  
✅ **Async API Usage**: Properly waits for callbacks from all async APIs  
✅ **Progress Bars**: Visual progress indicators for download and flash  
✅ **Error Handling**: Comprehensive error checking at every step  
✅ **Timeout Management**: Appropriate timeouts for each phase  
✅ **Clean Cleanup**: Proper unregistration even on errors  

## Build

```bash
cd librdkFwupdateMgr/examples

gcc example_app.c \
    -o example_app \
    -I../include \
    -L../build \
    -lrdkFwupdateMgr \
    $(pkg-config --cflags --libs gio-2.0) \
    -lpthread

# Or use the Makefile if available
make example_app
```

## Run

```bash
# Ensure daemon is running
systemctl status rdkFwupdateMgr.service

# Run the example
./example_app
```

## Expected Output

### When Firmware Update is Available

```
╔═══════════════════════════════════════════════════════╗
║   RDK Firmware Update Manager - Complete Workflow    ║
╚═══════════════════════════════════════════════════════╝

┌─────────────────────────────────────────────────────┐
│ STEP 1: Register with firmware daemon              │
└─────────────────────────────────────────────────────┘
  Process Name : ExampleApp
  Lib Version  : 1.0.0

  ✓ Registered successfully
    Handle: '12345'

┌─────────────────────────────────────────────────────┐
│ STEP 2: Check for firmware updates                 │
└─────────────────────────────────────────────────────┘
  Calling checkForUpdate()...
  (API returns immediately; callback fires when XConf query completes)

  ✓ checkForUpdate() returned SUCCESS
    (Daemon ACK received - waiting for actual firmware data...)

  Waiting for firmware check callback

┌──────────────────────────────────────────────────────┐
│  ✓ checkForUpdate Callback Received                 │
└──────────────────────────────────────────────────────┘
  Handle              : 12345
  Status              : FIRMWARE_AVAILABLE (0)
  Update Available    : YES
  Current Version     : 1.0.0
  Available Version   : 2.0.0
  Status Message      : Firmware update available

  → Firmware check data saved. Main thread will proceed.
└──────────────────────────────────────────────────────┘

  ✓ Firmware update available!
    Current Version  : 1.0.0
    Available Version: 2.0.0
    → Proceeding to download...

┌─────────────────────────────────────────────────────┐
│ STEP 3: Download firmware image                    │
└─────────────────────────────────────────────────────┘
  Firmware Name : firmware_2.0.0.bin
  Download URL  : (use XConf URL)
  Firmware Type : PCI

  Calling downloadFirmware()...

  ✓ downloadFirmware() returned SUCCESS
    Waiting for download progress...

  Download Progress:
    [████░░░░░░░░░░░░░░░░]  20%  DWNL_IN_PROGRESS
    [████████░░░░░░░░░░░░]  40%  DWNL_IN_PROGRESS
    [████████████░░░░░░░░]  60%  DWNL_IN_PROGRESS
    [████████████████░░░░]  80%  DWNL_IN_PROGRESS
    [████████████████████] 100%  DWNL_COMPLETED

    ✓ Download completed successfully!

  → Download complete. Proceeding to flash...

┌─────────────────────────────────────────────────────┐
│ STEP 4: Flash firmware to device                   │
└─────────────────────────────────────────────────────┘
  Firmware Name  : firmware_2.0.0.bin
  Firmware Type  : PCI
  Location       : (use daemon default)
  Reboot Now     : false

  Calling updateFirmware()...

  ✓ updateFirmware() returned SUCCESS
    Waiting for flash progress...

  Flash Progress:
    [▓▓▓▓░░░░░░░░░░░░░░░░]  20%  UPDATE_IN_PROGRESS
    [▓▓▓▓▓▓▓▓░░░░░░░░░░░░]  40%  UPDATE_IN_PROGRESS
    [▓▓▓▓▓▓▓▓▓▓▓▓░░░░░░░░]  60%  UPDATE_IN_PROGRESS
    [▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓░░░░]  80%  UPDATE_IN_PROGRESS
    [▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓] 100%  UPDATE_COMPLETED

    ✓ Firmware flash completed successfully!

  → Flash complete!

┌─────────────────────────────────────────────────────┐
│ STEP 5: Unregister from daemon                     │
└─────────────────────────────────────────────────────┘
  Calling unregisterProcess()...
  ✓ Unregistered successfully

╔═══════════════════════════════════════════════════════╗
║   ✓ FIRMWARE UPDATE WORKFLOW COMPLETED               ║
╚═══════════════════════════════════════════════════════╝

  ⚠ NOTE: Firmware flashed successfully.
          System reboot required to activate new firmware.
          Use: systemctl reboot
```

### When No Update is Available

```
╔═══════════════════════════════════════════════════════╗
║   RDK Firmware Update Manager - Complete Workflow    ║
╚═══════════════════════════════════════════════════════╝

[... registration and check steps ...]

┌──────────────────────────────────────────────────────┐
│  ✓ checkForUpdate Callback Received                 │
└──────────────────────────────────────────────────────┘
  Handle              : 12345
  Status              : FIRMWARE_NOT_AVAILABLE (1)
  Update Available    : NO
  Current Version     : 2.0.0
  Available Version   : 2.0.0

  ⚠ No firmware update available
    Status: 1
    Current Version: 2.0.0
    → Already on latest version. No action needed.

[... unregister and exit ...]
```

## Code Structure

### Global State
- **Synchronization**: Mutexes and condition variables for async callbacks
- **Firmware Info**: Stores version strings from checkForUpdate callback
- **Progress Tracking**: Flags for download and flash completion

### Callbacks

1. **`on_firmware_check_callback()`**
   - Fired when XConf query completes
   - Copies firmware version data
   - Wakes main thread to proceed

2. **`on_download_progress_callback()`**
   - Fired repeatedly during download
   - Displays progress bar
   - Signals completion/error

3. **`on_update_progress_callback()`**
   - Fired repeatedly during flash
   - Displays progress bar
   - Signals completion/error

### Main Function

Executes the complete workflow:
- Register → Check → Wait → Download → Wait → Flash → Wait → Unregister

## Timeout Configuration

| Phase | Timeout | Adjustable? | Reason |
|-------|---------|-------------|--------|
| checkForUpdate callback | 120s (2 min) | Yes | XConf query time varies |
| downloadFirmware completion | 300s (5 min) | Yes | Depends on firmware size/network |
| updateFirmware completion | 600s (10 min) | Yes | Depends on flash speed |

**To adjust timeouts**, modify these lines in `main()`:

```c
timeout.tv_sec += 120;  // checkForUpdate timeout
timeout.tv_sec += 300;  // downloadFirmware timeout
timeout.tv_sec += 600;  // updateFirmware timeout
```

## Error Handling

The example handles all common error scenarios:

- ❌ Daemon not running → Exit with error message
- ❌ API call fails → Jump to cleanup and unregister
- ❌ Callback timeout → Exit with timeout message
- ❌ No update available → Clean exit (not an error)
- ❌ Download/flash fails → Detected in callback, exit with error

All errors perform cleanup via `cleanup_unregister` label:
- Calls `unregisterProcess()` if handle is valid
- Sets exit code appropriately
- Prints helpful error messages

## Customization

### To Use Actual Firmware from XConf

The example derives firmware name from version, but you may want to:

1. **Store more data from callback**: Modify `on_firmware_check_callback()` to save more fields from `event_data`
2. **Use real firmware name**: If daemon provides it, use that instead of constructing from version
3. **Handle multiple firmware types**: Check for PDRI, peripheral firmware, etc.

### To Enable Immediate Reboot

Change this line in STEP 4:

```c
update_req.rebootImmediately = true;  // Device reboots after flash
```

⚠️ **Warning**: If `rebootImmediately = true`, the device will reboot immediately after flashing. The example will NOT reach the unregister step!

### To Download from Custom URL

Override the download URL:

```c
strncpy(download_req.downloadUrl, "https://my-server.com/firmware.bin",
        sizeof(download_req.downloadUrl) - 1);
```

## Troubleshooting

### Daemon Not Running

```
[ERROR] registerProcess() failed!
        Ensure rdkFwupdateMgr daemon is running:
        systemctl status rdkFwupdateMgr.service
```

**Solution**:
```bash
systemctl start rdkFwupdateMgr.service
systemctl enable rdkFwupdateMgr.service  # Auto-start on boot
```

### Callback Timeout

```
[ERROR] Timeout waiting for checkForUpdate callback (120s)
        XConf query may be taking longer than expected.
```

**Solutions**:
- Increase timeout in code
- Check network connectivity
- Check daemon logs: `tail -f /opt/logs/rdkFwupdateMgr.log`
- Verify XConf server is reachable

### D-Bus Errors

```
[ERROR] checkForUpdate() returned FAIL!
        Possible reasons:
          - D-Bus connection error
          - Daemon not responding
          - Invalid handle
```

**Solutions**:
```bash
# Check D-Bus service
systemctl status dbus

# Monitor D-Bus traffic
dbus-monitor --system "interface='org.rdkfwupdater.Interface'"

# Check daemon logs
journalctl -u rdkFwupdateMgr.service -f
```

## API Flow Diagram

See the comprehensive sequence diagram at the end of `example_app.c` showing:
- Main thread execution
- Library background thread callbacks
- Daemon D-Bus signals
- Timing and synchronization points

## Learn More

- **API Reference**: `../CHECK_FOR_UPDATE_API.md`
- **Quick Start**: `../QUICK_START.md`
- **Design Review**: `../CHECKFORUPDATE_DESIGN_REVIEW.md`
- **Build Instructions**: `../BUILD_AND_TEST.md`

## Notes

1. **One-Shot Design**: This example is designed as a one-shot binary. For production services/plugins, you would integrate this workflow into your event loop.

2. **Callback Threading**: All callbacks run in the library's background thread, NOT the main thread. This example uses mutexes/condvars to synchronize properly.

3. **Data Lifetime**: Data passed to callbacks is only valid during the callback. The example copies necessary data to global state.

4. **Production Use**: For production code:
   - Add retry logic for transient failures
   - Implement exponential backoff
   - Add logging to syslog
   - Handle edge cases (power loss during flash, etc.)
   - Consider user notifications

---

**Version**: 1.0  
**Last Updated**: March 2026  
**Status**: Complete working example
