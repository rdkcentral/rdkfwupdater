/**
 * @file xconf_comm_status.c
 * @brief Thread-safe XConf communication status management
 * 
 * This module provides thread-safe access to the XConf fetch operation status
 * using mutex protection. Prevents race conditions when multiple threads check
 * or update the XConf communication state.
 * 
 * Usage:
 *   - Call initXConfCommStatus() once at daemon startup
 *   - Use setXConfCommStatus() to update status from any thread
 *   - Use getXConfCommStatus() to check status from any thread
 *   - Call cleanupXConfCommStatus() at daemon shutdown
 * 
 * Thread Safety: All functions are thread-safe via internal mutex protection
 */

#include <glib.h>
#include "rdkv_cdl_log_wrapper.h"

/* ========================================================================
 * PRIVATE VARIABLES (Module-Internal)
 * ======================================================================== */

/**
 * Mutex protecting IsCheckUpdateInProgress flag
 * 
 * Ensures atomic read-modify-write operations on the status flag.
 * Must be locked before accessing IsCheckUpdateInProgress.
 */
static GMutex check_update_mutex;

/**
 * Flag indicating XConf fetch operation status
 * 
 * TRUE:  XConf fetch is in progress (background thread active)
 * FALSE: No XConf fetch running (idle state)
 * 
 * CRITICAL: NEVER access this variable directly!
 * ALWAYS use getXConfCommStatus() and setXConfCommStatus()
 */
static gboolean IsCheckUpdateInProgress = FALSE;

/**
 * Initialization flag to ensure mutex is initialized only once
 */
static gboolean xconf_status_initialized = FALSE;

/* ========================================================================
 * PUBLIC API - Thread-Safe Status Management
 * ======================================================================== */

/**
 * @brief Initialize XConf communication status tracking system
 * 
 * Must be called once at daemon startup before any threads are created.
 * Initializes the mutex and sets initial state to FALSE (idle).
 * 
 * Thread Safety: NOT thread-safe. Must be called from main thread only,
 *                before spawning any worker threads.
 * 
 * @return TRUE on success, FALSE if already initialized
 */
gboolean initXConfCommStatus(void)
{
    if (xconf_status_initialized) {
        SWLOG_WARN("[XCONF_STATUS] Already initialized (ignoring duplicate init)\n");
        return FALSE;
    }
    
    g_mutex_init(&check_update_mutex);
    IsCheckUpdateInProgress = FALSE;
    xconf_status_initialized = TRUE;
    
    SWLOG_INFO("[XCONF_STATUS] Initialized XConf status tracking (mutex: %p)\n", 
               &check_update_mutex);
    return TRUE;
}

/**
 * @brief Get current XConf communication status (thread-safe read)
 * 
 * Returns whether an XConf fetch operation is currently in progress.
 * Uses mutex to ensure atomic read operation.
 * 
 * Thread Safety: Safe to call from any thread
 * Blocking: Minimal (only mutex acquisition overhead, ~microseconds)
 * 
 * @return TRUE if XConf fetch is in progress, FALSE otherwise
 * 
 * Example Usage:
 *   if (getXConfCommStatus()) {
 *       SWLOG_INFO("XConf fetch already running - piggybacking\n");
 *       queue_waiting_client(...);
 *   } else {
 *       // Start new fetch
 *       setXConfCommStatus(TRUE);
 *       spawn_xconf_worker_thread();
 *   }
 */
gboolean getXConfCommStatus(void)
{
    if (!xconf_status_initialized) {
        SWLOG_ERROR("[XCONF_STATUS] CRITICAL: getXConfCommStatus() called before initXConfCommStatus()\n");
        return FALSE;  // Safe default: assume no fetch in progress
    }
    
    g_mutex_lock(&check_update_mutex);
    gboolean status = IsCheckUpdateInProgress;
    g_mutex_unlock(&check_update_mutex);
    
    return status;
}

/**
 * @brief Set XConf communication status (thread-safe write)
 * 
 * Updates the XConf fetch operation status flag.
 * Uses mutex to ensure atomic write operation.
 * 
 * Thread Safety: Safe to call from any thread
 * Blocking: Minimal (only mutex acquisition overhead, ~microseconds)
 * 
 * @param status New status value
 *               - TRUE: XConf fetch started (mark as in-progress)
 *               - FALSE: XConf fetch completed (mark as idle)
 * 
 * Example Usage:
 *   // Starting XConf fetch
 *   setXConfCommStatus(TRUE);
 *   
 *   // Completing XConf fetch
 *   setXConfCommStatus(FALSE);
 *   notify_waiting_clients();
 */
void setXConfCommStatus(gboolean status)
{
    if (!xconf_status_initialized) {
        SWLOG_ERROR("[XCONF_STATUS] CRITICAL: setXConfCommStatus() called before initXConfCommStatus()\n");
        return;  // Ignore write attempt
    }
    
    g_mutex_lock(&check_update_mutex);
    gboolean old_status = IsCheckUpdateInProgress;
    IsCheckUpdateInProgress = status;
    g_mutex_unlock(&check_update_mutex);
    
}

/**
 * @brief Atomically check and set XConf status (compare-and-swap)
 * 
 * Atomically checks if XConf fetch is idle, and if so, marks it as in-progress.
 * This prevents TOCTOU (Time-Of-Check-Time-Of-Use) race conditions.
 * 
 * Thread Safety: Safe to call from any thread
 * Blocking: Minimal (only mutex acquisition overhead, ~microseconds)
 * 
 * @return TRUE if successfully claimed XConf operation (was idle, now in-progress)
 *         FALSE if XConf operation already in progress (no change)
 * 
 * Example Usage:
 *   if (trySetXConfCommStatus()) {
 *       // Successfully claimed XConf operation - we start the fetch
 *       SWLOG_INFO("Starting new XConf fetch\n");
 *       spawn_xconf_worker_thread();
 *   } else {
 *       // Another thread already started fetch - we piggyback
 *       SWLOG_INFO("XConf fetch already running - piggybacking\n");
 *       queue_waiting_client(...);
 *   }
 * 
 * IMPORTANT: This is the PREFERRED method for starting XConf fetches!
 *            Eliminates the race condition between get() and set().
 */
gboolean trySetXConfCommStatus(void)
{
    if (!xconf_status_initialized) {
        SWLOG_ERROR("[XCONF_STATUS] CRITICAL: trySetXConfCommStatus() called before initXConfCommStatus()\n");
        return FALSE;
    }
    
    g_mutex_lock(&check_update_mutex);
    
    gboolean was_idle = !IsCheckUpdateInProgress;
    
    if (was_idle) {
        // Successfully claimed operation (was idle, now in-progress)
        IsCheckUpdateInProgress = TRUE;
        g_mutex_unlock(&check_update_mutex);
        SWLOG_INFO("[XCONF_STATUS] Successfully claimed XConf operation (IDLE -> IN_PROGRESS)\n");
        return TRUE;
    } else {
        // Already in progress (cannot claim)
        g_mutex_unlock(&check_update_mutex);
        SWLOG_INFO("[XCONF_STATUS] XConf operation already in progress (no change)\n");
        return FALSE;
    }
}

/**
 * @brief Cleanup XConf status tracking system
 * 
 * Destroys the mutex and resets state. Should be called at daemon shutdown
 * after all worker threads have been joined.
 * 
 * Thread Safety: NOT thread-safe. Must be called from main thread only,
 *                after all worker threads have terminated.
 * 
 * Precondition: All worker threads accessing XConf status must be terminated
 */
void cleanupXConfCommStatus(void)
{
    if (!xconf_status_initialized) {
        SWLOG_WARN("[XCONF_STATUS] Already cleaned up (ignoring duplicate cleanup)\n");
        return;
    }
    
    // Note: No mutex lock here - caller must ensure no threads are using it
    SWLOG_INFO("[XCONF_STATUS] Cleaning up (final status: %s)\n",
               IsCheckUpdateInProgress ? "IN_PROGRESS" : "IDLE");
    
    g_mutex_clear(&check_update_mutex);
    IsCheckUpdateInProgress = FALSE;
    xconf_status_initialized = FALSE;
    
    SWLOG_INFO("[XCONF_STATUS] Cleanup complete\n");
}

/* ========================================================================
 * DEBUGGING / MONITORING API (Optional)
 * ======================================================================== */

/**
 * @brief Get human-readable status string
 * 
 * Returns a static string describing the current XConf status.
 * Useful for logging and debugging.
 * 
 * Thread Safety: Safe to call from any thread
 * 
 * @return Static string (do not free): "IDLE", "IN_PROGRESS", or "UNINITIALIZED"
 */
const gchar* getXConfCommStatusString(void)
{
    if (!xconf_status_initialized) {
        return "UNINITIALIZED";
    }
    
    return getXConfCommStatus() ? "IN_PROGRESS" : "IDLE";
}

/**
 * @brief Print detailed status information (for debugging)
 * 
 * Logs comprehensive status information including mutex state,
 * initialization status, and current flag value.
 * 
 * Thread Safety: Safe to call from any thread
 */
void dumpXConfCommStatus(void)
{
    SWLOG_INFO("========== XCONF STATUS DEBUG DUMP ==========\n");
    SWLOG_INFO("Initialized: %s\n", xconf_status_initialized ? "YES" : "NO");
    SWLOG_INFO("Mutex Address: %p\n", &check_update_mutex);
    
    if (xconf_status_initialized) {
        g_mutex_lock(&check_update_mutex);
        SWLOG_INFO("Current Status: %s\n", IsCheckUpdateInProgress ? "IN_PROGRESS" : "IDLE");
        g_mutex_unlock(&check_update_mutex);
    } else {
        SWLOG_INFO("Current Status: UNINITIALIZED\n");
    }
    
    SWLOG_INFO("=============================================\n");
}
