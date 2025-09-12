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

#ifndef VIDEO_DOWNLOAD_STATUS_HELPER_H_
#define VIDEO_DOWNLOAD_STATUS_HELPER_H_

#include "rdkv_cdl.h"
#include "../rfcInterface/rfcinterface.h"
#define MIN_BUFF_SIZE2 MIN_BUFF_SIZE1 + 40
#define MIN_BUFF_SIZE3 MIN_BUFF_SIZE + 64

struct FWDownloadStatus {
    char method[MIN_BUFF_SIZE1];
    char proto[MIN_BUFF_SIZE1];
#if __WORDSIZE == 64
    char status[BUFF_SIZE];
#else
    char status[MIN_BUFF_SIZE2];
#endif
    char reboot[MIN_BUFF_SIZE1];
#if __WORDSIZE == 64
    char failureReason[MAX_BUFF_SIZE1];
#else
    char failureReason[MIN_BUFF_SIZE3];
#endif
    char dnldVersn[MAX_BUFF_SIZE1];
    char dnldfile[MAX_BUFF_SIZE1];
    char dnldurl[MAX_BUFF_SIZE];
    char lastrun[MAX_BUFF_SIZE1];
    char FwUpdateState[MIN_BUFF_SIZE];
    char DelayDownload[MIN_BUFF_SIZE1];
};


int updateFWDownloadStatus(struct FWDownloadStatus *fwdls, const char *disableStatsUpdate) ;
int notifyDwnlStatus(const char *key, const char *value, RFCVALDATATYPE datatype);

#endif /* VIDEO_DOWNLOAD_STATUS_HELPER_H_ */
