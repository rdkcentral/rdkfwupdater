# RDKFW-002 case compatibility

The reader accepts complete TRUE or CANARY values regardless of ASCII letter case and normalizes them to uppercase. It allows one LF or CRLF ending. Extra words, suffixes, spaces, embedded NULs and oversized contents remain invalid. Invalid or missing contents retain non-Canary behavior. This does not authenticate the file producer.

## L1

Rebuild your unit-test target in a disposable Linux container, then run from its build directory:

```sh
./rdkfw_main_gtest --gtest_filter='Contents/XconfStateValidationTest.*' --gtest_output=xml:rdkfw002_l1.xml
```

Expected: 81 cases (27 inputs times exact reader output and the two call-site decisions). Lowercase canary, mixed CaNaRy and lowercase canary with LF/CRLF must select Canary; true and TrUe must return the normalized TRUE value. TRUEextra and canary x must still be rejected. Existing malformed cases remain covered. Also rerun MainHelperFunctionTest.flashImage* for existing power-state regression coverage.

## L2

Rebuild/install the production flash library using your normal VM process. From the repository root:

```sh
cc -std=c11 -Wall -Wextra -Werror -rdynamic -Isrc/include \
  test/functional-tests/tests/xconf_state_driver.c -ldl -o xconf_state_driver
export RDKFW_STATE_DRIVER="$PWD/xconf_state_driver"
export RDKFW_FLASH_LIBRARY=/usr/local/lib/librdksw_flash.so
export LD_LIBRARY_PATH=/usr/local/lib:${LD_LIBRARY_PATH:-}
pytest -q -s test/functional-tests/tests/test_xconf_state_validation.py --junitxml=rdkfw002_l2.xml
```

Adjust the library path for your installation. Expected: 54 cases (27 inputs times flash/post). The driver loads the production library and intercepts platform actions; PDRI avoids actual reboot and power-state RPC. It redirects test state to temporary files. Run only in a disposable container; this is library integration, not CLI/daemon or physical reboot coverage. The existing run_l2.sh does not select this test automatically. Missing driver/library is a setup failure, not a skip.

No builds or tests were run by the assistant. Attach tested commit, library hash and XML/console reports. The allocation/cleanup review issue and producer trust remain separate outstanding concerns; do not mark the security finding fully closed.