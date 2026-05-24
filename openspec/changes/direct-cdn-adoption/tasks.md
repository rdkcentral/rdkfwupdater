## 1. RFC Feature Gate

- [x] 1.1 Add `rfc_directcdn[RFC_VALUE_BUF_SIZE]` field to `Rfc_t` struct in `src/include/rfcinterface.h`
- [x] 1.2 Add `#define RFC_DIRECTCDN` macro in `src/include/rfcinterface.h`
- [x] 1.3 Implement `isDirectCDNEnabled()` function in `src/rfcInterface/rfcinterface.c`
- [x] 1.4 Add `rfc_directcdn` read to `getRFCSettings()` in `src/rfcInterface/rfcinterface.c`
- [x] 1.5 Add L1 unit tests for `isDirectCDNEnabled()` (true/false/absent cases)

## 2. XConf URL Path Branching & Codebig Bypass

- [x] 2.1 Modify `GetServURL()` in `src/deviceutils/device_api.c` to branch URL path on `isDirectCDNEnabled()`
- [x] 2.2 Modify `checkCodebigAccess()` in `src/device_status_helper.c` to return false when Direct CDN enabled
- [x] 2.3 Add L1 unit tests for `GetServURL()` path branching
- [x] 2.4 Add L1 unit tests for `checkCodebigAccess()` bypass

## 3. Enriched XConf Response Parsing

- [x] 3.1 Add `firmwareUrl`, `pdriUrl`, `remCtrlUrl` fields to `XCONFRES` in `src/include/json_process.h`
- [x] 3.2 Add conditional per-artifact URL parsing to `getXconfRespData()` in `src/json_process.c`
- [x] 3.3 Add `getPeripheralProduct()` function to `src/deviceutils/deviceutils.c` and declare in `deviceutils.h`
- [x] 3.4 Cache peripheral product in `BuildRemoteInfo()` in `src/deviceutils/deviceutils.c`
- [x] 3.5 Add PDRI `_PDRI_` substring validation in `processJsonResponse()` in `src/json_process.c`
- [x] 3.6 Add L1 unit tests for enriched parsing (directCdn=true and directCdn=false)
- [x] 3.7 Add L1 unit tests for `getPeripheralProduct()` and PDRI validation

## 4. Per-Artifact Download Orchestration

- [x] 4.1 Add `bool direct_cdn` field to `RdkUpgradeContext_t` in `src/include/rdkv_upgrade.h`
- [x] 4.2 Add `DIRECT_CDN_RETRY_ERR` define to `src/include/rdkv_cdl.h`
- [x] 4.3 Change `checkTriggerUpgrade()` signature to add `int upgrade_type` parameter in `src/include/rdkv_cdl.h`
- [x] 4.4 Update `checkTriggerUpgrade()` implementation in `src/rdkv_main.c` for per-artifact mode
- [x] 4.5 Update existing callers of `checkTriggerUpgrade()` to pass `upgrade_type=0` (legacy mode)
- [x] 4.6 Create `src/directcdn.c` with `DirectCDNDownload()` orchestrator function
- [x] 4.7 Create header declaration for `DirectCDNDownload()` (in `src/include/rdkv_cdl.h` or new header)
- [x] 4.8 Add `src/directcdn.c` to `Makefile.am` (`rdkvfwupgrader_SOURCES`)

## 5. One-Shot & Daemon Integration

- [ ] 5.1 Add Direct CDN branch in one-shot main flow (`src/rdkv_main.c`) after `initialValidation()`
- [ ] 5.2 Set `context.direct_cdn = true` in daemon download handler (`src/dbus/rdkFwupdateMgr_handlers.c`)
- [ ] 5.3 Modify `rdkv_upgrade_request()` to skip Codebig fallback when `context->direct_cdn == true`

## 6. Selective Retry Logic

- [ ] 6.1 Implement transient vs permanent error classification in per-artifact `checkTriggerUpgrade()` path
- [ ] 6.2 Verify retry loop in `DirectCDNDownload()` correctly skips succeeded artifacts
- [ ] 6.3 Add L1 unit tests for retry scenarios (fail→fail→succeed, permanent failure, max retry exceeded)

## 7. L1 Unit Tests (Integration)

- [ ] 7.1 Add `DirectCDNDownload()` unit tests to `unittest/` (mock XConf + per-artifact downloads)
- [ ] 7.2 Update `getRFCParameter` mock `Times()` expectations in existing tests
- [ ] 7.3 Add `checkTriggerUpgrade()` per-artifact mode unit tests
- [ ] 7.4 Verify all existing unit tests still pass with signature changes

## 8. L2 Functional Tests & Validation

- [ ] 8.1 Create `test/functional-tests/tests/test_directcdn_download.py` with RFC on/off scenarios
- [ ] 8.2 Update `run_l2.sh` to set Direct CDN RFC parameter
- [ ] 8.3 Run full L1 regression suite in Docker
- [ ] 8.4 Run full L2 regression suite in Docker
- [ ] 8.5 Static analysis (Coverity) clean check
