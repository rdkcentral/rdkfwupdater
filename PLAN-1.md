# PLAN-1: Design for OPTOUT Status Integration in CheckForUpdate API

## Document Information
- **Version**: 2.0 (REVISED)
- **Date**: 2025-01-28 (Updated: 2026-02-16)
- **Author**: RDK Firmware Update Team
- **Status**: Design Proposal - Revision 2
- **Target Component**: rdkFwupdateMgr D-Bus API (Daemon Mode)
- **Related Documents**: OPTOUT_MECHANISM.md, XCONF_CACHE_DESIGN.md

---

## 🔄 Revision History

| Version | Date       | Changes                                                           |
|---------|------------|-------------------------------------------------------------------|
| 1.0     | 2025-01-28 | Initial design proposal                                           |
| 2.0     | 2026-02-16 | **CORRECTED**: Removed XConf skip optimization; Added maint_status clarification |

### Key Changes in Version 2.0

**❌ REMOVED (INCORRECT)**:
- ~~"Skip XConf query if opt-out will block update"~~ (Section 1, 3.2)
- ~~Pre-flight optimization to avoid XConf queries~~ (Section 3.3, Phase 1)
- ~~Early exit logic before XConf communication~~ (Section 10.2)

**✅ CORRECTED**:
- **XConf query ALWAYS occurs**, regardless of opt-out status (Section 1, 3.2, 3.3)
- Opt-out logic evaluates **AFTER** XConf response is received (Section 3.3, Phase 3)
- Status codes (`IGNORE_OPTOUT`, `BYPASS_OPTOUT`) returned with full firmware metadata (Section 3.3)
- Added comprehensive `maint_status` clarification section (Section 2.4)
- Updated all flow diagrams to reflect correct behavior (Section 3.2)
- Revised decision tables to include `maint_status` checks (Section 3.3, 5)
- Updated performance analysis to explain why optimization was rejected (Section 10)
- Fixed implementation example in Appendix A to remove pre-flight checks

**📋 KEY CORRECTIONS SUMMARY**:

| Aspect                     | Version 1.0 (INCORRECT)                      | Version 2.0 (CORRECTED)                          |
|----------------------------|----------------------------------------------|--------------------------------------------------|
| XConf Query Timing         | ❌ "Skip if optout=1"                        | ✅ "Always query XConf"                          |
| Opt-Out Check Timing       | ❌ "Pre-flight before XConf"                 | ✅ "Post-XConf after validation"                 |
| Status Return              | ❌ "May return without firmware metadata"    | ✅ "Always includes firmware metadata"           |
| maint_status Documentation | ❌ "Brief mention only"                      | ✅ "Comprehensive section (2.4) with lifecycle"  |
| Performance Optimization   | ❌ "Suggested skipping XConf"                | ✅ "Explained why NOT to skip (Section 10.2)"    |

---

## Executive Summary

This document provides a comprehensive design for extending the `CheckForUpdate` API to support two new firmware status codes: **`IGNORE_OPTOUT`** and **`BYPASS_OPTOUT`**. These status codes will inform clients about opt-out related restrictions **AFTER** querying the XConf server, ensuring clients receive complete firmware metadata even when updates are blocked by user preferences.

### Key Goals
1. Enable CheckForUpdate to detect and report opt-out restrictions with full context
2. **ALWAYS query XConf server** to provide firmware metadata to clients
3. Return appropriate status codes indicating opt-out state with firmware details
4. Maintain backward compatibility with existing clients
5. Preserve critical update override mechanism
6. Provide clear, actionable information to client applications

---

## 1. Problem Statement

### Current Behavior

In the **current CLI implementation** (`rdkv_main.c`), opt-out checking happens **after** the following steps:
1. XConf server is queried for firmware availability
2. Firmware response is validated (model check, version comparison)
3. Download URLs are constructed
4. Only then is the opt-out mechanism checked

**Consequences**:
- Client applications receive no advance warning about opt-out restrictions
- No way for clients to inform users about opt-out status through CheckForUpdate response
- Firmware download may be initiated only to be blocked later in the flow
- CheckForUpdate API doesn't communicate opt-out state to clients

### Expected Behavior (Daemon Mode)

In **daemon mode**, the `CheckForUpdate` API should:
1. **ALWAYS perform XConf query** to obtain firmware metadata
2. Validate firmware after XConf response (model compatibility, version)
3. **After validation**, check opt-out status and critical update flag
4. Return appropriate status codes (`IGNORE_OPTOUT` or `BYPASS_OPTOUT`) **WITH firmware metadata**
5. Still allow critical updates to proceed even if user has opted out
6. Provide clear status messages to guide client UI/behavior

**IMPORTANT**: XConf communication is **NEVER skipped**. The daemon always queries the server to ensure:
- Firmware metadata is available for client decision-making
- Critical update flag (`cloudImmediateRebootFlag`) can be checked
- Clients have complete information regardless of opt-out state

### Problem Scope

**New Status Codes** (already defined in `rdkFwupdateMgr_handlers.h`):
```c
typedef enum {
    FIRMWARE_AVAILABLE = 0,     // New firmware available - proceed
    FIRMWARE_NOT_AVAILABLE = 1, // Already on latest firmware
    UPDATE_NOT_ALLOWED = 2,     // Model mismatch or validation failed
    FIRMWARE_CHECK_ERROR = 3,   // Network or server error
    IGNORE_OPTOUT = 4,          // NEW: User has opted out - updates blocked
    BYPASS_OPTOUT = 5           // NEW: User requires consent - update on hold
} CheckForUpdateStatus;
```

**Issue**: The logic to return these status codes is **not yet implemented** in `rdkFwupdateMgr_checkForUpdate()`.

---

## 2. Current Implementation Analysis

### 2.1 CLI Mode Flow (`rdkv_main.c`)

**Location**: Lines 651-667

```c
// OPTOUT check happens AFTER XConf query and validation
if (0 == strncmp(device_info.maint_status, "true", 4)) {
    // Check if critical update
    if ((strncmp(pResponse->cloudImmediateRebootFlag, "true", 4)) == 0) {
        isCriticalUpdate = true;
    }
    
    // OPTOUT mechanism
    if (0 == strncmp(device_info.sw_optout, "true", 4)) {
        optout = getOPTOUTValue("/opt/maintenance_mgr_record.conf");
        
        if (optout == 1 && isCriticalUpdate != true) {
            // IGNORE_UPDATE case
            SWLOG_INFO("OptOut: IGNORE UPDATE is set.Exiting !!\n");
            eventManager("MaintenanceMGR", MAINT_FWDOWNLOAD_ABORTED);
            uninitialize(INITIAL_VALIDATION_SUCCESS);
            exit(1);
        }
        else if((0 == optout) && (trigger_type != 4)) {
            // ENFORCE_OPTOUT case
            eventManager(FW_STATE_EVENT, FW_STATE_ONHOLD_FOR_OPTOUT);
            SWLOG_INFO("OptOut: Event sent for on hold for OptOut\n");
            eventManager("MaintenanceMGR" ,MAINT_FWDOWNLOAD_COMPLETE);
            SWLOG_INFO("OptOut: Consent Required from User\n");
            t2CountNotify("SYST_INFO_NoConsentFlash", 1);
            uninitialize(INITIAL_VALIDATION_SUCCESS);
            exit(1);
        }
    }
}
```

**Key Observations**:
- Opt-out is checked **after** firmware validation
- Two distinct exit paths: `IGNORE_UPDATE` (abort) vs `ENFORCE_OPTOUT` (on-hold)
- Critical updates bypass all opt-out restrictions
- Events are sent to MaintenanceMGR for state coordination

### 2.2 Daemon CheckForUpdate Implementation

**Location**: `rdkFwupdateMgr_handlers.c`, lines 1139-1272

**Current Flow**:
```
1. Validate handler_id
2. Query XConf server (or load from cache)
3. Validate firmware model compatibility
4. Compare versions
5. Return response with status
```

**Missing**: No opt-out check anywhere in this flow

### 2.3 Key Dependencies

**Required Data**:
- `device_info.maint_status` - Maintenance mode flag (see Section 2.4 for details)
- `device_info.sw_optout` - Feature enable flag
- `optout` value from `/opt/maintenance_mgr_record.conf`
- `cloudImmediateRebootFlag` from XConf response (for critical update detection)

**Functions**:
- `getOPTOUTValue()` - Reads opt-out preference from config file (exists in `rdkFwupdateMgr.c`)
- `processJsonResponse()` - Validates firmware model compatibility
- `fetch_xconf_firmware_info()` - Queries XConf server

### 2.4 Understanding `maint_status` (Maintenance Mode Flag)

#### What is `maint_status`?

**Definition**: `maint_status` is a device property field that indicates whether the device is integrated with an external **Maintenance Manager** component.

**Source**: Read from `/etc/device.properties` via `getDeviceProperties()` call

**Purpose**: Acts as a feature flag to enable/disable integration with MaintenanceMGR component

#### Values and Meaning

| Value    | Meaning                                  | Behavior                                           |
|----------|------------------------------------------|----------------------------------------------------|
| `"true"` | Maintenance Manager integration enabled  | Opt-out mechanism is active; events sent to MaintenanceMGR |
| `"false"` or empty | No Maintenance Manager integration | Opt-out mechanism is **bypassed entirely**        |

#### Lifecycle and Usage

**Initialization** (Both CLI and Daemon):
```c
// Called in initialize() function (rdkv_main.c line 250, rdkFwupdateMgr.c line 307)
int ret = getDeviceProperties(&device_info);
// Populates device_info.maint_status from /etc/device.properties

// Later, check if MaintenanceMGR integration is active:
if (0 == strncmp(device_info.maint_status, "true", 4)) {
    // Query MaintenanceManager for current mode (BACKGROUND/FOREGROUND)
    // Enable opt-out mechanism
    // Send events to MaintenanceMGR
}
```

**Decision Points**:
1. **Opt-Out Activation**: OPTOUT logic only runs when `maint_status == "true"`
2. **Event Emission**: Events like `MAINT_FWDOWNLOAD_ABORTED`, `MAINT_FWDOWNLOAD_COMPLETE` only sent if `maint_status == "true"`
3. **Mode Query**: Daemon queries `MaintenanceManager.1.getMaintenanceMode` to determine if device is in BACKGROUND or FOREGROUND mode (affects download speed/timing)

**In `processJsonResponse()`** (`json_process.c` line 310):
- Used to conditionally send error events to MaintenanceMGR
- Example (line 378-380):
  ```c
  if (0 == strncmp(maint, "true", 4)) {
      eventManager("MaintenanceMGR", MAINT_FWDOWNLOAD_ERROR);
  }
  ```

#### Impact on CheckForUpdate API

**Current Daemon Behavior** (CheckForUpdate handler):
- `device_info.maint_status` is available after initialization
- Currently **NOT** checked in `rdkFwupdateMgr_checkForUpdate()` function
- Passed to `processJsonResponse()` for firmware validation

**Proposed Behavior**:
- CheckForUpdate should check `maint_status` **AFTER** XConf query
- If `maint_status == "true"` AND `sw_optout == "true"` → evaluate opt-out logic
- If `maint_status == "false"` → skip opt-out logic entirely, return normal status

**Why This Matters**:
- Opt-out is a MaintenanceMGR feature → only relevant when MaintenanceMGR is active
- Devices without MaintenanceMGR (`maint_status == "false"`) should never return `IGNORE_OPTOUT` or `BYPASS_OPTOUT`
- Ensures clean separation between standalone firmware updates and maintenance-managed updates

#### Assumptions for This Design

1. **Assumption**: `device_info.maint_status` is populated during daemon initialization and remains stable
2. **Assumption**: Value comes from `/etc/device.properties` which is set during device provisioning
3. **Assumption**: No runtime changes to `maint_status` (no hot-reload of device properties)
4. **Verification Needed**: Confirm global `device_info` structure is accessible in CheckForUpdate handler context

---

## 3. Proposed Solution Design

### 3.1 Design Principles

1. **Always Query XConf**: Never skip XConf communication - firmware metadata is essential
2. **Post-XConf Opt-Out Evaluation**: Check opt-out status AFTER receiving XConf response
3. **Critical Update Priority**: Always respect critical update override
4. **Client Transparency**: Return clear, actionable status codes WITH firmware metadata
5. **Backward Compatibility**: Existing clients continue to work
6. **Separation of Concerns**: Keep business logic separate from D-Bus layer
7. **Maintenance Manager Awareness**: Opt-out only applies when `maint_status == "true"`

### 3.2 Corrected CheckForUpdate Flow

```
┌─────────────────────────────────────────────────────────────────┐
│           rdkFwupdateMgr_checkForUpdate(handler_id)             │
└────────────────────────────┬────────────────────────────────────┘
                             │
                             ▼
                   ┌─────────────────────┐
                   │ Validate handler_id  │
                   └──────────┬───────────┘
                             │
                             ▼
                ┌────────────────────────────────┐
                │ ALWAYS Query XConf Server      │
                │ ────────────────────────────── │
                │ • Call fetch_xconf_firmware_   │
                │   info() (live or cache)       │
                │ • NO early exit                │
                │ • NO opt-out check yet         │
                └──────────┬─────────────────────┘
                           │
                           ▼
                    (XConf Success?)
                           │
              ┌────────────┴────────────┐
              │                         │
          (Success)                 (Failed)
              │                         │
              ▼                         ▼
┌─────────────────────────────┐   ┌──────────────────────────┐
│ Validate XConf Response     │   │ Return FIRMWARE_CHECK_   │
│ ───────────────────────     │   │ ERROR                    │
│ • processJsonResponse()     │   │ • Network error          │
│ • Model compatibility       │   │ • Server error           │
│ • Firmware version check    │   └──────────────────────────┘
└──────────┬──────────────────┘
           │
           ▼
    (Validation Pass?)
           │
     ┌─────┴─────┐
     │           │
  (Pass)      (Fail)
     │           │
     │           ▼
     │    ┌──────────────────────────┐
     │    │ Return UPDATE_NOT_ALLOWED│
     │    │ • Model mismatch         │
     │    └──────────────────────────┘
     │
     ▼
(Firmware Available?)
     │
  ┌──┴──┐
  │     │
 Yes    No
  │     │
  │     ▼
  │  ┌────────────────────────────────┐
  │  │ Return FIRMWARE_NOT_AVAILABLE  │
  │  │ • Already on latest version    │
  │  └────────────────────────────────┘
  │
  ▼
┌──────────────────────────────────────────────────────────────┐
│ NEW: POST-XCONF OPT-OUT EVALUATION                            │
│ ────────────────────────────────────────────────────────────  │
│ NOW we have firmware metadata from XConf!                     │
│                                                                │
│ Step 1: Check Maintenance Manager Integration                 │
│   if (maint_status != "true") → SKIP opt-out, goto Normal     │
│                                                                │
│ Step 2: Check Opt-Out Feature Flag                            │
│   if (sw_optout != "true") → SKIP opt-out, goto Normal        │
│                                                                │
│ Step 3: Read User Preference                                  │
│   optout = getOPTOUTValue("/opt/maintenance_mgr_record.conf") │
│   if (optout == -1) → No preference, goto Normal              │
│                                                                │
│ Step 4: Parse Critical Update Flag from XConf                 │
│   isCriticalUpdate = (cloudImmediateRebootFlag == "true")     │
│                                                                │
│ Step 5: Apply Decision Logic (See Decision Table Below)       │
└────────────────────────┬─────────────────────────────────────┘
                         │
         ┌───────────────┼───────────────┬──────────────┐
         │               │               │              │
      optout=1     optout=1       optout=0       optout=-1
     isCritical   !isCritical                    OR no checks
         │               │               │              │
         ▼               ▼               ▼              ▼
┌──────────────┐ ┌──────────────┐ ┌──────────────┐ ┌──────────────┐
│ Return       │ │ Return       │ │ Return       │ │ Return       │
│ FIRMWARE_    │ │ IGNORE_      │ │ BYPASS_      │ │ FIRMWARE_    │
│ AVAILABLE    │ │ OPTOUT       │ │ OPTOUT       │ │ AVAILABLE    │
│              │ │              │ │              │ │              │
│ Status: 0    │ │ Status: 4    │ │ Status: 5    │ │ Status: 0    │
│ Critical!    │ │ User blocked │ │ Need consent │ │ Normal       │
│ Bypass       │ │ Non-critical │ │ Download OK  │ │              │
│              │ │ NO download  │ │ Install wait │ │              │
└──────────────┘ └──────────────┘ └──────────────┘ └──────────────┘
     │               │               │              │
     └───────────────┴───────────────┴──────────────┘
                         │
                         ▼
              ┌───────────────────────┐
              │ ALL RESPONSES INCLUDE: │
              │ • current_img_version  │
              │ • available_version    │
              │ • update_details       │
              │ • status_message       │
              └───────────────────────┘
```

**Key Points**:
- ✅ XConf query happens **first**, always
- ✅ Opt-out evaluation happens **after** XConf success
- ✅ All responses include firmware metadata
- ✅ Critical updates bypass opt-out
- ✅ Maintenance manager integration checked first

### 3.3 Detailed Logic Flow

#### Phase 1: XConf Query (UNCHANGED - Always Executes)

**Objective**: Obtain firmware metadata from XConf server

**Implementation**:
```c
// ALWAYS query XConf - no early exits
SWLOG_INFO("[rdkFwupdateMgr] Making live XConf call\n");
ret = fetch_xconf_firmware_info(&response, server_type, &http_code);

if (ret != 0 || http_code != 200) {
    // Network or server error
    return create_result_response(FIRMWARE_CHECK_ERROR, 
                                   "Network error - unable to reach update server");
}
```

**Why No Pre-Flight Optimization**:
1. **Critical Flag Unknown**: Cannot determine `cloudImmediateRebootFlag` without XConf response
2. **Client Metadata Needs**: Even blocked updates should provide firmware version information to clients
3. **Simplicity**: Single code path easier to maintain and debug
4. **Telemetry**: XConf queries provide valuable analytics about update availability

#### Phase 2: Firmware Validation (UNCHANGED)

**Standard flow** - No changes to existing logic:
```c
// Validate firmware model compatibility
int validation_result = processJsonResponse(&response, 
                                             cur_img_detail.cur_img_name,
                                             device_info.model,
                                             device_info.maint_status);

if (validation_result != 0) {
    return create_result_response(UPDATE_NOT_ALLOWED, 
                                   "Firmware validation failed");
}

// Check if firmware version is present
if (!response.cloudFWVersion[0] || strlen(response.cloudFWVersion) == 0) {
    return create_result_response(FIRMWARE_NOT_AVAILABLE, 
                                   "No firmware update available");
}
```

#### Phase 3: Post-XConf Opt-Out Enforcement (NEW)

**Objective**: Determine if opt-out restrictions apply AFTER successful XConf query

**Conditions Evaluated in Order**:
```c
// After successful XConf validation and firmware availability confirmed

// Parse critical update flag from XConf response
bool isCriticalUpdate = false;
if (strncmp(response.cloudImmediateRebootFlag, "true", 4) == 0) {
    isCriticalUpdate = true;
    SWLOG_INFO("[CheckForUpdate] CRITICAL UPDATE DETECTED\n");
}

// Check 1: Is Maintenance Manager integration active?
if (strncmp(device_info.maint_status, "true", 4) != 0) {
    // No MaintenanceMGR → No opt-out mechanism
    SWLOG_INFO("[CheckForUpdate] MaintenanceMGR not active - skipping opt-out\n");
    goto normal_flow;  // Return FIRMWARE_AVAILABLE
}

// Check 2: Is opt-out feature enabled for this device?
if (strncmp(device_info.sw_optout, "true", 4) != 0) {
    // Opt-out feature disabled → No opt-out mechanism
    SWLOG_INFO("[CheckForUpdate] Opt-out feature disabled - skipping opt-out\n");
    goto normal_flow;  // Return FIRMWARE_AVAILABLE
}

// Check 3: Read user's opt-out preference
int optout = getOPTOUTValue("/opt/maintenance_mgr_record.conf");
SWLOG_INFO("[CheckForUpdate] Opt-out value: %d\n", optout);

if (optout == -1) {
    // No preference set or file read error → Treat as no restriction
    SWLOG_INFO("[CheckForUpdate] No opt-out preference set\n");
    goto normal_flow;  // Return FIRMWARE_AVAILABLE
}

// Check 4: Apply opt-out decision logic
if (optout == 1) {
    // User has opted out (IGNORE_UPDATE)
    if (isCriticalUpdate) {
        // Critical update bypasses opt-out
        SWLOG_INFO("[CheckForUpdate] CRITICAL UPDATE - Bypassing opt-out\n");
        return create_success_response(response.cloudFWVersion, 
                                        update_details, 
                                        "Critical firmware update available (security/stability)");
    } else {
        // Non-critical update blocked by user
        SWLOG_INFO("[CheckForUpdate] BLOCKING: User opted out, non-critical update\n");
        return create_result_response(IGNORE_OPTOUT, 
                                       "Firmware download blocked - user has opted out of updates");
    }
}
else if (optout == 0) {
    // User requires consent (ENFORCE_OPTOUT)
    SWLOG_INFO("[CheckForUpdate] CONSENT REQUIRED: Returning firmware info with BYPASS_OPTOUT\n");
    
    // Return BYPASS_OPTOUT with full firmware metadata
    return create_optout_response(BYPASS_OPTOUT,
                                   response.cloudFWVersion,
                                   update_details,
                                   "Firmware available - user consent required before installation");
}

normal_flow:
    // Normal flow - no opt-out restrictions
    SWLOG_INFO("[CheckForUpdate] Normal flow - returning FIRMWARE_AVAILABLE\n");
    return create_success_response(response.cloudFWVersion, 
                                    update_details, 
                                    "Firmware update available");
```

**Decision Table**:

| Condition                          | Status Code            | Message                                          | Client Action                          |
|------------------------------------|------------------------|--------------------------------------------------|----------------------------------------|
| optout == -1 (not set)             | FIRMWARE_AVAILABLE (0) | "Firmware update available"                      | Proceed to download                    |
| maint_status == "false"            | FIRMWARE_AVAILABLE (0) | "Firmware update available"                      | Proceed to download                    |
| sw_optout == "false"               | FIRMWARE_AVAILABLE (0) | "Firmware update available"                      | Proceed to download                    |
| optout == 1 && isCriticalUpdate    | FIRMWARE_AVAILABLE (0) | "Critical firmware update available"             | Proceed to download (bypass opt-out)   |
| optout == 1 && !isCriticalUpdate   | **IGNORE_OPTOUT (4)**  | "User has opted out of updates"                  | **Do NOT download - inform user**      |
| optout == 0                        | **BYPASS_OPTOUT (5)**  | "User consent required for installation"         | **Download OK, hold install for approval** |

**Note**: All these decisions happen **AFTER** XConf query succeeds and firmware is validated.


### 3.4 New Helper Function

To maintain consistency, we need a new response creation function:

```c
/**
 * @brief Create CheckUpdateResponse for opt-out scenarios with firmware metadata
 * 
 * Similar to create_success_response() but with opt-out status codes.
 * Used when firmware is available but opt-out restrictions apply.
 * 
 * @param status_code IGNORE_OPTOUT or BYPASS_OPTOUT
 * @param available_version Firmware version from XConf
 * @param update_details Pipe-delimited metadata string
 * @param status_message Human-readable status
 * @return CheckUpdateResponse with all metadata populated
 */
static CheckUpdateResponse create_optout_response(CheckForUpdateStatus status_code,
                                                   const gchar *available_version,
                                                   const gchar *update_details,
                                                   const gchar *status_message) {
    CheckUpdateResponse response = {0};
    char current_img_buffer[256] = {0};
    
    bool img_status = GetFirmwareVersion(current_img_buffer, sizeof(current_img_buffer));
    
    response.result = CHECK_FOR_UPDATE_SUCCESS;
    response.status_code = status_code;
    response.current_img_version = g_strdup(img_status ? current_img_buffer : "Unknown");
    response.available_version = g_strdup(available_version ? available_version : "");
    response.update_details = g_strdup(update_details ? update_details : "");
    response.status_message = g_strdup(status_message ? status_message : "");
    
    return response;
}
```

---

## 4. Edge Cases and Special Scenarios

### 4.1 Critical Update Override

**Scenario**: User has opted out (optout=1) but XConf returns critical update

**Expected Behavior**:
- Return `FIRMWARE_AVAILABLE` (code 0), **not** `IGNORE_OPTOUT`
- Include clear message: "Critical firmware update available - user opt-out bypassed for security"
- Client should proceed with download immediately
- No user consent required

**Implementation**:
```c
if (optout == 1 && isCriticalUpdate) {
    SWLOG_INFO("[CheckForUpdate] Critical update - bypassing opt-out\n");
    return create_success_response(response.cloudFWVersion, 
                                    update_details, 
                                    "Critical firmware update available (security/stability)");
}
```

### 4.2 Trigger Type 4 (Manual/Forced Trigger)

**Current CLI Behavior**: `trigger_type == 4` bypasses ENFORCE_OPTOUT

**Daemon Consideration**:
- `CheckForUpdate` API has no trigger_type parameter currently
- This may need to be added as an optional parameter in future enhancement
- For now: Document limitation and recommend UI handle manual triggers differently

**Recommendation**: 
- Document that `BYPASS_OPTOUT` status should allow manual override in UI
- Future enhancement: Add optional `triggerType` parameter to CheckForUpdate

### 4.3 Config File Missing or Corrupt

**Scenario**: `/opt/maintenance_mgr_record.conf` doesn't exist or is unreadable

**Current `getOPTOUTValue()` Behavior**: Returns `-1` (not set)

**Expected Behavior**: 
- Treat as "no preference set" - proceed normally
- Log warning: "OPTOUT config file not found - treating as no restriction"
- Return `FIRMWARE_AVAILABLE` if firmware is valid

### 4.4 sw_optout Flag Not Set

**Scenario**: `device_info.sw_optout` is `"false"` or empty

**Expected Behavior**:
- Skip entire opt-out mechanism
- Never return `IGNORE_OPTOUT` or `BYPASS_OPTOUT` status codes
- Behave as if opt-out feature is disabled for this device

### 4.5 Cache vs. Live XConf Query

**Current Implementation**: CheckForUpdate tries cache first (commented out), then live query

**Opt-Out Consideration**:
- Critical flag (`cloudImmediateRebootFlag`) is in XConf response
- If using cache: Critical flag is preserved - decision logic works correctly
- If cache miss: Live query provides fresh critical flag
- **No special handling needed** - existing cache logic is compatible

### 4.6 No Firmware Available (Already Up-to-Date)

**Scenario**: XConf returns no firmware version (already on latest)

**Current Behavior**: Returns `FIRMWARE_NOT_AVAILABLE` (code 1)

**With Opt-Out**: 
- If user has opted out but no firmware available anyway: Return `FIRMWARE_NOT_AVAILABLE`
- Opt-out checks are only relevant when firmware IS available
- Optimization: Can skip opt-out check entirely if no firmware returned from XConf

---

## 5. Decision Table (Comprehensive)

**Legend**:
- `*` = Any value / Don't care
- `optout`: -1=not set/error, 0=ENFORCE_OPTOUT, 1=IGNORE_UPDATE
- `isCritical`: Parsed from `cloudImmediateRebootFlag` in XConf response
- `FW Avail`: Firmware availability from XConf (Yes/No/Error)

| maint_status | sw_optout | optout | isCritical | FW Avail | Status Code               | Message                                         |
|--------------|-----------|--------|------------|----------|---------------------------|-------------------------------------------------|
| false        | *         | *      | *          | Yes      | FIRMWARE_AVAILABLE (0)    | "Firmware update available"                     |
| true         | false     | *      | *          | Yes      | FIRMWARE_AVAILABLE (0)    | "Firmware update available"                     |
| true         | true      | -1     | *          | Yes      | FIRMWARE_AVAILABLE (0)    | "Firmware update available"                     |
| true         | true      | 1      | true       | Yes      | FIRMWARE_AVAILABLE (0)    | "Critical firmware update available"            |
| true         | true      | 1      | false      | Yes      | **IGNORE_OPTOUT (4)**     | "User has opted out of updates"                 |
| true         | true      | 0      | *          | Yes      | **BYPASS_OPTOUT (5)**     | "User consent required for installation"        |
| *            | *         | *      | *          | No       | FIRMWARE_NOT_AVAILABLE (1) | "No firmware update available"                  |
| *            | *         | *      | *          | Error    | FIRMWARE_CHECK_ERROR (3)   | "Error checking for updates"                    |
| *            | *         | *      | *          | Invalid  | UPDATE_NOT_ALLOWED (2)     | "Firmware not compatible with device"           |

**Key Decision Points**:
1. If `maint_status == "false"` → Skip all opt-out logic, return normal status
2. If `sw_optout == "false"` → Skip all opt-out logic, return normal status  
3. If `optout == -1` → No user preference, return normal status
4. If `optout == 1 && isCritical` → Bypass opt-out for critical updates
5. If `optout == 1 && !isCritical` → Block update, return IGNORE_OPTOUT
6. If `optout == 0` → Allow download but require consent, return BYPASS_OPTOUT

**All decisions occur AFTER XConf query completes successfully**.

---

## 6. Client Application Guidance

### 6.1 How Clients Should Handle Status Codes

#### FIRMWARE_AVAILABLE (0)
```
✓ Proceed to download firmware
✓ No user interaction required (or show progress)
✓ Call DownloadFirmware API after CheckForUpdate
```

#### FIRMWARE_NOT_AVAILABLE (1)
```
✓ Inform user: "Already on latest firmware"
✓ No further action needed
✓ Update UI to reflect current status
```

#### UPDATE_NOT_ALLOWED (2)
```
✗ Do NOT attempt download
✗ Log error: Firmware not for this device model
✓ This should not happen in production (server-side issue)
```

#### FIRMWARE_CHECK_ERROR (3)
```
✗ Network or server error
✓ Inform user: "Unable to check for updates - try again later"
✓ Retry with exponential backoff
```

#### **IGNORE_OPTOUT (4)** [NEW]
```
✗ Do NOT download firmware
✗ Do NOT show "Update Available" notification
✓ UI should show: "Firmware updates disabled by user"
✓ Provide option to change opt-out preference
✓ If critical update: Server will set cloudImmediateRebootFlag, 
  and status will be FIRMWARE_AVAILABLE instead
```

#### **BYPASS_OPTOUT (5)** [NEW]
```
✓ Download firmware in background (call DownloadFirmware API)
✓ DO NOT install automatically
✓ UI should show: "Firmware update ready - approval required"
✓ Present user consent dialog: "Install now?" [Yes] [Later] [Never]
✓ If user approves: Call UpdateFirmware API
✓ If user declines: Keep firmware downloaded, reminder later
```

### 6.2 Sample Client Pseudo-Code

```javascript
// Client calls CheckForUpdate
response = rdkFwupdateMgr.CheckForUpdate(handlerId);

switch (response.status_code) {
    case 0: // FIRMWARE_AVAILABLE
        showNotification("Firmware update available");
        downloadAndInstall(response);
        break;
        
    case 1: // FIRMWARE_NOT_AVAILABLE
        updateUI("Up to date");
        break;
        
    case 4: // IGNORE_OPTOUT
        log("User has opted out - not downloading");
        showSettingsPrompt("Firmware updates disabled. Change settings?");
        break;
        
    case 5: // BYPASS_OPTOUT
        log("User consent required - downloading but not installing");
        downloadFirmware(response); // Download in background
        showConsentDialog("Firmware ready - install now?");
        // If user approves → updateFirmware()
        // If user declines → schedule reminder
        break;
        
    case 3: // FIRMWARE_CHECK_ERROR
        showError("Unable to check for updates");
        scheduleRetry();
        break;
        
    default:
        logError("Unknown status code: " + response.status_code);
}
```

---

## 7. Backward Compatibility

### 7.1 Existing Clients

**Scenario**: Old client applications that don't know about status codes 4 and 5

**Impact**:
- Clients may treat unknown status codes as errors
- Default case in switch statements may handle gracefully
- Or clients may ignore updates entirely

**Mitigation**:
1. **Graceful Degradation**: Clients with default cases will log and handle appropriately
2. **Client Updates Required**: Recommend clients update to handle new codes
3. **Documentation**: Clearly document new status codes in API changelog
4. **Versioning**: Consider API version field in future

**Risk Level**: LOW
- Most clients already handle multiple status codes
- Unknown codes typically treated as "no action" scenarios
- Critical updates still work (return code 0)

### 7.2 D-Bus Interface

**Current D-Bus Method Signature** (from `rdkFwupdateMgr.xml`):
```xml
<method name="CheckForUpdate">
    <arg name="handler_id" type="t" direction="in"/>
    <arg name="result" type="u" direction="out"/>
    <arg name="status_code" type="u" direction="out"/>
    <arg name="current_img_version" type="s" direction="out"/>
    <arg name="available_version" type="s" direction="out"/>
    <arg name="update_details" type="s" direction="out"/>
    <arg name="status_message" type="s" direction="out"/>
</method>
```

**Change Required**: NONE
- Status codes 4 and 5 are already in enum (lines 58-59 of `rdkFwupdateMgr_handlers.h`)
- D-Bus interface uses unsigned integer (`type="u"`) for status_code
- No schema change needed
- Fully backward compatible

### 7.3 Testing Strategy

**Compatibility Test Plan**:
1. Test old client with new status codes → Should handle gracefully
2. Test new client with old daemon (without opt-out logic) → Works (never returns 4/5)
3. Test existing automated tests → Should pass (no opt-out in test scenarios)
4. Test new status codes with various client UI frameworks

---

## 8. Implementation Checklist

### 8.1 Code Changes Required

**File**: `src/dbus/rdkFwupdateMgr_handlers.c`

- [ ] Add post-XConf opt-out enforcement logic (Phase 3)
- [ ] Implement `create_optout_response()` helper function
- [ ] Update `rdkFwupdateMgr_checkForUpdate()` with opt-out decision logic
- [ ] Add comprehensive logging for opt-out decisions
- [ ] Handle edge cases (config file missing, sw_optout disabled, maint_status disabled, etc.)
- [ ] Ensure XConf query always executes (no early exits)

**File**: `src/dbus/rdkFwupdateMgr_handlers.h`

- [ ] Verify status code enum definitions (already exist - lines 58-59)
- [ ] Update function documentation to include opt-out behavior
- [ ] Document new status codes in header comments
- [ ] Document that XConf query always occurs

**File**: `src/rdkFwupdateMgr.c`

- [ ] Ensure `getOPTOUTValue()` is accessible (may need to move to common utils)
- [ ] Verify `device_info` structure is populated correctly at daemon startup
- [ ] Confirm `device_info` is globally accessible in CheckForUpdate context
- [ ] Verify maintenance mode flag (`maint_status`) is loaded correctly

### 8.2 Testing Requirements

**Unit Tests** (`unittest/rdkFwupdateMgr_handlers_gtest.cpp`):

- [ ] Test IGNORE_OPTOUT return (optout=1, non-critical)
- [ ] Test BYPASS_OPTOUT return (optout=0)
- [ ] Test critical update bypass (optout=1, critical=true → returns 0)
- [ ] Test sw_optout disabled (should never return 4/5)
- [ ] Test config file missing (optout=-1)
- [ ] Test maint_status disabled (should skip opt-out)
- [ ] Test normal flow (no opt-out) → returns 0
- [ ] Test error cases (XConf failure + opt-out)

**Functional Tests**:

- [ ] End-to-end test: Set IGNORE_UPDATE, verify CheckForUpdate returns code 4
- [ ] End-to-end test: Set ENFORCE_OPTOUT, verify CheckForUpdate returns code 5
- [ ] Critical update test: Set IGNORE_UPDATE + critical firmware → returns code 0
- [ ] Client integration test: Verify UI handles new status codes correctly
- [ ] Stress test: Rapid CheckForUpdate calls with opt-out changes

### 8.3 Documentation Updates

- [ ] Update API documentation with new status codes
- [ ] Update client integration guide with handling instructions
- [ ] Add examples to developer documentation
- [ ] Update CHANGELOG.md with new feature
- [ ] Update OPTOUT_MECHANISM.md with daemon behavior
- [ ] Add sequence diagrams for new flows

### 8.4 Deployment Considerations

- [ ] Ensure config file path is correct for target platforms
- [ ] Verify device properties are loaded correctly in daemon
- [ ] Test on all supported RDK platforms
- [ ] Validate with production XConf server responses
- [ ] Performance test: Measure impact of opt-out checks (should be negligible)

---

## 9. Risks and Mitigation

### 9.1 Risk: False Opt-Out Blocks

**Scenario**: Bug causes legitimate updates to be blocked incorrectly

**Mitigation**:
- Critical updates always bypass (fallback safety net)
- Extensive logging to debug false blocks
- Unit tests cover all decision branches
- Easy to disable opt-out via device properties

**Severity**: Medium | **Likelihood**: Low

### 9.2 Risk: Config File Corruption

**Scenario**: `/opt/maintenance_mgr_record.conf` gets corrupted

**Mitigation**:
- `getOPTOUTValue()` returns -1 on error → treated as "no restriction"
- System defaults to allowing updates (safe default)
- Regular config file validation in maintenance manager

**Severity**: Low | **Likelihood**: Low

### 9.3 Risk: Client Doesn't Handle New Status Codes

**Scenario**: Old client treats IGNORE_OPTOUT/BYPASS_OPTOUT as errors

**Mitigation**:
- Document new codes in changelog
- Provide client update guide
- Most clients have default error handling
- Gradual rollout with client updates

**Severity**: Low | **Likelihood**: Medium

### 9.4 Risk: Critical Flag Not Set Correctly by XConf

**Scenario**: Security update lacks `cloudImmediateRebootFlag=true`

**Mitigation**:
- Server-side responsibility to set flag correctly
- Document critical update criteria clearly
- Provide manual override mechanism (trigger_type=4 in future)
- Monitor telemetry for blocked critical updates

**Severity**: High | **Likelihood**: Low

### 9.5 Risk: Race Condition During Config File Update

**Scenario**: User changes opt-out preference while CheckForUpdate is running

**Mitigation**:
- `getOPTOUTValue()` is atomic (single file read)
- Result is based on snapshot at time of call
- Next CheckForUpdate will use updated preference
- No persistent state issues

**Severity**: Low | **Likelihood**: Low

---

## 10. Performance Impact

### 10.1 Additional Operations

**New Operations**:
1. Read `/opt/maintenance_mgr_record.conf` (small file, ~100 bytes)
2. Parse opt-out value (string search, ~3 lines)
3. Additional conditional checks (negligible CPU)

**Estimated Impact**: < 5ms per CheckForUpdate call

**Not Impacted**: 
- XConf query time (always executed in both old and new design)
- Network latency (no change)
- Firmware validation time (no change)

### 10.2 Why No Pre-Flight Optimization

**Original Proposal** (Version 1.0):
- Suggested skipping XConf query when `optout==1` to save network resources

**Rejected Because**:
1. **Critical Flag Unknown**: Cannot determine if update is critical without XConf response
2. **Incomplete Client Info**: Clients need firmware metadata even for blocked updates
3. **Complexity**: Multiple code paths increase maintenance burden
4. **Limited Benefit**: Opt-out is rare; most users don't opt out
5. **Analytics Loss**: Skipping XConf loses telemetry about update availability

**Performance Trade-off Analysis**:
```
Scenario: 10,000 CheckForUpdate calls/day, 5% users opted out

OLD (with skip optimization):
- 9,500 calls: Full XConf query (500ms avg) = 4,750s
- 500 calls: Skip XConf, early exit (10ms avg) = 5s
- Total: 4,755s (79.25 minutes)

NEW (always query):
- 10,000 calls: Full XConf query (500ms avg) = 5,000s
- Total: 5,000s (83.33 minutes)

Difference: 4 minutes/day across entire fleet
Benefit: Simpler code, complete client info, preserved telemetry
```

**Conclusion**: Minor performance cost (< 5%) for significant architectural benefits.

### 10.3 Future Optimization Opportunities

**If Performance Becomes Issue**:
1. **XConf Response Caching**: Already implemented - leverage cache more aggressively
2. **Opt-Out Status Caching**: Cache opt-out value in memory (reduce file I/O)
3. **Batch Updates**: Combine multiple CheckForUpdate requests
4. **CDN for XConf**: Reduce XConf query latency through content delivery network

**Recommendation**: Monitor telemetry after deployment; optimize only if data shows performance issue.

---

## 11. Future Enhancements

### 11.1 Trigger Type Parameter

**Proposal**: Add optional `triggerType` parameter to CheckForUpdate

```c
CheckUpdateResponse rdkFwupdateMgr_checkForUpdate(const gchar *handler_id, 
                                                   int trigger_type);
```

**Use Case**: 
- `trigger_type=4` → Manual/forced trigger → bypass ENFORCE_OPTOUT
- Matches CLI behavior

**Consideration**: D-Bus interface change required

### 11.2 Opt-Out Status Query API

**Proposal**: New API to query current opt-out status without checking firmware

```c
OptOutStatus rdkFwupdateMgr_getOptOutStatus(void);

typedef enum {
    OPTOUT_DISABLED = 0,    // Feature disabled on device
    OPTOUT_NOT_SET = 1,     // No preference
    OPTOUT_IGNORE = 2,      // User has opted out
    OPTOUT_ENFORCE = 3      // User requires consent
} OptOutStatus;
```

**Use Case**: UI can show opt-out status before attempting update check

### 11.3 Consent API

**Proposal**: API for clients to record user consent

```c
int rdkFwupdateMgr_recordConsent(const gchar *handler_id, 
                                 const gchar *firmware_version,
                                 gboolean approved);
```

**Use Case**: Client can inform daemon when user approves/denies update

### 11.4 Opt-Out Change Notification

**Proposal**: D-Bus signal when opt-out preference changes

```xml
<signal name="OptOutStatusChanged">
    <arg name="new_status" type="u"/>
    <arg name="message" type="s"/>
</signal>
```

**Use Case**: Client UI can react to preference changes in real-time

---

## 12. Summary and Recommendations

### 12.1 Key Takeaways

1. **New status codes already defined** - Implementation is the remaining work
2. **Opt-out logic exists in CLI** - Need to adapt for daemon CheckForUpdate API
3. **Critical updates always work** - Safety net is preserved
4. **Backward compatible** - Existing clients won't break
5. **Clear client guidance** - Developers know how to handle new codes

### 12.2 Implementation Priority

**High Priority** (Must Have):
- ✅ Implement opt-out decision logic in CheckForUpdate
- ✅ Return IGNORE_OPTOUT for blocked updates
- ✅ Return BYPASS_OPTOUT for consent-required updates
- ✅ Handle critical update override
- ✅ Add comprehensive unit tests

**Medium Priority** (Should Have):
- ⚠️ Update documentation and examples
- ⚠️ Client integration guide
- ⚠️ Functional/integration tests
- ⚠️ Performance validation

**Low Priority** (Nice to Have):
- 💡 Trigger type parameter support
- 💡 Opt-out status query API
- 💡 Consent recording API
- 💡 Change notification signals

### 12.3 Success Criteria

**Feature is successful if**:
1. ✅ CheckForUpdate returns IGNORE_OPTOUT when user has opted out (non-critical)
2. ✅ CheckForUpdate returns BYPASS_OPTOUT when consent required
3. ✅ Critical updates always proceed regardless of opt-out status
4. ✅ Existing clients continue to work (no breaking changes)
5. ✅ New clients can provide better UX with opt-out awareness
6. ✅ No performance degradation (< 5ms overhead)
7. ✅ All unit and functional tests pass

---

## 13. References

### Internal Documentation
- **OPTOUT_MECHANISM.md** - Complete opt-out mechanism documentation
- **XCONF_CACHE_DESIGN.md** - XConf caching architecture
- **README.md** - Project overview and build instructions

### Source Files
- `src/dbus/rdkFwupdateMgr_handlers.c` (lines 1139-1272) - CheckForUpdate implementation
- `src/dbus/rdkFwupdateMgr_handlers.h` (lines 48-59) - Status code enum definitions
- `src/rdkv_main.c` (lines 651-667) - CLI opt-out logic reference
- `src/rdkFwupdateMgr.c` (lines 640-680) - Daemon opt-out logic reference

### Key Functions
- `rdkFwupdateMgr_checkForUpdate()` - Main API to be updated
- `getOPTOUTValue()` - Reads opt-out preference from config
- `create_result_response()` - Creates status response structure
- `fetch_xconf_firmware_info()` - Queries XConf server

### Configuration Files
- `/opt/maintenance_mgr_record.conf` - User opt-out preference
- `/etc/device.properties` - Device feature flags (sw_optout, maint_status)

---

## Appendix A: Code Snippets

### A.1 Complete Implementation Example

```c
// In rdkFwupdateMgr_handlers.c

CheckUpdateResponse rdkFwupdateMgr_checkForUpdate(const gchar *handler_id) {
    SWLOG_INFO("[rdkFwupdateMgr] ===== FUNCTION ENTRY: rdkFwupdateMgr_checkForUpdate() =====\n");
    
    if (!handler_id) {
        SWLOG_ERROR("[rdkFwupdateMgr] CRITICAL ERROR: handler_id is NULL!\n");
        return create_result_response(FIRMWARE_CHECK_ERROR, "Internal error - invalid handler ID");
    }
    
    SWLOG_INFO("[rdkFwupdateMgr] CheckForUpdate: handler=%s\n", handler_id);
    
    XCONFRES response = {0};
    int http_code = 0;
    int server_type = HTTP_XCONF_DIRECT;
    int ret = -1;
    
    // ALWAYS query XConf - no early exits based on opt-out
    SWLOG_INFO("[rdkFwupdateMgr] Making live XConf call\n");
    ret = fetch_xconf_firmware_info(&response, server_type, &http_code);
    
    if (ret == 0 && http_code == 200) {
        // Validate firmware
        SWLOG_INFO("[rdkFwupdateMgr] ===== VALIDATE XCONF RESPONSE =====\n");
        int validation_result = processJsonResponse(&response,
                                                    cur_img_detail.cur_img_name,
                                                    device_info.model,
                                                    device_info.maint_status);
        
        if (validation_result != 0) {
            SWLOG_ERROR("[rdkFwupdateMgr] VALIDATION FAILED\n");
            return create_result_response(UPDATE_NOT_ALLOWED, 
                                          "Firmware validation failed - not for this device model");
        }
        
        // Check if firmware version is present
        if (!response.cloudFWVersion[0] || strlen(response.cloudFWVersion) == 0) {
            SWLOG_INFO("[rdkFwupdateMgr] No firmware version - no update available\n");
            return create_result_response(FIRMWARE_NOT_AVAILABLE, "No firmware update available");
        }
        
        // Parse critical update flag from XConf response
        bool isCriticalUpdate = false;
        if (strncmp(response.cloudImmediateRebootFlag, "true", 4) == 0) {
            isCriticalUpdate = true;
            SWLOG_INFO("[CheckForUpdate] CRITICAL UPDATE DETECTED\n");
        }
        
        // Create firmware metadata string
        gchar *update_details = g_strdup_printf(
            "File:%s|Location:%s|IPv6Location:%s|Version:%s|Protocol:%s|Reboot:%s|Delay:%s|PDRI:%s|Peripherals:%s|CertBundle:%s", 
            response.cloudFWFile[0] ? response.cloudFWFile : "N/A",
            response.cloudFWLocation[0] ? response.cloudFWLocation : "N/A", 
            response.ipv6cloudFWLocation[0] ? response.ipv6cloudFWLocation : "N/A",
            response.cloudFWVersion[0] ? response.cloudFWVersion : "N/A",
            response.cloudProto[0] ? response.cloudProto : "HTTP",
            response.cloudImmediateRebootFlag[0] ? response.cloudImmediateRebootFlag : "false",
            response.cloudDelayDownload[0] ? response.cloudDelayDownload : "0",
            response.cloudPDRIVersion[0] ? response.cloudPDRIVersion : "N/A",
            response.peripheralFirmwares[0] ? response.peripheralFirmwares : "N/A",
            response.dlCertBundle[0] ? response.dlCertBundle : "N/A"
        );
        
        // ===== POST-XCONF OPT-OUT EVALUATION =====
        
        // Check 1: Is Maintenance Manager integration active?
        if (strncmp(device_info.maint_status, "true", 4) != 0) {
            SWLOG_INFO("[CheckForUpdate] MaintenanceMGR not active - skipping opt-out\n");
            CheckUpdateResponse result = create_success_response(
                response.cloudFWVersion,
                update_details,
                "Firmware update available"
            );
            g_free(update_details);
            return result;
        }
        
        // Check 2: Is opt-out feature enabled for this device?
        if (strncmp(device_info.sw_optout, "true", 4) != 0) {
            SWLOG_INFO("[CheckForUpdate] Opt-out feature disabled - skipping opt-out\n");
            CheckUpdateResponse result = create_success_response(
                response.cloudFWVersion,
                update_details,
                "Firmware update available"
            );
            g_free(update_details);
            return result;
        }
        
        // Check 3: Read user's opt-out preference
        int optout = getOPTOUTValue("/opt/maintenance_mgr_record.conf");
        SWLOG_INFO("[CheckForUpdate] Opt-out value: %d\n", optout);
        
        if (optout == -1) {
            SWLOG_INFO("[CheckForUpdate] No opt-out preference set\n");
            CheckUpdateResponse result = create_success_response(
                response.cloudFWVersion,
                update_details,
                "Firmware update available"
            );
            g_free(update_details);
            return result;
        }
        
        // Check 4: Apply opt-out decision logic
        if (optout == 1) {
            // User has opted out (IGNORE_UPDATE)
            if (isCriticalUpdate) {
                // Critical update bypasses opt-out
                SWLOG_INFO("[CheckForUpdate] CRITICAL UPDATE - Bypassing opt-out\n");
                CheckUpdateResponse result = create_success_response(
                    response.cloudFWVersion,
                    update_details,
                    "Critical firmware update available (security/stability)"
                );
                g_free(update_details);
                return result;
            } else {
                // Non-critical update blocked by user
                SWLOG_INFO("[CheckForUpdate] BLOCKING: User opted out, non-critical update\n");
                g_free(update_details);
                return create_result_response(IGNORE_OPTOUT, 
                                              "Firmware download blocked - user has opted out of updates");
            }
        }
        else if (optout == 0) {
            // User requires consent (ENFORCE_OPTOUT)
            SWLOG_INFO("[CheckForUpdate] CONSENT REQUIRED: Returning firmware info with BYPASS_OPTOUT\n");
            CheckUpdateResponse result = create_optout_response(
                BYPASS_OPTOUT,
                response.cloudFWVersion,
                update_details,
                "Firmware available - user consent required before installation"
            );
            g_free(update_details);
            return result;
        }
        
        // Normal flow (should not reach here, but defensive)
        SWLOG_INFO("[CheckForUpdate] Normal flow - returning FIRMWARE_AVAILABLE\n");
        CheckUpdateResponse result = create_success_response(
            response.cloudFWVersion,
            update_details,
            "Firmware update available"
        );
        g_free(update_details);
        return result;
        
    } else {
        // XConf query failed
        SWLOG_ERROR("[rdkFwupdateMgr] XConf communication failed: ret=%d, http=%d\n", ret, http_code);
        
        if (http_code != 200) {
            return create_result_response(FIRMWARE_CHECK_ERROR, 
                                          "Network error - unable to reach update server");
        } else {
            return create_result_response(FIRMWARE_CHECK_ERROR, 
                                          "Update check failed - server communication error");
        }
    }
}

// New helper function
static CheckUpdateResponse create_optout_response(CheckForUpdateStatus status_code,
                                                   const gchar *available_version,
                                                   const gchar *update_details,
                                                   const gchar *status_message) {
    CheckUpdateResponse response = {0};
    char current_img_buffer[256] = {0};
    
    bool img_status = GetFirmwareVersion(current_img_buffer, sizeof(current_img_buffer));
    
    response.result = CHECK_FOR_UPDATE_SUCCESS;
    response.status_code = status_code;
    response.current_img_version = g_strdup(img_status ? current_img_buffer : "Unknown");
    response.available_version = g_strdup(available_version ? available_version : "");
    response.update_details = g_strdup(update_details ? update_details : "");
    response.status_message = g_strdup(status_message ? status_message : "");
    
    SWLOG_INFO("[rdkFwupdateMgr] create_optout_response: status_code=%d, message='%s'\n",
               status_code, response.status_message);
    
    return response;
}
```

---

## Appendix B: Test Cases

### B.1 Unit Test Scenarios

```cpp
// Test 1: IGNORE_OPTOUT - User opted out, non-critical
TEST(CheckForUpdate, IgnoreOptOut_NonCritical) {
    // Setup
    device_info.maint_status = "true";
    device_info.sw_optout = "true";
    mock_optout_value = 1; // IGNORE_UPDATE
    mock_xconf_immediate_reboot = "false"; // Not critical
    
    // Execute
    CheckUpdateResponse response = rdkFwupdateMgr_checkForUpdate("test_handler");
    
    // Verify
    EXPECT_EQ(response.status_code, IGNORE_OPTOUT);
    EXPECT_STREQ(response.status_message, "Firmware download blocked - user has opted out of updates");
}

// Test 2: FIRMWARE_AVAILABLE - User opted out, but CRITICAL
TEST(CheckForUpdate, IgnoreOptOut_Critical) {
    // Setup
    device_info.maint_status = "true";
    device_info.sw_optout = "true";
    mock_optout_value = 1; // IGNORE_UPDATE
    mock_xconf_immediate_reboot = "true"; // CRITICAL
    
    // Execute
    CheckUpdateResponse response = rdkFwupdateMgr_checkForUpdate("test_handler");
    
    // Verify
    EXPECT_EQ(response.status_code, FIRMWARE_AVAILABLE); // Bypassed opt-out
    EXPECT_TRUE(strstr(response.status_message, "Critical") != NULL);
}

// Test 3: BYPASS_OPTOUT - User requires consent
TEST(CheckForUpdate, BypassOptOut_ConsentRequired) {
    // Setup
    device_info.maint_status = "true";
    device_info.sw_optout = "true";
    mock_optout_value = 0; // ENFORCE_OPTOUT
    
    // Execute
    CheckUpdateResponse response = rdkFwupdateMgr_checkForUpdate("test_handler");
    
    // Verify
    EXPECT_EQ(response.status_code, BYPASS_OPTOUT);
    EXPECT_STREQ(response.status_message, "Firmware available - user consent required before installation");
    EXPECT_STRNE(response.available_version, ""); // Should have firmware info
}

// Test 4: Normal flow - opt-out disabled
TEST(CheckForUpdate, OptOutDisabled) {
    // Setup
    device_info.maint_status = "true";
    device_info.sw_optout = "false"; // Disabled
    mock_optout_value = 1; // Even if set, should be ignored
    
    // Execute
    CheckUpdateResponse response = rdkFwupdateMgr_checkForUpdate("test_handler");
    
    // Verify
    EXPECT_EQ(response.status_code, FIRMWARE_AVAILABLE); // Normal flow
}

// Test 5: Config file missing
TEST(CheckForUpdate, ConfigFileMissing) {
    // Setup
    device_info.maint_status = "true";
    device_info.sw_optout = "true";
    mock_optout_value = -1; // File missing or error
    
    // Execute
    CheckUpdateResponse response = rdkFwupdateMgr_checkForUpdate("test_handler");
    
    // Verify
    EXPECT_EQ(response.status_code, FIRMWARE_AVAILABLE); // Treat as no restriction
}
```

---

## Document Approval

| Role                     | Name           | Date       | Signature |
|--------------------------|----------------|------------|-----------|
| Lead Developer           |                |            |           |
| Software Architect       |                |            |           |
| QA Lead                  |                |            |           |
| Product Manager          |                |            |           |

---

**End of Document**

