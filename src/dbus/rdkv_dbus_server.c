#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include "rdkv_dbus_server.h"
#include "rdkv_cdl_log_wrapper.h"
static gboolean IsCheckUpdateInProgress = FALSE; 
static gboolean IsDownloadInProgress = FALSE; 
//static gboolean IsUpgradeInProgress = FALSE; // will be used based once the shared lib is ready.commented out to pass jenkins build
static GSList *waiting_checkUpdate_ids = NULL;  //  List of task IDs waiting for CheckUpdate
static GSList *waiting_download_ids = NULL; // List of task IDs waiting for download
//static GSList *waiting_upgrade_ids = NULL; //  List of task IDs waiting for Upgrade

static guint owner_id = 0;
static guint64 next_process_id = 1;

/*Vars for Dbus*/
static GDBusConnection *connection = NULL;
GMainLoop *main_loop = NULL;
static guint registration_id = 0;

/* Vars for Task tracking system*/
GHashTable *active_tasks = NULL;      // hash table to track running async tasks (task_id -> TaskContext)
static guint next_task_id = 1;              // Unique task IDs

/*process tracking*/
static GHashTable *registered_processes = NULL;  // handler_id -> ProcessInfo;

/* D-Bus introspection data or dbus interface : Exposes the methods for apps */
static const gchar introspection_xml[] =
"<node>"
"  <interface name='org.rdkfwupgrader.Interface'>"
"    <method name='CheckForUpdate'>"
"      <arg type='s' name='handler' direction='in'/>"
"      <arg type='s' name='version' direction='in'/>"
"      <arg type='s' name='AvailableVersion' direction='out'/>"
"      <arg type='s' name='IsXconfComSuccess' direction='out'/>"
"    </method>"
"    <method name='DownloadFirmware'>"
"      <arg type='s' name='handler' direction='in'/>" //this is a struct as per requirement, for now taking it as string.  handler argument will be the process_name in dbus_handlers
"      <arg type='s' name='ImageToDownload' direction='in'/>"
"      <arg type='s' name='DownloadedImageVersion' direction='out'/>"  // just send out the success message once the download is triggered; will modify it later to send updates in parallel.Need to add one more output arg here
"      <arg type='s' name='downloadPath' direction='out'/>"
"    </method>"
"    <method name='UpdateFirmware'>"
"      <arg type='s' name='hanlder' direction='in'/>"
"      <arg type='s' name='currFWVersion' direction='in'/>"
"      <arg type='s' name='availableVersion' direction='in'/>"
"      <arg type='s' name='option1' direction='in'/>" // this will be part of UpdateDetails object in FwData ; for now hardcoding to 0and1 and not taking statusOfFw as input yet
"      <arg type='s' name='option2' direction='in'/>"
"      <arg type='b' name='success' direction='out'/>" //send out success to app once the Upgrade fucntion is called. eventually system reboots.
"      <arg type='s' name='Message' direction='out'/>" // some intimation message
"    </method>"
"    <method name='RegisterProcess'>"
"      <arg type='s' name='handler' direction='in'/>" //the process name it is
"      <arg type='s' name='libVersion' direction='in'/>"
"      <arg type='t' name='handler_id' direction='out'/>" //handler type  sent to app and app stores it
"    </method>"
"    <method name='UnregisterProcess'>"
"      <arg type='t' name='handler' direction='in'/>" //handler type will be changed to struct a<s> ; for now it is 
"      <arg type='b' name='success' direction='out'/>"
"    </method>"
"  </interface>"
"</node>";    

static void process_app_request(GDBusConnection *rdkv_conn_dbus,
                             const gchar *rdkv_req_caller_id,
                             const gchar *rdkv_req_obj_path,
                             const gchar *rdkv_req_iface_name,
                             const gchar *rdkv_req_method,
                             GVariant *rdkv_req_payload,
                             GDBusMethodInvocation *rdkv_resp_ctx,
                             gpointer rdkv_user_ctx);

/*Initialize process tracking*/
static void init_process_tracking()
{
    /* Maps handler IDs to process information , so that processes that are registered for updates can be tracked.*/
    registered_processes = g_hash_table_new_full(g_direct_hash, g_direct_equal,NULL, (GDestroyNotify)g_free);
    SWLOG_INFO("[TRACKING] process tracking initialized\n");
}

/* Add process to list for tracking*/
static guint64 add_process_to_tracking(const gchar *process_name,
                                      const gchar *lib_version,
                                      const gchar *sender_id)
{
	ProcessInfo *info = g_malloc0(sizeof(ProcessInfo));
	info->handler_id=next_process_id++;
	info->process_name = g_strdup(process_name);
	info->lib_version = g_strdup(lib_version);
	info->sender_id = g_strdup(sender_id);
	info->registration_time = g_get_monotonic_time(); //no significance in current design; might be useful in future for debug purposes
	guint64 key = info->handler_id;
	SWLOG_INFO("KEY: %"G_GUINT64_FORMAT"\n",info->handler_id);
	g_hash_table_insert(registered_processes, GINT_TO_POINTER(key), info);
	SWLOG_INFO("[PROCESS_TRACKING] Added: %s (handler: %"G_GUINT64_FORMAT", sender: %s)\n",process_name, info->handler_id, sender_id);
	SWLOG_INFO("[PROCESS_TRACKING] Total registered: %d\n", g_hash_table_size(registered_processes));
	return info->handler_id;
}

/* Remove process from tracking list */
static gboolean remove_process_from_tracking(guint64 handler_id)
{
	ProcessInfo *info = g_hash_table_lookup(registered_processes, &handler_id);
	if (!info) {
		SWLOG_INFO("[PROCESS_TRACKING] Handler %"G_GUINT64_FORMAT" not found\n", handler_id);
		return FALSE;
	}
	SWLOG_INFO("[PROCESS_TRACKING] Removing: %s (handler: %"G_GUINT64_FORMAT")\n",info->process_name, handler_id);
	g_hash_table_remove(registered_processes, &handler_id);
	SWLOG_INFO("[PROCESS_TRACKING] Total registered: %d\n", g_hash_table_size(registered_processes));
	return TRUE;
}                                                                                                                                                                                                                  
/* Free process tracking resources */
void cleanup_process_tracking()
{
	if (registered_processes) {
		SWLOG_INFO("[TRACKING] Cleaning up %d registered processes\n",g_hash_table_size(registered_processes));
		g_hash_table_destroy(registered_processes);
		registered_processes = NULL;
	}
}

/* Initializes the async task tracking system */
void init_task_system()
{
	active_tasks = g_hash_table_new_full(g_direct_hash, g_direct_equal,NULL, (GDestroyNotify)g_free);
	SWLOG_INFO("[TASK-SYSTEM] Initialized task tracking system\n");
	// initialize process tracking
	init_process_tracking();
}

// Create context for each app/procces's request - for sending back the intermediate/final responses to apps
static TaskContext* create_task_context(const gchar* app_id,                                                                                                                 const gchar* sender_id,                                                                                                              GDBusMethodInvocation *invocation)
{
	TaskContext *ctx = g_malloc0(sizeof(TaskContext));
	ctx->process_name = g_strdup(app_id);
	ctx->sender_id = g_strdup(sender_id);
	ctx->invocation = invocation;
	SWLOG_INFO("Created task context\n");
	return ctx;
}

/* Free task context when done*/
static void free_task_context(TaskContext *ctx)
{
	if (!ctx) return;
	g_free(ctx->process_name);
	g_free(ctx->sender_id);
	g_free(ctx);
}

/* Description: send the xconf server response to apps and clear the task from task tracking system */
void complete_CheckUpdate_waiting_tasks(const gchar *availableVersion, const gchar *successMsg, TaskContext *ctx) 
{ 
	// in the arguments Message string will be XConf server resp
	SWLOG_INFO("Completing %d waiting CheckUpdate tasks\n",g_slist_length(waiting_checkUpdate_ids));
	// Iterate through each task_id in waiting_checkUpdate_ids
	GSList *current = waiting_checkUpdate_ids;
	while (current != NULL) {
		guint task_id = GPOINTER_TO_UINT(current->data);
		SWLOG_INFO("current task Id %d will get cleared after sending response to the app\n", task_id);
		if (active_tasks == NULL) {
			SWLOG_INFO("ERROR: tasks table is NULL\n");
			return;
		}
		// Lookup task_id in active_task
		//TaskContext *context = g_hash_table_lookup(active_tasks, GUINT_TO_POINTER(task_id));
		TaskContext *context = g_hash_table_lookup(active_tasks, GUINT_TO_POINTER(task_id));
		if (context != NULL) {
			SWLOG_INFO("[Waiting task_id in -%d] Sending response to app_id : %s\n",task_id, context->process_name);
			// Send D-Bus response
			g_dbus_method_invocation_return_value(context->invocation,g_variant_new("(ss)", availableVersion, successMsg));
			// Remove task_id from active_tasks
			g_hash_table_remove(active_tasks, GUINT_TO_POINTER(task_id));
		} else {
			SWLOG_INFO("Task-%d not found in active_tasks\n", task_id);
		}
		current = current->next;
	}
	// Clear waiting_CheckUpdatr_ids list
	g_slist_free(waiting_checkUpdate_ids);
	waiting_checkUpdate_ids = NULL;
	// Set IsCheckUpdateInProgress = FALSE
	IsCheckUpdateInProgress = FALSE;
	SWLOG_INFO("All CheckUpdate waiting tasks completed !!\n");
}
/* Description: send the Download progress response to apps and clear the task from task tracking system */                          void complete_Download_waiting_tasks(const gchar *ImageDownloaded, const gchar *DLpath, TaskContext *ctx) {
	SWLOG_INFO("Completing %d waiting DownloadFW tasks\n",g_slist_length(waiting_download_ids));
	// Iterate through each task_id in waiting_downlaod_ids
	GSList *current = waiting_download_ids;
	while (current != NULL) {
		guint task_id = GPOINTER_TO_UINT(current->data);
		SWLOG_INFO("current task Id in waiting list: %d will get cleared after sending response to the app\n", task_id);
		if (active_tasks == NULL) {
			SWLOG_INFO("ERROR: active_tasks table is NULL\n");
			return;
		}
		// Lookup task_id in active_tasks
		TaskContext *context = g_hash_table_lookup(active_tasks, GUINT_TO_POINTER(task_id));
		if (context != NULL) {
			SWLOG_INFO("[Waiting task_id in -%d] Sending response to app_id : %s\n",task_id, ctx->process_name);
			// Send D-Bus response
			g_dbus_method_invocation_return_value(context->invocation,g_variant_new("(ss)", ImageDownloaded, DLpath));
			// Remove task_id from active_tasks
			g_hash_table_remove(active_tasks, GUINT_TO_POINTER(task_id));
		} else {
			SWLOG_INFO("Task-%d not found in active_tasks\n", task_id);
		}
		current = current->next;
	}
	// Clear waiting_download_ids list
	g_slist_free(waiting_download_ids);
	waiting_download_ids = NULL;
	IsCheckUpdateInProgress = FALSE;
	SWLOG_INFO("All Downaod waiting tasks completed !!\n");
}

/* Function to clear waiting list of tasks - check for update */
static gboolean CheckUpdate_complete_callback(gpointer user_data) {
	TaskContext *ctx = (TaskContext *)user_data;
	SWLOG_INFO("In CheckUpdate_complete_callback\n");
	complete_CheckUpdate_waiting_tasks("SKY_AvailableVersion.bin", "YES",ctx);
	SWLOG_INFO(" back from complete_CheckUpdate_waiting_tasks\n");
	return G_SOURCE_REMOVE;  // Don't repeat this timeout
}

/* Function to clear waiting list of tasks - Download Upgrade */
static gboolean Download_complete_callback(gpointer user_data) {
	TaskContext *ctx = (TaskContext *)user_data;
	SWLOG_INFO("In Download_complete_callback\n");
	complete_Download_waiting_tasks("SKY_DownloadedVersion.bin", "YES",ctx);
	SWLOG_INFO(" back from complete_CheckUpdate_waiting_tasks\n");
	return G_SOURCE_REMOVE;  // Don't repeat this timeout
}

//Dummy fucntion to represent xconf communication
int XConfCom()
{
	for (int i=1;i < 1000; i++);
	return 1;
}

/* Async Check Update Task - calls xconf communication check function */
static gboolean check_update_task(gpointer user_data)
{
	CheckUpdate_TaskData *data = (CheckUpdate_TaskData*)user_data;
	guint task_id = data->update_task_id;
	//  guint task_id= (guint)(int)(g_hash_table_lookup(active_tasks, data->update_task_id)); // index of the current task's ctx in active_tasks table. i.e unique task id that is assigned earlier. CheckUpdateTask_id

	SWLOG_INFO("[TASK[task_id extracted from active_tasks]-%d] Starting CheckUpdate for app_id : %s (sender: %s)\n", task_id, data->CheckupdateTask_ctx->process_name, data->CheckupdateTask_ctx->sender_id);
	//  Call checkWithXconf logic here
	// ret =  checkWithXConf(ctx->process_name);

	// For now, simulating the with logs
	if (IsCheckUpdateInProgress == TRUE) {
		SWLOG_INFO("Checkupdate is in progress. Adding task to waiting queue. Will send response once done\n");
		waiting_checkUpdate_ids = g_slist_append(waiting_checkUpdate_ids,GUINT_TO_POINTER(task_id));
		SWLOG_INFO("[CheckUpdate task-%d] Added to waiting queue (total waiting: %d)\n",task_id, g_slist_length(waiting_checkUpdate_ids));

	}else{
		SWLOG_INFO("Starting new CheckUpdate operation for task %d\n\n", task_id);
		SWLOG_INFO("[CheckUpdate task-%d] Contacting xconf server for process-id: %s...\n", task_id, data->CheckupdateTask_ctx->process_name);
		IsCheckUpdateInProgress = TRUE;
		waiting_checkUpdate_ids = g_slist_append(waiting_checkUpdate_ids,GUINT_TO_POINTER(task_id));
		//sleep(300);  // will get replaced by actual check logic  Keepin sleep here blocks main loop ; cannot use sleep it is blocking
		int isDone = XConfCom();
		if(isDone == 1 ) {
			g_timeout_add_seconds(10, CheckUpdate_complete_callback,data->CheckupdateTask_ctx); //  once the xconf communication is done, delay 10 seconds and then call CheckUpdate_complete_callback
		}
	}
	return G_SOURCE_REMOVE;
}

/* Async Download Task - calls downloadFirmware function */
static gboolean downloadFW_task(gpointer user_data)
{
	DownloadFW_TaskData  *data = (DownloadFW_TaskData*)user_data;
	guint task_id = data->download_task_id;

	if (IsDownloadInProgress == TRUE) {
		SWLOG_INFO("Download FirmWware is in progress. Adding task to waiting queue. Will send response once done\n");
		waiting_download_ids = g_slist_append(waiting_download_ids,GUINT_TO_POINTER(task_id));
	}
	else{
		SWLOG_INFO("Starting new DownloadFW operation for task %d\n\n", task_id);
		SWLOG_INFO("[Download task-%d] Starting to download Image : %s for process-id: %s...\n", task_id, data->ImageToDownload,data->DownloadFWTask_ctx->process_name);

		IsDownloadInProgress = TRUE;
		waiting_download_ids = g_slist_append(waiting_download_ids,GUINT_TO_POINTER(task_id));
		// will write downloadFirmware logic
		// Example: ret = downloadFirmwarwfunction(ctx->process_name, progress);//it is the function to makesure that download is completed.
		g_timeout_add_seconds(10, Download_complete_callback,data->DownloadFWTask_ctx);
	}
	return G_SOURCE_REMOVE;
}

/*Async Upgrade Task - calls upgradeFW function*/
static gboolean upgrade_task(gpointer user_data)
{
	TaskContext *ctx = (TaskContext*)user_data;
	guint task_id = GPOINTER_TO_UINT(g_hash_table_lookup(active_tasks, ctx));

	SWLOG_INFO("[TASK-%d] Starting Upgrade for %s (sender: %s)\n",task_id, ctx->process_name, ctx->sender_id);

	// call upgradeFW function
	// ret = upgradeFW(ctx->process_name,ctx->imageNameToDownload);
	//
	SWLOG_INFO("[TASK-%d] Flashing firmware for %s...\n", task_id, ctx->process_name);
	sleep(3);  // will get replaced by actual upgrade logic
	SWLOG_INFO("[TASK-%d] Upgrade completed for %s - SYSTEM WILL REBOOT\n",task_id, ctx->process_name);

	g_dbus_method_invocation_return_value(ctx->invocation,
			g_variant_new("(bs)", TRUE, "Upgrade completed - system will reboot"));

	// Cleanup
	g_hash_table_remove(active_tasks, GUINT_TO_POINTER(task_id));
	free_task_context(ctx);
	return G_SOURCE_REMOVE;
}

/* D-BUS METHOD HANDLER - entry point for all the requests from apps*/
static void process_app_request(GDBusConnection *rdkv_conn_dbus,
				const gchar *rdkv_req_caller_id,
				const gchar *rdkv_req_obj_path,
				const gchar *rdkv_req_iface_name,
				const gchar *rdkv_req_method,
				GVariant *rdkv_req_payload,
				GDBusMethodInvocation *resp_ctx,
				gpointer rdkv_user_ctx)
{
	SWLOG_INFO("\n==== [D-BUS] INCOMING REQUEST: %s from %s ====\n", rdkv_req_method, rdkv_req_caller_id);

	/* CHECK UPDATE REQUEST*/
	//extract process name and libversion from the payload -  inputs provided by app
	if (g_strcmp0(rdkv_req_method, "CheckForUpdate") == 0) {
		gchar *app_id=NULL;
		gchar *CurrFWVersion=NULL; //app_id the registration id given by dbus server to app.
		g_variant_get(rdkv_req_payload, "(ss)", &app_id, &CurrFWVersion);
		CheckUpdate_TaskData *user_data = g_malloc(sizeof(CheckUpdate_TaskData));
		SWLOG_INFO("[D-BUS] CheckForUpdate request : app_id:%s ,CurrFWVersion:%s---------\n",app_id,CurrFWVersion);
		//  g_variant_get(rdkv_req_payload, "(ss)", &app_id, &CurrFWVersion);
		//REgistreation Check
		//gboolean is_registered = g_hash_table_contains(registered_processes, g_ascii_strtoulil(app_id, NULL, 10));
		gboolean is_registered = g_hash_table_contains(registered_processes, GINT_TO_POINTER(g_ascii_strtoull(app_id, NULL, 10)));
		//gboolean is_registered=1;
		SWLOG_INFO("[D-BUS] is_registered:%d app_id searched for : %ld \n",is_registered,g_ascii_strtoull(app_id,NULL,10));
		if (!is_registered) {
			SWLOG_INFO("[D-BUS] REJECTED: CheckUpdate from unregistered sender '%s'\n", rdkv_req_caller_id);
			return;
		}
		else{
			SWLOG_INFO("App is registered\n");
		}

		SWLOG_INFO("[D-BUS] CheckForUpdate request: process='%s', currFWVersion='%s', sender(dbus assigned caller id)='%s'\n",
				app_id, CurrFWVersion, rdkv_req_caller_id);

		// Create task context (stores all request info)
		TaskContext *CheckUpdateTask_ctx = create_task_context(app_id, rdkv_req_caller_id, resp_ctx);

		// Assign unique task ID and track it
		guint CheckUpdateTask_id = next_task_id++; // this will be the key in active_tasks hash table
		g_hash_table_insert(active_tasks, GUINT_TO_POINTER(CheckUpdateTask_id), CheckUpdateTask_ctx);

		SWLOG_INFO("[D-BUS] Spawning ASYNC CheckUpdate task-%d \n",CheckUpdateTask_id);
		user_data->update_task_id = CheckUpdateTask_id;
		user_data->CheckupdateTask_ctx = CheckUpdateTask_ctx;
		// Start async task
		g_timeout_add(100, check_update_task, user_data);
		g_free(app_id);
		g_free(CurrFWVersion);
	}

	/* DOWNLOAD REQUEST  */
	else if (g_strcmp0(rdkv_req_method, "DownloadFirmware") == 0) {
		gchar* app_id=NULL;
		gchar* targetImg=NULL; //the registration id given by dbus server to app-app_id.
		DownloadFW_TaskData *user_data = g_malloc(sizeof(DownloadFW_TaskData));
		g_variant_get(rdkv_req_payload, "(ss)", &app_id,&targetImg);
		SWLOG_INFO("[D-BUS] DownloadFirmware requesit from process='%s', sender='%s'\n, Image To Donwload : %s\n",app_id, rdkv_req_caller_id,targetImg);

		gboolean is_registered = g_hash_table_contains(registered_processes, GINT_TO_POINTER(g_ascii_strtoull(app_id, NULL, 10)));
		//gboolean is_registered=1;
		SWLOG_INFO("[D-BUS] is_registered:%d app_id searched for : %"G_GUINT64_FORMAT" \n",is_registered,g_ascii_strtoull(app_id,NULL,10));
		if (!is_registered) {
			SWLOG_INFO("[D-BUS] REJECTED: CheckUpdate from unregistered sender '%s'\n", rdkv_req_caller_id);
			return;
		}
		else{
			SWLOG_INFO("App is registered\n");
		}

		TaskContext *DownloadFWTask_ctx = create_task_context(app_id, rdkv_req_caller_id, resp_ctx);
		guint DownloadFWTask_id = next_task_id++;
		g_hash_table_insert(active_tasks, GUINT_TO_POINTER(DownloadFWTask_id), DownloadFWTask_ctx);

		SWLOG_INFO("[D-BUS] Spawning ASYNC Download task-%d\n", DownloadFWTask_id);

		// Start download with progress updates every 2 seconds
		user_data->download_task_id = DownloadFWTask_id;
		user_data->DownloadFWTask_ctx = DownloadFWTask_ctx;
		g_timeout_add(2000, downloadFW_task, user_data);

		g_free(app_id);
		g_free(targetImg);
	}

	/* UPGRADE REQUEST - */
	else if (g_strcmp0(rdkv_req_method, "UpdateFirmware") == 0) {
		gchar* app_id=NULL;
		g_variant_get(rdkv_req_payload, "(s)", &app_id);

		SWLOG_INFO("[D-BUS] UpdateFirmware request: process='%s', sender='%s'\n",
				app_id, rdkv_req_caller_id);
		SWLOG_INFO("[D-BUS] WARNING: This will flash firmware and reboot system!\n");

		TaskContext *ctx = create_task_context(app_id, rdkv_req_caller_id, resp_ctx);
		guint task_id = next_task_id++;
		g_hash_table_insert(active_tasks, GUINT_TO_POINTER(task_id), ctx);

		SWLOG_INFO("[D-BUS] Spawning ASYNC Upgrade task-%d\n", task_id);

		g_timeout_add(100, upgrade_task, ctx);

		g_free(app_id);
	}

	/* REGISTER PROCESS */
	else if (g_strcmp0(rdkv_req_method, "RegisterProcess") == 0) {
		gchar *process_name, *lib_version;
		g_variant_get(rdkv_req_payload, "(ss)", &process_name, &lib_version);

		SWLOG_INFO("[D-BUS] RegisterProcess: process='%s', lib='%s', sender='%s'\n",
				process_name, lib_version, rdkv_req_caller_id);

		// Add to process trackiing system
		guint64 handler_id = add_process_to_tracking(process_name, lib_version,rdkv_req_caller_id);

		SWLOG_INFO("[D-BUS] Process registered with handler ID: %"G_GUINT64_FORMAT"\n", handler_id);

		// Send immediate response (no async task needed)
		g_dbus_method_invocation_return_value(resp_ctx,
				g_variant_new("(t)", handler_id)); // convert the handler_id into integer

		g_free(process_name);
		g_free(lib_version);
	}

	/* UNREGISTER PROCESS - Immediate response (no async task needed)*/
	else if (g_strcmp0(rdkv_req_method, "UnregisterProcess") == 0) {
		guint64 handler;
		g_variant_get(rdkv_req_payload, "(t)", &handler);

		SWLOG_INFO("[D-BUS] UnregisterProcess: handler=%"G_GUINT64_FORMAT", sender='%s'\n", handler, rdkv_req_caller_id);

		// Remove from basic tracking system
		if (remove_process_from_tracking(handler)) {
			SWLOG_INFO("[D-BUS] Process unregistered successfully\n");
			g_dbus_method_invocation_return_value(resp_ctx,
					g_variant_new("(b)", TRUE));
		} else {
			SWLOG_INFO("[D-BUS]Failed to unregister process\n");
			g_dbus_method_invocation_return_value(resp_ctx,
					g_variant_new("(b)", FALSE));
		}
	}

	/* UNKNOWN METHOD*/
	else {
		SWLOG_INFO("[D-BUS] Unknown method: %s\n", rdkv_req_method);
		g_dbus_method_invocation_return_error(resp_ctx,
				G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
				"Unknown method: %s", rdkv_req_method);
	}
	SWLOG_INFO("==== [D-BUS] Request handling complete - Active tasks: %d ====\n\n",
			g_hash_table_size(active_tasks));
}


/*D-Bus interface vtable*/
static const GDBusInterfaceVTable interface_vtable = {
    process_app_request,
    NULL, // get_property
    NULL  // set_property
};

/* Initialize D-Bus server */
int setup_dbus_server()
{
	GError *error = NULL;
	GDBusNodeInfo *introspection_data = NULL;

	SWLOG_INFO("[D-BUS SETUP] Setting up D-Bus server...\n");
	// Parse the introspection XML
	introspection_data = g_dbus_node_info_new_for_xml(introspection_xml, &error);
	if (!introspection_data) {
		SWLOG_INFO("[D-BUS SETUP] Error parsing introspection XML: %s\n", error->message);
		g_error_free(error);
		return 0;
	}

	// Get connection to the system bus
	connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
	if (!connection) {
		SWLOG_INFO("[D-BUS SETUP] Error connecting to D-Bus: %s\n", error->message);
		g_error_free(error);
		g_dbus_node_info_unref(introspection_data);
		return 0;
	}

	// Register the object
	registration_id = g_dbus_connection_register_object(
			connection,
			OBJECT_PATH,
			introspection_data->interfaces[0],
			&interface_vtable,
			NULL,  // user_data
			NULL,  // user_data_free_func
			&error);

	if (registration_id == 0) {
		SWLOG_INFO("[D-BUS SETUP] Error registering object: %s\n", error->message);
		g_error_free(error);
		g_dbus_node_info_unref(introspection_data);
		return 0;
	}

	// Request the bus name
	owner_id = g_bus_own_name_on_connection(
			connection,
			BUS_NAME,
			G_BUS_NAME_OWNER_FLAGS_NONE,
			NULL, // name_acquired_callback
			NULL, // name_lost_callback
			NULL, // user_data
			NULL  // user_data_free_func
			);

	SWLOG_INFO("[D-BUS SETUP] Server setup complete. Service name: %s\n", BUS_NAME);
	SWLOG_INFO("[D-BUS SETUP] Object path: %s\n", OBJECT_PATH);
	g_dbus_node_info_unref(introspection_data);
	return 1;
}

// Cleanup D-Bus resources
void cleanup_dbus()
{
	SWLOG_INFO("[CLEANUP] Starting D-Bus cleanup...\n");
	// Clean up all active tasks
	if (active_tasks) {
		SWLOG_INFO("[CLEANUP] Cleaning up %d active tasks...\n", g_hash_table_size(active_tasks));
		GHashTableIter iter;
		gpointer key, value;
		g_hash_table_iter_init(&iter, active_tasks);
		while (g_hash_table_iter_next(&iter, &key, &value)) {
			TaskContext *ctx = (TaskContext*)value;
			SWLOG_INFO("[CLEANUP] Freeing task for process: %s\n", ctx->process_name);
			free_task_context(ctx);
		}
		g_hash_table_destroy(active_tasks);
		active_tasks = NULL;
	}

	// Clean up process tracking system
	cleanup_process_tracking();

	// Unregister D-Bus object
	if (registration_id > 0) {
		SWLOG_INFO("[CLEANUP] Unregistering D-Bus object...\n");
		g_dbus_connection_unregister_object(connection, registration_id);
		registration_id = 0;
	}

	// Release D-Bus connection
	if (connection) {
		SWLOG_INFO("[CLEANUP] Releasing D-Bus connection...\n");
		g_object_unref(connection);
		connection = NULL;
	}

	if (owner_id != 0) {
		SWLOG_INFO("[DBUS]Failed to own bus name\n");
		g_bus_unown_name(owner_id);
	}

	// Free main loop
	if (main_loop) {
        SWLOG_INFO("[CLEANUP] Freeing main loop...\n");
        g_main_loop_unref(main_loop);
        main_loop = NULL;
    }

    SWLOG_INFO("[CLEANUP] D-Bus cleanup complete\n");
}

