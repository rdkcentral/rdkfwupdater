#ifndef DBUS_SERVER_H
#define DBUS_SERVER_H

#include <glib.h>
#include <gio/gio.h>
#include "rdkFwupdateMgr_handlers.h"  // For CheckForUpdateResult enum

//structure to track registered processes
typedef struct {
    guint64 handler_id;
    gchar *process_name;
    gchar *lib_version;
    gchar *sender_id;
    gint64 registration_time;
} ProcessInfo;

// Task context for async operations
// Task type enumeration for union-based polymorphism
typedef enum {
    TASK_TYPE_CHECK_UPDATE,
    TASK_TYPE_DOWNLOAD,
    TASK_TYPE_UPDATE,
    TASK_TYPE_REGISTER,  // For future use
    TASK_TYPE_UNREGISTER
} TaskType;

// Union-based TaskContext for memory efficiency and type safety
typedef struct {
    // Common fields for all task types
    TaskType type;                      // Task type identifier
    guint32 padding;                    // Alignment padding
    gchar *process_name;                // From handler_process_name
    gchar *sender_id;                   // D-Bus sender ID
    GDBusMethodInvocation *invocation;  // Used to send response back to app
    
    // Method-specific data (union - only one is active at runtime)
    union {
        // CheckUpdate-specific fields
        struct {
            gchar *client_fwdata_version;       // fwdata_version from client
            gchar *client_fwdata_availableVersion; // fwdata_availableVersion (usually empty)
            gchar *client_fwdata_updateDetails;    // fwdata_updateDetails (usually empty)
            gchar *client_fwdata_status;           // fwdata_status from client
            CheckForUpdateResult result_code;      // Result status from CheckForUpdate call
        } check_update;
        
        // Download-specific fields
        struct {
            gchar *image_to_download;    // Image name to download
            gchar *download_url;         // Download URL (if different from image name)
            guint64 file_size;          // Expected file size
            guint32 progress_percent;    // Download progress
        } download;
        
        // Update/Flash-specific fields
        struct {
            gchar *firmware_path;        // Path to firmware file
            gboolean immediate_reboot;   // Whether to reboot immediately after flash
            guint32 flash_progress;      // Flash progress percentage
            guint32 reserved;           // Reserved for future use
        } update;
    } data;
} TaskContext;

// Task data structures for async operations
typedef struct {
    guint update_task_id;
    TaskContext *CheckupdateTask_ctx;  // Points to TaskContext with type TASK_TYPE_CHECK_UPDATE
} CheckUpdate_TaskData;

typedef struct {
    guint download_task_id;
    TaskContext *DownloadFWTask_ctx;   // Points to TaskContext with type TASK_TYPE_DOWNLOAD
    gchar* ImageToDownload;            // TODO: Move this into TaskContext.data.download
} DownloadFW_TaskData;

/*D-Bus service information*/
#define BUS_NAME "org.rdkfwupdater.Service"
#define OBJECT_PATH "/org/rdkfwupdater/Service"
#define INTERFACE_NAME "org.rdkfwupdater.Interface"
extern GMainLoop *main_loop;
extern GHashTable *active_tasks;

extern  void cleanup_process_tracking();
extern  void init_task_system();
extern int setup_dbus_server();
extern void cleanup_dbus();

#endif
