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

#ifndef MOCK_RBUS_H
#define MOCK_RBUS_H

#include "../rdkv_cdl_log_wrapper.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RBUS_ERROR_SUCCESS 0
#define RBUS_ERROR_BUS_ERROR 1

#define RDKFWUPGRADER_RBUS_HANDLE_NAME "rdkfwRbus"
#define T2_UPLOAD "Device.X_RDKCENTRAL-COM_T2.UploadDCMReport"

typedef int rbusError_t;

struct _rbusHandle
{
};

typedef struct _rbusHandle *rbusHandle_t;

struct _rbusObject
{
};
typedef struct _rbusObject *rbusObject_t;

struct _rbusValue
{
};
typedef struct _rbusValue *rbusValue_t;

typedef void (*rbusMethodAsyncRespHandler_t)(void);

//typedef void (*rbusMethodAsyncRespHandler_t)(rbusHandle_t handle, char const *methodName, rbusError_t error, rbusObject_t params);

rbusError_t rbus_open(rbusHandle_t *handle, const char* componentName);
rbusError_t rbus_close(rbusHandle_t handle);
rbusError_t rbusMethod_InvokeAsync(rbusHandle_t handle, const char* method, rbusObject_t input, rbusMethodAsyncRespHandler_t handler, int timeout);
rbusValue_t rbusObject_GetValue(rbusObject_t obj, const char* name);

#ifdef __cplusplus
}
#endif

#endif  // MOCK_RBUS_H
