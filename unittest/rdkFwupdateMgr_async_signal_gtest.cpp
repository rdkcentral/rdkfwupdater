/**
 * @file rdkFwupdateMgr_async_signal_gtest.cpp
 * @brief Unit tests for signal parsing and memory management
 *
 * Phase 6.3: Signal Parsing Memory Management Tests
 *
 * Tests:
 * - Safe parsing of signal data
 * - Handling of malformed signal data
 * - Memory allocation/deallocation during signal processing
 * - Concurrent signal handling
 * - Large signal data handling
 * - NULL and empty string handling
 */

#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <chrono>

extern "C" {
#include "rdkFwupdateMgr_client.h"
// Internal testing APIs
extern void rdkFwupdateMgr_async_init_for_test(void);
extern void rdkFwupdateMgr_async_cleanup_for_test(void);
extern void rdkFwupdateMgr_async_simulate_signal_for_test(
    const char* status, const char* message, const char* version);
}

namespace {

/**
 * Test fixture for signal parsing tests
 */
class AsyncSignalParsingTest : public ::testing::Test {
protected:
    void SetUp() override {
        rdkFwupdateMgr_async_init_for_test();
        callback_invoked = false;
        callback_status.clear();
        callback_message.clear();
        callback_version.clear();
    }

    void TearDown() override {
        rdkFwupdateMgr_async_cleanup_for_test();
    }

    // Shared state for callbacks
    static bool callback_invoked;
    static std::string callback_status;
    static std::string callback_message;
    static std::string callback_version;

    static void test_callback(const char* status, const char* message,
                             const char* version, void* user_data) {
        (void)user_data;
        callback_invoked = true;
        callback_status = status ? status : "";
        callback_message = message ? message : "";
        callback_version = version ? version : "";
    }
};

bool AsyncSignalParsingTest::callback_invoked = false;
std::string AsyncSignalParsingTest::callback_status;
std::string AsyncSignalParsingTest::callback_message;
std::string AsyncSignalParsingTest::callback_version;

/**
 * Test: Parse valid signal data
 * Expected: All fields correctly parsed and passed to callback
 */
TEST_F(AsyncSignalParsingTest, ParseValidSignalData) {
    int handler_id = rdkFwupdateMgr_checkForUpdate_async(test_callback, nullptr);
    ASSERT_GT(handler_id, 0);

    // Simulate signal with valid data
    rdkFwupdateMgr_async_simulate_signal_for_test(
        "UPDATE_AVAILABLE",
        "New firmware version available",
        "2.0.0"
    );

    // Give time for signal processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_TRUE(callback_invoked);
    EXPECT_EQ(callback_status, "UPDATE_AVAILABLE");
    EXPECT_EQ(callback_message, "New firmware version available");
    EXPECT_EQ(callback_version, "2.0.0");
}

/**
 * Test: Parse signal with NULL status
 * Expected: Callback receives NULL or empty string safely
 */
TEST_F(AsyncSignalParsingTest, ParseSignalWithNullStatus) {
    int handler_id = rdkFwupdateMgr_checkForUpdate_async(test_callback, nullptr);
    ASSERT_GT(handler_id, 0);

    rdkFwupdateMgr_async_simulate_signal_for_test(
        nullptr,  // NULL status
        "Some message",
        "1.0.0"
    );

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_TRUE(callback_invoked);
    EXPECT_EQ(callback_status, "");
}

/**
 * Test: Parse signal with NULL message
 * Expected: Callback receives NULL or empty string safely
 */
TEST_F(AsyncSignalParsingTest, ParseSignalWithNullMessage) {
    int handler_id = rdkFwupdateMgr_checkForUpdate_async(test_callback, nullptr);
    ASSERT_GT(handler_id, 0);

    rdkFwupdateMgr_async_simulate_signal_for_test(
        "NO_UPDATE",
        nullptr,  // NULL message
        "1.0.0"
    );

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_TRUE(callback_invoked);
    EXPECT_EQ(callback_message, "");
}

/**
 * Test: Parse signal with NULL version
 * Expected: Callback receives NULL or empty string safely
 */
TEST_F(AsyncSignalParsingTest, ParseSignalWithNullVersion) {
    int handler_id = rdkFwupdateMgr_checkForUpdate_async(test_callback, nullptr);
    ASSERT_GT(handler_id, 0);

    rdkFwupdateMgr_async_simulate_signal_for_test(
        "ERROR",
        "Failed to check for updates",
        nullptr  // NULL version
    );

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_TRUE(callback_invoked);
    EXPECT_EQ(callback_version, "");
}

/**
 * Test: Parse signal with all NULL fields
 * Expected: Callback invoked with empty strings, no crashes
 */
TEST_F(AsyncSignalParsingTest, ParseSignalAllNullFields) {
    int handler_id = rdkFwupdateMgr_checkForUpdate_async(test_callback, nullptr);
    ASSERT_GT(handler_id, 0);

    rdkFwupdateMgr_async_simulate_signal_for_test(nullptr, nullptr, nullptr);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_TRUE(callback_invoked);
}

/**
 * Test: Parse signal with empty strings
 * Expected: Empty strings passed to callback correctly
 */
TEST_F(AsyncSignalParsingTest, ParseSignalEmptyStrings) {
    int handler_id = rdkFwupdateMgr_checkForUpdate_async(test_callback, nullptr);
    ASSERT_GT(handler_id, 0);

    rdkFwupdateMgr_async_simulate_signal_for_test("", "", "");

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_TRUE(callback_invoked);
    EXPECT_EQ(callback_status, "");
    EXPECT_EQ(callback_message, "");
    EXPECT_EQ(callback_version, "");
}

/**
 * Test: Parse signal with very long strings
 * Expected: Long strings handled correctly without buffer overflows
 */
TEST_F(AsyncSignalParsingTest, ParseSignalLongStrings) {
    int handler_id = rdkFwupdateMgr_checkForUpdate_async(test_callback, nullptr);
    ASSERT_GT(handler_id, 0);

    std::string long_status(1000, 'A');
    std::string long_message(5000, 'B');
    std::string long_version(500, 'C');

    rdkFwupdateMgr_async_simulate_signal_for_test(
        long_status.c_str(),
        long_message.c_str(),
        long_version.c_str()
    );

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_TRUE(callback_invoked);
    EXPECT_EQ(callback_status.size(), 1000u);
    EXPECT_EQ(callback_message.size(), 5000u);
    EXPECT_EQ(callback_version.size(), 500u);
}

/**
 * Test: Parse signal with special characters
 * Expected: Special characters preserved correctly
 */
TEST_F(AsyncSignalParsingTest, ParseSignalSpecialCharacters) {
    int handler_id = rdkFwupdateMgr_checkForUpdate_async(test_callback, nullptr);
    ASSERT_GT(handler_id, 0);

    rdkFwupdateMgr_async_simulate_signal_for_test(
        "Status with spaces & symbols !@#$%",
        "Message with\nnewlines\tand\ttabs",
        "v1.2.3-beta+build.123"
    );

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_TRUE(callback_invoked);
    EXPECT_EQ(callback_status, "Status with spaces & symbols !@#$%");
    EXPECT_TRUE(callback_message.find('\n') != std::string::npos);
    EXPECT_EQ(callback_version, "v1.2.3-beta+build.123");
}

/**
 * Test: Multiple signals in rapid succession
 * Expected: All signals processed correctly, no memory corruption
 */
TEST_F(AsyncSignalParsingTest, MultipleRapidSignals) {
    std::atomic<int> callback_count(0);

    auto counting_callback = [](const char* status, const char* message,
                                const char* version, void* user_data) {
        (void)status; (void)message; (void)version;
        auto* counter = static_cast<std::atomic<int>*>(user_data);
        (*counter)++;
    };

    const int num_handlers = 5;
    for (int i = 0; i < num_handlers; i++) {
        int handler_id = rdkFwupdateMgr_checkForUpdate_async(
            (rdkFwupdateMgr_CheckForUpdateCallback)counting_callback,
            &callback_count);
        ASSERT_GT(handler_id, 0);
    }

    // Send multiple signals
    for (int i = 0; i < 10; i++) {
        rdkFwupdateMgr_async_simulate_signal_for_test(
            "UPDATE_AVAILABLE",
            "Signal number",
            "1.0.0"
        );
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Each handler should be invoked at least once
    EXPECT_GE(callback_count.load(), num_handlers);
}

/**
 * Test: Signal parsing with concurrent API calls
 * Expected: Thread-safe signal processing, no race conditions
 */
TEST_F(AsyncSignalParsingTest, SignalParsingWithConcurrentCalls) {
    std::atomic<int> callback_count(0);

    auto counting_callback = [](const char* status, const char* message,
                                const char* version, void* user_data) {
        (void)status; (void)message; (void)version;
        auto* counter = static_cast<std::atomic<int>*>(user_data);
        (*counter)++;
    };

    // Thread 1: Register callbacks
    std::thread register_thread([&]() {
        for (int i = 0; i < 10; i++) {
            int handler_id = rdkFwupdateMgr_checkForUpdate_async(
                (rdkFwupdateMgr_CheckForUpdateCallback)counting_callback,
                &callback_count);
            (void)handler_id;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });

    // Thread 2: Send signals
    std::thread signal_thread([&]() {
        for (int i = 0; i < 10; i++) {
            rdkFwupdateMgr_async_simulate_signal_for_test(
                "UPDATE_AVAILABLE", "Test", "1.0.0");
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });

    register_thread.join();
    signal_thread.join();

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Should have invoked callbacks
    EXPECT_GT(callback_count.load(), 0);
}

/**
 * Test: Memory leak in signal parsing
 * Expected: No memory leaks (run under Valgrind)
 */
TEST_F(AsyncSignalParsingTest, SignalParsingMemoryLeak) {
    const int num_iterations = 100;

    for (int i = 0; i < num_iterations; i++) {
        int handler_id = rdkFwupdateMgr_checkForUpdate_async(test_callback, nullptr);
        ASSERT_GT(handler_id, 0);

        rdkFwupdateMgr_async_simulate_signal_for_test(
            "UPDATE_AVAILABLE",
            "Test message",
            "1.0.0"
        );

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // All callbacks should complete without memory leaks
}

} // namespace

/**
 * Main test runner
 */
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
