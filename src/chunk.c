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

#include <sys/stat.h>

#include "include/rdkv_cdl.h"
#include "rdkv_cdl_log_wrapper.h"
//#include "mtlsUtils.h"
#ifndef GTEST_ENABLE
#include "downloadUtil.h"
#include "urlHelper.h"
#endif

#ifdef GTEST_ENABLE
#define CURLE_OK 0
#endif

extern void *curl;
extern int force_exit;

/* Description: Function will provide content length by reading from the http header file
 * @param: void
 * @return: size_t which is content length*/
size_t getContentLength(const char *file)
{
    size_t cnt_len = 0;
    char tbuff[64] = {0};
    char *tmp = NULL;
    FILE *fp = NULL;

    fp = fopen(file,"r");
    if (fp == NULL) {
	SWLOG_ERROR( "Inside getContentLength() unable to open file\n");
 	return cnt_len;
    }
    while((fgets(tbuff, sizeof(tbuff)-1, fp) != NULL)) {
	SWLOG_INFO("%s\n", tbuff);
        if (strstr(tbuff, "Content-Length: ")) {
            SWLOG_INFO("Content_lenght string=%s\n", tbuff);
            tmp = strchr(tbuff, ':');
            if (tmp != NULL) {
                cnt_len = atoi(tmp + 2);
                //break;
            }
        }
	memset(tbuff,'\0', sizeof(tbuff));
    }
    SWLOG_INFO("Content_lenght string=%zu\n", cnt_len);
    //unlink(FILE_CONTENT_LEN);
    //CID:280545-Resource leak-file not closed
    fclose(fp);
    return cnt_len;
}


/* Description: chunkDownload(): Use For Chunk Download.
 * file_dwnl : This is input structure which contains url, pathname to store file
 * 		and chunkdownload retry time.
 * sec : Use for pass crdential required for communicate to server.
 * speed_limit : Use for Throttle feature. If it is zero speed limiti will not set.
 * httpcode : http code Return to caller function.
 * Return type: int success 0 and failure -1 and for full download request 1. 
*/
int chunkDownload(FileDwnl_t *pfile_dwnl, MtlsAuth_t *sec, unsigned int speed_limit, int *httpcode)
{
    int ret = -1;
    int curl_ret_code = -1;
    int curl_code_header_req = -1;
    size_t content_len = 0;
    char range[16] = {0};
    int file_size = 0;
    char headerfile[136];

    if (pfile_dwnl == NULL || httpcode == NULL) {
        return ret;
    }
    snprintf(headerfile, sizeof(headerfile), "%s.header", pfile_dwnl->pathname);
    content_len = getContentLength(headerfile);
    SWLOG_INFO("content_len = %zu featched from headerfile=%s\n", content_len, headerfile);
    t2CountNotify("SYST_INFO_FetchFWCTN", 1);
    if (((filePresentCheck(pfile_dwnl->pathname)) == 0) && (content_len > 0)) {
        file_size = getFileSize(pfile_dwnl->pathname);
        /* Already Full File Downloaded*/
        if (file_size == content_len) {
            SWLOG_INFO("chunkDownload() Existing file_size=%d and content_len=%zu are same\n", file_size, content_len);
            t2CountNotify("SYST_INFO_SAME_FWCTN", 1);
            *httpcode = 200;
            curl_ret_code = CURLE_OK;
            return curl_ret_code;
        } else if (file_size != -1) {
            snprintf(range,sizeof(range), "%d-", file_size);
            SWLOG_INFO("chunkDownload() file size=%d and range=%s\n", file_size, range);
        }   else {
            SWLOG_ERROR( "chunkDownload() error getFileSize=%s\n", pfile_dwnl->pathname);
            return -1;
        }
    }else {
        SWLOG_ERROR( "chunkDownload() Error to proceed for chunk download due to below reason.\nContent length not present=%zu or Partial image file not present.\n", content_len);
        t2CountNotify("SYST_ERR_FWCTNFetch", 1);
        return curl_code_header_req;
    }
    if (httpcode != NULL) {
        *httpcode = 0;
    }
    setDwnlState(RDKV_FWDNLD_DOWNLOAD_INIT);
    curl = doCurlInit();
    setDwnlState(RDKV_FWDNLD_DOWNLOAD_INPROGRESS);
    if (curl != NULL) {
        curl_ret_code = doHttpFileDownload(curl, pfile_dwnl, sec, speed_limit, range, httpcode);
    }else {
	SWLOG_ERROR( "chunkDownload() error in doCurlInit\n");
        return curl_ret_code;
    } 
    setDwnlState(RDKV_FWDNLD_DOWNLOAD_EXIT);
    if (curl != NULL) {
        doStopDownload(curl);
    }
    /*During Download Stop and exit the app. This feature for Throttling
     * when throttle speed limit set to 0*/
    if (force_exit == 1 && (curl_ret_code == 23)) {
	uninitialize(INITIAL_VALIDATION_SUCCESS);
        exit(1);
    }
    SWLOG_INFO("chunkDownload() curl ret status=%u\n", curl_ret_code);
    if (curl_ret_code == 33 || curl_ret_code == 36) {
        SWLOG_ERROR( "chunkDownload() curl retun 33/36 So going for full Download:%u\n", curl_ret_code);
        if ((filePresentCheck(pfile_dwnl->pathname)) == 0) {
            unlink(pfile_dwnl->pathname);
            unlink(headerfile);
            setDwnlState(RDKV_FWDNLD_DOWNLOAD_INIT);
            curl = doCurlInit();
            setDwnlState(RDKV_FWDNLD_DOWNLOAD_INPROGRESS);
            // Triggering Full Download.
    	    if (curl != NULL) {
                curl_ret_code = doHttpFileDownload(curl, pfile_dwnl, sec, speed_limit, NULL, httpcode);
    	    }else {
                SWLOG_ERROR( "chunkDownload() error in doCurlInit after curl return 33 or 36\n");
                return curl_ret_code;
    	    }
            setDwnlState(RDKV_FWDNLD_DOWNLOAD_EXIT);
            if (curl != NULL) {
                doStopDownload(curl);
            }
             /*During Download Stop and exit the app. This feature for Throttling
             * when throttle speed limit set to 0*/
            if (force_exit == 1 && (curl_ret_code == 23)) {
                uninitialize(INITIAL_VALIDATION_SUCCESS);
                exit(1);
            }
        }
    } else if ((curl_ret_code == 0) && ((filePresentCheck(pfile_dwnl->pathname)) == 0)) {
        file_size = 0;
        file_size = getFileSize(pfile_dwnl->pathname);
        SWLOG_INFO("chunkDownload() curl status success=%u, filesize=%d, content_len=%zu\n", curl_ret_code, file_size, content_len);
        if (file_size == content_len) {
            SWLOG_INFO("chunkDownload() All file data Downloaded\n");
            setDwnlState(RDKV_FWDNLD_DOWNLOAD_COMPLETE);
        } else {
            SWLOG_ERROR( "chunkDownload() Downloaded File Size and content length fetch from header are not same. So Go For Full Download\n");
            t2CountNotify("SYST_ERR_DiffFWCTN_FLdnld", 1);
            SWLOG_ERROR( "chunkDownload() File Size=%d and content len=%zu\n", file_size, content_len);
            setDwnlState(RDKV_FWDNLD_DOWNLOAD_INIT);
            curl = doCurlInit();
            setDwnlState(RDKV_FWDNLD_DOWNLOAD_INPROGRESS);
            // Trigger Full Download
            if (curl != NULL) {
                curl_ret_code = doHttpFileDownload(curl, pfile_dwnl, sec, speed_limit, NULL, httpcode);
            }else {
                SWLOG_ERROR( "chunkDownload() error in doCurlInit after content length not match\n");
                return -1;
            }
            setDwnlState(RDKV_FWDNLD_DOWNLOAD_EXIT);
            if (curl != NULL) {
                doStopDownload(curl);
            }
            if (force_exit == 1 && (curl_ret_code == 23)) {
                uninitialize(INITIAL_VALIDATION_SUCCESS);
                exit(1);
            }
	}
    } else {
        SWLOG_ERROR( "chunkDownload() curl status fail=%u\n", curl_ret_code);
        setDwnlState(RDKV_FWDNLD_DOWNLOAD_FAILED);
    }

    if((filePresentCheck(CURL_PROGRESS_FILE)) == 0) {
        SWLOG_INFO("%s : Curl Progress data For Chunk Download...\n", __FUNCTION__);
        logFileData(CURL_PROGRESS_FILE);
        unlink(CURL_PROGRESS_FILE);
    }
    return curl_ret_code;
}
