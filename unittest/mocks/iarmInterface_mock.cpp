/*
 * Copyright 2023 Comcast Cable Communications Management, LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * You may not use this file except in compliance with the License.
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

#include "iarmInterface_mock.h"
#include <iostream>
#include <cstring>

using namespace std;

// A simple global pointer for mocking; in real use you can extend this with gmock if desired
IarmInterfaceMock *g_IarmInterfaceMock = nullptr;

extern "C" size_t GetPDRIFileNameUsingMFR(char *pPDRIFilename, size_t szBufSize)
{
    if (!g_IarmInterfaceMock) {
        cout << "GetPDRIFileNameUsingMFR g_IarmInterfaceMock object is NULL" << endl;
        return 0;
    }
    printf("Inside Mock Function GetPDRIFileNameUsingMFR\n");
    // Give a fake file name for tests unless you want to do more in your mock object
    const char *mockPDRI = "mock-PDRI-image.bin";
    size_t len = strlen(mockPDRI);
    if (pPDRIFilename && szBufSize > len) {
        strncpy(pPDRIFilename, mockPDRI, szBufSize);
        pPDRIFilename[szBufSize - 1] = '\0';
        return len;
    }
    return g_IarmInterfaceMock->GetPDRIFileNameUsingMFR(pPDRIFilename, szBufSize);
}
