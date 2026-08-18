## ADDED Requirements

### Requirement: HTTP 403 classified as retryable for Direct CDN per-artifact downloads
The per-artifact download error classification SHALL treat HTTP 403 as a transient retryable error (`DIRECT_CDN_RETRY_ERR`) when operating in Direct CDN per-artifact mode. This enables the `DirectCDNDownload()` retry loop to re-query XConf for fresh token-bearing URLs.

#### Scenario: HTTP 403 triggers retry with XConf re-query
- **WHEN** a per-artifact download receives HTTP 403 response
- **THEN** `checkTriggerUpgrade()` SHALL return `DIRECT_CDN_RETRY_ERR`
- **AND** the `DirectCDNDownload()` orchestrator SHALL re-query XConf on the next iteration to obtain fresh per-artifact URLs

#### Scenario: HTTP 403 respects maximum retry count
- **WHEN** per-artifact downloads receive HTTP 403 on all retry iterations
- **THEN** the orchestrator SHALL fail permanently after 3 total iterations (existing retry cap)

#### Scenario: HTTP 404 remains permanent failure (unchanged)
- **WHEN** a per-artifact download receives HTTP 404 response
- **THEN** `checkTriggerUpgrade()` SHALL return -1 (permanent failure, non-retryable)
