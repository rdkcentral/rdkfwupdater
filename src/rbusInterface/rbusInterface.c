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

#include "rbusInterface.h"

rbusError_t invokeRbusDCMReport()
{
    rbusHandle_t rdkfwRbusHandle;
    rbusObject_t inParams;
    rbusObject_t outParams;
    rbusObject_Init(&inParams, NULL);
    if (RBUS_ERROR_SUCCESS == rbus_open(&rdkfwRbusHandle, RDKFWUPGRADER_RBUS_HANDLE_NAME)) {
	if (RBUS_ERROR_SUCCESS != rbusMethod_Invoke(rdkfwRbusHandle, T2_UPLOAD, inParams, &outParams)) {
            SWLOG_ERROR("Error in calling Device.X_RDKCENTRAL-COM_T2.UploadDCMReport\n");
	    rbusObject_Release(inParams);
	    return RBUS_ERROR_BUS_ERROR;
	}
        else {
	    rbusProperty_t outProps = rbusObject_GetProperties(outParams);
	    rbusValue_t value;
	    if (outProps) {
		value = rbusProperty_GetValue(outProps);
		SWLOG_INFO("Device.X_RDKCENTRAL-COM_T2.UploadDCMReport Upload Status = %s\n", rbusValue_GetString(value, NULL));
	    }
	    else {
		SWLOG_ERROR("Failed to retrieve properties of Device.X_RDKCENTRAL-COM_T2.UploadDCMReport response\n");
		rbusObject_Release(inParams);
		rbusObject_Release(outParams);
		return RBUS_ERROR_BUS_ERROR;
	    }
	}
	rbusObject_Release(inParams);
	rbusObject_Release(outParams);
    }
    else {
	SWLOG_ERROR("Error in opening rbus handle\n");
	rbusObject_Release(inParams);
        return RBUS_ERROR_BUS_ERROR;
    }
    if (RBUS_ERROR_SUCCESS != rbus_close(rdkfwRbusHandle)) {
        SWLOG_ERROR("Rbus termination failed\n");
	return RBUS_ERROR_BUS_ERROR;
    }
    return RBUS_ERROR_SUCCESS;
}
#endif
