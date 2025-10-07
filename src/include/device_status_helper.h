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

#ifndef VIDEO_DEVICE_STATUS_HELPER_H_
#define VIDEO_DEVICE_STATUS_HELPER_H_

#include <stdbool.h>
#include <time.h>

#define DNS_RESOLV_FILE "/etc/resolv.dnsmasq"
#define IP_ROUTE_FLAG "/tmp/route_available"
#define GATEWAYIP_FILE "/tmp/.GatewayIP_dfltroute"
#define ROUTE_FLAG_MAX_CHECK 5
#define MAINTENANCE_MGR_RECORD_FILE "/opt/maintenance_mgr_record.conf"
//Below file is use for update MAINTENANCE_MGR_RECORD_FILE file data.
#define MAINTENANCE_MGR_RECORD_UPDATE_FILE "/opt/.mm_record_update.conf"
#define FLAG_MAX_CHECK 5

#ifdef GTEST_ENABLE
#define RDK_API_SUCCESS 0
#include <fcntl.h>           /* Definition of AT_* constants */
#include <sys/stat.h>
#endif

bool isDeviceReadyForDownload();
int isStateRedSupported(void);
int isInStateRed(void);
void checkAndEnterStateRed(int curlret, const char *);
int checkVideoStatus(const char *device_name);
int isThrottleEnabled(const char *device_name, const char *reboot_immediate_flag, int app_mode);
int isOCSPEnable(void);
int isCodeBigFallBackEnabled(void);
int isDwnlBlock(int type);
bool isUpgradeInProgress(void);
time_t getCurrentSysTimeSec(void);
bool isDelayFWDownloadActive(int DelayDownloadXconf, const char *maint, int trigger_type);
bool isPDRIEnable(void);
bool checkPDRIUpgrade(const char *dwnl_pdri_img);
bool checkForValidPCIUpgrade(int trigger_type, const char *myfwversion, const char *cloudFWVersion, const char *cloudFWFile);
bool lastDwnlImg(char *img_name, size_t img_name_size);
bool currentImg(char *img_name, size_t img_name_size);
void unsetStateRed(void);
bool GetPDRIVersion(char *pPdriVersion, size_t szBufSize);
bool updateOPTOUTFile(const char *optout_file_name);
bool CheckIProuteConnectivity(const char *file_name);
bool isDnsResolve(const char *dns_file_name);
bool checkCodebigAccess(void);
bool CurrentRunningInst(const char *file);
bool lastDwnlImg(char *img_name, size_t img_name_size);
void waitForNtp(void);
bool prevFlashedFile(char *img_name, size_t img_name_size);
unsigned int getFileLastModifyTime(char *file_name);

#endif /* VIDEO_DEVICE_STATUS_HELPER_H_ */
