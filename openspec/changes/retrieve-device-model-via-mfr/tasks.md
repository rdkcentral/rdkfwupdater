## 1. MFR Model Interface

- [ ] 1.1 Add the `GetModelNameUsingMFR()` prototype and the `GTEST_ENABLE` model serialized-data constant to `src/include/iarmInterface.h` without changing production MFR definitions.
- [ ] 1.2 Implement `GetModelNameUsingMFR()` beside `GetPDRIFileNameUsingMFR()` using the existing IARM connection, `mfrSERIALIZED_TYPE_MODELNAME`, validated fixed-buffer copying, explicit null termination, CR/LF normalization, model-specific diagnostics, and zero-length failure signaling.
- [ ] 1.3 Extend the existing IARM interface mock/tests to verify API owner/id/type/parameter size, exact successful model output, invalid arguments and lengths, bounded truncation, line-ending normalization, IARM failure, and absence of lifecycle/allocation side effects.
- [ ] 1.4 Run the focused `rdkfw_interface_gtest` target and repair only failures caused by the new helper.

## 2. XConf Model Selection

- [ ] 2.1 Update `src/json_process.c::createJsonString()` to query MFR first under `IARM_ENABLED`, call `GetModelNum()` only when MFR returns zero, and preserve direct `GetModelNum()` behavior without IARM.
- [ ] 2.2 Extend only the required `device_status_helper` test mocks and cases to verify MFR success suppresses legacy lookup, the exact MFR model is emitted, matching legacy values still use MFR first, MFR failure uses `GetModelNum()`, and dual failure omits the model without crashing or hanging.
- [ ] 2.3 Run the focused JSON/device-status unit-test target and repair only failures caused by model-source selection.

## 3. Runtime Device Information

- [ ] 3.1 Update `src/rdkv_main.c::initialize()` after `init_event_handler()` to retrieve into a temporary buffer and replace global `device_info.model` only on MFR success.
- [ ] 3.2 Update `src/rdkFwupdateMgr.c::initialize()` with the same success-only post-IARM model replacement so both current binaries use consistent runtime identity.
- [ ] 3.3 Extend only the required main-flow mocks/tests to verify each initializer installs a successful MFR value and preserves the `getDeviceProperties()` model on MFR failure.
- [ ] 3.4 Run the focused `rdkfw_main_gtest` and `rdkfwupdatemgr_main_flow_gtest` targets and repair only failures caused by runtime propagation.

## 4. Compatibility And Verification

- [ ] 4.1 Compile the affected targets with IARM enabled and compile-check the non-IARM path to confirm no MFR helper reference escapes its build guard and no header/prototype dependency is missing.
- [ ] 4.2 Run the smallest available updater unit-test/build command and, where the platform environment is available, run existing L2 regression coverage without modifying its public interface or fixtures solely for model retrieval.
- [ ] 4.3 Inspect the final diff for unrelated changes, direct model-property reads, unsafe buffer paths, accidental IARM lifecycle calls, missing fallback branches, and modifications to power-state, D-Bus, client SDK, or unrelated device-property behavior.
