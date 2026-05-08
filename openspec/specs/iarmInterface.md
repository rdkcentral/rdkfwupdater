# Module Spec: iarmInterface (IARM Event Bus)

## Identity

- **Module**: `iarmInterface`
- **Source**: `src/iarmInterface/iarmInterface.c`
- **Header**: `src/include/iarmInterface.h`
- **Library**: `librdksw_iarmIntf.la`
- **Type**: Shared library
- **Conditional**: `--enable-iarmevent` (`-DIARM_ENABLED`)

## Purpose

Provides integration with the IARM (Inter-Application Resource Manager) bus for publishing firmware download status events to other RDK components (UI, system manager, etc.).

## Responsibilities

1. Initialize IARM bus connection
2. Register as IARM bus member
3. Publish firmware download status events
4. Broadcast image download progress/completion
5. Handle maintenance manager events (if enabled)

## IARM Events Published

| Event | Bus | Description |
|-------|-----|-------------|
| `IARM_BUS_SYSMGR_EVENT_IMAGE_DNLD` | `SYSMGR` | Firmware download status change |
| `IARM_BUS_SYSMGR_EVENT_SYSTEMSTATE` | `SYSMGR` | System state transitions |

## Event Data Structure

```c
typedef struct _IARM_BUS_SYSMgr_EventData_t {
    union {
        struct _CARD_FWDNLD_DATA {
            char eventType;
            char status;
        } cardFWDNLD;
        struct _IMAGE_FWDNLD_DATA {
            char status;
        } imageFWDNLD;
    };
} IARM_BUS_SYSMgr_EventData_t;
```

## Key Functions

```c
// Initialize IARM bus and register member
void initIarmBus(void);

// Send download status event
void eventManager(int status);

// Cleanup IARM bus connection
void cleanupIarmBus(void);
```

## Conditional Compilation

- **IARM_ENABLED**: Full implementation with `libIARMBus`
- **Not enabled**: Stub functions (no-ops)
- **GTEST_ENABLE**: Mock types for unit testing

## Dependencies

- `libIARMBus` — IARM bus client library
- `mfrMgr.h` — MFR manager types
- `sysMgr.h` — System manager event types
- `libIBus.h`, `libIBusDaemon.h` — IARM bus core

## Test Coverage

- `test/testiarmInterface.c` — IARM integration tests
- Mocked in unit tests via `GTEST_ENABLE` stubs
