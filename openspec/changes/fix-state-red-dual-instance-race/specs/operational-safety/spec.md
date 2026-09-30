## MODIFIED Requirements

### Requirement: State Red entry must not perform premature cleanup
The `checkAndEnterStateRed()` function SHALL NOT call `uninitialize()` or delete the PID file (`/tmp/DIFD.pid`). It SHALL only create the state red flag file (`/tmp/stateRedEnabled`), report telemetry/status, and return an error indicator. Process cleanup MUST remain the responsibility of the normal exit path in `main()`.

#### Scenario: State Red entered during download
- **WHEN** `checkAndEnterStateRed()` determines that a fatal TLS/certificate error requires State Red entry
- **THEN** the function SHALL create `/tmp/stateRedEnabled`, update firmware download status, and return -1 without calling `uninitialize()` or deleting `/tmp/DIFD.pid`

#### Scenario: PID file remains valid until process exit
- **WHEN** State Red has been entered and the process is unwinding through normal return path
- **THEN** `/tmp/DIFD.pid` SHALL exist and contain the current process PID until `main()` calls `uninitialize()` at exit

#### Scenario: No second instance started while first is unwinding
- **WHEN** `stateRedRecovery.sh` executes and starts a new `rdkvfwupgrader` instance after State Red flag is set
- **THEN** the new instance SHALL find the existing PID file via `CurrentRunningInst()` and exit with `DWNL_INPROGRESS` status, because the first instance has not yet deleted its PID file
