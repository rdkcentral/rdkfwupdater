# CheckForUpdate Opt-Out Integration - Feature Documentation

## Feature Overview

The CheckForUpdate API now supports opt-out enforcement, allowing users to control firmware updates on their devices. When a user opts out of updates, the API will return appropriate status codes with complete firmware metadata, enabling better user experience in client applications.

## What's New

### New Status Codes

Two new status codes have been added to the `CheckForUpdateStatus` enum:

- **IGNORE_OPTOUT (4)**: User has completely opted out of firmware updates (non-critical updates are blocked)
- **BYPASS_OPTOUT (5)**: User requires consent before installation (updates can be downloaded but require approval)

### Key Features

✅ **Always Queries XConf**: Server communication happens regardless of opt-out status  
✅ **Post-XConf Evaluation**: Opt-out logic runs after successful firmware query  
✅ **Full Metadata**: All responses include firmware version and update details  
✅ **Critical Update Override**: Security/stability updates bypass user opt-out  
✅ **Backward Compatible**: Existing clients continue to work without changes  
✅ **Comprehensive Logging**: Detailed logs for debugging and troubleshooting  

## How It Works

### Decision Flow

```
1. Query XConf server (always executes)
   ↓
2. Validate firmware for device compatibility
   ↓
3. Check if firmware version is present
   ↓
4. Evaluate opt-out conditions:
   ├─ Is Maintenance Manager active? (maint_status == "true")
   ├─ Is opt-out feature enabled? (sw_optout == "true")
   ├─ Does user have opt-out preference set?
   └─ Is this a critical update?
   ↓
5. Return appropriate status code with firmware metadata
```

### Configuration Sources

| Variable | File Location | Purpose |
|----------|--------------|---------|
| `maint_status` | `/etc/device.properties` | Enables Maintenance Manager integration |
| `sw_optout` | `/etc/device.properties` | Enables opt-out feature on device |
| `optout` | `/opt/maintenance_mgr_record.conf` | User's opt-out preference |

### User Opt-Out Values

| Value | Name | Meaning | Behavior |
|-------|------|---------|----------|
| `1` | IGNORE_UPDATE | Complete opt-out | Blocks all non-critical updates |
| `0` | ENFORCE_OPTOUT | Consent required | Downloads updates, requires approval to install |
| `-1` | Not set | No preference | Updates proceed normally |

## Client Integration

### Handling New Status Codes

#### Status Code 4: IGNORE_OPTOUT

**Meaning**: User has opted out of firmware updates. Non-critical update is blocked.

**Client Should**:
- ❌ Do NOT download firmware
- ❌ Do NOT show "Update Available" notification
- ✅ Display: "Firmware updates disabled by user"
- ✅ Provide option to change settings

**Example**:
```c
if (response.status_code == IGNORE_OPTOUT) {
    // User has blocked updates
    showSettingsPrompt("Firmware updates are disabled. Would you like to enable them?");
    // Display firmware info from response.available_version
    logInfo("Update blocked by user: Version %s available but opted out", 
            response.available_version);
}
```

#### Status Code 5: BYPASS_OPTOUT

**Meaning**: User requires consent before installation. Update can be downloaded but not auto-installed.

**Client Should**:
- ✅ Download firmware in background (call DownloadFirmware API)
- ❌ Do NOT install automatically
- ✅ Display: "Firmware update ready - approval required"
- ✅ Present consent dialog with options: "Install now?" [Yes] [Later] [Never]

**Example**:
```c
if (response.status_code == BYPASS_OPTOUT) {
    // Download allowed, installation needs consent
    downloadFirmware(response.update_details);
    
    // After download completes
    showConsentDialog(
        "New firmware version %s is ready to install. Install now?",
        response.available_version
    );
    // If user approves → call UpdateFirmware API
    // If user declines → keep downloaded, remind later
}
```

### Complete Example

```c
CheckUpdateResponse response = rdkFwupdateMgr_checkForUpdate(handlerId);

switch (response.status_code) {
    case FIRMWARE_AVAILABLE:  // 0
        showNotification("Firmware update available");
        downloadAndInstall(response);
        break;
        
    case FIRMWARE_NOT_AVAILABLE:  // 1
        updateUI("Up to date");
        break;
        
    case UPDATE_NOT_ALLOWED:  // 2
        logError("Firmware validation failed");
        break;
        
    case FIRMWARE_CHECK_ERROR:  // 3
        showError("Unable to check for updates");
        scheduleRetry();
        break;
        
    case IGNORE_OPTOUT:  // 4 (NEW)
        log("User has opted out - not downloading");
        showSettingsPrompt("Firmware updates disabled. Change settings?");
        // Firmware info available in response.available_version
        break;
        
    case BYPASS_OPTOUT:  // 5 (NEW)
        log("User consent required - downloading");
        downloadFirmware(response);
        showConsentDialog("Firmware ready - install now?");
        break;
        
    default:
        logError("Unknown status code: %d", response.status_code);
}

// Always free response when done
checkupdate_response_free(&response);
```

## Configuration Examples

### Enable Opt-Out Feature

**File**: `/etc/device.properties`

```properties
MAINT_STATUS=true       # Enable Maintenance Manager integration
SW_OPTOUT=true          # Enable opt-out feature
```

### User Opt-Out Preferences

**File**: `/opt/maintenance_mgr_record.conf`

```
# Complete opt-out (blocks all non-critical updates)
softwareoptout=IGNORE_UPDATE

# OR

# Consent required (download OK, installation needs approval)
softwareoptout=ENFORCE_OPTOUT
```

### Disable Opt-Out Feature

To disable opt-out feature entirely, set either:

```properties
# In /etc/device.properties
MAINT_STATUS=false      # Disables Maintenance Manager integration
# OR
SW_OPTOUT=false         # Disables opt-out feature
```

## Testing

### Manual Testing

#### Test Case 1: User Opted Out (Non-Critical Update)

**Setup**:
```bash
# In /etc/device.properties
MAINT_STATUS=true
SW_OPTOUT=true

# In /opt/maintenance_mgr_record.conf
softwareoptout=IGNORE_UPDATE
```

**Expected**: CheckForUpdate returns status code 4 (IGNORE_OPTOUT)

**Verify**:
```bash
# Check logs
tail -f /opt/logs/swupdate.log | grep -E "IGNORE_OPTOUT|opted out"

# Should see:
# "BLOCKING UPDATE: User opted out (IGNORE_UPDATE), non-critical firmware"
# "===== END OPT-OUT EVALUATION: RETURNING IGNORE_OPTOUT ====="
```

---

#### Test Case 2: Consent Required

**Setup**:
```bash
# In /etc/device.properties
MAINT_STATUS=true
SW_OPTOUT=true

# In /opt/maintenance_mgr_record.conf
softwareoptout=ENFORCE_OPTOUT
```

**Expected**: CheckForUpdate returns status code 5 (BYPASS_OPTOUT)

**Verify**:
```bash
# Check logs
tail -f /opt/logs/swupdate.log | grep -E "BYPASS_OPTOUT|CONSENT"

# Should see:
# "CONSENT REQUIRED: User has ENFORCE_OPTOUT set"
# "===== END OPT-OUT EVALUATION: RETURNING BYPASS_OPTOUT ====="
```

---

#### Test Case 3: Critical Update Bypasses Opt-Out

**Setup**:
```bash
# User has opted out
softwareoptout=IGNORE_UPDATE

# Server provides critical update
# (XConf response has cloudImmediateRebootFlag = "true")
```

**Expected**: CheckForUpdate returns status code 0 (FIRMWARE_AVAILABLE)

**Verify**:
```bash
# Check logs
tail -f /opt/logs/swupdate.log | grep -E "CRITICAL|bypass"

# Should see:
# "CRITICAL UPDATE DETECTED (cloudImmediateRebootFlag=true)"
# "CRITICAL UPDATE OVERRIDE: Bypassing user opt-out"
# "===== END OPT-OUT EVALUATION: CRITICAL BYPASS ====="
```

### Debug Commands

#### Check Device Configuration
```bash
# Check if Maintenance Manager is active
grep MAINT_STATUS /etc/device.properties

# Check if opt-out feature is enabled
grep SW_OPTOUT /etc/device.properties
```

#### Check User Preference
```bash
# View current opt-out setting
cat /opt/maintenance_mgr_record.conf | grep softwareoptout
```

#### Monitor Logs
```bash
# Watch opt-out decision making
tail -f /opt/logs/swupdate.log | grep -E "OPT-OUT|optout|maint_status|sw_optout"

# Watch for specific decision points
tail -f /opt/logs/swupdate.log | grep "===== BEGIN POST-XCONF OPT-OUT"
```

## Troubleshooting

### Problem: Opt-Out Not Working (Updates Proceed Despite Setting)

**Possible Causes**:
1. Maintenance Manager not active (`MAINT_STATUS != "true"`)
2. Opt-out feature disabled (`SW_OPTOUT != "true"`)
3. Config file missing or malformed
4. Update is marked as critical (bypasses opt-out)

**Solution**:
```bash
# 1. Verify device properties
grep -E "MAINT_STATUS|SW_OPTOUT" /etc/device.properties
# Should show: MAINT_STATUS=true and SW_OPTOUT=true

# 2. Verify config file exists
ls -la /opt/maintenance_mgr_record.conf

# 3. Verify config file content
cat /opt/maintenance_mgr_record.conf | grep softwareoptout
# Should show: softwareoptout=IGNORE_UPDATE or ENFORCE_OPTOUT

# 4. Check if update is critical
tail /opt/logs/swupdate.log | grep "CRITICAL UPDATE"
```

### Problem: Updates Blocked Unexpectedly

**Possible Causes**:
1. User has set IGNORE_UPDATE preference
2. Config file has incorrect value

**Solution**:
```bash
# Check opt-out value
cat /opt/maintenance_mgr_record.conf | grep softwareoptout

# To allow updates, either:
# Option 1: Remove opt-out preference
rm /opt/maintenance_mgr_record.conf

# Option 2: Change to ENFORCE_OPTOUT (consent mode)
echo "softwareoptout=ENFORCE_OPTOUT" > /opt/maintenance_mgr_record.conf

# Option 3: Disable opt-out feature
# Edit /etc/device.properties:
# SW_OPTOUT=false
```

## Performance Impact

- **Opt-out evaluation overhead**: < 5ms per CheckForUpdate call
- **Network impact**: None (XConf query always happens regardless)
- **Memory impact**: Negligible (single file read, ~100 bytes)
- **CPU impact**: Minimal (string comparisons only)

## Security Considerations

### Critical Updates

Critical updates (security/stability fixes) **always bypass opt-out**:
- Server sets `cloudImmediateRebootFlag = "true"` in XConf response
- Daemon detects critical flag and overrides user preference
- Update proceeds with status code 0 (FIRMWARE_AVAILABLE)
- User is informed via status message

### Config File Security

The opt-out config file (`/opt/maintenance_mgr_record.conf`) should be:
- Readable by firmware updater daemon
- Protected from unauthorized modification
- Located in secure directory (`/opt`)

## API Reference

### Function: rdkFwupdateMgr_checkForUpdate()

**Signature**:
```c
CheckUpdateResponse rdkFwupdateMgr_checkForUpdate(const gchar *handler_id);
```

**Returns**: `CheckUpdateResponse` structure containing:
- `result`: API call result (0=success, 1=fail)
- `status_code`: Firmware status (0-5, see table below)
- `current_img_version`: Currently running firmware version
- `available_version`: Available firmware version (may be empty)
- `update_details`: Pipe-delimited metadata string
- `status_message`: Human-readable status message

**Status Codes**:
| Code | Name | Metadata? | Description |
|------|------|-----------|-------------|
| 0 | FIRMWARE_AVAILABLE | ✅ | Update available, proceed with download |
| 1 | FIRMWARE_NOT_AVAILABLE | ❌ | Already on latest version |
| 2 | UPDATE_NOT_ALLOWED | ❌ | Validation failed (model mismatch) |
| 3 | FIRMWARE_CHECK_ERROR | ❌ | XConf/network error |
| 4 | **IGNORE_OPTOUT** | ✅ | User opted out (non-critical blocked) |
| 5 | **BYPASS_OPTOUT** | ✅ | User consent required |

**Memory Management**: Caller must free response using `checkupdate_response_free()`

## Related Documentation

- **IMPLEMENTATION_SUMMARY.md** - Complete implementation details
- **OPTOUT_DECISION_TABLE.md** - Quick reference decision table
- **PLAN-1.md** - Original design specification (Version 2.0)
- **OPTOUT_MECHANISM.md** - Opt-out mechanism overview

## Support

For questions or issues:
1. Check logs for opt-out evaluation section
2. Verify device properties and config files
3. Review decision table (OPTOUT_DECISION_TABLE.md)
4. Contact RDK firmware update team

---

**Implementation Version**: 1.0  
**Last Updated**: 2025-01-28  
**Status**: ✅ Complete - Ready for Testing
