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

#include <sys/stat.h>
#include <curl/curl.h>
#include "rdkv_cdl.h"
#include "rdkv_cdl_log_wrapper.h"
#include "download_status_helper.h"
#include "device_status_helper.h"
#include "iarmInterface/iarmInterface.h"
#ifndef GTEST_ENABLE
#include "urlHelper.h"
#include "json_parse.h"
#include "rdk_fwdl_utils.h"
#include "system_utils.h"
#include "rbusInterface/rbusInterface.h"
#else
#include "rbus_mock.h"
#endif
#include "rfcInterface/rfcinterface.h"
#include <sys/wait.h>
#include "deviceutils.h"

extern int getTriggerType();
extern void t2CountNotify(char *marker, int val);

/* Description:Use for Flashing the image
 * @param: server_url : server url
 * @param: upgrade_file : Image file to be flash
 * @param: reboot_flag : reboot action after flash
 * @param: proto : protocol used
 * @param: upgrade_type : pci/pdri
 * @param: maint : maintenance manager enable/disable
 * @return int: 0 : Success other than 0 false
 * */
int flashImage(const char *server_url, const char *upgrade_file, const char *reboot_flag, const char *proto, int upgrade_type, const char *maint)
{
    int ret =  -1;
    bool mediaclient = false;
    char *failureReason = NULL;
    char cpu_arch[8] = {0};
    char headerinfofile[128] = {0};
    char difw_path[16] = {0};
    char *rflag = "0";
    char *uptype = "pci";
    char *file = NULL;
    int flash_status = -1;
    struct FWDownloadStatus fwdls;
    memset(&fwdls, '\0', sizeof(fwdls));

    if (server_url == NULL || upgrade_file == NULL || reboot_flag == NULL || proto == NULL || maint == NULL) {
        SWLOG_ERROR("%s : Parametr is NULL\n", __FUNCTION__);
        return ret;
    }
    if (0 == ((strncmp(reboot_flag, "true", 4)))) {
        rflag = "1";
	SWLOG_INFO("reboot flag = %s\n", rflag);
    }
    file = strrchr(upgrade_file, '/');
    if (file != NULL) {
        SWLOG_INFO("upgrade file = %s\n", file);
        SWLOG_INFO("upgrade file = %s\n", file+1);
    }
    snprintf(headerinfofile, sizeof(headerinfofile), "%s.header", upgrade_file);
    SWLOG_INFO("Starting Image Flashing ...\n");
    SWLOG_INFO("Upgrade Server = %s\nUpgrade File = %s\nReboot Flag = %s\nUpgrade protocol = %s\nPDRI Upgrade = %d\nImage name = %s\nheaderfile=%s\n", server_url, upgrade_file, reboot_flag, proto, upgrade_type, file, headerinfofile);
    if (upgrade_type == PDRI_UPGRADE) {
        SWLOG_INFO("Updating PDRI image with  %s\n", upgrade_file);
    }
    mediaclient = isMediaClientDevice();
    if (true == mediaclient) {
        eventManager(IMG_DWL_EVENT, IMAGE_FWDNLD_FLASH_INPROGRESS);
    }
    ret = getDevicePropertyData("CPU_ARCH", cpu_arch, sizeof(cpu_arch));
    if (ret == UTILS_SUCCESS) {
         SWLOG_INFO("cpu_arch = %s\n", cpu_arch);
    } else {
         SWLOG_ERROR("%s: getDevicePropertyData() for cpu arch fail\n", __FUNCTION__);
    }
    ret = getDevicePropertyData("DIFW_PATH", difw_path, sizeof(difw_path));
    if (ret == UTILS_SUCCESS) {
         SWLOG_INFO("difw path = %s\n", difw_path);
         //snprintf(dwnl_file_name, sizeof(dwnl_file_name), "%s/%s", difw_path, upgrade_file);
         //SWLOG_INFO("download file name = %s\n", dwnl_file_name);
    } else {
         SWLOG_ERROR("%s: getDevicePropertyData() for DIFW_PATH fail\n", __FUNCTION__);
    }
    if (0 == (filePresentCheck("/lib/rdk/imageFlasher.sh"))) {
        //lib/rdk/imageFlasher.sh $UPGRADE_PROTO $UPGRADE_SERVER $DIFW_PATH $UPGRADE_FILE $REBOOT_IMMEDIATE_FLAG $PDRI_UPGRADE
	//proto=2 for http
	//server=server url upgrade file = download file reboot = 1 for true and 0 for false
	if (upgrade_type == PDRI_UPGRADE) {
            uptype = "pdri";
	    SWLOG_INFO("upgrade type = %s\n", uptype);
	}
        ret = v_secure_system("/lib/rdk/imageFlasher.sh '%s' '%s' '%s' '%s' '%s' '%s'", proto, server_url, difw_path, file+1, rflag, uptype);
        flash_status = ret;
	SWLOG_INFO("flash_status = %d and ret = %d\n",flash_status, ret);
    } else {
         SWLOG_ERROR("imageFlasher.sh required for flash image. This is device specific implementation\n");
    }
    if (flash_status == 0 && (upgrade_type != PDRI_UPGRADE)) {
        SWLOG_INFO("doCDL success.\n");
        t2CountNotify("SYST_INFO_CDLSuccess", 1);
    }
    if (flash_status != 0) {
         SWLOG_INFO("Image Flashing failed\n");
         t2CountNotify("SYST_ERR_imageflsfail", 1);
 	 if (false == mediaclient) {
	     failureReason = "RCDL Upgrade Failed";
	     if ((strncmp(cpu_arch, "x86", 3)) == 0) {
	         failureReason = "ECM trigger failed";
	     }
	 } else {
             failureReason = "Failed in flash write";
             eventManager(IMG_DWL_EVENT, IMAGE_FWDNLD_FLASH_FAILED);
	     if ((strncmp(maint, "true", 4)) == 0) {
                 eventManager("MaintenanceMGR", MAINT_FWDOWNLOAD_ERROR);
	         SWLOG_INFO("Image Flash Failed and send status to MaintenanceMGR\n");
	     }
	 }
	 //updateFWDownloadStatus "$cloudProto" "Failure" "$cloudImmediateRebootFlag" "$failureReason" "$dnldVersion" "$cloudFWFile" "$runtime" "Failed" "$DelayDownloadXconf"
         snprintf(fwdls.status, sizeof(fwdls.status), "Status|Failure\n");
         snprintf(fwdls.FwUpdateState, sizeof(fwdls.FwUpdateState), "FwUpdateState|Failed\n");
         snprintf(fwdls.failureReason, sizeof(fwdls.failureReason), "FailureReason|%s\n", failureReason);
         eventManager(FW_STATE_EVENT, FW_STATE_FAILED);
         updateUpgradeFlag(2);// Remove file TODO: Logic need to change
    } else if (true == mediaclient) {
         SWLOG_INFO("Image Flashing is success\n");
         t2CountNotify("SYST_INFO_ImgFlashOK", 1);
	 //updateFWDownloadStatus "$cloudProto" "Success" "$cloudImmediateRebootFlag" "" "$dnldVersion" "$cloudFWFile" "$runtime" "Validation complete" "$DelayDownloadXconf"
         snprintf(fwdls.status, sizeof(fwdls.status), "Status|Success\n");
         snprintf(fwdls.FwUpdateState, sizeof(fwdls.FwUpdateState), "FwUpdateState|Validation complete\n");
         snprintf(fwdls.failureReason, sizeof(fwdls.failureReason), "FailureReason|");
	 char *pXconfCheckNow = calloc(10, sizeof(char));
         FILE *canFile = fopen("/tmp/xconfchecknow_val", "r");
         if (canFile != NULL) {
             int fret = fscanf(canFile, "%9s", pXconfCheckNow);
	     if (fret <= 0) {
	         SWLOG_ERROR("Device_X_COMCAST_COM_Xcalibur_Client_xconfCheckNow: Error reading from file\n");
	     }
             fclose(canFile);
         }
         else {
             SWLOG_INFO("Device_X_COMCAST_COM_Xcalibur_Client_xconfCheckNow: File does not exist\n");
         }
	 if (((strncmp(maint, "true", 4)) == 0) && (0 == (strncmp(reboot_flag, "true", 4))) && ((0 != strcasecmp("CANARY", pXconfCheckNow)) || (getTriggerType() != 3))) {
             eventManager("MaintenanceMGR", MAINT_CRITICAL_UPDATE);
	     SWLOG_INFO("Posting Critical update");
	 }
	 if( pXconfCheckNow != NULL ) {
             free(pXconfCheckNow);
         }
	 if (0 == filePresentCheck(upgrade_file)) {
             SWLOG_INFO("flashImage: Flashing completed. Deleting File:%s\n", upgrade_file);
             unlink(upgrade_file);
	 }
	 postFlash(maint, file+1, upgrade_type, reboot_flag);
         updateUpgradeFlag(2);// Remove file TODO: Logic need to change
    }
    if (true == mediaclient && (0 == filePresentCheck(upgrade_file))) {
        unlink(upgrade_file);
    }
    if (0 == filePresentCheck(headerinfofile)) {
        SWLOG_INFO("flashImage: Flashing completed. Deleting headerfile File:%s\n", headerinfofile);
        unlink(headerinfofile);
    }
    snprintf(fwdls.method, sizeof(fwdls.method), "Method|xconf\n");
    snprintf(fwdls.proto, sizeof(fwdls.proto), "Proto|http\n");
    snprintf(fwdls.reboot, sizeof(fwdls.reboot), "Reboot|%s\n", reboot_flag);
    //snprintf(fwdls.failureReason, sizeof(fwdls.failureReason), "%s", failureReason);
    snprintf(fwdls.dnldVersn, sizeof(fwdls.dnldVersn), "DnldVersn|\n");
    snprintf(fwdls.dnldfile, sizeof(fwdls.dnldfile), "DnldFile|%s\n", upgrade_file);
    snprintf(fwdls.dnldurl, sizeof(fwdls.dnldurl), "DnldURL|%s\n", server_url);
    snprintf(fwdls.lastrun, sizeof(fwdls.lastrun), "LastRun|\n");
    snprintf(fwdls.DelayDownload, sizeof(fwdls.DelayDownload), "DelayDownload|\n");
    if (upgrade_type == PDRI_UPGRADE) {
        updateFWDownloadStatus(&fwdls, "yes");
    } else {
        updateFWDownloadStatus(&fwdls, "no");
    }

    return flash_status;
}

/* Description:Use for do some action after flash complete successful
 * @param: upgrade_file : Image file to be flash
 * @param: reboot_flag : reboot action after flash
 * @param: upgrade_type : pci/pdri
 * @param: maint : maintenance manager enable/disable
 * @return int: 0
 * */

int postFlash(const char *maint, const char *upgrade_file, int upgrade_type, const char *reboot_flag)
{
    DownloadData DwnLoc;
    int ret = -1;
    char device_type[32];
    char device_name[32];
    char stage2lock[32] = {0};
    FILE *fp = NULL;
    char *stage2file = NULL;
    char *tmp_stage2lock = NULL;
    bool st_notify_flag = false;
    char post_data[] = "{\"jsonrpc\":\"2.0\",\"id\":\"3\",\"method\":\"org.rdk.FactoryProtect.1.setManufacturerData\",\"params\":{\"key\":\"deviceStage\",\"value\":\"stage2\"}}";

    if (maint == NULL || reboot_flag == NULL || upgrade_file == NULL) {
        SWLOG_ERROR("%s: Parameter is NULL\n", __FUNCTION__);
        return ret;
    }
    /*Below logic is use for to call updateSecurityStage function inside script*/
    fp = fopen("/tmp/rdkvfw_sec_stage", "w");
    if (fp != NULL) {
        fclose(fp);
    }
    ret = getDevicePropertyData("DEVICE_TYPE", device_type, sizeof(device_type));
    if (ret == UTILS_SUCCESS) {
         SWLOG_INFO("%s: device_type = %s\n", __FUNCTION__, device_type);
    } else {
         SWLOG_ERROR("%s: getDevicePropertyData() for device_type fail\n", __FUNCTION__);
	 return ret;
    }
    ret = getDevicePropertyData("DEVICE_NAME", device_name, sizeof(device_name));
    if (ret == UTILS_SUCCESS) {
         SWLOG_INFO("%s: device_name = %s\n", __FUNCTION__, device_name);
    } else {
         SWLOG_ERROR("%s: getDevicePropertyData() for device_name fail\n", __FUNCTION__);
	 return ret;
    }
    if (0 == (strncmp(device_name, "PLATCO", 6))) {
        ret = getDevicePropertyData("STAGE2LOCKFILE", stage2lock, sizeof(stage2lock));
        if (ret == UTILS_SUCCESS) {
            SWLOG_INFO("%s: security stage2file name = %s\n", __FUNCTION__, stage2lock);
            /* Below block is use for remove special charecter("") from the string */
            if (stage2lock[0] == '"') {
                tmp_stage2lock = strchr(stage2lock+1, '"');
                if (tmp_stage2lock != NULL) {
                    *tmp_stage2lock = '\0';
                }
                SWLOG_INFO("Security stage file name=%s\n", stage2lock+1);
                stage2file = stage2lock+1;
                if (stage2file != NULL) {
                    SWLOG_INFO("Security stage file name after remove special character=%s\n", stage2file);
                }
            }else {
                stage2file = stage2lock;
            }
            if (0 != (filePresentCheck(stage2file))) {
                startFactoryProtectService();
                sleep(2);
                
                if( MemDLAlloc( &DwnLoc, DEFAULT_DL_ALLOC ) == 0 )
                {
                    getJsonRpc(post_data, &DwnLoc);//Update security stage to stage2
                    fp = fopen(stage2file, "w");
                    if (fp != NULL) {
                        SWLOG_INFO("Security stage file created\n");
                        fclose(fp);
                    }else {
                        SWLOG_ERROR("Unable to create Security stage file\n");
                    }
                    if( DwnLoc.pvOut != NULL )
                    {
                        free( DwnLoc.pvOut );
                    }
                }
            }
        } else {
            SWLOG_ERROR("%s: getDevicePropertyData() for device_name fail\n", __FUNCTION__);
        }
    }
    st_notify_flag = isMmgbleNotifyEnabled();
    eventManager(FW_STATE_EVENT, FW_STATE_VALIDATION_COMPLETE);
    eventManager(IMG_DWL_EVENT, IMAGE_FWDNLD_FLASH_COMPLETE);
    if( isInStateRed() ) {
	    eventManager(RED_STATE_EVENT, RED_RECOVERY_PROGRAMMED);
	    SWLOG_INFO("Creating red_state_reboot file\n");
	    fp = fopen(RED_STATE_REBOOT, "w");
	    if (fp != NULL) {
		    fclose(fp);
	    }
    }
    if ((strncmp(device_type, "broadband", 9)) && (0 == (strncmp(maint, "true", 4)))) {
	eventManager("MaintenanceMGR", MAINT_FWDOWNLOAD_COMPLETE);
    }
    sleep(5);
    sync();
    char* pXconfCheckNow = calloc(10, sizeof(char));
    if (pXconfCheckNow == NULL) {
        SWLOG_ERROR("Device_X_COMCAST_COM_Xcalibur_Client_xconfCheckNow: CALLOC failure\n");
	return ret;
    }
    FILE *file = fopen("/tmp/xconfchecknow_val", "r");
    if (file != NULL) {
        if (fscanf(file, "%9s", pXconfCheckNow) == EOF) {
	    SWLOG_ERROR("Device_X_COMCAST_COM_Xcalibur_Client_xconfCheckNow: Error reading file\n");
	}
        fclose(file);
    }
    else {
        SWLOG_INFO("Device_X_COMCAST_COM_Xcalibur_Client_xconfCheckNow: Error opening file for read\n");
    }
    if (0 != (filePresentCheck("/tmp/fw_preparing_to_reboot"))) {
	fp = fopen("/tmp/fw_preparing_to_reboot", "w");
	if (fp == NULL) {
	    SWLOG_ERROR("Error creating file /tmp/fw_preparing_to_reboot\n");
	}else {
	    SWLOG_INFO("Creating flag for preparing to reboot event sent to AS/EPG\n");
	    fclose(fp);
	}
        if ((0 != strcasecmp("CANARY", pXconfCheckNow)) || (getTriggerType() != 3)) {
            eventManager(FW_STATE_EVENT,FW_STATE_PREPARING_TO_REBOOT);
	}
    }
    if (upgrade_type == PDRI_UPGRADE) {
        SWLOG_INFO("Reboot Not Needed after PDRI Upgrade..!\n");
    } else {
        SWLOG_INFO("%s : Upgraded file = %s\n", __FUNCTION__, upgrade_file);
        fp = fopen("/opt/cdl_flashed_file_name", "w");
        if (fp != NULL) {
            fprintf(fp, "%s\n", upgrade_file);
            fclose(fp);
        }
	if ((0 == strcasecmp("CANARY", pXconfCheckNow)) && (getTriggerType() == 3)) {

            char post_data[] = "{\"jsonrpc\":\"2.0\",\"id\":\"42\",\"method\": \"org.rdk.System.getPowerState\"}";
            DownloadData DwnLoc = {NULL, 0, 0};
            JSON *pJson = NULL;
            JSON *pItem = NULL;
            JSON *res_val = NULL;

            if( MemDLAlloc( &DwnLoc, DEFAULT_DL_ALLOC ) == 0 ) {
              if (0 != getJsonRpc(post_data, &DwnLoc)) {
                  SWLOG_INFO("%s :: isconnected JsonRpc call failed\n",__FUNCTION__);
		  free(pXconfCheckNow);
		  if (DwnLoc.pvOut != NULL) {
		      free(DwnLoc.pvOut);
		  }
                  return ret;
              }
              pJson = ParseJsonStr( (char *)DwnLoc.pvOut );
              if( pJson != NULL ) {
                  pItem = GetJsonItem( pJson, "result" );
                  res_val = GetJsonItem( pItem, "powerState" );
              }
              else {
                  SWLOG_INFO("%s :: isconnected JsonRpc response is empty\n",__FUNCTION__);
		  free(pXconfCheckNow);
		  if (DwnLoc.pvOut != NULL) {
		      free(DwnLoc.pvOut);
		  }
                  return ret;
              }
            }

	    if(res_val != NULL) {
                if(0 == strcasecmp("ON", res_val->valuestring)) {
                    SWLOG_INFO("Defer Reboot for Canary Firmware Upgrade since power state is ON\n");
                    t2CountNotify("SYS_INFO_DEFER_CANARY_REBOOT", 1);
                }
#ifndef GTEST_ENABLE
                else {
                    // Call rbus method - Device.X_RDKCENTRAL-COM_T2.UploadDCMReport
                    if( RBUS_ERROR_SUCCESS != invokeRbusDCMReport()) {
		        SWLOG_ERROR("Error in uploading telemetry report\n");
			if( DwnLoc.pvOut != NULL ) {
                            free( DwnLoc.pvOut );
                        }
                        if( pJson != NULL ) {
                            FreeJson( pJson );
                        }
			if ( pXconfCheckNow != NULL ) {
	                    free(pXconfCheckNow);
	                }
			return ret;
		    }
                    if (0 == (strncmp(reboot_flag, "true", 4))) {
                        SWLOG_INFO("Rebooting from RDK for Canary Firmware Upgrade\n");
                        t2CountNotify("SYS_INFO_CANARY_Update", 1);
                        v_secure_system("sh /rebootNow.sh -s '%s' -o '%s'","CANARY_Update", "Rebooting the box from RDK for Pending Canary Firmware Upgrade...");
		    }
		}
#endif
	    }
            if( DwnLoc.pvOut != NULL ) {
                free( DwnLoc.pvOut );
            }
            if( pJson != NULL ) {
                FreeJson( pJson );
            }
        }
        else if (0 == (strncmp(maint, "true", 4))) {
	    eventManager("MaintenanceMGR", MAINT_REBOOT_REQUIRED);
	    if (0 == (strncmp(device_name, "PLATCO", 6)) && (0 == (strncmp(reboot_flag, "true", 4)))) {
		SWLOG_INFO("Send notification to reboot in 10mins due to critical upgrade\n");
		eventManager(FW_STATE_EVENT, FW_STATE_CRITICAL_REBOOT);
		SWLOG_INFO("Sleeping for 600 sec before rebooting the STB\n");
		sleep(600);
		SWLOG_INFO("Application Reboot Timer of 600 expired, Rebooting from RDK\n");
		//sh /rebootNow.sh -s UpgradeReboot_"`basename $0`" -o "Rebooting the box from RDK for Pending Critical Firmware Upgrade..."
		v_secure_system("sh /rebootNow.sh -s '%s' -o '%s'","UpgradeReboot_rdkvfwupgrader", "Rebooting the box from RDK for Pending Critical Firmware Upgrade...");
	    }
	    updateOPTOUTFile(MAINTENANCE_MGR_RECORD_FILE);
	}else {
            if (0 == (strncmp(reboot_flag, "true", 4))) {
                SWLOG_INFO("Download is complete. Rebooting the box now...\n");
		SWLOG_INFO("Trigger RebootPendingNotification in background\n");
		if (true == st_notify_flag) {
		    SWLOG_INFO("RDKV_REBOOT : Setting RebootPendingNotification before reboot\n");
		    notifyDwnlStatus(RFC_FW_REBOOT_NOTIFY, REBOOT_PENDING_DELAY, RFC_UINT);
		    SWLOG_INFO("RDKV_REBOOT  : RebootPendingNotification SET succeeded\n");
		}
		unsetStateRed();
		SWLOG_INFO("sleep for 2 sec to send reboot pending notification\n");
		sleep(2);
		//sh /rebootNow.sh -s UpgradeReboot_"`basename $0`" -o "Rebooting the box after Firmware Image Upgrade..."
		v_secure_system("sh /rebootNow.sh -s '%s' -o '%s'", "UpgradeReboot_rdkvfwupgrader", "Rebooting the box after Firmware Image Upgrade...");
	    }
	}
    }
    if( pXconfCheckNow != NULL ) {
        free(pXconfCheckNow);
    }
    return 0;
}
