## 1. Core Fix

- [x] 1.1 Add trailing newline/CR stripping in `GetPDRIFileNameUsingMFR()` (IARM-enabled path) after both memcpy branches, before `return len`
- [x] 1.2 Add trailing newline/CR stripping in `GetPDRIFileNameUsingMFR()` (stub/non-IARM path) for consistency

## 2. Unit Tests

- [x] 2.1 Add unit test: MFR returns PDRI name with trailing `\n` — verify stripped
- [x] 2.2 Add unit test: MFR returns PDRI name with trailing `\r\n` — verify both stripped
- [x] 2.3 Add unit test: MFR returns PDRI name without trailing whitespace — verify unchanged
- [x] 2.4 Add unit test: MFR returns only `\n` — verify empty string and length 0
