/**
 * @file rdkv_dbus_server.c
 * @brief D-Bus server implementation for RDK Firmware Updater daemon.
 *
 * This file implements the D-Bus service that exposes firmware update operations
 * to client applications. It handles:
 * - D-Bus service registration and method dispatch
 * - Client process registration/tracking (RegisterProcess/UnregisterProcess)
 * - Asynchronous task management (CheckForUpdate, DownloadFirmware, UpdateFirmware)
 * - Signal emission for async operation completion
 * - Concurrency control (one CheckUpdate/Download at a time, queuing for multiple clients)
 *
 * Architecture:
 * - GDBus-based service on system bus: org.rdkfwupdater.Interface
 * - Task tracking system using GHashTable for active async operations
 * - Process tracking to enforce one registration per client/process name
 * - GTask worker threads for long-running XConf/download operations
 * - D-Bus signals for async completion callbacks to clients
 *
 * Key design decisions:
 * - Only one CheckForUpdate or Download operation can run at a time system-wide
 * - Additional requests are queued and processed after current operation completes
 * - Each client can register only once, and process names must be unique
 * - Async operations use GTask to avoid blocking the main D-Bus event loop
 */

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <curl/curl.h>
#include "rdkv_dbus_server.h"
#include "rdkFwupdateMgr_handlers.h"
#include "rdkv_cdl_log_wrapper.h"
#include "rdkv_upgrade.h"  // For RdkUpgradeContext_t and rdkv_upgrade_request()
#include "rdk_fwdl_utils.h"  // For getDeviceProperties() and DeviceProperty_t
#include "rfcinterface.h"  // For getRFCSettings() and Rfc_t

#define DWNL_PATH_FILE_LENGTH DWNL_PATH_FILE_LEN + 32
#define MAX_URL_LEN         512
#define MAX_URL_LEN1 MAX_URL_LEN + 128
/**
 * Context structure for background XConf fetch operation.
 * Passed to GTask worker thread for async CheckForUpdate processing.
 */
/*
 * Context structure for async CheckForUpdate operation.
 * Contains handler ID and D-Bus connection for async xconf fetch.
 */
typedef struct {
    gchar *handler_id;          /* Client handler identifier */
    GDBusConnection *connection; /* D-Bus connection for signal emission */
} AsyncXconfFetchContext;

/* Forward declarations - CheckForUpdate async operation handlers */
static void rdkfw_xconf_fetch_worker(GTask *task, 
		                     gpointer source_object, 
                                     gpointer task_data, 
				     GCancellable *cancellable);
static void rdkfw_xconf_fetch_done(GObject *source_object, 
                                   GAsyncResult *res, gpointer user_data);

/* Forward declarations - DownloadFirmware async operation handlers */
static void rdkfw_download_worker(GTask *task, gpointer source_object, 
                                  gpointer task_data, GCancellable *cancellable);
static void rdkfw_download_done(GObject *source_object, 
                                GAsyncResult *res, gpointer user_data);
static gboolean rdkfw_emit_download_progress(gpointer user_data);

/* Progress monitor context structure - for real-time download progress tracking */
typedef struct {
    GDBusConnection* connection;        // D-Bus connection (borrowed, do NOT free)
    gchar* handler_id;                  // Handler ID string (owned, must free)
    gchar* firmware_name;               // Firmware name (owned, must free)
    gboolean* stop_flag;                // Atomic flag to signal thread shutdown
    GMutex* mutex;                      // Protects last_dlnow from race conditions
    guint64 last_dlnow;                 // Last reported bytes (for throttling)
    time_t last_activity_time;          // Last time progress changed (for timeout detection)
} ProgressMonitorContext;

/* Note: rdkfw_progress_monitor_thread() is declared in rdkFwupdateMgr_handlers.h */

/* Forward declarations - UpdateFirmware async operation handlers */
// Note: These are implemented in rdkFwupdateMgr_handlers.c and externally visible
gboolean emit_flash_progress_idle(gpointer user_data);
gboolean cleanup_flash_state_idle(gpointer user_data);
gpointer rdkfw_flash_worker_thread(gpointer user_data);

/* Concurrency control flags - enforce single operation at a time */
static gboolean IsCheckUpdateInProgress = FALSE;
static gboolean IsDownloadInProgress = FALSE;
gboolean IsFlashInProgress =  FALSE;  // Non-static: accessed by worker thread cleanup

/* Queue management - hold waiting clients when operation in progress */
static GSList *waiting_checkUpdate_ids = NULL;
static GSList *waiting_download_ids = NULL;

/* D-Bus service state */
static guint owner_id = 0;
static GDBusConnection *connection = NULL;
GMainLoop *main_loop = NULL;
static guint registration_id = 0;

/* Task tracking - active async operations */
GHashTable *active_tasks = NULL;
static guint next_task_id = 1;

/* Process tracking - registered clients */
GHashTable *registered_processes = NULL;  // Made non-static for access from handlers
static guint64 next_process_id = 1;

/* Download tracking - active firmware downloads */
GHashTable *active_download_tasks = NULL;  // Map: firmwareName (gchar*) → DownloadState*

/**
 * Current Download State Tracker
 * 
 * Tracks the single active download operation. When a download is in progress,
 * this structure holds the state and list of waiting clients (piggybacking).
 * 
 * Thread Safety:
 * - Only accessed from main loop thread (D-Bus handlers and g_idle_add callbacks)
 * - No mutex needed due to GLib's main loop serialization
 * 
 * Lifecycle:
 * 1. Created (g_new0) when first client initiates download
 * 2. Updated via g_idle_add callbacks from worker thread
 * 3. Queried by subsequent clients for piggybacking
 * 4. Freed in async_download_complete() after download finishes
 * 
 * NULL when no download is active.
 */
static CurrentDownloadState *current_download = NULL;

/* UpdateFirmware flash state - Non-static for worker thread access */
CurrentFlashState *current_flash = NULL;

/* D-Bus introspection data or dbus interface : Exposes the methods for apps */
static const gchar introspection_xml[] =
"<node>"
"  <interface name='org.rdkfwupdater.Interface'>"
"<!-- RegisterProcess: Register a client process -->"
"<method name='RegisterProcess'>"
"<arg type='s' name='handler' direction='in'/>" //the process name it is
"<arg type='s' name='libVersion' direction='in'/>"
"<arg type='t' name='handler_id' direction='out'/>"//handler type  sent to app and app stores it
"</method>"
"<!-- UnregisterProcess: Unregister a client process -->"
"<method name='UnregisterProcess'>"
"<arg type='t' name='handlerId' direction='in'/>"
"<arg type='b' name='success' direction='out'/>"
"</method>"
"<!-- CheckForUpdate: Check for firmware updates -->"
"<method name='CheckForUpdate'>"
"<arg type='s' name='handler_process_name' direction='in'/>" //Handler/Client ID - only input needed
//FwData structure - All output parameters filled by server
"<arg type='s' name='fwdata_version' direction='out'/>" //Current firmware version (detected by server)
"<arg type='s' name='fwdata_availableVersion' direction='out'/>" //Available version (from XConf)
"<arg type='s' name='fwdata_updateDetails' direction='out'/>" //Update details (from XConf)
"<arg type='s' name='fwdata_status' direction='out'/>" //Status string from FwData structure (optional field)
"<arg type='i' name='fwdata_status_code' direction='out'/>" //Status code (0=available, 1=not_available, 2=error)
" </method>"
" <!-- DownloadFirmware: Download firmware image -->"
" <method name='DownloadFirmware'>"
" <arg type='s' name='handlerId' direction='in'/>"
" <arg type='s' name='firmwareName' direction='in'/>"
" <arg type='s' name='downloadUrl' direction='in'/>"
" <arg type='s' name='typeOfFirmware' direction='in'/>"
" <arg type='s' name='result' direction='out'/>"
" <arg type='s' name='status' direction='out'/>"
" <arg type='s' name='message' direction='out'/>"
" </method>"
" <!-- UpdateFirmware: Flash and install firmware -->"
" <method name='UpdateFirmware'>"
" <arg type='s' name='handlerId' direction='in'/>"
" <arg type='s' name='firmwareName' direction='in'/>"
" <arg type='s' name='TypeOfFirmware' direction='in'/>"
" <arg type='s' name='LocationOfFirmware' direction='in'/>"
" <arg type='s' name='rebootImmediately' direction='in'/>"
" <arg type='s' name='UpdateResult' direction='out'/>"
" <arg type='s' name='UpdateStatus' direction='out'/>"
" <arg type='s' name='message' direction='out'/>"
" </method>"
" <!-- Signals -->"
" <signal name='CheckForUpdateComplete'>"
" <arg type='t' name='handlerId'/>"
" <arg type='i' name='resultCode'/>"
" <arg type='s' name='currentVersion'/>"
" <arg type='s' name='availableVersion'/>"
" <arg type='s' name='updateDetails'/>"
" <arg type='s' name='statusMessage'/>"
" </signal>"
" <signal name='DownloadProgress'>"
" <arg type='t' name='handlerId'/>"
" <arg type='s' name='firmwareName'/>"
" <arg type='u' name='progress'/>"
" <arg type='s' name='status'/>"
" <arg type='s' name='message'/>"
" </signal>"
" <signal name='DownloadError'>"
" <arg type='t' name='handlerId'/>"
" <arg type='s' name='firmwareName'/>"
" <arg type='s' name='status'/>"
" <arg type='s' name='errorMessage'/>"
" </signal>"
" <!-- UpdateProgress: Progress updates during firmware flash operation -->"
" <signal name='UpdateProgress'>"
" <arg type='t' name='handlerId'/>"
" <arg type='s' name='firmwareName'/>"
" <arg type='i' name='progress'/>"
" <arg type='i' name='status'/>"
" <arg type='s' name='message'/>"
" </signal>"
" </interface>"
"</node>";    

static void process_app_request(GDBusConnection *rdkv_conn_dbus,
                             const gchar *rdkv_req_caller_id,
                             const gchar *rdkv_req_obj_path,
                             const gchar *rdkv_req_iface_name,
                             const gchar *rdkv_req_method,
                             GVariant *rdkv_req_payload,
                             GDBusMethodInvocation *rdkv_resp_ctx,
			     gpointer rdkv_user_ctx);

static gchar* get_difw_path(void)
{
    GKeyFile *keyfile = g_key_file_new();
    gchar *path = NULL;

    if (g_key_file_load_from_file(keyfile,
                                  "/etc/device.properties",
                                  G_KEY_FILE_NONE,
                                  NULL)) {
        path = g_key_file_get_string(keyfile, "Device", "DIFW_PATH", NULL);
    }

    g_key_file_unref(keyfile);

    if (!path || !*path) {
        // fallback if not set
        g_free(path);
        return g_strdup("/opt/CDL");
    }

    return path;   // caller must free
}


/**
 * @brief Initialize the process tracking system.
 *
 * Creates hash table mapping handler_id -> ProcessInfo to track registered clients.
 * Called once at daemon startup.
 */
static void init_process_tracking()
{
    registered_processes = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)g_free);
    SWLOG_INFO("[TRACKING] process tracking initialized\n");
}

/**
 * @brief Register a new process with duplicate detection and conflict prevention.
 *
 * Enforces the following registration rules:
 * 1. Same client (sender_id), same process_name -> Return existing handler_id (idempotent)
 * 2. Same client, different process_name -> REJECT (one registration per client)
 * 3. Different client, same process_name -> REJECT (process names must be unique)
 * 4. New client, new process_name -> ALLOW (create new registration)
 *
 * @param process_name Human-readable process identifier
 * @param lib_version Client library version string
 * @param sender_id D-Bus unique sender name (e.g., ":1.23")
 * @return handler_id (>0) on success, 0 on conflict/error
 */
static guint64 add_process_to_tracking(const gchar *process_name,
                                      const gchar *lib_version,
                                      const gchar *sender_id)
{
	GHashTableIter iter;
	gpointer key, value;
	g_hash_table_iter_init(&iter, registered_processes);
	
	ProcessInfo *existing_same_client = NULL;
	ProcessInfo *existing_same_process = NULL;
	
	SWLOG_INFO("[PROCESS_TRACKING] Validating registration for process='%s', sender='%s'\n", 
	           process_name, sender_id);
	
	while (g_hash_table_iter_next(&iter, &key, &value)) {
		ProcessInfo *info = (ProcessInfo*)value;
		
		if (g_strcmp0(info->sender_id, sender_id) == 0) {
			existing_same_client = info;
			SWLOG_INFO("[PROCESS_TRACKING] Found existing client: process='%s', handler=%"G_GUINT64_FORMAT"\n",
			           info->process_name, info->handler_id);
		}
		
		if (g_strcmp0(info->process_name, process_name) == 0) {
			existing_same_process = info;
			SWLOG_INFO("[PROCESS_TRACKING] Found existing process name: sender='%s', handler=%"G_GUINT64_FORMAT"\n",
			           info->sender_id, info->handler_id);
		}
	}
	
	// Case 1: Idempotent re-registration (same client, same process)
	if (existing_same_client && existing_same_process && 
	    existing_same_client == existing_same_process) {
		SWLOG_INFO("[PROCESS_TRACKING] SCENARIO: Same client re-registering same process\n");
		SWLOG_INFO("[PROCESS_TRACKING] RESULT: Returning existing handler_id %"G_GUINT64_FORMAT"\n", 
		           existing_same_client->handler_id);
		return existing_same_client->handler_id;
	}
	
	// Case 2: Conflict - Same client with different process name
	if (existing_same_client && (!existing_same_process || existing_same_client != existing_same_process)) {
		SWLOG_ERROR("[PROCESS_TRACKING] SCENARIO: Same client attempting different process name\n");
		SWLOG_ERROR("[PROCESS_TRACKING] CONFLICT: Client already registered as '%s' (handler=%"G_GUINT64_FORMAT")\n",
		            existing_same_client->process_name, existing_same_client->handler_id);
		SWLOG_ERROR("[PROCESS_TRACKING] RESULT: REJECTED - One registration per client\n");
		return 0;
	}
	
	// Case 3: Conflict - Process name already taken by different client
	if (existing_same_process && (!existing_same_client || existing_same_client != existing_same_process)) {
		SWLOG_ERROR("[PROCESS_TRACKING] SCENARIO: Different client attempting same process name\n");
		SWLOG_ERROR("[PROCESS_TRACKING] CONFLICT: Process '%s' already registered by client '%s' (handler=%"G_GUINT64_FORMAT")\n",
		            existing_same_process->process_name, existing_same_process->sender_id, existing_same_process->handler_id);
		SWLOG_ERROR("[PROCESS_TRACKING] RESULT: REJECTED - Process name already taken\n");
		return 0;
	}
	
	// Case 4: Success - New client with new process name
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

/**
 * @brief Remove a process from the tracking system.
 *
 * Called when client invokes UnregisterProcess. Frees associated ProcessInfo.
 *
 * @param handler_id Handler ID to remove
 * @return TRUE if found and removed, FALSE if not found
 */
static gboolean remove_process_from_tracking(guint64 handler_id)
{
	ProcessInfo *info = g_hash_table_lookup(registered_processes, GINT_TO_POINTER(handler_id));
	if (!info) {
		SWLOG_INFO("[PROCESS_TRACKING] Handler %"G_GUINT64_FORMAT" not found\n", handler_id);
		return FALSE;
	}
	SWLOG_INFO("[PROCESS_TRACKING] Removing: %s (handler: %"G_GUINT64_FORMAT")\n", info->process_name, handler_id);
	g_hash_table_remove(registered_processes, GINT_TO_POINTER(handler_id));
	SWLOG_INFO("[PROCESS_TRACKING] Total registered: %d\n", g_hash_table_size(registered_processes));
	return TRUE;
}

/**
 * @brief Clean up process tracking resources at daemon shutdown.
 */
void cleanup_process_tracking()
{
	if (registered_processes) {
		SWLOG_INFO("[TRACKING] Cleaning up %d registered processes\n", g_hash_table_size(registered_processes));
		g_hash_table_destroy(registered_processes);
		registered_processes = NULL;
	}
	
	if (active_download_tasks) {
		SWLOG_INFO("[TRACKING] Cleaning up %d active downloads\n", g_hash_table_size(active_download_tasks));
		// TODO: Cancel any in-progress downloads and free DownloadState structures
		g_hash_table_destroy(active_download_tasks);
		active_download_tasks = NULL;
	}
}

/**
 * @brief Generate human-readable error message for registration rejection.
 *
 * Used by RegisterProcess D-Bus method to provide specific failure reasons.
 *
 * @param process_name Attempted process name
 * @param sender_id D-Bus sender ID
 * @return Static error string describing conflict
 */
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
	
	if (existing_same_client && (!existing_same_process || existing_same_client != existing_same_process)) {
		return "Client already registered with different process name";
	}
	
	if (existing_same_process && (!existing_same_client || existing_same_client != existing_same_process)) {
		return "Process name already registered by another client";
	}
	
	return "Unknown registration conflict";
}

/**
 * @brief Initialize the task tracking and process tracking systems.
 *
 * Called once at daemon startup. Creates hash table for active async tasks.
 */
void init_task_system()
{
	active_tasks = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)g_free);
	SWLOG_INFO("[TASK-SYSTEM] Initialized task tracking system\n");
	
	// Initialize download tracking hash table
	active_download_tasks = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	SWLOG_INFO("[TASK-SYSTEM] Initialized download tracking system\n");
	
	init_process_tracking();
}

/**
 * @brief Allocate and initialize a TaskContext structure.
 *
 * Creates context for async operations (CheckForUpdate, Download, Update).
 * Union-based data field is automatically zeroed by g_malloc0().
 *
 * @param type Task type (TASK_TYPE_CHECK_UPDATE, TASK_TYPE_DOWNLOAD, etc.)
 * @param handler_process_name Process name of requesting client
 * @param sender_id D-Bus sender ID
 * @param invocation D-Bus method invocation context for reply
 * @return Allocated TaskContext (must be freed with free_task_context)
 */
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
	
	SWLOG_INFO("Created task context for type: %d\n", type);
	return ctx;
}

/**
 * @brief Free a TaskContext and all its dynamically allocated fields.
 *
 * Handles union-based data structure by freeing fields specific to task type.
 * Safe to call with NULL pointer.
 *
 * @param ctx TaskContext to free
 */
static void free_task_context(TaskContext *ctx)
{
	if (!ctx) return;
	
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
			g_free(ctx->data.download.firmwareName);
			g_free(ctx->data.download.downloadUrl);
			g_free(ctx->data.download.typeOfFirmware);
			g_free(ctx->data.download.errorMessage);
			g_free(ctx->data.download.localFilePath);
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

/**
 * @brief Complete all waiting CheckForUpdate tasks and send responses.
 *
 * Called after XConf query completes. Iterates through waiting_checkUpdate_ids list,
 * sends D-Bus method responses with cached result data, emits CheckForUpdateComplete
 * signals, and cleans up task contexts. Resets IsCheckUpdateInProgress flag.
 *
 * @param ctx Task context (currently unused, kept for API consistency)
 */
void complete_CheckUpdate_waiting_tasks(TaskContext *ctx) 
{
	SWLOG_INFO("Completing %d waiting CheckUpdate tasks\n", g_slist_length(waiting_checkUpdate_ids));
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
				"/org/rdkfwupdater/Service",
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
	IsCheckUpdateInProgress = FALSE;
	SWLOG_INFO("All CheckUpdate waiting tasks completed !!\n");
}

/**
 * @brief Complete all waiting DownloadFirmware tasks and send responses.
 *
 * Similar to complete_CheckUpdate_waiting_tasks but for download operations.
 * Sends download status and path to all waiting clients.
 *
 * @param ImageDownloaded Downloaded firmware version/name
 * @param DLpath Download path or status indicator
 * @param ctx Task context
 */
void complete_Download_waiting_tasks(const gchar *ImageDownloaded, const gchar *DLpath, TaskContext *ctx) {
	SWLOG_INFO("Completing %d waiting DownloadFW tasks\n", g_slist_length(waiting_download_ids));
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
	IsDownloadInProgress = FALSE;
	SWLOG_INFO("All Download waiting tasks completed !!\n");
}

/**
 * OBSOLETE FUNCTION - Kept for reference only.
 * Previously used as GLib timeout callback for completing CheckUpdate tasks.
 * Replaced by async_xconf_fetch_complete() in GTask-based async architecture.
 */
#if 0
static gboolean CheckUpdate_complete_callback(gpointer user_data) {
	TaskContext *ctx = (TaskContext *)user_data;
	SWLOG_INFO("In CheckUpdate_complete_callback\n");
	complete_CheckUpdate_waiting_tasks(ctx);
	SWLOG_INFO(" back from complete_CheckUpdate_waiting_tasks\n");
	return G_SOURCE_REMOVE;  // Don't repeat this timeout
}
#endif

/**
 * @brief GLib timeout callback for completing download tasks.
 *
 * PLACEHOLDER - Will be replaced with proper async download implementation.
 *
 * @param user_data TaskContext pointer
 * @return G_SOURCE_REMOVE (one-shot callback)
 */
#if 0
static gboolean Download_complete_callback(gpointer user_data) {
	TaskContext *ctx = (TaskContext *)user_data;
	SWLOG_INFO("In Download_complete_callback\n");
	complete_Download_waiting_tasks("SKY_DownloadedVersion.bin", "YES", ctx);
	SWLOG_INFO(" back from complete_CheckUpdate_waiting_tasks\n");
	return G_SOURCE_REMOVE;
}
#endif
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

#if 0
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
#endif

/**
 * @brief Main D-Bus method call handler - dispatches all client requests.
 *
 * GDBus callback invoked when clients call methods on org.rdkfwupdater.Interface.
 * Dispatches to appropriate handler based on method name:
 * - RegisterProcess: Client registration
 * - UnregisterProcess: Client cleanup
 * - CheckForUpdate: Firmware update check (cache-first, async fallback)
 * - DownloadFirmware: Firmware download (placeholder)
 * - UpdateFirmware: Firmware flash and reboot (placeholder)
 *
 * @param rdkv_conn_dbus D-Bus connection
 * @param rdkv_req_caller_id Unique D-Bus sender name (e.g., ":1.23")
 * @param rdkv_req_obj_path Object path ("/org/rdkfwupdater/Service")
 * @param rdkv_req_iface_name Interface name ("org.rdkfwupdater.Interface")
 * @param rdkv_req_method Method name ("CheckForUpdate", "RegisterProcess", etc.)
 * @param rdkv_req_payload Method parameters as GVariant
 * @param resp_ctx Method invocation context for sending reply
 * @param rdkv_user_ctx User data (unused)
 */
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
		SWLOG_INFO("[CHECK_UPDATE] D-Bus Path: /org/rdkfwupdater/Service\n");
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
				case 0: SWLOG_INFO(" (UPDATE_AVAILABLE)\n"); break;
				case 1: SWLOG_INFO(" (UPDATE_NOT_AVAILABLE)\n"); break;
				case 2: SWLOG_INFO(" (UPDATE_ERROR)\n"); break;
				default: SWLOG_INFO(" (UNKNOWN)\n"); break;
			}
			SWLOG_INFO("[CHECK_UPDATE]   - Current Version: '%s'\n", 
			           response.current_img_version ? response.current_img_version : "N/A");
			SWLOG_INFO("[CHECK_UPDATE]   - Available Version: '%s'\n", 
			           response.available_version ? response.available_version : "N/A");
			SWLOG_INFO("[CHECK_UPDATE]   - Status Message: '%s'\n", 
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
				NULL, "/org/rdkfwupdater/Service",
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
		SWLOG_INFO("  Async non-blocking fetch required\n");
		SWLOG_INFO("  Client flow:\n");
		SWLOG_INFO("    1. Gets UPDATE_ERROR (status=2) immediately\n");
		SWLOG_INFO("    2. Waits for CheckForUpdateComplete signal\n");
		SWLOG_INFO("    3. Receives real result when XConf fetch completes\n");
		
		// 4. SEND IMMEDIATE UPDATE_ERROR RESPONSE
		SWLOG_INFO("\nImmediate Response\n");
		SWLOG_INFO("  Sending: D-Bus method response\n");
		SWLOG_INFO("  Response: UPDATE_ERROR (status=2)\n");
		g_dbus_method_invocation_return_value(resp_ctx,
			g_variant_new("(ssssi)", "", "", "", "UPDATE_ERROR", 2));
		
		SWLOG_INFO("[CHECK_UPDATE] Response sent successfully\n");
		SWLOG_INFO("  Client now knows: Fetch in progress, wait for signal\n");
		SWLOG_INFO("  Note: D-Bus invocation consumed (cannot respond twice)\n");
		
		// 5. CREATE TASK AND ADD TO TRACKING
		SWLOG_INFO("\n Task Creation & Tracking\n");
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
		SWLOG_INFO("[CHECK_UPDATE]  Adding to 'active_tasks' hash table\n");
		g_hash_table_insert(active_tasks, GUINT_TO_POINTER(task_id), task_ctx);
		SWLOG_INFO("[CHECK_UPDATE]   Task-%d stored in active_tasks (size=%d)\n", 
		           task_id, g_hash_table_size(active_tasks));
		
		SWLOG_INFO("[CHECK_UPDATE]  Adding to 'waiting_checkUpdate_ids' queue\n");
		waiting_checkUpdate_ids = g_slist_append(waiting_checkUpdate_ids, GUINT_TO_POINTER(task_id));
		SWLOG_INFO("[CHECK_UPDATE]   Task-%d queued in waiting list (size=%d)\n", 
		           task_id, g_slist_length(waiting_checkUpdate_ids));
		
		SWLOG_INFO("[CHECK_UPDATE] Tracking Summary:\n");
		SWLOG_INFO("[CHECK_UPDATE]   'active_tasks' stores full TaskContext (for cleanup)\n");
		SWLOG_INFO("[CHECK_UPDATE]   'waiting_checkUpdate_ids' stores task IDs (for batch processing)\n");
		SWLOG_INFO("[CHECK_UPDATE]   Both cleaned up together when XConf fetch completes\n");
		
		// 6. CHECK IF FETCH ALREADY IN PROGRESS
		SWLOG_INFO("\n Fetch Status Check\n");
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
			SWLOG_INFO("[CHECK_UPDATE]   Signal broadcast at: /org/rdkfwupdater/Service\n");
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
		SWLOG_INFO("[CHECK_UPDATE]   Input handler_process_name: '%s' (ptr=%p)\n",
		           handler_process_name ? handler_process_name : "NULL", handler_process_name);
		
		// Validate handler_process_name before proceeding
		if (!handler_process_name) {
			SWLOG_ERROR("[CHECK_UPDATE] CRITICAL: handler_process_name is NULL!\n");
			SWLOG_ERROR("[CHECK_UPDATE] Cannot create async context - aborting fetch\n");
			IsCheckUpdateInProgress = FALSE;
			return;
		}
		
		AsyncXconfFetchContext *async_ctx = g_new0(AsyncXconfFetchContext, 1);
		
		// Verify allocation succeeded
		if (!async_ctx) {
			SWLOG_ERROR("[CHECK_UPDATE] CRITICAL: Failed to allocate AsyncXconfFetchContext!\n");
			SWLOG_ERROR("[CHECK_UPDATE] aborting fetch\n");
			IsCheckUpdateInProgress = FALSE;
			return;
		}
		
		SWLOG_INFO("[CHECK_UPDATE]   Allocated context at: %p\n", async_ctx);
		
		async_ctx->handler_id = g_strdup(handler_process_name);
		
		// Verify string duplication succeeded
		if (!async_ctx->handler_id) {
			SWLOG_ERROR("[CHECK_UPDATE] CRITICAL: g_strdup(handler_process_name) returned NULL!\n");
			SWLOG_ERROR("[CHECK_UPDATE] Out of memory - cleaning up and aborting\n");
			g_free(async_ctx);
			IsCheckUpdateInProgress = FALSE;
			return;
		}
		
		SWLOG_INFO("[CHECK_UPDATE]   Duplicated handler_id: '%s' (ptr=%p)\n",
		           async_ctx->handler_id, async_ctx->handler_id);
		
		// Validate connection
		if (!connection) {
			SWLOG_ERROR("[CHECK_UPDATE] CRITICAL: connection parameter is NULL!\n");
			SWLOG_ERROR("[CHECK_UPDATE] Cannot proceed without D-Bus connection\n");
			g_free(async_ctx->handler_id);
			g_free(async_ctx);
			IsCheckUpdateInProgress = FALSE;
			return;
		}
		
		async_ctx->connection = connection;
		SWLOG_INFO("[CHECK_UPDATE]   Handler ID: '%s'\n", handler_process_name);
		SWLOG_INFO("[CHECK_UPDATE]   Connection: %p\n", (void*)connection);
		
		SWLOG_INFO("[CHECK_UPDATE] Creating GTask:\n");
		SWLOG_INFO("[CHECK_UPDATE]   Worker function: rdkfw_xconf_fetch_worker\n");
		SWLOG_INFO("[CHECK_UPDATE]   (runs in separate thread - blocking I/O OK)\n");
		SWLOG_INFO("[CHECK_UPDATE]   Completion CB: rdkfw_xconf_fetch_done\n");
		SWLOG_INFO("[CHECK_UPDATE]   (runs on main loop - broadcasts signal)\n");
		GTask *task = g_task_new(NULL, NULL, rdkfw_xconf_fetch_done, async_ctx);
		
		// Verify GTask creation succeeded
		if (!task) {
			SWLOG_ERROR("[CHECK_UPDATE] CRITICAL: g_task_new() returned NULL!\n");
			SWLOG_ERROR("[CHECK_UPDATE] Failed to create GTask - cleaning up\n");
			g_free(async_ctx->handler_id);
			g_free(async_ctx);
			IsCheckUpdateInProgress = FALSE;
			return;
		}
		
		// Set task data for worker thread to access
		// The 4th parameter of g_task_new() goes to completion callback, not worker.
		// NULL destroy function because completion callback manually frees ctx
		SWLOG_INFO("[CHECK_UPDATE] Setting task_data for worker thread: %p\n", async_ctx);
		g_task_set_task_data(task, async_ctx, NULL);  // NULL: manual cleanup in completion callback
		
		SWLOG_INFO("[CHECK_UPDATE] Launching GTask...\n");
		g_task_run_in_thread(task, rdkfw_xconf_fetch_worker);
		g_object_unref(task);
		SWLOG_INFO("[CHECK_UPDATE] Worker thread spawned\n");
		
		SWLOG_INFO("[CHECK_UPDATE] Async Flow Summary:\n");
		SWLOG_INFO("[CHECK_UPDATE]   [Worker Thread] Fetches from XConf (blocking network I/O)\n");
		SWLOG_INFO("[CHECK_UPDATE]   [Main Loop] Continues processing D-Bus (non-blocking)\n");
		SWLOG_INFO("[CHECK_UPDATE]   [Waiting Queue] Task-%d + %d other(s) = %d total\n", 
		           task_id, g_slist_length(waiting_checkUpdate_ids) - 1,
		           g_slist_length(waiting_checkUpdate_ids));
		SWLOG_INFO("[CHECK_UPDATE]   [On Completion] Callback runs on main loop\n");
		SWLOG_INFO("[CHECK_UPDATE]   [Signal] Broadcast to /org/rdkfwupdater/Service\n");
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

	/* ====================================================================
	 * DOWNLOAD FIRMWARE REQUEST - Async GTask-based Implementation
	 * ====================================================================
	 * This handler implements the following features:
	 * 1. Comprehensive input validation (handler_id, firmware_name, etc.)
	 * 2. File caching check (skip download if already cached)
	 * 3. Piggyback support (multiple clients can join same download)
	 * 4. GTask worker thread for blocking download operation
	 * 5. Progress signals via g_idle_add() for thread-safe emission
	 * 6. Robust error handling and cleanup
	 * 7. Real-time progress updates via curl's CURLOPT_XFERINFOFUNCTION callback
	 * ==================================================================*/
	else if (g_strcmp0(rdkv_req_method, "DownloadFirmware") == 0) {
		SWLOG_INFO("[DOWNLOADFIRMWARE] ========== NEW DOWNLOAD REQUEST ==========\n");
		SWLOG_INFO("[DOWNLOADFIRMWARE] Timestamp: %ld\n", (long)time(NULL));
		SWLOG_INFO("[DOWNLOADFIRMWARE] D-Bus Sender: %s\n", rdkv_req_caller_id ? rdkv_req_caller_id : "NULL");
		SWLOG_INFO("[DOWNLOADFIRMWARE] Daemon State: IsDownloadInProgress=%s, Registered=%d\n", 
		           IsDownloadInProgress ? "YES" : "NO", g_hash_table_size(registered_processes));
		if (IsDownloadInProgress && current_download) {
			SWLOG_INFO("[DOWNLOADFIRMWARE] Current Download: %s (progress=%d%%, status=%d, waiting_clients=%d)\n", 
			           current_download->firmware_name ? current_download->firmware_name : "NULL",
			           current_download->current_progress, current_download->status,
			           g_slist_length(current_download->waiting_handler_ids));
			g_dbus_method_invocation_return_value(resp_ctx,
					g_variant_new("(sss)",
                                        "RDKFW_DWNL_FAILED",
                                        "DWNL_ERROR",
                                        "There is an Ongoing Firmware Download"));
                        return;


		}
		
		// NULL CHECKS: Critical pointers
		SWLOG_INFO("[DOWNLOADFIRMWARE] Validating critical pointers...\n");
		
		if (!resp_ctx) {
			SWLOG_ERROR("[DOWNLOADFIRMWARE] CRITICAL: resp_ctx is NULL, cannot send response!\n");
			return;
		}
		
		if (!rdkv_req_payload) {
			SWLOG_ERROR("[DOWNLOADFIRMWARE] CRITICAL: rdkv_req_payload is NULL!\n");
			g_dbus_method_invocation_return_value(resp_ctx,
				g_variant_new("(sss)", 
					"RDKFW_DWNL_FAILED",
					"DWNL_ERROR",
					"Internal error: request payload is missing"));
			return;
		}
		
		if (!rdkv_conn_dbus) {
			SWLOG_ERROR("[DOWNLOADFIRMWARE] CRITICAL: D-Bus connection is NULL!\n");
			g_dbus_method_invocation_return_value(resp_ctx,
				g_variant_new("(sss)", 
					"RDKFW_DWNL_FAILED",
					"DWNL_ERROR",
					"Internal error: D-Bus connection unavailable"));
			return;
		}
		
		// ========== EXTRACT INPUT PARAMETERS ==========
		
		gchar *handler_id_str = NULL;
		gchar *firmware_name = NULL;
		gchar *download_url = NULL;
		gchar *type_of_firmware = NULL;
		
		// Parse D-Bus parameters: (s handlerId, s firmwareName, s downloadUrl, s typeOfFirmware)
		g_variant_get(rdkv_req_payload, "(ssss)", 
		              &handler_id_str, 
		              &firmware_name, 
		              &download_url, 
		              &type_of_firmware);
		
		SWLOG_INFO("[DOWNLOADFIRMWARE] Input parameters:\n");
		SWLOG_INFO("[DOWNLOADFIRMWARE]   handler_id: '%s'\n", handler_id_str ? handler_id_str : "NULL");
		SWLOG_INFO("[DOWNLOADFIRMWARE]   firmware_name: '%s'\n", firmware_name ? firmware_name : "NULL");
		SWLOG_INFO("[DOWNLOADFIRMWARE]   download_url: '%s'\n", 
		           download_url && strlen(download_url) > 0 ? download_url : "(empty - will use XConf)");
		SWLOG_INFO("[DOWNLOADFIRMWARE]   type_of_firmware: '%s'\n", type_of_firmware ? type_of_firmware : "NULL");
		
		// ========== VALIDATION PHASE ==========
		
		SWLOG_INFO("[DOWNLOADFIRMWARE] Starting validation...\n");
	
	        if (!handler_id_str || !strlen(handler_id_str) || !firmware_name   || !strlen(firmware_name)   || !download_url    || !strlen(download_url)|| !type_of_firmware || !strlen(type_of_firmware)) {
			SWLOG_ERROR("[DOWNLOADFIRMWARE] Invalid input. One or more fields are empty or NULL\n");
			g_dbus_method_invocation_return_value(resp_ctx,
                                g_variant_new("(sss)",
                                        "RDKFW_DWNL_FAILED",
                                        "DWNL_ERROR",
                                        "one more inputs are empty/invalid"));
                        g_free(handler_id_str);
                        g_free(firmware_name);
                        g_free(download_url);
                        g_free(type_of_firmware);
			return ;   
		}	
		// 1. Validate handler ID (not NULL, not empty)
		if (!handler_id_str || strlen(handler_id_str) == 0) {
			SWLOG_ERROR("[DOWNLOADFIRMWARE] REJECTED: Invalid handler ID (NULL or empty)\n");
			g_dbus_method_invocation_return_value(resp_ctx,
				g_variant_new("(sss)", 
					"RDKFW_DWNL_FAILED",
					"DWNL_ERROR",
					"Invalid handler ID"));
			g_free(handler_id_str);
			g_free(firmware_name);
			g_free(download_url);
			g_free(type_of_firmware);
			return;
		}
		
		// 2. Convert handler_id string to numeric
		guint64 handler_id_numeric = g_ascii_strtoull(handler_id_str, NULL, 10);
		if (handler_id_numeric == 0) {
			SWLOG_ERROR("[DOWNLOADFIRMWARE] REJECTED: Invalid handler ID format\n");
			g_dbus_method_invocation_return_value(resp_ctx,
				g_variant_new("(sss)", 
					"RDKFW_DWNL_FAILED",
					"DWNL_ERROR",
					"Invalid handler ID format"));
			g_free(handler_id_str);
			g_free(firmware_name);
			g_free(download_url);
			g_free(type_of_firmware);
			return;
		}
	        	
		// 3. Check registration
		gboolean is_registered = g_hash_table_contains(registered_processes, 
		                                                GINT_TO_POINTER(handler_id_numeric));
		if (!is_registered) {
			SWLOG_ERROR("[DOWNLOADFIRMWARE] REJECTED: Handler %"G_GUINT64_FORMAT" not registered\n", 
			           handler_id_numeric);
			g_dbus_method_invocation_return_value(resp_ctx,
				g_variant_new("(sss)", 
					"RDKFW_DWNL_FAILED",
					"DWNL_ERROR",
					"Handler not registered"));
			g_free(handler_id_str);
			g_free(firmware_name);
			g_free(download_url);
			g_free(type_of_firmware);
			return;
		}
		
		// 4. Validate firmware name (Scenario 3)
		if (!firmware_name || strlen(firmware_name) == 0) {
			SWLOG_ERROR("[DOWNLOADFIRMWARE] REJECTED: Invalid firmware name (NULL or empty)\n");
			g_dbus_method_invocation_return_value(resp_ctx,
				g_variant_new("(sss)", 
					"RDKFW_DWNL_FAILED",
					"DWNL_ERROR",
					"Invalid firmware name"));
			g_free(handler_id_str);
			g_free(firmware_name);
			g_free(download_url);
			g_free(type_of_firmware);
			return;
		}
		
		// 5. Validate download URL (if custom URL provided, it must be non-empty)
		if (download_url && strlen(download_url) > 0) {
			// Custom URL provided - validate it's a valid URL format
			if (!g_str_has_prefix(download_url, "http://") && 
			    !g_str_has_prefix(download_url, "https://")) {
				SWLOG_ERROR("[DOWNLOADFIRMWARE] REJECTED: Invalid download URL format\n");
				g_dbus_method_invocation_return_value(resp_ctx,
					g_variant_new("(sss)", 
						"RDKFW_DWNL_FAILED",
						"DWNL_ERROR",
						"Invalid download URL format"));
				g_free(handler_id_str);
				g_free(firmware_name);
				g_free(download_url);
				g_free(type_of_firmware);
				return;
			}
		}
		if (IsDownloadInProgress && current_download && g_strcmp0(current_download->firmware_name, firmware_name) != 0) {
			SWLOG_INFO("[DOWNLOADFIRMWARE] Current Download: %s (progress=%d%%, status=%d, waiting_clients=%d)\n",
					current_download->firmware_name ? current_download->firmware_name : "NULL",
					current_download->current_progress, current_download->status,
					g_slist_length(current_download->waiting_handler_ids));
			SWLOG_INFO("[DOWNLOADFIRMWARE] REJECTING DOWNLOAD REQUEST: Already downloading '%s', but new request is for '%s\n",current_download->firmware_name,firmware_name);
			g_dbus_method_invocation_return_value(resp_ctx,
					g_variant_new("(sss)",
						"RDKFW_DWNL_FAILED",
						"DWNL_ERROR",
						"There is an ongoing Download for another image"));
			g_free(handler_id_str);
			g_free(firmware_name);
			g_free(download_url);
			g_free(type_of_firmware);
			return;
		}
		
		SWLOG_INFO("[DOWNLOADFIRMWARE] All validations passed\n");
		SWLOG_INFO("[DOWNLOADFIRMWARE]   Handler ID (validated): %"G_GUINT64_FORMAT"\n", handler_id_numeric);
		
		// ========== CHECK FOR CACHED FILE (Scenario 8) ==========
		
		SWLOG_INFO("[DOWNLOADFIRMWARE] Checking for cached file...\n");
		gchar *cache_path = g_strdup_printf("/opt/CDL/%s", firmware_name);  // MADHU - check if this is the path always to download image
		SWLOG_INFO("[DOWNLOADFIRMWARE]   Cache path: %s\n", cache_path);
		
		if (g_file_test(cache_path, G_FILE_TEST_EXISTS)) {
			SWLOG_INFO("[DOWNLOADFIRMWARE] File already cached!\n");
			SWLOG_INFO("[DOWNLOADFIRMWARE] Returning SUCCESS with COMPLETED status immediately\n");
			
			g_free(cache_path);
			
			// Return success to D-Bus caller
			g_dbus_method_invocation_return_value(resp_ctx,
				g_variant_new("(sss)", 
					"RDKFW_DWNL_SUCCESS",
					"COMPLETED",
					"Firmware already downloaded"));
			
			// Emit signal with COMPLETED status
			ProgressUpdate *update = g_new0(ProgressUpdate, 1);
			if (update) {
				update->progress = 100;
				update->status = FW_DWNL_COMPLETED;
				update->handler_id = g_strdup(handler_id_str);
				update->firmware_name = g_strdup(firmware_name);
				update->connection = rdkv_conn_dbus;
				g_idle_add(rdkfw_emit_download_progress, update);
			}
			
			// Cleanup
			g_free(handler_id_str);
			g_free(firmware_name);
			g_free(download_url);
			g_free(type_of_firmware);
			
			SWLOG_INFO("[DOWNLOADFIRMWARE] CACHED FILE scenario complete\n");
			SWLOG_INFO("[DOWNLOADFIRMWARE] ========== REQUEST COMPLETE (CACHED) ==========\n\n");
			return;
		}
		
		g_free(cache_path);
		SWLOG_INFO("[DOWNLOADFIRMWARE] File not cached, will download\n");
		
		// ========== CHECK FOR IN-PROGRESS DOWNLOAD (Scenario 2 - Piggyback) ==========
		
		if (IsDownloadInProgress && current_download && 
		    g_strcmp0(current_download->firmware_name, firmware_name) == 0) {
			
			SWLOG_INFO("[DOWNLOADFIRMWARE] *** PIGGYBACK SCENARIO ***\n");
			SWLOG_INFO("[DOWNLOADFIRMWARE] Download already in progress for: %s\n", firmware_name);
			SWLOG_INFO("[DOWNLOADFIRMWARE]   Current progress: %d%%\n", current_download->current_progress);
			SWLOG_INFO("[DOWNLOADFIRMWARE]   Current status: %d\n", current_download->status);
			SWLOG_INFO("[DOWNLOADFIRMWARE]   Existing waiting clients: %d\n", 
			           g_slist_length(current_download->waiting_handler_ids));
			
			// Add this client to waiting list
			current_download->waiting_handler_ids = g_slist_append(
				current_download->waiting_handler_ids, 
				g_strdup(handler_id_str)
			);
			
			SWLOG_INFO("[DOWNLOADFIRMWARE] Client added to waiting list\n");
			SWLOG_INFO("[DOWNLOADFIRMWARE]   Total waiting clients: %d\n", 
			           g_slist_length(current_download->waiting_handler_ids));
			
			// Return success with INPROGRESS status
			g_dbus_method_invocation_return_value(resp_ctx,
				g_variant_new("(sss)", 
					"RDKFW_DWNL_SUCCESS",
					"INPROGRESS",
					"Download already in progress"));
			
			// Emit current progress signal to this client
			ProgressUpdate *update = g_new0(ProgressUpdate, 1);
			if (update) {
				update->progress = current_download->current_progress;
				update->status = FW_DWNL_INPROGRESS;
				update->handler_id = g_strdup(handler_id_str);
				update->firmware_name = g_strdup(firmware_name);
				update->connection = rdkv_conn_dbus;
				g_idle_add(rdkfw_emit_download_progress, update);
			}
			
			// Cleanup
			g_free(handler_id_str);
			g_free(firmware_name);
			g_free(download_url);
			g_free(type_of_firmware);
			
			SWLOG_INFO("[DOWNLOADFIRMWARE] PIGGYBACK scenario complete\n");
			SWLOG_INFO("[DOWNLOADFIRMWARE] ========== REQUEST COMPLETE (PIGGYBACK) ==========\n\n");
			return;
		}
		
		// ========== START NEW DOWNLOAD (Scenario 1) ==========
		
		SWLOG_INFO("[DOWNLOADFIRMWARE] *** STARTING NEW DOWNLOAD ***\n");
		SWLOG_INFO("[DOWNLOADFIRMWARE] This is the first client for this firmware\n");
		
		// Set progress flag
		IsDownloadInProgress = TRUE;
		SWLOG_INFO("[DOWNLOADFIRMWARE] IsDownloadInProgress = TRUE\n");
		
		// Initialize global download state
		current_download = g_new0(CurrentDownloadState, 1);
		if (!current_download) {
			SWLOG_ERROR("[DOWNLOADFIRMWARE] CRITICAL: Failed to allocate download state!\n");
			IsDownloadInProgress = FALSE;
			g_dbus_method_invocation_return_value(resp_ctx,
				g_variant_new("(sss)", 
					"RDKFW_DWNL_FAILED",
					"DWNL_ERROR",
					"Internal error: memory allocation failed"));
			g_free(handler_id_str);
			g_free(firmware_name);
			g_free(download_url);
			g_free(type_of_firmware);
			return;
		}
		
		current_download->firmware_name = g_strdup(firmware_name);
		current_download->current_progress = 0;
		current_download->status = FW_DWNL_INPROGRESS;
		current_download->waiting_handler_ids = g_slist_append(NULL, g_strdup(handler_id_str));
		
		SWLOG_INFO("[DOWNLOADFIRMWARE] Download state initialized\n");
		SWLOG_INFO("[DOWNLOADFIRMWARE]   Firmware: %s\n", current_download->firmware_name);
		SWLOG_INFO("[DOWNLOADFIRMWARE]   Initial progress: 0%%\n");
		SWLOG_INFO("[DOWNLOADFIRMWARE]   Waiting clients: 1\n");
		
		// Create async context for worker thread
		AsyncDownloadContext *async_ctx = g_new0(AsyncDownloadContext, 1);
		if (!async_ctx) {
			SWLOG_ERROR("[DOWNLOADFIRMWARE] CRITICAL: Failed to allocate async context!\n");
			
			// Cleanup download state
			if (current_download->firmware_name) g_free(current_download->firmware_name);
			g_slist_free_full(current_download->waiting_handler_ids, g_free);
			g_free(current_download);
			current_download = NULL;
			IsDownloadInProgress = FALSE;
			
			g_dbus_method_invocation_return_value(resp_ctx,
				g_variant_new("(sss)", 
					"RDKFW_DWNL_FAILED",
					"DWNL_ERROR",
					"Internal error: memory allocation failed"));
			g_free(handler_id_str);
			g_free(firmware_name);
			g_free(download_url);
			g_free(type_of_firmware);
			return;
		}
		
		async_ctx->handler_id = g_strdup(handler_id_str);
		async_ctx->firmware_name = g_strdup(firmware_name);
		async_ctx->download_url = g_strdup(download_url);
		async_ctx->type_of_firmware = g_strdup(type_of_firmware);
		async_ctx->connection = rdkv_conn_dbus;
		
		// Verify all critical fields were allocated
		if (!async_ctx->handler_id || !async_ctx->firmware_name || 
		    !async_ctx->download_url || !async_ctx->type_of_firmware) {
			SWLOG_ERROR("[DOWNLOADFIRMWARE] CRITICAL: Failed to duplicate context fields!\n");
			
			// Cleanup async context
			if (async_ctx->handler_id) g_free(async_ctx->handler_id);
			if (async_ctx->firmware_name) g_free(async_ctx->firmware_name);
			if (async_ctx->download_url) g_free(async_ctx->download_url);
			if (async_ctx->type_of_firmware) g_free(async_ctx->type_of_firmware);
			g_free(async_ctx);
			
			// Cleanup download state
			if (current_download->firmware_name) g_free(current_download->firmware_name);
			g_slist_free_full(current_download->waiting_handler_ids, g_free);
			g_free(current_download);
			current_download = NULL;
			IsDownloadInProgress = FALSE;
			
			g_dbus_method_invocation_return_value(resp_ctx,
				g_variant_new("(sss)", 
					"RDKFW_DWNL_FAILED",
					"DWNL_ERROR",
					"Internal error: memory allocation failed"));
			g_free(handler_id_str);
			g_free(firmware_name);
			g_free(download_url);
			g_free(type_of_firmware);
			return;
		}
		
		SWLOG_INFO("[DOWNLOADFIRMWARE] Async context created\n");
		SWLOG_INFO("[DOWNLOADFIRMWARE]   handler_id: %s\n", async_ctx->handler_id);
		SWLOG_INFO("[DOWNLOADFIRMWARE]   firmware_name: %s\n", async_ctx->firmware_name);
		SWLOG_INFO("[DOWNLOADFIRMWARE]   download_url: %s\n", async_ctx->download_url);
		SWLOG_INFO("[DOWNLOADFIRMWARE]   type_of_firmware: %s\n", async_ctx->type_of_firmware);
		
		// Create GTask for async execution
		SWLOG_INFO("[DOWNLOADFIRMWARE] Creating GTask for worker thread...\n");
		GTask *task = g_task_new(NULL, NULL, rdkfw_download_done, async_ctx);
		if (!task) {
			SWLOG_ERROR("[DOWNLOADFIRMWARE] CRITICAL: Failed to create GTask!\n");
			
			// Cleanup async context
			g_free(async_ctx->handler_id);
			g_free(async_ctx->firmware_name);
			g_free(async_ctx->download_url);
			g_free(async_ctx->type_of_firmware);
			g_free(async_ctx);
			
			// Cleanup download state
			if (current_download->firmware_name) g_free(current_download->firmware_name);
			g_slist_free_full(current_download->waiting_handler_ids, g_free);
			g_free(current_download);
			current_download = NULL;
			IsDownloadInProgress = FALSE;
			
			g_dbus_method_invocation_return_value(resp_ctx,
				g_variant_new("(sss)", 
					"RDKFW_DWNL_FAILED",
					"DWNL_ERROR",
					"Internal error: failed to create async task"));
			g_free(handler_id_str);
			g_free(firmware_name);
			g_free(download_url);
			g_free(type_of_firmware);
			return;
		}
		
		g_task_set_task_data(task, async_ctx, NULL);
		
		SWLOG_INFO("[DOWNLOADFIRMWARE] GTask created successfully\n");
		SWLOG_INFO("[DOWNLOADFIRMWARE] Spawning worker thread...\n");
		
		// Spawn worker thread
		g_task_run_in_thread(task, rdkfw_download_worker);
		g_object_unref(task);
		
		SWLOG_INFO("[DOWNLOADFIRMWARE] Worker thread spawned\n");
		
		// Return success immediately (download will continue in background)
		g_dbus_method_invocation_return_value(resp_ctx,
			g_variant_new("(sss)", 
				"RDKFW_DWNL_SUCCESS",
				"INPROGRESS",
				"Download started successfully"));
		
		SWLOG_INFO("[DOWNLOADFIRMWARE] SUCCESS response sent to client\n");
		SWLOG_INFO("[DOWNLOADFIRMWARE] Worker thread is now running in background\n");
		
		// Cleanup original strings (copies made for async_ctx)
		g_free(handler_id_str);
		g_free(firmware_name);
		g_free(download_url);
		g_free(type_of_firmware);
		
		SWLOG_INFO("[DOWNLOADFIRMWARE] ========== REQUEST ACCEPTED (NEW DOWNLOAD) ==========\n\n");
	}

	/* UPGRADE REQUEST - */
	/* ====================================================================
         * UPDATE FIRMWARE REQUEST - Async GTask-based Implementation
         * ====================================================================
         * This handler implements the following features:
         * 1. Comprehensive input validation (handler_id, firmware_name, etc.)
         * 2. Check if there is an ongoing flash - based on isUpdateInProgress
         * 3. Piggyback support (multiple clients gets the update progress)
         * 4. GTask worker thread for blocking Flash operation
         * 5. Progress signals via g_idle_add() for thread-safe emission
         * 6. Robust error handling and cleanup
         * ==================================================================*/
	else if (g_strcmp0(rdkv_req_method, "UpdateFirmware") == 0) {
		SWLOG_INFO("[UPDATEFIRMWARE] ========== NEW FLASH REQUEST ==========\n");
		SWLOG_INFO("[UPDATEFIRMWARE] Timestamp: %ld\n", (long)time(NULL));
		SWLOG_INFO("[UPDATEFIRMWARE] D-Bus Sender: %s\n", rdkv_req_caller_id ? rdkv_req_caller_id : "NULL");
		SWLOG_INFO("[UPDATEFIRMWARE] Daemon State: IsFlashInProgress=%s, Registered=%d\n",
                            IsFlashInProgress ? "YES" : "NO", g_hash_table_size(registered_processes));
		
		if (IsDownloadInProgress) {
			SWLOG_INFO("[UPDATEFIRMWARE] There is an Ongoing Firmware Download\n");
			SWLOG_INFO("[UPDATEFIRMWARE] Cannot Flash the device now.Try after sometime\n");
			g_dbus_method_invocation_return_value(resp_ctx,
					g_variant_new("(sss)",
				        "RDKFW_UPDATE_FAILED",                                                                              
                                        "UPDATE_ERROR",                                                                                     
                                        "On going Firmware Download"));
			return;	

		}

		if (IsFlashInProgress) {
			// TODO  - MADHU: HAVE TO CHECK HOW THE PERCENTAGE OF PROGRESS CAN BE READ ; FOR NOW NULL 
                        SWLOG_INFO("[UPDATEFIRMWARE] Current Flash: %s (progress=%d%%, status=%d)\n",
                                   current_flash->firmware_name ? current_flash->firmware_name : "NULL",
                                   current_flash->current_progress, current_flash->status);
			g_dbus_method_invocation_return_value(resp_ctx,
					g_variant_new("(sss)",
                                        "RDKFW_UPDATE_FAILED",
                                        "UPDATE_ERROR",
                                        "On going Flash Firmware"));
                        return;

                }

		// NULL CHECKS: Critical pointers
                SWLOG_INFO("[UPDATEFIRMWARE] Validating critical pointers...\n");

                if (!resp_ctx) {
                        SWLOG_ERROR("[UPDATEFIRMWARE] CRITICAL: resp_ctx is NULL, cannot send response!\n");
                        return;
                }

                if (!rdkv_req_payload) {
                        SWLOG_ERROR("[UPDATEFIRMWARE] CRITICAL: rdkv_req_payload is NULL!\n");
                        g_dbus_method_invocation_return_value(resp_ctx,
                                g_variant_new("(sss)",
                                        "RDKFW_UPDATE_FAILED",
                                        "UPDATE_ERROR",
                                        "Internal error: request payload is missing"));
                        return;
                }

                if (!rdkv_conn_dbus) {
                        SWLOG_ERROR("[UPDATEFIRMWARE] CRITICAL: D-Bus connection is NULL!\n");
                        g_dbus_method_invocation_return_value(resp_ctx,
                                g_variant_new("(sss)",
                                        "RDKFW_UPDATE_FAILED",
                                        "UPDATE_ERROR",
                                        "Internal error: D-Bus connection unavailable"));
                        return;
                }

		// ========== EXTRACT INPUT PARAMETERS ==========
		gchar *handler_id_str = NULL;
		gchar *firmware_name = NULL;
                gchar *loc_of_firmware = NULL;
                gchar *type_of_firmware = NULL;
                gchar *rebootImmediately = NULL;

                // Parse D-Bus parameters: (s handlerId, s firmwareName, s loc_of_firmware, s typeOfFirmware, s rebootImmediately)
                g_variant_get(rdkv_req_payload, "(sssss)",
                              &handler_id_str,
                              &firmware_name,
                              &loc_of_firmware,
			      &type_of_firmware,
                              &rebootImmediately);

                SWLOG_INFO("[UPDATEFIRMWARE] Input parameters:\n");
                SWLOG_INFO("[UPDATEFIRMWARE]   handler_id: '%s'\n", handler_id_str ? handler_id_str : "NULL");
                SWLOG_INFO("[UPDATEFIRMWARE]   firmware_name: '%s'\n", firmware_name ? firmware_name : "NULL");
                SWLOG_INFO("[UPDATEFIRMWARE]   loc_of_firmware: '%s'\n",loc_of_firmware && strlen(loc_of_firmware) > 0 ? loc_of_firmware : "(empty)");
                SWLOG_INFO("[UPDATEFIRMWARE]   type_of_firmware: '%s'\n", type_of_firmware ? type_of_firmware : "NULL");
                SWLOG_INFO("[UPDATEFIRMWARE]   rebootImmediately: '%s'\n", rebootImmediately ? rebootImmediately : "NULL");


		
		// ========== VALIDATION PHASE ==========
		
		SWLOG_INFO("[UPDATEFIRMWARE] Starting validation...\n");
	
	        if (!handler_id_str || !strlen(handler_id_str) || !firmware_name   || !strlen(firmware_name)   || !loc_of_firmware    || !strlen(loc_of_firmware)|| !type_of_firmware || !strlen(type_of_firmware) || !rebootImmediately || !strlen(rebootImmediately)) {
			SWLOG_ERROR("[UPLOADFIRMWARE] Invalid input. One or more fields are empty or NULL\n");
			g_dbus_method_invocation_return_value(resp_ctx,
                                g_variant_new("(sss)",
                                        "RDKFW_UPDATE_FAILED",
                                        "UPDATE_ERROR",
                                        "one more inputs are empty/invalid"));
                        g_free(handler_id_str);
                        g_free(firmware_name);
                        g_free(loc_of_firmware);
                        g_free(type_of_firmware);
			g_free(rebootImmediately);
			return ;   
		}	
		// 1. Validate handler ID (not NULL, not empty)
		if (!handler_id_str || strlen(handler_id_str) == 0) {
			SWLOG_ERROR("[DOWNLOADFIRMWARE] REJECTED: Invalid handler ID (NULL or empty)\n");
			g_dbus_method_invocation_return_value(resp_ctx,
				g_variant_new("(sss)", 
					"RDKFW_UPDATE_FAILED",
					"UPDATE_ERROR",
					"Invalid handler ID"));
			g_free(handler_id_str);
			g_free(firmware_name);
			g_free(loc_of_firmware);
			g_free(type_of_firmware);
			 g_free(rebootImmediately);
			return;
		}
		
		// 2. Convert handler_id string to numeric
		guint64 handler_id_numeric = g_ascii_strtoull(handler_id_str, NULL, 10);
		if (handler_id_numeric == 0) {
			SWLOG_ERROR("[UPDATEFIRMWARE] REJECTED: Invalid handler ID format\n");
			g_dbus_method_invocation_return_value(resp_ctx,
				g_variant_new("(sss)", 
					"RDKFW_UPDATE_FAILED",
					"UPDATE_ERROR",
					"Invalid handler ID format"));
			g_free(handler_id_str);
			g_free(firmware_name);
			g_free(loc_of_firmware);
			g_free(type_of_firmware);
			 g_free(rebootImmediately);
			return;
		}
	        	
		// 3. Check registration
		gboolean is_registered = g_hash_table_contains(registered_processes, 
		                                                GINT_TO_POINTER(handler_id_numeric));
		if (!is_registered) {
			SWLOG_ERROR("[UPDATEFIRMWARE] REJECTED: Handler %"G_GUINT64_FORMAT" not registered\n", 
			           handler_id_numeric);
			g_dbus_method_invocation_return_value(resp_ctx,
				g_variant_new("(sss)", 
					"RDKFW_UPDATE_FAILED",
					"UPDATE_ERROR",
					"Handler not registered"));
			g_free(handler_id_str);
			g_free(firmware_name);
			g_free(loc_of_firmware);
			g_free(type_of_firmware);
			g_free(rebootImmediately);
			return;
		}
		
		// 4. Validate firmware name (Scenario 3)
		if (!firmware_name || strlen(firmware_name) == 0) {
			SWLOG_ERROR("[UPDATEFIRMWARE] REJECTED: Invalid firmware name (NULL or empty)\n");
			g_dbus_method_invocation_return_value(resp_ctx,
				g_variant_new("(sss)", 
					"RDKFW_UPDATE_FAILED",
					"UPDATE_ERROR",
					"Invalid firmware name"));
			g_free(handler_id_str);
			g_free(firmware_name);
			g_free(loc_of_firmware);
			g_free(type_of_firmware);
			g_free(rebootImmediately);
			return;
		}
		
		// 5. Validate location of firmware (if custom path provided, it must be non-empty)
		// If no custom path provided, use default firmware directory
		if (loc_of_firmware == NULL || strlen(loc_of_firmware) == 0) {
			loc_of_firmware = get_difw_path();
		}

		// Validate firmware directory was obtained
		if (loc_of_firmware == NULL || strlen(loc_of_firmware) == 0) {
			SWLOG_ERROR("[UPDATEFIRMWARE] REJECTED: Firmware directory path is empty or null\n");
			g_dbus_method_invocation_return_value(resp_ctx,
					g_variant_new("(sss)",
					"RDKFW_UPDATE_FAILED",
					"UPDATE_ERROR",
					"Firmware directory path is not available"));
			g_free(handler_id_str);
			g_free(firmware_name);
			g_free(loc_of_firmware);
			g_free(type_of_firmware);
			g_free(rebootImmediately);
			return;
		}
		// Validate firmware directory exists on the device
		if (!g_file_test(loc_of_firmware, G_FILE_TEST_IS_DIR)) {
			SWLOG_ERROR("[DOWNLOADFIRMWARE] REJECTED: Directory does not exist: %s\n", loc_of_firmware);
			gchar *error_msg = g_strdup_printf("Directory '%s' does not exist", loc_of_firmware);
			g_dbus_method_invocation_return_value(resp_ctx,
					g_variant_new("(sss)",
					"RDKFW_UPDATE_FAILED",
					"UPDATE_ERROR",
					error_msg));
			g_free(error_msg);
			g_free(handler_id_str);
			g_free(firmware_name);
			g_free(loc_of_firmware);
			g_free(type_of_firmware);
			g_free(rebootImmediately);
			return;
		}

		gchar *firmware_fullpath = g_build_filename(loc_of_firmware, firmware_name, NULL); // full path for the firmware file name i.e /opt/CDL/firmware.bin<
	        if (!g_file_test(firmware_fullpath, G_FILE_TEST_EXISTS)) {
		   SWLOG_ERROR("[UPDATEFIRMWARE] REJECTED: Firmware file not found: %s\n", firmware_fullpath);
		   gchar *error_msg = g_strdup_printf("'%s' is not present in '%s' path", firmware_name, loc_of_firmware);
		   g_dbus_method_invocation_return_value(resp_ctx,
				   g_variant_new("(sss)",
			           "RDKFW_UPDATE_FAILED",
				   "UPDATE_ERROR",
				   error_msg));
		   g_free(error_msg);
		   g_free(firmware_fullpath);
		   g_free(handler_id_str);
		   g_free(firmware_name);
		   g_free(loc_of_firmware);
		   g_free(type_of_firmware);
		   g_free(rebootImmediately);
		   return;
		}
		
		SWLOG_INFO("[UPDATEFIRMWARE] All validations passed\n");
		SWLOG_INFO("[UPDATEFIRMWARE]   Handler ID (validated): %"G_GUINT64_FORMAT"\n", handler_id_numeric);
		
		// ===== INITIALIZE FLASH STATE TRACKING =====
		SWLOG_INFO("[UPDATEFIRMWARE] Initializing global flash state tracker\n");
		
		current_flash = g_new0(CurrentFlashState, 1);
		current_flash->firmware_name = g_strdup(firmware_name);
		current_flash->current_progress = 0;
		current_flash->status = FW_UPDATE_INPROGRESS;
		IsFlashInProgress = TRUE;
		
		SWLOG_INFO("[UPDATEFIRMWARE] Flash state tracker created:\n");
		SWLOG_INFO("[UPDATEFIRMWARE]   - firmware_name: '%s'\n", current_flash->firmware_name);
		SWLOG_INFO("[UPDATEFIRMWARE]   - current_progress: %d%%\n", current_flash->current_progress);
		SWLOG_INFO("[UPDATEFIRMWARE]   - status: %d (INPROGRESS)\n", current_flash->status);
		SWLOG_INFO("[UPDATEFIRMWARE]   - IsFlashInProgress: TRUE\n");
		
		// ===== SEND IMMEDIATE SUCCESS RESPONSE =====
		SWLOG_INFO("[UPDATEFIRMWARE] Sending immediate SUCCESS response to client\n");
		
		g_dbus_method_invocation_return_value(resp_ctx,
			g_variant_new("(sss)", 
				"RDKFW_UPDATE_SUCCESS", 
				"UPDATE_IN_PROGRESS", 
				"Flash operation initiated"));
		
		SWLOG_INFO("[UPDATEFIRMWARE] SUCCESS response sent to client\n");
		SWLOG_INFO("[UPDATEFIRMWARE] Response details:\n");
		SWLOG_INFO("[UPDATEFIRMWARE]   - UpdateResult: 'RDKFW_UPDATE_SUCCESS'\n");
		SWLOG_INFO("[UPDATEFIRMWARE]   - UpdateStatus: 'NOTSTARTED'\n");
		SWLOG_INFO("[UPDATEFIRMWARE]   - message: 'Flash operation initiated'\n");
		SWLOG_INFO("[UPDATEFIRMWARE] Client should now listen for UpdateProgress signals:\n");
		SWLOG_INFO("[UPDATEFIRMWARE]   Signal: org.rdkfwupdater.Interface.UpdateProgress\n");
		SWLOG_INFO("[UPDATEFIRMWARE]   Expected sequence: 0%% → 25%% → 50%% → 75%% → 100%% (or -1%% for error)\n");
		
		// ===== CREATE WORKER THREAD CONTEXT =====
		SWLOG_INFO("[UPDATEFIRMWARE] Creating AsyncFlashContext for worker thread\n");
		
		AsyncFlashContext *flash_ctx = g_new0(AsyncFlashContext, 1);
		flash_ctx->connection = rdkv_conn_dbus;  // Borrowed pointer (do NOT free)
		flash_ctx->handler_id = handler_id_str;  // Ownership transferred (will be freed by worker)
		flash_ctx->firmware_name = firmware_name;  // Ownership transferred
		flash_ctx->firmware_type = type_of_firmware;  // Ownership transferred
		flash_ctx->firmware_fullpath = firmware_fullpath;  // Ownership transferred (already allocated)
		// Use placeholder URL to satisfy imageFlasher.sh validation
		// For mediaclient devices, this URL is not used for actual flashing
		flash_ctx->server_url = g_strdup("http://Dummy-xconf-server/firmware"); // Not required for using FlashApp. it is only passed for script validation
		flash_ctx->immediate_reboot = (g_strcmp0(rebootImmediately, "true") == 0);
		flash_ctx->trigger_type = 4;  // 4 = App triggered (D-Bus API call)
		flash_ctx->last_progress = 0;
		flash_ctx->operation_start_time = time(NULL);
		
		SWLOG_INFO("[UPDATEFIRMWARE] AsyncFlashContext created successfully:\n");
		SWLOG_INFO("[UPDATEFIRMWARE]   Context Details:\n");
		SWLOG_INFO("[UPDATEFIRMWARE]     - connection: %p (borrowed)\n", (void*)flash_ctx->connection);
		SWLOG_INFO("[UPDATEFIRMWARE]     - handler_id: '%s'\n", flash_ctx->handler_id);
		SWLOG_INFO("[UPDATEFIRMWARE]     - firmware_name: '%s'\n", flash_ctx->firmware_name);
		SWLOG_INFO("[UPDATEFIRMWARE]     - firmware_type: '%s'\n", flash_ctx->firmware_type);
		SWLOG_INFO("[UPDATEFIRMWARE]     - firmware_fullpath: '%s'\n", flash_ctx->firmware_fullpath);
		SWLOG_INFO("[UPDATEFIRMWARE]     - server_url: '%s'\n", flash_ctx->server_url);
		SWLOG_INFO("[UPDATEFIRMWARE]     - immediate_reboot: %s\n", 
		           flash_ctx->immediate_reboot ? "TRUE" : "FALSE");
		SWLOG_INFO("[UPDATEFIRMWARE]     - trigger_type: %d (4=App triggered)\n", flash_ctx->trigger_type);
		SWLOG_INFO("[UPDATEFIRMWARE]     - operation_start_time: %ld\n", flash_ctx->operation_start_time);
		
		// ===== SPAWN WORKER THREAD =====
		SWLOG_INFO("[UPDATEFIRMWARE] Spawning GThread worker for flash operation\n");
		SWLOG_INFO("[UPDATEFIRMWARE] Thread name: 'rdkfw_flash_worker'\n");
		SWLOG_INFO("[UPDATEFIRMWARE] Thread function: rdkfw_flash_worker_thread()\n");
		
		GError *thread_error = NULL;
		GThread *flash_thread = g_thread_try_new("rdkfw_flash_worker",
		                                         rdkfw_flash_worker_thread,
		                                         flash_ctx,
		                                         &thread_error);
		
		if (!flash_thread) {
			// Thread spawn failed - critical error
			SWLOG_ERROR("[UPDATEFIRMWARE] CRITICAL: Failed to spawn flash worker thread!\n");
			SWLOG_ERROR("[UPDATEFIRMWARE] Error: %s\n", 
			            thread_error ? thread_error->message : "unknown error");
			
			if (thread_error) {
				SWLOG_ERROR("[UPDATEFIRMWARE] Error details: domain=%s, code=%d\n",
				           g_quark_to_string(thread_error->domain), thread_error->code);
				g_error_free(thread_error);
			}
			
			// Emit error signal to client
			SWLOG_ERROR("[UPDATEFIRMWARE] Emitting error signal to client\n");
			FlashProgressUpdate *error_update = g_new0(FlashProgressUpdate, 1);
			error_update->progress = -1;
			error_update->status = FW_UPDATE_ERROR;
			error_update->handler_id = g_strdup(flash_ctx->handler_id);
			error_update->firmware_name = g_strdup(flash_ctx->firmware_name);
			error_update->error_message = g_strdup("Failed to spawn flash worker thread");
			error_update->connection = rdkv_conn_dbus;
			g_idle_add(emit_flash_progress_idle, error_update);
			
			// Cleanup flash_ctx
			SWLOG_ERROR("[UPDATEFIRMWARE] Cleaning up failed flash context\n");
			g_free(flash_ctx->handler_id);
			g_free(flash_ctx->firmware_name);
			g_free(flash_ctx->firmware_type);
			g_free(flash_ctx->firmware_fullpath);
			g_free(flash_ctx->server_url);
			//g_mutex_clear(flash_ctx->mutex);
			//g_free(flash_ctx->mutex);
			//g_free(flash_ctx->stop_flag);
			g_free(flash_ctx);
			
			// Cleanup global state
			SWLOG_ERROR("[UPDATEFIRMWARE] Cleaning up global flash state\n");
			g_free(current_flash->firmware_name);
			g_free(current_flash);
			current_flash = NULL;
			IsFlashInProgress = FALSE;
			
			// Cleanup remaining input strings
			g_free(rebootImmediately);
			g_free(loc_of_firmware);
			
			SWLOG_ERROR("[UPDATEFIRMWARE] Thread spawn failure handling complete\n");
			return;
		}
		
		// Thread spawned successfully
		SWLOG_INFO("[UPDATEFIRMWARE] ===== Flash Worker Thread Spawned Successfully =====\n");
		SWLOG_INFO("[UPDATEFIRMWARE] Thread ID: %p\n", (void*)flash_thread);
		SWLOG_INFO("[UPDATEFIRMWARE] Thread is now running independently\n");
		SWLOG_INFO("[UPDATEFIRMWARE] Worker thread will:\n");
		SWLOG_INFO("[UPDATEFIRMWARE]   1. Emit UpdateProgress(0%%, INPROGRESS)\n");
		SWLOG_INFO("[UPDATEFIRMWARE]   2. Emit UpdateProgress(25%%, INPROGRESS)\n");
		SWLOG_INFO("[UPDATEFIRMWARE]   3. Call flashImage() from librdksw_flash.so\n");
		SWLOG_INFO("[UPDATEFIRMWARE]   4. Emit UpdateProgress(50%%, INPROGRESS)\n");
		SWLOG_INFO("[UPDATEFIRMWARE]   5. Emit UpdateProgress(75%%, INPROGRESS)\n");
		SWLOG_INFO("[UPDATEFIRMWARE]   6. Emit UpdateProgress(100%%, COMPLETED) or (-1, ERROR)\n");
		SWLOG_INFO("[UPDATEFIRMWARE]   7. Cleanup global state via g_idle_add\n");
		SWLOG_INFO("[UPDATEFIRMWARE]   8. Exit thread\n");
		
		g_thread_unref(flash_thread);  // Detach thread (runs independently)
		
		SWLOG_INFO("[UPDATEFIRMWARE] Thread detached (g_thread_unref called)\n");
		SWLOG_INFO("[UPDATEFIRMWARE] Thread will run until flash operation completes\n");
		
		// Cleanup remaining input strings (ownership transferred to thread)
		// Note: handler_id_str, firmware_name, type_of_firmware, firmware_fullpath 
		// are now owned by flash_ctx and will be freed by worker thread
		SWLOG_INFO("[UPDATEFIRMWARE] Cleaning up non-transferred input strings\n");
		g_free(rebootImmediately);
		g_free(loc_of_firmware);
		
		/* Coverity fix: FORWARD_NULL - Log flash_ctx details BEFORE setting to NULL */
		SWLOG_INFO("[UPDATEFIRMWARE] ========== REQUEST HANDLING COMPLETE ==========\n");
		SWLOG_INFO("[UPDATEFIRMWARE]   - Handler ID: %"G_GUINT64_FORMAT"\n", handler_id_numeric);
		SWLOG_INFO("[UPDATEFIRMWARE]   - Firmware: '%s'\n", flash_ctx->firmware_name);
		SWLOG_INFO("[UPDATEFIRMWARE]   - Type: '%s'\n", flash_ctx->firmware_type);
		SWLOG_INFO("[UPDATEFIRMWARE]   - Response: SUCCESS (IN_PROGRESS)\n");
		SWLOG_INFO("[UPDATEFIRMWARE]   - Worker: Spawned and running\n");
		SWLOG_INFO("[UPDATEFIRMWARE]   - Next: Client receives UpdateProgress signals\n");
		
		/* Coverity fix: RESOURCE_LEAK - Ownership of flash_ctx transferred to worker thread.
		 * The thread will free flash_ctx when it completes. Do NOT set to NULL to avoid
		 * false positive "resource leak" warnings from Coverity on the NULL assignment. */
		// flash_ctx is now owned by the worker thread and will be freed there
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


/**
 * @brief D-Bus interface vtable - maps method calls to handler function.
 */
static const GDBusInterfaceVTable interface_vtable = {
    process_app_request,
    NULL,
    NULL
};

/**
 * @brief Initialize and register the D-Bus service.
 *
 * Sets up the D-Bus service on the system bus:
 * 1. Parses introspection XML to define interface
 * 2. Connects to system bus
 * 3. Registers object at /org/rdkfwupdater/Service
 * 4. Claims bus name org.rdkfwupdater.Interface
 *
 * @return 1 on success, 0 on failure
 */
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

/**
 * @brief Clean up D-Bus resources at daemon shutdown.
 *
 * Performs orderly shutdown:
 * 1. Frees all active task contexts
 * 2. Cleans up process tracking system
 * 3. Unregisters D-Bus object
 * 4. Releases D-Bus connection
 * 5. Releases bus name
 * 6. Frees main loop
 */
void cleanup_dbus()
{
	SWLOG_INFO("[CLEANUP] Starting D-Bus cleanup...\n");
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
/**
 * @brief GTask worker thread function - performs blocking XConf query.
 *
 * This function runs in a separate GLib worker thread, allowing the main D-Bus
 * event loop to remain responsive while the blocking network I/O to XConf server
 * executes. Once complete, results are packaged and passed back to main thread
 * via g_task_return_pointer() for signal broadcast.
 *
 * Execution flow:
 * 1. Runs in background worker thread (not main loop)
 * 2. Calls rdkFwupdateMgr_checkForUpdate() which does blocking XConf network call
 * 3. Packages CheckUpdateResponse into GVariant for D-Bus signal
 * 4. Returns result to GTask framework
 * 5. GTask framework schedules async_xconf_fetch_complete() on main loop
 *
 * @param task GTask handle for async operation
 * @param source_object Source object (unused)
 * @param task_data AsyncXconfFetchContext with handler_id and connection
 * @param cancellable Cancellable object (unused)
 */
static void rdkfw_xconf_fetch_worker(GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable) {
    AsyncXconfFetchContext *ctx = (AsyncXconfFetchContext *)task_data;
    
    SWLOG_INFO("\n[ASYNC_FETCH] ========================================\n");
    SWLOG_INFO("[ASYNC_FETCH] *** WORKER THREAD STARTED ***\n");
    SWLOG_INFO("[ASYNC_FETCH] Background XConf fetch executing in separate thread\n");
    SWLOG_INFO("[ASYNC_FETCH] Thread ID: %lu\n", (unsigned long)pthread_self());
    SWLOG_INFO("[ASYNC_FETCH] ========================================\n");
    
    // CRITICAL: Validate context and handler_id before proceeding
    if (!ctx) {
        SWLOG_ERROR("[ASYNC_FETCH] CRITICAL: task_data (ctx) is NULL!\n");
        return;
    }
    SWLOG_INFO("[ASYNC_FETCH] Context pointer: %p\n", ctx);
    
    if (!ctx->handler_id) {
        SWLOG_ERROR("[ASYNC_FETCH] CRITICAL: ctx->handler_id is NULL!\n");
        return;
    }
    SWLOG_INFO("[ASYNC_FETCH] Handler ID: '%s' (ptr=%p)\n", ctx->handler_id, ctx->handler_id);
    
    if (!ctx->connection) {
        SWLOG_ERROR("[ASYNC_FETCH] WARNING: ctx->connection is NULL!\n");
    }
    SWLOG_INFO("[ASYNC_FETCH] Connection: %p\n", ctx->connection);
    
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
    
    SWLOG_INFO("[ASYNC_FETCH] About to call function with handler_id='%s'\n", ctx->handler_id);
    
    // Call the blocking handler function - safe here because we're in worker thread!
    CheckUpdateResponse response = rdkFwupdateMgr_checkForUpdate(ctx->handler_id);
    
    SWLOG_INFO("[ASYNC_FETCH] Function call returned successfully!\n");
    
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
    SWLOG_INFO("[ASYNC_FETCH] ========== WORKER THREAD FINISHED ==========\n\n");
}

/**
 * @brief GTask completion callback - broadcasts results to all waiting clients.
 *
 * This function runs on the MAIN LOOP thread after async_xconf_fetch_task() completes
 * in the worker thread. It retrieves the XConf query results, broadcasts a
 * CheckForUpdateComplete signal to all waiting clients, cleans up task tracking state,
 * and resets the IsCheckUpdateInProgress flag.
 *
 * Execution flow:
 * 1. Retrieve result from worker thread via g_task_propagate_pointer()
 * 2. Broadcast CheckForUpdateComplete D-Bus signal to all listeners
 * 3. Iterate through waiting_checkUpdate_ids and clean up all task contexts
 * 4. Reset IsCheckUpdateInProgress flag to allow new CheckForUpdate requests
 * 5. Free async context and associated resources
 *
 * @param source_object Source object (unused)
 * @param res GAsyncResult from GTask framework
 * @param user_data AsyncXconfFetchContext with connection and handler_id
 */
static void rdkfw_xconf_fetch_done(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    SWLOG_INFO("\n[COMPLETE] ========================================\n");
    SWLOG_INFO("[COMPLETE] *** COMPLETION CALLBACK TRIGGERED ***\n");
    SWLOG_INFO("[COMPLETE] Running on MAIN LOOP thread (async operation complete)\n");
    SWLOG_INFO("[COMPLETE] Thread ID: %lu\n", (unsigned long)pthread_self());
    SWLOG_INFO("[COMPLETE] ========================================\n");
    
    // CRITICAL: Validate all parameters
    if (!res) {
        SWLOG_ERROR("[COMPLETE] CRITICAL: GAsyncResult (res) is NULL!\n");
        SWLOG_ERROR("[COMPLETE] ========================================\n\n");
        return;
    }
    
    if (!user_data) {
        SWLOG_ERROR("[COMPLETE] CRITICAL: user_data (ctx) is NULL!\n");
        SWLOG_ERROR("[COMPLETE] Cannot cleanup context - potential memory leak\n");
        SWLOG_ERROR("[COMPLETE] Attempting to reset state anyway...\n");
        IsCheckUpdateInProgress = FALSE;
        if (waiting_checkUpdate_ids) {
            g_slist_free(waiting_checkUpdate_ids);
            waiting_checkUpdate_ids = NULL;
        }
        SWLOG_ERROR("[COMPLETE] ========================================\n\n");
        return;
    }
    
    AsyncXconfFetchContext *ctx = (AsyncXconfFetchContext *)user_data;
    SWLOG_INFO("[COMPLETE] Context pointer: %p\n", ctx);
    
    // Validate context contents
    if (!ctx->handler_id) {
        SWLOG_ERROR("[COMPLETE] WARNING: ctx->handler_id is NULL!\n");
    } else {
        SWLOG_INFO("[COMPLETE] Handler ID: '%s'\n", ctx->handler_id);
    }
    
    if (!ctx->connection) {
        SWLOG_ERROR("[COMPLETE] WARNING: ctx->connection is NULL!\n");
    } else {
        SWLOG_INFO("[COMPLETE] Connection: %p\n", ctx->connection);
    }
    
    GTask *task = G_TASK(res);
    if (!task) {
        SWLOG_ERROR("[COMPLETE] CRITICAL: Failed to cast res to GTask!\n");
        SWLOG_ERROR("[COMPLETE] Cleaning up and aborting...\n");
        IsCheckUpdateInProgress = FALSE;
        if (waiting_checkUpdate_ids) {
            g_slist_free(waiting_checkUpdate_ids);
            waiting_checkUpdate_ids = NULL;
        }
        if (ctx->handler_id) g_free(ctx->handler_id);
        g_free(ctx);
        SWLOG_ERROR("[COMPLETE] ========================================\n\n");
        return;
    }
    
    GVariant *result = g_task_propagate_pointer(task, NULL);
    
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
        
        if (ctx->handler_id) g_free(ctx->handler_id);
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
    SWLOG_INFO("[COMPLETE]   Object path: '%s'\n", OBJECT_PATH);
    SWLOG_INFO("[COMPLETE]   Interface: 'org.rdkfwupdater.Interface'\n");
    SWLOG_INFO("[COMPLETE]   Destination: NULL (broadcast to all listeners)\n");
    SWLOG_INFO("[COMPLETE]   Waiting clients count: %d\n", 
               g_slist_length(waiting_checkUpdate_ids));
    
    // Extract data for logging with NULL checks
    gchar *handler_id_str = NULL;
    gint32 result_code = -1;
    gchar *current_ver = NULL, *available_ver = NULL, *update_details = NULL, *status_msg = NULL;
    
    g_variant_get(result, "(sissss)", &handler_id_str, &result_code,
                 &current_ver, &available_ver, &update_details, &status_msg);
    
    SWLOG_INFO("[COMPLETE]   Signal payload:\n");
    SWLOG_INFO("[COMPLETE]     - Handler ID: '%s'\n", handler_id_str ? handler_id_str : "NULL");
    SWLOG_INFO("[COMPLETE]     - Result code: %d ", result_code);
    switch(result_code) {
        case 0: SWLOG_INFO("(UPDATE_AVAILABLE)\n"); break;
        case 1: SWLOG_INFO("(UPDATE_NOT_AVAILABLE)\n"); break;
        case 2: SWLOG_INFO("(UPDATE_ERROR)\n"); break;
        default: SWLOG_INFO("(UNKNOWN)\n"); break;
    }
    SWLOG_INFO("[COMPLETE]     - Current version: '%s'\n", current_ver ? current_ver : "NULL");
    SWLOG_INFO("[COMPLETE]     - Available version: '%s'\n", available_ver ? available_ver : "NULL");
    SWLOG_INFO("[COMPLETE]     - Update details: '%s'\n", update_details ? update_details : "NULL");
    SWLOG_INFO("[COMPLETE]     - Status message: '%s'\n", status_msg ? status_msg : "NULL");
    
    // Free extracted strings (g_variant_get duplicates them)
    if (handler_id_str) g_free(handler_id_str);
    if (current_ver) g_free(current_ver);
    if (available_ver) g_free(available_ver);
    if (update_details) g_free(update_details);
    if (status_msg) g_free(status_msg);
    
    // Validate connection before emitting signal
    if (!ctx->connection) {
        SWLOG_ERROR("[COMPLETE] CRITICAL: ctx->connection is NULL!\n");
        SWLOG_ERROR("[COMPLETE] Cannot emit signal - no D-Bus connection\n");
        SWLOG_ERROR("[COMPLETE] This should never happen - investigating...\n");
        
        // Still cleanup state
        IsCheckUpdateInProgress = FALSE;
        if (waiting_checkUpdate_ids) {
            g_slist_free(waiting_checkUpdate_ids);
            waiting_checkUpdate_ids = NULL;
        }
        if (ctx->handler_id) g_free(ctx->handler_id);
        g_free(ctx);
        g_variant_unref(result);
        SWLOG_ERROR("[COMPLETE] ========================================\n\n");
        return;
    }
    
    SWLOG_INFO("[COMPLETE] Emitting signal now...\n");
    GError *error = NULL;
    gboolean signal_sent = g_dbus_connection_emit_signal(
        ctx->connection,
        NULL,  // NULL destination = broadcast to all listeners
        OBJECT_PATH,  // Use constant instead of hardcoded path
        "org.rdkfwupdater.Interface",
        "CheckForUpdateComplete",
        result,
        &error
    );
    
    // Check emission result
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
    if (ctx) {
        if (ctx->handler_id) {
            SWLOG_INFO("[COMPLETE]   Freeing handler_id: '%s'\n", ctx->handler_id);
            g_free(ctx->handler_id);
            ctx->handler_id = NULL;
        } else {
            SWLOG_WARN("[COMPLETE]   handler_id is NULL (already freed or never set)\n");
        }
        g_free(ctx);
        SWLOG_INFO("[COMPLETE]   Context freed\n");
    } else {
        SWLOG_ERROR("[COMPLETE]   Context is NULL (should never happen at this point!)\n");
    }
    
    SWLOG_INFO("==============CHECKFORUPDATE COMPLETION CALLBACK FINISHED============\n");
}

/* ============================================================================
 * DownloadFirmware Async Implementation
 * ============================================================================
 * These functions implement the GTask-based async download architecture with:
 * - Worker thread for blocking rdkv_upgrade_request() call
 * - g_idle_add() for thread-safe D-Bus signal emission from main loop
 * - Multi-client piggybacking (multiple clients can wait for same download)
 * - Robust error handling and cleanup
 * - Real-time progress updates via curl's CURLOPT_XFERINFOFUNCTION callback
 * ============================================================================
 */

/**
 * @brief Progress callback invoked by curl during firmware download
 * 
 * Thread Context: WORKER THREAD (called by libcurl via xferinfo)
 * Thread Safety: Uses g_idle_add() to marshal signals to main loop
 * 
 * Call Chain:
 *   libcurl (worker thread) → xferinfo() [urlHelper.c]
 *     → download_progress_callback() [HERE]
 *       → g_idle_add(rdkfw_emit_download_progress, ...)
 *         → rdkfw_emit_download_progress() [main loop thread]
 *           → g_dbus_connection_emit_signal()
 * 
 * Signature matches RdkUpgradeContext_t.progress_callback:
 *   void (*)(unsigned long long current_bytes, unsigned long long total_bytes, void* user_data)
 * 
 * @param current_bytes Bytes downloaded so far
 * @param total_bytes Total file size in bytes
 * @param user_data AsyncDownloadContext* pointer
 */
#if 0
static void download_progress_callback(unsigned long long current_bytes, 
                                       unsigned long long total_bytes, 
                                       void* user_data) {
    AsyncDownloadContext *ctx = (AsyncDownloadContext *)user_data;
    
    SWLOG_DEBUG("[PROGRESS_CB] Invoked from worker thread (Thread ID: %lu)\n", (unsigned long)pthread_self());
    
    // NULL CHECK: Validate context
    if (!ctx) {
        SWLOG_ERROR("[PROGRESS_CB] ERROR: NULL context received!\n");
        return;
    }
    
    // NULL CHECK: Validate D-Bus connection
    if (!ctx->connection) {
        SWLOG_ERROR("[PROGRESS_CB] ERROR: NULL D-Bus connection in context!\n");
        return;
    }
    
    // Calculate percentage from bytes
    int progress_int = 0;
    if (total_bytes > 0) {
        double percent = ((double)current_bytes / (double)total_bytes) * 100.0;
        progress_int = (int)percent;
        if (progress_int > 100) progress_int = 100;
        if (progress_int < 0) progress_int = 0;
    }
    
    // Throttle logging (only log on change)
    static int last_logged = -1;
    if (progress_int != last_logged) {
        SWLOG_INFO("[PROGRESS_CB] Download progress: %d%% (%llu/%llu bytes)\n", 
                   progress_int, current_bytes, total_bytes);
        SWLOG_INFO("[PROGRESS_CB]   Firmware: %s\n", ctx->firmware_name ? ctx->firmware_name : "NULL");
        last_logged = progress_int;
    }
    
    // Update global state
    if (current_download) {
        current_download->current_progress = progress_int;
    }
    
    // Create progress update for D-Bus signal
    ProgressUpdate *update = g_new0(ProgressUpdate, 1);
    if (!update) {
        SWLOG_ERROR("[PROGRESS_CB] ERROR: Failed to allocate ProgressUpdate!\n");
        return;
    }
    
    update->progress = progress_int;
    update->status = FW_DWNL_INPROGRESS;
    update->handler_id = ctx->handler_id ? g_strdup(ctx->handler_id) : NULL;
    update->firmware_name = ctx->firmware_name ? g_strdup(ctx->firmware_name) : NULL;
    update->connection = ctx->connection;
    
    // Schedule signal emission on main loop (thread-safe!)
    SWLOG_DEBUG("[PROGRESS_CB] Scheduling D-Bus signal emission via g_idle_add\n");
    g_idle_add(rdkfw_emit_download_progress, update);
}
#endif
/**
 * @brief Emit DownloadProgress signal on main loop (called via g_idle_add)
 * 
 * This function runs on the MAIN LOOP thread (scheduled via g_idle_add from worker thread).
 * It emits the DownloadProgress D-Bus signal to all subscribed clients.
 * 
 * Thread Safety:
 * - Called from main loop context (scheduled via g_idle_add from worker)
 * - Safe to call D-Bus emission functions
 * - Frees all allocated data in ProgressUpdate struct after emission
 * 
 * @param user_data ProgressUpdate* containing progress, status, handler_id, firmware_name, connection
 * @return G_SOURCE_REMOVE (one-shot callback, do not reschedule)
 * 
 * Memory Management:
 * - Takes ownership of ProgressUpdate and all its string fields
 * - Frees handler_id, firmware_name, and ProgressUpdate struct
 * - Does NOT free connection (borrowed pointer)
 */
static gboolean rdkfw_emit_download_progress(gpointer user_data) {
    ProgressUpdate *update = (ProgressUpdate *)user_data;
    
    // NULL CHECK: Critical - validate input data
    if (!update) {
        SWLOG_ERROR("[PROGRESS_SIGNAL] CRITICAL: NULL update data received!\n");
        return G_SOURCE_REMOVE;
    }
    
    SWLOG_INFO("\n[PROGRESS_SIGNAL] ========================================\n");
    SWLOG_INFO("[PROGRESS_SIGNAL] Emitting DownloadProgress D-Bus signal\n");
    SWLOG_INFO("[PROGRESS_SIGNAL]   Handler ID: %s\n", update->handler_id ? update->handler_id : "(none)");
    SWLOG_INFO("[PROGRESS_SIGNAL]   Firmware: %s\n", update->firmware_name ? update->firmware_name : "NULL");
    SWLOG_INFO("[PROGRESS_SIGNAL]   Progress: %d%%\n", update->progress);
    SWLOG_INFO("[PROGRESS_SIGNAL]   Status: %d ", update->status);
    
    // Log human-readable status
    switch(update->status) {
        case FW_DWNL_NOTSTARTED:
            SWLOG_INFO("(FW_DWNL_NOTSTARTED)\n");
            break;
        case FW_DWNL_INPROGRESS:
            SWLOG_INFO("(FW_DWNL_INPROGRESS)\n");
            break;
        case FW_DWNL_COMPLETED:
            SWLOG_INFO("(FW_DWNL_COMPLETED)\n");
            break;
        case FW_DWNL_ERROR:
            SWLOG_INFO("(FW_DWNL_ERROR)\n");
            break;
        default:
            SWLOG_INFO("(UNKNOWN)\n");
            break;
    }
    
    // NULL CHECK: Validate D-Bus connection before emission
    if (!update->connection) {
        SWLOG_ERROR("[PROGRESS_SIGNAL] ERROR: D-Bus connection is NULL, cannot emit signal\n");
        goto cleanup;
    }
    
    // NULL CHECK: Validate firmware_name before building variant
    if (!update->firmware_name) {
        SWLOG_ERROR("[PROGRESS_SIGNAL] ERROR: firmware_name is NULL, using placeholder\n");
        update->firmware_name = g_strdup("(unknown)");
    }
    
    // Convert handler_id string to guint64 for signal (D-Bus type 't')
    guint64 handler_id_numeric = 0;
    if (update->handler_id) {
        handler_id_numeric = g_ascii_strtoull(update->handler_id, NULL, 10);
    }
    
    // Build status and message strings for signal
    const gchar *status_str = NULL;
    const gchar *message_str = NULL;
    
    switch(update->status) {
        case FW_DWNL_NOTSTARTED:
            status_str = "NOTSTARTED";
            message_str = "Download accepted, queued for processing";
            break;
        case FW_DWNL_INPROGRESS:
            status_str = "INPROGRESS";
            message_str = "Download in progress";
            break;
        case FW_DWNL_COMPLETED:
            status_str = "COMPLETED";
            message_str = "Download completed successfully";
            break;
        case FW_DWNL_ERROR:
            status_str = "DWNL_ERROR";
            message_str = "Download failed";
            break;
        default:
            status_str = "UNKNOWN";
            message_str = "Unknown status";
            break;
    }
    
    // Build signal variant: (t handlerId, s firmwareName, u progress, s status, s message)
    GVariant *signal_data = g_variant_new("(tsuss)",
        handler_id_numeric,           // handlerId (uint64)
        update->firmware_name,        // firmwareName (string)
        (guint32)update->progress,    // progress (uint32)
        status_str,                   // status (string)
        message_str                   // message (string)
    );
    
    // NULL CHECK: Validate variant creation
    if (!signal_data) {
        SWLOG_ERROR("[PROGRESS_SIGNAL] ERROR: Failed to create signal variant\n");
        goto cleanup;
    }
    
    SWLOG_INFO("[PROGRESS_SIGNAL] Signal variant created, emitting...\n");
    
    // Emit signal (broadcast to all listeners)
    GError *error = NULL;
    gboolean signal_sent = g_dbus_connection_emit_signal(
        update->connection,
        NULL,  // NULL destination = broadcast to all listeners
        OBJECT_PATH,
        INTERFACE_NAME,
        "DownloadProgress",
        signal_data,
        &error
    );
    
    // Check emission result
    if (signal_sent) {
        SWLOG_INFO("[PROGRESS_SIGNAL] Signal emitted successfully\n");
    } else {
        SWLOG_ERROR("[PROGRESS_SIGNAL] Signal emission FAILED: %s\n", 
                   error ? error->message : "Unknown error");
        if (error) {
            g_error_free(error);
        }
    }
    
cleanup:
    SWLOG_INFO("[PROGRESS_SIGNAL] Cleaning up progress update data\n");
    SWLOG_INFO("[PROGRESS_SIGNAL] ========================================\n\n");
    
    // Cleanup: Free all allocated strings
    if (update->handler_id) {
        g_free(update->handler_id);
    }
    if (update->firmware_name) {
        g_free(update->firmware_name);
    }
    
    // Free the update struct itself
    g_free(update);
    
    return G_SOURCE_REMOVE;  // One-shot callback, do not reschedule
}

/**
 * @brief Worker thread function for async firmware download
 * 
 * This function runs in a SEPARATE WORKER THREAD (via GTask).
 * It can safely make blocking calls (like rdkv_upgrade_request) without
 * freezing the D-Bus main loop.
 * 
 * Progress updates are scheduled on main loop via g_idle_add() for
 * thread-safe D-Bus signal emission.
 * 
 * Thread Safety:
 * - Runs in worker thread (safe to block)
 * - Uses g_idle_add() to schedule signal emission on main loop
 * - AsyncDownloadContext is immutable (safe)
 * - current_download accessed only via g_idle_add (main loop serialization)
 * 
 * @param task GTask instance managing this async operation
 * @param source_object Source object (unused, pass NULL)
 * @param task_data AsyncDownloadContext with download parameters
 * @param cancellable GCancellable for cancellation support (unused)
 * 
 * Result:
 * - Success: g_task_return_boolean(task, TRUE)
 * - Failure: g_task_return_boolean(task, FALSE)
 */
static void rdkfw_download_worker(GTask *task, gpointer source_object, 
                                  gpointer task_data, GCancellable *cancellable) {
    AsyncDownloadContext *ctx = (AsyncDownloadContext *)task_data;
    char imageHTTPURL[MAX_URL_LEN1]; 
    // NULL CHECKS: Validate critical context fields before dereferencing
    if (!ctx) {
        SWLOG_ERROR("[DOWNLOAD_WORKER] CRITICAL: NULL context!\n");
        g_task_return_boolean(task, FALSE);
        return;
    }
    
    /* Coverity fix: REVERSE_INULL - Remove redundant null check after dereferencing ctx */
    SWLOG_INFO("\n");
    SWLOG_INFO("=========================DOWNLOAD WORKER THREAD STARTED=====================\n");
    SWLOG_INFO(" Thread ID: %lu\n", (unsigned long)pthread_self());
    SWLOG_INFO("Handler ID: %s\n", ctx->handler_id ? ctx->handler_id : "NULL");
    SWLOG_INFO("Firmware: %s\n", ctx->firmware_name ? ctx->firmware_name : "NULL");
    SWLOG_INFO("URL: %s\n", ctx->download_url ? ctx->download_url : "NULL");
    SWLOG_INFO("Type: %s\n", ctx->type_of_firmware ? ctx->type_of_firmware : "NULL");
    
    if (!ctx->firmware_name || strlen(ctx->firmware_name) == 0) {
        SWLOG_ERROR("[DOWNLOAD_WORKER] CRITICAL: Invalid firmware name!\n");
        
        // Emit error signal
        ProgressUpdate *error_update = g_new0(ProgressUpdate, 1);
        error_update->progress = -1;
        error_update->status = FW_DWNL_ERROR;
        error_update->handler_id = ctx->handler_id ? g_strdup(ctx->handler_id) : NULL;
        error_update->firmware_name = g_strdup("(invalid)");
        error_update->connection = ctx->connection;
        g_idle_add(rdkfw_emit_download_progress, error_update);
        
        g_task_return_boolean(task, FALSE);
        return;
    }
    
    if (!ctx->download_url || strlen(ctx->download_url) == 0) {
        SWLOG_ERROR("[DOWNLOAD_WORKER] CRITICAL: Invalid download URL!\n");
        
        // Emit error signal
        ProgressUpdate *error_update = g_new0(ProgressUpdate, 1);
        error_update->progress = -1;
        error_update->status = FW_DWNL_ERROR;
        error_update->handler_id = ctx->handler_id ? g_strdup(ctx->handler_id) : NULL;
        error_update->firmware_name = ctx->firmware_name ? g_strdup(ctx->firmware_name) : NULL;
        error_update->connection = ctx->connection;
        g_idle_add(rdkfw_emit_download_progress, error_update);
        
        g_task_return_boolean(task, FALSE);
        return;
    }
    
    // ========== STEP 2: BUILD DOWNLOAD PATH ==========
    SWLOG_INFO("[DOWNLOAD_WORKER] Building download path...\n");
    char download_path[DWNL_PATH_FILE_LENGTH];
    gchar *difw_path = get_difw_path();
    int path_len = snprintf(download_path, sizeof(download_path), "%s/%s", difw_path, ctx->firmware_name);
//int path_len = snprintf(download_path, sizeof(download_path), "/tmp/%s", ctx->firmware_name);
    SWLOG_INFO("DWNL path with img name=%s\n", download_path); 
    if (path_len < 0 || path_len >= sizeof(download_path)) {
        SWLOG_ERROR("[DOWNLOAD_WORKER] ERROR: Download path too long or snprintf failed!\n");
        if (difw_path)
            g_free(difw_path);
        g_task_return_boolean(task, FALSE);
        return;
    }
    
    SWLOG_INFO("[DOWNLOAD_WORKER] Download path: %s\n", download_path);
    SWLOG_INFO("[DOWNLOAD_WORKER]   Path length: %d characters\n", path_len);
    
    /* difw_path no longer needed after building download_path; free to avoid leak */
    if (difw_path) {
        g_free(difw_path);
        difw_path = NULL;
    }
 // ========== STEP 3: LOAD DEVICE PROPERTIES  ==========
    SWLOG_INFO("[DOWNLOAD_WORKER] ========================================\n");
    SWLOG_INFO("[DOWNLOAD_WORKER] LOADING DEVICE PROPERTIES\n");
    SWLOG_INFO("[DOWNLOAD_WORKER] ========================================\n");
    
    DeviceProperty_t device_info;
    memset(&device_info, 0, sizeof(DeviceProperty_t));
    
    SWLOG_INFO("[DOWNLOAD_WORKER] Calling getDeviceProperties()...\n");
    int ret = getDeviceProperties(&device_info);
    if (ret != 1) {
        SWLOG_ERROR("[DOWNLOAD_WORKER] WARNING: getDeviceProperties() failed with code %d\n", ret);
        SWLOG_WARN("[DOWNLOAD_WORKER] Continuing with zero-initialized device_info\n");
    }
    
    // ========== STEP 4: LOAD RFC SETTINGS ==========
    SWLOG_INFO("[DOWNLOAD_WORKER] ========================================\n");
    SWLOG_INFO("[DOWNLOAD_WORKER] LOADING RFC SETTINGS\n");
    SWLOG_INFO("[DOWNLOAD_WORKER] ========================================\n");
    
    Rfc_t rfc_list;
    memset(&rfc_list, 0, sizeof(Rfc_t));
    
    SWLOG_INFO("[DOWNLOAD_WORKER] Calling getRFCSettings()...\n");
    ret = getRFCSettings(&rfc_list);
    if (ret != 0) {
        SWLOG_ERROR("[DOWNLOAD_WORKER] WARNING: getRFCSettings() failed with code %d\n", ret);
        SWLOG_WARN("[DOWNLOAD_WORKER] Continuing with zero-initialized rfc_list (no throttling)\n");
    }
    
    // ========== STEP 5: CREATE ADDITIONAL REQUIRED VARIABLES ==========
    SWLOG_INFO("[DOWNLOAD_WORKER] ========================================\n");
    SWLOG_INFO("[DOWNLOAD_WORKER] CREATING REQUIRED VARIABLES\n");
    SWLOG_INFO("[DOWNLOAD_WORKER] ========================================\n");
    
    // Force exit flag (used by downloadFile to check for cancellation)
    int force_exit = 0;
    SWLOG_INFO("[DOWNLOAD_WORKER] force_exit = 0 \n");
    
    // Trigger type: D-Bus = app-initiated (same as rdkv_main.c uses)
    int trigger_type = 4;  // 1=bootup, 2=scheduled, 3=TR69, 4=app, 5=delayed, 6=statred
    SWLOG_INFO("[DOWNLOAD_WORKER] trigger_type = 4 (app-initiated via D-Bus)\n");
    
    // Initialize lastrun as empty string 
    // In rdkv_main.c: char lastrun[64] = { 0 };  // Store last run time
    char lastrun[64] = { 0 };
    SWLOG_INFO("[DOWNLOAD_WORKER] lastrun = \"\" (empty string, as in  rdkv_main.c)\n");
    
    // ========== STEP 6: CREATE RdkUpgradeContext_t ==========
    SWLOG_INFO("[DOWNLOAD_WORKER] ========================================\n");
    SWLOG_INFO("[DOWNLOAD_WORKER] CREATING RdkUpgradeContext_t\n");
    SWLOG_INFO("[DOWNLOAD_WORKER] ========================================\n");
    
    RdkUpgradeContext_t upgrade_ctx = {0};  // Zero-initialize all fields
    SWLOG_INFO("[DOWNLOAD_WORKER] Structure zero-initialized\n");
    
    // Map firmware type string to enum
    SWLOG_INFO("[DOWNLOAD_WORKER] Mapping firmware type '%s' to enum...\n", ctx->type_of_firmware);
    
    if (g_strcmp0(ctx->type_of_firmware, "PCI") == 0) {
        upgrade_ctx.upgrade_type = PCI_UPGRADE;
        SWLOG_INFO("[DOWNLOAD_WORKER] Mapped to PCI_UPGRADE\n");
    } else if (g_strcmp0(ctx->type_of_firmware, "PDRI") == 0) {
        upgrade_ctx.upgrade_type = PDRI_UPGRADE;
        SWLOG_INFO("[DOWNLOAD_WORKER] Mapped to PDRI_UPGRADE\n");
    } else if (g_strcmp0(ctx->type_of_firmware, "PERIPHERAL") == 0) {
        upgrade_ctx.upgrade_type = PERIPHERAL_UPGRADE;
        SWLOG_INFO("[DOWNLOAD_WORKER] Mapped to PERIPHERAL_UPGRADE\n");
    } else {
        SWLOG_WARN("[DOWNLOAD_WORKER] WARNING: Unknown type '%s', defaulting to PCI_UPGRADE\n", 
                   ctx->type_of_firmware);
        upgrade_ctx.upgrade_type = PCI_UPGRADE;
    }
    
    // Populate context fields
    SWLOG_INFO("[DOWNLOAD_WORKER] Populating RdkUpgradeContext_t fields...\n");
    
    upgrade_ctx.server_type = HTTP_SSR_DIRECT;
    SWLOG_INFO("[DOWNLOAD_WORKER]   server_type = HTTP_SSR_DIRECT\n");
    
    snprintf(imageHTTPURL, sizeof(imageHTTPURL), "%s/%s", ctx->download_url, ctx->firmware_name);
    upgrade_ctx.artifactLocationUrl = imageHTTPURL;
    SWLOG_INFO("[DOWNLOAD_WORKER]   artifactLocationUrl = %s\n", upgrade_ctx.artifactLocationUrl);
    
    upgrade_ctx.dwlloc = download_path;
    SWLOG_INFO("[DOWNLOAD_WORKER]   dwlloc = %s\n", (const char*)upgrade_ctx.dwlloc);
    
    upgrade_ctx.pPostFields = NULL;
    SWLOG_INFO("[DOWNLOAD_WORKER]   pPostFields = NULL (HTTP GET)\n");
    
    upgrade_ctx.immed_reboot_flag = "false";
    SWLOG_INFO("[DOWNLOAD_WORKER]   immed_reboot_flag = \"false\" (D-Bus never reboots)\n");
    
    upgrade_ctx.delay_dwnl = 0;
    SWLOG_INFO("[DOWNLOAD_WORKER]   delay_dwnl = 0 (no delay for D-Bus)\n");
    
    upgrade_ctx.lastrun = lastrun;
    SWLOG_INFO("[DOWNLOAD_WORKER]   lastrun = \"\" (empty string, EXACT PARITY with rdkv_main.c)\n");
    
    upgrade_ctx.disableStatsUpdate = "yes";
    SWLOG_INFO("[DOWNLOAD_WORKER]   disableStatsUpdate = \"yes\" (D-Bus handles telemetry)\n");
    
    upgrade_ctx.device_info = &device_info;
    SWLOG_INFO("[DOWNLOAD_WORKER]   device_info = %p (device properties loaded)\n", 
               (void*)upgrade_ctx.device_info);
    
    upgrade_ctx.force_exit = &force_exit;
    SWLOG_INFO("[DOWNLOAD_WORKER]   force_exit = %p (valid pointer to int)\n", 
               (void*)upgrade_ctx.force_exit);
    
    upgrade_ctx.trigger_type = trigger_type;
    SWLOG_INFO("[DOWNLOAD_WORKER]   trigger_type = %d  (app-initiated)\n", upgrade_ctx.trigger_type);
    
    upgrade_ctx.rfc_list = &rfc_list;
    SWLOG_INFO("[DOWNLOAD_WORKER]   rfc_list = %p ( RFC settings loaded)\n", 
               (void*)upgrade_ctx.rfc_list);
    
    // *** CRITICAL FIELDS FOR D-BUS ***
    upgrade_ctx.download_only = 1;
    SWLOG_INFO("[DOWNLOAD_WORKER]   download_only = 1 *** CRITICAL: SKIP FLASHING in rdkv_upgrade_request()! ***\n");
    
    //upgrade_ctx.progress_callback = download_progress_callback;
    //SWLOG_INFO("[DOWNLOAD_WORKER]   progress_callback = %p (download_progress_callback)\n", 
      //         (void*)upgrade_ctx.progress_callback);
    
    //upgrade_ctx.progress_callback_data = (void*)ctx;
    //SWLOG_INFO("[DOWNLOAD_WORKER]   progress_callback_data = %p (AsyncDownloadContext)\n", 
      //         upgrade_ctx.progress_callback_data);
    
    SWLOG_INFO("[DOWNLOAD_WORKER] ========================================\n");
    SWLOG_INFO("[DOWNLOAD_WORKER] RdkUpgradeContext_t FULLY POPULATED\n");
    SWLOG_INFO("[DOWNLOAD_WORKER] ========================================\n");
    
    // ========== STEP 7: SPAWN PROGRESS MONITOR THREAD ==========
    SWLOG_INFO("[DOWNLOAD_WORKER] \n");
    SWLOG_INFO("[DOWNLOAD_WORKER] ========== SPAWNING PROGRESS MONITOR THREAD ==========\n");
    SWLOG_INFO("[DOWNLOAD_WORKER] Enabling real-time progress updates to D-Bus clients\n");
    
    GThread* monitor_thread = NULL;
    gboolean stop_monitor = FALSE;
    GMutex* monitor_mutex = NULL;
    ProgressMonitorContext* monitor_ctx = NULL;
    
    // Allocate and initialize mutex for thread-safe access
    monitor_mutex = g_new0(GMutex, 1);
    if (monitor_mutex == NULL) {
        SWLOG_ERROR("[DOWNLOAD_WORKER] ERROR: Failed to allocate monitor mutex\n");
        SWLOG_ERROR("[DOWNLOAD_WORKER] Continuing without progress monitoring\n");
    } else {
        g_mutex_init(monitor_mutex);
        SWLOG_DEBUG("[DOWNLOAD_WORKER] Monitor mutex allocated and initialized\n");
        
        // Allocate progress monitor context
        monitor_ctx = g_new0(ProgressMonitorContext, 1);
        if (monitor_ctx == NULL) {
            SWLOG_ERROR("[DOWNLOAD_WORKER] ERROR: Failed to allocate monitor context\n");
            g_mutex_clear(monitor_mutex);
            g_free(monitor_mutex);
            monitor_mutex = NULL;
            SWLOG_ERROR("[DOWNLOAD_WORKER] Continuing without progress monitoring\n");
        } else {
            SWLOG_DEBUG("[DOWNLOAD_WORKER] Monitor context allocated\n");
            
            // Initialize context fields
            monitor_ctx->connection = ctx->connection;  // Borrowed pointer (do NOT free)
            monitor_ctx->handler_id = ctx->handler_id ? g_strdup(ctx->handler_id) : NULL;
            monitor_ctx->firmware_name = ctx->firmware_name ? g_strdup(ctx->firmware_name) : NULL;
            monitor_ctx->stop_flag = &stop_monitor;
            monitor_ctx->mutex = monitor_mutex;
            monitor_ctx->last_dlnow = 0;
            monitor_ctx->last_activity_time = time(NULL);
            
            SWLOG_DEBUG("[DOWNLOAD_WORKER] Monitor context initialized:\n");
            SWLOG_DEBUG("[DOWNLOAD_WORKER]   - Handler ID: %s\n", monitor_ctx->handler_id ? monitor_ctx->handler_id : "(null)");
            SWLOG_DEBUG("[DOWNLOAD_WORKER]   - Firmware: %s\n", monitor_ctx->firmware_name ? monitor_ctx->firmware_name : "(null)");
            SWLOG_DEBUG("[DOWNLOAD_WORKER]   - Connection: %p\n", (void*)monitor_ctx->connection);
            
            // Spawn monitor thread
            GError* thread_error = NULL;
            monitor_thread = g_thread_try_new("rdkfw_progress_monitor", 
                                              rdkfw_progress_monitor_thread, 
                                              monitor_ctx, 
                                              &thread_error);
            
            if (monitor_thread == NULL) {
                SWLOG_ERROR("[DOWNLOAD_WORKER] ERROR: Failed to spawn monitor thread: %s\n",
                           thread_error ? thread_error->message : "Unknown error");
                
                // Cleanup on thread creation failure
                if (thread_error != NULL) {
                    g_error_free(thread_error);
                    thread_error = NULL;
                }
                
                // Free string fields (g_strdup'd copies)
                if (monitor_ctx->handler_id) {
                    g_free(monitor_ctx->handler_id);
                    monitor_ctx->handler_id = NULL;
                }
                if (monitor_ctx->firmware_name) {
                    g_free(monitor_ctx->firmware_name);
                    monitor_ctx->firmware_name = NULL;
                }
                
                // Clear and free mutex
                g_mutex_clear(monitor_mutex);
                g_free(monitor_mutex);
                monitor_mutex = NULL;
                
                // Free context
                g_free(monitor_ctx);
                monitor_ctx = NULL;
                
                // Continue without progress monitoring (non-fatal)
                SWLOG_ERROR("[DOWNLOAD_WORKER] Continuing without progress monitoring\n");
            } else {
                SWLOG_INFO("[DOWNLOAD_WORKER] Progress monitor thread started successfully\n");
                SWLOG_INFO("[DOWNLOAD_WORKER] Thread will poll /opt/curl_progress every 100ms\n");
                SWLOG_INFO("[DOWNLOAD_WORKER] D-Bus signals will be emitted when progress changes \n");
            }
        }
    }
    
    // ========== STEP 8: CALL rdkv_upgrade_request() ==========
    SWLOG_INFO("[DOWNLOAD_WORKER] \n");
    SWLOG_INFO("[DOWNLOAD_WORKER] ========== CALLING rdkv_upgrade_request() ==========\n");
    SWLOG_INFO("[DOWNLOAD_WORKER] This is a BLOCKING call - will not return until download completes\n");
    SWLOG_INFO("[DOWNLOAD_WORKER] Context summary:\n");
    SWLOG_INFO("[DOWNLOAD_WORKER]   device_info: LOADED from system\n");
    SWLOG_INFO("[DOWNLOAD_WORKER]   rfc_list: LOADED from RFC server\n");
    SWLOG_INFO("[DOWNLOAD_WORKER]   force_exit: Valid pointer\n");
    SWLOG_INFO("[DOWNLOAD_WORKER]   trigger_type: 4 (app-initiated)\n");
    SWLOG_INFO("[DOWNLOAD_WORKER]   lastrun: Empty string (matching rdkv_main.c)\n");
    SWLOG_INFO("[DOWNLOAD_WORKER]   progress_monitoring: %s\n", monitor_thread ? "ENABLED" : "DISABLED ");
    SWLOG_INFO("[DOWNLOAD_WORKER] Starting download NOW...\n");
    
    void* curl_handle = NULL;
    int http_code = 0;
    
    int curl_ret_code = rdkv_upgrade_request(&upgrade_ctx, &curl_handle, &http_code);
    
    SWLOG_INFO("[DOWNLOAD_WORKER] \n");
    SWLOG_INFO("[DOWNLOAD_WORKER] ========== rdkv_upgrade_request() RETURNED ==========\n");
    SWLOG_INFO("[DOWNLOAD_WORKER] Return value: %d\n", curl_ret_code);
    SWLOG_INFO("[DOWNLOAD_WORKER] HTTP code: %d\n", http_code);
    
    // ========== STEP 9: STOP PROGRESS MONITOR THREAD ==========
    if (monitor_thread != NULL) {
        SWLOG_INFO("[DOWNLOAD_WORKER] Stopping progress monitor thread...\n");
        
        // Signal thread to stop atomically
        g_atomic_int_set(&stop_monitor, TRUE);
        
        /* Coverity fix: RESOURCE_LEAK - g_thread_join() frees the thread handle.
         * Do NOT set monitor_thread = NULL afterward as Coverity flags it as a leak.
         * The thread handle is properly freed by g_thread_join() and should not be
         * used again after this point. */
        SWLOG_DEBUG("[DOWNLOAD_WORKER] Waiting for monitor thread to exit...\n");
        g_thread_join(monitor_thread);
        /* Coverity workaround: Set to NULL to suppress "going out of scope" warning.
         * This is cosmetic - the thread was already freed by g_thread_join() above. */
        monitor_thread = NULL;
        
        SWLOG_INFO("[DOWNLOAD_WORKER]  Progress monitor thread stopped cleanly\n");
        
        // Note: monitor_mutex and monitor_ctx are cleaned up by the thread itself
        // Do NOT free them here to avoid double-free
    } else {
        SWLOG_DEBUG("[DOWNLOAD_WORKER] No monitor thread to stop (was not started)\n");
        /* Coverity fix: RESOURCE_LEAK - If monitor_thread is NULL but monitor_ctx was allocated
         * and thread creation failed, we need to clean it up here. Check if monitor_ctx exists. */
        if (monitor_ctx != NULL) {
            SWLOG_DEBUG("[DOWNLOAD_WORKER] Cleaning up monitor_ctx (thread was not started)\n");
            if (monitor_ctx->handler_id) g_free(monitor_ctx->handler_id);
            if (monitor_ctx->firmware_name) g_free(monitor_ctx->firmware_name);
            if (monitor_ctx->mutex) {
                g_mutex_clear(monitor_ctx->mutex);
                g_free(monitor_ctx->mutex);
            }
            g_free(monitor_ctx);
            monitor_ctx = NULL;
        }
    }
    
    // ========== STEP 10: HANDLE RESULT ==========
    if (curl_ret_code != 0) {
        // Download failed
        SWLOG_ERROR("[DOWNLOAD_WORKER] *** DOWNLOAD FAILED! ***\n");
        SWLOG_ERROR("[DOWNLOAD_WORKER]   curl_code = %d\n", curl_ret_code);
        SWLOG_ERROR("[DOWNLOAD_WORKER]   http_code = %d\n", http_code);
        
        // Map curl error to friendly message
        const char *error_desc = "Unknown error";
        if (curl_ret_code == 6) error_desc = "Could not resolve host";
        else if (curl_ret_code == 7) error_desc = "Failed to connect to host";
        else if (curl_ret_code == 28) error_desc = "Operation timeout";
        else if (curl_ret_code == 35) error_desc = "SSL connect error";
        else if (curl_ret_code == 52) error_desc = "Server returned nothing";
        else if (curl_ret_code == 56) error_desc = "Failure in receiving network data";
        
        SWLOG_ERROR("[DOWNLOAD_WORKER]   Error description: %s\n", error_desc);
        
        /* Coverity fix: RESOURCE_LEAK - Stop monitor thread before returning on error */
        if (monitor_thread != NULL) {
            SWLOG_INFO("[DOWNLOAD_WORKER] Stopping monitor thread due to download error...\n");
            g_atomic_int_set(&stop_monitor, TRUE);
            g_thread_join(monitor_thread);
            monitor_thread = NULL;  // Suppress Coverity "going out of scope" warning
        } else if (monitor_ctx != NULL) {
            SWLOG_DEBUG("[DOWNLOAD_WORKER] Cleaning up monitor_ctx (thread was not started)\n");
            if (monitor_ctx->handler_id) g_free(monitor_ctx->handler_id);
            if (monitor_ctx->firmware_name) g_free(monitor_ctx->firmware_name);
            if (monitor_ctx->mutex) {
                g_mutex_clear(monitor_ctx->mutex);
                g_free(monitor_ctx->mutex);
            }
            g_free(monitor_ctx);
        }
        
        // Emit error signal
        ProgressUpdate *error_update = g_new0(ProgressUpdate, 1);
        if (error_update) {
            error_update->progress = -1;  // Error indicator
            error_update->status = FW_DWNL_ERROR;
            error_update->handler_id = ctx->handler_id ? g_strdup(ctx->handler_id) : NULL;
            error_update->firmware_name = ctx->firmware_name ? g_strdup(ctx->firmware_name) : NULL;
            error_update->connection = ctx->connection;
            g_idle_add(rdkfw_emit_download_progress, error_update);
        }
        
        g_task_return_boolean(task, FALSE);
        return;
    }
    
    // Check HTTP code (even if curl succeeded)
    if (http_code != 200 && http_code != 206) {
        SWLOG_ERROR("[DOWNLOAD_WORKER] *** HTTP ERROR! ***\n");
        SWLOG_ERROR("[DOWNLOAD_WORKER]   HTTP code = %d\n", http_code);
        
        /* Coverity fix: RESOURCE_LEAK - Stop monitor thread before returning on error */
        if (monitor_thread != NULL) {
            SWLOG_INFO("[DOWNLOAD_WORKER] Stopping monitor thread due to HTTP error...\n");
            g_atomic_int_set(&stop_monitor, TRUE);
            g_thread_join(monitor_thread);
            monitor_thread = NULL;  // Suppress Coverity "going out of scope" warning
        } else if (monitor_ctx != NULL) {
            SWLOG_DEBUG("[DOWNLOAD_WORKER] Cleaning up monitor_ctx (thread was not started)\n");
            if (monitor_ctx->handler_id) g_free(monitor_ctx->handler_id);
            if (monitor_ctx->firmware_name) g_free(monitor_ctx->firmware_name);
            if (monitor_ctx->mutex) {
                g_mutex_clear(monitor_ctx->mutex);
                g_free(monitor_ctx->mutex);
            }
            g_free(monitor_ctx);
        }
        
        // Emit error signal
        ProgressUpdate *error_update = g_new0(ProgressUpdate, 1);
        if (error_update) {
            error_update->progress = -1;
            error_update->status = FW_DWNL_ERROR;
            error_update->handler_id = ctx->handler_id ? g_strdup(ctx->handler_id) : NULL;
            error_update->firmware_name = ctx->firmware_name ? g_strdup(ctx->firmware_name) : NULL;
            error_update->connection = ctx->connection;
            g_idle_add(rdkfw_emit_download_progress, error_update);
        }
        
        g_task_return_boolean(task, FALSE);
        return;
    }
    
    // Success!
    SWLOG_INFO("[DOWNLOAD_WORKER] *** DOWNLOAD SUCCESSFUL! ***\n");
    SWLOG_INFO("[DOWNLOAD_WORKER]   Downloaded to: %s\n", download_path);
    SWLOG_INFO("[DOWNLOAD_WORKER]   curl_code = 0 (CURLE_OK)\n");
    SWLOG_INFO("[DOWNLOAD_WORKER]   http_code = %d\n", http_code);
    
    // Emit final 100% completion signal (in case progress callback didn't reach exactly 100%)
    ProgressUpdate *complete_update = g_new0(ProgressUpdate, 1);
    if (complete_update) {
        complete_update->progress = 100;
        complete_update->status = FW_DWNL_COMPLETED;
        complete_update->handler_id = ctx->handler_id ? g_strdup(ctx->handler_id) : NULL;
        complete_update->firmware_name = ctx->firmware_name ? g_strdup(ctx->firmware_name) : NULL;
        complete_update->connection = ctx->connection;
        g_idle_add(rdkfw_emit_download_progress, complete_update);
    }
    
    SWLOG_INFO("[DOWNLOAD_WORKER] ----------------------------------------\n");
    SWLOG_INFO("[DOWNLOAD_WORKER] *** WORKER THREAD COMPLETE ***\n");
    SWLOG_INFO("====================DOWNLOAD WORKER THREAD FINISHED====================\n");
    
    g_task_return_boolean(task, TRUE);
}

/**
 * @brief GTask completion callback - cleans up after download completes
 * 
 * Runs on MAIN LOOP thread after worker thread completes.
 * Handles cleanup of download state and waiting client list.
 * 
 * Thread Safety:
 * - Called from main loop context (scheduled by GLib after worker finishes)
 * - Safe to modify global state (current_download, IsDownloadInProgress)
 * - Safe to emit final D-Bus signals if needed
 * 
 * @param source_object Source object (unused)
 * @param res GAsyncResult from GTask
 * @param user_data AsyncDownloadContext (same as passed to async_download_task)
 * 
 * Cleanup:
 * - Frees current_download and all waiting handler IDs
 * - Resets IsDownloadInProgress flag
 * - Frees AsyncDownloadContext and all its strings
 */
static void rdkfw_download_done(GObject *source_object, GAsyncResult *res, 
                                gpointer user_data) {
    AsyncDownloadContext *ctx = (AsyncDownloadContext *)user_data;
    
    SWLOG_INFO("\n");
    SWLOG_INFO("=============DOWNLOAD COMPLETION CALLBACK TRIGGERED=======================================\n");
    SWLOG_INFO("=================Running on: MAIN LOOP thread\n");
    SWLOG_INFO("===============Firmware: %s\n", ctx ? (ctx->firmware_name ? ctx->firmware_name : "NULL") : "NULL");
    
    // NULL CHECK: Validate task result
    if (!res) {
        SWLOG_ERROR("[DOWNLOAD_COMPLETE] ERROR: GAsyncResult is NULL!\n");
        goto cleanup_ctx;
    }
    
    GTask *task = G_TASK(res);
    GError *error = NULL;
    gboolean success = g_task_propagate_boolean(task, &error);
    
    if (error) {
        SWLOG_ERROR("[DOWNLOAD_COMPLETE] Task completed with error: %s\n", error->message);
        g_error_free(error);
    } else if (success) {
        SWLOG_INFO("[DOWNLOAD_COMPLETE] Download result: SUCCESS\n");
    } else {
        SWLOG_INFO("[DOWNLOAD_COMPLETE] Download result: FAILED\n");
    }
    
    // Cleanup global download state
    if (current_download) {
        SWLOG_INFO("[DOWNLOAD_COMPLETE] Cleaning up download state:\n");
        SWLOG_INFO("[DOWNLOAD_COMPLETE]   Firmware: %s\n", 
                   current_download->firmware_name ? current_download->firmware_name : "NULL");
        SWLOG_INFO("[DOWNLOAD_COMPLETE]   Final progress: %d%%\n", 
                   current_download->current_progress);
        SWLOG_INFO("[DOWNLOAD_COMPLETE]   Waiting clients: %d\n", 
                   g_slist_length(current_download->waiting_handler_ids));
        
        // Free waiting handler IDs list
        if (current_download->waiting_handler_ids) {
		SWLOG_INFO("[DOWNLOAD_COMPLETE] Freeing %d waiting handler IDs\n", g_slist_length(current_download->waiting_handler_ids));
            g_slist_free_full(current_download->waiting_handler_ids, g_free);
            current_download->waiting_handler_ids = NULL;
        }
        
        // Free firmware name
        if (current_download->firmware_name) {
            g_free(current_download->firmware_name);
            current_download->firmware_name = NULL;
        }
        
        // Free the state struct
        g_free(current_download);
        current_download = NULL;
        
        SWLOG_INFO("[DOWNLOAD_COMPLETE] Download state cleaned up\n");
    } else {
        SWLOG_INFO("[DOWNLOAD_COMPLETE] No download state to clean up (already NULL)\n");
    }
    
    // Reset progress flag
    IsDownloadInProgress = FALSE;
    SWLOG_INFO("[DOWNLOAD_COMPLETE] IsDownloadInProgress = FALSE\n");
    SWLOG_INFO("[DOWNLOAD_COMPLETE] System ready for next download\n");
    
cleanup_ctx:
    // Cleanup AsyncDownloadContext
    if (ctx) {
        SWLOG_INFO("[DOWNLOAD_COMPLETE] Freeing AsyncDownloadContext\n");
        
        if (ctx->handler_id) {
            g_free(ctx->handler_id);
        }
        if (ctx->firmware_name) {
            g_free(ctx->firmware_name);
        }
        if (ctx->download_url) {
            g_free(ctx->download_url);
        }
        if (ctx->type_of_firmware) {
            g_free(ctx->type_of_firmware);
        }
        
        g_free(ctx);
        
        SWLOG_INFO("[DOWNLOAD_COMPLETE] - Context freed\n");
    }
    
    SWLOG_INFO(" ==============================DOWNLOAD COMPLETION CALLBACK FINISHED ===============================\n");
}

/* End of DownloadFirmware async implementation */

