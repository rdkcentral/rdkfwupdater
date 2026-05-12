# Module Spec: deviceutils (Device Utilities)

## Identity

- **Module**: `deviceutils`
- **Source**: `src/deviceutils/deviceutils.c`, `src/deviceutils/device_api.c`
- **Headers**: `src/deviceutils/deviceutils.h`, `src/deviceutils/device_api.h`
- **Library**: `librdksw_fwutils.la`
- **Type**: Shared library

## Purpose

Provides device information retrieval and system command execution. Reads device properties (model, MAC address, partner ID, firmware version) from configuration files and system APIs.

## Responsibilities

1. Read device properties from `/etc/device.properties` and `/version.txt`
2. Retrieve MAC address, serial number, partner ID
3. Execute secure system commands (md5sum, mfr_util, WPEFrameworkSecurityUtility)
4. Build XConf request URL with device parameters
5. Manage TR-181 URL resolution (bootstrap, XConf, DevXConf, recovery)
6. Provide device capability strings

## Key Functions

### deviceutils.c

```c
// Execute predefined system commands securely
size_t RunCommand(SYSCMD eSysCmd, const char *pArgs, char *pResult, size_t szResultSize);
```

### device_api.c

- Build XConf request URLs with device parameters
- Read device properties from various configuration files
- Resolve TR-181 data model URLs
- Provide device capability information

## System Commands (SYSCMD enum)

| Enum | Command |
|------|---------|
| `eMD5Sum` | `/usr/bin/md5sum <file>` |
| `eRdkSsaCli` | Key retrieval |
| `eMfrUtil` | `/usr/bin/mfr_util <args>` |
| `eWpeFrameworkSecurityUtility` | `/usr/bin/WPEFrameworkSecurityUtility` |
| `eGetInstalledRdmManifestVersion` | RDM manifest version query |

## URL Types (TR181URL enum)

| Type | Description |
|------|-------------|
| `eRecovery` | State Red recovery URL |
| `eAutoExclude` | Auto-exclude URL |
| `eBootstrap` | Bootstrap URL from RFC |
| `eDevXconf` | Development XConf URL |
| `eCIXconf` | CI XConf URL |
| `eXconf` | Production XConf URL |
| `eDac15` | DAC15 URL |

## Key Files Read

| Path | Content |
|------|---------|
| `/opt/secure/RFC/bootstrap.ini` | Bootstrap configuration |
| `/opt/www/authService/partnerId3.dat` | Partner ID |
| `/version.txt` | Firmware version info |
| `/tmp/.estb_mac` | eSTB MAC address |

## Dependencies

- `libsecure_wrapper` — Secure command execution
- `libfwutils` — Firmware utility helpers
- `json_parse.h` — JSON structure definitions

## Test Coverage

- `unittest/deviceutils/device_api_gtest.cpp`
- `unittest/deviceutils/deviceutils_gtest.cpp`
