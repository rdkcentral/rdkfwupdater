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

#include "iarm_interface_mock.h"

#include <stdio.h>
#include <string.h>

#define IARM_INTERFACE_MOCK_PDRI_MAX_LEN 256

static char g_pdri_filename[IARM_INTERFACE_MOCK_PDRI_MAX_LEN];

void iarm_interface_mock_set_pdri_filename(const char *value)
{
    if (value == NULL) {
        g_pdri_filename[0] = '\0';
        return;
    }

    snprintf(g_pdri_filename, sizeof(g_pdri_filename), "%s", value);
}

void iarm_interface_mock_reset(void)
{
    g_pdri_filename[0] = '\0';
}

size_t GetPDRIFileNameUsingMFR(char *pPDRIFilename, size_t szBufSize)
{
    size_t len;

    if (pPDRIFilename == NULL || szBufSize == 0 || g_pdri_filename[0] == '\0') {
        return 0;
    }

    len = strlen(g_pdri_filename);
    if (len >= szBufSize) {
        return 0;
    }

    memcpy(pPDRIFilename, g_pdri_filename, len + 1);
    return len;
}