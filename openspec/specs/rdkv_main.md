# Module Spec: rdkv_main

## Identity

- **Module**: `rdkv_main`
- **Source**: `src/rdkv_main.c`
- **Binary**: `rdkvfwupgrader`
- **Type**: CLI executable (legacy mode)

## Purpose

Entry point for the legacy script-invoked firmware upgrader. Called by `swupdate_utility.sh` with command-line parameters to perform a one-shot firmware check-download-flash cycle.

## Responsibilities

1. Initialize logging subsystem (rdkloggers)
2. Parse command-line arguments (trigger type, protocol)
3. Check for duplicate running instances (`/tmp/.fwdnld.pid`)
4. Initialize device info (`DeviceProperty_t`)
5. Initialize IARM event bus (if enabled)
6. Read RFC configuration values
7. Install signal handlers (SIGTERM, SIGINT)
8. Orchestrate the firmware update lifecycle:
   - `MakeXconfComms()` → XConf HTTP request
   - `processJsonResponse()` → Parse XConf response
   - `checkTriggerUpgrade()` → Determine upgrade action
   - `upgradeRequest()` → Execute download with fallback strategy

## Key Data Structures

```c
DeviceProperty_t device_info;   // Device model, MAC, partner ID, etc.
ImageDetails_t cur_img_detail;  // Current firmware version/image name
Rfc_t rfc_list;                 // RFC configuration values
```

## State Management

- `DwnlState` — Download state enum (UNINITIALIZED, IN_PROGRESS, COMPLETE, etc.)
- `app_mode` — Foreground (1) or background (0) execution
- `trigger_type` — Source of update trigger (script, maintenance, etc.)
- `force_exit` — Forced exit flag when throttle speed is zero

## Thread Safety

- `mutuex_dwnl_state` — Protects download state transitions
- `app_mode_status` — Protects app mode read/write

## External Interfaces

| Interface | Direction | Description |
|-----------|-----------|-------------|
| XConf Server | Outbound | HTTP/HTTPS firmware availability query |
| IARM Bus | Outbound | Download status events |
| rbus | Outbound | T2 telemetry upload trigger |
| File System | Read | Device properties, RFC values, PID files |
| File System | Write | Download status, PID file, firmware image |

## Exit Conditions

- Successful firmware flash + optional reboot
- No firmware available (already up to date)
- Duplicate instance detected
- Fatal error (network, auth, flash failure)
- Signal received (SIGTERM/SIGINT)

## Dependencies

- `librdksw_upgrade` — Download engine
- `librdksw_jsonparse` — JSON processing
- `librdksw_rfcIntf` — RFC access
- `librdksw_iarmIntf` — IARM events
- `librdksw_flash` — Flash operations
- `librdksw_fwutils` — Device utilities

## Test Coverage

- `unittest/basic_rdkv_main_gtest.cpp` — Core logic tests with mocked dependencies
