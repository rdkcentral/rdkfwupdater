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
#include "rbus_mock.h"
#include <stdio.h>
#include <string.h>

// Simulated global handle
static rbusHandle_t dummyHandle = (rbusHandle_t)0x1234;

// Simulated value for UPLOAD_STATUS
static int uploadStatusValue = 1;

rbusError_t rbus_open(rbusHandle_t* handle, const char* componentName) {
    printf("Mock rbus_open called with name: %s\n", componentName);
    *handle = dummyHandle;
    return RBUS_ERROR_SUCCESS;
}

rbusError_t rbus_close(rbusHandle_t handle) {
    printf("Mock rbus_close called\n");
    return RBUS_ERROR_SUCCESS;
}

rbusError_t rbusMethod_InvokeAsync(rbusHandle_t handle, const char* method, rbusObject_t input, rbusMethodAsyncRespHandler_t handler, int timeout) {
    printf("Mock rbusMethod_InvokeAsync called with method: %s, timeout: %d\n", method, timeout);
    if (handler) {
        handler();  // Simulate callback
    }
    return RBUS_ERROR_SUCCESS;
}

rbusValue_t rbusObject_GetValue(rbusObject_t obj, const char* name) {
    printf("Mock rbusObject_GetValue called with name: %s\n", name);
    if (strcmp(name, "UPLOAD_STATUS") == 0) {
        return (rbusValue_t)&uploadStatusValue;
    }
    return NULL;
}

const char* rbusValue_GetString(rbusValue_t value, void* unused)
{
    (void)unused; // unused parameter

    return "MockedUploadStatus";
}

