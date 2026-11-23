# RDKFW-005 outgoing-request L2

Run only in your configured disposable L2 container, with no updater running. Install binaries/libraries built from PR #332's current commit first. No firmware is transferred: the process-local probe blocks curl_easy_perform and records each attempt.

From the repository root:

```sh
cc -std=c11 -Wall -Wextra -Werror -shared -fPIC \
  $(pkg-config --cflags libcurl) test/functional-tests/tests/xconf_request_probe.c \
  -ldl -o xconf_request_probe.so
export RDKFW_REQUEST_PROBE="$PWD/xconf_request_probe.so"
export RDKFW_REQUEST_BINARY=/usr/local/bin/rdkvfwupgrader
export LD_LIBRARY_PATH=/usr/local/lib:${LD_LIBRARY_PATH:-}
pytest -q -s test/functional-tests/tests/test_xconf_request_overflow.py \
  --junitxml=rdkfw005_overflow_l2.xml
```

Expected: two passed tests, one for standard XConf and one for Direct CDN. Each runs a normal-input control and an overflow case. The production createJsonString is called unchanged; three metadata fields are expanded to 390 bytes only while it runs. Each field fits its 400-byte staging buffer, but together they exceed the 1000-byte outgoing request. The probe does not mock the builder's return value.

Normal input must build a nonempty request and reach the blocked transport. Overflow must return the failure sentinel, clear the request, make zero transport calls and produce a nonzero CLI exit status. Missing binaries, loader errors, wrong mode or initialization failure must fail the test, not count as overflow rejection. The normal control intentionally cannot complete a network update.

The probe suppresses sleep, usleep and nanosleep process-locally, so retry paths cannot consume the test timeout. The pytest timeout is 30 seconds; a missed interception fails quickly instead of waiting through production retry delays. This covers the actual CLI standard and Direct CDN callers with controlled process-local inputs. It is not a live MockXConf exchange, daemon integration test or device OTA test. The existing FetchXconfFirmwareInfoTest.Failure_RequestBuildFailed_DoesNotStartTransport covers the D-Bus caller at L1; daemon overflow L2 remains separate. The main run_l2.sh lists suites explicitly and does not automatically run this opt-in test.

Capture the tested commit, installed binary/library hashes, probe compile output and XML/console results. No compile or test execution was performed by the assistant. Production initialization may touch normal container state; rebuild/reset a disposable container afterward. Do not run the preload probe on a device.