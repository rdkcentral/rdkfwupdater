## Why

RDK-E devices expose the authoritative model name through MFR serialized data, while the updater currently obtains model identity through `getDeviceProperties()` and `GetModelNum()`, whose legacy implementation depends on `/etc/device.properties`. The updater's canary reboot path also uses `System.getPowerState`, which is planned for deprecation from SystemServices, instead of the supported PowerManager API.

## What Changes

- Add a buffer-based `GetModelNameUsingMFR()` IARM interface API using `IARM_BUS_MFRLIB_API_GetSerializedData` with `mfrSERIALIZED_TYPE_MODELNAME`.
- After the existing IARM connection is established, replace the model in each long-lived updater `device_info` structure when MFR retrieval succeeds; retain the value loaded by `getDeviceProperties()` when MFR retrieval fails.
- Build XConf request data from the MFR model when available and call the existing `GetModelNum()` only as fallback.
- Migrate the canary firmware power-state request from `org.rdk.System.getPowerState` to `org.rdk.PowerManager.getPowerState` and consume `result.currentState` instead of `result.powerState`.
- Preserve the existing `flashImage()` decision logic, telemetry, report upload, reboot behavior, error handling, and cleanup.
- Add focused unit coverage for successful retrieval, returned-value handling, invalid or failed IARM responses, fallback selection, truncation/null termination, and unchanged non-IARM behavior.
- Add focused power-state coverage for the PowerManager request, `currentState` parsing, and unchanged ON/non-ON decisions.

## Capabilities

### New Capabilities
- `device-model-retrieval`: Defines MFR/IARM-first device-model retrieval, legacy fallback behavior, runtime model propagation, build compatibility, and resource-safety requirements.
- `firmware-power-state-retrieval`: Defines PowerManager-based power-state retrieval for the existing canary firmware reboot decision.

### Modified Capabilities

None.

## Impact

Affected production code is limited to the shared IARM interface (`src/iarmInterface/iarmInterface.c`, `src/include/iarmInterface.h`), XConf request construction (`src/json_process.c`), the two current long-lived updater initialization paths (`src/rdkv_main.c`, `src/rdkFwupdateMgr.c`), and the existing canary reboot power-state request in `src/flash.c`. Focused tests and mocks under `unittest/` will be adjusted only where required to exercise those symbols and behavior.

The implementation depends on the existing `mfrMgr.h` serialized-data contract and existing process-level IARM lifecycle. It does not add a library dependency, change public D-Bus/client APIs, alter `DeviceProperty_t`, or replace `/etc/device.properties` for unrelated device properties. `GetModelNum()` and the model populated by `getDeviceProperties()` remain compatibility fallbacks.
