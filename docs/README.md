# rdkfwupdater — OpenSpec Documentation

> **Repository:** `rdkcentral/rdkfwupdater`  
> **Purpose:** Specification, architecture, and design documentation for the RDK Firmware Update Manager  
> **Status:** Active — code-verified baseline

This folder contains all architectural specifications, runtime documentation, and design records for the `rdkfwupdater` project. It covers both production binaries (`rdkFwupdateMgr` daemon and `rdkvfwupgrader` one-shot updater) and the client-facing shared library (`librdkFwupdateMgr.so`).

---

## Quick Navigation

| Document                                     | What It Covers                                                                                  |
| -------------------------------------------- | ----------------------------------------------------------------------------------------------- |
| [project.md](project.md)                     | Baseline architecture overview — system purpose, artifact inventory, subsystem responsibilities |
| [gaps-and-unknowns.md](gaps-and-unknowns.md) | Known unknowns, inferences, and assumptions requiring manual validation                         |

---

## Document Structure

### `specs/` — Subsystem Specifications

Behavioral contracts and API specifications per subsystem. Each folder contains a `spec.md`.

| Spec                                                                   | Subsystem                                                       | Priority |
| ---------------------------------------------------------------------- | --------------------------------------------------------------- | -------- |
| [specs/client-sdk/spec.md](specs/client-sdk/spec.md)                   | `librdkFwupdateMgr.so` public C API and async callback model    | P0       |
| [specs/dbus-ipc/spec.md](specs/dbus-ipc/spec.md)                       | D-Bus method/signal contracts for `org.rdkfwupdater.Interface`  | P0       |
| [specs/daemon-runtime/spec.md](specs/daemon-runtime/spec.md)           | Daemon lifecycle state machine (`STATE_INIT` → `STATE_IDLE`)    | P0       |
| [specs/download-engine/spec.md](specs/download-engine/spec.md)         | HTTP/HTTPS firmware download engine (libcurl, throttling, mTLS) | P0       |
| [specs/direct-cdn-download/spec.md](specs/direct-cdn-download/spec.md) | Direct CDN download path and CodeBig fallback                   | P1       |
| [specs/firmware-validation/spec.md](specs/firmware-validation/spec.md) | Firmware image validation and version comparison                | P1       |
| [specs/updater-execution/spec.md](specs/updater-execution/spec.md)     | One-shot orchestrator (`rdkvfwupgrader`) behavioral contract    | P1       |
| [specs/operational-safety/spec.md](specs/operational-safety/spec.md)   | Concurrency control, mutex ownership, piggybacking semantics    | P1       |
| [specs/retry-recovery/spec.md](specs/retry-recovery/spec.md)           | Download retry logic and state-red recovery path                | P1       |
| [specs/test-mock-isolation/spec.md](specs/test-mock-isolation/spec.md) | Test isolation boundaries and mock interfaces                   | P2       |

---

### `runtime/` — Runtime Behavior Documentation

Sequence diagrams and lifecycle documentation for both execution models.

| Document                                                                     | What It Covers                                                           |
| ---------------------------------------------------------------------------- | ------------------------------------------------------------------------ |
| [runtime/rdkFwupdateMgr-lifecycle.md](runtime/rdkFwupdateMgr-lifecycle.md)   | Daemon lifecycle: systemd service config, state machine, signal handling |
| [runtime/rdkFwupdateMgr-sequence.md](runtime/rdkFwupdateMgr-sequence.md)     | Daemon request/response sequence diagrams                                |
| [runtime/rdkvfwupgrader-lifecycle.md](runtime/rdkvfwupgrader-lifecycle.md)   | One-shot updater lifecycle and exit code semantics                       |
| [runtime/rdkvfwupgrader-sequence.md](runtime/rdkvfwupgrader-sequence.md)     | One-shot updater sequence diagrams                                       |
| [runtime/client-daemon-interaction.md](runtime/client-daemon-interaction.md) | D-Bus fire-and-forget protocol between client library and daemon         |
| [runtime/daemon-threading-model.md](runtime/daemon-threading-model.md)       | GLib main loop, GTask worker threads, mutex ownership map                |
| [runtime/firmware-update-flows.md](runtime/firmware-update-flows.md)         | End-to-end call flows: CheckForUpdate, DownloadFirmware, UpdateFirmware  |

---

### `dbus/` — D-Bus Interface Reference

| Document                                               | What It Covers                                                                          |
| ------------------------------------------------------ | --------------------------------------------------------------------------------------- |
| [dbus/dbus-architecture.md](dbus/dbus-architecture.md) | Full D-Bus interface introspection XML, method signatures, signal payloads, error codes |

---

### `diagrams/` — Architecture Diagrams

| Document                                                                 | What It Covers                                            |
| ------------------------------------------------------------------------ | --------------------------------------------------------- |
| [diagrams/architecture-diagrams.md](diagrams/architecture-diagrams.md)   | System component diagram, shared library dependency graph |
| [diagrams/subsystem-architecture.md](diagrams/subsystem-architecture.md) | Subsystem interaction diagram and layer boundaries        |

---

### `subsystems/` — Subsystem Analysis

| Document                                                               | What It Covers                                                                 |
| ---------------------------------------------------------------------- | ------------------------------------------------------------------------------ |
| [subsystems/subsystem-inventory.md](subsystems/subsystem-inventory.md) | Full inventory of 17 subsystems with type, scope, and spec priority            |
| [subsystems/subsystem-map.md](subsystems/subsystem-map.md)             | Classification matrix: which subsystems are shared vs execution-model-specific |
| [subsystems/spec-boundaries.md](subsystems/spec-boundaries.md)         | Prioritized spec recommendations (P0–P3 tiers) with rationale                  |
| [subsystems/client-library.md](subsystems/client-library.md)           | Client library architecture: public API surface, background thread model       |

---

### `changes/` — Design Records and Change Proposals

Active and archived design records for tracked changes.

| Entry                                                                                  | Status   | What It Covers                                                                    |
| -------------------------------------------------------------------------------------- | -------- | --------------------------------------------------------------------------------- |
| [changes/fix-state-red-dual-instance-race/](changes/fix-state-red-dual-instance-race/) | Active   | Fix for state-red + dual-instance race condition (proposal, design, specs, tasks) |
| [changes/archive/](changes/archive/)                                                   | Archived | Completed change records (direct-cdn-adoption, parity-guards, L1 segfault fix)    |

---

## How to Update This Documentation

- **If a subsystem behavior changes** → update the relevant `specs/<subsystem>/spec.md`
- **If the D-Bus interface changes** → update `dbus/dbus-architecture.md` and `specs/dbus-ipc/spec.md`
- **If a new design change is planned** → create `changes/<change-name>/` with `proposal.md`, `design.md`, `tasks.md`
- **If a gap is resolved or a new unknown is discovered** → update `gaps-and-unknowns.md`
- **For architecture-level additions** → update `project.md` and the relevant `diagrams/` file

---

## Key Concepts

| Term                    | Meaning                                                                           |
| ----------------------- | --------------------------------------------------------------------------------- |
| `rdkFwupdateMgr`        | Persistent systemd daemon; serves firmware operations via D-Bus                   |
| `rdkvfwupgrader`        | Legacy one-shot CLI binary; invoked by Maintenance Manager                        |
| `librdkFwupdateMgr.so`  | Client shared library; abstracts D-Bus behind a plain C callback API              |
| XConf                   | Cloud configuration server; queried via HTTP POST for firmware availability       |
| CDN                     | Content delivery network hosting firmware images for HTTP/HTTPS download          |
| IARM                    | Inter-Application Resource Manager; platform event bus for RDK-V                  |
| RFC                     | Runtime Feature Configuration; key-value store for tunable parameters             |
| PCI / PDRI / Peripheral | Firmware artifact types: main image / disaster-recovery image / peripheral device |
| D-Bus bus name          | `org.rdkfwupdater.Interface` on the system bus                                    |

---

## Component Interaction Matrix

Full reference of all external systems this component communicates with.

| Target Component / Layer            | Interaction Purpose                                                                       | Key APIs / Topics                                                                                                                                                            |
| ----------------------------------- | ----------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **XConf Server**                    | Firmware availability query — HTTP POST with device identity and current firmware version | `createJsonString()`, `getJsonRpc()`                                                                                                                                         |
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

| Event Name                | IARM / D-Bus Topic                                | Trigger Condition                                                         | Subscriber                            |
| ------------------------- | ------------------------------------------------- | ------------------------------------------------------------------------- | ------------------------------------- |
| `ImageDwldEvent`          | `IARM_BUS_SYSMGR_SYSSTATE_FIRMWARE_DWNLD`         | Firmware download state changes                                           | System Manager subscribers            |
| `FirmwareStateEvent`      | `IARM_BUS_SYSMGR_SYSSTATE_FIRMWARE_UPDATE_STATE`  | Firmware update state changes (started, completed, failed)                | System Manager subscribers            |
| `RedStateEvent`           | `IARM_BUS_SYSMGR_SYSSTATE_RED_RECOV_UPDATE_STATE` | Device enters or exits state-red recovery                                 | System Manager subscribers            |
| Maintenance module status | `IARM_BUS_MAINTENANCEMGR_EVENT_UPDATE`            | Download starts, completes, or errors when Maintenance Manager is enabled | Maintenance Manager                   |
| `CheckForUpdateComplete`  | D-Bus signal on `org.rdkfwupdater.Interface`      | XConf query worker thread completes                                       | `librdkFwupdateMgr` background thread |
| `DownloadProgress`        | D-Bus signal on `org.rdkfwupdater.Interface`      | Download bytes progress, completion, or error                             | `librdkFwupdateMgr` background thread |
| `UpdateProgress`          | D-Bus signal on `org.rdkfwupdater.Interface`      | Flash progress, completion, or error                                      | `librdkFwupdateMgr` background thread |

---

## Configuration Reference

### Key Configuration Files

| File                     | Purpose                                                                                                                            | Override Mechanism                                         |
| ------------------------ | ---------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------- |
| `/etc/device.properties` | Device model, MAC address, partner ID, firmware storage path, build type, and other device identity fields                         | Platform-provided; read-only at runtime                    |
| `/opt/fwdnldstatus.txt`  | Persistent firmware download status record (method, protocol, download state, reboot flag, failure reason, version, URL, last run) | Written by `updateFWDownloadStatus()` on each state change |
| `softwareoptout`         | Opt-out status for firmware updates; contains `IGNORE_UPDATE` or `ENFORCE_OPTOUT` value                                            | Platform-managed; read by `getOPTOUTValue()`               |

### Runtime Cache Files

These files are session-scoped and refreshed by the daemon during normal operation; they are not configuration inputs.

| Cache File                        | Purpose                                                                                                                                                            |
| --------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `/tmp/xconf_response_thunder.txt` | Cached raw XConf server HTTP response body; written after a successful XConf query and read by subsequent `DownloadFirmware` calls to avoid re-querying the server |
| `/tmp/xconf_httpcode_thunder.txt` | Cached HTTP response code from the last XConf query; written alongside the response body cache                                                                     |

### RFC Parameters

| Parameter                                                                      | Type | Default | Description                                                |
| ------------------------------------------------------------------------------ | ---- | ------- | ---------------------------------------------------------- |
| `Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.SWDLSpLimit.Enable`            | bool | `false` | Enables download speed throttling                          |
| `Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.SWDLSpLimit.TopSpeed`          | uint | `0`     | Maximum download speed in bytes/second (0 = unlimited)     |
| `Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.IncrementalCDL.Enable`         | bool | `false` | Enables incremental (delta) firmware download              |
| `Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.MTLS.mTlsXConfDownload.Enable` | bool | `false` | Enables mutual TLS for XConf server communication          |
| `Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.FWUpdate.AutoExcluded.Enable`  | bool | `false` | Enables the auto-exclude firmware update opt-out mechanism |
| `Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.RedRecovery.Status`            | bool | `false` | Enables state-red recovery firmware acquisition path       |

> **Runtime vs startup:** All RFC parameters are read at daemon startup via `getRFCSettings()` and apply for the lifetime of that session — **except** `SWDLSpLimit.TopSpeed`, which is re-read live whenever an `IARM_BUS_RDKVFWUPGRADER_MODECHANGED` event is received. Speed throttle changes therefore take effect on the next foreground/background mode transition without a restart.

---

## HAL / Library APIs Integration

Consolidated reference of all HAL and external library APIs called by the daemon.

| API                         | Purpose                                                                                                   | Implementation File                 |
| --------------------------- | --------------------------------------------------------------------------------------------------------- | ----------------------------------- |
| `flashImage()`              | Writes a downloaded firmware image (PCI, PDRI, or peripheral) to the appropriate device storage partition | `src/flash.c`                       |
| `doHttpFileDownload()`      | Performs HTTP/HTTPS file download using libcurl with configurable speed limit and mTLS credentials        | `src/rdkv_upgrade.c`                |
| `chunkDownload()`           | Resumes a partial firmware download using HTTP range requests                                             | `src/chunk.c`                       |
| `doInteruptDwnl()`          | Pauses or resumes an active curl download with a new speed cap                                            | `src/rdkFwupdateMgr.c`              |
| `doStopDownload()`          | Stops and releases an active curl download session                                                        | `src/rdkFwupdateMgr.c`              |
| `doGetDwnlBytes()`          | Reads the number of bytes downloaded so far from the active curl handle                                   | `src/rdkFwupdateMgr.c`              |
| `getDeviceProperties()`     | Reads device model, partner ID, serial number, and firmware storage path from the platform                | `src/rdkFwupdateMgr.c`              |
| `getImageDetails()`         | Reads the currently running firmware version and image name                                               | `src/rdkFwupdateMgr.c`              |
| `IARM_Bus_BroadcastEvent()` | Publishes firmware download and state events to platform event subscribers                                | `src/iarmInterface/iarmInterface.c` |
| `IARM_Bus_Call()`           | Sends peripheral firmware update notification to the control manager                                      | `src/iarmInterface/iarmInterface.c` |
