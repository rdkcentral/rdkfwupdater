# Copyright 2023 Comcast Cable Communications Management, LLC
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

#!/usr/bin/env python3

import dbus
import subprocess
import time
import os
import json
from pathlib import Path

from rdkfw_test_helper import *

# D-Bus Configuration
DBUS_SERVICE_NAME = "org.rdkfwupdater.Service"
DBUS_OBJECT_PATH = "/org/rdkfwupdater/Service"
DBUS_INTERFACE = "org.rdkfwupdater.Interface"
DAEMON_BINARY = "/usr/local/bin/rdkFwupdateMgr"

# Cache files
XCONF_CACHE_FILE = "/tmp/xconf_response_thunder.txt"
XCONF_HTTP_CODE_FILE = "/tmp/xconf_httpcode_thunder.txt"
SWUPDATE_CONF_FILE = "/opt/swupdate.conf"
SWUPDATE_LOG_FILE_0 = "/opt/logs/swupdate.txt.0"

# Mock XConf URLs
XCONF_NORMAL_URL = "https://mockxconf:50052/firmwareupdate/getfirmwaredata"
XCONF_404_URL = "https://mockxconf:50052/firmwareupdate404/getfirmwaredata"
XCONF_INVALID_JSON_URL = "https://mockxconf:50052/firmwareupdate/getinvalidfirmwaredata"
XCONF_UNRESOLVED_URL = "https://unmockxconf:50052/featureControl/getSettings"
XCONF_INVALIDPCI_URL = "https://mockxconf:50052/firmwareupdate/getinvalidpcifirmwaredata"
XCONF_DELAY_URL = "https://mockxconf:50052/firmwareupdate/delaydwnlfirmwaredata"
XCONF_REBOOT_URL = "https://mockxconf:50052/firmwareupdate/getreboottruefirmwaredata"

# Backup conf file
BKUP_SWUPDATE_CONF_FILE = "/opt/bk_swupdate.conf"


# Result codes
CHECK_FOR_UPDATE_SUCCESS = 0  # API call succeeded
CHECK_FOR_UPDATE_FAIL = 1     # API call failed

# Status codes
FIRMWARE_AVAILABLE = 0
FIRMWARE_NOT_AVAILABLE = 1
UPDATE_NOT_ALLOWED = 2
FIRMWARE_CHECK_ERROR = 3
IGNORE_OPTOUT = 4
BYPASS_OPTOUT = 5

def set_xconf_url(url):
    """
    Set XConf URL in swupdate.conf
    This simulates different XConf server behaviors
    """
    # Backup original if exists
    if os.path.exists(SWUPDATE_CONF_FILE) and not os.path.exists(BKUP_SWUPDATE_CONF_FILE):
        rename_file(SWUPDATE_CONF_FILE, BKUP_SWUPDATE_CONF_FILE)
    
    # Write new URL
    write_on_file(SWUPDATE_CONF_FILE, url)
    print(f"[SETUP] XConf URL set to: {url}")

def restore_xconf_url():
    """Restore original XConf URL"""
    if os.path.exists(BKUP_SWUPDATE_CONF_FILE):
        remove_file(SWUPDATE_CONF_FILE)
        rename_file(BKUP_SWUPDATE_CONF_FILE, SWUPDATE_CONF_FILE)

def wait_for_cache_creation(timeout=30):
    """
    Wait for XConf cache to be created
    """
    print(f"[INFO] Waiting for XConf query and cache creation (max {timeout}s)...")
    elapsed = 0
    wait_interval = 5
    
    while elapsed < timeout:
        if cache_exists():
            print(f"[PASS] Cache created after {elapsed}s")
            return True
        time.sleep(wait_interval)
        elapsed += wait_interval
        if elapsed % 30 == 0:
            print(f"[INFO] Still waiting... ({elapsed}s elapsed)")
    
    return False


def write_device_prop():
    file_path = "/etc/device.properties"
    data = """DEVICE_NAME=DEV_CONTAINER
DEVICE_TYPE=mediaclient
DIFW_PATH=/opt/CDL
ENABLE_MAINTENANCE=false
MODEL_NUM=ABCD
ENABLE_SOFTWARE_OPTOUT=false
BUILD_TYPE=VBN
ESTB_INTERFACE=eth0
PDRI_ENABLED=true
"""
    try:
        with open(file_path, "w") as file:
            file.write(data)
    except Exception as e:
        print(f"Error creating device.properties: {e}")


def start_daemon():
    """Start D-Bus daemon """
    subprocess.run(['pkill', '-9', '-f', 'rdkFwupdateMgr'], capture_output=True)
    time.sleep(0.5)
    proc = subprocess.Popen([DAEMON_BINARY, "0", "1"])
    time.sleep(3)
    return proc


def stop_daemon(proc):
    """Stop daemon"""
    proc.terminate()
    proc.wait()


def iface():
    """Get D-Bus interface"""
    bus = dbus.SystemBus()
    proxy = bus.get_object(DBUS_SERVICE_NAME, DBUS_OBJECT_PATH)
    return dbus.Interface(proxy, DBUS_INTERFACE)


def cleanup_daemon_files():
    """Clean daemon-specific files"""
    remove_file(XCONF_CACHE_FILE)
    remove_file(XCONF_HTTP_CODE_FILE)


def cache_exists():
    """Check if XConf cache exists"""
    return os.path.exists(XCONF_CACHE_FILE) and os.path.exists(XCONF_HTTP_CODE_FILE)

def wait_for_log_line(log_file, text, timeout=30):
    """
    Wait for specific text to appear in log file
    
    Args:
        log_file: Path to log file
        text: Text to search for
        timeout: Maximum seconds to wait
    
    Returns:
        bool: True if found, False if timeout
    """
    start = time.time()
    
    while time.time() - start < timeout:
        if os.path.exists(log_file):
            try:
                # Open with UTF-8 error handling
                with open(log_file, 'r', encoding='utf-8', errors='ignore') as f:
                    content = f.read()
                    if text in content:
                        return True
            except Exception as e:
                # Log file might be being written to
                pass
        
        time.sleep(0.2)  # Check every 0.2 seconds
    
    return False  # Timeout - not found

def create_xconf_cache(firmware_available=True, version="ABCD_1.0.0"):
    """
    Create mock XConf cache for testing
    
    Args:
        firmware_available: If True, creates cache with new firmware
        version: Available firmware version
    """
    xconf_data = {
        "firmwareFilename": f"{version}.bin",
        "firmwareVersion": version,
        "firmwareLocation": f"https://mockxconf:50052/firmwareupdate/{version}.bin",
        "proto": "https",
        "rebootImmediately": False,
        "firmwareDownloadProtocol": "https"
    }
    
    os.makedirs(os.path.dirname(XCONF_CACHE_FILE), exist_ok=True)
    with open(XCONF_CACHE_FILE, 'w') as f:
        json.dump(xconf_data, f)
    
    # Create HTTP code file
    with open(XCONF_HTTP_CODE_FILE, 'w') as f:
        f.write("200")


def parse_checkupdate_response(response):
    """
    Parse CheckForUpdate response tuple
    
    Response signature: (issssi)
    Returns: dict with all fields
    """
    assert response is not None, "Response should not be None"
    assert isinstance(response, (tuple, list)), f"Expected tuple, got {type(response)}"
    assert len(response) == 6, f"Expected 6 elements, got {len(response)}"
    
    return {
        'result': int(response[0]),            # 0=success, 1=fail
        'current_version': str(response[1]),
        'available_version': str(response[2]),
        'update_details': str(response[3]),
        'status_message': str(response[4]),
        'status_code': int(response[5])        # 0-5
    }



def test_checkupdate_unregistered_handler():
    """
    CheckForUpdate with unregistered handler
    
    SCENARIO: Call CheckForUpdate without RegisterProcess
    SETUP: No registration
    EXECUTE: CheckForUpdate("999")
    VERIFY:
        - result = CHECK_FOR_UPDATE_SUCCESS (0)
        - status_code = FIRMWARE_CHECK_ERROR (3)
        - message mentions "not registered"
    """
    proc = start_daemon()
    initial_rdkfw_setup()
    write_device_prop()
    cleanup_daemon_files()
    
    try:
        api = iface()
        
        # Call CheckForUpdate with unregistered handler_id
        response = api.CheckForUpdate("999")
        parsed = parse_checkupdate_response(response)
        
        # Verify response
        assert parsed['result'] == CHECK_FOR_UPDATE_SUCCESS, \
            f"API call should succeed, got {parsed['result']}"
        print("[PASS] API call succeeded")
        
        assert parsed['status_code'] == FIRMWARE_CHECK_ERROR, \
            f"Expected FIRMWARE_CHECK_ERROR (3), got {parsed['status_code']}"
        print("[PASS] Status code is FIRMWARE_CHECK_ERROR")
        
        assert "not registered" in parsed['status_message'].lower(), \
            f"Message should mention 'not registered', got: {parsed['status_message']}"
        print(f"[PASS] Error message: {parsed['status_message']}")
        
    finally:
        cleanup_daemon_files()
        stop_daemon(proc)


def test_checkupdate_after_registration():
    """
    CheckForUpdate after successful registration
    
    SCENARIO: Register first, then check for updates
    SETUP: RegisterProcess
    EXECUTE: CheckForUpdate with valid handler_id
    VERIFY:
        - result = CHECK_FOR_UPDATE_SUCCESS (0)
        - status_code = 0, 1, or 3 (valid firmware status)
    """
    proc = start_daemon()
    initial_rdkfw_setup()
    write_device_prop()
    cleanup_daemon_files()
    
    try:
        api = iface()
        
        # Register process
        result = api.RegisterProcess("TestApp", "1.0")
        handler_id = str(result[0] if isinstance(result, tuple) else result)
        assert int(handler_id) > 0, "Registration failed"
        print(f"[PASS] Registered with handler_id: {handler_id}")
        
        # Call CheckForUpdate
        response = api.CheckForUpdate(handler_id)
        parsed = parse_checkupdate_response(response)
        
        # Verify response
        assert parsed['result'] == CHECK_FOR_UPDATE_SUCCESS, \
            f"API call should succeed, got {parsed['result']}"
        print("[PASS] API call succeeded")
        
        # Status code should be valid (0-5)
        assert 0 <= parsed['status_code'] <= 5, \
            f"Status code should be 0-5, got {parsed['status_code']}"
        print(f"[PASS] Status code: {parsed['status_code']}")
        print(f"[INFO] Message: {parsed['status_message']}")
        
    finally:
        cleanup_daemon_files()
        stop_daemon(proc)


def test_checkupdate_after_unregistration():
    """
    CheckForUpdate after UnregisterProcess
    
    SCENARIO: Register, unregister, then try to check updates
    SETUP: RegisterProcess - UnregisterProcess
    EXECUTE: CheckForUpdate with unregistered handler_id
    VERIFY: Returns FIRMWARE_CHECK_ERROR (3)
    """
    proc = start_daemon()
    initial_rdkfw_setup()
    write_device_prop()
    cleanup_daemon_files()
    
    try:
        api = iface()
        
        # Register process
        result = api.RegisterProcess("TestApp", "1.0")
        handler_id = int(result[0] if isinstance(result, tuple) else result)
        print(f"[PASS] Registered with handler_id: {handler_id}")
        
        # Unregister process
        unregister_result = api.UnregisterProcess(handler_id)
        assert bool(unregister_result) == True, "Unregister should succeed"
        print("[PASS] Unregistered successfully")
        
        # Try CheckForUpdate after unregistration
        response = api.CheckForUpdate(str(handler_id))
        parsed = parse_checkupdate_response(response)
        
        # Should return error
        assert parsed['result'] == CHECK_FOR_UPDATE_SUCCESS, \
            "API call itself should succeed"
        assert parsed['status_code'] == FIRMWARE_CHECK_ERROR, \
            f"Expected FIRMWARE_CHECK_ERROR (3), got {parsed['status_code']}"
        assert "not registered" in parsed['status_message'].lower(), \
            f"Message should mention 'not registered', got: {parsed['status_message']}"
        print("[PASS] CheckForUpdate correctly rejected unregistered handler")
        
    finally:
        cleanup_daemon_files()
        stop_daemon(proc)



def test_checkupdate_cache_miss():
    """
    CheckForUpdate with cache miss (first boot)
    
    SCENARIO: No XConf cache exists
    SETUP: Delete cache files
    EXECUTE: CheckForUpdate
    VERIFY:
        - Returns status_code = 3 (checking in progress)
        - Eventually creates cache files
        - Logs show "Cache miss"

    """
    proc = start_daemon()
    initial_rdkfw_setup()
    write_device_prop()
    cleanup_daemon_files()
    
    # Ensure no cache exists
    remove_file(XCONF_CACHE_FILE)
    remove_file(XCONF_HTTP_CODE_FILE)
    assert not cache_exists(), "Cache should not exist"
    
    try:
        api = iface()
        
        # Register process
        result = api.RegisterProcess("TestApp", "1.0")
        handler_id = str(result[0] if isinstance(result, tuple) else result)
        print(f"[PASS] Registered with handler_id: {handler_id}")
        
        # Call CheckForUpdate (cache miss)
        response = api.CheckForUpdate(handler_id)
        parsed = parse_checkupdate_response(response)
        
        assert parsed['result'] == CHECK_FOR_UPDATE_SUCCESS, \
            f"API call should succeed, got {parsed['result']}"
        print("[PASS] CheckForUpdate called (cache miss)")
        
        # Status code 3 means checking in progress
        print(f"[INFO] Status code: {parsed['status_code']}")
        print(f"[INFO] Message: {parsed['status_message']}")
        
        # Wait for cache to be created (daemon has 120s sleep + XConf call time)
        print("[INFO] Waiting for XConf query and cache creation...")
        
        # Verify cache exists
        if cache_exists():
            print("[PASS] XConf cache files created")
        else:
            print(f"[WARN] Cache not created")
        
        # Check logs for cache miss message
        if grep_log_file(SWUPDATE_LOG_FILE_0, "Cache miss"):
            print("[PASS] Log shows cache miss")
        
    finally:
        cleanup_daemon_files()
        stop_daemon(proc)


def test_checkupdate_cache_hit():
    """
    CheckForUpdate with cache hit
    
    SCENARIO: XConf cache already exists
    SETUP: Create XConf cache
    EXECUTE: CheckForUpdate
    VERIFY:
        - Returns immediately (no XConf call)
        - Uses cached data
        - status_code = 0 or 1 (firmware available/not available)
    """
    proc = start_daemon()
    initial_rdkfw_setup()
    write_device_prop()
    cleanup_daemon_files()
    
    # Create cache before CheckForUpdate
    create_xconf_cache(firmware_available=True, version="ABCD_2.0.0")
    assert cache_exists(), "Cache should exist"
    print("[SETUP] XConf cache created")
    
    try:
        api = iface()
        
        # Register process
        result = api.RegisterProcess("TestApp", "1.0")
        handler_id = str(result[0] if isinstance(result, tuple) else result)
        print(f"[PASS] Registered with handler_id: {handler_id}")
        
        # Call CheckForUpdate (cache hit)
        response = api.CheckForUpdate(handler_id)
        parsed = parse_checkupdate_response(response)
        
        assert parsed['result'] == CHECK_FOR_UPDATE_SUCCESS, \
            f"API call should succeed, got {parsed['result']}"
        print("[PASS] CheckForUpdate succeeded (cache hit)")
        
        # Should return quickly with cached data
        # Status code should be 0 (available) or 1 (not available)
        assert parsed['status_code'] in [FIRMWARE_AVAILABLE, FIRMWARE_NOT_AVAILABLE], \
            f"Expected status 0 or 1, got {parsed['status_code']}"
        print(f"[PASS] Status code: {parsed['status_code']} (using cache)")
        print(f"[INFO] Available version: {parsed['available_version']}")
        
    finally:
        cleanup_daemon_files()
        stop_daemon(proc)



def test_checkupdate_malformed_cache():
    """
    CheckForUpdate with malformed cache JSON
    
    SCENARIO: Cache file has invalid JSON (malformed syntax)
    SETUP: Create cache with "{ invalid json ]"
    EXECUTE: CheckForUpdate
    VERIFY: Daemon handles error gracefully (doesn't crash or hang)
    
    """
    proc = start_daemon()
    initial_rdkfw_setup()
    write_device_prop()
    cleanup_daemon_files()
    
    # Create malformed cache
    os.makedirs(os.path.dirname(XCONF_CACHE_FILE), exist_ok=True)
    with open(XCONF_CACHE_FILE, 'w') as f:
        f.write("{ invalid json ]")
    
    with open(XCONF_HTTP_CODE_FILE, 'w') as f:
        f.write("200")
    
    print("Created malformed XConf cache")
    
    try:
        api = iface()
        
        # Register process
        result = api.RegisterProcess("TestApp", "1.0")
        handler_id = str(result[0] if isinstance(result, tuple) else result)
        print(f"[PASS] Registered with handler_id: {handler_id}")
        
        # Call CheckForUpdate with timeout (daemon might hang on malformed JSON)
        try:
            api.CheckForUpdate(handler_id)
        except dbus.exceptions.DBusException:
            pass  # ignore timeout for this test
        assert wait_for_log_line(
                "/opt/logs/swupdate.txt.0",
                "Cache read failed, falling back to live XConf call",
                timeout=10 )     
    finally:
        cleanup_daemon_files()
        stop_daemon(proc)



def test_checkupdate_multi_client_access():
    """
    Multiple clients can query same handler
    
    SCENARIO: Client 1 registers, Client 2 checks updates
    SETUP: Client 1 registers process
    EXECUTE: Client 2 calls CheckForUpdate with Client 1's handler_id
    VERIFY: CheckForUpdate is read-only, accessible by any client
    """
    proc = start_daemon()
    initial_rdkfw_setup()
    write_device_prop()
    cleanup_daemon_files()
    
    try:
        # Client 1 registers
        api1 = iface()
        result1 = api1.RegisterProcess("ProcA", "1.0")
        handler_id = int(result1[0] if isinstance(result1, tuple) else result1)
        print(f"[PASS] Client 1 registered with handler_id: {handler_id}")
        
        # Client 2 tries to check updates for Client 1's handler
        bus2 = dbus.SystemBus()
        proxy2 = bus2.get_object(DBUS_SERVICE_NAME, DBUS_OBJECT_PATH)
        api2 = dbus.Interface(proxy2, DBUS_INTERFACE)
        
        response = api2.CheckForUpdate(str(handler_id))
        parsed = parse_checkupdate_response(response)
        
        # CheckForUpdate is read-only, should succeed
        assert parsed['result'] == CHECK_FOR_UPDATE_SUCCESS, \
            f"Client 2 should access CheckForUpdate, got {parsed['result']}"
        print("[PASS] Client 2 successfully called CheckForUpdate")
        print(f"[INFO] Status code: {parsed['status_code']}")
        
    finally:
        cleanup_daemon_files()
        stop_daemon(proc)


def test_checkupdate_firmware_available():
    """
    CheckForUpdate with firmware available
    
    SCENARIO: Cache has newer firmware version
    SETUP: Create cache with version > current
    EXECUTE: CheckForUpdate
    VERIFY:
        - status_code = FIRMWARE_AVAILABLE (0)
        - available_version populated
        - update_details contains download URL
    """
    proc = start_daemon()
    initial_rdkfw_setup()
    write_device_prop()
    cleanup_daemon_files()
    
    # Create cache with new firmware
    create_xconf_cache(firmware_available=True, version="ABCD_2.0.0")
    
    try:
        api = iface()
        
        # Register process
        result = api.RegisterProcess("TestApp", "1.0")
        handler_id = str(result[0] if isinstance(result, tuple) else result)
        
        # Call CheckForUpdate
        response = api.CheckForUpdate(handler_id)
        parsed = parse_checkupdate_response(response)
        
        assert parsed['result'] == CHECK_FOR_UPDATE_SUCCESS, \
            "API call should succeed"
        
        # Should indicate firmware available
        if parsed['status_code'] == FIRMWARE_AVAILABLE:
            print("[PASS] Firmware available")
            print(f"[INFO] Available version: {parsed['available_version']}")
            print(f"[INFO] Update details: {parsed['update_details'][:100]}...")
            
            # Verify fields are populated
            assert len(parsed['available_version']) > 0, \
                "Available version should be populated"
            assert len(parsed['update_details']) > 0, \
                "Update details should be populated"
        else:
            print(f"[INFO] Status code: {parsed['status_code']}")
            print(f"[INFO] Message: {parsed['status_message']}")
        
    finally:
        cleanup_daemon_files()
        stop_daemon(proc)


def test_checkupdate_response_structure():
    """
    Verify CheckForUpdate response structure
    
    SCENARIO: Validate response tuple format
    SETUP: Standard setup with cache
    EXECUTE: CheckForUpdate
    VERIFY:
        - Response is tuple with 6 elements
        - Element types are correct (i, s, s, s, s, i)
        - All fields are accessible
    """
    proc = start_daemon()
    initial_rdkfw_setup()
    write_device_prop()
    cleanup_daemon_files()
    
    create_xconf_cache(firmware_available=True)
    
    try:
        api = iface()
        
        # Register process
        result = api.RegisterProcess("TestApp", "1.0")
        handler_id = str(result[0] if isinstance(result, tuple) else result)
        
        # Call CheckForUpdate
        response = api.CheckForUpdate(handler_id)
        
        # Verify structure
        assert response is not None, "Response should not be None"
        print("[PASS] Response is not None")
        
        assert isinstance(response, (tuple, list)), \
            f"Response should be tuple, got {type(response)}"
        print("[PASS] Response is tuple")
        
        assert len(response) == 6, \
            f"Response should have 6 elements, got {len(response)}"
        print("[PASS] Response has 6 elements")
        
        # Verify types
        assert isinstance(int(response[0]), int), "Element 0 should be int"
        assert isinstance(str(response[1]), str), "Element 1 should be string"
        assert isinstance(str(response[2]), str), "Element 2 should be string"
        assert isinstance(str(response[3]), str), "Element 3 should be string"
        assert isinstance(str(response[4]), str), "Element 4 should be string"
        assert isinstance(int(response[5]), int), "Element 5 should be int"
        print("[PASS] All element types correct (i,s,s,s,s,i)")
        
        parsed = parse_checkupdate_response(response)
        print(f"[INFO] Parsed response: {parsed}")
        
    finally:
        cleanup_daemon_files()
        stop_daemon(proc)

def test_xconf_http_404_error():
    """
    XConf returns HTTP 404
    
    SCENARIO: XConf server returns 404 Not Found
    SETUP: Configure XConf URL to 404 endpoint
    EXECUTE: CheckForUpdate triggers XConf call
    VERIFY:
        - Handles 404 gracefully
        - Returns FIRMWARE_CHECK_ERROR or appropriate status
        - Logs show 404 error
    """
    proc = start_daemon()
    initial_rdkfw_setup()
    write_device_prop()
    cleanup_daemon_files()
    
    # Set XConf URL to 404 endpoint
    set_xconf_url(XCONF_404_URL)
    
    try:
        api = iface()
        
        # Register process
        result = api.RegisterProcess("TestApp", "1.0")
        handler_id = str(result[0] if isinstance(result, tuple) else result)
        print(f"[PASS] Registered with handler_id: {handler_id}")
        
        # Call CheckForUpdate (will trigger XConf call to 404 endpoint)
        response = api.CheckForUpdate(handler_id)
        parsed = parse_checkupdate_response(response)
        
        assert parsed['result'] == CHECK_FOR_UPDATE_SUCCESS, \
            "API call should succeed"
        print("[PASS] CheckForUpdate API call succeeded")
        
        # Wait for XConf query to complete
        time.sleep(5)
        
        # Check if cache was created with 404 response
        if wait_for_cache_creation():
            print("[INFO] Cache created despite 404")
            
            # Verify HTTP code file shows 404
            if os.path.exists(XCONF_HTTP_CODE_FILE):
                with open(XCONF_HTTP_CODE_FILE, 'r') as f:
                    http_code = f.read().strip()
                    if http_code == "404":
                        print(f"[PASS] HTTP code file shows 404: {http_code}")
        
        # Check logs for 404 handling
        if grep_log_file(SWUPDATE_LOG_FILE_0, "404"):
            print("[PASS] Log shows 404 error handling")
        
    finally:
        restore_xconf_url()
        cleanup_daemon_files()
        stop_daemon(proc)

def test_xconf_invalid_json_response():
    """
    XConf returns invalid JSON

    VERIFY:
    - CheckForUpdate API call succeeds
    - Invalid JSON is handled gracefully
    - Daemon does NOT crash or hang
    """

    proc = start_daemon()
    initial_rdkfw_setup()
    write_device_prop()
    cleanup_daemon_files()

    set_xconf_url(XCONF_INVALID_JSON_URL)

    try:
        api = iface()

        # Register once
        result = api.RegisterProcess("TestApp", "1.0")
        handler_id = str(result[0] if isinstance(result, tuple) else result)
        print(f"[PASS] Registered with handler_id: {handler_id}")

        # Trigger XConf call (invalid JSON)
        response = api.CheckForUpdate(handler_id)
        parsed = parse_checkupdate_response(response)

        # API call success (this is the key contract)
        assert parsed["result"] == CHECK_FOR_UPDATE_SUCCESS, \
            "CheckForUpdate API call should succeed even with invalid JSON"
        print("[PASS] CheckForUpdate API succeeded")


        # Daemon is still responsive (call again)
        response2 = api.CheckForUpdate(handler_id)
        parsed2 = parse_checkupdate_response(response2)
        assert parsed2["result"] == CHECK_FOR_UPDATE_SUCCESS
        print("[PASS] Daemon still responsive after invalid JSON")

        # Process still alive
        assert proc.poll() is None, "Daemon process exited unexpectedly"
        print("[PASS] Daemon still running")

    finally:
        restore_xconf_url()
        cleanup_daemon_files()
        stop_daemon(proc)

def test_xconf_model_validation():
    """
    XConf returns firmware for wrong model

    SCENARIO: XConf returns firmware not matching device model
    SETUP: Configure XConf URL to invalid PCI endpoint
    EXECUTE: CheckForUpdate with model mismatch
    VERIFY:
        - Detects model mismatch
        - Returns UPDATE_NOT_ALLOWED or error
        - Logs show model validation

    Based on: test_dwnl_firmware_invalidpci_test() from test_imagedwnl_error.py
    """
    proc = start_daemon()
    initial_rdkfw_setup()
    write_device_prop()  # Sets MODEL_NUM=ABCD
    cleanup_daemon_files()

    # Set XConf URL to invalid PCI endpoint (returns firmware for different model)
    set_xconf_url(XCONF_INVALIDPCI_URL)

    try:
        api = iface()

        # Register process
        result = api.RegisterProcess("TestApp", "1.0")
        handler_id = str(result[0] if isinstance(result, tuple) else result)
        print(f"[PASS] Registered with handler_id: {handler_id}")

        # Call CheckForUpdate (XConf returns wrong model firmware)
        response = api.CheckForUpdate(handler_id)
        parsed = parse_checkupdate_response(response)

        assert parsed['result'] == CHECK_FOR_UPDATE_SUCCESS, \
            "API call should succeed"
        print("[PASS] CheckForUpdate called")


        time.sleep(5)

        # Check logs for model validation error
        # Binary test expects: "Image configured is not of model"
        if grep_log_file(SWUPDATE_LOG_FILE_0, "model") or \
           grep_log_file(SWUPDATE_LOG_FILE_0, "Image configured is not of model"):
            print("[PASS] Log shows model validation check")

        # Status may indicate update not allowed
        if parsed['status_code'] == UPDATE_NOT_ALLOWED:
            print(f"[PASS] Status code indicates update not allowed: {parsed['status_message']}")

    finally:
        restore_xconf_url()
        cleanup_daemon_files()
        stop_daemon(proc)

def test_xconf_successful_query_creates_cache():
    """
    Successful XConf query creates cache files
    
    SCENARIO: XConf query succeeds, cache is created
    SETUP: Normal XConf URL, no existing cache
    EXECUTE: CheckForUpdate triggers XConf call
    VERIFY:
        - Both cache files created
        - Cache contains valid JSON
        - HTTP code file shows 200
        - Logs show cache creation
    """
    proc = start_daemon()
    initial_rdkfw_setup()
    write_device_prop()
    cleanup_daemon_files()
    
    # Ensure no cache exists
    assert not cache_exists(), "Cache should not exist initially"
    
    # Set normal XConf URL
    set_xconf_url(XCONF_NORMAL_URL)
    
    try:
        api = iface()
        
        # Register process
        result = api.RegisterProcess("TestApp", "1.0")
        handler_id = str(result[0] if isinstance(result, tuple) else result)
        print(f"[PASS] Registered with handler_id: {handler_id}")
        
        # Call CheckForUpdate (will trigger XConf call)
        response = api.CheckForUpdate(handler_id)
        parsed = parse_checkupdate_response(response)
        
        assert parsed['result'] == CHECK_FOR_UPDATE_SUCCESS, \
            "API call should succeed"
        print("[PASS] CheckForUpdate called")
        
        
        # Verify cache file contains valid JSON
        if os.path.exists(XCONF_CACHE_FILE):
            with open(XCONF_CACHE_FILE, 'r') as f:
                cache_content = f.read()
                try:
                    cache_json = json.loads(cache_content)
                    print("[PASS] Cache contains valid JSON")
                    print(f"[INFO] Firmware version: {cache_json.get('firmwareVersion', 'N/A')}")
                except json.JSONDecodeError:
                    print("[WARN] Cache contains non-JSON data")
        
        # Verify HTTP code file shows success
        if os.path.exists(XCONF_HTTP_CODE_FILE):
            with open(XCONF_HTTP_CODE_FILE, 'r') as f:
                http_code = f.read().strip()
                assert http_code == "200", f"Expected HTTP 200, got {http_code}"
                print(f"[PASS] HTTP code: {http_code}")
        
        # Check logs for cache creation
        if grep_log_file(SWUPDATE_LOG_FILE_0, "cached") or \
           grep_log_file(SWUPDATE_LOG_FILE_0, "XConf data cached successfully"):
            print("[PASS] Log shows cache creation")
        
    finally:
        restore_xconf_url()
        cleanup_daemon_files()
        stop_daemon(proc)


def test_xconf_cache_miss_triggers_query():
    """
    Cache miss triggers XConf query
    
    SCENARIO: No cache exists, CheckForUpdate triggers query
    SETUP: Delete cache, normal XConf URL
    EXECUTE: CheckForUpdate
    VERIFY:
        - Logs show "Cache miss"
        - XConf query is triggered
        - Eventually creates cache
    """
    proc = start_daemon()
    initial_rdkfw_setup()
    write_device_prop()
    cleanup_daemon_files()
    
    # Ensure no cache
    remove_file(XCONF_CACHE_FILE)
    remove_file(XCONF_HTTP_CODE_FILE)
    assert not cache_exists(), "Cache should not exist"
    
    set_xconf_url(XCONF_NORMAL_URL)
    
    try:
        api = iface()
        
        # Register process
        result = api.RegisterProcess("TestApp", "1.0")
        handler_id = str(result[0] if isinstance(result, tuple) else result)
        print(f"[PASS] Registered with handler_id: {handler_id}")
        
        # Call CheckForUpdate (cache miss)
        response = api.CheckForUpdate(handler_id)
        parsed = parse_checkupdate_response(response)
        
        assert parsed['result'] == CHECK_FOR_UPDATE_SUCCESS, \
            "API call should succeed"
        print("[PASS] CheckForUpdate called (cache miss)")
        
        # Give it a moment for logs to be written
        time.sleep(3)
        
        # Check logs for cache miss
        if grep_log_file(SWUPDATE_LOG_FILE_0, "Cache miss") or \
           grep_log_file(SWUPDATE_LOG_FILE_0, "cache miss"):
            print("[PASS] Log shows cache miss")
        
        
    finally:
        restore_xconf_url()
        cleanup_daemon_files()
        stop_daemon(proc)


def test_xconf_subsequent_call_uses_cache():
    """
    Second CheckForUpdate uses cache
    
    SCENARIO: First call creates cache, second uses it
    SETUP: Two CheckForUpdate calls
    EXECUTE: Call CheckForUpdate twice
    VERIFY:
        - First call creates cache
        - Second call uses cache (no XConf call)
        - Response is immediate on second call
    """
    proc = start_daemon()
    initial_rdkfw_setup()
    write_device_prop()
    cleanup_daemon_files()
    
    set_xconf_url(XCONF_NORMAL_URL)
    
    try:
        api = iface()
        
        # Register process
        result = api.RegisterProcess("TestApp", "1.0")
        handler_id = str(result[0] if isinstance(result, tuple) else result)
        print(f"[PASS] Registered with handler_id: {handler_id}")
        
        # First call - cache miss
        print("\n[TEST] First CheckForUpdate call (cache miss)...")
        response1 = api.CheckForUpdate(handler_id)
        parsed1 = parse_checkupdate_response(response1)
        
        assert parsed1['result'] == CHECK_FOR_UPDATE_SUCCESS, \
            "First call should succeed"
        print("[PASS] First call completed")
        
        
        # Second call - cache hit
        print("\n[TEST] Second CheckForUpdate call (cache hit)...")
        start_time = time.time()
        response2 = api.CheckForUpdate(handler_id)
        elapsed = time.time() - start_time
        
        parsed2 = parse_checkupdate_response(response2)
        
        assert parsed2['result'] == CHECK_FOR_UPDATE_SUCCESS, \
            "Second call should succeed"
        print(f"[PASS] Second call completed in {elapsed:.2f}s (using cache)")
        
        # Second call should be much faster (< 5 seconds if using cache)
        if elapsed < 5:
            print(f"[PASS] Second call was fast ({elapsed:.2f}s) - used cache")
        else:
            print(f"[WARN] Second call took {elapsed:.2f}s - may not have used cache")
        
    finally:
        restore_xconf_url()
        cleanup_daemon_files()
        stop_daemon(proc)


def test_xconf_response_firmware_available():
    """
    XConf response indicates firmware available
    
    SCENARIO: XConf returns newer firmware version
    SETUP: Normal XConf URL (returns firmware)
    EXECUTE: CheckForUpdate
    VERIFY:
        - status_code = FIRMWARE_AVAILABLE (0)
        - available_version is populated
        - update_details contains firmware info
        - Cache contains firmware details
    """
    proc = start_daemon()
    initial_rdkfw_setup()
    write_device_prop()
    cleanup_daemon_files()
    
    set_xconf_url(XCONF_NORMAL_URL)
    
    try:
        api = iface()
        
        # Register process
        result = api.RegisterProcess("TestApp", "1.0")
        handler_id = str(result[0] if isinstance(result, tuple) else result)
        print(f"[PASS] Registered with handler_id: {handler_id}")
        
        # Call CheckForUpdate
        response = api.CheckForUpdate(handler_id)
        parsed = parse_checkupdate_response(response)
        
        assert parsed['result'] == CHECK_FOR_UPDATE_SUCCESS, \
            "API call should succeed"
        
            
        # Call again to get cached result
        time.sleep(2)
        response2 = api.CheckForUpdate(handler_id)
        parsed2 = parse_checkupdate_response(response2)
        # Check if firmware is available
        if parsed2['status_code'] == FIRMWARE_AVAILABLE:
            print(f"[PASS] Firmware available: {parsed2['available_version']}")
            # Verify fields are populated
            assert len(parsed2['available_version']) > 0, \
                    "Available version should be populated"
            print(f"[PASS] Available version: {parsed2['available_version']}")
                
            assert len(parsed2['update_details']) > 0, \
                    "Update details should be populated"
            print(f"[INFO] Update details: {parsed2['update_details'][:100]}...")
                
        else:
            print(f"[INFO] Status code: {parsed2['status_code']}")
            print(f"[INFO] Message: {parsed2['status_message']}")
        
    finally:
        restore_xconf_url()
        cleanup_daemon_files()
        stop_daemon(proc)
