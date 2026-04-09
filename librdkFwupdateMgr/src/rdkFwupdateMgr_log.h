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
 * FWUPMGR_* macros are thin wrappers around the common SWLOG_* macros
 * from rdkv_cdl_log_wrapper.h.  Each macro prepends the "[FWUPMGR] "
 * component tag so that library log lines are easily distinguishable
 * from daemon (SWLOG_*) and application (EXAMPLE_*) log lines when
 * they share the same stdout/stderr stream.
 *
 * The hosting application (example_plugin, unit-test harness, etc.)
 * is responsible for calling log_init() before using this library
 * and log_exit() on shutdown.  The library does NOT own the log
 * lifecycle.
 *
 * Usage:
 *   FWUPMGR_INFO("Registered with handler: %s\n", handler_id);
 *   FWUPMGR_ERROR("Registration failed: %s\n", error_msg);
 *   FWUPMGR_DEBUG("D-Bus proxy created: %p\n", proxy);
 */

#ifndef RDKFWUPDATEMGR_LOG_H
#define RDKFWUPDATEMGR_LOG_H

#include "rdkv_cdl_log_wrapper.h"  /* SWLOG_*, log_init(), log_exit() */

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * LOGGING MACROS — FWUPMGR_*
 *
 * Thin wrappers around SWLOG_* with a "[FWUPMGR] " component prefix.
 * FWUPMGR_TRACE maps to SWLOG_DEBUG (no TRACE level in SWLOG).
 * FWUPMGR_FATAL maps to SWLOG_ERROR (no FATAL level in SWLOG).
 * ======================================================================== */

#define FWUPMGR_TRACE(format, ...) \
    SWLOG_DEBUG("[FWUPMGR] " format, ##__VA_ARGS__)

#define FWUPMGR_DEBUG(format, ...) \
    SWLOG_DEBUG("[FWUPMGR] " format, ##__VA_ARGS__)

#define FWUPMGR_INFO(format, ...) \
    SWLOG_INFO("[FWUPMGR] " format, ##__VA_ARGS__)

#define FWUPMGR_WARN(format, ...) \
    SWLOG_WARN("[FWUPMGR] " format, ##__VA_ARGS__)

#define FWUPMGR_ERROR(format, ...) \
    SWLOG_ERROR("[FWUPMGR] " format, ##__VA_ARGS__)

#define FWUPMGR_FATAL(format, ...) \
    SWLOG_ERROR("[FWUPMGR][FATAL] " format, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* RDKFWUPDATEMGR_LOG_H */
