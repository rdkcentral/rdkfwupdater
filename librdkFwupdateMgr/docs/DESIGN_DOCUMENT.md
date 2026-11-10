# librdkFwupdateMgr — Engineering Design Document

> **Document Version**: 2.0  
> **Date**: May 02, 2026  
> **Component**: `librdkFwupdateMgr` (shared library)  

---

## Table of Contents

1.  [Executive Summary](#1-executive-summary)
2.  [Why This Library Exists](#2-why-this-library-exists)
3.  [Why Clients Must Not Directly Talk to the Daemon](#3-why-clients-must-not-directly-talk-to-the-daemon)
4.  [Shared Library Responsibilities](#4-shared-library-responsibilities)
5.  [Daemon Responsibilities](#5-daemon-responsibilities)
6.  [High-Level Request Lifecycle](#6-high-level-request-lifecycle)
7.  [Detailed Public API Reference](#7-detailed-public-api-reference)
    - 7.1 [registerProcess()](#71-registerprocess)
    - 7.2 [checkForUpdate()](#72-checkforupdate)
    - 7.3 [downloadFirmware()](#73-downloadfirmware)
    - 7.4 [updateFirmware()](#74-updatefirmware)
    - 7.5 [unregisterProcess()](#75-unregisterprocess)
8.  [Internal Helper Modules](#8-internal-helper-modules)
9.  [IPC Communication Model](#9-ipc-communication-model)
10. [Retry and Timeout Strategy](#10-retry-and-timeout-strategy)
11. [Failure Scenarios and Recovery](#11-failure-scenarios-and-recovery)
12. [Logging Architecture](#12-logging-architecture)
13. [Security Considerations](#13-security-considerations)
14. [Performance Considerations](#14-performance-considerations)
15. [Scalability for Multiple Clients](#15-scalability-for-multiple-clients)
16. [Future Extensibility](#16-future-extensibility)

---

## 1. Executive Summary

`librdkFwupdateMgr` is a C shared library (`librdkFwupdateMgr.so`) that provides client applications on RDK-based embedded devices a clean, stable API for firmware lifecycle management. The library acts as a thin, intelligent client-side proxy that communicates with the `rdkFwupdateMgr` daemon over D-Bus (system bus IPC).

### What Problem Does It Solve?

Firmware updates on embedded devices require coordination between:
- Cloud infrastructure (XConf configuration server)
- Local device services (download managers, flash subsystems, reboot coordinators)
- Multiple client applications that may each need to initiate or observe firmware updates

Without this library, every client application would need to:
- Know D-Bus method names, object paths, interface names, and GVariant type signatures
- Manage D-Bus connections, proxies, and signal subscriptions
- Handle threading for asynchronous signal reception
- Implement timeout and retry logic
- Parse raw D-Bus data into usable structures

This library eliminates all of that complexity. A client app includes one header, links one library, calls five functions, and implements three callback signatures.

### Key Design Characteristics

| Characteristic | Decision | Rationale |
|---------------|----------|-----------|
| IPC mechanism | D-Bus (system bus) | Standard Linux IPC, D-Bus policy enforcement, well-understood |
| Threading model | Single background thread + per-call ephemeral connections | Simple, minimal resource usage |
| API style | Async fire-and-forget with callbacks | Non-blocking; suitable for event-driven and threaded apps |
| Memory model | Library owns handle; client owns callback data copies | Clear ownership boundaries |
| Connection model | Stateless (new D-Bus connection per API call) | No connection lifecycle management needed |
| Callback delivery | Background thread invocation | Deterministic delivery; client uses condvar to synchronize |

---

## 2. Why This Library Exists

### 2.1 The Fundamental Problem

The `rdkFwupdateMgr` daemon manages all firmware operations on the device: checking for updates, downloading firmware images, flashing them to storage, and coordinating reboots. Multiple client applications need to interact with this daemon:

- **example_plugin** — A reference one-shot firmware updater
- **TR-069/TR-181 agents** — Remote management protocols that trigger updates
- **WebUI services** — User-facing interfaces showing update status
- **Monitoring daemons** — Health-check services that poll firmware state

Each of these would need to implement identical D-Bus client logic if they talked to the daemon directly. This is the classic "N-clients × M-operations" maintenance problem.

### 2.2 What the Library Provides

```
WITHOUT library:                    WITH library:
─────────────────                   ─────────────────
Client A: 200 lines D-Bus code      Client A: 30 lines using 5 API calls
Client B: 200 lines D-Bus code      Client B: 30 lines using 5 API calls
Client C: 200 lines D-Bus code      Client C: 30 lines using 5 API calls
                                     Library: 1500 lines (maintained once)
```

### 2.3 Design Goals

1. **Simplicity** — Five public functions. Three callback types. One header to include.
2. **Correctness** — Thread-safe, leak-free, handles all error paths.
3. **Stability** — Public API (header) changes require version bumps. Internal implementation can change freely.
4. **Observability** — Structured logging with module separation (`[FWUPMGR]`).
5. **Portability** — Works with or without RDK_LOGGER. Falls back to `fprintf` for unit testing.

---

## 3. Why Clients Must Not Directly Talk to the Daemon

This section documents the engineering rationale for mandating library usage rather than allowing direct D-Bus calls.

### 3.1 Protocol Encapsulation

The D-Bus interface between client and daemon is an **internal protocol**, not a public contract:

| Aspect | Risk of Direct D-Bus Access |
|--------|---------------------------|
| Method signatures | `(ss) → (t)`, `(ssss)`, `(sssss)` — one typo = crash or silent failure |
| Signal signatures | `(tiissss)`, `(tsuss)`, `(tsiis)` — must parse correctly or lose data |
| Signal subscription setup | Must happen BEFORE method call, or response signal is lost |
| Update details format | Pipe-separated `Key:Value` string — undocumented, may change |
| Handler ID encoding | `uint64` on wire but `char*` in API — format details shouldn't leak |

If the daemon team changes a signal signature (e.g., adds a field), only the library needs updating — not every client.

### 3.2 Threading Complexity

Receiving D-Bus signals requires:
- A dedicated GLib event loop running in a thread
- Signal subscription with correct object path and interface filters
- Proper GMainContext isolation (so the app's own GLib loop isn't disrupted)
- Mutex-protected callback dispatch with deadlock prevention

No client developer should reimplement this. It's error-prone and already solved in the library.

### 3.3 Connection Lifecycle

The daemon identifies clients by `handler_id`, not by D-Bus sender address. This is because our library's stateless model creates a **new D-Bus connection per API call**, meaning each call gets a different sender ID (`:1.140`, `:1.141`, `:1.145`, etc.).

A naive client attempting direct D-Bus calls would likely assume sender-ID stability — leading to authorization failures at the daemon's process tracking layer.

### 3.4 Forward Compatibility

The library provides a stable ABI boundary:
- Daemon protocol changes → library absorbs them internally
- New capabilities (e.g., cancel, pause) → added as new library functions
- Client code recompiles against same header, links same `.so` name

---

## 4. Shared Library Responsibilities

The library (`librdkFwupdateMgr.so`) is responsible for the following and **only** the following:

| # | Responsibility | Implementation |
|---|---------------|----------------|
| 1 | **Input validation** | NULL checks, empty string checks, length limits on all public API parameters |
| 2 | **D-Bus transport** | Create connection, build GVariant payloads, send method calls, handle D-Bus errors |
| 3 | **Async signal reception** | Background thread with GLib event loop subscribed to daemon signals |
| 4 | **Callback management** | Three registries (check, download, update) with mutex protection |
| 5 | **Data transformation** | Convert daemon's wire format (GVariant) into typed C structs (`FwInfoData`, progress values) |
| 6 | **Handle lifecycle** | Allocate on register, validate on use, free on unregister |
| 7 | **Structured logging** | All operations logged under `LOG.RDK.FWUPMGR` module |
| 8 | **Resource cleanup** | Thread join, mutex destroy, memory free on deinit — no leaks |

### What the Library Does NOT Do

- Does NOT perform actual firmware downloads (daemon does this)
- Does NOT interact with XConf servers (daemon does this)
- Does NOT flash firmware (daemon delegates to HAL)
- Does NOT manage reboots (daemon handles this)
- Does NOT persist state across process restarts
- Does NOT retry failed operations (client's responsibility)
- Does NOT own the logging lifecycle (`log_init`/`log_exit` are caller's job)

---

## 5. Daemon Responsibilities

For context, here is what the `rdkFwupdateMgr` daemon does (the other side of the D-Bus):

| # | Responsibility | Details |
|---|---------------|---------|
| 1 | **Process registration** | Assign unique handler_id, track client metadata, enforce one-registration-per-process-name |
| 2 | **XConf query** | HTTP request to XConf server with device model, current firmware version, MAC address |
| 3 | **Firmware download** | HTTP/HTTPS download from CDN, chunked transfer, integrity verification |
| 4 | **Flash coordination** | Write firmware to appropriate storage partition via device HAL |
| 5 | **Progress reporting** | Emit D-Bus signals with percentage and status as operations progress |
| 6 | **Multi-client orchestration** | Coalesce duplicate download requests (piggybacking), serialize flash operations |
| 7 | **Reboot management** | Coordinate post-flash reboot timing based on client's `rebootImmediately` flag |
| 8 | **Cleanup on client disconnect** | Handle ungraceful client exits (D-Bus name owner watching) |

---

## 6. High-Level Request Lifecycle

Every firmware update workflow follows this lifecycle:

```
┌─────────────────────────────────────────────────────────────────────┐
│                        CLIENT APPLICATION                            │
│                                                                      │
│   ┌──────────┐     ┌──────────────┐     ┌────────────┐     ┌──────┐│
│   │ REGISTER │────►│ CHECK UPDATE │────►│  DOWNLOAD  │────►│FLASH ││
│   └──────────┘     └──────────────┘     └────────────┘     └──────┘│
│        │                   │                    │               │    │
│        │            callback fires        callback fires   callback │
│        │            (once, with           (many times,     fires    │
│        │             firmware info)        with progress)   (many)  │
│        │                                                            │
│   ┌────────────┐                                                    │
│   │ UNREGISTER │◄───────────────────── (always, even on error) ────┘│
│   └────────────┘                                                    │
└─────────────────────────────────────────────────────────────────────┘
```

### Lifecycle States

```
   ┌────────────┐
   │  UNLINKED  │  Library loaded but not registered
   └─────┬──────┘
         │ registerProcess() succeeds
         ▼
   ┌────────────┐
   │ REGISTERED │  Handle valid, background thread running, ready for API calls
   └─────┬──────┘
         │ checkForUpdate() / downloadFirmware() / updateFirmware()
         ▼
   ┌────────────┐
   │   ACTIVE   │  One or more async operations pending
   └─────┬──────┘
         │ All callbacks have fired (completed/errored)
         ▼
   ┌────────────┐
   │ REGISTERED │  Back to idle, can call APIs again
   └─────┬──────┘
         │ unregisterProcess()
         ▼
   ┌────────────┐
   │  UNLINKED  │  Handle freed, thread stopped, library dormant
   └────────────┘
```

### Complete Sequence (Normal Path)

```
Time    Client App                   librdkFwupdateMgr           Daemon
─────   ──────────                   ─────────────────           ──────
T+0s    registerProcess()
          ├─────────────────── D-Bus: RegisterProcess ──────────► assigns handler_id
          │◄─────────────────── returns handler_id ──────────────┘
          ├─ internal_system_init()
          │   └─ spawns background thread
          │       ├─ subscribes to 3 signals
          │       └─ enters g_main_loop_run()
          └─ returns handle "12345"

T+1s    checkForUpdate(handle, cb)
          ├─ validates inputs
          ├─ registers cb in registry
          ├────────────── D-Bus: CheckForUpdate ──────────────────► queries XConf
          └─ returns SUCCESS immediately

T+15s                                                              XConf response arrives
                                    ◄── CheckForUpdateComplete ────┤
                                    dispatches cb(&fwinfo)
                                      └─► cb runs in BG thread

T+16s   downloadFirmware(handle, req, dl_cb)
          ├─ validates inputs
          ├─ registers dl_cb in dwnl registry
          ├────────── D-Bus: DownloadFirmware ────────────────────► starts download
          └─ returns SUCCESS

T+20s                                                              download 25%
                                    ◄── DownloadProgress(25%) ─────┤
                                    dispatches dl_cb(25, IN_PROGRESS)
T+40s                                                              download 100%
                                    ◄── DownloadProgress(100%) ────┤
                                    dispatches dl_cb(100, COMPLETED)
                                    resets slot to IDLE

T+41s   updateFirmware(handle, req, upd_cb)
          ├─ validates inputs
          ├─ registers upd_cb in update registry
          ├─────────── D-Bus: UpdateFirmware ─────────────────────► starts flash
          └─ returns SUCCESS

T+90s                                                              flash 100%
                                    ◄── UpdateProgress(100%) ──────┤
                                    dispatches upd_cb(100, COMPLETED)

T+91s   unregisterProcess(handle)
          ├─ internal_system_deinit()
          │   ├─ g_main_loop_quit()
          │   ├─ pthread_join()
          │   └─ free registries + mutexes
          ├────────── D-Bus: UnregisterProcess ───────────────────► removes ProcessInfo
          └─ free(handle)
```

---

## 7. Detailed Public API Reference

### 7.1 `registerProcess()`

```c
FirmwareInterfaceHandle registerProcess(const char *processName, const char *libVersion);
```

#### Purpose

Establishes a session with the firmware daemon. This is the mandatory first call before any other API can be used. It tells the daemon "I exist, here's my name, give me a session token."

#### Inputs

| Parameter | Type | Constraints | Example |
|-----------|------|-------------|---------|
| `processName` | `const char*` | Non-NULL, non-empty, ≤256 chars | `"VideoPlayer"` |
| `libVersion` | `const char*` | Non-NULL, ≤64 chars (empty OK) | `"1.0.0"` |

#### Output

| Return | Meaning |
|--------|---------|
| Non-NULL string (e.g., `"12345"`) | Success — this is your session handle |
| `NULL` | Failure — daemon not running, D-Bus error, or validation failure |

#### Return Code Semantics

This function doesn't use an enum return code — it returns the handle directly or `NULL`.

#### Internal Steps (What Happens Inside)

```
Step 1: Validate processName
        ├─ NULL check → FWUPMGR_ERROR, return NULL
        ├─ Empty check → FWUPMGR_ERROR, return NULL
        └─ Length check (>256) → FWUPMGR_ERROR, return NULL

Step 2: Validate libVersion
        ├─ NULL check → FWUPMGR_ERROR, return NULL
        └─ Length check (>64) → FWUPMGR_ERROR, return NULL

Step 3: Create D-Bus proxy
        ├─ g_bus_get_sync(G_BUS_TYPE_SYSTEM) → GDBusConnection
        │   └─ Failure: log error, return NULL
        ├─ g_dbus_proxy_new_sync() → GDBusProxy
        │   └─ Failure: log error, unref connection, return NULL
        └─ Unref connection (proxy holds its own reference)

Step 4: Call RegisterProcess D-Bus method
        ├─ g_dbus_proxy_call_sync("RegisterProcess", (ss), timeout=5000ms)
        │   └─ Failure: log D-Bus error message, unref proxy, return NULL
        ├─ Extract handler_id (uint64) from reply GVariant (t)
        └─ Unref result + proxy

Step 5: Allocate handle string
        ├─ malloc(32) → buffer for decimal string
        │   └─ Failure: CRITICAL — registration succeeded but can't return handle
        │       ├─ Create cleanup proxy
        │       ├─ Call UnregisterProcess(handler_id) to undo daemon-side registration
        │       └─ Return NULL
        └─ snprintf(buffer, 32, "%" PRIu64, handler_id)

Step 6: Start async engine
        ├─ internal_system_init()
        │   ├─ Initialize CallbackRegistry (mutex + zero array)
        │   ├─ Initialize DwnlCallbackRegistry (mutex + zero array)
        │   ├─ Initialize UpdateCbRegistry (mutex + zero array)
        │   ├─ Create GMainContext (isolated from app's GLib)
        │   ├─ Create GMainLoop
        │   ├─ pthread_create(background_thread_func)
        │   └─ Spin-wait (max 5s) until bg thread sets running=true
        └─ Return handle string
```

#### Error Handling

| Error Condition | Action | User-Visible Effect |
|----------------|--------|---------------------|
| NULL/empty processName | Log error, return NULL immediately | No D-Bus call made |
| D-Bus system bus unavailable | Log connection error, return NULL | Daemon may not be installed |
| Daemon not responding (timeout) | Log timeout, return NULL | Daemon may be crashed/overloaded |
| Daemon rejects registration | Log D-Bus error message, return NULL | Process name conflict or internal error |
| malloc failure after success | Best-effort UnregisterProcess, return NULL | Extremely rare (OOM condition) |
| Background thread fails to start | Log error, return NULL | System resource exhaustion |

#### Thread Safety

- Fully thread-safe for concurrent calls (different process names)
- GDBus synchronous calls are internally thread-safe
- No shared state until `internal_system_init()` creates registries (which are mutex-protected)

#### Memory Ownership

| Who | Owns What |
|-----|-----------|
| Library | The returned handle string (malloc'd) |
| Caller | NOTHING — do not free the handle; call `unregisterProcess()` instead |

#### Typical Caller Usage

```c
#include "rdkFwupdateMgr_client.h"

int main(void) {
    FirmwareInterfaceHandle handle = registerProcess("MyPlugin", "2.1.0");
    if (handle == NULL) {
        fprintf(stderr, "Failed to register with daemon. Is it running?\n");
        return EXIT_FAILURE;
    }
    
    printf("Registered! Handle: %s\n", handle);
    
    // ... use other APIs with this handle ...
    
    unregisterProcess(handle);  // MUST call this before exit
    return EXIT_SUCCESS;
}
```

---

### 7.2 `checkForUpdate()`

```c
CheckForUpdateResult checkForUpdate(FirmwareInterfaceHandle handle, UpdateEventCallback callback);
```

#### Purpose

Initiates a non-blocking firmware availability check. The daemon queries the XConf cloud server to determine if a newer firmware version exists for this device. The result is delivered asynchronously via your callback — this function returns immediately.

#### Inputs

| Parameter | Type | Constraints | Example |
|-----------|------|-------------|---------|
| `handle` | `FirmwareInterfaceHandle` | Non-NULL, non-empty, from `registerProcess()` | `"12345"` |
| `callback` | `UpdateEventCallback` | Non-NULL function pointer | `my_check_callback` |

#### Callback Signature

```c
typedef void (*UpdateEventCallback)(const FwInfoData *fwinfodata);
```

The callback receives:
```c
typedef struct {
    char CurrFWVersion[64];           // Current firmware version on device
    UpdateDetails *UpdateDetails;     // Non-NULL only when status == FIRMWARE_AVAILABLE
    CheckForUpdateStatus status;      // Result enum
} FwInfoData;

typedef struct {
    char FwFileName[128];             // e.g., "firmware_v2.0.bin"
    char FwUrl[512];                  // Download URL
    char FwVersion[64];               // Available firmware version
    char RebootImmediately[12];       // "true" or "false"
    char DelayDownload[8];            // "true" or "false"
    char PDRIVersion[64];             // PDRI image version (may be empty)
    char PeripheralFirmwares[256];    // Peripheral versions (may be empty)
} UpdateDetails;
```

#### Output

| Return Value | Meaning |
|-------------|---------|
| `CHECK_FOR_UPDATE_SUCCESS` (0) | Request sent to daemon. Callback will fire later. |
| `CHECK_FOR_UPDATE_FAIL` (1) | Request could not be sent. No callback will fire. |

**Critical**: `SUCCESS` does NOT mean firmware is available. It means the request was accepted. Actual firmware availability comes through the callback.

#### Internal Steps

```
Step 1: Validate handle (not NULL, not empty) → FAIL on error
Step 2: Validate callback (not NULL) → FAIL on error
Step 3: Connect to D-Bus system bus
        └─ Failure: return FAIL (no stale registry entry created)
Step 4: Register callback in CallbackRegistry (slot: IDLE → PENDING)
        └─ Failure (registry full, 30 slots): unref connection, return FAIL
Step 5: Fire-and-forget g_dbus_connection_call("CheckForUpdate", (s)handle)
        └─ No reply expected — returns immediately
Step 6: Unref D-Bus connection
Step 7: Return CHECK_FOR_UPDATE_SUCCESS
```

#### Error Handling

| Error | Action | Callback Fires? |
|-------|--------|----------------|
| Invalid handle | Return FAIL immediately | No |
| NULL callback | Return FAIL immediately | No |
| D-Bus connection failure | Return FAIL, no registry entry | No |
| Registry full (30 slots) | Return FAIL, cleanup connection | No |
| Daemon crashes after call sent | Callback never fires (client should timeout) | No — client uses condvar timeout |

#### Thread Safety

- Safe to call from multiple threads concurrently (registry mutex protects slot allocation)
- Same handle can have only one pending check at a time (existing slot is overwritten)
- Callback fires in the library's background thread, NOT the caller's thread

#### Memory Ownership

| Data | Lifetime | Owner |
|------|----------|-------|
| `FwInfoData*` passed to callback | Valid ONLY during callback execution | Library (stack-allocated in dispatch function) |
| `UpdateDetails*` inside FwInfoData | Valid ONLY during callback execution | Library (stack-allocated) |
| All strings in FwInfoData | Valid ONLY during callback execution | Library |

**Rule**: If you need data after the callback returns, `strncpy()` it to your own buffers before the callback returns.

#### Typical Caller Usage

```c
static pthread_mutex_t check_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  check_cond  = PTHREAD_COND_INITIALIZER;
static int             check_done  = 0;
static CheckForUpdateStatus check_result;

void my_check_callback(const FwInfoData *fwinfo) {
    // This runs in BACKGROUND THREAD — copy what you need, signal main thread
    pthread_mutex_lock(&check_mutex);
    check_result = fwinfo->status;
    check_done = 1;
    pthread_cond_signal(&check_cond);
    pthread_mutex_unlock(&check_mutex);
}

// In main thread:
CheckForUpdateResult rc = checkForUpdate(handle, my_check_callback);
if (rc != CHECK_FOR_UPDATE_SUCCESS) {
    // Handle error — callback will NOT fire
    return;
}

// Wait for callback with 2-minute timeout
struct timespec timeout;
clock_gettime(CLOCK_REALTIME, &timeout);
timeout.tv_sec += 120;

pthread_mutex_lock(&check_mutex);
while (!check_done) {
    if (pthread_cond_timedwait(&check_cond, &check_mutex, &timeout) != 0) {
        // Timeout — XConf query took too long
        break;
    }
}
pthread_mutex_unlock(&check_mutex);
```

---

### 7.3 `downloadFirmware()`

```c
DownloadResult downloadFirmware(FirmwareInterfaceHandle handle,
                                const FwDwnlReq *fwdwnlreq,
                                DownloadCallback callback);
```

#### Purpose

Initiates a non-blocking firmware image download. The daemon downloads the specified firmware file from the CDN and reports progress through repeated callback invocations. Returns immediately.

#### Inputs

| Parameter | Type | Constraints |
|-----------|------|-------------|
| `handle` | `FirmwareInterfaceHandle` | Non-NULL, non-empty |
| `fwdwnlreq` | `const FwDwnlReq*` | Non-NULL; `firmwareName` must be non-NULL and non-empty |
| `callback` | `DownloadCallback` | Non-NULL |

```c
typedef struct {
    const char *firmwareName;      // REQUIRED: "firmware_v2.bin"
    const char *downloadUrl;       // OPTIONAL: NULL or "" → daemon uses XConf URL
    const char *TypeOfFirmware;    // OPTIONAL: "PCI", "PDRI", or "PERIPHERAL"
} FwDwnlReq;
```

#### Callback Signature

```c
typedef void (*DownloadCallback)(int download_progress, DownloadStatus fwdwnlstatus);
```

Called **multiple times**:
- `(10, DWNL_IN_PROGRESS)` — 10% done
- `(50, DWNL_IN_PROGRESS)` — halfway
- `(100, DWNL_COMPLETED)` — finished successfully
- OR `(X, DWNL_ERROR)` — failed at X%

#### Output

| Return Value | Meaning |
|-------------|---------|
| `RDKFW_DWNL_SUCCESS` (0) | Download request sent. Callbacks will fire. |
| `RDKFW_DWNL_FAILED` (1) | Could not send request. No callbacks will fire. |

#### Internal Steps

```
Step 1: Validate handle, fwdwnlreq, fwdwnlreq->firmwareName, callback
Step 2: Connect to D-Bus system bus
Step 3: Register callback in DwnlCallbackRegistry (slot: IDLE → ACTIVE)
Step 4: Fire-and-forget: DownloadFirmware(s handle, s firmwareName, s url, s type)
        ├─ url defaults to "" if NULL
        └─ type defaults to "" if NULL
Step 5: Unref connection, return SUCCESS
```

#### Key Difference from checkForUpdate()

| Aspect | checkForUpdate | downloadFirmware |
|--------|---------------|-----------------|
| Callback fires | Once | Multiple times (every progress signal) |
| Registry slot lifecycle | PENDING → DISPATCHED → IDLE | ACTIVE → ACTIVE → ... → IDLE |
| Slot reset trigger | After single dispatch | Only on DWNL_COMPLETED or DWNL_ERROR |

#### Thread Safety

- Safe for concurrent calls (own registry with own mutex)
- Same handle calling `downloadFirmware()` twice overwrites the previous slot (prevents stale callbacks)

#### Typical Caller Usage

```c
void my_download_cb(int progress, DownloadStatus status) {
    printf("Download: %d%% [%s]\n", progress,
           status == DWNL_COMPLETED ? "DONE" :
           status == DWNL_ERROR ? "ERROR" : "IN_PROGRESS");
    
    if (status == DWNL_COMPLETED || status == DWNL_ERROR) {
        // Signal main thread — download finished
        pthread_mutex_lock(&dl_mutex);
        dl_done = 1;
        dl_status = status;
        pthread_cond_signal(&dl_cond);
        pthread_mutex_unlock(&dl_mutex);
    }
}

FwDwnlReq req = {
    .firmwareName = "firmware_v2.bin",
    .downloadUrl = NULL,            // Let daemon use XConf URL
    .TypeOfFirmware = "PCI"
};

DownloadResult rc = downloadFirmware(handle, &req, my_download_cb);
```

---

### 7.4 `updateFirmware()`

```c
UpdateResult updateFirmware(FirmwareInterfaceHandle handle,
                            const FwUpdateReq *fwupdatereq,
                            UpdateCallback callback);
```

#### Purpose

Initiates non-blocking firmware flashing. The daemon writes the previously downloaded firmware image to the device's storage partition. Progress is reported through repeated callback invocations.

**WARNING**: This operation modifies device firmware. It is irreversible once the flash begins.

#### Inputs

| Parameter | Type | Constraints |
|-----------|------|-------------|
| `handle` | `FirmwareInterfaceHandle` | Non-NULL, non-empty |
| `fwupdatereq` | `const FwUpdateReq*` | Non-NULL; `firmwareName` and `TypeOfFirmware` required |
| `callback` | `UpdateCallback` | Non-NULL |

```c
typedef struct {
    const char *firmwareName;         // REQUIRED: must match downloaded file
    const char *TypeOfFirmware;       // REQUIRED: "PCI", "PDRI", or "PERIPHERAL"
    const char *LocationOfFirmware;   // OPTIONAL: NULL → use /etc/device.properties default
    bool rebootImmediately;           // true → device reboots when flash completes
} FwUpdateReq;
```

#### Callback Signature

```c
typedef void (*UpdateCallback)(int update_progress, UpdateStatus fwupdatestatus);
```

#### Output

| Return Value | Meaning |
|-------------|---------|
| `RDKFW_UPDATE_SUCCESS` (0) | Flash request sent. Callbacks will fire. |
| `RDKFW_UPDATE_FAILED` (1) | Could not send request. No callbacks. |

#### D-Bus Wire Format

The library converts the `FwUpdateReq` struct to a D-Bus method call with signature `(sssss)`:
```
s  handle                          — "12345"
s  firmwareName                    — "firmware_v2.bin"
s  LocationOfFirmware              — "/opt/CDL" (or "" if NULL)
s  TypeOfFirmware                  — "PCI"
s  rebootImmediately               — "true" or "false" (string, not bool!)
```

Note: `rebootImmediately` is a `bool` in the struct but transmitted as a string because the daemon's D-Bus method expects string arguments.

#### Thread Safety

Same as `downloadFirmware()` — own registry, own mutex, safe for concurrent use.

---

### 7.5 `unregisterProcess()`

```c
void unregisterProcess(FirmwareInterfaceHandle handler);
```

#### Purpose

Terminates the session with the daemon, stops the background thread, frees all library resources, and frees the handle memory. This is the mandatory last call.

#### Inputs

| Parameter | Type | Constraints |
|-----------|------|-------------|
| `handler` | `FirmwareInterfaceHandle` | May be NULL (no-op) |

#### Output

Returns `void`. This function always succeeds from the caller's perspective (best-effort cleanup).

#### Internal Steps

```
Step 1: NULL check → if NULL, log info and return (no-op, safe)

Step 2: Parse handle string → uint64 handler_id
        ├─ Uses strtoull() with strict endptr validation
        ├─ Rejects: "123abc", " 123", "", "abc", overflow
        └─ On invalid: FWUPMGR_ERROR, free(handler), return

Step 3: internal_system_deinit()
        ├─ g_main_loop_quit() → background thread wakes up from g_main_loop_run()
        ├─ pthread_join() → wait for background thread to exit cleanly
        ├─ g_main_loop_unref() + g_main_context_unref()
        ├─ internal_dwnl_system_deinit() → free download registry
        ├─ internal_update_system_deinit() → free update registry
        ├─ Free all remaining handle_key strings in check registry
        └─ pthread_mutex_destroy() × 4 (bg_thread, registry, dwnl, update)

Step 4: Create D-Bus proxy (best-effort — may fail if daemon is already gone)

Step 5: Call UnregisterProcess(t handler_id) on daemon
        ├─ Success: daemon removes ProcessInfo
        └─ Failure: logged but ignored (daemon may have crashed/restarted)

Step 6: free(handler) — the string is released regardless of D-Bus call outcome
```

#### Error Handling

This function is **deliberately tolerant of errors**:

| Error | Action | Cleanup Continues? |
|-------|--------|-------------------|
| NULL handle | No-op, return | N/A |
| Invalid handle string | Log error, free handle, return | Yes |
| D-Bus proxy creation fails | Log warning, continue | Yes — free(handle) still happens |
| UnregisterProcess D-Bus call fails | Log warning, continue | Yes — free(handle) still happens |
| Daemon already crashed | D-Bus call times out, logged | Yes — local cleanup still happens |

#### Why `internal_system_deinit()` Before D-Bus Call?

1. After unregistering, daemon stops sending signals → background thread is useless
2. `pthread_join()` returns immediately since `g_main_loop_quit()` unblocks the thread
3. If we sent D-Bus first and the daemon is slow, the background thread would sit idle waiting for signals that will never come

#### Memory Ownership

After `unregisterProcess()` returns:
- The handle pointer is **freed and invalid** — do not use it
- All background resources are released
- Library is back to "unlinked" state — `registerProcess()` can be called again if needed

#### Typical Caller Usage

```c
// Always call before exit, even on error paths
cleanup:
    if (handle != NULL) {
        unregisterProcess(handle);
        handle = NULL;  // Defensive: mark as invalid
    }
    return exit_code;
```

---

## 8. Internal Helper Modules

### 8.1 Module: `rdkFwupdateMgr_async.c` — The Async Engine

This is the core internal module. It owns:

| Component | Purpose |
|-----------|---------|
| `g_registry` (CallbackRegistry) | Stores pending `checkForUpdate` callbacks |
| `g_dwnl_registry` (DwnlCallbackRegistry) | Stores active download callbacks |
| `g_update_registry` (UpdateCbRegistry) | Stores active update callbacks |
| `g_bg_thread` (BackgroundThread) | Holds thread handle, GMainLoop, connection, subscription IDs |

#### Key Internal Functions

| Function | Called By | Purpose |
|----------|-----------|---------|
| `internal_system_init()` | `registerProcess()` | Start everything: registries + thread |
| `internal_system_deinit()` | `unregisterProcess()` | Stop everything: thread + registries |
| `internal_register_callback()` | `checkForUpdate()` | Add callback to check registry |
| `internal_dwnl_register_callback()` | `downloadFirmware()` | Add callback to download registry |
| `internal_update_register_callback()` | `updateFirmware()` | Add callback to update registry |
| `background_thread_func()` | `pthread_create()` | Thread entry point: connect + subscribe + loop |
| `on_check_complete_signal()` | GLib (signal dispatch) | Parse signal → dispatch callbacks |
| `on_download_progress_signal()` | GLib (signal dispatch) | Parse signal → dispatch callbacks |
| `on_update_progress_signal()` | GLib (signal dispatch) | Parse signal → dispatch callbacks |
| `dispatch_all_pending()` | `on_check_complete_signal()` | Two-phase dispatch for check callbacks |
| `dispatch_all_dwnl_active()` | `on_download_progress_signal()` | Two-phase dispatch for download callbacks |
| `dispatch_all_update_active()` | `on_update_progress_signal()` | Two-phase dispatch for update callbacks |
| `parse_update_details()` | `dispatch_all_pending()` | Parse pipe-separated firmware details string |

### 8.2 Module: `rdkFwupdateMgr_process.c` — Registration Logic

Contains `registerProcess()` and `unregisterProcess()` plus two helpers:

| Function | Purpose |
|----------|---------|
| `create_dbus_proxy()` | Create a GDBusProxy connected to daemon (used by both register and unregister) |
| `validate_process_name()` | NULL, empty, and length checks |
| `validate_lib_version()` | NULL and length checks |

### 8.3 Module: `rdkFwupdateMgr_log.h` — Logging Macros

Header-only. Defines `FWUPMGR_*` macros. See [Section 12](#12-logging-architecture) for full details.

### 8.4 Module: `rdkFwupdateMgr_async_internal.h` — Internal Types

Header for internal use only. Defines:
- All `typedef struct` types for registries and signal data
- All `internal_*` function declarations
- D-Bus constants (`DBUS_SERVICE_NAME`, etc.)
- Architecture ASCII diagrams in comments

---

## 9. IPC Communication Model

### 9.1 Transport: D-Bus System Bus

| Property | Value |
|----------|-------|
| Bus type | System bus (`G_BUS_TYPE_SYSTEM`) |
| Well-known name | `org.rdkfwupdater.Service` |
| Object path | `/org/rdkfwupdater/Service` |
| Interface | `org.rdkfwupdater.Interface` |

### 9.2 Connection Pattern: Ephemeral Per-Call

```
registerProcess():       [Connect] → [Call] → [Disconnect]  (unique sender :1.140)
checkForUpdate():        [Connect] → [Call] → [Disconnect]  (unique sender :1.141)
downloadFirmware():      [Connect] → [Call] → [Disconnect]  (unique sender :1.142)
updateFirmware():        [Connect] → [Call] → [Disconnect]  (unique sender :1.143)
unregisterProcess():     [Connect] → [Call] → [Disconnect]  (unique sender :1.145)
```

**Background thread**: Has its OWN **persistent** connection for signal subscriptions. This connection lives for the entire library lifecycle.

### 9.3 Method Calls (Client → Daemon)

| Method | GVariant Signature | Direction | Blocking? |
|--------|-------------------|-----------|-----------|
| `RegisterProcess` | IN: `(ss)` OUT: `(t)` | Synchronous | Yes (5s timeout) |
| `UnregisterProcess` | IN: `(t)` OUT: `(b)` | Synchronous | Yes (5s timeout) |
| `CheckForUpdate` | IN: `(s)` OUT: none | Fire-and-forget | No |
| `DownloadFirmware` | IN: `(ssss)` OUT: none | Fire-and-forget | No |
| `UpdateFirmware` | IN: `(sssss)` OUT: none | Fire-and-forget | No |

### 9.4 Signals (Daemon → Client)

| Signal | GVariant Signature | Delivery |
|--------|-------------------|----------|
| `CheckForUpdateComplete` | `(tiissss)` | Once per check |
| `DownloadProgress` | `(tsuss)` | Repeated (per progress %) |
| `UpdateProgress` | `(tsiis)` | Repeated (per progress %) |

### 9.5 Why Fire-and-Forget for Async Operations?

For `checkForUpdate`, `downloadFirmware`, and `updateFirmware`:
- The actual work takes seconds to minutes
- Blocking the caller for that duration defeats the purpose
- The daemon acknowledges receipt implicitly by starting work
- Results come as signals (broadcast notifications)
- If the call fails at D-Bus level, `g_dbus_connection_call()` still returns successfully (message queued) — the failure manifests as no signal ever arriving

---

## 10. Retry and Timeout Strategy

### 10.1 Library-Side Timeouts

| Operation | Timeout | Location | Behavior on Timeout |
|-----------|---------|----------|---------------------|
| `RegisterProcess` D-Bus call | 5000ms | `DBUS_TIMEOUT_MS` constant | Returns NULL with error log |
| `UnregisterProcess` D-Bus call | 5000ms | Same constant | Logs warning, continues cleanup |
| Fire-and-forget calls | 5000ms | Same constant | GLib queues message; timeout only applies to queueing |
| Background thread startup | 5000ms | `internal_system_init()` spin-wait | Continues anyway (first API call will fail) |

### 10.2 Library-Side Retry Policy

**The library does NOT retry.** This is a deliberate design decision:

- Retry logic belongs in the **caller**, not the transport layer
- The caller knows the right retry interval and max attempts for their use case
- A monitoring daemon might retry every 60 seconds; a user-facing app might retry once after 5 seconds
- Silent retries inside the library would hide failures from the caller

### 10.3 Caller-Side Timeout Guidance

| Operation | Recommended Timeout | Rationale |
|-----------|-------------------|-----------|
| `checkForUpdate` callback | 120 seconds (2 min) | XConf HTTP query + network latency |
| `downloadFirmware` callback | 300 seconds (5 min) | Large firmware images over cellular |
| `updateFirmware` callback | 600 seconds (10 min) | Flash operations vary by storage type |

The `example_app.c` uses exactly these values with `pthread_cond_timedwait()`.

### 10.4 Callback Timeout Detection (Internal)

Each registry entry stores a `registered_time` timestamp. The async engine could use this for internal timeout detection (sweeping stale entries). Currently, timeout detection is NOT actively enforced — the `TIMED_OUT` state exists in the state machine but no sweeper thread runs.

**Recommendation for future work**: Add a periodic sweep (every 60s) in the background thread via `g_timeout_add()` to reset stale PENDING/ACTIVE entries that have exceeded `CALLBACK_TIMEOUT_SECONDS` (60s).

---

## 11. Failure Scenarios and Recovery

### 11.1 Daemon Not Running

| Symptom | Detection | Recovery |
|---------|-----------|----------|
| `registerProcess()` returns NULL | D-Bus error: "The name org.rdkfwupdater.Service was not provided by any .service files" | Client logs error, retries, or exits |
| Fire-and-forget calls appear to succeed | No signal ever arrives | Client's condvar times out |

### 11.2 Daemon Crashes Mid-Operation

| Symptom | Detection | Recovery |
|---------|-----------|----------|
| No more signals arrive | Client's condvar times out | Client calls `unregisterProcess()` (best-effort), then re-registers |
| Background thread's D-Bus connection emits "closed" signal | Not currently handled | **Future work**: detect and propagate error to pending callbacks |

### 11.3 D-Bus System Bus Restart

| Symptom | Detection | Recovery |
|---------|-----------|----------|
| All D-Bus connections become invalid | Next API call fails at `g_bus_get_sync()` | Client calls `unregisterProcess()`, waits, tries `registerProcess()` again |

### 11.4 Client Crashes Without Unregistering

| Symptom | Detection | Recovery |
|---------|-----------|----------|
| Daemon holds stale ProcessInfo | Daemon watches D-Bus name owner changes (NameOwnerChanged signal) | Daemon auto-removes registration when client's bus name disappears |

**Note**: Due to the per-call connection model, this detection is unreliable — each API call has a different bus name that immediately disappears after the call. The daemon relies on explicit `UnregisterProcess` or periodic cleanup.

### 11.5 Registry Full (30 Slots)

| Symptom | Detection | Recovery |
|---------|-----------|----------|
| `checkForUpdate`/`downloadFirmware`/`updateFirmware` returns FAIL | `internal_*_register_callback()` returns false | Client should wait for pending operations to complete, then retry |

### 11.6 Signal Arrives Before Callback Registered

| Cause | Prevention | Impact |
|-------|-----------|--------|
| Daemon responds extremely fast | Library registers callback BEFORE sending D-Bus call | Cannot happen with current ordering |
| Connect → Register → Send ordering eliminates this race | N/A | N/A |

---

## 12. Logging Architecture

### 12.1 Three-Module Design

```
┌──────────────────────────────────────────────────┐
│                   LOG OUTPUT                      │
│  /opt/logs/rdkFwupdateMgr.log                   │
├──────────────────────────────────────────────────┤
│  [EXAMPLE] App-level messages                    │  ← Client app
│  [FWUPMGR] Library internal messages             │  ← This library
│  [FWUPG]   Daemon operational messages           │  ← Daemon
└──────────────────────────────────────────────────┘
```

| RDK_LOGGER Module | Macro Prefix | Source Files | Purpose |
|------------------|--------------|--------------|---------|
| `LOG.RDK.FWUPMGR` | `FWUPMGR_*` | `_process.c`, `_api.c`, `_async.c` | Library internals |
| `LOG.RDK.FWUPG` | `SWLOG_*` | Daemon sources | Daemon operations |
| `LOG.RDK.EXAMPLE` | `EXAMPLE_*` | `example_app.c` | Example client app |

### 12.2 Log Levels Used

| Level | Macro | Usage |
|-------|-------|-------|
| TRACE | `FWUPMGR_TRACE` | Not used currently (reserved for future verbose tracing) |
| DEBUG | `FWUPMGR_DEBUG` | Detailed internal state (registry operations, GVariant parsing) |
| INFO | `FWUPMGR_INFO` | Normal operation flow (API entry/exit, handle values, signal reception) |
| WARN | `FWUPMGR_WARN` | Recoverable issues (D-Bus call failed in unregister, daemon already gone) |
| ERROR | `FWUPMGR_ERROR` | Failures that cause API to return error code (validation, connection, registry full) |
| FATAL | `FWUPMGR_FATAL` | Not used currently (reserved for unrecoverable states) |

### 12.3 Log Lifecycle Ownership

```c
// CALLER'S responsibility — library never calls these:
log_init();                        // Before any library API call
// ... use library ...
log_exit();                        // After unregisterProcess()
```

**Rationale**: A process should call `log_init()` exactly once. If both the library and the app call it, double-initialization could corrupt state.

### 12.4 Macro Implementation

```c
// rdkFwupdateMgr_log.h (when RDK_LOGGER is defined):
#define FWUPMGR_LOG(level, module, format, ...) \
    RDK_LOG(level, module, format, ##__VA_ARGS__)

#define FWUPMGR_INFO(format, ...) \
    FWUPMGR_LOG(RDK_LOG_INFO, "LOG.RDK.FWUPMGR", format, ##__VA_ARGS__)

// Client apps reuse the base macro with their own module:
#define MYAPP_INFO(format, ...) \
    FWUPMGR_LOG(RDK_LOG_INFO, "LOG.RDK.MYAPP", format, ##__VA_ARGS__)
```

### 12.5 Non-RDK_LOGGER Fallback

For unit tests and development builds without RDK_LOGGER:
```c
#define FWUPMGR_LOG(level, module, FORMAT...) fprintf(stderr, "[%s] " FORMAT, module)
```

### 12.6 Build Dependencies

```makefile
# In Makefile.am for any target that uses logging:
target_CFLAGS += -I${top_srcdir}/librdkFwupdateMgr/src       # rdkFwupdateMgr_log.h
target_CFLAGS += -I${top_srcdir}/common_utilities/utils       # rdkv_cdl_log_wrapper.h
target_LDADD  += -lfwutils -lrdkloggers                      # Runtime implementations
```

---

## 13. Security Considerations

### 13.1 D-Bus Policy Enforcement

Access control is enforced at the D-Bus level via policy files:
```xml
<!-- /etc/dbus-1/system.d/rdkFwupdateMgr.conf -->
<policy user="root">
    <allow send_destination="org.rdkfwupdater.Service"/>
    <allow receive_sender="org.rdkfwupdater.Service"/>
</policy>
```

Only processes running as the configured user (typically root or a specific service account) can call methods or receive signals.

### 13.2 Handler ID as Authorization Token

The `handler_id` (e.g., `"12345"`) serves as the session authorization token:
- It's a daemon-generated 64-bit value (not guessable from outside)
- Required for all operations after registration
- The daemon validates that the handler_id exists before processing any request
- In this library model, sender-ID validation is not used (see Section 9.2 for why)

**Limitation**: The handler_id is a sequential counter (not cryptographically random). In a multi-client environment, a malicious client could potentially guess another client's handler_id. For production hardening, consider using a random 128-bit token.

### 13.3 Input Validation at Library Boundary

All public API functions validate inputs before forwarding to D-Bus:
- NULL pointer checks on all parameters
- Empty string rejection
- Length limit enforcement (prevents buffer overflow in daemon's fixed-size buffers)
- Numeric parse validation in `unregisterProcess()` (strict `strtoull` with endptr check)

### 13.4 No Credential Storage

The library does not store, cache, or transmit any credentials. Authentication is entirely handled by D-Bus policy and daemon-side process tracking.

### 13.5 Memory Safety

- No `sprintf()` usage — all string formatting uses `snprintf()` with bounds
- All `strncpy()` usage explicitly null-terminates the destination
- Handle memory is freed exactly once (in `unregisterProcess()`)
- Registry entries are freed on slot reset — no dangling pointers
- GLib objects are unreffed on all code paths (including error paths)

---

## 14. Performance Considerations

### 14.1 Overhead Per API Call

| Operation | Typical Latency | Bottleneck |
|-----------|----------------|------------|
| `registerProcess()` | 5-10ms | D-Bus round-trip |
| `checkForUpdate()` | <1ms (returns immediately) | D-Bus message queueing |
| `downloadFirmware()` | <1ms (returns immediately) | D-Bus message queueing |
| `updateFirmware()` | <1ms (returns immediately) | D-Bus message queueing |
| `unregisterProcess()` | 10-15ms | pthread_join + D-Bus round-trip |

### 14.2 Memory Footprint

| Component | Size | Notes |
|-----------|------|-------|
| Three registries (static arrays) | ~30 × 3 × (272 bytes per entry) ≈ 24 KB | Stack-like, no heap fragmentation |
| Background thread stack | 8 MB default (configurable) | Linux thread default |
| GMainLoop + GMainContext | ~2 KB | GLib internal |
| D-Bus connection (persistent) | ~4 KB | Background thread's connection |
| Per-call D-Bus proxy | ~2 KB | Freed immediately after call |

**Total resident overhead**: ~35 KB (excluding thread stack, which is virtual memory only)

### 14.3 Connection Overhead

The per-call connection model has overhead:
- Each `g_bus_get_sync()` call performs a D-Bus handshake
- Each gets a unique sender name allocated by dbus-daemon
- Connection teardown releases the name

For infrequent firmware operations (minutes/hours between calls), this overhead is negligible. If call frequency were high (>10/second), a persistent connection pool would be warranted.

### 14.4 Signal Dispatch Efficiency

The two-phase dispatch creates a stack-local snapshot array on every signal. With `MAX_PENDING_CALLBACKS=30` and ~272 bytes per snapshot entry, this is ~8KB of stack usage — well within typical 8MB thread stack limits.

The linear scan of 30 entries is O(30) — negligible for this use case. A hash map would be over-engineering.

---

## 15. Scalability for Multiple Clients

### 15.1 Current Capacity

| Resource | Limit | Constraint |
|----------|-------|------------|
| Concurrent registered processes | Limited by daemon's `registered_processes` hash table | Effectively unbounded (GHashTable) |
| Concurrent pending checkForUpdate callbacks | 30 | `MAX_PENDING_CALLBACKS` constant |
| Concurrent active downloads | 30 | Same constant for download registry |
| Concurrent active updates | 30 | Same constant for update registry |

### 15.2 Multi-Client Signal Delivery

D-Bus signals are **broadcast** — all connected clients receive them. The library's dispatch logic handles this:

```
Daemon emits CheckForUpdateComplete:
  ├─ Client A's background thread receives it → dispatches to Client A's callback
  ├─ Client B's background thread receives it → dispatches to Client B's callback
  └─ Client C's background thread receives it → dispatches to Client C's callback
```

Each client has its own library instance (separate `.so` loaded into its process space), its own background thread, its own registries. They are completely independent.

### 15.3 Daemon-Side Coalescing

For download operations, the daemon implements **piggybacking**: if multiple clients request the same firmware file simultaneously, the daemon downloads it once and sends progress signals to all registered clients. The library dispatches these signals to all ACTIVE download callbacks transparently.

### 15.4 Scaling Limitations

- If more than 30 checkForUpdate calls are pending simultaneously (across the same process), the 31st will fail with `CHECK_FOR_UPDATE_FAIL`. This is unlikely in practice — 30 concurrent firmware checks from one process would be a design error.
- The background thread is single-threaded — if a callback takes a long time (e.g., client does heavy processing in the callback), other signals queue up in the GMainContext. Clients should keep callbacks short and signal their main thread for heavy work.

---

## 16. Future Extensibility

### 16.1 Potential New APIs

| API | Purpose | Priority |
|-----|---------|----------|
| `cancelDownload(handle)` | Abort an in-progress download | High |
| `pauseDownload(handle)` / `resumeDownload(handle)` | Pause/resume for network bandwidth management | Medium |
| `getUpdateStatus(handle)` | Synchronous poll of current state (for apps that don't want callbacks) | Medium |
| `setUpdatePolicy(handle, policy)` | Configure auto-update behavior per client | Low |
| `subscribeToDeviceState(handle, cb)` | Get notified of device firmware state changes (not just self-initiated) | Low |

### 16.2 HAL Integration (Planned)

The `UpdateCallback` signature documentation notes:
> "The signature and behavior of this callback may change in future versions when HAL (Hardware Abstraction Layer) APIs become available."

When HAL is integrated:
- More granular progress reporting (per-partition)
- Device-specific status codes
- Verification step callbacks (checksum validation)

### 16.3 Persistent Connection Model (If Needed)

If call frequency increases or the daemon implements per-connection state:
- Replace per-call `g_bus_get_sync()` with a connection pool
- Add reconnection logic on connection drop
- Add heartbeat/keepalive mechanism

### 16.4 User-Data in Callbacks

Current callback signatures don't include a `void *user_data` parameter (e.g., `UpdateEventCallback` takes only `const FwInfoData*`). Adding user_data would:
- Eliminate the need for global variables in client apps
- Allow multiple independent sessions in one process
- Be an ABI-breaking change (major version bump)

### 16.5 Async/Await Style (C11 Atomics)

For modern C codebases, consider offering a "future" API alongside callbacks:
```c
// Hypothetical future API:
FwCheckFuture *future = checkForUpdateAsync(handle);
// ... do other work ...
FwInfoData *result = awaitFwCheck(future, timeout_ms);  // blocks until ready
freeFwCheckFuture(future);
```

This would be a convenience wrapper over the existing callback mechanism.

---

## Appendix A: Complete D-Bus Interface Contract

```
Service:   org.rdkfwupdater.Service
Path:      /org/rdkfwupdater/Service
Interface: org.rdkfwupdater.Interface

METHODS:
  RegisterProcess(s processName, s libVersion) → (t handler_id)
  UnregisterProcess(t handler_id) → (b success)
  CheckForUpdate(s handle)
  DownloadFirmware(s handle, s firmwareName, s downloadUrl, s typeOfFirmware)
  UpdateFirmware(s handle, s firmwareName, s location, s type, s rebootImmediately)

SIGNALS:
  CheckForUpdateComplete(t handler_id, i result_code, i status_code,
                         s current_version, s available_version,
                         s update_details, s status_message)
  
  DownloadProgress(t handler_id, s firmware_name, u progress_percent,
                   s status_string, s message)
  
  UpdateProgress(t handler_id, s firmware_name, i progress_percent,
                 i status_code, s message)
```

---

## Appendix B: End-to-End Sequence Diagram

```
  Client App (main)          librdkFwupdateMgr          BG Thread           Daemon
  ─────────────────          ─────────────────          ─────────           ──────
        │                           │                       │                  │
        │  registerProcess()        │                       │                  │
        │──────────────────────────►│                       │                  │
        │                           │── D-Bus: Register ───────────────────────►│
        │                           │◄──── handler_id ─────────────────────────┤
        │                           │── internal_system_init()                 │
        │                           │   └─ pthread_create() ──►│               │
        │                           │                          │─ subscribe ──►│
        │                           │                          │ signals       │
        │                           │◄── running=true ─────────┤               │
        │◄── handle "12345" ────────┤                          │               │
        │                           │                          │               │
        │  checkForUpdate(h, cb)    │                          │               │
        │──────────────────────────►│                          │               │
        │                           │── register cb in reg.    │               │
        │                           │── D-Bus: CheckForUpdate ─────────────────►│
        │◄── SUCCESS ───────────────┤                          │               │
        │                           │                          │               │
        │  [waiting on condvar]     │                          │     XConf...  │
        │                           │                          │               │
        │                           │                          │◄── signal ────┤
        │                           │                          │               │
        │                           │              on_check_complete_signal()   │
        │                           │              dispatch_all_pending()       │
        │                           │                    │                      │
        │◄──────────────────────────│────── cb(&fwinfo) ◄┘                     │
        │  [condvar signaled]       │                                          │
        │                           │                                          │
        │  downloadFirmware(h,r,cb) │                                          │
        │──────────────────────────►│                                          │
        │                           │── register dl_cb                         │
        │                           │── D-Bus: DownloadFirmware ───────────────►│
        │◄── SUCCESS ───────────────┤                          │               │
        │                           │                          │◄── 25% ───────┤
        │◄──── dl_cb(25,INPROG) ────│──────────────────────────┤               │
        │                           │                          │◄── 100% ──────┤
        │◄──── dl_cb(100,DONE) ─────│──────────────────────────┤               │
        │                           │                                          │
        │  unregisterProcess(h)     │                                          │
        │──────────────────────────►│                                          │
        │                           │── internal_system_deinit()               │
        │                           │   └─ quit loop ──────────►│ exits        │
        │                           │   └─ join ◄──────────────┘              │
        │                           │── D-Bus: Unregister ─────────────────────►│
        │                           │── free(handle)                           │
        │◄── return ────────────────┤                                          │
        │                           │                                          │
```

---

## Appendix C: Build and Integration

### Library Build (Autotools)

```makefile
# librdkFwupdateMgr/Makefile.am
lib_LTLIBRARIES = librdkFwupdateMgr.la

librdkFwupdateMgr_la_SOURCES = \
    src/rdkFwupdateMgr_process.c \
    src/rdkFwupdateMgr_api.c \
    src/rdkFwupdateMgr_async.c

librdkFwupdateMgr_la_CFLAGS = \
    -I$(top_srcdir)/librdkFwupdateMgr/include \
    -I$(top_srcdir)/librdkFwupdateMgr/src \
    -I$(top_srcdir)/common_utilities/utils \
    $(GIO_CFLAGS) $(GLIB_CFLAGS)

librdkFwupdateMgr_la_LIBADD = \
    $(GIO_LIBS) $(GLIB_LIBS) -lpthread

# Installed public header
librdkFwupdateMgr_includedir = $(includedir)/rdkFwupdateMgr
librdkFwupdateMgr_include_HEADERS = include/rdkFwupdateMgr_client.h
```

### Client Linking

```makefile
my_app_LDADD = -lrdkFwupdateMgr -lfwutils -lrdkloggers $(GIO_LIBS)
my_app_CFLAGS = -I$(includedir)/rdkFwupdateMgr
```

### Minimum Client Code

```c
#include "rdkFwupdateMgr_client.h"
#include "rdkv_cdl_log_wrapper.h"

int main(void) {
    log_init();
    
    FirmwareInterfaceHandle h = registerProcess("MyApp", LIB_VERSION);
    if (!h) return 1;
    
    // ... use APIs ...
    
    unregisterProcess(h);
    log_exit();
    return 0;
}
```

---

*End of Document*
