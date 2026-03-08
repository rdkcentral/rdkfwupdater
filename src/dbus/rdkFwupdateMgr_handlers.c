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
 * @file rdkFwupdateMgr_handlers.c
 * @brief Business logic handlers for firmware update operations
 * 
 * Implements the core firmware update functionality:
 * - CheckForUpdate: Queries XConf server for available firmware
 * - Response caching to reduce server load
 * - Firmware validation against device model
 * - Version comparison logic
 * 
 * These handlers are called by D-Bus interface but contain no D-Bus code.
 */

#include "rdkFwupdateMgr_handlers.h"
#include "rdkv_cdl_log_wrapper.h"
#include "rdkv_cdl.h"
#include "json_process.h"
#include "device_status_helper.h"
#ifndef GTEST_ENABLE
#include "downloadUtil.h"
#include "urlHelper.h"
#include "common_device_api.h"
#include "rdk_fwdl_utils.h"
#include "system_utils.h"
#else
#include "miscellaneous.h"
#endif
#include "deviceutils.h"
#include "rdkv_upgrade.h"
#include "device_api.h"
#include "iarmInterface.h"
#include "rfcinterface.h"
#include "flash.h"  // For flashImage() function from librdksw_flash.so
#include <string.h>
#include <gio/gio.h>
#include "rdkv_dbus_server.h"

// Buffer sizes for JSON and URL construction
#define JSON_STR_LEN        1000
#define URL_MAX_LEN         512

// XConf response cache files (reduces server queries)
#define XCONF_CACHE_FILE        "/tmp/xconf_response_thunder.txt"
#define XCONF_HTTP_CODE_FILE    "/tmp/xconf_httpcode_thunder.txt"
#define XCONF_PROGRESS_FILE     "/tmp/xconf_curl_progress_thunder"
#define RED_STATE_FILE          "/lib/rdk/stateRedRecovery.sh"

// ============================================================================
// GLOBAL IN-MEMORY XCONF CACHE
// ============================================================================
/**
 * @brief Global parsed XConf response cache
 * 
 * This structure holds the most recent successfully parsed XConf response
 * in memory to avoid repeated file I/O and JSON parsing operations.
 * 
 * Benefits:
 * - Fast access to firmware metadata without file I/O
 * - No repeated JSON parsing overhead
 * - Direct access to download URLs for DownloadFirmware API
 * - Thread-safe via g_xconf_data_cache mutex
 * 
 * Lifecycle:
 * - Populated by save_xconf_to_cache() after successful XConf query
 * - Read by get_cached_xconf_data() with automatic deep copy
 * - Cleared by clear_cached_xconf_data() on errors or invalidation
 * - Protected by g_xconf_data_cache mutex for thread safety
 * 
 * Memory:
 * - Struct itself is statically allocated for the lifetime of the process
 * - String data is stored in fixed-size char arrays inside XCONFRES
 *   (no g_strdup/g_free; data is copied via memcpy/strncpy into the struct)
 * - clear_cached_xconf_data() resets/invalidates the struct; no heap frees
 */
static XCONFRES g_cached_xconf_data = {0};
static gboolean g_xconf_data_valid = FALSE;
static int g_cached_http_code = 0;

/**
 * @brief Mutex protecting global XConf data cache
 * 
 * Protects concurrent access to:
 * - g_cached_xconf_data (parsed XConf response structure)
 * - g_xconf_data_valid (cache validity flag)
 * - g_cached_http_code (HTTP status code)
 * 
 * Lock Scope:
 * - MUST lock before: save_cached_xconf_data(), get_cached_xconf_data(), clear_cached_xconf_data()
 * - Release immediately after data copy completes
 * - Do NOT hold during network calls or file I/O
 * 
 * Thread Safety:
 * - Protects both read and write operations
 * - Ensures atomicity of cache updates
 * - Prevents partial reads during writes
 */
G_LOCK_DEFINE_STATIC(xconf_data_cache);

// ============================================================================
// CACHE SYNCHRONIZATION
// ============================================================================
/**
 * @brief Mutex protecting XConf cache file operations
 * 
 * Protects concurrent access to:
 * - XCONF_CACHE_FILE (/tmp/xconf_response_thunder.txt)
 * - XCONF_HTTP_CODE_FILE (/tmp/xconf_httpcode_thunder.txt)
 * 
 * Lock Scope:
 * - MUST lock before: xconf_cache_exists(), load_xconf_from_cache(), save_xconf_to_cache()
 * - Release immediately after file operation completes
 * - Do NOT hold during business logic (parsing, validation, network calls)
 * 
 * Thread Safety:
 * - Initialized using G_LOCK_DEFINE_STATIC (thread-safe, no init needed)
 * - Static mutex lifetime (program lifetime, no cleanup required)
 * 
 * Performance:
 * - No contention: Cache access is rare (minutes/hours apart)
 * - Lock time: ~1-5ms (file I/O duration only)
 * - Zero overhead when not in cache critical section
 */
G_LOCK_DEFINE_STATIC(xconf_cache);

/**
 * @brief Helper macro for cache operation error handling
 * 
 * Ensures mutex is ALWAYS released on error paths.
 * Usage:
 *   G_LOCK(xconf_cache);
 *   if (error_condition) {
 *       CACHE_UNLOCK_AND_RETURN(FALSE);
 *   }
 *   // ... success path ...
 *   G_UNLOCK(xconf_cache);
 */
#define CACHE_UNLOCK_AND_RETURN(retval) \
    do { \
        G_UNLOCK(xconf_cache); \
        return (retval); \
    } while(0)

// *** NEW: Progress monitoring constants ***
#define CURL_PROGRESS_FILE      "/opt/curl_progress"
#define PROGRESS_POLL_INTERVAL_MS    100   // Poll every 100ms for responsive updates
#define PROGRESS_THROTTLE_PERCENT    1.0   // Emit signal only if progress changed by ≥1%
#define PROGRESS_MONITOR_TIMEOUT_SEC 600   // 10 minutes max without progress

// *** Forward declarations ***
typedef struct _ProgressData ProgressData;
gboolean emit_download_progress_idle(gpointer user_data);

// *** NEW: Progress data for g_idle_add callback ***
/**
 * @brief Data structure for progress signal emission via g_idle_add()
 * 
 * Contains all data needed to emit a DownloadProgress D-Bus signal from
 * the main thread. Used to marshal progress updates from monitor thread
 * to main loop.
 * 
 * Memory Management:
 * - connection: Borrowed pointer (do NOT free)
 * - handler_id: Owned string (must be freed)
 * - firmware_name: Owned string (must be freed)
 * - Structure: Allocated with g_new0, freed in emit_download_progress_idle()
 */
struct _ProgressData {
    GDBusConnection* connection;        // D-Bus connection (borrowed, do not free)
    gchar* handler_id;                  // Handler ID string (owned, must free)
    gchar* firmware_name;               // Firmware name (owned, must free)
    guint32 progress_percent;           // Progress percentage 0-100
    guint64 bytes_downloaded;           // Current downloaded bytes
    guint64 total_bytes;                // Total bytes to download
};

// *** NEW: Progress monitor context structure ***
/**
 * @brief Context data for progress monitoring thread
 * 
 * This structure contains all data needed by the progress monitor thread to:
 * - Poll /opt/curl_progress file
 * - Emit D-Bus signals via g_idle_add()
 * - Gracefully shutdown when signaled
 * 
 * Memory Management:
 * - connection: Borrowed pointer (do NOT free, owned by D-Bus server)
 * - handler_id: Owned string (must call g_free)
 * - firmware_name: Owned string (must call g_free)
 * - mutex: Must be initialized with g_mutex_init() and cleared with g_mutex_clear()
 * - stop_flag: Pointer to stack variable in download handler (no cleanup needed)
 * - Structure itself: Allocated with g_new0, freed with g_free by thread before exit
 */
typedef struct {
    GDBusConnection* connection;        // D-Bus connection (borrowed, do NOT free)
    gchar* handler_id;                  // Handler ID string (owned, must free)
    gchar* firmware_name;               // Firmware name (owned, must free)
    gint* stop_flag;                    // Atomic flag to signal thread shutdown (gint for g_atomic_int_get/set)
    GMutex* mutex;                      // Protects last_dlnow from race conditions
    guint64 last_dlnow;                 // Last reported bytes (for throttling)
    time_t last_activity_time;          // Last time progress changed (for timeout detection)
} ProgressMonitorContext;

// *** NEW: Download state context structure ***
/**
 * @brief Context passed from D-Bus server for progress monitoring
 * 
 * This structure is passed as the download_state parameter to provide
 * necessary information for emitting D-Bus progress signals.
 * 
 * Memory Management:
 * - connection: Borrowed pointer (owned by D-Bus server, do NOT free)
 * - handler_id: Borrowed string (owned by caller, do NOT free here)
 * - firmware_name: Borrowed string (owned by caller, do NOT free here)
 */
typedef struct {
    GDBusConnection* connection;        // D-Bus connection (borrowed)
    const gchar* handler_id;           // Handler ID (borrowed)
    const gchar* firmware_name;         // Firmware name (borrowed)
} DownloadStateContext;

// Shared device and image information (populated at daemon startup)
extern DeviceProperty_t device_info;

// Forward declaration for getOPTOUTValue function from rdkFwupdateMgr.c
extern int getOPTOUTValue(const char *file_name);
extern ImageDetails_t cur_img_detail;
extern Rfc_t rfc_list;

// Trigger type constant for manual/D-Bus initiated downloads
#define TRIGGER_MANUAL 1

// ============================================================================
// FORWARD DECLARATIONS (Internal Functions)
// ============================================================================
static void clear_cached_xconf_data_internal(void);
static gboolean save_cached_xconf_data(const XCONFRES *pResponse, int http_code);

/**
 * @brief Check if XConf response cache exists
 * @return TRUE if cache file exists, FALSE otherwise
 * 
 * Used to avoid unnecessary XConf queries when cached data is available.
 */
/**
 * @brief Check if XConf cache file exists (thread-safe)
 * @return TRUE if cache exists, FALSE otherwise
 * 
 * Thread Safety: Locks xconf_cache mutex during file existence check
 * to prevent TOCTOU race with concurrent save operations.
 */
gboolean xconf_cache_exists(void) {
    G_LOCK(xconf_cache);
    gboolean exists = g_file_test(XCONF_CACHE_FILE, G_FILE_TEST_EXISTS);
    G_UNLOCK(xconf_cache);
    
    SWLOG_DEBUG("[CACHE] Cache exists check: %s\n", exists ? "YES" : "NO");
    return exists;
}

/**
 * @brief Load XConf response from cache file (thread-safe)
 * @param[out] pResponse Structure to populate with cached data
 * @return TRUE on success, FALSE if cache read/parse fails
 * 
 * Thread Safety: Locks xconf_cache mutex during file read to prevent
 * concurrent writes from corrupting the read operation.
 * 
 * Lock Scope: Held ONLY during g_file_get_contents(), released before parsing
 * to minimize lock time (parsing can take milliseconds).
 * 
 * Reads cached XConf JSON response and parses it into XCONFRES structure.
 * Cache miss or parse failure returns FALSE - caller should fetch from XConf server.
 */
gboolean load_xconf_from_cache(XCONFRES *pResponse) {
    gchar *cache_content = NULL;
    gsize length;
    GError *error = NULL;
    gboolean result = FALSE;
    
    // Validate input parameter (no lock needed)
    if (pResponse == NULL) {
        SWLOG_ERROR("[CACHE] pResponse parameter is NULL\n");
        return FALSE;
    }
    
    SWLOG_INFO("[CACHE] Loading XConf data from cache: %s\n", XCONF_CACHE_FILE);
    
    // === CRITICAL SECTION START ===
    G_LOCK(xconf_cache);
    
    // Read file with mutex held (prevents concurrent writes)
    if (!g_file_get_contents(XCONF_CACHE_FILE, &cache_content, &length, &error)) {
        SWLOG_ERROR("[CACHE] Failed to read cache file: %s\n", 
                    error ? error->message : "Unknown error");
        if (error) g_error_free(error);
        CACHE_UNLOCK_AND_RETURN(FALSE);  // Unlock on error path
    }
    
    G_UNLOCK(xconf_cache);
    // === CRITICAL SECTION END ===
    
    // Parse OUTSIDE critical section (no need to hold lock)
    SWLOG_INFO("[CACHE] Loaded %zu bytes from cache\n", length);
    SWLOG_INFO("[CACHE] Cache content: %s\n", cache_content);

    // Parse cached JSON using existing parser
    int parse_result = getXconfRespData(pResponse, cache_content);
    if (parse_result == 0) {
        SWLOG_INFO("[CACHE] Successfully parsed: version='%s', file='%s'\n", 
                   pResponse->cloudFWVersion, pResponse->cloudFWFile);
        result = TRUE;
    } else {
        SWLOG_ERROR("[CACHE] Failed to parse cached data (error: %d)\n", parse_result);
    }
    
    g_free(cache_content);
    return result;
}

#ifdef GTEST_ENABLE
gboolean save_xconf_to_cache(const char *xconf_response, int http_code)
#else
static gboolean save_xconf_to_cache(const char *xconf_response, int http_code)
#endif
{
    GError *error = NULL;
   
    // Validate input parameters (no lock needed)
    if (!xconf_response) {
        SWLOG_ERROR("[CACHE] Cannot save NULL response to cache\n");
        return FALSE;
    }
    
    if (strlen(xconf_response) == 0) {
        SWLOG_ERROR("[CACHE] Cannot save empty response to cache\n");
        return FALSE;
    }

    SWLOG_INFO("[CACHE] Saving XConf response to cache files and memory\n");
    
    // === CRITICAL SECTION START (File Cache) ===
    G_LOCK(xconf_cache);
    
    // Save main XConf response (with lock held)
    if (!g_file_set_contents(XCONF_CACHE_FILE, xconf_response, -1, &error)) {
        SWLOG_ERROR("[CACHE] Failed to save XConf response: %s\n", 
                    error ? error->message : "Unknown error");
        if (error) g_error_free(error);
        CACHE_UNLOCK_AND_RETURN(FALSE);  // Unlock on error
    }
    
    // Save HTTP code (still holding lock for atomic update)
    gchar *http_code_str = g_strdup_printf("%d", http_code);
    if (!g_file_set_contents(XCONF_HTTP_CODE_FILE, http_code_str, -1, &error)) {
        SWLOG_ERROR("[CACHE] Failed to save HTTP code: %s\n", 
                    error ? error->message : "Unknown error");
        if (error) g_error_free(error);
        g_free(http_code_str);
        CACHE_UNLOCK_AND_RETURN(FALSE);  // Unlock on error
    }
    
    G_UNLOCK(xconf_cache);
    // === CRITICAL SECTION END (File Cache) ===
    
    SWLOG_INFO("[CACHE] XConf data cached to files successfully\n");
    SWLOG_INFO("[CACHE]   - Response file: %s\n", XCONF_CACHE_FILE);
    SWLOG_INFO("[CACHE]   - HTTP code file: %s (code: %d)\n", 
               XCONF_HTTP_CODE_FILE, http_code);
    
    g_free(http_code_str);
    
    // Parse JSON response and save to global in-memory cache
    XCONFRES parsed_response = {0};
    int parse_result = getXconfRespData(&parsed_response, (char *)xconf_response);
    
    if (parse_result == 0) {
        SWLOG_INFO("[CACHE] Parsed XConf response successfully, saving to memory cache\n");
        
        // Save parsed data to global in-memory cache
        if (save_cached_xconf_data(&parsed_response, http_code)) {
            SWLOG_INFO("[CACHE] In-memory cache updated successfully\n");
        } else {
            SWLOG_ERROR("[CACHE] Failed to update in-memory cache (non-fatal)\n");
        }
    } else {
        SWLOG_ERROR("[CACHE] Failed to parse XConf response for memory cache (error: %d)\n", parse_result);
        SWLOG_ERROR("[CACHE] File cache saved but in-memory cache not updated\n");
        // Non-fatal: file cache is still valid
    }
    
    return TRUE;
}

#ifdef GTEST_ENABLE
// Exposed for unit testing - normally static
int fetch_xconf_firmware_info( XCONFRES *pResponse, int server_type, int *pHttp_code )
#else
static int fetch_xconf_firmware_info( XCONFRES *pResponse, int server_type, int *pHttp_code )
#endif
{
    DownloadData DwnLoc;
    char *pJSONStr = NULL;      // contains the device data string to send to XCONF server
    char *pServURL = NULL;      // the server to do the XCONF comms
    size_t len = 0;
    int ret = -1;
    void *curl = NULL;          // CURL handle

    // Validate input parameters
    if (pResponse == NULL) {
        SWLOG_ERROR("fetch_xconf_firmware_info: pResponse parameter is NULL\n");
        return -1;
    }
    
    if (pHttp_code == NULL) {
        SWLOG_ERROR("fetch_xconf_firmware_info: pHttp_code parameter is NULL\n");
        return -1;
    }

    DwnLoc.pvOut = NULL;
    DwnLoc.datasize = 0;
    DwnLoc.memsize = 0;
    *pHttp_code = 0;
    
    if( allocDowndLoadDataMem( &DwnLoc, DEFAULT_DL_ALLOC ) == 0 )
    {
        if( (pJSONStr=(char*)malloc( JSON_STR_LEN )) != NULL )
        {
            if( (pServURL=(char*)malloc( URL_MAX_LEN )) != NULL )
            {
                len = GetServURL( pServURL, URL_MAX_LEN );
                SWLOG_INFO( "fetch_xconf_firmware_info: server URL %s\n", pServURL );
                if( len )
                {
                    SWLOG_INFO("fetch_xconf_firmware_info: Server URL length: %d, preparing device JSON data...\n", (int)len);
                    len = createJsonString( pJSONStr, JSON_STR_LEN );
		    if (len >= JSON_STR_LEN) {
			    SWLOG_ERROR("JSON buffer overflow:  %zu >= %d", len, JSON_STR_LEN);
			    // Free allocated resources before returning
			    if (DwnLoc.pvOut != NULL) {
				    free(DwnLoc.pvOut);
				    DwnLoc.pvOut = NULL;
			    }
			    free(pServURL);
			    free(pJSONStr);
			    return ret;
		    }
                    SWLOG_INFO("fetch_xconf_firmware_info: Device JSON data prepared (%d bytes)\n", (int)len);
                    SWLOG_INFO("fetch_xconf_firmware_info: JSON POST data:\n%s\n", pJSONStr);

                    //context structure for XCONF upgrade request
                    RdkUpgradeContext_t xconf_context = {0};
                    xconf_context.upgrade_type = XCONF_UPGRADE;
                    xconf_context.server_type = server_type;
                    xconf_context.artifactLocationUrl = pServURL;
                    xconf_context.dwlloc = &DwnLoc;
                    xconf_context.pPostFields = pJSONStr;
                    
                    //Have to revist these vars once - MADHU
                    Rfc_t local_rfc_list = {0};
                    getRFCSettings(&local_rfc_list);  // Read actual RFC settings from system
                    
                    const char *local_immed_reboot_flag = "false";  // Default daemon setting
                    int local_delay_dwnl = 0;                       // Default daemon setting  
                    const char *local_lastrun = "0";               // Default daemon setting
                    char *local_disableStatsUpdate = "false";      // Default daemon setting
                    int local_force_exit = 0;                      // Default daemon setting
                    int local_trigger_type = 1;                    // Default daemon setting
                    
                    xconf_context.immed_reboot_flag = local_immed_reboot_flag;
                    xconf_context.delay_dwnl = local_delay_dwnl;
                    xconf_context.lastrun = local_lastrun;
                    xconf_context.disableStatsUpdate = local_disableStatsUpdate;
                    xconf_context.device_info = &device_info;      // Uses extern global
                    xconf_context.force_exit = &local_force_exit;
                    xconf_context.trigger_type = local_trigger_type;
                    xconf_context.rfc_list = &local_rfc_list;  

                    SWLOG_INFO("fetch_xconf_firmware_info: Initiating XConf request with server_type=%d\n", server_type);
                    SWLOG_INFO("fetch_xconf_firmware_info: Context setup - device_info=%p, rfc_list=%p\n", 
                               xconf_context.device_info, xconf_context.rfc_list);
                    
                    // Call rdkv_upgrade_request with error handling
                    SWLOG_INFO("fetch_xconf_firmware_info: Calling rdkv_upgrade_request...\n");
                    ret = rdkv_upgrade_request(&xconf_context, &curl, pHttp_code);
                    SWLOG_INFO("fetch_xconf_firmware_info: rdkv_upgrade_request returned (ret=%d)\n", ret);
                    
                    // Handle library-specific errors (negative values) - Daemon NEVER exits
                    if (ret < 0) {
                        SWLOG_ERROR("fetch_xconf_firmware_info: Library error: %s (code: %d)\n",
                                   rdkv_upgrade_strerror(ret), ret);
                        // Daemon continues - ret is already < 0, will be handled by existing error logic below
                    }
                    
                    SWLOG_INFO("fetch_xconf_firmware_info: XConf request completed - ret=%d, http_code=%d\n", ret, *pHttp_code);
                    
                    if( ret == 0 && *pHttp_code == 200 && DwnLoc.pvOut != NULL )
                    {
                        SWLOG_INFO("fetch_xconf_firmware_info: SUCCESS - XConf communication successful\n");
                        SWLOG_INFO("fetch_xconf_firmware_info: Raw XConf response (%d bytes):\n%s\n", 
                                   (int)DwnLoc.datasize, (char *)DwnLoc.pvOut);
                        SWLOG_INFO("fetch_xconf_firmware_info: Calling getXconfRespData to parse response...\n");
                        ret = getXconfRespData( pResponse, (char *)DwnLoc.pvOut );
                        SWLOG_INFO("fetch_xconf_firmware_info: getXconfRespData returned %d\n", ret);
                        
                        // Log parsed XConf response details
                        if (ret == 0) {
                            SWLOG_INFO("MakeXconfComms: PARSED XConf Response Data:\n");
                            SWLOG_INFO("  - firmwareFilename: '%s'\n", pResponse->cloudFWFile);
                            SWLOG_INFO("  - firmwareLocation: '%s'\n", pResponse->cloudFWLocation);
                            SWLOG_INFO("  - firmwareVersion: '%s'\n", pResponse->cloudFWVersion);
                            SWLOG_INFO("  - firmwareProtocol: '%s'\n", pResponse->cloudProto);
                            SWLOG_INFO("  - rebootImmediately: '%s'\n", pResponse->cloudImmediateRebootFlag);
                            SWLOG_INFO("  - delayDownload: '%s'\n", pResponse->cloudDelayDownload);
                            SWLOG_INFO("  - peripheralFirmwares: '%s'\n", pResponse->peripheralFirmwares);
                            SWLOG_INFO("  - cloudPDRIVersion: '%s'\n", pResponse->cloudPDRIVersion);
                            
                            // Cache the successful XConf response for future use
                            SWLOG_INFO("[CACHE] Saving successful XConf response to cache...\n");
                            if (save_xconf_to_cache((char *)DwnLoc.pvOut, *pHttp_code)) {
                                SWLOG_INFO("[CACHE] XConf response cached successfully\n");
                            } else {
                                SWLOG_ERROR("[CACHE] Failed to cache XConf response\n");
                            }
                        } else {
                            SWLOG_ERROR("MakeXconfComms: ERROR - Failed to parse XConf response\n");
                        }
                        
                        // Recovery completed event handling 
                        #ifndef GTEST_ENABLE
                        if( (filePresentCheck( RED_STATE_REBOOT ) == RDK_API_SUCCESS) ) {
                             SWLOG_INFO("%s : RED Recovery completed\n", __FUNCTION__);
                             eventManager(RED_STATE_EVENT, RED_RECOVERY_COMPLETED);
                             unlink(RED_STATE_REBOOT);
                        }
                        #endif
                    }
                    else
                    {
                        SWLOG_ERROR("fetch_xconf_firmware_info: FAILED - XConf communication failed\n");
                        SWLOG_ERROR("  - ret=%d (0=success)\n", ret);
                        SWLOG_ERROR("  - http_code=%d (200=success)\n", *pHttp_code);
                        SWLOG_ERROR("  - DwnLoc.pvOut=%p (should not be NULL)\n", DwnLoc.pvOut);
                        if (DwnLoc.pvOut != NULL) {
                            SWLOG_ERROR("  - Response data size: %d bytes\n", (int)DwnLoc.datasize);
                            SWLOG_ERROR("  - Response data: '%s'\n", (char *)DwnLoc.pvOut);
                        }
                    }
                }
                else
                {
                    SWLOG_ERROR( "fetch_xconf_firmware_info: no valid server URL\n" );
                }
                free( pServURL );
            }
            else
            {
                SWLOG_ERROR("fetch_xconf_firmware_info: Failed malloc for server URL of %d bytes\n", URL_MAX_LEN );
            }
            free( pJSONStr );
        }
        else
        {
            SWLOG_ERROR("fetch_xconf_firmware_info: Failed malloc for json string of %d bytes\n", JSON_STR_LEN );
        }
        if( DwnLoc.pvOut != NULL )
        {
            free( DwnLoc.pvOut );
        }
    }
    return ret;
}

/**
 * @brief Free all dynamically allocated memory in a CheckUpdateResponse structure.
 * 
 * Safely releases all g_strdup'd strings within the response structure and nullifies
 * the pointers to prevent double-free errors. Safe to call on already-freed or 
 * partially-initialized responses.
 * 
 * @param response Pointer to CheckUpdateResponse structure to free (can be NULL)
 */
void checkupdate_response_free(CheckUpdateResponse *response) {
    if (response) {
        if (response->available_version != NULL) {
            g_free(response->available_version);
            response->available_version = NULL;
        }
        if (response->update_details != NULL) {
            g_free(response->update_details);
            response->update_details = NULL;
        }
        if (response->status_message != NULL) {
            g_free(response->status_message);
            response->status_message = NULL;
        }
    }
}

/**
 * @brief Create a CheckUpdateResponse structure for a successful update check.
 * 
 * Compares the current firmware version with the available version from XConf.
 * If versions differ, returns UPDATE_AVAILABLE. If identical, returns UPDATE_NOT_AVAILABLE.
 * Retrieves the current firmware version via GetFirmwareVersion() and populates all
 * response fields with allocated strings.
 * 
 * @param available_version Firmware version from XConf (e.g., "VERSION_1.2.3")
 * @param update_details Pipe-delimited string with firmware metadata (URL, protocol, etc.)
 * @param status_message Human-readable status string
 * @return CheckUpdateResponse structure with allocated strings (must be freed by caller)
 */
#ifdef GTEST_ENABLE
CheckUpdateResponse create_success_response(const gchar *available_version,
                                           const gchar *update_details,
                                           const gchar *status_message)
#else
static CheckUpdateResponse create_success_response(const gchar *available_version,
                                                   const gchar *update_details,
                                                   const gchar *status_message) 
#endif
{
    CheckUpdateResponse response = {0};
    char current_img_buffer[64] = {0};
    
    bool img_status = GetFirmwareVersion(current_img_buffer, sizeof(current_img_buffer));
    
    SWLOG_INFO("[rdkFwupdateMgr] create_success_response: Getting current image info\n");
    SWLOG_INFO("[rdkFwupdateMgr]   - currentImg status: %s\n", img_status ? "SUCCESS" : "FAILED");
    SWLOG_INFO("[rdkFwupdateMgr]   - current_img_buffer: '%s'\n", current_img_buffer);

    // Null check for available_version before comparison (short-circuit evaluation ensures safety)
    gboolean is_update_available = img_status && 
                                   available_version != NULL && 
                                   (g_strcmp0(current_img_buffer, available_version) != 0);
    
    response.result = CHECK_FOR_UPDATE_SUCCESS;  // API call succeeded
    
    if (is_update_available) {
        response.status_code = FIRMWARE_AVAILABLE;
        response.current_img_version = g_strdup(current_img_buffer);
        response.available_version = g_strdup(available_version);
        response.update_details = g_strdup(update_details ? update_details : "");
        response.status_message = g_strdup(status_message ? status_message : "Firmware update available");
        
        SWLOG_INFO("[rdkFwupdateMgr] create_success_response: Response created with current image: '%s', available: '%s', status: '%s'\n", 
                   response.current_img_version, response.available_version, response.status_message);
    } else {
        response.status_code = FIRMWARE_NOT_AVAILABLE;
        response.current_img_version = g_strdup(current_img_buffer);
        response.available_version = g_strdup(available_version ? available_version : "");
        response.update_details = g_strdup("");
        response.status_message = g_strdup("Already on latest firmware");	
    }
    return response;
}

/**
 * @brief Create a CheckUpdateResponse for error cases or no-update scenarios.
 * 
 * Populates response structure with the given result code and status message.
 * Sets available_version and update_details to empty strings. Retrieves current
 * firmware version and provides default status messages if none supplied.
 * 
 * @param result_code Result code (FIRMWARE_NOT_AVAILABLE, FIRMWARE_CHECK_ERROR, etc.)
 * @param status_message Optional custom status message (can be NULL for default)
 * @return CheckUpdateResponse structure with allocated strings (must be freed by caller)
 */
#ifdef GTEST_ENABLE
CheckUpdateResponse create_result_response(CheckForUpdateStatus status_code,
                                                  const gchar *status_message)
#else 
static CheckUpdateResponse create_result_response(CheckForUpdateStatus status_code,
                                                  const gchar *status_message) 
#endif
{
    CheckUpdateResponse response = {0};
    char current_img_buffer[256] = {0};
    
    bool img_status = GetFirmwareVersion(current_img_buffer, sizeof(current_img_buffer));
    
    SWLOG_INFO("[rdkFwupdateMgr] create_result_response: Getting current image info\n");
    SWLOG_INFO("[rdkFwupdateMgr]   - currentImg status: %s\n", img_status ? "SUCCESS" : "FAILED");
    SWLOG_INFO("[rdkFwupdateMgr]   - current_img_buffer: '%s'\n", current_img_buffer);
    
    response.result = CHECK_FOR_UPDATE_SUCCESS;  // API call succeeded
    response.status_code = status_code;           // Firmware status
    response.current_img_version = g_strdup(img_status ? current_img_buffer : "Unknown");
    response.available_version = g_strdup("");
    response.update_details = g_strdup("");
    
    const gchar *default_status = "";
    if (status_message) {
        default_status = status_message;
    } else {
        switch(status_code) {
            case FIRMWARE_AVAILABLE:
                default_status = "Firmware update available";
                break;
            case FIRMWARE_NOT_AVAILABLE:
                default_status = "No firmware update available";
                break;
            case UPDATE_NOT_ALLOWED:
                default_status = "Firmware not compatible with this device model";
                break;
            case FIRMWARE_CHECK_ERROR:
                default_status = "Error checking for updates";
                break;
            case IGNORE_OPTOUT:
                default_status = "Firmware download not allowed - IGNORE_OPTOUT";
                break;
            case BYPASS_OPTOUT:
                default_status = "Firmware download not allowed - BYPASS_OPTOUT";
                break;
            default:
                default_status = "Unknown status";
                break;
        }
    }
    
    response.status_message = g_strdup(default_status);
    
    SWLOG_INFO("[rdkFwupdateMgr] create_result_response: Response created with current image: '%s', status: '%s'\n", 
               response.current_img_version, response.status_message);
    
    return response;
}

/**
 * @brief Create a CheckUpdateResponse for opt-out scenarios with firmware metadata.
 * 
 * Similar to create_success_response, but specifically for IGNORE_OPTOUT and BYPASS_OPTOUT
 * status codes. Always includes full firmware metadata so clients can display available
 * update information even when updates are blocked or require consent.
 * 
 * @param status_code Status code (IGNORE_OPTOUT or BYPASS_OPTOUT)
 * @param available_version Firmware version from XConf server
 * @param update_details Pipe-delimited firmware metadata string
 * @param status_message Custom status message explaining opt-out state
 * @return CheckUpdateResponse structure with allocated strings (must be freed by caller)
 */
#ifdef GTEST_ENABLE
CheckUpdateResponse create_optout_response(CheckForUpdateStatus status_code,
                                           const gchar *available_version,
                                           const gchar *update_details,
                                           const gchar *status_message)
#else
static CheckUpdateResponse create_optout_response(CheckForUpdateStatus status_code,
                                                  const gchar *available_version,
                                                  const gchar *update_details,
                                                  const gchar *status_message)
#endif
{
    CheckUpdateResponse response = {0};
    char current_img_buffer[256] = {0};
    
    bool img_status = GetFirmwareVersion(current_img_buffer, sizeof(current_img_buffer));
    
    SWLOG_INFO("[rdkFwupdateMgr] create_optout_response: Creating response for status_code=%d\n", status_code);
    SWLOG_INFO("[rdkFwupdateMgr]   - currentImg status: %s\n", img_status ? "SUCCESS" : "FAILED");
    SWLOG_INFO("[rdkFwupdateMgr]   - current_img_buffer: '%s'\n", current_img_buffer);
    
    response.result = CHECK_FOR_UPDATE_SUCCESS;  // API call succeeded
    response.status_code = status_code;           // IGNORE_OPTOUT or BYPASS_OPTOUT
    response.current_img_version = g_strdup(img_status ? current_img_buffer : "Unknown");
    response.available_version = g_strdup(available_version ? available_version : "");
    response.update_details = g_strdup(update_details ? update_details : "");
    response.status_message = g_strdup(status_message ? status_message : "");
    
    SWLOG_INFO("[rdkFwupdateMgr] create_optout_response: Response created with:\n");
    SWLOG_INFO("[rdkFwupdateMgr]   - current: '%s'\n", response.current_img_version);
    SWLOG_INFO("[rdkFwupdateMgr]   - available: '%s'\n", response.available_version);
    SWLOG_INFO("[rdkFwupdateMgr]   - status_message: '%s'\n", response.status_message);
    
    return response;
}

// *** NEW: Progress signal emission (main thread callback) ***

/**
 * @brief Emit DownloadProgress D-Bus signal (runs in main loop via g_idle_add)
 * 
 * This function is called by g_idle_add() to emit a DownloadProgress signal
 * from the main thread. It's the bridge between the progress monitor thread
 * and the D-Bus signal emission.
 * 
 * Signal Signature (must match D-Bus XML):
 * - handlerId (uint64): Client handler ID
 * - firmwareName (string): Firmware filename
 * - progress (uint32): Progress percentage 0-100
 * - status (string): "INPROGRESS", "COMPLETED", "DWNLERROR"
 * - message (string): Human-readable status message
 * 
 * Memory Management:
 * - Takes ownership of ProgressData and all its fields
 * - Frees handler_id, firmware_name, and ProgressData struct
 * - Does NOT free connection (borrowed pointer)
 * - Always returns FALSE to run only once
 * 
 * Thread Safety:
 * - Runs in main loop thread (GLib serialization guarantees)
 * - No mutex needed for D-Bus operations
 * 
 * @param user_data ProgressData* (must not be NULL, will be freed)
 * @return FALSE always (remove from idle queue after one run)
 */
 gboolean emit_download_progress_idle(gpointer user_data) {
    ProgressData* data = (ProgressData*)user_data;
    
    // NULL CHECK: Validate input
    if (data == NULL) {
        SWLOG_ERROR("[PROGRESS_IDLE] CRITICAL: NULL data in idle callback\n");
        return FALSE;  // Remove from idle queue
    }
    
    // NULL CHECK: Validate D-Bus connection
    if (data->connection == NULL) {
        SWLOG_ERROR("[PROGRESS_IDLE] CRITICAL: NULL D-Bus connection\n");
        // Still need to free allocated data
        if (data->handler_id) g_free(data->handler_id);
        if (data->firmware_name) g_free(data->firmware_name);
        g_free(data);
        return FALSE;
    }
    
    // NULL CHECK: Validate firmware name
    if (data->firmware_name == NULL) {
        SWLOG_ERROR("[PROGRESS_IDLE] ERROR: NULL firmware name, using placeholder\n");
        data->firmware_name = g_strdup("(unknown)");
    }
    
    // Convert handler_id string to uint64 for D-Bus signal
    guint64 handler_id_numeric = 0;
    if (data->handler_id != NULL) {
        handler_id_numeric = g_ascii_strtoull(data->handler_id, NULL, 10);
    }
    
    // Determine status string based on progress
    const gchar *status_str = "INPROGRESS";
    const gchar *message_str = "Download in progress";
    
    if (data->progress_percent >= 100) {
        status_str = "COMPLETED";
        message_str = "Download completed successfully";
    } else if (data->progress_percent == 0 && data->total_bytes == 0) {
        status_str = "NOTSTARTED";
        message_str = "Download starting";
    }
    
    // Build signal variant: (t handlerId, s firmwareName, u progress, s status, s message)
    GVariant *signal_data = g_variant_new("(tsuss)",
        handler_id_numeric,                      // handlerId (uint64)
        data->firmware_name,                     // firmwareName (string)
        (guint32)data->progress_percent,         // progress (uint32)
        status_str,                              // status (string)
        message_str                              // message (string)
    );
    
    // NULL CHECK: Validate variant creation
    if (signal_data == NULL) {
        SWLOG_ERROR("[PROGRESS_IDLE] ERROR: Failed to create signal variant\n");
        goto cleanup;
    }
    
    SWLOG_DEBUG("[PROGRESS_IDLE] Emitting DownloadProgress: %d%% (%llu/%llu bytes)\n",
               data->progress_percent,
               (unsigned long long)data->bytes_downloaded,
               (unsigned long long)data->total_bytes);
    
    // Emit D-Bus signal
    GError *error = NULL;
    gboolean signal_sent = g_dbus_connection_emit_signal(
        data->connection,
        NULL,  // NULL destination = broadcast to all listeners
        "/org/rdkfwupdater/Service",  // Object path
        "org.rdkfwupdater.Interface",  // Interface name
        "DownloadProgress",  // Signal name
        signal_data,
        &error
    );
    
    // Check emission result
    if (signal_sent) {
        SWLOG_DEBUG("[PROGRESS_IDLE] Signal emitted successfully\n");
    } else {
        SWLOG_ERROR("[PROGRESS_IDLE] Signal emission FAILED: %s\n", 
                   error ? error->message : "Unknown error");
        if (error) {
            g_error_free(error);
            error = NULL;
        }
    }
    
cleanup:
    // Cleanup: Free all allocated data
    if (data->handler_id) {
        g_free(data->handler_id);
        data->handler_id = NULL;
    }
    if (data->firmware_name) {
        g_free(data->firmware_name);
        data->firmware_name = NULL;
    }
    g_free(data);
    data = NULL;
    
    return FALSE;  // Remove from idle queue (run only once)
}

// *** NEW: Progress monitoring thread implementation ***

/**
 * @brief Progress monitor thread that polls /opt/curl_progress file
 * 
 * This thread runs in parallel with rdkv_upgrade_request() and performs the following:
 * 1. Polls /opt/curl_progress every 100ms
 * 2. Parses "UP: X of Y  DOWN: dlnow of dltotal" format
 * 3. Emits D-Bus DownloadProgress signal when progress changes by ≥1%
 * 4. Detects stalled downloads (no progress for 10 minutes)
 * 5. Cleans up all resources before exiting
 * 
 * Thread Safety:
 * - Uses g_atomic_int_get() for stop_flag (lock-free atomic read)
 * - Uses mutex to protect last_dlnow for race-free updates
 * - Emits D-Bus signals via g_idle_add() for thread-safe main thread dispatch
 * 
 * Memory Management:
 * - Frees handler_id and firmware_name (g_strdup'd copies)
 * - Clears and frees mutex
 * - Frees ProgressMonitorContext at thread exit
 * - Does NOT free connection (borrowed pointer)
 * 
 * @param user_data ProgressMonitorContext* (must not be NULL)
 * @return NULL always
 */
gpointer rdkfw_progress_monitor_thread(gpointer user_data) {
    ProgressMonitorContext* ctx = (ProgressMonitorContext*)user_data;
    FILE* progress_file = NULL;
    guint64 dlnow = 0;
    guint64 dltotal = 0;
    char line[512];
    gint consecutive_failures = 0;
    gboolean has_started = FALSE;
    
    // NULL CHECK: Validate input context
    if (ctx == NULL) {
        SWLOG_ERROR("[PROGRESS_MONITOR] CRITICAL: NULL context passed to thread\n");
        return NULL;
    }
    
    // NULL CHECK: Validate critical fields
    if (ctx->stop_flag == NULL) {
        SWLOG_ERROR("[PROGRESS_MONITOR] CRITICAL: NULL stop_flag in context\n");
        goto cleanup;
    }
    
    if (ctx->mutex == NULL) {
        SWLOG_ERROR("[PROGRESS_MONITOR] CRITICAL: NULL mutex in context\n");
        goto cleanup;
    }
    
    if (ctx->connection == NULL) {
        SWLOG_ERROR("[PROGRESS_MONITOR] CRITICAL: NULL D-Bus connection in context\n");
        goto cleanup;
    }
    
    SWLOG_INFO("[PROGRESS_MONITOR] Thread started successfully\n");
    SWLOG_INFO("[PROGRESS_MONITOR]   Handler ID: %s\n", ctx->handler_id ? ctx->handler_id : "(null)");
    SWLOG_INFO("[PROGRESS_MONITOR]   Firmware: %s\n", ctx->firmware_name ? ctx->firmware_name : "(null)");
    SWLOG_INFO("[PROGRESS_MONITOR]   Poll interval: %d ms\n", PROGRESS_POLL_INTERVAL_MS);
    SWLOG_INFO("[PROGRESS_MONITOR]   Progress file: %s\n", CURL_PROGRESS_FILE);
    
    // Initialize activity timestamp
    ctx->last_activity_time = time(NULL);
    
    // Main monitoring loop - runs until stop_flag is set
    while (!g_atomic_int_get(ctx->stop_flag)) {
        // Clear previous data
        line[0] = '\0';
        dlnow = 0;
        dltotal = 0;
        
        // Attempt to open progress file (written by curl's xferinfo callback)
        progress_file = fopen(CURL_PROGRESS_FILE, "r");
        if (progress_file == NULL) {
            // File doesn't exist yet (download not started, completed, or file deleted)
            consecutive_failures++;
            
            // Log periodically while waiting (every 5 seconds = 50 polls * 100ms)
            if (consecutive_failures == 50 && !has_started) {
                SWLOG_DEBUG("[PROGRESS_MONITOR] Waiting for download to start (file not found)...\n");
                consecutive_failures = 0;  // Reset counter for next log message
            }
            
            // Check for timeout if download has started
            if (has_started) {
                time_t now = time(NULL);
                time_t elapsed = now - ctx->last_activity_time;
                if (elapsed > PROGRESS_MONITOR_TIMEOUT_SEC) {
                    SWLOG_ERROR("[PROGRESS_MONITOR] TIMEOUT: No progress for %d seconds\n", 
                               PROGRESS_MONITOR_TIMEOUT_SEC);
                    // Worker thread will detect download failure via rdkv_upgrade_request() return code
                    // We just stop monitoring
                    break;
                }
            }
            
            // Sleep before next poll attempt
            g_usleep(PROGRESS_POLL_INTERVAL_MS * 1000);  // Convert ms to microseconds
            continue;
        }
        
        // File opened successfully
        consecutive_failures = 0;
        has_started = TRUE;
        
        // Read progress line
        // Expected format: "UP: ulnow of ultotal  DOWN: dlnow of dltotal"
        // Example: "UP: 0 of 0  DOWN: 52428800 of 104857600"
        if (fgets(line, sizeof(line), progress_file) == NULL) {
            // File is empty or read error occurred
            fclose(progress_file);
            /* Coverity fix: UNUSED_VALUE - Removed progress_file = NULL assignment.
             * The value is immediately overwritten in next loop iteration (line 685). */
            g_usleep(PROGRESS_POLL_INTERVAL_MS * 1000);
            continue;
        }
        
        // Close file immediately after reading to avoid holding it open
        fclose(progress_file);
        /* Coverity fix: UNUSED_VALUE - Removed progress_file = NULL assignment.
         * The value is immediately overwritten in next loop iteration (line 685). */
        
        // Parse download progress (we ignore upload progress for firmware downloads)
        // sscanf returns number of successfully parsed items
        int parsed = sscanf(line, "UP: %*u of %*u  DOWN: %llu of %llu", 
                           (unsigned long long*)&dlnow, 
                           (unsigned long long*)&dltotal);
        
        if (parsed != 2) {
            // Parsing failed - malformed line or unexpected format
            SWLOG_DEBUG("[PROGRESS_MONITOR] Failed to parse line: '%s'\n", line);
            g_usleep(PROGRESS_POLL_INTERVAL_MS * 1000);
            continue;
        }
        
        // Successfully parsed progress data
        // Now determine if we should emit a progress signal
        
        // Lock mutex for thread-safe access to last_dlnow
        g_mutex_lock(ctx->mutex);
        
        gboolean should_emit = FALSE;
        guint64 prev_dlnow = ctx->last_dlnow;
        
        // Decision logic for emitting progress signal
        if (dltotal > 0 && dlnow != prev_dlnow) {
            // Progress has changed
            
            // Calculate percentage change since last emission
            gdouble percent_change = 0.0;
            if (prev_dlnow > 0) {
                percent_change = ((gdouble)(dlnow - prev_dlnow) / (gdouble)dltotal) * 100.0;
            }
            
            // Emit if: first update (prev==0), changed by ≥1%, or completed (100%)
            if (prev_dlnow == 0 || 
                percent_change >= PROGRESS_THROTTLE_PERCENT || 
                dlnow >= dltotal) {
                should_emit = TRUE;
                ctx->last_dlnow = dlnow;
                ctx->last_activity_time = time(NULL);  // Update activity timestamp
            }
        } else if (dltotal == 0 && dlnow == 0 && prev_dlnow != 0) {
            // Download restarted or reset
            should_emit = TRUE;
            ctx->last_dlnow = 0;
            ctx->last_activity_time = time(NULL);
        }
        
        g_mutex_unlock(ctx->mutex);
        
        // Emit D-Bus signal outside mutex to avoid potential deadlocks
        if (should_emit) {
            // NULL CHECK: Validate connection before allocating
            if (ctx->connection == NULL) {
                SWLOG_ERROR("[PROGRESS_MONITOR] ERROR: D-Bus connection is NULL, skipping emission\n");
                g_usleep(PROGRESS_POLL_INTERVAL_MS * 1000);
                continue;
            }
            
            // Allocate progress data for g_idle_add callback
            ProgressData* progress_data = g_new0(ProgressData, 1);
            if (progress_data == NULL) {
                SWLOG_ERROR("[PROGRESS_MONITOR] ERROR: Memory allocation failed for progress data\n");
                g_usleep(PROGRESS_POLL_INTERVAL_MS * 1000);
                continue;
            }
            
            // Populate progress data (make copies of strings)
            progress_data->connection = ctx->connection;  // Borrowed pointer (do not free)
            progress_data->handler_id = ctx->handler_id ? g_strdup(ctx->handler_id) : NULL;
            progress_data->firmware_name = ctx->firmware_name ? g_strdup(ctx->firmware_name) : NULL;
            progress_data->bytes_downloaded = dlnow;
            progress_data->total_bytes = dltotal;
            
            // Calculate percentage
            if (dltotal > 0) {
                progress_data->progress_percent = (guint32)(((gdouble)dlnow / (gdouble)dltotal) * 100.0);
                if (progress_data->progress_percent > 100) {
                    progress_data->progress_percent = 100;
                }
            } else {
                progress_data->progress_percent = 0;
            }
            
            // Schedule signal emission on main thread (thread-safe)
            g_idle_add(emit_download_progress_idle, progress_data);
            
            SWLOG_DEBUG("[PROGRESS_MONITOR] Progress update: %d%% (%llu/%llu bytes)\n",
                       progress_data->progress_percent,
                       (unsigned long long)dlnow, 
                       (unsigned long long)dltotal);
        }
        
        // Wait before next poll
        g_usleep(PROGRESS_POLL_INTERVAL_MS * 1000);
    }
    
    SWLOG_INFO("[PROGRESS_MONITOR] Thread stopping (stop_flag=%d)\n", 
               g_atomic_int_get(ctx->stop_flag));

cleanup:
    /* Coverity fix: DEADCODE - Removed unreachable fclose(progress_file) check.
     * progress_file is always closed and set to NULL within the loop (lines 723, 730-731)
     * before any break/continue, so it's always NULL when reaching this cleanup label. */
    
    // Cleanup: Free all allocated resources
    if (ctx != NULL) {
        // Clear and free mutex
        if (ctx->mutex != NULL) {
            g_mutex_clear(ctx->mutex);
            g_free(ctx->mutex);
            ctx->mutex = NULL;
        }
        
        // Free handler_id (g_strdup'd copy)
        if (ctx->handler_id != NULL) {
            g_free(ctx->handler_id);
            ctx->handler_id = NULL;
        }
        
        // Free firmware_name (g_strdup'd copy)
        if (ctx->firmware_name != NULL) {
            g_free(ctx->firmware_name);
            ctx->firmware_name = NULL;
        }
        
        // Do NOT free connection (borrowed pointer, owned by D-Bus server)
        
        // Free context structure itself
        g_free(ctx);
        ctx = NULL;
    }
    
    SWLOG_INFO("[PROGRESS_MONITOR] Thread exited cleanly with all resources freed\n");
    return NULL;
}

/**
 * @brief Check for available firmware updates via XConf server query.
 * 
 * This is the core implementation of the CheckForUpdate D-Bus method. It performs:
 * 1. XConf server query (with cache support for offline scenarios)
 * 2. Firmware validation (model matching, version comparison)
 * 3. Response structure population with update details
 * 
 * Flow:
 * - First checks local cache for recent XConf response (to handle offline/recovery)
 * - If cache miss, queries XConf server directly via fetch_xconf_firmware_info()
 * - Validates firmware is for correct device model via processJsonResponse()
 * - Compares available version with current firmware version
 * - Returns structured response with all metadata for client decision-making
 * 
 * @param handler_id Client identifier for logging/tracking (must not be NULL)
 * @return CheckUpdateResponse structure with result_code, versions, and details
 *         Caller must free response using checkupdate_response_free()
 */
CheckUpdateResponse rdkFwupdateMgr_checkForUpdate(const gchar *handler_id) {
    
    SWLOG_INFO("[rdkFwupdateMgr] ===== FUNCTION ENTRY: rdkFwupdateMgr_checkForUpdate() =====\n");
    
    if (!handler_id) {
        SWLOG_ERROR("[rdkFwupdateMgr] CRITICAL ERROR: handler_id is NULL!\n");
        return create_result_response(FIRMWARE_CHECK_ERROR, "Internal error - invalid handler ID");
    }
    
    SWLOG_INFO("[rdkFwupdateMgr] CheckForUpdate: handler=%s\n", handler_id);
    SWLOG_INFO("[rdkFwupdateMgr] About to allocate XCONFRES structure on stack (~2KB)...\n");
    
    XCONFRES response = {0};
    
    SWLOG_INFO("[rdkFwupdateMgr] XCONFRES structure allocated successfully\n");
    int http_code = 0;
    int server_type = HTTP_XCONF_DIRECT;
    int ret = -1;
    
        SWLOG_INFO("[rdkFwupdateMgr] Making live XConf call\n");
        ret = fetch_xconf_firmware_info(&response, server_type, &http_code);
        
        if (ret == 0 && http_code == 200) {
            // Validate firmware is intended for this device model
            SWLOG_INFO("[rdkFwupdateMgr] ===== VALIDATE XCONF RESPONSE =====\n");
            int validation_result = processJsonResponse(&response,
                                                        cur_img_detail.cur_img_name,
                                                        device_info.model,
                                                        device_info.maint_status);

            SWLOG_INFO("[rdkFwupdateMgr] processJsonResponse returned: %d (0=success, 1=failed)\n", validation_result);
            if (validation_result != 0) {
                SWLOG_ERROR("[rdkFwupdateMgr] VALIDATION FAILED - Firmware not valid for this device\n");
                SWLOG_ERROR("[rdkFwupdateMgr]   - Device model: '%s'\n", device_info.model);
                SWLOG_ERROR("[rdkFwupdateMgr]   - cloudFWFile: '%s'\n", response.cloudFWFile);
                SWLOG_ERROR("[rdkFwupdateMgr]   - Reason: Model name not found in firmware filename\n");
                return create_result_response(UPDATE_NOT_ALLOWED, "Firmware validation failed - not for this device model");
            }
            SWLOG_INFO("[rdkFwupdateMgr] VALIDATION PASSED - Firmware is valid for this device\n");
            SWLOG_INFO("[rdkFwupdateMgr] ===== VALIDATION & COMPARISON COMPLETE =====\n");
        }
    
    SWLOG_INFO("[rdkFwupdateMgr] XConf call completed with result: ret=%d\n",ret);
    
    // Process successful XConf response
    if (ret == 0 && http_code == 200) {
        SWLOG_INFO("=== [rdkFwupdateMgr] XConf Response - Complete Data ===\n");
        SWLOG_INFO("[rdkFwupdateMgr] Core Firmware Data:\n");
        SWLOG_INFO("[rdkFwupdateMgr]   - cloudFWVersion: '%s'\n", 
                   response.cloudFWVersion[0] ? response.cloudFWVersion : "(empty)");
        SWLOG_INFO("[rdkFwupdateMgr]   - cloudFWFile: '%s'\n", 
                   response.cloudFWFile[0] ? response.cloudFWFile : "(empty)");
        SWLOG_INFO("[rdkFwupdateMgr]   - cloudFWLocation: '%s'\n", 
                   response.cloudFWLocation[0] ? response.cloudFWLocation : "(empty)");
        SWLOG_INFO("[rdkFwupdateMgr]   - ipv6cloudFWLocation: '%s'\n", 
                   response.ipv6cloudFWLocation[0] ? response.ipv6cloudFWLocation : "(empty)");
        
        SWLOG_INFO("[rdkFwupdateMgr] Download Control:\n");
        SWLOG_INFO("[rdkFwupdateMgr]   - cloudDelayDownload: '%s'\n", 
                   response.cloudDelayDownload[0] ? response.cloudDelayDownload : "(empty)");
        SWLOG_INFO("[rdkFwupdateMgr]   - cloudProto: '%s'\n", 
                   response.cloudProto[0] ? response.cloudProto : "(empty)");
        SWLOG_INFO("[rdkFwupdateMgr]   - cloudImmediateRebootFlag: '%s'\n", 
                   response.cloudImmediateRebootFlag[0] ? response.cloudImmediateRebootFlag : "(empty)");
        
        SWLOG_INFO("[rdkFwupdateMgr] Additional Components:\n");
        SWLOG_INFO("[rdkFwupdateMgr]   - peripheralFirmwares: '%s'\n", 
                   response.peripheralFirmwares[0] ? response.peripheralFirmwares : "(empty)");
        SWLOG_INFO("[rdkFwupdateMgr]   - dlCertBundle: '%s'\n", 
                   response.dlCertBundle[0] ? response.dlCertBundle : "(empty)");
        SWLOG_INFO("[rdkFwupdateMgr]   - cloudPDRIVersion: '%s'\n", 
                   response.cloudPDRIVersion[0] ? response.cloudPDRIVersion : "(empty)");
        SWLOG_INFO("=== [rdkFwupdateMgr] XConf Response - End ===\n"); 
       
        // Check if firmware version is present
        if (!response.cloudFWVersion[0] || strlen(response.cloudFWVersion) == 0) {
            SWLOG_INFO("[rdkFwupdateMgr] XConf returned no firmware version - no update available\n");
            return create_result_response(FIRMWARE_NOT_AVAILABLE, "No firmware update available");
        }
        
        SWLOG_INFO("[rdkFwupdateMgr] XConf returned firmware version: '%s'\n", response.cloudFWVersion);
        
        // Serialize XConf metadata into pipe-delimited string for D-Bus transport
        gchar *update_details = g_strdup_printf(
                "File:%s|Location:%s|IPv6Location:%s|Version:%s|Protocol:%s|Reboot:%s|Delay:%s|PDRI:%s|Peripherals:%s|CertBundle:%s", 
                response.cloudFWFile[0] ? response.cloudFWFile : "N/A",
                response.cloudFWLocation[0] ? response.cloudFWLocation : "N/A", 
                response.ipv6cloudFWLocation[0] ? response.ipv6cloudFWLocation : "N/A",
                response.cloudFWVersion[0] ? response.cloudFWVersion : "N/A",
                response.cloudProto[0] ? response.cloudProto : "HTTP",
                response.cloudImmediateRebootFlag[0] ? response.cloudImmediateRebootFlag : "false",
                response.cloudDelayDownload[0] ? response.cloudDelayDownload : "0",
                response.cloudPDRIVersion[0] ? response.cloudPDRIVersion : "N/A",
                response.peripheralFirmwares[0] ? response.peripheralFirmwares : "N/A",
                response.dlCertBundle[0] ? response.dlCertBundle : "N/A"
            );
        
        // ===== POST-XCONF OPT-OUT EVALUATION =====
        SWLOG_INFO("[rdkFwupdateMgr] ===== BEGIN POST-XCONF OPT-OUT EVALUATION =====\n");
        
        // Parse critical update flag from XConf response
        bool isCriticalUpdate = false;
        if (strncmp(response.cloudImmediateRebootFlag, "true", 4) == 0) {
            isCriticalUpdate = true;
            SWLOG_INFO("[rdkFwupdateMgr] CRITICAL UPDATE DETECTED (cloudImmediateRebootFlag=true)\n");
        } else {
            SWLOG_INFO("[rdkFwupdateMgr] Non-critical update (cloudImmediateRebootFlag=%s)\n",
                      response.cloudImmediateRebootFlag[0] ? response.cloudImmediateRebootFlag : "false");
        }
        
        // Check 1: Is Maintenance Manager integration active?
        SWLOG_INFO("[rdkFwupdateMgr] Checking maint_status: '%s'\n", device_info.maint_status);
        if (strncmp(device_info.maint_status, "true", 4) != 0) {
            SWLOG_INFO("[rdkFwupdateMgr] MaintenanceMGR not active (maint_status != 'true') - skipping opt-out logic\n");
            SWLOG_INFO("[rdkFwupdateMgr] ===== END OPT-OUT EVALUATION: NORMAL FLOW =====\n");
            CheckUpdateResponse result = create_success_response(
                response.cloudFWVersion,
                update_details,
                "Firmware update available"
            );
            g_free(update_details);
            return result;
        }
        
        // Check 2: Is opt-out feature enabled for this device?
        SWLOG_INFO("[rdkFwupdateMgr] Checking sw_optout: '%s'\n", device_info.sw_optout);
        if (strncmp(device_info.sw_optout, "true", 4) != 0) {
            SWLOG_INFO("[rdkFwupdateMgr] Opt-out feature disabled (sw_optout != 'true') - skipping opt-out logic\n");
            SWLOG_INFO("[rdkFwupdateMgr] ===== END OPT-OUT EVALUATION: NORMAL FLOW =====\n");
            CheckUpdateResponse result = create_success_response(
                response.cloudFWVersion,
                update_details,
                "Firmware update available"
            );
            g_free(update_details);
            return result;
        }
        
        // Check 3: Read user's opt-out preference
        SWLOG_INFO("[rdkFwupdateMgr] Reading opt-out preference from /opt/maintenance_mgr_record.conf\n");
        int optout = getOPTOUTValue("/opt/maintenance_mgr_record.conf");
        SWLOG_INFO("[rdkFwupdateMgr] Opt-out value: %d (-1=not set, 0=ENFORCE_OPTOUT, 1=IGNORE_UPDATE)\n", optout);
        
        if (optout == -1) {
            SWLOG_INFO("[rdkFwupdateMgr] No opt-out preference set (file missing or no value) - allowing update\n");
            SWLOG_INFO("[rdkFwupdateMgr] ===== END OPT-OUT EVALUATION: NORMAL FLOW =====\n");
            CheckUpdateResponse result = create_success_response(
                response.cloudFWVersion,
                update_details,
                "Firmware update available"
            );
            g_free(update_details);
            return result;
        }
        
        // Check 4: Apply opt-out decision logic
        if (optout == 1) {
            // User has opted out (IGNORE_UPDATE)
            if (isCriticalUpdate) {
                // Critical update bypasses opt-out
                SWLOG_INFO("[rdkFwupdateMgr] CRITICAL UPDATE OVERRIDE: Bypassing user opt-out\n");
                SWLOG_INFO("[rdkFwupdateMgr] ===== END OPT-OUT EVALUATION: CRITICAL BYPASS =====\n");
                CheckUpdateResponse result = create_success_response(
                    response.cloudFWVersion,
                    update_details,
                    "Critical firmware update available (security/stability)"
                );
                g_free(update_details);
                return result;
            } else {
                // Non-critical update blocked by user
                SWLOG_INFO("[rdkFwupdateMgr] BLOCKING UPDATE: User opted out (IGNORE_UPDATE), non-critical firmware\n");
                SWLOG_INFO("[rdkFwupdateMgr] ===== END OPT-OUT EVALUATION: RETURNING IGNORE_OPTOUT =====\n");
                CheckUpdateResponse result = create_optout_response(
                    IGNORE_OPTOUT,
                    response.cloudFWVersion,
                    update_details,
                    "Firmware download blocked - user has opted out of updates"
                );
                g_free(update_details);
                return result;
            }
        }
        else if (optout == 0) {
            // User requires consent (ENFORCE_OPTOUT)
            SWLOG_INFO("[rdkFwupdateMgr] CONSENT REQUIRED: User has ENFORCE_OPTOUT set\n");
            SWLOG_INFO("[rdkFwupdateMgr] ===== END OPT-OUT EVALUATION: RETURNING BYPASS_OPTOUT =====\n");
            CheckUpdateResponse result = create_optout_response(
                BYPASS_OPTOUT,
                response.cloudFWVersion,
                update_details,
                "Firmware available - user consent required before installation"
            );
            g_free(update_details);
            return result;
        }
        
        // Defensive: Should not reach here, but return normal flow
        SWLOG_WARN("[rdkFwupdateMgr] WARNING: Unexpected opt-out value path - returning normal flow\n");
        SWLOG_INFO("[rdkFwupdateMgr] ===== END OPT-OUT EVALUATION: FALLBACK NORMAL FLOW =====\n");
        CheckUpdateResponse result = create_success_response(
            response.cloudFWVersion,
            update_details,
            "Firmware update available"
        );
        g_free(update_details);
        return result;
    } else {
        // XConf query failed - network or server error
        SWLOG_ERROR("[rdkFwupdateMgr] XConf communication failed: ret=%d, http=%d\n", ret, http_code);
        
        if (http_code != 200) {
            return create_result_response(FIRMWARE_CHECK_ERROR, "Network error - unable to reach update server");
        } else {
            return create_result_response(FIRMWARE_CHECK_ERROR, "Update check failed - server communication error");
        }
    }
}


/*
 * ===================================================================
 * UpdateFirmware Worker Thread Implementation
 * ===================================================================
 * 
 * This section implements the async firmware flash operation using
 * GThread worker pattern similar to DownloadFirmware implementation.
 * 
 * Architecture:
 * - Worker thread calls flashImage() from librdksw_flash.so
 * - Progress signals emitted via g_idle_add() for thread safety
 * - Supports all 13 scenarios from sequence diagram
 * - Handles PCI, PDRI, and PERIPHERAL firmware types
 * 
 * Signal Flow:
 * Worker Thread ->g_idle_add() ->Main Loop-> D-Bus Signal ->Client
 * ===================================================================
 */

/**
 * @brief Progress emission idle callback for UpdateProgress signal
 * 
 * Called by GLib main loop via g_idle_add() to emit UpdateProgress D-Bus signal.
 * This marshals progress updates from the worker thread to the main loop thread
 * in a thread-safe manner.
 * 
 * Signal Format:
 *   UpdateProgress(handlerId, firmwareName, progress, status, message)
 *   - handlerId: uint64 - Client handler ID
 *   - firmwareName: string - Firmware filename
 *   - progress: int32 - Progress percentage (0-100, -1=error)
 *   - status: int32 - FwUpdateStatus enum (0=INPROGRESS, 1=COMPLETED, 2=ERROR)
 *   - message: string - Human-readable status message
 * 
 * @param user_data FlashProgressUpdate* containing progress data
 * @return G_SOURCE_REMOVE (one-shot callback, don't reschedule)
 * 
 * Thread Safety: Runs in main loop thread, safe to call D-Bus functions
 * Memory Management: Frees FlashProgressUpdate and all owned strings
 * 
 * Logging: INFO level for successful emissions, ERROR for failures
 */
/**
 * @brief Emit UpdateProgress D-Bus signal (called via g_idle_add from worker thread)
 * 
 * Non-static to allow external linkage from rdkv_dbus_server.c
 */
gboolean emit_flash_progress_idle(gpointer user_data)
{
    FlashProgressUpdate *update = (FlashProgressUpdate*)user_data;
    
    if (!update) {
        SWLOG_ERROR("[FLASH_PROGRESS] CRITICAL: emit_flash_progress_idle called with NULL data\n");
        return G_SOURCE_REMOVE;
    }
    
    SWLOG_INFO("[FLASH_PROGRESS] ===== Emitting UpdateProgress Signal =====\n");
    SWLOG_INFO("[FLASH_PROGRESS] Handler ID: %s\n", 
               update->handler_id ? update->handler_id : "NULL");
    SWLOG_INFO("[FLASH_PROGRESS] Firmware: '%s'\n", 
               update->firmware_name ? update->firmware_name : "NULL");
    SWLOG_INFO("[FLASH_PROGRESS] Progress: %d%%\n", update->progress);
    SWLOG_INFO("[FLASH_PROGRESS] Status: %d (%s)\n", update->status,
               update->status == FW_UPDATE_INPROGRESS ? "INPROGRESS" :
               update->status == FW_UPDATE_COMPLETED ? "COMPLETED" : "ERROR");
    
    // Construct human-readable message based on status and progress
    const gchar *status_msg;
    if (update->status == FW_UPDATE_ERROR) {
        // Error scenario: Use provided error message or default
        status_msg = update->error_message ? update->error_message : "Flash operation failed";
        SWLOG_ERROR("[FLASH_PROGRESS] Error message: %s\n", status_msg);
    } else if (update->status == FW_UPDATE_COMPLETED) {
        // Success scenario: Flash completed
        status_msg = "Flash completed successfully";
        SWLOG_INFO("[FLASH_PROGRESS] Flash operation completed successfully\n");
    } else {
        // In-progress scenario: Generate message based on progress percentage
        if (update->progress == 0) {
            status_msg = "Flash started";
        } else if (update->progress < 25) {
            status_msg = "Verifying firmware image";
        } else if (update->progress < 50) {
            status_msg = "Flashing in progress";
        } else if (update->progress < 75) {
            status_msg = "Flash operation continuing";
        } else if (update->progress < 100) {
            status_msg = "Nearing completion";
        } else {
            status_msg = "Flash operation in progress";
        }
    }
    
    // Convert handler_id string to uint64 for D-Bus signal
    guint64 handler_id_numeric = 0;
    if (update->handler_id) {
        handler_id_numeric = g_ascii_strtoull(update->handler_id, NULL, 10);
    }
    
    // Emit D-Bus signal: UpdateProgress
    // Signal path: /org/rdkfwupdater/Service
    // Interface: org.rdkfwupdater.Interface
    GError *signal_error = NULL;
    gboolean signal_emitted = g_dbus_connection_emit_signal(
        update->connection,
        NULL,  // Broadcast to all listeners (no specific destination)
        "/org/rdkfwupdater/Service",
        "org.rdkfwupdater.Interface",
        "UpdateProgress",
        g_variant_new("(tsiis)",
                      handler_id_numeric,
                      update->firmware_name ? update->firmware_name : "",
                      update->progress,
                      (gint32)update->status,
                      status_msg),
        &signal_error);
    
    if (!signal_emitted) {
        SWLOG_ERROR("[FLASH_PROGRESS] FAILED to emit UpdateProgress signal: %s\n",
                    signal_error ? signal_error->message : "unknown error");
        if (signal_error) {
            SWLOG_ERROR("[FLASH_PROGRESS] Error domain: %s, code: %d\n",
                       g_quark_to_string(signal_error->domain), signal_error->code);
            g_error_free(signal_error);
               }
    } else {
        SWLOG_INFO("[FLASH_PROGRESS] UpdateProgress signal emitted successfully\n");
        SWLOG_INFO("[FLASH_PROGRESS] Signal details: handlerId=%"G_GUINT64_FORMAT", "
                   "firmware='%s', progress=%d%%, status=%d\n",
                   handler_id_numeric, 
                   update->firmware_name ? update->firmware_name : "",
                   update->progress, update->status);
    }
    
    // Cleanup: Free all owned strings and the update structure
    SWLOG_INFO("[FLASH_PROGRESS] Cleaning up FlashProgressUpdate structure\n");
    g_free(update->handler_id);
    g_free(update->firmware_name);
    g_free(update->error_message);
    g_free(update);
    
    SWLOG_INFO("[FLASH_PROGRESS] ===== UpdateProgress Signal Emission Complete =====\n");
    
    return G_SOURCE_REMOVE;  // One-shot callback, don't reschedule
}

/**
 * @brief Cleanup global flash state (called via g_idle_add)
 * 
 * Safely cleans up current_flash from the main loop thread.
 * This must run in the main loop to avoid race conditions with D-Bus handlers
 * that check IsFlashInProgress and current_flash state.
 * 
 * Thread Safety:
 * - Must run in main loop thread (enforced by g_idle_add)
 * - No mutex needed due to GLib main loop serialization
 * 
 * State Cleanup:
 * - Frees current_flash->firmware_name
 * - Frees current_flash structure
 * - Sets current_flash = NULL
 * - Sets IsFlashInProgress = FALSE
 * 
 * @param user_data Unused (can be NULL)
 * @return G_SOURCE_REMOVE (one-shot callback)
 * 
 * Logging: INFO level for successful cleanup, WARN if already cleaned
 * 
 * Non-static to allow external linkage from rdkv_dbus_server.c
 */
gboolean cleanup_flash_state_idle(gpointer user_data)
{
    (void)user_data;  // Unused parameter
    
    // current_flash and IsFlashInProgress are declared in rdkv_dbus_server.h
    
    SWLOG_INFO("[FLASH_CLEANUP] ===== Cleaning Up Global Flash State =====\n");
    
    if (current_flash) {
        SWLOG_INFO("[FLASH_CLEANUP] Freeing current_flash structure\n");
        SWLOG_INFO("[FLASH_CLEANUP]   Firmware name: '%s'\n",
                   current_flash->firmware_name ? current_flash->firmware_name : "NULL");
        SWLOG_INFO("[FLASH_CLEANUP]   Final progress: %d%%\n", current_flash->current_progress);
        SWLOG_INFO("[FLASH_CLEANUP]   Final status: %d\n", current_flash->status);
        
        // Free owned strings
        g_free(current_flash->firmware_name);
        
        // Free structure
        g_free(current_flash);
        current_flash = NULL;
        
        // Clear flash-in-progress flag
        IsFlashInProgress = FALSE;
        
        SWLOG_INFO("[FLASH_CLEANUP] Global flash state cleared successfully\n");
        SWLOG_INFO("[FLASH_CLEANUP] IsFlashInProgress = FALSE\n");
        SWLOG_INFO("[FLASH_CLEANUP] current_flash = NULL\n");
    } else {
        SWLOG_WARN("[FLASH_CLEANUP] current_flash already NULL (double cleanup attempt?)\n");
    }
    
    SWLOG_INFO("[FLASH_CLEANUP] ===== Flash State Cleanup Complete =====\n");
    
    return G_SOURCE_REMOVE;  // One-shot callback
}

/**
 * @brief Worker thread function for firmware flash operation
 * 
 * This function runs in a separate GThread and performs the actual firmware
 * flash operation by calling flashImage() from librdksw_flash.so. It emits
 * progress signals at key milestones (0%, 25%, 50%, 75%, 100%) and handles
 * all error scenarios.
 * 
 * Scenarios Handled (13 total):
 * - S1: PCI Success + Immediate Reboot (flash_result==0, immediate_reboot==TRUE)
 * - S2: PCI Success + Delayed Reboot (flash_result==0, immediate_reboot==FALSE)
 * - S3: PDRI Success (upgrade_type==PDRI_UPGRADE)
 * - S9: Flash Write Error (flash_result!=0)
 * - S11: Insufficient Storage (handled by flashImage(), returns error)
 * - S12: Custom Location (any valid firmware path)
 * - S13: Peripheral Update (upgrade_type==2)
 * 
 * Note: S4-S8, S10 are handled in D-Bus handler before worker spawn
 * 
 * flashImage() Parameters:
 * - server_url: Server URL (can be empty, used for telemetry)
 * - upgrade_file: Full path to firmware file
 * - reboot_flag: "true" or "false" string
 * - proto: "2" for HTTP protocol
 * - upgrade_type: 0=PCI, PDRI_UPGRADE=PDRI, 2=PERIPHERAL
 * - maint: "true" or "false" for maintenance mode
 * - trigger_type: 1=bootup, 2=cron, 3=TR69, 4=app, 5=delayed, 6=red_state
 * 
 * @param user_data AsyncFlashContext* containing flash parameters
 * @return NULL (thread exit value not used)
 * 
 * Thread Safety:
 * - Runs in worker thread
 * - Uses g_idle_add() for all D-Bus signal emissions (thread-safe)
 * - No direct access to global state (uses g_idle_add for cleanup)
 * 
 * Memory Management:
 * - Frees AsyncFlashContext and all owned strings before exit
 * - Clears and frees mutex
 * - Frees stop_flag
 * 
 * Progress Flow:
 * 1. 0% - Flash started
 * 2. 25% - Verification complete
 * 3. Call flashImage() - actual flash operation
 * 4. 50% - Flashing in progress (simulated)
 * 5. 75% - Nearing completion (simulated)
 * 6. 100% - Flash completed OR -1% - Flash error
 * no need of specific free for progress structure. emit_flash_progress_idle  will take care. 
 * Logging: Comprehensive INFO/ERROR logging at each stage
 */
gpointer rdkfw_flash_worker_thread(gpointer user_data)
{
    AsyncFlashContext *ctx = (AsyncFlashContext*)user_data;
    
    // Critical validation: Ensure context is not NULL
    if (!ctx) {
        SWLOG_ERROR("[FLASH_WORKER] CRITICAL: Thread started with NULL context\n");
        SWLOG_ERROR("[FLASH_WORKER] Cannot proceed, exiting thread immediately\n");
        return NULL;
    }
    
    SWLOG_INFO("[FLASH_WORKER] ========== FLASH WORKER THREAD STARTED ==========\n");
    SWLOG_INFO("[FLASH_WORKER] Thread ID: %p\n", (void*)g_thread_self());
    SWLOG_INFO("[FLASH_WORKER] Firmware: '%s'\n", ctx->firmware_name ? ctx->firmware_name : "NULL");
    SWLOG_INFO("[FLASH_WORKER] Type: '%s'\n", ctx->firmware_type ? ctx->firmware_type : "NULL");
    SWLOG_INFO("[FLASH_WORKER] Full path: '%s'\n", ctx->firmware_fullpath ? ctx->firmware_fullpath : "NULL");
    SWLOG_INFO("[FLASH_WORKER] Reboot: %s\n", ctx->immediate_reboot ? "IMMEDIATE" : "DEFERRED");
    SWLOG_INFO("[FLASH_WORKER] Handler ID: %s\n", ctx->handler_id ? ctx->handler_id : "NULL");
    
    int flash_result = -1;
    FlashProgressUpdate *progress = NULL;
    
    // PROGRESS 0%: FLASH STARTED
    SWLOG_INFO("[FLASH_WORKER] Emitting 0%% progress - Flash operation initiated\n");
    //FlashProgressUpdate *progress = NULL;
    /* Allocate and zero-initialize */
    progress = (FlashProgressUpdate *)calloc(1, sizeof(FlashProgressUpdate));
    if (!progress) {
	    SWLOG_ERROR("[FLASH_WORKER] Failed to allocate FlashProgressUpdate\n");
	    goto flash_error;
    }
    /* Populate fields */
    progress->connection = ctx->connection;
    progress->handler_id = g_strdup(ctx->handler_id);
    progress->firmware_name = g_strdup(ctx->firmware_name);
    progress->progress = 0;
    progress->status = FW_UPDATE_INPROGRESS;
    progress->error_message = NULL;
    g_idle_add(emit_flash_progress_idle, progress);
    usleep(500000);


   // progress = g_new0(FlashProgressUpdate, 1);
    //progress->connection = ctx->connection;
    //progress->handler_id = g_strdup(ctx->handler_id);
    //progress->firmware_name = g_strdup(ctx->firmware_name);
    //progress->progress = 0;
    //progress->status = FW_UPDATE_INPROGRESS;
    //progress->error_message = NULL;
    //g_idle_add(emit_flash_progress_idle, progress);
    //usleep(500000);
    
    // PROGRESS 25%: VERIFICATION
    SWLOG_INFO("[FLASH_WORKER] Pre-flash validation started\n");
    gboolean validation_passed = TRUE;
    
    if (ctx->firmware_fullpath && strlen(ctx->firmware_fullpath) > 0) {
        if (!g_file_test(ctx->firmware_fullpath, G_FILE_TEST_EXISTS)) {
            SWLOG_ERROR("[FLASH_WORKER] S12: Firmware file not found: %s\n", ctx->firmware_fullpath);
            validation_passed = FALSE;
        } else {
            SWLOG_INFO("[FLASH_WORKER] S12: Firmware file validated: %s\n", ctx->firmware_fullpath);
        }
    }
    
    if (validation_passed) {
        SWLOG_INFO("[FLASH_WORKER] Emitting 25%% progress\n");
	//FlashProgressUpdate *progress = NULL;
	/* Allocate and zero-initialize */
	progress = (FlashProgressUpdate *)calloc(1, sizeof(FlashProgressUpdate));
	if (!progress) {
        SWLOG_ERROR("[FLASH_WORKER] Failed to allocate FlashProgressUpdate\n");
        goto flash_error;
	}
	/* Populate fields */
	progress->connection = ctx->connection;
	progress->handler_id = g_strdup(ctx->handler_id);
	progress->firmware_name = g_strdup(ctx->firmware_name);
	progress->progress = 25;
	progress->status = FW_UPDATE_INPROGRESS;
	progress->error_message = NULL;
        g_idle_add(emit_flash_progress_idle, progress);
	usleep(500000);	
	
	//progress = g_new0(FlashProgressUpdate, 1);
        //progress->connection = ctx->connection;
        //progress->handler_id = g_strdup(ctx->handler_id);
        //progress->firmware_name = g_strdup(ctx->firmware_name);
        //progress->progress = 25;
       // progress->status = FW_UPDATE_INPROGRESS;
       // progress->error_message = NULL;
       // g_idle_add(emit_flash_progress_idle, progress);
       // usleep(500000);
    } else {
        goto flash_error;
    }
    
    // PROGRESS 50%: CALLING flashImage()
    SWLOG_INFO("[FLASH_WORKER] Building flashImage() parameters\n");
    const char *upgrade_file_path = ctx->firmware_fullpath ? ctx->firmware_fullpath : ctx->firmware_name;
    const char *server_url = ctx->server_url ? ctx->server_url : "";
    const char *reboot_flag = ctx->immediate_reboot ? "true" : "false";
    const char *proto = "2";
    const char *maint = "false";
    
    // Determine upgrade_type from firmware_type string
    int upgrade_type = 0;  // Default: PCI
    if (ctx->firmware_type) {
        if (g_strcmp0(ctx->firmware_type, "PDRI") == 0) {
            upgrade_type = PDRI_UPGRADE;  // Usually defined as 1
        } else if (g_strcmp0(ctx->firmware_type, "PERIPHERAL") == 0) {
            upgrade_type = 2;
        }
    }
    
    SWLOG_INFO("[FLASH_WORKER] Emitting 50%% progress\n");
    //FlashProgressUpdate *progress = NULL;
    /* Allocate and zero-initialize */
    progress = (FlashProgressUpdate *)calloc(1, sizeof(FlashProgressUpdate));
    if (!progress) {
	    SWLOG_ERROR("[FLASH_WORKER] Failed to allocate FlashProgressUpdate\n");
	    goto flash_error;
    }
    /* Populate fields */
    progress->connection = ctx->connection;
    progress->handler_id = g_strdup(ctx->handler_id);
    progress->firmware_name = g_strdup(ctx->firmware_name);
    progress->progress = 50;
    progress->status = FW_UPDATE_INPROGRESS;
    progress->error_message = NULL;
    g_idle_add(emit_flash_progress_idle, progress);
    usleep(500000);

   // progress = g_new0(FlashProgressUpdate, 1);
   // progress->connection = ctx->connection;
   // progress->handler_id = g_strdup(ctx->handler_id);
   // progress->firmware_name = g_strdup(ctx->firmware_name);
   // progress->progress = 50;
   // progress->status = FW_UPDATE_INPROGRESS;
   // progress->error_message = NULL;
   // g_idle_add(emit_flash_progress_idle, progress);
   // usleep(1000000);
    
    SWLOG_INFO("[FLASH_WORKER] *** CALLING flashImage() ***\n");
#ifndef GTEST_ENABLE
    flash_result = flashImage(server_url, upgrade_file_path, reboot_flag, proto, 
                              upgrade_type, maint, ctx->trigger_type);
#else
    SWLOG_WARN("[FLASH_WORKER] GTEST: Simulating flashImage() = 0\n");
    flash_result = 0;
    sleep(2);
#endif
    SWLOG_INFO("[FLASH_WORKER] flashImage() returned: %d\n", flash_result);
    
    if (flash_result == 0) {
        // SUCCESS
        SWLOG_INFO("[FLASH_WORKER] FLASH SUCCESS\n");
        if (ctx->firmware_type && g_strcmp0(ctx->firmware_type, "PDRI") == 0) {
            SWLOG_INFO("[FLASH_WORKER] S3: PDRI upgrade successful\n");
        } else if (ctx->firmware_type && g_strcmp0(ctx->firmware_type, "PERIPHERAL") == 0) {
            SWLOG_INFO("[FLASH_WORKER] S13: Peripheral update successful\n");
        } else if (!ctx->immediate_reboot) {
            SWLOG_INFO("[FLASH_WORKER] S2: Deferred reboot successful\n");
        } else {
            SWLOG_INFO("[FLASH_WORKER] S1: Normal flash successful\n");
        }
        
        // PROGRESS 75%
	//FlashProgressUpdate *progress = NULL;                                                                                                                      
        /* Allocate and zero-initialize */                                                                                                                         
        progress = (FlashProgressUpdate *)calloc(1, sizeof(FlashProgressUpdate));                                                                                  
        if (!progress) {                                                                                                                                           
        SWLOG_ERROR("[FLASH_WORKER] Failed to allocate FlashProgressUpdate\n");                                                                                    
        goto flash_error;                                                                                                                                          
        }                                                                                                                                                          
        /* Populate fields */                                                                                                                                      
        progress->connection = ctx->connection;                                                                                                                    
        progress->handler_id = g_strdup(ctx->handler_id);                                                                                                          
        progress->firmware_name = g_strdup(ctx->firmware_name);                                                                                                    
        progress->progress = 75;                                                                                                                                   
        progress->status = FW_UPDATE_INPROGRESS;                                                                                                                   
        progress->error_message = NULL;
        g_idle_add(emit_flash_progress_idle, progress);                                                                                                            
        usleep(500000);                           


       // progress = g_new0(FlashProgressUpdate, 1);
        //progress->connection = ctx->connection;
        //progress->handler_id = g_strdup(ctx->handler_id);
        //progress->firmware_name = g_strdup(ctx->firmware_name);
        //progress->progress = 75;
        //progress->status = FW_UPDATE_INPROGRESS;
        //progress->error_message = NULL;
        //g_idle_add(emit_flash_progress_idle, progress);
        //usleep(500000);
        
        // PROGRESS 100%
        SWLOG_INFO("[FLASH_WORKER] Emitting 100%% progress - COMPLETE\n");
	FlashProgressUpdate *progress = NULL;
        /* Allocate and zero-initialize */
        progress = (FlashProgressUpdate *)calloc(1, sizeof(FlashProgressUpdate));
        if (!progress) {
        SWLOG_ERROR("[FLASH_WORKER] Failed to allocate FlashProgressUpdate\n");
        goto flash_error;
        }
        /* Populate fields */
        progress->connection = ctx->connection;
        progress->handler_id = g_strdup(ctx->handler_id);
        progress->firmware_name = g_strdup(ctx->firmware_name);
        progress->progress = 100;
        progress->status = FW_UPDATE_COMPLETED;
        progress->error_message = NULL;
        g_idle_add(emit_flash_progress_idle, progress);
        usleep(500000);


        //progress = g_new0(FlashProgressUpdate, 1);
        //progress->connection = ctx->connection;
        //progress->handler_id = g_strdup(ctx->handler_id);
       // progress->firmware_name = g_strdup(ctx->firmware_name);
       // progress->progress = 100;
       // progress->status = FW_UPDATE_COMPLETED;
       // progress->error_message = NULL;
       // g_idle_add(emit_flash_progress_idle, progress);
        
    } else {
        // FAILURE
flash_error:
        SWLOG_ERROR("[FLASH_WORKER] FLASH FAILED: %d\n", flash_result);
        if (flash_result == -28) {
            SWLOG_ERROR("[FLASH_WORKER] : Insufficient storage (ENOSPC)\n");
        } else if (flash_result == -2) {
            SWLOG_ERROR("[FLASH_WORKER] : File not found or missing resource (ENOENT)\n");
        } else {
            SWLOG_ERROR("[FLASH_WORKER] : Flash write error\n");
        }
        
       FlashProgressUpdate *progress = NULL;
        /* Allocate and zero-initialize */
        progress = (FlashProgressUpdate *)calloc(1, sizeof(FlashProgressUpdate));
        if (!progress) {
        SWLOG_ERROR("[FLASH_WORKER] Failed to allocate FlashProgressUpdate\n");
        //goto flash_error;  // Don't retry to allocate memory once calloc failed. otherwise it might  cause infinite loop 
        }
	else {
        /* Populate fields */ // these will get allocated only if progress is not NULL
        progress->connection = ctx->connection;
        progress->handler_id = g_strdup(ctx->handler_id);
        progress->firmware_name = g_strdup(ctx->firmware_name);
        progress->progress = -1;
        progress->status = FW_UPDATE_ERROR;
        progress->error_message = g_strdup_printf("Flash failed: error code %d", flash_result);
        g_idle_add(emit_flash_progress_idle, progress);
        usleep(500000);
	}

       // progress = g_new0(FlashProgressUpdate, 1);
        //progress->connection = ctx->connection;
        //progress->handler_id = g_strdup(ctx->handler_id);
       // progress->firmware_name = g_strdup(ctx->firmware_name);
       // progress->progress = -1;
       // progress->status = FW_UPDATE_ERROR;
        //progress->error_message = g_strdup_printf("Flash failed: error code %d", flash_result);
       // g_idle_add(emit_flash_progress_idle, progress);
    }
    
    // CLEANUP
    SWLOG_INFO("[FLASH_WORKER] Scheduling cleanup\n");
    g_idle_add(cleanup_flash_state_idle, NULL);
    
    if (ctx->handler_id) g_free(ctx->handler_id);
    if (ctx->firmware_name) g_free(ctx->firmware_name);
    if (ctx->firmware_type) g_free(ctx->firmware_type);
    if (ctx->firmware_fullpath) g_free(ctx->firmware_fullpath);
    if (ctx->server_url) g_free(ctx->server_url);
    g_free(ctx);
    
    SWLOG_INFO("[FLASH_WORKER] Thread exiting, result: %d\n", flash_result);
    return NULL;
}

// ============================================================================
// GLOBAL IN-MEMORY XCONF CACHE MANAGEMENT FUNCTIONS
// ============================================================================

/**
 * @brief Clear the global XConf data cache
 * 
 * Frees all dynamically allocated strings in the global cache and marks
 * it as invalid. Should be called before overwriting with new data or
 * when cache needs to be invalidated.
 * 
 * Thread Safety: Caller MUST hold g_xconf_data_cache lock
 * 
 * Note: This is an internal function, always called with lock held
 */
static void clear_cached_xconf_data_internal(void)
{
    
    // Zero out the entire structure
    memset(&g_cached_xconf_data, 0, sizeof(XCONFRES));
    
    // Mark cache as invalid
    g_xconf_data_valid = FALSE;
    g_cached_http_code = 0;
    
    SWLOG_DEBUG("[CACHE_MEM] Global XConf cache cleared\n");
}

/**
 * @brief Save parsed XConf data to global in-memory cache
 * 
 * Populates the global g_cached_xconf_data structure with parsed XConf
 * response data. This allows fast access without file I/O or JSON parsing.
 * 
 * Thread Safety: Thread-safe, uses g_xconf_data_cache mutex
 * 
 * @param pResponse Parsed XConf response structure to cache
 * @param http_code HTTP status code from XConf query
 * @return TRUE on success, FALSE on error
 */
static gboolean save_cached_xconf_data(const XCONFRES *pResponse, int http_code)
{
    if (!pResponse) {
        SWLOG_ERROR("[CACHE_MEM] Cannot save NULL XConf response to memory cache\n");
        return FALSE;
    }
    
    SWLOG_INFO("[CACHE_MEM] Saving parsed XConf data to global in-memory cache\n");
    
    // === CRITICAL SECTION START ===
    G_LOCK(xconf_data_cache);
    
    // Clear existing cache before overwriting
    clear_cached_xconf_data_internal();
    
    // Deep copy all fields from pResponse to g_cached_xconf_data
    // Note: XCONFRES uses fixed-size char arrays, not pointers
    
    if (pResponse->cloudFWVersion[0]) {
        strncpy(g_cached_xconf_data.cloudFWVersion, pResponse->cloudFWVersion, 
                sizeof(g_cached_xconf_data.cloudFWVersion) - 1);
        g_cached_xconf_data.cloudFWVersion[sizeof(g_cached_xconf_data.cloudFWVersion) - 1] = '\0';
    }
    
    if (pResponse->cloudFWFile[0]) {
        strncpy(g_cached_xconf_data.cloudFWFile, pResponse->cloudFWFile, 
                sizeof(g_cached_xconf_data.cloudFWFile) - 1);
        g_cached_xconf_data.cloudFWFile[sizeof(g_cached_xconf_data.cloudFWFile) - 1] = '\0';
    }
    
    if (pResponse->cloudFWLocation[0]) {
        strncpy(g_cached_xconf_data.cloudFWLocation, pResponse->cloudFWLocation, 
                sizeof(g_cached_xconf_data.cloudFWLocation) - 1);
        g_cached_xconf_data.cloudFWLocation[sizeof(g_cached_xconf_data.cloudFWLocation) - 1] = '\0';
    }
    
    if (pResponse->ipv6cloudFWLocation[0]) {
        strncpy(g_cached_xconf_data.ipv6cloudFWLocation, pResponse->ipv6cloudFWLocation, 
                sizeof(g_cached_xconf_data.ipv6cloudFWLocation) - 1);
        g_cached_xconf_data.ipv6cloudFWLocation[sizeof(g_cached_xconf_data.ipv6cloudFWLocation) - 1] = '\0';
    }
    
    if (pResponse->cloudProto[0]) {
        strncpy(g_cached_xconf_data.cloudProto, pResponse->cloudProto, 
                sizeof(g_cached_xconf_data.cloudProto) - 1);
        g_cached_xconf_data.cloudProto[sizeof(g_cached_xconf_data.cloudProto) - 1] = '\0';
    }
    
    if (pResponse->cloudImmediateRebootFlag[0]) {
        strncpy(g_cached_xconf_data.cloudImmediateRebootFlag, pResponse->cloudImmediateRebootFlag, 
                sizeof(g_cached_xconf_data.cloudImmediateRebootFlag) - 1);
        g_cached_xconf_data.cloudImmediateRebootFlag[sizeof(g_cached_xconf_data.cloudImmediateRebootFlag) - 1] = '\0';
    }
    
    if (pResponse->cloudDelayDownload[0]) {
        strncpy(g_cached_xconf_data.cloudDelayDownload, pResponse->cloudDelayDownload, 
                sizeof(g_cached_xconf_data.cloudDelayDownload) - 1);
        g_cached_xconf_data.cloudDelayDownload[sizeof(g_cached_xconf_data.cloudDelayDownload) - 1] = '\0';
    }
    
    if (pResponse->cloudPDRIVersion[0]) {
        strncpy(g_cached_xconf_data.cloudPDRIVersion, pResponse->cloudPDRIVersion, 
                sizeof(g_cached_xconf_data.cloudPDRIVersion) - 1);
        g_cached_xconf_data.cloudPDRIVersion[sizeof(g_cached_xconf_data.cloudPDRIVersion) - 1] = '\0';
    }
    
    if (pResponse->peripheralFirmwares[0]) {
        strncpy(g_cached_xconf_data.peripheralFirmwares, pResponse->peripheralFirmwares, 
                sizeof(g_cached_xconf_data.peripheralFirmwares) - 1);
        g_cached_xconf_data.peripheralFirmwares[sizeof(g_cached_xconf_data.peripheralFirmwares) - 1] = '\0';
    }
    
    if (pResponse->dlCertBundle[0]) {
        strncpy(g_cached_xconf_data.dlCertBundle, pResponse->dlCertBundle, 
                sizeof(g_cached_xconf_data.dlCertBundle) - 1);
        g_cached_xconf_data.dlCertBundle[sizeof(g_cached_xconf_data.dlCertBundle) - 1] = '\0';
    }
    
    // Save HTTP code
    g_cached_http_code = http_code;
    
    // Mark cache as valid
    g_xconf_data_valid = TRUE;
    
    G_UNLOCK(xconf_data_cache);
    // === CRITICAL SECTION END ===
    
    SWLOG_INFO("[CACHE_MEM] Global in-memory cache saved successfully\n");
    SWLOG_INFO("[CACHE_MEM]   - Version: '%s'\n", g_cached_xconf_data.cloudFWVersion);
    SWLOG_INFO("[CACHE_MEM]   - File: '%s'\n", g_cached_xconf_data.cloudFWFile);
    SWLOG_INFO("[CACHE_MEM]   - Location: '%s'\n", g_cached_xconf_data.cloudFWLocation);
    SWLOG_INFO("[CACHE_MEM]   - HTTP Code: %d\n", g_cached_http_code);
    
    return TRUE;
}

/**
 * @brief Get parsed XConf data from global in-memory cache
 * 
 * Returns a deep copy of the cached XConf response data. This is the
 * primary access method for other functions to retrieve firmware metadata.
 * 
 * Use Case: DownloadFirmware can call this to get cloudFWLocation without
 *           file I/O or JSON parsing overhead.
 * 
 * Thread Safety: Thread-safe, uses g_xconf_data_cache mutex
 * 
 * @param[out] pResponse Output structure to populate with cached data
 * @param[out] pHttpCode Output HTTP status code (can be NULL if not needed)
 * @return TRUE if cache is valid and data copied, FALSE if cache invalid/empty
 */
gboolean get_cached_xconf_data(XCONFRES *pResponse, int *pHttpCode)
{
    if (!pResponse) {
        SWLOG_ERROR("[CACHE_MEM] Cannot copy to NULL pResponse\n");
        return FALSE;
    }
    
    gboolean result = FALSE;
    
    // === CRITICAL SECTION START ===
    G_LOCK(xconf_data_cache);
    
    if (!g_xconf_data_valid) {
        SWLOG_DEBUG("[CACHE_MEM] Global cache is invalid or empty\n");
        G_UNLOCK(xconf_data_cache);
        return FALSE;
    }
    
    // Deep copy cached data to output structure
    memcpy(pResponse, &g_cached_xconf_data, sizeof(XCONFRES));
    
    // Copy HTTP code if requested
    if (pHttpCode) {
        *pHttpCode = g_cached_http_code;
    }
    
    result = TRUE;
    
    G_UNLOCK(xconf_data_cache);
    // === CRITICAL SECTION END ===
    
    SWLOG_DEBUG("[CACHE_MEM] Retrieved XConf data from global cache\n");
    SWLOG_DEBUG("[CACHE_MEM]   - Version: '%s'\n", pResponse->cloudFWVersion);
    SWLOG_DEBUG("[CACHE_MEM]   - Location: '%s'\n", pResponse->cloudFWLocation);
    
    return result;
}

/**
 * @brief Clear the global XConf data cache (public interface)
 * 
 * Thread-safe public wrapper for clearing the global cache.
 * Use this when cache needs to be invalidated (e.g., on error or manual refresh).
 */
void clear_cached_xconf_data(void)
{
    G_LOCK(xconf_data_cache);
    clear_cached_xconf_data_internal();
    G_UNLOCK(xconf_data_cache);
    
    SWLOG_INFO("[CACHE_MEM] Global XConf cache cleared by request\n");
}
