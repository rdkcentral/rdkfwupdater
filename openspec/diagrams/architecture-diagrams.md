# Architecture Diagrams

> Mermaid-format diagrams for engineering design reviews.  
> Render with any Mermaid-compatible tool (GitHub, VS Code preview, mermaid.live).

---

## 1. System Component Diagram

```mermaid
graph TB
    subgraph "Client Layer"
        APP1["Client App 1<br/>(example_plugin)"]
        APP2["Client App N"]
    end

    subgraph "Client Library"
        LIB["librdkFwupdateMgr.so<br/>├── Process Registration<br/>├── Async API (fire-and-forget)<br/>├── Callback Registry<br/>└── Background D-Bus Thread"]
    end

    subgraph "IPC Layer"
        DBUS["System D-Bus<br/>org.rdkfwupdater.Interface"]
    end

    subgraph "Daemon Process (rdkFwupdateMgr)"
        DBSRV["D-Bus Server<br/>(GDBus)"]
        HANDLERS["Method Handlers<br/>├── RegisterProcess<br/>├── CheckForUpdate<br/>├── DownloadFirmware<br/>└── UpdateFirmware"]
        XCONF_CACHE["XConf Cache<br/>(file + memory)"]
        WORKERS["Worker Threads<br/>├── GTask (XConf fetch)<br/>├── GTask (Download)<br/>└── GThread (Flash)"]
        CONCURRENCY["Concurrency Control<br/>├── xconf_comm_status (mutex)<br/>├── IsDownloadInProgress<br/>├── IsFlashInProgress<br/>└── Piggybacking queues"]
    end

    subgraph "One-Shot Binary (rdkvfwupgrader)"
        ONESHOT["rdkvfwupgrader<br/>├── XConf query<br/>├── Download<br/>├── Flash<br/>└── Exit"]
    end

    subgraph "Shared Libraries"
        UPGRADE["librdksw_upgrade<br/>(HTTP download engine)"]
        JSONPARSE["librdksw_jsonparse<br/>(XConf JSON parser)"]
        FLASH_LIB["librdksw_flash<br/>(Image flash + status)"]
        RFC["librdksw_rfcIntf<br/>(RFC settings)"]
        IARM_LIB["librdksw_iarmIntf<br/>(IARM events)"]
        FWUTILS["librdksw_fwutils<br/>(Device utilities)"]
    end

    subgraph "External Services"
        XCONF_SRV["XConf Cloud Server"]
        CDN["CDN / HTTP Server"]
        MM["Maintenance Manager"]
        IARM_BUS["IARM Bus"]
        RBUS["rBus"]
    end

    subgraph "Device Hardware"
        FLASH_HW["Flash Storage"]
    end

    APP1 --> LIB
    APP2 --> LIB
    LIB <--> DBUS
    DBUS <--> DBSRV
    DBSRV --> HANDLERS
    HANDLERS --> XCONF_CACHE
    HANDLERS --> WORKERS
    HANDLERS --> CONCURRENCY
    
    WORKERS --> UPGRADE
    WORKERS --> JSONPARSE
    WORKERS --> FLASH_LIB
    
    ONESHOT --> UPGRADE
    ONESHOT --> JSONPARSE
    ONESHOT --> FLASH_LIB
    ONESHOT --> RFC
    ONESHOT --> IARM_LIB
    ONESHOT --> FWUTILS
    
    UPGRADE --> XCONF_SRV
    UPGRADE --> CDN
    FLASH_LIB --> FLASH_HW
    IARM_LIB --> IARM_BUS
    ONESHOT -.-> MM
    
    style DBSRV fill:#2d5986,color:#fff
    style HANDLERS fill:#2d5986,color:#fff
    style WORKERS fill:#2d5986,color:#fff
    style CONCURRENCY fill:#2d5986,color:#fff
    style XCONF_CACHE fill:#2d5986,color:#fff
    style LIB fill:#4a7c59,color:#fff
    style ONESHOT fill:#8b6914,color:#fff
    style UPGRADE fill:#555,color:#fff
    style JSONPARSE fill:#555,color:#fff
    style FLASH_LIB fill:#555,color:#fff
    style RFC fill:#555,color:#fff
    style IARM_LIB fill:#555,color:#fff
    style FWUTILS fill:#555,color:#fff
```

---

## 2. Daemon Internal Architecture

```mermaid
graph TD
    subgraph "Main Thread (GLib Main Loop)"
        ML["g_main_loop_run()"]
        DISPATCH["process_app_request()<br/>(method dispatcher)"]
        SIGNAL_EMIT["Signal Emission<br/>(via g_idle_add)"]
        TASK_TRACK["Task Tracking<br/>(GHashTable)"]
        PROC_TRACK["Process Tracking<br/>(GHashTable)"]
        DL_STATE["CurrentDownloadState<br/>(single active download)"]
    end

    subgraph "Worker Threads"
        W_XCONF["XConf Fetch Worker<br/>(GTask thread pool)"]
        W_DWNL["Download Worker<br/>(GTask thread pool)"]
        W_FLASH["Flash Worker<br/>(dedicated GThread)"]
        W_PROGRESS["Progress Monitor<br/>(polling thread)"]
    end

    subgraph "Thread-Safe Modules"
        XCONF_STATUS["xconf_comm_status<br/>(GMutex protected)"]
        XCONF_FILE_CACHE["File Cache Mutex<br/>(G_LOCK xconf_cache)"]
        XCONF_MEM_CACHE["Memory Cache Mutex<br/>(G_LOCK xconf_data_cache)"]
    end

    ML --> DISPATCH
    ML --> SIGNAL_EMIT
    DISPATCH --> TASK_TRACK
    DISPATCH --> PROC_TRACK
    DISPATCH --> DL_STATE
    
    DISPATCH -->|"GTask"| W_XCONF
    DISPATCH -->|"GTask"| W_DWNL
    DISPATCH -->|"GThread"| W_FLASH
    W_DWNL -->|"spawn"| W_PROGRESS
    
    W_XCONF -->|"g_idle_add"| SIGNAL_EMIT
    W_DWNL -->|"g_idle_add"| SIGNAL_EMIT
    W_FLASH -->|"g_idle_add"| SIGNAL_EMIT
    W_PROGRESS -->|"g_idle_add"| SIGNAL_EMIT
    
    W_XCONF --> XCONF_STATUS
    W_XCONF --> XCONF_FILE_CACHE
    W_XCONF --> XCONF_MEM_CACHE
    DISPATCH --> XCONF_STATUS

    style ML fill:#1a4d2e,color:#fff
    style DISPATCH fill:#1a4d2e,color:#fff
    style SIGNAL_EMIT fill:#1a4d2e,color:#fff
    style W_XCONF fill:#4a2d7c,color:#fff
    style W_DWNL fill:#4a2d7c,color:#fff
    style W_FLASH fill:#4a2d7c,color:#fff
    style W_PROGRESS fill:#4a2d7c,color:#fff
    style XCONF_STATUS fill:#7c4a2d,color:#fff
    style XCONF_FILE_CACHE fill:#7c4a2d,color:#fff
    style XCONF_MEM_CACHE fill:#7c4a2d,color:#fff
```

---

## 3. Client Library Threading Model

```mermaid
graph LR
    subgraph "Application Thread"
        REG["registerProcess()"]
        CFU["checkForUpdate()"]
        DFW["downloadFirmware()"]
        UFW["updateFirmware()"]
        URG["unregisterProcess()"]
        WAIT["pthread_cond_wait()"]
    end

    subgraph "Ephemeral D-Bus Connections"
        EC1["conn :1.142<br/>(created per call)"]
        EC2["conn :1.143<br/>(created per call)"]
    end

    subgraph "Background Thread"
        PERSISTENT["Persistent conn :1.141"]
        GLOOP["g_main_loop_run()"]
        SUB1["Subscribe: CheckForUpdateComplete"]
        SUB2["Subscribe: DownloadProgress"]
        SUB3["Subscribe: UpdateProgress"]
        DISP["dispatch_all_*()"]
    end

    subgraph "Registries (mutex-protected)"
        R1["g_registry<br/>(30 slots, checkForUpdate)"]
        R2["g_dwnl_registry<br/>(30 slots, download)"]
        R3["g_update_registry<br/>(30 slots, update)"]
    end

    REG -->|sync| EC1
    CFU -->|fire&forget| EC2
    DFW -->|fire&forget| EC2
    UFW -->|fire&forget| EC2
    URG -->|sync| EC1
    
    CFU -->|"register cb"| R1
    DFW -->|"register cb"| R2
    UFW -->|"register cb"| R3
    
    GLOOP --> SUB1
    GLOOP --> SUB2
    GLOOP --> SUB3
    SUB1 --> DISP
    SUB2 --> DISP
    SUB3 --> DISP
    DISP -->|"read slots"| R1
    DISP -->|"read slots"| R2
    DISP -->|"read slots"| R3
    DISP -->|"invoke callback"| WAIT

    style REG fill:#2d6986,color:#fff
    style CFU fill:#2d6986,color:#fff
    style DFW fill:#2d6986,color:#fff
    style UFW fill:#2d6986,color:#fff
    style GLOOP fill:#692d86,color:#fff
    style DISP fill:#692d86,color:#fff
    style R1 fill:#86692d,color:#fff
    style R2 fill:#86692d,color:#fff
    style R3 fill:#86692d,color:#fff
```

---

## 4. Build Artifact Dependency Graph

```mermaid
graph TD
    subgraph "Binaries"
        BIN1["rdkvfwupgrader"]
        BIN2["rdkFwupdateMgr"]
        BIN3["testClient"]
        BIN4["example_plugin"]
    end

    subgraph "Internal Libraries"
        L1["librdksw_upgrade.so"]
        L2["librdksw_jsonparse.so"]
        L3["librdksw_flash.so"]
        L4["librdksw_rfcIntf.so"]
        L5["librdksw_iarmIntf.so"]
        L6["librdksw_fwutils.so"]
        L7["librdkFwupdateMgr.so"]
    end

    subgraph "External Dependencies"
        E1["libcurl"]
        E2["libcjson"]
        E3["GLib/GIO"]
        E4["libdwnlutil"]
        E5["libfwutils"]
        E6["librdkloggers"]
        E7["libsecure_wrapper"]
        E8["libparsejson"]
        E9["librbus"]
        E10["libpthread"]
    end

    BIN1 --> L1 & L2 & L3 & L4 & L5 & L6
    BIN2 --> L1 & L2 & L3 & L4 & L5 & L6
    BIN2 -->|"+ D-Bus server"| E3
    BIN3 --> E3
    BIN4 --> L7
    L7 --> E3 & E10

    L1 --> E1 & E2
    L2 --> E2
    L3 --> E1

    style BIN1 fill:#8b6914,color:#fff
    style BIN2 fill:#2d5986,color:#fff
    style BIN3 fill:#666,color:#fff
    style BIN4 fill:#4a7c59,color:#fff
    style L7 fill:#4a7c59,color:#fff
```

---

## 5. Daemon State Machine

```mermaid
stateDiagram-v2
    direction LR
    
    [*] --> STATE_INIT: Process start
    
    STATE_INIT --> STATE_INIT_VALIDATION: D-Bus + init OK
    STATE_INIT --> CLEANUP: Init failed
    
    STATE_INIT_VALIDATION --> STATE_IDLE: Validation OK
    STATE_INIT_VALIDATION --> STATE_IDLE: Download complete
    STATE_INIT_VALIDATION --> STATE_IDLE: Download in-progress
    STATE_INIT_VALIDATION --> CLEANUP: Validation failed
    
    state STATE_IDLE {
        direction TB
        [*] --> WAITING: Enter main loop
        WAITING --> HANDLE_REQUEST: D-Bus method call
        HANDLE_REQUEST --> SPAWN_WORKER: Async operation
        SPAWN_WORKER --> WAITING: Worker running
        HANDLE_REQUEST --> WAITING: Sync response
    }
    
    STATE_IDLE --> CLEANUP: g_main_loop_quit()
    
    CLEANUP --> [*]: exit()
    
    note right of STATE_IDLE
        Runs indefinitely.
        All firmware operations
        handled via D-Bus + GTask.
    end note
```

---

## 6. XConf Cache Decision Flow

```mermaid
flowchart TD
    A[CheckForUpdate Request] --> B{Cache exists?}
    
    B -->|Yes| C[load_xconf_from_cache]
    B -->|No| D{XConf fetch in progress?}
    
    C --> E{Parse successful?}
    E -->|Yes| F[Validate firmware vs model]
    E -->|No| D
    
    F --> G{Compatible?}
    G -->|Yes, newer| H[Return FIRMWARE_AVAILABLE]
    G -->|Same version| I[Return FIRMWARE_NOT_AVAILABLE]
    G -->|Incompatible| J[Return UPDATE_NOT_ALLOWED]
    
    D -->|Yes| K[Add to waiting queue]
    D -->|No| L[Spawn XConf fetch worker]
    
    L --> M[Return FIRMWARE_CHECK_ERROR immediately]
    K --> M
    
    M --> N[Worker: HTTP POST to XConf]
    N --> O[Worker: save_xconf_to_cache]
    O --> P[Emit CheckForUpdateComplete signal]
    P --> Q[Process waiting queue]

    style H fill:#2d7c2d,color:#fff
    style I fill:#7c7c2d,color:#fff
    style J fill:#7c2d2d,color:#fff
    style M fill:#4a4a7c,color:#fff
```
