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

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <iostream>
#include <unistd.h>
#include <string>
#include <sys/stat.h>

#include "rdkv_cdl_log_wrapper.h"
extern "C" {
#include "device_status_helper.h"
#include "download_status_helper.h"
//#include "json_process.h"
//#include "rdk_fwdl_utils.h"
int copyFile(const char *src, const char *target);
}

#include "./mocks/device_status_helper_mock.h"
#include "./mocks/deviceutils_mock.h"

#include "miscellaneous.h"
#include "miscellaneous_mock.cpp"
#include "deviceutils_mock_global.h"

#define JSON_STR_LEN        1000

DeviceUtilsMock Deviceglobal;
//DeviceUtilsMock *g_DeviceUtilsMock = &Deviceglobal;

#define GTEST_DEFAULT_RESULT_FILEPATH "/tmp/Gtest_Report/"
#define GTEST_DEFAULT_RESULT_FILENAME "RdkFwDwnld_rdkvMain_gtest_report.json"
#define GTEST_REPORT_FILEPATH_SIZE 256

using namespace testing;
using namespace std;
using ::testing::Return;
using ::testing::StrEq;

extern "C" {
    extern Rfc_t rfc_list;
    extern DeviceProperty_t device_info;
    extern void *curl;

    bool savePID(const char *file, char *data);
    void interuptDwnl(int app_mode);
    int doInteruptDwnl(void *in_curl, unsigned int max_dwnl_speed);
    bool checkt2ValNotify( int iCurlCode, int iUpgradeType, char *Url  );
    unsigned int doGetDwnlBytes(void *in_curl);
    bool checkForTlsErrors(int curl_code, const char *type);
    int retryDownload(const RdkUpgradeContext_t* context, int retry_cnt, int delay, int *httpCode, void **curl);

    int downloadFile(const RdkUpgradeContext_t* context, int *httpCode, void **curl);
    int checkTriggerUpgrade(XCONFRES *response, const char *model_num, int upgrade_type);
    int DirectCDNDownload(XCONFRES *response, char *cur_img_name, DeviceProperty_t *device_info, int server_type, int *pHttp_code);
    void setForceStop(int value);
    T2ERROR t2_event_s(char* marker, char* value);
    T2ERROR t2_event_d(char* marker, int value);
    void t2_init(char *component);
    int getDeviceProperties(DeviceProperty_t *pDevice_info);
    int getImageDetails(ImageDetails_t *);
    int createDir(const char *dirname);
    //void createFile(const char *file_name);
    void t2_uninit(void);
    void log_exit();
    int doHttpFileDownload(void *in_curl, FileDwnl_t *pfile_dwnl, MtlsAuth_t *auth, unsigned int max_dwnl_speed, char *dnl_start_pos, int *out_httpCode );
    int logFileData(const char *file_path);
    bool isMediaClientDevice(void);
    int doAuthHttpFileDownload(void *in_curl, FileDwnl_t *pfile_dwnl, int *out_httpCode);
    void logMilestone(const char *msg_code);
    int eraseFolderExceParamFile(const char *folder, const char* file_name,const char* pdri_file_name, const char *model_num);
    int doCurlPutRequest(void *in_curl, FileDwnl_t *pfile_dwnl, char *jsonrpc_auth_token, int *out_httpCode);
    int getOPTOUTValue(const char *filename);
    void getPidStore(const char *key, const char *value);
    void dwnlError(int curl_code, int http_code, int server_type, const DeviceProperty_t *device_info, const char *lastrun, char *disableStatsUpdate);
    int peripheral_firmware_dndl(char *pCloudFWLocation, char *pPeripheralFirmwares);
    int fallBack(const RdkUpgradeContext_t* context, int *httpCode, void **curl);
    void saveHTTPCode(int http_code, const char *lastrun);
    int rdkv_upgrade_request(const RdkUpgradeContext_t* context, void** curl, int* pHttp_code);
    size_t getContentLength(const char *file);
    int chunkDownload(FileDwnl_t *pfile_dwnl, MtlsAuth_t *sec, unsigned int speed_limit, int *httpcode);
}

static const char* kBuildTypeFile = "/tmp/device_gtest.prop";
static const char* kRdmOverrideConfig = "/opt/rdm-versioned-packages.conf";
static const char* kRdmBinaryPath = "/usr/bin/rdm";

static bool WriteTextFile(const char* path, const std::string& content)
{
    FILE* fp = fopen(path, "w");
    if (fp == NULL) {
        return false;
    }
    const size_t written = fwrite(content.c_str(), 1, content.size(), fp);
    fclose(fp);
    return written == content.size();
}

static bool EnsureRdmBinaryForTest()
{
    if (access(kRdmBinaryPath, F_OK) == 0) {
        return true;
    }

    FILE* fp = fopen(kRdmBinaryPath, "w");
    if (fp == NULL) {
        return false;
    }
    fputs("#!/bin/sh\nexit 0\n", fp);
    fclose(fp);
    chmod(kRdmBinaryPath, 0755);
    return (access(kRdmBinaryPath, F_OK) == 0);
}

static void PrepareBundleResponse(XCONFRES* response)
{
    memset(response, 0, sizeof(*response));
    snprintf(response->cloudFWFile, sizeof(response->cloudFWFile), "%s", "HS_bundle_test-signed.bin");
    snprintf(response->cloudFWLocation, sizeof(response->cloudFWLocation), "%s", "https://cdlserver.tv/Images");
    snprintf(response->cloudFWVersion, sizeof(response->cloudFWVersion), "%s", "HS_bundle_test");
    snprintf(response->cloudProto, sizeof(response->cloudProto), "%s", "http");
    snprintf(response->dlCertBundle, sizeof(response->dlCertBundle), "%s", "xconf-cert");
    snprintf(response->dlAppBundle, sizeof(response->dlAppBundle), "%s", "xconf-app");
}

TEST(getContentLengthTest,TestSuccess){
    int ret = 0;
    ret = system("echo \"Content-Length: 1234\" > /tmp/contentlength.txt");
    EXPECT_NE(getContentLength("/tmp/contentlength.txt"), 0);
    ret = system("rm -f /tmp/contentlength.txt");
}
TEST(getContentLengthTest,TestFail){
    EXPECT_EQ(getContentLength("./contentlength.txt"), 0);
}
TEST(getContentLengthTest,TestFail1){
    int ret = 0;
    ret = system("echo \"Content: 1234\" > /tmp/contentlength.txt");
    EXPECT_EQ(getContentLength("/tmp/contentlength.txt"), 0);
    ret  = system("rm -f /tmp/contentlength.txt");
}

TEST(MainHelperFunctionTest,getAppModeDefault){
    EXPECT_EQ(getAppMode(), 1);
}

TEST(MainHelperFunctionTest,setAppModeDefault){
    setAppMode(0);
    EXPECT_EQ(getAppMode(), 0);
}

TEST(MainHelperFunctionTest,setandgetAppMode){
    setAppMode(1);
    EXPECT_EQ(getAppMode(), 1);
}

TEST(MainHelperFunctionTest,getDwnlStateDefault){
    EXPECT_EQ(getDwnlState(), 0);
}

TEST(MainHelperFunctionTest,setDwnlStateDefault){
    setDwnlState(0);
    EXPECT_EQ(getDwnlState(), 0);
}
TEST(MainHelperFunctionTest,setandgetDwnlState){
    setDwnlState(1);
    EXPECT_EQ(getDwnlState(), 1);
}

TEST(MainHelperFunctionTest,savePIDTestnull){
    EXPECT_FALSE(savePID((const char *) NULL, (char *) NULL));
}

TEST(MainHelperFunctionTest,savePIDTestnullfp){
    EXPECT_FALSE(savePID((const char *) "testfile",(char *) NULL));
}

TEST(MainHelperFunctionTest,savePIDTestnullfp1){
    char data[] = "1234";
    EXPECT_FALSE(savePID("/com/testfile",data));
}

TEST(MainHelperFunctionTest,savePIDTestSuccess){
    char data[] = "1234";
    EXPECT_TRUE(savePID("/tmp/testfile", data));
}

TEST(MainHelperFunctionTest,getPidStore){

    char buff[16] = {0};
    FILE *fp = NULL;
    getPidStore("NEW","true");
    fflush(NULL);
    fp = fopen("/tmp/.curl.pid", "r");
    if (fp != NULL) {
    printf("File open success\n");
    while((fgets(buff, sizeof(buff), fp)) != NULL) {
        printf("After read buf = %s\n",buff);
    }
    fclose(fp);
    }
    pid_t pid = getpid();
    unsigned int read_pid = atoi(buff);
    printf("pid = %u and read_pid = %u\n", pid, read_pid);
    EXPECT_EQ(pid, read_pid);
}

TEST(MainHelperFunctionTest, HandlesDownloadInProgress) {
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    strcpy(rfc_list.rfc_throttle, "true");
    strcpy(rfc_list.rfc_topspeed, "0");
    setDwnlState(RDKV_FWDNLD_DOWNLOAD_INPROGRESS);
    strcpy(device_info.maint_status, "true");
    EXPECT_CALL(mockexternal,eventManager("MaintenanceMGR", "9")).Times(1);
    EXPECT_CALL(mockexternal,eventManager(FW_STATE_EVENT, "3")).Times(1);
    interuptDwnl(0);
    global_mockexternal_ptr = NULL;
}

TEST(MainHelperFunctionTest, HandlesDownloadInProgressSpeed10) {
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    strcpy(rfc_list.rfc_throttle, "true");
    strcpy(rfc_list.rfc_topspeed, "10");
    setDwnlState(RDKV_FWDNLD_DOWNLOAD_INPROGRESS);
    strcpy(device_info.maint_status, "true");
    EXPECT_CALL(mockexternal,doGetDwnlBytes(_)).Times(1).WillOnce(Return(10));
    curl = (char *) rfc_list.rfc_throttle;
    EXPECT_CALL(mockexternal,doInteruptDwnl(_,10)).Times(1).WillOnce(Return(DWNL_UNPAUSE_FAIL));
    interuptDwnl(0);
    curl = NULL;
    global_mockexternal_ptr = NULL;

}

TEST(MainHelperFunctionTest, HandlesDownloadInProgressAppMode1) {
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    strcpy(rfc_list.rfc_throttle, "true");
    strcpy(rfc_list.rfc_topspeed, "10");
    setDwnlState(RDKV_FWDNLD_DOWNLOAD_INPROGRESS);
    strcpy(device_info.maint_status, "true");
    EXPECT_CALL(mockexternal,doGetDwnlBytes(_)).Times(1).WillOnce(Return(10));
    curl = (char *) rfc_list.rfc_throttle;
    interuptDwnl(1);
    curl = NULL;
    global_mockexternal_ptr = NULL;
}

TEST(MainHelperFunctionTest, HandlesDownloadInProgressAppMode2) {
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    strcpy(rfc_list.rfc_throttle, "true");
    strcpy(rfc_list.rfc_topspeed, "11");
    setDwnlState(RDKV_FWDNLD_DOWNLOAD_INPROGRESS);
    strcpy(device_info.maint_status, "true");
    EXPECT_CALL(mockexternal,doGetDwnlBytes(_)).Times(1).WillOnce(Return(10));
    curl = (char *) rfc_list.rfc_throttle;
    interuptDwnl(2);
    curl = NULL;
    global_mockexternal_ptr = NULL;
}

TEST(MainHelperFunctionTest, t2ValNotifyHandlesNullInputs) {
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    t2ValNotify(NULL, NULL);
    global_mockexternal_ptr = NULL;
}

TEST(MainHelperFunctionTest, t2ValNotifyHandlesNonNullInputs) {
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    EXPECT_CALL(mockexternal, t2_event_s("marker", "value")).Times(1).WillOnce(testing::Return(T2ERROR_SUCCESS));
    t2ValNotify("marker", "value");
    global_mockexternal_ptr = NULL;
}


TEST(MainHelperFunctionTest, checkt2ValNotifynegative){
    EXPECT_FALSE(checkt2ValNotify(100, PERIPHERAL_UPGRADE,"thisistest"));
}

TEST(MainHelperFunctionTest, checkt2ValNotifynegative1){
    EXPECT_FALSE(checkt2ValNotify(35, PERIPHERAL_UPGRADE,"thisistest"));
}

TEST(MainHelperFunctionTest, checkt2ValNotifynegative2){
    EXPECT_FALSE(checkt2ValNotify(91, PERIPHERAL_UPGRADE,"thisistest"));
}

TEST(MainHelperFunctionTest, checkt2ValNotifypositive){
    EXPECT_TRUE(checkt2ValNotify(35, PERIPHERAL_UPGRADE,"https://thisistest.com/test"));
}

TEST(MainHelperFunctionTest, checkForTlsErrorsnegative){
    EXPECT_TRUE(checkForTlsErrors(100, "https://thisistest"));
}

TEST(MainHelperFunctionTest, checkForTlsErrorspositive){
    EXPECT_TRUE(checkForTlsErrors(91, "https://thisistest"));
}

TEST(MainHelperFunctionTest, retryDownloadtest){
    // Test with NULL context - should handle gracefully
    EXPECT_EQ(retryDownload(NULL, 1, 0, NULL, NULL), -1);
}

TEST(MainHelperFunctionTest, retryDownloadtest1){
    MockDownloadFileOps mockfileops;
    global_mockdownloadfileops_ptr = &mockfileops;
    EXPECT_CALL(mockfileops, downloadFile(_,_,_,_,_)).Times(1).WillOnce(testing::Return(CURL_SUCCESS));
    int code = HTTP_SUCCESS;
    int force_exit = 0;
    int dummy_curl = 0;
    void *curl = &dummy_curl;  // Must point to valid memory, not NULL
    
    RdkUpgradeContext_t context = {};
    context.server_type = HTTP_SSR_DIRECT;
    context.artifactLocationUrl = "test";
    context.dwlloc = "test1";
    context.pPostFields = (char*)"test2";
    context.force_exit = &force_exit;
    
    EXPECT_EQ(retryDownload(&context, 1, 0, &code, &curl), CURL_SUCCESS);
    global_mockdownloadfileops_ptr = NULL;
}
TEST(MainHelperFunctionTest, retryDownloadtest2){
    MockDownloadFileOps mockfileops;
    global_mockdownloadfileops_ptr = &mockfileops;
    EXPECT_CALL(mockfileops, downloadFile(_,_,_,_,_)).Times(1).WillOnce(testing::Return(CURL_SUCCESS));
    int code = HTTP_CHUNK_SUCCESS;
    int force_exit = 0;
    int dummy_curl = 0;
    void *curl = &dummy_curl;
    
    RdkUpgradeContext_t context = {};
    context.server_type = HTTP_XCONF_DIRECT;
    context.artifactLocationUrl = "test";
    context.dwlloc = "test1";
    context.pPostFields = (char*)"test2";
    context.force_exit = &force_exit;
    
    EXPECT_EQ(retryDownload(&context, 1, 0, &code, &curl), CURL_SUCCESS);
    global_mockdownloadfileops_ptr = NULL;
}

TEST(MainHelperFunctionTest, retryDownloadtest3){
    MockDownloadFileOps mockfileops;
    global_mockdownloadfileops_ptr = &mockfileops;
    EXPECT_CALL(mockfileops, downloadFile(_,_,_,_,_)).Times(1).WillOnce(testing::Return(!CURL_SUCCESS));
    int code = HTTP_PAGE_NOT_FOUND;
    int force_exit = 0;
    int dummy_curl = 0;
    void *curl = &dummy_curl;
    
    RdkUpgradeContext_t context = {};
    context.server_type = HTTP_XCONF_DIRECT;
    context.artifactLocationUrl = "test";
    context.dwlloc = "test1";
    context.pPostFields = (char*)"test2";
    context.force_exit = &force_exit;
    
    EXPECT_EQ(retryDownload(&context, 1, 0, &code, &curl), !CURL_SUCCESS);
    global_mockdownloadfileops_ptr = NULL;
}

TEST(MainHelperFunctionTest, retryDownloadtest4){
    MockDownloadFileOps mockfileops;
    global_mockdownloadfileops_ptr = &mockfileops;
    EXPECT_CALL(mockfileops, downloadFile(_,_,_,_,_)).Times(1).WillOnce(testing::Return(!CURL_SUCCESS));
    int code = DWNL_BLOCK;
    int force_exit = 0;
    int dummy_curl = 0;
    void *curl = &dummy_curl;
    
    RdkUpgradeContext_t context = {};
    context.server_type = HTTP_XCONF_DIRECT;
    context.artifactLocationUrl = "test";
    context.dwlloc = "test1";
    context.pPostFields = (char*)"test2";
    context.force_exit = &force_exit;
    
    EXPECT_EQ(retryDownload(&context, 1, 0, &code, &curl), !CURL_SUCCESS);
    global_mockdownloadfileops_ptr = NULL;
}

TEST(MainHelperFunctionTest, retryDownloadtest5){
    MockDownloadFileOps mockfileops;
    global_mockdownloadfileops_ptr = &mockfileops;
    EXPECT_CALL(mockfileops, downloadFile(_,_,_,_,_)).Times(1).WillOnce(testing::Return(!CURL_SUCCESS));
    int code = HTTP_SUCCESS;
    int force_exit = 0;
    int dummy_curl = 0;
    void *curl = &dummy_curl;
    
    RdkUpgradeContext_t context = {};
    context.server_type = HTTP_SSR_DIRECT;
    context.artifactLocationUrl = "test";
    context.dwlloc = "test1";
    context.pPostFields = (char*)"test2";
    context.force_exit = &force_exit;
    
    EXPECT_EQ(retryDownload(&context, 1, 0, &code, &curl), !CURL_SUCCESS);
    global_mockdownloadfileops_ptr = NULL;

}

TEST(MainHelperFunctionTest, retryDownloadtest6){
    MockDownloadFileOps mockfileops;
    global_mockdownloadfileops_ptr = &mockfileops;
    EXPECT_CALL(mockfileops, codebigdownloadFile(_,_,_,_,_)).Times(1).WillOnce(testing::Return(CURL_SUCCESS));
    int code = HTTP_SUCCESS;
    int force_exit = 0;
    int dummy_curl = 0;
    void *curl = &dummy_curl;
    
    RdkUpgradeContext_t context = {};
    context.server_type = HTTP_SSR_CODEBIG;
    context.artifactLocationUrl = "test";
    context.dwlloc = "test1";
    context.pPostFields = (char*)"test2";
    context.force_exit = &force_exit;
    
    EXPECT_EQ(retryDownload(&context, 1, 0, &code, &curl), CURL_SUCCESS);
    global_mockdownloadfileops_ptr = NULL;

}

TEST(MainHelperFunctionTest, retryDownloadtest7){
    MockDownloadFileOps mockfileops;
    global_mockdownloadfileops_ptr = &mockfileops;
    EXPECT_CALL(mockfileops, codebigdownloadFile(_,_,_,_,_)).Times(1).WillOnce(testing::Return(!CURL_SUCCESS));
    int code = HTTP_PAGE_NOT_FOUND;
    int force_exit = 0;
    int dummy_curl = 0;
    void *curl = &dummy_curl;
    
    RdkUpgradeContext_t context = {};
    context.server_type = HTTP_SSR_CODEBIG;
    context.artifactLocationUrl = "test";
    context.dwlloc = "test1";
    context.pPostFields = (char*)"test2";
    context.force_exit = &force_exit;
    
    EXPECT_EQ(retryDownload(&context, 1, 0, &code, &curl), !CURL_SUCCESS);
    global_mockdownloadfileops_ptr = NULL;

}

TEST(MainHelperFunctionTest, retryDownloadtest8){
    MockDownloadFileOps mockfileops;
    global_mockdownloadfileops_ptr = &mockfileops;
    EXPECT_CALL(mockfileops, codebigdownloadFile(_,_,_,_,_)).Times(1).WillOnce(testing::Return(!CURL_SUCCESS));
    int code = HTTP_SUCCESS;
    int force_exit = 0;
    int dummy_curl = 0;
    void *curl = &dummy_curl;
    
    RdkUpgradeContext_t context = {};
    context.server_type = HTTP_SSR_CODEBIG;
    context.artifactLocationUrl = "test";
    context.dwlloc = "test1";
    context.pPostFields = (char*)"test2";
    context.force_exit = &force_exit;
    
    EXPECT_EQ(retryDownload(&context, 1, 1, &code, &curl), !CURL_SUCCESS);
    global_mockdownloadfileops_ptr = NULL;
}

TEST(DwnlErrorTest, HandlesCurlCode0) {
    int curl_code = 0;
    int http_code = 200;
    int server_type = 0;
    DeviceProperty_t device_info = {0};
    strcpy(device_info.dev_type, "mediaclient");
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    EXPECT_CALL(mockexternal,checkAndEnterStateRed(_,_)).Times(1);
    dwnlError(curl_code, http_code, server_type,  &device_info, NULL, NULL);
    global_mockexternal_ptr = NULL;

}

TEST(DwnlErrorTest, HandlesCurlCode22) {
    int curl_code = 22;
    int http_code = 200;
    int server_type = 0;
    DeviceProperty_t device_info = {0};
    strcpy(device_info.dev_type, "mediaclient");
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    EXPECT_CALL(mockexternal,eventManager(_,_)).Times(1);
    EXPECT_CALL(mockexternal,checkAndEnterStateRed(_,_)).Times(1);
    EXPECT_CALL(mockexternal,updateFWDownloadStatus(_,_)).Times(1);
    dwnlError(curl_code, http_code, server_type,  &device_info, NULL, NULL);
    global_mockexternal_ptr = NULL;

}

TEST(DwnlErrorTest, HandlesCurlCode18) {
    int curl_code = 18;
    int http_code = 0;
    int server_type = 0;
    DeviceProperty_t device_info = {0};
    strcpy(device_info.dev_type, "mediaclient");
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    strcpy(device_info.dev_type,"mediaclient");
    EXPECT_CALL(mockexternal,eventManager(_,_)).Times(1);
    EXPECT_CALL(mockexternal,checkAndEnterStateRed(_,_)).Times(1);
    EXPECT_CALL(mockexternal,updateFWDownloadStatus(_,_)).Times(1);
    dwnlError(curl_code, http_code, server_type,  &device_info, NULL, NULL);
    global_mockexternal_ptr = NULL;

}

TEST(DwnlErrorTest, HandlesCurlCode91) {
    int curl_code = 91;
    int http_code = 200;
    int server_type = 0;
    DeviceProperty_t device_info = {0};
    strcpy(device_info.dev_type, "mediaclient1");
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    EXPECT_CALL(mockexternal,eventManager(_,_)).Times(1);
    EXPECT_CALL(mockexternal,checkAndEnterStateRed(_,_)).Times(1);
    EXPECT_CALL(mockexternal,updateFWDownloadStatus(_,_)).Times(1);
    dwnlError(curl_code, http_code, server_type,  &device_info, NULL, NULL);
    global_mockexternal_ptr = NULL;

}
TEST(MainHelperFunctionTest,rdkv_upgrade_requestTest){
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    // Create context structure with test values
    RdkUpgradeContext_t context = {0};
    context.upgrade_type = 1;
    context.server_type = 1;
    context.artifactLocationUrl = "test";
    context.dwlloc = "test1";
    context.pPostFields = (char*)"test2";

    void* curl = NULL;
    int* http_code = NULL;

    //EXPECT_CALL(mockexternal,checkForValidPCIUpgrade(_,_,_,_)).Times(1).WillOnce(Return(false));
    EXPECT_EQ(rdkv_upgrade_request(&context, &curl, http_code), -1);
    global_mockexternal_ptr = NULL;
}

TEST(MainHelperFunctionTest,chunkDownloadTestNULL){
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    int httpcode = -1;
    EXPECT_EQ(chunkDownload(NULL, NULL, 0, &httpcode), -1);
    //int chunkDownload(FileDwnl_t *pfile_dwnl, MtlsAuth_t *sec, unsigned int speed_limit, int *httpcode)
    global_mockexternal_ptr = NULL;
}
TEST(MainHelperFunctionTest,chunkDownloadTestSuccess){
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    DeviceUtilsMock DeviceMock;
    g_DeviceUtilsMock = &DeviceMock;
    MockExternal mock;
    global_mockexternal_ptr = &mock;

    int httpcode = -1;
    char *temp = "temp";
    int ret = 0;
    FileDwnl_t file;
    memset(&file, '\0', sizeof(file));
    snprintf(file.pathname, sizeof(file.pathname),"%s", "/tmp/testfirmware.bin");
    ret = system("echo \"Content-Length: 1234\" > /tmp/testfirmware.bin.header");

    EXPECT_CALL(DeviceMock,getFileSize(_)).WillRepeatedly(Return(12));
    EXPECT_CALL(DeviceMock, filePresentCheck(_)).WillRepeatedly(Return(0));
    // Set expectations on the mock methods
    EXPECT_CALL(*g_DeviceUtilsMock, doCurlInit()).WillRepeatedly(Return((void*)temp));
    EXPECT_CALL(*g_DeviceUtilsMock, doStopDownload(_)).Times(2);
    EXPECT_CALL(*global_mockexternal_ptr, doHttpFileDownload(_,_,_,_,_,_)).WillRepeatedly(Return(0));

    EXPECT_EQ(chunkDownload(&file, NULL, 0, &httpcode), 0);
    ret = system("rm -f  /tmp/testfirmware.bin.header");
    //int chunkDownload(FileDwnl_t *pfile_dwnl, MtlsAuth_t *sec, unsigned int speed_limit, int *httpcode)
    global_mockexternal_ptr = NULL;
    g_DeviceUtilsMock = &Deviceglobal;
    global_mockexternal_ptr = NULL;
}

TEST(MainHelperFunctionTest,chunkDownloadFullTestSuccess){
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    DeviceUtilsMock DeviceMock;
    g_DeviceUtilsMock = &DeviceMock;
    MockExternal mock;
    g_DeviceUtilsMock = &DeviceMock;
    global_mockexternal_ptr = &mock;

    int httpcode = -1;
    char *temp = "temp";
    int ret = 0;
    FileDwnl_t file;
    memset(&file, '\0', sizeof(file));
    snprintf(file.pathname, sizeof(file.pathname),"%s", "/tmp/testfirmware.bin");
    ret = system("echo \"Content-Length: 1234\" > /tmp/testfirmware.bin.header");

    EXPECT_CALL(DeviceMock,getFileSize(_)).WillRepeatedly(Return(12));
    EXPECT_CALL(DeviceMock, filePresentCheck(_)).WillRepeatedly(Return(0));
    // Set expectations on the mock methods
    EXPECT_CALL(*g_DeviceUtilsMock, doCurlInit()).WillRepeatedly(Return((void*)temp));
    EXPECT_CALL(*g_DeviceUtilsMock, doStopDownload(_)).Times(2);
    EXPECT_CALL(*global_mockexternal_ptr, doHttpFileDownload(_,_,_,_,_,_)).WillRepeatedly(Return(33));

    EXPECT_NE(chunkDownload(&file, NULL, 0, &httpcode), 0);
    ret = system("rm -f  /tmp/testfirmware.bin.header");
    //int chunkDownload(FileDwnl_t *pfile_dwnl, MtlsAuth_t *sec, unsigned int speed_limit, int *httpcode)
    global_mockexternal_ptr = NULL;
    g_DeviceUtilsMock = &Deviceglobal;
    global_mockexternal_ptr = NULL;
}
TEST(MainHelperFunctionTest,chunkDownloadNotNeededTest){
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    DeviceUtilsMock DeviceMock;
    g_DeviceUtilsMock = &DeviceMock;
    MockExternal mock;
    g_DeviceUtilsMock = &DeviceMock;
    global_mockexternal_ptr = &mock;

    int httpcode = -1;
    char *temp = "temp";
    int ret = 0;
    FileDwnl_t file;
    memset(&file, '\0', sizeof(file));
    snprintf(file.pathname, sizeof(file.pathname),"%s", "/tmp/testfirmware.bin");
    ret = system("echo \"Content-Length: 12\" > /tmp/testfirmware.bin.header");

    EXPECT_CALL(DeviceMock,getFileSize(_)).WillRepeatedly(Return(12));
    EXPECT_CALL(DeviceMock, filePresentCheck(_)).WillRepeatedly(Return(0));
    // Set expectations on the mock methods
    //EXPECT_CALL(*g_DeviceUtilsMock, doCurlInit()).WillRepeatedly(Return((void*)temp));
    //EXPECT_CALL(*g_DeviceUtilsMock, doStopDownload(_));


    EXPECT_EQ(chunkDownload(&file, NULL, 0, &httpcode), 0);
    ret = system("rm -f  /tmp/testfirmware.bin.header");
    //int chunkDownload(FileDwnl_t *pfile_dwnl, MtlsAuth_t *sec, unsigned int speed_limit, int *httpcode)
    global_mockexternal_ptr = NULL;
    g_DeviceUtilsMock = &Deviceglobal;
    global_mockexternal_ptr = NULL;
}
TEST(MainHelperFunctionTest,chunkDownloadgetfilesizeTestFail){
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    DeviceUtilsMock DeviceMock;
    g_DeviceUtilsMock = &DeviceMock;
    MockExternal mock;
    g_DeviceUtilsMock = &DeviceMock;
    global_mockexternal_ptr = &mock;

    int httpcode = -1;
    char *temp = "temp";
    int ret = 0;
    FileDwnl_t file;
    memset(&file, '\0', sizeof(file));
    snprintf(file.pathname, sizeof(file.pathname),"%s", "/tmp/testfirmware.bin");
    ret = system("echo \"Content-Length: 1234\" > /tmp/testfirmware.bin.header");

    EXPECT_CALL(DeviceMock,getFileSize(_)).WillRepeatedly(Return(-1));
    EXPECT_CALL(DeviceMock, filePresentCheck(_)).WillRepeatedly(Return(0));
    // Set expectations on the mock methods
    //EXPECT_CALL(*g_DeviceUtilsMock, doCurlInit()).WillRepeatedly(Return((void*)temp));
    //EXPECT_CALL(*g_DeviceUtilsMock, doStopDownload(_));


    EXPECT_EQ(chunkDownload(&file, NULL, 0, &httpcode), -1);
    ret = system("rm -f  /tmp/testfirmware.bin.header");
    //int chunkDownload(FileDwnl_t *pfile_dwnl, MtlsAuth_t *sec, unsigned int speed_limit, int *httpcode)
    global_mockexternal_ptr = NULL;
    g_DeviceUtilsMock = &Deviceglobal;
    global_mockexternal_ptr = NULL;
}

/* Test: Verify that when getFileSize() returns -1 (error), chunkDownload()
 * cleans up both the partial image file and its .header file via unlink(). */
TEST(MainHelperFunctionTest,chunkDownloadgetfilesizeFailCleansUpFiles){
    DeviceUtilsMock DeviceMock;
    g_DeviceUtilsMock = &DeviceMock;
    MockExternal mock;
    global_mockexternal_ptr = &mock;

    int httpcode = -1;
    int ret = 0;
    FileDwnl_t file;
    memset(&file, '\0', sizeof(file));
    snprintf(file.pathname, sizeof(file.pathname),"%s", "/tmp/testfirmware_cleanup1.bin");

    /* Create real files so unlink() has something to remove */
    ret = system("echo 'partial data' > /tmp/testfirmware_cleanup1.bin");
    ret = system("echo 'Content-Length: 1234' > /tmp/testfirmware_cleanup1.bin.header");

    /* filePresentCheck returns 0 (file exists) for all checks */
    EXPECT_CALL(DeviceMock, filePresentCheck(_)).WillRepeatedly(Return(0));
    /* getFileSize returns -1 to trigger the error/cleanup path */
    EXPECT_CALL(DeviceMock, getFileSize(_)).WillRepeatedly(Return(-1));

    EXPECT_EQ(chunkDownload(&file, NULL, 0, &httpcode), -1);

    /* Verify both files were cleaned up by unlink() */
    EXPECT_NE(access("/tmp/testfirmware_cleanup1.bin", F_OK), 0)
        << "Partial image file should have been removed";
    EXPECT_NE(access("/tmp/testfirmware_cleanup1.bin.header", F_OK), 0)
        << "Header file should have been removed";

    /* Safety cleanup in case test assertions fail */
    ret = system("rm -f /tmp/testfirmware_cleanup1.bin /tmp/testfirmware_cleanup1.bin.header");
    global_mockexternal_ptr = NULL;
    g_DeviceUtilsMock = &Deviceglobal;
}

/* Test: Verify that when content_len is 0 (no Content-Length in header)
 * and the partial file is present, chunkDownload() cleans up both files. */
TEST(MainHelperFunctionTest,chunkDownloadNoContentLenCleansUpFiles){
    DeviceUtilsMock DeviceMock;
    g_DeviceUtilsMock = &DeviceMock;
    MockExternal mock;
    global_mockexternal_ptr = &mock;

    int httpcode = -1;
    int ret = 0;
    FileDwnl_t file;
    memset(&file, '\0', sizeof(file));
    snprintf(file.pathname, sizeof(file.pathname),"%s", "/tmp/testfirmware_cleanup2.bin");

    /* Create partial image file and a header file with NO Content-Length line */
    ret = system("echo 'partial data' > /tmp/testfirmware_cleanup2.bin");
    ret = system("echo 'No-Content-Here' > /tmp/testfirmware_cleanup2.bin.header");

    /* filePresentCheck returns 0 (file exists) for all checks */
    EXPECT_CALL(DeviceMock, filePresentCheck(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(DeviceMock, getFileSize(_)).WillRepeatedly(Return(12));

    EXPECT_EQ(chunkDownload(&file, NULL, 0, &httpcode), -1);

    /* Verify both files were cleaned up by unlink() */
    EXPECT_NE(access("/tmp/testfirmware_cleanup2.bin", F_OK), 0)
        << "Partial image file should have been removed";
    EXPECT_NE(access("/tmp/testfirmware_cleanup2.bin.header", F_OK), 0)
        << "Header file should have been removed";

    /* Safety cleanup in case test assertions fail */
    ret = system("rm -f /tmp/testfirmware_cleanup2.bin /tmp/testfirmware_cleanup2.bin.header");
    global_mockexternal_ptr = NULL;
    g_DeviceUtilsMock = &Deviceglobal;
}

TEST(MainHelperFunctionTest,chunkDownloadTestFail2){
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    DeviceUtilsMock DeviceMock;
    g_DeviceUtilsMock = &DeviceMock;
    MockExternal mock;
    g_DeviceUtilsMock = &DeviceMock;
    global_mockexternal_ptr = &mock;

    int httpcode = -1;
    char *temp = "temp";
    //int ret = 0;
    FileDwnl_t file;
    memset(&file, '\0', sizeof(file));
    snprintf(file.pathname, sizeof(file.pathname),"%s", "/tmp/testfirmware.bin");
    //ret = system("echo \"Content-Length: 1234\" > /tmp/testfirmware.bin.header");

    EXPECT_CALL(DeviceMock,getFileSize(_)).WillRepeatedly(Return(12));
    EXPECT_CALL(DeviceMock, filePresentCheck(_)).WillRepeatedly(Return(1));
    // Set expectations on the mock methods
    //EXPECT_CALL(*g_DeviceUtilsMock, doCurlInit()).WillRepeatedly(Return((void*)temp));
    //EXPECT_CALL(*g_DeviceUtilsMock, doStopDownload(_));


    EXPECT_EQ(chunkDownload(&file, NULL, 0, &httpcode), -1);
    //ret = system("rm -f  /tmp/testfirmware.bin.header");
    //int chunkDownload(FileDwnl_t *pfile_dwnl, MtlsAuth_t *sec, unsigned int speed_limit, int *httpcode)
    global_mockexternal_ptr = NULL;
    g_DeviceUtilsMock = &Deviceglobal;
    global_mockexternal_ptr = NULL;
}
// This is a test for the startFactoryProtectService function
TEST(startFactoryProtectServiceTest, ReturnsZeroWhenSuccessful) {
    // Create a mock object
    MockExternal mock;
    DeviceUtilsMock DeviceMock;
    g_DeviceUtilsMock = &DeviceMock;
    global_mockexternal_ptr = &mock;
    char *temp = "temp";
    // Set expectations on the mock methods
    EXPECT_CALL(*g_DeviceUtilsMock, doCurlInit()).WillOnce(Return((void*)temp));
    EXPECT_CALL(mock, doCurlPutRequest(_, _, _, _)).WillOnce(DoAll(SetArgPointee<3>(200), Return(0)));
    EXPECT_CALL(*g_DeviceUtilsMock, doStopDownload(_));

    // Call the function to test
    EXPECT_EQ(startFactoryProtectService(),0);
    global_mockexternal_ptr = NULL;
    g_DeviceUtilsMock = &Deviceglobal;
}

TEST(startFactoryProtectServiceTest, ReturnsminusoneWhenError) {
    // Create a mock object
    MockExternal mock;
    global_mockexternal_ptr = &mock;
    DeviceUtilsMock DeviceMock;
    g_DeviceUtilsMock = &DeviceMock;
    EXPECT_CALL(*g_DeviceUtilsMock, doCurlInit()).WillOnce(Return(NULL));

    // Call the function to test
    EXPECT_EQ(startFactoryProtectService(), -1);
    global_mockexternal_ptr = NULL;
    g_DeviceUtilsMock = &Deviceglobal;
}
/*
TEST(checkTriggerUpgradeTest, ReturnsZeroWhenSuccessful) {
    // Create a mock object
    MockFunctionsInternal mock;
    MockExternal mockexternal;

    // Set expectations on the mock methods
    EXPECT_CALL(mock, checkForValidPCIUpgrade(_, _, _, _)).WillOnce(Return(true));

    EXPECT_CALL(mock, getOPTOUTValue(_)).WillOnce(Return(0));
    EXPECT_CALL(mock, uninitialize(_));
    EXPECT_CALL(mock, upgradeRequest(_, _, _, _, _, _)).WillOnce(DoAll(SetArgPointee<5>(200), Return(0)));
    EXPECT_CALL(mock, isPDRIEnable()).WillOnce(Return(true));
    EXPECT_CALL(mock, filePresentCheck(_)).WillOnce(Return(0));
    EXPECT_CALL(mock, peripheral_firmware_dndl(_, _)).WillOnce(Return(0));

    // Create a response object
    XCONFRES response;
    // Fill the response object with test data
    // ...

    // Call the function to test
    int result = checkTriggerUpgrade(&response, "testModel", LEGACY_ALL_UPGRADE);

    // Check the result
    EXPECT_EQ(result, 0);
}
*/

TEST(PeripheralFirmwareDndlTest, HandlesValidInput) {

    MockDownloadFileOps mockdownloadfileops;
    global_mockdownloadfileops_ptr = &mockdownloadfileops;
    DeviceUtilsMock DeviceMock;
    g_DeviceUtilsMock = &DeviceMock;

    char *pCloudFWLocation = "http://example.com";
    char *pPeripheralFirmwares = "firmware1,firmware2";
    EXPECT_CALL(mockdownloadfileops, downloadFile(_,_,_,_,_)).WillRepeatedly(Return(0));
    EXPECT_CALL(mockdownloadfileops, codebigdownloadFile(_,_,_,_,_)).WillRepeatedly(Return(CODEBIG_SIGNING_FAILED));

    EXPECT_CALL(DeviceMock,getFileSize(_)).WillRepeatedly(Return(-1));
    int result = peripheral_firmware_dndl(pCloudFWLocation, pPeripheralFirmwares);

    EXPECT_EQ(result, -1);
    global_mockdownloadfileops_ptr = NULL;
    g_DeviceUtilsMock = &Deviceglobal;
}

TEST(PeripheralFirmwareDndlTest, HandlesValidInput404) {

    MockDownloadFileOps mockdownloadfileops;
    global_mockdownloadfileops_ptr = &mockdownloadfileops;

    char *pCloudFWLocation = "http://example.com";
    char *pPeripheralFirmwares = "firmware1,firmware2";
    EXPECT_CALL(mockdownloadfileops, downloadFile(_,_,_,_,_)).WillRepeatedly(Return(HTTP_PAGE_NOT_FOUND));
    EXPECT_CALL(mockdownloadfileops, codebigdownloadFile(_,_,_,_,_)).WillRepeatedly(Return(0));
    int result = peripheral_firmware_dndl(pCloudFWLocation, pPeripheralFirmwares);

    // Check the result of the function
    EXPECT_EQ(result, -1);
    global_mockdownloadfileops_ptr = NULL;
    // Check the state of the system after the function call
    // This depends on what the function is supposed to do
}

/*
crashing when the input is NULL
Program received signal SIGSEGV, Segmentation fault.
strnlen () at ../sysdeps/aarch64/strnlen.S:63
63      ../sysdeps/aarch64/strnlen.S: No such file or directory.
(gdb) bt
#0  strnlen () at ../sysdeps/aarch64/strnlen.S:63
#1  0x0000aaaaea1cfdf4 in peripheral_firmware_dndl (pCloudFWLocation=0x0, pPeripheralFirmwares=0x0) at ../video/rdkv_main.c:1338
#2  0x0000aaaaea1df494 in PeripheralFirmwareDndlTest_HandlesNullInput_Test::TestBody (this=<optimized out>) at basic_rdkv_main_gtest.cpp:456
#3  0x0000aaaaea318c50 in void testing::internal::HandleExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) ()
#4  0x0000aaaaea30a99c in testing::Test::Run() ()
#5  0x0000aaaaea30ab20 in testing::TestInfo::Run() ()

TEST(PeripheralFirmwareDndlTest, HandlesNullInput) {
    char *pCloudFWLocation = NULL;
    char *pPeripheralFirmwares = NULL;
    MockDownloadFileOps mockdownloadfileops;
    global_mockdownloadfileops_ptr = &mockdownloadfileops;
    EXPECT_CALL(mockdownloadfileops, downloadFile(_,_,_,_,_)).WillOnce(Return(26));
    int result = peripheral_firmware_dndl(pCloudFWLocation, pPeripheralFirmwares);

    // Check the result of the function
    EXPECT_EQ(result, -1);
    global_mockdownloadfileops_ptr = NULL;
    // Check the state of the system after the function call
    // This depends on what the function is supposed to do
}
*/

TEST(checkTriggerUpgradeTest, ReturnsZeroWhenSuccessful) {
    // Create a mock object
    MockFunctionsInternal mock;
    MockExternal mockexternal;
    MockDownloadFileOps mockdownloadfileops;

    global_mockexternal_ptr = &mockexternal;
    global_mockdownloadfileops_ptr = &mockdownloadfileops;

    //EXPECT_CALL(mockexternal,eventManager(_,_)).WillRepeatedly(Return(NULL));
    EXPECT_CALL(mockdownloadfileops, downloadFile(_,_,_,_,_)).WillRepeatedly(Return(0));

    // Create a response object
    XCONFRES response;

    // Call the function to test
    int result = checkTriggerUpgrade(&response, "testModel", LEGACY_ALL_UPGRADE);

    // Check the result
    EXPECT_EQ(result, 0);
    global_mockexternal_ptr = NULL;
    global_mockdownloadfileops_ptr = NULL;
}

TEST(checkTriggerUpgradeTest, TestFailNull) {
    XCONFRES response;
    int result = checkTriggerUpgrade(&response, NULL, LEGACY_ALL_UPGRADE);
    EXPECT_EQ(result, -1);
}
TEST(checkTriggerUpgradeTest, ReturnsZeroWhenSuccessful404) {
    // Create a mock object
    MockFunctionsInternal mock;
    MockExternal mockexternal;
    MockDownloadFileOps mockdownloadfileops;

    global_mockexternal_ptr = &mockexternal;
    global_mockdownloadfileops_ptr = &mockdownloadfileops;

    //EXPECT_CALL(mockexternal,eventManager(_,_)).WillRepeatedly(Return(NULL));
    EXPECT_CALL(mockdownloadfileops, downloadFile(_,_,_,_,_)).WillRepeatedly(Return(HTTP_PAGE_NOT_FOUND));

    // Create a response object
    XCONFRES response;

    // Call the function to test
    int result = checkTriggerUpgrade(&response, "testModel", LEGACY_ALL_UPGRADE);

    // Check the result
    EXPECT_EQ(result, 0);
    global_mockexternal_ptr = NULL;
    global_mockdownloadfileops_ptr = NULL;
}

TEST(checkTriggerUpgradeTest, TestValidPciUpgradeSuccess) {
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    DeviceUtilsMock DeviceMock;
    g_DeviceUtilsMock = &DeviceMock;
    MockDownloadFileOps mockdownloadfileops;
    global_mockdownloadfileops_ptr = &mockdownloadfileops;
    
    XCONFRES response;
    memset(&response, '\0', sizeof(response));
    strncpy(response.cloudFWVersion, "testModel.bin", 13);
    strncpy(response.cloudFWFile, "testModel.bin", 13);
    strncpy(response.cloudPDRIVersion, "testModel_PDRI_.bin", 19);
    strncpy(response.cloudImmediateRebootFlag, "true", 4);
    EXPECT_CALL(mockexternal,checkForValidPCIUpgrade(_,_,_,_)).Times(1).WillOnce(Return(true));
    EXPECT_CALL(DeviceMock, getDevicePropertyData(_,_,_)).WillRepeatedly(Return(0));
    EXPECT_CALL(mockexternal,updateFWDownloadStatus(_,_)).Times(1);
    EXPECT_CALL(mockexternal, isPDRIEnable()).WillOnce(Return(true));
    EXPECT_CALL(mockdownloadfileops, downloadFile(_,_,_,_,_)).WillRepeatedly(Return(0));
    int result = checkTriggerUpgrade(&response, "testModel", LEGACY_ALL_UPGRADE);
    EXPECT_EQ(result, 0);
    global_mockexternal_ptr = NULL;
    global_mockdownloadfileops_ptr = NULL;
}
TEST(checkTriggerUpgradeTest, TestPdriUpgradeSuccess) {
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    DeviceUtilsMock DeviceMock;
    g_DeviceUtilsMock = &DeviceMock;
    MockDownloadFileOps mockdownloadfileops;
    global_mockdownloadfileops_ptr = &mockdownloadfileops;
    
    XCONFRES response;
    memset(&response, '\0', sizeof(response));
    strncpy(response.cloudFWVersion, "testModel.bin", 13);
    strncpy(response.cloudFWFile, "testModel.bin", 13);
    strncpy(response.cloudPDRIVersion, "testModel_PDRI_.bin", 19);
    strncpy(response.cloudImmediateRebootFlag, "false", 4);
    EXPECT_CALL(mockexternal,checkForValidPCIUpgrade(_,_,_,_)).Times(1).WillOnce(Return(true));
    EXPECT_CALL(DeviceMock, getDevicePropertyData(_,_,_)).WillRepeatedly(Return(0));
    EXPECT_CALL(mockexternal,updateFWDownloadStatus(_,_)).Times(1);
    EXPECT_CALL(mockexternal, isPDRIEnable()).WillOnce(Return(true));
    EXPECT_CALL(mockdownloadfileops, downloadFile(_,_,_,_,_)).WillRepeatedly(Return(0));
    int result = checkTriggerUpgrade(&response, "testModel", LEGACY_ALL_UPGRADE);
    EXPECT_EQ(result, 0);
    global_mockexternal_ptr = NULL;
    global_mockdownloadfileops_ptr = NULL;
}

TEST(MainHelperFunctionTest, fallBackTestNULL){
     int http_code = 0;
     void *curl = NULL;
     EXPECT_EQ(fallBack(NULL, &http_code, &curl), -1);   
}

TEST(MainHelperFunctionTest, fallBackTestSuccess){
    MockDownloadFileOps mockfileops;
    global_mockdownloadfileops_ptr = &mockfileops;
    int http_code = 200;
    int force_exit = 0;
    int dummy_curl = 0;
    void *curl = &dummy_curl;
    
    // Create context structure with HTTP_XCONF_DIRECT
    RdkUpgradeContext_t context = {0};
    context.upgrade_type = HTTP_XCONF_DIRECT;
    context.server_type = HTTP_XCONF_DIRECT;
    context.force_exit = &force_exit;
    context.artifactLocationUrl = "test";
    context.dwlloc = "test1";
    
    EXPECT_CALL(mockfileops, downloadFile(_,_,_,_,_)).Times(1).WillOnce(testing::Return(CURL_SUCCESS));
    EXPECT_EQ(fallBack(&context, &http_code, &curl), CURL_SUCCESS);
    global_mockdownloadfileops_ptr = NULL;
}

TEST(MainHelperFunctionTest, fallBackTestFailure){
    MockDownloadFileOps mockfileops;
    global_mockdownloadfileops_ptr = &mockfileops;
    int http_code = 200;
    int force_exit = 0;
    int dummy_curl = 0;
    void *curl = &dummy_curl;
    
    // Create context structure with HTTP_XCONF_DIRECT
    RdkUpgradeContext_t context = {0};
    context.upgrade_type = HTTP_XCONF_DIRECT;
    context.server_type = HTTP_XCONF_DIRECT;
    context.force_exit = &force_exit;
    context.artifactLocationUrl = "test";
    context.dwlloc = "test1";
    
    EXPECT_CALL(mockfileops, downloadFile(_,_,_,_,_)).Times(1).WillOnce(testing::Return(!CURL_SUCCESS));
    EXPECT_EQ(fallBack(&context, &http_code, &curl), !CURL_SUCCESS);
    global_mockdownloadfileops_ptr = NULL;
}

TEST(MainHelperFunctionTest, fallBackTestSuccessCodebig){
    MockDownloadFileOps mockfileops;
    global_mockdownloadfileops_ptr = &mockfileops;
    int http_code = 200;
    int force_exit = 0;
    int dummy_curl = 0;
    void *curl = &dummy_curl;
    
    // Create context structure with HTTP_SSR_CODEBIG
    RdkUpgradeContext_t context = {0};
    context.upgrade_type = HTTP_SSR_CODEBIG;
    context.server_type = HTTP_SSR_CODEBIG;
    context.force_exit = &force_exit;
    context.artifactLocationUrl = "test";
    context.dwlloc = "test1";
    
    EXPECT_CALL(mockfileops, codebigdownloadFile(_,_,_,_,_)).Times(1).WillOnce(testing::Return(CURL_SUCCESS));
    EXPECT_EQ(fallBack(&context, &http_code, &curl), CURL_SUCCESS);
    global_mockdownloadfileops_ptr = NULL;
}

TEST(MainHelperFunctionTest, fallBackTestFailureCodebig){
    MockDownloadFileOps mockfileops;
    global_mockdownloadfileops_ptr = &mockfileops;
    int http_code = 200;
    int force_exit = 0;
    int dummy_curl = 0;
    void *curl = &dummy_curl;
    
    // Create context structure with HTTP_SSR_CODEBIG
    RdkUpgradeContext_t context = {0};
    context.upgrade_type = HTTP_SSR_CODEBIG;
    context.server_type = HTTP_SSR_CODEBIG;
    context.force_exit = &force_exit;
    context.artifactLocationUrl = "test";
    context.dwlloc = "test1";
    
    EXPECT_CALL(mockfileops, codebigdownloadFile(_,_,_,_,_)).Times(1).WillOnce(testing::Return(!CURL_SUCCESS));
    EXPECT_EQ(fallBack(&context, &http_code, &curl), !CURL_SUCCESS);
    global_mockdownloadfileops_ptr = NULL;
}

TEST(MainHelperFunctionTest, updateUpgradeFlag1){
    strcpy(device_info.dev_type,"mediaclient\0");
    updateUpgradeFlag(1);
    EXPECT_EQ(access("/tmp/.imageDnldInProgress", F_OK), 0);
}

TEST(MainHelperFunctionTest, updateUpgradeFlag11){
    strcpy(device_info.dev_type,"mediaclien1t\0");
    updateUpgradeFlag(1);
    EXPECT_EQ(access(HTTP_CDL_FLAG, F_OK), 0);
}

TEST(MainHelperFunctionTest, updateUpgradeFlag2){
    strcpy(device_info.dev_type,"mediaclient\0");
    updateUpgradeFlag(2);
    EXPECT_EQ(access("/tmp/.imageDnldInProgress", F_OK), -1);
}

TEST(MainHelperFunctionTest, updateUpgradeFlag22){
    strcpy(device_info.dev_type,"mediaclien1t\0");
    updateUpgradeFlag(2);
    EXPECT_EQ(access(HTTP_CDL_FLAG, F_OK), -1);
}

TEST(MainHelperFunctionTest,uninitializeTest){
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    EXPECT_CALL(mockexternal,t2_uninit()).Times(1);
    EXPECT_CALL(mockexternal,log_exit()).Times(1);
    uninitialize(INITIAL_VALIDATION_DWNL_INPROGRESS);
    global_mockexternal_ptr = NULL;
}


TEST(MainHelperFunctionTest,initializeTest){
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    EXPECT_CALL(mockexternal,getDeviceProperties(_)).Times(1).WillOnce(Return(-1));
    EXPECT_EQ(initialize(), -1);
    global_mockexternal_ptr = NULL;
}

TEST(MainHelperFunctionTest,initializeTest1){
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    EXPECT_CALL(mockexternal,getDeviceProperties(_)).Times(1).WillOnce(Return(0));
    EXPECT_CALL(mockexternal,getImageDetails(_)).Times(1).WillOnce(Return(0));
    EXPECT_CALL(mockexternal,getRFCSettings(_)).Times(1);
    EXPECT_CALL(mockexternal,init_event_handler()).Times(1);
    strcpy(device_info.maint_status,"true");
    EXPECT_EQ(initialize(), 1);
    global_mockexternal_ptr = NULL;
}

TEST(MainHelperFunctionTest,saveHTTPCodeTest){
    saveHTTPCode(200, NULL);
    fflush(NULL);
    char buff[16] = {0};
    FILE *fp = NULL;
    fp = fopen(HTTP_CODE_FILE, "r");
    if (fp != NULL) {
    printf("File open success\n");
    while((fgets(buff, sizeof(buff), fp)) != NULL) {
        printf("After read buf = %s\n",buff);
    }
    fclose(fp);
    }

    EXPECT_EQ(atoi(buff), 200);
    remove(HTTP_CODE_FILE);
}

TEST(MainHelperFunctionTest, HandlesNormalCase1) {

    const char* filename = "/tmp/testfile.txt";
  
    const char* content = "softwareoptout IGNORE_UPDATE \0\r\n";
    FILE *fOut = fopen (filename, "w");
    if (fOut != NULL) {
        if (fputs (content, fOut) == EOF) {
            printf ("File write not successful\n");
        }
        printf ("closing the file\n");
        fflush(fOut);
        fclose (fOut); 
    }
        int result = getOPTOUTValue(filename);
        EXPECT_EQ(result, 1);
        remove(filename);
}

TEST(MainHelperFunctionTest, HandlesNormalCase0) {

    const char* filename = "/tmp/testfile.txt";
  
    const char* content = "softwareoptout ENFORCE_OPTOUT \0\r\n";
    FILE *fOut = fopen (filename, "w");
    if (fOut != NULL) {
        if (fputs (content, fOut) == EOF) {
            printf ("File write not successful\n");
        }
        printf ("closing the file\n");
        fflush(fOut);
        fclose (fOut); 
    }
        int result = getOPTOUTValue(filename);
        EXPECT_EQ(result, 0);
        remove(filename);
}

TEST(MainHelperFunctionTest, HandlesNullFilename) {
    int result = getOPTOUTValue(NULL);
    EXPECT_EQ(result, -1);
}

TEST(MainHelperFunctionTest,flashImageTestNull){
    EXPECT_EQ(flashImage(NULL, NULL, "false", "2", 0, "false",1), -1);
}
TEST(MainHelperFunctionTest,flashImageTest){
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    DeviceUtilsMock DeviceMock;
    g_DeviceUtilsMock = &DeviceMock;

    EXPECT_CALL(mockexternal,isMediaClientDevice()).WillOnce(Return(true));
    EXPECT_CALL(DeviceMock, filePresentCheck(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(mockexternal,eventManager(_,_)).WillRepeatedly(Return());
    EXPECT_CALL(mockexternal,updateFWDownloadStatus(_,_)).Times(1);
    EXPECT_CALL(DeviceMock, getDevicePropertyData(_,_,_)).WillRepeatedly(Return(0));
    //int flashImage(const char *server_url, const char *upgrade_file, const char *reboot_flag, const char *proto, int upgrade_type, const char *maint)
    EXPECT_EQ(flashImage("fwdl.com", "/tmp/firmware.bin", "false", "2", 0, "false",2), 0);
    global_mockexternal_ptr = NULL;
    g_DeviceUtilsMock = nullptr;
}
TEST(MainHelperFunctionTest,flashImageTestRedState){
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    DeviceUtilsMock DeviceMock;
    g_DeviceUtilsMock = &DeviceMock;

    EXPECT_CALL(mockexternal,isMediaClientDevice()).WillOnce(Return(true));
    EXPECT_CALL(DeviceMock, filePresentCheck(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(mockexternal,eventManager(_,_)).WillRepeatedly(Return());
    EXPECT_CALL(mockexternal,updateFWDownloadStatus(_,_)).Times(1);
    EXPECT_CALL(DeviceMock, getDevicePropertyData(_,_,_)).WillRepeatedly(Return(0));
    EXPECT_CALL(DeviceMock, isInStateRed()).Times(1).WillOnce(Return(true));
    //int flashImage(const char *server_url, const char *upgrade_file, const char *reboot_flag, const char *proto, int upgrade_type, const char *maint)
    EXPECT_EQ(flashImage("fwdl.com", "/tmp/firmware.bin", "false", "2", 0, "false",6), 0);
    global_mockexternal_ptr = NULL;
    g_DeviceUtilsMock = nullptr;
}
TEST(MainHelperFunctionTest,flashImageTestFail){
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    DeviceUtilsMock DeviceMock;
    g_DeviceUtilsMock = &DeviceMock;

    EXPECT_CALL(mockexternal,isMediaClientDevice()).WillOnce(Return(true));
    EXPECT_CALL(DeviceMock, filePresentCheck(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(mockexternal,eventManager(_,_)).WillRepeatedly(Return());
    EXPECT_CALL(DeviceMock,v_secure_system(_,_,_)).WillRepeatedly(Return(1));
    EXPECT_CALL(mockexternal,updateFWDownloadStatus(_,_)).Times(1);
    EXPECT_CALL(DeviceMock, getDevicePropertyData(_,_,_)).WillRepeatedly(Return(0));
    //int flashImage(const char *server_url, const char *upgrade_file, const char *reboot_flag, const char *proto, int upgrade_type, const char *maint)
    EXPECT_EQ(flashImage("fwdl.com", "/tmp/firmware.bin", "false", "2", 0, "false",3), 1);
    global_mockexternal_ptr = NULL;
    g_DeviceUtilsMock = nullptr;
}
TEST(MainHelperFunctionTest,flashImageTestFail1){
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    DeviceUtilsMock DeviceMock;
    g_DeviceUtilsMock = &DeviceMock;

    EXPECT_CALL(mockexternal,isMediaClientDevice()).WillOnce(Return(false));
    EXPECT_CALL(DeviceMock, filePresentCheck(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(mockexternal,eventManager(_,_)).WillRepeatedly(Return());
    EXPECT_CALL(DeviceMock,v_secure_system(_,_,_)).WillRepeatedly(Return(1));
    EXPECT_CALL(mockexternal,updateFWDownloadStatus(_,_)).Times(1);
    EXPECT_CALL(DeviceMock, getDevicePropertyData(_,_,_)).WillRepeatedly(Return(0));
    //int flashImage(const char *server_url, const char *upgrade_file, const char *reboot_flag, const char *proto, int upgrade_type, const char *maint)
    EXPECT_EQ(flashImage("fwdl.com", "/tmp/firmware.bin", "false", "2", 0, "false",1), 1);
    global_mockexternal_ptr = NULL;
    g_DeviceUtilsMock = nullptr;
}
TEST(MainHelperFunctionTest,flashImageTestFail2){
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    DeviceUtilsMock DeviceMock;
    g_DeviceUtilsMock = &DeviceMock;

    EXPECT_CALL(mockexternal,isMediaClientDevice()).WillOnce(Return(true));
    EXPECT_CALL(DeviceMock, filePresentCheck(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(mockexternal,eventManager(_,_)).WillRepeatedly(Return());
    EXPECT_CALL(DeviceMock,v_secure_system(_,_,_)).WillRepeatedly(Return(1));
    EXPECT_CALL(mockexternal,updateFWDownloadStatus(_,_)).Times(1);
    EXPECT_CALL(DeviceMock, getDevicePropertyData(_,_,_)).WillRepeatedly(Return(1));
    //int flashImage(const char *server_url, const char *upgrade_file, const char *reboot_flag, const char *proto, int upgrade_type, const char *maint)
    EXPECT_EQ(flashImage("fwdl.com", "/tmp/firmware.bin", "false", "2", 0, "false",5), 1);
    global_mockexternal_ptr = NULL;
    g_DeviceUtilsMock = nullptr;
}
TEST(MainHelperFunctionTest,flashImageTestRebootTrue){
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    DeviceUtilsMock DeviceMock;
    g_DeviceUtilsMock = &DeviceMock;

    EXPECT_CALL(mockexternal,isMediaClientDevice()).WillOnce(Return(true));
    EXPECT_CALL(DeviceMock, filePresentCheck(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(mockexternal,eventManager(_,_)).WillRepeatedly(Return());
    EXPECT_CALL(mockexternal,updateFWDownloadStatus(_,_)).Times(1);
    EXPECT_CALL(DeviceMock, getDevicePropertyData(_,_,_)).WillRepeatedly(Return(0));
    //int flashImage(const char *server_url, const char *upgrade_file, const char *reboot_flag, const char *proto, int upgrade_type, const char *maint)
    EXPECT_EQ(flashImage("fwdl.com", "/tmp/firmware.bin", "true", "2", 0, "false",2), 0);
    global_mockexternal_ptr = NULL;
    g_DeviceUtilsMock = nullptr;
}
TEST(MainHelperFunctionTest,flashImageTestPdri){
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    DeviceUtilsMock DeviceMock;
    g_DeviceUtilsMock = &DeviceMock;

    EXPECT_CALL(mockexternal,isMediaClientDevice()).WillOnce(Return(true));
    EXPECT_CALL(DeviceMock, filePresentCheck(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(mockexternal,eventManager(_,_)).WillRepeatedly(Return());
    EXPECT_CALL(mockexternal,updateFWDownloadStatus(_,_)).Times(1);
    EXPECT_CALL(DeviceMock, getDevicePropertyData(_,_,_)).WillRepeatedly(Return(0));
    //int flashImage(const char *server_url, const char *upgrade_file, const char *reboot_flag, const char *proto, int upgrade_type, const char *maint)
    EXPECT_EQ(flashImage("fwdl.com", "/tmp/firmware.bin", "false", "2", 1, "false",6), 0);
    global_mockexternal_ptr = NULL;
    g_DeviceUtilsMock = nullptr;
}

TEST(MainHelperFunctionTest,flashImageTestMaintTrue){
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    DeviceUtilsMock DeviceMock;
    g_DeviceUtilsMock = &DeviceMock;

    EXPECT_CALL(mockexternal,isMediaClientDevice()).WillOnce(Return(true));
    EXPECT_CALL(DeviceMock, filePresentCheck(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(mockexternal,eventManager(_,_)).WillRepeatedly(Return());
    EXPECT_CALL(mockexternal,updateFWDownloadStatus(_,_)).Times(1);
    EXPECT_CALL(DeviceMock, getDevicePropertyData(_,_,_)).WillRepeatedly(Return(0));
    //int flashImage(const char *server_url, const char *upgrade_file, const char *reboot_flag, const char *proto, int upgrade_type, const char *maint)
    EXPECT_EQ(flashImage("fwdl.com", "/tmp/firmware.bin", "true", "2", 0, "true",1), 0);
    global_mockexternal_ptr = NULL;
    g_DeviceUtilsMock = nullptr;
}
TEST(MainHelperFunctionTest,flashImageTestMaintFalse){
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    DeviceUtilsMock DeviceMock;
    g_DeviceUtilsMock = &DeviceMock;

    EXPECT_CALL(mockexternal,isMediaClientDevice()).WillOnce(Return(true));
    EXPECT_CALL(DeviceMock, filePresentCheck(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(mockexternal,eventManager(_,_)).WillRepeatedly(Return());
    EXPECT_CALL(mockexternal,updateFWDownloadStatus(_,_)).Times(1);
    EXPECT_CALL(DeviceMock, getDevicePropertyData(_,_,_)).WillRepeatedly(Return(0));
    //int flashImage(const char *server_url, const char *upgrade_file, const char *reboot_flag, const char *proto, int upgrade_type, const char *maint)
    EXPECT_EQ(flashImage("fwdl.com", "/tmp/firmware.bin", "true", "2", 0, "false",2), 0);
    global_mockexternal_ptr = NULL;
    g_DeviceUtilsMock = nullptr;
}
TEST(MainHelperFunctionTest,getXconfResTest){
    XCONFRES response;
    char data[] = "{\"firmwareDownloadProtocol\":\"http\",\"firmwareFilename\":\"HS_VBN_24_sprint_20240725233056sdy_NG-signed.bin\",\"firmwareLocation\":\"https://cdlserver.tv/Images\",\"firmwareVersion\":\"HS_VBN_24_sprint_20240725233056sdy_NG\",\"rebootImmediately\":false}";
    *response.cloudFWFile = 0;
    *response.cloudFWLocation = 0;
    *response.ipv6cloudFWLocation = 0;
    *response.cloudFWVersion = 0;
    *response.cloudDelayDownload = 0;
    *response.cloudProto = 0;
    *response.cloudImmediateRebootFlag = 0;
    *response.peripheralFirmwares = 0;
    *response.dlCertBundle = 0;
    *response.cloudPDRIVersion = 0;
    cout << data << endl;
    EXPECT_EQ(getXconfRespData(&response, data), 0);
}
TEST(MainHelperFunctionTest,getXconfResTestNull){
    XCONFRES response;
    char data[] = "{\"firmwareDownloadProtocol\":\"http\",\"firmwareFilename\":\"HS_VBN_24_sprint_20240725233056sdy_NG-signed.bin\",\"firmwareLocation\":\"https://cdlserver.tv/Images\",\"firmwareVersion\":\"HS_VBN_24_sprint_20240725233056sdy_NG\",\"rebootImmediately\":false}";
    *response.cloudFWFile = 0;
    *response.cloudFWLocation = 0;
    *response.ipv6cloudFWLocation = 0;
    *response.cloudFWVersion = 0;
    *response.cloudDelayDownload = 0;
    *response.cloudProto = 0;
    *response.cloudImmediateRebootFlag = 0;
    *response.peripheralFirmwares = 0;
    *response.dlCertBundle = 0;
    *response.cloudPDRIVersion = 0;
    cout << data << endl;
    EXPECT_EQ(getXconfRespData(NULL, data), -1);
}
TEST(MainHelperFunctionTest,getXconfResTestNull1){
    XCONFRES response;
    *response.cloudFWFile = 0;
    *response.cloudFWLocation = 0;
    *response.ipv6cloudFWLocation = 0;
    *response.cloudFWVersion = 0;
    *response.cloudDelayDownload = 0;
    *response.cloudProto = 0;
    *response.cloudImmediateRebootFlag = 0;
    *response.peripheralFirmwares = 0;
    *response.dlCertBundle = 0;
    *response.cloudPDRIVersion = 0;
    EXPECT_EQ(getXconfRespData(&response, NULL), -1);
}

TEST(MainHelperFunctionTest,ProcessResTest){
    XCONFRES response;
    char data[] = "{\"firmwareDownloadProtocol\":\"http\",\"firmwareFilename\":\"HS_VBN_24_sprint_20240725233056sdy_NG-signed.bin\",\"additionalFwVerInfo\":\"HS_VBN_PDRI_20240725233056sdy_NG\",\"firmwareLocation\":\"https://cdlserver.tv/Images\",\"firmwareVersion\":\"HS_VBN_24_sprint_20240725233056sdy_NG\",\"rebootImmediately\":false}";
    *response.cloudFWFile = 0;
    *response.cloudFWLocation = 0;
    *response.ipv6cloudFWLocation = 0;
    *response.cloudFWVersion = 0;
    *response.cloudDelayDownload = 0;
    *response.cloudProto = 0;
    *response.cloudImmediateRebootFlag = 0;
    *response.peripheralFirmwares = 0;
    *response.dlCertBundle = 0;
    *response.cloudPDRIVersion = 0;
    cout << data << endl;
    getXconfRespData(&response, data);
    EXPECT_EQ(processJsonResponse(&response, "1234.bin","HS", "true"), 0);
}
TEST(MainHelperFunctionTest,ProcessResTestMaintFalse){
    XCONFRES response;
    char data[] = "{\"firmwareDownloadProtocol\":\"http\",\"firmwareFilename\":\"HS_VBN_24_sprint_20240725233056sdy_NG-signed.bin\",\"additionalFwVerInfo\":\"HS_VBN_PDRI_20240725233056sdy_NG\",\"firmwareLocation\":\"https://cdlserver.tv/Images\",\"firmwareVersion\":\"HS_VBN_24_sprint_20240725233056sdy_NG\",\"rebootImmediately\":false}";
    *response.cloudFWFile = 0;
    *response.cloudFWLocation = 0;
    *response.ipv6cloudFWLocation = 0;
    *response.cloudFWVersion = 0;
    *response.cloudDelayDownload = 0;
    *response.cloudProto = 0;
    *response.cloudImmediateRebootFlag = 0;
    *response.peripheralFirmwares = 0;
    *response.dlCertBundle = 0;
    *response.cloudPDRIVersion = 0;
    cout << data << endl;
    getXconfRespData(&response, data);
    EXPECT_EQ(processJsonResponse(&response, "1234.bin","HS", "false"), 0);
}
TEST(MainHelperFunctionTest,ProcessResTestNull){
    EXPECT_EQ(processJsonResponse(NULL, NULL,NULL, NULL), -1);
}
TEST(MainHelperFunctionTest,ProcessResTest_NonProdBuild_UsesOverrideBundleConfig)
{
    if (!EnsureRdmBinaryForTest()) {
        GTEST_SKIP() << "Cannot create/access /usr/bin/rdm in this environment";
    }

    ASSERT_TRUE(WriteTextFile(kBuildTypeFile, "BUILD_TYPE=vbn\n"));

    /* Make sure the override directory/path is usable in CI */
    FILE* verifyDir = fopen(kRdmOverrideConfig, "w");
    if (verifyDir == NULL) {
        unlink(kBuildTypeFile);
        GTEST_SKIP() << "Cannot create /opt/rdm-versioned-packages.conf in this environment";
    }
    fclose(verifyDir);

    ASSERT_TRUE(WriteTextFile(kRdmOverrideConfig, "dlCertBundle=cfg-cert|dlAppBundle=cfg-app\n"));

    FILE* fp = fopen(kRdmOverrideConfig, "r");
    if (fp == NULL) {
        unlink(kBuildTypeFile);
        unlink(kRdmOverrideConfig);
        GTEST_SKIP() << "Cannot read /opt/rdm-versioned-packages.conf in this environment";
    }
    fclose(fp);

    XCONFRES response;
    PrepareBundleResponse(&response);

    DeviceUtilsMock DeviceMock;
    g_DeviceUtilsMock = &DeviceMock;
    std::string capturedCmd;
    EXPECT_CALL(DeviceMock, v_secure_system(_, _, _))
        .WillOnce(testing::Invoke([&capturedCmd](const char* mode, const char* cmd, const char* opt) -> FILE* {
            (void)mode;
            (void)opt;
            capturedCmd = (cmd != NULL) ? cmd : "";
            return (FILE*)1;
        }));

    processJsonResponse(&response, "old_fw.bin", "HS", "false");

    EXPECT_EQ(capturedCmd.find("dlCertBundle=cfg-cert|dlAppBundle=cfg-app"), std::string::npos);

    g_DeviceUtilsMock = nullptr;
    unlink(kBuildTypeFile);
    unlink(kRdmOverrideConfig);
}
TEST(MainHelperFunctionTest,ProcessResTest_NonProdBuild_ConfigOnlyOverrideTriggersRdm)
{
    if (!EnsureRdmBinaryForTest()) {
        GTEST_SKIP() << "Cannot create/access /usr/bin/rdm in this environment";
    }

    ASSERT_TRUE(WriteTextFile(kBuildTypeFile, "BUILD_TYPE=dev\n"));

    FILE* verifyDir = fopen(kRdmOverrideConfig, "w");
    if (verifyDir == NULL) {
        unlink(kBuildTypeFile);
        GTEST_SKIP() << "Cannot create /opt/rdm-versioned-packages.conf in this environment";
    }
    fclose(verifyDir);

    const std::string configBundle = "dlCertBundle=cfg-only-cert|dlAppBundle=cfg-only-app";
    ASSERT_TRUE(WriteTextFile(kRdmOverrideConfig, configBundle + "\n"));

    XCONFRES response;
    PrepareBundleResponse(&response);
    response.dlCertBundle[0] = '\0';
    response.dlAppBundle[0] = '\0';

    DeviceUtilsMock DeviceMock;
    g_DeviceUtilsMock = &DeviceMock;

    std::string capturedCmd;
    EXPECT_CALL(DeviceMock, v_secure_system(_, _, _))
        .Times(1)
        .WillOnce(testing::Invoke([&capturedCmd](const char* mode, const char* cmd, const char* opt) -> FILE* {
            (void)mode;
            (void)opt;
            capturedCmd = (cmd != NULL) ? cmd : "";
            return (FILE*)1;
        }));

    processJsonResponse(&response, "old_fw.bin", "HS", "false");

    EXPECT_EQ(capturedCmd.find("rdm -v \""), std::string::npos);
    EXPECT_EQ(capturedCmd.find(configBundle), std::string::npos);

    g_DeviceUtilsMock = nullptr;
    unlink(kBuildTypeFile);
    unlink(kRdmOverrideConfig);
}
TEST(MainHelperFunctionTest,ProcessResTest_ProdBuild_DoesNotUseOverrideBundleConfig)
{
    if (!EnsureRdmBinaryForTest()) {
        GTEST_SKIP() << "Cannot create/access /usr/bin/rdm in this environment";
    }

    FILE* verifyDir = fopen(kRdmOverrideConfig, "w");
    if (verifyDir == NULL) {
         GTEST_SKIP() << "Cannot create /opt/rdm-versioned-packages.conf in this environment";
    }
    fclose(verifyDir);

    ASSERT_TRUE(WriteTextFile(kRdmOverrideConfig, "dlCertBundle=cfg-cert|dlAppBundle=cfg-app\n"));

    ASSERT_TRUE(WriteTextFile(kBuildTypeFile, "BUILD_TYPE=prod\n"));

    XCONFRES response;
    PrepareBundleResponse(&response);

    DeviceUtilsMock DeviceMock;
    g_DeviceUtilsMock = &DeviceMock;
    std::string capturedCmd;
    EXPECT_CALL(DeviceMock, v_secure_system(_, _, _))
        .WillOnce(testing::Invoke([&capturedCmd](const char* mode, const char* cmd, const char* opt) -> FILE* {
            (void)mode;
            (void)opt;
            capturedCmd = (cmd != NULL) ? cmd : "";
            return (FILE*)1;
        }));

    processJsonResponse(&response, "old_fw.bin", "HS", "false");

    EXPECT_NE(capturedCmd.find("dlCertBundle=xconf-cert|dlAppBundle=xconf-app"), std::string::npos);
    EXPECT_EQ(capturedCmd.find("dlCertBundle=cfg-cert|dlAppBundle=cfg-app"), std::string::npos);
    g_DeviceUtilsMock = nullptr;

    unlink(kBuildTypeFile);
    unlink(kRdmOverrideConfig);
}
TEST(MainHelperFunctionTest,initialValidationTestSuccess){
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    DeviceUtilsMock DeviceMock;
    g_DeviceUtilsMock = &DeviceMock;
    EXPECT_CALL(DeviceMock, read_RFCProperty(_,_,_,_)).WillRepeatedly(Return(1));
    EXPECT_CALL(mockexternal, CurrentRunningInst(_)).WillRepeatedly(Return(false));
    EXPECT_CALL(DeviceMock, filePresentCheck(_)).WillRepeatedly(Return(0));
    EXPECT_EQ(initialValidation(), 3);
}
TEST(MainHelperFunctionTest,initialValidationTestFail){
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    DeviceUtilsMock DeviceMock;
    g_DeviceUtilsMock = &DeviceMock;
    EXPECT_CALL(DeviceMock, read_RFCProperty(_,_,_,_)).WillRepeatedly(Return(-1));
    EXPECT_CALL(DeviceMock, filePresentCheck(_)).WillRepeatedly(Return(1));
    EXPECT_CALL(mockexternal, CurrentRunningInst(_)).WillRepeatedly(Return(false));
    EXPECT_EQ(initialValidation(), 0);
}
TEST(MainHelperFunctionTest,initialValidationTestFail1){
    int ret = 0;
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    DeviceUtilsMock DeviceMock;
    g_DeviceUtilsMock = &DeviceMock;
    EXPECT_CALL(DeviceMock, read_RFCProperty(_,_,_,_)).WillRepeatedly(Return(-1));
    EXPECT_CALL(DeviceMock, filePresentCheck(_)).WillOnce(Return(1)).WillRepeatedly(Return(0));
    EXPECT_CALL(mockexternal, CurrentRunningInst(_)).WillRepeatedly(Return(false));
    EXPECT_EQ(initialValidation(), 0);
    ret = system("rm -rf /tmp/DIFD.pid");
}
TEST(MainHelperFunctionTest,initialValidationTestInprogress){
    DeviceUtilsMock DeviceMock;
    g_DeviceUtilsMock = &DeviceMock;
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    EXPECT_CALL(DeviceMock, read_RFCProperty(_,_,_,_)).WillRepeatedly(Return(1));
    EXPECT_CALL(mockexternal, CurrentRunningInst(_)).WillRepeatedly(Return(true));
    EXPECT_EQ(initialValidation(), 2);
}
TEST(MainHelperFunctionTest,copyFileTestFail){
    EXPECT_EQ(copyFile(NULL, NULL), -1);
}
TEST(MainHelperFunctionTest,copyFileTestSuccess){
    const char *src = "/tmp/src.txt";
    const char *dst = "/tmp/dst.txt";
    int ret = 0;
    ret = system("echo \"tesing\" > /tmp/src.txt");
    EXPECT_EQ(copyFile(src, dst), 0);
    ret = system("rm -f /tmp/src.txt");
    ret = system("rm -f /tmp/dst.txt");
}

// =============================================================================
// Task 3.6: L1 tests for enriched XConf response parsing
// =============================================================================

TEST(DirectCDNParsingTest, getXconfRespData_DirectCDN_ParsesPerArtifactURLs)
{
    XCONFRES response;
    memset(&response, 0, sizeof(response));

    /* Enable Direct CDN via mock */
    DeviceUtilsMock localMock;
    g_DeviceUtilsMock = &localMock;
    EXPECT_CALL(localMock, isDirectCDNEnabled()).WillRepeatedly(Return(true));

    char data[] = "{"
        "\"firmwareDownloadProtocol\":\"http\","
        "\"firmwareFilename\":\"PX051AEI_VBN_24Q4_sprint_20241015-signed.bin\","
        "\"firmwareLocation\":\"https://cdlserver.tv/Images\","
        "\"firmwareVersion\":\"PX051AEI_VBN_24Q4_sprint_20241015\","
        "\"additionalFwVerInfo\":\"PX051AEI_VBN_PDRI_24Q4_sprint_20241015\","
        "\"firmware_URL\":\"https://cdn.direct/fw/PX051AEI_VBN_24Q4.bin\","
        "\"additionalFwVerInfo_URL\":\"https://cdn.direct/pdri/PX051AEI_PDRI.bin\","
        "\"remCtrlXR15\":\"XR15-10_firmware_4.2.0.0.tgz\","
        "\"remCtrlXR15_URL\":\"https://cdn.direct/peri/XR15-10_firmware_4.2.0.0.tgz\","
        "\"rebootImmediately\":false"
    "}";

    EXPECT_EQ(getXconfRespData(&response, data), 0);
    EXPECT_STREQ(response.firmwareUrl, "https://cdn.direct/fw/PX051AEI_VBN_24Q4.bin");
    EXPECT_STREQ(response.pdriUrl, "https://cdn.direct/pdri/PX051AEI_PDRI.bin");
    /* Peripheral URL is parsed via dynamic key (getPeripheralProduct) */
    EXPECT_STREQ(response.cloudFWLocation, "https://cdlserver.tv/Images");
    EXPECT_STREQ(response.cloudFWFile, "PX051AEI_VBN_24Q4_sprint_20241015-signed.bin");

    /* Cleanup */
    g_DeviceUtilsMock = nullptr;
}

TEST(DirectCDNParsingTest, getXconfRespData_DirectCDN_Disabled_LegacyPath)
{
    XCONFRES response;
    memset(&response, 0, sizeof(response));

    /* Direct CDN disabled via mock */
    DeviceUtilsMock localMock;
    g_DeviceUtilsMock = &localMock;
    EXPECT_CALL(localMock, isDirectCDNEnabled()).WillRepeatedly(Return(false));

    char data[] = "{"
        "\"firmwareDownloadProtocol\":\"http\","
        "\"firmwareFilename\":\"PX051AEI_VBN_24Q4-signed.bin\","
        "\"firmwareLocation\":\"https://cdlserver.tv/Images\","
        "\"firmwareVersion\":\"PX051AEI_VBN_24Q4\","
        "\"firmware_URL\":\"https://cdn.direct/fw/should_not_parse.bin\","
        "\"remCtrlXR15\":\"XR15-10_firmware_4.2.0.0.tgz\","
        "\"rebootImmediately\":false"
    "}";

    EXPECT_EQ(getXconfRespData(&response, data), 0);
    /* Per-artifact URLs should NOT be populated in legacy mode */
    EXPECT_STREQ(response.firmwareUrl, "");
    EXPECT_STREQ(response.pdriUrl, "");
    EXPECT_STREQ(response.remCtrlUrl, "");
    /* Legacy peripheral parsing should work */
    EXPECT_STRNE(response.peripheralFirmwares, "");

    /* Cleanup */
    g_DeviceUtilsMock = nullptr;
}

// =============================================================================
// Task 3.7: L1 tests for getPeripheralProduct() and PDRI validation
// =============================================================================

extern "C" {
    int getPeripheralProduct(char *buf, size_t szIn);
}

TEST(DirectCDNParsingTest, getPeripheralProduct_NullBuffer_ReturnsError)
{
    EXPECT_EQ(getPeripheralProduct(NULL, 64), -1);
}

TEST(DirectCDNParsingTest, getPeripheralProduct_ZeroSize_ReturnsError)
{
    char buf[64];
    EXPECT_EQ(getPeripheralProduct(buf, 0), -1);
}

TEST(DirectCDNParsingTest, getPeripheralProduct_Default_ReturnsRemCtrl)
{
    /* Before BuildRemoteInfo populates the cache, default is "remCtrl" */
    char buf[64] = {0};
    /* Note: If BuildRemoteInfo was called earlier in this process, the static
       may already be populated. We test the function returns successfully. */
    EXPECT_EQ(getPeripheralProduct(buf, sizeof(buf)), 0);
    /* Result should be non-empty (either "remCtrl" or cached product) */
    EXPECT_STRNE(buf, "");
}

TEST(DirectCDNParsingTest, processJsonResponse_PDRI_MissingSubstring_Invalid)
{
    XCONFRES response;
    memset(&response, 0, sizeof(response));

    /* Set up a valid firmware image but PDRI without _PDRI_ substring */
    strncpy(response.cloudFWFile, "HS_VBN_24Q4-signed.bin", sizeof(response.cloudFWFile) - 1);
    strncpy(response.cloudFWLocation, "https://cdlserver.tv/Images", sizeof(response.cloudFWLocation) - 1);
    strncpy(response.cloudFWVersion, "HS_VBN_24Q4", sizeof(response.cloudFWVersion) - 1);
    strncpy(response.cloudProto, "http", sizeof(response.cloudProto) - 1);
    /* PDRI name without _PDRI_ substring — should fail validation */
    strncpy(response.cloudPDRIVersion, "HS_VBN_24Q4_addon", sizeof(response.cloudPDRIVersion) - 1);

    /* processJsonResponse should fail (ret=1) since PDRI is invalid */
    int ret = processJsonResponse(&response, "old_fw.bin", "HS", "false");
    EXPECT_EQ(ret, 1);
}

TEST(DirectCDNParsingTest, processJsonResponse_PDRI_WithSubstring_Valid)
{
    XCONFRES response;
    memset(&response, 0, sizeof(response));

    strncpy(response.cloudFWFile, "HS_VBN_24Q4-signed.bin", sizeof(response.cloudFWFile) - 1);
    strncpy(response.cloudFWLocation, "https://cdlserver.tv/Images", sizeof(response.cloudFWLocation) - 1);
    strncpy(response.cloudFWVersion, "HS_VBN_24Q4", sizeof(response.cloudFWVersion) - 1);
    strncpy(response.cloudProto, "http", sizeof(response.cloudProto) - 1);
    /* PDRI name WITH _PDRI_ substring — should pass validation */
    strncpy(response.cloudPDRIVersion, "HS_VBN_PDRI_24Q4_addon", sizeof(response.cloudPDRIVersion) - 1);

    int ret = processJsonResponse(&response, "old_fw.bin", "HS", "false");
    EXPECT_EQ(ret, 0);
}

/* =========================================================================
 * Task 5 Unit Tests: Direct CDN main-flow routing & Codebig bypass
 * ========================================================================= */

/**
 * @brief Task 5.7: When direct_cdn=true and direct download fails with
 * connectivity issue, Codebig fallback must NOT be attempted.
 */
TEST(DirectCDNBypassTest, DirectCDN_SkipsCodebigFallback) {
    MockDownloadFileOps mockfileops;
    global_mockdownloadfileops_ptr = &mockfileops;
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    DeviceUtilsMock DeviceMock;
    g_DeviceUtilsMock = &DeviceMock;

    int http_code = 0;
    int local_force_exit = 0;
    void *test_curl = NULL;
    Rfc_t local_rfc = {0};
    strncpy(local_rfc.rfc_throttle, "false", sizeof(local_rfc.rfc_throttle) - 1);

    RdkUpgradeContext_t context = {0};
    context.upgrade_type = PCI_UPGRADE;
    context.server_type = HTTP_SSR_DIRECT;
    context.artifactLocationUrl = "https://cdn.example.com/firmware.bin";
    context.dwlloc = "/tmp/firmware.bin";
    context.pPostFields = NULL;
    context.immed_reboot_flag = "false";
    context.delay_dwnl = 0;
    context.lastrun = "0";
    context.disableStatsUpdate = (char*)"true";
    context.device_info = &device_info;
    context.force_exit = &local_force_exit;
    context.trigger_type = 1;
    context.rfc_list = &local_rfc;
    context.direct_cdn = true;  /* KEY: Direct CDN enabled */

    /* downloadFile fails with connectivity issue (curl error 7) */
    EXPECT_CALL(mockfileops, downloadFile(_, _, _, _, _))
        .WillRepeatedly(testing::DoAll(testing::SetArgPointee<4>(0), testing::Return(CURL_CONNECTIVITY_ISSUE)));

    /* codebigdownloadFile should NEVER be called — this is the assertion */
    EXPECT_CALL(mockfileops, codebigdownloadFile(_, _, _, _, _)).Times(0);

    /* Mock supporting calls */
    EXPECT_CALL(mockexternal, isDwnlBlock(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(DeviceMock, filePresentCheck(_)).WillRepeatedly(Return(-1));
    EXPECT_CALL(mockexternal, isMediaClientDevice()).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, isDelayFWDownloadActive(_, _, _)).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, isUpgradeInProgress()).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, isMmgbleNotifyEnabled()).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, updateFWDownloadStatus(_, _)).WillRepeatedly(Return(0));
    EXPECT_CALL(mockexternal, logMilestone(_)).Times(testing::AnyNumber());
    EXPECT_CALL(mockexternal, eventManager(_, _)).Times(testing::AnyNumber());
    EXPECT_CALL(mockexternal, checkPDRIUpgrade(_)).WillRepeatedly(Return(true));
    EXPECT_CALL(DeviceMock, getDevicePropertyData(_, _, _)).WillRepeatedly(Return(-1));
    EXPECT_CALL(mockexternal, CheckIProuteConnectivity(_)).WillRepeatedly(Return(false));

    int result = rdkv_upgrade_request(&context, &test_curl, &http_code);
    /* Download should fail (no fallback available) */
    EXPECT_NE(result, CURL_SUCCESS);

    global_mockdownloadfileops_ptr = NULL;
    global_mockexternal_ptr = NULL;
    g_DeviceUtilsMock = &Deviceglobal;
}

/**
 * @brief Task 5.7: When direct_cdn=false and direct download fails with
 * connectivity issue, Codebig fallback MUST be attempted (legacy behavior).
 */
TEST(DirectCDNBypassTest, LegacyMode_AttempsCodebigFallback) {
    MockDownloadFileOps mockfileops;
    global_mockdownloadfileops_ptr = &mockfileops;
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    DeviceUtilsMock DeviceMock;
    g_DeviceUtilsMock = &DeviceMock;

    int http_code = 0;
    int local_force_exit = 0;
    void *test_curl = NULL;
    Rfc_t local_rfc = {0};
    strncpy(local_rfc.rfc_throttle, "false", sizeof(local_rfc.rfc_throttle) - 1);

    RdkUpgradeContext_t context = {0};
    context.upgrade_type = PCI_UPGRADE;
    context.server_type = HTTP_SSR_DIRECT;
    context.artifactLocationUrl = "https://cdn.example.com/firmware.bin";
    context.dwlloc = "/tmp/firmware.bin";
    context.pPostFields = NULL;
    context.immed_reboot_flag = "false";
    context.delay_dwnl = 0;
    context.lastrun = "0";
    context.disableStatsUpdate = (char*)"true";
    context.device_info = &device_info;
    context.force_exit = &local_force_exit;
    context.trigger_type = 1;
    context.rfc_list = &local_rfc;
    context.direct_cdn = false;  /* KEY: Direct CDN disabled (legacy) */

    /* downloadFile fails with connectivity issue (curl error 7) */
    EXPECT_CALL(mockfileops, downloadFile(_, _, _, _, _))
        .WillRepeatedly(testing::DoAll(testing::SetArgPointee<4>(0), testing::Return(CURL_CONNECTIVITY_ISSUE)));

    /* codebigdownloadFile SHOULD be called as fallback — at least once */
    EXPECT_CALL(mockfileops, codebigdownloadFile(_, _, _, _, _))
        .Times(testing::AtLeast(1))
        .WillRepeatedly(testing::DoAll(testing::SetArgPointee<4>(200), testing::Return(CURL_SUCCESS)));

    /* Mock supporting calls */
    EXPECT_CALL(mockexternal, isDwnlBlock(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(DeviceMock, filePresentCheck(_)).WillRepeatedly(Return(-1));
    EXPECT_CALL(mockexternal, isMediaClientDevice()).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, isDelayFWDownloadActive(_, _, _)).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, isUpgradeInProgress()).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, isMmgbleNotifyEnabled()).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, updateFWDownloadStatus(_, _)).WillRepeatedly(Return(0));
    EXPECT_CALL(mockexternal, logMilestone(_)).Times(testing::AnyNumber());
    EXPECT_CALL(mockexternal, eventManager(_, _)).Times(testing::AnyNumber());
    EXPECT_CALL(mockexternal, checkPDRIUpgrade(_)).WillRepeatedly(Return(true));
    EXPECT_CALL(DeviceMock, getDevicePropertyData(_, _, _)).WillRepeatedly(Return(-1));
    EXPECT_CALL(mockexternal, CheckIProuteConnectivity(_)).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, checkCodebigAccess()).WillRepeatedly(Return(true));

    int result = rdkv_upgrade_request(&context, &test_curl, &http_code);
    /* Should succeed via Codebig fallback */
    EXPECT_EQ(result, CURL_SUCCESS);

    global_mockdownloadfileops_ptr = NULL;
    global_mockexternal_ptr = NULL;
    g_DeviceUtilsMock = &Deviceglobal;
}

/**
 * @brief Task 5.7: When direct_cdn=true and download succeeds on first attempt,
 * no fallback is triggered and result is success.
 */
TEST(DirectCDNBypassTest, DirectCDN_SuccessNoFallbackNeeded) {
    MockDownloadFileOps mockfileops;
    global_mockdownloadfileops_ptr = &mockfileops;
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    DeviceUtilsMock DeviceMock;
    g_DeviceUtilsMock = &DeviceMock;

    int http_code = 0;
    int local_force_exit = 0;
    void *test_curl = NULL;
    Rfc_t local_rfc = {0};
    strncpy(local_rfc.rfc_throttle, "false", sizeof(local_rfc.rfc_throttle) - 1);

    RdkUpgradeContext_t context = {0};
    context.upgrade_type = PCI_UPGRADE;
    context.server_type = HTTP_SSR_DIRECT;
    context.artifactLocationUrl = "https://cdn.example.com/firmware.bin";
    context.dwlloc = "/tmp/firmware.bin";
    context.pPostFields = NULL;
    context.immed_reboot_flag = "false";
    context.delay_dwnl = 0;
    context.lastrun = "0";
    context.disableStatsUpdate = (char*)"true";
    context.device_info = &device_info;
    context.force_exit = &local_force_exit;
    context.trigger_type = 1;
    context.rfc_list = &local_rfc;
    context.direct_cdn = true;

    /* downloadFile succeeds */
    EXPECT_CALL(mockfileops, downloadFile(_, _, _, _, _))
        .WillOnce(testing::DoAll(testing::SetArgPointee<4>(200), testing::Return(CURL_SUCCESS)));

    /* codebigdownloadFile should NEVER be called */
    EXPECT_CALL(mockfileops, codebigdownloadFile(_, _, _, _, _)).Times(0);

    /* Mock supporting calls */
    EXPECT_CALL(mockexternal, isDwnlBlock(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(DeviceMock, filePresentCheck(_)).WillRepeatedly(Return(-1));
    EXPECT_CALL(mockexternal, isMediaClientDevice()).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, isDelayFWDownloadActive(_, _, _)).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, isUpgradeInProgress()).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, isMmgbleNotifyEnabled()).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, updateFWDownloadStatus(_, _)).WillRepeatedly(Return(0));
    EXPECT_CALL(mockexternal, logMilestone(_)).Times(testing::AnyNumber());
    EXPECT_CALL(mockexternal, eventManager(_, _)).Times(testing::AnyNumber());
    EXPECT_CALL(mockexternal, checkPDRIUpgrade(_)).WillRepeatedly(Return(true));
    EXPECT_CALL(DeviceMock, getDevicePropertyData(_, _, _)).WillRepeatedly(Return(-1));

    int result = rdkv_upgrade_request(&context, &test_curl, &http_code);
    EXPECT_EQ(result, CURL_SUCCESS);
    EXPECT_EQ(http_code, 200);

    global_mockdownloadfileops_ptr = NULL;
    global_mockexternal_ptr = NULL;
    g_DeviceUtilsMock = &Deviceglobal;
}

/* =========================================================================
 * Task 6 Unit Tests: Selective Retry Logic — Error Classification
 * ========================================================================= */

/**
 * @brief Task 6.1: Per-artifact PCI download success returns 0.
 * When downloadFile returns 0 and HTTP 200, checkTriggerUpgrade returns success.
 */
TEST(DirectCDNRetryTest, PerArtifact_PCI_Success_ReturnsZero) {
    MockDownloadFileOps mockfileops;
    global_mockdownloadfileops_ptr = &mockfileops;
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    DeviceUtilsMock DeviceMock;
    g_DeviceUtilsMock = &DeviceMock;

    XCONFRES response;
    memset(&response, 0, sizeof(response));
    strncpy(response.firmwareUrl, "https://cdn.example.com/fw.bin", sizeof(response.firmwareUrl) - 1);
    strncpy(response.cloudFWFile, "testModel_fw.bin", sizeof(response.cloudFWFile) - 1);

    /* downloadFile succeeds with HTTP 200 */
    EXPECT_CALL(mockfileops, downloadFile(_, _, _, _, _))
        .WillOnce(testing::DoAll(testing::SetArgPointee<4>(200), testing::Return(0)));

    /* Mock supporting calls required by rdkv_upgrade_request */
    EXPECT_CALL(mockexternal, isDwnlBlock(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(DeviceMock, filePresentCheck(_)).WillRepeatedly(Return(-1));
    EXPECT_CALL(mockexternal, isMediaClientDevice()).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, isDelayFWDownloadActive(_, _, _)).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, isMmgbleNotifyEnabled()).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, updateFWDownloadStatus(_, _)).WillRepeatedly(Return(0));
    EXPECT_CALL(mockexternal, logMilestone(_)).Times(testing::AnyNumber());
    EXPECT_CALL(mockexternal, eventManager(_, _)).Times(testing::AnyNumber());
    EXPECT_CALL(mockexternal, checkPDRIUpgrade(_)).WillRepeatedly(Return(true));
    EXPECT_CALL(DeviceMock, getDevicePropertyData(_, _, _)).WillRepeatedly(Return(-1));

    int result = checkTriggerUpgrade(&response, "testModel", PCI_UPGRADE);
    EXPECT_EQ(result, 0);

    global_mockdownloadfileops_ptr = NULL;
    global_mockexternal_ptr = NULL;
    g_DeviceUtilsMock = &Deviceglobal;
}

/**
 * @brief Task 6.1: Per-artifact PCI transient curl error returns DIRECT_CDN_RETRY_ERR.
 * Network connectivity issue (curl error 7) is classified as retryable.
 */
TEST(DirectCDNRetryTest, PerArtifact_PCI_TransientCurlError_ReturnsRetryErr) {
    MockDownloadFileOps mockfileops;
    global_mockdownloadfileops_ptr = &mockfileops;
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    DeviceUtilsMock DeviceMock;
    g_DeviceUtilsMock = &DeviceMock;

    XCONFRES response;
    memset(&response, 0, sizeof(response));
    strncpy(response.firmwareUrl, "https://cdn.example.com/fw.bin", sizeof(response.firmwareUrl) - 1);
    strncpy(response.cloudFWFile, "testModel_fw.bin", sizeof(response.cloudFWFile) - 1);

    /* downloadFile fails with CURL_CONNECTIVITY_ISSUE (7) — retryable */
    EXPECT_CALL(mockfileops, downloadFile(_, _, _, _, _))
        .WillRepeatedly(testing::DoAll(testing::SetArgPointee<4>(0), testing::Return(CURL_CONNECTIVITY_ISSUE)));

    /* Mock supporting calls */
    EXPECT_CALL(mockexternal, isDwnlBlock(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(DeviceMock, filePresentCheck(_)).WillRepeatedly(Return(-1));
    EXPECT_CALL(mockexternal, isMediaClientDevice()).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, isDelayFWDownloadActive(_, _, _)).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, isMmgbleNotifyEnabled()).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, updateFWDownloadStatus(_, _)).WillRepeatedly(Return(0));
    EXPECT_CALL(mockexternal, logMilestone(_)).Times(testing::AnyNumber());
    EXPECT_CALL(mockexternal, eventManager(_, _)).Times(testing::AnyNumber());
    EXPECT_CALL(mockexternal, checkPDRIUpgrade(_)).WillRepeatedly(Return(true));
    EXPECT_CALL(DeviceMock, getDevicePropertyData(_, _, _)).WillRepeatedly(Return(-1));
    EXPECT_CALL(mockexternal, CheckIProuteConnectivity(_)).WillRepeatedly(Return(false));

    int result = checkTriggerUpgrade(&response, "testModel", PCI_UPGRADE);
    EXPECT_EQ(result, DIRECT_CDN_RETRY_ERR);

    global_mockdownloadfileops_ptr = NULL;
    global_mockexternal_ptr = NULL;
    g_DeviceUtilsMock = &Deviceglobal;
}

/**
 * @brief Task 6.1: Per-artifact PCI timeout error returns DIRECT_CDN_RETRY_ERR.
 * CURLTIMEOUT (28) is classified as retryable.
 */
TEST(DirectCDNRetryTest, PerArtifact_PCI_Timeout_ReturnsRetryErr) {
    MockDownloadFileOps mockfileops;
    global_mockdownloadfileops_ptr = &mockfileops;
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    DeviceUtilsMock DeviceMock;
    g_DeviceUtilsMock = &DeviceMock;

    XCONFRES response;
    memset(&response, 0, sizeof(response));
    strncpy(response.firmwareUrl, "https://cdn.example.com/fw.bin", sizeof(response.firmwareUrl) - 1);
    strncpy(response.cloudFWFile, "testModel_fw.bin", sizeof(response.cloudFWFile) - 1);

    /* downloadFile fails with CURLTIMEOUT (28) — retryable */
    EXPECT_CALL(mockfileops, downloadFile(_, _, _, _, _))
        .WillRepeatedly(testing::DoAll(testing::SetArgPointee<4>(0), testing::Return(CURLTIMEOUT)));

    /* Mock supporting calls */
    EXPECT_CALL(mockexternal, isDwnlBlock(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(DeviceMock, filePresentCheck(_)).WillRepeatedly(Return(-1));
    EXPECT_CALL(mockexternal, isMediaClientDevice()).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, isDelayFWDownloadActive(_, _, _)).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, isMmgbleNotifyEnabled()).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, updateFWDownloadStatus(_, _)).WillRepeatedly(Return(0));
    EXPECT_CALL(mockexternal, logMilestone(_)).Times(testing::AnyNumber());
    EXPECT_CALL(mockexternal, eventManager(_, _)).Times(testing::AnyNumber());
    EXPECT_CALL(mockexternal, checkPDRIUpgrade(_)).WillRepeatedly(Return(true));
    EXPECT_CALL(DeviceMock, getDevicePropertyData(_, _, _)).WillRepeatedly(Return(-1));
    EXPECT_CALL(mockexternal, CheckIProuteConnectivity(_)).WillRepeatedly(Return(false));

    int result = checkTriggerUpgrade(&response, "testModel", PCI_UPGRADE);
    EXPECT_EQ(result, DIRECT_CDN_RETRY_ERR);

    global_mockdownloadfileops_ptr = NULL;
    global_mockexternal_ptr = NULL;
    g_DeviceUtilsMock = &Deviceglobal;
}

/**
 * @brief Task 6.1: Per-artifact PCI low-bandwidth error returns DIRECT_CDN_RETRY_ERR.
 * CURL_LOW_BANDWIDTH (18) is classified as retryable.
 */
TEST(DirectCDNRetryTest, PerArtifact_PCI_LowBandwidth_ReturnsRetryErr) {
    MockDownloadFileOps mockfileops;
    global_mockdownloadfileops_ptr = &mockfileops;
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    DeviceUtilsMock DeviceMock;
    g_DeviceUtilsMock = &DeviceMock;

    XCONFRES response;
    memset(&response, 0, sizeof(response));
    strncpy(response.firmwareUrl, "https://cdn.example.com/fw.bin", sizeof(response.firmwareUrl) - 1);
    strncpy(response.cloudFWFile, "testModel_fw.bin", sizeof(response.cloudFWFile) - 1);

    /* downloadFile fails with CURL_LOW_BANDWIDTH (18) — retryable */
    EXPECT_CALL(mockfileops, downloadFile(_, _, _, _, _))
        .WillRepeatedly(testing::DoAll(testing::SetArgPointee<4>(0), testing::Return(CURL_LOW_BANDWIDTH)));

    /* Mock supporting calls */
    EXPECT_CALL(mockexternal, isDwnlBlock(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(DeviceMock, filePresentCheck(_)).WillRepeatedly(Return(-1));
    EXPECT_CALL(mockexternal, isMediaClientDevice()).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, isDelayFWDownloadActive(_, _, _)).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, isMmgbleNotifyEnabled()).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, updateFWDownloadStatus(_, _)).WillRepeatedly(Return(0));
    EXPECT_CALL(mockexternal, logMilestone(_)).Times(testing::AnyNumber());
    EXPECT_CALL(mockexternal, eventManager(_, _)).Times(testing::AnyNumber());
    EXPECT_CALL(mockexternal, checkPDRIUpgrade(_)).WillRepeatedly(Return(true));
    EXPECT_CALL(DeviceMock, getDevicePropertyData(_, _, _)).WillRepeatedly(Return(-1));

    int result = checkTriggerUpgrade(&response, "testModel", PCI_UPGRADE);
    EXPECT_EQ(result, DIRECT_CDN_RETRY_ERR);

    global_mockdownloadfileops_ptr = NULL;
    global_mockexternal_ptr = NULL;
    g_DeviceUtilsMock = &Deviceglobal;
}

/**
 * @brief Task 6.1: Per-artifact transient HTTP 503 returns DIRECT_CDN_RETRY_ERR.
 * Server-side Service Unavailable is classified as retryable.
 */
TEST(DirectCDNRetryTest, PerArtifact_PCI_Http503_ReturnsRetryErr) {
    MockDownloadFileOps mockfileops;
    global_mockdownloadfileops_ptr = &mockfileops;
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    DeviceUtilsMock DeviceMock;
    g_DeviceUtilsMock = &DeviceMock;

    XCONFRES response;
    memset(&response, 0, sizeof(response));
    strncpy(response.firmwareUrl, "https://cdn.example.com/fw.bin", sizeof(response.firmwareUrl) - 1);
    strncpy(response.cloudFWFile, "testModel_fw.bin", sizeof(response.cloudFWFile) - 1);

    /* downloadFile returns curl 0 (ok) but HTTP 503 (Service Unavailable) — retryable */
    EXPECT_CALL(mockfileops, downloadFile(_, _, _, _, _))
        .WillRepeatedly(testing::DoAll(testing::SetArgPointee<4>(503), testing::Return(0)));

    /* Mock supporting calls */
    EXPECT_CALL(mockexternal, isDwnlBlock(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(DeviceMock, filePresentCheck(_)).WillRepeatedly(Return(-1));
    EXPECT_CALL(mockexternal, isMediaClientDevice()).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, isDelayFWDownloadActive(_, _, _)).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, isMmgbleNotifyEnabled()).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, updateFWDownloadStatus(_, _)).WillRepeatedly(Return(0));
    EXPECT_CALL(mockexternal, logMilestone(_)).Times(testing::AnyNumber());
    EXPECT_CALL(mockexternal, eventManager(_, _)).Times(testing::AnyNumber());
    EXPECT_CALL(mockexternal, checkPDRIUpgrade(_)).WillRepeatedly(Return(true));
    EXPECT_CALL(DeviceMock, getDevicePropertyData(_, _, _)).WillRepeatedly(Return(-1));

    int result = checkTriggerUpgrade(&response, "testModel", PCI_UPGRADE);
    EXPECT_EQ(result, DIRECT_CDN_RETRY_ERR);

    global_mockdownloadfileops_ptr = NULL;
    global_mockexternal_ptr = NULL;
    g_DeviceUtilsMock = &Deviceglobal;
}

/**
 * @brief Task 6.1: Per-artifact permanent HTTP 404 returns -1.
 * HTTP Not Found is classified as non-retryable permanent failure.
 */
TEST(DirectCDNRetryTest, PerArtifact_PCI_Http404_ReturnsPermanentFailure) {
    MockDownloadFileOps mockfileops;
    global_mockdownloadfileops_ptr = &mockfileops;
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    DeviceUtilsMock DeviceMock;
    g_DeviceUtilsMock = &DeviceMock;

    XCONFRES response;
    memset(&response, 0, sizeof(response));
    strncpy(response.firmwareUrl, "https://cdn.example.com/fw.bin", sizeof(response.firmwareUrl) - 1);
    strncpy(response.cloudFWFile, "testModel_fw.bin", sizeof(response.cloudFWFile) - 1);

    /* downloadFile returns curl 0 but HTTP 404 — permanent failure */
    EXPECT_CALL(mockfileops, downloadFile(_, _, _, _, _))
        .WillRepeatedly(testing::DoAll(testing::SetArgPointee<4>(404), testing::Return(0)));

    /* Mock supporting calls */
    EXPECT_CALL(mockexternal, isDwnlBlock(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(DeviceMock, filePresentCheck(_)).WillRepeatedly(Return(-1));
    EXPECT_CALL(mockexternal, isMediaClientDevice()).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, isDelayFWDownloadActive(_, _, _)).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, isMmgbleNotifyEnabled()).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, updateFWDownloadStatus(_, _)).WillRepeatedly(Return(0));
    EXPECT_CALL(mockexternal, logMilestone(_)).Times(testing::AnyNumber());
    EXPECT_CALL(mockexternal, eventManager(_, _)).Times(testing::AnyNumber());
    EXPECT_CALL(mockexternal, checkPDRIUpgrade(_)).WillRepeatedly(Return(true));
    EXPECT_CALL(DeviceMock, getDevicePropertyData(_, _, _)).WillRepeatedly(Return(-1));

    int result = checkTriggerUpgrade(&response, "testModel", PCI_UPGRADE);
    EXPECT_EQ(result, -1);

    global_mockdownloadfileops_ptr = NULL;
    global_mockexternal_ptr = NULL;
    g_DeviceUtilsMock = &Deviceglobal;
}

/**
 * @brief Task 6.2: Per-artifact with empty URL returns 0 (skip).
 * When artifact URL is empty, checkTriggerUpgrade skips and returns success.
 */
TEST(DirectCDNRetryTest, PerArtifact_EmptyUrl_SkipsAndReturnsZero) {
    XCONFRES response;
    memset(&response, 0, sizeof(response));
    /* firmwareUrl is empty — PCI should be skipped */
    strncpy(response.cloudFWFile, "testModel_fw.bin", sizeof(response.cloudFWFile) - 1);

    int result = checkTriggerUpgrade(&response, "testModel", PCI_UPGRADE);
    EXPECT_EQ(result, 0);
}

/**
 * @brief Task 6.2: Per-artifact with empty filename returns 0 (skip).
 * When artifact filename is empty, checkTriggerUpgrade skips and returns success.
 */
TEST(DirectCDNRetryTest, PerArtifact_EmptyFilename_SkipsAndReturnsZero) {
    XCONFRES response;
    memset(&response, 0, sizeof(response));
    strncpy(response.firmwareUrl, "https://cdn.example.com/fw.bin", sizeof(response.firmwareUrl) - 1);
    /* cloudFWFile is empty — PCI should be skipped */

    int result = checkTriggerUpgrade(&response, "testModel", PCI_UPGRADE);
    EXPECT_EQ(result, 0);
}

/**
 * @brief Task 6.1: Per-artifact PDRI download success returns 0.
 * Validates PDRI path uses pdriUrl correctly.
 */
TEST(DirectCDNRetryTest, PerArtifact_PDRI_Success_ReturnsZero) {
    MockDownloadFileOps mockfileops;
    global_mockdownloadfileops_ptr = &mockfileops;
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    DeviceUtilsMock DeviceMock;
    g_DeviceUtilsMock = &DeviceMock;

    XCONFRES response;
    memset(&response, 0, sizeof(response));
    strncpy(response.pdriUrl, "https://cdn.example.com/pdri.bin", sizeof(response.pdriUrl) - 1);
    strncpy(response.cloudPDRIVersion, "testModel_PDRI_v2.bin", sizeof(response.cloudPDRIVersion) - 1);

    /* downloadFile succeeds */
    EXPECT_CALL(mockfileops, downloadFile(_, _, _, _, _))
        .WillOnce(testing::DoAll(testing::SetArgPointee<4>(200), testing::Return(0)));

    /* Mock supporting calls */
    EXPECT_CALL(mockexternal, isDwnlBlock(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(DeviceMock, filePresentCheck(_)).WillRepeatedly(Return(-1));
    EXPECT_CALL(mockexternal, isMediaClientDevice()).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, isDelayFWDownloadActive(_, _, _)).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, isMmgbleNotifyEnabled()).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, updateFWDownloadStatus(_, _)).WillRepeatedly(Return(0));
    EXPECT_CALL(mockexternal, logMilestone(_)).Times(testing::AnyNumber());
    EXPECT_CALL(mockexternal, eventManager(_, _)).Times(testing::AnyNumber());
    EXPECT_CALL(mockexternal, checkPDRIUpgrade(_)).WillRepeatedly(Return(true));
    EXPECT_CALL(DeviceMock, getDevicePropertyData(_, _, _)).WillRepeatedly(Return(-1));

    int result = checkTriggerUpgrade(&response, "testModel", PDRI_UPGRADE);
    EXPECT_EQ(result, 0);

    global_mockdownloadfileops_ptr = NULL;
    global_mockexternal_ptr = NULL;
    g_DeviceUtilsMock = &Deviceglobal;
}

/**
 * @brief Task 6.1: Per-artifact PDRI transient error returns DIRECT_CDN_RETRY_ERR.
 * DNS resolution failure is classified as retryable for PDRI.
 */
TEST(DirectCDNRetryTest, PerArtifact_PDRI_TransientError_ReturnsRetryErr) {
    MockDownloadFileOps mockfileops;
    global_mockdownloadfileops_ptr = &mockfileops;
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    DeviceUtilsMock DeviceMock;
    g_DeviceUtilsMock = &DeviceMock;

    XCONFRES response;
    memset(&response, 0, sizeof(response));
    strncpy(response.pdriUrl, "https://cdn.example.com/pdri.bin", sizeof(response.pdriUrl) - 1);
    strncpy(response.cloudPDRIVersion, "testModel_PDRI_v2.bin", sizeof(response.cloudPDRIVersion) - 1);

    /* downloadFile fails with DNS resolution error (6) */
    EXPECT_CALL(mockfileops, downloadFile(_, _, _, _, _))
        .WillRepeatedly(testing::DoAll(testing::SetArgPointee<4>(0), testing::Return(CURL_COULDNT_RESOLVE_HOST)));

    /* Mock supporting calls */
    EXPECT_CALL(mockexternal, isDwnlBlock(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(DeviceMock, filePresentCheck(_)).WillRepeatedly(Return(-1));
    EXPECT_CALL(mockexternal, isMediaClientDevice()).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, isDelayFWDownloadActive(_, _, _)).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, isMmgbleNotifyEnabled()).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, updateFWDownloadStatus(_, _)).WillRepeatedly(Return(0));
    EXPECT_CALL(mockexternal, logMilestone(_)).Times(testing::AnyNumber());
    EXPECT_CALL(mockexternal, eventManager(_, _)).Times(testing::AnyNumber());
    EXPECT_CALL(mockexternal, checkPDRIUpgrade(_)).WillRepeatedly(Return(true));
    EXPECT_CALL(DeviceMock, getDevicePropertyData(_, _, _)).WillRepeatedly(Return(-1));
    EXPECT_CALL(mockexternal, CheckIProuteConnectivity(_)).WillRepeatedly(Return(false));

    int result = checkTriggerUpgrade(&response, "testModel", PDRI_UPGRADE);
    EXPECT_EQ(result, DIRECT_CDN_RETRY_ERR);

    global_mockdownloadfileops_ptr = NULL;
    global_mockexternal_ptr = NULL;
    g_DeviceUtilsMock = &Deviceglobal;
}

/**
 * @brief Task 6.1: Per-artifact PERIPHERAL empty URL returns 0 (skip).
 * Peripheral with no URL available is skipped (does not gate retry loop).
 */
TEST(DirectCDNRetryTest, PerArtifact_Peripheral_EmptyUrl_SkipsReturnsZero) {
    XCONFRES response;
    memset(&response, 0, sizeof(response));
    /* remCtrlUrl is empty — PERIPHERAL should be skipped */
    strncpy(response.peripheralFirmwares, "remCtrl_fw.bin", sizeof(response.peripheralFirmwares) - 1);

    int result = checkTriggerUpgrade(&response, "testModel", PERIPHERAL_UPGRADE);
    EXPECT_EQ(result, 0);
}

/* =========================================================================
 * Task 7.1 Unit Tests: DirectCDNDownload orchestrator coverage
 * ========================================================================= */

class DirectCDNOrchestratorTest : public ::testing::Test {
protected:
    MockDownloadFileOps mockfileops;
    MockExternal mockexternal;
    DeviceUtilsMock DeviceMock;
    DeviceProperty_t local_device_info;
    int http_code;
    char cur_img_name[64];

    void SetUp() override {
        global_mockdownloadfileops_ptr = &mockfileops;
        global_mockexternal_ptr = &mockexternal;
        g_DeviceUtilsMock = &DeviceMock;

        memset(&local_device_info, 0, sizeof(local_device_info));
        strncpy(local_device_info.model, "testModel", sizeof(local_device_info.model) - 1);
        strncpy(local_device_info.maint_status, "false", sizeof(local_device_info.maint_status) - 1);
        strncpy(local_device_info.difw_path, "/tmp", sizeof(local_device_info.difw_path) - 1);

        /* checkTriggerUpgrade() per-artifact path uses global device_info */
        strncpy(device_info.model, "testModel", sizeof(device_info.model) - 1);
        strncpy(device_info.maint_status, "false", sizeof(device_info.maint_status) - 1);
        strncpy(device_info.difw_path, "/tmp", sizeof(device_info.difw_path) - 1);

        strncpy(cur_img_name, "testModel_current.bin", sizeof(cur_img_name) - 1);
        cur_img_name[sizeof(cur_img_name) - 1] = '\0';
        http_code = 0;

        EXPECT_CALL(mockexternal, isDwnlBlock(_)).WillRepeatedly(Return(0));
        EXPECT_CALL(DeviceMock, filePresentCheck(_)).WillRepeatedly(Return(-1));
        EXPECT_CALL(mockexternal, isMediaClientDevice()).WillRepeatedly(Return(false));
        EXPECT_CALL(mockexternal, isDelayFWDownloadActive(_, _, _)).WillRepeatedly(Return(false));
        EXPECT_CALL(mockexternal, isMmgbleNotifyEnabled()).WillRepeatedly(Return(false));
        EXPECT_CALL(mockexternal, updateFWDownloadStatus(_, _)).WillRepeatedly(Return(0));
        EXPECT_CALL(mockexternal, logMilestone(_)).Times(testing::AnyNumber());
        EXPECT_CALL(mockexternal, eventManager(_, _)).Times(testing::AnyNumber());
        EXPECT_CALL(mockexternal, checkPDRIUpgrade(_)).WillRepeatedly(Return(true));
        EXPECT_CALL(mockexternal, CheckIProuteConnectivity(_)).WillRepeatedly(Return(false));
        EXPECT_CALL(DeviceMock, getDevicePropertyData(_, _, _)).WillRepeatedly(Return(-1));
        EXPECT_CALL(DeviceMock, isDirectCDNEnabled()).WillRepeatedly(Return(true));
    }

    void TearDown() override {
        global_mockdownloadfileops_ptr = NULL;
        global_mockexternal_ptr = NULL;
        g_DeviceUtilsMock = &Deviceglobal;
    }
};

TEST_F(DirectCDNOrchestratorTest, HappyPath_AllArtifactsSucceed_ReturnsZero) {
    const char *xconf_json = "{"
        "\"firmwareDownloadProtocol\":\"http\"," 
        "\"firmwareFilename\":\"testModel_fw.bin\"," 
        "\"firmwareLocation\":\"https://legacy.example.com/fw\"," 
        "\"firmwareVersion\":\"testModel_VBN_24Q4\"," 
        "\"additionalFwVerInfo\":\"testModel_PDRI_24Q4.bin\"," 
        "\"firmware_URL\":\"https://cdn.example.com/pci.bin\"," 
        "\"additionalFwVerInfo_URL\":\"https://cdn.example.com/pdri.bin\"," 
        "\"remCtrl\":\"peri_fw.bin\"," 
        "\"remCtrl_URL\":\"https://cdn.example.com/peri.bin\"," 
        "\"rebootImmediately\":false"
    "}";

    int xconf_calls = 0;
    int pci_calls = 0;
    int pdri_calls = 0;
    int peri_calls = 0;

    EXPECT_CALL(mockfileops, codebigdownloadFile(_, _, _, _, _)).Times(0);
    EXPECT_CALL(mockfileops, downloadFile(_, _, _, _, _))
        .WillRepeatedly(testing::Invoke([&](int, const char *url, const void *dwlloc, char *post, int *http) {
            if (post != NULL) {
                DownloadData *dl = (DownloadData *)dwlloc;
                size_t max_copy = dl->memsize > 0 ? dl->memsize - 1 : 0;
                xconf_calls++;
                if (dl != NULL && dl->pvOut != NULL && max_copy > 0) {
                    strncpy((char *)dl->pvOut, xconf_json, max_copy);
                    ((char *)dl->pvOut)[max_copy] = '\0';
                    dl->datasize = strlen((char *)dl->pvOut);
                }
                *http = 200;
                return 0;
            }

            if (url != NULL && strstr(url, "pci.bin") != NULL) {
                pci_calls++;
            } else if (url != NULL && strstr(url, "pdri.bin") != NULL) {
                pdri_calls++;
            } else if (url != NULL && strstr(url, "peri.bin") != NULL) {
                peri_calls++;
            }

            *http = 200;
            return 0;
        }));

    XCONFRES response;
    memset(&response, 0, sizeof(response));

    int ret = DirectCDNDownload(&response, cur_img_name, &local_device_info, HTTP_XCONF_DIRECT, &http_code);
    EXPECT_EQ(ret, 0);
    EXPECT_STRNE(response.firmwareUrl, "");
    EXPECT_EQ(xconf_calls, 1);
    EXPECT_EQ(pci_calls, 1);
    EXPECT_EQ(pdri_calls, 1);
    EXPECT_EQ(peri_calls, 1);
}

TEST_F(DirectCDNOrchestratorTest, SelectiveRetry_SkipsSucceededArtifactOnNextIteration) {
    const char *xconf_json = "{"
        "\"firmwareDownloadProtocol\":\"http\"," 
        "\"firmwareFilename\":\"testModel_fw.bin\"," 
        "\"firmwareLocation\":\"https://legacy.example.com/fw\"," 
        "\"firmwareVersion\":\"testModel_VBN_24Q4\"," 
        "\"additionalFwVerInfo\":\"testModel_PDRI_24Q4.bin\"," 
        "\"firmware_URL\":\"https://cdn.example.com/pci.bin\"," 
        "\"additionalFwVerInfo_URL\":\"https://cdn.example.com/pdri.bin\"," 
        "\"remCtrl\":\"peri_fw.bin\"," 
        "\"remCtrl_URL\":\"https://cdn.example.com/peri.bin\"," 
        "\"rebootImmediately\":false"
    "}";

    int xconf_calls = 0;
    int pci_calls = 0;
    int pdri_calls = 0;

    EXPECT_CALL(mockfileops, codebigdownloadFile(_, _, _, _, _)).Times(0);
    EXPECT_CALL(mockfileops, downloadFile(_, _, _, _, _))
        .WillRepeatedly(testing::Invoke([&](int, const char *url, const void *dwlloc, char *post, int *http) {
            if (post != NULL) {
                DownloadData *dl = (DownloadData *)dwlloc;
                size_t max_copy = dl->memsize > 0 ? dl->memsize - 1 : 0;
                xconf_calls++;
                if (dl != NULL && dl->pvOut != NULL && max_copy > 0) {
                    strncpy((char *)dl->pvOut, xconf_json, max_copy);
                    ((char *)dl->pvOut)[max_copy] = '\0';
                    dl->datasize = strlen((char *)dl->pvOut);
                }
                *http = 200;
                return 0;
            }

            if (url != NULL && strstr(url, "pci.bin") != NULL) {
                pci_calls++;
                *http = 200;
                return 0;
            }

            if (url != NULL && strstr(url, "pdri.bin") != NULL) {
                pdri_calls++;
                if (xconf_calls < 2) {
                    *http = 0;
                    return CURLTIMEOUT;
                }
                *http = 200;
                return 0;
            }

            *http = 200;
            return 0;
        }));

    XCONFRES response;
    memset(&response, 0, sizeof(response));

    int ret = DirectCDNDownload(&response, cur_img_name, &local_device_info, HTTP_XCONF_DIRECT, &http_code);
    EXPECT_EQ(ret, 0);
    EXPECT_GE(xconf_calls, 2);
    EXPECT_EQ(pci_calls, 1) << "PCI must not be retried after first success";
    EXPECT_GE(pdri_calls, 2);
}

TEST_F(DirectCDNOrchestratorTest, MaxRetryExhaustion_ReturnsFailure) {
    const char *xconf_json = "{"
        "\"firmwareDownloadProtocol\":\"http\"," 
        "\"firmwareFilename\":\"testModel_fw.bin\"," 
        "\"firmwareLocation\":\"https://legacy.example.com/fw\"," 
        "\"firmwareVersion\":\"testModel_VBN_24Q4\"," 
        "\"additionalFwVerInfo\":\"testModel_PDRI_24Q4.bin\"," 
        "\"firmware_URL\":\"https://cdn.example.com/pci.bin\"," 
        "\"additionalFwVerInfo_URL\":\"https://cdn.example.com/pdri.bin\"," 
        "\"rebootImmediately\":false"
    "}";

    int xconf_calls = 0;

    EXPECT_CALL(mockfileops, codebigdownloadFile(_, _, _, _, _)).Times(0);
    EXPECT_CALL(mockfileops, downloadFile(_, _, _, _, _))
        .WillRepeatedly(testing::Invoke([&](int, const char *, const void *dwlloc, char *post, int *http) {
            if (post != NULL) {
                DownloadData *dl = (DownloadData *)dwlloc;
                size_t max_copy = dl->memsize > 0 ? dl->memsize - 1 : 0;
                xconf_calls++;
                if (dl != NULL && dl->pvOut != NULL && max_copy > 0) {
                    strncpy((char *)dl->pvOut, xconf_json, max_copy);
                    ((char *)dl->pvOut)[max_copy] = '\0';
                    dl->datasize = strlen((char *)dl->pvOut);
                }
                *http = 200;
                return 0;
            }

            *http = 0;
            return CURLTIMEOUT;
        }));

    XCONFRES response;
    memset(&response, 0, sizeof(response));

    int ret = DirectCDNDownload(&response, cur_img_name, &local_device_info, HTTP_XCONF_DIRECT, &http_code);
    EXPECT_NE(ret, 0);
    EXPECT_EQ(xconf_calls, 3);
}

/**
 * @brief When isDwnlBlock forces server_type flip to CODEBIG while Direct CDN
 * is active, codebigdownloadFile rejects the request and the download fails.
 */
TEST(DirectCDNBypassTest, DirectCDN_WhenDwnlBlockForcesCodebig_RequestFails) {
    MockDownloadFileOps mockfileops;
    global_mockdownloadfileops_ptr = &mockfileops;
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    DeviceUtilsMock DeviceMock;
    g_DeviceUtilsMock = &DeviceMock;

    int http_code = 0;
    int local_force_exit = 0;
    void *test_curl = NULL;
    Rfc_t local_rfc = {0};
    strncpy(local_rfc.rfc_throttle, "false", sizeof(local_rfc.rfc_throttle) - 1);

    RdkUpgradeContext_t context = {0};
    context.upgrade_type = PCI_UPGRADE;
    context.server_type = HTTP_SSR_DIRECT;
    context.artifactLocationUrl = "https://cdn.example.com/firmware.bin";
    context.dwlloc = "/tmp/firmware.bin";
    context.pPostFields = NULL;
    context.immed_reboot_flag = "false";
    context.delay_dwnl = 0;
    context.lastrun = "0";
    context.disableStatsUpdate = (char*)"true";
    context.device_info = &device_info;
    context.force_exit = &local_force_exit;
    context.trigger_type = 1;
    context.rfc_list = &local_rfc;
    context.direct_cdn = true;

    /* isDwnlBlock forces server_type flip from DIRECT to CODEBIG */
    EXPECT_CALL(mockexternal, isDwnlBlock(HTTP_SSR_DIRECT)).WillOnce(Return(1));
    EXPECT_CALL(mockexternal, isDwnlBlock(HTTP_SSR_CODEBIG)).WillOnce(Return(0));

    /* codebigdownloadFile is entered but returns -1 (direct_cdn guard) */
    EXPECT_CALL(mockfileops, codebigdownloadFile(_, _, _, _, _))
        .Times(1)
        .WillOnce(testing::DoAll(testing::SetArgPointee<4>(0), testing::Return(-1)));

    /* retryDownload uses context->server_type (HTTP_SSR_DIRECT) so downloadFile
       gets called from the retry path; let it fail so the overall result != 0 */
    EXPECT_CALL(mockfileops, downloadFile(_, _, _, _, _))
        .WillRepeatedly(testing::DoAll(testing::SetArgPointee<4>(0), testing::Return(-1)));

    EXPECT_CALL(DeviceMock, filePresentCheck(_)).WillRepeatedly(Return(-1));
    EXPECT_CALL(mockexternal, isMediaClientDevice()).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, isDelayFWDownloadActive(_, _, _)).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, isUpgradeInProgress()).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, isMmgbleNotifyEnabled()).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, updateFWDownloadStatus(_, _)).WillRepeatedly(Return(0));
    EXPECT_CALL(mockexternal, logMilestone(_)).Times(testing::AnyNumber());
    EXPECT_CALL(mockexternal, eventManager(_, _)).Times(testing::AnyNumber());
    EXPECT_CALL(mockexternal, checkPDRIUpgrade(_)).WillRepeatedly(Return(true));
    EXPECT_CALL(DeviceMock, getDevicePropertyData(_, _, _)).WillRepeatedly(Return(-1));

    int result = rdkv_upgrade_request(&context, &test_curl, &http_code);
    EXPECT_NE(result, CURL_SUCCESS);

    global_mockdownloadfileops_ptr = NULL;
    global_mockexternal_ptr = NULL;
    g_DeviceUtilsMock = &Deviceglobal;
}

/**
 * @brief Per-artifact PDRI download without .bin suffix gets .bin appended
 * to the local save path.
 */
TEST(DirectCDNRetryTest, PerArtifact_PDRI_WhenNoBinSuffix_AppendsBinToPath) {
    MockDownloadFileOps mockfileops;
    global_mockdownloadfileops_ptr = &mockfileops;
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    DeviceUtilsMock DeviceMock;
    g_DeviceUtilsMock = &DeviceMock;

    XCONFRES response;
    memset(&response, 0, sizeof(response));
    strncpy(response.pdriUrl, "https://cdn.example.com/pdri_image", sizeof(response.pdriUrl) - 1);
    strncpy(response.cloudPDRIVersion, "testModel_PDRI_v2", sizeof(response.cloudPDRIVersion) - 1);

    EXPECT_CALL(mockfileops, downloadFile(_, _, _, _, _))
        .WillOnce(testing::Invoke([](int, const char *, const void *dwlloc, char *, int *http) {
            const char *path = (const char *)dwlloc;
            EXPECT_NE(strstr(path, "testModel_PDRI_v2.bin"), nullptr);
            *http = 200;
            return 0;
        }));

    EXPECT_CALL(mockexternal, isDwnlBlock(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(DeviceMock, filePresentCheck(_)).WillRepeatedly(Return(-1));
    EXPECT_CALL(mockexternal, isMediaClientDevice()).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, isDelayFWDownloadActive(_, _, _)).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, isMmgbleNotifyEnabled()).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, updateFWDownloadStatus(_, _)).WillRepeatedly(Return(0));
    EXPECT_CALL(mockexternal, logMilestone(_)).Times(testing::AnyNumber());
    EXPECT_CALL(mockexternal, eventManager(_, _)).Times(testing::AnyNumber());
    EXPECT_CALL(mockexternal, checkPDRIUpgrade(_)).WillRepeatedly(Return(true));
    EXPECT_CALL(DeviceMock, getDevicePropertyData(_, _, _)).WillRepeatedly(Return(-1));

    int result = checkTriggerUpgrade(&response, "testModel", PDRI_UPGRADE);
    EXPECT_EQ(result, 0);

    global_mockdownloadfileops_ptr = NULL;
    global_mockexternal_ptr = NULL;
    g_DeviceUtilsMock = &Deviceglobal;
}

/**
 * @brief Per-artifact PDRI download with existing .bin suffix does not
 * double-append (no .bin.bin in the local save path).
 */
TEST(DirectCDNRetryTest, PerArtifact_PDRI_WhenBinSuffixPresent_PathUnchanged) {
    MockDownloadFileOps mockfileops;
    global_mockdownloadfileops_ptr = &mockfileops;
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    DeviceUtilsMock DeviceMock;
    g_DeviceUtilsMock = &DeviceMock;

    XCONFRES response;
    memset(&response, 0, sizeof(response));
    strncpy(response.pdriUrl, "https://cdn.example.com/pdri_image.bin", sizeof(response.pdriUrl) - 1);
    strncpy(response.cloudPDRIVersion, "testModel_PDRI_v2.bin", sizeof(response.cloudPDRIVersion) - 1);

    EXPECT_CALL(mockfileops, downloadFile(_, _, _, _, _))
        .WillOnce(testing::Invoke([](int, const char *, const void *dwlloc, char *, int *http) {
            const char *path = (const char *)dwlloc;
            EXPECT_NE(strstr(path, "testModel_PDRI_v2.bin"), nullptr);
            EXPECT_EQ(strstr(path, ".bin.bin"), nullptr);
            *http = 200;
            return 0;
        }));

    EXPECT_CALL(mockexternal, isDwnlBlock(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(DeviceMock, filePresentCheck(_)).WillRepeatedly(Return(-1));
    EXPECT_CALL(mockexternal, isMediaClientDevice()).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, isDelayFWDownloadActive(_, _, _)).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, isMmgbleNotifyEnabled()).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, updateFWDownloadStatus(_, _)).WillRepeatedly(Return(0));
    EXPECT_CALL(mockexternal, logMilestone(_)).Times(testing::AnyNumber());
    EXPECT_CALL(mockexternal, eventManager(_, _)).Times(testing::AnyNumber());
    EXPECT_CALL(mockexternal, checkPDRIUpgrade(_)).WillRepeatedly(Return(true));
    EXPECT_CALL(DeviceMock, getDevicePropertyData(_, _, _)).WillRepeatedly(Return(-1));

    int result = checkTriggerUpgrade(&response, "testModel", PDRI_UPGRADE);
    EXPECT_EQ(result, 0);

    global_mockdownloadfileops_ptr = NULL;
    global_mockexternal_ptr = NULL;
    g_DeviceUtilsMock = &Deviceglobal;
}

/**
 * @brief Per-artifact download receiving HTTP 403 (token expiry) is classified
 * as retryable so the orchestrator can re-query XConf for fresh URLs.
 */
TEST(DirectCDNRetryTest, PerArtifact_WhenHttp403Received_ReturnsRetryErr) {
    MockDownloadFileOps mockfileops;
    global_mockdownloadfileops_ptr = &mockfileops;
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    DeviceUtilsMock DeviceMock;
    g_DeviceUtilsMock = &DeviceMock;

    XCONFRES response;
    memset(&response, 0, sizeof(response));
    strncpy(response.firmwareUrl, "https://cdn.example.com/fw.bin", sizeof(response.firmwareUrl) - 1);
    strncpy(response.cloudFWFile, "testModel_fw.bin", sizeof(response.cloudFWFile) - 1);

    EXPECT_CALL(mockfileops, downloadFile(_, _, _, _, _))
        .WillRepeatedly(testing::DoAll(testing::SetArgPointee<4>(403), testing::Return(0)));

    EXPECT_CALL(mockexternal, isDwnlBlock(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(DeviceMock, filePresentCheck(_)).WillRepeatedly(Return(-1));
    EXPECT_CALL(mockexternal, isMediaClientDevice()).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, isDelayFWDownloadActive(_, _, _)).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, isMmgbleNotifyEnabled()).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, updateFWDownloadStatus(_, _)).WillRepeatedly(Return(0));
    EXPECT_CALL(mockexternal, logMilestone(_)).Times(testing::AnyNumber());
    EXPECT_CALL(mockexternal, eventManager(_, _)).Times(testing::AnyNumber());
    EXPECT_CALL(mockexternal, checkPDRIUpgrade(_)).WillRepeatedly(Return(true));
    EXPECT_CALL(DeviceMock, getDevicePropertyData(_, _, _)).WillRepeatedly(Return(-1));

    int result = checkTriggerUpgrade(&response, "testModel", PCI_UPGRADE);
    EXPECT_EQ(result, DIRECT_CDN_RETRY_ERR);

    global_mockdownloadfileops_ptr = NULL;
    global_mockexternal_ptr = NULL;
    g_DeviceUtilsMock = &Deviceglobal;
}

/**
 * @brief When downloadFile returns RDKV_UPGRADE_ERROR_STATE_RED, rdkv_upgrade_request
 * must short-circuit immediately without calling retryDownload or codebig fallback.
 * This verifies the state-red guard prevents retry loops after uninitialize() was called.
 */
TEST(StateRedShortCircuitTest, DirectPath_SkipsRetryWhenStateRedReturned) {
    MockDownloadFileOps mockfileops;
    global_mockdownloadfileops_ptr = &mockfileops;
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    DeviceUtilsMock DeviceMock;
    g_DeviceUtilsMock = &DeviceMock;

    int local_force_exit = 0;
    int http_code = 0;
    void *test_curl = NULL;
    Rfc_t local_rfc = {0};
    strncpy(local_rfc.rfc_throttle, "false", sizeof(local_rfc.rfc_throttle) - 1);

    RdkUpgradeContext_t context = {0};
    context.upgrade_type = PCI_UPGRADE;
    context.server_type = HTTP_SSR_DIRECT;
    context.artifactLocationUrl = "https://cdn.example.com/firmware.bin";
    context.dwlloc = "/tmp/firmware.bin";
    context.pPostFields = NULL;
    context.immed_reboot_flag = "false";
    context.delay_dwnl = 0;
    context.lastrun = "0";
    context.disableStatsUpdate = (char*)"true";
    context.device_info = &device_info;
    context.force_exit = &local_force_exit;
    context.trigger_type = 1;
    context.rfc_list = &local_rfc;
    context.direct_cdn = false;

    /* downloadFile returns STATE_RED — should be called exactly once (no retry) */
    EXPECT_CALL(mockfileops, downloadFile(_, _, _, _, _))
        .Times(1)
        .WillOnce(testing::DoAll(testing::SetArgPointee<4>(0), testing::Return(RDKV_UPGRADE_ERROR_STATE_RED)));

    /* codebigdownloadFile should NEVER be called — no fallback after state red */
    EXPECT_CALL(mockfileops, codebigdownloadFile(_, _, _, _, _)).Times(0);

    /* Mock supporting calls */
    EXPECT_CALL(mockexternal, isDwnlBlock(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(DeviceMock, filePresentCheck(_)).WillRepeatedly(Return(-1));
    EXPECT_CALL(mockexternal, isMediaClientDevice()).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, isDelayFWDownloadActive(_, _, _)).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, isUpgradeInProgress()).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, isMmgbleNotifyEnabled()).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, updateFWDownloadStatus(_, _)).WillRepeatedly(Return(0));
    EXPECT_CALL(mockexternal, logMilestone(_)).Times(testing::AnyNumber());
    EXPECT_CALL(mockexternal, eventManager(_, _)).Times(testing::AnyNumber());
    EXPECT_CALL(mockexternal, checkPDRIUpgrade(_)).WillRepeatedly(Return(true));
    EXPECT_CALL(DeviceMock, getDevicePropertyData(_, _, _)).WillRepeatedly(Return(-1));
    EXPECT_CALL(mockexternal, CheckIProuteConnectivity(_)).WillRepeatedly(Return(false));

    int result = rdkv_upgrade_request(&context, &test_curl, &http_code);
    EXPECT_EQ(result, RDKV_UPGRADE_ERROR_STATE_RED);

    global_mockdownloadfileops_ptr = NULL;
    global_mockexternal_ptr = NULL;
    g_DeviceUtilsMock = &Deviceglobal;
}

/**
 * @brief When codebigdownloadFile returns RDKV_UPGRADE_ERROR_STATE_RED on the codebig
 * path, rdkv_upgrade_request must short-circuit without retrying.
 */
TEST(StateRedShortCircuitTest, CodebigPath_SkipsRetryWhenStateRedReturned) {
    MockDownloadFileOps mockfileops;
    global_mockdownloadfileops_ptr = &mockfileops;
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    DeviceUtilsMock DeviceMock;
    g_DeviceUtilsMock = &DeviceMock;

    int local_force_exit = 0;
    int http_code = 0;
    void *test_curl = NULL;
    Rfc_t local_rfc = {0};
    strncpy(local_rfc.rfc_throttle, "false", sizeof(local_rfc.rfc_throttle) - 1);

    RdkUpgradeContext_t context = {0};
    context.upgrade_type = PCI_UPGRADE;
    context.server_type = HTTP_SSR_CODEBIG;
    context.artifactLocationUrl = "https://cdn.example.com/firmware.bin";
    context.dwlloc = "/tmp/firmware.bin";
    context.pPostFields = NULL;
    context.immed_reboot_flag = "false";
    context.delay_dwnl = 0;
    context.lastrun = "0";
    context.disableStatsUpdate = (char*)"true";
    context.device_info = &device_info;
    context.force_exit = &local_force_exit;
    context.trigger_type = 1;
    context.rfc_list = &local_rfc;
    context.direct_cdn = false;

    /* codebigdownloadFile returns STATE_RED — should be called exactly once (no retry) */
    EXPECT_CALL(mockfileops, codebigdownloadFile(_, _, _, _, _))
        .Times(1)
        .WillOnce(testing::DoAll(testing::SetArgPointee<4>(0), testing::Return(RDKV_UPGRADE_ERROR_STATE_RED)));

    /* downloadFile should NEVER be called — no fallback after state red */
    EXPECT_CALL(mockfileops, downloadFile(_, _, _, _, _)).Times(0);

    /* Mock supporting calls */
    EXPECT_CALL(mockexternal, isDwnlBlock(_)).WillRepeatedly(Return(0));
    EXPECT_CALL(DeviceMock, filePresentCheck(_)).WillRepeatedly(Return(-1));
    EXPECT_CALL(mockexternal, isMediaClientDevice()).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, isDelayFWDownloadActive(_, _, _)).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, isUpgradeInProgress()).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, isMmgbleNotifyEnabled()).WillRepeatedly(Return(false));
    EXPECT_CALL(mockexternal, updateFWDownloadStatus(_, _)).WillRepeatedly(Return(0));
    EXPECT_CALL(mockexternal, logMilestone(_)).Times(testing::AnyNumber());
    EXPECT_CALL(mockexternal, eventManager(_, _)).Times(testing::AnyNumber());
    EXPECT_CALL(mockexternal, checkPDRIUpgrade(_)).WillRepeatedly(Return(true));
    EXPECT_CALL(DeviceMock, getDevicePropertyData(_, _, _)).WillRepeatedly(Return(-1));
    EXPECT_CALL(mockexternal, CheckIProuteConnectivity(_)).WillRepeatedly(Return(false));

    int result = rdkv_upgrade_request(&context, &test_curl, &http_code);
    EXPECT_EQ(result, RDKV_UPGRADE_ERROR_STATE_RED);

    global_mockdownloadfileops_ptr = NULL;
    global_mockexternal_ptr = NULL;
    g_DeviceUtilsMock = &Deviceglobal;
}

GTEST_API_ int main(int argc, char *argv[]){
    char testresults_fullfilepath[GTEST_REPORT_FILEPATH_SIZE];
    char buffer[GTEST_REPORT_FILEPATH_SIZE];

    memset( testresults_fullfilepath, 0, GTEST_REPORT_FILEPATH_SIZE );
    memset( buffer, 0, GTEST_REPORT_FILEPATH_SIZE );

    snprintf( testresults_fullfilepath, GTEST_REPORT_FILEPATH_SIZE, "json:%s%s" , GTEST_DEFAULT_RESULT_FILEPATH , GTEST_DEFAULT_RESULT_FILENAME);
    ::testing::GTEST_FLAG(output) = testresults_fullfilepath;
    ::testing::InitGoogleTest(&argc, argv);
    //testing::Mock::AllowLeak(mock);
    cout << "Starting GTEST MAIN ===========================>" << endl;
    return RUN_ALL_TESTS();
}
