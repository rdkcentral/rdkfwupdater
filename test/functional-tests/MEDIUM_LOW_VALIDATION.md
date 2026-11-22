# Medium and Low integration validation

## Scope and status (2026-11-23)

Branch: topic/rdkfw_medium_low. Base: origin/develop 21dc1f3. This is a draft integration candidate, not security closure. No builds or tests were run by the assistant. Commit ancestry, conflict-marker checks, whitespace checks and read-only merge review were performed. Original PR branches are unchanged.

Legacy means rdkvfwupgrader; non-legacy means rdkFwupdateMgr. Direct CDN is a download mode, not a synonym for the daemon.

| PR | Finding | Included head | What it changes |
|---|---|---|---|
| 343 | 008 Medium | 7173f07 | Reject unsafe peripheral names and truncated URLs/paths |
| 348 | 009 Medium | 201078c | Constrain failure cleanup paths |
| 341 | 010 Medium | 4787a3f | Bound installed-bundle list writes |
| 338 | 011 Medium | eec18e3 | Bound RFC quote removal and output copies |
| 340 | 012 Medium | a0989db | Remove token-bearing log statements |
| 342 | 013 Medium | c5c4881 | Reject incomplete peripheral token fields |
| 344 | 014 Medium | d0a3e54 | Free registration-owned strings |
| 349 | 015 Medium | ad20c3a | Check storage/artifact trust before cache/update admission |
| 346 | 016 Medium | 73499e8 | Check opened cache descriptor before reading |
| 345 | 017 Medium | c45c5df | Restrict client signal subscriptions to the service sender |
| 347 | 018 Low | f3a50d6 | Check predictable state files before writing |

Conflicts were resolved by retaining separate malformed/path tests, filename/cleanup/trusted-write helpers, and registration destructor/cache-trust helpers. High PRs 337/330/332 are NOT included in this branch. Use a separately reviewed combined High+Medium image before large peripheral-list or outgoing-request stress: old High defects can mask these tests. Do not merge integration PRs blindly into develop; they contain commits also present in individual PRs.

## Outstanding issues before sign-off

| PR | Current gap / action |
|---|---|
| 343 | Latest audited L1 CI failed. Investigate its failure; the user's earlier five-test L1 pass is not a full-suite pass. Add legacy URL/path boundary and malicious Direct CDN L2 inputs. Current Direct CDN guard IS included. |
| 348 | 121-127 byte artifact paths pass this predicate but can truncate flash's 128-byte .header path; configured trailing slash can produce rejected double-slash destinations. Fix compatibility/length contract and add boundary tests. |
| 341 | Coverity status failed; inspect report. Two added tests do not cover multiple-manifest separators, incomplete-record rollback and remaining-node release. |
| 338 | Five unit cases exist; real provider/consumer compatibility still needs verification. |
| 340 | No new automated redaction test. Run synthetic token flow and inspect fresh logs for BOTH implementations. |
| 342 | Verify malformed-then-valid handling through actual process; exit zero alone is not proof. |
| 344 | Lifecycle test is not leak proof. Use allocation profiler on actual daemon removal and orderly shutdown. |
| 349 | Update admission may validate a caller directory different from configured flasher storage. INI-only DIFW_PATH parsing may ignore flat device.properties. Changed missing-file error text can break existing L2 assertion. Fix before trusting cache/update results. Does not pin a descriptor through flasher or prevent trusted/root replacement. |
| 346 | Symlink unit target is invalid JSON (/etc/passwd), so rejection alone does not prove no-follow behavior. Use protected valid JSON as the outside target. Coverity read-length alert needs inspection. For runtime reader coverage use DownloadFirmware file-cache fallback, not a CheckForUpdate path that overwrites the cache. |
| 345 | Callback simulations do not test bus subscription filtering. Need a linked client, rogue sender and legitimate sender. Separate reported memory concern is not addressed by sender restriction. |
| 347 | In-tree CLI fopen(STATEREDFLAG, "w") remains outside the helper; fix/verify this writer. Do not describe all remaining writes as external. Check existing consumers accept restricted file permissions. |

These concerns were not repaired in this integration task. Passing normal device sanity cannot cancel them.

## Existing L1 commands

Rebuild in the configured disposable Linux unit-test environment first. Run from the unit binary directory:

```sh
./rdkfw_main_gtest --gtest_filter='PeripheralPathSecurityTest.*:DirectCDNOrchestratorTest.PeripheralTraversalRejectedBeforeDownload:DirectCDNArtifactPathTest.*:FirmwareCleanupSecurityTest.*:PeripheralTokenSecurityTest.*:TrustedStateFileTest.*'
./rdkfwupdatemgr_main_flow_gtest --gtest_filter='RdkFwupdateMgrMainFlowTest.PeripheralUnsafePathSkippedBeforeValidDownload:RdkFwupdateMgrMainFlowTest.PeripheralMalformed*'
./rdkfw_deviceutils_gtest --gtest_filter='DeviceApiTestFixture.BundleBounds_*'
./rdkfw_interface_gtest --gtest_filter='InterfaceTestFixture.RfcValueBounds_*'
./rdkFwupdateMgr_handlers_gtest --gtest_filter='RdkFwupdateMgrHandlersTest.CacheSecurity_*:RdkFwupdateMgrHandlersTest.LoadXconfFromCache_*:RdkFwupdateMgrHandlersTest.SaveXconfToCache_*'
./rdkfw_main_gtest --gtest_filter='startFactoryProtectServiceTest.*'
./rdkfwupdatemgr_main_flow_gtest --gtest_filter='RdkFwupdateMgrMainFlowTest.StartFactoryProtectService_*'
```

Confirm each filter selects tests; zero tests or fixture skips are not a pass. FactoryProtect L1 checks behavior, not log redaction. Server cleanup/cache-admission helpers and sender filtering have no adequate direct L1 coverage in these PRs. Run full relevant unit suites after focused cases.

## Existing VM L2 commands

Use disposable root-capable L2 containers, freshly built binaries AND client library, mock services, fake platform scripts, and no concurrent updater. Existing setup modifies device files; never run these VM fixtures on production devices.

```sh
unset RDKFW_FORCE_DIRECTCDN
pytest -v -s test/functional-tests/tests/test_imagedwnl.py test/functional-tests/tests/test_imagedwnl_error.py test/functional-tests/tests/test_peripheral_imagedwnl.py
RDKFW_FORCE_DIRECTCDN=true pytest -v -s test/functional-tests/tests/test_DCDN_imagedwnl.py test/functional-tests/tests/test_DCDN_imagedwnl_error.py test/functional-tests/tests/test_DCDN_peripheral_imagedwnl.py
unset RDKFW_FORCE_DIRECTCDN
pytest -v -s test/functional-tests/tests/test_dbus_RegisterProcess.py test/functional-tests/tests/test_dbus_UnregisterProcess.py test/functional-tests/tests/test_dbus_CheckForUpdate.py test/functional-tests/tests/test_dbus_DownloadFirmware.py test/functional-tests/tests/test_dbus_UpdateFirmware.py
pytest -v -s test/functional-tests/tests/test_dbus_UnregisterProcess.py -k repeated_register_unregister_preserves_daemon_lifecycle
pytest -v -s test/functional-tests/tests/test_dbus_DownloadFirmware.py -k rejects_untrusted_cached_artifact
```

Direct CDN initial setup is in test_DCDN_imagedwnl.py; running peripheral tests alone can leave legacy endpoints installed. Verify RFC mode and mock URLs in logs. Standard listener is normally mockxconf:50052; Direct CDN fixtures use mockxconf:50065. General regression commands above are NOT dedicated coverage of every security rule.

## Required targeted VM scenarios (missing or incomplete automation)

| Finding | Inputs / action | Pass evidence |
|---|---|---|
| 008 | Mock unsafe names in regular and Direct CDN responses, then valid control. Test final URL/path just fitting and first-too-long. | No unsafe GET or cleanup/flash; outside canary unchanged; valid request still occurs. |
| 009 | Force failed direct and Codebig artifact requests; use outside path, near-limit path and trailing-slash config independently. | Outside files/header unchanged; intended safe failure files removed; positive download remains compatible. |
| 010 | Multiple metadata manifests: short list, exact fit, next incomplete entry, large list. Run ASan/Valgrind as available. | Complete comma-separated name:version records within capacity; no overwritten guard/memory errors or leaked remaining nodes. |
| 011 | Real RFC provider returns normal quoted/unquoted value, exact fit, oversized, empty and malformed cases. | Correct normal value; oversized safely rejected; valid RFC consumers still operate. |
| 012 | Synthetic token in reachable FactoryProtect flow for both executables; receiver records authorization. | Receiver gets token; fresh stdout and all relevant logs contain none of raw JSON/token/header. No real credential in reports. Feature not reached = blocked. |
| 013 | Missing product/type/version, empty/truncated fields, malformed-only and malformed followed by valid token. | Process remains alive; malformed entry has no GET or success record; valid later entry downloads. |
| 014 | One persistent daemon: repeated register/unregister, same-client re-registration and orderly daemon shutdown under profiler. | Correct lifecycle and IDs; owned strings released on removal and shutdown. RSS alone is not proof. |
| 015 | Symlink/writable file, unsafe parent, foreign owner, hardlink/FIFO/empty artifact; protected valid file and missing-file fresh download; nondefault DIFW_PATH and caller/configured location mismatch. | Immediate rejection before transfer/flash; canary unchanged; trusted positive flows succeed; validated and flashed artifact locations agree. Mock flash only. |
| 016 | Empty in-memory cache, no custom DownloadFirmware URL; cache symlink to valid JSON, FIFO, writable/foreign-owned cache; trusted valid JSON control. | Hardened reader reached; unsafe cache not read; accepted valid cache supplies intended URL. Fresh CheckForUpdate recovery is a separate step. |
| 017 | Real client pending operation: rogue bus sender sends matching progress/completion; legitimate daemon then sends them; repeat after service restart. | Rogue signal does not change callbacks/state; legitimate signal does; service-name ownership policy correct. |
| 018 | Every modified writer: symlink, FIFO, hardlink, wrong owner/writable file, trusted file and create/append controls, including state-red producer. | Outside target unchanged; no blocking; normal marker consumer works. Existing unguarded state-red write must be fixed first. |

Use fresh log offsets, unique canaries and cleared caches; reset fixtures between cases. Do not inject several unrelated bad inputs at once: an earlier guard can hide missing later checks. Record actual POST/GET requests, not only logs or exit status. These scenarios are plans, not claims of existing automated tests or passing results.

## Device sanity / feature checks

Build the integration firmware normally. Do NOT deploy test probes, fake flash/reboot scripts or synthetic pairing metadata from the VM.

1. Legacy and non-legacy normal update/no-update flows; Direct CDN where supported; expected status and cleanup.
2. Normal peripheral processing with supported paired products; verify actual records, events and remote versions where OTA is in scope.
3. RFC reads with actual provider; bundle reporting with installed metadata.
4. Daemon register/check/download/update/unregister and client progress/completion callbacks, including service restart.
5. Actual configured storage path/owner/modes; trusted cache hit and fresh download. A normal start does not exercise cache admission.
6. FactoryProtect/token flow only where enabled and approved; inspect fresh logs with redacted evidence. Do not trigger a flash merely to reach this feature.
7. State/marker creation and consumption with the stricter modes, including state-red where supported.

Startup sanity alone is insufficient for 012/014/015/016/017/018. VM targeted evidence plus compatible normal device feature behavior can validate the declared code scope, not root-peer protection or full security closure where listed gaps remain.

## Historical harness references

Paths are under sibling rdkfwupdater_harness. Original PoC IDs differ from canonical RDKFW IDs.

| Finding | Folder | Script |
|---|---|---|
| 008 | Medium_001/xione_gated | poc_rdkfwupdater-002.sh |
| 009 | Medium_002/xione_gated | poc_rdkfwupdater-003.sh |
| 010 | Medium_003/xione_confirmed | poc_rdkfwupdater-006.sh |
| 011 | Medium_004/xione_gated | poc_rdkfwupdater-007.sh |
| 012 | Medium_005/xione_infeasible | poc_rdkfwupdater-008.sh |
| 013 | Medium_006/xione_gated | poc_rdkfwupdater-011.sh |
| 014/015/016/017 | Medium_007/008/009/010 | No standalone PoC established by this review; use feature tests above |
| 018 | Low_001/xione_infeasible | poc_rdkfwupdater-010.sh |

After staging each script AND its referenced companions using that folder's INSTRUCTIONS.md, default probe syntax is:

```sh
sh /tmp/harnessone/oldscans/poc_rdkfwupdater-NNN.sh
```

Replace NNN with the mapped suffix above. Default probes do not prove fixed-build rejection. Full triggers can alter credentials/configuration/caches or crash the updater; review each script before use on authorized disposable hardware. The token probe cannot force a gated post-flash feature. Old gated/not-in-build/infeasible results and rc=0 are not new PASS evidence. Prefer controlled VM regression tests; attach cleanup proof if any PoC is used.

## Report checklist

For each PR record tested commit/image, selected test names/counts, PASS/FAIL/BLOCKED, fresh logs/request traces, before/after canary hashes, profiler evidence where required and restored fixtures. Attach XML/JSON reports. Keep integration sanity results separate from individual finding closure. Unexecuted or skipped cases remain pending.