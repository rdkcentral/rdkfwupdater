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
Multi-Process D-Bus Client Helper for rdkFwupdateMgr

Provides utilities to run multiple D-Bus clients in separate processes,
each with its own D-Bus connection. This is necessary because the daemon
enforces one client per D-Bus connection.

Usage:
    clients = MultiProcessDBusClients(num_clients=3)
    results = clients.register_all("TestClient", "1.0.0")
    check_results = clients.check_for_update_all()
    clients.unregister_all()
    clients.cleanup()
"""

import multiprocessing as mp
import time
import sys
import os
from typing import List, Dict, Any, Optional, Tuple

# Try to import dbus-python
try:
    import dbus
    from dbus.mainloop.glib import DBusGMainLoop
    NATIVE_DBUS_AVAILABLE = True
except ImportError:
    NATIVE_DBUS_AVAILABLE = False


# =============================================================================
# Single Client Process Function
# =============================================================================

def _client_process_worker(client_id: int, task_queue: mp.Queue, result_queue: mp.Queue):
    """
    Worker process for a single D-Bus client.
    
    Runs in a separate process, maintains its own D-Bus connection,
    and executes commands from the task queue.
    
    Args:
        client_id: Unique identifier for this client
        task_queue: Queue to receive commands
        result_queue: Queue to send results back to parent
    """
    
    # Initialize D-Bus connection in this process
    bus = None
    proxy = None
    handler_id = None
    
    try:
        if NATIVE_DBUS_AVAILABLE:
            # Use native Python D-Bus
            DBusGMainLoop(set_as_default=True)
            bus = dbus.SystemBus()
            
            # Retry getting D-Bus object and verify interface (daemon might still be initializing)
            max_retries = 10
            for attempt in range(max_retries):
                try:
                    proxy = bus.get_object('org.rdkfwupdater.Service', '/org/rdkfwupdater/Service')
                    # Verify the interface exists by introspecting
                    proxy.Introspect(dbus_interface='org.freedesktop.DBus.Introspectable')
                    break
                except dbus.exceptions.DBusException as e:
                    if attempt < max_retries - 1:
                        time.sleep(0.5)
                    else:
                        result_queue.put((client_id, 'fatal_error', f'Cannot connect to D-Bus service after {max_retries} attempts: {e}'))
                        return
        else:
            # Fallback not needed for multi-process (subprocess already isolates)
            result_queue.put((client_id, 'error', 'dbus-python not available'))
            return
        
        # Process commands from queue
        while True:
            try:
                task = task_queue.get(timeout=30)
                
                if task['command'] == 'exit':
                    result_queue.put((client_id, 'exit', 'ok'))
                    break
                
                elif task['command'] == 'register':
                    interface = dbus.Interface(proxy, 'org.rdkfwupdater.Service')
                    result = interface.RegisterProcess(
                        task['client_name'],
                        task['client_version']
                    )
                    # result is tuple: (handler_id_uint64, object_path_string)
                    handler_id = result[0]
                    obj_path = result[1]
                    result_queue.put((client_id, 'register', {'handler_id': int(handler_id), 'object_path': obj_path}))
                
                elif task['command'] == 'checkforupdate':
                    if handler_id is None:
                        result_queue.put((client_id, 'checkforupdate', {'error': 'not registered'}))
                        continue
                    
                    interface = dbus.Interface(proxy, 'org.rdkfwupdater.Service')
                    result = interface.CheckForUpdate(str(handler_id))  # Must be string
                    # result is tuple of 7 values
                    response = {
                        'update_available': int(result[0]),
                        'available_version': str(result[1]),
                        'update_url': str(result[2]),
                        'reboot_immediately': int(result[3]),
                        'error_code': int(result[4]),
                        'error_message': str(result[5]),
                        'http_code': int(result[6])
                    }
                    result_queue.put((client_id, 'checkforupdate', response))
                
                elif task['command'] == 'unregister':
                    if handler_id is None:
                        result_queue.put((client_id, 'unregister', {'error': 'not registered'}))
                        continue
                    
                    interface = dbus.Interface(proxy, 'org.rdkfwupdater.Service')
                    result = interface.UnregisterProcess(str(handler_id))  # Must be string
                    success = bool(result)
                    result_queue.put((client_id, 'unregister', {'success': success}))
                    handler_id = None  # Clear after unregister
                
                else:
                    result_queue.put((client_id, 'error', f'Unknown command: {task["command"]}'))
            
            except dbus.exceptions.DBusException as e:
                # D-Bus specific error - return the error message with context
                error_msg = f"DBus Error: {e.get_dbus_name()}: {e.get_dbus_message()}"
                result_queue.put((client_id, 'dbus_error', error_msg))
            except Exception as e:
                result_queue.put((client_id, 'error', str(e)))
    
    except Exception as e:
        result_queue.put((client_id, 'fatal_error', str(e)))


# =============================================================================
# Multi-Process D-Bus Client Manager
# =============================================================================

class MultiProcessDBusClients:
    """
    Manages multiple D-Bus clients, each in its own process.
    
    Each client has its own D-Bus connection, allowing true concurrent
    D-Bus operations that the daemon will see as separate clients.
    """
    
    def __init__(self, num_clients: int):
        """
        Initialize multi-client manager.
        
        Args:
            num_clients: Number of client processes to create
        """
        self.num_clients = num_clients
        self.task_queues: List[mp.Queue] = []
        self.result_queue = mp.Queue()
        self.processes: List[mp.Process] = []
        self.handler_ids: List[Optional[int]] = [None] * num_clients
        self.object_paths: List[Optional[str]] = [None] * num_clients
        
        # Start all client processes
        for i in range(num_clients):
            task_queue = mp.Queue()
            self.task_queues.append(task_queue)
            
            process = mp.Process(
                target=_client_process_worker,
                args=(i, task_queue, self.result_queue)
            )
            process.start()
            self.processes.append(process)
        
        # Give processes time to start and connect to D-Bus
        time.sleep(1.0)
    
    def _send_command_to_all(self, command: str, **kwargs) -> List[Dict[str, Any]]:
        """
        Send a command to all clients and collect results.
        
        Args:
            command: Command name
            **kwargs: Command parameters
        
        Returns:
            List of results, one per client
        """
        # Send command to all clients
        for task_queue in self.task_queues:
            task = {'command': command, **kwargs}
            task_queue.put(task)
        
        # Collect results
        results = [None] * self.num_clients
        for _ in range(self.num_clients):
            try:
                client_id, cmd, result = self.result_queue.get(timeout=10)
                results[client_id] = result
            except Exception as e:
                print(f"Error collecting result: {e}")
        
        return results
    
    def register_all(self, client_name_prefix: str, version: str) -> List[Dict[str, Any]]:
        """
        Register all clients with the daemon.
        
        Args:
            client_name_prefix: Base name for clients (will append index)
            version: Client version string
        
        Returns:
            List of registration results with handler_id and object_path
        """
        all_results = []
        
        for i in range(self.num_clients):
            task = {
                'command': 'register',
                'client_name': f"{client_name_prefix}_{i}",
                'client_version': version
            }
            self.task_queues[i].put(task)
        
        # Collect results
        for _ in range(self.num_clients):
            try:
                client_id, cmd, result = self.result_queue.get(timeout=10)
                if 'handler_id' in result:
                    self.handler_ids[client_id] = result['handler_id']
                    self.object_paths[client_id] = result['object_path']
                all_results.append((client_id, result))
            except Exception as e:
                print(f"Error during registration: {e}")
                all_results.append((-1, {'error': str(e)}))
        
        # Sort by client_id
        all_results.sort(key=lambda x: x[0])
        return [r[1] for r in all_results]
    
    def check_for_update_all(self) -> List[Dict[str, Any]]:
        """
        Send CheckForUpdate from all clients.
        
        Returns:
            List of CheckForUpdate responses
        """
        return self._send_command_to_all('checkforupdate')
    
    def unregister_all(self) -> List[Dict[str, Any]]:
        """
        Unregister all clients.
        
        Returns:
            List of unregister results
        """
        results = self._send_command_to_all('unregister')
        self.handler_ids = [None] * self.num_clients
        self.object_paths = [None] * self.num_clients
        return results
    
    def cleanup(self):
        """
        Clean up all client processes.
        """
        # Send exit command to all
        for task_queue in self.task_queues:
            try:
                task_queue.put({'command': 'exit'})
            except:
                pass
        
        # Wait for processes to finish
        for process in self.processes:
            process.join(timeout=2)
            if process.is_alive():
                process.terminate()
                process.join(timeout=1)
        
        # Close queues
        for task_queue in self.task_queues:
            task_queue.close()
        self.result_queue.close()


# =============================================================================
# Convenience Functions
# =============================================================================

def test_multiprocess_clients(num_clients: int = 3) -> bool:
    """
    Quick test of multi-process client functionality.
    
    Args:
        num_clients: Number of clients to test with
    
    Returns:
        True if test passed, False otherwise
    """
    print(f"\n=== Testing Multi-Process D-Bus Clients ({num_clients} clients) ===")
    
    try:
        # Create clients
        clients = MultiProcessDBusClients(num_clients=num_clients)
        
        # Register all
        print("Registering all clients...")
        reg_results = clients.register_all("TestClient", "1.0.0")
        for i, result in enumerate(reg_results):
            if 'handler_id' in result:
                print(f"  Client {i}: handler_id={result['handler_id']}, path={result['object_path']}")
            else:
                print(f"  Client {i}: ERROR - {result}")
                clients.cleanup()
                return False
        
        # Check for update from all
        print("\nChecking for updates from all clients...")
        check_results = clients.check_for_update_all()
        for i, result in enumerate(check_results):
            if result and 'available_version' in result:
                print(f"  Client {i}: version={result['available_version']}, available={result['update_available']}")
            else:
                print(f"  Client {i}: ERROR or no update - {result}")
        
        # Unregister all
        print("\nUnregistering all clients...")
        unreg_results = clients.unregister_all()
        for i, result in enumerate(unreg_results):
            if result and 'success' in result:
                print(f"  Client {i}: unregister={'success' if result['success'] else 'failed'}")
            else:
                print(f"  Client {i}: ERROR - {result}")
        
        # Cleanup
        clients.cleanup()
        print("\n✓ Multi-process test completed successfully")
        return True
    
    except Exception as e:
        print(f"\n✗ Multi-process test failed: {e}")
        return False


if __name__ == '__main__':
    """
    Standalone test when run directly.
    """
    if not NATIVE_DBUS_AVAILABLE:
        print("ERROR: dbus-python is not available")
        print("Install with: pip install dbus-python")
        sys.exit(1)
    
    success = test_multiprocess_clients(num_clients=3)
    sys.exit(0 if success else 1)
