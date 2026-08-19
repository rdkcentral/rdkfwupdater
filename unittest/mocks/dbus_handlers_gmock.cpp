/*
 * Copyright 2025 Comcast Cable Communications Management, LLC
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
/**
 * @file dbus_handlers_gmock.cpp
 * @brief Google Mock implementations for D-Bus handlers unit tests
 * 
 * This file contains mock implementations for external dependencies
 * used by rdkFwupdateMgr_handlers.c and rdkv_dbus_server.c
 * 
 * Mocked modules:
 * - JSON processing functions
 * - Device utility functions  
 * - RDK upgrade functions
 * - Device API functions
 * - RFC interface functions
 * - IARM interface functions
 * - Flash functions
 * - System utilities
 */

#include "dbus_handlers_gmock.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>  // For std::remove

// Global mock instances
MockJsonProcess* mock_json_process = nullptr;
MockDeviceUtils* mock_deviceutils = nullptr;
MockRdkvUpgrade* mock_rdkv_upgrade = nullptr;
MockDeviceApi* mock_device_api = nullptr;
MockRfcInterface* mock_rfc_interface = nullptr;
MockIarmInterface* mock_iarm_interface = nullptr;
MockFlash* mock_flash = nullptr;
MockSystemUtils* mock_system_utils = nullptr;

// Global device info (extern from handlers)
extern "C" {
DeviceProperty_t device_info = {};  // Use empty initializer
ImageDetails_t cur_img_detail = {};
Rfc_t rfc_list = {};
}

// Global variables declared in rdkv_dbus_server.h (C++ linkage, not extern "C")
CurrentFlashState* current_flash = NULL;
gboolean IsFlashInProgress = FALSE;

/**
 * @brief Initialize all mocks
 */
void InitializeMocks() {
    if (!mock_json_process) mock_json_process = new MockJsonProcess();
    if (!mock_deviceutils) mock_deviceutils = new MockDeviceUtils();
    if (!mock_rdkv_upgrade) mock_rdkv_upgrade = new MockRdkvUpgrade();
    if (!mock_device_api) mock_device_api = new MockDeviceApi();
    if (!mock_rfc_interface) mock_rfc_interface = new MockRfcInterface();
    if (!mock_iarm_interface) mock_iarm_interface = new MockIarmInterface();
    if (!mock_flash) mock_flash = new MockFlash();
    if (!mock_system_utils) mock_system_utils = new MockSystemUtils();
}

/**
 * @brief Cleanup all mocks
 */
void CleanupMocks() {
    delete mock_json_process; mock_json_process = nullptr;
    delete mock_deviceutils; mock_deviceutils = nullptr;
    delete mock_rdkv_upgrade; mock_rdkv_upgrade = nullptr;
    delete mock_device_api; mock_device_api = nullptr;
    delete mock_rfc_interface; mock_rfc_interface = nullptr;
    delete mock_iarm_interface; mock_iarm_interface = nullptr;
    delete mock_flash; mock_flash = nullptr;
    delete mock_system_utils; mock_system_utils = nullptr;
}

// ============================================================================
// Mock Function Implementations (C linkage for C source files to link)
// ============================================================================

extern "C" {

// ============================================================================
// JSON Processing Function Mocks
// ============================================================================

/**
 * @brief Mock implementation of getXconfRespData
 */
#if 0
int getXconfRespData(XCONFRES *pResponse, char *pStr) {
    if (mock_json_process) {
        return mock_json_process->getXconfRespData(pResponse, pStr);
    }
    return -1;  // Default failure
}
#endif
/**
 * @brief Mock implementation of processJsonResponse
 */
#if 0
int processJsonResponse(XCONFRES *pResponse, const char *cur_img_name,
                       const char *device_model, const char *maint_status) {
    if (mock_json_process) {
        return mock_json_process->processJsonResponse(pResponse, cur_img_name,
                                                      device_model, maint_status);
    }
    return -1;  // Default failure
}
#endif
// ============================================================================
// Device Utils Function Mocks
// ============================================================================

// NOTE: GetServURL is provided by device_api.c - removed mock to avoid multiple definition

/**
 * @brief Mock implementation of createJsonString
 */
#if 0
size_t createJsonString(char *jsonStr, size_t max_len) {
    if (mock_deviceutils) {
        return mock_deviceutils->createJsonString(jsonStr, max_len);
    }
    return 0;  // Default failure
}
#endif
/**
 * @brief Mock implementation of allocDowndLoadDataMem
 */
int allocDowndLoadDataMem(DownloadData *pDwnLoc, size_t size) {
    if (mock_deviceutils) {
        return mock_deviceutils->allocDowndLoadDataMem(pDwnLoc, size);
    }
    
    // Default implementation
    if (!pDwnLoc) return -1;
    
    pDwnLoc->pvOut = malloc(size);
    if (!pDwnLoc->pvOut) return -1;
    
    pDwnLoc->memsize = size;
    pDwnLoc->datasize = 0;
    return 0;
}

/**
 * @brief Mock implementation of get_difw_path
 */
char* get_difw_path(void) {
    if (mock_deviceutils) {
        return mock_deviceutils->get_difw_path();
    }
    return strdup("/opt/CDL");  // Default path
}

// ============================================================================
// RDK Upgrade Function Mocks
// ============================================================================

/**
 * @brief Mock implementation of rdkv_upgrade_request
 */
int rdkv_upgrade_request(const RdkUpgradeContext_t *ctx, void **curl_handle, int *pHttp_code) {
    if (mock_rdkv_upgrade) {
        return mock_rdkv_upgrade->rdkv_upgrade_request(ctx, curl_handle, pHttp_code);
    }
    return -1;  // Default failure
}

// ============================================================================
// Device API Function Mocks
// ============================================================================

/**
 * @brief Mock implementation of GetFirmwareVersion
 */
bool GetFirmwareVersion(char *buffer, size_t buffer_size) {
    if (mock_device_api) {
        return mock_device_api->GetFirmwareVersion(buffer, buffer_size);
    }
    
    // Default implementation
    if (buffer && buffer_size > 0) {
        strncpy(buffer, "VERSION_1.0.0", buffer_size - 1);
        buffer[buffer_size - 1] = '\0';
        return true;
    }
    return false;
}

/**
 * @brief Mock implementation of getDeviceProperties
 */
int getDeviceProperties(DeviceProperty_t *pDeviceInfo) {
    if (mock_device_api) {
        return mock_device_api->getDeviceProperties(pDeviceInfo);
    }
    
    // Default implementation
    if (!pDeviceInfo) return -1;
    
    strncpy(pDeviceInfo->model, "TEST_MODEL", sizeof(pDeviceInfo->model) - 1);
    strncpy(pDeviceInfo->maint_status, "false", sizeof(pDeviceInfo->maint_status) - 1);
    return 0;
}

/**
 * @brief Mock implementation of filePresentCheck
 */
int filePresentCheck(const char *filepath) {
    if (mock_device_api) {
        return mock_device_api->filePresentCheck(filepath);
    }
    return -1;  // File not present
}

// ============================================================================
// RFC Interface Function Mocks
// ============================================================================

/**
 * @brief Mock implementation of isDirectCDNEnabled
 */
bool isDirectCDNEnabled(void) {
    if (mock_rfc_interface) {
        return mock_rfc_interface->isDirectCDNEnabled();
    }
    return false;
}

/**
 * @brief Mock implementation of getRFCSettings
 */
int getRFCSettings(Rfc_t *pRfc) {
    if (mock_rfc_interface) {
        return mock_rfc_interface->getRFCSettings(pRfc);
    }
    
    // Default implementation
    if (!pRfc) return -1;
    
    memset(pRfc, 0, sizeof(Rfc_t));
    return 0;
}

// ============================================================================
// IARM Interface Function Mocks
// ============================================================================

/**
 * @brief Mock implementation of eventManager
 */
void eventManager(const char *event_type, const char *event_status) {
    if (mock_iarm_interface) {
        mock_iarm_interface->eventManager(event_type, event_status);
    }
}

// ============================================================================
// Flash Function Mocks
// ============================================================================

/**
 * @brief Mock implementation of flashImage
 */
int flashImage(const char *server_url, const char *upgrade_file,
               const char *reboot_flag, const char *proto,
               int upgrade_type, const char *maint, int trigger_type) {
    if (mock_flash) {
        return mock_flash->flashImage(server_url, upgrade_file, reboot_flag,
                                      proto, upgrade_type, maint, trigger_type);
    }
    return 0;  // Success
}

} // extern "C"
// ============================================================================
// System Utils Function Mocks
// ============================================================================

/**
 * @brief Mock implementation of system
 */
int system(const char *command) {
    if (mock_system_utils) {
        return mock_system_utils->system_call(command);
    }
    return 0;  // Success
}

/**
 * @brief Mock implementation of unlink
 * Note: For test isolation, we actually delete the file even when mocked
 */
int unlink(const char *pathname) {
    // Actually delete the file for test isolation
    int real_result = std::remove(pathname);  // Use C++ std::remove
    
    // Then call mock if it exists (for verification)
    if (mock_system_utils) {
        mock_system_utils->unlink_call(pathname);
    }
    
    return (real_result == 0) ? 0 : -1;
}

/**
 * @brief Mock implementation of stat
 */
int stat(const char *pathname, struct stat *statbuf) {
    if (mock_system_utils) {
        return mock_system_utils->stat_call(pathname, statbuf);
    }
    
    // Default: File exists with size 1024
    if (statbuf) {
        memset(statbuf, 0, sizeof(struct stat));
        statbuf->st_size = 1024;
        statbuf->st_mode = S_IFREG | 0644;
        return 0;
    }
    return -1;
}

/**
 * @brief Mock implementation of sleep
 */
unsigned int sleep(unsigned int seconds) {
    if (mock_system_utils) {
        return mock_system_utils->sleep_call(seconds);
    }
    return 0;  // Don't actually sleep in tests
}

/**
 * @brief Mock implementation of usleep
 */
int usleep(useconds_t usec) {
    if (mock_system_utils) {
        return mock_system_utils->usleep_call(usec);
    }
    return 0;  // Don't actually sleep in tests
}

// ============================================================================
// Mock Helper Functions
// ============================================================================

/**
 * @brief Setup default mock behaviors for successful operations
 */
void SetupDefaultMocks() {
    using ::testing::_;
    using ::testing::Return;
    using ::testing::DoAll;
    using ::testing::Invoke;
    
    // Default JSON processing
    ON_CALL(*mock_json_process, getXconfRespData(_, _))
        .WillByDefault(Return(0));
    
    ON_CALL(*mock_json_process, processJsonResponse(_, _, _, _))
        .WillByDefault(Return(0));
    
    // Default device utils - GetServURL removed, using real implementation
    
    ON_CALL(*mock_deviceutils, createJsonString(_, _))
        .WillByDefault(DoAll(
            Invoke([](char* json, size_t len) {
                strncpy(json, "{\"test\":\"data\"}", len - 1);
                json[len - 1] = '\0';
                return strlen(json);
            }),
            Return(15)
        ));

    ON_CALL(*mock_deviceutils, allocDowndLoadDataMem(_, _))
        .WillByDefault(Invoke([](DownloadData* pDwnLoc, size_t size) {
            if (pDwnLoc == NULL || size == 0) {
                return -1;
            }

            pDwnLoc->pvOut = calloc(1, size);
            if (pDwnLoc->pvOut == NULL) {
                return -1;
            }

            pDwnLoc->memsize = size;
            pDwnLoc->datasize = 0;
            return 0;
        }));
    
    ON_CALL(*mock_deviceutils, get_difw_path())
        .WillByDefault(Return(strdup("/opt/CDL")));
    
    // Default upgrade request
    ON_CALL(*mock_rdkv_upgrade, rdkv_upgrade_request(_, _, _))
        .WillByDefault(DoAll(
            Invoke([](const RdkUpgradeContext_t* ctx, void** curl, int* http) {
                *http = 200;
                if (curl != NULL) {
                    *curl = (void*)0x1;
                }

                if (ctx != NULL && ctx->dwlloc != NULL) {
                    DownloadData* dwlloc = (DownloadData*)ctx->dwlloc;
                    const char* json = "{\"firmwareVersion\":\"VERSION_2.0.0\","
                                       "\"firmwareFilename\":\"test.bin\","
                                       "\"firmwareLocation\":\"http://test.com/test.bin\","
                                       "\"rebootImmediately\":\"false\"}";
                    if (dwlloc->pvOut != NULL && dwlloc->memsize > 0) {
                        snprintf((char*)dwlloc->pvOut, dwlloc->memsize, "%s", json);
                        dwlloc->datasize = strlen((char*)dwlloc->pvOut);
                    }
                }
                return 0;
            }),
            Return(0)
        ));
    
    // Default device API
    ON_CALL(*mock_device_api, GetFirmwareVersion(_, _))
        .WillByDefault(DoAll(
            Invoke([](char* buffer, size_t len) {
                strncpy(buffer, "VERSION_1.0.0", len - 1);
                return true;
            }),
            Return(true)
        ));
    
    ON_CALL(*mock_device_api, getDeviceProperties(_))
        .WillByDefault(Return(0));
    
    // Default RFC settings
    ON_CALL(*mock_rfc_interface, getRFCSettings(_))
        .WillByDefault(Return(0));
    
    // Default event manager
    ON_CALL(*mock_iarm_interface, eventManager(_, _))
        .WillByDefault(Invoke([](const char *, const char *) {}));
    
    // Default flash
    ON_CALL(*mock_flash, flashImage(_, _, _, _, _, _, _))
        .WillByDefault(Return(0));
    
    // Default system calls
    ON_CALL(*mock_system_utils, system_call(_))
        .WillByDefault(Return(0));
    
    ON_CALL(*mock_system_utils, sleep_call(_))
        .WillByDefault(Return(0));
    
    ON_CALL(*mock_system_utils, usleep_call(_))
        .WillByDefault(Return(0));
}

/**
 * @brief Setup mocks for failure scenarios
 */
void SetupFailureMocks() {
    using ::testing::_;
    using ::testing::Return;
    
    // Failure responses
    ON_CALL(*mock_json_process, getXconfRespData(_, _))
        .WillByDefault(Return(-1));
    
    // GetServURL removed - using real implementation
    
    ON_CALL(*mock_rdkv_upgrade, rdkv_upgrade_request(_, _, _))
        .WillByDefault(Return(-1));
    
    ON_CALL(*mock_device_api, GetFirmwareVersion(_, _))
        .WillByDefault(Return(false));
    
    ON_CALL(*mock_flash, flashImage(_, _, _, _, _, _, _))
        .WillByDefault(Return(-1));
}

/**
 * @brief Reset all mocks to default state
 */
void ResetAllMocks() {
    if (mock_json_process) {
        testing::Mock::VerifyAndClearExpectations(mock_json_process);
    }
    if (mock_deviceutils) {
        testing::Mock::VerifyAndClearExpectations(mock_deviceutils);
    }
    if (mock_rdkv_upgrade) {
        testing::Mock::VerifyAndClearExpectations(mock_rdkv_upgrade);
    }
    if (mock_device_api) {
        testing::Mock::VerifyAndClearExpectations(mock_device_api);
    }
    if (mock_rfc_interface) {
        testing::Mock::VerifyAndClearExpectations(mock_rfc_interface);
    }
    if (mock_iarm_interface) {
        testing::Mock::VerifyAndClearExpectations(mock_iarm_interface);
    }
    if (mock_flash) {
        testing::Mock::VerifyAndClearExpectations(mock_flash);
    }
    if (mock_system_utils) {
        testing::Mock::VerifyAndClearExpectations(mock_system_utils);
    }
}

// ============================================================================
// Additional missing function stubs
// ============================================================================

// NOTE: Global variables current_flash and IsFlashInProgress are declared
// in rdkv_dbus_server.h with C++ linkage. We don't redeclare them here.

// NOTE: lastDwnlImg and currentImg are already declared in extern "C" block above

size_t lastDwnlImg(char *img_name, size_t img_name_size) {
    if (img_name == NULL || img_name_size == 0) {
        return 0;
    }

    snprintf(img_name, img_name_size, "%s", "VERSION_1.0.0");
    return strlen(img_name);
}

size_t currentImg(char *img_name, size_t img_name_size) {
    if (img_name == NULL || img_name_size == 0) {
        return 0;
    }

    snprintf(img_name, img_name_size, "%s", "VERSION_1.0.0");
    return strlen(img_name);
}

// SWLOG_* macros are already defined in rdkv_cdl_log_wrapper.h
// so we don't need to provide function implementations

// Device API functions are already declared in device_api.h
// The real implementations will be linked from device_api.c

// Functions that are NOT in the headers and need stubs:

int getDevicePropertyData(const char* key, char* value, unsigned int size) {
    if (key == NULL || value == NULL || size == 0) {
        return -1;
    }

    if (strcmp(key, "ESTB_INTERFACE") == 0) {
        snprintf(value, size, "%s", "eth0");
    } else if (strcmp(key, "LABSIGNED_ENABLED") == 0) {
        const char* env_val = getenv("DBUS_MOCK_LABSIGNED");
        snprintf(value, size, "%s", (env_val && *env_val) ? env_val : "false");
    } else if (strcmp(key, "CPU_ARCH") == 0) {
        snprintf(value, size, "%s", "x86");
    } else if (strcmp(key, "DEVICE_NAME") == 0) {
        snprintf(value, size, "%s", "XI_TEST");
    } else if (strcmp(key, "WHOAMI_SUPPORT") == 0) {
        snprintf(value, size, "%s", "false");
    } else {
        value[0] = '\0';
    }

    return 0;
}

int waitForNtp(void) {
    return 0;  // Success
}

// Telemetry functions - match the signature in rdkv_cdl.h
void t2ValNotify(char *marker, char *val) {
    // Stub - do nothing in tests
}

// URL conversion function
char* makeHttpHttps(const char* url) {
    if (!url) return NULL;
    // Simple stub - just duplicate the URL
    return strdup(url);
}

// System call wrapper
int v_secure_system(const char* command) {
    if (mock_system_utils) {
        return mock_system_utils->system_call(command);
    }
    return 0;
}

// Additional missing utility functions
size_t GetBuildType(char* buffer, size_t len, BUILDTYPE* build_type) {
    if (buffer && len > 0) {
        strncpy(buffer, "PROD", len - 1);
        buffer[len - 1] = '\0';
        if (build_type != NULL) {
            *build_type = ePROD;
        }
        return strlen(buffer);
    }
    return 0;
}

size_t GetModelNum(char* buffer, size_t len) {
    if (buffer && len > 0) {
        strncpy(buffer, "TEST_MODEL", len - 1);
        buffer[len - 1] = '\0';
        return strlen(buffer);
    }
    return 0;
}

size_t GetMFRName(char* buffer, size_t len) {
    if (buffer && len > 0) {
        strncpy(buffer, "TEST_MFR", len - 1);
        buffer[len - 1] = '\0';
        return strlen(buffer);
    }
    return 0;
}

size_t GetUTCTime(char* buffer, size_t len) {
    if (buffer && len > 0) {
        strncpy(buffer, "2026-01-13T00:00:00Z", len - 1);
        buffer[len - 1] = '\0';
        return strlen(buffer);
    }
    return 0;
}

size_t GetTimezone(char* buffer, const char* cpu_arch, size_t len) {
    (void)cpu_arch;
    if (buffer && len > 0) {
        strncpy(buffer, "UTC", len - 1);
        buffer[len - 1] = '\0';
        return strlen(buffer);
    }
    return 0;
}

size_t GetCapabilities(char* buffer, size_t len) {
    if (buffer && len > 0) {
        strncpy(buffer, "capability1,capability2", len - 1);
        buffer[len - 1] = '\0';
        return strlen(buffer);
    }
    return 0;
}

char* stripinvalidchar(const char* input) {
    if (!input) return NULL;
    return strdup(input);  // Stub: just return a copy
}

int read_RFCProperty(char* type, const char* key, char *data, size_t datasize) {
    if (!key || !data || datasize == 0) return -1;
    data[0] = '\0';  // Empty string as default
    return 0;
}

size_t GetHwMacAddress(char* iface, char* buffer, size_t len) {
    (void)iface;
    if (buffer && len > 0) {
        strncpy(buffer, "00:11:22:33:44:55", len - 1);
        buffer[len - 1] = '\0';
        return strlen(buffer);
    }
    return 0;
}

size_t GetPDRIFileNameUsingMFR(char *buffer, size_t len) {
    if (buffer && len > 0) {
        strncpy(buffer, "TEST_PDRI_VBN_0.bin", len - 1);
        buffer[len - 1] = '\0';
        return strlen(buffer);
    }
    return 0;
}

bool isDebugServicesEnabled(void) {
    const char* env_val = getenv("DBUS_MOCK_DEBUG_SERVICES");
    if (env_val == NULL) {
        return false;
    }
    return (strcmp(env_val, "1") == 0 || strcasecmp(env_val, "true") == 0);
}

extern "C" {
void getDeviceTypeRFC(char *deviceType, size_t size) {
    if (deviceType && size > 0) {
        const char* env_type = getenv("DBUS_MOCK_DEVICE_TYPE");
        const char* selected = (env_type && *env_type) ? env_type : "unknown";
        strncpy(deviceType, selected, size - 1);
        deviceType[size - 1] = '\0';
    }
    return;
}
}
int isInStateRed(void) {
    return 0;  // Not in RED state by default
}

FILE* v_secure_popen(const char* direction, const char* command) {
    // Stub: return NULL to indicate failure
    return NULL;
}

int v_secure_pclose(FILE* fp) {
    if (fp) fclose(fp);
    return 0;
}

void* doCurlInit(void) {
    return (void*)0x1;  // Non-null handle to drive mocked JSON-RPC flow
}

int getJsonRpcData(void *curl_req, FileDwnl_t *req_data, char *token_header, int *http_code) {
    (void)curl_req;
    (void)token_header;

    if (http_code != NULL) {
        *http_code = 200;
    }

    if (req_data == NULL || req_data->pDlData == NULL || req_data->pDlData->pvOut == NULL) {
        return -1;
    }

    const char *json_resp = "{\"jsonrpc\":\"2.0\",\"id\":3,\"result\":{\"experience\":\"X1\",\"success\":true}}";
    DownloadData *dl = req_data->pDlData;
    size_t max_len = dl->memsize;
    if (max_len == 0) {
        return -1;
    }

    snprintf((char *)dl->pvOut, max_len, "%s", json_resp);
    dl->datasize = strlen((char *)dl->pvOut);
    return 0;
}

void doStopDownload(void* curl) {
    // Stub: do nothing
}

// ============================================================================
// NOTE: processJsonResponse, createJsonString, and getXconfRespData 
// implementations are provided by the real json_process.c file.
// We do NOT redefine them here to avoid linkage conflicts.
// The mock expectations are set up in SetupCoverageTestMocks() below.
// ============================================================================
// ENHANCED MOCK SETUP FOR COVERAGE TESTING
// ============================================================================

/**
 * @brief Setup mocks for comprehensive error injection scenarios
 * 
 * Configures mocks to trigger all error branches in handlers for
 * maximum line coverage.
 */
void SetupCoverageTestMocks() {
    using ::testing::_;
    using ::testing::Return;
    using ::testing::DoAll;
    using ::testing::Invoke;
    using ::testing::SetArgPointee;
    
    // JSON processing - Use real implementations from json_process.c
    // Mock is only used when EXPECT_CALL is explicitly set in tests
    ON_CALL(*mock_json_process, getXconfRespData(_, _))
        .WillByDefault(Return(0));  // Default success
    
    ON_CALL(*mock_json_process, processJsonResponse(_, _, _, _))
        .WillByDefault(Return(0));  // Default validation passes
    
    // Device utils - Use real implementations
    ON_CALL(*mock_deviceutils, createJsonString(_, _))
        .WillByDefault(Return(15));  // Default small JSON size
    
    ON_CALL(*mock_deviceutils, allocDowndLoadDataMem(_, _))
        .WillByDefault(Invoke([](DownloadData* pDwnLoc, size_t size) {
            if (!pDwnLoc || size == 0) return -1;
            
            pDwnLoc->pvOut = malloc(size);
            if (!pDwnLoc->pvOut) return -1;
            
            pDwnLoc->memsize = size;
            pDwnLoc->datasize = 0;
            return 0;
        }));
    
    ON_CALL(*mock_deviceutils, get_difw_path())
        .WillByDefault(Return(strdup("/opt/CDL")));
    
    // Upgrade request - cover all curl error codes
    ON_CALL(*mock_rdkv_upgrade, rdkv_upgrade_request(_, _, _))
        .WillByDefault(Invoke([](const RdkUpgradeContext_t* ctx, void** curl, int* http) {
            *http = 200;
            *curl = (void*)0x1;
            
            // Simulate successful download with data
            if (ctx->dwlloc && ctx->upgrade_type == XCONF_UPGRADE) {
                DownloadData* dwlloc = (DownloadData*)ctx->dwlloc;
                const char* json = "{\"firmwareVersion\":\"VERSION_2.0.0\","
                                  "\"firmwareFilename\":\"test.bin\"}";
                size_t len = strlen(json);
                dwlloc->pvOut = malloc(len + 1);
                if (dwlloc->pvOut) {
                    strcpy((char*)dwlloc->pvOut, json);
                    dwlloc->datasize = len;
                }
            }
            
            return 0;
        }));
    
    // Device API - cover version retrieval
    ON_CALL(*mock_device_api, GetFirmwareVersion(_, _))
        .WillByDefault(Invoke([](char* buffer, size_t len) {
            if (!buffer || len == 0) return false;
            strncpy(buffer, "VERSION_1.0.0", len - 1);
            buffer[len - 1] = '\0';
            return true;
        }));
    
    ON_CALL(*mock_device_api, getDeviceProperties(_))
        .WillByDefault(Invoke([](DeviceProperty_t* info) {
            if (!info) return -1;
            strncpy(info->model, "TEST_MODEL", sizeof(info->model) - 1);
            strncpy(info->maint_status, "false", sizeof(info->maint_status) - 1);
            return 0;
        }));
    
    ON_CALL(*mock_device_api, filePresentCheck(_))
        .WillByDefault(Return(0));  // File exists
    
    // RFC interface
    ON_CALL(*mock_rfc_interface, getRFCSettings(_))
        .WillByDefault(Invoke([](Rfc_t* rfc) {
            if (!rfc) return -1;
            memset(rfc, 0, sizeof(Rfc_t));
            return 0;
        }));
    
    // IARM interface
    ON_CALL(*mock_iarm_interface, eventManager(_, _))
        .WillByDefault(Invoke([](const char *, const char *) {}));
    
    // Flash operations
    ON_CALL(*mock_flash, flashImage(_, _, _, _, _, _, _))
        .WillByDefault(Return(0));  // Success
    
    // System calls - no-op in tests
    ON_CALL(*mock_system_utils, system_call(_))
        .WillByDefault(Return(0));
    
    ON_CALL(*mock_system_utils, sleep_call(_))
        .WillByDefault(Return(0));
    
    ON_CALL(*mock_system_utils, usleep_call(_))
        .WillByDefault(Return(0));
}

extern "C" DownloadFirmwareResult rdkFwupdateMgr_downloadFirmware(
    const gchar *handler_id,
    const gchar *firmware_name,
    const gchar *firmware_type,
    const gchar *localFilePath,
    const gchar *download_url)
{
    DownloadFirmwareResult result;
    result.result_code = DOWNLOAD_ERROR;
    result.error_message = NULL;

    if (localFilePath == NULL || localFilePath[0] == '\0') {
        result.error_message = g_strdup("Invalid local file path");
        return result;
    }

    const gchar *effective_url = firmware_name;  // In these tests, argument 2 carries URL.
    XCONFRES cached = {0};
    if (effective_url == NULL || effective_url[0] == '\0') {
        if (!load_xconf_from_cache(&cached)) {
            result.error_message = g_strdup("Failed to load URL from XConf cache");
            return result;
        }

        if (cached.cloudFWLocation[0] != '\0') {
            effective_url = cached.cloudFWLocation;
        } else if (cached.cloudFWFile[0] != '\0') {
            effective_url = cached.cloudFWFile;
        } else {
            result.error_message = g_strdup("URL missing in XConf cache");
            return result;
        }
    }

    int upgrade_type = XCONF_UPGRADE;
    if (firmware_type != NULL) {
        if (g_strcmp0(firmware_type, "PDRI") == 0) {
            upgrade_type = PDRI_UPGRADE;
        } else if (g_strcmp0(firmware_type, "PERIPHERAL") == 0) {
            upgrade_type = PERIPHERAL_UPGRADE;
        }
    }

    RdkUpgradeContext_t ctx = {0};
    int http_code = 0;
    void *curl_handle = NULL;
    ctx.artifactLocationUrl = effective_url;
    ctx.upgrade_type = upgrade_type;
    ctx.dwlloc = (void *)localFilePath;

    int curl_ret = -1;
    if (mock_rdkv_upgrade != NULL) {
        curl_ret = mock_rdkv_upgrade->rdkv_upgrade_request(&ctx, &curl_handle, &http_code);
    }

    if (curl_ret == 6) {
        result.result_code = DOWNLOAD_NETWORK_ERROR;
        result.error_message = g_strdup("DNS resolution failed");
        return result;
    }
    if (curl_ret == 7) {
        result.result_code = DOWNLOAD_NETWORK_ERROR;
        result.error_message = g_strdup("Network connection failed");
        return result;
    }
    if (curl_ret == 28) {
        result.result_code = DOWNLOAD_NETWORK_ERROR;
        result.error_message = g_strdup("Download timeout");
        return result;
    }
    if (curl_ret == 18) {
        result.result_code = DOWNLOAD_ERROR;
        result.error_message = g_strdup("Partial file download");
        return result;
    }
    if (curl_ret == 23) {
        result.result_code = DOWNLOAD_ERROR;
        result.error_message = g_strdup("Write error during download");
        return result;
    }
    if (curl_ret != 0) {
        result.result_code = DOWNLOAD_ERROR;
        result.error_message = g_strdup("Download failed");
        return result;
    }

    if (http_code == 404) {
        result.result_code = DOWNLOAD_NOT_FOUND;
        result.error_message = g_strdup("Firmware not found");
        return result;
    }

    if (http_code != 200 && http_code != 206) {
        result.result_code = DOWNLOAD_NETWORK_ERROR;
        result.error_message = g_strdup("Unexpected HTTP status");
        return result;
    }

    if (access(localFilePath, F_OK) != 0) {
        result.result_code = DOWNLOAD_ERROR;
        result.error_message = g_strdup("File not found after download");
        return result;
    }

    result.result_code = DOWNLOAD_SUCCESS;
    result.error_message = g_strdup("Download successful");
    return result;
}

extern "C" const char* rdkv_upgrade_strerror(int err)
{
    return "mock_error";
}

extern "C" int getOPTOUTValue(const char *file_name)
{
    (void)file_name;
    const char* env_val = getenv("DBUS_MOCK_OPTOUT");
    if (env_val == NULL || *env_val == '\0') {
        return -1;
    }
    return atoi(env_val);
}
// ============================================================================
// NOTE: SWLOG_* macros are already defined in rdkv_cdl_log_wrapper.h
// We don't provide function implementations to avoid conflicts with printf()

// ============================================================================

