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

#ifndef GTEST_ENABLE
#include <unistd.h>
#include "rbusInterface.h"
#else
#include "rbus_mock.h"
#include <stddef.h>
#endif
/* Description:Use for Rbus callback
 * @param: handle : rbus handle
 * @param: methodName : Method name
 * @param: error : rbus return value type
 * @param: param : rbus object
 * @return void : NA
 * */
static void t2EventHandler(rbusHandle_t handle, char const* methodName, rbusError_t error, rbusObject_t param)
{
    SWLOG_INFO("Got %s rbus callback\n", methodName);
    if (RBUS_ERROR_SUCCESS == error)
    {
        rbusValue_t uploadStatus = rbusObject_GetValue(param, "UPLOAD_STATUS");
        if(uploadStatus)
        {
            SWLOG_INFO("Device.X_RDKCENTRAL-COM_T2.UploadDCMReport Upload Status = %s\n", rbusValue_GetString(uploadStatus, NULL));
        }
    }
}

/* Description: Trigger T2 upload
 * @return rbusError_t : Return SUCCESS/FAILURE
 * */
rbusError_t invokeRbusDCMReport()
{
    rbusHandle_t rdkfwRbusHandle;
    if (RBUS_ERROR_SUCCESS == rbus_open(&rdkfwRbusHandle, RDKFWUPGRADER_RBUS_HANDLE_NAME)) {
        if (RBUS_ERROR_SUCCESS == rbusMethod_InvokeAsync(rdkfwRbusHandle, T2_UPLOAD, NULL, t2EventHandler, 0)) {
            SWLOG_INFO("Waiting 60 sec to complete upload from Device.X_RDKCENTRAL-COM_T2.UploadDCMReport\n");
            sleep(60);
        }
        else {
            SWLOG_ERROR("Error in calling Device.X_RDKCENTRAL-COM_T2.UploadDCMReport\n");
	    return RBUS_ERROR_BUS_ERROR;
        }
    }
    else {
	SWLOG_ERROR("Error in opening rbus handle\n");
        return RBUS_ERROR_BUS_ERROR;
    }
    if (RBUS_ERROR_SUCCESS != rbus_close(rdkfwRbusHandle)) {
        SWLOG_ERROR("Rbus termination failed\n");
	return RBUS_ERROR_BUS_ERROR;
    }
    return RBUS_ERROR_SUCCESS;
}

