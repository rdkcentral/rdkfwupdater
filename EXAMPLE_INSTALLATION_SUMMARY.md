# Example Programs Installation - Summary

**Date:** 2026-02-26  
**Change:** Modified Makefile.am to install example programs to rootfs

---

## What Was Changed

### Modified File
- **`Makefile.am`** - Updated to install example programs

### Key Changes

1. **Changed from `noinst_PROGRAMS` to `bin_PROGRAMS`:**
   ```makefile
   # BEFORE:
   noinst_PROGRAMS = example_plugin_registration example_checkforupdate
   
   # AFTER:
   bin_PROGRAMS += example_plugin_registration example_checkforupdate example_async_checkforupdate
   ```

2. **Added async example to build:**
   ```makefile
   example_async_checkforupdate_SOURCES = \
       ${top_srcdir}/librdkFwupdateMgr/examples/example_async_checkforupdate.c
   
   example_async_checkforupdate_CFLAGS = -I${top_srcdir}/librdkFwupdateMgr/include $(AM_CFLAGS) $(GLIB_CFLAGS)
   example_async_checkforupdate_LDADD = librdkFwupdateMgr.la $(GLIB_LIBS) -lpthread
   example_async_checkforupdate_LDFLAGS = -L$(PKG_CONFIG_SYSROOT_DIR)/$(libdir)
   ```

3. **Added documentation:**
   - Created `librdkFwupdateMgr/examples/README.md` with comprehensive usage guide

---

## What Gets Installed

After building and installing, the following binaries will be available on the device:

### Location: `/usr/bin/`

1. **`example_plugin_registration`**
   - Tests plugin registration and unregistration
   - Demonstrates how to register with handler ID
   - Keeps plugin registered for 30 seconds

2. **`example_checkforupdate`**
   - Tests synchronous (blocking) CheckForUpdate API
   - Simple demonstration of sync API usage
   - Returns update information

3. **`example_async_checkforupdate`**
   - Tests asynchronous (non-blocking) CheckForUpdate API
   - Demonstrates multiple concurrent requests
   - Shows callback invocation
   - Includes cancellation test

---

## How to Use on Device

### 1. Build and Install

```bash
# On build machine
cd /path/to/rdkfwupdater
./configure
make
make install DESTDIR=/path/to/rootfs

# Or for direct install
sudo make install
```

### 2. Deploy to Device

```bash
# Copy rootfs to device or flash updated image
# Examples will be at /usr/bin/example_*
```

### 3. Run on Device

#### Prerequisites
```bash
# Ensure daemon is running
ps aux | grep rdkFwupdateMgr

# If not running, start it
systemctl start rdkFwupdateMgr
# OR
/usr/bin/rdkFwupdateMgr &
```

#### Test Plugin Registration
```bash
example_plugin_registration

# Expected output:
# ✓ Plugin registered successfully!
# (Waits 30 seconds)
# ✓ Plugin unregistered successfully!
```

#### Test Synchronous API
```bash
example_checkforupdate

# Expected output:
# CheckForUpdate completed:
#   Status: 0
#   Update Available: Yes/No
#   Version info...
```

#### Test Asynchronous API
```bash
example_async_checkforupdate

# Expected output:
# ✓ Async subsystem initialized
# [UI Handler] Request registered with callback_id: 1
# [Background Handler] Request registered with callback_id: 2
# [Monitoring Handler] Request registered with callback_id: 3
# (Shows callbacks being invoked)
# ✓ All callbacks completed
```

---

## Testing Scenarios

### Basic Functionality Test
```bash
# 1. Start daemon
systemctl start rdkFwupdateMgr

# 2. Run each example
example_plugin_registration
example_checkforupdate
example_async_checkforupdate

# 3. Check all passed
echo $?  # Should be 0 for each
```

### Concurrent Operations Test
```bash
# Run multiple async examples simultaneously
for i in {1..5}; do
    example_async_checkforupdate &
done
wait

# All should complete successfully
```

### Integration with Daemon Test
```bash
# Terminal 1: Keep plugin registered
example_plugin_registration &
PLUGIN_PID=$!

# Wait for registration
sleep 2

# Terminal 2: Trigger update from daemon
testClient CheckForUpdate

# Terminal 1 should show callback notification
wait $PLUGIN_PID
```

---

## Verification

After installation, verify:

```bash
# Check binaries exist
ls -l /usr/bin/example_*
# Should show:
# -rwxr-xr-x 1 root root ... /usr/bin/example_plugin_registration
# -rwxr-xr-x 1 root root ... /usr/bin/example_checkforupdate
# -rwxr-xr-x 1 root root ... /usr/bin/example_async_checkforupdate

# Check dependencies are met
ldd /usr/bin/example_plugin_registration
# Should show librdkFwupdateMgr.so and other libs

# Check library is installed
ls -l /usr/lib/librdkFwupdateMgr.so*
# Should show library files

# Test execution
example_plugin_registration --help
# Should run without errors
```

---

## Troubleshooting

### Build Issues

**Problem:** "example_async_checkforupdate.c: No such file or directory"

**Solution:** Make sure the file exists:
```bash
ls -l librdkFwupdateMgr/examples/example_async_checkforupdate.c
```
If missing, it should have been created in Phase 5. Check if it exists or needs to be created.

### Runtime Issues

**Problem:** "Failed to connect to D-Bus"

**Solution:**
```bash
# Check D-Bus is running
ps aux | grep dbus

# Set up D-Bus session if needed
eval $(dbus-launch --sh-syntax)
```

**Problem:** "Library not found"

**Solution:**
```bash
# Update library cache
ldconfig

# Check LD_LIBRARY_PATH
export LD_LIBRARY_PATH=/usr/lib:$LD_LIBRARY_PATH
```

**Problem:** "Daemon not responding"

**Solution:**
```bash
# Restart daemon
systemctl restart rdkFwupdateMgr

# Check daemon logs
journalctl -u rdkFwupdateMgr -f
```

---

## Documentation

For detailed usage and troubleshooting, see:
- **`librdkFwupdateMgr/examples/README.md`** - Complete user guide
- **`ASYNC_API_QUICK_REFERENCE.md`** - API documentation
- **`IMPLEMENTATION_SUMMARY.md`** - Architecture overview

---

## Benefits

1. ✅ **Easy Testing:** Examples can now be run directly on device
2. ✅ **Integration Testing:** Verify Register, Unregister, CheckForUpdate work end-to-end
3. ✅ **Debugging:** Test async API behavior in real environment
4. ✅ **Validation:** Confirm daemon-client communication works correctly
5. ✅ **Demos:** Show plugin developers how to use the APIs

---

## What's Included

### Programs (3)
- `example_plugin_registration` - Registration/unregistration test
- `example_checkforupdate` - Synchronous API test
- `example_async_checkforupdate` - Asynchronous API test

### Documentation (1)
- `librdkFwupdateMgr/examples/README.md` - Complete usage guide with:
  - Prerequisites
  - Usage examples
  - Testing scenarios
  - Troubleshooting
  - Integration testing scripts

---

## Next Steps

1. **Rebuild:**
   ```bash
   make clean
   make
   make install
   ```

2. **Deploy to device:**
   - Flash updated rootfs, or
   - Copy binaries manually

3. **Test on device:**
   ```bash
   example_plugin_registration
   example_checkforupdate
   example_async_checkforupdate
   ```

4. **Verify functionality:**
   - All examples should complete successfully
   - Check daemon logs for any errors
   - Verify callbacks are invoked correctly

---

**Status:** ✅ Complete - Ready to build and test on device

---

**Document Version:** 1.0  
**Last Updated:** 2026-02-26
