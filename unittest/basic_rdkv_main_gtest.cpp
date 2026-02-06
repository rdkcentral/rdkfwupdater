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
#include <gtest/gtest.h> 
#include <iostream>
#include <unistd.h>

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


#define JSON_STR_LEN        1000

DeviceUtilsMock Deviceglobal;
DeviceUtilsMock *g_DeviceUtilsMock = &Deviceglobal;

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
    int checkTriggerUpgrade(XCONFRES *response, const char *model_num);
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
    int eraseFolderExcePramaFile(const char *folder, const char* file_name, const char *model_num);
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
    int result = checkTriggerUpgrade(&response, "testModel");

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
    int result = checkTriggerUpgrade(&response, "testModel");

    // Check the result
    EXPECT_EQ(result, 0);
    global_mockexternal_ptr = NULL;
    global_mockdownloadfileops_ptr = NULL;
}

TEST(checkTriggerUpgradeTest, TestFailNull) {
    XCONFRES response;
    int result = checkTriggerUpgrade(&response, NULL);
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
    int result = checkTriggerUpgrade(&response, "testModel");

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
    int result = checkTriggerUpgrade(&response, "testModel");
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
    int result = checkTriggerUpgrade(&response, "testModel");
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
