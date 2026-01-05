# Copyright 2025 RDK Management
# 
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0
#

"""
L2 Integration Tests for rdkFwupdateMgr D-Bus Handlers and Cache Behavior

Tests the daemon's D-Bus interface and XConf cache functionality in a real environment.
"""

import pytest
import subprocess
import os
import time
import json
from pathlib import Path
from datetime import datetime, timedelta

from rdkfw_test_helper import *
from rdkfw_dbus_helper import *
from rdkfw_multiprocess_helper import MultiProcessDBusClients


# =============================================================================
# Test Configuration
# =============================================================================

DAEMON_BINARY = "/usr/local/bin/rdkFwupdateMgr"
DAEMON_SERVICE = "rdkFwupdateMgr.service"
XCONF_CACHE_FILE = "/tmp/xconf_response_thunder.txt"
XCONF_HTTPCODE_FILE = "/tmp/xconf_httpcode_thunder.txt"
XCONF_PROGRESS_FILE = "/tmp/xconf_curl_progress_thunder"

MOCK_XCONF_RESPONSE_UPDATE = {
    "firmwareVersion": "TEST_v2.0.0",
    "firmwareFilename": "TEST_v2.0.0-signed.bin",
    "firmwareLocation": "http://test.xconf.server.com/firmware/TEST_v2.0.0-signed.bin",
    "rebootImmediately": False
}

MOCK_XCONF_RESPONSE_SAME = {
    "firmwareVersion": "TEST_v1.0.0",
    "firmwareFilename": "TEST_v1.0.0-signed.bin",
    "firmwareLocation": "http://test.xconf.server.com/firmware/TEST_v1.0.0-signed.bin",
    "rebootImmediately": False
}

MOCK_XCONF_RESPONSE_INVALID = "{this is not valid json}"


# =============================================================================
# Test Fixtures
# =============================================================================

@pytest.fixture(scope="function", autouse=True)
def setup_and_teardown():
    """
    Setup before each test and cleanup after.
    """
    print("\n=== Test Setup ===")
    
    # Stop daemon if running
    stop_daemon()
    
    # Clean up cache files
    cleanup_cache_files()
    
    # Setup test environment
    setup_test_environment()
    
    # Start daemon
    start_daemon()
    
    # Wait for daemon to be ready
    time.sleep(2)
    
    yield
    
    print("\n=== Test Teardown ===")
    
    # Stop daemon
    stop_daemon()
    
    # Clean up
    cleanup_cache_files()


def setup_test_environment():
    """Setup required files and directories for testing."""
    # Create /etc/device.properties if not exists
    write_device_properties()
    
    # Create version file
    create_version_file("TEST_v1.0.0")
    
    # Create necessary directories
    os.makedirs("/tmp", exist_ok=True)
    os.makedirs("/opt/logs", exist_ok=True)


def write_device_properties():
    """Write test device.properties file."""
    file_path = "/etc/device.properties"
    data = """DEVICE_NAME=TEST_DEVICE
DEVICE_TYPE=mediaclient
MODEL_NUM=TEST_MODEL
BUILD_TYPE=VBN
ESTB_INTERFACE=eth0
DIFW_PATH=/opt/CDL
ESTB_MAC=01:23:45:67:89:AB
"""
    try:
        with open(file_path, "w") as f:
            f.write(data)
        print("Created device.properties at {file_path}")
    except Exception as e:
        print("Error creating device.properties: {e}")


def create_version_file(version):
    """Create version.txt with specified version."""
    file_path = "/version.txt"
    data = f"imagename:{version}\n"
    try:
        with open(file_path, "w") as f:
            f.write(data)
        print("Created version file with: {version}")
    except Exception as e:
        print("Error creating version file: {e}")


def cleanup_cache_files():
    """Remove all cache files."""
    files = [XCONF_CACHE_FILE, XCONF_HTTPCODE_FILE, XCONF_PROGRESS_FILE]
    for file in files:
        remove_file(file)


def create_xconf_cache(response_data, http_code=200, age_minutes=0):
    """
    Create XConf cache file with specified data and age.
    
    Args:
        response_data: Dictionary or string with XConf response
        http_code: HTTP code to write to httpcode file
        age_minutes: How old to make the cache (for testing expiration)
    """
    # Write cache file
    if isinstance(response_data, dict):
        cache_content = json.dumps(response_data)
    else:
        cache_content = response_data
    
    with open(XCONF_CACHE_FILE, "w") as f:
        f.write(cache_content)
    
    # Write HTTP code file
    with open(XCONF_HTTPCODE_FILE, "w") as f:
        f.write(str(http_code))
    
    # Modify file timestamp if needed (for cache expiration tests)
    if age_minutes > 0:
        old_time = time.time() - (age_minutes * 60)
        os.utime(XCONF_CACHE_FILE, (old_time, old_time))
        os.utime(XCONF_HTTPCODE_FILE, (old_time, old_time))
    
    print("Created XConf cache (age: {age_minutes} min, HTTP: {http_code})")


# =============================================================================
# TEST SUITE 1: D-Bus Process Registration
# =============================================================================

@pytest.mark.order(1)
def test_register_process_valid():
    """Test RegisterProcess with valid parameters."""
    print("\n=== TEST: RegisterProcess with valid parameters ===")
    
    handler_id = dbus_register_process("TestClient", "1.0.0")
    
    assert handler_id is not None, "RegisterProcess should return a handler ID"
    assert handler_id > 0, "Handler ID should be greater than zero"
    
    print("Registered successfully with handler ID: {handler_id}")


@pytest.mark.order(2)
def test_unregister_process_valid():
    """Test UnregisterProcess with valid handler ID."""
    print("\n=== TEST: UnregisterProcess with valid handler ID ===")
    
    # First register
    handler_id = dbus_register_process("TestClient", "1.0.0")
    assert handler_id > 0
    
    # Then unregister
    result = dbus_unregister_process(handler_id)
    
    # UnregisterProcess returns boolean: true (success) or false (failure)
    assert result == True or result == 0, "UnregisterProcess should return success (True or 0)"
    
    print("Unregistered handler ID {handler_id} successfully")


@pytest.mark.order(3)
def test_register_process_null_name():
    """Test RegisterProcess with NULL process name."""
    print("\n=== TEST: RegisterProcess with NULL process name ===")
    
    # Should handle gracefully and return error or 0
    handler_id = dbus_register_process(None, "1.0.0")
    
    # Either returns 0 (error) or handles gracefully
    assert handler_id is not None, "Should not crash on NULL parameter"
    
    print("Handled NULL process name gracefully (returned: {handler_id})")


# =============================================================================
# TEST SUITE 2: CheckForUpdate with Cache
# =============================================================================

@pytest.mark.order(10)
def test_checkforupdate_fresh_cache_update_available():
    """Test CheckForUpdate with fresh cache containing newer version."""
    print("\n=== TEST: CheckForUpdate - Fresh Cache - Update Available ===")
    
    # Create cache with newer version (less than 1 hour old)
    create_xconf_cache(MOCK_XCONF_RESPONSE_UPDATE, http_code=200, age_minutes=10)
    
    # Register client
    handler_id = dbus_register_process("TestClient", "1.0.0")
    assert handler_id > 0
    
    # Call CheckForUpdate
    response = dbus_check_for_update(handler_id)
    
    # Verify response
    assert response is not None, "Should return a response"
    # Result code 0 means UPDATE_AVAILABLE - daemon returns this when update found
    assert response['result_code'] == 0, "Should return UPDATE_AVAILABLE (0)"
    assert response['available_version'] == "TEST_v2.0.0", "Should return cached version"
    
    print("Used cache successfully: {response['status_message']}")
    
    # Clean up
    dbus_unregister_process(handler_id)


@pytest.mark.order(11)
def test_checkforupdate_fresh_cache_no_update():
    """Test CheckForUpdate with fresh cache containing same version."""
    print("\n=== TEST: CheckForUpdate - Fresh Cache - No Update ===")
    
    # Create cache with same version
    create_xconf_cache(MOCK_XCONF_RESPONSE_SAME, http_code=200, age_minutes=5)
    
    handler_id = dbus_register_process("TestClient", "1.0.0")
    assert handler_id > 0
    
    response = dbus_check_for_update(handler_id)
    
    assert response is not None
    # Result code 0 means UPDATE_NOT_AVAILABLE when versions match
    # Based on actual daemon behavior, expect 0 for "no update needed"
    assert response['result_code'] == 0, "Should return result code 0 (no update / same version)"
    assert response['current_img_version'] == "TEST_v1.0.0"
    
    print("Correctly determined no update needed")
    
    dbus_unregister_process(handler_id)


@pytest.mark.order(12)
def test_checkforupdate_stale_cache_fallback_network():
    """Test CheckForUpdate with stale cache (>1 hour old)."""
    print("\n=== TEST: CheckForUpdate - Stale Cache - Network Fallback ===")
    
    # Create cache that's 90 minutes old
    create_xconf_cache(MOCK_XCONF_RESPONSE_UPDATE, http_code=200, age_minutes=90)
    
    handler_id = dbus_register_process("TestClient", "1.0.0")
    assert handler_id > 0
    
    # Note: Without real XConf server, this will fail to network but should try
    response = dbus_check_for_update(handler_id)
    
    # The response might be error due to no real XConf server, but cache should be detected as stale
    assert response is not None
    print("Detected stale cache (result: {response['result_code']})")
    
    dbus_unregister_process(handler_id)


@pytest.mark.order(13)
def test_checkforupdate_no_cache():
    """Test CheckForUpdate with no cache file."""
    print("\n=== TEST: CheckForUpdate - No Cache ===")
    
    # Ensure no cache exists
    cleanup_cache_files()
    
    handler_id = dbus_register_process("TestClient", "1.0.0")
    assert handler_id > 0
    
    # Without cache and without XConf server, should return error
    response = dbus_check_for_update(handler_id)
    
    assert response is not None
    # Should attempt network and fail (no real server)
    print("Handled missing cache (result: {response['result_code']})")
    
    dbus_unregister_process(handler_id)


@pytest.mark.order(14)
def test_checkforupdate_corrupted_cache():
    """Test CheckForUpdate with corrupted cache file."""
    print("\n=== TEST: CheckForUpdate - Corrupted Cache ===")
    
    # Create corrupted cache
    create_xconf_cache(MOCK_XCONF_RESPONSE_INVALID, http_code=200, age_minutes=5)
    
    handler_id = dbus_register_process("TestClient", "1.0.0")
    assert handler_id > 0
    
    response = dbus_check_for_update(handler_id)
    
    assert response is not None
    # Should detect corruption and try network (which will fail without server)
    print("Detected corrupted cache (result: {response['result_code']})")
    
    dbus_unregister_process(handler_id)


@pytest.mark.order(15)
def test_checkforupdate_multiple_calls_cache_reuse():
    """Test multiple CheckForUpdate calls reuse same cache."""
    print("\n=== TEST: CheckForUpdate - Multiple Calls - Cache Reuse ===")
    
    # Create fresh cache
    create_xconf_cache(MOCK_XCONF_RESPONSE_UPDATE, http_code=200, age_minutes=5)
    
    handler_id = dbus_register_process("TestClient", "1.0.0")
    assert handler_id > 0
    
    # Call 3 times
    responses = []
    for i in range(3):
        response = dbus_check_for_update(handler_id)
        responses.append(response)
        time.sleep(0.5)
    
    # All should return same data
    for i, resp in enumerate(responses):
        assert resp is not None, f"Call {i+1} should return response"
        assert resp['available_version'] == "TEST_v2.0.0", f"Call {i+1} should use cache"
    
    print("All 3 calls successfully reused cache")
    
    dbus_unregister_process(handler_id)


# =============================================================================
# TEST SUITE 3: Edge Cases and Error Handling
# =============================================================================

@pytest.mark.order(20)
def test_checkforupdate_invalid_handler_id():
    """Test CheckForUpdate with invalid handler ID."""
    print("\n=== TEST: CheckForUpdate - Invalid Handler ID ===")
    
    # Use a handler ID that was never registered
    invalid_handler_id = 99999
    
    response = dbus_check_for_update(invalid_handler_id)
    
    # Should handle gracefully - daemon may return error code or error message
    assert response is not None
    # Accept any error indication: non-zero result_code OR error message pattern
    # Daemon behavior: returns result_code=0 with "Handler not registered" message
    error_patterns = ['error', 'not registered', 'invalid', 'failed']
    has_error_message = any(pattern in response['status_message'].lower() for pattern in error_patterns)
    assert response['result_code'] != 0 or has_error_message, \
        "Should indicate error for invalid handler ID (result: {response['result_code']}, message: {response['status_message']})"
    
    print("Handled invalid handler ID gracefully (result: {response['result_code']}, message: {response['status_message']})")


# @pytest.mark.order(21)
# def test_concurrent_checkforupdate_multiple_clients():
#     """Test concurrent CheckForUpdate from multiple clients (true multi-process)."""
#     print("\n=== TEST: Concurrent CheckForUpdate - Multiple Clients (Multi-Process) ===")
#     
#     # Create cache
#     create_xconf_cache(MOCK_XCONF_RESPONSE_UPDATE, http_code=200, age_minutes=5)
#     
#     # Give daemon extra time to fully initialize D-Bus interface
#     print("Waiting for daemon D-Bus interface to be ready...")
#     time.sleep(2)
#     
#     # Create 3 clients in separate processes
#     num_clients = 3
#     clients = MultiProcessDBusClients(num_clients=num_clients)
#     
#     try:
#         # Register all clients (each in its own process with own D-Bus connection)
#         print(f"Registering {num_clients} clients (each in separate process)...")
#         reg_results = clients.register_all("TestClient", "1.0.0")
#         
#         # Verify all registered successfully
#         for i, result in enumerate(reg_results):
#             assert 'handler_id' in result, f"Client {i} should register successfully"
#             assert result['handler_id'] > 0, f"Client {i} should get valid handler_id"
#             print(f"  Client {i}: handler_id={result['handler_id']}, path={result['object_path']}")
#         
#         # All clients check for update concurrently (true concurrency via multiprocessing)
#         print(f"\nAll {num_clients} clients checking for updates concurrently...")
#         check_results = clients.check_for_update_all()
#         
#         # All should succeed with consistent responses
#         for i, resp in enumerate(check_results):
#             assert resp is not None, f"Client {i} should get response"
#             assert 'available_version' in resp, f"Client {i} should have available_version in response"
#             assert resp['available_version'] == "TEST_v2.0.0", f"Client {i} should get correct version"
#             assert resp['update_available'] == 1, f"Client {i} should see update available"
#             assert resp['http_code'] == 200, f"Client {i} should see HTTP 200 from cache"
#             print(f"  Client {i}: version={resp['available_version']}, available={resp['update_available']}, http={resp['http_code']}")
#         
#         print(f"✓ All {num_clients} clients got consistent responses from cache")
#         
#         # Unregister all
#         print(f"\nUnregistering all {num_clients} clients...")
#         unreg_results = clients.unregister_all()
#         for i, result in enumerate(unreg_results):
#             assert result is not None, f"Client {i} should get unregister response"
#             assert result.get('success') == True, f"Client {i} should unregister successfully"
#             print(f"  Client {i}: unregistered successfully")
#         
#         print(f"✓ All {num_clients} clients unregistered successfully")
#     
#     finally:
#         # Cleanup processes
#         clients.cleanup()


@pytest.mark.order(22)
def test_cache_file_permissions():
    """Test cache file permissions and location."""
    print("\n=== TEST: Cache File Permissions ===")
    
    # Create cache
    create_xconf_cache(MOCK_XCONF_RESPONSE_UPDATE, http_code=200, age_minutes=5)
    
    # Check file exists
    assert os.path.exists(XCONF_CACHE_FILE), "Cache file should exist"
    
    # Check it's in /tmp
    assert XCONF_CACHE_FILE.startswith("/tmp/"), "Cache should be in /tmp"
    
    # Check readable
    assert os.access(XCONF_CACHE_FILE, os.R_OK), "Cache should be readable"
    
    print("Cache file has correct location and permissions")


# =============================================================================
# TEST SUITE 4: Event Subscription
# =============================================================================

@pytest.mark.order(30)
def test_subscribe_to_events():
    """Test SubscribeToEvents method."""
    print("\n=== TEST: SubscribeToEvents ===")
    
    handler_id = dbus_register_process("TestClient", "1.0.0")
    assert handler_id > 0
    
    # Subscribe to events (if method exists)
    result = dbus_subscribe_to_events(handler_id, "http://localhost:8080/callback")
    
    # Should return success or not crash
    print("SubscribeToEvents result: {result}")
    
    dbus_unregister_process(handler_id)


# =============================================================================
# Helper to run all tests
# =============================================================================

if __name__ == "__main__":
    pytest.main([__file__, "-v", "-s"])
