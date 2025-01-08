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

#ifndef __DEVICEUTILS_H__
#define __DEVICEUTILS_H__

#ifndef GTEST_ENABLE
#include "urlHelper.h"
#endif
#include "json_parse.h"     // needed for JSON struct definition
#include "mtlsUtils.h"

#ifndef GETRDMMANIFESTVERSION_IN_SCRIPT
    #define GETRDMMANIFESTVERSION_IN_SCRIPT
#endif
#ifndef GETMODEL_IN_SCRIPT
    #define GETMODEL_IN_SCRIPT
#endif

typedef enum {
    eMD5Sum,
    eRdkSsaCli,
    eMfrUtil,
    eWpeFrameworkSecurityUtility
#ifdef GETRDMMANIFESTVERSION_IN_SCRIPT
    ,eGetInstalledRdmManifestVersion
#endif
#ifdef GETMODEL_IN_SCRIPT
    ,eGetModelNum
#endif
} SYSCMD;

#define DEFAULT_DL_ALLOC    1024

typedef struct metaDataFileList
{
    char fileName[512];
    struct metaDataFileList *next;
}metaDataFileList_st;

/* function stripinvalidchar - truncates a string when a space or control
    character is encountered.
 
        Usage: size_t stripinvalidchar <char *pIn> <size_t szIn>
 
            pIn - pointer to a char buffer to check/modify.

            szIn - the size of the character buffer in argument 1.

            RETURN - number of characters in the buffer upon exit.
 
            PITFALLS - does not check for NULL input
*/
size_t stripinvalidchar(char *pIn, size_t szIn);

/* function makeHttpHttps - checks a URL for "http:" and, if found,
    makes it "https:"
 
        Usage: size_t makeHttpHttps <char *pIn> <size_t szpInSize>
 
            pIn - pointer to a char buffer to check/modify.

            szpInSize - the size of the character buffer in argument 1.

            RETURN - number of characters in the buffer upon exit.
 
            PITFALLS - does not check for NULL input
*/
size_t makeHttpHttps(char *pIn, size_t szpInSize);

/* function MemDLAlloc - allocates a memory block and fills in data structure used for curl
                          memory downloads.
 
        Usage: int MemDLAlloc <DownloadData *pDwnData>
 
            pDwnData - pointer to a DownloadData structure to hold download allocation data.

            RETURN - 0 on success, non-zero otherwise
 
            Function Notes - The caller is responsible for freeing the dynamically allocated
            memory pointed to by the pvOut member of the DownloadData structure pointer.
*/
int MemDLAlloc(DownloadData *pDwnData, size_t szDataSize);

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
            "/lib/rdk/cdlSupport.sh getModel"     			eGetModelNum
 
            %s in the command string indicates an argument (pArgs) is required
*/
size_t RunCommand(SYSCMD eSysCmd, const char *pArgs, char *pResult, size_t szResultSize);

/* function BuildRemoteInfo - Formats the "periperalFirmwares" string for remote info part of xconf communication
 
        Usage: size_t BuildRemoteInfo <JSON *pItem> <char *pRemoteInfo> <size_t szMaxBuf> <bool bAddremCtrl>
 
            pItem - a pointer to a JSON structure that contains the remote info.
 
            pRemoteInfo - a pointer to a character buffer to store the output
 
            szMaxBuf - the maximum size of the buffer

            bAddremCtrl - if true then prefix values with &remCtrl. false does not add prefix

            RETURN - the number of characters written to the buffer
*/
size_t BuildRemoteInfo(JSON *pItem, char *pRemoteInfo, size_t szMaxBuf, bool bAddremCtrl);

/* function getJsonRpc - Use to get jsonrpc using curl lib
   @param : post_data: Required Postfield data

    @return: 0 on success, -1 otherwise
*/
int getJsonRpc(char *post_data, DownloadData* pJsonRpc);

/* Description: Use For parsing jsonrpc token
 * @param: token : Pointer to receive token
 * @param: pJsonStr : Full json data
 * @param: token_size : token buffer size
 * @return int:
 * */
int getJRPCTokenData(char *token, char *pJsonStr, unsigned int token_size);

/* Description: Use to get system uptime in second
 *  @param: uptime : pointer to recive uptime of system
 *  @return :bool
 * */

bool get_system_uptime(double *uptime);

metaDataFileList_st *getInstalledBundleFileList();
metaDataFileList_st *getMetaDataFile(char *dir);
metaDataFileList_st * mergeLists(metaDataFileList_st *nvmList, metaDataFileList_st *rfsList);


#endif
