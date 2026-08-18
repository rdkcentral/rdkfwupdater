## ADDED Requirements

### Requirement: Direct CDN per-artifact selective retry
The Direct CDN orchestrator SHALL implement a per-artifact retry mechanism: only artifacts that failed with a transient error are re-attempted on subsequent iterations.

#### Scenario: Successful artifact not re-attempted
- **WHEN** PCI download succeeds on iteration 1 but PDRI fails with a transient error
- **THEN** on iteration 2 the orchestrator SHALL skip PCI and only re-attempt PDRI

#### Scenario: Maximum 3 retry iterations
- **WHEN** an artifact fails with a transient error on all iterations
- **THEN** the orchestrator SHALL attempt at most 3 total iterations before declaring failure

#### Scenario: Both PCI and PDRI succeed stops retry loop
- **WHEN** both PCI and PDRI downloads succeed within a retry iteration
- **THEN** the orchestrator SHALL exit the retry loop immediately regardless of peripheral status

### Requirement: Transient vs permanent failure classification
The per-artifact download mode SHALL classify errors as transient (retryable) or permanent (non-retryable) based on the curl error code.

#### Scenario: Connection timeout is transient
- **WHEN** `rdkv_upgrade_request()` returns `CURLE_OPERATION_TIMEDOUT`
- **THEN** the per-artifact caller SHALL return `DIRECT_CDN_RETRY_ERR` (retryable)

#### Scenario: Connection refused is transient
- **WHEN** `rdkv_upgrade_request()` returns `CURLE_COULDNT_CONNECT`
- **THEN** the per-artifact caller SHALL return `DIRECT_CDN_RETRY_ERR` (retryable)

#### Scenario: Receive error is transient
- **WHEN** `rdkv_upgrade_request()` returns `CURLE_RECV_ERROR`
- **THEN** the per-artifact caller SHALL return `DIRECT_CDN_RETRY_ERR` (retryable)

#### Scenario: HTTP 404 is permanent
- **WHEN** the HTTP response code is 404
- **THEN** the per-artifact caller SHALL return -1 (non-retryable, permanent failure)

### Requirement: XConf re-query per retry iteration
The Direct CDN orchestrator SHALL perform a fresh XConf query on each retry iteration to obtain current per-artifact URLs.

#### Scenario: Fresh XConf data per iteration
- **WHEN** the retry loop begins a new iteration
- **THEN** it SHALL call `rdkv_upgrade_request()` with `XCONF_UPGRADE` type and re-parse the response before attempting per-artifact downloads

### Requirement: Peripheral failure does not block overall success
Peripheral download failure SHALL NOT prevent the overall `DirectCDNDownload()` from reporting success, provided PCI and PDRI both succeeded.

#### Scenario: PCI+PDRI success with peripheral failure
- **WHEN** PCI and PDRI downloads succeed but peripheral download fails
- **THEN** `DirectCDNDownload()` SHALL return 0 (success)

#### Scenario: Peripheral retried within loop but non-blocking
- **WHEN** peripheral download fails on one iteration
- **THEN** it SHALL be re-attempted on subsequent iterations but SHALL NOT contribute to the retry-gate condition
