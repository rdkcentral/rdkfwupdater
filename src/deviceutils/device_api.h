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

#ifndef __DEVICE_API_H__
#define __DEVICE_API_H__

#ifndef GETRDMMANIFESTVERSION_IN_SCRIPT
    #define GETRDMMANIFESTVERSION_IN_SCRIPT
#endif

#ifndef GTEST_ENABLE
#include "rdk_fwdl_utils.h"
extern char* strcasestr(const char* s1, const char* s2);
#endif

#define URL_MAX_LEN 512

typedef enum {
    eRecovery,
    eAutoExclude,
    eBootstrap,
    eDevXconf,
    eCIXconf,
    eXconf,
    eDac15
} TR181URL ;

#define DEVICE_CAPABILITIES     "rebootDecoupled&capabilities=RCDL&capabilities=supportsFullHttpUrl"
#ifndef GTEST_ENABLE
	#define BOOTSTRAP_FILE          "/opt/secure/RFC/bootstrap.ini"
	#define PARTNER_ID_FILE         "/opt/www/authService/partnerId3.dat"
	#define VERSION_FILE            "/version.txt"
	#define ESTB_MAC_FILE           "/tmp/.estb_mac"
#else
	#define BOOTSTRAP_FILE          "/tmp/bootstrap.ini"
	#define PARTNER_ID_FILE         "/tmp/partnerId3.dat"
	#define VERSION_FILE            "/tmp/version_test.txt"
	#define ESTB_MAC_FILE           "/tmp/.estb_mac_gtest.txt"
#endif
#define OUTPUT_JSON_FILE        "/opt/output.json"
#define OUTPUT_JSON_FILE_X86    "/tmp/output.json"
#ifndef GTEST_ENABLE
	#define TIMEZONE_DST_FILE       "/opt/persistent/timeZoneDST"
	#define TIMEZONE_OFFSET_MAP     "/etc/timeZone_offset_map"
	#define STATE_RED_CONF          "/opt/stateredrecovry.conf"
	#define SWUPDATE_CONF           "/opt/swupdate.conf"
#else
	#define TIMEZONE_DST_FILE       "/tmp/timeZoneDST"
	#define TIMEZONE_OFFSET_MAP     "/tmp/timeZone_offset_map"
	#define STATE_RED_CONF          "/tmp/stateredrecovry.conf"
	#define SWUPDATE_CONF           "/tmp/swupdate.conf"
#endif
#define PERIPHERAL_JSON_FILE    "/tmp/rc-proxy-params.json"

#define RFC_ACCOUNTID       "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.AccountInfo.AccountID"
#define RFC_SERIALNUM       "Device.DeviceInfo.SerialNumber"
#define RFC_OS_CLASS        "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Bootstrap.OsClass"
#define MR_ID               "Device.DeviceInfo.MigrationPreparer.MigrationReady"

#define NO_URL              NULL

#define RFC_DAC15URL        "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Sysint.DAC15CDLUrl"
#define DAC15URL            "DAC15CDLUrl"

#define RFC_XCONFURL        "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Sysint.XconfUrl"
#define XCONFURL            "XconfUrl"

#define RFC_CIXCONFURL      "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Sysint.CIXconfUrl"
#define CIXCONFURL          "CIXconfUrl"

#define RFC_DEVXCONFURL     "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Sysint.XconfDEVUrl"
#define DEVXCONFURL         "XconfDEVUrl"

#define RFC_RECOVERYURL     "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Bootstrap.XconfRecoveryUrl"
#define RECOVERYURL         "XconfRecoveryUrl"
#define RECOVERYDEFAULT     NO_URL

#define RFC_AUTOEXCLUDEURL  "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.FWUpdate.AutoExcluded.XconfUrl"
#define AUTOEXCLUDEURL      "AxXconfUrl"
#define AUTOEXCLUDEDEFAULT  NO_URL

#define RFC_BOOTSTRAPURL    "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Bootstrap.XconfUrl"
#define BOOTSTRAPURL        "BsXconfUrl"
#define BOOTSTRAPDEFAULT    NO_URL

/*
#define Debug_Services_Enabled(labSigned, eBuildType, dbgServices, deviceType) \
    ( ((labSigned) && ((eBuildType) == ePROD) && (dbgServices) && ((deviceType) == DEVICE_TYPE_TEST)) || ((eBuildType) == eDEV) )
*/

/* function GetServerUrlFile - scans a file for a URL. 
        Usage: size_t GetServerUrlFile <char *pServUrl> <size_t szBufSize> <char *pFileName>
 
            pServUrl - pointer to a char buffer to store the output string..

            szBufSize - the size of the character buffer in argument 1.

            pFileName - a character pointer to a filename to scan.

            RETURN - number of characters copied to the output buffer.
*/
size_t GetServerUrlFile(char *pServUrl, size_t szBufSize, char *pFileName);


/* function GetTimezone - returns the timezone for the device. 
        Usage: size_t GetTimezone <char *pTimezone> <const char  *cpu> <size_t szBufSize>
 
            pTimezone - pointer to a char buffer to store the output string.

	        cpuArch - poniter holds device cpu type

            szBufSize - the size of the character buffer in argument 1.

            RETURN - number of characters copied to the output buffer.
*/
size_t GetTimezone(char *pTimezone, const char *cpuArch, size_t szBufSize);

/* function GetAdditionalFwVerInfo - returns the PDRI filename plus Remote Info for the device. 
        Usage: size_t GetAdditionalFwVerInfo <char *pAdditionalFwVerInfo> <size_t szBufSize>
 
            pAdditionalFwVerInfo - pointer to a char buffer to store the output string.

            szBufSize - the size of the character buffer in argument 1.

            RETURN - number of characters copied to the output buffer.
*/
size_t GetAdditionalFwVerInfo(char *pAdditionalFwVerInfo, size_t szBufSize);

/* function GetPDRIFileName - returns the PDRI for the device. 
        Usage: size_t GetPDRIFileName <char *pPDRIFilename> <size_t szBufSize>
 
            pPDRIFilename - pointer to a char buffer to store the output string.

            szBufSize - the size of the character buffer in argument 1.

            RETURN - number of characters copied to the output buffer.
*/
size_t GetPDRIFileName(char *pPDRIFilename, size_t szBufSize);


/* function GetInstalledBundles - gets the bundles installed on a device. 
        Usage: size_t GetInstalledBundles <char *pBundles> <size_t szBufSize>
 
            pBundles - pointer to a char buffer to store the output string.

            szBufSize - the size of the character buffer in argument 1.

            RETURN - number of characters copied to the output buffer.
*/
size_t GetInstalledBundles(char *pBundles, size_t szBufSize);


/* function GetUTCTime - gets a formatted UTC device time. Example;
    Tue Jul 12 21:56:06 UTC 2022 
        Usage: size_t GetUTCTime <char *pUTCTime> <size_t szBufSize>
 
            pUTCTime - pointer to a char buffer to store the output string.

            szBufSize - the size of the character buffer in argument 1.

            RETURN - number of characters copied to the output buffer.
*/
size_t GetUTCTime(char *pUTCTime, size_t szBufSize);


/* function GetCapabilities - gets the device capabilities.
 
        Usage: size_t GetCapabilities <char *pCapabilities> <size_t szBufSize>
 
            pCapabilities - pointer to a char buffer to store the output string.

            szBufSize - the size of the character buffer in argument 1.

            RETURN - number of characters copied to the output buffer.
*/
size_t GetCapabilities(char *pCapabilities, size_t szBufSize);


/* function GetPartnerId - gets the partner ID of the device.
 
        Usage: size_t GetPartnerId <char *pPartnerId> <size_t szBufSize>
 
            pPartnerId - pointer to a char buffer to store the output string.

            szBufSize - the size of the character buffer in argument 1.

            RETURN - number of characters copied to the output buffer.
*/
size_t GetPartnerId(char *pPartnerId, size_t szBufSize);

/* function GetOsClass - gets the OsClass of the device.
 
        Usage: size_t GetOsClass( char *pOsClass, size_t szBufSize )
 
            pOsClass - pointer to a char buffer to store the output string.

            szBufSize - the size of the character buffer in argument 1.

            RETURN - number of characters copied to the output buffer.
*/
size_t GetOsClass( char *pOsClass, size_t szBufSize );

/* function GetSerialNum - gets the serial number of the device.
 
        Usage: size_t GetSerialNum <char *pSerialNum> <size_t szBufSize>
 
            pSerialNum - pointer to a char buffer to store the output string.

            szBufSize - the size of the character buffer in argument 1.

            RETURN - number of characters copied to the output buffer.
*/
size_t GetSerialNum( char *pSerialNum, size_t szBufSize);


/* function GetExperience - gets the experience of the device.
 
        Usage: size_t GetExperience <char *pExperience> <size_t szBufSize>
 
            pExperience - pointer to a char buffer to store the output string.

            szBufSize - the size of the character buffer in argument 1.

            RETURN - number of characters copied to the output buffer.
*/
// TODO: GetExperience must be implemented correctly
size_t GetExperience(char *pExperience, size_t szBufSize);


/* function GetMigrationReady - gets the migration readiness status.

 	Usage: size_t GetMigrationReady <char *pMRComponents> <size_t szBufSize>

 	pMRComponents - pointer to a char buffer to store the output string.

 	szBufSize - the size of the character buffer in argument 1.

 	RETURN - number of characters copied to the output buffer.
 */
 size_t GetMigrationReady(char *pMRComponents, size_t szBufSize);


/* function GetAccountID - gets the account ID of the device.
 
        Usage: size_t GetAccountID <char *pAccountID> <size_t szBufSize>
 
            pAccountID - pointer to a char buffer to store the output string.

            szBufSize - the size of the character buffer in argument 1.

            RETURN - number of characters copied to the output buffer.
*/
size_t GetAccountID(char *pAccountID, size_t szBufSize);

#ifdef GTEST_ENABLE
/* function GetModelNum - gets the model number of the device.

        Usage: size_t GetModelNum <char *pModelNum> <size_t szBufSize>

            pModelNum - pointer to a char buffer to store the output string.
            szBufSize - the size of the character buffer in argument 1.
            RETURN - number of characters copied to the output buffer.
*/
size_t GetModelNum(char *pModelNum, size_t szBufSize);
#endif

/* function GetBuildType - gets the build type of the device in lowercase. Optionally, sets an enum
    indication the build type.
    Example: vbn or prod or qa or dev
 
        Usage: size_t GetBuildType <char *pBuildType> <size_t szBufSize> <BUILDTYPE *peBuildTypeOut>
 
            pBuildType - pointer to a char buffer to store the output string.

            szBufSize - the size of the character buffer in argument 1.
 
            peBuildTypeOut - a pointer to a BUILDTYPE enum or NULL if not needed by the caller.
                Contains an enum indicating the buildtype if not NULL on function exit.

            RETURN - number of characters copied to the output buffer.
*/
size_t GetBuildType(char *pBuildType, size_t szBufSize, BUILDTYPE *peBuildTypeOut);

/* function GetMFRName - gets the  manufacturer name of the device.
        Usage: size_t GetMFRName <char *pMFRName> <size_t szBufSize>
            pMFRName - pointer to a char buffer to store the output string.

            szBufSize - the size of the character buffer in argument 1.

            RETURN - number of characters copied to the output buffer.
*/
size_t GetMFRName(char *pMFRName, size_t szBufSize);


/* function GetFirmwareVersion - gets the firmware version of the device.
 
        Usage: size_t GetFirmwareVersion <char *pFWVersion> <size_t szBufSize>
 
            pFWVersion - pointer to a char buffer to store the output string.

            szBufSize - the size of the character buffer in argument 1.

            RETURN - number of characters copied to the output buffer.
*/
size_t GetFirmwareVersion(char *pFWVersion, size_t szBufSize);


/* function GetEstbMac - gets the eSTB MAC address of the device.
 
        Usage: size_t GetEstbMac <char *pEstbMac> <size_t szBufSize>
 
            pEstbMac - pointer to a char buffer to store the output string.

            szBufSize - the size of the character buffer in argument 1.

            RETURN - number of characters copied to the output buffer.
*/
size_t GetEstbMac(char *pEstbMac, size_t szBufSize);


/* function GetRemoteInfo - gets the remote info of the device.
 
        Usage: size_t GetRemoteInfo <char *pRemoteInfo> <size_t szBufSize>
 
            pRemoteInfo - pointer to a char buffer to store the output string.

            szBufSize - the size of the character buffer in argument 1.

            RETURN - number of characters copied to the output buffer.
*/
size_t GetRemoteInfo(char *pRemoteInfo, size_t szBufSize);

/* function GetRemoteVers - gets the peripheral versions of the device.
        (this is identical to GetRemoteInfo except there is no prefix to the string)
 
        Usage: size_t GetRemoteVers <char *pRemoteVers > <size_t szBufSize>
 
            pRemoteVers  - pointer to a char buffer to store the output string.

            szBufSize - the size of the character buffer in argument 1.

            RETURN - number of characters copied to the output buffer.
*/
size_t GetRemoteVers(char *pRemoteVers , size_t szBufSize);

/* function GetRdmManifestVersion - gets the remote info of the device.
 
        Usage: size_t GetRdmManifestVersion <char *pRdmManifestVersion> <size_t szBufSize>
 
            pRdmManifestVersion - pointer to a char buffer to store the output string.

            szBufSize - the size of the character buffer in argument 1.

            RETURN - number of characters copied to the output buffer.
*/
size_t GetRdmManifestVersion(char *pRdmManifestVersion, size_t szBufSize);


/* function GetTR181Url - gets a specific URL from tr181 associated with code downloads.
 
        Usage: size_t GetTR181Url <TR181URL eURL> <char *pRemoteInfo> <size_t szBufSize>
 
            eURL - the specific URL to query tr181 for.
 
            pUrlOut - pointer to a char buffer to store the output string.

            szBufSize - the size of the character buffer in argument 1.

            RETURN - number of characters copied to the output buffer.
*/
size_t GetTR181Url(TR181URL eURL, char *pUrlOut, size_t szBufSize);


/* function GetServURL - gets the correct XCONF URL based upon device configuration.
 
        Usage: size_t GetServURL <char *pServURL> <size_t szBufSize>
 
            pServURL - pointer to a char buffer to store the output string.

            szBufSize - the size of the character buffer in argument 1.

            RETURN - number of characters copied to the output buffer.
*/
size_t GetServURL(char *pServURL, size_t szBufSize);

/* function GetFileContents - gets the contents of a file into a dynamically allocated buffer.
 
        Usage: size_t GetFileContents <char **pOut> <char *pFileName>
 
            pOut - the address of a char pointer (char **) where the dynamically allocated
                    character buffer will be located.

            pFileName - the name of the file to read.

            RETURN - number of characters copied to the output buffer.
 
            Notes - GetFileContents uses malloc to allocate the the buffer where the string is stored.
                    The caller must use free(*pOut) when done using the buffer to avoid memory leaks.
*/
size_t GetFileContents(char **pOut, char *pFileName);

/* function GetLabsignedValue - gets the LABSIGNED_ENABLED value from /etc/device.properties.

        Usage: bool GetLabsignedValue <char> *pBuf, <size_t> szBufSize

            pBuf - pointer to a char buffer to store the output string.

            szBufSize - the size of the character buffer in argument 1.

            RETURN - if firmware version has LABSIGNED and LABSIGNED_ENABLE is true, then TRUE shall be returned. Else, false.
*/
bool GetLabsignedValue(char *pBuf, size_t szBufSize);

bool Debug_Services_Enabled(bool labSigned, BUILDTYPE eBuildType, bool dbgServices, const char* deviceType);
#endif
