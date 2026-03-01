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
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

/* ========================================================================
 * GLOBAL STATE
 * ======================================================================== */

static CallbackRegistry g_registry;
static BackgroundThread g_bg_thread;

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

static void  dispatch_all_pending(const InternalSignalData *signal_data);
static void  registry_reset_slot(CallbackEntry *entry);

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

    /* Free any leftover handle_key strings */
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

    FWUPMGR_INFO("background_thread: subscribed (id=%u), entering event loop\n",
                 g_bg_thread.subscription_id);

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
     * FwUpdateEventData is built once and shared (read-only) across
     * all callback invocations. Strings point into signal_data which
     * stays valid until internal_cleanup_signal_data() at the end of
     * on_check_complete_signal().
     */
    FwUpdateEventData event_data = {
        .status            = status,
        .current_version   = signal_data->current_version,
        .available_version = signal_data->available_version,
        .status_message    = signal_data->status_message,
        .update_available  = (status == FIRMWARE_AVAILABLE)
    };

    for (int i = 0; i < count; i++) {
        Snapshot *s = &snapshots[i];

        FWUPMGR_INFO("dispatch_all_pending: invoking callback for handle='%s'\n",
                     s->handle_copy);

        /*
         * Two-parameter call — aligned to the required API:
         *   UpdateEventCallback(FirmwareInterfaceHandle, const FwUpdateEventData*)
         *
         * handle_copy is a stack-local copy of the handle string.
         * It is valid for the duration of this callback invocation.
         */
        s->callback(s->handle_copy, &event_data);

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
 * Expected signature: (iissss)
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
    if (strcmp(sig, "(iissss)") != 0) {
        FWUPMGR_ERROR("internal_parse_signal_data: unexpected signature '%s'\n", sig);
        return false;
    }

    const gchar *cur = NULL, *avail = NULL, *details = NULL, *msg = NULL;
    gint32 result = 0, status = 0;

    g_variant_get(parameters, "(iissss)",
                  &result, &status, &cur, &avail, &details, &msg);

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

/* ---- Download registry global ---- */
static DwnlCallbackRegistry g_dwnl_registry;

/* ---- Forward declarations ---- */
static void on_download_progress_signal(GDBusConnection *conn,
                                        const gchar *sender,
                                        const gchar *object_path,
                                        const gchar *interface_name,
                                        const gchar *signal_name,
                                        GVariant *parameters,
                                        gpointer user_data);
static void dispatch_all_dwnl_active(const InternalDwnlSignalData *signal_data);
static void dwnl_registry_reset_slot(DwnlCallbackEntry *entry);

/* ========================================================================
 * DOWNLOAD REGISTRY INIT — called from internal_system_init()
 *
 * The download registry is initialised alongside the CheckForUpdate
 * registry and uses the SAME background thread and GLib event loop.
 * We only add a second D-Bus signal subscription.
 * ======================================================================== */

/**
 * @brief Initialize download registry and subscribe to DownloadProgress signal
 *
 * Called from internal_system_init() after the background thread is ready.
 * The background thread's connection and context already exist at this point.
 *
 * @return 0 on success, -1 on error
 */
int internal_dwnl_system_init(void)
{
    FWUPMGR_INFO("internal_dwnl_system_init: begin\n");

    memset(&g_dwnl_registry, 0, sizeof(g_dwnl_registry));
    if (pthread_mutex_init(&g_dwnl_registry.mutex, NULL) != 0) {
        FWUPMGR_ERROR("internal_dwnl_system_init: mutex init failed\n");
        return -1;
    }
    g_dwnl_registry.initialized = true;

    /*
     * Subscribe to DownloadProgress signal on the SAME connection and
     * context as the CheckForUpdate signal subscription.
     * g_bg_thread.connection is already open from internal_system_init().
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

    FWUPMGR_INFO("internal_dwnl_system_init: subscribed to DownloadProgress (id=%u)\n",
                 dwnl_sub_id);

    return 0;
}

/**
 * @brief Cleanup download registry — called from internal_system_deinit()
 */
void internal_dwnl_system_deinit(void)
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

    FWUPMGR_INFO("on_download_progress_signal: progress=%d%% status=%d\n",
                 signal_data.progress_percent, signal_data.status_code);

    dispatch_all_dwnl_active(&signal_data);
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

    DownloadStatus status = internal_map_dwnl_status_code(signal_data->status_code);
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
    if (strcmp(sig, "(ii)") != 0) {
        FWUPMGR_ERROR("internal_parse_dwnl_signal_data: unexpected signature '%s'\n", sig);
        return false;
    }

    gint32 progress = 0, status = 0;
    g_variant_get(parameters, "(ii)", &progress, &status);

    out_data->progress_percent = (int32_t)progress;
    out_data->status_code      = (int32_t)status;

    return true;
}

/**
 * @brief Map raw integer to DownloadStatus enum
 */
DownloadStatus internal_map_dwnl_status_code(int32_t status_code)
{
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

/* ---- Update registry global ---- */
static UpdateCbRegistry g_update_registry;

/* ---- Forward declarations ---- */
static void on_update_progress_signal(GDBusConnection *conn,
                                      const gchar *sender,
                                      const gchar *object_path,
                                      const gchar *interface_name,
                                      const gchar *signal_name,
                                      GVariant *parameters,
                                      gpointer user_data);
static void dispatch_all_update_active(const InternalUpdateSignalData *signal_data);
static void update_registry_reset_slot(UpdateCbEntry *entry);

/* ========================================================================
 * UPDATE SUBSYSTEM LIFECYCLE
 * ======================================================================== */

/**
 * @brief Initialize update registry and subscribe to UpdateProgress signal
 *
 * Reuses g_bg_thread.connection — no new thread or connection needed.
 * Called from internal_system_init() after background thread is running.
 */
int internal_update_system_init(void)
{
    FWUPMGR_INFO("internal_update_system_init: begin\n");

    memset(&g_update_registry, 0, sizeof(g_update_registry));
    if (pthread_mutex_init(&g_update_registry.mutex, NULL) != 0) {
        FWUPMGR_ERROR("internal_update_system_init: mutex init failed\n");
        return -1;
    }
    g_update_registry.initialized = true;

    guint update_sub_id = g_dbus_connection_signal_subscribe(
        g_bg_thread.connection,
        NULL,                           /* sender: any                        */
        DBUS_INTERFACE_NAME,            /* interface                          */
        DBUS_SIGNAL_UPDATE_PROGRESS,    /* signal: UpdateProgress             */
        DBUS_OBJECT_PATH,               /* object path                        */
        NULL,                           /* arg0 filter: none                  */
        G_DBUS_SIGNAL_FLAGS_NONE,
        on_update_progress_signal,      /* handler                            */
        NULL,
        NULL
    );

    FWUPMGR_INFO("internal_update_system_init: subscribed to UpdateProgress (id=%u)\n",
                 update_sub_id);
    return 0;
}

/**
 * @brief Cleanup update registry — frees all strdup'd handle_key strings
 */
void internal_update_system_deinit(void)
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

    FWUPMGR_INFO("on_update_progress_signal: progress=%d%% status=%d\n",
                 signal_data.progress_percent, signal_data.status_code);

    dispatch_all_update_active(&signal_data);
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
    if (strcmp(sig, "(ii)") != 0) {
        FWUPMGR_ERROR("internal_parse_update_signal_data: "
                      "unexpected signature '%s'\n", sig);
        return false;
    }

    gint32 progress = 0, status = 0;
    g_variant_get(parameters, "(ii)", &progress, &status);

    out_data->progress_percent = (int32_t)progress;
    out_data->status_code      = (int32_t)status;

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


