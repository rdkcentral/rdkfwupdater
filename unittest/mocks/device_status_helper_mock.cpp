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

#include "device_status_helper_mock.h"
#include <iostream>

using namespace std;

extern DeviceStatusMock *g_DeviceStatusMock;

extern "C" int getDevicePropertyData(const char *model, char *data, int size)
{
    if (!g_DeviceStatusMock)
    {
	cout << "g_DeviceStatusMock object is NULL" << endl;
        return -1;
    }
    printf("Inside Mock Function getDevicePropertyData\n");
    if (0 == (strncmp(model, "CPU_ARCH", 8))) {
        snprintf(data, size, "%s", "X86");
    } else if (0 == (strncmp(model, "DEVICE_NAME", 11))) {
        snprintf(data, size, "%s", "PLATCO");
    } else if (0 == (strncmp(model, "PDRI_ENABLED", 12))){
        snprintf(data, size, "%s", "true");
    }
    return g_DeviceStatusMock->getDevicePropertyData(model, data, size);
}

extern "C" size_t GetEstbMac(char *pEstbMac, size_t szBufSize)
{
    if (!g_DeviceStatusMock)
    {
	cout << "GetEstbMac g_DeviceStatusMock object is NULL" << endl;
        return 0;
    }
    printf("Inside Mock Function GetEstbMac\n");
    //snprintf(pEstbMac, szBufSize, "%s", "aa:bb:cc:dd:ee");
    return g_DeviceStatusMock->GetEstbMac(pEstbMac, szBufSize);
}
extern "C" int write_RFCProperty(const char *key, const char *value, RFCVALDATATYPE datatype)
{
    if (!g_DeviceStatusMock)
    {
	cout << "GetEstbMac g_DeviceStatusMock object is NULL" << endl;
        return 0;
    }
    printf("Inside Mock Function write_RFCProperty\n");
    return g_DeviceStatusMock->write_RFCProperty(key, value, datatype);
}

extern "C" size_t GetFirmwareVersion( char *pFWVersion, size_t szBufSize )
{
    if (!g_DeviceStatusMock)
    {
	cout << "GetFirmwareVersion g_DeviceStatusMock object is NULL" << endl;
        return 0;
    }
    printf("Inside Mock Function GetFirmwareVersion\n");
    snprintf(pFWVersion, szBufSize, "%s", "123456_comcast.bin");
    return g_DeviceStatusMock->GetFirmwareVersion(pFWVersion, szBufSize);
}

extern "C" size_t GetAdditionalFwVerInfo( char *pAdditionalFwVerInfo, size_t szBufSize )
{
    if (!g_DeviceStatusMock)
    {
	cout << "GetAdditionalFwVerInfo  g_DeviceStatusMock object is NULL" << endl;
        return 0;
    }
    return g_DeviceStatusMock->GetAdditionalFwVerInfo(pAdditionalFwVerInfo, szBufSize);
}

extern "C" size_t GetBuildType( char *pBuildType, size_t szBufSize, BUILDTYPE *peBuildTypeOut )
{
    if (!g_DeviceStatusMock)
    {
        cout << "GetBuildType  g_DeviceStatusMock object is NULL" << endl;
        return 0;
    }
    printf("Inside Mock Function GetBuildType\n");
    snprintf(pBuildType, szBufSize, "%s", "prod");
    return g_DeviceStatusMock->GetBuildType(pBuildType, szBufSize, peBuildTypeOut);
}

extern "C" size_t GetModelNum( char *pModelNum, size_t szBufSize )
{
    if (!g_DeviceStatusMock)
    {
	cout << "GetModelNum g_DeviceStatusMock object is NULL" << endl;
        return 0;
    }
    printf("Inside Mock Function GetModelNum\n");
    snprintf(pModelNum, szBufSize, "%s", "12345");
    return g_DeviceStatusMock->GetModelNum(pModelNum, szBufSize);
}

extern "C" size_t GetMFRName( char *pMFRName, size_t szBufSize )
{
    if (!g_DeviceStatusMock)
    {
	cout << "GetMFRName  g_DeviceStatusMock object is NULL" << endl;
        return 0;
    }
    printf("Inside Mock Function GetMFRName\n");
    snprintf(pMFRName, szBufSize, "%s", "unknown");
    return g_DeviceStatusMock->GetMFRName(pMFRName, szBufSize);
}

extern "C" size_t GetPartnerId( char *pPartnerId, size_t szBufSize )
{
    if (!g_DeviceStatusMock)
    {
	cout << "GetPartnerId  g_DeviceStatusMock object is NULL" << endl;
        return 0;
    }
    printf("Inside Mock Function GetPartnerId\n");
    snprintf(pPartnerId, szBufSize, "%s", "global");
    return g_DeviceStatusMock->GetPartnerId(pPartnerId, szBufSize);
}

extern "C" size_t GetOsClass( char *pOsClass, size_t szBufSize )
{
    if (!g_DeviceStatusMock)
    {
	cout << "GetOsClass  g_DeviceStatusMock object is NULL" << endl;
        return 0;
    }
    printf("Inside Mock Function GetOsClass\n");
    snprintf(pOsClass, szBufSize, "%s", "NO");
    return g_DeviceStatusMock->GetOsClass(pOsClass, szBufSize);
}

extern "C" size_t GetExperience( char *pExperience, size_t szBufSize )
{
    if (!g_DeviceStatusMock)
    {
	cout << "GetExperience  g_DeviceStatusMock object is NULL" << endl;
        return 0;
    }
    printf("Inside Mock Function GetExperience\n");
    snprintf(pExperience, szBufSize, "%s", "NO");//TODO: Need to check what is value
    return g_DeviceStatusMock->GetExperience(pExperience, szBufSize);
}

extern "C" size_t GetMigrationReady( char *pMigrationReady, size_t szBufSize )
 {
     if (!g_DeviceStatusMock)
     {
 	cout << "GetMigrationReady  g_DeviceStatusMock object is NULL" << endl;
         return 0;
     }
     printf("Inside Mock Function GetMigrationReady\n");
     snprintf(pMigrationReady, szBufSize, "%s", "NO");
     return g_DeviceStatusMock->GetMigrationReady(pMigrationReady, szBufSize);
 }

extern "C" size_t GetAccountID( char *pAccountID, size_t szBufSize )
{
    if (!g_DeviceStatusMock)
    {
	cout << "GetAccountID  g_DeviceStatusMock object is NULL" << endl;
        return 0;
    }
    printf("Inside Mock Function GetAccountID\n");
    snprintf(pAccountID, szBufSize, "%s", "123456789123456789");
    return g_DeviceStatusMock->GetAccountID(pAccountID, szBufSize);
}

extern "C" size_t GetSerialNum( char *pSerialNum, size_t szBufSize )
{
    if (!g_DeviceStatusMock)
    {
	cout << "GetSerialNum  g_DeviceStatusMock object is NULL" << endl;
        return 0;
    }
    printf("Inside Mock Function GetSerialNum\n");
    snprintf(pSerialNum, szBufSize, "%s", "123456789123456789");
    return g_DeviceStatusMock->GetSerialNum(pSerialNum, szBufSize);
}

extern "C" size_t GetUTCTime( char *pUTCTime, size_t szBufSize )
{
    if (!g_DeviceStatusMock)
    {
	cout << "GetUTCTime  g_DeviceStatusMock object is NULL" << endl;
        return 0;
    }
    printf("Inside Mock Function GetUTCTime\n");
    snprintf(pUTCTime, szBufSize, "%s", "GLOBAL");
    return g_DeviceStatusMock->GetUTCTime(pUTCTime, szBufSize);
}

extern "C" size_t GetInstalledBundles(char *pBundles, size_t szBufSize)
{
    if (!g_DeviceStatusMock)
    {
	cout << "GetInstalledBundles  g_DeviceStatusMock object is NULL" << endl;
        return 0;
    }
    printf("Inside Mock Function GetInstalledBundles\n");
    snprintf(pBundles, szBufSize, "%s", "castore");
    return g_DeviceStatusMock->GetInstalledBundles(pBundles, szBufSize);
}
extern "C" size_t GetRdmManifestVersion( char *pRdmManifestVersion, size_t szBufSize ) 
{
    if (!g_DeviceStatusMock)
    {
	cout << "GetRdmManifestVersion  g_DeviceStatusMock object is NULL" << endl;
        return 0;
    }
    printf("Inside Mock Function GetRdmManifestVersion\n");
    snprintf(pRdmManifestVersion, szBufSize, "%s", "rdm_1.2.3.4.5.6.7.8");
    return g_DeviceStatusMock->GetRdmManifestVersion(pRdmManifestVersion, szBufSize);
}
extern "C" size_t GetTimezone( char *pTimezone, const char *cpuArch, size_t szBufSize ) 
{
    if (!g_DeviceStatusMock)
    {
	cout << "GetTimezone  g_DeviceStatusMock object is NULL" << endl;
        return 0;
    }
    printf("Inside Mock Function GetTimezone\n");
    snprintf(pTimezone, szBufSize, "%s", "xglobal");
    return g_DeviceStatusMock->GetTimezone(pTimezone, "ARM", szBufSize);
}
extern "C" size_t GetCapabilities( char *pCapabilities, size_t szBufSize ) 
{
    if (!g_DeviceStatusMock)
    {
	cout << "GetCapabilities  g_DeviceStatusMock object is NULL" << endl;
        return 0;
    }
    printf("Inside Mock Function GetCapabilities\n");
    snprintf(pCapabilities, szBufSize, "%s", "NA");
    return g_DeviceStatusMock->GetCapabilities(pCapabilities, szBufSize);
}

extern "C" int filePresentCheck(const char *filename)
{
    if (!g_DeviceStatusMock)
    {
	cout << "filePresentCheck  g_DeviceStatusMock object is NULL" << endl;
        return 0;
    }
    printf("Inside Mock Function filePresentCheck\n");
    return g_DeviceStatusMock->filePresentCheck(filename);
}

extern "C" bool isConnectedToInternet (void)
{
    if (!g_DeviceStatusMock)
    {
	cout << "isConnectedToInternet  g_DeviceStatusMock object is NULL" << endl;
        return 0;
    }
    printf("Inside Mock Function isConnectedToInternet\n");
    return g_DeviceStatusMock->isConnectedToInternet();
}

extern "C" int v_secure_system(const char *str)
{
    if (!g_DeviceStatusMock)
    {
	cout << "v_secure_system  g_DeviceStatusMock object is NULL" << endl;
        return 0;
    }
    printf("Inside Mock Function v_secure_system\n");
    return g_DeviceStatusMock->v_secure_system(str);
}

/*extern "C" int updateFWDownloadStatus(struct FWDownloadStatus *fwdls, const char *disableStatsUpdate)
{
    if (!g_DeviceStatusMock)
    {
	cout << "updateFWDownloadStatus  g_DeviceStatusMock object is NULL" << endl;
        return 0;
    }
    printf("Inside Mock Function updateFWDownloadStatus\n");
    return g_DeviceStatusMock->updateFWDownloadStatus(fwdls, disableStatsUpdate);
}*/

extern "C" void uninitialize(int value)
{
    if (!g_DeviceStatusMock)
    {
	cout << "uninitialize  g_DeviceStatusMock object is NULL" << endl;
        return ;
    }
    printf("Inside Mock Function uninitialize\n");
    return g_DeviceStatusMock->uninitialize(value);
}

extern "C" void eventManager(const char *cur_event_name, const char *event_status)
{
    if (!g_DeviceStatusMock)
    {
	cout << "eventManager  g_DeviceStatusMock object is NULL" << endl;
        return ;
    }
    printf("Inside Mock Function eventManager\n");
    return g_DeviceStatusMock->eventManager(cur_event_name, event_status);
}

extern "C" size_t GetPDRIFileName( char *pPDRIFilename, size_t szBufSize )
{
    if (!g_DeviceStatusMock)
    {
	cout << "GetPDRIFileName  g_DeviceStatusMock object is NULL" << endl;
        return 0;
    }
    printf("Inside Mock Function GetPDRIFileName\n");
    return g_DeviceStatusMock->GetPDRIFileName(pPDRIFilename, szBufSize);
}

extern "C" void updateUpgradeFlag(int action)
{
    if (!g_DeviceStatusMock)
    {
	cout << "updateUpgradeFlag  g_DeviceStatusMock object is NULL" << endl;
        return ;
    }
    printf("Inside Mock Function updateUpgradeFlag\n");
    return g_DeviceStatusMock->updateUpgradeFlag(action);
}

extern "C" void t2CountNotify(char *marker)
{
    if (!g_DeviceStatusMock)
    {
	cout << "t2CountNotify  g_DeviceStatusMock object is NULL" << endl;
        return ;
    }
    printf("Inside Mock Function t2CountNotify\n");
    return g_DeviceStatusMock->t2CountNotify(marker);
}

extern "C" void t2ValNotify(char *marker, char *val)
{
    if (!g_DeviceStatusMock)
    {
	cout << "t2ValNotify  g_DeviceStatusMock object is NULL" << endl;
        return ;
    }
    printf("Inside Mock Function t2ValNotify\n");
    return g_DeviceStatusMock->t2ValNotify(marker, val);
}

// Mock for swLog (logging function from common_utilities)
extern "C" int swLog(int level, const char* format, ...)
{
    // Simple stub - just return success without actually logging
    // In real code, this would write to syslog/files, but for tests we just ignore it
    return 0;
}
