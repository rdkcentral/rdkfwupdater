# Module Spec: cedmInterface (Certificate & Codebig)

## Identity

- **Module**: `cedmInterface`
- **Source**: `src/cedmInterface/codebigUtils.c`, `src/cedmInterface/mtlsUtils.c`
- **Headers**: `src/cedmInterface/codebigUtils.h`, `src/cedmInterface/mtlsUtils.h`
- **Alternate**: `src/cedmInterface-cpc/` (CPC variant, `--enable-cpc-code`)
- **Type**: Compiled into `librdksw_upgrade.la`

## Purpose

Manages mTLS (mutual TLS) certificate retrieval and Codebig URL signing for authenticated firmware downloads. Handles both standard CEDM and rdkcertselector certificate sources.

## Components

### codebigUtils

Handles Codebig proxy URL signing for firmware downloads.

```c
int doCodeBigSigning(int server_type, const char *SignInput,
                     char *signurl, size_t signurlsize,
                     char *outhheader, size_t outHeaderSize);
```

### mtlsUtils

Retrieves mTLS certificates and keys for secure server communication.

```c
// With rdkcertselector
MtlsAuthStatus getMtlscert(MtlsAuth_t *sec, rdkcertselector_h *pthisCertSel);

// Without rdkcertselector
int getMtlscert(MtlsAuth_t *sec);
```

## Service Types (Codebig)

| Constant | Value | Description |
|----------|-------|-------------|
| `INVALID_SERVICE` | 0 | Invalid/unknown |
| `SSR_SERVICE` | 1 | SSR download server |
| `XCONF_SERVICE` | 2 | XConf configuration server |
| `CIXCONF_SERVICE` | 4 | CI XConf server |
| `DAC15_SERVICE` | 14 | DAC15 service |

## mTLS Certificate Sources

1. **rdkcertselector** (if `--enable-rdkcertselector`) — Dynamic certificate selection
2. **RDK SSA CLI** — `GetKey` command for key retrieval
3. **Device files** — Direct certificate/key file paths

## Error Handling (mTLS)

```c
typedef enum {
    STATE_RED_CERT_FETCH_FAILURE = -2,  // State Red recovery failure
    MTLS_CERT_FETCH_FAILURE = -1,       // General mTLS failure
    MTLS_CERT_FETCH_SUCCESS = 0         // Success
} MtlsAuthStatus;
```

## Build Variants

- **Standard** (`src/cedmInterface/`): Default CEDM implementation
- **CPC** (`src/cedmInterface-cpc/`): Alternative CPC code path, enabled with `--enable-cpc-code`

## Security Considerations

- Certificate/key material handled in memory only
- `libsecure_wrapper` used for secure command execution
- OCSP stapling support (`/tmp/.EnableOCSPStapling`)
- Long-term certificate mode (`long_term_cert` flag)

## Dependencies

- `libsecure_wrapper` — Secure popen for key retrieval
- `librdkcertselector` (optional) — Dynamic cert selection
- `system_utils` — System-level utilities
