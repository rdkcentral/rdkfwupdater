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

#include "mtlsUtils.h"
#include "deviceutils.h"
#ifdef LIBRDKCONFIG_BUILD
#include "rdkconfig.h"
#endif

/* Description: Use for get all mtls related certificate and key.
 * @param sec: This is a pointer hold the certificate, key and type of certificate.
 * @return : int Success 1 and failure -1
 * */
int getMtlscert(MtlsAuth_t *sec) {
	/*
            strncpy(sec->cert_name, STATE_RED_CERT, sizeof(sec->cert_name) - 1);
	    sec->cert_name[sizeof(sec->cert_name) - 1] = '\0';
            strncpy(sec->cert_type, "P12", sizeof(sec->cert_type) - 1);
	    sec->cert_type[sizeof(sec->cert_type) - 1] = '\0';
            strncpy(sec->key_pas, mtlsbuff, sizeof(sec->key_pas) - 1);
            sec->key_pas[sizeof(sec->key_pas) - 1] = '\0';
	*/
    return MTLS_SUCCESS;
}
