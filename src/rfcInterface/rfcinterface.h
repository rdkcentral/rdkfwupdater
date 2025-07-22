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

#ifndef VIDEO_RFCINTERFACE_RFCINTERFACE_H_
#define VIDEO_RFCINTERFACE_RFCINTERFACE_H_


#include <stdbool.h>
// Guard the inclusion in such a way that this module could be easily combined as stub even on
// platforms without this api or an alternative interface such as rbus or ccspbus or xmidt
#if defined(RFC_API_ENABLED)
#ifndef GTEST_ENABLE
#include "rfcapi.h"
#endif
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RFC_VALUE_BUF_SIZE 512
#define READ_RFC_SUCCESS 1
#define READ_RFC_FAILURE -1
#define READ_RFC_NOTAPPLICABLE 0
#define WRITE_RFC_SUCCESS 1
#define WRITE_RFC_FAILURE -1
#define WRITE_RFC_NOTAPPLICABLE 0

#ifdef GTEST_ENABLE
/*Below code is use when GTEST is enable. Because During this
 * L1 Unit Test rfcapi.h header file not present */
typedef struct gtest_rfc {
    char value[32];
    char name[32];
    int type;
    int status;
}RFC_ParamData_t;
typedef enum {
    WDMP_FAILURE = 0,
    WDMP_SUCCESS,
    WDMP_ERR_DEFAULT_VALUE
}WDMP_STATUS;

#define WDMP_STRING 1
#define WDMP_UINT 3
#define WDMP_BOOLEAN 2
#endif

typedef enum
{
  RFC_STRING=1,
  RFC_BOOL,
  RFC_UINT
}RFCVALDATATYPE;

typedef struct rfcdetails {
    char rfc_throttle[RFC_VALUE_BUF_SIZE];
    char rfc_topspeed[RFC_VALUE_BUF_SIZE];
    char rfc_incr_cdl[RFC_VALUE_BUF_SIZE];
    char rfc_mtls[RFC_VALUE_BUF_SIZE];
}Rfc_t;

#define RFC_THROTTLE "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.SWDLSpLimit.Enable"
#define RFC_TOPSPEED "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.SWDLSpLimit.TopSpeed"
#define RFC_INCR_CDL "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.IncrementalCDL.Enable"
#define RFC_MTLS "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.MTLS.mTlsXConfDownload.Enable"
#define RFC_MNG_NOTIFY "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.ManageableNotification.Enable"

#define RFC_FW_DWNL_START "Device.DeviceInfo.X_RDKCENTRAL-COM_xOpsDeviceMgmt.RPC.FirmwareDownloadStartedNotification"
#define RFC_FW_DWNL_END "Device.DeviceInfo.X_RDKCENTRAL-COM_xOpsDeviceMgmt.RPC.FirmwareDownloadCompletedNotification"
#define RFC_FW_REBOOT_NOTIFY "Device.DeviceInfo.X_RDKCENTRAL-COM_xOpsDeviceMgmt.RPC.RebootPendingNotification"
#define RFC_FW_AUTO_EXCLUDE "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.FWUpdate.AutoExcluded.Enable"
#define RFC_DEBUGSRV "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Identity.DbgServices.Enable"

#define RFC_XCONF_CHECK_NOW "Device.X_COMCAST-COM_Xcalibur.Client.xconfCheckNow"

int getRFCSettings(Rfc_t *rfc_list);

int read_RFCProperty(char* type, const char* key, char *data, size_t datasize);
int write_RFCProperty(char* type, const char* key, const char *data, RFCVALDATATYPE datatype);

int isMtlsEnabled(const char *);
int isIncremetalCDLEnable(const char *file_name);
bool isMmgbleNotifyEnabled(void);
bool isDebugServicesEnabled(void);

#endif /* VIDEO_RFCINTERFACE_RFCINTERFACE_H_ */
