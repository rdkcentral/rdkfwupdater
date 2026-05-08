## MODIFIED Requirements

### Requirement: GetPDRIFileNameUsingMFR returns clean filename

The `GetPDRIFileNameUsingMFR()` function SHALL strip trailing newline (`\n`) and carriage-return (`\r`) characters from the PDRI image name buffer before returning to the caller. The returned length SHALL reflect the trimmed string length.

#### Scenario: MFR returns PDRI name with trailing newline
- **WHEN** IARM_Bus_Call returns a PDRI image name with a trailing `\n` character (e.g., `"device_pdri_image.bin\n"`)
- **THEN** the function MUST remove the trailing `\n` and return `"device_pdri_image.bin"` with length excluding the newline

#### Scenario: MFR returns PDRI name with trailing CRLF
- **WHEN** IARM_Bus_Call returns a PDRI image name with trailing `\r\n` characters
- **THEN** the function MUST remove both trailing `\r` and `\n` and return a clean filename string

#### Scenario: MFR returns PDRI name without trailing whitespace
- **WHEN** IARM_Bus_Call returns a PDRI image name with no trailing newline or carriage-return
- **THEN** the function SHALL return the string unchanged with its original length

#### Scenario: MFR returns only newline characters
- **WHEN** IARM_Bus_Call returns a buffer containing only `\n` or `\r\n` with no actual filename
- **THEN** the function SHALL return an empty string with length 0

#### Scenario: Truncated buffer path also strips newline
- **WHEN** the MFR buffer length exceeds the caller's buffer size AND the truncated result ends with `\n` or `\r`
- **THEN** the function MUST still strip trailing newline/CR from the truncated result
