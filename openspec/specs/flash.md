# Module Spec: flash (Firmware Flash)

## Identity

- **Module**: `flash`
- **Source**: `src/flash.c`, `src/download_status_helper.c`
- **Header**: `src/include/flash.h`
- **Library**: `librdksw_flash.la`
- **Type**: Shared library

## Purpose

Handles writing downloaded firmware images to flash memory and performing post-flash operations (verification, status update, reboot trigger).

## Public API

```c
// Flash firmware image to device memory
int flashImage(const char *server_url, const char *upgrade_file,
               const char *reboot_flag, const char *proto,
               int upgrade_type, const char *maint, int trigger_type);

// Post-flash operations (verify, update status, trigger reboot)
int postFlash(const char *maint, const char *upgrade_file,
              int upgrade_type, const char *reboot_flag, int trigger_type);
```

## Responsibilities

1. Validate downloaded firmware file integrity
2. Write firmware image to appropriate flash partition
3. Track flash progress for status reporting
4. Execute post-flash verification
5. Update firmware download/flash status files
6. Trigger device reboot if `rebootImmediately` flag is set
7. Handle different upgrade types (PCI, PDRI, Peripheral)

## Upgrade Types Handled

| Type | Constant | Flash Target |
|------|----------|-------------|
| PCI | `PCI_UPGRADE` | Primary code image partition |
| PDRI | `PDRI_UPGRADE` | PDRI partition |
| XConf | `XCONF_UPGRADE` | Standard XConf-directed |
| Peripheral | `PERIPHERAL_UPGRADE` | Peripheral device flash |

## Status Files

- `CDL_FLASHED_IMAGE` (`/opt/cdl_flashed_file_name`) — Last flashed image
- `PREVIOUS_FLASHED_IMAGE` (`/opt/previous_flashed_file_name`) — Previous image
- `CURRENTLY_RUNNING_IMAGE` (`/tmp/currently_running_image_name`) — Current running

## Dependencies

- `libsecure_wrapper` — Secure flash command execution
- `download_status_helper` — Progress reporting
- `device_status_helper` — Device state queries

## Test Coverage

- Tested via `unittest/basic_rdkv_main_gtest.cpp` (flash logic with mocked I/O)
