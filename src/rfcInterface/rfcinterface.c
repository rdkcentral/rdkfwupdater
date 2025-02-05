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

#include "rfcinterface.h"

#include "rdkv_cdl_log_wrapper.h"
#ifndef GTEST_ENABLE
#include "rdk_fwdl_utils.h"
#include "system_utils.h"
#endif

/*
 * Description: Get RFC data and store inside structure.
 * @param: void
 * @return: int
 * */
int getRFCSettings(Rfc_t *rfc_list) {
    int ret = -1;
    char data[RFC_VALUE_BUF_SIZE];

    if (rfc_list == NULL) {
	SWLOG_ERROR("getRFCSettings(): Parameter is NULL ret= %d\n", ret);
	return ret;
    }
    ret = read_RFCProperty("SWDLSpLimit", RFC_THROTTLE, data, sizeof(data));
    if(ret == -1) {
        SWLOG_ERROR("getRFCSettings() failed Status %d\n", ret);
    }else {
        strncpy(rfc_list->rfc_throttle, data, RFC_VALUE_BUF_SIZE - 1);
        rfc_list->rfc_throttle[RFC_VALUE_BUF_SIZE - 1] = '\0';
	SWLOG_INFO("getRFCSettings() rfc throttle= %s\n", rfc_list->rfc_throttle);
    }
    ret = read_RFCProperty("SWDLSpLimit", RFC_TOPSPEED, data, sizeof(data));
    if(ret == -1) {
        SWLOG_ERROR("getRFCSettings() failed Status %d\n", ret);
    }else {
        strncpy(rfc_list->rfc_topspeed, data, RFC_VALUE_BUF_SIZE - 1);
        rfc_list->rfc_topspeed[RFC_VALUE_BUF_SIZE - 1] = '\0';
	SWLOG_INFO("getRFCSettings() rfc topspeed= %s\n", rfc_list->rfc_topspeed);
    }
    ret = read_RFCProperty("IncrementalCDL", RFC_INCR_CDL, data, sizeof(data));
    if(ret == -1) {
        SWLOG_ERROR("getRFCSettings() failed Status %d and %s\n", ret, rfc_list->rfc_incr_cdl);
    }else {
        strncpy(rfc_list->rfc_incr_cdl, data, RFC_VALUE_BUF_SIZE - 1);
        rfc_list->rfc_incr_cdl[RFC_VALUE_BUF_SIZE - 1] = '\0';
	SWLOG_INFO("getRFCSettings() rfc IncrementalCDL= %s\n", rfc_list->rfc_incr_cdl);
    }
    ret = read_RFCProperty("MTLS", RFC_MTLS, data, sizeof(data));
    if(ret == -1) {
        SWLOG_ERROR("getRFCSettings() rfc= %s failed Status %d\n", RFC_MTLS, ret);
    }else {
        strncpy(rfc_list->rfc_mtls, data, RFC_VALUE_BUF_SIZE - 1);
	rfc_list->rfc_mtls[RFC_VALUE_BUF_SIZE - 1] = '\0';
        SWLOG_INFO("getRFCSettings() rfc mtls= %s\n", rfc_list->rfc_mtls);
    }
    return 0;
}

#if defined(RFC_API_ENABLED)
/* Description: Reading rfc data
 * @param type : rfc type
 * @param key: rfc key
 * @param data : Store rfc value
 * @return int 1 READ_RFC_SUCCESS on success and READ_RFC_FAILURE -1 on failure
 * */
int read_RFCProperty(char* type, const char* key, char *out_value, size_t datasize) {
    RFC_ParamData_t param;
    int data_len;
    int ret = READ_RFC_FAILURE;

    if(key == NULL || out_value == NULL || datasize == 0 || type == NULL) {
        SWLOG_ERROR("read_RFCProperty() one or more input values are invalid\n");
        return ret;
    }
    //SWLOG_INFO("key=%s\n", key);
    WDMP_STATUS status = getRFCParameter(type, key, &param);
    if(status == WDMP_SUCCESS || status == WDMP_ERR_DEFAULT_VALUE) {
        data_len = strlen(param.value);
        if(data_len >= 2 && (param.value[0] == '"') && (param.value[data_len - 1] == '"')) {
            // remove quotes arround data
            snprintf( out_value, datasize, "%s", &param.value[1] );
            *(out_value + data_len - 2) = 0;
        }else {
            snprintf( out_value, datasize, "%s", param.value );
        }
        SWLOG_INFO("read_RFCProperty() name=%s,type=%d,value=%s,status=%d\n", param.name, param.type, param.value, status);
        ret = READ_RFC_SUCCESS;
    }else {
        SWLOG_ERROR("error:read_RFCProperty(): status= %d\n", status);
        *out_value = 0;
    }
    return ret;
}

/* Description: Writing rfc data
 * @param type : rfc type
 * @param key: rfc key
 * @param data : new rfc value
 * @param datatype: data type of value parameter
 * @return int 1 WRITE_RFC_SUCCESS on success and WRITE_RFC_FAILURE -1 on failure
 * */
int write_RFCProperty(char* type, const char* key, const char *value, RFCVALDATATYPE datatype) {
    WDMP_STATUS status = WDMP_FAILURE;
    int ret = WRITE_RFC_FAILURE;
    if (type == NULL || key == NULL || value == NULL) {
        SWLOG_ERROR("%s: Parameter is NULL\n", __FUNCTION__);
	return ret;
    }
    if (datatype == RFC_STRING) {
        status = setRFCParameter(type, key, value, WDMP_STRING);
    } else if(datatype == RFC_UINT) {
        status = setRFCParameter(type, key, value, WDMP_UINT);
    } else {
        status = setRFCParameter(type, key, value, WDMP_BOOLEAN);
    }
    if (status != WDMP_SUCCESS) {
        SWLOG_ERROR("%s: setRFCParameter failed. key=%s and status=%s\n", __FUNCTION__, key, getRFCErrorString(status));
    } else {
        SWLOG_INFO("%s: setRFCParameter Success\n", __FUNCTION__);
	ret = WRITE_RFC_SUCCESS;
    }
    return ret;
}
#else
/* Description: Below function should be implement Reading rfc data for RDK-M
 * @param type : rfc type
 * @param key: rfc key
 * @param data : Store rfc value
 * @return int 1 READ_RFC_SUCCESS on success and READ_RFC_FAILURE -1 on failure
 *             0 READ_RFC_NOTAPPLICABLE 
 * */
int read_RFCProperty(char* type, const char* key, char *out_value, size_t datasize) {
    //TODO: Need to implement for RDK-M
    SWLOG_INFO("%s: Not Applicabe For RDK-M. Need to implement\n", __FUNCTION__);
    return READ_RFC_NOTAPPLICABLE;
}
/* Description: Below function should be Writing rfc data For RDk-M
 * @param type : rfc type
 * @param key: rfc key
 * @param data : new rfc value
 * @param datatype: data type of value parameter
 * @return int 1 WRITE_RFC_SUCCESS on success and WRITE_RFC_FAILURE -1 on failure
 *             0 READ_RFC_NOTAPPLICABLE 
 * */
int write_RFCProperty(char* type, const char* key, const char *value, RFCVALDATATYPE datatype) {
    //TODO: Need to implement for RDK-M
    SWLOG_INFO("%s: Not Applicabe For RDK-M. Need to implement\n", __FUNCTION__);
    return WRITE_RFC_NOTAPPLICABLE;
}
#endif
int isMtlsEnabled(const char *device_name)
{
    int mtls_check = 0;
    int ret = UTILS_FAIL;
    char *dev_prop_name = "FORCE_MTLS";
    char data[MAX_DEVICE_PROP_BUFF_SIZE];
    char rfc_data[RFC_VALUE_BUF_SIZE];

    *data = 0;
    *rfc_data = 0;
    /*if ((device_name != NULL) && (device_name)) {
        SWLOG_INFO("%s: MTLS default enable for this device:%s\n", __FUNCTION__, device_name);
        mtls_check = 1;
        return mtls_check;
    }*/
    ret = getDevicePropertyData(dev_prop_name, data, sizeof(data));
    if (ret == UTILS_SUCCESS) {
        SWLOG_INFO("%s: MTLS status from device.property file=%s\n", __FUNCTION__, data);
    } else {
        SWLOG_INFO("%s: NO MTLS status from device.property file\n", __FUNCTION__);
    }
    ret = UTILS_FAIL;
    ret = read_RFCProperty("MTLS", RFC_MTLS, rfc_data, sizeof(rfc_data));
    if(ret == -1) {
        SWLOG_ERROR("%s: rfc=%s failed Status %d\n", __FUNCTION__, RFC_MTLS, ret);
    }else {
        SWLOG_INFO("%s: rfc mtls= %s\n", __FUNCTION__, rfc_data);
    }
    if((!(strncmp(data, "true", 4))) || (!(strncmp(rfc_data, "true", 4)))) {
        SWLOG_INFO("MTLS prefered\n");
        mtls_check = 1;
    }
    return mtls_check;
}

int isIncremetalCDLEnable(const char *file_name)
{
    int chunk_dwld = 0;
    int ret = -1;
    char rfc_data[RFC_VALUE_BUF_SIZE];

    if (file_name == NULL) {
        SWLOG_ERROR("%s : Parameter is NULL\n", __FUNCTION__);
        return chunk_dwld;
    }
    SWLOG_INFO("%s: Checking IncremetalCDLEnable... Download image name=%s\n", __FUNCTION__, file_name);

    *rfc_data = 0;
    ret = read_RFCProperty("IncrementalCDL", RFC_INCR_CDL, rfc_data, sizeof(rfc_data));
    if(ret == -1) {
        SWLOG_ERROR("%s: IncrementalCDL rfc=%s failed Status %d\n", __FUNCTION__, RFC_MTLS, ret);
	return chunk_dwld;
    }else {
        SWLOG_INFO("%s: rfc IncrementalCDL= %s\n", __FUNCTION__, rfc_data);
    }

    if((strncmp(rfc_data, "true", 4)) == 0) {
        SWLOG_INFO("%s :  incremental cdl is TRUE\n", __FUNCTION__);
        if((filePresentCheck(file_name)) == 0) {
            chunk_dwld = 1;
            SWLOG_INFO("%s: File=%s is present. IncrementalCDL enable=%d\n",__FUNCTION__, file_name, chunk_dwld);
        }
    }
    return chunk_dwld;
}

/* Description:Checking debug services rfc status
 * @param type : void
 * @return bool true : enable and false: disable
 * */
bool isDebugServicesEnabled(void)
{
    bool status =false;
    int ret = -1;
    char rfc_data[RFC_VALUE_BUF_SIZE];

    *rfc_data = 0;
    ret = read_RFCProperty("DIRECTCDN", RFC_DEBUGSRV, rfc_data, sizeof(rfc_data));
    if (ret == -1) {
        SWLOG_ERROR("%s: rfc Debug services =%s failed Status %d\n", __FUNCTION__, RFC_DEBUGSRV, ret);
        return status;
    } else {
        SWLOG_INFO("%s: rfc Debug services = %s\n", __FUNCTION__, rfc_data);
        if ((strncmp(rfc_data, "true", 4)) == 0) {
            status = true;
        }
    }
    return status;
}

/* Description: Cheacking notify rfc status
 * @param type : void
 * @return bool true : enable and false: disable
 * */
bool isMmgbleNotifyEnabled(void)
{
    bool status = false;
    int ret = -1;
    char rfc_data[RFC_VALUE_BUF_SIZE];

    *rfc_data = 0;
    ret = read_RFCProperty("ManageNotify", RFC_MNG_NOTIFY, rfc_data, sizeof(rfc_data));
    if (ret == -1) {
        SWLOG_ERROR("%s: ManageNotify rfc=%s failed Status %d\n", __FUNCTION__, RFC_MNG_NOTIFY, ret);
	return status;
    } else {
        SWLOG_INFO("%s: rfc ManageNotify= %s\n", __FUNCTION__, rfc_data);
        if ((strncmp(rfc_data, "true", 4)) == 0) {
            status = true;
        }
    }
    return status;
}
