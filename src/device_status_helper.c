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

#include "device_status_helper.h"
#include "rdkv_cdl.h"
#include "rdkv_cdl_log_wrapper.h"
#ifndef GTEST_ENABLE
#include "rdk_fwdl_utils.h"
#include "json_parse.h"
#endif
#include "rfcInterface/rfcinterface.h"
#include "download_status_helper.h"
#include "iarmInterface/iarmInterface.h"
#include "json_process.h"
#include "device_api.h"
#ifndef GTEST_ENABLE
#include "common_device_api.h"
#endif

extern char * strcasestr(const char * s1, const char * s2);     // removes compiler warning, I can't find prototype
extern Rfc_t rfc_list;

/* Description: Use to check either alreday any instances for same process
 *              is running or not. If Running return true else return false.
 * @param: file : procees id store file name 
 * return :bool : Success: true and flase : failure 
 * */
bool CurrentRunningInst(const char *file)
{
    bool status = false;
    FILE *fp = NULL;
    char buf[64] = {0};
    char *tmp = NULL;
    char procfile[78];
    char *arg = NULL;
    size_t size = 0;

    if (file != NULL) {
        fp = fopen(file, "r");
	if (fp != NULL) {
            fgets(buf, sizeof(buf), fp); //Reading process id
	    tmp = strchr(buf, '\n');
	    if (tmp != NULL) {
                *tmp = '\0';
            }
	    fclose(fp);
	    fp = NULL;
#ifndef GTEST_ENABLE
	    snprintf(procfile, sizeof(procfile), "/proc/%s/cmdline", buf);
#else
	    snprintf(procfile, sizeof(procfile), "/tmp/cmdline.txt", buf);//This is for Gtest only
#endif
            SWLOG_INFO("procfile=%s\n", procfile);
	    /*Opening cmdline file present inside particular pid folder */
            fp = fopen(procfile, "r");
            if (fp != NULL) {
                //Reading based on delimiter becuase inside /proc/<pid>/cmdline file replace space with NULL
                while(getdelim(&arg, &size, 0,fp) != -1){
                    if (arg != NULL) {
                        SWLOG_INFO("proc entry process name:%s\n",arg);
		        /* Checking process name is same as rdkvfwupgrader or deviceInitiatedFWDnld*/
		        if ((strstr(arg, "rdkvfwupgrader")) || (strstr(arg,"deviceInitiatedFWDnld"))) {
		            SWLOG_INFO("proc entry cmdline and process name matched.\nDevice initiated CDL is in progress..\n");
		            SWLOG_INFO("Exiting without triggering device initiated firmware download.\n");
                            t2CountNotify("SYST_INFO_FWUpgrade_Exit", 1);
		            status = true;
			    break;
		        }
		    }
                }
		/* Free arg because getdelim use malloc for memory allocation */
		if (arg != NULL) {
                    free(arg);
		}
		fclose(fp);
            }else {
                SWLOG_ERROR("CurrentRunningInst():Unable to open the file:%s\n", procfile);
	    }
	}else{
            SWLOG_ERROR("CurrentRunningInst():Unable to open the file:%s\n", file);
	}
    }else {
            SWLOG_ERROR("CurrentRunningInst(): Function parameter is NULL\n");
    }
    return status;
}

/* Description: Waiting for ntp service to start.
 * @param: void
 * return :void */
void waitForNtp(void)
{
    char model[16] = {0};
    int ret = -1;
    ret = getDevicePropertyData("MODEL_NUM", model, sizeof(model));
    if (ret == UTILS_SUCCESS) {
         SWLOG_INFO("model = %s\n", model);
	 //TODO: Need add some exit logic
	 if (0 != (strncmp(model, "RPI", 3))) {
             while (0 != filePresentCheck("/tmp/stt_received")) {
                 SWLOG_INFO("Waiting for STT\n");
	         sleep(2);
             }
             SWLOG_INFO("Received STT flag\n");
	 }
    }else {
         SWLOG_ERROR("%s: getDevicePropertyData() for MODEL fail\n", __FUNCTION__);
    }
}
/* Description: Checking dns nameserver ip is present or not.
 * @param: dns_file_name : pointer to dns config file name
 * return :true = success
 * return :false = failure */
bool isDnsResolve(const char *dns_file_name)
{
    bool dns_status = false;
    bool string_check = false;
    FILE *fp = NULL;
    char tbuff[80] = {0};
    char *tmp;
    if (dns_file_name == NULL) {
        SWLOG_ERROR("isDnsResolve(): parameter is NULL\n");
        return dns_status;
    }
    fp = fopen(dns_file_name , "r");
    if (fp != NULL) {
        while (NULL != (fgets(tbuff, sizeof(tbuff), fp))) {
            if (NULL != (strstr(tbuff, "nameserver"))) {
                string_check = true;
                break;
            }
        }
        if (string_check == true) {
            tmp = strstr(tbuff, "nameserver");
            SWLOG_INFO("dns resolve data=%s\n", tbuff);
            if (tmp != NULL) {
                tmp = tmp + 10;
                if (*tmp != '\0' && *tmp != '\n') {
                    SWLOG_INFO("dns nameserver present.\n");
                    dns_status = true;
                }
            }
        }
	fclose(fp);
    }else {
        SWLOG_INFO("dns resolve file:%s not present\n", dns_file_name);
    }
    return dns_status;
}
/* Description: Checking IP route address and device is online or not.
 *              Use IARM event provided by net service manager to check either
 *              device is online or not.
 * @param: file_name : pointer to gateway iproute config file name
 * return :true = success
 * return :false = failure */
bool CheckIProuteConnectivity(const char *file_name)
{
    bool ip_status = false;
    bool string_check = false;
    FILE *fp = NULL;
    char tbuff[80] = {0};
    char *tmp;
    int IpRouteCnt = 5;
    if (file_name == NULL) {
        SWLOG_ERROR("parameter is NULL\n");
        return ip_status;
    }
    SWLOG_INFO("CheckIPRoute Waiting for Route Config %s file\n", IP_ROUTE_FLAG);
    while(IpRouteCnt--) {
        if (0 == (filePresentCheck(IP_ROUTE_FLAG))) {
            break;
	}
	sleep(15);
    }
    if (IpRouteCnt == 0 && (0 == (filePresentCheck(IP_ROUTE_FLAG)))) {
        SWLOG_INFO("%s: route flag=%s not present\n", __FUNCTION__, IP_ROUTE_FLAG);
	return ip_status;
    }
    SWLOG_INFO("CheckIPRoute Received Route Config file\n");
    fp = fopen(file_name , "r");
    if (fp != NULL) {
        while (NULL != (fgets(tbuff, sizeof(tbuff), fp))) {
            if (NULL != (strstr(tbuff, "IPV"))) {
                string_check = true;
                break;
            }
        }
        if (string_check == true) {
            tmp = strstr(tbuff, "IPV");
            SWLOG_INFO("ip address=%s\n", tbuff);
            if (tmp != NULL && (NULL != strstr(tmp, "IPV4"))) {
                /*tmp = tmp + 4;
                if (*tmp != '\0' && *tmp != '\n') {
                    SWLOG_INFO("default router Link Local IPV4 address present=%s\n", tmp);
                    snprintf(ipaddr, buf_size, "%s", tmp+1);
                    ip_status = true;
                }*/
                SWLOG_INFO("default router Link Local IPV4 address present=%s\n", tmp);
            }else if (tmp != NULL && (NULL != strstr(tmp, "IPV6"))) {
                    SWLOG_INFO("default router Link Local IPV6 address present=%s\n", tmp);
            }else {
                SWLOG_ERROR("IP address type does not found\n");
            }
        }else {
            SWLOG_ERROR("File %s does not have IP address in proper format\n", file_name);
	}
     fclose(fp);
    }else {
        SWLOG_INFO("ip route file:%s not present\n", file_name);
    }
    if ( true == isConnectedToInternet()) {
        SWLOG_INFO("Device is online\n");
	ip_status = true;
    }else{
        SWLOG_INFO("Device is not online\n");
	ip_status = false;
    }
    return ip_status;
}
/* Description: Update optout option to ENFORCE_OPTOUT
 * @param: optout_file_name : pointer to optout config file name
 * return :true = success
 * return :false = failure */
bool updateOPTOUTFile(const char *optout_file_name)
{
    bool opt_status = false;
    bool enforce_optout_set = false;
    FILE *fp = NULL;
    FILE *fp_write = NULL;
    char tbuff[80] = {0};
    char *update_data = "softwareoptout=ENFORCE_OPTOUT\n";
    int ret = -1;
    if (optout_file_name == NULL) {
        SWLOG_ERROR("%s: parameter is NULL\n", __FUNCTION__);
        return opt_status;
    }
    fp_write = fopen(MAINTENANCE_MGR_RECORD_UPDATE_FILE, "w");
    if (fp_write == NULL) {
        SWLOG_ERROR("%s: Unable to create file:%s\n", __FUNCTION__, MAINTENANCE_MGR_RECORD_UPDATE_FILE);
        return opt_status;
    }
    fp = fopen(optout_file_name , "r");
    if (fp != NULL) {
        while (NULL != (fgets(tbuff, sizeof(tbuff), fp))) {
            if ((NULL != (strstr(tbuff, "softwareoptout"))) && (NULL != (strstr(tbuff, "BYPASS_OPTOUT")))) {
                SWLOG_INFO("optout set to:%s\n", update_data);
                fputs(update_data, fp_write);
                enforce_optout_set = true;
            }else {
                fputs(tbuff, fp_write);
            }
        }
        fclose(fp);
        fclose(fp_write);
        if (enforce_optout_set == true) {
            /*rename updated file to orginal optout config file*/
            ret = rename(MAINTENANCE_MGR_RECORD_UPDATE_FILE, optout_file_name);
            if (ret == 0) {
                SWLOG_INFO("rename optout file to %s\n", optout_file_name);
		opt_status = true;
            }else {
                SWLOG_ERROR("fail to rename optout file to %s: error=%d\n", optout_file_name, ret);
            }
        }
    }else {
        SWLOG_ERROR("optout file:%s not present\n", optout_file_name);
        fclose(fp_write);
    }
    unlink(MAINTENANCE_MGR_RECORD_UPDATE_FILE);
    return opt_status;
}

/* Description: Checking either device is having codebig access or not
 * return :true = access present
 * return :false = access not present */
bool checkCodebigAccess(void)
{
    int ret = -1;
    bool codebigEnable = false;
    ret = v_secure_system("GetServiceUrl 2 temp");
    SWLOG_INFO("Exit code for codebigcheck:%d\n", ret);
    if (ret == 0) {
        SWLOG_INFO("CodebigAccess Present:%d\n", ret);
	codebigEnable = true;
    }else {
        SWLOG_INFO("CodebigAccess Not Present:%d\n", ret);
    }
    return codebigEnable;
}
/* Description: Checking state red support is present or not
 * return :1 = if state red support is present
 * return :0 = if state red support is not present */
int isStateRedSupported(void) {
    int ret = -1;
    ret = filePresentCheck(STATE_RED_SPRT_FILE);
    if(ret == 0) {
        SWLOG_INFO("isStateRedSupported(): Yes file present:%s\n", STATE_RED_SPRT_FILE);
        return 1;
    }
    SWLOG_INFO("isStateRedSupported(): No:%s\n", STATE_RED_SPRT_FILE);
    return 0;
}

/* Description: Checking either device is in state red or not.
 * return 1: In state red
 *        0: Not in state red
 * */
int isInStateRed(void) {
    int ret = -1;
    int stateRed = 0;
    ret = isStateRedSupported();
    if(ret == 0) {
        SWLOG_INFO("isInStateRed(): No ret:%d\n", stateRed);
        return stateRed;
    }
    ret = filePresentCheck(STATEREDFLAG);
    if(ret == 0) {
        SWLOG_INFO("isInStateRed(): Yes Flag prsent:%s. Device is in statered\n", STATEREDFLAG);
        stateRed = 1;
    } else {
        SWLOG_INFO("isInStateRed(): No Flag Not prsent:%s. Device is not in statered\n", STATEREDFLAG);
    }
    return stateRed;
}

void unsetStateRed(void)
{
    int ret = -1;
    ret = filePresentCheck(STATEREDFLAG);
    if (ret == 0) {
        SWLOG_INFO("RED:unsetStateRed: Exiting State Red\n");
	unlink(STATEREDFLAG);
    } else {
        SWLOG_INFO("RED:unsetStateRed: Not in State Red\n");
    }
}


/* Description: If state red support is present eneter to state red
 * @param curlret: Receving curl status from Caller
 * */
void checkAndEnterStateRed(int curlret, const char *disableStatsUpdate) {
    int ret = -1;
    FILE *fp = NULL;
    struct FWDownloadStatus fwdls;
    ret = isStateRedSupported();
    if(ret == 0) {
        return;
    }
    ret = isInStateRed();
    if(ret == 1) {
        SWLOG_INFO("RED checkAndEnterStateRed: device state red recovery flag already set\n");
        t2CountNotify("SYST_INFO_RedstateSet", 1);
        return;
    }
    if((curlret == 35) || (curlret == 51) || (curlret == 53) || (curlret == 54) || (curlret == 58) || (curlret == 59) || (curlret == 60)
            || (curlret == 64) || (curlret == 66) || (curlret == 77) || (curlret == 80) || (curlret == 82) || (curlret == 83) || (curlret == 90)
            || (curlret == 91)|| (curlret == 495)) {
        SWLOG_INFO("RED checkAndEnterStateRed: Curl SSL/TLS error %d. Set State Red Recovery Flag and Exit!!!", curlret);
        t2CountNotify("CDLrdkportal_split", curlret);
        //CID:280507-Unchecked return value
	if(remove(DIRECT_BLOCK_FILENAME) != 0){
		perror("Error deleting DIRECT_BLOCK_FAILURE");
	}
        if(remove(CB_BLOCK_FILENAME) != 0){
		perror("Error deleting CB_BLOCK_FILENAME");
	}
        if(remove(HTTP_CDL_FLAG) != 0){
		perror("Error deleting HTTP_CDL_FLAG");
	}
        //updateFWDownloadStatus "" "Failure" "" "TLS/SSL Error" "" "" "$runtime" "Failed" "$DelayDownloadXconf"
        
        snprintf(fwdls.method, sizeof(fwdls.method), "Method|xconf\n");
        snprintf(fwdls.proto, sizeof(fwdls.proto), "Proto|\n");
        snprintf(fwdls.status, sizeof(fwdls.status), "Status|Failure\n");
        snprintf(fwdls.reboot, sizeof(fwdls.reboot), "Reboot|\n");
        snprintf(fwdls.failureReason, sizeof(fwdls.failureReason), "FailureReason|TLS/SSL Error\n");
        snprintf(fwdls.dnldVersn, sizeof(fwdls.dnldVersn), "DnldVersn|\n");
        snprintf(fwdls.dnldfile, sizeof(fwdls.dnldfile), "DnldFile|\n");
        snprintf(fwdls.dnldurl, sizeof(fwdls.dnldurl), "DnldURL|\n");
        fwdls.lastrun[0] = 0;
        //TODO sprintf(fwdls.lastrun, "LastRun|%s\n", lastrun); // This data should come from script as a argument
        snprintf(fwdls.FwUpdateState, sizeof(fwdls.FwUpdateState), "FwUpdateState|Failed\n");
        fwdls.DelayDownload[0] = 0;
        //TODO sprintf(fwdls.DelayDownload, "DelayDownload|%s\n", delaydnld); // This data should come from script as a argument
        updateFWDownloadStatus(&fwdls, disableStatsUpdate);

        uninitialize(INITIAL_VALIDATION_SUCCESS);
        fp = fopen(STATEREDFLAG, "w");
        if(fp != NULL) {
            fclose(fp);
        }
        exit(1);
    } else {
        //Recovery completed event send for the failure case but not due to fatal error
        if( (filePresentCheck( RED_STATE_REBOOT ) == RDK_API_SUCCESS) ) {
             SWLOG_INFO("%s : RED Recovery completed\n", __FUNCTION__);
             eventManager(RED_STATE_EVENT, RED_RECOVERY_COMPLETED);
             unlink(RED_STATE_REBOOT);
        }
    }
}


/* Description: checkVideoStatus(): This Function is used for to identify either video is playing or not in TV
 * @param device_name : send your device name
 * return : 1 on success
 *      -1 on failure
 * */
int checkVideoStatus(const char *device_name) {
    int ret = -1;
    FILE *fp = NULL;
    char file_name[64] = { 0 };
    char str_grep[16] = { 0 };
    char tbuff[80] = { 0 };
    char *tmp = NULL;

    if(device_name == NULL) {
        SWLOG_ERROR("checkVideoStatus() parameter is NULL\n");
        return ret;
    }
    //TODO: Below logic is for workaround solution. We are going bring API to know whether video is playing or not.
    // In that API, implementation shall be made based on soc, instead of model name.
    if((0 == (strncmp(device_name, "LLAMA", 5))) || ((0 == (strncmp(device_name, "PLATCO", 6))))) {
        strncpy(file_name, "/sys/class/vdec/vdec_status", sizeof(file_name) - 1);
	file_name[sizeof(file_name) - 1] = '\0';
        strncpy(str_grep, "frame width", sizeof(str_grep) - 1);
	str_grep[sizeof(str_grep) - 1] = '\0';
    }else {
        strncpy(file_name, "/proc/brcm/video_decoder", sizeof(file_name) - 1);
	file_name[sizeof(file_name) - 1] = '\0';
        strncpy(str_grep, "pts", sizeof(str_grep) - 1);
	str_grep[sizeof(str_grep) - 1] = '\0';
    }
    SWLOG_INFO("checkVideoStatus() device name=%s and checking file=%s\n", device_name, file_name);
    fp = fopen(file_name, "r");
    if(fp == NULL) {
        SWLOG_ERROR("checkVideoStatus() unable to open file=%s\n", file_name);
        return ret;
    }
    while((fgets(tbuff, sizeof(tbuff) - 1, fp)) != NULL) {
        tmp = strstr(tbuff, str_grep);
        if(tmp != NULL) {
            SWLOG_INFO("checkVideoStatus() video frame data=%s\n", tmp);
            ret = 1;
            break;
        }else {
            continue;
        }
    }
    fclose(fp);
    return ret;
}


/* Description: Check for Throttle is enable or not.
 * @param : device_name: Send the name of the device
 * @param : reboot_immediate_flag: Send the Cloud Reboot Immediate Flag
 * @param : app_mode: Send current App Mode of the device
 * @return: int 1: enable and -1: disable
 * */
int isThrottleEnabled(const char *device_name, const char *reboot_immediate_flag, int app_mode) {
    int ret = -1;
    int video_ply_status = -1;
    if ((device_name == NULL) || (reboot_immediate_flag == NULL)) {
        SWLOG_ERROR("%s: Parameter is NULL\n", __FUNCTION__);
        return ret;
    }
    SWLOG_INFO("isThrottleEnabled(): DeviceType=%s RebootImmediateFlag=%s\n", device_name, reboot_immediate_flag);
    if((strcmp(rfc_list.rfc_throttle, "true")) == 0) {
        video_ply_status = checkVideoStatus(device_name);
        if(video_ply_status == 1 || app_mode == APP_BACKGROUND) {
            if(strncmp(reboot_immediate_flag, "false", 5) == 0) {
                ret = 1;
                SWLOG_INFO("Video is Streaming. Hence, Continuing with the Throttle Mode. Video Play Status=%d\n", video_ply_status);
            }else {
                SWLOG_INFO("Video is Streaming, but cloudImmediateRebootFlag is %s. Continuing with Unthrottle Mode. Video Play Status=%d\n", reboot_immediate_flag, video_ply_status);
            }
        }else {
            SWLOG_INFO("Video is not playing. Throttle mode is not enable\n");
        }
    }else {
        SWLOG_INFO("Throttle enable rfc is %s\n", rfc_list.rfc_throttle);
    }
    return ret;
}

/* Description: Checking OCSP enable or disable.
 * @param : void
 * @return: int 1: enable and -1: disable
 * */
int isOCSPEnable(void)
{
    int flag_ocspstapling = 0;
    int flag_ocsp = 0;
    int ret = -1;
    ret = filePresentCheck(EnableOCSPStapling);
    if(ret == 0) {
        flag_ocspstapling = 1;
    }
    ret = filePresentCheck(EnableOCSP);
    if(ret == 0) {
        flag_ocsp = 1;
    }
    SWLOG_INFO("isOCSPEnable() : ocsp status=%d\n", (flag_ocspstapling && flag_ocsp));
    return (flag_ocspstapling && flag_ocsp);
}

/* Description: Checking earlier any upgrade in progress.
 * @param : void
 * @return: bool false : no upgrade in progress
 *               true : Alreday upgrader in progress
 * */
bool isUpgradeInProgress(void)
{
    bool status = false;
    if ((RDK_API_SUCCESS == filePresentCheck(HTTP_CDL_FLAG)) || (RDK_API_SUCCESS == filePresentCheck(SNMP_CDL_FLAG)) || (RDK_API_SUCCESS == filePresentCheck(ECM_CDL_FLAG))) {
        status = true;
    }
    return status;
}

/*int isCodeBigFallBackEnabled(void)
{
    return 0;// Logic need to insert. For now return disable for testing
}*/


/* Description: Use for to get last modified time of the file
 * @param : file_name: File name
 * @return: int Success: return time and -1: fail
 * */
unsigned int getFileLastModifyTime(char *file_name)
{
    struct stat attr;
    int ret = 0;
    if (file_name == NULL) {
        SWLOG_ERROR("%s : Parameter is NULL\n", __FUNCTION__);
        return ret;
    }
    memset(&attr, '\0', sizeof(attr));
    ret = stat(file_name, &attr);
    if (ret != 0) {
        SWLOG_ERROR("%s: File: %s not present: %d\n", __FUNCTION__, file_name, ret);
        return 0;
    }
    SWLOG_INFO("%s : Last mod time: %lu\n", __FUNCTION__, attr.st_mtime);
    return attr.st_mtime;
}

/* Description: Use for to get current system time
 * @param : void:
 * @return: int Success: return time and 0: fail
 * */
time_t getCurrentSysTimeSec(void)
{
    time_t curtime = time(0);
    if (curtime == ((time_t) -1)) {
        SWLOG_INFO("%s : time return error\n", __FUNCTION__);
        return 0;
    } else {
        SWLOG_INFO("%s : current system time=%lu\n", __FUNCTION__, curtime);
    }
    return curtime;
}

/* Description: Check either any download is block or nor
 * @param : type: request type to check which download is block
 * @return: int Success: 1 and -1: fail
 * */
int isDwnlBlock(int type)
{
    int ret = -1;
    int block_time = 0;
    char file_name[128] = {0};
    unsigned int current_time = 0;
    unsigned int last_mod_time = 0;
    unsigned int modification_time = 0;
    int remtime = 0;
    int block = 0;

    if (type == HTTP_SSR_DIRECT || type == HTTP_XCONF_DIRECT) {
        snprintf(file_name, sizeof(file_name), "%s", DIRECT_BLOCK_FILENAME);
        block_time = 86400;
    } else if (type == HTTP_SSR_CODEBIG || type == HTTP_XCONF_CODEBIG) {
        snprintf(file_name, sizeof(file_name), "%s", CB_BLOCK_FILENAME);
        block_time = 1800;
    } else {
        return ret;
    }
    char *req_type = ((type == HTTP_SSR_DIRECT || type == HTTP_XCONF_DIRECT) ? "direct" : "codebig");
    SWLOG_INFO(" %s : Checking for %s\n", __FUNCTION__, req_type);
    last_mod_time = getFileLastModifyTime(file_name);
    if (last_mod_time != 0) {
        current_time = getCurrentSysTimeSec();
        modification_time = current_time - last_mod_time;
        SWLOG_INFO("%s modtime=%u\n",req_type, modification_time);
        remtime = (block_time/60) - (modification_time/60);
        SWLOG_INFO("%s remtime=%u\n",req_type, remtime);
        if (modification_time <= block_time) {
            SWLOG_INFO("ImageUpgrade: Last %s failed blocking is still valid for %d mins, preventing direct\n", req_type, remtime);
            block = 1;
        } else {
            SWLOG_INFO("ImageUpgrade: Last %s failed blocking has expired, removing %s, allowing direct\n", req_type, file_name);
            unlink(file_name);
        }
    }
    return block;
}

/* Description: Checking delay based download is enable or not.
 * @param DelayDownloadXconf: Time in minutes for delay
 * @param maint: maintainance manager support check
 * @param trigger_type: Type of software update trigger. TODO:Use for future
 * */
bool isDelayFWDownloadActive(int DelayDownloadXconf, const char *maint, int trigger_type)
{
    int delay_sec;

    delay_sec = DelayDownloadXconf * 60;
    SWLOG_INFO("%s: Device configured with download delay of %d minutes.\n", __FUNCTION__, DelayDownloadXconf);
    if(delay_sec > 0 && trigger_type != 5) {
        if ((maint != NULL) && (!strncmp(maint, "true", 4))) {
            SWLOG_INFO("%s: Sending event to Maintenance Plugin with Error before exit\n", __FUNCTION__);
	    //eventManager "MaintenanceMGR" $MAINT_FWDOWNLOAD_ERROR
	    eventManager("MaintenanceMGR" ,MAINT_FWDOWNLOAD_ERROR);
	}
	sleep(delay_sec);
    }
    return true;
}

/* Description: Checking either pdri upgrade required or not.
 * @param dwnl_pdri_img: Requested pdri image recived from xconf
 * @return bool: true yes otherwise no
 * */
bool isPDRIEnable(void)
{
    bool status = false;
    char *dev_prop_name = "PDRI_ENABLED";
    int ret = -1;
    char pdri_status[64] = {0};
    ret = getDevicePropertyData(dev_prop_name, pdri_status, sizeof(pdri_status));
    if (ret == UTILS_SUCCESS) {
        SWLOG_INFO("%s: pdri status from device.property file=%s\n", __FUNCTION__, pdri_status);
	status = true;
    } else {
        SWLOG_INFO("%s: P-DRI Upgrade Unsupported !!\n", __FUNCTION__);
    }
    return status;
}

bool GetPDRIVersion( char *pPdriVersion, size_t szBufSize )
{
    char *tmp = NULL;
    bool bRet = false;

    if( pPdriVersion != NULL )
    {
        *pPdriVersion = 0;
        if( isPDRIEnable() == true )
        {
            GetPDRIFileName( pPdriVersion, szBufSize );
            
            SWLOG_INFO( "current pdri image = %s\n", pPdriVersion );
            tmp = strstr( pPdriVersion, ".bin" );
            if( tmp != NULL )
            {
                *tmp = 0;
                SWLOG_INFO( "After Removing .bin = %s\n", pPdriVersion );
            }
            bRet = true;        // looks like a valid version
        }
    }
    return bRet;
}


// TODO: Convert to array of function pointer calls to reduce size of this function
size_t createJsonString( char *pPostFieldOut, size_t szPostFieldOut )
{
    char *pTmpPost = pPostFieldOut;     // keep original pointer in case needed
    size_t len, totlen = 0, remainlen;
    int ret = -1;
    char tmpbuf[400];
    char cpuarch[16];
    char devicename[32];
   
    cpuarch[0] = 0;
    devicename[0] = 0;
    ret = getDevicePropertyData("CPU_ARCH", cpuarch, sizeof(cpuarch));
    if (ret == UTILS_SUCCESS) {
         SWLOG_INFO("cpu_arch = %s\n", cpuarch);
    } else {
         SWLOG_ERROR("%s: getDevicePropertyData() for cpu arch fail\n", __FUNCTION__);
    }
    ret = getDevicePropertyData("DEVICE_NAME", devicename, sizeof(devicename));
    if (ret == UTILS_SUCCESS) {
         SWLOG_INFO("DEVICE_NAME = %s\n", devicename);
    } else {
         SWLOG_ERROR("%s: getDevicePropertyData() device name fail\n", __FUNCTION__);
    }

    len = GetEstbMac( tmpbuf, sizeof(tmpbuf) );
    if( len )
    {
        remainlen = szPostFieldOut - totlen;
        totlen += snprintf( (pTmpPost + totlen), remainlen, "eStbMac=%s", tmpbuf );
    }
    len = GetFirmwareVersion( tmpbuf, sizeof(tmpbuf) );
    if( len )
    {
        if( totlen )
        {
            *(pTmpPost + totlen) = '&';
            ++totlen;
        }
        remainlen = szPostFieldOut - totlen;
        totlen += snprintf( pTmpPost + totlen, remainlen, "firmwareVersion=%s", tmpbuf );
    }
    len = GetAdditionalFwVerInfo( tmpbuf, sizeof(tmpbuf) );
    if( len )
    {
        if( totlen )
        {
            *(pTmpPost + totlen) = '&';
            ++totlen;
        }
        remainlen = szPostFieldOut - totlen;
        totlen += snprintf( (pTmpPost + totlen), remainlen, "additionalFwVerInfo=%s", tmpbuf );
    }
    len = GetBuildType( tmpbuf, sizeof(tmpbuf), NULL );
    if( len )
    {
        if( totlen )
        {
            *(pTmpPost + totlen) = '&';
            ++totlen;
        }
        remainlen = szPostFieldOut - totlen;
        totlen += snprintf( (pTmpPost + totlen), remainlen, "env=%s", tmpbuf );
    }
    SWLOG_INFO("Calling GetModelNum function\n");
    len = GetModelNum( tmpbuf, sizeof(tmpbuf) );
    if( len )
    {
        if( totlen )
        {
            *(pTmpPost + totlen) = '&';
            ++totlen;
        }
        remainlen = szPostFieldOut - totlen;
        totlen += snprintf( (pTmpPost + totlen), remainlen, "model=%s", tmpbuf );
    }
    len = GetMFRName( tmpbuf, sizeof(tmpbuf) ); 
    if( len )
    {
        if( totlen )
        {
            *(pTmpPost + totlen) = '&';
            ++totlen;
        }
        remainlen = szPostFieldOut - totlen;
        totlen += snprintf( (pTmpPost + totlen), remainlen, "manufacturer=%s", tmpbuf );
    }
    len = GetPartnerId( tmpbuf, sizeof(tmpbuf) );
    if( len )
    {
        if( totlen )
        {
            *(pTmpPost + totlen) = '&';
            ++totlen;
        }
        remainlen = szPostFieldOut - totlen;
        totlen += snprintf( (pTmpPost + totlen), remainlen, "partnerId=%s", tmpbuf );
        remainlen = szPostFieldOut - totlen;
        totlen += snprintf( (pTmpPost + totlen), remainlen, "&activationInProgress=false" );
    }
    else    // there's no partner ID (kind of impossible since there's a default
    {
        if( totlen )
        {
            *(pTmpPost + totlen) = '&';
            ++totlen;
        }
        remainlen = szPostFieldOut - totlen;
        totlen += snprintf( (pTmpPost + totlen), remainlen, "activationInProgress=true" );
    }
    len = GetOsClass( tmpbuf, sizeof(tmpbuf) );
    if( len )
    {
        if( totlen )
        {
            *(pTmpPost + totlen) = '&';
            ++totlen;
        }
        remainlen = szPostFieldOut - totlen;
        totlen += snprintf( (pTmpPost + totlen), remainlen, "osClass=%s", tmpbuf );
    }
    len = GetAccountID( tmpbuf, sizeof(tmpbuf) );
    if( len )
    {
        if( totlen )
        {
            *(pTmpPost + totlen) = '&';
            ++totlen;
        }
        remainlen = szPostFieldOut - totlen;
        totlen += snprintf( (pTmpPost + totlen), remainlen, "accountId=%s", tmpbuf );
    }
    len = GetExperience( tmpbuf, sizeof(tmpbuf) );
    if( len )
    {
        if( totlen )
        {
            *(pTmpPost + totlen) = '&';
            ++totlen;
        }
        remainlen = szPostFieldOut - totlen;
        totlen += snprintf( (pTmpPost + totlen), remainlen, "experience=%s", tmpbuf );
    }
    len = GetMigrationReady( tmpbuf, sizeof(tmpbuf) );
     if( len )
     {
         if( totlen )
         {
             *(pTmpPost + totlen) = '&';
             ++totlen;
         }
         remainlen = szPostFieldOut - totlen;
         totlen += snprintf( (pTmpPost + totlen), remainlen, "migrationReady=%s", tmpbuf );
     }
    len = GetSerialNum( tmpbuf, sizeof(tmpbuf) );
    if( len )
    {
        if( totlen )
        {
            *(pTmpPost + totlen) = '&';
            ++totlen;
        }
        remainlen = szPostFieldOut - totlen;
        totlen += snprintf( (pTmpPost + totlen), remainlen, "serial=%s", tmpbuf );
    }
    len = GetUTCTime( tmpbuf, sizeof(tmpbuf) );
    if( len )
    {
        if( totlen )
        {
            *(pTmpPost + totlen) = '&';
            ++totlen;
        }
        remainlen = szPostFieldOut - totlen;
        totlen += snprintf( (pTmpPost + totlen), remainlen, "localtime=%s", tmpbuf );
    }
    len = GetInstalledBundles( tmpbuf, sizeof(tmpbuf) );
    if( totlen )
    {
        *(pTmpPost + totlen) = '&';
        ++totlen;
    }
    remainlen = szPostFieldOut - totlen;
    totlen += snprintf( (pTmpPost + totlen), remainlen, "dlCertBundle=%s", tmpbuf );
    len = GetRdmManifestVersion( tmpbuf, sizeof(tmpbuf) );
    if( totlen )
    {
        *(pTmpPost + totlen) = '&';
        ++totlen;
    }
    remainlen = szPostFieldOut - totlen;
    totlen += snprintf( (pTmpPost + totlen), remainlen, "rdmCatalogueVersion=%s", tmpbuf );
    //TODO: WAREHOUSE_ENV="$RAMDISK_PATH/warehouse_mode_active" this is not present then call
    len = GetTimezone( tmpbuf, cpuarch, sizeof(tmpbuf) );
    if( len )
    {
        if( totlen )
        {
            *(pTmpPost + totlen) = '&';
            ++totlen;
        }
        remainlen = szPostFieldOut - totlen;
        totlen += snprintf( (pTmpPost + totlen), remainlen, "timezone=%s", tmpbuf );
    }
    waitForNtp(); // Waiting for ntp server to start
    len = GetCapabilities( tmpbuf, sizeof(tmpbuf) );
    if( len )
    {
        if( totlen )
        {
            *(pTmpPost + totlen) = '&';
            ++totlen;
        }
        remainlen = szPostFieldOut - totlen;
        totlen += snprintf( (pTmpPost + totlen), remainlen, "capabilities=%s", tmpbuf );
    }
    SWLOG_INFO( "createJsonString: totlen = %d\n%s\n", totlen, pPostFieldOut );
    return totlen;
}

/* Description: Checking either pdri upgrade required or not.
 * @param dwnl_pdri_img: Requested pdri image recived from xconf
 * @return bool: true yes otherwise no
 * */
bool checkPDRIUpgrade(const char *dwnl_pdri_img)
{
    bool status = false;
    char cur_pdri_version[64];

    if (dwnl_pdri_img == NULL)
    {
        SWLOG_ERROR("%s: parameter is NULL\n", __FUNCTION__);
    }
    else
    {
        status = GetPDRIVersion( cur_pdri_version, sizeof(cur_pdri_version) );
        if( status == true )
        {
            SWLOG_INFO("current pdri image = %s and requested image = %s\n",cur_pdri_version, dwnl_pdri_img);
            if (strcasestr(dwnl_pdri_img, cur_pdri_version))
            {
                SWLOG_INFO("current pdri image = %s and requested dwnl pdri image = %s\n", cur_pdri_version, dwnl_pdri_img);
                SWLOG_INFO("PDRI version of the active image and the image to be upgraded are the same. No upgrade required.\n");
                status = false;
            }
            else
            {
                status = true;
            }
        }
        else
        {
            SWLOG_INFO("PDRI not supported\n");
        }
    }
    return status;
}

bool lastDwnlImg(char *img_name, size_t img_name_size)
{
    static char last_dwnl_img_name[64] = {0};
    bool status = false;
    char tbuff[64];
    int index = 0;
    FILE *fp = NULL;
    tbuff[0] = 0;

    if (img_name_size > sizeof(last_dwnl_img_name)) {
        SWLOG_INFO("%s: Input buffer size is greater than %d\n", __FUNCTION__, sizeof(last_dwnl_img_name));
        return status;
    }
    if (last_dwnl_img_name[0] == 0) {
    fp = fopen("/opt/cdl_flashed_file_name", "r");
    if (fp != NULL) {
        fgets(tbuff, sizeof(tbuff), fp);
        SWLOG_INFO("lastDnldFile tbuff: %s\n", tbuff);
        index = strcspn(tbuff, "\n");
        if (index > 0 && (index < sizeof(tbuff))) {
            tbuff[index] = '\0';
        }
        snprintf(last_dwnl_img_name, sizeof(last_dwnl_img_name), "%s", tbuff);
        snprintf(img_name, img_name_size, "%s", last_dwnl_img_name);
        SWLOG_INFO("lastDnldFile: %s\n", last_dwnl_img_name);
        SWLOG_INFO("Image name return to caller function: %s\n", img_name);
        fclose(fp);
        status = true;
    }
    } else {
        snprintf(img_name, img_name_size, "%s", last_dwnl_img_name);
        SWLOG_INFO("Optimize lastDnldFile: %s\n", img_name);
        status = true;
    }
    return status;
}

bool currentImg(char *img_name, size_t img_name_size)
{
    static char cur_img_name[64] = {0};
    bool status = false;
    char tbuff[64];
    int index = 0;
    FILE *fp = NULL;
    tbuff[0] = 0;

    if (img_name_size > sizeof(cur_img_name)) {
        SWLOG_INFO("%s: Input buffer size is greater than %d\n", __FUNCTION__, sizeof(cur_img_name));
        return status;
    }
    if (cur_img_name[0] == 0) {
        fp = fopen("/tmp/currently_running_image_name", "r");
        if (fp != NULL) {
            fgets(tbuff, sizeof(tbuff), fp);
            SWLOG_INFO("currentImg tbuff: %s\n", tbuff);
            index = strcspn(tbuff, "\n");
            if (index > 0 && (index < sizeof(tbuff))) {
                tbuff[index] = '\0';
            }
            snprintf(cur_img_name, sizeof(cur_img_name), "%s", tbuff);
            snprintf(img_name, img_name_size, "%s", cur_img_name);
            SWLOG_INFO("currentImg: %s\n", cur_img_name);
            SWLOG_INFO("currentImg: %s\n", img_name);
            fclose(fp);
            status = true;
        }
    } else {
        snprintf(img_name, img_name_size, "%s", cur_img_name);
        SWLOG_INFO("Optimize currentImg: %s\n", img_name);
        status = true;
    }
    return status;
}

bool prevFlashedFile(char *img_name, size_t img_name_size)
{
    static char prev_img_name[64] = {0};
    bool status = false;
    char tbuff[64];
    int index = 0;
    tbuff[0] = 0;

    if (img_name_size > sizeof(prev_img_name)) {
        SWLOG_INFO("%s: Input buffer size is greater than %d\n", __FUNCTION__, sizeof(prev_img_name));
        return status;
    }
    if (prev_img_name[0] == 0) {
        FILE *fp;
        fp = fopen(PREVIOUS_FLASHED_IMAGE, "r");
        if (fp != NULL) {
            fgets(tbuff, sizeof(tbuff), fp);
            SWLOG_INFO("prevImg tbuff: %s\n", tbuff);
            index = strcspn(tbuff, "\n");
            if (index > 0 && (index < sizeof(tbuff))) {
                tbuff[index] = '\0';
            }
            snprintf(prev_img_name, sizeof(prev_img_name), "%s", tbuff);
            snprintf(img_name, img_name_size, "%s", prev_img_name);
            SWLOG_INFO("prevImg: %s\n", img_name);
            fclose(fp);
            status = true;
        }
    } else {
        snprintf(img_name, img_name_size, "%s", prev_img_name);
        SWLOG_INFO("Optimize prevImg: %s\n", img_name);
        status = true;
    }
    return status;
}

/* Description: Use For check either pci upgrade is required or not
 * @param: trigger_type : which type of download trigger value from 0 to 6
 * @param: myfwversion : running firmware version on the dveice
 * @param: cloudFWVersion : Requested firmware version
 * @param: cloudFWFile : Requested Firmware File
 * @return bool: true : go for download and false download not required
 * */
bool checkForValidPCIUpgrade(int trigger_type, const char *myfwversion, const char *cloudFWVersion, const char *cloudFWFile)
{
    bool pci_valid_status = false;
    bool last_dwnl_status = false;
    bool current_img_status = false;
    char last_dwnl_img[64] = {0};
    char current_img[64] = {0};
    struct FWDownloadStatus fwdls;

    if (myfwversion == NULL || cloudFWVersion == NULL || cloudFWFile == NULL) {
        SWLOG_INFO("%s: Parameter is NULL\n", __FUNCTION__);
        return false;
    }
    SWLOG_INFO("Xconf image/PDRI configuration Check\n");
    if ((strstr(cloudFWFile, "_PDRI_")) != NULL) {
        SWLOG_INFO("PDRI image is wrongly configured as Cloud Firmware Value\n");
        return pci_valid_status;
    }
    SWLOG_INFO("Trigger Type=%d\n", trigger_type);
    last_dwnl_status = lastDwnlImg(last_dwnl_img, sizeof(last_dwnl_img));
    current_img_status = currentImg(current_img, sizeof(current_img));
    SWLOG_INFO("last_dwnl_status=%i and current_img_status=%i\n", last_dwnl_status, current_img_status);
    SWLOG_INFO("myfwversion:%s\n", myfwversion);
    SWLOG_INFO("cloudFWVersion:%s\n", cloudFWVersion);
    SWLOG_INFO("cloudFWFile:%s\n",cloudFWFile);
    t2ValNotify("cloudFWFile_split", (char *)cloudFWFile);
    SWLOG_INFO("lastdwnlfile:%s\n", last_dwnl_img);
    SWLOG_INFO("currentImg:%s\n", current_img);
    if (trigger_type == 1 || trigger_type == 3 || trigger_type == 4) {
        if ((false == current_img_status) || (false == last_dwnl_status)) {
            SWLOG_INFO("Unable to fetch current running image file name or last download file\n");
            SWLOG_INFO("Error identified with image file comparison !!! Proceeding with firmware version check.\n");
            if (strcasecmp(myfwversion, cloudFWVersion)) {
                SWLOG_INFO("Firmware versions are different myFWVersion : %s and cloudFWVersion : %s\n", myfwversion, cloudFWVersion);
                pci_valid_status = true;
            }
        }
        if (true == current_img_status) {
            if (strcasecmp(current_img, cloudFWFile)) {
                SWLOG_INFO("pci file check true\n");
                pci_valid_status = true;
                return pci_valid_status;
            }
        }
    }
    if (false == pci_valid_status) {
        if ((true == current_img_status) && (strcasecmp(current_img, cloudFWFile))) {
            if ((true == last_dwnl_status) && (strcasecmp(last_dwnl_img, cloudFWFile))) {
                SWLOG_INFO("pci File Check  is true\n");
                pci_valid_status = true;
	    } else {
            SWLOG_INFO("FW version of the standby image and the image to be upgraded are the same. No upgrade required.\n");
            t2CountNotify("SYST_INFO_SwdlSameImg_Stndby", 1);
            //updateFWDownloadStatus "$cloudProto" "No upgrade needed" "$cloudImmediateRebootFlag" "Versions Match" "$dnldVersion" "$cloudFWFile" "$runtime" "No upgrade needed" "$DelayDownloadXconf"
            eventManager(FW_STATE_EVENT, FW_STATE_NO_UPGRADE_REQUIRED);
            updateUpgradeFlag(2);
	    }
	} else {
          SWLOG_INFO("FW version of the active image and the image to be upgraded are the same. No upgrade required.\n");
          t2CountNotify("SYST_INFO_swdlSameImg", 1);
	      //updateFWDownloadStatus "$cloudProto" "No upgrade needed" "$cloudImmediateRebootFlag" "Versions Match" "$dnldVersion" "$cloudFWFile" "$runtime" "No upgrade needed" "$DelayDownloadXconf
	      eventManager(FW_STATE_EVENT, FW_STATE_NO_UPGRADE_REQUIRED);
          updateUpgradeFlag(2);
	    }
    }
    if (false == pci_valid_status) {
        snprintf(fwdls.method, sizeof(fwdls.method), "Method|xconf\n");
        snprintf(fwdls.proto, sizeof(fwdls.proto), "Proto|http\n");
        snprintf(fwdls.status, sizeof(fwdls.status), "Status|No upgrade needed\n");
        snprintf(fwdls.reboot, sizeof(fwdls.reboot), "Reboot|\n");
        snprintf(fwdls.failureReason, sizeof(fwdls.failureReason), "Failure|Versions Match\n");
        snprintf(fwdls.dnldVersn, sizeof(fwdls.dnldVersn), "DnldVersn|%s\n",cloudFWVersion);
        snprintf(fwdls.dnldfile, sizeof(fwdls.dnldfile), "DnldFile|%s\n", cloudFWFile);
        snprintf(fwdls.dnldurl, sizeof(fwdls.dnldurl), "DnldURL|\n");
        snprintf(fwdls.lastrun, sizeof(fwdls.lastrun), "LastRun|\n");
        snprintf(fwdls.FwUpdateState, sizeof(fwdls.FwUpdateState), "FwUpdateState|No upgrade needed\n");
        snprintf(fwdls.DelayDownload, sizeof(fwdls.DelayDownload), "DelayDownload|\n");
        updateFWDownloadStatus(&fwdls, "no");
    }
    return pci_valid_status;
}
