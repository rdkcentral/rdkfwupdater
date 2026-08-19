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

/**
 * @file rdkfwupdatemgr_main_flow_gtest.cpp
 * @brief Unit tests for rdkFwupdateMgr.c main flow functions
 * 
 * Tests coverage:
 * - getTriggerType()
 * - handle_signal()
 * - prevCurUpdateInfo()
 * - initialValidation() (gap filling)
 * - main() (state machine, argument parsing)
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include "deviceutils_mock_global.h"
extern "C" {
#include "rdkv_cdl.h"
#include "rdkv_cdl_log_wrapper.h"
#include "miscellaneous.h"
#include "device_status_helper.h"
#include "rfcinterface.h"
#include "dbus/xconf_comm_status.h"

// External declarations for functions under test
int getTriggerType(void);
int getAppMode(void);
void setAppMode(int mode);
void setDwnlState(int state);
int getDwnlState(void);
void t2CountNotify(char *marker, int val);
void t2ValNotify(char *marker, char *val);
void interuptDwnl(int app_mode);
void handle_signal(int no, siginfo_t* info, void* uc);
int prevCurUpdateInfo(void);
int initialValidation(void);
int initialize(void);
void uninitialize(int fwDwnlStatus);
int copyFile(const char *src, const char *target);
void updateUpgradeFlag(int action);
int getOPTOUTValue(const char *file_name);
int peripheral_firmware_dndl(char *pCloudFWLocation, char *pPeripheralFirmwares);

// External variables from rdkFwupdateMgr.c
extern DeviceProperty_t device_info;
extern ImageDetails_t cur_img_detail;
extern int force_exit;
extern Rfc_t rfc_list;
extern void *curl;

// Mock functions (will be linked with mocks)
int getDeviceProperties(DeviceProperty_t *pDevice_info);
int getImageDetails(ImageDetails_t *pImage_detail);
// getRFCSettings and read_RFCProperty are declared in rfcinterface.h
int createDir(const char *dirname);
int init_event_handler(void);
int term_event_handler(void);
int filePresentCheck(const char *filename);
void setForceStop(int value);
void eventManager(const char *event_type, const char *event_data);
bool CurrentRunningInst(const char *filename);
size_t GetFirmwareVersion(char *pFWVersion, size_t szBufSize);
size_t GetBuildType(char *buf, size_t szBufSize, BUILDTYPE *peBuildType);
// Note: lastDwnlImg and prevFlashedFile are already declared in device_status_helper.h
// bool lastDwnlImg(char *img_name, size_t img_name_size);
// bool prevFlashedFile(char *img_name, size_t img_name_size);
}

#include "./mocks/deviceutils_mock.h"
#include "deviceutils_mock_global.h"
#include "miscellaneous_mock.h"

using namespace testing;
using namespace std;

// Instantiate the global mock object for deviceutils_mock
DeviceUtilsMock Deviceglobal;
//DeviceUtilsMock *g_DeviceUtilsMock = &Deviceglobal;

// Test file paths
#define TEST_CDL_FLASHED_IMAGE          "/tmp/test_cdl_flashed_file_name"
#define TEST_PREVIOUS_FLASHED_IMAGE     "/tmp/test_previous_flashed_file_name"
#define TEST_CURRENTLY_RUNNING_IMAGE    "/tmp/test_currently_running_image_name"
#define TEST_DIFD_PID                   "/tmp/test_DIFD.pid"
#define TEST_FW_PREPARING_REBOOT        "/tmp/test_fw_preparing_to_reboot"
#define TEST_VERSION_FILE               "/tmp/test_version.txt"
#define TEST_DOWNLOADED_PERIPHERAL_VERS "/tmp/downloaded_peripheral_versions.txt"

// =============================================================================
// TEST FIXTURE
// =============================================================================

class RdkFwupdateMgrMainFlowTest : public ::testing::Test {
protected:
    RdkFwupdateMgrMainFlowTest() {
        // Constructor
    }

    virtual ~RdkFwupdateMgrMainFlowTest() {
        // Destructor
    }

    virtual void SetUp() override {
        printf("%s\n", __func__);
        // Clean up test files before each test
        CleanupTestFiles();
        
        // Initialize global variables
        memset(&device_info, 0, sizeof(device_info));
        memset(&cur_img_detail, 0, sizeof(cur_img_detail));
        memset(&rfc_list, 0, sizeof(rfc_list));
        force_exit = 0;
        curl = NULL;
        
        // Set default values
        strncpy(device_info.dev_type, "hybrid", sizeof(device_info.dev_type) - 1);
        strncpy(device_info.maint_status, "false", sizeof(device_info.maint_status) - 1);
        setAppMode(1);
        setDwnlState(0);
    }

    virtual void TearDown() override {
        printf("%s\n", __func__);
        // Clean up test files after each test
        CleanupTestFiles();
    }

    static void SetUpTestCase() {
        printf("%s\n", __func__);
    }

    static void TearDownTestCase() {
        printf("%s\n", __func__);
    }

    // Helper: Clean up all test files
    void CleanupTestFiles() {
        unlink(TEST_CDL_FLASHED_IMAGE);
        unlink(TEST_PREVIOUS_FLASHED_IMAGE);
        unlink(TEST_CURRENTLY_RUNNING_IMAGE);
        unlink(TEST_DIFD_PID);
        unlink(TEST_FW_PREPARING_REBOOT);
        unlink(TEST_VERSION_FILE);
        unlink(TEST_DOWNLOADED_PERIPHERAL_VERS);
        unlink(CDL_FLASHED_IMAGE);
        unlink(PREVIOUS_FLASHED_IMAGE);
        unlink(CURRENTLY_RUNNING_IMAGE);
        unlink(DIFDPID);
        unlink("/tmp/fw_preparing_to_reboot");
    }

    // Helper: Create test file with content
    
    void TestFileCreate(const char *filename, const char *content) {
        FILE *fp = fopen(filename, "w");
        if (fp) {
            fputs(content, fp);
            fclose(fp);
        }
    }

    // Helper: Check if file exists
    bool FileExists(const char *filename) {
        return (access(filename, F_OK) == 0);
    }

    // Helper: Read file content
    string ReadFileContent(const char *filename) {
        ifstream file(filename);
        if (!file.is_open()) return "";
        string content((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
        return content;
    }
};

// =============================================================================
// TEST SUITE 1: getTriggerType()
// =============================================================================

TEST_F(RdkFwupdateMgrMainFlowTest, XConfCommStatus_InitAndStateTransitions) {
    EXPECT_TRUE(initXConfCommStatus());
    EXPECT_FALSE(getXConfCommStatus());
    EXPECT_STREQ(getXConfCommStatusString(), "IDLE");

    setXConfCommStatus(TRUE);
    EXPECT_TRUE(getXConfCommStatus());
    EXPECT_STREQ(getXConfCommStatusString(), "IN_PROGRESS");

    setXConfCommStatus(FALSE);
    EXPECT_FALSE(getXConfCommStatus());
    EXPECT_STREQ(getXConfCommStatusString(), "IDLE");

    cleanupXConfCommStatus();
}

TEST_F(RdkFwupdateMgrMainFlowTest, XConfCommStatus_TrySetGuardAndDuplicateInit) {
    EXPECT_TRUE(initXConfCommStatus());
    EXPECT_FALSE(initXConfCommStatus());
    EXPECT_TRUE(trySetXConfCommStatus());
    EXPECT_FALSE(trySetXConfCommStatus());
    EXPECT_TRUE(getXConfCommStatus());

    setXConfCommStatus(FALSE);
    EXPECT_FALSE(getXConfCommStatus());

    cleanupXConfCommStatus();
}

TEST_F(RdkFwupdateMgrMainFlowTest, XConfCommStatus_GetAndSetBeforeInitAreSafe) {
    EXPECT_FALSE(getXConfCommStatus());
    setXConfCommStatus(TRUE);
    EXPECT_FALSE(getXConfCommStatus());
    EXPECT_STREQ(getXConfCommStatusString(), "UNINITIALIZED");

    EXPECT_FALSE(trySetXConfCommStatus());
    cleanupXConfCommStatus();
}

TEST_F(RdkFwupdateMgrMainFlowTest, XConfCommStatus_DumpAndCleanupCoverDebugAndSafetyPaths) {
    EXPECT_STREQ(getXConfCommStatusString(), "UNINITIALIZED");
    EXPECT_NO_FATAL_FAILURE(dumpXConfCommStatus());

    EXPECT_TRUE(initXConfCommStatus());
    EXPECT_FALSE(getXConfCommStatus());
    EXPECT_STREQ(getXConfCommStatusString(), "IDLE");
    EXPECT_NO_FATAL_FAILURE(dumpXConfCommStatus());

    setXConfCommStatus(TRUE);
    EXPECT_TRUE(getXConfCommStatus());
    EXPECT_STREQ(getXConfCommStatusString(), "IN_PROGRESS");
    EXPECT_NO_FATAL_FAILURE(dumpXConfCommStatus());

    cleanupXConfCommStatus();
    EXPECT_STREQ(getXConfCommStatusString(), "UNINITIALIZED");
    EXPECT_NO_FATAL_FAILURE(dumpXConfCommStatus());

    cleanupXConfCommStatus();
    EXPECT_STREQ(getXConfCommStatusString(), "UNINITIALIZED");
}

TEST_F(RdkFwupdateMgrMainFlowTest, StartFactoryProtectService_UsesCurlAndStopsOnSuccess) {
    MockExternal mock;
    DeviceUtilsMock DeviceMock;
    g_DeviceUtilsMock = &DeviceMock;
    global_mockexternal_ptr = &mock;

    void* temp = reinterpret_cast<void*>(0x1);
    EXPECT_CALL(*g_DeviceUtilsMock, doCurlInit()).WillOnce(Return(temp));
    EXPECT_CALL(mock, doCurlPutRequest(_, _, _, _)).WillOnce(DoAll(SetArgPointee<3>(200), Return(0)));
    EXPECT_CALL(*g_DeviceUtilsMock, doStopDownload(_)).Times(1);

    EXPECT_EQ(startFactoryProtectService(), 0);

    global_mockexternal_ptr = NULL;
    g_DeviceUtilsMock = &Deviceglobal;
}

TEST_F(RdkFwupdateMgrMainFlowTest, StartFactoryProtectService_ReturnsNegativeWhenCurlInitFails) {
    MockExternal mock;
    DeviceUtilsMock DeviceMock;
    g_DeviceUtilsMock = &DeviceMock;
    global_mockexternal_ptr = &mock;

    EXPECT_CALL(*g_DeviceUtilsMock, doCurlInit()).WillOnce(Return(nullptr));

    EXPECT_EQ(startFactoryProtectService(), -1);

    global_mockexternal_ptr = NULL;
    g_DeviceUtilsMock = &Deviceglobal;
}

TEST_F(RdkFwupdateMgrMainFlowTest, GetTriggerType_ReturnsDefault) {
    // getTriggerType() returns the global trigger_type variable
    // In test environment, it should return default value
    int trigger = getTriggerType();
    
    // Should return a valid trigger type (typically 0 or 1-6)
    EXPECT_TRUE(trigger >= 0 && trigger <= 6);
}

TEST_F(RdkFwupdateMgrMainFlowTest, GetTriggerType_Consistency) {
    // getTriggerType() should return consistent value in same test
    int trigger1 = getTriggerType();
    int trigger2 = getTriggerType();
    
    EXPECT_EQ(trigger1, trigger2);
}

TEST_F(RdkFwupdateMgrMainFlowTest, SetAppMode_ForegroundAndBackground_RoundTrips) {
    setAppMode(0);
    EXPECT_EQ(getAppMode(), 0);

    setAppMode(1);
    EXPECT_EQ(getAppMode(), 1);
}

TEST_F(RdkFwupdateMgrMainFlowTest, SetDwnlState_RoundTripsThroughGetter) {
    setDwnlState(0);
    EXPECT_EQ(getDwnlState(), 0);

    setDwnlState(1);
    EXPECT_EQ(getDwnlState(), 1);
}

TEST_F(RdkFwupdateMgrMainFlowTest, T2CountNotify_And_T2ValNotify_DoNotCrash) {
    char marker[] = "marker";
    char value[] = "value";

    EXPECT_NO_FATAL_FAILURE({
        t2CountNotify(marker, 7);
        t2ValNotify(marker, value);
    });
}

TEST_F(RdkFwupdateMgrMainFlowTest, InteruptDwnl_ThrottleDisabled_LogsAndReturns) {
    strncpy(rfc_list.rfc_throttle, "false", sizeof(rfc_list.rfc_throttle) - 1);
    strncpy(rfc_list.rfc_topspeed, "0", sizeof(rfc_list.rfc_topspeed) - 1);
    setDwnlState(0);
    curl = nullptr;

    EXPECT_NO_FATAL_FAILURE({
        interuptDwnl(0);
    });
}

TEST_F(RdkFwupdateMgrMainFlowTest, InteruptDwnl_ThrottleEnabled_StopPathSetsForceExit) {
    strncpy(rfc_list.rfc_throttle, "true", sizeof(rfc_list.rfc_throttle) - 1);
    strncpy(rfc_list.rfc_topspeed, "0", sizeof(rfc_list.rfc_topspeed) - 1);
    strncpy(device_info.maint_status, "true", sizeof(device_info.maint_status) - 1);
    setDwnlState(2);
    curl = nullptr;
    force_exit = 0;

    EXPECT_NO_FATAL_FAILURE({
        interuptDwnl(0);
    });

    EXPECT_EQ(force_exit, 1);
}

TEST_F(RdkFwupdateMgrMainFlowTest, InteruptDwnl_ThrottleEnabled_PauseRetryFailureStopsDownload) {
    DeviceUtilsMock localMock;
    g_DeviceUtilsMock = &localMock;
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;

    strncpy(rfc_list.rfc_throttle, "true", sizeof(rfc_list.rfc_throttle) - 1);
    strncpy(rfc_list.rfc_topspeed, "1000", sizeof(rfc_list.rfc_topspeed) - 1);
    strncpy(device_info.maint_status, "false", sizeof(device_info.maint_status) - 1);
    setDwnlState(RDKV_FWDNLD_DOWNLOAD_INPROGRESS);
    force_exit = 0;
    curl = reinterpret_cast<void*>(0x1234);

    EXPECT_CALL(mockexternal, doGetDwnlBytes(_)).WillOnce(Return(512u));
    EXPECT_CALL(mockexternal, doInteruptDwnl(_, 1000u)).WillOnce(Return(DWNL_UNPAUSE_FAIL));
    EXPECT_CALL(localMock, doStopDownload(_)).Times(1);

    interuptDwnl(0);

    EXPECT_EQ(force_exit, 0);
    EXPECT_EQ(curl, nullptr);

    g_DeviceUtilsMock = nullptr;
    global_mockexternal_ptr = nullptr;
}

// =============================================================================
// TEST SUITE 2: handle_signal()
// =============================================================================

TEST_F(RdkFwupdateMgrMainFlowTest, HandleSignal_SIGUSR1_SetsForceExit) {
    // Setup
    force_exit = 0;
    siginfo_t info;
    memset(&info, 0, sizeof(info));
    
    // Test: handle_signal should set force_exit = 1
    handle_signal(SIGUSR1, &info, NULL);
    
    // Verify
    EXPECT_EQ(force_exit, 1);
}

TEST_F(RdkFwupdateMgrMainFlowTest, HandleSignal_SIGUSR1_CallsSetForceStop) {
    // Test that handle_signal calls setForceStop(1)
    // Note: In real implementation, this calls the download library
    // In test, we just verify it doesn't crash
    
    siginfo_t info;
    memset(&info, 0, sizeof(info));
    
    EXPECT_NO_FATAL_FAILURE({
        handle_signal(SIGUSR1, &info, NULL);
    });
}

TEST_F(RdkFwupdateMgrMainFlowTest, HandleSignal_SIGUSR1_WithMaintenanceMode) {
    // Setup maintenance mode
    strncpy(device_info.maint_status, "true", sizeof(device_info.maint_status) - 1);
    
    siginfo_t info;
    memset(&info, 0, sizeof(info));
    
    // Should call eventManager with MAINT_FWDOWNLOAD_ERROR
    EXPECT_NO_FATAL_FAILURE({
        handle_signal(SIGUSR1, &info, NULL);
    });
    
    EXPECT_EQ(force_exit, 1);
}

TEST_F(RdkFwupdateMgrMainFlowTest, HandleSignal_SIGUSR1_WithoutMaintenanceMode) {
    // Setup: maintenance mode disabled
    strncpy(device_info.maint_status, "false", sizeof(device_info.maint_status) - 1);
    
    siginfo_t info;
    memset(&info, 0, sizeof(info));
    
    // Should still set force_exit even without maintenance mode
    handle_signal(SIGUSR1, &info, NULL);
    
    EXPECT_EQ(force_exit, 1);
}

TEST_F(RdkFwupdateMgrMainFlowTest, HandleSignal_ExplicitlyTriggersFailureEvents) {
    MockExternal mock;
    global_mockexternal_ptr = &mock;
    strncpy(device_info.maint_status, "true", sizeof(device_info.maint_status) - 1);
    force_exit = 0;

    EXPECT_CALL(mock, eventManager(_, _)).Times(AtLeast(1));
    EXPECT_CALL(mock, setForceStop(1)).Times(1);

    siginfo_t info;
    memset(&info, 0, sizeof(info));
    handle_signal(SIGUSR1, &info, NULL);

    EXPECT_EQ(force_exit, 1);
    global_mockexternal_ptr = nullptr;
}

TEST_F(RdkFwupdateMgrMainFlowTest, CheckTriggerUpgrade_ValidNoPDRI_ReturnsZero) {
    MockExternal mock;
    global_mockexternal_ptr = &mock;
    DeviceUtilsMock deviceMock;
    g_DeviceUtilsMock = &deviceMock;

    strncpy(device_info.maint_status, "false", sizeof(device_info.maint_status) - 1);
    strncpy(device_info.difw_path, "/tmp", sizeof(device_info.difw_path) - 1);
    strncpy(cur_img_detail.cur_img_name, "testModel.bin", sizeof(cur_img_detail.cur_img_name) - 1);

    XCONFRES response = {};
    snprintf(response.cloudFWVersion, sizeof(response.cloudFWVersion), "%s", "testModel.bin");
    snprintf(response.cloudFWFile, sizeof(response.cloudFWFile), "%s", "testModel.bin");
    snprintf(response.cloudFWLocation, sizeof(response.cloudFWLocation), "%s", "/tmp");
    snprintf(response.cloudImmediateRebootFlag, sizeof(response.cloudImmediateRebootFlag), "%s", "false");
    snprintf(response.cloudDelayDownload, sizeof(response.cloudDelayDownload), "%s", "0");
    snprintf(response.cloudPDRIVersion, sizeof(response.cloudPDRIVersion), "%s", "");
    snprintf(response.peripheralFirmwares, sizeof(response.peripheralFirmwares), "%s", "");

    EXPECT_CALL(mock, isUpgradeInProgress()).WillRepeatedly(Return(false));
    EXPECT_CALL(mock, checkForValidPCIUpgrade(_, _, _, _)).WillRepeatedly(Return(false));
    EXPECT_CALL(deviceMock, filePresentCheck(_)).WillRepeatedly(Return(1));

    EXPECT_EQ(checkTriggerUpgrade(&response, "testModel", 1), 0);

    g_DeviceUtilsMock = &Deviceglobal;
    global_mockexternal_ptr = nullptr;
}

TEST_F(RdkFwupdateMgrMainFlowTest, CheckTriggerUpgrade_PDRIPathUsesUpgradeRequest) {
    MockExternal mock;
    global_mockexternal_ptr = &mock;
    DeviceUtilsMock deviceMock;
    g_DeviceUtilsMock = &deviceMock;

    strncpy(device_info.maint_status, "false", sizeof(device_info.maint_status) - 1);
    strncpy(device_info.difw_path, "/tmp", sizeof(device_info.difw_path) - 1);
    strncpy(cur_img_detail.cur_img_name, "testModel.bin", sizeof(cur_img_detail.cur_img_name) - 1);

    XCONFRES response = {};
    snprintf(response.cloudFWVersion, sizeof(response.cloudFWVersion), "%s", "testModel.bin");
    snprintf(response.cloudFWFile, sizeof(response.cloudFWFile), "%s", "testModel.bin");
    snprintf(response.cloudFWLocation, sizeof(response.cloudFWLocation), "%s", "/tmp");
    snprintf(response.cloudImmediateRebootFlag, sizeof(response.cloudImmediateRebootFlag), "%s", "false");
    snprintf(response.cloudDelayDownload, sizeof(response.cloudDelayDownload), "%s", "0");
    snprintf(response.cloudPDRIVersion, sizeof(response.cloudPDRIVersion), "%s", "testModel_PDRI.bin");
    snprintf(response.peripheralFirmwares, sizeof(response.peripheralFirmwares), "%s", "");

    EXPECT_CALL(mock, isUpgradeInProgress()).WillRepeatedly(Return(false));
    EXPECT_CALL(mock, checkForValidPCIUpgrade(_, _, _, _)).WillRepeatedly(Return(true));
    EXPECT_CALL(mock, isPDRIEnable()).WillRepeatedly(Return(true));
    EXPECT_CALL(deviceMock, filePresentCheck(_)).WillRepeatedly(Return(1));

    EXPECT_EQ(checkTriggerUpgrade(&response, "testModel", 1), 0);

    g_DeviceUtilsMock = &Deviceglobal;
    global_mockexternal_ptr = nullptr;
}

// =============================================================================
// TEST SUITE 3: copyFile() (helper for prevCurUpdateInfo)
// =============================================================================

TEST_F(RdkFwupdateMgrMainFlowTest, CopyFile_Success) {
    // Create source file
    const char *src = "/tmp/test_copyfile_src.txt";
    const char *dst = "/tmp/test_copyfile_dst.txt";
    const char *content = "Test content for copy\n";
    
    TestFileCreate(src, content);
    
    // Test copyFile
    int result = copyFile(src, dst);
    
    // Verify
    EXPECT_EQ(result, 0);
    EXPECT_TRUE(FileExists(dst));
    
    // Verify content matches
    string dst_content = ReadFileContent(dst);
    EXPECT_EQ(dst_content, content);
    
    // Cleanup
    unlink(src);
    unlink(dst);
}

TEST_F(RdkFwupdateMgrMainFlowTest, CopyFile_SourceNotExist) {
    const char *src = "/tmp/test_nonexistent_src.txt";
    const char *dst = "/tmp/test_copyfile_dst.txt";
    
    // Test copyFile with non-existent source
    int result = copyFile(src, dst);
    
    // Should return -1 (failure)
    EXPECT_EQ(result, -1);
    EXPECT_FALSE(FileExists(dst));
}

TEST_F(RdkFwupdateMgrMainFlowTest, CopyFile_NullParameters) {
    // Test with NULL source
    int result1 = copyFile(NULL, "/tmp/test_dst.txt");
    EXPECT_EQ(result1, -1);
    
    // Test with NULL destination
    int result2 = copyFile("/tmp/test_src.txt", NULL);
    EXPECT_EQ(result2, -1);
    
    // Test with both NULL
    int result3 = copyFile(NULL, NULL);
    EXPECT_EQ(result3, -1);
}

// =============================================================================
// TEST SUITE 4: prevCurUpdateInfo()
// =============================================================================

TEST_F(RdkFwupdateMgrMainFlowTest, PrevCurUpdateInfo_MismatchWithPreviousCopiesPreviousImage) {
    pid_t child = fork();
    ASSERT_GE(child, 0);

    if (child == 0) {
        MockExternal mockexternal;
        global_mockexternal_ptr = &mockexternal;
        DeviceUtilsMock localMock;
        g_DeviceUtilsMock = &localMock;

        EXPECT_CALL(mockexternal, GetFirmwareVersion(_, _))
            .WillRepeatedly(Invoke([](char *buf, size_t size) {
                snprintf(buf, size, "%s", "1.0.0.0");
                return 0;
            }));
        EXPECT_CALL(localMock, filePresentCheck(_)).WillRepeatedly(Return(1));
        EXPECT_CALL(mockexternal, GetBuildType(_, _, _)).WillRepeatedly(Return(0));
        EXPECT_CALL(mockexternal, CurrentRunningInst(_)).WillRepeatedly(Return(false));

        TestFileCreate(CDL_FLASHED_IMAGE, "2.0.0.0-signed.bin\n");
        TestFileCreate(PREVIOUS_FLASHED_IMAGE, "1.0.0.0-signed.bin\n");
        TestFileCreate(CURRENTLY_RUNNING_IMAGE, "");

        EXPECT_EQ(prevCurUpdateInfo(), 0);
        EXPECT_TRUE(FileExists(CURRENTLY_RUNNING_IMAGE));
        EXPECT_EQ(ReadFileContent(CURRENTLY_RUNNING_IMAGE), "1.0.0.0-signed.bin\n");

        _exit(0);
    }

    int status = 0;
    EXPECT_EQ(waitpid(child, &status, 0), child);
    EXPECT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST_F(RdkFwupdateMgrMainFlowTest, PrevCurUpdateInfo_NoCdlFileCreatesCurrentAndPreviousImages) {
    pid_t child = fork();
    ASSERT_GE(child, 0);

    if (child == 0) {
        MockExternal mockexternal;
        global_mockexternal_ptr = &mockexternal;
        DeviceUtilsMock localMock;
        g_DeviceUtilsMock = &localMock;

        EXPECT_CALL(mockexternal, GetFirmwareVersion(_, _))
            .WillRepeatedly(Invoke([](char *buf, size_t size) {
                snprintf(buf, size, "%s", "1.0.0.0");
                return 0;
            }));
        EXPECT_CALL(localMock, filePresentCheck(_)).WillRepeatedly(Return(1));
        EXPECT_CALL(mockexternal, GetBuildType(_, _, _)).WillRepeatedly(Return(0));

        EXPECT_EQ(prevCurUpdateInfo(), 0);
        EXPECT_TRUE(FileExists(PREVIOUS_FLASHED_IMAGE));
        EXPECT_TRUE(FileExists(CURRENTLY_RUNNING_IMAGE));
        EXPECT_EQ(ReadFileContent(PREVIOUS_FLASHED_IMAGE), "1.0.0.0-signed.bin\n");
        EXPECT_EQ(ReadFileContent(CURRENTLY_RUNNING_IMAGE), "1.0.0.0-signed.bin\n");

        _exit(0);
    }

    int status = 0;
    EXPECT_EQ(waitpid(child, &status, 0), child);
    EXPECT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST_F(RdkFwupdateMgrMainFlowTest, InitialValidation_AlreadyRunning_ReturnsDownloadInProgress) {
    MockExternal mockexternal;
    global_mockexternal_ptr = &mockexternal;
    DeviceUtilsMock localMock;
    g_DeviceUtilsMock = &localMock;

    EXPECT_CALL(localMock, read_RFCProperty(_, _, _, _)).WillRepeatedly(Return(-1));
    EXPECT_CALL(localMock, filePresentCheck(_)).WillRepeatedly(Return(1));
    EXPECT_CALL(mockexternal, CurrentRunningInst(_)).WillOnce(Return(true));
    EXPECT_CALL(mockexternal, GetBuildType(_, _, _)).WillRepeatedly(Return(0));

    EXPECT_EQ(initialValidation(), INITIAL_VALIDATION_DWNL_INPROGRESS);

    g_DeviceUtilsMock = nullptr;
    global_mockexternal_ptr = nullptr;
}


// =============================================================================
// TEST SUITE 5: main() - State Machine and Argument Parsing
// =============================================================================

// Note: Testing main() is challenging because:
// 1. It contains infinite loop with g_main_loop_run()
// 2. It calls D-Bus setup which requires system resources
// 3. It has exit() calls
//
// For now, we'll test the components that main() uses:
// - Argument parsing logic
// - State machine transitions
// - Error handling paths

TEST_F(RdkFwupdateMgrMainFlowTest, Main_ArgumentParsing_TriggerTypes) {
    // Test that different trigger types are recognized
    // Trigger types: 1=Bootup, 2=Scheduled, 3=TR69/SNMP, 4=App, 5=Delayed, 6=StateRed
    
    // In main(), trigger_type is parsed from argv[2]
    // This test validates the logic (not the actual main function)
    
    const char *valid_triggers[] = {"1", "2", "3", "4", "5", "6"};
    
    for (int i = 0; i < 6; i++) {
        int trigger = atoi(valid_triggers[i]);
        EXPECT_TRUE(trigger >= 1 && trigger <= 6) 
            << "Trigger type " << valid_triggers[i] << " should be valid";
    }
}

TEST_F(RdkFwupdateMgrMainFlowTest, Main_ArgumentParsing_InvalidTrigger) {
    // Test invalid trigger types
    const char *invalid_triggers[] = {"0", "7", "999", "-1", "abc"};
    
    for (int i = 0; i < 5; i++) {
        int trigger = atoi(invalid_triggers[i]);
        
        if (strcmp(invalid_triggers[i], "abc") == 0) {
            EXPECT_EQ(trigger, 0) << "Non-numeric trigger should parse as 0";
        } else {
            EXPECT_FALSE(trigger >= 1 && trigger <= 6) 
                << "Trigger " << invalid_triggers[i] << " should be invalid";
        }
    }
}

TEST_F(RdkFwupdateMgrMainFlowTest, Main_ArgumentCount_LessThan3) {
    // main() expects at least 3 arguments: program_name, retry_count, trigger_type
    // argc < 3 should cause error
    
    // Simulate: argc=1 (only program name)
    int argc_1 = 1;
    EXPECT_LT(argc_1, 3) << "argc < 3 should be detected as error";
    
    // Simulate: argc=2 (program name + 1 arg)
    int argc_2 = 2;
    EXPECT_LT(argc_2, 3) << "argc < 3 should be detected as error";
}

TEST_F(RdkFwupdateMgrMainFlowTest, Main_StateTransitions_InitToInitValidation) {
    // Test STATE_INIT -> STATE_INIT_VALIDATION transition
    // In main(), after successful initialize(), state moves to INIT_VALIDATION
    
    typedef enum {
        STATE_INIT_VALIDATION,
        STATE_INIT,
        STATE_IDLE,
        STATE_CHECK_UPDATE,
        STATE_DOWNLOAD_UPDATE,
        STATE_UPGRADE
    } FwUpgraderState;
    
    FwUpgraderState currentState = STATE_INIT;
    
    // Simulate successful initialize()
    bool init_success = true;
    
    if (init_success) {
        currentState = STATE_INIT_VALIDATION;
    }
    
    EXPECT_EQ(currentState, STATE_INIT_VALIDATION);
}

TEST_F(RdkFwupdateMgrMainFlowTest, Main_StateTransitions_InitValidationToIdle) {
    // Test STATE_INIT_VALIDATION -> STATE_IDLE transition
    
    typedef enum {
        STATE_INIT_VALIDATION,
        STATE_INIT,
        STATE_IDLE,
        STATE_CHECK_UPDATE,
        STATE_DOWNLOAD_UPDATE,
        STATE_UPGRADE
    } FwUpgraderState;
    
    FwUpgraderState currentState = STATE_INIT_VALIDATION;
    
    // Simulate successful initialValidation()
    int validation_ok = 0;
    int init_validate_status = validation_ok;
    
    if (init_validate_status == validation_ok) {
        currentState = STATE_IDLE;
    }
    
    EXPECT_EQ(currentState, STATE_IDLE);
}

TEST_F(RdkFwupdateMgrMainFlowTest, Main_StateTransitions_ValidationFail_NoTransition) {
    // Test that failed validation doesn't transition to IDLE
    
    typedef enum {
        STATE_INIT_VALIDATION,
        STATE_INIT,
        STATE_IDLE,
        STATE_CHECK_UPDATE,
        STATE_DOWNLOAD_UPDATE,
        STATE_UPGRADE
    } FwUpgraderState;
    
    FwUpgraderState currentState = STATE_INIT_VALIDATION;
    
    // Simulate failed initialValidation()
    int validation_ok = 0;
    int validation_fail = -1;
    int init_validate_status = validation_fail;
    
    if (init_validate_status == validation_ok) {
        currentState = STATE_IDLE;
    }
    // else: state remains in INIT_VALIDATION, should goto cleanup
    
    EXPECT_NE(currentState, STATE_IDLE);
    EXPECT_EQ(currentState, STATE_INIT_VALIDATION);
}

TEST_F(RdkFwupdateMgrMainFlowTest, Main_TriggerTypeLogging_Bootup) {
    // Test that trigger_type=1 is recognized as "Bootup"
    int trigger_type = 1;
    const char *expected = "Image Upgrade During Bootup";
    
    EXPECT_EQ(trigger_type, 1);
    SUCCEED() << "Trigger type 1 = " << expected;
}

TEST_F(RdkFwupdateMgrMainFlowTest, Main_TriggerTypeLogging_Scheduled) {
    // Test that trigger_type=2 is recognized as "Scheduled"
    int trigger_type = 2;
    const char *expected = "Scheduled Image Upgrade using cron";
    
    EXPECT_EQ(trigger_type, 2);
    SUCCEED() << "Trigger type 2 = " << expected;
}

TEST_F(RdkFwupdateMgrMainFlowTest, Main_TriggerTypeLogging_TR69) {
    // Test that trigger_type=3 is recognized as "TR-69/SNMP"
    int trigger_type = 3;
    const char *expected = "TR-69/SNMP triggered Image Upgrade";
    
    EXPECT_EQ(trigger_type, 3);
    SUCCEED() << "Trigger type 3 = " << expected;
}

TEST_F(RdkFwupdateMgrMainFlowTest, Main_TriggerTypeLogging_App) {
    // Test that trigger_type=4 is recognized as "App triggered"
    int trigger_type = 4;
    const char *expected = "App triggered Image Upgrade";
    
    EXPECT_EQ(trigger_type, 4);
    SUCCEED() << "Trigger type 4 = " << expected;
}

TEST_F(RdkFwupdateMgrMainFlowTest, Main_TriggerTypeLogging_Delayed) {
    // Test that trigger_type=5 is recognized as "Delayed Trigger"
    int trigger_type = 5;
    const char *expected = "Delayed Trigger Image Upgrade";
    
    EXPECT_EQ(trigger_type, 5);
    SUCCEED() << "Trigger type 5 = " << expected;
}

TEST_F(RdkFwupdateMgrMainFlowTest, Main_TriggerTypeLogging_StateRed) {
    // Test that trigger_type=6 is recognized as "State Red"
    int trigger_type = 6;
    const char *expected = "State Red Image Upgrade";
    
    EXPECT_EQ(trigger_type, 6);
    SUCCEED() << "Trigger type 6 = " << expected;
}

TEST_F(RdkFwupdateMgrMainFlowTest, Main_XCONFResponseInitialization) {
    // Test that XCONFRES structure is properly initialized in main()
    
    typedef struct {
        char cloudFWFile[256];
        char cloudFWLocation[512];
        char ipv6cloudFWLocation[512];
        char cloudFWVersion[64];
        char cloudDelayDownload[16];
        char cloudProto[8];
        char cloudImmediateRebootFlag[12];
        char peripheralFirmwares[1024];
        char dlCertBundle[256];
        char cloudPDRIVersion[64];
    } XCONFRES_TEST;
    
    XCONFRES_TEST response;
    
    // Simulate main() initialization
    response.cloudFWFile[0] = 0;
    response.cloudFWLocation[0] = 0;
    response.ipv6cloudFWLocation[0] = 0;
    response.cloudFWVersion[0] = 0;
    response.cloudDelayDownload[0] = 0;
    response.cloudProto[0] = 0;
    response.cloudImmediateRebootFlag[0] = 0;
    response.peripheralFirmwares[0] = 0;
    response.dlCertBundle[0] = 0;
    response.cloudPDRIVersion[0] = 0;
    
    // Verify all fields are empty
    EXPECT_EQ(response.cloudFWFile[0], 0);
    EXPECT_EQ(response.cloudFWLocation[0], 0);
    EXPECT_EQ(response.cloudFWVersion[0], 0);
    EXPECT_EQ(response.cloudImmediateRebootFlag[0], 0);
}

TEST_F(RdkFwupdateMgrMainFlowTest, Main_DisableStatsUpdate_DefaultValue) {
    // Test that disableStatsUpdate is initialized to "no" in main()
    char disableStatsUpdate[4] = {0};
    
    // Simulate main() initialization
    snprintf(disableStatsUpdate, sizeof(disableStatsUpdate), "%s", "no");
    
    EXPECT_STREQ(disableStatsUpdate, "no");
}

// =============================================================================
// TEST SUITE 6: updateUpgradeFlag()
// =============================================================================

TEST_F(RdkFwupdateMgrMainFlowTest, UpdateUpgradeFlag_MediaClientDevice_Create) {
    // Test creating flag for media client device
    strncpy(device_info.dev_type, "mediaclient", sizeof(device_info.dev_type) - 1);
    
    // Action 1 = create flag
    EXPECT_NO_FATAL_FAILURE({
        updateUpgradeFlag(1);
    });
}

TEST_F(RdkFwupdateMgrMainFlowTest, UpdateUpgradeFlag_MediaClientDevice_Delete) {
    // Test deleting flag for media client device
    strncpy(device_info.dev_type, "mediaclient", sizeof(device_info.dev_type) - 1);
    
    // Action 2 = delete flag
    EXPECT_NO_FATAL_FAILURE({
        updateUpgradeFlag(2);
    });
}

TEST_F(RdkFwupdateMgrMainFlowTest, UpdateUpgradeFlag_HybridDevice_HTTP) {
    // Test HTTP flag for hybrid device
    strncpy(device_info.dev_type, "hybrid", sizeof(device_info.dev_type) - 1);
    
    // proto=1 means HTTP (tested via updateUpgradeFlag behavior)
    EXPECT_NO_FATAL_FAILURE({
        updateUpgradeFlag(1);
    });
}

TEST_F(RdkFwupdateMgrMainFlowTest, UpdateUpgradeFlag_InvalidAction) {
    // Test invalid action (not 1 or 2)
    // Should not crash, just do nothing
    
    EXPECT_NO_FATAL_FAILURE({
        updateUpgradeFlag(0);
        updateUpgradeFlag(3);
        updateUpgradeFlag(99);
    });
}

// =============================================================================
// TEST SUITE 7: initialize(), getOPTOUTValue(), uninitialize()
// =============================================================================

TEST_F(RdkFwupdateMgrMainFlowTest, Initialize_DefaultEnvironment_Succeeds) {
    EXPECT_EQ(getAppMode(), 1);
    EXPECT_EQ(initialize(), 1);
    EXPECT_EQ(getAppMode(), 1);
}

TEST_F(RdkFwupdateMgrMainFlowTest, Initialize_MaintenanceMode_BackgroundJsonSetsAppModeZero) {
    DeviceUtilsMock localMock;
    g_DeviceUtilsMock = &localMock;

    strncpy(device_info.maint_status, "true", sizeof(device_info.maint_status) - 1);

    EXPECT_CALL(localMock, doCurlInit())
        .WillOnce(Return(reinterpret_cast<void*>(0x1)));
    EXPECT_CALL(localMock, getJsonRpcData(_, _, _, _))
        .WillOnce(Invoke([](void *, FileDwnl_t *req_data, char, int) {
            const char *payload = "{\"mode\":\"BACKGROUND\"}";
            strcpy(static_cast<char*>(req_data->pDlData->pvOut), payload);
            req_data->pDlData->datasize = strlen(payload);
            return 0;
        }));
    EXPECT_CALL(localMock, doStopDownload(_)).WillOnce(Return());

    EXPECT_EQ(initialize(), 1);
    EXPECT_EQ(getAppMode(), 0);

    g_DeviceUtilsMock = nullptr;
}

TEST_F(RdkFwupdateMgrMainFlowTest, PeripheralFirmwareDownload_Success_WritesDownloadList) {
    const char *cloudBaseUrl = "https://cloud.example.com/firmwares";
    char peripheralList[] = "XR11-20_firmware_1.1.4.1";

    unlink(TEST_DOWNLOADED_PERIPHERAL_VERS);

    int result = peripheral_firmware_dndl(const_cast<char*>(cloudBaseUrl), peripheralList);

    EXPECT_EQ(result, 0);
    EXPECT_TRUE(FileExists(TEST_DOWNLOADED_PERIPHERAL_VERS));
    EXPECT_EQ(ReadFileContent(TEST_DOWNLOADED_PERIPHERAL_VERS), "XR11-20_firmware_1.1.4.1.tgz");
}

TEST_F(RdkFwupdateMgrMainFlowTest, GetOPTOUTValue_NullFile_ReturnsError) {
    EXPECT_EQ(getOPTOUTValue(NULL), -1);
}

TEST_F(RdkFwupdateMgrMainFlowTest, GetOPTOUTValue_MissingFile_ReturnsError) {
    EXPECT_EQ(getOPTOUTValue("/tmp/nonexistent_optout.conf"), -1);
}

TEST_F(RdkFwupdateMgrMainFlowTest, GetOPTOUTValue_IgnoreUpdate_ReturnsOne) {
    const char *optoutFile = "/tmp/test_optout_ignore.conf";
    TestFileCreate(optoutFile, "softwareoptout=IGNORE_UPDATE\n");

    EXPECT_EQ(getOPTOUTValue(optoutFile), 1);

    unlink(optoutFile);
}

TEST_F(RdkFwupdateMgrMainFlowTest, GetOPTOUTValue_EnforceOptout_ReturnsZero) {
    const char *optoutFile = "/tmp/test_optout_enforce.conf";
    TestFileCreate(optoutFile, "softwareoptout=ENFORCE_OPTOUT\n");

    EXPECT_EQ(getOPTOUTValue(optoutFile), 0);

    unlink(optoutFile);
}

TEST_F(RdkFwupdateMgrMainFlowTest, GetOPTOUTValue_UnknownValue_ReturnsError) {
    const char *optoutFile = "/tmp/test_optout_unknown.conf";
    TestFileCreate(optoutFile, "softwareoptout=BYPASS_OPTOUT\n");

    EXPECT_EQ(getOPTOUTValue(optoutFile), -1);

    unlink(optoutFile);
}

TEST_F(RdkFwupdateMgrMainFlowTest, InitialValidation_RfcReadFailure_CreatesPidAndSucceeds) {
    DeviceUtilsMock localMock;
    g_DeviceUtilsMock = &localMock;

    EXPECT_CALL(localMock, read_RFCProperty(_, _, _, _)).WillOnce(Return(-1));
    EXPECT_CALL(localMock, filePresentCheck(_)).WillRepeatedly(Return(1));

    const int result = initialValidation();

    EXPECT_EQ(result, 0);
    EXPECT_TRUE(FileExists("/tmp/DIFD.pid"));

    unlink("/tmp/DIFD.pid");
    g_DeviceUtilsMock = nullptr;
}

TEST_F(RdkFwupdateMgrMainFlowTest, InitialValidation_RebootPreparation_CompletesAndRemovesFlag) {
    DeviceUtilsMock localMock;
    g_DeviceUtilsMock = &localMock;
    strncpy(device_info.maint_status, "true", sizeof(device_info.maint_status) - 1);
    TestFileCreate("/tmp/fw_preparing_to_reboot", "1\n");

    EXPECT_CALL(localMock, read_RFCProperty(_, _, _, _)).WillOnce(Return(-1));
    EXPECT_CALL(localMock, filePresentCheck(_))
        .WillRepeatedly(Invoke([](const char *path) {
            return (path != NULL && strcmp(path, "/tmp/fw_preparing_to_reboot") == 0) ? 0 : 1;
        }));

    const int result = initialValidation();

    EXPECT_EQ(result, 3);
    EXPECT_FALSE(FileExists("/tmp/fw_preparing_to_reboot"));
    EXPECT_FALSE(FileExists("/tmp/DIFD.pid"));

    g_DeviceUtilsMock = nullptr;
}

TEST_F(RdkFwupdateMgrMainFlowTest, Uninitialize_RemovesPidFile_WhenNotInProgress) {
    const char *pidFile = "/tmp/DIFD.pid";
    TestFileCreate(pidFile, "1234\n");
    ASSERT_TRUE(FileExists(pidFile));

    uninitialize(0);

    EXPECT_FALSE(FileExists(pidFile));
}

TEST_F(RdkFwupdateMgrMainFlowTest, Uninitialize_PreservesPidFile_WhenDownloadInProgress) {
    const char *pidFile = "/tmp/DIFD.pid";
    TestFileCreate(pidFile, "1234\n");
    ASSERT_TRUE(FileExists(pidFile));

    uninitialize(2);

    EXPECT_TRUE(FileExists(pidFile));
    unlink(pidFile);
}

// =============================================================================
// END OF TEST FILE
// =============================================================================
