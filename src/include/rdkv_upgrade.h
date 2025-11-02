#ifndef RDKV_UPGRADE_H_
#define RDKV_UPGRADE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "rdkv_cdl.h"
#include "rfcinterface.h"
#include "deviceutils.h"
#include "device_api.h"

#ifdef GTEST_ENABLE
#include "miscellaneous.h"
#endif
/**
 * @brief Input context structure for rdkv_upgrade_request function
 * Contains all input parameters passed to the upgrade request function
 */
typedef struct {
    int upgrade_type;                           // Type of upgrade
    int server_type;                            //  Server type
    const char* artifactLocationUrl;           //   URL of the firmware artifact to download
    const void* dwlloc;                        //   Download location (INPUT parameter)
    char* pPostFields;                        //   POST data for the request
    const char* immed_reboot_flag;            //   Immediate reboot flag
    int delay_dwnl;                           //   Download delay
    const char* lastrun;                     //    Last run information
    char* disableStatsUpdate;                //    Disable stats update flag
    const DeviceProperty_t* device_info;     //    Device info structure
    int* force_exit;                         //    Force exit flag pointer
    int trigger_type;                        //    Trigger type
    const Rfc_t* rfc_list;                  //     RFC list
} RdkUpgradeContext_t;

/**
 * @brief Firmware upgrade request function with context structure
 * @param context Input context structure containing all upgrade parameters
 * @param curl Output parameter for curl handle
 * @param pHttp_code Output parameter for HTTP response code
 * @return 0 on success, curl error code on failure
 */
int rdkv_upgrade_request(const RdkUpgradeContext_t* context, void** curl, int* pHttp_code);

int downloadFile( int server_type, const char* artifactLocationUrl, const void* localDownloadLocation, char* pPostFields, int *httpCode, void **curl, int *force_exit, const char *immed_reboot_flag, const DeviceProperty_t *device_info,const char *lastrun,const Rfc_t *rfc_list,char *disableStatsUpdate);

int codebigdownloadFile( int server_type, const char* artifactLocationUrl, const void* localDownloadLocation, char *pPostFields, int *httpCode, void **curl, int *force_exit, const char *immed_reboot_flag, const DeviceProperty_t *device_info,const char *lastrun,const Rfc_t *rfc_list,char *disableStatsUpdate);

int retryDownload(int server_type, const char* artifactLocationUrl, const void* localDownloadLocation, char *pPostFields, int retry_cnt, int delay, int *httpCode, void **curl, int *force_exit, const char *immed_reboot_flag, const DeviceProperty_t *device_info,const char *lastrun,const Rfc_t *rfc_list, char *disableStatsUpdate);

int fallBack(int server_type, const char* artifactLocationUrl, const void* localDownloadLocation, char *pPostFields, int *httpCode, void **curl, int *force_exit,const char *immed_reboot_flag, const DeviceProperty_t *device_info,const char *lastrun,const Rfc_t *rfc_list, char *disableStatsUpdate);


void dwnlError(int curl_code, int http_code, int server_type,const DeviceProperty_t *device_info,const char *lastrun, char *disableStatsUpdate);

void saveHTTPCode(int http_code, const char *lastrun);

void t2CountNotify(char *marker, int val); 

void t2ValNotify( char *marker, char *val )
#ifdef __cplusplus
}
#endif

#endif /* RDKV_UPGRADE_H_ */
