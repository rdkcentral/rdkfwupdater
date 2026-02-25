# Building and Testing librdkFwupdateMgr with CheckForUpdate API

## Quick Start

```bash
# 1. Configure and build
cd /path/to/rdkfwupdater
autoreconf -if
./configure
make

# 2. Run examples
export LD_LIBRARY_PATH=.libs:$LD_LIBRARY_PATH
sudo systemctl start rdkFwupdateMgr.service
./example_plugin_registration
./example_checkforupdate

# 3. View logs
tail -f /opt/logs/rdkFwupdateMgr.log
```

## Detailed Build Steps

### Prerequisites

```bash
# Install dependencies
sudo apt-get install build-essential autoconf automake libtool pkg-config
sudo apt-get install libglib2.0-dev libgio-2.0-dev libdbus-1-dev
```

### Build Process

```bash
# 1. Generate build system
autoreconf -if

# 2. Configure
./configure \
    --prefix=/usr \
    --sysconfdir=/etc \
    --localstatedir=/var

# 3. Build library and examples
make

# 4. Install (optional)
sudo make install
sudo ldconfig
```

### Build Artifacts

After successful build:

```
.libs/
├── librdkFwupdateMgr.so        # Shared library
├── librdkFwupdateMgr.so.1      # Library symlink
└── librdkFwupdateMgr.so.1.0.0  # Actual library file

./
├── example_plugin_registration  # Registration example
└── example_checkforupdate       # CheckForUpdate example

rdkFwupdateMgr                   # Daemon binary
```

## Running Examples

### Example 1: Process Registration

```bash
# Start daemon
sudo systemctl start rdkFwupdateMgr.service

# Run example
export LD_LIBRARY_PATH=.libs:$LD_LIBRARY_PATH
./example_plugin_registration

# Expected output:
# [Plugin] Registering with firmware daemon...
# [Plugin] Registration successful! Handle: 12345
# [Plugin] Unregistering...
# [Plugin] Cleanup complete
```

### Example 2: CheckForUpdate

```bash
# Ensure daemon is running
sudo systemctl status rdkFwupdateMgr.service

# Run example
export LD_LIBRARY_PATH=.libs:$LD_LIBRARY_PATH
./example_checkforupdate

# Expected output:
# [Example] Step 1: Registering with firmware daemon...
# [Example] Registration successful! Handle: 67890
# [Example] Step 2: Checking for firmware updates...
# [Example] checkForUpdate() returned: SUCCESS
# [UpdateCallback] Firmware update check completed!
# [UpdateCallback]   Current Version: X1-SCXI11AIS-2023.01.01
# [UpdateCallback]   Status: FIRMWARE_AVAILABLE (0)
# [UpdateCallback]   Available Version: X1-SCXI11AIS-2023.02.01
# [Example] Step 3: Unregistering from daemon...
# [Example] Cleanup complete
```

## Testing CheckForUpdate API

### Manual Test Procedure

1. **Start the daemon**
   ```bash
   sudo systemctl start rdkFwupdateMgr.service
   sudo systemctl status rdkFwupdateMgr.service
   ```

2. **Run checkForUpdate example**
   ```bash
   export LD_LIBRARY_PATH=.libs:$LD_LIBRARY_PATH
   ./example_checkforupdate
   ```

3. **Verify logs**
   ```bash
   tail -f /opt/logs/rdkFwupdateMgr.log
   ```

   Expected log entries:
   ```
   [librdkFwupdateMgr] checkForUpdate() called
   [librdkFwupdateMgr] checkForUpdate: handle=67890
   [librdkFwupdateMgr] checkForUpdate: Calling D-Bus method CheckForUpdate
   [rdkFwupdateMgr] CheckForUpdate: handler=67890
   [rdkFwupdateMgr] XConf call completed with result: ret=0
   [librdkFwupdateMgr] checkForUpdate: D-Bus response received
   [librdkFwupdateMgr] checkForUpdate: Invoking callback with status=0
   [librdkFwupdateMgr] checkForUpdate: Completed successfully
   ```

### Test Cases

#### Test Case 1: Successful Update Available
```bash
# Precondition: XConf server has newer firmware
./example_checkforupdate

# Expected:
# - Status: FIRMWARE_AVAILABLE (0)
# - Available version different from current
# - UpdateDetails populated with URL, filename, etc.
```

#### Test Case 2: Already on Latest Firmware
```bash
# Precondition: Device on latest firmware
./example_checkforupdate

# Expected:
# - Status: FIRMWARE_NOT_AVAILABLE (1)
# - Available version same as current
# - UpdateDetails may be empty
```

#### Test Case 3: Update Not Allowed
```bash
# Precondition: Firmware not compatible with device model
./example_checkforupdate

# Expected:
# - Status: UPDATE_NOT_ALLOWED (2)
# - Error message in logs about validation failure
```

#### Test Case 4: XConf Fetch Error
```bash
# Precondition: XConf server unreachable or cache empty
./example_checkforupdate

# Expected:
# - Status: FIRMWARE_CHECK_ERROR (3)
# - Error details in daemon logs
```

### Automated Testing

```bash
# Create test script
cat > test_checkforupdate.sh << 'EOF'
#!/bin/bash

echo "Starting checkForUpdate API tests..."

# Test 1: Basic registration and check
echo "Test 1: Basic checkForUpdate..."
./example_checkforupdate > /tmp/test1.log 2>&1
if grep -q "checkForUpdate() returned: SUCCESS" /tmp/test1.log; then
    echo "✓ Test 1 PASSED"
else
    echo "✗ Test 1 FAILED"
    cat /tmp/test1.log
fi

# Test 2: Multiple sequential checks
echo "Test 2: Multiple checks..."
./example_checkforupdate > /dev/null 2>&1 &
sleep 1
./example_checkforupdate > /tmp/test2.log 2>&1
if grep -q "Registration successful" /tmp/test2.log; then
    echo "✓ Test 2 PASSED"
else
    echo "✗ Test 2 FAILED"
fi

wait
echo "All tests completed"
EOF

chmod +x test_checkforupdate.sh
./test_checkforupdate.sh
```

## Debugging

### Enable Debug Logging

```bash
# Set log level in daemon
echo "DEBUG" > /tmp/rdkfwupdater_loglevel

# Restart daemon
sudo systemctl restart rdkFwupdateMgr.service

# Run example with verbose output
./example_checkforupdate
```

### Common Issues

#### Issue 1: Daemon Not Running
```bash
# Symptom
[Example] ERROR: Registration failed!

# Solution
sudo systemctl start rdkFwupdateMgr.service
sudo systemctl enable rdkFwupdateMgr.service
```

#### Issue 2: Library Not Found
```bash
# Symptom
./example_checkforupdate: error while loading shared libraries

# Solution
export LD_LIBRARY_PATH=.libs:$LD_LIBRARY_PATH
# Or install library
sudo make install
sudo ldconfig
```

#### Issue 3: D-Bus Permission Denied
```bash
# Symptom
Failed to connect to D-Bus system bus: Access denied

# Solution
# Check D-Bus policy in /etc/dbus-1/system.d/
sudo cat /etc/dbus-1/system.d/rdkfwupdater.conf
```

#### Issue 4: Callback Not Invoked
```bash
# Symptom
[Example] WARNING: Callback not received

# Possible causes:
# 1. Daemon processing async (XConf fetch in progress)
# 2. D-Bus signal not received
# 3. Callback registration failed

# Debug:
tail -f /opt/logs/rdkFwupdateMgr.log | grep "CheckForUpdate"
dbus-monitor --system "sender='org.rdkfwupdater.Interface'"
```

### D-Bus Monitoring

```bash
# Monitor all D-Bus traffic for firmware daemon
dbus-monitor --system "sender='org.rdkfwupdater.Interface'" &

# Run example
./example_checkforupdate

# Expected D-Bus messages:
# method call sender=:1.123 destination=org.rdkfwupdater.Interface
#   path=/org/rdkfwupdater/Service interface=org.rdkfwupdater.Interface
#   member=RegisterProcess
# method return sender=:1.42 destination=:1.123
#   uint64 12345
# method call member=CheckForUpdate
#   string "12345"
# method return
#   int32 0  string "current_version"  string "avail_version" ...
```

## Performance Testing

```bash
# Measure checkForUpdate latency
cat > perf_test.sh << 'EOF'
#!/bin/bash

iterations=10
total_time=0

for i in $(seq 1 $iterations); do
    start=$(date +%s%N)
    ./example_checkforupdate > /dev/null 2>&1
    end=$(date +%s%N)
    elapsed=$((($end - $start) / 1000000))  # Convert to milliseconds
    echo "Iteration $i: ${elapsed}ms"
    total_time=$(($total_time + $elapsed))
done

avg_time=$(($total_time / $iterations))
echo "Average time: ${avg_time}ms"
EOF

chmod +x perf_test.sh
./perf_test.sh
```

## Integration Testing

### Test with Real Plugin

```c
// my_plugin.c - Example plugin integration
#include "rdkFwupdateMgr_client.h"
#include <stdio.h>

static FirmwareInterfaceHandle g_fw_handle = NULL;

void on_firmware_available(const FwInfoData *fwinfo) {
    if (fwinfo->status == FIRMWARE_AVAILABLE) {
        printf("New firmware: %s\n", fwinfo->UpdateDetails->FwVersion);
        // Trigger download and update workflow
    }
}

int plugin_init() {
    g_fw_handle = registerProcess("MyPlugin", "1.0.0");
    if (g_fw_handle == NULL) {
        return -1;
    }
    return 0;
}

void plugin_check_updates() {
    CheckForUpdateResult result = checkForUpdate(g_fw_handle, on_firmware_available);
    if (result != CHECK_FOR_UPDATE_SUCCESS) {
        printf("Failed to check for updates\n");
    }
}

void plugin_cleanup() {
    if (g_fw_handle) {
        unregisterProcess(g_fw_handle);
        g_fw_handle = NULL;
    }
}
```

## Continuous Integration

```yaml
# .gitlab-ci.yml example
build_and_test:
  script:
    - autoreconf -if
    - ./configure
    - make
    - export LD_LIBRARY_PATH=.libs:$LD_LIBRARY_PATH
    - systemctl start rdkFwupdateMgr.service
    - ./example_plugin_registration
    - ./example_checkforupdate
    - grep "checkForUpdate() returned: SUCCESS" /opt/logs/rdkFwupdateMgr.log
```

## See Also

- [CheckForUpdate API Documentation](CHECK_FOR_UPDATE_API.md)
- [Quick Reference](QUICK_REFERENCE.md)
- [Process Registration API](librdkFwupdateMgr/PROCESS_REGISTRATION_API.md)
