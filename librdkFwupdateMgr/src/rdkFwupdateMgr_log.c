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
 */

#include "rdkFwupdateMgr_log.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include <errno.h>

/* ========================================================================
 * INTERNAL STATE
 * ======================================================================== */

static FILE *g_log_file = NULL;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_log_initialized = 0;

/* ========================================================================
 * LOGGING IMPLEMENTATION
 * ======================================================================== */

/**
 * @brief Initialize logging
 */
void fwupmgr_log_init(void)
{
    pthread_mutex_lock(&g_log_mutex);
    
    if (g_log_initialized) {
        pthread_mutex_unlock(&g_log_mutex);
        return;  // Already initialized
    }
    
    // Create log directory if it doesn't exist
    mkdir("/opt/logs", 0755);  // Ignore error if exists
    
    // Open log file in append mode
    g_log_file = fopen(FWUPMGR_LOG_FILE, "a");
    if (!g_log_file) {
        // Fallback to stderr if log file can't be opened
        fprintf(stderr, "[%s] WARNING: Cannot open log file %s: %s\n",
                FWUPMGR_LOG_MODULE, FWUPMGR_LOG_FILE, strerror(errno));
        fprintf(stderr, "[%s] Logging will go to stderr\n", FWUPMGR_LOG_MODULE);
    } else {
        // Make log file line-buffered for immediate writes
        setlinebuf(g_log_file);
    }
    
    g_log_initialized = 1;
    pthread_mutex_unlock(&g_log_mutex);
    
    // Log initialization message
    fwupmgr_log_internal("INFO", "Logging initialized\n");
}

/**
 * @brief Close logging
 */
void fwupmgr_log_close(void)
{
    pthread_mutex_lock(&g_log_mutex);
    
    if (!g_log_initialized) {
        pthread_mutex_unlock(&g_log_mutex);
        return;  // Not initialized
    }
    
    if (g_log_file) {
        // Write shutdown message directly to avoid deadlock
        // (fwupmgr_log_internal would try to lock g_log_mutex again)
        time_t now;
        struct tm *tm_info;
        char timestamp[64];
        
        time(&now);
        tm_info = localtime(&now);
        if (strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info) == 0) {
            snprintf(timestamp, sizeof(timestamp), "UNKNOWN-TIME");
        }
        
        fprintf(g_log_file, "%s [%s] INFO: Logging shutdown\n", 
                timestamp, FWUPMGR_LOG_MODULE);
        fflush(g_log_file);
        
        fclose(g_log_file);
        g_log_file = NULL;
    }
    
    g_log_initialized = 0;
    pthread_mutex_unlock(&g_log_mutex);
}

/**
 * @brief Internal logging function with timestamp and thread-safety
 */
void fwupmgr_log_internal(const char *level, const char *format, ...)
{
    time_t now;
    struct tm *tm_info;
    char timestamp[64];
    va_list args;
    FILE *output;
    
    pthread_mutex_lock(&g_log_mutex);
    
    // Auto-initialize if not done
    if (!g_log_initialized) {
        pthread_mutex_unlock(&g_log_mutex);
        fwupmgr_log_init();
        pthread_mutex_lock(&g_log_mutex);
    }
    
    // Determine output stream (log file or stderr fallback)
    output = g_log_file ? g_log_file : stderr;
    
    // Get current timestamp
    time(&now);
    tm_info = localtime(&now);
    if (strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info) == 0) {
        snprintf(timestamp, sizeof(timestamp), "UNKNOWN-TIME");
    }
    
    // Write log header: timestamp [MODULE] LEVEL:
    fprintf(output, "%s [%s] %s: ", timestamp, FWUPMGR_LOG_MODULE, level);
    
    // Write log message
    va_start(args, format);
    vfprintf(output, format, args);
    va_end(args);
    
    // Ensure immediate write
    fflush(output);
    
    pthread_mutex_unlock(&g_log_mutex);
}

