# Subsystem Inventory — rdkfwupdater

> **Analysis Scope:** Runtime behavioral architecture, not directory structure  
> **Evidence Level:** Verified from `Makefile.am`, source files, and headers  
> **Classification:** Each subsystem tagged by type, ownership scope, and execution-model affinity

---

## Inventory Summary

| # | Subsystem | Type | Scope | Spec Priority |
|---|-----------|------|-------|---------------|
| 1 | Firmware Upgrade Engine | Core Runtime | Shared | **P0 — First** |
| 2 | XConf Communication | Core Runtime | Shared | **P0 — First** |
| 3 | D-Bus Service Runtime | Core Runtime | Daemon-specific | **P0 — First** |
| 4 | Client Library SDK | IPC/Integration | Daemon ecosystem | **P0 — First** |
| 5 | Flash Subsystem | Core Runtime | Shared | **P1 — High** |
| 6 | IARM Event Interface | IPC/Integration | Shared | **P1 — High** |
| 7 | Device & Firmware Identity | Infrastructure | Shared | **P1 — High** |
| 8 | XConf Response Parsing | Infrastructure | Shared | P2 — Medium |
| 9 | RFC Configuration | Infrastructure | Shared | P2 — Medium |
| 10 | One-Shot Orchestrator | Core Runtime | One-shot-specific | **P1 — High** |
| 11 | Daemon Orchestrator | Core Runtime | Daemon-specific | **P1 — High** |
| 12 | Download Status Tracking | Operational/Safety | Shared | P2 — Medium |
| 13 | CEDM / Certificate Auth | Infrastructure | Shared | P2 — Medium |
| 14 | Telemetry Integration | Observability | Shared (cross-cutting) | P3 — Low |
| 15 | rBus Integration | IPC/Integration | Shared (cross-cutting) | P3 — Low |
| 16 | Concurrency Control | Operational/Safety | Daemon-specific (cross-cutting) | **P1 — High** |
| 17 | Process Safety Guards | Operational/Safety | Shared (cross-cutting) | P2 — Medium |

---

## 1. Firmware Upgrade Engine

**Type:** Core Runtime Subsystem  
**Scope:** Shared across both binaries  
**Library:** `librdksw_upgrade.so`

### Purpose

Owns the blocking HTTP download lifecycle for all firmware artifact types (PCI, PDRI, Peripheral). Encapsulates URL construction, curl-based download with throttling/resume, mTLS authentication, Codebig fallback, and progress tracking.

### Primary Responsibilities

- Execute `rdkv_upgrade_request()` — the central download function used by both execution models
- Manage chunked/resumable downloads via `chunkDownload()`
- Handle download throttling (speed limiting, pause/unpause via `doInteruptDwnl()`)
- Support PCI, PDRI, and Peripheral upgrade types
- Enforce `download_only` mode (daemon skips flash; one-shot chains into flash)
- Report download errors with curl error codes and TLS error classification
- Manage PID file tracking during download (`savePID()`, `getPidStore()`)

### Key Source Files

| File | Responsibility |
|------|---------------|
| `src/rdkv_upgrade.c` | Core download orchestration, URL construction, error reporting |
| `src/chunk.c` | Chunked/resumable HTTP download with content-length tracking |
| `src/cedmInterface/codebigUtils.c` | Codebig endpoint signing |
| `src/cedmInterface/mtlsUtils.c` | mTLS certificate management |

### Exposed Interface

- `rdkv_upgrade_request(RdkUpgradeContext_t *ctx, void **curl, int *http_code)` — primary API
- `rdkv_upgrade_strerror(int error)` — error code to string
- `RdkUpgradeContext_t` — input context structure (header: `rdkv_upgrade.h`)

### Runtime Dependencies

- `libcurl` (HTTP/HTTPS), `librdksw_fwutils` (device properties), `librdksw_rfcIntf` (RFC settings)
- External: `libdwnlutil`, `libfwutils`, `libsecure_wrapper`

### Interaction with Other Subsystems

- **Called by:** One-Shot Orchestrator (directly), Daemon D-Bus Service (via GTask worker)
- **Calls into:** IARM Event Interface (state events), Flash Subsystem (when `download_only == 0`), Download Status Tracking, Telemetry
- **Receives from:** RFC Configuration (throttle speed, mTLS settings)

### Ownership Boundaries

- Owns the curl handle lifecycle during download
- Does NOT own the decision of whether to download (that belongs to orchestrators)
- Does NOT own XConf communication (separate subsystem)
- Optionally owns flash (when `download_only == 0`, one-shot mode)

### Operational Constraints

- All I/O is blocking — caller must provide threading context
- Global `curl` and `force_exit` variables create implicit coupling with orchestrators
- Throttle interaction requires IARM callback registration by calling code

### Failure/Recovery Responsibilities

- Returns curl error codes; does NOT retry (caller decides retry policy)
- Supports resumable download via chunk mechanism
- Classifies TLS errors for telemetry reporting

---

## 2. XConf Communication

**Type:** Core Runtime Subsystem  
**Scope:** Shared (different integration points per execution model)

### Purpose

Manages the request/response lifecycle with the XConf cloud server to determine whether firmware updates are available. Builds the device identity payload, executes the HTTP POST, and delivers the raw response for parsing.

### Primary Responsibilities

- Construct XConf request payload (`createJsonString()`) with device identity fields
- Determine XConf server URL (`GetServURL()`, with Codebig fallback)
- Execute blocking HTTP POST to XConf server
- Deliver raw response data for JSON parsing
- In daemon: implement cache-first strategy with file + in-memory cache
- In daemon: provide `rdkFwupdateMgr_checkForUpdate()` wrapper with caching

### Key Source Files

| File | Responsibility | Scope |
|------|---------------|-------|
| `src/json_process.c` → `createJsonString()` | Build XConf request payload | Shared |
| `src/rdkv_upgrade.c` → `MakeXconfComms()` in one-shot `rdkv_main.c` | One-shot XConf query | One-shot |
| `src/dbus/rdkFwupdateMgr_handlers.c` → `rdkFwupdateMgr_checkForUpdate()` | Daemon XConf query with cache | Daemon |
| `src/dbus/rdkFwupdateMgr_handlers.c` → cache functions | In-memory + file XConf response cache | Daemon |

### Exposed Interface

- One-shot: `MakeXconfComms(XCONFRES *response, int server_type, int *http_code)` (in `rdkv_main.c`)
- Daemon: `rdkFwupdateMgr_checkForUpdate(const char *handler_id)` → `CheckUpdateResponse`
- Daemon cache: `save_xconf_to_cache()`, `get_cached_xconf_data()`, `load_xconf_from_cache()`, `xconf_cache_exists()`

### Ownership Boundaries

- Owns XConf HTTP session lifecycle
- Daemon: owns cache validity state (`g_xconf_data_valid`, G_LOCK-protected)
- Does NOT own the decision logic of what to do with the response (parsing subsystem)

---

## 3. D-Bus Service Runtime

**Type:** Core Runtime Subsystem  
**Scope:** Daemon-specific

### Purpose

Owns the D-Bus server lifecycle, method dispatch, async task management, client tracking, signal broadcasting, and concurrency control for the daemon execution model.

### Primary Responsibilities

- D-Bus service setup: introspection XML parsing, bus name acquisition, object registration
- Method dispatch: route incoming D-Bus calls to appropriate handlers (`process_app_request()`)
- Client process tracking: register/unregister clients (`registered_processes` GHashTable)
- Async task management: GTask lifecycle for CheckForUpdate, Download, Flash workers
- Piggyback queues: coalesce concurrent identical requests (waiting lists)
- Signal broadcasting: emit `CheckForUpdateComplete`, `DownloadProgress`, `UpdateProgress`
- Concurrency guards: `IsDownloadInProgress`, `IsFlashInProgress`, `XConfCommStatus`

### Key Source Files

| File | Responsibility |
|------|---------------|
| `src/dbus/rdkv_dbus_server.c` | D-Bus setup, `process_app_request()`, task lifecycle, worker spawn, signal emission |
| `src/dbus/rdkv_dbus_server.h` | Interface constants, struct definitions, extern declarations |
| `src/dbus/rdkFwupdateMgr_handlers.c` | Business logic: XConf query, cache, validation |
| `src/dbus/rdkFwupdateMgr_handlers.h` | Handler function declarations |
| `src/dbus/xconf_comm_status.c` | Thread-safe XConf status flag management |
| `src/dbus/xconf_comm_status.h` | Status API declarations |

### Exposed Interface

- D-Bus methods: `RegisterProcess`, `UnregisterProcess`, `CheckForUpdate`, `DownloadFirmware`, `UpdateFirmware`
- D-Bus signals: `CheckForUpdateComplete`, `DownloadProgress`, `UpdateProgress`
- C API: `setup_dbus_server()`, `cleanup_dbus()`, `init_task_system()`

### Runtime Dependencies

- GLib/GIO (GDBus, GTask, g_idle_add, GMutex)
- `librdksw_upgrade` (download workers), `librdksw_flash` (flash workers)
- `librdksw_jsonparse`, `librdksw_fwutils`, `librdksw_rfcIntf`

### Ownership Boundaries

- Owns all D-Bus connection state, object registration, bus name
- Owns task lifecycle (TaskContext, active_tasks, waiting queues)
- Owns process registry (client registration state)
- Does NOT own business logic (delegates to handlers)
- Does NOT own blocking I/O (delegates to GTask workers)

### Failure/Recovery Responsibilities

- Never calls `exit()` — logs errors, resets state, continues serving
- Cleans up all task contexts on shutdown
- Validates all D-Bus inputs before processing

---

## 4. Client Library SDK

**Type:** IPC/Integration Subsystem  
**Scope:** Daemon ecosystem (external consumer)  
**Library:** `librdkFwupdateMgr.so`

### Purpose

Provides the public C API for third-party applications to interact with the rdkFwupdateMgr daemon. Abstracts all D-Bus communication behind a simple register/check/download/update/unregister lifecycle.

### Primary Responsibilities

- Process registration/unregistration with daemon
- Synchronous D-Bus method invocation (fire-and-forget pattern)
- Asynchronous callback dispatch via background thread
- D-Bus signal subscription for update/download/flash notifications
- Callback registry management (3 independent registries)
- Background GLib main loop thread lifecycle

### Key Source Files

| File | Responsibility |
|------|---------------|
| `librdkFwupdateMgr/src/rdkFwupdateMgr_process.c` | `registerProcess()`, `unregisterProcess()`, D-Bus proxy creation |
| `librdkFwupdateMgr/src/rdkFwupdateMgr_api.c` | `checkForUpdate()`, `downloadFirmware()`, `updateFirmware()` |
| `librdkFwupdateMgr/src/rdkFwupdateMgr_async.c` | Background thread, signal handlers, callback dispatch |
| `librdkFwupdateMgr/include/rdkFwupdateMgr_client.h` | Public API header |

### Exposed Interface

- `rdkFwupdateMgr_init()` / `rdkFwupdateMgr_term()`
- `registerProcess()` / `unregisterProcess()`
- `checkForUpdate()`, `downloadFirmware()`, `updateFirmware()`
- `registerCheckForUpdateCallback()`, `registerDownloadCallback()`, `registerUpdateCallback()`

### Ownership Boundaries

- Owns background thread lifecycle
- Owns callback registries
- Does NOT own D-Bus connection persistence (ephemeral proxies per API call)
- Does NOT own firmware update logic (pure IPC bridge)

---

## 5. Flash Subsystem

**Type:** Core Runtime Subsystem  
**Scope:** Shared across both binaries  
**Library:** `librdksw_flash.so`

### Purpose

Owns the firmware image flashing lifecycle. Validates firmware files, invokes platform-specific flash routines, reports flash status via IARM events, and manages reboot decisions.

### Primary Responsibilities

- Flash firmware image to device storage (`flashImage()`)
- Handle PCI vs PDRI flash paths
- Report flash progress via IARM events
- Manage post-flash reboot decision
- Update firmware download status file
- Handle flash-specific telemetry

### Key Source Files

| File | Responsibility |
|------|---------------|
| `src/flash.c` | `flashImage()` — core flash logic |
| `src/download_status_helper.c` | `updateFWDownloadStatus()` — STATUS_FILE writes |

### Exposed Interface

- `flashImage(server_url, upgrade_file, reboot_flag, proto, upgrade_type, maint, trigger_type)`

### Ownership Boundaries

- Owns flash I/O lifecycle
- Does NOT own download decision or URL resolution
- Does NOT own reboot execution (sets flags, invoked externally)

---

## 6. IARM Event Interface

**Type:** IPC/Integration Subsystem  
**Scope:** Shared across both binaries  
**Library:** `librdksw_iarmIntf.so`

### Purpose

Provides system-wide event broadcasting via IARM Bus. Publishes firmware state transitions, maintenance manager updates, peripheral upgrade notifications, and handles download-throttle callbacks.

### Primary Responsibilities

- `eventManager()` — broadcast firmware state, maintenance status, and peripheral events
- `DwnlStopEventHandler()` — receive download throttle/stop callbacks from Maintenance Manager
- `init_event_handler()` / `term_event_handler()` — IARM Bus connect/disconnect lifecycle
- Peripheral firmware notification via ctrlm (Control Manager) integration

### Key Source Files

| File | Responsibility |
|------|---------------|
| `src/iarmInterface/iarmInterface.c` | All IARM event publishing and callback handling |

### Exposed Interface

- `eventManager(event_name, event_status)`
- `init_event_handler()` / `term_event_handler()`
- `DwnlStopEventHandler()` (registered as IARM callback)
- `interuptDwnl(app_mode)` (called from IARM callback context)

### Ownership Boundaries

- Owns IARM Bus connection lifecycle
- Does NOT own event content/semantics (callers define event names/values)

---

## 7. Device & Firmware Identity

**Type:** Infrastructure/Shared-Library Subsystem  
**Scope:** Shared across both binaries  
**Library:** `librdksw_fwutils.so`

### Purpose

Provides device identity queries (model, serial, MAC, build type, firmware version, CPU arch) from device property files. These are consumed by XConf payload construction, flash validation, and status reporting.

### Primary Responsibilities

- `getDeviceProperties()` — populate `DeviceProperty_t` from `/etc/device.properties`
- `getImageDetails()` — read current firmware version
- `GetBuildType()`, `GetEstbMac()`, `GetModelNum()`, `GetFirmwareVersion()` — individual queries
- `getDevicePropertyData()` — generic key-value reader
- File utilities: `filePresentCheck()`, `getFileSize()`, `createDir()`, `createFile()`
- Bundle/certificate path management

### Key Source Files

| File | Responsibility |
|------|---------------|
| `src/deviceutils/deviceutils.c` | File utilities, bundle management, PDRI version, peripheral queries |
| `src/deviceutils/device_api.c` | Device property reading, build type, URL resolution, debug services |

### Exposed Interface

- `DeviceProperty_t`, `ImageDetails_t` data structures
- `getDeviceProperties()`, `getImageDetails()`, `GetBuildType()`, etc.
- `filePresentCheck()`, `getFileSize()`, `createDir()`, `createFile()`

---

## 8. XConf Response Parsing

**Type:** Infrastructure/Shared-Library Subsystem  
**Scope:** Shared across both binaries  
**Library:** `librdksw_jsonparse.so`

### Purpose

Parses XConf JSON responses into the `XCONFRES` structure. Validates firmware version against device model, extracts download URLs, reboot flags, peripheral firmware lists, and PDRI versions.

### Primary Responsibilities

- `createJsonString()` — build XConf POST payload from device identity
- `processJsonResponse()` — validate and interpret parsed XConf data
- `getXconfRespData()` — parse raw JSON into `XCONFRES` fields
- Version comparison between current and available firmware

### Key Source Files

| File | Responsibility |
|------|---------------|
| `src/json_process.c` | All XConf JSON construction and parsing |

### Exposed Interface

- `createJsonString()`, `processJsonResponse()`, `getXconfRespData()`
- `XCONFRES` data structure (defined in `rdkv_cdl.h`)

---

## 9. RFC Configuration

**Type:** Infrastructure/Shared-Library Subsystem  
**Scope:** Shared across both binaries  
**Library:** `librdksw_rfcIntf.so`

### Purpose

Reads Remote Feature Control (RFC) settings that govern firmware update behavior: throttle policies, mTLS enablement, incremental CDL, auto-exclusion.

### Primary Responsibilities

- `getRFCSettings()` — populate `Rfc_t` with throttle, topspeed, mTLS, incremental CDL flags
- `read_RFCProperty()` / `write_RFCProperty()` — generic RFC key-value access
- Auto-exclusion check for non-production devices

### Key Source Files

| File | Responsibility |
|------|---------------|
| `src/rfcInterface/rfcinterface.c` | RFC read/write operations |

### Exposed Interface

- `getRFCSettings(Rfc_t *rfc_list)`
- `read_RFCProperty()`, `write_RFCProperty()`
- `Rfc_t` data structure

---

## 10. One-Shot Orchestrator

**Type:** Core Runtime Subsystem  
**Scope:** One-shot-specific  
**Binary:** `rdkvfwupgrader`

### Purpose

Owns the linear, start-to-exit firmware update lifecycle for the one-shot execution model. Sequences initialization, validation, XConf query, download, flash, and cleanup as a single synchronous pipeline.

### Primary Responsibilities

- Process lifecycle: `main()` → `initialize()` → `initialValidation()` → XConf → download → flash → `uninitialize()` → `exit()`
- Argument parsing (trigger type, retry count)
- Instance mutex via PID file (`/tmp/DIFD.pid`)
- Signal handling (`SIGUSR1` → abort download)
- Maintenance Manager integration (mode query, status reporting)
- Peripheral firmware multi-download loop
- State Red recovery detection

### Key Source Files

| File | Responsibility |
|------|---------------|
| `src/rdkv_main.c` | Complete one-shot orchestration |

### Exposed Interface

- None (binary entry point)
- Exit code semantics: 0=success, non-zero=curl error code

### Ownership Boundaries

- Owns entire process lifecycle
- Directly invokes all shared libraries synchronously
- Owns global state variables: `DwnlState`, `app_mode`, `force_exit`, `curl`

---

## 11. Daemon Orchestrator

**Type:** Core Runtime Subsystem  
**Scope:** Daemon-specific  
**Binary:** `rdkFwupdateMgr`

### Purpose

Owns the persistent daemon lifecycle: state machine startup, D-Bus service initialization, GLib main loop entry, and orderly shutdown.

### Primary Responsibilities

- State machine: `STATE_INIT` → `STATE_INIT_VALIDATION` → `STATE_IDLE`
- D-Bus server setup before `initialize()` (daemon is reachable during init)
- GLib main loop management (`g_main_loop_run`)
- Shared initialization reuse: `initialize()`, `initialValidation()`, `uninitialize()`
- Orderly shutdown: `cleanup_dbus()` → `uninitialize()` → `exit()`

### Key Source Files

| File | Responsibility |
|------|---------------|
| `src/rdkFwupdateMgr.c` | Daemon `main()`, state machine, shared init reuse |

### Ownership Boundaries

- Owns GLib main loop lifecycle
- Owns state machine transitions
- Delegates all request handling to D-Bus Service Runtime
- Shares `initialize()` / `uninitialize()` behavior with One-Shot Orchestrator

---

## 12. Download Status Tracking

**Type:** Operational/Safety Subsystem  
**Scope:** Shared across both binaries

### Purpose

Persists firmware download status to a file (`/opt/fwdnldstatus.txt`) for crash recovery and external monitoring. Also notifies RFC of download status changes.

### Primary Responsibilities

- `updateFWDownloadStatus()` — write structured status to `STATUS_FILE`
- `notifyDwnlStatus()` — push status to RFC via `write_RFCProperty()`
- Suppressible via `disableStatsUpdate` flag (disabled for PDRI/Peripheral)

### Key Source Files

| File | Responsibility |
|------|---------------|
| `src/download_status_helper.c` | Status file writes, RFC notifications |

---

## 13. CEDM / Certificate Auth

**Type:** Infrastructure Subsystem  
**Scope:** Shared (compiled into `librdksw_upgrade.so`)

### Purpose

Manages Codebig endpoint discovery and mTLS certificate authentication for secure firmware downloads.

### Key Source Files

| File | Responsibility |
|------|---------------|
| `src/cedmInterface/codebigUtils.c` | Codebig URL signing (currently stub) |
| `src/cedmInterface/mtlsUtils.c` | mTLS certificate selection and configuration |

---

## 14. Telemetry Integration

**Type:** Observability/Cross-cutting  
**Scope:** Shared across both binaries

### Purpose

Publishes firmware update metrics to Telemetry 2.0 (T2) framework for operational monitoring.

### Characteristics

- `t2CountNotify()` / `t2ValNotify()` — wrapper functions in each binary
- `flashT2CountNotify()` — rename to avoid symbol collision with flash library
- Conditional compilation via `T2_EVENT_ENABLED`
- Not a subsystem with clear boundaries — pervasive instrumentation

---

## 15. rBus Integration

**Type:** IPC/Integration (Cross-cutting)  
**Scope:** Shared across both binaries

### Purpose

Triggers Telemetry 2.0 report uploads via rBus method invocation. Registers firmware update data model with rBus data broker.

### Key Source Files

| File | Responsibility |
|------|---------------|
| `src/rbusInterface/rbusInterface.c` | T2 upload trigger, rBus handle management |

---

## 16. Concurrency Control

**Type:** Operational/Safety (Cross-cutting)  
**Scope:** Daemon-specific

### Purpose

Prevents unsafe concurrent firmware operations. This is not a single module but a cross-cutting set of guards distributed across D-Bus Service Runtime and worker threads.

### Components

| Guard | Protection | Mechanism |
|-------|-----------|-----------|
| `XConfCommStatus` | At most one XConf fetch | `GMutex` in `xconf_comm_status.c` |
| `IsDownloadInProgress` | At most one download | Main-loop-serialized boolean |
| `IsFlashInProgress` | At most one flash | Main-loop-serialized boolean |
| `g_xconf_data_cache` mutex | Thread-safe cache access | `G_LOCK` in handlers |
| `mutuex_dwnl_state` | Download state across threads | `pthread_mutex_t` |
| `app_mode_status` | App mode across threads | `pthread_mutex_t` |
| Piggyback queues | Coalesce concurrent requests | GSList + main-loop serialization |

---

## 17. Process Safety Guards

**Type:** Operational/Safety (Cross-cutting)  
**Scope:** Shared across both binaries

### Purpose

Prevents unsafe concurrent execution of multiple firmware update processes and manages crash recovery indicators.

### Components

- PID file management (`/tmp/DIFD.pid`) — prevents duplicate instances
- Reboot preparation file (`/tmp/fw_preparing_to_reboot`) — crash recovery
- Upgrade flag file (`/tmp/.imageDnldInProgress` or `HTTP_CDL_FLAG`) — external visibility
- RFC auto-exclusion check — exclude non-production devices
- `CurrentRunningInst()` — cross-binary PID-file validation

### Key Source Files

| File | Responsibility |
|------|---------------|
| `src/device_status_helper.c` | `CurrentRunningInst()`, `waitForNtp()`, `isDnsResolve()`, `CheckIProuteConnectivity()` |
| `src/rdkv_main.c` / `src/rdkFwupdateMgr.c` | `initialValidation()`, `updateUpgradeFlag()` |
