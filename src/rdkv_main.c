/*
 * Copyright 2023 Comcast Cable Communications Management, LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <pthread.h>
#include <signal.h>

#include "rdkv_cdl.h"
#include "rdkv_upgrade.h"
#include "rdkv_cdl_log_wrapper.h"
#ifndef GTEST_ENABLE
#include "downloadUtil.h"
#include "urlHelper.h"
#endif
#include "download_status_helper.h"
#include "device_status_helper.h"
#include "iarmInterface.h"
#include "rfcinterface.h"
#include "codebigUtils.h"
#include "mtlsUtils.h"
#ifndef GTEST_ENABLE
#include "rdk_fwdl_utils.h"
#include "system_utils.h"
#include "rdk_logger_milestone.h"
#else
#include "miscellaneous.h"
#endif
#include "json_process.h"
#include "device_api.h"
#include "deviceutils.h"
#include "common_device_api.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define JSON_STR_LEN        1000

#define DOWNLOADED_PERIPHERAL_VERSION "/tmp/downloaded_peripheral_versions.txt"
#define MAX_VER_LEN             10
#define TWO_FIFTY_SIX           256
#define DOWNLOADED_VERS_SIZE    TWO_FIFTY_SIX
#define URL_MAX_LEN1 URL_MAX_LEN + 128
#define DWNL_PATH_FILE_LEN1 DWNL_PATH_FILE_LEN + 32

// Below are the global variable
// TODO  Global variables should be avoided to best possible extend and used only as a very last resort !!
// Device properties is a candidate for getter only utils
DeviceProperty_t device_info; // Contains all device info
ImageDetails_t cur_img_detail; // Running Image details
Rfc_t rfc_list;

bool isCriticalUpdate = false; //This is true if rebootimead flag is true

char disableStatsUpdate[4] = { 0 }; // Use for Flag to disable STATUS_FILE updates in case of PDRI upgrade
int long_term_cert = 0; // If this value is 1 we will select the key file insted of password. 

char lastrun[64] = { 0 };  // Store last run time
char immed_reboot_flag[12] = { 0 }; // Store immediate reboot flag
int delay_dwnl = 0; // Store delay in integer format

static int proto = 1;       //0 = tftp and 1  = http
static int trigger_type = 0;
static int DwnlState = RDKV_FWDNLD_UNINITIALIZED; //Use For set download state
void *curl = NULL;
static pthread_mutex_t mutuex_dwnl_state = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t app_mode_status = PTHREAD_MUTEX_INITIALIZER;
static int app_mode = 1; // 1: fore ground and 0: background
int force_exit = 0; //This use when rdkvfwupgrader rcv appmode background and thottle speed is set to zero.

/* Description: Set App mode.
 * @param: state : set state
 * @return: void
 * */

void setAppMode(int mode)
{
    pthread_mutex_lock(&app_mode_status);
    app_mode = mode;
    SWLOG_INFO("%s: app mode = %d\n", __FUNCTION__, app_mode);
    pthread_mutex_unlock(&app_mode_status);
}
/* Description: Get App mode
 * @param: void
 * @return: int : return state
 * */
int getAppMode(void)
{
    int mode = 1;
    pthread_mutex_lock(&app_mode_status);
    mode = app_mode;
    pthread_mutex_unlock(&app_mode_status);
    SWLOG_INFO("%s: app mode = %d\n", __FUNCTION__, mode);
    return mode;
}
/* Description: Set Download state.
 * @param: state : set state
 * @return: void
 * */
void setDwnlState(int state)
{
    pthread_mutex_lock(&mutuex_dwnl_state);
    DwnlState = state;
    pthread_mutex_unlock(&mutuex_dwnl_state);
    SWLOG_INFO("%s: status = %d\n", __FUNCTION__, state);
}
/* Description: Get Download state
 * @param: void
 * @return: int : return state
 * */
int getDwnlState(void)
{
    int curdwnl_state = 0;
    pthread_mutex_lock(&mutuex_dwnl_state);
    curdwnl_state = DwnlState;
    pthread_mutex_unlock(&mutuex_dwnl_state);
    SWLOG_INFO("%s: status = %d\n", __FUNCTION__, curdwnl_state);
    return curdwnl_state;
}

/* Description: Callback function trigger by mm using iarm
 * @param: int: app_mode : foreground/background 1/0
 * @return: void
 * */
void interuptDwnl(int app_mode)
{
    int dwnl_state = 0;
    int curl_ret = 99;
    unsigned int speed = 0;
    unsigned int bytes_dwnled = 0;
    SWLOG_INFO("Checking Interupt download\n");
    setAppMode(app_mode);
    dwnl_state = getDwnlState();
    if ((0 == (strncmp(rfc_list.rfc_throttle, "true", 4))) && (dwnl_state == RDKV_FWDNLD_DOWNLOAD_INPROGRESS)) {
        bytes_dwnled = doGetDwnlBytes(curl);
        SWLOG_INFO("Bytes Downloaded = %u\n", bytes_dwnled);
        if (app_mode == 0) {
            speed = atoi(rfc_list.rfc_topspeed);
            /*If Throttle speed value is zero stopping the download */
            if (speed == 0) {
                force_exit = 1;
                SWLOG_INFO("app mode is background and download speed is set to:%d\n", speed);
                /*Below function set the value in download lib to stop the downlaod */
                setForceStop(1);
                if (!(strncmp(device_info.maint_status, "true", 4))) {
                    eventManager("MaintenanceMGR", MAINT_FWDOWNLOAD_ERROR);
                }
                eventManager(FW_STATE_EVENT, FW_STATE_FAILED);
                SWLOG_INFO("Download is going to stop\n");
                return;
            }
            if (curl != NULL && bytes_dwnled > 0) {
                SWLOG_INFO("Pause download and unpause with speed %s=>%u\n", rfc_list.rfc_topspeed, speed);
                curl_ret = doInteruptDwnl(curl, speed);
            }
        } else if (app_mode == 1){
            speed = 0;
            if (curl != NULL && bytes_dwnled > 0) {
                SWLOG_INFO("Pause download and unpause with UnThrottle mode %u\n", speed);
                curl_ret = doInteruptDwnl(curl, speed);
            }
        }
    } else {
        SWLOG_INFO("Throttle rfc=%s\nFile Download alreday completed or not started\n", rfc_list.rfc_throttle);
    }
    /* If unpause fail forefully stop the download */
    if (curl_ret == DWNL_UNPAUSE_FAIL) {
        SWLOG_ERROR("Curl Unpause fail:%d\n", curl_ret);
        doStopDownload(curl);
        curl = NULL;
    }
}

void handle_signal(int no, siginfo_t* info, void* uc)
{

    SWLOG_INFO("Raise SIGUSR1 signal\n");
    force_exit = 1;
    setForceStop(1);
    if (!(strncmp(device_info.maint_status, "true", 4))) {
        eventManager("MaintenanceMGR", MAINT_FWDOWNLOAD_ERROR);
    }
    eventManager(FW_STATE_EVENT, FW_STATE_FAILED);
    SWLOG_INFO("Download is going to stop and aborted\n");
    updateUpgradeFlag(2);
}

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

// TODO - do similar to what is done for IARM eventing. Is not the primary goal of this module
/* Description: Use for sending telemetry Log 
 * @param marker: use for send marker details
 * @return : void
 * */
/*
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
*/
/*
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
*/
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
void dwnlError(int curl_code, int http_code, int server_type)
{
    char telemetry_data[32];
    char device_type[32];
    struct FWDownloadStatus fwdls;
    char failureReason[128];
    char *type = "Direct"; //TODO: Need to pass this type as a function parameter

    *failureReason = 0;
    if(curl_code == 22) {
        snprintf(telemetry_data, sizeof(telemetry_data), "swdl_failed");
        t2CountNotify(telemetry_data, 1);
    }else if(curl_code == 18 || curl_code == 7) {
        snprintf(telemetry_data, sizeof(telemetry_data), "swdl_failed_%d", curl_code);
        t2CountNotify(telemetry_data, 1);
    }else {
        *telemetry_data = 0;
        SWLOG_ERROR("%s : CDL is suspended due to Curl %d Error\n", __FUNCTION__, curl_code);
        t2CountNotify("CDLsuspended_split", curl_code);
    }
    checkForTlsErrors(curl_code, type);
    snprintf( device_type, sizeof(device_type), "%s", device_info.dev_type );
    if(curl_code != 0 || (http_code != 200 && http_code != 206) || http_code == 495) {
        if (server_type == HTTP_SSR_DIRECT) {
            SWLOG_ERROR("%s : Failed to download image from normal SSR code download server with ret:%d, httpcode:%d\n", __FUNCTION__, curl_code, http_code);
	    t2CountNotify("SYST_ERR_cdl_ssr", 1);
            if (http_code == 302)
	    {
                t2CountNotify("SYST_INFO_Http302", 1);
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


/* Description: initialize function
 * @param: void
 * @return: int SUCCESS 1 AND FAILURE -1
 * */
int initialize(void) {
    DownloadData DwnLoc;
    int ret = -1;
    char post_data[] = "{\"jsonrpc\":\"2.0\",\"id\":\"3\",\"method\":\"org.rdk.MaintenanceManager.1.getMaintenanceMode\",\"params\":{}}";

#ifdef T2_EVENT_ENABLED
    t2_init("rdkfwupgrader");
#endif
 
    *cur_img_detail.cur_img_name = 0;
    *rfc_list.rfc_incr_cdl = 0;
    *rfc_list.rfc_mtls = 0;
    *rfc_list.rfc_throttle = 0;
    *rfc_list.rfc_topspeed = 0;

    ret = getDeviceProperties(&device_info);
    if(-1 == ret) {
        SWLOG_INFO("getDeviceProperties() return fail\n");
        return ret ;
    }
    ret = getImageDetails(&cur_img_detail);
    if(-1 == ret) {
        SWLOG_INFO("getImageDetails() return fail\n");
        return ret ;
    }
    getRFCSettings(&rfc_list);
    ret = createDir(device_info.difw_path);
    if (-1 == ret) {
        SWLOG_INFO("createDir() return fail. Dir name:%s\n", device_info.difw_path);
        return ret;
    }
    init_event_handler();
    if (0 == (strncmp(device_info.maint_status, "true", 4))) {
        DwnLoc.pvOut = NULL;
        DwnLoc.datasize = 0;
        DwnLoc.memsize = 0;
        if( allocDowndLoadDataMem( &DwnLoc, DEFAULT_DL_ALLOC ) == 0 )
        {
            getJsonRpc(post_data, &DwnLoc);
            if( (strstr((char*)DwnLoc.pvOut, "BACKGROUND")) != NULL ) {
                SWLOG_INFO("%s: Setting mode to BACKGROUND\n", __FUNCTION__);
                setAppMode(0);//MM mode is background so set to 0
            }
            free( DwnLoc.pvOut );
        }
    }
    return 1;
}

/* Description: uninitialize function
 * @param: void
 * @return: void
 * */
void uninitialize(int fwDwnlStatus) {
#ifdef T2_EVENT_ENABLED
    t2_uninit();
#endif
    pthread_mutex_destroy(&mutuex_dwnl_state);
    pthread_mutex_destroy(&app_mode_status);
    term_event_handler();
    updateUpgradeFlag(2);
    if((fwDwnlStatus != INITIAL_VALIDATION_DWNL_INPROGRESS) && ((filePresentCheck(DIFDPID)) == 0)) {
        SWLOG_INFO("Deleting DIFD.pid file\n");
        unlink(DIFDPID);
    }
    log_exit();
}

/* Description: Save http value inside file
 * @param: http_code : http value after curl command return
 * @return: void
 * */
void saveHTTPCode(int http_code)
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

/* Description: Use for download image from codebig
 * @param: artifactLocationUrl : server url
 * @param: localDownloadLocation : download location
 * @param: httpCode : send back http value
 * @return int: success/failure
 * */
#ifndef GTEST_BASIC
int codebigdownloadFile( int server_type, const char* artifactLocationUrl, const void* localDownloadLocation, char* pPostFields, int *httpCode ) {
    int signFailed = 1;           // 0 for success, 1 indicates failed
    FileDwnl_t file_dwnl;
    char oAuthHeader[BIG_BUF_LEN]  = "Authorization: OAuth realm=\"\", ";
    int curl_ret_code = -1;
    char headerInfoFile[136];

    if (artifactLocationUrl == NULL || localDownloadLocation == NULL || httpCode == NULL) {
        SWLOG_ERROR("%s: Parameter is NULL\n", __FUNCTION__);
        return curl_ret_code;
    }
    *httpCode = 0;

#ifdef DEBUG_CODEBIG_CDL
    if( filePresentCheck( "/tmp/.forceCodebigFailure" ) == RDK_API_SUCCESS )
    {
        SWLOG_ERROR("%s:  Forcing Codebig Failure!!\n", __FUNCTION__);
        saveHTTPCode(*httpCode);
        return CURLTIMEOUT;     // timeout error
    }
#endif

    if (isDwnlBlock(server_type)) {
            SWLOG_ERROR("%s: Codebig Download is block\n", __FUNCTION__);
            curl_ret_code = DWNL_BLOCK;
            return curl_ret_code;
    }

    SWLOG_INFO("Using Codebig Image upgrade connection\nCheck if codebig is applicable for the Device\n");
    t2CountNotify("SYST_INFO_cb_xconf", 1);
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
        curl = doCurlInit();
        if (curl != NULL) {
            if (server_type == HTTP_XCONF_CODEBIG) {
                setDwnlState(RDKV_XCONF_FWDNLD_DOWNLOAD_INPROGRESS);
            } else {
                setDwnlState(RDKV_FWDNLD_DOWNLOAD_INPROGRESS);
            }
            curl_ret_code = doAuthHttpFileDownload(curl ,&file_dwnl, httpCode); // Handle return status
            if (server_type == HTTP_XCONF_CODEBIG) {
                setDwnlState(RDKV_XCONF_FWDNLD_DOWNLOAD_EXIT);
            } else {
                setDwnlState(RDKV_FWDNLD_DOWNLOAD_EXIT);
            }

            doStopDownload(curl);
            curl = NULL;
            /* Stop the donwload if Throttle speed rfc is set to zero */
            if (force_exit == 1 && (curl_ret_code == 23)) {
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
            t2CountNotify("SYS_INFO_CodBPASS", 1);
        }
    }
    else
    {
        *httpCode = 0;
        curl_ret_code = CODEBIG_SIGNING_FAILED;
        SWLOG_ERROR( "%s : Codebig signing failed, server type = %d, aborting download!!\n", __FUNCTION__, server_type );
    }
    saveHTTPCode(*httpCode);
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
int downloadFile( int server_type, const char* artifactLocationUrl, const void* localDownloadLocation, char* pPostFields, int *httpCode ){

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
    int curl_ret_code = -1;
    FileDwnl_t file_dwnl;
    int chunk_dwnl = 0;
    int mtls_enable = 1; //Setting mtls by default enable
    char headerInfoFile[136] = {0};

    if (artifactLocationUrl == NULL || localDownloadLocation == NULL || httpCode == NULL) {
        SWLOG_ERROR("%s: Parameter is NULL\n", __FUNCTION__);
        return ret;
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
    }
    else        // server_type must be HTTP_XCONF_DIRECT, store to memory not a file
    {
        file_dwnl.pDlData = (DownloadData *)localDownloadLocation;
        *(file_dwnl.pathname) = 0;
    }
    file_dwnl.pPostFields = pPostFields;

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
        t2CountNotify("SYST_INFO_TLS_xconf", 1);
    }
    if ((1 == (isThrottleEnabled(device_info.dev_name, immed_reboot_flag, app_mode)))) {
        if (0 == (strncmp(rfc_list.rfc_throttle, "true", 4))) {
            max_dwnl_speed = atoi(rfc_list.rfc_topspeed);
            SWLOG_INFO("%s : Throttle feature is Enable\n", __FUNCTION__);
            t2CountNotify("SYST_INFO_Thrtl_Enable", 1);
            if (max_dwnl_speed == 0) {
                SWLOG_INFO("%s : Throttle speed set to 0. So exiting the download process\n", __FUNCTION__);
                if (!(strncmp(device_info.maint_status, "true", 4))) {
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
    getPidStore(device_info.dev_name, device_info.maint_status); //TODO: Added for script support. Need to remove later    
    if ((strcmp(disableStatsUpdate, "yes")) && (server_type == HTTP_SSR_DIRECT)) {
        chunk_dwnl = isIncremetalCDLEnable(file_dwnl.pathname);
    }
#ifndef LIBRDKCERTSELECTOR	
    SWLOG_INFO("Fetching MTLS credential for SSR/XCONF\n");
    ret = getMtlscert(&sec);
    if (-1 == ret) {
        SWLOG_ERROR("%s : getMtlscert() Featching MTLS fail. Going For NON MTLS:%d\n", __FUNCTION__, ret);
        mtls_enable = -1;//If certificate or key featching fail try with non mtls
    }else {
        SWLOG_INFO("MTLS is enable\nMTLS creds for SSR fetched ret=%d\n", ret);
        t2CountNotify("SYS_INFO_MTLS_enable", 1);
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
            t2CountNotify("SYS_INFO_MTLS_enable", 1);
	}
#endif
        do {
            if ((1 == state_red)) {
                SWLOG_INFO("RED:state red recovery attempting MTLS connection to XCONF server\n");
                if (CHUNK_DWNL_ENABLE == chunk_dwnl) {
	            SWLOG_INFO("RED: Calling  chunkDownload() in state red recovery\n");
                    t2CountNotify("SYST_INFO_RedStateRecovery", 1);
	            curl_ret_code = chunkDownload(&file_dwnl, &sec, max_dwnl_speed, httpCode);
	            break;
	        }else {
                curl = doCurlInit();
	            if (curl != NULL) {
	                (server_type == HTTP_SSR_DIRECT) ? setDwnlState(RDKV_FWDNLD_DOWNLOAD_INPROGRESS) : setDwnlState(RDKV_XCONF_FWDNLD_DOWNLOAD_INPROGRESS);
                    curl_ret_code = doHttpFileDownload(curl, &file_dwnl, &sec, max_dwnl_speed, NULL, httpCode);
	                (server_type == HTTP_SSR_DIRECT) ? setDwnlState(RDKV_FWDNLD_DOWNLOAD_EXIT) : setDwnlState(RDKV_XCONF_FWDNLD_DOWNLOAD_EXIT);
                    if (curl != NULL) {
                        doStopDownload(curl);
	                    curl = NULL;
                    }
	                if (force_exit == 1 && (curl_ret_code == 23)) {
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
                      curl = doCurlInit();
                      if (curl != NULL) {
                          (server_type == HTTP_SSR_DIRECT) ? setDwnlState(RDKV_FWDNLD_DOWNLOAD_INPROGRESS) : setDwnlState(RDKV_XCONF_FWDNLD_DOWNLOAD_INPROGRESS);
                          curl_ret_code = doHttpFileDownload(curl, &file_dwnl, &sec, max_dwnl_speed, NULL, httpCode);
                          (server_type == HTTP_SSR_DIRECT) ? setDwnlState(RDKV_FWDNLD_DOWNLOAD_EXIT) : setDwnlState(RDKV_XCONF_FWDNLD_DOWNLOAD_EXIT);
                          if (curl != NULL) {
                            doStopDownload(curl);
                            curl = NULL;
                          }
	                  if (force_exit == 1 && (curl_ret_code == 23)) {
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
                    curl = doCurlInit();
                    if (curl != NULL) {
                        (server_type == HTTP_SSR_DIRECT) ? setDwnlState(RDKV_FWDNLD_DOWNLOAD_INPROGRESS) : setDwnlState(RDKV_XCONF_FWDNLD_DOWNLOAD_INPROGRESS);
                        curl_ret_code = doHttpFileDownload(curl, &file_dwnl, NULL, max_dwnl_speed, NULL, httpCode);
                        (server_type == HTTP_SSR_DIRECT) ? setDwnlState(RDKV_FWDNLD_DOWNLOAD_EXIT) : setDwnlState(RDKV_XCONF_FWDNLD_DOWNLOAD_EXIT);
                        if (curl != NULL) {
                            doStopDownload(curl);
                            curl = NULL;
                        }
	                if (force_exit == 1 && (curl_ret_code == 23)) {
	                    uninitialize(INITIAL_VALIDATION_SUCCESS);
	                    exit(1);
                        }
                    }
                }
            }
            if (strcmp(disableStatsUpdate, "yes") && (CHUNK_DWNL_ENABLE != chunk_dwnl)) {
                chunk_dwnl = isIncremetalCDLEnable(file_dwnl.pathname);
            }
            SWLOG_INFO("%s : After curl request the curl status = %d and http=%d and chunk download=%d\n", __FUNCTION__, curl_ret_code, *httpCode, chunk_dwnl);
        } while(chunk_dwnl && (CURL_LOW_BANDWIDTH == curl_ret_code || CURLTIMEOUT == curl_ret_code));
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
            SWLOG_INFO("%s : Direct Image upgrade Success: curl ret:%d http_code:%d\n", __FUNCTION__, curl_ret_code, *httpCode);
            t2CountNotify("SYS_INFO_DirectSuccess", 1);
        }
        else
        {
            SWLOG_INFO("%s : Direct Image upgrade connection success: curl ret:%d http_code:%d\n", __FUNCTION__, curl_ret_code, *httpCode);
        }
    }else {
        SWLOG_ERROR("%s : Direct Image upgrade Fail: curl ret:%d http_code:%d\n", __FUNCTION__, curl_ret_code, *httpCode);
        (server_type == HTTP_SSR_DIRECT) ? setDwnlState(RDKV_FWDNLD_DOWNLOAD_FAILED) : setDwnlState(RDKV_XCONF_FWDNLD_DOWNLOAD_FAILED);
        dwnlError(curl_ret_code, *httpCode, server_type);
        if( *(file_dwnl.pathname) != 0 )
        {
            unlink(file_dwnl.pathname);
            unlink(headerInfoFile);
        }
    }
    saveHTTPCode(*httpCode);
    return curl_ret_code;
}
#endif

/* Description: Download retry logic
 * @param: server_type : Type of the server.
 * @param: artifactLocationUrl : server url
 * @param: localDownloadLocation : Download path.
 * @param: retry_cnt : Use for retry logic.
 * @param: delay : delay between retry
 * @param: httCode : Send back httpCode 
 * @return int:  Success/Failure
 * */
int retryDownload(int server_type, const char* artifactLocationUrl, const void* localDownloadLocation, char *pPostFields, int retry_cnt, int delay, int *httpCode ){
    int curl_ret_code = -1;
    int retry_completed = 1;
    
    if (artifactLocationUrl == NULL || localDownloadLocation == NULL || httpCode == NULL) {
        SWLOG_ERROR("%s: Parameter is NULL\n", __FUNCTION__);
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
            curl_ret_code = downloadFile(server_type, artifactLocationUrl, localDownloadLocation, pPostFields, httpCode);
            if ((curl_ret_code == CURL_SUCCESS) && (*httpCode == HTTP_SUCCESS || *httpCode == HTTP_CHUNK_SUCCESS)) {
	        if(server_type == HTTP_SSR_DIRECT)
	        {
	            SWLOG_INFO("%s : Direct Image upgrade Success: ret:%d http_code:%d\n", __FUNCTION__, curl_ret_code, *httpCode);
	            t2CountNotify("SYS_INFO_DirectSuccess", 1);
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
            curl_ret_code = codebigdownloadFile(server_type, artifactLocationUrl, localDownloadLocation, pPostFields, httpCode);
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

/* Description: Use for fall back between direct to codebig and vise versa
 * @param: server_type : Type of the server.
 * @param: artifactLocationUrl : server url
 * @param: localDownloadLocation : Download path.
 * @param: httCode : Send back httpCode 
 * @return int:  Success/Failure
 * */
int fallBack(int server_type, const char* artifactLocationUrl, const void* localDownloadLocation, char *pPostFields, int *httpCode) {
    int curl_ret_code = -1;

    if (artifactLocationUrl == NULL || localDownloadLocation == NULL || httpCode == NULL) {
        SWLOG_ERROR("%s: Parameter is NULL\n", __FUNCTION__);
        return curl_ret_code;
    }

    if (server_type == HTTP_SSR_DIRECT || server_type == HTTP_XCONF_DIRECT) {
        SWLOG_INFO("%s: calling downloadFile\n", __FUNCTION__ );
        curl_ret_code = downloadFile(server_type, artifactLocationUrl, localDownloadLocation, pPostFields, httpCode);
        if (*httpCode != HTTP_SUCCESS && *httpCode != HTTP_CHUNK_SUCCESS && *httpCode != 404) {
            SWLOG_ERROR("%s : Direct image upgrade failover request failed return=%d, httpcode=%d\n", __FUNCTION__, curl_ret_code, *httpCode);
        } else {
            SWLOG_INFO("%s : Direct image upgrade failover request received return=%d, httpcode=%d\n", __FUNCTION__, curl_ret_code, *httpCode);
        }
    } else if (server_type == HTTP_SSR_CODEBIG || server_type == HTTP_XCONF_CODEBIG) {
        //curl_ret_code = codebigdownloadFile(artifactLocationUrl, localDownloadLocation, httpCode);
        SWLOG_INFO("%s: calling retryDownload\n", __FUNCTION__ );
        curl_ret_code = retryDownload(server_type, artifactLocationUrl, localDownloadLocation, pPostFields, CB_RETRY_COUNT, 10, httpCode);
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

//TODO: Below functio is use only for temporary. Once all script convert to C
//      We have to change this logic.
void updateUpgradeFlag(int action)
{
    char *flag_file = NULL;
    if (0 == (strncmp(device_info.dev_type, "mediaclient", 11))) {
        flag_file = "/tmp/.imageDnldInProgress";
    } else if (proto == 1) {
        flag_file = HTTP_CDL_FLAG;
    } else {
        flag_file = SNMP_CDL_FLAG;
    }
    if (action == 1) {
        createFile(flag_file);
    } else if (action == 2 && (0 == (filePresentCheck(flag_file)))) {
        unlink(flag_file);
    }
}

/* Description: Use for requesting upgrade pci/pdri */
 // @param: upgrade_type : pci/pdri
// upgradeRequest() function has been moved to librdkv_upgrade.so
// Use rdkv_upgrade_request() from rdkv_upgrade.h instead

int getOPTOUTValue(const char *file_name)
{
    int status = -1;
    FILE *fp = NULL;
    char tbuff[80] = {0};

    if (file_name == NULL) {
        return status;
    }

    fp = fopen(file_name, "r");
    if (fp != NULL) {
        while(NULL != fgets(tbuff, sizeof(tbuff), fp)) {
            if (strstr(tbuff,"softwareoptout")) {
                SWLOG_INFO("softwareoptout value =%s.\n", tbuff);
		break;
	    }
	}
    } else {
        SWLOG_ERROR("Unable to open file=%s\n", file_name);
	return status;
    }

    if (strstr(tbuff, "IGNORE_UPDATE")) {
        status = 1;
    }else if(strstr(tbuff, "ENFORCE_OPTOUT")) {
        status = 0;
    }
    fclose(fp);
    return status;
}

int peripheral_firmware_dndl( char *pCloudFWLocation, char *pPeripheralFirmwares )
{
    FILE *fp;
    char *pPeriphFW;
    char *pSavedFW;
    char *pSavedDetails;
    char *pFW;
    char *pDeviceName;      // based upon function input arg pPeripheralFirmwares
    char *pDeviceType;      // based upon function input arg pPeripheralFirmwares
    char *pDeviceVer;       // based upon function input arg pPeripheralFirmwares
    char *pPrevDlVers = NULL;
    char *pDownloadedVers;
    char *pTmp;
    size_t szLen;
    size_t szRunningLen = 0;
    int iVerCmp;
    int iRet = 0;
    int http_code;
    int iCurlCode;
    char *pCurVer;
    char *pCurFW;
    char cDLStoreLoc[DWNL_PATH_FILE_LEN];       // TODO: dynamically allocate buffers
    char cSourceURL[URL_MAX_LEN];
    char cTmpCloudFW[100];
    char cCurVerBuf[200];
    char cTmpCurVerBuf[sizeof(cCurVerBuf)];
    char cTmpPrevVerBuf[sizeof(cCurVerBuf)];
    bool bTriggerDL;


    SWLOG_INFO( "%s: pPeripheralFirmwares = %s\n", __FUNCTION__, pPeripheralFirmwares );

    GetFileContents( &pPrevDlVers, DOWNLOADED_PERIPHERAL_VERSION );
    if( pPrevDlVers != NULL)
    {
        SWLOG_INFO( "%s: PrevDownload Versions = %s\n", __FUNCTION__, pPrevDlVers  );
    }

    szLen = strnlen( pPeripheralFirmwares, 959 ) + 1;       // 959 is just some sort of arbitrary limit
    pPeriphFW = malloc( szLen );    // make room for a copy of input arg plus NULL byte
    if( pPeriphFW != NULL )
    {
        pDownloadedVers = malloc( DOWNLOADED_VERS_SIZE );
        if( pDownloadedVers != NULL )
        {
            *pDownloadedVers = 0;

            GetRemoteVers( cCurVerBuf, sizeof(cCurVerBuf) );    // what's in the device
            SWLOG_INFO( "%s: GetRemoteVers found cCurVerBuf = %s\n", __FUNCTION__,  cCurVerBuf );
            snprintf( pPeriphFW, szLen, "%s", pPeripheralFirmwares );
            pFW = strtok_r( pPeriphFW, ",", &pSavedFW );

            while( pFW != NULL )
            {
                bTriggerDL = true;

                SWLOG_INFO( "%s: pFW = %s\n", __FUNCTION__, pFW );
                snprintf( cTmpCloudFW, sizeof(cTmpCloudFW), "%s", pFW );      //save a copy, example XR11-20_firmware_1.1.4.1

                pDeviceName = strtok_r( cTmpCloudFW, "_", &pSavedDetails );
                pDeviceType = strtok_r( NULL, "_", &pSavedDetails );    // should be pointing to firmware, audio, dsp, or kw_model
                pDeviceVer = strtok_r( NULL, "_", &pSavedDetails );     // really just the last bit

                SWLOG_INFO( "%s: pDeviceName = %s\n", __FUNCTION__, pDeviceName );
                SWLOG_INFO( "%s: pDeviceType = %s\n", __FUNCTION__, pDeviceType );
                SWLOG_INFO( "%s: pDeviceVer = %s\n", __FUNCTION__, pDeviceVer );


                if( bTriggerDL == true && pPrevDlVers != NULL && *pPrevDlVers )
                {
                    snprintf( cTmpPrevVerBuf, sizeof(cTmpPrevVerBuf), "%s", pPrevDlVers );
                    pTmp = strtok_r( cTmpPrevVerBuf, ",", &pSavedDetails );

                    while( pTmp != NULL )
                    {
                        SWLOG_INFO( "%s: Finding pDeviceType = %s and pDeviceVer= %s in pTmp = %s\n", __FUNCTION__, pDeviceType, pDeviceVer, pTmp );
                        if( strstr( pTmp, pDeviceName ) && strstr( pTmp, pDeviceType ) && strstr( pTmp, pDeviceVer ) )  // we've already downloaded this version
                        {
                            bTriggerDL = false;
                            SWLOG_INFO( "%s: Prev downloaded FW and requested cloud FW download versions are the same (%s)\n", __FUNCTION__, pDeviceVer );
                            break;              // no point in continuing
                        }

                        pTmp = strtok_r( NULL, ",", &pSavedDetails );
                    }
                }

                if( bTriggerDL == true )
                {
                    snprintf( cTmpCurVerBuf, sizeof(cTmpCurVerBuf), "%s", cCurVerBuf );     // makes a copy again
                    pCurFW = strtok_r( cTmpCurVerBuf, ",", &pSavedDetails );
                    while( pCurFW != NULL )
                    {
                   
                        // pCurVer == the current version in the device
                        // pDeviceVer == the version xconf says to load
                        // the strncmp works as long as versions are equal length
                        // if they are unequal lengths, pDeviceVer = "1.4.0.0" and pCurVer = "1.4.0", then
                        // the strncmp will be a positive value causing a peripheral upgrade.
                        if( (pDeviceName != NULL) && strstr( pCurFW, pDeviceName ) && strstr( pCurFW, pDeviceType) )
                        {
                            if( (pCurVer=strrchr( pCurFW, '_' )) != NULL )  // find last underscore char ('_')
                            {
                                ++pCurVer;          // point to character after '_' 
                                iVerCmp = strncmp( pDeviceVer, pCurVer, MAX_VER_LEN );
                                SWLOG_INFO( "%s: Compared pDeviceVer = %s and pCurVer = %s, output = %d\n", __FUNCTION__, pDeviceVer, pCurVer, iVerCmp );
                                if( iVerCmp <= 0 )     // the version in the device is newer or same, no upgrade needed
                                {
                                    SWLOG_INFO( "%s: The version in the device is newer or same, no upgrade needed\n", __FUNCTION__ );
                                    bTriggerDL = false;
                                }
                            }
                        }
                        pCurFW = strtok_r( NULL, ",", &pSavedDetails );
                    }                
                }
                if( bTriggerDL == true )
                {
                    snprintf( cSourceURL, sizeof(cSourceURL), "%s/%s.tgz", pCloudFWLocation, pFW );
                    snprintf( cDLStoreLoc, sizeof(cDLStoreLoc), "%s/%s.tgz",  device_info.difw_path, pFW );         // where on the device the file downloads to

                    SWLOG_INFO( "%s: firmware filename with path = %s\n", __FUNCTION__,  cDLStoreLoc );
                    if( filePresentCheck( cDLStoreLoc ) != 0 )
                    {
                        snprintf( cTmpCurVerBuf, sizeof(cTmpCurVerBuf), "%s_%s_", pDeviceName, pDeviceType );   // reuse cTmpCurVerBuf
                        eraseTGZItemsMatching( device_info.difw_path, cTmpCurVerBuf );
                    }

                    SWLOG_INFO( "%s: Requesting upgrade to %s from %s\n", __FUNCTION__, cDLStoreLoc, cSourceURL );
                    iCurlCode = rdkv_upgrade_request( PERIPHERAL_UPGRADE, HTTP_SSR_DIRECT, cSourceURL, cDLStoreLoc, NULL, &http_code,immed_reboot_flag,delay_dwnl , lastrun, disableStatsUpdate, &device_info);
                    if( iCurlCode == 0 && http_code == 200 )
                    {
                        if( szRunningLen )
                        {
                            snprintf( pDownloadedVers + szRunningLen, DOWNLOADED_VERS_SIZE - szRunningLen, "," );
                            ++szRunningLen;
                        }
                        szRunningLen += snprintf( pDownloadedVers + szRunningLen,
                                                   DOWNLOADED_VERS_SIZE - szRunningLen, "%s.tgz", pFW );

                        snprintf( cTmpPrevVerBuf, sizeof(cTmpPrevVerBuf), "%s.tgz is successful", pFW );      // reuse buf
                        t2ValNotify( "xr_fwdnld_split", cTmpPrevVerBuf );
                    }
                    else
                    {
                        iRet = -1;
                        SWLOG_ERROR( "%s: Peripheral download failed with curl return = %d, http_code = %d\n",
                                     __FUNCTION__, iCurlCode, http_code );
                    }
                }

                pFW = strtok_r( NULL, ",", &pSavedFW );
            }

            if( *pDownloadedVers )
            {
                if( (fp=fopen( DOWNLOADED_PERIPHERAL_VERSION, "a")) != NULL )
                {
                    fprintf( fp, "%s", pDownloadedVers );
                    fclose( fp );
                }
                else
                {
                    SWLOG_ERROR( "%s: Unable to open %s for appending\n", __FUNCTION__, DOWNLOADED_PERIPHERAL_VERSION );
                }
                snprintf( cSourceURL, sizeof(cSourceURL), "%s:%s", device_info.difw_path, pDownloadedVers );    // reuse this buffer, no longer needed
                eventManager(  "PeripheralUpgradeEvent", cSourceURL );
            }
            free( pDownloadedVers );
        }
        free( pPeriphFW );
    }

    if( pPrevDlVers != NULL)
    {
        free( pPrevDlVers );
    }

    return iRet;
}


int checkTriggerUpgrade(XCONFRES *pResponse, const char *model)
{
    int http_code;
    int upgrade_status = -1;
    bool valid_pci_status = false;
    int pci_curl_code = -1;
    int pdri_curl_code = -1;
    int peripheral_curl_code = -1;
    char imageHTTPURL[URL_MAX_LEN1];
    char dwlpath_filename[DWNL_PATH_FILE_LEN1];
    FILE *fp = NULL;
    int optout = -1;

    imageHTTPURL[0] = 0;
    dwlpath_filename[0] = 0;
    if (model == NULL) {
        SWLOG_ERROR("%s : Parameter is NULL\n", __FUNCTION__);
        return upgrade_status;
    }
    if (true == isUpgradeInProgress()) {
        SWLOG_ERROR("Exiting from DEVICE INITIATED HTTP CDL\nAnother upgrade is in progress\n");
        if (!(strncmp(device_info.maint_status, "true", 4))) {
            eventManager("MaintenanceMGR", MAINT_FWDOWNLOAD_ERROR);
        }
        uninitialize(INITIAL_VALIDATION_SUCCESS);
        exit(1);
    }
    if ((strstr(pResponse->cloudFWVersion, model)) == NULL) {
        SWLOG_INFO("cloudFWVersion is empty. Do Nothing\n");
        eventManager(FW_STATE_EVENT, FW_STATE_FAILED);
    }
    snprintf(immed_reboot_flag, sizeof(immed_reboot_flag), "%s", pResponse->cloudImmediateRebootFlag);
    delay_dwnl = atoi(pResponse->cloudDelayDownload);
    SWLOG_INFO("%s: reboot_flag =%s and delay_dwnl=%d\n", __FUNCTION__, immed_reboot_flag, delay_dwnl);
    valid_pci_status = checkForValidPCIUpgrade(trigger_type, cur_img_detail.cur_img_name, pResponse->cloudFWVersion, pResponse->cloudFWFile);//TODO: Trigger type should recived from script as a argument
    if (valid_pci_status == true) {
        SWLOG_INFO("checkForValidPCIUpgrade return true\n");
        if (0 == strncmp(device_info.maint_status, "true", 4)) {
            if ((strncmp(pResponse->cloudImmediateRebootFlag, "true", 4)) == 0) {
                isCriticalUpdate = true;
            }
            if (0 == strncmp(device_info.sw_optout, "true", 4)) {
                optout = getOPTOUTValue("/opt/maintenance_mgr_record.conf");
                if (optout == 1 && isCriticalUpdate != true) {
                    SWLOG_INFO("OptOut: IGNORE UPDATE is set.Exiting !!\n");
                    eventManager("MaintenanceMGR", MAINT_FWDOWNLOAD_ABORTED);
                    uninitialize(INITIAL_VALIDATION_SUCCESS);
                    exit(1);//TODO
                }else if((0 == optout) && (trigger_type != 4)) {
                    eventManager(FW_STATE_EVENT, FW_STATE_ONHOLD_FOR_OPTOUT);
                    SWLOG_INFO("OptOut: Event sent for on hold for OptOut\n");
                    eventManager("MaintenanceMGR" ,MAINT_FWDOWNLOAD_COMPLETE);
                    SWLOG_INFO("OptOut: Consent Required from User\n");
                    t2CountNotify("SYST_INFO_NoConsentFlash", 1);
                    uninitialize(INITIAL_VALIDATION_SUCCESS);
                    exit(1);//TODO
                }
	        }
    	}
        snprintf(imageHTTPURL, sizeof(imageHTTPURL), "%s/%s", pResponse->cloudFWLocation, pResponse->cloudFWFile);
        SWLOG_INFO("imageHTTPURL=%s\n", imageHTTPURL);
        fp = fopen(DWNL_URL_VALUE, "w");
        if (fp != NULL) {
            fprintf(fp, "%s\n", imageHTTPURL);
            fclose(fp);
        }
        snprintf(dwlpath_filename, sizeof(dwlpath_filename), "%s/%s", device_info.difw_path, pResponse->cloudFWFile);
	    SWLOG_INFO("DWNL path with img name=%s\n", dwlpath_filename);
        eraseFolderExcePramaFile(device_info.difw_path, pResponse->cloudFWFile, device_info.model);
        pci_curl_code = rdkv_upgrade_request(PCI_UPGRADE, HTTP_SSR_DIRECT, imageHTTPURL, dwlpath_filename, NULL, &http_code,immed_reboot_flag,delay_dwnl , lastrun, disableStatsUpdate, &device_info);
    } else {
        SWLOG_INFO("checkForValidPCIUpgrade return false\n");
        pci_curl_code = 0;
    }
    if ((strstr(pResponse->cloudPDRIVersion, model)) && true == (isPDRIEnable())) {
        if ((0 == strncmp(pResponse->cloudImmediateRebootFlag, "true", 4)) && (true == valid_pci_status)) {
            SWLOG_INFO("cloudImmediateRebootFlag is true, PCI Upgrade is required. Skipping PDRI upgrade check ... \n");
            return 0;
        } else {
            SWLOG_INFO("cloudImmediateRebootFlag is %s. Starting PDRI upgrade check ... \n", pResponse->cloudImmediateRebootFlag);
            if ((strstr(pResponse->cloudPDRIVersion, ".bin")) == NULL) {
                optout = strnlen( pResponse->cloudPDRIVersion, sizeof(pResponse->cloudPDRIVersion) );   // reuse variable
                snprintf( pResponse->cloudPDRIVersion + optout, sizeof(pResponse->cloudPDRIVersion) - optout, ".bin" ); // prevent buffer overflow
                SWLOG_INFO("Added .bin in pdri image=%s\n", pResponse->cloudPDRIVersion);
            }
            snprintf(imageHTTPURL, sizeof(imageHTTPURL), "%s/%s", pResponse->cloudFWLocation, pResponse->cloudPDRIVersion);
            SWLOG_INFO("pdri imageHTTPURL=%s\n", imageHTTPURL);
            snprintf(dwlpath_filename, sizeof(dwlpath_filename), "%s/%s", device_info.difw_path, pResponse->cloudPDRIVersion);
            SWLOG_INFO("pdri DWNL path with img name=%s\n", dwlpath_filename);
            if (true == valid_pci_status && pci_curl_code == 0) {
                SWLOG_INFO("Adding a sleep of 30secs to avoid the PCI PDRI race condition during flashing\n");
                sleep(30);
            }
            snprintf(disableStatsUpdate, sizeof(disableStatsUpdate), "%s","yes");
            pdri_curl_code = rdkv_upgrade_request(PDRI_UPGRADE, HTTP_SSR_DIRECT, imageHTTPURL, dwlpath_filename, NULL, &http_code,immed_reboot_flag,delay_dwnl , lastrun, disableStatsUpdate, &device_info);
            snprintf(disableStatsUpdate, sizeof(disableStatsUpdate), "%s","no");
            if (pdri_curl_code == 100) {
                pdri_curl_code = 0;
            }
        }
    } else {
        SWLOG_INFO("cloudPDRIfile is empty. Do Nothing\n");
        pdri_curl_code = 0;
    }
    if ((0 == (filePresentCheck("/etc/os-release"))) && (*pResponse->peripheralFirmwares != 0)) {
        strncat(pResponse->peripheralFirmwares, ",", sizeof(pResponse->peripheralFirmwares) - 1);
        pResponse->peripheralFirmwares[sizeof(pResponse->peripheralFirmwares) - 1] = '\0';
	SWLOG_INFO("Triggering Peripheral Download cloudFWLocation: %s\nperipheralFirmwares: %s\n", pResponse->cloudFWLocation, pResponse->peripheralFirmwares);
        peripheral_curl_code = peripheral_firmware_dndl( pResponse->cloudFWLocation, pResponse->peripheralFirmwares );
        SWLOG_INFO("After Trigger Peripheral Download status=%d\n", peripheral_curl_code);
    } else {
        SWLOG_INFO("Skipping Peripheral Download\n");
    }
    if ((pci_curl_code == 0) && (pdri_curl_code == 0)) {
        upgrade_status = 0;
    }
    return upgrade_status;
}
/* function startFactoryProtectService - Use to start FactoryProtect service to change security stage
   @param : void

    @return: 0 on success, -1 otherwise
*/
int startFactoryProtectService(void)
{
    void *Curl_req = NULL;
    char token[256];
    char jsondata[256];
    int httpCode = 0;
    FileDwnl_t req_data;
    int curl_ret_code = -1;
    char header[64]  = "Content-Type: application/json";
    char token_header[300];
    char url[128] = "http://127.0.0.1:9998/Service/Controller/Activate/org.rdk.FactoryProtect.1";

    *token = 0;
    *jsondata = 0;
    RunCommand( eWpeFrameworkSecurityUtility, NULL, jsondata, sizeof(jsondata) );
    
    SWLOG_INFO("token jsondata=%s\n", jsondata);
    getJRPCTokenData(token, jsondata, sizeof(token));
    SWLOG_INFO("token after parse=%s\n", token);
    req_data.pHeaderData = header;
    req_data.pDlHeaderData = NULL;
    snprintf(token_header, sizeof(token_header), "Authorization: Bearer %s", token);
    req_data.pPostFields = NULL;
    req_data.pDlData = NULL;
    snprintf(req_data.url, sizeof(req_data.url), "%s", url);
    SWLOG_INFO("%s: url=%s\n", __FUNCTION__, req_data.url);
    SWLOG_INFO("%s: header=%s\n", __FUNCTION__, req_data.pHeaderData);
    SWLOG_INFO("%s: token_header=%s\n", __FUNCTION__, token_header);
    Curl_req = doCurlInit();
    if (Curl_req != NULL) {
        curl_ret_code = doCurlPutRequest(Curl_req, &req_data, token_header, &httpCode );
        SWLOG_INFO("%s: curl ret code=%d and http code = %d\n", __FUNCTION__, curl_ret_code, httpCode);
        doStopDownload(Curl_req);
    }else {
        SWLOG_ERROR("%s: doCurlInit fail\n", __FUNCTION__);
    }
    return curl_ret_code;
}

/* function MakeXconfComms - makes the communication with the XCONF server
 
        Usage: int MakeXconfComms <XCONFRES *pResponse> <int server_type> <char *pMoreJsonIn>
 
            pResponse - pointer to a XCONFRES structure to receive the output responses.

            server_type - the kind of server to communicate with.
 
            pHttp_code - pointer to an integer that will contain the HTTP output code.
 
            RETURN - 0 on success, non-zero otherwise
 
            PITFALLS - input arguments are not checked for NULL. Call the function correctly!
*/
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
                    ret = rdkv_upgrade_request( XCONF_UPGRADE, server_type, pServURL, &DwnLoc, pJSONStr, pHttp_code, immed_reboot_flag, delay_dwnl , lastrun,disableStatsUpdate, &device_info);
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
/* function copyFile() - copy one file data to another file
        RETURN - 0 on success, -1 on fail
*/
int copyFile(const char *src, const char *target)
{
    int ret = -1;
    FILE *src_fp = NULL;
    FILE *target_fp = NULL;
    char tbuff[68] = {0};
    if (src != NULL && target != NULL) {
        src_fp = fopen(src, "r");
        if (src_fp != NULL) {
            target_fp = fopen(target, "w");
            if (target_fp != NULL) {
                while(fgets(tbuff, sizeof(tbuff), src_fp) != NULL) {
                    fputs(tbuff,target_fp);
                }
		ret = 0;
		fclose(target_fp);
            }else {
                SWLOG_ERROR("Target File open failed %s\n",target);
            }
            fclose(src_fp);
        }else {
            SWLOG_ERROR("Source File open failed %s\n",src);
	}
    }else {
	SWLOG_ERROR("Received Function parameter NULL\n");
    }
    return ret;
}
/* function prevCurUpdateInfo - makes the communication with the XCONF server
        Usage: int prevCurUpdateInfo
        RETURN - 0 on success, non-zero otherwise
*/
int prevCurUpdateInfo(void)
{
    char myFWVersion[64];
    char cdlFlashedFileName[64];
    char prevCdlFlashedFileName[64];
    char currentImage[80];
    int ret = -1;
    FILE *fp;

    GetFirmwareVersion( myFWVersion, sizeof(myFWVersion));
    if ((filePresentCheck(CDL_FLASHED_IMAGE)) == 0) {
	lastDwnlImg(cdlFlashedFileName, sizeof(cdlFlashedFileName));
	if (!(strstr(cdlFlashedFileName, myFWVersion))) {
	    SWLOG_INFO("Looks like previous upgrade failed but flashed image status is showing success\n");
	    if ((filePresentCheck(PREVIOUS_FLASHED_IMAGE)) == 0) {
	        prevFlashedFile(prevCdlFlashedFileName, sizeof(prevCdlFlashedFileName));
		if (strstr(prevCdlFlashedFileName, myFWVersion)) {
		    SWLOG_INFO("Updating /tmp/currently_running_image_name with previous successful flashed imagename\n");
		    copyFile(PREVIOUS_FLASHED_IMAGE, CURRENTLY_RUNNING_IMAGE);
		}
	    } else {
	        SWLOG_INFO("Previous flashed file name not found !!! \n");
		SWLOG_INFO("Updating currently_running_image_name with cdl_flashed_file_name ... \n");
	        copyFile(CDL_FLASHED_IMAGE, CURRENTLY_RUNNING_IMAGE);
	    }
	} else {
	    //Save succesfully flashed file name to identify the previous flashed image for next upgrades
	    copyFile(CDL_FLASHED_IMAGE, PREVIOUS_FLASHED_IMAGE);
	    copyFile(CDL_FLASHED_IMAGE, CURRENTLY_RUNNING_IMAGE);
	}
    }else {
        SWLOG_INFO("cdl_flashed_file_name file not found !!!\n");
	snprintf(currentImage, sizeof(currentImage), "%s-signed.bin\n", myFWVersion);
	SWLOG_INFO("Updating currently_running_image_name:%s:with version.txt ...\n", currentImage);
	fp = fopen(PREVIOUS_FLASHED_IMAGE, "w");
	if (fp != NULL) {
	    fputs(currentImage, fp);
	    fclose(fp);
	}
	fp = fopen(CURRENTLY_RUNNING_IMAGE, "w");
	if (fp != NULL) {
	    fputs(currentImage, fp);
	    fclose(fp);
	}
    }
    ret = 0;
    return ret;
}
/* function initialValidation - 
 
   Usage: Use validation like either device is in AutoExcluded list.
          Checking either any other instances of same process is running or not.
	  Stroing process id inside /tmp/DIFD.pid file
Return: int: 0 : Success and 1: Failure and 2: Alreday Download is in progress
   */
 
int initialValidation(void)
{
    int status = INITIAL_VALIDATION_FAIL;
    bool already_running = false;
    char data[RFC_VALUE_BUF_SIZE];
    char buf[64];
    BUILDTYPE eBuildType;
    char curpid[16];
    unsigned int pid;
    int ret = -1;
    FILE *fp = NULL;

    GetBuildType( buf, sizeof(buf), &eBuildType );
    ret = read_RFCProperty("AutoExcluded", RFC_FW_AUTO_EXCLUDE, data, sizeof(data));
    if(ret == -1) {
        SWLOG_ERROR("read_RFCProperty() return failed Status %d\n", ret);
	status = INITIAL_VALIDATION_SUCCESS;
    }else {
        SWLOG_INFO("getRFCSettings() rfc AutoExcluded= %s\n", data);
	if ((0 == (strncmp(data, "true", 4))) && (eBuildType != ePROD)) {
            SWLOG_ERROR("Device excluded from firmware update. Exiting !!\n");
	}else {
            status = INITIAL_VALIDATION_SUCCESS;
	}
    }
    if (status == INITIAL_VALIDATION_SUCCESS) {
        already_running = CurrentRunningInst(DIFDPID);
	if (already_running == true) {
	    SWLOG_INFO("initialValidation(): Alreday one Instance is running\n");
            status = INITIAL_VALIDATION_DWNL_INPROGRESS;
	}else if((filePresentCheck("/tmp/fw_preparing_to_reboot")) == 0) {
            if (0 == (strncmp(device_info.maint_status, "true", 4))) {
                eventManager("MaintenanceMGR", MAINT_FWDOWNLOAD_COMPLETE);
            }
	    unlink("/tmp/fw_preparing_to_reboot");
	    status = INITIAL_VALIDATION_DWNL_COMPLETED;
	}else {
            fp = fopen(DIFDPID, "w");
	    if (fp != NULL) {
                pid = getpid();
		snprintf(curpid, sizeof(curpid),"%d\n", pid);
		SWLOG_INFO("current pid=%s:%u\n", curpid, pid);
                fputs(curpid, fp);
		fclose(fp);
	    }else {
		SWLOG_ERROR("unable to create file:/tmp/DIFD.pid\n");
	    }
	    prevCurUpdateInfo(); //This function is use for validate prevoius image update and based on this
            status = INITIAL_VALIDATION_SUCCESS;
	}
    }
    return status;
}

#ifndef GTEST_ENABLE

int main(int argc, char *argv[]) {
    static XCONFRES response;
    int ret = -1;
    int ret_sig = -1;
    int i;
    int ret_curl_code = 1;
    int server_type = HTTP_XCONF_DIRECT;
    int json_res = -1;
    int http_code;
    struct sigaction rdkv_newaction;
    memset(&rdkv_newaction, '\0', sizeof(struct sigaction));
    int init_validate_status = INITIAL_VALIDATION_FAIL;

    rdkv_newaction.sa_sigaction = handle_signal;
    rdkv_newaction.sa_flags = SA_ONSTACK | SA_SIGINFO;
    log_init();
    ret_sig = sigaction(SIGUSR1, &rdkv_newaction, NULL);
    if (ret_sig == -1) {
        SWLOG_ERROR( "SIGUSR1 handler install fail\n");
    }else {
        SWLOG_INFO( "SIGUSR1 handler install success\n");
    }
    *response.cloudFWFile = 0;
    *response.cloudFWLocation = 0;
    *response.ipv6cloudFWLocation = 0;
    *response.cloudFWVersion = 0;
    *response.cloudDelayDownload = 0;
    *response.cloudProto = 0;
    *response.cloudImmediateRebootFlag = 0;
    *response.peripheralFirmwares = 0;
    *response.dlCertBundle = 0;
    *response.cloudPDRIVersion = 0;
    SWLOG_INFO("Starting c method rdkvfwupgrader\n");
    t2CountNotify("SYST_INFO_C_CDL", 1);
    
    snprintf(disableStatsUpdate, sizeof(disableStatsUpdate), "%s","no");

    ret = initialize();
    if (1 != ret) {
        SWLOG_ERROR( "initialize(): Fail:%d\n", ret);
        log_exit();
        exit(ret_curl_code);
    }
    if(argc < 3) {
        SWLOG_ERROR( "Provide 2 arguments. Less than 2 arguments received\n");
        SWLOG_ERROR("Retry Count (1) argument will not be parsed as we will use hardcoded fallback mechanism added \
                     triggerType=2 # Set the Image Upgrade trigger Type \
                     Usage: rdkvfwupgrader <failure retry count> <Image trigger Type> \
                     failure retry count: This value from DCM settings file, if not  \
                     Image trigger Type : Bootup(1)/scheduled(2)/tr69 or SNMP triggered upgrade(3)/App triggered upgrade(4)/(5) Delayed Download\n");
        if (0 == (strncmp(device_info.maint_status, "true", 4))) {
            eventManager("MaintenanceMGR", MAINT_FWDOWNLOAD_ERROR);
        }
        log_exit();
        exit(ret_curl_code);
    }
    for( i = 0; i < argc; i++ ) {
        SWLOG_INFO("[%d] = %s\n", i, argv[i]);
    }


    trigger_type = atoi(argv[2]);
    if (trigger_type == 1) {
        SWLOG_INFO("Image Upgrade During Bootup ..!\n");
    }else if (trigger_type == 2) {
        SWLOG_INFO("Scheduled Image Upgrade using cron ..!\n");
        t2CountNotify("SYST_INFO_SWUpgrdChck", 1);
    }else if(trigger_type == 3){
        SWLOG_INFO("TR-69/SNMP triggered Image Upgrade ..!\n");
    }else if(trigger_type == 4){
        SWLOG_INFO("App triggered Image Upgrade ..!\n");
    }else if(trigger_type == 5){
        SWLOG_INFO("Delayed Trigger Image Upgrade ..!\n");
    }else if(trigger_type == 6){
        SWLOG_INFO("State Red Image Upgrade ..!\n");
    }else{
        SWLOG_INFO("Invalid trigger type Image Upgrade ..!\n");
        if (0 == (strncmp(device_info.maint_status, "true", 4))) {
	    eventManager("MaintenanceMGR", MAINT_FWDOWNLOAD_ERROR);
	}
	log_exit();
        exit(ret_curl_code);
    }
    init_validate_status = initialValidation();
    SWLOG_INFO("init_validate_status = %d\n", init_validate_status);
    if( init_validate_status == INITIAL_VALIDATION_SUCCESS)
    {
        eventManager(FW_STATE_EVENT, FW_STATE_UNINITIALIZED);
	if( isInStateRed() ) {
          eventManager(RED_STATE_EVENT, RED_RECOVERY_STARTED);
        }
	eventManager(FW_STATE_EVENT, FW_STATE_REQUESTING);
        ret_curl_code = MakeXconfComms( &response, server_type, &http_code );

        SWLOG_INFO("XCONF Download completed with curl code:%d\n", ret_curl_code);
        if( ret_curl_code == 0 && http_code == 200)
        {
            SWLOG_INFO("XCONF Download Success\n");
            json_res = processJsonResponse(&response, cur_img_detail.cur_img_name, device_info.model, device_info.maint_status);
            SWLOG_INFO("processJsonResponse returned %d\n", json_res);
            if (0 == (strncmp(response.cloudProto, "tftp", 4))) {
                proto = 0;
            }
            if ((proto == 1) && (json_res == 0)) {
                ret_curl_code = checkTriggerUpgrade(&response, device_info.model);

                char *msg = printCurlError(ret_curl_code);
                if (msg != NULL) {
                    SWLOG_INFO("curl return code =%d and error message=%s\n", ret_curl_code, msg);
                    t2CountNotify("CurlRet_split", ret_curl_code);
                }
                SWLOG_INFO("rdkvfwupgrader daemon exit curl code: %d\n", ret_curl_code);
            } else if (proto == 0) {    // tftp = 0
               SWLOG_INFO("tftp protocol support not present.\n");
            }
            else {
               SWLOG_INFO("Invalid JSON Response.\n");
            }
	}else {
            SWLOG_INFO("XCONF Download Fail\n");
	}
    }
    if (init_validate_status == INITIAL_VALIDATION_DWNL_INPROGRESS){
        if (!(strncmp(device_info.maint_status, "true", 4))) {
            eventManager("MaintenanceMGR", MAINT_FWDOWNLOAD_INPROGRESS); //Sending status to maintenance manager
        }
    }else if(init_validate_status == INITIAL_VALIDATION_DWNL_COMPLETED) {
	SWLOG_INFO("Software Update is completed by AS/EPG, Exiting from firmware download.\n");
    }else if ((ret_curl_code != 0) || (json_res != 0)) {
        if (!(strncmp(device_info.maint_status, "true", 4))) {
            eventManager("MaintenanceMGR", MAINT_FWDOWNLOAD_ERROR); //Sending status to maintenance manager
        }
	if (trigger_type == 6) {
            unsetStateRed();
	}
    }else {
        if (!(strncmp(device_info.maint_status, "true", 4))) {
            eventManager("MaintenanceMGR", MAINT_FWDOWNLOAD_COMPLETE); //Sending status to maintenance manager
        }
    }

    uninitialize(init_validate_status);
    exit(ret_curl_code);
}

#endif
