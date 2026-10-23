## ADDED Requirements

### Requirement: Token Expiry Inner-Retry Short-Circuit
When operating in Direct CDN mode, the download engine's inner retry loop (`retryDownload()`) SHALL NOT retry a request that received HTTP 403. The 403 response indicates token expiry; retrying with the same stale-token URL is futile. Control SHALL return immediately to the caller so the outer `DirectCDNDownload()` loop can re-query XConf for fresh token-bearing URLs.

#### Scenario: HTTP 403 breaks inner retry in Direct CDN mode
- **WHEN** `retryDownload()` is executing in direct-path mode (HTTP_SSR_DIRECT)
- **AND** `context->direct_cdn == true`
- **AND** `downloadFile()` returns with `*httpCode == 403`
- **THEN** `retryDownload()` SHALL break immediately without further iterations or delay

#### Scenario: HTTP 403 retries normally in legacy mode
- **WHEN** `retryDownload()` is executing in direct-path mode (HTTP_SSR_DIRECT)
- **AND** `context->direct_cdn == false`
- **AND** `downloadFile()` returns with `*httpCode == 403`
- **THEN** `retryDownload()` SHALL continue its normal retry loop (existing behavior unchanged)

#### Scenario: Other break conditions preserved
- **WHEN** `retryDownload()` is executing with `context->direct_cdn == true`
- **AND** `downloadFile()` returns HTTP 200, 206, 404, or DWNL_BLOCK
- **THEN** existing break conditions SHALL remain unchanged

#### Scenario: Backward compatibility for non-Direct-CDN paths
- **WHEN** `context->direct_cdn == false`
- **THEN** all retry behavior in `retryDownload()` SHALL remain identical to pre-change behavior regardless of HTTP response code

---

### Requirement: mTLS Bypass for Direct CDN Downloads
When operating in Direct CDN mode and NOT in state-red recovery, `downloadFile()` SHALL skip mTLS certificate acquisition and pass NULL as the certificate parameter to the HTTP download function. Direct CDN URLs contain embedded authentication tokens; client certificates are neither required nor expected by the CDN.

#### Scenario: Certificate fetch skipped for Direct CDN (normal state)
- **WHEN** `downloadFile()` is called with `context->direct_cdn == true`
- **AND** `isInStateRed()` returns 0 (not in state-red)
- **THEN** `getMtlscert()` SHALL NOT be called
- **AND** `doHttpFileDownload()` / `chunkDownload()` SHALL receive NULL as the certificate parameter

#### Scenario: Recovery certificate used during state-red (even with Direct CDN)
- **WHEN** `downloadFile()` is called with `context->direct_cdn == true`
- **AND** `isInStateRed()` returns 1 (state-red active)
- **THEN** `getMtlscert()` SHALL be called with the recovery cert group
- **AND** the download function SHALL receive the populated certificate structure

#### Scenario: Legacy mTLS path unchanged
- **WHEN** `downloadFile()` is called with `context->direct_cdn == false`
- **THEN** existing mTLS certificate fetch and usage behavior SHALL remain unchanged regardless of state-red status

#### Scenario: Cert fetch failure cannot trigger spurious state-red in Direct CDN mode
- **WHEN** `context->direct_cdn == true`
- **AND** `isInStateRed() != 1`
- **THEN** `MTLS_CERT_FETCH_FAILURE` → `RDKV_UPGRADE_ERROR_STATE_RED` path SHALL NOT be reachable (since `getMtlscert()` is never called)

#### Scenario: Both ifdef/ifndef LIBRDKCERTSELECTOR paths guarded
- **WHEN** `downloadFile()` is compiled with `LIBRDKCERTSELECTOR` defined
- **THEN** the mTLS bypass guard SHALL apply to the `getMtlscert()` call in the `#ifdef` path
- **WHEN** `downloadFile()` is compiled without `LIBRDKCERTSELECTOR` defined
- **THEN** the mTLS bypass guard SHALL apply to the `getMtlscert()` call in the `#ifndef` path
