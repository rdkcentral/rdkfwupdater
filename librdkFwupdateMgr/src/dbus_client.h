/**
 * @file dbus_client.h
 * @brief D-Bus client connection and method call wrappers
 * 
 * Internal module for managing D-Bus communication with the
 * RDK Firmware Update Manager daemon.
 */

#ifndef DBUS_CLIENT_H
#define DBUS_CLIENT_H

#include <glib.h>
#include <gio/gio.h>
#include <stdint.h>
#include <stdbool.h>

/* D-Bus service details - from daemon rdkv_dbus_server.h */
#define DBUS_SERVICE_NAME       "org.rdkfwupdater.Service"
#define DBUS_OBJECT_PATH        "/org/rdkfwupdater/Service"
#define DBUS_INTERFACE_NAME     "org.rdkfwupdater.Interface"

/**
 * @brief Initialize D-Bus connection (lazy init on first register)
 * 
 * Connects to system D-Bus and prepares for method calls.
 * Thread-safe - uses internal mutex.
 * 
 * @param error Optional GError for error reporting
 * @return TRUE on success, FALSE on failure
 */
gboolean dbus_client_init(GError **error);

/**
 * @brief Cleanup D-Bus connection
 * 
 * Should be called during library shutdown (e.g., on last unregister).
 * Thread-safe.
 */
void dbus_client_cleanup(void);

/**
 * @brief Call daemon's RegisterProcess method
 * 
 * @param processName Client process name (e.g., "VideoApp")
 * @param libVersion Library version (e.g., "1.0.0")
 * @param error Optional GError for error reporting
 * @return Daemon-assigned handle ID (uint64), or 0 on failure
 */
uint64_t dbus_call_register_process(
    const char *processName,
    const char *libVersion,
    GError **error
);

/**
 * @brief Call daemon's UnregisterProcess method
 * 
 * @param handleId Daemon handle ID from RegisterProcess
 * @param error Optional GError for error reporting
 * @return TRUE on success, FALSE on failure
 */
gboolean dbus_call_unregister_process(
    uint64_t handleId,
    GError **error
);

/**
 * @brief Call daemon's CheckForUpdate method
 * 
 * Triggers firmware update check on daemon. Result is returned via
 * CheckForUpdateComplete D-Bus signal (async).
 * 
 * @param handleId Daemon handle ID from RegisterProcess
 * @param error Optional GError for error reporting
 * @return TRUE on success (method call succeeded), FALSE on failure
 */
gboolean dbus_call_check_for_update(
    uint64_t handleId,
    GError **error
);

/**
 * @brief Get current D-Bus connection
 * 
 * Used by other modules for signal subscriptions.
 * 
 * @return GDBusConnection pointer, or NULL if not initialized
 */
GDBusConnection* dbus_get_connection(void);

/**
 * @brief Check if D-Bus connection is initialized
 * 
 * @return TRUE if connected, FALSE otherwise
 */
gboolean dbus_is_initialized(void);

#endif /* DBUS_CLIENT_H */
