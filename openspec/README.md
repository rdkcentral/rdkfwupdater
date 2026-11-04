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

## Detailed Reference Tables

| Topic                                                               | Location                                                            |
| ------------------------------------------------------------------ | ------------------------------------------------------------------- |
| Component interaction matrix                                       | [project.md §9](project.md#9-component-interaction-matrix)          |
| IARM / D-Bus events published                                      | [project.md §10](project.md#10-events-published)                    |
| Configuration files, runtime cache files, RFC parameters           | [project.md §11](project.md#11-configuration-reference)             |
| HAL / library APIs                                                 | [project.md §12](project.md#12-hal--library-apis-integration)       |

