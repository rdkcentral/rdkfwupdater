# Module Spec: json_process (XConf Response Parser)

## Identity

- **Module**: `json_process`
- **Source**: `src/json_process.c`
- **Header**: `src/include/json_process.h`
- **Library**: `librdksw_jsonparse.la`
- **Type**: Shared library

## Purpose

Parses XConf server JSON responses and builds the firmware request JSON payload. Extracts firmware metadata (URL, version, flags) into a structured `XCONFRES` for downstream consumers.

## Responsibilities

1. Create JSON POST body with device parameters for XConf query
2. Parse XConf server JSON response
3. Validate firmware version against current device version
4. Populate `XCONFRES` structure with firmware details
5. Handle peripheral firmware and PDRI version fields

## Key Data Structure

```c
typedef struct xconf_response {
    char cloudFWFile[128];              // Firmware filename
    char cloudFWLocation[512];          // Download URL (IPv4)
    char ipv6cloudFWLocation[512];      // Download URL (IPv6)
    char cloudFWVersion[64];            // Available firmware version
    char cloudDelayDownload[8];         // Delay download flag
    char cloudProto[6];                 // Protocol (http/tftp)
    char cloudImmediateRebootFlag[12];  // Immediate reboot flag
    char peripheralFirmwares[256];      // Peripheral firmware versions
    char dlCertBundle[64];              // Certificate bundle name
    char dlAppBundle[64];               // Application bundle name
    char cloudPDRIVersion[64];          // PDRI version string
    char rdmCatalogueVersion[512];      // RDM catalogue version
} XCONFRES;
```

## Public API

```c
// Parse JSON string into XCONFRES, validate against current firmware
int processJsonResponse(XCONFRES *response, const char *myfwversion, const char *model, const char *maint);

// Extract XConf response data from raw JSON string
int getXconfRespData(XCONFRES *pResponse, char *pJsonStr);

// Build JSON POST body for XConf request
size_t createJsonString(char *pPostFieldOut, size_t szPostFieldOut);
```

## XConf Response Fields Parsed

| JSON Key | Maps To | Description |
|----------|---------|-------------|
| `firmwareDownloadProtocol` | `cloudProto` | Download protocol |
| `firmwareFilename` | `cloudFWFile` | Firmware file name |
| `firmwareLocation` | `cloudFWLocation` | Download URL |
| `firmwareVersion` | `cloudFWVersion` | Version string |
| `ipv6FirmwareLocation` | `ipv6cloudFWLocation` | IPv6 URL |
| `rebootImmediately` | `cloudImmediateRebootFlag` | Reboot flag |
| `delayDownload` | `cloudDelayDownload` | Delay flag |
| `additionalFwVerInfo` | `peripheralFirmwares` | Peripheral versions |

## Dependencies

- `libcjson` — JSON parsing engine
- `libparsejson` — Additional JSON utilities
- Device info structures for building request payload

## Test Coverage

- Tested indirectly via `unittest/basic_rdkv_main_gtest.cpp`
- JSON parsing tested with mock response data
