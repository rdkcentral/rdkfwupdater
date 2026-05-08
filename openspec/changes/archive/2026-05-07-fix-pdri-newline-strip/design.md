## Context

`GetPDRIFileNameUsingMFR()` in `src/iarmInterface/iarmInterface.c` retrieves the PDRI image name from the MFR hardware layer via `IARM_Bus_Call`. The raw buffer returned by MFR may contain a trailing newline (`\n` or `\r\n`) — common when the underlying implementation reads from files or `/proc` entries. Currently, the buffer is `memcpy`'d and null-terminated without any whitespace sanitization.

The codebase already has precedent for this: `GetAdditionalFwVerInfo()` in `device_api.c` calls `stripinvalidchar()` after `GetPDRIFileName()` to clean the string before building XConf query parameters. However, `GetPDRIVersion()` in `device_status_helper.c` does NOT call `stripinvalidchar()`, relying on `.bin` suffix truncation to accidentally mask the issue.

## Goals / Non-Goals

**Goals:**
- Ensure `GetPDRIFileNameUsingMFR()` always returns a clean filename without trailing newline/CR characters.
- Fix the bug at the source so all downstream callers benefit without individual sanitization.
- Add unit test coverage for the trailing-newline scenario.

**Non-Goals:**
- Refactoring the broader `stripinvalidchar()` usage across the codebase.
- Changing the `GetPDRIVersion()` or `checkPDRIUpgrade()` logic.
- Handling other whitespace characters (spaces, tabs) — only `\n` and `\r` are targeted.

## Decisions

**Decision 1: Fix at the source (`GetPDRIFileNameUsingMFR`) rather than consumers**

*Rationale*: A trailing newline in a firmware filename is never valid. Fixing at the lowest retrieval point ensures all callers — current and future — get clean data. The alternative (fixing in `GetPDRIVersion()`) would leave `GetPDRIFileName()` returning dirty data for any new consumers.

*Alternatives considered*:
- Fix in `GetPDRIVersion()` only — rejected because it's consumer-side and doesn't protect other callers.
- Use `stripinvalidchar()` — rejected because it strips ALL whitespace/control chars including mid-string ones, which is overly aggressive for a targeted newline fix.

**Decision 2: Strip only trailing `\n` and `\r` using a while loop**

*Rationale*: A simple backward-scanning loop (`while len > 0 && last char is \n or \r`) is minimal, readable, and doesn't depend on external functions. It targets exactly the known problem without side effects.

**Decision 3: Apply the strip in both `memcpy` code paths (normal and truncation)**

*Rationale*: The function has two branches — one for `param.bufLen < szBufSize` and one for truncation. Both must strip, so the stripping code goes after the if/else block, operating on the final `len` value.

## Risks / Trade-offs

- **[Risk: MFR returns only `\n` with no filename]** → After stripping, `len` becomes 0 and `pPDRIFilename[0] == '\0'`. This is correct — caller already handles empty string (logged as "PDRI filename retrieving Failed").
- **[Risk: Legitimate `\r` in filename]** → Firmware filenames never contain carriage returns. This is safe.
- **[Trade-off: Not using existing `stripinvalidchar()`]** → Keeps the fix self-contained within iarmInterface.c, avoids adding a dependency on an external utility function for a 3-line fix.
