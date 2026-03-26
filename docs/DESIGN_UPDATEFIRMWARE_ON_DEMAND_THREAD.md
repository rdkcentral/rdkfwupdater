<!-- filepath: docs/DESIGN_UPDATEFIRMWARE_ON_DEMAND_THREAD.md -->
# UpdateFirmware API — On-Demand Worker Thread Redesign

## Document Version

| Version | Date       | Author | Description                              |
|---------|------------|--------|------------------------------------------|
| 1.0     | 2026-03-25 | —      | Initial design, analysis, and migration plan for UpdateFirmware on-demand thread |

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Terminology & Clarifications](#2-terminology--clarifications)
3. [Current Architecture (Before)](#3-current-architecture-before)
4. [Proposed Architecture (After)](#4-proposed-architecture-after)
5. [Design Decisions & Rationale](#5-design-decisions--rationale)
6. [Multi-Client Scenario Walkthrough](#6-multi-client-scenario-walkthrough)
7. [Daemon Update Handler Deep Dive](#7-daemon-update-handler-deep-dive)
8. [Thread Lifecycle & Memory Ownership](#8-thread-lifecycle--memory-ownership)
9. [Thread Safety Proof](#9-thread-safety-proof)
10. [Edge Cases & Robustness](#10-edge-cases--robustness)
11. [Dead Code Removal Plan](#11-dead-code-removal-plan)
12. [File-by-File Change Specification](#12-file-by-file-change-specification)
13. [Unit Test Impact](#13-unit-test-impact)
14. [Resource Cost Comparison](#14-resource-cost-comparison)
15. [Migration Steps](#15-migration-steps)
16. [Open Items & Future Work](#16-open-items--future-work)

---

## 1. Executive Summary

This document describes the redesign of the `updateFirmware()` API implementation
in `librdkFwupdateMgr.so` (the client library) to replace the persistent background
thread + callback registry model with an **on-demand worker thread** model.

This is **Phase 3** of a three-phase migration:

| Phase | API                | Status       |
|-------|--------------------|-------------|
| 1     | `checkForUpdate()` | ✅ Complete  |
| 2     | `downloadFirmware()` | ✅ Complete |
| 3     | `updateFirmware()`  | 📋 This doc |

Phase 3 is the **final phase**. After its completion:
- The persistent background thread (`BackgroundThread`) will be **completely removed**
- `internal_system_init()` / `internal_system_deinit()` will be **removed**
- The library constructor becomes a **no-op** for async infrastructure
- All three APIs will use the identical on-demand worker thread pattern
- Zero resource cost when idle (no threads, no registries, no D-Bus connections)

### What changes

| Aspect               | Before (Phase 2 state)          | After (Phase 3)                        |
|-----------------------|---------------------------------|----------------------------------------|
| UpdateFirmware model  | Persistent BG thread + registry | On-demand worker thread                |
| BG thread             | Exists (for UpdateProgress)     | **Removed entirely**                   |
| `internal_system_init()` | Creates BG thread            | **Removed**                            |
| Constructor overhead  | Thread + GLib objects            | **Zero**                               |
| Resource when idle    | ~14KB (thread + registry + GLib) | **0 bytes**                           |
| Daemon rejection      | Ignored (always SUCCESS)         | Accurate (condvar handshake)          |
| Callback dispatch     | Broadcast to all slots           | Single ctx→callback                   |
| Signal parse          | Wrong: `(ii)` vs actual `(tsiis)` | Fixed: `(tsiis)`                     |

### What does NOT change

- **Public API header** (`rdkFwupdateMgr_client.h`) — zero modifications
- **Daemon code** — no changes required
- **CheckForUpdate flow** (Phase 1) — unchanged
- **DownloadFirmware flow** (Phase 2) — unchanged
- **`registerProcess()` / `unregisterProcess()`** API signatures — unchanged

---

## 2. Terminology & Clarifications

| Term | Meaning |
|------|---------|
| **Library** | `librdkFwupdateMgr.so` — shared library linked by client apps |
| **Daemon** | `rdkFwupdateMgr` — system service managing firmware operations |
| **Client / App** | Any process that links to the library (e.g., App A, App B) |
| **Worker thread** | Short-lived pthread created per `updateFirmware()` call |
| **BG thread** | The persistent background thread created at library load (being removed) |
| **Registry** | `UpdateCbRegistry` — fixed-size array of callback slots (being removed) |
| **handler_id** | Unique ID assigned by daemon at `registerProcess()`, carried in all signals |
| **Terminal status** | `UPDATE_COMPLETED` or `UPDATE_ERROR` — causes worker thread to exit |
| **Condvar handshake** | `pthread_cond_wait`/`signal` pattern for caller ↔ worker synchronization |
| **Isolated GMainContext** | Per-thread GLib context ensuring signals dispatch only on that thread |

---

## 3. Current Architecture (Before)

### 3.1 System Initialization (Library Load)

```
__attribute__((constructor)) library_init()
    └─ internal_system_init()
         ├─ Initialize UpdateCbRegistry (30 slots, mutex)
         └─ Start BackgroundThread:
              ├─ pthread_create(background_thread_func)
              │    ├─ g_main_context_new() → private GMainContext
              │    ├─ g_bus_get_sync() → D-Bus connection
              │    ├─ Subscribe to "UpdateProgress" signal
              │    │     handler: on_update_progress_signal()
              │    └─ g_main_loop_run() → BLOCKS FOREVER
              └─ Cost: ~14KB thread stack + GLib objects
                   (even if updateFirmware() is NEVER called)
```

### 3.2 updateFirmware() Call Flow

```
Client calls updateFirmware(handle, request, callback)
        │
  [1]   ├─ Validate handle (is registered?)
  [2]   ├─ Validate request (non-NULL, firmwareName not empty)
  [3]   ├─ Validate callback (non-NULL)
  [4]   ├─ g_bus_get_sync() → NEW D-Bus connection (caller's thread)
  [5]   ├─ internal_update_register_callback(handle, callback)
        │     └─ Lock registry mutex
        │        Find first IDLE slot
        │        slot.state = ACTIVE
        │        slot.handle_key = strdup(handle)
        │        slot.callback = callback
        │        slot.registered_time = time(NULL)
        │        Unlock mutex
        │        (If registry full → return false)
        │
  [6]   ├─ g_dbus_connection_call()  ← FIRE AND FORGET
        │     Method: "UpdateFirmware"
        │     Args: (ss) firmwareName, rebootFlag
        │     Reply callback: on_update_dbus_reply() → LOGS ONLY, ignores result
        │
  [7]   ├─ g_object_unref(connection)
  [8]   └─ return RDKFW_UPDATE_SUCCESS  ← ALWAYS, regardless of daemon response
```

### 3.3 Signal Dispatch (Background Thread)

```
Daemon emits UpdateProgress signal
        │
  BG thread receives it
        │
  on_update_progress_signal()
        │
  [A]   ├─ internal_parse_update_signal_data(parameters, &signal_data)
        │     g_variant_get(parameters, "(ii)", ...)   ← WRONG FORMAT
        │     (Daemon emits (tsiis), parse expects (ii) → READS GARBAGE)
        │
  [B]   └─ dispatch_all_update_active(&signal_data)
             Lock registry mutex
             FOR each slot WHERE state == ACTIVE:
               Build UpdateResponse:
                 response.progress = signal_data.progress_percent  ← GARBAGE
                 response.status = internal_map_update_status_code(status_code) ← GARBAGE
               slot.callback(&response)  ← FIRES CALLBACK WITH GARBAGE DATA
               IF status == UPDATE_COMPLETED or UPDATE_ERROR:
                 state = IDLE, free(handle_key)
             Unlock mutex
```

### 3.4 Problems Summary

| # | Problem | Impact |
|---|---------|--------|
| 1 | **BG thread alive 24/7** | ~14KB wasted on an embedded STB even when idle |
| 2 | **Fire-and-forget D-Bus call** | Daemon rejects → library says "SUCCESS" → client is lied to |
| 3 | **Broadcast dispatch** | All registered callbacks receive all signals — wrong for multi-client |
| 4 | **Silent callback overwrite** | Same handle calling twice → first callback silently lost |
| 5 | **Two D-Bus connections** | Caller thread opens one, BG thread has another |
| 6 | **No stale slot timeout** | Daemon crash → slot stays ACTIVE forever → handle string leaked |
| 7 | **Constructor overhead** | Thread + registry created at `dlopen()` even if never used |
| 8 | **Parse function broken** | `(ii)` format vs daemon's actual `(tsiis)` → reads garbage values |
| 9 | **Arbitrary 30-slot limit** | Hard-coded, no feedback when full beyond a log message |

---

## 4. Proposed Architecture (After)

### 4.1 System Initialization

```
__attribute__((constructor)) library_init()
    └─ (NO async init needed — all three APIs use on-demand threads)
        internal_system_init() is REMOVED
        BackgroundThread is REMOVED
        UpdateCbRegistry is REMOVED
```

### 4.2 updateFirmware() Call Flow

```
Client calls updateFirmware(handle, request, callback)
        │
  [1]   ├─ Validate handle (is registered?)
  [2]   ├─ Validate request (non-NULL, firmwareName not empty)
  [3]   ├─ Validate callback (non-NULL)
  [4]   ├─ Allocate UpdateRequestContext on heap (ctx)
        │    ctx->handle_key      = strdup(handle)
        │    ctx->firmware_name   = strdup(request->firmwareName)
        │    ctx->reboot_flag     = strdup(request->rebootFlag)
        │    ctx->callback        = callback
        │    pthread_mutex_init(&ctx->ready_mutex)
        │    pthread_cond_init(&ctx->ready_cond)
        │
  [5]   ├─ internal_begin_update(ctx)
        │    └─ if g_update_in_progress == true:
        │         return false → RDKFW_UPDATE_FAILED (reject duplicate)
        │       else:
        │         g_update_in_progress = true
        │         g_active_update_ctx = ctx
        │         return true
        │
  [6]   ├─ pthread_create(internal_update_worker_thread, ctx)
        │        │
        │   [A]  ├─ g_main_context_new() (isolated — per-thread)
        │   [B]  ├─ g_main_loop_new(ctx->context, FALSE)
        │   [C]  ├─ g_main_context_push_thread_default(ctx->context)
        │   [D]  ├─ g_bus_get_sync() → ctx->connection
        │        │
        │   [E]  ├─ g_dbus_connection_signal_subscribe(
        │        │     "UpdateProgress",
        │        │     handler = on_update_signal_handler,
        │        │     user_data = ctx)
        │        │
        │   [F]  ├─ g_dbus_connection_call_sync("UpdateFirmware",   ← BLOCKS
        │        │     g_variant_new("(ss)", firmware_name, reboot_flag))
        │        │
        │        │   Daemon checks:
        │        │     IsUpdateInProgress? Same firmware? etc.
        │        │
        │        │   Reply: (sss) result, status, message
        │        │
        │        │   IF result == "RDKFW_UPDATE_FAILED":
        │        │     ctx->init_failed = true
        │        │     ctx->daemon_reject_message = strdup(message)
        │        │     goto signal_ready
        │        │
        │        │   IF result == "RDKFW_UPDATE_SUCCESS":
        │        │     ctx->daemon_accepted = true
        │        │
        │   [G]  ├─ Add timeout source (3600s) to GMainContext
        │        │
        │   [H]  ├─ signal_ready:
        │        │     pthread_mutex_lock(&ctx->ready_mutex)
        │        │     ctx->is_ready = true
        │        │     pthread_cond_signal(&ctx->ready_cond)
        │        │     pthread_mutex_unlock(&ctx->ready_mutex)
        │        │
        │        │   IF init_failed: goto cleanup (skip loop)
        │        │
        │   [I]  ├─ g_main_loop_run()  ← BLOCKS in event loop
        │        │     │
        │        │     │  Daemon flashes firmware... (5–60 minutes)
        │        │     │
        │        │     ├─ UpdateProgress signal (25%, INPROGRESS)
        │        │     │   on_update_signal_handler():
        │        │     │     parse (tsiis) → build UpdateResponse
        │        │     │     ctx->callback(&response)  ← fires callback
        │        │     │     (NOT terminal — do not quit loop)
        │        │     │
        │        │     ├─ UpdateProgress signal (50%, INPROGRESS)
        │        │     │   ctx->callback(&response)  ← fires callback
        │        │     │
        │        │     ├─ UpdateProgress signal (100%, COMPLETED)
        │        │     │   ctx->callback(&response)  ← fires callback
        │        │     │   g_main_loop_quit()  ← TERMINAL: quit loop
        │        │     │
        │        │     └─ Timeout (3600s, no signal received)
        │        │         on_update_timeout():
        │        │           build error response (0%, UPDATE_ERROR, "timeout")
        │        │           ctx->callback(&error_response)
        │        │           g_main_loop_quit()
        │        │
        │   [J]  ├─ g_main_loop_run() returns
        │   [K]  └─ Cleanup:
        │             g_dbus_connection_signal_unsubscribe(subscription_id)
        │             g_main_context_pop_thread_default()
        │             g_object_unref(connection)
        │             g_main_loop_unref(main_loop)
        │             g_main_context_unref(context)
        │             internal_end_update() → g_update_in_progress = false
        │             free(handle_key), free(firmware_name), free(reboot_flag)
        │             free(daemon_reject_message)
        │             pthread_mutex_destroy(&ready_mutex)
        │             pthread_cond_destroy(&ready_cond)
        │             if (timeout_source) g_source_destroy(timeout_source)
        │             free(ctx)
        │             return NULL → THREAD EXITS
        │
  [7]   ├─ pthread_cond_wait() wakes up  ◄───────────────────────┘
  [8]   ├─ Check ctx->init_failed
        │    If true  → return RDKFW_UPDATE_FAILED  ← ACCURATE daemon rejection
        │    If false → return RDKFW_UPDATE_SUCCESS ← daemon truly accepted
        │
  ═══════ CALLER IS FREE — never touches ctx again ═══════
```

### 4.3 Signal Isolation (No handler_id Filtering Needed)

Each worker thread creates its own **isolated `GMainContext`**. D-Bus signals are
dispatched only on the GMainContext that holds the subscription. Because:

1. **Same process:** Library guard (`g_update_in_progress`) prevents a second worker
   thread from being created. Only one subscription exists per process.

2. **Different process (rejected):** If Process B's daemon request is rejected, the
   worker thread **skips `g_main_loop_run()`**, immediately **unsubscribes** from the
   signal, and exits. Signals queued on B's GMainContext are never dispatched because
   the loop never runs. The subscription is removed before any signal can be processed.

3. **Different process (accepted):** Cannot happen — daemon rejects concurrent updates.

Therefore, **no handler_id filtering is required**. The signal subscription lifecycle
(subscribe before `call_sync`, unsubscribe in cleanup) combined with the isolated
GMainContext guarantees that only the accepted client's callback receives signals.

---

## 5. Design Decisions & Rationale

### 5.1 Same Pattern as DownloadFirmware (Condvar Handshake)

**Decision:** Use the same condvar handshake as DownloadFirmware — caller waits for
daemon's accept/reject reply before returning `SUCCESS` or `FAILED`.

**Rationale:**
- Consistent behavior across all three APIs
- Accurate return value reflects daemon's actual decision
- Client code can trust the return value
- No need for "rejection via callback" pattern (simpler client code)
- Blocking duration is minimal (~50–200ms for D-Bus round-trip), not minutes

**Alternative considered:** Return `SUCCESS` immediately after local validation,
deliver daemon rejection via callback. Rejected because:
- Inconsistent with CheckForUpdate and DownloadFirmware
- Client must handle rejection in two places (return value AND callback)
- More complex client code for no benefit

### 5.2 Fix Parse Function: `(ii)` → `(tsiis)`

**Decision:** Fix `internal_parse_update_signal_data()` to parse the correct
GVariant signature `(tsiis)` matching the daemon's actual emission.

**Rationale:**
- The current `(ii)` format is wrong — daemon emits `(tsiis)` per introspection XML
  and the actual `g_variant_new()` call in `rdkv_upgrade.c`
- Current code reads garbage values for progress and status
- This is a correctness bug, not a design choice

### 5.3 Remove Persistent Background Thread Entirely

**Decision:** After Phase 3, remove the `BackgroundThread` struct, `internal_system_init()`,
`internal_system_deinit()`, and the library constructor's async initialization.

**Rationale:**
- Phase 1 removed `CheckForUpdateComplete` subscription from BG thread
- Phase 2 removed `DownloadProgress` subscription from BG thread
- Phase 3 removes `UpdateProgress` subscription — the BG thread has **nothing left to do**
- Keeping an empty thread alive wastes ~14KB and adds code complexity

### 5.4 Remove UpdateCbRegistry Entirely

**Decision:** Replace the 30-slot registry with a single `UpdateRequestContext` per request.

**Rationale:**
- Only one update can be active per process (library guard)
- Only one update can be active per device (daemon guard)
- A 30-slot registry for a maximum of 1 active operation is unnecessary overhead
- The per-request context pattern (from Phase 1 and 2) is proven, simpler, and leak-free

### 5.5 Timeout: 3600 Seconds

**Decision:** Use 3600s (1 hour) timeout, same as DownloadFirmware.

**Rationale:**
- Firmware flashing on embedded devices typically takes 5–30 minutes
- 1 hour provides generous safety margin
- Consistent with DownloadFirmware timeout
- If daemon crashes mid-flash, client learns within 1 hour (not stuck forever)
- Can be adjusted later if field data suggests a different value

---

## 6. Multi-Client Scenario Walkthrough

### Scenario: Client A flashes, Client B requests during flash

```
TIME    CLIENT A                    LIBRARY (librdkFwupdateMgr.so)           DAEMON
────    ────────                    ──────────────────────────────           ──────

t=0     updateFirmware(1,req,cb_A)
          │
          ├─ validate ✅
          ├─ alloc UpdateRequestContext_A
          ├─ internal_begin_update(ctx_A)
          │    g_update_in_progress = true ✅
          ├─ pthread_create(worker_A)
          │        │
          │   Worker A:
          │        ├─ GMainContext_A (isolated)
          │        ├─ subscribe UpdateProgress
          │        ├─ call_sync("UpdateFirmware")  ───────────────────────►
          │        │                                          Daemon: no active update
          │        │                                          → ACCEPTED
          │        │   ◄──────────────────────────────────────
          │        ├─ daemon_accepted = true
          │        ├─ cond_signal(ready)
          │        │
          ├─ cond_wait returns
          ├─ init_failed == false
          └─ return RDKFW_UPDATE_SUCCESS ✅
                                                              Daemon starts flashing...

t=5     updateFirmware(2,req,cb_B)
          │
          ├─ validate ✅
          ├─ alloc UpdateRequestContext_B
          ├─ internal_begin_update(ctx_B)
          │    g_update_in_progress == true → return false
          ├─ free(ctx_B)
          └─ return RDKFW_UPDATE_FAILED ✅
              (No thread created, no D-Bus call, no wasted resources)

t=10                                Worker A's loop:
                                    UpdateProgress(25%, INPROG)
          cb_A(25, UPDATE_INPROGRESS, "Flashing...") ◄──────

t=30                                UpdateProgress(50%, INPROG)
          cb_A(50, UPDATE_INPROGRESS, "Flashing...") ◄──────

t=60                                UpdateProgress(100%, COMPLETED)
          cb_A(100, UPDATE_COMPLETED, "Done") ◄──────────────
                                    g_main_loop_quit()
                                    Worker A cleanup:
                                      unsubscribe
                                      internal_end_update()
                                        g_update_in_progress = false
                                      free everything
                                      thread exits

t=61    Client B can now retry:
        updateFirmware(2,req,cb_B)
          ├─ internal_begin_update(ctx_B) → true ✅
          └─ ... succeeds ...
```

### Scenario: Client B in a different process, daemon rejects

```
TIME    CLIENT A (Process 1)        CLIENT B (Process 2)         DAEMON
────    ────────────────────        ────────────────────         ──────

t=0     updateFirmware(1,req,cb_A)
          → worker_A started
          → call_sync → ACCEPTED
          → return SUCCESS ✅

t=5                                 updateFirmware(2,req,cb_B)
                                      → worker_B started
                                        (B's process has g_update_in_progress=false ← own copy)
                                      → subscribe UpdateProgress
                                      → call_sync("UpdateFirmware") ────────►
                                                                    Daemon: update active!
                                                                    → REJECTED
                                      ◄────────────────────────────────────
                                      → init_failed = true
                                      → cond_signal(ready)
                                      → SKIP g_main_loop_run()
                                      → unsubscribe ← signal removed before any dispatch
                                      → internal_end_update()
                                      → cleanup, free, thread exits

                                    return RDKFW_UPDATE_FAILED ✅
                                    (B never receives A's UpdateProgress signals)

t=10    cb_A(25%, INPROG) ◄──────   (B's thread is already dead, no subscription)
t=30    cb_A(50%, INPROG) ◄──────
t=60    cb_A(100%, COMPLETED) ◄──
          worker_A exits
```

---

## 7. Daemon Update Handler Deep Dive

### 7.1 D-Bus Method: `UpdateFirmware`

From `src/rdkFwupdateMgr.c`, the daemon handler:

```
D-Bus method "UpdateFirmware" received
    │
    ├─ Parse (ss): firmwareName, rebootFlag
    │
    ├─ Check: IsUpdateInProgress()?
    │   └─ If YES:
    │        reply (sss): "RDKFW_UPDATE_FAILED", "REJECTED", "Another update in progress"
    │        return
    │
    ├─ SetUpdateInProgress(true)
    ├─ Reply (sss): "RDKFW_UPDATE_SUCCESS", "ACCEPTED", "Firmware update initiated"
    │
    ├─ Start firmware flashing (flash.c / rdkv_upgrade.c)
    │   └─ Periodically emit UpdateProgress signal:
    │        g_variant_new("(tsiis)",
    │          handler_id,        // t  uint64
    │          firmware_name,     // s  string
    │          progress_percent,  // i  int32
    │          status_code,       // i  int32
    │          message)           // s  string
    │
    └─ On completion/error:
         Emit final UpdateProgress with terminal status
         SetUpdateInProgress(false)
```

### 7.2 D-Bus Signal: `UpdateProgress`

| Field | Type | Description |
|-------|------|-------------|
| `handlerId` | `t` (uint64) | Handler ID assigned at registration |
| `firmwareName` | `s` (string) | Name of firmware being flashed |
| `progressPercent` | `i` (int32) | 0–100 completion percentage |
| `status` | `i` (int32) | Status code (maps to `UpdateStatus` enum) |
| `message` | `s` (string) | Human-readable status message |

### 7.3 Status Code Mapping

| status_code (int) | UpdateStatus enum | Terminal? |
|---|---|---|
| 0 | `RDKFW_UPDATE_COMPLETED` | ✅ Yes |
| 1 | `RDKFW_UPDATE_INPROGRESS` | No |
| 2 | `RDKFW_UPDATE_ERROR` | ✅ Yes |
| other | `RDKFW_UPDATE_ERROR` (default) | ✅ Yes |

---

## 8. Thread Lifecycle & Memory Ownership

### 8.1 UpdateRequestContext Lifecycle

```
   CALLER THREAD                      WORKER THREAD
   ─────────────                      ─────────────
   calloc(ctx)              ─────►    (ctx passed via pthread_create arg)
   populate ctx fields
   pthread_create()
                                      ctx is now SHARED during handshake
   cond_wait()
                                      setup D-Bus, subscribe, call_sync
                                      cond_signal(ready)
                                      ┌─ ctx->init_failed? ─┐
   cond_wait returns                  │  YES: goto cleanup   │
   read ctx->init_failed              │  NO:  run loop       │
   return to client                   └──────────────────────┘
   ═══ NEVER TOUCH ctx AGAIN ═══
                                      g_main_loop_run()
                                      ... signals fire callbacks ...
                                      terminal → quit loop
                                      internal_end_update()
                                      free all strings
                                      destroy mutex/cond
                                      free(ctx)  ─────►  ctx is DEAD
                                      return NULL ─────► thread exits
```

### 8.2 Memory Ownership Rules

| Resource | Allocated by | Freed by | When |
|----------|-------------|----------|------|
| `ctx` (struct) | `updateFirmware()` caller | Worker thread | After cleanup |
| `ctx->handle_key` | `updateFirmware()` via `strdup` | Worker thread | In cleanup |
| `ctx->firmware_name` | `updateFirmware()` via `strdup` | Worker thread | In cleanup |
| `ctx->reboot_flag` | `updateFirmware()` via `strdup` | Worker thread | In cleanup |
| `ctx->daemon_reject_message` | Worker thread via `strdup` | Worker thread | In cleanup |
| `ctx->connection` | Worker thread via `g_bus_get_sync` | Worker thread via `g_object_unref` | In cleanup |
| `ctx->main_loop` | Worker thread via `g_main_loop_new` | Worker thread via `g_main_loop_unref` | In cleanup |
| `ctx->context` | Worker thread via `g_main_context_new` | Worker thread via `g_main_context_unref` | In cleanup |
| `ctx->timeout_source` | Worker thread via `g_timeout_source_new_seconds` | Worker thread via `g_source_destroy` + `g_source_unref` | In cleanup |
| `ctx->ready_mutex` | `updateFirmware()` via `pthread_mutex_init` | Worker thread via `pthread_mutex_destroy` | In cleanup |
| `ctx->ready_cond` | `updateFirmware()` via `pthread_cond_init` | Worker thread via `pthread_cond_destroy` | In cleanup |
| Signal data strings | GLib (from `g_variant_get`) | Worker thread via `g_free` | After callback dispatch |

### 8.3 Exception: pthread_create Failure

If `pthread_create()` fails, ownership stays with the caller:

```c
if (pthread_create(&ctx->thread, NULL, internal_update_worker_thread, ctx) != 0) {
    internal_abort_update();          // clear g_update_in_progress
    free(ctx->handle_key);
    free(ctx->firmware_name);
    free(ctx->reboot_flag);
    pthread_mutex_destroy(&ctx->ready_mutex);
    pthread_cond_destroy(&ctx->ready_cond);
    free(ctx);
    return RDKFW_UPDATE_FAILED;
}
```

---

## 9. Thread Safety Proof

### 9.1 Shared State Inventory

| Variable | Writers | Readers | Protection |
|----------|---------|---------|------------|
| `g_update_in_progress` | `internal_begin_update`, `internal_end_update`, `internal_abort_update` | `internal_is_update_in_progress`, `internal_begin_update` | `g_update_in_progress_mutex` |
| `g_active_update_ctx` | `internal_begin_update`, `internal_end_update`, `internal_abort_update` | `internal_cancel_all_active_update_threads` | `g_update_in_progress_mutex` |
| `ctx->is_ready` | Worker thread | Caller thread | `ctx->ready_mutex` + `ctx->ready_cond` |
| `ctx->init_failed` | Worker thread (before `is_ready=true`) | Caller thread (after `cond_wait` returns) | Condvar guarantees happens-before |

### 9.2 No Data Races

**Caller → Worker (write-before-signal):**
All `ctx` fields are populated by the caller **before** `pthread_create()`. The POSIX
`pthread_create()` call establishes a happens-before relationship — the worker thread
sees all writes made by the caller before the create call.

**Worker → Caller (signal-before-read):**
Worker writes `ctx->init_failed` and `ctx->daemon_accepted` **before** setting
`ctx->is_ready = true` and calling `pthread_cond_signal()`. The condvar signal
establishes a happens-before relationship — the caller reads consistent values
after `pthread_cond_wait()` returns.

**Worker post-handshake:**
After the condvar handshake, the caller **never touches ctx again**. The worker
has exclusive ownership. No further synchronization needed.

### 9.3 No Deadlocks

- `ctx->ready_mutex` is held only briefly (set `is_ready`, signal cond, unlock)
- `g_update_in_progress_mutex` is held only for atomic check-and-set (~10ns)
- No nested mutex acquisition
- Callbacks invoked with NO mutex held

### 9.4 No Use-After-Free

- Caller never accesses `ctx` after returning from `updateFirmware()`
- Worker frees `ctx` only after all cleanup is complete
- `internal_end_update()` clears `g_active_update_ctx = NULL` **before** `free(ctx)`
- Destructor calls `internal_cancel_all_active_update_threads()` which reads
  `g_active_update_ctx` under mutex, copies the thread handle, then joins

---

## 10. Edge Cases & Robustness

### 10.1 Daemon Crash During Flash

```
Worker thread is in g_main_loop_run(), waiting for UpdateProgress signals.
Daemon crashes. No more signals arrive.
    │
    ├─ 3600s timeout fires
    ├─ on_update_timeout():
    │     Build error response: (0, UPDATE_ERROR, "Timeout: no progress signal")
    │     ctx->callback(&error_response)
    │     g_main_loop_quit()
    ├─ Cleanup proceeds normally
    └─ Thread exits cleanly
```

### 10.2 Client Crashes During Flash

```
Client process receives SIGSEGV or exit().
    │
    ├─ OS reclaims all memory (including ctx, worker thread stack)
    ├─ D-Bus connection closed automatically (socket closed)
    ├─ Daemon continues flashing (doesn't care about client)
    └─ No resource leak (OS cleanup)
```

### 10.3 Library Unload During Active Flash

```
dlclose(librdkFwupdateMgr.so)
    │
    └─ __attribute__((destructor)) library_deinit()
         ├─ internal_cancel_all_active_update_threads()
         │    ├─ Lock mutex, read g_active_update_ctx
         │    ├─ If non-NULL:
         │    │    copy thread handle
         │    │    g_main_loop_quit(ctx->main_loop)  ← wakes worker
         │    │    Unlock mutex
         │    │    pthread_join(thread)  ← blocks until worker exits
         │    └─ Worker exits cleanly (normal cleanup path)
         ├─ internal_cancel_all_active_download_threads()
         ├─ internal_cancel_all_active_check_threads()
         └─ (No more internal_system_deinit() — removed in Phase 3)
```

### 10.4 Rapid Retry After Failure

```
Client A: updateFirmware() → daemon rejects → FAILED
    Worker thread: init_failed → skip loop → cleanup → end_update() → exit
    g_update_in_progress = false

Client A: updateFirmware() → (immediately retries)
    internal_begin_update() → g_update_in_progress == false → true → SUCCESS
    Worker thread starts normally
```

The cleanup in the rejected worker thread's path ensures `g_update_in_progress`
is cleared **before** the thread exits, so retries succeed immediately.

### 10.5 Condvar Spurious Wakeup

```c
pthread_mutex_lock(&ctx->ready_mutex);
while (!ctx->is_ready) {              // LOOP guards against spurious wakeup
    pthread_cond_wait(&ctx->ready_cond, &ctx->ready_mutex);
}
pthread_mutex_unlock(&ctx->ready_mutex);
```

The `while (!ctx->is_ready)` loop ensures the caller only proceeds when the
worker has genuinely completed setup (or failed). Spurious wakeups re-enter
the wait.

---

## 11. Dead Code Removal Plan

Phase 3 removes the **last consumer** of the persistent background thread. This
enables complete removal of the following infrastructure:

### 11.1 Types to Remove

| Type | File | Reason |
|------|------|--------|
| `UpdateCbState` enum | `_async_internal.h` | Replaced by per-request context |
| `UpdateCbEntry` struct | `_async_internal.h` | Replaced by `UpdateRequestContext` |
| `UpdateCbRegistry` struct | `_async_internal.h` | No more registry |
| `BackgroundThread` struct | `_async_internal.h` | BG thread removed entirely |

### 11.2 Functions to Remove

| Function | File | Reason |
|----------|------|--------|
| `internal_system_init()` | `_async.c` | No more async init at constructor |
| `internal_system_deinit()` | `_async.c` | No more BG thread to stop |
| `background_thread_func()` | `_async.c` | BG thread removed |
| `internal_update_register_callback()` | `_async.c` | No more registry |
| `dispatch_all_update_active()` | `_async.c` | No more broadcast dispatch |
| `on_update_progress_signal()` | `_async.c` | Replaced by `on_update_signal_handler()` |
| `on_update_dbus_reply()` | `_api.c` | Fire-and-forget removed |
| Old `updateFirmware()` body | `_api.c` | Replaced entirely |

### 11.3 Global Variables to Remove

| Variable | File | Reason |
|----------|------|--------|
| `g_bg_thread` | `_async.c` | BG thread removed |
| `g_update_registry` | `_async.c` | Registry removed |

### 11.4 Constructor/Destructor Changes

| Function | Before | After |
|----------|--------|-------|
| `library_init()` (constructor) | Calls `internal_system_init()` | Remove that call (or remove constructor if it does nothing else) |
| `library_deinit()` (destructor) | Calls `internal_system_deinit()` + cancel threads | Remove `internal_system_deinit()` call, keep cancel thread calls |

---

## 12. File-by-File Change Specification

### 12.1 `librdkFwupdateMgr/src/rdkFwupdateMgr_async_internal.h`

**Remove:**
- `BackgroundThread` struct
- `UpdateCbState` enum
- `UpdateCbEntry` struct
- `UpdateCbRegistry` struct
- `internal_system_init()` declaration
- `internal_system_deinit()` declaration
- `internal_update_register_callback()` declaration
- Old architecture diagram showing BG thread for Update

**Add:**
- `UpdateRequestContext` struct (modeled on `DownloadRequestContext`):
  ```c
  typedef struct {
      /* Condvar handshake */
      pthread_mutex_t   ready_mutex;
      pthread_cond_t    ready_cond;
      bool              is_ready;
      bool              init_failed;

      /* GLib event loop (isolated, per-thread) */
      GMainContext     *context;
      GMainLoop        *main_loop;
      GDBusConnection  *connection;
      guint             subscription_id;

      /* Request data (all strdup'd — owned by worker thread) */
      char             *handle_key;
      char             *firmware_name;
      char             *reboot_flag;
      UpdateCallback    callback;

      /* Daemon reply */
      bool              daemon_accepted;
      char             *daemon_reject_message;

      /* Timeout */
      GSource          *timeout_source;

      /* Thread handle */
      pthread_t         thread;
  } UpdateRequestContext;
  ```
- `#define UPDATE_SIGNAL_TIMEOUT_SECONDS 3600`
- Function declarations:
  - `void *internal_update_worker_thread(void *arg);`
  - `bool internal_begin_update(UpdateRequestContext *ctx);`
  - `void internal_end_update(void);`
  - `void internal_abort_update(void);`
  - `bool internal_is_update_in_progress(void);`
  - `void internal_cancel_all_active_update_threads(void);`

**Modify:**
- Architecture overview comment: add Phase 3 UpdateFirmware on-demand thread diagram
- Remove Phase 3 "unchanged" note
- Fix `internal_parse_update_signal_data()` doc: `(ii)` → `(tsiis)`

### 12.2 `librdkFwupdateMgr/src/rdkFwupdateMgr_async.c`

**Remove:**
- `static BackgroundThread g_bg_thread;`
- `static UpdateCbRegistry g_update_registry;`
- `background_thread_func()`
- `internal_system_init()`
- `internal_system_deinit()`
- `internal_update_register_callback()`
- `dispatch_all_update_active()`
- `on_update_progress_signal()`

**Add:**
- Static state:
  ```c
  static pthread_mutex_t       g_update_in_progress_mutex = PTHREAD_MUTEX_INITIALIZER;
  static bool                  g_update_in_progress       = false;
  static UpdateRequestContext  *g_active_update_ctx        = NULL;
  ```
- `internal_begin_update()` — atomic check-and-set
- `internal_end_update()` — clear state
- `internal_abort_update()` — clear state (error path)
- `internal_is_update_in_progress()` — query
- `internal_cancel_all_active_update_threads()` — quit loop + join
- `on_update_signal_handler()` — parse `(tsiis)`, build `UpdateResponse`, fire callback, quit on terminal
- `on_update_timeout()` — fire error callback, quit loop
- `internal_update_worker_thread()` — full lifecycle

**Modify:**
- `internal_parse_update_signal_data()` — fix `(ii)` → `(tsiis)`

### 12.3 `librdkFwupdateMgr/src/rdkFwupdateMgr_api.c`

**Remove:**
- `on_update_dbus_reply()` function
- Old `updateFirmware()` body
- `internal_system_init()` call from constructor
- `internal_system_deinit()` call from destructor

**Add:**
- New `updateFirmware()` body (validate → alloc ctx → begin_update → pthread_create → condvar wait → return)
- `internal_cancel_all_active_update_threads()` call in destructor

### 12.4 `librdkFwupdateMgr/src/rdkFwupdateMgr_process.c`

**Add:**
- `internal_is_update_in_progress()` guard in `unregisterProcess()`

### 12.5 Public Header (`librdkFwupdateMgr/include/rdkFwupdateMgr_client.h`)

**NO CHANGES.**

### 12.6 Daemon Code (`src/`)

**NO CHANGES.**

---

## 13. Unit Test Impact

### 13.1 Tests to Remove/Rewrite

| Test | Reason |
|------|--------|
| `test_update_register_callback_*` | Registry removed |
| `test_dispatch_all_update_active_*` | Dispatch removed |
| `test_bg_thread_*` | BG thread removed |
| `test_update_registry_full` | Registry removed |

### 13.2 New Tests Required

| Test | What it validates |
|------|-------------------|
| `test_update_worker_thread_daemon_accepts` | Full happy path: ctx alloc → thread → call_sync accepted → signals → callback fires → cleanup |
| `test_update_worker_thread_daemon_rejects` | Daemon rejects → init_failed=true → caller gets FAILED → thread exits cleanly |
| `test_update_in_progress_guard_same_process` | Second `updateFirmware()` while first is active → returns FAILED |
| `test_update_timeout` | No signals → 3600s timeout → callback(ERROR) → thread exits |
| `test_update_callback_fires_multiple_times` | Multiple INPROGRESS signals → callback fires each time → COMPLETED → quit |
| `test_update_callback_fires_on_error` | ERROR signal → callback fires → quit |
| `test_update_context_freed_after_completion` | No memory leaks (valgrind) |
| `test_update_context_freed_after_rejection` | No memory leaks on rejection path |
| `test_update_context_freed_after_pthread_create_fails` | Caller frees ctx correctly |
| `test_update_unregister_blocked_during_update` | `unregisterProcess()` returns FAILED while update active |
| `test_update_destructor_joins_thread` | Library unload during active update → thread joined cleanly |
| `test_update_parse_signal_tsiis` | Parse function correctly handles `(tsiis)` format |
| `test_update_parse_signal_invalid` | Parse function returns false on bad input |

### 13.3 Tests Unchanged

All CheckForUpdate and DownloadFirmware tests remain unchanged.

---

## 14. Resource Cost Comparison

### 14.1 Idle State (No Operations Active)

| Resource | Before (Phase 2 state) | After (Phase 3) |
|----------|----------------------|------------------|
| Threads | 1 (BG thread) | **0** |
| D-Bus connections | 1 (BG thread) | **0** |
| GMainLoop instances | 1 (BG thread) | **0** |
| GMainContext instances | 1 (BG thread) | **0** |
| Registry memory | ~2.5KB (30 × UpdateCbEntry) | **0** |
| Signal subscriptions | 1 (UpdateProgress) | **0** |
| **Total** | **~14KB** | **0 bytes** |

### 14.2 During Active Update

| Resource | Before | After |
|----------|--------|-------|
| Threads | 1 BG + caller's thread | **1 worker thread** |
| D-Bus connections | 2 (BG + caller) | **1 (worker only)** |
| Signal subscriptions | 1 (BG thread) | **1 (worker thread)** |
| Context memory | 30-slot registry (~2.5KB) | **1 ctx (~200 bytes)** |

### 14.3 Full System Comparison (All Three APIs Idle)

| Resource | Before Phase 1 | After Phase 3 |
|----------|---------------|---------------|
| Threads | 1 BG (permanent) | **0** |
| D-Bus connections | 1 BG (permanent) | **0** |
| Registries | 3 (Check + Dwnl + Update) | **0** |
| Total idle memory | **~18KB** | **0 bytes** |

---

## 15. Migration Steps

### Step 1: Update Internal Header

- Remove old types (BackgroundThread, UpdateCb*, system_init/deinit declarations)
- Add `UpdateRequestContext`, new function declarations
- Update architecture overview
- Fix parse function doc

### Step 2: Implement New Update Engine in `_async.c`

- Add static state (`g_update_in_progress`, `g_active_update_ctx`)
- Implement all accessor functions (begin/end/abort/is_in_progress/cancel)
- Implement `internal_update_worker_thread()`
- Implement `on_update_signal_handler()`
- Implement `on_update_timeout()`
- Fix `internal_parse_update_signal_data()`: `(ii)` → `(tsiis)`

### Step 3: Remove Old Update Engine from `_async.c`

- Remove `g_bg_thread`, `g_update_registry`
- Remove `background_thread_func()`
- Remove `internal_system_init()`, `internal_system_deinit()`
- Remove `internal_update_register_callback()`
- Remove `dispatch_all_update_active()`
- Remove `on_update_progress_signal()`

### Step 4: Update `_api.c`

- Replace `updateFirmware()` body
- Remove `on_update_dbus_reply()`
- Remove `internal_system_init()` call from constructor
- Remove `internal_system_deinit()` call from destructor
- Add `internal_cancel_all_active_update_threads()` to destructor

### Step 5: Update `_process.c`

- Add `internal_is_update_in_progress()` guard in `unregisterProcess()`

### Step 6: Update Unit Tests

- Remove old registry/dispatch tests
- Add new on-demand thread tests
- Verify all existing Check/Download tests still pass

### Step 7: Verification

- Valgrind (no leaks)
- Thread sanitizer (no races)
- Manual testing: happy path, rejection, timeout, rapid retry
- Destructor test: `dlclose` during active update

---

## 16. Open Items & Future Work

### 16.1 Resolved

| Item | Resolution |
|------|-----------|
| Signal format mismatch | Fix parse function: `(ii)` → `(tsiis)` |
| handler_id filtering | Not needed — isolated GMainContext + unsubscribe-on-rejection handles it |
| Condvar vs immediate return | Use condvar (same as DownloadFirmware, consistent across all APIs) |
| Timeout value | 3600s (same as DownloadFirmware) |

### 16.2 Future Work (Post Phase 3)

| Item | Phase |
|------|-------|
| Remove `MAX_PENDING_CALLBACKS` constant (no more registries) | Phase 3 cleanup |
| Consider making timeout configurable via RFC | Future |
| Consolidate common worker thread boilerplate into shared helper | Future (Phase 4?) |
| Add telemetry/metrics for update duration | Future |
| Consider `rebootFlag` handling validation | Future |

### 16.3 Related Documents

| Document | Description |
|----------|-------------|
| `docs/DESIGN_CHECKFORUPDATE_ON_DEMAND_THREAD.md` | Phase 1 design (complete) |
| `docs/DESIGN_DOWNLOAD_FIRMWARE_ON_DEMAND_THREAD.md` | Phase 2 design (complete) |
| `docs/CHECKFORUPDATE_PROGRESS.md` | Phase 1 tracking |
| `docs/DOWNLOADFIRMWARE_PROGRESS.md` | Phase 2 tracking |
| `docs/TRACKING_CHECKFORUPDATE_REDESIGN.md` | Phase 1 step-by-step tracking |
| `docs/TRACKING_DOWNLOADFIRMWARE_REDESIGN.md` | Phase 2 step-by-step tracking |
