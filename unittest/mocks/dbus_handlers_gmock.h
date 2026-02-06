/*
 * Copyright 2025 Comcast Cable Communications Management, LLC
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
/**
 * @file dbus_handlers_gmock.h
 * @brief Google Mock header for D-Bus handlers unit tests
 * 
 * This header declares mock classes for all external dependencies
 * used by rdkFwupdateMgr_handlers.c and rdkv_dbus_server.c
 */

#ifndef DBUS_HANDLERS_GMOCK_H
#define DBUS_HANDLERS_GMOCK_H

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <sys/stat.h>

// Include C headers - they should have their own extern "C" guards
// Don't wrap them in extern "C" to avoid nested issues
#include "miscellaneous.h"
#include "json_process.h"
#include "rdkv_upgrade.h"  // For RdkUpgradeContext_t
#include "rdkv_cdl.h"
#include "rdkv_dbus_server.h"  // For CurrentFlashState
#include "deviceutils.h"
#include "device_api.h"
#include "rfcinterface.h"
#include "iarmInterface.h"
#include "flash.h"

/**
 * @brief Mock class for JSON processing functions
 */
class MockJsonProcess {
public:
    virtual ~MockJsonProcess() {}
    
    MOCK_METHOD(int, getXconfRespData, (XCONFRES *pResponse, char *pStr));
    MOCK_METHOD(int, processJsonResponse, (XCONFRES *pResponse, const char *cur_img_name,
                                           const char *device_model, const char *maint_status));
};

/**
 * @brief Mock class for device utility functions
 */
class MockDeviceUtils {
public:
    virtual ~MockDeviceUtils() {}
    
    // NOTE: GetServURL removed - using real implementation from device_api.c
    MOCK_METHOD(size_t, createJsonString, (char *jsonStr, size_t max_len));
    MOCK_METHOD(int, allocDowndLoadDataMem, (DownloadData *pDwnLoc, size_t size));
    MOCK_METHOD(char*, get_difw_path, ());
};

/**
 * @brief Mock class for RDK upgrade functions
 */
class MockRdkvUpgrade {
public:
    virtual ~MockRdkvUpgrade() {}
    
    MOCK_METHOD(int, rdkv_upgrade_request, (const RdkUpgradeContext_t *ctx, void **curl_handle, int *pHttp_code));
};

/**
 * @brief Mock class for device API functions
 */
class MockDeviceApi {
public:
    virtual ~MockDeviceApi() {}
    
    MOCK_METHOD(bool, GetFirmwareVersion, (char *buffer, size_t buffer_size));
    MOCK_METHOD(int, getDeviceProperties, (DeviceProperty_t *pDeviceInfo));
    MOCK_METHOD(int, filePresentCheck, (const char *filepath));
};

/**
 * @brief Mock class for RFC interface functions
 */
class MockRfcInterface {
public:
    virtual ~MockRfcInterface() {}
    
    MOCK_METHOD(int, getRFCSettings, (Rfc_t *pRfc));
};

/**
 * @brief Mock class for IARM interface functions
 */
class MockIarmInterface {
public:
    virtual ~MockIarmInterface() {}
    
    MOCK_METHOD(int, eventManager, (int event_type, int event_status));
};

/**
 * @brief Mock class for flash functions
 */
class MockFlash {
public:
    virtual ~MockFlash() {}
    
    MOCK_METHOD(int, flashImage, (const char *server_url, const char *upgrade_file,
                                  const char *reboot_flag, const char *proto,
                                  int upgrade_type, const char *maint, int trigger_type));
};

/**
 * @brief Mock class for system utility functions
 */
class MockSystemUtils {
public:
    virtual ~MockSystemUtils() {}
    
    MOCK_METHOD(int, system_call, (const char *command));
    MOCK_METHOD(int, unlink_call, (const char *pathname));
    MOCK_METHOD(int, stat_call, (const char *pathname, struct stat *statbuf));
    MOCK_METHOD(unsigned int, sleep_call, (unsigned int seconds));
    MOCK_METHOD(int, usleep_call, (useconds_t usec));
};

// Global mock instances
extern MockJsonProcess* mock_json_process;
extern MockDeviceUtils* mock_deviceutils;
extern MockRdkvUpgrade* mock_rdkv_upgrade;
extern MockDeviceApi* mock_device_api;
extern MockRfcInterface* mock_rfc_interface;
extern MockIarmInterface* mock_iarm_interface;
extern MockFlash* mock_flash;
extern MockSystemUtils* mock_system_utils;

// Helper functions
void InitializeMocks();
void CleanupMocks();
void SetupDefaultMocks();
void SetupFailureMocks();
void SetupCoverageTestMocks();
void ResetAllMocks();

// Extern C declarations for stub functions needed by real source files
#ifdef __cplusplus
extern "C" {
#endif

// NOTE: processJsonResponse, createJsonString, and getXconfRespData are already
// declared in json_process.h - we don't redeclare them here to avoid linkage conflicts

// Utility function stubs
int getDevicePropertyData(DeviceProperty_t* device_info, const char* property);
int waitForNtp(void);
// t2ValNotify is already declared in rdkv_cdl.h - don't redeclare
char* makeHttpHttps(const char* url);
int v_secure_system(const char* command);
int GetBuildType(char* buffer, size_t len);
int GetModelNum(char* buffer, size_t len);
int GetMFRName(char* buffer, size_t len);
int GetUTCTime(char* buffer, size_t len);
int GetTimezone(char* buffer, size_t len);
int GetCapabilities(char* buffer, size_t len);
char* stripinvalidchar(const char* input);
// read_RFCProperty is already declared in rfcinterface.h - don't redeclare
int GetHwMacAddress(char* buffer, size_t len);
int isInStateRed(void);
FILE* v_secure_popen(const char* direction, const char* command);
int v_secure_pclose(FILE* fp);
void* doCurlInit(void);
int getJsonRpcData(void* curl, const char* url, char** output);
void doStopDownload(void* curl);

// NOTE: SWLOG_* macros are already defined in rdkv_cdl_log_wrapper.h
// We don't redefine them here to avoid conflicts

// Global variables
extern DeviceProperty_t device_info;
extern ImageDetails_t cur_img_detail;
extern Rfc_t rfc_list;
extern char lastDwnlImg[256];
extern char currentImg[256];

#ifdef __cplusplus
}
#endif

// NOTE: current_flash and IsFlashInProgress are declared with C++ linkage 
// in rdkv_dbus_server.h, so we don't redeclare them to avoid linkage conflicts

#endif // DBUS_HANDLERS_GMOCK_H
