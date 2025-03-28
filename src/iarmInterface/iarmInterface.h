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

#ifndef VIDEO_IARMINTERFACE_IARMINTERFACE_H_
#define VIDEO_IARMINTERFACE_IARMINTERFACE_H_

#if defined(IARM_ENABLED)
#ifndef GTEST_ENABLE
#include "sysMgr.h"
#include "libIARMCore.h"
#include "libIBus.h"
#include "libIBusDaemon.h"
#include <glib.h>
#endif
#ifdef EN_MAINTENANCE_MANAGER
#include "maintenanceMGR.h"
#endif

#ifdef GTEST_ENABLE

typedef int IARM_EventId_t;
#define IARM_BUS_SYSMGR_NAME "SYSMGR"
typedef enum _IARM_Result_t
  {
    IARM_RESULT_SUCCESS,
    IARM_RESULT_INVALID_PARAM,
    IARM_RESULT_INVALID_STATE,
  } IARM_Result_t;

typedef enum _SYSMgr_EventId_t {
      IARM_BUS_SYSMGR_EVENT_SYSTEMSTATE,
  	IARM_BUS_SYSMGR_EVENT_IMAGE_DNLD,
  	IARM_BUS_SYSMGR_EVENT_USB_MOUNT_CHANGED,
  	IARM_BUS_SYSMGR_EVENT_MAX
} IARM_Bus_SYSMgr_EventId_t;

typedef struct _IARM_BUS_SYSMgr_EventData_t{
  	union {
  		struct _CARD_FWDNLD_DATA {
  			char eventType;
  			char status;
  		} cardFWDNLD;
  		struct _IMAGE_FWDNLD_DATA {
  			char status;
  		} imageFWDNLD;
  		struct _SystemStates {
  			int stateId;
  			int state;
  			int error;
  			char payload[128];
  		} systemStates;

  	} data;
  }IARM_Bus_SYSMgr_EventData_t;

#define IARM_BUS_SYSMGR_SYSSTATE_FIRMWARE_UPDATE_STATE 1
#define IARM_BUS_SYSMGR_SYSSTATE_FIRMWARE_DWNLD 2
#define IARM_BUS_SYSMGR_SYSSTATE_RED_RECOV_UPDATE_STATE 3

typedef char gchar;

#endif
#endif

#define IARM_BUS_RDKVFWUPGRADER_MGR_NAME "RdkvFWupgrader"
#define IARM_BUS_RDKVFWUPGRADER_MODECHANGED 0
#define IARM_BUS_NETSRVMGR_API_isConnectedToInternet "isConnectedToInternet"
#define IARM_BUS_NM_SRV_MGR_NAME "NET_SRV_MGR"

//Image Download States
#define IMAGE_FWDNLD_UNINITIALIZED "0"
#define IMAGE_FWDNLD_DOWNLOAD_INPROGRESS "1"
#define IMAGE_FWDNLD_DOWNLOAD_COMPLETE "2"
#define IMAGE_FWDNLD_DOWNLOAD_FAILED "3"
#define IMAGE_FWDNLD_FLASH_INPROGRESS "4"
#define IMAGE_FWDNLD_FLASH_COMPLETE "5"
#define IMAGE_FWDNLD_FLASH_FAILED "6"

//maintaince states
#define MAINT_FWDOWNLOAD_COMPLETE "8"
#define MAINT_FWDOWNLOAD_ERROR "9"
#define MAINT_FWDOWNLOAD_ABORTED "10"
#define MAINT_CRITICAL_UPDATE "11"
#define MAINT_REBOOT_REQUIRED "12"
#define MAINT_FWDOWNLOAD_INPROGRESS "15"
#define MAINT_FWDOWNLOAD_FG "17"
#define MAINT_FWDOWNLOAD_BG "18"

//Firmware Upgrade states
#define FW_STATE_UNINITIALIZED "0"
#define FW_STATE_REQUESTING "1"
#define FW_STATE_DOWNLOADING "2"
#define FW_STATE_FAILED "3"
#define FW_STATE_DOWNLOAD_COMPLETE "4"
#define FW_STATE_VALIDATION_COMPLETE "5"
#define FW_STATE_PREPARING_TO_REBOOT "6"
#define FW_STATE_ONHOLD_FOR_OPTOUT "7"
#define FW_STATE_CRITICAL_REBOOT "8"
#define FW_STATE_NO_UPGRADE_REQUIRED "9"

//Red Recovery states
#define RED_RECOVERY_COMPLETED "0"
#define RED_RECOVERY_STARTED "1"
#define RED_RECOVERY_DOWNLOADED "2"
#define RED_RECOVERY_PROGRAMMED "3"

/** Description: Send event to iarm event manager
 *
 *  @param cur_event_name: event name.
 *  @param event_status: Status Of the event.
 *  @return void.
 */
void eventManager(const char *cur_event_name, const char *event_status) ;
int term_event_handler(void);
int init_event_handler(void);
void interuptDwnl(int app_mode);
bool isConnectedToInternet (void);

#endif /* VIDEO_IARMINTERFACE_IARMINTERFACE_H_ */
