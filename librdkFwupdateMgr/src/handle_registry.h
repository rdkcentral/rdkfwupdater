/**
 * @file handle_registry.h
 * @brief Internal handle registry for tracking active handles
 * 
 * Provides storage and lookup for registered handles. Used for:
 * - Tracking all active client handles
 * - Looking up handles by daemon handle ID (for signal routing)
 * - Cleanup on library shutdown
 */

#ifndef HANDLE_REGISTRY_H
#define HANDLE_REGISTRY_H

#include "handle_mgr.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Initialize handle registry
 * 
 * Should be called once during library initialization.
 * Thread-safe - uses internal rwlock.
 */
void registry_init(void);

/**
 * @brief Cleanup handle registry
 * 
 * Destroys any remaining handles and cleans up resources.
 * Should be called during library shutdown.
 */
void registry_cleanup(void);

/**
 * @brief Add handle to registry
 * 
 * @param handle Handle to add
 * @return TRUE on success, FALSE if registry full or invalid handle
 */
bool registry_add(InternalHandle *handle);

/**
 * @brief Remove handle from registry by pointer
 * 
 * @param handle Handle to remove
 * @return TRUE if found and removed, FALSE otherwise
 */
bool registry_remove(InternalHandle *handle);

/**
 * @brief Find handle by daemon handle ID
 * 
 * Used for signal routing - when daemon sends signal with handleId,
 * we need to find which InternalHandle it corresponds to.
 * 
 * @param daemonHandleId Handle ID from daemon
 * @return Handle pointer, or NULL if not found
 */
InternalHandle* registry_find_by_id(uint64_t daemonHandleId);

/**
 * @brief Get count of active handles
 * 
 * @return Number of registered handles
 */
int registry_count(void);

/**
 * @brief Check if registry is initialized
 * 
 * @return TRUE if initialized, FALSE otherwise
 */
bool registry_is_initialized(void);

#endif /* HANDLE_REGISTRY_H */
