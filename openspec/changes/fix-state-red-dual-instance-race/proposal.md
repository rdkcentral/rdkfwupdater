## Why

`checkAndEnterStateRed()` calls `uninitialize()` which deletes `/tmp/DIFD.pid` and destroys mutexes, but only returns -1 instead of exiting the process. The caller proceeds into `retryDownload()` keeping the process alive for minutes without a PID file, while `stateRedRecovery.sh` sees `/tmp/stateRedEnabled` and starts a second instance that passes `CurrentRunningInst()` (since the PID file is gone). This causes dual-instance corruption and unpredictable firmware update behavior. The bug exists in develop, stable2 (production), and topic/RDKEMW-9150 branches.

## What Changes

- After `checkAndEnterStateRed()` triggers state red entry, the process must NOT continue into retry/fallback logic — it must propagate the error immediately so the process exits cleanly
- `checkAndEnterStateRed()` will no longer call `uninitialize()` directly; cleanup responsibility stays with the normal exit path in `main()`
- A state-red-entered signal (return value) will be propagated from `dwnlError()` → `downloadFile()` → `rdkv_upgrade_request()` to short-circuit `retryDownload()`

## Capabilities

### New Capabilities

### Modified Capabilities
- `operational-safety`: State Red entry path must not delete PID file or destroy mutexes prematurely; process must exit cleanly through the normal uninitialize path
- `retry-recovery`: Retry/fallback logic must be skipped when State Red has been entered; error propagation from `checkAndEnterStateRed()` must short-circuit all retry layers

## Impact

- **Code**: `src/device_status_helper.c` (`checkAndEnterStateRed()`), `src/rdkv_upgrade.c` (`dwnlError()`, `downloadFile()`, `rdkv_upgrade_request()`)
- **Behavior**: After state red entry, process exits immediately instead of lingering for retry_count * 60 seconds
- **Risk**: Low — state red is already a terminal failure; skipping retries after it is the correct semantic
- **Testing**: Unit tests should verify that retryDownload is not called after state red entry
