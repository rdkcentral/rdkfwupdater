#include "rdkFwupdateMgr_handlers.h"
#include "rdkv_cdl_log_wrapper.h"
#include "rdkv_cdl.h"           // For device_info, cur_img_detail
#include "json_process.h"       // For processJsonResponse
#include <string.h>

// External declarations for existing functions

//extern DeviceProperty_t device_info;
//extern ImageDetails_t cur_img_detail;

static int MakeXconfComms( XCONFRES *pResponse, int server_type, int *pHttp_code )
{
    DownloadData DwnLoc;
    char *pJSONStr = NULL;      // contains the device data string to send to XCONF server
    char *pServURL = NULL;      // the server to do the XCONF comms
    size_t len = 0;
    int ret = -1;

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

                    //context structure for XCONF upgrade request
                    RdkUpgradeContext_t xconf_context = {0};
                    xconf_context.upgrade_type = XCONF_UPGRADE;
                    xconf_context.server_type = server_type;
                    xconf_context.artifactLocationUrl = pServURL;
                    xconf_context.dwlloc = &DwnLoc;
                    xconf_context.pPostFields = pJSONStr;
                    xconf_context.immed_reboot_flag = immed_reboot_flag;
                    xconf_context.delay_dwnl = delay_dwnl;
                    xconf_context.lastrun = lastrun;
                    xconf_context.disableStatsUpdate = disableStatsUpdate;
                    xconf_context.device_info = &device_info;
                    xconf_context.force_exit = &force_exit;
                    xconf_context.trigger_type = trigger_type;
                    xconf_context.rfc_list = &rfc_list;

                    ret = rdkv_upgrade_request(&xconf_context, &curl, pHttp_code);
                    if( ret == 0 && *pHttp_code == 200 && DwnLoc.pvOut != NULL )
                    {
                        SWLOG_INFO( "MakeXconfComms: Calling getXconfRespData with input = %s\n", (char *)DwnLoc.pvOut );
                        ret = getXconfRespData( pResponse, (char *)DwnLoc.pvOut );
                        //Recovery completed event send for the success case
                        if( (filePresentCheck( RED_STATE_REBOOT ) == RDK_API_SUCCESS) ) {
                             SWLOG_INFO("%s : RED Recovery completed\n", __FUNCTION__);
                             eventManager(RED_STATE_EVENT, RED_RECOVERY_COMPLETED);
                             unlink(RED_STATE_REBOOT);
                        }
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


int rdkFwupdateMgr_checkForUpdate(const gchar *handler_id,
                                  const gchar *current_version,
                                  gchar **available_version,
                                  gchar **update_details,
                                  gchar **status) {
    
    SWLOG_INFO("[rdkFwupdateMgr] CheckForUpdate: handler=%s, version=%s",handler_id, current_version);
    
    // Initialize outputs
    *available_version = NULL;
    *update_details = NULL;
    *status = NULL;
    
    // Use existing XConf communication function
    XCONFRES response = {0};
    int http_code = 0;
    int server_type = 1;  // HTTP server type
    
    // Real XConf communication
    int ret = MakeXconfComms(&response, server_type, &http_code);
    
    SWLOG_INFO("[rdkFwupdateMgr] XConf call result: ret=%d, http_code=%d", 
               ret, http_code);
    
    if (ret == 0 && http_code == 200) {
        // Use existing JSON processing
        int json_res = processJsonResponse(&response, current_version, 
                                         device_info.model, device_info.maint_status);
        
        SWLOG_INFO("[rdkFwupdateMgr] JSON processing result: %d", json_res);
        
        if (json_res == 0) {
            // Update available
            *available_version = g_strdup(response.imageVersion ? response.imageVersion : "");
            *update_details = g_strdup(response.firmwareDownloadURL ? response.firmwareDownloadURL : "");
            *status = g_strdup("UPDATE_AVAILABLE");
            
            SWLOG_INFO("[rdkFwupdateMgr] Update available: %s", *available_version);
            return 0;
        } else {
            // No update or processing failed
            *available_version = g_strdup("");
            *update_details = g_strdup("");
            *status = g_strdup("UPDATE_NOT_AVAILABLE");
            
            SWLOG_INFO("[rdkFwupdateMgr] No update available");
            return 0;
        }
    } else {
        // XConf communication failed
        *available_version = g_strdup("");
        *update_details = g_strdup("");
        *status = g_strdup("UPDATE_ERROR");
        
        SWLOG_ERROR("[rdkFwupdateMgr] XConf communication failed: ret=%d, http=%d", 
                    ret, http_code);
        return -1;
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
