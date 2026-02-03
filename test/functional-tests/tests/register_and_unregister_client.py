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
#!/usr/bin/env python3
"""
Helper script to register and then unregister a D-Bus client from the same process.
This maintains the same sender_id for both operations, which is required for proper
security validation in the daemon.

Usage: register_and_unregister_client.py <process_name> <version>
"""

import sys
import dbus
import time

DBUS_SERVICE_NAME = "org.rdkfwupdater.Service"
DBUS_OBJECT_PATH = "/org/rdkfwupdater/Service"
DBUS_INTERFACE = "org.rdkfwupdater.Interface"

def main():
    if len(sys.argv) < 3:
        print("Usage: register_and_unregister_client.py <process_name> <version>", file=sys.stderr)
        sys.exit(1)
    
    process_name = sys.argv[1]
    version = sys.argv[2]
    
    try:
        # Connect to D-Bus (this establishes our sender_id)
        bus = dbus.SystemBus()
        proxy = bus.get_object(DBUS_SERVICE_NAME, DBUS_OBJECT_PATH)
        interface = dbus.Interface(proxy, DBUS_INTERFACE)
        
        # Step 1: Register
        result = interface.RegisterProcess(process_name, version)
        handler_id = int(result[0]) if isinstance(result, tuple) else int(result)
        print(f"REGISTERED:{handler_id}", flush=True)
        
        # Wait for parent to signal us to unregister (read from stdin)
        # This keeps the subprocess alive and maintains the same sender_id
        sys.stdin.readline()
        
        # Step 2: Unregister (using same sender_id as registration)
        unregister_result = interface.UnregisterProcess(handler_id)
        success = bool(unregister_result)
        
        if success:
            print("UNREGISTERED:SUCCESS", flush=True)
            sys.exit(0)
        else:
            print("UNREGISTERED:FAILED", flush=True)
            sys.exit(1)
        
    except dbus.exceptions.DBusException as e:
        print(f"ERROR:{e.get_dbus_name()}:{e.get_dbus_message()}", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"ERROR:Exception:{e}", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()

