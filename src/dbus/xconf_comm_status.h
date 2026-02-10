/*
 * Copyright 2023 Comcast Cable Communications Management, LLC
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
 * @file xconf_comm_status.h
 * @brief Thread-safe XConf communication status management API
 * 
 * Provides mutex-protected getter/setter functions for tracking XConf
 * fetch operation status across multiple threads.
 * 
 * Thread Safety: All functions (except init/cleanup) are thread-safe
 * 
 * Usage Pattern:
 *   1. Call initXConfCommStatus() once at startup (main thread)
 *   2. Use trySetXConfCommStatus() to start fetch (any thread)
 *   3. Use getXConfCommStatus() to check status (any thread)
 *   4. Use setXConfCommStatus(FALSE) when done (worker thread)
 *   5. Call cleanupXConfCommStatus() at shutdown (main thread)
 */

#ifndef XCONF_COMM_STATUS_H
#define XCONF_COMM_STATUS_H

#include <glib.h>

/* ========================================================================
 * INITIALIZATION / CLEANUP API
 * ======================================================================== */

/**
 * @brief Initialize XConf status tracking system
 * 
 * Must be called once at daemon startup before creating any threads.
 * Initializes mutex and sets initial state to idle.
 * 
 * Thread Safety: NOT thread-safe. Call from main thread only.
 * 
 * @return TRUE on success, FALSE if already initialized
 * 
 * Example:
 *   int main() {
 *       initXConfCommStatus();
 *       // ... create D-Bus server, spawn threads ...
 *   }
 */
gboolean initXConfCommStatus(void);

/**
 * @brief Cleanup XConf status tracking system
 * 
 * Destroys mutex and resets state. Call at daemon shutdown after
 * all worker threads have been joined.
 * 
 * Thread Safety: NOT thread-safe. Call from main thread only.
 * Precondition: All threads must be terminated
 * 
 * Example:
 *   int main() {
 *       // ... join all threads ...
 *       cleanupXConfCommStatus();
 *       return 0;
 *   }
 */
void cleanupXConfCommStatus(void);

/* ========================================================================
 * STATUS ACCESS API (Thread-Safe)
 * ======================================================================== */

/**
 * @brief Get current XConf fetch status
 * 
 * Thread-safe read of XConf operation status.
 * 
 * @return TRUE if XConf fetch in progress, FALSE if idle
 * 
 * Thread Safety: Safe from any thread
 * Blocking: Minimal (~microseconds for mutex)
 * 
 * Example:
 *   if (getXConfCommStatus()) {
 *       SWLOG_INFO("Fetch already running\n");
 *   }
 */
gboolean getXConfCommStatus(void);

/**
 * @brief Set XConf fetch status
 * 
 * Thread-safe write of XConf operation status.
 * 
 * @param status TRUE to mark in-progress, FALSE to mark idle
 * 
 * Thread Safety: Safe from any thread
 * Blocking: Minimal (~microseconds for mutex)
 * 
 * Example:
 *   // Start fetch
 *   setXConfCommStatus(TRUE);
 *   
 *   // Complete fetch
 *   setXConfCommStatus(FALSE);
 */
void setXConfCommStatus(gboolean status);

/**
 * @brief Atomically claim XConf operation (compare-and-swap)
 * 
 * Atomically checks if idle and sets to in-progress if so.
 * Prevents TOCTOU race conditions.
 * 
 * @return TRUE if successfully claimed (was idle, now in-progress)
 *         FALSE if already in progress (no change)
 * 
 * Thread Safety: Safe from any thread
 * Blocking: Minimal (~microseconds for mutex)
 * 
 * Example (RECOMMENDED PATTERN):
 *   if (trySetXConfCommStatus()) {
 *       // We successfully claimed operation - start fetch
 *       spawn_xconf_worker_thread();
 *   } else {
 *       // Already running - piggyback on existing fetch
 *       queue_waiting_client();
 *   }
 * 
 * IMPORTANT: Prefer this over separate get() + set() calls!
 */
gboolean trySetXConfCommStatus(void);

/* ========================================================================
 * DEBUGGING / MONITORING API (Optional)
 * ======================================================================== */

/**
 * @brief Get status as human-readable string
 * 
 * @return Static string: "IDLE", "IN_PROGRESS", or "UNINITIALIZED"
 *         Do not free returned string.
 * 
 * Thread Safety: Safe from any thread
 * 
 * Example:
 *   SWLOG_INFO("XConf status: %s\n", getXConfCommStatusString());
 */
const gchar* getXConfCommStatusString(void);

/**
 * @brief Print detailed status for debugging
 * 
 * Logs comprehensive status information including mutex state.
 * 
 * Thread Safety: Safe from any thread
 * 
 * Example:
 *   dumpXConfCommStatus();  // Logs to SWLOG
 */
void dumpXConfCommStatus(void);

#endif /* XCONF_COMM_STATUS_H */
