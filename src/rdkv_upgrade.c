#include "rdkv_upgrade.h"
#include "rdkv_cdl_log_wrapper.h"
#include "device_status_helper.h"
#include "download_status_helper.h"
#include "iarmInterface.h"
#include "rfcinterface.h"
#include "deviceutils.h"
#include "rdk_logger_milestone.h"


// TODO - do similar to what is done for IARM eventing. Is not the primary goal of this module
/* Description: Use for sending telemetry Log
 * @param marker: use for send marker details
 * @return : void
 * */
void t2CountNotify(char *marker, int val) {
#ifdef T2_EVENT_ENABLED
    t2_event_d(marker, val);
#endif
}

void t2ValNotify( char *marker, char *val )
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
                   t2ValNotify( "certerr_split", outbuf + strlen(pStartString) );    // point to 'P' in PCDL
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
 * @brief Initialize the RDK firmware utility library
 */
int rdkv_utils_init(void)
{
    SWLOG_INFO("RDK firmware utilities library initialized\n");
    return 0;
}

/**
 * @brief Cleanup the RDK firmware utility library
 */
void rdkv_utils_cleanup(void)
{
    SWLOG_INFO("RDK firmware utilities library cleaned up\n");
}


/**
 * @brief Main firmware upgrade request function (moved from rdkv_main.c)
 * 
 * This function was extracted from rdkv_main.c to create a modular library.
 * It handles all types of firmware upgrade requests including XCONF, PCI, 
 * PDRI, and peripheral upgrades. It manages download coordination, status 
 * reporting, and error handling.
 */
int rdkv_upgrade_request(int upgrade_type, int server_type, 
                        const char* artifactLocationUrl, const void* dwlloc, 
                        char *pPostFields, int *pHttp_code,char *immed_reboot_flag,int delay_dwnl , char *lastrun, char *disableStatsUpdate, DeviceProperty_t* device_info)
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

    if (artifactLocationUrl == NULL || dwlloc == NULL || pHttp_code == NULL) {
        SWLOG_ERROR("%s: Parameter is NULL\n", __FUNCTION__);
        return ret_curl_code;
    }
    if (upgrade_type == XCONF_UPGRADE) {
        SWLOG_INFO("Trying to communicate with XCONF server");
        t2CountNotify("SYST_INFO_XCONFConnect", 1);
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
            t2CountNotify("SYS_INFO_swdltriggered", 1);
            SWLOG_INFO("PDRI Download in Progress for %s\n", dwlpath_filename);
            eventManager(IMG_DWL_EVENT, IMAGE_FWDNLD_UNINITIALIZED);
        }else if(upgrade_type == PCI_UPGRADE) {
            SWLOG_INFO("Triggering the Image Download ...\n");
            t2CountNotify("SYS_INFO_swdltriggered", 1);
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
            ret_curl_code = downloadFile(server_type, artifactLocationUrl, dwlloc, pPostFields, pHttp_code);
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
                ret_curl_code = retryDownload(server_type, artifactLocationUrl, dwlloc, pPostFields, RETRY_COUNT, 60, pHttp_code);
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
                    ret_curl_code = fallBack(server_type, artifactLocationUrl, dwlloc, pPostFields, pHttp_code);
                }
            }
        }
        else if (server_type == HTTP_SSR_CODEBIG || server_type == HTTP_XCONF_CODEBIG) {
            ret_curl_code = codebigdownloadFile(server_type, artifactLocationUrl, dwlloc, pPostFields, pHttp_code);
            if (ret_curl_code != CURL_SUCCESS ||
                (*pHttp_code != HTTP_SUCCESS && *pHttp_code != HTTP_CHUNK_SUCCESS && *pHttp_code != HTTP_PAGE_NOT_FOUND)) {
                if( ret_curl_code != CODEBIG_SIGNING_FAILED )
                {
                    // if CODEBIG_SIGNING_FAILED, no point in retrying codebig since signing won't correct
                    // but it might work when falling back to direct 
                    ret_curl_code = retryDownload(server_type, artifactLocationUrl, dwlloc, pPostFields, CB_RETRY_COUNT, 10, pHttp_code);
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
                    ret_curl_code = fallBack(server_type, artifactLocationUrl, dwlloc, pPostFields, pHttp_code);
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
                t2CountNotify("SYST_ERR_PDRIUpg_failure", 1);
            } else if (upgrade_type == XCONF_UPGRADE && ret_curl_code == 6) {
                t2CountNotify("xconf_couldnt_resolve", 1); 
            } else if (upgrade_type == PCI_UPGRADE) {
                SWLOG_ERROR("doCDL failed\n");
                t2CountNotify("SYST_ERR_CDLFail", 1);
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
            t2CountNotify("SYST_INFO_FWCOMPLETE", 1);
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
            t2CountNotify("Filesize_split", getFileSize(dwlpath_filename));
            *md5_sum = 0;
            RunCommand( eMD5Sum, dwlpath_filename, md5_sum, sizeof(md5_sum) );
            SWLOG_INFO("md5sum of %s : %s\n", dwlpath_filename, md5_sum);
            if (upgrade_type == PDRI_UPGRADE) {
                SWLOG_INFO("PDRI image upgrade successful.\n");
                t2CountNotify("SYST_INFO_PDRIUpgSuccess", 1);
            }
            if (upgrade_type == PCI_UPGRADE || upgrade_type == PDRI_UPGRADE) {
                setDwnlState(RDKV_FWDNLD_FLASH_INPROGRESS);
                snprintf(dwnl_status, sizeof(dwnl_status), "Flashing In Progress");
                snprintf(fwdls.status, sizeof(fwdls.status), "Status|%s\n", dwnl_status);
                updateFWDownloadStatus(&fwdls, disableStatsUpdate);
                flash_status  = flashImage(artifactLocationUrl, dwlpath_filename, immed_reboot_flag, "2", upgrade_type, device_info->maint_status);
                if (upgrade_type == PCI_UPGRADE) {
                    if (flash_status != 0 && upgrade_type == PCI_UPGRADE) {
                        SWLOG_ERROR("doCDL failed\n");
			t2CountNotify("SYST_ERR_CDLFail", 1);
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
