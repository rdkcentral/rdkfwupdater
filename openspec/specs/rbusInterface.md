# Module Spec: rbusInterface (RDK Bus)

## Identity

- **Module**: `rbusInterface`
- **Source**: `src/rbusInterface/rbusInterface.c`
- **Header**: `src/rbusInterface/rbusInterface.h`
- **Type**: Compiled into main binaries (not a separate library)

## Purpose

Provides RDK Bus (rbus) integration for triggering telemetry (T2) data uploads after firmware update operations.

## Responsibilities

1. Initialize rbus handle for firmware upgrader
2. Invoke T2 DCM report upload via rbus
3. Clean rbus handle on completion

## Public API

```c
// Trigger T2 telemetry upload via rbus
rbusError_t invokeRbusDCMReport(void);
```

## Constants

```c
#define RDKFWUPGRADER_RBUS_HANDLE_NAME "rdkfwRbus"
#define T2_UPLOAD "Device.X_RDKCENTRAL-COM_T2.UploadDCMReport"
```

## Behavior

1. Opens rbus handle with name `rdkfwRbus`
2. Invokes `Device.X_RDKCENTRAL-COM_T2.UploadDCMReport` data element
3. Triggers DCM (Device Configuration Manager) telemetry report upload
4. Closes rbus handle

## Conditional Compilation

- Not compiled when `GTEST_ENABLE` is defined (excluded from test builds)
- Requires `librbus` at link time

## Dependencies

- `librbus` — RDK bus client library
- `rbus/rbus.h` — RDK bus API header

## Usage Context

Called after firmware download completion to report telemetry markers (download success/failure, timing, error codes) to the cloud analytics backend.
