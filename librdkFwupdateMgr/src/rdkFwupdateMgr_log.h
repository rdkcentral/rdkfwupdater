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
 * Logging init uses log_init() from common_utilities (rdkv_cdl_log_wrapper)
 * — the same init as the daemon (rdkv_main.c / rdkFwupdateMgr.c).
 *
 * FWUPMGR_* macros write to /opt/logs/rdkFwupdateMgr.log via
 * fwupmgr_write_log(), which is a simple file-based logger.
 * This keeps library logs separate from the daemon's /opt/logs/swupdate.log.
 */

#ifndef RDKFWUPDATEMGR_LOG_H
#define RDKFWUPDATEMGR_LOG_H

#include "rdkv_cdl_log_wrapper.h"  /* For log_init(), log_exit() */

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * MODULE TAG
 *
 * Daemon (SWLOG_*) → "LOG.RDK.FWUPG"  → /opt/logs/swupdate.log
 * Library (FWUPMGR_*) → "LOG.RDK.FWUPMGR" → /opt/logs/rdkFwupdateMgr.log
 * ======================================================================== */

#define FWUPMGR_LOG_MODULE "LOG.RDK.FWUPMGR"

/* ========================================================================
 * LOGGING INIT / CLEANUP
 *
 * fwupmgr_log_init()  — calls log_init() (same as daemon) + opens log file
 * fwupmgr_log_close() — closes log file + calls log_exit()
 * ======================================================================== */

void fwupmgr_log_init(void);
void fwupmgr_log_close(void);

/* ========================================================================
 * INTERNAL LOG WRITER
 *
 * Writes directly to /opt/logs/rdkFwupdateMgr.log.
 * Thread-safe (internal mutex). Auto-opens file on first call.
 * ======================================================================== */

void fwupmgr_write_log(const char *level, const char *file, int line,
                        const char *format, ...);

/* ========================================================================
 * LOGGING MACROS — FWUPMGR_*
 *
 * Always route to fwupmgr_write_log() → /opt/logs/rdkFwupdateMgr.log
 * Regardless of whether RDK_LOGGER is defined or not.
 *
 * Usage:
 *   FWUPMGR_INFO("Registered with handler: %s\n", handler_id);
 *   FWUPMGR_ERROR("Registration failed: %s\n", error_msg);
 *   FWUPMGR_DEBUG("D-Bus proxy created: %p\n", proxy);
 * ======================================================================== */

#define FWUPMGR_TRACE(format, ...) \
    fwupmgr_write_log("TRACE", __FILE__, __LINE__, format, ##__VA_ARGS__)

#define FWUPMGR_DEBUG(format, ...) \
    fwupmgr_write_log("DEBUG", __FILE__, __LINE__, format, ##__VA_ARGS__)

#define FWUPMGR_INFO(format, ...) \
    fwupmgr_write_log("INFO", __FILE__, __LINE__, format, ##__VA_ARGS__)

#define FWUPMGR_WARN(format, ...) \
    fwupmgr_write_log("WARN", __FILE__, __LINE__, format, ##__VA_ARGS__)

#define FWUPMGR_ERROR(format, ...) \
    fwupmgr_write_log("ERROR", __FILE__, __LINE__, format, ##__VA_ARGS__)

#define FWUPMGR_FATAL(format, ...) \
    fwupmgr_write_log("FATAL", __FILE__, __LINE__, format, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* RDKFWUPDATEMGR_LOG_H */
