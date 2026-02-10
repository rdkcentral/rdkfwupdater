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
Helper script to test D-Bus UnregisterProcess from a separate process.
This is used to simulate a truly different D-Bus client attempting to
unregister another client's handler_id.
"""

import sys
import dbus

DBUS_SERVICE_NAME = "org.rdkfwupdater.Service"
DBUS_OBJECT_PATH = "/org/rdkfwupdater/Service"
DBUS_INTERFACE = "org.rdkfwupdater.Interface"

def main():
    if len(sys.argv) < 2:
        print("Usage: unregister_client.py <handler_id>", file=sys.stderr)
        sys.exit(1)
    
    handler_id = int(sys.argv[1])
    
    try:
        # Connect to D-Bus
        bus = dbus.SystemBus()
        proxy = bus.get_object(DBUS_SERVICE_NAME, DBUS_OBJECT_PATH)
        interface = dbus.Interface(proxy, DBUS_INTERFACE)
        
        # Call UnregisterProcess
        result = interface.UnregisterProcess(handler_id)
        
        # Extract success boolean
        success = bool(result)
        
        # Print result (parent process will read this)
        print(f"{success}")
        sys.exit(0 if success else 1)
        
    except dbus.exceptions.DBusException as e:
        # Print error (parent will read this)
        print(f"ERROR: {e.get_dbus_name()}: {e.get_dbus_message()}", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
