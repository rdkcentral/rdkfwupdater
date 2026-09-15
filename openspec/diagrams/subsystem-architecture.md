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

---

## 7. One-Shot Execution Lifecycle (`rdkvfwupgrader`)

> **Cross-reference:** [specs/updater-execution/spec.md](../specs/updater-execution/spec.md), [runtime/rdkvfwupgrader-sequence.md](../runtime/rdkvfwupgrader-sequence.md)

```mermaid
stateDiagram-v2
    [*] --> INVOCATION : Maintenance Manager / cron / TR-69

    state INVOCATION {
        [*] --> PARSE_ARGS : main(argc, argv)
        PARSE_ARGS --> LOG_INIT : log_init()
        LOG_INIT --> SIGNAL_SETUP : sigaction(SIGUSR1)
    }

    INVOCATION --> INITIALIZATION

    state INITIALIZATION {
        [*] --> DEVICE_PROPS : getDeviceProperties()
        DEVICE_PROPS --> IMAGE_DETAILS : getImageDetails()
        IMAGE_DETAILS --> RFC_SETTINGS : getRFCSettings()
        RFC_SETTINGS --> DIR_CREATE : createDir(difw_path)
        DIR_CREATE --> IARM_INIT : init_event_handler()
        IARM_INIT --> MAINT_QUERY : getJsonRpc(MaintenanceMode)
    }

    INITIALIZATION --> VALIDATION

    state VALIDATION {
        [*] --> BUILD_CHECK : GetBuildType() + RFC exclusion
        BUILD_CHECK --> PID_CHECK : CurrentRunningInst("/tmp/DIFD.pid")
        PID_CHECK --> REBOOT_CHECK : Check fw_preparing_to_reboot
        REBOOT_CHECK --> UPGRADE_FLAG : isUpgradeInProgress()
    }

    VALIDATION --> XCONF_PHASE : Validation passed
    VALIDATION --> EXIT_SKIP : Conflict detected

    state XCONF_PHASE {
        [*] --> BUILD_PAYLOAD : createJsonString()
        BUILD_PAYLOAD --> GET_URL : GetServURL()
        GET_URL --> HTTP_POST : rdkv_upgrade_request(XCONF_UPGRADE)
        HTTP_POST --> PARSE_RESP : getXconfRespData()
        PARSE_RESP --> VALIDATE_RESP : processJsonResponse()
    }

    XCONF_PHASE --> FIRMWARE_PIPELINE : Update available
    XCONF_PHASE --> EXIT_NO_UPDATE : No update

    state FIRMWARE_PIPELINE {
        [*] --> PCI_DOWNLOAD : rdkv_upgrade_request(PCI)
        PCI_DOWNLOAD --> PCI_FLASH : flashImage(PCI)
        PCI_FLASH --> DELAY_30S : sleep(30)
        DELAY_30S --> PDRI_DOWNLOAD : rdkv_upgrade_request(PDRI)
        PDRI_DOWNLOAD --> PDRI_FLASH : flashImage(PDRI)
        PDRI_FLASH --> PERIPHERAL_LOOP : For each peripheral
        PERIPHERAL_LOOP --> [*]
    }

    FIRMWARE_PIPELINE --> CLEANUP
    EXIT_SKIP --> CLEANUP
    EXIT_NO_UPDATE --> CLEANUP

    state CLEANUP {
        [*] --> IARM_REPORT : eventManager(MaintenanceMGR, status)
        IARM_REPORT --> UNINIT : uninitialize()
        UNINIT --> REMOVE_PID : unlink("/tmp/DIFD.pid")
    }

    CLEANUP --> [*] : exit(ret_code)
```

---

## 8. Data Flow — Firmware Update (One-Shot Path)

> Counterpart to §4 (Daemon Path). Shows the same firmware update lifecycle through the synchronous/procedural one-shot execution model.

```mermaid
flowchart TD
    subgraph "External Trigger"
        MM["Maintenance Manager<br/>(or cron / TR-69)"]
    end

    subgraph "rdkvfwupgrader Process (Single Thread)"
        MAIN["main()<br/>Linear orchestration"]
        INIT["initialize()<br/>Populate globals"]
        VALID["initialValidation()<br/>Safety gates"]
        
        subgraph "XConf Query (blocking)"
            XCONF_BUILD["createJsonString()"]
            XCONF_REQ["rdkv_upgrade_request(XCONF)"]
            XCONF_PARSE_OP["getXconfRespData()"]
        end
        
        subgraph "Download (blocking)"
            DL_PCI["rdkv_upgrade_request(PCI)"]
            DL_PDRI["rdkv_upgrade_request(PDRI)"]
            DL_PERIPH["rdkv_upgrade_request(PERIPHERAL)"]
        end
        
        subgraph "Flash (blocking)"
            FLASH_PCI["flashImage(PCI)"]
            FLASH_PDRI["flashImage(PDRI)"]
        end
        
        CLEANUP_OP["uninitialize() + exit()"]
    end

    subgraph "Shared Libraries (synchronous calls)"
        LIB_UPGRADE["librdksw_upgrade.so"]
        LIB_FLASH["librdksw_flash.so"]
        LIB_JSON["librdksw_jsonparse.so"]
        LIB_FWUTILS["librdksw_fwutils.so"]
        LIB_RFC["librdksw_rfcIntf.so"]
        LIB_IARM["librdksw_iarmIntf.so"]
    end

    subgraph "External Services"
        XCONF_SRV["XConf Server"]
        CDN_SRV["CDN / HTTP"]
        FLASH_HW["Flash Storage"]
        IARM_BUS["IARM Bus"]
    end

    MM -->|"fork+exec"| MAIN
    MAIN --> INIT
    INIT -->|"getDeviceProperties"| LIB_FWUTILS
    INIT -->|"getRFCSettings"| LIB_RFC
    INIT -->|"init_event_handler"| LIB_IARM
    INIT --> VALID
    VALID --> XCONF_BUILD
    
    XCONF_BUILD -->|"device payload"| LIB_JSON
    XCONF_REQ -->|"HTTP POST (blocking)"| LIB_UPGRADE
    LIB_UPGRADE -->|"HTTPS"| XCONF_SRV
    XCONF_PARSE_OP -->|"parse JSON"| LIB_JSON
    
    XCONF_BUILD --> XCONF_REQ --> XCONF_PARSE_OP --> DL_PCI
    
    DL_PCI -->|"HTTP GET (blocking, minutes)"| LIB_UPGRADE
    LIB_UPGRADE -->|"HTTPS"| CDN_SRV
    DL_PCI --> FLASH_PCI
    FLASH_PCI -->|"flash I/O"| LIB_FLASH
    LIB_FLASH -->|"write"| FLASH_HW
    
    FLASH_PCI --> DL_PDRI
    DL_PDRI -->|"HTTP GET"| LIB_UPGRADE
    DL_PDRI --> FLASH_PDRI
    FLASH_PDRI -->|"flash I/O"| LIB_FLASH
    
    FLASH_PDRI --> DL_PERIPH
    DL_PERIPH -->|"HTTP GET(s)"| LIB_UPGRADE
    
    DL_PERIPH --> CLEANUP_OP
    
    LIB_IARM -->|"events"| IARM_BUS
```

---

## 9. Execution Model Comparison — Side-by-Side

> Structural comparison showing how each execution model uses the same shared infrastructure differently.

```mermaid
flowchart LR
    subgraph "rdkvfwupgrader<br/>(One-Shot)"
        direction TB
        OS_TRIGGER["Maintenance Manager<br/>fork+exec"]
        OS_MAIN["main() — single thread"]
        OS_INIT["initialize()"]
        OS_VALID["initialValidation()"]
        OS_XCONF["MakeXconfComms()<br/>(direct, no cache)"]
        OS_DL["rdkv_upgrade_request()<br/>(blocks main thread)"]
        OS_FLASH["flashImage()<br/>(inline after download)"]
        OS_EXIT["exit(ret_code)"]
        
        OS_TRIGGER --> OS_MAIN --> OS_INIT --> OS_VALID
        OS_VALID --> OS_XCONF --> OS_DL --> OS_FLASH --> OS_EXIT
    end

    subgraph "Shared Platform"
        direction TB
        S_UPGRADE["librdksw_upgrade.so"]
        S_FLASH["librdksw_flash.so"]
        S_JSON["librdksw_jsonparse.so"]
        S_FWUTILS["librdksw_fwutils.so"]
        S_RFC["librdksw_rfcIntf.so"]
        S_IARM["librdksw_iarmIntf.so"]
    end

    subgraph "rdkFwupdateMgr<br/>(Daemon)"
        direction TB
        D_SYSTEMD["systemd<br/>ExecStart"]
        D_MAIN["main() — state machine"]
        D_INIT["initialize()"]
        D_VALID["initialValidation()"]
        D_DBUS["setup_dbus_server()"]
        D_LOOP["g_main_loop_run()<br/>(indefinite)"]
        D_WORKERS["GTask Workers<br/>(XConf, Download, Flash)"]
        D_SIGNALS["D-Bus Signal Emission"]
        
        D_SYSTEMD --> D_MAIN --> D_DBUS --> D_INIT --> D_VALID
        D_VALID --> D_LOOP --> D_WORKERS --> D_SIGNALS
        D_SIGNALS --> D_LOOP
    end

    OS_XCONF -->|"sync call"| S_JSON
    OS_DL -->|"sync call"| S_UPGRADE
    OS_FLASH -->|"sync call"| S_FLASH
    OS_INIT -->|"sync call"| S_FWUTILS
    OS_INIT -->|"sync call"| S_RFC
    OS_INIT -->|"sync call"| S_IARM

    D_WORKERS -->|"in GTask thread"| S_UPGRADE
    D_WORKERS -->|"in GTask thread"| S_FLASH
    D_WORKERS -->|"in GTask thread"| S_JSON
    D_INIT -->|"sync call"| S_FWUTILS
    D_INIT -->|"sync call"| S_RFC
    D_INIT -->|"sync call"| S_IARM
```

---

## 10. One-Shot Invocation & Recovery Model

> Shows how `rdkvfwupgrader` is triggered, how it interacts with the Maintenance Manager, and how recovery works across invocations.

```mermaid
sequenceDiagram
    participant MM as Maintenance Manager
    participant OS as rdkvfwupgrader
    participant FS as Filesystem (/tmp, /opt)
    participant IARM as IARM Bus
    participant XCONF as XConf Server
    participant CDN as CDN

    Note over MM,CDN: === Normal Invocation ===
    MM->>OS: fork+exec(rdkvfwupgrader, 3, 2)
    activate OS
    OS->>FS: Check /tmp/DIFD.pid (no conflict)
    OS->>FS: Write PID to /tmp/DIFD.pid
    OS->>IARM: eventManager(FW_STATE_REQUESTING)
    OS->>XCONF: HTTP POST (XConf query)
    XCONF-->>OS: JSON response (update available)
    OS->>IARM: eventManager(FW_STATE_DOWNLOADING)
    OS->>FS: Set upgrade flag
    OS->>CDN: HTTP GET (firmware binary, blocks minutes)
    CDN-->>OS: Firmware data → /opt/fw/image.bin
    OS->>IARM: eventManager(FW_STATE_FLASHING)
    OS->>FS: Create /tmp/fw_preparing_to_reboot
    OS->>FS: flashImage() → Flash storage
    OS->>FS: Remove upgrade flag
    OS->>IARM: eventManager(MaintenanceMGR, COMPLETE)
    OS->>FS: Remove /tmp/DIFD.pid
    OS-->>MM: exit(0)
    deactivate OS

    Note over MM,CDN: === Crash Recovery (next invocation) ===
    MM->>OS: fork+exec(rdkvfwupgrader, 3, 2)
    activate OS
    OS->>FS: Check /tmp/fw_preparing_to_reboot → EXISTS
    OS->>IARM: eventManager(MaintenanceMGR, COMPLETE)
    OS->>FS: Remove /tmp/fw_preparing_to_reboot
    OS->>FS: Remove /tmp/DIFD.pid
    OS-->>MM: exit(0) [DWNL_COMPLETED]
    deactivate OS

    Note over MM,CDN: === Download Interrupted (SIGUSR1) ===
    MM->>OS: fork+exec(rdkvfwupgrader, 3, 2)
    activate OS
    OS->>CDN: HTTP GET (downloading...)
    MM->>OS: SIGUSR1 (abort request)
    OS->>OS: force_exit=1 → curl abort
    OS->>FS: Partial file remains (/opt/fw/image.bin, 50MB)
    OS->>FS: Remove /tmp/DIFD.pid
    OS-->>MM: exit(curl_error)
    deactivate OS

    Note over MM,CDN: === Resume on Re-invocation ===
    MM->>OS: fork+exec(rdkvfwupgrader, 3, 2)
    activate OS
    OS->>XCONF: HTTP POST (XConf query, confirms same FW)
    OS->>FS: Check /opt/fw/image.bin → 50MB exists
    OS->>CDN: HTTP GET (Range: bytes=52428800-)
    CDN-->>OS: 206 Partial Content (remaining data)
    OS->>FS: Append to existing file → complete
    OS->>FS: flashImage()
    OS-->>MM: exit(0)
    deactivate OS
```

---

## 11. One-Shot Shared-Library Interaction Model

> Shows the synchronous call chain between `rdkvfwupgrader` and each shared library, with timing characteristics.

```mermaid
flowchart TD
    subgraph "rdkvfwupgrader Process"
        MAIN["main() — single thread<br/>All calls are BLOCKING"]
    end

    subgraph "Initialization Phase (~50ms)"
        LIB_FW["librdksw_fwutils.so<br/>getDeviceProperties()<br/>getImageDetails()"]
        LIB_RFC_I["librdksw_rfcIntf.so<br/>getRFCSettings()"]
        LIB_IARM_I["librdksw_iarmIntf.so<br/>init_event_handler()"]
    end

    subgraph "XConf Phase (5-60s)"
        LIB_JSON_X["librdksw_jsonparse.so<br/>createJsonString()"]
        LIB_UPG_X["librdksw_upgrade.so<br/>rdkv_upgrade_request(XCONF)"]
        LIB_JSON_P["librdksw_jsonparse.so<br/>getXconfRespData()<br/>processJsonResponse()"]
    end

    subgraph "Download Phase (seconds to minutes)"
        LIB_UPG_D["librdksw_upgrade.so<br/>rdkv_upgrade_request(PCI/PDRI/PERIPH)"]
    end

    subgraph "Flash Phase (seconds)"
        LIB_FLASH_F["librdksw_flash.so<br/>flashImage()"]
    end

    subgraph "Cleanup Phase (~10ms)"
        LIB_IARM_T["librdksw_iarmIntf.so<br/>term_event_handler()"]
    end

    MAIN -->|"1. sync"| LIB_FW
    MAIN -->|"2. sync"| LIB_RFC_I
    MAIN -->|"3. sync"| LIB_IARM_I
    MAIN -->|"4. sync"| LIB_JSON_X
    MAIN -->|"5. sync (network)"| LIB_UPG_X
    MAIN -->|"6. sync"| LIB_JSON_P
    MAIN -->|"7. sync (network, long)"| LIB_UPG_D
    MAIN -->|"8. sync (I/O)"| LIB_FLASH_F
    MAIN -->|"9. sync"| LIB_IARM_T
```

### Call Duration Characteristics

| Phase | Library | Duration | Blocking? | Interruptible? |
|-------|---------|----------|-----------|----------------|
| Init | librdksw_fwutils | ~1-5ms | File I/O | No |
| Init | librdksw_rfcIntf | ~5ms | File/IPC | No |
| Init | librdksw_iarmIntf | ~10ms | IPC handshake | No |
| XConf | librdksw_upgrade | **5-60 seconds** | Network | Via `force_exit` |
| Download | librdksw_upgrade | **Seconds to minutes** | Network | Via `force_exit` / throttle |
| Flash | librdksw_flash | **Seconds** | Storage I/O | No |
| Cleanup | librdksw_iarmIntf | ~1ms | IPC | No |

---

## 12. One-Shot vs Daemon — Subsystem Usage Overlay

> Shows which subsystems are active in each execution model and how they differ in invocation pattern.

```mermaid
flowchart TD
    subgraph "Shared Subsystems (Both Models)"
        style S fill:#e8f5e9
        S1["Firmware Upgrade Engine<br/>librdksw_upgrade.so"]
        S2["Flash Subsystem<br/>librdksw_flash.so"]
        S3["XConf Parsing<br/>librdksw_jsonparse.so"]
        S4["Device Identity<br/>librdksw_fwutils.so"]
        S5["RFC Configuration<br/>librdksw_rfcIntf.so"]
        S6["IARM Events<br/>librdksw_iarmIntf.so"]
        S7["Download Status<br/>download_status_helper.c"]
        S8["Process Safety<br/>device_status_helper.c"]
        S9["CEDM / Certs<br/>codebigUtils + mtlsUtils"]
        S10["Telemetry (T2)"]
    end

    subgraph "One-Shot Only"
        style OS fill:#fff3e0
        OS1["Linear Pipeline<br/>(PCI→PDRI→Peripheral)"]
        OS2["Multi-firmware Sequencing<br/>(30s inter-flash delay)"]
        OS3["Direct XConf Query<br/>(no cache, no dedup)"]
        OS4["Signal Abort (SIGUSR1)"]
        OS5["Maintenance Manager<br/>Status Reporting"]
        OS6["Exit Code Propagation"]
    end

    subgraph "Daemon Only"
        style D fill:#e3f2fd
        D1["D-Bus Service Runtime"]
        D2["Client Process Registry"]
        D3["GTask Worker Pool"]
        D4["Concurrency Guards"]
        D5["Piggyback Queues"]
        D6["XConf Response Cache"]
        D7["D-Bus Signal Broadcasting"]
        D8["Progress Monitor Thread"]
        D9["Client Library SDK"]
    end

    %% One-shot → Shared (synchronous)
    OS1 -->|"sync, blocks main"| S1
    OS1 -->|"sync, blocks main"| S2
    OS3 -->|"sync, blocks main"| S3
    OS5 -->|"fire-and-forget"| S6
    OS1 -->|"writes status"| S7
    OS4 -->|"checks force_exit"| S1

    %% Daemon → Shared (via workers)
    D3 -->|"async, blocks worker"| S1
    D3 -->|"async, blocks worker"| S2
    D3 -->|"async, blocks worker"| S3
    D1 -->|"init/term"| S6
    D3 -->|"writes status"| S7
    D4 -->|"guards access"| S1
    D4 -->|"guards access"| S2

    %% Cross-cutting
    S8 -.->|"PID check"| OS1
    S8 -.->|"PID check"| D1
```

---

## 13. One-Shot Threading & Abort Model

> Counterpart to §6 (Daemon Concurrency Architecture). Documents the simpler concurrency model of the one-shot binary.

```mermaid
flowchart TD
    subgraph "rdkvfwupgrader — Single Thread + Signals"
        MAIN_THREAD["Main Thread<br/>(only thread owned by process)"]
        
        subgraph "Main Thread Execution"
            M1["initialize()"]
            M2["initialValidation()"]
            M3["MakeXconfComms()"]
            M4["rdkv_upgrade_request()"]
            M5["flashImage()"]
            M6["uninitialize()"]
            M1 --> M2 --> M3 --> M4 --> M5 --> M6
        end
    end

    subgraph "External Threads (not owned by process)"
        IARM_THREAD["IARM Internal Thread<br/>(delivers DwnlStopEventHandler)"]
        SIGNAL_CTX["Signal Context<br/>(SIGUSR1 delivery)"]
    end

    subgraph "Shared Mutable State"
        STATE_FORCE["force_exit<br/>(written by signal/IARM, read by curl)"]
        STATE_DWNL["DwnlState<br/>(pthread_mutex protected)"]
        STATE_MODE["app_mode<br/>(pthread_mutex protected)"]
        STATE_CURL["curl handle<br/>(main thread owns, signal may cleanup)"]
    end

    SIGNAL_CTX -->|"sets force_exit=1<br/>curl_easy_cleanup()"| STATE_FORCE
    IARM_THREAD -->|"doInteruptDwnl(speed)<br/>sets DwnlState"| STATE_DWNL
    IARM_THREAD -->|"if speed==0: force_exit=1"| STATE_FORCE
    
    M4 -->|"curl progress callback<br/>checks force_exit"| STATE_FORCE
    M4 -->|"read/write"| STATE_DWNL
    MAIN_THREAD -->|"owns lifecycle"| STATE_CURL
```

### Contrast: Concurrency Primitives

| Aspect | One-Shot | Daemon |
|--------|----------|--------|
| Threads owned | 1 (main only) | 1 main + N workers + progress monitors |
| Event loop | None | GLib main loop |
| Async dispatch | None | GTask + g_idle_add |
| Concurrency guards | PID file only | PID file + IsDownload + IsFlash + XConfComm |
| Mutex usage | 2 (DwnlState, app_mode) — mostly uncontested | 5+ (GMutex, G_LOCK, pthread_mutex) |
| Abort mechanism | SIGUSR1 → force_exit → curl abort | stop_flag per download + g_idle_add |
| Signal emission | N/A | D-Bus broadcast via main loop |
| State protection | Single-thread guarantee + signal-safe flag writes | Main-loop serialization + mutexes |

---

## 14. Maintenance Manager Integration (One-Shot)

> Shows how `rdkvfwupgrader` integrates with the RDK Maintenance Manager ecosystem.

```mermaid
flowchart TD
    subgraph "RDK Maintenance Framework"
        MM["Maintenance Manager"]
        CRON["Scheduled Trigger<br/>(cron / timer)"]
        TR69["TR-69 / SNMP<br/>Remote Trigger"]
    end

    subgraph "rdkvfwupgrader Lifecycle"
        INVOKE["Process Start<br/>(fork+exec)"]
        MODE_Q["Query Maintenance Mode<br/>(Thunder JSON-RPC)"]
        EXEC["Firmware Pipeline<br/>(XConf → Download → Flash)"]
        REPORT["Report Completion<br/>(IARM eventManager)"]
        EXIT_P["Process Exit<br/>(exit code)"]
    end

    subgraph "IARM Event Reporting"
        FW_REQUESTING["FW_STATE_REQUESTING"]
        FW_DOWNLOADING["FW_STATE_DOWNLOADING"]
        FW_FLASHING["FW_STATE_FLASHING"]
        MAINT_COMPLETE["MAINT_FWDOWNLOAD_COMPLETE"]
        MAINT_ERROR["MAINT_FWDOWNLOAD_ERROR"]
    end

    subgraph "Interaction Points"
        THROTTLE["DwnlStopEventHandler<br/>(IARM callback)"]
        SIGUSR["SIGUSR1<br/>(abort signal)"]
    end

    MM -->|"trigger_type=2"| INVOKE
    CRON -->|"trigger_type=2"| INVOKE
    TR69 -->|"trigger_type=3"| INVOKE

    INVOKE --> MODE_Q
    MODE_Q -->|"Thunder localhost:9998"| MM
    MODE_Q --> EXEC

    EXEC -->|"state events"| FW_REQUESTING
    EXEC -->|"state events"| FW_DOWNLOADING
    EXEC -->|"state events"| FW_FLASHING

    EXEC --> REPORT
    REPORT -->|"success"| MAINT_COMPLETE
    REPORT -->|"failure"| MAINT_ERROR

    REPORT --> EXIT_P
    EXIT_P -->|"exit(0) or exit(error)"| MM

    MM -->|"throttle/stop"| THROTTLE
    MM -->|"abort"| SIGUSR
    THROTTLE -->|"modifies download speed"| EXEC
    SIGUSR -->|"force_exit=1"| EXEC
```

---

## 15. Firmware Orchestration — Download/Flash Sequencing (One-Shot)

> Detailed sequencing of multi-firmware operations showing blocking durations and inter-operation coordination.

```mermaid
gantt
    title rdkvfwupgrader — Firmware Operation Timeline
    dateFormat X
    axisFormat %s

    section Initialization
    initialize()           :init, 0, 50
    initialValidation()    :valid, 50, 100

    section XConf Query
    createJsonString()     :json, 100, 105
    HTTP POST to XConf     :xconf, 105, 165
    Parse response         :parse, 165, 170

    section PCI Firmware
    Download PCI (HTTP GET):dl_pci, 170, 770
    Flash PCI              :flash_pci, 770, 780

    section Inter-flash Delay
    sleep(30)              :delay, 780, 810

    section PDRI Firmware
    Download PDRI          :dl_pdri, 810, 910
    Flash PDRI             :flash_pdri, 910, 915

    section Peripheral
    Download Peripheral(s) :dl_periph, 915, 945

    section Cleanup
    uninitialize() + exit  :cleanup, 945, 950
```

```mermaid
sequenceDiagram
    participant MAIN as main() [single thread]
    participant UPGRADE as librdksw_upgrade
    participant FLASH as librdksw_flash
    participant IARM as IARM Bus
    participant FS as Filesystem

    Note over MAIN,FS: === PCI Firmware ===
    MAIN->>IARM: eventManager(FW_STATE_DOWNLOADING)
    MAIN->>FS: eraseFolderExceParamFile() [clean old images]
    MAIN->>UPGRADE: rdkv_upgrade_request(PCI) [BLOCKS]
    Note right of UPGRADE: May take minutes.<br/>Throttle via IARM callback.<br/>Interruptible via force_exit.
    UPGRADE-->>MAIN: 0 (success)
    
    MAIN->>IARM: eventManager(FW_STATE_FLASHING)
    MAIN->>FS: create /tmp/fw_preparing_to_reboot
    MAIN->>FLASH: flashImage(PCI) [BLOCKS]
    FLASH-->>MAIN: 0 (success)
    
    Note over MAIN,FS: === PDRI Firmware (30s delay) ===
    MAIN->>MAIN: sleep(30)
    MAIN->>UPGRADE: rdkv_upgrade_request(PDRI) [BLOCKS]
    UPGRADE-->>MAIN: 0 (success)
    MAIN->>FLASH: flashImage(PDRI) [BLOCKS]
    FLASH-->>MAIN: 0 (success)
    
    Note over MAIN,FS: === Peripheral Firmware ===
    loop For each peripheral in XConf response
        MAIN->>UPGRADE: rdkv_upgrade_request(PERIPHERAL) [BLOCKS]
        UPGRADE-->>MAIN: 0 (success)
    end
    MAIN->>IARM: eventManager(PeripheralUpgradeEvent, paths)
    
    Note over MAIN,FS: === Cleanup ===
    MAIN->>IARM: eventManager(MaintenanceMGR, COMPLETE)
    MAIN->>FS: Remove /tmp/fw_preparing_to_reboot
    MAIN->>FS: Remove /tmp/DIFD.pid
    MAIN->>MAIN: exit(0)
```
