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
    int download_only;                       //     If non-zero, skip flashing (download-only mode for D-Bus API)
    //void (*progress_callback)(unsigned long long current_bytes, unsigned long long total_bytes, void* user_data); // Progress callback
    //void* progress_callback_data;            //     User data for progress callback
} RdkUpgradeContext_t;

/**
 * @brief Firmware upgrade request function with context structure
 * @param context Input context structure containing all upgrade parameters
 * @param curl Output parameter for curl handle
 * @param pHttp_code Output parameter for HTTP response code
 * @return 0 on success, curl error code on failure
 */
int rdkv_upgrade_request(const RdkUpgradeContext_t* context, void** curl, int* pHttp_code);

/**
 * @brief Download firmware file directly (HTTP/HTTPS)
 * @param context Upgrade context containing all download parameters
 * @param httpCode Output parameter for HTTP response code
 * @param curl Output parameter for CURL handle
 * @return CURL error code (0 = success)
 */
int downloadFile(
    const RdkUpgradeContext_t* context,
    int *httpCode,
    void **curl
);

/**
 * @brief Download firmware file via CodeBig proxy
 * @param context Upgrade context containing all download parameters
 * @param httpCode Output parameter for HTTP response code
 * @param curl Output parameter for CURL handle
 * @return CURL error code (0 = success)
 */
int codebigdownloadFile(
    const RdkUpgradeContext_t* context,
    int *httpCode,
    void **curl
);

/**
 * @brief Retry firmware download with exponential backoff
 * @param context Upgrade context containing all download parameters
 * @param retry_cnt Number of retry attempts
 * @param delay Delay between retries (seconds)
 * @param httpCode Output parameter for HTTP response code
 * @param curl Output parameter for CURL handle
 * @return CURL error code (0 = success)
 */
int retryDownload(
    const RdkUpgradeContext_t* context,
    int retry_cnt,
    int delay,
    int *httpCode,
    void **curl
);

/**
 * @brief Fallback to alternate download method (Direct↔CodeBig)
 * @param context Upgrade context containing all download parameters
 * @param httpCode Output parameter for HTTP response code
 * @param curl Output parameter for CURL handle
 * @return CURL error code (0 = success)
 */
int fallBack(
    const RdkUpgradeContext_t* context,
    int *httpCode,
    void **curl
);


void dwnlError(int curl_code, int http_code, int server_type,const DeviceProperty_t *device_info,const char *lastrun, char *disableStatsUpdate);

void saveHTTPCode(int http_code, const char *lastrun);
void Upgradet2CountNotify(char *marker, int val); 
void Upgradet2ValNotify( char *marker, char *val );
#ifdef __cplusplus
}
#endif

#endif /* RDKV_UPGRADE_H_ */

