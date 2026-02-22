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
 * @file example_plugin_registration.c
 * @brief Example usage of registerProcess/unregisterProcess APIs
 *
 * This example demonstrates how plugin implementation teams should use
 * the firmware update process registration APIs.
 *
 * COMPILE:
 *   gcc -o example example_plugin_registration.c \
 *       -I../include \
 *       -L../lib -lrdkFwupdateMgr \
 *       $(pkg-config --cflags --libs glib-2.0 gio-2.0)
 *
 * RUN:
 *   # Ensure rdkFwupdateMgr daemon is running
 *   sudo systemctl start rdkFwupdateMgr.service
 *
 *   # Run example
 *   ./example
 *
 * EXPECTED OUTPUT:
 *   [Plugin] Registering with firmware daemon...
 *   [rdkFwupdateMgr] registerProcess() called
 *   [rdkFwupdateMgr]   processName: 'VideoPlayerPlugin'
 *   [rdkFwupdateMgr]   libVersion:  '2.0.1'
 *   [rdkFwupdateMgr] Registration successful
 *   [rdkFwupdateMgr]   handler_id: 12345
 *   [Plugin] Registration successful! Handle: 12345
 *   [Plugin] Now ready to call CheckForUpdate, DownloadFirmware, etc.
 *   [Plugin] Unregistering...
 *   [rdkFwupdateMgr] Unregistration successful
 *   [Plugin] Cleanup complete
 */

#include "rdkFwupdateMgr_process.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/**
 * EXAMPLE 1: Basic registration pattern (recommended)
 */
void example_basic_registration(void)
{
    printf("\n=== EXAMPLE 1: Basic Registration ===\n");

    FirmwareInterfaceHandle handle;

    // Register at plugin initialization
    printf("[Plugin] Registering with firmware daemon...\n");
    handle = registerProcess("VideoPlayerPlugin", "2.0.1");

    if (handle == FIRMWARE_INVALID_HANDLE) {
        fprintf(stderr, "[Plugin] ERROR: Registration failed!\n");
        fprintf(stderr, "[Plugin] Possible causes:\n");
        fprintf(stderr, "[Plugin]   - Daemon not running (check systemctl status rdkFwupdateMgr)\n");
        fprintf(stderr, "[Plugin]   - Process name already registered\n");
        fprintf(stderr, "[Plugin]   - D-Bus permission denied\n");
        return;
    }

    printf("[Plugin] Registration successful! Handle: %s\n", handle);
    printf("[Plugin] Now ready to call CheckForUpdate, DownloadFirmware, etc.\n");

    // Simulate plugin work
    printf("[Plugin] Doing plugin work...\n");
    sleep(1);

    // TODO: Use handle for firmware operations
    // checkForUpdate(handle, ...);
    // downloadFirmware(handle, ...);
    // updateFirmware(handle, ...);

    // Unregister at plugin shutdown
    printf("[Plugin] Unregistering...\n");
    unregisterProcess(handle);
    handle = NULL;  // Good practice: Mark as invalid after unregister

    printf("[Plugin] Cleanup complete\n");
}

/**
 * EXAMPLE 2: Error handling pattern
 */
void example_error_handling(void)
{
    printf("\n=== EXAMPLE 2: Error Handling ===\n");

    FirmwareInterfaceHandle handle;

    // Bad: NULL process name
    printf("[Test] Attempting registration with NULL processName...\n");
    handle = registerProcess(NULL, "1.0");
    if (handle == FIRMWARE_INVALID_HANDLE) {
        printf("[Test] ✓ Correctly rejected NULL processName\n");
    } else {
        printf("[Test] ✗ ERROR: Should have rejected NULL processName\n");
        unregisterProcess(handle);
    }

    // Bad: Empty process name
    printf("[Test] Attempting registration with empty processName...\n");
    handle = registerProcess("", "1.0");
    if (handle == FIRMWARE_INVALID_HANDLE) {
        printf("[Test] ✓ Correctly rejected empty processName\n");
    } else {
        printf("[Test] ✗ ERROR: Should have rejected empty processName\n");
        unregisterProcess(handle);
    }

    // Bad: NULL libVersion
    printf("[Test] Attempting registration with NULL libVersion...\n");
    handle = registerProcess("TestPlugin", NULL);
    if (handle == FIRMWARE_INVALID_HANDLE) {
        printf("[Test] ✓ Correctly rejected NULL libVersion\n");
    } else {
        printf("[Test] ✗ ERROR: Should have rejected NULL libVersion\n");
        unregisterProcess(handle);
    }

    // OK: Empty libVersion (allowed)
    printf("[Test] Attempting registration with empty libVersion...\n");
    handle = registerProcess("TestPlugin", "");
    if (handle != FIRMWARE_INVALID_HANDLE) {
        printf("[Test] ✓ Correctly accepted empty libVersion\n");
        unregisterProcess(handle);
    } else {
        printf("[Test] ✗ ERROR: Should have accepted empty libVersion\n");
    }

    printf("[Test] Error handling tests complete\n");
}

/**
 * EXAMPLE 3: Proper cleanup pattern (atexit handler)
 */
static FirmwareInterfaceHandle g_global_handle = FIRMWARE_INVALID_HANDLE;

void cleanup_handler(void)
{
    if (g_global_handle != FIRMWARE_INVALID_HANDLE) {
        printf("[Cleanup] Unregistering via atexit handler...\n");
        unregisterProcess(g_global_handle);
        g_global_handle = FIRMWARE_INVALID_HANDLE;
    }
}

void example_atexit_cleanup(void)
{
    printf("\n=== EXAMPLE 3: Cleanup via atexit ===\n");

    // Register cleanup handler
    atexit(cleanup_handler);

    // Register process
    printf("[Plugin] Registering...\n");
    g_global_handle = registerProcess("ATExitPlugin", "1.0");

    if (g_global_handle == FIRMWARE_INVALID_HANDLE) {
        fprintf(stderr, "[Plugin] Registration failed\n");
        return;
    }

    printf("[Plugin] Registration successful: %s\n", g_global_handle);
    printf("[Plugin] atexit handler will clean up automatically\n");

    // Note: cleanup_handler() will be called automatically at exit
}

/**
 * EXAMPLE 4: Multiple registrations (different process names)
 */
void example_multiple_registrations(void)
{
    printf("\n=== EXAMPLE 4: Multiple Registrations ===\n");

    FirmwareInterfaceHandle handle1, handle2;

    // Register first process
    printf("[Test] Registering VideoService...\n");
    handle1 = registerProcess("VideoService", "1.0");
    if (handle1 == FIRMWARE_INVALID_HANDLE) {
        fprintf(stderr, "[Test] VideoService registration failed\n");
        return;
    }
    printf("[Test] VideoService registered: %s\n", handle1);

    // Register second process (different name)
    printf("[Test] Registering AudioService...\n");
    handle2 = registerProcess("AudioService", "2.0");
    if (handle2 == FIRMWARE_INVALID_HANDLE) {
        fprintf(stderr, "[Test] AudioService registration failed\n");
        unregisterProcess(handle1);
        return;
    }
    printf("[Test] AudioService registered: %s\n", handle2);

    printf("[Test] Both services registered successfully\n");

    // Cleanup
    printf("[Test] Unregistering both services...\n");
    unregisterProcess(handle1);
    unregisterProcess(handle2);
    printf("[Test] Cleanup complete\n");
}

/**
 * EXAMPLE 5: Idempotent unregister (safe to call multiple times)
 */
void example_idempotent_unregister(void)
{
    printf("\n=== EXAMPLE 5: Idempotent Unregister ===\n");

    FirmwareInterfaceHandle handle;

    printf("[Test] Registering TestPlugin...\n");
    handle = registerProcess("TestPlugin", "1.0");
    if (handle == FIRMWARE_INVALID_HANDLE) {
        fprintf(stderr, "[Test] Registration failed\n");
        return;
    }

    printf("[Test] Registered: %s\n", handle);

    // Unregister multiple times (should be safe)
    printf("[Test] Unregistering (first time)...\n");
    unregisterProcess(handle);

    printf("[Test] Unregistering (second time - should be no-op)...\n");
    unregisterProcess(handle);  // Safe: no double-free, no crash

    printf("[Test] Unregistering NULL (should be no-op)...\n");
    unregisterProcess(NULL);  // Safe: no-op

    printf("[Test] Idempotent unregister test complete\n");
}

/**
 * Main entry point
 */
int main(int argc, char *argv[])
{
    printf("=================================================\n");
    printf("Firmware Update Process Registration Examples\n");
    printf("=================================================\n");

    // Run all examples
    example_basic_registration();
    example_error_handling();
    example_multiple_registrations();
    example_idempotent_unregister();
    example_atexit_cleanup();

    printf("\n=================================================\n");
    printf("All examples complete\n");
    printf("=================================================\n");

    return 0;
}
