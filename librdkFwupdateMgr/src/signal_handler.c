/**
 * @file signal_handler.c
 * @brief D-Bus signal subscription and routing implementation
 */

#include "signal_handler.h"
#include "dbus_client.h"
#include "handle_registry.h"
#include "handle_mgr.h"
#include "rdkFwupdateMgr_client.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>

/* D-Bus signal names */
#define SIGNAL_CHECK_UPDATE_COMPLETE "CheckForUpdateComplete"
#define SIGNAL_DOWNLOAD_PROGRESS     "DownloadProgress"
#define SIGNAL_UPDATE_PROGRESS       "UpdateProgress"

/* Global signal subscription tracking */
static pthread_mutex_t g_signal_mutex = PTHREAD_MUTEX_INITIALIZER;
static GHashTable *g_subscriptions = NULL;  /* Key: daemon_handle_id (guint64), Value: subscription_id (guint) */
static bool g_signal_handler_initialized = false;

/**
 * Signal handler for CheckForUpdateComplete
 * 
 * Signal signature from daemon:
 *   CheckForUpdateComplete(
 *     uint64 handlerId,
 *     int32 result,
 *     int32 statusCode,
 *     string currentVersion,
 *     string availableVersion,
 *     string updateDetails,
 *     string statusMessage
 *   )
 */
static void on_check_update_complete_signal(
    GDBusConnection *connection,
    const gchar *sender_name,
    const gchar *object_path,
    const gchar *interface_name,
    const gchar *signal_name,
    GVariant *parameters,
    gpointer user_data)
{
    (void)connection;
    (void)sender_name;
    (void)object_path;
    (void)interface_name;
    (void)signal_name;
    (void)user_data;
    
    /* Parse signal parameters */
    guint64 handler_id;
    gint32 result;
    gint32 status_code;
    const gchar *current_version;
    const gchar *available_version;
    const gchar *update_details;
    const gchar *status_message;
    
    g_variant_get(parameters, "(tiissss)",
                  &handler_id,
                  &result,
                  &status_code,
                  &current_version,
                  &available_version,
                  &update_details,
                  &status_message);
    
    fprintf(stderr, "[librdkFwupdateMgr] CheckForUpdateComplete signal received:\n");
    fprintf(stderr, "  handleId: %" G_GUINT64_FORMAT "\n", handler_id);
    fprintf(stderr, "  result: %d, statusCode: %d\n", result, status_code);
    fprintf(stderr, "  currentVersion: %s\n", current_version);
    fprintf(stderr, "  availableVersion: %s\n", available_version);
    fprintf(stderr, "  updateDetails: %s\n", update_details);
    fprintf(stderr, "  statusMessage: %s\n", status_message);
    
    /* Lookup handle in registry */
    InternalHandle *handle = registry_lookup_by_daemon_id(handler_id);
    if (!handle) {
        fprintf(stderr, "[librdkFwupdateMgr] WARNING: No handle found for daemon ID %" G_GUINT64_FORMAT "\n",
                handler_id);
        return;
    }
    
    /* Lock handle to access callback */
    pthread_mutex_lock(&handle->lock);
    
    UpdateEventCallback callback = handle->update_event_cb;
    void *user_data_cb = handle->update_event_user_data;
    
    if (!callback) {
        fprintf(stderr, "[librdkFwupdateMgr] WARNING: No callback registered for handle %" G_GUINT64_FORMAT "\n",
                handler_id);
        pthread_mutex_unlock(&handle->lock);
        return;
    }
    
    /* Prepare callback data */
    FwInfoData fw_data = {
        .version = available_version,
        .updateDetails = update_details,
        .status = (CheckForUpdateStatus)status_code
    };
    
    fprintf(stderr, "[librdkFwupdateMgr] Invoking callback for handle %" G_GUINT64_FORMAT "\n", handler_id);
    
    /* Unlock before invoking callback (callback must not block or call library APIs) */
    pthread_mutex_unlock(&handle->lock);
    
    /* Invoke client callback */
    callback(&fw_data, user_data_cb);
    
    fprintf(stderr, "[librdkFwupdateMgr] Callback completed for handle %" G_GUINT64_FORMAT "\n", handler_id);
}

/**
 * Initialize signal handling subsystem
 */
bool signal_handler_init(void)
{
    pthread_mutex_lock(&g_signal_mutex);
    
    if (g_signal_handler_initialized) {
        pthread_mutex_unlock(&g_signal_mutex);
        return true;
    }
    
    fprintf(stderr, "[librdkFwupdateMgr] Initializing signal handler...\n");
    
    /* Create subscription tracking table */
    g_subscriptions = g_hash_table_new_full(
        g_int64_hash,
        g_int64_equal,
        g_free,      /* Free key (guint64*) */
        NULL         /* Value is just subscription_id (guint), no free needed */
    );
    
    if (!g_subscriptions) {
        fprintf(stderr, "[librdkFwupdateMgr] Failed to create subscriptions table\n");
        pthread_mutex_unlock(&g_signal_mutex);
        return false;
    }
    
    g_signal_handler_initialized = true;
    pthread_mutex_unlock(&g_signal_mutex);
    
    fprintf(stderr, "[librdkFwupdateMgr] Signal handler initialized\n");
    return true;
}

/**
 * Cleanup signal handling subsystem
 */
void signal_handler_cleanup(void)
{
    pthread_mutex_lock(&g_signal_mutex);
    
    if (!g_signal_handler_initialized) {
        pthread_mutex_unlock(&g_signal_mutex);
        return;
    }
    
    fprintf(stderr, "[librdkFwupdateMgr] Cleaning up signal handler...\n");
    
    /* Get D-Bus connection */
    GDBusConnection *connection = dbus_get_connection();
    
    /* Unsubscribe all signals */
    if (connection && g_subscriptions) {
        GHashTableIter iter;
        gpointer key, value;
        
        g_hash_table_iter_init(&iter, g_subscriptions);
        while (g_hash_table_iter_next(&iter, &key, &value)) {
            guint subscription_id = GPOINTER_TO_UINT(value);
            fprintf(stderr, "[librdkFwupdateMgr] Unsubscribing signal ID: %u\n", subscription_id);
            g_dbus_connection_signal_unsubscribe(connection, subscription_id);
        }
    }
    
    /* Destroy subscription table */
    if (g_subscriptions) {
        g_hash_table_destroy(g_subscriptions);
        g_subscriptions = NULL;
    }
    
    g_signal_handler_initialized = false;
    pthread_mutex_unlock(&g_signal_mutex);
    
    fprintf(stderr, "[librdkFwupdateMgr] Signal handler cleanup complete\n");
}

/**
 * Subscribe to CheckForUpdateComplete signal for a handle
 */
bool signal_handler_subscribe_check_update(uint64_t daemon_handle_id)
{
    pthread_mutex_lock(&g_signal_mutex);
    
    if (!g_signal_handler_initialized) {
        fprintf(stderr, "[librdkFwupdateMgr] ERROR: Signal handler not initialized\n");
        pthread_mutex_unlock(&g_signal_mutex);
        return false;
    }
    
    /* Check if already subscribed */
    guint64 *key = g_new(guint64, 1);
    *key = daemon_handle_id;
    
    if (g_hash_table_contains(g_subscriptions, key)) {
        fprintf(stderr, "[librdkFwupdateMgr] Already subscribed to CheckForUpdateComplete for handle %" G_GUINT64_FORMAT "\n",
                daemon_handle_id);
        g_free(key);
        pthread_mutex_unlock(&g_signal_mutex);
        return true;
    }
    
    /* Get D-Bus connection */
    GDBusConnection *connection = dbus_get_connection();
    if (!connection) {
        fprintf(stderr, "[librdkFwupdateMgr] ERROR: D-Bus connection not available\n");
        g_free(key);
        pthread_mutex_unlock(&g_signal_mutex);
        return false;
    }
    
    fprintf(stderr, "[librdkFwupdateMgr] Subscribing to CheckForUpdateComplete for handle %" G_GUINT64_FORMAT "\n",
            daemon_handle_id);
    
    /* Subscribe to signal */
    guint subscription_id = g_dbus_connection_signal_subscribe(
        connection,
        NULL,                           /* sender (NULL = any) */
        DBUS_INTERFACE_NAME,            /* interface */
        SIGNAL_CHECK_UPDATE_COMPLETE,   /* signal name */
        DBUS_OBJECT_PATH,               /* object path */
        NULL,                           /* arg0 filter (NULL = any) */
        G_DBUS_SIGNAL_FLAGS_NONE,
        on_check_update_complete_signal,
        NULL,                           /* user_data */
        NULL                            /* user_data_free_func */
    );
    
    if (subscription_id == 0) {
        fprintf(stderr, "[librdkFwupdateMgr] ERROR: Failed to subscribe to CheckForUpdateComplete\n");
        g_free(key);
        pthread_mutex_unlock(&g_signal_mutex);
        return false;
    }
    
    /* Store subscription ID */
    g_hash_table_insert(g_subscriptions, key, GUINT_TO_POINTER(subscription_id));
    
    fprintf(stderr, "[librdkFwupdateMgr] Subscribed to CheckForUpdateComplete (subscription ID: %u)\n",
            subscription_id);
    
    pthread_mutex_unlock(&g_signal_mutex);
    return true;
}

/**
 * Unsubscribe from all signals for a handle
 */
void signal_handler_unsubscribe_all(uint64_t daemon_handle_id)
{
    pthread_mutex_lock(&g_signal_mutex);
    
    if (!g_signal_handler_initialized || !g_subscriptions) {
        pthread_mutex_unlock(&g_signal_mutex);
        return;
    }
    
    fprintf(stderr, "[librdkFwupdateMgr] Unsubscribing all signals for handle %" G_GUINT64_FORMAT "\n",
            daemon_handle_id);
    
    /* Lookup subscription */
    guint64 key = daemon_handle_id;
    gpointer value = g_hash_table_lookup(g_subscriptions, &key);
    
    if (value) {
        guint subscription_id = GPOINTER_TO_UINT(value);
        
        /* Get D-Bus connection */
        GDBusConnection *connection = dbus_get_connection();
        if (connection) {
            fprintf(stderr, "[librdkFwupdateMgr] Unsubscribing signal ID: %u\n", subscription_id);
            g_dbus_connection_signal_unsubscribe(connection, subscription_id);
        }
        
        /* Remove from table */
        g_hash_table_remove(g_subscriptions, &key);
    }
    
    pthread_mutex_unlock(&g_signal_mutex);
}
