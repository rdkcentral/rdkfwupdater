# User-Executed PR Security Test Plan

Status: planned tests, NOT results. User owns compilation, native tests, L2, and on-device execution in VM/lab. Assistant provides code/test development and instructions; do not run builds/tests unless user explicitly requests execution. OpenSpec validation checks documents only, not firmware behavior.

## Definitions and Safety

- L1: native unit/component tests using existing GoogleTest mocks; no live firmware flash/reboot.
- L2: isolated Linux integration environment with D-Bus/RBUS/MockXConf and mock platform scripts.
- On-device: isolated lab image at the exact topic commit, with approved safe test artifacts and captured effects. Never use production firmware/device to exercise destructive paths.
- Test IDs below are proposed execution/evidence cases, NOT names of existing GoogleTests or existing mock endpoints. Existing test files/binary names below were inspected. Add targeted cases to those files; publish their actual names/filters before PR review.
- Save/restore /opt/swupdate.conf, RFC values, /etc/device.properties, firmware/cache/flag files, certificate setup and any mock flash/reboot scripts. Use finally/traps even on timeout/failure. Do not remove pre-existing files blindly.
- Stub imageFlasher.sh and rebootNow.sh in disposable L2; capture actual command arguments and exit codes. Existing mock helper comments do not prove production argument order: inspect flashImage's invocation before relying on script assertions.
- Supply valid model/version to exercise the intended branch. A model mismatch, empty server response, 404-only gate, failed setup, permission block, missing feature or timeout is NOT an overflow/traversal remediation pass.
- Prefer a scratch directory plus harmless canary content outside it; never use /etc, real boot images, credentials, or service configuration as write/delete targets.
- Record sanitizer/Valgrind memory results when the VM supports them, with build flags. Crash-free execution alone is not memory-safety proof.

## Existing VM Commands - User Only

Run inside the user's configured Linux project environment. Existing scripts install dependencies/change system configuration: inspect run_ut.sh and run_l2.sh before launching; do not invoke on this Windows workspace.

```sh
# Existing unit target, from configured unittest directory:
make rdkfwupdatemgr_main_flow_gtest
./rdkfwupdatemgr_main_flow_gtest --gtest_filter='RdkFwupdateMgrMainFlowTest.AppendDownloadedVersion_ExactFitAndRejectsOverflow:RdkFwupdateMgrMainFlowTest.PeripheralFirmwareDownload_Success_WritesDownloadList'

# Existing broad native targets after configured build:
./rdkfw_main_gtest
./rdkfwupdatemgr_main_flow_gtest
./rdkFwupdateMgr_handlers_gtest
./dbus_handlers_gtest

# Existing L2 files, from repository root in prepared lab environment:
pytest -v -s test/functional-tests/tests/test_imagedwnl.py test/functional-tests/tests/test_peripheral_imagedwnl.py
pytest -v -s test/functional-tests/tests/test_DCDN_imagedwnl.py test/functional-tests/tests/test_DCDN_peripheral_imagedwnl.py
pytest -v -s test/functional-tests/tests/test_dbus_DownloadFirmware.py test/functional-tests/tests/test_dbus_UpdateFirmware.py test/functional-tests/tests/test_dbus_RegisterProcess.py test/functional-tests/tests/test_dbus_UnregisterProcess.py test/functional-tests/tests/test_dbus_CheckForUpdate.py
```

The first filter references two actual existing test names; further proposed cases need to be implemented and their filters added before the relevant PR. Re-run affected cases after each rework; final integrated build runs relevant complete component suites.

The existing MockXConf helper points legacy responses at https://mockxconf:50052 and Direct CDN at https://mockxconf:50065. Server implementation and custom-fixture control are outside this repository: confirm real response injection/redirect configuration with user, do not invent route paths. Capture request/response trace proving the supplied fixture was consumed. Original production XConf access is NOT required.

## RDKFW-004 - Current PR a19652a

Existing tests: `unittest/rdkfwupdatemgr_main_flow_gtest.cpp` contains `AppendDownloadedVersion_ExactFitAndRejectsOverflow` (12-byte helper test) and normal `PeripheralFirmwareDownload_Success_WritesDownloadList`. Add integrated cases for both this manager target and `unittest/basic_rdkv_main_gtest.cpp` CLI target.

| Case | Level/input | Expected result/evidence |
|---|---|---|
| 004-L1-01 | Existing 12-byte helper exact-fit/rejected-third-entry test | a.tgz,b.tgz length 11; rejection preserves contents/length |
| 004-L1-02 | Proposed helper input that fills 255 payload bytes of 256 | Correct terminator; guard bytes unchanged; accepted actual length 255 |
| 004-L1-03 | Proposed first/next entry exceeding capacity by one; NULL/zero capacity; repeated names | Clean false/error; buffer and offset unchanged; no canary overwrite |
| 004-L1-04 | Proposed both-binary mocked downloads: success, failure after reservation, next success | Only successes recorded; rollback restores terminator/offset and retry can append |
| 004-L1-05 | Proposed both-binary aggregate list capacity exceeded | No download call for rejected record; function reports failure; previously successful list is bounded |
| 004-L2-01 | Valid single then multiple peripheral names via controlled response | Expected complete downloaded list and normal flow, no regressions |
| 004-DEVICE-01 | Approved long-list harness vector | Prove configured XConf consumed and successful earlier transfers; clean capacity error; no >255-byte newly recorded accumulation or crash; rejected record not fetched |

Harness inputs in sibling `../rdkfwupdater_harness/rdkfwupdater-develop_High_002/xione_confirmed/`: exact files `poc_rdkfwupdater-004.sh`, `httpd_rdkfwupdater-004.sh`, `payload_min_rdkfwupdater-004.tgz`. Consult that finding's INSTRUCTIONS.md, META/RUNMETA and PoC header for dependencies, invocation/marker contract, device paths and cleanup. Historical campaign bin/dut/device IDs are not assumed available now.

After staging ALL exact files to the approved isolated device directory, user may execute the documented probe and, only with safe artifact approval, full trigger:

```sh
sh /tmp/harnessone/oldscans/poc_rdkfwupdater-004.sh
FULL_EXEC=1 sh /tmp/harnessone/oldscans/poc_rdkfwupdater-004.sh
```

These commands are documentation only, not executed by the assistant. Safe probe alone cannot validate remediation. Harness marker logic can reflect old behavior; inspect raw hit counts/list content and current failure semantics, not simply marker absence. The 256-byte invariant covers newly accumulated entries; existing file content may already contain historical entries, so snapshot/restore or isolate that file before measuring.

## RDKFW-005 - Bounded POST and Caller Rejection

| Case | Level/input | Expected result/evidence |
|---|---|---|
| 005-L1-01 | Proposed builder mocked getters; baseline values | Same form-style field keys/order/representation and terminated actual-length output |
| 005-L1-02 | Exact-fit, aggregate one-over, delimiter-at-boundary, zero/NULL buffer | Clean explicit failure on invalid/oversize; no outside write; output not usable as partial request |
| 005-L1-03 | Builder failure injected into CLI/manager/Direct CDN/D-Bus callers | No transport starts; resources cleaned; existing error path reports failure without daemon exit |
| 005-L2-01 | Controlled individually safe fields whose combined request exceeds capacity | Builder failure observed and no partial XConf POST received; normal smaller request works |
| 005-DEVICE-01 | Approved original manifest vector baseline/attack | Capture exact reached getter/builder path and controlled return; no memory errors attributed to builder; cleanup preserved |

Existing candidate files: `unittest/basic_rdkv_main_gtest.cpp`, `unittest/rdkfwupdatemgr_main_flow_gtest.cpp`, relevant D-Bus fixtures, and L2 `test_dbus_CheckForUpdate.py`. Some fixtures mock createJsonString, so separate real-builder and caller-no-transport tests are required.

Exact harness: `../rdkfwupdater_harness/rdkfwupdater-develop_High_003/xione_confirmed/poc_rdkfwupdater-005.sh`; co-stage exact `ho005_payload_package.json`. Documented device invocation:

```sh
sh /tmp/harnessone/oldscans/poc_rdkfwupdater-005.sh
FULL_EXEC=1 sh /tmp/harnessone/oldscans/poc_rdkfwupdater-005.sh
```

Follow INSTRUCTIONS.md cleanup/trap. Do not mark failure of the independent Medium GetInstalledBundles overflow as proof this High fix failed/passed: isolate inputs or report the blocked vector with independent builder coverage. Historical crash with manifests is not precise sanitizer proof of builder location.

## RDKFW-001 and RDKFW-003 - Separate Evidence, Shared Implementation

| Case | Level/input | Expected result/evidence |
|---|---|---|
| 001-L1-01 | Proposed DownloadFirmware names ../outside.bin, absolute path, slash/backslash, control bytes, oversized name | Existing appropriate failure response before cache/task/transfer; unchanged outside canary |
| 001-L2-01 | Direct D-Bus valid name versus invalid name, trusted nondefault directory | Positive path succeeds under configured directory; negative paths create/delete nothing outside it |
| 003-L1-01 | Proposed eligible XCONFRES with bad cloudFWFile/cloudPDRIVersion, legacy and per-artifact, both binaries | Reject before transfer/path cleanup, not merely model mismatch |
| 003-L1-02 | Valid PDRI with/without .bin and normalization-overflow | Correct valid suffix behavior; clean rejection if final path cannot fit |
| 003-L2-01 | User-configured malicious MockXConf response matching real model/version eligibility | Trace response consumption; reject traversal; outside scratch canary unchanged; valid response downloads normally |
| PATH-DEVICE-01 | Approved scratch symlink/nonregular destination and failure cleanup | No outside target opened/deleted; record trusted-directory and true open-boundary checks, not raceable precheck only |

Use actual `test_dbus_DownloadFirmware.py`, `test_imagedwnl.py`, `test_DCDN_imagedwnl.py` as starting fixtures; implement new negative cases with cleanup in finally. Approved mock flasher/reboot and harmless bodies only.

RDKFW-001 has no standalone harness script. RDKFW-003 has exact `../rdkfwupdater_harness/rdkfwupdater-develop_High_001/xione_gated/poc_rdkfwupdater-001.sh`; read its INSTRUCTIONS.md/header. Prefer working MockXConf over recreating July dead-listener gates. If replayed, require proven response delivery and gate cleanup; 404 prevents flashing but may still write a destination before error, so never target a real system file.

## RDKFW-006 - Real Peer Authorization and Flash Paths

Use existing D-Bus functional files and a new private bus-connection test setup within them. Merely calling dbus.SystemBus twice can share a connection; verify distinct unique sender names and actual UIDs. Do not weaken D-Bus policy for success.

| Case | Level/input | Expected result/evidence |
|---|---|---|
| 006-L1-01 | Proposed mocked principal policy/lookup: authorized, denied, lookup failure | Allowed true owner continues; denied/unknown fails closed before task/transfer/flash |
| 006-L1-02 | Proposed own/foreign handlers, trailing/sign/overflow/high-width identities | Foreign/malformed/aliased identities denied; full valid ID resolves only its real owner |
| 006-L2-01 | Unprivileged denied peer registers its OWN handler then requests download/update | Mutating operations denied; no mock flasher/transfer invocation |
| 006-L2-02 | Allowed peer uses handler from DIFFERENT private connection | Denied even when both peers are otherwise allowed |
| 006-L2-03 | Allowed true owner with trusted file; outside location/traversal/symlink negatives | Positive progress preserved; negatives leave canary and registration untouched |
| 006-DEVICE-01 | Legitimate production principal invokes supported flow | Real client compatibility and privilege policy verified with safe platform stubs/artifacts |

No standalone harness script exists for this warning; do not fabricate one. Policy decision and credential evidence belong in results.

## RDKFW-007 - HTTPS and Redirect Trace

| Case | Level/input | Expected result/evidence |
|---|---|---|
| 007-L1-01 | Proposed URL parser valid HTTPS, HTTP, malformed scheme/host, credentials according to approved policy | Reject unsafe URLs before transport, preserve legitimate HTTPS |
| 007-L1-02 | Empty custom URL resolves to cached/XConf HTTP | No effective-URL bypass; fail before transfer |
| 007-L2-01 | Controlled HTTPS endpoint redirecting to HTTP; tested direct/fallback paths | Transport refuses downgrade; HTTP server capture proves ZERO firmware requests |
| 007-L2-02 | HTTPS response/secure redirect with valid TLS and signed CDN query | Valid flow succeeds, query unchanged; peer/host verification enabled |
| 007-L2-03 | Invalid server certificate or unsupported redirect scheme | Fail safely; never use insecure TLS workaround |
| 007-DEVICE-01 | Approved HTTPS artifact flow with actual dependency/curl versions | Compatible secure flow and downgrade refusal demonstrated on release configuration |

No standalone harness script exists. Initial URL rejection alone is incomplete evidence for redirect enforcement. Record dependency patch/version if transport belongs outside this source repo. Existing makeHttpHttps normalization is not proof a custom URL/redirect is rejected.

## RDKFW-002 - Paused Pending Producer/Behavior Decision

Only execute after owner decision and user unpause. Existing `src/flash.c` is included in the CLI unit target; mock device getters/JSON-RPC/event/flasher/reboot as needed. L2 tests currently contain no specific xconfchecknow_val/Canary negative case; these cases need development.

| Case | Level/input | Expected result/evidence |
|---|---|---|
| 002-L1-01 | Proposed trusted valid CANARY/non-Canary, missing/malformed/overlong state | Agreed policy followed; default behavior documented; no unsafe allocation/read handling |
| 002-L1-02 | Proposed unauthorized owner, group/other-writable state, symlink, FIFO | Reject safely without blocking/reading outside file; no attacker-induced Canary decision |
| 002-L1-03 | Trigger 3 and other triggers; maintenance true/false; reboot true/false; PDRI skip; PowerManager ON/other/error | Expected notifications and captured reboot commands; legitimate behavior unchanged except intended trust rejection |
| 002-L2-01 | Disposable mock success-flash and captured reboot script | Demonstrate actual postFlash branch, not just binary literals; no real flash/reboot |
| 002-DEVICE-01 | Trusted producer and platform owner-approved behavioral scenario | Producer permissions/lifecycle/default policy confirmed and logs/events agree |

Exact harness `../rdkfwupdater_harness/rdkfwupdater-develop_Critical_001/xione_notinbuild/poc_rdkfwupdater-009.sh` is SAFE-REACH only, even with FULL_EXEC; it never proves exploitation. Its probe plus mocked functional behavior does not by itself validate assessed Critical impact. Obtain security review evidence for impact/closure.

## Before Each PR - Evidence Template

| Field | Fill with actual values |
|---|---|
| Warning IDs / PR URL / reviewer | Pending |
| Branch / full commit hash / tested diff range | Pending |
| VM toolchain flags / dependency and curl versions | Pending |
| Device model / image / caller credentials / configured storage | Pending |
| Test ID / actual implemented test name / exact command | Pending |
| Fixture values / response / stage artifacts / independent reach proof | Pending |
| Expected outcome / observed return/events/requests/canary/list length | Pending |
| Status | NOT RUN / PASS / FAIL / BLOCKED / NOT APPLICABLE with reason |
| Unit report / sanitizer log / server trace / device log paths | Pending |
| Cleanup restore evidence / legitimate-flow regression | Pending |
| Review comments / rework commit / affected rerun | Pending |
| Security revalidation reference | Pending; external owner |

Only record PASS from supplied actual results. Security finding closure also needs approved scan/Mythos or documented security review, not just a reviewer-ready plan.