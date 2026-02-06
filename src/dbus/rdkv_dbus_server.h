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
/*
 * RDK Firmware Update Manager - D-Bus Server Interface
 * 
 * This header defines the D-Bus server infrastructure that exposes firmware
 * update operations to client applications via the system D-Bus.
 * 
 * Architecture:
 * - Manages client process registrations and lifecycle tracking
 * - Handles async task execution using GLib GTask framework
 * - Provides signal-based notifications for long-running operations
 * - Maintains task context for proper cleanup and error handling
 */

#ifndef DBUS_SERVER_H
#define DBUS_SERVER_H

#include <glib.h>
#include <gio/gio.h>
#include "rdkFwupdateMgr_handlers.h"  // For CheckForUpdateResult enum

/*
 * Process Registration Information
 * 
 * Tracks each registered client process for access control and lifecycle management.
 * Stored in GHashTable keyed by handler_id for O(1) lookup during D-Bus method calls.
 */
typedef struct {
    guint64 handler_id;           // Unique ID assigned during registration
    gchar *process_name;          // Client-provided process identifier (e.g., "VideoApp")
    gchar *lib_version;           // Client library version for compatibility tracking
    gchar *sender_id;             // D-Bus unique name (e.g., ":1.42") for disconnect detection
    gint64 registration_time;     // Registration timestamp (for debugging/monitoring)
} ProcessInfo;

/*
 * Async Task Type Enumeration
 * 
 * Identifies the type of async operation in progress. Used to interpret
 * the TaskContext union correctly and route completion handlers.
 */
typedef enum {
    TASK_TYPE_CHECK_UPDATE,   // CheckForUpdate async XConf fetch
    TASK_TYPE_DOWNLOAD,       // Firmware download (future)
    TASK_TYPE_UPDATE,         // Firmware flash/install (future)
    TASK_TYPE_REGISTER,       // Process registration (currently synchronous)
    TASK_TYPE_UNREGISTER      // Process unregistration (currently synchronous)
} TaskType;

/*
 * Firmware Download Status Enumeration
 * 
 * Matches the client library fwdwnlstatus enum for consistency
 */
typedef enum {
    FW_DWNL_NOTSTARTED = 0,    // Download accepted but not yet started
    FW_DWNL_INPROGRESS = 1,    // Download in progress (0-99%)
    FW_DWNL_COMPLETED = 2,     // Download completed successfully (100%)
    FW_DWNL_ERROR = 3          // Download failed
} FwDwnlStatus;

/*
 * Firmware Flash Status Enumeration
 *
 * Matches the client library fwupdatestatus enum for consistency
 */
typedef enum {
    FW_UPDATE_INPROGRESS = 0,    // Flash in progress (0-99%)
    FW_UPDATE_COMPLETED = 1,     // Flash completed successfully (100%)
    FW_UPDATE_ERROR = 2          // Flash failed
} FwUpdateStatus;

/*
 * ===================================================================
 * DownloadFirmware Async Implementation Structures
 * ===================================================================
 * These structures support the GTask-based async download architecture
 * with progress callback integration and multi-client piggybacking.
 * 
 * Design Pattern:
 * 1. AsyncDownloadContext: Passed to worker thread via GTask
 * 2. ProgressUpdate: Passed to main loop via g_idle_add for signal emission
 * 3. DownloadState: Global tracking for current download with waiting clients
 * 
 * Thread Safety:
 * - AsyncDownloadContext: Immutable once passed to worker (safe)
 * - ProgressUpdate: Created per signal, freed after emission (safe)
 * - DownloadState: Protected by main loop execution (no mutex needed)
 */

/*
 * AsyncDownloadContext
 * 
 * Context data passed to worker thread for async firmware download.
 * Contains all information needed to perform download without accessing
 * shared state (thread-safe by immutability).
 * 
 * Lifecycle:
 * 1. Created in D-Bus handler (main loop thread)
 * 2. Passed to GTask via g_task_set_task_data()
 * 3. Used by worker thread in async_download_task()
 * 4. Freed in async_download_complete() callback (main loop thread)
 * 
 * Memory: ~256 bytes (4 strings + connection pointer)
 */
typedef struct {
    gchar *handler_id;              // Handler ID of requesting client (for logging)
    gchar *firmware_name;           // Firmware filename (e.g., "image_v2.bin")
    gchar *download_url;            // Full download URL (from XConf or custom)
    gchar *type_of_firmware;        // Firmware type: "PCI", "PDRI", "PERIPHERAL"
    GDBusConnection *connection;    // D-Bus connection for signal emission (borrowed, not owned)
} AsyncDownloadContext;

/*
 * ProgressUpdate
 * 
 * Progress data passed from worker thread to main loop via g_idle_add().
 * Contains all data needed to emit a single DownloadProgress D-Bus signal.
 * 
 * Usage Pattern:
 * Worker thread:
 *   ProgressUpdate *update = g_new0(ProgressUpdate, 1);
 *   update->progress = 50;
 *   update->status = FW_DWNL_INPROGRESS;
 *   update->handler_id = g_strdup(ctx->handler_id);
 *   update->firmware_name = g_strdup(ctx->firmware_name);
 *   update->connection = ctx->connection;
 *   g_idle_add(emit_download_progress_signal, update);
 * 
 * Main loop:
 *   emit_download_progress_signal() emits signal, then frees all fields
 * 
 * Memory: ~128 bytes (2 strings + int + enum + pointer)
 */
typedef struct {
    int progress;                   // Progress percentage (0-100, -1 for error)
    FwDwnlStatus status;            // Download status (INPROGRESS, COMPLETED, ERROR)
    gchar *handler_id;              // Handler ID for logging (optional, can be NULL)
    gchar *firmware_name;           // Firmware name for signal payload
    GDBusConnection *connection;    // D-Bus connection for signal emission (borrowed)
} ProgressUpdate;

/*
 * CurrentDownloadState
 * 
 * Global state tracker for the currently active download operation.
 * Enables multi-client piggybacking - subsequent clients for the same
 * firmware join the existing download instead of starting a new one.
 * 
 * Access Pattern:
 * - Only accessed from main loop thread (D-Bus handlers and idle callbacks)
 * - No mutex needed due to GLib main loop serialization guarantees
 * 
 * Lifecycle:
 * 1. Created when first client initiates download
 * 2. Updated by progress callbacks (via g_idle_add)
 * 3. Queried by subsequent clients (piggyback check)
 * 4. Destroyed in async_download_complete() callback
 * 
 * Memory: ~256 bytes (2 strings + int + enum + GSList)
 */
typedef struct {
    gchar *firmware_name;           // Name of firmware being downloaded
    int current_progress;           // Latest progress percentage (0-100)
    FwDwnlStatus status;            // Current download status
    GSList *waiting_handler_ids;    // List of gchar* handler IDs waiting for this download
} CurrentDownloadState;

/* End of DownloadFirmware async structures */

/*
 * CurrentDownloadState
 *
 * Global state tracker for the currently active flash operation.
 * Enables flash status notifying  mechanism- subsequent clients' requests will get rejected with ongoing flash info.
 *
 * Access Pattern:
 * - Only accessed from main loop thread (D-Bus handlers and idle callbacks)
 * - No mutex needed due to GLib main loop serialization guarantees.
 *
 * Lifecycle:
 * 1. Created when first client initiates flash/update
 * 2. Updated by progress callbacks (via g_idle_add)
 * 3. Queried by subsequent clients (piggyback check) - library takes care of listening to this signal
 * 4. Destroyed in async_flash_complete() callback
 *
 * Memory: ~256 bytes (2 strings + int + enum + GSList)
 */
typedef struct {
    gchar *firmware_name;           // Name of firmware being flashed
    int current_progress;           // Latest progress percentage (0-100)
    FwUpdateStatus status;            // Current flash status
} CurrentFlashState;

/*
 * ===================================================================
 * UpdateFirmware Worker Thread Structures
 * ===================================================================
 * These structures support the GThread-based async flash architecture
 * with progress signal emission and flashImage() integration.
 * 
 * Design Pattern:
 * 1. AsyncFlashContext: Passed to worker thread via GThread
 * 2. FlashProgressUpdate: Passed to main loop via g_idle_add for signal emission
 * 3. CurrentFlashState: Global tracking for current flash operation
 * 
 * Thread Safety:
 * - AsyncFlashContext: Immutable once passed to worker (safe)
 * - FlashProgressUpdate: Created per signal, freed after emission (safe)
 * - CurrentFlashState: Protected by main loop execution (no mutex needed)
 */

/*
 * AsyncFlashContext
 * 
 * Context data passed to worker thread for async firmware flash operation.
 * Contains all information needed to perform flash without accessing
 * shared state (thread-safe by immutability).
 * 
 * Lifecycle:
 * 1. Created in D-Bus handler (main loop thread)
 * 2. Passed to GThread via g_thread_try_new()
 * 3. Used by worker thread in rdkfw_flash_worker_thread()
 * 4. Freed by worker thread before exit
 * 
 * Memory: ~512 bytes (strings + primitives)
 * 
 * All gchar* fields are OWNED (must call g_free when done)
 * GDBusConnection* is BORROWED (do NOT free)
 */
typedef struct {
    // D-Bus communication
    GDBusConnection *connection;        // D-Bus connection for signal emission (borrowed, NOT owned)
    gchar *handler_id;                  // Handler ID string (owned, must free)
    
    // Firmware identification
    gchar *firmware_name;               // Firmware filename (e.g., "image_v2.bin") (owned)
    gchar *firmware_type;               // Firmware type: "PCI", "PDRI", "PERIPHERAL" (owned)
    gchar *firmware_fullpath;           // Full path to firmware file (e.g., "/opt/CDL/firmware.bin") (owned)
    gchar *server_url;                  // Server URL for telemetry (owned )
    
    // Flash parameters
    gboolean immediate_reboot;          // TRUE = reboot after flash, FALSE = defer reboot
    int trigger_type;                   // Trigger type (1=bootup, 2=cron, 3=TR69, 4=app, 5=delayed, 6=red_state)
    // Progress tracking
    int last_progress;                  // Last emitted progress percentage (0-100)
    time_t operation_start_time;        // Flash start time for timeout detection
} AsyncFlashContext;

/*
 * FlashProgressUpdate
 * 
 * Progress data passed from worker thread to main loop via g_idle_add().
 * Contains all data needed to emit a single UpdateProgress D-Bus signal.
 * 
 * Usage Pattern:
 * Worker thread:
 *   FlashProgressUpdate *update = g_new0(FlashProgressUpdate, 1);
 *   update->progress = 50;
 *   update->status = FW_UPDATE_INPROGRESS;
 *   update->handler_id = g_strdup(ctx->handler_id);
 *   update->firmware_name = g_strdup(ctx->firmware_name);
 *   update->error_message = NULL;  // or g_strdup(error_desc) for errors
 *   update->connection = ctx->connection;
 *   g_idle_add(emit_flash_progress_idle, update);
 * 
 * Main loop (automatically invoked):
 *   emit_flash_progress_idle() emits signal, then frees all fields
 * 
 * Memory: ~192 bytes (3 strings + int + enum + pointer)
 * 
 * All gchar* fields are OWNED (freed in emit_flash_progress_idle)
 * GDBusConnection* is BORROWED (do NOT free)
 */
typedef struct {
    int progress;                       // Progress percentage (0-100, -1 for error)
    FwUpdateStatus status;              // Flash status: FW_UPDATE_INPROGRESS/COMPLETED/ERROR
    gchar *handler_id;                  // Handler ID string (owned, must free)
    gchar *firmware_name;               // Firmware name for signal payload (owned, must free)
    gchar *error_message;               // Error description if status==ERROR (owned, can be NULL)
    GDBusConnection *connection;        // D-Bus connection for signal emission (borrowed)
} FlashProgressUpdate;

/* End of UpdateFirmware async structures */

/*
 * Async Task Context
 * 
 * Maintains state for async operations (CheckForUpdate, Download, etc.).
 * Uses union-based design for memory efficiency - only one operation type
 * is active at any time, sharing the same memory space.
 * 
 * Design Rationale:
 * - Union saves memory compared to separate structs for each operation
 * - Type field ensures correct interpretation of union data
 * - invocation field enables sending D-Bus response when task completes
 * - Common fields (process_name, sender_id) available regardless of task type
 * 
 * Memory Layout: ~128 bytes total (common fields + largest union member)
 */
typedef struct {
    /* Common fields for all task types */
    TaskType type;                      // Identifies which union member is active
    guint32 padding;                    // Memory alignment (4-byte boundary)
    gchar *process_name;                // Client process name for logging
    gchar *sender_id;                   // D-Bus sender for response routing
    GDBusMethodInvocation *invocation;  // D-Bus method context for reply
    
    /* Task-specific data - only one active based on 'type' field */
    union {
        /* CheckForUpdate task data */
        struct {
            gchar *client_fwdata_version;          // Client's current firmware version
            gchar *client_fwdata_availableVersion; // Client's available version (usually NULL)
            gchar *client_fwdata_updateDetails;    // Client's update details (usually NULL)
            gchar *client_fwdata_status;           // Client's current status string
            CheckForUpdateStatus result_code;      // Firmware status code (FIRMWARE_AVAILABLE, NOT_AVAILABLE, etc.)
        } check_update;
        
        /* Download task data */
        struct {
            gchar *firmwareName;         // Firmware image filename
            gchar *downloadUrl;          // Custom URL or empty string (use XConf URL)
            gchar *typeOfFirmware;       // Firmware type: "PCI", "PDRI", "PERIPHERAL"
            guint32 progress;            // Current download progress (0-100, -1 for error)
            FwDwnlStatus status;         // Current download status
            gchar *errorMessage;         // Error description if status == FW_DWNL_ERROR
            gchar *localFilePath;        // Path where file is/will be saved
        } download;
        
        /* Firmware flash/install task data (future implementation) */
        struct {
            gchar *firmware_path;        // Local path to downloaded firmware
            gboolean immediate_reboot;   // TRUE if reboot required immediately
            guint32 flash_progress;      // Flash operation progress (0-100)
            guint32 reserved;            // Reserved for future flags/data
        } update;
    } data;
} TaskContext;

/*
 * CheckForUpdate Task Wrapper
 * 
 * Associates a GLib GTask ID with the CheckForUpdate operation context.
 * Stored in active_tasks hash table for tracking and cleanup.
 */
typedef struct {
    guint update_task_id;               // GLib GTask source ID (for cancellation)
    TaskContext *CheckupdateTask_ctx;   // Task context with type=TASK_TYPE_CHECK_UPDATE
} CheckUpdate_TaskData;

/*
 * Download Task Wrapper
 * 
 * Associates a GLib GTask ID with firmware download operation context.
 */
typedef struct {
    guint download_task_id;             // GLib GTask source ID
    TaskContext *DownloadFWTask_ctx;    // Task context with type=TASK_TYPE_DOWNLOAD
    gchar* firmwareName;                // Firmware filename
    gchar* downloadUrl;                 // Custom URL or empty string
    gchar* typeOfFirmware;              // Firmware type: "PCI", "PDRI", etc.
} DownloadFW_TaskData;

/*
 * Download State Tracker
 * 
 * Tracks a single active download operation that may have multiple
 * clients waiting for completion. Stored in active_download_tasks
 * hash table, keyed by firmwareName.
 */
typedef struct {
    gchar* firmwareName;               // Firmware being downloaded
    gchar* downloadUrl;                // URL being used (XConf or custom)
    gchar* localFilePath;              // Destination file path
    guint progress;                    // Current progress (0-100)
    FwDwnlStatus status;               // Current status
    GThread* worker_thread;            // Background worker thread
    GSList* waiting_handlers;          // List of guint64 handler_ids waiting for this download
    gchar* errorMessage;               // Error description if status == FW_DWNL_ERROR
} DownloadState;

/*
 * D-Bus Service Configuration
 * 
 * Well-known D-Bus names for the RDK Firmware Update Manager service.
 * Clients use these to connect and invoke methods.
 */
#define BUS_NAME "org.rdkfwupdater.Service"          // D-Bus service name
#define OBJECT_PATH "/org/rdkfwupdater/Service"      // D-Bus object path
#define INTERFACE_NAME "org.rdkfwupdater.Interface"  // D-Bus interface name

/*
 * Global State
 * 
 * These are initialized in rdkv_dbus_server.c and used throughout
 * the D-Bus server implementation.
 */
extern GMainLoop *main_loop;                    // GLib event loop for async operations
extern GHashTable *active_tasks;                // Active async tasks (keyed by task_id)
extern GHashTable *active_download_tasks;       // Active downloads (keyed by firmwareName)
extern GHashTable *registered_processes;        // Registered clients (keyed by handler_id)

/*
 * Initialize Process Tracking System
 * 
 * Sets up the process registry hash table for tracking registered clients.
 * Called during daemon startup before D-Bus server initialization.
 */
extern void init_task_system();

/*
 * Cleanup Process Tracking
 * 
 * Frees all registered process information and destroys the registry.
 * Called during daemon shutdown or before exit.
 */
extern void cleanup_process_tracking();

/*
 * Setup D-Bus Server
 * 
 * Initializes the D-Bus connection, registers methods/signals, and starts
 * listening for client connections.
 * 
 * Returns:
 *   0 on success, -1 on failure (e.g., name already taken, permission denied)
 */
extern int setup_dbus_server();

/*
 * Cleanup D-Bus Server
 * 
 * Unregisters from D-Bus, closes connections, and frees resources.
 * Called during daemon shutdown.
 */
extern void cleanup_dbus();

/* Global state variables - accessed by worker threads */
extern CurrentFlashState *current_flash;
extern gboolean IsFlashInProgress;

/* Helper functions for worker threads */
extern gboolean emit_flash_progress_idle(gpointer user_data);
extern gboolean cleanup_flash_state_idle(gpointer user_data);

#endif
