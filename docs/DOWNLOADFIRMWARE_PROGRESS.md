# DownloadFirmware Redesign: Progress & Next Steps

> **Last updated:** 2026-03-25  
> **Reference:** [`DESIGN_DOWNLOAD_FIRMWARE_ON_DEMAND_THREAD.md`](./DESIGN_DOWNLOAD_FIRMWARE_ON_DEMAND_THREAD.md)

---

## ✅ Completed

### Design & Documentation
- [x] Design document created: rationale, architecture, edge cases, migration phases, unit test plan
- [x] File-by-file change specification (§12 in design doc)
- [x] Multi-client scenario walkthrough (§6) — reject, piggyback, same-process duplicate
- [x] Daemon download handler deep dive (§7) — decision tree, signal emission, piggyback logic
- [x] Thread lifecycle & memory ownership diagram (§8) — ownership wall, free-point audit
- [x] Thread safety proof (§9) — shared mutable state inventory, condvar correctness
- [x] Edge cases & robustness analysis (§10) — 11 edge cases covered
- [x] Dead code removal plan (§11) — what to remove, what to keep
- [x] Resource cost comparison (§14) — zero cost when idle
- [x] Inline code documentation added to all modified source files (TL;DR comments)

### Implementation (Phase 2)
- [x] `rdkFwupdateMgr_async_internal.h` — Added `DownloadRequestContext` struct, `InternalDwnlSignalData`, worker thread declarations, session-state query API
- [x] `rdkFwupdateMgr_async_internal.h` — Added `DBUS_METHOD_DOWNLOAD`, `DBUS_SIGNAL_DWNL_PROGRESS`, `DWNL_SIGNAL_TIMEOUT_SECONDS` constants
- [x] `rdkFwupdateMgr_async_internal.h` — Removed legacy `DwnlCallbackState`, `DwnlCallbackEntry`, `DwnlCallbackRegistry`
- [x] `rdkFwupdateMgr_async_internal.h` — Removed legacy `internal_dwnl_register_callback()`, `internal_dwnl_system_deinit()` declarations
- [x] `rdkFwupdateMgr_async.c` — Implemented `internal_download_worker_thread()` (on-demand worker with synchronous D-Bus call)
- [x] `rdkFwupdateMgr_async.c` — Implemented `on_download_signal_handler()` (multi-fire callback, quits only on terminal status)
- [x] `rdkFwupdateMgr_async.c` — Implemented `on_download_timeout()` (3600s safety net, fires `DWNL_ERROR` callback)
- [x] `rdkFwupdateMgr_async.c` — Implemented `map_dwnl_status_string()` (maps daemon status strings to `DownloadStatus` enum)
- [x] `rdkFwupdateMgr_async.c` — Implemented `internal_parse_dwnl_signal_data()` (parses `(tsuss)` GVariant)
- [x] `rdkFwupdateMgr_async.c` — Implemented `internal_map_dwnl_status_code()` (maps integer status to enum)
- [x] `rdkFwupdateMgr_async.c` — Implemented `internal_is_dwnl_in_progress()` (session-state query)
- [x] `rdkFwupdateMgr_async.c` — Implemented `internal_begin_download()` / `internal_end_download()` / `internal_abort_download()` (encapsulated state accessors)
- [x] `rdkFwupdateMgr_async.c` — Implemented `internal_cancel_all_active_download_threads()` (destructor cleanup)
- [x] `rdkFwupdateMgr_async.c` — Removed legacy `g_dwnl_registry`, `on_download_progress_signal()`, `dispatch_all_dwnl_active()`, `internal_dwnl_register_callback()`, `dwnl_registry_reset_slot()`, `internal_dwnl_system_deinit()`
- [x] `rdkFwupdateMgr_async.c` — Removed `DownloadProgress` subscription from background thread (BG thread now handles `UpdateProgress` only)
- [x] `rdkFwupdateMgr_api.c` — Rewrote `downloadFirmware()` to use on-demand worker thread model with synchronous daemon reply
- [x] `rdkFwupdateMgr_api.c` — Updated library destructor to cancel/join active download worker before BG thread cleanup
- [x] `rdkFwupdateMgr_process.c` — Added session-state guard in `unregisterProcess()` (rejects if download in progress)
- [x] All state encapsulated: `g_dwnl_in_progress`, `g_active_dwnl_ctx` are `static` in `_async.c`, accessed only through accessor functions
- [x] All modified files compile cleanly (zero errors)

### Key Design Decisions Implemented
- [x] **Synchronous D-Bus call** (`g_dbus_connection_call_sync`) — daemon reply (accept/reject) accurately reported to caller
- [x] **One download at a time per process** — `g_dwnl_in_progress` flag prevents duplicate worker threads
- [x] **Multi-fire callback** — callback invoked on every `DownloadProgress` signal, loop quits only on `COMPLETED`/`ERROR`
- [x] **No handler_id filtering** — daemon's accept/reject gates entry; all accepted clients receive all broadcast signals
- [x] **Thread is joinable** (NOT detached) — destructor can join it during library unload
- [x] **Timeout fires error callback** — `on_download_timeout()` calls `callback(0, DWNL_ERROR)` so client knows

### Verification
- [x] Public API (`rdkFwupdateMgr_client.h`) unchanged — zero ABI breakage
- [x] CheckForUpdate code paths unchanged and unaffected
- [x] Update (Phase 3) code paths unchanged and unaffected
- [x] Background thread still alive for `UpdateProgress` only (Phase 3 removes it)

---

## 🔄 In Progress

### Device Testing
- [ ] **Cross-compile for target device** — verify build succeeds on device toolchain
- [ ] **Runtime smoke test** — `registerProcess()` → `checkForUpdate()` → `downloadFirmware()` → callbacks fire → `unregisterProcess()`
- [ ] **Session-state guard test** — call `unregisterProcess()` during active download, verify rejection log
- [ ] **Timeout test** — stop daemon mid-download, verify 3600s timeout fires `DWNL_ERROR` callback and clean exit
- [ ] **Library unload test** — `dlclose()` during active download, verify destructor joins worker
- [ ] **Daemon reject test** — start download on process A, attempt download on process B, verify B gets `RDKFW_DWNL_FAILED`
- [ ] **Piggyback test** — start download of same firmware from two processes, verify both receive progress

---

## ⏳ Pending (Next Steps)

### Unit Tests (Priority: HIGH)
| # | Test | Description | Status |
|---|------|-------------|--------|
| 1 | `DownloadWorker_StartsAndExits` | Worker thread created, exits after COMPLETED signal | ⬜ |
| 2 | `DownloadWorker_FiresMultipleCallbacks` | Callback invoked for each progress signal (25%, 50%, 100%) | ⬜ |
| 3 | `DownloadWorker_FiresErrorCallback` | Callback invoked with `DWNL_ERROR` on error signal | ⬜ |
| 4 | `DownloadWorker_Timeout` | Thread exits after 3600s, fires `DWNL_ERROR` callback | ⬜ |
| 5 | `DownloadWorker_DaemonReject` | `RDKFW_DWNL_FAILED` returned when daemon rejects | ⬜ |
| 6 | `DownloadWorker_DaemonPiggyback` | Worker enters signal loop on piggyback, receives progress | ⬜ |
| 7 | `DownloadWorker_CachedFirmware` | Worker receives immediate COMPLETED, exits fast | ⬜ |
| 8 | `DownloadDuplicate_Rejected` | Second `downloadFirmware()` returns FAILED while first active | ⬜ |
| 9 | `UnregisterDuringDownload_Rejected` | `unregisterProcess()` rejected while download active | ⬜ |
| 10 | `LibraryUnloadDuringDownload` | Destructor joins active worker thread | ⬜ |
| 11 | `DownloadWorker_DBusFailure` | `RDKFW_DWNL_FAILED` returned when D-Bus unavailable | ⬜ |
| 12 | `DownloadCallbackData_Correct` | Percentage and status values match signal payload | ⬜ |
| 13 | `DownloadWorker_RapidSignals` | Multiple signals in quick succession all fire callbacks | ⬜ |

### Legacy Tests to Rewrite
| # | File | Reason |
|---|------|--------|
| 1 | `rdkFwupdateMgr_async_cleanup_gtest.cpp` | References old download registry init/cleanup |
| 2 | `rdkFwupdateMgr_async_refcount_gtest.cpp` | Tests old download registry slot refcounting |
| 3 | `rdkFwupdateMgr_async_signal_gtest.cpp` | Tests old download signal dispatch through registry |
| 4 | `rdkFwupdateMgr_async_stress_gtest.cpp` | Uses old `g_dwnl_registry`, concurrent registration |
| 5 | `rdkFwupdateMgr_async_threadsafety_gtest.cpp` | Tests old concurrent download registration/dispatch |
| 6 | `rdkFwupdateMgr_handlers_gtest.cpp` | May reference old download handler dispatch |
| 7 | `fwdl_interface_gtest.cpp` | May reference old download interface |

### Integration Testing
- [ ] Multi-process scenario: two separate apps call `downloadFirmware()`, daemon rejects second
- [ ] Multi-process piggyback: two apps request same firmware, both receive progress
- [ ] Daemon crash during active download: verify 3600s timeout fires, error callback, clean exit
- [ ] Rapid register/check/download/unregister cycles: no leaks, no crashes
- [ ] Download after failed download: verify `g_dwnl_in_progress` resets correctly

### Production Hardening
- [ ] **ASan validation** — Run with AddressSanitizer, verify no memory leaks or heap-use-after-free
- [ ] **TSan validation** — Run with ThreadSanitizer, verify no data races in multi-fire callback pattern
- [ ] **Coverity scan** — New code must pass with zero defects
- [ ] **30-minute download test** — verify timeout doesn't trigger prematurely on slow networks
- [ ] **Daemon crash recovery test** — verify error callback fires and thread exits cleanly
- [ ] **Rapid progress signals test** — 100 signals in 1 second, verify no queue overflow

---

## 🔮 Future Phases

### Phase 3: Migrate UpdateFirmware to On-Demand Thread
- Same pattern as DownloadFirmware (multi-fire callback, terminal status quit)
- Worker thread uses synchronous D-Bus call for accurate daemon reply
- Removes `UpdateCbRegistry`, `UpdateCbEntry`, `UpdateCbState` types
- Removes `dispatch_all_update_active()`, `internal_update_register_callback()`
- Removes last `UpdateProgress` subscription from background thread
- Estimated effort: ~8 hours

### Phase 4: Remove Persistent Background Thread Entirely
- Remove `internal_system_init()` / `internal_system_deinit()`
- Remove `BackgroundThread` struct
- Library constructor becomes a true no-op
- Zero resource cost when library is loaded but no API calls made
- Estimated effort: ~4 hours

### API Improvements (Future)
- Add `cancelDownloadFirmware()` API for mid-flight cancellation
- Stall-based timeout (no signal for N seconds) instead of total elapsed
- Change `unregisterProcess()` return type from `void` to `UnregisterResult` enum
- Add error codes for session-state violations (currently log-only)
- Add configurable timeout (env var or RFC parameter)

---

## 📁 Modified Files Summary

| File | Changes |
|------|---------|
| `librdkFwupdateMgr/src/rdkFwupdateMgr_async_internal.h` | Added `DownloadRequestContext`, `InternalDwnlSignalData`, worker declarations, session-state API. Removed legacy download registry types. |
| `librdkFwupdateMgr/src/rdkFwupdateMgr_async.c` | On-demand download worker engine, signal/timeout handlers, cancel/query APIs, status mappers. Removed old download registry + dispatch code. Removed `DownloadProgress` subscription from BG thread. |
| `librdkFwupdateMgr/src/rdkFwupdateMgr_api.c` | Rewrote `downloadFirmware()` with on-demand thread + synchronous daemon reply. Updated destructor. |
| `librdkFwupdateMgr/src/rdkFwupdateMgr_process.c` | Session-state guard for download in `unregisterProcess()`. |
| `librdkFwupdateMgr/include/rdkFwupdateMgr_client.h` | **NO CHANGES** (public API unchanged) |
| `docs/DESIGN_DOWNLOAD_FIRMWARE_ON_DEMAND_THREAD.md` | Full design document |
| `docs/DOWNLOADFIRMWARE_PROGRESS.md` | This file |

---

## 📊 Comparison: Before vs. After

### Architecture
| Aspect | Before (Registry + BG Thread) | After (On-Demand Worker Thread) |
|--------|-------------------------------|--------------------------------|
| Thread when idle | Always alive (~14KB) | **No thread (~0 bytes)** |
| Thread during download | Same BG thread (always alive) | Worker thread (same cost) |
| Thread after download | Still alive (wasted) | **Exited, freed** |
| D-Bus call model | Fire-and-forget (daemon reply ignored) | **Synchronous** (accurate accept/reject) |
| Callback dispatch | Broadcast to ALL 30 registry slots | **Direct** to single requester |
| Daemon rejection | **Lied** — returned SUCCESS anyway | **Accurate** — returns FAILED |
| Concurrency guard | None (library level) | **`g_dwnl_in_progress`** flag |
| Timeout | None (slot stays ACTIVE forever) | **3600s** with error callback |
| Memory model | 30-slot pre-allocated registry | **Per-request heap allocation** |

### Daemon Reply Accuracy
| Daemon Response | Old Library Return | New Library Return |
|----------------|-------------------|-------------------|
| Download accepted (new) | `RDKFW_DWNL_SUCCESS` ✅ | `RDKFW_DWNL_SUCCESS` ✅ |
| Download accepted (piggyback) | `RDKFW_DWNL_SUCCESS` ✅ | `RDKFW_DWNL_SUCCESS` ✅ |
| Download rejected (different FW active) | `RDKFW_DWNL_SUCCESS` ❌ **LIE** | `RDKFW_DWNL_FAILED` ✅ **TRUTH** |
| Invalid handler ID | `RDKFW_DWNL_SUCCESS` ❌ **LIE** | `RDKFW_DWNL_FAILED` ✅ **TRUTH** |
| D-Bus connection failed | `RDKFW_DWNL_FAILED` ✅ | `RDKFW_DWNL_FAILED` ✅ |
