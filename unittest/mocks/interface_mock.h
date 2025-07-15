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

#ifndef INTERFACE_MOCK
#define INTERFACE_MOCK
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "rdkv_cdl_log_wrapper.h"

extern "C" {
#include "rfcinterface.h"
}
/* --------- RBUS MACROS ------------*/
typedef enum _rbusError
{
    RBUS_ERROR_SUCCESS,
    RBUS_ERROR_NOT_INITIALIZED,
    RBUS_ERROR_BUS_ERROR,
} rbusError_t;

char const * rbusError_ToString(rbusError_t e);

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

typedef void (*rbusMethodAsyncRespHandler_t)(rbusHandle_t handle, char const *methodName, rbusError_t error, rbusObject_t params);


/* ------------------- RBUS Impl--------------- */
class RBusApiInterface
{
public:
    virtual ~RBusApiInterface() = default;
    virtual rbusError_t rbus_open(rbusHandle_t *handle, char const *componentName) = 0;
    virtual rbusError_t rbus_close(rbusHandle_t handle) = 0;
    virtual rbusError_t rbusValue_Init(rbusValue_t *value) = 0;
    virtual rbusError_t rbusValue_SetString(rbusValue_t value, char const *str) = 0;
    virtual rbusError_t rbus_set(rbusHandle_t handle, char const *objectName, rbusValue_t value, rbusMethodAsyncRespHandler_t respHandler) = 0;
};

class RBusApiWrapper
{
protected:
    static RBusApiInterface *impl;

public:
    RBusApiWrapper();
    static void setImpl(RBusApiInterface *newImpl);
    static void clearImpl();
    static rbusError_t rbus_open(rbusHandle_t *handle, char const *componentName);
    static rbusError_t rbus_close(rbusHandle_t handle);
    static rbusError_t rbusValue_Init(rbusValue_t *value);
    static rbusError_t rbusValue_SetString(rbusValue_t value, char const *str);
    static rbusError_t rbus_set(rbusHandle_t handle, char const *objectName, rbusValue_t value, rbusMethodAsyncRespHandler_t respHandler);
};

extern rbusError_t (*rbus_open)(rbusHandle_t *, char const *);
extern rbusError_t (*rbus_close)(rbusHandle_t);
extern rbusError_t (*rbusValue_Init)(rbusValue_t *);
extern rbusError_t (*rbusValue_SetString)(rbusValue_t, char const *);
extern rbusError_t (*rbus_set)(rbusHandle_t, char const *, rbusValue_t, rbusMethodAsyncRespHandler_t);

class MockRBusApi : public RBusApiInterface
{
public:
    MOCK_METHOD2(rbus_open, rbusError_t(rbusHandle_t *, char const *));
    MOCK_METHOD1(rbus_close, rbusError_t(rbusHandle_t));
    MOCK_METHOD1(rbusValue_Init, rbusError_t(rbusValue_t *));
    MOCK_METHOD2(rbusValue_SetString, rbusError_t(rbusValue_t, char const *));
    MOCK_METHOD4(rbus_set, rbusError_t(rbusHandle_t, char const *, rbusValue_t, rbusMethodAsyncRespHandler_t));
};

class FwDlInterface
{
    public:
        virtual ~FwDlInterface() {}
        virtual int filePresentCheck(const char *filename) = 0;
        virtual int getRFCParameter(char* type, const char* key, RFC_ParamData_t *param) = 0;
        virtual int setRFCParameter(char* type, const char* key, const char *value, int datatype) = 0;
        virtual int getDevicePropertyData(const char *model, char *data, int size) = 0;
        virtual char* getRFCErrorString(int status) = 0;
        virtual int getAppMode() = 0;
        virtual int interuptDwnl(int val) = 0;
        virtual int IARM_Bus_IsConnected(const char *str, int *val) = 0;
        virtual int IARM_Bus_BroadcastEvent(const char *name, int val, void *p, int size) = 0;
        virtual int IARM_Bus_Init(const char *name) = 0;
        virtual int IARM_Bus_RegisterEventHandler(const char *name, int mode, void *fun) = 0;
        virtual int IARM_Bus_Connect() = 0;
        virtual int IARM_Bus_UnRegisterEventHandler(const char *name, int mode) = 0;
        virtual int IARM_Bus_Disconnect() = 0;
        virtual int IARM_Bus_Term() = 0;
        virtual int MemDLAlloc(void *ptr, int size) = 0;
        virtual int getJsonRpc(char *data, void *ptr) = 0;
};

class FwDlInterfaceMock: public FwDlInterface
{
    public:
        virtual ~FwDlInterfaceMock() {}
        MOCK_METHOD(int, filePresentCheck, (const char *filename ), ());
        MOCK_METHOD(int, getRFCParameter, (char* type, const char* key, RFC_ParamData_t *param), ());
        MOCK_METHOD(int, setRFCParameter, (char* type, const char* key, const char *value, int datatype), ());
        MOCK_METHOD(int, getDevicePropertyData, (const char *model, char *data, int size), ());
        MOCK_METHOD(char*, getRFCErrorString, (int status), ());
        MOCK_METHOD(int, getAppMode, (), ());
        MOCK_METHOD(int, interuptDwnl, (int val), ());
        MOCK_METHOD(int, IARM_Bus_IsConnected, (const char *str, int *val), ());
        MOCK_METHOD(int, IARM_Bus_BroadcastEvent, (const char *name, int val, void *p, int size), ());
        MOCK_METHOD(int, IARM_Bus_Init, (const char *name), ());
        MOCK_METHOD(int, IARM_Bus_RegisterEventHandler, (const char *name, int mode, void *fun), ());
        MOCK_METHOD(int, IARM_Bus_Connect, (), ());
        MOCK_METHOD(int, IARM_Bus_UnRegisterEventHandler, (const char *name, int mode), ());
        MOCK_METHOD(int, IARM_Bus_Disconnect, (), ());
        MOCK_METHOD(int, IARM_Bus_Term, (), ());
        MOCK_METHOD(int, MemDLAlloc, (void *ptr, int size), ());
        MOCK_METHOD(int, getJsonRpc, (char *data, void *ptr), ());
};

#endif
