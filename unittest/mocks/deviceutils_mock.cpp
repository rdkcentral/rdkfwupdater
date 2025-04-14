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
        return NULL;
    }
    printf("Inside Mock Function v_secure_system\n");
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
