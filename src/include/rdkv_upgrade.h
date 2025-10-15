#ifndef RDKV_UTILS_H_
#define RDKV_UTILS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "rdkv_cdl.h"
#include "rfcinterface.h"

/**
 * @brief Initialize the RDK firmware utility library
 * @return 0 on success, -1 on failure
 */
int rdkv_utils_init(void);

/**
 * @brief Cleanup the RDK firmware utility library
 */
void rdkv_utils_cleanup(void);


/**
 * 
 * @param upgrade_type Type of upgrade
 * @param server_type Server type
 * @param artifactLocationUrl URL of the firmware artifact to download
 * @param dwlloc Download location 
 * @param pPostFields POST data for the request
 * @param pHttp_code Pointer to store HTTP response code
 * @return 0 on success, curl error code on failure
 */
int rdkv_upgrade_request(int upgrade_type, int server_type, 
                        const char* artifactLocationUrl, const void* dwlloc, 
                        char *pPostFields, int *pHttp_code,const char *immed_reboot_flag,int delay_dwnl , 
			const char *lastrun, char *disableStatsUpdate, const DeviceProperty_t *device_info,void **curl,int *force_exit,const Rfc_t *rfc_list);

int downloadFile( int server_type, const char* artifactLocationUrl, const void* localDownloadLocation, char* pPostFields, int *httpCode, void **curl, int *force_exit, const char *immed_reboot_flag, const DeviceProperty_t *device_info,const char *lastrun,const Rfc_t *rfc_list,char *disableStatsUpdate);

int codebigdownloadFile( int server_type, const char* artifactLocationUrl, const void* localDownloadLocation, char *pPostFields, int *httpCode, void **curl, int *force_exit, const char *immed_reboot_flag, const DeviceProperty_t *device_info,const char *lastrun,const Rfc_t *rfc_list,char *disableStatsUpdate);

int retryDownload(int server_type, const char* artifactLocationUrl, const void* localDownloadLocation, char *pPostFields, int retry_cnt, int delay, int *httpCode, void **curl, int *force_exit, const char *immed_reboot_flag, const DeviceProperty_t *device_info,const char *lastrun,const Rfc_t *rfc_list, char *disableStatsUpdate);

int fallBack(int server_type, const char* artifactLocationUrl, const void* localDownloadLocation, char *pPostFields, int *httpCode, void **curl, int *force_exit,const char *immed_reboot_flag, const DeviceProperty_t *device_info,const char *lastrun,const Rfc_t *rfc_list, char *disableStatsUpdate);

//void t2CountNotify(char *marker, int val);

//void t2ValNotify(char *marker, char *val);

void dwnlError(int curl_code, int http_code, int server_type,const DeviceProperty_t *device_info,const char *lastrun, char *disableStatsUpdate);

void saveHTTPCode(int http_code, const char *lastrun);
#ifdef __cplusplus
}
#endif

#endif /* RDKV_UTILS_H_ */
