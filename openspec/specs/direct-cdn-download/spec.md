# Subsystem Specification: direct-cdn-download

> **Subsystem:** Direct CDN Download Feature  
> **Type:** Feature Module  
> **Scope:** One-shot and daemon execution models  
> **Evidence Level:** Verified from `src/rfcInterface/rfcinterface.c`, `src/deviceutils/device_api.c`, `src/device_status_helper.c`, `src/json_process.c`, `src/rdkv_main.c`

---

## 1. Purpose

The `direct-cdn-download` subsystem defines the RFC-gated Direct CDN firmware download feature, which bypasses Codebig intermediaries and downloads firmware artifacts directly from per-artifact URLs provided by XConf.

---

## 2. Requirements

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
