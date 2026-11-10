## ADDED Requirements

### Requirement: PDRI filename normalization in per-artifact mode
The per-artifact download path for `PDRI_UPGRADE` in `checkTriggerUpgrade()` SHALL normalize the PDRI filename by appending `.bin` suffix if not already present, before constructing the local save path. This ensures the local filesystem filename is consistent regardless of whether the XConf response includes the `.bin` extension.

#### Scenario: PDRI filename without .bin gets normalized
- **WHEN** `checkTriggerUpgrade()` processes `PDRI_UPGRADE` and `pResponse->cloudPDRIVersion` does NOT contain `.bin`
- **THEN** the local download path SHALL be constructed as `<difw_path>/<cloudPDRIVersion>.bin`

#### Scenario: PDRI filename with .bin used as-is
- **WHEN** `checkTriggerUpgrade()` processes `PDRI_UPGRADE` and `pResponse->cloudPDRIVersion` already contains `.bin`
- **THEN** the local download path SHALL be constructed as `<difw_path>/<cloudPDRIVersion>` without additional suffix

#### Scenario: Normalization applies only to local save path
- **WHEN** PDRI `.bin` normalization is applied
- **THEN** it SHALL only affect the local filesystem path (`dwlpath_filename`) and SHALL NOT modify the download URL (`artifact_url` / `pResponse->pdriUrl`)

#### Scenario: Normalization does not apply to PCI or Peripheral artifacts
- **WHEN** `checkTriggerUpgrade()` processes `PCI_UPGRADE` or `PERIPHERAL_UPGRADE`
- **THEN** no `.bin` normalization SHALL be applied (only PDRI requires this)
