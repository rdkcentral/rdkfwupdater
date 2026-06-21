## 1. OpenSpec Specification Updates

- [ ] 1.1 Add Token Expiry Inner-Retry Short-Circuit requirement to `openspec/specs/direct-cdn-download/spec.md`
- [ ] 1.2 Add mTLS Bypass for Direct CDN Downloads requirement to `openspec/specs/direct-cdn-download/spec.md`
- [ ] 1.3 Add Inner Retry Loop Short-Circuit requirement to `openspec/specs/retry-recovery/spec.md`
- [ ] 1.4 Cross-check `openspec/specs/download-engine/spec.md` for consistency (verify no contradictions)

## 2. HTTP 403 Early-Return in retryDownload()

- [x] 2.1 Add `if (*httpCode == 403 && context->direct_cdn) break;` in direct-path while loop of `retryDownload()` in `src/rdkv_upgrade.c` (~line 1244)
- [x] 2.2 Add log message before break: token expired, breaking retry
- [x] 2.3 Verify no behavioral change when `direct_cdn == false` (manual code inspection)

## 3. mTLS Certificate Skip in downloadFile()

- [ ] 3.1 Add `context->direct_cdn && state_red != 1` guard before `getMtlscert()` in `#ifdef LIBRDKCERTSELECTOR` path of `downloadFile()` in `src/rdkv_upgrade.c`
- [ ] 3.2 Add same guard in `#ifndef LIBRDKCERTSELECTOR` path
- [ ] 3.3 Set `mtls_enable = -1` when guard active to force NULL cert in download call
- [ ] 3.4 Ensure `doHttpFileDownload()` / `chunkDownload()` receives NULL cert when guard active
- [ ] 3.5 Verify recovery cert path preserved when `state_red == 1` (manual code inspection)

## 4. Unit Tests — HTTP 403 Early-Return

- [x] 4.1 Test: `direct_cdn=true` + `httpCode=403` → immediate return from retryDownload (no sleep)
- [x] 4.2 Test: `direct_cdn=false` + `httpCode=403` → normal retry behavior (sleep called)
- [x] 4.3 Test: `direct_cdn=true` + `httpCode=200` → existing success break unchanged
- [x] 4.4 Test: `direct_cdn=true` + `httpCode=404` → existing 404 break unchanged

## 5. Unit Tests — mTLS Bypass

- [ ] 5.1 Test: `direct_cdn=true` + `state_red=0` → `getMtlscert` NOT called, NULL cert passed
- [ ] 5.2 Test: `direct_cdn=true` + `state_red=1` → `getMtlscert` IS called (recovery cert)
- [ ] 5.3 Test: `direct_cdn=false` + `state_red=0` → existing mTLS path unchanged (getMtlscert called)
