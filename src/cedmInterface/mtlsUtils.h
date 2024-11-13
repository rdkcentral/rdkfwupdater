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

#ifndef VIDEO_CORE_MTLSUTILS_H_
#define VIDEO_CORE_MTLSUTILS_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "device_status_helper.h"
#include "rdkv_cdl_log_wrapper.h"
#ifndef GTEST_ENABLE
#include "system_utils.h"
#include "urlHelper.h"
#include "rdkcertselector.h"
//#include <secure_wrapper.h>
#endif

// Below macro is invoked if the getMtlscert API fails to retrieve all MTLS certificates.
#define CURL_MTLS_LOCAL_CERTPROBLEM 58

typedef enum {
    STATE_RED_CERT_FETCH_FAILURE = -2,     // Indicates failure in state red recovery
    MTLS_CERT_FETCH_FAILURE = -1,          // Indicates general MTLS failure
    MTLS_CERT_FETCH_SUCCESS = 0            // Indicates success
} MtlsAuthStatus;

// Below both Macro should be filled with proper value
#define RDKSSACLI                       "GetKey %s"
#define GETCONFIGFILE_STATERED          "GetConfigFile"
#define DAC15DEFAULT        "URL"
#define CIXCONFDEFAULT      "configurl"
#define DEVXCONFDEFAULT     "defaulturl"
#define XCONFDEFAULT        "xconf"

/*typedef struct credential {
        char cert_name[64];
        char cert_type[16];
        char key_pas[32];
}MtlsAuth_t;*/

MtlsAuthStatus getMtlscert(MtlsAuth_t *sec, rdkcertselector_h* pthisCertSel);


#endif /* VIDEO_CORE_MTLSUTILS_H_ */
