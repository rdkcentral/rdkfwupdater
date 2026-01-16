"""
Comprehensive Integration Test Suite for RDK Firmware Download Manager
Tests the complete DownloadFirmware flow from D-Bus interface through to file system

Test Scope:
- D-Bus method invocation and response validation
- Worker thread lifecycle and signal emission
- Progress monitoring and reporting
- Status file updates
- RFC notification integration
- Error handling and edge cases
- Concurrent download scenarios
- File system state management
"""

import pytest
import os
import time
import threading
import tempfile
import json
import signal
import subprocess
import hashlib
from pathlib import Path
from typing import Dict, List, Optional, Tuple
from unittest.mock import Mock, patch, MagicMock
from dataclasses import dataclass
from enum import IntEnum
import dbus
import dbus.service
import dbus.mainloop.glib
from gi.repository import GLib

# D-Bus service configuration (must match daemon's actual registration)
DBUS_SERVICE_NAME = "org.rdkfwupdater.Service"      # BUS_NAME
DBUS_OBJECT_PATH = "/org/rdkfwupdater/Service"      # OBJECT_PATH (actual daemon path)
DBUS_INTERFACE = "org.rdkfwupdater.Interface"       # Interface name

DAEMON_BINARY = "/usr/local/bin/rdkFwupdateMgr"
DAEMON_PID_FILE = "/tmp/rdkFwupdateMgr.pid"

XCONF_CACHE_FILE = "/tmp/xconf_response_thunder.txt"
XCONF_HTTP_CODE_FILE = "/tmp/xconf_httpcode_thunder.txt"
PROGRESS_FILE = "/opt/curl_progress"
#DownloadFirmwareStatusCode  Result codes
DOWNLOAD_SUCCESS = 0
DOWNLOAD_ALREADY_EXISTS = 1
DOWNLOAD_NETWORK_ERROR = 2
DOWNLOAD_NOT_FOUND = 3
DOWNLOAD_ERROR = 4

#DownloadResult
RDKFW_DWNL_SUCCESS = 0 #Firmware download initiated successfully.
RDKFW_DWNL_FAILED = 1  #Firmware download initiation failed.
def start_daemon():
    """Start the daemon."""
    subprocess.run(['pkill', '-9', '-f', 'rdkFwupdateMgr'], capture_output=True)
    time.sleep(0.5)
    proc = subprocess.Popen([DAEMON_BINARY, "0", "1"])
    time.sleep(3)
    return proc

def stop_daemon(proc):
    """Stop the daemon."""
    proc.terminate()
    proc.wait()

def iface():
    """Get D-Bus interface."""
    bus = dbus.SystemBus()
    proxy = bus.get_object(DBUS_SERVICE_NAME, DBUS_OBJECT_PATH)
    return dbus.Interface(proxy, DBUS_INTERFACE)

def cleanup_test_files():
    """Clean up all test files."""
    files = [PROGRESS_FILE, XCONF_CACHE_FILE,XCONF_HTTP_CODE_FILE,
             "/tmp/test_firmware.bin", "/tmp/test_download.bin",
             "/tmp/test_pci.bin", "/tmp/test_pdri.bin", "/tmp/test_peripheral.bin"]
    for f in files:
        if os.path.exists(f):
            try:
                os.remove(f)
            except:
                pass
    if os.path.exists("/tmp/firmware_test_server"):
        try:
            shutil.rmtree("/tmp/firmware_test_server")
        except:
            pass

def create_xconf_cache(firmware_url: str, firmware_version: str = "RDKV_TEST_12345"):
    """Create XConf cache file."""
    xconf_data = {
        "firmwareFilename": f"{firmware_version}.bin",
        "firmwareVersion": firmware_version,
        "firmwareLocation": firmware_url,
        "proto": "https",
        "rebootImmediately": False,
        "firmwareDownloadProtocol": "http"
    }
    os.makedirs(os.path.dirname(XCONF_CACHE_FILE), exist_ok=True)
    with open(XCONF_CACHE_FILE, 'w') as f:
        json.dump(xconf_data, f)

def read_status_file() -> Optional[str]:
    """Read status file."""
    if not os.path.exists(STATUS_FILE):
        return None
    try:
        with open(STATUS_FILE, 'r') as f:
            return f.read()
    except:
        return None

def wait_for_file(filepath: str, timeout: float = 10.0) -> bool:
    """Wait for file to exist."""
    start = time.time()
    while time.time() - start < timeout:
        if os.path.exists(filepath):
            time.sleep(0.5)
            return True
        time.sleep(0.2)
    return False

def create_test_firmware_file(filepath: str, size_bytes: int = 1024) -> str:
    """Create test firmware file."""
    content = b"TEST_FIRMWARE_DATA" * (size_bytes // 18 + 1)
    content = content[:size_bytes]
    Path(filepath).parent.mkdir(parents=True, exist_ok=True)
    with open(filepath, 'wb') as f:
        f.write(content)
    return hashlib.md5(content).hexdigest()

def start_test_http_server(port: int = 8888) -> Optional[subprocess.Popen]:
    """Start HTTP server."""
    server_dir = "/tmp/firmware_test_server"
    os.makedirs(server_dir, exist_ok=True)
    create_test_firmware_file(f"{server_dir}/test_firmware.bin", 10240)
    proc = subprocess.Popen(
        ["python3", "-m", "http.server", str(port), "--directory", server_dir],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
    )
    time.sleep(1)
    return proc

def stop_test_http_server(proc: Optional[subprocess.Popen]):
    """Stop HTTP server."""
    if proc:
        proc.terminate()
        proc.wait()


def test_download_with_null_firmware_name():
    """
    SCENARIO: firmwareName is empty string
    EXPECTED: Return DOWNLOAD_ERROR (4) or D-Bus error
    VALIDATES: Input validation prevents NULL firmware name
    """
    proc = start_daemon()
    cleanup_test_files()
    try:
        api = iface()
        result = api.RegisterProcess("TestApp", "1.0")
        handler_id = result[0] if isinstance(result, tuple) else int(result)
        assert handler_id > 0, "Registration failed"
        
        try:
            result = api.DownloadFirmware("", "test.bin","http://test.com/fw.bin", "PCI")
            result_code = result[0] if isinstance(result, tuple) else int(result)
            assert result_code == "RDKFW_DWNL_FAILED", f"Expected {RDKFW_DWNL_FAILED}, got {result_code}"
            print("[OK] Empty firmware name rejected with error code")
        except dbus.exceptions.DBusException as e:
            print(f"[OK] Empty firmware name rejected with D-Bus error: {e.get_dbus_name()}")
    finally:
        cleanup_test_files()
        stop_daemon(proc)

def test_download_with_invalid_firmware_type():
    """
    SCENARIO: typeOfFirmware is invalid (not PCI/PDRI/PERIPHERAL)
    EXPECTED: Return DOWNLOAD_ERROR  or D-Bus error
    VALIDATES: Firmware type validation
    """
    proc = start_daemon()
    cleanup_test_files()
    try:
        api = iface()
        result = api.RegisterProcess("TestApp", "1.0")
        handler_id = result[0] if isinstance(result, tuple) else int(result)
        assert handler_id > 0, "Registration failed"
        
        try:
            result = api.DownloadFirmware(str(handler_id),"test_img.bin","http://test.com/","INVALID")
            result_code = result[0] if isinstance(result, tuple) else int(result)
            assert result_code == "RDKFW_DWNL_FAILED", f"Expected {RDKFW_DWNL_FAILED}, got {result_code}"
            print("[OK] Invalid firmware type rejected")
        except dbus.exceptions.DBusException as e:
            print(f"[OK] Invalid firmware type rejected: {e.get_dbus_name()}")
    finally:
        cleanup_test_files()
        stop_daemon(proc)

def test_download_with_unregistered_handler():
    """
    SCENARIO: DownloadFirmware called without RegisterProcess first
    EXPECTED: D-Bus error or rejection
    VALIDATES: Process must be registered before downloading
    """
    proc = start_daemon()
    cleanup_test_files()
    try:
        api = iface()
        try:
            result = api.DownloadFirmware(str(999), "fw.bin","http://test.com/","PCI")
            result_code = result[0] if isinstance(result, tuple) else int(result)
            assert result_code == "RDKFW_DWNL_FAILED", f"Expected {RDKFW_DWNL_FAILED}, got {result_code}"
            print("[OK] Request from unregistered client")
        except dbus.exceptions.DBusException as e:
            print(f"[OK] Request from unregistered client: {e.get_dbus_name()}")
    finally:
        cleanup_test_files()
        stop_daemon(proc)


def test_download_with_custom_url():
    """
    SCENARIO: downloadUrl parameter is non-empty
    EXPECTED: Uses provided URL, ignores XConf cache
    VALIDATES: Custom URL takes precedence
    """
    proc = start_daemon()
    cleanup_test_files()
    server = start_test_http_server()
    try:
        api = iface()
        result = api.RegisterProcess("TestApp", "1.0")
        handler_id = result[0] if isinstance(result, tuple) else int(result)
        assert handler_id > 0, "Registration failed"
        
        # Create XConf cache with DIFFERENT URL
        create_xconf_cache("https://wrong.server.com/wrong.bin")
        try:
            # Download with custom URL
            custom_url = "https://localhost:8888/test_firmware.bin"
            result = api.DownloadFirmware(str(handler_id), "test_fw.bin","https://localhost:8888/","PCI")
            result_code = result[0] if isinstance(result, tuple) else int(result)
            assert result_code == "RDKFW_DWNL_SUCCESS", f"Expected {RDKFW_DWNL_SUCCESS}, got {result_code}"
        except dbus.exceptions.DBusException as e:
            print(f"[OK] invalid URL accepted")
    finally:
        cleanup_test_files()
        stop_test_http_server(server)
        stop_daemon(proc)

def test_download_with_invalid_custom_url():
    """
    SCENARIO: downloadUrl parameter is non-empty
    EXPECTED: Uses provided URL, ignores XConf cache
    VALIDATES: Custom URL takes precedence
    """
    proc = start_daemon()
    cleanup_test_files()
    server = start_test_http_server()
    try:
        api = iface()
        #Register first
        result = api.RegisterProcess("TestApp", "1.0")
        handler_id = result[0] if isinstance(result, tuple) else int(result)
        assert handler_id > 0, "Registration failed"

        # Create XConf cache with DIFFERENT URL
        create_xconf_cache("https://wrong.server.com/wrong.bin")
        
        try:
            # Download with custom URL
            custom_url = "http:localhost:8888/test_invalidurl"
            result = api.DownloadFirmware(str(handler_id), "test_download.bin","http:wrong.server.com/","PCI")
            result_code = result[0] if isinstance(result, tuple) else int(result)
            assert result_code == "RDKFW_DWNL_FAILED", f"Expected {RDKFW_DWNL_FAILED}, got {result_code}"
            
        except dbus.exceptions.DBusException as e:
            print(f"[OK] invalid URL rejected")
    finally:                                                                                                                         
        cleanup_test_files()                                                                                                         
        stop_test_http_server(server)                                                                                                
        stop_daemon(proc)
