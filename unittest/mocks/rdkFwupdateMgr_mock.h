/*
 * Copyright 2024 Comcast Cable Communications Management, LLC
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

#ifndef RDKFWUPDATEMGR_MOCK_H
#define RDKFWUPDATEMGR_MOCK_H

#include <gtest/gtest.h>
#include <gmock/gmock.h>

extern "C" {
#include "json_process.h"
#include "rdkFwupdateMgr_handlers.h"
//#include "downloadUtil.h"
#include "rfcinterface.h"
#include "rdkv_upgrade.h"
}

/**
 * Mock interface for rdkFwupdateMgr external dependencies
 * Allows testing of rdkFwupdateMgr_handlers.c functions with controlled behavior
 */
class RdkFwupdateMgrInterface {
public:
    virtual ~RdkFwupdateMgrInterface() {}
    
    // XConf communication mocks
    //virtual int getXconfRespData(XCONFRES *pResponse, char *jsonData) = 0;
    virtual size_t GetServURL(char *pServURL, size_t szBufSize) = 0;
    virtual size_t createJsonString(char *pJSONStr, size_t szBufSize) = 0;
    virtual int allocDowndLoadDataMem(DownloadData *pDwnLoc, int size) = 0;
    virtual void freeDownLoadMem(DownloadData *pDwnLoc) = 0;
    
    // RFC settings mock
    virtual void getRFCSettings(Rfc_t *rfc_list) = 0;
    
    // Image details mocks
    virtual size_t currentImg(char *pCurImg, size_t szBufSize) = 0;
    virtual size_t GetFirmwareVersion(char *pFWVersion, size_t szBufSize) = 0;
    
    // File operations mocks
    virtual int filePresentCheck(const char *filename) = 0;
    virtual bool isConnectedToInternet() = 0;
    
    // Upgrade request mock
    virtual int rdkv_upgrade_request(RdkUpgradeContext_t *context, void **curl, int *pHttp_code) = 0;
};

/**
 * GMock class for rdkFwupdateMgr dependencies
 */
class RdkFwupdateMgrMock : public RdkFwupdateMgrInterface {
public:
    virtual ~RdkFwupdateMgrMock() {}
    
    //MOCK_METHOD(int, getXconfRespData, (XCONFRES *pResponse, char *jsonData), ());
    MOCK_METHOD(size_t, GetServURL, (char *pServURL, size_t szBufSize), ());
    MOCK_METHOD(size_t, createJsonString, (char *pJSONStr, size_t szBufSize), ());
    MOCK_METHOD(int, allocDowndLoadDataMem, (DownloadData *pDwnLoc, int size), ());
    MOCK_METHOD(void, freeDownLoadMem, (DownloadData *pDwnLoc), ());
    MOCK_METHOD(void, getRFCSettings, (Rfc_t *rfc_list), ());
    MOCK_METHOD(size_t, currentImg, (char *pCurImg, size_t szBufSize), ());
    MOCK_METHOD(size_t, GetFirmwareVersion, (char *pFWVersion, size_t szBufSize), ());
    MOCK_METHOD(int, filePresentCheck, (const char *filename), ());
    MOCK_METHOD(bool, isConnectedToInternet, (), ());
    MOCK_METHOD(int, rdkv_upgrade_request,(RdkUpgradeContext_t *context, void **curl, int *pHttp_code), ());
};

// Global mock pointer for C code to access
extern RdkFwupdateMgrMock *g_RdkFwupdateMgrMock;

#endif // RDKFWUPDATEMGR_MOCK_H

