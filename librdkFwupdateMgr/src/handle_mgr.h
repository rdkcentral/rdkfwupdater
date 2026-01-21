/**
 * @file handle_mgr.h
 * @brief Internal handle management (not exposed to clients)
 */

#ifndef HANDLE_MGR_H
#define HANDLE_MGR_H

#include <stdint.h>
#include <pthread.h>
#include "rdkFwupdateMgr_client.h"

/* Magic number for handle validation */
#define HANDLE_MAGIC 0xFEEDFACE

/**
 * @brief Internal handle structure
 * 
 * This is NOT exposed to clients. Clients only see opaque FirmwareInterfaceHandle.
 */
typedef struct {
    uint32_t magic;                    /* Validation marker (0xFEEDFACE) */
    uint64_t daemon_handle_id;         /* Handle ID from daemon */
    char *process_name;                /* Process name (owned by handle) */
    char *lib_version;                 /* Library version (owned by handle) */
    
    /* Callback storage */
    UpdateEventCallback update_event_cb;
    void *update_event_user_data;
    
    DownloadCallback download_cb;
    void *download_user_data;
    
    UpdateCallback update_cb;
    void *update_user_data;
    
    pthread_mutex_t lock;              /* Protects this handle's callbacks */
} InternalHandle;

/**
 * @brief Create a new handle
 * 
 * @param processName Process name
 * @param libVersion Library version
 * @param daemonHandleId Handle ID returned by daemon
 * @return Pointer to allocated handle, or NULL on failure
 */
InternalHandle* handle_create(const char *processName, 
                              const char *libVersion,
                              uint64_t daemonHandleId);

/**
 * @brief Destroy a handle and free resources
 * 
 * @param handle Handle to destroy
 */
void handle_destroy(InternalHandle *handle);

/**
 * @brief Validate handle
 * 
 * @param handle Handle to validate
 * @return 1 if valid, 0 if invalid
 */
int handle_validate(InternalHandle *handle);

#endif /* HANDLE_MGR_H */
