## Why

The authoritative `RDKFWUPDATER (2)` worksheet in `Device OS Owned Components Mythos Assessment_template.xlsx` lists 18 findings: 2 Critical, 5 High, 10 Medium, and 1 Low. This single user story, RDKEMW-24384, addresses only the seven Critical/High findings, removing their unsafe execution paths while preserving firmware-update functionality.

The user targets developer-tested, reviewed build handover to DevQA on November 23, 2026, using rolling PRs during a 15-weekday development window. This is a constrained delivery target, not evidence that unfinished or unvalidated findings can be closed.

## What Changes

- RDKFW-001: constrain D-Bus DownloadFirmware filenames and destination paths before cache checks, file creation, or cleanup.
- RDKFW-002: authenticate the state used for Canary/post-flash reboot decisions instead of trusting `/tmp/xconfchecknow_val`; retain in scope but pause coding until producer and behavior decisions are resolved.
- RDKFW-003: validate XConf PCI/PDRI artifact filenames in legacy and Direct CDN paths; use controlled MockXConf/L2 validation without production XConf access.
- RDKFW-004: bound peripheral-version accumulation in both executables. Code is already committed at `a19652a`; native testing and reviewer approval remain pending.
- RDKFW-005: make XConf request construction capacity-safe, including delimiter writes and caller failure propagation, preserving the existing form-style POST payload.
- RDKFW-006: authorize firmware-mutating D-Bus operations, bind handler IDs to their actual bus sender, and constrain caller-selected flash paths. Sender binding alone is not authorization for every locally registered caller.
- RDKFW-007: reject cleartext firmware download URLs and prevent HTTPS-to-HTTP redirect downgrade at the actual transport boundary.
- **BREAKING (intentional security restriction):** reject formerly accepted unsafe paths, unauthorized firmware operations, and cleartext firmware downloads. Preserve public signatures and legitimate clients where possible.
- Produce repeatable L1/L2/on-device test instructions and evidence templates before each PR. The user runs builds/tests in their VM and lab; the assistant does not execute builds/tests unless explicitly requested.

## Capabilities

### New Capabilities

None. Security requirements belong to the existing owning capabilities.

### Modified Capabilities

- `dbus-ipc`: authenticated firmware-operation callers, sender-bound registrations, and constrained download/flash paths.
- `firmware-validation`: safe artifact filenames and bounded, format-preserving XConf request construction.
- `updater-execution`: capacity-safe peripheral-version recording in both execution models.
- `operational-safety`: trusted Canary state and HTTPS-only firmware download behavior, including transport downgrade prevention.

## Impact

Production ownership includes `src/rdkv_main.c`, `src/rdkFwupdateMgr.c`, `src/include/rdkv_cdl.h`, `src/json_process.c`, `src/directcdn.c`, `src/dbus/rdkv_dbus_server.c`, `src/dbus/rdkFwupdateMgr_handlers.c`, `src/flash.c`, and applicable download paths in `src/rdkv_upgrade.c`. Exact additional helper/header modifications will be selected from local evidence during implementation, not assumed completed.

Reuse existing GoogleTest fixtures and `test/functional-tests/tests/` MockXConf/D-Bus flows. Curl file opening and redirect controls may belong to external `common_utilities`/`dwnlutil` dependencies; investigate that boundary before claiming remediation. Latest product builds ship the daemon and client library, per user confirmation; July harness absence is not a current-build exclusion.

All work stays on `topic/RDKEMW-24384`, never directly on `develop`. Medium/Low findings, unrelated refactoring, and external Mythos retesting are outside developer scope. Security closure still requires approved revalidation evidence. Companion delivery, test, and checkpoint documents record constraints and remaining work for future sessions.