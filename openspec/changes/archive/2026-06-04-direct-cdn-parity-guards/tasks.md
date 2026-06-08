## 1. Codebig Entry-Point Guard (PG-1)

- [x] 1.1 Add `context->direct_cdn` early-return guard at top of `codebigdownloadFile()` in `src/rdkv_upgrade.c` (after null-check block, before `server_type` local variable assignment)
- [x] 1.2 Add L1 unit test: call `codebigdownloadFile()` with `context->direct_cdn = true`, assert returns -1 without curl activity
- [x] 1.3 Add L1 unit test: call `codebigdownloadFile()` with `context->direct_cdn = false`, assert existing behavior unchanged

## 2. PDRI Filename Normalization (PG-2)

- [x] 2.1 Add `.bin` suffix check and normalization in `PDRI_UPGRADE` case of per-artifact switch in `checkTriggerUpgrade()` in `src/rdkv_main.c` (construct normalized path into `dwlpath_filename` without mutating `pResponse->cloudPDRIVersion`)
- [x] 2.2 Add L1 unit test: PDRI per-artifact with `cloudPDRIVersion` lacking `.bin` → verify download path has `.bin` appended
- [x] 2.3 Add L1 unit test: PDRI per-artifact with `cloudPDRIVersion` containing `.bin` → verify download path used as-is
- [x] 2.4 Confirm PCI_UPGRADE and PERIPHERAL_UPGRADE cases are NOT affected by normalization

## 3. HTTP 403 Retry Classification (PG-3)

- [x] 3.1 Add `http_code == 403` to retryable classification in per-artifact result handling in `checkTriggerUpgrade()` in `src/rdkv_main.c` (after existing 502/503 block)
- [x] 3.2 Add L1 unit test: per-artifact download returns HTTP 403 → assert `DIRECT_CDN_RETRY_ERR` returned
- [x] 3.3 Add L1 unit test: per-artifact download returns HTTP 404 → assert permanent failure (-1) returned (regression guard)

## 4. Validation

- [ ] 4.1 Run full L1 unit test suite — confirm no regressions from parity guards
- [ ] 4.2 Verify legacy path (`direct_cdn = false`) behavior unchanged in all three modified functions
