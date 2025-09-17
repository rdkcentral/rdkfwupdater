#ifndef RDKV_UTILS_H_
#define RDKV_UTILS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "rdkv_cdl.h"


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
                        char *pPostFields, int *pHttp_code,char *immed_reboot_flag,int delay_dwnl , 
			char *lastrun, char *disableStatsUpdate, DeviceProperty_t *device_info);

#ifdef __cplusplus
}
#endif

#endif /* RDKV_UTILS_H_ */
