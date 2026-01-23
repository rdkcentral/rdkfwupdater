/**
 * @file dbus_client.c
 * @brief D-Bus client implementation
 */

#include "dbus_client.h"
#include <stdio.h>
#include <pthread.h>

/* Global D-Bus connection (shared by all handles) */
static GDBusConnection *g_dbus_connection = NULL;
static pthread_mutex_t g_dbus_mutex = PTHREAD_MUTEX_INITIALIZER;
static gboolean g_dbus_initialized = FALSE;

/**
 * Initialize D-Bus connection to system bus
 */
gboolean dbus_client_init(GError **error)
{
    pthread_mutex_lock(&g_dbus_mutex);
    
    if (g_dbus_initialized && g_dbus_connection) {
        pthread_mutex_unlock(&g_dbus_mutex);
        return TRUE;  /* Already initialized */
    }
    
    /* Connect to system D-Bus */
    g_dbus_connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, error);
    if (!g_dbus_connection) {
        fprintf(stderr, "[librdkFwupdateMgr] Failed to connect to D-Bus system bus: %s\n",
                error && *error ? (*error)->message : "unknown error");
        pthread_mutex_unlock(&g_dbus_mutex);
        return FALSE;
    }
    
    g_dbus_initialized = TRUE;
    pthread_mutex_unlock(&g_dbus_mutex);
    
    fprintf(stderr, "[librdkFwupdateMgr] D-Bus connection initialized\n");
    return TRUE;
}

/**
 * Cleanup D-Bus connection
 */
void dbus_client_cleanup(void)
{
    pthread_mutex_lock(&g_dbus_mutex);
    
    if (g_dbus_connection) {
        g_object_unref(g_dbus_connection);
        g_dbus_connection = NULL;
    }
    
    g_dbus_initialized = FALSE;
    pthread_mutex_unlock(&g_dbus_mutex);
    
    fprintf(stderr, "[librdkFwupdateMgr] D-Bus connection cleaned up\n");
}

/**
 * Call RegisterProcess D-Bus method
 * 
 * Method signature: RegisterProcess(ss) -> (t)
 * - Input: processName (string), libVersion (string)
 * - Output: handler_id (uint64)
 */
uint64_t dbus_call_register_process(
    const char *processName,
    const char *libVersion,
    GError **error)
{
    if (!g_dbus_connection) {
        fprintf(stderr, "[librdkFwupdateMgr] D-Bus not initialized\n");
        if (error) {
            *error = g_error_new_literal(G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED,
                                        "D-Bus connection not initialized");
        }
        return 0;
    }
    
    fprintf(stderr, "[librdkFwupdateMgr] Calling RegisterProcess: %s (version: %s)\n",
            processName, libVersion);
    
    /* Call daemon method: RegisterProcess(string, string) -> uint64 */
    GVariant *result = g_dbus_connection_call_sync(
        g_dbus_connection,
        DBUS_SERVICE_NAME,
        DBUS_OBJECT_PATH,
        DBUS_INTERFACE_NAME,
        "RegisterProcess",
        g_variant_new("(ss)", processName, libVersion),
        G_VARIANT_TYPE("(t)"),  /* Returns uint64 */
        G_DBUS_CALL_FLAGS_NONE,
        5000,  /* 5 second timeout */
        NULL,
        error
    );
    
    if (!result) {
        fprintf(stderr, "[librdkFwupdateMgr] RegisterProcess D-Bus call failed: %s\n",
                error && *error ? (*error)->message : "unknown error");
        return 0;
    }
    
    /* Extract handle ID from response */
    uint64_t handleId;
    g_variant_get(result, "(t)", &handleId);
    g_variant_unref(result);
    
    fprintf(stderr, "[librdkFwupdateMgr] RegisterProcess successful, handle ID: %" G_GUINT64_FORMAT "\n", 
            handleId);
    return handleId;
}

/**
 * Call UnregisterProcess D-Bus method
 * 
 * Method signature: UnregisterProcess(t) -> (b)
 * - Input: handlerId (uint64)
 * - Output: success (boolean)
 */
gboolean dbus_call_unregister_process(uint64_t handleId, GError **error)
{
    if (!g_dbus_connection) {
        fprintf(stderr, "[librdkFwupdateMgr] D-Bus not initialized\n");
        if (error) {
            *error = g_error_new_literal(G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED,
                                        "D-Bus connection not initialized");
        }
        return FALSE;
    }
    
    fprintf(stderr, "[librdkFwupdateMgr] Calling UnregisterProcess: handle ID %" G_GUINT64_FORMAT "\n", 
            handleId);
    
    /* Call daemon method: UnregisterProcess(uint64) -> boolean */
    GVariant *result = g_dbus_connection_call_sync(
        g_dbus_connection,
        DBUS_SERVICE_NAME,
        DBUS_OBJECT_PATH,
        DBUS_INTERFACE_NAME,
        "UnregisterProcess",
        g_variant_new("(t)", handleId),
        G_VARIANT_TYPE("(b)"),  /* Returns boolean */
        G_DBUS_CALL_FLAGS_NONE,
        5000,
        NULL,
        error
    );
    
    if (!result) {
        fprintf(stderr, "[librdkFwupdateMgr] UnregisterProcess D-Bus call failed: %s\n",
                error && *error ? (*error)->message : "unknown error");
        return FALSE;
    }
    
    gboolean success;
    g_variant_get(result, "(b)", &success);
    g_variant_unref(result);
    
    fprintf(stderr, "[librdkFwupdateMgr] UnregisterProcess %s, handle ID: %" G_GUINT64_FORMAT "\n",
            success ? "successful" : "failed", handleId);
    return success;
}

/**
 * Get current D-Bus connection
 */
GDBusConnection* dbus_get_connection(void)
{
    return g_dbus_connection;
}

/**
 * Check if D-Bus is initialized
 */
gboolean dbus_is_initialized(void)
{
    gboolean initialized;
    pthread_mutex_lock(&g_dbus_mutex);
    initialized = g_dbus_initialized;
    pthread_mutex_unlock(&g_dbus_mutex);
    return initialized;
}
