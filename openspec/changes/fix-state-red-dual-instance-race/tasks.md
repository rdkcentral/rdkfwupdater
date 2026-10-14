## 1. Define Error Constant

- [x] 1.1 Add `RDKV_UPGRADE_ERROR_STATE_RED` constant to `src/include/rdkv_upgrade.h` (or wherever `RDKV_UPGRADE_ERROR_FORCE_EXIT` is defined)

## 2. Propagate State Red from downloadFile (Sites 1 & 2)

- [x] 2.1 In `downloadFile()` (`src/rdkv_upgrade.c`), after each `dwnlError()` call in the download loop, check `filePresentCheck(STATEREDFLAG)` — if state red flag exists, return `RDKV_UPGRADE_ERROR_STATE_RED`
- [x] 2.2 Apply the same pattern in `codebigdownloadFile()` if it has an equivalent `dwnlError()` call path — N/A: codebigdownloadFile does not call dwnlError or checkAndEnterStateRed

## 3. Fix Site 3 Return Value (MTLS_CERT_FETCH_FAILURE)

- [x] 3.1 In `downloadFile()` (~line 1052), change `return curl_ret_code` to `return RDKV_UPGRADE_ERROR_STATE_RED` in the `MTLS_CERT_FETCH_FAILURE` block (after `checkAndEnterStateRed()` call)

## 4. Short-Circuit Retry in rdkv_upgrade_request

- [x] 4.1 In `rdkv_upgrade_request()` for the direct download path, add `RDKV_UPGRADE_ERROR_STATE_RED` check alongside existing `RDKV_UPGRADE_ERROR_FORCE_EXIT` and `RDKV_UPGRADE_ERROR_THROTTLE_ZERO` checks before `retryDownload()` call
- [x] 4.2 In `rdkv_upgrade_request()` for the codebig download path, add the same `RDKV_UPGRADE_ERROR_STATE_RED` check before `retryDownload()` call

## 5. Guard main() Against Double-Uninitialize

- [x] 5.1 In `main()` (`src/rdkv_main.c`), wrap the final `uninitialize()` call with a check: skip it when `ret_curl_code == RDKV_UPGRADE_ERROR_STATE_RED`

## 6. Unit Tests

- [x] 6.1 Add test verifying `downloadFile()` returns `RDKV_UPGRADE_ERROR_STATE_RED` when state red flag is set after `dwnlError()`
- [x] 6.2 Add test verifying `rdkv_upgrade_request()` does not call `retryDownload()` when `RDKV_UPGRADE_ERROR_STATE_RED` is returned
- [x] 6.3 Add test verifying `main()` skips `uninitialize()` when return code is `RDKV_UPGRADE_ERROR_STATE_RED` — N/A for unit test (main() calls exit()), verified by code review + L2 integration tests
