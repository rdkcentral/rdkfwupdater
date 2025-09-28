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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <time.h>

#ifndef GTEST_ENABLE
#include "urlHelper.h"
#include "rdk_fwdl_utils.h"
#include "system_utils.h"
#endif

#include "rdkv_cdl.h"
#include "json_parse.h"
#include "rdkv_cdl_log_wrapper.h"
#include "device_api.h"
#include "deviceutils.h"
#include "device_status_helper.h"
#include "rfcinterface.h"

#define MAC_ADDRESS_LEN 17

/* function GetServerUrlFile - scans a file for a URL. 
        Usage: size_t GetServerUrlFile <char *pServUrl> <size_t szBufSize> <char *pFileName>
 
            pServUrl - pointer to a char buffer to store the output string..

            szBufSize - the size of the character buffer in argument 1.

            pFileName - a character pointer to a filename to scan.

            RETURN - number of characters copied to the output buffer.
*/
size_t GetServerUrlFile( char *pServUrl, size_t szBufSize, char *pFileName )
{
    FILE *fp;
    char *pHttp, *pLb;
    size_t i = 0;
    char buf[256];

    if( pServUrl != NULL && pFileName != NULL )
    {
        *pServUrl = 0;
        if( (fp = fopen( pFileName, "r" )) != NULL )
        {
            pHttp = NULL;
            while( (pHttp == NULL) && (fgets( buf, sizeof(buf), fp ) != NULL) )
            {
                if( (pHttp = strstr( buf, "https://" )) != NULL )
                {
                    if( (pLb = strchr( buf, (int)'#' )) != NULL )
                    {
                        if( pLb <= pHttp )
                        {
                            pHttp = NULL;
                            continue;       // '#' is before http or at beginning means commented line
                        }
                        else
                        {
                            *pLb = 0;       // otherwise NULL terminate the string
                        }
                    }
                    pLb = pHttp + 8;    // reuse pLb for parsing, pLb should point to first character after https://
                    while( *pLb )   // convert non-alpha numerics (but not '.') or whitespace to NULL terminator
                    {
                        if( (!isalnum( *pLb ) && *pLb != '.' && *pLb != '/' && *pLb != '-' && *pLb != '_' && *pLb != ':') || isspace( *pLb ) )
                        {
                            *pLb = 0;   // NULL terminate at end of URL
                            break;
                        }
                        ++pLb;
                    }
                }
            }
            fclose( fp );
            if( pHttp != NULL )
            {
                i = snprintf( pServUrl, szBufSize, "%s", pHttp );
            }
        }
        else
        {
            SWLOG_INFO( "GetServerUrl: %s can't be opened\n", pFileName );
        }
    }
    else
    {
        SWLOG_ERROR( "GetServerUrlFile: Error, input argument NULL\n" );
    }
    return i;
}

/* function GetAdditionalFwVerInfo - returns the PDRI filename plus Remote Info for the device. 
        Usage: size_t GetAdditionalFwVerInfo <char *pAdditionalFwVerInfo> <size_t szBufSize>
 
            pAdditionalFwVerInfo - pointer to a char buffer to store the output string.

            szBufSize - the size of the character buffer in argument 1.

            RETURN - number of characters copied to the output buffer.
*/
size_t GetAdditionalFwVerInfo( char *pAdditionalFwVerInfo, size_t szBufSize )
{
    size_t len = 0;

    if( pAdditionalFwVerInfo != NULL )
    {
        len = GetPDRIFileName( pAdditionalFwVerInfo, szBufSize );
	if( len < szBufSize )
        {
            len += GetRemoteInfo( (pAdditionalFwVerInfo + len), (szBufSize - len) );
        }
    }
    else
    {
        SWLOG_ERROR( "GetAdditionalFwVerInfo: Error, input argument NULL\n" );
    }

    return len;
}

/* function GetPDRIFileName - returns the PDRI for the device. 
        Usage: size_t GetPDRIFileName <char *pPDRIFilename> <size_t szBufSize>
 
            pPDRIFilename - pointer to a char buffer to store the output string.

            szBufSize - the size of the character buffer in argument 1.

            RETURN - number of characters copied to the output buffer.
*/
size_t GetPDRIFileName( char *pPDRIFilename, size_t szBufSize )
{
    char *pTmp;
    size_t len = 0;

    if( pPDRIFilename != NULL )
    {
        len = RunCommand( eMfrUtil, "--PDRIVersion", pPDRIFilename, szBufSize );
        if( len && ((pTmp = strcasestr( pPDRIFilename, "failed" )) == NULL) )   // if "failed" is not found
        {
            SWLOG_INFO( "GetPDRIFileName: PDRI Version = %s\n", pPDRIFilename );
            t2ValNotify("PDRI_Version_split", pPDRIFilename);
        }
        else
        {
            *pPDRIFilename = 0;
            len = 0;
            SWLOG_ERROR( "GetPDRIFileName: PDRI filename retrieving Failed ...\n" );
        }
    }
    else
    {
        SWLOG_ERROR( "GetPDRIFileName: Error, input argument NULL\n" );
    }
    return len;
}


/* function GetInstalledBundles - gets the bundles installed on a device. 
        Usage: size_t GetInstalledBundles <char *pBundles> <size_t szBufSize>
 
            pBundles - pointer to a char buffer to store the output string.

            szBufSize - the size of the character buffer in argument 1.

            RETURN - number of characters copied to the output buffer.
*/

size_t GetInstalledBundles(char *pBundles, size_t szBufSize)
{
    JSON *pJsonTop;
    JSON *pJson;
    char *pJsonStr;
    size_t len;
    size_t szRunningLen = 0;
    metaDataFileList_st *installedBundleListNode = NULL, *tmpNode = NULL;

    if (pBundles != NULL)
    {
        *pBundles = 0;
        installedBundleListNode = getInstalledBundleFileList();

        while (installedBundleListNode != NULL)
        {
            SWLOG_INFO("GetInstalledBundles: calling GetJson with arg = %s\n", installedBundleListNode->fileName);
            pJsonStr = GetJson(installedBundleListNode->fileName);
            if (pJsonStr != NULL)
            {
                SWLOG_INFO("GetInstalledBundles: pJsonStr = %s\n", pJsonStr);
                pJsonTop = ParseJsonStr(pJsonStr);
                SWLOG_INFO("GetInstalledBundles: ParseJsonStr returned =%s\n", pJsonStr);
                pJson = pJsonTop;
                if (pJsonTop != NULL)
                {
                    while (pJson != NULL)
                    {
                        len = GetJsonVal(pJson, "name", installedBundleListNode->fileName, sizeof(installedBundleListNode->fileName));
                        if (len)
                        {
                            if (szRunningLen)
                            {
                                *(pBundles + szRunningLen) = ',';
                                ++szRunningLen;
                            }
                            len = snprintf(pBundles + szRunningLen, szBufSize - szRunningLen, "%s:", installedBundleListNode->fileName);
                            szRunningLen += len;
                            len = GetJsonVal(pJson, "version", installedBundleListNode->fileName, sizeof(installedBundleListNode->fileName));
                            len = snprintf(pBundles + szRunningLen, szBufSize - szRunningLen, "%s", installedBundleListNode->fileName);
                            SWLOG_INFO("Updated Bundles = %s\n", pBundles);
                            szRunningLen += len;
                        }
                        pJson = pJson->next; // need "next" getter function
                    }
                    FreeJson(pJsonTop);
                }
                free(pJsonStr);
            }
            tmpNode = installedBundleListNode;
            installedBundleListNode = installedBundleListNode->next;
            free(tmpNode);
        }
        SWLOG_INFO("GetInstalledBundles: pBundles = %s\n", pBundles);
        SWLOG_INFO("GetInstalledBundles: szRunningLen = %zu\n", szRunningLen);
    }

    return szRunningLen;
}

/* function GetOsClass - gets the OsClass of the device.
 
        Usage: size_t GetOsClass( char *pOsClass, size_t szBufSize )
 
            pOsClass - pointer to a char buffer to store the output string.

            szBufSize - the size of the character buffer in argument 1.

            RETURN - number of characters copied to the output buffer.
*/
size_t GetOsClass( char *pOsClass, size_t szBufSize )
{
    size_t i = 0;
    char whoami[8];
    int ret = -1;
    if( pOsClass != NULL )
    {
	whoami[0] = 0;
	ret = getDevicePropertyData("WHOAMI_SUPPORT", whoami, sizeof(whoami));
        if (ret == UTILS_SUCCESS) 
	{
            SWLOG_INFO("whoami is = %s\n", whoami);
        } 
	else 
	{
            SWLOG_ERROR("%s: getDevicePropertyData() for whoami fail\n", __FUNCTION__);
        }
        *pOsClass = 0;
	if (0 == (strncmp(whoami, "true", 4)))
	{
            i = read_RFCProperty( "OsClass", RFC_OS_CLASS, pOsClass, szBufSize );
            if( i == READ_RFC_FAILURE )
            {
                SWLOG_ERROR( "GetOsClass: read_RFCProperty() failed Status %d\n", i );
                i = snprintf( pOsClass, szBufSize, "Not Available" );
            }
            else
            {
                i = strnlen( pOsClass, szBufSize );
            }
	}
	else
	{
            SWLOG_INFO( "GetOsClass: whoami is not enable Status\n");
            i = snprintf( pOsClass, szBufSize, "Not Available" );
	}
    }
    else
    {
        SWLOG_ERROR( "GetOsClass: Error, input argument NULL\n" );
    }
    return i;
}

/* function GetSerialNum - gets the serial number of the device.
 
        Usage: size_t GetSerialNum <char *pSerialNum> <size_t szBufSize>
 
            pSerialNum - pointer to a char buffer to store the output string.

            szBufSize - the size of the character buffer in argument 1.

            RETURN - number of characters copied to the output buffer.
*/
size_t GetSerialNum( char *pSerialNum, size_t szBufSize )
{
    size_t i = 0;

    if( pSerialNum != NULL )
    {
        *pSerialNum = 0;
        i = read_RFCProperty( "SerialNumber", RFC_SERIALNUM, pSerialNum, szBufSize );
        if( i == READ_RFC_FAILURE )
        {
            SWLOG_ERROR( "GetSerialNum: read_RFCProperty() failed Status %d\n", i );
            i = snprintf( pSerialNum, szBufSize, "Not Available" );
        }
        else
        {
            i = strnlen( pSerialNum, szBufSize );
        }
    }
    else
    {
        SWLOG_ERROR( "GetSerialNum: Error, input argument NULL\n" );
    }
    return i;
}

/* function GetMigrationReady - gets the migration readiness status.
         Usage: size_t GetMigrationReady <char *pMRComponents> <size_t szBufSize>
             pMRComponents - pointer to a char buffer to store the output string.
             szBufSize - the size of the character buffer in argument 1.
             RETURN - number of characters copied to the output buffer.
 */
 size_t GetMigrationReady( char *pMRComponents, size_t szBufSize )
 {
     size_t i = 0;
 
     if( pMRComponents != NULL )
     {
         i = read_RFCProperty( "MigrationReady", MR_ID, pMRComponents, szBufSize );
         if( i == READ_RFC_FAILURE )
         {
             i = 0;
             SWLOG_ERROR( "GetMigrationReady: read_RFCProperty() failed Status %d\n", i );
         }
         else
         {
             i = strnlen( pMRComponents, szBufSize );
         }
     }
     else
     {
         SWLOG_ERROR( "GetMigrationReady: Error, input argument NULL\n" );
     }
     return i;
 }

/* function GetExperience - gets the experience of the device.
 
        Usage: size_t GetExperience <char *pExperience> <size_t szBufSize>
 
            pExperience - pointer to a char buffer to store the output string.

            szBufSize - the size of the character buffer in argument 1.

            RETURN - number of characters copied to the output buffer.
*/
size_t GetExperience( char *pExperience, size_t szBufSize )
{
    DownloadData DwnLoc;
    JSON *pJson;
    JSON *pItem;
    size_t i = 0;
    char post_data[] = "{\"jsonrpc\":\"2.0\",\"id\":\"3\",\"method\":\"org.rdk.AuthService.getExperience\", \"params\":{}}";

    if( pExperience != NULL )
    {
        *pExperience = 0;

        if( allocDowndLoadDataMem( &DwnLoc, DEFAULT_DL_ALLOC ) == 0 )
        {
            getJsonRpc( post_data, &DwnLoc );

            // expecting a json string returned in this format
            // {"jsonrpc":"2.0","id":3,"result":{"experience":"X1","success":true}}

            pJson = ParseJsonStr( (char *)DwnLoc.pvOut );

            if( pJson != NULL )
            {
                pItem = GetJsonItem( pJson, "result" );
                if( pItem != NULL )
                {
                    i = GetJsonVal( pItem, "experience", pExperience, szBufSize );
                    if( !*pExperience )       // we got nothing back, "X1" is default
                    {
                        *pExperience = 'X';
                        *(pExperience + 1) = '1';
                        *(pExperience + 2) = 0;
                    }
                }
                FreeJson( pJson );
            }

            if( DwnLoc.pvOut != NULL )
            {
                free( DwnLoc.pvOut );
            }
        }
    }
    else
    {
        SWLOG_ERROR( "GetExperience: Error, input argument NULL\n" );
    }
    return i;
}

/* function GetAccountID - gets the account ID of the device.
 
        Usage: size_t GetAccountID <char *pAccountID> <size_t szBufSize>
 
            pAccountID - pointer to a char buffer to store the output string.

            szBufSize - the size of the character buffer in argument 1.

            RETURN - number of characters copied to the output buffer.
*/
size_t GetAccountID( char *pAccountID, size_t szBufSize )
{
    size_t i = 0;

    if( pAccountID != NULL )
    {
        i = read_RFCProperty( "AccountID", RFC_ACCOUNTID, pAccountID, szBufSize );
        if( i == READ_RFC_FAILURE )
        {
            i = snprintf( pAccountID, szBufSize, "Unknown" );
            SWLOG_ERROR( "GetAccountID: read_RFCProperty() failed Status %d\n", i );
        }
        else
        {
            i = strnlen( pAccountID, szBufSize );
//            SWLOG_INFO( "GetAccountID: AccountID = %s\n", pAccountID );
        }
    }
    else
    {
        SWLOG_ERROR( "GetAccountID: Error, input argument NULL\n" );
    }
    return i;
}
/* function GetRemoteInfo - gets the remote info of the device.
 
        Usage: size_t GetRemoteInfo <char *pRemoteInfo> <size_t szBufSize>
 
            pRemoteInfo - pointer to a char buffer to store the output string.

            szBufSize - the size of the character buffer in argument 1.

            RETURN - number of characters copied to the output buffer.
*/
size_t GetRemoteInfo( char *pRemoteInfo, size_t szBufSize )
{
    size_t len, sztotlen = 0;
    JSON *pJson, *pItem;
    char *pJsonStr;
    size_t szBufRemaining;
    unsigned index, num;

    if( pRemoteInfo != NULL )
    {
        *pRemoteInfo = 0;
        pJsonStr = GetJson( PERIPHERAL_JSON_FILE );
        if( pJsonStr != NULL )
        {
            szBufRemaining = szBufSize;
            pJson = ParseJsonStr( pJsonStr );
            if( pJson != NULL )
            {
                if( IsJsonArray( pJson ) )
                {
                    num = GetJsonArraySize( pJson );
                    for( index = 0; index < num; index++ )
                    {
                        pItem = GetJsonArrayItem( pJson, index );
                        if( pItem != NULL )
                        {
			    len = BuildRemoteInfo( pItem, pRemoteInfo + sztotlen, szBufRemaining, true );
                            sztotlen += len;
                            if( len <= szBufRemaining ) // make sure not to roll value over
                            {
                                szBufRemaining -= len;
                            }
                            if( szBufRemaining <= 1 )    // if it's 1 then buf is full since NULL isn't counted
                            {
                                SWLOG_INFO( "%s: WARNING, buffer is full and will be truncated, sztotlen=%zu\n", __FUNCTION__, sztotlen );
                                break;
                            }
                        }
                    }
                }
                else
                {
                    sztotlen = BuildRemoteInfo( pJson, pRemoteInfo, szBufSize, true );
                }
                FreeJson( pJson );
            }
            free( pJsonStr );
        }
    }
    else
    {
        SWLOG_ERROR( "GetRemoteInfo: Error, input argument NULL\n" );
    }
    SWLOG_INFO( "%s: returning sztotlen=%zu\n", __FUNCTION__, sztotlen );
    return sztotlen;
}

/* function GetRemoteVers - gets the peripheral versions of the device.
        (this is identical to GetRemoteInfo except there is no prefix to the string)
 
        Usage: size_t GetRemoteVers <char *pRemoteVers > <size_t szBufSize>
 
            pRemoteVers  - pointer to a char buffer to store the output string.

            szBufSize - the size of the character buffer in argument 1.

            RETURN - number of characters copied to the output buffer.
*/
size_t GetRemoteVers( char *pRemoteVers , size_t szBufSize )
{
    size_t len = 0;
    JSON *pJson, *pItem;
    char *pJsonStr;
    unsigned index, num;

    if( pRemoteVers  != NULL )
    {
        *pRemoteVers  = 0;
        pJsonStr = GetJson( PERIPHERAL_JSON_FILE );
        if( pJsonStr != NULL )
        {
            pJson = ParseJsonStr( pJsonStr );
            if( pJson != NULL )
            {
                if( IsJsonArray( pJson ) )
                {
                    num = GetJsonArraySize( pJson );
                    for( index = 0; index < num; index++ )
                    {
                        pItem = GetJsonArrayItem( pJson, index );
                        if( pItem != NULL )
                        {
                            len += BuildRemoteInfo( pItem, pRemoteVers  + len, szBufSize - len, false );
                        }
                    }
                }
                else
                {
                    len = BuildRemoteInfo( pJson, pRemoteVers , szBufSize, false );
                }
                FreeJson( pJson );
            }
            free( pJsonStr );
        }
    }
    else
    {
        SWLOG_INFO( "GetRemoteVers: Error, input argument NULL\n" );
    }

    return len;
}

/* function GetRdmManifestVersion - gets the remote info of the device.
 
        Usage: size_t GetRdmManifestVersion <char *pRdmManifestVersion> <size_t szBufSize>
 
            pRdmManifestVersion - pointer to a char buffer to store the output string.

            szBufSize - the size of the character buffer in argument 1.

            RETURN - number of characters copied to the output buffer.
*/
size_t GetRdmManifestVersion( char *pRdmManifestVersion, size_t szBufSize )
{
    size_t len = 0;

#ifdef GETRDMMANIFESTVERSION_IN_SCRIPT
    if( pRdmManifestVersion != NULL )
    {
	*pRdmManifestVersion = 0;
        len = RunCommand( eGetInstalledRdmManifestVersion, NULL, pRdmManifestVersion, szBufSize );
    }
    else
    {
        SWLOG_INFO( "GetRdmManifestVersion: Error, input argument NULL\n" );
    }
#else
    // TODO: Add C implemementation
#endif
    return len;
}

/* function GetTR181Url - gets a specific URL from tr181 associated with code downloads.
 
        Usage: size_t GetTR181Url <TR181URL eURL> <char *pUrlOut> <size_t szBufSize>
 
            eURL - the specific URL to query tr181 for.
 
            pUrlOut - pointer to a char buffer to store the output string.

            szBufSize - the size of the character buffer in argument 1.

            RETURN - number of characters copied to the output buffer.
*/
size_t GetTR181Url( TR181URL eURL, char *pUrlOut, size_t szBufSize )
{
    char *pDefaultURL = NULL;
    char *pURLName = NULL;
    char *pURLType = NULL;
    size_t len = 0;
    int status;

    if( pUrlOut != NULL )
    {
        switch( eURL )
        {
           case eRecovery :
               pDefaultURL = RECOVERYDEFAULT;       // NO_URL = NULL
               pURLName = RFC_RECOVERYURL;
               pURLType = RECOVERYURL;
               break;

           case eAutoExclude :
               pDefaultURL = AUTOEXCLUDEDEFAULT;    // NO_URL = NULL
               pURLName = RFC_AUTOEXCLUDEURL;
               pURLType = AUTOEXCLUDEURL;
               break;

           case eBootstrap :
               pDefaultURL = BOOTSTRAPDEFAULT;      // NO_URL = NULL
               pURLName = RFC_BOOTSTRAPURL;
               pURLType = BOOTSTRAPURL;
               break;

           case eDevXconf :
               pDefaultURL = DEVXCONFDEFAULT;
               pURLName = RFC_DEVXCONFURL;
               pURLType = DEVXCONFURL;
               break;

           case eCIXconf :
               pDefaultURL = CIXCONFDEFAULT;
               pURLName = RFC_CIXCONFURL;
               pURLType = CIXCONFURL;
               break;

           case eXconf :
               pDefaultURL = XCONFDEFAULT;
               pURLName = RFC_XCONFURL;
               pURLType = XCONFURL;
               break;

           case eDac15 :
               pDefaultURL = DAC15DEFAULT;
               pURLName = RFC_DAC15URL;
               pURLType = DAC15URL;
               break;

           default :
               SWLOG_ERROR( "GetTR181Url: Unknown URL type %d\n", eURL );
               break;

        }

        status = read_RFCProperty( pURLType, pURLName, pUrlOut, szBufSize );    // read_RFCProperty catches NULL in pURLType and pURLName
        if( status == READ_RFC_FAILURE )
        {
            if( pDefaultURL != NULL )
            {
                len = snprintf( pUrlOut, szBufSize, "%s", pDefaultURL );    // default value
                SWLOG_INFO( "GetTR181Url: RFCProperty not found, defaulting to %s\n", pUrlOut );
            }
            else
            {
                SWLOG_INFO( "GetTR181Url: RFCProperty not found and no default URL provided\n" );
            }
        }
        else
        {
            len = strnlen( pUrlOut, szBufSize );
        }
        SWLOG_INFO( "GetTR181Url: pUrlOut = %s\n", pUrlOut );
    }
    else
    {
        SWLOG_ERROR( "GetTR181Url: Error, input argument NULL\n" );
    }

    return len;
}

/* function GetServURL - gets the correct XCONF URL based upon device configuration.
 
        Usage: size_t GetServURL <char *pServURL> <size_t szBufSize>
 
            pServURL - pointer to a char buffer to store the output string.

            szBufSize - the size of the character buffer in argument 1.

            RETURN - number of characters copied to the output buffer.
*/
size_t GetServURL( char *pServURL, size_t szBufSize )
{
    size_t len = 0;
    BUILDTYPE eBuildType;
    char buf[URL_MAX_LEN];
    bool skip = false;
    bool dbgServices = isDebugServicesEnabled(); //check debug services enabled

    if( pServURL != NULL )
    {
        *pServURL = 0;
        GetBuildType( buf, sizeof(buf), &eBuildType );
        if( isInStateRed() )
        {
            if(( eBuildType != ePROD )  || ( dbgServices == true ))
            {
                len = GetServerUrlFile( pServURL, szBufSize, STATE_RED_CONF );
            }
            if( len == 0 || *pServURL == 0 )
            {
                len = GetTR181Url( eRecovery, pServURL, szBufSize );
            }
        }
        else
        {
            if(( eBuildType != ePROD )  || ( dbgServices == true ))
            {
                if( (filePresentCheck( SWUPDATE_CONF ) == RDK_API_SUCCESS) )    // if the file exists
                {
                    len = GetServerUrlFile( pServURL, szBufSize, SWUPDATE_CONF ); // see if swupdate.conf override exists, use it if it's available
                    if( !len )  // then didn't find a valid URL
                    {
                         SWLOG_INFO( "Device configured with an invalid overriden URL!!! Exiting from Image Upgrade process..!\n" );
                         t2ValNotify("SYST_WARN_UPGD_SKIP", pServURL);
                         skip = true;   // the only time to skip further checks is when not Prod build
                    }
                }
            }
            if( len == 0 && skip == false )
            {
                if( eBuildType != ePROD )
                {
                    len = GetTR181Url( eAutoExclude, pServURL, szBufSize );
                }
                if( *pServURL == 0 )    // still no URL or eBuildType == ePROD
                {
                    *buf = 0;
                    GetTR181Url( eBootstrap, buf, sizeof(buf) );
                    if( *buf != 0 )
                    {
                        len = snprintf( pServURL, szBufSize, "%s/xconf/swu/stb", buf );    // default value
                    }
                    else
                    {
                        if( eBuildType == eQA )
                        {
                            len = GetTR181Url( eDevXconf, pServURL, szBufSize );
                        }
                        else
                        {
                            GetTR181Url( eXconf, buf, sizeof(buf) );
                            len = snprintf( pServURL, szBufSize, "https://%s/xconf/swu/stb/", buf );
                        }
                    }
                }
            }
        }
    }
    else
    {
        SWLOG_ERROR( "GetServURL: Error, input argument NULL\n" );
    }
    return len;
}
