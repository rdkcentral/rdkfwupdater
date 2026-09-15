## Context

When `rdkvfwupgrader` encounters a fatal TLS/certificate error during firmware download, it enters "State Red" — a recovery mode where the device reboots and retries with special parameters. The current implementation of `checkAndEnterStateRed()` in `src/device_status_helper.c` calls `uninitialize()` (which deletes `/tmp/DIFD.pid` and destroys mutexes) but then only returns -1 to the caller instead of exiting the process.

The caller chain (`dwnlError()` → `downloadFile()` → `rdkv_upgrade_request()`) does not propagate the "state red entered" condition, so `rdkv_upgrade_request()` proceeds into `retryDownload()` — keeping the process alive for `RETRY_COUNT * 60` seconds without a PID file. Meanwhile, `stateRedRecovery.sh` sees `/tmp/stateRedEnabled` and starts a second instance which passes `CurrentRunningInst()` since the PID file is gone.

There are 3 call sites of `checkAndEnterStateRed()`, all in `src/rdkv_upgrade.c`:
- **Site 1** (line ~199): inside `dwnlError()`, HTTP 495 path
- **Site 2** (line ~204): inside `dwnlError()`, curl error path
- **Site 3** (line ~1052): inside `downloadFile()`, `MTLS_CERT_FETCH_FAILURE` path

Site 3 already returns immediately after calling `checkAndEnterStateRed()`, but it returns `curl_ret_code` (-1) which is not a recognized short-circuit code — so the caller (`rdkv_upgrade_request()`) still enters `retryDownload()`. All 3 sites exhibit the same bug.

## Goals / Non-Goals

**Goals:**
- Prevent dual-instance race condition after State Red entry
- Ensure process exits cleanly through normal `main()` → `uninitialize()` → `exit()` path
- Skip all retry/fallback logic once State Red has been entered
- Minimal change surface — follow existing short-circuit patterns already in the codebase

**Non-Goals:**
- Refactoring the overall retry architecture
- Changing State Red recovery shell script behavior
- Modifying the PID file mechanism itself
- Adding new error reporting or telemetry beyond what exists

## Decisions

### Decision 1: Keep `uninitialize()` in `checkAndEnterStateRed()` — no change

**Choice:** Do NOT remove `uninitialize()` from `checkAndEnterStateRed()`. The function continues to call `uninitialize(INITIAL_VALIDATION_SUCCESS)` as it does today. Instead, we guard `main()` against calling `uninitialize()` a second time.

**Rationale:** `checkAndEnterStateRed()` has always performed cleanup before creating the state-red flag. This is intentional — it tears down IARM connections, deletes the PID file, and frees resources to prepare the process for a clean exit. Removing it would change established behavior. The simpler fix is to ensure the caller (main) doesn't double-call uninitialize.

**Alternative considered:** Remove `uninitialize()` from `checkAndEnterStateRed()` and let `main()` handle it. Rejected because it changes the semantics of state-red entry — the PID file must be removed BEFORE the process continues (to allow the recovery script to start a fresh instance), and `uninitialize()` is the function that does this.

### Decision 2: Introduce `RDKV_UPGRADE_ERROR_STATE_RED` return code

**Choice:** Define a new error constant (e.g., value -2 or a dedicated enum value) to signal that State Red was entered, following the existing pattern of `RDKV_UPGRADE_ERROR_FORCE_EXIT` and `RDKV_UPGRADE_ERROR_THROTTLE_ZERO`.

**Rationale:** The codebase already has this pattern — `rdkv_upgrade_request()` checks for `RDKV_UPGRADE_ERROR_FORCE_EXIT` and returns immediately before calling `retryDownload()`. Adding a similar check for `RDKV_UPGRADE_ERROR_STATE_RED` is a minimal, consistent change.

### Decision 3: Propagate state-red status from `downloadFile()` after `dwnlError()` (Sites 1 & 2)

**Choice:** After `dwnlError()` is called in the download loop within `downloadFile()`, check if the state-red flag file (`/tmp/stateRedEnabled`) was created. If so, return `RDKV_UPGRADE_ERROR_STATE_RED` instead of continuing the loop.

**Rationale:** `dwnlError()` is a void function and changing its signature would be a larger refactor. The state-red flag file is the authoritative indicator that `checkAndEnterStateRed()` was triggered, and `filePresentCheck(STATEREDFLAG)` is already used elsewhere. This keeps `dwnlError()` unchanged.

### Decision 4: Change Site 3 return value to `RDKV_UPGRADE_ERROR_STATE_RED`

**Choice:** In the `MTLS_CERT_FETCH_FAILURE` path (line ~1052), change `return curl_ret_code` to `return RDKV_UPGRADE_ERROR_STATE_RED`.

**Rationale:** Site 3 currently returns `-1` (the initial value of `curl_ret_code`) which is not a recognized short-circuit code. The caller (`rdkv_upgrade_request()`) sees `-1 != CURL_SUCCESS` and enters `retryDownload()` — the same bug as Sites 1 & 2. Returning the new error code ensures the caller short-circuits consistently across all 3 sites.

### Decision 5: Short-circuit retryDownload in `rdkv_upgrade_request()`

**Choice:** Add a check for `RDKV_UPGRADE_ERROR_STATE_RED` alongside the existing `RDKV_UPGRADE_ERROR_FORCE_EXIT` and `RDKV_UPGRADE_ERROR_THROTTLE_ZERO` checks, before the `retryDownload()` call. This guard must be added in both the direct and codebig paths.

**Rationale:** Follows the exact existing pattern. No new control flow structures needed.

### Decision 6: Guard `main()` against double-uninitialize

**Choice:** In `main()`, before the final `uninitialize()` call, check if the return code is `RDKV_UPGRADE_ERROR_STATE_RED`. If so, skip `uninitialize()` since `checkAndEnterStateRed()` already called it.

**Rationale:** `checkAndEnterStateRed()` calls `uninitialize()` which destroys mutexes, tears down IARM, and deletes the PID file. Calling `uninitialize()` again in `main()` causes undefined behavior (pthread_mutex_destroy on destroyed mutex) and IARM_ASSERT failures seen in production logs. The guard prevents the double-call.

## Risks / Trade-offs

- **[Risk] Double-uninitialize in edge cases** → Mitigation: The guard in `main()` checks for `RDKV_UPGRADE_ERROR_STATE_RED` specifically. If a future code path reaches `main()` with this error code without having called uninitialize first, it would skip cleanup. This is acceptable because `RDKV_UPGRADE_ERROR_STATE_RED` is only ever set by paths that went through `checkAndEnterStateRed()`.

- **[Risk] Process continues executing briefly after `uninitialize()` is called inside `checkAndEnterStateRed()`** → Mitigation: The new return code propagates up quickly (no sleeps or loops between state-red detection and `main()` exit). The window is milliseconds, not the previous minutes-long retry loop.

- **[Risk] `codebigdownloadFile()` may have the same pattern** → Mitigation: The fix covers both the direct and codebig paths in `rdkv_upgrade_request()` since both share the same short-circuit check point before `retryDownload()`.

- **[Trade-off] Using file-system check in `downloadFile()` after `dwnlError()` instead of propagating return value through `dwnlError()`** → Acceptable because it avoids changing a void function's signature which is called in multiple places, and the flag file is the canonical state indicator.

- **[Trade-off] Site 3 (MTLS path) return value changes from `-1` to `RDKV_UPGRADE_ERROR_STATE_RED`** → This changes what value eventually reaches `main()` as `ret_curl_code`. Since `main()` uses this as the `exit()` code, the exit code will differ. This is acceptable because the process is about to reboot in state-red recovery regardless of exit code.
