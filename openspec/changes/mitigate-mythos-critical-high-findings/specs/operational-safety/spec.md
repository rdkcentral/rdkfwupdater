## ADDED Requirements

### Requirement: Canary decisions consume authenticated update state
For RDKFW-002 flashImage and postFlash MUST NOT allow a lower-trust local file to select Canary notification or reboot behavior. Both reads SHALL use a consistent trusted-producer policy and reject symlinks, nonregular files, unauthorized ownership, untrusted write permissions, and malformed/oversized values. Rejected or missing state SHALL follow an explicitly owner-approved safe default. Trusted valid state SHALL preserve approved maintenance and reboot behavior.

#### Scenario: Attacker-created Canary state
- **WHEN** a local lower-trust actor creates or replaces xconfchecknow_val with CANARY
- **THEN** the updater rejects the untrusted state and does not select the Canary behavior because of that content

#### Scenario: Blocking or symlink input
- **WHEN** the state path is a FIFO, other nonregular file, or symlink to another file
- **THEN** the read fails safely without blocking the updater or reading the target

#### Scenario: Trusted legitimate Canary upgrade
- **WHEN** authenticated valid Canary state is supplied for the approved trigger and maintenance conditions
- **THEN** the intended PowerManager-dependent notifications and reboot policy remain functional

### Requirement: Firmware transport cannot downgrade from HTTPS
For RDKFW-007 firmware downloads originating through DownloadFirmware MUST use parsed valid HTTPS URLs, including effective URLs resolved from XConf/cache. The actual transport and applicable fallback paths MUST permit only HTTPS redirects, MUST retain TLS peer/host verification, and MUST preserve supported signed CDN query strings and credentials without leakage. Initial prefix validation alone MUST NOT be considered downgrade prevention.

#### Scenario: Cleartext custom firmware URL
- **WHEN** a client submits an http URL for a firmware download
- **THEN** the request is rejected before transfer starts

#### Scenario: HTTPS redirect to HTTP
- **WHEN** the controlled HTTPS endpoint redirects a firmware transfer to HTTP
- **THEN** the transport refuses the downgrade and makes no HTTP request to retrieve the firmware body

#### Scenario: Effective cached cleartext URL
- **WHEN** an empty custom URL resolves to an HTTP location through XConf or its cache
- **THEN** the operation fails before transfer rather than bypassing HTTPS policy

#### Scenario: Valid secure signed download
- **WHEN** an authorized caller requests a supported HTTPS signed CDN URL with valid certificates and query tokens
- **THEN** the transfer preserves those tokens, succeeds normally, and does not weaken TLS verification