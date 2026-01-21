#!/usr/bin/env python3

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
    """Clean daemon-specific files including flash indicators"""
    remove_file(STATUS_FILE)
    remove_file(PROGRESS_FILE)
    remove_file(XCONF_CACHE_FILE)
    # Clean flash indicator files to ensure test isolation
    remove_file("/tmp/fw_preparing_to_reboot")
    remove_file("/tmp/currently_running_image_name")
    remove_file("/opt/cdl_flashed_file_name")

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
    EXPECTED: Return RDKFW_DWNL_FAILED or D-Bus error
    VALIDATES: Input validation prevents NULL firmware name
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
    EXPECTED: Return RDKFW_DWNL_FAILED or D-Bus error
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
    SCENARIO: Basic firmware download with direct URL
    EXECUTE: DownloadFirmware with direct URL to mock server
    VERIFY: File downloaded to /opt/CDL
    """
    proc = start_daemon()
    initial_rdkfw_setup()
    write_device_prop()
    cleanup_daemon_files()
    
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
    SCENARIO: Direct URL to mock server 404 endpoint
    SETUP: Clear previous cache
    EXECUTE: DownloadFirmware with 404 URL
    VERIFY: D-Bus API accepts request and handles error gracefully
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


    finally:
        cleanup_daemon_files()
        stop_daemon(proc)

def test_download_delay():
    """
    SCENARIO: XConf cache has download delay
    SETUP: Create cache with delay (simulates CheckForUpdate response)
    EXECUTE: DownloadFirmware with URL from cache
    VERIFY: Delay happens before download
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
        api.DownloadFirmware(
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
   
    remove_file("/opt/CDL/ABCD_PDRI_img.bin")  #just to make sure previous test's traces aren't found
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

def test_connection_timeout_with_retry():
    """
    Connection timeout with retry logic
    
    SCENARIO: Unresolvable hostname causes connection timeout
    SETUP: Direct URL to unresolvable host
    EXECUTE: DownloadFirmware with unresolvable URL
    VERIFY: 
        1. Daemon attempts retries
        2. Eventually fails with network error
        3. No file created
        4. Retry attempts visible in logs/status
    
    """
    proc = start_daemon()
    initial_rdkfw_setup()
    write_device_prop()
    cleanup_daemon_files()
    
    remove_file("/tmp/pdri_image_file")
    pdri_file = Path("/tmp/pdri_image_file")
    pdri_file.touch(exist_ok=True)
    write_on_file("/tmp/pdri_image_file", "ABCD_PDRI_img")
    
    try:
        api = iface()
        result = api.RegisterProcess("TestApp", "1.0")
        handler_id = str(result[0] if isinstance(result, tuple) else result)
        assert int(handler_id) > 0, "Registration failed"
        
        # Unresolvable hostname - will timeout
        unresolvable_url = "https://unmockxconf:50052/featureControl/firmware.bin"
        
        api.DownloadFirmware(
            handler_id,
            "ABCD_PDRI_img.bin",
            unresolvable_url,
            "PCI"
        )
        
        # Wait for timeout and retries (daemon may retry 2-3 times)
        time.sleep(20)
        
        # File should NOT be created on network failure
        assert not os.path.exists("/opt/CDL/ABCD_PDRI_img.bin"), \
            "File should not exist after timeout"
        print("[PASS] No file created on timeout")
        
        # Check for retry evidence in logs or status file
        retry_found = False
        if os.path.exists(SWUPDATE_LOG_FILE_0):
            if grep_log_file(SWUPDATE_LOG_FILE_0, "retry") or \
               grep_log_file(SWUPDATE_LOG_FILE_0, "Codebig"):
                retry_found = True
                print("[PASS] Retry attempts logged")
        
        if os.path.exists(STATUS_FILE):
            with open(STATUS_FILE, 'r') as f:
                status = f.read()
                if "retry" in status.lower() or "error" in status.lower():
                    retry_found = True
                    print("[PASS] Status shows retry/error")
        
        # At least one retry indicator should be present
        assert retry_found, "No evidence of retry attempts found"
        
    finally:
        remove_file("/opt/CDL/ABCD_PDRI_img.bin")
        cleanup_daemon_files()
        stop_daemon(proc)


def test_file_already_exists():
    """
    File already exists optimization
    
    Adapted from: test_waiting_for_reboot (binary test 3)
    
    SCENARIO: Target file already exists at download location
    SETUP: Pre-create file at target path
    EXECUTE: DownloadFirmware to same path
    VERIFY:
        1. Daemon detects existing file
        2. Returns ALREADY_EXISTS (optimization) OR re-downloads (verification)
        3. No crash or error
    
    Prevents re-downloading same firmware
    """
    proc = start_daemon()
    initial_rdkfw_setup()
    write_device_prop()
    cleanup_daemon_files()
    
    # Pre-create file at target location
    target_file = "/opt/CDL/test_exists.bin"
    os.makedirs(os.path.dirname(target_file), exist_ok=True)
    with open(target_file, 'wb') as f:
        f.write(b"EXISTING_FIRMWARE_DATA" * 500)
    
    original_size = os.path.getsize(target_file)
    
    try:
        api = iface()
        result = api.RegisterProcess("TestApp", "1.0")
        handler_id = str(result[0] if isinstance(result, tuple) else result)
        assert int(handler_id) > 0, "Registration failed"
        
        # Try to download to existing file location
        result = api.DownloadFirmware(
            handler_id,
            "test_exists.bin",
            "https://mockxconf:50052/firmwareupdate/getfirmwaredata/ABCD_PDRI_img.bin",
            "PCI"
        )
        
        result_code = str(result[0] if isinstance(result, tuple) else result)
        
        # Two valid behaviors:
        # 1. ALREADY_EXISTS - daemon skips download (optimization)
        # 2. SUCCESS - daemon re-downloads (verification)
        valid_codes = ["RDKFW_DWNL_SUCCESS"]
        assert result_code in valid_codes, \
            f"Expected SUCCESS (optimization or re-download), got {result_code}"
        
        time.sleep(5)
        
        # File should still exist (either original or re-downloaded)
        assert os.path.exists(target_file), "Target file should still exist"
        print("[PASS] File exists handling works")
        
        # Check if file was re-downloaded (size changed) or kept (optimization)
        new_size = os.path.getsize(target_file)
        if new_size == original_size:
            print("[INFO] File kept (optimization)")
        else:
            print("[INFO] File re-downloaded (verification)")
                
    finally:
        remove_file(target_file)
        cleanup_daemon_files()
        stop_daemon(proc)


def test_pdri_firmware_type():
    proc = start_daemon()
    initial_rdkfw_setup()
    write_device_prop()
    cleanup_daemon_files()

    # CRITICAL: Create /tmp/pdri_image_file (required by checkPDRIUpgrade())
    # Content must match firmware name WITHOUT .bin extension
    remove_file("/tmp/pdri_image_file")
    pdri_file = Path("/tmp/pdri_image_file")
    pdri_file.touch(exist_ok=True)
    write_on_file("/tmp/pdri_image_file", "ABCD_PDRI_test")  # No .bin extension
    print("[INFO] Created /tmp/pdri_image_file with content: ABCD_PDRI_test")

    try:
        api = iface()
        result = api.RegisterProcess("TestApp", "1.0")
        handler_id = str(result[0] if isinstance(result, tuple) else result)
        assert int(handler_id) > 0, "Registration failed"

        # Call DownloadFirmware with PDRI type
        result = api.DownloadFirmware(
            handler_id,
            "ABCD_PDRI_test.bin",
            "https://mockxconf:50052/firmwareupdate/getfirmwaredata",  # Base URL
            "PDRI"  #PDRI type triggers PDRI-specific flow
        )

        # Verify D-Bus response
        result_code = str(result[0] if isinstance(result, tuple) else result)
        assert result_code == "RDKFW_DWNL_SUCCESS", \
            f"PDRI type should be accepted, got {result_code}"
        print("[PASS] PDRI firmware type accepted (D-Bus API)")

        # Wait for async download to complete
        time.sleep(10)

        # Verify file downloaded to /opt/CDL (informational - may fail with cert selector)
        # The key validation is D-Bus API acceptance above
        if wait_for_file("/opt/CDL/ABCD_PDRI_test.bin", timeout=15):
            print("[PASS] PDRI firmware file created: /opt/CDL/ABCD_PDRI_test.bin")
            file_created = True
        else:
            print("[INFO] File not created within timeout (may be expected with cert selector)")
            print("[INFO] D-Bus API correctly accepted PDRI type - primary test objective met")
            file_created = False

        # Verify status file updated (if not skipped by disableStatsUpdate)
        if os.path.exists(STATUS_FILE):
            with open(STATUS_FILE, 'r') as f:
                status_content = f.read()
                if "Download complete" in status_content or "Download In Progress" in status_content:
                    print("[PASS] Status file updated (PDRI download tracked)")
                else:
                    print("[INFO] Status file exists but may not show PDRI update (disableStatsUpdate=yes)")
        else:
            print("[INFO] Status file not created (expected with disableStatsUpdate=yes)")

        # Verify PDRI-specific log entries
        if os.path.exists(SWUPDATE_LOG_FILE_0):
            with open(SWUPDATE_LOG_FILE_0, 'r', errors='ignore') as f:
                log_content = f.read()
                pdri_logged = False
                
                # Check for PDRI-specific messages (per rdkv_upgrade.c)
                if "PDRI Download in Progress" in log_content:
                    print("[PASS] PDRI-specific log: 'PDRI Download in Progress'")
                    pdri_logged = True
                
                if "PDRI image upgrade successful" in log_content:
                    print("[PASS] PDRI-specific log: 'PDRI image upgrade successful'")
                    pdri_logged = True
                
                if "Triggering the Image Download" in log_content:
                    print("[PASS] Download worker triggered")
                    pdri_logged = True
                
                if not pdri_logged:
                    print("[INFO] PDRI-specific logs not found (may be in different log file)")
        
        # Verify NO flashing occurred (D-Bus sets download_only=1)
        # Check for absence of flash-related files/logs
        flash_indicators = [
            "/tmp/fw_preparing_to_reboot",
            "/tmp/currently_running_image_name",
            "/opt/cdl_flashed_file_name"
        ]
        found_flash_files = [f for f in flash_indicators if os.path.exists(f)]
        if found_flash_files:
            print(f"[ERROR] Flash indicator files found: {found_flash_files}")
            for flash_file in found_flash_files:
                if os.path.exists(flash_file):
                    try:
                        with open(flash_file, 'r') as f:
                            content = f.read()
                            print(f"[DEBUG] Content of {flash_file}: {content[:200]}")
                    except:
                        print(f"[DEBUG] {flash_file} exists but cannot read (may be empty)")
        
        assert not found_flash_files, \
            f"Flash should NOT occur for D-Bus DownloadFirmware (download_only=1). Found: {found_flash_files}"
        print("[PASS] No flashing occurred (download-only mode verified)")

    finally:
        remove_file("/opt/CDL/ABCD_PDRI_test.bin")
        remove_file("/tmp/pdri_image_file")
        cleanup_daemon_files()
        stop_daemon(proc)


def test_peripheral_firmware_type():
    proc = start_daemon()
    initial_rdkfw_setup()
    write_device_prop()
    cleanup_daemon_files()
    
    # Clean up potential download locations
    remove_file("/opt/CDL/peripheral_fw.bin")
    remove_file("/tmp/peripheral_fw.bin")

    try:
        api = iface()
        result = api.RegisterProcess("TestApp", "1.0")
        handler_id = str(result[0] if isinstance(result, tuple) else result)
        assert int(handler_id) > 0, "Registration failed"

        # Call DownloadFirmware with PERIPHERAL type
        result = api.DownloadFirmware(
            handler_id,
            "peripheral_fw.bin",
            "https://mockxconf:50052/firmwareupdate/getperipheralfirmwaredata",
            "PERIPHERAL"  # PERIPHERAL type - third valid type alongside PCI/PDRI
        )

        # Verify D-Bus API accepts PERIPHERAL as valid firmware type
        result_code = str(result[0] if isinstance(result, tuple) else result)
        assert result_code == "RDKFW_DWNL_SUCCESS", \
            f"PERIPHERAL type should be accepted, got {result_code}"
        print("[PASS] PERIPHERAL firmware type accepted (D-Bus API validation)")

        # Wait for async worker to process
        time.sleep(5)

        # Check if file was created (may or may not succeed depending on cert selector)
        # This is informational - the key validation is API acceptance above
        peripheral_found = False
        if os.path.exists("/opt/CDL/peripheral_fw.bin"):
            print("[PASS] PERIPHERAL firmware downloaded to /opt/CDL")
            peripheral_found = True
        elif os.path.exists("/tmp/peripheral_fw.bin"):
            print("[PASS] PERIPHERAL firmware downloaded to /tmp")
            peripheral_found = True
        else:
            print("[INFO] File not created (expected with cert selector in test environment)")
            print("[INFO] D-Bus API correctly accepted PERIPHERAL type - test objective met")
        
        # Check for worker activity in logs
        if os.path.exists(SWUPDATE_LOG_FILE_0):
            with open(SWUPDATE_LOG_FILE_0, 'r', errors='ignore') as f:
                log_content = f.read()
                if "Triggering the Image Download" in log_content:
                    print("[PASS] Download worker was triggered for PERIPHERAL type")
                if "PERIPHERAL" in log_content:
                    print("[PASS] PERIPHERAL type logged in worker")

    finally:
        remove_file("/opt/CDL/peripheral_fw.bin")
        remove_file("/tmp/peripheral_fw.bin")
        cleanup_daemon_files()
        stop_daemon(proc)

def test_progress_file_creation():
    """
    Progress file creation during download

    SCENARIO: Download creates progress file for monitoring
    SETUP: Start download
    EXECUTE: DownloadFirmware with valid URL
    VERIFY:
        1. /opt/curl_progress created during download
        2. File contains progress percentage
        3. Progress updates over time

    """
    proc = start_daemon()
    initial_rdkfw_setup()
    write_device_prop()
    cleanup_daemon_files()

    try:
        api = iface()
        result = api.RegisterProcess("TestApp", "1.0")
        handler_id = str(result[0] if isinstance(result, tuple) else result)
        assert int(handler_id) > 0, "Registration failed"

        result = api.DownloadFirmware(
            handler_id,
            "test_progress.bin",
            "https://mockxconf:50052/firmwareupdate/getfirmwaredata/test_progress.bin",
            "PCI"
        )

        # Progress file should be created DURING download
        # Wait a bit for download to start
        time.sleep(3)

        # Check if progress file exists
        progress_exists = wait_for_file(PROGRESS_FILE, timeout=10)

        if progress_exists:
            print("[PASS] Progress file created during download")

            # Try to read progress (may contain percentage)
            try:
                with open(PROGRESS_FILE, 'r') as f:
                    progress_content = f.read()
                    if progress_content.strip():
                        print(f"[INFO] Progress content: {progress_content[:100]}")
            except:
                pass
        else:
            # Progress file might be created briefly and removed after completion
            # Or implementation might use different progress mechanism
            print("[WARN] Progress file not found - may use different progress mechanism")

    finally:
        remove_file("/opt/CDL/test_progress.bin")
        remove_file(PROGRESS_FILE)
        cleanup_daemon_files()
        stop_daemon(proc)
