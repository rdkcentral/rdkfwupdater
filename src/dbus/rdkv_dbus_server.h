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
            CheckForUpdateResult result_code;      // Result of update check
        } check_update;
        
        /* Download task data (future implementation) */
        struct {
            gchar *image_to_download;    // Firmware image filename
            gchar *download_url;         // Full URL if different from filename
            guint64 file_size;           // Expected download size in bytes
            guint32 progress_percent;    // Current download progress (0-100)
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
 * Download Task Wrapper (Future Implementation)
 * 
 * Associates a GLib GTask ID with firmware download operation context.
 * NOTE: ImageToDownload should be moved into TaskContext.data.download
 */
typedef struct {
    guint download_task_id;             // GLib GTask source ID
    TaskContext *DownloadFWTask_ctx;    // Task context with type=TASK_TYPE_DOWNLOAD
    gchar* ImageToDownload;             // Firmware image name (TODO: refactor)
} DownloadFW_TaskData;

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
extern GMainLoop *main_loop;      // GLib event loop for async operations
extern GHashTable *active_tasks;  // Active async tasks (keyed by task_id)

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

#endif
