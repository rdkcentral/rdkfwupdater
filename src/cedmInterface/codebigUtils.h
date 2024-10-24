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

#ifndef  _CODEBIG_DL_H_
#define  _CODEBIG_DL_H_

#define MAX_DIR_LEN     256
#define MAX_HEADER_LEN  512
#define MAX_FMT_LEN     32

#define INVALID_SERVICE 0
#define SSR_SERVICE     1
#define XCONF_SERVICE   2
#define CIXCONF_SERVICE 4
#define DAC15_SERVICE   14

int doCodeBigSigning(int server_type, const char* SignInput, char *signurl, size_t signurlsize, char *outhheader, size_t outHeaderSize);
#endif
