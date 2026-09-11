## Why

PR #249 (RDKEMW-9150) delivered the structural foundation for Direct CDN — RFC gate, per-artifact URL parsing, Codebig bypass, and context-struct architecture — but two runtime behaviors present in the RDKV-reference implementation were not carried over into the refactored download engine:

1. **Token expiry short-circuit**: When a per-artifact CDN download returns HTTP 403 (token expired), the RDKV-reference returns immediately to the outer XConf re-query loop. The refactored code enters `retryDownload()` unconditionally, wasting 120 seconds (2 retries × 60s delay) retrying the same expired-token URL before the outer loop can refresh.

2. **mTLS bypass**: The RDKV-reference skips mTLS certificate fetch entirely when downloading from token-authenticated CDN URLs (except during state-red recovery). The refactored `downloadFile()` always fetches mTLS certs regardless of `direct_cdn` flag, causing unnecessary I/O and potential spurious state-red entry if cert fetch fails.

These gaps are confirmed by line-for-line comparison between `RDKV-reference/rdkv_main.c` (lines 702-714, 1114-1119) and `src/rdkv_upgrade.c` (lines 1039-1067, 1244-1260).

## What Changes

- `retryDownload()` in `src/rdkv_upgrade.c`: Add HTTP 403 as a break condition when `context->direct_cdn == true`. No change to behavior when `direct_cdn == false`.
- `downloadFile()` in `src/rdkv_upgrade.c`: Add guard to skip `getMtlscert()` when `context->direct_cdn == true` and `isInStateRed() != 1`. Pass `NULL` cert to `doHttpFileDownload()` / `chunkDownload()`. When state-red IS active, recovery cert path preserved.

## Capabilities

### New Capabilities

(none — no new capabilities introduced)

### Modified Capabilities

- `direct-cdn-download`: Add token expiry handling and mTLS bypass behavioral requirements
- `retry-recovery`: Refine inner retry loop behavior to short-circuit on HTTP 403 in Direct CDN mode

## Impact

- **Files modified**: `src/rdkv_upgrade.c` (two functions: `retryDownload()`, `downloadFile()`)
- **API changes**: None — no signature, struct, or Makefile changes
- **Test impact**: New unit tests for both paths; existing tests unaffected
- **Risk**: Low — both changes are additive guards with RFC kill-switch (`SWDLDirect.Enable=false` disables entire Direct CDN path)
- **Scope boundary**: Does NOT touch `directcdn.c`, `rdkv_main.c`, `json_process.c`, daemon handlers, or any other code paths
