# CheckForUpdate API — On-Demand Worker Thread Redesign

## Document Version

| Version | Date       | Author | Description                              |
|---------|------------|--------|------------------------------------------|
| 1.0     | 2026-03-16 | —      | Initial design, analysis, and migration plan |
| 1.1     | 2026-03-16 | —      | REVISED §5.4: Block unregisterProcess() during active checkForUpdate(). Added §9.9 (SIGTERM handling). Updated §11.5 (process.c changes). Updated §15.1 resolved items. |
| 1.2     | 2026-03-17 | —      | Encapsulated CheckForUpdate state: replaced extern globals with internal_begin_check()/internal_end_check()/internal_abort_check() accessors. All state now static in _async.c. |

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Terminology & Clarifications](#2-terminology--clarifications)
3. [Current Architecture (Before)](#3-current-architecture-before)
4. [Proposed Architecture (After)](#4-proposed-architecture-after)
5. [Design Decisions & Rationale](#5-design-decisions--rationale)
6. [Multi-Client Scenario Walkthrough](#6-multi-client-scenario-walkthrough)
7. [Thread Lifecycle & Memory Ownership](#7-thread-lifecycle--memory-ownership)
8. [Thread Safety Proof](#8-thread-safety-proof)
9. [Edge Cases & Robustness](#9-edge-cases--robustness)
10. [Dead Code Removal Plan](#10-dead-code-removal-plan)
11. [File-by-File Change Specification](#11-file-by-file-change-specification)
12. [Unit Test Impact](#12-unit-test-impact)
13. [Resource Cost Comparison](#13-resource-cost-comparison)
14. [Migration Phases](#14-migration-phases)
15. [Open Items & Future Work](#15-open-items--future-work)

---

## 1. Executive Summary

This document describes the redesign of the `checkForUpdate()` API implementation
within `librdkFwupdateMgr.so`. The change replaces the **persistent background
thread** model (thread created at library load, lives until library unload) with an
**on-demand worker thread** model (thread created per `checkForUpdate()` call,
destroyed after the callback fires).

**Goals:**

- Zero resource cost when no `checkForUpdate()` is in progress
- Thread exists only for the duration of one firmware check operation
- No change to the public API (`rdkFwupdateMgr_client.h`)
- Correct multi-client behavior (separate processes A and B both get callbacks)
- No memory leaks, no crashes, no dangling threads
- Clean dead code removal of the old CheckForUpdate registry

**Scope:** `checkForUpdate()` API only. `downloadFirmware()` and `updateFirmware()`
remain on the existing persistent-thread model in this phase and will be migrated
subsequently.

---

## 2. Terminology & Clarifications

### 2.1 What is "the caller"?

**The caller** is the **client application's thread** that calls `checkForUpdate()`.
This is the app's main thread (or whichever thread the app uses to invoke the API).

Example from `example_app.c`:
```c
// This is the CALLER — it's the app's main() thread
CheckForUpdateResult cfu_result = checkForUpdate(g_handle, on_firmware_check_callback);
// ← checkForUpdate() returns here. The caller is free to do anything after this.
```

After `checkForUpdate()` returns `CHECK_FOR_UPDATE_SUCCESS`, the caller's
involvement is **over**. The caller does not wait, does not block, does not touch
any internal state. It is the caller's application code that continues executing.

The caller's stack frame for `checkForUpdate()` is indeed "gone" after the function
returns — meaning the local variables inside the `checkForUpdate()` function body
are deallocated. But this is irrelevant because:

### 2.2 What is `ctx` (CheckRequestContext)?

`ctx` is a **heap-allocated** structure (`calloc`/`malloc`). It is NOT a stack
variable. It lives on the heap, which means it survives after `checkForUpdate()`
returns.

**Lifecycle of `ctx`:**

```
CALLER THREAD                               WORKER THREAD
─────────────                                ─────────────
checkForUpdate() {
  ctx = calloc(1, sizeof(*ctx));   ← ctx BORN on the heap
  ctx->handle_key = strdup(handle);
  ctx->callback = callback;
  pthread_create(worker, ctx);     ← ownership TRANSFERRED to worker
  pthread_cond_wait(ctx->ready);   ← caller reads ctx->is_ready (under mutex)
  return SUCCESS;                  ← caller NEVER touches ctx again
}                                  ← stack frame gone, but ctx is on heap!
                                             │
                                             ├─ worker uses ctx throughout its life
                                             ├─ worker fires ctx->callback
                                             ├─ worker frees ctx->handle_key
                                             ├─ worker destroys ctx->ready_mutex
                                             ├─ worker destroys ctx->ready_cond
                                             └─ free(ctx)    ← ctx DIES
```

**Key point:** `ctx` is owned by the heap. The caller allocates it, then transfers
ownership to the worker thread. After the condvar handshake, the caller never
reads or writes `ctx` again. The worker thread is the sole owner and is responsible
for freeing it.

### 2.3 What is "the worker thread"?

The **worker thread** is a `pthread` spawned by `checkForUpdate()`. It:

1. Creates a GLib event loop
2. Connects to D-Bus
3. Subscribes to `CheckForUpdateComplete` signal
4. Sends the `CheckForUpdate` D-Bus method call to the daemon
5. Signals the caller "I'm ready" via condvar
6. Runs the event loop, waiting for the daemon's signal
7. When signal arrives: parses it, fires the client's callback
8. Cleans up all resources and exits (thread terminates)

The worker thread is **not** a persistent thread. It is born for one request and
dies when that request is complete.

---

## 3. Current Architecture (Before)

### 3.1 What happens today

```
Library load (__attribute__((constructor)))
  │
  └─► internal_system_init()
        ├─ Initialize g_registry (30-slot CallbackEntry array + mutex)
        ├─ Initialize g_dwnl_registry (30-slot DwnlCallbackEntry array + mutex)
        ├─ Initialize g_update_registry (30-slot UpdateCbEntry array + mutex)
        ├─ Create GMainContext + GMainLoop
        └─ pthread_create(background_thread_func)
              │
              ├─ Connect to D-Bus
              ├─ Subscribe to CheckForUpdateComplete
              ├─ Subscribe to DownloadProgress
              ├─ Subscribe to UpdateProgress
              ├─ Signal ready (spin-wait)
              └─ g_main_loop_run()  ← BLOCKS FOREVER until library unload
                   │
                   │ (idle... idle... idle... for hours/days)
                   │
                   │ signal arrives → on_check_complete_signal()
                   │   → dispatch_all_pending()
                   │     → fires ALL PENDING callbacks (broadcast to everyone)
                   │
                   │ (idle again...)

checkForUpdate(handle, callback)
  ├─ Validate handle + callback
  ├─ Connect to D-Bus (from caller thread — a SECOND connection)
  ├─ internal_register_callback(handle, callback) → puts in g_registry[slot]
  ├─ g_dbus_connection_call("CheckForUpdate") → fire-and-forget from caller thread
  └─ Return CHECK_FOR_UPDATE_SUCCESS

Library unload (__attribute__((destructor)))
  └─► internal_system_deinit()
        ├─ g_main_loop_quit() → background thread wakes up
        ├─ pthread_join() → wait for thread to exit
        └─ Free all registries, mutexes, GLib objects
```

### 3.2 Problems with current design

| Problem | Details |
|---------|---------|
| Persistent idle thread | Thread + D-Bus connection + GMainContext consume ~14KB even when no requests are active |
| No signal routing | `dispatch_all_pending()` fires ALL pending callbacks regardless of which handler_id the signal is for |
| Constructor overhead | Thread, D-Bus connection, and 3 registries created at library load even if the app never calls `checkForUpdate()` |
| Spin-wait at init | `internal_system_init()` uses 50 × 100ms nanosleep polling loop instead of a proper condvar |
| Timeout not implemented | `CALLBACK_TIMEOUT_SECONDS = 60` is defined but never enforced — stale PENDING entries accumulate forever |
| Two D-Bus connections | The caller thread creates a connection for fire-and-forget, while the BG thread has a separate connection for signal listening |

---

## 4. Proposed Architecture (After)

### 4.1 New flow for checkForUpdate()

```
Library load (__attribute__((constructor)))
  │
  └─► internal_system_init()       ← STILL CALLED (for Download/Update)
        ├─ Initialize g_dwnl_registry    ← KEPT (for downloadFirmware)
        ├─ Initialize g_update_registry  ← KEPT (for updateFirmware)
        ├─ Create GMainContext + GMainLoop
        └─ pthread_create(background_thread_func)
              ├─ Connect to D-Bus
              ├─ Subscribe to DownloadProgress     ← KEPT
              ├─ Subscribe to UpdateProgress       ← KEPT
              ├─ (CheckForUpdateComplete subscription REMOVED)
              └─ g_main_loop_run()

checkForUpdate(handle, callback)
  │
  ├─ [1] Validate handle (not NULL, not empty)
  ├─ [2] Validate callback (not NULL)
  ├─ [3] Check: is a checkForUpdate already in progress for this process?
  │       If YES → log warning, return CHECK_FOR_UPDATE_FAIL
  ├─ [4] Allocate CheckRequestContext on heap
  │       ctx->handle_key = strdup(handle)
  │       ctx->callback = callback
  │       init ready_mutex, ready_cond
  ├─ [5] Set g_check_in_progress = true
  ├─ [6] Track ctx in active list (for library unload safety)
  ├─ [7] pthread_create(internal_check_worker_thread, ctx)
  │                                           │
  │                                           ├─ [A] g_main_context_new() (isolated)
  │                                           ├─ [B] g_main_loop_new()
  │                                           ├─ [C] g_main_context_push_thread_default()
  │                                           ├─ [D] g_bus_get_sync() → connection
  │                                           │      (if FAIL: set init_failed, signal ready, goto cleanup)
  │                                           ├─ [E] g_dbus_connection_signal_subscribe(
  │                                           │        "CheckForUpdateComplete",
  │                                           │        handler = on_check_signal_handler,
  │                                           │        user_data = ctx)
  │                                           ├─ [F] g_dbus_connection_call(
  │                                           │        "CheckForUpdate", handle)
  │                                           │      ← D-Bus request sent from worker thread
  │                                           ├─ [G] Add 120s timeout to GMainContext
  │                                           ├─ [H] Signal ready: ctx->is_ready = true
  │                                           │      pthread_cond_signal()
  │                                           │
  ├─ [8] pthread_cond_wait(ctx->ready_cond)  │
  │       ← NO TIMEOUT on this wait          │
  │       (see Section 5.1 for rationale)    │
  │                                           │
  │       ← wakes up when worker signals     ├─ [I] g_main_loop_run()
  │                                           │      ← BLOCKS until signal or 120s timeout
  ├─ [9] Check ctx->init_failed              │
  │       If true → return CHECK_FOR_UPDATE_FAIL
  │       (worker thread cleans itself up)   │
  │                                           │  ... daemon does XConf query (5s - 2min+) ...
  ├─ [10] Return CHECK_FOR_UPDATE_SUCCESS    │
  │       ← CALLER IS FREE                  │
  │                                           ├─ [J] Signal arrives from daemon
                                              │      on_check_signal_handler(ctx):
                                              │        parse GVariant → FwInfoData
                                              │        ctx->callback(&fwinfo_data)
                                              │        g_main_loop_quit()
                                              │
                                              ├─ [K] g_main_loop_run() returns
                                              ├─ [L] Cleanup:
                                              │      unsubscribe signal
                                              │      g_object_unref(connection)
                                              │      g_main_context_pop_thread_default()
                                              │      g_main_loop_unref()
                                              │      g_main_context_unref()
                                              │      untrack from active list
                                              │      Set g_check_in_progress = false
                                              │      free(ctx->handle_key)
                                              │      destroy ready_mutex, ready_cond
                                              │      free(ctx)
                                              └─ [M] return NULL  ← thread exits

Library unload (__attribute__((destructor)))
  └─► rdkFwupdateMgr_lib_deinit()
        ├─ internal_cancel_all_active_check_threads()
        │    ├─ For each active ctx: g_main_loop_quit()
        │    └─ For each active ctx: pthread_join()
        └─ internal_system_deinit()  ← for Download/Update cleanup
```

---

## 5. Design Decisions & Rationale

### 5.1 DECIDED: No timeout on the condvar wait in checkForUpdate()

**Question raised:** "If the worker thread takes >5 seconds to reach the ready signal,
`pthread_cond_timedwait()` returns `ETIMEDOUT`."

**Clarification:** There are **two different waits** to reason about:

| Wait | What it waits for | How long? | Timeout? |
|------|-------------------|-----------|----------|
| **Wait #1** — in `checkForUpdate()` (caller thread) | Worker thread to start up, connect D-Bus, subscribe, send request, and signal "ready" | Typically <100ms (D-Bus connect + subscribe + call) | **NO TIMEOUT** |
| **Wait #2** — in worker thread (`g_main_loop_run()`) | Daemon to emit `CheckForUpdateComplete` signal after XConf query | 5 seconds to 2+ minutes | **120 second timeout** |

**Wait #1 is NOT waiting for the daemon.** It is only waiting for the worker thread
to set up its GLib event loop and fire the D-Bus call. This is a purely local
operation (~10-100ms). If D-Bus itself is completely dead, `g_bus_get_sync()` will
fail and the worker will signal `init_failed = true`. So Wait #1 does not need a
timeout.

**Wait #2 IS waiting for the daemon** (XConf query). This is where the daemon can
take 2+ minutes. The 120-second timeout on the GMainLoop protects against the
daemon never responding. But the caller never experiences this wait — the caller
already returned `SUCCESS` at step [10].

**Decision:** `checkForUpdate()` uses `pthread_cond_wait()` (**no timeout**) for Wait #1.
The worker thread uses a 120-second `g_timeout_source` for Wait #2.

**What if D-Bus is extremely slow but not dead?** `g_bus_get_sync()` has its own
internal timeout (GLib default: 25 seconds). If it takes that long, the worker
thread is stuck at step [D] for 25 seconds, and the caller is stuck at step [8]
for 25 seconds. This is the worst case for Wait #1.

**Is 25 seconds acceptable for Wait #1?** On an embedded STB, if D-Bus itself is
unresponsive for 25 seconds, the system has bigger problems. The caller blocking
for 25 seconds is acceptable in this extreme scenario. If we wanted to cap it,
we could use a 10-second `pthread_cond_timedwait()`, but the failure handling
gets complex (see next section).

**Final decision: Use plain `pthread_cond_wait()` (no timeout) for Wait #1.**
Rationale: simpler, avoids the complex failure/cancellation path, and the
scenario where this blocks for more than ~100ms is extremely rare.

### 5.2 DECIDED: No timeout cancellation complexity

**Question raised:** "If the caller returned FAIL due to timeout, but the worker
eventually succeeds and fires the callback — is that acceptable?"

**This question is now MOOT** because we decided NOT to timeout Wait #1. The caller
will always wait until the worker signals ready. The worker either:

- Succeeds → signals `is_ready = true`, `init_failed = false` → caller returns SUCCESS
- Fails (D-Bus error) → signals `is_ready = true`, `init_failed = true` → caller returns FAIL

There is no scenario where the caller returns FAIL but the worker later fires the
callback. The only way the caller returns FAIL is if the worker itself failed to
initialize, in which case the worker goes directly to cleanup and never fires any
callback.

**Result:** No need for a `cancelled` flag. No ghost callbacks. No ambiguity. ✅

### 5.3 DECIDED: Reject duplicate checkForUpdate() calls from the same process

**Question raised:** "Worker thread A and worker thread B (if two `checkForUpdate()`
calls are made from the same process) share the same underlying D-Bus connection —
we should actually stop app from making such multiple requests."

**Agreed.** A single client process should not have two concurrent `checkForUpdate()`
requests in flight. The reasons:

1. **Daemon side:** The daemon does one XConf query and broadcasts one signal.
   Two concurrent requests from the same process would create two threads both
   listening for the same signal, both firing the same callback with the same data.
   This is wasteful and confusing for the client.

2. **Resource waste:** Two threads, two GMainContexts, two signal subscriptions
   for identical data.

3. **Client confusion:** If the client gets two callbacks, it may double-process
   the firmware info.

**Implementation:** Add a process-global flag `g_check_in_progress` (protected by a
mutex) that is set to `true` when `checkForUpdate()` spawns a worker, and reset to
`false` when the worker exits (after callback or timeout).

```c
/* In rdkFwupdateMgr_async.c */
static pthread_mutex_t g_check_in_progress_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool g_check_in_progress = false;

/* In checkForUpdate(): */
pthread_mutex_lock(&g_check_in_progress_mutex);
if (g_check_in_progress) {
    pthread_mutex_unlock(&g_check_in_progress_mutex);
    FWUPMGR_WARN("checkForUpdate: already in progress, rejecting\n");
    return CHECK_FOR_UPDATE_FAIL;
}
g_check_in_progress = true;
pthread_mutex_unlock(&g_check_in_progress_mutex);

/* In worker thread cleanup: */
pthread_mutex_lock(&g_check_in_progress_mutex);
g_check_in_progress = false;
pthread_mutex_unlock(&g_check_in_progress_mutex);
```

### 5.4 ~~DECIDED: Do NOT block unregisterProcess() during active checkForUpdate()~~

### 5.4 REVISED: BLOCK unregisterProcess() during active checkForUpdate()

> **History:** The original decision (v1.0) was to keep `unregisterProcess()`
> completely independent. After deeper system-level analysis, this was reversed.
> The original rationale is preserved below (struck through) for audit trail,
> followed by the revised decision.

**Original question:** "Do you think we should stop the app from calling unregister
until the current checkForUpdate is completed?"

#### ~~Original Decision (v1.0): Do NOT block~~ — SUPERSEDED

~~Rationale was: (1) unregisterProcess() is stateless, (2) blocking could cause
2-minute hangs on SIGTERM, (3) worker has its own strdup'd handle so no UAF,
(4) callback function pointers stay valid. While these technical observations
are true, they miss the architectural point.~~

#### Revised Decision (v1.1): BLOCK unregisterProcess() — Return failure if checkForUpdate is active

**The fundamental insight:** `registerProcess()` and `unregisterProcess()` represent
a **session** between the client and the daemon, not just memory allocation/deallocation.

| API Call | Semantic Meaning |
|----------|-----------------|
| `registerProcess()` | "I am a client. I exist. I want to interact with you." |
| `checkForUpdate()` | "Within my active session, check firmware and tell me when done." |
| `unregisterProcess()` | "I'm done. Forget about me. I will not interact further." |

**Calling `unregisterProcess()` while `checkForUpdate()` is in flight is a semantic
contradiction.** The app is saying "forget about me" while simultaneously expecting
"tell me when you're done." This is like hanging up the phone and expecting to hear
the answer.

**What happens on the daemon side if this is allowed:**
- The daemon receives `UnregisterProcess(handler_id)` and removes the client from
  its internal tracking.
- The daemon may or may not still emit the `CheckForUpdateComplete` signal (the
  XConf query may already be in flight and can't be cancelled).
- The relationship is logically severed. The signal might arrive, might not.
  The data might reference a handle the daemon no longer recognizes.
- This is **undefined territory** — exactly what good API design prevents.

**The universal pattern:** You cannot end a session while you have outstanding
operations. This principle appears everywhere in systems programming:
- You can't `close()` a file descriptor while an `aio_read()` is pending (UB)
- You can't destroy a socket while an async `recv()` is in flight
- You can't `dlclose()` a library while its threads are still running
- You can't `CloseHandle()` on a Windows IOCP while completion packets are pending

**Implementation:** `unregisterProcess()` will check the process-global
`g_check_in_progress` flag and **reject the call** (not block/wait):

```c
/* In unregisterProcess(), before any D-Bus call: */
#include "rdkFwupdateMgr_async_internal.h"  /* for internal_is_check_in_progress() */

if (internal_is_check_in_progress()) {
    FWUPMGR_ERROR("unregisterProcess: cannot unregister while "
                  "checkForUpdate() is in progress. Wait for the "
                  "callback to fire, then unregister.\n");
    return;  /* Do NOT free(handler) — caller still owns it */
}
/* ... proceed with normal unregistration ... */
```

**Critical detail — reject, don't block:** We return immediately with a logged error
rather than blocking. If we blocked (`pthread_cond_wait` on the worker to finish),
we'd risk a 2-minute hang during SIGTERM. By rejecting, we give the app clear
feedback: "Your call sequence is wrong. Fix it."

**The correct app sequence:**
```c
registerProcess() → checkForUpdate() → [wait for callback] → unregisterProcess()
```

**If the app receives SIGTERM during a check:**
1. **Best:** Wait for the callback (120s max), then unregister. The callback has a
   bounded timeout, so the app will not hang forever.
2. **Acceptable:** Just `exit()`. The daemon will detect the D-Bus peer disconnect
   and clean up the registration automatically. No resource leak.
3. **Future enhancement:** Add a `cancelCheckForUpdate()` API that cleanly tears
   down the worker thread, then the app can unregister.

**Note on `void` return type:** The current `unregisterProcess()` signature returns
`void`. We cannot return an error code without an API break. Options:
- **Option A (recommended for Phase 1):** Log a loud error and return without
  doing anything. The caller still holds a valid handle and can retry after
  the callback fires. This is a **logical no-op** when check is in progress.
- **Option B (Phase 2 API update):** Change return type to `UnregisterResult`
  enum. This is an API break but a cleaner contract.

**Why the original "don't block" decision was wrong:**
The original reasoning was technically correct (no memory corruption, no crashes)
but architecturally wrong. Just because something doesn't crash doesn't mean it
should be allowed. Allowing `unregisterProcess()` during an active check creates
an **undefined state** in the daemon-client relationship. Good API design makes
illegal states unrepresentable — or at minimum, rejects them at the call site.

**Summary:** `unregisterProcess()` now validates session state before proceeding.
If a `checkForUpdate()` is in progress, the call is rejected with a log message.
The caller must wait for the callback before unregistering.

### 5.5 DECIDED: Persistent thread stays for Download/Update (Phase 1)

**Decision:** In this phase, `internal_system_init()` is still called from the
library constructor. The persistent background thread still runs. But it is
**modified** to only subscribe to `DownloadProgress` and `UpdateProgress` — the
`CheckForUpdateComplete` subscription is **removed** from it.

**Why not leave the old CheckForUpdate subscription and let it be "harmless"?**

Because that would be dead code. The old `on_check_complete_signal()` handler
would fire, call `dispatch_all_pending()`, find zero entries, and return. This
wastes CPU cycles parsing the GVariant for nothing. More importantly:

- It makes the codebase confusing (two handlers for the same signal)
- It makes debugging harder (signal appears to be handled twice in logs)
- It violates the principle of removing dead code

**Clean approach:** Remove the `CheckForUpdateComplete` subscription from the
persistent thread, and remove all CheckForUpdate registry code. See Section 10.

---

## 6. Multi-Client Scenario Walkthrough

### Scenario: Process A and Process B both call checkForUpdate()

**Important:** A and B are **separate OS processes**. Each has its own copy of
`librdkFwupdateMgr.so` loaded. They share **nothing** in memory. The only shared
channel is the D-Bus system bus.

```
PROCESS A                     D-BUS SYSTEM BUS                    PROCESS B
─────────                     ──────────────────                   ─────────

registerProcess("AppA")       ──────► Daemon assigns ID=1
handle_A = "1"                ◄──────                              registerProcess("AppB")
                                      Daemon assigns ID=2 ◄────── 
                                                           ──────► handle_B = "2"

checkForUpdate("1", cbA)                                           checkForUpdate("2", cbB)
├─ Validate ✓                                                      ├─ Validate ✓
├─ g_check_in_progress=true                                        ├─ g_check_in_progress=true
├─ Alloc ctx_A                                                     ├─ Alloc ctx_B
├─ spawn worker_A                                                  ├─ spawn worker_B
│                                                                   │
│  worker_A:                                                        │  worker_B:
│  ├─ subscribe(Complete)                                           │  ├─ subscribe(Complete)
│  ├─ call(CheckForUpdate,"1")  ───►  Daemon receives "1"          │  ├─ call(CheckForUpdate,"2")
│  ├─ signal ready                    Daemon receives "2"   ◄───   │  ├─ signal ready
│  └─ g_main_loop_run()                                            │  └─ g_main_loop_run()
│                                                                   │
├─ condvar wakes up                                                 ├─ condvar wakes up
├─ Return SUCCESS                                                   ├─ Return SUCCESS
│                                                                   │
│  App A does other work              Daemon queries XConf...       │  App B does other work
│                                     ... 5-30 seconds ...          │
│                                                                   │
│                                     Daemon emits signal           │
│                                     (BROADCAST, dest=NULL,        │
│                                      handler_id=1,                │
│                                      firmware data)               │
│                                         │                         │
│  worker_A receives signal ◄─────────────┤──────────────────────► worker_B receives signal
│  ├─ Parse GVariant                      │                         ├─ Parse GVariant
│  ├─ Build FwInfoData                                              ├─ Build FwInfoData
│  ├─ cbA(&fwinfo_data)                                             ├─ cbB(&fwinfo_data)
│  ├─ g_main_loop_quit()                                            ├─ g_main_loop_quit()
│  ├─ cleanup                                                       ├─ cleanup
│  ├─ g_check_in_progress=false                                     ├─ g_check_in_progress=false
│  └─ thread exits                                                  └─ thread exits
│                                                                   │
│  App A's callback data ready          App B's callback data ready │
```

**Why both receive the signal:** D-Bus broadcast signals (destination=NULL) are
delivered to **every connection** on the system bus that has a matching subscription.
Process A and Process B have separate D-Bus connections (separate socket FDs).
Both subscribed to `CheckForUpdateComplete`. Both receive it.

**Why both should fire their callbacks:** The daemon queries XConf once and
broadcasts the result. The firmware data (available version, download URL, etc.)
is **the same for the device** regardless of which client asked. Both A and B
want the same answer. So both callbacks firing with the same data is **correct
behavior**.

**The handler_id in the signal** (`handler_id=1` from the first requester) is
present in the GVariant payload. In this design, we do NOT filter by handler_id.
Both worker threads fire their callbacks regardless of which handler_id is in the
signal. This is correct because:

1. XConf response is device-global, not client-specific
2. The daemon may batch requests (one XConf query for multiple clients)
3. The handler_id in the signal is the first requester's ID, not a per-client field

---

## 7. Thread Lifecycle & Memory Ownership

### 7.1 Complete lifecycle diagram

```
                    HEAP
                    ┌─────────────────────────────────────┐
CALLER THREAD       │  CheckRequestContext *ctx            │  WORKER THREAD
─────────────       │                                     │  ─────────────
                    │  handle_key ──► strdup("1")         │
calloc(ctx) ───────►│  callback ──► cbA                   │
                    │  ready_mutex, ready_cond             │
                    │  is_ready = false                    │
                    │  init_failed = false                 │
pthread_create() ──►│  thread ──► worker thread ID        │◄── thread starts
                    │                                     │
cond_wait()         │  (worker sets up GLib, D-Bus...)    │  g_main_context_new()
  │ blocked         │                                     │  g_bus_get_sync()
  │                 │  is_ready = true ◄──────────────────│  subscribe + call
  │ wakes up ◄──────│  cond_signal()                      │  g_main_loop_run()
  │                 │                                     │    │ blocked
reads init_failed   │                                     │    │
  │                 │      OWNERSHIP WALL                 │    │
  │                 │      ═══════════════                 │    │
  ▼                 │   Caller NEVER touches ctx again    │    │
return SUCCESS      │                                     │    │
                    │                                     │    ▼ signal arrives
                    │                                     │  callback fires
                    │                                     │  g_main_loop_quit()
                    │                                     │
                    │  free(handle_key) ◄──────────────────│  cleanup
                    │  destroy mutex, cond ◄──────────────│
                    └─────────────────────────────────────┘
                    free(ctx) ◄────────────────────────────│  thread exits
```

### 7.2 Memory ownership rules

| Memory | Allocated by | Owned by | Freed by |
|--------|-------------|----------|----------|
| `ctx` itself | Caller (`calloc`) | Worker thread (after condvar handshake) | Worker thread (`free`) |
| `ctx->handle_key` | Caller (`strdup`) | Worker thread | Worker thread (`free`) |
| `ctx->callback` | N/A (function pointer, not heap memory) | N/A | N/A |
| `ctx->ready_mutex` | Caller (`pthread_mutex_init`) | Worker thread | Worker thread (`pthread_mutex_destroy`) |
| `ctx->ready_cond` | Caller (`pthread_cond_init`) | Worker thread | Worker thread (`pthread_cond_destroy`) |
| `ctx->context` (GMainContext) | Worker thread | Worker thread | Worker thread (`g_main_context_unref`) |
| `ctx->main_loop` (GMainLoop) | Worker thread | Worker thread | Worker thread (`g_main_loop_unref`) |
| `ctx->connection` (GDBusConnection) | Worker thread (via GLib singleton) | GLib | Worker thread (`g_object_unref`) |

**No double-free risk:** Every allocation has exactly one owner and one free point.

**No use-after-free risk:** After the condvar handshake, the caller never touches
`ctx`. The worker is the sole accessor.

---

## 8. Thread Safety Proof

### 8.1 Shared mutable state inventory

| State | Accessed by | Protection |
|-------|------------|------------|
| `ctx->is_ready`, `ctx->init_failed` | Caller (read), Worker (write) | `ctx->ready_mutex` + `ctx->ready_cond` |
| `g_check_in_progress` | Caller (read/write), Worker (write) | `g_check_in_progress_mutex` |
| `g_active_check_ctx` | Caller (write), Worker (write), Destructor (read/write) | `g_check_in_progress_mutex` (reuse same mutex) |

**That's it.** Only 3 pieces of shared mutable state, all mutex-protected.

Compare with current design: `g_registry` (30-entry array + mutex), `g_bg_thread`
(multiple fields + mutex) — significantly more shared state.

### 8.2 Condvar handshake correctness

```c
// CALLER:
pthread_mutex_lock(&ctx->ready_mutex);
while (!ctx->is_ready) {
    pthread_cond_wait(&ctx->ready_cond, &ctx->ready_mutex);
}
bool failed = ctx->init_failed;
pthread_mutex_unlock(&ctx->ready_mutex);

// WORKER:
pthread_mutex_lock(&ctx->ready_mutex);
ctx->init_failed = false;   // or true on error
ctx->is_ready = true;
pthread_cond_signal(&ctx->ready_cond);
pthread_mutex_unlock(&ctx->ready_mutex);
```

This is the textbook condvar pattern. Safe against:

- **Spurious wakeup:** `while (!ctx->is_ready)` re-checks the predicate.
- **Missed signal:** If worker signals before caller enters `pthread_cond_wait`,
  the `while` loop checks `is_ready` which is already `true`, so the wait is
  skipped entirely.
- **Data race:** Both `is_ready` and `init_failed` are read/written under the
  same mutex.

### 8.3 g_check_in_progress flag correctness

```c
// Entry (in checkForUpdate):
pthread_mutex_lock(&g_check_in_progress_mutex);
if (g_check_in_progress) {
    pthread_mutex_unlock(&g_check_in_progress_mutex);
    return CHECK_FOR_UPDATE_FAIL;   // reject duplicate
}
g_check_in_progress = true;
pthread_mutex_unlock(&g_check_in_progress_mutex);

// Exit (in worker thread cleanup, ALWAYS reached):
pthread_mutex_lock(&g_check_in_progress_mutex);
g_check_in_progress = false;
pthread_mutex_unlock(&g_check_in_progress_mutex);
```

This guarantees:
- At most one worker thread exists at any time per process.
- The flag is always reset, even on error/timeout paths.
- No race between two rapid `checkForUpdate()` calls.

---

## 9. Edge Cases & Robustness

### 9.1 Client calls checkForUpdate() twice quickly

```c
checkForUpdate("1", cbA);  // → SUCCESS, worker spawned
checkForUpdate("1", cbA);  // → FAIL, "already in progress"
```

**Behavior:** Second call returns `CHECK_FOR_UPDATE_FAIL` immediately with a log
message. No second thread is spawned. ✅

### 9.2 Client calls unregisterProcess() while check is pending

**Behavior (revised v1.1):** `unregisterProcess()` checks `internal_is_check_in_progress()`
and **rejects the call** with a loud `FWUPMGR_ERROR` log message. The handle is NOT
freed. The caller still owns it and must retry `unregisterProcess()` after the
`checkForUpdate()` callback fires.

```c
checkForUpdate("12345", myCallback);   // → SUCCESS, worker spawned
unregisterProcess(handle);             // → REJECTED (logged), handle NOT freed
// ... callback fires with FwInfoData ...
unregisterProcess(handle);             // → SUCCESS, handle freed
```

**Rationale:** See §5.4. Unregistering during an active operation creates an
undefined daemon-client state. The API enforces the correct sequencing. ✅

### 9.9 App receives SIGTERM while checkForUpdate() is pending

**Scenario:** The app's `checkForUpdate()` returned SUCCESS. The callback hasn't
fired yet. The app receives SIGTERM and wants to exit.

**Options for the app:**

1. **Wait for callback, then exit (recommended):** The callback is bounded by the
   120-second worker timeout. The app can install a SIGTERM handler that sets a
   "shutting_down" flag. When the callback fires, the app checks the flag, calls
   `unregisterProcess()`, and exits. Worst case: 120 seconds.

2. **Just exit immediately (acceptable):** Call `_exit()` or `exit()`. The library
   destructor (`__attribute__((destructor))`) will join the worker thread (via
   `internal_cancel_all_active_check_threads()`). The daemon will detect the
   D-Bus peer disconnect and clean up the registration automatically. No resource
   leak on the daemon side.

3. **Force-skip unregisterProcess() (acceptable):** The daemon is designed to
   handle client disappearance gracefully. Orphaned registrations are cleaned up
   when the D-Bus connection drops. The only "leak" is the handle's 32 bytes of
   heap memory, which the OS reclaims on process exit.

**What the app should NOT do:**
```c
// WRONG: unregisterProcess() will be rejected
signal_handler(SIGTERM) {
    unregisterProcess(handle);  // REJECTED — check still in progress!
    exit(0);                    // handle leaked (not freed)
}
```

**Future enhancement:** A `cancelCheckForUpdate()` API would allow the app to:
```c
cancelCheckForUpdate();        // Worker thread is torn down
unregisterProcess(handle);     // Now succeeds
exit(0);
```
This is deferred to a future phase. ✅

### 9.3 Daemon crashes/restarts while check is pending

**Behavior:** The D-Bus subscription becomes orphaned. The 120-second timeout
fires. `g_main_loop_quit()` is called. Worker exits cleanly. No crash. ✅

### 9.4 Library unloaded (dlclose) while worker thread is active

**Behavior:** `__attribute__((destructor))` calls
`internal_cancel_all_active_check_threads()`:

1. Calls `g_main_loop_quit(ctx->main_loop)` on the active worker (if any).
2. Calls `pthread_join(ctx->thread, NULL)` to wait for worker to exit.
3. Library code is not unmapped until `pthread_join()` returns.

**No crash.** No code executing in unmapped memory. ✅

### 9.5 Signal arrives after timeout already fired

**Timeline:**
```
T=0s    Worker starts, subscribes, sends D-Bus call
T=120s  Timeout fires → g_main_loop_quit()
T=120s  Worker enters cleanup, unsubscribes signal
T=121s  Daemon finally emits signal
```

At T=121s, the signal arrives but the subscription is already removed (step
`g_dbus_connection_signal_unsubscribe()` at cleanup). GLib does not deliver
the signal. No crash. No dangling callback. ✅

### 9.6 Signal arrives between g_main_loop_quit() and unsubscribe

**Timeline:**
```
T=120.000s  Timeout fires → g_main_loop_quit()
T=120.001s  Signal arrives (queued in GMainContext)
T=120.002s  g_main_loop_run() returns (loop is quit)
T=120.003s  Worker calls g_dbus_connection_signal_unsubscribe()
```

At T=120.001s, the signal is queued but `g_main_loop_run()` is already returning.
The handler does NOT fire because the loop has exited. `g_dbus_connection_signal_unsubscribe()`
at T=120.003s cleans up the subscription. No crash. ✅

### 9.7 Worker thread D-Bus connection fails

**Behavior:** `g_bus_get_sync()` returns NULL. Worker sets `init_failed = true`,
signals ready via condvar, goes to cleanup, frees ctx, exits. Caller sees
`init_failed = true`, returns `CHECK_FOR_UPDATE_FAIL`. No thread leak. ✅

### 9.8 Worker thread signal subscribe fails

**Behavior:** `g_dbus_connection_signal_subscribe()` returns 0 on failure. The
worker should check this, set `init_failed = true`, signal ready, and go to
cleanup. The D-Bus method call is NOT sent (preventing a request with no listener). ✅

---

## 10. Dead Code Removal Plan

### 10.1 What to remove from `rdkFwupdateMgr_async_internal.h`

| Item | Action | Reason |
|------|--------|--------|
| `CallbackEntryState` enum | **REMOVE** | Only used by CheckForUpdate registry |
| `CallbackEntry` struct | **REMOVE** | CheckForUpdate registry entry — replaced by per-request ctx |
| `CallbackRegistry` struct | **REMOVE** | Global registry — no longer needed |
| `BackgroundThread.subscription_id` | **KEEP** (but this field is reused for Download/Update subscriptions) | Still needed for DownloadProgress/UpdateProgress |
| `internal_register_callback()` declaration | **REMOVE** | No registry to register in |
| `internal_system_init()` declaration | **KEEP** | Still initializes Download/Update registries and BG thread |
| `internal_system_deinit()` declaration | **KEEP** | Still cleans up Download/Update |
| `MAX_PENDING_CALLBACKS` | **KEEP** | Still used by Download/Update registries |
| `CALLBACK_TIMEOUT_SECONDS` | **REMOVE** | Was never used. New design has explicit 120s timeout. |

### 10.2 What to remove from `rdkFwupdateMgr_async.c`

| Item | Action | Reason |
|------|--------|--------|
| `static CallbackRegistry g_registry;` | **REMOVE** | No global registry |
| `on_check_complete_signal()` function | **REMOVE** | Old BG thread signal handler for CheckForUpdate |
| `dispatch_all_pending()` function | **REMOVE** | Old broadcast dispatch — replaced by direct callback in worker |
| `internal_register_callback()` function | **REMOVE** | No registry |
| `registry_reset_slot()` function | **REMOVE** | No registry slots |
| `g_registry` cleanup in `internal_system_deinit()` | **REMOVE** | No `g_registry` to clean up |
| `g_registry` init in `internal_system_init()` | **REMOVE** | No `g_registry` to init |
| `CheckForUpdateComplete` subscription in `background_thread_func()` | **REMOVE** | BG thread no longer handles CheckForUpdate signals |

### 10.3 What to remove from `rdkFwupdateMgr_api.c`

| Item | Action | Reason |
|------|--------|--------|
| Old `checkForUpdate()` body | **REPLACE** with new on-demand implementation | Core change |

### 10.4 What to keep

**Everything related to Download and Update is UNTOUCHED:**

- `g_dwnl_registry`, `g_update_registry` — kept
- `on_download_progress_signal()` — kept
- `on_update_progress_signal()` — kept
- `dispatch_all_dwnl_active()` — kept
- `dispatch_all_update_active()` — kept
- `internal_dwnl_register_callback()` — kept
- `internal_update_register_callback()` — kept
- `internal_dwnl_system_deinit()` — kept
- `internal_update_system_deinit()` — kept
- `background_thread_func()` — kept (but removes CheckForUpdateComplete subscription)
- `internal_system_init()` — kept (but removes g_registry init)
- `internal_system_deinit()` — kept (but removes g_registry cleanup)

**Helper functions kept (shared with new handler):**

- `internal_parse_signal_data()` — reused by new `on_check_signal_handler()`
- `internal_cleanup_signal_data()` — reused
- `internal_map_status_code()` — reused
- `parse_update_details()` — reused

---

## 11. File-by-File Change Specification

### 11.1 `rdkFwupdateMgr_client.h` — NO CHANGES

Public API unchanged. Zero breakage.

### 11.2 `rdkFwupdateMgr_async_internal.h`

**Removals:**
- `CallbackEntryState` enum
- `CallbackEntry` struct
- `CallbackRegistry` struct
- `CALLBACK_TIMEOUT_SECONDS` define
- `internal_register_callback()` declaration

**Additions:**
```c
/* Timeout for worker thread waiting for daemon signal (seconds) */
#define CHECK_SIGNAL_TIMEOUT_SECONDS 120

/**
 * Per-request context for on-demand CheckForUpdate worker thread.
 *
 * Lifecycle:
 *   - Allocated in checkForUpdate() (caller thread)
 *   - Ownership transferred to worker thread after condvar handshake
 *   - Freed by worker thread after callback fires (or timeout)
 *
 * Memory: ~100 bytes (excluding GLib objects)
 */
typedef struct {
    /* Condvar handshake: worker signals "I'm ready" to caller */
    pthread_mutex_t   ready_mutex;
    pthread_cond_t    ready_cond;
    bool              is_ready;       /**< true = worker finished setup      */
    bool              init_failed;    /**< true = D-Bus connect failed       */

    /* GLib event loop (isolated, per-thread) */
    GMainContext     *context;
    GMainLoop        *main_loop;
    GDBusConnection  *connection;
    guint             subscription_id;

    /* Request data */
    char             *handle_key;     /**< strdup of FirmwareInterfaceHandle */
    UpdateEventCallback callback;     /**< Client's callback function ptr    */

    /* Thread handle (for join in destructor) */
    pthread_t         thread;
} CheckRequestContext;

/**
 * Worker thread entry point for on-demand CheckForUpdate.
 * @param arg  CheckRequestContext* (ownership transferred)
 * @return NULL
 */
void *internal_check_worker_thread(void *arg);
```

**No changes to:**
- `InternalSignalData` struct
- `internal_parse_signal_data()` / `internal_cleanup_signal_data()` / `internal_map_status_code()` declarations
- All Download types (`DwnlCallbackState`, `InternalDwnlSignalData`, `DwnlCallbackEntry`, `DwnlCallbackRegistry`)
- All Update types
- `BackgroundThread` struct (still used for Download/Update BG thread)
- `internal_system_init()` / `internal_system_deinit()` declarations

### 11.3 `rdkFwupdateMgr_api.c`

**Replace `checkForUpdate()` body entirely.** New implementation:

1. Validate handle and callback (same as today)
2. Check `g_check_in_progress` — reject if already active
3. Allocate `CheckRequestContext`, copy handle and callback
4. Track context for library-unload safety
5. `pthread_create()` worker thread
6. `pthread_cond_wait()` for worker to signal ready
7. If `init_failed` → return `CHECK_FOR_UPDATE_FAIL`
8. Return `CHECK_FOR_UPDATE_SUCCESS`

**Modify constructor:** Keep `internal_system_init()` call (for Download/Update).
Add init of `g_check_in_progress_mutex`.

**Modify destructor:** Add `internal_cancel_all_active_check_threads()` call
before `internal_system_deinit()`.

### 11.4 `rdkFwupdateMgr_async.c`

**Remove** (CheckForUpdate-specific old code):
- `static CallbackRegistry g_registry;`
- `g_registry` init in `internal_system_init()`
- `g_registry` cleanup in `internal_system_deinit()`
- `on_check_complete_signal()` function
- `dispatch_all_pending()` function
- `internal_register_callback()` function
- `registry_reset_slot()` function
- `CheckForUpdateComplete` subscription in `background_thread_func()`

**Add** (new on-demand CheckForUpdate code):

1. `static pthread_mutex_t g_check_in_progress_mutex;`
2. `static bool g_check_in_progress;`
3. `static CheckRequestContext *g_active_check_ctx;`
   (only one can be active at a time due to dedup, so a single pointer suffices)
4. `void *internal_check_worker_thread(void *arg)` — worker function
5. `static void on_check_signal_handler(...)` — signal handler (fires callback, quits loop)
6. `static gboolean on_check_timeout(gpointer user_data)` — timeout handler
7. `void internal_cancel_all_active_check_threads(void)` — for destructor

**No changes to:**
- All Download engine functions
- All Update engine functions
- `internal_parse_signal_data()`, `internal_cleanup_signal_data()`, `internal_map_status_code()`
- `parse_update_details()`
- `background_thread_func()` (except removing CheckForUpdateComplete subscription)
- `internal_system_init()` (except removing g_registry init)
- `internal_system_deinit()` (except removing g_registry cleanup)

### 11.5 `rdkFwupdateMgr_process.c` — MODIFIED (Session State Validation)

**Context:** `unregisterProcess()` must now validate that no `checkForUpdate()` is
in progress before proceeding. This introduces a dependency from `_process.c` to
the async engine's state, but through a clean, narrow API boundary.

**Changes:**

1. **Add include:** `#include "rdkFwupdateMgr_async_internal.h"` (for `internal_is_check_in_progress()`)

2. **Add guard at top of `unregisterProcess()` body** (before any NULL checks):
   ```c
   void unregisterProcess(FirmwareInterfaceHandle handler)
   {
       /* Session state validation: reject if checkForUpdate() is active */
       if (internal_is_check_in_progress()) {
           FWUPMGR_ERROR("unregisterProcess: REJECTED — checkForUpdate() is in "
                         "progress. Wait for the callback to fire, then retry "
                         "unregisterProcess().\n");
           /* Do NOT free(handler): caller still owns it and will need it later */
           return;
       }

       /* ... rest of existing function unchanged ... */
   }
   ```

3. **New function exposed by async engine** (in `rdkFwupdateMgr_async.c`):
   ```c
   bool internal_is_check_in_progress(void)
   {
       pthread_mutex_lock(&g_check_in_progress_mutex);
       bool result = g_check_in_progress;
       pthread_mutex_unlock(&g_check_in_progress_mutex);
       return result;
   }
   ```

4. **Declaration in `rdkFwupdateMgr_async_internal.h`:**
   ```c
   /**
    * @brief Query whether a checkForUpdate() operation is currently in progress.
    *
    * Used by unregisterProcess() to enforce the session-state invariant:
    * a client cannot unregister while it has outstanding operations.
    *
    * Thread-safe: protected by internal mutex.
    *
    * @return true if a checkForUpdate worker thread is active, false otherwise.
    */
   bool internal_is_check_in_progress(void);
   ```

**Design notes:**
- The coupling is minimal: one `bool` query function. `_process.c` has zero
  knowledge of mutexes, threads, or contexts.
- The function is `internal_*` prefixed (library-internal, not exported).
- If the async engine is not initialized (library in bad state), the mutex is
  statically initialized (`PTHREAD_MUTEX_INITIALIZER`), so the query is safe
  even if `internal_system_init()` hasn't been called.
- The `void` return type of `unregisterProcess()` means we can't return an error
  code. The rejection is signaled via a loud `FWUPMGR_ERROR` log. This is
  acceptable for Phase 1. A future API revision (Phase 2+) could add a return type.

### 11.6 `rdkFwupdateMgr_log.c` / `rdkFwupdateMgr_log.h` — NO CHANGES

### 11.7 `example_app.c` — NO CHANGES

The example app's callback runs in the worker thread (previously ran in the
persistent BG thread). The condvar signaling in the example works identically.

---

## 12. Unit Test Impact

### 12.1 Tests that need updating (CheckForUpdate-specific)

| Test File | Impact |
|-----------|--------|
| `rdkFwupdateMgr_async_cleanup_gtest.cpp` | **REWRITE** — references `rdkFwupdateMgr_async_init_for_test()`, `get_pending_count` (registry-based) |
| `rdkFwupdateMgr_async_refcount_gtest.cpp` | **REWRITE** — likely tests registry slot refcounting |
| `rdkFwupdateMgr_async_signal_gtest.cpp` | **REWRITE** — tests signal dispatch through registry |
| `rdkFwupdateMgr_async_stress_gtest.cpp` | **REWRITE** — uses `g_async_registry`, concurrent registration |
| `rdkFwupdateMgr_async_threadsafety_gtest.cpp` | **REWRITE** — concurrent registration/dispatch |

### 12.2 Tests that remain unchanged (Download/Update)

| Test File | Impact |
|-----------|--------|
| `dbus_handlers.cpp` | **UNCHANGED** — tests daemon-side handlers |
| `device_status_helper_gtest.cpp` | **UNCHANGED** |
| `fwdl_interface_gtest.cpp` | **UNCHANGED** |
| `basic_rdkv_main_gtest.cpp` | **UNCHANGED** |
| `rdkfwupdatemgr_main_flow_gtest.cpp` | **UNCHANGED** |
| `rdkFwupdateMgr_handlers_gtest.cpp` | **UNCHANGED** — tests daemon-side |
| `deviceutils/device_api_gtest.cpp` | **UNCHANGED** |
| `deviceutils/deviceutils_gtest.cpp` | **UNCHANGED** |

### 12.3 New tests needed

| Test | Description |
|------|-------------|
| `WorkerThread_StartsAndStops` | Verify thread is created on `checkForUpdate()` and exits after signal |
| `WorkerThread_FiresCallback` | Verify callback is invoked with correct FwInfoData |
| `WorkerThread_Timeout` | Verify thread exits cleanly after 120s with no signal |
| `WorkerThread_DBusFailure` | Verify `CHECK_FOR_UPDATE_FAIL` returned when D-Bus is unavailable |
| `DuplicateRequest_Rejected` | Verify second `checkForUpdate()` returns FAIL while first is active |
| `UnregisterDuringCheck_Rejected` | Verify `unregisterProcess()` is rejected (no-op) while `checkForUpdate()` is active. Handle is NOT freed. |
| `UnregisterAfterCallback_Succeeds` | Verify `unregisterProcess()` succeeds after callback fires and `g_check_in_progress` is cleared. |
| `LibraryUnloadDuringCheck` | Verify destructor joins active worker thread |
| `CallbackDataValidity` | Verify FwInfoData fields are correct (version, UpdateDetails, status) |
| `MultiProcess_BothReceiveSignal` | Integration test: two processes, both get callbacks |
| `SIGTERM_DuringCheck_ExitClean` | Verify that calling `exit()` during an active check does not crash or leak (destructor joins thread). |

---

## 13. Resource Cost Comparison

### 13.1 Memory comparison

| State | Current Design | New Design |
|-------|---------------|------------|
| Library loaded, no API calls | ~14KB (persistent thread + registries + D-Bus conn) | ~14KB* |
| Library loaded, never calls checkForUpdate() | ~14KB (same) | ~14KB* |
| One checkForUpdate() in progress | ~14KB (same) | ~14KB* + ~10KB (worker) = ~24KB |
| checkForUpdate() completed, idle | ~14KB (thread still alive) | ~14KB* (worker exited) |

*~14KB is for the persistent BG thread that still runs for Download/Update.
When Download/Update are also migrated to on-demand (Phase 2), this drops to ~0.

### 13.2 Per-request cost

| Resource | Size | Duration |
|----------|------|----------|
| `CheckRequestContext` | ~128 bytes | Request lifetime |
| pthread stack | ~8KB (default) | Request lifetime |
| GMainContext | ~1.5KB | Request lifetime |
| GMainLoop | ~200 bytes | Request lifetime |
| D-Bus signal subscription | ~100 bytes | Request lifetime |
| **Total** | **~10KB** | **5s to 2min (daemon response time)** |

All resources freed to zero after callback fires.

---

## 14. Migration Phases

### Phase 1 (This Document): CheckForUpdate on-demand thread

| Step | Task | Effort | Risk |
|------|------|--------|------|
| 1.1 | Add `CheckRequestContext` to `_async_internal.h` | 0.5h | Low |
| 1.2 | Remove CheckForUpdate registry types from `_async_internal.h` | 0.5h | Low |
| 1.3 | Implement `internal_check_worker_thread()` in `_async.c` | 2h | Medium |
| 1.4 | Implement signal handler, timeout handler in `_async.c` | 1h | Medium |
| 1.5 | Implement in-progress guard and active thread tracking in `_async.c` | 1h | Low |
| 1.6 | Remove old CheckForUpdate code from `_async.c` | 1h | Low |
| 1.7 | Remove CheckForUpdateComplete subscription from BG thread | 0.5h | Low |
| 1.8 | Remove g_registry init/cleanup from system_init/deinit | 0.5h | Low |
| 1.9 | Rewrite `checkForUpdate()` in `_api.c` | 1.5h | Medium |
| 1.10 | Update constructor/destructor in `_api.c` | 0.5h | Low |
| 1.11 | Update/rewrite unit tests | 3-4h | High |
| 1.12 | Integration testing (multi-process) | 2h | Medium |
| **Total** | | **~14h (2 days)** | |

### Phase 2 (Future): DownloadFirmware on-demand thread

Same pattern but with multi-fire callback (thread stays alive across
multiple `DownloadProgress` signals, exits on COMPLETED/ERROR).

### Phase 3 (Future): UpdateFirmware on-demand thread

Same pattern as Download.

### Phase 4 (Future): Remove persistent background thread entirely

After Download and Update are migrated, `internal_system_init()` and the
persistent BG thread can be removed entirely. Constructor becomes a true no-op.

---

## 15. Open Items & Future Work

### 15.1 Resolved in this document

| Item | Resolution |
|------|-----------|
| Timeout on condvar wait in checkForUpdate() | **No timeout.** Worker setup is fast (~100ms). Plain `pthread_cond_wait()`. |
| Caller returns FAIL but callback fires later | **Cannot happen.** No timeout means caller always waits for worker's answer. |
| Duplicate checkForUpdate() calls | **Rejected** with `CHECK_FOR_UPDATE_FAIL` and log message. |
| Block unregisterProcess() during check | **YES — REVISED (v1.1).** `unregisterProcess()` is rejected (returns immediately with error log) if `checkForUpdate()` is in progress. Caller must wait for callback, then unregister. Rationale: ending a session while operations are outstanding is a semantic contradiction and creates undefined daemon-client state. See §5.4 for full analysis. |
| Dead code in persistent BG thread | **Remove it.** Strip CheckForUpdateComplete subscription and all registry code. |
| handler_id routing in signal | **Not filtered.** Both processes receive broadcast and fire callbacks. This is correct because XConf data is device-global. |

### 15.2 Items for Phase 2+

| Item | Phase |
|------|-------|
| Add `cancelCheckForUpdate()` API for graceful in-flight cancellation | Phase 1.5 |
| Change `unregisterProcess()` return type to `UnregisterResult` enum | Phase 2 |
| Migrate downloadFirmware() to on-demand thread | Phase 2 |
| Migrate updateFirmware() to on-demand thread | Phase 3 |
| Remove persistent BG thread entirely | Phase 4 |
| Remove `internal_system_init()` / `internal_system_deinit()` | Phase 4 |
| Remove `BackgroundThread` struct | Phase 4 |
| Remove `DwnlCallbackRegistry` / `UpdateCbRegistry` | Phase 2-3 |
| Make library constructor a true no-op | Phase 4 |

### 15.3 Considerations for production hardening

| Item | Priority | Notes |
|------|----------|-------|
| Log rotation for worker thread logs | Medium | Each worker thread logs to same file — ensure thread-safe logging |
| Configurable timeout | Low | Currently hardcoded to 120s. Could be made configurable via env var or RFC. |
| D-Bus reconnection | Low | If D-Bus daemon restarts, `g_bus_get_sync()` should reconnect. GLib handles this internally for new connections. |
| Memory sanitizer validation | High | Run with AddressSanitizer/ThreadSanitizer to validate no leaks or races |
| Coverity scan | High | Current codebase uses Coverity. New code must pass. |

---

## Appendix A: D-Bus Signal Introspection Reference

```xml
<signal name='CheckForUpdateComplete'>
  <arg type='t' name='handlerId'/>         <!-- uint64: handler ID of first requester -->
  <arg type='i' name='result'/>            <!-- int32: API result (0=SUCCESS, 1=FAIL) -->
  <arg type='i' name='statusCode'/>        <!-- int32: firmware status (0-5) -->
  <arg type='s' name='currentVersion'/>    <!-- string: running firmware version -->
  <arg type='s' name='availableVersion'/>  <!-- string: available firmware version -->
  <arg type='s' name='updateDetails'/>     <!-- string: pipe-delimited metadata -->
  <arg type='s' name='statusMessage'/>     <!-- string: human-readable message -->
</signal>
```

GVariant signature: `(tiissss)`

Parsed by: `internal_parse_signal_data()` in `rdkFwupdateMgr_async.c`

---

## Appendix B: g_bus_get_sync() Singleton Behavior

`g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error)` returns a **process-wide
singleton** `GDBusConnection`. Multiple calls within the same process return the
same object with an incremented reference count.

**Implications:**

- Worker thread's `g_bus_get_sync()` shares the underlying socket FD with any
  other GLib code in the process (including the persistent BG thread for
  Download/Update).
- `g_object_unref()` in the worker's cleanup decrements the refcount but does NOT
  close the connection (other users still hold references).
- Signal subscriptions are per-context: the worker's subscription dispatches to
  the worker's `GMainContext`, even though the underlying connection is shared.
- Between separate processes (A and B), the connections are completely independent
  (separate socket FDs to the D-Bus daemon).

---

## Appendix C: Complete Ordering Proof

```
TIME    WORKER THREAD                           D-BUS DAEMON               FIRMWARE DAEMON
────    ─────────────                           ────────────               ───────────────

T1      g_main_context_new()
T2      g_main_loop_new()
T3      g_main_context_push_thread_default()
T4      g_bus_get_sync() → connection
T5      g_dbus_connection_signal_subscribe()    (subscription registered
         → subscription_id                       locally in GLib, no
                                                 round-trip to D-Bus daemon)

T6      g_dbus_connection_call(CheckForUpdate)  → message queued          → received
        (NOTE: subscribe at T5 is LOCAL.                                     XConf query starts
         The call at T6 goes over the wire.
         The subscription is guaranteed to be
         active before the call is sent because
         both use the same connection object
         and GLib processes them in order.)

T7      pthread_cond_signal(ready)
T8      g_main_loop_run()                       (waiting for events...)
        ↓ blocked in poll()

                                                                            XConf query done
                                                                            Build GVariant
T9                                              ← emit_signal(broadcast)
                                                → deliver to all subscribers

T10     poll() returns, GLib dispatches signal
T11     on_check_signal_handler() fires
T12     ctx->callback(&fwinfo_data)
T13     g_main_loop_quit()
T14     g_main_loop_run() returns
T15     g_dbus_connection_signal_unsubscribe()
T16     g_object_unref(connection)
T17     g_main_context_pop_thread_default()
T18     g_main_loop_unref()
T19     g_main_context_unref()
T20     free(ctx)
T21     return NULL  → thread exits

GUARANTEE: Signal at T5 is always registered before method call at T6.
           No signal can be missed.
```
