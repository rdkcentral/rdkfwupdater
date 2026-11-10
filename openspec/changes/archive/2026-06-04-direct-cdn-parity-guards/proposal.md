## Why

PR #249 refactored Direct CDN support into the context-struct architecture (`rdkv_upgrade_request()` / `RdkUpgradeContext_t`) but lost three defense-in-depth behaviors that PR #120 (the behavioral reference for RDKE-874) explicitly provided. These gaps create reviewer-visible behavioral regressions that prevent PR #249 from reaching review-ready status. This is a minimal parity-restoration patch — not a refactor or cleanup.

## What Changes

- Add `context->direct_cdn` early-return guard at the top of `codebigdownloadFile()` in `src/rdkv_upgrade.c` to prevent Direct CDN requests from ever entering the Codebig signing/download path (parity with PR #120's `codebigdownloadFile()` early-return guard for `rfc_directcdn`)
- Add `.bin` filename normalization for PDRI in the per-artifact branch of `checkTriggerUpgrade()` in `src/rdkv_main.c` so that `cloudPDRIVersion` is always normalized before use as a local save filename (parity with PR #120's pre-branch normalization)
- Add HTTP 403 to the retryable-error classification in the per-artifact download result handling within `checkTriggerUpgrade()` in `src/rdkv_main.c` (parity with PR #120's `DIRECT_CDN_RETRY_ERR` classification for token-expiry 403s)

## Capabilities

### New Capabilities

(none — no new capabilities introduced)

### Modified Capabilities

- `download-engine`: Add Codebig entry-point guard that short-circuits when `direct_cdn=true`; this is a defense-in-depth requirement for the Direct CDN path
- `retry-recovery`: Add HTTP 403 as a retryable transient error for Direct CDN per-artifact downloads (token-expiry retry)
- `firmware-validation`: Ensure PDRI filename normalization (`.bin` suffix) applies in per-artifact mode, same as legacy mode

## Impact

- **Files modified**: `src/rdkv_upgrade.c` (codebig guard), `src/rdkv_main.c` (PDRI normalization + 403 retry)
- **API changes**: None — no signature or struct changes
- **Test impact**: Existing L1 tests for `codebigdownloadFile()` may need `direct_cdn=true` path assertion; per-artifact retry gtest needs 403 scenario
- **Risk**: Low — all three changes are additive guards/classifications, not control-flow restructuring
- **Scope boundary**: Does NOT touch `directcdn.c`, `json_process.c`, daemon handlers, RFC gate, XConf parsing, or any other existing working code paths
