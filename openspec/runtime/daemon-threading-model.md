# Daemon Threading Model

> **Evidence Level:** Verified from `src/dbus/rdkv_dbus_server.c`, `src/rdkFwupdateMgr.c`, `librdkFwupdateMgr/src/rdkFwupdateMgr_async.c`  
> **Scope:** Every thread context in both daemon and client library, with synchronization primitives

---

## 1. Daemon Thread Architecture

```mermaid
flowchart TD
    subgraph "rdkFwupdateMgr Process"
        direction TB
        
        subgraph MAIN_THREAD["Main Thread (GLib Main Loop)"]
            direction LR
            ML_DBUS["D-Bus Dispatch<br/>process_app_request()"]
            ML_IDLE["g_idle_add() callbacks<br/>emit signals"]
            ML_DONE["GTask completion callbacks<br/>xconf_fetch_done()<br/>download_done()<br/>flash_done()"]
        end
        
        subgraph WORKER_POOL["GLib Thread Pool (GTask Workers)"]
            direction LR
            W1["Worker: rdkfw_xconf_fetch_worker<br/>Blocking XConf HTTP POST"]
            W2["Worker: rdkfw_download_worker<br/>Blocking firmware download"]
            W3["Worker: rdkfw_flash_worker<br/>Blocking flash operation"]
        end
        
        subgraph MONITOR_THREADS["Dedicated Threads (GThread)"]
            M1["rdkfw_progress_monitor_thread<br/>Polls /opt/curl_progress<br/>every 100ms"]
        end
    end
    
    MAIN_THREAD -->|"g_task_run_in_thread()"| WORKER_POOL
    WORKER_POOL -->|"g_task_return_*()"| MAIN_THREAD
    WORKER_POOL -->|"g_thread_try_new()"| MONITOR_THREADS
    MONITOR_THREADS -->|"g_idle_add()"| MAIN_THREAD
    WORKER_POOL -->|"g_idle_add()"| MAIN_THREAD
```

---

## 2. Thread Inventory

### 2.1 Daemon Threads

| Thread | Creation | Lifetime | Purpose | Blocking Calls |
|--------|----------|----------|---------|----------------|
| **Main Thread** | Process start | Entire daemon lifecycle | GLib main loop, D-Bus dispatch, signal emission, state management | `g_main_loop_run()` (event wait) |
| **XConf Worker** | On first CheckForUpdate | Until XConf response received | Blocking HTTP POST to XConf server | `rdkFwupdateMgr_checkForUpdate()` |
| **Download Worker** | On DownloadFirmware | Until download completes | Blocking HTTP GET for firmware | `rdkv_upgrade_request()` |
| **Flash Worker** | On UpdateFirmware | Until flash completes | Blocking flash I/O | `flashImage()` |
| **Progress Monitor** | Spawned by Download Worker | Until download worker signals stop | Poll progress file, emit signals | `g_usleep(100000)` (100ms sleep) |

### 2.2 Client Library Threads

| Thread | Creation | Lifetime | Purpose | Blocking Calls |
|--------|----------|----------|---------|----------------|
| **Caller Thread** | Application owns | Application-managed | Synchronous D-Bus proxy calls | `g_dbus_proxy_call_sync()` (10-50ms) |
| **Background Thread** | `rdkFwupdateMgr_init()` | Until `rdkFwupdateMgr_term()` | GLib main loop for signal subscription and callback dispatch | `g_main_loop_run()` (event wait) |

---

## 3. Detailed Thread Interaction Diagram

```mermaid
sequenceDiagram
    participant T1 as Main Thread<br/>(GLib Main Loop)
    participant T2 as XConf Worker<br/>(GTask pool)
    participant T3 as Download Worker<br/>(GTask pool)
    participant T4 as Progress Monitor<br/>(dedicated GThread)

    Note over T1: g_main_loop_run() — event dispatch
    
    Note over T1,T2: === CheckForUpdate ===
    T1->>T1: process_app_request("CheckForUpdate")
    T1->>T1: Validation + immediate response
    T1->>T1: setXConfCommStatus(TRUE) [G_LOCK]
    T1->>T2: g_task_run_in_thread(xconf_fetch_worker)
    activate T2
    Note right of T1: Main loop FREE<br/>for other requests
    T2->>T2: rdkFwupdateMgr_checkForUpdate() [BLOCKS 5-60s]
    T2->>T2: g_task_return_pointer(result)
    deactivate T2
    T1->>T1: rdkfw_xconf_fetch_done() [scheduled by GTask]
    T1->>T1: emit_signal("CheckForUpdateComplete")
    T1->>T1: setXConfCommStatus(FALSE) [G_LOCK]
    
    Note over T1,T4: === DownloadFirmware ===
    T1->>T1: process_app_request("DownloadFirmware")
    T1->>T1: Validation + immediate response
    T1->>T1: IsDownloadInProgress = TRUE
    T1->>T3: g_task_run_in_thread(download_worker)
    activate T3
    T3->>T4: g_thread_try_new("progress_monitor")
    activate T4
    T3->>T3: rdkv_upgrade_request() [BLOCKS minutes]
    
    loop Every 100ms
        T4->>T4: Read /opt/curl_progress
        T4->>T1: g_idle_add(emit_download_progress)
        T1->>T1: rdkfw_emit_download_progress() [signal emit]
    end
    
    T3->>T3: Download complete
    T3->>T4: atomic_store(stop_flag, 1)
    T4->>T4: Detect stop_flag, exit loop
    deactivate T4
    T3->>T3: g_thread_join(monitor_thread)
    T3->>T1: g_idle_add(emit_progress: COMPLETED)
    T3->>T3: g_task_return_boolean(TRUE)
    deactivate T3
    T1->>T1: rdkfw_download_done() [scheduled by GTask]
    T1->>T1: IsDownloadInProgress = FALSE
```

---

## 4. Synchronization Primitives

### 4.1 Daemon Process

```mermaid
flowchart TD
    subgraph "Mutex-Protected State"
        M1["pthread_mutex_t mutuex_dwnl_state<br/>Guards: DwnlState (int)"]
        M2["pthread_mutex_t app_mode_status<br/>Guards: app_mode (int)"]
        M3["G_LOCK(xconf_status_mutex)<br/>Guards: XConfCommStatus (gboolean)"]
        M4["GMutex* monitor_mutex<br/>Guards: stop_flag (per-download)"]
    end
    
    subgraph "Main-Loop-Serialized State (no mutex needed)"
        S1["registered_processes (GHashTable)"]
        S2["active_tasks (GHashTable)"]
        S3["waiting_checkUpdate_ids (GSList)"]
        S4["waiting_download_ids (GSList)"]
        S5["IsDownloadInProgress (gboolean)"]
        S6["IsFlashInProgress (gboolean)"]
        S7["current_download (CurrentDownloadState*)"]
        S8["current_flash (CurrentFlashState*)"]
    end
    
    subgraph "Thread-Safe Communication"
        C1["g_task_return_pointer() / g_task_return_boolean()<br/>Worker → Main (via GTask framework)"]
        C2["g_idle_add(callback, data)<br/>Worker/Monitor → Main (via GLib idle source)"]
        C3["atomic gint stop_flag<br/>Worker → Monitor (via atomic/mutex)"]
    end
```

### 4.2 Synchronization Rules

| Rule | Mechanism | Rationale |
|------|-----------|-----------|
| Only one XConf fetch at a time | `XConfCommStatus` via `G_LOCK` | Prevents duplicate network calls |
| Worker → Main loop data transfer | `g_task_return_*()` | GTask framework ensures main-loop dispatch |
| Worker → Signal emission | `g_idle_add()` | Schedules function on main loop (thread-safe) |
| Stop progress monitor | Atomic int + `GMutex` | Worker sets flag, monitor polls it |
| Download/Flash state flags | Main loop serialization | Only modified in main-loop context callbacks |
| `DwnlState` | `pthread_mutex_t` | Read from IARM callback context (possibly different thread) |
| `app_mode` | `pthread_mutex_t` | Written from IARM callback (`interuptDwnl`) |

---

## 5. Client Library Threading Model

```mermaid
flowchart TD
    subgraph "Client Library (librdkFwupdateMgr.so)"
        direction TB
        
        subgraph CALLER["Caller's Thread"]
            API["Public API functions<br/>registerProcess()<br/>checkForUpdate()<br/>downloadFirmware()<br/>updateFirmware()<br/>unregisterProcess()"]
            PROXY["Ephemeral GDBusProxy<br/>(created per call, destroyed after)"]
        end
        
        subgraph BG_THREAD["Background Thread (fwupdate_bg_thread)"]
            BG_LOOP["GLib Main Loop"]
            SUBS["Signal Subscriptions<br/>CheckForUpdateComplete<br/>DownloadProgress<br/>UpdateProgress"]
            DISPATCH["Callback Dispatch<br/>Looks up registered callback<br/>Invokes in BG thread context"]
        end
        
        subgraph REGISTRIES["Callback Registries (thread-safe)"]
            R1["g_registry<br/>(CheckForUpdate callbacks)"]
            R2["g_dwnl_registry<br/>(Download callbacks)"]
            R3["g_update_registry<br/>(Update callbacks)"]
        end
    end
    
    API -->|"sync D-Bus call"| PROXY
    BG_LOOP -->|"signal received"| SUBS
    SUBS -->|"lookup"| REGISTRIES
    REGISTRIES -->|"invoke"| DISPATCH
    API -->|"register callback"| REGISTRIES
```

### 5.1 Library Thread Safety Guarantees

| Operation | Thread Safety | Evidence |
|-----------|--------------|----------|
| `registerCheckForUpdateCallback()` | Safe from any thread | Modifies `g_registry` (GLib hash table with internal locking) |
| `registerDownloadCallback()` | Safe from any thread | Modifies `g_dwnl_registry` |
| Callback invocation | Always on BG thread | `background_thread_func` processes signals |
| `rdkFwupdateMgr_init()` | Call once from single thread | Creates BG thread, not reentrant |
| `rdkFwupdateMgr_term()` | Call once from single thread | Joins BG thread |
| API calls (`checkForUpdate` etc.) | Safe from any thread | Each creates independent proxy |

---

## 6. Thread Lifecycle Diagram

```mermaid
gantt
    title Thread Lifetimes During Firmware Update Operation
    dateFormat ss
    axisFormat %S

    section Daemon
    Main Thread (GLib Loop)     :active, main, 00, 200s
    XConf Worker               :crit, xconf, 05, 20s
    Download Worker            :crit, dwnl, 30, 120s
    Progress Monitor           :monitor, 31, 119s
    Flash Worker               :crit, flash, 155, 30s

    section Client Library
    App Thread (API calls)     :app, 00, 200s
    BG Thread (signal RX)      :active, bg, 00, 200s
```

---

## 7. Cross-Thread Data Flow Matrix

| From → To | Data | Mechanism | Ownership Transfer |
|-----------|------|-----------|-------------------|
| Main → XConf Worker | `AsyncXconfFetchContext` | `g_task_set_task_data()` | Main allocates, completion CB frees |
| XConf Worker → Main | `GVariant* result` | `g_task_return_pointer()` | Worker creates, completion CB unrefs |
| Main → Download Worker | `AsyncDownloadContext` | `g_task_set_task_data()` | Main allocates, completion CB frees |
| Download Worker → Main | `gboolean success` | `g_task_return_boolean()` | Value type (no ownership) |
| Download Worker → Monitor | `ProgressMonitorContext*` | Thread argument | Worker allocates + frees after join |
| Monitor → Main | `ProgressUpdate*` | `g_idle_add()` | Monitor allocates, idle CB frees |
| Download Worker → Main | `ProgressUpdate*` (final) | `g_idle_add()` | Worker allocates, idle CB frees |
| IARM → Main | `int app_mode` | `interuptDwnl()` callback | Value type via mutex |

---

## 8. Potential Concurrency Hazards

| Hazard | Risk | Mitigation |
|--------|------|------------|
| Signal arrives before callback registered (client) | Missed update notification | Library pattern: register callback BEFORE calling API |
| `interuptDwnl()` modifies `force_exit` during download | Race with download worker reading it | `force_exit` is `int` — atomic on most platforms; curl checks it periodically |
| Multiple CheckForUpdate during XConf fetch | Duplicate worker spawn | `XConfCommStatus` flag with G_LOCK prevents this |
| Download worker + Progress monitor access `current_download` | Data race | Monitor only reads progress file, not shared struct; Worker sets stop flag via mutex |
| Main loop callbacks accessing freed context | Use-after-free | GTask framework guarantees completion CB runs after worker; `g_idle_add` serializes |
| Client unregister while signal in flight | Callback invoked on stale handle | **[INFERENCE]** Possible issue — signal could arrive after unregister but before term |

---

## 9. GLib Async Patterns Used

### 9.1 GTask (Offload blocking work)
```
Main Thread                    Worker Thread
    │                              │
    ├─ g_task_new()                │
    ├─ g_task_set_task_data()      │
    ├─ g_task_run_in_thread()──────┤
    │  (main loop continues)       ├─ worker_func(task, data)
    │                              ├─ <blocking I/O>
    │                              ├─ g_task_return_pointer()
    │  ┌───────────────────────────┘
    ├──┤ completion_callback()
    │  │ (runs on main loop)
    │  └───────────────────────
```

### 9.2 g_idle_add (Thread-safe main loop dispatch)
```
Worker Thread                  Main Thread
    │                              │
    ├─ Allocate ProgressUpdate     │
    ├─ g_idle_add(emit_fn, data)───┤
    │  (worker continues)          ├─ emit_fn(data) [on next loop iteration]
    │                              ├─ g_dbus_connection_emit_signal()
    │                              ├─ g_free(data)
    │                              ├─ return G_SOURCE_REMOVE
```

### 9.3 Dedicated GThread (Long-running monitoring)
```
Download Worker                Monitor Thread
    │                              │
    ├─ g_thread_try_new()──────────┤
    │  (download proceeds)         ├─ while (!stop_flag)
    │                              │    ├─ read progress file
    │                              │    ├─ g_idle_add(emit_progress)
    │                              │    └─ g_usleep(100000)
    ├─ atomic_store(stop_flag, 1)──┤
    │                              ├─ exit loop
    ├─ g_thread_join()─────────────┤
    │  (waits for monitor)         └─ thread exits
```

---

## 10. Summary: What Runs Where

| Context | What Happens Here | Never Do Here |
|---------|-------------------|---------------|
| **Daemon Main Loop** | D-Bus dispatch, validation, state changes, signal emission, completion callbacks | Blocking I/O, network calls, sleep |
| **Daemon Worker Thread** | XConf queries, firmware downloads, flash operations | Direct D-Bus calls, direct state mutation |
| **Daemon Monitor Thread** | File polling, progress tracking | Direct D-Bus calls (uses `g_idle_add`) |
| **Client Caller Thread** | Synchronous D-Bus proxy calls (brief blocking) | Long waits, callback registration after API call |
| **Client BG Thread** | Signal reception, callback dispatch | Blocking calls, API invocations |
