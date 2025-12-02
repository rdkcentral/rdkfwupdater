#include "rdkFwupdateMgr_handlers.h"
#include "rdkv_cdl_log_wrapper.h"
#include "rdkv_cdl.h"           // For device_info, cur_img_detail
#include "json_process.h"       // For processJsonResponse
#include "device_status_helper.h" // For currentImg function
#ifndef GTEST_ENABLE
#include "downloadUtil.h"       // For DownloadData and related functions
#include "urlHelper.h"         // For DownloadData, FileDwnl_t, MtlsAuth_t types
#include "common_device_api.h" // For DeviceProperty_t, ImageDetails_t
#include "rdk_fwdl_utils.h"    // For system utility functions
#include "system_utils.h"      // For system utility functions
#else
#include "miscellaneous.h"     // For test context
#endif
#include "deviceutils.h"       // For DEFAULT_DL_ALLOC and other constants
#include "rdkv_cdl.h"          // For HTTP server type constants
#include "rdkv_upgrade.h"      // For RdkUpgradeContext_t and rdkv_upgrade_request
#include "device_api.h"        // For device information functions
#include "iarmInterface.h"     // For RED_RECOVERY_COMPLETED and eventManager
#include "rfcinterface.h"      // For getRFCSettings and Rfc_t
#include <string.h>

// Constants needed for daemon context
#define JSON_STR_LEN        1000
#define URL_MAX_LEN         512

// Cache file paths for XConf response persistence
#define XCONF_CACHE_FILE        "/tmp/xconf_response_thunder.txt"
#define XCONF_HTTP_CODE_FILE    "/tmp/xconf_httpcode_thunder.txt"
#define XCONF_PROGRESS_FILE     "/tmp/xconf_curl_progress_thunder"
#define RED_STATE_FILE          "/lib/rdk/stateRedRecovery.sh"

// External declarations for existing functions and variables
// In daemon mode, we need to manage these differently than monolithic binary
extern DeviceProperty_t device_info;
extern ImageDetails_t cur_img_detail;

// Cache utility functions for XConf response persistence
gboolean xconf_cache_exists(void) {
    return g_file_test(XCONF_CACHE_FILE, G_FILE_TEST_EXISTS);
}

static gboolean load_xconf_from_cache(XCONFRES *pResponse) {
    gchar *cache_content = NULL;
    gsize length;
    GError *error = NULL;
    gboolean result = FALSE;
    
    SWLOG_INFO("[CACHE] Loading XConf data from cache file: %s\n", XCONF_CACHE_FILE);
    
    if (!g_file_get_contents(XCONF_CACHE_FILE, &cache_content, &length, &error)) {
        SWLOG_ERROR("[CACHE] Failed to read cache file: %s\n", error ? error->message : "Unknown error");
        if (error) g_error_free(error);
        return FALSE;
    }
    
    SWLOG_INFO("[CACHE] Cache file loaded successfully (%zu bytes)\n", length);
    SWLOG_INFO("[CACHE] Cache content: %s\n", cache_content);
    
    // Parse the cached JSON response using existing parser
    int parse_result = getXconfRespData(pResponse, cache_content);
    if (parse_result == 0) {
        SWLOG_INFO("[CACHE] Successfully parsed cached XConf data\n");
        SWLOG_INFO("[CACHE]   - firmwareVersion: '%s'\n", pResponse->cloudFWVersion);
        SWLOG_INFO("[CACHE]   - firmwareFilename: '%s'\n", pResponse->cloudFWFile);
        SWLOG_INFO("[CACHE]   - firmwareLocation: '%s'\n", pResponse->cloudFWLocation);
        result = TRUE;
    } else {
        SWLOG_ERROR("[CACHE] Failed to parse cached XConf data (error: %d)\n", parse_result);
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

// Helper function to free CheckUpdateResponse memory
void checkupdate_response_free(CheckUpdateResponse *response) {
    if (response) {
        g_free(response->available_version);
        g_free(response->update_details);
        g_free(response->status_message);
        // Clear pointers to prevent double-free
        response->available_version = NULL;
        response->update_details = NULL;
        response->status_message = NULL;
    }
}

// Helper function to create success response
static CheckUpdateResponse create_success_response(const gchar *available_version,
                                                   const gchar *update_details,
                                                   const gchar *status_message) {
    CheckUpdateResponse response = {0};
    char current_img_buffer[64] = {0};
    
    // Get the current running image version using currentImg function
    bool img_status = GetFirmwareVersion(current_img_buffer, sizeof(current_img_buffer));
    
    SWLOG_INFO("[rdkFwupdateMgr] create_success_response: Getting current image info\n");
    SWLOG_INFO("[rdkFwupdateMgr]   - currentImg status: %s\n", img_status ? "SUCCESS" : "FAILED");
    SWLOG_INFO("[rdkFwupdateMgr]   - current_img_buffer: '%s'\n", current_img_buffer);

    // Compare versions only if both are non-null
    gboolean is_update_available = img_status && available_version && (g_strcmp0(current_img_buffer, available_version) != 0);
    if (is_update_available) {
    response.result_code = UPDATE_AVAILABLE;
    response.current_img_version = g_strdup(img_status ? current_img_buffer : "Unknown");
    response.available_version = g_strdup(available_version ? available_version : "");
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

// Helper function to create error/no-update response
static CheckUpdateResponse create_result_response(CheckForUpdateResult result_code,
                                                  const gchar *status_message) {
    CheckUpdateResponse response = {0};
    char current_img_buffer[256] = {0};
    
    // Get the current running image version using currentImg function
    bool img_status = GetFirmwareVersion(current_img_buffer, sizeof(current_img_buffer));
    
    SWLOG_INFO("[rdkFwupdateMgr] create_result_response: Getting current image info\n");
    SWLOG_INFO("[rdkFwupdateMgr]   - currentImg status: %s\n", img_status ? "SUCCESS" : "FAILED");
    SWLOG_INFO("[rdkFwupdateMgr]   - current_img_buffer: '%s'\n", current_img_buffer);
    
    response.result_code = result_code;
    response.current_img_version = g_strdup(img_status ? current_img_buffer : "Unknown");
    response.available_version = g_strdup("");
    response.update_details = g_strdup("");
    
    // Set appropriate status string based on result code
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

CheckUpdateResponse rdkFwupdateMgr_checkForUpdate(const gchar *handler_id) {
    
    // FIRST LOG - Proves function was entered
    SWLOG_INFO("[rdkFwupdateMgr] ===== FUNCTION ENTRY: rdkFwupdateMgr_checkForUpdate() =====\n");
    
    // CRITICAL: Add NULL check first to prevent segfault
    if (!handler_id) {
        SWLOG_ERROR("[rdkFwupdateMgr] CRITICAL ERROR: handler_id is NULL!\n");
        return create_result_response(UPDATE_ERROR, "Internal error - invalid handler ID");
    }
    
    SWLOG_INFO("[rdkFwupdateMgr] CheckForUpdate: handler=%s\n", handler_id);
    SWLOG_INFO("[rdkFwupdateMgr] About to allocate XCONFRES structure on stack (~2KB)...\n");
    
    // Use existing XConf communication function
    XCONFRES response = {0};
    
    SWLOG_INFO("[rdkFwupdateMgr] XCONFRES structure allocated successfully\n");
    int http_code = 0;
    int server_type = HTTP_XCONF_DIRECT;  // XConf query mode (2), not download mode

    //  XConf communication with caching support
    int ret = -1;
    
    SWLOG_INFO("[rdkFwupdateMgr] CheckForUpdate: Checking for cached XConf data...\n");
    
    // Try to load from cache first
    if (xconf_cache_exists()) {
        SWLOG_INFO("[rdkFwupdateMgr] Cache hit! Loading XConf data from cache\n");
        if (load_xconf_from_cache(&response)) {
            ret = 0;
            http_code = 200;  // Simulate successful HTTP response from cache
            SWLOG_INFO("[rdkFwupdateMgr] Successfully loaded XConf data from cache\n");
        } else {
            SWLOG_ERROR("[rdkFwupdateMgr] Cache read failed, falling back to live XConf call\n");
            ret = fetch_xconf_firmware_info(&response, server_type, &http_code);
        }
    } else {
        SWLOG_INFO("[rdkFwupdateMgr] Cache miss! Making live XConf call\n");
        ret = fetch_xconf_firmware_info(&response, server_type, &http_code);
        
        // Save successful response to cache for future use
        if (ret == 0 && http_code == 200) {
            SWLOG_INFO("[rdkFwupdateMgr] XConf call successful - cache save will be implemented in fetch_xconf_firmware_info\n");
        }
    }
    
    SWLOG_INFO("[rdkFwupdateMgr] XConf call completed with result: ret=%d\n",ret);
    
    if (ret == 0 && http_code == 200) {
        // Comprehensive logging of all XConf response variables
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
       

       // Create detailed update information from XConf response
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
        
        // Check if we actually received a firmware version from XConf
        if (response.cloudFWVersion[0] && strlen(response.cloudFWVersion) > 0) {
            SWLOG_INFO("[rdkFwupdateMgr] XConf returned firmware version: '%s'\n", response.cloudFWVersion);
            
            // Create success response with firmware update available
            CheckUpdateResponse result = create_success_response(
                response.cloudFWVersion,
                update_details,
                "Firmware update available"
            );
            
            g_free(update_details);
            return result;
        } else {
            SWLOG_INFO("[rdkFwupdateMgr] XConf returned no firmware version - no update available\n");
            
            // No update available - XConf communication succeeded but no new firmware
            g_free(update_details);
            return create_result_response(UPDATE_NOT_AVAILABLE, "No firmware update available");
        }
    } else {
        // XConf communication failed
        SWLOG_ERROR("[rdkFwupdateMgr] XConf communication failed: ret=%d, http=%d\n", ret, http_code);
        
        if (http_code != 200) {
            return create_result_response(UPDATE_ERROR, "Network error - unable to reach update server");
        } else {
            return create_result_response(UPDATE_ERROR, "Update check failed - server communication error");
        }
    }
}

// TODO: Other wrapper functions will be implemented in subsequent subtasks
int rdkFwupdateMgr_downloadFirmware(const gchar *handler_id,
                                    const gchar *image_name,
                                    const gchar *available_version,
                                    gchar **download_status,
                                    gchar **download_path) {
    // Placeholder for Subtask 2
    *download_status = g_strdup("RDKFW_FAILED");
    *download_path = g_strdup("");
    SWLOG_INFO("[rdkFwupdateMgr] downloadFirmware: Not implemented yet");
    return -1;
}

// ... other functions as placeholders for now
