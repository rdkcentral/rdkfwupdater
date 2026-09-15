## Context

### Current runtime flow

```text
rdkvfwupgrader initialize() [src/rdkv_main.c]
    -> getDeviceProperties(&device_info)
       -> external rdk_fwdl_utils implementation
       -> populates device_info.model from the legacy device-property source
    -> init_event_handler()
       -> IARM_Bus_Init("RDKVFWEvent")
       -> IARM_Bus_Connect()

rdkFwupdateMgr initialize() [src/rdkFwupdateMgr.c]
    -> getDeviceProperties(&device_info)
    -> init_event_handler()

XConf request construction
    -> createJsonString() [src/json_process.c]
    -> GetModelNum(tmpbuf, sizeof(tmpbuf))
       -> external common_device_api implementation
    -> append model=<value>
```

`DeviceProperty_t` is supplied by external `rdk_fwdl_utils.h`; the in-repository test mirror names its fixed-size field `model`, not `model_name`. `GetModelNum()` is supplied by external `common_device_api.h`. The repository does not own either legacy implementation, so it cannot remove their internal `/etc/device.properties` reads without changing an external component.

The shared IARM implementation already retrieves PDRI serialized data in `GetPDRIFileNameUsingMFR()`. It zeroes `IARM_Bus_MFRLib_GetSerializedData_Param_t`, sets a serialized-data type, calls `IARM_Bus_Call()`, validates `bufLen`, copies into a caller-owned buffer, null-terminates, reports failures, and returns the copied length. IARM process lifecycle is owned by `init_event_handler()` and `term_event_handler()`; per-query helpers do not initialize, connect, disconnect, terminate, allocate, or free IARM buffers.

The historical `origin/topic/RDKEMW-2824` implementation comprised commits `f64f30a` and `877dc50`. It added `GetModelNameUsingMFR()` to the IARM source/header and made `createJsonString()` fall back to `GetModelNum()`. It did not update `device_info.model`, add tests, add RDK-E build conditions, or alter L1/L2 interfaces. Its helper also retained copy-paste PDRI diagnostics and initially checked the wrong variable; those defects are not reusable.

Current architecture has evolved to build both `rdkvfwupgrader` (`src/rdkv_main.c`) and `rdkFwupdateMgr` (`src/rdkFwupdateMgr.c`) against the shared JSON and IARM libraries. Both long-lived initialization paths populate a global `device_info` and establish IARM. The newer D-Bus download worker creates a local `DeviceProperty_t`, but that local model is not used to build XConf device identity and therefore does not require modification for this story.

### Relevant files

| File | Relevant symbols | Role |
| --- | --- | --- |
| `src/iarmInterface/iarmInterface.c` | `GetPDRIFileNameUsingMFR()`, `init_event_handler()`, new `GetModelNameUsingMFR()` | Owns IARM/MFR invocation and process lifecycle pattern. |
| `src/include/iarmInterface.h` | IARM test types, helper prototypes | Declares the new internal helper and test serialized-data constant. |
| `src/json_process.c` | `createJsonString()` | Selects the model placed in XConf request data. |
| `src/rdkv_main.c` | `initialize()`, global `device_info` | Initializes the legacy updater runtime model. |
| `src/rdkFwupdateMgr.c` | `initialize()`, global `device_info` | Initializes the current D-Bus daemon runtime model. |
| `unittest/fwdl_interface_gtest.cpp` | IARM interface tests | Exercises serialized-data success/failure and buffer behavior. |
| `unittest/device_status_helper_gtest.cpp` and mocks | `createJsonString()` test | Exercises MFR-first selection and legacy fallback. |
| `unittest/basic_rdkv_main_gtest.cpp`, `unittest/rdkfwupdatemgr_main_flow_gtest.cpp` and mocks | `initialize()` tests | Exercises runtime `device_info.model` replacement/retention. |

## Goals / Non-Goals

**Goals:**

- Make MFR serialized data the primary source for model identity when IARM support is enabled.
- Propagate a successfully retrieved model into each long-lived updater runtime's `device_info.model`.
- Preserve `GetModelNum()` and the model loaded by `getDeviceProperties()` as failure fallbacks.
- Preserve existing IARM lifecycle ownership and fixed-buffer ownership.
- Keep RDK-E behavior MFR-first without introducing a repository-specific RDK-E macro that does not exist today.
- Add focused L1 unit coverage; use existing L2 execution to detect integration regressions without changing the external L2 interface.

**Non-Goals:**

- Changing `DeviceProperty_t`, external common-utilities implementations, or unrelated `/etc/device.properties` properties such as `DIFW_PATH`.
- Changing D-Bus or client-library interfaces.
- Adding a separate IARM initialization/connection per model query.
- Changing the D-Bus download worker's local `device_info`, which does not control XConf model identity.
- Refactoring the existing PDRI helper or unrelated IARM code.
- Changing power-state behavior. `src/flash.c` already queries `org.rdk.System.getPowerState` for reboot deferral, and no new power-state contract is specified by this story.

## Decisions

### 1. Add `GetModelNameUsingMFR()` beside the PDRI helper

The function SHALL accept a caller-owned character buffer and size and return the resulting model length, matching `GetPDRIFileNameUsingMFR()` and `GetModelNum()`. It SHALL zero `IARM_Bus_MFRLib_GetSerializedData_Param_t`, set `param.type = mfrSERIALIZED_TYPE_MODELNAME`, and invoke:

```text
IARM_Bus_Call(
    IARM_BUS_MFRLIB_NAME,
    IARM_BUS_MFRLIB_API_GetSerializedData,
    &param,
    sizeof(param))
```

The serialized-data contract uses the fixed `param.buffer` and `param.bufLen`; no heap allocation is required or owned by the caller. On success, the helper validates `0 < bufLen <= sizeof(param.buffer)`, copies at most `szBufSize - 1`, explicitly null-terminates, strips trailing CR/LF consistently with the current PDRI helper, and returns the final length. On invalid input, IARM failure, or invalid returned length, it returns zero and clears a valid destination buffer. Truncation is bounded and reported.

Alternative rejected: shelling out to `mfr_util --Modelname`. The user story requests MFR/IARM serialized data, and a process invocation would add quoting, availability, and output-parsing failure modes.

Alternative rejected: allocating `bufLen + 1`. The repository's installed MFR parameter contract embeds a fixed response buffer, and the PDRI implementation already establishes caller-buffer ownership without heap allocation.

### 2. Reuse the process-level IARM lifecycle

Neither model helper nor XConf request construction SHALL call `IARM_Bus_Init()`, `IARM_Bus_Connect()`, `IARM_Bus_Disconnect()`, or `IARM_Bus_Term()`. Startup initialization SHALL call the helper only after `init_event_handler()` has attempted to establish the connection. Existing shutdown remains responsible for disconnect/termination.

Alternative rejected: initializing and terminating IARM inside each helper call. That would conflict with event registration and shared process ownership and could disconnect active users.

### 3. Override runtime model only on success

In both `src/rdkv_main.c::initialize()` and `src/rdkFwupdateMgr.c::initialize()`, call `GetModelNameUsingMFR(device_info.model, sizeof(device_info.model))` immediately after `init_event_handler()` when `IARM_ENABLED` is defined. Because the helper clears its destination on failure, use a temporary model buffer and copy it into `device_info.model` only when the helper returns nonzero. This preserves the model loaded by `getDeviceProperties()` on MFR failure.

Alternative rejected: pass `device_info.model` directly. Clearing the destination on failure would destroy the required fallback value.

### 4. Use MFR first in XConf construction

In `src/json_process.c::createJsonString()`, call `GetModelNameUsingMFR()` first under `IARM_ENABLED`. If it returns zero, call `GetModelNum()` exactly as today. Without `IARM_ENABLED`, call `GetModelNum()` directly. Append `model=` only when either source returns a nonzero length.

This keeps non-IARM builds source-compatible and behaviorally unchanged. For RDK-E configurations with IARM enabled, `/etc/device.properties` is not the primary model source; it is reached only through the compatibility fallback.

### 5. Do not add L1/L2 production interfaces

The new helper is an internal L1 IARM interface declaration in `src/include/iarmInterface.h`; unit-test mocks must expose the same symbol where linked callers require it. No L2 production API consumes or returns model identity, so no D-Bus XML, client SDK, service, or functional-test interface change is justified. Existing `run_l2.sh` coverage may be executed as regression validation if its environment is available.

### 6. Minimum file-level change plan

| File/symbol | Current behavior | Required change | Dependencies and error handling |
| --- | --- | --- | --- |
| `src/iarmInterface/iarmInterface.c::GetModelNameUsingMFR()` | Missing. | Add the bounded fixed-buffer MFR query beside PDRI. | Requires connected IARM; return zero and clear output on failure; no allocation or lifecycle changes. |
| `src/include/iarmInterface.h` | Declares only PDRI MFR helper; test stub lacks model serialized type. | Declare model helper and define `mfrSERIALIZED_TYPE_MODELNAME` only for `GTEST_ENABLE`. | Production value comes from `mfrMgr.h`; avoid redefining it outside tests. |
| `src/json_process.c::createJsonString()` | Always calls `GetModelNum()`. | MFR-first under `IARM_ENABLED`, then existing fallback. | Preserve omission of `model=` if both sources fail. |
| `src/rdkv_main.c::initialize()` | Leaves `device_info.model` from `getDeviceProperties()`. | After IARM init, replace with temporary MFR value on success. | Keep startup successful and legacy model unchanged on query failure. |
| `src/rdkFwupdateMgr.c::initialize()` | Same long-lived model behavior in newer daemon. | Apply the same post-IARM success-only replacement. | Prevent runtime divergence between shipped binaries. |
| Focused unit tests/mocks | No model MFR coverage. | Add only symbols and cases required by helper, JSON selection, and startup propagation. | Mock IARM response buffers and retrieval return lengths; no production API changes. |

## Risks / Trade-offs

- [MFR manager unavailable when startup or XConf data is built] -> Preserve the already-loaded `device_info.model` and call `GetModelNum()` for XConf construction.
- [Serialized data is empty, oversized, or not null-terminated] -> Validate `bufLen`, bound every copy, and explicitly terminate the destination.
- [MFR includes trailing CR/LF like command output] -> Strip only trailing CR/LF and return the normalized length.
- [IARM lifecycle ordering differs at a call site] -> Place startup retrieval after `init_event_handler()` and do not create a second lifecycle owner.
- [Current binaries diverge] -> Apply the same long-lived model override in both current `initialize()` implementations; exclude only the local D-Bus worker structure that does not control model identity.
- [Optional IARM build breaks] -> Compile MFR calls only under `IARM_ENABLED`; preserve direct `GetModelNum()` behavior otherwise.
- [Historical code defects are copied] -> Use the historical branch only as behavioral evidence; correct variable checks and model-specific logs in the new implementation.
- [External common utilities change fallback semantics] -> Treat any nonzero length as existing success and avoid changing those external contracts.

## Migration Plan

No data or API migration is required. Deploy through the normal package build with IARM/MFR headers already used by PDRI retrieval. Rollback consists of reverting the focused source and test changes; the legacy model sources remain intact throughout.

Validation SHALL run the focused IARM interface and JSON/startup unit tests first, then the smallest available build target. Existing L2 tests may run as regression coverage where their platform services are available; no new L2 protocol fixture is required.

## Open Questions

- The story title mentions retrieving device power state, but the detailed requirements specify only model retrieval. The repository already has unrelated power-state logic in `src/flash.c`. Confirmation is required before adding any power-state scope.
