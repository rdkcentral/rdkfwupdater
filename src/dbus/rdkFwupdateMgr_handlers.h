/*
 * RDK Firmware Update Manager - D-Bus Handler Interface
 * 
 * This header defines the core API for firmware update operations exposed
 * via D-Bus to client applications (UI, apps, etc.)
 * 
 * Key Features:
 * - Process registration/unregistration for update eligibility
 * - Asynchronous firmware update checking with caching
 * - Non-blocking operations with D-Bus signal notifications
 */

#ifndef RDKFWUPDATEMGR_HANDLERS_H
#define RDKFWUPDATEMGR_HANDLERS_H

#include <glib.h>
#include "json_process.h"  // For XCONFRES structure

/*
 * CheckForUpdate API Call Result
 * 
 * Indicates whether the CheckForUpdate method call itself succeeded or failed.
 * This is separate from the firmware update status.
 */
typedef enum {
    CHECK_FOR_UPDATE_SUCCESS = 0,  // API call succeeded - check status_code for firmware status
    CHECK_FOR_UPDATE_FAIL = 1      // API call failed - validation error, internal error, etc.
} CheckForUpdateResult;

/*
 * CheckForUpdate Status Codes
 * 
 * These codes indicate the outcome of the firmware update check and
 * determine what action the client should take.
 */
typedef enum {
    FIRMWARE_AVAILABLE = 0,     // New firmware available - client can proceed with download
    FIRMWARE_NOT_AVAILABLE = 1, // Already on latest firmware - no action needed
    UPDATE_NOT_ALLOWED = 2,     // Firmware not compatible with this device model
    FIRMWARE_CHECK_ERROR = 3,   // Error occurred while checking for updates
    IGNORE_OPTOUT = 4,          // Firmware Download not allowed
    BYPASS_OPTOUT = 5           // Firmware Download not allowed
} CheckForUpdateStatus;

/*
 * CheckForUpdate Response Structure
 * 
 * Contains all information about firmware update availability.
 * All string fields are dynamically allocated and must be freed using
 * checkupdate_response_free() to prevent memory leaks.
 */
typedef struct {
    CheckForUpdateResult result;       // API call result (SUCCESS/FAIL)
    CheckForUpdateStatus status_code;  // Firmware update status (0-5)
    gchar *current_img_version;        // Currently running firmware version
    gchar *available_version;          // Available firmware version from XConf server
    gchar *update_details;             // Detailed update info (URL, protocol, flags, etc.)
    gchar *status_message;             // Human-readable status for logging/display
} CheckUpdateResponse;

/*
 * Check for Firmware Updates (Core API)
 * 
 * Queries XConf server for available firmware updates. Uses cache-first approach
 * for fast responses. If cache miss, spawns async XConf fetch and returns
 * UPDATE_ERROR immediately, with actual result delivered via D-Bus signal.
 * 
 * Flow:
 * 1. Validates handler_id is registered
 * 2. Checks cache (/tmp/xconf_response_thunder.txt)
 * 3. If cache hit: validates firmware and returns result immediately
 * 4. If cache miss: spawns async fetch, returns UPDATE_ERROR, emits signal when done
 * 
 * Parameters:
 *   handler_id - Process registration ID from rdkFwupdateMgr_registerProcess()
 * 
 * Returns:
 *   CheckUpdateResponse with result_code and firmware details
 *   Must be freed with checkupdate_response_free()
 */
CheckUpdateResponse rdkFwupdateMgr_checkForUpdate(const gchar *handler_id);

/*
 * Free CheckUpdateResponse Memory
 * 
 * Safely frees all dynamically allocated strings in the response structure.
 * NULL-safe - can be called on already-freed or zero-initialized structures.
 */
void checkupdate_response_free(CheckUpdateResponse *response);

/*
 * Check if XConf Cache Exists
 * 
 * Used internally by D-Bus server to determine cache-first behavior.
 * Returns TRUE if cache file exists, FALSE otherwise.
 */
gboolean xconf_cache_exists(void);

#ifdef GTEST_ENABLE
/*
 * Fetch Firmware Info from XConf Server (Exposed for Unit Testing)
 * 
 * Internal function that queries the XConf server for firmware update information.
 * Normally static, but exposed under GTEST_ENABLE for comprehensive unit testing.
 * 
 * Parameters:
 *   pResponse   - Output structure to populate with XConf response data
 *   server_type - Server type (0=direct, 1=codebig)
 *   pHttp_code  - Output HTTP response code (200=success)
 * 
 * Returns:
 *   0 on success, -1 on failure
 * 
 * Note: This is an internal function - use rdkFwupdateMgr_checkForUpdate() for production code.
 */
int fetch_xconf_firmware_info(XCONFRES *pResponse, int server_type, int *pHttp_code);
#endif

/*
 * Download Firmware Result Codes
 * 
 * These codes indicate the outcome of a firmware download request.
 */
typedef enum {
    DOWNLOAD_SUCCESS = 0,          // Download completed successfully
    DOWNLOAD_ALREADY_EXISTS = 1,   // File already downloaded
    DOWNLOAD_NETWORK_ERROR = 2,    // Network error during download
    DOWNLOAD_NOT_FOUND = 3,        // Firmware not found on server (HTTP 404)
    DOWNLOAD_ERROR = 4             // Generic error
} DownloadFirmwareResultCode;

/*
 * Download Firmware Result Structure
 * 
 * Contains the result of a firmware download operation.
 * All string fields are dynamically allocated and must be freed.
 */
typedef struct {
    DownloadFirmwareResultCode result_code;  // Result of the download
    gchar *error_message;                    // Error description if failed
} DownloadFirmwareResult;

/*
 * Download Firmware
 * 
 * Initiates firmware download from XConf-provided URL or custom URL.
 * This function performs the actual download in the calling thread.
 * 
 * Parameters:
 *   firmwareName - Firmware filename to download
 *   downloadUrl - Custom URL or empty string (use XConf URL)
 *   typeOfFirmware - Firmware type: "PCI", "PDRI", "PERIPHERAL"
 *   localFilePath - Destination file path
 *   download_state - DownloadState pointer for progress updates (can be NULL)
 * 
 * Returns:
 *   DownloadFirmwareResult with result_code and error details
 */
DownloadFirmwareResult rdkFwupdateMgr_downloadFirmware(const gchar *firmwareName,
                                                       const gchar *downloadUrl,
                                                       const gchar *typeOfFirmware,
                                                       const gchar *localFilePath,
                                                       void *download_state);

/*
 * Update Firmware (Future Implementation)
 * 
 * Applies downloaded firmware and triggers reboot if required.
 * Currently not implemented - placeholder for future functionality.
 */
int rdkFwupdateMgr_updateFirmware(const gchar *handler_id,
                                  const gchar *current_version,
                                  const gchar *target_version,
                                  gchar **update_status,
                                  gchar **status_message);

/*
 * Flash Worker Thread Function
 * 
 * Worker thread that performs firmware flash operation by calling flashImage()
 * from librdksw_flash.so. Emits UpdateProgress signals for client notification.
 * 
 * This function is called in a separate GThread spawned by the UpdateFirmware
 * D-Bus handler. It performs the blocking flash operation and emits progress
 * signals at key milestones (0%, 25%, 50%, 75%, 100% or error).
 * 
 * @param user_data AsyncFlashContext* containing flash parameters
 * @return NULL (thread exit value not used)
 * 
 * Thread Safety: Uses g_idle_add() for all D-Bus signal emissions
 * Memory: Frees AsyncFlashContext before exit
 */
gpointer rdkfw_flash_worker_thread(gpointer user_data);

/*
 * Subscribe to Events (Future Implementation)
 * 
 * Registers callback endpoint for firmware update events.
 * Currently not implemented - placeholder for future functionality.
 */
int rdkFwupdateMgr_subscribeToEvents(const gchar *handler_id,
                                     const gchar *callback_endpoint);

/*
 * Register Process for Firmware Updates
 * 
 * Registers a client application/process with the firmware update manager.
 * Only registered processes can check for updates or receive update notifications.
 * 
 * Registration Logic:
 * - Each process_name can only be registered once (prevents duplicate registrations)
 * - Returns unique handler_id used for all subsequent operations
 * - Tracks D-Bus sender_id for automatic cleanup on client disconnect
 * 
 * Parameters:
 *   process_name - Unique identifier for the process (e.g., "VideoApp", "SettingsUI")
 *   lib_version  - Version of the client library (for compatibility tracking)
 *   sender_id    - D-Bus unique connection name (e.g., ":1.42") for lifecycle tracking
 * 
 * Returns:
 *   handler_id (64-bit) on success, 0 on failure (duplicate registration or error)
 */
guint64 rdkFwupdateMgr_registerProcess(const gchar *process_name,
                                       const gchar *lib_version,
                                       const gchar *sender_id);

/*
 * Unregister Process from Firmware Updates
 * 
 * Removes a previously registered process from the update manager.
 * After unregistration, the handler_id becomes invalid and cannot be used.
 * 
 * Parameters:
 *   handler_id - Registration ID from rdkFwupdateMgr_registerProcess()
 * 
 * Returns:
 *   0 on success, -1 if handler_id not found or already unregistered
 */
int rdkFwupdateMgr_unregisterProcess(guint64 handler_id);

/*
 * Progress Monitor Thread (Internal Use)
 * 
 * Background thread that polls /opt/curl_progress file and emits D-Bus
 * DownloadProgress signals. Used internally by download worker threads.
 * 
 * This function is the entry point for the progress monitoring thread.
 * It continuously polls the curl progress file and emits D-Bus signals
 * when download progress changes by ≥1%.
 * 
 * Parameters:
 *   user_data - ProgressMonitorContext* with connection, handler_id, firmware_name
 * 
 * Returns:
 *   NULL (thread entry point)
 * 
 * Note: This is an internal API used by rdkv_dbus_server.c for real-time
 *       progress monitoring during firmware downloads.
 */
gpointer rdkfw_progress_monitor_thread(gpointer user_data);

#endif // RDKFWUPDATEMGR_HANDLERS_H
