# RDKFW-011 - RFC Value Copy Bounds

Scope: read_RFCProperty in RFC_API_ENABLED builds. The old quoted-value branch copied with snprintf but placed the NUL at an index derived from the unbounded source length. The fix bounds source parsing, strips matched surrounding quotes, and checks stripped length plus terminator before copying. Fitting values retain their representation; oversized and unterminated values return READ_RFC_FAILURE with empty output rather than silently truncating and reporting success. Invalid argument and non-RFC-build behavior are unchanged.

Status: implementation and test development only. No builds/tests were executed by assistant. RDKFW-011 is Medium and separate from the seven Critical/High findings originally scoped to RDKEMW-24384; confirm the tracking allocation before merge. This is not a claim of security closure.

## Native L1 - User VM

```sh
make rdkfw_interface_gtest
./rdkfw_interface_gtest --gtest_filter='InterfaceTestFixture.RfcValueBounds_*'
./rdkfw_interface_gtest
```

Expected focused selection: five tests. Quoted and unquoted exact-fit values succeed; both oversized forms fail and clear output; an RFC value without a NUL in its fixed source array fails; empty quotes fit a one-byte output. Guard bytes around the destination remain unchanged. Both WDMP_SUCCESS and WDMP_ERR_DEFAULT_VALUE are exercised. Existing NULL tests and full interface regression must still pass. Use supported sanitizers to check out-of-bounds access if available in the VM.

## Controlled L2 / On-Device - User Only

Use the configured Linux component environment, not this Windows workspace. Record current RFC values and restore them after the run. Use a disposable test build or a focused harness calling read_RFCProperty with known destination capacity; do not corrupt a real device's operational RFC values merely to trigger the branch.

1. Provide valid quoted and unquoted values shorter than, equal to, and one byte beyond the caller's payload capacity (reserve one output byte for NUL). Confirm success only when the stripped value fits.
2. Verify a rejected value produces the capacity error and READ_RFC_FAILURE, empty output, no crash, and no guard/sanitizer violation. Callers must follow their existing failure handling rather than consume a partial configuration.
3. Exercise normal getRFCSettings consumers: throttle Enable/TopSpeed, IncrementalCDL, MTLS and Direct CDN. Confirm valid existing configuration still works and restoration succeeds.
4. The actual RFC provider may reject oversized/quoted writes upstream. If so record that gate; it is not proof this reader was exercised. Use mocked RFC results in L1 for the exact sink and attach approved security review evidence for that branch.

Optional original reproduction artifact: sibling rdkfwupdater_harness/rdkfwupdater-develop_Medium_004/xione_gated/poc_rdkfwupdater-007.sh. Read its INSTRUCTIONS.md and safety/cleanup contract before use; it mutates RFC settings and was gated in the July campaign. A skipped trigger or missing marker is not a passing regression result. Avoid FULL_EXEC on production devices.

Attach exact commit/build/image, real RFC provider and caller capacity, L1 XML, sanitizer output if used, L2/device return/log evidence, valid-value regression and restoration results, reviewer decision and approved revalidation reference. Keep NOT RUN, PASS, FAIL and BLOCKED distinct.