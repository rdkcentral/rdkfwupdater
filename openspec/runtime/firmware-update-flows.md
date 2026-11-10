# Firmware Update Flows — End-to-End Sequences

> **Evidence Level:** Facts verified from source code across all subsystems

---

## 1. Complete Firmware Update Workflow (Daemon Mode)

```mermaid
sequenceDiagram
    participant App as Client Application
    participant Lib as librdkFwupdateMgr.so
    participant BG as Library BG Thread
    participant Bus as System D-Bus
    participant Daemon as rdkFwupdateMgr
    participant Worker as Worker Thread
    participant XConf as XConf Server
    participant CDN as CDN/HTTP Server
    participant Flash as Device Flash

    Note over App,Flash: Phase 1: Registration
    App->>Lib: registerProcess("MyApp", "1.0")
    Lib->>Lib: internal_system_init()
    Note over Lib: Starts BG thread, subscribes to signals
    Lib->>Bus: RegisterProcess("MyApp", "1.0") [sync]
    Bus->>Daemon: add_process_to_tracking()
    Daemon-->>Bus: handler_id = 1
    Bus-->>Lib: handler_id = 1
    Lib-->>App: handle = "1"

    Note over App,Flash: Phase 2: Check for Updates
    App->>Lib: checkForUpdate("1", callback)
    Lib->>Lib: Register callback in g_registry
    Lib->>Bus: CheckForUpdate("1") [fire-and-forget]
    Lib-->>App: CHECK_FOR_UPDATE_SUCCESS
    
    Bus->>Daemon: process_app_request("CheckForUpdate")
    Daemon->>Daemon: trySetXConfCommStatus() → claimed
    Daemon-->>Bus: Immediate reply (status=3, CHECK_ERROR)
    Daemon->>Worker: GTask → rdkfw_xconf_fetch_worker()
    
    Worker->>XConf: HTTP POST (device JSON)
    XConf-->>Worker: JSON response (fw version, URL, etc.)
    Worker->>Worker: save_xconf_to_cache()
    Worker->>Worker: Validate firmware vs device model
    Worker->>Daemon: g_idle_add(emit CheckForUpdateComplete)
    
    Daemon->>Bus: CheckForUpdateComplete signal
    Bus->>BG: Signal received
    BG->>BG: on_check_complete_signal()
    BG->>BG: dispatch_all_pending()
    BG->>App: callback(FwInfoData{status=FIRMWARE_AVAILABLE})
    
    Note over App,Flash: Phase 3: Download Firmware
    App->>Lib: downloadFirmware("1", {name, url, type}, dl_cb)
    Lib->>Lib: Register dl_cb in g_dwnl_registry
    Lib->>Bus: DownloadFirmware("1","fw.bin","url","PCI") [fire-and-forget]
    Lib-->>App: RDKFW_DWNL_SUCCESS
    
    Bus->>Daemon: process_app_request("DownloadFirmware")
    Daemon->>Worker: GTask → rdkfw_download_worker()
    
    loop Progress Updates (0% → 100%)
        Worker->>CDN: HTTP GET (firmware binary)
        Worker->>Daemon: g_idle_add(emit DownloadProgress)
        Daemon->>Bus: DownloadProgress signal (progress%)
        Bus->>BG: Signal received
        BG->>App: dl_cb(progress, DWNL_IN_PROGRESS)
    end
    
    Worker->>Daemon: g_idle_add(emit DownloadProgress 100%)
    Daemon->>Bus: DownloadProgress signal (100%, COMPLETED)
    Bus->>BG: Signal received
    BG->>App: dl_cb(100, DWNL_COMPLETED)

    Note over App,Flash: Phase 4: Flash Firmware
    App->>Lib: updateFirmware("1", {name, type, reboot}, upd_cb)
    Lib->>Lib: Register upd_cb in g_update_registry
    Lib->>Bus: UpdateFirmware("1","fw.bin","/opt/CDL","PCI","true") [fire-and-forget]
    Lib-->>App: RDKFW_UPDATE_SUCCESS
    
    Bus->>Daemon: process_app_request("UpdateFirmware")
    Daemon->>Worker: GThread → rdkfw_flash_worker_thread()
    
    loop Flash Progress (0% → 100%)
        Worker->>Flash: flashImage()
        Worker->>Daemon: g_idle_add(emit UpdateProgress)
        Daemon->>Bus: UpdateProgress signal (progress%)
        Bus->>BG: Signal received
        BG->>App: upd_cb(progress, UPDATE_IN_PROGRESS)
    end
    
    Worker->>Daemon: g_idle_add(emit UpdateProgress 100%)
    Daemon->>Bus: UpdateProgress signal (100%, COMPLETED)
    Bus->>BG: Signal received
    BG->>App: upd_cb(100, UPDATE_COMPLETED)
    
    Note over App,Flash: Phase 5: Cleanup
    App->>Lib: unregisterProcess("1")
    Lib->>Bus: UnregisterProcess(1) [sync]
    Bus->>Daemon: remove_process_from_tracking()
    Daemon-->>Bus: success = true
    Lib->>Lib: Free handle memory
```

---

## 2. One-Shot Binary Flow (rdkvfwupgrader)

```mermaid
sequenceDiagram
    participant MM as Maintenance Manager
    participant Proc as rdkvfwupgrader
    participant IARM as IARM Bus
    participant XConf as XConf Server
    participant CDN as CDN Server
    participant Flash as Device Flash

    MM->>Proc: Launch via systemd/IARM
    
    Note over Proc: Initialization
    Proc->>Proc: log_init()
    Proc->>Proc: initialize()
    Proc->>Proc: getDeviceProperties()
    Proc->>Proc: getImageDetails()
    Proc->>Proc: getRFCSettings()
    Proc->>IARM: init_event_handler()
    
    Note over Proc: Validation
    Proc->>Proc: initialValidation()
    Proc->>Proc: Check /tmp/DIFD.pid
    Proc->>Proc: Write current PID
    
    Note over Proc: XConf Query
    Proc->>IARM: eventManager(FW_STATE_REQUESTING)
    Proc->>XConf: MakeXconfComms() → HTTP POST
    XConf-->>Proc: JSON response
    Proc->>Proc: processJsonResponse()
    
    Note over Proc: Firmware Download
    Proc->>Proc: checkTriggerUpgrade()
    Proc->>CDN: rdkv_upgrade_request(PCI)
    CDN-->>Proc: Firmware binary
    
    alt PDRI Available
        Proc->>CDN: rdkv_upgrade_request(PDRI)
        CDN-->>Proc: PDRI image
    end
    
    alt Peripheral FW Available
        Proc->>CDN: peripheral_firmware_dndl()
        CDN-->>Proc: Peripheral packages
    end
    
    Note over Proc: Flash & Cleanup
    Proc->>Flash: flashImage()
    Proc->>IARM: eventManager(FW_STATE_COMPLETED)
    Proc->>IARM: eventManager(MaintenanceMGR, COMPLETE)
    Proc->>Proc: uninitialize()
    Proc->>Proc: exit(0)
```

---

## 3. Piggybacking Flow (Multiple Clients)

```mermaid
sequenceDiagram
    participant A as Client A
    participant B as Client B
    participant Daemon as rdkFwupdateMgr
    participant Worker as Worker Thread
    participant XConf as XConf Server

    Note over A,XConf: Client A initiates check
    A->>Daemon: CheckForUpdate("handler_A")
    Daemon->>Daemon: trySetXConfCommStatus() → TRUE (claimed)
    Daemon->>Worker: Spawn XConf fetch
    
    Note over A,XConf: Client B arrives while fetch in progress
    B->>Daemon: CheckForUpdate("handler_B")
    Daemon->>Daemon: trySetXConfCommStatus() → FALSE (busy)
    Daemon->>Daemon: Add "handler_B" to waiting_checkUpdate_ids
    Daemon-->>B: Immediate reply (status=3, IN_PROGRESS)
    
    Note over A,XConf: XConf fetch completes
    Worker->>XConf: HTTP POST
    XConf-->>Worker: Response
    Worker->>Daemon: Fetch complete
    
    Note over A,XConf: Both clients get notified
    Daemon->>A: CheckForUpdateComplete signal (handler_A)
    Daemon->>Daemon: Process waiting queue
    Daemon->>B: CheckForUpdateComplete signal (handler_B)
    Daemon->>Daemon: setXConfCommStatus(FALSE)
```

---

## 4. Download Throttling Flow

```mermaid
sequenceDiagram
    participant MM as Maintenance Manager
    participant IARM as IARM Bus
    participant Proc as rdkvfwupgrader/daemon
    participant DL as Download Library (libcurl)

    Note over MM,DL: Normal foreground download
    Proc->>DL: rdkv_upgrade_request() [full speed]
    
    MM->>IARM: Mode change → BACKGROUND
    IARM->>Proc: interuptDwnl(app_mode=0)
    Proc->>Proc: setAppMode(0)
    
    alt Throttle speed > 0
        Proc->>DL: doInteruptDwnl(curl, speed)
        Note over DL: Pause → Unpause at throttled speed
    else Throttle speed = 0
        Proc->>Proc: force_exit = 1
        Proc->>DL: setForceStop(1)
        Proc->>IARM: eventManager(FW_STATE_FAILED)
        Note over DL: Download aborted
    end
    
    MM->>IARM: Mode change → FOREGROUND
    IARM->>Proc: interuptDwnl(app_mode=1)
    Proc->>Proc: setAppMode(1)
    Proc->>DL: doInteruptDwnl(curl, 0)
    Note over DL: Resume at full speed
```
