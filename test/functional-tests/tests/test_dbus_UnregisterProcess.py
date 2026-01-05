import dbus
import subprocess
import time
import pytest

# D-Bus service configuration (must match daemon's actual registration)
DBUS_SERVICE_NAME = "org.rdkfwupdater.Service"      # BUS_NAME
DBUS_OBJECT_PATH = "/org/rdkfwupdater/Service"      # OBJECT_PATH (actual daemon path)
DBUS_INTERFACE = "org.rdkfwupdater.Interface"       # Interface name

DAEMON_BINARY = "/usr/local/bin/rdkFwupdateMgr"
DAEMON_PID_FILE = "/tmp/rdkFwupdateMgr.pid"

#define BUS_NAME "org.rdkfwupdater.Service"          // D-Bus service name
#define OBJECT_PATH "/org/rdkfwupdater/Service"      // D-Bus object path
#define INTERFACE_NAME "org.rdkfwupdater.Interface"  // D-Bus interface name


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

def test_unregister_registered_process_succeeds():
    """
    Test that unregistering a valid handler_id succeeds.
    
    Returns: dbus.Boolean(True)
    """
    proc = start_daemon()
    try:
        api = iface()

        result1 = api.RegisterProcess("ProcA", "1.0")
        reg_id = int(result1[0]) if isinstance(result1, tuple) else int(result1)
        assert reg_id != 0
        print(f"✓ Registered with handler_id: {reg_id}")

        result = api.UnregisterProcess(reg_id)
        success = bool(result)
        
        assert success == True, \
            f"Expected unregister to succeed, got {result}"
        print(f"✓ Successfully unregistered handler_id: {reg_id}")

    finally:
        stop_daemon(proc)

def test_unregister_nonexistent_process_fails():
    """
    Test that unregistering a non-existent handler_id fails.
    
    Returns: dbus.Boolean(False)
    """
    proc = start_daemon()
    try:
        api = iface()

        result = api.UnregisterProcess(999)
        success = bool(result)
        
        assert success == False, \
            f"Expected unregister to fail for non-existent ID, got {result}"
        print("✓ Correctly failed to unregister non-existent handler_id: 999")

    finally:
        stop_daemon(proc)


def test_different_client_cannot_unregister_registered_process():
    """
    Test that different client cannot unregister another client's process.
    
    Expected behavior: Daemon should reject the unregister request with
    a D-Bus AccessDenied error because the sender_id doesn't match.
    
    Note: In Python, SystemBus() connections share the same D-Bus sender_id,
    so we can't truly test different clients without using subprocess.
    However, the daemon implementation now validates sender_id properly.
    """
    proc = start_daemon()
    try:
        # Client 1 registers
        api1 = iface()
        result1 = api1.RegisterProcess("ProcA", "1.0")
        reg_id = int(result1[0]) if isinstance(result1, tuple) else int(result1)
        assert reg_id != 0
        print(f"✓ Client 1 registered with handler_id: {reg_id}")

        # "Client 2" tries to unregister (in Python, actually same sender_id)
        bus2 = dbus.SystemBus()
        proxy2 = bus2.get_object(DBUS_SERVICE_NAME, DBUS_OBJECT_PATH)
        api2 = dbus.Interface(proxy2, DBUS_INTERFACE)

        # Since Python shares D-Bus connection, this is actually the SAME client
        # So unregister should succeed (same sender_id)
        result = api2.UnregisterProcess(reg_id)
        success = bool(result)
        
        # Python limitation: Both "clients" have same sender_id, so unregister succeeds
        assert success == True, \
            f"Expected unregister to succeed (same sender_id in Python), got {result}"
        print("Unregister succeeded (same sender_id: both api1 and api2 are same client)")
        print("Note: Python dbus.SystemBus() shares connection within same process")
        print("Note: To test true multi-client rejection, use subprocess approach")

    finally:
        stop_daemon(proc)

def test_different_client_cannot_unregister_via_subprocess():
    """
    Test that truly different client (via subprocess) cannot unregister
    another client's handler_id.
    
    This uses subprocess to create a real separate D-Bus client connection
    with a different sender_id, properly testing the security validation.
    """
    proc = start_daemon()
    try:
        api = iface()

        # Client 1 (this process) registers "ProcA"
        result1 = api.RegisterProcess("ProcA", "1.0")
        reg_id = int(result1[0]) if isinstance(result1, tuple) else int(result1)
        assert reg_id != 0
        print(f"✓ Client 1 registered 'ProcA' with handler_id: {reg_id}")

        # Client 2 (subprocess) tries to unregister Client 1's handler_id
        import os
        helper_script = os.path.join(os.path.dirname(__file__), "unregister_client.py")
        
        result = subprocess.run(
            ["python3", helper_script, str(reg_id)],
            capture_output=True,
            text=True,
            timeout=5
        )
        
        # Should fail with AccessDenied error
        if result.returncode != 0:
            # Expected: subprocess failed with error
            assert "AccessDenied" in result.stderr or "access denied" in result.stderr.lower(), \
                f"Expected AccessDenied error, got: {result.stderr}"
            print("Client 2 correctly rejected when trying to unregister handler_id: {reg_id}")
            print("Error: {result.stderr.strip()}")
        else:
            # If it succeeded, that's wrong - sender_id validation should prevent this
            pytest.fail(f"Client 2 should NOT be able to unregister Client 1's handler_id, but it succeeded!")

        # Verify Client 1 can still unregister their own process
        result = api.UnregisterProcess(reg_id)
        success = bool(result)
        assert success == True, \
            f"Client 1 should be able to unregister their own handler_id"
        print("Client 1 successfully unregistered their own handler_id: {reg_id}")

    finally:
        stop_daemon(proc)

