/*
 * Copyright 2023 Comcast Cable Communications Management, LLC
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
// FAKE FILE I/O STATE (declare before extern "C" functions that use it)
// ============================================================================

namespace FakeFileIO {
    static bool g_file_exists = false;
    static std::string g_file_content;
    static int g_fopen_call_count = 0;
    static int g_usleep_call_count = 0;
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
 * @brief Get last emitted status as integer
 */
gint32 fake_dbus_get_last_status_int() {
    // Try to parse status string as integer
    if (!FakeDBus::g_last_status.empty()) {
        return atoi(FakeDBus::g_last_status.c_str());
    }
    return 0;
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
    
    // Parse signal parameters - support multiple signal formats
    // DownloadProgress: "(tsuss)" - (handler_id, firmware_name, progress_u32, status_str, message)
    // UpdateProgress:   "(tsiis)" - (handler_id, firmware_name, progress_i32, status_i32, message)
    if (parameters != NULL) {
        const gchar* format = g_variant_get_type_string(parameters);
        
        if (g_strcmp0(format, "(tsuss)") == 0) {
            // DownloadProgress signal
            guint64 handler_id = 0;
            const gchar* firmware_name = NULL;
            guint32 progress = 0;
            const gchar* status = NULL;
            const gchar* message = NULL;
            
            g_variant_get(parameters, "(tsuss)", 
                         &handler_id, &firmware_name, &progress, &status, &message);
            
            // Store in fake D-Bus state
            FakeDBus::g_last_handler_id = handler_id;
            FakeDBus::g_last_firmware_name = firmware_name ? firmware_name : "";
            FakeDBus::g_last_progress_percent = progress;
            FakeDBus::g_last_status = status ? status : "";
            FakeDBus::g_last_message = message ? message : "";
            
            // Add to history
            FakeDBus::EmittedSignal sig;
            sig.handler_id = handler_id;
            sig.firmware_name = firmware_name ? firmware_name : "";
            sig.progress_percent = progress;
            sig.status = status ? status : "";
            sig.message = message ? message : "";
            FakeDBus::g_signal_history.push_back(sig);
            
        } else if (g_strcmp0(format, "(tsiis)") == 0) {
            // UpdateProgress signal (flash progress)
            guint64 handler_id = 0;
            const gchar* firmware_name = NULL;
            gint32 progress = 0;
            gint32 status_code = 0;
            const gchar* message = NULL;
            
            g_variant_get(parameters, "(tsiis)", 
                         &handler_id, &firmware_name, &progress, &status_code, &message);
            
            // Store in fake D-Bus state (convert status_code to string for consistency)
            FakeDBus::g_last_handler_id = handler_id;
            FakeDBus::g_last_firmware_name = firmware_name ? firmware_name : "";
            FakeDBus::g_last_progress_percent = (guint32)progress;
            FakeDBus::g_last_status = std::to_string(status_code);  // Store as string
            FakeDBus::g_last_message = message ? message : "";
            
            // Add to history
            FakeDBus::EmittedSignal sig;
            sig.handler_id = handler_id;
            sig.firmware_name = firmware_name ? firmware_name : "";
            sig.progress_percent = (guint32)progress;
            sig.status = std::to_string(status_code);
            sig.message = message ? message : "";
            FakeDBus::g_signal_history.push_back(sig);
        }
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

/**
 * @brief Fake implementation of g_usleep (eliminates delays in tests)
 * 
 * This makes thread tests run instantly instead of sleeping
 */
void g_usleep(gulong microseconds) {
    (void)microseconds;  // Ignore - make tests instant
    FakeFileIO::g_usleep_call_count++;
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

// ============================================================================
// FAKE FILE I/O FUNCTIONS (extern "C" linkage)
// ============================================================================

extern "C" {

/**
 * @brief Reset file I/O fake state
 */
void fake_fileio_reset() {
    FakeFileIO::g_file_exists = false;
    FakeFileIO::g_file_content.clear();
    FakeFileIO::g_fopen_call_count = 0;
    FakeFileIO::g_usleep_call_count = 0;
}

/**
 * @brief Set fake progress file content
 */
void fake_fileio_set_progress_file(const char* content) {
    FakeFileIO::g_file_exists = (content != nullptr);
    if (content) {
        FakeFileIO::g_file_content = content;
    } else {
        FakeFileIO::g_file_content.clear();
    }
}

/**
 * @brief Get fopen call count (for verification)
 */
int fake_fileio_get_fopen_count() {
    return FakeFileIO::g_fopen_call_count;
}

/**
 * @brief Get g_usleep call count (for verification)
 */
int fake_fileio_get_usleep_count() {
    return FakeFileIO::g_usleep_call_count;
}

/**
 * @brief Fake implementation of fopen for progress file
 * 
 * ONLY intercepts /opt/curl_progress - all other files use real fopen
 */
FILE* fopen(const char* path, const char* mode) {
    if (path && strcmp(path, "/opt/curl_progress") == 0) {
        FakeFileIO::g_fopen_call_count++;
        
        if (!FakeFileIO::g_file_exists) {
            return nullptr;  // File doesn't exist
        }
        
        // Create temporary file with fake content
        FILE* tmp = tmpfile();
        if (tmp && !FakeFileIO::g_file_content.empty()) {
            fwrite(FakeFileIO::g_file_content.c_str(), 1, 
                   FakeFileIO::g_file_content.length(), tmp);
            rewind(tmp);
        }
        return tmp;
    }
    
    // For all other files, return NULL (tests should not need other files)
    return nullptr;
}

} // extern "C"
