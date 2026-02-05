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
