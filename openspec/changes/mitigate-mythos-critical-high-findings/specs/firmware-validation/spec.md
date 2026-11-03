## ADDED Requirements

### Requirement: XConf artifact filenames cannot escape firmware storage
For RDKFW-003 both updater execution models SHALL validate externally supplied PCI/PDRI filenames before using them in local paths. Legacy and Direct CDN per-artifact paths MUST enforce single-component names, trusted-directory containment, and destination capacity. PDRI suffix normalization SHALL occur only after validation and SHALL account for the appended suffix and terminator.

#### Scenario: Malicious MockXConf legacy artifact
- **WHEN** a controlled XConf response contains a traversal filename that otherwise matches device model/version eligibility
- **THEN** the legacy updater rejects the artifact before a local outside-directory file write or cleanup

#### Scenario: Direct CDN PDRI normalization
- **WHEN** a valid PDRI filename without the required .bin suffix is received in per-artifact mode
- **THEN** the normalized filename remains within trusted storage and fits the destination without truncation

#### Scenario: Unsafe Direct CDN artifact
- **WHEN** a per-artifact PCI or PDRI filename contains separators or normalization would exceed destination capacity
- **THEN** the artifact is rejected before download without changing a neighboring file

### Requirement: XConf POST construction is bounded and format preserving
For RDKFW-005 createJsonString SHALL construct the existing form-style POST data within the supplied buffer, including every separator and NUL terminator. It MUST handle invalid output arguments, formatting errors, and insufficient capacity without out-of-bounds writes or size subtraction underflow. Successful construction SHALL preserve existing keys, field order, and valid-input representation. Failure SHALL clear usable output and return an explicit result distinguishable from successful length.

#### Scenario: Exact capacity boundary
- **WHEN** the complete POST data plus its NUL terminator fits exactly in the supplied capacity
- **THEN** construction succeeds with actual payload length less than capacity and a terminated output

#### Scenario: Aggregate overflow
- **WHEN** individually bounded device fields collectively exceed output capacity
- **THEN** construction fails without partial output being submitted or memory beyond the buffer being modified

#### Scenario: Invalid output argument
- **WHEN** the caller supplies a NULL buffer or zero capacity
- **THEN** construction returns explicit failure without dereferencing the output

### Requirement: Every XConf caller rejects failed construction
All CLI, manager, Direct CDN, and D-Bus XConf call paths MUST recognize the builder's failure result and MUST NOT submit a failed or partial POST body. Error propagation SHALL preserve daemon liveness and clean up owned temporary allocations.

#### Scenario: Builder failure in legacy orchestration
- **WHEN** request construction fails in MakeXconfComms or the corresponding manager path
- **THEN** no XConf transport starts and the caller reports an error through its existing interface

#### Scenario: Builder failure in shared or asynchronous paths
- **WHEN** construction fails in DirectCDNDownload or a D-Bus XConf worker
- **THEN** the caller releases its owned resources, reports failure, and does not send partial data