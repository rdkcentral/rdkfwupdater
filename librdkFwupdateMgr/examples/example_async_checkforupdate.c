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
 * @file example_async_checkforupdate.c
 * @brief Example program demonstrating async CheckForUpdate API
 *
 * This example shows how to use the new non-blocking checkForUpdate_async() API.
 * It demonstrates:
 * - Calling the async API with a callback
 * - Receiving and processing the update information
 * - Handling both success and error cases
 * - Cancelling an operation (optional)
 *
 * COMPILE:
 * ========
 * gcc -o example_async example_async_checkforupdate.c \
 *     -I../include \
 *     -L../lib \
 *     -lrdkFwupdateMgr \
 *     `pkg-config --cflags --libs glib-2.0` \
 *     -lpthread
 *
 * RUN:
 * ====
 * LD_LIBRARY_PATH=../lib ./example_async
 */

#include "rdkFwupdateMgr_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

/* Global flag for graceful shutdown */
static volatile int g_running = 1;

/**
 * @brief Signal handler for Ctrl+C
 */
static void signal_handler(int sig) {
    (void)sig;
    printf("\n[MAIN] Caught interrupt signal, shutting down...\n");
    g_running = 0;
}

/**
 * @brief Callback function invoked when update check completes
 *
 * This function is called from a background thread when the daemon
 * responds with firmware update information.
 *
 * IMPORTANT: Keep this function short and thread-safe!
 *
 * @param info Update information (valid only during this callback)
 * @param user_data User data (we pass a string identifier)
 */
static void update_callback(const AsyncUpdateInfo *info, void *user_data) {
    const char *caller_name = (const char *)user_data;
    
    printf("\n========================================\n");
    printf("[CALLBACK] Called from: %s\n", caller_name ? caller_name : "Unknown");
    printf("========================================\n");
    
    /* Check API result */
    if (info->result != 0) {
        printf("[CALLBACK] ERROR: API call failed (result=%d)\n", info->result);
        printf("[CALLBACK] Message: %s\n", info->status_message ? info->status_message : "N/A");
        return;
    }
    
    /* Display firmware status */
    printf("[CALLBACK] Status Code: %d ", info->status_code);
    switch (info->status_code) {
        case 0:  printf("(FIRMWARE_AVAILABLE)\n"); break;
        case 1:  printf("(FIRMWARE_NOT_AVAILABLE)\n"); break;
        case 2:  printf("(UPDATE_NOT_ALLOWED)\n"); break;
        case 3:  printf("(FIRMWARE_CHECK_ERROR)\n"); break;
        case 4:  printf("(IGNORE_OPTOUT)\n"); break;
        case 5:  printf("(BYPASS_OPTOUT)\n"); break;
        default: printf("(UNKNOWN)\n"); break;
    }
    
    /* Display version information */
    printf("[CALLBACK] Current Version:   %s\n", 
           info->current_version ? info->current_version : "N/A");
    printf("[CALLBACK] Available Version: %s\n", 
           info->available_version ? info->available_version : "N/A");
    
    /* Check if update is available */
    printf("[CALLBACK] Update Available:  %s\n", 
           info->update_available ? "YES" : "NO");
    
    /* Display status message */
    printf("[CALLBACK] Status Message:    %s\n", 
           info->status_message ? info->status_message : "N/A");
    
    /* Display raw update details (if available) */
    if (info->update_details != NULL && strlen(info->update_details) > 0) {
        printf("[CALLBACK] Update Details:\n");
        printf("           %s\n", info->update_details);
        
        /* Parse update details (just for demonstration) */
        char *details_copy = strdup(info->update_details);
        if (details_copy != NULL) {
            char *token = strtok(details_copy, ",");
            while (token != NULL) {
                printf("           - %s\n", token);
                token = strtok(NULL, ",");
            }
            free(details_copy);
        }
    }
    
    printf("========================================\n\n");
}

/**
 * @brief Example 1: Simple async check
 */
static void example_simple_check(void) {
    printf("\n=== Example 1: Simple Async Check ===\n");
    
    AsyncCallbackId id = checkForUpdate_async(update_callback, "Example1");
    
    if (id == 0) {
        fprintf(stderr, "[ERROR] Failed to start async check\n");
        return;
    }
    
    printf("[MAIN] Async check started, callback ID: %u\n", id);
    printf("[MAIN] Waiting for callback...\n");
    
    /* Your application continues running here! */
    /* The callback will be invoked when the daemon responds */
    
    /* For this example, we'll wait a bit */
    sleep(5);
}

/**
 * @brief Example 2: Multiple concurrent checks
 */
static void example_multiple_checks(void) {
    printf("\n=== Example 2: Multiple Concurrent Checks ===\n");
    
    AsyncCallbackId id1 = checkForUpdate_async(update_callback, "Check-1");
    AsyncCallbackId id2 = checkForUpdate_async(update_callback, "Check-2");
    AsyncCallbackId id3 = checkForUpdate_async(update_callback, "Check-3");
    
    printf("[MAIN] Started 3 concurrent checks:\n");
    printf("[MAIN]   Check 1 ID: %u\n", id1);
    printf("[MAIN]   Check 2 ID: %u\n", id2);
    printf("[MAIN]   Check 3 ID: %u\n", id3);
    printf("[MAIN] All callbacks will receive the same result\n");
    printf("[MAIN] Waiting for callbacks...\n");
    
    /* Wait for all callbacks */
    sleep(5);
}

/**
 * @brief Example 3: Cancellation
 */
static void example_cancellation(void) {
    printf("\n=== Example 3: Cancellation ===\n");
    
    AsyncCallbackId id = checkForUpdate_async(update_callback, "ToBeCancelled");
    
    if (id == 0) {
        fprintf(stderr, "[ERROR] Failed to start async check\n");
        return;
    }
    
    printf("[MAIN] Async check started, callback ID: %u\n", id);
    printf("[MAIN] Attempting to cancel immediately...\n");
    
    /* Try to cancel (may or may not succeed depending on timing) */
    int ret = checkForUpdate_async_cancel(id);
    
    if (ret == 0) {
        printf("[MAIN] Cancellation successful! Callback will NOT be invoked.\n");
    } else {
        printf("[MAIN] Cancellation failed (already completed or in progress)\n");
        printf("[MAIN] Callback may still be invoked...\n");
    }
    
    sleep(5);
}

/**
 * @brief Main function
 */
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    
    printf("\n");
    printf("========================================\n");
    printf("  Async CheckForUpdate Example\n");
    printf("========================================\n");
    printf("\n");
    printf("This example demonstrates the async checkForUpdate_async() API.\n");
    printf("The library will automatically initialize on startup.\n");
    printf("\n");
    
    /* Register signal handler for graceful shutdown */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("[MAIN] Library initialized, background thread running\n");
    printf("[MAIN] Press Ctrl+C to exit\n\n");
    
    /* Run examples */
    example_simple_check();
    
    if (g_running) {
        example_multiple_checks();
    }
    
    if (g_running) {
        example_cancellation();
    }
    
    /* Keep program running to receive callbacks */
    printf("\n[MAIN] All examples complete.\n");
    printf("[MAIN] Press Ctrl+C to exit, or waiting 10 more seconds...\n\n");
    
    int countdown = 10;
    while (g_running && countdown > 0) {
        sleep(1);
        countdown--;
    }
    
    printf("\n[MAIN] Shutting down...\n");
    printf("[MAIN] Library will automatically clean up\n");
    printf("[MAIN] (Background thread stopped, pending callbacks cancelled)\n");
    printf("\n[MAIN] Example complete!\n\n");
    
    return 0;
}

/**
 * EXPECTED OUTPUT:
 * ================
 *
 * ========================================
 *   Async CheckForUpdate Example
 * ========================================
 *
 * This example demonstrates the async checkForUpdate_async() API.
 * The library will automatically initialize on startup.
 *
 * [MAIN] Library initialized, background thread running
 * [MAIN] Press Ctrl+C to exit
 *
 * === Example 1: Simple Async Check ===
 * [MAIN] Async check started, callback ID: 1
 * [MAIN] Waiting for callback...
 *
 * ========================================
 * [CALLBACK] Called from: Example1
 * ========================================
 * [CALLBACK] Status Code: 1 (FIRMWARE_NOT_AVAILABLE)
 * [CALLBACK] Current Version:   V1.2.3
 * [CALLBACK] Available Version: N/A
 * [CALLBACK] Update Available:  NO
 * [CALLBACK] Status Message:    Already on latest version
 * ========================================
 *
 * === Example 2: Multiple Concurrent Checks ===
 * [MAIN] Started 3 concurrent checks:
 * [MAIN]   Check 1 ID: 2
 * [MAIN]   Check 2 ID: 3
 * [MAIN]   Check 3 ID: 4
 * [MAIN] All callbacks will receive the same result
 * [MAIN] Waiting for callbacks...
 *
 * ========================================
 * [CALLBACK] Called from: Check-1
 * ========================================
 * ... (same result for all 3 callbacks)
 *
 * [MAIN] All examples complete.
 * [MAIN] Press Ctrl+C to exit, or waiting 10 more seconds...
 *
 * [MAIN] Shutting down...
 * [MAIN] Library will automatically clean up
 * [MAIN] (Background thread stopped, pending callbacks cancelled)
 *
 * [MAIN] Example complete!
 */
