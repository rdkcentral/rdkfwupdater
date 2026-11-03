## 1. Scope and Resume Protocol

- [x] 1.1 Confirm authoritative RDKFWUPDATER (2) inventory: 18 findings total, only RDKFW-001..007 (2 Critical, 5 High) scoped to one RDKEMW-24384 story; user confirmed latest daemon/client applicability.
- [x] 1.2 Verify a19652a on topic/RDKEMW-24384 and origin/topic/RDKEMW-24384; develop remains 67b714d as of Nov 3.
- [ ] 1.3 At each resumed session read checkpoint.md, design.md, delivery-plan.md, test-plan.md, tasks.md and the touched spec; verify git status/branch/current symbols before editing; preserve unrelated user changes.
- [ ] 1.4 Resolve RDKFW-002 state producer, accepted values, ownership and missing/rejected-state default; record owner decision before unpausing its coding.
- [ ] 1.5 Confirm RDKFW-006 allowed production caller credentials and RDKFW-007 dependency/curl and MockXConf fixture capabilities; record concrete policy/versions, not guessed values.
- [ ] 1.6 Obtain the capacity decision for the two emergency working days while retaining Nov 23 handover and all-seven scope; disclose any pre-handover reserve gap.

## 2. RDKFW-004 - Finish Existing Commit and Evidence First

- [x] 2.1 Implement appendDownloadedVersion in src/include/rdkv_cdl.h and replace unsafe accumulation in both peripheral_firmware_dndl implementations; commit a19652a already exists.
- [x] 2.2 Add existing AppendDownloadedVersion_ExactFitAndRejectsOverflow GoogleTest in rdkfwupdatemgr_main_flow_gtest.cpp; source verified, not executed.
- [ ] 2.3 Extend helper tests for 256-byte capacity exact fit, one-over, oversized first item, zero capacity, invalid arguments, multiple records, and unchanged buffer/offset on rejection with boundary canaries.
- [ ] 2.4 Add integrated tests for both executable paths: normal success, reserve rollback on library/HTTP failure, next success after failure, no transfer for excess entry, and earlier successful records retained.
- [ ] 2.5 Provide the RDKFW-004 PR test plan from test-plan.md and actual unit test names/commands; user runs native L1 and harmless L2/on-device vector, including approved harness invocation.
- [ ] 2.6 Record user-supplied test results and obtain rolling reviewer feedback; repair failures/comments and update checkpoint before treating RDKFW-004 as developer-validated.

## 3. RDKFW-005 - Request Builder and All Callers

- [ ] 3.1 Re-locate createJsonString and every caller; confirm form-style payload and existing buffer/error contracts, including legacy CLI/manager, DirectCDNDownload and D-Bus worker.
- [ ] 3.2 Implement bounded field/separator appends with invalid argument and format/truncation handling; return an explicit failure sentinel and clear usable output without changing successful payload format.
- [ ] 3.3 Update every caller to reject builder failure before transport and clean up owned allocations; verify no unchecked call path remains.
- [ ] 3.4 Develop independent unit tests with mocked device getters: minimum capacity, exact fit, one-over aggregate, optional fields, many fields, delimiter boundary, and no transport on failed construction in all callers.
- [ ] 3.5 Provide RDKFW-005 L1/L2/on-device PR instructions; isolate aggregate overflow from Medium GetInstalledBundles weakness; user executes and supplies baseline/attack and memory-safety evidence where supported.
- [ ] 3.6 Record results, reviewer decision, and any remaining coupled-input limitation; repair local defects before moving its task state to validated.

## 4. RDKFW-001 and RDKFW-003 - Shared Filename Policy

- [ ] 4.1 Agree legitimate filename grammar and configured directory trust; inspect existing PDRI guards, all legacy/Direct CDN joins, D-Bus cache checks, cleanup and external file-open boundary.
- [ ] 4.2 Implement one shared non-GLib filename/capacity policy reused by both binaries and daemon; reject traversal/separators/control values and path/suffix truncation without modifying valid names.
- [ ] 4.3 Apply validation before D-Bus cache/task admission and worker joins (RDKFW-001); use the same trusted configured DIFW_PATH for cache and transfer.
- [ ] 4.4 Apply policy to PCI/PDRI legacy and per-artifact Direct CDN joins in both orchestrators (RDKFW-003); preserve PDRI normalization and eligibility.
- [ ] 4.5 Establish secure file-use/cleanup behavior for symlink or nonregular destinations; if external dependency changes are required, record owner/version and integrate before claiming sink protection.
- [ ] 4.6 Develop unit and D-Bus negative tests for ../, absolute paths, backslash/slash, control bytes, oversized names, suffix overflow and nondefault trusted directory; verify no outside write/delete/flash.
- [ ] 4.7 Prepare a controlled malicious MockXConf response for legacy and Direct CDN PCI/PDRI, with valid model/version eligibility, disposable outside canary, and harmless artifacts; confirm endpoint configuration with user rather than inventing one.
- [ ] 4.8 Provide PR test instructions and user executes L1/L2/device checks; record separate RDKFW-001 and RDKFW-003 evidence while counting shared implementation once.
- [ ] 4.9 Obtain reviewer feedback for shared validator/callers and rerun user-owned affected tests after rework.

## 5. RDKFW-006 - Firmware Mutation Authorization

- [ ] 5.1 Implement agreed allowed-principal credential/policy check using actual D-Bus sender; fail closed on lookup failure, not process-name or registration-based authorization.
- [ ] 5.2 Validate full-width handler parsing and sender ownership on DownloadFirmware/UpdateFirmware; audit narrowing/identity aliases and preserve existing UnregisterProcess ownership guard.
- [ ] 5.3 Constrain LocationOfFirmware and name before flash task creation using shared path policy and secure file-use invariant; preserve legitimate asynchronous API behavior and credential lifetime semantics.
- [ ] 5.4 Develop unit tests for denied own-registration caller, authorized foreign-handler caller, failed credential lookup, malformed/overflowed handler, invalid storage path, and successful legitimate client.
- [ ] 5.5 Provide L2/device instructions using distinct private bus connections and distinct credentials where needed; stub flasher/reboot so denied requests demonstrate zero privileged side effects.
- [ ] 5.6 User supplies test evidence; obtain security/owner approval of authorization policy plus rolling code review and record outcomes.

## 6. RDKFW-007 - Secure URL and Transport Boundary

- [ ] 6.1 Inspect deployed GLib/libcurl/dwnlutil versions and actual direct/fallback redirect controls in user's dependency source; identify the exact enforceable sink.
- [ ] 6.2 Implement structured HTTPS/effective URL checks in D-Bus and resolved XConf/cache paths, preserving signed query tokens and supported legitimate HTTPS behavior.
- [ ] 6.3 Enforce HTTPS-only redirect protocols at actual transfer/fallback boundary using APIs supported by deployed curl; integrate required dependency patch or keep blocker explicit.
- [ ] 6.4 Develop initial HTTP/malformed URL, cached HTTP, HTTPS-to-HTTP downgrade, HTTPS positive/secure redirect, signed query preservation and invalid certificate rejection test cases.
- [ ] 6.5 Provide PR test instructions with controlled redirect endpoint confirmed by user; user supplies server request trace proving no HTTP retrieval on rejected downgrade.
- [ ] 6.6 Obtain reviewer feedback for transport/caller changes and record dependency version and all user-owned test results before validation completion.

## 7. RDKFW-002 - Unpause Only After Behavior Decision

- [ ] 7.1 After task 1.4 and user unpause, implement shared secure state read or approved protected-state channel for flashImage/postFlash; enforce authenticated producer and regular/no-follow/bounded value handling without FIFO blocking.
- [ ] 7.2 Implement owner-approved missing/rejected-state default and preserve legitimate Canary/maintenance/reboot branches; handle allocation/read errors without crashing.
- [ ] 7.3 Develop tests for trusted valid/missing/malformed/oversized state, unauthorized owner/mode, symlink and FIFO; assert captured notifications/PowerManager/reboot calls for trigger 3 and non-Canary cases.
- [ ] 7.4 Provide safe L1/L2/device reach and behavioral instructions with mock flash/reboot; user executes and supplies evidence; do not use a destructive live flash to prove branch behavior.
- [ ] 7.5 Obtain reviewer and owner/security behavior/impact approval; retain assessment Critical label unless approved reassessment is recorded.

## 8. Rolling Review and Nov 23 Handover

- [ ] 8.1 Before each PR/update deliver finding IDs, commit range, security invariant, exact user-run L1/L2/device commands, expected results, cleanup, evidence template and limitations; use one incrementally updated PR or explicitly approved grouped/stacked branches.
- [ ] 8.2 Schedule five reviewer working days in parallel (never fewer than three); complete every substantive review and developer comment rework before handover; allow re-review for substantive late changes.
- [ ] 8.3 User executes integrated component regression across legacy/Direct CDN, CLI/daemon, legitimate D-Bus clients, downloads, request payload and post-flash behavior; attach actual build/commit and reports.
- [ ] 8.4 Freeze reviewed code by Nov 20 target and provide all-seven developer-tested build plus per-finding evidence to DevQA Nov 23 target; if a required gate fails, record and escalate instead of declaring false completion.
- [ ] 8.5 After each milestone update tasks.md and checkpoint.md with commit, branch, current status, supplied evidence, unresolved questions and exact next task. Do not repeat the existing RDKFW-004 implementation or commit to develop.

## 9. DevQA and Security Closure - Not Assistant-Owned Execution

- [ ] 9.1 DevQA starts only after handover and records its own targeted/component results; developer addresses reported defects and user reruns affected tests before accepting rework.
- [ ] 9.2 Attach code review, negative/boundary results, relevant unit reports, component/L2/device results, and static/security scan evidence where applicable to RDKEMW-24384.
- [ ] 9.3 Security team supplies Mythos/approved scan/documented security-review revalidation before closing each scoped finding; external retest is excluded from developer effort and Nov 30 closure is not assumed without evidence.