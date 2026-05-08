# Module Spec: dbus_server (D-Bus Server Infrastructure)

## Identity

- **Module**: `dbus_server`
- **Source**: `src/dbus/rdkv_dbus_server.c`, `src/dbus/rdkFwupdateMgr_handlers.c`, `src/dbus/xconf_comm_status.c`
- **Headers**: `src/dbus/rdkv_dbus_server.h`, `src/dbus/rdkFwupdateMgr_handlers.h`, `src/dbus/xconf_comm_status.h`
- **Type**: Internal daemon component

## Purpose

D-Bus system bus server that exposes firmware update operations to client applications. Manages process registrations, dispatches async tasks, and emits completion/progress signals.

## Components

### rdkv_dbus_server

- Acquires D-Bus bus name `org.rdkfwupdater.Service`
- Registers object path `/org/rdkfwupdater/Service`
- Routes incoming method calls to appropriate handlers
- Manages GDBusConnection lifecycle
- Emits D-Bus signals for async operation results

### rdkFwupdateMgr_handlers

- Implements business logic for each D-Bus method
- Process registration tracking (`GHashTable` keyed by handler_id)
- CheckForUpdate with cache-first approach
- Async XConf fetch via GTask framework
- Result formatting for D-Bus response

### xconf_comm_status

- Thread-safe XConf fetch status tracking
- Mutex-protected state (idle/in-progress)
- Prevents duplicate concurrent XConf fetches

## Key Data Structures

```c
typedef struct {
    guint64 handler_id;
    gchar *process_name;
    gchar *lib_version;
    gchar *sender_id;
    gint64 registration_time;
} ProcessInfo;

typedef enum {
    TASK_TYPE_CHECK_UPDATE,
    TASK_TYPE_DOWNLOAD,
    TASK_TYPE_UPDATE,
    TASK_TYPE_REGISTER,
    TASK_TYPE_UNREGISTER
} TaskType;

typedef struct {
    CheckForUpdateResult result;
    CheckForUpdateStatus status_code;
    gchar *current_img_version;
    gchar *available_version;
    gchar *update_details;
    gchar *status_message;
} CheckUpdateResponse;
```

## XConf Cache Strategy

1. Check cache file: `/tmp/xconf_response_thunder.txt`
2. **Cache hit**: Parse cached response, validate, return immediately
3. **Cache miss**: Spawn async GTask to fetch from XConf server
4. Return `FIRMWARE_CHECK_ERROR` immediately on cache miss
5. Emit `CheckForUpdateComplete` signal when async fetch completes

## Thread Safety

- `xconf_comm_status` — Mutex-protected, prevents concurrent XConf fetches
- `ProcessInfo` hash table — Accessed only from GLib main loop thread
- GTask worker threads — Isolated execution context

## API Functions

```c
// Handlers
CheckUpdateResponse rdkFwupdateMgr_checkForUpdate(const gchar *handler_id);
void checkupdate_response_free(CheckUpdateResponse *resp);

// XConf Status
gboolean initXConfCommStatus(void);
gboolean trySetXConfCommStatus(gboolean in_progress);
gboolean getXConfCommStatus(void);
void cleanupXConfCommStatus(void);
```

## Test Coverage

- `unittest/rdkFwupdateMgr_handlers_gtest.cpp`
- `unittest/dbus_handlers.cpp`
- `unittest/test_dbus_fake.cpp` (fake D-Bus for unit testing)
