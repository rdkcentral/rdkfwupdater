/**
 * @file signal_handler.h
 * @brief D-Bus signal subscription and routing
 * 
 * This module manages D-Bus signal subscriptions and routes incoming signals
 * to the appropriate handle callbacks. Handles CheckForUpdateComplete,
 * DownloadProgress, and UpdateProgress signals from the daemon.
 */

#ifndef SIGNAL_HANDLER_H
#define SIGNAL_HANDLER_H

#include <glib.h>
#include <gio/gio.h>
#include <stdbool.h>

/**
 * @brief Initialize signal handling subsystem
 * 
 * Must be called before subscribing to any signals.
 * Idempotent - safe to call multiple times.
 * 
 * @return true on success, false on failure
 */
bool signal_handler_init(void);

/**
 * @brief Cleanup signal handling subsystem
 * 
 * Unsubscribes all signals and releases resources.
 * Should be called during library cleanup.
 */
void signal_handler_cleanup(void);

/**
 * @brief Subscribe to CheckForUpdateComplete signal for a handle
 * 
 * Registers a signal handler that will route CheckForUpdateComplete
 * signals for the given daemon_handle_id to the appropriate handle.
 * 
 * Should be called once per handle during checkForUpdate().
 * Idempotent - subscribing twice for same handle is safe (no-op).
 * 
 * @param daemon_handle_id Daemon handle ID to filter signals for
 * @return true on success, false on failure
 */
bool signal_handler_subscribe_check_update(uint64_t daemon_handle_id);

/**
 * @brief Unsubscribe from all signals for a handle
 * 
 * Called during unregisterProcess() to cleanup signal subscriptions.
 * Safe to call even if no subscriptions exist for this handle.
 * 
 * @param daemon_handle_id Daemon handle ID
 */
void signal_handler_unsubscribe_all(uint64_t daemon_handle_id);

#endif /* SIGNAL_HANDLER_H */
