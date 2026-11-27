## ADDED Requirements

### Requirement: Retrieve power state through PowerManager
The updater MUST request firmware-update power state through `org.rdk.PowerManager.getPowerState` and MUST NOT call the deprecated `org.rdk.System.getPowerState` method.

#### Scenario: Canary power-state request
- **WHEN** `flashImage()` evaluates power state for a canary firmware update
- **THEN** it sends the request through the existing JSON-RPC helper with method `org.rdk.PowerManager.getPowerState`

### Requirement: Consume the current PowerManager state
The updater MUST obtain the active power state from `result.currentState` and MUST NOT depend on the legacy `result.powerState` field.

#### Scenario: Device is on
- **WHEN** PowerManager returns `result.currentState` equal to `ON`
- **THEN** the updater defers the canary reboot and emits the existing `SYS_INFO_DEFER_CANARY_REBOOT` telemetry event

#### Scenario: Device is not on
- **WHEN** PowerManager returns a valid `result.currentState` other than `ON`
- **THEN** the updater follows the existing non-ON telemetry, report-upload, and conditional reboot behavior

### Requirement: Preserve power-state failure handling
The migration MUST preserve the existing JSON-RPC allocation, transport-failure, parse-failure, missing-state, cleanup, and return behavior.

#### Scenario: PowerManager request fails
- **WHEN** the existing JSON-RPC helper cannot obtain a PowerManager response
- **THEN** the updater logs the failure, releases allocated response resources, and returns through the existing failure path without crashing or hanging

#### Scenario: PowerManager response is invalid
- **WHEN** the response is empty, invalid JSON, or lacks `result.currentState`
- **THEN** the updater does not execute either a state-dependent deferral or reboot decision from invalid data and preserves existing cleanup behavior

### Requirement: Preserve firmware-update behavior and interfaces
The migration MUST NOT change the canary gate, state comparison, telemetry names, report-upload behavior, reboot commands, or public D-Bus and client interfaces.

#### Scenario: Existing firmware workflow
- **WHEN** an existing client initiates a firmware update
- **THEN** externally visible behavior remains unchanged except that power state is obtained from PowerManager