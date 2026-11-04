# Legacy Multiple-Remote L2 - PR #330 / RDKFW-004

Status: tests developed, not executed. Branch topic/RDKEMW-24384 (PR #330), not the separate RDKFW-005 branch. No production changes are included here. User runs this suite in the prepared Linux L2 container.

## Existing Coverage and Purpose

Existing tests/test_peripheral_imagedwnl.py uses a single peripheral fixture, checks exit codes, and checks a general image-flash log from a previous test. It does not assert two remote archives, complete version-list contents, per-remote skip behavior, or failed-entry rollback. It is also ordered and assumes earlier tests prepared configuration.

The new tests/test_legacy_multi_remote.py module is independent, opt-in and explicitly NON-Direct CDN. It disables and verifies SWDLDirect.Enable, removes RDKFW_FORCE_DIRECTCDN from the child process, configures the legacy XConf override, and proves the real legacy peripheral branch was reached using fresh logs. It does not use a Direct CDN firmware_URL or remCtrl_URL field.

## What Updating Both Remotes Means Here

1. /tmp/rc-proxy-params.json describes paired products and their installed versions. This fixture uses two different products, AB11-20 and CD15-10, and only FwVer to avoid unrelated audio/DSP serialization paths. It is an input simulation, not physical pairing.
2. The updater posts the remote metadata, consumes XConf's remCtrl comma-separated string, compares each requested version against installed/cached versions, and downloads eligible name.tgz artifacts sequentially.
3. It records successful downloads and calls eventManager("PeripheralUpgradeEvent", "/opt/CDL:<successful names>"). With IARM/CTRLM enabled, the implementation passes that directory and names to Control Manager; without those build flags the handoff may be logged or a no-op.
4. Control Manager/platform logic performs physical remote delivery/flashing. This repository's archive/list tests do NOT prove OTA success on paired hardware. Two physical remotes of the SAME model/version can share one downloaded archive; one comma-separated token is not necessarily one remote address.

For physical validation, capture the control-manager update-available event and payload, verify its IARM call succeeds, record each paired remote identifier/product/version before and after the OTA, and attach per-remote update completion/failure. A successful process return is not OTA proof.

When an IARM-enabled build logs PeripheralUpgradeEvent, the tests additionally assert its event_status payload contains the exact successful list. Set RDKFW_REQUIRE_REMOTE_EVENT=1 to require this evidence rather than allow a non-IARM build. This still does not establish successful Control Manager delivery or physical flashing; collect the call result and downstream OTA evidence separately. The standard L2 runner does not enable IARM, so lack of that handoff is printed explicitly, not described as an OTA pass.

## Prerequisites and Safety

- Use a DISPOSABLE root-owned L2 container/VM with PR #330 binary installed, RBUS, pgrep and existing mTLS/test certificate setup. No production boxes, paired physical remotes, or concurrent daemons/pytest workers. Stop rdkvfwupgrader/rdkFwupdateMgr and control-manager consumers before these fixture-driven checks.
- Tests replace device/version/config/log state and install fail-fast imageFlasher.sh and rebootNow.sh stubs. All managed regular files and two RFC values are backed up and restored using fixture cleanup. Symlinks/nonregular managed files fail setup. Directory creation and file metadata ownership are not full-machine rollback; dispose of the container after tests.
- eraseTGZItemsMatching can remove older archives sharing a requested product/type prefix. Use a throwaway /opt/CDL with NO unrelated AB11-20/CD15-10/A/B firmware archives. Only the exact fixture paths are backed up, not every possible product archive.
- Keep MockXConf on HTTPS and use its configured trusted certificates. Tests do not disable certificate checks or replace CA bundles. The existing legacy listener commonly uses port 50052, but configure actual scenario URLs below.
- The fixture saves/restores Direct CDN and debug RFCs, sets Direct CDN false and debug true for /opt/swupdate.conf override. Both RFCs must already be readable booleans. If secure-debug policy still blocks the override, report setup failure; do not claim firmware tests passed.
- Main PCI/PDRI updates must be disabled by the response contract below. A platform flash/reboot attempt is intercepted, recorded, and fails the test.

## MockXConf Setup - User-Owned Server Changes

The MockXConf server source is NOT in this repository. Implement these responses in your container using routes or fixture mechanisms you control. No URLs below claim a new endpoint already exists. The URL mapping file can point to any actual configured routes; use separate artifact base directories for failure modes so a 404 and a truncated response do not conflict.

### Paired Input Generated by the Test

```json
[
  {"Product": "AB11-20", "FwVer": "1.0.0.0"},
  {"Product": "CD15-10", "FwVer": "1.0.0.0"}
]
```

Equal/newer-version cases modify FwVer in this file. In server POST traces verify the real request advertises both products (for example remCtrlAB11-20 and remCtrlCD15-10 within additionalFwVerInfo). Do not build your response from a hard-coded assumption that only the first paired remote is present.

### Common Legacy Response

Return HTTP 200 with JSON:

```json
{
  "firmwareDownloadProtocol": "http",
  "firmwareFilename": "ABCD_firmware_test.bin",
  "firmwareVersion": "ABCD_firmware_test.bin",
  "firmwareLocation": "<YOUR_HTTPS_ARTIFACT_BASE_FOR_THIS_SCENARIO>",
  "additionalFwVerInfo": "",
  "rebootImmediately": false,
  "delayDownload": 0,
  "remCtrl": "AB11-20_firmware_2.0.0.0,CD15-10_firmware_2.0.0.0"
}
```

Replace the placeholder with a valid HTTPS directory URL. Its scheme, not the legacy protocol label, determines secure artifact retrieval. Main image/version match the fixture's /version.txt so PCI is not downloaded/flashed; PDRI is empty. Do not send dlCertBundle/dlAppBundle updates or Direct CDN URL fields. The remCtrl field is one STRING, not an array or two duplicate JSON keys. Names omit .tgz because the legacy downloader adds it.

For split_product_keys, replace the single remCtrl field with the following TWO DISTINCT string keys, in this order; do not also include remCtrl, since the containing-match parser would combine it again:

```json
{
  "remCtrlAB11-20": "AB11-20_firmware_2.0.0.0",
  "remCtrlCD15-10": "CD15-10_firmware_2.0.0.0"
}
```

The local unittest parser helper combines matching values with commas. Production uses a linked parser library; the L2 test verifies the actual installed implementation also yields both tokens.

### Scenarios and Artifact Server Behavior

Serve the same harmless nonempty valid .tgz bytes for ALL successful fixture artifact GETs. Set correct Content-Length and return 200. Supply the same .tgz file to pytest so it can compare downloaded bytes and print SHA-256 values; matching names alone is insufficient.

| Scenario key | remCtrl value | Artifact behavior |
|---|---|---|
| two_upgrades | AB11-20_firmware_2.0.0.0,CD15-10_firmware_2.0.0.0 | Both .tgz URLs return 200 + the supplied archive |
| split_product_keys | Separate product keys above, no single remCtrl field | Both .tgz URLs return 200; combined parsed list must match two_upgrades |
| middle_404 | AB11-20_firmware_2.0.0.0,CD15-10_firmware_3.0.0.0,CD15-10_firmware_2.0.0.0 | First/third return 200. Middle returns 404. Same-product cleanup on the second product does not erase the first product archive. |
| middle_curl_failure | Same three tokens as middle_404 | First/third return 200. Middle advertises a larger Content-Length than bytes delivered and closes the body early, on EVERY retry (curl partial-file error 18). Do not send a complete 200 body. No transient success during retries. |
| capacity | A_firmware_20,B_firmware_21,A_firmware_22,B_firmware_23,A_firmware_24,B_firmware_25,A_firmware_26,B_firmware_27,A_firmware_28,B_firmware_29,A_firmware_30,B_firmware_31,A_firmware_32,B_firmware_33,A_firmware_34,B_firmware_35 | All potential .tgz URLs may serve 200. Fixed updater must stop BEFORE requesting A_firmware_34.tgz. |

Capacity case uses paired products A and B at installed version 1.0.0.0. The source remCtrl string is 223 ASCII bytes, safely below the existing 256-byte response field limit and unrelated trailing-comma issue. Suffix/comma expansion makes 16 complete records exceed the accumulator. Expected successful prefix is the first 14 records (251 bytes); A_firmware_34 and B_firmware_35 must not be fetched. This is an adversarial list-boundary fixture, not 16 paired physical remotes. Older archives for repeated A/B products may be removed by normal cleanup; this test asserts the complete recorded prefix and no transfer of the rejected record, not persistence of every earlier archive.

The tests retain real retry/fallback behavior. A curl-18 fixture must keep nonzero HTTP status (200 with truncated body); connection errors with HTTP zero can trigger Codebig and do not test this specific scenario. Capture request trace and attach all actual codes.

## Run in Your Linux L2 Environment

Create /tmp/legacy-remote-urls.json with FIVE configured HTTPS URLs (replace these labels with your actual routes):

```json
{
  "two_upgrades": "<CONFIGURED_HTTPS_XCONF_URL_FOR_TWO_UPGRADES>",
  "split_product_keys": "<CONFIGURED_HTTPS_XCONF_URL_FOR_SEPARATE_PRODUCT_KEYS>",
  "middle_404": "<CONFIGURED_HTTPS_XCONF_URL_FOR_404_CASE>",
  "middle_curl_failure": "<CONFIGURED_HTTPS_XCONF_URL_FOR_TRUNCATED_BODY_CASE>",
  "capacity": "<CONFIGURED_HTTPS_XCONF_URL_FOR_CAPACITY_CASE>"
}
```

Run the happy/skip cases first after configuring only two_upgrades. From repository root:

```sh
export RDKFW_LEGACY_REMOTE_L2=1
export RDKFW_LEGACY_REMOTE_URLS=/tmp/legacy-remote-urls.json
export RDKFW_LEGACY_REMOTE_PAYLOAD=/path/to/the-exact-served-harmless.tgz

pytest -v -s test/functional-tests/tests/test_legacy_multi_remote.py \
  -k 'download_both_versions or current_remote_is_skipped or both_remotes_current'
```

Expected selection: four cases (both download, equal first, newer first, both current). Other scenario routes are not needed for this subset. Then run all nine cases once all server fixtures exist:

```sh
pytest -v -s --junitxml=/tmp/legacy-multi-remote.xml \
  test/functional-tests/tests/test_legacy_multi_remote.py
```

No parallel execution. Each subprocess has a 300-second timeout. The default run_l2.sh has NOT been changed to depend on these custom server routes. Without explicit opt-in, these tests skip; skips are not passes.

## Expected Results and Known Regression Signal

| Case | Expected updater outcome |
|---|---|
| Both need upgrade | Both archives match fixture bytes; exact list AB11-20_firmware_2.0.0.0.tgz,CD15-10_firmware_2.0.0.0.tgz; peripheral status 0 |
| Separate response keys | Both matching product-key values are combined and both eligible archives downloaded; same list as above |
| First equal/current | First not fetched, only second recorded/downloaded; peripheral status 0 |
| First newer | No downgrade for first; second still downloaded independently |
| Both current | No archive or downloaded-list file created; no Requesting upgrade log |
| First previously downloaded | No repeat download for first; second downloaded; old/new persisted entries separated by comma |
| Middle 404 | Failed version excluded; first and third retained with one comma; peripheral status -1; third remote path still reached |
| Middle curl failure | Same recorded outcome after real retries; no partial middle entry; peripheral status -1 |
| Capacity | First 14 complete records retained, length 251, clean capacity error, peripheral status -1; excess URLs never requested |

The pre-existing production code appends a batch with fopen("a")/fprintf("%s") but does not insert a separator between a prior nonempty batch and the new batch. Therefore the previously-downloaded case is EXPECTED TO EXPOSE a list-format regression on the current source (concatenation instead of the expected comma). It is not marked xfail and the assertion is not relaxed. Investigate it separately before claiming the complete legacy flow passes; this patch only adds tests and does not quietly repair production behavior.

checkTriggerUpgrade currently derives its final return from PCI/PDRI results without including peripheral_curl_code. A zero process return in failure/capacity cases is NOT accepted as proof of peripheral success. Tests assert fresh legacy branch evidence, actual peripheral status, payload bytes and exact list contents.

## Evidence and Defect Triage

Attach exact tested commit/binary path, VM/container image, XConf response and POST trace, GET names/counts/status, stdout plus fresh swupdate logs, printed archive SHA-256 and exact recorded list, JUnit XML and cleanup/RFC restoration evidence. If intended scenario response did not reach the updater, classify setup/blocker, not pass.

For device testing with actual paired remotes, do NOT run this fixture: it overwrites paired input and installs mock platform scripts. Instead retain real pairing/rc-proxy state, use owner-approved firmware and XConf response, monitor the IARM update-available payload, and collect per-remote OTA outcomes. This extra control-manager/device integration is beyond the download tests and remains required to claim both physical remotes upgraded.