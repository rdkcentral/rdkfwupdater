## ADDED Requirements

### Requirement: Inner Retry Loop Short-Circuit for Direct CDN HTTP 403
The inner retry loop (`retryDownload()`) SHALL short-circuit on HTTP 403 when the download context indicates Direct CDN mode. This prevents the retry loop from wasting time re-attempting a download with an expired token. The outer orchestration loop in `DirectCDNDownload()` is responsible for obtaining fresh URLs via XConf re-query.

#### Scenario: Direct CDN 403 causes immediate break
- **WHEN** `retryDownload()` receives control after `downloadFile()` returns HTTP 403
- **AND** `context->direct_cdn == true`
- **THEN** the retry loop SHALL break immediately
- **AND** no `sleep(delay)` SHALL be executed for this iteration
- **AND** the 403 HTTP code SHALL be preserved in `*httpCode` for the caller

#### Scenario: Non-Direct-CDN 403 retries normally
- **WHEN** `retryDownload()` receives control after `downloadFile()` returns HTTP 403
- **AND** `context->direct_cdn == false`
- **THEN** existing retry behavior SHALL apply (retry with delay, up to retry_cnt iterations)

#### Scenario: Short-circuit does not affect connectivity-based fallback
- **WHEN** `retryDownload()` returns after 403 short-circuit
- **AND** the caller (`rdkv_upgrade_request()`) evaluates fallback conditions
- **THEN** the Codebig fallback SHALL NOT trigger (since `*httpCode == 403`, not 0, and `curl_ret_code != CURL_CONNECTIVITY_ISSUE`)
- **AND** the Direct CDN Codebig-skip guard at `rdkv_upgrade_request()` line 525 provides defense-in-depth

#### Scenario: Time savings verification
- **WHEN** Direct CDN download receives HTTP 403 on first attempt
- **THEN** `retryDownload()` SHALL return to caller in less than 1 second (no 60-second sleep delays)
- **AND** the outer `DirectCDNDownload()` loop can re-query XConf immediately

#### Scenario: Backward compatibility for all non-Direct-CDN modes
- **WHEN** `context->direct_cdn == false`
- **THEN** all existing break conditions (HTTP 200, 206, 404, DWNL_BLOCK) and retry timing in `retryDownload()` SHALL remain unchanged
