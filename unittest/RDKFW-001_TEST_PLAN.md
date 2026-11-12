# RDKFW-001 - D-Bus Filename Traversal

Scope: reject unsafe firmware names before DownloadFirmware cache lookup/task admission, and recheck in the worker. Names use ASCII letters/digits, dot, underscore and hyphen, up to 255 bytes; empty/dot/dot-dot, separators, URL escapes/query characters and control bytes are rejected. Existing worker destination snprintf checks remain intact. Public D-Bus signature unchanged.

Not claimed: UpdateFirmware authorization, symlink/TOCTOU resistance in an attacker-writable storage directory, or HTTPS redirect control. Those are independent findings/dependency concerns. DIFW_PATH/device.properties must remain trusted. Real production filename compatibility must be confirmed before merge.

## User-owned validation (not executed by assistant)

In configured native unittest environment:

```sh
make rdkfw_main_gtest
./rdkfw_main_gtest --gtest_filter='FirmwareFilenameSecurityTest.*'
```

Expect two tests covering legitimate PCI/PDRI/peripheral names, NULL/empty/dot/separator/encoded/query/control inputs and 255/256-byte name limits. Run the complete component suite afterward.

In disposable prepared L2 container with rebuilt daemon:

```sh
pytest -v -s test/functional-tests/tests/test_dbus_DownloadFirmware.py -k 'rejects_unsafe_firmware_name_before_transfer'
```

Expect six cases: immediate RDKFW_DWNL_FAILED and Unsafe firmware filename, no outside-canary modification. Capture MockXConf request trace to prove no artifact request was started. Do not treat generic D-Bus errors, unavailable service or failed registration as passes. Existing L2 setup modifies config/platform scripts; use isolated container, no real flash/reboot. Run normal DownloadFirmware cases to verify supported names still work, and an overlong final destination case to retain path-length rejection.

On isolated release device use equivalent harmless canary names, capture D-Bus response and transfer trace; do not use system files as targets. Attach tested commit/image, unit XML, L2 results, server traces and positive download regression. Code review and approved security revalidation remain required before closure.