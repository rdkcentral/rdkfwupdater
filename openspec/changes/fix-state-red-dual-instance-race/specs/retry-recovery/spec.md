## MODIFIED Requirements

### Requirement: Retry logic must be skipped after State Red entry
The `rdkv_upgrade_request()` function SHALL NOT call `retryDownload()` or `fallBack()` when the preceding download attempt resulted in State Red entry. The error code indicating State Red (`RDKV_UPGRADE_ERROR_STATE_RED`) MUST be returned immediately, bypassing all retry and fallback logic.

#### Scenario: downloadFile returns State Red error code
- **WHEN** `downloadFile()` detects that `dwnlError()` triggered State Red entry (by checking presence of `/tmp/stateRedEnabled` flag file after `dwnlError()` returns)
- **THEN** `downloadFile()` SHALL return `RDKV_UPGRADE_ERROR_STATE_RED` instead of the raw curl error code

#### Scenario: rdkv_upgrade_request short-circuits on State Red
- **WHEN** `rdkv_upgrade_request()` receives `RDKV_UPGRADE_ERROR_STATE_RED` from `downloadFile()` or `codebigdownloadFile()`
- **THEN** `rdkv_upgrade_request()` SHALL return `RDKV_UPGRADE_ERROR_STATE_RED` immediately without calling `retryDownload()` or `fallBack()`

#### Scenario: Process exits within seconds of State Red entry
- **WHEN** State Red is entered during any download path (direct, codebig, mTLS cert failure)
- **THEN** the process SHALL unwind through the return chain and reach `main()` exit within seconds, not minutes

### Requirement: State Red error code defined consistently
A new error constant `RDKV_UPGRADE_ERROR_STATE_RED` SHALL be defined to signal that State Red was entered. This constant MUST be handled at every point where `RDKV_UPGRADE_ERROR_FORCE_EXIT` is currently checked.

#### Scenario: Consistent handling with FORCE_EXIT pattern
- **WHEN** code checks for `RDKV_UPGRADE_ERROR_FORCE_EXIT` to short-circuit retry logic
- **THEN** the same check SHALL also handle `RDKV_UPGRADE_ERROR_STATE_RED` with the same short-circuit behavior
