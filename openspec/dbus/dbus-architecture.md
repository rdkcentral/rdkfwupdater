# D-Bus Architecture — `rdkFwupdateMgr` Daemon

> **Evidence Level:** Facts verified from `src/dbus/` source code  
> **Sources:** `rdkv_dbus_server.c`, `rdkv_dbus_server.h`, `rdkFwupdateMgr_handlers.c`, `rdkFwupdateMgr_handlers.h`, `xconf_comm_status.c`

---

## 1. Service Registration

### 1.1 D-Bus Coordinates

| Property | Value | Source |
|----------|-------|--------|
| Bus Type | System bus (`G_BUS_TYPE_SYSTEM`) | **[FACT]** `g_bus_get_sync(G_BUS_TYPE_SYSTEM)` |
| Bus Name | `org.rdkfwupdater.Interface` | **[FACT]** `BUS_NAME` constant |
| Object Path | `/org/rdkfwupdater/Service` | **[FACT]** `OBJECT_PATH` constant |
| Interface | `org.rdkfwupdater.Interface` | **[FACT]** introspection XML |

### 1.2 Setup Flow

**[FACT]** `setup_dbus_server()` performs:
1. Parse introspection XML into `GDBusNodeInfo`
2. Connect to system bus via `g_bus_get_sync()`
3. Register object at `OBJECT_PATH` with `process_app_request` as method handler
4. Own bus name via `g_bus_own_name_on_connection()`
5. Returns `1` on success, `0` on failure

**[FACT]** Called from daemon's `STATE_INIT` before `initialize()`.

---

## 2. Interface Introspection

```xml
<interface name='org.rdkfwupdater.Interface'>

  <!-- Process Management -->
  <method name='RegisterProcess'>
    <arg type='s' name='handler' direction='in'/>
    <arg type='s' name='libVersion' direction='in'/>
    <arg type='t' name='handler_id' direction='out'/>
  </method>

  <method name='UnregisterProcess'>
    <arg type='t' name='handlerId' direction='in'/>
    <arg type='b' name='success' direction='out'/>
  </method>

  <!-- Firmware Operations -->
  <method name='CheckForUpdate'>
    <arg type='s' name='handler_process_name' direction='in'/>
    <arg type='i' name='result' direction='out'/>
    <arg type='s' name='fwdata_version' direction='out'/>
    <arg type='s' name='fwdata_availableVersion' direction='out'/>
    <arg type='s' name='fwdata_updateDetails' direction='out'/>
    <arg type='s' name='fwdata_status' direction='out'/>
    <arg type='i' name='fwdata_status_code' direction='out'/>
  </method>

  <method name='DownloadFirmware'>
    <arg type='s' name='handlerId' direction='in'/>
    <arg type='s' name='firmwareName' direction='in'/>
    <arg type='s' name='downloadUrl' direction='in'/>
    <arg type='s' name='typeOfFirmware' direction='in'/>
    <arg type='s' name='result' direction='out'/>
    <arg type='s' name='status' direction='out'/>
    <arg type='s' name='message' direction='out'/>
  </method>

  <method name='UpdateFirmware'>
    <arg type='s' name='handlerId' direction='in'/>
    <arg type='s' name='firmwareName' direction='in'/>
    <arg type='s' name='LocationOfFirmware' direction='in'/>
    <arg type='s' name='TypeOfFirmware' direction='in'/>
    <arg type='s' name='rebootImmediately' direction='in'/>
    <arg type='s' name='UpdateResult' direction='out'/>
    <arg type='s' name='UpdateStatus' direction='out'/>
    <arg type='s' name='message' direction='out'/>
  </method>

  <!-- Async Signals -->
  <signal name='CheckForUpdateComplete'>
    <arg type='t' name='handlerId'/>
    <arg type='i' name='result'/>
    <arg type='i' name='statusCode'/>
    <arg type='s' name='currentVersion'/>
    <arg type='s' name='availableVersion'/>
    <arg type='s' name='updateDetails'/>
    <arg type='s' name='statusMessage'/>
  </signal>

  <signal name='DownloadProgress'>
    <arg type='t' name='handlerId'/>
    <arg type='s' name='firmwareName'/>
    <arg type='u' name='progress'/>
    <arg type='s' name='status'/>
    <arg type='s' name='message'/>
  </signal>

  <signal name='DownloadError'>
    <arg type='t' name='handlerId'/>
    <arg type='s' name='firmwareName'/>
    <arg type='s' name='status'/>
    <arg type='s' name='errorMessage'/>
  </signal>

  <signal name='UpdateProgress'>
    <arg type='t' name='handlerId'/>
    <arg type='s' name='firmwareName'/>
    <arg type='i' name='progress'/>
    <arg type='i' name='status'/>
    <arg type='s' name='message'/>
  </signal>
</interface>
```

---

## 3. Method Handler Dispatch

**[FACT]** `process_app_request()` is the single GDBus method call handler. It routes by method name:

```
process_app_request()
    ├── "RegisterProcess"    → add_process_to_tracking()
    ├── "UnregisterProcess"  → remove_process_from_tracking()
    ├── "CheckForUpdate"     → rdkFwupdateMgr_checkForUpdate() + async GTask
    ├── "DownloadFirmware"   → async GTask (rdkfw_download_worker)
    ├── "UpdateFirmware"     → async GThread (rdkfw_flash_worker_thread)
    └── unknown              → G_DBUS_ERROR_UNKNOWN_METHOD
```

---

## 4. Concurrency Model

### 4.1 Single-Operation-at-a-Time Enforcement

**[FACT]** The daemon enforces that only one operation of each type can run simultaneously:

| Operation | Guard | Thread Safety |
|-----------|-------|---------------|
| CheckForUpdate | `xconf_comm_status` module (mutex-protected) | Thread-safe via `trySetXConfCommStatus()` |
| Download | `IsDownloadInProgress` (main thread only) | Safe due to GLib main loop serialization |
| Flash | `IsFlashInProgress` (multi-thread access) | **[FACT]** TODO comment in code notes it should be mutex-protected |

### 4.2 Piggybacking / Queueing

**[FACT]** When an operation is already in progress:
- `CheckForUpdate` clients are added to `waiting_checkUpdate_ids` GSList
- `DownloadFirmware` clients for same firmware are added to `CurrentDownloadState.waiting_handler_ids`
- All waiting clients receive the same result via D-Bus signals when the operation completes

### 4.3 Worker Thread Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  GLib Main Loop Thread (main thread)                         │
│                                                              │
│  D-Bus method calls serialized by GLib                      │
│  ├── process_app_request() dispatches                       │
│  ├── g_idle_add() callbacks for signal emission             │
│  └── g_task_run_in_thread() spawns workers                  │
│                                                              │
│  Accesses without mutex (main loop guarantees):             │
│  - IsDownloadInProgress                                     │
│  - current_download (CurrentDownloadState)                  │
│  - registered_processes (GHashTable)                        │
│  - waiting_*_ids (GSList queues)                            │
└─────────────────────────────────────────────────────────────┘
          ▲                    ▲                    ▲
          │ g_idle_add()       │ g_idle_add()       │ g_idle_add()
          │                    │                    │
┌─────────┴──────┐  ┌─────────┴──────┐  ┌─────────┴──────┐
│ GTask Worker   │  │ GTask Worker   │  │ GThread Worker │
│ (XConf fetch)  │  │ (Download)     │  │ (Flash)        │
│                │  │                │  │                │
│ rdkfw_xconf_   │  │ rdkfw_download_│  │ rdkfw_flash_   │
│ fetch_worker() │  │ worker()       │  │ worker_thread()│
│                │  │                │  │                │
│ ► XConf HTTP   │  │ ► HTTP dwnl   │  │ ► flashImage() │
│ ► Parse JSON   │  │ ► Progress     │  │ ► Progress     │
│ ► Cache result │  │   monitor      │  │   via idle_add │
└────────────────┘  └────────────────┘  └────────────────┘
```

---

## 5. Process Tracking System

### 5.1 Data Structure

**[FACT]** `registered_processes` is a `GHashTable` mapping `handler_id` → `ProcessInfo`:

```c
typedef struct {
    guint64 handler_id;
    gchar *process_name;
    gchar *lib_version;
    gchar *sender_id;      // D-Bus unique name (e.g., ":1.42")
    gint64 registration_time;
} ProcessInfo;
```

### 5.2 Registration Rules

**[FACT]** Four cases for `RegisterProcess`:

| Scenario | Result |
|----------|--------|
| Same client, same process_name | Return existing handler_id (idempotent) |
| Same client, different process_name | **REJECT** — one registration per client |
| Different client, same process_name | **REJECT** — process names must be unique |
| New client, new process_name | **ALLOW** — create new registration |

---

## 6. XConf Cache System

### 6.1 File Cache

**[FACT]** The daemon caches XConf responses to reduce server load:

| File | Content |
|------|---------|
| `/tmp/xconf_response_thunder.txt` | Raw JSON response from XConf server |
| `/tmp/xconf_httpcode_thunder.txt` | HTTP response code |

**[FACT]** Protected by `G_LOCK_DEFINE_STATIC(xconf_cache)` mutex.

### 6.2 In-Memory Cache

**[FACT]** `g_cached_xconf_data` (static `XCONFRES`) holds parsed XConf response in memory. Protected by `G_LOCK_DEFINE_STATIC(xconf_data_cache)` mutex.

### 6.3 Cache-First Strategy

For `CheckForUpdate`:
1. If cache exists → validate firmware, return result immediately
2. If cache miss → return `FIRMWARE_CHECK_ERROR` immediately, spawn async XConf fetch
3. When fetch completes → cache result, emit `CheckForUpdateComplete` signal to all waiting clients

---

## 7. Signal Emission

**[FACT]** Background worker threads marshal signals to the main loop via `g_idle_add()`:

```
Worker Thread                    Main Loop Thread
    │                                │
    ├── g_idle_add(emit_signal)  ──► │
    │                                ├── g_dbus_connection_emit_signal()
    │                                │   (emits to all D-Bus subscribers)
    │                                │
    └────────────────────────────────┘
```

This ensures all signal emissions occur on the main loop thread where the `GDBusConnection` was created.

---

## 8. Cleanup / Shutdown

**[FACT]** `cleanup_dbus()` performs:
1. `cleanupXConfCommStatus()` — destroy XConf status mutex
2. Free all `TaskContext` entries from `active_tasks`
3. `cleanup_process_tracking()` — destroy process and download tracking hash tables
4. Unregister D-Bus object
5. Release D-Bus connection
6. Release bus name
7. Free `GMainLoop`
