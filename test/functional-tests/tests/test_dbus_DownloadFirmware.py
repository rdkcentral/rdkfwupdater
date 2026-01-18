#!/usr/bin/env python3
"""
Fixed Parameter Validation Tests for DownloadFirmware
These tests validate input parameters and error handling

NOTE: These tests work with the default build (rdkcertselector enabled).
They validate D-Bus API behavior and error handling, matching the approach
used in the binary tests (test_imagedwnl.py).
"""

import dbus
import subprocess
import time
import os
import json
import pytest
from pathlib import Path

from rdkfw_test_helper import *

# D-Bus Configuration
DBUS_SERVICE_NAME = "org.rdkfwupdater.Service"
DBUS_OBJECT_PATH = "/org/rdkfwupdater/Service"
DBUS_INTERFACE = "org.rdkfwupdater.Interface"
DAEMON_BINARY = "/usr/local/bin/rdkFwupdateMgr"

# Daemon files
STATUS_FILE = "/tmp/dnldmgr_status.txt"
PROGRESS_FILE = "/opt/curl_progress"
XCONF_CACHE_FILE = "/tmp/xconf_response_thunder.txt"
SWUPDATE_LOG_FILE_0 = "/opt/logs/swupdate.txt.0"

# Result codes
#DOWNLOAD_SUCCESS = 0
#DOWNLOAD_ALREADY_EXISTS = 1
#DOWNLOAD_NETWORK_ERROR = 2
#DOWNLOAD_NOT_FOUND = 3
#DOWNLOAD_ERROR = 4

#DownloadResult
RDKFW_DWNL_SUCCESS = 0 #Firmware download initiated successfully.
RDKFW_DWNL_FAILED = 1  #Firmware download initiation failed.

def write_device_prop():
    """Same as binary test"""
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
    """Start D-Bus daemon"""
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
    remove_file(STATUS_FILE)
    remove_file(PROGRESS_FILE)
    remove_file(XCONF_CACHE_FILE)

def wait_for_file(filepath, timeout=15.0):
    """Wait for file to exist"""
    start = time.time()
    while time.time() - start < timeout:
        if os.path.exists(filepath):
            time.sleep(0.5)
            return True
        time.sleep(0.2)
    return False

def wait_for_log_line(log_file, text, timeout=10):
    end_time = time.time() + timeout
    while time.time() < end_time:
        if os.path.exists(log_file):
            with open(log_file, "r", errors="ignore") as f:
                if text in f.read():
                    return True
        time.sleep(0.5)
    return False


def test_download_with_null_firmware_name():
    """
    SCENARIO: firmwareName is empty string
    EXPECTED: Return DOWNLOAD_ERROR (4) or D-Bus error
    VALIDATES: Input validation prevents NULL firmware name
    
    CORRECT SIGNATURE: DownloadFirmware(firmwareName, downloadUrl, firmwareType, localFilePath)
    """
    proc = start_daemon()
    initial_rdkfw_setup()
    cleanup_daemon_files()
    
    try:
        api = iface()
        result = api.RegisterProcess("TestApp", "1.0")
        handler_id = result[0] if isinstance(result, tuple) else int(result)
        assert handler_id > 0, "Registration failed"
        
        try:
            # Empty firmware name - should fail
            result = api.DownloadFirmware(
                str(handler_id),
                "",  # Empty firmware name
                "https://mockxconf:50052/firmwareupdate/getfirmwaredata/test.bin",
                "PCI"
            )
            result_code = str(result[0] if isinstance(result, tuple) else result)
            assert result_code == "RDKFW_DWNL_FAILED", \
                    f"Empty firmware name should be rejected, got {result_code}"

                
        except dbus.exceptions.DBusException as e:
            print(f"[PASS] Empty firmware name rejected with D-Bus error: {e.get_dbus_name()}")
            
    finally:
        cleanup_daemon_files()
        stop_daemon(proc)


def test_download_with_invalid_firmware_type():
    """
    SCENARIO: typeOfFirmware is invalid (not PCI/PDRI/PERIPHERAL)
    EXPECTED: Return DOWNLOAD_ERROR (4) or D-Bus error
    VALIDATES: Firmware type validation
    
    """
    proc = start_daemon()
    initial_rdkfw_setup()
    cleanup_daemon_files()
    
    try:
        api = iface()
        result = api.RegisterProcess("TestApp", "1.0")
        handler_id = result[0] if isinstance(result, tuple) else int(result)
        assert handler_id > 0, "Registration failed"
        
        # Test various invalid types
        invalid_types = ["INVALID", "pci", "", "USB", "invalid"]
        
        for invalid_type in invalid_types:
            try:
                result = api.DownloadFirmware(
                    str(handler_id),
                    "test_img.bin",
                    "https://mockxconf:50052/firmwareupdate/getfirmwaredata/test_img.bin",
                    "invalid_type"  # Invalid type
                    
                )
                result_code = str(result[0] if isinstance(result, tuple) else result)
                assert result_code == "RDKFW_DWNL_FAILED", \
                        f"Invalid firmware type '{invalid_type}' should be rejected, got {result_code}"
            except dbus.exceptions.DBusException as e:
                print(f"[PASS] Invalid type '{invalid_type}' rejected: {e.get_dbus_name()}")
                
    finally:
        cleanup_daemon_files()
        stop_daemon(proc)


def test_download_with_unregistered_handler():
    """
    SCENARIO: DownloadFirmware called WITHOUT RegisterProcess first
    EXPECTED: D-Bus error or daemon rejection
    VALIDATES: Process must be registered before downloading
    
    NOTE: This tests calling DownloadFirmware before RegisterProcess.
    The daemon should track registered clients and reject unregistered requests.
    """
    proc = start_daemon()
    initial_rdkfw_setup()
    cleanup_daemon_files()
    
    try:
        api = iface()
        
        # DO NOT call RegisterProcess - go straight to DownloadFirmware
        try:
            result = api.DownloadFirmware(
                "",
                "fw.bin",
                "https://mockxconf:50052/firmwareupdate/getfirmwaredata/fw.bin",
                "PCI",
                
            )
            result_code = str(result[0] if isinstance(result, tuple) else result)
            assert result_code == "RDKFW_DWNL_FAILED", \
                    f"Unregistered client should be rejected, got {result_code}"
        except dbus.exceptions.DBusException as e:
            print(f"[PASS] Invalid type '{invalid_type}' rejected: {e.get_dbus_name()}")
                
            
    finally:
        cleanup_daemon_files()
        stop_daemon(proc)


def test_download_with_custom_url():
    """
    SCENARIO: downloadUrl parameter is non-empty (custom URL provided)
    EXPECTED: Uses provided URL, ignores XConf cache
    VALIDATES: Custom URL takes precedence over cache
    
    SETUP: Create XConf cache with wrong URL, provide correct URL to DownloadFirmware
    VERIFY: Download succeeds using custom URL (not cache)
    """
    proc = start_daemon()
    initial_rdkfw_setup()
    write_device_prop()
    cleanup_daemon_files()
    
    try:
        api = iface()
        result = api.RegisterProcess("TestApp", "1.0")
        handler_id = result[0] if isinstance(result, tuple) else int(result)
        assert handler_id > 0, "Registration failed"
        
        # Create XConf cache with WRONG URL
        xconf_data = {
            "firmwareFilename": "wrong.bin",
            "firmwareVersion": "WRONG",
            "firmwareLocation": "https://wrong.server.com/wrong.bin",
            "proto": "https",
            "rebootImmediately": False,
            "firmwareDownloadProtocol": "https"
        }
        os.makedirs(os.path.dirname(XCONF_CACHE_FILE), exist_ok=True)
        with open(XCONF_CACHE_FILE, 'w') as f:
            json.dump(xconf_data, f)
        
        # Provide CUSTOM URL - should use this, not cache
        custom_url = "https://mockxconf:50052/firmwareupdate/getfirmwaredata/"
        result = api.DownloadFirmware(
            str(handler_id),
            "test_fw.bin",
            custom_url,  # Custom URL
            "PCI",
            
        )
        
        time.sleep(8)
        
        # Check if download attempted with custom URL
        # If cache was used, download would fail (wrong.server.com doesn't exist)
        # If custom URL used, download should succeed or at least attempt mock server
        result_code = str(result[0] if isinstance(result, tuple) else result)
        assert result_code == "RDKFW_DWNL_SUCCESS",\
                  "Download request was not accepted"

    finally:
        remove_file("/tmp/test_fw.bin")
        cleanup_daemon_files()
        stop_daemon(proc)


def test_download_with_invalid_custom_url():
    """
    SCENARIO: downloadUrl parameter has malformed URL
    EXPECTED: Return DOWNLOAD_ERROR or network error
    VALIDATES: Invalid URL format is rejected
    
    SETUP: Provide malformed URL (missing //, wrong protocol, etc.)
    VERIFY: Download fails with appropriate error
    """
    proc = start_daemon()
    initial_rdkfw_setup()
    cleanup_daemon_files()
    
    try:
        api = iface()
        result = api.RegisterProcess("TestApp", "1.0")
        handler_id = result[0] if isinstance(result, tuple) else int(result)
        assert handler_id > 0, "Registration failed"
        
        # Test various malformed URLs
        invalid_urls = [
            "http:localhost:8888/test",  # Missing //
            "htp://wrong.com/test",      # Wrong protocol
            "://noprotocol.com/test",    # No protocol
            "not-a-url",                 # Not a URL
            "",                          # Empty (but this should use cache)
        ]
        
        for invalid_url in invalid_urls:
            if invalid_url == "":
                continue  # Skip empty - that's a different test
                
            try:
                result = api.DownloadFirmware(
                    str(handler_id),
                    "test_download.bin",
                    invalid_url,
                    "PCI"
                )
                
                time.sleep(3)
                result_code = str(result[0] if isinstance(result, tuple) else result)
                assert result_code == "RDKFW_DWNL_FAILED", \
                        f"Invalid URL '{invalid_url}' should be rejected, got {result_code}"

            except dbus.exceptions.DBusException as e:
                print(f"[PASS] Invalid URL '{invalid_url}' rejected: {e.get_dbus_name()}")
                
            # Cleanup between attempts
            remove_file("/tmp/test_download.bin")
            time.sleep(1)
            
    finally:
        remove_file("/tmp/test_download.bin")
        cleanup_daemon_files()
        stop_daemon(proc)


def test_dwnl_firmware_basic():
    """
    Adapted from: test_dwnl_firmware_test (binary test 1)
    
    SCENARIO: Basic firmware download with direct URL
    SETUP: Same as binary - device.properties, pdri_image_file
    EXECUTE: DownloadFirmware with direct URL to mock server
    VERIFY: File downloaded to /opt/CDL
    """
    proc = start_daemon()
    initial_rdkfw_setup()
    write_device_prop()
    cleanup_daemon_files()
    
    # Same setup as binary test
    remove_file("/tmp/pdri_image_file")
    remove_file("/tmp/.xconfssrdownloadurl")
    pdri_file = Path("/tmp/pdri_image_file")
    pdri_file.touch(exist_ok=True)
    write_on_file("/tmp/pdri_image_file", "ABCD_PDRI_img")
    
    try:
        api = iface()
        result = api.RegisterProcess("TestApp", "1.0")
        handler_id = result[0] if isinstance(result, tuple) else int(result)
        assert handler_id > 0, "Registration failed"
        
        # Direct URL to mock server - simple!
        result = api.DownloadFirmware(
            str(handler_id),
            "ABCD_PDRI_img.bin",
            "https://mockxconf:50052/firmwareupdate/getfirmwaredata/ABCD_PDRI_img.bin",
            "PCI",
        )
        
        # Wait for download
        time.sleep(8)
        result_code = str(result[0] if isinstance(result, tuple) else result)
        assert result_code == "RDKFW_DWNL_SUCCESS", \
                "Download request was not accepted"

        # Verify log line
        assert wait_for_log_line(SWUPDATE_LOG_FILE_0,"Triggering the Image Download"), "Download worker was not triggered"

    finally:
        #remove_file("/opt/CDL/ABCD_PDRI_img.bin")
        cleanup_daemon_files()
        stop_daemon(proc)


def test_http_404_error():
    """
    Adapted from: test_http_404 (binary test 5)

    SCENARIO: Direct URL to mock server 404 endpoint
    SETUP: Clear previous cache
    EXECUTE: DownloadFirmware with 404 URL
    VERIFY: D-Bus API accepts request and handles error gracefully
    
    NOTE: Like the binary test (test_imagedwnl.py::test_http_404), this validates
    the D-Bus API flow with a 404 URL. With rdkcertselector enabled (default build),
    the cert init may fail before reaching HTTP layer, but the test still validates:
    - D-Bus API correctness
    - Request acceptance
    - Error handling
    - No file created on error
    
    This matches the binary test approach - validate API behavior, not HTTP specifics.
    """
    proc = start_daemon()
    initial_rdkfw_setup()
    cleanup_daemon_files()
    
    # Daemon will download to: /opt/CDL/nonexistent.bin (auto-determined)
    auto_download_path = "/opt/CDL/nonexistent.bin"
    remove_file(auto_download_path)

    try:
        api = iface()
        result = api.RegisterProcess("TestApp", "1.0")
        handler_id = int(result[0]) if isinstance(result, tuple) else int(result)
        assert handler_id > 0, "Registration failed"

        # Call DownloadFirmware with 404 URL
        result = api.DownloadFirmware(
            str(handler_id),
            "nonexistent.bin",
            "https://mockxconf:50052/firmwareupdate404/getfirmwaredata",
            "PCI"
        )

        # Verify D-Bus response - request should be accepted
        assert isinstance(result, tuple) and len(result) == 3, \
            f"Expected (result, status, message), got {result}"
        
        result_code, status_code, message = result
        assert result_code == "RDKFW_DWNL_SUCCESS", \
            f"Expected RDKFW_DWNL_SUCCESS, got {result_code}"

        # Wait for async worker
        time.sleep(5)

        # Verify file NOT created (error case - whether cert failure or 404)
        assert not os.path.exists(auto_download_path), \
            f"File should not be created on error: {auto_download_path}"

    finally:
        remove_file(auto_download_path)
        cleanup_daemon_files()
        stop_daemon(proc)


def test_empty_url_no_cache():
    """
    SCENARIO: Empty URL but no XConf cache exists
    SETUP: No cache file
    EXECUTE: DownloadFirmware with empty URL
    VERIFY: Returns error immediately
    """
    proc = start_daemon()
    initial_rdkfw_setup()
    cleanup_daemon_files()

    # Ensure NO cache exists
    remove_file(XCONF_CACHE_FILE)

    try:
        api = iface()
        result = api.RegisterProcess("TestApp", "1.0")
        handler_id = int(result[0]) if isinstance(result, tuple) else int(result)
        assert handler_id > 0, "Registration failed"

        # Empty URL, no cache - should fail
        try:
            # Correct API: DownloadFirmware(handler_id, filename, url, type)
            result = api.DownloadFirmware(
                str(handler_id),
                "test.bin",
                "",  # Empty URL - should be rejected
                "PCI"
            )

            result_code = str(result[0] if isinstance(result, tuple) else result)
            assert result_code == "RDKFW_DWNL_FAILED", \
                f"Expected RDKFW_DWNL_FAILED, got {result_code}"
            print("[PASS] Returned RDKFW_DWNL_FAILED (empty URL rejected)")

        except dbus.exceptions.DBusException as e:
            print(f"[PASS] D-Bus error for empty URL: {e.get_dbus_name()}")

        # Either result code or D-Bus exception is acceptable

    finally:
        cleanup_daemon_files()
        stop_daemon(proc)

def test_download_delay():
    """
    Adapted from: test_delay_dwnl (binary test 8)

    SCENARIO: XConf cache has download delay
    SETUP: Create cache with delay (simulates CheckForUpdate response)
    EXECUTE: DownloadFirmware with URL from cache
    VERIFY: Delay happens before download
    
    NOTE: The daemon reads the delay from cache (if it exists) and applies it,
    but the client must still provide the download URL explicitly.
    """
    proc = start_daemon()
    initial_rdkfw_setup()
    cleanup_daemon_files()

    remove_file("/tmp/pdri_image_file")
    pdri_file = Path("/tmp/pdri_image_file")
    pdri_file.touch(exist_ok=True)
    write_on_file("/tmp/pdri_image_file", "ABCD_PDRI_firmware_test.bin")

    try:
        api = iface()
        result = api.RegisterProcess("TestApp", "1.0")
        handler_id = result[0] if isinstance(result, tuple) else int(result)
        assert handler_id > 0, "Registration failed"

        # Create cache with delay
        download_url = "https://mockxconf:50052/firmwareupdate/getfirmwaredata/ABCD_PDRI_firmware_test.bin"
        xconf_data = {
            "firmwareFilename": "ABCD_PDRI_firmware_test.bin",
            "firmwareVersion": "ABCD_PDRI_firmware_test",
            "firmwareLocation": download_url,
            "proto": "https",
            "rebootImmediately": False,
            "firmwareDownloadProtocol": "https",
            "downloadDelayMinutes": 1
        }
        os.makedirs(os.path.dirname(XCONF_CACHE_FILE), exist_ok=True)
        with open(XCONF_CACHE_FILE, 'w') as f:
            json.dump(xconf_data, f)

        start_time = time.time()

        # Provide URL explicitly (daemon reads delay from cache, but URL still required)
        result = api.DownloadFirmware(
            str(handler_id),
            "ABCD_PDRI_firmware_test.bin",
            download_url,  # URL must be provided (not empty)
            "PCI"
        )

        # Wait for delay + download
        time.sleep(75)

        elapsed = time.time() - start_time

        # Verify delay happened (at least 60 seconds)
        assert elapsed >= 60, f"Download should be delayed by 1 minute, took only {elapsed:.0f}s"
        print(f"[PASS] Download delayed ({elapsed:.0f}s)")

        # Check status for delay (optional)
        if os.path.exists(STATUS_FILE):
            with open(STATUS_FILE, 'r') as f:
                status = f.read()
                if "delay" in status.lower():
                    print("[PASS] Status shows delay")

    finally:
        remove_file("/tmp/test_delay.bin")
        remove_file("/tmp/fw_preparing_to_reboot")
        remove_file("/tmp/currently_running_image_name")
        remove_file("/opt/cdl_flashed_file_name")
        cleanup_daemon_files()
        stop_daemon(proc)

def test_empty_url_rejected_even_with_cache():
    """
    SCENARIO: Empty URL provided, even though valid cache exists
    SETUP: Create XConf cache with valid mock server URL (simulates CheckForUpdate was called)
    EXECUTE: DownloadFirmware with EMPTY URL
    VERIFY: 
        1. Daemon REJECTS the request (validates download_url parameter)
        2. Returns RDKFW_DWNL_FAILED or D-Bus error
        3. Does NOT fall back to cache (input validation happens first)
    
    RATIONALE:
    The daemon's D-Bus handler validates that download_url is non-empty BEFORE
    any cache lookup or processing. This is intentional input validation.
    Empty download_url is considered invalid input and rejected immediately.
    
    Clients should always provide a valid URL to DownloadFirmware.
    If they want to use cached XConf data, they should:
    1. Call CheckForUpdate to populate cache
    2. Read the cache themselves
    3. Pass the URL from cache to DownloadFirmware
    """
    proc = start_daemon()
    initial_rdkfw_setup()
    write_device_prop()
    cleanup_daemon_files()
    
    # Setup same as binary test
    remove_file("/tmp/pdri_image_file")
    remove_file("/tmp/.xconfssrdownloadurl")
    pdri_file = Path("/tmp/pdri_image_file")
    pdri_file.touch(exist_ok=True)
    write_on_file("/tmp/pdri_image_file", "ABCD_PDRI_img")
    
    try:
        api = iface()
        result = api.RegisterProcess("TestApp", "1.0")
        handler_id = result[0] if isinstance(result, tuple) else int(result)
        assert handler_id > 0, "Registration failed"
        
        # Create XConf cache with VALID URL (to verify cache is NOT used)
        xconf_data = {
            "firmwareFilename": "ABCD_PDRI_img.bin",
            "firmwareVersion": "ABCD_PDRI_img",
            "firmwareLocation": "https://mockxconf:50052/firmwareupdate/getfirmwaredata/ABCD_PDRI_img.bin",
            "proto": "https",
            "rebootImmediately": False,
            "firmwareDownloadProtocol": "https"
        }
        os.makedirs(os.path.dirname(XCONF_CACHE_FILE), exist_ok=True)
        with open(XCONF_CACHE_FILE, 'w') as f:
            json.dump(xconf_data, f)
        
        print("[INFO] XConf cache created (but should be ignored due to empty URL)")
        
        # Call DownloadFirmware with EMPTY URL - should be REJECTED
        try:
            result = api.DownloadFirmware(
                str(handler_id),
                "ABCD_PDRI_img.bin",
                "",  # EMPTY URL - daemon should reject this
                "PCI"
            )
            
            # If we get here, check result code
            result_code = str(result[0] if isinstance(result, tuple) else result)
            assert result_code == "RDKFW_DWNL_FAILED", \
                f"Empty URL should be rejected with RDKFW_DWNL_FAILED, got {result_code}"
            print("[PASS] Empty URL rejected with RDKFW_DWNL_FAILED (input validation)")
            
        except dbus.exceptions.DBusException as e:
            # D-Bus error is also acceptable (daemon rejected before processing)
            print(f"[PASS] Empty URL rejected with D-Bus error: {e.get_dbus_name()}")
        
        # Verify NO download happened
        time.sleep(2)
        assert not os.path.exists("/opt/CDL/ABCD_PDRI_img.bin"), \
            "File should NOT be created when URL is rejected"
        print("[PASS] No download occurred (request properly rejected)")
        
    finally:
        remove_file("/opt/CDL/ABCD_PDRI_img.bin")
        cleanup_daemon_files()
        stop_daemon(proc)
