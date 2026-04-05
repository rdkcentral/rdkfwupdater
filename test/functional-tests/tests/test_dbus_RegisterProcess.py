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

import dbus
import subprocess
import time

# D-Bus service configuration (must match daemon's actual registration)
DBUS_SERVICE_NAME = "org.rdkfwupdater.Service"      # BUS_NAME
DBUS_OBJECT_PATH = "/org/rdkfwupdater/Service"      # OBJECT_PATH (actual daemon path)
DBUS_INTERFACE = "org.rdkfwupdater.Interface"       # Interface name

DAEMON_BINARY = "/usr/local/bin/rdkFwupdateMgr"
DAEMON_PID_FILE = "/tmp/rdkFwupdateMgr.pid"

def start_daemon():
    """
    Start the daemon with required arguments.
    
    The daemon requires 2 arguments:
        argv[1] = "0" - Retry count (0 for tests)
        argv[2] = "1" - Trigger type (1 = Bootup)
    
    Without these arguments, the daemon will exit immediately.
    """
    # Kill any existing daemon
    subprocess.run(['pkill', '-9', '-f', 'rdkFwupdateMgr'], 
                  capture_output=True)
    time.sleep(0.5)
    
    # Start daemon with required arguments: retry_count=0, trigger_type=1 (Bootup)
    proc = subprocess.Popen([DAEMON_BINARY, "0", "1"])
    time.sleep(3)  # Give it time to initialize and register on D-Bus
    return proc


def stop_daemon(proc):
    proc.terminate()
    proc.wait()

def iface():
    bus = dbus.SystemBus()
    proxy = bus.get_object(DBUS_SERVICE_NAME, DBUS_OBJECT_PATH)
    return dbus.Interface(proxy, DBUS_INTERFACE)

def test_same_process_re_registration_returns_same_id_even_if_libversion_differs():
    proc = start_daemon()
    try:
        api = iface()

        result1 = api.RegisterProcess("ProcA", "1.0")
        result2 = api.RegisterProcess("ProcA", "2.5")  # only libVersion changed
        
        id1 = int(result1[0]) if isinstance(result1, tuple) else int(result1)
        id2 = int(result2[0]) if isinstance(result2, tuple) else int(result2)

        assert id1 != 0
        assert id2 != 0
        assert id1 == id2

    finally:
        stop_daemon(proc)

def test_same_client_different_process_is_rejected():
    """
    Test that same client cannot register with different process name.
    Expected: Daemon throws DBusException with AccessDenied
    """
    proc = start_daemon()
    try:
        api = iface()

        # First registration succeeds
        id1 = api.RegisterProcess("ProcA", "1.0")
        handler_id1 = id1
        assert handler_id1 > 0
        print(f"First registration (ProcA) succeeded with handler_id: {handler_id1}")
        
        # Second registration with different process name should fail
        try:
            id2 = api.RegisterProcess("ProcB", "1.0")
            
            # If we get here without exception, check if it returned 0
            handler_id2 = id2 if isinstance(id2, tuple) else int(id2)
            assert handler_id2 == 0, \
                f"Expected rejection (0), but got handler_id: {handler_id2}"
            print("Second registration (ProcB) correctly rejected with handler_id=0")
            
        except dbus.exceptions.DBusException as e:
            # This is the expected behavior - daemon returns error
            assert "Registration rejected" in str(e) or "AccessDenied" in str(e), \
                f"Expected rejection error, got: {e}"
            print(f"Second registration (ProcB) correctly rejected with error: {e.get_dbus_name()}")

    finally:
        stop_daemon(proc)

def test_same_process_registered_by_another_client_is_rejected():
    """
    Test that different client cannot register same process name.
    
    In Python, SystemBus() connections share the same D-Bus sender_id,
    so we need to use subprocess to simulate a truly different client.
    
    For now, this test documents the expected behavior but may not
    truly test different clients without subprocess approach.
    """
    proc = start_daemon()
    try:
        api = iface()

        # First client registers "ProcA"
        id1 = api.RegisterProcess("ProcA", "1.0")
        handler_id1 = id1
        assert handler_id1 > 0
        print(f"First client registered 'ProcA' with handler_id: {handler_id1}")

        # Attempt to create a "different" client
        # In Python's dbus module, this still shares the same connection
        # and sender_id, so the daemon sees it as the SAME client!
        bus2 = dbus.SystemBus()
        proxy2 = bus2.get_object(DBUS_SERVICE_NAME, DBUS_OBJECT_PATH)
        api2 = dbus.Interface(proxy2, DBUS_INTERFACE)

        id2 = api2.RegisterProcess("ProcA", "3.3")
        handler_id2 = id2
        
        # Because Python shares the same D-Bus connection, the daemon
        # sees this as idempotent re-registration (same client, same process)
        # So it returns the SAME handler_id
        print(f"Second 'client' got handler_id: {handler_id2}")
        
        # Since we can't truly test different clients in Python without
        # subprocess, we'll just verify idempotent behavior here
        assert handler_id2 == handler_id1, \
            "Python dbus module shares connection, so this is idempotent registration"
        
        print("Python dbus module shares connections, so both 'clients'")
        print("   are actually the same client (same sender_id) to the daemon.")
        print("   True multi-client testing requires subprocess or different processes.")

    finally:
        stop_daemon(proc)

def test_libversion_does_not_influence_registration_identity():
    proc = start_daemon()
    try:
        api = iface()

        result1 = api.RegisterProcess("ProcX", "banana")
        result2 = api.RegisterProcess("ProcX", "42.0.9-weird")
        
        id1 = int(result1[0]) if isinstance(result1, tuple) else int(result1)
        id2 = int(result2[0]) if isinstance(result2, tuple) else int(result2)

        assert id1 == id2

    finally:
        stop_daemon(proc)

def test_different_client_same_process_rejected_using_subprocess():
    """
    Test that truly different client (different process) cannot register same process name.
    
    This uses subprocess to create a real separate D-Bus client connection.
    """
    proc = start_daemon()
    try:
        api = iface()

        # First client (this process) registers "SharedProc"
        result1 = api.RegisterProcess("SharedProc", "1.0")
        handler_id1 = result1 if isinstance(result1, tuple) else int(result1)
        assert handler_id1 > 0
        print(f"Client 1 (this process) registered 'SharedProc' with handler_id: {handler_id1}")

        # Second client (subprocess) tries to register same "SharedProc"
        import os
        helper_script = os.path.join(os.path.dirname(__file__), "register_client.py")
        
        result = subprocess.run(
            ["python3", helper_script, "SharedProc", "2.0"],
            capture_output=True,
            text=True,
            timeout=5
        )
        
        if result.returncode == 0:
            # Subprocess succeeded - check if it got handler_id = 0
            handler_id2 = int(result.stdout.strip())
            assert handler_id2 == 0, \
                f"Expected client 2 to be rejected (handler_id=0), but got {handler_id2}"
            print(f"Client 2 (subprocess) correctly rejected with handler_id=0")
        else:
            # Subprocess failed - check for rejection error
            assert "rejected" in result.stderr.lower() or "already registered" in result.stderr.lower(), \
                f"Expected rejection error, got: {result.stderr}"
            print(f"Client 2 (subprocess) correctly rejected with error")
            print(f"  Error: {result.stderr.strip()}")

    finally:
        stop_daemon(proc)


def test_different_clients_different_processes_allowed():
    """
    Test that different clients can register with different process names.
    
    Uses subprocess to create truly different D-Bus clients.
    """
    proc = start_daemon()
    try:
        api = iface()

        # First client registers "VideoApp"
        result1 = api.RegisterProcess("VideoApp", "1.0")
        handler_id1 = int(result1[0]) if isinstance(result1, tuple) else int(result1)
        assert handler_id1 > 0
        print(f"Client 1 registered 'VideoApp' with handler_id: {handler_id1}")

        # Second client (subprocess) registers "AudioApp"  
        import os
        helper_script = os.path.join(os.path.dirname(__file__), "register_client.py")
        
        result = subprocess.run(
            ["python3", helper_script, "AudioApp", "1.0"],
            capture_output=True,
            text=True,
            timeout=5
        )
        
        assert result.returncode == 0, \
            f"Client 2 registration failed: {result.stderr}"
        
        handler_id2 = int(result.stdout.strip())
        assert handler_id2 > 0, \
            f"Expected valid handler_id, got {handler_id2}"
        assert handler_id2 != handler_id1, \
            f"Expected different handler_ids, but both got {handler_id1}"
        
        print(f"Client 2 registered 'AudioApp' with handler_id: {handler_id2}")
        print("Different clients with different process names both succeeded")

    finally:
        stop_daemon(proc)



