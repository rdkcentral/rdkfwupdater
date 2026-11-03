## Context

This change implements the proposal for one story, RDKEMW-24384, on `topic/RDKEMW-24384`. Dates and evidence are anchored to November 3, 2026. Authoritative inventory is the workbook's `RDKFWUPDATER (2)` tab, not its other similarly named tab. Warning IDs in that tab differ from both historical report IDs and harness script suffixes.

The user confirmed latest product builds ship `rdkFwupdateMgr`, its policy, and the client library, and intended release variants match the relevant July cohort. Repository Makefile.am:211/228 builds the daemon. Historical absence on a July image MUST NOT exclude a present repository path. No new device or Mythos retest has been executed in this session.

The latest verified checkpoint is commit `a19652a` (local and remote topic branch); `develop` remains at `67b714d`. RDKFW-004 code and one boundary unit test exist, but no native compilation, unit suite, L2, device regression, or review approval is recorded. A previous JavaScript calculation checked arithmetic examples only; it is NOT validation of the C implementation.

## Goals / Non-Goals

**Goals:**

- Address all seven Critical/High findings in one story with rolling PR review and November 23 build handover target.
- Preserve public D-Bus signatures, valid artifact names, firmware eligibility rules, signed CDN queries, mTLS, and legitimate reboot behavior unless an approved security restriction requires a contract change.
- Remove the actual sink paths, test the reported primitives, and propagate failures without root-process crashes or out-of-directory writes/deletes.
- Before each PR provide executable L1/L2/on-device instructions, expected outcomes, safety constraints, and a result template. User executes builds/tests in VM/lab; assistant develops code and documents results only when supplied.
- Maintain `tasks.md` and `checkpoint.md` after each real milestone so resuming does not depend on chat memory.

**Non-Goals:**

- Independent remediation of RDKFW-008 through RDKFW-018; shared validation may incidentally protect those paths, but they are not separate scope commitments.
- Rewriting curl/download architecture, changing the XConf request wire format to JSON, or restructuring all duplicate orchestrator logic.
- Claiming verified closure from source inspection, a PoC's absent marker, a crash-free run without proven reach, or OpenSpec's artifact-ready status.
- Assistant-owned builds/tests, external Mythos retest effort, unapproved commits/pushes, or direct commits to `develop`.

## Decisions

### Finding-to-code and harness crosswalk

Paths below are repository-relative; harness paths are relative to sibling `../rdkfwupdater_harness`. Line anchors describe the observed snapshot and MUST be re-located by symbol after code changes.

| Warning | Severity | Owning source/function | Exact harness artifact | Evidence and implementation status |
|---|---|---|---|---|
| RDKFW-001 | Critical | `src/dbus/rdkv_dbus_server.c`, `process_app_request` DownloadFirmware, name check near 1050, cache near 1129, `rdkfw_download_worker` path near 2861 | `rdkfwupdater-develop_Critical_002/INSTRUCTIONS.md`; no standalone script | Unsafe name joins observed; July daemon absent; latest build applicability confirmed by user; patch/tests pending |
| RDKFW-002 | Critical (assessment) | `src/flash.c`, `flashImage` near 169/180 and `postFlash` near 330/361/399 | `rdkfwupdater-develop_Critical_001/xione_notinbuild/poc_rdkfwupdater-009.sh` | Untrusted state controls notifications/reboot branch; exploit impact and producer policy unresolved; coding paused by user, remains in scope |
| RDKFW-003 | High | `src/rdkv_main.c` and `src/rdkFwupdateMgr.c`, `checkTriggerUpgrade`, legacy PCI/PDRI and per-artifact Direct CDN joins | `rdkfwupdater-develop_High_001/xione_gated/poc_rdkfwupdater-001.sh` | Unsafe joins observed; July listener gate; use controlled MockXConf validation; patch/tests pending |
| RDKFW-004 | High | `src/include/rdkv_cdl.h:99` `appendDownloadedVersion`; `peripheral_firmware_dndl` in both orchestrators near 485/542 | `rdkfwupdater-develop_High_002/xione_confirmed/poc_rdkfwupdater-004.sh` plus `httpd_rdkfwupdater-004.sh` in same directory | July live-confirmed 355-byte output from 256-byte allocation; code committed a19652a; validation/review pending |
| RDKFW-005 | High | `src/json_process.c:37` `createJsonString`, all call sites including `MakeXconfComms` and `DirectCDNDownload`, D-Bus XConf worker | `rdkfwupdater-develop_High_003/xione_confirmed/poc_rdkfwupdater-005.sh` | Source underflow confirmed; July attack/baseline crash difference, not precise sanitizer attribution; patch/tests pending |
| RDKFW-006 | High | `src/dbus/rdkv_dbus_server.c` registration near 322/385, UpdateFirmware registration near 1557, path join near 1630; `src/dbus/rdkFwupdateMgr_handlers.c` flash worker | `rdkfwupdater-develop_High_004/INSTRUCTIONS.md`; no standalone script | Registration is not privileged authorization; sender ownership absent on update/download; allowed-principal policy unresolved |
| RDKFW-007 | High | `src/dbus/rdkv_dbus_server.c` URL prefix check near 1090 and `rdkfw_download_worker`; `src/rdkv_upgrade.c` transfer boundary | `rdkfwupdater-develop_High_005/INSTRUCTIONS.md`; no standalone script | http/https accepted; dependency controls redirects; HTTPS policy and tests pending |

### RDKFW-004: preserve a bounded complete-record invariant

Keep existing `appendDownloadedVersion` helper in common header so both binary copies share identical arithmetic. The invariant is `length < capacity` and NUL-terminated complete `name.tgz` entries. Entry-fit rejection MUST leave buffer and length unchanged; reserve comma, suffix, and terminator before copying.

Existing call sites reserve an entry before download. On transfer failure they restore the previous terminator/length; on capacity rejection they stop with failure before another download and persist only successful entries. This differs from unsafe partial/truncated list recording, intentionally. Add integrated tests for first failure, success then failure, next success after failure, exact 255-byte payload in 256-byte storage, one-over, single too-long entry, and repeated entries matching the harness. Both executable copies need integration coverage; a helper test alone cannot prove call-site rollback and no-download-on-overflow.

Alternative: append after a successful download and truncate. Rejected because an unrecorded successful artifact can produce inconsistent tracking and retries. Do not conflate this patch with filename traversal, malformed-token parsing, or the existing unrelated `strncat` issue.

### RDKFW-005: bounded appends and explicit caller failure

`createJsonString(char *, size_t)` currently constructs form-style `key=value&...` data, not JSON. Preserve existing field order, keys, optional fields, and normal-value encoding; a format or URL-encoding redesign is not part of this fix.

Introduce one capacity-aware append mechanism local to the request builder. Reject invalid output arguments. Check before delimiter writes or pointer arithmetic; handle negative `snprintf` results and truncation without advancing an invalid offset. Clear the output on failure. A proposed ABI-preserving sentinel is `SIZE_MAX`: verify includes and make ALL callers reject it before transport; return successful actual length otherwise. Do not implement the sentinel without updating every caller (legacy CLI, manager, shared Direct CDN, D-Bus worker). The existing Direct CDN `len >= JSON_STR_LEN` check recognizes a large sentinel; legacy `MakeXconfComms` currently does not check the builder result. Re-locate all calls before editing.

Use mocked device getters for independent builder boundary tests. Real manifest inputs also pass through independently vulnerable Medium RDKFW-010 `GetInstalledBundles`; for RDKFW-005 tests keep each input within the 400-byte getter buffer and overflow only the aggregate, or mock those getters. A crash from the Medium path is not proof of builder failure. Repairing that independent issue needs user agreement, not silent scope expansion.

Alternative: enlarge the fixed 1000-byte allocation. Rejected as insufficient because uncontrolled accumulation remains and any larger fixed size can be exceeded.

### RDKFW-001 and RDKFW-003: one filename policy, multiple entry boundaries

Add a small shared non-GLib filename validation helper in the existing common code boundary suitable for both binaries. Policy must accept legitimate single-component names and reject empty names, `.`/`..`, slash/backslash, control bytes, and length/path truncation. Decide whether percent-encoded traversal is rejected or preserved literally; NEVER decode after validating. Do not reject ordinary dots or underscores used by existing PCI/PDRI names.

Apply before D-Bus cache `g_file_test`, download-task state, worker path joins, and XConf path construction in both legacy and per-artifact modes. Preserve PDRI `.bin` normalization, but account for suffix bytes before constructing a destination. `validateImage` model matching is not a substitute for path validation. Audit any existing PDRI-only basename guard; consolidate rather than overwrite it.

Use the configured DIFW_PATH consistently instead of mixing hard-coded `/opt/CDL` cache checks with configured worker storage. The directory must be trusted, not attacker-controlled. Check symlink/pre-existing nonregular destinations at the true file-open boundary. `realpath`/existence checking followed by another pathname open is not sufficient for a hostile directory. If `dwnlutil` opens a pathname outside this repository, establish its no-follow/descriptor behavior before selecting a secure open implementation; do not claim full symlink resistance from a precheck.

Tests for lexical traversal use an innocuous canary file outside a disposable firmware directory and assert unchanged content, no outside writes/deletes, and no transfer/flash on rejection. Cover both binaries, configured nondefault directory, legacy and Direct CDN PCI/PDRI artifacts, and truncation. Preserve legitimate image eligibility and extension logic.

Alternative: substring rejection of `../` or only verifying model name. Rejected because absolute/separator/truncation variants and other joins remain.

### RDKFW-006: authorize peers, not caller-provided identities

Fetch credentials from the actual D-Bus unique sender/system bus, not the caller-supplied process name or handler text. Define an owner-approved allowed UID/group/policy for DownloadFirmware and UpdateFirmware. Root-only is a candidate only if legitimate production clients run as root; do not hardcode an invented service UID. A user-controlled RegisterProcess name is not a security credential.

Bind the provided full-width handler ID to `ProcessInfo.sender_id` for mutating operations; validate numeric conversion completely (overflow, sign, trailing characters) and audit pointer-key conversions to prevent identity aliasing. Preserve UnregisterProcess's existing sender check near 1897. Validate firmware storage location as well as basename before spawning flash work. Fail authorization or credential lookup closed without starting work. Revalidate deferred work against an operation identity snapshot if registrations can disappear before execution.

Do not expand service access by editing the test policy to make negative cases pass. The existing L2 policy permits default clients to send messages; add negative nonprivileged/foreign-peer tests. Unit tests can simulate credential failures; L2 tests must use distinct bus connections and, where required, distinct UIDs. Existing `iface()` tests often reuse SystemBus: explicitly use private connections for cross-sender tests.

Alternative: sender ownership only. Rejected because an unprivileged peer could register its own handler and still request a root flash. Process-name allowlisting also fails this threat model.

### RDKFW-007: HTTPS at input and redirect sinks

Use structured URL parsing appropriate to the owning abstraction (GLib URI parsing in D-Bus, curl URL API only if supported by deployed version). Require an HTTPS scheme and valid host, reject malformed schemes and embedded credentials where not required. Preserve signed Direct CDN query data exactly; do not reconstruct tokens or infer security from a `?` alone. Validate effective resolved URLs, including XConf/cache resolution, before transfer.

An HTTPS initial URL alone does not prevent HTTP redirection. Verify `dwnlutil`/`common_utilities` redirect settings and supported libcurl API; enforce HTTPS-only redirect protocols at every firmware transport/fallback path. On old curl use supported equivalent options rather than unsupported new APIs. If that change requires an external dependency patch, record it, owner, and version in the same story's delivery dependencies; do not mark the finding fixed locally without it. Keep TLS peer/host verification, mTLS, and intended Codebig behavior; never disable certificates to make tests pass.

Alternative: prefix-only handler check. Rejected as incomplete for effective cached URLs, malformed URLs, redirects, and signed downloads.

### RDKFW-002: trust the producer and preserve the real reboot policy

Both reads in flashImage/postFlash are compiled in the repository without a discovered Canary feature guard; runtime media-client/success and trigger conditions control reach. `CANARY` with trigger 3 suppresses maintenance critical/preparing notifications and selects PowerManager-dependent reboot behavior. This is not evidence that the file chooses an arbitrary firmware image or independently triggers a flash. Keep assessed Critical severity in inventory while security team reviews impact.

User paused implementation to finish RDKFW-004 first. Next obtain who writes the file, UID, creation/update lifecycle, valid values, behavior for missing/invalid files, and compatibility with existing producers. Candidate minimal implementation: shared secure reader, `open` with `O_NOFOLLOW` and nonblocking behavior, `fstat` on the opened descriptor, expected owner and regular-file checks, no group/other write permission, bounded exact-token parse, close descriptor on every path. A protected directory/state channel plus atomic producer update is preferable if the existing `/tmp` producer cannot provide trustworthy provenance. `chmod` on a pre-existing attacker-selected file is not a remedy. No source-wide writer was found in this repository; do not invent one.

Define safe default policy with owner before code. Handle first-read allocation failure as part of this reader boundary. Test trusted valid/missing/malformed values, untrusted ownership/modes, symlink, FIFO without blocking, and notifications/reboot commands captured by mocks. Stub flasher/reboot operations in disposable L2 environment; no live flash/reboot merely to test this control.

## Risks / Trade-offs

- [Fifteen-day target consumes all weekdays Nov 3-23] -> Maintain a separate two-working-day emergency reserve and disclose the resulting 13-day protected capacity before handover. Do not silently remove the reserve or call 15 days buffered; see delivery-plan.md.
- [RDKFW-002 paused but required for all-seven handover] -> Resolve producer/default-policy gate early; continue other fixes meanwhile, and raise schedule impact immediately if unresolved.
- [Unknown legitimate D-Bus principals] -> Agree an authorization policy and run positive tests for real clients before enforcement; sender binding alone does not close the finding.
- [External redirect/file-open behavior] -> Obtain dependency source/version in user VM; keep RDKFW-007 and symlink closure pending until enforcement is demonstrated.
- [Existing docs differ from source] -> Treat proposal/delta specs as desired behavior; use current source and evidence for APIs and do not perform unrelated base-document rewrites.
- [Multiple PRs on one topic branch] -> Use one PR with incremental commits or reviewer-agreed stacked/grouped PRs. Do not duplicate the same topic-to-develop PR seven times. Do not create extra branches without user authorization.
- [Test fixture contamination or harness setup gate] -> Restore config and scripts in finally/traps, prove entry/sink reach independently, record skip/setup failure separately from pass, test paths with harmless canary data.

## Migration Plan

Implement in small related commits, starting from the existing RDKFW-004 checkpoint and proceeding to RDKFW-005. Each review package includes exact commit, changed files, security invariant, user-owned test plan, supplied results, and unresolved limits. Reviewer work overlaps subsequent coding, not unit execution effort.

Before Nov 23 handover integrate reviewed fixes, obtain user-run unit/L2/on-device results for all seven, and attach a matrix with per-finding failure-path and normal-flow evidence. Handover does not equal DevQA success or external security closure. If an acceptance gate fails, keep the story open and escalate explicitly; the schedule does not authorize suppression or false pass markers.

Deploy the topic build into the intended release test image first. Security restrictions on callers/HTTP/custom paths need owner acceptance and client compatibility evidence. Rollback is an approved release decision to a known build with documented reopened exposure, not an automatic return to vulnerable behavior. Keep all commits/branches and user edits intact unless explicitly directed.

## Open Questions

1. Which producer writes xconfchecknow_val, with which UID, and what is the agreed default reboot/notification policy for missing or rejected state?
2. Which legitimate UIDs/groups/policies may invoke firmware-mutating D-Bus methods in the latest build?
3. Which actual common_utilities/dwnlutil and curl versions open artifacts and enforce redirect schemes? Can these be securely configured locally or does an external patch need integration?
4. Can the available MockXConf instance accept custom response fixtures and redirect endpoints? Existing tests use endpoints, but their server implementation is outside this repository.
5. Where will the two emergency days be protected: recover two planned days with capacity support before Nov 23, or reserve them after handover Nov 24-25 with no pre-handover schedule protection? This is an explicit planning decision, not an assumption of extra capacity.