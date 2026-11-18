# Client Library Architecture — `librdkFwupdateMgr.so`

> **Evidence Level:** Facts verified from source code unless noted otherwise  
> **Sources:** `librdkFwupdateMgr/src/`, `librdkFwupdateMgr/include/rdkFwupdateMgr_client.h`

---

## 1. Purpose

`librdkFwupdateMgr.so` is a C shared library that provides a simple, callback-based API for client applications to perform firmware operations (check, download, flash) without direct D-Bus knowledge. The library:

- Abstracts D-Bus protocol details behind a plain C API
- Manages a background GLib event loop thread for receiving D-Bus signals
- Provides callback registries for asynchronous result delivery
- Handles connection lifecycle (ephemeral connections for method calls, persistent connection for signals)

---

## 2. Public API Surface

**Header:** `rdkFwupdateMgr_client.h`

### 2.1 Registration

```c
FirmwareInterfaceHandle registerProcess(const char *processName, const char *libVersion);
void unregisterProcess(FirmwareInterfaceHandle handler);
```

- **[FACT]** `registerProcess()` makes a synchronous D-Bus call to the daemon's `RegisterProcess` method
- **[FACT]** Returns a string handle (e.g., `"1"`) allocated by the library; caller must not free it
- **[FACT]** Internally calls `internal_system_init()` to start the background thread on first invocation
- **[FACT]** `unregisterProcess()` makes a synchronous D-Bus call, then frees the handle

### 2.2 Firmware Check

```c
CheckForUpdateResult checkForUpdate(FirmwareInterfaceHandle handle, UpdateEventCallback callback);
```

- **[FACT]** Non-blocking fire-and-forget call; returns `CHECK_FOR_UPDATE_SUCCESS` immediately
- **[FACT]** Registers callback in `g_registry` before sending D-Bus call (prevents signal race)
- **[FACT]** Uses ephemeral D-Bus connection (created and destroyed per call)
- **[FACT]** Callback fires exactly once when `CheckForUpdateComplete` signal arrives
- **[FACT]** Callback receives `const FwInfoData*` with version info and `UpdateDetails` struct

### 2.3 Download

```c
DownloadResult downloadFirmware(FirmwareInterfaceHandle handle, const FwDwnlReq *fwdwnlreq, DownloadCallback callback);
```

- **[FACT]** Non-blocking; callback fires repeatedly with progress (0-100%)
- **[FACT]** Uses separate `g_dwnl_registry` from checkForUpdate's registry
- **[FACT]** Callback transitions: `DWNL_IN_PROGRESS` → `DWNL_COMPLETED` or `DWNL_ERROR`
- **[FACT]** Slot stays ACTIVE across multiple signals, resets to IDLE on terminal status

### 2.4 Flash/Update

```c
UpdateResult updateFirmware(FirmwareInterfaceHandle handle, const FwUpdateReq *fwupdatereq, UpdateCallback callback);
```

- **[FACT]** Non-blocking; callback fires repeatedly with flash progress
- **[FACT]** Uses `g_update_registry` (independent registry)
- **[FACT]** Callback transitions: `UPDATE_IN_PROGRESS` → `UPDATE_COMPLETED` or `UPDATE_ERROR`

---

## 3. Internal Architecture

### 3.1 Source Files

| File | Responsibility |
|------|---------------|
| `rdkFwupdateMgr_process.c` | `registerProcess()` / `unregisterProcess()` — synchronous D-Bus calls |
| `rdkFwupdateMgr_api.c` | `checkForUpdate()`, `downloadFirmware()`, `updateFirmware()` — fire-and-forget async APIs |
| `rdkFwupdateMgr_async.c` | Background thread, callback registries, D-Bus signal handlers, dispatch logic |
| `rdkFwupdateMgr_async_internal.h` | Internal types: `CallbackRegistry`, `BackgroundThread`, `InternalSignalData` |
| `rdkFwupdateMgr_log.h` | Logging macros (`FWUPMGR_INFO`, `FWUPMGR_ERROR`, etc.) |

### 3.2 Threading Model

```
┌─────────────────────────────────────────────────────────────────┐
│  Application Thread (caller's thread)                            │
│                                                                  │
│  registerProcess() ─── sync D-Bus ──┐                           │
│  checkForUpdate()  ─── fire&forget ──┤                           │
│  downloadFirmware()── fire&forget ──┤   Ephemeral D-Bus         │
│  updateFirmware() ─── fire&forget ──┤   connections (one per    │
│  unregisterProcess()─ sync D-Bus ──┘    call, then destroyed)   │
│                                                                  │
│  pthread_cond_timedwait() ◄── waits for callback signal         │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│  Background Thread (library-internal)                            │
│                                                                  │
│  Persistent D-Bus connection (e.g., :1.141)                     │
│  Isolated GMainContext (no interference with app's GLib loop)    │
│                                                                  │
│  Subscriptions:                                                  │
│    - CheckForUpdateComplete → on_check_complete_signal()         │
│    - DownloadProgress       → on_download_progress_signal()      │
│    - UpdateProgress         → on_update_progress_signal()        │
│                                                                  │
│  on_*_signal() → dispatch_*() → invoke app callback              │
│                                                                  │
│  g_main_loop_run() ← blocks until internal_system_deinit()      │
└─────────────────────────────────────────────────────────────────┘
```

### 3.3 Callback Registry Design

**[FACT]** Three independent registries exist:

| Registry | API | Capacity | Slot Lifecycle |
|----------|-----|----------|----------------|
| `g_registry` | `checkForUpdate` | 30 slots | IDLE → PENDING → DISPATCHED → IDLE |
| `g_dwnl_registry` | `downloadFirmware` | 30 slots | IDLE → ACTIVE → IDLE (on terminal) |
| `g_update_registry` | `updateFirmware` | 30 slots | IDLE → ACTIVE → IDLE (on terminal) |

Each registry has its own `pthread_mutex_t`. Callbacks are invoked with the mutex **released** to prevent deadlocks.

### 3.4 Race Condition Prevention

**[FACT]** The API follows a strict ordering:

1. Connect to D-Bus (fail early if bus is down)
2. Register callback in registry (ensures slot exists before signal can arrive)
3. Send fire-and-forget D-Bus method call
4. Close ephemeral connection, return SUCCESS

This ordering prevents:
- **Ghost entries:** If D-Bus connection fails after callback registration
- **Missed signals:** If daemon responds before callback is registered

---

## 4. D-Bus Wire Protocol

### 4.1 Connection Constants

```c
#define DBUS_SERVICE_NAME    "org.rdkfwupdater.Service"
#define DBUS_OBJECT_PATH     "/org/rdkfwupdater/Service"
#define DBUS_INTERFACE_NAME  "org.rdkfwupdater.Interface"
#define DBUS_TIMEOUT_MS      5000
#define DBUS_SYNC_TIMEOUT_MS 10000
```

### 4.2 Signal Formats

| Signal | GVariant Type | Fields |
|--------|--------------|--------|
| `CheckForUpdateComplete` | `(tiissss)` | handler_id, result, status_code, currentVersion, availableVersion, updateDetails, statusMessage |
| `DownloadProgress` | `(tsuss)` | handler_id, firmware_name, progress_percent, status_string, message |
| `UpdateProgress` | `(tsiss)` | handler_id, firmware_name, progress, status, message |

---

## 5. Data Structures

### 5.1 FwInfoData (checkForUpdate result)

```c
typedef struct {
    char CurrFWVersion[64];
    UpdateDetails *UpdateDetails;     // Non-NULL only when FIRMWARE_AVAILABLE
    CheckForUpdateStatus status;
} FwInfoData;
```

### 5.2 UpdateDetails (firmware metadata)

```c
typedef struct {
    char FwFileName[128];
    char FwUrl[512];
    char FwVersion[64];
    char RebootImmediately[12];
    char DelayDownload[8];
    char PDRIVersion[64];
    char PeripheralFirmwares[256];
} UpdateDetails;
```

**[FACT]** `UpdateDetails` is populated by parsing the `updateDetails` pipe-separated string from the D-Bus signal (format: `Key:Value|Key:Value|...`).

---

## 6. Lifecycle Example

```c
// 1. Register with daemon
FirmwareInterfaceHandle h = registerProcess("MyApp", "1.0");

// 2. Check for updates (non-blocking)
checkForUpdate(h, my_callback);
// ... callback fires on BG thread with FwInfoData ...

// 3. Download if available (non-blocking)
FwDwnlReq req = { .firmwareName = "fw_v8.bin", .downloadUrl = url, .TypeOfFirmware = "PCI" };
downloadFirmware(h, &req, download_cb);
// ... progress callbacks fire repeatedly ...

// 4. Flash firmware (non-blocking)
FwUpdateReq ureq = { .firmwareName = "fw_v8.bin", .TypeOfFirmware = "PCI", .rebootImmediately = true };
updateFirmware(h, &ureq, update_cb);
// ... progress callbacks fire, device reboots on completion ...

// 5. Cleanup
unregisterProcess(h);
```
