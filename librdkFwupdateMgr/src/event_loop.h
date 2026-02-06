/**
 * @file event_loop.h
 * @brief Background GLib main loop for async D-Bus signal handling
 * 
 * This module manages a dedicated background thread running a GLib main loop
 * to handle incoming D-Bus signals for async operations (checkForUpdate,
 * downloadFirmware, updateFirmware).
 * 
 * Thread Safety:
 * - All functions are thread-safe
 * - Single event loop thread created on first checkForUpdate()
 * - Event loop runs until library cleanup
 */

#ifndef EVENT_LOOP_H
#define EVENT_LOOP_H

#include <glib.h>
#include <stdbool.h>

/**
 * @brief Start the event loop thread (idempotent)
 * 
 * Creates a background thread running a GLib main loop to process
 * D-Bus signals. If already started, this is a no-op.
 * 
 * Must be called before any async operations (checkForUpdate, etc.).
 * Automatically called by checkForUpdate() on first invocation.
 * 
 * @return true on success (or already running), false on failure
 */
bool event_loop_start(void);

/**
 * @brief Stop the event loop thread
 * 
 * Signals the event loop to quit and waits for thread termination.
 * Should be called during library cleanup.
 * 
 * Safe to call even if not running (no-op).
 */
void event_loop_stop(void);

/**
 * @brief Check if event loop is running
 * 
 * @return true if running, false otherwise
 */
bool event_loop_is_running(void);

/**
 * @brief Get the event loop's GMainContext
 * 
 * Used by signal subscription code to attach D-Bus signal handlers
 * to the correct thread's main loop.
 * 
 * @return GMainContext pointer, or NULL if not running
 */
GMainContext* event_loop_get_context(void);

#endif /* EVENT_LOOP_H */
