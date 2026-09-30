## ADDED Requirements

### Requirement: RFC Feature Gate
The system SHALL read the RFC parameter `Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.SWDLDirect.Enable` at startup and expose a runtime query function `isDirectCDNEnabled()` that returns true only when the RFC value is `"true"`.

#### Scenario: RFC enabled
- **WHEN** the RFC parameter `SWDLDirect.Enable` is set to `"true"`
- **THEN** `isDirectCDNEnabled()` SHALL return true

#### Scenario: RFC disabled
- **WHEN** the RFC parameter `SWDLDirect.Enable` is set to `"false"` or is absent
- **THEN** `isDirectCDNEnabled()` SHALL return false

#### Scenario: RFC value stored in Rfc_t
- **WHEN** `getRFCSettings()` is called during initialization
- **THEN** the Direct CDN RFC value SHALL be stored in `Rfc_t.rfc_directcdn`

### Requirement: XConf URL Path Branching
The system SHALL use a different XConf query URL path when Direct CDN is enabled. The path `/xconf/firmware/stb/` SHALL be used instead of `/xconf/swu/stb`.

#### Scenario: Direct CDN enabled changes XConf URL
- **WHEN** `GetServURL()` is called and `isDirectCDNEnabled()` returns true
- **THEN** the returned URL SHALL contain the path `/xconf/firmware/stb/`

#### Scenario: Direct CDN disabled uses legacy URL
- **WHEN** `GetServURL()` is called and `isDirectCDNEnabled()` returns false
- **THEN** the returned URL SHALL contain the path `/xconf/swu/stb`

### Requirement: Codebig Bypass
The system SHALL skip Codebig access probing when Direct CDN is enabled.

#### Scenario: Direct CDN bypasses Codebig
- **WHEN** `checkCodebigAccess()` is called and `isDirectCDNEnabled()` returns true
- **THEN** the function SHALL return false without performing any network probe

#### Scenario: Codebig probe unaffected when disabled
- **WHEN** `checkCodebigAccess()` is called and `isDirectCDNEnabled()` returns false
- **THEN** the function SHALL perform the normal Codebig accessibility check

### Requirement: Per-Artifact URL Parsing
The system SHALL parse per-artifact download URLs from the enriched XConf JSON response when Direct CDN is enabled: `firmware_URL` (PCI), `additionalFwVerInfo_URL` (PDRI), and `<peripheralProduct>_URL` (Peripheral).

#### Scenario: PCI firmware URL parsed
- **WHEN** XConf response contains `firmware_URL` field and Direct CDN is enabled
- **THEN** the value SHALL be stored in `XCONFRES.firmwareUrl`

#### Scenario: PDRI URL parsed
- **WHEN** XConf response contains `additionalFwVerInfo_URL` field and Direct CDN is enabled
- **THEN** the value SHALL be stored in `XCONFRES.pdriUrl`

#### Scenario: Peripheral URL parsed with dynamic key
- **WHEN** XConf response contains `<peripheralProduct>_URL` field and Direct CDN is enabled
- **THEN** the value SHALL be stored in `XCONFRES.remCtrlUrl` where `<peripheralProduct>` is resolved by `getPeripheralProduct()`

#### Scenario: Legacy parsing when disabled
- **WHEN** Direct CDN is disabled
- **THEN** `getXconfRespData()` SHALL use the legacy `GetJsonValContaining("remCtrl", ...)` approach for peripherals and SHALL NOT attempt to parse per-artifact URL fields

### Requirement: Peripheral Product Discovery
The system SHALL provide `getPeripheralProduct()` which returns the peripheral product identifier cached during `BuildRemoteInfo()`.

#### Scenario: Product available after initialization
- **WHEN** `BuildRemoteInfo()` has populated the product identifier
- **THEN** `getPeripheralProduct()` SHALL return the cached product identifier (e.g., `"remCtrlXR15"`)

#### Scenario: Product not yet available
- **WHEN** `BuildRemoteInfo()` has not yet populated the product identifier
- **THEN** `getPeripheralProduct()` SHALL return the default `"remCtrl"`

### Requirement: DirectCDNDownload Orchestrator
The system SHALL provide a `DirectCDNDownload()` function that orchestrates per-artifact firmware downloads with selective retry. It SHALL query XConf, parse the response, then attempt PCI, PDRI, and Peripheral downloads independently.

#### Scenario: All artifacts succeed on first attempt
- **WHEN** `DirectCDNDownload()` is called and all per-artifact downloads succeed
- **THEN** the function SHALL return 0 (success) after exactly one XConf query and one download attempt per artifact

#### Scenario: PCI fails then succeeds on retry
- **WHEN** PCI download fails with a transient error on the first attempt
- **THEN** the orchestrator SHALL re-query XConf and re-attempt PCI download (up to 3 total iterations)

#### Scenario: PDRI fails then succeeds on retry
- **WHEN** PDRI download fails with a transient error on the first attempt but PCI already succeeded
- **THEN** the orchestrator SHALL re-query XConf, skip PCI (already succeeded), and re-attempt PDRI

#### Scenario: Peripheral failure does not gate retry
- **WHEN** Peripheral download fails but PCI and PDRI both succeed
- **THEN** the orchestrator SHALL return 0 (overall success) — peripheral failure is non-fatal

#### Scenario: Max retry exceeded
- **WHEN** PCI or PDRI fails on all 3 retry iterations
- **THEN** the orchestrator SHALL return -1 (failure)

#### Scenario: TFTP protocol rejected
- **WHEN** XConf response specifies `cloudProto` as `"tftp"`
- **THEN** `DirectCDNDownload()` SHALL abort and return -1 (TFTP not supported in Direct CDN mode)

### Requirement: Per-Artifact Download Mode
The system SHALL support calling `checkTriggerUpgrade()` with a specific `upgrade_type` to download only a single artifact type (PCI, PDRI, or Peripheral) using the per-artifact URL from `XCONFRES`.

#### Scenario: PCI-only download via per-artifact URL
- **WHEN** `checkTriggerUpgrade(response, model, PCI_UPGRADE)` is called and `response->firmwareUrl` is populated
- **THEN** the download SHALL use `response->firmwareUrl` as the artifact location URL

#### Scenario: PDRI-only download via per-artifact URL
- **WHEN** `checkTriggerUpgrade(response, model, PDRI_UPGRADE)` is called and `response->pdriUrl` is populated
- **THEN** the download SHALL use `response->pdriUrl` as the artifact location URL

#### Scenario: Peripheral download via per-artifact URL
- **WHEN** `checkTriggerUpgrade(response, model, PERIPHERAL_UPGRADE)` is called and `response->remCtrlUrl` is populated
- **THEN** the download SHALL use `response->remCtrlUrl` as the artifact location URL

#### Scenario: Transient failure returns retryable code
- **WHEN** a per-artifact download fails with a network error (connection timeout, receive error)
- **THEN** `checkTriggerUpgrade()` SHALL return `DIRECT_CDN_RETRY_ERR` (-2)

#### Scenario: Permanent failure returns non-retryable code
- **WHEN** a per-artifact download fails with a validation error or HTTP 404
- **THEN** `checkTriggerUpgrade()` SHALL return -1 (non-retryable)

### Requirement: Context Struct Direct CDN Flag
The `RdkUpgradeContext_t` struct SHALL include a `bool direct_cdn` field that, when set to true, causes the download engine to skip the Codebig fallback chain and use the artifact URL directly.

#### Scenario: direct_cdn flag set
- **WHEN** `rdkv_upgrade_request()` is called with `context->direct_cdn == true`
- **THEN** the download SHALL attempt only the direct URL (no mTLS → Codebig fallback chain)

#### Scenario: direct_cdn flag not set (default)
- **WHEN** `rdkv_upgrade_request()` is called with `context->direct_cdn == false`
- **THEN** the download SHALL use the existing fallback chain (mTLS → direct → Codebig)

### Requirement: One-Shot Integration
The one-shot binary SHALL invoke `DirectCDNDownload()` when Direct CDN is enabled, replacing the legacy `MakeXconfComms()` → `checkTriggerUpgrade()` path.

#### Scenario: Direct CDN path in one-shot
- **WHEN** the one-shot binary detects `isDirectCDNEnabled() == true` after initial validation
- **THEN** it SHALL call `DirectCDNDownload()` instead of the legacy XConf→download path

#### Scenario: Legacy path when disabled
- **WHEN** the one-shot binary detects `isDirectCDNEnabled() == false`
- **THEN** it SHALL use the existing `MakeXconfComms()` → `processJsonResponse()` → `checkTriggerUpgrade(response, model, 0)` path unchanged

### Requirement: Daemon Integration
The daemon SHALL set `context.direct_cdn = true` on `RdkUpgradeContext_t` when Direct CDN is enabled, causing firmware downloads to bypass Codebig and use per-artifact URLs.

#### Scenario: Daemon uses Direct CDN flag
- **WHEN** the daemon's `DownloadFirmware` handler constructs an `RdkUpgradeContext_t` and `isDirectCDNEnabled()` returns true
- **THEN** it SHALL set `context.direct_cdn = true` and populate `context.artifactLocationUrl` from the cached per-artifact URL in `XCONFRES`
