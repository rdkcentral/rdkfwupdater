/*
 * Copyright 2026 Comcast Cable Communications Management, LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file rdkFwupdateMgr_async.c
 * @brief Internal engine: registry, background thread, signal dispatch
 *
 * Owns:
 *   - Global callback registry (one slot per pending checkForUpdate call)
 *   - Background GLib event loop thread
 *   - D-Bus signal subscription and handler
 *   - Dispatch: signal arrives → find all PENDING → fire each callback
 *
 * Apps never interact with this file directly.
 * All entry points are through rdkFwupdateMgr_api.c.
 */

#include "rdkFwupdateMgr_async_internal.h"
#include "rdkFwupdateMgr_log.h"
#include "rdkFwupdateMgr_client.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <inttypes.h>  /* For PRIu64 */

/* ========================================================================
 * GLOBAL STATE
 * ======================================================================== */

static CallbackRegistry g_registry;
static BackgroundThread g_bg_thread;
static DwnlCallbackRegistry g_dwnl_registry;
static UpdateCbRegistry g_update_registry;

/* ========================================================================
 * FORWARD DECLARATIONS
 * ======================================================================== */

static void *background_thread_func(void *arg);

static void  on_check_complete_signal(GDBusConnection *conn,
                                      const gchar *sender,
                                      const gchar *object_path,
                                      const gchar *interface_name,
                                      const gchar *signal_name,
                                      GVariant *parameters,
                                      gpointer user_data);

static void  on_download_progress_signal(GDBusConnection *conn,
                                         const gchar *sender,
                                         const gchar *object_path,
                                         const gchar *interface_name,
                                         const gchar *signal_name,
                                         GVariant *parameters,
                                         gpointer user_data);

static void  on_update_progress_signal(GDBusConnection *conn,
                                       const gchar *sender,
                                       const gchar *object_path,
                                       const gchar *interface_name,
                                       const gchar *signal_name,
                                       GVariant *parameters,
                                       gpointer user_data);

static void  dispatch_all_pending(const InternalSignalData *signal_data);
static void  registry_reset_slot(CallbackEntry *entry);
static bool  parse_update_details(const char *update_details_str,
                                   UpdateDetails *out_details);

/* Forward declaration for download status mapping function */
static DownloadStatus map_dwnl_status_string(const char *status_str);

/* Forward declarations for cleanup functions */
static void internal_dwnl_system_deinit(void);
static void internal_update_system_deinit(void);

/* ========================================================================
 * LIBRARY LIFECYCLE
 * ======================================================================== */

/**
 * @brief Initialize the internal system
 *
 * STEPS:
 *   1. Zero and mutex-init the registry
 *   2. Create isolated GLib context + event loop
 *   3. Spawn background thread
 *   4. Wait until background thread confirms it is ready
 *      (ensures signal subscription exists before any D-Bus call is fired)
 */
int internal_system_init(void)
{
    FWUPMGR_INFO("internal_system_init: begin\n");

    /* Registry */
    memset(&g_registry, 0, sizeof(g_registry));
    if (pthread_mutex_init(&g_registry.mutex, NULL) != 0) {
        FWUPMGR_ERROR("internal_system_init: registry mutex init failed\n");
        return -1;
    }
    g_registry.initialized = true;

    /* Background thread state */
    memset(&g_bg_thread, 0, sizeof(g_bg_thread));
    if (pthread_mutex_init(&g_bg_thread.mutex, NULL) != 0) {
        FWUPMGR_ERROR("internal_system_init: bg thread mutex init failed\n");
        pthread_mutex_destroy(&g_registry.mutex);
        return -1;
    }

    /*
     * Isolated GLib context: prevents interference with any GLib event loop
     * the app may be running on its own main thread.
     */
    g_bg_thread.context   = g_main_context_new();
    g_bg_thread.main_loop = g_main_loop_new(g_bg_thread.context, FALSE);
    g_bg_thread.running   = false;

    if (pthread_create(&g_bg_thread.thread, NULL, background_thread_func, NULL) != 0) {
        FWUPMGR_ERROR("internal_system_init: pthread_create failed\n");
        g_main_loop_unref(g_bg_thread.main_loop);
        g_main_context_unref(g_bg_thread.context);
        pthread_mutex_destroy(&g_bg_thread.mutex);
        pthread_mutex_destroy(&g_registry.mutex);
        return -1;
    }

    /*
     * Spin-wait for background thread to set running=true.
     * Max wait: 50 × 100ms = 5 seconds.
     * Ensures D-Bus signal subscription is live before checkForUpdate()
     * can send a D-Bus method call — prevents missing the response signal.
     */
    for (int i = 0; i < 50; i++) {
        pthread_mutex_lock(&g_bg_thread.mutex);
        bool ready = g_bg_thread.running;
        pthread_mutex_unlock(&g_bg_thread.mutex);
        if (ready) break;

        struct timespec ts = { .tv_sec = 0, .tv_nsec = 100 * 1000 * 1000 };
        nanosleep(&ts, NULL);
    }

    /* Initialize download and update registries */
    memset(&g_dwnl_registry, 0, sizeof(g_dwnl_registry));
    if (pthread_mutex_init(&g_dwnl_registry.mutex, NULL) != 0) {
        FWUPMGR_ERROR("internal_system_init: dwnl mutex init failed\n");
        return -1;
    }
    g_dwnl_registry.initialized = true;

    memset(&g_update_registry, 0, sizeof(g_update_registry));
    if (pthread_mutex_init(&g_update_registry.mutex, NULL) != 0) {
        FWUPMGR_ERROR("internal_system_init: update mutex init failed\n");
        pthread_mutex_destroy(&g_dwnl_registry.mutex);
        return -1;
    }
    g_update_registry.initialized = true;

    FWUPMGR_INFO("internal_system_init: ready\n");
    return 0;
}

/**
 * @brief Shut down the internal system
 *
 * STEPS:
 *   1. Quit GLib event loop → background thread exits g_main_loop_run()
 *   2. Join background thread (wait for clean exit)
 *   3. Free GLib resources
 *   4. Free any remaining strdup'd handle_key strings in registry
 *   5. Destroy mutexes
 */
void internal_system_deinit(void)
{
    FWUPMGR_INFO("internal_system_deinit: begin\n");

    if (g_bg_thread.main_loop != NULL) {
        g_main_loop_quit(g_bg_thread.main_loop);
    }

    pthread_join(g_bg_thread.thread, NULL);

    if (g_bg_thread.main_loop) g_main_loop_unref(g_bg_thread.main_loop);
    if (g_bg_thread.context)   g_main_context_unref(g_bg_thread.context);
    pthread_mutex_destroy(&g_bg_thread.mutex);

    /* Cleanup download and update registries */
    internal_dwnl_system_deinit();
    internal_update_system_deinit();

    /* Free any leftover handle_key strings from CheckForUpdate registry */
    pthread_mutex_lock(&g_registry.mutex);
    for (int i = 0; i < MAX_PENDING_CALLBACKS; i++) {
        if (g_registry.entries[i].handle_key != NULL) {
            free(g_registry.entries[i].handle_key);
            g_registry.entries[i].handle_key = NULL;
        }
    }
    pthread_mutex_unlock(&g_registry.mutex);
    pthread_mutex_destroy(&g_registry.mutex);

    FWUPMGR_INFO("internal_system_deinit: done\n");
}

/* ========================================================================
 * BACKGROUND THREAD
 * ======================================================================== */

/**
 * @brief Background thread entry point
 *
 * Runs for the lifetime of the library.
 *
 *   1. Push isolated GLib context for this thread
 *   2. Connect to system D-Bus
 *   3. Subscribe to CheckForUpdateComplete signal
 *   4. Signal main thread: ready
 *   5. g_main_loop_run() — blocks until deinit calls g_main_loop_quit()
 *   6. Cleanup: unsubscribe, release connection, pop context
 */
static void *background_thread_func(void *arg)
{
    (void)arg;
    FWUPMGR_INFO("background_thread: starting\n");

    g_main_context_push_thread_default(g_bg_thread.context);

    GError *error = NULL;
    g_bg_thread.connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    if (g_bg_thread.connection == NULL) {
        FWUPMGR_ERROR("background_thread: D-Bus connect failed: %s\n",
                      error ? error->message : "unknown");
        if (error) g_error_free(error);
        goto thread_exit;
    }

    /*
     * Subscribe to the CheckForUpdateComplete signal.
     *
     * sender = NULL  → accept from any sender
     *                  (daemon's well-known name may vary by deployment)
     * arg0   = NULL  → no filter on first argument
     *
     * GLib calls on_check_complete_signal() in THIS thread's context
     * whenever the signal arrives.
     */
    g_bg_thread.subscription_id = g_dbus_connection_signal_subscribe(
        g_bg_thread.connection,
        NULL,                        /* sender: any                       */
        DBUS_INTERFACE_NAME,         /* interface                         */
        DBUS_SIGNAL_COMPLETE,        /* signal: CheckForUpdateComplete    */
        DBUS_OBJECT_PATH,            /* object path                       */
        NULL,                        /* arg0 filter: none                 */
        G_DBUS_SIGNAL_FLAGS_NONE,
        on_check_complete_signal,    /* handler                           */
        NULL,                        /* user_data: not needed (use globals)*/
        NULL                         /* user_data destroy notify          */
    );

    FWUPMGR_INFO("background_thread: subscribed to CheckForUpdateComplete (id=%u)\n",
                 g_bg_thread.subscription_id);

    /*
     * Subscribe to DownloadProgress and UpdateProgress signals.
     * Must be done HERE in the background thread, not from main thread,
     * because the connection belongs to this thread's GMainContext.
     */
    guint dwnl_sub_id = g_dbus_connection_signal_subscribe(
        g_bg_thread.connection,
        NULL,                           /* sender: any                        */
        DBUS_INTERFACE_NAME,            /* interface                          */
        DBUS_SIGNAL_DWNL_PROGRESS,     /* signal: DownloadProgress           */
        DBUS_OBJECT_PATH,              /* object path                         */
        NULL,                          /* arg0 filter: none                   */
        G_DBUS_SIGNAL_FLAGS_NONE,
        on_download_progress_signal,   /* handler                             */
        NULL,
        NULL
    );
    FWUPMGR_INFO("background_thread: subscribed to DownloadProgress (id=%u)\n", dwnl_sub_id);

    guint update_sub_id = g_dbus_connection_signal_subscribe(
        g_bg_thread.connection,
        NULL,                           /* sender: any                        */
        DBUS_INTERFACE_NAME,            /* interface                          */
        DBUS_SIGNAL_UPDATE_PROGRESS,   /* signal: UpdateProgress             */
        DBUS_OBJECT_PATH,              /* object path                         */
        NULL,                          /* arg0 filter: none                   */
        G_DBUS_SIGNAL_FLAGS_NONE,
        on_update_progress_signal,     /* handler                             */
        NULL,
        NULL
    );
    FWUPMGR_INFO("background_thread: subscribed to UpdateProgress (id=%u)\n", update_sub_id);

    /* Signal main thread that we are ready */
    pthread_mutex_lock(&g_bg_thread.mutex);
    g_bg_thread.running = true;
    pthread_mutex_unlock(&g_bg_thread.mutex);

    /* Block here until internal_system_deinit() calls g_main_loop_quit() */
    g_main_loop_run(g_bg_thread.main_loop);
    FWUPMGR_INFO("background_thread: event loop exited\n");

    if (g_bg_thread.subscription_id != 0) {
        g_dbus_connection_signal_unsubscribe(g_bg_thread.connection,
                                             g_bg_thread.subscription_id);
    }
    g_object_unref(g_bg_thread.connection);
    g_bg_thread.connection = NULL;

thread_exit:
    g_main_context_pop_thread_default(g_bg_thread.context);
    FWUPMGR_INFO("background_thread: exiting\n");
    return NULL;
}

/* ========================================================================
 * D-BUS SIGNAL HANDLER
 * ======================================================================== */

/*
 * on_check_complete_signal - D-Bus signal handler for CheckForUpdateComplete.
 *
 * OVERVIEW
 *
 * PURPOSE:
 *   This function is called by GLib's D-Bus infrastructure when the daemon
 *   broadcasts the "CheckForUpdateComplete" signal. It is the ENTRY POINT
 *   for the "response" side of the checkForUpdate() async flow.
 *
 * WHEN DOES THIS FIRE?
 *   5-30 seconds after checkForUpdate() was called. The daemon queried
 *   the XConf cloud server for firmware availability, got a response,
 *   and broadcast the result as a D-Bus signal to ALL listeners.
 *
 * WHICH THREAD RUNS THIS?
 *   The BACKGROUND THREAD. Not the main thread. This is critical.
 *
 *   The BG thread is blocked in g_main_loop_run() waiting for events.
 *   When the signal arrives on the BG thread's persistent D-Bus connection
 *   (:1.141), GLib wakes the BG thread and dispatches to this handler.
 *   This handler was registered via g_dbus_connection_signal_subscribe()
 *   in background_thread_func() during registerProcess().
 *
 * PARAMETERS:
 *   conn           -- the BG thread's persistent D-Bus connection (:1.141)
 *   sender         -- the daemon's unique sender name (e.g., ":1.5")
 *   object_path    -- "/org/rdkfwupdater/Service"
 *   interface_name -- "org.rdkfwupdater.Interface"
 *   signal_name    -- "CheckForUpdateComplete"
 *   parameters     -- GVariant of type "(tiissss)" containing the result
 *   user_data      -- NULL (we use global state, not user_data)
 *
 *   All parameters except 'parameters' are unused (cast to void).
 *   We only care about the GVariant payload.
 *
 * EXECUTION FLOW:
 *   1. Parse the GVariant "(tiissss)" into an InternalSignalData struct
 *      (4 strdup'd strings: current_version, available_version,
 *       update_details, status_message)
 *   2. Call dispatch_all_pending() which:
 *      a. Finds all PENDING registry entries
 *      b. Builds FwInfoData from the signal data
 *      c. Invokes each callback
 *      d. Resets each slot to IDLE
 *   3. Free the 4 strdup'd strings via internal_cleanup_signal_data()
 *
 * ERROR HANDLING:
 *   If GVariant parsing fails (wrong type signature, corrupt data),
 *   we log an error and return without dispatching. Callbacks will
 *   NOT fire. The caller's condvar timeout will eventually expire.
 *
 * MEMORY:
 *   internal_parse_signal_data() allocates 4 strings via strdup().
 *   internal_cleanup_signal_data() frees them after dispatch completes.
 *   The InternalSignalData struct itself is on the stack (this function's
 *   stack frame on the BG thread).
 *
 * After this function returns, the BG thread goes back to
 * g_main_loop_run() and sleeps until the next signal.
 */
static void on_check_complete_signal(GDBusConnection *conn,
                                     const gchar *sender,
                                     const gchar *object_path,
                                     const gchar *interface_name,
                                     const gchar *signal_name,
                                     GVariant *parameters,
                                     gpointer user_data)
{
    /*
     * Suppress "unused parameter" warnings. GLib's signal handler
     * signature requires all 7 parameters, but we only use 'parameters'.
     * (void) casts are the standard C idiom for this.
     */
    (void)conn; (void)sender; (void)object_path;
    (void)interface_name; (void)signal_name; (void)user_data;

    FWUPMGR_INFO("on_check_complete_signal: received\n");

    /*
     * Stack-allocated struct to hold the parsed signal data.
     * memset to zero ensures all pointers start as NULL (important
     * for cleanup -- free(NULL) is safe, free(garbage) is not).
     */
    InternalSignalData signal_data;
    memset(&signal_data, 0, sizeof(signal_data));

    /*
     * Parse the GVariant "(tiissss)" payload into our struct.
     * See internal_parse_signal_data() below for details.
     *
     * If parsing fails (wrong signature, NULL parameters), we return
     * without dispatching. The caller's condvar timeout will fire.
     * This is the correct behavior -- we can't dispatch garbage data.
     */
    if (!internal_parse_signal_data(parameters, &signal_data)) {
        FWUPMGR_ERROR("on_check_complete_signal: parse failed\n");
        return;
    }

    /*
     * Dispatch to all registered callbacks. This is where the actual
     * callback invocation happens. See dispatch_all_pending() below
     * for the detailed two-phase dispatch explanation.
     */
    dispatch_all_pending(&signal_data);

    /*
     * Free the 4 strdup'd strings in signal_data.
     * After dispatch_all_pending() returns, all callbacks have completed
     * and no one holds references to these strings anymore.
     *
     * internal_cleanup_signal_data() calls free() on each string
     * and memset's the struct to zero (defensive cleanup).
     */
    internal_cleanup_signal_data(&signal_data);
}

/*
 * dispatch_all_pending - Find all PENDING callbacks and invoke them.
 *
 * OVERVIEW
 *
 * PURPOSE:
 *   This is the CORE of the async engine. When a CheckForUpdateComplete
 *   signal arrives, this function finds every PENDING callback in the
 *   registry, builds the FwInfoData struct from the signal payload,
 *   and invokes each callback.
 *
 * WHY "ALL PENDING" (NOT JUST ONE)?
 *   If multiple clients (or the same client calling checkForUpdate
 *   multiple times) have PENDING entries, they all get the same
 *   firmware check result. The daemon broadcasts ONE signal and
 *   ALL pending callbacks receive it. This is the "fan-out" pattern.
 *
 *   In the typical single-client case, there is exactly 1 PENDING entry.
 *
 * TWO-PHASE DESIGN -- THE DEADLOCK PREVENTION PATTERN:
 *
 *   PHASE 1 (mutex HELD):
 *     Lock g_registry.mutex.
 *     Scan all 30 entries. For each PENDING entry:
 *       - Copy its callback pointer and handle into a local stack array
 *       - Change its state from PENDING to DISPATCHED
 *     Unlock g_registry.mutex.
 *
 *   PHASE 2 (mutex RELEASED):
 *     Build FwInfoData from signal_data (stack-allocated).
 *     For each entry in the snapshot:
 *       - Invoke: callback(&fwinfo_data)
 *       - After callback returns: lock mutex, reset slot to IDLE, unlock
 *
 *   WHY NOT HOLD THE MUTEX DURING CALLBACK INVOCATION?
 *
 *   Scenario that would deadlock WITHOUT two-phase:
 *     1. BG thread holds g_registry.mutex
 *     2. BG thread calls callback(&fwinfo_data)
 *     3. Inside the callback, the app calls checkForUpdate() again
 *        (re-entrant use -- not common, but must be safe)
 *     4. checkForUpdate() calls internal_register_callback()
 *     5. internal_register_callback() calls pthread_mutex_lock(&g_registry.mutex)
 *     6. DEADLOCK -- the BG thread is already holding that mutex
 *        (from step 1), and it's the same thread trying to re-acquire it
 *
 *   With two-phase, the mutex is released BEFORE step 2, so step 5
 *   would succeed (no one holds the mutex).
 *
 *   Even if the callback does NOT call checkForUpdate() again, holding
 *   the mutex during a potentially slow callback (imagine the callback
 *   does heavy work -- file I/O, network, etc.) would block the main
 *   thread from registering new callbacks until the slow callback finishes.
 *   Two-phase keeps the critical section (Phase 1) fast: just a scan
 *   and copy, microseconds.
 *
 * WHY MARK AS DISPATCHED (NOT JUST SKIP IDLE)?
 *   DISPATCHED is an intermediate state between PENDING and IDLE.
 *   It means "we're about to call this callback but haven't finished yet."
 *   If another signal arrives while Phase 2 is running (very unlikely but
 *   possible), the next dispatch_all_pending() call would see DISPATCHED
 *   and skip it -- preventing double-dispatch of the same callback.
 *
 * MEMORY MODEL:
 *   - The Snapshot struct is stack-allocated (local array of 30 entries)
 *   - Each Snapshot copies the callback pointer and handle string
 *   - FwInfoData is stack-allocated in this function's frame
 *   - UpdateDetails is stack-allocated in this function's frame
 *   - ALL of this data is valid ONLY during callback execution
 *   - When this function returns, all stack data is gone
 *   - The callback MUST copy any data it needs before returning
 *
 * THREAD: Always runs on the BG thread (called from on_check_complete_signal).
 *
 * @param signal_data  Parsed signal payload from internal_parse_signal_data().
 *                     Contains strdup'd strings -- valid until cleanup.
 *
 * Called by: on_check_complete_signal()
 * Calls:    internal_map_status_code(), parse_update_details(),
 *           each registered callback, registry_reset_slot()
 */
static void dispatch_all_pending(const InternalSignalData *signal_data)
{
    /*
     * Local snapshot struct -- what we copy from each PENDING entry.
     *
     * typedef here (inside the function) because this struct is only
     * used locally -- no other function needs it.
     *
     * callback:     the function pointer to invoke
     * handle_copy:  a copy of the handle string (256 bytes, more than enough
     *               for handle strings like "1" or "12345")
     * slot_index:   which entry in g_registry.entries[] this came from
     *               (needed to reset the slot to IDLE after callback returns)
     */
    typedef struct {
        UpdateEventCallback  callback;
        char                 handle_copy[256];
        int                  slot_index;
    } Snapshot;

    /*
     * Stack-allocated array of snapshots. 30 entries x ~270 bytes each
     * = ~8KB on the stack. Well within the BG thread's 8MB stack limit.
     *
     * count tracks how many PENDING entries we found.
     */
    Snapshot snapshots[MAX_PENDING_CALLBACKS];
    int      count = 0;

    /* ================================================================
     * PHASE 1: COLLECT PENDING ENTRIES UNDER MUTEX
     *
     * This is the fast critical section. We hold the mutex for as short
     * as possible: scan 30 entries, copy the ones we need, release.
     * Total time: microseconds.
     * ================================================================ */
    pthread_mutex_lock(&g_registry.mutex);

    for (int i = 0; i < MAX_PENDING_CALLBACKS; i++) {
        CallbackEntry *e = &g_registry.entries[i];

        /*
         * Skip anything that isn't PENDING.
         *   IDLE       -- empty slot, nothing to do
         *   DISPATCHED -- another dispatch is already handling this
         *   TIMED_OUT  -- expired, will be cleaned up separately
         */
        if (e->state != CB_STATE_PENDING) continue;

        /*
         * Found a PENDING entry. Copy its essential data into our
         * local snapshot so we can invoke it after releasing the mutex.
         *
         * We copy the function pointer (8 bytes) and the handle string
         * (up to 256 bytes via snprintf). We also record the slot index
         * so we can reset the exact slot to IDLE later.
         *
         * snprintf with "%s" safely copies the string with null termination.
         * If handle_key is NULL (should never happen for PENDING entries,
         * but defensive), we copy an empty string.
         */
        snapshots[count].callback   = e->callback;
        snapshots[count].slot_index = i;
        snprintf(snapshots[count].handle_copy,
                 sizeof(snapshots[count].handle_copy),
                 "%s", e->handle_key ? e->handle_key : "");

        /*
         * Mark the slot as DISPATCHED. This is the transition:
         *   PENDING -> DISPATCHED
         *
         * This prevents:
         *   1. Another signal dispatch from calling this callback again
         *   2. A new checkForUpdate() from overwriting this slot
         *      (dedup only matches PENDING, not DISPATCHED)
         *
         * The slot will go DISPATCHED -> IDLE after the callback returns
         * (in Phase 2's loop below).
         */
        e->state = CB_STATE_DISPATCHED;
        count++;

        FWUPMGR_INFO("dispatch_all_pending: queued handle='%s'\n",
                     e->handle_key ? e->handle_key : "(null)");
    }

    /*
     * Release the mutex. Phase 1 is done.
     * From this point on, the main thread is free to register new
     * callbacks via internal_register_callback() -- no blocking.
     */
    pthread_mutex_unlock(&g_registry.mutex);

    FWUPMGR_INFO("dispatch_all_pending: %d callback(s) to fire\n", count);

    /* ================================================================
     * PHASE 2: BUILD DATA AND INVOKE CALLBACKS (NO MUTEX HELD)
     *
     * Now we build the FwInfoData struct that callbacks receive,
     * and invoke each callback in sequence. No mutex is held during
     * any of this -- safe for re-entrant use.
     * ================================================================ */

    /*
     * Map the daemon's integer status_code to our public enum.
     *
     * The daemon sends status_code as an integer in the signal:
     *   0 = FIRMWARE_AVAILABLE
     *   1 = FIRMWARE_NOT_AVAILABLE
     *   2 = UPDATE_NOT_ALLOWED
     *   3 = FIRMWARE_CHECK_ERROR
     *   4 = IGNORE_OPTOUT
     *   5 = BYPASS_OPTOUT
     *
     * internal_map_status_code() converts this to our CheckForUpdateStatus enum.
     * Unknown values map to FIRMWARE_CHECK_ERROR (safe default).
     */
    CheckForUpdateStatus status = internal_map_status_code(signal_data->status_code);

    /*
     * Build FwInfoData on the STACK.
     *
     * This is the struct that callbacks receive via their
     * (const FwInfoData *fwinfodata) parameter.
     *
     * CRITICAL: This struct is stack-allocated. It exists only while
     * this function is running. When dispatch_all_pending() returns
     * (after all callbacks have been invoked and returned), this stack
     * frame is destroyed and fwinfo_data becomes invalid.
     *
     * This is why callbacks MUST copy any data they need before returning.
     * The example_app does this:
     *   strncpy(g_fw_filename, event_data->UpdateDetails->FwFileName, ...);
     *
     * memset to zero ensures all char arrays start as empty strings
     * (first byte is '\0') and all pointers start as NULL.
     */
    FwInfoData fwinfo_data;
    memset(&fwinfo_data, 0, sizeof(fwinfo_data));

    /*
     * Copy the current firmware version from the signal data into
     * fwinfo_data.CurrFWVersion (a char[64] array).
     *
     * strncpy with sizeof()-1 ensures we never overflow the buffer.
     * The explicit null-termination on the next line is a safety net
     * in case signal_data->current_version is >= 63 characters
     * (strncpy does NOT null-terminate if src >= n characters).
     *
     * If current_version is NULL (daemon didn't provide it), we skip
     * the copy and the field stays as empty string from memset.
     */
    if (signal_data->current_version) {
        strncpy(fwinfo_data.CurrFWVersion, signal_data->current_version,
                sizeof(fwinfo_data.CurrFWVersion) - 1);
        fwinfo_data.CurrFWVersion[sizeof(fwinfo_data.CurrFWVersion) - 1] = '\0';
    }

    /* Set the status enum in the struct. */
    fwinfo_data.status = status;

    /*
     * Parse UpdateDetails if firmware is available.
     *
     * The daemon sends update details as a pipe-separated string:
     *   "File:firmware_v8.bin|Location:http://cdn..|Version:RDKV_8.0|Reboot:false|..."
     *
     * We need to parse this into an UpdateDetails struct with individual
     * fields (FwFileName, FwUrl, FwVersion, RebootImmediately, etc.).
     *
     * UpdateDetails is ALSO stack-allocated. It lives in this function's
     * stack frame. fwinfo_data.UpdateDetails is a POINTER to this stack
     * variable. After this function returns, both are gone.
     *
     * We only populate UpdateDetails when status == FIRMWARE_AVAILABLE.
     * For all other statuses (NOT_AVAILABLE, ERROR, etc.), UpdateDetails
     * is set to NULL -- there's nothing to update, so no details to show.
     */
    UpdateDetails update_details;
    if (status == FIRMWARE_AVAILABLE && signal_data->update_details) {
        memset(&update_details, 0, sizeof(update_details));
        
        /*
         * parse_update_details() tokenizes the pipe-separated string
         * and copies each Key:Value pair into the appropriate field
         * of the UpdateDetails struct. See parse_update_details() below.
         */
        if (parse_update_details(signal_data->update_details, &update_details)) {
            /*
             * Point FwInfoData's UpdateDetails pointer to our stack variable.
             *
             * This is safe because: both fwinfo_data and update_details
             * live on the same stack frame. The pointer is valid as long
             * as this function is executing. The callback receives this
             * pointer and MUST copy what it needs before returning.
             */
            fwinfo_data.UpdateDetails = &update_details;
            
            FWUPMGR_INFO("dispatch_all_pending: UpdateDetails populated\n");
            FWUPMGR_INFO("  FwFileName: %s\n", update_details.FwFileName);
            FWUPMGR_INFO("  FwVersion: %s\n", update_details.FwVersion);
        } else {
            /*
             * Parsing failed (malformed string, etc.). Set to NULL so
             * the callback knows: "firmware is available but I couldn't
             * parse the details." The callback should handle this gracefully.
             */
            fwinfo_data.UpdateDetails = NULL;
            FWUPMGR_ERROR("dispatch_all_pending: parse_update_details failed\n");
        }
    } else {
        /*
         * Either firmware is NOT available, or the daemon didn't send
         * an update_details string. Set pointer to NULL.
         *
         * The callback should check: if (event_data->UpdateDetails != NULL)
         * before accessing any UpdateDetails fields.
         */
        fwinfo_data.UpdateDetails = NULL;
    }

    /*
     * Invoke each callback from our snapshot.
     *
     * For each snapshot entry:
     *   1. Call the callback with a pointer to our stack-allocated FwInfoData
     *   2. After the callback returns, lock the mutex and reset the slot to IDLE
     *
     * The same FwInfoData struct is passed to ALL callbacks. They all see
     * the same firmware check result (because the daemon broadcast one signal).
     *
     * Callbacks run SEQUENTIALLY on the BG thread. If there are 3 pending
     * callbacks, they fire one after another (not in parallel). If one
     * callback is slow, it delays the others. Callbacks should be fast --
     * typically just copy data and signal a condvar.
     */
    for (int i = 0; i < count; i++) {
        Snapshot *s = &snapshots[i];

        FWUPMGR_INFO("dispatch_all_pending: invoking callback for handle='%s'\n",
                     s->handle_copy);

        /*
         * THE ACTUAL CALLBACK INVOCATION.
         *
         * s->callback is the function pointer stored during
         * internal_register_callback(). For our example_app, this is
         * on_firmware_check_callback().
         *
         * &fwinfo_data is a pointer to our stack-allocated struct.
         * The callback receives it as (const FwInfoData *event_data).
         * "const" means the callback cannot modify it, but it can read
         * all fields and copy them.
         *
         * THIS CALL BLOCKS THE BG THREAD until the callback returns.
         * While inside the callback:
         *   - The BG thread is busy (not listening for more signals)
         *   - If another signal arrives, GLib queues it in the GMainContext
         *   - The signal will be dispatched after this function returns
         *     and the BG thread goes back to g_main_loop_run()
         */
        s->callback(&fwinfo_data);

        /*
         * Callback has returned. Now reset the slot to IDLE.
         *
         * We must lock the mutex for this because dispatch_all_pending()
         * could be called concurrently (another signal arrives while
         * we're in Phase 2), or the main thread could be registering
         * a new callback.
         *
         * registry_reset_slot() frees the strdup'd handle_key, sets
         * callback to NULL, and sets state to CB_STATE_IDLE.
         *
         * After this, the slot is available for reuse by the next
         * checkForUpdate() call.
         */
        pthread_mutex_lock(&g_registry.mutex);
        registry_reset_slot(&g_registry.entries[s->slot_index]);
        pthread_mutex_unlock(&g_registry.mutex);
    }
}

/* ========================================================================
 * REGISTRY OPERATIONS
 * ======================================================================== */

/*
 * internal_register_callback - Store a callback in the check-for-update registry.
 *
 * OVERVIEW
 *
 * PURPOSE:
 *   Called by checkForUpdate() to record the caller's callback function
 *   in g_registry so that when the BG thread later receives the
 *   CheckForUpdateComplete D-Bus signal, it can find and invoke it.
 *
 *   Think of it as writing your name and phone number on a waiting list.
 *   When the result arrives, the BG thread walks the list and calls
 *   everyone who signed up.
 *
 * REGISTRY STRUCTURE:
 *   g_registry is a static global CallbackRegistry:
 *     - entries[30] -- array of CallbackEntry structs (MAX_PENDING_CALLBACKS=30)
 *     - mutex       -- pthread_mutex_t protecting the array
 *     - initialized -- bool (set by internal_system_init)
 *
 *   Each CallbackEntry has:
 *     - state           -- IDLE, PENDING, DISPATCHED, or TIMED_OUT
 *     - handle_key      -- strdup'd copy of the handle string (e.g., "1")
 *     - callback        -- function pointer to the caller's callback
 *     - registered_time -- unix timestamp for potential timeout detection
 *
 * SLOT LIFECYCLE:
 *   IDLE     -- slot is empty, available for use
 *   PENDING  -- callback registered, waiting for signal from daemon
 *   DISPATCHED -- signal received, callback is being invoked right now
 *   IDLE     -- callback returned, slot reset and available again
 *
 * DEDUP BEHAVIOR:
 *   If the same handle already has a PENDING entry (the caller called
 *   checkForUpdate() twice before the first callback fired), the old
 *   entry is OVERWRITTEN with the new callback. This prevents "ghost"
 *   callbacks from accumulating. The old callback will never fire.
 *
 * THREAD SAFETY:
 *   Protected by g_registry.mutex. The main thread calls this function
 *   (to register). The BG thread calls dispatch_all_pending() (to read
 *   and dispatch). The mutex ensures they never see inconsistent state.
 *
 * MEMORY:
 *   handle_key is strdup'd here (heap allocation). It is freed either:
 *     a. When the slot is reset to IDLE (registry_reset_slot)
 *     b. When an existing entry is overwritten (dedup path)
 *     c. When internal_system_deinit cleans up all remaining entries
 *
 * @param handle    The client's handle string (e.g., "1"). Will be
 *                  strdup'd -- caller retains ownership of their copy.
 * @param callback  The function to call when the signal arrives.
 *                  Signature: void callback(const FwInfoData *fwinfodata)
 * @return true if registered successfully, false if registry is full (30 slots)
 *
 * Called by: checkForUpdate() in rdkFwupdateMgr_api.c
 * Pairs with: dispatch_all_pending() which reads PENDING entries
 */
bool internal_register_callback(FirmwareInterfaceHandle handle,
                                 UpdateEventCallback callback)
{
    /*
     * Lock the registry mutex before touching the entries array.
     *
     * Who else might be holding this mutex right now?
     *   - dispatch_all_pending() on the BG thread (Phase 1 -- collecting
     *     PENDING entries into a snapshot). But Phase 1 is very fast
     *     (microseconds), so contention is rare.
     *   - registry_reset_slot() on the BG thread (after invoking a
     *     callback, resetting the slot to IDLE). Also very fast.
     *
     * In practice, the main thread and BG thread almost never contend
     * because the BG thread only touches the registry when a signal
     * arrives, which is seconds apart.
     */
    pthread_mutex_lock(&g_registry.mutex);

    /*
     * Two scan targets:
     *   free_slot     -- first IDLE entry (to use if no dedup match)
     *   existing_slot -- a PENDING entry with the same handle (dedup)
     *
     * We scan all 30 entries in one pass, looking for both simultaneously.
     */
    CallbackEntry *free_slot     = NULL;
    CallbackEntry *existing_slot = NULL;

    for (int i = 0; i < MAX_PENDING_CALLBACKS; i++) {
        CallbackEntry *e = &g_registry.entries[i];

        /*
         * Dedup check: is there already a PENDING entry for this handle?
         *
         * This happens when the caller calls checkForUpdate() twice
         * with the same handle before the first callback fires.
         * Without dedup, both entries would BOTH get dispatched when
         * the signal arrives -- the callback would fire twice. The
         * second invocation would be a "ghost" with stale data.
         *
         * By overwriting, only the latest callback survives.
         *
         * strcmp is safe here because handle_key is always a valid
         * null-terminated string (set by strdup) or NULL (checked first).
         */
        if (e->state == CB_STATE_PENDING &&
            e->handle_key != NULL &&
            strcmp(e->handle_key, handle) == 0) {
            existing_slot = e;
            break;
        }

        /*
         * Remember the first free slot we find, but keep scanning
         * in case there's a dedup match later in the array.
         *
         * We only record the FIRST free slot (free_slot == NULL guard)
         * to avoid unnecessary work.
         */
        if (free_slot == NULL && e->state == CB_STATE_IDLE) {
            free_slot = e;
        }
    }

    /*
     * Choose target: prefer dedup (overwrite existing) over new slot.
     *
     * If existing_slot != NULL: overwrite the existing PENDING entry.
     *   This is the dedup path -- same handle called checkForUpdate again.
     *
     * If existing_slot == NULL and free_slot != NULL: use the free slot.
     *   This is the normal first-call path.
     *
     * If both are NULL: registry is full. All 30 slots are occupied
     *   (some combination of PENDING and DISPATCHED). Return false.
     */
    CallbackEntry *target = existing_slot ? existing_slot : free_slot;

    if (target == NULL) {
        FWUPMGR_ERROR("internal_register_callback: registry full (max=%d)\n",
                      MAX_PENDING_CALLBACKS);
        pthread_mutex_unlock(&g_registry.mutex);
        return false;
    }

    /*
     * If overwriting an existing entry, free the old handle_key.
     *
     * The old strdup'd string must be freed to avoid a memory leak.
     * The old callback function pointer is just overwritten -- function
     * pointers don't need freeing, they point to code, not heap data.
     */
    if (existing_slot) {
        FWUPMGR_INFO("internal_register_callback: overwriting existing for handle='%s'\n",
                     handle);
        free(target->handle_key);
        target->handle_key = NULL;
    }

    /*
     * Populate the slot:
     *
     * handle_key = strdup(handle):
     *   Creates a heap copy of the handle string "1". We need our own
     *   copy because the caller's 'handle' pointer belongs to them --
     *   they could theoretically modify or free it later. strdup
     *   allocates strlen(handle)+1 bytes (2 bytes for "1\0").
     *   This copy is freed in registry_reset_slot() when the slot
     *   returns to IDLE.
     *
     * callback = callback:
     *   Stores the function pointer. When dispatch_all_pending() runs,
     *   it will call this: callback(&fwinfo_data).
     *   Function pointers are just addresses -- no heap allocation.
     *
     * state = CB_STATE_PENDING:
     *   Marks this slot as "waiting for a signal." The BG thread's
     *   dispatch_all_pending() only looks at PENDING entries.
     *   IDLE entries are skipped, DISPATCHED entries are being processed.
     *
     * registered_time = time(NULL):
     *   Unix timestamp of when this callback was registered. Currently
     *   used only for logging/debugging, but could be used by a future
     *   timeout sweeper to detect stale entries that have been PENDING
     *   for too long (e.g., > CALLBACK_TIMEOUT_SECONDS = 60s).
     */
    target->handle_key      = strdup(handle);
    target->callback        = callback;
    target->state           = CB_STATE_PENDING;
    target->registered_time = time(NULL);

    pthread_mutex_unlock(&g_registry.mutex);

    FWUPMGR_INFO("internal_register_callback: registered handle='%s'\n", handle);
    return true;
}

/*
 * registry_reset_slot - Return a CallbackEntry to the IDLE state.
 *
 * PURPOSE:
 *   After a callback has been dispatched (invoked and returned), the
 *   registry slot must be cleaned up and made available for reuse.
 *   This function frees the strdup'd handle_key, clears the callback
 *   pointer, resets the timestamp, and sets state to IDLE.
 *
 * PRECONDITION:
 *   Caller MUST hold g_registry.mutex before calling this function.
 *   dispatch_all_pending() does this: lock -> reset -> unlock.
 *   internal_system_deinit() also calls this during cleanup.
 *
 *   Why must the mutex be held?
 *   Without the mutex, a race could occur:
 *     - BG thread is resetting slot 0 (setting state to IDLE)
 *     - Main thread scans for free slots (sees IDLE in half-written state)
 *     - Main thread writes into slot 0 while BG thread is still clearing it
 *   The mutex ensures atomicity of the reset operation.
 *
 * MEMORY:
 *   handle_key was allocated by strdup() in internal_register_callback().
 *   We free() it here. After this call, entry->handle_key is NULL.
 *   The callback function pointer is just zeroed (it points to code
 *   segment, not heap -- no need to free).
 *
 * STATE TRANSITION:
 *   DISPATCHED -> IDLE (normal flow after callback invocation)
 *   PENDING    -> IDLE (during system deinit cleanup)
 *   Any state  -> IDLE (this function doesn't check current state)
 *
 * @param entry  Pointer to the CallbackEntry to reset.
 *               Must not be NULL.
 */
static void registry_reset_slot(CallbackEntry *entry)
{
    /*
     * Free the strdup'd handle_key string (e.g., "1").
     * NULL check is defensive -- PENDING and DISPATCHED entries always
     * have a non-NULL handle_key, but IDLE entries have NULL.
     * free(NULL) is safe in C (no-op), but the explicit check avoids
     * confusion and makes the intent clear.
     */
    if (entry->handle_key != NULL) {
        free(entry->handle_key);
        entry->handle_key = NULL;
    }

    /*
     * Clear remaining fields. After this, the entry looks identical
     * to a freshly memset'd entry from internal_system_init().
     */
    entry->callback        = NULL;
    entry->registered_time = 0;
    entry->state           = CB_STATE_IDLE;
}

/* ========================================================================
 * SIGNAL DATA HELPERS
 * ======================================================================== */

/*
 * internal_parse_signal_data - Extract fields from CheckForUpdateComplete signal.
 *
 * PURPOSE:
 *   The daemon broadcasts a D-Bus signal with a GVariant of type "(tiissss)".
 *   This function unpacks that GVariant into an InternalSignalData struct
 *   with individual typed fields, making the data easy to work with.
 *
 * GVariant TYPE "(tiissss)" -- what each letter means:
 *   '(' and ')' = tuple delimiters (the whole thing is a tuple)
 *   't'         = uint64 (guint64) -- handler_id
 *   'i'         = int32 (gint32)   -- result_code (0=success, 1=fail)
 *   'i'         = int32 (gint32)   -- status_code (0=available, 1=not, 3=error)
 *   's'         = string (gchar*)  -- current_version (e.g., "RDKV_7.0")
 *   's'         = string (gchar*)  -- available_version (e.g., "RDKV_8.0")
 *   's'         = string (gchar*)  -- update_details (pipe-separated "Key:Value|...")
 *   's'         = string (gchar*)  -- status_message (human-readable text)
 *
 * WHY strdup() EACH STRING?
 *   g_variant_get() with 's' type returns pointers into the GVariant's
 *   internal buffer. Those pointers are only valid while the GVariant
 *   exists. After on_check_complete_signal() returns, GLib may free
 *   the GVariant. We strdup() to create our own heap copies that survive
 *   beyond the GVariant's lifetime.
 *
 *   The strdup'd copies are freed later by internal_cleanup_signal_data().
 *
 * VALIDATION:
 *   We check the GVariant type signature before extracting. If the daemon
 *   sends a signal with a different signature (protocol mismatch, daemon
 *   version skew), we reject it immediately rather than crashing on
 *   mismatched g_variant_get().
 *
 * THREAD: Called on the BG thread (from on_check_complete_signal).
 *
 * @param parameters  The GVariant payload from the D-Bus signal.
 *                    Type must be "(tiissss)".
 * @param out_data    Output struct. Must be zero-initialized by caller.
 *                    On success, contains result_code, status_code, and
 *                    4 strdup'd strings (any may be NULL if daemon sent NULL).
 * @return true on success, false if parameters is NULL or wrong type.
 */
bool internal_parse_signal_data(GVariant *parameters, InternalSignalData *out_data)
{
    if (parameters == NULL || out_data == NULL) return false;

    /*
     * Check the type signature before extracting.
     *
     * g_variant_get_type_string() returns the GVariant's type as a string.
     * We expect "(tiissss)". If it's anything else, the daemon sent
     * something unexpected -- possibly a newer/older protocol version.
     * Extracting with the wrong format string would read garbage.
     */
    const gchar *sig = g_variant_get_type_string(parameters);
    if (strcmp(sig, "(tiissss)") != 0) {
        FWUPMGR_ERROR("internal_parse_signal_data: unexpected signature '%s'\n", sig);
        return false;
    }

    /*
     * Local variables to receive g_variant_get() output.
     *
     * For string types ('s'), g_variant_get() returns pointers into
     * the GVariant's internal buffer. These are temporary -- we must
     * strdup() them before the GVariant could be freed.
     *
     * For integer types ('t', 'i'), g_variant_get() copies the value
     * directly into our local variables.
     */
    const gchar *cur = NULL, *avail = NULL, *details = NULL, *msg = NULL;
    guint64 handler_id = 0;
    gint32 result = 0, status = 0;

    /*
     * Extract all 7 fields from the GVariant tuple in one call.
     *
     * The format string "(tiissss)" must exactly match the GVariant type.
     * Each format character corresponds to one pointer argument:
     *   &handler_id -- receives the uint64
     *   &result     -- receives the first int32
     *   &status     -- receives the second int32
     *   &cur        -- receives pointer to current_version string
     *   &avail      -- receives pointer to available_version string
     *   &details    -- receives pointer to update_details string
     *   &msg        -- receives pointer to status_message string
     */
    g_variant_get(parameters, "(tiissss)",
                  &handler_id, &result, &status, &cur, &avail, &details, &msg);

    /*
     * Copy extracted values into the output struct.
     *
     * Integers are copied directly (they're values, not pointers).
     *
     * Strings are strdup'd to create heap copies that we own.
     * The ternary (cur ? strdup(cur) : NULL) handles the case where
     * the daemon sent an empty string (glib may return "" not NULL)
     * or truly NULL. strdup(NULL) is undefined behavior in C, so
     * the NULL check is essential.
     *
     * Note: handler_id is extracted but not stored in InternalSignalData.
     * We don't currently use it because the dispatch is broadcast to
     * ALL pending callbacks, not filtered by handler_id. If we later
     * need per-client filtering, we would add handler_id to the struct.
     */
    out_data->result_code       = (int32_t)result;
    out_data->status_code       = (int32_t)status;
    out_data->current_version   = cur     ? strdup(cur)     : NULL;
    out_data->available_version = avail   ? strdup(avail)   : NULL;
    out_data->update_details    = details ? strdup(details) : NULL;
    out_data->status_message    = msg     ? strdup(msg)     : NULL;

    return true;
}

/*
 * internal_cleanup_signal_data - Free the strdup'd strings in InternalSignalData.
 *
 * PURPOSE:
 *   Called after dispatch_all_pending() has finished invoking all callbacks.
 *   Frees the 4 heap-allocated strings that internal_parse_signal_data()
 *   created via strdup(). Also zeroes the struct as a defensive measure.
 *
 * WHY memset AFTER free()?
 *   After freeing the pointers, the struct still contains the old pointer
 *   values (dangling pointers). If someone accidentally reads the struct
 *   after cleanup, they'd get use-after-free. memset to zero sets all
 *   pointers to NULL (safe to dereference for a NULL check) and all
 *   integers to 0.
 *
 * free(NULL) is safe in C -- it's a no-op. So if any string was NULL
 * (daemon didn't send it), the free() call is harmless.
 *
 * @param data  The InternalSignalData to clean up. Must not be NULL.
 */
void internal_cleanup_signal_data(InternalSignalData *data)
{
    free(data->current_version);
    free(data->available_version);
    free(data->update_details);
    free(data->status_message);
    memset(data, 0, sizeof(InternalSignalData));
}

/*
 * internal_map_status_code - Convert daemon's integer to our public enum.
 *
 * PURPOSE:
 *   The daemon sends status_code as a plain integer in the D-Bus signal.
 *   Our public API uses a typed enum (CheckForUpdateStatus). This function
 *   does the mapping.
 *
 * MAPPING:
 *   0 -> FIRMWARE_AVAILABLE      (new firmware exists, UpdateDetails populated)
 *   1 -> FIRMWARE_NOT_AVAILABLE  (device is on latest version)
 *   2 -> UPDATE_NOT_ALLOWED      (device policy prevents updates)
 *   3 -> FIRMWARE_CHECK_ERROR    (XConf query failed, network error, etc.)
 *   4 -> IGNORE_OPTOUT           (update available, ignore opt-out preference)
 *   5 -> BYPASS_OPTOUT           (update available, bypass opt-out preference)
 *   anything else -> FIRMWARE_CHECK_ERROR  (unknown code = error)
 *
 * WHY DEFAULT TO FIRMWARE_CHECK_ERROR?
 *   Unknown status codes indicate a protocol mismatch (daemon version
 *   newer than library). Treating unknown as "error" is the safest
 *   default -- the caller will handle it as a failure case rather than
 *   proceeding with potentially incorrect firmware data.
 *
 * @param status_code  Integer from the daemon's signal payload.
 * @return Corresponding CheckForUpdateStatus enum value.
 */
CheckForUpdateStatus internal_map_status_code(int32_t status_code)
{
    switch (status_code) {
        case 0:  return FIRMWARE_AVAILABLE;
        case 1:  return FIRMWARE_NOT_AVAILABLE;
        case 2:  return UPDATE_NOT_ALLOWED;
        case 3:  return FIRMWARE_CHECK_ERROR;
        case 4:  return IGNORE_OPTOUT;
        case 5:  return BYPASS_OPTOUT;
        default:
            FWUPMGR_ERROR("internal_map_status_code: unknown %d -> FIRMWARE_CHECK_ERROR\n",
                          status_code);
            return FIRMWARE_CHECK_ERROR;
    }
}


/* ========================================================================
 * DOWNLOAD FIRMWARE — INTERNAL ENGINE
 * ========================================================================
 *
 * Everything below is the DownloadFirmware equivalent of the
 * CheckForUpdate engine above. Same patterns, different registry and signal.
 *
 * KEY DIFFERENCE:
 *   CheckForUpdate slot fires ONCE then goes IDLE.
 *   Download slot stays ACTIVE and fires on EVERY DownloadProgress signal
 *   until the daemon sends DWNL_COMPLETED or DWNL_ERROR.
 * ======================================================================== */

/* ---- Forward declarations for helper functions ---- */
static void dispatch_all_dwnl_active(const InternalDwnlSignalData *signal_data);
static void dwnl_registry_reset_slot(DwnlCallbackEntry *entry);

/* ========================================================================
 * DOWNLOAD REGISTRY CLEANUP
 * ======================================================================== */

/*
 * internal_dwnl_system_deinit - Free all download registry resources.
 *
 * PURPOSE:
 *   Called from internal_system_deinit() during library unload (either
 *   via __attribute__((destructor)) or explicitly by unregisterProcess).
 *   Frees any strdup'd handle_key strings that are still in the registry
 *   (e.g., downloads that were in-progress when the app exits) and
 *   destroys the mutex.
 *
 * WHEN IS THIS CALLED?
 *   During orderly shutdown of the library. The BG thread has already
 *   been joined (stopped), so no concurrent access to g_dwnl_registry
 *   is possible. The mutex lock/unlock is purely defensive -- in theory
 *   no other thread can be using the registry at this point.
 *
 * WHY FREE handle_key's?
 *   If the app exits while a download is ACTIVE (e.g., download at 50%
 *   and app receives SIGTERM), the slot still holds a strdup'd handle_key
 *   that was never freed by dwnl_registry_reset_slot() (because the
 *   terminal COMPLETED/ERROR signal never arrived). We must free it here
 *   to avoid a memory leak reported by Valgrind/ASan.
 *
 * WHY pthread_mutex_destroy()?
 *   The mutex was initialized by pthread_mutex_init() in internal_system_init().
 *   Every init must have a matching destroy for clean resource management.
 *   Destroying a locked mutex is undefined behavior (we unlock first).
 *
 * NOTE: Signal unsubscription (g_dbus_connection_signal_unsubscribe) is
 *   handled separately by the BG thread during its shutdown sequence,
 *   NOT here. This function only handles registry memory.
 *
 * @param none (operates on global g_dwnl_registry)
 */
static void internal_dwnl_system_deinit(void)
{
    /*
     * Lock before freeing. Defensive -- no other thread should be
     * active at this point, but the pattern is consistent with how
     * all other registry operations lock before accessing entries[].
     */
    pthread_mutex_lock(&g_dwnl_registry.mutex);

    /*
     * Scan all 30 slots. Free any non-NULL handle_key strings.
     * IDLE slots have handle_key == NULL (already freed or never set).
     * ACTIVE slots (abandoned downloads) have handle_key != NULL.
     */
    for (int i = 0; i < MAX_PENDING_CALLBACKS; i++) {
        if (g_dwnl_registry.entries[i].handle_key != NULL) {
            free(g_dwnl_registry.entries[i].handle_key);
            g_dwnl_registry.entries[i].handle_key = NULL;
        }
    }
    pthread_mutex_unlock(&g_dwnl_registry.mutex);

    /*
     * Destroy the mutex. After this, any attempt to lock it is UB.
     * Since the library is unloading, no one should try.
     */
    pthread_mutex_destroy(&g_dwnl_registry.mutex);

    FWUPMGR_INFO("internal_dwnl_system_deinit: done\n");
}

/* ========================================================================
 * DOWNLOAD SIGNAL HANDLER
 * ======================================================================== */

/*
 * on_download_progress_signal - BG thread entry point for DownloadProgress.
 *
 * PURPOSE:
 *   This function is called by GLib's D-Bus infrastructure when the daemon
 *   broadcasts the "DownloadProgress" signal. It is the ENTRY POINT for
 *   the "response" side of the downloadFirmware() async flow.
 *
 * WHEN DOES THIS FIRE?
 *   Repeatedly, starting shortly after downloadFirmware() was called.
 *   The daemon emits a DownloadProgress signal each time it has a
 *   progress update (0%, 10%, 50%, 100%, or on error). Unlike
 *   checkForUpdate which fires ONCE, this fires MANY TIMES.
 *
 * WHICH THREAD RUNS THIS?
 *   The BACKGROUND THREAD. Same thread that handles CheckForUpdateComplete
 *   and UpdateProgress. All three signals are dispatched on the same
 *   single BG thread. Only one signal handler runs at a time because
 *   they all share the same GMainContext.
 *
 * WHY (void) CASTS?
 *   GLib's signal handler signature requires 7 parameters. We only need
 *   'parameters' (the GVariant payload). The (void) casts suppress
 *   "unused parameter" compiler warnings for the other 6.
 *
 * SIGNAL PAYLOAD FORMAT:
 *   GVariant type "(tsuss)":
 *     t  handler_id       (uint64 -- which registered client)
 *     s  firmware_name    (string -- filename being downloaded)
 *     u  progress_percent (uint32 -- 0 to 100)
 *     s  status_string    (string -- "NOTSTARTED"/"INPROGRESS"/"COMPLETED"/"ERROR")
 *     s  message          (string -- human-readable status message)
 *
 *   IMPORTANT DIFFERENCE FROM checkForUpdate:
 *     checkForUpdate used integer status codes mapped by internal_map_status_code().
 *     download uses STRING status values mapped by map_dwnl_status_string().
 *     The daemon sends "INPROGRESS" not 0, "COMPLETED" not 1.
 *
 * MEMORY MANAGEMENT (DIFFERENT FROM checkForUpdate):
 *   checkForUpdate: internal_parse_signal_data() uses strdup() -> free()
 *   download: internal_parse_dwnl_signal_data() uses GLib's g_variant_get()
 *   with 's' format -> returns gchar* that caller must g_free().
 *
 *   The strings (firmware_name, status_string, message) are allocated by
 *   GLib during g_variant_get(). They stay valid until we g_free() them
 *   AFTER dispatch is complete. This ensures the strings are valid
 *   throughout all callback invocations.
 *
 * FLOW:
 *   1. Parse GVariant -> InternalDwnlSignalData (extracts 5 fields)
 *   2. Call dispatch_all_dwnl_active() -- fires all ACTIVE callbacks
 *   3. g_free() the 3 GLib-allocated strings
 *
 * @param conn           The BG thread's persistent D-Bus connection (:1.141)
 * @param sender         D-Bus sender address (ignored -- accept from any)
 * @param object_path    D-Bus object path of the signal source (ignored)
 * @param interface_name D-Bus interface the signal belongs to (ignored)
 * @param signal_name    "DownloadProgress" (ignored -- we know from subscription)
 * @param parameters     The GVariant payload -- type "(tsuss)"
 * @param user_data      NULL (we use globals, no per-subscription user data)
 */
static void on_download_progress_signal(GDBusConnection *conn,
                                        const gchar *sender,
                                        const gchar *object_path,
                                        const gchar *interface_name,
                                        const gchar *signal_name,
                                        GVariant *parameters,
                                        gpointer user_data)
{
    /* Suppress "unused parameter" warnings for the 6 params we don't need. */
    (void)conn; (void)sender; (void)object_path;
    (void)interface_name; (void)signal_name; (void)user_data;

    FWUPMGR_INFO("on_download_progress_signal: received\n");

    /*
     * Stack-allocate and zero-init the parsed data struct.
     * After parsing, this holds:
     *   handler_id       -- uint64 (which client the signal is for)
     *   firmware_name    -- gchar* (GLib-allocated, must g_free)
     *   progress_percent -- uint32 (0-100)
     *   status_string    -- gchar* (GLib-allocated, must g_free)
     *   message          -- gchar* (GLib-allocated, must g_free)
     */
    InternalDwnlSignalData signal_data;
    memset(&signal_data, 0, sizeof(signal_data));

    /*
     * Parse the GVariant. If parsing fails (wrong type signature,
     * NULL parameters), log and return. No callbacks fire.
     */
    if (!internal_parse_dwnl_signal_data(parameters, &signal_data)) {
        FWUPMGR_ERROR("on_download_progress_signal: parse failed\n");
        return;
    }

    FWUPMGR_INFO("on_download_progress_signal: handler=%" PRIu64 " firmware='%s' progress=%u%% status='%s'\n",
                 signal_data.handler_id,
                 signal_data.firmware_name ? signal_data.firmware_name : "(null)",
                 signal_data.progress_percent,
                 signal_data.status_string ? signal_data.status_string : "(null)");

    /*
     * Dispatch to all ACTIVE download callbacks.
     * This is the two-phase dispatch (snapshot under mutex, invoke without).
     * The callback may fire for progress (slot stays ACTIVE) or for
     * terminal status (slot reset to IDLE).
     */
    dispatch_all_dwnl_active(&signal_data);

    /*
     * Free the GLib-allocated strings.
     *
     * g_free() is GLib's equivalent of free(). We use g_free() because
     * the strings were allocated by GLib's g_variant_get() internally.
     * Using standard free() on GLib-allocated memory is technically
     * undefined behavior (though it works on most platforms).
     *
     * These strings were valid throughout dispatch_all_dwnl_active()
     * because we only free them AFTER all callbacks have returned.
     * The callbacks receive progress_percent (int, copied by value)
     * and status (enum, copied by value), so they don't reference
     * these strings directly. But the dispatch function does read
     * status_string to determine is_final, so it must be valid then.
     *
     * g_free(NULL) is safe -- it's a no-op.
     */
    g_free(signal_data.firmware_name);
    g_free(signal_data.status_string);
    g_free(signal_data.message);
}

/*
 * dispatch_all_dwnl_active - Two-phase dispatch for download progress.
 *
 * PURPOSE:
 *   Called by on_download_progress_signal() on the BG thread.
 *   Finds ALL ACTIVE entries in g_dwnl_registry and invokes each one's
 *   callback with the progress and status. If the download ended
 *   (COMPLETED or ERROR), resets the slot to IDLE after the callback.
 *
 * TWO-PHASE DESIGN (SAME PATTERN AS checkForUpdate's dispatch_all_pending):
 *
 *   PHASE 1 -- SNAPSHOT (mutex held, ~microseconds):
 *     Lock g_dwnl_registry.mutex.
 *     Scan all 30 slots. For each ACTIVE entry:
 *       - Copy callback function pointer into stack-local snapshot
 *       - Copy handle string (snprintf into fixed buffer)
 *       - Record slot index (for IDLE reset later)
 *       - Record whether this is a terminal signal (is_final)
 *     Unlock mutex.
 *
 *   PHASE 2 -- INVOKE (no mutex held, may take milliseconds):
 *     For each snapshot entry:
 *       - Call: callback(progress_percent, status)
 *       - If is_final: re-lock mutex, reset slot to IDLE, unlock
 *
 *   WHY TWO PHASES?
 *     Same deadlock prevention as checkForUpdate. If we held the mutex
 *     while calling the app's callback, and the callback tried to call
 *     downloadFirmware() or unregisterProcess(), that would try to lock
 *     the same mutex -> DEADLOCK. By releasing before invoking, the
 *     callback can safely call any library API.
 *
 * KEY DIFFERENCE FROM checkForUpdate's dispatch_all_pending():
 *
 *   checkForUpdate: slot goes PENDING -> DISPATCHED -> IDLE after ONE callback.
 *   download: slot stays ACTIVE across MANY callbacks. Only goes IDLE when
 *     the status is DWNL_COMPLETED or DWNL_ERROR (is_final == true).
 *
 *   For in-progress signals (is_final == false):
 *     - Phase 1: snapshot the ACTIVE slot, do NOT touch slot state.
 *     - Phase 2: invoke callback, do NOT reset slot.
 *     - Result: slot remains ACTIVE for the next DownloadProgress signal.
 *
 *   For terminal signals (is_final == true):
 *     - Phase 1: same snapshot.
 *     - Phase 2: invoke callback, THEN lock mutex and reset slot to IDLE.
 *     - Result: slot is freed. No more callbacks will fire for this handle.
 *
 * STATUS MAPPING:
 *   The daemon sends status as a STRING ("INPROGRESS", "COMPLETED", etc.).
 *   map_dwnl_status_string() converts it to the DownloadStatus enum:
 *     "NOTSTARTED" or "INPROGRESS" -> DWNL_IN_PROGRESS
 *     "COMPLETED"                  -> DWNL_COMPLETED
 *     "ERROR" or "DWNL_ERROR"     -> DWNL_ERROR
 *
 *   is_final is true only for DWNL_COMPLETED or DWNL_ERROR.
 *   All other statuses (IN_PROGRESS) keep the slot alive.
 *
 * CALLBACK SIGNATURE:
 *   void callback(int progress_per, DownloadStatus status)
 *   - progress_per: 0-100 integer (percentage of download complete)
 *   - status: DWNL_IN_PROGRESS, DWNL_COMPLETED, or DWNL_ERROR
 *   - No handle parameter (different from checkForUpdate's callback)
 *   - Runs on BG thread, NOT the app's main thread
 *
 * THREAD: Runs entirely on the BG thread.
 *
 * @param signal_data  Parsed DownloadProgress signal payload.
 *                     Must remain valid for the duration of this function
 *                     (strings freed by caller AFTER this returns).
 */
static void dispatch_all_dwnl_active(const InternalDwnlSignalData *signal_data)
{
    /*
     * Stack-local snapshot struct for one ACTIVE entry.
     * Same pattern as checkForUpdate's dispatch -- we copy what we need
     * while the mutex is held, then work from the copy with no mutex.
     *
     * Fields:
     *   callback    -- the app's function pointer (copied from slot)
     *   handle_copy -- snprintf'd copy of handle_key (for logging only)
     *   slot_index  -- which slot in entries[] this came from
     *   is_final    -- true if COMPLETED/ERROR (need to reset slot after)
     */
    typedef struct {
        DownloadCallback  callback;
        char              handle_copy[256];
        int               slot_index;
        bool              is_final;   /* true if COMPLETED or ERROR -- remove after firing */
    } DwnlSnapshot;

    DwnlSnapshot snapshots[MAX_PENDING_CALLBACKS];
    int          count = 0;

    /*
     * Map the status string to our enum BEFORE entering the mutex.
     * This is a pure computation (strcmp calls) with no shared state.
     * Doing it outside the mutex keeps the critical section shorter.
     *
     * is_final determines whether we reset the slot after the callback:
     *   true  = COMPLETED or ERROR -> download ended, free the slot
     *   false = IN_PROGRESS -> download continuing, keep slot ACTIVE
     */
    DownloadStatus status = map_dwnl_status_string(signal_data->status_string);
    bool           is_final = (status == DWNL_COMPLETED || status == DWNL_ERROR);

    /* ---- PHASE 1: snapshot under mutex ---- */

    /*
     * Lock the download registry. This blocks any concurrent call to
     * internal_dwnl_register_callback() (from main thread) until we
     * finish our snapshot. Hold time: microseconds (just scanning + copying).
     */
    pthread_mutex_lock(&g_dwnl_registry.mutex);

    /*
     * Scan all 30 slots. Only process entries in ACTIVE state.
     * IDLE and TIMED_OUT entries are skipped.
     */
    for (int i = 0; i < MAX_PENDING_CALLBACKS; i++) {
        DwnlCallbackEntry *e = &g_dwnl_registry.entries[i];
        if (e->state != DWNL_CB_STATE_ACTIVE) continue;

        /*
         * Copy the entry data into our stack-local snapshot.
         * After we unlock, the slot might be modified by another thread
         * (extremely unlikely, but the pattern is safe regardless).
         */
        snapshots[count].callback   = e->callback;
        snapshots[count].slot_index = i;
        snapshots[count].is_final   = is_final;
        snprintf(snapshots[count].handle_copy,
                 sizeof(snapshots[count].handle_copy),
                 "%s", e->handle_key ? e->handle_key : "");

        /*
         * NOTE: We do NOT change the slot state here.
         * For in-progress signals, the slot must remain ACTIVE.
         * For terminal signals, we'll reset it in Phase 2 AFTER
         * the callback fires. This is different from checkForUpdate
         * which sets state=DISPATCHED during Phase 1.
         */
        count++;

        FWUPMGR_INFO("dispatch_all_dwnl_active: queued handle='%s' progress=%d%% final=%d\n",
                     e->handle_key ? e->handle_key : "(null)",
                     signal_data->progress_percent, is_final);
    }

    /*
     * Unlock BEFORE invoking callbacks.
     * This is the deadlock prevention: callbacks may call library APIs
     * that need g_dwnl_registry.mutex, so we must not hold it.
     */
    pthread_mutex_unlock(&g_dwnl_registry.mutex);

    FWUPMGR_INFO("dispatch_all_dwnl_active: %d callback(s) to fire\n", count);

    /* ---- PHASE 2: invoke callbacks, no mutex held ---- */

    for (int i = 0; i < count; i++) {
        DwnlSnapshot *s = &snapshots[i];

        FWUPMGR_INFO("dispatch_all_dwnl_active: invoking callback for handle='%s'\n",
                     s->handle_copy);

        /*
         * Invoke the app's download callback.
         *
         * Callback signature: void fn(int progress_per, DownloadStatus status)
         *   progress_per = signal_data->progress_percent (0-100)
         *   status = mapped enum (DWNL_IN_PROGRESS, DWNL_COMPLETED, DWNL_ERROR)
         *
         * The app's callback (e.g., on_download_progress_callback in example_app)
         * typically:
         *   - Logs the progress
         *   - On terminal status: locks its own condvar mutex, sets done=1,
         *     signals the condvar to wake the main thread
         *   - Returns
         *
         * This call may take microseconds to milliseconds depending on
         * what the app does in the callback. We hold NO library mutex
         * during this time.
         */
        s->callback(signal_data->progress_percent, status);

        /*
         * If this was a terminal signal (download ended), reset the slot.
         *
         * We re-acquire the mutex, call dwnl_registry_reset_slot(), then
         * release. This frees the strdup'd handle_key and sets state=IDLE.
         *
         * After this, no more callbacks will fire for this handle.
         * The slot is available for reuse by a future downloadFirmware() call.
         *
         * For in-progress signals, we skip this entirely. The slot stays
         * ACTIVE and will be found again on the NEXT DownloadProgress signal.
         */
        if (s->is_final) {
            pthread_mutex_lock(&g_dwnl_registry.mutex);
            dwnl_registry_reset_slot(&g_dwnl_registry.entries[s->slot_index]);
            pthread_mutex_unlock(&g_dwnl_registry.mutex);

            FWUPMGR_INFO("dispatch_all_dwnl_active: slot %d reset to IDLE (download ended)\n",
                         s->slot_index);
        }
    }
}

/* ========================================================================
 * DOWNLOAD REGISTRY OPERATIONS
 * ======================================================================== */

/*
 * internal_dwnl_register_callback - Allocate a download registry slot.
 *
 * PURPOSE:
 *   Called by downloadFirmware() (on the main thread) to register the
 *   app's DownloadCallback in g_dwnl_registry. After this call, the
 *   BG thread will invoke the callback on EVERY DownloadProgress signal
 *   until the download completes or errors.
 *
 * REGISTRY DETAILS (g_dwnl_registry -- SEPARATE from g_registry):
 *   - 30 slots (DwnlCallbackEntry entries[MAX_PENDING_CALLBACKS])
 *   - Protected by g_dwnl_registry.mutex (its own mutex, independent)
 *   - State machine: IDLE -> ACTIVE -> IDLE
 *   - No PENDING or DISPATCHED states (unlike checkForUpdate)
 *   - Slot stays ACTIVE across MULTIPLE DownloadProgress signals
 *
 * SLOT LIFECYCLE:
 *   IDLE:   Slot is empty. handle_key==NULL, callback==NULL, state==0.
 *   ACTIVE: Slot is registered. Callback fires on every DownloadProgress.
 *           Stays ACTIVE until DWNL_COMPLETED or DWNL_ERROR arrives.
 *   IDLE:   Reset by dwnl_registry_reset_slot() after terminal signal.
 *
 * DEDUP / OVERWRITE:
 *   If the same handle already has an ACTIVE entry (e.g., the app called
 *   downloadFirmware() twice without waiting for the first to complete),
 *   we OVERWRITE the existing slot. This prevents:
 *     - Two ACTIVE entries for the same handle (double-firing callbacks)
 *     - Ghost entries from a previous download that was abandoned
 *   We free the old handle_key before replacing it.
 *
 * SCAN ORDER:
 *   Linear scan from slot 0 to 29. We look for two things simultaneously:
 *     1. existing_slot: an ACTIVE entry with the same handle (overwrite it)
 *     2. free_slot: the first IDLE entry (use it if no existing found)
 *   If existing_slot is found, we break immediately (priority: overwrite).
 *   If neither is found after scanning all 30, registry is full -> fail.
 *
 * THREAD SAFETY:
 *   Called on the MAIN thread (from downloadFirmware).
 *   g_dwnl_registry.mutex protects against concurrent access from:
 *     - Another main-thread downloadFirmware() call (unlikely, but safe)
 *     - The BG thread's dispatch_all_dwnl_active() reading the registry
 *
 * MEMORY:
 *   handle is strdup'd (heap copy). Freed by dwnl_registry_reset_slot()
 *   when the slot is released (after COMPLETED/ERROR).
 *
 * @param handle    The handle string (e.g., "1"). Will be strdup'd.
 * @param callback  The app's DownloadCallback function pointer.
 * @return true if registered successfully, false if registry full.
 */
bool internal_dwnl_register_callback(FirmwareInterfaceHandle handle,
                                      DownloadCallback callback)
{
    /*
     * Lock the download registry mutex.
     * This ensures only one thread modifies g_dwnl_registry at a time.
     * The BG thread also locks this during Phase 1 of dispatch (snapshot)
     * and during is_final slot reset.
     */
    pthread_mutex_lock(&g_dwnl_registry.mutex);

    DwnlCallbackEntry *free_slot     = NULL;
    DwnlCallbackEntry *existing_slot = NULL;

    /*
     * Single-pass scan of all 30 slots.
     *
     * Priority 1: Find an ACTIVE entry with the same handle.
     *   -> We'll overwrite it (dedup).
     *   -> Break immediately when found (no need to continue).
     *
     * Priority 2: Remember the first IDLE slot encountered.
     *   -> We'll use it if no existing slot is found.
     *   -> Don't break -- keep scanning for existing_slot.
     */
    for (int i = 0; i < MAX_PENDING_CALLBACKS; i++) {
        DwnlCallbackEntry *e = &g_dwnl_registry.entries[i];

        /*
         * Check for existing ACTIVE entry with same handle.
         * Three conditions must all be true:
         *   1. Slot is ACTIVE (not IDLE or TIMED_OUT)
         *   2. handle_key is not NULL (defensive -- should always be set for ACTIVE)
         *   3. handle_key matches our handle (string comparison)
         */
        if (e->state == DWNL_CB_STATE_ACTIVE &&
            e->handle_key != NULL &&
            strcmp(e->handle_key, handle) == 0) {
            existing_slot = e;
            break;
        }

        /*
         * Remember first free slot (only if we haven't found one yet).
         * We DON'T break here -- we keep scanning because an existing
         * slot at a higher index takes priority over a free slot at
         * a lower index.
         */
        if (free_slot == NULL && e->state == DWNL_CB_STATE_IDLE) {
            free_slot = e;
        }
    }

    /*
     * Decide which slot to use.
     * existing_slot (overwrite) takes priority over free_slot (new).
     */
    DwnlCallbackEntry *target = existing_slot ? existing_slot : free_slot;

    /*
     * If neither was found, the registry is full. All 30 slots are ACTIVE
     * (30 concurrent downloads -- should never happen in practice).
     * Unlock and return false.
     */
    if (target == NULL) {
        FWUPMGR_ERROR("internal_dwnl_register_callback: registry full (max=%d)\n",
                      MAX_PENDING_CALLBACKS);
        pthread_mutex_unlock(&g_dwnl_registry.mutex);
        return false;
    }

    /*
     * If overwriting an existing slot, free the old handle_key first.
     * strdup'd memory must be freed to avoid a memory leak.
     */
    if (existing_slot) {
        FWUPMGR_INFO("internal_dwnl_register_callback: overwriting existing for handle='%s'\n",
                     handle);
        free(target->handle_key);
        target->handle_key = NULL;
    }

    /*
     * Populate the slot with the new registration.
     *
     * strdup(handle): Creates a heap copy of the handle string.
     *   We need our own copy because the caller's handle may be freed
     *   or overwritten after downloadFirmware() returns. The slot
     *   must retain the handle for the entire download duration.
     *
     * callback: Store the function pointer directly. No copy needed --
     *   function pointers are just addresses in the code segment.
     *
     * state = DWNL_CB_STATE_ACTIVE: The slot is now live. The BG thread
     *   will find it during its next dispatch_all_dwnl_active() call.
     *   NOTE: ACTIVE, not PENDING. There's no intermediate state.
     *
     * registered_time: Stored for potential timeout detection (not
     *   currently active, but the infrastructure is there for future use).
     */
    target->handle_key      = strdup(handle);
    target->callback        = callback;
    target->state           = DWNL_CB_STATE_ACTIVE;
    target->registered_time = time(NULL);

    pthread_mutex_unlock(&g_dwnl_registry.mutex);

    FWUPMGR_INFO("internal_dwnl_register_callback: registered handle='%s'\n", handle);
    return true;
}

/*
 * dwnl_registry_reset_slot - Clear a download registry slot back to IDLE.
 *
 * PURPOSE:
 *   Called after a terminal DownloadProgress signal (COMPLETED or ERROR)
 *   has been dispatched. Frees the strdup'd handle_key and resets all
 *   fields to zero/NULL/IDLE so the slot can be reused by a future
 *   downloadFirmware() call.
 *
 * PRECONDITION:
 *   MUST be called with g_dwnl_registry.mutex held by the caller.
 *   The caller (dispatch_all_dwnl_active Phase 2) acquires the mutex
 *   before calling this and releases it after.
 *
 * WHAT GETS FREED:
 *   - handle_key: strdup'd in internal_dwnl_register_callback().
 *     Must be freed to avoid memory leak. Set to NULL after free.
 *
 * WHAT GETS ZEROED:
 *   - callback: set to NULL (dangling pointer prevention)
 *   - registered_time: set to 0 (slot has no registration timestamp)
 *   - state: set to DWNL_CB_STATE_IDLE (slot is free for reuse)
 *
 * AFTER THIS CALL:
 *   The slot looks exactly like it did after internal_system_init():
 *   all zeros, state == IDLE, ready for a new registration.
 *   Subsequent DownloadProgress signals will skip this slot because
 *   dispatch_all_dwnl_active() only processes ACTIVE slots.
 *
 * free(NULL) SAFETY:
 *   If handle_key is already NULL (shouldn't happen, but defensive),
 *   free(NULL) is a safe no-op in C.
 *
 * @param entry  Pointer to the DwnlCallbackEntry to reset.
 *               MUST NOT be NULL.
 */
static void dwnl_registry_reset_slot(DwnlCallbackEntry *entry)
{
    if (entry->handle_key != NULL) {
        free(entry->handle_key);
        entry->handle_key = NULL;
    }
    entry->callback        = NULL;
    entry->registered_time = 0;
    entry->state           = DWNL_CB_STATE_IDLE;
}

/* ========================================================================
 * DOWNLOAD SIGNAL DATA HELPERS
 * ======================================================================== */

/*
 * internal_parse_dwnl_signal_data - Extract fields from DownloadProgress signal.
 *
 * PURPOSE:
 *   The daemon broadcasts DownloadProgress as a GVariant of type "(tsuss)".
 *   This function unpacks that GVariant into an InternalDwnlSignalData struct
 *   with individual typed fields.
 *
 * GVariant TYPE "(tsuss)" -- what each letter means:
 *   '(' and ')' = tuple delimiters
 *   't'         = uint64 (guint64) -- handler_id (which client this is for)
 *   's'         = string (gchar*)  -- firmware_name (file being downloaded)
 *   'u'         = uint32 (guint32) -- progress_percent (0-100)
 *   's'         = string (gchar*)  -- status_string ("INPROGRESS", "COMPLETED", etc.)
 *   's'         = string (gchar*)  -- message (human-readable)
 *
 * MEMORY MODEL (DIFFERENT FROM checkForUpdate):
 *   checkForUpdate used strdup() on strings returned by g_variant_get('s').
 *   For download, we DON'T strdup. Instead, g_variant_get() with 's' format
 *   returns a NEWLY-ALLOCATED gchar* that the caller must g_free().
 *
 *   Wait -- isn't 's' supposed to return a pointer into the GVariant?
 *   Actually NO: GLib documentation says for g_variant_get():
 *     's' format: returns a newly-allocated copy (gchar*) that caller frees.
 *     '&s' format: returns a pointer into the GVariant (no allocation).
 *   We use 's' (not '&s'), so we get fresh allocations that outlive the GVariant.
 *
 *   The caller (on_download_progress_signal) calls g_free() on all three
 *   string pointers AFTER dispatch is complete.
 *
 * VALIDATION:
 *   Checks the GVariant type is "(tsuss)" before extracting. If the daemon
 *   sends a different signature (version mismatch), we reject immediately.
 *
 * THREAD: Called on the BG thread (from on_download_progress_signal).
 *
 * @param parameters  The GVariant payload from the DownloadProgress signal.
 *                    Type must be "(tsuss)".
 * @param out_data    Output struct. Must be zero-initialized by caller.
 *                    On success, contains handler_id, progress, and 3 strings.
 *                    Strings are GLib-allocated -- caller must g_free() them.
 * @return true on success, false if parameters is NULL or wrong type.
 */
bool internal_parse_dwnl_signal_data(GVariant *parameters,
                                      InternalDwnlSignalData *out_data)
{
    if (parameters == NULL || out_data == NULL) return false;

    /*
     * Verify the type signature before extracting.
     * "(tsuss)" is the expected format for DownloadProgress.
     * If it's different, the daemon protocol changed -- reject.
     */
    const gchar *sig = g_variant_get_type_string(parameters);
    if (strcmp(sig, "(tsuss)") != 0) {
        FWUPMGR_ERROR("internal_parse_dwnl_signal_data: unexpected signature '%s' (expected '(tsuss)')\n", sig);
        return false;
    }

    /*
     * Local variables to receive g_variant_get() output.
     * For 's' format: GLib allocates fresh strings (must g_free).
     * For 't' and 'u' format: values copied directly into locals.
     */
    guint64  handler_id = 0;
    gchar   *firmware_name = NULL;
    guint32  progress = 0;
    gchar   *status_str = NULL;
    gchar   *message_str = NULL;

    /*
     * Extract all 5 fields from the GVariant tuple.
     * Format "(tsuss)" maps to:
     *   &handler_id    -- receives uint64
     *   &firmware_name -- receives gchar* (GLib-allocated)
     *   &progress      -- receives uint32
     *   &status_str    -- receives gchar* (GLib-allocated)
     *   &message_str   -- receives gchar* (GLib-allocated)
     */
    g_variant_get(parameters, "(tsuss)", 
                  &handler_id, 
                  &firmware_name, 
                  &progress, 
                  &status_str, 
                  &message_str);

    /*
     * Copy into output struct. The string pointers are just transferred --
     * we don't strdup them again. Ownership passes to the caller.
     * Caller is responsible for g_free() on firmware_name, status_string, message.
     */
    out_data->handler_id      = handler_id;
    out_data->firmware_name   = firmware_name;   /* Caller must g_free */
    out_data->progress_percent = progress;
    out_data->status_string   = status_str;      /* Caller must g_free */
    out_data->message         = message_str;     /* Caller must g_free */

    return true;
}

/*
 * internal_map_dwnl_status_code - Map integer status to DownloadStatus enum.
 *
 * PURPOSE:
 *   Legacy function kept for backward compatibility. In the current protocol,
 *   the daemon sends status as a STRING ("INPROGRESS", "COMPLETED", "ERROR")
 *   and the actual mapping is done by map_dwnl_status_string() below.
 *
 *   This function exists for cases where an integer status code is received
 *   (older daemon versions or internal testing).
 *
 * MAPPING:
 *   0 -> DWNL_IN_PROGRESS  (download is actively happening)
 *   1 -> DWNL_COMPLETED    (download finished successfully)
 *   2 -> DWNL_ERROR        (download failed)
 *   anything else -> DWNL_ERROR (unknown = treat as failure)
 *
 * @param status_code  Integer status from an older protocol format.
 * @return Corresponding DownloadStatus enum value.
 */
DownloadStatus internal_map_dwnl_status_code(int32_t status_code)
{
    switch (status_code) {
        case 0:  return DWNL_IN_PROGRESS;
        case 1:  return DWNL_COMPLETED;
        case 2:  return DWNL_ERROR;
        default:
            FWUPMGR_ERROR("internal_map_dwnl_status_code: unknown %d -> DWNL_ERROR\n",
                          status_code);
            return DWNL_ERROR;
    }
}

/*
 * map_dwnl_status_string - Map daemon's status string to DownloadStatus enum.
 *
 * PURPOSE:
 *   The daemon sends download status as a human-readable string in the
 *   DownloadProgress signal. This function converts that string to the
 *   typed DownloadStatus enum that the app's callback receives.
 *
 * WHY STRINGS INSTEAD OF INTEGERS?
 *   The daemon team chose strings for DownloadProgress (unlike
 *   CheckForUpdateComplete which uses integers). Strings are more
 *   debuggable in D-Bus tools (dbus-monitor shows "COMPLETED" not "1")
 *   but require strcmp-based mapping in the library.
 *
 * MAPPING:
 *   "INPROGRESS"  -> DWNL_IN_PROGRESS  (download actively downloading)
 *   "NOTSTARTED"  -> DWNL_IN_PROGRESS  (download queued, about to start)
 *   "COMPLETED"   -> DWNL_COMPLETED    (file fully downloaded)
 *   "ERROR"       -> DWNL_ERROR        (download failed)
 *   "DWNL_ERROR"  -> DWNL_ERROR        (alternate error string from daemon)
 *   NULL          -> DWNL_ERROR        (missing field = error)
 *   anything else -> DWNL_ERROR        (unknown = error, with log)
 *
 * WHY "NOTSTARTED" MAPS TO IN_PROGRESS:
 *   "NOTSTARTED" is the daemon's first signal saying "I received your
 *   request and queued it." From the app's perspective, this is the
 *   beginning of the download process -- it's "in progress" even if
 *   bytes haven't started flowing yet. There's no separate enum for
 *   "queued but not started" -- the app just sees 0% IN_PROGRESS.
 *
 * TERMINAL vs NON-TERMINAL:
 *   The return value determines whether dispatch_all_dwnl_active()
 *   resets the slot:
 *     DWNL_IN_PROGRESS -> slot stays ACTIVE (more signals coming)
 *     DWNL_COMPLETED   -> slot reset to IDLE (download ended)
 *     DWNL_ERROR       -> slot reset to IDLE (download ended)
 *
 * @param status_str  String from the daemon's signal. May be NULL.
 * @return Corresponding DownloadStatus enum value.
 */
static DownloadStatus map_dwnl_status_string(const char *status_str)
{
    /*
     * NULL means the field was missing from the signal (parse error
     * or daemon bug). Treat as error -- something is wrong.
     */
    if (status_str == NULL) {
        return DWNL_ERROR;
    }

    /*
     * String comparisons for known values.
     * "INPROGRESS" and "NOTSTARTED" both mean "not done yet."
     */
    if (strcmp(status_str, "INPROGRESS") == 0 || strcmp(status_str, "NOTSTARTED") == 0) {
        return DWNL_IN_PROGRESS;
    } else if (strcmp(status_str, "COMPLETED") == 0) {
        return DWNL_COMPLETED;
    } else if (strcmp(status_str, "ERROR") == 0 || strcmp(status_str, "DWNL_ERROR") == 0) {
        return DWNL_ERROR;
    }

    /*
     * Unknown string. Log it (for debugging daemon protocol changes)
     * and treat as error. The slot will be reset to IDLE.
     */
    FWUPMGR_ERROR("map_dwnl_status_string: unknown status '%s' -> DWNL_ERROR\n", status_str);
    return DWNL_ERROR;
}

/* ========================================================================
 * UPDATE FIRMWARE -- INTERNAL ENGINE
 * ========================================================================
 *
 * This section contains all the internal machinery that powers the
 * updateFirmware() public API. It is the third and final async engine
 * in the library, mirroring the download engine above.
 *
 * ARCHITECTURE OVERVIEW:
 *
 *   updateFirmware() [_api.c, main thread]
 *        |
 *        +--> internal_update_register_callback()  [registers in g_update_registry]
 *        +--> g_dbus_connection_call()             [fire-and-forget to daemon]
 *        |
 *   [daemon flashes firmware, broadcasts UpdateProgress signals]
 *        |
 *   on_update_progress_signal() [BG thread, GLib callback]
 *        |
 *        +--> internal_parse_update_signal_data()  [extract "(tsiis)" payload]
 *        +--> dispatch_all_update_active()         [two-phase dispatch]
 *              |
 *              +--> internal_map_update_status_code() [int -> UpdateStatus]
 *              +--> callback(progress, status)        [app's function]
 *              +--> update_registry_reset_slot()       [if terminal]
 *
 * KEY DIFFERENCES FROM DOWNLOAD ENGINE:
 *
 *   Signal format:
 *     Download: "(tsuss)" -- progress is uint32, status is STRING
 *     Update:   "(tsiis)" -- progress is int32,  status is INTEGER
 *
 *   Status mapping:
 *     Download: map_dwnl_status_string() uses strcmp on strings
 *     Update:   internal_map_update_status_code() uses switch on integers
 *
 *   Strings to free after parsing:
 *     Download: 3 (firmware_name, status_string, message)
 *     Update:   2 (firmware_name, message) -- no status_string
 *
 *   Registry:
 *     Download: g_dwnl_registry with DwnlCallbackEntry and DWNL_CB_STATE_*
 *     Update:   g_update_registry with UpdateCbEntry and UPDATE_CB_STATE_*
 *
 *   Slot lifecycle (same as download):
 *     IDLE -> ACTIVE (on register) -> ACTIVE (fires repeatedly) -> IDLE (on terminal)
 *
 * SIGNAL: UpdateProgress "(tsiis)"
 *   t  handler_id       -- uint64, identifies the registered client
 *   s  firmware_name    -- string, image being flashed
 *   i  progress_percent -- int32, 0 to 100
 *   i  status_code      -- int32, 0=IN_PROGRESS, 1=COMPLETED, 2=ERROR
 *   s  message          -- string, human-readable status
 *
 * Registry slot: ACTIVE until UPDATE_COMPLETED or UPDATE_ERROR, then IDLE.
 * ======================================================================== */

/* ---- Forward declarations for helper functions ---- */
/*
 * These forward declarations allow the functions to be defined in a
 * logical order (signal handler first, then dispatch, then helpers)
 * even though the C compiler needs to see declarations before use.
 */
static void dispatch_all_update_active(const InternalUpdateSignalData *signal_data);
static void update_registry_reset_slot(UpdateCbEntry *entry);

/* ========================================================================
 * UPDATE SUBSYSTEM LIFECYCLE
 * ======================================================================== */

/**
 * @brief Cleanup update registry -- frees all strdup'd handle_key strings
 *
 * PURPOSE:
 *   Called from internal_system_deinit() during library shutdown
 *   (unregisterProcess -> internal_system_deinit -> this function).
 *   Walks all 30 registry slots and frees any handle_key strings that
 *   were allocated by strdup() in internal_update_register_callback().
 *
 * WHY THIS IS NEEDED:
 *   When the library shuts down, any ACTIVE update callbacks are
 *   abandoned (no more signals will be dispatched). But the strdup'd
 *   handle_key strings are still on the heap. Without this cleanup,
 *   they would leak. Valgrind would report "definitely lost" blocks.
 *
 * WHAT ABOUT THE CALLBACKS THEMSELVES:
 *   Callback function pointers are not heap-allocated -- they're just
 *   pointers to compiled code. Setting callback=NULL is defensive but
 *   doesn't free anything. The ONLY heap allocation per slot is
 *   handle_key (from strdup).
 *
 * SIGNAL UNSUBSCRIPTION:
 *   This function does NOT unsubscribe from the UpdateProgress D-Bus
 *   signal. That's handled by the BG thread's cleanup code when it
 *   calls g_dbus_connection_signal_unsubscribe(). The signal
 *   subscription and the registry are independent concerns.
 *
 * MUTEX DESTRUCTION:
 *   After freeing all strings, pthread_mutex_destroy() is called to
 *   release the mutex's internal resources. After this, the mutex
 *   must NOT be used again -- any lock/unlock would be undefined behavior.
 *
 * THREAD SAFETY:
 *   Called during shutdown when the BG thread has already been stopped.
 *   The mutex lock/unlock is still used for correctness, even though
 *   no other thread should be accessing the registry at this point.
 *
 * Called from: internal_system_deinit() (in this file)
 */
static void internal_update_system_deinit(void)
{
    /*
     * Lock the mutex before modifying the registry. Even during
     * shutdown, we follow the locking protocol for consistency.
     */
    pthread_mutex_lock(&g_update_registry.mutex);

    /*
     * Walk all 30 slots and free any non-NULL handle_key strings.
     *
     * We don't check the slot's state -- even if a slot is somehow
     * in an inconsistent state, we still free its handle_key to
     * prevent leaks. A NULL handle_key means the slot was already
     * clean (IDLE with no previous allocation).
     */
    for (int i = 0; i < MAX_PENDING_CALLBACKS; i++) {
        if (g_update_registry.entries[i].handle_key != NULL) {
            free(g_update_registry.entries[i].handle_key);
            g_update_registry.entries[i].handle_key = NULL;  /* Prevent dangling pointer */
        }
    }

    /*
     * Unlock the mutex before destroying it. pthread_mutex_destroy()
     * requires the mutex to be unlocked. Destroying a locked mutex
     * is undefined behavior on most POSIX implementations.
     */
    pthread_mutex_unlock(&g_update_registry.mutex);

    /*
     * Destroy the mutex itself. This releases any OS resources
     * associated with the mutex (e.g., kernel futex state on Linux).
     * After this call, the mutex must NEVER be used again.
     */
    pthread_mutex_destroy(&g_update_registry.mutex);

    FWUPMGR_INFO("internal_update_system_deinit: done\n");
}

/* ========================================================================
 * UPDATE SIGNAL HANDLER
 *
 * When the daemon broadcasts an "UpdateProgress" D-Bus signal, GLib's
 * event loop on the BG thread dispatches it to on_update_progress_signal().
 * That function parses the signal, then calls dispatch_all_update_active()
 * to invoke all registered UpdateCallbacks.
 * ======================================================================== */

/**
 * @brief Called by GLib when UpdateProgress signal arrives on D-Bus
 *
 * PURPOSE:
 *   This is the BG thread's entry point for handling firmware update
 *   progress signals. When the daemon flashes firmware, it periodically
 *   broadcasts UpdateProgress signals on D-Bus. GLib's GMainLoop on
 *   the BG thread receives these signals and invokes THIS function.
 *
 * EXECUTION CONTEXT:
 *   Runs on the BACKGROUND THREAD (not the main thread).
 *   Called by g_main_loop_run() -> GLib signal dispatch.
 *   The BG thread subscribed to "UpdateProgress" signals during
 *   internal_system_init() using g_dbus_connection_signal_subscribe().
 *
 * SIGNAL FORMAT -- "(tsiis)":
 *   t  handler_id       -- uint64, identifies which registered client
 *   s  firmware_name    -- string, the image being flashed
 *   i  progress_percent -- int32, 0 to 100
 *   i  status_code      -- int32, 0=IN_PROGRESS, 1=COMPLETED, 2=ERROR
 *   s  message          -- string, human-readable status message
 *
 *   NOTE: This is DIFFERENT from DownloadProgress's "(tsuss)":
 *     - Download uses uint32 for progress, update uses int32
 *     - Download uses string for status ("INPROGRESS"), update uses int32
 *     - Download has 3 g_free-able strings, update has 2
 *
 * FLOW:
 *   1. Suppress unused parameter warnings with (void) casts
 *   2. Zero-initialize InternalUpdateSignalData on the stack
 *   3. Call internal_parse_update_signal_data() to extract fields
 *   4. Log the parsed data for debugging
 *   5. Call dispatch_all_update_active() to invoke all ACTIVE callbacks
 *   6. Free heap-allocated strings (firmware_name, message) from g_variant_get
 *
 * MEMORY OWNERSHIP:
 *   g_variant_get() with "s" format allocates new strings on the heap
 *   via g_strdup(). The caller (this function) MUST g_free() them.
 *   Two strings need freeing: firmware_name and message.
 *   (Compare: download has three -- firmware_name, status_string, message)
 *
 * THREAD SAFETY:
 *   This function itself is single-threaded (only the BG thread calls it).
 *   But it calls dispatch_all_update_active() which accesses the shared
 *   g_update_registry under mutex protection.
 *
 * @param conn            The BG thread's persistent D-Bus connection
 * @param sender          The D-Bus sender (daemon's unique name)
 * @param object_path     D-Bus object path ("/org/rdkfwupdater/Service")
 * @param interface_name  D-Bus interface ("org.rdkfwupdater.Interface")
 * @param signal_name     "UpdateProgress"
 * @param parameters      GVariant containing the "(tsiis)" payload
 * @param user_data       NULL (not used)
 */
static void on_update_progress_signal(GDBusConnection *conn,
                                      const gchar *sender,
                                      const gchar *object_path,
                                      const gchar *interface_name,
                                      const gchar *signal_name,
                                      GVariant *parameters,
                                      gpointer user_data)
{
    /*
     * Suppress "unused parameter" compiler warnings. GLib's signal
     * callback signature requires all 7 parameters, but we only
     * need 'parameters' (the signal payload). The (void) cast tells
     * the compiler "yes, I know I'm not using these."
     */
    (void)conn; (void)sender; (void)object_path;
    (void)interface_name; (void)signal_name; (void)user_data;

    FWUPMGR_INFO("on_update_progress_signal: received\n");

    /*
     * Stack-allocate the signal data struct and zero-initialize it.
     * memset ensures all pointers start as NULL and all integers as 0.
     * This is defensive -- if parsing fails partially, we don't have
     * garbage values in the struct.
     */
    InternalUpdateSignalData signal_data;
    memset(&signal_data, 0, sizeof(signal_data));

    /*
     * Parse the GVariant payload "(tsiis)" into our struct.
     * internal_parse_update_signal_data() validates the signature,
     * extracts handler_id, firmware_name, progress, status_code,
     * and message. Returns false if signature mismatch.
     *
     * If parsing fails, we bail out. No callbacks are invoked, no
     * memory needs freeing (the struct was zero-initialized).
     */
    if (!internal_parse_update_signal_data(parameters, &signal_data)) {
        FWUPMGR_ERROR("on_update_progress_signal: parse failed\n");
        return;
    }

    /*
     * Log the parsed signal data for debugging. This is invaluable
     * when troubleshooting "callback never fired" bugs -- it proves
     * whether the signal was received and what it contained.
     *
     * PRIu64 is the portable format specifier for uint64_t
     * (avoids warnings on 32-bit vs 64-bit platforms).
     */
    FWUPMGR_INFO("on_update_progress_signal: handler=%" PRIu64 " firmware='%s' progress=%d%% status=%d\n",
                 signal_data.handler_id,
                 signal_data.firmware_name ? signal_data.firmware_name : "(null)",
                 signal_data.progress_percent,
                 signal_data.status_code);

    /*
     * Dispatch the parsed signal data to all ACTIVE update callbacks.
     * dispatch_all_update_active() does the two-phase dispatch:
     *   Phase 1: snapshot ACTIVE entries under mutex
     *   Phase 2: invoke callbacks without mutex
     *   If terminal status: reset slot to IDLE
     *
     * After this call returns, all registered UpdateCallbacks have
     * been invoked with the current progress and status.
     */
    dispatch_all_update_active(&signal_data);

    /*
     * Free the heap-allocated strings from g_variant_get().
     *
     * g_variant_get() with the "s" format specifier allocates new
     * strings via g_strdup(). We own these strings and must free them.
     *
     * Two strings to free:
     *   1. firmware_name -- the image filename (e.g., "firmware_v8.bin")
     *   2. message       -- the status message (e.g., "Writing partition 2")
     *
     * Compare with download's on_download_progress_signal() which frees
     * THREE strings (firmware_name, status_string, message). Update
     * has no status_string because it uses an integer status_code instead.
     *
     * g_free(NULL) is safe (it's a no-op), so we don't need NULL checks.
     */
    g_free(signal_data.firmware_name);
    g_free(signal_data.message);
}

/**
 * @brief Dispatch UpdateProgress signal to every ACTIVE update callback
 *
 * PURPOSE:
 *   Called by on_update_progress_signal() after parsing the D-Bus signal
 *   payload. Finds ALL ACTIVE entries in g_update_registry and invokes
 *   their callbacks with the current progress and status.
 *
 * TWO-PHASE DESIGN (identical pattern to download dispatch):
 *
 *   PHASE 1 (mutex HELD):
 *     - Lock g_update_registry.mutex
 *     - Scan all 30 slots for ACTIVE entries
 *     - For each ACTIVE entry, copy callback pointer, handle, and slot
 *       index into a local snapshot array on the stack
 *     - Determine if this is a terminal signal (COMPLETED or ERROR)
 *     - Unlock mutex
 *
 *   PHASE 2 (mutex RELEASED):
 *     - Iterate through snapshot array
 *     - Invoke each callback(progress_percent, status)
 *     - If terminal signal: re-lock mutex, reset slot to IDLE, unlock
 *     - If in-progress: leave slot ACTIVE for the next signal
 *
 * WHY TWO PHASES (not one):
 *   If we held the mutex while invoking callbacks, the callbacks could
 *   not safely call any library function that touches the registry
 *   (e.g., updateFirmware() again, or unregisterProcess()). That would
 *   deadlock because our thread already holds the mutex. By releasing
 *   the mutex before invoking callbacks, we avoid this entirely.
 *
 * WHY SNAPSHOT (not direct access):
 *   Once we release the mutex, another thread could modify the registry
 *   (e.g., the main thread calling updateFirmware() to register a new
 *   callback). The snapshot freezes the state at scan time, so our
 *   iteration is safe regardless of concurrent modifications.
 *
 * TERMINAL vs IN-PROGRESS SIGNALS:
 *   - status == UPDATE_COMPLETED or UPDATE_ERROR -> TERMINAL
 *     The update is done (success or failure). Reset slot to IDLE so
 *     it can be reused for future updateFirmware() calls.
 *   - status == UPDATE_IN_PROGRESS -> IN-PROGRESS
 *     The update is still running. Leave slot ACTIVE so the NEXT
 *     UpdateProgress signal also dispatches to this callback.
 *
 *   This is the key difference from checkForUpdate's dispatch:
 *     checkForUpdate: slot fires ONCE then goes to IDLE
 *     downloadFirmware: slot fires MANY times, IDLE on terminal
 *     updateFirmware: slot fires MANY times, IDLE on terminal (same)
 *
 * THREAD SAFETY:
 *   Called on the BG thread. Accesses g_update_registry under mutex.
 *   Phase 2 callbacks run WITHOUT mutex -- the app's callback function
 *   can safely call library APIs without deadlocking.
 *
 * @param signal_data  Parsed signal data from on_update_progress_signal()
 */
static void dispatch_all_update_active(const InternalUpdateSignalData *signal_data)
{
    /*
     * Local snapshot struct -- stores one entry's callback info.
     * We allocate an array of these on the stack (up to 30 entries).
     *
     * Fields:
     *   callback    -- the UpdateCallback function pointer to invoke
     *   handle_copy -- string copy of handle_key (for logging only)
     *   slot_index  -- which g_update_registry.entries[] index this is
     *   is_final    -- true if this signal ends the update (COMPLETED/ERROR)
     *
     * handle_copy is a fixed-size char[256] buffer, not a heap allocation.
     * This avoids malloc/free overhead for a temporary logging string.
     */
    typedef struct {
        UpdateCallback  callback;
        char            handle_copy[256];
        int             slot_index;
        bool            is_final;
    } UpdateSnapshot;

    UpdateSnapshot snapshots[MAX_PENDING_CALLBACKS];
    int            count = 0;

    /*
     * Map the raw integer status_code to the UpdateStatus enum.
     * internal_map_update_status_code() does:
     *   0 -> UPDATE_IN_PROGRESS
     *   1 -> UPDATE_COMPLETED
     *   2 -> UPDATE_ERROR
     *   anything else -> UPDATE_ERROR (defensive default)
     *
     * Then determine if this is a terminal signal (update finished).
     * A terminal signal means we should reset the slot to IDLE after
     * invoking the callback, because no more signals are coming.
     */
    UpdateStatus status   = internal_map_update_status_code(signal_data->status_code);
    bool         is_final = (status == UPDATE_COMPLETED || status == UPDATE_ERROR);

    /* ---- PHASE 1: snapshot under mutex ---- */

    /*
     * Lock the registry. While we hold this lock, no other thread can
     * modify g_update_registry (e.g., the main thread can't register
     * a new callback via internal_update_register_callback()).
     */
    pthread_mutex_lock(&g_update_registry.mutex);

    /*
     * Scan all 30 slots. For each ACTIVE entry, copy its info into
     * the snapshot array. We copy the callback pointer (not the entry
     * pointer) because after releasing the mutex, the entry could be
     * modified by another thread.
     */
    for (int i = 0; i < MAX_PENDING_CALLBACKS; i++) {
        UpdateCbEntry *e = &g_update_registry.entries[i];

        /*
         * Skip non-ACTIVE entries. IDLE slots have no callback to invoke.
         * (There's no PENDING or DISPATCHED state for update -- only
         * IDLE and ACTIVE.)
         */
        if (e->state != UPDATE_CB_STATE_ACTIVE) continue;

        /*
         * Copy this entry's data into the snapshot:
         *   callback   -- the function pointer we'll invoke in Phase 2
         *   slot_index -- needed to reset this specific slot in Phase 2
         *   is_final   -- same for all entries (determined by status_code)
         *   handle_copy -- snprintf'd for safe logging (truncated to 255 chars)
         */
        snapshots[count].callback   = e->callback;
        snapshots[count].slot_index = i;
        snapshots[count].is_final   = is_final;
        snprintf(snapshots[count].handle_copy,
                 sizeof(snapshots[count].handle_copy),
                 "%s", e->handle_key ? e->handle_key : "");

        count++;

        FWUPMGR_INFO("dispatch_all_update_active: queued handle='%s' "
                     "progress=%d%% final=%d\n",
                     e->handle_key ? e->handle_key : "(null)",
                     signal_data->progress_percent, is_final);
    }

    /*
     * Release the mutex. From this point, the main thread can freely
     * register new callbacks. Our snapshot is a frozen copy -- it won't
     * be affected by concurrent registry modifications.
     */
    pthread_mutex_unlock(&g_update_registry.mutex);

    FWUPMGR_INFO("dispatch_all_update_active: %d callback(s) to fire\n", count);

    /* ---- PHASE 2: invoke callbacks, no mutex held ---- */

    /*
     * Iterate through the snapshot array and invoke each callback.
     * No mutex is held during callback invocation -- the app's callback
     * can safely call any library function without deadlocking.
     */
    for (int i = 0; i < count; i++) {
        UpdateSnapshot *s = &snapshots[i];

        FWUPMGR_INFO("dispatch_all_update_active: invoking callback "
                     "for handle='%s'\n", s->handle_copy);

        /*
         * INVOKE THE APP'S CALLBACK
         *
         * Callback signature: void fn(int progress_per, UpdateStatus status)
         * Matches the UpdateCallback typedef exactly.
         *
         * Arguments:
         *   signal_data->progress_percent -- 0 to 100 (how far along)
         *   status -- UPDATE_IN_PROGRESS, UPDATE_COMPLETED, or UPDATE_ERROR
         *
         * This runs on the BG thread. If the app needs to update UI,
         * it must marshal the call to the main thread (e.g., via
         * pthread_cond_signal, g_idle_add, or similar mechanism).
         *
         * The callback MUST NOT block for a long time, because it blocks
         * this BG thread from processing further D-Bus signals.
         */
        s->callback(signal_data->progress_percent, status);

        /*
         * If this was a TERMINAL signal (COMPLETED or ERROR), reset
         * the slot back to IDLE so it can be reused.
         *
         * We must RE-ACQUIRE the mutex to modify the registry. This is
         * a brief lock/unlock pair -- just enough to reset one slot.
         *
         * If this was an IN-PROGRESS signal, we leave the slot ACTIVE.
         * The next UpdateProgress signal will dispatch to the same
         * callback again.
         */
        if (s->is_final) {
            pthread_mutex_lock(&g_update_registry.mutex);
            update_registry_reset_slot(&g_update_registry.entries[s->slot_index]);
            pthread_mutex_unlock(&g_update_registry.mutex);

            FWUPMGR_INFO("dispatch_all_update_active: slot %d -> IDLE "
                         "(update ended)\n", s->slot_index);
        }
    }
}

/* ========================================================================
 * UPDATE REGISTRY OPERATIONS
 *
 * These functions manage the g_update_registry -- allocating slots for
 * new update callbacks (internal_update_register_callback) and cleaning
 * up slots when updates complete (update_registry_reset_slot).
 *
 * The registry holds up to MAX_PENDING_CALLBACKS (30) entries.
 * Each entry has two states: UPDATE_CB_STATE_IDLE (available) and
 * UPDATE_CB_STATE_ACTIVE (callback registered, waiting for signals).
 * ======================================================================== */

/**
 * @brief Register an update callback keyed by handle
 *
 * PURPOSE:
 *   Called by updateFirmware() (in _api.c) AFTER the D-Bus connection
 *   succeeds but BEFORE the fire-and-forget D-Bus call is sent.
 *   Allocates a slot in g_update_registry so the BG thread can find
 *   the callback when UpdateProgress signals arrive.
 *
 * HOW IT WORKS:
 *   1. Lock g_update_registry.mutex (prevents races with BG thread)
 *   2. Scan all MAX_PENDING_CALLBACKS (30) slots looking for:
 *      a. An existing ACTIVE slot with the same handle (dedup case)
 *      b. The first IDLE slot (normal allocation case)
 *   3. Pick the target:
 *      - If same handle found: overwrite it (existing_slot)
 *      - Else if free slot found: use it (free_slot)
 *      - Else: return false (registry full)
 *   4. Populate the target slot:
 *      - handle_key = strdup(handle)           -- heap copy of "1"
 *      - callback = the UpdateCallback function pointer
 *      - state = UPDATE_CB_STATE_ACTIVE         -- ready for dispatch
 *      - registered_time = time(NULL)           -- unix timestamp
 *   5. Unlock mutex and return true
 *
 * SAME HANDLE TWICE:
 *   If the same handle already has an ACTIVE slot (e.g., the app calls
 *   updateFirmware() again before the first update finishes), the old
 *   entry is OVERWRITTEN. The old handle_key string is freed first to
 *   avoid a memory leak. This means:
 *   - Only ONE active update callback per handle at a time
 *   - The NEW callback replaces the old one
 *   - The old callback will never fire again
 *
 * WHY ACTIVE (NOT PENDING):
 *   checkForUpdate uses PENDING -> DISPATCHED -> IDLE (fires once).
 *   updateFirmware uses ACTIVE -> IDLE (fires many times until terminal).
 *   The slot stays ACTIVE and the callback fires on EVERY UpdateProgress
 *   signal until the status is UPDATE_COMPLETED or UPDATE_ERROR, at
 *   which point dispatch_all_update_active() resets it to IDLE.
 *
 * THREAD SAFETY:
 *   Thread-safe. Protected by g_update_registry.mutex.
 *   Called from the main thread (inside updateFirmware()).
 *   The BG thread reads the same registry in dispatch_all_update_active().
 *   The mutex ensures they never read/write the same slot simultaneously.
 *
 * MEMORY:
 *   handle_key = strdup(handle) -- heap allocated by THIS function.
 *   Freed by update_registry_reset_slot() when the slot returns to IDLE,
 *   or freed here if overwriting an existing entry.
 *
 * @param handle    The handle string from registerProcess(), e.g. "1"
 * @param callback  The UpdateCallback function pointer to invoke later
 * @return true if registered, false if registry full (all 30 slots occupied)
 */
bool internal_update_register_callback(FirmwareInterfaceHandle handle,
                                        UpdateCallback callback)
{
    /*
     * Lock the update registry mutex. This mutex protects the entire
     * g_update_registry.entries[] array from concurrent access.
     * The BG thread also locks this mutex when dispatching callbacks.
     */
    pthread_mutex_lock(&g_update_registry.mutex);

    /*
     * We need to find a slot. Two pointers track our search:
     *   free_slot     -- first IDLE entry found (available for use)
     *   existing_slot -- entry with same handle (dedup/overwrite case)
     *
     * Both start NULL. If neither is found after scanning, the registry
     * is full of ACTIVE entries (all 30 slots occupied by other handles).
     */
    UpdateCbEntry *free_slot     = NULL;
    UpdateCbEntry *existing_slot = NULL;

    /*
     * Linear scan through all 30 slots. We MUST scan the entire array
     * even after finding a free slot, because we need to check for
     * duplicate handles. An existing ACTIVE entry for the same handle
     * takes priority over a free slot.
     */
    for (int i = 0; i < MAX_PENDING_CALLBACKS; i++) {
        UpdateCbEntry *e = &g_update_registry.entries[i];

        /*
         * Check for DEDUP: is this an ACTIVE entry with the same handle?
         *
         * If the app calls updateFirmware() twice with the same handle
         * before the first update finishes, we find the old entry here
         * and overwrite it rather than wasting a new slot.
         *
         * strcmp(e->handle_key, handle) == 0 means exact string match.
         * The handle_key NULL check prevents strcmp(NULL, ...) crash.
         */
        if (e->state == UPDATE_CB_STATE_ACTIVE &&
            e->handle_key != NULL &&
            strcmp(e->handle_key, handle) == 0) {
            existing_slot = e;
            break;  /* Found duplicate -- no need to continue scanning */
        }

        /*
         * Track the FIRST idle slot we encounter. We only save the
         * first one (free_slot == NULL check) because we want the
         * lowest-indexed available slot for consistency.
         */
        if (free_slot == NULL && e->state == UPDATE_CB_STATE_IDLE) {
            free_slot = e;
        }
    }

    /*
     * Pick the target slot:
     *   - Prefer existing_slot (dedup/overwrite case)
     *   - Fall back to free_slot (normal allocation case)
     *   - If both NULL: registry is full, fail
     */
    UpdateCbEntry *target = existing_slot ? existing_slot : free_slot;

    if (target == NULL) {
        /*
         * All 30 slots are ACTIVE with different handles. This should
         * never happen in practice -- a device rarely has more than
         * one or two concurrent update clients. But we handle it
         * gracefully by returning false instead of crashing.
         */
        FWUPMGR_ERROR("internal_update_register_callback: registry full (max=%d)\n",
                      MAX_PENDING_CALLBACKS);
        pthread_mutex_unlock(&g_update_registry.mutex);
        return false;
    }

    /*
     * If we're overwriting an existing entry (same handle), free the
     * old handle_key string to avoid a memory leak. The old strdup'd
     * string is on the heap and must be explicitly freed.
     */
    if (existing_slot) {
        FWUPMGR_INFO("internal_update_register_callback: "
                     "overwriting existing for handle='%s'\n", handle);
        free(target->handle_key);
        target->handle_key = NULL;  /* Defensive: NULL before reassignment */
    }

    /*
     * Populate the slot with the new callback registration.
     *
     * strdup(handle) creates a heap copy of the handle string "1".
     * We need our own copy because the caller's string may go out of
     * scope or be freed after updateFirmware() returns.
     *
     * UPDATE_CB_STATE_ACTIVE means this slot is ready to receive
     * UpdateProgress signals. The BG thread's dispatch_all_update_active()
     * will find it and invoke the callback.
     *
     * registered_time is recorded for diagnostic purposes (log how
     * long a slot has been active).
     */
    target->handle_key      = strdup(handle);
    target->callback        = callback;
    target->state           = UPDATE_CB_STATE_ACTIVE;
    target->registered_time = time(NULL);

    /*
     * Unlock the mutex. The slot is now visible to the BG thread.
     * From this point, any UpdateProgress signal will find our ACTIVE
     * entry and invoke the callback.
     */
    pthread_mutex_unlock(&g_update_registry.mutex);

    FWUPMGR_INFO("internal_update_register_callback: registered handle='%s'\n",
                 handle);
    return true;
}

/**
 * @brief Reset an update registry slot to IDLE
 *
 * PURPOSE:
 *   Returns a single UpdateCbEntry to the IDLE (empty) state so it
 *   can be reused by a future updateFirmware() call. Called in two
 *   situations:
 *     1. dispatch_all_update_active() -- when a terminal signal arrives
 *        (UPDATE_COMPLETED or UPDATE_ERROR), the slot is reset after
 *        the callback is invoked.
 *     2. internal_update_system_deinit() -- during library shutdown,
 *        all slots are cleaned up (though deinit frees handle_key
 *        directly rather than calling this function).
 *
 * WHAT IT DOES:
 *   1. Frees the handle_key string (heap-allocated by strdup in
 *      internal_update_register_callback). Sets pointer to NULL.
 *   2. Clears the callback function pointer to NULL.
 *   3. Resets registered_time to 0.
 *   4. Sets state back to UPDATE_CB_STATE_IDLE.
 *
 *   After this call, the slot is indistinguishable from a never-used
 *   slot. It will be found by the next registry scan looking for a
 *   free slot.
 *
 * THREAD SAFETY:
 *   MUST be called with g_update_registry.mutex HELD by the caller.
 *   This function does NOT lock the mutex itself -- the caller is
 *   responsible for locking. This is because the caller typically
 *   needs to do the lock, call this function, then do other work
 *   before unlocking (or is already inside a locked section).
 *
 * MEMORY:
 *   Frees one heap allocation: handle_key (from strdup).
 *   Does NOT free the entry itself -- entries are array elements
 *   inside g_update_registry, not individually heap-allocated.
 *
 * @param entry  Pointer to the UpdateCbEntry to reset. Must not be NULL.
 */
static void update_registry_reset_slot(UpdateCbEntry *entry)
{
    /*
     * Free the handle_key string if it exists.
     *
     * handle_key was allocated by strdup() in
     * internal_update_register_callback(). We must free it to avoid
     * a memory leak. The NULL check is defensive -- an IDLE slot
     * should already have handle_key == NULL, but we check anyway.
     */
    if (entry->handle_key != NULL) {
        free(entry->handle_key);
        entry->handle_key = NULL;  /* Prevent dangling pointer / double-free */
    }

    /*
     * Clear the callback function pointer. Setting to NULL ensures
     * that even if something accidentally tries to invoke this slot's
     * callback, it will be a NULL dereference (crash) rather than
     * calling a stale function pointer (undefined behavior / security risk).
     */
    entry->callback        = NULL;

    /*
     * Reset the registration timestamp. Not strictly necessary for
     * correctness, but keeps the slot in a clean, known state.
     */
    entry->registered_time = 0;

    /*
     * Set state back to IDLE. This is the critical line -- it's what
     * makes the slot available for reuse by the next
     * internal_update_register_callback() call. The linear scan in
     * that function looks for UPDATE_CB_STATE_IDLE to find free slots.
     */
    entry->state           = UPDATE_CB_STATE_IDLE;
}

/* ========================================================================
 * UPDATE SIGNAL DATA HELPERS
 *
 * These helper functions handle the translation between D-Bus wire
 * format and the library's internal types:
 *
 *   internal_parse_update_signal_data() -- GVariant "(tsiis)" -> struct
 *   internal_map_update_status_code()   -- int (0/1/2) -> UpdateStatus enum
 *
 * Both are pure functions with no side effects (except logging on error).
 * ======================================================================== */

/**
 * @brief Parse GVariant UpdateProgress payload into a struct
 *
 * PURPOSE:
 *   Extracts the five fields from the D-Bus UpdateProgress signal's
 *   GVariant payload and stores them in an InternalUpdateSignalData
 *   struct for easy access by the dispatch logic.
 *
 * EXPECTED GVariant SIGNATURE: "(tsiis)"
 *   t  handler_id       -- uint64: identifies which registered client
 *   s  firmware_name    -- string: the image being flashed (e.g., "firmware_v8.bin")
 *   i  progress_percent -- int32:  0 to 100
 *   i  status_code      -- int32:  0=IN_PROGRESS, 1=COMPLETED, 2=ERROR
 *   s  message          -- string: human-readable status message
 *
 * COMPARISON WITH DOWNLOAD SIGNAL:
 *   Download signal "(tsuss)":
 *     t handler_id, s firmware_name, u progress (uint32),
 *     s status_string ("INPROGRESS"/"COMPLETED"/"ERROR"), s message
 *   Update signal "(tsiis)":
 *     t handler_id, s firmware_name, i progress (int32),
 *     i status_code (0/1/2), s message
 *
 *   Key differences:
 *     - Download: progress is uint32 (u), status is string (s)
 *     - Update:   progress is int32 (i),  status is int32 (i)
 *     - Download: 3 strings to g_free (firmware_name, status_string, message)
 *     - Update:   2 strings to g_free (firmware_name, message)
 *
 * MEMORY OWNERSHIP:
 *   g_variant_get() with "s" format ALLOCATES new strings on the heap
 *   (via g_strdup). The CALLER is responsible for freeing them with
 *   g_free() when done. This function sets:
 *     out_data->firmware_name  -- caller must g_free()
 *     out_data->message        -- caller must g_free()
 *   Integer fields (handler_id, progress_percent, status_code) are
 *   simple value copies -- no heap allocation.
 *
 * THREAD SAFETY:
 *   Safe -- operates only on its parameters (no global state).
 *   Called from on_update_progress_signal() on the BG thread.
 *
 * @param parameters  The GVariant from the D-Bus signal. Must not be NULL.
 * @param out_data    Output struct to populate. Must not be NULL.
 *                    Caller must g_free firmware_name and message.
 * @return true on success, false if parameters is NULL, out_data is NULL,
 *         or the GVariant signature doesn't match "(tsiis)".
 */
bool internal_parse_update_signal_data(GVariant *parameters,
                                        InternalUpdateSignalData *out_data)
{
    /*
     * NULL checks on both parameters. If either is NULL, we can't
     * proceed -- return false immediately. No cleanup needed because
     * nothing has been allocated yet.
     */
    if (parameters == NULL || out_data == NULL) return false;

    /*
     * Verify the GVariant's type signature matches what we expect.
     *
     * g_variant_get_type_string() returns the GVariant's type as a
     * string, e.g. "(tsiis)". If the daemon sends a different format
     * (e.g., due to a version mismatch), we'd get garbage if we tried
     * to extract with the wrong format. This check catches mismatches
     * early with a clear error message.
     *
     * strcmp returns 0 if the strings are equal. If NOT equal (non-zero),
     * log the unexpected signature and return false.
     */
    const gchar *sig = g_variant_get_type_string(parameters);
    if (strcmp(sig, "(tsiis)") != 0) {
        FWUPMGR_ERROR("internal_parse_update_signal_data: "
                      "unexpected signature '%s' (expected '(tsiis)')\n", sig);
        return false;
    }

    /*
     * Declare local variables to receive the extracted values.
     * We use typed locals rather than extracting directly into out_data
     * for clarity and to match the GLib API pattern.
     *
     * guint64 handler_id   -- maps to 't' (uint64)
     * gchar  *firmware_name -- maps to 's' (string, heap-allocated by g_variant_get)
     * gint32  progress      -- maps to 'i' (int32)
     * gint32  status        -- maps to 'i' (int32)
     * gchar  *message_str   -- maps to 's' (string, heap-allocated by g_variant_get)
     */
    guint64  handler_id = 0;
    gchar   *firmware_name = NULL;
    gint32   progress = 0;
    gint32   status = 0;
    gchar   *message_str = NULL;

    /*
     * Extract all five fields from the GVariant in one call.
     *
     * g_variant_get() is the inverse of g_variant_new(). The format
     * string "(tsiis)" tells GLib how to interpret the binary data:
     *   t -> extract as guint64, store at &handler_id
     *   s -> extract as string, allocate copy, store at &firmware_name
     *   i -> extract as gint32, store at &progress
     *   i -> extract as gint32, store at &status
     *   s -> extract as string, allocate copy, store at &message_str
     *
     * The 's' format specifier ALWAYS allocates a new string (g_strdup).
     * This is why the caller must g_free() firmware_name and message.
     */
    g_variant_get(parameters, "(tsiis)", 
                  &handler_id, 
                  &firmware_name, 
                  &progress, 
                  &status, 
                  &message_str);

    /*
     * Copy the extracted values into the output struct.
     *
     * For strings (firmware_name, message): we transfer OWNERSHIP of
     * the heap-allocated string to the caller via the out_data struct.
     * The caller (on_update_progress_signal) is responsible for calling
     * g_free() on these pointers when done.
     *
     * For integers (handler_id, progress_percent, status_code): these
     * are simple value copies. No heap allocation, no cleanup needed.
     */
    out_data->handler_id       = handler_id;
    out_data->firmware_name    = firmware_name;   /* Caller must g_free */
    out_data->progress_percent = progress;
    out_data->status_code      = status;
    out_data->message          = message_str;     /* Caller must g_free */

    return true;
}

/**
 * @brief Map raw integer status code to UpdateStatus enum
 *
 * PURPOSE:
 *   The daemon's UpdateProgress signal sends status as a raw integer
 *   (0, 1, or 2). The library's public API uses the UpdateStatus enum
 *   (UPDATE_IN_PROGRESS, UPDATE_COMPLETED, UPDATE_ERROR). This function
 *   translates between the two representations.
 *
 * COMPARISON WITH DOWNLOAD STATUS MAPPING:
 *   Download uses STRING codes: "INPROGRESS", "COMPLETED", "ERROR"
 *     -> mapped by map_dwnl_status_string() using strcmp()
 *   Update uses INTEGER codes: 0, 1, 2
 *     -> mapped by THIS function using a switch statement
 *
 *   The integer approach is simpler and faster (no string comparison),
 *   but less self-documenting in D-Bus traces. The two APIs evolved
 *   independently, which is why they use different conventions.
 *
 * MAPPING:
 *   0 -> UPDATE_IN_PROGRESS  (flashing is underway, more signals coming)
 *   1 -> UPDATE_COMPLETED    (flashing finished successfully)
 *   2 -> UPDATE_ERROR        (flashing failed)
 *   anything else -> UPDATE_ERROR (defensive default, with error log)
 *
 * WHY DEFAULT TO ERROR:
 *   If the daemon sends an unknown status code (e.g., 3), we treat it
 *   as an error. This is the SAFEST default because:
 *   - It causes the slot to be reset to IDLE (terminal status)
 *   - It notifies the app that something unexpected happened
 *   - It prevents the slot from staying ACTIVE forever
 *   If we defaulted to IN_PROGRESS, an unknown code would leave the
 *   slot ACTIVE indefinitely, leaking a registry slot.
 *
 * THREAD SAFETY:
 *   Pure function -- no side effects, no global state. Safe to call
 *   from any thread.
 *
 * @param status_code  The raw integer from the D-Bus signal (0, 1, or 2)
 * @return The corresponding UpdateStatus enum value
 */
UpdateStatus internal_map_update_status_code(int32_t status_code)
{
    switch (status_code) {
        /*
         * 0 = IN_PROGRESS: The daemon is still flashing the firmware.
         * More UpdateProgress signals will follow. The callback slot
         * stays ACTIVE.
         */
        case 0:  return UPDATE_IN_PROGRESS;

        /*
         * 1 = COMPLETED: The firmware was flashed successfully.
         * This is a TERMINAL status -- no more signals will come.
         * dispatch_all_update_active() will reset the slot to IDLE.
         */
        case 1:  return UPDATE_COMPLETED;

        /*
         * 2 = ERROR: The firmware flash failed.
         * This is a TERMINAL status -- no more signals will come.
         * dispatch_all_update_active() will reset the slot to IDLE.
         */
        case 2:  return UPDATE_ERROR;

        /*
         * Unknown status code. This should never happen with a matching
         * daemon version. But if the daemon is newer and adds status
         * code 3 (e.g., "PAUSED"), we default to ERROR so the slot
         * gets cleaned up rather than stuck ACTIVE forever.
         */
        default:
            FWUPMGR_ERROR("internal_map_update_status_code: "
                          "unknown %d -> UPDATE_ERROR\n", status_code);
            return UPDATE_ERROR;
    }
}

/* ========================================================================
 * UPDATE DETAILS PARSING
 * ======================================================================== */

/**
 * parse_update_details - Parse pipe-separated firmware details into a struct.
 *
 * OVERVIEW
 *
 * PURPOSE:
 *   When the daemon reports FIRMWARE_AVAILABLE, it includes a string
 *   describing the available firmware. This string uses a custom
 *   pipe-separated Key:Value format:
 *
 *     "File:firmware_v8.bin|Location:http://cdn.example.com/fw|Version:RDKV_8.0|Reboot:false|Delay:false|PDRI:N/A|Peripherals:N/A"
 *
 *   This function tokenizes that string and copies each value into the
 *   appropriate field of an UpdateDetails struct, which the callback
 *   receives via FwInfoData->UpdateDetails.
 *
 * WHY PIPE-SEPARATED (NOT JSON)?
 *   This is a daemon-internal format, not a public protocol. It's simple,
 *   requires no JSON parser dependency, and is easy to tokenize with
 *   strtok_r(). The library translates this format into typed struct
 *   fields so callers never see the pipe-separated format.
 *
 * STRING FORMAT:
 *   - Tokens separated by '|' (pipe)
 *   - Each token is "Key:Value" (colon-separated)
 *   - Known keys: File, Location, IPv6Location, Version, Reboot,
 *                 Delay, PDRI, Peripherals, Protocol, CertBundle
 *   - "N/A" is treated as "not available" for PDRI and Peripherals
 *   - Unknown keys are logged and skipped (forward compatibility)
 *
 * THREAD SAFETY:
 *   Safe -- operates only on local data. The work_str is a strdup'd
 *   copy (so strtok_r doesn't modify the original), and out_details
 *   is caller-provided (stack-allocated in dispatch_all_pending).
 *
 * MEMORY:
 *   work_str is strdup'd at the start and freed at the end.
 *   out_details fields are char arrays (not pointers) -- data is
 *   copied directly into the struct, no additional heap allocation.
 *
 * ROBUSTNESS:
 *   - NULL/empty input is valid (returns success with zeroed struct)
 *   - Malformed tokens (no colon) are logged and skipped
 *   - Unknown keys are logged and skipped
 *   - strdup failure returns false (out of memory)
 *
 * @param update_details_str  The pipe-separated string from the daemon.
 *                            May be NULL or empty (both are valid).
 * @param out_details         Output struct. Filled with parsed values.
 *                            Caller must provide allocated storage.
 * @return true on success (even if input was NULL/empty -- struct is zeroed),
 *         false only on critical errors (NULL out_details, OOM).
 *
 * Called by: dispatch_all_pending() (Phase 2, when status == FIRMWARE_AVAILABLE)
 */
static bool parse_update_details(const char *update_details_str,
                                   UpdateDetails *out_details)
{
    /*
     * NULL check on the output struct. This is a programming error
     * in the caller -- should never happen, but catch it defensively.
     */
    if (out_details == NULL) {
        FWUPMGR_ERROR("parse_update_details: out_details is NULL\n");
        return false;
    }

    /*
     * Zero-initialize the output struct.
     *
     * All char arrays (FwFileName, FwUrl, etc.) start as empty strings
     * (first byte '\0'). This ensures that if a key is missing from
     * the daemon's string, the corresponding field is empty rather
     * than containing garbage.
     */
    memset(out_details, 0, sizeof(UpdateDetails));

    /*
     * NULL or empty input is valid -- it means the daemon has no
     * details to share. Return success with a zeroed struct.
     * The caller (dispatch_all_pending) will see empty strings in
     * all fields and can handle accordingly.
     */
    if (update_details_str == NULL || update_details_str[0] == '\0') {
        FWUPMGR_INFO("parse_update_details: empty input, returning zeroed structure\n");
        return true;
    }

    FWUPMGR_INFO("parse_update_details: parsing '%s'\n", update_details_str);

    /*
     * Create a working copy of the input string.
     *
     * Why? strtok_r() MODIFIES the string it tokenizes (it replaces
     * delimiters with '\0'). The input string belongs to InternalSignalData
     * (from strdup in internal_parse_signal_data). We must not modify it
     * because internal_cleanup_signal_data() needs to free() the original
     * pointer. Modifying the string would corrupt the pointer if strtok_r
     * happened to insert '\0' at a different position.
     *
     * strdup() allocates strlen(update_details_str)+1 bytes on the heap.
     * Freed at the end of this function.
     */
    char *work_str = strdup(update_details_str);
    if (work_str == NULL) {
        FWUPMGR_ERROR("parse_update_details: strdup failed\n");
        return false;
    }

    /*
     * Tokenize the pipe-separated string.
     *
     * strtok_r() is the REENTRANT version of strtok(). We use it
     * instead of strtok() because:
     *   - strtok() uses a static internal buffer -- NOT thread-safe.
     *     If another thread called strtok() simultaneously, they'd
     *     corrupt each other's state.
     *   - strtok_r() uses the caller-provided 'saveptr' for state,
     *     making it thread-safe.
     *
     * First call: strtok_r(work_str, "|", &saveptr)
     *   Returns pointer to first token (everything before first '|')
     *   Replaces the '|' with '\0' in work_str
     *   Stores position in saveptr for next call
     *
     * Subsequent calls: strtok_r(NULL, "|", &saveptr)
     *   Returns pointer to next token
     *   NULL when no more tokens
     *
     * Example:
     *   Input:  "File:fw.bin|Version:8.0|Reboot:false"
     *   Call 1: returns "File:fw.bin"
     *   Call 2: returns "Version:8.0"
     *   Call 3: returns "Reboot:false"
     *   Call 4: returns NULL (done)
     */
    char *saveptr = NULL;
    char *token = strtok_r(work_str, "|", &saveptr);

    while (token != NULL) {
        /*
         * Each token should be "Key:Value". Find the colon separator.
         *
         * strchr() returns a pointer to the first ':' in the token,
         * or NULL if there is no colon (malformed token).
         */
        char *colon = strchr(token, ':');
        if (colon == NULL) {
            /*
             * No colon found -- this token is malformed. Skip it.
             * This is defensive: if the daemon sends garbage like
             * "File:fw.bin|OOPS|Version:8.0", we skip "OOPS" and
             * continue parsing the rest.
             */
            FWUPMGR_ERROR("parse_update_details: malformed token '%s' (no colon)\n", token);
            token = strtok_r(NULL, "|", &saveptr);
            continue;
        }

        /*
         * Split the token into key and value by replacing ':' with '\0'.
         *
         * Before: token = "File:fw.bin"  (colon points to ':')
         * After:  key   = "File"         (token, now null-terminated at colon)
         *         value = "fw.bin"        (colon + 1, rest of original string)
         *
         * This is an in-place split -- we're modifying our work_str copy.
         */
        *colon = '\0';
        const char *key = token;
        const char *value = colon + 1;

        /*
         * Match the key to our struct fields and copy the value.
         *
         * strncpy with sizeof(field)-1 ensures we never overflow the
         * destination buffer. The struct fields are fixed-size arrays
         * (e.g., FwFileName[128], FwUrl[512]). The -1 leaves room for
         * the null terminator.
         *
         * We don't need to explicitly null-terminate because memset
         * zeroed the entire struct at the start (all bytes are '\0').
         * strncpy will write the value characters and NOT overwrite
         * the trailing '\0' that's already there from memset, as long
         * as the value is shorter than the buffer.
         *
         * KEY MAPPING:
         *   Daemon key       -> Struct field
         *   "File"           -> FwFileName
         *   "Location"       -> FwUrl (IPv4 download URL)
         *   "IPv6Location"   -> FwUrl (IPv6 fallback, used if Location is "N/A")
         *   "Version"        -> FwVersion
         *   "Reboot"         -> RebootImmediately ("true" or "false")
         *   "Delay"          -> DelayDownload ("true" or "false")
         *   "PDRI"           -> PDRIVersion (PDRI image version)
         *   "Peripherals"    -> PeripheralFirmwares (peripheral versions)
         *   "Protocol"       -> (no struct field -- skipped)
         *   "CertBundle"     -> (no struct field -- skipped)
         */
        if (strcmp(key, "File") == 0) {
            strncpy(out_details->FwFileName, value, 
                    sizeof(out_details->FwFileName) - 1);
        }
        else if (strcmp(key, "Location") == 0 || strcmp(key, "IPv6Location") == 0) {
            /*
             * Use Location if it's a real URL (not "N/A" and not empty).
             * IPv6Location is a fallback -- if Location was "N/A" but
             * IPv6Location has a URL, we use that instead.
             *
             * We don't overwrite an already-set FwUrl. If Location came
             * first and was valid, IPv6Location won't overwrite it.
             * This depends on daemon field ordering (Location before IPv6Location).
             */
            if (strcmp(value, "N/A") != 0 && value[0] != '\0') {
                strncpy(out_details->FwUrl, value,
                        sizeof(out_details->FwUrl) - 1);
            }
        }
        else if (strcmp(key, "Version") == 0) {
            strncpy(out_details->FwVersion, value,
                    sizeof(out_details->FwVersion) - 1);
        }
        else if (strcmp(key, "Reboot") == 0) {
            strncpy(out_details->RebootImmediately, value,
                    sizeof(out_details->RebootImmediately) - 1);
        }
        else if (strcmp(key, "Delay") == 0) {
            strncpy(out_details->DelayDownload, value,
                    sizeof(out_details->DelayDownload) - 1);
        }
        else if (strcmp(key, "PDRI") == 0) {
            /*
             * Skip "N/A" -- leave the field as empty string (from memset).
             * "N/A" means the daemon has no PDRI version info, which is
             * the common case for non-PDRI devices.
             */
            if (strcmp(value, "N/A") != 0) {
                strncpy(out_details->PDRIVersion, value,
                        sizeof(out_details->PDRIVersion) - 1);
            }
        }
        else if (strcmp(key, "Peripherals") == 0) {
            /* Same N/A handling as PDRI. */
            if (strcmp(value, "N/A") != 0) {
                strncpy(out_details->PeripheralFirmwares, value,
                        sizeof(out_details->PeripheralFirmwares) - 1);
            }
        }
        else if (strcmp(key, "Protocol") == 0 || strcmp(key, "CertBundle") == 0) {
            /*
             * These keys exist in the daemon's format but our UpdateDetails
             * struct doesn't have fields for them. Log and skip.
             * If a future version needs these, add struct fields and
             * copy them here.
             */
            FWUPMGR_INFO("parse_update_details: skipping field '%s'='%s'\n", key, value);
        }
        else {
            /*
             * Unknown key -- forward compatibility. If the daemon adds
             * new fields in a future version, we log and skip them
             * rather than failing. This allows the library to work with
             * newer daemons that send extra fields.
             */
            FWUPMGR_INFO("parse_update_details: unknown key '%s', ignoring\n", key);
        }

        /* Advance to next pipe-separated token. */
        token = strtok_r(NULL, "|", &saveptr);
    }

    /*
     * Free the working copy. All the data we needed has been copied
     * into out_details struct fields (which are char arrays, not pointers
     * into work_str). So freeing work_str is safe.
     */
    free(work_str);

    FWUPMGR_INFO("parse_update_details: parsed successfully\n");
    FWUPMGR_INFO("  FwFileName: %s\n", out_details->FwFileName);
    FWUPMGR_INFO("  FwVersion: %s\n", out_details->FwVersion);

    return true;
}

/* ========================================================================
 * D-BUS SIGNAL HANDLERS
 * ======================================================================== */


