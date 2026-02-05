#!/usr/bin/env python3
"""
Helper script to test D-Bus RegisterProcess from a separate process.
This is used to simulate a truly different D-Bus client.
"""

import sys
import dbus

DBUS_SERVICE_NAME = "org.rdkfwupdater.Service"
DBUS_OBJECT_PATH = "/org/rdkfwupdater/Service"
DBUS_INTERFACE = "org.rdkfwupdater.Interface"

def main():
    if len(sys.argv) < 3:
        print("Usage: register_client.py <process_name> <version>", file=sys.stderr)
        sys.exit(1)
    
    process_name = sys.argv[1]
    version = sys.argv[2]
    
    try:
        # Connect to D-Bus
        bus = dbus.SystemBus()
        proxy = bus.get_object(DBUS_SERVICE_NAME, DBUS_OBJECT_PATH)
        interface = dbus.Interface(proxy, DBUS_INTERFACE)
        
        # Call RegisterProcess
        result = interface.RegisterProcess(process_name, version)
        
        # Extract handler_id (result can be a tuple or a single value)
        if isinstance(result, tuple):
            handler_id = int(result[0])
        else:
            handler_id = int(result)
        
        # Print result (parent process will read this)
        print(f"{handler_id}")
        sys.exit(0)
        
    except dbus.exceptions.DBusException as e:
        # Print error (parent will read this)
        print(f"ERROR: {e.get_dbus_name()}: {e.get_dbus_message()}", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
