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

#include "../mocks/deviceutils_mock.h"
extern "C" {
#include "device_api.h"
}

#define JSON_STR_LEN        1000

using namespace testing;
using namespace std;
using ::testing::Return;
using ::testing::StrEq;


//DeviceUtilsMock *g_DeviceApiMock = NULL;

extern DeviceUtilsMock *g_DeviceUtilsMock;

class DeviceApiTestFixture : public ::testing::Test {
    protected:
        DeviceUtilsMock mockDeviceUtils;

        DeviceApiTestFixture()
        {
            g_DeviceUtilsMock = &mockDeviceUtils;
        }
        virtual ~DeviceApiTestFixture()
        {
            g_DeviceUtilsMock = NULL;
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

TEST(TestGetServerUrlFile, TestName_NULLCheck)
{
    EXPECT_EQ(GetServerUrlFile(NULL, 0, NULL), 0);
}

TEST(TestGetServerUrlFile, TestName_goodurl)
{
    int ret;
    char serverurl[128];
    ret = system("echo \"https://mockserver.com\" > /tmp/swupdate.conf");
    EXPECT_NE(GetServerUrlFile(serverurl, sizeof(serverurl), "/tmp/swupdate.conf"), 0);
    printf("GTEST serverurl=%s\n", serverurl);
    ret = system("rm -f /tmp/swupdate.conf");
}

TEST(TestGetServerUrlFile, TestName_goodurl1)
{
    int ret;
    char serverurl[128];
    ret = system("echo \"https://mock-ser_ver.com\" > /tmp/swupdate.conf");
    EXPECT_NE(GetServerUrlFile(serverurl, sizeof(serverurl), "/tmp/swupdate.conf"), 0);
    printf("GTEST serverurl=%s\n", serverurl);
    ret = system("rm -f /tmp/swupdate.conf");
}

TEST(TestGetServerUrlFile, TestName_filenotpresent)
{
    char serverurl[128];
    EXPECT_EQ(GetServerUrlFile(serverurl, sizeof(serverurl), "/tmp/swupdate1.conf"), 0);
}

TEST_F(DeviceApiTestFixture,TestName_Nullcheck)
{
    EXPECT_EQ(GetTimezone(NULL, NULL, 0), 0);
}

TEST_F(DeviceApiTestFixture,TestName_getdevicepropfail)
{
    int ret;
    char output[8];
    EXPECT_CALL(*g_DeviceUtilsMock, getDevicePropertyData(_, _, _)).Times(1).WillOnce(Return(-1));
    //ret = system("echo \"India\" > /tmp/timeZoneDST");
    EXPECT_EQ(GetTimezone(output, "x86", sizeof(output)), 0);
}
TEST_F(DeviceApiTestFixture,TestName_gettime)
{
    int ret;
    char output[8];
    EXPECT_CALL(*g_DeviceUtilsMock, getDevicePropertyData(_, _, _)).Times(1).WillOnce(Return(0));
    ret = system("echo \"India\" > /tmp/timeZoneDST");
    EXPECT_NE(GetTimezone(output, "x86", sizeof(output)), 0);
    ret = system("rm -f  /tmp/timeZoneDST");
}
//TODO: Need to get json file from device and write some more test case
TEST_F(DeviceApiTestFixture,TestName_gettimesuccess)
{
    int ret;
    char output[8];
    EXPECT_CALL(*g_DeviceUtilsMock, getDevicePropertyData(_, _, _)).Times(1).WillOnce(Return(0));
    ret = system("echo \"IndiaDelhi\" > /tmp/timeZoneDST");
    ret = system("echo \"Delhi\" > /tmp/timeZone_offset_map");
    EXPECT_NE(GetTimezone(output, "x86", sizeof(output)), 0);
    ret = system("rm -f  /tmp/timeZone_offset_map");
    ret = system("rm -f  /tmp/timeZoneDST");
}
TEST_F(DeviceApiTestFixture,TestName_gettimeskydevice)
{
   char expectedData[] = "SKY";
   char output[8];
   EXPECT_CALL(*g_DeviceUtilsMock, getDevicePropertyData(_, _, _))
                .Times(1)
                .WillOnce(Invoke([&expectedData](const char *model, char *data, int size) {
                strncpy(data, expectedData, size - 1);
                data[size - 1] = '\0';  // Ensure null-termination
                return 0;
        }));
    EXPECT_NE(GetTimezone(output, "x86", sizeof(output)), 0);
 
}
TEST_F(DeviceApiTestFixture,TestName_gettimeskydevicearm)
{
   char expectedData[] = "SKY";
   char output[8];
   EXPECT_CALL(*g_DeviceUtilsMock, getDevicePropertyData(_, _, _))
                .Times(1)
                .WillOnce(Invoke([&expectedData](const char *model, char *data, int size) {
                strncpy(data, expectedData, size - 1);
                data[size - 1] = '\0';  // Ensure null-termination
                return 0;
        }));
    EXPECT_NE(GetTimezone(output, "arm", sizeof(output)), 0);
}
TEST_F(DeviceApiTestFixture,TestName_getadditionfw_nullcheck)
{
    EXPECT_EQ(GetAdditionalFwVerInfo(NULL, 0), 0);
}
//TODO: Need to check why v_secure_popen is not returning properly
TEST_F(DeviceApiTestFixture,TestName_Success)
{
    char data[64];
    FILE *fp = NULL;
    char buff[] = "1234_pdri_image.bin\n";
    fp = fopen("/tmp/pdri.txt", "w");
    if (fp != NULL) {
        fwrite(buff, sizeof(buff), 1,fp);
	fclose(fp);
	fp = NULL;
    }
    //EXPECT_CALL(*g_DeviceUtilsMock, v_secure_popen(_, _, _)).Times(1).WillOnce(Return(fopen("/tmp/pdri.txt", "r")));
    EXPECT_CALL(*g_DeviceUtilsMock, v_secure_popen(_, _, _)).Times(1).WillOnce(Return(fp));
    EXPECT_EQ(GetAdditionalFwVerInfo(data, sizeof(data)), 0);
}
TEST(TestGetPDRIFileName, Test_pdri_Nullcheck)
{
    EXPECT_EQ(GetPDRIFileName(NULL, 0), 0);
}
TEST_F(DeviceApiTestFixture,TestName_bundle_Nullcheck)
{
    EXPECT_EQ(GetInstalledBundles(NULL, 0), 0);
}
TEST_F(DeviceApiTestFixture,TestName_bundle_Success)
{
    int ret;
    char pBundles[32] = {0};
    ret = system("mkdir /tmp/certs;cp ca-store-update-bundle_package.json /tmp/certs/ ");
    EXPECT_NE(GetInstalledBundles(pBundles, sizeof(pBundles)), 0);
    ret = system("rm -rf /tmp/certs/ ");
    printf("BUNDLE = %s\n",pBundles);
}
TEST_F(DeviceApiTestFixture,TestName_bundle_rfcpath)
{
    int ret;
    char pBundles[32] = {0};
    ret = system("mkdir /tmp/rfc;mkdir /tmp/rfc/certs; cp ca-store-update-bundle_package.json /tmp/rfc/certs/ ");
    EXPECT_NE(GetInstalledBundles(pBundles, sizeof(pBundles)), 0);
    ret = system("rm -rf /tmp/rfc/certs/ ");
    ret = system("rm -rf /tmp/rfc/ ");
    printf("BUNDLE = %s\n",pBundles);
}
TEST_F(DeviceApiTestFixture,TestName_bundle_Fail)
{
    char pBundles[32] = {0};
    EXPECT_EQ(GetInstalledBundles(pBundles, sizeof(pBundles)), 0);
}

TEST(TestGetUTCTime, TestName_Nullcheck)
{
    EXPECT_EQ(GetUTCTime(NULL, 0), 0);
}
TEST(TestGetUTCTime, TestName_Success)
{
    char utc_time[6];
    EXPECT_EQ(GetUTCTime(utc_time, sizeof(utc_time)), 0);
    printf("UTC time = %s\n", utc_time);
}
TEST(TestGetCapabilities, TestName_Nullcheck)
{
    EXPECT_EQ(GetCapabilities(NULL, 0), 0);
}
TEST(TestGetCapabilities, TestName_Success)
{
    char capability[6];
    EXPECT_NE(GetCapabilities(capability, sizeof(capability)), 0);
    printf("capabilities = %s\n", capability);
}

TEST_F(DeviceApiTestFixture,TestName_GetPartnerId_Nullcheck)
{
    EXPECT_EQ(GetPartnerId(NULL, 0), 0);
}
TEST_F(DeviceApiTestFixture,TestName_GetPartnerId_Success)
{
   char expectedData[] = "true";
   char output[8];
   int ret;
   EXPECT_CALL(*g_DeviceUtilsMock, getDevicePropertyData(_, _, _))
                .Times(1)
                .WillOnce(Invoke([&expectedData](const char *model, char *data, int size) {
                strncpy(data, expectedData, size - 1);
                data[size - 1] = '\0';  // Ensure null-termination
                return 0;
        }));
    ret = system("echo \"X_RDKCENTRAL-COM_RFC.Bootstrap.PartnerName=comcast\" > /tmp/bootstrap.ini");
    EXPECT_NE(GetPartnerId(output, sizeof(output)), 0);
    printf("partner ID = %s\n", output);
    ret = system("rm -f /tmp/bootstrap.in");
}
TEST_F(DeviceApiTestFixture,TestName_GetPartnerId_notfound)
{
   char expectedData[] = "false";
   char output[8];
   int ret;
   EXPECT_CALL(*g_DeviceUtilsMock, getDevicePropertyData(_, _, _))
                .Times(1)
                .WillOnce(Invoke([&expectedData](const char *model, char *data, int size) {
                strncpy(data, expectedData, size - 1);
                data[size - 1] = '\0';  // Ensure null-termination
                return 0;
        }));
    ret = system("echo \"X_RDKCENTRAL-COM_RFC.Bootstrap.PartnerName=comcast-sky\" > /tmp/bootstrap.ini");
    EXPECT_EQ(GetPartnerId(output, sizeof(output)), 0);
    printf("partner ID = %s\n", output);
    ret = system("rm -f /tmp/bootstrap.in");
}
TEST_F(DeviceApiTestFixture,TestName_GetPartnerId_SuccessThird)
{
   char expectedData[] = "false";
   char output[8];
   int ret;
   EXPECT_CALL(*g_DeviceUtilsMock, getDevicePropertyData(_, _, _))
                .Times(1)
                .WillOnce(Invoke([&expectedData](const char *model, char *data, int size) {
                strncpy(data, expectedData, size - 1);
                data[size - 1] = '\0';  // Ensure null-termination
                return 0;
        }));
    ret = system("echo \"X_RDKCENTRAL-COM_Syndication.PartnerId=xglobal\" > /tmp/bootstrap.ini");
    EXPECT_NE(GetPartnerId(output, sizeof(output)), 0);
    printf("partner ID = %s\n", output);
    ret = system("rm -f /tmp/bootstrap.in");
}
TEST_F(DeviceApiTestFixture,TestName_GetPartnerId_SuccessFourth)
{
   char expectedData[] = "false";
   char output[16];
   int ret;
   EXPECT_CALL(*g_DeviceUtilsMock, getDevicePropertyData(_, _, _))
                .Times(1)
                .WillOnce(Invoke([&expectedData](const char *model, char *data, int size) {
                strncpy(data, expectedData, size - 1);
                data[size - 1] = '\0';  // Ensure null-termination
                return 0;
        }));
    ret = system("echo \"comcast-xglobal\" > /tmp/partnerId3.dat");
    EXPECT_NE(GetPartnerId(output, sizeof(output)), 0);
    printf("partner ID = %s\n", output);
    ret = system("rm -f /tmp/partnerId3.dat");
}
TEST_F(DeviceApiTestFixture,TestName_GetPartnerId_defaultvalue)
{
   char expectedData[] = "false";
   char output[16];
   int ret;
   EXPECT_CALL(*g_DeviceUtilsMock, getDevicePropertyData(_, _, _))
                .Times(1)
                .WillOnce(Invoke([&expectedData](const char *model, char *data, int size) {
                strncpy(data, expectedData, size - 1);
                data[size - 1] = '\0';  // Ensure null-termination
                return 0;
        }));
    EXPECT_NE(GetPartnerId(output, sizeof(output)), 0);
}
TEST_F(DeviceApiTestFixture,TestName_GetOsClass_Nullcheck)
{
    EXPECT_EQ(GetOsClass(NULL, 0), 0);
}
TEST_F(DeviceApiTestFixture,TestName_GetOsClass_Fail)
{
   char expectedData[] = "false";
   char output[16];
   int ret;
   EXPECT_CALL(*g_DeviceUtilsMock, getDevicePropertyData(_, _, _))
                .Times(1)
                .WillOnce(Invoke([&expectedData](const char *model, char *data, int size) {
                strncpy(data, expectedData, size - 1);
                data[size - 1] = '\0';  // Ensure null-termination
                return 0;
        }));
   //EXPECT_CALL(*g_DeviceUtilsMock, read_RFCProperty(_, _, _, _)).Times(1).WillOnce(Return(1));
    EXPECT_NE(GetOsClass(output, sizeof(output)), 0);
    printf("GetOsClass = %s\n", output);
}

TEST_F(DeviceApiTestFixture,TestName_GetOsClass_Success)
{
   char expectedData[] = "true";
   char output[16];
   int ret;
   EXPECT_CALL(*g_DeviceUtilsMock, getDevicePropertyData(_, _, _))
                .Times(1)
                .WillOnce(Invoke([&expectedData](const char *model, char *data, int size) {
                strncpy(data, expectedData, size - 1);
                data[size - 1] = '\0';  // Ensure null-termination
                return 0;
        }));
   EXPECT_CALL(*g_DeviceUtilsMock, read_RFCProperty(_, _, _, _)).Times(1).WillOnce(Return(1));
    EXPECT_NE(GetOsClass(output, sizeof(output)), 0);
    printf("GetOsClass = %s\n", output);
}
TEST_F(DeviceApiTestFixture,TestName_GetSerialNum_Nullcheck)
{
    EXPECT_EQ(GetSerialNum(NULL, 0), 0);
}
TEST_F(DeviceApiTestFixture,TestName_GetSerialNum_Success)
{
   char output[16];
   EXPECT_CALL(*g_DeviceUtilsMock, read_RFCProperty(_, _, _, _)).Times(1).WillOnce(Return(1));
   EXPECT_NE(GetSerialNum(output, sizeof(output)), 0);
   printf("GetSerialNumber = %s\n", output);
}
TEST_F(DeviceApiTestFixture,TestName_GetAccountID_Nullcheck)
{
    EXPECT_EQ(GetAccountID(NULL, 0), 0);
}
TEST_F(DeviceApiTestFixture,TestName_GetAccountID_Success)
{
   char output[16];
   EXPECT_CALL(*g_DeviceUtilsMock, read_RFCProperty(_, _, _, _)).Times(1).WillOnce(Return(1));
   EXPECT_NE(GetAccountID(output, sizeof(output)), 0);
   printf("GetAccountID = %s\n", output);
}
TEST_F(DeviceApiTestFixture,TestName_GetAccountID_Fail)
{
   char output[16];
   EXPECT_CALL(*g_DeviceUtilsMock, read_RFCProperty(_, _, _, _)).Times(1).WillOnce(Return(-1));
   EXPECT_NE(GetAccountID(output, sizeof(output)), 0);
   printf("GetAccountID = %s\n", output);
}
TEST(TestGetFirmwareVersion, TestName_GetFirmwareVersion_Nullcheck)
{
    EXPECT_EQ(GetFirmwareVersion(NULL, 0), 0);
}
TEST(TestGetFirmwareVersion, TestName_GetFirmwareVersion_Success)
{
    int ret;
    char output[6];
    ret = system("echo \"imagename:12345.bin\" > /tmp/version_test.txt");
    EXPECT_NE(GetFirmwareVersion(output, sizeof(output)), 0);
    ret = system("rm -f /tmp/version_test.txt");
    printf("GetFirmwareVersion = %s\n", output);
}
TEST(TestGetFirmwareVersion, TestName_GetFirmwareVersion_Fail)
{
    int ret;
    char output[6] = {0};
    ret = system("echo \"imagenamenot:12345.bin\" > /tmp/version_test.txt");
    EXPECT_EQ(GetFirmwareVersion(output, sizeof(output)), 0);
    ret = system("rm -f /tmp/version_test.txt");
    printf("GetFirmwareVersion = %s\n", output);
}
TEST_F(DeviceApiTestFixture,TestName_GetModelNum_Nullcheck)
{
    EXPECT_EQ(GetModelNum(NULL, 0), 0);
}
TEST_F(DeviceApiTestFixture,TestName_GetMFRName_Nullcheck)
{
    EXPECT_EQ(GetMFRName(NULL, 0), 0);
}
TEST_F(DeviceApiTestFixture, GetMFRName_file_found)
{
    int ret;
    char data[32];
    ret = system("echo \"03272025\" > /tmp/.manufacturer");
    EXPECT_NE(GetMFRName(data, 7),0);
    ret = system("rm -f /tmp/.manufacturer");
}
TEST_F(DeviceApiTestFixture, GetMFRName_file_not_found)
{
    int ret;
    char data[32];
    EXPECT_EQ(GetMFRName(data, 7),0);
}
TEST_F(DeviceApiTestFixture, TestName_GetEstbMac_Nullcheck)
{
    EXPECT_EQ(GetEstbMac(NULL, 0), 0);
}
TEST_F(DeviceApiTestFixture, TestName_GetEstbMac_Success)
{
    int ret;
    char output[32];
    ret = system("echo \"aa:bb:cc:dd:ff:gg\" > /tmp/.estb_mac_gtest.txt");
    EXPECT_NE(GetEstbMac(output, sizeof(output)), 0);
    ret = system("rm -f /tmp/.estb_mac_gtest.txt");
}
TEST_F(DeviceApiTestFixture, TestName_GetEstbMac_Fail)
{
    int ret;
    char output[8];
    EXPECT_CALL(*g_DeviceUtilsMock, getDevicePropertyData(_, _, _)).Times(1).WillOnce(Return(-1));
    EXPECT_EQ(GetEstbMac(output, sizeof(output)), 0);
}

TEST(TestGetRdmManifestVersion, TestName_GetRdmManifestVersion_Nullcheck)
{
    EXPECT_EQ(GetRdmManifestVersion(NULL, 0), 0);
}

TEST_F(DeviceApiTestFixture, TestName_GetFileContents_Nullcheck)
{
    EXPECT_EQ(GetFileContents(NULL, NULL), 0);
}
TEST_F(DeviceApiTestFixture, TestName_GetFileContents_Success)
{
    char *data = NULL;
    int ret;
    EXPECT_CALL(*g_DeviceUtilsMock, getFileSize(_)).Times(1).WillOnce(Return(10));
    ret = system("echo \"Comcast India\" > /tmp/test.txt");
    EXPECT_NE(GetFileContents(&data, "/tmp/test.txt"), 0);
    ret = system("rm -f /tmp/test.txt");
    if (data != NULL) {
        printf("data is = %s\n", data);
    }
}
TEST_F(DeviceApiTestFixture, TestName_GetFileContents_Fail)
{
    EXPECT_EQ(GetFileContents(NULL, NULL), 0);
}
TEST_F(DeviceApiTestFixture, TestName_GetServURL_Nullcheck)
{
    EXPECT_EQ(GetServURL(NULL, 0), 0);
}
TEST_F(DeviceApiTestFixture, TestName_GetServURL_SuccessStatered_DebugServices_Enabled)
{
    char output[32];
    int ret;
    char servUrl[]="https://www.statered.com";
    EXPECT_CALL(*g_DeviceUtilsMock, isInStateRed()).Times(1).WillOnce(Return(true));
    //EXPECT_CALL(*g_DeviceUtilsMock, read_RFCProperty(_, _, _, _)).Times(1).WillOnce(Return(1));
    ret = system("echo \"BUILD_TYPE=vbn\" > /tmp/device_gtest.prop");
    //EXPECT_CALL(*g_DeviceUtilsMock, filePresentCheck(_)).Times(1).WillOnce(Return(1));
    EXPECT_CALL(*g_DeviceUtilsMock, isDebugServicesEnabled()).Times(1).WillOnce(Return(true));
    ret = system("echo \"https://www.statered.com\" > /tmp/stateredrecovry.conf");
    ret = GetServURL(output, sizeof(output));
    EXPECT_EQ(strncmp(output,servUrl,strlen(servUrl)),0);
    printf("output ====================================== %s \n",output);
    printf("servUrl ===================================== %s \n" ,servUrl);
    ret = system("rm -f /tmp/stateredrecovry.conf");
    ret = system("rm -f /tmp/device_gtest.prop");
    printf("Server URL = %s\n", output);
}
TEST_F(DeviceApiTestFixture, TestName_GetServURL_SuccessStatered_DebugServices_Disabled)
{
    char output[32];
    int ret;
    char servUrl[]="https://www.statered.com";
    EXPECT_CALL(*g_DeviceUtilsMock, isInStateRed()).Times(1).WillOnce(Return(true));
    //EXPECT_CALL(*g_DeviceUtilsMock, read_RFCProperty(_, _, _, _)).Times(1).WillOnce(Return(1));
    ret = system("echo \"BUILD_TYPE=vbn\" > /tmp/device_gtest.prop");
    //EXPECT_CALL(*g_DeviceUtilsMock, filePresentCheck(_)).Times(1).WillOnce(Return(1));
    EXPECT_CALL(*g_DeviceUtilsMock, isDebugServicesEnabled()).Times(1).WillOnce(Return(false));
    ret = system("echo \"https://www.statered.com\" > /tmp/stateredrecovry.conf");
    ret = GetServURL(output, sizeof(output));
    EXPECT_EQ(strncmp(output,servUrl,strlen(servUrl)),0);
    printf("output ====================================== %s \n",output);
    printf("servUrl ===================================== %s \n" ,servUrl);
    ret = system("rm -f /tmp/stateredrecovry.conf");
    ret = system("rm -f /tmp/device_gtest.prop");
    printf("Server URL = %s\n", output);
}
TEST_F(DeviceApiTestFixture, TestName_GetServURL_SuccessStatered_Prod_DebugServices_Enabled)
{
    char output[32];
    int ret;
    char servUrl[]="https://www.statered.com";
    EXPECT_CALL(*g_DeviceUtilsMock, isInStateRed()).Times(1).WillOnce(Return(true));
    //EXPECT_CALL(*g_DeviceUtilsMock, read_RFCProperty(_, _, _, _)).Times(1).WillOnce(Return(1));
    ret = system("echo \"BUILD_TYPE=PROD\" > /tmp/device_gtest.prop");
    //EXPECT_CALL(*g_DeviceUtilsMock, filePresentCheck(_)).Times(1).WillOnce(Return(1));
    EXPECT_CALL(*g_DeviceUtilsMock, isDebugServicesEnabled()).Times(1).WillOnce(Return(true));
    ret = system("echo \"https://www.statered.com\" > /tmp/stateredrecovry.conf");
    ret = GetServURL(output, sizeof(output));
    EXPECT_EQ(strncmp(output,servUrl,strlen(servUrl)),0);
    printf("output ====================================== %s\n ",output);
    printf("servUrl ===================================== %s \n" ,servUrl);
    ret = system("rm -f /tmp/stateredrecovry.conf");
    ret = system("rm -f /tmp/device_gtest.prop");
    printf("Server URL = %s\n", output);
}
TEST_F(DeviceApiTestFixture, TestName_GetServURL_SuccessStatered_Prod_DebugServices_Disabled)
{
    char output[64];
    int ret;
    char servUrl[]="https://www.tr181Rfc.com";
    EXPECT_CALL(*g_DeviceUtilsMock, isInStateRed()).Times(1).WillOnce(Return(true));
    //EXPECT_CALL(*g_DeviceUtilsMock, read_RFCProperty(_, _, _, _)).Times(1).WillOnce(Return(1));
    ret = system("echo \"BUILD_TYPE=PROD\" > /tmp/device_gtest.prop");
    //EXPECT_CALL(*g_DeviceUtilsMock, filePresentCheck(_)).Times(1).WillOnce(Return(1));
    EXPECT_CALL(*g_DeviceUtilsMock, isDebugServicesEnabled()).Times(1).WillOnce(Return(false));
    ret = system("echo \"https://www.statered.com\" > /tmp/stateredrecovry.conf");
    ret = system("echo \"https://www.autotool.com\" > /tmp/swupdate.conf");
    EXPECT_CALL(*g_DeviceUtilsMock, read_RFCProperty(_, _, _, _))
                .Times(1)
                .WillOnce(Invoke([&servUrl]( char* type, const char* key, char *out_value, size_t datasize ) {
                strncpy(out_value, servUrl , datasize-1);
                out_value[datasize - 1] = '\0';
                return strlen(out_value);
                }));

    ret=GetServURL(output , sizeof(output));
    EXPECT_EQ(strncmp(output , servUrl , strlen(servUrl)),0);
    printf("Output ========================= %s\n ", output);
    printf("servUrl ======================== %s \n ",servUrl);
    ret = system("rm -f /tmp/stateredrecovry.conf");
    ret = system("rm -f /tmp/device_gtest.prop");
    printf("Server URL = %s\n", output);
}
TEST_F(DeviceApiTestFixture, TestName_GetServURL_SuccessSwupdate_DebugServices_Enabled)
{
    char output[32];
    int ret;
    char servUrl[]="https://www.rdkautotool.com";
    EXPECT_CALL(*g_DeviceUtilsMock, isInStateRed()).Times(1).WillOnce(Return(false));
    EXPECT_CALL(*g_DeviceUtilsMock, filePresentCheck(_)).Times(1).WillOnce(Return(0));
    //EXPECT_CALL(*g_DeviceUtilsMock, read_RFCProperty(_, _, _, _)).Times(1).WillOnce(Return(1));
    ret = system("echo \"BUILD_TYPE=vbn\" > /tmp/device_gtest.prop");
    EXPECT_CALL(*g_DeviceUtilsMock, isDebugServicesEnabled()).Times(1).WillOnce(Return(true));
    ret = system("echo \"https://www.rdkautotool.com\" > /tmp/swupdate.conf");
    ret=GetServURL(output , sizeof(output));
    EXPECT_EQ(strncmp(output , servUrl , strlen(servUrl)),0);
    printf("Output ========================= %s\n ", output);
    printf("servUrl ======================== %s \n ",servUrl);
    ret = system("rm -f /tmp/swupdate.conf");
    ret = system("rm -f /tmp/device_gtest.prop");
    printf("Server URL = %s\n", output);
}

TEST_F(DeviceApiTestFixture, TestName_GetServURL_SuccessSwupdate_DebugServices_Disabled)
{
    char output[32];
    int ret;
    char servUrl[]="https://www.rdkautotool.com";
    EXPECT_CALL(*g_DeviceUtilsMock, isInStateRed()).Times(1).WillOnce(Return(false));
    EXPECT_CALL(*g_DeviceUtilsMock, filePresentCheck(_)).Times(1).WillOnce(Return(0));
    //EXPECT_CALL(*g_DeviceUtilsMock, read_RFCProperty(_, _, _, _)).Times(1).WillOnce(Return(1));
    ret = system("echo \"BUILD_TYPE=vbn\" > /tmp/device_gtest.prop");
    EXPECT_CALL(*g_DeviceUtilsMock, isDebugServicesEnabled()).Times(1).WillOnce(Return(false));
    ret = system("echo \"https://www.rdkautotool.com\" > /tmp/swupdate.conf");
    ret=GetServURL(output , sizeof(output));
    EXPECT_EQ(strncmp(output , servUrl , strlen(servUrl)),0);
    printf("Output ========================= %s\n ", output);
    printf("servUrl ======================== %s \n ",servUrl);
    ret = system("rm -f /tmp/swupdate.conf");
    ret = system("rm -f /tmp/device_gtest.prop");
    printf("Server URL = %s\n", output);
}

TEST_F(DeviceApiTestFixture, TestName_GetServURL_SuccessSwupdate_Prod_DebugServices_Enabled)
{
    char output[32];
    int ret;
    char servUrl[]="https://www.rdkautotool.com";
    EXPECT_CALL(*g_DeviceUtilsMock, isInStateRed()).Times(1).WillOnce(Return(false));
    EXPECT_CALL(*g_DeviceUtilsMock, filePresentCheck(_)).Times(1).WillOnce(Return(0));
    ret = system("echo \"BUILD_TYPE=PROD\" > /tmp/device_gtest.prop");
    EXPECT_CALL(*g_DeviceUtilsMock, isDebugServicesEnabled()).Times(1).WillOnce(Return(true));
    ret = system("echo \"https://www.rdkautotool.com\" > /tmp/swupdate.conf");
    ret=GetServURL(output , sizeof(output));
    EXPECT_EQ(strncmp(output , servUrl , strlen(servUrl)),0);
    printf("Output ========================= %s\n ", output);
    printf("servUrl ======================== %s \n ",servUrl);
    ret = system("rm -f /tmp/swupdate.conf");
    ret = system("rm -f /tmp/device_gtest.prop");
    printf("Server URL = %s\n", output);
}

TEST_F(DeviceApiTestFixture, TestName_GetServURL_SuccessSwupdate_Prod_DebugServices_Disabled)
{
    char output[64];
    int ret;
    char servUrl[]= "https://www.tr181Rfc.com";
    EXPECT_CALL(*g_DeviceUtilsMock, isInStateRed()).Times(1).WillOnce(Return(false));
    ret = system("echo \"BUILD_TYPE=PROD\" > /tmp/device_gtest.prop");
    EXPECT_CALL(*g_DeviceUtilsMock, isDebugServicesEnabled()).Times(1).WillOnce(Return(false));
    ret = system("echo \"https://www.rdkautotool.com\" > /tmp/swupdate.conf");
    EXPECT_CALL(*g_DeviceUtilsMock, read_RFCProperty(_, _, _, _))
	        .Times(1)
		.WillOnce(Invoke([&servUrl]( char* type, const char* key, char *out_value, size_t datasize ) {
		strncpy(out_value, servUrl , datasize-1);
                out_value[datasize - 1] = '\0';
		return strlen(out_value);
		}));

    ret=GetServURL(output , sizeof(output));
    EXPECT_EQ(strncmp(output , "https://www.tr181Rfc.com/xconf/swu/stb", strlen("https://www.tr181Rfc.com/xconf/swu/stb")),0);
    printf("Output ========================= %s\n ", output);
    printf("servUrl ======================== %s \n ",servUrl);
    ret = system("rm -f /tmp/swupdate.conf");
    ret = system("rm -f /tmp/device_gtest.prop");
    printf("Server URL = %s\n", output);
}
TEST_F(DeviceApiTestFixture, TestName_GetBuildType_Success)
{
    int ret;
    char output[8];
    BUILDTYPE eBuildType;
    ret = system("echo \"BUILD_TYPE=vbn\" > /tmp/device_gtest.prop");
    EXPECT_NE(GetBuildType(output, sizeof(output), &eBuildType), 0);
    ret = system("rm -f /tmp/device_gtest.prop");
    printf("Build Type = %s\n", output);
}
//TODO: Need to write more test case
TEST_F(DeviceApiTestFixture, TestName_GetExperience_Nullcheck)
{
    EXPECT_EQ(GetExperience(NULL, 0), 0);
}

TEST_F(DeviceApiTestFixture,TestName_GetRemoteInfo_Nullcheck)
{
    EXPECT_EQ(GetRemoteInfo(NULL, 0), 0);
}

TEST_F(DeviceApiTestFixture,TestName_GetRemoteInfo_Success)
{
    int ret;
    char pRemoteInfo[256] = {0};
    ret = system("cp rc-proxy-params.json /tmp/ ");
    EXPECT_NE(GetRemoteInfo(pRemoteInfo, sizeof(pRemoteInfo)), 0);
    ret = system("rm -rf /tmp/rc-proxy-params.json ");
    printf("RemoteInfo = %s\n",pRemoteInfo);
}
TEST_F(DeviceApiTestFixture,TestName_GetRemoteInfo_Fail)
{
    char pRemoteInfo[256] = {0};
    EXPECT_EQ(GetRemoteInfo(pRemoteInfo, sizeof(pRemoteInfo)), 0);
}
TEST_F(DeviceApiTestFixture,TestName_GetRemoteVers_Nullcheck)
{
    EXPECT_EQ(GetRemoteVers(NULL, 0), 0);
}

TEST_F(DeviceApiTestFixture,TestName_GetRemoteVers_Success)
{
    int ret;
    char pRemoteInfo[256] = {0};
    ret = system("cp rc-proxy-params.json /tmp/ ");
    EXPECT_NE(GetRemoteVers(pRemoteInfo, sizeof(pRemoteInfo)), 0);
    ret = system("rm -rf /tmp/rc-proxy-params.json ");
    printf("RemoteVersion = %s\n",pRemoteInfo);
}
TEST_F(DeviceApiTestFixture,TestName_GetRemoteVers_Fail)
{
    char pRemoteInfo[256] = {0};
    EXPECT_EQ(GetRemoteVers(pRemoteInfo, sizeof(pRemoteInfo)), 0);
}
