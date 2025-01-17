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

#include <stdbool.h>
#if defined(IARM_ENABLED)

#include "iarmInterface.h"

#ifdef CTRLM_ENABLED
#include "ctrlm_ipc_device_update.h"
#endif

#include "rdkv_cdl_log_wrapper.h"
#include <curl/curl.h>
#include "json_process.h"
#ifndef GTEST_ENABLE
#include "urlHelper.h"
#include "json_parse.h"
#endif
#include "deviceutils.h"

int getAppMode(void);
#define IARM_RDKVFWUPGRADER_EVENT "RDKVFWEvent"

/** Description: Send event to iarm event manager
 *
 *  @param cur_event_name: event name.
 *  @param event_status: Status Of the event.
 *  @return void.
 */
void eventManager(const char *cur_event_name, const char *event_status) {
    struct eventList {
        gchar* event_name;
        unsigned char sys_state_event;
    } event_list[] = { 
        { "ImageDwldEvent", IARM_BUS_SYSMGR_SYSSTATE_FIRMWARE_DWNLD },
        { "FirmwareStateEvent", IARM_BUS_SYSMGR_SYSSTATE_FIRMWARE_UPDATE_STATE }
    };
    IARM_Bus_SYSMgr_EventData_t event_data;
    int i;
    int len = sizeof(event_list) / sizeof(struct eventList);
    IARM_Result_t ret_code = IARM_RESULT_SUCCESS; 
    bool event_match = false;


    if(cur_event_name == NULL || event_status == NULL) {
        SWLOG_ERROR("eventManager() failed due to NULL parameter\n");
        return;
    }
    
    SWLOG_INFO("%s: Generate IARM_BUS_NAME current event=%s\n", __FUNCTION__, cur_event_name);
#ifdef EN_MAINTENANCE_MANAGER
    if ( !(strncmp(cur_event_name,"MaintenanceMGR", 14)) ) {
        IARM_Bus_MaintMGR_EventData_t infoStatus;
        unsigned int main_mgr_event;

        memset( &infoStatus, 0, sizeof(IARM_Bus_MaintMGR_EventData_t) );
        main_mgr_event = atoi(event_status);
        SWLOG_INFO(">>>>> Identified MaintenanceMGR with event value=%u", main_mgr_event);
        infoStatus.data.maintenance_module_status.status = (IARM_Maint_module_status_t)main_mgr_event;
        ret_code=IARM_Bus_BroadcastEvent(IARM_BUS_MAINTENANCE_MGR_NAME,(IARM_EventId_t)IARM_BUS_MAINTENANCEMGR_EVENT_UPDATE, (void *)&infoStatus, sizeof(infoStatus));
        SWLOG_INFO(">>>>> IARM %s  Event  = %d",(ret_code == IARM_RESULT_SUCCESS) ? "SUCCESS" : "FAILURE",\
                infoStatus.data.maintenance_module_status.status);
    }
    else
#endif
    if( !(strncmp( cur_event_name,"PeripheralUpgradeEvent", 22 )) )
    {
#ifdef CTRLM_ENABLED
        ctrlm_device_update_iarm_call_update_available_t firmwareInfo;
        IARM_Result_t result;
        char *pSaved;
	char* event_status_copy = strdup(event_status);
	if(event_status_copy == NULL) {
            SWLOG_ERROR("eventManager() failed due to NULL parameter\n");
            return;
        }    

        SWLOG_INFO( "%s: event_status = %s\n", __FUNCTION__, event_status );
        firmwareInfo.api_revision = CTRLM_DEVICE_UPDATE_IARM_BUS_API_REVISION;
        snprintf( firmwareInfo.firmwareLocation, sizeof(firmwareInfo.firmwareLocation), "%s",
                  strtok_r( event_status_copy, ":", &pSaved ) );
        snprintf( firmwareInfo.firmwareNames, sizeof(firmwareInfo.firmwareLocation), "%s",
                  strtok_r( NULL, ":", &pSaved ) );
        SWLOG_INFO( "%s: firmwareInfo.firmwareLocation = %s\nfirmwareInfo.firmwareNames = %s\n",
                    __FUNCTION__, firmwareInfo.firmwareLocation, firmwareInfo.firmwareNames );

        result = IARM_Bus_Call( CTRLM_MAIN_IARM_BUS_NAME,
                  CTRLM_DEVICE_UPDATE_IARM_CALL_UPDATE_AVAILABLE,
                  (void *)&firmwareInfo,
                  sizeof(firmwareInfo) );

        SWLOG_INFO("%s : IARM_Bus_Call result = %d\n", __FUNCTION__, (int)result );
	free(event_status_copy);
#else
        SWLOG_INFO( "%s: event_status = %s - no control manager available, not processing IARM event\n",
                    __FUNCTION__, event_status );
#endif
    }
    else
    {
        SWLOG_INFO( "%s: event_status = %u\n", __FUNCTION__, atoi(event_status) );
        for( i = 0; i < len; i++ ) {
            if(!(strcmp(cur_event_name, event_list[i].event_name))) {
                event_data.data.systemStates.stateId = event_list[i].sys_state_event;
                event_data.data.systemStates.state = atoi(event_status);
                event_match = true;
                break;
            }
        }
        if(event_match == true) {
            event_data.data.systemStates.error = 0;
            ret_code = IARM_Bus_BroadcastEvent(IARM_BUS_SYSMGR_NAME, (IARM_EventId_t) IARM_BUS_SYSMGR_EVENT_SYSTEMSTATE, (void *) &event_data,
                       sizeof(event_data));
            if(ret_code == IARM_RESULT_SUCCESS) {
                SWLOG_INFO("%s : >> IARM SUCCESS  Event=%s,sysStateEvent=%d\n", __FUNCTION__, event_list[i].event_name, event_list[i].sys_state_event);
            }else {
                SWLOG_ERROR("%s : >> IARM FAILURE  Event=%s,sysStateEvent=%d\n", __FUNCTION__, event_list[i].event_name, event_list[i].sys_state_event);
            }
        }else {
        SWLOG_ERROR("%s: There are no matching IARM sys events for %s\n", __FUNCTION__, cur_event_name);
        }
    }
    SWLOG_INFO("%s : IARM_event_sender closing\n", __FUNCTION__);
}

/** Description: Send event to iarm event manager
 *
 *  @param cur_event_name: event name.
 *  @param event_status: Status Of the event.
 *  @return void.
 */
void DwnlStopEventHandler(const char *owner, IARM_EventId_t eventId, void *data, size_t len)
{
    int app_mode = -1;
    int recv_app_mode = -1;
    if (data == NULL) {
         SWLOG_ERROR("%s: Data is NULL\n", __FUNCTION__);
         return;
    }
    SWLOG_ERROR("%s: In event Data recv\n", __FUNCTION__);
    recv_app_mode = *((int *)data);
    SWLOG_INFO("%s: Data recv:%d\n", __FUNCTION__, *((int *)data));
    switch(eventId)
    {
         case IARM_BUS_RDKVFWUPGRADER_MODECHANGED:
         {
             app_mode = getAppMode();
	     if (recv_app_mode != app_mode) {
                 interuptDwnl(recv_app_mode);
	     } else {
                 SWLOG_INFO("Current app mode %d and recieved app mode %d is same\n", app_mode, recv_app_mode);
	     }
         }
         break;

         default:
         break;
    }
    return;
}

/** Description: Send event to iarm event manager
 *
 *  @param cur_event_name: event name.
 *  @param event_status: Status Of the event.
 *  @return void.
 */
static bool isConnected()
{
    IARM_Result_t res;
    int isRegistered = 0;
    res = IARM_Bus_IsConnected(IARM_RDKVFWUPGRADER_EVENT, &isRegistered);
    SWLOG_INFO("IARM_Bus_IsConnected: %d (%d)", res, isRegistered);

    if (isRegistered == 1) {
        return true;
    } else {
        return false;
    }
}
/** Description: To Register required IARM event handlers with appropriate callback function to handle the event.
 *
 *  @param cur_event_name: event name.
 *  @param event_status: Status Of the event.
 *  @return void.
 */
int init_event_handler(void)
{
    IARM_Result_t res;
    if (isConnected()) {
            SWLOG_INFO("IARM already connected\n");
    } else {
        res = IARM_Bus_Init(IARM_RDKVFWUPGRADER_EVENT);
	SWLOG_INFO("IARM_Bus_Init: %d\n", res);
	if (res == IARM_RESULT_SUCCESS || res == IARM_RESULT_INVALID_STATE) {
            SWLOG_INFO("SUCCESS: IARM_Bus_Init done!\n");

            res = IARM_Bus_Connect();
	    SWLOG_INFO("IARM_Bus_Connect: %d\n", res);
	    if (res == IARM_RESULT_SUCCESS || res == IARM_RESULT_INVALID_STATE) {
		if (isConnected()) {
		      SWLOG_INFO("SUCCESS: IARM_Bus_Connect done!\n");
		}
	    } else {
                SWLOG_ERROR("IARM_Bus_Connect failure: %d\n", res);
	    }
	} else {
             SWLOG_ERROR("IARM_Bus_Init failure: %d\n", res);
	}

	res = IARM_Bus_RegisterEventHandler(IARM_BUS_RDKVFWUPGRADER_MGR_NAME,  IARM_BUS_RDKVFWUPGRADER_MODECHANGED, DwnlStopEventHandler);
	SWLOG_INFO("%s: IARM_Bus_RegisterEventHandler ret=%d\n", __FUNCTION__, res);
    }

    return 0;
}

/** Description: This API UnRegister IARM event handlers in order to release bus-facing resources.
 *
 *  @param cur_event_name: event name.
 *  @param event_status: Status Of the event.
 *  @return void.
 */
int term_event_handler(void)
{
    IARM_Result_t res = 0;
    res = IARM_Bus_UnRegisterEventHandler(IARM_BUS_RDKVFWUPGRADER_MGR_NAME,  IARM_BUS_RDKVFWUPGRADER_MODECHANGED);
    SWLOG_INFO("Successfully terminated all event handlers:%d\n",res);
    IARM_Bus_Disconnect();
    IARM_Bus_Term();
        return 0;
}

bool isConnectedToInternet (void)
{
    bool isconnected = false;

    DownloadData DwnLoc;
    JSON *pJson = NULL;
    JSON *pItem = NULL;
    JSON *res_val = NULL;

    char post_data4[] = "{\"jsonrpc\":\"2.0\",\"id\":\"42\",\"method\": \"org.rdk.NetworkManager.IsConnectedToInternet\", \"params\" : { \"ipversion\" : \"IPv4\"}}";
    char post_data6[] = "{\"jsonrpc\":\"2.0\",\"id\":\"42\",\"method\": \"org.rdk.NetworkManager.IsConnectedToInternet\", \"params\" : { \"ipversion\" : \"IPv6\"}}";
    char status[20];
    if( MemDLAlloc( &DwnLoc, DEFAULT_DL_ALLOC ) == 0 )
    {
        if (0 != getJsonRpc(post_data4, &DwnLoc))
	{
	    SWLOG_INFO("%s :: isconnected JsonRpc call failed\n",__FUNCTION__);
	    return isconnected;
	}
        pJson = ParseJsonStr( (char *)DwnLoc.pvOut );

        if( pJson != NULL )
        {
            pItem = GetJsonItem( pJson, "result" );
            res_val = GetJsonItem( pItem, "status" );
            if( pItem != NULL )
            {
                strncpy(status, res_val->valuestring, sizeof(status));
                SWLOG_INFO("%s :: status = %s\n",__FUNCTION__, status);
                if (strcmp(status, "NO_INTERNET") != 0) {
                    SWLOG_INFO("%s :: ipv4 check only return\n",__FUNCTION__);
                    isconnected = true;
                }
                else
                {
		    if (0 != getJsonRpc(post_data6, &DwnLoc))
		    {
	    	        SWLOG_INFO("%s :: isconnected JsonRpc call failed\n",__FUNCTION__);
	                return isconnected;
	            }
                    pJson = ParseJsonStr( (char *)DwnLoc.pvOut );
                    if( pJson != NULL )
                    {
                        pItem = GetJsonItem( pJson, "result" );
                        res_val = GetJsonItem( pItem, "status" );
                        if( pItem != NULL )
                        {
                            strncpy(status, res_val->valuestring, sizeof(status));
                            SWLOG_INFO("%s :: status = %s\n",__FUNCTION__, status);
                            if (strcmp(status, "NO_INTERNET") != 0) {
                                isconnected = true;
                                SWLOG_INFO("%s :: Reached ipv6 check\n",__FUNCTION__);
                            }
                        }
                    }
                }
                SWLOG_INFO("%s :: isconnected status = %d\n",__FUNCTION__,isconnected);
            }
        FreeJson( pJson );
        }
        if( DwnLoc.pvOut != NULL )
        {
            free( DwnLoc.pvOut );
        }
    }
    return isconnected;
}

#else

// Do nothing act as pass through function .
// Iarm eventing is not the main purpose of the code download module
void eventManager(const char *cur_event_name, const char *event_status) {
    return ;
}

int term_event_handler(void)
{
    return 0;
}
int init_event_handler(void)
{
    return 0;
}
bool isConnectedToInternet (void)
{
    return true;
}
#endif
