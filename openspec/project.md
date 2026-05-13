# rdkfwupdater — Baseline Architecture Overview

> **Document Type:** Canonical repository overview and architectural baseline  
> **Status:** Baseline analysis (code-verified)  
> **Repository:** `rdkcentral/rdkfwupdater`  
> **License:** Apache-2.0 (Comcast Cable Communications Management, LLC)

---

## 1. System Purpose

`rdkfwupdater` is production-grade firmware update infrastructure for RDK-V (Reference Design Kit for Video) embedded Linux devices. It manages the complete firmware lifecycle: checking for updates via XConf cloud server, downloading firmware images over HTTP/HTTPS, flashing them to the device, and coordinating reboot.

The system supports three categories of firmware artifacts:
- **PCI** — Primary Customer Image (main device firmware)
- **PDRI** — Primary Disaster Recovery Image (Backup working image)
- **Peripheral** — Peripheral device firmware (e.g., remote control firmware)

---

## 2. Architectural Overview

The repository produces **two distinct binaries** and **one client shared library**:

| Artifact | Entry Point | Runtime Model | Purpose |
|----------|------------|---------------|---------|
| `rdkvfwupgrader` | `src/rdkv_main.c` | One-shot CLI process | Legacy/existing firmware updater, invoked by Maintenance Manager |
| `rdkFwupdateMgr` | `src/rdkFwupdateMgr.c` | Persistent systemd daemon | D-Bus service exposing firmware update APIs to client applications |
| `librdkFwupdateMgr.so` | `librdkFwupdateMgr/` | Shared library | Client-side SDK that abstracts D-Bus communication with the daemon |

Additionally, `example_plugin` (`librdkFwupdateMgr/examples/example_app.c`) is a reference consumer that demonstrates the full update workflow through the client library.

### 2.1 High-Level System Architecture

```
┌──────────────────────────────────────────────────────────────────────┐
│                        RDK Device (Embedded Linux)                   │
│                                                                      │
│  ┌─────────────┐     ┌──────────────────────────────────────────┐   │
│  │ example_app  │     │            rdkFwupdateMgr               │   │
│  │ (or any app) │     │           (systemd daemon)               │   │
│  │              │     │                                          │   │
│  │  links to:   │     │  ┌──────────┐  ┌──────────────────┐    │   │
│  │ librdkFwup-  │◄───►│  │ D-Bus    │  │ Firmware Logic   │    │   │
│  │ dateMgr.so   │ IPC │  │ Server   │  │ (XConf, Download,│    │   │
│  │              │     │  │ (GDBus)  │  │  Flash, Events)  │    │   │
│  └─────────────┘     │  └──────────┘  └──────────────────┘    │   │
│                       └──────────────────────────────────────────┘   │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │                    rdkvfwupgrader (one-shot)                  │    │
│  │     Direct XConf query → Download → Flash → Exit             │    │
│  └─────────────────────────────────────────────────────────────┘    │
│                                                                      │
│  ┌──────────────────── Shared Libraries ────────────────────────┐   │
│  │ librdksw_upgrade  librdksw_jsonparse  librdksw_flash         │   │
│  │ librdksw_rfcIntf  librdksw_iarmIntf   librdksw_fwutils       │   │
│  └──────────────────────────────────────────────────────────────┘   │
│                                                                      │
│  ┌──── External Dependencies ────────────────────────────────────┐  │
│  │ libcurl  libcjson  GLib/GIO  IARMBus  rbus  libdwnlutil      │  │
│  │ libfwutils  librdkloggers  libsecure_wrapper  libparsejson    │  │
│  └───────────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────────┘
          │                         │
          ▼                         ▼
   ┌─────────────┐          ┌──────────────┐
   │ XConf Cloud  │          │ CDN/Firmware  │
   │ Server       │          │ Server (HTTP) │
   └─────────────┘          └──────────────┘
```

---

## 3. Subsystem Responsibilities

### 3.1 Internal Shared Libraries (Build Artifacts)

| Library | Sources | Responsibility |
|---------|---------|---------------|
| `librdksw_upgrade` | `rdkv_upgrade.c`, `chunk.c`, `cedmInterface/*` | Core HTTP upgrade request engine; Codebig/mTLS certificate management |
| `librdksw_jsonparse` | `json_process.c` | XConf JSON response parsing into `XCONFRES` structure |
| `librdksw_flash` | `flash.c`, `download_status_helper.c` | Firmware image flashing and download status tracking |
| `librdksw_rfcIntf` | `rfcInterface/rfcinterface.c` | Remote Feature Control (RFC) settings reader |
| `librdksw_iarmIntf` | `iarmInterface/iarmInterface.c` | IARM Bus event manager for system-wide event dispatch |
| `librdksw_fwutils` | `deviceutils/deviceutils.c`, `device_api.c` | Device property queries, file utilities, system information |
| `librdkFwupdateMgr` | `librdkFwupdateMgr/src/*` | Client-side SDK library (see §4) |

### 3.2 Source Subsystem Map

| Subsystem | Location | Description |
|-----------|----------|-------------|
| Upgrade Engine | `src/rdkv_upgrade.c`, `src/chunk.c` | HTTP/HTTPS firmware download with chunked transfer, throttling, retry logic |
| CEDM Interface | `src/cedmInterface/` | Codebig endpoint discovery and mTLS authentication |
| Device Utilities | `src/deviceutils/` | Device property file parsing, firmware version queries |
| RFC Interface | `src/rfcInterface/` | RFC (Remote Feature Control) parameter access |
| IARM Interface | `src/iarmInterface/` | IARMBus event publishing (MaintenanceMGR, firmware state) |
| rBus Interface | `src/rbusInterface/` | rBus data model provider registration |
| D-Bus Server | `src/dbus/` | GDBus service for daemon mode (see [D-Bus documentation](dbus/dbus-architecture.md)) |
| JSON Processing | `src/json_process.c` | XConf response parsing |
| Flash | `src/flash.c` | Firmware flashing to persistent storage |
| Status Helpers | `src/device_status_helper.c`, `src/download_status_helper.c` | Device and download status tracking |

---

## 4. Runtime Models

### 4.1 One-Shot Model (`rdkvfwupgrader`)

**Entry point:** `src/rdkv_main.c` → `main()`  
**Invocation:** Typically launched by Maintenance Manager via IARM or cron  
**Arguments:** `rdkvfwupgrader <retry_count> <trigger_type>`

Trigger types: `1`=Bootup, `2`=Scheduled, `3`=TR-69/SNMP, `4`=App, `5`=Delayed, `6`=State Red Recovery

**Execution flow:** Initialize → Validate → XConf query → Download → Flash → Exit

The one-shot binary performs the entire firmware update lifecycle in a single process invocation. It communicates directly with the XConf server, downloads firmware via HTTP/HTTPS, flashes the image, and exits. On fatal errors, it calls `exit()`.

> **Detailed flow:** [runtime/rdkvfwupgrader-lifecycle.md](runtime/rdkvfwupgrader-lifecycle.md)  
> **Deep sequence analysis:** [runtime/rdkvfwupgrader-sequence.md](runtime/rdkvfwupgrader-sequence.md)

### 4.2 Daemon Model (`rdkFwupdateMgr`)

**Entry point:** `src/rdkFwupdateMgr.c` → `main()`  
**Startup:** `systemd` via `rdkFwupdateMgr.service` (`After=tr69hostif.service dbus.service`)  
**Invocation:** `/usr/bin/rdkFwupdateMgr 0 1`

**State machine:** `STATE_INIT` → `STATE_INIT_VALIDATION` → `STATE_IDLE` (GLib main loop)

The daemon initializes D-Bus service, performs device validation, then enters the GLib main event loop indefinitely. All firmware operations (check, download, flash) are triggered by D-Bus method calls from client applications and executed asynchronously using GTask worker threads.

> **Detailed flow:** [runtime/rdkFwupdateMgr-lifecycle.md](runtime/rdkFwupdateMgr-lifecycle.md)  
> **Deep sequence analysis:** [runtime/rdkFwupdateMgr-sequence.md](runtime/rdkFwupdateMgr-sequence.md)  
> **Threading model:** [runtime/daemon-threading-model.md](runtime/daemon-threading-model.md)

### 4.3 Client Library (`librdkFwupdateMgr.so`)

The shared library provides a C API for applications to interact with the daemon. It abstracts all D-Bus communication:

| API | Behavior | Callback |
|-----|----------|----------|
| `registerProcess()` | Synchronous D-Bus call; returns handle | None |
| `checkForUpdate()` | Fire-and-forget D-Bus call; returns immediately | `UpdateEventCallback` (once) |
| `downloadFirmware()` | Fire-and-forget D-Bus call; returns immediately | `DownloadCallback` (repeated) |
| `updateFirmware()` | Fire-and-forget D-Bus call; returns immediately | `UpdateCallback` (repeated) |
| `unregisterProcess()` | Synchronous D-Bus call; frees handle | None |

**Threading model:** The library spawns a background GLib event loop thread at first `registerProcess()` call. This thread subscribes to D-Bus signals and dispatches callbacks. Callers typically use mutex+condvar to synchronize with callbacks.

> **Detailed architecture:** [subsystems/client-library.md](subsystems/client-library.md)  
> **End-to-end interaction flow:** [runtime/client-daemon-interaction.md](runtime/client-daemon-interaction.md)

---

## 5. Process Relationships

```
  Maintenance Manager          Client Applications
        │                      (example_plugin, etc.)
        │ (triggers via IARM      │
        │  or systemd)            │ links librdkFwupdateMgr.so
        ▼                        │
  ┌──────────────┐               │ D-Bus IPC
  │rdkvfwupgrader│               │ (system bus)
  │ (one-shot)   │               ▼
  └──────┬───────┘         ┌──────────────────┐
         │                 │ rdkFwupdateMgr   │
         │                 │ (daemon)          │
         │                 └────────┬─────────┘
         │                          │
         ▼                          ▼
   ┌─────────────────────────────────────┐
   │  Shared firmware update libraries   │
   │  (librdksw_upgrade, librdksw_flash, │
   │   etc.)                             │
   └──────────────┬──────────────────────┘
                  │
    ┌─────────────┼────────────┐
    ▼             ▼            ▼
 XConf        CDN/HTTP      Device
 Server       Servers       Flash Storage
```

Both binaries link the same set of shared upgrade libraries but differ in lifecycle:
- `rdkvfwupgrader`: Exits after single upgrade cycle; can call `exit()` on fatal errors
- `rdkFwupdateMgr`: Never exits on upgrade errors; logs and continues serving

---

## 6. IPC Architecture Summary

### 6.1 D-Bus (Daemon ↔ Client Library)

| Property | Value |
|----------|-------|
| Bus | System bus (`G_BUS_TYPE_SYSTEM`) |
| Service Name | `org.rdkfwupdater.Service` |
| Object Path | `/org/rdkfwupdater/Service` |
| Interface | `org.rdkfwupdater.Interface` |

**Methods:** `RegisterProcess`, `UnregisterProcess`, `CheckForUpdate`, `DownloadFirmware`, `UpdateFirmware`  
**Signals:** `CheckForUpdateComplete`, `DownloadProgress`, `DownloadError`, `UpdateProgress`

> **Full D-Bus documentation:** [dbus/dbus-architecture.md](dbus/dbus-architecture.md)

### 6.2 IARM Bus (System-Wide Events)

Both binaries publish firmware state events via IARM Bus:
- `FW_STATE_EVENT` — firmware update state transitions
- `MaintenanceMGR` — maintenance window status reporting
- `PeripheralUpgradeEvent` — peripheral firmware results
- `RED_STATE_EVENT` — State Red recovery notifications

### 6.3 rBus

Used for data model provider registration (rBus integration via `rbusInterface/rbusInterface.c`). Both binaries compile this source.

### 6.4 JSON-RPC (via Thunder)

Used to query MaintenanceManager mode via JSON-RPC over HTTP to WPEFramework's local endpoint (`http://127.0.0.1:9998`).

---

## 7. Build Configuration

**Build system:** GNU Autotools (`configure.ac` / `Makefile.am`)

### Conditional Compilation Flags

| Flag | Purpose |
|------|---------|
| `--enable-dbus-daemon` | Enables D-Bus daemon compilation (defines `ENABLE_DBUS_DAEMON`) |
| `--enable-iarmevent` | Enables IARM Bus event support |
| `--enable-rfcapi` | Enables RFC API for remote feature control |
| `--enable-t2api` | Enables Telemetry 2.0 event support |
| `--enable-cpc-code` | Alternate CEDM/mTLS implementation |
| `--enable-extended-logger` | Extended RDK logger initialization |
| `--enable-rdkcertselector` | Alternate certificate selection library |
| `--enable-mountutils` | Mount utilities integration |

---

## 8. Supporting Documentation

| Document | Path | Content |
|----------|------|---------|
| D-Bus Architecture | [dbus/dbus-architecture.md](dbus/dbus-architecture.md) | D-Bus server setup, interface introspection, method handlers, signal model, concurrency control |
| rdkvfwupgrader Lifecycle | [runtime/rdkvfwupgrader-lifecycle.md](runtime/rdkvfwupgrader-lifecycle.md) | One-shot binary execution flow, state transitions, error handling |
| rdkFwupdateMgr Lifecycle | [runtime/rdkFwupdateMgr-lifecycle.md](runtime/rdkFwupdateMgr-lifecycle.md) | Daemon lifecycle, GLib main loop, state machine, shutdown |
| Client Library Architecture | [subsystems/client-library.md](subsystems/client-library.md) | Library internals, callback registry, background thread, signal dispatch |
| Firmware Update Flows | [runtime/firmware-update-flows.md](runtime/firmware-update-flows.md) | End-to-end sequence diagrams for check/download/flash operations |
| **rdkvfwupgrader Sequence** | [runtime/rdkvfwupgrader-sequence.md](runtime/rdkvfwupgrader-sequence.md) | **Deep execution-flow analysis: blocking operations, SIGUSR1 handling, throttle interaction, Gantt timeline** |
| **rdkFwupdateMgr Sequence** | [runtime/rdkFwupdateMgr-sequence.md](runtime/rdkFwupdateMgr-sequence.md) | **Deep daemon lifecycle: state machine, CheckForUpdate/Download/Flash GTask flows, concurrency controls, shutdown** |
| **Client-Daemon Interaction** | [runtime/client-daemon-interaction.md](runtime/client-daemon-interaction.md) | **End-to-end flow from app→library→D-Bus→daemon→worker→signal→callback, D-Bus message formats, timing profile** |
| **Daemon Threading Model** | [runtime/daemon-threading-model.md](runtime/daemon-threading-model.md) | **Thread inventory, synchronization primitives, cross-thread data flow, GLib async patterns (GTask, g_idle_add, GThread)** |
| Architecture Diagrams | [diagrams/](diagrams/) | Mermaid-format sequence and interaction diagrams |
| Gaps & Unknowns | [gaps-and-unknowns.md](gaps-and-unknowns.md) | Areas requiring manual validation, unverified assumptions |

---

## 9. Evidence Classification

Throughout supporting documents, findings are classified as:

- **[FACT]** — Directly verified from source code, build files, or configuration
- **[INFERENCE]** — Strongly implied by code structure, naming, or call patterns
- **[ASSUMPTION]** — Reasonable but not verified from code alone
- **[UNKNOWN]** — Requires manual validation on target hardware or platform documentation
