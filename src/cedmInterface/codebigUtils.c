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

#include "rdkv_cdl.h"
#include "rdkv_cdl_log_wrapper.h"
#include "codebigUtils.h"
//#include <safec_lib.h>
#include "rfcinterface.h"
#include "rdk_fwdl_utils.h"
#include "device_api.h"

// DEBUG_CODEBIG_CDL can be turned on with distro feature debug_codebig_cdl

#define URL_DELIMITER   '?'

//static const char Sign_Cmd[] = "GetServiceUrl"; //$request_type \"$imagedownloadHTTPURL\"";
//static char respbuf[BIG_BUF_LEN];
//static char location[MAX_HEADER_LEN];
//static char fmt[MAX_FMT_LEN];        // values in sscanf need to be 1 less than buffer size


/* function doCodeBigSigning - creates an authorization signature and finds the Codebig URL to use for Codebig communication. 
        Usage: int doCodeBigSigning <int server_type> <const char *SignInput> <char *signurl> <size_t signurlsize> <char *outhheader> <size_t outHeaderSize>
 
            server_type - HTTP_XCONF_CODEBIG if XCONF signing, defaults to CDL server otherwise

            SignInput - pointer to character string to create signature for.

            signurl - a character buffer to store the Codebig URL output string.
 
            signurlsize - the signurl buffer maximum length in bytes.
 
            outhheader - a character buffer to store the authorization signature output.
 
            outHeaderSize - the outhheader buffer maximum length in bytes.

            RETURN - number of characters copied to the output buffer.
*/

int doCodeBigSigning( int server_type, const char *SignInput, char *signurl, size_t signurlsize, char *outhheader, size_t outHeaderSize )
{
    return 1;
}
