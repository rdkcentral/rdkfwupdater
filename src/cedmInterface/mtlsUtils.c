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
#ifndef LIBRDKCERTSELECTOR
#ifdef LIBRDKCONFIG_BUILD
#include "rdkconfig.h"
#endif
#endif

#ifdef LIBRDKCERTSELECTOR
#define FILESCHEME "file://"

/* Description: Use for get all mtls related certificate and key.
 * @param sec: This is a pointer hold the certificate, key and type of certificate.
 * @return : MTLS_CERT_FETCH_SUCCESS on success, MTLS_CERT_FETCH_FAILURE on mtls cert failure , STATE_RED_CERT_FETCH_FAILURE on state red cert failure
 * */
MtlsAuthStatus getMtlscert(MtlsAuth_t *sec, rdkcertselector_h* pthisCertSel) {

    int state_red = 0;
    char *certUri = NULL;
    char *certPass = NULL;
    char *engine = NULL;
    char *certFile = NULL;

    state_red = isInStateRed();
    SWLOG_ERROR("MADHU --  In getMtlscert\n");
    if(state_red == 1) {
        rdkcertselectorStatus_t stateredcertStat = rdkcertselector_getCert(*pthisCertSel, &certUri, &certPass);

        if (stateredcertStat != certselectorOk || certUri == NULL || certPass == NULL) {
            SWLOG_ERROR("%s, Failed to retrieve certificate for RCVRY\n", __FUNCTION__);
            rdkcertselector_free(pthisCertSel);
            if(*pthisCertSel == NULL){
                SWLOG_INFO("%s, state red Cert selector memory free\n", __FUNCTION__);
            }else{
                SWLOG_ERROR("%s, state red Cert selector memory free failed\n", __FUNCTION__);
            }
            SWLOG_ERROR("%s, All attempts/tries to retrieve certs are exhausted\n", __FUNCTION__);
    SWLOG_INFO(" --- MADHU_ returning form getMtlscert() \n");
            return STATE_RED_CERT_FETCH_FAILURE; // Return error
        }

        certFile = certUri;
        if (strncmp(certFile, FILESCHEME, sizeof(FILESCHEME)-1) == 0) {
            certFile += (sizeof(FILESCHEME)-1); // Remove file scheme prefix
        }

        size_t certFile_len = strnlen(certFile, sizeof(sec->cert_name));
        if (certFile_len >= sizeof(sec->cert_name) - 1) {
            SWLOG_ERROR("%s, Certificate file name too long (%zu chars), maximum allowed: %zu\n",
                       __FUNCTION__, certFile_len, sizeof(sec->cert_name) - 1);
            rdkcertselector_free(pthisCertSel);
    SWLOG_INFO(" --- MADHU_ returning form getMtlscert() \n");
            return STATE_RED_CERT_FETCH_FAILURE;
        }
        strncpy(sec->cert_name, certFile, sizeof(sec->cert_name));

        size_t certPass_len = strnlen(certPass, sizeof(sec->key_pas));
        if (certPass_len >= sizeof(sec->key_pas) - 1) {
            SWLOG_ERROR("%s, Certificate password too long (%zu chars), maximum allowed: %zu\n",
                       __FUNCTION__, certPass_len, sizeof(sec->key_pas) - 1);
            rdkcertselector_free(pthisCertSel);
    SWLOG_INFO(" --- MADHU_ returning form getMtlscert() \n");
            return STATE_RED_CERT_FETCH_FAILURE;
        }
        strncpy(sec->key_pas, certPass, sizeof(sec->key_pas));

        engine = rdkcertselector_getEngine(*pthisCertSel);
        if (engine == NULL) {
             sec->engine[0] = '\0';
        } else {
             size_t engine_len = strnlen(engine, sizeof(sec->engine));
             if (engine_len >= sizeof(sec->engine) - 1) {
                 SWLOG_ERROR("%s, Engine name too long (%zu chars), maximum allowed: %zu\n",
                            __FUNCTION__, engine_len, sizeof(sec->engine) - 1);
                 rdkcertselector_free(pthisCertSel);
    SWLOG_INFO(" --- MADHU_ returning form getMtlscert() \n");
                 return STATE_RED_CERT_FETCH_FAILURE;
             }
             strncpy(sec->engine, engine, sizeof(sec->engine));
        }

        strncpy(sec->cert_type, "P12", sizeof(sec->cert_type) - 1);

        SWLOG_INFO("%s, State red success. cert=%s, type=%s, engine=%s\n", __FUNCTION__, sec->cert_name, sec->cert_type, sec->engine);
        SWLOG_INFO("RED:State Red Recovery CURL_CMD: method for download\n");

    } else {

        rdkcertselectorStatus_t certStat = rdkcertselector_getCert(*pthisCertSel, &certUri, &certPass);

        if (certStat != certselectorOk || certUri == NULL || certPass == NULL) {
            SWLOG_ERROR("%s, Failed to retrieve certificate for MTLS\n",  __FUNCTION__);
            rdkcertselector_free(pthisCertSel);
            if(*pthisCertSel == NULL){
                 SWLOG_INFO("%s, Cert selector memory free\n", __FUNCTION__);
            }else{
                 SWLOG_ERROR("%s, Cert selector memory free failed\n", __FUNCTION__);
            }
    SWLOG_INFO(" --- MADHU_ returning form getMtlscert() \n");
            return MTLS_CERT_FETCH_FAILURE; // Return error
        }

        certFile = certUri;
        if (strncmp(certFile, FILESCHEME, sizeof(FILESCHEME)-1) == 0) {
            certFile += (sizeof(FILESCHEME)-1); // Remove file scheme prefix
        }

        size_t certFile_len = strnlen(certFile, sizeof(sec->cert_name));
        if (certFile_len >= sizeof(sec->cert_name) - 1) {
            SWLOG_ERROR("%s, Certificate file name too long (%zu chars), maximum allowed: %zu\n",
                       __FUNCTION__, certFile_len, sizeof(sec->cert_name) - 1);
            rdkcertselector_free(pthisCertSel);
    SWLOG_INFO(" --- MADHU_ returning form getMtlscert() \n");
            return MTLS_CERT_FETCH_FAILURE;
        }
        strncpy(sec->cert_name, certFile, sizeof(sec->cert_name));
        sec->cert_name[sizeof(sec->cert_name) - 1] = '\0';

        size_t certPass_len = strnlen(certPass, sizeof(sec->key_pas));
        if (certPass_len >= sizeof(sec->key_pas) - 1) {
            SWLOG_ERROR("%s, Certificate password too long (%zu chars), maximum allowed: %zu\n",
                       __FUNCTION__, certPass_len, sizeof(sec->key_pas) - 1);
            rdkcertselector_free(pthisCertSel);
    SWLOG_INFO(" --- MADHU_ returning form getMtlscert() \n");
            return MTLS_CERT_FETCH_FAILURE;
        }
        strncpy(sec->key_pas, certPass, sizeof(sec->key_pas));

        engine = rdkcertselector_getEngine(*pthisCertSel);
        if (engine == NULL) {
              sec->engine[0] = '\0';
        } else {
             size_t engine_len = strnlen(engine, sizeof(sec->engine));
             if (engine_len >= sizeof(sec->engine) - 1) {
                 SWLOG_ERROR("%s, Engine name too long (%zu chars), maximum allowed: %zu\n",
                            __FUNCTION__, engine_len, sizeof(sec->engine) - 1);
                 rdkcertselector_free(pthisCertSel);
    SWLOG_INFO(" --- MADHU_ returning form getMtlscert() \n");
                 return MTLS_CERT_FETCH_FAILURE;
             }
             strncpy(sec->engine, engine, sizeof(sec->engine));
         }

        strncpy(sec->cert_type, "P12", sizeof(sec->cert_type) - 1);

        SWLOG_INFO("%s, MTLS dynamic/static cert success. cert=%s, type=%s, engine=%s\n", __FUNCTION__, sec->cert_name, sec->cert_type, sec->engine);
    }
    SWLOG_INFO(" --- MADHU_ returning form getMtlscert() \n");
    return MTLS_CERT_FETCH_SUCCESS; // Return success
}
#else
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
    /* TODO: RDKE-419: temporary change until RDKE-419 gets proper solution. */
    return MTLS_FAILURE;
}
#endif
