/**
 * @file test_dbus_fake.h
 * @brief Header for fake D-Bus implementation (link-time symbol interposition)
 * 
 * This header provides access to the fake D-Bus state for unit tests.
 * Include this in your test files to verify D-Bus signal emissions.
 */

#ifndef TEST_DBUS_FAKE_H
#define TEST_DBUS_FAKE_H

#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// FAKE D-BUS STATE CONTROL
// ============================================================================

/**
 * @brief Reset fake D-Bus state between tests
 * 
 * Call this in SetUp() or at the start of each test.
 */
void fake_dbus_reset(void);

/**
 * @brief Check if any D-Bus signal was emitted
 * @return true if g_dbus_connection_emit_signal was called
 */
bool fake_dbus_was_signal_emitted(void);

/**
 * @brief Get the last emitted progress percentage
 * @return Progress value from last emitted signal (0-100)
 */
guint32 fake_dbus_get_last_progress(void);

/**
 * @brief Get the last emitted status string
 * @return Status string (e.g., "INPROGRESS", "COMPLETE", "NOTSTARTED")
 */
const char* fake_dbus_get_last_status(void);

/**
 * @brief Get the last emitted message
 * @return Message string (e.g., "Download in progress")
 */
const char* fake_dbus_get_last_message(void);

/**
 * @brief Get the last emitted firmware name
 * @return Firmware filename
 */
const char* fake_dbus_get_last_firmware_name(void);

/**
 * @brief Get the last emitted handler ID
 * @return Handler ID value
 */
guint64 fake_dbus_get_last_handler_id(void);

/**
 * @brief Get total number of signals emitted
 * @return Count of signals in history
 */
int fake_dbus_get_signal_count(void);

/**
 * @brief Get the last emitted status as integer (for UpdateProgress signal)
 * @return Status code (for UpdateProgress/flash signals)
 */
gint32 fake_dbus_get_last_status_int(void);

/**
 * @brief Configure the fake to fail on next emission
 * @param should_fail Set to true to simulate emission failure
 * @param error_code Error code to return
 * @param error_msg Error message string
 */
void fake_dbus_set_should_fail(bool should_fail, int error_code, const char* error_msg);

#ifdef __cplusplus
}

// ============================================================================
// C++ HELPER CLASS
// ============================================================================

namespace FakeDBus {

/**
 * @brief RAII wrapper for fake D-Bus state reset
 * 
 * Usage:
 *   TEST(MyTest, EmitSignal) {
 *       FakeDBus::ScopedReset reset;  // Auto-reset on scope exit
 *       // ... test code ...
 *   }
 */
class ScopedReset {
public:
    ScopedReset();
    ~ScopedReset();
};

} // namespace FakeDBus

#endif // __cplusplus

#endif // TEST_DBUS_FAKE_H
