## Context

PR #249 (`topic/RDKEMW-9150`) implements the Direct CDN adoption (per `openspec/changes/direct-cdn-adoption/`). The implementation refactored the monolithic positional-parameter API from PR #120 into the context-struct architecture (`RdkUpgradeContext_t` → `rdkv_upgrade_request()`).

During review-readiness validation, three defense-in-depth behaviors present in PR #120 were found to be absent or weakened in PR #249's refactored implementation:

1. **Codebig entry-point guard**: PR #120's `codebigdownloadFile()` returns immediately if `rfc_directcdn=="true"`. PR #249's `codebigdownloadFile()` in `src/rdkv_upgrade.c` has no such guard — it relies solely on the `context->direct_cdn` check at line 522 (fallback path). The `isDwnlBlock()` flip at line 382 can redirect a DIRECT request to CODEBIG, bypassing the fallback guard entirely.

2. **PDRI `.bin` normalization**: PR #120 normalizes `cloudPDRIVersion` (appends `.bin` if missing) BEFORE the Direct CDN URL branch, so both legacy and Direct CDN paths get the normalized filename. PR #249's per-artifact switch in `checkTriggerUpgrade()` uses `pResponse->cloudPDRIVersion` raw — no `.bin` normalization.

3. **HTTP 403 retryable classification**: PR #120 classifies HTTP 403 as `DIRECT_CDN_RETRY_ERR` (token expiry → retryable, triggers XConf re-query). PR #249's per-artifact error classification only lists curl errors + HTTP 502/503 as retryable; HTTP 403 falls through to permanent failure.

### Constraints

- All three fixes MUST be additive guards/classifications — no control-flow restructuring
- MUST NOT change any function signatures, struct layouts, or Makefile dependencies
- MUST NOT modify behavior when `direct_cdn == false` (legacy path unchanged)
- MUST NOT alter existing `direct-cdn-adoption` task list or design decisions
- The existing OpenSpec proposal D1/D2/D3/D4/D5/D6 decisions remain unchanged

---

## Goals / Non-Goals

**Goals:**
- Restore behavioral parity with PR #120 for the three identified defense-in-depth mechanisms
- Enable PR #249 to pass review without "weaker than reference" objections on these points
- Keep each fix independently reviewable and independently testable

**Non-Goals:**
- Refactoring `codebigdownloadFile()` or `rdkv_upgrade_request()` internals
- Adding new retry layers beyond what PR #120 provided
- Addressing Copilot review comments that are inherited patterns (C1/C2/C9/C10/C11)
- Addressing comments that were already resolved (C3/C4) or incorrect (C6)
- Changing the `isDwnlBlock()` mechanism itself (pre-existing platform infrastructure)

---

## Decisions

### PG-1: Codebig guard placement — top of `codebigdownloadFile()` (straightforward parity)

**Decision:** Add a 2-line early-return guard at the top of `codebigdownloadFile()` in `src/rdkv_upgrade.c`, after the null-check block and before any Codebig signing logic:

```c
if (context->direct_cdn) {
    SWLOG_INFO("%s: Direct CDN mode - Codebig path not permitted, returning\n", __FUNCTION__);
    return -1;
}
```

**Rationale:** This is the exact behavioral equivalent of PR #120's:
```c
if (strncmp(rfc_list.rfc_directcdn, "true", 4) == 0) { return -1; }
```
The only difference is using `context->direct_cdn` (PR #249's D1 pattern) instead of re-reading the RFC global. The context field is always set from `isDirectCDNEnabled()` at call-site initialization, so the semantic is identical.

**Why this location:** The guard must be inside `codebigdownloadFile()` (not at the call site in `rdkv_upgrade_request()`) because the `isDwnlBlock()` flip changes `server_type` BEFORE the branch that selects `codebigdownloadFile()` vs `downloadFile()`. The existing fallback-path guard at line 522 only covers the "direct failed → consider Codebig" path, not the "operator-blocked DIRECT → forced CODEBIG" path.

**Design sensitivity:** None. This is a direct behavioral port of an existing proven guard.

**Alternative considered:** Guard at the `isDwnlBlock()` flip point (suppress flip when `direct_cdn==true`). Rejected — `isDwnlBlock()` is a pre-existing platform mechanism used for operational overrides; suppressing it may have unintended side effects for operator tooling. The entry-point guard is safer because it only prevents the Codebig-specific download path, allowing `isDwnlBlock()` to continue functioning for other operational purposes.

---

### PG-2: PDRI `.bin` normalization in per-artifact path (design-sensitive)

**Decision:** Add `.bin` suffix normalization for `artifact_file` in the `PDRI_UPGRADE` case of the per-artifact switch in `checkTriggerUpgrade()` (`src/rdkv_main.c`), using the same logic as the legacy path:

```c
case PDRI_UPGRADE:
    artifact_url = pResponse->pdriUrl;
    artifact_file = pResponse->cloudPDRIVersion;
    /* Normalize: ensure .bin suffix for local filename (parity with legacy path) */
    {
        size_t len = strlen(artifact_file);
        if (len > 0 && len < (sizeof(dwlpath_filename) - 5)) {
            if (strstr(artifact_file, ".bin") == NULL) {
                snprintf(dwlpath_filename, sizeof(dwlpath_filename), "%s/%s.bin",
                         device_info.difw_path, artifact_file);
            } else {
                snprintf(dwlpath_filename, sizeof(dwlpath_filename), "%s/%s",
                         device_info.difw_path, artifact_file);
            }
        }
    }
    break;
```

**Rationale:** PR #120 normalizes `cloudPDRIVersion` before any URL-path branching, using:
```c
if (strstr(cloudPDRIVersion, ".bin") == NULL) {
    strncat(cloudPDRIVersion, ".bin", sizeof(cloudPDRIVersion) - strlen(cloudPDRIVersion) - 1);
}
```
PR #249's per-artifact case currently does `snprintf(dwlpath_filename, ..., "%s/%s", path, artifact_file)` without `.bin` check.

**Design sensitivity:** **Medium**. Two open questions:
1. Should normalization modify `artifact_file` in place (as PR #120 does on `cloudPDRIVersion`) or construct the normalized path directly into `dwlpath_filename`? The latter is safer (doesn't mutate shared `XCONFRES` data) but diverges slightly from PR #120's approach.
2. Does the XConf server in Direct CDN mode guarantee `.bin` suffix on `additionalFwVerInfo` responses? If so, this guard is defensive-only and never fires. If not, it's functionally necessary.

**Recommended approach:** Construct normalized path into `dwlpath_filename` (option 2) — avoids mutating the `XCONFRES` struct which may be re-used across retry iterations in `DirectCDNDownload()`.

**Assumption to confirm:** The normalization only affects the LOCAL save path (filesystem filename), NOT the download URL. The URL comes from `pResponse->pdriUrl` and is used as-is (the CDN URL already knows the correct remote filename).

---

### PG-3: HTTP 403 retry classification (straightforward parity)

**Decision:** Add HTTP 403 to the retryable-error classification in the per-artifact download result handling within `checkTriggerUpgrade()` (`src/rdkv_main.c`), immediately after the existing HTTP 502/503 check:

```c
/* Transient HTTP-level failures → retryable (server-side temporary errors) */
if (curl_ret == 0 && (http_code == 502 || http_code == 503)) {
    SWLOG_WARN("%s: upgrade_type %d transient HTTP %d, retryable\n",
               __FUNCTION__, upgrade_type, http_code);
    return DIRECT_CDN_RETRY_ERR;
}
/* HTTP 403 → retryable (token expiry, requires fresh XConf query for new URL) */
if (curl_ret == 0 && http_code == 403) {
    SWLOG_WARN("%s: upgrade_type %d HTTP 403 (token expired), retryable\n",
               __FUNCTION__, upgrade_type, http_code);
    return DIRECT_CDN_RETRY_ERR;
}
```

**Rationale:** In Direct CDN mode, per-artifact URLs are signed/tokenized by XConf. HTTP 403 means the token has expired. PR #120 explicitly returns `DIRECT_CDN_RETRY_ERR` for 403, which causes `DirectCDNDownload()` to re-query XConf (fresh tokens) and re-attempt the download. Without this, a temporary token expiry becomes a permanent failure requiring full process re-invocation.

**Design sensitivity:** None. This is a direct behavioral port. The `DirectCDNDownload()` retry loop already handles `DIRECT_CDN_RETRY_ERR` correctly — it re-queries XConf on each iteration, which provides fresh URLs with fresh tokens.

**Alternative considered:** Only classify 403 as retryable when `context->direct_cdn == true`. Rejected — the per-artifact path is ONLY entered when `upgrade_type != LEGACY_ALL_UPGRADE`, which only happens from `DirectCDNDownload()` where `direct_cdn` is always true. No disambiguation needed.

---

## Risks / Trade-offs

| Risk | Mitigation |
|------|-----------|
| PG-1 returns -1 on Codebig path — caller may interpret as download error and retry inappropriately | The return from `codebigdownloadFile()` is -1 (same as existing failure path). In Direct CDN mode the `isDwnlBlock()` scenario is highly unlikely (requires operator to manually block DIRECT while Direct CDN is RFC-enabled). Even if it fires, the per-artifact retry path classifies this as a permanent curl failure and stops retrying. |
| PG-2 normalization fires on a filename that already has `.bin` | The `strstr(artifact_file, ".bin")` check prevents double-append. Safe no-op. |
| PG-2 normalizes filename that intentionally omits `.bin` (future XConf schema change) | Defensive only — if XConf guarantees `.bin`, the guard never fires. If it doesn't, this is necessary for local filesystem correctness. Either way, safe. |
| PG-3 classifies non-token-related 403 as retryable (e.g., ACL denial) | In Direct CDN mode, 403 from CDN endpoints is always token-related (URLs are pre-signed with time-limited tokens). ACL-style 403 would come from XConf itself, not from per-artifact CDN downloads. |
| Retry churn from 403 re-query loop if tokens always expire immediately | Max 3 iterations (existing `DirectCDNDownload()` cap). After 3 failed iterations, permanent failure. |

## Open Questions

1. **PG-2 XConf server guarantee**: Does the Direct CDN XConf response always include `.bin` suffix on `additionalFwVerInfo` (PDRI filename)? If confirmed, the normalization is purely defensive and can be documented as such in the reviewer response.

2. **PG-1 return value**: Should the early-return from `codebigdownloadFile()` return -1 (generic failure) or a distinct error code like `DIRECT_CDN_BLOCKED`? Returning -1 is simplest and matches PR #120's approach, but a distinct code would aid debugging if this path ever fires in production.

3. **PG-2 scope**: Should the normalization also apply to `PERIPHERAL_UPGRADE` case? PR #120 only normalizes PDRI; peripheral filenames have different naming conventions. Current recommendation: PDRI only (matching PR #120 exactly).
