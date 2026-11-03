# Subsystem Specification: firmware-nomenclature

> **Subsystem:** RDKE Firmware Artifact Naming
> **Type:** Metadata / Release Contract
> **Scope:** All RDKE firmware artifacts produced for product, vendor, and middleware integration builds
> **Status:** Proposed specification

---

## 1. Purpose

This specification defines the canonical naming convention for RDKE firmware images so that artifacts are uniquely identifiable, sortable, and compatible across release, integration, and layered RDKE build flows.

The naming scheme must be consistent across:
- Product release builds
- Regular product builds (daily/nightly)
- Signed and unsigned artifacts

---

## 2. Build Types

### 2.1 PROD Builds

`PROD` indicates a production release build intended for shipping or deployment in a production environment.

- Product release artifacts MUST use the release-build naming pattern.
- Signed artifacts MUST use the `-signed` indicator.
- Unsigned production artifacts are not the default path, but if allowed they MUST be clearly identifiable and not confused with signed artifacts.

### 2.2 Development / Integration (DEV) Builds

`DEV` indicates a development or integration build used for validation, staging, or engineering testing.

- DEV builds MUST be distinguishable from release builds by the `DEV` deploy-type token.
- DEV builds MAY be created from `develop`, `hotfix`, or other working branches.
- DEV artifacts MUST carry a UTC timestamp to preserve uniqueness across daily or nightly builds.

---

## 3. Naming Conventions

> **Definition:** `T&PD` refers to the target platform / product development model identifier used by the RDKE platform team to identify the device model or platform variant.

The following fields apply across the naming grammar for RDKE firmware images:

#### Field definitions
- `model#`: This is the T&PD model number
- `Version`: Semantic versioning in `X.Y.Z` format, for example `1.0.0`
- `deploytype`: `DEV` or `PROD`
- `branch`: branch identifier used to distinguish `develop`, `hotfix`, or other release tracks
- `layer`: Optional RDKE layer token for layered builds only: `VENDOR`, `MIDDLEWARE`, `APPLICATION`, or `FULLSTACK` for assembler-style integration artifacts; omitted for direct model builds
- `timestamp`: UTC timestamp in the format `YEARMMDDHHMNSS`
- `signed/not-signed`: `-signed` for signed release images; for current DEV build variants, the explicit signed marker is not always used in the observed naming pattern
- `filetype`: `tgz` for tarball, `bin` for monolith image

### 3.1 Product Release Builds

Product release builds follow this pattern:

`<model#>_<Version>_<deploytype><signed/not-signed>.<filetype>`

#### Examples
- `RPI4B_4.8.3_PROD-signed.bin`
- `RPI4B_4.8.3_PROD-signed.tgz`
- `RPI4B_4.8.3_DEV-signed.bin`

> Canonical rule: signed release artifacts include `-signed` immediately before the file extension.

### 3.2 Regular Build (Product / DEV)

Regular non-release daily/nightly builds follow the pattern below for the currently observed RDKE naming convention:

`<model#>[_<layer>]_<_deploytype>_<branch>_<timestamp>`

Notes:
- `layer` is optional and appears only for layered builds such as `VENDOR`, `MIDDLEWARE`, `APPLICATION`, or `FULLSTACK`.
- Direct model or assembler builds may omit the `layer` token entirely.

#### Timestamp rules
- `YEAR` = year
- `MM` = month
- `DD` = day
- `HH` = hour
- `MN` = minute
- `SS` = seconds
- Timestamp is in UTC

#### Examples
- `RPI4B_MIDDLEWARE_DEV_develop_20260916070903`
- `RPI4B_FULLSTACK_DEV_develop_20261103160449`
- `RPI4B_VENDOR_DEV_develop_20261103160449`
- `RPI4B_DEV_develop_20261029225222`
- `RPI4B_DEV_develop_20261021072552`

> The signed marker is not required in the current DEV naming convention shown by the observed build artifacts.

### 3.3 Vendor Image

Vendor images are a combination of kernel, bootloader, and other vendor components for middleware integration. They follow this naming convention:

`<model#>_VENDOR_<deploytype>_<branch>_<timestamp>`

#### Field definitions
- `model#`: This is the T&PD model number
- `layer`: `VENDOR`
- `deploytype`: `DEV` or `PROD`
- `branch`: build branch such as `develop`
- `timestamp`: UTC timestamp in the format `YEARMMDDHHMNSS`

#### Examples
- `RPI4B_VENDOR_DEV_develop_20261103160449`

> Vendor images are a distinct artifact class and MUST NOT be conflated with product release or direct model builds.

### 3.4 Middleware Image

Middleware images are a combination of vendor images and middleware components for application-layer integration. They follow this naming convention:

`<model#>_MIDDLEWARE_<deploytype>_<branch>_<timestamp>`

#### Field definitions
- `model#`: This is the T&PD model number
- `layer`: `MIDDLEWARE`
- `deploytype`: `DEV` or `PROD`
- `branch`: build branch such as `develop`
- `timestamp`: UTC timestamp in the format `YEARMMDDHHMNSS`

#### Examples
- `RPI4B_MIDDLEWARE_DEV_develop_20260916070903`
- `RPI4B_MIDDLEWARE_DEV_develop_20261029231609`

> Middleware images are a distinct integration artifact class and MUST use the `_MIDDLEWARE_` token in the filename when the build is a middleware-layer artifact.

---

## 4. Normalized Rules

The following rules apply to all RDKE firmware image names:

1. `model#` MUST be the T&PD model number.
2. `Version` MUST use semantic versioning: `X.Y.Z` for release artifacts.
3. `deploytype` MUST be uppercase: `DEV` or `PROD`.
4. `branch` MUST identify the source branch, such as `main`, `develop`, or `hotfix`.
5. `layer` is optional and used only for layered builds; it MUST be omitted for direct model / assembler builds.
6. `timestamp` MUST be UTC and formatted as `YYYYMMDDHHMNSS`.
7. `filetype` MUST be one of the supported values when a file extension is used:
   - `bin` for monolith images
   - `tgz` for tarball images
8. Signed release images MUST be clearly marked with `-signed` for release builds.
9. The current DEV naming convention may omit explicit signed markers and instead rely on the UTC timestamp and branch metadata for uniqueness.
10. Vendor artifacts for layered builds follow `<model#>_VENDOR_<deploytype>_<branch>_<timestamp>`.
11. Middleware artifacts for layered builds follow `<model#>_MIDDLEWARE_<deploytype>_<branch>_<timestamp>`.

---

## 5. Canonical Examples

| Artifact Type | Example |
|---|---|
| Product release | `RPI4B_4.8.3_PROD-signed.bin` |
| Product release tarball | `RPI4B_4.8.3_PROD-signed.tgz` |
| Dev release | `RPI4B_4.8.3_DEV-signed.bin` |
| Layered DEV middleware | `RPI4B_MIDDLEWARE_DEV_develop_20260916070903` |
| Layered DEV vendor | `RPI4B_VENDOR_DEV_develop_20261103160449` |
| Layered DEV fullstack | `RPI4B_FULLSTACK_DEV_develop_20261103160449` |
| Direct DEV build | `RPI4B_DEV_develop_20261029225222` |
| Direct DEV model build | `RPI4B_DEV_develop_20261021072552` |

---

## 6. Validation Requirements

Implementation and tooling validating filenames MUST enforce the following behavior:

- Only uppercase `DEV` and `PROD` values are valid deployment types.
- Release builds MUST include a semantic version in `X.Y.Z` format.
- Regular build images MUST include a UTC timestamp in `YYYYMMDDHHMNSS` format.
- Layered builds MAY include `VENDOR`, `MIDDLEWARE`, `APPLICATION`, or `FULLSTACK` as the optional layer token.
- Direct model or assembler builds MAY omit the layer token entirely.
- Vendor images MUST match `<model#>_VENDOR_<deploytype>_<branch>_<timestamp>` when the layer token is present.
- Middleware images MUST match `<model#>_MIDDLEWARE_<deploytype>_<branch>_<timestamp>` when the layer token is present.
- Signed release images MUST use the `-signed` marker immediately before the file extension.
- DEV naming does not require an explicit signed marker in the observed artifact naming convention.

---

## 7. Compatibility Notes

This naming standard is intended to support both human inspection and automated artifact processing in packaging and update pipelines. The format provides a clear separation between:
- release vs. daily/nightly build lineage,
- production vs. development/integration build intent,
- direct model builds vs. layered vendor/middleware/fullstack build variants,
- signed release images vs. DEV pipeline artifacts without explicit signed markers.
