/**
 * @file api_impl.c
 * @brief Public API implementation for rdkFwupdateMgr client library
 */

#include "rdkFwupdateMgr_client.h"
#include "handle_mgr.h"
#include "handle_registry.h"
#include "dbus_client.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>

/* Library initialization flag */
static pthread_once_t g_init_once = PTHREAD_ONCE_INIT;
static bool g_library_initialized = false;

/**
 * One-time library initialization
 */
static void library_init(void)
{
    fprintf(stderr, "[librdkFwupdateMgr] Library initializing...\n");
    registry_init();
    g_library_initialized = true;
    fprintf(stderr, "[librdkFwupdateMgr] Library initialized\n");
}

/**
 * Library cleanup (called on unload)
 */
static void __attribute__((destructor)) library_cleanup(void)
{
    if (!g_library_initialized) {
        return;
    }
    
    fprintf(stderr, "[librdkFwupdateMgr] Library cleanup...\n");
    
    /* Cleanup registry (will warn about leaked handles) */
    registry_cleanup();
    
    /* Cleanup D-Bus connection */
    dbus_client_cleanup();
    
    g_library_initialized = false;
    fprintf(stderr, "[librdkFwupdateMgr] Library cleanup complete\n");
}

/**
 * @brief Register process with firmware update daemon
 * 
 * Public API implementation.
 */
FirmwareInterfaceHandle registerProcess(
    const char *processName,
    const char *libVersion)
{
    /* Lazy library initialization */
    pthread_once(&g_init_once, library_init);
    
    /* Validate inputs */
    if (!processName || !libVersion) {
        fprintf(stderr, "[librdkFwupdateMgr] registerProcess: NULL parameter\n");
        return NULL;
    }
    
    if (strlen(processName) == 0 || strlen(libVersion) == 0) {
        fprintf(stderr, "[librdkFwupdateMgr] registerProcess: empty string parameter\n");
        return NULL;
    }
    
    fprintf(stderr, "[librdkFwupdateMgr] registerProcess: %s (version: %s)\n",
            processName, libVersion);
    
    /* Initialize D-Bus connection */
    GError *error = NULL;
    if (!dbus_client_init(&error)) {
        fprintf(stderr, "[librdkFwupdateMgr] registerProcess: D-Bus init failed: %s\n",
                error ? error->message : "unknown");
        if (error) g_error_free(error);
        return NULL;
    }
    
    /* Call daemon's RegisterProcess method */
    error = NULL;
    uint64_t daemonHandleId = dbus_call_register_process(
        processName, libVersion, &error);
    
    if (daemonHandleId == 0) {
        fprintf(stderr, "[librdkFwupdateMgr] registerProcess: D-Bus call failed: %s\n",
                error ? error->message : "unknown");
        if (error) g_error_free(error);
        return NULL;
    }
    
    /* Create internal handle */
    InternalHandle *handle = handle_create(
        processName, libVersion, daemonHandleId);
    
    if (!handle) {
        fprintf(stderr, "[librdkFwupdateMgr] registerProcess: handle_create failed\n");
        
        /* Try to unregister from daemon to avoid leaked registration */
        GError *cleanup_error = NULL;
        dbus_call_unregister_process(daemonHandleId, &cleanup_error);
        if (cleanup_error) {
            fprintf(stderr, "[librdkFwupdateMgr] WARNING: Failed to cleanup daemon registration: %s\n",
                    cleanup_error->message);
            g_error_free(cleanup_error);
        }
        
        return NULL;
    }
    
    /* Add to registry */
    if (!registry_add(handle)) {
        fprintf(stderr, "[librdkFwupdateMgr] registerProcess: registry_add failed\n");
        
        /* Cleanup */
        GError *cleanup_error = NULL;
        dbus_call_unregister_process(daemonHandleId, &cleanup_error);
        if (cleanup_error) {
            fprintf(stderr, "[librdkFwupdateMgr] WARNING: Failed to cleanup daemon registration: %s\n",
                    cleanup_error->message);
            g_error_free(cleanup_error);
        }
        handle_destroy(handle);
        
        return NULL;
    }
    
    fprintf(stderr, "[librdkFwupdateMgr] registerProcess successful: %s v%s (handle ID: %" G_GUINT64_FORMAT ")\n",
            processName, libVersion, daemonHandleId);
    
    /* Return as opaque pointer */
    return (FirmwareInterfaceHandle)handle;
}

/**
 * @brief Unregister process from firmware update daemon
 * 
 * Public API implementation.
 */
void unregisterProcess(FirmwareInterfaceHandle handle)
{
    if (!handle) {
        /* NULL is acceptable - no-op */
        return;
    }
    
    InternalHandle *internal = (InternalHandle*)handle;
    
    /* Validate handle */
    if (!handle_validate(internal)) {
        fprintf(stderr, "[librdkFwupdateMgr] unregisterProcess: invalid handle (bad magic number)\n");
        return;
    }
    
    uint64_t daemonHandleId = internal->daemon_handle_id;
    
    fprintf(stderr, "[librdkFwupdateMgr] unregisterProcess: handle ID %" G_GUINT64_FORMAT "\n", daemonHandleId);
    
    /* Remove from registry first */
    registry_remove(internal);
    
    /* Call daemon's UnregisterProcess method */
    GError *error = NULL;
    if (!dbus_call_unregister_process(daemonHandleId, &error)) {
        fprintf(stderr, "[librdkFwupdateMgr] unregisterProcess: D-Bus call failed: %s\n",
                error ? error->message : "unknown");
        /* Continue with cleanup even if D-Bus call failed */
        if (error) g_error_free(error);
    }
    
    /* Destroy handle */
    handle_destroy(internal);
    
    fprintf(stderr, "[librdkFwupdateMgr] unregisterProcess successful (handle ID: %" G_GUINT64_FORMAT ")\n", 
            daemonHandleId);
}

/**
 * @brief Check for firmware updates (stub - Subtask 3)
 */
CheckForUpdateResult checkForUpdate(
    FirmwareInterfaceHandle handle,
    UpdateEventCallback callback,
    void *userData)
{
    (void)handle;
    (void)callback;
    (void)userData;
    
    fprintf(stderr, "[librdkFwupdateMgr] checkForUpdate: NOT YET IMPLEMENTED (Subtask 3)\n");
    return CHECK_FOR_UPDATE_FAIL;
}

/**
 * @brief Subscribe to update events (stub - Subtask 3)
 */
SubscribeResult subscribeToUpdateEvents(
    FirmwareInterfaceHandle handle,
    UpdateEventCallback callback,
    void *userData)
{
    (void)handle;
    (void)callback;
    (void)userData;
    
    fprintf(stderr, "[librdkFwupdateMgr] subscribeToUpdateEvents: NOT YET IMPLEMENTED (Subtask 3)\n");
    return SUBSCRIBE_FAILED;
}

/**
 * @brief Download firmware (stub - Subtask 4)
 */
DownloadResult downloadFirmware(
    FirmwareInterfaceHandle handle,
    const FwDwnlReq *request,
    DownloadCallback callback,
    void *userData)
{
    (void)handle;
    (void)request;
    (void)callback;
    (void)userData;
    
    fprintf(stderr, "[librdkFwupdateMgr] downloadFirmware: NOT YET IMPLEMENTED (Subtask 4)\n");
    return RDKFW_DWNL_FAILED;
}

/**
 * @brief Update firmware (stub - Subtask 5)
 */
UpdateResult updateFirmware(
    FirmwareInterfaceHandle handle,
    const FwUpdateReq *request,
    UpdateCallback callback,
    void *userData)
{
    (void)handle;
    (void)request;
    (void)callback;
    (void)userData;
    
    fprintf(stderr, "[librdkFwupdateMgr] updateFirmware: NOT YET IMPLEMENTED (Subtask 5)\n");
    return RDKFW_UPDATE_FAILED;
}
