# librdkFwupdateMgr — API & Design Guide

**Library:** `librdkFwupdateMgr.so`  
**Header:** `rdkFwupdateMgr_client.h`  
**Date:** April 2026  
**Audience:** Anyone — developers, testers, architects, or anyone curious about how this library works.

---

## Table of Contents

1. [What Is This Library?](#1-what-is-this-library)
2. [The Big Picture — How It All Fits Together](#2-the-big-picture--how-it-all-fits-together)
3. [Analogy: The Post Office](#3-analogy-the-post-office)
4. [API Reference](#4-api-reference)
   - [registerProcess()](#41-registerprocess)
   - [checkForUpdate()](#42-checkforupdate)
   - [downloadFirmware()](#43-downloadfirmware)
   - [updateFirmware()](#44-updatefirmware)
   - [unregisterProcess()](#45-unregisterprocess)
5. [The On-Demand Worker Thread Model](#5-the-on-demand-worker-thread-model)
   - [What Problem Does It Solve?](#51-what-problem-does-it-solve)
   - [How It Works — The Restaurant Analogy](#52-how-it-works--the-restaurant-analogy)
   - [Technical Deep Dive](#53-technical-deep-dive)
   - [The Condvar Handshake](#54-the-condvar-handshake)
   - [Why On-Demand Instead of Persistent?](#55-why-on-demand-instead-of-persistent)
6. [Complete Workflow Example](#6-complete-workflow-example)
7. [Thread Safety — How the Library Keeps Things Safe](#7-thread-safety--how-the-library-keeps-things-safe)
8. [Memory Ownership — Who Frees What?](#8-memory-ownership--who-frees-what)
9. [Error Handling — What Can Go Wrong?](#9-error-handling--what-can-go-wrong)
10. [Callbacks — The Rules](#10-callbacks--the-rules)
11. [Quick Reference Card](#11-quick-reference-card)

---

## 1. What Is This Library?

`librdkFwupdateMgr` is a **C shared library** that lets applications manage firmware updates on RDK devices. It talks to a background system service (the **firmware daemon**) over D-Bus to:

- **Check** if new firmware is available
- **Download** firmware from a server
- **Flash** (install) firmware onto the device

Think of it as a remote control for firmware updates. Your app tells the library what to do, the library talks to the daemon, and the daemon does the heavy lifting.

```
┌──────────────┐       ┌───────────────────┐       ┌──────────────────┐
│   Your App   │──────▶│ librdkFwupdateMgr │──────▶│ Firmware Daemon  │
│              │◀──────│   (this library)   │◀──────│  (rdkfwupdater)  │
└──────────────┘       └───────────────────┘       └──────────────────┘
    API calls              D-Bus IPC              XConf, curl, flash
```

---

## 2. The Big Picture — How It All Fits Together

A typical firmware update follows five steps, always in this order:

```
Step 1: Register     →  "Hey daemon, I'm here. Give me an ID."
Step 2: Check        →  "Is there new firmware for me?"
Step 3: Download     →  "Download that firmware file."
Step 4: Flash        →  "Install the firmware on the device."
Step 5: Unregister   →  "I'm done. Bye."
```

Steps 2, 3, and 4 are **asynchronous** — they return immediately and deliver results later through **callbacks** (functions you provide that the library calls when something happens).

```
                    TIME ─────────────────────────────────────────────▶

Your App:    register ─── check ─── download ─── flash ─── unregister
                           │           │           │
                           │           │           │
                           ▼           ▼           ▼
Callbacks:             "Update      "50%..."    "Flashing..."
                        found!"     "100%!"     "Done!"
```

---

## 3. Analogy: The Post Office

If this all sounds abstract, here's a simple analogy:

| Firmware Update | Post Office Analogy |
|----------------|---------------------|
| `registerProcess()` | Walk into the post office and get a ticket number |
| Your **handle** (ID) | The ticket number on your receipt |
| `checkForUpdate()` | Ask the clerk: "Do I have any packages?" |
| Your **callback** | The clerk calls your name when they find your package |
| `downloadFirmware()` | "Please bring my package from the warehouse" |
| Download **progress callbacks** | The clerk shouts: "25% loaded... 50%... 100%!" |
| `updateFirmware()` | "Please install/unwrap the package for me" |
| Flash **progress callbacks** | The clerk shouts: "Unpacking... Installing... Done!" |
| `unregisterProcess()` | Leave the post office and throw away your ticket |

**Key insight:** After you ask a question at the counter, you **don't stand there and wait**. You go sit down, and the clerk calls you when they have an answer. That's exactly how the async APIs work.

---

## 4. API Reference

### 4.1 `registerProcess()`

> **"Get your ticket."** — This is the first thing you call.

```c
FirmwareInterfaceHandle registerProcess(const char *processName, 
                                         const char *libVersion);
```

**What it does:**
- Connects to the firmware daemon over D-Bus
- Tells the daemon: "Hi, I'm [processName], using library version [libVersion]"
- The daemon gives you back a unique ID number (like "42857")

**Parameters:**
| Name | Type | Description |
|------|------|-------------|
| `processName` | `const char*` | Your app's name (e.g. `"VideoPlayer"`, `"SettingsApp"`) |
| `libVersion` | `const char*` | Your app's version (e.g. `"1.0"`, `"2.3.1"`) |

**Returns:**
- **Success:** A string ID like `"42857"` — save this, you need it for everything else
- **Failure:** `NULL` — daemon not running, D-Bus error, or invalid inputs

**Behavior:**
- ✅ Synchronous — blocks until the daemon responds (typically < 10ms)
- ✅ No background threads created
- ⚠️ The returned string belongs to the library — **never call `free()` on it**
- ⚠️ The string becomes invalid after `unregisterProcess()`

**Example:**
```c
FirmwareInterfaceHandle handle = registerProcess("MyApp", "1.0");
if (handle == NULL) {
    printf("Registration failed! Is the daemon running?\n");
    return;
}
printf("Registered! My handle: %s\n", handle);
// handle might print: "42857"
```

---

### 4.2 `checkForUpdate()`

> **"Is there new firmware?"** — Ask the daemon, get a callback later.

```c
CheckForUpdateResult checkForUpdate(FirmwareInterfaceHandle handle,
                                     UpdateEventCallback callback);
```

**What it does:**
- Asks the daemon to check the XConf server for available firmware
- **Returns immediately** (doesn't wait for the answer)
- When the daemon finds out, it calls **your callback function** with the result

**Parameters:**
| Name | Type | Description |
|------|------|-------------|
| `handle` | `FirmwareInterfaceHandle` | Your ID from `registerProcess()` |
| `callback` | `UpdateEventCallback` | Your function that receives the firmware info |

**Your callback signature:**
```c
void my_callback(const FwInfoData *fwinfo);
```

The `FwInfoData` struct tells you:
- `status` — Was firmware found? (`FIRMWARE_AVAILABLE`, `FIRMWARE_NOT_AVAILABLE`, `FIRMWARE_CHECK_ERROR`, etc.)
- `CurrFWVersion` — Current firmware version on this device
- `UpdateDetails` — If available: filename, download URL, version, reboot flag, etc.

**Returns:**
| Value | Meaning |
|-------|---------|
| `CHECK_FOR_UPDATE_SUCCESS` | Request started OK — your callback will fire later |
| `CHECK_FOR_UPDATE_FAIL` | Couldn't start — bad handle, NULL callback, already in progress, etc. |

**Behavior:**
- ✅ Returns immediately (non-blocking)
- ✅ Callback fires **exactly once** (or zero times if timeout/error)
- ✅ Callback fires in a **background thread** (not your thread)
- ⚠️ Only **one** `checkForUpdate()` at a time per process — second call is rejected
- ⚠️ Data in `FwInfoData` is only valid **during** the callback — copy what you need
- 🕐 Typical callback delay: 5–30 seconds (max 120 seconds safety timeout)

**Timeline:**
```
Your App Thread                      Worker Thread (created by library)
───────────────                      ─────────────────────────────────
checkForUpdate(handle, my_cb)
  │  Spawns worker thread ──────────▶  Connect to D-Bus
  │  Waits ~100ms for worker ready     Subscribe to signals
  │  Returns SUCCESS                    Send request to daemon
  │                                     ...waiting for daemon (5-30s)...
  │  (you can do other things)          
  │                                    Daemon responds!
  │                                    Parse result
  │                                    ──▶ my_cb(&fwinfo)  ◀── YOUR CALLBACK
  │                                    Cleanup resources
  │                                    Thread exits
  ▼
```

**Example:**
```c
void on_check_result(const FwInfoData *info) {
    if (info->status == FIRMWARE_AVAILABLE) {
        printf("New firmware available: %s\n", 
               info->UpdateDetails->FwVersion);
    } else {
        printf("No update available.\n");
    }
}

// Start the check
CheckForUpdateResult result = checkForUpdate(handle, on_check_result);
if (result == CHECK_FOR_UPDATE_SUCCESS) {
    printf("Check started! Waiting for callback...\n");
}
```

---

### 4.3 `downloadFirmware()`

> **"Download that firmware."** — Start a download, get progress callbacks.

```c
DownloadResult downloadFirmware(FirmwareInterfaceHandle handle,
                                 const FwDwnlReq *fwdwnlreq,
                                 DownloadCallback callback);
```

**What it does:**
- Tells the daemon to download a firmware file from the server
- **Returns immediately** (doesn't wait for the download to finish)
- Your callback fires **multiple times** as the download progresses (0%, 25%, 50%, 75%, 100%)

**Parameters:**
| Name | Type | Description |
|------|------|-------------|
| `handle` | `FirmwareInterfaceHandle` | Your ID from `registerProcess()` |
| `fwdwnlreq` | `const FwDwnlReq*` | What to download (name, URL, type) |
| `callback` | `DownloadCallback` | Your function that tracks progress |

**The `FwDwnlReq` struct:**
```c
typedef struct {
    const char *firmwareName;      // "firmware_v2.bin" — REQUIRED
    const char *downloadUrl;       // URL to download from (NULL = let daemon decide)
    const char *TypeOfFirmware;    // "PCI", "PDRI", or "PERIPHERAL" (NULL = default)
} FwDwnlReq;
```

**Your callback signature:**
```c
void my_download_cb(int progress_percent, DownloadStatus status);
```

| `status` value | Meaning |
|----------------|---------|
| `DWNL_IN_PROGRESS` | Still downloading — `progress_percent` is 0–99 |
| `DWNL_COMPLETED` | Done! `progress_percent` is 100 |
| `DWNL_ERROR` | Download failed |

**Returns:**
| Value | Meaning |
|-------|---------|
| `RDKFW_DWNL_SUCCESS` | Daemon accepted the download — callbacks will fire |
| `RDKFW_DWNL_FAILED` | Couldn't start — bad handle, daemon rejected, already downloading, etc. |

**Behavior:**
- ✅ Returns immediately
- ✅ Return value is **accurate** — it reflects whether the daemon actually accepted the request
- ✅ Callback fires **multiple times** (once per progress update)
- ⚠️ Only **one** download at a time — second call is rejected
- 🕐 Typical download time: 1–30 minutes (max 1 hour safety timeout)

**Timeline:**
```
Your App Thread                   Worker Thread
───────────────                   ─────────────
downloadFirmware(handle, req, cb)
  │  Spawns worker thread ──────▶  Connect to D-Bus
  │  Waits ~200ms for daemon       Subscribe to signals
  │   reply (accept/reject)        Call daemon (synchronous)
  │  Returns SUCCESS                Daemon: "Accepted, downloading..."
  │                                 ...receiving progress signals...
  │  (you can do other things)      
  │                                ──▶ cb(10, DWNL_IN_PROGRESS)
  │                                ──▶ cb(25, DWNL_IN_PROGRESS)
  │                                ──▶ cb(50, DWNL_IN_PROGRESS)
  │                                ──▶ cb(75, DWNL_IN_PROGRESS)
  │                                ──▶ cb(100, DWNL_COMPLETED)
  │                                 Cleanup, thread exits
  ▼
```

**Example:**
```c
void on_download_progress(int percent, DownloadStatus status) {
    if (status == DWNL_IN_PROGRESS) {
        printf("Downloading: %d%%\n", percent);
    } else if (status == DWNL_COMPLETED) {
        printf("Download complete!\n");
    } else {
        printf("Download failed!\n");
    }
}

FwDwnlReq request = {
    .firmwareName = "firmware_v2.bin",
    .downloadUrl = NULL,           // let daemon use XConf URL
    .TypeOfFirmware = "PCI"
};

DownloadResult result = downloadFirmware(handle, &request, on_download_progress);
if (result == RDKFW_DWNL_SUCCESS) {
    printf("Download started!\n");
}
```

---

### 4.4 `updateFirmware()`

> **"Flash it."** — Install firmware on the device.

```c
UpdateResult updateFirmware(FirmwareInterfaceHandle handle,
                              const FwUpdateReq *fwupdatereq,
                              UpdateCallback callback);
```

**What it does:**
- Tells the daemon to flash (install) downloaded firmware onto the device
- **Returns immediately** (doesn't wait for flash to complete)
- Your callback fires **multiple times** as flashing progresses
- ⚠️ **This modifies your device's firmware — irreversible!**

**Parameters:**
| Name | Type | Description |
|------|------|-------------|
| `handle` | `FirmwareInterfaceHandle` | Your ID from `registerProcess()` |
| `fwupdatereq` | `const FwUpdateReq*` | What to flash (name, type, location, reboot) |
| `callback` | `UpdateCallback` | Your function that tracks progress |

**The `FwUpdateReq` struct:**
```c
typedef struct {
    const char *firmwareName;         // "firmware_v2.bin" — REQUIRED
    const char *TypeOfFirmware;       // "PCI", "PDRI", or "PERIPHERAL" — REQUIRED
    const char *LocationOfFirmware;   // Path to file (NULL = default from device.properties)
    bool rebootImmediately;           // true = reboot when done; false = you reboot manually
} FwUpdateReq;
```

**Your callback signature:**
```c
void my_update_cb(int progress_percent, UpdateStatus status);
```

| `status` value | Meaning |
|----------------|---------|
| `UPDATE_IN_PROGRESS` | Still flashing — `progress_percent` is 0–99 |
| `UPDATE_COMPLETED` | Done! Firmware installed successfully |
| `UPDATE_ERROR` | Flash failed |

**Returns:**
| Value | Meaning |
|-------|---------|
| `RDKFW_UPDATE_SUCCESS` | Daemon accepted — flash starting |
| `RDKFW_UPDATE_FAILED` | Couldn't start — bad handle, daemon rejected, already flashing, etc. |

**Behavior:**
- ✅ Returns immediately
- ✅ Return value is **accurate** — reflects daemon's accept/reject decision
- ✅ Callback fires **multiple times** (once per progress update)
- ⚠️ Only **one** flash at a time — second call is rejected
- ⚠️ If `rebootImmediately` is true, the device **will reboot** after flash completes
- 🕐 Typical flash time: 5–60 minutes (max 1 hour safety timeout)

**Example:**
```c
void on_flash_progress(int percent, UpdateStatus status) {
    if (status == UPDATE_IN_PROGRESS) {
        printf("Flashing: %d%%\n", percent);
    } else if (status == UPDATE_COMPLETED) {
        printf("Flash complete! Firmware installed.\n");
    } else {
        printf("Flash FAILED!\n");
    }
}

FwUpdateReq request = {
    .firmwareName = "firmware_v2.bin",
    .TypeOfFirmware = "PCI",
    .LocationOfFirmware = NULL,       // use default path
    .rebootImmediately = false        // don't reboot automatically
};

UpdateResult result = updateFirmware(handle, &request, on_flash_progress);
```

---

### 4.5 `unregisterProcess()`

> **"I'm leaving."** — Disconnect from the daemon.

```c
void unregisterProcess(FirmwareInterfaceHandle handler);
```

**What it does:**
- Tells the daemon you're disconnecting
- Frees the handle memory
- After this, your handle is **invalid** — don't use it again

**Parameters:**
| Name | Type | Description |
|------|------|-------------|
| `handler` | `FirmwareInterfaceHandle` | Your ID from `registerProcess()` |

**Behavior:**
- ✅ Safe to call with `NULL` (does nothing)
- ✅ Synchronous — blocks briefly for D-Bus call
- ✅ Best-effort — if D-Bus is dead, it still frees local memory
- ⛔ **Will REFUSE** to unregister if a `checkForUpdate()`, `downloadFirmware()`, or `updateFirmware()` is still in progress — you must wait for the callback first
- ⚠️ After this call, your handle is **gone** — don't use it for anything

**Example:**
```c
// Always call before your app exits
unregisterProcess(handle);
handle = NULL;  // good practice: avoid using stale handle
```

---

## 5. The On-Demand Worker Thread Model

This is the heart of the library's design. Understanding this section explains *why* the APIs work the way they do.

### 5.1 What Problem Does It Solve?

The library needs to do two things at once:
1. **Return quickly** to your app (so your app isn't frozen)
2. **Wait for the daemon** (which can take seconds, minutes, or even an hour)

You can't do both in one thread. If your app's thread waits for the daemon, your app is stuck. If you return immediately, who listens for the daemon's answer?

**Answer: A worker thread.** The library creates a temporary background thread to wait for the daemon, while your app continues running.

### 5.2 How It Works — The Restaurant Analogy

Imagine you're at a restaurant:

```
1. You (the app) sit down at a table.

2. You call the waiter (the library API):
   "I'd like to order the firmware check, please."

3. The waiter writes down your order and hands it to a RUNNER
   (the worker thread). The runner goes to the kitchen (the daemon).

4. The waiter comes back to you immediately:
   "Your order has been placed!" (API returns SUCCESS)
   
5. You're free to chat, check your phone, whatever.
   (Your app continues running)

6. Meanwhile, the runner is standing in the kitchen,
   waiting for the chef to finish cooking.
   (Worker thread waits for D-Bus signal from daemon)

7. When the food is ready, the runner brings it to your table
   and calls out: "Your firmware check result is here!"
   (Worker thread fires your callback)

8. The runner's job is done. They clock out and go home.
   (Worker thread cleans up and exits)
```

**Key insight:** The runner (worker thread) only exists for the duration of your order. There's no runner standing around when nobody has ordered anything. This is the "on-demand" part.

### 5.3 Technical Deep Dive

Here's what actually happens under the hood when you call, say, `checkForUpdate()`:

```
YOUR APP THREAD                             WORKER THREAD (created per call)
═══════════════                             ════════════════════════════════

checkForUpdate(handle, my_callback)
  │
  ├─ 1. Validate inputs
  │     (is handle valid? is callback non-NULL?)
  │
  ├─ 2. Allocate context (ctx) on the heap
  │     ctx = {
  │       handle_key: "42857"        (copied from your handle)
  │       callback:   my_callback    (pointer to your function)
  │       ready_mutex / ready_cond   (for synchronization)
  │       is_ready:   false
  │       init_failed: false
  │     }
  │
  ├─ 3. Reject if already in progress
  │     (only one check at a time allowed)
  │
  ├─ 4. pthread_create() ──────────────────▶  WORKER STARTS
  │                                            │
  │                                            ├─ Create isolated GLib event loop
  │                                            │   (GMainContext + GMainLoop)
  │                                            │
  │                                            ├─ Connect to D-Bus system bus
  │                                            │   (g_bus_get_sync)
  │                                            │
  │                                            ├─ Subscribe to daemon signal:
  │                                            │   "CheckForUpdateComplete"
  │                                            │
  │                                            ├─ Send D-Bus method call:
  │                                            │   "CheckForUpdate" with handle
  │                                            │
  │  ◀─── 5. Worker signals "READY" ──────────┤
  │        (condvar: is_ready = true)          │
  │                                            ├─ Start event loop: g_main_loop_run()
  ├─ 6. Check: did worker init OK?             │   ...waiting for signal from daemon...
  │     YES → return SUCCESS                   │   ...could take 5-120 seconds...
  │     NO  → join thread, return FAIL         │
  │                                            │
  │  YOUR APP IS FREE HERE                     │
  │  (do whatever you want)                    │
  │                                            │  SIGNAL ARRIVES from daemon!
  │                                            ├─ Parse signal data
  │                                            │   (version, status, details)
  │                                            │
  │                                            ├─ Build FwInfoData struct
  │                                            │
  │                                            ├─ ═══▶ my_callback(&fwinfo) ◀═══
  │                                            │       YOUR CODE RUNS HERE
  │                                            │       (in the worker thread)
  │                                            │
  │                                            ├─ Cleanup:
  │                                            │   - Unsubscribe from signals
  │                                            │   - Close D-Bus connection
  │                                            │   - Destroy event loop
  │                                            │   - Free ctx and all strings
  │                                            │
  │                                            └─ WORKER EXITS
  ▼
```

### 5.4 The Condvar Handshake

The trickiest part of the design is **step 5** — the "ready" handshake. This is how your app knows the worker started successfully.

**The problem:** Your app needs to know *right away* if the check will work. But the worker needs a moment to connect to D-Bus and set things up. How do you coordinate?

**The solution: A condition variable (condvar).**

Think of it like a walkie-talkie:

```
Your App:    "Worker, are you set up? Over."     (pthread_cond_wait)
             ... waiting ...
Worker:      "Roger that, I'm connected to       (pthread_cond_signal)
              D-Bus and listening. Over."
Your App:    "Great, returning SUCCESS to         (function returns)
              the caller."
```

If the worker can't connect (D-Bus is dead, system error):

```
Your App:    "Worker, are you set up? Over."     (pthread_cond_wait)
             ... waiting ...
Worker:      "Negative. D-Bus connection          (init_failed = true,
              failed. Over and out."               pthread_cond_signal)
Your App:    "Copy. Returning FAIL to the         (function returns FAIL)
              caller."
```

There's also a **safety timeout** (10 seconds). If the worker doesn't respond at all (crashed, stuck), the app gives up and returns FAIL rather than hanging forever.

### 5.5 Why On-Demand Instead of Persistent?

The library used to have a **persistent background thread** — a thread that was created when the library loaded and ran continuously until the library unloaded. Here's why on-demand is better:

| Aspect | Persistent Thread (Old) | On-Demand Thread (Current) |
|--------|------------------------|---------------------------|
| **Resource usage when idle** | Thread + D-Bus connection + event loop running 24/7 | **Zero** — nothing running |
| **Thread lifetime** | Minutes to hours (entire app lifetime) | Seconds to minutes (one operation) |
| **Complexity** | Registry of callbacks, signal routing, subscription management | Simple: one thread, one job, self-contained |
| **Multi-client isolation** | Shared thread serving multiple registrations | Each call gets its own isolated thread |
| **Cleanup** | Complex shutdown: stop loop, drain queues, join thread | Simple: thread cleans up after itself |
| **Failure blast radius** | If BG thread dies, ALL pending operations fail | If one thread dies, only THAT operation fails |

**The key principle:** *"If nobody is checking for updates right now, there should be zero threads, zero D-Bus connections, and zero event loops running."*

---

## 6. Complete Workflow Example

Here's how a real application uses the library from start to finish:

```c
#include "rdkFwupdateMgr_client.h"
#include <stdio.h>
#include <unistd.h>

/* Step 1: Define your callbacks */

void on_check(const FwInfoData *info) {
    printf("Check result: %d\n", info->status);
    if (info->status == FIRMWARE_AVAILABLE) {
        printf("  New version: %s\n", info->UpdateDetails->FwVersion);
        printf("  Filename:    %s\n", info->UpdateDetails->FwFileName);
        // Signal main thread to proceed with download...
    }
}

void on_download(int percent, DownloadStatus status) {
    printf("Download: %d%% [%s]\n", percent,
           status == DWNL_COMPLETED ? "DONE" :
           status == DWNL_ERROR ? "FAILED" : "...");
    // Signal main thread when terminal status received...
}

void on_flash(int percent, UpdateStatus status) {
    printf("Flash: %d%% [%s]\n", percent,
           status == UPDATE_COMPLETED ? "DONE" :
           status == UPDATE_ERROR ? "FAILED" : "...");
    // Signal main thread when terminal status received...
}

int main() {
    /* Step 2: Register */
    FirmwareInterfaceHandle handle = registerProcess("MyApp", LIB_VERSION);
    if (!handle) { return 1; }

    /* Step 3: Check for update */
    checkForUpdate(handle, on_check);
    // on_check fires later in a background thread...
    // (your app waits for callback via pthread_cond_wait or similar)

    /* Step 4: Download (after check callback says FIRMWARE_AVAILABLE) */
    FwDwnlReq dl = { .firmwareName = "firmware_v2.bin" };
    downloadFirmware(handle, &dl, on_download);
    // on_download fires multiple times as download progresses...

    /* Step 5: Flash (after download callback says DWNL_COMPLETED) */
    FwUpdateReq upd = {
        .firmwareName = "firmware_v2.bin",
        .TypeOfFirmware = "PCI",
        .rebootImmediately = false
    };
    updateFirmware(handle, &upd, on_flash);
    // on_flash fires multiple times as flash progresses...

    /* Step 6: Unregister (after flash callback says UPDATE_COMPLETED) */
    unregisterProcess(handle);
    handle = NULL;

    return 0;
}
```

---

## 7. Thread Safety — How the Library Keeps Things Safe

Multiple threads accessing shared data can cause chaos. Here's how the library prevents it:

### 7.1 One-at-a-Time Enforcement

Each operation type (`check`, `download`, `update`) has a **global boolean flag** guarded by a **mutex**:

```
g_check_in_progress  ──── protected by g_check_in_progress_mutex
g_dwnl_in_progress   ──── protected by g_dwnl_in_progress_mutex
g_update_in_progress ──── protected by g_update_in_progress_mutex
```

When you call `checkForUpdate()`, the library does this atomically (under lock):
1. Check: is `g_check_in_progress` true? → If yes, REJECT (return FAIL)
2. Set `g_check_in_progress = true`
3. Store reference to the active context

When the worker finishes, it atomically sets `g_check_in_progress = false`.

This means: **you can never have two checks running at the same time**, even if two threads call `checkForUpdate()` simultaneously.

### 7.2 State Transition Functions

Instead of exposing raw mutexes, the library uses clean accessor functions:

| Function | What it does |
|----------|-------------|
| `internal_begin_check(ctx)` | Atomically: if idle → set in-progress + track ctx; if busy → reject |
| `internal_end_check()` | Atomically: set idle + clear ctx (worker calls this when done) |
| `internal_abort_check()` | Same as end, but for error paths (API call itself failed) |
| `internal_is_check_in_progress()` | Atomically: return whether a check is active |

The same pattern exists for `download` and `update`. Nobody directly touches the booleans or mutexes — they go through these functions.

### 7.3 Per-Request Isolation

Each API call creates its own:
- **GMainContext** (GLib event loop context — isolated from other threads)
- **GMainLoop** (event loop — only this thread's signals are dispatched here)
- **GDBusConnection** (D-Bus connection — not shared with anyone)
- **Condvar** (`ready_mutex` / `ready_cond` — only this call's handshake)

This means worker threads are completely isolated from each other. A check thread and a download thread running at the same time won't interfere.

---

## 8. Memory Ownership — Who Frees What?

Memory bugs (leaks, double-frees, use-after-free) are a major source of crashes. Here are the clear ownership rules:

### 8.1 The Handle

```
registerProcess() ──▶ malloc(handle_str) ──▶ You use it ──▶ unregisterProcess() ──▶ free(handle_str)
```

- **Created by:** `registerProcess()` (via `malloc`)
- **Owned by:** The library (you have read-only access)
- **Freed by:** `unregisterProcess()` (via `free`)
- **Your responsibility:** Never call `free()` on the handle yourself

### 8.2 The Request Context (`ctx`)

```
API call ──▶ calloc(ctx) ──▶ pthread_create ──▶ [condvar handshake] ──▶ ...

PATH A (success):
  API returns SUCCESS ──▶ ctx now owned by WORKER THREAD ──▶ worker frees ctx when done

PATH B (init failure):
  Worker signals failure ──▶ API joins worker ──▶ API frees ctx
```

- **Created by:** The API function (`checkForUpdate`, `downloadFirmware`, `updateFirmware`)
- **Ownership transfer:** After the condvar handshake, the worker thread owns `ctx`
- **Freed by:** Whoever owns it at that point

### 8.3 Callback Data

- `FwInfoData*` in `UpdateEventCallback` → **valid only during the callback**. Copy what you need!
- Progress values (`int percent`, `DownloadStatus`) → simple values, no memory to manage

---

## 9. Error Handling — What Can Go Wrong?

| Scenario | What happens |
|----------|-------------|
| Daemon not running | `registerProcess()` returns `NULL`; async APIs return FAIL |
| D-Bus system bus down | Same as above — all D-Bus operations fail |
| Invalid/NULL handle | API returns FAIL immediately (input validation) |
| NULL callback | API returns FAIL immediately |
| Already in progress | API returns FAIL (one-at-a-time enforcement) |
| Worker thread can't connect | Condvar handshake reports failure → API returns FAIL |
| Daemon rejects request | `downloadFirmware()`/`updateFirmware()` return FAIL (accurate) |
| Daemon signal never arrives | Safety timeout fires (120s for check, 3600s for download/flash) |
| `malloc`/`calloc` fails | Cascading cleanup — everything allocated so far is freed, returns FAIL |
| `pthread_create` fails | In-progress flag reset via `abort_*()`, ctx freed, returns FAIL |
| Library unloaded while worker running | Destructor joins all active worker threads before unloading |
| App crashes in callback | Worker thread dies; in-progress flag stays `true` permanently (known limitation) |

---

## 10. Callbacks — The Rules

Since callbacks are the main way you receive results, here are the important rules:

### ✅ DO:
- Copy any data you need from the callback arguments (data is temporary)
- Keep callbacks **short** — don't do heavy work inside them
- Use mutexes/condvars to signal your main thread from the callback
- Handle both success and error cases

### ❌ DON'T:
- **Don't call other library APIs** from inside a callback (can deadlock)
- Don't call `unregisterProcess()` from inside a callback
- Don't assume which thread the callback runs on (it's a worker thread)
- Don't store the `FwInfoData*` pointer — it's invalid after the callback returns
- Don't let your callback crash (the worker thread will die, and the in-progress flag stays stuck)

### Callback Summary Table:

| API | Callback Type | # of Calls | When |
|-----|--------------|------------|------|
| `checkForUpdate` | `UpdateEventCallback` | **1** (or 0 on timeout) | When daemon finishes checking XConf |
| `downloadFirmware` | `DownloadCallback` | **Many** | Each progress update (0%→100%) |
| `updateFirmware` | `UpdateCallback` | **Many** | Each progress update (0%→100%) |

---

## 11. Quick Reference Card

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    librdkFwupdateMgr Quick Reference                    │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  LIFECYCLE:                                                             │
│    handle = registerProcess("AppName", "1.0")    → get your ticket     │
│    unregisterProcess(handle)                      → give it back        │
│                                                                         │
│  CHECK:                                                                 │
│    checkForUpdate(handle, callback)               → is there an update? │
│    callback fires ONCE with FwInfoData*                                 │
│                                                                         │
│  DOWNLOAD:                                                              │
│    downloadFirmware(handle, &request, callback)   → download firmware   │
│    callback fires MANY TIMES with (percent, status)                     │
│                                                                         │
│  FLASH:                                                                 │
│    updateFirmware(handle, &request, callback)     → flash firmware      │
│    callback fires MANY TIMES with (percent, status)                     │
│                                                                         │
│  RULES:                                                                 │
│    ✓ One operation per type at a time                                   │
│    ✓ All async APIs return immediately                                  │
│    ✓ Results come through callbacks (in background thread)              │
│    ✓ Copy callback data — it's temporary                                │
│    ✗ Don't call APIs from inside callbacks                              │
│    ✗ Don't free the handle yourself                                     │
│    ✗ Don't use handle after unregisterProcess()                         │
│                                                                         │
│  THREAD MODEL:                                                          │
│    On-demand workers: thread created per API call, exits when done      │
│    Zero cost when idle: no threads, no connections, no event loops      │
│    Condvar handshake: API waits ~100ms for worker setup confirmation    │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```
