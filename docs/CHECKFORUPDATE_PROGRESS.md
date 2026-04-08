# CheckForUpdate Redesign: Progress & Next Steps

> **Last updated:** 2026-03-17  
> **Reference:** [`DESIGN_CHECKFORUPDATE_ON_DEMAND_THREAD.md`](./DESIGN_CHECKFORUPDATE_ON_DEMAND_THREAD.md)

---

## ✅ Completed

### Design & Documentation
- [x] Design document created: rationale, architecture, edge cases, migration phases, unit test plan
- [x] File-by-file change specification (§11 in design doc)
- [x] Multi-client scenario walkthrough (§6)
- [x] Thread safety proof (§8)
- [x] Resource cost comparison (§13)
- [x] Inline code documentation added to all modified source files (TL;DR comments)

### Implementation (Phase 1)
- [x] `rdkFwupdateMgr_async_internal.h` — Added `CheckRequestContext` struct, worker thread declarations, session-state query API
- [x] `rdkFwupdateMgr_async_internal.h` — Removed legacy `CallbackEntry`, `CallbackRegistry`, `CallbackEntryState`, `CALLBACK_TIMEOUT_SECONDS`
- [x] `rdkFwupdateMgr_async.c` — Implemented `internal_check_worker_thread()` (on-demand worker)
- [x] `rdkFwupdateMgr_async.c` — Implemented `on_check_signal_handler()` (fires callback directly)
- [x] `rdkFwupdateMgr_async.c` — Implemented `on_check_timeout()` (120s safety net)
- [x] `rdkFwupdateMgr_async.c` — Implemented `internal_is_check_in_progress()` (session-state query)
- [x] `rdkFwupdateMgr_async.c` — Implemented `internal_cancel_all_active_check_threads()` (destructor cleanup)
- [x] `rdkFwupdateMgr_async.c` — Removed legacy `g_registry`, `on_check_complete_signal()`, `dispatch_all_pending()`, `internal_register_callback()`
- [x] `rdkFwupdateMgr_async.c` — Removed `CheckForUpdateComplete` subscription from background thread
- [x] `rdkFwupdateMgr_async.c` — Background thread now unsubscribes Download/Update signals on exit
- [x] `rdkFwupdateMgr_api.c` — Rewrote `checkForUpdate()` to use on-demand worker thread model
- [x] `rdkFwupdateMgr_api.c` — Updated library destructor to cancel/join active worker before BG thread cleanup
- [x] `rdkFwupdateMgr_process.c` — Added session-state guard in `unregisterProcess()` (rejects if check in progress)
- [x] All modified files compile cleanly (zero errors)

### Verification
- [x] `example_app.c` verified — works with new API, no changes needed
- [x] Public API (`rdkFwupdateMgr_client.h`) unchanged — zero ABI breakage
- [x] Download/Update code paths unchanged and unaffected

---

## 🔄 In Progress

### Device Testing
- [ ] **Cross-compile for target device** — verify build succeeds on device toolchain
- [ ] **Runtime smoke test** — `registerProcess()` → `checkForUpdate()` → callback fires → `unregisterProcess()`
- [ ] **Session-state guard test** — call `unregisterProcess()` during active check, verify rejection log
- [ ] **Timeout test** — stop daemon, call `checkForUpdate()`, verify 120s timeout and clean exit
- [ ] **Library unload test** — `dlclose()` during active check, verify destructor joins worker

---

## ⏳ Pending (Next Steps)

### Unit Tests (Priority: HIGH)
| # | Test | File | Status |
|---|------|------|--------|
| 1 | `WorkerThread_StartsAndStops` | new gtest file | ⬜ |
| 2 | `WorkerThread_FiresCallback` | new gtest file | ⬜ |
| 3 | `WorkerThread_Timeout` | new gtest file | ⬜ |
| 4 | `WorkerThread_DBusFailure` | new gtest file | ⬜ |
| 5 | `DuplicateRequest_Rejected` | new gtest file | ⬜ |
| 6 | `UnregisterDuringCheck_Rejected` | new gtest file | ⬜ |
| 7 | `UnregisterAfterCallback_Succeeds` | new gtest file | ⬜ |
| 8 | `LibraryUnloadDuringCheck` | new gtest file | ⬜ |
| 9 | `CallbackDataValidity` | new gtest file | ⬜ |
| 10 | `MultiProcess_BothReceiveSignal` | integration test | ⬜ |
| 11 | `SIGTERM_DuringCheck_ExitClean` | new gtest file | ⬜ |

### Legacy Tests to Rewrite
| # | File | Reason |
|---|------|--------|
| 1 | `rdkFwupdateMgr_async_cleanup_gtest.cpp` | References old registry init/cleanup |
| 2 | `rdkFwupdateMgr_async_refcount_gtest.cpp` | Tests old registry slot refcounting |
| 3 | `rdkFwupdateMgr_async_signal_gtest.cpp` | Tests old signal dispatch through registry |
| 4 | `rdkFwupdateMgr_async_stress_gtest.cpp` | Uses old `g_async_registry`, concurrent registration |
| 5 | `rdkFwupdateMgr_async_threadsafety_gtest.cpp` | Tests old concurrent registration/dispatch |

### Integration Testing
- [ ] Multi-process scenario: two separate apps call `checkForUpdate()`, both receive callback
- [ ] Daemon restart during active check: verify 120s timeout fires, clean exit
- [ ] Rapid register/check/unregister cycles: no leaks, no crashes

---

## 🔮 Future Phases

### Phase 1.5: `cancelCheckForUpdate()` API
- Add ability to tear down an active worker thread mid-flight
- Enables clean `SIGTERM → cancel → unregister → exit` flow
- Estimated effort: ~4 hours

### Phase 2: Migrate Download to On-Demand Thread
- Same pattern as CheckForUpdate but with multi-fire callback
- Worker stays alive across multiple `DownloadProgress` signals
- Estimated effort: ~8 hours

### Phase 3: Migrate Update to On-Demand Thread
- Same as Phase 2 but for `UpdateProgress`
- Estimated effort: ~6 hours

### Phase 4: Remove Persistent Background Thread
- Remove `internal_system_init()` / `internal_system_deinit()`
- Remove `BackgroundThread` struct
- Library constructor becomes a true no-op
- Zero resource cost when library is loaded but no API calls made
- Estimated effort: ~4 hours

### API Improvements
- Change `unregisterProcess()` return type from `void` to `UnregisterResult` enum
- Add error codes for session-state violations (currently log-only)
- Add configurable timeout (env var or RFC parameter)

---

## 📁 Modified Files Summary

| File | Changes |
|------|---------|
| `librdkFwupdateMgr/src/rdkFwupdateMgr_async_internal.h` | Added `CheckRequestContext`, worker declarations, session-state API. Removed legacy registry types. |
| `librdkFwupdateMgr/src/rdkFwupdateMgr_async.c` | On-demand worker engine, signal/timeout handlers, cancel/query APIs. Removed old registry + dispatch code. |
| `librdkFwupdateMgr/src/rdkFwupdateMgr_api.c` | Rewrote `checkForUpdate()`, updated destructor. |
| `librdkFwupdateMgr/src/rdkFwupdateMgr_process.c` | Session-state guard in `unregisterProcess()`. |
| `librdkFwupdateMgr/include/rdkFwupdateMgr_client.h` | **NO CHANGES** (public API unchanged) |
| `librdkFwupdateMgr/examples/example_app.c` | **NO CHANGES** (works as-is) |
| `docs/DESIGN_CHECKFORUPDATE_ON_DEMAND_THREAD.md` | Full design document |
| `docs/CHECKFORUPDATE_PROGRESS.md` | This file |
