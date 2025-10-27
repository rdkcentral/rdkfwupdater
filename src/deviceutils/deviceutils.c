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
#include <ctype.h>
#include <stdbool.h>

#ifndef GTEST_ENABLE
#include <secure_wrapper.h>
#include "downloadUtil.h"
#endif

#include "rdkv_cdl_log_wrapper.h"
#include "deviceutils.h"
#include "json_parse.h"
#include <dirent.h>

#ifndef GTEST_ENABLE
	#define BUNDLE_METADATA_NVM_PATH    "/media/apps/etc/certs"
	#define BUNDLE_METADATA_RFS_PATH    "/etc/certs"
#else
	#define BUNDLE_METADATA_NVM_PATH    "/tmp/certs"
	#define BUNDLE_METADATA_RFS_PATH    "/tmp/rfc/certs"
#endif

#define WPEFRAMEWORKSECURITYUTILITY     "/usr/bin/WPEFrameworkSecurityUtility"
#define MFRUTIL                         "/usr/bin/mfr_util %s" // --PDRIVersion"
#define MD5SUM                          "/usr/bin/md5sum %s"

#ifdef GETRDMMANIFESTVERSION_IN_SCRIPT
#define GETINSTALLEDRDMMANIFESTVERSIONSCRIPT    "/lib/rdk/cdlSupport.sh getInstalledRdmManifestVersion"
#endif

#define MAX_PERIPHERAL_ITEMS 4

char *pRemCtrlStrings[MAX_PERIPHERAL_ITEMS] = {
    "&remCtrl",
    "&remCtrlAudio",
    "&remCtrlDsp",
    "&remCtrlKwModel"
};

char *pNullStrings[MAX_PERIPHERAL_ITEMS] = {
    "",
    "",
    "",
    ""
};

char *pEqualStrings[MAX_PERIPHERAL_ITEMS] = {
    "=",
    "=",
    "=",
    "="
};

char *pTypeStrings[MAX_PERIPHERAL_ITEMS] = {
    "_firmware_",
    "_audio_",
    "_dsp_",
    "_kw_model_"
};

char *pExtStrings[] = {
    ".tgz,"
};

char *pPeripheralName[MAX_PERIPHERAL_ITEMS] = {
    "FwVer",
    "AudioVer",
    "DspVer",
    "KwModelVer"
};
/* function stripinvalidchar - truncates a string when a space or control
    character is encountered.
 
        Usage: size_t stripinvalidchar <char *pIn> <size_t szIn>
 
            pIn - pointer to a char buffer to check/modify.

            szIn - the size of the character buffer in argument 1.

            RETURN - number of characters in the buffer upon exit.
 
            PITFALLS - does not check for NULL input
*/
size_t stripinvalidchar( char *pIn, size_t szIn )
{
    size_t i = 0;

    if( pIn != NULL )
    {
        while( *pIn && szIn )
        {
            if( isspace( *pIn ) || iscntrl( *pIn ) )
            {
                *pIn = 0;
                break;
            }
            ++pIn;
            --szIn;
            ++i;
        }
    }
    return i;
}

/* function makeHttpHttps - checks a URL for "http:" and, if found,
    makes it "https:"
 
        Usage: size_t makeHttpHttps <char *pIn> <size_t szpInSize>
 
            pIn - pointer to a char buffer to check/modify.

            szpInSize - the size of the character buffer in argument 1.

            RETURN - number of characters in the buffer upon exit.
 
            PITFALLS - does not check for NULL input
*/
size_t makeHttpHttps( char *pIn, size_t szpInSize )
{
    char *pTmp, *pEnd;
    int i;
    //CID:306160-Initialization-fixed
    size_t len = 0;

    if( pIn != NULL && szpInSize )
    {
       pEnd = pIn;
        i = szpInSize - 1;
        while( *pEnd && i )
        {
            --i;
            ++pEnd;     // should be left pointing to '\0' with room to insert
        }
        len = pEnd - pIn;
        if( i )     // check if room to insert
        {
            if( (pTmp = strstr( pIn, "http://" )) != NULL )
            {
                pTmp += 3;
                while( pEnd > pTmp )
                {
                    *(pEnd + 1) = *pEnd;    // copy current char 1 position to right
                    --pEnd;
                }
                ++pTmp;
                *pTmp = 's';
                ++len;
            }
        }
    }
    return len;
}


/* function MemDLAlloc - allocates a memory block and fills in data structure used for curl
                          memory downloads.
 
        Usage: int MemDLAlloc <DownloadData *pDwnData>
 
            pDwnData - pointer to a DownloadData structure to hold download allocation data.

            RETURN - 0 on success, non-zero otherwise
 
            Function Notes - The caller is responsible for freeing the dynamically allocated
            memory pointed to by the pvOut member of the DownloadData structure pointer.
*/
int MemDLAlloc( DownloadData *pDwnData, size_t szDataSize )
{
    void *ptr;
    int iRet = 1;

    if( pDwnData != NULL )
    {
        pDwnData->datasize = 0;
        ptr = malloc( szDataSize );
        pDwnData->pvOut = ptr;
        pDwnData->datasize = 0;
        if( ptr != NULL )
        {
            pDwnData->memsize = szDataSize;
            *(char *)ptr = 0;
            iRet = 0;
        }
        else
        {
            pDwnData->memsize = 0;
            SWLOG_ERROR( "MemDLAlloc: Failed to allocate memory for XCONF download\n" );
        }
    }
    return iRet;
}

/* function RunCommand - runs a predefined system command using v_secure_popen
 
        Usage: size_t RunCommand <SYSCMD eSysCmd> <const char *pArgs> <char *pResult> <size_t szResultSize>
 
            eSysCmd - an enum of type SYSCMD referencing the command to run.
 
            pArgs - pointer to a commmand argument or NULL if none required.
 
            pResult - a character buffer to store the output
 
            szResultSize - the maximum size of the output buffer

            RETURN - the number of characters in the output buffer
 
            Function Notes - Available commands along with corresponding SYSCMD enum are;
                COMMAND                                                     ENUM
 
            "/usr/bin/WPEFrameworkSecurityUtility"                      eWpeFrameworkSecurityUtility
            "/usr/bin/mfr_util %s"                                      eMfrUtil
            "/usr/bin/md5sum %s"                                        eMD5Sum
            "/lib/rdk/cdlSupport.sh getRemoteInfo"                      eGetRemoteInfo
            "/lib/rdk/cdlSupport.sh getInstalledBundleList"             eGetInstalledBundleList
            "/lib/rdk/cdlSupport.sh getInstalledRdmManifestVersion"     eGetInstalledRdmManifestVersion
 
            %s in the command string indicates an argument (pArgs) is required
*/
size_t RunCommand( SYSCMD eSysCmd, const char *pArgs, char *pResult, size_t szResultSize )
{
    FILE *fp;
    size_t nbytes_read = 0;

    if( pResult != NULL && szResultSize >= 1 )
    {
        *pResult = 0;
        switch( eSysCmd )
        {
           case eMD5Sum :
               if( pArgs != NULL )
               {
                   fp = v_secure_popen( "r", MD5SUM, pArgs );
               }
               else
               {
                   fp = NULL;
                   SWLOG_ERROR( "RunCommand: Error, %s requires an input argument\n", MD5SUM );
               }
               break;

           case eRdkSsaCli :
               if( pArgs != NULL )
               {
                   fp = v_secure_popen( "r", RDKSSACLI, pArgs );
               }
               else
               {
                   fp = NULL;
                   SWLOG_ERROR( "RunCommand: Error, %s requires an input argument\n", RDKSSACLI );
               }
               break;

           case eMfrUtil :
               if( pArgs != NULL )
               {
                   fp = v_secure_popen( "r", MFRUTIL, pArgs );
               }
               else
               {
                   fp = NULL;
                   SWLOG_ERROR( "RunCommand: Error, %s requires an input argument\n", MFRUTIL );
               }
               break;

           case eWpeFrameworkSecurityUtility :
               fp = v_secure_popen( "r", WPEFRAMEWORKSECURITYUTILITY );
               break;


#ifdef GETRDMMANIFESTVERSION_IN_SCRIPT
           case eGetInstalledRdmManifestVersion :
               fp = v_secure_popen( "r", GETINSTALLEDRDMMANIFESTVERSIONSCRIPT );
               break;
#endif

           default:
               fp = NULL;
               SWLOG_ERROR( "RunCommand: Error, unknown request type %d\n", (int)eSysCmd );
               break;
        }

        if( fp != NULL )
        {
            nbytes_read = fread( pResult, 1, szResultSize - 1, fp );
            v_secure_pclose( fp );
            if( nbytes_read != 0 )
            {
                SWLOG_INFO( "%s: Successful read %zu bytes\n", __FUNCTION__, nbytes_read );
                pResult[nbytes_read] = '\0';
                nbytes_read = strnlen( pResult, szResultSize ); // fread might include NULL characters, get accurate count
            }
            else
            {
                SWLOG_ERROR( "%s fread fails:%zu\n", __FUNCTION__, nbytes_read );
            }
//            SWLOG_INFO( "output=%s\n", pResult );
        }
        else
        {
            SWLOG_ERROR( "RunCommand: Failed to open pipe command execution\n" );
        }
    }
    else
    {
        SWLOG_ERROR( "RunCommand: Error, input argument invalid\n" );
    }
    return nbytes_read;
}

/* function BuildRemoteInfo - Formats the "periperalFirmwares" string for remote info part of xconf communication
 
        Usage: size_t BuildRemoteInfo <JSON *pItem> <char *pRemoteInfo> <size_t szMaxBuf> <bool bAddremCtrl>
 
            pItem - a pointer to a JSON structure that contains the remote info.
 
            pRemoteInfo - a pointer to a character buffer to store the output
 
            szMaxBuf - the maximum size of the buffer

            bAddremCtrl - if true then prefix values with &remCtrl. false does not add prefix

            RETURN - the number of characters written to the buffer
*/

size_t BuildRemoteInfo( JSON *pItem, char *pRemoteInfo, size_t szMaxBuf, bool bAddremCtrl )
{
    char **pPrefix;
    char **pMid;
    char *pSuffix;
    size_t szBufRemaining, szRunningLen = 0;
    int iLen = 0;
    int i = 0;
    int x;
    char productBuf[100];
    char versionBuf[50];

    if( pItem != NULL && pRemoteInfo != NULL )
    {
        szBufRemaining = szMaxBuf;
	SWLOG_INFO( "BuildRemoteInfo: Start\n" );

        i = GetJsonVal( pItem, "Product", productBuf, sizeof(productBuf) );
        if( i )
        {
            if( bAddremCtrl == true )
            {
                pPrefix = pRemCtrlStrings;
                pMid = pEqualStrings;
                pSuffix = *pNullStrings;
            }
            else
            {
                pPrefix = pNullStrings;
                pMid = pTypeStrings;
                pSuffix = *pExtStrings;
            }

            /*
                Now try to find the json values in the listed in the pPeripheralName array.
                If bAddRemCtrl is true then the output will be formatted similar to the following for each value in the list;
                    &remCtrlXR11-20=1.1.1.1&remCtrlAudioXR11-20=0.1.0.0&remCtrlDspXR11-20=0.1.0.0&remCtrlKwModelXR11-20=0.1.0.0
                Otherwise the output will be formatted similar to the following for each value in the list;
                    XR11-20_firmware_1.1.1.1.tgz,XR11-20_audio_0.1.0.0.tgz,XR11-20_dsp_0.1.0.0.tgz,XR11-20_kw_model_0.1.0.0.tgz
                Note that model name and version numbers are variables depending on the device
            */

            for( x=0; x < MAX_PERIPHERAL_ITEMS; x++ )
            {
                i = GetJsonVal( pItem, pPeripheralName[x], versionBuf, sizeof(versionBuf) );
                if( i )
                {
                    iLen = snprintf( pRemoteInfo + szRunningLen, szBufRemaining, "%s%s%s%s%s", *pPrefix, productBuf, *pMid, versionBuf, pSuffix );
		    if (iLen >= szBufRemaining) {
			SWLOG_INFO( "Buffer is Full\n" );
			iLen = szBufRemaining;
			break;
		    }
                    ++pPrefix;
                    ++pMid;
                    szBufRemaining -= iLen;
                    szRunningLen += iLen;
                }
            }
        }
	SWLOG_INFO( "BuildRemoteInfo: End\n" );
    }
    else
    {
        SWLOG_ERROR( "BuildRemoteInfo: Error, input argument(s) invalid\n" );
    }
    return (size_t)iLen;
}

/* function getJsonRpc - Use to get jsonrpc using curl lib
   @param : post_data: Required Postfield data

    @return: 0 on success, -1 otherwise
*/
int getJsonRpc(char *post_data, DownloadData* pJsonRpc )
{
    void *Curl_req = NULL;
    char token[256];
    char jsondata[256];
    int httpCode = 0;
    FileDwnl_t req_data;
    int curl_ret_code = -1;
    char header[]  = "Content-Type: application/json";
    char token_header[300];

    *token = 0;
    *jsondata = 0;
    RunCommand( eWpeFrameworkSecurityUtility, NULL, jsondata, sizeof(jsondata) );
    
    getJRPCTokenData(token, jsondata, sizeof(token));
    if (pJsonRpc->pvOut != NULL) {
        req_data.pHeaderData = header;
	req_data.pDlHeaderData = NULL;
        snprintf(token_header, sizeof(token_header), "Authorization: Bearer %s", token);
        req_data.pPostFields = post_data;
        req_data.pDlData = pJsonRpc;
        snprintf(req_data.url, sizeof(req_data.url), "%s", "http://127.0.0.1:9998/jsonrpc");
        Curl_req = doCurlInit();
        if (Curl_req != NULL) {
            curl_ret_code = getJsonRpcData(Curl_req, &req_data, token_header, &httpCode );
            
            doStopDownload(Curl_req);
        }else {
            SWLOG_ERROR("%s: doCurlInit fail\n", __FUNCTION__);
        }
    }else {
        SWLOG_ERROR("%s: Failed to allocate memory using malloc\n", __FUNCTION__);
    }
    return curl_ret_code;
}

/* Description: Use For parsing jsonrpc token
 * @param: token : Pointer to receive token
 * @param: pJsonStr : Full json data
 * @param: token_size : token buffer size
 * @return int:
 * */
int getJRPCTokenData( char *token, char *pJsonStr, unsigned int token_size )
{
    JSON *pJson = NULL;
    char status[8];
    int ret = -1;

    if (token == NULL || pJsonStr == NULL) {
        SWLOG_INFO( "%s: Parameter is NULL\n", __FUNCTION__);
        return ret;
    }
    *status = 0;
    pJson = ParseJsonStr( pJsonStr );
    if( pJson != NULL )
    {
        GetJsonVal(pJson, "token", token, token_size);
        GetJsonVal(pJson, "success", status, sizeof(status));
        SWLOG_INFO( "%s: status:%s\n", __FUNCTION__, status);
        FreeJson( pJson );
        ret = 0;
    }
    return ret;
}

/* function getInstalledBundleFileList - gets the list of bundles installed on a device. 
        Usage: metaDataFileList_st *getInstalledBundleFileList()
        Input : void
        RETURN - List of installed Bundle in NVM and RFS directory
*/
metaDataFileList_st *getInstalledBundleFileList()
{
    metaDataFileList_st *metadataNVMls = NULL, *metadataRFSls = NULL, *metaDataList = NULL;

    metadataNVMls = getMetaDataFile(BUNDLE_METADATA_NVM_PATH);
    if (metadataNVMls == NULL)
    {
        SWLOG_INFO("Certificate does not exist in NVM Path\n");
    }

    metadataRFSls = getMetaDataFile(BUNDLE_METADATA_RFS_PATH);
    if (metadataRFSls == NULL)
    {
        SWLOG_INFO("Certificate does not exist in RFS Path\n");
    }

    if ((metadataNVMls == NULL) && (metadataRFSls == NULL))
    {
        SWLOG_INFO("No metadata found only in CPE");
    }
    else if ((metadataNVMls) && (metadataRFSls == NULL))
    {
        metaDataList = metadataNVMls;
        SWLOG_INFO("No metadata found only in %s\n", BUNDLE_METADATA_NVM_PATH);
    }
    else if ((metadataNVMls == NULL) && (metadataRFSls))
    {
        metaDataList = metadataRFSls;
        SWLOG_INFO("No metadata found only in %s\n", BUNDLE_METADATA_RFS_PATH);
    }
    else if ((metadataNVMls) && (metadataRFSls))
    {
        metaDataList = mergeLists(metadataNVMls, metadataRFSls);
    }

    return metaDataList;
}

/* function getMetaDataFile - gets the files list in the directory
        Usage: metaDataFileList_st *getMetaDataFile(char *dir)
        dir : directory of NVM or RFS Path
        RETURN - List of installed Bundle in NVM or RFS directory
*/
metaDataFileList_st *getMetaDataFile(char *dir)
{
    metaDataFileList_st *newnode = NULL, *prevnode = NULL, *headNode = NULL;
    struct dirent *pDirent = NULL;

    DIR *directory = opendir(dir);
    if (directory)
    {
        while ((pDirent = readdir(directory)) != NULL)
        {
            if (pDirent->d_type == DT_REG && strstr(pDirent->d_name, "_package.json") != NULL)
            {
                newnode = (metaDataFileList_st *)malloc(sizeof(metaDataFileList_st));
                SWLOG_INFO("GetInstalledBundles: found %s\n", pDirent->d_name);
                snprintf(newnode->fileName, sizeof(newnode->fileName), "%s/%s", dir, pDirent->d_name);
                newnode->next = NULL;
                if (headNode == NULL)
                {
                    headNode = newnode;
                    prevnode = headNode;
                }
                else
                {
                    prevnode->next = newnode;
                    prevnode = newnode;
                }
            }
        }
        closedir(directory);
    }
    else
    {
        SWLOG_INFO("%s does not exist\n", dir);
    }
    return headNode;
}

/* function mergeLists - merge the RFS and NVM file list. 
        Usage: metaDataFileList_st * mergeLists(metaDataFileList_st *nvmList, metaDataFileList_st *rfsList)
        nvmList : NVM files list
        rfsList : RFS files list
        RETURN - common files list of installed Bundle in NVM and RFS directory
*/
metaDataFileList_st * mergeLists(metaDataFileList_st *nvmList, metaDataFileList_st *rfsList)
{
   metaDataFileList_st  tmp;
   metaDataFileList_st *currentNVMNode = nvmList;
   metaDataFileList_st *currentRFSNode = rfsList;
   metaDataFileList_st *metaDataList = &tmp;
   metaDataFileList_st *next = NULL, *removeDup = NULL;
   int cmpval = 0;
  
  tmp.next = NULL;
  while(currentRFSNode && currentNVMNode)
  {
      cmpval = strncmp(currentRFSNode->fileName,currentNVMNode->fileName, sizeof(currentRFSNode->fileName));
      next = (cmpval < 0) ? currentRFSNode : currentNVMNode;

      metaDataList->next = next;
      metaDataList = next;
     
      if (cmpval < 0) 
      {
          currentRFSNode = currentRFSNode->next;
      } 
      else 
      {
          currentNVMNode = currentNVMNode->next;
      }
   }

  metaDataList->next = currentRFSNode ? currentRFSNode : currentNVMNode;

  removeDup = tmp.next;
  
  while(removeDup)
  {
      if(removeDup->next && strncmp(removeDup->fileName, removeDup->next->fileName, sizeof(removeDup->fileName)) == 0)
      {
          removeDup->next = removeDup->next->next;
      }
      removeDup = removeDup->next;
  }
  
  return tmp.next;
  
}

bool get_system_uptime(double *uptime) {
    FILE* uptime_file = fopen("/proc/uptime", "r");
    if ((uptime_file != NULL) && (uptime != NULL)) {
        if (fscanf(uptime_file, "%lf", uptime) == 1) {
            fclose(uptime_file);
            return true;
        }
    }
    if( uptime_file != NULL)
    {
        fclose(uptime_file); 
    }
    return false;
}
