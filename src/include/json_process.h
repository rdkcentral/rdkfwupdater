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

#ifndef _JSON_PROCESS_H_
#define _JSON_PROCESS_H_

#define CLD_URL_MAX_LEN 512

typedef struct xconf_response {
	char cloudFWFile[128];
	char cloudFWLocation[CLD_URL_MAX_LEN];
	char ipv6cloudFWLocation[CLD_URL_MAX_LEN];//TODO: Need to check
	char cloudFWVersion[64];
	char cloudDelayDownload[8];
	char cloudProto[6];
	char cloudImmediateRebootFlag[12];
	char peripheralFirmwares[256];
	char dlCertBundle[64];
	char cloudPDRIVersion[64];
    char rdmCatalogueVersion[512];
} XCONFRES;

int processJsonResponse(XCONFRES *response, const char *myfwversion, const char *model, const char *maint);
int getXconfRespData(XCONFRES *pResponse, char *pJsonStr);
size_t createJsonString(char *pPostFieldOut, size_t szPostFieldOut);

#endif
