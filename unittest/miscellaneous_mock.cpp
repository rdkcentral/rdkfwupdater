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

#include <gmock/gmock.h>
#include "miscellaneous.h"


class MockDownloadFile {
public:
    virtual ~MockDownloadFile() {}  // Add a virtual destructor
    virtual int downloadFile( int server_type, const char* artifactLocationUrl, const void* localDownloadLocation, char* pPostFields, int *httpCode ) =0;
    virtual int codebigdownloadFile( int server_type, const char* artifactLocationUrl, const void* localDownloadLocation, char* pPostFields, int *httpCode )=0;
};


class MockDownloadFileOps {
public:
    MOCK_METHOD(int, downloadFile, (int server_type, const char* artifactLocationUrl, const void* localDownloadLocation, char* pPostFields, int *httpCode), ());
    MOCK_METHOD(int, codebigdownloadFile, (int server_type, const char* artifactLocationUrl, const void* localDownloadLocation, char* pPostFields, int *httpCode), ());
};

MockDownloadFileOps* global_mockdownloadfileops_ptr;

extern "C" {
    int downloadFile(int server_type, const char* artifactLocationUrl, const void* localDownloadLocation, char* pPostFields, int *httpCode) {
        return global_mockdownloadfileops_ptr->downloadFile(server_type, artifactLocationUrl, localDownloadLocation, pPostFields, httpCode);
    }

    int codebigdownloadFile(int server_type, const char* artifactLocationUrl, const void* localDownloadLocation, char* pPostFields, int *httpCode) {
        return global_mockdownloadfileops_ptr->codebigdownloadFile(server_type, artifactLocationUrl, localDownloadLocation, pPostFields, httpCode);
    }
}

class MockExternal {
public:
    MOCK_METHOD(unsigned int, doGetDwnlBytes, (void*), ());
    MOCK_METHOD(int, doInteruptDwnl, (void*, unsigned int), ());
    MOCK_METHOD(void, setForceStop, (int), ());
    MOCK_METHOD(T2ERROR, t2_event_s, (char*, char*), ());
    MOCK_METHOD(T2ERROR, t2_event_d, (char*, int), ());
    MOCK_METHOD(void, t2_init, (char*), ());
    MOCK_METHOD(int, getDeviceProperties, (DeviceProperty_t*), ());
    MOCK_METHOD(int, getImageDetails, (ImageDetails_t*), ());
    MOCK_METHOD(int, createDir, (const char*), ());
    //MOCK_METHOD(void, createFile, (const char*), ());
    MOCK_METHOD(void, t2_uninit, (), ());
    MOCK_METHOD(void, log_exit, (), ());
    MOCK_METHOD(int, doHttpFileDownload, (void*, FileDwnl_t*, MtlsAuth_t*, unsigned int, char*, int*), ());
    MOCK_METHOD(int, logFileData, (const char*), ());
    MOCK_METHOD(bool, isMediaClientDevice, (), ());
    MOCK_METHOD(int, doAuthHttpFileDownload, (void*, FileDwnl_t*, int*), ());
    MOCK_METHOD(void, logMilestone, (const char*), ());
    MOCK_METHOD(int, eraseFolderExcePramaFile, (const char*, const char*, const char*), ());
    MOCK_METHOD(int, doCurlPutRequest, (void*, FileDwnl_t*, char*, int*), ());
    MOCK_METHOD(void, checkAndEnterStateRed, (int, const char*), ());
    MOCK_METHOD(int, getRFCSettings, (Rfc_t*), ());
    MOCK_METHOD(void, eventManager, (const char*, const char*), ());
    MOCK_METHOD(int, updateFWDownloadStatus, (struct FWDownloadStatus*, const char*), ());
    MOCK_METHOD(int, init_event_handler, (), ());
    MOCK_METHOD(int, isDwnlBlock, (int), ());
    MOCK_METHOD(bool, checkCodebigAccess, (), ());
    MOCK_METHOD(int, term_event_handler, (), ());
    MOCK_METHOD(int, isThrottleEnabled, (const char*, const char*, int), ());
    MOCK_METHOD(int, isOCSPEnable, (), ());
    MOCK_METHOD(int, getMtlscert, (MtlsAuth_t*), ());
    MOCK_METHOD(int, isIncremetalCDLEnable, (const char*), ());
    MOCK_METHOD(bool, isDelayFWDownloadActive, (int, const char*, int), ());
    MOCK_METHOD(bool, checkPDRIUpgrade, (const char*), ());
    MOCK_METHOD(bool, isUpgradeInProgress, (), ());
    MOCK_METHOD(bool, isMmgbleNotifyEnabled, (), ());
    MOCK_METHOD(time_t, getCurrentSysTimeSec, (), ());
    MOCK_METHOD(int, notifyDwnlStatus, (const char*, const char*, RFCVALDATATYPE), ());
    MOCK_METHOD(bool, updateOPTOUTFile, (const char*), ());
    MOCK_METHOD(bool, CheckIProuteConnectivity, (const char*), ());
    MOCK_METHOD(bool, isDnsResolve, (const char*), ());
    MOCK_METHOD(void, unsetStateRed, (), ());
    MOCK_METHOD(bool, checkForValidPCIUpgrade, (int, const char*, const char*, const char*), ());
    MOCK_METHOD(bool, isPDRIEnable, (), ());
    MOCK_METHOD(bool, lastDwnlImg, (char*, size_t), ());
    MOCK_METHOD(bool, currentImg, (char*, size_t), ());
    MOCK_METHOD(bool, CurrentRunningInst, (const char*), ());
    //MOCK_METHOD(void, eraseTGZItemsMatching, (const char*, const char*), ());
    MOCK_METHOD(bool, prevFlashedFile, (char*, size_t), ());
    MOCK_METHOD(int, doCodeBigSigning, (int, const char*, char*, size_t, char*, size_t), ());
};

MockExternal* global_mockexternal_ptr;

extern "C" {
    unsigned int doGetDwnlBytes(void *in_curl) {
        if (global_mockexternal_ptr == nullptr) {
            return 0; // Return default value if global_mockexternal_ptr is NULL
        }
        return global_mockexternal_ptr->doGetDwnlBytes(in_curl);
    }

    int doInteruptDwnl(void *in_curl, unsigned int max_dwnl_speed) {
        if (global_mockexternal_ptr == nullptr) {
            return 0; // Return default value if global_mockexternal_ptr is NULL
        }
        return global_mockexternal_ptr->doInteruptDwnl(in_curl, max_dwnl_speed);
    }

    void setForceStop(int value) {
        if (global_mockexternal_ptr == nullptr) {
            return; // Return default value if global_mockexternal_ptr is NULL
        }
        global_mockexternal_ptr->setForceStop(value);
    }

    T2ERROR t2_event_s(char* marker, char* value) {
        if (global_mockexternal_ptr == nullptr) {
            return T2ERROR_SUCCESS; // Return default value if global_mockexternal_ptr is NULL
        }
        return global_mockexternal_ptr->t2_event_s(marker, value);
    }

    T2ERROR t2_event_d(char* marker, int value) {
        if (global_mockexternal_ptr == nullptr) {
            return T2ERROR_SUCCESS; // Return default value if global_mockexternal_ptr is NULL
        }
        return global_mockexternal_ptr->t2_event_d(marker, value);
    }

    void t2_init(char *component) {
        if (global_mockexternal_ptr == nullptr) {
            return; // Return default value if global_mockexternal_ptr is NULL
        }
        global_mockexternal_ptr->t2_init(component);
    }

    int getImageDetails(ImageDetails_t *pImage_details) {
        if (global_mockexternal_ptr == nullptr) {
            return 0; // Return default value if global_mockexternal_ptr is NULL
        }
        return global_mockexternal_ptr->getImageDetails(pImage_details);
    }

    int createDir(const char *dirname) {
        if (global_mockexternal_ptr == nullptr) {
            return 0; // Return default value if global_mockexternal_ptr is NULL
        }
        return global_mockexternal_ptr->createDir(dirname);
    }
/*
    void createFile(const char *file_name) {
        if (global_mockexternal_ptr == nullptr) {
            FILE *file = fopen(file_name, "w");
            if (file == NULL) {
                printf("Failed to create file\n");
                return;
            }
            fclose(file);
            return; // Return default value if global_mockexternal_ptr is NULL
        }
        global_mockexternal_ptr->createFile(file_name);
    }
*/
    void t2_uninit(void) {
        if (global_mockexternal_ptr == nullptr) {
            return; // Return default value if global_mockexternal_ptr is NULL
        }
        global_mockexternal_ptr->t2_uninit();
    }

    void log_exit() {
        if (global_mockexternal_ptr == nullptr) {
            return; // Return default value if global_mockexternal_ptr is NULL
        }
        global_mockexternal_ptr->log_exit();
    }

    int doHttpFileDownload(void *in_curl, FileDwnl_t *pfile_dwnl, MtlsAuth_t *auth, unsigned int max_dwnl_speed, char *dnl_start_pos, int *out_httpCode) {
        if (global_mockexternal_ptr == nullptr) {
            return 0; // Return default value if global_mockexternal_ptr is NULL
        }
        return global_mockexternal_ptr->doHttpFileDownload(in_curl, pfile_dwnl, auth, max_dwnl_speed, dnl_start_pos, out_httpCode);
    }

    int logFileData(const char *file_path) {
        if (global_mockexternal_ptr == nullptr) {
            return 0; // Return default value if global_mockexternal_ptr is NULL
        }
        return global_mockexternal_ptr->logFileData(file_path);
    }

    bool isMediaClientDevice(void) {
        if (global_mockexternal_ptr == nullptr) {
            return false; // Return default value if global_mockexternal_ptr is NULL
        }
        return global_mockexternal_ptr->isMediaClientDevice();
    }

    int doAuthHttpFileDownload(void *in_curl, FileDwnl_t *pfile_dwnl, int *out_httpCode) {
        if (global_mockexternal_ptr == nullptr) {
            return 0; // Return default value if global_mockexternal_ptr is NULL
        }
        return global_mockexternal_ptr->doAuthHttpFileDownload(in_curl, pfile_dwnl, out_httpCode);
    }

    void logMilestone(const char *msg_code) {
        if (global_mockexternal_ptr == nullptr) {
            return; // Return default value if global_mockexternal_ptr is NULL
        }
        global_mockexternal_ptr->logMilestone(msg_code);
    }

    int eraseFolderExcePramaFile(const char *folder, const char* file_name, const char *model_num) {
        if (global_mockexternal_ptr == nullptr) {
            return 0; // Return default value if global_mockexternal_ptr is NULL
        }
        return global_mockexternal_ptr->eraseFolderExcePramaFile(folder, file_name, model_num);
    }

    int doCurlPutRequest(void *in_curl, FileDwnl_t *pfile_dwnl, char *jsonrpc_auth_token, int *out_httpCode) {
        if (global_mockexternal_ptr == nullptr) {
            return 0; // Return default value if global_mockexternal_ptr is NULL
        }
        return global_mockexternal_ptr->doCurlPutRequest(in_curl, pfile_dwnl, jsonrpc_auth_token, out_httpCode);
    }

    void checkAndEnterStateRed(int curlret, const char *) {
        if (global_mockexternal_ptr == nullptr) {
            return; // Return default value if global_mockexternal_ptr is NULL
        }
        global_mockexternal_ptr->checkAndEnterStateRed(curlret, "");
    }

    int getRFCSettings(Rfc_t *rfc_list) {
        if (global_mockexternal_ptr == nullptr) {
            return; // Return default value if global_mockexternal_ptr is NULL
        }
        return global_mockexternal_ptr->getRFCSettings(rfc_list);
    }

    int getDeviceProperties(DeviceProperty_t *pDevice_info) {
        if (global_mockexternal_ptr == nullptr) {
            return 0; // Return default value if global_mockexternal_ptr is NULL
        }
        return global_mockexternal_ptr->getDeviceProperties(pDevice_info);
    }
    // gtest

    void eventManager(const char *cur_event_name, const char *event_status) {
        if (global_mockexternal_ptr == nullptr) {
            return; // Return default value if global_mockexternal_ptr is NULL
        }
        global_mockexternal_ptr->eventManager(cur_event_name, event_status);
    }

    int updateFWDownloadStatus(struct FWDownloadStatus *fwdls, const char *disableStatsUpdate) {
        if (global_mockexternal_ptr == nullptr) {
            return 0; // Return default value if global_mockexternal_ptr is NULL
        }
        return global_mockexternal_ptr->updateFWDownloadStatus(fwdls, disableStatsUpdate);
    }

    int init_event_handler(void) {
        if (global_mockexternal_ptr == nullptr) {
            return 0; // Return default value if global_mockexternal_ptr is NULL
        }
        return global_mockexternal_ptr->init_event_handler();
    }

    int isDwnlBlock(int type) {
        if (global_mockexternal_ptr == nullptr) {
            return 0; // Return default value if global_mockexternal_ptr is NULL
        }
        return global_mockexternal_ptr->isDwnlBlock(type);
    }

    bool checkCodebigAccess(void) {
        if (global_mockexternal_ptr == nullptr) {
            return false; // Return default value if global_mockexternal_ptr is NULL
        }
        return global_mockexternal_ptr->checkCodebigAccess();
    }

    int term_event_handler(void) {
        if (global_mockexternal_ptr == nullptr) {
            return 0; // Return default value if global_mockexternal_ptr is NULL
        }
        return global_mockexternal_ptr->term_event_handler();
    }

    int isThrottleEnabled(const char *device_name, const char *reboot_immediate_flag, int app_mode) {
        if (global_mockexternal_ptr == nullptr) {
            return 0; // Return default value if global_mockexternal_ptr is NULL
        }
        return global_mockexternal_ptr->isThrottleEnabled(device_name, reboot_immediate_flag, app_mode);
    }

    int isOCSPEnable(void) {
        if (global_mockexternal_ptr == nullptr) {
            return 0; // Return default value if global_mockexternal_ptr is NULL
        }
        return global_mockexternal_ptr->isOCSPEnable();
    }

    int getMtlscert(MtlsAuth_t *sec) {
        if (global_mockexternal_ptr == nullptr) {
            return 0; // Return default value if global_mockexternal_ptr is NULL
        }
        return global_mockexternal_ptr->getMtlscert(sec);
    }

    int isIncremetalCDLEnable(const char *file_name) {
        if (global_mockexternal_ptr == nullptr) {
            return 0; // Return default value if global_mockexternal_ptr is NULL
        }
        return global_mockexternal_ptr->isIncremetalCDLEnable(file_name);
    }

    bool isDelayFWDownloadActive(int DelayDownloadXconf, const char *maint, int trigger_type) {
        if (global_mockexternal_ptr == nullptr) {
            return false; // Return default value if global_mockexternal_ptr is NULL
        }
        return global_mockexternal_ptr->isDelayFWDownloadActive(DelayDownloadXconf, maint, trigger_type);
    }

    bool checkPDRIUpgrade(const char *dwnl_pdri_img) {
        if (global_mockexternal_ptr == nullptr) {
            return false; // Return default value if global_mockexternal_ptr is NULL
        }
        return global_mockexternal_ptr->checkPDRIUpgrade(dwnl_pdri_img);
    }

    bool isUpgradeInProgress(void) {
        if (global_mockexternal_ptr == nullptr) {
            return false; // Return default value if global_mockexternal_ptr is NULL
        }
        return global_mockexternal_ptr->isUpgradeInProgress();
    }

    bool isMmgbleNotifyEnabled(void) {
        if (global_mockexternal_ptr == nullptr) {
            return false; // Return default value if global_mockexternal_ptr is NULL
        }
        return global_mockexternal_ptr->isMmgbleNotifyEnabled();
    }

    time_t getCurrentSysTimeSec(void) {
        if (global_mockexternal_ptr == nullptr) {
            return 0; // Return default value if global_mockexternal_ptr is NULL
        }
        return global_mockexternal_ptr->getCurrentSysTimeSec();
    }

    int notifyDwnlStatus(const char *key, const char *value, RFCVALDATATYPE datatype) {
        if (global_mockexternal_ptr == nullptr) {
            return 0; // Return default value if global_mockexternal_ptr is NULL
        }
        return global_mockexternal_ptr->notifyDwnlStatus(key, value, datatype);
    }
    bool updateOPTOUTFile(const char *value) {
        if (global_mockexternal_ptr == nullptr) {
            return 0; // Return default value if global_mockexternal_ptr is NULL
        }
        return global_mockexternal_ptr->updateOPTOUTFile(value);
    }


    bool CheckIProuteConnectivity(const char *file_name) {
        if (global_mockexternal_ptr == nullptr) {
            return false; // Return default value if global_mockexternal_ptr is NULL
        }
        return global_mockexternal_ptr->CheckIProuteConnectivity(file_name);
    }

    bool isDnsResolve(const char *dns_file_name) {
        if (global_mockexternal_ptr == nullptr) {
            return false; // Return default value if global_mockexternal_ptr is NULL
        }
        return global_mockexternal_ptr->isDnsResolve(dns_file_name);
    }

    void unsetStateRed(void) {
        if (global_mockexternal_ptr == nullptr) {
            return; // Return default value if global_mockexternal_ptr is NULL
        }
        global_mockexternal_ptr->unsetStateRed();
    }

    bool checkForValidPCIUpgrade(int trigger_type, const char *myfwversion, const char *cloudFWVersion, const char *cloudFWFile) {
        if (global_mockexternal_ptr == nullptr) {
            return false; // Return default value if global_mockexternal_ptr is NULL
        }
        return global_mockexternal_ptr->checkForValidPCIUpgrade(trigger_type, myfwversion, cloudFWVersion, cloudFWFile);
    }

    bool isPDRIEnable(void) {
        if (global_mockexternal_ptr == nullptr) {
            return false; // Return default value if global_mockexternal_ptr is NULL
        }
        return global_mockexternal_ptr->isPDRIEnable();
    }

    bool lastDwnlImg(char *img_name, size_t img_name_size) {
        if (global_mockexternal_ptr == nullptr) {
            return false; // Return default value if global_mockexternal_ptr is NULL
        }
        return global_mockexternal_ptr->lastDwnlImg(img_name, img_name_size);
    }
    bool currentImg(char *img_name, size_t img_name_size) {
        if (global_mockexternal_ptr == nullptr) {
            return false; // Return default value if global_mockexternal_ptr is NULL
        }
        return global_mockexternal_ptr->currentImg(img_name, img_name_size);
    }

    bool CurrentRunningInst(const char *file) {
        if (global_mockexternal_ptr == nullptr) {
            return false; // Return default value if global_mockexternal_ptr is NULL
        }
        return global_mockexternal_ptr->CurrentRunningInst(file);
    }
/*
    void eraseTGZItemsMatching(const char *path, const char *pattern) {
        if (global_mockexternal_ptr == nullptr) {
            return; // Return default value if global_mockexternal_ptr is NULL
        }
        global_mockexternal_ptr->eraseTGZItemsMatching(path, pattern);
    }
*/
    bool prevFlashedFile(char *img_name, size_t img_name_size) {
        if (global_mockexternal_ptr == nullptr) {
            return false; // Return default value if global_mockexternal_ptr is NULL
        }
        return global_mockexternal_ptr->prevFlashedFile(img_name, img_name_size);
    }

    int doCodeBigSigning(int server_type, const char* SignInput, char *signurl, size_t signurlsize, char *outhheader, size_t outHeaderSize) {
        if (global_mockexternal_ptr == nullptr) {
            return 0; // Return default value if global_mockexternal_ptr is NULL
        }
        return global_mockexternal_ptr->doCodeBigSigning(server_type, SignInput, signurl, signurlsize, outhheader, outHeaderSize);
    }
}

class MockFunctionsInternal {
public:
    MOCK_METHOD(void, RunCommand, (int command, void* arg1, char* jsondata, int size));
    MOCK_METHOD(void, getJRPCTokenData, (char* token, char* jsondata, int size));
    MOCK_METHOD(void*, doCurlInit, ());
    MOCK_METHOD(int, doCurlPutRequest, (void* Curl_req, FileDwnl_t* req_data, char* token_header, int* httpCode));
    MOCK_METHOD(void, doStopDownload, (void* Curl_req));
    MOCK_METHOD(bool, checkForValidPCIUpgrade, (int trigger_type, const char* cur_img_name, const char* cloudFWVersion, const char* cloudFWFile));
    MOCK_METHOD(int, getOPTOUTValue, (const char* path));
    MOCK_METHOD(void, uninitialize, (int status));
    MOCK_METHOD(int, upgradeRequest, (int upgrade_type, int http_ssr_direct, const char* imageHTTPURL, const char* dwlpath_filename, void* arg, int* http_code));
    MOCK_METHOD(bool, isPDRIEnable, ());
    MOCK_METHOD(int, filePresentCheck, (const char* path));
    MOCK_METHOD(int, peripheral_firmware_dndl, (const char* cloudFWLocation, const char* peripheralFirmwares));
};
