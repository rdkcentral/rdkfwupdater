/*
 * Copyright 2025 Comcast Cable Communications Management, LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * testClient.c - RDK Firmware Update Manager D-Bus Test Client
 * test client for validating firmware update daemon functionality.
 * Supports all D-Bus API methods: RegisterProcess, CheckForUpdate,
 * DownloadFirmware, UpdateFirmware, and UnregisterProcess.
 *
 * Build:
 *   gcc -o testClient testClient.c $(pkg-config --cflags --libs gio-2.0 glib-2.0)
 *
 * Usage:
 *   ./testClient --help
 *   ./testClient --list
 *   ./testClient <process_name> <lib_version> <test_mode> [arguments...]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <gio/gio.h>

#define DBUS_SERVICE_NAME    "org.rdkfwupdater.Service"
#define DBUS_OBJECT_PATH     "/org/rdkfwupdater/Service"
#define DBUS_INTERFACE_NAME  "org.rdkfwupdater.Interface"

#define XCONF_CACHE_FILE     "/tmp/xconf_response_thunder.txt"
#define DEFAULT_FIRMWARE_DIR "/opt/CDL"
#define SIGNAL_TIMEOUT_SEC   60
#define FLASH_TIMEOUT_SEC    120

typedef enum { LOG_ERROR_LVL = 0, LOG_WARN_LVL, LOG_INFO_LVL, LOG_DEBUG_LVL } LogLevel;
static LogLevel g_log_level = LOG_INFO_LVL;

static const char* get_timestamp(void) {
    static char buf[32];
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    if (t) strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", t);
    else snprintf(buf, sizeof(buf), "unknown");
    return buf;
}

#define LOG_ERROR(fmt, ...) fprintf(stderr, "[%s] TC_ERROR: " fmt "\n", get_timestamp(), ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  do { if (g_log_level >= LOG_WARN_LVL) printf("[%s] TC_WARN:  " fmt "\n", get_timestamp(), ##__VA_ARGS__); } while(0)
#define LOG_INFO(fmt, ...)  do { if (g_log_level >= LOG_INFO_LVL) printf("[%s] TC_INFO:  " fmt "\n", get_timestamp(), ##__VA_ARGS__); } while(0)
#define LOG_DEBUG(fmt, ...) do { if (g_log_level >= LOG_DEBUG_LVL) printf("[%s] TC_DEBUG: " fmt "\n", get_timestamp(), ##__VA_ARGS__); } while(0)

typedef struct {
    GDBusConnection *connection;
    gchar *process_name;
    gchar *lib_version;
    guint64 handler_id;
    gboolean is_registered;
    GMainLoop *loop;
    gboolean check_complete, check_success;
    gint check_result_code;
    gboolean download_complete, download_success;
    gint download_progress;
    gboolean flash_complete, flash_success;
    gint flash_progress;
    guint signal_count;
} TestClientContext;

static void on_check_complete(GDBusConnection *c, const gchar *s, const gchar *o, const gchar *i, const gchar *n, GVariant *p, gpointer u) {
    TestClientContext *ctx = (TestClientContext*)u;
    gchar *hid = NULL; gint rc = 0; gchar *cv = NULL, *av = NULL, *d = NULL, *st = NULL;
    if (!ctx || !p) return;
    g_variant_get(p, "(sissss)", &hid, &rc, &cv, &av, &d, &st);
    LOG_INFO("Signal: CheckForUpdateComplete, handler=%s, result=%d", hid ? hid : "", rc);
    gchar *exp = g_strdup_printf("%" G_GUINT64_FORMAT, ctx->handler_id);
    if (hid && g_strcmp0(hid, exp) == 0) {
        ctx->check_complete = TRUE; ctx->check_result_code = rc;
        ctx->check_success = (rc == 0 || rc == 1); ctx->signal_count++;
        if (ctx->loop && g_main_loop_is_running(ctx->loop)) g_main_loop_quit(ctx->loop);
    }
    g_free(exp); g_free(hid); g_free(cv); g_free(av); g_free(d); g_free(st);
}

static void on_download_progress(GDBusConnection *c, const gchar *s, const gchar *o, const gchar *i, const gchar *n, GVariant *p, gpointer u) {
    TestClientContext *ctx = (TestClientContext*)u;
    guint64 hid = 0; gchar *fn = NULL; guint32 prog = 0; gchar *st = NULL, *msg = NULL;
    if (!ctx || !p) return;
    g_variant_get(p, "(tsuss)", &hid, &fn, &prog, &st, &msg);
    LOG_INFO("Signal: DownloadProgress, handler=%" G_GUINT64_FORMAT ", progress=%u%%, status=%s", hid, prog, st ? st : "");
    if (ctx->handler_id == hid) {
        ctx->download_progress = (gint)prog; ctx->signal_count++;
        gboolean done = (prog >= 100) || (st && g_strcmp0(st, "COMPLETED") == 0);
        gboolean err = (st && g_strcmp0(st, "DWNL_ERROR") == 0);
        if (done || err) {
            ctx->download_complete = TRUE; ctx->download_success = done && !err;
            if (ctx->loop && g_main_loop_is_running(ctx->loop)) g_main_loop_quit(ctx->loop);
        }
    }
    g_free(fn); g_free(st); g_free(msg);
}

static void on_update_progress(GDBusConnection *c, const gchar *s, const gchar *o, const gchar *i, const gchar *n, GVariant *p, gpointer u) {
    TestClientContext *ctx = (TestClientContext*)u;
    guint64 hid = 0; gchar *fn = NULL; gint32 prog = 0, sc = 0; gchar *msg = NULL;
    if (!ctx || !p) return;
    g_variant_get(p, "(tsiis)", &hid, &fn, &prog, &sc, &msg);
    LOG_INFO("Signal: UpdateProgress, handler=%" G_GUINT64_FORMAT ", progress=%d%%, status=%d", hid, prog, sc);
    if (ctx->handler_id == hid) {
        ctx->flash_progress = prog; ctx->signal_count++;
        gboolean done = (sc == 1) || (prog == 100);
        gboolean err = (sc == 2) || (prog < 0);
        if (done || err) {
            ctx->flash_complete = TRUE; ctx->flash_success = done && !err;
            if (ctx->loop && g_main_loop_is_running(ctx->loop)) g_main_loop_quit(ctx->loop);
        }
    }
    g_free(fn); g_free(msg);
}

static gboolean on_timeout(gpointer u) {
    TestClientContext *ctx = (TestClientContext*)u;
    LOG_WARN("Operation timed out");
    if (ctx) {
        ctx->download_complete = ctx->flash_complete = ctx->check_complete = TRUE;
        ctx->download_success = ctx->flash_success = ctx->check_success = FALSE;
        if (ctx->loop && g_main_loop_is_running(ctx->loop)) g_main_loop_quit(ctx->loop);
    }
    return G_SOURCE_REMOVE;
}

static TestClientContext* client_create(const gchar *pname, const gchar *ver) {
    GError *err = NULL;
    TestClientContext *ctx = g_malloc0(sizeof(TestClientContext));
    if (!ctx) { LOG_ERROR("Memory allocation failed"); return NULL; }
    ctx->connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &err);
    if (!ctx->connection) { LOG_ERROR("D-Bus connection failed: %s", err->message); g_error_free(err); g_free(ctx); return NULL; }
    ctx->process_name = g_strdup(pname);
    ctx->lib_version = g_strdup(ver);
    g_dbus_connection_signal_subscribe(ctx->connection, DBUS_SERVICE_NAME, DBUS_INTERFACE_NAME, "CheckForUpdateComplete", DBUS_OBJECT_PATH, NULL, G_DBUS_SIGNAL_FLAGS_NONE, on_check_complete, ctx, NULL);
    g_dbus_connection_signal_subscribe(ctx->connection, DBUS_SERVICE_NAME, DBUS_INTERFACE_NAME, "DownloadProgress", DBUS_OBJECT_PATH, NULL, G_DBUS_SIGNAL_FLAGS_NONE, on_download_progress, ctx, NULL);
    g_dbus_connection_signal_subscribe(ctx->connection, DBUS_SERVICE_NAME, DBUS_INTERFACE_NAME, "UpdateProgress", DBUS_OBJECT_PATH, NULL, G_DBUS_SIGNAL_FLAGS_NONE, on_update_progress, ctx, NULL);
    LOG_INFO("Test client initialized");
    return ctx;
}

static void client_destroy(TestClientContext *ctx) {
    if (!ctx) return;
    if (ctx->is_registered) {
        GVariant *r = g_dbus_connection_call_sync(ctx->connection, DBUS_SERVICE_NAME, DBUS_OBJECT_PATH, DBUS_INTERFACE_NAME, "UnregisterProcess", g_variant_new("(t)", ctx->handler_id), G_VARIANT_TYPE("(b)"), G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL);
        if (r) g_variant_unref(r);
    }
    if (ctx->loop) { if (g_main_loop_is_running(ctx->loop)) g_main_loop_quit(ctx->loop); g_main_loop_unref(ctx->loop); }
    if (ctx->connection) g_object_unref(ctx->connection);
    g_free(ctx->process_name); g_free(ctx->lib_version); g_free(ctx);
    LOG_INFO("Test client destroyed");
}

static gboolean client_register(TestClientContext *ctx) {
    GError *err = NULL;
    LOG_INFO("Registering process: %s (version: %s)", ctx->process_name, ctx->lib_version);
    GVariant *r = g_dbus_connection_call_sync(ctx->connection, DBUS_SERVICE_NAME, DBUS_OBJECT_PATH, DBUS_INTERFACE_NAME, "RegisterProcess", g_variant_new("(ss)", ctx->process_name, ctx->lib_version), G_VARIANT_TYPE("(t)"), G_DBUS_CALL_FLAGS_NONE, -1, NULL, &err);
    if (r) { g_variant_get(r, "(t)", &ctx->handler_id); ctx->is_registered = TRUE; g_variant_unref(r); LOG_INFO("Registered, handler ID: %" G_GUINT64_FORMAT, ctx->handler_id); return TRUE; }
    LOG_ERROR("Registration failed: %s", err->message); g_error_free(err); return FALSE;
}

static gboolean client_unregister(TestClientContext *ctx) {
    if (!ctx || !ctx->is_registered) return TRUE;
    GError *err = NULL; gboolean ok = FALSE;
    GVariant *r = g_dbus_connection_call_sync(ctx->connection, DBUS_SERVICE_NAME, DBUS_OBJECT_PATH, DBUS_INTERFACE_NAME, "UnregisterProcess", g_variant_new("(t)", ctx->handler_id), G_VARIANT_TYPE("(b)"), G_DBUS_CALL_FLAGS_NONE, -1, NULL, &err);
    if (r) { g_variant_get(r, "(b)", &ok); g_variant_unref(r); if (ok) { ctx->is_registered = FALSE; ctx->handler_id = 0; LOG_INFO("Unregistered"); } return ok; }
    LOG_ERROR("Unregistration failed: %s", err->message); g_error_free(err); return FALSE;
}

static gboolean api_check_for_update(TestClientContext *ctx, const gchar *hid) {
    GError *err = NULL; gchar *cv = NULL, *av = NULL, *d = NULL, *st = NULL; gint32 result = 0, sc = 0;
    LOG_INFO("Calling CheckForUpdate (handler: %s)", hid);
    GVariant *r = g_dbus_connection_call_sync(ctx->connection, DBUS_SERVICE_NAME, DBUS_OBJECT_PATH, DBUS_INTERFACE_NAME, "CheckForUpdate", g_variant_new("(s)", hid), G_VARIANT_TYPE("(issssi)"), G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &err);
    if (r) { g_variant_get(r, "(issssi)", &result, &cv, &av, &d, &st, &sc); LOG_INFO("Response: result=%d, current=%s, available=%s, status_code=%d", result, cv ? cv : "", av ? cv : "", sc); g_free(cv); g_free(av); g_free(d); g_free(st); g_variant_unref(r); return TRUE; }
    LOG_ERROR("CheckForUpdate failed: %s", err->message); g_error_free(err); return FALSE;
}

static gboolean api_download_firmware(TestClientContext *ctx, const gchar *hid, const gchar *fn, const gchar *url, const gchar *type) {
    GError *err = NULL; gchar *r1 = NULL, *r2 = NULL, *r3 = NULL; gboolean ok = FALSE;
    LOG_INFO("Calling DownloadFirmware (handler=%s, firmware=%s, url=%s, type=%s)", hid, fn ? fn : "", url ? url : "", type ? type : "");
    GVariant *r = g_dbus_connection_call_sync(ctx->connection, DBUS_SERVICE_NAME, DBUS_OBJECT_PATH, DBUS_INTERFACE_NAME, "DownloadFirmware", g_variant_new("(ssss)", hid, fn ? fn : "", url ? url : "", type ? type : ""), G_VARIANT_TYPE("(sss)"), G_DBUS_CALL_FLAGS_NONE, 30000, NULL, &err);
    if (r) { g_variant_get(r, "(sss)", &r1, &r2, &r3); LOG_INFO("Response: result=%s, status=%s, message=%s", r1 ? r1 : "", r2 ? r2 : "", r3 ? r3 : ""); ok = (r1 && g_strcmp0(r1, "RDKFW_DWNL_SUCCESS") == 0); g_free(r1); g_free(r2); g_free(r3); g_variant_unref(r); return ok; }
    LOG_ERROR("DownloadFirmware failed: %s", err->message); g_error_free(err); return FALSE;
}

static gboolean api_update_firmware(TestClientContext *ctx, const gchar *hid, const gchar *fn, const gchar *loc, const gchar *type, const gchar *reboot) {
    GError *err = NULL; gchar *r1 = NULL, *r2 = NULL, *r3 = NULL; gboolean ok = FALSE;
    LOG_INFO("Calling UpdateFirmware (handler=%s, firmware=%s, location=%s, type=%s, reboot=%s)", hid, fn ? fn : "", loc ? loc : "", type ? type : "", reboot ? reboot : "true");
    GVariant *r = g_dbus_connection_call_sync(ctx->connection, DBUS_SERVICE_NAME, DBUS_OBJECT_PATH, DBUS_INTERFACE_NAME, "UpdateFirmware", g_variant_new("(sssss)", hid, fn ? fn : "", type ? type : "", loc ? loc : "", reboot ? reboot : "true"), G_VARIANT_TYPE("(sss)"), G_DBUS_CALL_FLAGS_NONE, 30000, NULL, &err);
    if (r) { g_variant_get(r, "(sss)", &r1, &r2, &r3); LOG_INFO("Response: result=%s, status=%s, message=%s", r1 ? r1 : "", r2 ? r2 : "", r3 ? r3 : ""); ok = (r1 && g_strcmp0(r1, "RDKFW_UPDATE_SUCCESS") == 0); g_free(r1); g_free(r2); g_free(r3); g_variant_unref(r); return ok; }
    LOG_ERROR("UpdateFirmware failed: %s", err->message); g_error_free(err); return FALSE;
}

static gboolean wait_for_signal(TestClientContext *ctx, guint timeout_sec) {
    if (!ctx) return FALSE;
    ctx->loop = g_main_loop_new(NULL, FALSE);
    guint tid = g_timeout_add_seconds(timeout_sec, on_timeout, ctx);
    g_main_loop_run(ctx->loop);
    g_source_remove(tid);
    g_main_loop_unref(ctx->loop); ctx->loop = NULL;
    return TRUE;
}

static void print_help(const char *prog) {
    printf("\nRDK Firmware Update Manager - Test Client\n\n");
    printf("Usage:\n  %s <process_name> <lib_version> <test_mode> [args...]\n  %s --help\n  %s --list\n\n", prog, prog, prog);
    printf("Parameters:\n  process_name   Client process name (e.g., 'VideoApp')\n  lib_version    Library version (e.g., '1.0.0')\n  test_mode      Test scenario to execute\n\n");
    printf("Options:\n  --help    Show this help\n  --list    List available tests\n  --debug   Enable debug logging\n\n");
    printf("Examples:\n  %s MyApp 1.0 register\n  %s MyApp 1.0 check-basic\n  %s MyApp 1.0 download-success fw.bin http://server/fw.bin PCI\n  %s MyApp 1.0 update-pci-success fw.bin /opt/CDL PCI true\n\n", prog, prog, prog, prog);
}

static void print_test_list(void) {
    printf("\nAvailable Test Modes:\n\n");
    printf("Registration:\n  register                    Register process\n  register-duplicate          Re-registration test\n  register-stress             Rapid cycles\n\n");
    //printf("CheckForUpdate:\n  check-basic                 Basic check\n  check-cache-hit             With cache\n  check-cache-miss            Without cache\n  check-not-registered        Without registration\n  check-invalid-handler       Invalid handler\n  check-concurrent           Rapid calls\n\n");
    printf("DownloadFirmware:\n  download-success <n> <url> <type>   Valid download\n  download-cached <n>                 Cached file\n  download-not-registered                Without registration\n  download-invalid-handler               Invalid handler\n  download-empty-name                    Empty name\n  download-empty-url                     Empty URL\n  download-invalid-url                   Invalid URL\n  download-progress <n> <url> <type>  Monitor progress\n\n");
    printf("UpdateFirmware:\n  update-pci-success <n> <loc> <type> <reboot>   PCI upgrade\n  update-pci-deferred <n> <loc> <type>           Deferred reboot\n  update-pdri-success <n> <loc>                  PDRI upgrade\n  update-not-registered                             Without registration\n  update-empty-name                                 Empty name\n  update-empty-type                                 Empty type\n  update-file-not-found                             Missing file\n  update-peripheral <n> <loc>                    Peripheral\n  update-custom-location <n> <path>              Custom path\n  update-progress <n> <loc>                      Monitor progress\n\n");
    printf("Workflow:\n  workflow-check-download <n> <url> <type>   Check then download\n  workflow-full <n> <url> <type>             Full cycle\n\n");
}

static int execute_test(TestClientContext *ctx, int argc, char *argv[]) {
    const char *mode = argv[3];
    gchar *hid = NULL;
    gboolean passed = FALSE;

    LOG_INFO("Executing test: %s", mode);

    if (g_strcmp0(mode, "register") == 0) { passed = client_register(ctx); }
    else if (g_strcmp0(mode, "register-duplicate") == 0) {
        if (client_register(ctx)) { guint64 fid = ctx->handler_id; ctx->is_registered = FALSE; ctx->handler_id = 0;
            if (client_register(ctx)) { passed = (ctx->handler_id == fid); } else { passed = TRUE; ctx->handler_id = fid; ctx->is_registered = TRUE; } }
    }
    else if (g_strcmp0(mode, "register-stress") == 0) {
        int ok = 0; for (int i = 0; i < 10; i++) { ctx->is_registered = FALSE; ctx->handler_id = 0; if (client_register(ctx) && client_unregister(ctx)) ok++; usleep(100000); }
        LOG_INFO("Stress: %d/10 successful", ok); passed = (ok == 10);
    }
    else if (g_strcmp0(mode, "check-basic") == 0) { if (client_register(ctx)) { hid = g_strdup_printf("%" G_GUINT64_FORMAT, ctx->handler_id); passed = api_check_for_update(ctx, hid); g_free(hid); } }
    else if (g_strcmp0(mode, "check-cache-hit") == 0) {
        if (!g_file_test(XCONF_CACHE_FILE, G_FILE_TEST_EXISTS)) { LOG_WARN("Cache not found: %s", XCONF_CACHE_FILE); }
        else if (client_register(ctx)) { hid = g_strdup_printf("%" G_GUINT64_FORMAT, ctx->handler_id); passed = api_check_for_update(ctx, hid); g_free(hid); }
    }
    else if (g_strcmp0(mode, "check-cache-miss") == 0) {
        if (g_file_test(XCONF_CACHE_FILE, G_FILE_TEST_EXISTS)) { LOG_WARN("Remove cache first: rm %s", XCONF_CACHE_FILE); }
        else if (client_register(ctx)) { hid = g_strdup_printf("%" G_GUINT64_FORMAT, ctx->handler_id); api_check_for_update(ctx, hid); LOG_INFO("Waiting for signal..."); ctx->check_complete = FALSE; wait_for_signal(ctx, SIGNAL_TIMEOUT_SEC); passed = ctx->check_complete; g_free(hid); }
    }
    else if (g_strcmp0(mode, "check-not-registered") == 0) { passed = !api_check_for_update(ctx, "12345"); }
    else if (g_strcmp0(mode, "check-invalid-handler") == 0) { if (client_register(ctx)) { passed = !api_check_for_update(ctx, "99999999"); } }
    /*else if (g_strcmp0(mode, "check-concurrent") == 0) { if (client_register(ctx)) { hid = g_strdup_printf("%" G_GUINT64_FORMAT, ctx->handler_id); for (int i = 0; i < 3; i++) { LOG_INFO("Call %d/3", i+1); api_check_for_update(ctx, hid); usleep(100000); } passed = TRUE; g_free(hid); } }*/
    else if (g_strcmp0(mode, "download-success") == 0) {
        if (argc < 7) { LOG_ERROR("Usage: %s <p> <v> download-success <name> <url> <type>", argv[0]); return 1; }
        if (client_register(ctx)) { hid = g_strdup_printf("%" G_GUINT64_FORMAT, ctx->handler_id); if (api_download_firmware(ctx, hid, argv[4], argv[5], argv[6])) { LOG_INFO("Waiting for download..."); ctx->download_complete = FALSE; wait_for_signal(ctx, SIGNAL_TIMEOUT_SEC); passed = ctx->download_success; } g_free(hid); }
    }
    else if (g_strcmp0(mode, "download-cached") == 0) {
        if (argc < 5) { LOG_ERROR("Usage: %s <p> <v> download-cached <name>", argv[0]); return 1; }
        gchar *p = g_strdup_printf("%s/%s", DEFAULT_FIRMWARE_DIR, argv[4]); if (!g_file_test(p, G_FILE_TEST_EXISTS)) { LOG_WARN("File not found: %s", p); } else if (client_register(ctx)) { hid = g_strdup_printf("%" G_GUINT64_FORMAT, ctx->handler_id); passed = api_download_firmware(ctx, hid, argv[4], "http://dummy", "PCI"); g_free(hid); } g_free(p);
    }
    else if (g_strcmp0(mode, "download-not-registered") == 0) { passed = !api_download_firmware(ctx, "12345", "test.bin", "http://test", "PCI"); }
    else if (g_strcmp0(mode, "download-invalid-handler") == 0) { if (client_register(ctx)) { passed = !api_download_firmware(ctx, "99999999", "test.bin", "http://test", "PCI"); } }
    else if (g_strcmp0(mode, "download-empty-name") == 0) { if (client_register(ctx)) { hid = g_strdup_printf("%" G_GUINT64_FORMAT, ctx->handler_id); passed = !api_download_firmware(ctx, hid, "", "http://test", "PCI"); g_free(hid); } }
    else if (g_strcmp0(mode, "download-empty-url") == 0) { if (client_register(ctx)) { hid = g_strdup_printf("%" G_GUINT64_FORMAT, ctx->handler_id); passed = !api_download_firmware(ctx, hid, "test.bin", "", "PCI"); g_free(hid); } }
    else if (g_strcmp0(mode, "download-invalid-url") == 0) { if (client_register(ctx)) { hid = g_strdup_printf("%" G_GUINT64_FORMAT, ctx->handler_id); passed = !api_download_firmware(ctx, hid, "test.bin", "invalid", "PCI"); g_free(hid); } }
    else if (g_strcmp0(mode, "download-progress") == 0) {
        if (argc < 7) { LOG_ERROR("Usage: %s <p> <v> download-progress <name> <url> <type>", argv[0]); return 1; }
        if (client_register(ctx)) { hid = g_strdup_printf("%" G_GUINT64_FORMAT, ctx->handler_id); if (api_download_firmware(ctx, hid, argv[4], argv[5], argv[6])) { LOG_INFO("Monitoring progress..."); ctx->download_complete = FALSE; wait_for_signal(ctx, SIGNAL_TIMEOUT_SEC); passed = (ctx->signal_count > 0); LOG_INFO("Signals received: %u", ctx->signal_count); } g_free(hid); }
    }
    else if (g_strcmp0(mode, "update-pci-success") == 0) {
        if (argc < 8) { LOG_ERROR("Usage: %s <p> <v> update-pci-success <name> <loc> <type> <reboot>", argv[0]); return 1; }
        if (client_register(ctx)) { hid = g_strdup_printf("%" G_GUINT64_FORMAT, ctx->handler_id); if (api_update_firmware(ctx, hid, argv[4], argv[5], argv[6], argv[7])) { LOG_INFO("Waiting for flash..."); ctx->flash_complete = FALSE; wait_for_signal(ctx, FLASH_TIMEOUT_SEC); passed = ctx->flash_success; } g_free(hid); }
    }
    else if (g_strcmp0(mode, "update-pci-deferred") == 0) {
        if (argc < 7) { LOG_ERROR("Usage: %s <p> <v> update-pci-deferred <name> <loc> <type>", argv[0]); return 1; }
        if (client_register(ctx)) { hid = g_strdup_printf("%" G_GUINT64_FORMAT, ctx->handler_id); if (api_update_firmware(ctx, hid, argv[4], argv[5], argv[6], "false")) { LOG_INFO("Waiting for flash..."); ctx->flash_complete = FALSE; wait_for_signal(ctx, FLASH_TIMEOUT_SEC); passed = ctx->flash_success; } g_free(hid); }
    }
    else if (g_strcmp0(mode, "update-pdri-success") == 0) {
        if (argc < 6) { LOG_ERROR("Usage: %s <p> <v> update-pdri-success <name> <loc>", argv[0]); return 1; }
        if (client_register(ctx)) { hid = g_strdup_printf("%" G_GUINT64_FORMAT, ctx->handler_id); if (api_update_firmware(ctx, hid, argv[4], argv[5], "PDRI", "false")) { LOG_INFO("Waiting for flash..."); ctx->flash_complete = FALSE; wait_for_signal(ctx, FLASH_TIMEOUT_SEC); passed = ctx->flash_success; } g_free(hid); }
    }
    else if (g_strcmp0(mode, "update-not-registered") == 0) { passed = !api_update_firmware(ctx, "12345", "test.bin", "/opt/CDL", "PCI", "true"); }
    else if (g_strcmp0(mode, "update-empty-name") == 0) { if (client_register(ctx)) { hid = g_strdup_printf("%" G_GUINT64_FORMAT, ctx->handler_id); passed = !api_update_firmware(ctx, hid, "", "/opt/CDL", "PCI", "true"); g_free(hid); } }
    else if (g_strcmp0(mode, "update-empty-type") == 0) { if (client_register(ctx)) { hid = g_strdup_printf("%" G_GUINT64_FORMAT, ctx->handler_id); passed = !api_update_firmware(ctx, hid, "test.bin", "/opt/CDL", "", "true"); g_free(hid); } }
    else if (g_strcmp0(mode, "update-file-not-found") == 0) { if (client_register(ctx)) { hid = g_strdup_printf("%" G_GUINT64_FORMAT, ctx->handler_id); passed = !api_update_firmware(ctx, hid, "nonexistent.bin", "/opt/CDL", "PCI", "true"); g_free(hid); } }
    else if (g_strcmp0(mode, "update-peripheral") == 0) {
        if (argc < 6) { LOG_ERROR("Usage: %s <p> <v> update-peripheral <name> <loc>", argv[0]); return 1; }
        if (client_register(ctx)) { hid = g_strdup_printf("%" G_GUINT64_FORMAT, ctx->handler_id); if (api_update_firmware(ctx, hid, argv[4], argv[5], "PERIPHERAL", "false")) { LOG_INFO("Waiting for flash..."); ctx->flash_complete = FALSE; wait_for_signal(ctx, FLASH_TIMEOUT_SEC); passed = ctx->flash_success; } g_free(hid); }
    }
    else if (g_strcmp0(mode, "update-custom-location") == 0) {
        if (argc < 6) { LOG_ERROR("Usage: %s <p> <v> update-custom-location <name> <path>", argv[0]); return 1; }
        if (client_register(ctx)) { hid = g_strdup_printf("%" G_GUINT64_FORMAT, ctx->handler_id); if (api_update_firmware(ctx, hid, argv[4], argv[5], "PCI", "false")) { LOG_INFO("Waiting for flash..."); ctx->flash_complete = FALSE; wait_for_signal(ctx, FLASH_TIMEOUT_SEC); passed = ctx->flash_success; } g_free(hid); }
    }
    else if (g_strcmp0(mode, "update-progress") == 0) {
        if (argc < 6) { LOG_ERROR("Usage: %s <p> <v> update-progress <name> <loc>", argv[0]); return 1; }
        if (client_register(ctx)) { hid = g_strdup_printf("%" G_GUINT64_FORMAT, ctx->handler_id); if (api_update_firmware(ctx, hid, argv[4], argv[5], "PCI", "false")) { LOG_INFO("Monitoring progress..."); ctx->flash_complete = FALSE; wait_for_signal(ctx, FLASH_TIMEOUT_SEC); passed = (ctx->signal_count > 0); LOG_INFO("Signals received: %u", ctx->signal_count); } g_free(hid); }
    }
    else if (g_strcmp0(mode, "workflow-check-download") == 0) {
        if (argc < 7) { LOG_ERROR("Usage: %s <p> <v> workflow-check-download <name> <url> <type>", argv[0]); return 1; }
        if (client_register(ctx)) { hid = g_strdup_printf("%" G_GUINT64_FORMAT, ctx->handler_id); LOG_INFO("Step 1: CheckForUpdate"); api_check_for_update(ctx, hid); sleep(1); LOG_INFO("Step 2: DownloadFirmware"); if (api_download_firmware(ctx, hid, argv[4], argv[5], argv[6])) { ctx->download_complete = FALSE; wait_for_signal(ctx, SIGNAL_TIMEOUT_SEC); passed = ctx->download_success; } g_free(hid); }
    }
    else if (g_strcmp0(mode, "workflow-full") == 0) {
        if (argc < 7) { LOG_ERROR("Usage: %s <p> <v> workflow-full <name> <url> <type>", argv[0]); return 1; }
        if (client_register(ctx)) { hid = g_strdup_printf("%" G_GUINT64_FORMAT, ctx->handler_id); LOG_INFO("Step 1: CheckForUpdate"); api_check_for_update(ctx, hid); sleep(1); LOG_INFO("Step 2: DownloadFirmware"); if (api_download_firmware(ctx, hid, argv[4], argv[5], argv[6])) { ctx->download_complete = FALSE; wait_for_signal(ctx, SIGNAL_TIMEOUT_SEC); if (ctx->download_success) { LOG_INFO("Step 3: UpdateFirmware"); if (api_update_firmware(ctx, hid, argv[4], DEFAULT_FIRMWARE_DIR, argv[6], "false")) { ctx->flash_complete = FALSE; wait_for_signal(ctx, FLASH_TIMEOUT_SEC); passed = ctx->flash_success; } } } g_free(hid); }
    }
    else { LOG_ERROR("Unknown test: %s", mode); LOG_INFO("Use --list to see available tests"); return 1; }

    LOG_INFO("Test result: %s", passed ? "PASSED" : "FAILED");
    return passed ? 0 : 1;
}

int main(int argc, char *argv[]) {
    if (argc < 2) { print_help(argv[0]); return 1; }
    if (g_strcmp0(argv[1], "--help") == 0 || g_strcmp0(argv[1], "-h") == 0) { print_help(argv[0]); return 0; }
    if (g_strcmp0(argv[1], "--list") == 0 || g_strcmp0(argv[1], "-l") == 0) { print_test_list(); return 0; }
    if (argc < 4) { LOG_ERROR("Insufficient arguments"); print_help(argv[0]); return 1; }
    for (int i = 1; i < argc; i++) if (g_strcmp0(argv[i], "--debug") == 0) g_log_level = LOG_DEBUG_LVL;
    const gchar *pname = argv[1], *ver = argv[2];
    if (!pname || strlen(pname) == 0) { LOG_ERROR("Process name required"); return 1; }
    if (!ver || strlen(ver) == 0) { LOG_ERROR("Version required"); return 1; }
    LOG_INFO("RDK Firmware Update Manager Test Client");
    LOG_INFO("Process: %s, Version: %s, Test: %s", pname, ver, argv[3]);
    TestClientContext *ctx = client_create(pname, ver);
    if (!ctx) { LOG_ERROR("Failed to initialize client"); return 1; }
    int rc = execute_test(ctx, argc, argv);
    client_destroy(ctx);
    return rc;
}
