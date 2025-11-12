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

#include "download_status_helper.h"
#include "rfcinterface.h"

/* Description: Updating Firmware Download status inside status File.
 * @param fwdls: This is structure use to Receive all data required to write inside status file.
 * */
int updateFWDownloadStatus(struct FWDownloadStatus *fwdls, const char *disableStatsUpdate) {
    FILE *fp;
    //strncpy(disableStatsUpdate, "no", 2); // For testing. Need to remove final change
    //Flag to disable STATUS_FILE updates in case of PDRI upgrade
    if (fwdls == NULL || disableStatsUpdate == NULL) {
        SWLOG_ERROR("updateFWDownloadStatus(): Parameter is NULL\n");
        return FAILURE;
    }
    if((strcmp(disableStatsUpdate, "yes")) == 0) {
        SWLOG_INFO("updateFWDownloadStatus(): Status Update Disable:%s\n", disableStatsUpdate);
        return SUCCESS;
    }
    fp = fopen(STATUS_FILE, "w");
    if(fp == NULL) {
        SWLOG_ERROR("updateFWDownloadStatus(): fopen failed:%s\n", STATUS_FILE);
        return FAILURE;
    }
    SWLOG_INFO("updateFWDownloadStatus(): Going to write:%s\n", STATUS_FILE);
    //TODO: Need to implement if FwUpdateState not present read from STATUS_FILE file
    fprintf( fp, "%s", fwdls->method );
    fprintf( fp, "%s", fwdls->proto );
    fprintf( fp, "%s", fwdls->status );
    fprintf( fp, "%s", fwdls->reboot );
    fprintf( fp, "%s", fwdls->failureReason );
    fprintf( fp, "%s", fwdls->dnldVersn );
    fprintf( fp, "%s", fwdls->dnldfile );
    fprintf( fp, "%s", fwdls->dnldurl );
    fprintf( fp, "%s", fwdls->lastrun );
    fprintf( fp, "%s", fwdls->FwUpdateState );
    fprintf( fp, "%s", fwdls->DelayDownload );
    fclose(fp);
    return SUCCESS;
}

/* Description: Updating download status to the rfc.
 * @param key: Pointer to the rfc name
 * @param value: Rfc value going to set
 * */
int notifyDwnlStatus(const char *key, const char *value, RFCVALDATATYPE datatype) {
    int ret = WRITE_RFC_FAILURE;
    if (key == NULL || value == NULL) {
        SWLOG_ERROR("%s: Parameter is NULL\n", __FUNCTION__);
    } else {
        ret = write_RFCProperty("NotifyDwnlSt", key, value, datatype);
    }
    return ret;
}
