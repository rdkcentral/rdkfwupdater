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

#ifndef DEVICE_UTILS_MOCK
#define DEVICE_UTILS_MOCK
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "urlHelper.h"
//#include "rdkv_cdl_log_wrapper.h"
#define RDK_API_SUCCESS 0

class DeviceUtilsInterface
{
    public:
        virtual ~DeviceUtilsInterface() {}
	virtual FILE* v_secure_system(const char *mode,const char *cmd, const char *opt) = 0;
	virtual FILE* v_secure_popen(const char *mode,const char *cmd, const char *opt) = 0;
	virtual int v_secure_pclose(FILE *fp) = 0;
	virtual void*  doCurlInit() = 0;
	virtual void  doStopDownload(void *curl) = 0;
	virtual int getJsonRpcData(void *Curl_req, FileDwnl_t *req_data, char token_header, int httpCode ) = 0;
	virtual int getDevicePropertyData(const char *model, char *data, int size) = 0;
	virtual int read_RFCProperty(char* type, const char* key, char *out_value, size_t datasize) = 0;
	virtual int filePresentCheck(const char *filename) = 0;
	virtual int getFileSize(const char *filename) = 0;
	virtual bool isInStateRed() = 0;
	virtual bool isDebugServicesEnabled() = 0;
        virtual size_t GetHwMacAddress( char *iface, char *pMac, size_t szBufSize ) = 0;
	virtual size_t GetModelNum( char *pModelNum, size_t szBufSize ) = 0;
        virtual void t2CountNotify(char *marker) = 0;
        virtual void t2ValNotify(char *marker, char *val) = 0;
};

class DeviceUtilsMock: public DeviceUtilsInterface
{
    public:
        virtual ~DeviceUtilsMock() {}
	MOCK_METHOD(FILE*, v_secure_system, (const char *mode, const char *cmd, const char *opt ), ());
	MOCK_METHOD(FILE*, v_secure_popen, (const char *mode, const char *cmd, const char *opt ), ());
	MOCK_METHOD(int, v_secure_pclose, (FILE *fp), ());
	MOCK_METHOD(void*, doCurlInit, (), ());
	MOCK_METHOD(void, doStopDownload, (void *curl), ());
	MOCK_METHOD(int, getJsonRpcData, (void *Curl_req, FileDwnl_t *req_data, char token_header, int httpCode ), ());
	MOCK_METHOD(int, getDevicePropertyData, (const char *model, char *data, int size), ());
	MOCK_METHOD(int, read_RFCProperty, (char* type, const char* key, char *out_value, size_t datasize), ());
	MOCK_METHOD(int, filePresentCheck, (const char *filename ), ());
	MOCK_METHOD(int, getFileSize, (const char *filename ), ());
	MOCK_METHOD(bool, isInStateRed, (), ());
	MOCK_METHOD(bool, isDebugServicesEnabled, (), ());
	MOCK_METHOD(size_t, GetHwMacAddress, (char *iface, char *pMac, size_t szBufSize), ());
	MOCK_METHOD(size_t, GetModelNum, ( char *pModelNum, size_t szBufSize ), ());
        MOCK_METHOD(void, t2CountNotify, (char *marker), ());
        MOCK_METHOD(void, t2ValNotify, (char *marker, char *val), ());
};
#endif
