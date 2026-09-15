# librdkFwupdateMgr — Visual Engineering Documentation

> **Document Version**: 1.0  
> **Date**: April 28, 2026  
> **Classification**: Internal Engineering — Pull Request Review  
> **Component**: `librdkFwupdateMgr` (shared library)  
> **Companion**: See [DESIGN_DOCUMENT.md](DESIGN_DOCUMENT.md) for full prose reference

---

## Color Legend

| Color | Meaning | Used For |
|-------|---------|----------|
| 🔵 Blue | Public API | Exported functions, client-visible interfaces |
| 🟢 Green | Success | Successful returns, normal completion paths |
| 🔴 Red | Error | Failures, error returns, exception paths |
| 🟡 Yellow | Validation | Input checks, parameter validation gates |
| 🟣 Purple | Logging | Log emission points, log module boundaries |
| ⬜ Gray | Internal Helpers | Private functions, internal state management |

---

## Table of Contents

1. [High-Level Architecture](#1-high-level-architecture)
2. [API Flowcharts](#2-api-flowcharts)
   - 2.1 [registerProcess()](#21-registerprocess)
   - 2.2 [checkForUpdate()](#22-checkforupdate)
   - 2.3 [downloadFirmware()](#23-downloadfirmware)
   - 2.4 [updateFirmware()](#24-updatefirmware)
   - 2.5 [unregisterProcess()](#25-unregisterprocess)
3. [Sequence Diagrams](#3-sequence-diagrams)
   - 3.1 [Complete Firmware Update Flow](#31-complete-firmware-update-flow)
   - 3.2 [Daemon Unavailable + Retry](#32-daemon-unavailable--retry)
   - 3.3 [Callback Registration & Delivery](#33-callback-registration--delivery)
   - 3.4 [Timeout Recovery](#34-timeout-recovery)
4. [Thread Safety Diagram](#4-thread-safety-diagram)
5. [Memory Ownership Diagram](#5-memory-ownership-diagram)
6. [Logging Pipeline Diagram](#6-logging-pipeline-diagram)

---

## 1. High-Level Architecture

```mermaid
graph TB
    subgraph CLIENT["🔵 Client Applications"]
        direction LR
        P1["example_plugin"]
        P2["TR-069 Agent"]
        P3["WebUI Service"]
    end

    subgraph LIB["🔵 librdkFwupdateMgr.so"]
        direction TB
        API["Public API Layer<br/>registerProcess · checkForUpdate<br/>downloadFirmware · updateFirmware<br/>unregisterProcess"]
        VAL["🟡 Validation Gate<br/>NULL checks · length limits<br/>handle verification"]
        REG["⬜ Callback Registries<br/>g_registry (check)<br/>g_dwnl_registry (download)<br/>g_update_registry (update)"]
        BGT["⬜ Background Thread<br/>GMainLoop · signal handlers<br/>two-phase dispatch"]
        LOG_LIB["🟣 LOG.RDK.FWUPMGR"]

        API --> VAL
        VAL --> REG
        REG --> BGT
        API -.-> LOG_LIB
        VAL -.-> LOG_LIB
        REG -.-> LOG_LIB
        BGT -.-> LOG_LIB
    end

    subgraph IPC["⬜ IPC Layer — D-Bus System Bus"]
        direction LR
        METHODS["Method Calls<br/>RegisterProcess · UnregisterProcess<br/>CheckForUpdate · DownloadFirmware<br/>UpdateFirmware"]
        SIGNALS["Signals (Broadcast)<br/>CheckForUpdateComplete<br/>DownloadProgress<br/>UpdateProgress"]
    end

    subgraph DAEMON["⬜ rdkFwupdateMgr Daemon"]
        direction TB
        DBUS_SRV["D-Bus Server<br/>Method handler · Signal emitter"]
        PROC_MGR["Process Manager<br/>handler_id tracking<br/>registration table"]
        LOG_DMN["🟣 LOG.RDK.FWUPG"]

        DBUS_SRV --> PROC_MGR
        DBUS_SRV -.-> LOG_DMN
        PROC_MGR -.-> LOG_DMN
    end

    subgraph FW["⬜ Firmware Subsystem"]
        direction LR
        XCONF["XConf Server<br/>(cloud)"]
        CDN["CDN<br/>(firmware images)"]
        HAL["Device HAL<br/>(flash storage)"]
        REBOOT["Reboot Manager"]
    end

    P1 --> API
    P2 --> API
    P3 --> API

    API -- "ephemeral connection<br/>per API call" --> METHODS
    BGT -- "persistent connection<br/>signal subscription" --> SIGNALS

    METHODS --> DBUS_SRV
    DBUS_SRV --> SIGNALS

    PROC_MGR --> XCONF
    PROC_MGR --> CDN
    PROC_MGR --> HAL
    PROC_MGR --> REBOOT

    style CLIENT fill:#dbeafe,stroke:#2563eb,stroke-width:2px,color:#1e3a5f
    style LIB fill:#dbeafe,stroke:#2563eb,stroke-width:2px,color:#1e3a5f
    style IPC fill:#f3f4f6,stroke:#6b7280,stroke-width:2px,color:#374151
    style DAEMON fill:#f3f4f6,stroke:#6b7280,stroke-width:2px,color:#374151
    style FW fill:#f3f4f6,stroke:#6b7280,stroke-width:2px,color:#374151

    style API fill:#3b82f6,stroke:#1d4ed8,color:#fff
    style VAL fill:#eab308,stroke:#a16207,color:#fff
    style REG fill:#9ca3af,stroke:#4b5563,color:#fff
    style BGT fill:#9ca3af,stroke:#4b5563,color:#fff
    style LOG_LIB fill:#a855f7,stroke:#7e22ce,color:#fff
    style LOG_DMN fill:#a855f7,stroke:#7e22ce,color:#fff
    style DBUS_SRV fill:#9ca3af,stroke:#4b5563,color:#fff
    style PROC_MGR fill:#9ca3af,stroke:#4b5563,color:#fff
```

### Layer Responsibilities Summary

```mermaid
graph LR
    subgraph LAYERS["System Layers"]
        direction TB
        L1["🔵 Client Layer<br/>Calls 5 APIs · implements 3 callbacks<br/>owns condvar sync · owns log lifecycle"]
        L2["🔵 Library Layer<br/>Validates · Transports · Dispatches<br/>owns handle · owns BG thread"]
        L3["⬜ IPC Layer<br/>D-Bus system bus<br/>methods ↓ · signals ↑"]
        L4["⬜ Daemon Layer<br/>Orchestrates firmware ops<br/>owns process table · emits signals"]
        L5["⬜ Firmware Subsystem<br/>XConf · CDN · HAL · Reboot"]

        L1 --> L2 --> L3 --> L4 --> L5
    end

    style L1 fill:#3b82f6,stroke:#1d4ed8,color:#fff
    style L2 fill:#3b82f6,stroke:#1d4ed8,color:#fff
    style L3 fill:#d1d5db,stroke:#6b7280,color:#374151
    style L4 fill:#d1d5db,stroke:#6b7280,color:#374151
    style L5 fill:#d1d5db,stroke:#6b7280,color:#374151
```

---

## 2. API Flowcharts

### 2.1 `registerProcess()`

```mermaid
flowchart TD
    START(["🔵 registerProcess(processName, libVersion)"])
    START --> LOG_ENTRY["🟣 FWUPMGR_INFO: Entry with processName"]

    LOG_ENTRY --> V1{"🟡 processName == NULL?"}
    V1 -- Yes --> ERR1["🔴 FWUPMGR_ERROR: NULL processName"]
    ERR1 --> RET_NULL1(["🔴 return NULL"])
    V1 -- No --> V2{"🟡 processName empty?"}

    V2 -- Yes --> ERR2["🔴 FWUPMGR_ERROR: empty processName"]
    ERR2 --> RET_NULL2(["🔴 return NULL"])
    V2 -- No --> V3{"🟡 strlen > 256?"}

    V3 -- Yes --> ERR3["🔴 FWUPMGR_ERROR: name too long"]
    ERR3 --> RET_NULL3(["🔴 return NULL"])
    V3 -- No --> V4{"🟡 libVersion == NULL?"}

    V4 -- Yes --> ERR4["🔴 FWUPMGR_ERROR: NULL libVersion"]
    ERR4 --> RET_NULL4(["🔴 return NULL"])
    V4 -- No --> V5{"🟡 strlen(libVersion) > 64?"}

    V5 -- Yes --> ERR5["🔴 FWUPMGR_ERROR: version too long"]
    ERR5 --> RET_NULL5(["🔴 return NULL"])
    V5 -- No --> DBUS_CONN["⬜ g_bus_get_sync(SYSTEM)"]

    DBUS_CONN --> CONN_OK{"Connection OK?"}
    CONN_OK -- No --> ERR6["🔴 FWUPMGR_ERROR: D-Bus connect failed"]
    ERR6 --> RET_NULL6(["🔴 return NULL"])
    CONN_OK -- Yes --> PROXY["⬜ g_dbus_proxy_new_sync()"]

    PROXY --> PROXY_OK{"Proxy OK?"}
    PROXY_OK -- No --> ERR7["🔴 FWUPMGR_ERROR: proxy creation failed"]
    ERR7 --> UNREF_CONN1["⬜ g_object_unref(connection)"]
    UNREF_CONN1 --> RET_NULL7(["🔴 return NULL"])

    PROXY_OK -- Yes --> CALL["⬜ g_dbus_proxy_call_sync<br/>RegisterProcess(ss) → (t)<br/>timeout: 5000ms"]

    CALL --> CALL_OK{"D-Bus reply OK?"}
    CALL_OK -- No --> ERR8["🔴 FWUPMGR_ERROR: daemon rejected / timeout"]
    ERR8 --> UNREF_PROXY1["⬜ g_object_unref(proxy)"]
    UNREF_PROXY1 --> RET_NULL8(["🔴 return NULL"])

    CALL_OK -- Yes --> EXTRACT["⬜ Extract handler_id (uint64)<br/>from GVariant reply"]
    EXTRACT --> UNREF_RES["⬜ Unref result + proxy"]
    UNREF_RES --> MALLOC["⬜ malloc(32) for handle string"]

    MALLOC --> MALLOC_OK{"malloc OK?"}
    MALLOC_OK -- No --> ROLLBACK["🔴 Best-effort: call UnregisterProcess<br/>to undo daemon-side registration"]
    ROLLBACK --> RET_NULL9(["🔴 return NULL"])

    MALLOC_OK -- Yes --> SNPRINTF["⬜ snprintf(handle, 32, handler_id)"]
    SNPRINTF --> INIT["⬜ internal_system_init()"]

    INIT --> INIT_MUT["⬜ Initialize 3 mutexes"]
    INIT_MUT --> INIT_REG["⬜ Zero 3 callback registries"]
    INIT_REG --> INIT_CTX["⬜ Create GMainContext + GMainLoop"]
    INIT_CTX --> INIT_THR["⬜ pthread_create(bg_thread)"]
    INIT_THR --> INIT_WAIT["⬜ Spin-wait: bg_thread running? (max 5s)"]

    INIT_WAIT --> LOG_EXIT["🟣 FWUPMGR_INFO: Registered, handle=X"]
    LOG_EXIT --> RET_OK(["🟢 return handle"])

    style START fill:#3b82f6,stroke:#1d4ed8,color:#fff
    style RET_OK fill:#22c55e,stroke:#15803d,color:#fff
    style RET_NULL1 fill:#ef4444,stroke:#b91c1c,color:#fff
    style RET_NULL2 fill:#ef4444,stroke:#b91c1c,color:#fff
    style RET_NULL3 fill:#ef4444,stroke:#b91c1c,color:#fff
    style RET_NULL4 fill:#ef4444,stroke:#b91c1c,color:#fff
    style RET_NULL5 fill:#ef4444,stroke:#b91c1c,color:#fff
    style RET_NULL6 fill:#ef4444,stroke:#b91c1c,color:#fff
    style RET_NULL7 fill:#ef4444,stroke:#b91c1c,color:#fff
    style RET_NULL8 fill:#ef4444,stroke:#b91c1c,color:#fff
    style RET_NULL9 fill:#ef4444,stroke:#b91c1c,color:#fff
    style ERR1 fill:#ef4444,stroke:#b91c1c,color:#fff
    style ERR2 fill:#ef4444,stroke:#b91c1c,color:#fff
    style ERR3 fill:#ef4444,stroke:#b91c1c,color:#fff
    style ERR4 fill:#ef4444,stroke:#b91c1c,color:#fff
    style ERR5 fill:#ef4444,stroke:#b91c1c,color:#fff
    style ERR6 fill:#ef4444,stroke:#b91c1c,color:#fff
    style ERR7 fill:#ef4444,stroke:#b91c1c,color:#fff
    style ERR8 fill:#ef4444,stroke:#b91c1c,color:#fff
    style ROLLBACK fill:#ef4444,stroke:#b91c1c,color:#fff
    style V1 fill:#eab308,stroke:#a16207,color:#fff
    style V2 fill:#eab308,stroke:#a16207,color:#fff
    style V3 fill:#eab308,stroke:#a16207,color:#fff
    style V4 fill:#eab308,stroke:#a16207,color:#fff
    style V5 fill:#eab308,stroke:#a16207,color:#fff
    style LOG_ENTRY fill:#a855f7,stroke:#7e22ce,color:#fff
    style LOG_EXIT fill:#a855f7,stroke:#7e22ce,color:#fff
    style DBUS_CONN fill:#9ca3af,stroke:#4b5563,color:#fff
    style PROXY fill:#9ca3af,stroke:#4b5563,color:#fff
    style CALL fill:#9ca3af,stroke:#4b5563,color:#fff
    style EXTRACT fill:#9ca3af,stroke:#4b5563,color:#fff
    style UNREF_RES fill:#9ca3af,stroke:#4b5563,color:#fff
    style UNREF_CONN1 fill:#9ca3af,stroke:#4b5563,color:#fff
    style UNREF_PROXY1 fill:#9ca3af,stroke:#4b5563,color:#fff
    style MALLOC fill:#9ca3af,stroke:#4b5563,color:#fff
    style SNPRINTF fill:#9ca3af,stroke:#4b5563,color:#fff
    style INIT fill:#9ca3af,stroke:#4b5563,color:#fff
    style INIT_MUT fill:#9ca3af,stroke:#4b5563,color:#fff
    style INIT_REG fill:#9ca3af,stroke:#4b5563,color:#fff
    style INIT_CTX fill:#9ca3af,stroke:#4b5563,color:#fff
    style INIT_THR fill:#9ca3af,stroke:#4b5563,color:#fff
    style INIT_WAIT fill:#9ca3af,stroke:#4b5563,color:#fff
```

---

### 2.2 `checkForUpdate()`

```mermaid
flowchart TD
    START(["🔵 checkForUpdate(handle, callback)"])
    START --> LOG_ENTRY["🟣 FWUPMGR_INFO: checkForUpdate entry"]

    LOG_ENTRY --> V1{"🟡 handle == NULL<br/>or empty?"}
    V1 -- Yes --> ERR1["🔴 FWUPMGR_ERROR: invalid handle"]
    ERR1 --> RET_FAIL1(["🔴 return CHECK_FOR_UPDATE_FAIL"])
    V1 -- No --> V2{"🟡 callback == NULL?"}

    V2 -- Yes --> ERR2["🔴 FWUPMGR_ERROR: NULL callback"]
    ERR2 --> RET_FAIL2(["🔴 return CHECK_FOR_UPDATE_FAIL"])
    V2 -- No --> DBUS["⬜ g_bus_get_sync(SYSTEM)"]

    DBUS --> DBUS_OK{"Connection OK?"}
    DBUS_OK -- No --> ERR3["🔴 FWUPMGR_ERROR: D-Bus connect failed"]
    ERR3 --> RET_FAIL3(["🔴 return CHECK_FOR_UPDATE_FAIL"])

    DBUS_OK -- Yes --> LOCK["⬜ pthread_mutex_lock(registry)"]
    LOCK --> REG{"⬜ Find IDLE slot<br/>in g_registry<br/>(max 30)"}
    REG -- Full --> ERR4["🔴 FWUPMGR_ERROR: registry full"]
    ERR4 --> UNLOCK_ERR["⬜ pthread_mutex_unlock"]
    UNLOCK_ERR --> UNREF_ERR["⬜ g_object_unref(conn)"]
    UNREF_ERR --> RET_FAIL4(["🔴 return CHECK_FOR_UPDATE_FAIL"])

    REG -- Found --> STORE["⬜ Store callback + handle_key<br/>slot state: IDLE → PENDING"]
    STORE --> UNLOCK["⬜ pthread_mutex_unlock"]

    UNLOCK --> FIRE["⬜ g_dbus_connection_call<br/>CheckForUpdate(s handle)<br/>fire-and-forget, timeout=5000ms"]

    FIRE --> UNREF["⬜ g_object_unref(connection)"]
    UNREF --> LOG_EXIT["🟣 FWUPMGR_INFO: request sent"]
    LOG_EXIT --> RET_OK(["🟢 return CHECK_FOR_UPDATE_SUCCESS"])

    style START fill:#3b82f6,stroke:#1d4ed8,color:#fff
    style RET_OK fill:#22c55e,stroke:#15803d,color:#fff
    style RET_FAIL1 fill:#ef4444,stroke:#b91c1c,color:#fff
    style RET_FAIL2 fill:#ef4444,stroke:#b91c1c,color:#fff
    style RET_FAIL3 fill:#ef4444,stroke:#b91c1c,color:#fff
    style RET_FAIL4 fill:#ef4444,stroke:#b91c1c,color:#fff
    style ERR1 fill:#ef4444,stroke:#b91c1c,color:#fff
    style ERR2 fill:#ef4444,stroke:#b91c1c,color:#fff
    style ERR3 fill:#ef4444,stroke:#b91c1c,color:#fff
    style ERR4 fill:#ef4444,stroke:#b91c1c,color:#fff
    style V1 fill:#eab308,stroke:#a16207,color:#fff
    style V2 fill:#eab308,stroke:#a16207,color:#fff
    style LOG_ENTRY fill:#a855f7,stroke:#7e22ce,color:#fff
    style LOG_EXIT fill:#a855f7,stroke:#7e22ce,color:#fff
    style DBUS fill:#9ca3af,stroke:#4b5563,color:#fff
    style LOCK fill:#9ca3af,stroke:#4b5563,color:#fff
    style REG fill:#9ca3af,stroke:#4b5563,color:#fff
    style STORE fill:#9ca3af,stroke:#4b5563,color:#fff
    style UNLOCK fill:#9ca3af,stroke:#4b5563,color:#fff
    style UNLOCK_ERR fill:#9ca3af,stroke:#4b5563,color:#fff
    style UNREF_ERR fill:#9ca3af,stroke:#4b5563,color:#fff
    style FIRE fill:#9ca3af,stroke:#4b5563,color:#fff
    style UNREF fill:#9ca3af,stroke:#4b5563,color:#fff
```

---

### 2.3 `downloadFirmware()`

```mermaid
flowchart TD
    START(["🔵 downloadFirmware(handle, fwdwnlreq, callback)"])
    START --> LOG_ENTRY["🟣 FWUPMGR_INFO: downloadFirmware entry"]

    LOG_ENTRY --> V1{"🟡 handle == NULL<br/>or empty?"}
    V1 -- Yes --> ERR1["🔴 FWUPMGR_ERROR: invalid handle"] --> RET_FAIL1(["🔴 return RDKFW_DWNL_FAILED"])
    V1 -- No --> V2{"🟡 fwdwnlreq == NULL?"}

    V2 -- Yes --> ERR2["🔴 FWUPMGR_ERROR: NULL request"] --> RET_FAIL2(["🔴 return RDKFW_DWNL_FAILED"])
    V2 -- No --> V3{"🟡 firmwareName<br/>== NULL or empty?"}

    V3 -- Yes --> ERR3["🔴 FWUPMGR_ERROR: no firmware name"] --> RET_FAIL3(["🔴 return RDKFW_DWNL_FAILED"])
    V3 -- No --> V4{"🟡 callback == NULL?"}

    V4 -- Yes --> ERR4["🔴 FWUPMGR_ERROR: NULL callback"] --> RET_FAIL4(["🔴 return RDKFW_DWNL_FAILED"])
    V4 -- No --> DBUS["⬜ g_bus_get_sync(SYSTEM)"]

    DBUS --> DBUS_OK{"Connection OK?"}
    DBUS_OK -- No --> ERR5["🔴 FWUPMGR_ERROR: D-Bus failed"] --> RET_FAIL5(["🔴 return RDKFW_DWNL_FAILED"])

    DBUS_OK -- Yes --> LOCK["⬜ pthread_mutex_lock(dwnl_registry)"]
    LOCK --> REG["⬜ Find/overwrite slot<br/>in g_dwnl_registry<br/>state: IDLE → ACTIVE"]
    REG --> UNLOCK["⬜ pthread_mutex_unlock"]

    UNLOCK --> DEFAULT["⬜ Default NULL fields to empty string<br/>url = fwdwnlreq→downloadUrl ?? ''<br/>type = fwdwnlreq→TypeOfFirmware ?? ''"]

    DEFAULT --> FIRE["⬜ g_dbus_connection_call<br/>DownloadFirmware(s handle, s name, s url, s type)<br/>fire-and-forget"]

    FIRE --> UNREF["⬜ g_object_unref(connection)"]
    UNREF --> LOG_EXIT["🟣 FWUPMGR_INFO: download request sent"]
    LOG_EXIT --> RET_OK(["🟢 return RDKFW_DWNL_SUCCESS"])

    style START fill:#3b82f6,stroke:#1d4ed8,color:#fff
    style RET_OK fill:#22c55e,stroke:#15803d,color:#fff
    style RET_FAIL1 fill:#ef4444,stroke:#b91c1c,color:#fff
    style RET_FAIL2 fill:#ef4444,stroke:#b91c1c,color:#fff
    style RET_FAIL3 fill:#ef4444,stroke:#b91c1c,color:#fff
    style RET_FAIL4 fill:#ef4444,stroke:#b91c1c,color:#fff
    style RET_FAIL5 fill:#ef4444,stroke:#b91c1c,color:#fff
    style ERR1 fill:#ef4444,stroke:#b91c1c,color:#fff
    style ERR2 fill:#ef4444,stroke:#b91c1c,color:#fff
    style ERR3 fill:#ef4444,stroke:#b91c1c,color:#fff
    style ERR4 fill:#ef4444,stroke:#b91c1c,color:#fff
    style ERR5 fill:#ef4444,stroke:#b91c1c,color:#fff
    style V1 fill:#eab308,stroke:#a16207,color:#fff
    style V2 fill:#eab308,stroke:#a16207,color:#fff
    style V3 fill:#eab308,stroke:#a16207,color:#fff
    style V4 fill:#eab308,stroke:#a16207,color:#fff
    style LOG_ENTRY fill:#a855f7,stroke:#7e22ce,color:#fff
    style LOG_EXIT fill:#a855f7,stroke:#7e22ce,color:#fff
    style DBUS fill:#9ca3af,stroke:#4b5563,color:#fff
    style LOCK fill:#9ca3af,stroke:#4b5563,color:#fff
    style REG fill:#9ca3af,stroke:#4b5563,color:#fff
    style UNLOCK fill:#9ca3af,stroke:#4b5563,color:#fff
    style DEFAULT fill:#9ca3af,stroke:#4b5563,color:#fff
    style FIRE fill:#9ca3af,stroke:#4b5563,color:#fff
    style UNREF fill:#9ca3af,stroke:#4b5563,color:#fff
```

---

### 2.4 `updateFirmware()`

```mermaid
flowchart TD
    START(["🔵 updateFirmware(handle, fwupdatereq, callback)"])
    START --> LOG_ENTRY["🟣 FWUPMGR_INFO: updateFirmware entry"]

    LOG_ENTRY --> V1{"🟡 handle == NULL<br/>or empty?"}
    V1 -- Yes --> E1["🔴 FWUPMGR_ERROR: invalid handle"] --> F1(["🔴 return RDKFW_UPDATE_FAILED"])
    V1 -- No --> V2{"🟡 fwupdatereq == NULL?"}

    V2 -- Yes --> E2["🔴 FWUPMGR_ERROR: NULL request"] --> F2(["🔴 return RDKFW_UPDATE_FAILED"])
    V2 -- No --> V3{"🟡 firmwareName<br/>NULL or empty?"}

    V3 -- Yes --> E3["🔴 FWUPMGR_ERROR: missing name"] --> F3(["🔴 return RDKFW_UPDATE_FAILED"])
    V3 -- No --> V4{"🟡 TypeOfFirmware<br/>NULL or empty?"}

    V4 -- Yes --> E4["🔴 FWUPMGR_ERROR: missing type"] --> F4(["🔴 return RDKFW_UPDATE_FAILED"])
    V4 -- No --> V5{"🟡 callback == NULL?"}

    V5 -- Yes --> E5["🔴 FWUPMGR_ERROR: NULL callback"] --> F5(["🔴 return RDKFW_UPDATE_FAILED"])
    V5 -- No --> DBUS["⬜ g_bus_get_sync(SYSTEM)"]

    DBUS --> OK{"Connection OK?"}
    OK -- No --> E6["🔴 FWUPMGR_ERROR: D-Bus failed"] --> F6(["🔴 return RDKFW_UPDATE_FAILED"])

    OK -- Yes --> LOCK["⬜ pthread_mutex_lock(update_registry)"]
    LOCK --> REG["⬜ Register callback<br/>state: IDLE → ACTIVE"]
    REG --> UNLOCK["⬜ pthread_mutex_unlock"]

    UNLOCK --> CONV["⬜ Convert rebootImmediately<br/>bool → string: 'true'/'false'<br/>Default location to '' if NULL"]

    CONV --> FIRE["⬜ g_dbus_connection_call<br/>UpdateFirmware(s handle, s name,<br/>s location, s type, s reboot)<br/>fire-and-forget"]

    FIRE --> UNREF["⬜ g_object_unref(connection)"]
    UNREF --> LOG_EXIT["🟣 FWUPMGR_INFO: update request sent"]
    LOG_EXIT --> RET_OK(["🟢 return RDKFW_UPDATE_SUCCESS"])

    style START fill:#3b82f6,stroke:#1d4ed8,color:#fff
    style RET_OK fill:#22c55e,stroke:#15803d,color:#fff
    style F1 fill:#ef4444,stroke:#b91c1c,color:#fff
    style F2 fill:#ef4444,stroke:#b91c1c,color:#fff
    style F3 fill:#ef4444,stroke:#b91c1c,color:#fff
    style F4 fill:#ef4444,stroke:#b91c1c,color:#fff
    style F5 fill:#ef4444,stroke:#b91c1c,color:#fff
    style F6 fill:#ef4444,stroke:#b91c1c,color:#fff
    style E1 fill:#ef4444,stroke:#b91c1c,color:#fff
    style E2 fill:#ef4444,stroke:#b91c1c,color:#fff
    style E3 fill:#ef4444,stroke:#b91c1c,color:#fff
    style E4 fill:#ef4444,stroke:#b91c1c,color:#fff
    style E5 fill:#ef4444,stroke:#b91c1c,color:#fff
    style E6 fill:#ef4444,stroke:#b91c1c,color:#fff
    style V1 fill:#eab308,stroke:#a16207,color:#fff
    style V2 fill:#eab308,stroke:#a16207,color:#fff
    style V3 fill:#eab308,stroke:#a16207,color:#fff
    style V4 fill:#eab308,stroke:#a16207,color:#fff
    style V5 fill:#eab308,stroke:#a16207,color:#fff
    style LOG_ENTRY fill:#a855f7,stroke:#7e22ce,color:#fff
    style LOG_EXIT fill:#a855f7,stroke:#7e22ce,color:#fff
    style DBUS fill:#9ca3af,stroke:#4b5563,color:#fff
    style LOCK fill:#9ca3af,stroke:#4b5563,color:#fff
    style REG fill:#9ca3af,stroke:#4b5563,color:#fff
    style UNLOCK fill:#9ca3af,stroke:#4b5563,color:#fff
    style CONV fill:#9ca3af,stroke:#4b5563,color:#fff
    style FIRE fill:#9ca3af,stroke:#4b5563,color:#fff
    style UNREF fill:#9ca3af,stroke:#4b5563,color:#fff
```

---

### 2.5 `unregisterProcess()`

```mermaid
flowchart TD
    START(["🔵 unregisterProcess(handle)"])
    START --> V1{"🟡 handle == NULL?"}

    V1 -- Yes --> LOG_NULL["🟣 FWUPMGR_INFO: NULL handle, no-op"]
    LOG_NULL --> RET_VOID1(["🟢 return (void)"])

    V1 -- No --> PARSE["⬜ strtoull(handle) → handler_id"]
    PARSE --> PARSE_OK{"🟡 Parse valid?<br/>strict endptr check"}

    PARSE_OK -- No --> ERR1["🔴 FWUPMGR_ERROR: invalid handle format"]
    ERR1 --> FREE_HANDLE_ERR["⬜ free(handle)"]
    FREE_HANDLE_ERR --> RET_VOID2(["🔴 return (void)"])

    PARSE_OK -- Yes --> LOG_DEINIT["🟣 FWUPMGR_INFO: deinit starting"]
    LOG_DEINIT --> DEINIT["⬜ internal_system_deinit()"]

    DEINIT --> QUIT["⬜ g_main_loop_quit()"]
    QUIT --> JOIN["⬜ pthread_join(bg_thread)"]
    JOIN --> UNREF_LOOP["⬜ g_main_loop_unref()<br/>g_main_context_unref()"]
    UNREF_LOOP --> FREE_DWNL["⬜ internal_dwnl_system_deinit()"]
    FREE_DWNL --> FREE_UPD["⬜ internal_update_system_deinit()"]
    FREE_UPD --> FREE_REG["⬜ Free check registry handle_keys"]
    FREE_REG --> DESTROY_MTX["⬜ pthread_mutex_destroy() × 4"]

    DESTROY_MTX --> PROXY["⬜ Create D-Bus proxy<br/>(best-effort)"]
    PROXY --> PROXY_OK{"Proxy OK?"}

    PROXY_OK -- No --> LOG_WARN["🟣 FWUPMGR_WARN: daemon unreachable"]
    LOG_WARN --> FREE_HANDLE2["⬜ free(handle)"]
    FREE_HANDLE2 --> RET_VOID3(["🟢 return (void)"])

    PROXY_OK -- Yes --> CALL["⬜ g_dbus_proxy_call_sync<br/>UnregisterProcess(t handler_id)"]
    CALL --> CALL_OK{"D-Bus OK?"}

    CALL_OK -- No --> LOG_WARN2["🟣 FWUPMGR_WARN: unregister call failed"]
    LOG_WARN2 --> UNREF_P2["⬜ g_object_unref(proxy)"]
    UNREF_P2 --> FREE_HANDLE3["⬜ free(handle)"]
    FREE_HANDLE3 --> RET_VOID4(["🟢 return (void)"])

    CALL_OK -- Yes --> UNREF_ALL["⬜ g_object_unref(result + proxy)"]
    UNREF_ALL --> LOG_OK["🟣 FWUPMGR_INFO: unregistered OK"]
    LOG_OK --> FREE_HANDLE4["⬜ free(handle)"]
    FREE_HANDLE4 --> RET_VOID5(["🟢 return (void)"])

    style START fill:#3b82f6,stroke:#1d4ed8,color:#fff
    style RET_VOID1 fill:#22c55e,stroke:#15803d,color:#fff
    style RET_VOID2 fill:#ef4444,stroke:#b91c1c,color:#fff
    style RET_VOID3 fill:#22c55e,stroke:#15803d,color:#fff
    style RET_VOID4 fill:#22c55e,stroke:#15803d,color:#fff
    style RET_VOID5 fill:#22c55e,stroke:#15803d,color:#fff
    style ERR1 fill:#ef4444,stroke:#b91c1c,color:#fff
    style V1 fill:#eab308,stroke:#a16207,color:#fff
    style PARSE_OK fill:#eab308,stroke:#a16207,color:#fff
    style LOG_NULL fill:#a855f7,stroke:#7e22ce,color:#fff
    style LOG_DEINIT fill:#a855f7,stroke:#7e22ce,color:#fff
    style LOG_WARN fill:#a855f7,stroke:#7e22ce,color:#fff
    style LOG_WARN2 fill:#a855f7,stroke:#7e22ce,color:#fff
    style LOG_OK fill:#a855f7,stroke:#7e22ce,color:#fff
    style PARSE fill:#9ca3af,stroke:#4b5563,color:#fff
    style DEINIT fill:#9ca3af,stroke:#4b5563,color:#fff
    style QUIT fill:#9ca3af,stroke:#4b5563,color:#fff
    style JOIN fill:#9ca3af,stroke:#4b5563,color:#fff
    style UNREF_LOOP fill:#9ca3af,stroke:#4b5563,color:#fff
    style FREE_DWNL fill:#9ca3af,stroke:#4b5563,color:#fff
    style FREE_UPD fill:#9ca3af,stroke:#4b5563,color:#fff
    style FREE_REG fill:#9ca3af,stroke:#4b5563,color:#fff
    style DESTROY_MTX fill:#9ca3af,stroke:#4b5563,color:#fff
    style PROXY fill:#9ca3af,stroke:#4b5563,color:#fff
    style CALL fill:#9ca3af,stroke:#4b5563,color:#fff
    style UNREF_ALL fill:#9ca3af,stroke:#4b5563,color:#fff
    style FREE_HANDLE_ERR fill:#9ca3af,stroke:#4b5563,color:#fff
    style FREE_HANDLE2 fill:#9ca3af,stroke:#4b5563,color:#fff
    style FREE_HANDLE3 fill:#9ca3af,stroke:#4b5563,color:#fff
    style FREE_HANDLE4 fill:#9ca3af,stroke:#4b5563,color:#fff
    style UNREF_P2 fill:#9ca3af,stroke:#4b5563,color:#fff
```

---

## 3. Sequence Diagrams

### 3.1 Complete Firmware Update Flow

```mermaid
sequenceDiagram
    autonumber
    participant App as 🔵 Client App
    participant Lib as 🔵 Library API
    participant BG as ⬜ BG Thread
    participant Bus as ⬜ D-Bus
    participant Dmn as ⬜ Daemon
    participant XConf as ⬜ XConf Cloud
    participant CDN as ⬜ CDN
    participant HAL as ⬜ Device HAL

    Note over App,HAL: Phase 1 — Registration

    App->>+Lib: registerProcess("MyPlugin", "1.0")
    Lib-->>Lib: 🟡 validate inputs
    Lib->>+Bus: RegisterProcess(ss)
    Bus->>+Dmn: RegisterProcess
    Dmn-->>Dmn: assign handler_id=12345
    Dmn->>-Bus: reply (t) 12345
    Bus->>-Lib: GVariant reply
    Lib-->>Lib: ⬜ malloc handle, internal_system_init()
    Lib-->>BG: ⬜ pthread_create
    BG-->>Bus: subscribe to 3 signals
    BG-->>BG: ⬜ g_main_loop_run()
    Lib->>-App: 🟢 handle "12345"

    Note over App,HAL: Phase 2 — Check for Update

    App->>+Lib: checkForUpdate(handle, my_cb)
    Lib-->>Lib: 🟡 validate handle + callback
    Lib-->>Lib: ⬜ register cb in g_registry [PENDING]
    Lib->>Bus: CheckForUpdate(s "12345") [fire-and-forget]
    Lib->>-App: 🟢 CHECK_FOR_UPDATE_SUCCESS
    App-->>App: pthread_cond_timedwait (120s)

    Bus->>Dmn: CheckForUpdate
    Dmn->>+XConf: HTTP GET /xconf?model=...
    XConf->>-Dmn: firmware_v2.bin available

    Dmn->>Bus: signal: CheckForUpdateComplete(t,i,i,s,s,s,s)
    Bus->>BG: deliver signal
    BG-->>BG: ⬜ on_check_complete_signal()
    BG-->>BG: ⬜ dispatch_all_pending()
    BG->>App: 🟢 my_cb(&fwinfo) [status=AVAILABLE]
    App-->>App: pthread_cond_signal (wake main)

    Note over App,HAL: Phase 3 — Download

    App->>+Lib: downloadFirmware(handle, req, dl_cb)
    Lib-->>Lib: 🟡 validate inputs
    Lib-->>Lib: ⬜ register dl_cb in g_dwnl_registry [ACTIVE]
    Lib->>Bus: DownloadFirmware(ssss) [fire-and-forget]
    Lib->>-App: 🟢 RDKFW_DWNL_SUCCESS
    App-->>App: pthread_cond_timedwait (300s)

    Bus->>Dmn: DownloadFirmware
    Dmn->>+CDN: HTTPS GET firmware_v2.bin

    loop Every progress update
        CDN-->>Dmn: chunk received
        Dmn->>Bus: signal: DownloadProgress(t,s,u,s,s)
        Bus->>BG: deliver signal
        BG->>App: dl_cb(progress%, IN_PROGRESS)
    end

    CDN->>-Dmn: download complete
    Dmn->>Bus: signal: DownloadProgress(100, COMPLETED)
    Bus->>BG: deliver signal
    BG->>App: 🟢 dl_cb(100, COMPLETED)
    App-->>App: pthread_cond_signal

    Note over App,HAL: Phase 4 — Flash Update

    App->>+Lib: updateFirmware(handle, req, upd_cb)
    Lib-->>Lib: 🟡 validate inputs
    Lib-->>Lib: ⬜ register upd_cb in g_update_registry [ACTIVE]
    Lib->>Bus: UpdateFirmware(sssss) [fire-and-forget]
    Lib->>-App: 🟢 RDKFW_UPDATE_SUCCESS
    App-->>App: pthread_cond_timedwait (600s)

    Bus->>Dmn: UpdateFirmware
    Dmn->>+HAL: flash firmware_v2.bin

    loop Flash progress
        HAL-->>Dmn: partition written
        Dmn->>Bus: signal: UpdateProgress(t,s,i,i,s)
        Bus->>BG: deliver signal
        BG->>App: upd_cb(progress%, IN_PROGRESS)
    end

    HAL->>-Dmn: flash complete
    Dmn->>Bus: signal: UpdateProgress(100, COMPLETED)
    Bus->>BG: deliver signal
    BG->>App: 🟢 upd_cb(100, COMPLETED)

    Note over App,HAL: Phase 5 — Cleanup

    App->>+Lib: unregisterProcess(handle)
    Lib-->>Lib: ⬜ internal_system_deinit()
    Lib-->>BG: g_main_loop_quit()
    BG-->>Lib: thread exits
    Lib-->>Lib: ⬜ pthread_join, free registries
    Lib->>Bus: UnregisterProcess(t 12345)
    Bus->>Dmn: UnregisterProcess
    Dmn-->>Dmn: remove ProcessInfo
    Lib-->>Lib: ⬜ free(handle)
    Lib->>-App: 🟢 return (void)
```

---

### 3.2 Daemon Unavailable + Retry

```mermaid
sequenceDiagram
    autonumber
    participant App as 🔵 Client App
    participant Lib as 🔵 Library
    participant Bus as ⬜ D-Bus

    Note over App,Bus: Attempt 1 — Daemon not running

    App->>+Lib: registerProcess("MyPlugin", "1.0")
    Lib-->>Lib: 🟡 validate inputs — OK
    Lib->>+Bus: g_bus_get_sync(SYSTEM)
    Bus->>-Lib: connection OK
    Lib->>+Bus: g_dbus_proxy_new_sync(org.rdkfwupdater.Service)
    Bus->>-Lib: 🔴 Error: "name not provided by any .service files"
    Lib-->>Lib: 🟣 FWUPMGR_ERROR: proxy creation failed
    Lib-->>Lib: ⬜ g_object_unref(connection)
    Lib->>-App: 🔴 return NULL

    App-->>App: Log "daemon not running"
    App-->>App: sleep(5)

    Note over App,Bus: Attempt 2 — Daemon started between retries

    App->>+Lib: registerProcess("MyPlugin", "1.0")
    Lib-->>Lib: 🟡 validate inputs — OK
    Lib->>+Bus: g_bus_get_sync(SYSTEM)
    Bus->>-Lib: connection OK
    Lib->>+Bus: g_dbus_proxy_new_sync(org.rdkfwupdater.Service)
    Bus->>-Lib: 🟢 proxy OK
    Lib->>+Bus: RegisterProcess(ss)
    Bus->>-Lib: 🟢 handler_id = 42
    Lib-->>Lib: ⬜ internal_system_init()
    Lib->>-App: 🟢 handle "42"
    App-->>App: proceed with firmware operations
```

---

### 3.3 Callback Registration & Delivery

```mermaid
sequenceDiagram
    autonumber
    participant Caller as 🔵 Caller Thread
    participant API as 🔵 API Layer
    participant Reg as ⬜ Registry (mutex)
    participant Bus as ⬜ D-Bus
    participant BG as ⬜ BG Thread
    participant Dmn as ⬜ Daemon

    Note over Caller,Dmn: Step 1 — Register callback BEFORE sending D-Bus call

    Caller->>+API: checkForUpdate(handle, my_cb)
    API->>+Reg: 🔒 lock(g_registry.mutex)
    Reg-->>Reg: find IDLE slot
    Reg-->>Reg: store {callback=my_cb, handle_key=handle, state=PENDING}
    API->>-Reg: 🔓 unlock
    API->>Bus: fire-and-forget: CheckForUpdate(handle)
    API->>-Caller: 🟢 SUCCESS

    Note over Caller,Dmn: Step 2 — Signal arrives, two-phase dispatch

    Dmn->>Bus: signal: CheckForUpdateComplete(handler_id, ...)
    Bus->>+BG: on_check_complete_signal()

    BG->>+Reg: 🔒 lock(g_registry.mutex)
    Note over BG,Reg: Phase 1: Snapshot matching entries<br/>Copy callback pointers + data to local array<br/>Mark slots DISPATCHED
    BG->>-Reg: 🔓 unlock

    Note over BG: Phase 2: Dispatch WITHOUT holding lock
    BG->>Caller: my_cb(&fwinfo) — runs in BG thread context
    Note over Caller: Callback copies data, signals condvar

    BG->>+Reg: 🔒 lock(g_registry.mutex)
    Note over BG,Reg: Phase 3: Reset dispatched slots to IDLE
    BG->>-Reg: 🔓 unlock
    deactivate BG
```

---

### 3.4 Timeout Recovery

```mermaid
sequenceDiagram
    autonumber
    participant App as 🔵 Client App
    participant Lib as 🔵 Library
    participant BG as ⬜ BG Thread
    participant Dmn as ⬜ Daemon

    App->>Lib: checkForUpdate(handle, cb)
    Lib-->>Lib: register cb [PENDING]
    Lib->>Dmn: CheckForUpdate (fire-and-forget)
    Lib->>App: 🟢 SUCCESS

    App-->>App: pthread_cond_timedwait(120s)

    Note over Dmn: Daemon is overloaded or<br/>XConf is unreachable

    Note over App: ⏰ 120 seconds pass...<br/>No signal arrives

    App-->>App: 🔴 timedwait returns ETIMEDOUT

    Note over App: Recovery: clean up and retry

    App->>+Lib: unregisterProcess(handle)
    Lib-->>Lib: internal_system_deinit()
    Lib-->>BG: g_main_loop_quit()
    BG-->>Lib: thread exits
    Lib-->>Lib: free registries (stale PENDING cleared)
    Lib->>Dmn: UnregisterProcess
    Lib-->>Lib: free(handle)
    Lib->>-App: return

    App-->>App: sleep(30) — backoff

    App->>+Lib: registerProcess("MyPlugin", "1.0")
    Lib->>Dmn: RegisterProcess
    Dmn->>Lib: handler_id=99
    Lib-->>Lib: internal_system_init()
    Lib->>-App: 🟢 handle "99"

    App->>Lib: checkForUpdate("99", cb)
    Lib->>Dmn: CheckForUpdate
    Lib->>App: 🟢 SUCCESS
    App-->>App: pthread_cond_timedwait(120s)

    Dmn->>BG: signal: CheckForUpdateComplete
    BG->>App: 🟢 cb(&fwinfo)
    App-->>App: condvar signaled — success
```

---

## 4. Thread Safety Diagram

### 4.1 Multi-Client Shared State Map

```mermaid
graph TB
    subgraph PROCESS["Single Process Address Space"]
        subgraph MAIN["🔵 Main Thread"]
            MT_REG["registerProcess()"]
            MT_CHECK["checkForUpdate()"]
            MT_DWNL["downloadFirmware()"]
            MT_UPD["updateFirmware()"]
            MT_UNREG["unregisterProcess()"]
        end

        subgraph WORKER["🔵 Worker Thread (optional)"]
            WT_CHECK["checkForUpdate()"]
            WT_DWNL["downloadFirmware()"]
        end

        subgraph SHARED["⬜ Shared State (mutex-protected)"]
            subgraph REG1["g_registry<br/>🔒 registry_mutex"]
                R1_S["slots[0..29]<br/>state | callback | handle_key"]
            end
            subgraph REG2["g_dwnl_registry<br/>🔒 dwnl_mutex"]
                R2_S["slots[0..29]<br/>state | callback | handle_key"]
            end
            subgraph REG3["g_update_registry<br/>🔒 update_mutex"]
                R3_S["slots[0..29]<br/>state | callback | handle_key"]
            end
        end

        subgraph BG_THREAD["⬜ Background Thread"]
            LOOP["g_main_loop_run()"]
            SIG1["on_check_complete_signal()"]
            SIG2["on_download_progress_signal()"]
            SIG3["on_update_progress_signal()"]
            LOOP --> SIG1
            LOOP --> SIG2
            LOOP --> SIG3
        end

        MT_CHECK -- "🔒 lock → store → unlock" --> REG1
        WT_CHECK -- "🔒 lock → store → unlock" --> REG1
        SIG1 -- "🔒 lock → snapshot → unlock<br/>dispatch WITHOUT lock<br/>🔒 lock → reset → unlock" --> REG1

        MT_DWNL -- "🔒 lock → store → unlock" --> REG2
        WT_DWNL -- "🔒 lock → store → unlock" --> REG2
        SIG2 -- "🔒 lock → snapshot → unlock<br/>dispatch WITHOUT lock<br/>🔒 lock → reset → unlock" --> REG2

        MT_UPD -- "🔒 lock → store → unlock" --> REG3
        SIG3 -- "🔒 lock → snapshot → unlock<br/>dispatch WITHOUT lock<br/>🔒 lock → reset → unlock" --> REG3
    end

    style MAIN fill:#dbeafe,stroke:#2563eb,stroke-width:2px,color:#1e3a5f
    style WORKER fill:#dbeafe,stroke:#2563eb,stroke-width:2px,color:#1e3a5f
    style SHARED fill:#f3f4f6,stroke:#6b7280,stroke-width:2px,color:#374151
    style BG_THREAD fill:#f3f4f6,stroke:#6b7280,stroke-width:2px,color:#374151
    style REG1 fill:#fef3c7,stroke:#d97706,stroke-width:2px,color:#78350f
    style REG2 fill:#fef3c7,stroke:#d97706,stroke-width:2px,color:#78350f
    style REG3 fill:#fef3c7,stroke:#d97706,stroke-width:2px,color:#78350f
```

### 4.2 Two-Phase Dispatch (Deadlock Prevention)

```mermaid
flowchart LR
    subgraph PHASE1["Phase 1: Under Lock"]
        direction TB
        P1_LOCK["🔒 mutex_lock"]
        P1_SCAN["Scan slots:<br/>find matching handler_id"]
        P1_COPY["Copy callback ptr + data<br/>to stack-local snapshot[]"]
        P1_MARK["Mark slot DISPATCHED"]
        P1_UNLOCK["🔓 mutex_unlock"]
        P1_LOCK --> P1_SCAN --> P1_COPY --> P1_MARK --> P1_UNLOCK
    end

    subgraph PHASE2["Phase 2: No Lock Held"]
        direction TB
        P2_LOOP["for each snapshot entry"]
        P2_CALL["snapshot[i].callback(data)"]
        P2_NOTE["Client callback executes<br/>freely — can call any API<br/>NO DEADLOCK RISK"]
        P2_LOOP --> P2_CALL --> P2_NOTE
    end

    subgraph PHASE3["Phase 3: Under Lock"]
        direction TB
        P3_LOCK["🔒 mutex_lock"]
        P3_RESET["Reset DISPATCHED → IDLE<br/>free handle_key"]
        P3_UNLOCK["🔓 mutex_unlock"]
        P3_LOCK --> P3_RESET --> P3_UNLOCK
    end

    PHASE1 --> PHASE2 --> PHASE3

    style PHASE1 fill:#fef3c7,stroke:#d97706,stroke-width:2px,color:#78350f
    style PHASE2 fill:#dcfce7,stroke:#16a34a,stroke-width:2px,color:#14532d
    style PHASE3 fill:#fef3c7,stroke:#d97706,stroke-width:2px,color:#78350f
```

### 4.3 Connection Model — Why Each Call Is Independent

```mermaid
sequenceDiagram
    participant App as 🔵 Client
    participant Lib as 🔵 Library
    participant Bus as ⬜ D-Bus

    Note over App,Bus: Each API call creates + destroys its own connection

    App->>Lib: registerProcess()
    Lib->>+Bus: g_bus_get_sync() → conn_1 (sender :1.140)
    Lib->>Bus: RegisterProcess via conn_1
    Lib->>-Bus: g_object_unref(conn_1) — destroyed

    App->>Lib: checkForUpdate()
    Lib->>+Bus: g_bus_get_sync() → conn_2 (sender :1.141)
    Lib->>Bus: CheckForUpdate via conn_2
    Lib->>-Bus: g_object_unref(conn_2) — destroyed

    App->>Lib: downloadFirmware()
    Lib->>+Bus: g_bus_get_sync() → conn_3 (sender :1.142)
    Lib->>Bus: DownloadFirmware via conn_3
    Lib->>-Bus: g_object_unref(conn_3) — destroyed

    Note over Lib,Bus: BG Thread has separate PERSISTENT connection<br/>for signal subscription (lives entire session)
```

---

## 5. Memory Ownership Diagram

### 5.1 Ownership Boundaries

```mermaid
graph TB
    subgraph CALLER["🔵 Caller Allocates & Owns"]
        C1["processName (const char*)<br/>passed to registerProcess()"]
        C2["libVersion (const char*)<br/>passed to registerProcess()"]
        C3["FwDwnlReq struct<br/>passed to downloadFirmware()"]
        C4["FwUpdateReq struct<br/>passed to updateFirmware()"]
        C5["callback function pointers"]
        C6["condvar / mutex<br/>for synchronization"]
        C7["Copies of callback data<br/>(strncpy'd inside callback)"]
    end

    subgraph LIBRARY["🔵 Library Allocates & Owns"]
        L1["FirmwareInterfaceHandle<br/>malloc(32) in registerProcess()"]
        L2["handle_key strings<br/>strdup'd in registry slots"]
        L3["CallbackRegistry<br/>(static global array)"]
        L4["DwnlCallbackRegistry<br/>(static global array)"]
        L5["UpdateCbRegistry<br/>(static global array)"]
        L6["GMainContext<br/>+ GMainLoop"]
        L7["Background pthread"]
        L8["FwInfoData<br/>(stack in dispatch — transient!)"]
        L9["UpdateDetails<br/>(stack in dispatch — transient!)"]
    end

    subgraph DAEMON["⬜ Daemon Allocates & Owns"]
        D1["handler_id counter<br/>(uint64)"]
        D2["ProcessInfo records<br/>(GHashTable)"]
        D3["GVariant signal payloads<br/>(auto-freed after emission)"]
        D4["Downloaded firmware file<br/>(on-disk)"]
    end

    subgraph DBUS["⬜ D-Bus / GLib Manages"]
        B1["GDBusConnection<br/>(per-call ephemeral)"]
        B2["GDBusProxy objects"]
        B3["GVariant method args<br/>+ reply data"]
        B4["BG thread persistent<br/>GDBusConnection"]
    end

    L1 -. "freed by<br/>unregisterProcess()" .-> FREE1(("free()"))
    L2 -. "freed on slot reset" .-> FREE2(("free()"))
    L6 -. "unref'd in<br/>internal_system_deinit()" .-> FREE3(("unref"))
    B1 -. "unref'd after each API call" .-> FREE4(("unref"))
    B2 -. "unref'd after each API call" .-> FREE5(("unref"))

    style CALLER fill:#dbeafe,stroke:#2563eb,stroke-width:2px,color:#1e3a5f
    style LIBRARY fill:#dbeafe,stroke:#2563eb,stroke-width:2px,color:#1e3a5f
    style DAEMON fill:#f3f4f6,stroke:#6b7280,stroke-width:2px,color:#374151
    style DBUS fill:#f3f4f6,stroke:#6b7280,stroke-width:2px,color:#374151
    style FREE1 fill:#ef4444,stroke:#b91c1c,color:#fff
    style FREE2 fill:#ef4444,stroke:#b91c1c,color:#fff
    style FREE3 fill:#ef4444,stroke:#b91c1c,color:#fff
    style FREE4 fill:#ef4444,stroke:#b91c1c,color:#fff
    style FREE5 fill:#ef4444,stroke:#b91c1c,color:#fff
```

### 5.2 Callback Data Lifetime

```mermaid
flowchart LR
    subgraph SIGNAL["Signal Arrives"]
        SIG["D-Bus signal<br/>GVariant payload"]
    end

    subgraph PARSE["BG Thread Parses"]
        P1["g_variant_get()<br/>extract fields"]
        P2["Populate FwInfoData<br/>on STACK"]
        P3["Populate UpdateDetails<br/>on STACK"]
    end

    subgraph DISPATCH["Callback Executes"]
        CB["client_callback(&fwinfo)"]
        COPY["🟢 Client MUST copy<br/>any needed data NOW"]
        WARN["🔴 After callback returns:<br/>all pointers are INVALID"]
    end

    subgraph CLEANUP["Stack Unwinds"]
        CL["FwInfoData destroyed<br/>UpdateDetails destroyed<br/>GVariant unreffed"]
    end

    SIG --> PARSE --> DISPATCH --> CLEANUP

    style SIGNAL fill:#f3f4f6,stroke:#6b7280,color:#374151
    style PARSE fill:#f3f4f6,stroke:#6b7280,color:#374151
    style DISPATCH fill:#dbeafe,stroke:#2563eb,color:#1e3a5f
    style CLEANUP fill:#fecaca,stroke:#dc2626,color:#7f1d1d
    style COPY fill:#22c55e,stroke:#15803d,color:#fff
    style WARN fill:#ef4444,stroke:#b91c1c,color:#fff
```

### 5.3 Cleanup Sequence

```mermaid
flowchart TD
    UNREG(["🔵 unregisterProcess(handle)"])
    UNREG --> DEINIT["⬜ internal_system_deinit()"]

    DEINIT --> S1["⬜ g_main_loop_quit()<br/>→ BG thread wakes up"]
    S1 --> S2["⬜ pthread_join()<br/>→ BG thread fully stopped"]
    S2 --> S3["⬜ g_main_loop_unref()<br/>g_main_context_unref()"]
    S3 --> S4["⬜ internal_dwnl_system_deinit()<br/>→ free dwnl registry handle_keys<br/>→ destroy dwnl_mutex"]
    S4 --> S5["⬜ internal_update_system_deinit()<br/>→ free update registry handle_keys<br/>→ destroy update_mutex"]
    S5 --> S6["⬜ Free check registry handle_keys"]
    S6 --> S7["⬜ pthread_mutex_destroy × 4<br/>(bg_thread, registry, dwnl, update)"]
    S7 --> S8["⬜ D-Bus: UnregisterProcess<br/>(best-effort)"]
    S8 --> S9["⬜ free(handle)<br/>🔴 handle pointer now INVALID"]
    S9 --> DONE(["🟢 All resources released"])

    style UNREG fill:#3b82f6,stroke:#1d4ed8,color:#fff
    style DONE fill:#22c55e,stroke:#15803d,color:#fff
    style S9 fill:#ef4444,stroke:#b91c1c,color:#fff
    style DEINIT fill:#9ca3af,stroke:#4b5563,color:#fff
    style S1 fill:#9ca3af,stroke:#4b5563,color:#fff
    style S2 fill:#9ca3af,stroke:#4b5563,color:#fff
    style S3 fill:#9ca3af,stroke:#4b5563,color:#fff
    style S4 fill:#9ca3af,stroke:#4b5563,color:#fff
    style S5 fill:#9ca3af,stroke:#4b5563,color:#fff
    style S6 fill:#9ca3af,stroke:#4b5563,color:#fff
    style S7 fill:#9ca3af,stroke:#4b5563,color:#fff
    style S8 fill:#9ca3af,stroke:#4b5563,color:#fff
```

---

## 6. Logging Pipeline Diagram

### 6.1 Three-Module Logging Architecture

```mermaid
graph TB
    subgraph SOURCES["Log Sources"]
        direction TB
        subgraph CLIENT_LOG["🔵 Client Application"]
            EX_INFO["EXAMPLE_INFO(...)"]
            EX_ERR["EXAMPLE_ERROR(...)"]
            EX_DBG["EXAMPLE_DEBUG(...)"]
        end

        subgraph LIB_LOG["🔵 Library Internals"]
            FW_INFO["FWUPMGR_INFO(...)"]
            FW_ERR["FWUPMGR_ERROR(...)"]
            FW_WARN["FWUPMGR_WARN(...)"]
            FW_DBG["FWUPMGR_DEBUG(...)"]
        end

        subgraph DMN_LOG["⬜ Daemon"]
            SW_INFO["SWLOG_INFO(...)"]
            SW_ERR["SWLOG_ERROR(...)"]
        end
    end

    subgraph MACROS["🟣 Macro Expansion Layer"]
        M_CLIENT["Module: LOG.RDK.EXAMPLE"]
        M_LIB["Module: LOG.RDK.FWUPMGR"]
        M_DMN["Module: LOG.RDK.FWUPG"]
    end

    subgraph BACKEND["🟣 Logging Backend"]
        RDK{"RDK_LOGGER<br/>defined?"}
        YES["RDK_LOG(level, module, fmt, ...)<br/>→ rdk_logger subsystem"]
        NO["fprintf(stderr, [module] fmt, ...)<br/>→ console fallback"]
    end

    subgraph OUTPUT["🟣 Log Output"]
        FILE["/opt/logs/rdkFwupdateMgr.log"]
        CONSOLE["stderr (unit tests)"]
    end

    EX_INFO & EX_ERR & EX_DBG --> M_CLIENT
    FW_INFO & FW_ERR & FW_WARN & FW_DBG --> M_LIB
    SW_INFO & SW_ERR --> M_DMN

    M_CLIENT & M_LIB & M_DMN --> RDK

    RDK -- "Yes (production)" --> YES --> FILE
    RDK -- "No (unit test)" --> NO --> CONSOLE

    style CLIENT_LOG fill:#dbeafe,stroke:#2563eb,stroke-width:2px,color:#1e3a5f
    style LIB_LOG fill:#dbeafe,stroke:#2563eb,stroke-width:2px,color:#1e3a5f
    style DMN_LOG fill:#f3f4f6,stroke:#6b7280,stroke-width:2px,color:#374151
    style MACROS fill:#f3e8ff,stroke:#9333ea,stroke-width:2px,color:#581c87
    style BACKEND fill:#f3e8ff,stroke:#9333ea,stroke-width:2px,color:#581c87
    style OUTPUT fill:#f3e8ff,stroke:#9333ea,stroke-width:2px,color:#581c87
    style RDK fill:#a855f7,stroke:#7e22ce,color:#fff
```

### 6.2 Log Points by API Function

```mermaid
graph LR
    subgraph REGISTER["registerProcess()"]
        R_ENTRY["🟣 INFO: Entry<br/>(processName)"]
        R_VAL["🟣 ERROR: Validation<br/>failure details"]
        R_DBUS["🟣 ERROR: D-Bus<br/>connection/call failure"]
        R_ID["🟣 INFO: Got handler_id"]
        R_INIT["🟣 INFO: BG thread started"]
        R_OK["🟣 INFO: Registered<br/>handle=X"]
    end

    subgraph CHECK["checkForUpdate()"]
        C_ENTRY["🟣 INFO: Entry<br/>(handle)"]
        C_VAL["🟣 ERROR: Bad handle<br/>or NULL callback"]
        C_REG["🟣 DEBUG: Slot allocated<br/>in registry"]
        C_SEND["🟣 INFO: Request sent"]
    end

    subgraph DOWNLOAD["downloadFirmware()"]
        D_ENTRY["🟣 INFO: Entry<br/>(handle, firmwareName)"]
        D_VAL["🟣 ERROR: Missing<br/>required fields"]
        D_REG["🟣 DEBUG: Slot allocated<br/>in dwnl_registry"]
        D_SEND["🟣 INFO: Request sent"]
    end

    subgraph UPDATE["updateFirmware()"]
        U_ENTRY["🟣 INFO: Entry<br/>(handle, firmwareName)"]
        U_VAL["🟣 ERROR: Missing<br/>name or type"]
        U_REG["🟣 DEBUG: Slot allocated"]
        U_SEND["🟣 INFO: Request sent"]
    end

    subgraph UNREGISTER["unregisterProcess()"]
        X_ENTRY["🟣 INFO: Entry<br/>(handle)"]
        X_PARSE["🟣 ERROR: Invalid<br/>handle format"]
        X_DEINIT["🟣 INFO: Deinit<br/>starting"]
        X_WARN["🟣 WARN: D-Bus call<br/>failed (best-effort)"]
        X_OK["🟣 INFO: Unregistered OK"]
    end

    subgraph BG_SIGNALS["Background Thread Signals"]
        S_RECV["🟣 DEBUG: Signal received<br/>(handler_id, type)"]
        S_DISPATCH["🟣 DEBUG: Dispatching<br/>N callbacks"]
        S_RESET["🟣 DEBUG: Slot reset<br/>to IDLE"]
    end

    style REGISTER fill:#dbeafe,stroke:#2563eb,color:#1e3a5f
    style CHECK fill:#dbeafe,stroke:#2563eb,color:#1e3a5f
    style DOWNLOAD fill:#dbeafe,stroke:#2563eb,color:#1e3a5f
    style UPDATE fill:#dbeafe,stroke:#2563eb,color:#1e3a5f
    style UNREGISTER fill:#dbeafe,stroke:#2563eb,color:#1e3a5f
    style BG_SIGNALS fill:#f3f4f6,stroke:#6b7280,color:#374151

    style R_ENTRY fill:#a855f7,stroke:#7e22ce,color:#fff
    style R_VAL fill:#a855f7,stroke:#7e22ce,color:#fff
    style R_DBUS fill:#a855f7,stroke:#7e22ce,color:#fff
    style R_ID fill:#a855f7,stroke:#7e22ce,color:#fff
    style R_INIT fill:#a855f7,stroke:#7e22ce,color:#fff
    style R_OK fill:#a855f7,stroke:#7e22ce,color:#fff
    style C_ENTRY fill:#a855f7,stroke:#7e22ce,color:#fff
    style C_VAL fill:#a855f7,stroke:#7e22ce,color:#fff
    style C_REG fill:#a855f7,stroke:#7e22ce,color:#fff
    style C_SEND fill:#a855f7,stroke:#7e22ce,color:#fff
    style D_ENTRY fill:#a855f7,stroke:#7e22ce,color:#fff
    style D_VAL fill:#a855f7,stroke:#7e22ce,color:#fff
    style D_REG fill:#a855f7,stroke:#7e22ce,color:#fff
    style D_SEND fill:#a855f7,stroke:#7e22ce,color:#fff
    style U_ENTRY fill:#a855f7,stroke:#7e22ce,color:#fff
    style U_VAL fill:#a855f7,stroke:#7e22ce,color:#fff
    style U_REG fill:#a855f7,stroke:#7e22ce,color:#fff
    style U_SEND fill:#a855f7,stroke:#7e22ce,color:#fff
    style X_ENTRY fill:#a855f7,stroke:#7e22ce,color:#fff
    style X_PARSE fill:#a855f7,stroke:#7e22ce,color:#fff
    style X_DEINIT fill:#a855f7,stroke:#7e22ce,color:#fff
    style X_WARN fill:#a855f7,stroke:#7e22ce,color:#fff
    style X_OK fill:#a855f7,stroke:#7e22ce,color:#fff
    style S_RECV fill:#a855f7,stroke:#7e22ce,color:#fff
    style S_DISPATCH fill:#a855f7,stroke:#7e22ce,color:#fff
    style S_RESET fill:#a855f7,stroke:#7e22ce,color:#fff
```

### 6.3 Correlation — Tracing a Request by handler_id

```mermaid
sequenceDiagram
    participant App as 🔵 Client
    participant Lib as 🔵 Library
    participant Log as 🟣 Log File
    participant Dmn as ⬜ Daemon

    App->>Lib: registerProcess("MyPlugin", "1.0")
    Lib->>Log: [FWUPMGR] INFO: registerProcess entry, processName=MyPlugin
    Lib->>Dmn: RegisterProcess
    Dmn->>Log: [FWUPG] INFO: Registered MyPlugin, handler_id=12345
    Lib->>Log: [FWUPMGR] INFO: Got handler_id=12345
    Lib->>App: handle "12345"
    App->>Log: [EXAMPLE] INFO: Registered with handle 12345

    Note over Log: All subsequent logs include handler_id=12345<br/>for end-to-end request correlation

    App->>Lib: checkForUpdate("12345", cb)
    Lib->>Log: [FWUPMGR] INFO: checkForUpdate, handle=12345
    Lib->>Log: [FWUPMGR] DEBUG: registry slot 3 allocated, handler_id=12345
    Lib->>Dmn: CheckForUpdate("12345")
    Dmn->>Log: [FWUPG] INFO: CheckForUpdate for handler_id=12345, querying XConf

    Note over Dmn: XConf responds

    Dmn->>Log: [FWUPG] INFO: CheckForUpdate result for 12345: AVAILABLE
    Dmn->>Lib: signal CheckForUpdateComplete(12345, ...)
    Lib->>Log: [FWUPMGR] DEBUG: signal received, handler_id=12345
    Lib->>Log: [FWUPMGR] DEBUG: dispatching 1 callback(s) for handler_id=12345
    Lib->>App: cb(&fwinfo)
    App->>Log: [EXAMPLE] INFO: Firmware available for handle 12345
```

### 6.4 Log Lifecycle Ownership

```mermaid
flowchart TD
    subgraph APP["🔵 Client Application (caller's responsibility)"]
        INIT["log_init()<br/>⚠️ MUST call before any library API"]
        USE["Use library APIs<br/>(all logging works)"]
        EXIT["log_exit()<br/>⚠️ MUST call after unregisterProcess()"]
        INIT --> USE --> EXIT
    end

    subgraph LIB_INTERNAL["🔵 Library (never calls log_init/exit)"]
        LOG_CALL["FWUPMGR_INFO/ERROR/DEBUG/WARN<br/>Just emits — assumes log is initialized"]
    end

    USE -.-> LOG_CALL

    subgraph WRONG["🔴 WRONG — Double Init"]
        BAD["Library calling log_init()<br/>→ corrupts app's log state"]
    end

    style APP fill:#dbeafe,stroke:#2563eb,stroke-width:2px,color:#1e3a5f
    style LIB_INTERNAL fill:#dbeafe,stroke:#2563eb,stroke-width:2px,color:#1e3a5f
    style WRONG fill:#fecaca,stroke:#dc2626,stroke-width:2px,color:#7f1d1d
    style INIT fill:#22c55e,stroke:#15803d,color:#fff
    style EXIT fill:#22c55e,stroke:#15803d,color:#fff
    style BAD fill:#ef4444,stroke:#b91c1c,color:#fff
```

---

## Appendix: State Machine Diagrams

### A.1 Check Callback Registry Slot States

```mermaid
stateDiagram-v2
    [*] --> IDLE

    IDLE --> PENDING : checkForUpdate()<br/>registers callback
    PENDING --> DISPATCHED : Signal arrives<br/>Phase 1 snapshots
    DISPATCHED --> IDLE : Phase 3 resets<br/>after callback returns
    PENDING --> TIMED_OUT : Future: sweep thread<br/>(not yet implemented)
    TIMED_OUT --> IDLE : Cleanup

    state IDLE {
        [*] : Slot available
    }
    state PENDING {
        [*] : Callback stored, waiting for signal
    }
    state DISPATCHED {
        [*] : Callback is being invoked
    }
    state TIMED_OUT {
        [*] : Stale entry (future)
    }
```

### A.2 Download/Update Registry Slot States

```mermaid
stateDiagram-v2
    [*] --> IDLE

    IDLE --> ACTIVE : downloadFirmware() /<br/>updateFirmware()
    ACTIVE --> ACTIVE : Progress signal<br/>(still in progress)
    ACTIVE --> IDLE : Terminal signal<br/>(COMPLETED or ERROR)

    state IDLE {
        [*] : Slot available
    }
    state ACTIVE {
        [*] : Callback registered,<br/>receiving progress signals
    }
```

### A.3 Library Handle Lifecycle

```mermaid
stateDiagram-v2
    [*] --> UNLINKED : Library loaded

    UNLINKED --> REGISTERED : registerProcess()<br/>returns non-NULL handle
    UNLINKED --> UNLINKED : registerProcess()<br/>returns NULL (error)

    REGISTERED --> ACTIVE : checkForUpdate() /<br/>downloadFirmware() /<br/>updateFirmware()
    ACTIVE --> ACTIVE : More API calls
    ACTIVE --> REGISTERED : All callbacks complete

    REGISTERED --> UNLINKED : unregisterProcess()
    ACTIVE --> UNLINKED : unregisterProcess()<br/>(stale callbacks cleared)

    state UNLINKED {
        [*] : No handle, no BG thread
    }
    state REGISTERED {
        [*] : Handle valid,<br/>BG thread running,<br/>no pending ops
    }
    state ACTIVE {
        [*] : Handle valid,<br/>pending callbacks<br/>in registries
    }
```

---

*End of Visual Engineering Documentation*
