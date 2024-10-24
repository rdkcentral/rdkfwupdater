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

/** Description: Send event to iarm event manager
 *
 *  @param cur_event_name: event name.
 *  @param event_status: Status Of the event.
 *  @return void.
 */

#include "testiarmInterface.h"

//#include "rdkv_cdl_log_wrapper.h"

void eventManagerTest(const char *cur_event_name, int app_mode) {
    if(cur_event_name == NULL) {
        printf("eventManager() failed due to NULL parameter\n");
        return;
    }
    int len = sizeof(app_mode);
    IARM_Result_t ret_code = IARM_RESULT_SUCCESS;
    //IARM_Bus_SYSMgr_EventData_t event_data;
    IARM_Bus_Init(cur_event_name);
    IARM_Bus_Connect();
    printf("%s: Generate IARM_BUS_NAME current Event=%s,eventstatus=%d:len=%d\n", __FUNCTION__, cur_event_name, app_mode, len);
    /*for( i = 0; i < len; i++ ) {
        if(!(strcmp(cur_event_name, event_list[i].event_name))) {
            event_data.data.systemStates.stateId = event_list[i].sys_state_event;
            event_data.data.systemStates.state = atoi(event_status);
            event_match = true;
            break;
        }
    }*/
    /*CID:327005-Constant variable guards dead
     * Unwanted condition it always true so else part is not work
     * so removing the condition*/
        ret_code = IARM_Bus_BroadcastEvent(cur_event_name, (IARM_EventId_t) IARM_BUS_RDKVFWUPGRADER_MODECHANGED, (void *) &app_mode,
                len);
        if(ret_code == IARM_RESULT_SUCCESS) {
            //SWLOG_INFO("%s : >> IARM SUCCESS  Event=%s,sysStateEvent=%d\n", __FUNCTION__, event_list[i].event_name, event_list[i].sys_state_event);
            printf("%s : >> IARM SUCCESS\n", __FUNCTION__);
        }else {
            //SWLOG_ERROR("%s : >> IARM FAILURE  Event=%s,sysStateEvent=%d\n", __FUNCTION__, event_list[i].event_name, event_list[i].sys_state_event);
            printf("%s : >> IARM FAILURE\n", __FUNCTION__);
	}
    IARM_Bus_Disconnect();
    IARM_Bus_Term();
    printf("%s : IARM_event_sender closing\n", __FUNCTION__);
}

int main(int argc, char *argv[])
{
   int app_mode = 0;
   if (argc == 2) {
       app_mode = atoi(argv[1]);
       printf("app mode = %d\n", app_mode);
       eventManagerTest("RdkvFWupgrader", app_mode);
   } else {
       printf("Invalid no of argument\nReq only 1 argument 1 or 0\n");
   }
   return 0;
}
