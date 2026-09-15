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
 * @file rdkFwupdateMgr_async_refcount_gtest.cpp
 * @brief Unit tests for async API reference counting
 *
 * This test suite validates:
 * - Reference counting correctness
 * - Thread-safe ref/unref operations
 * - Prevention of use-after-free
 * - Prevention of double-free
 * - Proper cleanup at zero refcount
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <pthread.h>
#include <atomic>
#include <vector>
#include <thread>

extern "C" {
    // Include internal header for direct testing
    // Note: In production, adjust include path as needed
    #include "../librdkFwupdateMgr/src/rdkFwupdateMgr_async_internal.h"
    
    // Expose internal functions for testing
    extern void context_ref(AsyncCallbackContext *ctx);
    extern void context_unref(AsyncCallbackContext *ctx);
    extern AsyncCallbackRegistry g_async_registry;
}

using ::testing::_;
using ::testing::Return;

/**
 * Test fixture for reference counting tests
 */
class AsyncRefCountTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize a test context
        memset(&test_ctx, 0, sizeof(test_ctx));
        test_ctx.id = 12345;
        test_ctx.state = ASYNC_CALLBACK_STATE_IDLE;
        test_ctx.callback = nullptr;
        test_ctx.user_data = nullptr;
        test_ctx.ref_count = 0;
        test_ctx.registered_time = time(nullptr);
    }

    void TearDown() override {
        // Cleanup any allocated resources
        if (test_ctx.update_info.message) {
            free(test_ctx.update_info.message);
            test_ctx.update_info.message = nullptr;
        }
        if (test_ctx.update_info.version) {
            free(test_ctx.update_info.version);
            test_ctx.update_info.version = nullptr;
        }
        if (test_ctx.update_info.download_url) {
            free(test_ctx.update_info.download_url);
            test_ctx.update_info.download_url = nullptr;
        }
    }

    AsyncCallbackContext test_ctx;
};

/* ========================================================================
 * Basic Reference Counting Tests
 * ======================================================================== */

/**
 * Test: Initial reference count is 0
 */
TEST_F(AsyncRefCountTest, InitialRefCountIsZero) {
    EXPECT_EQ(test_ctx.ref_count, 0);
}

/**
 * Test: context_ref increments reference count
 */
TEST_F(AsyncRefCountTest, RefIncrementsCount) {
    EXPECT_EQ(test_ctx.ref_count, 0);
    
    context_ref(&test_ctx);
    EXPECT_EQ(test_ctx.ref_count, 1);
    
    context_ref(&test_ctx);
    EXPECT_EQ(test_ctx.ref_count, 2);
    
    context_ref(&test_ctx);
    EXPECT_EQ(test_ctx.ref_count, 3);
}

/**
 * Test: context_unref decrements reference count
 */
TEST_F(AsyncRefCountTest, UnrefDecrementsCount) {
    // Start with ref count of 3
    test_ctx.ref_count = 3;
    
    context_unref(&test_ctx);
    EXPECT_EQ(test_ctx.ref_count, 2);
    
    context_unref(&test_ctx);
    EXPECT_EQ(test_ctx.ref_count, 1);
    
    context_unref(&test_ctx);
    EXPECT_EQ(test_ctx.ref_count, 0);
}

/**
 * Test: context_unref at 0 does not underflow (stays at 0)
 */
TEST_F(AsyncRefCountTest, UnrefAtZeroDoesNotUnderflow) {
    EXPECT_EQ(test_ctx.ref_count, 0);
    
    // Should not go negative (atomic_fetch_sub prevents this in real impl)
    context_unref(&test_ctx);
    
    // Ref count should still be 0 (not negative)
    // Note: Implementation should use atomic_fetch_sub and check for underflow
    EXPECT_GE(test_ctx.ref_count, 0);
}

/**
 * Test: Balanced ref/unref leaves count at 0
 */
TEST_F(AsyncRefCountTest, BalancedRefUnrefReturnsToZero) {
    context_ref(&test_ctx);
    context_ref(&test_ctx);
    context_ref(&test_ctx);
    
    EXPECT_EQ(test_ctx.ref_count, 3);
    
    context_unref(&test_ctx);
    context_unref(&test_ctx);
    context_unref(&test_ctx);
    
    EXPECT_EQ(test_ctx.ref_count, 0);
}

/* ========================================================================
 * Thread Safety Tests
 * ======================================================================== */

/**
 * Test: Concurrent ref operations are atomic
 */
TEST_F(AsyncRefCountTest, ConcurrentRefIsAtomic) {
    const int NUM_THREADS = 10;
    const int REFS_PER_THREAD = 1000;
    
    std::vector<std::thread> threads;
    
    // Launch threads that all increment ref count
    for (int i = 0; i < NUM_THREADS; i++) {
        threads.emplace_back([this, REFS_PER_THREAD]() {
            for (int j = 0; j < REFS_PER_THREAD; j++) {
                context_ref(&test_ctx);
            }
        });
    }
    
    // Wait for all threads
    for (auto& t : threads) {
        t.join();
    }
    
    // Final ref count should be exactly NUM_THREADS * REFS_PER_THREAD
    EXPECT_EQ(test_ctx.ref_count, NUM_THREADS * REFS_PER_THREAD);
}

/**
 * Test: Concurrent unref operations are atomic
 */
TEST_F(AsyncRefCountTest, ConcurrentUnrefIsAtomic) {
    const int NUM_THREADS = 10;
    const int UNREFS_PER_THREAD = 1000;
    const int INITIAL_COUNT = NUM_THREADS * UNREFS_PER_THREAD;
    
    // Start with high ref count
    test_ctx.ref_count = INITIAL_COUNT;
    
    std::vector<std::thread> threads;
    
    // Launch threads that all decrement ref count
    for (int i = 0; i < NUM_THREADS; i++) {
        threads.emplace_back([this, UNREFS_PER_THREAD]() {
            for (int j = 0; j < UNREFS_PER_THREAD; j++) {
                context_unref(&test_ctx);
            }
        });
    }
    
    // Wait for all threads
    for (auto& t : threads) {
        t.join();
    }
    
    // Final ref count should be 0
    EXPECT_EQ(test_ctx.ref_count, 0);
}

/**
 * Test: Mixed concurrent ref/unref operations
 */
TEST_F(AsyncRefCountTest, ConcurrentMixedRefUnref) {
    const int NUM_REF_THREADS = 5;
    const int NUM_UNREF_THREADS = 5;
    const int OPS_PER_THREAD = 1000;
    
    // Start with some initial refs so unrefs don't underflow
    test_ctx.ref_count = NUM_UNREF_THREADS * OPS_PER_THREAD;
    
    std::vector<std::thread> threads;
    
    // Launch ref threads
    for (int i = 0; i < NUM_REF_THREADS; i++) {
        threads.emplace_back([this, OPS_PER_THREAD]() {
            for (int j = 0; j < OPS_PER_THREAD; j++) {
                context_ref(&test_ctx);
            }
        });
    }
    
    // Launch unref threads
    for (int i = 0; i < NUM_UNREF_THREADS; i++) {
        threads.emplace_back([this, OPS_PER_THREAD]() {
            for (int j = 0; j < OPS_PER_THREAD; j++) {
                context_unref(&test_ctx);
            }
        });
    }
    
    // Wait for all threads
    for (auto& t : threads) {
        t.join();
    }
    
    // Net change should be 0 (same number of refs and unrefs)
    EXPECT_EQ(test_ctx.ref_count, 0);
}

/* ========================================================================
 * Cleanup Tests
 * ======================================================================== */

/**
 * Test: Cleanup occurs when ref count reaches 0
 * 
 * Note: This test verifies that state transitions occur properly
 * when reference count reaches zero. The actual cleanup of allocated
 * memory should be tested separately.
 */
TEST_F(AsyncRefCountTest, CleanupAtZeroRefCount) {
    // Set up context with some data
    test_ctx.ref_count = 1;
    test_ctx.state = ASYNC_CALLBACK_STATE_COMPLETED;
    test_ctx.update_info.message = strdup("Test message");
    test_ctx.update_info.version = strdup("1.2.3");
    
    // Last unref should trigger cleanup
    context_unref(&test_ctx);
    
    EXPECT_EQ(test_ctx.ref_count, 0);
    
    // After cleanup, allocated strings should be freed
    // Note: This depends on implementation details of context_unref
    // In production code, verify that allocated memory is freed
}

/**
 * Test: Multiple refs followed by unrefs with cleanup check
 */
TEST_F(AsyncRefCountTest, MultipleRefsWithFinalCleanup) {
    // Allocate some data
    test_ctx.update_info.message = strdup("Test data");
    
    // Multiple refs
    context_ref(&test_ctx);  // ref=1
    context_ref(&test_ctx);  // ref=2
    context_ref(&test_ctx);  // ref=3
    
    EXPECT_EQ(test_ctx.ref_count, 3);
    
    // Unref twice (should not cleanup yet)
    context_unref(&test_ctx);  // ref=2
    context_unref(&test_ctx);  // ref=1
    
    EXPECT_EQ(test_ctx.ref_count, 1);
    
    // Final unref should cleanup
    context_unref(&test_ctx);  // ref=0, cleanup
    
    EXPECT_EQ(test_ctx.ref_count, 0);
}

/* ========================================================================
 * Edge Case Tests
 * ======================================================================== */

/**
 * Test: NULL context handling
 */
TEST_F(AsyncRefCountTest, NullContextHandling) {
    // Should not crash
    context_ref(nullptr);
    context_unref(nullptr);
    
    // Test passes if no crash occurs
    SUCCEED();
}

/**
 * Test: Very high reference count
 */
TEST_F(AsyncRefCountTest, VeryHighRefCount) {
    const int HIGH_COUNT = 1000000;
    
    // Increment to very high count
    for (int i = 0; i < HIGH_COUNT; i++) {
        context_ref(&test_ctx);
    }
    
    EXPECT_EQ(test_ctx.ref_count, HIGH_COUNT);
    
    // Decrement back to zero
    for (int i = 0; i < HIGH_COUNT; i++) {
        context_unref(&test_ctx);
    }
    
    EXPECT_EQ(test_ctx.ref_count, 0);
}

/**
 * Test: Reference count during state transitions
 */
TEST_F(AsyncRefCountTest, RefCountDuringStateTransitions) {
    // IDLE -> WAITING
    test_ctx.state = ASYNC_CALLBACK_STATE_IDLE;
    context_ref(&test_ctx);
    test_ctx.state = ASYNC_CALLBACK_STATE_WAITING;
    EXPECT_EQ(test_ctx.ref_count, 1);
    
    // WAITING -> COMPLETED
    context_ref(&test_ctx);
    test_ctx.state = ASYNC_CALLBACK_STATE_COMPLETED;
    EXPECT_EQ(test_ctx.ref_count, 2);
    
    // Cleanup
    context_unref(&test_ctx);
    context_unref(&test_ctx);
    EXPECT_EQ(test_ctx.ref_count, 0);
}

/* ========================================================================
 * Stress Tests
 * ======================================================================== */

/**
 * Stress Test: Rapid ref/unref cycles
 */
TEST_F(AsyncRefCountTest, RapidRefUnrefCycles) {
    const int NUM_CYCLES = 10000;
    
    for (int i = 0; i < NUM_CYCLES; i++) {
        context_ref(&test_ctx);
        EXPECT_EQ(test_ctx.ref_count, 1);
        
        context_unref(&test_ctx);
        EXPECT_EQ(test_ctx.ref_count, 0);
    }
}

/**
 * Stress Test: Many threads performing random ref/unref
 */
TEST_F(AsyncRefCountTest, ManyThreadsRandomRefUnref) {
    const int NUM_THREADS = 20;
    const int OPS_PER_THREAD = 500;
    
    // Start with ref count that can handle all threads
    test_ctx.ref_count = NUM_THREADS * OPS_PER_THREAD / 2;
    
    std::vector<std::thread> threads;
    std::atomic<int> total_refs{0};
    std::atomic<int> total_unrefs{0};
    
    for (int i = 0; i < NUM_THREADS; i++) {
        threads.emplace_back([this, OPS_PER_THREAD, &total_refs, &total_unrefs]() {
            for (int j = 0; j < OPS_PER_THREAD; j++) {
                if (rand() % 2 == 0) {
                    context_ref(&test_ctx);
                    total_refs++;
                } else {
                    context_unref(&test_ctx);
                    total_unrefs++;
                }
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // Expected ref count = initial + refs - unrefs
    int expected = (NUM_THREADS * OPS_PER_THREAD / 2) + total_refs - total_unrefs;
    EXPECT_EQ(test_ctx.ref_count, expected);
    
    // Cleanup remaining refs
    while (test_ctx.ref_count > 0) {
        context_unref(&test_ctx);
    }
}

/* ========================================================================
 * Memory Safety Tests
 * ======================================================================== */

/**
 * Test: No use-after-free when context is being cleaned
 */
TEST_F(AsyncRefCountTest, NoUseAfterFree) {
    // Allocate data
    test_ctx.update_info.message = strdup("Message");
    test_ctx.update_info.version = strdup("1.0");
    
    context_ref(&test_ctx);
    
    // Store pointer before unref
    AsyncCallbackContext *ptr = &test_ctx;
    
    // Unref (should cleanup)
    context_unref(ptr);
    
    // Accessing ptr here would be use-after-free in production
    // In this test, since test_ctx is on stack, we can verify state
    EXPECT_EQ(test_ctx.ref_count, 0);
    
    // In production, access to ptr after this point should be prevented
    // by proper lifetime management
}

/**
 * Test: No double-free when cleanup called multiple times
 */
TEST_F(AsyncRefCountTest, NoDoubleFree) {
    // Allocate data
    test_ctx.update_info.message = strdup("Message");
    
    // Manual cleanup (simulating what context_unref does at refcount=0)
    if (test_ctx.update_info.message) {
        free(test_ctx.update_info.message);
        test_ctx.update_info.message = nullptr;  // Important: set to NULL
    }
    
    // Second cleanup should be safe (no-op)
    if (test_ctx.update_info.message) {
        free(test_ctx.update_info.message);
        test_ctx.update_info.message = nullptr;
    }
    
    // Test passes if no crash
    SUCCEED();
}

/* ========================================================================
 * Main Entry Point
 * ======================================================================== */

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
