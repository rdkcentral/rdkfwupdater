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
#include "iarmInterface.h"
#include "rfcinterface.h"
#ifndef GTEST_ENABLE
#include "rdk_fwdl_utils.h"
#include "system_utils.h"
#include "common_device_api.h"
#endif
#include "json_parse.h"
#include "deviceutils.h"
#include "device_api.h"

#define RDM_VERSIONED_PACKAGES_CONF "/opt/rdm-versioned-packages.conf"
#define RDM_BUNDLE_ARG_MAX_LEN 1024
#define RDM_BINARY_PATH              "/usr/bin/rdm"
#define RDM_STATUS_LOG_PATH          "/opt/logs/meminsight.log"
#define BUILD_TYPE_MAX_LEN           32
#define RDM_BUNDLE_MAX_LEN           1024
static int trim_whitespace_inplace(char *str)
{
    char *src = NULL;
    char *dst = NULL;

    if (str == NULL) {
        return -1;
    }

    src = str;
    dst = str;

    while (*src != '\0') {
        if (*src != ' ' && *src != '\t' && *src != '\n' && *src != '\r') {
            *dst++ = *src;
        }
        src++;
    }
    *dst = '\0';
    return 0;
}

static int read_rdm_bundle_config(char *out, size_t out_size)
{
    FILE *fp = NULL;

    if (out == NULL || out_size == 0) {
        return -1;
    }

    out[0] = '\0';

    fp = fopen(RDM_VERSIONED_PACKAGES_CONF, "r");
    if (fp == NULL) {
        SWLOG_INFO("%s not found\n", RDM_VERSIONED_PACKAGES_CONF);
        return -1;
    }

    if (fgets(out, out_size, fp) == NULL) {
        fclose(fp);
        SWLOG_INFO("%s is empty\n", RDM_VERSIONED_PACKAGES_CONF);
        return -1;
    }
    fclose(fp);

    trim_whitespace_inplace(out);

    if (out[0] == '\0') {
        SWLOG_INFO("%s is empty after trimming whitespace\n", RDM_VERSIONED_PACKAGES_CONF);
        return -1;
    }

    return 0;
}

static int normalize_rdm_bundle_args(const char *input, char *output, size_t output_size)
{
    int ret = 0;

    if (input == NULL || output == NULL || output_size == 0) {
        return -1;
    }

    if (strstr(input, "dlAppBundle=") != NULL || strstr(input, "dlCertBundle=") != NULL) {
        ret = snprintf(output, output_size, "%s", input);
    } else {
        ret = snprintf(output, output_size, "dlAppBundle=%s", input);
    }

    if (ret < 0 || (size_t)ret >= output_size) {
        SWLOG_ERROR("RDM bundle argument too long, truncation occurred\n");
        return -1;
    }

    return 0;
}

static int build_xconf_rdm_bundle_args(const XCONFRES *response, char *dlBundle, size_t bundle_size)
{
    size_t available = 0;
    size_t current_len = 0;
    int retval = 0;

    if (response == NULL || dlBundle == NULL || bundle_size == 0) {
        return -1;
    }

    dlBundle[0] = '\0';
    available = bundle_size;

    if (response->dlCertBundle[0] != '\0') {
        retval = snprintf(dlBundle, available, "dlCertBundle=%s", response->dlCertBundle);
        if (retval < 0 || (size_t)retval >= available) {
            SWLOG_ERROR("dlCertBundle string too long, truncation occurred\n");
            return -1;
        }
    }

    if (response->dlAppBundle[0] != '\0') {
        current_len = strlen(dlBundle);
        available = bundle_size - current_len;

        if (dlBundle[0] != '\0') {
            retval = snprintf(dlBundle + current_len, available, "|dlAppBundle=%s", response->dlAppBundle);
        } else {
            retval = snprintf(dlBundle + current_len, available, "dlAppBundle=%s", response->dlAppBundle);
        }

        if (retval < 0 || (size_t)retval >= available) {
            SWLOG_ERROR("dlAppBundle string too long, truncation occurred\n");
            return -1;
        }
    }

    return 0;
}

static int get_effective_rdm_bundle_args(const XCONFRES *response, char *dlBundle, size_t bundle_size)
{
    char buildType[32] = {0};
    BUILDTYPE eBuildType;
    char fileBundle[RDM_BUNDLE_ARG_MAX_LEN] = {0};

    if (response == NULL || dlBundle == NULL || bundle_size == 0) {
        return -1;
    }

    dlBundle[0] = '\0';
    GetBuildType(buildType, sizeof(buildType), &eBuildType);

    if (eBuildType != ePROD) {
        if (read_rdm_bundle_config(fileBundle, sizeof(fileBundle)) == 0) {
            if (normalize_rdm_bundle_args(fileBundle, dlBundle, bundle_size) == 0) {
                SWLOG_INFO("Using RDM bundle config from %s\n", RDM_VERSIONED_PACKAGES_CONF);
                return 0;
            }
            return -1;
        }
        SWLOG_INFO("Falling back to XConf bundle configuration\n");
    }

    return build_xconf_rdm_bundle_args(response, dlBundle, bundle_size);
}

#ifndef HANDLER_TEST_ONLY
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
    else
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
    len = GetInstalledBundles( tmpbuf, sizeof(tmpbuf), "dlCertBundle" );
    if( totlen )
    {
        *(pTmpPost + totlen) = '&';
        ++totlen;
    }
    remainlen = szPostFieldOut - totlen;
    totlen += snprintf( (pTmpPost + totlen), remainlen, "dlCertBundle=%s", tmpbuf );
    len = GetInstalledBundles( tmpbuf, sizeof(tmpbuf), "dlAppBundle" );
    if( totlen )
    {
        *(pTmpPost + totlen) = '&';
        ++totlen;
    }
    remainlen = szPostFieldOut - totlen;
    totlen += snprintf( (pTmpPost + totlen), remainlen, "dlAppBundle=%s", tmpbuf );
    len = GetRdmManifestVersion( tmpbuf, sizeof(tmpbuf) );
    if( totlen )
    {
        *(pTmpPost + totlen) = '&';
        ++totlen;
    }
    remainlen = szPostFieldOut - totlen;
    totlen += snprintf( (pTmpPost + totlen), remainlen, "rdmCatalogueVersion=%s", tmpbuf );
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
    waitForNtp();
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
    SWLOG_INFO( "createJsonString: totlen = %zu\n%s\n", totlen, pPostFieldOut );
    return totlen;
}
#endif

#if 0
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

            if (isDirectCDNEnabled()) {
                GetJsonVal( pJson, "firmware_URL", pResponse->firmwareUrl, sizeof(pResponse->firmwareUrl) );
                GetJsonVal( pJson, "additionalFwVerInfo_URL", pResponse->pdriUrl, sizeof(pResponse->pdriUrl) );

                char peripheral_product[64] = {0};
                char peripheral_product_url[100] = {0};
                int peri_ret = getPeripheralProduct(peripheral_product, sizeof(peripheral_product));
                if (peri_ret != -1 && peripheral_product[0] != '\0') {
                    snprintf(peripheral_product_url, sizeof(peripheral_product_url), "%s_URL", peripheral_product);
                    GetJsonVal( pJson, peripheral_product, pResponse->peripheralFirmwares, sizeof(pResponse->peripheralFirmwares) );
                    SWLOG_INFO("remctrl with buf %s= %s\n", peripheral_product, pResponse->peripheralFirmwares);
                    GetJsonVal( pJson, peripheral_product_url, pResponse->remCtrlUrl, sizeof(pResponse->remCtrlUrl) );
                    SWLOG_INFO("remctrl with buf url %s= %s\n", peripheral_product_url, pResponse->remCtrlUrl);
                }
            } else {
                GetJsonValContaining( pJson, "remCtrl", pResponse->peripheralFirmwares, sizeof(pResponse->peripheralFirmwares) );
            }

            t2ValNotify("SYST_INFO_PRXR_Ver_split", pResponse->peripheralFirmwares);
            GetJsonVal( pJson, "dlCertBundle", pResponse->dlCertBundle, sizeof(pResponse->dlCertBundle) );
            GetJsonVal( pJson, "dlAppBundle", pResponse->dlAppBundle, sizeof(pResponse->dlAppBundle) );
            strncmp(pResponse->dlCertBundle, "lxyupdate-bundle:", 17)?1:t2ValNotify("lxybundleversion_split", pResponse->dlCertBundle + 17);
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
#endif
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
            t2ValNotify("SYST_INFO_PRXR_Ver_split", pResponse->peripheralFirmwares);
            GetJsonVal( pJson, "dlCertBundle", pResponse->dlCertBundle, sizeof(pResponse->dlCertBundle) );
            GetJsonVal( pJson, "dlAppBundle", pResponse->dlAppBundle, sizeof(pResponse->dlAppBundle) );
            strncmp(pResponse->dlCertBundle, "lxyupdate-bundle:", 17)?1:t2ValNotify("lxybundleversion_split", pResponse->dlCertBundle + 17);
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

int processJsonResponse(XCONFRES *response, const char *myfwversion, const char *model, const char *maint)
{
    bool valid_img = false;
    bool valid_pdri_img = true;
    bool ret_status = false;
    char last_dwnl_img[64];
    char current_img[64];
    char dlBundle[RDM_BUNDLE_ARG_MAX_LEN] = {0};
    FILE *fp = NULL;
    int ret = -1;

    last_dwnl_img[0] = 0;
    current_img[0] = 0;

    if( response != NULL && myfwversion != NULL && model != NULL )
    {
        makeHttpHttps( response->cloudFWLocation, sizeof(response->cloudFWLocation) );
        makeHttpHttps( response->ipv6cloudFWLocation, sizeof(response->ipv6cloudFWLocation) );
        SWLOG_INFO("cloudFWFile: %s\n", response->cloudFWFile);
        SWLOG_INFO("cloudFWLocation: %s\n", response->cloudFWLocation);
        SWLOG_INFO("ipv6cloudFWLocation: %s\n", response->ipv6cloudFWLocation);
        SWLOG_INFO("cloudFWVersion: %s\n", response->cloudFWVersion);
        SWLOG_INFO("cloudDelayDownload: %s\n", response->cloudDelayDownload);
        SWLOG_INFO("cloudProto: %s\n", response->cloudProto);
        SWLOG_INFO("cloudImmediateRebootFlag: %s\n", response->cloudImmediateRebootFlag);
        SWLOG_INFO("peripheralFirmwares: %s\n", response->peripheralFirmwares);
        SWLOG_INFO("dlCertBundle: %s\n", response->dlCertBundle);
        SWLOG_INFO("dlAppBundle: %s\n", response->dlAppBundle);
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
            SWLOG_INFO("Updating RDM Catalogue version %s from XCONF in /tmp/.xconfRdmCatalogueVersion file\n", response->rdmCatalogueVersion);
            fprintf(fp, "%s\n", response->rdmCatalogueVersion);
            fclose(fp);
        }

        if (get_effective_rdm_bundle_args(response, dlBundle, sizeof(dlBundle)) == 0 &&
            dlBundle[0] != '\0') {
            SWLOG_INFO("Calling rdm Versioned_app download to process bundle update\n");
            SWLOG_INFO("Effective RDM bundle args: %s\n", dlBundle);

            if (access("/usr/bin/rdm", F_OK) == 0) {
                SWLOG_INFO("RDM binary is present\n");
                v_secure_system("rdm -v \"%s\" >> /opt/logs/rdm_status.log 2>&1", dlBundle);
                SWLOG_INFO("RDM Versioned app Download started and completed\n");
            } else {
                SWLOG_INFO("File Not Present .. Download Failed\n");
            }
        }

        valid_img = validateImage(response->cloudFWFile, model);
        if ((*(response->cloudPDRIVersion)) != 0) {
            SWLOG_INFO("Validate PDRI image with device model number\n");
            valid_pdri_img = validateImage(response->cloudPDRIVersion, model);
            if (valid_pdri_img && (strstr(response->cloudPDRIVersion, "_PDRI_")) == NULL) {
                SWLOG_INFO("Invalid PDRI image: missing _PDRI_ substring\n");
                valid_pdri_img = false;
            }
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
