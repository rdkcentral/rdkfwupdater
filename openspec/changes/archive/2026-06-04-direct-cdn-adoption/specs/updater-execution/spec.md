## ADDED Requirements

### Requirement: checkTriggerUpgrade supports per-artifact mode
The `checkTriggerUpgrade()` function SHALL accept an `upgrade_type` parameter that specifies whether to process all artifact types (legacy) or a single specific artifact type (per-artifact mode).

#### Scenario: Legacy mode (upgrade_type = 0)
- **WHEN** `checkTriggerUpgrade(response, model, 0)` is called
- **THEN** the function SHALL process all available artifact types sequentially (PCI → PDRI → Peripheral) using existing behavior

#### Scenario: Per-artifact mode (upgrade_type = PCI_UPGRADE)
- **WHEN** `checkTriggerUpgrade(response, model, PCI_UPGRADE)` is called
- **THEN** the function SHALL process only the PCI firmware download using `response->firmwareUrl`

#### Scenario: Per-artifact mode (upgrade_type = PDRI_UPGRADE)
- **WHEN** `checkTriggerUpgrade(response, model, PDRI_UPGRADE)` is called
- **THEN** the function SHALL process only the PDRI firmware download using `response->pdriUrl`

#### Scenario: Per-artifact mode (upgrade_type = PERIPHERAL_UPGRADE)
- **WHEN** `checkTriggerUpgrade(response, model, PERIPHERAL_UPGRADE)` is called
- **THEN** the function SHALL process only the peripheral firmware download using `response->remCtrlUrl`

### Requirement: One-shot branching on Direct CDN
The one-shot binary `rdkvfwupgrader` SHALL branch its firmware update pipeline based on `isDirectCDNEnabled()` after initial validation.

#### Scenario: Direct CDN path replaces legacy orchestration
- **WHEN** initial validation passes and `isDirectCDNEnabled()` returns true
- **THEN** the one-shot binary SHALL call `DirectCDNDownload()` instead of the legacy `MakeXconfComms()` → `processJsonResponse()` → `checkTriggerUpgrade(..., 0)` sequence

#### Scenario: Legacy path preserved when disabled
- **WHEN** initial validation passes and `isDirectCDNEnabled()` returns false
- **THEN** the one-shot binary SHALL execute the existing legacy path unchanged: `MakeXconfComms()` → `processJsonResponse()` → `checkTriggerUpgrade(response, model, 0)`

### Requirement: DirectCDNDownload is a new source file
The `DirectCDNDownload()` orchestrator SHALL be implemented in a separate source file `src/directcdn.c` and linked into the `rdkvfwupgrader` binary via `Makefile.am`.

#### Scenario: Build system includes directcdn.c
- **WHEN** the project is compiled
- **THEN** `src/directcdn.c` SHALL be included in `rdkvfwupgrader_SOURCES` in `Makefile.am`

#### Scenario: Function callable from one-shot and daemon
- **WHEN** `DirectCDNDownload()` is declared in a header
- **THEN** both the one-shot binary and daemon binary SHALL be able to call it
