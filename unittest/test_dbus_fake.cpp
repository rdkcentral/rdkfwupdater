/**
 * @file test_dbus_fake.cpp
 * @brief Fake D-Bus implementation for unit testing via link-time symbol interposition
 * 
 * TECHNIQUE: Link-time symbol override
 * ============================================================================
 * Production code calls:
 *   g_dbus_connection_emit_signal(...)
 * 
 * We provide our own implementation of this function in the test binary.
 * The linker picks our fake version instead of the real GLib one.
 * 
 * REQUIREMENTS:
 * - Compile with -lglib-2.0 but WITHOUT -lgio-2.0
 * - Link test binary with this file
 * - Production code remains unchanged
 * 
 * RESULT:
 * - No D-Bus daemon needed
 * - No IPC overhead
 * - Deterministic, fast unit tests
 * - Can verify signal parameters
 * ============================================================================
 */

#include <gio/gio.h>
#include <glib.h>
#include <string>
#include <vector>

// ============================================================================
// FAKE D-BUS STATE (for verification in tests)
// ============================================================================

namespace FakeDBus {
    // Track if signal was emitted
    static bool g_emit_called = false;
    
    // Track last emitted signal parameters
    static guint64 g_last_handler_id = 0;
    static std::string g_last_firmware_name;
    static guint32 g_last_progress_percent = 0;
    static std::string g_last_status;
    static std::string g_last_message;
    
    // Track all emitted signals (for multi-signal scenarios)
    struct EmittedSignal {
        guint64 handler_id;
        std::string firmware_name;
        guint32 progress_percent;
        std::string status;
        std::string message;
    };
    static std::vector<EmittedSignal> g_signal_history;
    
    // Track error conditions
    static bool g_should_fail = false;
    static GQuark g_error_domain = 0;
    static gint g_error_code = 0;
    static std::string g_error_message;
}

// ============================================================================
// HELPER FUNCTIONS (for tests to control fake D-Bus)
// ============================================================================

extern "C" {

/**
 * @brief Reset fake D-Bus state between tests
 */
void fake_dbus_reset() {
    FakeDBus::g_emit_called = false;
    FakeDBus::g_last_handler_id = 0;
    FakeDBus::g_last_firmware_name.clear();
    FakeDBus::g_last_progress_percent = 0;
    FakeDBus::g_last_status.clear();
    FakeDBus::g_last_message.clear();
    FakeDBus::g_signal_history.clear();
    FakeDBus::g_should_fail = false;
    FakeDBus::g_error_code = 0;
    FakeDBus::g_error_message.clear();
}

/**
 * @brief Check if signal was emitted
 */
bool fake_dbus_was_signal_emitted() {
    return FakeDBus::g_emit_called;
}

/**
 * @brief Get last emitted progress percentage
 */
guint32 fake_dbus_get_last_progress() {
    return FakeDBus::g_last_progress_percent;
}

/**
 * @brief Get last emitted status string
 */
const char* fake_dbus_get_last_status() {
    return FakeDBus::g_last_status.c_str();
}

/**
 * @brief Get last emitted message
 */
const char* fake_dbus_get_last_message() {
    return FakeDBus::g_last_message.c_str();
}

/**
 * @brief Get last emitted firmware name
 */
const char* fake_dbus_get_last_firmware_name() {
    return FakeDBus::g_last_firmware_name.c_str();
}

/**
 * @brief Get last emitted handler ID
 */
guint64 fake_dbus_get_last_handler_id() {
    return FakeDBus::g_last_handler_id;
}

/**
 * @brief Get number of signals emitted
 */
int fake_dbus_get_signal_count() {
    return FakeDBus::g_signal_history.size();
}

/**
 * @brief Configure fake to simulate failure
 */
void fake_dbus_set_should_fail(bool should_fail, int error_code, const char* error_msg) {
    FakeDBus::g_should_fail = should_fail;
    FakeDBus::g_error_code = error_code;
    if (error_msg) {
        FakeDBus::g_error_message = error_msg;
    }
}

// ============================================================================
// FAKE D-BUS FUNCTIONS (link-time symbol override)
// ============================================================================

/**
 * @brief Fake implementation of g_dbus_connection_emit_signal
 * 
 * This function OVERRIDES the real GLib implementation via link-time
 * symbol interposition. Production code calls this thinking it's the
 * real D-Bus function, but we intercept and record the call.
 * 
 * @param connection Ignored (can be NULL or fake pointer)
 * @param destination_bus_name Ignored
 * @param object_path Ignored
 * @param interface_name Ignored
 * @param signal_name Ignored
 * @param parameters Signal parameters to parse and record
 * @param error Error output parameter
 * @return TRUE on success, FALSE on configured failure
 */
gboolean g_dbus_connection_emit_signal(
    GDBusConnection* connection,
    const gchar* destination_bus_name,
    const gchar* object_path,
    const gchar* interface_name,
    const gchar* signal_name,
    GVariant* parameters,
    GError** error
) {
    (void)connection;           // Unused - we don't need real connection
    (void)destination_bus_name; // Unused
    (void)object_path;          // Unused
    (void)interface_name;       // Unused
    (void)signal_name;          // Unused
    
    // Mark that signal was emitted
    FakeDBus::g_emit_called = true;
    
    // Simulate failure if configured
    if (FakeDBus::g_should_fail) {
        if (error != NULL) {
            if (FakeDBus::g_error_domain == 0) {
                FakeDBus::g_error_domain = g_quark_from_static_string("fake-dbus-error");
            }
            *error = g_error_new(FakeDBus::g_error_domain,
                                FakeDBus::g_error_code,
                                "%s", FakeDBus::g_error_message.c_str());
        }
        return FALSE;
    }
    
    // Parse signal parameters (DownloadProgress signature: "(tsuss)")
    if (parameters != NULL) {
        guint64 handler_id = 0;
        const gchar* status = NULL;
        guint32 progress = 0;
        const gchar* message = NULL;
        const gchar* firmware = NULL;
        
        // Extract parameters
        g_variant_get(parameters, "(tsuss)", 
                     &handler_id, &status, &progress, &message, &firmware);
        
        // Store in fake D-Bus state
        FakeDBus::g_last_handler_id = handler_id;
        FakeDBus::g_last_status = status ? status : "";
        FakeDBus::g_last_progress_percent = progress;
        FakeDBus::g_last_message = message ? message : "";
        FakeDBus::g_last_firmware_name = firmware ? firmware : "";
        
        // Add to history
        FakeDBus::EmittedSignal sig;
        sig.handler_id = handler_id;
        sig.firmware_name = firmware ? firmware : "";
        sig.progress_percent = progress;
        sig.status = status ? status : "";
        sig.message = message ? message : "";
        FakeDBus::g_signal_history.push_back(sig);
    }
    
    return TRUE;  // Signal "emitted" successfully
}

/**
 * @brief Fake implementation of g_idle_add
 * 
 * In real GLib, this schedules a callback in the main loop.
 * For unit tests, we execute the callback IMMEDIATELY and synchronously.
 * 
 * This allows us to test idle callbacks without needing a running main loop.
 * 
 * @param function Callback to execute
 * @param data User data to pass to callback
 * @return Fake source ID (always 1)
 */
guint g_idle_add(GSourceFunc function, gpointer data) {
    // Execute callback immediately (synchronous for testing)
    if (function != NULL) {
        function(data);
    }
    return 1;  // Return fake source ID
}

} // extern "C"

// ============================================================================
// C++ HELPER CLASS (for RAII cleanup in tests)
// ============================================================================

namespace FakeDBus {

/**
 * @brief RAII wrapper for fake D-Bus state
 * 
 * Usage in tests:
 *   TEST(MyTest, EmitSignal) {
 *       FakeDBus::ScopedReset reset;  // Auto-reset on scope exit
 *       // ... test code ...
 *   }
 */
class ScopedReset {
public:
    ScopedReset() { fake_dbus_reset(); }
    ~ScopedReset() { fake_dbus_reset(); }
};

} // namespace FakeDBus
