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

#include "json_parse.h"
#include "rdkv_cdl_log_wrapper.h"
#include "device_api.h"
#include "deviceutils.h"
#include "device_status_helper.h"
#include "../rfcInterface/rfcinterface.h"

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

/* function GetTimezone - returns the timezone for the device. 
        Usage: size_t GetTimezone <char *pTimezone> <size_t szBufSize>
 
            pTimezone - pointer to a char buffer to store the output string.

            szBufSize - the size of the character buffer in argument 1.

            RETURN - number of characters copied to the output buffer.
*/
size_t GetTimezone( char *pTimezone, const char *cpuArch, size_t szBufSize )
{

    int ret;
    FILE *fp;
    char *pTmp;
    size_t i = 0;
    char buf[256];
    char *timezonefile;
    char *defaultimezone = "Universal";
    char device_name[32];
    char timeZone_offset_map[50];
    char *zoneValue = NULL;

    if( pTimezone != NULL )
    {
        *pTimezone = 0;
        *timeZone_offset_map = 0;

        ret = getDevicePropertyData("DEVICE_NAME", device_name, sizeof(device_name));
        if (ret == UTILS_SUCCESS)
        {
            if (0 == (strncmp(device_name, "PLATCO", 6)))
            {
                if((fp = fopen( TIMEZONE_DST_FILE, "r" )) != NULL )
                {
                    SWLOG_INFO("%s: Reading Timezone value from %s file...\n", __FUNCTION__, TIMEZONE_DST_FILE );
                    if (fgets( buf, sizeof(buf), fp ) != NULL)      // only read first line in file, timezone should be there
                    {
                        i = stripinvalidchar( buf, sizeof(buf) );
                        SWLOG_INFO("%s: Device TimeZone:%s\n", __FUNCTION__, buf );
                    }
                    fclose( fp );
                }

                if( i == 0 )    // either TIMEZONE_DST_FILE non-existent or empty, set a default in pTimezone
                {
                    SWLOG_INFO("%s: %s is empty or non-existent, default timezone America/New_York applied\n",__FUNCTION__, TIMEZONE_DST_FILE);
                    snprintf( buf, sizeof(buf), "America/New_York");
                }

                if((fp = fopen( TIMEZONE_OFFSET_MAP, "r" )) != NULL )
                {
                    while ( fgets(timeZone_offset_map, sizeof(timeZone_offset_map), fp ) != NULL )
                    {
                        if( strstr( timeZone_offset_map, buf ) != NULL )
                        {
                            zoneValue = strtok(timeZone_offset_map, ":");
                            if( zoneValue != NULL )  // there's more after ':'
                            {
                                zoneValue = strtok(NULL, ":");
                            }
                            break; //match found, breaks the while loop
                        }
                    }
                    fclose( fp );
                }

                if( zoneValue != NULL )
                {
                    i = snprintf( pTimezone, szBufSize, "%s", zoneValue );
                }
                else
                {
                    i = snprintf( pTimezone, szBufSize, "US/Eastern" );
                    SWLOG_INFO("%s: Given TimeZone not supported by XConf - default timezone US/Eastern is applied\n", __FUNCTION__);
                }
                SWLOG_INFO("%s: TimeZone Information after mapping : pTimezone = %s\n", __FUNCTION__, pTimezone );
            }
            else
            {
                if( cpuArch != NULL && (0 == (strncmp( cpuArch, "x86", 3 ))) )
                {
                    timezonefile = OUTPUT_JSON_FILE_X86; //For cpu arch x86 file path is /tmp
                }
                else
                {
                    timezonefile = OUTPUT_JSON_FILE;      //File path is /opt
                }

                if( (fp = fopen( timezonefile, "r" )) != NULL )
                {
                    SWLOG_INFO("%s: Reading Timezone value from %s file...\n", __FUNCTION__, timezonefile );
                    while( fgets( buf, sizeof(buf), fp ) != NULL )
                    {
                        if( (pTmp = strstr( buf, "timezone" )) != NULL )
                        {
                            while( *pTmp && *pTmp != ':' )  // should be left pointing to ':' at end of while
                            {
                                ++pTmp;
                            }

                            while( !isalnum( *pTmp ) )  // at end of while we should be pointing to first alphanumeric char after ':', this is timezone
                            {
                                ++pTmp;
                            }
                            i = snprintf( pTimezone, szBufSize, "%s", pTmp );
                            i = stripinvalidchar( pTimezone, i );
                            pTmp = pTimezone;
                            i = 0;
                            while( *pTmp != '"' && *pTmp )     // see if we have an end quote
                            {
                                ++i;    // recount chars for safety
                                ++pTmp;
                            }
                            *pTmp = 0;                  // either we're pointing to the end " character or a 0
                            SWLOG_INFO("%s: Got timezone using %s successfully, value:%s\n", __FUNCTION__, timezonefile, pTimezone );
                            break;
                        }
                    }
                    fclose( fp );
                }

                if( !i && (fp = fopen( TIMEZONE_DST_FILE, "r" )) != NULL )    // if we didn't find it above, try default
                {
                    SWLOG_INFO("%s: Timezone value from output.json is empty, Reading from %s file...\n", __FUNCTION__, TIMEZONE_DST_FILE );
                    if (fgets( buf, sizeof(buf), fp ) != NULL)              // only read first line
                    {
                        i = snprintf( pTimezone, szBufSize, "%s", buf );
                        i = stripinvalidchar( pTimezone, i );
                        SWLOG_INFO("%s: Got timezone using %s successfully, value:%s\n", __FUNCTION__, TIMEZONE_DST_FILE, pTimezone );
                    }
                    fclose( fp );
                }

                if ( !i )
                {
                    i = snprintf( pTimezone, szBufSize, "%s", defaultimezone );
                    SWLOG_INFO("%s: Timezone files %s and %s not found, proceeding with default timezone=%s\n", __FUNCTION__, timezonefile, TIMEZONE_DST_FILE, pTimezone);
                }
            }
        }
        else
        {
            SWLOG_ERROR("%s: getDevicePropertyData() for device_name fail\n", __FUNCTION__);
        }
    }
    else
    {
        SWLOG_ERROR("%s: Error, input argument NULL\n", __FUNCTION__);
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

/* function GetUTCTime - gets a formatted UTC device time. Example;
    Tue Jul 12 21:56:06 UTC 2022 
        Usage: size_t GetUTCTime <char *pUTCTime> <size_t szBufSize>
 
            pUTCTime - pointer to a char buffer to store the output string.

            szBufSize - the size of the character buffer in argument 1.

            RETURN - number of characters copied to the output buffer.
*/
size_t GetUTCTime( char *pUTCTime, size_t szBufSize )
{
    struct tm gmttime;
    time_t seconds;
    size_t i = 0;

    if( pUTCTime != NULL )
    {
        time( &seconds );
        gmtime_r( &seconds, &gmttime );
        gmttime.tm_isdst = 0;           // UTC doesn't know about DST, perhaps unnecessary but be safe
        i = strftime( pUTCTime, szBufSize, "%a %b %d %X UTC %Y", &gmttime );
        if( !i )    // buffer wasn't big enough for strftime call above
        {
            *pUTCTime = 0;
        }
    }
    else
    {
        SWLOG_ERROR( "GetUTCTime: Error, input argument NULL\n" );
    }
    return i;
}

/* function GetCapabilities - gets the device capabilities.
 
        Usage: size_t GetCapabilities <char *pCapabilities> <size_t szBufSize>
 
            pCapabilities - pointer to a char buffer to store the output string.

            szBufSize - the size of the character buffer in argument 1.

            RETURN - number of characters copied to the output buffer.
*/
size_t GetCapabilities( char *pCapabilities, size_t szBufSize )
{
    size_t i = 0;

    if( pCapabilities != NULL )
    {
        i = snprintf( pCapabilities, szBufSize, "%s", DEVICE_CAPABILITIES );
    }
    else
    {
        SWLOG_ERROR( "GetCapabilities: Error, input argument NULL\n" );
    }
    return i;
}

/* function GetPartnerId - gets the partner ID of the device.
 
        Usage: size_t GetPartnerId <char *pPartnerId> <size_t szBufSize>
 
            pPartnerId - pointer to a char buffer to store the output string.

            szBufSize - the size of the character buffer in argument 1.

            RETURN - number of characters copied to the output buffer.
*/
size_t GetPartnerId( char *pPartnerId, size_t szBufSize )
{
    char *pTmp;
    FILE *fp;
    size_t i = 0;
    char buf[150];
    char whoami[8];
    int ret = -1;

    if( pPartnerId != NULL )
    {
        *pPartnerId = 0;
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
	if (((0 == (strncmp(whoami, "true", 4))) && (fp = fopen( BOOTSTRAP_FILE, "r" )) != NULL))
        {
            while( fgets( buf, sizeof(buf), fp ) != NULL )
            {
                if( (pTmp = strstr( buf, "X_RDKCENTRAL-COM_RFC.Bootstrap.PartnerName" )) != NULL )
                {
                    while( *pTmp && *pTmp++ != '=' )
                    {
                        ;
                    }
                    snprintf( pPartnerId, szBufSize, "%s", pTmp );
		    break;
		}
	    }
	    fclose(fp);
        }
	else if( (fp = fopen( PARTNER_ID_FILE, "r" )) != NULL )
        {
            fgets( pPartnerId, szBufSize, fp );
            fclose( fp );
        }
        else if( (fp = fopen( BOOTSTRAP_FILE, "r" )) != NULL )
        {
            while( fgets( buf, sizeof(buf), fp ) != NULL )
            {
                if( (pTmp = strstr( buf, "X_RDKCENTRAL-COM_Syndication.PartnerId" )) != NULL )
                {
                    while( *pTmp && *pTmp++ != '=' )
                    {
                        ;
                    }
                    snprintf( pPartnerId, szBufSize, "%s", pTmp );
		    break;
                }
            }
            fclose( fp );
        }
        else
        {
            // TODO: need to check
            // if [ "$DEVICE_NAME" = "PLATCO" ]; then
            // defaultPartnerId="xglobal"
            // else
            // defaultPartnerId="comcast"
            // fi
            snprintf( pPartnerId, szBufSize, "comcast" );
        }
        i = stripinvalidchar( pPartnerId, szBufSize );      // remove newline etc.
    }
    else
    {
        SWLOG_ERROR( "GetPartnerId: Error, input argument NULL\n" );
    }
    return i;
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

        if( MemDLAlloc( &DwnLoc, DEFAULT_DL_ALLOC ) == 0 )
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

/* function GetMFRName - gets the  manufacturer name of the device.
        Usage: size_t GetMFRName <char *pMFRName> <size_t szBufSize>
            pMFRName - pointer to a char buffer to store the output string.

            szBufSize - the size of the character buffer in argument 1.

            RETURN - number of characters copied to the output buffer.
*/
size_t GetMFRName( char *pMFRName, size_t szBufSize )
{
    size_t i = 0;
    FILE *fp;
    char buf[150];
    if( pMFRName != NULL )
    {
        *pMFRName = 0;
	if( (fp = fopen( "/tmp/.manufacturer", "r" )) != NULL )
	{
	  while ( fgets ( buf, sizeof(buf), fp ) != NULL) {
            if ( buf[0] != '\n' && buf[0] != '\0') {
		  for (size_t t = 0; buf[t] != '\0' && i < szBufSize - 1; t++) {
                    pMFRName[i++] = buf[t];
            }
            pMFRName[i] = '\0';
            break;
            }
	  }
            fclose( fp );
	}
        else
        {
            SWLOG_ERROR( "GetMFRName: Cannot open %s for reading\n", "/tmp/.manufacturer" );
        }
    }
    else
    {
        SWLOG_ERROR( "GetMFRName: Error, input argument NULL\n" );
    }
    return i;

}
/* function GetModelNum - gets the model number of the device.
 
        Usage: size_t GetModelNum <char *pModelNum> <size_t szBufSize>
 
            pModelNum - pointer to a char buffer to store the output string.

            szBufSize - the size of the character buffer in argument 1.

            RETURN - number of characters copied to the output buffer.
*/
size_t GetModelNum( char *pModelNum, size_t szBufSize )
{
    size_t i = 0;

#ifdef GETMODEL_IN_SCRIPT
    if( pModelNum != NULL )
    {
        i = RunCommand( eGetModelNum, NULL, pModelNum, szBufSize );
        SWLOG_INFO("GetModelNum: model number:%s and ret=%d\n",pModelNum, i);
    }
    else
    {
        SWLOG_INFO( "GetModelNum: Error, input argument NULL\n" );
    }
#else
/* DELIA-60757: Below code need to be implemented for both mdeiaclient and non mediclient device. */
    FILE *fp;
    char *pTmp;
    char buf[150];

    if( pModelNum != NULL )
    {
        *pModelNum = 0;
	if( (fp = fopen( "/tmp/.model_number", "r" )) != NULL )
        {
	  while ( fgets ( buf, sizeof(buf), fp ) != NULL) {
             if ( buf[0] != '\n' && buf[0] != '\0') {
		  for (size_t t = 0; buf[t] != '\0' && i < szBufSize - 1; t++) {
                    pModelNum[i++] = buf[t];
            }
            pModelNum[i] = '\0';
            break;
            }
	  }
            fclose( fp );
        }
	else if( (fp = fopen( DEVICE_PROPERTIES_FILE, "r" )) != NULL )
        {
            while( fgets( buf, sizeof(buf), fp ) != NULL )
            {
                pTmp = strstr( buf, "MODEL_NUM=" );
                if( pTmp && pTmp == buf )   // if match found and match is first character on line
                {
//                    SWLOG_INFO("GetModelNum: Found %s\n", buf );    // TODO: remove, for debugging only
                    pTmp = strchr( pTmp, '=' );
                    //CID:330354-Dereference null return value
                    if(pTmp != NULL)
                    {
                    ++pTmp;
                    i = snprintf( pModelNum, szBufSize, "%s", pTmp );
                    i = stripinvalidchar( pModelNum, i );
                    }
                }
            }
            fclose( fp );
        }
        else
        {
            SWLOG_ERROR( "GetModelNum: Cannot open %s for reading\n", DEVICE_PROPERTIES_FILE );
        }
    }
    else
    {
        SWLOG_ERROR( "GetModelNum: Error, input argument NULL\n" );
    }
#endif
    return i;
}

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
size_t GetBuildType( char *pBuildType, size_t szBufSize, BUILDTYPE *peBuildTypeOut )
{
    FILE *fp;
    char *pTmp, *pOut = NULL;
    size_t i = 0;
    BUILDTYPE eBuildType = eUNKNOWN;
    char buf[150];

    if( pBuildType != NULL )
    {
        *pBuildType = 0;
        if( (fp = fopen( DEVICE_PROPERTIES_FILE, "r" )) != NULL )
        {
            while( fgets( buf, sizeof(buf), fp ) != NULL )
            {
                pTmp = strstr( buf, "BUILD_TYPE=" );
                if( pTmp == buf )   // if match found (!= NULL is implied since buf address cannot be NULL) and match is first character on line
                {
                    pTmp += 11;     // point to char after '='
                    i = snprintf( pBuildType, szBufSize, "%s", pTmp );
                    i = stripinvalidchar( pBuildType, i );
                    pTmp = pBuildType;
                    while( *pTmp )
                    {
                        *pTmp = tolower( *pTmp );
                        ++pTmp;
                    }
                }
            }
            fclose( fp );
        }
        if( *pBuildType == 0 )
        {
            GetFirmwareVersion( buf, sizeof(buf) );
            pTmp = buf;
            while( *pTmp )
            {
                *pTmp = tolower( *pTmp );
                ++pTmp;
            }
        }
        else
        {
            pTmp = pBuildType;
        }

        // run the following series of checks to set eBuildType
        // pBuildType must also be set if the value was found with GetFirmwareVersion()
        if( strstr( pTmp, "vbn" ) != NULL )
        {
            pOut = "vbn";
            eBuildType = eVBN;
        }
        else if( strstr( pTmp, "prod" ) != NULL )
        {
            pOut = "prod";
            eBuildType = ePROD;
        }
        else if( strstr( pTmp, "qa" ) != NULL )
        {
            pOut = "qa";
            eBuildType = eQA;
        }
        else if( strstr( pTmp, "dev" ) != NULL )
        {
            pOut = "dev";
            eBuildType = eDEV;
        }

        if( *pBuildType == 0 && pOut != NULL )
        {
            i = snprintf( pBuildType, szBufSize, "%s", pOut );
        }
    }
    else
    {
        SWLOG_ERROR( "GetBuildType: Error, input argument NULL\n" );
    }
    if( peBuildTypeOut != NULL )
    {
        *peBuildTypeOut = eBuildType;
    }
    return i;
}

/* function GetFirmwareVersion - gets the firmware version of the device.
 
        Usage: size_t GetFirmwareVersion <char *pFWVersion> <size_t szBufSize>
 
            pFWVersion - pointer to a char buffer to store the output string.

            szBufSize - the size of the character buffer in argument 1.

            RETURN - number of characters copied to the output buffer.
*/
size_t GetFirmwareVersion( char *pFWVersion, size_t szBufSize )
{
    FILE *fp;
    size_t i = 0;
    char *pTmp;
    char buf[150];

    if( pFWVersion != NULL )
    {
        *pFWVersion = 0;
        if( (fp = fopen( VERSION_FILE, "r" )) != NULL )
        {
            pTmp = NULL;
            while( fgets( buf, sizeof(buf), fp ) != NULL )
            {
                if( (pTmp = strstr( buf, "imagename:" )) != NULL )
                {
                    while( *pTmp++ != ':' )
                    {
                        ;
                    }
                    break;
                }
            }
            fclose( fp );
            if( pTmp )
            {
                i = snprintf( pFWVersion, szBufSize, "%s", pTmp );
                i = stripinvalidchar( pFWVersion, i );
            }
        }
    }
    else
    {
        SWLOG_INFO( "GetFirmwareVersion: Error, input argument NULL\n" );
    }
    return i;
}

/* function GetEstbMac - gets the eSTB MAC address of the device.
 
        Usage: size_t GetEstbMac <char *pEstbMac> <size_t szBufSize>
 
            pEstbMac - pointer to a char buffer to store the output string.

            szBufSize - the size of the character buffer in argument 1.

            RETURN - number of characters copied to the output buffer.
*/
size_t GetEstbMac( char *pEstbMac, size_t szBufSize )
{
    FILE *fp;
    size_t i = 0;
    char estb_interface[8] = {0};
    int ret = -1;
    bool read_from_hwinterface = false; // default value

    if( pEstbMac != NULL )
    {
        *pEstbMac = 0;
        if( (fp = fopen( ESTB_MAC_FILE, "r" )) != NULL )
        {
            fgets( pEstbMac, szBufSize, fp );   // better be a valid string on first line
            fclose( fp );
            i = stripinvalidchar( pEstbMac, szBufSize );
            SWLOG_INFO("GetEstbMac: After reading ESTB_MAC_FILE value=%s\n", pEstbMac);
            /* Below condition if ESTB_MAC_FILE file having empty data and pEstbMac does not have 17 character 
            * including total mac address with : separate */
            if (pEstbMac[0] == '\0' || pEstbMac[0] == '\n' || i != MAC_ADDRESS_LEN)
            {
                SWLOG_INFO("GetEstbMac: ESTB_MAC_FILE file is empty read_from_hwinterface is set to true\n");
                read_from_hwinterface = true;
            }
        }
        else
        {
            read_from_hwinterface = true;//ESTB_MAC_FILE file does not present proceed for reading from interface
            SWLOG_INFO("GetEstbMac: read_from_hwinterface is set to true\n");
        }
        if (read_from_hwinterface == true)
        {
            SWLOG_INFO("GetEstbMac: Reading from hw interface\n");
            ret = getDevicePropertyData("ESTB_INTERFACE", estb_interface, sizeof(estb_interface));
            if (ret == UTILS_SUCCESS)
            {
                i = GetHwMacAddress(estb_interface, pEstbMac, szBufSize);
                if(i)
                {
                    SWLOG_INFO("GetEstbMac: Hardware address=%s=\n", pEstbMac);
                }
                else
                {
                    /* When there is no hw address available */
                    *pEstbMac = 0;
                    SWLOG_ERROR("GetEstbMac: GetHwMacAddress return fail\n");
                }
            }
            else
            {
                *pEstbMac = 0;
                i = 0;
                SWLOG_ERROR("GetEstbMac: Interface is not part of /etc/device.properties missing\n");
            }
        }
    }
    else
    {
        SWLOG_ERROR( "GetEstbMac: Error, input argument NULL\n" );
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

    if( pServURL != NULL )
    {
        *pServURL = 0;
        GetBuildType( buf, sizeof(buf), &eBuildType );
        if( isInStateRed() )
        {
            if( eBuildType != ePROD )
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
            if( eBuildType != ePROD )
            {
                if( (filePresentCheck( SWUPDATE_CONF ) == RDK_API_SUCCESS) )    // if the file exists
                {
                    len = GetServerUrlFile( pServURL, szBufSize, SWUPDATE_CONF ); // see if swupdate.conf override exists, use it if it's available
                    if( !len )  // then didn't find a valid URL
                    {
                         SWLOG_INFO( "Device configured with an invalid overriden URL!!! Exiting from Image Upgrade process..!\n" );
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

/* function GetFileContents - gets the contents of a file into a dynamically allocated buffer.
 
        Usage: size_t GetFileContents <char **pOut> <char *pFileName>
 
            pOut - the address of a char pointer (char **) where the dynamically allocated
                    character buffer will be located.

            pFileName - the name of the file to read.

            RETURN - number of characters copied to the output buffer.
 
            Notes - GetFileContents uses malloc to allocate the the buffer where the string is stored.
                    The caller must use free(*pOut) when done using the buffer to avoid memory leaks.
*/

size_t GetFileContents( char **pOut, char *pFileName )
{
    FILE *fp;
    char *pBuf = NULL;
    char *pPtr;
    size_t len = 0;
    if( pOut != NULL && pFileName != NULL )
    {
        SWLOG_INFO( "GetFileContents: pFileName = %s\n", pFileName );
        if( (len=(size_t)getFileSize( pFileName )) != -1 )
        {
            SWLOG_INFO( "GetFileContents: file len = %zu\n", len );
            if( (fp=fopen( pFileName, "r" )) != NULL )
            {
                ++len;  // room for NULL, included in return value
                pBuf = malloc( len );
                if( pBuf != NULL )
                {
                    pPtr = pBuf;
                    while( ((*pPtr=(char)fgetc( fp )) != EOF) && !feof( fp ) )
                    {
                        ++pPtr;
                    }
                    *pPtr = 0;
                }
                else
                {
                    len = 0;
                }
                fclose( fp );
            }
        }
        *pOut = pBuf;
    }
    else
    {
        SWLOG_ERROR( "GetFileContents: Error, input argument NULL\n" );
    }
    return len;
}


