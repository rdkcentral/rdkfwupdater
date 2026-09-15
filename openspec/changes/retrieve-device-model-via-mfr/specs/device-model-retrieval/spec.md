## ADDED Requirements

### Requirement: Retrieve model through MFR serialized data
When built with IARM support, the updater SHALL provide an internal `GetModelNameUsingMFR()` operation that requests `mfrSERIALIZED_TYPE_MODELNAME` through `IARM_BUS_MFRLIB_API_GetSerializedData` and returns the normalized model in a caller-owned buffer.

#### Scenario: Successful MFR retrieval
- **WHEN** `IARM_Bus_Call()` succeeds with model bytes and a valid positive `bufLen`
- **THEN** the operation returns the model length and writes the exact model value followed by a null terminator

#### Scenario: Correct serialized-data contract
- **WHEN** the updater requests the model from MFR
- **THEN** it passes a zero-initialized `IARM_Bus_MFRLib_GetSerializedData_Param_t` with type `mfrSERIALIZED_TYPE_MODELNAME` to `IARM_BUS_MFRLIB_API_GetSerializedData`

#### Scenario: Trailing line ending
- **WHEN** MFR returns a model ending in carriage-return or newline bytes
- **THEN** the operation removes only those trailing bytes and returns the normalized length

### Requirement: Bound and validate returned model data
The MFR model operation SHALL reject invalid response lengths, SHALL never write beyond the caller's buffer, and SHALL null-terminate every nonempty result.

#### Scenario: Destination buffer is smaller than model
- **WHEN** MFR returns a valid model that does not fit in the destination buffer
- **THEN** the operation copies at most the destination size minus one, null-terminates the result, reports truncation, and returns the copied length

#### Scenario: Invalid MFR response length
- **WHEN** MFR reports zero bytes or a length greater than its serialized-data response buffer
- **THEN** the operation returns zero and leaves a valid destination as an empty string

#### Scenario: Invalid caller buffer
- **WHEN** the destination is null or its size is zero
- **THEN** the operation returns zero without dereferencing the destination

### Requirement: Preserve IARM lifecycle ownership
Model retrieval SHALL use the updater process's existing IARM connection and SHALL NOT independently initialize, connect, disconnect, or terminate IARM.

#### Scenario: Model query during initialized runtime
- **WHEN** model retrieval is invoked after `init_event_handler()`
- **THEN** it performs only the serialized-data API call and leaves IARM connection cleanup to `term_event_handler()`

#### Scenario: IARM API failure
- **WHEN** the serialized-data API returns a result other than `IARM_RESULT_SUCCESS`
- **THEN** model retrieval returns zero, clears a valid destination, and does not hang, crash, allocate memory, or alter IARM lifecycle state

### Requirement: Prefer MFR model in XConf request data
When built with IARM support, `createJsonString()` SHALL attempt MFR model retrieval before `GetModelNum()` and SHALL use the MFR value when retrieval succeeds.

#### Scenario: MFR model is available
- **WHEN** MFR returns `XUSPTC11MWR`
- **THEN** the request data contains `model=XUSPTC11MWR` and `GetModelNum()` is not called

#### Scenario: MFR model matches legacy value
- **WHEN** MFR and legacy sources would both return `XUSHTC11MWR`
- **THEN** the request uses `XUSHTC11MWR` from MFR without consulting the legacy source

### Requirement: Fall back to legacy model retrieval
The updater SHALL preserve existing model retrieval as a compatibility fallback when MFR retrieval is unavailable or unsuccessful.

#### Scenario: XConf MFR retrieval fails
- **WHEN** `GetModelNameUsingMFR()` returns zero during `createJsonString()`
- **THEN** `GetModelNum()` is called and its nonempty value is used for the `model` request field

#### Scenario: Both XConf model sources fail
- **WHEN** both MFR retrieval and `GetModelNum()` return zero
- **THEN** `createJsonString()` omits the `model` field and completes without crashing or hanging

#### Scenario: Runtime MFR retrieval fails
- **WHEN** startup has loaded `device_info.model` through `getDeviceProperties()` and MFR retrieval returns zero
- **THEN** startup preserves the previously loaded model and continues its existing initialization flow

### Requirement: Propagate MFR model to runtime device information
Each long-lived updater initialization path that owns a global `device_info` SHALL replace `device_info.model` with a successfully retrieved MFR model after the existing IARM initialization step.

#### Scenario: Legacy updater startup succeeds
- **WHEN** `src/rdkv_main.c::initialize()` establishes IARM and MFR returns a model
- **THEN** its global `device_info.model` contains that MFR model for subsequent validation and download operations

#### Scenario: D-Bus daemon startup succeeds
- **WHEN** `src/rdkFwupdateMgr.c::initialize()` establishes IARM and MFR returns a model
- **THEN** its global `device_info.model` contains that MFR model for subsequent validation and download operations

### Requirement: Preserve build compatibility
MFR-first model behavior SHALL apply to IARM-enabled builds, including RDK-E configurations, while builds without IARM support SHALL retain the existing direct legacy model path.

#### Scenario: RDK-E IARM-enabled build
- **WHEN** an RDK-E deployment builds with `IARM_ENABLED` and MFR model retrieval succeeds
- **THEN** `/etc/device.properties` is not used as the primary source for model identity

#### Scenario: Non-IARM build
- **WHEN** the updater is compiled without `IARM_ENABLED`
- **THEN** it does not reference the MFR model helper and `createJsonString()` obtains the model through the existing `GetModelNum()` path

#### Scenario: Existing non-model device properties
- **WHEN** updater initialization loads device configuration
- **THEN** all properties other than the model continue to use their existing sources and behavior

### Requirement: Avoid unnecessary interface changes
The change SHALL NOT alter public D-Bus, client SDK, or L2 request/response contracts.

#### Scenario: Existing L2 client interaction
- **WHEN** an existing L2 client invokes updater operations after this change
- **THEN** method signatures, payloads, signals, and response formats remain unchanged
