#include "rdkFwupdateMgr_handlers.h"
#include "rdkv_cdl_log_wrapper.h"
#include "rdkv_cdl.h"           // For device_info, cur_img_detail
#include "json_process.h"       // For processJsonResponse
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
#include "rdkv_upgrade.h"      // For RdkUpgradeContext_t and rdkv_upgrade_request
#include "device_api.h"        // For device information functions
#include "iarmInterface.h"     // For RED_RECOVERY_COMPLETED and eventManager
#include "rfcinterface.h"      // For getRFCSettings and Rfc_t
#include <string.h>

// Constants needed for daemon context
#define JSON_STR_LEN        1000
#define URL_MAX_LEN         512

// External declarations for existing functions and variables
// In daemon mode, we need to manage these differently than monolithic binary
extern DeviceProperty_t device_info;
extern ImageDetails_t cur_img_detail;

static int MakeXconfComms( XCONFRES *pResponse, int server_type, int *pHttp_code )
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
                SWLOG_INFO( "MakeXconfComms: server URL %s\n", pServURL );
                if( len )
                {
                    len = createJsonString( pJSONStr, JSON_STR_LEN );

                    //context structure for XCONF upgrade request - daemon mode initialization
                    RdkUpgradeContext_t xconf_context = {0};
                    xconf_context.upgrade_type = XCONF_UPGRADE;
                    xconf_context.server_type = server_type;
                    xconf_context.artifactLocationUrl = pServURL;
                    xconf_context.dwlloc = &DwnLoc;
                    xconf_context.pPostFields = pJSONStr;
                    
                    // For daemon mode, read REAL system configuration
                    // Unlike monolithic binary, daemon reads fresh config per request
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
                    xconf_context.rfc_list = &local_rfc_list;      // NOW USES REAL RFC DATA

                    ret = rdkv_upgrade_request(&xconf_context, &curl, pHttp_code);
                    if( ret == 0 && *pHttp_code == 200 && DwnLoc.pvOut != NULL )
                    {
                        SWLOG_INFO( "MakeXconfComms: Calling getXconfRespData with input = %s\n", (char *)DwnLoc.pvOut );
                        ret = getXconfRespData( pResponse, (char *)DwnLoc.pvOut );
                        
                        // Recovery completed event handling - daemon mode
                        #ifndef GTEST_ENABLE
                        if( (filePresentCheck( RED_STATE_REBOOT ) == RDK_API_SUCCESS) ) {
                             SWLOG_INFO("%s : RED Recovery completed\n", __FUNCTION__);
                             eventManager(RED_STATE_EVENT, RED_RECOVERY_COMPLETED);
                             unlink(RED_STATE_REBOOT);
                        }
                        #endif
                    }
                }
                else
                {
                    SWLOG_ERROR( "MakeXconfComms: no valid server URL\n" );
                }
                free( pServURL );
            }
            else
            {
                SWLOG_ERROR("MakeXconfComms: Failed malloc for server URL of %d bytes\n", URL_MAX_LEN );
            }
            free( pJSONStr );
        }
        else
        {
            SWLOG_ERROR("MakeXconfComms: Failed malloc for json string of %d bytes\n", JSON_STR_LEN );
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
    response.result_code = UPDATE_AVAILABLE;
    response.available_version = g_strdup(available_version ? available_version : "");
    response.update_details = g_strdup(update_details ? update_details : "");
    response.status_message = g_strdup(status_message ? status_message : "UPDATE_AVAILABLE");
    return response;
}

// Helper function to create error/no-update response
static CheckUpdateResponse create_result_response(CheckForUpdateResult result_code,
                                                  const gchar *status_message) {
    CheckUpdateResponse response = {0};
    response.result_code = result_code;
    response.available_version = g_strdup("");
    response.update_details = g_strdup("");
    response.status_message = g_strdup(status_message ? status_message : "");
    return response;
}

CheckUpdateResponse rdkFwupdateMgr_checkForUpdate(const gchar *handler_id,
                                                  const gchar *current_version) {
    
    SWLOG_INFO("[rdkFwupdateMgr] CheckForUpdate: handler=%s\n",handler_id);
    
    // Use existing XConf communication function
    XCONFRES response = {0};
    int http_code = 0;
    int server_type = 1;  // HTTP server type
    
    // Real XConf communication
    int ret = MakeXconfComms(&response, server_type, &http_code);
    
    SWLOG_INFO("[rdkFwupdateMgr] XConf call result: ret=%d, http_code=%d", 
               ret, http_code);
    
    if (ret == 0 && http_code == 200) {
        // Log what XConf returned before processing
        SWLOG_INFO("[rdkFwupdateMgr] XConf Response - FW Version: '%s', File: '%s', Location: '%s'",
                   response.cloudFWVersion[0] ? response.cloudFWVersion : "(empty)",
                   response.cloudFWFile[0] ? response.cloudFWFile : "(empty)", 
                   response.cloudFWLocation[0] ? response.cloudFWLocation : "(empty)");
        
        // Use existing JSON processing
        int json_res = processJsonResponse(&response, current_version, 
                                         device_info.model, device_info.maint_status);
        
        SWLOG_INFO("[rdkFwupdateMgr] JSON processing result: %d", json_res);
        
        if (json_res == 0) {
            // Update available - create comprehensive success response with full XConf data
            SWLOG_INFO("[rdkFwupdateMgr] Update available: %s", 
                      response.cloudFWVersion[0] ? response.cloudFWVersion : "Unknown");
            
            // Create detailed update information from XConf response
            gchar *update_details = g_strdup_printf(
                "File:%s|Location:%s|Protocol:%s|Reboot:%s|Delay:%s|PDRI:%s", 
                response.cloudFWFile[0] ? response.cloudFWFile : "N/A",
                response.cloudFWLocation[0] ? response.cloudFWLocation : "N/A", 
                response.cloudProto[0] ? response.cloudProto : "HTTP",
                response.cloudImmediateRebootFlag[0] ? response.cloudImmediateRebootFlag : "false",
                response.cloudDelayDownload[0] ? response.cloudDelayDownload : "0",
                response.cloudPDRIVersion[0] ? response.cloudPDRIVersion : "N/A"
            );
            
            CheckUpdateResponse result = create_success_response(
                response.cloudFWVersion[0] ? response.cloudFWVersion : "Unknown",
                update_details,
                "UPDATE_AVAILABLE"
            );
            
            g_free(update_details);
            return result;
        } else {
            // No update or processing failed
            if (response.cloudFWVersion[0]) {
                // XConf returned a version but processJsonResponse rejected it
                SWLOG_INFO("[rdkFwupdateMgr] Update rejected - Cloud version: %s, Current: %s", 
                          response.cloudFWVersion, current_version);
                
                // Check if versions are the same
                if (strcmp(response.cloudFWVersion, current_version) == 0) {
                    return create_result_response(UPDATE_NOT_AVAILABLE, 
                        "UPDATE_NOT_AVAILABLE: Already on latest version");
                } else {
                    return create_result_response(UPDATE_NOT_AVAILABLE, 
                        "UPDATE_NOT_AVAILABLE: Image not valid for this device model");
                }
            } else {
                // XConf didn't return any version
                SWLOG_INFO("[rdkFwupdateMgr] No firmware version in XConf response");
                return create_result_response(UPDATE_NOT_AVAILABLE, 
                    "UPDATE_NOT_AVAILABLE: No firmware configured for this device");
            }
        }
    } else {
        // XConf communication failed
        SWLOG_ERROR("[rdkFwupdateMgr] XConf communication failed: ret=%d, http=%d", 
                    ret, http_code);
        return create_result_response(UPDATE_ERROR, "UPDATE_ERROR");
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
