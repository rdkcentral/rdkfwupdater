# How to Use `example_plugin`

`example_plugin` is the compiled binary of `example_app.c`. It exercises the
full `rdkFwupdateMgr` client library — register, check, download, flash, and
unregister — against a live daemon over D-Bus.

---

## Prerequisites

Before running `example_plugin`, three things must be true on the target device.

### 1. The daemon is running

```bash
systemctl status rdkFwupdateMgr
```

If it is not running:

```bash
systemctl start rdkFwupdateMgr
```

If it has never been enabled:

```bash
systemctl enable --now rdkFwupdateMgr
```

The binary will immediately fail at `registerProcess()` and print:

```
[ERROR] registerProcess() returned NULL.
        Ensure rdkFwupdateMgr daemon is running.
        Check: systemctl status rdkFwupdateMgr
```

### 2. The library is installed and visible

The shared library `librdkFwupdateMgr.so` must be findable at runtime.

```bash
# Confirm it is installed
ls -l /usr/lib/librdkFwupdateMgr.so*

# If the library is in a non-standard path, tell the linker
export LD_LIBRARY_PATH=/path/to/librdkFwupdateMgr:$LD_LIBRARY_PATH
```

### 3. D-Bus permissions allow the call

The binary talks to the system bus. Confirm the D-Bus policy for
`org.rdkfwupdater.Interface` allows your user or the root user:

```bash
cat /etc/dbus-1/system.d/rdkFwupdateMgr.conf
```

If running as a non-root user and calls are rejected, run as root or adjust
the D-Bus policy file.

---

## Build (if not already built by Yocto / autotools)

For a quick native build on the device itself:

```bash
gcc example_app.c \
    -o example_plugin \
    -I/path/to/librdkFwupdateMgr/include \
    -L/path/to/lib \
    -lrdkFwupdateMgr \
    $(pkg-config --cflags --libs gio-2.0) \
    -lpthread
```

Through autotools (cross-build, Yocto):

```bash
make example_plugin
make install          # installs to $(DESTDIR)$(bindir)/example_plugin
```

---

## Running

```bash
example_plugin
```

Or with full path:

```bash
/usr/bin/example_plugin
```

---

## What It Does — Step by Step

The binary runs a fixed sequence. There are no command-line arguments.

### Step 1 — Register

```
[Step 1] Registering with firmware daemon...
         processName = 'ExampleApp'
         libVersion  = '1.0.0'
[Step 1] ✓ Registered successfully.
           handle (handler_id string) = '12345'
```

`registerProcess("ExampleApp", "1.0.0")` sends a synchronous D-Bus call and
blocks until the daemon responds. On success, the daemon assigns a numeric
`handler_id` (here `12345`) and the library hands it back as a string. This
string is your handle for every subsequent call.

**If this step fails**, the daemon is not running or the D-Bus policy is
blocking the call. The binary exits with `EXIT_FAILURE`.

---

### Step 2 — Check for update

```
[Step 2] Calling checkForUpdate()...
[Step 2] ✓ checkForUpdate() returned CHECK_FOR_UPDATE_SUCCESS.
           Callback registered. Waiting for daemon signal...
```

`checkForUpdate(handle, on_firmware_event)` returns **immediately**. It does
not block. It only means the question was sent to the daemon successfully. The
actual answer comes later.

**If this step fails**, the library's internal callback registry is full (max
30 slots) or the D-Bus call could not be sent.

---

### Step 3 — Wait for the callback (up to 60 seconds)

```
[Step 3] Waiting for CheckForUpdateComplete signal (timeout: 60s)...
```

The binary blocks internally on a `pthread_cond_timedwait`. The library's
background thread is listening on D-Bus. When the daemon emits the
`CheckForUpdateComplete` signal, the callback fires and prints:

```
┌─────────────────────────────────────────────────────┐
│         CheckForUpdate Callback Received            │
└─────────────────────────────────────────────────────┘
  Handle (handler_id) : 12345
  Update Available    : YES
  Status              : FIRMWARE_AVAILABLE (0)
  Current Version     : 2.0.0.0
  Available Version   : 2.1.0.0
  Status Message      : Firmware upgrade available

  Next Action:
  → New firmware available. Schedule downloadFirmware().
└─────────────────────────────────────────────────────┘
```

The status printed is one of:

| Status printed | Meaning |
|---|---|
| `FIRMWARE_AVAILABLE` | New firmware found — can proceed to download |
| `FIRMWARE_NOT_AVAILABLE` | Already on latest — nothing to do |
| `UPDATE_NOT_ALLOWED` | Device is in the exclusion list |
| `FIRMWARE_CHECK_ERROR` | XConf/network error during the check |
| `IGNORE_OPTOUT` | Device opted out of firmware downloads |
| `BYPASS_OPTOUT` | Device opted out (bypass variant) |

**If the 60-second timeout expires**, the daemon did not emit the signal. Check
that the daemon is healthy and XConf is reachable from the device.

---

### Step 4 — Unregister

```
[Step 4] Unregistering from firmware daemon...
[Step 4] ✓ Unregistered. Handle set to NULL.
```

`unregisterProcess(handle)` sends a synchronous `UnregisterProcess` D-Bus call
and then frees the handle memory internally. After this, the handle is invalid.

---

### Final output

On success:

```
┌─────────────────────────────────────────────────────┐
│                 App completed OK                   │
└─────────────────────────────────────────────────────┘
```

On any failure:

```
┌─────────────────────────────────────────────────────┐
│              App completed with errors              │
└─────────────────────────────────────────────────────┘
```

Exit code is `0` on success, `1` on failure.

---

## Extending It — Calling downloadFirmware and updateFirmware

The `example_app.c` file already contains `run_download_example()` and
`run_update_example()` functions. They are **not wired into `main()` by
default** — `main()` only calls `checkForUpdate`. To exercise the full
three-step flow, wire them into `main()` like this:

```c
int main(void)
{
    /* Step 1 — Register */
    FirmwareInterfaceHandle handle = registerProcess("ExampleApp", "1.0.0");
    if (handle == FIRMWARE_INVALID_HANDLE) return EXIT_FAILURE;

    /* Step 2+3 — Check */
    checkForUpdate(handle, on_firmware_event);
    /* [wait on condvar — on_firmware_event signals when done] */

    /* Only proceed if update is available */
    if (g_firmware_available) {

        /* Step 4+5 — Download */
        run_download_example(handle);
        /* [waits internally until DWNL_COMPLETED] */

        /* Step 6+7 — Flash */
        run_update_example(handle);
        /* [waits internally until UPDATE_COMPLETED] */
        /* device reboots here if rebootImmediately = true */
    }

    /* Step 8 — Unregister (skip if rebootImmediately = true) */
    unregisterProcess(handle);
    handle = NULL;

    return EXIT_SUCCESS;
}
```

> **Note:** Add a module-level `static int g_firmware_available = 0;` flag and
> set it inside `on_firmware_event()` when `status == FIRMWARE_AVAILABLE`.

---

## Timeouts Reference

| Stage | Timeout | Controlled by |
|---|---|---|
| `registerProcess()` | 10 seconds | `DBUS_TIMEOUT_MS` in process.c |
| `checkForUpdate()` wait | 60 seconds | `deadline.tv_sec += 60` in main() |
| `downloadFirmware()` wait | 5 minutes | `deadline.tv_sec += 300` in run_download_example() |
| `updateFirmware()` wait | 10 minutes | `deadline.tv_sec += 600` in run_update_example() |
| `unregisterProcess()` | 10 seconds | `DBUS_TIMEOUT_MS` in process.c |

---

## Diagnosing Common Failures

### `registerProcess() returned NULL`
```
Cause:   Daemon not running, or D-Bus policy blocking the call.
Fix:     systemctl start rdkFwupdateMgr
         Check /etc/dbus-1/system.d/rdkFwupdateMgr.conf
```

### `checkForUpdate() returned FAIL`
```
Cause:   D-Bus send failed, or library internal registry is full.
Fix:     Check journalctl -u rdkFwupdateMgr for daemon errors.
         Restart the daemon and retry.
```

### 60-second timeout on CheckForUpdateComplete
```
Cause:   Daemon is running but did not emit the signal.
         Possible reasons: XConf unreachable, daemon stuck processing.
Fix:     journalctl -u rdkFwupdateMgr --since "1 minute ago"
         Check network connectivity to XConf endpoint.
         Restart daemon: systemctl restart rdkFwupdateMgr
```

### `librdkFwupdateMgr.so: cannot open shared object file`
```
Cause:   Library not in LD path.
Fix:     export LD_LIBRARY_PATH=/usr/lib:$LD_LIBRARY_PATH
         Or run ldconfig after installing the library.
```

---

## Watching D-Bus Traffic in Real Time

Use `dbus-monitor` on another terminal to see the exact messages flowing:

```bash
dbus-monitor --system "interface='org.rdkfwupdater.Interface'"
```

You will see:

```
method call   → RegisterProcess("ExampleApp", "1.0.0")
method return ← handler_id = 12345

method call   → CheckForUpdate("12345")

signal        ← CheckForUpdateComplete(0, 0, "2.0.0.0", "2.1.0.0", "...", "...")

method call   → UnregisterProcess(12345)
method return ← success = true
```

This is the fastest way to confirm the library and daemon are communicating correctly.
