#ifndef RDKV_FLASH_H_
#define RDKV_FLASH_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "rdkv_cdl.h"

#ifdef __cplusplus
}
#endif
int flashImage(const char *server_url, const char *upgrade_file, const char *reboot_flag, const char *proto, int upgrade_type, const char *maint,int trigger_type);
int postFlash(const char *maint, const char *upgrade_file, int upgrade_type, const char *reboot_flag,int trigger_type);
#endif /* RDKV_FLASH_H_ */
