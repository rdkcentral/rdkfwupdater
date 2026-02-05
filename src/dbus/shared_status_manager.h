/**
 * @file shared_status_manager.h
 * @brief Thread-safe status management for all shared operation flags
 * 
 * This is a comprehensive solution managing ALL shared status variables:
 * - XConf fetch status (CheckForUpdate)
 * - Download status (DownloadFirmware)
 * - Flash status (UpdateFirmware)
 * 
 * Benefits:
 * - Single mutex for all related operations
 * - Consistent API across all operation types
 * - Atomic compare-and-swap operations
 * - Comprehensive debugging support
 * 
 * Thread Safety: All getter/setter functions are thread-safe
 */

#ifndef SHARED_STATUS_MANAGER_H
#define SHARED_STATUS_MANAGER_H

#include <glib.h>

/* ========================================================================
 * OPERATION TYPE ENUMERATION
 * ======================================================================== */

/**
 * Firmware update operation types
 */
typedef enum {
    OPERATION_XCONF_FETCH,    // CheckForUpdate XConf fetch
    OPERATION_DOWNLOAD,       // DownloadFirmware operation
    OPERATION_FLASH           // UpdateFirmware flash operation
} OperationType;

/* ========================================================================
 * INITIALIZATION / CLEANUP
 * ======================================================================== */

/**
 * @brief Initialize shared status manager
 * 
 * Must be called once at daemon startup.
 * Thread Safety: NOT thread-safe (call from main thread only)
 * 
 * @return TRUE on success, FALSE if already initialized
 */
gboolean initSharedStatusManager(void);

/**
 * @brief Cleanup shared status manager
 * 
 * Call at daemon shutdown after all threads terminated.
 * Thread Safety: NOT thread-safe (call from main thread only)
 */
void cleanupSharedStatusManager(void);

/* ========================================================================
 * GENERIC STATUS ACCESS (All Operation Types)
 * ======================================================================== */

/**
 * @brief Get status for any operation type
 * 
 * @param op_type Operation type to check
 * @return TRUE if operation in progress, FALSE if idle
 * 
 * Thread Safety: Safe from any thread
 * 
 * Example:
 *   if (getOperationStatus(OPERATION_XCONF_FETCH)) {
 *       SWLOG_INFO("XConf fetch in progress\n");
 *   }
 */
gboolean getOperationStatus(OperationType op_type);

/**
 * @brief Set status for any operation type
 * 
 * @param op_type Operation type to update
 * @param status TRUE for in-progress, FALSE for idle
 * 
 * Thread Safety: Safe from any thread
 */
void setOperationStatus(OperationType op_type, gboolean status);

/**
 * @brief Atomically claim operation (compare-and-swap)
 * 
 * @param op_type Operation type to claim
 * @return TRUE if successfully claimed (was idle, now in-progress)
 *         FALSE if already in progress
 * 
 * Thread Safety: Safe from any thread
 * 
 * Example (RECOMMENDED):
 *   if (trySetOperationStatus(OPERATION_DOWNLOAD)) {
 *       // Successfully claimed - start download
 *       start_download_worker();
 *   } else {
 *       // Already downloading - piggyback
 *       queue_waiting_client();
 *   }
 */
gboolean trySetOperationStatus(OperationType op_type);

/* ========================================================================
 * CONVENIENCE WRAPPERS (Easier API for Specific Operations)
 * ======================================================================== */

/* XConf Fetch Status */
gboolean getXConfCommStatus(void);
void setXConfCommStatus(gboolean status);
gboolean trySetXConfCommStatus(void);

/* Download Status */
gboolean getDownloadStatus(void);
void setDownloadStatus(gboolean status);
gboolean trySetDownloadStatus(void);

/* Flash Status */
gboolean getFlashStatus(void);
void setFlashStatus(gboolean status);
gboolean trySetFlashStatus(void);

/* ========================================================================
 * DEBUGGING / MONITORING
 * ======================================================================== */

/**
 * @brief Get human-readable status string
 * 
 * @param op_type Operation type
 * @return Static string: "IDLE", "IN_PROGRESS", or "UNINITIALIZED"
 */
const gchar* getOperationStatusString(OperationType op_type);

/**
 * @brief Dump all operation statuses (debugging)
 * 
 * Thread Safety: Safe from any thread
 */
void dumpAllOperationStatuses(void);

/**
 * @brief Get all statuses atomically (snapshot)
 * 
 * @param xconf_status Output: XConf fetch status
 * @param download_status Output: Download status
 * @param flash_status Output: Flash status
 * 
 * Thread Safety: Safe from any thread
 * 
 * Note: All three values read atomically under single mutex lock
 */
void getAll operationStatuses(gboolean *xconf_status,
                               gboolean *download_status,
                               gboolean *flash_status);

#endif /* SHARED_STATUS_MANAGER_H */
