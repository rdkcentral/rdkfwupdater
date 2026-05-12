# Subsystem Architecture Diagrams

> **Scope:** Layered architecture, subsystem relationships, ownership maps  
> **Format:** Mermaid diagrams for rendering in Markdown viewers

---

## 1. Layered Architecture — Both Execution Models

```mermaid
block-beta
    columns 5
    
    block:clients:5
        A["Client Applications<br/>(example_plugin, etc.)"]
    end
    
    block:sdk:5
        B["Client Library SDK<br/>librdkFwupdateMgr.so<br/>(D-Bus proxy + async callbacks)"]
    end
    
    space:5
    
    block:orchestrators:5
        C["One-Shot Orchestrator<br/>rdkv_main.c<br/>(linear pipeline)"]
        D["Daemon Orchestrator<br/>rdkFwupdateMgr.c<br/>(state machine + GLib loop)"]
    end
    
    block:dbus_layer:5
        E["D-Bus Service Runtime<br/>rdkv_dbus_server.c<br/>(dispatch, tasks, signals, concurrency)"]
    end
    
    block:core:5
        F["Firmware Upgrade Engine<br/>librdksw_upgrade.so"]
        G["XConf Communication<br/>json_process.c + handlers.c"]
        H["Flash Subsystem<br/>librdksw_flash.so"]
    end
    
    block:infra:5
        I["Device Identity<br/>librdksw_fwutils.so"]
        J["RFC Config<br/>librdksw_rfcIntf.so"]
        K["XConf Parsing<br/>librdksw_jsonparse.so"]
        L["CEDM / Certs<br/>codebigUtils + mtlsUtils"]
        M["Download Status<br/>download_status_helper.c"]
    end
    
    block:ipc:5
        N["IARM Events<br/>librdksw_iarmIntf.so"]
        O["rBus Integration<br/>rbusInterface.c"]
        P["Telemetry (T2)<br/>t2CountNotify"]
    end
    
    block:external:5
        Q["External:<br/>libcurl · libcjson · GLib/GIO · IARMBus · rbus · libdwnlutil"]
    end

    A --> B
    B --> E
    C --> F
    C --> G
    C --> H
    D --> E
    E --> F
    E --> G
    E --> H
    F --> I
    F --> J
    F --> L
    F --> M
    G --> K
    G --> I
    H --> I
    H --> M
    F --> N
    H --> N
    F --> Q
```

---

## 2. Subsystem Relationship / Interaction Map

```mermaid
flowchart TD
    subgraph "Execution Models"
        ONESHOT["One-Shot Orchestrator<br/>(rdkv_main.c)"]
        DAEMON["Daemon Orchestrator<br/>(rdkFwupdateMgr.c)"]
    end

    subgraph "Daemon-Specific Layer"
        DBUS["D-Bus Service Runtime<br/>(rdkv_dbus_server.c)"]
        HANDLERS["D-Bus Handlers<br/>(rdkFwupdateMgr_handlers.c)"]
        XCONF_STATUS["Concurrency Control<br/>(xconf_comm_status.c)"]
        CLIENT_LIB["Client Library SDK<br/>(librdkFwupdateMgr.so)"]
    end

    subgraph "Core Shared Subsystems"
        UPGRADE["Firmware Upgrade Engine<br/>(librdksw_upgrade.so)"]
        FLASH["Flash Subsystem<br/>(librdksw_flash.so)"]
        XCONF_PARSE["XConf Response Parsing<br/>(librdksw_jsonparse.so)"]
    end

    subgraph "Infrastructure Layer"
        DEV_IDENTITY["Device & FW Identity<br/>(librdksw_fwutils.so)"]
        RFC["RFC Configuration<br/>(librdksw_rfcIntf.so)"]
        CEDM["CEDM / Cert Auth<br/>(codebigUtils + mtlsUtils)"]
        DL_STATUS["Download Status<br/>(download_status_helper.c)"]
    end

    subgraph "IPC / Integration Layer"
        IARM["IARM Event Interface<br/>(librdksw_iarmIntf.so)"]
        RBUS["rBus Integration<br/>(rbusInterface.c)"]
    end

    subgraph "Cross-Cutting"
        T2["Telemetry (T2)"]
        SAFETY["Process Safety Guards<br/>(device_status_helper.c)"]
    end

    %% One-shot relationships
    ONESHOT -->|"synchronous call"| UPGRADE
    ONESHOT -->|"synchronous call"| FLASH
    ONESHOT -->|"synchronous call"| XCONF_PARSE
    ONESHOT -->|"init/term"| IARM
    ONESHOT -->|"initialValidation"| SAFETY
    ONESHOT -->|"getRFCSettings"| RFC
    ONESHOT -->|"getDeviceProperties"| DEV_IDENTITY

    %% Daemon relationships
    DAEMON -->|"setup + main loop"| DBUS
    DBUS -->|"dispatch"| HANDLERS
    DBUS -->|"check status"| XCONF_STATUS
    DBUS -->|"GTask worker"| UPGRADE
    DBUS -->|"GTask worker"| FLASH
    HANDLERS -->|"XConf query"| XCONF_PARSE
    HANDLERS -->|"device info"| DEV_IDENTITY
    DAEMON -->|"init/term"| IARM
    DAEMON -->|"initialValidation"| SAFETY
    DAEMON -->|"getRFCSettings"| RFC

    %% Client library
    CLIENT_LIB -->|"D-Bus IPC"| DBUS

    %% Cross-cutting
    UPGRADE -->|"status writes"| DL_STATUS
    UPGRADE -->|"mTLS/Codebig"| CEDM
    UPGRADE -->|"events"| IARM
    FLASH -->|"events"| IARM
    FLASH -->|"status writes"| DL_STATUS

    %% Telemetry
    ONESHOT -.->|"t2 events"| T2
    DAEMON -.->|"t2 events"| T2
    UPGRADE -.->|"t2 events"| T2
    FLASH -.->|"t2 events"| T2
```

---

## 3. Ownership Map — Who Owns What

```mermaid
flowchart LR
    subgraph "One-Shot Owns"
        direction TB
        O1["Process lifecycle<br/>(start → exit)"]
        O2["Global state<br/>(device_info, rfc_list, curl)"]
        O3["Signal handler<br/>(SIGUSR1 → force_exit)"]
        O4["Multi-firmware sequencing<br/>(PCI → PDRI → Peripheral)"]
        O5["PID file lifecycle"]
    end

    subgraph "Daemon Owns"
        direction TB
        D1["GLib main loop"]
        D2["D-Bus connection + bus name"]
        D3["Client process registry"]
        D4["Task lifecycle<br/>(active_tasks, waiting queues)"]
        D5["Concurrency guards<br/>(IsDownload/Flash/CheckUpdate)"]
        D6["XConf response cache"]
        D7["Worker thread spawn/join"]
    end

    subgraph "Upgrade Engine Owns"
        direction TB
        U1["Curl handle lifecycle<br/>(during download)"]
        U2["HTTP session state"]
        U3["Chunk resume state"]
        U4["Throttle interaction"]
    end

    subgraph "Flash Subsystem Owns"
        direction TB
        F1["Flash I/O lifecycle"]
        F2["Post-flash reboot decision"]
        F3["Flash status reporting"]
    end

    subgraph "IARM Interface Owns"
        direction TB
        I1["IARM Bus connection"]
        I2["Event broadcast dispatch"]
        I3["Throttle callback registration"]
    end

    subgraph "Client Library Owns"
        direction TB
        C1["Background thread lifecycle"]
        C2["3 callback registries"]
        C3["Signal subscriptions"]
        C4["Ephemeral proxy creation"]
    end
```

---

## 4. Data Flow — Firmware Update (Daemon Path)

```mermaid
flowchart TD
    subgraph "Client Process"
        APP["Application"]
        LIB["librdkFwupdateMgr.so"]
    end

    subgraph "D-Bus System Bus"
        BUS["org.rdkfwupdater.Interface"]
    end

    subgraph "Daemon Process"
        DISPATCH["process_app_request()<br/>[Main Thread]"]
        VALIDATE["Input Validation<br/>[Main Thread]"]
        
        subgraph "Workers [GTask Pool]"
            W_XCONF["XConf Worker"]
            W_DWNL["Download Worker"]
            W_FLASH["Flash Worker"]
        end
        
        subgraph "Shared Libraries"
            LIB_UPGRADE["librdksw_upgrade.so"]
            LIB_FLASH["librdksw_flash.so"]
            LIB_JSON["librdksw_jsonparse.so"]
        end
        
        SIGNAL["Signal Emission<br/>[Main Thread via g_idle_add]"]
    end

    subgraph "External Services"
        XCONF["XConf Server"]
        CDN["CDN / HTTP"]
        FLASH_HW["Flash Storage"]
    end

    APP -->|"API call"| LIB
    LIB -->|"D-Bus method"| BUS
    BUS -->|"dispatch"| DISPATCH
    DISPATCH -->|"validate"| VALIDATE
    VALIDATE -->|"spawn"| W_XCONF
    VALIDATE -->|"spawn"| W_DWNL
    VALIDATE -->|"spawn"| W_FLASH
    
    W_XCONF -->|"blocking"| LIB_JSON
    LIB_JSON -->|"HTTP POST"| XCONF
    
    W_DWNL -->|"blocking"| LIB_UPGRADE
    LIB_UPGRADE -->|"HTTP GET"| CDN
    
    W_FLASH -->|"blocking"| LIB_FLASH
    LIB_FLASH -->|"flash I/O"| FLASH_HW
    
    W_XCONF -->|"g_task_return"| SIGNAL
    W_DWNL -->|"g_idle_add"| SIGNAL
    W_FLASH -->|"g_idle_add"| SIGNAL
    
    SIGNAL -->|"D-Bus signal"| BUS
    BUS -->|"signal delivery"| LIB
    LIB -->|"callback"| APP
```

---

## 5. Library Dependency Graph

```mermaid
flowchart BT
    subgraph "External Dependencies"
        CURL["libcurl"]
        CJSON["libcjson"]
        GLIB["GLib/GIO"]
        IARMBUS["libIARMBus"]
        RBUS_LIB["librbus"]
        DWNLUTIL["libdwnlutil"]
        FWUTILS_EXT["libfwutils"]
        SECWRAP["libsecure_wrapper"]
        PARSEJSON["libparsejson"]
        PTHREAD["libpthread"]
        RFCAPI["librfcapi"]
        T2_LIB["libtelemetry_msgsender"]
    end

    subgraph "Internal Libraries"
        UPGRADE["librdksw_upgrade.so<br/>rdkv_upgrade.c, chunk.c<br/>codebigUtils.c, mtlsUtils.c"]
        JSONPARSE["librdksw_jsonparse.so<br/>json_process.c"]
        FLASH_LIB["librdksw_flash.so<br/>flash.c<br/>download_status_helper.c"]
        RFCINTF["librdksw_rfcIntf.so<br/>rfcinterface.c"]
        IARMINTF["librdksw_iarmIntf.so<br/>iarmInterface.c"]
        FWUTILS["librdksw_fwutils.so<br/>deviceutils.c, device_api.c"]
        CLIENTLIB["librdkFwupdateMgr.so<br/>process.c, api.c, async.c"]
    end

    subgraph "Binaries"
        ONESHOT_BIN["rdkvfwupgrader"]
        DAEMON_BIN["rdkFwupdateMgr"]
        EXAMPLE["example_plugin"]
    end

    %% Binary → Library
    ONESHOT_BIN --> UPGRADE
    ONESHOT_BIN --> JSONPARSE
    ONESHOT_BIN --> FLASH_LIB
    ONESHOT_BIN --> RFCINTF
    ONESHOT_BIN --> IARMINTF
    ONESHOT_BIN --> FWUTILS

    DAEMON_BIN --> UPGRADE
    DAEMON_BIN --> JSONPARSE
    DAEMON_BIN --> FLASH_LIB
    DAEMON_BIN --> RFCINTF
    DAEMON_BIN --> IARMINTF
    DAEMON_BIN --> FWUTILS
    DAEMON_BIN --> GLIB

    EXAMPLE --> CLIENTLIB
    CLIENTLIB --> GLIB
    CLIENTLIB --> PTHREAD

    %% Library → External
    UPGRADE --> CURL
    UPGRADE --> DWNLUTIL
    UPGRADE --> SECWRAP
    JSONPARSE --> CJSON
    JSONPARSE --> PARSEJSON
    FLASH_LIB --> CURL
    RFCINTF --> RFCAPI
    IARMINTF --> IARMBUS
    FWUTILS --> PARSEJSON
    FWUTILS --> SECWRAP
```

---

## 6. Concurrency Architecture (Daemon Only)

```mermaid
flowchart TD
    subgraph "Main Thread (GLib Main Loop)"
        ML_DISPATCH["D-Bus Method Dispatch<br/>process_app_request()"]
        ML_IDLE["g_idle_add() Callbacks<br/>(signal emission)"]
        ML_DONE["GTask Completion Callbacks<br/>(xconf_done, download_done, flash_done)"]
        
        ML_STATE["Main-Loop-Serialized State<br/>• registered_processes<br/>• active_tasks<br/>• waiting queues<br/>• IsDownloadInProgress<br/>• IsFlashInProgress<br/>• current_download/flash"]
    end

    subgraph "Worker Threads (GTask Pool)"
        WT_XCONF["XConf Worker<br/>rdkfw_xconf_fetch_worker()"]
        WT_DWNL["Download Worker<br/>rdkfw_download_worker()"]
        WT_FLASH["Flash Worker<br/>rdkfw_flash_worker()"]
    end

    subgraph "Monitor Threads (GThread)"
        MT_PROG["Progress Monitor<br/>rdkfw_progress_monitor_thread()"]
    end

    subgraph "Mutex-Protected State"
        MX1["XConfCommStatus<br/>(GMutex)"]
        MX2["DwnlState<br/>(pthread_mutex)"]
        MX3["app_mode<br/>(pthread_mutex)"]
        MX4["g_xconf_data_cache<br/>(G_LOCK)"]
        MX5["stop_flag<br/>(GMutex per download)"]
    end

    ML_DISPATCH -->|"g_task_run_in_thread"| WT_XCONF
    ML_DISPATCH -->|"g_task_run_in_thread"| WT_DWNL
    ML_DISPATCH -->|"g_task_run_in_thread"| WT_FLASH

    WT_XCONF -->|"g_task_return_pointer"| ML_DONE
    WT_DWNL -->|"g_task_return_boolean"| ML_DONE
    WT_FLASH -->|"g_task_return_boolean"| ML_DONE

    WT_DWNL -->|"g_thread_try_new"| MT_PROG
    MT_PROG -->|"g_idle_add"| ML_IDLE
    WT_DWNL -->|"g_idle_add"| ML_IDLE

    WT_XCONF -.->|"read/write"| MX1
    ML_DISPATCH -.->|"read/write"| MX1
    WT_DWNL -.->|"read/write"| MX2
    WT_DWNL -.->|"read/write"| MX5
    MT_PROG -.->|"read"| MX5
    WT_XCONF -.->|"write"| MX4

    ML_DISPATCH -.->|"read/write"| ML_STATE
    ML_DONE -.->|"read/write"| ML_STATE
```
