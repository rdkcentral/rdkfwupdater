# RDKFW-003 - XConf Artifact Filename Traversal

User executes builds/tests; assistant has not run them. This independent branch shares identical isFirmwareFilenameValid with RDKFW-001 (no new transport dependency). Validate actual production filename compatibility before merge. This is lexical filename traversal protection, not an assertion of symlink-safe opens or approved caller authorization.

Guards: processJsonResponse rejects unsafe PCI/PDRI names before response side effects; validateImage no longer accepts a model-containing traversal string; both checkTriggerUpgrade copies reject names before work. CLI Direct CDN artifact join checks the actual filename. Joined-path truncation and legacy PDRI .bin suffix exhaustion return failure.

## Native L1

```sh
make rdkfw_main_gtest rdkfwupdatemgr_main_flow_gtest
./rdkfw_main_gtest --gtest_filter='XconfFilenameSecurityTest.*'
./rdkfwupdatemgr_main_flow_gtest --gtest_filter='RdkFwupdateMgrMainFlowTest.XconfUnsafeFilenameRejectedBeforeUpgrade'
```

Expect two CLI and one manager test; traversal rejected before transfer/concurrency handling. Run all existing model/PDRI, DirectCDN and legacy suites afterward. This branch is not dependent on PR330 or PR332; never substitute their test counts/results.

## MockXConf L2 and on-device

Use disposable trusted firmware directory and harmless outside canary. Start from known eligible model/version response, valid HTTPS base and stub flasher/reboot. Send firmwareFilename ../ABCD_canary.bin, absolute /tmp/ABCD_canary.bin, backslash/encoded/query variants, then valid PCI plus additionalFwVerInfo ../ABCD_PDRI_canary. Exercise CLI legacy, actual daemon path, and per-artifact Direct CDN separately; original production XConf access is unnecessary. Confirm parsed malicious response reached the guard, error returned, no artifact GET/flash or outside canary modification/deletion. Generic model mismatch or unavailable MockXConf is not a pass.

Positive regression: valid signed PCI basename, valid PDRI with/without .bin suffix, actual supported names/lengths. Boundary: 63-byte PDRI field without .bin cannot safely add suffix; it must fail rather than download a truncated name. Retain normal version, no-op and redirect/mTLS behavior. Record exact commit/image, unit XML, fixture JSON, server POST/GET trace, canary before/after, logs and reviewer approval. Scan/security review required before closure.