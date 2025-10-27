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

#include "interface_mock.h"
#include <iostream>

using namespace std;

extern FwDlInterfaceMock *g_InterfaceMock;

extern "C" int getRFCParameter(char* type, const char* key, RFC_ParamData_t *param)
{
    if (!g_InterfaceMock)
    {
        cout << "getRFCParameter g_InterfaceMock object is NULL" << endl;
        return 0;
    }
    printf("Inside Mock Function getRFCParameter\n");
    snprintf(param->value,sizeof(param->value), "%s", "true");
    snprintf(param->name,sizeof(param->name), "%s", "rfc");
    param->type = 1;
    param->status = 1;
    return g_InterfaceMock->getRFCParameter(type,key,param);
}

extern "C" int setRFCParameter(char* type, const char* key, const char *value, int datatype)
{
    if (!g_InterfaceMock)
    {
        cout << "setRFCParametere g_InterfaceMock object is NULL" << endl;
        return 0;
    }
    printf("Inside Mock Function setRFCParameter\n");
    return g_InterfaceMock->setRFCParameter(type,key,value,datatype);
}

extern "C" int filePresentCheck(const char *filename)
{
    if (!g_InterfaceMock)
    {
        cout << "filePresentCheck g_InterfaceMock object is NULL" << endl;
        return 0;
    }
    printf("Inside Mock Function filePresentCheck\n");
    return g_InterfaceMock->filePresentCheck(filename);
}
extern "C" int getDevicePropertyData(const char *model, char *data, int size)
{
    if (!g_InterfaceMock)
    {
        cout << "g_InterfaceMock object is NULL" << endl;
        return -1;
    }
    printf("Inside Mock Function getDevicePropertyData\n");
    if (0 == (strncmp(model, "CPU_ARCH", 8))) {
        snprintf(data, size, "%s", "X86");
    } else if (0 == (strncmp(model, "DEVICE_NAME", 11))) {
        snprintf(data, size, "%s", "PLATCO");
    } else if (0 == (strncmp(model, "PDRI_ENABLED", 12))){
        snprintf(data, size, "%s", "true");
    }
    return g_InterfaceMock->getDevicePropertyData(model, data, size);
}
extern "C" char* getRFCErrorString(int status)
{
    if (!g_InterfaceMock)
    {
        cout << "g_InterfaceMock object is NULL" << endl;
        return -1;
    }
    printf("Inside Mock Function getRFCErrorString\n");
    return g_InterfaceMock->getRFCErrorString(status);
}
extern "C" int getAppMode()
{
    if (!g_InterfaceMock)
    {
        cout << "g_InterfaceMock object is NULL" << endl;
        return -1;
    }
    printf("Inside Mock Function getAppMode\n");
    return g_InterfaceMock->getAppMode();
}
extern "C" int interuptDwnl(int val)
{
    if (!g_InterfaceMock)
    {
        cout << "g_InterfaceMock object is NULL" << endl;
        return -1;
    }
    printf("Inside Mock Function interuptDwnl\n");
    return g_InterfaceMock->interuptDwnl(val);
}
extern "C" int IARM_Bus_IsConnected(const char *str, int *val)
{
    if (!g_InterfaceMock)
    {
        cout << "g_InterfaceMock object is NULL" << endl;
        return -1;
    }
    printf("Inside Mock Function IARM_Bus_IsConnected\n");
    return g_InterfaceMock->IARM_Bus_IsConnected(str, val);
}
extern "C" int IARM_Bus_BroadcastEvent(const char *name, int val, void *p, int size)
{
    if (!g_InterfaceMock)
    {
        cout << "g_InterfaceMock object is NULL" << endl;
        return -1;
    }
    printf("Inside Mock Function IARM_Bus_BroadcastEvent\n");
    return g_InterfaceMock->IARM_Bus_BroadcastEvent(name, val, p, size);
}
extern "C" int IARM_Bus_Init(const char *name)
{
    if (!g_InterfaceMock)
    {
        cout << "g_InterfaceMock object is NULL" << endl;
        return -1;
    }
    printf("Inside Mock Function IARM_Bus_Init\n");
    return g_InterfaceMock->IARM_Bus_Init(name);
}
extern "C" int IARM_Bus_RegisterEventHandler(const char *name, int mode, void *fun)
{
    if (!g_InterfaceMock)
    {
        cout << "g_InterfaceMock object is NULL" << endl;
        return -1;
    }
    printf("Inside Mock Function IARM_Bus_RegisterEventHandler\n");
    return g_InterfaceMock->IARM_Bus_RegisterEventHandler(name, mode, fun);
}
extern "C" int IARM_Bus_Connect()
{
    if (!g_InterfaceMock)
    {
        cout << "g_InterfaceMock object is NULL" << endl;
        return -1;
    }
    printf("Inside Mock Function IARM_Bus_Connect\n");
    return g_InterfaceMock->IARM_Bus_Connect();
}
extern "C" int IARM_Bus_UnRegisterEventHandler(const char *name, int mode)
{
    if (!g_InterfaceMock)
    {
        cout << "g_InterfaceMock object is NULL" << endl;
        return -1;
    }
    printf("Inside Mock Function IARM_Bus_UnRegisterEventHandler\n");
    return g_InterfaceMock->IARM_Bus_UnRegisterEventHandler(name, mode);
}
extern "C" int IARM_Bus_Disconnect()
{
    if (!g_InterfaceMock)
    {
        cout << "g_InterfaceMock object is NULL" << endl;
        return -1;
    }
    printf("Inside Mock Function IARM_Bus_Disconnect\n");
    return g_InterfaceMock->IARM_Bus_Disconnect();
}
extern "C" int IARM_Bus_Term()
{
    if (!g_InterfaceMock)
    {
        cout << "g_InterfaceMock object is NULL" << endl;
        return -1;
    }
    printf("Inside Mock Function IARM_Bus_Term\n");
    return g_InterfaceMock->IARM_Bus_Term();
}
/*
extern "C" int allocDowndLoadDataMem(void *ptr, int size)
{
    if (!g_InterfaceMock)
    {
        cout << "g_InterfaceMock object is NULL" << endl;
        return -1;
    }
    printf("Inside Mock Function allocDowndLoadDataMem\n");
    return g_InterfaceMock->allocDowndLoadDataMem(ptr, size);
}
*/
extern "C" int getJsonRpc(char *data, void *ptr)
{
    if (!g_InterfaceMock)
    {
        cout << "g_InterfaceMock object is NULL" << endl;
        return -1;
    }
    printf("Inside Mock Function getJsonRpc\n");
    return g_InterfaceMock->getJsonRpc(data, ptr);
}
