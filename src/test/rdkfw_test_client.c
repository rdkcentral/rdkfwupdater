#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <gio/gio.h>

// Test client for RDK Firmware Updater Daemon
// Comprehensive test for all registration scenarios

#define DAEMON_SERVICE "org.rdkfwupdater.service"
#define DAEMON_PATH "/org/rdkfwupdater/service"
#define DAEMON_INTERFACE "org.rdkfwupdater.Interface"

// Colors for output
#define COLOR_RED     "\x1b[31m"
#define COLOR_GREEN   "\x1b[32m"
#define COLOR_YELLOW  "\x1b[33m"
#define COLOR_BLUE    "\x1b[34m"
#define COLOR_RESET   "\x1b[0m"

typedef struct {
    GDBusConnection *connection;
    gchar *process_name;
    gchar *lib_version;
    gchar *sender_id;
    guint64 handler_id;
    gboolean is_registered;
} TestClient;

// Function prototypes
static TestClient* test_client_new(const gchar *process_name, const gchar *lib_version);
static gboolean test_client_register(TestClient *client);
static gboolean test_client_check_update(TestClient *client);
static gboolean test_client_unregister(TestClient *client);
static void test_client_free(TestClient *client);
static void print_usage(const char *program_name);
static void run_scenario_tests(void);

// Create new test client
static TestClient* test_client_new(const gchar *process_name, const gchar *lib_version)
{
    GError *error = NULL;
    TestClient *client = g_malloc0(sizeof(TestClient));
    
    printf(COLOR_BLUE "=== Creating Test Client ===" COLOR_RESET "\n");
    printf("Process Name: %s\n", process_name);
    printf("Library Version: %s\n", lib_version);
    
    // Connect to system bus
    client->connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    if (!client->connection) {
        printf(COLOR_RED "✗ Failed to connect to D-Bus: %s" COLOR_RESET "\n", error->message);
        g_error_free(error);
        g_free(client);
        return NULL;
    }
    
    client->process_name = g_strdup(process_name);
    client->lib_version = g_strdup(lib_version);
    client->handler_id = 0;
    client->is_registered = FALSE;
    
    // Get our sender ID (D-Bus unique name)
    client->sender_id = g_strdup(g_dbus_connection_get_unique_name(client->connection));
    
    printf(COLOR_GREEN "✓ Client created successfully" COLOR_RESET "\n");
    printf("D-Bus Sender ID: %s\n", client->sender_id);
    printf("\n");
    
    return client;
}

// Register process with daemon
static gboolean test_client_register(TestClient *client)
{
    GError *error = NULL;
    GVariant *result = NULL;
    
    printf(COLOR_BLUE "=== Registering Process ===" COLOR_RESET "\n");
    printf("Calling RegisterProcess('%s', '%s')...\n", client->process_name, client->lib_version);
    
    result = g_dbus_connection_call_sync(
        client->connection,
        DAEMON_SERVICE,
        DAEMON_PATH,
        DAEMON_INTERFACE,
        "RegisterProcess",
        g_variant_new("(ss)", client->process_name, client->lib_version),
        G_VARIANT_TYPE("(t)"),
        G_DBUS_CALL_FLAGS_NONE,
        -1,  // timeout
        NULL,
        &error
    );
    
    if (result) {
        g_variant_get(result, "(t)", &client->handler_id);
        client->is_registered = TRUE;
        g_variant_unref(result);
        
        printf(COLOR_GREEN "✓ Registration successful!" COLOR_RESET "\n");
        printf("Handler ID: %" G_GUINT64_FORMAT "\n", client->handler_id);
        printf("\n");
        return TRUE;
    } else {
        printf(COLOR_RED "✗ Registration failed: %s" COLOR_RESET "\n", error->message);
        g_error_free(error);
        printf("\n");
        return FALSE;
    }
}

// Test CheckForUpdate functionality
static gboolean test_client_check_update(TestClient *client)
{
    GError *error = NULL;
    GVariant *result = NULL;
    
    printf(COLOR_BLUE "=== Testing CheckForUpdate ===" COLOR_RESET "\n");
    
    if (!client->is_registered) {
        printf(COLOR_YELLOW "⚠ Client not registered, skipping CheckForUpdate test" COLOR_RESET "\n");
        printf("\n");
        return FALSE;
    }
    
    printf("Calling CheckForUpdate('%s')...\n", client->process_name);
    
    result = g_dbus_connection_call_sync(
        client->connection,
        DAEMON_SERVICE,
        DAEMON_PATH,
        DAEMON_INTERFACE,
        "CheckForUpdate",
        g_variant_new("(s)", client->process_name),
        G_VARIANT_TYPE("(ssssj)"),  // Expected return type for CheckForUpdate
        G_DBUS_CALL_FLAGS_NONE,
        -1,  // timeout
        NULL,
        &error
    );
    
    if (result) {
        gchar *fwdata_version, *fwdata_availableVersion, *fwdata_updateDetails, *fwdata_status;
        gint32 fwdata_status_code;
        
        g_variant_get(result, "(ssssj)", 
                     &fwdata_version, &fwdata_availableVersion, 
                     &fwdata_updateDetails, &fwdata_status, &fwdata_status_code);
        
        printf(COLOR_GREEN "✓ CheckForUpdate successful!" COLOR_RESET "\n");
        printf("Current Version: %s\n", fwdata_version ? fwdata_version : "N/A");
        printf("Available Version: %s\n", fwdata_availableVersion ? fwdata_availableVersion : "N/A");
        printf("Update Details: %s\n", fwdata_updateDetails ? fwdata_updateDetails : "N/A");
        printf("Status: %s\n", fwdata_status ? fwdata_status : "N/A");
        printf("Status Code: %d\n", fwdata_status_code);
        
        g_variant_unref(result);
        printf("\n");
        return TRUE;
    } else {
        printf(COLOR_RED "✗ CheckForUpdate failed: %s" COLOR_RESET "\n", error->message);
        g_error_free(error);
        printf("\n");
        return FALSE;
    }
}

// Unregister process from daemon
static gboolean test_client_unregister(TestClient *client)
{
    GError *error = NULL;
    GVariant *result = NULL;
    
    printf(COLOR_BLUE "=== Unregistering Process ===" COLOR_RESET "\n");
    
    if (!client->is_registered) {
        printf(COLOR_YELLOW "⚠ Client not registered, skipping unregistration" COLOR_RESET "\n");
        printf("\n");
        return FALSE;
    }
    
    printf("Calling UnregisterProcess(%" G_GUINT64_FORMAT ")...\n", client->handler_id);
    
    result = g_dbus_connection_call_sync(
        client->connection,
        DAEMON_SERVICE,
        DAEMON_PATH,
        DAEMON_INTERFACE,
        "UnregisterProcess",
        g_variant_new("(t)", client->handler_id),
        G_VARIANT_TYPE("(b)"),
        G_DBUS_CALL_FLAGS_NONE,
        -1,  // timeout
        NULL,
        &error
    );
    
    if (result) {
        gboolean success;
        g_variant_get(result, "(b)", &success);
        g_variant_unref(result);
        
        if (success) {
            printf(COLOR_GREEN "✓ Unregistration successful!" COLOR_RESET "\n");
            client->is_registered = FALSE;
            client->handler_id = 0;
        } else {
            printf(COLOR_RED "✗ Unregistration failed (daemon returned false)" COLOR_RESET "\n");
        }
        printf("\n");
        return success;
    } else {
        printf(COLOR_RED "✗ Unregistration failed: %s" COLOR_RESET "\n", error->message);
        g_error_free(error);
        printf("\n");
        return FALSE;
    }
}

// Free test client resources
static void test_client_free(TestClient *client)
{
    if (!client) return;
    
    if (client->is_registered) {
        printf(COLOR_YELLOW "⚠ Client still registered, auto-unregistering..." COLOR_RESET "\n");
        test_client_unregister(client);
    }
    
    if (client->connection) {
        g_object_unref(client->connection);
    }
    
    g_free(client->process_name);
    g_free(client->lib_version);
    g_free(client->sender_id);
    g_free(client);
    
    printf(COLOR_BLUE "Test client cleaned up" COLOR_RESET "\n\n");
}

// Print usage information
static void print_usage(const char *program_name)
{
    printf("RDK Firmware Updater Daemon Test Client\n");
    printf("========================================\n\n");
    
    printf("Usage:\n");
    printf("  %s --register <process_name> <lib_version>    Register process\n", program_name);
    printf("  %s --check-update <process_name> <lib_version> Register + CheckForUpdate\n", program_name);
    printf("  %s --full-test <process_name> <lib_version>   Register + Check + Unregister\n", program_name);
    printf("  %s --scenarios                               Run all test scenarios\n", program_name);
    printf("  %s --help                                    Show this help\n", program_name);
    printf("\n");
    
    printf("Examples:\n");
    printf("  %s --register \"MyApp\" \"1.0.0\"\n", program_name);
    printf("  %s --check-update \"TestApp\" \"2.0.0\"\n", program_name);
    printf("  %s --full-test \"DemoApp\" \"3.0.0\"\n", program_name);
    printf("  %s --scenarios\n", program_name);
    printf("\n");
}

// Run comprehensive scenario tests
static void run_scenario_tests(void)
{
    printf(COLOR_BLUE "========================================\n");
    printf("Running Comprehensive Registration Tests\n");
    printf("========================================" COLOR_RESET "\n\n");
    
    TestClient *client1 = NULL, *client2 = NULL;
    
    // Scenario 1: Basic registration
    printf(COLOR_YELLOW "--- Scenario 1: Basic Registration ---" COLOR_RESET "\n");
    client1 = test_client_new("ScenarioApp1", "1.0.0");
    if (client1) {
        test_client_register(client1);
        test_client_check_update(client1);
    }
    
    // Scenario 2: Same client re-registering same process (should succeed)
    printf(COLOR_YELLOW "--- Scenario 2: Same Client Re-registration ---" COLOR_RESET "\n");
    if (client1) {
        printf("Re-registering same process with same client...\n");
        test_client_register(client1);  // Should return existing handler_id
    }
    
    // Scenario 3: Same client, different process name (should fail)
    printf(COLOR_YELLOW "--- Scenario 3: Same Client, Different Process ---" COLOR_RESET "\n");
    if (client1) {
        TestClient *temp_client = g_malloc0(sizeof(TestClient));
        temp_client->connection = g_object_ref(client1->connection);  // Same connection
        temp_client->process_name = g_strdup("DifferentApp");
        temp_client->lib_version = g_strdup("1.0.0");
        temp_client->sender_id = g_strdup(client1->sender_id);  // Same sender
        temp_client->handler_id = 0;
        temp_client->is_registered = FALSE;
        
        printf("Attempting to register different process with same client...\n");
        test_client_register(temp_client);  // Should fail
        
        test_client_free(temp_client);
    }
    
    // Scenario 4: Different client, same process name (should fail)
    printf(COLOR_YELLOW "--- Scenario 4: Different Client, Same Process ---" COLOR_RESET "\n");
    client2 = test_client_new("ScenarioApp1", "2.0.0");  // Same process name, different client
    if (client2) {
        printf("Attempting to register same process name with different client...\n");
        test_client_register(client2);  // Should fail
    }
    
    // Scenario 5: Different client, different process (should succeed)
    printf(COLOR_YELLOW "--- Scenario 5: Different Client, Different Process ---" COLOR_RESET "\n");
    if (client2) {
        test_client_free(client2);
    }
    client2 = test_client_new("ScenarioApp2", "2.0.0");  // Different process, different client
    if (client2) {
        test_client_register(client2);  // Should succeed
        test_client_check_update(client2);
    }
    
    // Cleanup
    printf(COLOR_YELLOW "--- Cleanup ---" COLOR_RESET "\n");
    test_client_free(client1);
    test_client_free(client2);
    
    printf(COLOR_GREEN "========================================\n");
    printf("Scenario Testing Complete!\n");
    printf("========================================" COLOR_RESET "\n");
}

// Main function
int main(int argc, char *argv[])
{
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    if (g_strcmp0(argv[1], "--help") == 0) {
        print_usage(argv[0]);
        return 0;
    }
    
    if (g_strcmp0(argv[1], "--scenarios") == 0) {
        run_scenario_tests();
        return 0;
    }
    
    if (argc < 4) {
        printf(COLOR_RED "Error: Missing process_name or lib_version arguments" COLOR_RESET "\n\n");
        print_usage(argv[0]);
        return 1;
    }
    
    const char *process_name = argv[2];
    const char *lib_version = argv[3];
    
    TestClient *client = test_client_new(process_name, lib_version);
    if (!client) {
        return 1;
    }
    
    gboolean success = FALSE;
    
    if (g_strcmp0(argv[1], "--register") == 0) {
        success = test_client_register(client);
    }
    else if (g_strcmp0(argv[1], "--check-update") == 0) {
        if (test_client_register(client)) {
            success = test_client_check_update(client);
        }
    }
    else if (g_strcmp0(argv[1], "--full-test") == 0) {
        if (test_client_register(client)) {
            test_client_check_update(client);
            success = test_client_unregister(client);
        }
    }
    else {
        printf(COLOR_RED "Error: Unknown option '%s'" COLOR_RESET "\n\n", argv[1]);
        print_usage(argv[0]);
        test_client_free(client);
        return 1;
    }
    
    test_client_free(client);
    return success ? 0 : 1;
}
