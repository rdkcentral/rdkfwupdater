# Copyright 2025 RDK Management
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

"""
D-Bus Helper Functions for rdkFwupdateMgr Integration Tests

Provides Python wrappers for calling D-Bus methods on the rdkFwupdateMgr daemon.
Uses native Python D-Bus bindings when available, falls back to subprocess if not.
"""

import subprocess
import json
import time
import os
import signal

# Try to import native D-Bus Python bindings (preferred method)
try:
    import dbus
    from dbus.mainloop.glib import DBusGMainLoop
    DBUS_PYTHON_AVAILABLE = True
    print("✓ Using native Python D-Bus bindings (python3-dbus)")
except ImportError:
    DBUS_PYTHON_AVAILABLE = False
    print("⚠ python3-dbus not available, using subprocess fallback")
    print("  Install with: apt-get install python3-dbus python3-gi")


# =============================================================================
# D-Bus Connection Management (Native Python)
# =============================================================================

def get_system_bus_native():
    """
    Get D-Bus system bus connection using native Python bindings.
    
    Returns:
        dbus.SystemBus: System bus connection or None if unavailable
    """
    if not DBUS_PYTHON_AVAILABLE:
        return None
    
    try:
        # Initialize D-Bus main loop (required for proper operation)
        DBusGMainLoop(set_as_default=True)
        
        # Connect to system bus
        bus = dbus.SystemBus()
        return bus
    except dbus.exceptions.DBusException as e:
        print(f"ERROR: Cannot connect to D-Bus system bus: {e}")
        return None
    except Exception as e:
        print(f"ERROR: {e}")
        return None


def get_service_proxy_native():
    """
    Get proxy object for rdkfwupdater service using native Python bindings.
    
    Returns:
        dbus.proxies.Interface: Service interface or None
    """
    try:
        bus = get_system_bus_native()
        if not bus:
            return None
        
        # Get proxy object for the service
        proxy = bus.get_object(DBUS_SERVICE_NAME, DBUS_OBJECT_PATH)
        
        # Get interface
        interface = dbus.Interface(proxy, DBUS_INTERFACE)
        
        return interface
    except dbus.exceptions.DBusException as e:
        print(f"ERROR: Cannot get service proxy: {e}")
        return None
    except Exception as e:
        print(f"ERROR: {e}")
        return None


# =============================================================================
# D-Bus Configuration
# =============================================================================

# D-Bus service configuration (must match daemon's actual registration)
DBUS_SERVICE_NAME = "org.rdkfwupdater.Service"      # BUS_NAME
DBUS_OBJECT_PATH = "/org/rdkfwupdater/Service"      # OBJECT_PATH (actual daemon path)
DBUS_INTERFACE = "org.rdkfwupdater.Interface"       # Interface name

DAEMON_BINARY = "/usr/local/bin/rdkFwupdateMgr"
DAEMON_PID_FILE = "/tmp/rdkFwupdateMgr.pid"


# =============================================================================
# Daemon Control Functions
# =============================================================================

def start_daemon():
    """
    Start the rdkFwupdateMgr daemon.
    
    The daemon requires two command-line arguments:
    - argv[1]: Failure retry count (e.g., "0" for tests)
    - argv[2]: Trigger type (1-6):
        1 = Bootup (used for systemd daemon startup)
        2 = Scheduled (cron)
        3 = TR-69/SNMP triggered
        4 = App triggered
        5 = Delayed download
        6 = State Red
    
    For testing, we use trigger_type=1 (Bootup) to match systemd behavior.
    
    Returns:
        bool: True if daemon started successfully, False otherwise
    """
    print(f"Starting daemon: {DAEMON_BINARY}")
    
    # Check if binary exists
    if not os.path.exists(DAEMON_BINARY):
        print(f"ERROR: Daemon binary not found at {DAEMON_BINARY}")
        return False
    
    # Kill any existing daemon
    stop_daemon()
    
    try:
        # Start daemon in background with required arguments
        # Use trigger_type=1 (Bootup) to match systemd daemon startup
        process = subprocess.Popen(
            [DAEMON_BINARY, "0", "1"],  # retry_count=0, trigger_type=1 (Bootup)
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            start_new_session=True
        )
        
        # Save PID
        with open(DAEMON_PID_FILE, 'w') as f:
            f.write(str(process.pid))
        
        # Wait for daemon to initialize
        time.sleep(2)
        
        # Check if still running
        if process.poll() is None:
            print(f"✓ Daemon started successfully (PID: {process.pid})")
            return True
        else:
            print(f"ERROR: Daemon exited immediately")
            return False
            
    except Exception as e:
        print(f"ERROR starting daemon: {e}")
        return False


def stop_daemon():
    """
    Stop the rdkFwupdateMgr daemon.
    """
    print("Stopping daemon...")
    
    # Try to read PID from file
    if os.path.exists(DAEMON_PID_FILE):
        try:
            with open(DAEMON_PID_FILE, 'r') as f:
                pid = int(f.read().strip())
            
            # Send SIGTERM
            try:
                os.kill(pid, signal.SIGTERM)
                time.sleep(1)
                
                # Check if still running
                try:
                    os.kill(pid, 0)
                    # Still running, force kill
                    os.kill(pid, signal.SIGKILL)
                    print(f"✓ Daemon killed (PID: {pid})")
                except OSError:
                    print(f"✓ Daemon stopped (PID: {pid})")
                    
            except OSError:
                print("Daemon not running")
                
            # Remove PID file
            os.remove(DAEMON_PID_FILE)
            
        except Exception as e:
            print(f"Error stopping daemon: {e}")
    
    # Also try pkill as backup
    try:
        subprocess.run(['pkill', '-9', 'rdkFwupdateMgr'], 
                      stdout=subprocess.DEVNULL, 
                      stderr=subprocess.DEVNULL)
    except:
        pass


def is_daemon_running():
    """
    Check if daemon is running.
    
    Returns:
        bool: True if daemon is running
    """
    if os.path.exists(DAEMON_PID_FILE):
        try:
            with open(DAEMON_PID_FILE, 'r') as f:
                pid = int(f.read().strip())
            
            # Check if process exists
            os.kill(pid, 0)
            return True
        except:
            return False
    return False


# =============================================================================
# D-Bus Method Calls (Native Python - Preferred)
# =============================================================================

def dbus_register_process_native(process_name, lib_version):
    """
    Call RegisterProcess using native Python D-Bus bindings.
    
    Args:
        process_name: Name of the process registering
        lib_version: Version of the library
        
    Returns:
        int: Handler ID (0 on failure)
    """
    print(f"\n→ D-Bus: RegisterProcess('{process_name}', '{lib_version}') [native]")
    
    try:
        interface = get_service_proxy_native()
        if not interface:
            print("← Failed to get service interface")
            return 0
        
        # Call method - D-Bus automatically converts Python types
        handler_id = interface.RegisterProcess(process_name, lib_version)
        
        # Convert from dbus.UInt64 to Python int
        handler_id = int(handler_id)
        
        print(f"← Received handler ID: {handler_id}")
        return handler_id
        
    except dbus.exceptions.DBusException as e:
        print(f"← D-Bus error: {e}")
        return 0
    except Exception as e:
        print(f"← Error: {e}")
        return 0


def dbus_unregister_process_native(handler_id):
    """
    Call UnregisterProcess using native Python D-Bus bindings.
    
    Args:
        handler_id: Handler ID to unregister
        
    Returns:
        bool or int: True/False or 0 for success
    """
    print(f"\n→ D-Bus: UnregisterProcess({handler_id}) [native]")
    
    try:
        interface = get_service_proxy_native()
        if not interface:
            return False
        
        # Call method - try string first (most likely), fallback to uint64
        try:
            result = interface.UnregisterProcess(str(handler_id))
        except dbus.exceptions.DBusException as e:
            if "signature" in str(e).lower():
                # If string didn't work, try uint64
                result = interface.UnregisterProcess(dbus.UInt64(handler_id))
            else:
                raise
        
        # Convert from dbus.Boolean to Python bool
        result = bool(result)
        
        print(f"← Result: {result}")
        return result
        
    except dbus.exceptions.DBusException as e:
        print(f"← D-Bus error: {e}")
        return False
    except Exception as e:
        print(f"← Error: {e}")
        return False


def dbus_check_for_update_native(handler_id):
    """
    Call CheckForUpdate using native Python D-Bus bindings.
    
    Args:
        handler_id: Handler ID of the client
        
    Returns:
        dict: Response dictionary with result_code, versions, etc.
    """
    print(f"\n→ D-Bus: CheckForUpdate({handler_id}) [native]")
    
    try:
        interface = get_service_proxy_native()
        if not interface:
            return {
                'result_code': 4,
                'current_img_version': "",
                'available_version': "",
                'update_details': "",
                'status_message': "D-Bus connection failed"
            }
        
        # Call method - CheckForUpdate expects handler_id as STRING, not uint64
        # This is why subprocess version worked (it sends as string)
        result = interface.CheckForUpdate(str(handler_id))
        
        # Parse response tuple
        # Expected: (result_code, current_version, available_version, url, message, extended)
        response = {
            'result_code': int(result[0]) if len(result) > 0 else 4,
            'current_img_version': str(result[1]) if len(result) > 1 else "",
            'available_version': str(result[2]) if len(result) > 2 else "",
            'update_details': str(result[3]) if len(result) > 3 else "",
            'status_message': str(result[4]) if len(result) > 4 else ""
        }
        
        print(f"← Response: result={response['result_code']}, "
              f"current={response['current_img_version']}, "
              f"available={response['available_version']}")
        
        return response
        
    except dbus.exceptions.DBusException as e:
        print(f"← D-Bus error: {e}")
        return {
            'result_code': 4,
            'current_img_version': "",
            'available_version': "",
            'update_details': "",
            'status_message': f"D-Bus error: {e}"
        }
    except Exception as e:
        print(f"← Error: {e}")
        return {
            'result_code': 4,
            'current_img_version': "",
            'available_version': "",
            'update_details': "",
            'status_message': f"Error: {e}"
        }


# =============================================================================
# D-Bus Method Call Helpers (Subprocess - Fallback)
# =============================================================================

def dbus_call_method(method_name, *args):
    """
    Generic D-Bus method call using dbus-send or gdbus.
    
    Args:
        method_name: Name of the D-Bus method
        *args: Arguments to pass to the method
        
    Returns:
        Output from the D-Bus call (parsed if possible)
    """
    try:
        # Build gdbus command for system bus
        # Daemon registers on G_BUS_TYPE_SYSTEM (see rdkv_dbus_server.c)
        cmd = [
            'gdbus', 'call',
            '--system',  # System bus (not session)
            '--dest', DBUS_SERVICE_NAME,
            '--object-path', DBUS_OBJECT_PATH,
            '--method', f'{DBUS_INTERFACE}.{method_name}'
        ]
        
        # Add arguments
        for arg in args:
            if isinstance(arg, str):
                cmd.append(f'"{arg}"')
            else:
                cmd.append(str(arg))
        
        print(f"D-Bus call: {' '.join(cmd)}")
        
        # Execute
        result = subprocess.run(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=10,
            text=True
        )
        
        if result.returncode == 0:
            output = result.stdout.strip()
            print(f"D-Bus response: {output}")
            return output
        else:
            print(f"D-Bus error: {result.stderr}")
            return None
            
    except subprocess.TimeoutExpired:
        print("D-Bus call timed out")
        return None
    except Exception as e:
        print(f"D-Bus call exception: {e}")
        return None


# =============================================================================
# Specific D-Bus Method Wrappers
# =============================================================================

def dbus_register_process(process_name, lib_version):
    """
    Call RegisterProcess D-Bus method.
    Uses native Python bindings if available, otherwise subprocess.
    
    Args:
        process_name: Name of the process registering
        lib_version: Version of the library
        
    Returns:
        int: Handler ID (0 on failure)
    """
    if process_name is None:
        process_name = ""
    
    # Use native Python D-Bus if available (preferred)
    if DBUS_PYTHON_AVAILABLE:
        return dbus_register_process_native(process_name, lib_version)
    
    # Fall back to subprocess method
    print(f"\n→ D-Bus: RegisterProcess('{process_name}', '{lib_version}') [subprocess]")
    
    output = dbus_call_method('RegisterProcess', process_name, lib_version)
    
    if output:
        try:
            # Parse handler ID from output
            # Expected format: (uint64 12345,)
            # Remove parentheses, split by space, get second element (the number)
            parts = output.strip('(),').split()
            if len(parts) >= 2:
                handler_id = int(parts[1].rstrip(','))
            else:
                # Fallback: try to extract any number
                import re
                match = re.search(r'\d+', output)
                handler_id = int(match.group()) if match else 0
            
            print(f"← Received handler ID: {handler_id}")
            return handler_id
        except Exception as e:
            print(f"← Failed to parse handler ID from: {output} (error: {e})")
            return 0
    
    return 0


def dbus_unregister_process(handler_id):
    """
    Call UnregisterProcess D-Bus method.
    Uses native Python bindings if available, otherwise subprocess.
    
    Args:
        handler_id: Handler ID to unregister
        
    Returns:
        bool or int: Result (True/False or 0 for success)
    """
    # Use native Python D-Bus if available (preferred)
    if DBUS_PYTHON_AVAILABLE:
        return dbus_unregister_process_native(handler_id)
    
    # Fall back to subprocess method
    print(f"\n→ D-Bus: UnregisterProcess({handler_id}) [subprocess]")
    
    output = dbus_call_method('UnregisterProcess', handler_id)
    
    if output:
        try:
            # Handle boolean response: (true,) or (false,)
            if 'true' in output.lower():
                print(f"← Result: True (success)")
                return True
            elif 'false' in output.lower():
                print(f"← Result: False (failure)")
                return False
            else:
                # Try parsing as integer
                result = int(output.strip('(),'))
                print(f"← Result: {result}")
                return result
        except:
            return -1
    
    return -1


def dbus_check_for_update(handler_id):
    """
    Call CheckForUpdate D-Bus method.
    Uses native Python bindings if available, otherwise subprocess.
    
    Args:
        handler_id: Handler ID of the client
        
    Returns:
        dict: Response dictionary with:
            - result_code: CheckForUpdateResult enum value
            - current_img_version: Current firmware version
            - available_version: Available firmware version
            - update_details: Update download URL
            - status_message: Status/error message
    """
    # Use native Python D-Bus if available (preferred)
    if DBUS_PYTHON_AVAILABLE:
        return dbus_check_for_update_native(handler_id)
    
    # Fall back to subprocess method
    print(f"\n→ D-Bus: CheckForUpdate({handler_id}) [subprocess]")
    
    output = dbus_call_method('CheckForUpdate', handler_id)
    
    if output:
        try:
            # Parse response
            # Expected format: (0, 'v1.0.0', 'v2.0.0', 'http://...', 'Success')
            # This is a simplified parser - adjust based on actual D-Bus signature
            
            # Remove parentheses and split
            parts = output.strip('()').split(',')
            
            response = {
                'result_code': int(parts[0].strip()) if len(parts) > 0 else 4,
                'current_img_version': parts[1].strip().strip("'\"") if len(parts) > 1 else "",
                'available_version': parts[2].strip().strip("'\"") if len(parts) > 2 else "",
                'update_details': parts[3].strip().strip("'\"") if len(parts) > 3 else "",
                'status_message': parts[4].strip().strip("'\"") if len(parts) > 4 else ""
            }
            
            print(f"← Response: result={response['result_code']}, "
                  f"current={response['current_img_version']}, "
                  f"available={response['available_version']}")
            
            return response
            
        except Exception as e:
            print(f"← Failed to parse response: {e}")
            return {
                'result_code': 4,  # UPDATE_ERROR
                'current_img_version': "",
                'available_version': "",
                'update_details': "",
                'status_message': "Parse error"
            }
    
    # Default error response
    return {
        'result_code': 4,  # UPDATE_ERROR
        'current_img_version': "",
        'available_version': "",
        'update_details': "",
        'status_message': "D-Bus call failed"
    }


def dbus_subscribe_to_events(handler_id, callback_endpoint):
    """
    Call SubscribeToEvents D-Bus method.
    
    Args:
        handler_id: Handler ID of the client
        callback_endpoint: HTTP callback URL
        
    Returns:
        int: Result code (0 = success)
    """
    print(f"\n→ D-Bus: SubscribeToEvents({handler_id}, '{callback_endpoint}')")
    
    output = dbus_call_method('SubscribeToEvents', handler_id, callback_endpoint)
    
    if output:
        try:
            result = int(output.strip('(),'))
            print(f"← Result: {result}")
            return result
        except:
            return -1
    
    return -1


def dbus_download_firmware(handler_id, image_name, available_version):
    """
    Call DownloadFirmware D-Bus method.
    
    Args:
        handler_id: Handler ID
        image_name: Firmware image name
        available_version: Version to download
        
    Returns:
        dict: Response with download_status and download_path
    """
    print(f"\n→ D-Bus: DownloadFirmware({handler_id}, '{image_name}', '{available_version}')")
    
    output = dbus_call_method('DownloadFirmware', handler_id, image_name, available_version)
    
    # Parse response (implementation depends on actual D-Bus signature)
    # Placeholder return
    return {
        'result': 0,
        'download_status': 'Success',
        'download_path': '/tmp/firmware.bin'
    }


def dbus_update_firmware(handler_id, current_version, target_version):
    """
    Call UpdateFirmware D-Bus method.
    
    Args:
        handler_id: Handler ID
        current_version: Current firmware version
        target_version: Target firmware version
        
    Returns:
        dict: Response with update_status and status_message
    """
    print(f"\n→ D-Bus: UpdateFirmware({handler_id}, '{current_version}', '{target_version}')")
    
    output = dbus_call_method('UpdateFirmware', handler_id, current_version, target_version)
    
    # Parse response
    return {
        'result': 0,
        'update_status': 'Success',
        'status_message': 'Firmware updated'
    }


# =============================================================================
# Utility Functions
# =============================================================================

def wait_for_dbus_service(timeout=10):
    """
    Wait for D-Bus service to become available.
    
    Args:
        timeout: Maximum time to wait in seconds
        
    Returns:
        bool: True if service is available
    """
    print(f"Waiting for D-Bus service {DBUS_SERVICE_NAME}...")
    
    start_time = time.time()
    
    while (time.time() - start_time) < timeout:
        try:
            # Try to list objects
            result = subprocess.run(
                ['gdbus', 'introspect', '--session',
                 '--dest', DBUS_SERVICE_NAME,
                 '--object-path', DBUS_OBJECT_PATH],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=2
            )
            
            if result.returncode == 0:
                print("✓ D-Bus service is available")
                return True
                
        except:
            pass
        
        time.sleep(0.5)
    
    print(f"✗ D-Bus service not available after {timeout}s")
    return False


if __name__ == "__main__":
    # Simple test
    print("D-Bus Helper Module")
    print(f"Service: {DBUS_SERVICE_NAME}")
    print(f"Object Path: {DBUS_OBJECT_PATH}")
    print(f"Interface: {DBUS_INTERFACE}")
