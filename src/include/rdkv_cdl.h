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

#ifndef  _RDKV_CDL_H_
#define  _RDKV_CDL_H_

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

/* Forward declarations for types that may not be available in all contexts */
typedef struct filedwnl FileDwnl_t;
typedef struct credential MtlsAuth_t;
#include <inttypes.h>
#include <ctype.h>

#include <stdbool.h>

#ifndef GTEST_ENABLE
#ifdef T2_EVENT_ENABLED
#include <telemetry_busmessage_sender.h>
#endif
#include <secure_wrapper.h>
#include "urlHelper.h"
#endif

#include "json_process.h"

#define SUCCESS 1
#define FAILURE -1

#define RDKV_FW_UPGRADER "RdkvFwUpgrader"

#define CURL_PID_FILE "/tmp/.curl.pid"
#define FWDNLD_PID_FILE "/tmp/.fwdnld.pid"

#ifndef GTEST_ENABLE
    #define STATE_RED_SPRT_FILE "/lib/rdk/stateRedRecovery.sh"
    #define STATEREDFLAG "/tmp/stateRedEnabled"
#else
    #define STATE_RED_SPRT_FILE "/tmp/stateSupport"
    #define STATEREDFLAG "/tmp/stateRedEnabled"
#endif
#define DIRECT_BLOCK_FILENAME "/tmp/.lastdirectfail_cdl"
#define CB_BLOCK_FILENAME "/tmp/.lastcodebigfail_cdl"
#define HTTP_CDL_FLAG "/tmp/device_initiated_rcdl_in_progress"
#define SNMP_CDL_FLAG "/tmp/device_initiated_snmp_cdl_in_progress"
#define ECM_CDL_FLAG "/tmp/ecm_initiated_cdl_in_progress"
#define EnableOCSPStapling "/tmp/.EnableOCSPStapling"
#define EnableOCSP "/tmp/.EnableOCSPCA"
#define PREVIOUS_FLASHED_IMAGE "/opt/previous_flashed_file_name"
#define CURRENTLY_RUNNING_IMAGE "/tmp/currently_running_image_name"
#define CDL_FLASHED_IMAGE "/opt/cdl_flashed_file_name"
#define CB_RETRY_COUNT 1
#define RETRY_COUNT 2
#define HTTP_SSR_DIRECT 0
#define HTTP_SSR_CODEBIG 1
#define HTTP_XCONF_DIRECT 2
#define HTTP_XCONF_CODEBIG 3
#define HTTP_UNKNOWN 5
#define PCI_UPGRADE 0
#define PDRI_UPGRADE 1
#define XCONF_UPGRADE 2
#define PERIPHERAL_UPGRADE  3

#define INITIAL_VALIDATION_SUCCESS 0
#define INITIAL_VALIDATION_FAIL 1
#define INITIAL_VALIDATION_DWNL_INPROGRESS 2
#define INITIAL_VALIDATION_DWNL_COMPLETED 3

//File containing common firmware download state variables
#define STATUS_FILE "/opt/fwdnldstatus.txt"

#define MAX_BUFF_SIZE 512
#define MAX_BUFF_SIZE1 256
#define MIN_BUFF_SIZE 64
#define MIN_BUFF_SIZE1 32
#define SMALL_SIZE_BUFF 8
#define DWNL_PATH_FILE_LEN 128

//TLS values and timeouts
//#define CURL_TLS_TIMEOUT 7200L
#define TLS "--tlsv1.2"

#define FILE_CONTENT_LEN "/tmp/.chunk_download_curl_headers"
#define DIFDPID "/tmp/DIFD.pid"

#define HTTP_CODE_FILE "/opt/xconf_curl_httpcode"
#define CURL_PROGRESS_FILE "/opt/curl_progress"
#define DWNL_URL_VALUE "/opt/.dnldURL"
#define RED_STATE_REBOOT        "/opt/red_state_reboot"

#define IARM_EVENT_BINARY_LOCATION "/usr/bin/IARM_event_sender"
#define IMG_DWL_EVENT "ImageDwldEvent"
#define FW_STATE_EVENT "FirmwareStateEvent"
#define RED_STATE_EVENT "RedStateEvent"

#define HTTP_SUCCESS 200
#define HTTP_CHUNK_SUCCESS 206
#define HTTP_PAGE_NOT_FOUND 404
#define CURL_SUCCESS 0
#define CURL_COULDNT_RESOLVE_HOST 6
#define CURL_CONNECTIVITY_ISSUE 7
#define CURLTIMEOUT 28
#define CURL_LOW_BANDWIDTH 18
#define CURL_RECV_ERROR 56
#define CHUNK_DWNL_ENABLE 1
#define DWNL_BLOCK -2
#define REBOOT_PENDING_DELAY "2"
#define CODEBIG_SIGNING_FAILED -7

#define RDKV_FWDNLD_UNINITIALIZED 0
#define RDKV_FWDNLD_DOWNLOAD_INIT 1
#define RDKV_FWDNLD_DOWNLOAD_INPROGRESS 2
#define RDKV_FWDNLD_DOWNLOAD_EXIT 3
#define RDKV_FWDNLD_DOWNLOAD_COMPLETE 4
#define RDKV_FWDNLD_DOWNLOAD_FAILED 5
#define RDKV_FWDNLD_FLASH_INPROGRESS 6
#define RDKV_FWDNLD_FLASH_COMPLETE 7
#define RDKV_FWDNLD_FLASH_FAILED 8
#define RDKV_XCONF_FWDNLD_DOWNLOAD_INIT 9
#define RDKV_XCONF_FWDNLD_DOWNLOAD_INPROGRESS 10
#define RDKV_XCONF_FWDNLD_DOWNLOAD_EXIT 12
#define RDKV_XCONF_FWDNLD_DOWNLOAD_COMPLETE 13
#define RDKV_XCONF_FWDNLD_DOWNLOAD_FAILED 14

#define APP_BACKGROUND 0
#define APP_FOREGROUND 1

int chunkDownload(FileDwnl_t *pfile_dwnl, MtlsAuth_t *sec, unsigned int speed_limit, int *httpcode);
void uninitialize(int);
int initialize(void);
int logFileData(const char *file_path);
int checkTriggerUpgrade(XCONFRES *pResponse, const char *model);
int flashImage(const char *server_url, const char *upgrade_file, const char *reboot_flag, const char *proto, int upgrade_type, const char *maint,int trigger_type);
int postFlash(const char *maint, const char *upgrade_file, int upgrade_type, const char *reboot_flag,int trigger_type);
void updateUpgradeFlag(int action);
void t2CountNotify(char *marker, int val);
void t2ValNotify(char *marker, char *val);
void setAppMode(int mode);
int getAppMode(void);
void setDwnlState(int state);
int getDwnlState(void);
int startFactoryProtectService(void);
int initialValidation(void);

#endif
