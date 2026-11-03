## ADDED Requirements

### Requirement: Firmware download destinations are confined
For RDKFW-001 the daemon SHALL accept only valid single-component firmware filenames and SHALL construct download/cache destinations under the trusted configured DIFW_PATH. It MUST reject unsafe names or truncated destinations before cache lookup, task admission, transfer, or deletion. It MUST NOT treat model matching or path existence alone as containment validation.

#### Scenario: Traversal request rejected before side effects
- **WHEN** DownloadFirmware receives a filename containing path separators, traversal components, an absolute path, or a destination length that cannot fit
- **THEN** it returns an existing appropriate failure response without starting transfer, creating a destination, or deleting a file outside trusted storage

#### Scenario: Valid configured-directory request
- **WHEN** an authorized client requests a valid basename and the trusted firmware storage directory differs from /opt/CDL
- **THEN** cache and download paths use the same configured directory and normal download behavior is preserved

### Requirement: Firmware operations require authorized sender ownership
For RDKFW-006 the daemon MUST authenticate firmware-mutating DownloadFirmware and UpdateFirmware callers using credentials/policy associated with the actual D-Bus sender and an owner-approved allowed-principal policy. Registration alone MUST NOT grant privileged firmware-operation access. The supplied full-width handler ID MUST belong to that sender. Authorization or credential lookup failure SHALL fail closed before transfer or flash task creation.

#### Scenario: Unauthorized caller owns a registration
- **WHEN** a peer outside the allowed-principal policy registers its own process and requests DownloadFirmware or UpdateFirmware
- **THEN** the mutating operation is denied and no download or flash operation starts

#### Scenario: Authorized caller uses another sender's handler
- **WHEN** an otherwise authorized sender submits a handler ID registered to a different unique D-Bus sender
- **THEN** the request is denied without modifying that sender's task or registration

#### Scenario: Authorized owner requests a valid operation
- **WHEN** an allowed sender supplies its own valid registration, a permitted artifact path, and supported parameters
- **THEN** existing asynchronous success/progress behavior is preserved

#### Scenario: Malformed handler identity
- **WHEN** handler text contains overflow, signs, trailing characters, or an ID that would alias another registration through narrowing
- **THEN** the request is rejected rather than resolved to a different handler

### Requirement: Firmware flash paths are constrained before work starts
For RDKFW-006 the daemon MUST reject caller-controlled firmware locations outside trusted storage and unsafe firmware names before invoking the flash worker. It SHALL preserve validated file identity or establish a trusted-directory/file-open invariant so symlink following or replacement cannot redirect privileged flash or cleanup to an unintended file.

#### Scenario: Unapproved custom flash location
- **WHEN** UpdateFirmware supplies an existing directory outside the approved storage location
- **THEN** the daemon returns failure without invoking the flasher or unlinking the supplied file

#### Scenario: Symlink artifact escape
- **WHEN** a destination or candidate flash artifact resolves through an untrusted symlink to an outside file
- **THEN** the operation is rejected at a safe file-use boundary without accessing or deleting the outside file