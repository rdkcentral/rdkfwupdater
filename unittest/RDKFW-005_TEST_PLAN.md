# RDKFW-005 PR Validation

Branch: topic/RDKEMW-24384-rdkfw005, based on develop independently of PR #330.
Status: code and tests developed; builds/tests NOT executed by assistant. User executes in configured VM/lab and records results before PR approval.

## Changes and Invariants

- createJsonString preserves existing form-style keys, ordering, optional fields and valid-input representation; it does not switch the POST body to JSON.
- All field writes, including ampersands and recovery=true, use checked snprintf append accounting. Length advances only after the complete field fits with a NUL terminator.
- Failure clears usable output and returns XCONF_REQUEST_BUILD_FAILED ((size_t)-1); NULL/zero-capacity input fails without access.
- Active CLI MakeXconfComms, DirectCDNDownload and D-Bus fetch_xconf_firmware_info reject failure before transport. The old manager MakeXconfComms is commented out and is not an executable call path.
- Independent Medium GetInstalledBundles buffer defects are not repaired by this patch. Do not attribute a crash in that getter to the request builder without a stack/trace.

## L1 - User VM

From the configured unittest build directory:

```sh
make rdkfw_main_gtest rdkFwupdateMgr_handlers_gtest
./rdkfw_main_gtest --gtest_filter='XconfRequestBuilderTest.*:MainHelperFunctionTest.createJsonString*'
./rdkFwupdateMgr_handlers_gtest --gtest_filter='FetchXconfFirmwareInfoTest.Failure_RequestBuildFailed_DoesNotStartTransport'
```

Expected: seven CLI tests (four new builder cases plus three existing model-selection cases) and one new handler test are selected and pass. Verify the selected count rather than accepting a zero-test successful exit. The new cases check invalid arguments, exact-fit/short/delimiter boundaries with guard bytes, aggregate overflow from individually bounded fields, and recovery field capacity. The handler test requires zero upgrade transport calls on builder failure.

Then run the complete rdkfw_main_gtest, rdkfwupdatemgr_main_flow_gtest, rdkFwupdateMgr_handlers_gtest and dbus_handlers_gtest suites after building those targets in the normal environment. Capture compilation failures rather than treating editor diagnostics as compilation evidence. Use the VM's supported sanitizer configuration if available to catch out-of-bounds access and leaks.

## L2 - Controlled MockXConf

Run only in the disposable integration environment; existing tests change /opt and /etc configuration and mock platform scripts. Preserve and restore configs, manifests, flags and scripts with finally/traps. Stub flash/reboot actions; use harmless artifacts.

1. Establish a normal request baseline using existing test/functional-tests/tests/test_imagedwnl.py, test_DCDN_imagedwnl.py and test_dbus_CheckForUpdate.py. Save the captured POST. Confirm valid model/version behavior, POST fields and legitimate download handling remain unchanged.
2. Exercise CLI legacy, shared Direct CDN and daemon CheckForUpdate separately. Supply individually bounded test metadata whose combined POST exceeds the 1000-byte allocation. Getters must remain within their own capacity; otherwise classify the test as blocked by an independent getter defect.
3. Confirm logs report failed XConf request construction, no XConf POST is received for the failed operation, no download/flash is started, and the daemon remains responsive. Repeat with smaller metadata and confirm a normal request succeeds.
4. With the VM's state-red test configuration, capture a normal recovery=true request and an aggregate request that cannot fit after adding that field; verify normal success versus clean failure/no partial POST.

The legacy and Direct CDN MockXConf listeners are configured through the existing helper (ports 50052 and 50065). Confirm actual fixture control with the lab owner; do not assume new custom endpoints exist. Empty server captures alone are not proof: demonstrate request-builder reach via logs and successful normal baseline.

## On-Device Harness - Optional Controlled Replay

Read sibling rdkfwupdater_harness/rdkfwupdater-develop_High_003/INSTRUCTIONS.md and the script header/META before replay. Exact inputs are xione_confirmed/poc_rdkfwupdater-005.sh and ho005_payload_package.json in that folder. Historical campaign staging commands/device IDs may not exist in this lab.

After approved staging on an isolated test device:

```sh
sh /tmp/harnessone/oldscans/poc_rdkfwupdater-005.sh
FULL_EXEC=1 sh /tmp/harnessone/oldscans/poc_rdkfwupdater-005.sh
```

Default probe alone is not regression validation. Require builder-input reach, controlled overflow rejection, absence of builder-attributed memory errors, and no partial transport. Inspect raw logs and script cleanup, not only POC-VULN-CONFIRMED absence. Original manifests can also exercise GetInstalledBundles; if its independent defect prevents isolation, attach separate builder unit/L2 evidence and explicitly record the blocked harness result.

## Evidence to Attach

Record full tested commit, VM toolchain/build flags, device/image, test filter and selected count, fixture values, expected/observed return values, unit XML/logs, request trace showing normal baseline and no partial POST, sanitizer output if used, cleanup and legitimate-flow regression. Mark NOT RUN/PASS/FAIL/BLOCKED distinctly. Security closure still needs approved Mythos/scan/security-review evidence.