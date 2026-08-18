## Why

The legacy `rdkfwupdater` (stable2 branch) implements a Direct CDN firmware download mode (RDKE-874) that bypasses Codebig intermediaries and uses per-artifact download URLs from XConf. This capability needs to be adopted into the current refactored repo to maintain feature parity. The feature is RFC-gated and operator-controlled — when disabled, the system behaves identically to today.

## What Changes

- Add RFC parameter `Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.SWDLDirect.Enable` support
- Add `isDirectCDNEnabled()` runtime feature gate
- Branch `GetServURL()` to return `/xconf/firmware/stb/` when Direct CDN is enabled
- Skip Codebig access probe when Direct CDN is enabled
- Extend `XCONFRES` struct with per-artifact URL fields (`firmwareUrl`, `pdriUrl`, `remCtrlUrl`)
- Parse enriched XConf JSON response (per-artifact URLs, dynamic peripheral product key)
- Add PDRI filename validation (must contain `_PDRI_` substring)
- Add `getPeripheralProduct()` for dynamic peripheral JSON key resolution
- Modify `checkTriggerUpgrade()` to support per-artifact mode (`upgrade_type` parameter)
- Add `DirectCDNDownload()` orchestrator with per-artifact selective retry (max 3)
- Modify `upgradeRequest()` signature with `directCdn` parameter
- Wire into both one-shot (`rdkv_main.c`) and daemon (`rdkFwupdateMgr_handlers.c`) paths

## Capabilities

### New Capabilities
- `direct-cdn-download`: RFC-gated firmware download mode that uses per-artifact CDN URLs from XConf, bypasses Codebig, and orchestrates independent PCI/PDRI/Peripheral downloads with selective retry

### Modified Capabilities
- `download-engine`: `upgradeRequest()` gains `directCdn` parameter; Codebig fallback chain conditionally shortened
- `firmware-validation`: `XCONFRES` struct extended; `getXconfRespData()` gains conditional parsing branch; PDRI validation tightened
- `retry-recovery`: New Direct CDN retry policy (per-artifact, max 3, built-in) added as distinct layer
- `updater-execution`: `checkTriggerUpgrade()` modified for per-artifact mode; new `DirectCDNDownload()` orchestrator added to one-shot path

## Impact

- **Files modified**: `src/include/rfcinterface.h`, `src/rfcInterface/rfcinterface.c`, `src/include/json_process.h`, `src/json_process.c`, `src/deviceutils/deviceutils.c`, `src/deviceutils/deviceutils.h`, `src/deviceutils/device_api.c`, `src/device_status_helper.c`, `src/include/rdkv_cdl.h`, `src/rdkv_main.c`, `src/dbus/rdkFwupdateMgr_handlers.c`, `Makefile.am`
- **New files**: `src/directcdn.c` (or inlined into rdkv_main.c)
- **API changes**: `upgradeRequest()` and `checkTriggerUpgrade()` signatures change (backward-compatible via default `directCdn=false`)
- **External dependency**: RFC param must exist in TR-181 data model (already in tr69hostif)
- **Test impact**: L1 mock expectations need `Times()` updates; new L1 + L2 tests required
