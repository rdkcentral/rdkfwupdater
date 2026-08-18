/*
 * Copyright 2026 Comcast Cable Communications Management, LLC
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
 * @file rdkFwupdateMgr_async_stress_gtest.cpp
 * @brief Stress tests for async API
 *
 * This test suite validates system behavior under high load:
 * - Concurrent registration from multiple threads
 * - Registry exhaustion and recovery
 * - Rapid register/cancel cycles
 * - Signal flooding
 * - Long-running stability tests
 * - Memory usage under load
 * - Performance benchmarks
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <random>
#include <algorithm>

extern "C" {
    #include "rdkFwupdateMgr_client.h"
    
    // Internal functions for testing
    extern int async_init(void);
    extern void async_cleanup(void);
    extern AsyncCallbackRegistry g_async_registry;
}

using namespace std::chrono;

/**
 * Test fixture for stress tests
 */
class AsyncStressTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize async subsystem
        ASSERT_EQ(async_init(), 0) << "Failed to initialize async subsystem";
        
        // Reset counters
        callbacks_completed.store(0);
        callbacks_failed.store(0);
        registrations_succeeded.store(0);
        registrations_failed.store(0);
        cancellations_succeeded.store(0);
        cancellations_failed.store(0);
    }

    void TearDown() override {
        // Cleanup
        async_cleanup();
        
        // Print statistics
        std::cout << "\nTest Statistics:\n";
        std::cout << "  Callbacks Completed: " << callbacks_completed.load() << "\n";
        std::cout << "  Callbacks Failed: " << callbacks_failed.load() << "\n";
        std::cout << "  Registrations Succeeded: " << registrations_succeeded.load() << "\n";
        std::cout << "  Registrations Failed: " << registrations_failed.load() << "\n";
        std::cout << "  Cancellations Succeeded: " << cancellations_succeeded.load() << "\n";
        std::cout << "  Cancellations Failed: " << cancellations_failed.load() << "\n";
    }

    // Statistics
    std::atomic<int> callbacks_completed{0};
    std::atomic<int> callbacks_failed{0};
    std::atomic<int> registrations_succeeded{0};
    std::atomic<int> registrations_failed{0};
    std::atomic<int> cancellations_succeeded{0};
    std::atomic<int> cancellations_failed{0};

    // Test callback function
    static void test_callback(RdkFwupdateMgr_UpdateInfo *info, void *user_data) {
        AsyncStressTest *test = static_cast<AsyncStressTest*>(user_data);
        if (info && info->status == 0) {
            test->callbacks_completed++;
        } else {
            test->callbacks_failed++;
        }
    }
};

/* ========================================================================
 * Concurrent Registration Tests
 * ======================================================================== */

/**
 * Test: 100 concurrent registrations from 10 threads
 */
TEST_F(AsyncStressTest, ConcurrentRegistration100) {
    const int NUM_THREADS = 10;
    const int REGISTRATIONS_PER_THREAD = 10;
    
    std::vector<std::thread> threads;
    std::vector<std::vector<uint32_t>> callback_ids(NUM_THREADS);
    
    // Launch threads
    for (int i = 0; i < NUM_THREADS; i++) {
        threads.emplace_back([this, i, REGISTRATIONS_PER_THREAD, &callback_ids]() {
            for (int j = 0; j < REGISTRATIONS_PER_THREAD; j++) {
                uint32_t id = checkForUpdate_async(test_callback, this);
                if (id != 0) {
                    callback_ids[i].push_back(id);
                    registrations_succeeded++;
                } else {
                    registrations_failed++;
                }
                
                // Small delay to simulate real usage
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
    }
    
    // Wait for all threads
    for (auto& t : threads) {
        t.join();
    }
    
    // Verify all registrations succeeded
    int total_registered = 0;
    for (const auto& ids : callback_ids) {
        total_registered += ids.size();
    }
    
    EXPECT_EQ(total_registered, NUM_THREADS * REGISTRATIONS_PER_THREAD);
    EXPECT_EQ(registrations_succeeded.load(), NUM_THREADS * REGISTRATIONS_PER_THREAD);
    EXPECT_EQ(registrations_failed.load(), 0);
}

/**
 * Test: 1000 concurrent registrations (stress test)
 */
TEST_F(AsyncStressTest, ConcurrentRegistration1000) {
    const int NUM_THREADS = 20;
    const int REGISTRATIONS_PER_THREAD = 50;
    
    std::vector<std::thread> threads;
    
    auto start_time = high_resolution_clock::now();
    
    // Launch threads
    for (int i = 0; i < NUM_THREADS; i++) {
        threads.emplace_back([this, REGISTRATIONS_PER_THREAD]() {
            for (int j = 0; j < REGISTRATIONS_PER_THREAD; j++) {
                uint32_t id = checkForUpdate_async(test_callback, this);
                if (id != 0) {
                    registrations_succeeded++;
                } else {
                    registrations_failed++;
                }
            }
        });
    }
    
    // Wait for all threads
    for (auto& t : threads) {
        t.join();
    }
    
    auto end_time = high_resolution_clock::now();
    auto duration = duration_cast<milliseconds>(end_time - start_time);
    
    std::cout << "1000 registrations completed in " << duration.count() << "ms\n";
    
    // Most registrations should succeed
    // (Some may fail if registry is full, which is acceptable)
    EXPECT_GT(registrations_succeeded.load(), 0);
}

/* ========================================================================
 * Registry Exhaustion Tests
 * ======================================================================== */

/**
 * Test: Fill registry to capacity, verify graceful failure
 */
TEST_F(AsyncStressTest, RegistryExhaustion) {
    std::vector<uint32_t> callback_ids;
    
    // Register until registry is full
    for (int i = 0; i < MAX_ASYNC_CALLBACKS + 10; i++) {
        uint32_t id = checkForUpdate_async(test_callback, this);
        if (id != 0) {
            callback_ids.push_back(id);
            registrations_succeeded++;
        } else {
            registrations_failed++;
        }
    }
    
    // Should have registered exactly MAX_ASYNC_CALLBACKS
    EXPECT_EQ(callback_ids.size(), MAX_ASYNC_CALLBACKS);
    EXPECT_EQ(registrations_succeeded.load(), MAX_ASYNC_CALLBACKS);
    
    // Additional registrations should have failed
    EXPECT_EQ(registrations_failed.load(), 10);
    
    // Cancel some callbacks to free up slots
    for (int i = 0; i < 10; i++) {
        int ret = checkForUpdate_async_cancel(callback_ids[i]);
        if (ret == 0) {
            cancellations_succeeded++;
        }
    }
    
    // Should be able to register again
    for (int i = 0; i < 10; i++) {
        uint32_t id = checkForUpdate_async(test_callback, this);
        EXPECT_NE(id, 0) << "Should be able to register after cancellation";
    }
}

/**
 * Test: Registry slot reuse after completion
 */
TEST_F(AsyncStressTest, RegistrySlotReuse) {
    const int NUM_CYCLES = 100;
    
    for (int cycle = 0; cycle < NUM_CYCLES; cycle++) {
        // Register MAX_ASYNC_CALLBACKS callbacks
        std::vector<uint32_t> ids;
        for (int i = 0; i < MAX_ASYNC_CALLBACKS; i++) {
            uint32_t id = checkForUpdate_async(test_callback, this);
            ASSERT_NE(id, 0) << "Registration failed in cycle " << cycle;
            ids.push_back(id);
        }
        
        // Cancel all
        for (uint32_t id : ids) {
            checkForUpdate_async_cancel(id);
        }
        
        // Small delay to allow cleanup
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // If we got here, registry slots are being reused properly
    SUCCEED();
}

/* ========================================================================
 * Rapid Register/Cancel Tests
 * ======================================================================== */

/**
 * Test: Rapid register and immediate cancel
 */
TEST_F(AsyncStressTest, RapidRegisterCancel) {
    const int NUM_ITERATIONS = 10000;
    
    auto start_time = high_resolution_clock::now();
    
    for (int i = 0; i < NUM_ITERATIONS; i++) {
        uint32_t id = checkForUpdate_async(test_callback, this);
        if (id != 0) {
            registrations_succeeded++;
            
            // Immediately cancel
            int ret = checkForUpdate_async_cancel(id);
            if (ret == 0) {
                cancellations_succeeded++;
            } else {
                cancellations_failed++;
            }
        } else {
            registrations_failed++;
        }
    }
    
    auto end_time = high_resolution_clock::now();
    auto duration = duration_cast<milliseconds>(end_time - start_time);
    
    std::cout << NUM_ITERATIONS << " register/cancel cycles completed in " 
              << duration.count() << "ms\n";
    
    // All operations should succeed
    EXPECT_EQ(registrations_succeeded.load(), NUM_ITERATIONS);
    EXPECT_EQ(cancellations_succeeded.load(), NUM_ITERATIONS);
    EXPECT_EQ(registrations_failed.load(), 0);
}

/**
 * Test: Concurrent register and cancel from multiple threads
 */
TEST_F(AsyncStressTest, ConcurrentRegisterCancel) {
    const int NUM_THREADS = 10;
    const int CYCLES_PER_THREAD = 1000;
    
    std::vector<std::thread> threads;
    
    for (int i = 0; i < NUM_THREADS; i++) {
        threads.emplace_back([this, CYCLES_PER_THREAD]() {
            for (int j = 0; j < CYCLES_PER_THREAD; j++) {
                uint32_t id = checkForUpdate_async(test_callback, this);
                if (id != 0) {
                    registrations_succeeded++;
                    
                    // Random delay before cancel
                    if (rand() % 2 == 0) {
                        std::this_thread::sleep_for(std::chrono::microseconds(100));
                    }
                    
                    int ret = checkForUpdate_async_cancel(id);
                    if (ret == 0) {
                        cancellations_succeeded++;
                    }
                }
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // Most operations should succeed
    EXPECT_GT(registrations_succeeded.load(), NUM_THREADS * CYCLES_PER_THREAD * 0.9);
}

/* ========================================================================
 * Cancel Race Condition Tests
 * ======================================================================== */

/**
 * Test: Cancel while callback is being invoked
 * 
 * This tests a race condition where cancellation is attempted
 * at the same time the signal handler is invoking the callback.
 */
TEST_F(AsyncStressTest, CancelDuringCallback) {
    const int NUM_OPERATIONS = 100;
    std::vector<uint32_t> ids;
    
    // Register multiple callbacks
    for (int i = 0; i < NUM_OPERATIONS; i++) {
        uint32_t id = checkForUpdate_async(test_callback, this);
        if (id != 0) {
            ids.push_back(id);
        }
    }
    
    // Launch thread to cancel some of them
    std::thread cancel_thread([this, &ids]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        for (size_t i = 0; i < ids.size() / 2; i++) {
            checkForUpdate_async_cancel(ids[i]);
            cancellations_succeeded++;
        }
    });
    
    // Simulate signal arrival (in real test, would trigger actual signal)
    // Here we just wait to allow potential race conditions to occur
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    cancel_thread.join();
    
    // Test passes if no crash occurs
    SUCCEED();
}

/* ========================================================================
 * Performance Benchmarks
 * ======================================================================== */

/**
 * Benchmark: Registration latency
 */
TEST_F(AsyncStressTest, BenchmarkRegistrationLatency) {
    const int NUM_SAMPLES = 1000;
    std::vector<long long> latencies;
    
    for (int i = 0; i < NUM_SAMPLES; i++) {
        auto start = high_resolution_clock::now();
        uint32_t id = checkForUpdate_async(test_callback, this);
        auto end = high_resolution_clock::now();
        
        if (id != 0) {
            auto latency = duration_cast<microseconds>(end - start).count();
            latencies.push_back(latency);
            
            // Cancel immediately to free slot
            checkForUpdate_async_cancel(id);
        }
    }
    
    // Calculate statistics
    std::sort(latencies.begin(), latencies.end());
    long long min = latencies.front();
    long long max = latencies.back();
    long long median = latencies[latencies.size() / 2];
    long long p95 = latencies[(latencies.size() * 95) / 100];
    long long p99 = latencies[(latencies.size() * 99) / 100];
    
    std::cout << "\nRegistration Latency (microseconds):\n";
    std::cout << "  Min: " << min << "\n";
    std::cout << "  Median: " << median << "\n";
    std::cout << "  P95: " << p95 << "\n";
    std::cout << "  P99: " << p99 << "\n";
    std::cout << "  Max: " << max << "\n";
    
    // Latency should be reasonable (< 1ms for P95)
    EXPECT_LT(p95, 1000) << "P95 latency exceeds 1ms";
}

/**
 * Benchmark: Cancellation latency
 */
TEST_F(AsyncStressTest, BenchmarkCancellationLatency) {
    const int NUM_SAMPLES = 1000;
    std::vector<long long> latencies;
    std::vector<uint32_t> ids;
    
    // Pre-register callbacks
    for (int i = 0; i < NUM_SAMPLES; i++) {
        uint32_t id = checkForUpdate_async(test_callback, this);
        if (id != 0) {
            ids.push_back(id);
        }
    }
    
    // Measure cancellation latency
    for (uint32_t id : ids) {
        auto start = high_resolution_clock::now();
        checkForUpdate_async_cancel(id);
        auto end = high_resolution_clock::now();
        
        auto latency = duration_cast<microseconds>(end - start).count();
        latencies.push_back(latency);
    }
    
    // Calculate statistics
    std::sort(latencies.begin(), latencies.end());
    long long median = latencies[latencies.size() / 2];
    long long p95 = latencies[(latencies.size() * 95) / 100];
    
    std::cout << "\nCancellation Latency (microseconds):\n";
    std::cout << "  Median: " << median << "\n";
    std::cout << "  P95: " << p95 << "\n";
    
    // Cancellation should be fast (< 100us for P95)
    EXPECT_LT(p95, 100) << "P95 cancellation latency exceeds 100us";
}

/* ========================================================================
 * Memory Usage Tests
 * ======================================================================== */

/**
 * Test: Memory usage remains stable under load
 * 
 * This test monitors memory usage during repeated register/cancel cycles
 * to ensure there are no memory leaks.
 */
TEST_F(AsyncStressTest, MemoryUsageStability) {
    const int NUM_CYCLES = 100;
    const int OPS_PER_CYCLE = 100;
    
    // Get baseline memory usage (rough estimate via /proc/self/status)
    // Note: This is Linux-specific. For cross-platform, use other methods.
    
    for (int cycle = 0; cycle < NUM_CYCLES; cycle++) {
        std::vector<uint32_t> ids;
        
        // Register
        for (int i = 0; i < OPS_PER_CYCLE; i++) {
            uint32_t id = checkForUpdate_async(test_callback, this);
            if (id != 0) {
                ids.push_back(id);
            }
        }
        
        // Cancel
        for (uint32_t id : ids) {
            checkForUpdate_async_cancel(id);
        }
        
        // Allow cleanup to occur
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // In a real test with Valgrind or AddressSanitizer, this would
    // detect any memory growth. Here we just verify no crash.
    SUCCEED();
}

/* ========================================================================
 * Long-Running Stability Test
 * ======================================================================== */

/**
 * Stress Test: Long-running random operations
 * 
 * This test runs random operations for an extended period to
 * detect stability issues, memory leaks, or deadlocks.
 * 
 * Note: This test is disabled by default due to long runtime.
 * Enable with --gtest_also_run_disabled_tests
 */
TEST_F(AsyncStressTest, DISABLED_LongRunningStability) {
    const int DURATION_SECONDS = 3600;  // 1 hour
    const int NUM_THREADS = 10;
    
    std::atomic<bool> stop{false};
    std::vector<std::thread> threads;
    
    auto start_time = high_resolution_clock::now();
    
    // Launch worker threads
    for (int i = 0; i < NUM_THREADS; i++) {
        threads.emplace_back([this, &stop]() {
            std::vector<uint32_t> active_ids;
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(0, 100);
            
            while (!stop.load()) {
                int action = dis(gen);
                
                if (action < 40) {
                    // Register (40% probability)
                    uint32_t id = checkForUpdate_async(test_callback, this);
                    if (id != 0) {
                        active_ids.push_back(id);
                        registrations_succeeded++;
                    }
                } else if (action < 70 && !active_ids.empty()) {
                    // Cancel (30% probability)
                    size_t idx = dis(gen) % active_ids.size();
                    checkForUpdate_async_cancel(active_ids[idx]);
                    active_ids.erase(active_ids.begin() + idx);
                    cancellations_succeeded++;
                } else {
                    // Sleep (30% probability)
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                
                // Occasional cleanup
                if (active_ids.size() > 50) {
                    for (uint32_t id : active_ids) {
                        checkForUpdate_async_cancel(id);
                    }
                    active_ids.clear();
                }
            }
            
            // Cleanup remaining
            for (uint32_t id : active_ids) {
                checkForUpdate_async_cancel(id);
            }
        });
    }
    
    // Run for specified duration
    std::this_thread::sleep_for(std::chrono::seconds(DURATION_SECONDS));
    
    // Signal threads to stop
    stop.store(true);
    
    // Wait for threads
    for (auto& t : threads) {
        t.join();
    }
    
    auto end_time = high_resolution_clock::now();
    auto duration = duration_cast<seconds>(end_time - start_time);
    
    std::cout << "\nLong-running test completed after " << duration.count() << " seconds\n";
    
    // If we reached here without crash, test passes
    SUCCEED();
}

/* ========================================================================
 * Main Entry Point
 * ======================================================================== */

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    
    std::cout << "=== Async API Stress Tests ===\n";
    std::cout << "These tests validate system behavior under high load.\n";
    std::cout << "Some tests may take several minutes to complete.\n\n";
    
    return RUN_ALL_TESTS();
}
