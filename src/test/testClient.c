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
static void run_test_scenario(TestClientContext *client, const gchar *test_mode);

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
    printf("\nRDK Firmware Updater Daemon Test Client\n");
    printf("========================================\n\n");
    printf("Usage: %s <process_name> <lib_version> [test_mode]\n\n", program_name);
    
    printf("Parameters:\n");
    printf("  process_name  - Name of the process to register (e.g., 'MyApp')\n");
    printf("  lib_version   - Library version string (e.g., '1.0.0')\n");
    printf("  test_mode     - Test scenario to run (optional, default: 'basic')\n\n");
    
    printf("Test Modes:\n");
    printf("  basic         - Basic registration test\n");
    printf("  reregister    - Test same client re-registration (should return same handler_id)\n");
    printf("  check         - Register and perform CheckForUpdate\n");
    printf("  full          - Full workflow: Register -> Check -> Unregister\n");
    printf("  signals       - Test D-Bus signals and cache behavior (cache hit/miss scenarios)\n");
    printf("  concurrent    - Test concurrent CheckForUpdate requests (piggyback behavior)\n");
    printf("  stress        - Multiple rapid registration attempts\n\n");
    
    printf("Examples:\n");
    printf("  %s MyTestApp 1.0.0\n", program_name);
    printf("  %s MyTestApp 1.0.0 basic\n", program_name);
    printf("  %s MyTestApp 1.0.0 check\n", program_name);
    printf("  %s MyTestApp 1.0.0 signals\n", program_name);
    printf("  %s MyTestApp 1.0.0 concurrent\n", program_name);
    printf("  %s MyTestApp 1.0.0 full\n\n", program_name);
    
    printf("Expected Scenarios for Robust Testing:\n");
    printf("  1. Run same command twice -> Second should return same handler_id\n");
    printf("  2. Run with different process name from same client -> Should be rejected\n");
    printf("  3. Run from different terminal with same process name -> Should be rejected\n");
    printf("  4. Run CheckForUpdate without registration -> Should be rejected\n\n");
}

/**
 * Run specific test scenario
 */
static void run_test_scenario(TestClientContext *client, const gchar *test_mode)
{
    PRINT_INFO("Running test scenario: %s", test_mode);
    
    if (g_strcmp0(test_mode, "basic") == 0) {
        // Basic registration test
        test_client_register(client);
        
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
            
            if (test_client_register(client)) {
                if (client->handler_id == first_handler) {
                    PRINT_SUCCESS("Re-registration test PASSED - same handler_id returned");
                } else {
                    PRINT_ERROR("Re-registration test FAILED - different handler_id returned");
                }
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
        
    } else if (g_strcmp0(test_mode, "stress") == 0) {
        // Stress test - multiple rapid registrations
        PRINT_INFO("=== Running Stress Test ===");
        
        for (int i = 0; i < 5; i++) {
            PRINT_INFO("Stress test iteration %d/5", i + 1);
            client->is_registered = FALSE;
            client->handler_id = 0;
            
            test_client_register(client);
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
    run_test_scenario(client, test_mode);
    
    // Cleanup
    test_client_free(client);
    
    printf("\n=== Test Client Finished ===\n");
    return 0;
}
