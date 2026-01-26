#include "rdkv_upgrade.h"
#include "rdkv_cdl_log_wrapper.h"
#include "device_status_helper.h"
#include "download_status_helper.h"
#include "iarmInterface.h"
#include "rfcinterface.h"
#include "deviceutils.h"
#ifndef GTEST_ENABLE
//FIX for ../src/rdkv_upgrade.c:8:10: fatal error: rdk_logger_milestone.h: No such file or directory 
#include "rdk_logger_milestone.h"
#include "codebigUtils.h"
#include "mtlsUtils.h"
#include "downloadUtil.h"
#include "system_utils.h"
#endif
#include "flash.h"

/* Description: Use for save process id and store inside file.
 * @param: file: file name to save pid
 * @param: data: data to save inside file.
 * @return: bool
 * */
bool savePID(const char *file, char *data)
{
    bool status = false;
    FILE *fp = NULL;

    if ((file != NULL) && (data != NULL)) {
        fp = fopen(file, "w");
        if(fp == NULL) {
            SWLOG_INFO("savePID() %s unable to create file=\n", file);
        }else {
            SWLOG_INFO("savePID() =%s to file=%s\n", data, file);
            fputs(data, fp);
            fclose(fp);
            status = true;
        }
    }
    return status;
}


/* Description: Use for get process id and store inside file.
 * @param: device : device type
 * @param: maint_window : status maintenance manager support
 * @return: void
 * TODO: Need to remove full function once all implementation complete
 * */
void getPidStore(const char *device, const char *maint_window) {
    pid_t pid;
    char data[16] = { 0 };
    if (device == NULL || maint_window == NULL) {
        SWLOG_ERROR("getPidStore() parameter is NULL\n");
        return;
    }
    pid = getpid();
    snprintf(data,sizeof(data), "%u\n", pid);
    SWLOG_INFO("getPidStore() pid=%u in string=%s\n", pid, data);
    savePID(CURL_PID_FILE, data);
    savePID(FWDNLD_PID_FILE, data);
}


bool checkForTlsErrors(int curl_code, const char *type)
{
    if (type == NULL) {
        SWLOG_ERROR("%s : type parameter is NULL and curl error=%d\n", __FUNCTION__, curl_code);
        return false;
    }
    if((curl_code == 35) || (curl_code == 51) || (curl_code == 53) || (curl_code == 54) || (curl_code == 58) || (curl_code == 59) || (curl_code == 60)
            || (curl_code == 64) || (curl_code == 66) || (curl_code == 77) || (curl_code == 80) || (curl_code == 82) || (curl_code == 83)
            || (curl_code == 90) || (curl_code == 91)) {
        TLSLOG(TLS_LOG_ERR, "HTTPS %s failed to connect to %s server with curl error code %d", TLS, type, curl_code);
    }
    return true;
}


/* Description:Use for store download error and send telemetry
 * @param: curl_code : curl return status
 * @param: http_code : http return status
 * @return void:
 * */
void dwnlError(int curl_code, int http_code, int server_type,const DeviceProperty_t *device_info,const char *lastrun,char *disableStatsUpdate)
{
    char telemetry_data[32];
    char device_type[32];
    struct FWDownloadStatus fwdls;
    char failureReason[128];
    char *type = "Direct"; //TODO: Need to pass this type as a function parameter

    if (device_info == NULL) {
        SWLOG_ERROR("%s: device_info parameter is NULL\n", __FUNCTION__);
        return;
    }

    *failureReason = 0;
    if(curl_code == 22) {
        snprintf(telemetry_data, sizeof(telemetry_data), "swdl_failed");
        Upgradet2CountNotify(telemetry_data, 1);
    }else if(curl_code == 18 || curl_code == 7) {
        snprintf(telemetry_data, sizeof(telemetry_data), "swdl_failed_%d", curl_code);
        Upgradet2CountNotify(telemetry_data, 1);
    }else {
        *telemetry_data = 0;
        SWLOG_ERROR("%s : CDL is suspended due to Curl %d Error\n", __FUNCTION__, curl_code);
        Upgradet2CountNotify("CDLsuspended_split", curl_code);
    }
    checkForTlsErrors(curl_code, type);
    snprintf( device_type, sizeof(device_type), "%s", device_info->dev_type );
    if(curl_code != 0 || (http_code != 200 && http_code != 206) || http_code == 495) {
        if (server_type == HTTP_SSR_DIRECT) {
            SWLOG_ERROR("%s : Failed to download image from normal SSR code download server with ret:%d, httpcode:%d\n", __FUNCTION__, curl_code, http_code);
            Upgradet2CountNotify("SYST_ERR_cdl_ssr", 1);
            if (http_code == 302)
            {
                Upgradet2CountNotify("SYST_INFO_Http302", 1);
            }
        }
        if((strcmp(device_type, "mediaclient")) == 0) {
            if(http_code == 0) {
                snprintf( failureReason, sizeof(failureReason), "FailureReason|Image Download Failed -Unable to connect\n" );
            }else if(http_code == 404) {
                snprintf( failureReason, sizeof(failureReason), "FailureReason|Image Download Failed -Server not Found\n" );
            }else if(http_code == 495) {
+                snprintf( failureReason, sizeof(failureReason), "FailureReason|Image Download Failed -Client certificate expired\n" );
            }else if(http_code >= 500 && http_code <= 511) {
                snprintf( failureReason, sizeof(failureReason), "FailureReason|Image Download Failed -Error response from server\n" );
            }else {
                snprintf( failureReason, sizeof(failureReason), "FailureReason|Image Download Failed - Unknown\n" );
            }
            //updateFWDownloadStatus "$cloudProto" "Failure" "$cloudImmediateRebootFlag" "$failureReason" "$dnldFWVersion" "$UPGRADE_FILE" "$runtime" "Failed" "$DelayDownloadXconf"
            //eventManager $ImageDwldEvent $IMAGE_FWDNLD_DOWNLOAD_FAILED
            eventManager(IMG_DWL_EVENT, IMAGE_FWDNLD_DOWNLOAD_FAILED);
        }else {
            if((http_code == 0) || (http_code == 495)) {
                snprintf( failureReason, sizeof(failureReason), "FailureReason|ESTB Download Failure");
            }
            //updateFWDownloadStatus "$cloudProto" "Failure" "$cloudImmediateRebootFlag" "$failureReason" "$dnldFWVersion" "$UPGRADE_FILE" "$runtime" "Failed" "$DelayDownloadXconf"
            //eventManager $ImageDwldEvent $IMAGE_FWDNLD_DOWNLOAD_FAILED
            eventManager(IMG_DWL_EVENT, IMAGE_FWDNLD_DOWNLOAD_FAILED);
        }

        snprintf(fwdls.method, sizeof(fwdls.method), "Method|xconf\n");
        snprintf(fwdls.proto, sizeof(fwdls.proto), "Proto|http\n");
        snprintf(fwdls.status, sizeof(fwdls.status), "Status|Failure\n");
        snprintf(fwdls.reboot, sizeof(fwdls.reboot), "Reboot|false\n");
        snprintf(fwdls.failureReason, sizeof(fwdls.failureReason), "%s", failureReason);
        snprintf(fwdls.dnldVersn, sizeof(fwdls.dnldVersn), "DnldVersn|\n");
        snprintf(fwdls.dnldfile, sizeof(fwdls.dnldfile), "DnldFile|\n");
        snprintf(fwdls.dnldurl, sizeof(fwdls.dnldurl), "DnldURL|\n");
        snprintf(fwdls.lastrun, sizeof(fwdls.lastrun), "LastRun|%s\n", lastrun); // lastrun his data should come from script as a argument
        snprintf(fwdls.FwUpdateState, sizeof(fwdls.FwUpdateState), "FwUpdateState|Failed\n");
        snprintf(fwdls.DelayDownload, sizeof(fwdls.DelayDownload), "DelayDownload|\n"); // This data should come from script as a argument
        updateFWDownloadStatus(&fwdls, disableStatsUpdate);
    }
    // HTTP CODE 495 - Expired client certificate not in servers allow list
    if( http_code == 495 ) {
        SWLOG_INFO("%s : Calling checkAndEnterStateRed() with code:%d\n", __FUNCTION__, http_code);
        checkAndEnterStateRed(http_code, disableStatsUpdate);
    }else {
        SWLOG_INFO("%s : Calling checkAndEnterStateRed() with code:%d\n", __FUNCTION__, curl_code);
        checkAndEnterStateRed(curl_code, disableStatsUpdate);
    }
}

/* Description: Save http value inside file
 * @param: http_code : http value after curl command return
 * @return: void
 * */
void saveHTTPCode(int http_code,const char *lastrun)
{
    char http[8] = { 0 };
    FILE *fp = NULL;

    snprintf( http, sizeof(http), "%03ld\n", (long int)http_code );
    fp = fopen(HTTP_CODE_FILE, "w");
    if(fp == NULL) {
        SWLOG_ERROR("%s : fopen failed:%s\n", __FUNCTION__, HTTP_CODE_FILE);
    }else {
        SWLOG_INFO("saveHTTPCode() Writing httpcode=%s to file:%s\n", http, HTTP_CODE_FILE);
        fputs(http, fp);
        fclose(fp);
    }
}

// TODO - do similar to what is done for IARM eventing. Is not the primary goal of this module
/* Description: Use for sending telemetry Log
 * @param marker: use for send marker details
 * @return : void
 * */
void Upgradet2CountNotify(char *marker, int val) {
#ifdef T2_EVENT_ENABLED
    t2_event_d(marker, val);
#endif
}

void Upgradet2ValNotify( char *marker, char *val )
{
#ifdef T2_EVENT_ENABLED
    t2_event_s(marker, val);
#endif
}

// TODO: Use following function for all types of downloads when needed for telemetry v2 logs
bool checkt2ValNotify( int iCurlCode, int iUpgradeType, char *Url  )
{
    char *pStartString = "CERTERR, ";
    int inum;
    char fqdn[100];
    char outbuf[sizeof(fqdn) + 50];
    char fmt[25];
    bool bRet= false;

    *fqdn = 0;
    snprintf( fmt, sizeof(fmt), "https://%%%zu[^/?]", sizeof(fqdn) - 1 );
    inum = sscanf( Url, fmt, fqdn );
    switch( iCurlCode )
    {
       case 35:
       case 51:
       case 53:
       case 54:
       case 58:
       case 59:
       case 60:
       case 64:
       case 66:
       case 77:
       case 80:
       case 82:
       case 83:
       case 90:
       case 91:
           if( inum == 1 )
           {
               if( iUpgradeType == PERIPHERAL_UPGRADE )
               {
                   snprintf( outbuf, sizeof(outbuf), "%sPCDL, %d, %s", pStartString, iCurlCode, fqdn );
                   TLSLOG(TLS_LOG_ERR, "%s", outbuf );
                   Upgradet2ValNotify( "certerr_split", outbuf + strlen(pStartString) );    // point to 'P' in PCDL
                   bRet = true;
               }
           }
           break;

       default:
           break;
    }
    return bRet;
}

/**
 * @brief Main firmware upgrade request function (moved from rdkv_main.c)
 * 
 * This function was extracted from rdkv_main.c to create a modular library.
 * It handles all types of firmware upgrade requests including XCONF, PCI, 
 * PDRI, and peripheral upgrades. It manages download coordination, status 
 * reporting, and error handling.
 */
int rdkv_upgrade_request(const RdkUpgradeContext_t* context, void** curl, int* pHttp_code)
{
    const char* dwlpath_filename = NULL;
    int ret_curl_code = -1;
    char dwnl_status[64];
    unsigned long int curtime;
    char current_time[64];
    char *dev_prop_name = "CPU_ARCH";
    char cpu_arch[8] = {0};
    char *cmd_args = "FWDNLD_STARTED";
    char md5_sum[128] = {0};
    bool st_notify_flag = false;
    int ret = -1;
    struct FWDownloadStatus fwdls;
    FILE *fp = NULL;
    int flash_status = -1;

    // Comprehensive logging: entry and pointers
    SWLOG_INFO("[UPGRADE_REQ] Enter %s: context=%p, curl=%p, pHttp_code=%p\n", __FUNCTION__, (void*)context, (void*)curl, (void*)pHttp_code);

    if (context == NULL) {
        SWLOG_ERROR("[UPGRADE_REQ] CRITICAL: context parameter is NULL\n");
        return ret_curl_code;
    }
    if (curl == NULL || *curl == NULL) {
        SWLOG_ERROR("[UPGRADE_REQ] CRITICAL: curl parameter is NULL\n");
        return ret_curl_code;
    }
    if (pHttp_code == NULL) {
        SWLOG_ERROR("[UPGRADE_REQ] CRITICAL: pHttp_code parameter is NULL\n");
        return ret_curl_code;
    }

    int upgrade_type = context->upgrade_type;
    int server_type = context->server_type;
    const char* artifactLocationUrl = context->artifactLocationUrl;
    const void* dwlloc = context->dwlloc;
    char* pPostFields = context->pPostFields;
    const char* immed_reboot_flag = context->immed_reboot_flag;
    int delay_dwnl = context->delay_dwnl;
    const char* lastrun = context->lastrun;
    char* disableStatsUpdate = context->disableStatsUpdate;
    const DeviceProperty_t* device_info = context->device_info;
    int* force_exit = context->force_exit;
    int trigger_type = context->trigger_type;
    const Rfc_t* rfc_list = context->rfc_list;

    // Log context contents safely
    SWLOG_INFO("[UPGRADE_REQ] context values: upgrade_type=%d, server_type=%d, artifactLocationUrl='%s', dwlloc=%p\n",
               upgrade_type, server_type, artifactLocationUrl ? artifactLocationUrl : "(null)", dwlloc);
    SWLOG_INFO("[UPGRADE_REQ] context (continued): pPostFields=%p, immed_reboot_flag='%s', delay_dwnl=%d, lastrun='%s'\n",
               (void*)pPostFields, immed_reboot_flag ? immed_reboot_flag : "(null)", delay_dwnl, lastrun ? lastrun : "(null)");
    SWLOG_INFO("[UPGRADE_REQ] context (continued): disableStatsUpdate=%p, device_info=%p, force_exit=%p, trigger_type=%d, rfc_list=%p\n",
               (void*)disableStatsUpdate, (void*)device_info, (void*)force_exit, trigger_type, (void*)rfc_list);

    if (device_info != NULL) {
        SWLOG_INFO("[UPGRADE_REQ] device_info: dev_name='%s', model='%s', maint_status='%s'\n",
                   device_info->dev_name ? device_info->dev_name : "(null)",
                   device_info->model ? device_info->model : "(null)",
                   device_info->maint_status ? device_info->maint_status : "(null)");
    }
    if (rfc_list != NULL) {
        SWLOG_INFO("[UPGRADE_REQ] rfc_list: rfc_throttle='%s', rfc_topspeed='%s'\n",
                   rfc_list->rfc_throttle ? rfc_list->rfc_throttle : "(null)",
                   rfc_list->rfc_topspeed ? rfc_list->rfc_topspeed : "(null)");
    }

    if (artifactLocationUrl == NULL || dwlloc == NULL) {
        SWLOG_ERROR("%s: Parameter is NULL\n", __FUNCTION__);
        return ret_curl_code;
    }
    if (upgrade_type == XCONF_UPGRADE) {
        SWLOG_INFO("Trying to communicate with XCONF server\n");
        Upgradet2CountNotify("SYST_INFO_XCONFConnect", 1);
    }
    *pHttp_code = 0;

    if( isDwnlBlock(server_type) )     // download is blocked
    {
        if( server_type == HTTP_XCONF_DIRECT )
        {
            server_type = HTTP_XCONF_CODEBIG;
        }
        else if( server_type == HTTP_XCONF_CODEBIG )
        {
            server_type = HTTP_XCONF_DIRECT;
        }
        else if( server_type == HTTP_SSR_DIRECT )
        {
            server_type = HTTP_SSR_CODEBIG;
        }
        else if( server_type == HTTP_SSR_CODEBIG )
        {
            server_type = HTTP_SSR_DIRECT;
        }

        if( isDwnlBlock(server_type) )
        {
            ret_curl_code = DWNL_BLOCK;
        }
    }

    if( ret_curl_code != DWNL_BLOCK )   // no point in continuing if both download types are blocked
    {
        if( server_type == HTTP_SSR_DIRECT || server_type == HTTP_SSR_CODEBIG )
        {
            dwlpath_filename = (const char*)dwlloc;
            if (upgrade_type == PDRI_UPGRADE && (false == checkPDRIUpgrade(dwlpath_filename))) {
                ret_curl_code = 100;
                return ret_curl_code;
            }
            updateUpgradeFlag(1);
        }

        if (upgrade_type == PCI_UPGRADE || upgrade_type == PDRI_UPGRADE) {
            st_notify_flag = isMmgbleNotifyEnabled();
        }

        isDelayFWDownloadActive(delay_dwnl, device_info->maint_status, 1);
	SWLOG_INFO("Delayed Trigger Image Upgrade ..!\n");
        if (upgrade_type == PCI_UPGRADE) {
            logMilestone(cmd_args);
        }else if(upgrade_type == XCONF_UPGRADE) {
            cmd_args = "CONNECT_TO_XCONF_CDL";
            logMilestone(cmd_args);
        }

        if (upgrade_type == PDRI_UPGRADE) {
            SWLOG_INFO("Triggering the Image Download ...\n");
            Upgradet2CountNotify("SYS_INFO_swdltriggered", 1);
            SWLOG_INFO("PDRI Download in Progress for %s\n", dwlpath_filename);
            eventManager(IMG_DWL_EVENT, IMAGE_FWDNLD_UNINITIALIZED);
        }else if(upgrade_type == PCI_UPGRADE) {
            SWLOG_INFO("Triggering the Image Download ...\n");
            Upgradet2CountNotify("SYS_INFO_swdltriggered", 1);
            SWLOG_INFO("PCI Download in Progress for %s\n", dwlpath_filename);
            eventManager(IMG_DWL_EVENT, IMAGE_FWDNLD_UNINITIALIZED);
        }else if(upgrade_type == PERIPHERAL_UPGRADE) {
            SWLOG_INFO( "Trying to download %s\n", (char*)dwlloc );
        }else {
            SWLOG_INFO("XCONF Download in Progress\n");
        }

        (false == (isMediaClientDevice()))? snprintf(dwnl_status, sizeof(dwnl_status),"ESTB in progress"):snprintf(dwnl_status, sizeof(dwnl_status), "Download In Progress");
        
        if (upgrade_type == PCI_UPGRADE || upgrade_type == PDRI_UPGRADE) {
            eventManager(FW_STATE_EVENT, FW_STATE_DOWNLOADING);
            eventManager(IMG_DWL_EVENT, IMAGE_FWDNLD_DOWNLOAD_INPROGRESS);
        }
        snprintf(fwdls.method, sizeof(fwdls.method), "Method|xconf\n" );
        snprintf(fwdls.proto, sizeof(fwdls.proto), "Proto|http\n" );
        snprintf(fwdls.status, sizeof(fwdls.status), "Status|%s\n", dwnl_status);
        snprintf(fwdls.reboot, sizeof(fwdls.reboot), "Reboot|%s\n", immed_reboot_flag);
        snprintf(fwdls.failureReason, sizeof(fwdls.failureReason), "Failure|" );
        snprintf(fwdls.dnldVersn, sizeof(fwdls.dnldVersn), "Failure|" );
        snprintf(fwdls.dnldfile, sizeof(fwdls.dnldfile), "DnldFile|%s\n", dwlpath_filename ? dwlpath_filename : "XCONF");
        snprintf(fwdls.dnldurl, sizeof(fwdls.dnldurl), "DnldURL|%s\n", artifactLocationUrl);
        snprintf(fwdls.lastrun, sizeof(fwdls.lastrun),"LastRun|%.240s\n", lastrun);
        snprintf(fwdls.FwUpdateState, sizeof(fwdls.FwUpdateState), "FwUpdateState|Downloading\n" );
        snprintf(fwdls.DelayDownload, sizeof(fwdls.DelayDownload), "DelayDownload|%d\n", delay_dwnl);
         
        updateFWDownloadStatus(&fwdls, disableStatsUpdate);

        ret = getDevicePropertyData(dev_prop_name, cpu_arch, (unsigned int)sizeof(cpu_arch));
        if (ret == UTILS_SUCCESS) {
            SWLOG_INFO("cpu_arch = %s\n", cpu_arch);
        } else {
            SWLOG_ERROR("%s: getDevicePropertyData() for %s fail\n", __FUNCTION__, dev_prop_name);
        }

        if (true == st_notify_flag) {
            curtime = getCurrentSysTimeSec();
            snprintf(current_time, sizeof(current_time), "%lu", (unsigned long)curtime);
            SWLOG_INFO("current_time calculated as %lu and %s\n", (unsigned long)curtime, current_time);
            //write_RFCProperty("Rfc_FW", RFC_FW_DWNL_START, current_time, RFC_STRING);
            notifyDwnlStatus(RFC_FW_DWNL_START, current_time, RFC_STRING);
            SWLOG_INFO("FirmwareDownloadStartedNotification SET succeeded\n");
        }

        if (server_type == HTTP_SSR_DIRECT || server_type == HTTP_XCONF_DIRECT) {
            ret_curl_code = downloadFile(context, pHttp_code, curl);
            if ((server_type == HTTP_XCONF_DIRECT) && (ret_curl_code == 6 || ret_curl_code == 28)) {
                SWLOG_INFO("%s: Checking IP and Route configuration\n", __FUNCTION__);
                if (true == (CheckIProuteConnectivity(GATEWAYIP_FILE))) {
                    SWLOG_INFO("%s: Checking IP and Route configuration found\n", __FUNCTION__);
                    SWLOG_INFO("%s: Checking DNS Nameserver configuration\n", __FUNCTION__);
                    if(true == (isDnsResolve(DNS_RESOLV_FILE))) {
                        SWLOG_INFO("%s: DNS Nameservers are available\n", __FUNCTION__);
                    }else {
                        SWLOG_INFO("%s: DNS Nameservers missing..!!\n", __FUNCTION__);
                    }
                }else {
                    SWLOG_INFO("%s: IP and Route configuration not found...!!\n", __FUNCTION__);
                }
            }
            if (*pHttp_code == HTTP_PAGE_NOT_FOUND) {
                SWLOG_INFO("%s : Received HTTPS 404 Response from Xconf Server. Retry logic not needed\n", __FUNCTION__);
                SWLOG_INFO("%s : Creating /tmp/.xconfssrdownloadurl with 404 response from Xconf\n", __FUNCTION__);
                fp = fopen("/tmp/.xconfssrdownloadurl", "w");
                if (fp != NULL) {
                    fprintf(fp, "%d\n", *pHttp_code);
                    fclose(fp);
                }
                unsetStateRed();
            }
            if (ret_curl_code != CURL_SUCCESS ||
                (*pHttp_code != HTTP_SUCCESS && *pHttp_code != HTTP_CHUNK_SUCCESS && *pHttp_code != HTTP_PAGE_NOT_FOUND)) {
                ret_curl_code = retryDownload(context, RETRY_COUNT, 60, pHttp_code, curl);
                if (ret_curl_code == CURL_CONNECTIVITY_ISSUE || *pHttp_code == 0) {
                    if (server_type == HTTP_SSR_DIRECT) {
                        SWLOG_ERROR("%s : Direct Image upgrade Failed: http_code:%d, attempting codebig\n", __FUNCTION__, *pHttp_code);
                    }else {
                        SWLOG_ERROR("%s : sendXCONFRequest Direct Image upgrade Failed: http_code:%d, attempting codebig\n", __FUNCTION__, *pHttp_code);
                    }
                    if( server_type == HTTP_SSR_DIRECT )
                    {
                        server_type = HTTP_SSR_CODEBIG;
                    }
                    else
                    {
                        server_type = HTTP_XCONF_CODEBIG;
                    }
                    ret_curl_code = fallBack(context, pHttp_code, curl);
                }
            }
        }
        else if (server_type == HTTP_SSR_CODEBIG || server_type == HTTP_XCONF_CODEBIG) {
            ret_curl_code = codebigdownloadFile(context, pHttp_code, curl);
            if (ret_curl_code != CURL_SUCCESS ||
                (*pHttp_code != HTTP_SUCCESS && *pHttp_code != HTTP_CHUNK_SUCCESS && *pHttp_code != HTTP_PAGE_NOT_FOUND)) {
                if( ret_curl_code != CODEBIG_SIGNING_FAILED )
                {
                    // if CODEBIG_SIGNING_FAILED, no point in retrying codebig since signing won't correct
                    // but it might work when falling back to direct 
                    ret_curl_code = retryDownload(context, CB_RETRY_COUNT, 10, pHttp_code, curl);
                }
                if (ret_curl_code == CURL_CONNECTIVITY_ISSUE || *pHttp_code == 0) {
                    if (server_type == HTTP_SSR_CODEBIG) {
                        SWLOG_ERROR("%s : Codebig download failed: httpcode=%d, Switching direct\n", __FUNCTION__, *pHttp_code);
                    }else {
                        SWLOG_ERROR("%s : sendXCONFRequest Codebig download failed: http_code:%d, Switching direct\n", __FUNCTION__, *pHttp_code);
                    }
                    if( server_type == HTTP_SSR_CODEBIG )
                    {
                        server_type = HTTP_SSR_DIRECT;
                    }
                    else
                    {
                        server_type = HTTP_XCONF_DIRECT;
                    }
                    ret_curl_code = fallBack(context, pHttp_code, curl);
                }
            }
        }
        else
        {
            SWLOG_ERROR("Invalid Server Type: %d\n", server_type);
        }

        if (ret_curl_code != 0 || (*pHttp_code != HTTP_CHUNK_SUCCESS && *pHttp_code != HTTP_SUCCESS)) {
            eventManager(FW_STATE_EVENT, FW_STATE_FAILED);
            eventManager(IMG_DWL_EVENT, IMAGE_FWDNLD_DOWNLOAD_FAILED);

            if (true == st_notify_flag) {
                notifyDwnlStatus(RFC_FW_DWNL_END, "false", RFC_BOOL);
                SWLOG_INFO("FirmwareDownloadCompletedNotification SET to false succeeded\n");
            }
            if (upgrade_type == PDRI_UPGRADE) {
                SWLOG_INFO("PDRI image upgrade failure !!!\n");
                Upgradet2CountNotify("SYST_ERR_PDRIUpg_failure", 1);
            } else if (upgrade_type == XCONF_UPGRADE && ret_curl_code == 6) {
                Upgradet2CountNotify("xconf_couldnt_resolve", 1); 
            } else if (upgrade_type == PCI_UPGRADE) {
                SWLOG_ERROR("doCDL failed\n");
                Upgradet2CountNotify("SYST_ERR_CDLFail", 1);
                cmd_args = "FWDNLD_FAILED";
                logMilestone(cmd_args);
            } else if (upgrade_type == PERIPHERAL_UPGRADE) {
                checkt2ValNotify( ret_curl_code, upgrade_type, (char *)artifactLocationUrl );
            } else{
                SWLOG_ERROR("Invalid upgrade type\n");
            }
            updateUpgradeFlag(2);//Removing flag file in case of download fail
        } else if ((0 == filePresentCheck(dwlpath_filename)) && (upgrade_type != XCONF_UPGRADE)) {
            SWLOG_INFO("%s Local Image Download Completed using HTTPS TLS protocol!\n", dwlpath_filename);
            Upgradet2CountNotify("SYST_INFO_FWCOMPLETE", 1);
            eventManager(FW_STATE_EVENT, FW_STATE_DOWNLOAD_COMPLETE);

            strncpy(fwdls.FwUpdateState, "FwUpdateState|Download complete\n", sizeof(fwdls.FwUpdateState)-1);
            fwdls.FwUpdateState[sizeof(fwdls.FwUpdateState)-1] = '\0';
	    updateFWDownloadStatus(&fwdls, disableStatsUpdate);
            if (true == st_notify_flag) {
                notifyDwnlStatus(RFC_FW_DWNL_END, "true", RFC_BOOL);
                SWLOG_INFO("FirmwareDownloadCompletedNotification SET to true succeeded\n");
            }
            if (strncmp(cpu_arch, "x86", 3)) {
                eventManager(IMG_DWL_EVENT, IMAGE_FWDNLD_DOWNLOAD_COMPLETE);
            }
	    if( isInStateRed() ) {
                 SWLOG_INFO("RED recovery download complete\n");
                 eventManager(RED_STATE_EVENT, RED_RECOVERY_DOWNLOADED);
            }
            SWLOG_INFO("Downloaded %s of size %d\n", dwlpath_filename, getFileSize(dwlpath_filename));
            Upgradet2CountNotify("Filesize_split", getFileSize(dwlpath_filename));
            *md5_sum = 0;
            RunCommand( eMD5Sum, dwlpath_filename, md5_sum, sizeof(md5_sum) );
            SWLOG_INFO("md5sum of %s : %s\n", dwlpath_filename, md5_sum);
            if (upgrade_type == PDRI_UPGRADE) {
                SWLOG_INFO("PDRI image upgrade successful.\n");
                Upgradet2CountNotify("SYST_INFO_PDRIUpgSuccess", 1);
            }
            
            // Check download_only flag - if 1 (true), skip flashing (for D-Bus DownloadFirmware API)
            if (context->download_only == 1) {
                SWLOG_INFO("download_only flag is set - skipping flash operation\n");
                SWLOG_INFO("Download completed successfully without flashing\n");
            }
            else if (upgrade_type == PCI_UPGRADE || upgrade_type == PDRI_UPGRADE) {
                setDwnlState(RDKV_FWDNLD_FLASH_INPROGRESS);
                snprintf(dwnl_status, sizeof(dwnl_status), "Flashing In Progress");
                snprintf(fwdls.status, sizeof(fwdls.status), "Status|%s\n", dwnl_status);
                updateFWDownloadStatus(&fwdls, disableStatsUpdate);
                flash_status  = flashImage(artifactLocationUrl, dwlpath_filename, immed_reboot_flag, "2", upgrade_type, device_info->maint_status,trigger_type);
                if (upgrade_type == PCI_UPGRADE) {
                    if (flash_status != 0 && upgrade_type == PCI_UPGRADE) {
                        SWLOG_ERROR("doCDL failed\n");
			Upgradet2CountNotify("SYST_ERR_CDLFail", 1);
                        setDwnlState(RDKV_FWDNLD_FLASH_FAILED);
                        cmd_args = "FWDNLD_FAILED";
                        logMilestone(cmd_args);
                    } else {
                        setDwnlState(RDKV_FWDNLD_FLASH_COMPLETE);
                        cmd_args = "FWDNLD_COMPLETED";
                        logMilestone(cmd_args);
                    }
                } else {
                    SWLOG_INFO("PDRI image Flash upgrade successful.\n");
                }
            }
        }
    }
    else
    {
        SWLOG_INFO("%s : All upgrades blocked, skipping upgrade\n", __FUNCTION__);
    }

    return ret_curl_code;
}

/* Description: Use for download image from codebig
 * @param: artifactLocationUrl : server url
 * @param: localDownloadLocation : download location
 * @param: httpCode : send back http value
 * @return int: success/failure
 * */
#ifndef GTEST_BASIC
int codebigdownloadFile(
    const RdkUpgradeContext_t* context,
    int *httpCode,
    void **curl
) {
    int curl_ret_code = -1;

    // Null check for all parameters
    if (context == NULL) {
        SWLOG_ERROR("%s: context parameter is NULL\n", __FUNCTION__);
        return curl_ret_code;
    }
    if (httpCode == NULL) {
        SWLOG_ERROR("%s: httpCode parameter is NULL\n", __FUNCTION__);
        return curl_ret_code;
    }
    if (curl == NULL || *curl == NULL) {
        SWLOG_ERROR("%s: curl parameter is NULL\n", __FUNCTION__);
        return curl_ret_code;
    }


    int server_type = context->server_type;
    const char* artifactLocationUrl = context->artifactLocationUrl;
    const void* localDownloadLocation = context->dwlloc;
    char* pPostFields = context->pPostFields;
    int* force_exit = context->force_exit;
    const char* lastrun = context->lastrun;

    int signFailed = 1;           // 0 for success, 1 indicates failed
    FileDwnl_t file_dwnl;
    char oAuthHeader[BIG_BUF_LEN]  = "Authorization: OAuth realm=\"\", ";
    char headerInfoFile[136];

    if (artifactLocationUrl == NULL || localDownloadLocation == NULL) {
        SWLOG_ERROR("%s: artifactLocationUrl or localDownloadLocation is NULL\n", __FUNCTION__);
        return curl_ret_code;
    }
    *httpCode = 0;

#ifdef DEBUG_CODEBIG_CDL
    if( filePresentCheck( "/tmp/.forceCodebigFailure" ) == RDK_API_SUCCESS )
    {
        SWLOG_ERROR("%s:  Forcing Codebig Failure!!\n", __FUNCTION__);
        saveHTTPCode(*httpCode,lastrun);
        return CURLTIMEOUT;     // timeout error
    }
#endif

    if (isDwnlBlock(server_type)) {
            SWLOG_ERROR("%s: Codebig Download is block\n", __FUNCTION__);
            curl_ret_code = DWNL_BLOCK;
            return curl_ret_code;
    }

    SWLOG_INFO("Using Codebig Image upgrade connection\nCheck if codebig is applicable for the Device\n");
    Upgradet2CountNotify("SYST_INFO_cb_xconf", 1);
    /* checkCodebigAccess check is required only for xconf communication. Detail mention in ticket LLAMA-10049 */
    if ((server_type == HTTP_XCONF_CODEBIG) && (false == (checkCodebigAccess()))) {
        SWLOG_ERROR("%s:  Codebig Image upgrade is not supported.\n", __FUNCTION__);
        return curl_ret_code;
    }
    if (false == (isMediaClientDevice())) {
        SWLOG_ERROR("%s:  Codebig Image upgrade is not supported.Support is only for mediaclient device\n", __FUNCTION__);
        return curl_ret_code;
    }
    
    memset(&file_dwnl, '\0', sizeof(FileDwnl_t));
    file_dwnl.chunk_dwnl_retry_time = 0; // Assign zero because we do not have this support in codebig

    if( server_type == HTTP_XCONF_CODEBIG )
    {
        if( (signFailed=doCodeBigSigning(server_type, pPostFields, file_dwnl.url, sizeof(file_dwnl.url),
                                         oAuthHeader, sizeof(oAuthHeader) )) == 0 )
        {
            file_dwnl.pDlData = (DownloadData *)localDownloadLocation;
            *(file_dwnl.pathname) = 0;
            file_dwnl.pPostFields = NULL;
            file_dwnl.pHeaderData = NULL;
	    file_dwnl.pDlHeaderData = NULL;
            *headerInfoFile = 0;
        }
    }
    else
    {
        if( (signFailed=doCodeBigSigning(server_type, artifactLocationUrl, file_dwnl.url, sizeof(file_dwnl.url),
                                         oAuthHeader, sizeof(oAuthHeader) )) == 0 )
        {
            strncpy(file_dwnl.pathname, (char *)localDownloadLocation, sizeof(file_dwnl.pathname)-1);
            file_dwnl.pathname[sizeof(file_dwnl.pathname)-1] = '\0';
	    file_dwnl.pDlData = NULL;
            file_dwnl.pHeaderData = oAuthHeader;
	    file_dwnl.pDlHeaderData = NULL;
            file_dwnl.pPostFields = NULL;
            snprintf(headerInfoFile, sizeof(headerInfoFile), "%s.header", file_dwnl.pathname);
        }
    }

    if( signFailed == 0 )
    {
        if (server_type == HTTP_SSR_CODEBIG) {
            SWLOG_INFO("Trying to communicate with SSR via CodeBig server\nAttempting Codebig firmware download\n");
        }
        //    SWLOG_INFO("%s : After doCodeBigSigning=%s\nsigned url=%s\n", __FUNCTION__, artifactLocationUrl, file_dwnl.url);
        if (server_type == HTTP_XCONF_CODEBIG) {
            setDwnlState(RDKV_XCONF_FWDNLD_DOWNLOAD_INIT);
        } else {
            setDwnlState(RDKV_FWDNLD_DOWNLOAD_INIT);
        }
        // Use the passed curl handle instead of creating a new one
        if (curl != NULL && *curl != NULL) {
            if (server_type == HTTP_XCONF_CODEBIG) {
                setDwnlState(RDKV_XCONF_FWDNLD_DOWNLOAD_INPROGRESS);
            } else {
                setDwnlState(RDKV_FWDNLD_DOWNLOAD_INPROGRESS);
            }
            curl_ret_code = doAuthHttpFileDownload(*curl ,&file_dwnl, httpCode); // Handle return status
            if (server_type == HTTP_XCONF_CODEBIG) {
                setDwnlState(RDKV_XCONF_FWDNLD_DOWNLOAD_EXIT);
            } else {
                setDwnlState(RDKV_FWDNLD_DOWNLOAD_EXIT);
            }
            doStopDownload(*curl);
            // Don't set curl = NULL here - preserve the handle for reuse
            /* Stop the donwload if Throttle speed rfc is set to zero */
            if (*force_exit == 1 && (curl_ret_code == 23)) {
                uninitialize(INITIAL_VALIDATION_SUCCESS);
                exit(1);
            }
        }

        if((filePresentCheck(CURL_PROGRESS_FILE)) == 0) {
            SWLOG_INFO("%s : Curl Progress data...\n", __FUNCTION__);
            logFileData(CURL_PROGRESS_FILE);
            unlink(CURL_PROGRESS_FILE);
        }
        if (curl_ret_code != 0) {
            setDwnlState(RDKV_FWDNLD_DOWNLOAD_FAILED); 
            if( *(file_dwnl.pathname) != 0 )
            {
                unlink(file_dwnl.pathname);
                unlink(headerInfoFile);
            }
        }
        if ((curl_ret_code == 0) && (*httpCode == 200 || *httpCode == 206)) {
            setDwnlState(RDKV_FWDNLD_DOWNLOAD_COMPLETE); 
            SWLOG_INFO("%s : Codebig firmware download Success - ret:%d http_code:%d\n", __FUNCTION__, curl_ret_code, *httpCode);
            Upgradet2CountNotify("SYS_INFO_CodBPASS", 1);
        }
    }
    else
    {
        *httpCode = 0;
        curl_ret_code = CODEBIG_SIGNING_FAILED;
        SWLOG_ERROR( "%s : Codebig signing failed, server type = %d, aborting download!!\n", __FUNCTION__, server_type );
    }
    saveHTTPCode(*httpCode,lastrun);
    return curl_ret_code;
}
#endif

/* Description: Use for download image from Direct server
 * @param: artifactLocationUrl : server url
 * @param: localDownloadLocation : download location
 * @param: httpCode : send back http value
 * @return int: success/failure
 * */
#ifndef GTEST_BASIC
int downloadFile(
    const RdkUpgradeContext_t* context,
    int *httpCode,
    void **curl
) {
    int curl_ret_code = -1;

    // Null check for all parameters
    if (context == NULL) {
        SWLOG_ERROR("%s: context parameter is NULL\n", __FUNCTION__);
        return curl_ret_code;
    }
    if (httpCode == NULL) {
        SWLOG_ERROR("%s: httpCode parameter is NULL\n", __FUNCTION__);
        return curl_ret_code;
    }
    if (curl == NULL || *curl == NULL) {
        SWLOG_ERROR("%s: curl parameter is NULL\n", __FUNCTION__);
        return curl_ret_code;
    }

    int server_type = context->server_type;
    const char* artifactLocationUrl = context->artifactLocationUrl;
    const void* localDownloadLocation = context->dwlloc;
    char* pPostFields = context->pPostFields;
    int* force_exit = context->force_exit;
    const char* immed_reboot_flag = context->immed_reboot_flag;
    const DeviceProperty_t* device_info = context->device_info;
    const char* lastrun = context->lastrun;
    const Rfc_t* rfc_list = context->rfc_list;
    char* disableStatsUpdate = context->disableStatsUpdate;

    int app_mode = 0;
//The following compilation LIBRDKCERTSELECTOR is enabled only Comcast proprietary code until the rdk-cert-config(Cert selector) component becomes open-source.    
#ifdef LIBRDKCERTSELECTOR
    MtlsAuthStatus ret = MTLS_CERT_FETCH_SUCCESS;
#else
    int ret = -1 ;
#endif    
    MtlsAuth_t sec;
    int state_red = -1;
    unsigned int max_dwnl_speed = 0;
    //int curl_ret_code = -1;
    FileDwnl_t file_dwnl;
    int chunk_dwnl = 0;
    int mtls_enable = 1; //Setting mtls by default enable
    char headerInfoFile[136] = {0};

    // Entry logging
    SWLOG_INFO("[DOWNLOAD_FILE] Enter %s: server_type=%d, artifactLocationUrl='%s', localDownloadLocation=%p, pPostFields=%p, httpCode=%p, curl=%p, force_exit=%p, immed_reboot_flag='%s'\n",
               __FUNCTION__, server_type, artifactLocationUrl ? artifactLocationUrl : "(null)", localDownloadLocation, (void*)pPostFields, (void*)httpCode, (void*)curl, (void*)force_exit, immed_reboot_flag ? immed_reboot_flag : "(null)");

    if (device_info != NULL) {
        SWLOG_INFO("[DOWNLOAD_FILE] device_info: dev_name='%s', dev_type='%s', maint_status='%s'\n",
                   device_info->dev_name ? device_info->dev_name : "(null)",
                   device_info->dev_type ? device_info->dev_type : "(null)",
                   device_info->maint_status ? device_info->maint_status : "(null)");
    }
    if (rfc_list != NULL) {
        SWLOG_INFO("[DOWNLOAD_FILE] rfc_list: rfc_throttle='%s', rfc_topspeed='%s'\n",
                   rfc_list->rfc_throttle ? rfc_list->rfc_throttle : "(null)",
                   rfc_list->rfc_topspeed ? rfc_list->rfc_topspeed : "(null)");
    }
    SWLOG_INFO("[DOWNLOAD_FILE] lastrun='%s', disableStatsUpdate=%p\n", lastrun ? lastrun : "(null)", (void*)disableStatsUpdate);

    if (artifactLocationUrl == NULL || localDownloadLocation == NULL) {
        SWLOG_ERROR("[DOWNLOAD_FILE] CRITICAL: artifactLocationUrl or localDownloadLocation is NULL\n");
        return curl_ret_code;
    }
    app_mode = getAppMode();
    memset(&sec, '\0', sizeof(MtlsAuth_t));
    memset(&file_dwnl, '\0', sizeof(FileDwnl_t));
    
    state_red = isInStateRed();
#ifdef LIBRDKCERTSELECTOR
    static rdkcertselector_h thisCertSel = NULL;
    if (thisCertSel == NULL) {
        const char* certGroup = (state_red == 1) ? "RCVRY" : "MTLS";
        thisCertSel = rdkcertselector_new(DEFAULT_CONFIG, DEFAULT_HROT, certGroup);
        if (thisCertSel == NULL) {
            SWLOG_ERROR("%s, %s Cert selector initialization failed\n", __FUNCTION__, (state_red == 1) ? "State red" : "normal state");
            return curl_ret_code;
        } else {
            SWLOG_INFO("%s, %s Cert selector initialized successfully\n", __FUNCTION__, (state_red == 1) ? "State red" : "normal state");
        }
    } else {
        SWLOG_INFO("%s, Cert selector already initialized, reusing the existing instance\n", __FUNCTION__);
    }
#endif
    
    *httpCode = 0;
    file_dwnl.chunk_dwnl_retry_time = (((strncmp(immed_reboot_flag, "false", 5)) == 0) ? 10 : 0);
    strncpy(file_dwnl.url, artifactLocationUrl, sizeof(file_dwnl.url)-1);
    file_dwnl.url[sizeof(file_dwnl.url)-1] = '\0';
    if( server_type == HTTP_SSR_DIRECT )
    {
        strncpy(file_dwnl.pathname, (char *)localDownloadLocation, sizeof(file_dwnl.pathname)-1);
	file_dwnl.pathname[sizeof(file_dwnl.pathname)-1] = '\0';
        file_dwnl.pDlData = NULL;
        snprintf(headerInfoFile, sizeof(headerInfoFile), "%s.header", file_dwnl.pathname);

        // Detailed logging of file_dwnl when writing to disk
        SWLOG_INFO("[DOWNLOAD_FILE] FileDwnl set for SSR_DIRECT: url='%s', pathname='%s', headerInfoFile='%s'\n",
                   file_dwnl.url ? file_dwnl.url : "(null)", file_dwnl.pathname ? file_dwnl.pathname : "(null)", headerInfoFile);
    }
    else        // server_type must be HTTP_XCONF_DIRECT, store to memory not a file
    {
        file_dwnl.pDlData = (DownloadData *)localDownloadLocation;
        *(file_dwnl.pathname) = 0;

        // Detailed logging of file_dwnl when writing to memory
        SWLOG_INFO("[DOWNLOAD_FILE] FileDwnl set for XCONF_DIRECT: url='%s', pDlData=%p\n", file_dwnl.url ? file_dwnl.url : "(null)", (void*)file_dwnl.pDlData);
    }
    file_dwnl.pPostFields = pPostFields;
    SWLOG_INFO("[DOWNLOAD_FILE] file_dwnl.pPostFields=%p content='%s'\n", (void*)file_dwnl.pPostFields, file_dwnl.pPostFields ? file_dwnl.pPostFields : "(null)");

    // Continue with existing logic - ensure we log before calling curl/doHttpFileDownload
    SWLOG_INFO("[DOWNLOAD_FILE] About to start network download: server_type=%d, url='%s'\n", server_type, artifactLocationUrl ? artifactLocationUrl : "(null)");

    if (isDwnlBlock(server_type)) { // only care about DIRECT or CODEBIG, SSR or XCONF doesn't matter
        SWLOG_ERROR("%s: Direct Download is blocked\n", __FUNCTION__);
        curl_ret_code = DWNL_BLOCK;
#ifdef LIBRDKCERTSELECTOR
	if (thisCertSel != NULL) {
            rdkcertselector_free(&thisCertSel);
        }
#endif	    
        return curl_ret_code;
    }
    if (server_type == HTTP_SSR_DIRECT) {
        SWLOG_INFO("%s :Trying to communicate with SSR via TLS server\n", __FUNCTION__);
        Upgradet2CountNotify("SYST_INFO_TLS_xconf", 1);
    }

    if ((1 == (isThrottleEnabled(device_info->dev_name, immed_reboot_flag, app_mode)))) {
        /* Coverity fix: NO_EFFECT - rfc_throttle is a char array, not a pointer.
         * Removed redundant "!= NULL" check. Only check for non-empty string.
         * Ensure rfc_list is valid before dereferencing. */
        if (rfc_list != NULL && rfc_list->rfc_throttle[0] != '\0' &&
            0 == (strncmp(rfc_list->rfc_throttle, "true", 4))) {
            max_dwnl_speed = atoi(rfc_list->rfc_topspeed);
            SWLOG_INFO("%s : Throttle feature is Enable\n", __FUNCTION__);
            Upgradet2CountNotify("SYST_INFO_Thrtl_Enable", 1);
            if (max_dwnl_speed == 0) {
                SWLOG_INFO("%s : Throttle speed set to 0. So exiting the download process\n", __FUNCTION__);
                if (!(strncmp(device_info->maint_status, "true", 4))) {
                    eventManager("MaintenanceMGR", MAINT_FWDOWNLOAD_ERROR);
                }
                eventManager(FW_STATE_EVENT, FW_STATE_FAILED);
                uninitialize(INITIAL_VALIDATION_SUCCESS);
                exit(1); //maintenance mode is background and speed set to 0. So exiting the process
            }
        } else {
            SWLOG_INFO("%s : Throttle feature is Disable\n", __FUNCTION__);
        }
    } else {
        SWLOG_INFO("%s : Throttle is Disable\n", __FUNCTION__);
    }

    if (1 == (isOCSPEnable())) {
        SWLOG_INFO("%s : Enable OCSP check\n", __FUNCTION__);
        file_dwnl.sslverify = true;
    } else {
        SWLOG_INFO("%s : Disable OCSP check\n", __FUNCTION__);
    }
    getPidStore(device_info->dev_name, device_info->maint_status); //TODO: Added for script support. Need to remove later
    if (disableStatsUpdate != NULL && (strcmp(disableStatsUpdate, "yes")) && (server_type == HTTP_SSR_DIRECT)) {
        chunk_dwnl = isIncremetalCDLEnable(file_dwnl.pathname);
    }
#ifndef LIBRDKCERTSELECTOR	
    SWLOG_INFO("1 Fetching MTLS credential for SSR/XCONF\n");
    ret = getMtlscert(&sec);
    if (-1 == ret) {
        SWLOG_ERROR("%s : getMtlscert() Featching MTLS fail. Going For NON MTLS:%d\n", __FUNCTION__, ret);
        mtls_enable = -1;//If certificate or key featching fail try with non mtls
    }else {
        SWLOG_INFO("MTLS is enable\nMTLS creds for SSR fetched ret=%d\n", ret);
        Upgradet2CountNotify("SYS_INFO_MTLS_enable", 1);
    }
#endif	
    (server_type == HTTP_SSR_DIRECT) ? setDwnlState(RDKV_FWDNLD_DOWNLOAD_INIT) : setDwnlState(RDKV_XCONF_FWDNLD_DOWNLOAD_INIT);
#ifdef LIBRDKCERTSELECTOR
    do {
        SWLOG_INFO("Fetching MTLS credential for SSR/XCONF\n");
        ret = getMtlscert(&sec, &thisCertSel);
        SWLOG_INFO("%s, getMtlscert function ret value = %d\n", __FUNCTION__, ret);

        if (ret == MTLS_CERT_FETCH_FAILURE) {
            SWLOG_ERROR("%s : ret=%d\n", __FUNCTION__, ret);
            SWLOG_ERROR("%s : All MTLS certs are failed. Falling back to state red.\n", __FUNCTION__);
            checkAndEnterStateRed(CURL_MTLS_LOCAL_CERTPROBLEM, disableStatsUpdate);
            return curl_ret_code;
        } else if (ret == STATE_RED_CERT_FETCH_FAILURE) {
            SWLOG_ERROR("%s : State red cert failed.\n", __FUNCTION__);
            return curl_ret_code;
        } else {
            SWLOG_INFO("MTLS is enabled\nMTLS creds for SSR fetched ret=%d\n", ret);
            Upgradet2CountNotify("SYS_INFO_MTLS_enable", 1);
	}
#endif
        do {
            if ((1 == state_red)) {
                SWLOG_INFO("RED:state red recovery attempting MTLS connection to XCONF server\n");
                if (CHUNK_DWNL_ENABLE == chunk_dwnl) {
	            SWLOG_INFO("RED: Calling  chunkDownload() in state red recovery\n");
                    Upgradet2CountNotify("SYST_INFO_RedStateRecovery", 1);
	            curl_ret_code = chunkDownload(&file_dwnl, &sec, max_dwnl_speed, httpCode);
	            break;
	        }else {
                if (curl != NULL) {
                    *curl = doCurlInit();
                }
	            if (curl != NULL && *curl != NULL) {
	                (server_type == HTTP_SSR_DIRECT) ? setDwnlState(RDKV_FWDNLD_DOWNLOAD_INPROGRESS) : setDwnlState(RDKV_XCONF_FWDNLD_DOWNLOAD_INPROGRESS);
                    curl_ret_code = doHttpFileDownload(*curl, &file_dwnl, &sec, max_dwnl_speed, NULL, httpCode);
	                (server_type == HTTP_SSR_DIRECT) ? setDwnlState(RDKV_FWDNLD_DOWNLOAD_EXIT) : setDwnlState(RDKV_XCONF_FWDNLD_DOWNLOAD_EXIT);
                    doStopDownload(*curl);
	                *curl = NULL;
	                if (*force_exit == 1 && (curl_ret_code == 23)) {
	                    uninitialize(INITIAL_VALIDATION_SUCCESS);
	                    exit(1);
	                }
	            }
	        }
            } else if(1 == mtls_enable) {
                  if (CHUNK_DWNL_ENABLE == chunk_dwnl) {
	              SWLOG_INFO("Calling  chunkDownload() with cert mTlsXConfDownload enable\n");
	              curl_ret_code = chunkDownload(&file_dwnl, &sec, max_dwnl_speed, httpCode);
	              break;
                  } else {
                      SWLOG_INFO("Calling  doHttpFileDownload() with cert mTlsXConfDownload enable\n");
                      if (curl != NULL) {
                          *curl = doCurlInit();
                      }
                      if (curl != NULL && *curl != NULL) {
                          (server_type == HTTP_SSR_DIRECT) ? setDwnlState(RDKV_FWDNLD_DOWNLOAD_INPROGRESS) : setDwnlState(RDKV_XCONF_FWDNLD_DOWNLOAD_INPROGRESS);
                          curl_ret_code = doHttpFileDownload(*curl, &file_dwnl, &sec, max_dwnl_speed, NULL, httpCode);
                          (server_type == HTTP_SSR_DIRECT) ? setDwnlState(RDKV_FWDNLD_DOWNLOAD_EXIT) : setDwnlState(RDKV_XCONF_FWDNLD_DOWNLOAD_EXIT);
                          doStopDownload(*curl);
                          *curl = NULL;
	                  if (*force_exit == 1 && (curl_ret_code == 23)) {
	                      uninitialize(INITIAL_VALIDATION_SUCCESS);
	                      exit(1);
                          }
                      }
                  }
            } else {
	        if (CHUNK_DWNL_ENABLE == chunk_dwnl) {
	            SWLOG_INFO("Calling  chunkDownload() with cert mTlsXConfDownload disable\n");
	            curl_ret_code = chunkDownload(&file_dwnl, NULL, max_dwnl_speed, httpCode);
	            break;
                } else {
                    SWLOG_INFO("Calling doHttpFileDownload() with cert mTlsXConfDownload disable\n");
                    if (curl != NULL) {
                        *curl = doCurlInit();
                    }
                    if (curl != NULL && *curl != NULL) {
                        (server_type == HTTP_SSR_DIRECT) ? setDwnlState(RDKV_FWDNLD_DOWNLOAD_INPROGRESS) : setDwnlState(RDKV_XCONF_FWDNLD_DOWNLOAD_INPROGRESS);
                        curl_ret_code = doHttpFileDownload(*curl, &file_dwnl, NULL, max_dwnl_speed, NULL, httpCode);
                        (server_type == HTTP_SSR_DIRECT) ? setDwnlState(RDKV_FWDNLD_DOWNLOAD_EXIT) : setDwnlState(RDKV_XCONF_FWDNLD_DOWNLOAD_EXIT);
                        doStopDownload(*curl);
                        *curl = NULL;
	                if (*force_exit == 1 && (curl_ret_code == 23)) {
	                    uninitialize(INITIAL_VALIDATION_SUCCESS);
	                    exit(1);
                        }
                    }
                }
            }
            if (disableStatsUpdate != NULL && (strcmp(disableStatsUpdate, "yes")) && (CHUNK_DWNL_ENABLE != chunk_dwnl)) {
                chunk_dwnl = isIncremetalCDLEnable(file_dwnl.pathname);
            }
            SWLOG_INFO("%s : After curl request the curl status = %d and http=%d and chunk download=%d\n", __FUNCTION__, curl_ret_code, *httpCode, chunk_dwnl);
            // Sleep for 10 seconds in case of curl 56 (CURL_RECV_ERROR) for network to stabilize if this is due to network issue. 
        } while(chunk_dwnl && (CURL_LOW_BANDWIDTH == curl_ret_code || CURLTIMEOUT == curl_ret_code || ((CURL_RECV_ERROR == curl_ret_code) && !sleep(10)) ));
#ifdef LIBRDKCERTSELECTOR
    } while (rdkcertselector_setCurlStatus(thisCertSel, curl_ret_code, file_dwnl.url) == TRY_ANOTHER);
#endif
    if((filePresentCheck(CURL_PROGRESS_FILE)) == 0) {
        SWLOG_INFO("%s : Curl Progress data...\n", __FUNCTION__);
        logFileData(CURL_PROGRESS_FILE);
        unlink(CURL_PROGRESS_FILE);
    }
    if ((curl_ret_code == CURL_SUCCESS) && (*httpCode == HTTP_SUCCESS || *httpCode == HTTP_CHUNK_SUCCESS)) {
        (server_type == HTTP_SSR_DIRECT) ? setDwnlState(RDKV_FWDNLD_DOWNLOAD_COMPLETE) : setDwnlState(RDKV_XCONF_FWDNLD_DOWNLOAD_COMPLETE);
        if(server_type == HTTP_SSR_DIRECT)
        {
            SWLOG_INFO("%s : Direct Image upgrade Success: curl ret:%d http_code:%d\n", __FUNCTION__, curl_ret_code,*httpCode);
            Upgradet2CountNotify("SYS_INFO_DirectSuccess", 1);
        }
        else
        {
            SWLOG_INFO("%s : Direct Image upgrade connection success: curl ret:%d http_code:%d\n", __FUNCTION__, curl_ret_code, *httpCode);
        }
    }else {
        SWLOG_ERROR("%s : Direct Image upgrade Fail: curl ret:%d http_code:%d\n", __FUNCTION__, curl_ret_code, *httpCode);
        (server_type == HTTP_SSR_DIRECT) ? setDwnlState(RDKV_FWDNLD_DOWNLOAD_FAILED) : setDwnlState(RDKV_XCONF_FWDNLD_DOWNLOAD_FAILED);
        dwnlError(curl_ret_code, *httpCode, server_type,device_info,lastrun,disableStatsUpdate);
        if( *(file_dwnl.pathname) != 0 )
        {
            unlink(file_dwnl.pathname);
            unlink(headerInfoFile);
        }
    }
    saveHTTPCode(*httpCode,lastrun);
    return curl_ret_code;
}
#endif

int retryDownload(
    const RdkUpgradeContext_t* context,
    int retry_cnt,
    int delay,
    int *httpCode,
    void **curl
) {
    int curl_ret_code = -1;
    int retry_completed = 1;
    
    // Null check for all parameters
    if (context == NULL) {
        SWLOG_ERROR("%s: context parameter is NULL\n", __FUNCTION__);
        return curl_ret_code;
    }
    if (httpCode == NULL) {
        SWLOG_ERROR("%s: httpCode parameter is NULL\n", __FUNCTION__);
        return curl_ret_code;
    }
    if (curl == NULL || *curl == NULL) {
        SWLOG_ERROR("%s: curl parameter is NULL\n", __FUNCTION__);
        return curl_ret_code;
    }
    
    int server_type = context->server_type;
    const char* artifactLocationUrl = context->artifactLocationUrl;
    const void* localDownloadLocation = context->dwlloc;
    
    if (artifactLocationUrl == NULL || localDownloadLocation == NULL) {
        SWLOG_ERROR("%s: artifactLocationUrl or localDownloadLocation is NULL\n", __FUNCTION__);
        return curl_ret_code;
    }
    if (server_type == HTTP_SSR_DIRECT || server_type == HTTP_XCONF_DIRECT) {
        if( server_type == HTTP_SSR_DIRECT )
        {
            SWLOG_INFO("%s: servertype=%d, url=%s, loc=%s, httpcode=%d, total retry=%d, delay=%d\n", __FUNCTION__, server_type, artifactLocationUrl, (const char *)localDownloadLocation, *httpCode, retry_cnt, delay);
        }
        else
        {
            SWLOG_INFO("%s: servertype=%d, url=%s, loc=MEMORY, httpcode=%d, retry=%d, delay=%d\n", __FUNCTION__, server_type, artifactLocationUrl, *httpCode, retry_cnt, delay);
        }
        while( retry_completed <= retry_cnt) {
            sleep(delay);
            curl_ret_code = downloadFile(context, httpCode, curl);
            if ((curl_ret_code == CURL_SUCCESS) && (*httpCode == HTTP_SUCCESS || *httpCode == HTTP_CHUNK_SUCCESS)) {
	        if(server_type == HTTP_SSR_DIRECT)
	        {
	            SWLOG_INFO("%s : Direct Image upgrade Success: ret:%d http_code:%d\n", __FUNCTION__, curl_ret_code, *httpCode);
	            Upgradet2CountNotify("SYS_INFO_DirectSuccess", 1);
	        }
	        else
	        {
	            SWLOG_INFO("%s : Direct Image upgrade connection success: ret:%d http_code:%d\n", __FUNCTION__, curl_ret_code, *httpCode);
	        }
                break;
            } else if (*httpCode == HTTP_PAGE_NOT_FOUND) {
                (server_type == HTTP_SSR_DIRECT) ? SWLOG_INFO("%s : Received 404 response for Direct Image upgrade, Retry logic not needed\n", __FUNCTION__) : SWLOG_INFO("%s : Received 404 response Direct Image upgrade from xconf, Retry logic not needed\n", __FUNCTION__);
                break;
            } else if(curl_ret_code == DWNL_BLOCK) {
                break;
            } else {
                (server_type == HTTP_SSR_DIRECT) ? SWLOG_INFO("%s : Direct Image upgrade return: retry=%d ret:%d http_code:%d\n", __FUNCTION__, retry_completed, curl_ret_code, *httpCode) : SWLOG_INFO("%s : Direct Image upgrade connection return: retry=%d ret:%d http_code:%d\n", __FUNCTION__, retry_completed, curl_ret_code, *httpCode);
            }
            retry_completed++;
        }
    } else if (server_type == HTTP_SSR_CODEBIG || server_type == HTTP_XCONF_CODEBIG) {
        while(retry_completed <= retry_cnt) {
            sleep(delay);
            curl_ret_code = codebigdownloadFile(context, httpCode, curl);
            if ((curl_ret_code == CURL_SUCCESS) && (*httpCode == HTTP_SUCCESS || *httpCode == HTTP_CHUNK_SUCCESS)) {
                SWLOG_INFO("%s : Codebig Image upgrade Success: ret:%d http_code:%d\n", __FUNCTION__, curl_ret_code, *httpCode);
                break;
            } else if (*httpCode == HTTP_PAGE_NOT_FOUND) {
                (server_type == HTTP_SSR_CODEBIG) ? SWLOG_INFO("%s : Received 404 response for Codebig Image upgrade, Retry logic not needed\n", __FUNCTION__) : SWLOG_INFO("%s : Received 404 response Codebig Image upgrade from xconf, Retry logic not needed\n", __FUNCTION__);
                break;
            } else if (curl_ret_code == DWNL_BLOCK || curl_ret_code == CODEBIG_SIGNING_FAILED) {
                break;
            } else {
                SWLOG_INFO("%s : Codebig Image upgrade return: retry=%d ret:%d http_code:%d\n", __FUNCTION__, retry_completed, curl_ret_code, *httpCode);
            }
            retry_completed++;
        }
    } else {
        *httpCode = 0;
        SWLOG_ERROR("%s: Invalid Server Type=%d\n", __FUNCTION__, server_type);
    }
    return curl_ret_code;
}

int fallBack(
    const RdkUpgradeContext_t* context,
    int *httpCode,
    void **curl
) {
    int curl_ret_code = -1;

    // Null check for all parameters
    if (context == NULL) {
        SWLOG_ERROR("%s: context parameter is NULL\n", __FUNCTION__);
        return curl_ret_code;
    }
    if (httpCode == NULL) {
        SWLOG_ERROR("%s: httpCode parameter is NULL\n", __FUNCTION__);
        return curl_ret_code;
    }
    if (curl == NULL || *curl == NULL) {
        SWLOG_ERROR("%s: curl parameter is NULL\n", __FUNCTION__);
        return curl_ret_code;
    }

    int server_type = context->server_type;
    const char* artifactLocationUrl = context->artifactLocationUrl;
    const void* localDownloadLocation = context->dwlloc;

    if (artifactLocationUrl == NULL || localDownloadLocation == NULL) {
        SWLOG_ERROR("%s: artifactLocationUrl or localDownloadLocation is NULL\n", __FUNCTION__);
        return curl_ret_code;
    }

    if (server_type == HTTP_SSR_DIRECT || server_type == HTTP_XCONF_DIRECT) {
        SWLOG_INFO("%s: calling downloadFile\n", __FUNCTION__ );
        curl_ret_code = downloadFile(context, httpCode, curl);
        if (*httpCode != HTTP_SUCCESS && *httpCode != HTTP_CHUNK_SUCCESS && *httpCode != 404) {
            SWLOG_ERROR("%s : Direct image upgrade failover request failed return=%d, httpcode=%d\n", __FUNCTION__, curl_ret_code, *httpCode);
        } else {
            SWLOG_INFO("%s : Direct image upgrade failover request received return=%d, httpcode=%d\n", __FUNCTION__, curl_ret_code, *httpCode);
        }
    } else if (server_type == HTTP_SSR_CODEBIG || server_type == HTTP_XCONF_CODEBIG) {
        //curl_ret_code = codebigdownloadFile(artifactLocationUrl, localDownloadLocation, httpCode);
        SWLOG_INFO("%s: calling retryDownload\n", __FUNCTION__ );
        curl_ret_code = retryDownload(context, CB_RETRY_COUNT, 10, httpCode, curl);
        if ((curl_ret_code == CURL_SUCCESS) && (*httpCode == HTTP_SUCCESS || *httpCode == HTTP_CHUNK_SUCCESS)) {
            SWLOG_INFO("%s : Codebig Image upgrade Success: ret=%d httpcode=%d\n", __FUNCTION__, curl_ret_code, *httpCode);
            if ((filePresentCheck(DIRECT_BLOCK_FILENAME)) != 0) {
                createFile(DIRECT_BLOCK_FILENAME);
                SWLOG_INFO("%s : Use CodeBig and Blocking Direct attempts for 24hrs\n", __FUNCTION__);
            }
        } else if ((*httpCode != HTTP_PAGE_NOT_FOUND) && (curl_ret_code != -1)){
            /* if curl_ret_code -1 means codebig is not supported or some invalid paramter */
            SWLOG_INFO("%s : Codebig Image upgrade Fail: ret=%d httpcode=%d\n", __FUNCTION__, curl_ret_code, *httpCode);
            if ((filePresentCheck(CB_BLOCK_FILENAME)) != 0) {
                createFile(CB_BLOCK_FILENAME);
                SWLOG_INFO("%s : Switch Direct and Blocking Codebig for 30mins,\n", __FUNCTION__);
            }
        }
    }
    if (server_type == HTTP_SSR_DIRECT || server_type == HTTP_XCONF_DIRECT) {
        SWLOG_INFO("%s : fall back Direct Download. curl return code=%d and http=%d\n", __FUNCTION__, curl_ret_code, *httpCode);
    } else if (server_type == HTTP_SSR_CODEBIG || server_type == HTTP_XCONF_CODEBIG) {
        SWLOG_INFO("%s : fall back Codebig Download. curl return code=%d and http=%d\n", __FUNCTION__, curl_ret_code, *httpCode);
    } else {
        SWLOG_ERROR("%s: Invalid Server Type=%d\n", __FUNCTION__, server_type);
    }
    return curl_ret_code;
}
