# CheckForUpdate Opt-Out Decision Table - Quick Reference

## Overview
This document provides a quick reference for understanding the opt-out decision logic in the `rdkFwupdateMgr_checkForUpdate()` function.

---

## Decision Flow Sequence

```
1. XConf Query (ALWAYS EXECUTES)
   ↓
2. Firmware Validation
   ↓
3. Firmware Version Check
   ↓
4. Post-XConf Opt-Out Evaluation (NEW)
```

---

## Opt-Out Evaluation Decision Table

### Pre-Conditions (Must ALL be true to evaluate opt-out)
| Condition | Check | Action if FALSE |
|-----------|-------|-----------------|
| XConf Success | `ret == 0 && http_code == 200` | Return FIRMWARE_CHECK_ERROR |
| Validation Pass | `processJsonResponse() == 0` | Return UPDATE_NOT_ALLOWED |
| Version Present | `cloudFWVersion[0] != empty` | Return FIRMWARE_NOT_AVAILABLE |

---

### Opt-Out Gating (Early Exit Conditions)

| Gate # | Condition | Check | Result if FALSE |
|--------|-----------|-------|-----------------|
| **Gate 1** | Maintenance Manager Active? | `maint_status == "true"` | Return **FIRMWARE_AVAILABLE** (skip opt-out) |
| **Gate 2** | Opt-Out Feature Enabled? | `sw_optout == "true"` | Return **FIRMWARE_AVAILABLE** (skip opt-out) |
| **Gate 3** | Opt-Out Preference Set? | `optout != -1` | Return **FIRMWARE_AVAILABLE** (no restriction) |

---

### Final Decision Logic (After All Gates Pass)

| optout Value | isCriticalUpdate | Status Code Returned | Action |
|--------------|------------------|---------------------|---------|
| **1** (IGNORE_UPDATE) | **true** | **0** (FIRMWARE_AVAILABLE) | ✅ Critical bypass - update allowed |
| **1** (IGNORE_UPDATE) | **false** | **4** (IGNORE_OPTOUT) | ❌ Blocked - user opted out |
| **0** (ENFORCE_OPTOUT) | _any_ | **5** (BYPASS_OPTOUT) | ⏸️ Consent required - show prompt |
| **-1** (not set) | _any_ | **0** (FIRMWARE_AVAILABLE) | ✅ No restriction - update allowed |

---

## Complete Decision Table with Examples

| # | maint_status | sw_optout | optout | isCritical | → Status Code | → Status Name | Explanation |
|---|--------------|-----------|--------|------------|---------------|---------------|-------------|
| 1 | `"false"` | _any_ | _any_ | _any_ | **0** | FIRMWARE_AVAILABLE | No Maintenance Manager integration |
| 2 | `"true"` | `"false"` | _any_ | _any_ | **0** | FIRMWARE_AVAILABLE | Opt-out feature disabled |
| 3 | `"true"` | `"true"` | **-1** | _any_ | **0** | FIRMWARE_AVAILABLE | No opt-out preference set |
| 4 | `"true"` | `"true"` | **1** | **true** | **0** | FIRMWARE_AVAILABLE | Critical update bypasses opt-out |
| 5 | `"true"` | `"true"` | **1** | **false** | **4** | **IGNORE_OPTOUT** | User blocked, non-critical |
| 6 | `"true"` | `"true"` | **0** | **true** | **5** | **BYPASS_OPTOUT** | Consent required (even if critical) |
| 7 | `"true"` | `"true"` | **0** | **false** | **5** | **BYPASS_OPTOUT** | Consent required |

---

## Status Code Reference

| Code | Name | Meaning | Metadata Included? | Client Action |
|------|------|---------|-------------------|---------------|
| **0** | FIRMWARE_AVAILABLE | Update available - proceed | ✅ Yes | Download and install |
| **1** | FIRMWARE_NOT_AVAILABLE | Already on latest | ❌ No | No action needed |
| **2** | UPDATE_NOT_ALLOWED | Validation failed | ❌ No | Log error |
| **3** | FIRMWARE_CHECK_ERROR | XConf/network error | ❌ No | Retry later |
| **4** | **IGNORE_OPTOUT** | User blocked updates | ✅ **Yes** | Show "Updates disabled" + settings link |
| **5** | **BYPASS_OPTOUT** | Consent required | ✅ **Yes** | Download + prompt user for approval |

---

## Configuration Sources

| Variable | Source | Example Values | Default |
|----------|--------|----------------|---------|
| `maint_status` | `/etc/device.properties` | `"true"`, `"false"`, empty | `"false"` |
| `sw_optout` | `/etc/device.properties` | `"true"`, `"false"`, empty | `"false"` |
| `optout` | `/opt/maintenance_mgr_record.conf` | `1` (IGNORE), `0` (ENFORCE), `-1` (not set) | `-1` |
| `cloudImmediateRebootFlag` | XConf response | `"true"`, `"false"` | `"false"` |

---

## Logging Markers

Look for these log messages to understand the decision path:

```
[rdkFwupdateMgr] ===== BEGIN POST-XCONF OPT-OUT EVALUATION =====
[rdkFwupdateMgr] Checking maint_status: 'true'
[rdkFwupdateMgr] Checking sw_optout: 'true'
[rdkFwupdateMgr] Opt-out value: 1 (-1=not set, 0=ENFORCE_OPTOUT, 1=IGNORE_UPDATE)
[rdkFwupdateMgr] CRITICAL UPDATE DETECTED (cloudImmediateRebootFlag=true)
[rdkFwupdateMgr] ===== END OPT-OUT EVALUATION: [REASON] =====
```

**End reasons**:
- `NORMAL FLOW` - Opt-out skipped or no restriction
- `CRITICAL BYPASS` - Critical update overriding opt-out
- `RETURNING IGNORE_OPTOUT` - Blocking non-critical update
- `RETURNING BYPASS_OPTOUT` - Consent required
- `FALLBACK NORMAL FLOW` - Defensive case (should not occur)

---

## Common Scenarios

### Scenario 1: Device without Maintenance Manager
```
maint_status = "false"  → Gate 1 fails → FIRMWARE_AVAILABLE (0)
```
**Result**: Opt-out logic never runs

---

### Scenario 2: User Opted Out, Non-Critical Update
```
maint_status = "true"
sw_optout = "true"
optout = 1
isCritical = false
→ All gates pass → Decision logic → IGNORE_OPTOUT (4)
```
**Result**: Update blocked, firmware metadata included in response

---

### Scenario 3: User Opted Out, Critical Update
```
maint_status = "true"
sw_optout = "true"
optout = 1
isCritical = true
→ All gates pass → Decision logic → FIRMWARE_AVAILABLE (0)
```
**Result**: Critical update bypasses opt-out, update allowed

---

### Scenario 4: Consent Required
```
maint_status = "true"
sw_optout = "true"
optout = 0
→ All gates pass → Decision logic → BYPASS_OPTOUT (5)
```
**Result**: Download allowed, installation requires user consent

---

### Scenario 5: No Opt-Out Preference Set
```
maint_status = "true"
sw_optout = "true"
optout = -1  → Gate 3 fails → FIRMWARE_AVAILABLE (0)
```
**Result**: No restriction, update allowed

---

## Troubleshooting

### Update Blocked Unexpectedly
1. Check logs for opt-out evaluation section
2. Verify `optout` value from config file
3. Check `cloudImmediateRebootFlag` in XConf response
4. Confirm `maint_status` and `sw_optout` in `/etc/device.properties`

### Opt-Out Not Working
1. Verify `maint_status == "true"` in device.properties
2. Verify `sw_optout == "true"` in device.properties
3. Check if `/opt/maintenance_mgr_record.conf` exists and has correct format
4. Look for "skipping opt-out logic" in logs

### Critical Update Not Bypassing
1. Verify `cloudImmediateRebootFlag == "true"` in XConf response
2. Check logs for "CRITICAL UPDATE DETECTED" message
3. Verify opt-out value is `1` (IGNORE_UPDATE)

---

## Testing Quick Commands

### Check Device Properties
```bash
grep -E 'MAINT_STATUS|SW_OPTOUT' /etc/device.properties
```

### Check Opt-Out Preference
```bash
cat /opt/maintenance_mgr_record.conf | grep softwareoptout
```

### Monitor CheckForUpdate Logs
```bash
tail -f /opt/logs/swupdate.log | grep -E "OPT-OUT|optout|CRITICAL"
```

---

## Reference
- See **IMPLEMENTATION_SUMMARY.md** for complete implementation details
- See **PLAN-1.md** for design rationale
- See **OPTOUT_MECHANISM.md** for opt-out mechanism overview
