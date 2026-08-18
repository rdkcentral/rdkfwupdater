# Subsystem Map — Shared vs Execution-Model-Specific

> **Purpose:** Classify every identified subsystem by its relationship to the two execution models  
> **Evidence:** Build targets from `Makefile.am`, source inclusion per binary

---

## 1. Classification Matrix

```
                        ┌───────────────────┬───────────────────┐
                        │   rdkvfwupgrader   │  rdkFwupdateMgr   │
                        │    (one-shot)      │     (daemon)       │
┌───────────────────────┼───────────────────┼───────────────────┤
│ SHARED SUBSYSTEMS     │                   │                    │
│                       │                   │                    │
│ Firmware Upgrade      │    ■ (direct)     │  ■ (via GTask)     │
│   Engine              │                   │                    │
│                       │                   │                    │
│ XConf Communication   │  ■ (MakeXconf     │  ■ (checkForUpdate │
│                       │    Comms)         │    + cache)        │
│                       │                   │                    │
│ Flash Subsystem       │  ■ (inline after  │  ■ (via GTask      │
│                       │    download)      │    worker)         │
│                       │                   │                    │
│ IARM Event Interface  │    ■              │    ■               │
│                       │                   │                    │
│ Device & FW Identity  │    ■              │    ■               │
│                       │                   │                    │
│ XConf Response Parse  │    ■              │    ■               │
│                       │                   │                    │
│ RFC Configuration     │    ■              │    ■               │
│                       │                   │                    │
│ Download Status       │    ■              │    ■               │
│                       │                   │                    │
│ CEDM / Cert Auth      │    ■              │    ■               │
│                       │                   │                    │
│ Telemetry (T2)        │    ■              │    ■               │
│                       │                   │                    │
│ rBus Integration      │    ■              │    ■               │
│                       │                   │                    │
│ Process Safety Guards │    ■              │    ■               │
├───────────────────────┼───────────────────┼───────────────────┤
│ EXECUTION-MODEL-      │                   │                    │
│ SPECIFIC              │                   │                    │
│                       │                   │                    │
│ One-Shot Orchestrator │    ■              │                    │
│                       │                   │                    │
│ Daemon Orchestrator   │                   │    ■               │
│                       │                   │                    │
│ D-Bus Service Runtime │                   │    ■               │
│                       │                   │                    │
│ Concurrency Control   │                   │    ■               │
├───────────────────────┼───────────────────┼───────────────────┤
│ CLIENT ECOSYSTEM      │                   │                    │
│                       │                   │                    │
│ Client Library SDK    │                   │  ■ (consumer of    │
│ (librdkFwupdateMgr)   │                   │    daemon D-Bus)   │
└───────────────────────┴───────────────────┴───────────────────┘
```

---

## 2. Build-Level Source Mapping

### rdkvfwupgrader Binary Sources (from Makefile.am)

| Source File | Subsystem |
|-------------|-----------|
| `src/rdkv_main.c` | One-Shot Orchestrator |
| `src/chunk.c` | Firmware Upgrade Engine (compiled-in copy) |
| `src/device_status_helper.c` | Process Safety Guards, Device Identity |
| `src/rbusInterface/rbusInterface.c` | rBus Integration |
| **Linked libraries:** | |
| `librdksw_upgrade.so` | Firmware Upgrade Engine |
| `librdksw_jsonparse.so` | XConf Response Parsing |
| `librdksw_rfcIntf.so` | RFC Configuration |
| `librdksw_iarmIntf.so` | IARM Event Interface |
| `librdksw_flash.so` | Flash Subsystem |
| `librdksw_fwutils.so` | Device & Firmware Identity |

### rdkFwupdateMgr Binary Sources (from Makefile.am)

| Source File | Subsystem |
|-------------|-----------|
| `src/rdkFwupdateMgr.c` | Daemon Orchestrator |
| `src/chunk.c` | Firmware Upgrade Engine (compiled-in copy) |
| `src/device_status_helper.c` | Process Safety Guards, Device Identity |
| `src/rbusInterface/rbusInterface.c` | rBus Integration |
| `src/dbus/rdkv_dbus_server.c` | D-Bus Service Runtime |
| `src/dbus/rdkFwupdateMgr_handlers.c` | XConf Communication (daemon), D-Bus handlers |
| `src/dbus/xconf_comm_status.c` | Concurrency Control |
| **Linked libraries:** | (same as rdkvfwupgrader +) |
| `$(GLIB_LIBS)` | GLib/GIO for D-Bus, GTask, main loop |

### librdkFwupdateMgr.so Sources

| Source File | Subsystem |
|-------------|-----------|
| `librdkFwupdateMgr/src/rdkFwupdateMgr_process.c` | Client Library SDK |
| `librdkFwupdateMgr/src/rdkFwupdateMgr_api.c` | Client Library SDK |
| `librdkFwupdateMgr/src/rdkFwupdateMgr_async.c` | Client Library SDK |

---

## 3. Behavioral Differences by Execution Model

### Shared Subsystems: Same Code, Different Runtime Behavior

| Subsystem | One-Shot Behavior | Daemon Behavior |
|-----------|-------------------|-----------------|
| **Firmware Upgrade Engine** | Called synchronously from `main()`, blocks entire process | Called from GTask worker thread, blocks only worker |
| **XConf Communication** | Direct `MakeXconfComms()` → blocking in main thread | `rdkFwupdateMgr_checkForUpdate()` → cache-first with memory+file, async worker |
| **Flash Subsystem** | Called inline after download completes | Called via GTask worker, reports via `g_idle_add()` signals |
| **IARM Event Interface** | Callback (`DwnlStopEventHandler`) interrupts active download | Same callback, but in daemon context IARM may deliver via GLib integration |
| **Device & FW Identity** | Read once at `initialize()`, globals for process lifetime | Read per-request in worker threads (fresh `DeviceProperty_t` per download) |
| **RFC Configuration** | Read once at `initialize()`, stored in global `rfc_list` | Read per-request in worker threads (fresh `Rfc_t` per download) |
| **Download Status** | Written inline during download progress | Written from worker thread (same function, different thread context) |
| **Process Safety** | PID file + `CurrentRunningInst()` guards single instance | Same PID logic, but daemon always transitions to IDLE regardless |

### One-Shot-Specific Behaviors

| Behavior | Location | Description |
|----------|----------|-------------|
| Linear pipeline execution | `rdkv_main.c:main()` | Init → Validate → XConf → Download → Flash → Exit |
| Multi-firmware chaining | `checkTriggerUpgrade()` | PCI → sleep(30) → PDRI → Peripheral loop |
| `exit()` on fatal errors | `rdkv_main.c` | 8+ exit paths with `uninitialize()` cleanup |
| SIGUSR1 → abort + exit | `handle_signal()` | Sets `force_exit`, aborts curl, exits |
| Throttle-to-zero → exit | `interuptDwnl()` | When speed=0, sets `force_exit` |
| State Red recovery | `isInStateRed()` | Special boot-time recovery path |

### Daemon-Specific Behaviors

| Behavior | Location | Description |
|----------|----------|-------------|
| GLib main loop (indefinite) | `rdkFwupdateMgr.c:STATE_IDLE` | `g_main_loop_run()` — never returns normally |
| GTask async offloading | `rdkv_dbus_server.c` | `g_task_run_in_thread()` for CheckUpdate, Download, Flash |
| Piggyback queues | `rdkv_dbus_server.c` | `waiting_checkUpdate_ids`, `waiting_download_ids` |
| D-Bus signal broadcast | `rdkv_dbus_server.c` | `g_dbus_connection_emit_signal()` to all subscribers |
| XConf cache (memory+file) | `rdkFwupdateMgr_handlers.c` | `g_cached_xconf_data` with G_LOCK protection |
| Progress monitor thread | `rdkv_dbus_server.c` | Dedicated GThread polls `/opt/curl_progress` |
| `download_only=1` | `rdkv_dbus_server.c` | Decouples download from flash |
| Error → continue serving | Everywhere | Never exits on operation errors |
| D-Bus setup BEFORE init | `rdkFwupdateMgr.c:STATE_INIT` | Daemon reachable during initialization |

---

## 4. Shared vs Daemon-Specific State Ownership

```
┌─────────────────────────────────────────────────────────────────┐
│                   SHARED GLOBAL STATE                           │
│  (defined in rdkv_main.c / rdkFwupdateMgr.c,                  │
│   used by shared libraries)                                     │
│                                                                 │
│  DeviceProperty_t device_info    ← populated by initialize()   │
│  ImageDetails_t cur_img_detail   ← populated by initialize()   │
│  Rfc_t rfc_list                  ← populated by initialize()   │
│  int DwnlState                  ← mutex-protected              │
│  int app_mode                    ← mutex-protected              │
│  int force_exit                  ← set by signal/callback       │
│  void *curl                     ← active curl handle            │
│  bool isCriticalUpdate           ← reboot decision flag         │
│  int trigger_type                ← from argv[2]                 │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│              DAEMON-ONLY STATE                                  │
│  (defined in src/dbus/ files, only in rdkFwupdateMgr)          │
│                                                                 │
│  GDBusConnection *connection     ← D-Bus connection             │
│  GMainLoop *main_loop            ← GLib event loop              │
│  GHashTable *registered_processes ← client registry             │
│  GHashTable *active_tasks         ← in-flight tasks             │
│  GSList *waiting_checkUpdate_ids  ← piggyback queue             │
│  GSList *waiting_download_ids     ← piggyback queue             │
│  gboolean IsDownloadInProgress    ← download guard              │
│  gboolean IsFlashInProgress       ← flash guard                 │
│  gboolean IsCheckUpdateInProgress ← XConf guard (mutex)         │
│  CurrentDownloadState *current_download ← download tracking     │
│  CurrentFlashState *current_flash      ← flash tracking         │
│  XCONFRES g_cached_xconf_data    ← XConf response cache         │
│  guint next_task_id              ← monotonic task counter        │
└─────────────────────────────────────────────────────────────────┘
```

---

## 5. Cross-Cutting Concerns

| Concern | Subsystems Affected | Notes |
|---------|--------------------|-|
| **Thread safety** | Upgrade Engine, XConf Comm, Flash, Concurrency Control | Daemon workers must not touch main-loop-owned state directly |
| **Error propagation** | All subsystems | One-shot: exit codes; Daemon: D-Bus error responses + signals |
| **Logging** | All subsystems | `SWLOG_INFO/ERROR` macros, RDK logger integration |
| **Telemetry** | All subsystems | `t2CountNotify()` calls sprinkled throughout |
| **Resource cleanup** | All subsystems | Must handle graceful shutdown in both models |
| **Build conditionality** | IARM, RFC, T2, CEDM, rBus | `#ifdef` guards create compile-time subsystem variants |
| **Global mutable state** | Orchestrators, Upgrade Engine, IARM | `device_info`, `rfc_list`, `curl`, `force_exit` — shared globals |
