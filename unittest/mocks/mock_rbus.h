/*
* If not stated otherwise in this file or this component's LICENSE file the
* following copyright and licenses apply:
*
* Copyright 2020 RDK Management
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
*/
#ifndef MOCK_RBUS_H
#define MOCK_RBUS_H

#include <rbus.h>

class rbusInterface
{
    public:
        virtual ~rbusInterface() {}
	virtual rbusError_t rbus_open(rbusHandle_t*, char const*) = 0;
	virtual rbusError_t rbus_close(rbusHandle_t) = 0;
        virtual rbusValue_t rbusObject_GetValue(rbusObject_t, char const*) = 0;
	virtual rbusError_t rbusMethod_InvokeAsync(rbusHandle_t , char const* , rbusObject_t , rbusMethodAsyncRespHandler_t , int ) = 0;

};

class rbusMock: public rbusInterface
{
    public:
        virtual ~rbusMock() {}
	MOCK_METHOD2(rbus_open, rbusError_t(rbusHandle_t*, char const*));
	MOCK_METHOD1(rbus_close, rbusError_t(rbusHandle_t));
        MOCK_METHOD2(rbusObject_GetValue, rbusValue_t(rbusObject_t, char const*));
	MOCK_METHOD5(rbusMethod_InvokeAsync, rbusError_t(rbusHandle_t,  char const*, rbusObject_t, rbusMethodAsyncRespHandler_t, int));


};  

#endif //MOCK_RBUS_H
