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

#include "rdkFwupdateMgr_mock.h"
#include <cstring>
#include <iostream>

using namespace std;

// Global mock pointer
RdkFwupdateMgrMock *g_RdkFwupdateMgrMock = nullptr;

// Global variables needed by handlers.c
extern "C" {
    // Device info structure used by rdkFwupdateMgr_handlers.c
    DeviceProperty_t device_info = {0};
    
    // Current image details structure
    ImageDetails_t cur_img_detail = {0};
}

// =============================================================================
// XConf communication functions
// =============================================================================

extern "C" int getXconfRespData(XCONFRES *pResponse, char *jsonData) {
    if (!g_RdkFwupdateMgrMock) {
        cerr << "getXconfRespData: g_RdkFwupdateMgrMock is NULL" << endl;
        return -1;
    }
    return g_RdkFwupdateMgrMock->getXconfRespData(pResponse, jsonData);
}

extern "C" size_t GetServURL(char *pServURL, size_t szBufSize) {
    if (!g_RdkFwupdateMgrMock) {
        cerr << "GetServURL: g_RdkFwupdateMgrMock is NULL" << endl;
        return 0;
    }
    return g_RdkFwupdateMgrMock->GetServURL(pServURL, szBufSize);
}

extern "C" size_t createJsonString(char *pJSONStr, size_t szBufSize) {
    if (!g_RdkFwupdateMgrMock) {
        cerr << "createJsonString: g_RdkFwupdateMgrMock is NULL" << endl;
        return 0;
    }
    return g_RdkFwupdateMgrMock->createJsonString(pJSONStr, szBufSize);
}

extern "C" int allocDowndLoadDataMem(DownloadData *pDwnLoc, int size) {
    if (!g_RdkFwupdateMgrMock) {
        cerr << "allocDowndLoadDataMem: g_RdkFwupdateMgrMock is NULL" << endl;
        return -1;
    }
    return g_RdkFwupdateMgrMock->allocDowndLoadDataMem(pDwnLoc, size);
}

extern "C" void freeDownLoadMem(DownloadData *pDwnLoc) {
    if (!g_RdkFwupdateMgrMock) {
        cerr << "freeDownLoadMem: g_RdkFwupdateMgrMock is NULL" << endl;
        return;
    }
    g_RdkFwupdateMgrMock->freeDownLoadMem(pDwnLoc);
}

// =============================================================================
// RFC settings function
// =============================================================================

extern "C" int getRFCSettings(Rfc_t *rfc_list) {
    if (!g_RdkFwupdateMgrMock) {
        cerr << "getRFCSettings: g_RdkFwupdateMgrMock is NULL" << endl;
        return -1;
    }
    g_RdkFwupdateMgrMock->getRFCSettings(rfc_list);
    return 0;
}

// =============================================================================
// Device/Image information functions
// =============================================================================

extern "C" size_t currentImg(char *pCurImg, size_t szBufSize) {
    if (!g_RdkFwupdateMgrMock) {
        cerr << "currentImg: g_RdkFwupdateMgrMock is NULL" << endl;
        return 0;
    }
    return g_RdkFwupdateMgrMock->currentImg(pCurImg, szBufSize);
}

extern "C" size_t GetFirmwareVersion(char *pFWVersion, size_t szBufSize) {
    if (!g_RdkFwupdateMgrMock) {
        cerr << "GetFirmwareVersion: g_RdkFwupdateMgrMock is NULL" << endl;
        return 0;
    }
    return g_RdkFwupdateMgrMock->GetFirmwareVersion(pFWVersion, szBufSize);
}

// =============================================================================
// File operations
// =============================================================================

extern "C" int filePresentCheck(const char *filename) {
    if (!g_RdkFwupdateMgrMock) {
        cerr << "filePresentCheck: g_RdkFwupdateMgrMock is NULL" << endl;
        return 0;
    }
    return g_RdkFwupdateMgrMock->filePresentCheck(filename);
}

extern "C" bool isConnectedToInternet() {
    if (!g_RdkFwupdateMgrMock) {
        cerr << "isConnectedToInternet: g_RdkFwupdateMgrMock is NULL" << endl;
        return false;
    }
    return g_RdkFwupdateMgrMock->isConnectedToInternet();
}

// =============================================================================
// Upgrade request function
// =============================================================================

extern "C" int rdkv_upgrade_request(const RdkUpgradeContext_t* context, void** curl, int* pHttp_code) {
    if (!g_RdkFwupdateMgrMock) {
        cerr << "rdkv_upgrade_request: g_RdkFwupdateMgrMock is NULL" << endl;
        return -1;
    }
    // Note: The mock interface uses a different signature for convenience
    // We need to extract the relevant fields from context
    RdkUpgradeContext_t* non_const_ctx = const_cast<RdkUpgradeContext_t*>(context);
    return g_RdkFwupdateMgrMock->rdkv_upgrade_request(non_const_ctx, context->lastrun, 
                                                       context->delay_dwnl, context->immed_reboot_flag,
                                                       context->disableStatsUpdate, context->rfc_list, 
                                                       *context->force_exit);
}

// =============================================================================
// Device API functions (from deviceutils/device_api.c)
// =============================================================================

extern "C" size_t GetEstbMac(char *pEstbMac, size_t szBufSize) {
    if (!g_RdkFwupdateMgrMock) {
        if (pEstbMac && szBufSize > 0) {
            strncpy(pEstbMac, "00:11:22:33:44:55", szBufSize - 1);
            pEstbMac[szBufSize - 1] = '\0';
        }
        return 17;
    }
    return g_RdkFwupdateMgrMock->GetFirmwareVersion(pEstbMac, szBufSize);
}

extern "C" size_t GetAdditionalFwVerInfo(char *pVerInfo, size_t szBufSize) {
    if (pVerInfo && szBufSize > 0) {
        strncpy(pVerInfo, "ADDITIONAL_INFO", szBufSize - 1);
        pVerInfo[szBufSize - 1] = '\0';
    }
    return strlen(pVerInfo);
}

extern "C" size_t GetBuildType(char *pBuildType, size_t szBufSize) {
    if (pBuildType && szBufSize > 0) {
        strncpy(pBuildType, "VBN", szBufSize - 1);
        pBuildType[szBufSize - 1] = '\0';
    }
    return 3;
}

extern "C" size_t GetModelNum(char *pModelNum, size_t szBufSize) {
    if (pModelNum && szBufSize > 0) {
        strncpy(pModelNum, "TEST_MODEL", szBufSize - 1);
        pModelNum[szBufSize - 1] = '\0';
    }
    return strlen(pModelNum);
}

extern "C" size_t GetMFRName(char *pMFRName, size_t szBufSize) {
    if (pMFRName && szBufSize > 0) {
        strncpy(pMFRName, "TEST_MFR", szBufSize - 1);
        pMFRName[szBufSize - 1] = '\0';
    }
    return strlen(pMFRName);
}

extern "C" size_t GetPartnerId(char *pPartnerId, size_t szBufSize) {
    if (pPartnerId && szBufSize > 0) {
        strncpy(pPartnerId, "comcast", szBufSize - 1);
        pPartnerId[szBufSize - 1] = '\0';
    }
    return strlen(pPartnerId);
}

extern "C" size_t GetOsClass(char *pOsClass, size_t szBufSize) {
    if (pOsClass && szBufSize > 0) {
        strncpy(pOsClass, "Linux", szBufSize - 1);
        pOsClass[szBufSize - 1] = '\0';
    }
    return strlen(pOsClass);
}

extern "C" size_t GetAccountID(char *pAccountID, size_t szBufSize) {
    if (pAccountID && szBufSize > 0) {
        strncpy(pAccountID, "123456789", szBufSize - 1);
        pAccountID[szBufSize - 1] = '\0';
    }
    return strlen(pAccountID);
}

extern "C" size_t GetExperience(char *pExperience, size_t szBufSize) {
    if (pExperience && szBufSize > 0) {
        strncpy(pExperience, "X1", szBufSize - 1);
        pExperience[szBufSize - 1] = '\0';
    }
    return strlen(pExperience);
}

extern "C" size_t GetMigrationReady(char *pMigrationReady, size_t szBufSize) {
    if (pMigrationReady && szBufSize > 0) {
        strncpy(pMigrationReady, "true", szBufSize - 1);
        pMigrationReady[szBufSize - 1] = '\0';
    }
    return 4;
}

extern "C" size_t GetSerialNum(char *pSerialNum, size_t szBufSize) {
    if (pSerialNum && szBufSize > 0) {
        strncpy(pSerialNum, "SERIAL123456", szBufSize - 1);
        pSerialNum[szBufSize - 1] = '\0';
    }
    return strlen(pSerialNum);
}

extern "C" size_t GetUTCTime(char *pUTCTime, size_t szBufSize) {
    if (pUTCTime && szBufSize > 0) {
        strncpy(pUTCTime, "1638614400", szBufSize - 1);
        pUTCTime[szBufSize - 1] = '\0';
    }
    return strlen(pUTCTime);
}

extern "C" size_t GetInstalledBundles(char *pBundles, size_t szBufSize) {
    if (pBundles && szBufSize > 0) {
        strncpy(pBundles, "bundle1,bundle2", szBufSize - 1);
        pBundles[szBufSize - 1] = '\0';
    }
    return strlen(pBundles);
}

extern "C" size_t GetRdmManifestVersion(char *pRdmVersion, size_t szBufSize) {
    if (pRdmVersion && szBufSize > 0) {
        strncpy(pRdmVersion, "1.0.0", szBufSize - 1);
        pRdmVersion[szBufSize - 1] = '\0';
    }
    return 5;
}

extern "C" size_t GetTimezone(char *pTimezone, size_t szBufSize) {
    if (pTimezone && szBufSize > 0) {
        strncpy(pTimezone, "America/New_York", szBufSize - 1);
        pTimezone[szBufSize - 1] = '\0';
    }
    return strlen(pTimezone);
}

extern "C" int waitForNtp() {
    return 0;  // Success
}

extern "C" size_t GetCapabilities(char *pCapabilities, size_t szBufSize) {
    if (pCapabilities && szBufSize > 0) {
        strncpy(pCapabilities, "cap1,cap2,cap3", szBufSize - 1);
        pCapabilities[szBufSize - 1] = '\0';
    }
    return strlen(pCapabilities);
}

extern "C" int getDevicePropertyData(const char *model, char *data, int size) {
    if (!data || size <= 0) {
        return -1;
    }
    
    if (strncmp(model, "CPU_ARCH", 8) == 0) {
        strncpy(data, "X86", size - 1);
    } else if (strncmp(model, "DEVICE_NAME", 11) == 0) {
        strncpy(data, "PLATCO", size - 1);
    } else {
        strncpy(data, "UNKNOWN", size - 1);
    }
    data[size - 1] = '\0';
    return 0;
}

// =============================================================================
// Utility functions used by json_process.c
// =============================================================================

extern "C" void t2ValNotify(char *marker, char *val) {
    // Stub - telemetry function
    printf("T2: %s = %s\n", marker ? marker : "NULL", val ? val : "NULL");
}

extern "C" void makeHttpHttps(char *url, size_t size, const char *proto) {
    // Stub - just ensure URL has protocol
    if (!url || !proto) return;
    // Simple stub implementation
}

extern "C" size_t lastDwnlImg(char *pLastImg, size_t szBufSize) {
    if (pLastImg && szBufSize > 0) {
        strncpy(pLastImg, "LAST_IMAGE_v1.0.0", szBufSize - 1);
        pLastImg[szBufSize - 1] = '\0';
    }
    return strlen(pLastImg);
}

extern "C" int v_secure_system(const char *command, ...) {
    // Stub - just return success
    return 0;
}

extern "C" void eventManager(int event_type, const char *event_data) {
    // Stub - event manager
    printf("EventManager: type=%d, data=%s\n", event_type, event_data ? event_data : "NULL");
}

extern "C" int processJsonResponse(XCONFRES *response, const char *myfwversion, const char *model, const char *maint) {
    // Stub - return success (firmware available)
    // In real code, this compares versions and validates the response
    if (!response || !myfwversion) {
        return -1;  // Error
    }
    // Return 0 for "update available", -1 for error, 1 for "no update needed"
    return 0;  // Default: update available
}

// =============================================================================
// D-Bus Server Stubs (for rdkFwupdateMgr_handlers.c)
// =============================================================================

extern "C" {
    // Forward declarations for types
    typedef struct _CurrentFlashState CurrentFlashState;
    
    #ifndef TRUE
    #define TRUE 1
    #define FALSE 0
    #endif
    
    #ifndef gboolean
    typedef int gboolean;
    #endif
    
    // Global flash state (used by cleanup_flash_state_idle in handlers.c)
    CurrentFlashState *current_flash = NULL;
    
    // Global RFC configuration (used by rdkFwupdateMgr_downloadFirmware in handlers.c)
    Rfc_t rfc_list = {0};
    
    // Flash status check function (defined in rdkv_dbus_server.c)
    gboolean IsFlashInProgress(void) {
        return (current_flash != NULL) ? TRUE : FALSE;
    }
}

// SWLOG stub functions - rdkv_cdl_log_wrapper.h defines these as macros,
// but rdkFwupdateMgr_handlers.c might call them as functions.
// We undefine the macros and provide stub functions instead.
#ifdef SWLOG_DEBUG
#undef SWLOG_DEBUG
#endif
#ifdef SWLOG_INFO
#undef SWLOG_INFO
#endif
#ifdef SWLOG_WARN
#undef SWLOG_WARN
#endif
#ifdef SWLOG_ERROR
#undef SWLOG_ERROR
#endif

extern "C" {
    void SWLOG_DEBUG(const char* format, ...) {
        // Stub - suppress output in tests
    }
    
    void SWLOG_INFO(const char* format, ...) {
        // Stub - suppress output in tests
    }
    
    void SWLOG_WARN(const char* format, ...) {
        // Stub - suppress output in tests
    }
    
    void SWLOG_ERROR(const char* format, ...) {
        // Stub - suppress output in tests
    }
}
