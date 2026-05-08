# Module Spec: rfcInterface (Remote Feature Control)

## Identity

- **Module**: `rfcInterface`
- **Source**: `src/rfcInterface/rfcinterface.c`
- **Header**: `src/include/rfcinterface.h`
- **Library**: `librdksw_rfcIntf.la`
- **Type**: Shared library
- **Conditional**: `--enable-rfcapi` (`-DRFC_API_ENABLED`)

## Purpose

Interface layer for reading and writing Remote Feature Control (RFC) parameters via TR-181 data model. Controls runtime firmware download behavior (throttling, speed limits, incremental CDL).

## Key RFC Parameters

| RFC Path | Field | Purpose |
|----------|-------|---------|
| `Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.SWDLSpLimit.Enable` | `rfc_throttle` | Enable download speed limiting |
| `Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.SWDLSpLimit.TopSpeed` | `rfc_topspeed` | Maximum download speed |
| `Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.IncrementalCDL.Enable` | `rfc_incr_cdl` | Enable incremental CDL |
| mTLS RFC | `rfc_mtls` | mTLS configuration |

## Data Structures

```c
typedef struct rfcdetails {
    char rfc_throttle[512];
    char rfc_topspeed[512];
    char rfc_incr_cdl[512];
    char rfc_mtls[512];
} Rfc_t;

typedef enum {
    RFC_STRING = 1,
    RFC_BOOL,
    RFC_UINT
} RFCVALDATATYPE;
```

## Public API

```c
// Read RFC parameter value
int read_RFCProperty(const char *rfcName, char *rfcValue, size_t rfcValueSize);

// Read all firmware-related RFC settings into Rfc_t
void getRFCSettings(Rfc_t *rfc_list);
```

## Return Codes

| Constant | Value | Meaning |
|----------|-------|---------|
| `READ_RFC_SUCCESS` | 1 | Value read successfully |
| `READ_RFC_FAILURE` | -1 | Read failed |
| `READ_RFC_NOTAPPLICABLE` | 0 | RFC not applicable on this platform |

## Backend

- When `RFC_API_ENABLED`: Uses `librfcapi` (`rfcapi.h`) for TR-181 access
- When disabled: Stubs that return `READ_RFC_NOTAPPLICABLE`
- In GTEST mode: Mock `RFC_ParamData_t` and `WDMP_STATUS` types

## Dependencies

- `librfcapi` (optional) — TR-181 data model access
- Platform-specific RFC backend (ccsp, rbus, etc.)

## Test Coverage

- Tested via `unittest/basic_rdkv_main_gtest.cpp` with mocked RFC responses
