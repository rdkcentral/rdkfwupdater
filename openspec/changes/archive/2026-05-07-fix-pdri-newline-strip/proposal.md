## Why

The `GetPDRIFileNameUsingMFR()` function retrieves the PDRI image name from the MFR (Manufacturing) layer via IARM bus call. The MFR layer may return the image name with a trailing newline character (`\n` or `\r\n`), which is copied verbatim into the output buffer. This causes string comparison failures in `checkPDRIUpgrade()` (e.g., `"image_pdri\n"` vs `"image_pdri"` → mismatch), potentially triggering unnecessary PDRI downloads or causing upgrade validation to fail silently.

## What Changes

- Strip trailing newline/carriage-return characters from the PDRI image name immediately after retrieval in `GetPDRIFileNameUsingMFR()`.
- This ensures all downstream consumers (`GetPDRIVersion()`, `checkPDRIUpgrade()`, `GetAdditionalFwVerInfo()`) receive clean data without needing individual sanitization.

## Capabilities

### New Capabilities

_None_ — this is a bug fix within existing capability boundaries.

### Modified Capabilities

- `iarmInterface`: The `GetPDRIFileNameUsingMFR()` function will now strip trailing newline/carriage-return characters from the MFR buffer before returning, ensuring callers always receive a clean filename string.

## Impact

- **Code**: `src/iarmInterface/iarmInterface.c` — `GetPDRIFileNameUsingMFR()` function (both IARM-enabled and stub paths).
- **APIs**: No public API signature changes. Return value (`len`) will reflect the trimmed length.
- **Consumers**: `GetPDRIFileName()` → `GetPDRIVersion()` → `checkPDRIUpgrade()` all benefit without modification.
- **Risk**: Minimal — trailing `\n`/`\r` in a firmware filename is never valid. The fix is additive sanitization.
- **Testing**: Unit test for `GetPDRIFileNameUsingMFR` should cover inputs with trailing newline.
