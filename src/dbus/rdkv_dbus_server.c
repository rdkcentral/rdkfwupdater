#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include "rdkv_dbus_server.h"
#include "rdkFwupdateMgr_handlers.h"
#include "rdkv_cdl_log_wrapper.h"

// Context for background XConf fetch operation (runs in GTask worker thread)
typedef struct {
    gchar *handler_id;              // Handler ID that initiated request
    GDBusConnection *connection;    // D-Bus connection for signal emission
} AsyncXconfFetchContext;

// Forward declarations for async fetch functions
static void async_xconf_fetch_task(GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable);
static void async_xconf_fetch_complete(GObject *source_object, GAsyncResult *res, gpointer user_data);

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
"  <interface name='org.rdkfwupdater.Interface'>"
"<method name='CheckForUpdate'>"
"<arg type='s' name='handler_process_name' direction='in'/>" //Handler/Client ID - only input needed
//FwData structure - All output parameters filled by server
"<arg type='s' name='fwdata_version' direction='out'/>" //Current firmware version (detected by server)
"<arg type='s' name='fwdata_availableVersion' direction='out'/>" //Available version (from XConf)
"<arg type='s' name='fwdata_updateDetails' direction='out'/>" //Update details (from XConf)  
"<arg type='s' name='fwdata_status' direction='out'/>" //Status string from FwData structure (optional field)
"<arg type='i' name='fwdata_status_code' direction='out'/>" //Status code (0=available, 1=not_available, 2=error)
"</method>"
"<method name='DownloadFirmware'>"
"<arg type='s' name='handler' direction='in'/>" //this is a struct as per requirement, for now taking it as string.  handler argument will be the process_name in dbus_handlers
"<arg type='s' name='ImageToDownload' direction='in'/>"
"<arg type='s' name='DownloadedImageVersion' direction='out'/>"  // just send out the success message once the download is triggered; will modify it later to send updates in parallel.Need to add one more output arg here
"<arg type='s' name='downloadPath' direction='out'/>"
"</method>"
"<method name='UpdateFirmware'>"
"<arg type='s' name='hanlder' direction='in'/>"
"<arg type='s' name='currFWVersion' direction='in'/>"
"<arg type='s' name='availableVersion' direction='in'/>"
"<arg type='s' name='option1' direction='in'/>" // this will be part of UpdateDetails object in FwData ; for now hardcoding to 0and1 and not taking statusOfFw as input yet
"<arg type='s' name='option2' direction='in'/>"
"<arg type='b' name='success' direction='out'/>" //send out success to app once the Upgrade fucntion is called. eventually system reboots.
"<arg type='s' name='Message' direction='out'/>" // some intimation message
"</method>"
"<method name='RegisterProcess'>"
"<arg type='s' name='handler' direction='in'/>" //the process name it is
"<arg type='s' name='libVersion' direction='in'/>"
"<arg type='t' name='handler_id' direction='out'/>" //handler type  sent to app and app stores it
"</method>"
"<method name='UnregisterProcess'>"
"<arg type='t' name='handler_id' direction='in'/>"
"<arg type='b' name='success' direction='out'/>"
"</method>"
"<!-- D-Bus Signals for Async Callbacks -->"
"<signal name='CheckForUpdateComplete'>"
"<arg type='s' name='handler_id'/>"
"<arg type='i' name='result_code'/>"
"<arg type='s' name='current_version'/>"
"<arg type='s' name='available_version'/>"
"<arg type='s' name='update_details'/>"
"<arg type='s' name='status_message'/>"
"</signal>"
"</interface>"
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

/*add process to tracking with duplicate prevention */
static guint64 add_process_to_tracking(const gchar *process_name,
                                      const gchar *lib_version,
                                      const gchar *sender_id)
{
	// Check for existing registrations
	GHashTableIter iter;
	gpointer key, value;
	g_hash_table_iter_init(&iter, registered_processes);
	
	ProcessInfo *existing_same_client = NULL;
	ProcessInfo *existing_same_process = NULL;
	
	SWLOG_INFO("[PROCESS_TRACKING] Validating registration for process='%s', sender='%s'\n", 
	           process_name, sender_id);
	
	while (g_hash_table_iter_next(&iter, &key, &value)) {
		ProcessInfo *info = (ProcessInfo*)value;
		
		// Check for same sender_id (client) // comparing the dbus sender id 
		if (g_strcmp0(info->sender_id, sender_id) == 0) {
			existing_same_client = info;
			SWLOG_INFO("[PROCESS_TRACKING] Found existing client: process='%s', handler=%"G_GUINT64_FORMAT"\n",
			           info->process_name, info->handler_id);
		}
		
		// Check for same process_name
		if (g_strcmp0(info->process_name, process_name) == 0) {
			existing_same_process = info;
			SWLOG_INFO("[PROCESS_TRACKING] Found existing process name: sender='%s', handler=%"G_GUINT64_FORMAT"\n",
			           info->sender_id, info->handler_id);
		}
	}
	
	
	// Case 1: Same client, same process name -> Return existing handler_id
	if (existing_same_client && existing_same_process && 
	    existing_same_client == existing_same_process) {
		SWLOG_INFO("[PROCESS_TRACKING] SCENARIO: Same client re-registering same process\n");
		SWLOG_INFO("[PROCESS_TRACKING] RESULT: Returning existing handler_id %"G_GUINT64_FORMAT"\n", 
		           existing_same_client->handler_id);
		return existing_same_client->handler_id;
	}
	
	// Case 2: Same client, different process name -> REJECT
	if (existing_same_client && (!existing_same_process || existing_same_client != existing_same_process)) {
		SWLOG_ERROR("[PROCESS_TRACKING] SCENARIO: Same client attempting different process name\n");
		SWLOG_ERROR("[PROCESS_TRACKING] CONFLICT: Client already registered as '%s' (handler=%"G_GUINT64_FORMAT")\n",
		            existing_same_client->process_name, existing_same_client->handler_id);
		SWLOG_ERROR("[PROCESS_TRACKING] RESULT: REJECTED - One registration per client\n");
		return 0; // Error code
	}
	
	// Case 3: Different client, same process name -> REJECT
	if (existing_same_process && (!existing_same_client || existing_same_client != existing_same_process)) {
		SWLOG_ERROR("[PROCESS_TRACKING] SCENARIO: Different client attempting same process name\n");
		SWLOG_ERROR("[PROCESS_TRACKING] CONFLICT: Process '%s' already registered by client '%s' (handler=%"G_GUINT64_FORMAT")\n",
		            existing_same_process->process_name, existing_same_process->sender_id, existing_same_process->handler_id);
		SWLOG_ERROR("[PROCESS_TRACKING] RESULT: REJECTED - Process name already taken\n");
		return 0; // Error code
	}
	
	// Case 4: New client, new process name -> ALLOW
	SWLOG_INFO("[PROCESS_TRACKING] SCENARIO: New client, new process name\n");
	SWLOG_INFO("[PROCESS_TRACKING] RESULT: Creating new registration\n");
	
	ProcessInfo *info = g_malloc0(sizeof(ProcessInfo));
	info->handler_id = next_process_id++;
	info->process_name = g_strdup(process_name);
	info->lib_version = g_strdup(lib_version);
	info->sender_id = g_strdup(sender_id);
	info->registration_time = g_get_monotonic_time();
	
	guint64 handler_key = info->handler_id;
	g_hash_table_insert(registered_processes, GINT_TO_POINTER(handler_key), info);
	
	SWLOG_INFO("[PROCESS_TRACKING] SUCCESS: Added process='%s' (handler=%"G_GUINT64_FORMAT", sender='%s')\n",
	           process_name, info->handler_id, sender_id);
	SWLOG_INFO("[PROCESS_TRACKING] Total registered processes: %d\n", g_hash_table_size(registered_processes));
	
	return info->handler_id;
}

/* Remove process from tracking list */
static gboolean remove_process_from_tracking(guint64 handler_id)
{
	ProcessInfo *info = g_hash_table_lookup(registered_processes, GINT_TO_POINTER(handler_id));
	if (!info) {
		SWLOG_INFO("[PROCESS_TRACKING] Handler %"G_GUINT64_FORMAT" not found\n", handler_id);
		return FALSE;
	}
	SWLOG_INFO("[PROCESS_TRACKING] Removing: %s (handler: %"G_GUINT64_FORMAT")\n",info->process_name, handler_id);
	g_hash_table_remove(registered_processes, GINT_TO_POINTER(handler_id));
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

/* Helper function to get specific rejection reason for error messages */
static const gchar* get_rejection_reason(const gchar *process_name, const gchar *sender_id) 
{
	GHashTableIter iter;
	gpointer key, value;
	g_hash_table_iter_init(&iter, registered_processes);
	
	ProcessInfo *existing_same_client = NULL;
	ProcessInfo *existing_same_process = NULL;
	
	while (g_hash_table_iter_next(&iter, &key, &value)) {
		ProcessInfo *info = (ProcessInfo*)value;
		
		if (g_strcmp0(info->sender_id, sender_id) == 0) {
			existing_same_client = info;
		}
		
		if (g_strcmp0(info->process_name, process_name) == 0) {
			existing_same_process = info;
		}
	}
	
	// Determine specific rejection reason
	if (existing_same_client && (!existing_same_process || existing_same_client != existing_same_process)) {
		return "Client already registered with different process name";
	}
	
	if (existing_same_process && (!existing_same_client || existing_same_client != existing_same_process)) {
		return "Process name already registered by another client";
	}
	
	return "Unknown registration conflict";
}

/* Initializes the async task tracking system */
void init_task_system()
{
	active_tasks = g_hash_table_new_full(g_direct_hash, g_direct_equal,NULL, (GDestroyNotify)g_free);
	SWLOG_INFO("[TASK-SYSTEM] Initialized task tracking system\n");
	// initialize process tracking
	init_process_tracking();
}

// Create base task context with common fields
static TaskContext* create_task_context(TaskType type,
                                        const gchar* handler_process_name,
                                        const gchar* sender_id,
                                        GDBusMethodInvocation *invocation)
{
	TaskContext *ctx = g_malloc0(sizeof(TaskContext));
	ctx->type = type;
	ctx->process_name = g_strdup(handler_process_name);
	ctx->sender_id = g_strdup(sender_id);
	ctx->invocation = invocation;
	
	// Union fields are automatically zeroed by g_malloc0
	SWLOG_INFO("Created task context for type: %d\n", type);
	return ctx;
}

/* Free task context when done - handles union-based design */
static void free_task_context(TaskContext *ctx)
{
	if (!ctx) return;
	
	// Free common fields
	g_free(ctx->process_name);
	g_free(ctx->sender_id);
	
	// Free union-specific fields based on task type
	switch (ctx->type) {
		case TASK_TYPE_CHECK_UPDATE:
			g_free(ctx->data.check_update.client_fwdata_version);
			g_free(ctx->data.check_update.client_fwdata_availableVersion);
			g_free(ctx->data.check_update.client_fwdata_updateDetails);
			g_free(ctx->data.check_update.client_fwdata_status);
			break;
		
		case TASK_TYPE_DOWNLOAD:
			g_free(ctx->data.download.image_to_download);
			g_free(ctx->data.download.download_url);
			break;
		
		case TASK_TYPE_UPDATE:
			g_free(ctx->data.update.firmware_path);
			break;
		
		case TASK_TYPE_REGISTER:
			// No additional fields to free for register tasks
			break;
		
		default:
			SWLOG_ERROR("Unknown task type in free_task_context: %d\n", ctx->type);
			break;
	}
	
	g_free(ctx);
}

/* Description: send the xconf server response to apps and clear the task from task tracking system */
void complete_CheckUpdate_waiting_tasks(TaskContext *ctx) 
{
	// Send responses to all waiting CheckUpdate tasks using their stored result data
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
			
			// Send D-Bus response using the stored result data
			const gchar *version = context->data.check_update.client_fwdata_version ? 
			                      context->data.check_update.client_fwdata_version : "";
			const gchar *available = context->data.check_update.client_fwdata_availableVersion ? 
			                        context->data.check_update.client_fwdata_availableVersion : "";
			const gchar *details = context->data.check_update.client_fwdata_updateDetails ? 
			                      context->data.check_update.client_fwdata_updateDetails : "";
			const gchar *status_str = context->data.check_update.client_fwdata_status ? 
			                         context->data.check_update.client_fwdata_status : "";
			
			SWLOG_INFO("[CHECK_UPDATE] Task Completion - Sending Response\n");
			SWLOG_INFO("[CHECK_UPDATE] Task ID: %d\n", task_id);
			SWLOG_INFO("[CHECK_UPDATE] Response data:\n");
			SWLOG_INFO("[CHECK_UPDATE]   - Current FW Version: '%s'\n", version);
			SWLOG_INFO("[CHECK_UPDATE]   - Available Version: '%s'\n", available);
			SWLOG_INFO("[CHECK_UPDATE]   - Update Details: '%s'\n", details);
			SWLOG_INFO("[CHECK_UPDATE]   - Status String: '%s'\n", status_str);
			SWLOG_INFO("[CHECK_UPDATE]   - Status Code: %d ", (gint32)context->data.check_update.result_code);
			
			// Log status meaning
			switch(context->data.check_update.result_code) {
				case 0: SWLOG_INFO("(UPDATE_AVAILABLE)\n"); break;
				case 1: SWLOG_INFO("(UPDATE_NOT_AVAILABLE)\n"); break;
				case 2: SWLOG_INFO("(UPDATE_ERROR)\n"); break;
				default: SWLOG_INFO("(UNKNOWN_STATUS)\n"); break;
			}
			
			SWLOG_INFO("[CHECK_UPDATE] Sending D-Bus response to client...\n");
			g_dbus_method_invocation_return_value(context->invocation,
				g_variant_new("(ssssi)",
					version,     // Current/Detected Fw Version (from server)
					available,   // Available Version (from XConf)
					details,     // Update Details (from XConf)
					status_str,  // Status string from FwData structure (optional field)
					(gint32)context->data.check_update.result_code));    // Status Code (0=UPDATE_AVAILABLE, 1=UPDATE_NOT_AVAILABLE, 2=UPDATE_ERROR)

			SWLOG_INFO("[CHECK_UPDATE] Response sent successfully to client\n");
			
			// ALSO emit D-Bus signal for callback mechanism (NEW ADDITION)
			SWLOG_INFO("[CHECK_UPDATE] Emitting D-Bus signal for callback...\n");
			GError *signal_error = NULL;
			gboolean signal_result = g_dbus_connection_emit_signal(connection,
				NULL,  // Broadcast to all listeners
				"/org/rdkfwupdater/fwupgrade",
				"org.rdkfwupdater.Interface",
				"CheckForUpdateComplete",
				g_variant_new("(sissss)",
					context->process_name,                              // handler_id
					(gint32)context->data.check_update.result_code,     // result_code  
					version,                                            // current_version
					available,                                          // available_version
					details,                                            // update_details
					status_str                                          // status_message
				),
				&signal_error);
				
			if (signal_result) {
				SWLOG_INFO("[CHECK_UPDATE] D-Bus signal emitted successfully for handler '%s'\n", context->process_name);
			} else {
				SWLOG_ERROR("[CHECK_UPDATE] Failed to emit D-Bus signal: %s\n", 
				           signal_error ? signal_error->message : "Unknown error");
				if (signal_error) g_error_free(signal_error);
			}
			// Remove task_id from active_tasks
			g_hash_table_remove(active_tasks, GUINT_TO_POINTER(task_id));
			SWLOG_INFO("[CHECK_UPDATE] Task-%d removed from active tasks\n", task_id);
			SWLOG_INFO("[CHECK_UPDATE] Task Completion - SUCCESS\n");
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
/* COMMENTED OUT - No longer used in async implementation */
#if 0
static gboolean CheckUpdate_complete_callback(gpointer user_data) {
	TaskContext *ctx = (TaskContext *)user_data;
	SWLOG_INFO("In CheckUpdate_complete_callback\n");
	complete_CheckUpdate_waiting_tasks(ctx);
	SWLOG_INFO(" back from complete_CheckUpdate_waiting_tasks\n");
	return G_SOURCE_REMOVE;  // Don't repeat this timeout
}
#endif

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
/* COMMENTED OUT - Replaced by async_xconf_fetch_task and async_xconf_fetch_complete */
#if 0
static gboolean check_update_task(gpointer user_data)
{
	CheckUpdate_TaskData *data = (CheckUpdate_TaskData*)user_data;
	guint task_id = data->update_task_id;

	SWLOG_INFO("[CHECK_UPDATE_TASK] Async Task Execution Started\n");
	SWLOG_INFO("[CHECK_UPDATE_TASK] Task details:\n");
	SWLOG_INFO("[CHECK_UPDATE_TASK]   - Task ID: %d\n", task_id);
	SWLOG_INFO("[CHECK_UPDATE_TASK]   - Handler ID: %s\n", data->CheckupdateTask_ctx->process_name);
	SWLOG_INFO("[CHECK_UPDATE_TASK]   - D-Bus Sender: %s\n", data->CheckupdateTask_ctx->sender_id);
	SWLOG_INFO("[CHECK_UPDATE_TASK]   - Current check in progress: %s\n", IsCheckUpdateInProgress ? "YES" : "NO");

	//  Call checkWithXconf logic here
	// ret =  checkWithXConf(ctx->process_name);

	if (IsCheckUpdateInProgress == TRUE) {
		SWLOG_INFO("[CHECK_UPDATE_TASK] Another CheckUpdate operation is in progress\n");
		SWLOG_INFO("[CHECK_UPDATE_TASK] Adding task-%d to waiting queue...\n", task_id);
		waiting_checkUpdate_ids = g_slist_append(waiting_checkUpdate_ids, GUINT_TO_POINTER(task_id));
		SWLOG_INFO("[CHECK_UPDATE_TASK] Task-%d added to waiting queue (total waiting: %d)\n", 
			   task_id, g_slist_length(waiting_checkUpdate_ids));
		SWLOG_INFO("[CHECK_UPDATE_TASK] Will send response once current operation completes\n");

	} else {
		SWLOG_INFO("[CHECK_UPDATE_TASK] Starting NEW CheckUpdate operation for task-%d\n", task_id);
		SWLOG_INFO("[CHECK_UPDATE_TASK] Setting IsCheckUpdateInProgress = TRUE\n");
		IsCheckUpdateInProgress = TRUE;
		waiting_checkUpdate_ids = g_slist_append(waiting_checkUpdate_ids, GUINT_TO_POINTER(task_id));
		
		SWLOG_INFO("[CHECK_UPDATE_TASK] Initiating XConf communication and device queries...\n");
		
		// Use real handler ID and client's version from union
		TaskContext *ctx = data->CheckupdateTask_ctx;
		
		// Verify this is a CheckUpdate task
		if (ctx->type != TASK_TYPE_CHECK_UPDATE) {
			SWLOG_ERROR("[CHECK_UPDATE_TASK] ERROR: Wrong task type %d, expected %d\n", 
			           ctx->type, TASK_TYPE_CHECK_UPDATE);
			SWLOG_ERROR("[CHECK_UPDATE_TASK] Task-%d FAILED due to type mismatch\n", task_id);
			return G_SOURCE_REMOVE;
		}
		
		gchar *handler_id = g_strdup(ctx->process_name);
		SWLOG_INFO("[CHECK_UPDATE_TASK] Executing firmware check with:\n");
		SWLOG_INFO("[CHECK_UPDATE_TASK]   - Handler ID: '%s'\n", handler_id);
		
		SWLOG_INFO("[CHECK_UPDATE_TASK] Calling rdkFwupdateMgr_checkForUpdate()...\n");
		
		// Expecting a response structure in return from rdkFwupdateMgr_checkForUpdate
		CheckUpdateResponse response = rdkFwupdateMgr_checkForUpdate(handler_id);
		
		SWLOG_INFO("[CHECK_UPDATE_TASK] rdkFwupdateMgr_checkForUpdate() completed!\n");
		SWLOG_INFO("[CHECK_UPDATE_TASK] Results:\n");
		SWLOG_INFO("[CHECK_UPDATE_TASK]   - Result Code: %d", response.result_code);
		switch(response.result_code) {
			case 0: SWLOG_INFO(" (UPDATE_AVAILABLE)\n"); break;
			case 1: SWLOG_INFO(" (UPDATE_NOT_AVAILABLE)\n"); break;
			case 2: SWLOG_INFO(" (UPDATE_ERROR)\n"); break;
			default: SWLOG_INFO(" (UNKNOWN_STATUS)\n"); break;
		}
		SWLOG_INFO("[CHECK_UPDATE_TASK]   - Current Image Version: '%s'\n", 
		           response.current_img_version ? response.current_img_version : "NULL");
		SWLOG_INFO("[CHECK_UPDATE_TASK]   - Available Version: '%s'\n", 
		           response.available_version ? response.available_version : "NULL");
		SWLOG_INFO("[CHECK_UPDATE_TASK]   - Update Details: '%s'\n", 
		           response.update_details ? response.update_details : "NULL");
		
		SWLOG_INFO("[CHECK_UPDATE_TASK] Storing results in task context...\n");
		// Store response in TaskContext for callback to use
		ctx->data.check_update.result_code = response.result_code;
		g_free(ctx->data.check_update.client_fwdata_version);
		g_free(ctx->data.check_update.client_fwdata_availableVersion);
		g_free(ctx->data.check_update.client_fwdata_updateDetails); 
		g_free(ctx->data.check_update.client_fwdata_status);
		
		ctx->data.check_update.client_fwdata_version = g_strdup(response.current_img_version ? response.current_img_version : "");
		ctx->data.check_update.client_fwdata_availableVersion = g_strdup(response.available_version ? response.available_version : "");
		ctx->data.check_update.client_fwdata_updateDetails = g_strdup(response.update_details ? response.update_details : "");
		ctx->data.check_update.client_fwdata_status = g_strdup(response.status_message ? response.status_message : "");
		
		SWLOG_INFO("[CHECK_UPDATE_TASK] Results stored successfully in task context\n");
		SWLOG_INFO("[CHECK_UPDATE_TASK] Scheduling completion callback in 10 seconds...\n");
		
		// ALWAYS schedule callback - client needs response regardless of result
		g_timeout_add_seconds(10, CheckUpdate_complete_callback, data->CheckupdateTask_ctx);
		
		SWLOG_INFO("[CHECK_UPDATE_TASK] Callback scheduled - cleanup and exit\n");
		// Clean up allocated memory
		g_free(handler_id);
		checkupdate_response_free(&response);  // Clean up response structure
		
		SWLOG_INFO("[CHECK_UPDATE_TASK] Async Task Execution Complete\n");
	}
	return G_SOURCE_REMOVE;
}
#endif

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
	//extract process handler_id and FwData from the payload -  inputs provided by client app
	/* CHECK FOR UPDATE REQUEST - CACHE-FIRST, NON-BLOCKING */
	if (g_strcmp0(rdkv_req_method, "CheckForUpdate") == 0) {
		gchar *handler_process_name = NULL;
		g_variant_get(rdkv_req_payload, "(s)", &handler_process_name);
		
		SWLOG_INFO("[CHECK_UPDATE] NEW D-BUS REQUEST\n");
		SWLOG_INFO("[CHECK_UPDATE] Timestamp: %ld\n", (long)time(NULL));
		SWLOG_INFO("[CHECK_UPDATE] Handler ID: '%s'\n", handler_process_name ? handler_process_name : "NULL");
		SWLOG_INFO("[CHECK_UPDATE] D-Bus Sender: '%s'\n", rdkv_req_caller_id);
		SWLOG_INFO("[CHECK_UPDATE] D-Bus Path: /org/rdkfwupdater/fwupgrade\n");
		SWLOG_INFO("[CHECK_UPDATE] D-Bus Interface: org.rdkfwupdater.Interface\n");
		SWLOG_INFO("[CHECK_UPDATE] D-Bus Method: CheckForUpdate\n");
		SWLOG_INFO("[CHECK_UPDATE] Daemon State:\n");
		SWLOG_INFO("[CHECK_UPDATE]   Registered Processes: %d\n", g_hash_table_size(registered_processes));
		SWLOG_INFO("[CHECK_UPDATE]   XConf Fetch In Progress: %s\n", IsCheckUpdateInProgress ? "YES" : "NO");
		SWLOG_INFO("[CHECK_UPDATE]   Active Tasks (hash table): %d\n", g_hash_table_size(active_tasks));
		SWLOG_INFO("[CHECK_UPDATE]   Waiting Queue (list): %d task(s)\n", g_slist_length(waiting_checkUpdate_ids));
		
		// 1. VALIDATE HANDLER ID
		if (!handler_process_name || strlen(handler_process_name) == 0) {
			SWLOG_ERROR("[CHECK_UPDATE] REJECTED: Invalid handler ID\n");
			g_dbus_method_invocation_return_error(resp_ctx, 
				G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS, "Invalid handler ID");
			g_free(handler_process_name);
			return;
		}
		
		// 2. VALIDATE REGISTRATION
		guint64 handler_id_numeric = g_ascii_strtoull(handler_process_name, NULL, 10);
		gboolean is_registered = g_hash_table_contains(registered_processes, GINT_TO_POINTER(handler_id_numeric));
		
		SWLOG_INFO("[CHECK_UPDATE] Registration check: %s\n", is_registered ? "REGISTERED" : "NOT REGISTERED");
		
		if (!is_registered) {
			SWLOG_ERROR("[CHECK_UPDATE] REJECTED: Handler not registered\n");
			g_dbus_method_invocation_return_error(resp_ctx, 
				G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED, 
				"Handler not registered. Call RegisterProcess first.");
			g_free(handler_process_name);
			return;
		}
		
		// 3. CHECK CACHE (FAST, NON-BLOCKING)
		SWLOG_INFO("\n[STEP 3] Cache Check\n");
		SWLOG_INFO("  Calling: xconf_cache_exists()\n");
		gboolean cache_exists = xconf_cache_exists();
		SWLOG_INFO("[CHECK_UPDATE] Result: %s\n", cache_exists ? "CACHE HIT" : "CACHE MISS");
		
		if (cache_exists) {
			// CACHE HIT PATH
			SWLOG_INFO("[CHECK_UPDATE] CACHE HIT PATH - Immediate Response\n");
			SWLOG_INFO("[CHECK_UPDATE] Action: Loading firmware data from cache\n");
			
			CheckUpdateResponse response = rdkFwupdateMgr_checkForUpdate(handler_process_name);
			
			SWLOG_INFO("[CHECK_UPDATE] Cache data loaded successfully\n");
			SWLOG_INFO("[CHECK_UPDATE] Cached Firmware Data:\n");
			SWLOG_INFO("[CHECK_UPDATE]   Status Code: %d ", response.result_code);
			switch(response.result_code) {
				case 0: SWLOG_INFO("(UPDATE_AVAILABLE)\n"); break;
				case 1: SWLOG_INFO("(UPDATE_NOT_AVAILABLE)\n"); break;
				case 2: SWLOG_INFO("(UPDATE_ERROR)\n"); break;
				default: SWLOG_INFO("(UNKNOWN)\n"); break;
			}
			SWLOG_INFO("[CHECK_UPDATE]   Current Version: '%s'\n", 
			           response.current_img_version ? response.current_img_version : "N/A");
			SWLOG_INFO("[CHECK_UPDATE]   Available Version: '%s'\n", 
			           response.available_version ? response.available_version : "N/A");
			SWLOG_INFO("[CHECK_UPDATE]   Status Message: '%s'\n", 
			           response.status_message ? response.status_message : "N/A");
			
			SWLOG_INFO("[CHECK_UPDATE] Sending immediate D-Bus method response\n");
			// Send immediate D-Bus response
			g_dbus_method_invocation_return_value(resp_ctx,
				g_variant_new("(ssssi)",
					response.current_img_version ? response.current_img_version : "",
					response.available_version ? response.available_version : "",
					response.update_details ? response.update_details : "",
					response.status_message ? response.status_message : "",
					response.result_code));
			
			SWLOG_INFO("[CHECK_UPDATE] D-Bus method response sent successfully\n");
			
			// Also emit signal for consistency (so clients can use either method or signal)
			SWLOG_INFO("[CHECK_UPDATE] Emitting CheckForUpdateComplete signal for consistency...\n");
			GError *signal_error = NULL;
			gboolean signal_sent = g_dbus_connection_emit_signal(connection,
				NULL, "/org/rdkfwupdater/fwupgrade",
				"org.rdkfwupdater.Interface",
				"CheckForUpdateComplete",
				g_variant_new("(sissss)",
					handler_process_name,
					(gint32)response.result_code,
					response.current_img_version ? response.current_img_version : "",
					response.available_version ? response.available_version : "",
					response.update_details ? response.update_details : "",
					response.status_message ? response.status_message : ""
				),
				&signal_error);
			
			if (signal_sent) {
				SWLOG_INFO("[CHECK_UPDATE] Signal emitted successfully\n");
			} else {
				SWLOG_ERROR("[CHECK_UPDATE] Signal emission failed: %s\n",
				           signal_error ? signal_error->message : "Unknown");
				if (signal_error) g_error_free(signal_error);
			}
			
			checkupdate_response_free(&response);
			g_free(handler_process_name);
			
			SWLOG_INFO("[CHECK_UPDATE] CACHE HIT PATH COMPLETE\n");
			SWLOG_INFO("[CHECK_UPDATE] Total processing: Immediate (no async operation)\n");
			SWLOG_INFO("[CHECK_UPDATE] Client received: Real firmware data\n");
			return;
		}
		
		// CACHE MISS PATH
		SWLOG_INFO("CACHE MISS PATH - Async Background Fetch\n");
		SWLOG_INFO("  XConf cache not available\n");
		SWLOG_INFO("  Strategy: Async non-blocking fetch required\n");
		SWLOG_INFO("  Client flow:\n");
		SWLOG_INFO("    1. Gets UPDATE_ERROR (status=2) immediately\n");
		SWLOG_INFO("    2. Waits for CheckForUpdateComplete signal\n");
		SWLOG_INFO("    3. Receives real result when XConf fetch completes\n");
		
		// 4. SEND IMMEDIATE UPDATE_ERROR RESPONSE
		SWLOG_INFO("\n[STEP 4] Immediate Response\n");
		SWLOG_INFO("  Sending: D-Bus method response\n");
		SWLOG_INFO("  Response: UPDATE_ERROR (status=2)\n");
		g_dbus_method_invocation_return_value(resp_ctx,
			g_variant_new("(ssssi)", "", "", "", "UPDATE_ERROR", 2));
		
		SWLOG_INFO("[CHECK_UPDATE] Response sent successfully\n");
		SWLOG_INFO("  Client now knows: Fetch in progress, wait for signal\n");
		SWLOG_INFO("  Note: D-Bus invocation consumed (cannot respond twice)\n");
		
		// 5. CREATE TASK AND ADD TO TRACKING
		SWLOG_INFO("\n[STEP 5] Task Creation & Tracking\n");
		// NOTE: invocation is NULL because we already sent the response above.
		// Waiting tasks don't need invocation - they get result via signal broadcast.
		TaskContext *task_ctx = create_task_context(TASK_TYPE_CHECK_UPDATE, 
		                                            handler_process_name, 
		                                            rdkv_req_caller_id, 
		                                            NULL);  // invocation already consumed!
		guint task_id = next_task_id++;
		
		SWLOG_INFO("  Task Created:\n");
		SWLOG_INFO("    Task ID         : %d (global counter now: %d)\n", task_id, next_task_id);
		SWLOG_INFO("    Task Type       : TASK_TYPE_CHECK_UPDATE\n");
		SWLOG_INFO("    Handler         : '%s'\n", handler_process_name);
		SWLOG_INFO("    D-Bus Sender    : '%s'\n", rdkv_req_caller_id);
		SWLOG_INFO("    Invocation      : NULL (consumed for immediate UPDATE_ERROR)\n");
		
		SWLOG_INFO("\n  Adding to Tracking Systems:\n");
		SWLOG_INFO("[CHECK_UPDATE] [5a] Adding to 'active_tasks' hash table\n");
		g_hash_table_insert(active_tasks, GUINT_TO_POINTER(task_id), task_ctx);
		SWLOG_INFO("[CHECK_UPDATE]   Task-%d stored in active_tasks (size=%d)\n", 
		           task_id, g_hash_table_size(active_tasks));
		
		SWLOG_INFO("[CHECK_UPDATE] [5b] Adding to 'waiting_checkUpdate_ids' queue\n");
		waiting_checkUpdate_ids = g_slist_append(waiting_checkUpdate_ids, GUINT_TO_POINTER(task_id));
		SWLOG_INFO("[CHECK_UPDATE]   Task-%d queued in waiting list (size=%d)\n", 
		           task_id, g_slist_length(waiting_checkUpdate_ids));
		
		SWLOG_INFO("[CHECK_UPDATE] Tracking Summary:\n");
		SWLOG_INFO("[CHECK_UPDATE]   'active_tasks' stores full TaskContext (for cleanup)\n");
		SWLOG_INFO("[CHECK_UPDATE]   'waiting_checkUpdate_ids' stores task IDs (for batch processing)\n");
		SWLOG_INFO("[CHECK_UPDATE]   Both cleaned up together when XConf fetch completes\n");
		
		// 6. CHECK IF FETCH ALREADY IN PROGRESS
		SWLOG_INFO("\n[STEP 6] Fetch Status Check\n");
		SWLOG_INFO("  IsCheckUpdateInProgress = %s\n", 
		           IsCheckUpdateInProgress ? "TRUE (fetch running)" : "FALSE (idle)");
		
		if (IsCheckUpdateInProgress) {
			SWLOG_INFO("[CHECK_UPDATE] PIGGYBACK SCENARIO - Reuse Running Fetch\n");
			SWLOG_INFO("[CHECK_UPDATE] Another XConf fetch is already running\n");
			SWLOG_INFO("[CHECK_UPDATE] Task-%d will PIGGYBACK on the existing fetch\n", task_id);
			SWLOG_INFO("[CHECK_UPDATE] Current waiting queue: %d task(s)\n", 
			           g_slist_length(waiting_checkUpdate_ids));
			SWLOG_INFO("[CHECK_UPDATE] What happens next:\n");
			SWLOG_INFO("[CHECK_UPDATE]   NO new background fetch started (already running)\n");
			SWLOG_INFO("[CHECK_UPDATE]   Task-%d waits in queue with others\n", task_id);
			SWLOG_INFO("[CHECK_UPDATE]   When fetch completes: ALL waiting tasks get signal\n");
			SWLOG_INFO("[CHECK_UPDATE]   Signal broadcast at: /org/rdkfwupdater/fwupgrade\n");
			SWLOG_INFO("[CHECK_UPDATE]   Signal name: CheckForUpdateComplete\n");
			SWLOG_INFO("[CHECK_UPDATE] PIGGYBACK SETUP COMPLETE\n");
			g_free(handler_process_name);
			return;
		}
		
		// 7. START NEW BACKGROUND FETCH (GTask worker thread)
		SWLOG_INFO("[CHECK_UPDATE] NEW BACKGROUND FETCH - Launching GTask Worker Thread\n");
		SWLOG_INFO("[CHECK_UPDATE] Background Fetch Initialization\n");
		SWLOG_INFO("[CHECK_UPDATE] Setting IsCheckUpdateInProgress = TRUE\n");
		IsCheckUpdateInProgress = TRUE;
		SWLOG_INFO("[CHECK_UPDATE] Prevents duplicate concurrent fetches\n");
		SWLOG_INFO("[CHECK_UPDATE] Future requests will piggyback on this fetch\n");
		
		SWLOG_INFO("[CHECK_UPDATE] Creating AsyncXconfFetchContext:\n");
		AsyncXconfFetchContext *async_ctx = g_new0(AsyncXconfFetchContext, 1);
		async_ctx->handler_id = g_strdup(handler_process_name);
		async_ctx->connection = connection;
		SWLOG_INFO("[CHECK_UPDATE]   Handler ID: '%s'\n", handler_process_name);
		SWLOG_INFO("[CHECK_UPDATE]   Connection: %p\n", (void*)connection);
		
		SWLOG_INFO("[CHECK_UPDATE] Creating GTask:\n");
		SWLOG_INFO("[CHECK_UPDATE]   Worker function: async_xconf_fetch_task\n");
		SWLOG_INFO("[CHECK_UPDATE]   (runs in separate thread - blocking I/O OK)\n");
		SWLOG_INFO("[CHECK_UPDATE]   Completion CB: async_xconf_fetch_complete\n");
		SWLOG_INFO("[CHECK_UPDATE]   (runs on main loop - broadcasts signal)\n");
		GTask *task = g_task_new(NULL, NULL, async_xconf_fetch_complete, async_ctx);
		
		SWLOG_INFO("[CHECK_UPDATE] Launching GTask...\n");
		g_task_run_in_thread(task, async_xconf_fetch_task);
		g_object_unref(task);
		SWLOG_INFO("[CHECK_UPDATE] Worker thread spawned\n");
		
		SWLOG_INFO("[CHECK_UPDATE] Async Flow Summary:\n");
		SWLOG_INFO("[CHECK_UPDATE]   [Worker Thread] Fetches from XConf (blocking network I/O)\n");
		SWLOG_INFO("[CHECK_UPDATE]   [Main Loop] Continues processing D-Bus (non-blocking)\n");
		SWLOG_INFO("[CHECK_UPDATE]   [Waiting Queue] Task-%d + %d other(s) = %d total\n", 
		           task_id, g_slist_length(waiting_checkUpdate_ids) - 1,
		           g_slist_length(waiting_checkUpdate_ids));
		SWLOG_INFO("[CHECK_UPDATE]   [On Completion] Callback runs on main loop\n");
		SWLOG_INFO("[CHECK_UPDATE]   [Signal] Broadcast to /org/rdkfwupdater/fwupgrade\n");
		SWLOG_INFO("[CHECK_UPDATE]   [Cleanup] All %d waiting tasks cleaned up\n",
		           g_slist_length(waiting_checkUpdate_ids));
		SWLOG_INFO("[CHECK_UPDATE]   [Reset] IsCheckUpdateInProgress = FALSE\n");
		
		SWLOG_INFO("[CHECK_UPDATE] ASYNC FETCH INITIATED\n");
		SWLOG_INFO("[CHECK_UPDATE] Client Status:\n");
		SWLOG_INFO("[CHECK_UPDATE]   Already received: UPDATE_ERROR (immediate response)\n");
		SWLOG_INFO("[CHECK_UPDATE]   Waiting for: CheckForUpdateComplete signal\n");
		SWLOG_INFO("[CHECK_UPDATE]   Will receive: Real firmware data when fetch completes\n");
		
		g_free(handler_process_name);
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

		TaskContext *DownloadFWTask_ctx = create_task_context(TASK_TYPE_DOWNLOAD, app_id, rdkv_req_caller_id, resp_ctx);
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

		TaskContext *ctx = create_task_context(TASK_TYPE_UPDATE, app_id, rdkv_req_caller_id, resp_ctx);
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

		SWLOG_INFO("[REGISTER] Starting Registration Process\n");
		SWLOG_INFO("[REGISTER] Request details:\n");
		SWLOG_INFO("[REGISTER]   - Process Name: '%s'\n", process_name ? process_name : "NULL");
		SWLOG_INFO("[REGISTER]   - Library Version: '%s'\n", lib_version ? lib_version : "NULL");
		SWLOG_INFO("[REGISTER]   - D-Bus Sender: '%s'\n", rdkv_req_caller_id);
		SWLOG_INFO("[REGISTER]   - Current registered processes: %d\n", g_hash_table_size(registered_processes));

		// Validate inputs
		if (!process_name || strlen(process_name) == 0) {
			SWLOG_ERROR("[REGISTER] ERROR: Invalid process name provided\n");
			g_dbus_method_invocation_return_error(resp_ctx, 
				G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS, 
				"Invalid process name");
			g_free(process_name);
			g_free(lib_version);
			return;
		}

		// registration with validation
		SWLOG_INFO("[REGISTER] Calling add_process_to_tracking...\n");
		guint64 handler_id = add_process_to_tracking(process_name, lib_version, rdkv_req_caller_id);

		if (handler_id > 0) {
			// SUCCESS: Registration accepted (new or reused handler_id)
			SWLOG_INFO("[REGISTER] SUCCESS: Process registered with handler_id %"G_GUINT64_FORMAT"\n", handler_id);
			SWLOG_INFO("[REGISTER]   - Total registered processes: %d\n", g_hash_table_size(registered_processes));
			
			// Send immediate response (no async task needed)
			g_dbus_method_invocation_return_value(resp_ctx,
					g_variant_new("(t)", handler_id));
			
			SWLOG_INFO("[REGISTER] Response sent to client with handler ID: %"G_GUINT64_FORMAT"\n", handler_id);
		} 
		else {
			// ERROR: Registration rejected
			const gchar *reason = get_rejection_reason(process_name, rdkv_req_caller_id);
			SWLOG_ERROR("[REGISTER] REGISTRATION REJECTED: %s\n", reason);
			
			g_dbus_method_invocation_return_error(resp_ctx, 
				G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED, 
				"Registration rejected: %s", reason);
			
			SWLOG_ERROR("[REGISTER] Error response sent: %s\n", reason);
		}
		SWLOG_INFO("[REGISTER] Registration Complete\n");

		g_free(process_name);
		g_free(lib_version);
	}

	/* UNREGISTER PROCESS - Immediate response (no async task needed)*/
	else if (g_strcmp0(rdkv_req_method, "UnregisterProcess") == 0) {
		guint64 handler;
		g_variant_get(rdkv_req_payload, "(t)", &handler);

		SWLOG_INFO("[UNREGISTER] Starting Unregistration Process\n");
		SWLOG_INFO("[UNREGISTER] Request details:\n");
		SWLOG_INFO("[UNREGISTER]   - Handler ID: %"G_GUINT64_FORMAT"\n", handler);
		SWLOG_INFO("[UNREGISTER]   - D-Bus Sender: '%s'\n", rdkv_req_caller_id);
		SWLOG_INFO("[UNREGISTER]   - Current registered processes: %d\n", g_hash_table_size(registered_processes));

		// Validate handler ID
		if (handler == 0) {
			SWLOG_ERROR("[UNREGISTER] ERROR: Invalid handler ID (0) provided\n");
			g_dbus_method_invocation_return_error(resp_ctx, 
				G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS, 
				"Invalid handler ID");
			return;
		}

		// Look up process info to get process name for logging
		ProcessInfo *process_info = g_hash_table_lookup(registered_processes, GINT_TO_POINTER(handler));
		const gchar *process_name = process_info ? process_info->process_name : "UNKNOWN";
		
		// Remove from tracking system
		SWLOG_INFO("[UNREGISTER] Attempting to remove process '%s' from tracking...\n", process_name);
		if (remove_process_from_tracking(handler)) {
			SWLOG_INFO("[UNREGISTER] SUCCESS: Process '%s' unregistered successfully!\n", process_name);
			SWLOG_INFO("[UNREGISTER]   - Removed Handler ID: %"G_GUINT64_FORMAT" (process: %s)\n", handler, process_name);
			SWLOG_INFO("[UNREGISTER]   - Remaining registered processes: %d\n", g_hash_table_size(registered_processes));
			
			g_dbus_method_invocation_return_value(resp_ctx,
					g_variant_new("(b)", TRUE));
			SWLOG_INFO("[UNREGISTER] Response sent: SUCCESS (true)\n");
		} else {
			SWLOG_ERROR("[UNREGISTER] FAILED: Process '%s' not found or already unregistered\n", process_name);
			SWLOG_ERROR("[UNREGISTER]   - Handler ID: %"G_GUINT64_FORMAT" (process: %s) not found\n", handler, process_name);
			SWLOG_INFO("[UNREGISTER]   - Current registered processes: %d\n", g_hash_table_size(registered_processes));
			
			g_dbus_method_invocation_return_value(resp_ctx,
					g_variant_new("(b)", FALSE));
			SWLOG_INFO("[UNREGISTER] Response sent: FAILED (false)\n");
		}
		SWLOG_INFO("[UNREGISTER] Unregistration Complete\n");
	}

	/* UNKNOWN METHOD*/
	else {
		SWLOG_INFO("[D-BUS] Unknown method: %s\n", rdkv_req_method);
		g_dbus_method_invocation_return_error(resp_ctx,
				G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
				"Unknown method: %s", rdkv_req_method);
	}
	SWLOG_INFO("[D-BUS] Request handling complete - Active tasks: %d\n",
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

// ASYNC XCONF FETCH - WORKER THREAD
// This function runs in a GTask worker thread (NOT on main loop)
// It's safe to call blocking functions here
static void async_xconf_fetch_task(GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable) {
    AsyncXconfFetchContext *ctx = (AsyncXconfFetchContext *)task_data;
    
    SWLOG_INFO("\n[ASYNC_FETCH] ========================================\n");
    SWLOG_INFO("[ASYNC_FETCH] *** WORKER THREAD STARTED ***\n");
    SWLOG_INFO("[ASYNC_FETCH] Background XConf fetch executing in separate thread\n");
    SWLOG_INFO("[ASYNC_FETCH] Thread ID: %lu\n", (unsigned long)pthread_self());
    SWLOG_INFO("[ASYNC_FETCH] Handler ID: %s\n", ctx->handler_id);
    SWLOG_INFO("[ASYNC_FETCH] ========================================\n");
    SWLOG_INFO("[ASYNC_FETCH] IMPORTANT: Main loop is FREE and processing other requests!\n");
    SWLOG_INFO("[ASYNC_FETCH] IMPORTANT: This blocking XConf call won't freeze the daemon!\n");
    SWLOG_INFO("[ASYNC_FETCH] ========================================\n");
    
    SWLOG_INFO("[ASYNC_FETCH] Calling rdkFwupdateMgr_checkForUpdate (BLOCKING CALL)...\n");
    SWLOG_INFO("[ASYNC_FETCH] This will:\n");
    SWLOG_INFO("[ASYNC_FETCH]   1. Connect to XConf server (network I/O)\n");
    SWLOG_INFO("[ASYNC_FETCH]   2. Send device information\n");
    SWLOG_INFO("[ASYNC_FETCH]   3. Parse JSON response\n");
    SWLOG_INFO("[ASYNC_FETCH]   4. Save to cache file\n");
    SWLOG_INFO("[ASYNC_FETCH]   5. Return result structure\n");
    
    // Call the blocking handler function - safe here because we're in worker thread!
    CheckUpdateResponse response = rdkFwupdateMgr_checkForUpdate(ctx->handler_id);
    
    SWLOG_INFO("[ASYNC_FETCH] ========================================\n");
    SWLOG_INFO("[ASYNC_FETCH] XConf fetch completed in worker thread!\n");
    SWLOG_INFO("[ASYNC_FETCH] Response data:\n");
    SWLOG_INFO("[ASYNC_FETCH]   - Result code: %d ", response.result_code);
    switch(response.result_code) {
        case 0: SWLOG_INFO("(UPDATE_AVAILABLE)\n"); break;
        case 1: SWLOG_INFO("(UPDATE_NOT_AVAILABLE)\n"); break;
        case 2: SWLOG_INFO("(UPDATE_ERROR)\n"); break;
        default: SWLOG_INFO("(UNKNOWN)\n"); break;
    }
    SWLOG_INFO("[ASYNC_FETCH]   - Current version: '%s'\n", 
               response.current_img_version ? response.current_img_version : "");
    SWLOG_INFO("[ASYNC_FETCH]   - Available version: '%s'\n", 
               response.available_version ? response.available_version : "");
    SWLOG_INFO("[ASYNC_FETCH]   - Update details: '%s'\n", 
               response.update_details ? response.update_details : "");
    SWLOG_INFO("[ASYNC_FETCH]   - Status message: '%s'\n", 
               response.status_message ? response.status_message : "");
    
    SWLOG_INFO("[ASYNC_FETCH] ========================================\n");
    SWLOG_INFO("[ASYNC_FETCH] Packaging result for completion callback...\n");
    
    // Package results for completion callback (which runs on main loop)
    GVariant *result_variant = g_variant_new("(sissss)",
        ctx->handler_id,
        (gint32)response.result_code,
        response.current_img_version ? response.current_img_version : "",
        response.available_version ? response.available_version : "",
        response.update_details ? response.update_details : "",
        response.status_message ? response.status_message : ""
    );
    
    SWLOG_INFO("[ASYNC_FETCH] GVariant created with signal data\n");
    SWLOG_INFO("[ASYNC_FETCH] Returning result to GTask framework...\n");
    g_task_return_pointer(task, g_variant_ref(result_variant), (GDestroyNotify)g_variant_unref);
    
    // Cleanup
    checkupdate_response_free(&response);
    
    SWLOG_INFO("[ASYNC_FETCH] *** WORKER THREAD COMPLETE ***\n");
    SWLOG_INFO("[ASYNC_FETCH] What happens next:\n");
    SWLOG_INFO("[ASYNC_FETCH]   - GTask framework schedules completion callback on main loop\n");
    SWLOG_INFO("[ASYNC_FETCH]   - async_xconf_fetch_complete() will run on main thread\n");
    SWLOG_INFO("[ASYNC_FETCH]   - Completion callback will broadcast signal to all waiting clients\n");
    SWLOG_INFO("[ASYNC_FETCH]   - All waiting tasks will be cleaned up\n");
    SWLOG_INFO("[ASYNC_FETCH] Worker thread exiting now...\n");
    SWLOG_INFO("[ASYNC_FETCH] ========================================\n\n");
}

// ASYNC XCONF FETCH - COMPLETION CALLBACK
// This function runs on the MAIN LOOP after worker thread completes
static void async_xconf_fetch_complete(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    AsyncXconfFetchContext *ctx = (AsyncXconfFetchContext *)user_data;
    GTask *task = G_TASK(res);
    GVariant *result = g_task_propagate_pointer(task, NULL);
    
    SWLOG_INFO("\n[COMPLETE] ========================================\n");
    SWLOG_INFO("[COMPLETE] *** COMPLETION CALLBACK TRIGGERED ***\n");
    SWLOG_INFO("[COMPLETE] Running on MAIN LOOP thread (async operation complete)\n");
    SWLOG_INFO("[COMPLETE] Thread ID: %lu\n", (unsigned long)pthread_self());
    SWLOG_INFO("[COMPLETE] ========================================\n");
    
    if (!result) {
        SWLOG_ERROR("[COMPLETE] ERROR: No result from background task\n");
        SWLOG_ERROR("[COMPLETE] Worker thread may have failed or returned NULL\n");
        SWLOG_ERROR("[COMPLETE] Aborting completion - cannot broadcast signal\n");
        
        // Still need to cleanup and reset state
        SWLOG_INFO("[COMPLETE] Cleaning up state after error...\n");
        IsCheckUpdateInProgress = FALSE;
        if (waiting_checkUpdate_ids) {
            g_slist_free(waiting_checkUpdate_ids);
            waiting_checkUpdate_ids = NULL;
        }
        
        g_free(ctx->handler_id);
        g_free(ctx);
        SWLOG_ERROR("[COMPLETE] ========================================\n\n");
        return;
    }
    
    SWLOG_INFO("[COMPLETE] Result received from worker thread successfully\n");
    SWLOG_INFO("[COMPLETE] Current system state:\n");
    SWLOG_INFO("[COMPLETE]   - IsCheckUpdateInProgress: TRUE (will be reset)\n");
    SWLOG_INFO("[COMPLETE]   - Waiting tasks queue size: %d\n", 
               g_slist_length(waiting_checkUpdate_ids));
    SWLOG_INFO("[COMPLETE]   - Active tasks count: %d\n", 
               g_hash_table_size(active_tasks));
    SWLOG_INFO("[COMPLETE] ========================================\n");
    SWLOG_INFO("[COMPLETE] Now broadcasting signal and cleaning up ALL waiting tasks\n");
    SWLOG_INFO("[COMPLETE] ========================================\n");
    
    // 1. BROADCAST D-Bus Signal to ALL listeners
    SWLOG_INFO("[COMPLETE] Step 1: Broadcasting D-Bus signal to ALL waiting clients...\n");
    SWLOG_INFO("[COMPLETE]   Signal name: 'CheckForUpdateComplete'\n");
    SWLOG_INFO("[COMPLETE]   Object path: '/org/rdkfwupdater/fwupgrade'\n");
    SWLOG_INFO("[COMPLETE]   Interface: 'org.rdkfwupdater.Interface'\n");
    SWLOG_INFO("[COMPLETE]   Destination: NULL (broadcast to all listeners)\n");
    SWLOG_INFO("[COMPLETE]   Waiting clients count: %d\n", 
               g_slist_length(waiting_checkUpdate_ids));
    
    // Extract data for logging
    gchar *handler_id_str;
    gint32 result_code;
    gchar *current_ver, *available_ver, *update_details, *status_msg;
    g_variant_get(result, "(sissss)", &handler_id_str, &result_code,
                 &current_ver, &available_ver, &update_details, &status_msg);
    
    SWLOG_INFO("[COMPLETE]   Signal payload:\n");
    SWLOG_INFO("[COMPLETE]     - Handler ID: '%s'\n", handler_id_str);
    SWLOG_INFO("[COMPLETE]     - Result code: %d ", result_code);
    switch(result_code) {
        case 0: SWLOG_INFO("(UPDATE_AVAILABLE)\n"); break;
        case 1: SWLOG_INFO("(UPDATE_NOT_AVAILABLE)\n"); break;
        case 2: SWLOG_INFO("(UPDATE_ERROR)\n"); break;
        default: SWLOG_INFO("(UNKNOWN)\n"); break;
    }
    SWLOG_INFO("[COMPLETE]     - Current version: '%s'\n", current_ver);
    SWLOG_INFO("[COMPLETE]     - Available version: '%s'\n", available_ver);
    SWLOG_INFO("[COMPLETE]     - Update details: '%s'\n", update_details);
    SWLOG_INFO("[COMPLETE]     - Status message: '%s'\n", status_msg);
    
    // Free extracted strings (g_variant_get duplicates them)
    g_free(handler_id_str);
    g_free(current_ver);
    g_free(available_ver);
    g_free(update_details);
    g_free(status_msg);
    
    SWLOG_INFO("[COMPLETE] Emitting signal now...\n");
    GError *error = NULL;
    gboolean signal_sent = g_dbus_connection_emit_signal(
        ctx->connection,
        NULL,  // NULL = broadcast to all listeners
        "/org/rdkfwupdater/fwupgrade",
        "org.rdkfwupdater.Interface",
        "CheckForUpdateComplete",
        result,
        &error
    );
    
    if (signal_sent) {
        SWLOG_INFO("[COMPLETE]   Signal broadcast SUCCESSFUL\n");
        SWLOG_INFO("[COMPLETE]   All %d waiting client(s) will receive this signal\n",
                   g_slist_length(waiting_checkUpdate_ids));
    } else {
        SWLOG_ERROR("[COMPLETE]   Signal broadcast FAILED: %s\n", 
                   error ? error->message : "Unknown error");
        if (error) g_error_free(error);
    }
    
    // 2. NO NEED TO SEND D-Bus METHOD RESPONSES - They Already Got UPDATE_ERROR!
    // All waiting tasks received UPDATE_ERROR as immediate response when added to queue.
    // They will learn the real result via the D-Bus signal we just broadcast above.
    SWLOG_INFO("[COMPLETE] Step 2: Skipping method responses (already sent UPDATE_ERROR immediately)\n");
    SWLOG_INFO("[COMPLETE]   Total waiting tasks to cleanup: %d\n", g_slist_length(waiting_checkUpdate_ids));
    
    // 3. CLEANUP ALL WAITING TASKS from active_tasks
    SWLOG_INFO("[COMPLETE] Step 3: Cleaning up ALL waiting tasks from 'active_tasks' hash table...\n");
    SWLOG_INFO("[COMPLETE]   Tasks to cleanup: %d\n", g_slist_length(waiting_checkUpdate_ids));
    SWLOG_INFO("[COMPLETE]   Current active_tasks size: %d\n", g_hash_table_size(active_tasks));
    
    GSList *current = waiting_checkUpdate_ids;
    int cleanup_count = 0;
    int not_found_count = 0;
    
    while (current != NULL) {
        guint task_id = GPOINTER_TO_UINT(current->data);
        
        SWLOG_INFO("[COMPLETE]   Processing Task ID: %d\n", task_id);
        if (g_hash_table_remove(active_tasks, GUINT_TO_POINTER(task_id))) {
            SWLOG_INFO("[COMPLETE]     Task %d removed from active_tasks (free_task_context called)\n", task_id);
            cleanup_count++;
        } else {
            SWLOG_WARN("[COMPLETE]     Task %d NOT FOUND in active_tasks (possible bug)\n", task_id);
            not_found_count++;
        }
        
        current = current->next;
    }
    
    SWLOG_INFO("[COMPLETE]   Cleanup summary:\n");
    SWLOG_INFO("[COMPLETE]     - Tasks successfully cleaned: %d\n", cleanup_count);
    SWLOG_INFO("[COMPLETE]     - Tasks not found: %d\n", not_found_count);
    SWLOG_INFO("[COMPLETE]     - Remaining active tasks: %d\n", g_hash_table_size(active_tasks));
    
    // 4. CLEAR WAITING QUEUE
    SWLOG_INFO("[COMPLETE] Step 4: Clearing 'waiting_checkUpdate_ids' queue\n");
    SWLOG_INFO("[COMPLETE]   Queue size before: %d\n", g_slist_length(waiting_checkUpdate_ids));
    g_slist_free(waiting_checkUpdate_ids);
    waiting_checkUpdate_ids = NULL;
    SWLOG_INFO("[COMPLETE]   Queue cleared (all Task IDs freed)\n");
    
    // 5. RESET PROGRESS FLAG
    SWLOG_INFO("[COMPLETE] Step 5: Resetting progress flag\n");
    SWLOG_INFO("[COMPLETE]   Before: IsCheckUpdateInProgress = TRUE\n");
    IsCheckUpdateInProgress = FALSE;
    SWLOG_INFO("[COMPLETE]   After:  IsCheckUpdateInProgress = FALSE\n");
    SWLOG_INFO("[COMPLETE]   New CheckForUpdate requests can now start background fetch\n");
    
    // 6. CLEANUP CONTEXT
    SWLOG_INFO("[COMPLETE] Step 6: Freeing AsyncXconfFetchContext\n");
    SWLOG_INFO("[COMPLETE]   Freeing handler_id: '%s'\n", ctx->handler_id);
    g_free(ctx->handler_id);
    g_free(ctx);
    SWLOG_INFO("[COMPLETE]   Context freed\n");
    
    SWLOG_INFO("[COMPLETE] COMPLETION CALLBACK FINISHED\n");
    SWLOG_INFO("[COMPLETE] Summary:\n");
    SWLOG_INFO("[COMPLETE]   Signal broadcast to all %d waiting client(s)\n", cleanup_count);
    SWLOG_INFO("[COMPLETE]   All waiting tasks cleaned from active_tasks\n");
    SWLOG_INFO("[COMPLETE]   Waiting queue cleared\n");
    SWLOG_INFO("[COMPLETE]   Progress flag reset\n");
    SWLOG_INFO("[COMPLETE]   Context freed\n");
    SWLOG_INFO("[COMPLETE] System ready for next CheckForUpdate request\n");
    SWLOG_INFO("[COMPLETE] ========================================\n\n");
}

