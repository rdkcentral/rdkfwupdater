# DirectCDN L2 Production Findings

## Purpose

RDKEMW-14784 is primarily an L2 test-coverage change. While validating the new
DirectCDN scenarios end to end, two failures were traced to production behavior
rather than test setup or assertions. This document records the evidence and the
reason for the narrowly scoped source changes.

No mock-XConf source code was changed in this repository. The tests use the
running DirectCDN mock listener on port 50065.

## Finding 1: DirectCDN Artifact URL Scheme

### Observed behavior

The DirectCDN delay response was fetched and parsed successfully, but its
per-artifact URLs used `http://mockxconf:50065/...`. Port 50065 is an HTTPS
listener. The subsequent PCI artifact request therefore failed before receiving
an HTTP response:

```text
firmware_URL=http://mockxconf:50065/getfirmwarefile/DCDN/ABCD_firmware_test.bin
curl=52 http=0
DirectCDNDownload: pci_upgrade_status -1
```

Legacy firmware locations were already passed through `makeHttpHttps()`, but the
new DirectCDN fields (`firmwareUrl`, `pdriUrl`, and `remCtrlUrl`) bypassed that
normalization because they are complete per-artifact URLs.

### Production fix

`src/json_process.c` now applies the existing `makeHttpHttps()` behavior to all
three DirectCDN artifact URLs before download. This does not alter response
selection, retry behavior, or legacy mode. It makes DirectCDN use the same
transport normalization already applied to legacy firmware locations.

### Why a test-only workaround was rejected

Changing only the mock fixture or rewriting URLs in Python would hide a runtime
gap: a valid DirectCDN response containing an HTTP URL would still fail on a
device when the endpoint requires HTTPS. Normalizing at the point where response
URLs enter the download flow fixes that behavior for production callers.

## Finding 2: Already-Current PDRI Classification

### Observed behavior

`rdkv_upgrade_request()` returns `100` when `checkPDRIUpgrade()` determines that
the requested PDRI is already installed and no download is needed. The
DirectCDN per-artifact result mapping treated that value as a permanent failure:

```text
PDRI version of the active image and the image to be upgraded are the same.
No upgrade required.
checkTriggerUpgrade: upgrade_type 1 permanent failure curl=100 http=0
DirectCDNDownload: pdri_upgrade_status -1
DirectCDNDownload: Function return -1
```

This made the complete DirectCDN operation fail even though the PDRI artifact
required no work.

### Production fix

`src/rdkv_main.c` now maps return value `100` to success only for
`PDRI_UPGRADE` in the DirectCDN per-artifact branch. The artifact is logged as a
successful skip, allowing orchestration to continue. Real transport, HTTP, and
validation failures retain their existing mappings.

### Why a test-only workaround was rejected

Forcing every L2 scenario to report a different installed PDRI would avoid this
branch but leave the production defect intact. Devices legitimately encounter
an already-current PDRI, and that no-op must not fail an otherwise successful
DirectCDN operation.

## Test and Setup Adjustments

- DirectCDN XConf URLs in `rdkfw_test_helper.py` use port 50065, the running
  DirectCDN listener. Legacy URLs remain on port 50052.
- The delay test uses a distinct current PDRI fixture value so it exercises the
  intended response and download path where the environment supports it.
- The Codebig assertion verifies that the legacy fallback was not invoked. It
  does not require the error-only "skipping Codebig fallback" log on a successful
  DirectCDN download.
- The production DirectCDN implementation and mock server remain responsible for
  their normal runtime behavior; no success result is fabricated by the tests.
- The local DCDN certbundle fixture contains a DirectCDN artifact URL but does not
  contain the legacy `dlCertBundle` or `dlAppBundle` fields. Its L2 verification
  therefore checks successful DirectCDN artifact processing and flashing rather
  than legacy bundle-manager log messages.
- The local DCDN peripheral-404 fixture advertises a missing artifact. Its expected
  result is an HTTP 404 and DirectCDN return `255`, which verifies permanent
  artifact failure handling.
- The local invalid-PCI fixture contains `pciManufacturerId`, which is not a field
  consumed by the current parser. The test records successful response handling
  rather than asserting the unrelated legacy model-validation message.
- The unresolved-XConf test records DirectCDN return `-1`; an XConf connection
  failure exits before the artifact retry-exhaustion message is emitted.
- The local mock container provides the four valid DCDN artifacts and deliberately
  omits `ABCD_peripheral_notfound_test.bin`; valid artifact requests returned HTTP
  200 and the intentional missing-artifact request returned HTTP 404.

## Validation Evidence

After rebuilding and installing `rdkvfwupgrader`, the complete DirectCDN image
suite passed:

```text
pytest -v -s test/functional-tests/tests/test_DCDN_imagedwnl.py
10 passed in 13.71s
```

After the remaining DCDN assertions and state cleanup were aligned with the
verified local mock data, the complete ordered DirectCDN suite passed:

```text
RDKFW_FORCE_DIRECTCDN=true pytest -q -s \
  test/functional-tests/tests/test_DCDN_imagedwnl.py \
  test/functional-tests/tests/test_DCDN_imagedwnl_error.py \
  test/functional-tests/tests/test_DCDN_peripheral_imagedwnl.py \
  test/functional-tests/tests/test_DCDN_certbundle_dwnl.py
21 passed in 56.69s
```

Focused validation also confirmed the corrected delay path:

```text
2 passed, 8 deselected in 6.32s
```

The source build completed with `-Wall -Werror`, and `git diff --check` reported
no whitespace errors.

## Full L2 Follow-up: Optional DownloadFirmware URL

The restored full L2 runner exposed two D-Bus tests that expected
`DownloadFirmware` to reject an empty URL. This was a test expectation defect,
not a DirectCDN or production defect: the D-Bus worker explicitly supports an
empty URL by resolving it from XConf cache.

- With no cache, the D-Bus request is accepted asynchronously and the worker
  reports that no XConf cache is available.
- With a valid cache, the D-Bus request is accepted asynchronously and the
  worker uses the cached firmware URL.

The tests now verify those two supported behaviors. Focused validation passed:

```text
2 passed, 13 deselected in 8.58s
```

## Full L2 Follow-up: Standard XConf Fixture Isolation

Running the restored full suite exposed stale DirectCDN URLs at the start of the
standard XConf fixture files. `initial_rdkfw_setup()` previously appended normal
URLs to those files, allowing an earlier DCDN run to leave a port 50065 endpoint
as the first value. The standard flow consequently used the stale DirectCDN URL.

The standard setup now overwrites each XConf fixture file with exactly one normal
endpoint. This is test setup isolation only; it does not modify endpoint choice
or any production behavior. The complete standard image suite passed after this
correction:

```text
21 passed in 322.99s
```

## Full L2 Follow-up: UnregisterProcess Ownership

The full L2 runner also exposed that a D-Bus client could unregister a handler
created by another client. Registration stores the D-Bus sender ID, but the
UnregisterProcess path removed entries by handler ID without checking ownership.

The D-Bus handler now compares the caller sender ID to the tracked owner before
removal. A different sender receives `G_DBUS_ERROR_ACCESS_DENIED`; an unknown
handler ID continues to return `false`. This preserves existing unregister
behavior for the owning client while enforcing the ownership contract exercised
by the subprocess tests. Focused validation passed:

```text
2 passed, 5 deselected in 8.47s
```

## Scope and Risk

The two source changes are constrained to DirectCDN data handling:

- URL normalization executes only when DirectCDN is enabled.
- Return code `100` is accepted only for a DirectCDN PDRI operation.
- Legacy CDN behavior is unchanged.
- Other error codes remain failures or retryable results as before.