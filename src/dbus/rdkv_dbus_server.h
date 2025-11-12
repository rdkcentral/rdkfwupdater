#ifndef DBUS_SERVER_H
#define DBUS_SERVER_H

#include <glib.h>
#include <gio/gio.h>

//structure to track registered processes
typedef struct {
    guint64 handler_id;
    gchar *process_name;
    gchar *lib_version;
    gchar *sender_id;
    gint64 registration_time;
} ProcessInfo;

// Task context for async operations
typedef struct {
    gchar *process_name;           // App id (the registration id returned to app while registration)
    gchar *sender_id;             // D-Bus sender ID ( in string format ":1.50")
    //gchar *CurrImageVersion;      // will be used in checkUpdate,DownloadFirmwar and UpgradeFirmware methods
    //gchar *NextImageVersion;      //get used in UpgradeFirmware
    GDBusMethodInvocation *invocation; // used to send Response back to app
   // gint64 start_time;            // task starting time
} TaskContext;

typedef struct {
        guint update_task_id;
        TaskContext *CheckupdateTask_ctx;
}CheckUpdate_TaskData;

typedef struct {
        guint download_task_id;
        TaskContext *DownloadFWTask_ctx;
        gchar* ImageToDownload;
}DownloadFW_TaskData;

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
