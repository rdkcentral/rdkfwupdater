<!-- filepath: docs/DESIGN_DOWNLOAD_FIRMWARE_ON_DEMAND_THREAD.md -->
# DownloadFirmware API — On-Demand Worker Thread Redesign

## Document Version

| Version | Date       | Author | Description                              |
|---------|------------|--------|------------------------------------------|
| 1.0     | 2026-03-24 | —      | Initial design, analysis, and migration plan for DownloadFirmware on-demand thread |

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Terminology & Clarifications](#2-terminology--clarifications)
3. [Current Architecture (Before)](#3-current-architecture-before)
4. [Proposed Architecture (After)](#4-proposed-architecture-after)
5. [Design Decisions & Rationale](#5-design-decisions--rationale)
6. [Multi-Client Scenario Walkthrough](#6-multi-client-scenario-walkthrough)
7. [Daemon Download Handler Deep Dive](#7-daemon-download-handler-deep-dive)
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

This document describes the redesign of the `downloadFirmware()` API implementation
within `librdkFwupdateMgr.so`. The change replaces the **persistent background
thread + registry** model with an **on-demand worker thread** model, consistent
with the CheckForUpdate redesign completed in Phase 1.

**Goals:**

- Zero resource cost when no download is in progress
- Thread exists only for the duration of one firmware download operation
- Consistent architecture with CheckForUpdate (on-demand thread model)
- Accurate daemon response reporting via `g_dbus_connection_call_sync()`
  instead of fire-and-forget
- No change to the public API (`rdkFwupdateMgr_client.h`)
- Correct multi-client behavior
- No memory leaks, no crashes, no dangling threads
- Clean dead code removal of the old Download registry

**Scope:** `downloadFirmware()` API only. `updateFirmware()` will be migrated
subsequently in Phase 3 using the same pattern.

**Prerequisite:** Phase 1 (CheckForUpdate on-demand thread) must be completed.

---

## 2. Terminology & Clarifications

### 2.1 Key Difference from CheckForUpdate

| Aspect | CheckForUpdate | DownloadFirmware |
|--------|---------------|-----------------|
| Signal fires | **Once** — then done | **Many times** — 0%, 25%, 50%, 75%, 100% |
| Thread lifetime | Short (~5–120s) | Long (~1–30 minutes) |
| Callback invocations | Exactly 1 (or 0 on timeout) | N times until COMPLETED/ERROR |
| Terminal condition | Any signal = done | `DWNL_COMPLETED` or `DWNL_ERROR` in signal payload |
| Daemon response model | Fire-and-forget method call | **Synchronous reply** — daemon returns accept/reject |
| Daemon concurrency | Multiple checks allowed | **Single download at a time** (daemon enforced) |

### 2.2 What is the `DownloadCallback`?

From `rdkFwupdateMgr_client.h`:
```c
typedef void (*DownloadCallback)(int percentage, DownloadStatus status);
```

Where `DownloadStatus` is:
```c
typedef enum {
    DWNL_COMPLETED = 0,   /* Download finished successfully */
    DWNL_IN_PROGRESS,     /* Download is ongoing (percentage is meaningful) */
    DWNL_ERROR,           /* Download failed */
} DownloadStatus;
```

The callback is fired **multiple times** during a download — once per progress
signal from the daemon. It is fired with `DWNL_IN_PROGRESS` and an increasing
percentage, and finally with `DWNL_COMPLETED` (100%) or `DWNL_ERROR`.

### 2.3 What is the `FwDownloadRequest`?

From `rdkFwupdateMgr_client.h`:
```c
typedef struct {
    char firmwareName[256];   /* Firmware filename (e.g. "RDKV_firmware_v2.1.bin") */
    char firmwareUrl[512];    /* Download URL */
    char rebootFlag[16];      /* "1" = reboot after download, "0" = don't */
} FwDownloadRequest;
```

### 2.4 What is the `FirmwareDownloadResult`?

From `rdkFwupdateMgr_client.h`:
```c
typedef enum {
    RDKFW_DWNL_SUCCESS = 0,  /* Firmware download initiated successfully */
    RDKFW_DWNL_FAILED,       /* Firmware download initiation failed */
} FirmwareDownloadResult;
```

**Critical note:** `RDKFW_DWNL_SUCCESS` means "the download request was accepted
and initiated." It does NOT mean the download has completed. Completion is
reported via the callback.

### 2.5 What is `DownloadRequestContext`?

This is the new per-request context structure (equivalent to `CheckRequestContext`
from Phase 1). It is heap-allocated in `downloadFirmware()`, ownership-transferred
to the worker thread, and freed by the worker thread after the download completes
or fails. Full definition in [Section 4.2](#42-downloadrequestcontext-structure).

---

## 3. Current Architecture (Before)

### 3.1 What happens today

```
Library load (__attribute__((constructor)))
  │
  └─► internal_system_init()
        ├─ Initialize g_dwnl_registry (30-slot DwnlCallbackEntry array + mutex)
        ├─ Create GMainContext + GMainLoop
        └─ pthread_create(background_thread_func)
              │
              ├─ Connect to D-Bus
              ├─ Subscribe to DownloadProgress signal
              ├─ Subscribe to UpdateProgress signal
              ├─ Signal ready (spin-wait)
              └─ g_main_loop_run()  ← BLOCKS FOREVER until library unload
                   │
                   │ (idle... idle... idle... for hours/days)
                   │
                   │ DownloadProgress signal arrives
                   │   → on_download_progress_signal()
                   │     → dispatch_all_dwnl_active()
                   │       → fires ALL ACTIVE callbacks (broadcast to ALL slots)
                   │       → if COMPLETED/ERROR: reset slot to IDLE
                   │
                   │ (idle again...)

downloadFirmware(handle, request, callback)
  ├─ Validate handle + request + callback
  ├─ Connect to D-Bus (from caller thread — SEPARATE connection from BG thread)
  ├─ internal_dwnl_register_callback(handle, callback) → puts in g_dwnl_registry[slot]
  │    └─ If handle already in ACTIVE slot → OVERWRITE (silent callback loss!)
  ├─ g_dbus_connection_call("DownloadFirmware") → fire-and-forget from caller thread
  │    └─ Daemon reply is IGNORED (fire-and-forget)
  └─ Return RDKFW_DWNL_SUCCESS (always, regardless of daemon response)

Library unload (__attribute__((destructor)))
  └─► internal_system_deinit()
        ├─ g_main_loop_quit() → background thread wakes up
        ├─ pthread_join() → wait for thread to exit
        ├─ internal_dwnl_system_deinit() → destroy registry mutex, free handle_keys
        └─ Free GLib objects
```

### 3.2 Problems with current design

| # | Problem | Impact |
|---|---------|--------|
| 1 | **Persistent idle thread** | Thread + D-Bus connection + GMainContext consume ~14KB even when no downloads are active |
| 2 | **Fire-and-forget D-Bus call** | Daemon may reject the download (`RDKFW_DWNL_FAILED`) but library returns `RDKFW_DWNL_SUCCESS` anyway. Caller gets a **lie**. |
| 3 | **Broadcast dispatch to ALL slots** | `dispatch_all_dwnl_active()` fires every ACTIVE callback regardless of which handler_id the signal is for. If two handles are active, both get each other's progress events. |
| 4 | **Silent callback overwrite** | If same handle calls `downloadFirmware()` twice while first is active, `internal_dwnl_register_callback()` overwrites the existing slot. First callback is silently lost. |
| 5 | **No timeout for stale slots** | If daemon crashes, registry slot stays ACTIVE forever. Handle string leaked. Slot never reusable. |
| 6 | **Two D-Bus connections** | Caller thread creates ad-hoc connection for fire-and-forget. BG thread has separate connection for signals. |
| 7 | **Design inconsistency** | CheckForUpdate now uses on-demand thread. Download still uses persistent BG thread + registry. Two mental models in same library. |
| 8 | **30-slot fixed registry** | `MAX_PENDING_CALLBACKS = 30` — arbitrary limit. On-demand thread needs zero pre-allocated slots. |
| 9 | **Constructor overhead** | BG thread and registry created at library load even if app never calls `downloadFirmware()`. |

---

## 4. Proposed Architecture (After)

### 4.1 New flow for downloadFirmware()

```
downloadFirmware(handle, request, callback)
  │
  ├─ [1] Validate handle (not NULL, not empty)
  ├─ [2] Validate request (not NULL, firmwareName not empty)
  ├─ [3] Validate callback (not NULL)
  ├─ [4] Check: is a downloadFirmware already in progress for this process?
  │       If YES → log warning, return RDKFW_DWNL_FAILED
  ├─ [5] Allocate DownloadRequestContext on heap
  │       ctx->handle_key = strdup(handle)
  │       ctx->firmware_name = strdup(request->firmwareName)
  │       ctx->firmware_url = strdup(request->firmwareUrl)
  │       ctx->reboot_flag = strdup(request->rebootFlag)
  │       ctx->callback = callback
  │       init ready_mutex, ready_cond
  ├─ [6] internal_begin_download(ctx)
  │       Sets g_dwnl_in_progress = true, g_active_dwnl_ctx = ctx
  ├─ [7] pthread_create(internal_download_worker_thread, ctx)
  │                                           │
  │                                           ├─ [A] g_main_context_new() (isolated)
  │                                           ├─ [B] g_main_loop_new()
  │                                           ├─ [C] g_main_context_push_thread_default()
  │                                           ├─ [D] g_bus_get_sync() → connection
  │                                           │      (if FAIL: set init_failed, signal ready, goto cleanup)
  │                                           │
  │                                           ├─ [E] g_dbus_connection_signal_subscribe(
  │                                           │        "DownloadProgress",
  │                                           │        handler = on_download_signal_handler,
  │                                           │        user_data = ctx)
  │                                           │
  │                                           ├─ [F] g_dbus_connection_call_sync(
  │                                           │        "DownloadFirmware",
  │                                           │        handle, firmwareName, firmwareUrl, rebootFlag)
  │                                           │      ← SYNCHRONOUS: waits for daemon reply
  │                                           │      ← Daemon replies (sss): result, status, message
  │                                           │
  │                                           │      IF daemon returned "RDKFW_DWNL_FAILED":
  │                                           │        set init_failed = true
  │                                           │        set daemon_reject_message = message
  │                                           │        signal ready
  │                                           │        goto cleanup
  │                                           │
  │                                           │      IF daemon returned "RDKFW_DWNL_SUCCESS":
  │                                           │        set daemon_accepted = true
  │                                           │
  │                                           ├─ [G] Add timeout to GMainContext
  │                                           │      (DWNL_SIGNAL_TIMEOUT_SECONDS = 3600s)
  │                                           │
  │                                           ├─ [H] Signal ready: ctx->is_ready = true
  │                                           │      pthread_cond_signal()
  │                                           │
  ├─ [8] pthread_cond_wait(ctx->ready_cond)  │
  │       ← waits for worker setup + daemon reply
  │                                           │
  │       ← wakes up when worker signals     ├─ [I] g_main_loop_run()
  │                                           │      ← BLOCKS, receiving DownloadProgress signals
  ├─ [9] Check ctx->init_failed              │
  │       If true:                            │
  │         If daemon_rejected:               │
  │           Log daemon's rejection message   │
  │         return RDKFW_DWNL_FAILED          │
  │       (worker thread cleans itself up)    │
  │                                           │  ... daemon downloads firmware (1-30 min) ...
  ├─ [10] Return RDKFW_DWNL_SUCCESS          │  ... emits DownloadProgress signals periodically ...
  │       ← CALLER IS FREE                   │
  │                                           │
  │                                           ├─ [J] DownloadProgress signal arrives (25%)
  │                                           │      on_download_signal_handler():
  │                                           │        parse → (percentage=25, status=INPROGRESS)
  │                                           │        ctx->callback(25, DWNL_IN_PROGRESS)
  │                                           │        (do NOT quit loop — more signals coming)
  │                                           │
  │                                           ├─ [K] DownloadProgress signal arrives (50%)
  │                                           │      ctx->callback(50, DWNL_IN_PROGRESS)
  │                                           │
  │                                           ├─ [L] DownloadProgress signal arrives (100%)
  │                                           │      on_download_signal_handler():
  │                                           │        parse → (percentage=100, status=COMPLETED)
  │                                           │        ctx->callback(100, DWNL_COMPLETED)
  │                                           │        g_main_loop_quit()  ← NOW we quit
  │                                           │
  │                                           ├─ [M] g_main_loop_run() returns
  │                                           ├─ [N] Cleanup:
  │                                           │      unsubscribe signal
  │                                           │      g_object_unref(connection)
  │                                           │      g_main_context_pop_thread_default()
  │                                           │      g_main_loop_unref()
  │                                           │      g_main_context_unref()
  │                                           │      internal_end_download()
  │                                           │      free(ctx->handle_key)
  │                                           │      free(ctx->firmware_name)
  │                                           │      free(ctx->firmware_url)
  │                                           │      free(ctx->reboot_flag)
  │                                           │      free(ctx->daemon_reject_message)
  │                                           │      destroy ready_mutex, ready_cond
  │                                           │      free(ctx)
  │                                           └─ [O] return NULL  ← thread exits
```

### 4.2 DownloadRequestContext structure

```c
/**
 * Per-request context for on-demand DownloadFirmware worker thread.
 *
 * Lifecycle:
 *   - Allocated in downloadFirmware() (caller thread)
 *   - Ownership transferred to worker thread after condvar handshake
 *   - Freed by worker thread after download completes/fails (or timeout)
 *
 * Key difference from CheckRequestContext:
 *   - callback fires MULTIPLE times (per-progress-signal), not just once
 *   - worker quits loop ONLY on terminal status (COMPLETED/ERROR)
 *   - daemon_accepted flag: worker checks daemon's synchronous reply
 *   - longer timeout (3600s vs 120s)
 */
typedef struct {
    /* Condvar handshake: worker signals "I'm ready" to caller */
    pthread_mutex_t   ready_mutex;
    pthread_cond_t    ready_cond;
    bool              is_ready;          /**< true = worker finished setup           */
    bool              init_failed;       /**< true = D-Bus failed or daemon rejected */

    /* GLib event loop (isolated, per-thread) */
    GMainContext     *context;
    GMainLoop        *main_loop;
    GDBusConnection  *connection;
    guint             subscription_id;

    /* Request data (all strdup'd — owned by worker thread) */
    char             *handle_key;        /**< strdup of FirmwareInterfaceHandle      */
    char             *firmware_name;     /**< strdup of request->firmwareName         */
    char             *firmware_url;      /**< strdup of request->firmwareUrl          */
    char             *reboot_flag;       /**< strdup of request->rebootFlag           */
    DownloadCallback  callback;          /**< Client's callback function ptr          */

    /* Daemon reply (from synchronous D-Bus method return) */
    bool              daemon_accepted;   /**< true if daemon returned RDKFW_DWNL_SUCCESS */
    char             *daemon_reject_message; /**< strdup of daemon's error message (if rejected) */

    /* Timeout tracking */
    GSource          *timeout_source;    /**< For cancellation in cleanup             */

    /* Thread handle (for join in destructor) */
    pthread_t         thread;
} DownloadRequestContext;
```

---

## 5. Design Decisions & Rationale

### 5.1 DECIDED: On-demand thread, not persistent BG thread

**Question:** "The persistent BG thread model is architecturally sound for multi-signal
broadcast. Why change it?"

**Answer:** Your senior's feedback is correct. The persistent BG thread consumes
resources 24/7 on an embedded STB, even when no download is active. Downloads
happen rarely (maybe once per day or per week). The dominant state is idle.

| Scenario | Persistent BG Thread | On-Demand Thread |
|----------|---------------------|-----------------|
| App loaded, no download for 6 hours | Thread alive (idle, ~14KB) | **No thread (~0 bytes)** |
| Download in progress (10 min) | Thread alive | Thread alive — **same cost** |
| Download finished, idle again | Thread alive (idle) | **No thread** |
| App loaded, only does CheckForUpdate | Thread alive (wasted) | **No thread** |

The 10-minute download period where both models have identical cost is dwarfed by
the hours/days of idle time where on-demand costs zero.

**Design consistency:** CheckForUpdate (Phase 1) already uses on-demand threads.
Using a different model for DownloadFirmware creates:
- Two different mental models for developers
- Two different lifecycle patterns to test
- Two different cleanup paths in the destructor
- Constructor creates thread "just in case" someone calls `downloadFirmware()`

**Decision:** On-demand thread for DownloadFirmware. Same pattern as CheckForUpdate.

### 5.2 DECIDED: Synchronous D-Bus call (call_sync) instead of fire-and-forget

**Question:** "Should the worker thread use `g_dbus_connection_call()` (fire-and-forget)
or `g_dbus_connection_call_sync()` (wait for daemon reply)?"

**Current behavior (fire-and-forget):**
```
downloadFirmware() → always returns RDKFW_DWNL_SUCCESS
                   → daemon may reject → library never knows → caller is lied to
```

**New behavior (call_sync):**
```
downloadFirmware() → worker calls daemon synchronously → reads reply
                   → daemon returns RDKFW_DWNL_SUCCESS → caller gets SUCCESS
                   → daemon returns RDKFW_DWNL_FAILED → caller gets FAILED
```

**Why this is strictly superior:**

1. **Accurate result:** The caller gets the truth. If the daemon rejected the download
   (e.g., another download is already in progress), the caller knows immediately.

2. **No wasted thread:** If the daemon rejects, the worker thread exits immediately
   after the condvar handshake. No 3600-second timeout waiting for a signal that
   will never come.

3. **D-Bus round-trip cost:** ~1-10ms on a local system bus. The condvar wait in
   `downloadFirmware()` was already waiting for the worker to set up D-Bus and
   subscribe (~50-100ms). Adding 10ms for the synchronous reply is negligible.

4. **The daemon already sends a reply.** Looking at `rdkv_dbus_server.c`:
   ```c
   g_dbus_method_invocation_return_value(resp_ctx,
       g_variant_new("(sss)", "RDKFW_DWNL_SUCCESS", "INPROGRESS", "Download started"));
   // or
   g_dbus_method_invocation_return_value(resp_ctx,
       g_variant_new("(sss)", "RDKFW_DWNL_FAILED", "DWNL_ERROR",
                     "There is an Ongoing Firmware Download"));
   ```
   This reply is already being sent. The current library just ignores it. The new
   design reads it.

**Decision:** Worker thread uses `g_dbus_connection_call_sync()`. The daemon's reply
determines whether the worker enters the signal-listening loop or exits immediately.

### 5.3 DECIDED: One download at a time per process (library-level guard)

**Question:** "Doesn't rejecting duplicate downloads make the library stateful?"

**Answer:** Yes. The library is already stateful (see CheckForUpdate's
`g_check_in_progress`). The state here is **thread lifecycle management**, not
business logic.

**What the library's guard prevents:**
- Two worker threads in the same process both subscribed to `DownloadProgress`
- Both receiving the same broadcast signal
- Both firing their respective callbacks with the same progress data
- Client receiving duplicate progress events

**What the library's guard does NOT prevent:**
- Process A and Process B both requesting downloads (separate processes, separate
  library instances, separate `g_dwnl_in_progress` flags)
- The daemon decides whether to accept both, reject one, or piggyback

**The separation of concerns:**

| Level | Responsibility | Mechanism |
|-------|---------------|-----------|
| **Library** | One worker thread per process | `g_dwnl_in_progress` flag (per-process static) |
| **Daemon** | One download at a time globally | `IsDownloadInProgress` flag (daemon-global) |

These are orthogonal. The library prevents internal thread duplication. The daemon
prevents device-level resource conflicts (network bandwidth, flash I/O).

**Implementation:** Accessor functions matching CheckForUpdate pattern:
```c
/* In rdkFwupdateMgr_async.c — all static */
static pthread_mutex_t g_dwnl_in_progress_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool g_dwnl_in_progress = false;
static DownloadRequestContext *g_active_dwnl_ctx = NULL;

bool internal_begin_download(DownloadRequestContext *ctx);  /* returns false if already active */
void internal_end_download(void);                           /* clears flag + pointer */
void internal_abort_download(void);                         /* same as end, used on error paths */
bool internal_is_dwnl_in_progress(void);                   /* query for unregisterProcess() */
```

### 5.4 DECIDED: Block unregisterProcess() during active download

**Rationale:** Identical to CheckForUpdate (see Phase 1 design doc §5.4).

`unregisterProcess()` during an active download is a semantic contradiction:
"Forget about me" while "Download this firmware and tell me the progress."

**Implementation:** Extend the existing session-state guard in `unregisterProcess()`:
```c
if (internal_is_check_in_progress() || internal_is_dwnl_in_progress()) {
    FWUPMGR_ERROR("unregisterProcess: REJECTED — operation in progress.\n");
    return;
}
```

**Concern:** Downloads can take 30 minutes. Is rejecting `unregisterProcess()` for
30 minutes acceptable?

**Answer:** Yes. The app should not be trying to unregister while a download is
active. The correct sequence is:
```
registerProcess() → checkForUpdate() → [callback] → downloadFirmware() →
[callbacks: 25%, 50%, 100% COMPLETED] → unregisterProcess()
```

If the app receives SIGTERM during a download, the same rules as CheckForUpdate
apply: just `exit()`. The daemon detects D-Bus peer disconnect and cleans up.
The library destructor joins the worker thread.

### 5.5 DECIDED: Download timeout = 3600 seconds (1 hour), stall-based

**Question:** "What timeout for the download worker? 120s is too short."

**Analysis:** A firmware download can legitimately take 30 minutes over a slow
network. A flat 120-second timeout would kill valid downloads. But an infinite
timeout risks threads hanging forever if the daemon crashes.

**Options considered:**

| Option | Timeout Type | Value | Pros | Cons |
|--------|-------------|-------|------|------|
| A | Total elapsed | 3600s (1 hour) | Simple | Kills slow but valid 90-minute downloads |
| B | Per-signal stall detector | 300s (5 min no signal) | Catches stalls, allows long downloads | More complex to implement |
| C | No timeout | ∞ | Never kills valid downloads | Thread hangs forever if daemon crashes |

**Decision: Option A — 3600 seconds total.** Rationale:
- Simple to implement (single `g_timeout_source_new_seconds(3600)`)
- 1 hour is generous for any realistic firmware download
- If a download truly takes >1 hour, the network or device has issues
- Option B is better in theory but adds complexity (resetting timeout on each signal)
  — deferred to a future optimization if real-world data shows 1-hour downloads

**Implementation:**
```c
#define DWNL_SIGNAL_TIMEOUT_SECONDS 3600
```

### 5.6 DECIDED: Signal handler fires callback on every signal, quits only on terminal

**This is the KEY difference from CheckForUpdate.**

CheckForUpdate signal handler:
```c
// ONE signal → fire callback → quit loop → thread exits
ctx->callback(&fwinfo_data);
g_main_loop_quit(ctx->main_loop);
```

DownloadFirmware signal handler:
```c
// MANY signals → fire callback each time → quit loop ONLY on terminal
ctx->callback(percentage, status);

if (status == DWNL_COMPLETED || status == DWNL_ERROR) {
    g_main_loop_quit(ctx->main_loop);  // NOW quit — download is done
}
// Otherwise: return to g_main_loop_run(), wait for next signal
```

This means the worker thread stays alive across many signals. The GMainLoop
continues running, receiving signals, firing callbacks, until a terminal status
arrives. This is architecturally identical to the persistent BG thread during an
active download — but the thread only exists while a download is active.

### 5.7 DECIDED: Do NOT filter signals by handler_id in the library

**Question:** "The daemon sends handler_id in DownloadProgress. Should the library
filter signals and only fire callbacks when handler_id matches?"

**Analysis of daemon signal emission (rdkv_dbus_server.c):**

The daemon's download worker thread emits `DownloadProgress` as a broadcast
signal (`destination=NULL`). The `handler_id` in the signal is set to the
**original requesting client's** handler_id.

For the **piggyback** case (same firmware already downloading, new client attaches):
- The daemon adds the piggybacking client to `current_download->waiting_handler_ids`
- When the download completes, the daemon emits additional signals for each
  waiting handler_id
- During progress, only the original requester's handler_id is in the signal

**Cross-process implications:**

Since A and B are separate processes, each with their own D-Bus subscription:
- When daemon emits `DownloadProgress(handler_id=1, 50%, INPROGRESS)`:
  - Process A's worker receives it (handler_id=1 matches A's registration)
  - Process B's worker also receives it (broadcast, B is subscribed too)
  - If B filters by handler_id: B would miss this signal (handler_id=1 ≠ 2)
  - If B doesn't filter: B fires callback with A's progress — is this correct?

**The daemon already rejected B** (or piggybacked B) before the worker thread
entered the signal-listening loop. So:
- If daemon rejected B → B's worker never enters g_main_loop_run() →
  B never receives any signals → filtering is irrelevant
- If daemon piggybacked B → B should receive progress → don't filter

**Decision:** Do NOT filter by handler_id. The daemon's acceptance/rejection model
(via synchronous reply) already gates which clients enter the signal-listening
phase. Any client whose worker is listening has been accepted by the daemon and
should receive all progress signals.

**Exception for future consideration:** If the daemon evolves to support
parallel downloads of different firmware types, handler_id filtering would
become necessary. This is deferred.

---

## 6. Multi-Client Scenario Walkthrough

### Scenario 1: Process A downloads, Process B rejected by daemon

This is the primary scenario based on the daemon's `IsDownloadInProgress` guard.

```
PROCESS A (PID 100)                    DAEMON                           PROCESS B (PID 200)
──────────────────                     ──────                           ──────────────────

downloadFirmware("1", req, cbA)                                        
├─ validate ✓                                                          
├─ g_dwnl_in_progress=true                                            
├─ spawn worker_A                                                      
│                                                                      
│ worker_A:                                                            
│ ├─ subscribe(DownloadProgress)                                       
│ ├─ call_sync(DownloadFirmware)                                       
│ │   ─────────────────────────────►                                   
│ │                                  IsDownloadInProgress = false       
│ │                                  Accept! Start download.           
│ │                                  IsDownloadInProgress = TRUE       
│ │                                  current_download = {fw, 0%, [1]}  
│ │   ◄─────────────────────────────                                   
│ │   reply: ("SUCCESS","INPROGRESS","Download started")               
│ ├─ daemon_accepted = true                                            
│ ├─ signal ready                                                      
│ └─ g_main_loop_run()                                                 
│                                                                       downloadFirmware("2", req, cbB)
├─ condvar wakes                                                        ├─ validate ✓
├─ return RDKFW_DWNL_SUCCESS                                           ├─ g_dwnl_in_progress=true
│                                                                       ├─ spawn worker_B
│ App A free to do work                                                 │
│                                                                       │ worker_B:
│                                                                       │ ├─ subscribe(DownloadProgress)
│                                                                       │ ├─ call_sync(DownloadFirmware)
│                                                                       │ │   ────────────────────────►
│                                                                       │ │                              IsDownloadInProgress == TRUE
│                                                                       │ │                              REJECT!
│                                                                       │ │   ◄────────────────────────
│                                                                       │ │   reply: ("FAILED","DWNL_ERROR",
│                                                                       │ │           "There is an Ongoing Firmware Download")
│                                                                       │ ├─ daemon_accepted = false
│                                                                       │ ├─ init_failed = true
│                                                                       │ ├─ daemon_reject_message = "There is an Ongoing..."
│                                                                       │ ├─ signal ready
│                                                                       │ └─ goto cleanup → thread exits
│                                                                       │
│                                                                       ├─ condvar wakes
│                                                                       ├─ init_failed = true
│                                                                       ├─ Log: "Daemon rejected: There is an Ongoing..."
│                                                                       ├─ return RDKFW_DWNL_FAILED ◄── ACCURATE!
│                                                                       │
│                                      Daemon emitting progress...      │  App B knows: download rejected
│                                      DownloadProgress(1, 25%, INPROG)
│                                         │
│ worker_A receives ◄─────────────────────┘
│ cbA(25, DWNL_IN_PROGRESS)
│                                      DownloadProgress(1, 50%, INPROG)
│ cbA(50, DWNL_IN_PROGRESS)
│                                      DownloadProgress(1, 100%, COMPLETED)
│ cbA(100, DWNL_COMPLETED)
│ g_main_loop_quit()
│ cleanup, internal_end_download()
│ g_dwnl_in_progress = false
│ thread exits
```

**Key point:** Process B's library returned `RDKFW_DWNL_FAILED` with the daemon's
exact rejection message. Today it would return `RDKFW_DWNL_SUCCESS` (a lie).

### Scenario 2: Process A downloads, Process B piggybacks (same firmware)

The daemon's piggyback logic allows a second client to attach to an ongoing
download of the **same firmware file**.

```
PROCESS A                              DAEMON                              PROCESS B
─────────                              ──────                              ─────────

worker_A: call_sync(DownloadFirmware,
                     fw="RDKV_v2.1.bin")
          ──────────────────────────►
                                       Accept! Start download.
                                       IsDownloadInProgress = TRUE
                                       current_download = {RDKV_v2.1.bin, 0%, [1]}
          ◄──────────────────────────
          ("SUCCESS","INPROGRESS","Download started")
          g_main_loop_run()
                                                                           worker_B: call_sync(DownloadFirmware,
                                                                                        fw="RDKV_v2.1.bin")
                                                                           ──────────────────────────►
                                       Same firmware! PIGGYBACK.
                                       waiting_handler_ids = [2]
                                       current progress = 30%
                                                                           ◄──────────────────────────
                                                                           ("SUCCESS","INPROGRESS",
                                                                            "Download already in progress")
                                                                           g_main_loop_run()

                                       DownloadProgress(1, 50%, INPROG) ← broadcast
worker_A receives: cbA(50, INPROG)                                         worker_B receives: cbB(50, INPROG)

                                       DownloadProgress(1, 100%, COMPLETED) ← broadcast
worker_A receives: cbA(100, COMPLETED)                                     worker_B receives: cbB(100, COMPLETED)
g_main_loop_quit()                                                         g_main_loop_quit()
cleanup, thread exits                                                      cleanup, thread exits
```

**Both processes receive progress and completion.** The piggyback model works
correctly with on-demand threads because:
- Both worker threads are subscribed to `DownloadProgress` (broadcast)
- Both receive every signal
- Both fire their callbacks
- Both quit on `COMPLETED` and exit cleanly

### Scenario 3: Same process calls downloadFirmware() twice

```c
// WITHIN THE SAME PROCESS:
downloadFirmware("1", req1, cb1);   // → RDKFW_DWNL_SUCCESS, worker spawned
downloadFirmware("1", req2, cb2);   // → RDKFW_DWNL_FAILED (g_dwnl_in_progress == true)
```

**Behavior:** Second call rejected immediately at the library level (step [4]).
No thread spawned. No D-Bus call. Clear log message:
`"downloadFirmware: already in progress for this process, rejecting"`

---

## 7. Daemon Download Handler Deep Dive

Understanding the daemon's exact behavior is critical for the library design.
Here is the decision tree extracted from `rdkv_dbus_server.c`:

```
Daemon receives DownloadFirmware(handler_id, firmware_name, firmware_url, reboot_flag)
│
├── handler_id invalid or not registered?
│   └── Return ("RDKFW_DWNL_FAILED", "DWNL_ERROR", "Invalid handler ID")
│
├── IsDownloadInProgress == TRUE ?
│   ├── current_download->firmware_name == firmware_name ?
│   │   └── PIGGYBACK: Add handler_id to waiting_handler_ids
│   │       └── Return ("RDKFW_DWNL_SUCCESS", "INPROGRESS",
│   │                    "Download already in progress")
│   │           + Return current progress immediately
│   │
│   └── current_download->firmware_name != firmware_name ?
│       └── REJECT: Different firmware already downloading
│           └── Return ("RDKFW_DWNL_FAILED", "DWNL_ERROR",
│                        "There is an Ongoing Firmware Download")
│
├── Firmware already cached/downloaded?
│   └── CACHED: Return ("RDKFW_DWNL_SUCCESS", "COMPLETED",
│                         "Firmware already available")
│       + Emit DownloadProgress(handler_id, 100, COMPLETED) immediately
│
└── No download active, firmware not cached?
    └── START NEW DOWNLOAD:
        ├── IsDownloadInProgress = TRUE
        ├── current_download = {firmware_name, 0%, [handler_id]}
        ├── Spawn download_firmware_worker_thread()
        └── Return ("RDKFW_DWNL_SUCCESS", "INPROGRESS", "Download started")
```

### 7.1 Signal emission by daemon

The daemon's download worker thread emits `DownloadProgress` signals periodically:

```c
/* Signal signature: (tsuss) */
g_variant_new("(tsuss)",
    handler_id_numeric,        /* uint64: original requester's ID */
    firmware_name,             /* string: firmware filename */
    progress_percent,          /* uint32: 0-100 */
    status_string,             /* string: "INPROGRESS" or "COMPLETED" or "ERROR" */
    message                    /* string: human-readable message */
);
```

**Destination:** `NULL` (broadcast to all subscribed connections)

**When emitted:**
- Periodically during download (implementation-defined intervals)
- On download completion (100%, COMPLETED)
- On download error (DWNL_ERROR, with error message)
- Immediately on piggyback (current progress sent to piggybacking client)

### 7.2 Implications for library design

| Daemon behavior | Library impact |
|----------------|----------------|
| Daemon returns `(sss)` reply synchronously | Worker reads reply via `call_sync`, caller gets accurate SUCCESS/FAIL |
| Daemon rejects concurrent different-firmware downloads | Worker exits immediately on rejection, no signal-listening |
| Daemon piggybacks same-firmware downloads | Worker enters signal-listening, receives progress normally |
| Daemon emits cached-firmware COMPLETED immediately | Worker receives COMPLETED signal almost immediately, callback fires, thread exits fast |
| Signal is broadcast (NULL destination) | All subscribed workers receive it (multi-process safe) |

---

## 8. Thread Lifecycle & Memory Ownership

### 8.1 Complete lifecycle diagram

```
                    HEAP
                    ┌─────────────────────────────────────────────┐
CALLER THREAD       │  DownloadRequestContext *ctx                │  WORKER THREAD
─────────────       │                                             │  ─────────────
                    │  handle_key ──► strdup("1")                 │
calloc(ctx) ───────►│  firmware_name ──► strdup("RDKV_v2.1.bin") │
                    │  firmware_url ──► strdup("http://...")       │
                    │  reboot_flag ──► strdup("1")                │
                    │  callback ──► cbA                           │
                    │  ready_mutex, ready_cond                    │
                    │  is_ready = false                           │
                    │  init_failed = false                        │
                    │  daemon_accepted = false                    │
                    │  daemon_reject_message = NULL               │
                    │                                             │
pthread_create() ──►│  thread ──► worker thread ID               │◄── thread starts
                    │                                             │
cond_wait()         │  (worker: D-Bus setup, subscribe, call_sync)│
  │ blocked         │                                             │
  │                 │  daemon replies...                          │
  │                 │  daemon_accepted = true                     │
  │                 │  is_ready = true ◄──────────────────────────│  signal ready
  │ wakes up ◄──────│  cond_signal()                              │
  │                 │                                             │  g_main_loop_run()
reads init_failed   │                                             │    │
reads daemon_reject │      OWNERSHIP WALL                        │    │ (receives signals
  │                 │      ═══════════════                        │    │  for 1-30 minutes)
  ▼                 │   Caller NEVER touches ctx again           │    │
return SUCCESS      │                                             │    │
                    │                                             │    │ cbA(25, INPROG)
  App does work     │                                             │    │ cbA(50, INPROG)
                    │                                             │    │ cbA(75, INPROG)
                    │                                             │    │ cbA(100, COMPLETED)
                    │                                             │    ▼
                    │                                             │  g_main_loop_quit()
                    │  internal_end_download() ◄──────────────────│
                    │  free(handle_key) ◄─────────────────────────│  cleanup
                    │  free(firmware_name) ◄──────────────────────│
                    │  free(firmware_url) ◄───────────────────────│
                    │  free(reboot_flag) ◄────────────────────────│
                    │  free(daemon_reject_message) ◄─────────────│
                    │  destroy mutex, cond ◄─────────────────────│
                    └─────────────────────────────────────────────┘
                    free(ctx) ◄────────────────────────────────────│  thread exits
```

### 8.2 Memory ownership rules

| Memory | Allocated by | Owned by | Freed by |
|--------|-------------|----------|----------|
| `ctx` itself | Caller (`calloc`) | Worker thread (after handshake) | Worker thread (`free`) |
| `ctx->handle_key` | Caller (`strdup`) | Worker thread | Worker thread (`free`) |
| `ctx->firmware_name` | Caller (`strdup`) | Worker thread | Worker thread (`free`) |
| `ctx->firmware_url` | Caller (`strdup`) | Worker thread | Worker thread (`free`) |
| `ctx->reboot_flag` | Caller (`strdup`) | Worker thread | Worker thread (`free`) |
| `ctx->daemon_reject_message` | Worker thread (`g_strdup` from reply) | Worker thread | Worker thread (`free`) |
| `ctx->callback` | N/A (function pointer) | N/A | N/A |
| `ctx->context` (GMainContext) | Worker thread | Worker thread | Worker thread (`g_main_context_unref`) |
| `ctx->main_loop` (GMainLoop) | Worker thread | Worker thread | Worker thread (`g_main_loop_unref`) |
| `ctx->connection` (GDBusConnection) | Worker thread (GLib singleton) | GLib | Worker thread (`g_object_unref`) |
| `ctx->timeout_source` | Worker thread | GMainContext (attached) | Auto-freed when context destroyed |

**No double-free risk.** Every allocation has exactly one owner and one free point.

---

## 9. Thread Safety Proof

### 9.1 Shared mutable state inventory

| State | Accessed by | Protection |
|-------|------------|------------|
| `ctx->is_ready`, `ctx->init_failed`, `ctx->daemon_accepted`, `ctx->daemon_reject_message` | Caller (read), Worker (write) | `ctx->ready_mutex` + `ctx->ready_cond` |
| `g_dwnl_in_progress` | Caller (read/write), Worker (write) | `g_dwnl_in_progress_mutex` |
| `g_active_dwnl_ctx` | Caller (write), Worker (write), Destructor (read/write) | `g_dwnl_in_progress_mutex` |

**Only 3 pieces of shared mutable state, all mutex-protected.** Identical pattern
to CheckForUpdate.

### 9.2 Callback thread safety

The `DownloadCallback` is invoked from the worker thread. It is invoked **multiple
times** (per-signal). Each invocation is sequential — GLib's GMainLoop dispatches
signals one at a time. There is no concurrent callback invocation risk.

However, the client's callback code runs in the worker thread's context. If the
client's callback accesses shared state in the client app, the client is responsible
for its own synchronization. This is a documented API contract.

### 9.3 Condvar handshake correctness

Identical pattern to CheckForUpdate. See Phase 1 design doc §8.2. The only
difference is that the worker does MORE work before signaling ready (D-Bus setup +
synchronous daemon call instead of just D-Bus setup + async call). The condvar
protocol is identical:

```c
// WORKER: sets is_ready/init_failed/daemon_accepted UNDER MUTEX, then signals
// CALLER: waits UNDER MUTEX, reads is_ready/init_failed/daemon_accepted
```

### 9.4 g_dwnl_in_progress correctness

Identical pattern to `g_check_in_progress`. Set in `internal_begin_download()`,
cleared in `internal_end_download()`. Both under mutex. Accessor function
`internal_is_dwnl_in_progress()` for `unregisterProcess()`.

---

## 10. Edge Cases & Robustness

### 10.1 Same process calls downloadFirmware() twice

```c
downloadFirmware("1", req, cb1);   // → SUCCESS
downloadFirmware("1", req, cb2);   // → FAILED ("already in progress")
```
Second call rejected at library level. No thread, no D-Bus call. ✅

### 10.2 Daemon rejects download (another firmware already downloading)

```
Worker: call_sync(DownloadFirmware) → daemon returns RDKFW_DWNL_FAILED
Worker: init_failed = true, daemon_reject_message = "There is an Ongoing..."
Worker: signals ready, goto cleanup, thread exits
Caller: reads init_failed = true → returns RDKFW_DWNL_FAILED
```
Accurate error reporting. No wasted thread. ✅

### 10.3 Daemon accepts (piggyback — same firmware already downloading)

```
Worker: call_sync(DownloadFirmware) → daemon returns RDKFW_DWNL_SUCCESS
        reply includes: status="INPROGRESS", message="Download already in progress"
Worker: daemon_accepted = true, signals ready
Worker: enters g_main_loop_run() — receives remaining progress signals
Caller: returns RDKFW_DWNL_SUCCESS
Callback fires with remaining progress (50%, 75%, 100%)
```
Client receives progress from the point of piggybacking. ✅

### 10.4 Firmware already cached on device

```
Daemon: Firmware found in cache.
Daemon: returns ("RDKFW_DWNL_SUCCESS", "COMPLETED", "Firmware already available")
Daemon: immediately emits DownloadProgress(handler_id, 100, COMPLETED)
Worker: daemon_accepted = true, signals ready
Worker: enters g_main_loop_run()
Worker: immediately receives COMPLETED signal
Worker: cbA(100, DWNL_COMPLETED), g_main_loop_quit()
Worker: cleanup, thread exits (~100ms total)
```
Fast path for cached firmware. ✅

### 10.5 Daemon crashes during download

```
Worker: listening for DownloadProgress in g_main_loop_run()
Daemon: crashes
Worker: no more signals arrive
Worker: 3600-second timeout fires → g_main_loop_quit()
Worker: callback NOT fired (no COMPLETED/ERROR signal received)
Worker: cleanup, thread exits
```

**Should the worker fire a `DWNL_ERROR` callback on timeout?** Yes. The client
needs to know the download failed. Updated behavior:

```c
static gboolean on_download_timeout(gpointer user_data) {
    DownloadRequestContext *ctx = user_data;
    FWUPMGR_ERROR("download_worker: timed out after %d seconds\n",
                  DWNL_SIGNAL_TIMEOUT_SECONDS);
    /* Fire error callback so client knows */
    ctx->callback(0, DWNL_ERROR);
    g_main_loop_quit(ctx->main_loop);
    return G_SOURCE_REMOVE;
}
```
Client receives `DWNL_ERROR` on timeout. Clean exit. ✅

### 10.6 Client calls unregisterProcess() during download

```
downloadFirmware("1", req, cb);   // → SUCCESS, worker running
unregisterProcess(handle);         // → REJECTED (logged)
// ... 10 minutes later ...
// callback fires: cb(100, DWNL_COMPLETED)
unregisterProcess(handle);         // → SUCCESS
```
Session integrity preserved. ✅

### 10.7 Library unloaded (dlclose) during active download

```
Destructor: internal_cancel_all_active_download_threads()
  → g_main_loop_quit(ctx->main_loop)
  → pthread_join(ctx->thread, NULL)     ← blocks until worker exits
Worker: g_main_loop_run() returns, cleanup, thread exits
Destructor: continues, library code unmapped safely
```
No code executing in unmapped memory. ✅

### 10.8 SIGTERM during active download

Same as CheckForUpdate (Phase 1 doc §9.9):
1. **Best:** Wait for COMPLETED/ERROR callback, then unregister and exit
2. **Acceptable:** Just `exit()`. Destructor joins worker. Daemon detects disconnect.
3. **Wrong:** Call `unregisterProcess()` (rejected during download)

### 10.9 Download error signal from daemon

```
Daemon emits: DownloadProgress(handler_id, 0, "ERROR", "HTTP 404 Not Found")
Worker: on_download_signal_handler():
  → parse: percentage=0, status=DWNL_ERROR
  → ctx->callback(0, DWNL_ERROR)
  → g_main_loop_quit()   ← terminal status, quit loop
Worker: cleanup, thread exits
```
Error signal handled exactly like COMPLETED. ✅

### 10.10 Multiple progress signals arrive in rapid succession

```
Daemon emits: DownloadProgress(25%, INPROG)
Daemon emits: DownloadProgress(26%, INPROG)  ← immediately after
Daemon emits: DownloadProgress(27%, INPROG)  ← immediately after
```
GLib's GMainLoop dispatches these sequentially. `on_download_signal_handler()` is
called three times, each time firing the callback. No signal is lost. No
concurrent callback invocation. ✅

### 10.11 Signal arrives after g_main_loop_quit() but before unsubscribe

Same as CheckForUpdate (Phase 1 doc §9.6). Signal is queued but loop has
exited. Handler does NOT fire. `g_dbus_connection_signal_unsubscribe()` cleans up
the subscription. ✅

---

## 11. Dead Code Removal Plan

### 11.1 What to remove from `rdkFwupdateMgr_async_internal.h`

| Item | Action | Reason |
|------|--------|--------|
| `DwnlCallbackState` enum | **REMOVE** | Registry-based — replaced by per-request ctx |
| `DwnlCallbackEntry` struct | **REMOVE** | Registry slot — replaced by per-request ctx |
| `DwnlCallbackRegistry` struct | **REMOVE** | Global registry — replaced by on-demand thread |
| `InternalDwnlSignalData` struct | **KEEP** | Still needed to parse DownloadProgress signal |
| `internal_parse_dwnl_signal_data()` | **KEEP** | Reused by new signal handler |
| `internal_dwnl_register_callback()` | **REMOVE** | No registry to register in |
| `internal_dwnl_system_deinit()` | **REMOVE** | No registry to clean up |

### 11.2 What to remove from `rdkFwupdateMgr_async.c`

| Item | Action | Reason |
|------|--------|--------|
| `static DwnlCallbackRegistry g_dwnl_registry;` | **REMOVE** | No global registry |
| `on_download_progress_signal()` function | **REMOVE** | Old BG thread signal handler |
| `dispatch_all_dwnl_active()` function | **REMOVE** | Old broadcast dispatch |
| `internal_dwnl_register_callback()` function | **REMOVE** | No registry |
| `dwnl_registry_reset_slot()` function | **REMOVE** | No registry slots |
| `internal_dwnl_system_deinit()` function | **REMOVE** | No registry to clean up |
| `g_dwnl_registry` init in `internal_system_init()` | **REMOVE** | No registry |
| `g_dwnl_registry` cleanup in `internal_system_deinit()` | **REMOVE** | No registry |
| `DownloadProgress` subscription in `background_thread_func()` | **REMOVE** | BG thread no longer handles download signals |

### 11.3 What to remove from `rdkFwupdateMgr_api.c`

| Item | Action | Reason |
|------|--------|--------|
| Old `downloadFirmware()` body | **REPLACE** | New on-demand implementation |

### 11.4 What to keep

| Item | Reason |
|------|--------|
| `InternalDwnlSignalData` struct | Reused by new `on_download_signal_handler()` |
| `internal_parse_dwnl_signal_data()` | Reused |
| `internal_cleanup_dwnl_signal_data()` | Reused |
| All Update types and functions | Phase 3 — untouched in this phase |
| `background_thread_func()` | Still needed for UpdateProgress (Phase 3 removes it) |
| `internal_system_init()` | Still needed for Update registry + BG thread (Phase 3 removes it) |
| `internal_system_deinit()` | Still needed for Update cleanup (Phase 3 removes it) |

---

## 12. File-by-File Change Specification

### 12.1 `rdkFwupdateMgr_client.h` — NO CHANGES

Public API unchanged. Zero breakage.

`FirmwareDownloadResult`, `FwDownloadRequest`, `DownloadCallback`, `DownloadStatus`
all remain identical.

### 12.2 `rdkFwupdateMgr_async_internal.h`

**Removals:**
- `DwnlCallbackState` enum
- `DwnlCallbackEntry` struct
- `DwnlCallbackRegistry` struct
- `internal_dwnl_register_callback()` declaration
- `internal_dwnl_system_deinit()` declaration

**Additions:**
```c
/* Timeout for download worker thread (seconds) — 1 hour */
#define DWNL_SIGNAL_TIMEOUT_SECONDS 3600

/**
 * Per-request context for on-demand DownloadFirmware worker thread.
 *
 * Lifecycle:
 *   - Allocated in downloadFirmware() (caller thread)
 *   - Ownership transferred to worker thread after condvar handshake
 *   - Freed by worker thread after download completes/fails (or timeout)
 *
 * Key difference from CheckRequestContext:
 *   - callback fires MULTIPLE times (per-progress-signal), not just once
 *   - worker quits loop ONLY on terminal status (COMPLETED/ERROR)
 *   - daemon_accepted: worker reads daemon's synchronous reply
 *   - longer timeout (3600s vs 120s)
 */
typedef struct {
    pthread_mutex_t   ready_mutex;
    pthread_cond_t    ready_cond;
    bool              is_ready;
    bool              init_failed;

    GMainContext     *context;
    GMainLoop        *main_loop;
    GDBusConnection  *connection;
    guint             subscription_id;

    char             *handle_key;
    char             *firmware_name;
    char             *firmware_url;
    char             *reboot_flag;
    DownloadCallback  callback;

    bool              daemon_accepted;
    char             *daemon_reject_message;

    GSource          *timeout_source;
    pthread_t         thread;
} DownloadRequestContext;

/**
 * Worker thread entry point for on-demand DownloadFirmware.
 * @param arg  DownloadRequestContext* (ownership transferred)
 * @return NULL
 */
void *internal_download_worker_thread(void *arg);

/**
 * Begin/end download state management (encapsulated accessors).
 * All state is static inside _async.c.
 */
bool internal_begin_download(DownloadRequestContext *ctx);
void internal_end_download(void);
void internal_abort_download(void);
bool internal_is_dwnl_in_progress(void);
void internal_cancel_all_active_download_threads(void);
```

**No changes to:**
- `InternalDwnlSignalData` struct
- `internal_parse_dwnl_signal_data()` / `internal_cleanup_dwnl_signal_data()`
- All CheckForUpdate types (already migrated in Phase 1)
- All Update types (migrated in Phase 3)

### 12.3 `rdkFwupdateMgr_async.c`

**Remove** (Download-specific old code):
- `static DwnlCallbackRegistry g_dwnl_registry;`
- `g_dwnl_registry` init in `internal_system_init()`
- `g_dwnl_registry` cleanup in `internal_system_deinit()`
- `on_download_progress_signal()` function
- `dispatch_all_dwnl_active()` function
- `internal_dwnl_register_callback()` function
- `dwnl_registry_reset_slot()` function
- `internal_dwnl_system_deinit()` function
- `DownloadProgress` subscription in `background_thread_func()`

**Add** (new on-demand Download code):

1. **State globals (static, encapsulated):**
   ```c
   static pthread_mutex_t g_dwnl_in_progress_mutex = PTHREAD_MUTEX_INITIALIZER;
   static bool g_dwnl_in_progress = false;
   static DownloadRequestContext *g_active_dwnl_ctx = NULL;
   ```

2. **Accessor functions:**
   ```c
   bool internal_begin_download(DownloadRequestContext *ctx) {
       pthread_mutex_lock(&g_dwnl_in_progress_mutex);
       if (g_dwnl_in_progress) {
           pthread_mutex_unlock(&g_dwnl_in_progress_mutex);
           return false;
       }
       g_dwnl_in_progress = true;
       g_active_dwnl_ctx = ctx;
       pthread_mutex_unlock(&g_dwnl_in_progress_mutex);
       return true;
   }

   void internal_end_download(void) {
       pthread_mutex_lock(&g_dwnl_in_progress_mutex);
       g_dwnl_in_progress = false;
       g_active_dwnl_ctx = NULL;
       pthread_mutex_unlock(&g_dwnl_in_progress_mutex);
   }

   void internal_abort_download(void) { internal_end_download(); }

   bool internal_is_dwnl_in_progress(void) {
       pthread_mutex_lock(&g_dwnl_in_progress_mutex);
       bool result = g_dwnl_in_progress;
       pthread_mutex_unlock(&g_dwnl_in_progress_mutex);
       return result;
   }
   ```

3. **Worker thread function:** `void *internal_download_worker_thread(void *arg)`
   - Steps [A] through [O] as described in Section 4.1
   - Key difference from CheckForUpdate: step [F] uses `g_dbus_connection_call_sync()`
     and parses the `(sss)` reply
   - Key difference: signal handler fires callback but only quits on terminal

4. **Signal handler:** `static void on_download_signal_handler(...)`
   - Parses `InternalDwnlSignalData` via `internal_parse_dwnl_signal_data()`
   - Maps status string to `DownloadStatus` enum
   - Fires `ctx->callback(percentage, status)`
   - If `status == DWNL_COMPLETED || status == DWNL_ERROR`: `g_main_loop_quit()`
   - Otherwise: returns to loop (wait for next signal)

5. **Timeout handler:** `static gboolean on_download_timeout(...)`
   - Fires `ctx->callback(0, DWNL_ERROR)` to notify client
   - Calls `g_main_loop_quit()`

6. **Cancel function:** `void internal_cancel_all_active_download_threads(void)`
   - Same pattern as CheckForUpdate: quit loop → join thread

### 12.4 `rdkFwupdateMgr_api.c`

**Replace `downloadFirmware()` body entirely.** New implementation:

```c
FirmwareDownloadResult downloadFirmware(FirmwareInterfaceHandle handle,
                                         FwDownloadRequest *request,
                                         DownloadCallback callback)
{
    /* [1] Validate handle */
    if (handle == NULL || strlen(handle) == 0) {
        FWUPMGR_ERROR("downloadFirmware: invalid handle\n");
        return RDKFW_DWNL_FAILED;
    }

    /* [2] Validate request */
    if (request == NULL) {
        FWUPMGR_ERROR("downloadFirmware: request is NULL\n");
        return RDKFW_DWNL_FAILED;
    }
    if (strlen(request->firmwareName) == 0) {
        FWUPMGR_ERROR("downloadFirmware: firmwareName is empty\n");
        return RDKFW_DWNL_FAILED;
    }

    /* [3] Validate callback */
    if (callback == NULL) {
        FWUPMGR_ERROR("downloadFirmware: callback is NULL\n");
        return RDKFW_DWNL_FAILED;
    }

    /* [4] Allocate context */
    DownloadRequestContext *ctx = calloc(1, sizeof(DownloadRequestContext));
    if (ctx == NULL) {
        FWUPMGR_ERROR("downloadFirmware: calloc failed\n");
        return RDKFW_DWNL_FAILED;
    }

    ctx->handle_key = strdup(handle);
    ctx->firmware_name = strdup(request->firmwareName);
    ctx->firmware_url = strdup(request->firmwareUrl);
    ctx->reboot_flag = strdup(request->rebootFlag);
    ctx->callback = callback;
    pthread_mutex_init(&ctx->ready_mutex, NULL);
    pthread_cond_init(&ctx->ready_cond, NULL);

    /* [5] Attempt to claim the download slot (atomic) */
    if (!internal_begin_download(ctx)) {
        FWUPMGR_WARN("downloadFirmware: already in progress, rejecting\n");
        free(ctx->handle_key);
        free(ctx->firmware_name);
        free(ctx->firmware_url);
        free(ctx->reboot_flag);
        pthread_mutex_destroy(&ctx->ready_mutex);
        pthread_cond_destroy(&ctx->ready_cond);
        free(ctx);
        return RDKFW_DWNL_FAILED;
    }

    /* [6] Spawn worker thread */
    int rc = pthread_create(&ctx->thread, NULL, internal_download_worker_thread, ctx);
    if (rc != 0) {
        FWUPMGR_ERROR("downloadFirmware: pthread_create failed (%d)\n", rc);
        internal_abort_download();
        free(ctx->handle_key);
        free(ctx->firmware_name);
        free(ctx->firmware_url);
        free(ctx->reboot_flag);
        pthread_mutex_destroy(&ctx->ready_mutex);
        pthread_cond_destroy(&ctx->ready_cond);
        free(ctx);
        return RDKFW_DWNL_FAILED;
    }
    pthread_detach(ctx->thread);  /* NO — see §12.4.1 */

    /* [7] Wait for worker to set up and get daemon reply */
    pthread_mutex_lock(&ctx->ready_mutex);
    while (!ctx->is_ready) {
        pthread_cond_wait(&ctx->ready_cond, &ctx->ready_mutex);
    }
    bool failed = ctx->init_failed;
    pthread_mutex_unlock(&ctx->ready_mutex);

    /* [8] Check result */
    if (failed) {
        FWUPMGR_ERROR("downloadFirmware: worker init failed\n");
        /* Worker thread handles its own cleanup (ctx freed by worker) */
        return RDKFW_DWNL_FAILED;
    }

    /* [9] Success — download initiated, worker is listening for signals */
    FWUPMGR_INFO("downloadFirmware: initiated for handle='%s', firmware='%s'\n",
                 handle, request->firmwareName);
    return RDKFW_DWNL_SUCCESS;
}
```

#### 12.4.1 pthread_detach vs pthread_join — DO NOT DETACH

**We must NOT call `pthread_detach()`.** The destructor needs `pthread_join()` to
ensure the worker thread exits before library code is unmapped. Detached threads
cannot be joined. The worker thread handle is stored in `ctx->thread` and joined
by `internal_cancel_all_active_download_threads()` during library unload.

**Correction:** Remove `pthread_detach()` from the above code. The thread is
joinable (default). It is either:
- Self-completing (worker exits after COMPLETED/ERROR/timeout, no join needed)
- Joined by destructor (library unload while download active)

Since we can't join a self-completed thread (double-join is UB if thread already
exited), we use the same pattern as CheckForUpdate: the destructor quits the loop
(if still running) and joins. If the thread already exited, we need to track
whether joining is still valid.

**Solution:** Use the `g_active_dwnl_ctx` pointer as the join indicator.
`internal_end_download()` sets it to NULL. The destructor only joins if
`g_active_dwnl_ctx != NULL`.

### 12.5 `rdkFwupdateMgr_process.c`

**Extend the session-state guard:**

```c
void unregisterProcess(FirmwareInterfaceHandle handler)
{
    /* Session state validation: reject if ANY operation is active */
    if (internal_is_check_in_progress()) {
        FWUPMGR_ERROR("unregisterProcess: REJECTED — checkForUpdate() in progress\n");
        return;
    }
    if (internal_is_dwnl_in_progress()) {
        FWUPMGR_ERROR("unregisterProcess: REJECTED — downloadFirmware() in progress\n");
        return;
    }

    /* ... rest of existing function unchanged ... */
}
```

### 12.6 `rdkFwupdateMgr_api.c` (destructor update)

```c
__attribute__((destructor))
static void rdkFwupdateMgr_lib_deinit(void)
{
    FWUPMGR_INFO("=== rdkFwupdateMgr library unloading ===\n");

    /* Phase 1: Cancel active CheckForUpdate worker */
    internal_cancel_all_active_check_threads();

    /* Phase 2: Cancel active DownloadFirmware worker */
    internal_cancel_all_active_download_threads();

    /* Phase 3 (future): Cancel active UpdateFirmware worker */
    /* internal_cancel_all_active_update_threads(); */

    /* Persistent BG thread cleanup (still needed for Update in Phase 2) */
    internal_system_deinit();

    FWUPMGR_INFO("=== rdkFwupdateMgr library unloaded ===\n");
}
```

### 12.7 `example_app.c` — NO CHANGES

The example app uses the public API identically. `DownloadCallback` fires in the
worker thread (previously in the BG thread). Client behavior is unchanged.

---

## 13. Unit Test Impact

### 13.1 Tests to rewrite (Download-specific)

| Test | Current Behavior | New Behavior |
|------|-----------------|-------------|
| Download registry init/cleanup | Tests `g_dwnl_registry` initialization | N/A — no registry |
| Download callback registration | Tests `internal_dwnl_register_callback()` | N/A — no registration |
| Download dispatch | Tests `dispatch_all_dwnl_active()` | N/A — direct callback from signal handler |

### 13.2 New tests needed

| Test | Description |
|------|-------------|
| `DownloadWorker_StartsAndExits` | Worker thread created on `downloadFirmware()`, exits after COMPLETED signal |
| `DownloadWorker_FiresMultipleCallbacks` | Callback invoked for each progress signal (25%, 50%, 100%) |
| `DownloadWorker_FiresErrorCallback` | Callback invoked with DWNL_ERROR on error signal |
| `DownloadWorker_Timeout` | Thread exits after 3600s, fires DWNL_ERROR callback |
| `DownloadWorker_DaemonReject` | `RDKFW_DWNL_FAILED` returned when daemon rejects (accurate reporting) |
| `DownloadWorker_DaemonPiggyback` | Worker enters signal loop on piggyback, receives progress |
| `DownloadWorker_CachedFirmware` | Worker receives immediate COMPLETED, exits fast |
| `DownloadDuplicate_Rejected` | Second `downloadFirmware()` returns FAILED while first active |
| `UnregisterDuringDownload_Rejected` | `unregisterProcess()` rejected while download active |
| `LibraryUnloadDuringDownload` | Destructor joins active worker thread |
| `DownloadWorker_DBusFailure` | `RDKFW_DWNL_FAILED` returned when D-Bus unavailable |
| `DownloadCallbackData_Correct` | Percentage and status values match signal payload |
| `DownloadWorker_RapidSignals` | Multiple signals in quick succession all fire callbacks |

---

## 14. Resource Cost Comparison

### 14.1 Memory comparison

| State | Current (BG thread + registry) | New (on-demand) |
|-------|-------------------------------|-----------------|
| Library loaded, no download | ~14KB (BG thread + registries) | ~0 for download* |
| Download in progress (10 min) | ~14KB (same — BG thread idle cost) | ~12KB (worker + ctx + GLib) |
| Download finished, idle | ~14KB (BG thread still alive) | ~0 (worker exited)* |

*Plus the Update BG thread overhead, which is removed in Phase 3.

### 14.2 Per-request cost

| Resource | Size | Duration |
|----------|------|----------|
| `DownloadRequestContext` | ~200 bytes (more fields than CheckRequestContext) | Download lifetime (1–30 min) |
| pthread stack | ~8KB | Download lifetime |
| GMainContext | ~1.5KB | Download lifetime |
| GMainLoop | ~200 bytes | Download lifetime |
| D-Bus signal subscription | ~100 bytes | Download lifetime |
| **Total** | **~10KB** | **1–30 minutes** |

All resources freed to zero after download completes/fails.

---

## 15. Migration Steps

### Phase 2: DownloadFirmware On-Demand Thread

| Step | Task | Effort | Risk |
|------|------|--------|------|
| 2.1 | Add `DownloadRequestContext` to `_async_internal.h` | 0.5h | Low |
| 2.2 | Remove Download registry types from `_async_internal.h` | 0.5h | Low |
| 2.3 | Implement `internal_download_worker_thread()` in `_async.c` | 2.5h | Medium |
| 2.4 | Implement download signal handler (multi-fire + terminal detection) | 1.5h | Medium |
| 2.5 | Implement download timeout handler (fires DWNL_ERROR callback) | 0.5h | Low |
| 2.6 | Implement download state accessors (begin/end/abort/is_in_progress) | 1h | Low |
| 2.7 | Remove old Download code from `_async.c` | 1h | Low |
| 2.8 | Remove `DownloadProgress` subscription from BG thread | 0.5h | Low |
| 2.9 | Rewrite `downloadFirmware()` in `_api.c` | 1.5h | Medium |
| 2.10 | Update destructor in `_api.c` | 0.5h | Low |
| 2.11 | Extend `unregisterProcess()` guard in `_process.c` | 0.5h | Low |
| 2.12 | Update/rewrite download unit tests | 3–4h | High |
| 2.13 | Integration testing (multi-process, daemon reject, piggyback) | 2h | Medium |
| **Total** | | **~16h (2 days)** | |

### Post-Phase 2 State

After Phase 2:
- CheckForUpdate: ✅ on-demand thread (Phase 1)
- DownloadFirmware: ✅ on-demand thread (Phase 2)
- UpdateFirmware: ⬜ still on persistent BG thread (Phase 3)
- Persistent BG thread: still alive for UpdateProgress only

---

## 16. Open Items & Future Work

### 16.1 Resolved in this document

| Item | Resolution |
|------|-----------|
| On-demand vs persistent thread for download | **On-demand.** Zero cost when idle. Consistent with CheckForUpdate. |
| Fire-and-forget vs synchronous D-Bus call | **Synchronous.** Daemon reply gives accurate accept/reject to caller. |
| Where to enforce download concurrency | **Both.** Library: one thread per process. Daemon: one download per device. |
| Does library state affect other processes? | **No.** Static globals are per-process (copy-on-write). |
| Signal handler: quit on every signal or only terminal? | **Only terminal.** Fire callback on every signal, quit on COMPLETED/ERROR. |
| Filter signals by handler_id? | **No.** Daemon's accept/reject gates entry. All accepted clients get all signals. |
| Download timeout duration | **3600 seconds (1 hour).** Total elapsed. |
| Timeout callback | **Yes.** Fire `callback(0, DWNL_ERROR)` on timeout so client knows. |
| Block unregisterProcess() during download | **Yes.** Same session-state invariant as CheckForUpdate. |

### 16.2 Items for Phase 3+

| Item | Phase |
|------|-------|
| Migrate `updateFirmware()` to on-demand thread | Phase 3 |
| Remove persistent BG thread entirely | Phase 3 (after Update migration) |
| Remove `internal_system_init()` / `internal_system_deinit()` | Phase 3 |
| Remove `BackgroundThread` struct | Phase 3 |
| Remove `UpdateCbRegistry` | Phase 3 |
| Add `cancelDownloadFirmware()` API | Future |
| Stall-based timeout (no signal for N seconds) instead of total elapsed | Future |
| Make library constructor a true no-op | Phase 3 |

### 16.3 Production hardening

| Item | Priority | Notes |
|------|----------|-------|
| Thread-safe logging from worker thread | Medium | Worker and main thread both log — ensure `FWUPMGR_*` macros are thread-safe |
| ASan/TSan validation for download thread | High | Multi-fire callback pattern is more complex than single-fire |
| Coverity scan | High | New code must pass |
| Test with actual slow download (30 min) | Medium | Verify timeout doesn't trigger prematurely |
| Test daemon crash during download | High | Verify timeout fires error callback |
| Test with rapid progress signals (100 in 1 second) | Medium | Verify no queue overflow or missed callbacks |

---

## Appendix A: D-Bus Signal Introspection Reference (DownloadProgress)

```xml
<signal name='DownloadProgress'>
  <arg type='t' name='handlerId'/>       <!-- uint64: handler ID of original requester -->
  <arg type='s' name='firmwareName'/>    <!-- string: firmware filename being downloaded -->
  <arg type='u' name='percentage'/>      <!-- uint32: download progress 0-100 -->
  <arg type='s' name='status'/>          <!-- string: "INPROGRESS" or "COMPLETED" or "ERROR" -->
  <arg type='s' name='message'/>         <!-- string: human-readable status message -->
</signal>
```

GVariant signature: `(tsuss)`

Parsed by: `internal_parse_dwnl_signal_data()` in `rdkFwupdateMgr_async.c`

## Appendix B: D-Bus Method Return (DownloadFirmware)

```xml
<method name='DownloadFirmware'>
  <!-- Input -->
  <arg type='s' name='handlerId' direction='in'/>
  <arg type='s' name='firmwareName' direction='in'/>
  <arg type='s' name='firmwareUrl' direction='in'/>
  <arg type='s' name='rebootFlag' direction='in'/>

  <!-- Output (synchronous reply) -->
  <arg type='s' name='result' direction='out'/>     <!-- "RDKFW_DWNL_SUCCESS" or "RDKFW_DWNL_FAILED" -->
  <arg type='s' name='status' direction='out'/>     <!-- "INPROGRESS" or "COMPLETED" or "DWNL_ERROR" -->
  <arg type='s' name='message' direction='out'/>    <!-- Human-readable message -->
</method>
```

GVariant signature (reply): `(sss)`

**Daemon reply scenarios:**

| Scenario | result | status | message |
|----------|--------|--------|---------|
| New download started | `RDKFW_DWNL_SUCCESS` | `INPROGRESS` | `"Download started"` |
| Piggyback (same firmware) | `RDKFW_DWNL_SUCCESS` | `INPROGRESS` | `"Download already in progress"` |
| Firmware cached | `RDKFW_DWNL_SUCCESS` | `COMPLETED` | `"Firmware already available"` |
| Different firmware downloading | `RDKFW_DWNL_FAILED` | `DWNL_ERROR` | `"There is an Ongoing Firmware Download"` |
| Invalid handler ID | `RDKFW_DWNL_FAILED` | `DWNL_ERROR` | `"Invalid handler ID"` |

## Appendix C: Ordering Proof (Download-specific)

```
TIME    WORKER THREAD                           D-BUS DAEMON            FIRMWARE DAEMON
────    ─────────────                           ────────────            ───────────────

T1      g_main_context_new()
T2      g_main_loop_new()
T3      g_main_context_push_thread_default()
T4      g_bus_get_sync() → connection
T5      g_dbus_connection_signal_subscribe(DownloadProgress)

T6      g_dbus_connection_call_sync(DownloadFirmware)
        ← BLOCKS waiting for daemon reply ────────────►  Daemon receives request
                                                          Daemon checks IsDownloadInProgress
                                                          Daemon returns (sss) reply
        ← reply received ◄────────────────────────────

T7      Parse reply: daemon_accepted = true/false
T8      If rejected: init_failed=true, signal ready, goto cleanup
T9      Signal ready: is_ready = true, cond_signal

T10     Add 3600s timeout to context
T11     g_main_loop_run()
        ↓ blocked in poll()
                                                                       Download starts
T12                                               ← DownloadProgress(25%, INPROG)
        poll() returns, handler fires
        cbA(25, DWNL_IN_PROGRESS)
        return to loop ← NOT quitting

T13                                               ← DownloadProgress(50%, INPROG)
        cbA(50, DWNL_IN_PROGRESS)

T14                                               ← DownloadProgress(100%, COMPLETED)
        cbA(100, DWNL_COMPLETED)
        g_main_loop_quit() ← NOW quitting

T15     g_main_loop_run() returns
T16     internal_end_download()
T17     g_dbus_connection_signal_unsubscribe()
T18     g_object_unref(connection)
T19     g_main_context_pop_thread_default()
T20     g_main_loop_unref()
T21     g_main_context_unref()
T22     free(ctx->handle_key)
T23     free(ctx->firmware_name)
T24     free(ctx->firmware_url)
T25     free(ctx->reboot_flag)
T26     free(ctx->daemon_reject_message)
T27     destroy mutex, cond
T28     free(ctx)
T29     return NULL → thread exits

GUARANTEE: Subscribe at T5 before call_sync at T6.
           call_sync at T6 blocks until daemon replies.
           Signal loop at T11 only entered if daemon accepted.
           No signal can be missed.
```
