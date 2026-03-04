# Building and Testing librdkFwupdateMgr

## Quick Build & Test Guide

### Prerequisites

Ensure you have the required dependencies:
```bash
# Install build tools
sudo apt-get install autoconf automake libtool pkg-config

# Install GLib/GIO dependencies
sudo apt-get install libglib2.0-dev libgio-2.0-dev

# Verify pkg-config can find them
pkg-config --cflags --libs glib-2.0 gio-2.0
```

### Build Steps

#### 1. Full Build (Daemon + Library + Example)

```bash
cd /path/to/rdkfwupdater

# Generate configure script (first time only)
autoreconf -fi

# Configure
./configure

# Build everything
make

# Install (optional)
sudo make install
```

#### 2. Build Only librdkFwupdateMgr and Example

```bash
cd /path/to/rdkfwupdater

# If configure hasn't been run yet
./configure

# Build just the client library and example
make librdkFwupdateMgr.la
make example_plugin_registration

# The library will be at: .libs/librdkFwupdateMgr.so
# The example will be at: ./example_plugin_registration
```

#### 3. Quick Rebuild After Code Changes

```bash
# Rebuild library
make librdkFwupdateMgr.la

# Rebuild example
make example_plugin_registration
```

### Running the Example

#### Prerequisites
1. The rdkFwupdateMgr daemon must be running
2. D-Bus system bus must be accessible

```bash
# Start the daemon (if not already running)
sudo systemctl start rdkFwupdateMgr.service

# OR run daemon manually for testing
sudo ./rdkFwupdateMgr

# Check daemon status
systemctl status rdkFwupdateMgr.service
```

#### Run the Example

```bash
# Run the example program
./example_plugin_registration

# Expected output:
=================================================
Firmware Update Process Registration Examples
=================================================

=== EXAMPLE 1: Basic Registration ===
2026-02-23 10:30:45 [librdkFwupdateMgr] INFO: [registerProcess:240] registerProcess() called
2026-02-23 10:30:45 [librdkFwupdateMgr] INFO: [registerProcess:241]   processName: 'VideoPlayerPlugin'
2026-02-23 10:30:45 [librdkFwupdateMgr] INFO: [registerProcess:242]   libVersion:  '2.0.1'
2026-02-23 10:30:45 [librdkFwupdateMgr] DEBUG: [create_dbus_proxy:149] D-Bus proxy created successfully
2026-02-23 10:30:45 [librdkFwupdateMgr] INFO: [registerProcess:262] Registration successful
2026-02-23 10:30:45 [librdkFwupdateMgr] INFO: [registerProcess:263]   handler_id: 12345
[Plugin] Registration successful! Handle: 12345
[Plugin] Now ready to call CheckForUpdate, DownloadFirmware, etc.
[Plugin] Unregistering...
2026-02-23 10:30:46 [librdkFwupdateMgr] INFO: [unregisterProcess:298] unregisterProcess() called
2026-02-23 10:30:46 [librdkFwupdateMgr] INFO: [unregisterProcess:299]   handle: '12345'
2026-02-23 10:30:46 [librdkFwupdateMgr] INFO: [unregisterProcess:332] Unregistration successful
[Plugin] Cleanup complete

... (more examples) ...

=================================================
All examples complete
=================================================
```

#### Check Logs

```bash
# View library logs (same file as daemon)
tail -f /opt/logs/rdkFwupdateMgr.log

# Filter for library logs only
tail -f /opt/logs/rdkFwupdateMgr.log | grep librdkFwupdateMgr

# View daemon logs
journalctl -u rdkFwupdateMgr -f
```

### Monitor D-Bus Communication

```bash
# In one terminal, monitor D-Bus
dbus-monitor --system "sender='org.rdkfwupdater.Interface'" &

# In another terminal, run the example
./example_plugin_registration

# You should see D-Bus method calls:
method call sender=:1.123 -> destination=org.rdkfwupdater.Interface
   path=/org/rdkfwupdater/Service; interface=org.rdkfwupdater.Interface;
   member=RegisterProcess
   string "VideoPlayerPlugin"
   string "2.0.1"
method return sender=:1.1 -> destination=:1.123
   uint64 12345
```

### Build from Scratch (Clean Build)

```bash
# Clean everything
make clean
make distclean

# Regenerate build system
autoreconf -fi

# Configure
./configure

# Build
make

# Test
./example_plugin_registration
```

### Cross-Compilation (for ARM/target device)

```bash
# Set cross-compilation environment
export CC=arm-linux-gnueabihf-gcc
export CXX=arm-linux-gnueabihf-g++
export PKG_CONFIG_PATH=/path/to/target/sysroot/usr/lib/pkgconfig
export PKG_CONFIG_SYSROOT_DIR=/path/to/target/sysroot

# Configure for cross-compilation
./configure --host=arm-linux-gnueabihf \
            --prefix=/usr \
            --libdir=/usr/lib

# Build
make

# Install to staging area
make DESTDIR=/path/to/staging install
```

### Troubleshooting

#### Problem: "configure: command not found"

```bash
# Generate configure script
autoreconf -fi
```

#### Problem: "pkg-config: command not found"

```bash
sudo apt-get install pkg-config
```

#### Problem: "Package 'glib-2.0' not found"

```bash
sudo apt-get install libglib2.0-dev
```

#### Problem: "Cannot connect to D-Bus system bus"

```bash
# Check if D-Bus is running
ps aux | grep dbus-daemon

# Start D-Bus if needed
sudo systemctl start dbus

# Check permissions
groups  # Should include 'messagebus' or similar
```

#### Problem: "Registration failed"

```bash
# Check if daemon is running
systemctl status rdkFwupdateMgr.service

# Check daemon logs
journalctl -u rdkFwupdateMgr -n 50

# Try manual daemon start for debugging
sudo ./rdkFwupdateMgr
```

#### Problem: Example crashes or hangs

```bash
# Run with GDB
gdb ./example_plugin_registration
(gdb) run
# If it crashes:
(gdb) bt  # Get backtrace

# Run with valgrind for memory issues
valgrind --leak-check=full ./example_plugin_registration
```

#### Problem: Library not found at runtime

```bash
# Add library path
export LD_LIBRARY_PATH=.libs:$LD_LIBRARY_PATH

# Or install the library
sudo make install
sudo ldconfig
```

### Manual Compilation (without autotools)

If you need to compile manually without autotools:

```bash
# Compile library
gcc -fPIC -shared -o librdkFwupdateMgr.so \
    librdkFwupdateMgr/src/rdkFwupdateMgr_process.c \
    librdkFwupdateMgr/src/rdkFwupdateMgr_log.c \
    -I librdkFwupdateMgr/include \
    -I librdkFwupdateMgr/src \
    $(pkg-config --cflags --libs glib-2.0 gio-2.0) \
    -lpthread

# Compile example
gcc -o example_plugin_registration \
    librdkFwupdateMgr/examples/example_plugin_registration.c \
    -I librdkFwupdateMgr/include \
    -L. -lrdkFwupdateMgr \
    $(pkg-config --cflags --libs glib-2.0 gio-2.0) \
    -Wl,-rpath,.

# Run
./example_plugin_registration
```

### Integration with Your Plugin

```bash
# Link against librdkFwupdateMgr in your Makefile
CFLAGS += -I/usr/include/rdkFwupdateMgr
LDFLAGS += -lrdkFwupdateMgr

# Or with pkg-config (if .pc file is installed)
CFLAGS += $(shell pkg-config --cflags rdkFwupdateMgr)
LDFLAGS += $(shell pkg-config --libs rdkFwupdateMgr)

# Compile your plugin
gcc -o myplugin myplugin.c $(CFLAGS) $(LDFLAGS)
```

### Verifying Installation

```bash
# Check if library is installed
ls -la /usr/lib/librdkFwupdateMgr.so*

# Check if headers are installed
ls -la /usr/include/rdkFwupdateMgr/

# Check pkg-config
pkg-config --modversion rdkFwupdateMgr
pkg-config --cflags rdkFwupdateMgr
pkg-config --libs rdkFwupdateMgr

# Test library loading
ldd ./example_plugin_registration
```

### Next Steps

1. ✅ Build library and example
2. ✅ Run example with daemon
3. ✅ Check logs in /opt/logs/rdkFwupdateMgr.log
4. ✅ Integrate into your plugin code
5. ✅ Test with real firmware operations

For detailed API usage, see:
- `librdkFwupdateMgr/QUICK_START.md`
- `librdkFwupdateMgr/PROCESS_REGISTRATION_API.md`
