# Example Programs for Testing on Device

This directory contains example programs that demonstrate how to use the rdkFwupdateMgr client library. These programs are installed to `/usr/bin` on the device for testing purposes.

---

## Installed Programs

After building and installing, the following example programs will be available on the device:

1. **example_plugin_registration** - Test plugin registration and unregistration
2. **example_checkforupdate** - Test synchronous CheckForUpdate API
3. **example_async_checkforupdate** - Test asynchronous CheckForUpdate API

---

## Prerequisites

Before running the examples, ensure:

1. **rdkFwupdateMgr daemon is running:**
   ```bash
   # Check if daemon is running
   ps aux | grep rdkFwupdateMgr
   
   # If not running, start it
   systemctl start rdkFwupdateMgr
   # OR
   /usr/bin/rdkFwupdateMgr &
   ```

2. **D-Bus session is available:**
   ```bash
   # Check D-Bus
   dbus-daemon --version
   ```

3. **Library is installed:**
   ```bash
   # Verify library exists
   ls -l /usr/lib/librdkFwupdateMgr.so*
   ```

---

## Usage Examples

### 1. Plugin Registration Test

This example demonstrates how to register and unregister a plugin with the daemon.

```bash
# Run the example
example_plugin_registration
```

**What it does:**
- Registers a plugin named "ExamplePlugin" with handler ID 100
- Keeps the plugin registered for 30 seconds
- Unregisters the plugin
- Exits

**Expected Output:**
```
=== rdkFwupdateMgr Plugin Registration Example ===

Step 1: Registering plugin 'ExamplePlugin' with handler_id=100...
✓ Plugin registered successfully!

Step 2: Plugin will remain registered for 30 seconds...
(You can trigger CheckForUpdate from daemon during this time)
...
Step 3: Unregistering plugin...
✓ Plugin unregistered successfully!

=== Example completed successfully ===
```

**Testing during registration window:**
While the plugin is registered (30 second window), you can test from another terminal:
```bash
# Trigger update check from daemon (will notify the plugin)
testClient CheckForUpdate
```

---

### 2. Synchronous CheckForUpdate Test

This example demonstrates the synchronous (blocking) CheckForUpdate API.

```bash
# Run the example
example_checkforupdate
```

**What it does:**
- Calls CheckForUpdate (blocks until daemon responds)
- Prints update information
- Exits

**Expected Output:**
```
=== rdkFwupdateMgr CheckForUpdate Example ===

Calling CheckForUpdate (synchronous)...
This will block until the daemon responds...

CheckForUpdate completed:
  Status: 0
  Update Available: Yes
  Current Version: 1.0.0
  New Version: 2.0.0
  Download URL: http://example.com/firmware.bin

=== Example completed ===
```

**Variations:**
- If no update available, "Update Available" will be "No"
- If daemon is not running, will show error after timeout

---

### 3. Asynchronous CheckForUpdate Test

This example demonstrates the asynchronous (non-blocking) CheckForUpdate API with multiple concurrent requests.

```bash
# Run the example
example_async_checkforupdate
```

**What it does:**
- Registers 3 handlers (UI, Background, Monitoring)
- Each handler calls CheckForUpdate asynchronously
- Demonstrates callback invocation
- Tests cancellation (cancels one handler)
- Waits for all callbacks to complete

**Expected Output:**
```
=== rdkFwupdateMgr Async CheckForUpdate Example ===

Initializing async subsystem...
✓ Async subsystem initialized

Scenario 1: Multiple concurrent async calls
-------------------------------------------
[UI Handler] Calling checkForUpdate_async...
[UI Handler] Request registered with callback_id: 1
[Background Handler] Calling checkForUpdate_async...
[Background Handler] Request registered with callback_id: 2
[Monitoring Handler] Calling checkForUpdate_async...
[Monitoring Handler] Request registered with callback_id: 3

Scenario 2: Cancellation test
----------------------------
Cancelling Background Handler request...
✓ Background Handler request cancelled

Waiting for callbacks to complete (max 60 seconds)...

--- Callback Invoked ---
Handler: UI Handler
Status: 0 (SUCCESS)
Message: Update check completed
Update Available: Yes
New Version: 2.0.0
Download URL: http://example.com/firmware.bin
Timestamp: 1709251200
HTTP Status: 200
Reboot Required: Yes
------------------------

--- Callback Invoked ---
Handler: Monitoring Handler
Status: 0 (SUCCESS)
Message: Update check completed
Update Available: Yes
New Version: 2.0.0
------------------------

✓ All callbacks completed
(Background Handler was cancelled and did not receive callback)

Cleanup...
✓ Example completed successfully
```

---

## Testing Scenarios

### Scenario 1: Basic Plugin Registration

Test that plugins can register and receive updates:

```bash
# Terminal 1: Register plugin and keep it running
example_plugin_registration
# (Wait, don't exit yet)

# Terminal 2: Trigger update check
testClient CheckForUpdate

# Terminal 1 should show callback notification
```

### Scenario 2: Synchronous vs Asynchronous

Compare blocking vs non-blocking behavior:

```bash
# Synchronous (blocks until complete)
time example_checkforupdate
# Note: Will block for several seconds

# Asynchronous (returns immediately)
time example_async_checkforupdate
# Note: Main thread doesn't block, callback invoked in background
```

### Scenario 3: Multiple Concurrent Requests

Test that multiple plugins can request updates simultaneously:

```bash
# Run async example (which registers 3 handlers internally)
example_async_checkforupdate

# All 3 handlers should receive callbacks when update check completes
```

### Scenario 4: Cancellation

Test that requests can be cancelled:

```bash
# Run async example (includes cancellation test)
example_async_checkforupdate

# Note: One handler is cancelled and should NOT receive callback
# Check output to verify cancelled handler didn't get invoked
```

### Scenario 5: Stress Test

Test many concurrent registrations:

```bash
# Run multiple instances
for i in {1..10}; do
    example_async_checkforupdate &
done

# Wait for all to complete
wait

# Check logs for any errors
journalctl -u rdkFwupdateMgr -n 100
```

---

## Troubleshooting

### Problem: "Failed to connect to D-Bus"

**Solution:**
```bash
# Check if D-Bus daemon is running
ps aux | grep dbus-daemon

# Check D-Bus session bus address
echo $DBUS_SESSION_BUS_ADDRESS

# If not set, start a D-Bus session
eval $(dbus-launch --sh-syntax)
```

### Problem: "rdkFwupdateMgr daemon not responding"

**Solution:**
```bash
# Check if daemon is running
ps aux | grep rdkFwupdateMgr

# Check daemon logs
journalctl -u rdkFwupdateMgr -f

# Restart daemon
systemctl restart rdkFwupdateMgr

# Or manually start
killall rdkFwupdateMgr
/usr/bin/rdkFwupdateMgr &
```

### Problem: "Library not found"

**Solution:**
```bash
# Check library installation
ls -l /usr/lib/librdkFwupdateMgr.so*

# Update library cache
ldconfig

# Check library dependencies
ldd /usr/bin/example_plugin_registration
```

### Problem: "Timeout waiting for callback"

**Possible causes:**
1. Daemon is busy or crashed
2. Network issues (daemon can't reach XConf server)
3. Callback ID was cancelled

**Solution:**
```bash
# Check daemon status
systemctl status rdkFwupdateMgr

# Check daemon logs for errors
journalctl -u rdkFwupdateMgr -n 50

# Verify network connectivity
ping xconf.example.com

# Check if daemon can reach backend
testClient CheckForUpdate
```

---

## Debugging

### Enable Debug Logging

Set log level before running examples:

```bash
# Set environment variable for verbose logging
export RDK_LOG_LEVEL=7

# Run example
example_async_checkforupdate

# Check logs
journalctl -u rdkFwupdateMgr -f
```

### Monitor D-Bus Messages

Watch D-Bus traffic to see signal emissions:

```bash
# Monitor system bus
dbus-monitor --system "interface='com.rdk.FirmwareUpdate'"

# In another terminal, run example
example_async_checkforupdate
```

### Check Memory Usage

Verify no memory leaks:

```bash
# Run under Valgrind (if available)
valgrind --leak-check=full example_async_checkforupdate

# Check process memory
while true; do
    ps aux | grep example_async
    sleep 1
done
```

---

## Building and Installing

To rebuild and install the examples:

```bash
# On build machine
cd /path/to/rdkfwupdater
./configure
make clean
make
make install DESTDIR=/path/to/rootfs

# Examples will be installed to:
# ${DESTDIR}/usr/bin/example_plugin_registration
# ${DESTDIR}/usr/bin/example_checkforupdate
# ${DESTDIR}/usr/bin/example_async_checkforupdate
```

---

## Integration Testing

For automated testing, you can use these examples in scripts:

```bash
#!/bin/bash
# test_fwupdate_integration.sh

# Start daemon
systemctl start rdkFwupdateMgr
sleep 2

# Test 1: Plugin registration
echo "Test 1: Plugin registration"
timeout 60 example_plugin_registration
if [ $? -eq 0 ]; then
    echo "✓ PASS"
else
    echo "✗ FAIL"
    exit 1
fi

# Test 2: Sync API
echo "Test 2: Synchronous CheckForUpdate"
timeout 30 example_checkforupdate
if [ $? -eq 0 ]; then
    echo "✓ PASS"
else
    echo "✗ FAIL"
    exit 1
fi

# Test 3: Async API
echo "Test 3: Asynchronous CheckForUpdate"
timeout 120 example_async_checkforupdate
if [ $? -eq 0 ]; then
    echo "✓ PASS"
else
    echo "✗ FAIL"
    exit 1
fi

echo "All tests passed!"
```

---

## Expected Installation Locations

After `make install`:

```
/usr/bin/example_plugin_registration
/usr/bin/example_checkforupdate
/usr/bin/example_async_checkforupdate
/usr/lib/librdkFwupdateMgr.so.1.0.0
/usr/lib/librdkFwupdateMgr.so.1
/usr/lib/librdkFwupdateMgr.so
/usr/include/rdkFwupdateMgr/rdkFwupdateMgr_client.h
/usr/include/rdkFwupdateMgr/rdkFwupdateMgr_process.h
```

---

## Notes

- Examples are designed to be **self-contained** and exit after demonstrating functionality
- For **long-running testing**, modify the sleep durations in the source code
- Examples use **standard output** for user-friendly messages
- Examples use **proper error handling** and return non-zero on failure
- Suitable for **automated testing** and **CI/CD integration**

---

## Further Reading

- [ASYNC_API_QUICK_REFERENCE.md](../ASYNC_API_QUICK_REFERENCE.md) - API documentation
- [IMPLEMENTATION_SUMMARY.md](../IMPLEMENTATION_SUMMARY.md) - Architecture overview
- [ASYNC_MEMORY_MANAGEMENT.md](../ASYNC_MEMORY_MANAGEMENT.md) - Memory management guide

---

**Last Updated:** 2026-02-26  
**Maintainer:** RDK Firmware Update Team
