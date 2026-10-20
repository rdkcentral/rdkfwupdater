# rdkfwupdater — OpenSpec Documentation

> **Repository:** `rdkcentral/rdkfwupdater`  
> **Purpose:** Specification, architecture, and design documentation for the RDK Firmware Update Manager  
> **Status:** Active — code-verified baseline

This README indexes the architectural specifications, runtime documentation, and design records under this directory for the `rdkfwupdater` project. The indexed material covers both production binaries (`rdkFwupdateMgr` daemon and `rdkvfwupgrader` one-shot updater) and the client-facing shared library (`librdkFwupdateMgr.so`).

---

## Quick Navigation

| Document                                                    | What It Covers                                                                                  |
| ----------------------------------------------------------- | ----------------------------------------------------------------------------------------------- |
| [project.md](project.md)                     | Baseline architecture overview — system purpose, artifact inventory, subsystem responsibilities |
| [gaps-and-unknowns.md](gaps-and-unknowns.md) | Known unknowns, inferences, and assumptions requiring manual validation                         |

---

## Document Structure

### `specs/` — Subsystem Specifications

Behavioral contracts and API specifications per subsystem. Each folder contains a `spec.md`.

| Spec                                                | Subsystem                                                       | Priority |
| --------------------------------------------------- | --------------------------------------------------------------- | -------- |
| `specs/client-sdk/spec.md`                   | `librdkFwupdateMgr.so` public C API and async callback model    | P0       |
| `specs/dbus-ipc/spec.md`                       | D-Bus method/signal contracts for `org.rdkfwupdater.Interface`  | P0       |
| `specs/daemon-runtime/spec.md`           | Daemon lifecycle state machine (`STATE_INIT` → `STATE_IDLE`)    | P0       |
| `specs/download-engine/spec.md`         | HTTP/HTTPS firmware download engine (libcurl, throttling, mTLS) | P0       |
| `specs/direct-cdn-download/spec.md` | Direct CDN download path and CodeBig fallback                   | P1       |
| `specs/firmware-validation/spec.md` | Firmware image validation and version comparison                | P1       |
| `specs/updater-execution/spec.md`     | One-shot orchestrator (`rdkvfwupgrader`) behavioral contract    | P1       |
| `specs/operational-safety/spec.md`   | Concurrency control, mutex ownership, piggybacking semantics    | P1       |
| `specs/retry-recovery/spec.md`           | Download retry logic and state-red recovery path                | P1       |
| `specs/test-mock-isolation/spec.md` | Test isolation boundaries and mock interfaces                   | P2       |

---

### `runtime/` — Runtime Behavior Documentation

Sequence diagrams and lifecycle documentation for both execution models.

| Document                                                       | What It Covers                                                           |
| -------------------------------------------------------------- | ------------------------------------------------------------------------ |
| `runtime/rdkFwupdateMgr-lifecycle.md`   | Daemon lifecycle: systemd service config, state machine, signal handling |
| `runtime/rdkFwupdateMgr-sequence.md`     | Daemon request/response sequence diagrams                                |
| `runtime/rdkvfwupgrader-lifecycle.md`   | One-shot updater lifecycle and exit code semantics                       |
| `runtime/rdkvfwupgrader-sequence.md`     | One-shot updater sequence diagrams                                       |
| `runtime/client-daemon-interaction.md` | D-Bus fire-and-forget protocol between client library and daemon         |
| `runtime/daemon-threading-model.md`       | GLib main loop, GTask worker threads, mutex ownership map                |
| `runtime/firmware-update-flows.md`         | End-to-end call flows: CheckForUpdate, DownloadFirmware, UpdateFirmware  |

---

### `dbus/` — D-Bus Interface Reference

| Document                                    | What It Covers                                                                          |
| -------------------------------------------- | --------------------------------------------------------------------------------------- |
| `dbus/dbus-architecture.md` | Full D-Bus interface introspection XML, method signatures, signal payloads, error codes |

---

### `diagrams/` — Architecture Diagrams

| Document                                                 | What It Covers                                            |
| -------------------------------------------------------- | --------------------------------------------------------- |
| `diagrams/architecture-diagrams.md`   | System component diagram, shared library dependency graph |
| `diagrams/subsystem-architecture.md` | Subsystem interaction diagram and layer boundaries        |

---

### `subsystems/` — Subsystem Analysis

| Document                                              | What It Covers                                                                 |
| ----------------------------------------------------- | ------------------------------------------------------------------------------ |
| `subsystems/subsystem-inventory.md` | Full inventory of 17 subsystems with type, scope, and spec priority            |
| `subsystems/subsystem-map.md`             | Classification matrix: which subsystems are shared vs execution-model-specific |
| `subsystems/spec-boundaries.md`         | Prioritized spec recommendations (P0–P3 tiers) with rationale                  |
| `subsystems/client-library.md`           | Client library architecture: public API surface, background thread model       |

---

### `changes/` — Design Records and Change Proposals

Active and archived design records for tracked changes.

| Entry                                           | Status   | What It Covers                                                                    |
| ----------------------------------------------- | -------- | --------------------------------------------------------------------------------- |
| `changes/fix-state-red-dual-instance-race/` | Active   | Fix for state-red + dual-instance race condition (proposal, design, specs, tasks) |
| `changes/direct-cdn-token-and-mtls-fix/`       | Active   | Corrects direct-CDN token and mTLS handling                                       |
| `changes/archive/`                                   | Archived | Completed change records (direct-cdn-adoption, parity-guards, L1 segfault fix)    |

---

## How to Update This Documentation

- **If a subsystem behavior changes** → update the relevant `specs/<subsystem>/spec.md`
- **If the D-Bus interface changes** → update `dbus/dbus-architecture.md` and `specs/dbus-ipc/spec.md`
- **If a new design change is planned** → create `changes/<change-name>/` with `proposal.md`, `design.md`, `tasks.md`
- **If a gap is resolved or a new unknown is discovered** → update `gaps-and-unknowns.md`
- **For architecture-level additions** → update `project.md` and the relevant `diagrams/` file

---

## Key Concepts

| Term                    | Meaning                                                                              |
| ----------------------- | ------------------------------------------------------------------------------------ |
| `rdkFwupdateMgr`        | Persistent systemd daemon; serves firmware operations via D-Bus                      |
| `rdkvfwupgrader`        | Legacy one-shot CLI binary; invoked by Maintenance Manager                           |
| `librdkFwupdateMgr.so`  | Client shared library; abstracts D-Bus behind a plain C callback API                 |
| XConf                   | Cloud configuration server; queried via HTTP POST for firmware availability          |
| CDN                     | Content delivery network hosting firmware images for HTTP/HTTPS download             |
| IARM                    | Inter-Application Resource Manager; platform event bus for RDK-V                     |
| RFC                     | Runtime Feature Configuration; key-value store for tunable parameters                |
| PCI / PDRI / Peripheral | Firmware artifact types: main image / disaster-recovery image / peripheral device    |
| D-Bus bus name          | `org.rdkfwupdater.Service` on the system bus; interface `org.rdkfwupdater.Interface` |

---

## Component Interaction Matrix

Full reference of all external systems this component communicates with.

| Target Component / Layer            | Interaction Purpose                                                                       | Key APIs / Topics                                                                                                                                                            |
| ----------------------------------- | ----------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **XConf Server**                    | Firmware availability query — HTTP POST with device identity and current firmware version | `createJsonString()`, `rdkv_upgrade_request()`, `getXconfRespData()`                                                                                                         |
| **Firmware CDN**                    | Firmware image download over HTTP/HTTPS                                                   | `rdkv_upgrade_request()`, `downloadFile()`, `codebigdownloadFile()`, libcurl                                                                                                 |
| **System Manager (IARM)**           | Broadcast download and firmware state transitions to platform event subscribers           | `IARM_Bus_BroadcastEvent()` — `IARM_BUS_SYSMGR_SYSSTATE_FIRMWARE_DWNLD`, `IARM_BUS_SYSMGR_SYSSTATE_FIRMWARE_UPDATE_STATE`, `IARM_BUS_SYSMGR_SYSSTATE_RED_RECOV_UPDATE_STATE` |
| **Maintenance Manager (IARM)**      | Report firmware download module status for maintenance window coordination                | `IARM_Bus_BroadcastEvent()` — `IARM_BUS_MAINTENANCEMGR_EVENT_UPDATE`                                                                                                         |
| **Application Mode Source (IARM)**  | Receive foreground/background transitions to control download speed                       | `IARM_BUS_RDKVFWUPGRADER_MODECHANGED`                                                                                                                                        |
| **RFC API**                         | Read runtime configuration parameters; write download status notifications                | `getRFCSettings()`, `read_RFCProperty()`, `write_RFCProperty()` — `Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.*`                                                                 |
| **Flash HAL (`librdksw_flash.so`)** | Write firmware image to device storage partition                                          | `flashImage()`                                                                                                                                                               |
| **libcurl**                         | HTTP/HTTPS communication for XConf queries and firmware downloads                         | `doHttpFileDownload()`, `doInteruptDwnl()`, `doGetDwnlBytes()`, `doStopDownload()`                                                                                           |
| **D-Bus (system bus)**              | Expose firmware management service to client applications                                 | `org.rdkfwupdater.Interface` on `org.rdkfwupdater.Service`                                                                                                                   |
| **`librdkFwupdateMgr` consumers**   | Provide firmware lifecycle APIs to TR-069 agents, UI services, and monitoring daemons     | `registerProcess()`, `checkForUpdate()`, `downloadFirmware()`, `updateFirmware()`, `unregisterProcess()`                                                                     |

---

## Events Published

All events emitted by the daemon to external subscribers.

| Event Name                | IARM Bus / Topic                                                                                     | Payload Filter / Trigger Condition                                                                                                                                                             | Subscriber                            |
| ------------------------- | ---------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------- |
| `ImageDwldEvent`          | `IARM_BUS_SYSMGR_NAME` / `IARM_BUS_SYSMGR_EVENT_SYSTEMSTATE`; filter: `data.systemStates.stateId = IARM_BUS_SYSMGR_SYSSTATE_FIRMWARE_DWNLD`          | Firmware download state changes                                                                                                                                               | System Manager subscribers            |
| `FirmwareStateEvent`      | `IARM_BUS_SYSMGR_NAME` / `IARM_BUS_SYSMGR_EVENT_SYSTEMSTATE`; filter: `data.systemStates.stateId = IARM_BUS_SYSMGR_SYSSTATE_FIRMWARE_UPDATE_STATE`  | Firmware lifecycle transitions: uninitialized, requesting, downloading, failed, download/validation complete, opt-out hold, preparing/critical reboot, or no upgrade required | System Manager subscribers            |
| `RedStateEvent`           | `IARM_BUS_SYSMGR_NAME` / `IARM_BUS_SYSMGR_EVENT_SYSTEMSTATE`; filter: `data.systemStates.stateId = IARM_BUS_SYSMGR_SYSSTATE_RED_RECOV_UPDATE_STATE` | State-red recovery transitions: started, downloaded, programmed, or completed                                                                                                 | System Manager subscribers            |
| Maintenance module status | `IARM_BUS_MAINTENANCEMGR_NAME` / `IARM_BUS_MAINTENANCEMGR_EVENT_UPDATE`                            | Download in progress, complete, error, aborted, critical-update, or reboot-required status when Maintenance Manager is enabled                                                | Maintenance Manager                   |
| `CheckForUpdateComplete`  | D-Bus `org.rdkfwupdater.Interface` signal                                                            | XConf query worker thread completes                                                                                                                                           | `librdkFwupdateMgr` background thread |
| `DownloadProgress`        | D-Bus `org.rdkfwupdater.Interface` signal                                                            | Download bytes progress, completion, or error                                                                                                                                 | `librdkFwupdateMgr` background thread |
| `UpdateProgress`          | D-Bus `org.rdkfwupdater.Interface` signal                                                            | Flash progress, completion, or error                                                                                                                                          | `librdkFwupdateMgr` background thread |

---

## Configuration Reference

### Key Configuration Files

| File                               | Purpose                                                                                                                                                                                                    | Override Mechanism                                                                  |
| ---------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------- |
| `/etc/device.properties`           | Device model, MAC address, partner ID, firmware storage path, build type, and other device identity fields                                                                                                 | Platform-provided; read-only at runtime                                             |
| `/opt/fwdnldstatus.txt`            | Persistent firmware download status record (method, protocol, download state, reboot flag, failure reason, version, downloaded filename, URL, last run, firmware-update state, and delay-download setting) | Written by `updateFWDownloadStatus()` on each state change                          |
| `/opt/maintenance_mgr_record.conf` | Maintenance-manager record containing the `softwareoptout` entry (`IGNORE_UPDATE`, `ENFORCE_OPTOUT`, or `BYPASS_OPTOUT`)                                                                                   | Platform-managed; read by `getOPTOUTValue()` and normalized by `updateOPTOUTFile()` |

### Runtime Cache Files

These files are stored in `/tmp` and may survive daemon restarts until reboot; the daemon reuses an existing response cache and refreshes it after successful XConf queries. They are not configuration inputs.

| Cache File                        | Purpose                                                                                                                                                            |
| --------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `/tmp/xconf_response_thunder.txt` | Cached raw XConf server HTTP response body; written after a successful XConf query and read by subsequent `DownloadFirmware` calls to avoid re-querying the server |
| `/tmp/xconf_httpcode_thunder.txt` | Cached HTTP response code from the last XConf query; written alongside the response body cache                                                                     |

### RFC Parameters

| Parameter                                                                      | Type   | Default          | Description                                                                                              |
| ------------------------------------------------------------------------------ | ------ | ---------------- | -------------------------------------------------------------------------------------------------------- |
| `Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.SWDLSpLimit.Enable`            | bool   | `false`          | Enables download speed throttling                                                                        |
| `Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.SWDLSpLimit.TopSpeed`          | uint   | platform-defined | Maximum throttled download speed in bytes/second; `0` stops or rejects a throttled download              |
| `Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.IncrementalCDL.Enable`         | bool   | `false`          | Enables HTTP-range resumption of partial firmware downloads when a partial image and valid `.header` file exist |
| `Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.MTLS.mTlsXConfDownload.Enable` | bool   | build-dependent  | Controls mutual TLS when `MTLS_DEFAULT_DISABLE` is defined; otherwise mTLS is forced on                  |
| `Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.SWDLDirect.Enable`             | bool   | platform-defined | Enables direct CDN downloads                                                                             |
| `Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.FWUpdate.AutoExcluded.Enable`  | bool   | `false`          | Enables the auto-exclude firmware update opt-out mechanism                                               |
| `Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.RedRecovery.Status`            | string | platform-managed | Records state-red recovery lifecycle (`STARTED`, `DOWNLOADED`, `PROGRAMMED`, `COMPLETED`, or `DISABLED`) |

> **Runtime vs startup:** `getRFCSettings()` runs at daemon startup and again in XConf/download workers. The `IARM_BUS_RDKVFWUPGRADER_MODECHANGED` handler uses the cached global `rfc_list.rfc_topspeed`; it does not re-read `SWDLSpLimit.TopSpeed`, so changing that RFC alone does not take effect on the next mode transition.

---

## HAL / Library APIs Integration

Consolidated reference of all HAL and external library APIs called by the daemon.

| API                         | Purpose                                                                                                   | Defined In / Call Sites                                                                                                                                  |
| --------------------------- | --------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `flashImage()`              | Writes a downloaded firmware image (PCI, PDRI, or peripheral) to the appropriate device storage partition | Implemented in `src/flash.c`                                                                                                                             |
| `doHttpFileDownload()`      | Performs HTTP/HTTPS file download using libcurl with configurable speed limit and mTLS credentials        | External download utility (`downloadUtil.h` / `libdwnlutil`); called from `src/rdkv_upgrade.c`, `src/chunk.c`                                            |
| `chunkDownload()`           | Resumes a partial firmware download using HTTP range requests                                             | Implemented in `src/chunk.c`                                                                                                                             |
| `doInteruptDwnl()`          | Pauses or resumes an active curl download with a new speed cap                                            | External download utility (`downloadUtil.h` / `libdwnlutil`); called from `src/rdkFwupdateMgr.c`, `src/rdkv_main.c`                                      |
| `doStopDownload()`          | Stops and releases an active curl download session                                                        | External download utility (`downloadUtil.h` / `libdwnlutil`); called from `src/rdkFwupdateMgr.c`, `src/rdkv_main.c`, `src/rdkv_upgrade.c`, `src/chunk.c` |
| `doGetDwnlBytes()`          | Reads the number of bytes downloaded so far from the active curl handle                                   | External download utility (`downloadUtil.h` / `libdwnlutil`); called from `src/rdkFwupdateMgr.c`, `src/rdkv_main.c`                                      |
| `getDeviceProperties()`     | Reads device model, partner ID, serial number, and firmware storage path from the platform                | External library (`rdk_fwdl_utils.h` / `librdksw_fwutils`); called from `src/rdkFwupdateMgr.c`, `src/rdkv_main.c`, `src/dbus/rdkv_dbus_server.c`         |
| `getImageDetails()`         | Reads the currently running firmware version and image name                                               | External library (`rdk_fwdl_utils.h` / `librdksw_fwutils`); called from `src/rdkFwupdateMgr.c`, `src/rdkv_main.c`                                        |
| `IARM_Bus_BroadcastEvent()` | Publishes firmware download and state events to platform event subscribers                                | `src/iarmInterface/iarmInterface.c`                                                                                                                      |
| `IARM_Bus_Call()`           | Sends peripheral firmware update notification to the control manager                                      | `src/iarmInterface/iarmInterface.c`                                                                                                                      |
