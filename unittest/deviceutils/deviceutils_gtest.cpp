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
#include "deviceutils.h"
//#include "urlHelper.h"
//#include "common_device_api.h"
// Forward declaration for external function (mocked)
//int allocDowndLoadDataMem(void *ptr, int size);
//extern "C" int allocDowndLoadDataMem(DownloadData *ptr, int size);
}

#define JSON_STR_LEN        1000

#define GTEST_DEFAULT_RESULT_FILEPATH "/tmp/Gtest_Report/"
#define GTEST_DEFAULT_RESULT_FILENAME "RdkFwDwnld_DeviceUtils_gtest_report.json"
#define GTEST_REPORT_FILEPATH_SIZE 256

using namespace testing;
using namespace std;
using ::testing::Return;
using ::testing::StrEq;


DeviceUtilsMock *g_DeviceUtilsMock = NULL;

class DeviceUtilsTestFixture : public ::testing::Test {
    protected:
        DeviceUtilsMock mockDeviceUtils;

        DeviceUtilsTestFixture()
        {
            g_DeviceUtilsMock = &mockDeviceUtils;
        }
        virtual ~DeviceUtilsTestFixture()
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
/*
TEST_F(DeviceUtilsTestFixture, TestName_stripinvalidchar_Null)
{
    EXPECT_EQ(stripinvalidchar(NULL, 0), 0);
}
*/
/*
TEST_F(DeviceUtilsTestFixture, TestName_stripinvalidchar_notNull)
{
    char data[32];
    snprintf(data, sizeof(data), "%s", "Satya@#123456");
    EXPECT_NE(stripinvalidchar(data, sizeof(data)), 0);//TODO: Need to check how to do >= check
}
*/
// COMMENTED OUT: makeHttpHttps is now in common_utilities library and should be tested there
// This function is not called by local production code, so no mock is needed
/*
TEST_F(DeviceUtilsTestFixture, TestName_makeHttpHttps_Null)
{
    EXPECT_EQ(makeHttpHttps(NULL, 0), 0);
}
TEST_F(DeviceUtilsTestFixture, TestName_makeHttpHttps_notNull)
{
    char data[32];
    snprintf(data, sizeof(data), "%s", "https://xyz.com");
    EXPECT_NE(makeHttpHttps(data, sizeof(data)), 0);//TODO: Need to check how to do >= check
}
TEST_F(DeviceUtilsTestFixture, TestName_makeHttpHttp_check)
{
    char data[32];
    snprintf(data, sizeof(data), "%s", "http://xyz.com");
    EXPECT_NE(makeHttpHttps(data, sizeof(data)), 0);//TODO: Need to check how to do >= check
}
TEST_F(DeviceUtilsTestFixture, TestName_makeHttpHttp_badurl)
{
    char data[32];
    snprintf(data, sizeof(data), "%s", "ht//xyz.com");
    EXPECT_NE(makeHttpHttps(data, sizeof(data)), 0);
}
*/
/*
TEST_F(DeviceUtilsTestFixture, TestName_allocDowndLoadDataMem_Null)
{
    EXPECT_EQ(allocDowndLoadDataMem(NULL, 0), 1);
}
*/
/*
TEST_F(DeviceUtilsTestFixture, TestName_allocDowndLoadDataMem_NonNull)
{
    DownloadData pDwnData;
    EXPECT_EQ(allocDowndLoadDataMem(&pDwnData, sizeof(pDwnData)), 0);
}
*/
// COMMENTED OUT: get_system_uptime is now in common_utilities library and should be tested there
// This function is not called by local production code, so no mock is needed
/*
TEST_F(DeviceUtilsTestFixture, TestName_get_system_uptime_Null)
{
    EXPECT_EQ(get_system_uptime(NULL), false);
}
TEST_F(DeviceUtilsTestFixture, TestName_get_system_uptime_NonNull)
{
    double data;
    EXPECT_EQ(get_system_uptime(&data), true);
}
*/
TEST_F(DeviceUtilsTestFixture, TestName_getJRPCTokenData_BugOverflow)
{
    char token[32];
    char pJsonStr[] = "\{\"token\":\"abcdefghijklmnopqrstuvwxyz1234567890abcdefghijklmnopqrstuvwxyz123456ujklmnbvxawer\",\"success\":true}";
    EXPECT_EQ(getJRPCTokenData(token, pJsonStr, sizeof(token)), 0);
    printf("token = %s\n",token);
}
TEST_F(DeviceUtilsTestFixture, TestName_getJRPCTokenData_bufund)
{
    char token[32];
    char pJsonStr[] = "\{\"token\":\"eybhg-Osn3s\",\"success\":true}";
    EXPECT_EQ(getJRPCTokenData(token, pJsonStr, sizeof(token)), 0);
    printf("token = %s\n",token);
}
TEST_F(DeviceUtilsTestFixture, TestName_getJRPCTokenData_Null)
{
    EXPECT_EQ(getJRPCTokenData(NULL, NULL, 0), -1);
}
//int getJsonRpc(char *post_data, DownloadData* pJsonRpc )
TEST_F(DeviceUtilsTestFixture, TestName_getJsonRpc_Success)
{
    int ret;
    DownloadData pJsonRpc;
    FILE *fp = NULL;
    pJsonRpc.pvOut = malloc(50);
    pJsonRpc.datasize = 0;
    pJsonRpc.memsize = 50;
    char post_data[] = "Testing";
    ret = system("echo \{\"token\":\"eybhg-Osn3s\",\"success\":true} > /tmp/jrpctoken.txt");
    /*EXPECT_CALL(*g_DeviceUtilsMock, v_secure_popen(_, _, _))
                .Times(1)
                .WillOnce(Invoke([&](const char *mode, const char *cmd, const char *opt) {
                printf("WELCOMEEEEEEEEEEEEEEEEEEE\n");
                fp = fopen("/tmp/jrpctoken.txt", "r");
		fp = NULL;
                return fp;
        }));*/
    //EXPECT_CALL(*g_DeviceUtilsMock, v_secure_popen(_, _, _)).Times(1).WillOnce(Return(fopen("/tmp/jrpctoken.txt", "r")));
    EXPECT_CALL(*g_DeviceUtilsMock, v_secure_popen(_, _, _)).Times(1).WillOnce(Return(fp));
    EXPECT_CALL(*g_DeviceUtilsMock, doCurlInit()).Times(1).WillOnce(Return((size_t *)1));
    EXPECT_CALL(*g_DeviceUtilsMock, getJsonRpcData(_, _, _, _)).Times(1).WillOnce(Return(0));
    EXPECT_CALL(*g_DeviceUtilsMock, doStopDownload(_)).Times(1).WillOnce(Return());
    EXPECT_EQ(getJsonRpc(post_data, &pJsonRpc), 0);
    ret = system("rm -f /tmp/jrpctoken.txt");
}
TEST_F(DeviceUtilsTestFixture, TestName_getJsonRpc_Fail)
{
    int ret;
    DownloadData pJsonRpc;
    FILE *fp = NULL;
    pJsonRpc.pvOut = malloc(50);
    pJsonRpc.datasize = 0;
    pJsonRpc.memsize = 50;
    char post_data[] = "Testing";
    ret = system("echo \{\"token\":\"eybhg-Osn3s\",\"success\":true} > /tmp/jrpctoken.txt");
    /*EXPECT_CALL(*g_DeviceUtilsMock, v_secure_popen(_, _, _))
                .Times(1)
                .WillOnce(Invoke([&](const char *mode, const char *cmd, const char *opt) {
                printf("WELCOMEEEEEEEEEEEEEEEEEEE\n");
                fp = fopen("/tmp/jrpctoken.txt", "r");
		fp = NULL;
                return fp;
        }));*/
    //EXPECT_CALL(*g_DeviceUtilsMock, v_secure_popen(_, _, _)).Times(1).WillOnce(Return(fopen("/tmp/jrpctoken.txt", "r")));
    EXPECT_CALL(*g_DeviceUtilsMock, v_secure_popen(_, _, _)).Times(1).WillOnce(Return(fp));
    EXPECT_CALL(*g_DeviceUtilsMock, doCurlInit()).Times(1).WillOnce(Return((size_t *)1));
    EXPECT_CALL(*g_DeviceUtilsMock, getJsonRpcData(_, _, _, _)).Times(1).WillOnce(Return(-1));
    EXPECT_CALL(*g_DeviceUtilsMock, doStopDownload(_)).Times(1).WillOnce(Return());
    EXPECT_EQ(getJsonRpc(post_data, &pJsonRpc), -1);
    ret = system("rm -f /tmp/jrpctoken.txt");
}
TEST_F(DeviceUtilsTestFixture, TestName_getInstalledBundleFileList_Fail)
{
    metaDataFileList_st *meta_ret = NULL;
    EXPECT_EQ(getInstalledBundleFileList(), meta_ret);
}
TEST_F(DeviceUtilsTestFixture, TestName_getMetaDataFile_Null)
{
    metaDataFileList_st *meta_ret = NULL;
    EXPECT_EQ(getMetaDataFile("./test"), meta_ret);
}
TEST_F(DeviceUtilsTestFixture, TestName_mergeLists_Null)
{
    metaDataFileList_st *meta_ret = NULL;
    EXPECT_EQ(mergeLists(NULL, NULL), meta_ret);
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
    cout << "Starting deviceutils and device api App ===================>" << endl;
    return RUN_ALL_TESTS();
}
