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
        print(f"✓ Unregister succeeded (same sender_id: both api1 and api2 are same client)")
        print("ℹ️  Note: Python dbus.SystemBus() shares connection within same process")
        print("ℹ️  Note: To test true multi-client rejection, use subprocess approach")

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
            print(f"✓ Client 2 correctly rejected when trying to unregister handler_id: {reg_id}")
            print(f"   Error: {result.stderr.strip()}")
        else:
            # If it succeeded, that's wrong - sender_id validation should prevent this
            pytest.fail(f"Client 2 should NOT be able to unregister Client 1's handler_id, but it succeeded!")

        # Verify Client 1 can still unregister their own process
        result = api.UnregisterProcess(reg_id)
        success = bool(result)
        assert success == True, \
            f"Client 1 should be able to unregister their own handler_id"
        print(f"✓ Client 1 successfully unregistered their own handler_id: {reg_id}")

    finally:
        stop_daemon(proc)

def test_process_can_be_reregistered_after_unregistration():
    """
    Test that a process name can be reused after unregistration.
    
    This validates:
    1. Unregister properly cleans up the registration
    2. Process name becomes available for re-registration
    3. Re-registration gets a new handler_id (not the old one)
    """
    proc = start_daemon()
    try:
        api = iface()

        # Register first time
        result1 = api.RegisterProcess("ProcA", "1.0")
        id1 = int(result1[0]) if isinstance(result1, tuple) else int(result1)
        assert id1 > 0
        print(f"✓ First registration: handler_id = {id1}")

        # Unregister
        res = api.UnregisterProcess(id1)
        assert bool(res) == True, "Unregister should succeed"
        print(f"✓ Unregistered handler_id = {id1}")

        # Register again with same process name
        result2 = api.RegisterProcess("ProcA", "1.0")
        id2 = int(result2[0]) if isinstance(result2, tuple) else int(result2)
        assert id2 > 0
        print(f"✓ Second registration: handler_id = {id2}")
        
        # Verify it's a NEW handler_id (cleanup was complete)
        assert id2 != id1, \
            f"Re-registration should get new handler_id, but got same: {id1}"
        print(f"✓ Process name 'ProcA' successfully reused with new handler_id")

    finally:
        stop_daemon(proc)

def test_double_unregister_returns_false():
    """
    Test that unregistering the same handler_id twice fails on second attempt.
    
    This validates that:
    1. Cleanup properly removes the handler_id from tracking
    2. No double-free or memory corruption occurs
    3. Idempotency: subsequent calls return FALSE (not found)
    """
    proc = start_daemon()
    try:
        api = iface()
        
        # Register a process
        result1 = api.RegisterProcess("ProcA", "1.0")
        reg_id = int(result1[0]) if isinstance(result1, tuple) else int(result1)
        assert reg_id > 0
        print(f"✓ Registered with handler_id: {reg_id}")
        
        # First unregister - should succeed
        result_first = api.UnregisterProcess(reg_id)
        success_first = bool(result_first)
        assert success_first == True, \
            f"First unregister should succeed, got {result_first}"
        print(f"✓ First unregister succeeded")
        
        # Second unregister - should fail (already removed)
        result_second = api.UnregisterProcess(reg_id)
        success_second = bool(result_second)
        assert success_second == False, \
            f"Second unregister should fail (not found), got {result_second}"
        print(f"✓ Second unregister correctly returned FALSE (already removed)")

    finally:
        stop_daemon(proc)


def test_unregister_one_of_multiple_processes():
    """
    Test that multiple clients can register, and each can unregister independently.
    
    This validates tracking system integrity:
    - Multiple CLIENTS can register simultaneously with different process names
    - Each client can only unregister their OWN registration
    - Other clients remain registered after one unregisters
    
    Note: Uses long-lived subprocesses to maintain consistent sender_id for each client
    throughout the register/unregister lifecycle.
    """
    proc = start_daemon()
    client2_proc = None
    client3_proc = None
    
    try:
        import os
        helper_script = os.path.join(os.path.dirname(__file__), "register_and_unregister_client.py")
        
        # Client 1 (this process) registers VideoApp
        api = iface()
        result1 = api.RegisterProcess("VideoApp", "1.0")
        id1 = int(result1[0]) if isinstance(result1, tuple) else int(result1)
        print(f"✓ Client 1 registered VideoApp with handler_id: {id1}")
        
        # Client 2 (long-lived subprocess) registers AudioApp
        client2_proc = subprocess.Popen(
            ["python3", helper_script, "AudioApp", "2.0"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, bufsize=1
        )
        # Read registration confirmation
        line2 = client2_proc.stdout.readline().strip()
        assert line2.startswith("REGISTERED:"), f"Client 2 registration failed: {line2}"
        id2 = int(line2.split(":")[1])
        print(f"✓ Client 2 registered AudioApp with handler_id: {id2}")
        
        # Client 3 (long-lived subprocess) registers NetworkApp
        client3_proc = subprocess.Popen(
            ["python3", helper_script, "NetworkApp", "3.0"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, bufsize=1
        )
        # Read registration confirmation
        line3 = client3_proc.stdout.readline().strip()
        assert line3.startswith("REGISTERED:"), f"Client 3 registration failed: {line3}"
        id3 = int(line3.split(":")[1])
        print(f"✓ Client 3 registered NetworkApp with handler_id: {id3}")
        
        assert id1 != id2 != id3, "All handler_ids should be unique"
        print(f"✓ All handler_ids are unique: {id1}, {id2}, {id3}")
        
        # Client 1 (this process) unregisters its own VideoApp
        result = api.UnregisterProcess(id1)
        assert bool(result) == True, f"Client 1 unregister should succeed"
        print(f"✓ Client 1 unregistered VideoApp (handler_id: {id1})")
        
        # Verify Client 1 can re-register (proves it was cleaned up)
        result1_new = api.RegisterProcess("VideoApp", "1.0")
        id1_new = int(result1_new[0]) if isinstance(result1_new, tuple) else int(result1_new)
        assert id1_new != id1, \
            f"Re-registration should get new handler_id, got {id1_new} (old was {id1})"
        print(f"✓ Client 1 re-registered with new handler_id: {id1_new}")
        
        # Verify Client 1 CANNOT unregister Client 2's handler_id (security check)
        with pytest.raises(Exception) as exc_info:
            api.UnregisterProcess(id2)
        error_msg = str(exc_info.value).lower()
        assert "accessdenied" in error_msg or "denied" in error_msg, \
            f"Expected AccessDenied error, got: {exc_info.value}"
        print(f"✓ Security verified: Client 1 cannot unregister Client 2's handler_id {id2}")
        
        # Client 2 unregisters its own AudioApp (signal subprocess to unregister)
        client2_proc.stdin.write("\n")
        client2_proc.stdin.flush()
        line2_unreg = client2_proc.stdout.readline().strip()
        client2_proc.wait(timeout=5)
        assert line2_unreg == "UNREGISTERED:SUCCESS", \
            f"Client 2 unregister failed: {line2_unreg}"
        print(f"✓ Client 2 successfully unregistered AudioApp (handler_id: {id2})")
        
        # Client 3 unregisters its own NetworkApp
        client3_proc.stdin.write("\n")
        client3_proc.stdin.flush()
        line3_unreg = client3_proc.stdout.readline().strip()
        client3_proc.wait(timeout=5)
        assert line3_unreg == "UNREGISTERED:SUCCESS", \
            f"Client 3 unregister failed: {line3_unreg}"
        print(f"✓ Client 3 successfully unregistered NetworkApp (handler_id: {id3})")
        
        # Clean up Client 1's new registration
        api.UnregisterProcess(id1_new)
        print("✓ Tracking system integrity verified: Multiple clients work independently")

    finally:
        # Clean up subprocesses
        if client2_proc and client2_proc.poll() is None:
            client2_proc.terminate()
            client2_proc.wait(timeout=2)
        if client3_proc and client3_proc.poll() is None:
            client3_proc.terminate()
            client3_proc.wait(timeout=2)
        stop_daemon(proc)


def test_unregister_with_invalid_handler_ids():
    """
    Test UnregisterProcess with invalid/boundary handler_id values.
    
    This validates robustness against:
    - Invalid handler_id (0)
    - Very large numbers (boundary testing)
    - Non-existent handler_ids
    
    Ensures no crashes, proper error handling.
    """
    proc = start_daemon()
    try:
        api = iface()
        
        # Test 1: handler_id = 0 (invalid according to daemon code)
        try:
            result = api.UnregisterProcess(0)
            pytest.fail("UnregisterProcess(0) should raise DBusException for invalid args")
        except dbus.exceptions.DBusException as e:
            assert "Invalid" in str(e) or "invalid" in str(e).lower(), \
                f"Expected 'Invalid' error for handler_id=0, got: {e}"
            print("✓ handler_id=0 correctly rejected with error")
        
        # Test 2: Very large uint64 value (boundary test)
        max_uint64 = 18446744073709551615  # 2^64 - 1
        result = api.UnregisterProcess(max_uint64)
        success = bool(result)
        assert success == False, \
            f"Max uint64 should return FALSE (not found), got {result}"
        print(f"✓ Max uint64 ({max_uint64}) handled correctly: returned FALSE")
        
        # Test 3: Large non-existent handler_id
        result = api.UnregisterProcess(999999999)
        success = bool(result)
        assert success == False, \
            f"Large handler_id should return FALSE (not found), got {result}"
        print("✓ Large non-existent handler_id (999999999) returned FALSE")
        
        # Test 4: After registering and unregistering, same ID should fail
        reg_result = api.RegisterProcess("BoundaryTest", "1.0")
        reg_id = int(reg_result[0]) if isinstance(reg_result, tuple) else int(reg_result)
        api.UnregisterProcess(reg_id)
        
        # Try to unregister again - should fail
        result = api.UnregisterProcess(reg_id)
        success = bool(result)
        assert success == False, \
            f"Already unregistered handler_id should return FALSE, got {result}"
        print(f"✓ Already unregistered handler_id ({reg_id}) returned FALSE")
        
        print("✓ All boundary/invalid handler_id tests passed")

    finally:
        stop_daemon(proc)
