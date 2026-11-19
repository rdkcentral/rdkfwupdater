# RDKFW-008 validation plan

## L1 unit tests

From the unit-test build directory, run:

```sh
./rdkfw_main_gtest --gtest_filter='PeripheralPathSecurityTest.*:DirectCDNOrchestratorTest.PeripheralTraversalRejectedBeforeDownload:DirectCDNArtifactPathTest.*'
```

These tests check unsafe names in both the regular peripheral path and the Direct CDN per-artifact path. They also check that exact-fit URL/destination buffers are accepted and that too-small buffers are rejected with no partial output. The Direct CDN traversal test expects zero calls to either downloader.

Run the existing valid peripheral-download and Direct CDN success unit tests too, to check that normal filenames still work.

## L2 functional tests

On a disposable target with Direct CDN enabled and the mock XConf/CDN services available, run the existing Direct CDN peripheral success and 404 tests:

```sh
RDKFW_FORCE_DIRECTCDN=true pytest -q -s test/functional-tests/tests/test_DCDN_peripheral_imagedwnl.py
```

This verifies the normal Direct CDN peripheral flow and its error handling. To cover the security rejection end to end, the mock XConf response must also provide an unsafe peripheral filename such as `../XR11_firmware_1`. Verify the updater rejects it before any artifact GET and that no outside canary or downloaded-version record changes. The current checked-in mock fixture does not include that unsafe response, so add a test fixture or response to the mock service before claiming this L2 case passed.

## On-device harness

The harness contains `rdkfwupdater-develop_Medium_001/xione_gated/poc_rdkfwupdater-002.sh`. It tests the legacy XConf peripheral path, not Direct CDN. Its safe preflight command on the device is:

```sh
sh /tmp/harnessone/oldscans/poc_rdkfwupdater-002.sh
```

The full trigger can write a controlled test file outside the firmware directory. Run it only on an isolated, authorized lab device after reviewing the script and cleanup steps. It is not a substitute for the Direct CDN L2 negative case above. If the full trigger is run, restore test state with:

```sh
sh /tmp/harnessone/oldscans/poc_rdkfwupdater-002.sh cleanup
```

No build or tests were run while preparing this change.