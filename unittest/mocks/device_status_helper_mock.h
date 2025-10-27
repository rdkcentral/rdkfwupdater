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

#ifndef DEVICE_STATUS_MOCK
#define DEVICE_STATUS_MOCK
#include <gtest/gtest.h>
#include <gmock/gmock.h>

//#include "rdkv_cdl_log_wrapper.h"
#include "rfcinterface.h"
#include "rdk_fwdl_utils.h"  // Add this for BUILDTYPE from system library
#define RDK_API_SUCCESS 0
//int getDevicePropertyData(const char *model, char *data, int size);
class DeviceStatusInterface
{
    public:
        virtual ~DeviceStatusInterface() {}
        //virtual bool isMtlsEnabled() = 0;
	virtual int getDevicePropertyData(const char *model, char *data, int size) = 0;
	virtual size_t GetEstbMac(char *pEstbMac, size_t szBufSize) = 0;
	virtual size_t GetFirmwareVersion( char *pFWVersion, size_t szBufSize ) = 0;
	virtual size_t GetAdditionalFwVerInfo( char *pAdditionalFwVerInfo, size_t szBufSize ) = 0;
	virtual size_t GetBuildType( char *pBuildType, size_t szBufSize, BUILDTYPE *peBuildTypeOut ) = 0;
	virtual size_t GetModelNum( char *pModelNum, size_t szBufSize ) = 0;
	virtual size_t GetMFRName( char *pMFRName, size_t szBufSize ) = 0;
	virtual size_t GetPartnerId( char *pPartnerId, size_t szBufSize ) = 0;
	virtual size_t GetOsClass( char *pOsClass, size_t szBufSize ) = 0;
	virtual size_t GetExperience( char *pExperience, size_t szBufSize ) = 0;
	virtual size_t GetMigrationReady( char *pMigrationReady, size_t szBufSize ) = 0;
	virtual size_t GetAccountID( char *pAccountID, size_t szBufSize ) = 0;
	virtual size_t GetSerialNum( char *pSerialNum, size_t szBufSize ) = 0;
	virtual size_t GetUTCTime( char *pUTCTime, size_t szBufSize ) = 0;
	virtual size_t GetInstalledBundles(char *pBundles, size_t szBufSize) = 0;
	virtual size_t GetRdmManifestVersion( char *pRdmManifestVersion, size_t szBufSize ) = 0;
	virtual size_t GetTimezone( char *pTimezone, const char *cpuArch, size_t szBufSize ) = 0;
	virtual size_t GetCapabilities( char *pCapabilities, size_t szBufSize ) = 0;
	virtual int filePresentCheck(const char *filename) = 0;
	virtual bool isConnectedToInternet () = 0;
	//virtual int updateFWDownloadStatus(struct FWDownloadStatus *fwdls, const char *disableStatsUpdate) = 0;
	virtual int write_RFCProperty(const char *key, const char *value, RFCVALDATATYPE datatype) = 0;
	virtual void uninitialize(int data) = 0;
	virtual void eventManager(const char *cur_event_name, const char *event_status) = 0;
	virtual size_t GetPDRIFileName( char *pPDRIFilename, size_t szBufSize ) = 0;
	virtual void updateUpgradeFlag(int action) = 0;
	virtual void t2CountNotify(char *marker) = 0;
	virtual void t2ValNotify(char *marker, char *val) = 0;
	virtual int v_secure_system(const char *str ) = 0;
};

class DeviceStatusMock: public DeviceStatusInterface
{
    public:
        virtual ~DeviceStatusMock() {}
        //MOCK_METHOD0(isMtlsEnabled, bool());
	MOCK_METHOD(int, getDevicePropertyData, (const char *model, char *data, int size), ());
    	MOCK_METHOD(size_t, GetEstbMac, (char *pEstbMac, size_t szBufSize), ());
    	MOCK_METHOD(size_t, GetFirmwareVersion, ( char *pFWVersion, size_t szBufSize ), ());
    	MOCK_METHOD(size_t, GetAdditionalFwVerInfo, ( char *pAdditionalFwVerInfo, size_t szBufSize ), ());
    	MOCK_METHOD(size_t, GetBuildType, ( char *pBuildType, size_t szBufSize, BUILDTYPE *peBuildTypeOut ), ());
    	MOCK_METHOD(size_t, GetModelNum, ( char *pModelNum, size_t szBufSize ), ());
	MOCK_METHOD(size_t, GetMFRName, ( char *pMFRName, size_t szBufSize ), ());
    	MOCK_METHOD(size_t, GetPartnerId, ( char *pPartnerId, size_t szBufSize ), ());
    	MOCK_METHOD(size_t, GetOsClass, ( char *pOsClass, size_t szBufSize ), ());
    	MOCK_METHOD(size_t, GetExperience, ( char *pExperience, size_t szBufSize ), ());
	MOCK_METHOD(size_t, GetMigrationReady, ( char *pMigrationReady, size_t szBufSize ), ());
    	MOCK_METHOD(size_t, GetAccountID, ( char *pAccountID, size_t szBufSize ), ());
    	MOCK_METHOD(size_t, GetSerialNum, ( char *pSerialNum, size_t szBufSize ), ());
    	MOCK_METHOD(size_t, GetUTCTime, ( char *pUTCTime, size_t szBufSize ), ());
    	MOCK_METHOD(size_t, GetInstalledBundles, (char *pBundles, size_t szBufSize), ());
    	MOCK_METHOD(size_t, GetRdmManifestVersion, ( char *pRdmManifestVersion, size_t szBufSize ), ());
    	MOCK_METHOD(size_t, GetTimezone, ( char *pTimezone, const char *cpuArch, size_t szBufSize ), ());
    	MOCK_METHOD(size_t, GetCapabilities, ( char *pCapabilities, size_t szBufSize ), ());
    	MOCK_METHOD(int, filePresentCheck, (const char *filename ), ());
    	MOCK_METHOD(bool, isConnectedToInternet, (), ());
    	MOCK_METHOD(int, v_secure_system, (const char *str ), ());
    	//MOCK_METHOD(int, updateFWDownloadStatus, (struct FWDownloadStatus *fwdls, const char *disableStatsUpdate), ());
    	MOCK_METHOD(int, write_RFCProperty, (const char *key, const char *value, RFCVALDATATYPE datatype), ());
    	MOCK_METHOD(void, uninitialize, (int data), ());
    	MOCK_METHOD(void, eventManager, (const char *cur_event_name, const char *event_status), ());
    	MOCK_METHOD(size_t, GetPDRIFileName, ( char *pPDRIFilename, size_t szBufSize ), ());
    	MOCK_METHOD(void, updateUpgradeFlag, (int action), ());
    	MOCK_METHOD(void, t2CountNotify, (char *marker), ());
    	MOCK_METHOD(void, t2ValNotify, (char *marker, char *val), ());
};
#endif
