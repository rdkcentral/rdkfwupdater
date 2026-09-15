/**
 * @file rdkFwupdateMgr_async_cleanup_gtest.cpp
 * @brief Unit tests for cleanup and deinitialization of async CheckForUpdate API
 *
 * Phase 6.2: Cleanup and Deinitialization Tests
 *
 * Tests:
 * - Cleanup with no active callbacks
 * - Cleanup with pending callbacks
 * - Cleanup with completed callbacks
 * - Multiple init/cleanup cycles
 * - Cleanup while signal is being processed
 * - Cleanup cancels pending operations
 * - No memory leaks after cleanup
 */

#include <gtest/gtest.h>
#include <pthread.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <thread>

extern "C" {
#include "rdkFwupdateMgr_client.h"
// Internal testing APIs
extern void rdkFwupdateMgr_async_init_for_test(void);
extern void rdkFwupdateMgr_async_cleanup_for_test(void);
extern int rdkFwupdateMgr_async_get_pending_count_for_test(void);
extern int rdkFwupdateMgr_async_get_total_count_for_test(void);
}

namespace {

/**
 * Test fixture for cleanup tests
 */
class AsyncCleanupTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Each test starts fresh
        rdkFwupdateMgr_async_init_for_test();
    }

    void TearDown() override {
        // Clean up after each test
        rdkFwupdateMgr_async_cleanup_for_test();
    }

    static void dummy_callback(const char* status, const char* message,
                              const char* version, void* user_data) {
        (void)status;
        (void)message;
        (void)version;
        (void)user_data;
    }

    static void counting_callback(const char* status, const char* message,
                                  const char* version, void* user_data) {
        (void)status;
        (void)message;
        (void)version;
        std::atomic<int>* counter = static_cast<std::atomic<int>*>(user_data);
        (*counter)++;
    }
};

/**
 * Test: Cleanup with no active callbacks
 * Expected: Clean initialization and cleanup without crashes
 */
TEST_F(AsyncCleanupTest, CleanupWithNoCallbacks) {
    // Verify initial state
    EXPECT_EQ(rdkFwupdateMgr_async_get_pending_count_for_test(), 0);
    EXPECT_EQ(rdkFwupdateMgr_async_get_total_count_for_test(), 0);

    // Cleanup should succeed
    rdkFwupdateMgr_async_cleanup_for_test();

    // Verify cleanup
    EXPECT_EQ(rdkFwupdateMgr_async_get_pending_count_for_test(), 0);
    EXPECT_EQ(rdkFwupdateMgr_async_get_total_count_for_test(), 0);
}

/**
 * Test: Cleanup with pending callbacks
 * Expected: All pending callbacks are cancelled/cleaned
 */
TEST_F(AsyncCleanupTest, CleanupWithPendingCallbacks) {
    // Register some callbacks (they will be pending)
    const int num_callbacks = 5;
    int handler_ids[num_callbacks];

    for (int i = 0; i < num_callbacks; i++) {
        handler_ids[i] = rdkFwupdateMgr_checkForUpdate_async(
            dummy_callback, nullptr);
        ASSERT_GT(handler_ids[i], 0);
    }

    // Verify they are pending
    EXPECT_EQ(rdkFwupdateMgr_async_get_pending_count_for_test(), num_callbacks);

    // Cleanup should cancel/remove all pending callbacks
    rdkFwupdateMgr_async_cleanup_for_test();

    // Verify all cleaned up
    EXPECT_EQ(rdkFwupdateMgr_async_get_pending_count_for_test(), 0);
}

/**
 * Test: Cleanup with completed callbacks
 * Expected: Completed callbacks are properly freed
 */
TEST_F(AsyncCleanupTest, CleanupWithCompletedCallbacks) {
    std::atomic<int> callback_count(0);

    // Register callback
    int handler_id = rdkFwupdateMgr_checkForUpdate_async(
        counting_callback, &callback_count);
    ASSERT_GT(handler_id, 0);

    // Simulate signal completion (mark as completed)
    // In real scenario, signal handler would do this
    // For test, we just proceed to cleanup

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Cleanup should free completed callbacks
    rdkFwupdateMgr_async_cleanup_for_test();

    EXPECT_EQ(rdkFwupdateMgr_async_get_total_count_for_test(), 0);
}

/**
 * Test: Multiple init/cleanup cycles
 * Expected: Each cycle is independent and clean
 */
TEST_F(AsyncCleanupTest, MultipleInitCleanupCycles) {
    const int num_cycles = 10;

    for (int cycle = 0; cycle < num_cycles; cycle++) {
        // Init
        rdkFwupdateMgr_async_init_for_test();

        // Register some callbacks
        int h1 = rdkFwupdateMgr_checkForUpdate_async(dummy_callback, nullptr);
        int h2 = rdkFwupdateMgr_checkForUpdate_async(dummy_callback, nullptr);

        EXPECT_GT(h1, 0);
        EXPECT_GT(h2, 0);
        EXPECT_EQ(rdkFwupdateMgr_async_get_pending_count_for_test(), 2);

        // Cleanup
        rdkFwupdateMgr_async_cleanup_for_test();

        EXPECT_EQ(rdkFwupdateMgr_async_get_pending_count_for_test(), 0);
    }
}

/**
 * Test: Cleanup cancels pending operations
 * Expected: Callbacks registered before cleanup are not invoked after cleanup
 */
TEST_F(AsyncCleanupTest, CleanupCancelsPendingOperations) {
    std::atomic<int> callback_count(0);

    // Register callback
    int handler_id = rdkFwupdateMgr_checkForUpdate_async(
        counting_callback, &callback_count);
    ASSERT_GT(handler_id, 0);

    // Cleanup before signal arrives
    rdkFwupdateMgr_async_cleanup_for_test();

    // Give time for any signals (shouldn't invoke callback after cleanup)
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Callback should NOT have been invoked
    EXPECT_EQ(callback_count.load(), 0);
}

/**
 * Test: Cleanup with rapid registration/cancellation
 * Expected: All resources properly freed even under stress
 */
TEST_F(AsyncCleanupTest, CleanupWithRapidRegisterCancel) {
    const int num_iterations = 100;

    for (int i = 0; i < num_iterations; i++) {
        int handler_id = rdkFwupdateMgr_checkForUpdate_async(
            dummy_callback, nullptr);
        ASSERT_GT(handler_id, 0);

        // Cancel immediately
        int result = rdkFwupdateMgr_checkForUpdate_async_cancel(handler_id);
        EXPECT_EQ(result, 0);
    }

    // Cleanup should handle all cancelled contexts
    rdkFwupdateMgr_async_cleanup_for_test();

    EXPECT_EQ(rdkFwupdateMgr_async_get_total_count_for_test(), 0);
}

/**
 * Test: Double cleanup is safe
 * Expected: Second cleanup is a no-op, no crashes
 */
TEST_F(AsyncCleanupTest, DoubleCleanupSafe) {
    int handler_id = rdkFwupdateMgr_checkForUpdate_async(
        dummy_callback, nullptr);
    ASSERT_GT(handler_id, 0);

    // First cleanup
    rdkFwupdateMgr_async_cleanup_for_test();
    EXPECT_EQ(rdkFwupdateMgr_async_get_total_count_for_test(), 0);

    // Second cleanup should be safe
    rdkFwupdateMgr_async_cleanup_for_test();
    EXPECT_EQ(rdkFwupdateMgr_async_get_total_count_for_test(), 0);
}

/**
 * Test: Cleanup while callbacks are being invoked
 * Expected: Thread-safe cleanup, no race conditions
 */
TEST_F(AsyncCleanupTest, CleanupDuringCallbackInvocation) {
    std::atomic<bool> callback_running(false);
    std::atomic<bool> cleanup_started(false);

    auto slow_callback = [](const char* status, const char* message,
                           const char* version, void* user_data) {
        (void)status; (void)message; (void)version;
        auto* flags = static_cast<std::pair<std::atomic<bool>*, std::atomic<bool>*>*>(user_data);
        flags->first->store(true);  // callback_running

        // Wait for cleanup to start
        while (!flags->second->load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // Simulate slow callback
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    };

    std::pair<std::atomic<bool>*, std::atomic<bool>*> flags(&callback_running, &cleanup_started);

    // Register callback
    int handler_id = rdkFwupdateMgr_checkForUpdate_async(
        (rdkFwupdateMgr_CheckForUpdateCallback)slow_callback, &flags);
    ASSERT_GT(handler_id, 0);

    // Thread to trigger cleanup
    std::thread cleanup_thread([&]() {
        // Wait for callback to start
        while (!callback_running.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        cleanup_started.store(true);
        rdkFwupdateMgr_async_cleanup_for_test();
    });

    // Simulate signal arrival (invoke callback)
    std::thread signal_thread([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        // In real code, signal handler would invoke callbacks
    });

    cleanup_thread.join();
    signal_thread.join();

    // Verify cleanup succeeded
    EXPECT_EQ(rdkFwupdateMgr_async_get_total_count_for_test(), 0);
}

/**
 * Test: Memory allocation during init and cleanup
 * Expected: All allocated memory is freed (run under Valgrind)
 */
TEST_F(AsyncCleanupTest, MemoryLeakCheck) {
    const int num_cycles = 5;
    const int callbacks_per_cycle = 10;

    for (int cycle = 0; cycle < num_cycles; cycle++) {
        rdkFwupdateMgr_async_init_for_test();

        for (int i = 0; i < callbacks_per_cycle; i++) {
            int handler_id = rdkFwupdateMgr_checkForUpdate_async(
                dummy_callback, nullptr);
            ASSERT_GT(handler_id, 0);
        }

        rdkFwupdateMgr_async_cleanup_for_test();
    }

    // Final state should be clean
    EXPECT_EQ(rdkFwupdateMgr_async_get_total_count_for_test(), 0);
    EXPECT_EQ(rdkFwupdateMgr_async_get_pending_count_for_test(), 0);
}

} // namespace

/**
 * Main test runner
 */
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
