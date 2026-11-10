# rdkFwupdateMgr Daemon — Detailed Runtime Execution Sequence

> **Evidence Level:** Verified from `src/rdkFwupdateMgr.c` and `src/dbus/rdkv_dbus_server.c`  
> **Thread Contexts:** GLib Main Loop Thread + GTask Worker Pool + Progress Monitor Threads  
> **Primary Execution Model:** Event-driven (GLib main loop) with async offloading via GTask

---

## 1. Process Identity

| Property | Value |
|----------|-------|
| Binary | `/usr/bin/rdkFwupdateMgr` |
| Systemd unit | `rdkFwupdateMgr.service` |
| Bus name | `org.rdkfwupdater.Interface` |
| Object path | `/org/rdkfwupdater/Service` |
| Invocation | `rdkFwupdateMgr <retry_count> <trigger_type>` |
| Lifetime | Indefinite (systemd-managed daemon) |

---

## 2. Startup Sequence — State Machine

```mermaid
stateDiagram-v2
    [*] --> STATE_INIT : main() entry
    STATE_INIT --> STATE_INIT_VALIDATION : initialize() success
    STATE_INIT --> EXIT_FATAL : initialize() failure
    STATE_INIT --> EXIT_FATAL : argc < 3
    STATE_INIT --> EXIT_FATAL : Invalid trigger_type
    
    STATE_INIT_VALIDATION --> STATE_IDLE : INITIAL_VALIDATION_SUCCESS
    STATE_INIT_VALIDATION --> STATE_IDLE : DWNL_COMPLETED (pending reboot)
    STATE_INIT_VALIDATION --> STATE_IDLE : DWNL_INPROGRESS (another instance)
    STATE_INIT_VALIDATION --> EXIT_FATAL : INITIAL_VALIDATION_FAIL
    
    STATE_IDLE --> STATE_IDLE : g_main_loop_run() [BLOCKS FOREVER]
    STATE_IDLE --> EXIT_SHUTDOWN : Main loop quit (signal/error)
    
    EXIT_FATAL --> [*] : cleanup_dbus() + uninitialize() + exit()
    EXIT_SHUTDOWN --> [*] : cleanup_dbus() + uninitialize() + exit()
```

**[FACT]** Unlike `rdkvfwupgrader`, the daemon always transitions to `STATE_IDLE` (even with DWNL_INPROGRESS or DWNL_COMPLETED), never exits on validation status alone. Only `INITIAL_VALIDATION_FAIL` causes exit.

---

## 3. Complete Startup Sequence Diagram

```mermaid
sequenceDiagram
    participant SYSTEMD as systemd
    participant DAEMON as rdkFwupdateMgr<br/>(main thread)
    participant DBUS as GDBus<br/>(system bus)
    participant FS as Filesystem
    participant IARM as IARM Bus
    participant THUNDER as Thunder JSON-RPC

    Note over SYSTEMD,THUNDER: ═══ STATE_INIT ═══

    SYSTEMD->>DAEMON: ExecStart=rdkFwupdateMgr 3 2
    activate DAEMON

    DAEMON->>DAEMON: log_init() / rdk_logger_ext_init()
    DAEMON->>DAEMON: sigaction(SIGUSR1, handle_signal)
    DAEMON->>DAEMON: Zero XCONFRES response struct
    DAEMON->>DAEMON: currentState = STATE_INIT

    Note over DAEMON,DBUS: D-Bus Setup (before initialize())
    DAEMON->>DAEMON: init_task_system() [GHashTable + tracking init]
    DAEMON->>DBUS: setup_dbus_server()
    DBUS-->>DAEMON: Parse introspection XML
    DBUS-->>DAEMON: g_bus_get_sync(SYSTEM) → connection
    DBUS-->>DAEMON: register_object(/org/rdkfwupdater/Service)
    DBUS-->>DAEMON: g_bus_own_name(org.rdkfwupdater.Interface)
    DAEMON->>DAEMON: main_loop = g_main_loop_new(NULL, FALSE)

    Note over DAEMON,FS: initialize() — same as rdkvfwupgrader
    DAEMON->>FS: getDeviceProperties()
    DAEMON->>FS: getImageDetails()
    DAEMON->>FS: getRFCSettings()
    DAEMON->>FS: createDir(difw_path)
    DAEMON->>IARM: init_event_handler()
    
    alt maint_status == "true"
        DAEMON->>THUNDER: getJsonRpc("getMaintenanceMode")
        THUNDER-->>DAEMON: JSON response
        alt "BACKGROUND" in response
            DAEMON->>DAEMON: setAppMode(0)
        end
    end

    DAEMON->>DAEMON: Parse argv[2] → trigger_type
    DAEMON->>DAEMON: currentState = STATE_INIT_VALIDATION

    Note over DAEMON,FS: ═══ STATE_INIT_VALIDATION ═══
    DAEMON->>DAEMON: initialValidation()
    DAEMON->>FS: GetBuildType(), RFC check, PID check
    
    alt INITIAL_VALIDATION_SUCCESS
        DAEMON->>DAEMON: currentState = STATE_IDLE
    else DWNL_COMPLETED (reboot pending)
        DAEMON->>DAEMON: currentState = STATE_IDLE
    else DWNL_INPROGRESS
        DAEMON->>IARM: eventManager("MaintenanceMGR", INPROGRESS)
        DAEMON->>DAEMON: currentState = STATE_IDLE
    end

    Note over DAEMON,DBUS: ═══ STATE_IDLE (indefinite) ═══
    DAEMON->>DAEMON: g_main_loop_run(main_loop)
    Note right of DAEMON: BLOCKS HERE FOREVER.<br/>All work happens via<br/>D-Bus callbacks dispatched<br/>on this main loop.
    deactivate DAEMON
```

---

## 4. Steady-State Operation — D-Bus Request Handling

### 4.1 CheckForUpdate Request Flow

```mermaid
sequenceDiagram
    participant CLIENT as Client App<br/>(via D-Bus)
    participant MAIN as Main Loop Thread<br/>(process_app_request)
    participant TRACK as Task Tracking<br/>(active_tasks + waiting queue)
    participant WORKER as GTask Worker Thread<br/>(rdkfw_xconf_fetch_worker)
    participant XCONF as XConf Server
    participant SIGNAL as D-Bus Signal<br/>(CheckForUpdateComplete)

    CLIENT->>MAIN: D-Bus call: CheckForUpdate(handler_id)
    activate MAIN
    
    Note over MAIN: Validation (main thread)
    MAIN->>MAIN: Validate handler_id (non-empty)
    MAIN->>MAIN: Validate registration (hash lookup)
    
    Note over MAIN: Immediate Response
    MAIN->>CLIENT: Return (0, "", "", "", "check in progress", 3)
    Note right of CLIENT: Client gets FIRMWARE_CHECK_ERROR<br/>immediately (non-blocking!)
    
    Note over MAIN,TRACK: Task Tracking Setup
    MAIN->>TRACK: create_task_context(CHECK_UPDATE, handler, sender, NULL)
    MAIN->>TRACK: Add task_id to active_tasks hash table
    MAIN->>TRACK: Append task_id to waiting_checkUpdate_ids list
    
    alt getXConfCommStatus() == TRUE (fetch already running)
        Note over MAIN: PIGGYBACK — no new worker spawned
        MAIN->>MAIN: return (task joins existing queue)
    else getXConfCommStatus() == FALSE (no active fetch)
        Note over MAIN: Start New Background Fetch
        MAIN->>MAIN: setXConfCommStatus(TRUE)
        MAIN->>MAIN: Create AsyncXconfFetchContext
        MAIN->>WORKER: g_task_run_in_thread(rdkfw_xconf_fetch_worker)
        Note right of WORKER: Worker thread spawned.<br/>Main loop is FREE.
    end
    deactivate MAIN
    
    Note over WORKER,XCONF: Worker Thread (blocking network I/O)
    activate WORKER
    WORKER->>XCONF: rdkFwupdateMgr_checkForUpdate() [HTTP POST]
    Note right of XCONF: Blocks 5-60 seconds
    XCONF-->>WORKER: JSON response
    WORKER->>WORKER: Parse response → CheckUpdateResponse
    WORKER->>WORKER: Package into GVariant
    WORKER->>WORKER: g_task_return_pointer(result)
    deactivate WORKER

    Note over MAIN,SIGNAL: Completion Callback (main loop thread)
    activate MAIN
    MAIN->>MAIN: rdkfw_xconf_fetch_done() triggered by GTask
    MAIN->>SIGNAL: g_dbus_connection_emit_signal("CheckForUpdateComplete", result)
    Note right of SIGNAL: Broadcast to ALL listeners
    
    loop For each task_id in waiting_checkUpdate_ids
        MAIN->>TRACK: g_hash_table_remove(active_tasks, task_id)
        MAIN->>MAIN: free_task_context(ctx)
    end
    
    MAIN->>TRACK: g_slist_free(waiting_checkUpdate_ids) → NULL
    MAIN->>MAIN: setXConfCommStatus(FALSE)
    MAIN->>MAIN: Free AsyncXconfFetchContext
    deactivate MAIN
```

### 4.2 DownloadFirmware Request Flow

```mermaid
sequenceDiagram
    participant CLIENT as Client App
    participant MAIN as Main Loop Thread
    participant STATE as Download State<br/>(current_download)
    participant WORKER as Download Worker<br/>(GTask thread)
    participant MONITOR as Progress Monitor<br/>(GThread)
    participant CDN as CDN / HTTP Server
    participant SIGNAL as D-Bus Signal<br/>(DownloadProgress)

    CLIENT->>MAIN: D-Bus call: DownloadFirmware(handler, name, url, type)
    activate MAIN
    
    Note over MAIN: Validation Phase (main thread)
    MAIN->>MAIN: Validate handler_id, registration
    MAIN->>MAIN: Validate firmware_name, type (PCI/PDRI/PERIPHERAL)
    MAIN->>MAIN: Validate URL format (http/https prefix)
    
    alt IsDownloadInProgress && different firmware
        MAIN->>CLIENT: Return ("RDKFW_DWNL_FAILED", "DWNL_ERROR", "ongoing download")
        Note right of CLIENT: Rejected
    else File already cached at /opt/CDL/<name>
        MAIN->>CLIENT: Return ("RDKFW_DWNL_SUCCESS", "COMPLETED", "already downloaded")
        MAIN->>SIGNAL: g_idle_add(emit_progress: 100%, COMPLETED)
    else IsDownloadInProgress && same firmware (PIGGYBACK)
        MAIN->>STATE: Append handler_id to waiting_handler_ids
        MAIN->>CLIENT: Return ("RDKFW_DWNL_SUCCESS", "INPROGRESS", "in progress")
        MAIN->>SIGNAL: g_idle_add(emit_progress: current%, INPROGRESS)
    else New download
        MAIN->>STATE: IsDownloadInProgress = TRUE
        MAIN->>STATE: Allocate CurrentDownloadState
        MAIN->>MAIN: Create AsyncDownloadContext
        MAIN->>WORKER: g_task_run_in_thread(rdkfw_download_worker)
        MAIN->>CLIENT: Return ("RDKFW_DWNL_SUCCESS", "INPROGRESS", "started")
    end
    deactivate MAIN

    Note over WORKER,CDN: Worker Thread — Blocking Download
    activate WORKER
    WORKER->>WORKER: Resolve effective URL (client URL or XConf cache)
    WORKER->>WORKER: Build download path: <difw_path>/<firmware_name>
    WORKER->>WORKER: getDeviceProperties(), getRFCSettings()
    WORKER->>WORKER: Build RdkUpgradeContext_t (download_only=1)
    
    Note over WORKER,MONITOR: Spawn Progress Monitor
    WORKER->>MONITOR: g_thread_try_new("rdkfw_progress_monitor")
    activate MONITOR
    Note right of MONITOR: Polls /opt/curl_progress<br/>every 100ms
    
    WORKER->>CDN: rdkv_upgrade_request(upgrade_ctx)
    Note right of CDN: BLOCKING. Minutes for<br/>large firmware images.
    
    loop While download active
        MONITOR->>MONITOR: Read /opt/curl_progress
        MONITOR->>SIGNAL: g_idle_add(emit_progress: N%, INPROGRESS)
        SIGNAL-->>CLIENT: D-Bus signal: DownloadProgress
    end
    
    CDN-->>WORKER: Download complete (or error)
    
    WORKER->>MONITOR: Set stop_flag = TRUE (via atomic int)
    MONITOR->>MONITOR: Detects stop, exits loop
    deactivate MONITOR
    WORKER->>WORKER: g_thread_join(monitor_thread)
    
    WORKER->>SIGNAL: g_idle_add(emit_progress: 100%, COMPLETED)
    WORKER->>WORKER: g_task_return_boolean(task, TRUE/FALSE)
    deactivate WORKER
    
    Note over MAIN: Completion Callback (main loop)
    activate MAIN
    MAIN->>MAIN: rdkfw_download_done() [GTask callback]
    MAIN->>STATE: IsDownloadInProgress = FALSE
    MAIN->>STATE: Free current_download
    deactivate MAIN
```

### 4.3 UpdateFirmware Request Flow

```mermaid
sequenceDiagram
    participant CLIENT as Client App
    participant MAIN as Main Loop Thread
    participant WORKER as Flash Worker<br/>(GTask thread)
    participant FLASH as Flash HAL
    participant SIGNAL as D-Bus Signal<br/>(UpdateProgress)

    CLIENT->>MAIN: D-Bus call: UpdateFirmware(handler, name, path, type, reboot)
    activate MAIN
    
    MAIN->>MAIN: Validate handler, registration
    MAIN->>MAIN: Check: IsDownloadInProgress?
    MAIN->>MAIN: Check: IsFlashInProgress?
    MAIN->>MAIN: Validate firmware_name, path, type
    
    alt IsDownloadInProgress
        MAIN->>CLIENT: Return ("RDKFW_UPDATE_FAILED", "UPDATE_ERROR", "ongoing download")
    else IsFlashInProgress
        MAIN->>CLIENT: Return ("RDKFW_UPDATE_FAILED", "UPDATE_ERROR", "ongoing flash")
    else Validation passed
        MAIN->>MAIN: IsFlashInProgress = TRUE
        MAIN->>MAIN: Create AsyncUpdateContext
        MAIN->>WORKER: g_task_run_in_thread(rdkfw_flash_worker)
        MAIN->>CLIENT: Return ("RDKFW_UPDATE_SUCCESS", "INPROGRESS", "started")
    end
    deactivate MAIN

    Note over WORKER,FLASH: Worker Thread — Blocking Flash
    activate WORKER
    WORKER->>FLASH: flashImage(firmware_path) [BLOCKING]
    FLASH-->>WORKER: Flash result
    
    alt rebootImmediately == "true"
        WORKER->>WORKER: Schedule reboot
    end
    
    WORKER->>SIGNAL: g_idle_add(emit: 100%, COMPLETED/ERROR)
    WORKER->>WORKER: g_task_return_boolean(task, result)
    deactivate WORKER
    
    Note over MAIN: Completion Callback
    activate MAIN
    MAIN->>MAIN: rdkfw_flash_done()
    MAIN->>MAIN: IsFlashInProgress = FALSE
    MAIN->>MAIN: Free current_flash
    deactivate MAIN
```

### 4.4 RegisterProcess / UnregisterProcess

```mermaid
sequenceDiagram
    participant CLIENT as Client App
    participant MAIN as Main Loop Thread<br/>(process_app_request)
    participant REG as Process Registry<br/>(GHashTable)

    Note over CLIENT,REG: RegisterProcess — Synchronous (no worker thread)
    CLIENT->>MAIN: D-Bus call: RegisterProcess(name, version)
    activate MAIN
    MAIN->>MAIN: Validate process_name, lib_version
    MAIN->>REG: add_process_to_tracking(name, sender_id) → handler_id
    MAIN->>CLIENT: Return (handler_id) [uint64]
    deactivate MAIN
    Note right of CLIENT: All on main thread.<br/>No async. No GTask.

    Note over CLIENT,REG: UnregisterProcess — Synchronous
    CLIENT->>MAIN: D-Bus call: UnregisterProcess(handler_id, name)
    activate MAIN
    MAIN->>REG: remove_process_from_tracking(handler, sender)
    MAIN->>CLIENT: Return (boolean success)
    deactivate MAIN
```

---

## 5. Concurrency Control Summary

| Guard Variable | Protected By | Scope | Purpose |
|---------------|--------------|-------|---------|
| `XConfCommStatus` | `G_LOCK(xconf_status_mutex)` | Global | Prevents duplicate XConf fetch workers |
| `IsDownloadInProgress` | Main loop serialization | Global (bool) | Prevents concurrent downloads |
| `IsFlashInProgress` | Main loop serialization | Global (bool) | Prevents concurrent flashes |
| `DwnlState` | `pthread_mutex_t mutuex_dwnl_state` | Per-process | Download progress state |
| `app_mode` | `pthread_mutex_t app_mode_status` | Per-process | Throttle mode (FG/BG) |
| `registered_processes` | Main loop serialization | GHashTable | Client registry |
| `active_tasks` | Main loop serialization | GHashTable | In-flight task tracking |
| `waiting_checkUpdate_ids` | Main loop serialization | GSList | Piggyback queue for CheckUpdate |
| `waiting_download_ids` | Main loop serialization | GSList | Piggyback queue for Download |

**[FACT]** Most mutable state is only touched from the main loop thread — GTask completion callbacks and `g_idle_add` ensure main-loop serialization. Worker threads communicate results back via `g_task_return_*()`.

---

## 6. Daemon Shutdown Sequence

```mermaid
sequenceDiagram
    participant SIG as Signal / systemd
    participant LOOP as GLib Main Loop
    participant DBUS as D-Bus Subsystem
    participant TASK as Task System
    participant INIT as Subsystem Cleanup

    SIG->>LOOP: SIGTERM or g_main_loop_quit()
    activate LOOP
    LOOP->>LOOP: g_main_loop_run() returns
    
    Note over LOOP,INIT: cleanup_and_exit label
    LOOP->>DBUS: cleanup_dbus()
    activate DBUS
    DBUS->>DBUS: cleanupXConfCommStatus()
    DBUS->>TASK: Iterate active_tasks → free_task_context() each
    DBUS->>TASK: g_hash_table_destroy(active_tasks)
    DBUS->>DBUS: cleanup_process_tracking()
    DBUS->>DBUS: g_dbus_connection_unregister_object()
    DBUS->>DBUS: g_object_unref(connection)
    DBUS->>DBUS: g_bus_unown_name(owner_id)
    DBUS->>DBUS: g_main_loop_unref(main_loop)
    deactivate DBUS
    
    LOOP->>INIT: uninitialize(init_validate_status)
    activate INIT
    INIT->>INIT: t2_uninit()
    INIT->>INIT: pthread_mutex_destroy() × 2
    INIT->>INIT: term_event_handler() [IARM disconnect]
    INIT->>INIT: updateUpgradeFlag(2)
    INIT->>INIT: unlink("/tmp/DIFD.pid") [if applicable]
    deactivate INIT
    
    LOOP->>LOOP: log_exit()
    LOOP->>LOOP: exit(ret_curl_code)
    deactivate LOOP
```

---

## 7. Key Behavioral Differences from rdkvfwupgrader

| Aspect | rdkvfwupgrader (one-shot) | rdkFwupdateMgr (daemon) |
|--------|--------------------------|------------------------|
| XConf fetch | Synchronous in main() | GTask worker thread |
| Download | Synchronous, blocks everything | GTask worker + progress monitor thread |
| Flash | Synchronous | GTask worker thread |
| D-Bus | Not used | Core IPC mechanism |
| Client interaction | None | RegisterProcess → methods → signals |
| Lifetime | Seconds-to-minutes | Indefinite |
| Error recovery | Exit with error code | Log error, reset state, remain running |
| Concurrency | Single operation | Multiple piggybacked clients |
| Signal handling | Abort + exit | Abort current operation, remain running |
| Main loop | Linear execution | GLib event loop (g_main_loop_run) |

---

## 8. Operational Invariants

1. **[FACT]** D-Bus setup occurs BEFORE `initialize()` — the daemon is reachable before initialization completes
2. **[FACT]** The state machine always converges to `STATE_IDLE` unless `INITIAL_VALIDATION_FAIL`
3. **[FACT]** Once in `STATE_IDLE`, the daemon never leaves it (no state transitions occur)
4. **[FACT]** All request validation runs synchronously on the main loop thread
5. **[FACT]** Only one XConf fetch can run at a time (enforced by `XConfCommStatus`)
6. **[FACT]** Only one download can run at a time (enforced by `IsDownloadInProgress`)
7. **[FACT]** Only one flash can run at a time (enforced by `IsFlashInProgress`)
8. **[FACT]** Worker threads never directly emit D-Bus signals — always via `g_idle_add()` or `g_task_return_*()`
9. **[FACT]** Client gets immediate D-Bus response; real results arrive via signal broadcast
10. **[INFERENCE]** The XConf commented-out code in STATE_INIT_VALIDATION indicates future intent to do proactive update checks on startup
