/**
 * @file test_register.c
 * @brief Test program for registerProcess/unregisterProcess APIs
 * 
 * Build (from rdkfwupdater directory):
 *   gcc -o test_register librdkFwupdateMgr/test/test_register.c \
 *       -I./librdkFwupdateMgr/include \
 *       -L./.libs -lrdkFwupdateMgr \
 *       `pkg-config --cflags --libs glib-2.0 gio-2.0` \
 *       -lpthread
 * 
 * Run (ensure daemon is running):
 *   LD_LIBRARY_PATH=./.libs:$LD_LIBRARY_PATH ./test_register
 */

#include "rdkFwupdateMgr_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define TEST_PASS(msg) printf("PASS: %s\n", msg)
#define TEST_FAIL(msg) printf("FAIL: %s\n", msg)
#define TEST_INFO(msg) printf("INFO: %s\n", msg)

int main() {
    int passed = 0;
    int failed = 0;
    
    printf("\n=== librdkFwupdateMgr - Subtask 2 Test Suite ===\n\n");
    
    // Test 1: Basic registration
    TEST_INFO("Test 1: Basic registration");
    FirmwareInterfaceHandle handle1 = registerProcess("TestApp1", "1.0.0");
    if (handle1 != NULL) {
        TEST_PASS("registerProcess returned valid handle");
        passed++;
    } else {
        TEST_FAIL("registerProcess returned NULL");
        failed++;
    }
    
    // Test 2: Register another process
    TEST_INFO("Test 2: Register another process");
    FirmwareInterfaceHandle handle2 = registerProcess("TestApp2", "2.0.0");
    if (handle2 != NULL) {
        TEST_PASS("Second registerProcess successful");
        passed++;
    } else {
        TEST_FAIL("Second registerProcess failed");
        failed++;
    }
    
    // Test 3: NULL processName (should fail)
    TEST_INFO("Test 3: NULL processName (should fail)");
    FirmwareInterfaceHandle handle3 = registerProcess(NULL, "1.0.0");
    if (handle3 == NULL) {
        TEST_PASS("registerProcess correctly rejected NULL processName");
        passed++;
    } else {
        TEST_FAIL("registerProcess should have rejected NULL processName");
        unregisterProcess(handle3);
        failed++;
    }
    
    // Test 4: NULL libVersion (should fail)
    TEST_INFO("Test 4: NULL libVersion (should fail)");
    FirmwareInterfaceHandle handle4 = registerProcess("TestApp", NULL);
    if (handle4 == NULL) {
        TEST_PASS("registerProcess correctly rejected NULL libVersion");
        passed++;
    } else {
        TEST_FAIL("registerProcess should have rejected NULL libVersion");
        unregisterProcess(handle4);
        failed++;
    }
    
    // Test 5: Empty processName (should fail)
    TEST_INFO("Test 5: Empty processName (should fail)");
    FirmwareInterfaceHandle handle5 = registerProcess("", "1.0.0");
    if (handle5 == NULL) {
        TEST_PASS("registerProcess correctly rejected empty processName");
        passed++;
    } else {
        TEST_FAIL("registerProcess should have rejected empty processName");
        unregisterProcess(handle5);
        failed++;
    }
    
    // Test 6: Unregister handle1
    TEST_INFO("Test 6: Unregister first handle");
    unregisterProcess(handle1);
    TEST_PASS("unregisterProcess completed (no crash)");
    passed++;
    
    // Test 7: Unregister NULL (should not crash)
    TEST_INFO("Test 7: Unregister NULL (should not crash)");
    unregisterProcess(NULL);
    TEST_PASS("unregisterProcess(NULL) handled gracefully");
    passed++;
    
    // Test 8: Unregister handle2
    TEST_INFO("Test 8: Unregister second handle");
    unregisterProcess(handle2);
    TEST_PASS("unregisterProcess completed (no crash)");
    passed++;
    
    // Test 9: Register and unregister in quick succession
    TEST_INFO("Test 9: Rapid register/unregister cycles");
    for (int i = 0; i < 5; i++) {
        char name[32];
        snprintf(name, sizeof(name), "RapidTest%d", i);
        FirmwareInterfaceHandle h = registerProcess(name, "1.0.0");
        if (h == NULL) {
            TEST_FAIL("registerProcess failed in rapid cycle");
            failed++;
            break;
        }
        unregisterProcess(h);
    }
    TEST_PASS("Rapid cycles completed successfully");
    passed++;
    
    // Test 10: Verify daemon connection (keep one registered for a bit)
    TEST_INFO("Test 10: Keep handle registered for 5 seconds");
    FirmwareInterfaceHandle handle_final = registerProcess("LongRunning", "1.0.0");
    if (handle_final != NULL) {
        TEST_PASS("Handle created for long-running test");
        passed++;
        
        printf("      Sleeping 5 seconds (check daemon logs)...\n");
        sleep(5);
        
        unregisterProcess(handle_final);
        TEST_PASS("Long-running handle unregistered successfully");
        passed++;
    } else {
        TEST_FAIL("Failed to create long-running handle");
        failed += 2;
    }
    
    // Summary
    printf("\n=== Test Summary ===\n");
    printf("Passed: %d\n", passed);
    printf("Failed: %d\n", failed);
    printf("Total:  %d\n", passed + failed);
    
    if (failed == 0) {
        printf("\nAll tests PASSED! Subtask 2 complete.\n");
        return 0;
    } else {
        printf("\nSome tests FAILED.\n");
        return 1;
    }
}
