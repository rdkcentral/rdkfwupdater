# Copyright 2026 RDK Management
# Licensed under the Apache License, Version 2.0
# SPDX-License-Identifier: Apache-2.0
"""
PKCS#11 Certificate Selector Fallback Test

Validates rdkfwupdater falls back to client.p12/client.pem when
reference.p12 (PKCS#11 cert) is unavailable.
"""

import pytest
import subprocess
import os
import time
import json
import dbus
from pathlib import Path
from rdkfw_test_helper import remove_file, write_on_file, initial_rdkfw_setup

# Constants
DBUS_SERVICE_NAME = "org.rdkfwupdater.Service"
DBUS_OBJECT_PATH = "/org/rdkfwupdater/Service"
DBUS_INTERFACE = "org.rdkfwupdater.Interface"
DAEMON_BINARY = "/usr/local/bin/rdkFwupdateMgr"
CLEANUP_FILES = [
    "/tmp/dnldmgr_status.txt",
    "/opt/curl_progress", 
    "/tmp/xconf_response_thunder.txt",
    "/tmp/fw_preparing_to_reboot",
    "/tmp/currently_running_image_name",
    "/opt/cdl_flashed_file_name"
]


def write_config_files():
    """Create device.properties and RFC configuration"""
    # Device properties
    with open("/etc/device.properties", "w") as f:
        f.write("DEVICE_NAME=DEV_CONTAINER\nDEVICE_TYPE=mediaclient\nDIFW_PATH=/opt/CDL\n"
                "MODEL_NUM=ABCD\nBUILD_TYPE=VBN\nESTB_INTERFACE=eth0\nPDRI_ENABLED=true\n")
    
    # RFC configuration (valid JSON)
    os.makedirs("/tmp/.RFC", exist_ok=True)
    with open("/tmp/.RFC/.RFC_FWUpdate", "w") as f:
        json.dump({"Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.FWUpdate.Enable": {"value": "true"}}, f)


def cleanup_daemon_files():
    """Clean daemon-specific files"""
    for f in CLEANUP_FILES:
        remove_file(f)


def start_daemon_process():
    """Start D-Bus daemon"""
    subprocess.run(['pkill', '-9', '-f', 'rdkFwupdateMgr'], capture_output=True)
    time.sleep(0.5)
    proc = subprocess.Popen([DAEMON_BINARY, "0", "1"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(3)
    if proc.poll() is not None:
        raise RuntimeError(f"Daemon failed to start (exit code: {proc.returncode})")
    return proc


def stop_daemon_process(proc=None):
    """Stop D-Bus daemon"""
    if proc:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
    subprocess.run(['pkill', '-9', '-f', 'rdkFwupdateMgr'], capture_output=True)
    time.sleep(0.5)


@pytest.fixture(scope="module")
def backup_reference_cert():
    """Backup and remove reference.p12 for fallback testing"""
    ref_cert, backup = "/opt/certs/reference.p12", "/opt/certs/reference.p12.backup"
    
    if not os.path.exists(ref_cert):
        pytest.skip("reference.p12 not found - PKCS#11 mode not enabled")
    
    subprocess.run(['cp', ref_cert, backup], check=True, capture_output=True)
    os.remove(ref_cert)
    
    yield
    
    if os.path.exists(backup):
        subprocess.run(['mv', backup, ref_cert], check=True, capture_output=True)


def test_certsel_fallback_verification(backup_reference_cert):
    """Verify certselector environment is set up for fallback"""
    assert not os.path.exists("/opt/certs/reference.p12")
    assert os.path.exists("/opt/certs/client.p12") or os.path.exists("/opt/certs/client.pem"), \
        "Fallback certificate must exist"


def test_fallback_firmware_download(backup_reference_cert):
    """Test firmware download works with fallback to client.p12/client.pem"""
    write_config_files()
    cleanup_daemon_files()
    daemon_proc = start_daemon_process()
    
    try:
        bus = dbus.SystemBus()
        fw_interface = dbus.Interface(
            bus.get_object(DBUS_SERVICE_NAME, DBUS_OBJECT_PATH), 
            DBUS_INTERFACE
        )
        
        result = fw_interface.RegisterProcess("FallbackTest", "1.0")
        handler_id = str(result[0] if isinstance(result, tuple) else result)
        assert int(handler_id) > 0
        
        fw_interface.CheckForUpdate(handler_id)
        time.sleep(3)
        
        # Verify daemon still running (no cert crash)
        assert subprocess.run(['pgrep', '-f', 'rdkFwupdateMgr'], capture_output=True).returncode == 0
        
    finally:
        stop_daemon_process(daemon_proc)
        cleanup_daemon_files()




def test_fallback_rdkvfwupgrader_direct(backup_reference_cert):
    """Test rdkvfwupgrader binary directly with fallback certificates"""
    # Ensure any lingering daemon from previous test is fully gone
    # (previously test_verify_no_pkcs11_patch_activation provided natural delay)
    subprocess.run(['pkill', '-9', '-f', 'rdkFwupdateMgr'], capture_output=True)
    subprocess.run(['pkill', '-9', '-f', 'rdkvfwupgrader'], capture_output=True)
    time.sleep(5)  # Allow daemon and its curl connections to fully release

    initial_rdkfw_setup()
    write_config_files()
    
    for f in ["/tmp/pdri_image_file", "/tmp/.xconfssrdownloadurl"]:
        remove_file(f)
    
    Path("/tmp/pdri_image_file").touch()
    write_on_file("/tmp/pdri_image_file", "ABCD_PDRI_img")
    
    result = subprocess.run(['rdkvfwupgrader', '0', '1'], 
                           capture_output=True, timeout=60)
    
    stderr_text = result.stderr.decode('utf-8', errors='ignore')
    assert ('client.p12' in stderr_text or 'client.pem' in stderr_text or 
            'rdkcertselector_getCert:returning' in stderr_text)
    assert result.returncode == 0
    assert os.path.exists("/tmp/.xconfssrdownloadurl")
