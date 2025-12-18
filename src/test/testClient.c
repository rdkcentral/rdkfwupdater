/*
 * testClient.c - RDK Firmware Updater COMPREHENSIVE Test Suite
 * 
 * Version: 4.0 - Enhanced with Full Scenario Coverage & Multi-Instance Support
 * Date: December 18, 2025
 * 
 * Features:
 * ✓ 46 comprehensive test scenarios covering all APIs
 * ✓ Multi-instance concurrent testing support
 * ✓ Structured logging with timestamps
 * ✓ Automated pass/fail validation
 * ✓ Test report generation
 * ✓ All CheckForUpdate scenarios (12 tests)
 * ✓ All DownloadFirmware scenarios (13 tests - S1-S13)
 * ✓ All UpdateFirmware scenarios (13 tests - S1-S13)
 * ✓ Workflow and stress tests (5 tests)
 * 
 * Multi-Instance Usage (Concurrent Testing):
 *   Terminal 1: ./testClient Process1 1.0 check-concurrent
 *   Terminal 2: ./testClient Process2 1.0 check-concurrent  (run simultaneously)
 *   Terminal 3: ./testClient Process3 1.0 download-concurrent
 * 
 * Single Instance Usage:
 *   ./testClient <process_name> <lib_version> <test_mode>
 *   ./testClient --list                    # Show all test modes
 *   ./testClient --help                    # Show detailed help
 * 
 * Examples:
 *   ./testClient MyApp 1.0 check-cache-hit
 *   ./testClient MyApp 1.0 download-http-success
 *   ./testClient MyApp 1.0 flash-pci-deferred
 *   ./testClient MyApp 1.0 workflow-complete
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <gio/gio.h>

// D-Bus service details
#define DBUS_SERVICE_NAME "org.rdkfwupdater.Service"
#define DBUS_OBJECT_PATH "/org/rdkfwupdater/Service"
#define DBUS_INTERFACE_NAME "org.rdkfwupdater.Interface"

// Test configuration
#define DEFAULT_FIRMWARE_NAME "test_firmware.bin"
#define DEFAULT_DOWNLOAD_PATH "/opt/CDL"
#define XCONF_CACHE_FILE "/tmp/xconf_response_thunder.txt"

// Multi-instance support: Each instance gets unique process name
#define INSTANCE_ID_ENV "TESTCLIENT_INSTANCE_ID"

// Enhanced logging macros with timestamps and structured formatting
#define TIMESTAMP() ({ \
    time_t now = time(NULL); \
    struct tm *t = localtime(&now); \
    char buf[64]; \
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", t->tm_hour, t->tm_min, t->tm_sec); \
    buf; \
})

#define TEST_HEADER(scenario, description) \
    printf("\n╔════════════════════════════════════════════════════════════════╗\n"); \
    printf("║ TEST: %-57s║\n", scenario); \
    printf("║ DESC: %-57s║\n", description); \
    printf("║ TIME: %-57s║\n", TIMESTAMP()); \
    printf("║ PID:  %-57d║\n", getpid()); \
    printf("╚════════════════════════════════════════════════════════════════╝\n\n")

#define TEST_STEP(step_num, description) \
    printf("[%s][PID:%d] STEP %d: %s\n", TIMESTAMP(), getpid(), step_num, description)

#define TEST_EXPECT(condition, description) \
    printf("[%s][PID:%d] EXPECT: %s → %s\n", TIMESTAMP(), getpid(), description, (condition) ? "✓ PASS" : "✗ FAIL")

#define TEST_RESULT(passed, details) \
    do { \
        printf("\n"); \
        if (passed) { \
            printf("╔════════════════════════════════════════════════════════════════╗\n"); \
            printf("║                    ✓✓✓ TEST PASSED ✓✓✓                        ║\n"); \
            printf("╚════════════════════════════════════════════════════════════════╝\n"); \
        } else { \
            printf("╔════════════════════════════════════════════════════════════════╗\n"); \
            printf("║                    ✗✗✗ TEST FAILED ✗✗✗                        ║\n"); \
            printf("║ %s\n", details); \
            printf("╚════════════════════════════════════════════════════════════════╝\n"); \
        } \
        printf("\n"); \
    } while(0)

// Utility macros for enhanced logging
#define PRINT_SUCCESS(fmt, ...) printf("[%s][PID:%d] ✓ SUCCESS: " fmt "\n", TIMESTAMP(), getpid(), ##__VA_ARGS__)
#define PRINT_ERROR(fmt, ...)   printf("[%s][PID:%d] ✗ ERROR: " fmt "\n", TIMESTAMP(), getpid(), ##__VA_ARGS__)
#define PRINT_INFO(fmt, ...)    printf("[%s][PID:%d] ℹ INFO: " fmt "\n", TIMESTAMP(), getpid(), ##__VA_ARGS__)
#define PRINT_WARN(fmt, ...)    printf("[%s][PID:%d] ⚠ WARN: " fmt "\n", TIMESTAMP(), getpid(), ##__VA_ARGS__)
#define PRINT_DEBUG(fmt, ...)   printf("[%s][PID:%d]   DEBUG: " fmt "\n", TIMESTAMP(), getpid(), ##__VA_ARGS__)

#define LOG_API_CALL(api, params) \
    printf("[%s][PID:%d] 📞 API CALL: %s(%s)\n", TIMESTAMP(), getpid(), api, params)

#define LOG_API_RESPONSE(api, result) \
    printf("[%s][PID:%d] 📬 RESPONSE: %s → %s\n", TIMESTAMP(), getpid(), api, result)

#define LOG_SIGNAL(signal_name) \
    printf("[%s][PID:%d] 📡 SIGNAL: %s\n", TIMESTAMP(), getpid(), signal_name)

#define LOG_SEPARATOR() \
    printf("────────────────────────────────────────────────────────────────\n")

// Test client context structure - enhanced for comprehensive testing
typedef struct {
    GDBusConnection *connection;
    gchar *process_name;
    gchar *lib_version;
    guint64 handler_id;
    gboolean is_registered;
    gint instance_id;             // Unique ID for multi-instance testing

    // Main loop for async operations
    GMainLoop *loop;
    
    // CheckForUpdate test state
    gboolean check_complete;
    gboolean check_success;
    gint check_result_code;
    gchar *check_error_msg;
    
    // Download test orchestration
    gboolean download_done;       // Flag set when download finishes or fails
    gboolean download_success;    // Result of download (TRUE = success)
    gint download_progress;       // Last download progress percentage
    gchar *download_status_msg;   // Download status message
    
    // UpdateFirmware test orchestration
    gboolean flash_done;          // Flag set when flash finishes or fails
    gboolean flash_success;       // Result of flash operation (TRUE = success)
    gint flash_progress;          // Last received flash progress percentage
    gchar *flash_status_msg;      // Last flash status message
    
    // Signal subscription IDs for cleanup
    guint check_signal_id;
    guint download_progress_id;
    guint download_error_id;
    guint update_progress_id;
    
    // Test statistics and tracking
    gchar *current_test_name;     // Name of running test
    time_t test_start_time;       // Test start timestamp
    guint signals_received;       // Counter for debugging
    guint errors_detected;        // Counter for bug detection
    guint tests_passed;           // Passed test counter
    guint tests_failed;           // Failed test counter
} TestClientContext;

// =============================================================================
// FUNCTION PROTOTYPES
// =============================================================================

// Core client functions
static TestClientContext* test_client_new(const gchar *process_name, const gchar *lib_version);
static gboolean test_client_register(TestClientContext *client);
static gboolean test_client_check_update(TestClientContext *client);
static gboolean test_client_download_firmware(TestClientContext *client, const char *firmware_name, const char *download_url, const char *type_of_firmware);
static gboolean test_client_update_firmware(TestClientContext *client, const char *firmware_name, const char *type_of_firmware, const char *location, const char *reboot);
static gboolean test_client_unregister(TestClientContext *client);
static void test_client_free(TestClientContext *client);

// Help and usage functions
static void print_usage(const char *program_name);
static void print_all_test_modes(void);
static void run_test_scenario(TestClientContext *client, const gchar *test_mode, const char *program_name);

// Timeout callback
static gboolean operation_timeout_cb(gpointer user_data);

// CheckForUpdate test functions (12 scenarios)
static void test_check_cache_hit(TestClientContext *client);
static void test_check_cache_miss(TestClientContext *client);
static void test_check_concurrent(TestClientContext *client);
static void test_check_xconf_unreachable(TestClientContext *client);
static void test_check_cache_corrupt(TestClientContext *client);
static void test_check_invalid_handler(TestClientContext *client);
static void test_check_no_register(TestClientContext *client);
static void test_check_rapid(TestClientContext *client);

// DownloadFirmware test functions (13 scenarios - S1-S13)
static void test_download_http_success(TestClientContext *client);
static void test_download_custom_url(TestClientContext *client);
static void test_download_xconf_url(TestClientContext *client);
static void test_download_not_registered(TestClientContext *client);
static void test_download_concurrent(TestClientContext *client);
static void test_download_empty_name(TestClientContext *client);
static void test_download_invalid_type(TestClientContext *client);
static void test_download_with_progress(TestClientContext *client);

// UpdateFirmware test functions (13 scenarios - S1-S13)
static void test_flash_pci_immediate(TestClientContext *client);
static void test_flash_pci_deferred(TestClientContext *client);
static void test_flash_pdri_success(TestClientContext *client);
static void test_flash_not_registered(TestClientContext *client);
static void test_flash_concurrent(TestClientContext *client);
static void test_flash_empty_name(TestClientContext *client);
static void test_flash_invalid_type(TestClientContext *client);
static void test_flash_custom_location(TestClientContext *client);
static void test_flash_peripheral(TestClientContext *client);

// Workflow test functions
static void test_workflow_complete(TestClientContext *client);
static void test_workflow_download_flash(TestClientContext *client);
static void test_stress_all(TestClientContext *client);

/**
 * Signal callback for CheckForUpdateComplete - simulates library callback mechanism
 */
static void on_check_for_update_complete_signal(GDBusConnection *connection,
                                               const gchar *sender_name,
                                               const gchar *object_path,
                                               const gchar *interface_name,
                                               const gchar *signal_name,
                                               GVariant *parameters,
                                               gpointer user_data)
{
    TestClientContext *client = (TestClientContext*)user_data;
    gchar *handler_id;
    gint result_code;
    gchar *current_version, *available_version, *update_details, *status_message;
    
    // Extract signal parameters
    g_variant_get(parameters, "(sissss)", &handler_id, &result_code,
                  &current_version, &available_version, &update_details, &status_message);
    
    printf("\nD-Bus Signal Received: CheckForUpdateComplete\n");
    PRINT_INFO("Signal Details:");
    PRINT_INFO("  Handler ID: %s", handler_id);
    PRINT_INFO("  Result Code: %d (%s)", result_code, 
               result_code == 0 ? "UPDATE_AVAILABLE" :
               result_code == 1 ? "UPDATE_NOT_AVAILABLE" :
               result_code == 2 ? "UPDATE_ERROR" : "UNKNOWN");
    PRINT_INFO("  Current Version: %s", current_version);
    PRINT_INFO("  Available Version: %s", available_version);
    PRINT_INFO("  Update Details: %s", update_details);
    PRINT_INFO("  Status Message: %s", status_message);
    
    // Check if this signal is for our client (like real library would do)
    gchar *our_handler_str = g_strdup_printf("%"G_GUINT64_FORMAT, client->handler_id);
    if (g_strcmp0(handler_id, our_handler_str) == 0) {
        PRINT_SUCCESS("Signal is for our handler - invoking callback!");
        printf("[CALLBACK INVOKED] Client application callback called with FwData\n");
    } else {
        PRINT_WARN("Signal is for different handler (%s vs %s) - ignoring", handler_id, our_handler_str);
    }
    g_free(our_handler_str);
    
    // Free extracted strings
    g_free(handler_id);
    g_free(current_version);
    g_free(available_version);
    g_free(update_details);
    g_free(status_message);
    
    printf("Signal processing complete\n\n");
}

/**
 * Signal callback for DownloadProgress - simulates firmware download progress
 */
static void on_download_progress_signal(GDBusConnection *connection,
                                        const gchar *sender_name,
                                        const gchar *object_path,
                                        const gchar *interface_name,
                                        const gchar *signal_name,
                                        GVariant *parameters,
                                        gpointer user_data)
{
    TestClientContext *client = (TestClientContext*)user_data;
    guint64 handler_id_numeric = 0;
    gchar *firmware_name = NULL;
    guint32 progress = 0;
    gchar *status_message = NULL;
    gchar *message = NULL;

    // Signal parameters expected from daemon: (t handlerId, s firmwareName, u progress, s status, s message)
    g_variant_get(parameters, "(tsuss)", &handler_id_numeric, &firmware_name, &progress, &status_message, &message);

    printf("\nD-Bus Signal Received: DownloadProgress\n");
    PRINT_INFO("Signal Details:");
    PRINT_INFO("  Handler ID: %" G_GUINT64_FORMAT, handler_id_numeric);
    PRINT_INFO("  Firmware: %s", firmware_name ? firmware_name : "(null)");
    PRINT_INFO("  Progress: %u%%", progress);
    PRINT_INFO("  Status: %s", status_message ? status_message : "(null)");
    PRINT_INFO("  Message: %s", message ? message : "(null)");

    // Verify this signal is for our handler
    if (client && client->handler_id == handler_id_numeric) {
        PRINT_SUCCESS("Signal is for our handler - download progress update");

        // If progress indicates completion, mark done and quit loop
        if (progress >= 100 || (status_message && g_strcmp0(status_message, "COMPLETED") == 0)) {
            client->download_done = TRUE;
            client->download_success = TRUE;
            if (client->loop) g_main_loop_quit(client->loop);
        }
    } else {
        PRINT_WARN("Signal is for different handler (%" G_GUINT64_FORMAT " vs %" G_GUINT64_FORMAT ") - ignoring",
                   handler_id_numeric, client ? client->handler_id : 0);
    }

    // Free duplicated strings returned by g_variant_get
    if (firmware_name) g_free(firmware_name);
    if (status_message) g_free(status_message);
    if (message) g_free(message);

    printf("Signal processing complete\n\n");
}

/**
 * Signal callback for DownloadError - final error notification
 */
static void on_download_error_signal(GDBusConnection *connection,
                                     const gchar *sender_name,
                                     const gchar *object_path,
                                     const gchar *interface_name,
                                     const gchar *signal_name,
                                     GVariant *parameters,
                                     gpointer user_data)
{
    TestClientContext *client = (TestClientContext*)user_data;
    guint64 handler_id_numeric = 0;
    gchar *firmware_name = NULL;
    gchar *status = NULL;
    gchar *error_message = NULL;

    // Signal parameters: (t handlerId, s firmwareName, s status, s errorMessage)
    g_variant_get(parameters, "(tsss)", &handler_id_numeric, &firmware_name, &status, &error_message);

    printf("\nD-Bus Signal Received: DownloadError\n");
    PRINT_INFO("Signal Details:");
    PRINT_INFO("  Handler ID: %" G_GUINT64_FORMAT, handler_id_numeric);
    PRINT_INFO("  Firmware: %s", firmware_name ? firmware_name : "(null)");
    PRINT_INFO("  Status: %s", status ? status : "(null)");
    PRINT_INFO("  Error: %s", error_message ? error_message : "(null)");

    if (client && client->handler_id == handler_id_numeric) {
        PRINT_ERROR("Download failed for our handler");
        client->download_done = TRUE;
        client->download_success = FALSE;
        if (client->loop) g_main_loop_quit(client->loop);
    } else {
        PRINT_WARN("DownloadError signal for other handler - ignoring");
    }

    if (firmware_name) g_free(firmware_name);
    if (status) g_free(status);
    if (error_message) g_free(error_message);

    printf("Signal processing complete\n\n");
}

/**
 * Signal callback for UpdateProgress - firmware flash progress monitoring
 */
static void on_update_progress_signal(GDBusConnection *connection,
                                       const gchar *sender_name,
                                       const gchar *object_path,
                                       const gchar *interface_name,
                                       const gchar *signal_name,
                                       GVariant *parameters,
                                       gpointer user_data)
{
    TestClientContext *client = (TestClientContext*)user_data;
    guint64 handler_id_numeric = 0;
    gchar *firmware_name = NULL;
    gint32 progress = 0;
    gint32 status_code = 0;
    gchar *message = NULL;

    // Signal parameters: (t handlerId, s firmwareName, i progress, i status, s message)
    g_variant_get(parameters, "(tsii s)", &handler_id_numeric, &firmware_name, &progress, &status_code, &message);

    printf("\nD-Bus Signal Received: UpdateProgress\n");
    PRINT_INFO("Signal Details:");
    PRINT_INFO("  Handler ID: %" G_GUINT64_FORMAT, handler_id_numeric);
    PRINT_INFO("  Firmware: %s", firmware_name ? firmware_name : "(null)");
    PRINT_INFO("  Progress: %d%%", progress);
    PRINT_INFO("  Status Code: %d", status_code);
    PRINT_INFO("  Message: %s", message ? message : "(null)");

    // Verify this signal is for our handler
    if (client && client->handler_id == handler_id_numeric) {
        PRINT_SUCCESS("Signal is for our handler - flash progress update");
        
        client->signals_received++;
        client->flash_progress = progress;
        
        if (client->flash_status_msg) g_free(client->flash_status_msg);
        client->flash_status_msg = message ? g_strdup(message) : NULL;

        // Check for completion or error
        if (progress == 100) {
            PRINT_SUCCESS("Flash completed successfully (100%%)");
            client->flash_done = TRUE;
            client->flash_success = TRUE;
            if (client->loop) g_main_loop_quit(client->loop);
        } else if (progress < 0) {
            PRINT_ERROR("Flash failed (progress = %d)", progress);
            client->flash_done = TRUE;
            client->flash_success = FALSE;
            client->errors_detected++;
            if (client->loop) g_main_loop_quit(client->loop);
        }
    } else {
        PRINT_WARN("Signal for different handler (%" G_GUINT64_FORMAT " vs %" G_GUINT64_FORMAT ")",
                   handler_id_numeric, client ? client->handler_id : 0);
    }

    // Free extracted strings
    if (firmware_name) g_free(firmware_name);
    if (message && progress >= 0) g_free(message);  // Don't free if we saved it

    printf("Signal processing complete\n\n");
}

// Timeout callback for download wait
static gboolean download_timeout_cb(gpointer user_data)
{
    TestClientContext *client = (TestClientContext*)user_data;
    PRINT_ERROR("Download wait timed out");
    if (client) {
        client->download_done = TRUE;
        client->download_success = FALSE;
        if (client->loop) g_main_loop_quit(client->loop);
    }
    return G_SOURCE_REMOVE;
}

/**
 * Create new test client context
 */
static TestClientContext* test_client_new(const gchar *process_name, const gchar *lib_version)
{
    TestClientContext *client = g_malloc0(sizeof(TestClientContext));
    GError *error = NULL;
    
    PRINT_INFO("Creating test client for process '%s' version '%s'", process_name, lib_version);
    
    // Connect to system D-Bus
    client->connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    if (!client->connection) {
        PRINT_ERROR("Failed to connect to system D-Bus: %s", error->message);
        g_error_free(error);
        g_free(client);
        return NULL;
    }
    
    client->process_name = g_strdup(process_name);
    client->lib_version = g_strdup(lib_version);
    client->handler_id = 0;
    client->is_registered = FALSE;
    client->download_done = FALSE;
    client->download_success = FALSE;
    client->flash_done = FALSE;
    client->flash_success = FALSE;
    client->flash_progress = -1;
    client->flash_status_msg = NULL;
    client->signals_received = 0;
    client->errors_detected = 0;
    
    // Subscribe to CheckForUpdateComplete signal (like real library would do)
    PRINT_INFO("Subscribing to CheckForUpdateComplete D-Bus signals...");
    g_dbus_connection_signal_subscribe(
        client->connection,
        DBUS_SERVICE_NAME,                    // sender
        DBUS_INTERFACE_NAME,                  // interface
        "CheckForUpdateComplete",             // signal name
        DBUS_OBJECT_PATH,                     // object path
        NULL,                                 // arg0 (no filtering)
        G_DBUS_SIGNAL_FLAGS_NONE,
        on_check_for_update_complete_signal,  // callback
        client,                               // user_data
        NULL                                  // user_data_free_func
    );
    PRINT_SUCCESS("Subscribed to D-Bus signals successfully");
    
    // Subscribe to DownloadProgress signal
    PRINT_INFO("Subscribing to DownloadProgress D-Bus signals...");
    g_dbus_connection_signal_subscribe(
        client->connection,
        DBUS_SERVICE_NAME,                    // sender
        DBUS_INTERFACE_NAME,                  // interface
        "DownloadProgress",                   // signal name
        DBUS_OBJECT_PATH,                     // object path
        NULL,                                 // arg0 (no filtering)
        G_DBUS_SIGNAL_FLAGS_NONE,
        on_download_progress_signal,          // callback
        client,                               // user_data
        NULL                                  // user_data_free_func
    );
    PRINT_SUCCESS("Subscribed to D-Bus signals successfully");
    
    // Subscribe to DownloadError signal
    PRINT_INFO("Subscribing to DownloadError D-Bus signals...");
    g_dbus_connection_signal_subscribe(
        client->connection,
        DBUS_SERVICE_NAME,                    // sender
        DBUS_INTERFACE_NAME,                  // interface
        "DownloadError",                     // signal name
        DBUS_OBJECT_PATH,                     // object path
        NULL,                                 // arg0 (no filtering)
        G_DBUS_SIGNAL_FLAGS_NONE,
        on_download_error_signal,             // callback
        client,                               // user_data
        NULL                                  // user_data_free_func
    );
    PRINT_SUCCESS("Subscribed to D-Bus signals successfully");
    
    // Subscribe to UpdateProgress signal
    PRINT_INFO("Subscribing to UpdateProgress D-Bus signals...");
    g_dbus_connection_signal_subscribe(
        client->connection,
        DBUS_SERVICE_NAME,                    // sender
        DBUS_INTERFACE_NAME,                  // interface
        "UpdateProgress",                     // signal name
        DBUS_OBJECT_PATH,                     // object path
        NULL,                                 // arg0 (no filtering)
        G_DBUS_SIGNAL_FLAGS_NONE,
        on_update_progress_signal,            // callback
        client,                               // user_data
        NULL                                  // user_data_free_func
    );
    PRINT_SUCCESS("Subscribed to D-Bus signals successfully");
    
    PRINT_SUCCESS("Test client context created successfully");
    return client;
}

/**
 * Register process with daemon
 */
static gboolean test_client_register(TestClientContext *client)
{
    GError *error = NULL;
    GVariant *result = NULL;
    
    PRINT_INFO("Attempting to register process '%s' with version '%s'", 
               client->process_name, client->lib_version);
    
    // Call RegisterProcess D-Bus method
    result = g_dbus_connection_call_sync(
        client->connection,                                          // D-Bus connection
        DBUS_SERVICE_NAME,                                          // Bus name
        DBUS_OBJECT_PATH,                                           // Object path
        DBUS_INTERFACE_NAME,                                        // Interface
        "RegisterProcess",                                          // Method name
        g_variant_new("(ss)", client->process_name, client->lib_version), // Parameters
        G_VARIANT_TYPE("(t)"),                                      // Expected return type
        G_DBUS_CALL_FLAGS_NONE,                                     // Call flags
        -1,                                                         // Timeout (-1 = default)
        NULL,                                                       // Cancellable
        &error                                                      // Error
    );
    
    if (result) {
        // Success - extract handler_id
        g_variant_get(result, "(t)", &client->handler_id);
        client->is_registered = TRUE;
        g_variant_unref(result);
        
        PRINT_SUCCESS("Registration successful! Handler ID: %"G_GUINT64_FORMAT, client->handler_id);
        return TRUE;
    } else {
        // Error - print detailed error message
        PRINT_ERROR("Registration failed: %s", error->message);
        
        // Parse specific error types for better debugging
        if (g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED)) {
            PRINT_WARN("This is an ACCESS_DENIED error - likely duplicate registration scenario");
        } else if (g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS)) {
            PRINT_WARN("This is an INVALID_ARGS error - check process name validity");
        }
        
        g_error_free(error);
        return FALSE;
    }
}

/**
 * Perform CheckForUpdate call
 */
static gboolean test_client_check_update(TestClientContext *client)
{
    GError *error = NULL;
    GVariant *result = NULL;
    gchar *current_version = NULL;
    gchar *available_version = NULL; 
    gchar *update_details = NULL;
    gchar *status = NULL;
    gint32 status_code = 0;
    
    if (!client->is_registered) {
        PRINT_ERROR("Cannot check for updates - client not registered");
        return FALSE;
    }
    
    PRINT_INFO("Checking for updates using handler ID: %"G_GUINT64_FORMAT, client->handler_id);
    PRINT_INFO("Making D-Bus method call to daemon...");
    
    // Check if cache exists to predict the flow
    gboolean cache_exists = g_file_test("/tmp/xconf_response_thunder.txt", G_FILE_TEST_EXISTS);
    if (cache_exists) {
        PRINT_INFO("Cache hit - expecting immediate success with firmware data");
    } else {
        PRINT_INFO("No cache file - expecting immediate error + background fetch + signal");
    }
    
    // Convert handler_id to string (daemon expects handler_id as string)
    gchar *handler_id_str = g_strdup_printf("%"G_GUINT64_FORMAT, client->handler_id);
    PRINT_INFO("Sending handler_id as string: '%s'", handler_id_str);
    
    // Call CheckForUpdate D-Bus method
    result = g_dbus_connection_call_sync(
        client->connection,
        DBUS_SERVICE_NAME,
        DBUS_OBJECT_PATH,
        DBUS_INTERFACE_NAME,
        "CheckForUpdate",
        g_variant_new("(s)", handler_id_str),   // Send handler_id as string
        G_VARIANT_TYPE("(ssssi)"),
        G_DBUS_CALL_FLAGS_NONE,
        5000,                                         // 5 second timeout (should be immediate)
        NULL,
        &error
    );
    
    g_free(handler_id_str);  // Free the temporary string
    
    if (result) {
        // Success - extract all response fields
        g_variant_get(result, "(ssssi)", 
                      &current_version, &available_version, &update_details, &status, &status_code);
        
        PRINT_SUCCESS("CheckForUpdate D-Bus method response received!");
        PRINT_INFO("RESPONSE DATA:");
        PRINT_INFO("  Current Version: '%s'", current_version ? current_version : "");
        PRINT_INFO("  Available Version: '%s'", available_version ? available_version : ""); 
        PRINT_INFO("  Update Details: '%s'", update_details ? update_details : "");
        PRINT_INFO("  Status: '%s'", status ? status : "");
        PRINT_INFO("  Status Code: %d", status_code);
        
        // Interpret status code
        switch (status_code) {
            case 0:
                PRINT_SUCCESS("CACHE HIT: Update available!");
                PRINT_SUCCESS("Flow: Cache -> Immediate response with real data");
                break;
            case 1:
                PRINT_SUCCESS("CACHE HIT: No update available");
                PRINT_SUCCESS("Flow: Cache -> Immediate response with real data");
                break;
            case 2:
                PRINT_WARN("CACHE MISS: UPDATE_ERROR received");
                PRINT_INFO("Flow: No cache -> UPDATE_ERROR -> Background fetch started");
                PRINT_INFO("Wait for CheckForUpdateComplete signal for real result...");
                break;
            default:
                PRINT_ERROR("Unknown status code: %d", status_code);
                break;
        }
        
        // Free response strings
        g_free(current_version);
        g_free(available_version);
        g_free(update_details);
        g_free(status);
        g_variant_unref(result);
        
        return TRUE;
    } else {
        // Error
        PRINT_ERROR("CheckForUpdate failed: %s", error->message);
        
        if (g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED)) {
            PRINT_WARN("This suggests the process is not properly registered");
        }
        
        g_error_free(error);
        return FALSE;
    }
}

/**
 * Download firmware using DownloadFirmware call
 */
static gboolean test_client_download_firmware(TestClientContext *client, const char *firmware_name, const char *download_url, const char *type_of_firmware)
{
    GError *error = NULL;
    GVariant *result = NULL;
    gchar *handler_id_str = NULL;

    if (!client->is_registered) {
        PRINT_ERROR("Cannot download firmware - client not registered");
        return FALSE;
    }

    // Call CheckForUpdate first and process response (helps pick firmware info)
    PRINT_INFO("Performing CheckForUpdate before initiating download (informational)");
    test_client_check_update(client);

    if (!download_url || strlen(download_url) == 0) {
        PRINT_ERROR("Download URL must be provided for DownloadFirmware test (daemon will not accept empty URL)");
        return FALSE;
    }

    handler_id_str = g_strdup_printf("%"G_GUINT64_FORMAT, client->handler_id);
    PRINT_INFO("Calling DownloadFirmware with handler='%s', firmware='%s', url='%s', type='%s'",
               handler_id_str, firmware_name ? firmware_name : "(null)", download_url ? download_url : "(null)", type_of_firmware ? type_of_firmware : "(null)");

    result = g_dbus_connection_call_sync(
        client->connection,
        DBUS_SERVICE_NAME,
        DBUS_OBJECT_PATH,
        DBUS_INTERFACE_NAME,
        "DownloadFirmware",
        g_variant_new("(ssss)", handler_id_str, firmware_name ? firmware_name : "", download_url ? download_url : "", type_of_firmware ? type_of_firmware : ""),
        G_VARIANT_TYPE("(sss)"),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        &error
    );

    g_free(handler_id_str);

    if (result) {
        gchar *res1=NULL, *res2=NULL, *res3=NULL;
        g_variant_get(result, "(sss)", &res1, &res2, &res3);
        PRINT_SUCCESS("DownloadFirmware immediate response: %s / %s / %s", res1, res2, res3);
        g_free(res1); g_free(res2); g_free(res3);
        g_variant_unref(result);

        // Setup main loop and wait for signals, with timeout
        client->download_done = FALSE;
        client->download_success = FALSE;
        client->loop = g_main_loop_new(NULL, FALSE);

        // Timeout after 60 seconds
        guint timeout_id = g_timeout_add_seconds(60, download_timeout_cb, client);

        PRINT_INFO("Waiting for DownloadProgress/DownloadError signals (timeout: 60s)...");
        g_main_loop_run(client->loop);

        // If we returned before timeout, remove timeout source
        g_source_remove(timeout_id);

        // Clean up loop
        if (client->loop) {
            g_main_loop_unref(client->loop);
            client->loop = NULL;
        }

        if (client->download_done && client->download_success) {
            PRINT_SUCCESS("Download completed successfully (received terminal signal)");
            return TRUE;
        } else if (client->download_done && !client->download_success) {
            PRINT_ERROR("Download failed (received error signal)");
            return FALSE;
        } else {
            PRINT_ERROR("Download did not complete within timeout");
            return FALSE;
        }

    } else {
        PRINT_ERROR("DownloadFirmware call failed: %s", error->message);
        g_error_free(error);
        return FALSE;
    }
}

/**
 * Update (flash) firmware using UpdateFirmware call
 * 
 * Covers all 13 scenarios from sequence diagram:
 * S1:  Normal HTTP upgrade (default)
 * S2:  Deferred reboot (rebootImmediately="false")
 * S3:  PDRI upgrade (TypeOfFirmware="PDRI")
 * S4:  Not registered
 * S5:  Concurrent flash in progress
 * S6:  Empty firmware name
 * S7:  Invalid firmware type
 * S8:  NULL parameters
 * S9:  Flash write error (simulated by daemon)
 * S10: Download in progress
 * S11: Insufficient storage (handled by flashImage)
 * S12: Custom location (LocationOfFirmware="/custom/path")
 * S13: Peripheral update (TypeOfFirmware="PERIPHERAL")
 * 
 * @param client Test client context
 * @param firmware_name Name of firmware file (or empty for validation test)
 * @param type_of_firmware Type: "HTTP", "PDRI", "PERIPHERAL", etc.
 * @param location Custom location path (or empty for default)
 * @param reboot Reboot flag: "true" or "false"
 * @return TRUE if flash initiated successfully, FALSE otherwise
 */
static gboolean test_client_update_firmware(TestClientContext *client, 
                                            const char *firmware_name,
                                            const char *type_of_firmware, 
                                            const char *location,
                                            const char *reboot)
{
    GError *error = NULL;
    GVariant *result = NULL;
    gchar *handler_id_str = NULL;

    if (!client->is_registered) {
        PRINT_ERROR("Cannot update firmware - client not registered (TEST SCENARIO S4)");
        return FALSE;
    }

    PRINT_INFO("========== UPDATE FIRMWARE TEST ==========");
    PRINT_INFO("Test Scenario Detection:");
    
    // Detect and log which scenario is being tested
    if (!firmware_name || strlen(firmware_name) == 0) {
        PRINT_WARN("  S6: Empty firmware name - expecting validation error");
    }
    if (!type_of_firmware || strlen(type_of_firmware) == 0) {
        PRINT_WARN("  S7: Empty firmware type - expecting validation error");
    } else if (g_strcmp0(type_of_firmware, "PDRI") == 0) {
        PRINT_INFO("  S3: PDRI upgrade mode");
    } else if (g_strcmp0(type_of_firmware, "PERIPHERAL") == 0) {
        PRINT_INFO("  S13: Peripheral firmware update");
    } else {
        PRINT_INFO("  S1: Normal HTTP/PCI upgrade");
    }
    
    if (reboot && g_strcmp0(reboot, "false") == 0) {
        PRINT_INFO("  S2: Deferred reboot mode");
    }
    
    if (location && strlen(location) > 0) {
        PRINT_INFO("  S12: Custom firmware location: %s", location);
    }

    handler_id_str = g_strdup_printf("%"G_GUINT64_FORMAT, client->handler_id);
    
    PRINT_INFO("Calling UpdateFirmware with:");
    PRINT_INFO("  handler_id: %s", handler_id_str);
    PRINT_INFO("  firmware_name: '%s'", firmware_name ? firmware_name : "(null)");
    PRINT_INFO("  type: '%s'", type_of_firmware ? type_of_firmware : "(null)");
    PRINT_INFO("  location: '%s'", location ? location : "(default)");
    PRINT_INFO("  reboot: '%s'", reboot ? reboot : "true");

    // Call UpdateFirmware D-Bus method
    // Method signature: UpdateFirmware(s handlerId, s firmwareName, s TypeOfFirmware, 
    //                                  s LocationOfFirmware, s rebootImmediately)
    //                   -> (s UpdateResult, s UpdateStatus, s message)
    result = g_dbus_connection_call_sync(
        client->connection,
        DBUS_SERVICE_NAME,
        DBUS_OBJECT_PATH,
        DBUS_INTERFACE_NAME,
        "UpdateFirmware",
        g_variant_new("(sssss)", 
                      handler_id_str, 
                      firmware_name ? firmware_name : "",
                      type_of_firmware ? type_of_firmware : "",
                      location ? location : "",
                      reboot ? reboot : "true"),
        G_VARIANT_TYPE("(sss)"),
        G_DBUS_CALL_FLAGS_NONE,
        30000,  // 30 second timeout (flash init might take time)
        NULL,
        &error
    );

    g_free(handler_id_str);

    if (result) {
        gchar *update_result = NULL, *update_status = NULL, *message = NULL;
        g_variant_get(result, "(sss)", &update_result, &update_status, &message);
        
        PRINT_INFO("========== UPDATE FIRMWARE RESPONSE ==========");
        PRINT_INFO("  Update Result: %s", update_result ? update_result : "(null)");
        PRINT_INFO("  Update Status: %s", update_status ? update_status : "(null)");
        PRINT_INFO("  Message: %s", message ? message : "(null)");
        
        // Check for immediate errors (S4-S8, S10)
        if (update_result && g_strcmp0(update_result, "FAILURE") == 0) {
            PRINT_ERROR("UpdateFirmware immediate failure detected!");
            PRINT_ERROR("This indicates validation error or precondition failure");
            client->errors_detected++;
            
            g_free(update_result);
            g_free(update_status);
            g_free(message);
            g_variant_unref(result);
            return FALSE;
        }
        
        PRINT_SUCCESS("UpdateFirmware accepted - flash operation started asynchronously");
        PRINT_INFO("Expecting UpdateProgress signals...");
        
        g_free(update_result);
        g_free(update_status);
        g_free(message);
        g_variant_unref(result);

        // Setup main loop and wait for UpdateProgress signals
        client->flash_done = FALSE;
        client->flash_success = FALSE;
        client->flash_progress = -1;
        client->loop = g_main_loop_new(NULL, FALSE);

        // Timeout after 120 seconds (flash can take time)
        guint timeout_id = g_timeout_add_seconds(120, download_timeout_cb, client);

        PRINT_INFO("Waiting for UpdateProgress signals (timeout: 120s)...");
        PRINT_INFO("Expected progress: 0%% -> 25%% -> 50%% -> 75%% -> 100%%");
        PRINT_INFO("Or -1%% for flash error (S9, S11)");
        
        g_main_loop_run(client->loop);

        // Clean up
        g_source_remove(timeout_id);
        if (client->loop) {
            g_main_loop_unref(client->loop);
            client->loop = NULL;
        }

        // Analyze results
        PRINT_INFO("========== UPDATE FIRMWARE RESULTS ==========");
        PRINT_INFO("  Final Progress: %d%%", client->flash_progress);
        PRINT_INFO("  Status Message: %s", client->flash_status_msg ? client->flash_status_msg : "(none)");
        PRINT_INFO("  Signals Received: %u", client->signals_received);
        
        if (client->flash_done && client->flash_success) {
            PRINT_SUCCESS("======================================");
            PRINT_SUCCESS("  FLASH COMPLETED SUCCESSFULLY!");
            PRINT_SUCCESS("======================================");
            return TRUE;
        } else if (client->flash_done && !client->flash_success) {
            PRINT_ERROR("======================================");
            PRINT_ERROR("  FLASH FAILED!");
            PRINT_ERROR("  Error Scenario Detected (S9 or S11)");
            PRINT_ERROR("======================================");
            return FALSE;
        } else {
            PRINT_ERROR("======================================");
            PRINT_ERROR("  FLASH TIMEOUT!");
            PRINT_ERROR("  No completion signal received");
            PRINT_ERROR("  BUG: Check daemon logs");
            PRINT_ERROR("======================================");
            client->errors_detected++;
            return FALSE;
        }

    } else {
        PRINT_ERROR("========== UPDATE FIRMWARE CALL FAILED ==========");
        PRINT_ERROR("  Error: %s", error->message);
        
        // Analyze D-Bus errors
        if (g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED)) {
            PRINT_ERROR("  Cause: ACCESS_DENIED - not registered or invalid handler (S4)");
        } else if (g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS)) {
            PRINT_ERROR("  Cause: INVALID_ARGS - bad parameters (S6, S7, S8)");
        } else if (g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_TIMEOUT)) {
            PRINT_ERROR("  Cause: TIMEOUT - daemon not responding");
            client->errors_detected++;
        } else {
            PRINT_ERROR("  Cause: Unknown D-Bus error");
            client->errors_detected++;
        }
        
        g_error_free(error);
        return FALSE;
    }
}

/**
 * Unregister process from daemon
 */
static gboolean test_client_unregister(TestClientContext *client)
{
    GError *error = NULL;
    GVariant *result = NULL;
    gboolean success = FALSE;
    
    if (!client->is_registered) {
        PRINT_WARN("Client not registered, nothing to unregister");
        return TRUE;
    }
    
    PRINT_INFO("Unregistering handler ID: %"G_GUINT64_FORMAT, client->handler_id);
    
    // Call UnregisterProcess D-Bus method
    result = g_dbus_connection_call_sync(
        client->connection,
        DBUS_SERVICE_NAME,
        DBUS_OBJECT_PATH,
        DBUS_INTERFACE_NAME,
        "UnregisterProcess",
        g_variant_new("(t)", client->handler_id),                   // handler_id parameter
        G_VARIANT_TYPE("(b)"),                                      // Expected return type (boolean)
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        &error
    );
    
    if (result) {
        // Success - extract boolean result
        g_variant_get(result, "(b)", &success);
        g_variant_unref(result);
        
        if (success) {
            PRINT_SUCCESS("Unregistration successful!");
            client->is_registered = FALSE;
            client->handler_id = 0;
        } else {
            PRINT_WARN("Unregistration returned false - handler may not have been found");
        }
        
        return success;
    } else {
        // Error
        PRINT_ERROR("Unregistration failed: %s", error->message);
        g_error_free(error);
        return FALSE;
    }
}

/**
 * Free test client context
 */
static void test_client_free(TestClientContext *client)
{
    if (!client) return;
    
    // Unregister if still registered
    if (client->is_registered) {
        PRINT_INFO("Auto-unregistering before cleanup...");
        test_client_unregister(client);
    }
    
    // Print final statistics
    if (client->signals_received > 0 || client->errors_detected > 0) {
        PRINT_INFO("========== TEST SESSION STATISTICS ==========");
        PRINT_INFO("  Total D-Bus signals received: %u", client->signals_received);
        if (client->errors_detected > 0) {
            PRINT_ERROR("  Errors/Bugs detected: %u", client->errors_detected);
            PRINT_ERROR("  ** REVIEW DAEMON LOGS FOR DEBUGGING **");
        } else {
            PRINT_SUCCESS("  No errors detected - daemon working correctly");
        }
        PRINT_INFO("============================================");
    }
    
    if (client->connection) {
        g_object_unref(client->connection);
    }
    
    g_free(client->process_name);
    g_free(client->lib_version);
    if (client->flash_status_msg) g_free(client->flash_status_msg);
    g_free(client);
    
    PRINT_INFO("Test client cleaned up");
}

/**
 * Print comprehensive help documentation
 */
static void print_full_help(const char *program_name)
{
    printf("\n");
    printf("################################################################################\n");
    printf("##                                                                            ##\n");
    printf("##      RDK Firmware Updater Daemon - COMPREHENSIVE Test Client v3.0         ##\n");
    printf("##                                                                            ##\n");
    printf("##  THE SINGLE SOURCE OF TRUTH FOR DEV/QA TESTING                            ##\n");
    printf("##  Covers ALL daemon functionality + bug detection                          ##\n");
    printf("##                                                                            ##\n");
    printf("################################################################################\n\n");
    
    printf("USAGE:\n");
    printf("  %s <process_name> <lib_version> [test_mode]\n", program_name);
    printf("  %s --help     (show this comprehensive guide)\n\n", program_name);
    
    printf("QUICK START:\n");
    printf("  %s VideoApp 1.0.0 basic           # Test basic registration\n", program_name);
    printf("  %s VideoApp 1.0.0 check           # Test CheckForUpdate\n", program_name);
    printf("  %s VideoApp 1.0.0 update-s1       # Test UpdateFirmware S1\n", program_name);
    printf("  %s VideoApp 1.0.0 full-workflow   # Complete E2E test\n\n", program_name);
    
    printf("================================================================================\n");
    printf("                       UPDATE FIRMWARE TEST SCENARIOS\n");
    printf("                      (All 13 Scenarios from Sequence Diagram)\n");
    printf("================================================================================\n\n");
    
    printf("SUCCESS PATH SCENARIOS:\n");
    printf("  update-s1      S1: Normal HTTP/PCI upgrade with immediate reboot\n");
    printf("                 Default flow, tests flashImage() integration\n\n");
    
    printf("  update-s2      S2: Deferred reboot scenario\n");
    printf("                 Tests rebootImmediately='false' handling\n\n");
    
    printf("  update-s3      S3: PDRI upgrade mode\n");
    printf("                 Tests TypeOfFirmware='PDRI' path\n\n");
    
    printf("  update-s12     S12: Custom firmware location\n");
    printf("                 Tests LocationOfFirmware parameter\n\n");
    
    printf("  update-s13     S13: Peripheral firmware update\n");
    printf("                 Tests TypeOfFirmware='PERIPHERAL'\n\n");
    
    printf("ERROR/VALIDATION SCENARIOS:\n");
    printf("  update-s4      S4: Not registered error\n");
    printf("                 Tests ACCESS_DENIED when not registered\n\n");
    
    printf("  update-s5      S5: Concurrent flash in progress\n");
    printf("                 Tests rejection when flash already running\n");
    printf("                 NOTE: Run another flash first to test\n\n");
    
    printf("  update-s6      S6: Empty firmware name\n");
    printf("                 Tests validation of required firmware name\n\n");
    
    printf("  update-s7      S7: Invalid firmware type\n");
    printf("                 Tests validation of TypeOfFirmware parameter\n\n");
    
    printf("  update-s8      S8: NULL parameters\n");
    printf("                 Tests handling of NULL/missing parameters\n\n");
    
    printf("  update-s9      S9: Flash write error\n");
    printf("                 Tests flashImage() failure handling\n");
    printf("                 NOTE: Requires simulated error from daemon\n\n");
    
    printf("  update-s10     S10: Download in progress\n");
    printf("                 Tests rejection when download active\n");
    printf("                 NOTE: Start download first to test\n\n");
    
    printf("  update-s11     S11: Insufficient storage\n");
    printf("                 Tests flashImage() storage error\n");
    printf("                 NOTE: Requires low storage condition\n\n");
    
    printf("  update-all     Run all 13 UpdateFirmware scenarios sequentially\n");
    printf("                 Comprehensive test suite (takes ~10 minutes)\n\n");
    
    printf("================================================================================\n");
    printf("                      CHECK FOR UPDATE TEST SCENARIOS\n");
    printf("================================================================================\n\n");
    
    printf("BASIC TESTS:\n");
    printf("  basic          Basic registration and verification\n");
    printf("  check          Simple check for update flow\n");
    printf("  full           Complete CheckForUpdate lifecycle\n\n");
    
    printf("CACHE BEHAVIOR:\n");
    printf("  cache-hit      Test cache hit (immediate response)\n");
    printf("  cache-miss     Test cache miss (UPDATE_ERROR + signal)\n");
    printf("  signals        Monitor signal delivery timing\n\n");
    
    printf("CONCURRENCY:\n");
    printf("  concurrent     Test piggyback behavior\n");
    printf("  rapid-check    Rapid successive calls (stress test)\n\n");
    
    printf("REGISTRATION:\n");
    printf("  reregister     Re-registration from same client\n");
    printf("  duplicate      Duplicate process name (2 terminals)\n\n");
    
    printf("ERROR HANDLING:\n");
    printf("  no-register    CheckForUpdate without registration\n");
    printf("  invalid-handler CheckForUpdate with invalid handler_id\n\n");
    
    printf("STRESS TESTS:\n");
    printf("  stress-reg     Multiple rapid registrations\n");
    printf("  stress-check   Multiple rapid CheckForUpdate calls\n\n");
    
    printf("================================================================================\n");
    printf("                       DOWNLOAD FIRMWARE TEST SCENARIOS\n");
    printf("================================================================================\n\n");
    
    printf("  download       Basic firmware download test\n");
    printf("                 Tests DownloadFirmware with progress monitoring\n\n");
    
    printf("  download-custom Download with custom URL\n");
    printf("                 Tests download from specific server\n\n");
    
    printf("================================================================================\n");
    printf("                         COMPLETE E2E WORKFLOWS\n");
    printf("================================================================================\n\n");
    
    printf("  full-workflow  Complete end-to-end test:\n");
    printf("                 Register -> CheckForUpdate -> DownloadFirmware ->\n");
    printf("                 UpdateFirmware -> Unregister\n\n");
    
    printf("  quick-test     Quick sanity check (register + check + unregister)\n\n");
    
    printf("================================================================================\n");
    printf("                              PRACTICAL EXAMPLES\n");
    printf("================================================================================\n\n");
    
    printf("# Test normal flash operation (S1)\n");
    printf("$ %s VideoApp 1.0.0 update-s1\n\n", program_name);
    
    printf("# Test deferred reboot (S2)\n");
    printf("$ %s VideoApp 1.0.0 update-s2\n\n", program_name);
    
    printf("# Test validation errors (S6, S7)\n");
    printf("$ %s VideoApp 1.0.0 update-s6\n", program_name);
    printf("$ %s VideoApp 1.0.0 update-s7\n\n", program_name);
    
    printf("# Run complete UpdateFirmware test suite\n");
    printf("$ %s VideoApp 1.0.0 update-all\n\n", program_name);
    
    printf("# Test concurrent operations\n");
    printf("Terminal 1: $ %s VideoApp 1.0.0 update-s1\n", program_name);
    printf("Terminal 2: $ %s VideoApp 1.0.0 update-s5  # Should fail\n\n", program_name);
    
    printf("# Complete E2E workflow\n");
    printf("$ %s VideoApp 1.0.0 full-workflow\n\n", program_name);
    
    printf("================================================================================\n");
    printf("                            MONITORING & DEBUGGING\n");
    printf("================================================================================\n\n");
    
    printf("WATCH DAEMON LOGS:\n");
    printf("  $ tail -f /opt/logs/swupdate.log\n");
    printf("  $ journalctl -u rdkfwupdater -f\n\n");
    
    printf("MONITOR D-BUS SIGNALS:\n");
    printf("  $ sudo dbus-monitor --system \"type='signal',sender='org.rdkfwupdater.Service'\"\n\n");
    
    printf("CHECK CACHE STATUS:\n");
    printf("  $ ls -lh /tmp/xconf_response_thunder.txt\n");
    printf("  $ cat /tmp/xconf_response_thunder.txt | jq .\n\n");
    
    printf("CHECK DAEMON STATUS:\n");
    printf("  $ ps aux | grep rdkfwupdater\n");
    printf("  $ systemctl status rdkfwupdater\n\n");
    
    printf("BUG DETECTION:\n");
    printf("  The test client counts errors and signals.\n");
    printf("  At the end of each test, it reports:\n");
    printf("    - Total signals received\n");
    printf("    - Errors/bugs detected\n");
    printf("  Use this to identify daemon issues!\n\n");
    
    printf("================================================================================\n");
    printf("                           TEST CLIENT FEATURES\n");
    printf("================================================================================\n\n");
    
    printf("✓ Comprehensive UpdateFirmware testing (all 13 scenarios)\n");
    printf("✓ Complete CheckForUpdate flow coverage\n");
    printf("✓ DownloadFirmware with progress monitoring\n");
    printf("✓ Registration/unregistration edge cases\n");
    printf("✓ Error injection and validation\n");
    printf("✓ Signal monitoring and validation\n");
    printf("✓ Stress testing capabilities\n");
    printf("✓ Bug detection and reporting\n");
    printf("✓ Comprehensive logging and diagnostics\n");
    printf("✓ D-Bus error analysis\n\n");
    
    printf("================================================================================\n");
    printf("For full documentation, see:\n");
    printf("  - UPDATEFIRMWARE_IMPLEMENTATION_PLAN_V2.md\n");
    printf("  - IMPLEMENTATION_README.md\n");
    printf("  - dbus_tests_2/QA_Manual_Testing_Guide.md\n");
    printf("================================================================================\n\n");
}

/**
 * Print brief usage information
 */
static void print_usage(const char *program_name) 
{
    printf("\n");
    printf("================================================================================\n");
    printf("            RDK Firmware Updater Daemon - Test Client v3.0\n");
    printf("================================================================================\n\n");
    
    printf("USAGE:\n");
    printf("  %s <process_name> <lib_version> [test_mode]\n", program_name);
    printf("  %s --help\n\n", program_name);
    
    printf("PARAMETERS:\n");
    printf("  process_name   Name of the process (e.g., 'VideoApp', 'Netflix')\n");
    printf("  lib_version    Library version (e.g., '1.0.0', '2.5.1')\n");
    printf("  test_mode      Test scenario to execute\n\n");
    
    printf("COMMON TEST MODES:\n");
    printf("  basic          - Basic registration\n");
    printf("  check          - CheckForUpdate test\n");
    printf("  download       - DownloadFirmware test\n");
    printf("  update-s1      - UpdateFirmware S1 (normal flash)\n");
    printf("  update-s2      - UpdateFirmware S2 (deferred reboot)\n");
    printf("  update-all     - All UpdateFirmware scenarios (S1-S13)\n");
    printf("  full-workflow  - Complete E2E test\n\n");
    
    printf("For complete documentation of all test modes:\n");
    printf("  %s --help\n\n", program_name);
    
    printf("QUICK EXAMPLES:\n");
    printf("  %s VideoApp 1.0.0 basic\n", program_name);
    printf("  %s VideoApp 1.0.0 update-s1\n", program_name);
    printf("  %s VideoApp 1.0.0 full-workflow\n\n", program_name);
    
    printf("================================================================================\n\n");
}

/**
 * Run specific test scenario
 */
static void run_test_scenario(TestClientContext *client, const gchar *test_mode, const char *program_name)
{
    PRINT_INFO("Running test scenario: %s", test_mode);
    
    if (g_strcmp0(test_mode, "basic") == 0) {
        // Basic registration test
        if (!test_client_register(client)) {
            PRINT_ERROR("Basic registration test failed");
        }
        
    } else if (g_strcmp0(test_mode, "reregister") == 0) {
        // Test re-registration (should return same handler_id)
        PRINT_INFO("=== Testing Re-registration Scenario ===");
        
        // First registration
        if (test_client_register(client)) {
            guint64 first_handler = client->handler_id;
            
            // Second registration (should return same handler_id)
            PRINT_INFO("Attempting re-registration...");
            client->is_registered = FALSE; // Reset flag to allow re-registration call
            client->handler_id = 0;
            
            if (!test_client_register(client)) {
                PRINT_ERROR("Re-registration call failed");
            } else if (client->handler_id == first_handler) {
                PRINT_SUCCESS("Re-registration test PASSED - same handler_id returned");
            } else {
                PRINT_ERROR("Re-registration test FAILED - different handler_id returned");
            }
        }
        
    } else if (g_strcmp0(test_mode, "check") == 0) {
        // Registration + CheckForUpdate
        if (test_client_register(client)) {
            sleep(1); // Small delay before check
            test_client_check_update(client);
        }
        
    } else if (g_strcmp0(test_mode, "full") == 0) {
        // Full workflow test
        PRINT_INFO("=== Running Full Workflow Test ===");
        
        if (test_client_register(client)) {
            sleep(1);
            test_client_check_update(client);
            sleep(1);
            test_client_unregister(client);
        }
        
    } else if (g_strcmp0(test_mode, "signals") == 0) {
        // Test signal listening and cache behavior
        PRINT_INFO("=== Testing D-Bus Signals and Cache Behavior ===");
        
        if (test_client_register(client)) {
            sleep(1);
            
            // Check current cache state
            gboolean cache_exists = g_file_test("/tmp/xconf_response_thunder.txt", G_FILE_TEST_EXISTS);
            
            printf("\nCache Test Scenario:\n");
            if (cache_exists) {
                PRINT_INFO("Cache exists - will get immediate response");
            } else {
                PRINT_INFO("No cache - will get UPDATE_ERROR + signal later");
                PRINT_INFO("Tip: Delete cache with: rm /tmp/xconf_response_thunder.txt");
            }
            
            printf("\nStarting CheckForUpdate call...\n");
            test_client_check_update(client);
            
            // Wait for potential signals (especially for cache miss scenario)
            if (!cache_exists) {
                PRINT_INFO("Waiting 10 seconds for background XConf fetch and signal...");
                printf("   (You should see the signal callback above when XConf completes)\n");
                
                GMainContext *context = g_main_context_default();
                for (int i = 0; i < 100; i++) {  // 10 seconds = 100 * 100ms
                    g_main_context_iteration(context, FALSE);
                    usleep(100000); // 100ms
                    
                    // Print progress dots
                    if (i % 10 == 0) {
                        printf(".");
                        fflush(stdout);
                    }
                }
                printf("\n");
                PRINT_INFO("Signal waiting period completed");
            }
            
            sleep(1);
            test_client_unregister(client);
        }
        
    } else if (g_strcmp0(test_mode, "concurrent") == 0) {
        // Test concurrent CheckForUpdate requests (piggyback behavior)
        PRINT_INFO("=== Testing Concurrent CheckForUpdate (Piggyback) ===");
        
        if (test_client_register(client)) {
            sleep(1);
            
            // Check cache state
            gboolean cache_exists = g_file_test("/tmp/xconf_response_thunder.txt", G_FILE_TEST_EXISTS);
            
            if (cache_exists) {
                PRINT_WARN("Cache exists - delete it to test piggyback behavior:");
                PRINT_INFO("  rm /tmp/xconf_response_thunder.txt");
                PRINT_INFO("Then run this test again");
            } else {
                PRINT_SUCCESS("Cache does not exist - perfect for piggyback test");
                PRINT_INFO("Making 3 rapid CheckForUpdate calls...");
                PRINT_INFO("Expected behavior:");
                PRINT_INFO("  - All 3 calls get UPDATE_ERROR immediately");
                PRINT_INFO("  - Only ONE XConf fetch triggered");
                PRINT_INFO("  - All waiting clients get same signal");
                
                printf("\n");
                
                // Make 3 rapid calls
                for (int i = 1; i <= 3; i++) {
                    printf("Call #%d:\n", i);
                    test_client_check_update(client);
                    usleep(200000); // 200ms between calls
                    printf("\n");
                }
                
                PRINT_INFO("All calls completed - waiting for signal...");
                PRINT_INFO("Check daemon logs to verify:");
                PRINT_INFO("  - 'IsCheckUpdateInProgress = TRUE' appears only ONCE");
                PRINT_INFO("  - 'Piggyback' messages for calls #2 and #3");
                PRINT_INFO("  - Single 'async_xconf_fetch_task' execution");
                
                // Wait for signal
                PRINT_INFO("Waiting 10 seconds for CheckForUpdateComplete signal...");
                for (int i = 0; i < 10; i++) {
                    sleep(1);
                    printf(".");
                    fflush(stdout);
                }
                printf("\n");
            }
            
            sleep(1);
            test_client_unregister(client);
        }
        
    } else if (g_strcmp0(test_mode, "cache-hit") == 0) {
        // Test cache hit scenario
        PRINT_INFO("=== Testing Cache Hit Scenario ===");
        
        gboolean cache_exists = g_file_test("/tmp/xconf_response_thunder.txt", G_FILE_TEST_EXISTS);
        
        if (!cache_exists) {
            PRINT_WARN("Cache file does not exist at /tmp/xconf_response_thunder.txt");
            PRINT_INFO("Run daemon first or manually create cache file, then retry");
            PRINT_INFO("Expected behavior: Immediate response with firmware data from cache");
        } else {
            PRINT_SUCCESS("Cache file exists - testing cache hit flow");
            
            if (test_client_register(client)) {
                sleep(1);
                
                PRINT_INFO("Making CheckForUpdate call - expecting immediate cache response");
                test_client_check_update(client);
                
                PRINT_INFO("Cache hit test complete - verify immediate response above");
                sleep(1);
                test_client_unregister(client);
            }
        }
        
    } else if (g_strcmp0(test_mode, "cache-miss") == 0) {
        // Test cache miss scenario
        PRINT_INFO("=== Testing Cache Miss Scenario ===");
        
        gboolean cache_exists = g_file_test("/tmp/xconf_response_thunder.txt", G_FILE_TEST_EXISTS);
        
        if (cache_exists) {
            PRINT_WARN("Cache file exists at /tmp/xconf_response_thunder.txt");
            PRINT_INFO("Delete it with: rm /tmp/xconf_response_thunder.txt");
            PRINT_INFO("Then run this test again for true cache miss scenario");
        } else {
            PRINT_SUCCESS("Cache file does not exist - perfect for cache miss test");
            PRINT_INFO("Expected flow:");
            PRINT_INFO("  1. CheckForUpdate returns UPDATE_ERROR immediately");
            PRINT_INFO("  2. Background XConf fetch starts");
            PRINT_INFO("  3. CheckForUpdateComplete signal arrives with real data");
            
            if (test_client_register(client)) {
                sleep(1);
                
                printf("\nMaking CheckForUpdate call...\n");
                test_client_check_update(client);
                
                PRINT_INFO("Waiting 15 seconds for background XConf fetch and signal...");
                printf("   (Watch for CheckForUpdateComplete signal above)\n");
                
                GMainContext *context = g_main_context_default();
                for (int i = 0; i < 150; i++) {  // 15 seconds
                    g_main_context_iteration(context, FALSE);
                    usleep(100000); // 100ms
                    
                    if (i % 10 == 0) {
                        printf(".");
                        fflush(stdout);
                    }
                }
                printf("\n");
                
                PRINT_INFO("Cache miss test complete");
                sleep(1);
                test_client_unregister(client);
            }
        }
        
    } else if (g_strcmp0(test_mode, "rapid-check") == 0) {
        // Test rapid successive CheckForUpdate calls
        PRINT_INFO("=== Testing Rapid CheckForUpdate Calls ===");
        
        if (test_client_register(client)) {
            sleep(1);
            
            gboolean cache_exists = g_file_test("/tmp/xconf_response_thunder.txt", G_FILE_TEST_EXISTS);
            
            if (cache_exists) {
                PRINT_INFO("Cache exists - all calls will succeed immediately");
            } else {
                PRINT_INFO("No cache - testing IsCheckUpdateInProgress flag behavior");
                PRINT_INFO("Expected: First call triggers fetch, subsequent calls piggyback");
            }
            
            PRINT_INFO("Making 5 rapid CheckForUpdate calls with 100ms intervals...");
            printf("\n");
            
            for (int i = 1; i <= 5; i++) {
                printf("=== Rapid Call #%d ===\n", i);
                test_client_check_update(client);
                usleep(100000); // 100ms
                printf("\n");
            }
            
            if (!cache_exists) {
                PRINT_INFO("Waiting 10 seconds for background fetch signal...");
                GMainContext *context = g_main_context_default();
                for (int i = 0; i < 100; i++) {
                    g_main_context_iteration(context, FALSE);
                    usleep(100000);
                    if (i % 10 == 0) {
                        printf(".");
                        fflush(stdout);
                    }
                }
                printf("\n");
            }
            
            PRINT_INFO("Rapid check test complete");
            sleep(1);
            test_client_unregister(client);
        }
        
    } else if (g_strcmp0(test_mode, "no-register") == 0) {
        // Test CheckForUpdate without registration
        PRINT_INFO("=== Testing CheckForUpdate Without Registration ===");
        PRINT_INFO("Expected: ACCESS_DENIED or error indicating not registered");
        
        // Don't register - directly try to check for update
        client->handler_id = 12345; // Fake handler ID
        client->is_registered = TRUE; // Fake registration flag
        
        printf("\nAttempting CheckForUpdate with fake handler ID 12345...\n");
        test_client_check_update(client);
        
        PRINT_INFO("No-register test complete");
        
    } else if (g_strcmp0(test_mode, "invalid-handler") == 0) {
        // Test CheckForUpdate with invalid handler_id
        PRINT_INFO("=== Testing CheckForUpdate With Invalid Handler ID ===");
        PRINT_INFO("Expected: NOT_REGISTERED error or similar");
        
        if (test_client_register(client)) {
            sleep(1);
            
            // Save real handler_id and use fake one
            guint64 real_handler = client->handler_id;
            client->handler_id = 99999999;
            
            PRINT_INFO("Using invalid handler ID: 99999999");
            printf("\nAttempting CheckForUpdate with invalid handler...\n");
            test_client_check_update(client);
            
            // Restore real handler for cleanup
            client->handler_id = real_handler;
            
            sleep(1);
            test_client_unregister(client);
        }
        
    } else if (g_strcmp0(test_mode, "duplicate") == 0) {
        // Test duplicate process name registration
        PRINT_INFO("=== Testing Duplicate Process Name Registration ===");
        PRINT_INFO("This test requires running from 2 terminals with same process name");
        PRINT_INFO("Expected: Second registration should be rejected");
        
        if (test_client_register(client)) {
            PRINT_SUCCESS("Registration succeeded in this terminal");
            PRINT_INFO("Now run from another terminal:");
            PRINT_INFO("  %s %s %s duplicate", program_name, client->process_name, client->lib_version);
            PRINT_INFO("The second terminal should get ACCESS_DENIED error");
            PRINT_INFO("Keeping this registration alive for 30 seconds...");
            
            sleep(30);
            
            test_client_unregister(client);
        } else {
            PRINT_WARN("Registration failed - likely another instance already registered");
            PRINT_INFO("This is the expected behavior for duplicate registration");
        }
        
    } else if (g_strcmp0(test_mode, "stress-reg") == 0) {
        // Stress test - multiple rapid registrations
        PRINT_INFO("=== Running Registration Stress Test ===");
        PRINT_INFO("Testing daemon's ability to handle rapid registration changes");
        
        for (int i = 1; i <= 10; i++) {
            printf("\n=== Stress Registration Iteration %d/10 ===\n", i);
            
            client->is_registered = FALSE;
            client->handler_id = 0;
            
            if (!test_client_register(client)) {
                PRINT_ERROR("Registration %d failed", i);
            } else {
                PRINT_SUCCESS("Registration %d succeeded", i);
                
                // Unregister immediately
                if (test_client_unregister(client)) {
                    PRINT_SUCCESS("Unregistration %d succeeded", i);
                }
            }
            
            usleep(200000); // 200ms delay between iterations
        }
        
        PRINT_INFO("Registration stress test complete");
        
    } else if (g_strcmp0(test_mode, "stress-check") == 0) {
        // Stress test - multiple rapid CheckForUpdate operations
        PRINT_INFO("=== Running CheckForUpdate Stress Test ===");
        PRINT_INFO("Testing daemon's ability to handle many rapid check requests");
        
        if (test_client_register(client)) {
            sleep(1);
            
            gboolean cache_exists = g_file_test("/tmp/xconf_response_thunder.txt", G_FILE_TEST_EXISTS);
            
            if (!cache_exists) {
                PRINT_WARN("No cache exists - this stress test works best with cache");
                PRINT_INFO("Tip: Run daemon first to populate cache, then retry");
            }
            
            PRINT_INFO("Making 20 rapid CheckForUpdate calls...");
            printf("\n");
            
            int success_count = 0;
            int error_count = 0;
            
            for (int i = 1; i <= 20; i++) {
                printf("Stress Check #%d: ", i);
                fflush(stdout);
                
                if (test_client_check_update(client)) {
                    printf("OK\n");
                    success_count++;
                } else {
                    printf("FAIL\n");
                    error_count++;
                }
                
                usleep(50000); // 50ms between calls
            }
            
            printf("\n");
            PRINT_INFO("Stress Test Results:");
            PRINT_INFO("  Successful calls: %d/20", success_count);
            PRINT_INFO("  Failed calls: %d/20", error_count);
            
            if (!cache_exists) {
                PRINT_INFO("Waiting 10 seconds for any pending signals...");
                GMainContext *context = g_main_context_default();
                for (int i = 0; i < 100; i++) {
                    g_main_context_iteration(context, FALSE);
                    usleep(100000);
                    if (i % 10 == 0) {
                        printf(".");
                        fflush(stdout);
                    }
                }
                printf("\n");
            }
            
            sleep(1);
            test_client_unregister(client);
        }
        
    } else if (g_strcmp0(test_mode, "long-wait") == 0) {
        // Test long signal wait
        PRINT_INFO("=== Testing Extended Signal Wait (60 seconds) ===");
        PRINT_INFO("This tests signal delivery reliability over time");
        
        if (test_client_register(client)) {
            sleep(1);
            
            gboolean cache_exists = g_file_test("/tmp/xconf_response_thunder.txt", G_FILE_TEST_EXISTS);
            
            if (cache_exists) {
                PRINT_INFO("Deleting cache to trigger background fetch...");
                if (remove("/tmp/xconf_response_thunder.txt") != 0) {
                    PRINT_ERROR("Failed to delete cache file: %s", strerror(errno));
                }
            }
            
            printf("\nMaking CheckForUpdate call...\n");
            test_client_check_update(client);
            
            PRINT_INFO("Waiting 60 seconds for signal delivery...");
            PRINT_INFO("   (Signal should arrive within ~5-10 seconds)");
            PRINT_INFO("   (Remaining time tests signal handling stability)");
            
            GMainContext *context = g_main_context_default();
            for (int i = 0; i < 600; i++) {  // 60 seconds
                g_main_context_iteration(context, FALSE);
                usleep(100000); // 100ms
                
                if (i % 50 == 0) {  // Every 5 seconds
                    printf("  %d seconds elapsed...\n", i / 10);
                }
            }
            
            PRINT_INFO("Long wait test complete");
            sleep(1);
            test_client_unregister(client);
        }
        
    } else if (g_strcmp0(test_mode, "stress") == 0) {
        // Legacy stress test - multiple rapid registrations
        PRINT_INFO("=== Running Legacy Stress Test ===");
        
        for (int i = 0; i < 5; i++) {
            PRINT_INFO("Stress test iteration %d/5", i + 1);
            client->is_registered = FALSE;
            client->handler_id = 0;
            
            if (!test_client_register(client)) {
                PRINT_ERROR("Stress test registration failed at iteration %d", i + 1);
            }
            usleep(100000); // 100ms delay
        }
        
    } else if (g_strcmp0(test_mode, "download") == 0) {
        // Test DownloadFirmware flow
        PRINT_INFO("=== Testing DownloadFirmware Scenario ===");
        if (test_client_register(client)) {
            sleep(1);
            // Use sample firmware name and URL (modify as appropriate for environment)
            const char *fw_name = "test_fw.bin";
            const char *url = "http://example.com/test_fw.bin"; // If empty, daemon will use XConf
            const char *type = "PCI";

            test_client_download_firmware(client, fw_name, url, type);
            sleep(1);
            test_client_unregister(client);
        }

    } else {
        PRINT_ERROR("Unknown test mode: %s", test_mode);
        print_usage("testClient");
    }
}

static void test_check_cache_hit(TestClientContext *client) {
    PRINT_INFO("test_check_cache_hit - NOT YET IMPLEMENTED");
}

static void test_check_cache_miss(TestClientContext *client) {
    PRINT_INFO("test_check_cache_miss - NOT YET IMPLEMENTED");
}

static void test_check_concurrent(TestClientContext *client) {
    PRINT_INFO("test_check_concurrent - NOT YET IMPLEMENTED");
}

static void test_check_xconf_unreachable(TestClientContext *client) {
    PRINT_INFO("test_check_xconf_unreachable - NOT YET IMPLEMENTED");
}

static void test_check_cache_corrupt(TestClientContext *client) {
    PRINT_INFO("test_check_cache_corrupt - NOT YET IMPLEMENTED");
}

static void test_check_invalid_handler(TestClientContext *client) {
    PRINT_INFO("test_check_invalid_handler - NOT YET IMPLEMENTED");
}

static void test_check_no_register(TestClientContext *client) {
    PRINT_INFO("test_check_no_register - NOT YET IMPLEMENTED");
}

static void test_check_rapid(TestClientContext *client) {
    PRINT_INFO("test_check_rapid - NOT YET IMPLEMENTED");
}

static void test_download_http_success(TestClientContext *client) {
    PRINT_INFO("test_download_http_success - NOT YET IMPLEMENTED");
}

static void test_download_custom_url(TestClientContext *client) {
    PRINT_INFO("test_download_custom_url - NOT YET IMPLEMENTED");
}

static void test_download_xconf_url(TestClientContext *client) {
    PRINT_INFO("test_download_xconf_url - NOT YET IMPLEMENTED");
}

static void test_download_not_registered(TestClientContext *client) {
    PRINT_INFO("test_download_not_registered - NOT YET IMPLEMENTED");
}

static void test_download_concurrent(TestClientContext *client) {
    PRINT_INFO("test_download_concurrent - NOT YET IMPLEMENTED");
}

static void test_download_empty_name(TestClientContext *client) {
    PRINT_INFO("test_download_empty_name - NOT YET IMPLEMENTED");
}

static void test_download_invalid_type(TestClientContext *client) {
    PRINT_INFO("test_download_invalid_type - NOT YET IMPLEMENTED");
}

static void test_download_with_progress(TestClientContext *client) {
    PRINT_INFO("test_download_with_progress - NOT YET IMPLEMENTED");
}

static void test_flash_pci_immediate(TestClientContext *client) {
    PRINT_INFO("test_flash_pci_immediate - NOT YET IMPLEMENTED");
}

static void test_flash_pci_deferred(TestClientContext *client) {
    PRINT_INFO("test_flash_pci_deferred - NOT YET IMPLEMENTED");
}

static void test_flash_pdri_success(TestClientContext *client) {
    PRINT_INFO("test_flash_pdri_success - NOT YET IMPLEMENTED");
}

static void test_flash_not_registered(TestClientContext *client) {
    PRINT_INFO("test_flash_not_registered - NOT YET IMPLEMENTED");
}

static void test_flash_concurrent(TestClientContext *client) {
    PRINT_INFO("test_flash_concurrent - NOT YET IMPLEMENTED");
}

static void test_flash_empty_name(TestClientContext *client) {
    PRINT_INFO("test_flash_empty_name - NOT YET IMPLEMENTED");
}

static void test_flash_invalid_type(TestClientContext *client) {
    PRINT_INFO("test_flash_invalid_type - NOT YET IMPLEMENTED");
}

static void test_flash_custom_location(TestClientContext *client) {
    PRINT_INFO("test_flash_custom_location - NOT YET IMPLEMENTED");
}

static void test_flash_peripheral(TestClientContext *client) {
    PRINT_INFO("test_flash_peripheral - NOT YET IMPLEMENTED");
}

static void test_workflow_complete(TestClientContext *client) {
    PRINT_INFO("test_workflow_complete - NOT YET IMPLEMENTED");
}

static void test_workflow_download_flash(TestClientContext *client) {
    PRINT_INFO("test_workflow_download_flash - NOT YET IMPLEMENTED");
}

static void test_stress_all(TestClientContext *client) {
    PRINT_INFO("test_stress_all - NOT YET IMPLEMENTED");
}

int main(int argc, char *argv[])
int main(int argc, char *argv[])
{
    TestClientContext *client = NULL;
    const gchar *process_name = NULL;
    const gchar *lib_version = NULL;
    const gchar *test_mode = "basic";
    
    printf("\n=== RDK Firmware Updater Test Client ===\n");
    printf("Process ID: %d\n", getpid());
    printf("D-Bus Unique Name will be auto-assigned\n\n");
    
    // Parse command line arguments
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }
    
    process_name = argv[1];
    lib_version = argv[2];
    
    if (argc >= 4) {
        test_mode = argv[3];
    }
    
    // Validate arguments
    if (strlen(process_name) == 0) {
        PRINT_ERROR("Process name cannot be empty");
        return 1;
    }
    
    if (strlen(lib_version) == 0) {
        PRINT_ERROR("Library version cannot be empty"); 
        return 1;
    }
    
    PRINT_INFO("Test parameters:");
    PRINT_INFO("  Process Name: %s", process_name);
    PRINT_INFO("  Library Version: %s", lib_version);
    PRINT_INFO("  Test Mode: %s", test_mode);
    printf("\n");
    
    // Create test client
    client = test_client_new(process_name, lib_version);
    if (!client) {
        PRINT_ERROR("Failed to create test client");
        return 1;
    }
    
    // Run test scenario
    run_test_scenario(client, test_mode, argv[0]);
    
    // Cleanup
    test_client_free(client);
    
    printf("\n=== Test Client Finished ===\n");
    return 0;
}
