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

#include "json_process.h"
#include "rdkv_cdl.h"
#include "rdkv_cdl_log_wrapper.h"
#include "download_status_helper.h"
#include "device_status_helper.h"
#include "iarmInterface/iarmInterface.h"
#ifndef GTEST_ENABLE
#include "rdk_fwdl_utils.h"
#include "system_utils.h"
#endif
#include "json_parse.h"
#include "deviceutils.h"


int getXconfRespData( XCONFRES *pResponse, char *pJsonStr )
{
    JSON *pJson = NULL;
    int ret = -1;

    if( pResponse != NULL )
    {
        pJson = ParseJsonStr( pJsonStr );
        if( pJson != NULL )
        {
            GetJsonVal( pJson, "firmwareDownloadProtocol", pResponse->cloudProto, sizeof(pResponse->cloudProto) );
            GetJsonVal( pJson, "firmwareFilename", pResponse->cloudFWFile, sizeof(pResponse->cloudFWFile) );
            GetJsonVal( pJson, "firmwareLocation", pResponse->cloudFWLocation, sizeof(pResponse->cloudFWLocation) );
            GetJsonVal( pJson, "firmwareVersion", pResponse->cloudFWVersion, sizeof(pResponse->cloudFWVersion) );
            GetJsonVal( pJson, "rebootImmediately", pResponse->cloudImmediateRebootFlag, sizeof(pResponse->cloudImmediateRebootFlag) );
            GetJsonVal( pJson, "additionalFwVerInfo", pResponse->cloudPDRIVersion, sizeof(pResponse->cloudPDRIVersion) );
            GetJsonVal( pJson, "delayDownload", pResponse->cloudDelayDownload, sizeof(pResponse->cloudDelayDownload) );
            GetJsonValContaining( pJson, "remCtrl", pResponse->peripheralFirmwares, sizeof(pResponse->peripheralFirmwares) );
            GetJsonVal( pJson, "dlCertBundle", pResponse->dlCertBundle, sizeof(pResponse->dlCertBundle) );
            GetJsonVal( pJson, "rdmCatalogueVersion", pResponse->rdmCatalogueVersion, sizeof(pResponse->rdmCatalogueVersion) );
            GetJsonVal( pJson, "ipv6FirmwareLocation", pResponse->ipv6cloudFWLocation, sizeof(pResponse->ipv6cloudFWLocation) );

            FreeJson( pJson );
            ret = 0;
        }
    }
    else
    {
        SWLOG_INFO("getXconfRespData: input parameter is NULL\n");
    }

    return ret;
}

bool validateImage(const char *image_name, const char *model)
{
    bool status = false;
    if (image_name == NULL || model == NULL) {
        SWLOG_INFO("%s: parameter is NULL\n", __FUNCTION__);
        return status;
    }
    if ((strstr(image_name, model)) != NULL) {
        status = true;
    }
    return status;
}

/* Description: Use For processing data received from xconf
 * @param: response : Pointer to xconf response data
 * @param: myfwversion : Running firmware version
 * @param: model : Device model
 * @param: maint : maintenance manager enable or disable
 * @return int:
 * */
int processJsonResponse(XCONFRES *response, const char *myfwversion, const char *model, const char *maint)
{
    bool valid_img = false;
    bool valid_pdri_img = true;
    bool ret_status = false;
    char last_dwnl_img[64];
    char current_img[64];
    FILE *fp = NULL;
    int ret = -1;

    last_dwnl_img[0] = 0;
    current_img[0] = 0;

    if( response != NULL && myfwversion != NULL && model != NULL )
    {
        makeHttpHttps( response->cloudFWLocation, sizeof(response->cloudFWLocation) );
        makeHttpHttps( response->ipv6cloudFWLocation, sizeof(response->ipv6cloudFWLocation) );
        //check_pdri = isPDRIEnable(); 
        SWLOG_INFO("cloudFWFile: %s\n", response->cloudFWFile);
        SWLOG_INFO("cloudFWLocation: %s\n", response->cloudFWLocation);
        SWLOG_INFO("ipv6cloudFWLocation: %s\n", response->ipv6cloudFWLocation);
        SWLOG_INFO("cloudFWVersion: %s\n", response->cloudFWVersion);
        SWLOG_INFO("cloudDelayDownload: %s\n", response->cloudDelayDownload);
        SWLOG_INFO("cloudProto: %s\n", response->cloudProto);
        SWLOG_INFO("cloudImmediateRebootFlag: %s\n", response->cloudImmediateRebootFlag);
        SWLOG_INFO("peripheralFirmwares: %s\n", response->peripheralFirmwares);
        SWLOG_INFO("dlCertBundle: %s\n", response->dlCertBundle);
        SWLOG_INFO("cloudPDRIVersion: %s\n", response->cloudPDRIVersion);
        SWLOG_INFO("rdmCatalogueVersion: %s\n", response->rdmCatalogueVersion);

        fp = fopen("/tmp/.xconfssrdownloadurl", "w");
        if (fp != NULL) {
            fprintf(fp, "%s\n", response->cloudFWLocation);
            fclose(fp);
        }

        if( *response->rdmCatalogueVersion && 
            (fp=fopen( "/tmp/.xconfRdmCatalogueVersion", "w" )) != NULL )
        {
            SWLOG_INFO(  "Updating RDM Catalogue version %s from XCONF in /tmp/.xconfRdmCatalogueVersion file\n", response->rdmCatalogueVersion );
            fprintf( fp, "%s\n", response->rdmCatalogueVersion );
            fclose( fp );
        }
        if (response->dlCertBundle[0] != 0) {
            SWLOG_INFO("Calling /etc/rdm/rdmBundleMgr.sh to process bundle update\n");
            v_secure_system("sh /etc/rdm/rdmBundleMgr.sh '%s' '%s' >> /opt/logs/rdm_status.log 2>&1", response->dlCertBundle, response->cloudFWLocation);
            SWLOG_INFO("/etc/rdm/rdmBundleMgr.sh started and completed\n");
        }
        valid_img = validateImage(response->cloudFWFile, model);
	if ((*(response->cloudPDRIVersion)) != 0) {
	    SWLOG_INFO("Validate PDRI image with device model number\n");
            valid_pdri_img = validateImage(response->cloudPDRIVersion, model);
	}
        if ((false == valid_img) || (false == valid_pdri_img)) {
            SWLOG_INFO("Image configured is not of model %s.. Skipping the upgrade\nExiting from Image Upgrade process..!\n", model);
            eventManager(FW_STATE_EVENT, FW_STATE_FAILED);
            if (0 == (strncmp(maint, "true", 4))) {
                eventManager ("MaintenanceMGR", MAINT_FWDOWNLOAD_ERROR);
            }
            unlink(HTTP_CODE_FILE);
            ret = 1;
        } else {
            ret = 0;
        }
        ret_status = lastDwnlImg(last_dwnl_img, sizeof(last_dwnl_img));
        SWLOG_INFO("last_dwnl_status=%i\n", ret_status);
        ret_status = currentImg(current_img, sizeof(current_img));
        SWLOG_INFO("current_img_status=%i\n", ret_status);
        SWLOG_INFO("myFWVersion = %s\n", myfwversion);
        SWLOG_INFO("myFWFile = %s\n", current_img);
        SWLOG_INFO("lastDnldFile = %s\n", last_dwnl_img);
        SWLOG_INFO("cloudFWVersion = %s\n", response->cloudFWVersion);
        SWLOG_INFO("cloudFWFile = %s\n", response->cloudFWFile);
    }
    else
    {
        SWLOG_ERROR("%s ERROR: One or more input parameters are NULL\n", __FUNCTION__);
    }
    return ret;
}
