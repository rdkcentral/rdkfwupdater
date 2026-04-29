# librdkFwupdateMgr — Design, Code Documentation & Logging Architecture

> **Version**: v1.0  
> **Date**: April 28, 2026  
> **Scope**: Pull Request documentation for the `librdkFwupdateMgr` shared library  
> **Author**: Senior Engineer — rdkFwupdateMgr Team

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [High-Level Architecture](#2-high-level-architecture)
3. [File Structure](#3-file-structure)
4. [API-by-API Design](#4-api-by-api-design)
   - 4.1 [registerProcess()](#41-registerprocessprocessname-libversion--firmwareinterfacehandle)
   - 4.2 [checkForUpdate()](#42-checkforupdatehandle-callback--checkforupdateresult)
   - 4.3 [downloadFirmware()](#43-downloadfirmwarehandle-fwdwnlreq-callback--downloadresult)
   - 4.4 [updateFirmware()](#44-updatefirmwarehandle-fwupdatereq-callback--updateresult)
   - 4.5 [unregisterProcess()](#45-unregisterprocesshandle)
5. [Internal Async Engine Design](#5-internal-async-engine-design)
   - 5.1 [Three Callback Registries](#51-three-callback-registries)
   - 5.2 [Background Thread](#52-background-thread)
   - 5.3 [Two-Phase Signal Dispatch (Deadlock Prevention)](#53-two-phase-signal-dispatch-deadlock-prevention)
   - 5.4 [Per-Call D-Bus Connections (Stateless Model)](#54-per-call-d-bus-connections-stateless-model)
6. [Memory Management](#6-memory-management)
7. [Thread Safety](#7-thread-safety)
8. [Error Handling Strategy](#8-error-handling-strategy)
9. [In-Code Documentation Guidelines](#9-in-code-documentation-guidelines)
   - 9.1 [rdkFwupdateMgr_process.c — Key Inline Comments](#91-rdkfwupdatemgr_processc--key-inline-comments)
   - 9.2 [rdkFwupdateMgr_api.c — Key Inline Comments](#92-rdkfwupdatemgr_apic--key-inline-comments)
   - 9.3 [rdkFwupdateMgr_async.c — Key Inline Comments](#93-rdkfwupdatemgr_asyncc--key-inline-comments)
10. [Logging Architecture](#10-logging-architecture)
    - 10.1 [Three Log Modules](#101-three-log-modules)
    - 10.2 [Macro Definitions](#102-macro-definitions)
    - 10.3 [Log Initialization Ownership](#103-log-initialization-ownership)
    - 10.4 [Includes Required](#104-includes-required)
    - 10.5 [Build Configuration (Makefile.am)](#105-build-configuration-makefileam)
    - 10.6 [Sample Log Output](#106-sample-log-output)

---

## 1. Executive Summary

`librdkFwupdateMgr` is a shared library (`.so`) that provides a simple C API for client applications to perform firmware updates on RDK devices. It communicates with the `rdkFwupdateMgr` daemon over D-Bus. The library hides all D-Bus complexity — clients just call functions and receive results via callbacks.

**In simple terms**: Think of it like ordering food through a delivery app.
- You place an order (`registerProcess`) and get an order number (`handle`).
- You ask "is my food ready?" (`checkForUpdate`) and get a notification later (`callback`).
- You say "deliver it" (`downloadFirmware`) and track the delivery (`progress callbacks`).
- You say "serve it" (`updateFirmware`) and watch it being plated (`progress callbacks`).
- When done, you close the app (`unregisterProcess`).

The library handles all the complicated behind-the-scenes communication (D-Bus IPC) with the daemon that actually does the firmware work.

---

## 2. High-Level Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    CLIENT APPLICATION                        │
│                 (e.g., example_plugin)                       │
│                                                              │
│  main()                                                      │
│    ├─ registerProcess("MyApp", "1.0")  → handle "12345"     │
│    ├─ checkForUpdate(handle, my_cb)    → returns immediately │
│    │    [waits on condvar]                                    │
│    │    ◄── my_cb(fwinfo) fires in BG thread                │
│    ├─ downloadFirmware(handle, req, dl_cb) → returns immed. │
│    │    [waits on condvar]                                    │
│    │    ◄── dl_cb(progress%, status) fires repeatedly        │
│    ├─ updateFirmware(handle, req, upd_cb)  → returns immed. │
│    │    [waits on condvar]                                    │
│    │    ◄── upd_cb(progress%, status) fires repeatedly       │
│    └─ unregisterProcess(handle)                              │
│                                                              │
├──────────────── librdkFwupdateMgr.so ───────────────────────┤
│                                                              │
│  PUBLIC LAYER (rdkFwupdateMgr_api.c, _process.c)           │
│    • Input validation                                        │
│    • D-Bus proxy creation (per-call, stateless)             │
│    • Fire-and-forget D-Bus method calls                     │
│    • Callback registration in internal registries            │
│                                                              │
│  ASYNC ENGINE (rdkFwupdateMgr_async.c)                      │
│    • 3 callback registries (Check, Download, Update)        │
│    • 1 background thread running GLib event loop            │
│    • D-Bus signal subscriptions (3 signals)                 │
│    • Two-phase dispatch (snapshot → invoke)                 │
│                                                              │
├──────────────── D-Bus (system bus) ──────────────────────────┤
│                                                              │
│  rdkFwupdateMgr DAEMON                                      │
│    • Registered as org.rdkfwupdater.Service                 │
│    • Processes method calls (Register, Check, Download,      │
│      Update, Unregister)                                     │
│    • Emits signals back (CheckForUpdateComplete,             │
│      DownloadProgress, UpdateProgress)                       │
└─────────────────────────────────────────────────────────────┘
```

### How D-Bus Fits In (For Newcomers)

D-Bus is a messaging system used on Linux devices for programs to talk to each other. Think of it like a shared phone line between applications:

- The **daemon** (server) sits on the phone line and has a well-known name: `org.rdkfwupdater.Service`
- The **library** (client) calls methods on the daemon (like dialing a number and asking a question)
- The **daemon** sends **signals** back (like broadcast notifications) when work is done
- The **library's background thread** listens for these signals and routes them to the right callback

---

## 3. File Structure

| File | Layer | Purpose |
|------|-------|---------|
| `include/rdkFwupdateMgr_client.h` | **Public API** | All types, enums, callbacks, and function declarations a client needs. This is the ONLY header clients include. |
| `src/rdkFwupdateMgr_process.c` | **Public** | `registerProcess()` and `unregisterProcess()` — synchronous D-Bus calls |
| `src/rdkFwupdateMgr_api.c` | **Public** | `checkForUpdate()`, `downloadFirmware()`, `updateFirmware()` — async fire-and-forget APIs |
| `src/rdkFwupdateMgr_async.c` | **Internal** | The async engine — registries, background thread, signal handlers, dispatch logic |
| `src/rdkFwupdateMgr_async_internal.h` | **Internal** | Internal types and declarations — NOT part of public API, NOT shipped to clients |
| `src/rdkFwupdateMgr_log.h` | **Internal** | `FWUPMGR_*` logging macros (library code uses these) |
| `examples/example_app.c` | **Example** | Reference client demonstrating the full register→check→download→flash→unregister workflow |

### What's Public vs Internal?

- **Public** (`include/` folder): Headers that client applications `#include`. These define the API contract. Changing these requires a version bump.
- **Internal** (`src/` folder): Implementation details. Clients never see these. We can change them freely without breaking clients.

---

## 4. API-by-API Design

### 4.1 `registerProcess(processName, libVersion)` → `FirmwareInterfaceHandle`

**What it does in plain English**: Tells the daemon "Hello, I'm a new client named [processName]. Please give me a session ID so I can do firmware operations."

**Returns**: A string like `"12345"` (the session ID). Returns `NULL` on failure.

#### Design Decisions

- **Synchronous** — blocks until daemon responds (~5-10ms). Registration is fast so blocking is acceptable.
- Creates a **fresh D-Bus proxy per call** (stateless, no persistent connection). This means each call to any API gets a different D-Bus sender ID, which is a key characteristic of this library model.
- Daemon returns a `uint64` handler_id; library converts it to a `malloc()`'d string (e.g., `"12345"`). String representation provides ABI stability.
- Calls `internal_system_init()` to start the background thread that will listen for daemon signals.
- If `malloc()` fails after registration succeeds on the daemon side, performs **best-effort cleanup** by calling `UnregisterProcess` on the daemon to avoid leaking a registration.

#### D-Bus Protocol

```
Method: RegisterProcess(s processName, s libVersion) → (t handler_id)
```

#### Flow

```
Client Thread                          Daemon
─────────────                          ──────
registerProcess("MyApp", "1.0")
  │
  ├─ Validate processName (not NULL, not empty, ≤256 chars)
  ├─ Validate libVersion (not NULL, ≤64 chars)
  ├─ Create D-Bus proxy → connect to system bus
  ├─ Call RegisterProcess("MyApp", "1.0") ──────────► Daemon receives call
  │                                                    │ Creates ProcessInfo
  │                                                    │ Assigns handler_id=12345
  │◄──── Returns handler_id=12345 ─────────────────────┘
  ├─ Convert 12345 → malloc'd string "12345"
  ├─ internal_system_init()
  │    ├─ Init 3 callback registries (mutexes + arrays)
  │    ├─ Create isolated GLib context + event loop
  │    ├─ Spawn background thread
  │    └─ Wait until background thread is ready (signal subscriptions live)
  └─ Return "12345" to caller
```

#### Lifecycle Impact

This is the **start** of the library's lifecycle. Before `registerProcess()`:
- No background thread exists
- No callback registries exist
- No D-Bus signal subscriptions exist

After `registerProcess()` returns successfully, the library is fully operational and ready for async API calls.

---

### 4.2 `checkForUpdate(handle, callback)` → `CheckForUpdateResult`

**What it does in plain English**: Asks the daemon "Is there new firmware available for this device?" The answer comes later through your callback — this function returns immediately.

**Returns**: `CHECK_FOR_UPDATE_SUCCESS` (request started) or `CHECK_FOR_UPDATE_FAIL` (couldn't even start).

#### Design Decisions

- **Non-blocking** (fire-and-forget) — returns immediately after sending the D-Bus message
- **Callback fires exactly once** — when the daemon finishes querying the XConf server (5-30 seconds later)
- The callback receives a `FwInfoData` struct with firmware version info and update details

#### The Connect → Register → Send Ordering

This is a critical design detail:

```
Step 1: Connect to D-Bus    ← Fail fast if daemon/bus is down
Step 2: Register callback    ← Now we're ready to receive the signal
Step 3: Send method call     ← Daemon starts working
```

**Why this order?**
- If we registered the callback first but D-Bus connect fails, we'd have a stale PENDING entry in the registry that would never be dispatched (no signal will ever arrive for it).
- If we sent the method first but hadn't registered the callback yet, the daemon might emit the signal before our callback is registered — the signal arrives, no matching entry found, result is silently lost.
- Connecting first, then registering, then sending gives us the safest ordering with the smallest race window.

#### D-Bus Protocol

```
Method:  CheckForUpdate(s handle) — fire-and-forget, no reply waited for
Signal:  CheckForUpdateComplete(t handler_id, i result, i status, s currentVer,
                                 s availableVer, s updateDetails, s message)
```

#### Flow

```
Client Thread              Background Thread              Daemon
─────────────              ─────────────────              ──────
checkForUpdate("12345", my_cb)
  │
  ├─ Validate handle, callback
  ├─ Connect to D-Bus
  ├─ Register my_cb in CheckForUpdate registry
  │    (slot state: IDLE → PENDING)
  ├─ Fire-and-forget: CheckForUpdate("12345") ──────────► Daemon receives
  └─ Return SUCCESS immediately                           │ Queries XConf...
                                                          │ (5-30 seconds)
  [client is free to do                                   │
   other work or wait]                                    │
                                                          │
                           ◄── CheckForUpdateComplete ────┘ (signal emitted)
                           on_check_complete_signal():
                             ├─ Parse GVariant → InternalSignalData
                             ├─ dispatch_all_pending():
                             │    Phase 1 (mutex held):
                             │      Snapshot PENDING entries
                             │      Mark → DISPATCHED
                             │    Phase 2 (no mutex):
                             │      Build FwInfoData from signal
                             │      Call my_cb(&fwinfo_data) ──► Client's callback runs
                             │      Reset slot → IDLE
                             └─ Cleanup signal data
```

#### What the Callback Receives

```c
typedef struct {
    char CurrFWVersion[64];         // e.g., "1.0.0"
    UpdateDetails *UpdateDetails;   // Non-NULL only if FIRMWARE_AVAILABLE
    CheckForUpdateStatus status;    // FIRMWARE_AVAILABLE, NOT_AVAILABLE, etc.
} FwInfoData;
```

**Important**: The `FwInfoData` pointer and all its contents are only valid DURING the callback invocation. If you need the data later, copy it (which is exactly what `example_app.c` does with `strncpy` to globals).

---

### 4.3 `downloadFirmware(handle, fwdwnlreq, callback)` → `DownloadResult`

**What it does in plain English**: Tells the daemon "Download this firmware file from the server." Returns immediately; your callback fires repeatedly with progress updates (0%, 25%, 50%, 75%, 100%).

**Returns**: `RDKFW_DWNL_SUCCESS` (download started) or `RDKFW_DWNL_FAILED` (couldn't start).

#### Design Decisions

- Same fire-and-forget pattern as `checkForUpdate()`
- **Key difference**: Callback fires **multiple times** (once per progress signal from daemon)
- Registry slot stays **ACTIVE** across all progress signals; only resets to IDLE on `DWNL_COMPLETED` or `DWNL_ERROR`
- Download URL can be `NULL`/empty string — daemon will use the URL from the XConf query

#### D-Bus Protocol

```
Method:  DownloadFirmware(s handle, s firmwareName, s downloadUrl, s TypeOfFirmware)
Signal:  DownloadProgress(t handler_id, s firmwareName, u progress%, s status, s message)
```

#### Flow

```
Client Thread              Background Thread              Daemon
─────────────              ─────────────────              ──────
downloadFirmware("12345", &req, my_dl_cb)
  │
  ├─ Validate handle, req, callback
  ├─ Connect to D-Bus
  ├─ Register my_dl_cb in Download registry
  │    (slot state: IDLE → ACTIVE)
  ├─ Fire-and-forget: DownloadFirmware(...) ─────────► Daemon starts download
  └─ Return SUCCESS immediately                        │
                                                       │ Download progress...
                           ◄── DownloadProgress(10%) ──┘
                           dispatch_all_dwnl_active():
                             Call my_dl_cb(10, IN_PROGRESS) ──► prints "10%"
                             (slot stays ACTIVE)

                           ◄── DownloadProgress(50%) ──
                             Call my_dl_cb(50, IN_PROGRESS) ──► prints "50%"

                           ◄── DownloadProgress(100%) ──
                             Call my_dl_cb(100, COMPLETED) ──► prints "100% done!"
                             Reset slot → IDLE (download finished)
```

---

### 4.4 `updateFirmware(handle, fwupdatereq, callback)` → `UpdateResult`

**What it does in plain English**: Tells the daemon "Flash this downloaded firmware onto the device's storage." This modifies the device firmware. Returns immediately; callback fires with progress.

**Returns**: `RDKFW_UPDATE_SUCCESS` (flash started) or `RDKFW_UPDATE_FAILED` (couldn't start).

#### Design Decisions

- Identical pattern to `downloadFirmware()` — ACTIVE slot, multiple callbacks, reset on terminal status
- `LocationOfFirmware` can be `NULL`/empty — daemon uses the default path from `/etc/device.properties`
- `rebootImmediately` is a `bool` in the struct but sent to daemon as string `"true"`/`"false"` (daemon D-Bus method expects string)
- This operation is **irreversible** once the flash starts — client should verify the firmware file first

#### D-Bus Protocol

```
Method:  UpdateFirmware(s handle, s firmwareName, s location, s type, s rebootImmediately)
Signal:  UpdateProgress(t handler_id, s firmwareName, i progress%, i status, s message)
```

---

### 4.5 `unregisterProcess(handle)`

**What it does in plain English**: Tells the daemon "I'm done, please clean up my registration." Also frees all library resources (background thread, registries, handle memory).

**Returns**: Nothing (`void`). This is a best-effort cleanup.

#### Design Decisions

- **Synchronous** — blocks until daemon responds
- **Best-effort** — if D-Bus call fails, local cleanup still happens (daemon may have already cleaned up on its own)
- Calls `internal_system_deinit()` **FIRST** — stops background thread, frees registries, destroys mutexes
- Then sends D-Bus `UnregisterProcess`, then `free(handle)`
- **Idempotent**: safe to call with `NULL` handle (no-op)
- Strictly validates handle string: must be pure decimal digits, no leading/trailing whitespace, no garbage characters

#### Why deinit Before D-Bus Call?

After unregister, the daemon won't send any more signals for this client. There's no point keeping the background thread alive. Shutting it down first ensures a clean `pthread_join()` without waiting for signals that will never arrive.

#### Flow

```
Client Thread                          Daemon
─────────────                          ──────
unregisterProcess("12345")
  │
  ├─ Validate handle (not NULL)
  ├─ Parse "12345" → uint64 12345
  ├─ internal_system_deinit()
  │    ├─ g_main_loop_quit() → background thread wakes up
  │    ├─ pthread_join() → wait for background thread to exit
  │    ├─ Free GLib resources (loop, context)
  │    ├─ Free download & update registries
  │    ├─ Free check registry (any leftover handle_key strings)
  │    └─ Destroy all mutexes
  ├─ Create D-Bus proxy
  ├─ Call UnregisterProcess(12345) ──────────────► Daemon removes ProcessInfo
  │◄──── Returns success=true ──────────────────┘
  ├─ free(handle)   ← The "12345" string is freed
  └─ Return
```

---

## 5. Internal Async Engine Design

### 5.1 Three Callback Registries

The library maintains **three independent registries**, one per async API. They are separate because each has a different callback lifecycle:

| Registry | C Type | Slot Lifecycle | How Many Times Callback Fires |
|----------|--------|----------------|-------------------------------|
| `CallbackRegistry` | `g_registry` | IDLE → PENDING → DISPATCHED → IDLE | **Once** (check result) |
| `DwnlCallbackRegistry` | `g_dwnl_registry` | IDLE → ACTIVE → IDLE | **Multiple** (progress updates) |
| `UpdateCbRegistry` | `g_update_registry` | IDLE → ACTIVE → IDLE | **Multiple** (progress updates) |

Each registry is a **fixed-size array** of `MAX_PENDING_CALLBACKS` (30) slots. This means at most 30 concurrent pending callbacks across all clients of any given type.

Each slot holds:
- **State**: IDLE, PENDING, ACTIVE, DISPATCHED, or TIMED_OUT
- **handle_key**: `strdup()`'d copy of the app's handle string
- **callback**: Function pointer to the app's callback
- **registered_time**: Timestamp for timeout detection

Each registry has its **own `pthread_mutex_t`** — so checkForUpdate registrations don't block download progress dispatching.

#### State Machine

```
CheckForUpdate slot:
  IDLE ──(checkForUpdate called)──► PENDING ──(signal arrives)──► DISPATCHED ──► IDLE
                                        └──(timeout)──► TIMED_OUT ──► IDLE

Download/Update slot:
  IDLE ──(download/updateFirmware called)──► ACTIVE ──(COMPLETED/ERROR)──► IDLE
                                                │
                                                │  (fires callback on EVERY
                                                │   progress signal while ACTIVE)
                                                │
                                                └──(timeout)──► TIMED_OUT ──► IDLE
```

### 5.2 Background Thread

A single `pthread` runs a private GLib event loop for the entire lifetime of the library (from `registerProcess()` to `unregisterProcess()`).

#### Thread Startup Sequence

```
1. g_main_context_push_thread_default()
   └─ Creates an ISOLATED GLib context for this thread
      (won't interfere with app's own GLib loop if it has one)

2. g_bus_get_sync(G_BUS_TYPE_SYSTEM, ...)
   └─ Connect to system D-Bus (this is the background thread's
      OWN connection — different from the per-call connections
      used by the public API functions)

3. g_dbus_connection_signal_subscribe() × 3
   ├─ CheckForUpdateComplete → on_check_complete_signal()
   ├─ DownloadProgress       → on_download_progress_signal()
   └─ UpdateProgress         → on_update_progress_signal()

4. g_bg_thread.running = true
   └─ Main thread sees this and stops spin-waiting

5. g_main_loop_run()
   └─ BLOCKS here until internal_system_deinit() calls g_main_loop_quit()
      GLib dispatches signal callbacks within this loop.
```

#### Why Spin-Wait Instead of Condvar?

The main thread waits for the background thread to be ready by polling `g_bg_thread.running` every 100ms. A condvar would be slightly more elegant, but:

1. The wait is typically <100ms (thread starts fast)
2. Adding a condvar adds complexity and another resource to manage/destroy
3. This only happens **once per library lifetime** (at `registerProcess` time)
4. Max wait: 50 × 100ms = 5 seconds — if the thread hasn't started by then, something is seriously wrong

### 5.3 Two-Phase Signal Dispatch (Deadlock Prevention)

All three signal handlers use the same two-phase pattern. This is the most important design detail in the async engine.

#### The Problem

If we held the registry mutex while invoking a callback, and that callback called `checkForUpdate()` again, it would try to lock the same mutex → **deadlock**.

#### The Solution

```
PHASE 1 — Snapshot (mutex HELD):
  ├─ Scan registry for matching entries (PENDING or ACTIVE)
  ├─ Copy callback pointer + handle into a local stack array (snapshot)
  ├─ Mark slots appropriately (DISPATCHED for check, leave ACTIVE for download/update)
  └─ RELEASE mutex

PHASE 2 — Invoke (NO mutex held):
  ├─ Build result struct from signal data
  ├─ For each snapshot entry:
  │    ├─ Call callback(result_data)
  │    └─ If terminal state (COMPLETED/ERROR): re-lock mutex, reset slot → IDLE, unlock
  └─ Done
```

**Why is this safe?**
- During Phase 2, the mutex is released, so if a callback calls `checkForUpdate()` → `internal_register_callback()`, it can acquire the mutex without deadlock.
- The snapshot is a local stack array, so even if the registry changes during Phase 2, our snapshot is stable.

### 5.4 Per-Call D-Bus Connections (Stateless Model)

Each public API call (`registerProcess`, `checkForUpdate`, etc.) creates a **fresh D-Bus connection and proxy** for that single call, then immediately releases it via `g_object_unref()`.

#### Implications

- **No persistent connection** between the client and daemon at the API layer
- Each call may get a **different D-Bus sender ID** (e.g., `:1.140` for register, `:1.141` for checkForUpdate, `:1.145` for unregister)
- The daemon **cannot rely on sender ID** to identify a client across calls — only the `handler_id` (the numeric handle) is stable
- This is why the daemon's `UnregisterProcess` handler doesn't validate sender-ID ownership — it would always fail because unregister comes from a different sender than register

The background thread has its **OWN persistent connection** (for signal subscriptions), but the API-calling thread uses ephemeral connections.

---

## 6. Memory Management

| Resource | Owner | Allocation | Deallocation |
|----------|-------|------------|--------------|
| `FirmwareInterfaceHandle` (the `"12345"` string) | Library | `malloc()` in `registerProcess()` | `free()` in `unregisterProcess()` |
| Registry `handle_key` entries | Async engine | `strdup()` on callback registration | `free()` on slot reset to IDLE |
| D-Bus proxy/connection (per API call) | Caller's stack | `g_bus_get_sync()` + `g_dbus_proxy_new_sync()` | `g_object_unref()` at end of each API function |
| D-Bus connection (background thread) | Background thread | `g_bus_get_sync()` in thread func | `g_object_unref()` on thread exit |
| GMainLoop, GMainContext | Async engine | `g_main_loop_new()` / `g_main_context_new()` | `g_main_loop_unref()` / `g_main_context_unref()` in `deinit()` |
| `FwInfoData` + `UpdateDetails` in dispatch | Stack-allocated | `dispatch_all_pending()` local variables | Automatic (function returns) |
| `InternalSignalData` strings | Parse function | `strdup()` from GVariant data | `free()` in `internal_cleanup_signal_data()` |

### Rules for Client Developers

1. **Never `free()` the handle yourself.** Call `unregisterProcess()` and it handles everything.
2. **Copy callback data if you need it later.** The `FwInfoData*` pointer in your callback is only valid during the callback invocation. Use `strncpy()` to save values to your own buffers.
3. **Don't call library APIs from inside a callback.** The callback runs in the background thread. Re-entering the library is technically safe (due to two-phase dispatch) for `checkForUpdate`-style calls, but it's better practice to signal your main thread and make calls from there.

---

## 7. Thread Safety

| Operation | Thread-Safe? | Notes |
|-----------|-------------|-------|
| `registerProcess()` | Yes | Stateless per-call; GDBus sync calls are thread-safe |
| `unregisterProcess()` | Per-handle | Don't unregister the same handle from two threads simultaneously |
| `checkForUpdate()` | Yes | Registry mutex protects slot allocation |
| `downloadFirmware()` | Yes | Separate registry with its own mutex |
| `updateFirmware()` | Yes | Separate registry with its own mutex |
| Callbacks | N/A | Fire in background thread; app must use its own synchronization (mutex + condvar) to coordinate with main thread |

### Callback Threading Model

```
Main Thread                    Background Thread
───────────                    ─────────────────
                               on_check_complete_signal()
                                 └─ dispatch_all_pending()
                                      └─ your_callback(&fwinfo)  ← RUNS HERE
                                           ├─ Copy data to globals
                                           ├─ pthread_mutex_lock(&your_mutex)
                                           ├─ your_done_flag = 1
                                           ├─ pthread_cond_signal(&your_cond)
                                           └─ pthread_mutex_unlock(&your_mutex)

pthread_mutex_lock(&your_mutex)
while (!your_done_flag)
    pthread_cond_timedwait(...)  ← WAKES UP HERE
pthread_mutex_unlock(&your_mutex)
// Now use the copied data safely
```

This is exactly how `example_app.c` works — see `g_check_mutex`/`g_check_cond`/`g_check_done`.

---

## 8. Error Handling Strategy

The library follows a defense-in-depth approach:

### Layer 1: Input Validation (API Boundary)

Every public function validates ALL parameters before touching D-Bus:
- NULL checks on handles, callbacks, request structs
- Empty string checks
- Length limit checks (process name ≤256 chars, version ≤64 chars)
- Numeric validity for handle parsing in `unregisterProcess()` (uses `strtoull` with strict `endptr` checking)

### Layer 2: D-Bus Connection Failures

- Connection attempt happens BEFORE callback registration (Connect → Register → Send pattern)
- If D-Bus is down, function returns error immediately with no stale registry entries

### Layer 3: Daemon Errors

- D-Bus errors from the daemon are caught via `GError`
- Error message is logged via `FWUPMGR_ERROR`
- Error is propagated as a return code to the caller

### Layer 4: Resource Leak Prevention

- In `registerProcess()`: If registration succeeds on daemon but `malloc()` fails locally, a cleanup proxy is created to send `UnregisterProcess` to prevent leaking the registration
- In `unregisterProcess()`: Handle memory is freed regardless of whether the D-Bus call succeeds
- In all API functions: D-Bus connections/proxies are freed on all code paths (success and error)

### Layer 5: Best-Effort Cleanup

- `unregisterProcess()` is best-effort: if D-Bus call fails, local cleanup still happens
- This handles the case where the daemon has already crashed or been restarted

---

## 9. In-Code Documentation Guidelines

The source code already has extensive inline comments. Below are the **key documentation points** that every reviewer should understand, organized by file.

### 9.1 `rdkFwupdateMgr_process.c` — Key Inline Comments

#### `registerProcess()` — Why `internal_system_init()` is here

```c
/*
 * Start the background listener thread NOW (not at library load time).
 *
 * WHY HERE and not in __attribute__((constructor))?
 *   The constructor approach is #if 0'd out in rdkFwupdateMgr_api.c.
 *   We start the background thread at register time because:
 *   1. The handle must exist before any async API can be called
 *   2. The background thread needs a live D-Bus connection — doing it
 *      too early risks connecting before the system bus is ready
 *      (common during early boot on embedded devices)
 *   3. It pairs naturally with internal_system_deinit() in unregisterProcess()
 *
 * This initializes:
 *   - CallbackRegistry (checkForUpdate callbacks)
 *   - DwnlCallbackRegistry (download callbacks)
 *   - UpdateCbRegistry (update callbacks)
 *   - BackgroundThread (GLib event loop, D-Bus signal subscriptions)
 */
```

#### `unregisterProcess()` — Why `internal_system_deinit()` comes before D-Bus call

```c
/*
 * Stop the background listener thread BEFORE sending UnregisterProcess.
 *
 * WHY BEFORE the D-Bus call?
 *   After unregister, the daemon won't send us any more signals.
 *   There's no point keeping the background thread alive.
 *   Shutting down first ensures a clean pthread_join() without
 *   waiting for signals that will never arrive.
 *
 * This tears down:
 *   - g_main_loop_quit() → background thread exits g_main_loop_run()
 *   - pthread_join() → waits for clean exit
 *   - Frees all 3 registries (any leftover handle_key strings)
 *   - Destroys all mutexes
 */
```

### 9.2 `rdkFwupdateMgr_api.c` — Key Inline Comments

#### Connect → Register → Send ordering rationale

```c
/*
 * ORDERING MATTERS: Connect → Register → Send
 *
 * We could register the callback first, but then if D-Bus connection
 * fails, we'd have a stale PENDING entry in the registry that would
 * never be dispatched (no signal will ever arrive for it).
 *
 * We could send first, but then the daemon might emit the signal
 * before our callback is registered → signal arrives, no matching
 * entry found, result is silently lost.
 *
 * The correct order is:
 *   1. Connect (fail fast if daemon/D-Bus is down)
 *   2. Register callback (now we're ready to receive)
 *   3. Send the method call (daemon starts working)
 *
 * The window between register and send is microseconds — acceptably
 * small race window where the callback slot exists but the daemon
 * hasn't been asked yet.
 */
```

### 9.3 `rdkFwupdateMgr_async.c` — Key Inline Comments

#### Two-Phase Dispatch — Why we release the mutex before calling callbacks

```c
/*
 * TWO-PHASE DESIGN — avoids deadlock:
 *
 *   PHASE 1 (mutex held):
 *     Scan registry → snapshot all PENDING entries into local array.
 *     Mark each found entry as DISPATCHED.
 *     Release mutex.
 *
 *   PHASE 2 (mutex released):
 *     Build FwInfoData from signal_data.
 *     Invoke each snapshot callback: callback(&fwinfo_data)
 *     Re-acquire mutex briefly to reset each slot to IDLE.
 *
 * WHY RELEASE BEFORE CALLING CALLBACKS?
 *   If a callback called checkForUpdate() again, it would call
 *   internal_register_callback() which tries to lock the same mutex
 *   → deadlock. Releasing first makes re-entrant use safe.
 */
```

#### Background thread spin-wait rationale

```c
/*
 * Spin-wait for background thread to set running=true.
 * Max wait: 50 × 100ms = 5 seconds.
 *
 * WHY SPIN-WAIT instead of condvar?
 *   1. The wait is typically <100ms (thread starts fast)
 *   2. Adding a condvar adds complexity and another resource to manage
 *   3. This only happens once per library lifetime
 *   4. 100ms sleep granularity is fine for a one-time init
 *
 * Ensures D-Bus signal subscription is live before checkForUpdate()
 * can send a D-Bus method call — prevents missing the response signal.
 */
```

---

## 10. Logging Architecture

### 10.1 Three Log Modules

The system uses three distinct RDK_LOGGER modules so log output can be filtered by component:

| Module Name | Macro Prefix | Used By | Log Tag in Output |
|------------|--------------|---------|-------------------|
| `LOG.RDK.FWUPMGR` | `FWUPMGR_*` | Library code (`_process.c`, `_api.c`, `_async.c`) | `[FWUPMGR]` |
| `LOG.RDK.FWUPG` | `SWLOG_*` | Daemon code (`rdkv_dbus_server.c`, `rdkv_main.c`, etc.) | `[FWUPG]` |
| `LOG.RDK.EXAMPLE` | `EXAMPLE_*` | Example app (`example_app.c`) | `[EXAMPLE]` |

**Why three separate modules?** So you can filter logs in production:
- To see only library-side issues: `grep "\[FWUPMGR\]" /opt/logs/rdkFwupdateMgr.log`
- To see only daemon-side issues: `grep "\[FWUPG\]" /opt/logs/rdkFwupdateMgr.log`
- To see only client app issues: `grep "\[EXAMPLE\]" /opt/logs/rdkFwupdateMgr.log`

### 10.2 Macro Definitions

#### Library Macros — `rdkFwupdateMgr_log.h`

```c
/* ── Base macro — callers provide their own module name ── */
#define FWUPMGR_LOG(level, module, format, ...) \
    RDK_LOG(level, module, format, ##__VA_ARGS__)

/* ── Library convenience macros ── */
/* Used in rdkFwupdateMgr_process.c, rdkFwupdateMgr_api.c, rdkFwupdateMgr_async.c */
#define FWUPMGR_TRACE(format, ...) FWUPMGR_LOG(RDK_LOG_TRACE1, "LOG.RDK.FWUPMGR", format, ##__VA_ARGS__)
#define FWUPMGR_DEBUG(format, ...) FWUPMGR_LOG(RDK_LOG_DEBUG,  "LOG.RDK.FWUPMGR", format, ##__VA_ARGS__)
#define FWUPMGR_INFO(format, ...)  FWUPMGR_LOG(RDK_LOG_INFO,   "LOG.RDK.FWUPMGR", format, ##__VA_ARGS__)
#define FWUPMGR_WARN(format, ...)  FWUPMGR_LOG(RDK_LOG_WARN,   "LOG.RDK.FWUPMGR", format, ##__VA_ARGS__)
#define FWUPMGR_ERROR(format, ...) FWUPMGR_LOG(RDK_LOG_ERROR,  "LOG.RDK.FWUPMGR", format, ##__VA_ARGS__)
#define FWUPMGR_FATAL(format, ...) FWUPMGR_LOG(RDK_LOG_FATAL,  "LOG.RDK.FWUPMGR", format, ##__VA_ARGS__)
```

#### Example App Macros — `example_app.c`

```c
/* Reuses the FWUPMGR_LOG base macro but with a different module name */
#define EXAMPLE_DEBUG(format, ...) FWUPMGR_LOG(RDK_LOG_DEBUG, "LOG.RDK.EXAMPLE", format, ##__VA_ARGS__)
#define EXAMPLE_INFO(format, ...)  FWUPMGR_LOG(RDK_LOG_INFO,  "LOG.RDK.EXAMPLE", format, ##__VA_ARGS__)
#define EXAMPLE_WARN(format, ...)  FWUPMGR_LOG(RDK_LOG_WARN,  "LOG.RDK.EXAMPLE", format, ##__VA_ARGS__)
#define EXAMPLE_ERROR(format, ...) FWUPMGR_LOG(RDK_LOG_ERROR, "LOG.RDK.EXAMPLE", format, ##__VA_ARGS__)
```

#### Non-RDK_LOGGER Fallback

When `RDK_LOGGER` is **not** defined (unit tests, standalone development builds), macros fall back to `fprintf`:

```c
#define FWUPMGR_LOG(level, module, FORMAT...) fprintf(stderr, "[%s] " FORMAT, module)
```

This means logging works everywhere — just with different backends depending on the build configuration.

#### How to Define Your Own Module (For New Client Apps)

If you're writing a new client application (not using example_app.c), define your own macros:

```c
#include "rdkFwupdateMgr_log.h"      // Get FWUPMGR_LOG base macro
#include "rdkv_cdl_log_wrapper.h"     // Get log_init(), log_exit()

// Define your own module — logs will appear as [MYAPP]
#define MYAPP_INFO(fmt, ...)  FWUPMGR_LOG(RDK_LOG_INFO,  "LOG.RDK.MYAPP", fmt, ##__VA_ARGS__)
#define MYAPP_ERROR(fmt, ...) FWUPMGR_LOG(RDK_LOG_ERROR, "LOG.RDK.MYAPP", fmt, ##__VA_ARGS__)

int main(void) {
    log_init();                       // Initialize RDK logger
    MYAPP_INFO("Starting up\n");
    // ... use library APIs ...
    log_exit();                       // Shutdown RDK logger
}
```

### 10.3 Log Initialization Ownership

**The library does NOT own the log lifecycle.** The host application (whatever links to `librdkFwupdateMgr.so`) is responsible for calling:

```c
log_init();     // BEFORE any library call (typically first line of main())
log_exit();     // AFTER unregisterProcess() (typically last line before return)
```

Both `log_init()` and `log_exit()` are provided by `rdkv_cdl_log_wrapper.h` (in `common_utilities/utils/`).

| Build Config | `log_init()` does | `log_exit()` does |
|-------------|-------------------|-------------------|
| `RDK_LOGGER` defined | Calls `rdk_logger_init()` with config file | Calls `rdk_logger_deinit()` |
| `RDK_LOGGER` not defined | No-op | No-op |

**Why doesn't the library call `log_init()`?**
- A process should only call `log_init()` once. If the library called it, and the app also called it, that's a double-init which may cause issues.
- The app knows when it's ready to start logging. The library shouldn't make that decision.

### 10.4 Includes Required

#### For library source files (`_process.c`, `_api.c`, `_async.c`)

```c
#include "rdkFwupdateMgr_log.h"   // Provides FWUPMGR_* macros
```

That's all — the log header internally includes `rdkv_cdl_log_wrapper.h` and (if `RDK_LOGGER` is defined) `rdk_debug.h`.

#### For client applications

```c
#include "rdkFwupdateMgr_client.h"    // Public API (types, functions)
#include "rdkFwupdateMgr_log.h"       // FWUPMGR_LOG base macro (for defining your own module)
#include "rdkv_cdl_log_wrapper.h"     // log_init(), log_exit()
```

### 10.5 Build Configuration (Makefile.am)

The `example_plugin` target needs include paths for both log headers and link flags for the logger libraries:

```makefile
# Include paths
example_plugin_CFLAGS += -I${top_srcdir}/librdkFwupdateMgr/src      # rdkFwupdateMgr_log.h
example_plugin_CFLAGS += -I${top_srcdir}/common_utilities/utils      # rdkv_cdl_log_wrapper.h

# Link flags
example_plugin_LDADD  += -lfwutils -lrdkloggers                     # log_init/log_exit implementations
```

### 10.6 Sample Log Output

Below is what you'd see in `/opt/logs/rdkFwupdateMgr.log` during a typical firmware update workflow. Notice how the three tags (`[EXAMPLE]`, `[FWUPMGR]`, `[FWUPG]`) make it easy to trace what's happening at each layer:

```
[EXAMPLE] Application starting, PID: 1234
[EXAMPLE] STEP 1: Register with firmware daemon
[FWUPMGR] registerProcess() called
[FWUPMGR]   processName: 'ExampleApp'
[FWUPMGR]   libVersion:  '1.0.0'
[FWUPMGR] D-Bus proxy created successfully
[FWUPMGR] Calling RegisterProcess D-Bus method...
[FWUPG]   [D-BUS] RegisterProcess received from ':1.140'
[FWUPG]   [PROCESS_TRACKING] New client registered: ExampleApp (handler=12345)
[FWUPMGR] Registration successful
[FWUPMGR]   handler_id: 12345
[FWUPMGR] Handle created: '12345'
[FWUPMGR] internal_system_init: begin
[FWUPMGR] background_thread: starting
[FWUPMGR] background_thread: subscribed to CheckForUpdateComplete (id=1)
[FWUPMGR] background_thread: subscribed to DownloadProgress (id=2)
[FWUPMGR] background_thread: subscribed to UpdateProgress (id=3)
[FWUPMGR] internal_system_init: ready
[EXAMPLE] Registered successfully
[EXAMPLE]   Handle: '12345'
[EXAMPLE] STEP 2: Check for firmware updates
[EXAMPLE]   Calling checkForUpdate()...
[FWUPMGR] checkForUpdate: handle='12345'
[FWUPMGR] internal_register_callback: registered handle='12345'
[FWUPMGR] checkForUpdate: D-Bus call sent, returning SUCCESS.
[FWUPG]   [D-BUS] CheckForUpdate received for handler 12345
[FWUPG]   [XCONF] Querying XConf server...
[FWUPG]   [XCONF] Response: firmware_v2.bin available
[FWUPG]   [SIGNAL] Emitting CheckForUpdateComplete
[FWUPMGR] on_check_complete_signal: received
[FWUPMGR] dispatch_all_pending: 1 callback(s) to fire
[FWUPMGR] dispatch_all_pending: invoking callback for handle='12345'
[EXAMPLE] checkForUpdate Callback Received
[EXAMPLE]   Status Code: FIRMWARE_AVAILABLE (0)
[EXAMPLE]   Current FW Version: 1.0.0
[EXAMPLE]   Available Version: 2.0.0
[EXAMPLE] Firmware check data saved. Main thread will proceed.
[EXAMPLE] STEP 3: Download firmware image
[FWUPMGR] downloadFirmware: handle='12345' firmware='firmware_v2.bin'
[FWUPMGR] internal_dwnl_register_callback: registered handle='12345'
[FWUPMGR] downloadFirmware: D-Bus call sent, returning SUCCESS.
[FWUPMGR] on_download_progress_signal: progress=25% status='INPROGRESS'
[EXAMPLE]   Download:  25%  DWNL_IN_PROGRESS
[FWUPMGR] on_download_progress_signal: progress=50% status='INPROGRESS'
[EXAMPLE]   Download:  50%  DWNL_IN_PROGRESS
[FWUPMGR] on_download_progress_signal: progress=100% status='COMPLETED'
[EXAMPLE]   Download: 100%  DWNL_COMPLETED
[EXAMPLE]   Download completed successfully!
```

---

## Appendix A: Complete Sequence Diagram

```
  Main Thread              Library BG Thread         Daemon Process
  ───────────              ─────────────────         ──────────────

  [STEP 1: Register]
  registerProcess("ExampleApp", "1.0.0")
    │───── D-Bus: RegisterProcess ──────────────────► │
    │◄──── Returns handler_id=12345 ─────────────────┤
  g_handle = "12345"
  internal_system_init() starts BG thread
                           │ subscribe CheckForUpdateComplete
                           │ subscribe DownloadProgress
                           │ subscribe UpdateProgress
                           │ running = true
                           │ g_main_loop_run() ← BLOCKS

  [STEP 2: Check for Update]
  checkForUpdate("12345", on_check_cb)
    │── register callback in g_registry
    │───── D-Bus: CheckForUpdate("12345") ──────────► │
    │◄──── returns immediately                        │ query XConf...
    │ waiting on condvar...                           │
    │                                                 │ (5-30 seconds)
    │                       ◄── CheckForUpdateComplete ┤
    │                       on_check_complete_signal(): │
    │                         dispatch_all_pending()    │
    │                           on_check_cb(&fwinfo) ──►│
    │                             signal condvar ───────►│
    │ condvar wakes up!                                  │
    │ read g_check_status, g_fw_filename, etc.

  [STEP 3: Download]
  downloadFirmware("12345", &req, on_dl_cb)
    │── register callback in g_dwnl_registry
    │───── D-Bus: DownloadFirmware(...) ────────────► │
    │◄──── returns immediately                        │ downloading...
    │ waiting on condvar...                           │
    │                       ◄── DownloadProgress(25%) ─┤
    │                       on_dl_cb(25, IN_PROGRESS)   │
    │                       ◄── DownloadProgress(50%) ─┤
    │                       on_dl_cb(50, IN_PROGRESS)   │
    │                       ◄── DownloadProgress(100%) ┤
    │                       on_dl_cb(100, COMPLETED)    │
    │                         signal condvar ──────────►│
    │ condvar wakes up!

  [STEP 4: Flash]
  updateFirmware("12345", &req, on_upd_cb)
    │── register callback in g_update_registry
    │───── D-Bus: UpdateFirmware(...) ──────────────► │
    │◄──── returns immediately                        │ flashing...
    │ waiting on condvar...                           │
    │                       ◄── UpdateProgress(50%) ──┤
    │                       on_upd_cb(50, IN_PROGRESS)  │
    │                       ◄── UpdateProgress(100%) ─┤
    │                       on_upd_cb(100, COMPLETED)   │
    │                         signal condvar ──────────►│
    │ condvar wakes up!

  [STEP 5: Unregister]
  unregisterProcess("12345")
    │── internal_system_deinit()
    │     g_main_loop_quit() ──────────────► BG thread exits
    │     pthread_join() ◄──────────────────┘
    │     free registries, destroy mutexes
    │───── D-Bus: UnregisterProcess(12345) ─────────► │
    │◄──── Returns success=true ────────────────────┤
    │ free("12345")
    │ done.
```

---

## Appendix B: D-Bus Interface Summary

**Service**: `org.rdkfwupdater.Service`  
**Object Path**: `/org/rdkfwupdater/Service`  
**Interface**: `org.rdkfwupdater.Interface`

### Methods (Client → Daemon)

| Method | Signature | Description |
|--------|-----------|-------------|
| `RegisterProcess` | `(ss) → (t)` | Register client. Returns handler_id. |
| `UnregisterProcess` | `(t) → (b)` | Unregister client. Returns success. |
| `CheckForUpdate` | `(s)` | Fire-and-forget. No reply. |
| `DownloadFirmware` | `(ssss)` | Fire-and-forget. No reply. |
| `UpdateFirmware` | `(sssss)` | Fire-and-forget. No reply. |

### Signals (Daemon → Client)

| Signal | Signature | Description |
|--------|-----------|-------------|
| `CheckForUpdateComplete` | `(tiissss)` | Firmware check result. Fires once. |
| `DownloadProgress` | `(tsuss)` | Download progress. Fires repeatedly. |
| `UpdateProgress` | `(tsiis)` | Flash progress. Fires repeatedly. |
