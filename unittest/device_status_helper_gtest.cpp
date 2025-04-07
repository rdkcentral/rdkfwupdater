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

extern "C" {
#include "device_status_helper.h"
#include "download_status_helper.h"
#include "rfcinterface.h"
}
#include "./mocks/device_status_helper_mock.h"

#define JSON_STR_LEN        1000
Rfc_t rfc_list;

#define GTEST_DEFAULT_RESULT_FILEPATH "/tmp/Gtest_Report/"
#define GTEST_DEFAULT_RESULT_FILENAME "RdkFwDwnld_DeviceStsHlpr_gtest_report.json"
#define GTEST_REPORT_FILEPATH_SIZE 256

using namespace testing;
using namespace std;
using ::testing::Return;
using ::testing::StrEq;


DeviceStatusMock *g_DeviceStatusMock = NULL;

class CreateJsonTestFixture : public ::testing::Test {
    protected:
	DeviceStatusMock mockDeviceStatus;

        CreateJsonTestFixture()
        {
            g_DeviceStatusMock = &mockDeviceStatus;
        }
        virtual ~CreateJsonTestFixture()
        {
            g_DeviceStatusMock = NULL;
        }

         virtual void SetUp()
        {
            printf("%s\n", __func__);
        }

        virtual void TearDown()
        {
            printf("%s\n", __func__);
        }

        static void SetUpTestCase()
        {
            printf("%s\n", __func__);
        }

        static void TearDownTestCase()
        {
            printf("%s\n", __func__);
        }
};


TEST_F(CreateJsonTestFixture, TestName_checkPDRIUpgrade_Null) 
{
    EXPECT_EQ(checkPDRIUpgrade(NULL), false);
}
TEST_F(CreateJsonTestFixture, TestName_checkPDRIUpgrade_notrq) 
{
    char pdri_image[] = "Test_pdri.bin";
    EXPECT_CALL(*g_DeviceStatusMock, getDevicePropertyData(_, _, _)).Times(1).WillOnce(Return(0));
    EXPECT_CALL(*g_DeviceStatusMock, GetPDRIFileName(_, _))
                .Times(1)
                .WillOnce(Invoke([&pdri_image]( char *pPDRIFilename, size_t szBufSize ) {
                strncpy(pPDRIFilename, pdri_image, szBufSize - 1);
                pPDRIFilename[szBufSize - 1] = '\0';  // Ensure null-termination
                return strlen(pPDRIFilename);
        }));
    	EXPECT_EQ(checkPDRIUpgrade(pdri_image), false);
}
TEST_F(CreateJsonTestFixture, TestName_checkPDRIUpgrade_rq) 
{
    char pdri_image[] = "Test_pdri.bin";
    EXPECT_CALL(*g_DeviceStatusMock, getDevicePropertyData(_, _, _)).Times(1).WillOnce(Return(0));
    EXPECT_CALL(*g_DeviceStatusMock, GetPDRIFileName(_, _))
                .Times(1)
                .WillOnce(Invoke([&pdri_image]( char *pPDRIFilename, size_t szBufSize ) {
                strncpy(pPDRIFilename, pdri_image, szBufSize - 1);
                pPDRIFilename[szBufSize - 1] = '\0';  // Ensure null-termination
                return strlen(pPDRIFilename);
        }));
    	EXPECT_EQ(checkPDRIUpgrade("Test_false_pdri.bin"), true);
}
TEST_F(CreateJsonTestFixture, TestName_GetPDRIVersion_NullCheck) 
{
    EXPECT_EQ(GetPDRIVersion(NULL, 0), false);
}
TEST_F(CreateJsonTestFixture, TestName_GetPDRIVersion)
{
    char pdri_image[] = "Test_pdri.bin";
    char pdri_image_data[64] = {0};
    EXPECT_CALL(*g_DeviceStatusMock, getDevicePropertyData(_, _, _)).Times(1).WillOnce(Return(0));
    EXPECT_CALL(*g_DeviceStatusMock, GetPDRIFileName(_, _))
                .Times(1)
                .WillOnce(Invoke([&pdri_image]( char *pPDRIFilename, size_t szBufSize ) {
                strncpy(pPDRIFilename, pdri_image, szBufSize - 1);
                pPDRIFilename[szBufSize - 1] = '\0';  // Ensure null-termination
                return strlen(pPDRIFilename);
        }));
    	EXPECT_EQ(GetPDRIVersion(pdri_image_data, sizeof(pdri_image_data)), true);
}
TEST_F(CreateJsonTestFixture, TestName_isPDRIEnable)
{
    EXPECT_CALL(*g_DeviceStatusMock, getDevicePropertyData(_, _, _)).Times(1).WillOnce(Return(0));
    EXPECT_EQ(isPDRIEnable(), true);
}
TEST_F(CreateJsonTestFixture, TestName) {

    EXPECT_CALL(*g_DeviceStatusMock, getDevicePropertyData(_, _, _)).Times(3).WillRepeatedly(Return(0));
    char expectedData[] = "AA:bb:cc:dd";
    EXPECT_CALL(*g_DeviceStatusMock, GetEstbMac(_, _))
		.Times(1)
    		.WillOnce(Invoke([&expectedData](char* pEstbMac, size_t szBufSize) {
        	strncpy(pEstbMac, expectedData, szBufSize - 1);
        	pEstbMac[szBufSize - 1] = '\0';  // Ensure null-termination
        	return strlen(pEstbMac);
    	}));
    EXPECT_CALL(*g_DeviceStatusMock, GetFirmwareVersion (_, _)).Times(1).WillOnce(Return(20));
    char pdri_img[] = "Comcat-Gtest-pdri.bin";
    EXPECT_CALL(*g_DeviceStatusMock, GetAdditionalFwVerInfo(_, _))
		.Times(1)
    		.WillOnce(Invoke([&pdri_img](char *pAdditionalFwVerInfo, size_t szBufSize) {
        	strncpy(pAdditionalFwVerInfo, pdri_img, szBufSize - 1);
        	pAdditionalFwVerInfo[szBufSize - 1] = '\0';  // Ensure null-termination
        	return strlen(pAdditionalFwVerInfo);
    	}));
    EXPECT_CALL(*g_DeviceStatusMock, GetBuildType (_, _, _)).Times(1).WillOnce(Return(4));
    EXPECT_CALL(*g_DeviceStatusMock, GetModelNum (_, _)).Times(1).WillOnce(Return(5));
    EXPECT_CALL(*g_DeviceStatusMock, GetMFRName (_, _)).Times(1).WillOnce(Return(7));
    EXPECT_CALL(*g_DeviceStatusMock, GetPartnerId (_, _)).Times(1).WillOnce(Return(6));
    EXPECT_CALL(*g_DeviceStatusMock, GetOsClass (_, _)).Times(1).WillOnce(Return(2));
    EXPECT_CALL(*g_DeviceStatusMock, GetExperience (_, _)).Times(1).WillOnce(Return(2));
    EXPECT_CALL(*g_DeviceStatusMock, GetAccountID (_, _)).Times(1).WillOnce(Return(18));
    EXPECT_CALL(*g_DeviceStatusMock, GetSerialNum (_, _)).Times(1).WillOnce(Return(18));
    EXPECT_CALL(*g_DeviceStatusMock, GetUTCTime (_, _)).Times(1).WillOnce(Return(6));
    EXPECT_CALL(*g_DeviceStatusMock, GetInstalledBundles (_, _)).Times(1).WillOnce(Return(7));
    EXPECT_CALL(*g_DeviceStatusMock, GetRdmManifestVersion (_, _)).Times(1).WillOnce(Return(19));
    EXPECT_CALL(*g_DeviceStatusMock, GetTimezone  (_, _, _)).Times(1).WillOnce(Return(7));
    EXPECT_CALL(*g_DeviceStatusMock, GetCapabilities  (_, _)).Times(1).WillOnce(Return(2));
    EXPECT_CALL(*g_DeviceStatusMock, filePresentCheck (_)).Times(1).WillOnce(Return(0));

    char *pJSONStr = NULL;
    pJSONStr = (char *) malloc(JSON_STR_LEN);
    size_t szPostFieldOut = JSON_STR_LEN;
    size_t result = 0;
    if (pJSONStr != NULL) {
	cout << "Calling createJsonString==================>" << endl;
    	result = createJsonString(pJSONStr, szPostFieldOut);
    	EXPECT_NE(result, 0);
    }
    if (pJSONStr != NULL) {
	    free(pJSONStr);
	    pJSONStr = NULL;
    }
}

TEST_F(CreateJsonTestFixture, TestName_CheckIProuteConnectivity_NullCheck)
{
    EXPECT_EQ(CheckIProuteConnectivity(NULL), false);
}
TEST_F(CreateJsonTestFixture, TestName_CheckIProuteConnectivity_success)
{
    EXPECT_CALL(*g_DeviceStatusMock, isConnectedToInternet()).Times(1).WillOnce(Return(true));
    EXPECT_CALL(*g_DeviceStatusMock, filePresentCheck (_)).Times(1).WillOnce(Return(0));
    EXPECT_EQ(CheckIProuteConnectivity("TEST.txt"), true);
}
TEST_F(CreateJsonTestFixture, TestName_CheckIProuteConnectivity_offline)
{
    EXPECT_CALL(*g_DeviceStatusMock, isConnectedToInternet()).Times(1).WillOnce(Return(false));
    EXPECT_CALL(*g_DeviceStatusMock, filePresentCheck (_)).Times(1).WillOnce(Return(0));
    EXPECT_EQ(CheckIProuteConnectivity("TEST.txt"), false);
}
TEST_F(CreateJsonTestFixture, TestName_CheckIProuteConnectivity_fail)
{
    EXPECT_CALL(*g_DeviceStatusMock, isConnectedToInternet()).Times(1).WillOnce(Return(false));
    EXPECT_CALL(*g_DeviceStatusMock, filePresentCheck (_)).Times(5).WillRepeatedly(Return(1));
    EXPECT_EQ(CheckIProuteConnectivity("TEST.txt"), false);
}
/*TEST_F(CreateJsonTestFixture, TestName_isDelayFWDownloadActive)
{
    EXPECT_CALL(*g_DeviceStatusMock, eventManager(_, _)).Times(1).WillOnce(Return());
    EXPECT_EQ(isDelayFWDownloadActive(1, "true", 4), true);
}*/
TEST_F(CreateJsonTestFixture, TestName_isDelayFWDownloadActive_no)
{
    //EXPECT_CALL(*g_DeviceStatusMock, eventManager(_, _)).Times(1).WillOnce(Return());
    EXPECT_EQ(isDelayFWDownloadActive(1, "true", 5), true);
}
/*TEST_F(CreateJsonTestFixture, TestName_checkForValidPCIUpgrade_NullCheck)
{
    EXPECT_EQ(checkForValidPCIUpgrade(1, NULL, NULL, NULL), false);
}
TEST_F(CreateJsonTestFixture, TestName_checkForValidPCIUpgrade_failpdri)
{
    //EXPECT_CALL(*g_DeviceStatusMock, eventManager(_, _)).Times(1).WillOnce(Return());
    EXPECT_EQ(checkForValidPCIUpgrade(1, "123", "123_PDRI_34", "123_PDRI_34"), false);
}
TEST_F(CreateJsonTestFixture, TestName_checkForValidPCIUpgrade_Success)
{
    //EXPECT_CALL(*g_DeviceStatusMock, eventManager(_, _)).Times(1).WillOnce(Return());
    EXPECT_EQ(checkForValidPCIUpgrade(1, "pciimage.bin", "pciimage.bin", "pciimage.bin"), true);
}
TEST_F(CreateJsonTestFixture, TestName_checkForValidPCIUpgrade_Success1)
{
    //EXPECT_CALL(*g_DeviceStatusMock, eventManager(_, _)).Times(1).WillOnce(Return());
    EXPECT_EQ(checkForValidPCIUpgrade(1, "firmware.bin", "firmware.bin", "firmware.bin"), 10);
}
*/
TEST(TestCurrentRunningInst, TestName_NULLCheck) 
{
    EXPECT_EQ(CurrentRunningInst(NULL), false);
}
TEST(TestCurrentRunningInst, TestName_FilePresent) 
{
    int ret;
    ret = system("echo \"24\" > /tmp/runInst.txt");
    ret = system("echo \"rdkvfwupgrader 0 1\" > /tmp/cmdline.txt");
    EXPECT_EQ(CurrentRunningInst("/tmp/runInst.txt"), true);
}
TEST(TestCurrentRunningInst, TestName_FilePresentWrongData) 
{
    int ret;
    ret = system("echo \"xyz 0 1\" > /tmp/cmdline.txt");
    EXPECT_EQ(CurrentRunningInst("/tmp/runInst.txt"), false);
}
TEST(TestCurrentRunningInst, TestName_FileNotPresent) 
{
    int ret;
    ret = system("rm /tmp/runInst.txt");
    ret = system("rm /tmp/cmdline.txt");
    EXPECT_EQ(CurrentRunningInst("/tmp/runInst.txt"), false);
}
TEST(TestisDnsResolve, TestName_NULLCheck) 
{
    EXPECT_EQ(isDnsResolve(NULL), false);
}
TEST(TestisDnsResolve, TestName_FilePresent) 
{
    int ret;
    ret = system("echo \"nameserver:2345:34:56\" > /tmp/dnsResolv.txt");
    EXPECT_EQ(isDnsResolve("/tmp/dnsResolv.txt"), true);
}
TEST(TestisDnsResolve, TestName_FilePresentWithInfo) 
{
    int ret;
    ret = system("echo \"server:2345:34:56\" > /tmp/dnsResolv.txt");
    EXPECT_EQ(isDnsResolve("/tmp/dnsResolv.txt"), false);
}
TEST(TestisDnsResolve, TestName_FileNotPresent) 
{
    int ret;
    ret = system("rm /tmp/dnsResolv.txt");
    EXPECT_EQ(isDnsResolve("/tmp/dnsResolv.txt"), false);
}
TEST(TestlastDwnlImg, TestName_BigBuffer) 
{
    //bool lastDwnlImg(char *img_name, size_t img_name_size)
    EXPECT_EQ(lastDwnlImg(NULL, 128), false);
}

TEST(TestlastDwnlImg, TestName_Success) 
{
    char buf[16] = {0};
    int ret = 0;
    ret = system("echo \"TestLastImage.bin\" > /opt/cdl_flashed_file_name");
    EXPECT_EQ(lastDwnlImg(buf, sizeof(buf)), true);
    ret = system("rm -f /opt/cdl_flashed_file_name");
}
TEST(TestlastDwnlImg, TestName_Success1) 
{
    char buf[16] = {0};
    EXPECT_EQ(lastDwnlImg(buf, sizeof(buf)), true);
}
TEST(TestcurrentImg, TestName_BigBuffer) 
{
    EXPECT_EQ(currentImg(NULL, 128), false);
}
TEST(TestcurrentImg, TestName_Success) 
{
    char buf[16] = {0};
    int ret = 0;
    ret = system("echo \"TestImage.bin\" > /tmp/currently_running_image_name");
    EXPECT_EQ(currentImg(buf, sizeof(buf)), true);
    ret = system("rm -f /tmp/currently_running_image_name");
}
TEST(TestcurrentImg, TestName_Success1) 
{
    char buf[16] = {0};
    EXPECT_EQ(currentImg(buf, sizeof(buf)), true);
}
TEST(TestprevFlashedFile, TestName_BigBuffer) 
{
    EXPECT_EQ(prevFlashedFile(NULL, 128), false);
}
TEST(TestprevFlashedFile, TestName_Success)
{
    char buf[16] = {0};
    int ret = 0;
    ret = system("echo \"TestPrevImage.bin\" > /opt/previous_flashed_file_name");
    EXPECT_EQ(prevFlashedFile(buf, sizeof(buf)), true);
    ret = system("rm -f /opt/previous_flashed_file_name");
}
TEST(TestprevFlashedFile, TestName_Success1)
{
    char buf[16] = {0};
    EXPECT_EQ(prevFlashedFile(buf, sizeof(buf)), true);
}
TEST(TestcheckForValidPCIUpgrade, TestName_checkForValidPCIUpgrade_NullCheck)
{
    EXPECT_EQ(checkForValidPCIUpgrade(1, NULL, NULL, NULL), false);
}
TEST(TestcheckForValidPCIUpgrade, TestName_checkForValidPCIUpgrade_failpdri)
{
    //EXPECT_CALL(*g_DeviceStatusMock, eventManager(_, _)).Times(1).WillOnce(Return());
    EXPECT_EQ(checkForValidPCIUpgrade(1, "123", "123_PDRI_34", "123_PDRI_34"), false);
}
TEST(TestcheckForValidPCIUpgrade, TestName_checkForValidPCIUpgrade_Success)
{
    //EXPECT_CALL(*g_DeviceStatusMock, eventManager(_, _)).Times(1).WillOnce(Return());
    EXPECT_EQ(checkForValidPCIUpgrade(1, "pciimage.bin", "pciimage.bin", "pciimage.bin"), true);
}
TEST(TestcheckForValidPCIUpgrade, TestName_checkForValidPCIUpgrade_Success1)
{
    //EXPECT_CALL(*g_DeviceStatusMock, eventManager(_, _)).Times(1).WillOnce(Return());
    EXPECT_EQ(checkForValidPCIUpgrade(1, "TestImage.bin", "TestImage.bin", "TestImage.bin"), false);
}
TEST(TestupdateOPTOUTFile, TestName_NullCheck) 
{
    EXPECT_EQ(updateOPTOUTFile(NULL), false);
}
TEST(TestupdateOPTOUTFile, TestName_Success)
{
    int ret;
    ret = system("echo \"softwareoptout : BYPASS_OPTOUT\" > /tmp/maintenance_mgr_record.conf");
    EXPECT_EQ(updateOPTOUTFile("/tmp/maintenance_mgr_record.conf"), true);
    ret = system("rm /tmp/maintenance_mgr_record.conf");
}
TEST(TestupdateOPTOUTFile, TestName_Fail)
{
    int ret;
    ret = system("echo \"softwareoptout : ENFOURCE_OPTOUT\" > /tmp/maintenance_mgr_record.conf");
    EXPECT_EQ(updateOPTOUTFile("/tmp/maintenance_mgr_record.conf"), false);
    ret = system("rm /tmp/maintenance_mgr_record.conf");
}
TEST_F(CreateJsonTestFixture, TestName_checkCodebigAccess_Success)
{
    EXPECT_CALL(*g_DeviceStatusMock, v_secure_system(_)).Times(1).WillOnce(Return(0));
    EXPECT_EQ(checkCodebigAccess(), true);
}
TEST_F(CreateJsonTestFixture, TestName_checkCodebigAccess_Fail)
{
    EXPECT_CALL(*g_DeviceStatusMock, v_secure_system(_)).Times(1).WillOnce(Return(1));
    EXPECT_EQ(checkCodebigAccess(), false);
}
TEST_F(CreateJsonTestFixture, TestName_isStateRedSupported_Success)
{
    EXPECT_CALL(*g_DeviceStatusMock, filePresentCheck (_)).Times(1).WillOnce(Return(0));
    EXPECT_EQ(isStateRedSupported(), 1);
}
TEST_F(CreateJsonTestFixture, TestName_isStateRedSupported_Fail)
{
    EXPECT_CALL(*g_DeviceStatusMock, filePresentCheck (_)).Times(1).WillOnce(Return(1));
    EXPECT_EQ(isStateRedSupported(), 0);
}
TEST_F(CreateJsonTestFixture, TestName_isInStateRed_Sucess)
{
    EXPECT_CALL(*g_DeviceStatusMock, filePresentCheck (_)).Times(2).WillRepeatedly(Return(0));
    EXPECT_EQ(isInStateRed(), 1);
}
TEST_F(CreateJsonTestFixture, TestName_isInStateRed_Fail)
{
    EXPECT_CALL(*g_DeviceStatusMock, filePresentCheck (_)).Times(1).WillOnce(Return(1));
    EXPECT_EQ(isInStateRed(), 0);
}
TEST_F(CreateJsonTestFixture, TestName_isOCSPEnable_Sucess)
{
    EXPECT_CALL(*g_DeviceStatusMock, filePresentCheck (_)).Times(2).WillRepeatedly(Return(0));
    EXPECT_EQ(isOCSPEnable(), 1);
}
TEST_F(CreateJsonTestFixture, TestName_isOCSPEnable_Fail)
{
    EXPECT_CALL(*g_DeviceStatusMock, filePresentCheck (_)).Times(2).WillRepeatedly(Return(1));
    EXPECT_EQ(isOCSPEnable(), 0);
}
TEST_F(CreateJsonTestFixture, TestName_isUpgradeInProgress_Sucess)
{
    EXPECT_CALL(*g_DeviceStatusMock, filePresentCheck (_)).Times(1).WillRepeatedly(Return(0));
    EXPECT_EQ(isUpgradeInProgress(), true);
}
TEST_F(CreateJsonTestFixture, TestName_isUpgradeInProgress_Fail)
{
    EXPECT_CALL(*g_DeviceStatusMock, filePresentCheck (_)).Times(3).WillRepeatedly(Return(1));
    EXPECT_EQ(isUpgradeInProgress(), false);
}
TEST_F(CreateJsonTestFixture, TestName_unsetStateRed)
{
    EXPECT_CALL(*g_DeviceStatusMock, filePresentCheck (_)).Times(1).WillOnce(Return(1));
    unsetStateRed();
    EXPECT_EQ(0, 0);
}
//TODO: Below is void function. Need to change function return
TEST_F(CreateJsonTestFixture, TestName_checkAndEnterStateRed_Check)
{
    EXPECT_CALL(*g_DeviceStatusMock, filePresentCheck (_)).WillRepeatedly(Return(0));
    checkAndEnterStateRed(50, "true");
    EXPECT_EQ(0, 0);
}
TEST_F(CreateJsonTestFixture, TestName_checkAndEnterStateRed_instatered)
{
    int ret = 0;
    ret = system("touch /tmp/stateRedEnabled");
    ret = system("touch /tmp/stateSupport");
    EXPECT_CALL(*g_DeviceStatusMock, filePresentCheck (_)).WillRepeatedly(Return(0));
    checkAndEnterStateRed(50, "true");
    ret = system("rm /tmp/stateRedEnabled");
    ret = system("rm /tmp/stateSupport");
    EXPECT_EQ(0, 0);
}
TEST_F(CreateJsonTestFixture, TestName_checkAndEnterStateRed_notinstatered)
{
    int ret = 0;
    ret = system("touch /tmp/stateSupport");
    ret = system("touch /opt/red_state_reboot");
    EXPECT_CALL(*g_DeviceStatusMock, filePresentCheck (_)).WillRepeatedly(Return(0));
    checkAndEnterStateRed(50, "true");
    ret = system("rm /tmp/stateSupport");
    ret = system("rm /opt/red_state_reboot");
    EXPECT_EQ(0, 0);
}
TEST(TestcheckVideoStatus, TestName_NullCheck) 
{
    EXPECT_EQ(checkVideoStatus(NULL), -1);
}
TEST(TestisThrottleEnabled, TestName_NullCheck) 
{
    EXPECT_EQ(isThrottleEnabled(NULL, NULL, 1), -1);
}
TEST(TestisThrottleEnabled, TestName_success) 
{
    snprintf(rfc_list.rfc_throttle, sizeof(rfc_list.rfc_throttle), "%s", "true");
    EXPECT_EQ(isThrottleEnabled("PLATCO", "false", 1), -1);
}
TEST(TestgetFileLastModifyTime, TestName_NullCheck)
{
    EXPECT_EQ(getFileLastModifyTime(NULL), 0);
}
TEST(TestisDwnlBlock, TestName_direct_not_block)
{
    EXPECT_EQ(isDwnlBlock(HTTP_XCONF_DIRECT), 0);
}
TEST(TestisDwnlBlock, TestName_direct_block)
{
    int ret;
    ret = system("touch /tmp/.lastdirectfail_cdl");
    EXPECT_EQ(isDwnlBlock(HTTP_XCONF_DIRECT), 1);
    ret = system("rm /tmp/.lastdirectfail_cdl");
}
TEST(TestisDwnlBlock, TestName_codebig_not_block)
{
    EXPECT_EQ(isDwnlBlock(HTTP_XCONF_CODEBIG), 0);
}
TEST(TestisDwnlBlock, TestName_codebig_block)
{
    int ret;
    ret = system("touch /tmp/.lastcodebigfail_cdl");
    EXPECT_EQ(isDwnlBlock(HTTP_XCONF_CODEBIG), 1);
    ret = system("rm /tmp/.lastcodebigfail_cdl");
}
TEST(TestDwnlStatus, TestName_updateFWDownloadStatusNull)
{
    EXPECT_EQ(updateFWDownloadStatus(NULL, NULL), -1);
}
TEST(TestDwnlStatus, TestName_updateFWDownloadStatus)
{
    struct FWDownloadStatus fwdls;
    memset(&fwdls, '\0', sizeof(fwdls));
    EXPECT_EQ(updateFWDownloadStatus(&fwdls, "false"), 1);
}
TEST(TestDwnlStatus, TestName_updateFWDownloadStatustrue)
{
    struct FWDownloadStatus fwdls;
    memset(&fwdls, '\0', sizeof(fwdls));
    EXPECT_EQ(updateFWDownloadStatus(&fwdls, "true"), 1);
}
TEST(TestDwnlStatus, TestName_notifyDwnlStatusNull)
{
    EXPECT_EQ(notifyDwnlStatus(NULL, NULL, 0), -1);
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
    cout << "Starting device_status_helper App ===================>" << endl;
    return RUN_ALL_TESTS();
}
