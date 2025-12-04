/*
 * testClient.c - RDK Firmware Updater Daemon Test Client
 * 
 * This test client demonstrates all registration scenarios for robust testing
 * of the daemon's enhanced process registration logic.
 * 
 * Usage:
 *   ./testClient <process_name> <lib_version> [test_mode]
 * 
 * Test Modes:
 *   basic      - Basic registration and operations
 *   reregister - Test same client re-registration  
 *   check      - Register and perform CheckForUpdate
 *   full       - Full workflow test
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <gio/gio.h>

// Color codes removed for clean output

// D-Bus service details
#define DBUS_SERVICE_NAME "org.rdkfwupdater.Service"
#define DBUS_OBJECT_PATH "/org/rdkfwupdater/Service"
#define DBUS_INTERFACE_NAME "org.rdkfwupdater.Interface"

// Test client context structure
typedef struct {
    GDBusConnection *connection;
    gchar *process_name;
    gchar *lib_version;
    guint64 handler_id;
    gboolean is_registered;
} TestClientContext;

// Function prototypes
static TestClientContext* test_client_new(const gchar *process_name, const gchar *lib_version);
static gboolean test_client_register(TestClientContext *client);
static gboolean test_client_check_update(TestClientContext *client);
static gboolean test_client_unregister(TestClientContext *client);
static void test_client_free(TestClientContext *client);
static void print_usage(const char *program_name);
static void run_test_scenario(TestClientContext *client, const gchar *test_mode, const char *program_name);

// Utility macros for output
#define PRINT_SUCCESS(fmt, ...) printf("[SUCCESS] " fmt "\n", ##__VA_ARGS__)
#define PRINT_ERROR(fmt, ...)   printf("[ERROR] " fmt "\n", ##__VA_ARGS__)
#define PRINT_INFO(fmt, ...)    printf("[INFO] " fmt "\n", ##__VA_ARGS__)
#define PRINT_WARN(fmt, ...)    printf("[WARN] " fmt "\n", ##__VA_ARGS__)

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
    
    if (client->connection) {
        g_object_unref(client->connection);
    }
    
    g_free(client->process_name);
    g_free(client->lib_version);
    g_free(client);
    
    PRINT_INFO("Test client cleaned up");
}

/**
 * Print usage information
 */
static void print_usage(const char *program_name) 
{
    printf("\n");
    printf("================================================================================\n");
    printf("            RDK Firmware Updater Daemon - Test Client v2.0\n");
    printf("================================================================================\n\n");
    
    printf("USAGE:\n");
    printf("  %s <process_name> <lib_version> [test_mode]\n\n", program_name);
    
    printf("PARAMETERS:\n");
    printf("  process_name   Name of the process (e.g., 'VideoApp', 'Netflix')\n");
    printf("  lib_version    Library version (e.g., '1.0.0', '2.5.1')\n");
    printf("  test_mode      Test scenario to execute (see below)\n\n");
    
    printf("================================================================================\n");
    printf("TEST SCENARIOS - CheckForUpdate Flow Robustness\n");
    printf("================================================================================\n\n");
    
    printf("BASIC TESTS:\n");
    printf("  basic          Basic registration and verification\n");
    printf("                 Tests: RegisterProcess -> verify handler_id\n\n");
    
    printf("  check          Simple check for update flow\n");
    printf("                 Tests: Register -> CheckForUpdate -> Unregister\n\n");
    
    printf("  full           Complete workflow test\n");
    printf("                 Tests: Full lifecycle with all operations\n\n");
    
    printf("CACHE BEHAVIOR TESTS:\n");
    printf("  cache-hit      Test cache hit scenario\n");
    printf("                 Expects: Immediate response with firmware data\n");
    printf("                 Setup: Ensure /tmp/xconf_response_thunder.txt exists\n\n");
    
    printf("  cache-miss     Test cache miss scenario\n");
    printf("                 Expects: UPDATE_ERROR + background fetch + signal\n");
    printf("                 Setup: rm /tmp/xconf_response_thunder.txt\n\n");
    
    printf("  signals        Cache behavior with signal monitoring\n");
    printf("                 Tests: Both cache hit and miss flows with timing\n\n");
    
    printf("CONCURRENCY TESTS:\n");
    printf("  concurrent     Multiple rapid CheckForUpdate calls\n");
    printf("                 Tests: Piggyback behavior (single XConf fetch)\n");
    printf("                 Expected: All requests share same background fetch\n\n");
    
    printf("  rapid-check    Rapid successive CheckForUpdate calls\n");
    printf("                 Tests: 5 rapid calls with 100ms intervals\n");
    printf("                 Verifies: IsCheckUpdateInProgress flag behavior\n\n");
    
    printf("REGISTRATION TESTS:\n");
    printf("  reregister     Re-registration from same client\n");
    printf("                 Expected: Returns same handler_id\n\n");
    
    printf("  duplicate      Duplicate process name from different terminal\n");
    printf("                 Expected: Rejected (process already registered)\n");
    printf("                 Note: Run from 2 terminals with same process name\n\n");
    
    printf("ERROR HANDLING TESTS:\n");
    printf("  no-register    CheckForUpdate without registration\n");
    printf("                 Expected: ACCESS_DENIED error\n\n");
    
    printf("  invalid-handler CheckForUpdate with invalid handler_id\n");
    printf("                 Expected: NOT_REGISTERED error\n\n");
    
    printf("STRESS TESTS:\n");
    printf("  stress-reg     Multiple rapid registrations (10 iterations)\n");
    printf("                 Tests: Registration robustness under load\n\n");
    
    printf("  stress-check   Multiple rapid check operations (20 calls)\n");
    printf("                 Tests: CheckForUpdate handling under stress\n\n");
    
    printf("TIMEOUT TESTS:\n");
    printf("  long-wait      CheckForUpdate with extended signal wait\n");
    printf("                 Tests: Signal delivery over time (60s wait)\n\n");
    
    printf("================================================================================\n");
    printf("EXAMPLES:\n");
    printf("================================================================================\n\n");
    
    printf("  # Basic registration\n");
    printf("  %s VideoApp 1.0.0 basic\n\n", program_name);
    
    printf("  # Test cache hit (ensure cache exists first)\n");
    printf("  %s VideoApp 1.0.0 cache-hit\n\n", program_name);
    
    printf("  # Test cache miss (delete cache first)\n");
    printf("  rm /tmp/xconf_response_thunder.txt\n");
    printf("  %s VideoApp 1.0.0 cache-miss\n\n", program_name);
    
    printf("  # Test concurrent requests (piggyback)\n");
    printf("  rm /tmp/xconf_response_thunder.txt\n");
    printf("  %s VideoApp 1.0.0 concurrent\n\n", program_name);
    
    printf("  # Test error handling (no registration)\n");
    printf("  %s VideoApp 1.0.0 no-register\n\n", program_name);
    
    printf("  # Duplicate registration test (run in 2 terminals)\n");
    printf("  Terminal 1: %s VideoApp 1.0.0 basic\n", program_name);
    printf("  Terminal 2: %s VideoApp 1.0.0 duplicate\n\n", program_name);
    
    printf("================================================================================\n");
    printf("MONITORING:\n");
    printf("================================================================================\n\n");
    
    printf("  # Watch daemon logs\n");
    printf("  tail -f /opt/logs/swupdate.log\n\n");
    
    printf("  # Monitor D-Bus signals\n");
    printf("  sudo dbus-monitor --system \"type='signal',sender='org.rdkfwupdater.Service'\"\n\n");
    
    printf("  # Check cache status\n");
    printf("  ls -lh /tmp/xconf_response_thunder.txt\n");
    printf("  cat /tmp/xconf_response_thunder.txt\n\n");
    
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
        
    } else {
        PRINT_ERROR("Unknown test mode: %s", test_mode);
        print_usage("testClient");
    }
}

/**
 * Main function
 */
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
