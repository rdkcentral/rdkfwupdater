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
 * FWUPMGR_* macros log directly with the "LOG.RDK.FWUPMGR" module
 * (when RDK_LOGGER is enabled) so that library log lines appear as
 * "[FWUPMGR]" in the output  clearly distinguishable from daemon
 * logs ("[FWUPG]") and common-utility logs ("[COMMONUTILITIES]")
 * without any redundant double-tagging.
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
 * Library code uses FWUPMGR_* macros  - logs as [FWUPMGR]
 * Example app defines EXAMPLE_* macros - logs as [EXAMPLE]
 * ======================================================================== */

#if defined(RDK_LOGGER)
#include "rdk_debug.h"

/* Generic base macro callers provide their own module name */
#define FWUPMGR_LOG(level, module, format, ...) \
    RDK_LOG(level, module, format, ##__VA_ARGS__)

/* Default library macros  use LOG.RDK.FWUPMGR */
#define FWUPMGR_TRACE(format, ...) FWUPMGR_LOG(RDK_LOG_TRACE1, "LOG.RDK.FWUPMGR", format, ##__VA_ARGS__)
#define FWUPMGR_DEBUG(format, ...) FWUPMGR_LOG(RDK_LOG_DEBUG,  "LOG.RDK.FWUPMGR", format, ##__VA_ARGS__)
#define FWUPMGR_INFO(format, ...)  FWUPMGR_LOG(RDK_LOG_INFO,   "LOG.RDK.FWUPMGR", format, ##__VA_ARGS__)
#define FWUPMGR_WARN(format, ...)  FWUPMGR_LOG(RDK_LOG_WARN,   "LOG.RDK.FWUPMGR", format, ##__VA_ARGS__)
#define FWUPMGR_ERROR(format, ...) FWUPMGR_LOG(RDK_LOG_ERROR,  "LOG.RDK.FWUPMGR", format, ##__VA_ARGS__)
#define FWUPMGR_FATAL(format, ...) FWUPMGR_LOG(RDK_LOG_FATAL,  "LOG.RDK.FWUPMGR", format, ##__VA_ARGS__)

#else


/* Default library macros */
#define FWUPMGR_TRACE(FORMAT...) FWUPMGR_LOG(FWUPMGR_LOG_INFO, "FWUPMGR", FORMAT)
#define FWUPMGR_DEBUG(FORMAT...) FWUPMGR_LOG(FWUPMGR_LOG_INFO, "FWUPMGR", FORMAT)
#define FWUPMGR_INFO(FORMAT...)  FWUPMGR_LOG(FWUPMGR_LOG_INFO, "FWUPMGR", FORMAT)
#define FWUPMGR_WARN(FORMAT...)  FWUPMGR_LOG(FWUPMGR_LOG_INFO, "FWUPMGR", FORMAT)
#define FWUPMGR_ERROR(FORMAT...) FWUPMGR_LOG(FWUPMGR_LOG_INFO, "FWUPMGR", FORMAT)
#define FWUPMGR_FATAL(FORMAT...) FWUPMGR_LOG(FWUPMGR_LOG_INFO, "FWUPMGR", FORMAT)

#endif

#ifdef __cplusplus
}
#endif

#endif /* RDKFWUPDATEMGR_LOG_H */
