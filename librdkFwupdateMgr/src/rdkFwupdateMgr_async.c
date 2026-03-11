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

/**
 * @brief Called by GLib when CheckForUpdateComplete signal arrives
 *
 * Runs in the background thread context.
 *
 *   1. Parse GVariant payload → InternalSignalData
 *   2. Dispatch to all PENDING registry entries
 *   3. Free parsed signal data
 */
static void on_check_complete_signal(GDBusConnection *conn,
                                     const gchar *sender,
                                     const gchar *object_path,
                                     const gchar *interface_name,
                                     const gchar *signal_name,
                                     GVariant *parameters,
                                     gpointer user_data)
{
    (void)conn; (void)sender; (void)object_path;
    (void)interface_name; (void)signal_name; (void)user_data;

    FWUPMGR_INFO("on_check_complete_signal: received\n");

    InternalSignalData signal_data;
    memset(&signal_data, 0, sizeof(signal_data));

    if (!internal_parse_signal_data(parameters, &signal_data)) {
        FWUPMGR_ERROR("on_check_complete_signal: parse failed\n");
        return;
    }

    dispatch_all_pending(&signal_data);

    internal_cleanup_signal_data(&signal_data);
}

/**
 * @brief Dispatch signal result to every PENDING callback
 *
 * TWO-PHASE DESIGN — avoids deadlock:
 *
 *   PHASE 1 (mutex held):
 *     Scan registry → snapshot all PENDING entries into local array.
 *     Mark each found entry as DISPATCHED.
 *     Release mutex.
 *
 *   PHASE 2 (mutex released):
 *     Build FwUpdateEventData from signal_data.
 *     Invoke each snapshot callback: callback(handle, &event_data)
 *     Re-acquire mutex briefly to reset each slot to IDLE.
 *
 * WHY RELEASE BEFORE CALLING CALLBACKS?
 *   If a callback called checkForUpdate() again, it would call
 *   internal_register_callback() which tries to lock the same mutex
 *   → deadlock. Releasing first makes re-entrant use safe.
 *
 * @param signal_data  Parsed signal payload (shared across all callbacks)
 */
static void dispatch_all_pending(const InternalSignalData *signal_data)
{
    /* Local snapshot — avoids holding mutex during callback invocations */
    typedef struct {
        UpdateEventCallback  callback;
        char                 handle_copy[256];
        int                  slot_index;
    } Snapshot;

    Snapshot snapshots[MAX_PENDING_CALLBACKS];
    int      count = 0;

    /* ---- PHASE 1: collect under mutex ---- */
    pthread_mutex_lock(&g_registry.mutex);

    for (int i = 0; i < MAX_PENDING_CALLBACKS; i++) {
        CallbackEntry *e = &g_registry.entries[i];
        if (e->state != CB_STATE_PENDING) continue;

        snapshots[count].callback   = e->callback;
        snapshots[count].slot_index = i;
        snprintf(snapshots[count].handle_copy,
                 sizeof(snapshots[count].handle_copy),
                 "%s", e->handle_key ? e->handle_key : "");

        e->state = CB_STATE_DISPATCHED;
        count++;

        FWUPMGR_INFO("dispatch_all_pending: queued handle='%s'\n",
                     e->handle_key ? e->handle_key : "(null)");
    }

    pthread_mutex_unlock(&g_registry.mutex);

    FWUPMGR_INFO("dispatch_all_pending: %d callback(s) to fire\n", count);

    /* ---- PHASE 2: invoke callbacks, no mutex held ---- */

    CheckForUpdateStatus status = internal_map_status_code(signal_data->status_code);

    /*
     * Build FwInfoData with UpdateDetails for the callback.
     * This matches the public API signature: UpdateEventCallback(const FwInfoData*)
     *
     * MEMORY MANAGEMENT:
     *   - FwInfoData is stack-allocated (valid during callback invocations)
     *   - CurrFWVersion is copied from signal_data (array, not pointer)
     *   - UpdateDetails is stack-allocated if needed
     *   - All data valid until end of this function
     */
    FwInfoData fwinfo_data;
    memset(&fwinfo_data, 0, sizeof(fwinfo_data));

    /* Copy current firmware version */
    if (signal_data->current_version) {
        strncpy(fwinfo_data.CurrFWVersion, signal_data->current_version,
                sizeof(fwinfo_data.CurrFWVersion) - 1);
        fwinfo_data.CurrFWVersion[sizeof(fwinfo_data.CurrFWVersion) - 1] = '\0';
    }

    /* Set status */
    fwinfo_data.status = status;

    /* Parse and populate UpdateDetails if firmware is available */
    UpdateDetails update_details;
    if (status == FIRMWARE_AVAILABLE && signal_data->update_details) {
        memset(&update_details, 0, sizeof(update_details));
        
        if (parse_update_details(signal_data->update_details, &update_details)) {
            /* Point FwInfoData to our stack-allocated UpdateDetails */
            fwinfo_data.UpdateDetails = &update_details;
            
            FWUPMGR_INFO("dispatch_all_pending: UpdateDetails populated\n");
            FWUPMGR_INFO("  FwFileName: %s\n", update_details.FwFileName);
            FWUPMGR_INFO("  FwVersion: %s\n", update_details.FwVersion);
        } else {
            /* Parse failed - set to NULL to indicate no details available */
            fwinfo_data.UpdateDetails = NULL;
            FWUPMGR_ERROR("dispatch_all_pending: parse_update_details failed\n");
        }
    } else {
        /* Status is not FIRMWARE_AVAILABLE or no update_details string */
        fwinfo_data.UpdateDetails = NULL;
    }

    /* Invoke all callbacks with the same FwInfoData */
    for (int i = 0; i < count; i++) {
        Snapshot *s = &snapshots[i];

        FWUPMGR_INFO("dispatch_all_pending: invoking callback for handle='%s'\n",
                     s->handle_copy);

        /*
         * Invoke callback with proper signature:
         *   UpdateEventCallback(const FwInfoData *fwinfodata)
         *
         * handle_copy is passed but callback signature doesn't use it anymore.
         * We pass it to maintain compatibility with 2-param callbacks if needed.
         */
        s->callback(&fwinfo_data);

        /* Reset slot to IDLE */
        pthread_mutex_lock(&g_registry.mutex);
        registry_reset_slot(&g_registry.entries[s->slot_index]);
        pthread_mutex_unlock(&g_registry.mutex);
    }
}

/* ========================================================================
 * REGISTRY OPERATIONS
 * ======================================================================== */

/**
 * @brief Register a pending callback keyed by handle (no user_data)
 *
 * SAME HANDLE TWICE:
 *   If the same handle is still PENDING from a previous call, its slot
 *   is overwritten. Prevents ghost callbacks accumulating.
 *
 * @param handle    App's FirmwareInterfaceHandle (will be strdup'd)
 * @param callback  App's 2-param UpdateEventCallback
 * @return true on success, false if registry is full
 */
bool internal_register_callback(FirmwareInterfaceHandle handle,
                                 UpdateEventCallback callback)
{
    pthread_mutex_lock(&g_registry.mutex);

    CallbackEntry *free_slot     = NULL;
    CallbackEntry *existing_slot = NULL;

    for (int i = 0; i < MAX_PENDING_CALLBACKS; i++) {
        CallbackEntry *e = &g_registry.entries[i];

        /* Existing pending entry for same handle → overwrite it */
        if (e->state == CB_STATE_PENDING &&
            e->handle_key != NULL &&
            strcmp(e->handle_key, handle) == 0) {
            existing_slot = e;
            break;
        }

        if (free_slot == NULL && e->state == CB_STATE_IDLE) {
            free_slot = e;
        }
    }

    CallbackEntry *target = existing_slot ? existing_slot : free_slot;

    if (target == NULL) {
        FWUPMGR_ERROR("internal_register_callback: registry full (max=%d)\n",
                      MAX_PENDING_CALLBACKS);
        pthread_mutex_unlock(&g_registry.mutex);
        return false;
    }

    if (existing_slot) {
        FWUPMGR_INFO("internal_register_callback: overwriting existing for handle='%s'\n",
                     handle);
        free(target->handle_key);
        target->handle_key = NULL;
    }

    target->handle_key      = strdup(handle);
    target->callback        = callback;
    target->state           = CB_STATE_PENDING;
    target->registered_time = time(NULL);

    pthread_mutex_unlock(&g_registry.mutex);

    FWUPMGR_INFO("internal_register_callback: registered handle='%s'\n", handle);
    return true;
}

/**
 * @brief Reset a registry slot to IDLE
 * MUST be called with registry mutex held.
 */
static void registry_reset_slot(CallbackEntry *entry)
{
    if (entry->handle_key != NULL) {
        free(entry->handle_key);
        entry->handle_key = NULL;
    }
    entry->callback        = NULL;
    entry->registered_time = 0;
    entry->state           = CB_STATE_IDLE;
}

/* ========================================================================
 * SIGNAL DATA HELPERS
 * ======================================================================== */

/**
 * @brief Parse GVariant into InternalSignalData
 *
 * Expected signature: (tiissss)
 *   t  handler_id (uint64) - identifies which client this is for
 *   i  result_code
 *   i  status_code
 *   s  current_version
 *   s  available_version
 *   s  update_details
 *   s  status_message
 */
bool internal_parse_signal_data(GVariant *parameters, InternalSignalData *out_data)
{
    if (parameters == NULL || out_data == NULL) return false;

    const gchar *sig = g_variant_get_type_string(parameters);
    if (strcmp(sig, "(tiissss)") != 0) {
        FWUPMGR_ERROR("internal_parse_signal_data: unexpected signature '%s'\n", sig);
        return false;
    }

    const gchar *cur = NULL, *avail = NULL, *details = NULL, *msg = NULL;
    guint64 handler_id = 0;
    gint32 result = 0, status = 0;

    g_variant_get(parameters, "(tiissss)",
                  &handler_id, &result, &status, &cur, &avail, &details, &msg);

    out_data->result_code       = (int32_t)result;
    out_data->status_code       = (int32_t)status;
    out_data->current_version   = cur     ? strdup(cur)     : NULL;
    out_data->available_version = avail   ? strdup(avail)   : NULL;
    out_data->update_details    = details ? strdup(details) : NULL;
    out_data->status_message    = msg     ? strdup(msg)     : NULL;

    return true;
}

void internal_cleanup_signal_data(InternalSignalData *data)
{
    free(data->current_version);
    free(data->available_version);
    free(data->update_details);
    free(data->status_message);
    memset(data, 0, sizeof(InternalSignalData));
}

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
            FWUPMGR_ERROR("internal_map_status_code: unknown %d → FIRMWARE_CHECK_ERROR\n",
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
 *
 * Called from internal_system_deinit() to free download registry resources.
 * Signal unsubscription is handled by the background thread.
 * ======================================================================== */

/**
 * @brief Cleanup download registry — called from internal_system_deinit()
 */
static void internal_dwnl_system_deinit(void)
{
    pthread_mutex_lock(&g_dwnl_registry.mutex);
    for (int i = 0; i < MAX_PENDING_CALLBACKS; i++) {
        if (g_dwnl_registry.entries[i].handle_key != NULL) {
            free(g_dwnl_registry.entries[i].handle_key);
            g_dwnl_registry.entries[i].handle_key = NULL;
        }
    }
    pthread_mutex_unlock(&g_dwnl_registry.mutex);
    pthread_mutex_destroy(&g_dwnl_registry.mutex);

    FWUPMGR_INFO("internal_dwnl_system_deinit: done\n");
}

/* ========================================================================
 * DOWNLOAD SIGNAL HANDLER
 * ======================================================================== */

/**
 * @brief Called by GLib when DownloadProgress signal arrives
 *
 * Runs in the background thread — same thread as on_check_complete_signal().
 *
 * FLOW:
 *   1. Parse GVariant payload → InternalDwnlSignalData
 *   2. Dispatch to ALL ACTIVE download callbacks
 *   3. If status is COMPLETED or ERROR → remove finished slots from registry
 */
static void on_download_progress_signal(GDBusConnection *conn,
                                        const gchar *sender,
                                        const gchar *object_path,
                                        const gchar *interface_name,
                                        const gchar *signal_name,
                                        GVariant *parameters,
                                        gpointer user_data)
{
    (void)conn; (void)sender; (void)object_path;
    (void)interface_name; (void)signal_name; (void)user_data;

    FWUPMGR_INFO("on_download_progress_signal: received\n");

    InternalDwnlSignalData signal_data;
    memset(&signal_data, 0, sizeof(signal_data));

    if (!internal_parse_dwnl_signal_data(parameters, &signal_data)) {
        FWUPMGR_ERROR("on_download_progress_signal: parse failed\n");
        return;
    }

    FWUPMGR_INFO("on_download_progress_signal: handler=%" PRIu64 " firmware='%s' progress=%u%% status='%s'\n",
                 signal_data.handler_id,
                 signal_data.firmware_name ? signal_data.firmware_name : "(null)",
                 signal_data.progress_percent,
                 signal_data.status_string ? signal_data.status_string : "(null)");

    dispatch_all_dwnl_active(&signal_data);

    // Free allocated strings from g_variant_get
    g_free(signal_data.firmware_name);
    g_free(signal_data.status_string);
    g_free(signal_data.message);
}

/**
 * @brief Dispatch DownloadProgress signal to every ACTIVE download callback
 *
 * SAME TWO-PHASE DESIGN as CheckForUpdate dispatch:
 *
 *   PHASE 1 (mutex held):
 *     Snapshot all ACTIVE entries.
 *     Do NOT change state yet — slot must stay ACTIVE for future signals.
 *     EXCEPTION: if status is COMPLETED or ERROR, mark slot for removal.
 *     Release mutex.
 *
 *   PHASE 2 (mutex released):
 *     Invoke each callback: callback(progress_per, status)
 *     Re-acquire mutex to reset completed/errored slots to IDLE.
 *
 * WHY KEEP SLOTS ACTIVE ACROSS MULTIPLE SIGNALS?
 *   Download progress fires many times: 1%, 5%, 20%...100%.
 *   If we reset to IDLE after the first callback, subsequent signals
 *   would find no registered callback and be silently dropped.
 *   The slot only becomes IDLE when the download ends.
 */
static void dispatch_all_dwnl_active(const InternalDwnlSignalData *signal_data)
{
    typedef struct {
        DownloadCallback  callback;
        char              handle_copy[256];
        int               slot_index;
        bool              is_final;   /* true if COMPLETED or ERROR — remove after firing */
    } DwnlSnapshot;

    DwnlSnapshot snapshots[MAX_PENDING_CALLBACKS];
    int          count = 0;

    DownloadStatus status = map_dwnl_status_string(signal_data->status_string);
    bool           is_final = (status == DWNL_COMPLETED || status == DWNL_ERROR);

    /* ---- PHASE 1: snapshot under mutex ---- */
    pthread_mutex_lock(&g_dwnl_registry.mutex);

    for (int i = 0; i < MAX_PENDING_CALLBACKS; i++) {
        DwnlCallbackEntry *e = &g_dwnl_registry.entries[i];
        if (e->state != DWNL_CB_STATE_ACTIVE) continue;

        snapshots[count].callback   = e->callback;
        snapshots[count].slot_index = i;
        snapshots[count].is_final   = is_final;
        snprintf(snapshots[count].handle_copy,
                 sizeof(snapshots[count].handle_copy),
                 "%s", e->handle_key ? e->handle_key : "");

        /*
         * If this is the final signal (completed/error), mark the slot
         * so we reset it to IDLE after the callback fires.
         * For in-progress signals, leave the slot ACTIVE.
         */
        count++;

        FWUPMGR_INFO("dispatch_all_dwnl_active: queued handle='%s' progress=%d%% final=%d\n",
                     e->handle_key ? e->handle_key : "(null)",
                     signal_data->progress_percent, is_final);
    }

    pthread_mutex_unlock(&g_dwnl_registry.mutex);

    FWUPMGR_INFO("dispatch_all_dwnl_active: %d callback(s) to fire\n", count);

    /* ---- PHASE 2: invoke callbacks, no mutex held ---- */
    for (int i = 0; i < count; i++) {
        DwnlSnapshot *s = &snapshots[i];

        FWUPMGR_INFO("dispatch_all_dwnl_active: invoking callback for handle='%s'\n",
                     s->handle_copy);

        /*
         * Callback signature: void fn(int progress_per, DownloadStatus status)
         * No handle parameter — matches the DownloadCallback typedef exactly.
         */
        s->callback(signal_data->progress_percent, status);

        /*
         * If download is done (COMPLETED or ERROR), reset slot to IDLE.
         * This frees the handle_key and makes the slot available for reuse.
         * For in-progress signals, leave slot ACTIVE for next signal.
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

/**
 * @brief Register a download callback keyed by handle
 *
 * Sets slot state to ACTIVE. Slot will receive ALL subsequent
 * DownloadProgress signals until DWNL_COMPLETED or DWNL_ERROR.
 *
 * SAME HANDLE TWICE:
 *   Overwrites existing ACTIVE slot for the same handle.
 *   Prevents stale callbacks from a previous download session.
 *
 * @param handle    App's FirmwareInterfaceHandle (strdup'd internally)
 * @param callback  App's DownloadCallback
 * @return true on success, false if registry full
 */
bool internal_dwnl_register_callback(FirmwareInterfaceHandle handle,
                                      DownloadCallback callback)
{
    pthread_mutex_lock(&g_dwnl_registry.mutex);

    DwnlCallbackEntry *free_slot     = NULL;
    DwnlCallbackEntry *existing_slot = NULL;

    for (int i = 0; i < MAX_PENDING_CALLBACKS; i++) {
        DwnlCallbackEntry *e = &g_dwnl_registry.entries[i];

        if (e->state == DWNL_CB_STATE_ACTIVE &&
            e->handle_key != NULL &&
            strcmp(e->handle_key, handle) == 0) {
            existing_slot = e;
            break;
        }

        if (free_slot == NULL && e->state == DWNL_CB_STATE_IDLE) {
            free_slot = e;
        }
    }

    DwnlCallbackEntry *target = existing_slot ? existing_slot : free_slot;

    if (target == NULL) {
        FWUPMGR_ERROR("internal_dwnl_register_callback: registry full (max=%d)\n",
                      MAX_PENDING_CALLBACKS);
        pthread_mutex_unlock(&g_dwnl_registry.mutex);
        return false;
    }

    if (existing_slot) {
        FWUPMGR_INFO("internal_dwnl_register_callback: overwriting existing for handle='%s'\n",
                     handle);
        free(target->handle_key);
        target->handle_key = NULL;
    }

    target->handle_key      = strdup(handle);
    target->callback        = callback;
    target->state           = DWNL_CB_STATE_ACTIVE;
    target->registered_time = time(NULL);

    pthread_mutex_unlock(&g_dwnl_registry.mutex);

    FWUPMGR_INFO("internal_dwnl_register_callback: registered handle='%s'\n", handle);
    return true;
}

/**
 * @brief Reset a download registry slot to IDLE
 * MUST be called with g_dwnl_registry.mutex held.
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

/**
 * @brief Parse GVariant DownloadProgress signal payload
 *
 * Expected GVariant signature: (ii)
 *   i  progress_percent  (0–100)
 *   i  status_code       (maps to DownloadStatus)
 */
bool internal_parse_dwnl_signal_data(GVariant *parameters,
                                      InternalDwnlSignalData *out_data)
{
    if (parameters == NULL || out_data == NULL) return false;

    const gchar *sig = g_variant_get_type_string(parameters);
    if (strcmp(sig, "(tsuss)") != 0) {
        FWUPMGR_ERROR("internal_parse_dwnl_signal_data: unexpected signature '%s' (expected '(tsuss)')\n", sig);
        return false;
    }

    guint64  handler_id = 0;
    gchar   *firmware_name = NULL;
    guint32  progress = 0;
    gchar   *status_str = NULL;
    gchar   *message_str = NULL;

    g_variant_get(parameters, "(tsuss)", 
                  &handler_id, 
                  &firmware_name, 
                  &progress, 
                  &status_str, 
                  &message_str);

    out_data->handler_id      = handler_id;
    out_data->firmware_name   = firmware_name;   // Caller must g_free
    out_data->progress_percent = progress;
    out_data->status_string   = status_str;      // Caller must g_free
    out_data->message         = message_str;     // Caller must g_free

    return true;
}

/**
 * @brief Map status string to DownloadStatus enum
 */
DownloadStatus internal_map_dwnl_status_code(int32_t status_code)
{
    // This function is kept for backward compatibility but now receives
    // a mapped value. The actual mapping happens in the caller.
    switch (status_code) {
        case 0:  return DWNL_IN_PROGRESS;
        case 1:  return DWNL_COMPLETED;
        case 2:  return DWNL_ERROR;
        default:
            FWUPMGR_ERROR("internal_map_dwnl_status_code: unknown %d → DWNL_ERROR\n",
                          status_code);
            return DWNL_ERROR;
    }
}

/**
 * @brief Map status string from daemon to DownloadStatus enum
 */
static DownloadStatus map_dwnl_status_string(const char *status_str)
{
    if (status_str == NULL) {
        return DWNL_ERROR;
    }

    if (strcmp(status_str, "INPROGRESS") == 0 || strcmp(status_str, "NOTSTARTED") == 0) {
        return DWNL_IN_PROGRESS;
    } else if (strcmp(status_str, "COMPLETED") == 0) {
        return DWNL_COMPLETED;
    } else if (strcmp(status_str, "ERROR") == 0 || strcmp(status_str, "DWNL_ERROR") == 0) {
        return DWNL_ERROR;
    }

    FWUPMGR_ERROR("map_dwnl_status_string: unknown status '%s' → DWNL_ERROR\n", status_str);
    return DWNL_ERROR;
}

/* ========================================================================
 * UPDATE FIRMWARE — INTERNAL ENGINE
 * ========================================================================
 *
 * Mirror of the DownloadFirmware engine above.
 * Same registry pattern, same two-phase dispatch, same lifecycle.
 *
 * Signal:  UpdateProgress (ii) — progress_percent, status_code
 * Registry slot: ACTIVE until UPDATE_COMPLETED or UPDATE_ERROR, then IDLE.
 * ======================================================================== */

/* ---- Forward declarations for helper functions ---- */
static void dispatch_all_update_active(const InternalUpdateSignalData *signal_data);
static void update_registry_reset_slot(UpdateCbEntry *entry);

/* ========================================================================
 * UPDATE SUBSYSTEM LIFECYCLE
 * ======================================================================== */

/**
 * @brief Cleanup update registry — frees all strdup'd handle_key strings
 *
 * Called from internal_system_deinit(). Signal unsubscription is handled
 * by the background thread.
 */
static void internal_update_system_deinit(void)
{
    pthread_mutex_lock(&g_update_registry.mutex);
    for (int i = 0; i < MAX_PENDING_CALLBACKS; i++) {
        if (g_update_registry.entries[i].handle_key != NULL) {
            free(g_update_registry.entries[i].handle_key);
            g_update_registry.entries[i].handle_key = NULL;
        }
    }
    pthread_mutex_unlock(&g_update_registry.mutex);
    pthread_mutex_destroy(&g_update_registry.mutex);

    FWUPMGR_INFO("internal_update_system_deinit: done\n");
}

/* ========================================================================
 * UPDATE SIGNAL HANDLER
 * ======================================================================== */

/**
 * @brief Called by GLib when UpdateProgress signal arrives
 *
 * Runs in background thread. Parses payload and dispatches to all
 * ACTIVE update callbacks.
 */
static void on_update_progress_signal(GDBusConnection *conn,
                                      const gchar *sender,
                                      const gchar *object_path,
                                      const gchar *interface_name,
                                      const gchar *signal_name,
                                      GVariant *parameters,
                                      gpointer user_data)
{
    (void)conn; (void)sender; (void)object_path;
    (void)interface_name; (void)signal_name; (void)user_data;

    FWUPMGR_INFO("on_update_progress_signal: received\n");

    InternalUpdateSignalData signal_data;
    memset(&signal_data, 0, sizeof(signal_data));

    if (!internal_parse_update_signal_data(parameters, &signal_data)) {
        FWUPMGR_ERROR("on_update_progress_signal: parse failed\n");
        return;
    }

    FWUPMGR_INFO("on_update_progress_signal: handler=%" PRIu64 " firmware='%s' progress=%d%% status=%d\n",
                 signal_data.handler_id,
                 signal_data.firmware_name ? signal_data.firmware_name : "(null)",
                 signal_data.progress_percent,
                 signal_data.status_code);

    dispatch_all_update_active(&signal_data);

    // Free allocated strings from g_variant_get
    g_free(signal_data.firmware_name);
    g_free(signal_data.message);
}

/**
 * @brief Dispatch UpdateProgress signal to every ACTIVE update callback
 *
 * TWO-PHASE DESIGN (identical to download dispatch):
 *
 *   PHASE 1 (mutex held):
 *     Snapshot all ACTIVE entries.
 *     Mark is_final=true only if status is COMPLETED or ERROR.
 *     Release mutex.
 *
 *   PHASE 2 (mutex released):
 *     Invoke callback(progress_per, status) for each snapshot.
 *     If is_final: re-acquire mutex, reset slot to IDLE.
 *     If in-progress: leave slot ACTIVE for next signal.
 */
static void dispatch_all_update_active(const InternalUpdateSignalData *signal_data)
{
    typedef struct {
        UpdateCallback  callback;
        char            handle_copy[256];
        int             slot_index;
        bool            is_final;
    } UpdateSnapshot;

    UpdateSnapshot snapshots[MAX_PENDING_CALLBACKS];
    int            count = 0;

    UpdateStatus status   = internal_map_update_status_code(signal_data->status_code);
    bool         is_final = (status == UPDATE_COMPLETED || status == UPDATE_ERROR);

    /* ---- PHASE 1: snapshot under mutex ---- */
    pthread_mutex_lock(&g_update_registry.mutex);

    for (int i = 0; i < MAX_PENDING_CALLBACKS; i++) {
        UpdateCbEntry *e = &g_update_registry.entries[i];
        if (e->state != UPDATE_CB_STATE_ACTIVE) continue;

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

    pthread_mutex_unlock(&g_update_registry.mutex);

    FWUPMGR_INFO("dispatch_all_update_active: %d callback(s) to fire\n", count);

    /* ---- PHASE 2: invoke callbacks, no mutex held ---- */
    for (int i = 0; i < count; i++) {
        UpdateSnapshot *s = &snapshots[i];

        FWUPMGR_INFO("dispatch_all_update_active: invoking callback "
                     "for handle='%s'\n", s->handle_copy);

        /*
         * Callback signature: void fn(int progress_per, UpdateStatus status)
         * Matches UpdateCallback typedef exactly.
         */
        s->callback(signal_data->progress_percent, status);

        /*
         * If this was the final signal (COMPLETED or ERROR), reset slot to IDLE.
         * For in-progress signals, leave slot ACTIVE for the next signal.
         */
        if (s->is_final) {
            pthread_mutex_lock(&g_update_registry.mutex);
            update_registry_reset_slot(&g_update_registry.entries[s->slot_index]);
            pthread_mutex_unlock(&g_update_registry.mutex);

            FWUPMGR_INFO("dispatch_all_update_active: slot %d → IDLE "
                         "(update ended)\n", s->slot_index);
        }
    }
}

/* ========================================================================
 * UPDATE REGISTRY OPERATIONS
 * ======================================================================== */

/**
 * @brief Register an update callback keyed by handle
 *
 * Sets slot to ACTIVE. Slot receives ALL subsequent UpdateProgress signals
 * until UPDATE_COMPLETED or UPDATE_ERROR resets it to IDLE.
 *
 * SAME HANDLE TWICE:
 *   Overwrites existing ACTIVE slot for the same handle.
 */
bool internal_update_register_callback(FirmwareInterfaceHandle handle,
                                        UpdateCallback callback)
{
    pthread_mutex_lock(&g_update_registry.mutex);

    UpdateCbEntry *free_slot     = NULL;
    UpdateCbEntry *existing_slot = NULL;

    for (int i = 0; i < MAX_PENDING_CALLBACKS; i++) {
        UpdateCbEntry *e = &g_update_registry.entries[i];

        if (e->state == UPDATE_CB_STATE_ACTIVE &&
            e->handle_key != NULL &&
            strcmp(e->handle_key, handle) == 0) {
            existing_slot = e;
            break;
        }

        if (free_slot == NULL && e->state == UPDATE_CB_STATE_IDLE) {
            free_slot = e;
        }
    }

    UpdateCbEntry *target = existing_slot ? existing_slot : free_slot;

    if (target == NULL) {
        FWUPMGR_ERROR("internal_update_register_callback: registry full (max=%d)\n",
                      MAX_PENDING_CALLBACKS);
        pthread_mutex_unlock(&g_update_registry.mutex);
        return false;
    }

    if (existing_slot) {
        FWUPMGR_INFO("internal_update_register_callback: "
                     "overwriting existing for handle='%s'\n", handle);
        free(target->handle_key);
        target->handle_key = NULL;
    }

    target->handle_key      = strdup(handle);
    target->callback        = callback;
    target->state           = UPDATE_CB_STATE_ACTIVE;
    target->registered_time = time(NULL);

    pthread_mutex_unlock(&g_update_registry.mutex);

    FWUPMGR_INFO("internal_update_register_callback: registered handle='%s'\n",
                 handle);
    return true;
}

/**
 * @brief Reset an update registry slot to IDLE
 * MUST be called with g_update_registry.mutex held.
 */
static void update_registry_reset_slot(UpdateCbEntry *entry)
{
    if (entry->handle_key != NULL) {
        free(entry->handle_key);
        entry->handle_key = NULL;
    }
    entry->callback        = NULL;
    entry->registered_time = 0;
    entry->state           = UPDATE_CB_STATE_IDLE;
}

/* ========================================================================
 * UPDATE SIGNAL DATA HELPERS
 * ======================================================================== */

/**
 * @brief Parse GVariant UpdateProgress payload
 *
 * Expected GVariant signature: (ii)
 *   i  progress_percent  (0–100)
 *   i  status_code       (maps to UpdateStatus)
 */
bool internal_parse_update_signal_data(GVariant *parameters,
                                        InternalUpdateSignalData *out_data)
{
    if (parameters == NULL || out_data == NULL) return false;

    const gchar *sig = g_variant_get_type_string(parameters);
    if (strcmp(sig, "(tsiis)") != 0) {
        FWUPMGR_ERROR("internal_parse_update_signal_data: "
                      "unexpected signature '%s' (expected '(tsiis)')\n", sig);
        return false;
    }

    guint64  handler_id = 0;
    gchar   *firmware_name = NULL;
    gint32   progress = 0;
    gint32   status = 0;
    gchar   *message_str = NULL;

    g_variant_get(parameters, "(tsiis)", 
                  &handler_id, 
                  &firmware_name, 
                  &progress, 
                  &status, 
                  &message_str);

    out_data->handler_id       = handler_id;
    out_data->firmware_name    = firmware_name;   // Caller must g_free
    out_data->progress_percent = progress;
    out_data->status_code      = status;
    out_data->message          = message_str;     // Caller must g_free

    return true;
}

/**
 * @brief Map raw integer to UpdateStatus enum
 */
UpdateStatus internal_map_update_status_code(int32_t status_code)
{
    switch (status_code) {
        case 0:  return UPDATE_IN_PROGRESS;
        case 1:  return UPDATE_COMPLETED;
        case 2:  return UPDATE_ERROR;
        default:
            FWUPMGR_ERROR("internal_map_update_status_code: "
                          "unknown %d → UPDATE_ERROR\n", status_code);
            return UPDATE_ERROR;
    }
}

/* ========================================================================
 * UPDATE DETAILS PARSING
 * ======================================================================== */

/**
 * @brief Parse update_details string into UpdateDetails structure
 *
 * The update_details string from the daemon is a comma-separated key:value format:
 *   "FwFileName:filename.bin,FwUrl:https://...,FwVersion:1.0,..."
 *
 * This function safely parses it and populates the UpdateDetails structure.
 *
 * @param update_details_str   Comma-separated string from daemon (may be NULL)
 * @param out_details          Output UpdateDetails structure (must be allocated)
 * @return true if parsing succeeded (even if string was NULL/empty),
 *         false only on critical errors
 *
 * Thread safety: Safe - operates on local data only
 * Memory: out_details is caller-allocated, this function fills arrays
 */
static bool parse_update_details(const char *update_details_str,
                                   UpdateDetails *out_details)
{
    if (out_details == NULL) {
        FWUPMGR_ERROR("parse_update_details: out_details is NULL\n");
        return false;
    }

    /* Zero-initialize the output structure */
    memset(out_details, 0, sizeof(UpdateDetails));

    /* Empty or NULL input is valid - just means no details available */
    if (update_details_str == NULL || update_details_str[0] == '\0') {
        FWUPMGR_INFO("parse_update_details: empty input, returning zeroed structure\n");
        return true;
    }

    FWUPMGR_INFO("parse_update_details: parsing '%s'\n", update_details_str);

    /* Make a working copy since strtok modifies the string */
    char *work_str = strdup(update_details_str);
    if (work_str == NULL) {
        FWUPMGR_ERROR("parse_update_details: strdup failed\n");
        return false;
    }

    /* Parse pipe-separated key:value pairs (daemon uses | not ,) */
    char *saveptr = NULL;
    char *token = strtok_r(work_str, "|", &saveptr);

    while (token != NULL) {
        /* Split on ':' to get key and value */
        char *colon = strchr(token, ':');
        if (colon == NULL) {
            /* Malformed token, skip it */
            FWUPMGR_ERROR("parse_update_details: malformed token '%s' (no colon)\n", token);
            token = strtok_r(NULL, "|", &saveptr);
            continue;
        }

        /* Null-terminate the key and get the value */
        *colon = '\0';
        const char *key = token;
        const char *value = colon + 1;

        /* Match keys and copy values into appropriate fields
         * Daemon uses: File, Location, Version, Reboot, Delay, PDRI, Peripherals
         * We map them to our struct fields */
        if (strcmp(key, "File") == 0) {
            strncpy(out_details->FwFileName, value, 
                    sizeof(out_details->FwFileName) - 1);
        }
        else if (strcmp(key, "Location") == 0 || strcmp(key, "IPv6Location") == 0) {
            /* Use Location if not empty, fallback to IPv6Location */
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
            if (strcmp(value, "N/A") != 0) {
                strncpy(out_details->PDRIVersion, value,
                        sizeof(out_details->PDRIVersion) - 1);
            }
        }
        else if (strcmp(key, "Peripherals") == 0) {
            if (strcmp(value, "N/A") != 0) {
                strncpy(out_details->PeripheralFirmwares, value,
                        sizeof(out_details->PeripheralFirmwares) - 1);
            }
        }
        else if (strcmp(key, "Protocol") == 0 || strcmp(key, "CertBundle") == 0) {
            /* These fields exist in daemon format but not in our struct - ignore */
            FWUPMGR_INFO("parse_update_details: skipping field '%s'='%s'\n", key, value);
        }
        else {
            /* Unknown key - log but don't fail */
            FWUPMGR_INFO("parse_update_details: unknown key '%s', ignoring\n", key);
        }

        token = strtok_r(NULL, "|", &saveptr);
    }

    free(work_str);

    FWUPMGR_INFO("parse_update_details: parsed successfully\n");
    FWUPMGR_INFO("  FwFileName: %s\n", out_details->FwFileName);
    FWUPMGR_INFO("  FwVersion: %s\n", out_details->FwVersion);

    return true;
}

/* ========================================================================
 * D-BUS SIGNAL HANDLERS
 * ======================================================================== */


