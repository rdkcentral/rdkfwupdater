## 1. MFR Model Interface

- [x] 1.1 Add the `GetModelNameUsingMFR()` prototype and the `GTEST_ENABLE` model serialized-data constant to `src/include/iarmInterface.h` without changing production MFR definitions.
- [x] 1.2 Implement `GetModelNameUsingMFR()` beside `GetPDRIFileNameUsingMFR()` using the existing IARM connection, `mfrSERIALIZED_TYPE_MODELNAME`, validated fixed-buffer copying, explicit null termination, CR/LF normalization, model-specific diagnostics, and zero-length failure signaling; insufficient destination capacity must clear the output and return zero rather than expose a truncated model.
- [x] 1.3 Extend the existing IARM interface mock/tests to verify API owner/id/type/parameter size, exact successful model output, invalid arguments and lengths, truncation returns zero with an empty destination, line-ending normalization, IARM failure, and absence of lifecycle/allocation side effects.
- [ ] 1.4 Run the focused `rdkfw_interface_gtest` target and repair only failures caused by the new helper.

## 2. XConf Model Selection

- [x] 2.1 Update `src/json_process.c::createJsonString()` to query MFR first under `IARM_ENABLED`, call `GetModelNum()` only when MFR returns zero, and preserve direct `GetModelNum()` behavior without IARM.
- [x] 2.2 Extend only the required `device_status_helper` test mocks and cases to verify MFR success suppresses legacy lookup, the exact MFR model is emitted, matching legacy values still use MFR first, MFR failure including truncation uses `GetModelNum()`, and dual failure omits the model without crashing or hanging.
- [ ] 2.3 Run the focused JSON/device-status unit-test target and repair only failures caused by model-source selection.

## 3. Runtime Device Information

- [x] 3.1 Update `src/rdkv_main.c::initialize()` after `init_event_handler()` to retrieve into a temporary buffer and replace global `device_info.model` only on MFR success.
- [x] 3.2 Update `src/rdkFwupdateMgr.c::initialize()` with the same success-only post-IARM model replacement so both current binaries use consistent runtime identity.
- [x] 3.3 Extend only the required main-flow mocks/tests to verify each initializer installs a complete successful MFR value and preserves the `getDeviceProperties()` model on MFR failure, including insufficient destination capacity.
- [ ] 3.4 Run the focused `rdkfw_main_gtest` and `rdkfwupdatemgr_main_flow_gtest` targets and repair only failures caused by runtime propagation.

## 4. PowerManager Migration

- [x] 4.1 Update `src/flash.c::flashImage()` to request `org.rdk.PowerManager.getPowerState` and consume `result.currentState`, preserving the existing canary gate, ON-state deferral, non-ON behavior, errors, and cleanup.
- [x] 4.2 Add focused L1 coverage that verifies the PowerManager request and `currentState` parsing for ON, non-ON, RPC-failure, and invalid-response cases without changing reboot policy.
- [ ] 4.3 Add or update L2 coverage, where the platform harness supports power-state control, to verify canary ON-state deferral and non-ON behavior through the existing external interface.
- [ ] 4.4 Run the focused flash/main tests and repair only failures caused by the power-state API migration.

## 5. Compatibility And Verification

- [ ] 5.1 Compile the affected targets with IARM enabled and compile-check the non-IARM path to confirm no MFR helper reference escapes its build guard and no header/prototype dependency is missing.
- [ ] 5.2 Run the smallest available updater unit-test/build command and relevant existing L2 regression coverage without changing public interfaces.
- [x] 5.3 Inspect the final diff for unrelated changes, direct model-property reads, unsafe buffer paths, accidental IARM lifecycle calls, missing fallback branches, and changes to D-Bus, client SDK, unrelated device-property behavior, or canary reboot policy.
