## ADDED Requirements

### Requirement: Peripheral-version accumulation preserves complete bounded records
For RDKFW-004 both peripheral_firmware_dndl implementations MUST preserve a NUL-terminated list of complete successful name.tgz records within DOWNLOADED_VERS_SIZE. They SHALL reserve separator, suffix, and terminator space before writing and SHALL never advance an offset by a truncated formatting operation's would-have-written length. Rejecting an entry SHALL leave buffer and length unchanged.

#### Scenario: Exact-fit appended record
- **WHEN** a record including separator and suffix uses the last payload byte while leaving room for the NUL terminator
- **THEN** it is accepted with length equal to capacity minus one and no bytes outside the buffer modified

#### Scenario: Insufficient next-record capacity
- **WHEN** a requested record would exceed remaining capacity
- **THEN** the function returns failure before starting that record's download, preserves earlier successful records, and does not write an oversized list

#### Scenario: Failed transfer after reservation
- **WHEN** an entry fits but its transfer fails with a library error or unsuccessful HTTP/curl result
- **THEN** its reserved record is removed, the previous length/terminator is restored, and the failed entry is not persisted or emitted as successful

#### Scenario: Repeated adversarial firmware list
- **WHEN** either executable receives the long peripheral-list vector corresponding to poc_rdkfwupdater-004.sh
- **THEN** it rejects excess records cleanly without heap corruption or reporting a list larger than the allocated capacity