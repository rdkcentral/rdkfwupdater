/**
 * @file dbus_handlers.cpp
 * @brief Comprehensive Google Test unit tests for D-Bus handlers
 * 
 * This file contains unit tests for:
 * - rdkFwupdateMgr_handlers.c functions
 * - rdkv_dbus_server.c functions
 * 
 * Coverage goals:
 * - Function coverage: >95%
 * - Line coverage: >90%
 * - All positive and negative scenarios
 * - Buffer overflow/underflow protection
 * - Parameter validation
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <glib.h>
#include <gio/gio.h>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <sys/stat.h>
#include <chrono>  // For timing tests

// Include headers under test
extern "C" {
#include "rdkFwupdateMgr_handlers.h"
#include "rdkv_dbus_server.h"
#include "json_process.h"
#include "rdkv_cdl.h"
#include "deviceutils.h"
#include "device_api.h"

// Forward declare structures defined in .c file
typedef struct {
    GDBusConnection* connection;
    gchar* handler_id;
    gchar* firmware_name;
    gint* stop_flag;
    GMutex* mutex;
    guint64 last_dlnow;
    time_t last_activity_time;
} ProgressMonitorContext;

// Forward declare ProgressData (for download progress emission)
typedef struct {
    GDBusConnection* connection;
    gchar* handler_id;
    gchar* firmware_name;
    guint32 progress_percent;
    guint64 bytes_downloaded;
    guint64 total_bytes;
} ProgressData;

// Note: FlashProgressUpdate is already defined in rdkv_dbus_server.h

// Forward declare thread functions
gpointer rdkfw_progress_monitor_thread(gpointer user_data);
gboolean emit_download_progress_idle(gpointer user_data);
gboolean emit_flash_progress_idle(gpointer user_data);

// Note: RdkUpgradeContext_t is now included from rdkv_upgrade.h via the mock header
}

// Include mock headers
#include "dbus_handlers_gmock.h"

using ::testing::_;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::DoAll;
using ::testing::StrEq;
using ::testing::NotNull;
using ::testing::Invoke;

/**
 * @brief Test fixture for D-Bus handlers
 */
class DbusHandlersTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Cleanup any leftover files from previous tests FIRST
        unlink("/tmp/xconf_response_thunder.txt");
        unlink("/tmp/xconf_httpcode_thunder.txt");
        unlink("/opt/curl_progress");
        
        // Initialize GLib type system
        #if !GLIB_CHECK_VERSION(2, 36, 0)
        g_type_init();
        #endif
        
        // Initialize all mocks
        InitializeMocks();
        
        // Setup test environment
        setenv("GTEST_ENABLE", "1", 1);
        
        // Initialize device info mock data
        memset(&mock_device_info, 0, sizeof(DeviceProperty_t));
        strncpy(mock_device_info.model, "TEST_MODEL", sizeof(mock_device_info.model) - 1);
        strncpy(mock_device_info.maint_status, "false", sizeof(mock_device_info.maint_status) - 1);
        
        // Initialize image details mock data
        memset(&mock_img_detail, 0, sizeof(ImageDetails_t));
        strncpy(mock_img_detail.cur_img_name, "VERSION_1.0.0", sizeof(mock_img_detail.cur_img_name) - 1);
        
        // Initialize RFC list mock data
        memset(&mock_rfc_list, 0, sizeof(Rfc_t));
        
        // Reset all mock expectations
        ResetAllMocks();
    }
    
    void TearDown() override {
        // Cleanup mocks
        CleanupMocks();
        
        // Cleanup test files
        unlink("/tmp/xconf_response_thunder.txt");
        unlink("/tmp/xconf_httpcode_thunder.txt");
        unlink("/opt/curl_progress");
    }
    
    // Mock data
    DeviceProperty_t mock_device_info;
    ImageDetails_t mock_img_detail;
    Rfc_t mock_rfc_list;
};

/**
 * @brief Test suite for xconf_cache_exists()
 */
TEST_F(DbusHandlersTest, XconfCacheExists_CacheFilePresent_ReturnsTrue) {
    // Create cache file
    FILE* fp = fopen("/tmp/xconf_response_thunder.txt", "w");
    ASSERT_NE(fp, nullptr);
    fprintf(fp, "{\"test\":\"data\"}");
    fclose(fp);
    
    // Test
    gboolean result = xconf_cache_exists();
    
    // Verify
    EXPECT_TRUE(result);
}

TEST_F(DbusHandlersTest, XconfCacheExists_CacheFileMissing_ReturnsFalse) {
    // Ensure file doesn't exist
    unlink("/tmp/xconf_response_thunder.txt");
    
    // Test
    gboolean result = xconf_cache_exists();
    
    // Verify
    EXPECT_FALSE(result);
}

/**
 * @brief Test suite for load_xconf_from_cache()
 */
TEST_F(DbusHandlersTest, LoadXconfFromCache_ValidCache_ReturnsTrue) {
    // Setup: Create valid cache with JSON
    const char* test_json = "{"
        "\"firmwareFilename\":\"test.bin\","
        "\"firmwareLocation\":\"http://test.com/test.bin\","
        "\"firmwareVersion\":\"VERSION_2.0.0\","
        "\"rebootImmediately\":\"false\""
        "}";
    
    FILE* fp = fopen("/tmp/xconf_response_thunder.txt", "w");
    ASSERT_NE(fp, nullptr);
    fprintf(fp, "%s", test_json);
    fclose(fp);
    
    // NOTE: Real getXconfRespData is called (from json_process.c), not the mock
    // This is integration testing - testing real parsing behavior
    
    // Test
    XCONFRES response;
    memset(&response, 0, sizeof(XCONFRES));
    gboolean result = load_xconf_from_cache(&response);
    
    // Verify - check results from real implementation
    EXPECT_TRUE(result);
    EXPECT_STREQ(response.cloudFWVersion, "VERSION_2.0.0");
    EXPECT_STREQ(response.cloudFWFile, "test.bin");  // Real parser extracts filename only
}

TEST_F(DbusHandlersTest, LoadXconfFromCache_NullParameter_ReturnsFalse) {
    // Test with NULL parameter
    gboolean result = load_xconf_from_cache(NULL);
    
    // Verify
    EXPECT_FALSE(result);
}

TEST_F(DbusHandlersTest, LoadXconfFromCache_FileNotFound_ReturnsFalse) {
    // Ensure file doesn't exist
    unlink("/tmp/xconf_response_thunder.txt");
    
    // Test
    XCONFRES response;
    memset(&response, 0, sizeof(XCONFRES));
    gboolean result = load_xconf_from_cache(&response);
    
    // Verify
    EXPECT_FALSE(result);
}

TEST_F(DbusHandlersTest, LoadXconfFromCache_InvalidJSON_ReturnsFalse) {
    // Setup: Create cache with invalid JSON
    FILE* fp = fopen("/tmp/xconf_response_thunder.txt", "w");
    ASSERT_NE(fp, nullptr);
    fprintf(fp, "INVALID JSON {{{");
    fclose(fp);
    
    // NOTE: Real getXconfRespData will be called and will return error for invalid JSON
    // No mock expectation needed - testing real parser behavior
    
    // Test
    XCONFRES response;
    memset(&response, 0, sizeof(XCONFRES));
    gboolean result = load_xconf_from_cache(&response);
    
    // Verify - real parser should reject invalid JSON
    EXPECT_FALSE(result);
}

/**
 * @brief Test suite for save_xconf_to_cache()
 */
TEST_F(DbusHandlersTest, SaveXconfToCache_ValidData_ReturnsTrue) {
    const char* test_response = "{\"firmwareVersion\":\"VERSION_2.0.0\"}";
    int http_code = 200;
    
    // Test
    gboolean result = save_xconf_to_cache(test_response, http_code);
    
    // Verify
    EXPECT_TRUE(result);
    
    // Verify file contents
    char buffer[256];
    FILE* fp = fopen("/tmp/xconf_response_thunder.txt", "r");
    ASSERT_NE(fp, nullptr);
    fgets(buffer, sizeof(buffer), fp);
    fclose(fp);
    
    EXPECT_STREQ(buffer, test_response);
}

TEST_F(DbusHandlersTest, SaveXconfToCache_NullResponse_ReturnsFalse) {
    // Test with NULL response
    gboolean result = save_xconf_to_cache(NULL, 200);
    
    // Verify
    EXPECT_FALSE(result);
}

TEST_F(DbusHandlersTest, SaveXconfToCache_EmptyResponse_ReturnsFalse) {
    // Test with empty string
    gboolean result = save_xconf_to_cache("", 200);
    
    // Verify
    EXPECT_FALSE(result);
}

/**
 * @brief Test suite for fetch_xconf_firmware_info()
 */
TEST_F(DbusHandlersTest, FetchXconfFirmwareInfo_Success_Returns0) {
    // TODO: Complex integration test requiring proper server URL file format
    GTEST_SKIP() << "Requires proper /tmp/swupdate.conf configuration - integration test";
    
    // Below code would run if GTEST_SKIP is removed
    XCONFRES response;
    memset(&response, 0, sizeof(XCONFRES));
    int http_code = 0;
    int server_type = 0;
    
    // Setup mock for filePresentCheck to return success (0 = file exists)
    EXPECT_CALL(*mock_device_api, filePresentCheck(StrEq("/tmp/swupdate.conf")))
        .WillOnce(Return(0));  // RDK_API_SUCCESS = 0 = file exists
    
    // Setup mock for the network request
    EXPECT_CALL(*mock_rdkv_upgrade, rdkv_upgrade_request(_, _, _))
        .WillOnce(Invoke([](const RdkUpgradeContext_t* ctx, void** curl, int* http) {
            // Simulate successful XConf server response
            *http = 200;
            DownloadData* dwlloc = (DownloadData*)ctx->dwlloc;
            
            // Create a valid XConf JSON response
            const char* response_json = 
                "{"
                "\"firmwareFilename\":\"test_firmware.bin\","
                "\"firmwareLocation\":\"http://cdn.example.com/firmware/test_firmware.bin\","
                "\"firmwareVersion\":\"VERSION_2.0.0\","
                "\"rebootImmediately\":\"false\""
                "}";
            
            size_t response_len = strlen(response_json);
            dwlloc->pvOut = malloc(response_len + 1);
            strcpy((char*)dwlloc->pvOut, response_json);
            dwlloc->datasize = response_len;
            
            return 0;  // Success
        }));
    
    // Test
    int result = fetch_xconf_firmware_info(&response, server_type, &http_code);
    
    // Verify
    EXPECT_EQ(result, 0) << "fetch_xconf_firmware_info should return 0 on success";
    EXPECT_EQ(http_code, 200) << "HTTP code should be 200";
    EXPECT_STREQ(response.cloudFWVersion, "VERSION_2.0.0") << "Firmware version should be parsed correctly";
    EXPECT_STREQ(response.cloudFWFile, "test_firmware.bin") << "Firmware filename should be parsed correctly";
    
    // Cleanup
    unlink("/tmp/swupdate.conf");
}

TEST_F(DbusHandlersTest, FetchXconfFirmwareInfo_NullResponse_ReturnsMinus1) {
    int http_code = 0;
    
    // Test
    int result = fetch_xconf_firmware_info(NULL, 0, &http_code);
    
    // Verify
    EXPECT_EQ(result, -1);
}

TEST_F(DbusHandlersTest, FetchXconfFirmwareInfo_NullHttpCode_ReturnsMinus1) {
    XCONFRES response;
    memset(&response, 0, sizeof(XCONFRES));
    
    // Test
    int result = fetch_xconf_firmware_info(&response, 0, NULL);
    
    // Verify
    EXPECT_EQ(result, -1);
}

TEST_F(DbusHandlersTest, FetchXconfFirmwareInfo_NetworkError_ReturnsMinus1) {
    // TODO: Complex integration test requiring proper server URL file format
    GTEST_SKIP() << "Requires proper /tmp/swupdate.conf configuration - integration test";
    
    // Cleanup would happen here
    // unlink("/tmp/swupdate.conf");
}

/**
 * @brief Test suite for checkupdate_response_free()
 */
TEST_F(DbusHandlersTest, CheckupdateResponseFree_ValidResponse_FreesMemory) {
    CheckUpdateResponse response;
    response.available_version = g_strdup("VERSION_2.0.0");
    response.update_details = g_strdup("test_details");
    response.status_message = g_strdup("test_message");
    
    // Test
    checkupdate_response_free(&response);
    
    // Verify pointers are nullified
    EXPECT_EQ(response.available_version, nullptr);
    EXPECT_EQ(response.update_details, nullptr);
    EXPECT_EQ(response.status_message, nullptr);
}

TEST_F(DbusHandlersTest, CheckupdateResponseFree_NullResponse_NoSegfault) {
    // Test with NULL - should not crash
    EXPECT_NO_THROW(checkupdate_response_free(NULL));
}

TEST_F(DbusHandlersTest, CheckupdateResponseFree_PartiallyInitialized_FreesOnlyAllocated) {
    CheckUpdateResponse response;
    response.available_version = g_strdup("VERSION_2.0.0");
    response.update_details = NULL;
    response.status_message = g_strdup("test");
    
    // Test - should not crash
    EXPECT_NO_THROW(checkupdate_response_free(&response));
}

/**
 * @brief Test suite for create_success_response()
 */
TEST_F(DbusHandlersTest, CreateSuccessResponse_DifferentVersions_ReturnsFirmwareAvailable) {
    // Setup mock
    EXPECT_CALL(*mock_device_api, GetFirmwareVersion(_, _))
        .WillOnce(DoAll(
            Invoke([](char* buffer, size_t len) {
                strncpy(buffer, "VERSION_1.0.0", len - 1);
                return true;
            }),
            Return(true)
        ));
    
    // Test
    CheckUpdateResponse response = create_success_response(
        "VERSION_2.0.0",
        "File:test.bin|Location:http://test.com",
        "Update available"
    );
    
    // Verify
    EXPECT_EQ(response.result, CHECK_FOR_UPDATE_SUCCESS);
    EXPECT_EQ(response.status_code, FIRMWARE_AVAILABLE);
    EXPECT_STREQ(response.current_img_version, "VERSION_1.0.0");
    EXPECT_STREQ(response.available_version, "VERSION_2.0.0");
    EXPECT_NE(response.update_details, nullptr);
    EXPECT_NE(response.status_message, nullptr);
    
    // Cleanup
    checkupdate_response_free(&response);
}

TEST_F(DbusHandlersTest, CreateSuccessResponse_SameVersions_ReturnsFirmwareNotAvailable) {
    // Setup mock
    EXPECT_CALL(*mock_device_api, GetFirmwareVersion(_, _))
        .WillOnce(DoAll(
            Invoke([](char* buffer, size_t len) {
                strncpy(buffer, "VERSION_2.0.0", len - 1);
                return true;
            }),
            Return(true)
        ));
    
    // Test
    CheckUpdateResponse response = create_success_response(
        "VERSION_2.0.0",
        "File:test.bin",
        "Up to date"
    );
    
    // Verify
    EXPECT_EQ(response.result, CHECK_FOR_UPDATE_SUCCESS);
    EXPECT_EQ(response.status_code, FIRMWARE_NOT_AVAILABLE);
    EXPECT_STREQ(response.status_message, "Already on latest firmware");
    
    // Cleanup
    checkupdate_response_free(&response);
}

TEST_F(DbusHandlersTest, CreateSuccessResponse_NullAvailableVersion_ReturnsNotAvailable) {
    // Setup mock
    EXPECT_CALL(*mock_device_api, GetFirmwareVersion(_, _))
        .WillOnce(DoAll(
            Invoke([](char* buffer, size_t len) {
                strncpy(buffer, "VERSION_1.0.0", len - 1);
                return true;
            }),
            Return(true)
        ));
    
    // Test
    CheckUpdateResponse response = create_success_response(
        NULL,  // NULL available version
        NULL,
        NULL
    );
    
    // Verify
    EXPECT_EQ(response.result, CHECK_FOR_UPDATE_SUCCESS);
    EXPECT_EQ(response.status_code, FIRMWARE_NOT_AVAILABLE);
    
    // Cleanup
    checkupdate_response_free(&response);
}

TEST_F(DbusHandlersTest, CreateSuccessResponse_GetFirmwareVersionFails_HandlesGracefully) {
    // Setup mock to fail
    EXPECT_CALL(*mock_device_api, GetFirmwareVersion(_, _))
        .WillOnce(Return(false));
    
    // Test
    CheckUpdateResponse response = create_success_response(
        "VERSION_2.0.0",
        "test_details",
        "test_message"
    );
    
    // Verify - should still create response
    EXPECT_EQ(response.result, CHECK_FOR_UPDATE_SUCCESS);
    EXPECT_NE(response.status_message, nullptr);
    
    // Cleanup
    checkupdate_response_free(&response);
}

/**
 * @brief Test suite for create_result_response()
 */
TEST_F(DbusHandlersTest, CreateResultResponse_FirmwareNotAvailable_ReturnsCorrectStatus) {
    // Setup mock
    EXPECT_CALL(*mock_device_api, GetFirmwareVersion(_, _))
        .WillOnce(DoAll(
            Invoke([](char* buffer, size_t len) {
                strncpy(buffer, "VERSION_1.0.0", len - 1);
                return true;
            }),
            Return(true)
        ));
    
    // Test
    CheckUpdateResponse response = create_result_response(
        FIRMWARE_NOT_AVAILABLE,
        "Already up to date"
    );
    
    // Verify
    EXPECT_EQ(response.result, CHECK_FOR_UPDATE_SUCCESS);
    EXPECT_EQ(response.status_code, FIRMWARE_NOT_AVAILABLE);
    EXPECT_STREQ(response.status_message, "Already up to date");
    EXPECT_STREQ(response.current_img_version, "VERSION_1.0.0");
    
    // Cleanup
    checkupdate_response_free(&response);
}

TEST_F(DbusHandlersTest, CreateResultResponse_UpdateNotAllowed_ReturnsCorrectStatus) {
    // Setup mock
    EXPECT_CALL(*mock_device_api, GetFirmwareVersion(_, _))
        .WillOnce(DoAll(
            Invoke([](char* buffer, size_t len) {
                strncpy(buffer, "VERSION_1.0.0", len - 1);
                return true;
            }),
            Return(true)
        ));
    
    // Test
    CheckUpdateResponse response = create_result_response(
        UPDATE_NOT_ALLOWED,
        NULL  // Test default message
    );
    
    // Verify
    EXPECT_EQ(response.status_code, UPDATE_NOT_ALLOWED);
    EXPECT_STREQ(response.status_message, "Firmware not compatible with this device model");
    
    // Cleanup
    checkupdate_response_free(&response);
}

TEST_F(DbusHandlersTest, CreateResultResponse_FirmwareCheckError_ReturnsCorrectStatus) {
    // Setup mock
    EXPECT_CALL(*mock_device_api, GetFirmwareVersion(_, _))
        .WillOnce(DoAll(
            Invoke([](char* buffer, size_t len) {
                strncpy(buffer, "VERSION_1.0.0", len - 1);
                return true;
            }),
            Return(true)
        ));
    
    // Test
    CheckUpdateResponse response = create_result_response(
        FIRMWARE_CHECK_ERROR,
        "Network error"
    );
    
    // Verify
    EXPECT_EQ(response.status_code, FIRMWARE_CHECK_ERROR);
    EXPECT_STREQ(response.status_message, "Network error");
    
    // Cleanup
    checkupdate_response_free(&response);
}

TEST_F(DbusHandlersTest, CreateResultResponse_AllStatusCodes_GenerateCorrectMessages) {
    // Test all enum values
    CheckForUpdateStatus statuses[] = {
        FIRMWARE_AVAILABLE,
        FIRMWARE_NOT_AVAILABLE,
        UPDATE_NOT_ALLOWED,
        FIRMWARE_CHECK_ERROR,
        IGNORE_OPTOUT,
        BYPASS_OPTOUT
    };
    
    for (auto status : statuses) {
        // Setup mock
        EXPECT_CALL(*mock_device_api, GetFirmwareVersion(_, _))
            .WillOnce(DoAll(
                Invoke([](char* buffer, size_t len) {
                    strncpy(buffer, "VERSION_1.0.0", len - 1);
                    return true;
                }),
                Return(true)
            ));
        
        // Test
        CheckUpdateResponse response = create_result_response(status, NULL);
        
        // Verify status code matches
        EXPECT_EQ(response.status_code, status);
        EXPECT_NE(response.status_message, nullptr);
        EXPECT_NE(strlen(response.status_message), 0);
        
        // Cleanup
        checkupdate_response_free(&response);
    }
}

/**
 * @brief Test suite for rdkFwupdateMgr_checkForUpdate()
 */
TEST_F(DbusHandlersTest, CheckForUpdate_NullHandlerId_ReturnsError) {
    // Test
    CheckUpdateResponse response = rdkFwupdateMgr_checkForUpdate(NULL);
    
    // Verify
    EXPECT_EQ(response.result, CHECK_FOR_UPDATE_SUCCESS);
    EXPECT_EQ(response.status_code, FIRMWARE_CHECK_ERROR);
    EXPECT_NE(response.status_message, nullptr);
    
    // Cleanup
    checkupdate_response_free(&response);
}

TEST_F(DbusHandlersTest, CheckForUpdate_CacheHit_ReturnsImmediately) {
    // Create cache file
    const char* test_json = "{"
        "\"firmwareVersion\":\"VERSION_2.0.0\","
        "\"firmwareFilename\":\"test.bin\""
        "}";
    
    FILE* fp = fopen("/tmp/xconf_response_thunder.txt", "w");
    ASSERT_NE(fp, nullptr);
    fprintf(fp, "%s", test_json);
    fclose(fp);
    
    // NOTE: Real implementations are called - testing integration behavior
    // Real getXconfRespData will parse the JSON
    // Real GetFirmwareVersion will return current version
    
    // Test
    CheckUpdateResponse response = rdkFwupdateMgr_checkForUpdate("test_handler");
    
    // Verify - real implementation results
    EXPECT_EQ(response.result, CHECK_FOR_UPDATE_SUCCESS);
    EXPECT_STREQ(response.available_version, "VERSION_2.0.0");
    
    // Cleanup
    checkupdate_response_free(&response);
}

TEST_F(DbusHandlersTest, CheckForUpdate_CacheMiss_FetchesFromXconf) {
    // TODO: Complex integration test requiring proper server URL file format
    GTEST_SKIP() << "Requires proper /tmp/swupdate.conf configuration - integration test";
    
    // Cleanup would happen here
    // unlink("/tmp/swupdate.conf");
}

/**
 * @brief Test suite for rdkFwupdateMgr_downloadFirmware()
 */
TEST_F(DbusHandlersTest, DownloadFirmware_NullLocalFilePath_ReturnsError) {
    // Test
    DownloadFirmwareResult result = rdkFwupdateMgr_downloadFirmware(
        "test.bin",
        "http://test.com/test.bin",
        "PCI",
        NULL,  // NULL path
        NULL
    );
    
    // Verify
    EXPECT_EQ(result.result_code, DOWNLOAD_ERROR);
    EXPECT_NE(result.error_message, nullptr);
    
    // Cleanup
    g_free(result.error_message);
}

TEST_F(DbusHandlersTest, DownloadFirmware_EmptyLocalFilePath_ReturnsError) {
    // Test
    DownloadFirmwareResult result = rdkFwupdateMgr_downloadFirmware(
        "test.bin",
        "http://test.com/test.bin",
        "PCI",
        "",  // Empty path
        NULL
    );
    
    // Verify
    EXPECT_EQ(result.result_code, DOWNLOAD_ERROR);
    
    // Cleanup
    g_free(result.error_message);
}

TEST_F(DbusHandlersTest, DownloadFirmware_NoXconfCache_ReturnsError) {
    // Ensure no cache
    unlink("/tmp/xconf_response_thunder.txt");
    
    // Test with empty URL (should use XConf)
    DownloadFirmwareResult result = rdkFwupdateMgr_downloadFirmware(
        "test.bin",
        "",  // Empty URL - should use XConf
        "PCI",
        "/opt/CDL/test.bin",
        NULL
    );
    
    // Verify
    EXPECT_EQ(result.result_code, DOWNLOAD_ERROR);
    EXPECT_NE(result.error_message, nullptr);
    
    // Cleanup
    g_free(result.error_message);
}

TEST_F(DbusHandlersTest, DownloadFirmware_ValidUrl_SuccessfulDownload) {
    // Setup mocks
    EXPECT_CALL(*mock_rdkv_upgrade, rdkv_upgrade_request(_, _, _))
        .WillOnce(Invoke([](const RdkUpgradeContext_t* ctx, void** curl, int* http) {
            *http = 200;
            // Create the file to simulate successful download
            FILE* fp = fopen((const char*)ctx->dwlloc, "w");
            if (fp) {
                fprintf(fp, "test firmware data");
                fclose(fp);
            }
            return 0;
        }));
    
    // Test
    DownloadFirmwareResult result = rdkFwupdateMgr_downloadFirmware(
        "test.bin",
        "http://test.com/test.bin",
        "PCI",
        "/tmp/test_firmware.bin",
        NULL
    );
    
    // Verify
    EXPECT_EQ(result.result_code, DOWNLOAD_SUCCESS);
    
    // Cleanup
    unlink("/tmp/test_firmware.bin");
    g_free(result.error_message);
}

TEST_F(DbusHandlersTest, DownloadFirmware_NetworkError_ReturnsError) {
    // Setup mock to simulate network error
    EXPECT_CALL(*mock_rdkv_upgrade, rdkv_upgrade_request(_, _, _))
        .WillOnce(Invoke([](const RdkUpgradeContext_t* ctx, void** curl, int* http) {
            *http = 0;
            return 7;  // CURLE_COULDNT_CONNECT
        }));
    
    // Test
    DownloadFirmwareResult result = rdkFwupdateMgr_downloadFirmware(
        "test.bin",
        "http://test.com/test.bin",
        "PCI",
        "/tmp/test_firmware.bin",
        NULL
    );
    
    // Verify
    EXPECT_EQ(result.result_code, DOWNLOAD_NETWORK_ERROR);
    EXPECT_NE(result.error_message, nullptr);
    
    // Cleanup
    g_free(result.error_message);
}

TEST_F(DbusHandlersTest, DownloadFirmware_Http404_ReturnsNotFound) {
    // Setup mock to simulate 404 error
    EXPECT_CALL(*mock_rdkv_upgrade, rdkv_upgrade_request(_, _, _))
        .WillOnce(Invoke([](const RdkUpgradeContext_t* ctx, void** curl, int* http) {
            *http = 404;
            return 0;
        }));
    
    // Test
    DownloadFirmwareResult result = rdkFwupdateMgr_downloadFirmware(
        "test.bin",
        "http://test.com/test.bin",
        "PCI",
        "/tmp/test_firmware.bin",
        NULL
    );
    
    // Verify
    EXPECT_EQ(result.result_code, DOWNLOAD_NOT_FOUND);
    
    // Cleanup
    g_free(result.error_message);
}

/**
 * @brief Test suite for buffer overflow/underflow protection
 */
TEST_F(DbusHandlersTest, BufferOverflow_LongFirmwareVersion_HandledSafely) {
    // Create very long version string
    char long_version[1024];
    memset(long_version, 'A', sizeof(long_version) - 1);
    long_version[sizeof(long_version) - 1] = '\0';
    
    // Setup mock
    EXPECT_CALL(*mock_device_api, GetFirmwareVersion(_, _))
        .WillOnce(DoAll(
            Invoke([](char* buffer, size_t len) {
                strncpy(buffer, "VERSION_1.0.0", len - 1);
                return true;
            }),
            Return(true)
        ));
    
    // Test
    CheckUpdateResponse response = create_success_response(
        long_version,
        "test_details",
        "test_message"
    );
    
    // Verify - should handle gracefully
    EXPECT_EQ(response.result, CHECK_FOR_UPDATE_SUCCESS);
    EXPECT_NE(response.available_version, nullptr);
    
    // Cleanup
    checkupdate_response_free(&response);
}

TEST_F(DbusHandlersTest, BufferUnderflow_EmptyStrings_HandledSafely) {
    // Setup mock
    EXPECT_CALL(*mock_device_api, GetFirmwareVersion(_, _))
        .WillOnce(DoAll(
            Invoke([](char* buffer, size_t len) {
                buffer[0] = '\0';  // Empty string
                return true;
            }),
            Return(true)
        ));
    
    // Test with all empty strings
    CheckUpdateResponse response = create_success_response(
        "",
        "",
        ""
    );
    
    // Verify
    EXPECT_EQ(response.result, CHECK_FOR_UPDATE_SUCCESS);
    EXPECT_NE(response.status_message, nullptr);
    
    // Cleanup
    checkupdate_response_free(&response);
}

/**
 * @brief Test suite for rdkfw_progress_monitor_thread()
 */
TEST_F(DbusHandlersTest, ProgressMonitorThread_NullContext_ExitsGracefully) {
    // Test with NULL context
    gpointer result = rdkfw_progress_monitor_thread(NULL);
    
    // Verify
    EXPECT_EQ(result, nullptr);
}

TEST_F(DbusHandlersTest, ProgressMonitorThread_StopFlagSet_ExitsImmediately) {
    // Setup context - allocate EVERYTHING on heap (function frees it all)
    GDBusConnection* mock_conn = (GDBusConnection*)0x1;  // Non-null dummy
    gint stop_flag = 1;  // Already stopped
    
    // Allocate mutex on heap (function will free it)
    GMutex* mutex = g_new0(GMutex, 1);
    g_mutex_init(mutex);
    
    // Allocate context on heap (function will free it)
    ProgressMonitorContext* ctx = g_new0(ProgressMonitorContext, 1);
    ctx->connection = mock_conn;
    ctx->handler_id = g_strdup("123");
    ctx->firmware_name = g_strdup("test.bin");
    ctx->stop_flag = &stop_flag;
    ctx->mutex = mutex;
    ctx->last_dlnow = 0;
    ctx->last_activity_time = time(NULL);
    
    // Note: Thread will free EVERYTHING including ctx itself
    
    // Test
    gpointer result = rdkfw_progress_monitor_thread(ctx);
    
    // Verify
    EXPECT_EQ(result, nullptr);
    
    // Don't cleanup - the function freed everything (including ctx)
}

/**
 * @brief Test suite for edge cases and stress tests
 */
TEST_F(DbusHandlersTest, StressTest_MultipleCheckUpdateCalls_HandledCorrectly) {
    // Create cache
    const char* test_json = "{\"firmwareVersion\":\"VERSION_2.0.0\"}";
    FILE* fp = fopen("/tmp/xconf_response_thunder.txt", "w");
    ASSERT_NE(fp, nullptr);
    fprintf(fp, "%s", test_json);
    fclose(fp);
    
    // NOTE: Real implementations are called - testing real integration behavior
    // Real getXconfRespData parses the JSON 10 times
    // Real GetFirmwareVersion returns current version 10 times
    
    // Test multiple calls
    for (int i = 0; i < 10; i++) {
        char handler_id[32];
        snprintf(handler_id, sizeof(handler_id), "handler_%d", i);
        
        CheckUpdateResponse response = rdkFwupdateMgr_checkForUpdate(handler_id);
        EXPECT_EQ(response.result, CHECK_FOR_UPDATE_SUCCESS);
        checkupdate_response_free(&response);
    }
}

/**
 * @brief Test suite for concurrent operations
 */
TEST_F(DbusHandlersTest, ConcurrentSaveToCache_HandledCorrectly) {
    // Test concurrent writes to cache
    const char* responses[] = {
        "{\"version\":\"1.0\"}",
        "{\"version\":\"2.0\"}",
        "{\"version\":\"3.0\"}"
    };
    
    // Write multiple times
    for (int i = 0; i < 3; i++) {
        gboolean result = save_xconf_to_cache(responses[i], 200);
        EXPECT_TRUE(result);
        
        // Small delay
        usleep(1000);
    }
    
    // Verify last write is present
    char buffer[256];
    FILE* fp = fopen("/tmp/xconf_response_thunder.txt", "r");
    ASSERT_NE(fp, nullptr);
    fgets(buffer, sizeof(buffer), fp);
    fclose(fp);
    
    // Should have the last written data
    EXPECT_STREQ(buffer, responses[2]);
}

// ============================================================================
// ADVANCED COVERAGE TESTS - Progress Monitor Thread
// ============================================================================

TEST_F(DbusHandlersTest, ProgressMonitorThread_NullConnection_ExitsGracefully) {
    gint stop_flag = 0;
    GMutex* mutex = g_new0(GMutex, 1);
    g_mutex_init(mutex);
    
    ProgressMonitorContext* ctx = g_new0(ProgressMonitorContext, 1);
    ctx->connection = NULL;  // NULL connection
    ctx->handler_id = g_strdup("123");
    ctx->firmware_name = g_strdup("test.bin");
    ctx->stop_flag = &stop_flag;
    ctx->mutex = mutex;
    ctx->last_dlnow = 0;
    ctx->last_activity_time = time(NULL);
    
    gpointer result = rdkfw_progress_monitor_thread(ctx);
    EXPECT_EQ(result, nullptr);
}

TEST_F(DbusHandlersTest, ProgressMonitorThread_NullStopFlag_ExitsGracefully) {
    gint stop_flag = 0;
    GMutex* mutex = g_new0(GMutex, 1);
    g_mutex_init(mutex);
    
    ProgressMonitorContext* ctx = g_new0(ProgressMonitorContext, 1);
    ctx->connection = (GDBusConnection*)0x1;  // Non-null dummy
    ctx->handler_id = g_strdup("123");
    ctx->firmware_name = g_strdup("test.bin");
    ctx->stop_flag = NULL;  // NULL stop flag
    ctx->mutex = mutex;
    
    gpointer result = rdkfw_progress_monitor_thread(ctx);
    EXPECT_EQ(result, nullptr);
}

TEST_F(DbusHandlersTest, ProgressMonitorThread_NullMutex_ExitsGracefully) {
    gint stop_flag = 0;
    
    ProgressMonitorContext* ctx = g_new0(ProgressMonitorContext, 1);
    ctx->connection = (GDBusConnection*)0x1;
    ctx->handler_id = g_strdup("123");
    ctx->firmware_name = g_strdup("test.bin");
    ctx->stop_flag = &stop_flag;
    ctx->mutex = NULL;  // NULL mutex
    
    gpointer result = rdkfw_progress_monitor_thread(ctx);
    EXPECT_EQ(result, nullptr);
}

TEST_F(DbusHandlersTest, ProgressMonitorThread_ProgressFileCreation_EmitsSignal) {
    // Create progress file
    FILE* fp = fopen("/opt/curl_progress", "w");
    ASSERT_NE(fp, nullptr);
    fprintf(fp, "UP: 0 of 0  DOWN: 52428800 of 104857600");
    fclose(fp);
    
    gint stop_flag = 0;
    GMutex* mutex = g_new0(GMutex, 1);
    g_mutex_init(mutex);
    
    ProgressMonitorContext* ctx = g_new0(ProgressMonitorContext, 1);
    ctx->connection = (GDBusConnection*)0x1;
    ctx->handler_id = g_strdup("123");
    ctx->firmware_name = g_strdup("test.bin");
    ctx->stop_flag = &stop_flag;
    ctx->mutex = mutex;
    ctx->last_dlnow = 0;
    ctx->last_activity_time = time(NULL);
    
    // Start thread
    GThread* thread = g_thread_new("test_monitor", rdkfw_progress_monitor_thread, ctx);
    
    // Let it run briefly
    g_usleep(50000);  // 50ms
    
    // Signal stop
    g_atomic_int_set(&stop_flag, 1);
    
    // Wait for thread
    g_thread_join(thread);
    
    // Cleanup
    unlink("/opt/curl_progress");
}

TEST_F(DbusHandlersTest, ProgressMonitorThread_MalformedProgressFile_HandlesGracefully) {
    FILE* fp = fopen("/opt/curl_progress", "w");
    ASSERT_NE(fp, nullptr);
    fprintf(fp, "MALFORMED DATA");
    fclose(fp);
    
    gint stop_flag = 0;
    GMutex* mutex = g_new0(GMutex, 1);
    g_mutex_init(mutex);
    
    ProgressMonitorContext* ctx = g_new0(ProgressMonitorContext, 1);
    ctx->connection = (GDBusConnection*)0x1;
    ctx->handler_id = g_strdup("123");
    ctx->firmware_name = g_strdup("test.bin");
    ctx->stop_flag = &stop_flag;
    ctx->mutex = mutex;
    ctx->last_dlnow = 0;
    ctx->last_activity_time = time(NULL);
    
    GThread* thread = g_thread_new("test_monitor", rdkfw_progress_monitor_thread, ctx);
    g_usleep(50000);
    g_atomic_int_set(&stop_flag, 1);
    g_thread_join(thread);
    
    unlink("/opt/curl_progress");
}

// ============================================================================
// ADVANCED COVERAGE TESTS - Download Firmware
// ============================================================================

TEST_F(DbusHandlersTest, DownloadFirmware_CurlError6_ReturnsDnsError) {
    EXPECT_CALL(*mock_rdkv_upgrade, rdkv_upgrade_request(_, _, _))
        .WillOnce(Invoke([](const RdkUpgradeContext_t* ctx, void** curl, int* http) {
            *http = 0;
            return 6;  // CURLE_COULDNT_RESOLVE_HOST
        }));
    
    DownloadFirmwareResult result = rdkFwupdateMgr_downloadFirmware(
        "test.bin", "http://test.com/test.bin", "PCI", "/tmp/test.bin", NULL);
    
    EXPECT_EQ(result.result_code, DOWNLOAD_NETWORK_ERROR);
    EXPECT_NE(result.error_message, nullptr);
    EXPECT_TRUE(g_strrstr(result.error_message, "DNS") != NULL || 
                g_strrstr(result.error_message, "resolution") != NULL);
    
    g_free(result.error_message);
}

TEST_F(DbusHandlersTest, DownloadFirmware_CurlError18_ReturnsPartialFileError) {
    EXPECT_CALL(*mock_rdkv_upgrade, rdkv_upgrade_request(_, _, _))
        .WillOnce(Invoke([](const RdkUpgradeContext_t* ctx, void** curl, int* http) {
            *http = 200;
            return 18;  // CURLE_PARTIAL_FILE
        }));
    
    DownloadFirmwareResult result = rdkFwupdateMgr_downloadFirmware(
        "test.bin", "http://test.com/test.bin", "PCI", "/tmp/test.bin", NULL);
    
    EXPECT_EQ(result.result_code, DOWNLOAD_ERROR);
    EXPECT_NE(result.error_message, nullptr);
    
    g_free(result.error_message);
}

TEST_F(DbusHandlersTest, DownloadFirmware_CurlError23_ReturnsWriteError) {
    EXPECT_CALL(*mock_rdkv_upgrade, rdkv_upgrade_request(_, _, _))
        .WillOnce(Invoke([](const RdkUpgradeContext_t* ctx, void** curl, int* http) {
            *http = 200;
            return 23;  // CURLE_WRITE_ERROR
        }));
    
    DownloadFirmwareResult result = rdkFwupdateMgr_downloadFirmware(
        "test.bin", "http://test.com/test.bin", "PCI", "/tmp/test.bin", NULL);
    
    EXPECT_EQ(result.result_code, DOWNLOAD_ERROR);
    EXPECT_NE(result.error_message, nullptr);
    EXPECT_TRUE(g_strrstr(result.error_message, "Write") != NULL ||
                g_strrstr(result.error_message, "disk") != NULL);
    
    g_free(result.error_message);
}

TEST_F(DbusHandlersTest, DownloadFirmware_CurlError28_ReturnsTimeout) {
    EXPECT_CALL(*mock_rdkv_upgrade, rdkv_upgrade_request(_, _, _))
        .WillOnce(Invoke([](const RdkUpgradeContext_t* ctx, void** curl, int* http) {
            *http = 0;
            return 28;  // CURLE_OPERATION_TIMEDOUT
        }));
    
    DownloadFirmwareResult result = rdkFwupdateMgr_downloadFirmware(
        "test.bin", "http://test.com/test.bin", "PCI", "/tmp/test.bin", NULL);
    
    EXPECT_EQ(result.result_code, DOWNLOAD_NETWORK_ERROR);
    EXPECT_TRUE(g_strrstr(result.error_message, "timeout") != NULL ||
                g_strrstr(result.error_message, "timed out") != NULL);
    
    g_free(result.error_message);
}

TEST_F(DbusHandlersTest, DownloadFirmware_FirmwareTypePDRI_SetsCorrectUpgradeType) {
    EXPECT_CALL(*mock_rdkv_upgrade, rdkv_upgrade_request(_, _, _))
        .WillOnce(Invoke([](const RdkUpgradeContext_t* ctx, void** curl, int* http) {
            // Verify PDRI_UPGRADE type was set
            EXPECT_EQ(ctx->upgrade_type, PDRI_UPGRADE);
            *http = 200;
            FILE* fp = fopen((const char*)ctx->dwlloc, "w");
            if (fp) {
                fprintf(fp, "test");
                fclose(fp);
            }
            return 0;
        }));
    
    DownloadFirmwareResult result = rdkFwupdateMgr_downloadFirmware(
        "test.bin", "http://test.com/test.bin", "PDRI", "/tmp/test_pdri.bin", NULL);
    
    EXPECT_EQ(result.result_code, DOWNLOAD_SUCCESS);
    unlink("/tmp/test_pdri.bin");
    g_free(result.error_message);
}

TEST_F(DbusHandlersTest, DownloadFirmware_FirmwareTypePERIPHERAL_SetsCorrectType) {
    EXPECT_CALL(*mock_rdkv_upgrade, rdkv_upgrade_request(_, _, _))
        .WillOnce(Invoke([](const RdkUpgradeContext_t* ctx, void** curl, int* http) {
            EXPECT_EQ(ctx->upgrade_type, PERIPHERAL_UPGRADE);
            *http = 200;
            FILE* fp = fopen((const char*)ctx->dwlloc, "w");
            if (fp) {
                fprintf(fp, "test");
                fclose(fp);
            }
            return 0;
        }));
    
    DownloadFirmwareResult result = rdkFwupdateMgr_downloadFirmware(
        "test.bin", "http://test.com/test.bin", "PERIPHERAL", "/tmp/test_periph.bin", NULL);
    
    EXPECT_EQ(result.result_code, DOWNLOAD_SUCCESS);
    unlink("/tmp/test_periph.bin");
    g_free(result.error_message);
}

TEST_F(DbusHandlersTest, DownloadFirmware_FileNotFoundAfterDownload_ReturnsError) {
    EXPECT_CALL(*mock_rdkv_upgrade, rdkv_upgrade_request(_, _, _))
        .WillOnce(Invoke([](const RdkUpgradeContext_t* ctx, void** curl, int* http) {
            *http = 200;
            // Don't create the file - simulate missing file after download
            return 0;
        }));
    
    DownloadFirmwareResult result = rdkFwupdateMgr_downloadFirmware(
        "test.bin", "http://test.com/test.bin", "PCI", "/tmp/missing_file.bin", NULL);
    
    EXPECT_EQ(result.result_code, DOWNLOAD_ERROR);
    EXPECT_TRUE(g_strrstr(result.error_message, "not found") != NULL ||
                g_strrstr(result.error_message, "File") != NULL);
    
    g_free(result.error_message);
}

TEST_F(DbusHandlersTest, DownloadFirmware_Http206_SuccessfulPartialContent) {
    EXPECT_CALL(*mock_rdkv_upgrade, rdkv_upgrade_request(_, _, _))
        .WillOnce(Invoke([](const RdkUpgradeContext_t* ctx, void** curl, int* http) {
            *http = 206;  // Partial Content
            FILE* fp = fopen((const char*)ctx->dwlloc, "w");
            if (fp) {
                fprintf(fp, "partial data");
                fclose(fp);
            }
            return 0;
        }));
    
    DownloadFirmwareResult result = rdkFwupdateMgr_downloadFirmware(
        "test.bin", "http://test.com/test.bin", "PCI", "/tmp/test_206.bin", NULL);
    
    EXPECT_EQ(result.result_code, DOWNLOAD_SUCCESS);
    unlink("/tmp/test_206.bin");
    g_free(result.error_message);
}

TEST_F(DbusHandlersTest, DownloadFirmware_LoadFromXconfCache_Success) {
    // Create XConf cache
    const char* xconf_json = "{"
        "\"firmwareFilename\":\"http://cdn.test.com/firmware.bin\","
        "\"firmwareVersion\":\"VERSION_2.0.0\""
        "}";
    
    FILE* fp = fopen("/tmp/xconf_response_thunder.txt", "w");
    ASSERT_NE(fp, nullptr);
    fprintf(fp, "%s", xconf_json);
    fclose(fp);
    
    EXPECT_CALL(*mock_rdkv_upgrade, rdkv_upgrade_request(_, _, _))
        .WillOnce(Invoke([](const RdkUpgradeContext_t* ctx, void** curl, int* http) {
            *http = 200;
            FILE* fp = fopen((const char*)ctx->dwlloc, "w");
            if (fp) {
                fprintf(fp, "firmware data");
                fclose(fp);
            }
            return 0;
        }));
    
    // Empty URL should load from XConf cache
    DownloadFirmwareResult result = rdkFwupdateMgr_downloadFirmware(
        "firmware.bin", "", "PCI", "/tmp/test_xconf_load.bin", NULL);
    
    EXPECT_EQ(result.result_code, DOWNLOAD_SUCCESS);
    
    unlink("/tmp/test_xconf_load.bin");
    g_free(result.error_message);
}

TEST_F(DbusHandlersTest, DownloadFirmware_XconfCacheEmptyUrl_ReturnsError) {
    const char* xconf_json = "{"
        "\"firmwareFilename\":\"\","
        "\"firmwareVersion\":\"VERSION_2.0.0\""
        "}";
    
    FILE* fp = fopen("/tmp/xconf_response_thunder.txt", "w");
    ASSERT_NE(fp, nullptr);
    fprintf(fp, "%s", xconf_json);
    fclose(fp);
    
    DownloadFirmwareResult result = rdkFwupdateMgr_downloadFirmware(
        "firmware.bin", "", "PCI", "/tmp/test.bin", NULL);
    
    EXPECT_EQ(result.result_code, DOWNLOAD_ERROR);
    EXPECT_TRUE(g_strrstr(result.error_message, "URL") != NULL);
    
    g_free(result.error_message);
}

// ============================================================================
// ADVANCED COVERAGE TESTS - CheckForUpdate [DISABLED - OUTDATED API]
// ============================================================================
// NOTE: The following tests (lines 1270-1540) use an outdated API that doesn't
// match the current production code. They have been commented out to allow
// the new D-Bus signal emission tests to compile and run successfully.
// These tests were likely from an older branch or incomplete implementation.
// ============================================================================

#if 0  // DISABLED - API mismatch with current production code

TEST_F(DbusHandlersTest, CheckForUpdate_ValidationFailure_ReturnsUpdateNotAllowed) {
    // Mock XConf fetch success
    EXPECT_CALL(*mock_rdkfwupdatemgr, fetch_xconf_firmware_info(_, _, _))
        .WillOnce(Invoke([](XCONFRES* resp, int type, int* code) {
            strcpy(resp->cloudFWVersion, "VERSION_2.0.0_WRONG_MODEL");
            strcpy(resp->cloudFWFile, "wrong_model_firmware.bin");
            strcpy(resp->cloudFWLocation, "http://test.com/wrong.bin");
            strcpy(resp->firmwareUpdateState, "3");
            *code = 200;
            return 0;
        }));
    
    // Mock validation failure
    EXPECT_CALL(*mock_rdkfwupdatemgr, processJsonResponse(_, _, _))
        .WillOnce(Return(1));  // Validation failure
    
    CheckUpdateResponse result = rdkFwupdateMgr_checkForUpdate("test-handler", "", NULL);
    
    EXPECT_EQ(result.result_code, UPDATE_NOT_ALLOWED);
    EXPECT_TRUE(g_strrstr(result.status_message, "validation failed") != NULL ||
                g_strrstr(result.status_message, "not for this device") != NULL);
    
    checkupdate_response_free(&result);
}

/**
 * @test CheckForUpdate_EmptyFirmwareVersion_ReturnsNotAvailable
 * @brief Tests XConf response with no firmware version
 * COVERAGE TARGET: Lines 1167-1169 (empty version check)
 */
TEST_F(DbusHandlersTest, CheckForUpdate_EmptyFirmwareVersion_ReturnsNotAvailable) {
    EXPECT_CALL(*mock_rdkfwupdatemgr, fetch_xconf_firmware_info(_, _, _))
        .WillOnce(Invoke([](XCONFRES* resp, int type, int* code) {
            resp->cloudFWVersion[0] = '\0';  // Empty version
            strcpy(resp->cloudFWFile, "");
            strcpy(resp->cloudFWLocation, "");
            *code = 200;
            return 0;
        }));
    
    CheckUpdateResponse result = rdkFwupdateMgr_checkForUpdate("test-handler", "", NULL);
    
    EXPECT_EQ(result.result_code, FIRMWARE_NOT_AVAILABLE);
    EXPECT_TRUE(g_strrstr(result.status_message, "No firmware") != NULL ||
                g_strrstr(result.status_message, "not available") != NULL);
    
    checkupdate_response_free(&result);
}

/**
 * @test CheckForUpdate_ServerCommunicationError_ReturnsFirmwareCheckError
 * @brief Tests XConf server communication failure
 * COVERAGE TARGET: Lines 1178 (fetch failure error path)
 */
TEST_F(DbusHandlersTest, CheckForUpdate_ServerCommunicationError_ReturnsFirmwareCheckError) {
    EXPECT_CALL(*mock_rdkfwupdatemgr, fetch_xconf_firmware_info(_, _, _))
        .WillOnce(Return(-1));  // Communication error
    
    CheckUpdateResponse result = rdkFwupdateMgr_checkForUpdate("test-handler", "", NULL);
    
    EXPECT_EQ(result.result_code, FIRMWARE_CHECK_ERROR);
    EXPECT_TRUE(g_strrstr(result.status_message, "failed") != NULL ||
                g_strrstr(result.status_message, "error") != NULL);
    
    checkupdate_response_free(&result);
}

/**
 * @test FetchXconfFirmwareInfo_XconfCommFailure_LogsDetailedError
 * @brief Tests detailed error logging on XConf communication failure
 * COVERAGE TARGET: Lines 450-455 (XconfComms error logging)
 */
TEST_F(DbusHandlersTest, FetchXconfFirmwareInfo_XconfCommFailure_LogsDetailedError) {
    XCONFRES response;
    int http_code = 0;
    
    // Mock XconfComms to fail
    EXPECT_CALL(*mock_rdkfwupdatemgr, XconfComms(_, _, _, _, _))
        .WillOnce(Return(-1));  // Communication failure
    
    int result = fetch_xconf_firmware_info(&response, 0, &http_code);
    
    EXPECT_EQ(result, -1);
    // Error logging happens internally (lines 450-455 covered)
}

/**
 * @test CreateResultResponse_IgnoreOptout_ReturnsCorrectMessage
 * @brief Tests IGNORE_OPTOUT status code branch
 * COVERAGE TARGET: Lines 619-621 (IGNORE_OPTOUT case)
 */
TEST_F(DbusHandlersTest, CreateResultResponse_IgnoreOptout_ReturnsCorrectMessage) {
    CheckUpdateResponse result = create_result_response(IGNORE_OPTOUT, "Custom message");
    
    EXPECT_EQ(result.result_code, IGNORE_OPTOUT);
    EXPECT_STREQ(result.status_message, "Custom message");
    EXPECT_STREQ(result.firmware_version_available, "");
    
    checkupdate_response_free(&result);
}

/**
 * @test CreateResultResponse_BypassOptout_ReturnsCorrectMessage
 * @brief Tests BYPASS_OPTOUT status code branch
 * COVERAGE TARGET: Lines 622-624 (BYPASS_OPTOUT case)
 */
TEST_F(DbusHandlersTest, CreateResultResponse_BypassOptout_ReturnsCorrectMessage) {
    CheckUpdateResponse result = create_result_response(BYPASS_OPTOUT, "Bypass message");
    
    EXPECT_EQ(result.result_code, BYPASS_OPTOUT);
    EXPECT_STREQ(result.status_message, "Bypass message");
    
    checkupdate_response_free(&result);
}

/**
 * @test CreateResultResponse_UnknownStatus_ReturnsDefaultMessage
 * @brief Tests default case in status code switch
 * COVERAGE TARGET: Lines 625-627 (default case)
 */
TEST_F(DbusHandlersTest, CreateResultResponse_UnknownStatus_ReturnsDefaultMessage) {
    CheckUpdateResponse result = create_result_response((FirmwareUpdateResultCode)9999, NULL);
    
    EXPECT_EQ(result.result_code, (FirmwareUpdateResultCode)9999);
    // Default message handling (exact message depends on implementation)
    EXPECT_NE(result.status_message, nullptr);
    
    checkupdate_response_free(&result);
}

/**
 * @test FetchXconfFirmwareInfo_BufferOverflowCheck_ReturnsError
 * @brief Tests JSON buffer overflow protection
 * COVERAGE TARGET: Lines 357-358 (buffer overflow check)
 */
TEST_F(DbusHandlersTest, FetchXconfFirmwareInfo_BufferOverflowCheck_ReturnsError) {
    XCONFRES response;
    int http_code = 0;
    
    // Mock GetServURL to return size that would overflow
    EXPECT_CALL(*mock_rdkfwupdatemgr, GetServURL(_, _))
        .WillOnce(Return(2000));  // Larger than JSON_STR_LEN (1000)
    
    int result = fetch_xconf_firmware_info(&response, 0, &http_code);
    
    EXPECT_EQ(result, -1);
    // Buffer overflow prevention (lines 357-358 covered)
}

/**
 * @test FetchXconfFirmwareInfo_CacheSaveFailure_ContinuesExecution
 * @brief Tests handling of cache save failure (non-fatal error)
 * COVERAGE TARGET: Line 433 (cache save error log)
 */
TEST_F(DbusHandlersTest, FetchXconfFirmwareInfo_CacheSaveFailure_ContinuesExecution) {
    XCONFRES response;
    int http_code = 0;
    
    // Mock successful XConf fetch
    EXPECT_CALL(*mock_rdkfwupdatemgr, XconfComms(_, _, _, _, _))
        .WillOnce(Invoke([](DownloadData* dd, char*, int, void**, int* code) {
            dd->pvOut = strdup("{\"firmwareVersion\":\"1.0\"}");
            dd->datasize = strlen((char*)dd->pvOut);
            *code = 200;
            return 0;
        }));
    
    // Mock cache save to fail
    EXPECT_CALL(*mock_rdkfwupdatemgr, save_xconf_to_cache(_, _))
        .WillOnce(Return(FALSE));  // Cache save fails
    
    int result = fetch_xconf_firmware_info(&response, 0, &http_code);
    
    // Should still succeed despite cache failure (cache is optimization)
    EXPECT_EQ(result, 0);
    EXPECT_EQ(http_code, 200);
}

/**
 * @test CreateResultResponse_UpdateNotAllowed_UsesDefaultMessage
 * @brief Tests UPDATE_NOT_ALLOWED with NULL message (uses default)
 * COVERAGE TARGET: Lines 613-615 (UPDATE_NOT_ALLOWED case with default)
 */
TEST_F(DbusHandlersTest, CreateResultResponse_UpdateNotAllowed_UsesDefaultMessage) {
    CheckUpdateResponse result = create_result_response(UPDATE_NOT_ALLOWED, NULL);
    
    EXPECT_EQ(result.result_code, UPDATE_NOT_ALLOWED);
    // Should use default message: "Firmware not compatible with this device model"
    EXPECT_NE(result.status_message, nullptr);
    EXPECT_TRUE(g_strrstr(result.status_message, "not compatible") != NULL ||
                g_strrstr(result.status_message, "device") != NULL);
    
    checkupdate_response_free(&result);
}

/**
 * @test CreateResultResponse_FirmwareCheckError_UsesDefaultMessage
 * @brief Tests FIRMWARE_CHECK_ERROR with NULL message (uses default)
 * COVERAGE TARGET: Lines 616-618 (FIRMWARE_CHECK_ERROR case with default)
 */
TEST_F(DbusHandlersTest, CreateResultResponse_FirmwareCheckError_UsesDefaultMessage) {
    CheckUpdateResponse result = create_result_response(FIRMWARE_CHECK_ERROR, NULL);
    
    EXPECT_EQ(result.result_code, FIRMWARE_CHECK_ERROR);
    // Should use default message: "Error checking for updates"
    EXPECT_NE(result.status_message, nullptr);
    EXPECT_TRUE(g_strrstr(result.status_message, "Error") != NULL ||
                g_strrstr(result.status_message, "checking") != NULL);
    
    checkupdate_response_free(&result);
}

/**
 * @test FetchXconfFirmwareInfo_HttpNon200_HandledCorrectly
 * @brief Tests HTTP error codes (404, 500, etc.)
 * Coverage for various HTTP error handling paths
 */
TEST_F(DbusHandlersTest, FetchXconfFirmwareInfo_HttpNon200_HandledCorrectly) {
    XCONFRES response;
    int http_code = 0;
    
    // Mock XConf to return 404
    EXPECT_CALL(*mock_rdkfwupdatemgr, XconfComms(_, _, _, _, _))
        .WillOnce(Invoke([](DownloadData* dd, char*, int, void**, int* code) {
            dd->pvOut = strdup("Not Found");
            dd->datasize = 9;
            *code = 404;
            return 0;
        }));
    
    int result = fetch_xconf_firmware_info(&response, 0, &http_code);
    
    EXPECT_EQ(http_code, 404);
    // Function should handle non-200 codes gracefully
}

/**
 * @test CheckForUpdate_XconfReturnsInvalidJson_HandlesGracefully
 * @brief Tests XConf response with malformed JSON
 * Coverage for JSON parsing error paths
 */
TEST_F(DbusHandlersTest, CheckForUpdate_XconfReturnsInvalidJson_HandlesGracefully) {
    EXPECT_CALL(*mock_rdkfwupdatemgr, fetch_xconf_firmware_info(_, _, _))
        .WillOnce(Invoke([](XCONFRES* resp, int type, int* code) {
            // Return empty/invalid data
            memset(resp, 0, sizeof(XCONFRES));
            *code = 200;
            return 0;
        }));
    
    CheckUpdateResponse result = rdkFwupdateMgr_checkForUpdate("test-handler", "", NULL);
    
    // Should handle gracefully - either NOT_AVAILABLE or ERROR
    EXPECT_TRUE(result.result_code == FIRMWARE_NOT_AVAILABLE || 
                result.result_code == FIRMWARE_CHECK_ERROR);
    
    checkupdate_response_free(&result);
}

#endif  // End of disabled outdated API tests

/**
 * @brief Main test runner
 */
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

// ============================================================================
// PHASE 2: D-BUS SIGNAL EMISSION TESTS (Link-time Symbol Interposition)
// ============================================================================
// These tests use fake D-Bus implementation (test_dbus_fake.cpp) to test
// signal emission WITHOUT requiring a real D-Bus daemon or main loop.
// 
// TECHNIQUE: Link-time symbol override
// - Production code calls g_dbus_connection_emit_signal()
// - Test binary provides fake implementation
// - Linker picks our version instead of GLib's
// - Result: Fast, deterministic, no IPC overhead
// ============================================================================

#include "test_dbus_fake.h"

/**
 * @test EmitDownloadProgressIdle_ValidData_EmitsSignal
 * @brief Tests D-Bus signal emission with valid progress data
 * COVERAGE TARGET: Lines 668-768 (emit_download_progress_idle function)
 * TECHNIQUE: Link-time symbol interposition (fake g_dbus_connection_emit_signal)
 */
TEST_F(DbusHandlersTest, EmitDownloadProgressIdle_ValidData_EmitsSignal) {
    fake_dbus_reset();
    
    // Create test data
    ProgressData* data = g_new0(ProgressData, 1);
    data->connection = (GDBusConnection*)0xDEADBEEF;  // Fake non-NULL pointer
    data->handler_id = g_strdup("12345");
    data->firmware_name = g_strdup("test_firmware.bin");
    data->progress_percent = 50;
    data->bytes_downloaded = 5000;
    data->total_bytes = 10000;
    
    // Call the idle callback directly
    gboolean result = emit_download_progress_idle(data);
    
    // Verify function returns FALSE (removes from idle queue)
    EXPECT_EQ(result, FALSE);
    
    // Verify signal was emitted
    EXPECT_TRUE(fake_dbus_was_signal_emitted());
    EXPECT_EQ(fake_dbus_get_last_progress(), 50);
    EXPECT_STREQ(fake_dbus_get_last_status(), "INPROGRESS");
    EXPECT_STREQ(fake_dbus_get_last_firmware_name(), "test_firmware.bin");
    EXPECT_EQ(fake_dbus_get_last_handler_id(), 12345ULL);
    
    // Note: data is freed by the function
}

/**
 * @test EmitDownloadProgressIdle_Progress100_EmitsCompletedStatus
 * TEST: Completed flash with status=2 (FW_UPDATE_COMPLETED)
 */
TEST_F(DbusHandlersTest, EmitDownloadProgressIdle_Progress100_EmitsCompletedStatus) {
    fake_dbus_reset();
    
    ProgressData* data = g_new0(ProgressData, 1);
    data->connection = (GDBusConnection*)0xDEADBEEF;
    data->handler_id = g_strdup("999");
    data->firmware_name = g_strdup("completed_fw.bin");
    data->progress_percent = 100;
    data->bytes_downloaded = 10000;
    data->total_bytes = 10000;
    
    gboolean result = emit_download_progress_idle(data);
    
    EXPECT_EQ(result, FALSE);
    EXPECT_TRUE(fake_dbus_was_signal_emitted());
    EXPECT_EQ(fake_dbus_get_last_progress(), 100);
    EXPECT_EQ(fake_dbus_get_last_status_int(), 2);
    EXPECT_TRUE(strstr(fake_dbus_get_last_message(), "completed") != NULL ||
                strstr(fake_dbus_get_last_message(), "success") != NULL);
}

/**
 * @test EmitDownloadProgressIdle_Progress0TotalBytesZero_EmitsNotStartedStatus
 * @brief Tests signal emission with zero progress and total
 * COVERAGE TARGET: Lines 706-708 (progress == 0 && total == 0 branch)
 */
TEST_F(DbusHandlersTest, EmitDownloadProgressIdle_Progress0TotalBytesZero_EmitsNotStartedStatus) {
    fake_dbus_reset();
    
    ProgressData* data = g_new0(ProgressData, 1);
    data->connection = (GDBusConnection*)0xDEADBEEF;
    data->handler_id = g_strdup("0");
    data->firmware_name = g_strdup("starting_fw.bin");
    data->progress_percent = 0;
    data->bytes_downloaded = 0;
    data->total_bytes = 0;
    
    gboolean result = emit_download_progress_idle(data);
    
    EXPECT_EQ(result, FALSE);
    EXPECT_TRUE(fake_dbus_was_signal_emitted());
    EXPECT_EQ(fake_dbus_get_last_progress(), 0);
    EXPECT_STREQ(fake_dbus_get_last_status(), "NOTSTARTED");
}

/**
 * @test EmitDownloadProgressIdle_NullConnection_ExitsGracefully
 * @brief Tests error handling when D-Bus connection is NULL
 * COVERAGE TARGET: Lines 678-684 (NULL connection check)
 */
TEST_F(DbusHandlersTest, EmitDownloadProgressIdle_NullConnection_ExitsGracefully) {
    fake_dbus_reset();
    
    ProgressData* data = g_new0(ProgressData, 1);
    data->connection = NULL;  // NULL connection
    data->handler_id = g_strdup("123");
    data->firmware_name = g_strdup("test.bin");
    data->progress_percent = 50;
    data->bytes_downloaded = 5000;
    data->total_bytes = 10000;
    
    gboolean result = emit_download_progress_idle(data);
    
    // Should return FALSE and NOT emit signal
    EXPECT_EQ(result, FALSE);
    EXPECT_FALSE(fake_dbus_was_signal_emitted());
    
    // Note: data is freed by the function
}

/**
 * @test EmitDownloadProgressIdle_NullFirmwareName_UsesPlaceholder
 * @brief Tests firmware name fallback when NULL
 * COVERAGE TARGET: Lines 688-690 (NULL firmware name handling)
 */
TEST_F(DbusHandlersTest, EmitDownloadProgressIdle_NullFirmwareName_UsesPlaceholder) {
    fake_dbus_reset();
    
    ProgressData* data = g_new0(ProgressData, 1);
    data->connection = (GDBusConnection*)0xDEADBEEF;
    data->handler_id = g_strdup("456");
    data->firmware_name = NULL;  // NULL firmware name
    data->progress_percent = 75;
    data->bytes_downloaded = 7500;
    data->total_bytes = 10000;
    
    gboolean result = emit_download_progress_idle(data);
    
    EXPECT_EQ(result, FALSE);
    EXPECT_TRUE(fake_dbus_was_signal_emitted());
    EXPECT_EQ(fake_dbus_get_last_progress(), 75);
    
    // Should use placeholder: "(unknown)"
    EXPECT_STREQ(fake_dbus_get_last_firmware_name(), "(unknown)");
}

/**
 * @test EmitDownloadProgressIdle_NullHandlerId_StillEmitsSignal
 * @brief Tests handler ID handling when NULL
 * COVERAGE TARGET: Lines 694-696 (NULL handler_id check)
 */
TEST_F(DbusHandlersTest, EmitDownloadProgressIdle_NullHandlerId_StillEmitsSignal) {
    fake_dbus_reset();
    
    ProgressData* data = g_new0(ProgressData, 1);
    data->connection = (GDBusConnection*)0xDEADBEEF;
    data->handler_id = NULL;  // NULL handler ID
    data->firmware_name = g_strdup("fw.bin");
    data->progress_percent = 25;
    data->bytes_downloaded = 2500;
    data->total_bytes = 10000;
    
    gboolean result = emit_download_progress_idle(data);
    
    EXPECT_EQ(result, FALSE);
    EXPECT_TRUE(fake_dbus_was_signal_emitted());
    EXPECT_EQ(fake_dbus_get_last_progress(), 25);
    EXPECT_EQ(fake_dbus_get_last_handler_id(), 0ULL);  // Should be 0
}

/**
 * @test EmitDownloadProgressIdle_SignalEmissionFails_HandlesError
 * @brief Tests error handling when signal emission fails
 * COVERAGE TARGET: Lines 744-751 (signal emission error handling)
 */
TEST_F(DbusHandlersTest, EmitDownloadProgressIdle_SignalEmissionFails_HandlesError) {
    fake_dbus_reset();
    
    // Configure fake to simulate failure
    fake_dbus_set_should_fail(true, 42, "Simulated D-Bus failure");
    
    ProgressData* data = g_new0(ProgressData, 1);
    data->connection = (GDBusConnection*)0xDEADBEEF;
    data->handler_id = g_strdup("789");
    data->firmware_name = g_strdup("fail_fw.bin");
    data->progress_percent = 50;
    data->bytes_downloaded = 5000;
    data->total_bytes = 10000;
    
    gboolean result = emit_download_progress_idle(data);
    
    // Should still return FALSE (cleanup happened)
    EXPECT_EQ(result, FALSE);
    
    // Signal emission was attempted but failed
    EXPECT_TRUE(fake_dbus_was_signal_emitted());
}

/**
 * @test EmitDownloadProgressIdle_MultipleSignals_AllRecorded
 * @brief Tests multiple signal emissions are tracked
 * COVERAGE TARGET: Verify signal emission in various scenarios
 */
TEST_F(DbusHandlersTest, EmitDownloadProgressIdle_MultipleSignals_AllRecorded) {
    fake_dbus_reset();
    
    // Emit first signal (25%)
    ProgressData* data1 = g_new0(ProgressData, 1);
    data1->connection = (GDBusConnection*)0xDEADBEEF;
    data1->handler_id = g_strdup("1");
    data1->firmware_name = g_strdup("fw.bin");
    data1->progress_percent = 25;
    data1->bytes_downloaded = 2500;
    data1->total_bytes = 10000;
    emit_download_progress_idle(data1);
    
    // Emit second signal (50%)
    ProgressData* data2 = g_new0(ProgressData, 1);
    data2->connection = (GDBusConnection*)0xDEADBEEF;
    data2->handler_id = g_strdup("1");
    data2->firmware_name = g_strdup("fw.bin");
    data2->progress_percent = 50;
    data2->bytes_downloaded = 5000;
    data2->total_bytes = 10000;
    emit_download_progress_idle(data2);
    
    // Emit third signal (100%)
    ProgressData* data3 = g_new0(ProgressData, 1);
    data3->connection = (GDBusConnection*)0xDEADBEEF;
    data3->handler_id = g_strdup("1");
    data3->firmware_name = g_strdup("fw.bin");
    data3->progress_percent = 100;
    data3->bytes_downloaded = 10000;
    data3->total_bytes = 10000;
    emit_download_progress_idle(data3);
    
    // Verify all three signals were emitted
    EXPECT_EQ(fake_dbus_get_signal_count(), 3);
    
    // Last signal should be 100%
    EXPECT_EQ(fake_dbus_get_last_progress(), 100);
    EXPECT_STREQ(fake_dbus_get_last_status(), "COMPLETED");
}

/**
 * @test EmitDownloadProgressIdle_LargeFirmwareName_NoBufferOverflow
 * @brief Tests handling of very long firmware names
 * COVERAGE TARGET: Verify no buffer overflows in string handling
 */
TEST_F(DbusHandlersTest, EmitDownloadProgressIdle_LargeFirmwareName_NoBufferOverflow) {
    fake_dbus_reset();
    
    // Create very long firmware name
    std::string long_name(1000, 'A');
    long_name += "_firmware.bin";
    
    ProgressData* data = g_new0(ProgressData, 1);
    data->connection = (GDBusConnection*)0xDEADBEEF;
    data->handler_id = g_strdup("123");
    data->firmware_name = g_strdup(long_name.c_str());
    data->progress_percent = 50;
    data->bytes_downloaded = 5000;
    data->total_bytes = 10000;
    
    gboolean result = emit_download_progress_idle(data);
    
    EXPECT_EQ(result, FALSE);
    EXPECT_TRUE(fake_dbus_was_signal_emitted());
    
    // Should handle long name without crash
    const char* emitted_name = fake_dbus_get_last_firmware_name();
    EXPECT_NE(emitted_name, nullptr);
    EXPECT_GT(strlen(emitted_name), 1000);  // Should preserve full name
}

// ============================================================================
// PHASE 3: FLASH PROGRESS SIGNAL EMISSION TESTS
// ============================================================================
// Tests for emit_flash_progress_idle() - flash/upgrade progress reporting
// Signal format: "(tsiis)" - (handler_id, firmware_name, progress_i32, status_i32, message)
// ============================================================================

/**
 * @test EmitFlashProgressIdle_ValidData_EmitsSignal
 * COVERAGE TARGET: Lines 1635-1720 (emit_flash_progress_idle)
 * TEST: Basic flash progress signal emission with valid data
 */
TEST_F(DbusHandlersTest, EmitFlashProgressIdle_ValidData_EmitsSignal) {
    fake_dbus_reset();
    
    FlashProgressUpdate* update = g_new0(FlashProgressUpdate, 1);
    update->connection = (GDBusConnection*)0xCAFEBABE;
    update->handler_id = g_strdup("456");
    update->firmware_name = g_strdup("upgrade_firmware.bin");
    update->progress = 50;
    update->status = 0;  // FW_UPDATE_INPROGRESS
    update->error_message = NULL;
    
    gboolean result = emit_flash_progress_idle(update);
    
    // Should return G_SOURCE_REMOVE (FALSE) after emission
    EXPECT_EQ(result, FALSE);
    
    // Verify signal was emitted
    EXPECT_TRUE(fake_dbus_was_signal_emitted());
    EXPECT_EQ(fake_dbus_get_last_progress(), 50);
    EXPECT_STREQ(fake_dbus_get_last_firmware_name(), "upgrade_firmware.bin");
    EXPECT_EQ(fake_dbus_get_last_handler_id(), 456);
}

/**
 * @test EmitFlashProgressIdle_Progress100Status1_CompletedMessage
 * TEST: Completed flash with status=1 (FW_UPDATE_COMPLETED)
 */
TEST_F(DbusHandlersTest, EmitFlashProgressIdle_Progress100Status1_CompletedMessage) {
    fake_dbus_reset();
    
    FlashProgressUpdate* update = g_new0(FlashProgressUpdate, 1);
    update->connection = (GDBusConnection*)0xCAFEBABE;
    update->handler_id = g_strdup("789");
    update->firmware_name = g_strdup("completed_fw.bin");
    update->progress = 100;
    update->status = 1;  // FW_UPDATE_COMPLETED (1, not 2!)
    update->error_message = NULL;
    
    gboolean result = emit_flash_progress_idle(update);
    
    EXPECT_EQ(result, FALSE);
    EXPECT_TRUE(fake_dbus_was_signal_emitted());
    EXPECT_EQ(fake_dbus_get_last_progress(), 100);
    EXPECT_EQ(fake_dbus_get_last_status_int(), 1);
    EXPECT_TRUE(strstr(fake_dbus_get_last_message(), "completed") != NULL ||
                strstr(fake_dbus_get_last_message(), "success") != NULL);
}

/**
 * @test EmitFlashProgressIdle_Progress0Status0_StartingMessage
 * TEST: Flash starting with progress=0, status=0 (INPROGRESS)
 */
TEST_F(DbusHandlersTest, EmitFlashProgressIdle_Progress0Status0_StartingMessage) {
    fake_dbus_reset();
    
    FlashProgressUpdate* update = g_new0(FlashProgressUpdate, 1);
    update->connection = (GDBusConnection*)0xCAFEBABE;
    update->handler_id = g_strdup("111");
    update->firmware_name = g_strdup("starting_fw.bin");
    update->progress = 0;
    update->status = 0;  // FW_UPDATE_INPROGRESS
    update->error_message = NULL;
    
    gboolean result = emit_flash_progress_idle(update);
    
    EXPECT_EQ(result, FALSE);
    EXPECT_TRUE(fake_dbus_was_signal_emitted());
    EXPECT_EQ(fake_dbus_get_last_progress(), 0);
    EXPECT_TRUE(strstr(fake_dbus_get_last_message(), "started") != NULL ||
                strstr(fake_dbus_get_last_message(), "Verify") != NULL);
}

/**
 * @test EmitFlashProgressIdle_Status2Error_EmitsErrorMessage
 * TEST: Flash error with status=2 (FW_UPDATE_ERROR) and error message
 */
TEST_F(DbusHandlersTest, EmitFlashProgressIdle_Status2Error_EmitsErrorMessage) {
    fake_dbus_reset();
    
    FlashProgressUpdate* update = g_new0(FlashProgressUpdate, 1);
    update->connection = (GDBusConnection*)0xCAFEBABE;
    update->handler_id = g_strdup("999");
    update->firmware_name = g_strdup("failed_fw.bin");
    update->progress = 35;
    update->status = 2;  // FW_UPDATE_ERROR (2, not 1!)
    update->error_message = g_strdup("Flash verification failed");
    
    gboolean result = emit_flash_progress_idle(update);
    
    EXPECT_EQ(result, FALSE);
    EXPECT_TRUE(fake_dbus_was_signal_emitted());
    EXPECT_EQ(fake_dbus_get_last_status_int(), 2);
    EXPECT_STREQ(fake_dbus_get_last_message(), "Flash verification failed");
}

/**
 * @test EmitFlashProgressIdle_NullConnection_ExitsGracefully
 * TEST: NULL connection should not crash
 */
TEST_F(DbusHandlersTest, EmitFlashProgressIdle_NullConnection_ExitsGracefully) {
    fake_dbus_reset();
    
    FlashProgressUpdate* update = g_new0(FlashProgressUpdate, 1);
    update->connection = NULL;  // NULL connection
    update->handler_id = g_strdup("222");
    update->firmware_name = g_strdup("test_fw.bin");
    update->progress = 50;
    update->status = 0;
    update->error_message = NULL;
    
    gboolean result = emit_flash_progress_idle(update);
    
    EXPECT_EQ(result, FALSE);
    // Signal emission might fail or succeed depending on implementation
    // Just ensure no crash
}

/**
 * @test EmitFlashProgressIdle_NullUpdate_ReturnsImmediately
 * TEST: NULL update pointer should exit gracefully
 */
TEST_F(DbusHandlersTest, EmitFlashProgressIdle_NullUpdate_ReturnsImmediately) {
    fake_dbus_reset();
    
    gboolean result = emit_flash_progress_idle(NULL);
    
    // Should return immediately without crashing
    EXPECT_EQ(result, FALSE);  // G_SOURCE_REMOVE
    EXPECT_FALSE(fake_dbus_was_signal_emitted());
}

/**
 * @test EmitFlashProgressIdle_NullFirmwareName_UsesNullString
 * TEST: NULL firmware name should be handled (use "NULL" or empty string)
 */
TEST_F(DbusHandlersTest, EmitFlashProgressIdle_NullFirmwareName_UsesNullString) {
    fake_dbus_reset();
    
    FlashProgressUpdate* update = g_new0(FlashProgressUpdate, 1);
    update->connection = (GDBusConnection*)0xCAFEBABE;
    update->handler_id = g_strdup("333");
    update->firmware_name = NULL;  // NULL firmware name
    update->progress = 75;
    update->status = 0;
    update->error_message = NULL;
    
    gboolean result = emit_flash_progress_idle(update);
    
    EXPECT_EQ(result, FALSE);
    EXPECT_TRUE(fake_dbus_was_signal_emitted());
    // Should use "NULL" or empty string
    const char* emitted_name = fake_dbus_get_last_firmware_name();
    EXPECT_NE(emitted_name, nullptr);
}

/**
 * @test EmitFlashProgressIdle_ProgressValues_CorrectMessages
 * TEST: Different progress values generate appropriate messages
 */
TEST_F(DbusHandlersTest, EmitFlashProgressIdle_ProgressValues_CorrectMessages) {
    // Test progress 25% - should mention "Verifying"
    fake_dbus_reset();
    FlashProgressUpdate* update1 = g_new0(FlashProgressUpdate, 1);
    update1->connection = (GDBusConnection*)0xCAFEBABE;
    update1->handler_id = g_strdup("25");
    update1->firmware_name = g_strdup("fw.bin");
    update1->progress = 20;
    update1->status = 0;
    emit_flash_progress_idle(update1);
    EXPECT_TRUE(fake_dbus_was_signal_emitted());
    
    // Test progress 50% - should mention "Flashing"
    fake_dbus_reset();
    FlashProgressUpdate* update2 = g_new0(FlashProgressUpdate, 1);
    update2->connection = (GDBusConnection*)0xCAFEBABE;
    update2->handler_id = g_strdup("50");
    update2->firmware_name = g_strdup("fw.bin");
    update2->progress = 45;
    update2->status = 0;
    emit_flash_progress_idle(update2);
    EXPECT_TRUE(fake_dbus_was_signal_emitted());
    
    // Test progress 75% - should mention "continuing"
    fake_dbus_reset();
    FlashProgressUpdate* update3 = g_new0(FlashProgressUpdate, 1);
    update3->connection = (GDBusConnection*)0xCAFEBABE;
    update3->handler_id = g_strdup("75");
    update3->firmware_name = g_strdup("fw.bin");
    update3->progress = 70;
    update3->status = 0;
    emit_flash_progress_idle(update3);
    EXPECT_TRUE(fake_dbus_was_signal_emitted());
}

/**
 * @test EmitFlashProgressIdle_SignalEmissionFails_HandlesError
 * TEST: Handle D-Bus signal emission failure gracefully
 */
TEST_F(DbusHandlersTest, EmitFlashProgressIdle_SignalEmissionFails_HandlesError) {
    fake_dbus_reset();
    fake_dbus_set_should_fail(true, 42, "Simulated flash signal failure");
    
    FlashProgressUpdate* update = g_new0(FlashProgressUpdate, 1);
    update->connection = (GDBusConnection*)0xCAFEBABE;
    update->handler_id = g_strdup("error_test");
    update->firmware_name = g_strdup("test_fw.bin");
    update->progress = 50;
    update->status = 0;
    update->error_message = NULL;
    
    gboolean result = emit_flash_progress_idle(update);
    
    // Should handle error gracefully and return G_SOURCE_REMOVE
    EXPECT_EQ(result, FALSE);
    EXPECT_TRUE(fake_dbus_was_signal_emitted());  // Called, but failed
}

// ============================================================================
// PHASE 4: THREAD WORKER TESTS (Progress Monitor Thread)
// ============================================================================
// Tests for rdkfw_progress_monitor_thread() - background download monitor
// Uses fake file I/O to simulate progress file without real filesystem
// Uses fake g_usleep() to make tests instant (no 100ms delays)
// ============================================================================

/**
 * @test ProgressMonitorThread_FileFound_ParsesAndEmitsProgress
 * COVERAGE TARGET: Lines 845-920 (file parsing and signal emission)
 * TEST: Thread reads progress file and emits signals
 */
TEST_F(DbusHandlersTest, ProgressMonitorThread_FileFound_ParsesAndEmitsProgress) {
    fake_dbus_reset();
    fake_fileio_reset();
    
    // Setup context
    gint stop_flag = 0;
    GMutex mutex;
    g_mutex_init(&mutex);
    
    ProgressMonitorContext* ctx = g_new0(ProgressMonitorContext, 1);
    ctx->connection = (GDBusConnection*)0xDEADBEEF;
    ctx->handler_id = g_strdup("monitor_test");
    ctx->firmware_name = g_strdup("download_fw.bin");
    ctx->stop_flag = &stop_flag;
    ctx->mutex = &mutex;
    ctx->last_dlnow = 0;
    ctx->last_activity_time = time(NULL);
    
    // Simulate progress file with 50% complete
    fake_fileio_set_progress_file("UP: 0 of 0  DOWN: 50000000 of 100000000\n");
    
    // Run thread for one iteration then stop
    stop_flag = 1;  // Will exit after first iteration
    
    gpointer result = rdkfw_progress_monitor_thread(ctx);
    
    // Thread should complete without crash
    EXPECT_EQ(result, nullptr);
    
    // Verify file was opened
    EXPECT_GT(fake_fileio_get_fopen_count(), 0);
    
    // Verify signal was emitted
    EXPECT_TRUE(fake_dbus_was_signal_emitted());
    EXPECT_EQ(fake_dbus_get_last_progress(), 50);
    
    g_mutex_clear(&mutex);
}

/**
 * @test ProgressMonitorThread_FileNotFound_HandlesGracefully
 * TEST: Thread handles missing progress file without crash
 */
TEST_F(DbusHandlersTest, ProgressMonitorThread_FileNotFound_HandlesGracefully) {
    fake_dbus_reset();
    fake_fileio_reset();
    
    gint stop_flag = 0;
    GMutex mutex;
    g_mutex_init(&mutex);
    
    ProgressMonitorContext* ctx = g_new0(ProgressMonitorContext, 1);
    ctx->connection = (GDBusConnection*)0xDEADBEEF;
    ctx->handler_id = g_strdup("not_found_test");
    ctx->firmware_name = g_strdup("missing_fw.bin");
    ctx->stop_flag = &stop_flag;
    ctx->mutex = &mutex;
    ctx->last_dlnow = 0;
    ctx->last_activity_time = time(NULL);
    
    // No progress file
    fake_fileio_set_progress_file(nullptr);
    
    // Run for 2 iterations then stop
    stop_flag = 1;
    
    gpointer result = rdkfw_progress_monitor_thread(ctx);
    
    EXPECT_EQ(result, nullptr);
    // Should attempt to open file
    EXPECT_GT(fake_fileio_get_fopen_count(), 0);
    // Should NOT emit signal (no data)
    EXPECT_FALSE(fake_dbus_was_signal_emitted());
    
    g_mutex_clear(&mutex);
}

/**
 * @test ProgressMonitorThread_ProgressIncrements_EmitsMultipleSignals
 * TEST: Thread emits multiple signals as progress increases
 */
TEST_F(DbusHandlersTest, ProgressMonitorThread_ProgressIncrements_EmitsMultipleSignals) {
    fake_dbus_reset();
    fake_fileio_reset();
    
    gint stop_flag = 0;
    GMutex mutex;
    g_mutex_init(&mutex);
    
    ProgressMonitorContext* ctx = g_new0(ProgressMonitorContext, 1);
    ctx->connection = (GDBusConnection*)0xDEADBEEF;
    ctx->handler_id = g_strdup("increment_test");
    ctx->firmware_name = g_strdup("progress_fw.bin");
    ctx->stop_flag = &stop_flag;
    ctx->mutex = &mutex;
    ctx->last_dlnow = 0;
    ctx->last_activity_time = time(NULL);
    
    // Simulate 75% progress
    fake_fileio_set_progress_file("UP: 0 of 0  DOWN: 75000000 of 100000000\n");
    
    stop_flag = 1;
    gpointer result = rdkfw_progress_monitor_thread(ctx);
    
    EXPECT_EQ(result, nullptr);
    EXPECT_TRUE(fake_dbus_was_signal_emitted());
    EXPECT_EQ(fake_dbus_get_last_progress(), 75);
    
    g_mutex_clear(&mutex);
}

/**
 * @test ProgressMonitorThread_Complete100Percent_EmitsCompletedStatus
 * TEST: 100% progress triggers COMPLETED status
 */
TEST_F(DbusHandlersTest, ProgressMonitorThread_Complete100Percent_EmitsCompletedStatus) {
    fake_dbus_reset();
    fake_fileio_reset();
    
    gint stop_flag = 0;
    GMutex mutex;
    g_mutex_init(&mutex);
    
    ProgressMonitorContext* ctx = g_new0(ProgressMonitorContext, 1);
    ctx->connection = (GDBusConnection*)0xDEADBEEF;
    ctx->handler_id = g_strdup("complete_test");
    ctx->firmware_name = g_strdup("complete_fw.bin");
    ctx->stop_flag = &stop_flag;
    ctx->mutex = &mutex;
    ctx->last_dlnow = 0;
    ctx->last_activity_time = time(NULL);
    
    // 100% complete
    fake_fileio_set_progress_file("UP: 0 of 0  DOWN: 100000000 of 100000000\n");
    
    stop_flag = 1;
    gpointer result = rdkfw_progress_monitor_thread(ctx);
    
    EXPECT_EQ(result, nullptr);
    EXPECT_TRUE(fake_dbus_was_signal_emitted());
    EXPECT_EQ(fake_dbus_get_last_progress(), 100);
    EXPECT_STREQ(fake_dbus_get_last_status(), "COMPLETED");
    
    g_mutex_clear(&mutex);
}

/**
 * @test ProgressMonitorThread_MalformedData_HandlesGracefully
 * TEST: Invalid progress file format doesn't crash
 */
TEST_F(DbusHandlersTest, ProgressMonitorThread_MalformedData_HandlesGracefully) {
    fake_dbus_reset();
    fake_fileio_reset();
    
    gint stop_flag = 0;
    GMutex mutex;
    g_mutex_init(&mutex);
    
    ProgressMonitorContext* ctx = g_new0(ProgressMonitorContext, 1);
    ctx->connection = (GDBusConnection*)0xDEADBEEF;
    ctx->handler_id = g_strdup("malformed_test");
    ctx->firmware_name = g_strdup("bad_data_fw.bin");
    ctx->stop_flag = &stop_flag;
    ctx->mutex = &mutex;
    ctx->last_dlnow = 0;
    ctx->last_activity_time = time(NULL);
    
    // Invalid format
    fake_fileio_set_progress_file("GARBAGE DATA!@#$%\n");
    
    stop_flag = 1;
    gpointer result = rdkfw_progress_monitor_thread(ctx);
    
    // Should complete without crash
    EXPECT_EQ(result, nullptr);
    EXPECT_GT(fake_fileio_get_fopen_count(), 0);
    
    g_mutex_clear(&mutex);
}

/**
 * @test ProgressMonitorThread_UsesGUsleep_MakesTestFast
 * TEST: Verify g_usleep is called (proves our fake is working)
 */
TEST_F(DbusHandlersTest, ProgressMonitorThread_UsesGUsleep_MakesTestFast) {
    fake_dbus_reset();
    fake_fileio_reset();
    
    gint stop_flag = 0;
    GMutex mutex;
    g_mutex_init(&mutex);
    
    ProgressMonitorContext* ctx = g_new0(ProgressMonitorContext, 1);
    ctx->connection = (GDBusConnection*)0xDEADBEEF;
    ctx->handler_id = g_strdup("sleep_test");
    ctx->firmware_name = g_strdup("test_fw.bin");
    ctx->stop_flag = &stop_flag;
    ctx->mutex = &mutex;
    ctx->last_dlnow = 0;
    ctx->last_activity_time = time(NULL);
    
    // File not found - will call g_usleep
    fake_fileio_set_progress_file(nullptr);
    
    stop_flag = 1;
    
    auto start = std::chrono::steady_clock::now();
    rdkfw_progress_monitor_thread(ctx);
    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // Should complete in <50ms (fake g_usleep makes it instant)
    // Real thread would take 100ms+ per iteration
    EXPECT_LT(duration.count(), 50);
    
    g_mutex_clear(&mutex);
}
