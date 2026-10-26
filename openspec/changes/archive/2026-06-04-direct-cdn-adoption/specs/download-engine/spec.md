## ADDED Requirements

### Requirement: Direct CDN download mode skips Codebig fallback
When the `direct_cdn` flag is set in `RdkUpgradeContext_t`, the download engine SHALL attempt only the direct URL download without the mTLS → direct → Codebig fallback chain.

#### Scenario: direct_cdn true bypasses Codebig
- **WHEN** `rdkv_upgrade_request()` is called with `context->direct_cdn == true`
- **THEN** the engine SHALL perform a single direct HTTPS download attempt using `context->artifactLocationUrl` without Codebig signing or fallback

#### Scenario: direct_cdn false preserves existing fallback
- **WHEN** `rdkv_upgrade_request()` is called with `context->direct_cdn == false`
- **THEN** the engine SHALL execute the existing fallback chain: mTLS → direct → Codebig (unchanged behavior)

### Requirement: RdkUpgradeContext_t includes direct_cdn field
The `RdkUpgradeContext_t` structure SHALL include a `bool direct_cdn` field. Callers that zero-initialize the struct SHALL get `false` (legacy behavior) by default.

#### Scenario: Zero-initialized context defaults to legacy
- **WHEN** a caller creates `RdkUpgradeContext_t ctx = {0}` without setting `direct_cdn`
- **THEN** `ctx.direct_cdn` SHALL be false, preserving the full fallback chain behavior
