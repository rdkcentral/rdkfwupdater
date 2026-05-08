# Project Baseline — rdkfwupdater

## Overview

**rdkfwupdater** is an RDK (Reference Design Kit) firmware update system for embedded set-top box and TV devices. It handles the full firmware lifecycle: checking for updates via XConf cloud server, downloading firmware images, flashing to device memory, and coordinating reboots.

- **Repository**: `rdkcentral/rdkfwupdater`
- **License**: Apache-2.0
- **Copyright**: Comcast Cable Communications Management, LLC
- **Current Version**: 1.6.2
- **Language**: C (with C++ unit tests)

---

## Architecture

The system has two primary execution modes:

### 1. Legacy CLI Mode (`rdkvfwupgrader`)

Script-invoked binary called by `swupdate_utility.sh` with command-line parameters. Performs a one-shot firmware check-download-flash cycle and exits.

### 2. Daemon Mode (`rdkFwupdateMgr`)

Long-running systemd service exposing D-Bus APIs for client applications to request firmware operations asynchronously. Runs a GLib main loop and handles requests via D-Bus method calls and signals.

**Systemd Unit**: `rdkFwupdateMgr.service`  
**D-Bus Service**: `org.rdkfwupdater.Service`  
**D-Bus Object Path**: `/org/rdkfwupdater/Service`  
**D-Bus Interface**: `org.rdkfwupdater.Interface`

### Client Library (`librdkFwupdateMgr.so`)

Shared library providing a stable C API for client applications. Abstracts D-Bus IPC details, manages background threads for signal reception, and delivers results via callbacks.

---

## Tech Stack

| Layer | Technology |
|-------|-----------|
| Language | C11, C++11 (tests) |
| Build System | GNU Autotools (autoconf, automake, libtool) |
| IPC | D-Bus (GDBus / GLib GIO) |
| Event Loop | GLib GMainLoop |
| HTTP | libcurl |
| JSON | cJSON |
| Logging | rdkloggers (RDK logging framework) |
| Security | mTLS (mutual TLS), PKCS#11, rdkcertselector |
| Telemetry | T2 (telemetry2) |
| Bus | rbus (RDK bus) |
| Testing | Google Test / Google Mock, Behave (L2 functional) |
| Coverage | gcov |

---

## Directory Structure

```
rdkfwupdater/
├── src/                         # Main source code
│   ├── rdkv_main.c             # Legacy CLI entry point
│   ├── rdkFwupdateMgr.c        # Daemon entry point (state machine)
│   ├── rdkv_upgrade.c          # Download logic (direct/codebig/fallback)
│   ├── chunk.c                 # Chunked download support
│   ├── flash.c                 # Firmware flashing to memory
│   ├── json_process.c          # XConf JSON response parsing
│   ├── device_status_helper.c  # Device state tracking
│   ├── download_status_helper.c# Download progress tracking
│   ├── cedmInterface/          # CEDM certificate/key management
│   ├── deviceutils/            # Device info (model, MAC, partner ID)
│   ├── rfcInterface/           # Remote Feature Control (RFC) access
│   ├── iarmInterface/          # IARM event bus integration
│   ├── rbusInterface/          # RDK Bus (rbus) integration
│   ├── dbus/                   # D-Bus server (daemon mode)
│   │   ├── rdkv_dbus_server.c  # D-Bus name acquisition & method dispatch
│   │   ├── rdkFwupdateMgr_handlers.c # Business logic handlers
│   │   └── xconf_comm_status.c # Thread-safe XConf fetch status
│   └── include/                # Internal headers
├── librdkFwupdateMgr/          # Client library
│   ├── include/                # Public API header
│   │   └── rdkFwupdateMgr_client.h
│   ├── src/
│   │   ├── rdkFwupdateMgr_api.c      # Public API (checkForUpdate, download, update)
│   │   ├── rdkFwupdateMgr_async.c    # Background thread & signal dispatch
│   │   └── rdkFwupdateMgr_process.c  # registerProcess/unregisterProcess
│   ├── docs/                   # Design documentation
│   └── examples/               # Example client application
├── unittest/                   # L1 unit tests (Google Test)
├── test/
│   └── functional-tests/       # L2 functional tests (Behave/Python)
├── openspec/                   # OpenSpec workflow artifacts
├── Makefile.am                 # Top-level Automake file
├── configure.ac                # Autoconf configuration
└── rdkFwupdateMgr.service      # systemd unit file
```

---

## Build Artifacts

| Target | Type | Description |
|--------|------|-------------|
| `rdkvfwupgrader` | Binary | Legacy CLI firmware upgrader |
| `rdkFwupdateMgr` | Binary | D-Bus daemon (systemd service) |
| `testClient` | Binary | Test client for D-Bus API validation |
| `librdksw_upgrade.la` | Shared Lib | Download/upgrade engine |
| `librdksw_rfcIntf.la` | Shared Lib | RFC interface wrapper |
| `librdksw_iarmIntf.la` | Shared Lib | IARM event bus wrapper |
| `librdksw_jsonparse.la` | Shared Lib | XConf JSON parser |
| `librdksw_flash.la` | Shared Lib | Firmware flashing |
| `librdksw_fwutils.la` | Shared Lib | Device utilities |
| `librdkFwupdateMgr.la` | Shared Lib | Client API library (v1.0.0) |

---

## Configure Options

| Flag | Purpose |
|------|---------|
| `--enable-cpc-code` | Enable CPC (alternate CEDM) code path |
| `--enable-rdkcertselector` | Use rdkcertselector for certificate management |
| `--enable-mountutils` | Enable mount utilities / rdkconfig |
| `--enable-rfcapi` | Enable RFC API (`-DRFC_API_ENABLED`) |
| `--enable-t2api` | Enable T2 telemetry (`-DT2_EVENT_ENABLED`) |
| `--enable-iarmevent` | Enable IARM event bus (`-DIARM_ENABLED`) |
| `--enable-dbus-daemon` | Enable D-Bus daemon build (`-DENABLE_DBUS_DAEMON`) |
| `--enable-extended-logger` | Extended RDK logger init |
| `--enable-test-fwupgrader` | Install test firmware upgrader |

---

## Core Workflows

### Firmware Check-Download-Flash Cycle

1. **Device Init** — Read device properties (model, MAC, serial, partner ID, firmware version)
2. **XConf Query** — Build HTTP request with device parameters, send to XConf server
3. **JSON Parse** — Parse XConf response for firmware URL, version, flags
4. **Download** — Download firmware via direct HTTP, Codebig, or SSR with fallback/retry
5. **Flash** — Write downloaded image to flash memory
6. **Reboot** — Optionally reboot immediately or defer

### D-Bus Daemon Lifecycle (State Machine)

```
STATE_INIT → STATE_INIT_VALIDATION → STATE_IDLE (GLib main loop)
                                          ↓
                            D-Bus method calls trigger:
                            - RegisterProcess
                            - CheckForUpdate (async → XConf fetch → signal)
                            - DownloadFirmware (async → progress signals)
                            - UpdateFirmware (async → progress signals)
                            - UnregisterProcess
```

### Client Library API Flow

```c
FirmwareInterfaceHandle handle = registerProcess("MyApp");
checkForUpdate(handle, my_callback);        // Non-blocking, callback fires later
downloadFirmware(handle, &req, dl_cb);      // Non-blocking, progress callbacks
updateFirmware(handle, &req, update_cb);    // Non-blocking, progress callbacks
unregisterProcess(handle);
```

---

## Download Strategy

The system implements a resilient multi-path download strategy:

1. **Direct Download** — Standard HTTPS to XConf/SSR server
2. **Codebig Download** — Via Codebig proxy with signed URLs
3. **Fallback** — Switches between direct↔codebig after 3 consecutive HTTP 0 failures
4. **Retry** — Configurable retry count per download type
5. **State Red Recovery** — Special recovery mode for critical update scenarios
6. **Chunked Transfer** — Supports chunked/resumed downloads

---

## External Dependencies

| Library | Purpose |
|---------|---------|
| `libcurl` | HTTP/HTTPS downloads |
| `libcjson` | JSON parsing |
| `librdkloggers` | RDK centralized logging |
| `libdwnlutil` | Download utility helpers |
| `libfwutils` | Firmware utility functions |
| `libsecure_wrapper` | Secure command execution |
| `libparsejson` | Additional JSON parsing |
| `librbus` | RDK bus communication |
| `libglib-2.0` / `libgio-2.0` | GLib event loop, D-Bus bindings |
| `libIARMBus` | IARM event bus (optional) |
| `librfcapi` | RFC remote feature control (optional) |
| `libpthread` | POSIX threading |

---

## Testing

### L1 — Unit Tests (Google Test)

Located in `unittest/`. Key test suites:
- `basic_rdkv_main_gtest.cpp` — Core firmware upgrade logic
- `fwdl_interface_gtest.cpp` — Download interface
- `device_status_helper_gtest.cpp` — Device status helpers
- `dbus_handlers.cpp` — D-Bus handler unit tests
- `rdkFwupdateMgr_handlers_gtest.cpp` — CheckForUpdate handler tests
- `rdkfwupdatemgr_main_flow_gtest.cpp` — Daemon state machine tests
- `rdkFwupdateMgr_async_*.cpp` — Client library async tests (thread safety, stress, signal, cleanup, refcount)

### L2 — Functional Tests (Behave)

Located in `test/functional-tests/`. BDD-style tests using Python Behave framework.

### Running Tests

```bash
./run_ut.sh    # Unit tests with coverage
./run_l2.sh   # L2 functional tests
```

---

## Key Design Decisions

1. **Dual-mode architecture** — Legacy CLI preserved for backward compatibility while daemon provides modern async API
2. **Fire-and-forget D-Bus calls** — Client library sends method calls without waiting for reply; results arrive via broadcast signals
3. **Ephemeral + persistent connections** — API calls use short-lived D-Bus connections; background thread maintains persistent connection for signals
4. **Cache-first approach** — XConf responses cached to `/tmp/xconf_response_thunder.txt` for fast repeated queries
5. **Process registration** — Clients must register before using APIs (access control and lifecycle tracking)
6. **Thread-safe state management** — Mutex-protected global state for XConf communication status and download state
7. **Modular shared libraries** — Each subsystem (RFC, IARM, JSON, flash, device utils) built as separate `.so` for independent versioning

---

## Conventions

- **License header**: Apache-2.0 in every source file
- **Logging**: `SWLOG_INFO`, `SWLOG_ERROR`, `SWLOG_WARN` macros (rdkloggers backend)
- **Error codes**: Negative values for library errors, positive for CURL errors
- **Naming**: `rdkv_` prefix for legacy code, `rdkFwupdateMgr_` prefix for daemon/library code
- **Guards**: `#ifndef GTEST_ENABLE` to swap real implementations for mocks in tests
- **Telemetry**: T2 markers via `t2CountNotify()` / `t2ValNotify()` wrappers
