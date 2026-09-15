## Why

RDK-E devices expose the authoritative model name through MFR serialized data, while the updater currently obtains model identity through `getDeviceProperties()` and `GetModelNum()`, whose legacy implementation depends on `/etc/device.properties`. This can report a stale or inappropriate model for RDK-E firmware requests and model-dependent validation.

## What Changes

- Add a buffer-based `GetModelNameUsingMFR()` IARM interface API using `IARM_BUS_MFRLIB_API_GetSerializedData` with `mfrSERIALIZED_TYPE_MODELNAME`.
- After the existing IARM connection is established, replace the model in each long-lived updater `device_info` structure when MFR retrieval succeeds; retain the value loaded by `getDeviceProperties()` when MFR retrieval fails.
- Build XConf request data from the MFR model when available and call the existing `GetModelNum()` only as fallback.
- Add focused unit coverage for successful retrieval, returned-value handling, invalid or failed IARM responses, fallback selection, truncation/null termination, and unchanged non-IARM behavior.
- Do not change power-state handling: the only current power-state flow is the existing reboot-defer JSON-RPC logic in `src/flash.c`, and the story provides no requested power-state behavior or API contract.

## Capabilities

### New Capabilities
- `device-model-retrieval`: Defines MFR/IARM-first device-model retrieval, legacy fallback behavior, runtime model propagation, build compatibility, and resource-safety requirements.

### Modified Capabilities

None.

## Impact

Affected production code is limited to the shared IARM interface (`src/iarmInterface/iarmInterface.c`, `src/include/iarmInterface.h`), XConf request construction (`src/json_process.c`), and the two current long-lived updater initialization paths (`src/rdkv_main.c`, `src/rdkFwupdateMgr.c`). Focused tests and mocks under `unittest/` will be adjusted only where required to exercise those symbols.

The implementation depends on the existing `mfrMgr.h` serialized-data contract and existing process-level IARM lifecycle. It does not add a library dependency, change public D-Bus/client APIs, alter `DeviceProperty_t`, or replace `/etc/device.properties` for unrelated device properties. `GetModelNum()` and the model populated by `getDeviceProperties()` remain compatibility fallbacks.
