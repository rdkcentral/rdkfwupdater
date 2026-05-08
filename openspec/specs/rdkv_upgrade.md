# Module Spec: rdkv_upgrade (Download Engine)

## Identity

- **Module**: `rdkv_upgrade`
- **Source**: `src/rdkv_upgrade.c`, `src/chunk.c`
- **Library**: `librdksw_upgrade.la`
- **Type**: Shared library

## Purpose

Core download engine implementing multi-path firmware retrieval with retry, fallback, and resilience strategies. Handles HTTP/HTTPS downloads via libcurl with mTLS authentication.

## Responsibilities

1. Build XConf HTTP request with device parameters
2. Execute firmware downloads (direct and Codebig paths)
3. Implement fallback strategy (direct ↔ Codebig switching)
4. Implement retry logic with configurable counts
5. Support chunked/resumed downloads
6. Manage curl lifecycle (init, options, cleanup)
7. Support State Red recovery downloads
8. Throttle download speed based on RFC settings

## Key Functions

| Function | Purpose |
|----------|---------|
| `MakeXconfComms()` | Build and send XConf request |
| `upgradeRequest()` | Orchestrate download with business logic |
| `downloadFile()` | Direct server download (XConf/SSR) |
| `codebigdownloadFile()` | Codebig proxy download |
| `doCurlInit()` | Initialize curl handle |
| `doHttpFileDownload()` | Execute HTTP download with curl options |
| `doStopDownload()` | Release curl resources |
| `retryDownload()` | Retry failed download |
| `fallBack()` | Switch between direct/codebig paths |

## Download Paths

```
                    ┌─────────────┐
                    │upgradeRequest│
                    └──────┬──────┘
              ┌────────────┼────────────┐
              ▼            ▼            ▼
      ┌──────────┐  ┌──────────┐  ┌──────────┐
      │  Direct  │  │ Codebig  │  │State Red │
      │ Download │  │ Download │  │ Recovery │
      └────┬─────┘  └────┬─────┘  └──────────┘
           │              │
           └──── fallBack ┘  (after 3 HTTP 0 errors)
```

## Download Types

| Type | Constant | Description |
|------|----------|-------------|
| PCI | `PCI_UPGRADE (0)` | Primary code image |
| PDRI | `PDRI_UPGRADE (1)` | PDRI firmware |
| XCONF | `XCONF_UPGRADE (2)` | XConf-directed update |
| Peripheral | `PERIPHERAL_UPGRADE (3)` | Peripheral device firmware |

## Server Types

| Constant | Value | Description |
|----------|-------|-------------|
| `HTTP_SSR_DIRECT` | 0 | SSR direct |
| `HTTP_SSR_CODEBIG` | 1 | SSR via Codebig |
| `HTTP_XCONF_DIRECT` | 2 | XConf direct |
| `HTTP_XCONF_CODEBIG` | 3 | XConf via Codebig |

## Error Handling

- `RDKV_UPGRADE_SUCCESS (0)` — Success
- `RDKV_UPGRADE_ERROR_THROTTLE_ZERO (-100)` — Throttle speed = 0
- `RDKV_UPGRADE_ERROR_FORCE_EXIT (-101)` — Force exit (curl error 23)
- Positive values map to CURL error codes

## Dependencies

- `libcurl` — HTTP/HTTPS transport
- `cedmInterface` — mTLS certificates and Codebig signing
- `libdwnlutil` — Download utility helpers
- `libsecure_wrapper` — Secure command execution

## Test Coverage

- `unittest/basic_rdkv_main_gtest.cpp` (download path tests)
- `unittest/fwdl_interface_gtest.cpp`
