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
#include "rfcinterface.h"
#include "iarmInterface.h"
void DwnlStopEventHandler(const char *owner, IARM_EventId_t eventId, void *data, size_t len);
}
#include "./mocks/interface_mock.h"
#include "./mocks/mock_rbus.h"

#define IMG_DWL_EVENT "ImageDwldEvent"
#define FW_STATE_EVENT "FirmwareStateEvent"

#define GTEST_DEFAULT_RESULT_FILEPATH "/tmp/Gtest_Report/"
#define GTEST_DEFAULT_RESULT_FILENAME "RdkFwDwnld_Interface_gtest_report.json"
#define GTEST_REPORT_FILEPATH_SIZE 256

using namespace testing;
using namespace std;
using ::testing::Return;
using ::testing::StrEq;

FwDlInterfaceMock *g_InterfaceMock = NULL;

class InterfaceTestFixture : public ::testing::Test {
    protected:
        FwDlInterfaceMock mockfwdlInterface;

        InterfaceTestFixture()
        {
            g_InterfaceMock = &mockfwdlInterface;
        }
        virtual ~InterfaceTestFixture()
        {
            g_InterfaceMock = NULL;
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

TEST_F(InterfaceTestFixture, TestName_read_RFCPropertyNull)
{
    //char pdri_image[] = "Test_pdri.bin";
    //EXPECT_CALL(*g_DeviceStatusMock, getRFCParameter(_, _, _)).Times(1).WillOnce(Return(0));
    //EXPECT_CALL(*g_DeviceStatusMock, getDevicePropertyData(_, _, _)).Times(1).WillOnce(Return(0));
    /*EXPECT_CALL(*g_DeviceStatusMock, GetPDRIFileName(_, _))
                .Times(1)
                .WillOnce(Invoke([&pdri_image]( char *pPDRIFilename, size_t szBufSize ) {
                strncpy(pPDRIFilename, pdri_image, szBufSize - 1);
                pPDRIFilename[szBufSize - 1] = '\0';  // Ensure null-termination
                return strlen(pPDRIFilename);
        }));*/
        EXPECT_EQ(read_RFCProperty(NULL, NULL, NULL, 0), -1);
}
TEST_F(InterfaceTestFixture, TestName_read_RFCPropertySuccess)
{
    char data[16] = {0};
    EXPECT_CALL(*g_InterfaceMock, getRFCParameter(_, _, _)).Times(1).WillOnce(Return(1));
    EXPECT_EQ(read_RFCProperty("IncrementalCDL", "rfccdl", data, sizeof(data)), 1);
}
TEST_F(InterfaceTestFixture, TestName_read_RFCPropertyFail)
{
    char data[16] = {0};
    EXPECT_CALL(*g_InterfaceMock, getRFCParameter(_, _, _)).Times(1).WillOnce(Return(0));
    EXPECT_EQ(read_RFCProperty("IncrementalCDL", "rfccdl", data, sizeof(data)), -1);
}
TEST_F(InterfaceTestFixture, TestName_write_RFCPropertyNull)
{
    EXPECT_EQ(write_RFCProperty(NULL, NULL, NULL, 0), -1);
}
TEST_F(InterfaceTestFixture, TestName_write_RFCPropertySuccess)
{
    EXPECT_CALL(*g_InterfaceMock, setRFCParameter(_, _, _,_)).Times(1).WillOnce(Return(1));
    EXPECT_EQ(write_RFCProperty("int","fwdlrfc","true", 1), 1);
}
TEST_F(InterfaceTestFixture, TestName_getRFCSettingsSuccess)
{
    Rfc_t rfcvalue;
    memset(&rfcvalue, '\0', sizeof(rfcvalue));
    EXPECT_CALL(*g_InterfaceMock, getRFCParameter(_, _, _)).Times(4).WillRepeatedly(Return(1));
    EXPECT_EQ(getRFCSettings(&rfcvalue), 0);
}
TEST_F(InterfaceTestFixture, TestName_getRFCSettingsFail)
{
    Rfc_t rfcvalue;
    memset(&rfcvalue, '\0', sizeof(rfcvalue));
    EXPECT_CALL(*g_InterfaceMock, getRFCParameter(_, _, _)).Times(4).WillRepeatedly(Return(-1));
    EXPECT_EQ(getRFCSettings(&rfcvalue), 0);
}
TEST_F(InterfaceTestFixture, TestName_write_RFCPropertySuccess2)
{
    EXPECT_CALL(*g_InterfaceMock, setRFCParameter(_, _, _,_)).Times(1).WillOnce(Return(1));
    EXPECT_EQ(write_RFCProperty("int","fwdlrfc","true", 2), 1);
}
TEST_F(InterfaceTestFixture, TestName_write_RFCPropertySuccess3)
{
    EXPECT_CALL(*g_InterfaceMock, setRFCParameter(_, _, _,_)).Times(1).WillOnce(Return(1));
    EXPECT_EQ(write_RFCProperty("int","fwdlrfc","true", 3), 1);
}
TEST_F(InterfaceTestFixture, TestName_write_RFCPropertyFail)
{
    EXPECT_CALL(*g_InterfaceMock, setRFCParameter(_, _, _,_)).Times(1).WillOnce(Return(-1));
    EXPECT_CALL(*g_InterfaceMock, getRFCErrorString(_)).Times(1).WillOnce(Return(0));
    EXPECT_EQ(write_RFCProperty("int","fwdlrfc","true", 1), -1);
}
TEST_F(InterfaceTestFixture, TestName_isMtlsEnabledSuccess)
{
    EXPECT_CALL(*g_InterfaceMock, getRFCParameter(_, _, _)).Times(1).WillOnce(Return(1));
    EXPECT_CALL(*g_InterfaceMock, getDevicePropertyData(_, _, _)).Times(1).WillOnce(Return(0));
    EXPECT_EQ(isMtlsEnabled("PLATCO"), 1);
}
TEST_F(InterfaceTestFixture, TestName_isMtlsEnabledFail)
{
    EXPECT_CALL(*g_InterfaceMock, getRFCParameter(_, _, _)).Times(1).WillOnce(Return(-1));
    EXPECT_CALL(*g_InterfaceMock, getDevicePropertyData(_, _, _)).Times(1).WillOnce(Return(1));
    EXPECT_EQ(isMtlsEnabled("PLATCO"), 0);
}

TEST_F(InterfaceTestFixture, TestName_isMmgbleNotifyEnabledFail)
{
    EXPECT_CALL(*g_InterfaceMock, getRFCParameter(_, _, _)).Times(1).WillOnce(Return(-1));
    EXPECT_EQ(isMmgbleNotifyEnabled(), false);
}
TEST_F(InterfaceTestFixture, TestName_isMmgbleNotifyEnabledSuccess)
{
    EXPECT_CALL(*g_InterfaceMock, getRFCParameter(_, _, _)).Times(1).WillOnce(Return(1));
    EXPECT_EQ(isMmgbleNotifyEnabled(), true);
}
TEST_F(InterfaceTestFixture, TestName_isDebugServicesEnabledFail)        
{
    EXPECT_CALL(*g_InterfaceMock, getRFCParameter(_, _, _)).Times(1).WillOnce(Return(-1));
    EXPECT_EQ(isDebugServicesEnabled(), false);
}
TEST_F(InterfaceTestFixture, TestName_isDebugServicesEnableSuccess)
{
    EXPECT_CALL(*g_InterfaceMock, getRFCParameter(_, _, _)).Times(1).WillOnce(Return(1));
    EXPECT_EQ(isDebugServicesEnabled(), true);
}
TEST_F(InterfaceTestFixture, TestName_isIncremetalCDLEnableSuccess)
{
    EXPECT_CALL(*g_InterfaceMock, getRFCParameter(_, _, _)).Times(1).WillOnce(Return(1));
    //EXPECT_CALL(*g_InterfaceMock, getDevicePropertyData(_, _, _)).Times(1).WillOnce(Return(0));
    EXPECT_CALL(*g_InterfaceMock, filePresentCheck(_)).WillOnce(Return(0));
    EXPECT_EQ(isIncremetalCDLEnable("/tmp/123.bin"), 1);
}
TEST_F(InterfaceTestFixture, TestName_isIncremetalCDLEnableFailrfc)
{
    EXPECT_CALL(*g_InterfaceMock, getRFCParameter(_, _, _)).Times(1).WillOnce(Return(-1));
    //EXPECT_CALL(*g_InterfaceMock, getDevicePropertyData(_, _, _)).Times(1).WillOnce(Return(0));
    //EXPECT_CALL(*g_InterfaceMock, filePresentCheck(_)).WillOnce(Return(0));
    EXPECT_EQ(isIncremetalCDLEnable("/tmp/123.bin"), 0);
}
TEST_F(InterfaceTestFixture, TestName_init_event_handlerSuccess)
{
    char connectdata[] = "0";
    EXPECT_CALL(*g_InterfaceMock, IARM_Bus_Init(_)).Times(1).WillOnce(Return(0));
    EXPECT_CALL(*g_InterfaceMock, IARM_Bus_Connect()).WillOnce(Return(0));
    EXPECT_CALL(*g_InterfaceMock, IARM_Bus_RegisterEventHandler(_,_,_)).WillOnce(Return(0));
    EXPECT_CALL(*g_InterfaceMock, IARM_Bus_IsConnected(_, _))
                .Times(2)
                .WillRepeatedly(Invoke([&connectdata]( const char *name, int *data ) {
                if (data != NULL) {
		    *data = atoi(connectdata);
		}else {
		    *data = 0;
		}
                return 0;
        }));
    EXPECT_EQ(init_event_handler(), 0);
}
TEST_F(InterfaceTestFixture, TestName_init_event_handlerConnected)
{
    char connectdata[] = "1";
    EXPECT_CALL(*g_InterfaceMock, IARM_Bus_IsConnected(_, _))
                .Times(1)
                .WillRepeatedly(Invoke([&connectdata]( const char *name, int *data ) {
                if (data != NULL) {
		    *data = atoi(connectdata);
		}else {
		    *data = 0;
		}
                return 0;
        }));
    EXPECT_EQ(init_event_handler(), 0);
}
TEST_F(InterfaceTestFixture, TestName_term_event_handlerSuccess)
{
    EXPECT_CALL(*g_InterfaceMock, IARM_Bus_Term()).Times(1).WillOnce(Return(0));
    EXPECT_CALL(*g_InterfaceMock, IARM_Bus_Disconnect()).WillOnce(Return(0));
    EXPECT_CALL(*g_InterfaceMock, IARM_Bus_UnRegisterEventHandler(_,_)).WillOnce(Return(0));
    EXPECT_EQ(term_event_handler(), 0);
}
TEST_F(InterfaceTestFixture, TestName_DwnlStopEventHandlerSuccess)
{
    int data = 1;
    EXPECT_CALL(*g_InterfaceMock, getAppMode()).Times(1).WillOnce(Return(0));
    EXPECT_CALL(*g_InterfaceMock, interuptDwnl(_)).WillOnce(Return(0));
    DwnlStopEventHandler("test", IARM_BUS_RDKVFWUPGRADER_MODECHANGED, &data, 0);
    EXPECT_EQ(0, 0);
}
TEST_F(InterfaceTestFixture, TestName_DwnlStopEventHandlerSuccess1)
{
    int data = 1;
    EXPECT_CALL(*g_InterfaceMock, getAppMode()).Times(1).WillOnce(Return(1));
    DwnlStopEventHandler("test", IARM_BUS_RDKVFWUPGRADER_MODECHANGED, &data, 0);
    EXPECT_EQ(0, 0);
}
TEST_F(InterfaceTestFixture, TestName_DwnlStopEventHandlerNull)
{
    DwnlStopEventHandler(NULL, IARM_BUS_RDKVFWUPGRADER_MODECHANGED, NULL, 0);
    EXPECT_EQ(0, 0);
}
TEST_F(InterfaceTestFixture, TestName_eventManagerNull)
{
    eventManager(NULL, NULL);
    EXPECT_EQ(0, 0);
}
TEST_F(InterfaceTestFixture, TestName_eventManagerSuccess)
{
    EXPECT_CALL(*g_InterfaceMock, IARM_Bus_BroadcastEvent(_,_,_,_)).WillOnce(Return(0));
    eventManager(IMG_DWL_EVENT, "2");
    EXPECT_EQ(0, 0);
}
TEST_F(InterfaceTestFixture, TestName_eventManagerFail)
{
    EXPECT_CALL(*g_InterfaceMock, IARM_Bus_BroadcastEvent(_,_,_,_)).WillOnce(Return(1));
    eventManager(IMG_DWL_EVENT, "2");
    EXPECT_EQ(0, 0);
}
TEST(TestisDnsResolve, invokeRbusDCMReport)
{
    rbusError_t status = invokeRbusDCMReport();
    EXPECT_EQ(status, RBUS_ERROR_SUCCESS);
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
    cout << "Starting rdkfw_interface_gtest MAIN ===========================>" << endl;
    return RUN_ALL_TESTS();
}
