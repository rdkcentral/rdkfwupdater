# Module Spec: rdkFwupdateMgr (Daemon)

## Identity

- **Module**: `rdkFwupdateMgr`
- **Source**: `src/rdkFwupdateMgr.c`
- **Binary**: `rdkFwupdateMgr`
- **Type**: systemd daemon
- **Service File**: `rdkFwupdateMgr.service`

## Purpose

Long-running firmware update management daemon that exposes D-Bus APIs for client applications to query firmware availability, trigger downloads, and initiate flash operations asynchronously.

## Responsibilities

1. Run as a systemd service (`Type=simple`)
2. Initialize device info, logging, IARM, RFC (same as legacy)
3. Acquire D-Bus system bus name `org.rdkfwupdater.Service`
4. Run GLib main loop for event-driven operation
5. Handle D-Bus method calls via registered handlers
6. Emit D-Bus signals for async operation completion/progress
7. Manage XConf communication status (thread-safe)
8. Clean shutdown on SIGTERM

## State Machine

```
STATE_INIT
  → Initialize logging (rdkloggers)
  → Read device properties
  → Initialize IARM (if enabled)
  → Read RFC settings

STATE_INIT_VALIDATION
  → Validate device configuration
  → Check network readiness

STATE_IDLE
  → GLib main loop running
  → D-Bus server active
  → Waiting for client requests

STATE_CHECK_UPDATE (future)
STATE_DOWNLOAD_UPDATE (future)
STATE_UPGRADE (future)
```

## D-Bus Interface

| Method | Input | Output | Behavior |
|--------|-------|--------|----------|
| `RegisterProcess` | `(ss)` name, version | `(t)` handler_id | Sync registration |
| `UnregisterProcess` | `(t)` handler_id | `(b)` success | Sync cleanup |
| `CheckForUpdate` | `(s)` handler_id | `(tiissss)` via signal | Async XConf query |
| `DownloadFirmware` | `(s)` handler_id + params | Progress via signal | Async download |
| `UpdateFirmware` | `(s)` handler_id + params | Progress via signal | Async flash |

## Signals Emitted

| Signal | Type | Description |
|--------|------|-------------|
| `CheckForUpdateComplete` | `(tiissss)` | Update check result |
| `DownloadProgress` | Progress struct | Download percentage + status |
| `UpdateProgress` | Progress struct | Flash percentage + status |

## Key Design Decisions

- Always built (not conditional on `--enable-dbus-daemon` anymore)
- GLib GMainLoop for single-threaded event dispatch
- Async operations use GTask framework
- XConf responses cached to `/tmp/xconf_response_thunder.txt`
- Process registration required before API calls (access control)

## Dependencies

- All `librdksw_*` shared libraries
- GLib 2.0 / GIO 2.0
- D-Bus system bus

## Test Coverage

- `unittest/rdkfwupdatemgr_main_flow_gtest.cpp` — State machine tests
- `unittest/rdkFwupdateMgr_handlers_gtest.cpp` — Handler logic tests
- `unittest/dbus_handlers.cpp` — D-Bus integration tests
