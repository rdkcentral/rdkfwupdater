/*
 * Copyright 2026 Comcast Cable Communications Management, LLC
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
 * @file rdkFwupdateMgr_log.c
 * @brief Logging implementation for librdkFwupdateMgr client library
 *
 * Init/cleanup:
 *   fwupmgr_log_init()  → calls log_init() from common_utilities
 *                          (same as rdkv_main.c / rdkFwupdateMgr.c)
 *                        → opens /opt/logs/rdkFwupdateMgr.log for FWUPMGR_* output
 *
 *   fwupmgr_log_close() → closes the log file
 *                        → calls log_exit() from common_utilities
 *
 * Logging:
 *   FWUPMGR_* macros → fwupmgr_write_log() → /opt/logs/rdkFwupdateMgr.log
 *   (does NOT go to stdout, so it stays separate from daemon's swupdate.log)
 */

#include "rdkFwupdateMgr_log.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <pthread.h>

/** Log file path for the library (separate from daemon's swupdate.log) */
#define FWUPMGR_LOG_FILE "/opt/logs/rdkFwupdateMgr.log"

/* ========================================================================
 * INTERNAL STATE
 * ======================================================================== */

static FILE *g_fwupmgr_log_fp = NULL;
static pthread_mutex_t g_fwupmgr_log_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ========================================================================
 * fwupmgr_write_log() — writes to /opt/logs/rdkFwupdateMgr.log
 *
 * Called by all FWUPMGR_* macros. Thread-safe via mutex.
 * Auto-opens the log file on first call (lazy init).
 *
 * Log format:
 *   YYMMDD-HH:MM:SS [LOG.RDK.FWUPMGR] LEVEL: file:line message
 * ======================================================================== */

void fwupmgr_write_log(const char *level, const char *file, int line,
                        const char *format, ...)
{
    time_t now;
    struct tm *tm_info;
    char timestamp[64];
    va_list args;

    pthread_mutex_lock(&g_fwupmgr_log_mutex);

    /* Open log file on first use (lazy init) */
    if (!g_fwupmgr_log_fp) {
        g_fwupmgr_log_fp = fopen(FWUPMGR_LOG_FILE, "a");
        if (g_fwupmgr_log_fp) {
            setlinebuf(g_fwupmgr_log_fp);  /* Line-buffered for immediate writes */
        }
    }

    FILE *out = g_fwupmgr_log_fp ? g_fwupmgr_log_fp : stderr;

    /* Timestamp */
    time(&now);
    tm_info = localtime(&now);
    if (!tm_info || strftime(timestamp, sizeof(timestamp), "%y%m%d-%H:%M:%S", tm_info) == 0) {
        snprintf(timestamp, sizeof(timestamp), "UNKNOWN");
    }

    /* Write: timestamp [MODULE] LEVEL: file:line message */
    fprintf(out, "%s [%s] %s: %s:%d ", timestamp, FWUPMGR_LOG_MODULE, level, file, line);

    va_start(args, format);
    vfprintf(out, format, args);
    va_end(args);

    fflush(out);

    pthread_mutex_unlock(&g_fwupmgr_log_mutex);
}

/* ========================================================================
 * INIT / CLEANUP
 * ======================================================================== */

/**
 * @brief Initialize logging for the library
 *
 * 1. Calls log_init() from common_utilities — same as rdkv_main.c and
 *    rdkFwupdateMgr.c. This sets up the common logging infrastructure.
 * 2. Opens /opt/logs/rdkFwupdateMgr.log for FWUPMGR_* macro output.
 */
void fwupmgr_log_init(void)
{
    /* Same log_init() as daemon — sets up common_utilities logging */
    log_init();

    /* Open our own log file for FWUPMGR_* output */
    pthread_mutex_lock(&g_fwupmgr_log_mutex);
    if (!g_fwupmgr_log_fp) {
        g_fwupmgr_log_fp = fopen(FWUPMGR_LOG_FILE, "a");
        if (g_fwupmgr_log_fp) {
            setlinebuf(g_fwupmgr_log_fp);
        }
    }
    pthread_mutex_unlock(&g_fwupmgr_log_mutex);

    FWUPMGR_INFO("librdkFwupdateMgr logging initialized (output: %s)\n", FWUPMGR_LOG_FILE);
}

/**
 * @brief Close logging resources
 *
 * 1. Closes /opt/logs/rdkFwupdateMgr.log
 * 2. Calls log_exit() from common_utilities
 */
void fwupmgr_log_close(void)
{
    FWUPMGR_INFO("librdkFwupdateMgr logging shutdown\n");

    pthread_mutex_lock(&g_fwupmgr_log_mutex);
    if (g_fwupmgr_log_fp) {
        fflush(g_fwupmgr_log_fp);
        fclose(g_fwupmgr_log_fp);
        g_fwupmgr_log_fp = NULL;
    }
    pthread_mutex_unlock(&g_fwupmgr_log_mutex);

    /* Same log_exit() as daemon */
    log_exit();
}

