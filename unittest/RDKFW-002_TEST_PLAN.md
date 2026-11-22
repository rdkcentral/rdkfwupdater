# RDKFW-002: content-validation tests

Use a disposable Linux test container with no updater running. Do not run the L1 suite on a live device: existing unit tests and the new fixtures use fixed /tmp paths. Save any required state outside the container first.

## Inputs and expected values

- Accepted: TRUE and CANARY, each with no line ending, LF, or CRLF.
- Rejected: missing/empty file, FALSE, lowercase canary, leading/trailing spaces, CANARY followed by another token or line, repeated LF, bare CR, embedded NUL, exactly nine bytes, and oversized content.
- Missing/invalid contents return an empty value and preserve the existing non-Canary decision. Only accepted CANARY suppresses the Canary-controlled events for trigger 3.

## L1

Rebuild the normal unit-test target in your VM. From its build directory run:

```sh
./rdkfw_main_gtest --gtest_filter='Contents/XconfStateValidationTest.*' --gtest_output=xml:rdkfw002_l1.xml
```

Expected: 57 tests (19 inputs times three assertions): exact reader output, flashImage critical-update event, and postFlash preparing-to-reboot event. Every test must pass, not skip. Both decision tests use PDRI so no real flash/reboot or power-state RPC is needed. The exact reader check uses a GTEST_ENABLE-only accessor; it is absent from the production library.

Also rerun existing Canary power-state regressions:

```sh
./rdkfw_main_gtest --gtest_filter='MainHelperFunctionTest.flashImage*' --gtest_output=xml:rdkfw002_canary_regression.xml
```

## L2: installed shared-library integration

Install the freshly built production librdksw_flash.so and its dependencies using your normal L2 build process. Do not build flash.c with GTEST_ENABLE for this step. From the repository root, compile the test driver only:

```sh
cc -std=c11 -Wall -Wextra -Werror -rdynamic -Isrc/include \
  test/functional-tests/tests/xconf_state_driver.c -ldl -o xconf_state_driver
export RDKFW_STATE_DRIVER="$PWD/xconf_state_driver"
export RDKFW_FLASH_LIBRARY=/usr/local/lib/librdksw_flash.so
export LD_LIBRARY_PATH=/usr/local/lib:${LD_LIBRARY_PATH:-}
pytest -q -s test/functional-tests/tests/test_xconf_state_validation.py --junitxml=rdkfw002_l2.xml
```

Adjust the library path if your installation uses lib64 or another directory. Expected: 38 tests (19 inputs times flashImage/postFlash). Missing driver/library or loader errors fail setup; they are not a pass.

The driver loads the actual installed library and supplies process-local platform hooks. It records critical/preparing events, verifies mocked flash and final status/cleanup calls, redirects /tmp and /opt fopen operations to pytest's temporary folder, and refuses unexpected secure commands. No real firmware transfer, flash, reboot, device-property edits, or state-file replacement is required. This is not a security sandbox: use only a disposable container with the normal library dependencies installed. Any unexpected abort/loading failure must be investigated, not bypassed.

L2 verifies the effects of the validation in the actual library; L1 additionally proves that TRUE was accepted rather than merely falling back to non-Canary behavior. These tests do not run the CLI/daemon end to end or verify external file ownership, symlink protection, read errors, allocation failure, races, or physical power/reboot behavior. The existing allocation-failure review concern remains outside this test-only follow-up.

The main run_l2.sh currently lists test files explicitly and does not invoke this new driver test. Run the command above separately. Neither driver compilation nor tests were executed by the assistant.

## Evidence to attach to PR #350

Record the tested commit, production library path/hash, driver compile result, L1/L2 XML reports, and console output. A green existing CI job does not prove this new matrix ran. The historical poc_rdkfwupdater-009.sh tests reachability and untrusted CANARY creation, not these rejection rules; do not use it as a substitute.