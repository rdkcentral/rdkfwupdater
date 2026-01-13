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

#ifndef  _RDKV_CDL_LOG_WRPPER_H_
#define  _RDKV_CDL_LOG_WRPPER_H_

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

// Use system headers for struct definitions to avoid redefinition conflicts
//#include "urlHelper.h"

#define SWLOG_INFO(format, ...)        printf(format, ##__VA_ARGS__)
#define SWLOG_ERROR(format, ...)        printf(format, ##__VA_ARGS__)
#define SWLOG_DEBUG(format, ...)        printf(format, ##__VA_ARGS__)
#define SWLOG_WARN(format, ...)         printf(format, ##__VA_ARGS__)
#define SWLOG_FATAL(format, ...)        printf(format, ##__VA_ARGS__)

#define CURL_TLS_TIMEOUT 7200L
#define CURL_PROGRESS_FILE "/opt/curl_progress"

#define MAX_BUFF_SIZE 512
#define MAX_BUFF_SIZE1 256
#define MIN_BUFF_SIZE 64
#define MIN_BUFF_SIZE1 32
#define SMALL_SIZE_BUFF 8
#define URL_MAX_LEN 512
#define DWNL_PATH_FILE_LEN 128
#define BIG_BUF_LEN 1024
#define MAX_DEVICE_PROP_BUFF_SIZE 32

#define DEVICE_PROPERTIES_FILE "/tmp/device_gtest.prop"
#define UTILS_SUCCESS 0
#define UTILS_FAIL -1

// Note: credential, MtlsAuth_t, DownloadData, hashParam_t, FileDwnl_t 
// are now defined in urlHelper.h (included above) to avoid redefinition conflicts

typedef struct credential {                                                                                                                                                                              
        char cert_name[64];                                                                                                                                                                              
        char cert_type[16];                                                                                                                                                                              
        char key_pas[32];                                                                                                                                                                                
}MtlsAuth_t;                                                                                                                                                                                             
                                                                                                                                                                                                         
/* Below structure use for download file data */                                                                                                                                                         
                                                                                                                                                                                                         
typedef struct CommonDownloadData {                                                                                                                                                                      
    void* pvOut;                                                                                                                                                                                         
    size_t datasize;        // data size                                                                                                                                                                 
    size_t memsize;         // allocated memory size (if applicable)                                                                                                                                     
} DownloadData;                                                                                                                                                                                          
                                                                                                                                                                                                         
/* Structure Use for Hash Value and Time*/                                                                                                                                                               
                                                                                                                                                                                                         
typedef struct hashParam {                                                                                                                                                                               
    char *hashvalue;                                                                                                                                                                                     
    char *hashtime;                                                                                                                                                                                      
}hashParam_t;                                                                                                                                                                                            
                                                                                                                                                                                                         
typedef struct filedwnl {                                                                                                                                                                                
        char *pPostFields;                                                                                                                                                                               
        char *pHeaderData;                                                                                                                                                                               
        DownloadData *pDlData;                                                                                                                                                                           
        DownloadData *pDlHeaderData;                                                                                                                                                                     
        int chunk_dwnl_retry_time;                                                                                                                                                                       
        char url[BIG_BUF_LEN];                                                                                                                                                                           
        char pathname[DWNL_PATH_FILE_LEN];                                                                                                                                                               
        bool sslverify;                                                                                                                                                                                  
        hashParam_t *hashData;                                                                                                                                                                           
}FileDwnl_t;                               

typedef enum {
    eUNKNOWN,
    eDEV,
    eVBN,
    ePROD,
    eQA
} BUILDTYPE;

#endif
