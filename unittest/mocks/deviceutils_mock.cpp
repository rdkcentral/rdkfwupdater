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

#include "deviceutils_mock.h"
#include <iostream>

using namespace std;

extern DeviceUtilsMock *g_DeviceUtilsMock;

extern "C" int v_secure_system(const char *mode, ...)
{
    if (!g_DeviceUtilsMock)
    {
        cout << "v_secure_system g_DeviceUtilsMock object is NULL" << endl;
        return NULL;  // Return error code instead of NULL
    }
    printf("Inside Mock Function v_secure_system\n");
    // Note: v_secure_system returns int, but mock returns FILE*
    // This is a type mismatch - just return success
    return g_DeviceUtilsMock->v_secure_system(mode, NULL, NULL);
}
//extern "C" FILE* v_secure_popen(const char *mode, const char *cmd, const char *opt )
extern "C" FILE* v_secure_popen(const char *mode, ...)
{
    if (!g_DeviceUtilsMock)
    {
        cout << "v_secure_popen g_DeviceUtilsMock object is NULL" << endl;
        return NULL;
    }
    printf("Inside Mock Function v_secure_popen\n");
    return g_DeviceUtilsMock->v_secure_popen(mode, NULL, NULL);
}
/*extern "C" FILE* v_secure_popen(const char *mode, const char *cmd)
{
    if (!g_DeviceUtilsMock)
    {
        cout << "v_secure_popen g_DeviceUtilsMock object is NULL" << endl;
        return NULL;
    }
    printf("Inside Mock Function v_secure_popen\n");
    return g_DeviceUtilsMock->v_secure_popen(mode, cmd);
}*/

extern "C" int v_secure_pclose(FILE *fp)
{
    if (!g_DeviceUtilsMock)
    {
        cout << "v_secure_pclose g_DeviceUtilsMock object is NULL" << endl;
        return 0;
    }
    printf("Inside Mock Function v_secure_pclose\n");
    return g_DeviceUtilsMock->v_secure_pclose(fp);
}

extern "C" void* doCurlInit()
{
    if (!g_DeviceUtilsMock)
    {
        cout << "doCurlInit g_DeviceUtilsMock object is NULL" << endl;
        return 0;
    }
    printf("Inside Mock Function doCurlInit\n");
    return g_DeviceUtilsMock->doCurlInit();
}

extern "C" int getJsonRpcData(void *Curl_req, FileDwnl_t *req_data, char token_header, int httpCode )
{
    if (!g_DeviceUtilsMock)
    {
        cout << "getJsonRpcData g_DeviceUtilsMock object is NULL" << endl;
        return 0;
    }
    printf("Inside Mock Function getJsonRpcData\n");
    return g_DeviceUtilsMock->getJsonRpcData(Curl_req, req_data,token_header, httpCode );
}

extern "C" void doStopDownload(void *curl)
{
    if (!g_DeviceUtilsMock)
    {
        cout << "doStopDownload g_DeviceUtilsMock object is NULL" << endl;
        return;
    }
    printf("Inside Mock Function doStopDownload\n");
    return g_DeviceUtilsMock->doStopDownload(curl);
}

extern "C" int getDevicePropertyData(const char *model, char *data, int size)
{
    if (!g_DeviceUtilsMock)
    {
        cout << "g_DeviceUtilsMock object is NULL" << endl;
        return -1;
    }
    printf("Inside Mock Function getDevicePropertyData\n");
    if (0 == (strncmp(model, "CPU_ARCH", 8))) {
        snprintf(data, size, "%s", "X86");
    } else if (0 == (strncmp(model, "DEVICE_NAME", 11))) {
        snprintf(data, size, "%s", "PLATCO");
    } else if (0 == (strncmp(model, "PDRI_ENABLED", 12))){
        snprintf(data, size, "%s", "true");
    } else if (0 == (strncmp(model, "STAGE2LOCKFILE", 13))) {
        snprintf(data, size, "%s", "/tmp/stage2");
    } else if (0 == (strncmp(model, "ESTB_INTERFACE", 14))) {
        snprintf(data, size, "%s", "eth1");
    }
    return g_DeviceUtilsMock->getDevicePropertyData(model, data, size);
}

extern "C" int read_RFCProperty(char* type, const char* key, char *out_value, size_t datasize)
{
    if (!g_DeviceUtilsMock)
    {
        cout << "g_DeviceUtilsMock object is NULL" << endl;
        return -1;
    }
    printf("Inside Mock Function read_RFCProperty\n");
    if (0 == (strncmp(type, "OsClass", 7))) {
        snprintf(out_value, datasize, "%s", "true");
    } else if (0 == (strncmp(type, "SerialNumber", 12))) {
        snprintf(out_value, datasize, "%s", "123456789012345");
    } else if (0 == (strncmp(type, "PDRI_ENABLED", 12))){
        snprintf(out_value, datasize, "%s", "true");
    } else if (0 == (strncmp(type, "AccountID", 9))) {
        snprintf(out_value, datasize, "%s", "123456789");
    } else {
        snprintf(out_value, datasize, "%s", "default.com");
    }
    return g_DeviceUtilsMock->read_RFCProperty(type, key, out_value, datasize);
}

extern "C" int filePresentCheck(const char *filename)
{
    if (!g_DeviceUtilsMock)
    {
        cout << "filePresentCheck g_DeviceUtilsMock object is NULL" << endl;
        return 0;
    }
    printf("Inside Mock Function filePresentCheck\n");
    return g_DeviceUtilsMock->filePresentCheck(filename);
}

extern "C" int getFileSize(const char *file)
{
    if (!g_DeviceUtilsMock)
    {
        cout << "getFileSize g_DeviceUtilsMock object is NULL" << endl;
        return 0;
    }
    printf("Inside Mock Function getFileSize\n");
    return g_DeviceUtilsMock->getFileSize(file);
}

extern "C" bool isInStateRed()
{
    if (!g_DeviceUtilsMock)
    {
        cout << "isInStateRed g_DeviceUtilsMock object is NULL" << endl;
        return false;
    }
    printf("Inside Mock Function isInStateRed\n");
    return g_DeviceUtilsMock->isInStateRed();
}

extern "C" bool isDebugServicesEnabled(void) {
        if (!g_DeviceUtilsMock) {
            cout << "isDebugServicesEnabled g_DeviceUtilsMock object is NULL" << endl;
	    return false; 
        }
        return g_DeviceUtilsMock->isDebugServicesEnabled();
    }

extern "C" size_t GetHwMacAddress( char *iface, char *pMac, size_t szBufSize )
{
    if (!g_DeviceUtilsMock)
    {
        cout << "GetHwMacAddress g_DeviceUtilsMock object is NULL" << endl;
        return 0;
    }
    printf("Inside Mock Function GetHwMacAddress\n");
    return g_DeviceUtilsMock->GetHwMacAddress(iface, pMac, szBufSize);
}

extern "C" size_t GetModelNum( char *pModelNum, size_t szBufSize )
{
    if (!g_DeviceUtilsMock)
    {
	cout << "GetBuildType  g_DeviceStatusMock object is NULL" << endl;
        return 0;
    }
    printf("Inside Mock Function GetModelNum\n");
    snprintf(pModelNum, szBufSize, "%s", "12345");
    return g_DeviceUtilsMock->GetModelNum(pModelNum, szBufSize);
}

#ifdef DEVICE_API
extern "C" void t2CountNotify(char *marker)
{
    if (!g_DeviceUtilsMock)
    {
        cout << "t2CountNotify  g_DeviceUtilsMock object is NULL" << endl;
        return ;
    }
    printf("Inside Mock Function t2CountNotify\n");
    return g_DeviceUtilsMock->t2CountNotify(marker);
}

extern "C" void t2ValNotify(char *marker, char *val)
{
    if (!g_DeviceUtilsMock)
    {
        cout << "t2ValNotify g_DeviceUtilsMock object is NULL" << endl;
        return ;
    }
    printf("Inside Mock Function t2ValNotify\n");
    return g_DeviceUtilsMock->t2ValNotify(marker, val);
}

// ========== Mocks for external functions (moved to common_utilities) ==========
/*
extern "C" size_t GetTimezone(char *pTimezone, const char *arch, size_t szBufSize)
{
    // Simple stub for external function
    if (pTimezone != NULL && szBufSize > 0) {
        snprintf(pTimezone, szBufSize, "UTC");
        return strlen(pTimezone);
    }
    return 0;
}

extern "C" size_t GetCapabilities(char *pCapabilities, size_t szBufSize)
{
    // Simple stub for external function
    if (pCapabilities != NULL && szBufSize > 0) {
        snprintf(pCapabilities, szBufSize, "HDR");
        return strlen(pCapabilities);
    }
    return 0;
}

extern "C" size_t GetUTCTime(char *pUTCTime, size_t szBufSize)
{
    // Simple stub for external function
    if (pUTCTime != NULL && szBufSize > 0) {
        snprintf(pUTCTime, szBufSize, "12345");
        return strlen(pUTCTime);
    }
    return 0;
}

extern "C" size_t GetFirmwareVersion(char *pFWVersion, size_t szBufSize)
{
    // Simple stub for external function
    if (pFWVersion != NULL && szBufSize > 0) {
        snprintf(pFWVersion, szBufSize, "1.0.0");
        return strlen(pFWVersion);
    }
    return 0;
}

extern "C" size_t GetMFRName(char *pMFRName, size_t szBufSize)
{
    // Simple stub for external function
    if (pMFRName != NULL && szBufSize > 0) {
        snprintf(pMFRName, szBufSize, "Comcast");
        return strlen(pMFRName);
    }
    return 0;
}

extern "C" size_t GetFileContents(char **ppFileContents, const char *pFileName)
{
    // Simple stub for external function
    if (ppFileContents != NULL && pFileName != NULL) {
        *ppFileContents = (char *)malloc(20);
        if (*ppFileContents) {
            snprintf(*ppFileContents, 20, "File contents");
            return strlen(*ppFileContents);
        }
    }
    return 0;
}
*/
extern "C" size_t GetBuildType(char *pBuildType, size_t szBufSize, BUILDTYPE *peBuildTypeOut)
{
    // Mock for external function from common_utilities
    // Returns "prod" build type (ePROD enum value)
    // Possible values: "dev" (eDEV), "vbn" (eVBN), "prod" (ePROD), "qa" (eQA)
    if (pBuildType != NULL && szBufSize > 0) {
        snprintf(pBuildType, szBufSize, "prod");
        if (peBuildTypeOut) {
            *peBuildTypeOut = ePROD;  // Use ePROD instead of PRODUCTION
        }
        return strlen(pBuildType);
    }
    return 0;
}
/*
extern "C" int stripinvalidchar(char *pStr, int len)
{
    // Mock for external function from common_utilities
    // Returns the length after stripping invalid characters
    // For testing purposes, just return the original length
    if (pStr != NULL && len > 0) {
        return len;
    }
    return 0;
}
*/
extern "C" int makeHttpHttps(char *pStr, int len)
{
    // Mock for external function from common_utilities
    // This function converts http:// URLs to https://
    // For testing purposes, just return success
    if (pStr != NULL && len > 0) {
        return 1; // Return success
    }
    return 0;
}

extern "C" void swLog(const char *file, const char *func, int line, int level, const char *format, ...)
{
    // Mock for external function from common_utilities (libfwutils)
    // This is a logging function - for testing purposes, we can just ignore it
    // or optionally print to stdout for debugging
    return;
}

extern "C" int allocDowndLoadDataMem(void *ptr, int size)
{
    // Mock for external function from common_utilities (libdwnutils)
    // This function allocates memory for download data
    // For testing purposes, just return success
    return 0;
}

extern "C" size_t stripinvalidchar(char *pIn, size_t szIn)
{
    // Mock for external function from common_utilities (libfwutils)
    // This function truncates a string when a space or control character is encountered
    size_t i = 0;

    if (pIn != NULL)
    {
        while (*pIn && szIn)
        {
            if (isspace(*pIn) || iscntrl(*pIn))
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

#endif
