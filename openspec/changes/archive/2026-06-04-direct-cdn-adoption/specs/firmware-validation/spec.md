## ADDED Requirements

### Requirement: XCONFRES struct extended with per-artifact URL fields
The `XCONFRES` structure SHALL include `firmwareUrl`, `pdriUrl`, and `remCtrlUrl` fields for storing per-artifact download URLs received from the enriched XConf response.

#### Scenario: Per-artifact URL fields present in struct
- **WHEN** the `XCONFRES` struct is allocated
- **THEN** it SHALL contain `char firmwareUrl[CLD_URL_MAX_LEN]`, `char pdriUrl[CLD_URL_MAX_LEN]`, and `char remCtrlUrl[CLD_URL_MAX_LEN]` fields

### Requirement: Conditional XConf response parsing based on Direct CDN mode
The `getXconfRespData()` function SHALL parse per-artifact URL fields from XConf JSON when Direct CDN is enabled, and use legacy parsing when disabled.

#### Scenario: Direct CDN enabled parses per-artifact URLs
- **WHEN** `getXconfRespData()` is called and `rfc_list.rfc_directcdn` is `"true"`
- **THEN** it SHALL parse `firmware_URL`, `additionalFwVerInfo_URL`, and `<peripheralProduct>_URL` from the JSON response

#### Scenario: Direct CDN disabled uses legacy peripheral parsing
- **WHEN** `getXconfRespData()` is called and `rfc_list.rfc_directcdn` is not `"true"`
- **THEN** it SHALL use `GetJsonValContaining("remCtrl", ...)` for peripheral firmware discovery (existing behavior)

### Requirement: PDRI image filename validation
The `processJsonResponse()` function SHALL validate that a PDRI image filename contains the `_PDRI_` substring. If the substring is absent, the PDRI image SHALL be considered invalid.

#### Scenario: Valid PDRI filename accepted
- **WHEN** `processJsonResponse()` processes a response where `cloudPDRIVersion` contains `_PDRI_`
- **THEN** the PDRI image SHALL be considered valid for download

#### Scenario: Invalid PDRI filename rejected
- **WHEN** `processJsonResponse()` processes a response where `cloudPDRIVersion` does NOT contain `_PDRI_`
- **THEN** the PDRI image SHALL be marked invalid and SHALL NOT be downloaded

#### Scenario: Empty PDRI version skipped
- **WHEN** `processJsonResponse()` processes a response where `cloudPDRIVersion` is empty
- **THEN** PDRI SHALL be skipped entirely (no validation needed)
