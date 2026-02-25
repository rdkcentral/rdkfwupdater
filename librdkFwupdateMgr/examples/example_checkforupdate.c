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
 * @file example_checkforupdate.c
 * @brief Example demonstrating checkForUpdate API usage
 *
 * This example shows how to:
 * 1. Register with the firmware daemon
 * 2. Check for available firmware updates
 * 3. Handle the update callback
 * 4. Unregister from the daemon
 *
 * COMPILE:
 *   gcc -o example_checkforupdate example_checkforupdate.c \
 *       -I../include \
 *       -L../../.libs -lrdkFwupdateMgr \
 *       $(pkg-config --cflags --libs glib-2.0 gio-2.0) \
 *       -lpthread
 *
 * RUN:
 *   # Ensure rdkFwupdateMgr daemon is running
 *   sudo systemctl start rdkFwupdateMgr.service
 *
 *   # Set library path
 *   export LD_LIBRARY_PATH=../../.libs:$LD_LIBRARY_PATH
 *
 *   # Run example
 *   ./example_checkforupdate
 *
 * EXPECTED OUTPUT:
 *   [Example] Registering with firmware daemon...
 *   [Example] Registration successful! Handle: 12345
 *   [Example] Checking for firmware updates...
 *   [Example] checkForUpdate() returned: SUCCESS
 *   [UpdateCallback] Firmware update check completed!
 *   [UpdateCallback]   Current Version: X1-SCXI11AIS-2023.01.01
 *   [UpdateCallback]   Status: FIRMWARE_AVAILABLE (0)
 *   [UpdateCallback]   Available Version: X1-SCXI11AIS-2023.02.01
 *   [UpdateCallback]   Firmware Filename: firmware_v2.bin
 *   [UpdateCallback]   Download URL: http://server.com/fw/firmware_v2.bin
 *   [Example] Unregistering...
 *   [Example] Cleanup complete
 */

#include "rdkFwupdateMgr_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Global flag to track callback completion
static volatile int g_callback_received = 0;

/**
 * Status code to string converter
 */
const char* status_to_string(CheckForUpdateStatus status) {
    switch (status) {
        case FIRMWARE_AVAILABLE:     return "FIRMWARE_AVAILABLE";
        case FIRMWARE_NOT_AVAILABLE: return "FIRMWARE_NOT_AVAILABLE";
        case UPDATE_NOT_ALLOWED:     return "UPDATE_NOT_ALLOWED";
        case FIRMWARE_CHECK_ERROR:   return "FIRMWARE_CHECK_ERROR";
        case IGNORE_OPTOUT:          return "IGNORE_OPTOUT";
        case BYPASS_OPTOUT:          return "BYPASS_OPTOUT";
        default:                     return "UNKNOWN";
    }
}

/**
 * Update event callback - invoked when checkForUpdate completes
 */
void on_update_event(const FwInfoData *fwinfodata) {
    printf("\n[UpdateCallback] ========================================\n");
    printf("[UpdateCallback] Firmware update check completed!\n");
    printf("[UpdateCallback] ========================================\n");

    if (!fwinfodata) {
        printf("[UpdateCallback] ERROR: fwinfodata is NULL!\n");
        g_callback_received = 1;
        return;
    }

    // Print current firmware version
    printf("[UpdateCallback]   Current Version: %s\n", 
           fwinfodata->CurrFWVersion[0] ? fwinfodata->CurrFWVersion : "(empty)");

    // Print status
    printf("[UpdateCallback]   Status: %s (%d)\n", 
           status_to_string(fwinfodata->status),
           fwinfodata->status);

    // Print update details if available
    if (fwinfodata->UpdateDetails) {
        UpdateDetails *details = fwinfodata->UpdateDetails;
        
        printf("[UpdateCallback]   Update Details:\n");
        
        if (details->FwVersion[0]) {
            printf("[UpdateCallback]     - Available Version: %s\n", details->FwVersion);
        }
        
        if (details->FwFileName[0]) {
            printf("[UpdateCallback]     - Firmware Filename: %s\n", details->FwFileName);
        }
        
        if (details->FwUrl[0]) {
            printf("[UpdateCallback]     - Download URL: %s\n", details->FwUrl);
        }
        
        if (details->RebootImmediately[0]) {
            printf("[UpdateCallback]     - Reboot Immediately: %s\n", details->RebootImmediately);
        }
        
        if (details->DelayDownload[0]) {
            printf("[UpdateCallback]     - Delay Download: %s\n", details->DelayDownload);
        }
        
        if (details->PDRIVersion[0]) {
            printf("[UpdateCallback]     - PDRI Version: %s\n", details->PDRIVersion);
        }
        
        if (details->PeripheralFirmwares[0]) {
            printf("[UpdateCallback]     - Peripheral Firmwares: %s\n", details->PeripheralFirmwares);
        }
    } else {
        printf("[UpdateCallback]   Update Details: (none)\n");
    }

    // Provide user guidance based on status
    printf("[UpdateCallback] \n");
    printf("[UpdateCallback] Next Steps:\n");
    switch (fwinfodata->status) {
        case FIRMWARE_AVAILABLE:
            printf("[UpdateCallback]   --> New firmware is available!\n");
            printf("[UpdateCallback]   --> You can now call downloadFirmware() to download it\n");
            printf("[UpdateCallback]   --> Then call updateFirmware() to install it\n");
            break;
        case FIRMWARE_NOT_AVAILABLE:
            printf("[UpdateCallback]   --> You're already on the latest firmware version\n");
            printf("[UpdateCallback]   --> No action needed\n");
            break;
        case UPDATE_NOT_ALLOWED:
            printf("[UpdateCallback]   --> Firmware update not allowed for this device\n");
            printf("[UpdateCallback]   --> Check device model compatibility\n");
            break;
        case FIRMWARE_CHECK_ERROR:
            printf("[UpdateCallback]   --> Error occurred while checking for updates\n");
            printf("[UpdateCallback]   --> Check daemon logs for details\n");
            break;
        case IGNORE_OPTOUT:
        case BYPASS_OPTOUT:
            printf("[UpdateCallback]   --> Firmware download is blocked (opt-out)\n");
            printf("[UpdateCallback]   --> User consent required before proceeding\n");
            break;
        default:
            printf("[UpdateCallback]   --> Unknown status\n");
            break;
    }

    printf("[UpdateCallback] ========================================\n\n");
    
    g_callback_received = 1;
}

/**
 * Main example function
 */
int main(int argc, char *argv[]) {
    printf("\n=======================================================\n");
    printf("  CheckForUpdate API Example\n");
    printf("=======================================================\n\n");

    FirmwareInterfaceHandle handle;
    CheckForUpdateResult result;

    // Step 1: Register with firmware daemon
    printf("[Example] Step 1: Registering with firmware daemon...\n");
    handle = registerProcess("ExampleCheckForUpdate", "1.0.0");

    if (handle == FIRMWARE_INVALID_HANDLE) {
        fprintf(stderr, "[Example] ERROR: Registration failed!\n");
        fprintf(stderr, "[Example] Possible causes:\n");
        fprintf(stderr, "[Example]   - Daemon not running (check: systemctl status rdkFwupdateMgr)\n");
        fprintf(stderr, "[Example]   - D-Bus permission denied\n");
        fprintf(stderr, "[Example]   - Process name already registered\n");
        return 1;
    }

    printf("[Example] Registration successful! Handle: %s\n\n", handle);

    // Step 2: Check for firmware updates
    printf("[Example] Step 2: Checking for firmware updates...\n");
    result = checkForUpdate(handle, on_update_event);

    if (result != CHECK_FOR_UPDATE_SUCCESS) {
        fprintf(stderr, "[Example] ERROR: checkForUpdate() failed!\n");
        fprintf(stderr, "[Example] Possible causes:\n");
        fprintf(stderr, "[Example]   - Invalid handle\n");
        fprintf(stderr, "[Example]   - Daemon communication error\n");
        fprintf(stderr, "[Example]   - NULL callback\n");
        
        // Still unregister even on failure
        unregisterProcess(handle);
        return 1;
    }

    printf("[Example] checkForUpdate() returned: SUCCESS\n");
    printf("[Example] Waiting for callback...\n\n");

    // Wait for callback to be invoked (timeout after 5 seconds)
    int timeout_sec = 5;
    int elapsed_sec = 0;
    while (!g_callback_received && elapsed_sec < timeout_sec) {
        sleep(1);
        elapsed_sec++;
        if (!g_callback_received) {
            printf("[Example] Still waiting for callback... (%d/%d sec)\n", elapsed_sec, timeout_sec);
        }
    }

    if (!g_callback_received) {
        fprintf(stderr, "[Example] WARNING: Callback not received within %d seconds!\n", timeout_sec);
        fprintf(stderr, "[Example] This might indicate an async operation in progress.\n");
    }

    // Step 3: Unregister from daemon
    printf("\n[Example] Step 3: Unregistering from daemon...\n");
    unregisterProcess(handle);
    handle = NULL;  // Good practice: Mark as invalid

    printf("[Example] Cleanup complete\n");
    printf("\n=======================================================\n");
    printf("  Example completed successfully\n");
    printf("=======================================================\n\n");

    return 0;
}
