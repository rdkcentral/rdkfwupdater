# Module Spec: librdkFwupdateMgr (Client Library)

## Identity

- **Module**: `librdkFwupdateMgr`
- **Source**: `librdkFwupdateMgr/src/`
- **Public Header**: `librdkFwupdateMgr/include/rdkFwupdateMgr_client.h`
- **Artifact**: `librdkFwupdateMgr.so.1.0.0`
- **Type**: Shared library (C API)

## Purpose

Client-side shared library that provides a stable, callback-based C API for firmware lifecycle management. Abstracts D-Bus IPC, threading, and signal handling from application developers.

## Public API

### Process Lifecycle

```c
FirmwareInterfaceHandle registerProcess(const char *processName);
void unregisterProcess(FirmwareInterfaceHandle handle);
```

### Firmware Operations

```c
CheckForUpdateResult checkForUpdate(FirmwareInterfaceHandle handle, UpdateEventCallback callback);
DownloadResult downloadFirmware(FirmwareInterfaceHandle handle, const FwDwnlReq *request, DownloadCallback callback);
UpdateResult updateFirmware(FirmwareInterfaceHandle handle, const FwUpdateReq *request, UpdateCallback callback);
```

## Internal Architecture

### Source Files

| File | Responsibility |
|------|---------------|
| `rdkFwupdateMgr_process.c` | Register/unregister via synchronous D-Bus calls |
| `rdkFwupdateMgr_api.c` | Public API entry points (fire-and-forget D-Bus) |
| `rdkFwupdateMgr_async.c` | Background thread, signal subscription, callback dispatch |

### Threading Model

- **Caller's thread**: API calls execute and return immediately
- **Background thread**: GLib main loop subscribed to D-Bus signals
- **Dispatch**: Signal arrival → registry lookup → callback invocation on BG thread

### Connection Model

- **Ephemeral connections**: Created per API call for fire-and-forget method invocation
- **Persistent connection**: Background thread maintains long-lived subscription connection

### Callback Registry

- Fixed-size array of `CallbackEntry` slots
- Thread-safe access via mutex
- States: FREE → PENDING → DISPATCHING → FREE
- One slot per outstanding async operation

## Key Data Structures

```c
typedef char* FirmwareInterfaceHandle;  // String handle (e.g., "12345")

typedef struct {
    char CurrFWVersion[64];
    UpdateDetails *UpdateDetails;
    CheckForUpdateStatus status;
} FwInfoData;

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

## D-Bus Wire Protocol

| Operation | Service | Path | Interface |
|-----------|---------|------|-----------|
| All | `org.rdkfwupdater.Service` | `/org/rdkfwupdater/Service` | `org.rdkfwupdater.Interface` |

## Error Handling

- NULL handle → immediate FAIL return
- D-Bus connection failure → FAIL return, callback never fires
- Daemon crash → callback never fires (caller must use timeout)
- Recommended timeout: 120 seconds via condvar wait

## Memory Management

- Handle allocated by `registerProcess()`, freed by `unregisterProcess()`
- Callback data (FwInfoData) is stack-allocated during dispatch — valid only during callback
- Caller must copy any data needed beyond callback scope

## Test Coverage

- `unittest/rdkFwupdateMgr_async_threadsafety_gtest.cpp`
- `unittest/rdkFwupdateMgr_async_stress_gtest.cpp`
- `unittest/rdkFwupdateMgr_async_signal_gtest.cpp`
- `unittest/rdkFwupdateMgr_async_cleanup_gtest.cpp`
- `unittest/rdkFwupdateMgr_async_refcount_gtest.cpp`
