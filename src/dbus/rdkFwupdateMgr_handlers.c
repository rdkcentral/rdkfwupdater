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
#include <string.h>
#include <gio/gio.h>

// Buffer sizes for JSON and URL construction
#define JSON_STR_LEN        1000
#define URL_MAX_LEN         512

// XConf response cache files (reduces server queries)
#define XCONF_CACHE_FILE        "/tmp/xconf_response_thunder.txt"
#define XCONF_HTTP_CODE_FILE    "/tmp/xconf_httpcode_thunder.txt"
#define XCONF_PROGRESS_FILE     "/tmp/xconf_curl_progress_thunder"
#define RED_STATE_FILE          "/lib/rdk/stateRedRecovery.sh"

// *** NEW: Progress monitoring constants ***
#define CURL_PROGRESS_FILE      "/opt/curl_progress"
#define PROGRESS_POLL_INTERVAL_MS    100   // Poll every 100ms for responsive updates
#define PROGRESS_THROTTLE_PERCENT    1.0   // Emit signal only if progress changed by ≥1%
#define PROGRESS_MONITOR_TIMEOUT_SEC 600   // 10 minutes max without progress

// *** NEW: Forward declarations ***
typedef struct _ProgressData ProgressData;
static gboolean emit_download_progress_idle(gpointer user_data);

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
    gboolean* stop_flag;                // Atomic flag to signal thread shutdown
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
extern ImageDetails_t cur_img_detail;
extern Rfc_t rfc_list;

// Trigger type constant for manual/D-Bus initiated downloads
#define TRIGGER_MANUAL 1

/**
 * @brief Check if XConf response cache exists
 * @return TRUE if cache file exists, FALSE otherwise
 * 
 * Used to avoid unnecessary XConf queries when cached data is available.
 */
gboolean xconf_cache_exists(void) {
    return g_file_test(XCONF_CACHE_FILE, G_FILE_TEST_EXISTS);
}

/**
 * @brief Load XConf response from cache file
 * @param[out] pResponse Structure to populate with cached data
 * @return TRUE on success, FALSE if cache read/parse fails
 * 
 * Reads cached XConf JSON response and parses it into XCONFRES structure.
 * Cache miss or parse failure returns FALSE - caller should fetch from XConf server.
 */
static gboolean load_xconf_from_cache(XCONFRES *pResponse) {
    gchar *cache_content = NULL;
    gsize length;
    GError *error = NULL;
    gboolean result = FALSE;
    
    SWLOG_INFO("[CACHE] Loading XConf data from cache: %s\n", XCONF_CACHE_FILE);
    
    if (!g_file_get_contents(XCONF_CACHE_FILE, &cache_content, &length, &error)) {
        SWLOG_ERROR("[CACHE] Failed to read cache file: %s\n", error ? error->message : "Unknown error");
        if (error) g_error_free(error);
        return FALSE;
    }
    
    SWLOG_INFO("[CACHE] Loaded %zu bytes from cache\n", length);
    
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

static gboolean save_xconf_to_cache(const char *xconf_response, int http_code) {
    GError *error = NULL;
    
    SWLOG_INFO("[CACHE] Saving XConf response to cache files\n");
    
    // Save main XConf response
    if (!g_file_set_contents(XCONF_CACHE_FILE, xconf_response, -1, &error)) {
        SWLOG_ERROR("[CACHE] Failed to save XConf response: %s\n", error ? error->message : "Unknown error");
        if (error) g_error_free(error);
        return FALSE;
    }
    
    // Save HTTP code  
    gchar *http_code_str = g_strdup_printf("%d", http_code);
    if (!g_file_set_contents(XCONF_HTTP_CODE_FILE, http_code_str, -1, &error)) {
        SWLOG_ERROR("[CACHE] Failed to save HTTP code: %s\n", error ? error->message : "Unknown error");
        if (error) g_error_free(error);
        g_free(http_code_str);
        return FALSE;
    }
    
    SWLOG_INFO("[CACHE] XConf data cached successfully\n");
    SWLOG_INFO("[CACHE]   - Response file: %s\n", XCONF_CACHE_FILE);
    SWLOG_INFO("[CACHE]   - HTTP code file: %s (code: %d)\n", XCONF_HTTP_CODE_FILE, http_code);
    
    g_free(http_code_str);
    return TRUE;
}

static int fetch_xconf_firmware_info( XCONFRES *pResponse, int server_type, int *pHttp_code )
{
    DownloadData DwnLoc;
    char *pJSONStr = NULL;      // contains the device data string to send to XCONF server
    char *pServURL = NULL;      // the server to do the XCONF comms
    size_t len = 0;
    int ret = -1;
    void *curl = NULL;          // CURL handle

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

                    SWLOG_INFO("Simulating a 120 seconds sleep()\n");
                    sleep(120);
                    SWLOG_INFO("Just now completed 120 seconds sleep\n");
                    SWLOG_INFO("fetch_xconf_firmware_info: Initiating XConf request with server_type=%d\n", server_type);
                    SWLOG_INFO("fetch_xconf_firmware_info: Context setup - device_info=%p, rfc_list=%p\n", 
                               xconf_context.device_info, xconf_context.rfc_list);
                    
                    // Call rdkv_upgrade_request with error handling
                    SWLOG_INFO("fetch_xconf_firmware_info: Calling rdkv_upgrade_request...\n");
                    ret = rdkv_upgrade_request(&xconf_context, &curl, pHttp_code);
                    SWLOG_INFO("fetch_xconf_firmware_info: rdkv_upgrade_request returned (ret=%d)\n", ret);
                    
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
        g_free(response->available_version);
        g_free(response->update_details);
        g_free(response->status_message);
        response->available_version = NULL;
        response->update_details = NULL;
        response->status_message = NULL;
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
static CheckUpdateResponse create_success_response(const gchar *available_version,
                                                   const gchar *update_details,
                                                   const gchar *status_message) {
    CheckUpdateResponse response = {0};
    char current_img_buffer[64] = {0};
    
    bool img_status = GetFirmwareVersion(current_img_buffer, sizeof(current_img_buffer));
    
    SWLOG_INFO("[rdkFwupdateMgr] create_success_response: Getting current image info\n");
    SWLOG_INFO("[rdkFwupdateMgr]   - currentImg status: %s\n", img_status ? "SUCCESS" : "FAILED");
    SWLOG_INFO("[rdkFwupdateMgr]   - current_img_buffer: '%s'\n", current_img_buffer);

    gboolean is_update_available = img_status && available_version && (g_strcmp0(current_img_buffer, available_version) != 0);
    if (is_update_available) {
    response.result_code = UPDATE_AVAILABLE;
    response.current_img_version = g_strdup(current_img_buffer);
    response.available_version = g_strdup(available_version);
    response.update_details = g_strdup(update_details ? update_details : "");
    response.status_message = g_strdup(status_message ? status_message : "Firmware update available");
    
    SWLOG_INFO("[rdkFwupdateMgr] create_success_response: Response created with current image: '%s', available: '%s', status: '%s'\n", 
               response.current_img_version, response.available_version, response.status_message);
    }else {
        response.result_code = UPDATE_NOT_AVAILABLE;
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
 * @param result_code Result code (UPDATE_NOT_AVAILABLE, UPDATE_ERROR, etc.)
 * @param status_message Optional custom status message (can be NULL for default)
 * @return CheckUpdateResponse structure with allocated strings (must be freed by caller)
 */
static CheckUpdateResponse create_result_response(CheckForUpdateResult result_code,
                                                  const gchar *status_message) {
    CheckUpdateResponse response = {0};
    char current_img_buffer[256] = {0};
    
    bool img_status = GetFirmwareVersion(current_img_buffer, sizeof(current_img_buffer));
    
    SWLOG_INFO("[rdkFwupdateMgr] create_result_response: Getting current image info\n");
    SWLOG_INFO("[rdkFwupdateMgr]   - currentImg status: %s\n", img_status ? "SUCCESS" : "FAILED");
    SWLOG_INFO("[rdkFwupdateMgr]   - current_img_buffer: '%s'\n", current_img_buffer);
    
    response.result_code = result_code;
    response.current_img_version = g_strdup(img_status ? current_img_buffer : "Unknown");
    response.available_version = g_strdup("");
    response.update_details = g_strdup("");
    
    const gchar *default_status = "";
    if (status_message) {
        default_status = status_message;
    } else {
        switch(result_code) {
            case UPDATE_AVAILABLE:
                default_status = "Update available";
                break;
            case UPDATE_NOT_AVAILABLE:
                default_status = "No update available";
                break;
            case UPDATE_ERROR:
                default_status = "Error checking for updates";
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
static gboolean emit_download_progress_idle(gpointer user_data) {
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
            progress_file = NULL;
            g_usleep(PROGRESS_POLL_INTERVAL_MS * 1000);
            continue;
        }
        
        // Close file immediately after reading to avoid holding it open
        fclose(progress_file);
        progress_file = NULL;
        
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
    // Cleanup: Ensure file is closed if still open
    if (progress_file != NULL) {
        fclose(progress_file);
        progress_file = NULL;
    }
    
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
        return create_result_response(UPDATE_ERROR, "Internal error - invalid handler ID");
    }
    
    SWLOG_INFO("[rdkFwupdateMgr] CheckForUpdate: handler=%s\n", handler_id);
    SWLOG_INFO("[rdkFwupdateMgr] About to allocate XCONFRES structure on stack (~2KB)...\n");
    
    XCONFRES response = {0};
    
    SWLOG_INFO("[rdkFwupdateMgr] XCONFRES structure allocated successfully\n");
    int http_code = 0;
    int server_type = HTTP_XCONF_DIRECT;
    int ret = -1;
    
    SWLOG_INFO("[rdkFwupdateMgr] CheckForUpdate: Checking for cached XConf data...\n");
    
    // Try cache first to support offline recovery scenarios
    if (xconf_cache_exists()) {
        SWLOG_INFO("[rdkFwupdateMgr] Cache hit! Loading XConf data from cache\n");
        if (load_xconf_from_cache(&response)) {
            ret = 0;
            http_code = 200;
            SWLOG_INFO("[rdkFwupdateMgr] Successfully loaded XConf data from cache\n");
        } else {
            SWLOG_ERROR("[rdkFwupdateMgr] Cache read failed, falling back to live XConf call\n");
            ret = fetch_xconf_firmware_info(&response, server_type, &http_code);
        }
    } else {
        SWLOG_INFO("[rdkFwupdateMgr] Cache miss! Making live XConf call\n");
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
                return create_result_response(UPDATE_NOT_AVAILABLE, "Firmware validation failed - not for this device model");
            }
            SWLOG_INFO("[rdkFwupdateMgr] VALIDATION PASSED - Firmware is valid for this device\n");
            SWLOG_INFO("[rdkFwupdateMgr] ===== VALIDATION & COMPARISON COMPLETE =====\n");
        }
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
        
        // Determine result based on presence of firmware version
        if (response.cloudFWVersion[0] && strlen(response.cloudFWVersion) > 0) {
            SWLOG_INFO("[rdkFwupdateMgr] XConf returned firmware version: '%s'\n", response.cloudFWVersion);
            
            CheckUpdateResponse result = create_success_response(
                response.cloudFWVersion,
                update_details,
                "Firmware update available"
            );
            
            g_free(update_details);
            return result;
        } else {
            SWLOG_INFO("[rdkFwupdateMgr] XConf returned no firmware version - no update available\n");
            g_free(update_details);
            return create_result_response(UPDATE_NOT_AVAILABLE, "No firmware update available");
        }
    } else {
        // XConf query failed - network or server error
        SWLOG_ERROR("[rdkFwupdateMgr] XConf communication failed: ret=%d, http=%d\n", ret, http_code);
        
        if (http_code != 200) {
            return create_result_response(UPDATE_ERROR, "Network error - unable to reach update server");
        } else {
            return create_result_response(UPDATE_ERROR, "Update check failed - server communication error");
        }
    }
}

/**
 * @brief Download firmware with progress monitoring
 * 
 * Main entry point for firmware download operation. Features:
 * - URL source: Custom URL or XConf cache
 * - Progress monitoring: Spawns thread if download_state provided
 * - Error handling: Comprehensive curl/HTTP error mapping
 * - Memory safety: All allocations checked and cleaned up
 * 
 * Thread Safety:
 * - Spawns progress monitor thread if needed
 * - Properly joins thread before returning
 * - All shared state protected by mutex
 * 
 * Memory Management:
 * - All g_strdup'd strings must be freed by caller
 * - Thread context freed by thread itself
 * - Mutex and context freed by thread on exit
 * 
 * @param firmwareName Firmware filename (for logging, can be NULL)
 * @param downloadUrl Custom URL or empty string to use XConf URL
 * @param typeOfFirmware Type: "PCI", "PDRI", "PERIPHERAL" (can be NULL)
 * @param localFilePath Destination path (required, must not be NULL)
 * @param download_state D-Bus skeleton for progress signals (NULL = no progress)
 * @return DownloadFirmwareResult with result_code and error_message
 */
DownloadFirmwareResult rdkFwupdateMgr_downloadFirmware(const gchar *firmwareName,
                                                       const gchar *downloadUrl,
                                                       const gchar *typeOfFirmware,
                                                       const gchar *localFilePath,
                                                       void *download_state) {
    SWLOG_INFO("[DOWNLOAD_HANDLER] === Starting Firmware Download ===\n");
    SWLOG_INFO("[DOWNLOAD_HANDLER]   Firmware: %s\n", firmwareName ? firmwareName : "(null)");
    SWLOG_INFO("[DOWNLOAD_HANDLER]   Custom URL: '%s'\n", downloadUrl ? downloadUrl : "(empty)");
    SWLOG_INFO("[DOWNLOAD_HANDLER]   Type: %s\n", typeOfFirmware ? typeOfFirmware : "(null)");
    SWLOG_INFO("[DOWNLOAD_HANDLER]   Destination: %s\n", localFilePath ? localFilePath : "(null)");
    SWLOG_INFO("[DOWNLOAD_HANDLER]   Progress monitoring: %s\n", download_state ? "ENABLED" : "DISABLED");
    
    // Initialize result structure
    DownloadFirmwareResult result;
    result.result_code = DOWNLOAD_ERROR;
    result.error_message = NULL;
    
    // Validate required parameters
    if (localFilePath == NULL || strlen(localFilePath) == 0) {
        SWLOG_ERROR("[DOWNLOAD_HANDLER] ERROR: localFilePath is NULL or empty\n");
        result.error_message = g_strdup("Invalid parameters: localFilePath required");
        return result;
    }
    
    // Determine effective download URL
    gchar *effective_url = NULL;
    
    if (downloadUrl != NULL && strlen(downloadUrl) > 0) {
        // Use custom URL provided by caller
        SWLOG_INFO("[DOWNLOAD_HANDLER] Using custom URL: %s\n", downloadUrl);
        effective_url = g_strdup(downloadUrl);
        
        if (effective_url == NULL) {
            SWLOG_ERROR("[DOWNLOAD_HANDLER] ERROR: Failed to duplicate URL string\n");
            result.error_message = g_strdup("Memory allocation failed");
            return result;
        }
    } else {
        // Load URL from XConf cache
        SWLOG_INFO("[DOWNLOAD_HANDLER] No custom URL, loading from XConf cache\n");
        
        if (!xconf_cache_exists()) {
            SWLOG_ERROR("[DOWNLOAD_HANDLER] ERROR: No XConf cache found\n");
            SWLOG_ERROR("[DOWNLOAD_HANDLER] Client must call CheckForUpdate first\n");
            result.error_message = g_strdup("No firmware metadata. Call CheckForUpdate first.");
            return result;
        }
        
        XCONFRES xconf_response;
        memset(&xconf_response, 0, sizeof(XCONFRES));
        
        if (!load_xconf_from_cache(&xconf_response)) {
            SWLOG_ERROR("[DOWNLOAD_HANDLER] ERROR: Failed to load XConf cache\n");
            result.error_message = g_strdup("Failed to load firmware metadata from cache");
            return result;
        }
        
        SWLOG_INFO("[DOWNLOAD_HANDLER] Loaded XConf metadata:\n");
        SWLOG_INFO("[DOWNLOAD_HANDLER]   Version: %s\n", xconf_response.cloudFWVersion);
        SWLOG_INFO("[DOWNLOAD_HANDLER]   URL: %s\n", xconf_response.cloudFWFile);
        
        effective_url = g_strdup(xconf_response.cloudFWFile);
        
        if (effective_url == NULL) {
            SWLOG_ERROR("[DOWNLOAD_HANDLER] ERROR: Failed to duplicate XConf URL\n");
            result.error_message = g_strdup("Memory allocation failed");
            return result;
        }
    }
    
    // Validate effective URL
    if (strlen(effective_url) == 0) {
        SWLOG_ERROR("[DOWNLOAD_HANDLER] ERROR: No download URL available\n");
        result.error_message = g_strdup("No download URL available");
        g_free(effective_url);
        return result;
    }
    
    SWLOG_INFO("[DOWNLOAD_HANDLER] Effective download URL: %s\n", effective_url);
    
    // Prepare upgrade context
    RdkUpgradeContext_t upgrade_context;
    memset(&upgrade_context, 0, sizeof(RdkUpgradeContext_t));
    
    // Determine upgrade type from firmware type parameter
    if (typeOfFirmware != NULL) {
        if (strcmp(typeOfFirmware, "PCI") == 0) {
            upgrade_context.upgrade_type = PCI_UPGRADE;
        } else if (strcmp(typeOfFirmware, "PDRI") == 0) {
            upgrade_context.upgrade_type = PDRI_UPGRADE;
        } else if (strcmp(typeOfFirmware, "PERIPHERAL") == 0) {
            upgrade_context.upgrade_type = PERIPHERAL_UPGRADE;
        } else {
            SWLOG_ERROR("[DOWNLOAD_HANDLER] Unknown firmware type '%s', using PCI\n", typeOfFirmware);
            upgrade_context.upgrade_type = PCI_UPGRADE;
        }
    } else {
        upgrade_context.upgrade_type = PCI_UPGRADE;
    }
    
    SWLOG_INFO("[DOWNLOAD_HANDLER] Upgrade type: %d\n", upgrade_context.upgrade_type);
    
    // CRITICAL: Set download_only flag (do NOT flash automatically)
    upgrade_context.download_only = TRUE;
    SWLOG_INFO("[DOWNLOAD_HANDLER] download_only=TRUE (will NOT auto-flash)\n");
    
    // Set context fields
    upgrade_context.server_type = HTTP_SSR_DIRECT;
    upgrade_context.artifactLocationUrl = effective_url;
    upgrade_context.dwlloc = (const void*)localFilePath;
    upgrade_context.pPostFields = NULL;
    upgrade_context.immed_reboot_flag = "NO";
    upgrade_context.delay_dwnl = 0;
    
    // Generate timestamp for lastrun
    char timestamp[64];
    snprintf(timestamp, sizeof(timestamp), "%ld", (long)time(NULL));
    upgrade_context.lastrun = timestamp;
    upgrade_context.disableStatsUpdate = (char*)"false";
    upgrade_context.device_info = &device_info;
    
    int force_exit = 0;
    upgrade_context.force_exit = &force_exit;
    upgrade_context.trigger_type = TRIGGER_MANUAL;
    upgrade_context.rfc_list = &rfc_list;
    
    
    // *** NEW: Spawn progress monitor thread if download_state provided ***
    GThread* monitor_thread = NULL;
    gboolean stop_monitor = FALSE;
    GMutex* monitor_mutex = NULL;
    ProgressMonitorContext* monitor_ctx = NULL;
    
    if (download_state != NULL) {
        SWLOG_INFO("[DOWNLOAD_HANDLER] Setting up progress monitoring...\n");
        
        // Cast download_state to the proper type
        DownloadStateContext* dl_ctx = (DownloadStateContext*)download_state;
        
        // NULL CHECK: Validate download state context fields
        if (dl_ctx->connection == NULL) {
            SWLOG_ERROR("[DOWNLOAD_HANDLER] ERROR: NULL D-Bus connection in download_state\n");
            result.result_code = DOWNLOAD_ERROR;
            result.error_message = g_strdup("Invalid download state (NULL connection)");
            g_free(effective_url);
            return result;
        }
        
        // Allocate and initialize mutex for thread-safe access
        monitor_mutex = g_new0(GMutex, 1);
        if (monitor_mutex == NULL) {
            SWLOG_ERROR("[DOWNLOAD_HANDLER] ERROR: Failed to allocate monitor mutex\n");
            result.result_code = DOWNLOAD_ERROR;
            result.error_message = g_strdup("Memory allocation failed");
            g_free(effective_url);
            return result;
        }
        g_mutex_init(monitor_mutex);
        SWLOG_DEBUG("[DOWNLOAD_HANDLER] Monitor mutex allocated and initialized\n");
        
        // Allocate progress monitor context
        monitor_ctx = g_new0(ProgressMonitorContext, 1);
        if (monitor_ctx == NULL) {
            SWLOG_ERROR("[DOWNLOAD_HANDLER] ERROR: Failed to allocate monitor context\n");
            g_mutex_clear(monitor_mutex);
            g_free(monitor_mutex);
            result.result_code = DOWNLOAD_ERROR;
            result.error_message = g_strdup("Memory allocation failed");
            g_free(effective_url);
            return result;
        }
        SWLOG_DEBUG("[DOWNLOAD_HANDLER] Monitor context allocated\n");
        
        // Initialize context fields
        monitor_ctx->connection = dl_ctx->connection;  // Borrowed pointer (do NOT free)
        monitor_ctx->handler_id = dl_ctx->handler_id ? g_strdup(dl_ctx->handler_id) : NULL;
        monitor_ctx->firmware_name = dl_ctx->firmware_name ? g_strdup(dl_ctx->firmware_name) : NULL;
        monitor_ctx->stop_flag = &stop_monitor;
        monitor_ctx->mutex = monitor_mutex;
        monitor_ctx->last_dlnow = 0;
        monitor_ctx->last_activity_time = time(NULL);
        
        SWLOG_DEBUG("[DOWNLOAD_HANDLER] Monitor context initialized:\n");
        SWLOG_DEBUG("[DOWNLOAD_HANDLER]   - Handler ID: %s\n", monitor_ctx->handler_id ? monitor_ctx->handler_id : "(null)");
        SWLOG_DEBUG("[DOWNLOAD_HANDLER]   - Firmware: %s\n", monitor_ctx->firmware_name ? monitor_ctx->firmware_name : "(null)");
        
        // Spawn monitor thread
        GError* thread_error = NULL;
        monitor_thread = g_thread_try_new("rdkfw_progress_monitor", 
                                          rdkfw_progress_monitor_thread, 
                                          monitor_ctx, 
                                          &thread_error);
        
        if (monitor_thread == NULL) {
            SWLOG_ERROR("[DOWNLOAD_HANDLER] ERROR: Failed to spawn monitor thread: %s\n",
                       thread_error ? thread_error->message : "Unknown error");
            
            // Cleanup on thread creation failure
            if (thread_error != NULL) {
                g_error_free(thread_error);
                thread_error = NULL;
            }
            
            // Free string fields (g_strdup'd copies)
            if (monitor_ctx->handler_id) {
                g_free(monitor_ctx->handler_id);
                monitor_ctx->handler_id = NULL;
            }
            if (monitor_ctx->firmware_name) {
                g_free(monitor_ctx->firmware_name);
                monitor_ctx->firmware_name = NULL;
            }
            
            // Clear and free mutex
            g_mutex_clear(monitor_mutex);
            g_free(monitor_mutex);
            monitor_mutex = NULL;
            
            // Free context
            g_free(monitor_ctx);
            monitor_ctx = NULL;
            
            // Continue without progress monitoring (non-fatal)
            SWLOG_INFO("[DOWNLOAD_HANDLER] Continuing without progress monitoring\n");
        } else {
            SWLOG_INFO("[DOWNLOAD_HANDLER] ✓ Progress monitor thread started successfully\n");
        }
    } else {
        SWLOG_INFO("[DOWNLOAD_HANDLER] No progress monitoring requested (download_state=NULL)\n");
    }
    
    // Call rdkv_upgrade_request() (blocks until download completes or fails)
    SWLOG_INFO("[DOWNLOAD_HANDLER] Calling rdkv_upgrade_request()...\n");
    
    void *curl_handle = NULL;
    int http_code = 0;
    int curl_ret_code = rdkv_upgrade_request(&upgrade_context, &curl_handle, &http_code);
    
    SWLOG_INFO("[DOWNLOAD_HANDLER] rdkv_upgrade_request() returned: curl=%d, http=%d\n", 
               curl_ret_code, http_code);
    
    // *** NEW: Stop progress monitor thread ***
    if (monitor_thread != NULL) {
        SWLOG_INFO("[DOWNLOAD_HANDLER] Stopping progress monitor thread...\n");
        
        // Signal thread to stop atomically
        g_atomic_int_set(&stop_monitor, TRUE);
        
        // Wait for thread to exit (thread will cleanup its own resources)
        g_thread_join(monitor_thread);
        monitor_thread = NULL;
        
        SWLOG_INFO("[DOWNLOAD_HANDLER] Progress monitor thread stopped cleanly\n");
        
        // Note: monitor_mutex and monitor_ctx are cleaned up by the thread itself
        // Do NOT free them here to avoid double-free
    }
    
    // Analyze download result
    if (curl_ret_code == 0 && (http_code == 200 || http_code == 206)) {
        // Success: curl completed and HTTP OK/Partial Content
        SWLOG_INFO("[DOWNLOAD_HANDLER] Download completed successfully!\n");
        
        // Verify file exists on disk
        if (!g_file_test(localFilePath, G_FILE_TEST_EXISTS)) {
            SWLOG_ERROR("[DOWNLOAD_HANDLER] ERROR: File not found after download: %s\n", localFilePath);
            result.result_code = DOWNLOAD_ERROR;
            result.error_message = g_strdup("File not found after download");
            g_free(effective_url);
            return result;
        }
        
        // Get file size for logging
        struct stat st;
        if (stat(localFilePath, &st) == 0) {
            SWLOG_INFO("[DOWNLOAD_HANDLER] Downloaded file size: %ld bytes\n", (long)st.st_size);
        }
        
        result.result_code = DOWNLOAD_SUCCESS;
        result.error_message = NULL;
        
    } else if (curl_ret_code == 0 && http_code == 404) {
        // HTTP 404: Not Found
        SWLOG_ERROR("[DOWNLOAD_HANDLER] Firmware not found (HTTP 404)\n");
        result.result_code = DOWNLOAD_NOT_FOUND;
        result.error_message = g_strdup("Firmware not found on server (HTTP 404)");
        
    } else if (curl_ret_code == 6) {
        // CURLE_COULDNT_RESOLVE_HOST
        SWLOG_ERROR("[DOWNLOAD_HANDLER] DNS resolution failed (curl error 6)\n");
        result.result_code = DOWNLOAD_NETWORK_ERROR;
        result.error_message = g_strdup("DNS resolution failed");
        
    } else if (curl_ret_code == 7) {
        // CURLE_COULDNT_CONNECT
        SWLOG_ERROR("[DOWNLOAD_HANDLER] Connection failed (curl error 7)\n");
        result.result_code = DOWNLOAD_NETWORK_ERROR;
        result.error_message = g_strdup("Connection failed");
        
    } else if (curl_ret_code == 28) {
        // CURLE_OPERATION_TIMEDOUT
        SWLOG_ERROR("[DOWNLOAD_HANDLER] Timeout (curl error 28)\n");
        result.result_code = DOWNLOAD_NETWORK_ERROR;
        result.error_message = g_strdup("Operation timed out");
        
    } else if (curl_ret_code == 18) {
        // CURLE_PARTIAL_FILE
        SWLOG_ERROR("[DOWNLOAD_HANDLER] Partial file transfer (curl error 18)\n");
        result.result_code = DOWNLOAD_ERROR;
        result.error_message = g_strdup("Partial file transfer (incomplete download)");
        
    } else if (curl_ret_code == 23) {
        // CURLE_WRITE_ERROR
        SWLOG_ERROR("[DOWNLOAD_HANDLER] Write error (curl error 23) - disk full?\n");
        result.result_code = DOWNLOAD_ERROR;
        result.error_message = g_strdup("Write error (disk full or permission denied)");
        
    } else {
        // Generic error
        SWLOG_ERROR("[DOWNLOAD_HANDLER] Download failed (curl=%d, HTTP=%d)\n", 
                   curl_ret_code, http_code);
        
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "Download failed (curl error %d, HTTP status %d)", 
                curl_ret_code, http_code);
        result.error_message = g_strdup(error_msg);
    }
    
    // Cleanup
    g_free(effective_url);
    effective_url = NULL;
    
    SWLOG_INFO("[DOWNLOAD_HANDLER] === Download Handler Complete (result=%d) ===\n", 
               result.result_code);
    
    return result;
}
