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
#include <pthread.h>

#include "rdkv_cdl.h"
#include "rdkv_cdl_log_wrapper.h"
#include "downloadUtil.h"
#include "urlHelper.h"
#include "download_status_helper.h"
#include "device_status_helper.h"
#include "iarmInterface/iarmInterface.h"
#include "codebigUtils.h"
#include "mtlsUtils.h"
#ifndef GTEST_ENABLE
#include "rdk_fwdl_utils.h"
#include "system_utils.h"
#else
#include "miscellaneous.h"
#endif
#include "rfcInterface/rfcinterface.h"
#include "json_process.h"
#include "device_api.h"
#include "deviceutils.h"

/**
 * @Description Directly downloads a firmware image using a Content Delivery Network (CDN).
 *
 * This function downloads a firmware image directly from a CDN/SSR server. It handles the
 * download process of PCI/PDRI/PERI and updates the provided response structure accordingly.
 *
 * @param response Pointer to the XCONFRES structure where the response details will be stored.
 * @param cur_img_name Pointer to the name of the current firmware image to be downloaded.
 * @param device_info Pointer to the DeviceProperty_t structure containing device information.
 * @param server_type Integer indicating the type of server from which to download the image.
 * @param pHttp_code Pointer to an integer where the HTTP response code will be stored.
 *
 * @return An integer indicating the status of the download process.
 *         - Returns 0 on success.
 *         - Returns a non-zero value if an error occurs during the download process.
 */
int DirectCDNDownload( XCONFRES *response, char *cur_img_name, DeviceProperty_t *device_info, int server_type, int *pHttp_code )
{
    int curl_ret_code  = -1;
    DownloadData DwnLoc;
    char *pJSONStr = NULL;      // contains the device data string to send to XCONF server
    char *pServURL = NULL;      // the server to do the XCONF comms
    size_t len = 0;
    int ret = -1;
    int cnt = 0;
    int total_retry_cnt = 3;
    bool directCdn = false;
    int pci_upgrade_status = DIRECT_CDN_RETRY_ERR;
    int pdri_upgrade_status = DIRECT_CDN_RETRY_ERR;
    int peri_upgrade_status = -1;

    DwnLoc.pvOut = NULL;
    DwnLoc.datasize = 0;
    DwnLoc.memsize = 0;
    *pHttp_code = 0;
    int json_res = -1;
    int xconf_ret = -1;
    int proto = 1;

    if( MemDLAlloc( &DwnLoc, DEFAULT_DL_ALLOC ) == 0 ) {
    }else{
        SWLOG_INFO( "DirectCDNDownload: MemDLAlloc Fail\n" );
        return curl_ret_code;
    }
    if( (pJSONStr=(char*)malloc( JSON_STR_LEN )) != NULL )
    {
        if( (pServURL=(char*)malloc( URL_MAX_LEN )) != NULL )
        {
            len = GetServURL( pServURL, URL_MAX_LEN );
            SWLOG_INFO( "DirectCDNDownload: server URL %s\n", pServURL );
            if( len )
            {
                len = createJsonString( pJSONStr, JSON_STR_LEN );
		while(cnt < total_retry_cnt && ((pci_upgrade_status == DIRECT_CDN_RETRY_ERR) || (pdri_upgrade_status == DIRECT_CDN_RETRY_ERR)) ) {
                    ret = upgradeRequest( XCONF_UPGRADE, server_type, directCdn, pServURL, &DwnLoc, pJSONStr, pHttp_code );
                    if( ret == 0 && *pHttp_code == 200 && DwnLoc.pvOut != NULL )
                    {
                        SWLOG_INFO( "DirectCDNDownload: Calling getXconfRespData with input = %s\n", (char *)DwnLoc.pvOut );
                        SWLOG_INFO( "DirectCDNDownload: DownloadDate Size = %d\n",DwnLoc.datasize );
                        xconf_ret = getXconfRespData( response, (char *)DwnLoc.pvOut );
            		SWLOG_INFO("DirectCDNDownload: XCONF Download Success ret=%d\n", xconf_ret);
                        json_res = processJsonResponse(response, cur_img_name, device_info->model, device_info->maint_status);
                        SWLOG_INFO("DirectCDNDownload: processJsonResponse returned %d\n", json_res);
                        if (0 == (strncmp(response->cloudProto, "tftp", 4))) {
                            proto = 0;
                        }
                        if ((proto == 1) && (json_res == 0)) {
		            if (pci_upgrade_status == DIRECT_CDN_RETRY_ERR) {
                                pci_upgrade_status = checkTriggerUpgrade(response, device_info->model, true, PCI_UPGRADE);
                                SWLOG_INFO("DirectCDNDownload: pci_upgrade_status %d\n", pci_upgrade_status);
			    }
			    if (pdri_upgrade_status == DIRECT_CDN_RETRY_ERR) {
                                pdri_upgrade_status = checkTriggerUpgrade(response, device_info->model, true, PDRI_UPGRADE);
                                SWLOG_INFO("DirectCDNDownload: pdri_upgrade_status %d\n", pdri_upgrade_status);
			    }
			    if (peri_upgrade_status != 0) {
                                peri_upgrade_status = checkTriggerUpgrade(response, device_info->model, true, PERIPHERAL_UPGRADE);
                                SWLOG_INFO("DirectCDNDownload: peri_upgrade_status %d\n", peri_upgrade_status);
			    }
			    if ((pci_upgrade_status == 0) && (pdri_upgrade_status == 0)) {
				break;
			    }
                        }else if(proto == 0) {
                            SWLOG_INFO("DirectCDNDownload: tftp protocol support not present.\n");
			    break;
                        }else {
                            SWLOG_ERROR("DirectCDNDownload: processJsonResponse return fail:%d\n", json_res);
			    break;
                        }
                    }
		    else
		    {
                            SWLOG_ERROR( "DirectCDNDownload: DirectCDNDownload: XCONF fail\n" );
			    break;
		    }
		    cnt++;
		}
            }
            else
            {
                SWLOG_ERROR( "DirectCDNDownload: no valid server URL\n" );
            }
            free( pServURL );
        }
        else
        {
            SWLOG_ERROR("DirectCDNDownload: Failed malloc for server URL of %d bytes\n", URL_MAX_LEN );
        }
        free( pJSONStr );
        }
    else
    {
        SWLOG_ERROR("DirectCDNDownload: Failed malloc for json string of %d bytes\n", JSON_STR_LEN );
    }
    if( DwnLoc.pvOut != NULL )
    {
        free( DwnLoc.pvOut );
    }
    if ((pci_upgrade_status == 0) && (pdri_upgrade_status == 0)) {
	curl_ret_code = 0;
    }
    SWLOG_INFO("DirectCDNDownload: Function return %d\n", curl_ret_code);
    return curl_ret_code;
}
