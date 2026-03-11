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
 * @file rdkFwupdateMgr_log.h
 * @brief Logging macros for librdkFwupdateMgr client library
 *
 * This header provides logging macros that write to /opt/logs/rdkFwupdateMgr.log
 * using the RDK logger infrastructure, similar to SWLOG_* macros used in the daemon.
 */

#ifndef RDKFWUPDATEMGR_LOG_H
#define RDKFWUPDATEMGR_LOG_H

#include <stdio.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * LOGGING CONFIGURATION
 * ======================================================================== */

/** Log file path - same as daemon for consistent logging */
#define FWUPMGR_LOG_FILE "/opt/logs/rdkFwupdateMgr.log"

/** Log module name for identification */
#define FWUPMGR_LOG_MODULE "librdkFwupdateMgr"

/* ========================================================================
 * LOGGING API
 * ======================================================================== */

/**
 * @brief Initialize logging for the library
 *
 * Opens the log file for appending. Should be called once at library init.
 * Safe to call multiple times (no-op after first call).
 */
void fwupmgr_log_init(void);

/**
 * @brief Close logging resources
 *
 * Closes the log file. Should be called at library cleanup.
 * Safe to call multiple times (no-op if already closed).
 */
void fwupmgr_log_close(void);

/**
 * @brief Internal logging function
 *
 * @param level Log level string ("INFO", "ERROR", "DEBUG", "WARN")
 * @param format Printf-style format string
 * @param ... Variable arguments for format string
 */
void fwupmgr_log_internal(const char *level, const char *format, ...);

/* ========================================================================
 * LOGGING MACROS - Same pattern as SWLOG_* in daemon
 * ======================================================================== */

/**
 * @brief Log informational message
 *
 * Usage: FWUPMGR_INFO("Registered with handler: %s\n", handler_id);
 */
#define FWUPMGR_INFO(format, ...) \
    fwupmgr_log_internal("INFO", "[%s:%d] " format, __FUNCTION__, __LINE__, ##__VA_ARGS__)

/**
 * @brief Log error message
 *
 * Usage: FWUPMGR_ERROR("Registration failed: %s\n", error_msg);
 */
#define FWUPMGR_ERROR(format, ...) \
    fwupmgr_log_internal("ERROR", "[%s:%d] " format, __FUNCTION__, __LINE__, ##__VA_ARGS__)

/**
 * @brief Log debug message
 *
 * Usage: FWUPMGR_DEBUG("D-Bus proxy created: %p\n", proxy);
 */
#define FWUPMGR_DEBUG(format, ...) \
    fwupmgr_log_internal("DEBUG", "[%s:%d] " format, __FUNCTION__, __LINE__, ##__VA_ARGS__)

/**
 * @brief Log warning message
 *
 * Usage: FWUPMGR_WARN("Daemon not responding, retry recommended\n");
 */
#define FWUPMGR_WARN(format, ...) \
    fwupmgr_log_internal("WARN", "[%s:%d] " format, __FUNCTION__, __LINE__, ##__VA_ARGS__)

/**
 * @brief Log fatal error message
 *
 * Usage: FWUPMGR_FATAL("Out of memory, cannot continue\n");
 */
#define FWUPMGR_FATAL(format, ...) \
    fwupmgr_log_internal("FATAL", "[%s:%d] " format, __FUNCTION__, __LINE__, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* RDKFWUPDATEMGR_LOG_H */
