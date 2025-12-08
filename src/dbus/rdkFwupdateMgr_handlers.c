/**
 * @file rdkFwupdateMgr_handlers.c
 * @brief Business logic handlers for firmware update operations
 * 
 * Implements the core firmware update functionality:
 * - CheckForUpdate: Queries XConf server for available firmware
 * - Response caching to reduce server load
 * - Firmware validation against device model
 * - Version comparison logic
 * 
 * These handlers are called by D-Bus interface but contain no D-Bus code.
 */

#include "rdkFwupdateMgr_handlers.h"
#include "rdkv_cdl_log_wrapper.h"
#include "rdkv_cdl.h"
#include "json_process.h"
#include "device_status_helper.h"
#ifndef GTEST_ENABLE
#include "downloadUtil.h"
#include "urlHelper.h"
#include "common_device_api.h"
#include "rdk_fwdl_utils.h"
#include "system_utils.h"
#else
#include "miscellaneous.h"
#endif
#include "deviceutils.h"
#include "rdkv_upgrade.h"
#include "device_api.h"
#include "iarmInterface.h"
#include "rfcinterface.h"
#include <string.h>

// Buffer sizes for JSON and URL construction
#define JSON_STR_LEN        1000
#define URL_MAX_LEN         512

// XConf response cache files (reduces server queries)
#define XCONF_CACHE_FILE        "/tmp/xconf_response_thunder.txt"
#define XCONF_HTTP_CODE_FILE    "/tmp/xconf_httpcode_thunder.txt"
#define XCONF_PROGRESS_FILE     "/tmp/xconf_curl_progress_thunder"
#define RED_STATE_FILE          "/lib/rdk/stateRedRecovery.sh"

// Shared device and image information (populated at daemon startup)
extern DeviceProperty_t device_info;
extern ImageDetails_t cur_img_detail;
extern Rfc_t rfc_list;

// Trigger type constant for manual/D-Bus initiated downloads
#define TRIGGER_MANUAL 1

/**
 * @brief Check if XConf response cache exists
 * @return TRUE if cache file exists, FALSE otherwise
 * 
 * Used to avoid unnecessary XConf queries when cached data is available.
 */
gboolean xconf_cache_exists(void) {
    return g_file_test(XCONF_CACHE_FILE, G_FILE_TEST_EXISTS);
}

/**
 * @brief Load XConf response from cache file
 * @param[out] pResponse Structure to populate with cached data
 * @return TRUE on success, FALSE if cache read/parse fails
 * 
 * Reads cached XConf JSON response and parses it into XCONFRES structure.
 * Cache miss or parse failure returns FALSE - caller should fetch from XConf server.
 */
static gboolean load_xconf_from_cache(XCONFRES *pResponse) {
    gchar *cache_content = NULL;
    gsize length;
    GError *error = NULL;
    gboolean result = FALSE;
    
    SWLOG_INFO("[CACHE] Loading XConf data from cache: %s\n", XCONF_CACHE_FILE);
    
    if (!g_file_get_contents(XCONF_CACHE_FILE, &cache_content, &length, &error)) {
        SWLOG_ERROR("[CACHE] Failed to read cache file: %s\n", error ? error->message : "Unknown error");
        if (error) g_error_free(error);
        return FALSE;
    }
    
    SWLOG_INFO("[CACHE] Loaded %zu bytes from cache\n", length);
    
    // Parse cached JSON using existing parser
    int parse_result = getXconfRespData(pResponse, cache_content);
    if (parse_result == 0) {
        SWLOG_INFO("[CACHE] Successfully parsed: version='%s', file='%s'\n", 
                   pResponse->cloudFWVersion, pResponse->cloudFWFile);
        result = TRUE;
    } else {
        SWLOG_ERROR("[CACHE] Failed to parse cached data (error: %d)\n", parse_result);
    }
    
    g_free(cache_content);
    return result;
}

static gboolean save_xconf_to_cache(const char *xconf_response, int http_code) {
    GError *error = NULL;
    
    SWLOG_INFO("[CACHE] Saving XConf response to cache files\n");
    
    // Save main XConf response
    if (!g_file_set_contents(XCONF_CACHE_FILE, xconf_response, -1, &error)) {
        SWLOG_ERROR("[CACHE] Failed to save XConf response: %s\n", error ? error->message : "Unknown error");
        if (error) g_error_free(error);
        return FALSE;
    }
    
    // Save HTTP code  
    gchar *http_code_str = g_strdup_printf("%d", http_code);
    if (!g_file_set_contents(XCONF_HTTP_CODE_FILE, http_code_str, -1, &error)) {
        SWLOG_ERROR("[CACHE] Failed to save HTTP code: %s\n", error ? error->message : "Unknown error");
        if (error) g_error_free(error);
        g_free(http_code_str);
        return FALSE;
    }
    
    SWLOG_INFO("[CACHE] XConf data cached successfully\n");
    SWLOG_INFO("[CACHE]   - Response file: %s\n", XCONF_CACHE_FILE);
    SWLOG_INFO("[CACHE]   - HTTP code file: %s (code: %d)\n", XCONF_HTTP_CODE_FILE, http_code);
    
    g_free(http_code_str);
    return TRUE;
}

static int fetch_xconf_firmware_info( XCONFRES *pResponse, int server_type, int *pHttp_code )
{
    DownloadData DwnLoc;
    char *pJSONStr = NULL;      // contains the device data string to send to XCONF server
    char *pServURL = NULL;      // the server to do the XCONF comms
    size_t len = 0;
    int ret = -1;
    void *curl = NULL;          // CURL handle

    DwnLoc.pvOut = NULL;
    DwnLoc.datasize = 0;
    DwnLoc.memsize = 0;
    *pHttp_code = 0;
    
    if( allocDowndLoadDataMem( &DwnLoc, DEFAULT_DL_ALLOC ) == 0 )
    {
        if( (pJSONStr=(char*)malloc( JSON_STR_LEN )) != NULL )
        {
            if( (pServURL=(char*)malloc( URL_MAX_LEN )) != NULL )
            {
                len = GetServURL( pServURL, URL_MAX_LEN );
                SWLOG_INFO( "fetch_xconf_firmware_info: server URL %s\n", pServURL );
                if( len )
                {
                    SWLOG_INFO("fetch_xconf_firmware_info: Server URL length: %d, preparing device JSON data...\n", (int)len);
                    len = createJsonString( pJSONStr, JSON_STR_LEN );
                    SWLOG_INFO("fetch_xconf_firmware_info: Device JSON data prepared (%d bytes)\n", (int)len);
                    SWLOG_INFO("fetch_xconf_firmware_info: JSON POST data:\n%s\n", pJSONStr);

                    //context structure for XCONF upgrade request
                    RdkUpgradeContext_t xconf_context = {0};
                    xconf_context.upgrade_type = XCONF_UPGRADE;
                    xconf_context.server_type = server_type;
                    xconf_context.artifactLocationUrl = pServURL;
                    xconf_context.dwlloc = &DwnLoc;
                    xconf_context.pPostFields = pJSONStr;
                    
                    //Have to revist these vars once - MADHU
                    Rfc_t local_rfc_list = {0};
                    getRFCSettings(&local_rfc_list);  // Read actual RFC settings from system
                    
                    const char *local_immed_reboot_flag = "false";  // Default daemon setting
                    int local_delay_dwnl = 0;                       // Default daemon setting  
                    const char *local_lastrun = "0";               // Default daemon setting
                    char *local_disableStatsUpdate = "false";      // Default daemon setting
                    int local_force_exit = 0;                      // Default daemon setting
                    int local_trigger_type = 1;                    // Default daemon setting
                    
                    xconf_context.immed_reboot_flag = local_immed_reboot_flag;
                    xconf_context.delay_dwnl = local_delay_dwnl;
                    xconf_context.lastrun = local_lastrun;
                    xconf_context.disableStatsUpdate = local_disableStatsUpdate;
                    xconf_context.device_info = &device_info;      // Uses extern global
                    xconf_context.force_exit = &local_force_exit;
                    xconf_context.trigger_type = local_trigger_type;
                    xconf_context.rfc_list = &local_rfc_list;  

                    SWLOG_INFO("Simulating a 120 seconds sleep()\n");
                    sleep(120);
                    SWLOG_INFO("Just now completed 120 seconds sleep\n");
                    SWLOG_INFO("fetch_xconf_firmware_info: Initiating XConf request with server_type=%d\n", server_type);
                    SWLOG_INFO("fetch_xconf_firmware_info: Context setup - device_info=%p, rfc_list=%p\n", 
                               xconf_context.device_info, xconf_context.rfc_list);
                    
                    // Call rdkv_upgrade_request with error handling
                    SWLOG_INFO("fetch_xconf_firmware_info: Calling rdkv_upgrade_request...\n");
                    ret = rdkv_upgrade_request(&xconf_context, &curl, pHttp_code);
                    SWLOG_INFO("fetch_xconf_firmware_info: rdkv_upgrade_request returned (ret=%d)\n", ret);
                    
                    SWLOG_INFO("fetch_xconf_firmware_info: XConf request completed - ret=%d, http_code=%d\n", ret, *pHttp_code);
                    
                    if( ret == 0 && *pHttp_code == 200 && DwnLoc.pvOut != NULL )
                    {
                        SWLOG_INFO("fetch_xconf_firmware_info: SUCCESS - XConf communication successful\n");
                        SWLOG_INFO("fetch_xconf_firmware_info: Raw XConf response (%d bytes):\n%s\n", 
                                   (int)DwnLoc.datasize, (char *)DwnLoc.pvOut);
                        SWLOG_INFO("fetch_xconf_firmware_info: Calling getXconfRespData to parse response...\n");
                        ret = getXconfRespData( pResponse, (char *)DwnLoc.pvOut );
                        SWLOG_INFO("fetch_xconf_firmware_info: getXconfRespData returned %d\n", ret);
                        
                        // Log parsed XConf response details
                        if (ret == 0) {
                            SWLOG_INFO("MakeXconfComms: PARSED XConf Response Data:\n");
                            SWLOG_INFO("  - firmwareFilename: '%s'\n", pResponse->cloudFWFile);
                            SWLOG_INFO("  - firmwareLocation: '%s'\n", pResponse->cloudFWLocation);
                            SWLOG_INFO("  - firmwareVersion: '%s'\n", pResponse->cloudFWVersion);
                            SWLOG_INFO("  - firmwareProtocol: '%s'\n", pResponse->cloudProto);
                            SWLOG_INFO("  - rebootImmediately: '%s'\n", pResponse->cloudImmediateRebootFlag);
                            SWLOG_INFO("  - delayDownload: '%s'\n", pResponse->cloudDelayDownload);
                            SWLOG_INFO("  - peripheralFirmwares: '%s'\n", pResponse->peripheralFirmwares);
                            SWLOG_INFO("  - cloudPDRIVersion: '%s'\n", pResponse->cloudPDRIVersion);
                            
                            // Cache the successful XConf response for future use
                            SWLOG_INFO("[CACHE] Saving successful XConf response to cache...\n");
                            if (save_xconf_to_cache((char *)DwnLoc.pvOut, *pHttp_code)) {
                                SWLOG_INFO("[CACHE] XConf response cached successfully\n");
                            } else {
                                SWLOG_ERROR("[CACHE] Failed to cache XConf response\n");
                            }
                        } else {
                            SWLOG_ERROR("MakeXconfComms: ERROR - Failed to parse XConf response\n");
                        }
                        
                        // Recovery completed event handling 
                        #ifndef GTEST_ENABLE
                        if( (filePresentCheck( RED_STATE_REBOOT ) == RDK_API_SUCCESS) ) {
                             SWLOG_INFO("%s : RED Recovery completed\n", __FUNCTION__);
                             eventManager(RED_STATE_EVENT, RED_RECOVERY_COMPLETED);
                             unlink(RED_STATE_REBOOT);
                        }
                        #endif
                    }
                    else
                    {
                        SWLOG_ERROR("fetch_xconf_firmware_info: FAILED - XConf communication failed\n");
                        SWLOG_ERROR("  - ret=%d (0=success)\n", ret);
                        SWLOG_ERROR("  - http_code=%d (200=success)\n", *pHttp_code);
                        SWLOG_ERROR("  - DwnLoc.pvOut=%p (should not be NULL)\n", DwnLoc.pvOut);
                        if (DwnLoc.pvOut != NULL) {
                            SWLOG_ERROR("  - Response data size: %d bytes\n", (int)DwnLoc.datasize);
                            SWLOG_ERROR("  - Response data: '%s'\n", (char *)DwnLoc.pvOut);
                        }
                    }
                }
                else
                {
                    SWLOG_ERROR( "fetch_xconf_firmware_info: no valid server URL\n" );
                }
                free( pServURL );
            }
            else
            {
                SWLOG_ERROR("fetch_xconf_firmware_info: Failed malloc for server URL of %d bytes\n", URL_MAX_LEN );
            }
            free( pJSONStr );
        }
        else
        {
            SWLOG_ERROR("fetch_xconf_firmware_info: Failed malloc for json string of %d bytes\n", JSON_STR_LEN );
        }
        if( DwnLoc.pvOut != NULL )
        {
            free( DwnLoc.pvOut );
        }
    }
    return ret;
}

/**
 * @brief Free all dynamically allocated memory in a CheckUpdateResponse structure.
 * 
 * Safely releases all g_strdup'd strings within the response structure and nullifies
 * the pointers to prevent double-free errors. Safe to call on already-freed or 
 * partially-initialized responses.
 * 
 * @param response Pointer to CheckUpdateResponse structure to free (can be NULL)
 */
void checkupdate_response_free(CheckUpdateResponse *response) {
    if (response) {
        g_free(response->available_version);
        g_free(response->update_details);
        g_free(response->status_message);
        response->available_version = NULL;
        response->update_details = NULL;
        response->status_message = NULL;
    }
}

/**
 * @brief Create a CheckUpdateResponse structure for a successful update check.
 * 
 * Compares the current firmware version with the available version from XConf.
 * If versions differ, returns UPDATE_AVAILABLE. If identical, returns UPDATE_NOT_AVAILABLE.
 * Retrieves the current firmware version via GetFirmwareVersion() and populates all
 * response fields with allocated strings.
 * 
 * @param available_version Firmware version from XConf (e.g., "VERSION_1.2.3")
 * @param update_details Pipe-delimited string with firmware metadata (URL, protocol, etc.)
 * @param status_message Human-readable status string
 * @return CheckUpdateResponse structure with allocated strings (must be freed by caller)
 */
static CheckUpdateResponse create_success_response(const gchar *available_version,
                                                   const gchar *update_details,
                                                   const gchar *status_message) {
    CheckUpdateResponse response = {0};
    char current_img_buffer[64] = {0};
    
    bool img_status = GetFirmwareVersion(current_img_buffer, sizeof(current_img_buffer));
    
    SWLOG_INFO("[rdkFwupdateMgr] create_success_response: Getting current image info\n");
    SWLOG_INFO("[rdkFwupdateMgr]   - currentImg status: %s\n", img_status ? "SUCCESS" : "FAILED");
    SWLOG_INFO("[rdkFwupdateMgr]   - current_img_buffer: '%s'\n", current_img_buffer);

    gboolean is_update_available = img_status && available_version && (g_strcmp0(current_img_buffer, available_version) != 0);
    if (is_update_available) {
    response.result_code = UPDATE_AVAILABLE;
    // img_status and available_version are guaranteed non-NULL here due to is_update_available check
    response.current_img_version = g_strdup(current_img_buffer);
    response.available_version = g_strdup(available_version);
    response.update_details = g_strdup(update_details ? update_details : "");
    response.status_message = g_strdup(status_message ? status_message : "Firmware update available");
    
    SWLOG_INFO("[rdkFwupdateMgr] create_success_response: Response created with current image: '%s', available: '%s', status: '%s'\n", 
               response.current_img_version, response.available_version, response.status_message);
    }else {
	    response.result_code = UPDATE_NOT_AVAILABLE;
	    response.current_img_version = g_strdup(current_img_buffer);
	    response.available_version = g_strdup(available_version ? available_version : "");
	    response.update_details = g_strdup("");
	    response.status_message = g_strdup("Already on latest firmware");	
    }
    return response;
}

/**
 * @brief Create a CheckUpdateResponse for error cases or no-update scenarios.
 * 
 * Populates response structure with the given result code and status message.
 * Sets available_version and update_details to empty strings. Retrieves current
 * firmware version and provides default status messages if none supplied.
 * 
 * @param result_code Result code (UPDATE_NOT_AVAILABLE, UPDATE_ERROR, etc.)
 * @param status_message Optional custom status message (can be NULL for default)
 * @return CheckUpdateResponse structure with allocated strings (must be freed by caller)
 */
static CheckUpdateResponse create_result_response(CheckForUpdateResult result_code,
                                                  const gchar *status_message) {
    CheckUpdateResponse response = {0};
    char current_img_buffer[256] = {0};
    
    bool img_status = GetFirmwareVersion(current_img_buffer, sizeof(current_img_buffer));
    
    SWLOG_INFO("[rdkFwupdateMgr] create_result_response: Getting current image info\n");
    SWLOG_INFO("[rdkFwupdateMgr]   - currentImg status: %s\n", img_status ? "SUCCESS" : "FAILED");
    SWLOG_INFO("[rdkFwupdateMgr]   - current_img_buffer: '%s'\n", current_img_buffer);
    
    response.result_code = result_code;
    response.current_img_version = g_strdup(img_status ? current_img_buffer : "Unknown");
    response.available_version = g_strdup("");
    response.update_details = g_strdup("");
    
    const gchar *default_status = "";
    if (status_message) {
        default_status = status_message;
    } else {
        switch(result_code) {
            case UPDATE_AVAILABLE:
                default_status = "Update available";
                break;
            case UPDATE_NOT_AVAILABLE:
                default_status = "No update available";
                break;
            case UPDATE_ERROR:
                default_status = "Error checking for updates";
                break;
            default:
                default_status = "Unknown status";
                break;
        }
    }
    
    response.status_message = g_strdup(default_status);
    
    SWLOG_INFO("[rdkFwupdateMgr] create_result_response: Response created with current image: '%s', status: '%s'\n", 
               response.current_img_version, response.status_message);
    
    return response;
}

/**
 * @brief Check for available firmware updates via XConf server query.
 * 
 * This is the core implementation of the CheckForUpdate D-Bus method. It performs:
 * 1. XConf server query (with cache support for offline scenarios)
 * 2. Firmware validation (model matching, version comparison)
 * 3. Response structure population with update details
 * 
 * Flow:
 * - First checks local cache for recent XConf response (to handle offline/recovery)
 * - If cache miss, queries XConf server directly via fetch_xconf_firmware_info()
 * - Validates firmware is for correct device model via processJsonResponse()
 * - Compares available version with current firmware version
 * - Returns structured response with all metadata for client decision-making
 * 
 * @param handler_id Client identifier for logging/tracking (must not be NULL)
 * @return CheckUpdateResponse structure with result_code, versions, and details
 *         Caller must free response using checkupdate_response_free()
 */
CheckUpdateResponse rdkFwupdateMgr_checkForUpdate(const gchar *handler_id) {
    
    SWLOG_INFO("[rdkFwupdateMgr] ===== FUNCTION ENTRY: rdkFwupdateMgr_checkForUpdate() =====\n");
    
    if (!handler_id) {
        SWLOG_ERROR("[rdkFwupdateMgr] CRITICAL ERROR: handler_id is NULL!\n");
        return create_result_response(UPDATE_ERROR, "Internal error - invalid handler ID");
    }
    
    SWLOG_INFO("[rdkFwupdateMgr] CheckForUpdate: handler=%s\n", handler_id);
    SWLOG_INFO("[rdkFwupdateMgr] About to allocate XCONFRES structure on stack (~2KB)...\n");
    
    XCONFRES response = {0};
    
    SWLOG_INFO("[rdkFwupdateMgr] XCONFRES structure allocated successfully\n");
    int http_code = 0;
    int server_type = HTTP_XCONF_DIRECT;
    int ret = -1;
    
    SWLOG_INFO("[rdkFwupdateMgr] CheckForUpdate: Checking for cached XConf data...\n");
    
    // Try cache first to support offline recovery scenarios
    if (xconf_cache_exists()) {
        SWLOG_INFO("[rdkFwupdateMgr] Cache hit! Loading XConf data from cache\n");
        if (load_xconf_from_cache(&response)) {
            ret = 0;
            http_code = 200;
            SWLOG_INFO("[rdkFwupdateMgr] Successfully loaded XConf data from cache\n");
        } else {
            SWLOG_ERROR("[rdkFwupdateMgr] Cache read failed, falling back to live XConf call\n");
            ret = fetch_xconf_firmware_info(&response, server_type, &http_code);
        }
    } else {
        SWLOG_INFO("[rdkFwupdateMgr] Cache miss! Making live XConf call\n");
        ret = fetch_xconf_firmware_info(&response, server_type, &http_code);
        
        if (ret == 0 && http_code == 200) {
            // Validate firmware is intended for this device model
            SWLOG_INFO("[rdkFwupdateMgr] ===== VALIDATE XCONF RESPONSE =====\n");
            int validation_result = processJsonResponse(&response,
                                                        cur_img_detail.cur_img_name,
                                                        device_info.model,
                                                        device_info.maint_status);

            SWLOG_INFO("[rdkFwupdateMgr] processJsonResponse returned: %d (0=success, 1=failed)\n", validation_result);
            if (validation_result != 0) {
                SWLOG_ERROR("[rdkFwupdateMgr] VALIDATION FAILED - Firmware not valid for this device\n");
                SWLOG_ERROR("[rdkFwupdateMgr]   - Device model: '%s'\n", device_info.model);
                SWLOG_ERROR("[rdkFwupdateMgr]   - cloudFWFile: '%s'\n", response.cloudFWFile);
                SWLOG_ERROR("[rdkFwupdateMgr]   - Reason: Model name not found in firmware filename\n");
                return create_result_response(UPDATE_NOT_AVAILABLE, "Firmware validation failed - not for this device model");
            }
            SWLOG_INFO("[rdkFwupdateMgr] VALIDATION PASSED - Firmware is valid for this device\n");
            SWLOG_INFO("[rdkFwupdateMgr] ===== VALIDATION & COMPARISON COMPLETE =====\n");
        }
    }
    
    SWLOG_INFO("[rdkFwupdateMgr] XConf call completed with result: ret=%d\n",ret);
    
    // Process successful XConf response
    if (ret == 0 && http_code == 200) {
        SWLOG_INFO("=== [rdkFwupdateMgr] XConf Response - Complete Data ===\n");
        SWLOG_INFO("[rdkFwupdateMgr] Core Firmware Data:\n");
        SWLOG_INFO("[rdkFwupdateMgr]   - cloudFWVersion: '%s'\n", 
                   response.cloudFWVersion[0] ? response.cloudFWVersion : "(empty)");
        SWLOG_INFO("[rdkFwupdateMgr]   - cloudFWFile: '%s'\n", 
                   response.cloudFWFile[0] ? response.cloudFWFile : "(empty)");
        SWLOG_INFO("[rdkFwupdateMgr]   - cloudFWLocation: '%s'\n", 
                   response.cloudFWLocation[0] ? response.cloudFWLocation : "(empty)");
        SWLOG_INFO("[rdkFwupdateMgr]   - ipv6cloudFWLocation: '%s'\n", 
                   response.ipv6cloudFWLocation[0] ? response.ipv6cloudFWLocation : "(empty)");
        
        SWLOG_INFO("[rdkFwupdateMgr] Download Control:\n");
        SWLOG_INFO("[rdkFwupdateMgr]   - cloudDelayDownload: '%s'\n", 
                   response.cloudDelayDownload[0] ? response.cloudDelayDownload : "(empty)");
        SWLOG_INFO("[rdkFwupdateMgr]   - cloudProto: '%s'\n", 
                   response.cloudProto[0] ? response.cloudProto : "(empty)");
        SWLOG_INFO("[rdkFwupdateMgr]   - cloudImmediateRebootFlag: '%s'\n", 
                   response.cloudImmediateRebootFlag[0] ? response.cloudImmediateRebootFlag : "(empty)");
        
        SWLOG_INFO("[rdkFwupdateMgr] Additional Components:\n");
        SWLOG_INFO("[rdkFwupdateMgr]   - peripheralFirmwares: '%s'\n", 
                   response.peripheralFirmwares[0] ? response.peripheralFirmwares : "(empty)");
        SWLOG_INFO("[rdkFwupdateMgr]   - dlCertBundle: '%s'\n", 
                   response.dlCertBundle[0] ? response.dlCertBundle : "(empty)");
        SWLOG_INFO("[rdkFwupdateMgr]   - cloudPDRIVersion: '%s'\n", 
                   response.cloudPDRIVersion[0] ? response.cloudPDRIVersion : "(empty)");
        SWLOG_INFO("=== [rdkFwupdateMgr] XConf Response - End ===\n"); 
       
        // Serialize XConf metadata into pipe-delimited string for D-Bus transport
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
        
        // Determine result based on presence of firmware version
        if (response.cloudFWVersion[0] && strlen(response.cloudFWVersion) > 0) {
            SWLOG_INFO("[rdkFwupdateMgr] XConf returned firmware version: '%s'\n", response.cloudFWVersion);
            
            CheckUpdateResponse result = create_success_response(
                response.cloudFWVersion,
                update_details,
                "Firmware update available"
            );
            
            g_free(update_details);
            return result;
        } else {
            SWLOG_INFO("[rdkFwupdateMgr] XConf returned no firmware version - no update available\n");
            g_free(update_details);
            return create_result_response(UPDATE_NOT_AVAILABLE, "No firmware update available");
        }
    } else {
        // XConf query failed - network or server error
        SWLOG_ERROR("[rdkFwupdateMgr] XConf communication failed: ret=%d, http=%d\n", ret, http_code);
        
        if (http_code != 200) {
            return create_result_response(UPDATE_ERROR, "Network error - unable to reach update server");
        } else {
            return create_result_response(UPDATE_ERROR, "Update check failed - server communication error");
        }
    }
}

/**
 * @brief Download firmware image from XConf URL or custom URL.
 * 
 * Downloads firmware file. Blocks until complete.
 * 
 * @param firmwareName Firmware filename to download
 * @param downloadUrl Custom URL or empty string (use XConf URL)
 * @param typeOfFirmware Firmware type: "PCI", "PDRI", "PERIPHERAL"
 * @param localFilePath Destination file path
 * @param download_state DownloadState pointer for progress updates (can be NULL)
 * @return DownloadFirmwareResult with result_code and error details
 */
DownloadFirmwareResult rdkFwupdateMgr_downloadFirmware(const gchar *firmwareName,
                                                       const gchar *downloadUrl,
                                                       const gchar *typeOfFirmware,
                                                       const gchar *localFilePath,
                                                       void *download_state) {
    SWLOG_INFO("[DOWNLOAD_HANDLER] Starting firmware download\n");
    SWLOG_INFO("[DOWNLOAD_HANDLER]   Firmware: %s\n", firmwareName ? firmwareName : "NULL");
    SWLOG_INFO("[DOWNLOAD_HANDLER]   Custom URL: '%s'\n", downloadUrl ? downloadUrl : "");
    SWLOG_INFO("[DOWNLOAD_HANDLER]   Type: %s\n", typeOfFirmware ? typeOfFirmware : "NULL");
    SWLOG_INFO("[DOWNLOAD_HANDLER]   Destination: %s\n", localFilePath ? localFilePath : "NULL");
    
    // Initialize result
    DownloadFirmwareResult result;
    result.result_code = DOWNLOAD_ERROR;
    result.error_message = NULL;
    
    // Determine effective URL
    gchar *effective_url = NULL;
    
    if (downloadUrl && strlen(downloadUrl) > 0) {
        SWLOG_INFO("[DOWNLOAD_HANDLER] Using custom URL: %s\n", downloadUrl);
        effective_url = g_strdup(downloadUrl);
    } else {
        SWLOG_INFO("[DOWNLOAD_HANDLER] No custom URL, loading from XConf cache\n");
        
        if (!xconf_cache_exists()) {
            SWLOG_ERROR("[DOWNLOAD_HANDLER] ERROR: No XConf cache found\n");
            result.error_message = g_strdup("No firmware metadata. Call CheckForUpdate first.");
            return result;
        }
        
        XCONFRES xconf_response;
        memset(&xconf_response, 0, sizeof(XCONFRES));
        
        if (!load_xconf_from_cache(&xconf_response)) {
            SWLOG_ERROR("[DOWNLOAD_HANDLER] ERROR: Failed to load XConf cache\n");
            result.error_message = g_strdup("Failed to load firmware metadata from cache");
            return result;
        }
        
        SWLOG_INFO("[DOWNLOAD_HANDLER] Loaded XConf metadata:\n");
        SWLOG_INFO("[DOWNLOAD_HANDLER]   Version: %s\n", xconf_response.cloudFWVersion);
        SWLOG_INFO("[DOWNLOAD_HANDLER]   URL: %s\n", xconf_response.cloudFWFile);
        
        effective_url = g_strdup(xconf_response.cloudFWFile);
    }
    
    if (!effective_url || strlen(effective_url) == 0) {
        SWLOG_ERROR("[DOWNLOAD_HANDLER] ERROR: No download URL available\n");
        result.error_message = g_strdup("No download URL available");
        g_free(effective_url);
        return result;
    }
    
    SWLOG_INFO("[DOWNLOAD_HANDLER] Effective download URL: %s\n", effective_url);
    
    // Prepare download context
    RdkUpgradeContext_t upgrade_context;
    memset(&upgrade_context, 0, sizeof(RdkUpgradeContext_t));
    
    // Determine upgrade type
    if (typeOfFirmware && strcmp(typeOfFirmware, "PCI") == 0) {
        upgrade_context.upgrade_type = PCI_UPGRADE;
    } else if (typeOfFirmware && strcmp(typeOfFirmware, "PDRI") == 0) {
        upgrade_context.upgrade_type = PDRI_UPGRADE;
    } else if (typeOfFirmware && strcmp(typeOfFirmware, "PERIPHERAL") == 0) {
        upgrade_context.upgrade_type = PERIPHERAL_UPGRADE;
    } else {
        upgrade_context.upgrade_type = PCI_UPGRADE;
    }
    
    SWLOG_INFO("[DOWNLOAD_HANDLER] Upgrade type: %d\n", upgrade_context.upgrade_type);
    
    // CRITICAL: Set download_only flag
    upgrade_context.download_only = TRUE;
    SWLOG_INFO("[DOWNLOAD_HANDLER] download_only=TRUE (will NOT auto-flash)\n");
    
    upgrade_context.server_type = HTTP_SSR_DIRECT;
    upgrade_context.artifactLocationUrl = effective_url;
    upgrade_context.dwlloc = (const void*)localFilePath;
    upgrade_context.pPostFields = NULL;
    upgrade_context.immed_reboot_flag = "NO";
    upgrade_context.delay_dwnl = 0;
    
    char timestamp[64];
    snprintf(timestamp, sizeof(timestamp), "%ld", (long)time(NULL));
    upgrade_context.lastrun = timestamp;
    upgrade_context.disableStatsUpdate = (char*)"false";
    upgrade_context.device_info = &device_info;
    
    int force_exit = 0;
    upgrade_context.force_exit = &force_exit;
    upgrade_context.trigger_type = TRIGGER_MANUAL;
    upgrade_context.rfc_list = &rfc_list;
    upgrade_context.progress_callback = NULL;
    upgrade_context.progress_callback_data = download_state;
    
    SWLOG_INFO("[DOWNLOAD_HANDLER] Calling rdkv_upgrade_request()...\n");
    
    void *curl_handle = NULL;
    int http_code = 0;
    int curl_ret_code = rdkv_upgrade_request(&upgrade_context, &curl_handle, &http_code);
    
    SWLOG_INFO("[DOWNLOAD_HANDLER] returned: curl=%d, http=%d\n", curl_ret_code, http_code);
    
    // Analyze result
    if (curl_ret_code == 0 && (http_code == 200 || http_code == 206)) {
        SWLOG_INFO("[DOWNLOAD_HANDLER] Download completed successfully!\n");
        
        if (!g_file_test(localFilePath, G_FILE_TEST_EXISTS)) {
            SWLOG_ERROR("[DOWNLOAD_HANDLER] ERROR: File not found after download\n");
            result.result_code = DOWNLOAD_ERROR;
            result.error_message = g_strdup("File not found after download");
            g_free(effective_url);
            return result;
        }
        
        // Download successful
        SWLOG_INFO("[DOWNLOAD_HANDLER] Download completed, file exists on disk\n");
        result.result_code = DOWNLOAD_SUCCESS;
    } else if (curl_ret_code == 0 && http_code == 404) {
        result.result_code = DOWNLOAD_NOT_FOUND;
        result.error_message = g_strdup("Firmware not found (HTTP 404)");
    } else if (curl_ret_code == 6) {
        result.result_code = DOWNLOAD_NETWORK_ERROR;
        result.error_message = g_strdup("DNS resolution failed");
    } else if (curl_ret_code == 7) {
        result.result_code = DOWNLOAD_NETWORK_ERROR;
        result.error_message = g_strdup("Connection failed");
    } else if (curl_ret_code == 28) {
        result.result_code = DOWNLOAD_NETWORK_ERROR;
        result.error_message = g_strdup("Timeout");
    } else {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Download failed (curl=%d, http=%d)", curl_ret_code, http_code);
        result.error_message = g_strdup(error_msg);
    }
    
    g_free(effective_url);
    return result;
}
