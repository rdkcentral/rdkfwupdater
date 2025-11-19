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

#ifndef _MISCELLANEOUS_H_
#define _MISCELLANEOUS_H_
//#include "urlHelper.h"
//#include "common_device_api.h"
//#include "rdkv_cdl_log_wrapper.h"

#define TLS_LOG_FILE "/opt/logs/tlsError.log"
#define DEBUG_INI_NAME  "/etc/debug.ini"

#define TLS_LOG_ERR      (1)
#define TLS_LOG_WARN     (2)
#define TLS_LOG_INFO     (3)
#define tls_debug_level (3)


#define TLSLOG(level, ...) do {  \
                                    if (level == TLS_LOG_ERR) { \
                                        printf("ERROR: %s:%d:", __FILE__, __LINE__); \
                                    } else if (level == TLS_LOG_INFO) { \
                                        printf("INFO: %s:%d:", __FILE__, __LINE__); \
                                    } else { \
                                        printf("DBG: %s:%d:", __FILE__, __LINE__); \
                                    }\
                                printf(__VA_ARGS__); \
                                printf( "\n"); \
                        } while (0)

#define DWNL_FAIL -1
#define DWNL_SUCCESS 1
#define DWNL_UNPAUSE_FAIL -2

/* Below structure contains data from /etc/device.property */

typedef struct deviceproperty {
        BUILDTYPE eBuildType;           // keep buildtype as an enum, easier to compare
        char dev_name[MIN_BUFF_SIZE1];
        char dev_type[MIN_BUFF_SIZE1];
        char difw_path[MIN_BUFF_SIZE1];
        char log_path[MIN_BUFF_SIZE1];
        char persistent_path[MIN_BUFF_SIZE1];
        char maint_status[MIN_BUFF_SIZE1];
        char mtls[MIN_BUFF_SIZE1];
        char model[MIN_BUFF_SIZE1];
        char sw_optout[MIN_BUFF_SIZE1];
}DeviceProperty_t;



typedef enum {
    T2ERROR_SUCCESS,
    T2ERROR_FAILURE
} T2ERROR;


/* Below structure contails data from /version.txt */

typedef struct imagedetails {
        char cur_img_name[MIN_BUFF_SIZE];
}ImageDetails_t;

// define the functions to avoid compiler errors

#endif
