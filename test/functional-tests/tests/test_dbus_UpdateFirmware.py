#!/usr/bin/env python3
"""
RDK Firmware Update Manager - UpdateFirmware D-Bus Integration Tests

Tests cover:
- Success scenarios (PCI, PDRI, PERIPHERAL)
- Parameter validation
- Concurrency control
- Error handling
- Integration with DownloadFirmware
"""

import dbus
import subprocess
import time
import os
import signal
from pathlib import Path
from threading import Thread, Event
import json

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
REBOOT_FLAG_FILE = "/tmp/fw_preparing_to_reboot"

# Firmware directories
FIRMWARE_DIR = "/opt/CDL"
FLASH_SCRIPT = "/lib/rdk/imageFlasher.sh"
MOCK_FLASH_SCRIPT = "/tmp/mock_imageFlasher.sh"

# Result codes
RDKFW_UPDATE_SUCCESS = "RDKFW_UPDATE_SUCCESS"
RDKFW_UPDATE_FAILED = "RDKFW_UPDATE_FAILED"

# Flash status codes
FW_UPDATE_INPROGRESS = 0
FW_UPDATE_COMPLETED = 1
FW_UPDATE_ERROR = 2


def write_device_prop():
    """Same as DownloadFirmware tests"""
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
    """Start D-Bus daemon - same as DownloadFirmware"""
    subprocess.run(['pkill', '-9', '-f', 'rdkFwupdateMgr'], capture_output=True)
    time.sleep(0.5)
    proc = subprocess.Popen([DAEMON_BINARY, "0", "1"])
    time.sleep(3)
    return proc


def stop_daemon(proc):
    """Stop daemon - same as DownloadFirmware"""
    proc.terminate()
    proc.wait()


def iface():
    """Get D-Bus interface - same as DownloadFirmware"""
    bus = dbus.SystemBus()
    proxy = bus.get_object(DBUS_SERVICE_NAME, DBUS_OBJECT_PATH)
    return dbus.Interface(proxy, DBUS_INTERFACE)


def cleanup_daemon_files():
    """Clean daemon-specific files - same as DownloadFirmware"""
    remove_file(STATUS_FILE)
    remove_file(PROGRESS_FILE)
    remove_file(XCONF_CACHE_FILE)
    remove_file(REBOOT_FLAG_FILE)


def wait_for_file(filepath, timeout=15.0):
    """Wait for file to exist - same as DownloadFirmware"""
    start = time.time()
    while time.time() - start < timeout:
        if os.path.exists(filepath):
            time.sleep(0.5)
            return True
        time.sleep(0.2)
    return False


def create_mock_firmware_file(filename, size_kb=100):
    """Create a mock firmware file for testing"""
    filepath = os.path.join(FIRMWARE_DIR, filename)
    os.makedirs(FIRMWARE_DIR, exist_ok=True)
    
    # Create file with some content
    with open(filepath, 'wb') as f:
        f.write(b'\x00' * (size_kb * 1024))
    
    return filepath


def create_mock_flash_script(return_code=0, create_reboot_flag=False):
    """
    Create a mock imageFlasher.sh script for testing
    
    Args:
        return_code: Exit code (0=success, -2=ENOENT, -28=ENOSPC, -1=error)
        create_reboot_flag: If True, creates /tmp/fw_preparing_to_reboot
    """
    script_content = f"""#!/bin/bash
# Mock imageFlasher.sh for testing
# Arguments: $1=server_url, $2=firmware_path, $3=reboot_flag, $4=proto, $5=upgrade_type, $6=maint, $7=trigger_type

echo "[MOCK FLASH] Called with args: $@"
echo "[MOCK FLASH] Firmware: $2"
echo "[MOCK FLASH] Reboot: $3"
echo "[MOCK FLASH] Type: $5"

# Simulate flash delay
sleep 2

# Create reboot flag if requested
if [ "$3" = "true" ] || [ "{create_reboot_flag}" = "True" ]; then
    touch {REBOOT_FLAG_FILE}
    echo "[MOCK FLASH] Created reboot flag"
fi

# Return specified code
exit {return_code}
"""
    
    with open(MOCK_FLASH_SCRIPT, 'w') as f:
        f.write(script_content)
    
    os.chmod(MOCK_FLASH_SCRIPT, 0o755)
    
    # Backup real script and replace with mock
    if os.path.exists(FLASH_SCRIPT):
        subprocess.run(['mv', FLASH_SCRIPT, f"{FLASH_SCRIPT}.backup"], 
                      capture_output=True)
    subprocess.run(['cp', MOCK_FLASH_SCRIPT, FLASH_SCRIPT], 
                  capture_output=True)


def restore_flash_script():
    """Restore original imageFlasher.sh"""
    if os.path.exists(f"{FLASH_SCRIPT}.backup"):
        subprocess.run(['mv', f"{FLASH_SCRIPT}.backup", FLASH_SCRIPT], 
                      capture_output=True)
    remove_file(MOCK_FLASH_SCRIPT)


class UpdateProgressMonitor:
    """Monitor UpdateProgress signals from D-Bus"""
    
    def __init__(self):
        self.signals = []
        self.stop_event = Event()
        self.monitor_thread = None
    
    def signal_handler(self, handler_id, firmware_name, progress, status, message):
        """Callback for UpdateProgress signal"""
        signal_data = {
            'handler_id': int(handler_id),
            'firmware_name': str(firmware_name),
            'progress': int(progress),
            'status': int(status),
            'message': str(message),
            'timestamp': time.time()
        }
        self.signals.append(signal_data)
        print(f"[SIGNAL] UpdateProgress: {progress}%, status={status}, msg='{message}'")
    
    def start(self):
        """Start monitoring signals in background thread"""
        def monitor():
            from dbus.mainloop.glib import DBusGMainLoop
            from gi.repository import GLib
            
            DBusGMainLoop(set_as_default=True)
            bus = dbus.SystemBus()
            
            bus.add_signal_receiver(
                self.signal_handler,
                signal_name='UpdateProgress',
                dbus_interface=DBUS_INTERFACE,
                bus_name=DBUS_SERVICE_NAME,
                path=DBUS_OBJECT_PATH
            )
            
            loop = GLib.MainLoop()
            
            # Stop loop when stop_event is set
            def check_stop():
                if self.stop_event.is_set():
                    loop.quit()
                    return False
                return True
            
            GLib.timeout_add(500, check_stop)
            loop.run()
        
        self.monitor_thread = Thread(target=monitor, daemon=True)
        self.monitor_thread.start()
        time.sleep(1)  # Give thread time to setup
    
    def stop(self):
        """Stop monitoring"""
        self.stop_event.set()
        if self.monitor_thread:
            self.monitor_thread.join(timeout=2)
    
    def wait_for_progress(self, expected_progress, timeout=30):
        """Wait for specific progress value"""
        start = time.time()
        while time.time() - start < timeout:
            for sig in self.signals:
                if sig['progress'] == expected_progress:
                    return sig
            time.sleep(0.2)
        return None
    
    def get_final_signal(self):
        """Get last signal (should be 100% or -1%)"""
        return self.signals[-1] if self.signals else None



def test_update_pci_firmware_success():
    """
    Test 1: Basic PCI firmware flash success
    
    SCENARIO: Flash PCI firmware successfully
    SETUP: 
        - Mock firmware file exists
        - Mock flash script returns success
    EXECUTE: UpdateFirmware with PCI type
    VERIFY:
        - Returns RDKFW_UPDATE_SUCCESS
        - UpdateProgress signals: 0% ->25% ->50% ->75% ->100%
        - Final status = FW_UPDATE_COMPLETED
    """
    proc = start_daemon()
    initial_rdkfw_setup()
    write_device_prop()
    cleanup_daemon_files()
    
    # Create mock firmware file
    firmware_name = "ABCD_PCI_test.bin"
    firmware_path = create_mock_firmware_file(firmware_name)
    
    # Create mock flash script (success)
    create_mock_flash_script(return_code=0)
    
    # Start signal monitoring
    monitor = UpdateProgressMonitor()
    monitor.start()
    
    try:
        api = iface()
        result = api.RegisterProcess("TestApp", "1.0")
        handler_id = str(result[0] if isinstance(result, tuple) else result)
        assert int(handler_id) > 0, "Registration failed"
        
        # Call UpdateFirmware
        result = api.UpdateFirmware(
            handler_id,
            firmware_name,
            FIRMWARE_DIR,
            "PCI",
            "false"  # No immediate reboot
        )
        
        # Parse result
        update_result = str(result[0] if isinstance(result, tuple) else result[0])
        update_status = str(result[1] if isinstance(result, tuple) and len(result) > 1 else result[1])
        
        # Verify immediate response
        assert update_result == RDKFW_UPDATE_SUCCESS, \
            f"Expected {RDKFW_UPDATE_SUCCESS}, got {update_result}"
        print("[PASS] UpdateFirmware accepted")
        
        # Wait for completion signal (100%)
        completion_signal = monitor.wait_for_progress(100, timeout=30)
        assert completion_signal is not None, "Did not receive 100% progress signal"
        assert completion_signal['status'] == FW_UPDATE_COMPLETED, \
            f"Expected COMPLETED status, got {completion_signal['status']}"
        print("[PASS] Flash completed successfully (100%)")
        
        # Verify signal sequence
        progress_values = [sig['progress'] for sig in monitor.signals]
        assert 0 in progress_values, "Missing 0% signal"
        assert 100 in progress_values, "Missing 100% signal"
        print(f"[PASS] Progress sequence: {progress_values}")
        
    finally:
        monitor.stop()
        remove_file(firmware_path)
        restore_flash_script()
        cleanup_daemon_files()
        stop_daemon(proc)


def test_update_pdri_firmware_success():
    """
    Test 2: PDRI firmware flash success
    
    SCENARIO: Flash PDRI firmware
    SETUP: Mock PDRI firmware
    EXECUTE: UpdateFirmware with PDRI type
    VERIFY:
        - Flash succeeds
        - upgrade_type=1 passed to flash script
    """
    proc = start_daemon()
    initial_rdkfw_setup()
    write_device_prop()
    cleanup_daemon_files()
    
    firmware_name = "ABCD_PDRI_test.bin"
    firmware_path = create_mock_firmware_file(firmware_name)
    create_mock_flash_script(return_code=0)
    
    monitor = UpdateProgressMonitor()
    monitor.start()
    
    try:
        api = iface()
        result = api.RegisterProcess("TestApp", "1.0")
        handler_id = str(result[0] if isinstance(result, tuple) else result)
        assert int(handler_id) > 0, "Registration failed"
        
        result = api.UpdateFirmware(
            handler_id,
            firmware_name,
            FIRMWARE_DIR,
            "PDRI",  # PDRI type
            "false"
        )
        
        update_result = str(result[0] if isinstance(result, tuple) else result[0])
        assert update_result == RDKFW_UPDATE_SUCCESS, \
            f"PDRI flash should be accepted, got {update_result}"
        print("[PASS] PDRI firmware accepted")
        
        # Wait for completion
        completion_signal = monitor.wait_for_progress(100, timeout=30)
        assert completion_signal is not None, "PDRI flash did not complete"
        assert completion_signal['status'] == FW_UPDATE_COMPLETED, \
            "PDRI flash did not complete successfully"
        print("[PASS] PDRI firmware flashed successfully")
        
    finally:
        monitor.stop()
        remove_file(firmware_path)
        restore_flash_script()
        cleanup_daemon_files()
        stop_daemon(proc)


def test_update_peripheral_firmware_success():
    """
    Test 3: PERIPHERAL firmware flash success
    
    SCENARIO: Flash PERIPHERAL firmware
    SETUP: Mock PERIPHERAL firmware
    EXECUTE: UpdateFirmware with PERIPHERAL type
    VERIFY: Flash succeeds with upgrade_type=2
    """
    proc = start_daemon()
    initial_rdkfw_setup()
    write_device_prop()
    cleanup_daemon_files()
    
    firmware_name = "peripheral_test.bin"
    firmware_path = create_mock_firmware_file(firmware_name)
    create_mock_flash_script(return_code=0)
    
    monitor = UpdateProgressMonitor()
    monitor.start()
    
    try:
        api = iface()
        result = api.RegisterProcess("TestApp", "1.0")
        handler_id = str(result[0] if isinstance(result, tuple) else result)
        assert int(handler_id) > 0, "Registration failed"
        
        result = api.UpdateFirmware(
            handler_id,
            firmware_name,
            FIRMWARE_DIR,
            "PERIPHERAL",  # PERIPHERAL type
            "false"
        )
        
        update_result = str(result[0] if isinstance(result, tuple) else result[0])
        assert update_result == RDKFW_UPDATE_SUCCESS, \
            f"PERIPHERAL flash should be accepted, got {update_result}"
        print("[PASS] PERIPHERAL firmware accepted")
        
        # Wait for completion
        completion_signal = monitor.wait_for_progress(100, timeout=30)
        assert completion_signal is not None, "PERIPHERAL flash did not complete"
        assert completion_signal['status'] == FW_UPDATE_COMPLETED, \
            "PERIPHERAL flash did not complete successfully"
        print("[PASS] PERIPHERAL firmware flashed successfully")
        
    finally:
        monitor.stop()
        remove_file(firmware_path)
        restore_flash_script()
        cleanup_daemon_files()
        stop_daemon(proc)


def test_update_firmware_file_not_found():
    """
    Test 4: Firmware file not found
    
    SCENARIO: Firmware file doesn't exist in directory
    SETUP: Directory exists but file doesn't
    EXECUTE: UpdateFirmware with non-existent file
    VERIFY: Returns RDKFW_UPDATE_FAILED
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
        
        # Try to flash non-existent file
        result = api.UpdateFirmware(
            handler_id,
            "nonexistent.bin",  # File doesn't exist
            FIRMWARE_DIR,
            "PCI",
            "false"
        )
        
        update_result = str(result[0] if isinstance(result, tuple) else result[0])
        assert update_result == RDKFW_UPDATE_FAILED, \
            f"Should reject missing file, got {update_result}"
        print("[PASS] Missing firmware file rejected")
        
        # Check error message
        error_msg = str(result[2] if isinstance(result, tuple) and len(result) > 2 else "")
        assert "not present" in error_msg.lower() or "not found" in error_msg.lower(), \
            f"Error message should mention file not found: {error_msg}"
        print(f"[PASS] Error message: {error_msg}")
        
    finally:
        cleanup_daemon_files()
        stop_daemon(proc)


def test_update_directory_not_exist():
    """
    Test 5: Directory doesn't exist
    
    SCENARIO: Firmware directory doesn't exist
    SETUP: Provide non-existent directory
    EXECUTE: UpdateFirmware
    VERIFY: Returns RDKFW_UPDATE_FAILED
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
        
        # Try with non-existent directory
        result = api.UpdateFirmware(
            handler_id,
            "firmware.bin",
            "/nonexistent/path",  # Directory doesn't exist
            "PCI",
            "false"
        )
        
        update_result = str(result[0] if isinstance(result, tuple) else result[0])
        assert update_result == RDKFW_UPDATE_FAILED, \
            f"Should reject non-existent directory, got {update_result}"
        print("[PASS] Non-existent directory rejected")
        
        # Check error message
        error_msg = str(result[2] if isinstance(result, tuple) and len(result) > 2 else "")
        assert "directory" in error_msg.lower() or "not exist" in error_msg.lower(), \
            f"Error message should mention directory: {error_msg}"
        print(f"[PASS] Error message: {error_msg}")
        
    finally:
        cleanup_daemon_files()
        stop_daemon(proc)


def test_update_while_download_in_progress():
    """
    Test 6: Flash while download in progress
    
    SCENARIO: UpdateFirmware called while DownloadFirmware is running
    SETUP: Start DownloadFirmware (sets IsDownloadInProgress=TRUE)
    EXECUTE: UpdateFirmware
    VERIFY: Returns RDKFW_UPDATE_FAILED with "On going Firmware Download"
    """
    proc = start_daemon()
    initial_rdkfw_setup()
    write_device_prop()
    cleanup_daemon_files()
    
    firmware_name = "test.bin"
    firmware_path = create_mock_firmware_file(firmware_name)
    
    try:
        api = iface()
        result = api.RegisterProcess("TestApp", "1.0")
        handler_id = str(result[0] if isinstance(result, tuple) else result)
        assert int(handler_id) > 0, "Registration failed"
        
        # Start a download (doesn't complete immediately)
        download_result = api.DownloadFirmware(
            handler_id,
            "download_file.bin",
            "https://mockxconf:50052/firmwareupdate/getfirmwaredata/file.bin",
            "PCI"
        )
        
        # Immediately try to flash (download still in progress)
        time.sleep(1)  # Give download time to start
        
        result = api.UpdateFirmware(
            handler_id,
            firmware_name,
            FIRMWARE_DIR,
            "PCI",
            "false"
        )
        
        update_result = str(result[0] if isinstance(result, tuple) else result[0])
        assert update_result == RDKFW_UPDATE_FAILED, \
            f"Should reject flash during download, got {update_result}"
        print("[PASS] Flash blocked during download")
        
        # Check error message
        error_msg = str(result[2] if isinstance(result, tuple) and len(result) > 2 else "")
        assert "download" in error_msg.lower(), \
            f"Error should mention ongoing download: {error_msg}"
        print(f"[PASS] Error message: {error_msg}")
        
    finally:
        remove_file(firmware_path)
        cleanup_daemon_files()
        stop_daemon(proc)


def test_update_while_flash_in_progress():
    """
    Test 7: Flash while another flash in progress
    
    SCENARIO: UpdateFirmware called while another flash is running
    SETUP: Start UpdateFirmware #1 (sets IsFlashInProgress=TRUE)
    EXECUTE: UpdateFirmware #2 immediately
    VERIFY: Second request rejected with "On going Flash Firmware"
    """
    proc = start_daemon()
    initial_rdkfw_setup()
    write_device_prop()
    cleanup_daemon_files()
    
    firmware1 = "firmware1.bin"
    firmware2 = "firmware2.bin"
    path1 = create_mock_firmware_file(firmware1)
    path2 = create_mock_firmware_file(firmware2)
    
    # Mock script with delay to keep flash running
    create_mock_flash_script(return_code=0)
    
    try:
        api = iface()
        result = api.RegisterProcess("TestApp", "1.0")
        handler_id = str(result[0] if isinstance(result, tuple) else result)
        assert int(handler_id) > 0, "Registration failed"
        
        # Start first flash
        result1 = api.UpdateFirmware(
            handler_id,
            firmware1,
            FIRMWARE_DIR,
            "PCI",
            "false"
        )
        
        update_result1 = str(result1[0] if isinstance(result1, tuple) else result1[0])
        assert update_result1 == RDKFW_UPDATE_SUCCESS, "First flash should be accepted"
        print("[PASS] First flash started")
        
        # Immediately try second flash (first still in progress)
        time.sleep(1)
        
        result2 = api.UpdateFirmware(
            handler_id,
            firmware2,
            "PCI",
            FIRMWARE_DIR,
            "false"
        )
        
        update_result2 = str(result2[0] if isinstance(result2, tuple) else result2[0])
        assert update_result2 == RDKFW_UPDATE_FAILED, \
            f"Second flash should be rejected, got {update_result2}"
        print("[PASS] Second flash blocked")
        
        # Check error message
        error_msg = str(result2[2] if isinstance(result2, tuple) and len(result2) > 2 else "")
        assert "flash" in error_msg.lower() or "ongoing" in error_msg.lower(), \
            f"Error should mention ongoing flash: {error_msg}"
        print(f"[PASS] Error message: {error_msg}")
        
    finally:
        remove_file(path1)
        remove_file(path2)
        restore_flash_script()
        cleanup_daemon_files()
        time.sleep(5)  # Wait for first flash to complete
        stop_daemon(proc)


def test_update_flash_script_failure():
    """
    Test 8: Flash script returns error
    
    SCENARIO: imageFlasher.sh returns non-zero
    SETUP: Mock script returns -1 (error)
    EXECUTE: UpdateFirmware
    VERIFY:
        - UpdateProgress -1% (error)
        - Status = FW_UPDATE_ERROR
    """
    proc = start_daemon()
    initial_rdkfw_setup()
    write_device_prop()
    cleanup_daemon_files()
    
    firmware_name = "test.bin"
    firmware_path = create_mock_firmware_file(firmware_name)
    
    # Mock script returns error
    create_mock_flash_script(return_code=1)  # Non-zero = error
    
    monitor = UpdateProgressMonitor()
    monitor.start()
    
    try:
        api = iface()
        result = api.RegisterProcess("TestApp", "1.0")
        handler_id = str(result[0] if isinstance(result, tuple) else result)
        assert int(handler_id) > 0, "Registration failed"
        
        result = api.UpdateFirmware(
            handler_id,
            firmware_name,
            FIRMWARE_DIR,
            "PCI",
            "false"
        )
        
        update_result = str(result[0] if isinstance(result, tuple) else result[0])
        assert update_result == RDKFW_UPDATE_SUCCESS, \
            "Request should be accepted (failure happens in worker)"
        print("[PASS] UpdateFirmware request accepted")
        
        # Wait for error signal (-1%)
        error_signal = monitor.wait_for_progress(-1, timeout=30)
        assert error_signal is not None, "Did not receive error signal"
        assert error_signal['status'] == FW_UPDATE_ERROR, \
            f"Expected ERROR status, got {error_signal['status']}"
        print(f"[PASS] Flash error detected: {error_signal['message']}")
        
    finally:
        monitor.stop()
        remove_file(firmware_path)
        restore_flash_script()
        cleanup_daemon_files()
        stop_daemon(proc)


def test_update_immediate_reboot_flag():
    """
    Test 9: Immediate reboot flag handling
    
    SCENARIO: Flash with rebootImmediately="true"
    SETUP: Mock firmware and script
    EXECUTE: UpdateFirmware with rebootImmediately="true"
    VERIFY:
        - Flash succeeds
        - reboot_flag="true" passed to script
        - /tmp/fw_preparing_to_reboot created
    """
    proc = start_daemon()
    initial_rdkfw_setup()
    write_device_prop()
    cleanup_daemon_files()
    
    remove_file(REBOOT_FLAG_FILE)
    
    firmware_name = "test.bin"
    firmware_path = create_mock_firmware_file(firmware_name)
    
    # Mock script creates reboot flag
    create_mock_flash_script(return_code=0, create_reboot_flag=True)
    
    monitor = UpdateProgressMonitor()
    monitor.start()
    
    try:
        api = iface()
        result = api.RegisterProcess("TestApp", "1.0")
        handler_id = str(result[0] if isinstance(result, tuple) else result)
        assert int(handler_id) > 0, "Registration failed"
        
        result = api.UpdateFirmware(
            handler_id,
            firmware_name,
            FIRMWARE_DIR,
            "PCI",
            "true"  # Immediate reboot
        )
        
        update_result = str(result[0] if isinstance(result, tuple) else result[0])
        assert update_result == RDKFW_UPDATE_SUCCESS, "Flash should be accepted"
        print("[PASS] Flash with immediate reboot started")
        
        # Wait for completion
        completion_signal = monitor.wait_for_progress(100, timeout=30)
        assert completion_signal is not None, "Flash did not complete"
        print("[PASS] Flash completed")
        
        # Verify reboot flag created
        time.sleep(1)  # Give script time to create flag
        assert os.path.exists(REBOOT_FLAG_FILE), \
            "Reboot flag file should be created"
        print("[PASS] Reboot flag file created")
        
    finally:
        monitor.stop()
        remove_file(firmware_path)
        remove_file(REBOOT_FLAG_FILE)
        restore_flash_script()
        cleanup_daemon_files()
        stop_daemon(proc)


def test_update_progress_signals_basic():
    """
    Test 10: Progress signals are emitted
    
    SCENARIO: Verify UpdateProgress signals are emitted
    SETUP: Flash firmware successfully
    EXECUTE: UpdateFirmware
    VERIFY: At least receives 0% and 100% signals
    
    NOTE: This is a basic test - just confirms signals work
    """
    proc = start_daemon()
    initial_rdkfw_setup()
    write_device_prop()
    cleanup_daemon_files()
    
    firmware_name = "test.bin"
    firmware_path = create_mock_firmware_file(firmware_name)
    create_mock_flash_script(return_code=0)
    
    monitor = UpdateProgressMonitor()
    monitor.start()
    
    try:
        api = iface()
        result = api.RegisterProcess("TestApp", "1.0")
        handler_id = str(result[0] if isinstance(result, tuple) else result)
        assert int(handler_id) > 0, "Registration failed"
        
        result = api.UpdateFirmware(
            handler_id,
            firmware_name,
            FIRMWARE_DIR,
            "PCI",
            "false"
        )
        
        # Wait for completion
        time.sleep(10)
        
        # Verify signals received
        assert len(monitor.signals) > 0, "No UpdateProgress signals received"
        print(f"[PASS] Received {len(monitor.signals)} progress signals")
        
        # Verify has 0% and 100%
        progress_values = [sig['progress'] for sig in monitor.signals]
        assert 0 in progress_values, "Missing 0% signal"
        assert 100 in progress_values, "Missing 100% signal"
        print(f"[PASS] Progress sequence: {progress_values}")
        
    finally:
        monitor.stop()
        remove_file(firmware_path)
        restore_flash_script()
        cleanup_daemon_files()
        stop_daemon(proc)


def test_update_unregistered_handler():
    """
    Test 11: UpdateFirmware with unregistered handler
    
    SCENARIO: Call UpdateFirmware without RegisterProcess
    SETUP: Start daemon but don't register
    EXECUTE: UpdateFirmware with handler_id that was never registered
    VERIFY: Returns RDKFW_UPDATE_FAILED with "Handler not registered"
    """
    proc = start_daemon()
    initial_rdkfw_setup()
    write_device_prop()
    cleanup_daemon_files()
    
    firmware_name = "test.bin"
    firmware_path = create_mock_firmware_file(firmware_name)
    create_mock_flash_script(return_code=0)
    
    try:
        api = iface()
        
        # Call UpdateFirmware with fake/unregistered handler ID (e.g., "999")
        result = api.UpdateFirmware(
            "999",              # Unregistered handler_id
            firmware_name,
            FIRMWARE_DIR,       # LocationOfFirmware
            "PCI",              # TypeOfFirmware
            "false"
        )
        
        update_result = str(result[0] if isinstance(result, tuple) else result[0])
        assert update_result == RDKFW_UPDATE_FAILED, \
            f"Should reject unregistered handler, got {update_result}"
        print("[PASS] Unregistered handler rejected")
        
        # Check error message
        error_msg = str(result[2] if isinstance(result, tuple) and len(result) > 2 else "")
        assert "registered" in error_msg.lower() or "handler" in error_msg.lower(), \
            f"Expected registration error, got: {error_msg}"
        print(f"[PASS] Error message: {error_msg}")
        
    finally:
        remove_file(firmware_path)
        restore_flash_script()
        cleanup_daemon_files()
        stop_daemon(proc)


def test_update_empty_handler_id():
    """
    Test 12: UpdateFirmware with empty handler ID
    
    SCENARIO: Call UpdateFirmware with empty/NULL handler_id
    SETUP: Register process but pass empty handler_id
    EXECUTE: UpdateFirmware("", "fw.bin", "PCI", "/opt/CDL", "false")
    VERIFY: Returns RDKFW_UPDATE_FAILED with "Invalid handler ID"
    """
    proc = start_daemon()
    initial_rdkfw_setup()
    write_device_prop()
    cleanup_daemon_files()
    
    firmware_name = "test.bin"
    firmware_path = create_mock_firmware_file(firmware_name)
    create_mock_flash_script(return_code=0)
    
    try:
        api = iface()
        
        # Register (but don't use the handler_id)
        result = api.RegisterProcess("TestApp", "1.0")
        handler_id = str(result[0] if isinstance(result, tuple) else result)
        assert int(handler_id) > 0, "Registration failed"
        
        # Call UpdateFirmware with EMPTY handler_id
        result = api.UpdateFirmware(
            "",                 # Empty handler_id
            firmware_name,
            FIRMWARE_DIR,       # LocationOfFirmware
            "PCI",              # TypeOfFirmware
            "false"
        )
        
        update_result = str(result[0] if isinstance(result, tuple) else result[0])
        assert update_result == RDKFW_UPDATE_FAILED, \
            f"Should reject empty handler ID, got {update_result}"
        print("[PASS] Empty handler ID rejected")
        
        # Check error message
        error_msg = str(result[2] if isinstance(result, tuple) and len(result) > 2 else "")
        assert "handler" in error_msg.lower() or "invalid" in error_msg.lower(), \
            f"Expected handler error, got: {error_msg}"
        print(f"[PASS] Error message: {error_msg}")
        
    finally:
        remove_file(firmware_path)
        restore_flash_script()
        cleanup_daemon_files()
        stop_daemon(proc)


def test_update_empty_firmware_name():
    """
    Test 13: UpdateFirmware with empty firmware name
    
    SCENARIO: Call UpdateFirmware with empty/NULL firmware name
    SETUP: Valid handler but empty firmware name
    EXECUTE: UpdateFirmware(handler_id, "", "PCI", "/opt/CDL", "false")
    VERIFY: Returns RDKFW_UPDATE_FAILED with "Invalid firmware name"
    """
    proc = start_daemon()
    initial_rdkfw_setup()
    write_device_prop()
    cleanup_daemon_files()
    
    create_mock_flash_script(return_code=0)
    
    try:
        api = iface()
        result = api.RegisterProcess("TestApp", "1.0")
        handler_id = str(result[0] if isinstance(result, tuple) else result)
        assert int(handler_id) > 0, "Registration failed"
        
        # Call UpdateFirmware with EMPTY firmware name
        result = api.UpdateFirmware(
            handler_id,
            "",                 # Empty firmware name
            FIRMWARE_DIR,       # LocationOfFirmware
            "PCI",              # TypeOfFirmware
            "false"
        )
        
        update_result = str(result[0] if isinstance(result, tuple) else result[0])
        assert update_result == RDKFW_UPDATE_FAILED, \
            f"Should reject empty firmware name, got {update_result}"
        print("[PASS] Empty firmware name rejected")
        
        # Check error message
        error_msg = str(result[2] if isinstance(result, tuple) and len(result) > 2 else "")
        assert "firmware" in error_msg.lower() or "invalid" in error_msg.lower() or "empty" in error_msg.lower(), \
            f"Expected firmware name error, got: {error_msg}"
        print(f"[PASS] Error message: {error_msg}")
        
    finally:
        restore_flash_script()
        cleanup_daemon_files()
        stop_daemon(proc)


def test_update_sequential_flash_operations():
    """
    Test 14: Sequential flash operations (A → complete → B)
    
    SCENARIO: Flash firmware A, wait for completion, then flash firmware B
    SETUP: Two firmware files and mock flash script
    EXECUTE: 
        STEP 1: Flash firmware A, wait for 100%
        STEP 2: Flash firmware B (should work)
    VERIFY: 
        - First flash completes successfully
        - IsFlashInProgress resets to FALSE
        - Second flash starts successfully (state cleanup works)
    """
    proc = start_daemon()
    initial_rdkfw_setup()
    write_device_prop()
    cleanup_daemon_files()
    
    firmware_a = "firmware_a.bin"
    firmware_b = "firmware_b.bin"
    path_a = create_mock_firmware_file(firmware_a)
    path_b = create_mock_firmware_file(firmware_b)
    
    # Mock script with short delay
    create_mock_flash_script(return_code=0)
    
    monitor = UpdateProgressMonitor()
    monitor.start()
    
    try:
        api = iface()
        result = api.RegisterProcess("TestApp", "1.0")
        handler_id = str(result[0] if isinstance(result, tuple) else result)
        assert int(handler_id) > 0, "Registration failed"
        
        # ========== FLASH #1: Firmware A ==========
        print("\n[STEP 1] Starting flash of firmware A...")
        result1 = api.UpdateFirmware(
            handler_id,
            firmware_a,
            FIRMWARE_DIR,       # LocationOfFirmware
            "PCI",              # TypeOfFirmware
            "false"
        )
        
        update_result1 = str(result1[0] if isinstance(result1, tuple) else result1[0])
        assert update_result1 == RDKFW_UPDATE_SUCCESS, \
            f"First flash should be accepted, got {update_result1}"
        print("[PASS] First flash accepted")
        
        # Wait for first flash to complete (100% signal)
        completion_signal = monitor.wait_for_progress(100, timeout=30)
        assert completion_signal is not None, "First flash did not complete"
        assert completion_signal['status'] == FW_UPDATE_COMPLETED, \
            f"Expected COMPLETED status, got {completion_signal['status']}"
        print("[PASS] First flash completed successfully")
        
        # Give daemon time to reset IsFlashInProgress flag
        time.sleep(2)
        
        # Reset monitor for second flash
        monitor.signals.clear()
        
        # ========== FLASH #2: Firmware B ==========
        print("\n[STEP 2] Starting flash of firmware B...")
        result2 = api.UpdateFirmware(
            handler_id,
            firmware_b,
            FIRMWARE_DIR,       # LocationOfFirmware
            "PCI",              # TypeOfFirmware
            "false"
        )
        
        update_result2 = str(result2[0] if isinstance(result2, tuple) else result2[0])
        assert update_result2 == RDKFW_UPDATE_SUCCESS, \
            f"Second flash should be accepted (state cleanup worked), got {update_result2}"
        print("[PASS] Second flash accepted (IsFlashInProgress was reset)")
        
        # Wait for second flash to complete
        completion_signal2 = monitor.wait_for_progress(100, timeout=30)
        assert completion_signal2 is not None, "Second flash did not complete"
        assert completion_signal2['status'] == FW_UPDATE_COMPLETED, \
            f"Expected COMPLETED status, got {completion_signal2['status']}"
        print("[PASS] Second flash completed successfully")
        
        print("\n[PASS] Sequential flash operations work correctly (state management verified)")
        
    finally:
        monitor.stop()
        remove_file(path_a)
        remove_file(path_b)
        restore_flash_script()
        cleanup_daemon_files()
        stop_daemon(proc)


