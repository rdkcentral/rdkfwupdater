/**
 * @file rdkFwupdateMgr_async_threadsafety_gtest.cpp
 * @brief Unit tests for thread safety of async CheckForUpdate API
 *
 * Phase 6.5: Thread Safety Validation Tests
 *
 * Tests:
 * - Concurrent registration from multiple threads
 * - Concurrent cancellation from multiple threads
 * - Concurrent signal processing
 * - Registry lock contention
 * - Data race detection (run with ThreadSanitizer)
 * - Deadlock detection
 */

#include <gtest/gtest.h>
#include <pthread.h>
#include <atomic>
#include <vector>
#include <thread>
#include <chrono>
#include <random>
#include <algorithm>

extern "C" {
#include "rdkFwupdateMgr_client.h"
// Internal testing APIs
extern void rdkFwupdateMgr_async_init_for_test(void);
extern void rdkFwupdateMgr_async_cleanup_for_test(void);
extern void rdkFwupdateMgr_async_simulate_signal_for_test(
    const char* status, const char* message, const char* version);
extern int rdkFwupdateMgr_async_get_pending_count_for_test(void);
}

namespace {

/**
 * Test fixture for thread safety tests
 */
class AsyncThreadSafetyTest : public ::testing::Test {
protected:
    void SetUp() override {
        rdkFwupdateMgr_async_init_for_test();
    }

    void TearDown() override {
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
 * Test: Concurrent registration from multiple threads
 * Expected: All registrations succeed, no race conditions
 */
TEST_F(AsyncThreadSafetyTest, ConcurrentRegistration) {
    const int num_threads = 20;
    const int registrations_per_thread = 100;
    std::vector<std::thread> threads;
    std::atomic<int> success_count(0);
    std::atomic<int> failure_count(0);

    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([&]() {
            for (int i = 0; i < registrations_per_thread; i++) {
                int handler_id = rdkFwupdateMgr_checkForUpdate_async(
                    dummy_callback, nullptr);
                
                if (handler_id > 0) {
                    success_count++;
                } else {
                    failure_count++;
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // All registrations should succeed
    EXPECT_EQ(success_count.load(), num_threads * registrations_per_thread);
    EXPECT_EQ(failure_count.load(), 0);
}

/**
 * Test: Concurrent cancellation from multiple threads
 * Expected: All cancellations handled safely, no crashes
 */
TEST_F(AsyncThreadSafetyTest, ConcurrentCancellation) {
    const int num_callbacks = 1000;
    std::vector<int> handler_ids;

    // Register callbacks
    for (int i = 0; i < num_callbacks; i++) {
        int handler_id = rdkFwupdateMgr_checkForUpdate_async(
            dummy_callback, nullptr);
        ASSERT_GT(handler_id, 0);
        handler_ids.push_back(handler_id);
    }

    // Shuffle to randomize cancellation order
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(handler_ids.begin(), handler_ids.end(), g);

    // Cancel from multiple threads
    const int num_threads = 10;
    std::vector<std::thread> threads;
    std::atomic<int> cancel_success(0);

    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([&, t]() {
            int start = (t * num_callbacks) / num_threads;
            int end = ((t + 1) * num_callbacks) / num_threads;

            for (int i = start; i < end; i++) {
                int result = rdkFwupdateMgr_checkForUpdate_async_cancel(
                    handler_ids[i]);
                if (result == 0) {
                    cancel_success++;
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // Most cancellations should succeed (some may already be completed)
    EXPECT_GT(cancel_success.load(), 0);
}

/**
 * Test: Concurrent registration and cancellation
 * Expected: Thread-safe mixed operations
 */
TEST_F(AsyncThreadSafetyTest, ConcurrentRegisterAndCancel) {
    const int num_threads = 10;
    const int operations_per_thread = 200;
    std::vector<std::thread> threads;
    std::atomic<int> handler_id_counter(0);
    std::vector<int> handler_ids(num_threads * operations_per_thread);

    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([&, t]() {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(0, 1);

            for (int i = 0; i < operations_per_thread; i++) {
                if (dis(gen) == 0) {
                    // Register
                    int handler_id = rdkFwupdateMgr_checkForUpdate_async(
                        dummy_callback, nullptr);
                    if (handler_id > 0) {
                        int idx = handler_id_counter.fetch_add(1);
                        if (idx < (int)handler_ids.size()) {
                            handler_ids[idx] = handler_id;
                        }
                    }
                } else {
                    // Cancel random handler
                    int current = handler_id_counter.load();
                    if (current > 0) {
                        std::uniform_int_distribution<> id_dis(0, current - 1);
                        int idx = id_dis(gen);
                        if (idx < (int)handler_ids.size() && handler_ids[idx] > 0) {
                            rdkFwupdateMgr_checkForUpdate_async_cancel(
                                handler_ids[idx]);
                        }
                    }
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // Should complete without crashes or deadlocks
    SUCCEED();
}

/**
 * Test: Concurrent signal processing
 * Expected: All callbacks invoked correctly, no data races
 */
TEST_F(AsyncThreadSafetyTest, ConcurrentSignalProcessing) {
    const int num_callbacks = 100;
    std::atomic<int> callback_count(0);
    std::vector<int> handler_ids;

    // Register callbacks
    for (int i = 0; i < num_callbacks; i++) {
        int handler_id = rdkFwupdateMgr_checkForUpdate_async(
            counting_callback, &callback_count);
        ASSERT_GT(handler_id, 0);
        handler_ids.push_back(handler_id);
    }

    // Send multiple signals concurrently
    const int num_signal_threads = 5;
    std::vector<std::thread> threads;

    for (int t = 0; t < num_signal_threads; t++) {
        threads.emplace_back([&]() {
            for (int i = 0; i < 10; i++) {
                rdkFwupdateMgr_async_simulate_signal_for_test(
                    "UPDATE_AVAILABLE", "Test", "1.0.0");
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Each callback should be invoked at least once
    EXPECT_GE(callback_count.load(), num_callbacks);
}

/**
 * Test: Registry lock contention under high load
 * Expected: No deadlocks, correct behavior
 */
TEST_F(AsyncThreadSafetyTest, RegistryLockContention) {
    const int num_threads = 50;
    const int operations_per_thread = 50;
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([&]() {
            for (int i = 0; i < operations_per_thread; i++) {
                // Register
                int handler_id = rdkFwupdateMgr_checkForUpdate_async(
                    dummy_callback, nullptr);
                
                // Immediately cancel (causes lock contention)
                if (handler_id > 0) {
                    rdkFwupdateMgr_checkForUpdate_async_cancel(handler_id);
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // Should complete without deadlock
    SUCCEED();
}

/**
 * Test: Concurrent operations during signal processing
 * Expected: Thread-safe mixing of API calls and signal handling
 */
TEST_F(AsyncThreadSafetyTest, ConcurrentOperationsDuringSignals) {
    std::atomic<int> callback_count(0);
    std::atomic<bool> stop_flag(false);

    // Thread 1: Continuous registration
    std::thread register_thread([&]() {
        while (!stop_flag.load()) {
            int handler_id = rdkFwupdateMgr_checkForUpdate_async(
                counting_callback, &callback_count);
            (void)handler_id;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    // Thread 2: Continuous signal sending
    std::thread signal_thread([&]() {
        while (!stop_flag.load()) {
            rdkFwupdateMgr_async_simulate_signal_for_test(
                "UPDATE_AVAILABLE", "Test", "1.0.0");
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });

    // Thread 3: Continuous cancellation
    std::thread cancel_thread([&]() {
        int last_id = 1;
        while (!stop_flag.load()) {
            rdkFwupdateMgr_checkForUpdate_async_cancel(last_id++);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    // Let threads run for 2 seconds
    std::this_thread::sleep_for(std::chrono::seconds(2));
    stop_flag.store(true);

    register_thread.join();
    signal_thread.join();
    cancel_thread.join();

    // Should have processed callbacks
    EXPECT_GT(callback_count.load(), 0);
}

/**
 * Test: Stress test with many threads
 * Expected: Stable under high concurrency, no crashes
 */
TEST_F(AsyncThreadSafetyTest, ManyThreadsStressTest) {
    const int num_threads = 100;
    const int operations_per_thread = 100;
    std::vector<std::thread> threads;
    std::atomic<int> total_operations(0);

    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([&]() {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(0, 2);

            for (int i = 0; i < operations_per_thread; i++) {
                int op = dis(gen);
                
                if (op == 0) {
                    // Register
                    int handler_id = rdkFwupdateMgr_checkForUpdate_async(
                        dummy_callback, nullptr);
                    if (handler_id > 0) {
                        total_operations++;
                    }
                } else if (op == 1) {
                    // Cancel
                    rdkFwupdateMgr_checkForUpdate_async_cancel(i);
                    total_operations++;
                } else {
                    // Simulate signal
                    rdkFwupdateMgr_async_simulate_signal_for_test(
                        "TEST", "Test", "1.0.0");
                    total_operations++;
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // Should have processed many operations
    EXPECT_GT(total_operations.load(), num_threads * operations_per_thread / 2);
}

/**
 * Test: No priority inversion or lock ordering issues
 * Expected: Consistent lock acquisition order, no deadlocks
 */
TEST_F(AsyncThreadSafetyTest, LockOrderingConsistency) {
    const int num_iterations = 1000;
    std::vector<std::thread> threads;

    // Thread 1: Register then cancel pattern
    threads.emplace_back([&]() {
        for (int i = 0; i < num_iterations; i++) {
            int handler_id = rdkFwupdateMgr_checkForUpdate_async(
                dummy_callback, nullptr);
            if (handler_id > 0) {
                rdkFwupdateMgr_checkForUpdate_async_cancel(handler_id);
            }
        }
    });

    // Thread 2: Cancel then register pattern
    threads.emplace_back([&]() {
        for (int i = 0; i < num_iterations; i++) {
            rdkFwupdateMgr_checkForUpdate_async_cancel(i);
            int handler_id = rdkFwupdateMgr_checkForUpdate_async(
                dummy_callback, nullptr);
            (void)handler_id;
        }
    });

    // Thread 3: Signal processing
    threads.emplace_back([&]() {
        for (int i = 0; i < num_iterations / 10; i++) {
            rdkFwupdateMgr_async_simulate_signal_for_test(
                "TEST", "Test", "1.0.0");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    for (auto& thread : threads) {
        thread.join();
    }

    // Should complete without deadlock
    SUCCEED();
}

} // namespace

/**
 * Main test runner
 */
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
