The app asks "is there a firmware update?" and immediately 
gets back a yes/no on whether the question was sent successfully. 
The actual answer arrives later via a callback when the daemon finishes checking.


┌─────────────────────────────────────────────────────┐
│  LAYER 1 — Your App  (example_app.c)                │
│  "I want to check for updates"                      │
├─────────────────────────────────────────────────────┤
│  LAYER 2 — Public API  (rdkFwupdateMgr_api.c)       │
│  "Let me validate, register, and send the request"  │
├─────────────────────────────────────────────────────┤
│  LAYER 3 — Internal Engine  (rdkFwupdateMgr_async.c)│
│  "I manage all pending requests and the signal"     │
├─────────────────────────────────────────────────────┤
│  LAYER 4 — Daemon  (separate process over D-Bus)    │
│  "I actually check XConf for firmware updates"      │
└─────────────────────────────────────────────────────┘


LAYER 1 — The App's Perspective
The app does four things in sequence:

// 1. Register — "Hi daemon, I'm ExampleApp"
FirmwareInterfaceHandle handle = registerProcess("ExampleApp", "1.0.0");
// handle = "12345"  ← daemon assigned this ID, library returned it as a string

// 2. Ask — "Is there a firmware update?" (returns immediately)
CheckForUpdateResult r = checkForUpdate(handle, on_firmware_event);
// r = CHECK_FOR_UPDATE_SUCCESS   ← means "question was sent OK"
//                                   NOT "update is available"

// 3. Wait — app does other things, callback fires when answer arrives
//    on_firmware_event() is called by library background thread

// 4. Leave — "Goodbye daemon, cleaning up"
unregisterProcess(handle);
handle = NULL;


LAYER 2 — What checkForUpdate() Actually Does
When the app calls checkForUpdate(handle, on_firmware_event), internally it does exactly three things:
Thing 1 — Validate
if (handle == NULL) return CHECK_FOR_UPDATE_FAIL;
if (callback == NULL) return CHECK_FOR_UPDATE_FAIL;

Nothing happens if you pass garbage in.


Thing 2 — Write in the notebook (Register callback)

internal_register_callback(handle, on_firmware_event);
```
Think of this as opening a notebook and writing:
```
Notebook (Registry):
┌──────┬───────────────┬──────────────────────┐
│ Slot │ Handle        │ Callback             │
├──────┼───────────────┼──────────────────────┤
│  0   │ "12345"       │ on_firmware_event    │  ← just written
│  1   │ IDLE          │ -                    │
│  2   │ IDLE          │ -                    │
└──────┴───────────────┴──────────────────────┘

This is done BEFORE sending the D-Bus call. Why? 
Because if the daemon responds so fast that the signal arrives before 
we write in the notebook, we'd miss it. Write first, then ask.


Thing 3 — Send the question to the daemon (fire-and-forget)

g_dbus_connection_call(
    connection,
    "org.rdkfwupdater.Interface",   // who to call
    "CheckForUpdate",               // what to ask
    g_variant_new("(s)", "12345"),  // pass our handle
    NULL, NULL, NULL                // ← these 3 NULLs = "don't wait for reply"
);
```
The message goes into the D-Bus socket and `checkForUpdate()` returns immediately. It doesn't wait. It doesn't block. Control goes back to the app in microseconds.

---

### LAYER 3 — The Internal Engine (The Most Important Part)

This layer has two components working together: a **registry** and a **background thread**.

#### The Registry — A Fixed Hotel Booking System
```
64 rooms. Each room is either EMPTY (IDLE) or OCCUPIED (PENDING).

When checkForUpdate() is called:
  Find an empty room → fill it with { handle="12345", callback=on_firmware_event }
  Mark it PENDING

When signal arrives:
  Find all PENDING rooms → fire their callbacks → mark IDLE again
  
  
 In C terms: 
 
 typedef struct {
    char                *handle_key;   // "12345" — who this belongs to
    UpdateEventCallback  callback;     // on_firmware_event — what to call
    CallbackEntryState   state;        // IDLE or PENDING
} CallbackEntry;

CallbackEntry entries[64];  // the hotel — 64 rooms max
pthread_mutex_t mutex;      // only one person can check in/out at a time


The mutex is like a single receptionist desk — 
only one operation at a time to prevent two threads from grabbing the same room simultaneously.


The Background Thread — The Buzzer Watcher
When the library loads (before main() even runs), it silently spawns a background thread:

__attribute__((constructor))   // runs automatically at library load
static void lib_init(void) {
    internal_system_init();    // starts the background thread
}


This thread does one thing forever:

void *background_thread_func(void *arg) {
    // Connect to D-Bus
    // Subscribe to "CheckForUpdateComplete" signal
    // Tell main thread "I'm ready"

    g_main_loop_run(loop);   // ← SLEEPS HERE, waiting for signals
                             //   uses zero CPU while sleeping
                             //   wakes up only when signal arrives
}

It's like a security guard sitting by a buzzer panel. Zero effort until a buzzer goes off.


When the Signal Arrives — The Two-Phase Dispatch
When the daemon finishes checking XConf and emits CheckForUpdateComplete,
 the background thread wakes up and runs dispatch_all_pending(). This has two phases:
 
 Phase 1 — Lock the notebook, take a snapshot, unlock
 
 pthread_mutex_lock(&registry.mutex);   // "receptionist desk is busy"

// Snapshot every PENDING entry into a local list
for each slot in registry:
    if slot.state == PENDING:
        snapshot[i] = { slot.callback, slot.handle }
        slot.state  = DISPATCHED   // mark done so nobody else fires it

pthread_mutex_unlock(&registry.mutex); // "desk is free again"


Phase 2 — Fire callbacks WITHOUT holding the lock

// Build the result from the signal data
FwUpdateEventData event_data = {
    .status            = FIRMWARE_AVAILABLE,
    .current_version   = "1.0.0",
    .available_version = "2.0.0",
    ...
};

// Call each app's callback
for each snapshot entry:
    snapshot[i].callback(snapshot[i].handle, &event_data);
    // ↑ this is on_firmware_event("12345", &event_data) in the app
```

**Why two phases? Why release the lock before calling callbacks?**

Imagine if we held the lock while calling the callback, and the app's callback tried to call `checkForUpdate()` again:
```
Background thread holds mutex
  → calls on_firmware_event()
    → app calls checkForUpdate() again
      → tries to lock mutex  ← DEADLOCK. Waiting forever.
```

Two-phase solves this: snapshot under lock, fire outside lock. Re-entrant calls are safe.

---

### LAYER 4 — The Daemon's Role

The daemon lives in a completely separate process. The library never directly reads firmware version info — it only talks to the daemon via D-Bus messages.
```
Library side                    D-Bus (IPC)              Daemon side
────────────                    ──────────               ───────────
checkForUpdate sends:
  CheckForUpdate("12345") ────────────────────────────►  receives handle
                                                          queries XConf server
                                                          gets firmware response
                                                          emits signal:
  on_check_complete_signal ◄──── CheckForUpdateComplete  {status, versions, ...}
  (background thread wakes)
```

One daemon, many apps. The daemon doesn't send a separate response to each app — it broadcasts one signal and the library sorts out which app gets which callback.

---

### The Complete Timeline
```
TIME
 │
 │  [Library loads]
 │    constructor runs → background thread starts → subscribes to D-Bus signal
 │
 │  [App: registerProcess("ExampleApp", "1.0.0")]
 │    D-Bus call → daemon → returns "12345"
 │    handle = "12345"
 │
 │  [App: checkForUpdate("12345", on_firmware_event)]
 │    registry slot 0: { "12345", on_firmware_event, PENDING }
 │    D-Bus fire-and-forget: CheckForUpdate("12345") → daemon
 │    returns CHECK_FOR_UPDATE_SUCCESS  ← app is unblocked here
 │
 │  [App: doing other work...]
 │  [Background thread: sleeping in event loop...]
 │  [Daemon: querying XConf server...]
 │
 │  [Daemon emits: CheckForUpdateComplete signal]
 │
 │  [Background thread wakes up]
 │    parse signal → { FIRMWARE_AVAILABLE, "1.0.0", "2.0.0" }
 │    Phase 1: snapshot slot 0, mark DISPATCHED, release lock
 │    Phase 2: on_firmware_event("12345", &event_data) ← app callback fires
 │    slot 0 → IDLE
 │
 │  [App callback on_firmware_event prints result]
 │    "FIRMWARE_AVAILABLE — update from 1.0.0 to 2.0.0"
 │
 │  [App: unregisterProcess("12345")]
 │    D-Bus call → daemon → daemon removes "12345" from tracking
 │    free("12345")
 │    handle = NULL
 │
 ▼
```

---

### The Three Rules That Make It Safe

**Rule 1 — Register before you send**
Callback goes in the notebook before the D-Bus call is fired. No race condition where the signal arrives before registration.

**Rule 2 — Never call user code while holding the lock**
Two-phase dispatch: snapshot under lock, invoke outside lock. No deadlock if the callback re-enters the library.

**Rule 3 — The handle is the key**
Every pending registration is identified by the handle string `"12345"`. When the signal arrives and multiple apps are waiting, each app's callback is found and fired by matching its handle. One signal, many callbacks, each app gets exactly its own result.

---

### In One Diagram
```
App                  Library (api.c)      Library (async.c)      Daemon
───                  ───────────────      ─────────────────      ──────
registerProcess() ──────────────────────────────────────────────► assigns "12345"
                  ◄────────────────────────────────────────────── returns "12345"

checkForUpdate()
  validate ✓
  register ─────────────────────────► slot[0]={ "12345", cb, PENDING }
  send D-Bus ──────────────────────────────────────────────────► CheckForUpdate("12345")
  return SUCCESS ◄──────────────────────────────────────────────  (immediately)

[app free to do other things]                [daemon checks XConf...]

                                                                   emit signal
                     bg thread wakes ◄──────────────────────────── CheckForUpdateComplete
                     parse signal data
                     snapshot slot[0]
                     release lock
                     cb("12345", data) ──► on_firmware_event()
                                           prints result
                     slot[0] = IDLE

unregisterProcess() ──────────────────────────────────────────────► removes "12345"
handle = NULL



The library is invisible infrastructure. The app only sees registerProcess, 
checkForUpdate, a callback, and unregisterProcess. Everything else — 
the thread, the registry, the signal subscription, the dispatch — 
is handled silently by the library on the app's behalf.














