/**
 * @file handle_mgr.c
 * @brief Handle management implementation
 */

#include "handle_mgr.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/**
 * @brief Create a new handle
 */
InternalHandle* handle_create(const char *processName, 
                              const char *libVersion,
                              uint64_t daemonHandleId)
{
    if (!processName || !libVersion) {
        fprintf(stderr, "handle_create: NULL parameter\n");
        return NULL;
    }
    
    /* Allocate and zero-initialize */
    InternalHandle *handle = calloc(1, sizeof(InternalHandle));
    if (!handle) {
        fprintf(stderr, "handle_create: calloc failed\n");
        return NULL;
    }
    
    /* Set magic number */
    handle->magic = HANDLE_MAGIC;
    
    /* Store daemon handle ID */
    handle->daemon_handle_id = daemonHandleId;
    
    /* Copy strings */
    handle->process_name = strdup(processName);
    if (!handle->process_name) {
        fprintf(stderr, "handle_create: strdup(processName) failed\n");
        free(handle);
        return NULL;
    }
    
    handle->lib_version = strdup(libVersion);
    if (!handle->lib_version) {
        fprintf(stderr, "handle_create: strdup(libVersion) failed\n");
        free(handle->process_name);
        free(handle);
        return NULL;
    }
    
    /* Initialize mutex */
    if (pthread_mutex_init(&handle->lock, NULL) != 0) {
        fprintf(stderr, "handle_create: pthread_mutex_init failed\n");
        free(handle->lib_version);
        free(handle->process_name);
        free(handle);
        return NULL;
    }
    
    /* Callbacks are NULL (calloc zero-initialized) */
    
    return handle;
}

/**
 * @brief Destroy a handle
 */
void handle_destroy(InternalHandle *handle)
{
    if (!handle) {
        return;
    }
    
    /* Validate before destroying */
    if (handle->magic != HANDLE_MAGIC) {
        fprintf(stderr, "handle_destroy: Invalid magic number 0x%X\n", handle->magic);
        return;
    }
    
    /* Invalidate magic to catch use-after-free */
    handle->magic = 0;
    
    /* Destroy mutex */
    pthread_mutex_destroy(&handle->lock);
    
    /* Free strings */
    free(handle->process_name);
    free(handle->lib_version);
    
    /* Free handle */
    free(handle);
}

/**
 * @brief Validate handle
 */
int handle_validate(InternalHandle *handle)
{
    if (!handle) {
        return 0;
    }
    
    if (handle->magic != HANDLE_MAGIC) {
        return 0;
    }
    
    return 1;
}
