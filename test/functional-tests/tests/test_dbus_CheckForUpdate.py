import dbus
import subprocess
import time
import pytest
import os
# D-Bus service configuration (must match daemon's actual registration)
DBUS_SERVICE_NAME = "org.rdkfwupdater.Service"      # BUS_NAME
DBUS_OBJECT_PATH = "/org/rdkfwupdater/Service"      # OBJECT_PATH (actual daemon path)
DBUS_INTERFACE = "org.rdkfwupdater.Interface"       # Interface name

DAEMON_BINARY = "/usr/local/bin/rdkFwupdateMgr"
DAEMON_PID_FILE = "/tmp/rdkFwupdateMgr.pid"

XCONF_CACHE_FILE = "/tmp/xconf_response_thunder.txt"
XCONF_HTTP_CODE_FILE = "/tmp/xconf_httpcode_thunder.txt"

#define BUS_NAME "org.rdkfwupdater.Service"          // D-Bus service name
#define OBJECT_PATH "/org/rdkfwupdater/Service"      // D-Bus object path
#define INTERFACE_NAME "org.rdkfwupdater.Interface"  // D-Bus interface name


def safe_dbus_call(api_method, *args):
    """
    Safely call a D-Bus method and return (success, result_or_error_message).
    
    This function isolates D-Bus exceptions from pytest's exception inspection
    machinery, which can cause segfaults when trying to format DBusException objects.
    
    Returns:
        tuple: (success: bool, data: any)
               - If success=True, data is the method return value
               - If success=False, data is the error message string
    """
    try:
        result = api_method(*args)
        return (True, result)
    except dbus.exceptions.DBusException as e:
        # Extract error info and immediately discard exception object
        error_msg = str(e)
        error_name = e.get_dbus_name() if hasattr(e, 'get_dbus_name') else 'DBusException'
        e = None  # Help GC
        return (False, f"{error_name}: {error_msg}")
    except Exception as e:
        # Catch any other exceptions
        error_msg = f"{type(e).__name__}: {str(e)}"
        e = None  # Help GC
        return (False, error_msg)


def remove_cache_files():
    for f in (XCONF_CACHE_FILE, XCONF_HTTP_CODE_FILE):
        try:
            os.remove(f)
        except FileNotFoundError:
            pass


def cache_exists():
    return os.path.exists(XCONF_CACHE_FILE) and os.path.exists(XCONF_HTTP_CODE_FILE)


def grep_log_file(logfile_path, search_pattern):
    """
    Search for a pattern in a log file.
    
    Args:
        logfile_path: Path to log file
        search_pattern: String to search for (case-insensitive substring match)
    
    Returns:
        bool: True if pattern found, False otherwise
    """
    try:
        if not os.path.exists(logfile_path):
            print(f"WARNING: Log file does not exist: {logfile_path}")
            return False
            
        with open(logfile_path, 'r', encoding='utf-8', errors='ignore') as f:
            content = f.read()
            found = search_pattern.lower() in content.lower()
            if found:
                print(f"Found pattern '{search_pattern}' in {logfile_path}")
            else:
                print(f"Pattern '{search_pattern}' NOT found in {logfile_path}")
            return found
    except Exception as e:
        print(f"ERROR reading log file {logfile_path}: {e}")
        return False


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

def test_check_for_update_fails_if_not_registered():
    """
    Test that CheckForUpdate returns error for unregistered handler_id.
    
    Expected Response: (issssi)
      result=0 (CHECK_FOR_UPDATE_SUCCESS - D-Bus call succeeded)
      status_code=3 (FIRMWARE_CHECK_ERROR - handler not registered)
      status_message="Handler not registered. Call RegisterProcess first."
    
    Note: The daemon does NOT raise D-Bus exceptions. It returns a successful
    D-Bus response with error details encoded in the tuple.
    """
    print("\n=== Test: CheckForUpdate with unregistered handler_id ===")
    proc = start_daemon()
    
    try:
        api = iface()

        # Call CheckForUpdate with invalid handler_id (999)
        response = api.CheckForUpdate(str(999))
        
        # Response signature: (issssi)
        # (result, current_version, available_version, update_details, status_message, status_code)
        assert response is not None, "CheckForUpdate should return a response"
        assert isinstance(response, (tuple, list)), f"Expected tuple, got {type(response)}"
        assert len(response) == 6, f"Expected 6 elements, got {len(response)}: {response}"
        
        # Extract fields
        result = int(response[0])                    # API call result (0=success, 1=fail)
        current_version = str(response[1])           # Current firmware version
        available_version = str(response[2])         # Available firmware version
        update_details = str(response[3])            # Update details
        status_message = str(response[4])            # Status/error message
        status_code = int(response[5])               # Status code (0-5)
        
        # Verify response values
        assert result == 0, f"Expected result=0 (SUCCESS), got: {result}"
        assert status_code == 3, f"Expected status_code=3 (FIRMWARE_CHECK_ERROR), got: {status_code}"
        assert "not registered" in status_message.lower(), \
            f"Expected 'not registered' in message, got: '{status_message}'"
        
        print(f"TEST PASSED: CheckForUpdate correctly rejected unregistered handler_id")
        print(f"   Result: {result} (CHECK_FOR_UPDATE_SUCCESS)")
        print(f"   Status Code: {status_code} (FIRMWARE_CHECK_ERROR)")
        print(f"   Message: '{status_message}'")

    finally:
        stop_daemon(proc)

def test_check_for_update_succeeds_after_registration():
    """
    Test that CheckForUpdate returns firmware status after proper registration.
    
    Expected Response: (issssi)
      result=0 (CHECK_FOR_UPDATE_SUCCESS - D-Bus call succeeded)
      status_code=0,1,2,3,4,5 (Various firmware statuses)
      status_message=Descriptive message about firmware status
    
    Common status codes:
      0 = FIRMWARE_AVAILABLE (update available)
      1 = FIRMWARE_NOT_AVAILABLE (already on latest)
      3 = FIRMWARE_CHECK_ERROR (checking in progress/error)
    """
    print("\n=== Test: CheckForUpdate after registration ===")
    proc = start_daemon()
    try:
        api = iface()

        # Register process first
        result = api.RegisterProcess("ProcA", "1.0")
        handler_id = result if isinstance(result, tuple) else int(result)
        assert handler_id > 0, f"Registration should return valid handler_id, got {handler_id}"
        print(f"Registered with handler_id: {handler_id}")

        # Call CheckForUpdate with the registered handler_id
        response = api.CheckForUpdate(str(handler_id))
        
        # Response signature: (issssi)
        assert response is not None, "CheckForUpdate should return a response"
        assert isinstance(response, (tuple, list)), f"Expected tuple, got {type(response)}"
        assert len(response) == 6, f"Expected 6 elements, got {len(response)}: {response}"
        
        # Extract fields
        result_code = int(response[0])               # API call result
        current_version = str(response[1])           # Current firmware version
        available_version = str(response[2])         # Available firmware version
        update_details = str(response[3])            # Update details
        status_message = str(response[4])            # Status/error message
        status_code = int(response[5])               # Status code (0-5)
        
        print(f"CheckForUpdate returned:")
        print(f"   Result: {result_code} ({'SUCCESS' if result_code == 0 else 'FAIL'})")
        print(f"   Current Version: '{current_version}'")
        print(f"   Available Version: '{available_version}'")
        print(f"   Update Details: '{update_details}'")
        print(f"   Status Message: '{status_message}'")
        print(f"   Status Code: {status_code} ", end="")
        
        # Decode status code
        status_names = {
            0: "FIRMWARE_AVAILABLE",
            1: "FIRMWARE_NOT_AVAILABLE",
            2: "UPDATE_NOT_ALLOWED",
            3: "FIRMWARE_CHECK_ERROR",
            4: "IGNORE_OPTOUT",
            5: "BYPASS_OPTOUT"
        }
        print(f"({status_names.get(status_code, 'UNKNOWN')})")
        
        # Verify response structure
        assert result_code == 0, f"Expected result=0 (SUCCESS), got: {result_code}"
        assert status_code in [0, 1, 2, 3, 4, 5], \
            f"Invalid status_code: {status_code} (valid: 0-5)"
        
        print(f"TEST PASSED: Response structure valid")

    finally:
        stop_daemon(proc)


def test_different_client_cannot_check_update_for_registered_process():
    """
    Test that CheckForUpdate is accessible by any client (read-only operation).
    
    Unlike UnregisterProcess, CheckForUpdate doesn't require client ownership.
    Any client can check updates for any valid handler_id.
    
    Expected: Client 2 CAN successfully call CheckForUpdate with Client 1's handler_id.
    """
    print("\n=== Test: Multi-client CheckForUpdate access ===")
    proc = start_daemon()
    try:
        # Client 1 registers ProcA
        api1 = iface()
        result1 = api1.RegisterProcess("ProcA", "1.0")
        id1 = int(result1[0]) if isinstance(result1, tuple) else int(result1)
        print(f"Client 1 registered ProcA with handler_id: {id1}")

        # Client 2 tries to check update for ProcA's handler_id
        bus2 = dbus.SystemBus()
        proxy2 = bus2.get_object(DBUS_SERVICE_NAME, DBUS_OBJECT_PATH)
        api2 = dbus.Interface(proxy2, DBUS_INTERFACE)

        # This SHOULD succeed - CheckForUpdate is read-only
        response = api2.CheckForUpdate(str(id1))
        assert response is not None, "CheckForUpdate should succeed for valid handler_id"
        
        # Verify response structure
        assert len(response) == 6, f"Expected 6 elements, got {len(response)}"
        
        result_code = int(response[0])
        status_code = int(response[5])
        status_message = str(response[4])
        
        assert result_code == 0, f"Expected result=0 (SUCCESS), got: {result_code}"
        
        print(f"Client 2 successfully called CheckForUpdate:")
        print(f"   Result: {result_code} (SUCCESS)")
        print(f"   Status Code: {status_code}")
        print(f"   Message: '{status_message}'")
        print(f" TEST PASSED: Multi-client access works for CheckForUpdate")

    finally:
        stop_daemon(proc)


def test_check_for_update_fails_after_unregistration():
    """
    Test that CheckForUpdate returns error after handler_id is unregistered.
    
    Expected Response after unregistration: (issssi)
      result=0 (CHECK_FOR_UPDATE_SUCCESS - D-Bus call succeeded)
      status_code=3 (FIRMWARE_CHECK_ERROR - handler not registered)
      status_message="Handler not registered. Call RegisterProcess first."
    
    Note: The daemon does NOT raise D-Bus exceptions. It returns a successful
    D-Bus response with error details encoded in the tuple.
    """
    print("\n=== Test: CheckForUpdate after unregistration ===")
    proc = start_daemon()
    
    try:
        api = iface()

        # Register process
        result = api.RegisterProcess("ProcA", "1.0")
        id1 = result if isinstance(result, tuple) else int(result)
        print(f" Registered with handler_id: {id1}")
        
        # Unregister process
        unregister_result = api.UnregisterProcess(id1)
        assert bool(unregister_result) == True, "Unregister should succeed"
        print(f" Unregistered handler_id: {id1}")

        # Call CheckForUpdate with unregistered handler_id
        response = api.CheckForUpdate(str(id1))
        
        # Response signature: (issssi)
        assert response is not None, "CheckForUpdate should return a response"
        assert isinstance(response, (tuple, list)), f"Expected tuple, got {type(response)}"
        assert len(response) == 6, f"Expected 6 elements, got {len(response)}: {response}"
        
        # Extract fields
        result_code = int(response[0])               # API call result
        current_version = str(response[1])           # Current firmware version
        available_version = str(response[2])         # Available firmware version
        update_details = str(response[3])            # Update details
        status_message = str(response[4])            # Status/error message
        status_code = int(response[5])               # Status code (0-5)
        
        # Verify response values
        assert result_code == 0, f"Expected result=0 (SUCCESS), got: {result_code}"
        assert status_code == 3, f"Expected status_code=3 (FIRMWARE_CHECK_ERROR), got: {status_code}"
        assert "not registered" in status_message.lower(), \
            f"Expected 'not registered' in message, got: '{status_message}'"
        
        print(f" TEST PASSED: CheckForUpdate correctly rejected unregistered handler_id")
        print(f"   Result: {result_code} (CHECK_FOR_UPDATE_SUCCESS)")
        print(f"   Status Code: {status_code} (FIRMWARE_CHECK_ERROR)")
        print(f"   Message: '{status_message}'")

    finally:
        stop_daemon(proc)

def test_cache_miss_first_boot_calls_xconf_and_creates_cache():
    """
    Scenario 1: First boot cold start (cache miss)

    Preconditions:
      - No cache files exist
    Expected:
      - CheckForUpdate succeeds
      - Fresh XConf call is made
      - Cache files created
      - Correct log messages appear
    """

    # 1) ensure no cache exists
    remove_cache_files()
    assert not cache_exists()
    
    # 2) call CheckForUpdate through D-Bus wrapper
    proc = start_daemon()
    
    try:
        api = iface()
        # Register process first
        result = api.RegisterProcess("ProcA", "1.0")
        handler_id = result if isinstance(result, tuple) else int(result)
        assert handler_id > 0, f"Registration should return valid handler_id, got {handler_id}"
        print(f"Registered with handler_id: {handler_id}")

        # Call CheckForUpdate with the registered handler_id
        response = api.CheckForUpdate(str(handler_id))

        # 3) Verify response structure: (result, current_version, available_version, update_details, status_message, status_code)
        assert response is not None, "CheckForUpdate should return a response"
        assert isinstance(response, (tuple, list)), f"Expected tuple, got {type(response)}"
        assert len(response) == 6, f"Expected 6 elements, got {len(response)}: {response}"
        
        # Extract fields
        result = int(response[0])                    # API call result (0=success, 1=fail)
        current_version = str(response[1])           # Current firmware version
        available_version = str(response[2])         # Available firmware version
        update_details = str(response[3])            # Update details
        status_message = str(response[4])            # Status/error message
        status_code = int(response[5])               # Status code (0-5)
        
        # Verify CheckForUpdate succeeded
        assert result == 0, f"Expected result=0 (CHECK_FOR_UPDATE_SUCCESS), got: {result}"
        print(f"CheckForUpdate response: result={result}, status_code={status_code}, message='{status_message}'")

        # Note: The daemon has a 120-second sleep before making XConf call
        # Status code 3 (FIRMWARE_CHECK_ERROR) with "in progress" message means XConf call is happening
        print(f"Status code {status_code} indicates XConf query state")
        
        # 4) Wait for cache to be created (XConf call is asynchronous/delayed)
        # The daemon sleeps for 120 seconds before XConf call in fetch_xconf_firmware_info()
        print("Waiting for XConf query to complete and cache to be created...")
        max_wait_seconds = 180  # Wait up to 3 minutes (120s sleep + 60s for XConf call)
        wait_interval = 5       # Check every 5 seconds
        elapsed = 0
        
        while elapsed < max_wait_seconds:
            if cache_exists():
                print(f"Cache created after {elapsed} seconds")
                break
            time.sleep(wait_interval)
            elapsed += wait_interval
            if elapsed % 30 == 0:  # Log every 30 seconds
                print(f"Still waiting for cache... ({elapsed}s elapsed)")
        
        # Now verify cache exists
        assert cache_exists(), f"Cache files should be created after XConf query (waited {elapsed}s)"

        # 5) logs must say cache miss and creation
        assert grep_log_file("/opt/logs/swupdate.txt.0",
                             "Cache miss! Making live XConf call"), \
                             "Expected 'Cache miss!' log entry"

        assert grep_log_file("/opt/logs/swupdate.txt.0",
                             "XConf data cached successfully"), \
                             "Expected 'XConf data cached successfully' log entry"
        
        print("TEST PASSED: Cache miss scenario - XConf called and cache created")
        
    finally:
        stop_daemon(proc)
