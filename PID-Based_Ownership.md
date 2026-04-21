# PID-Based Ownership Validation for UnregisterProcess

## 1. Problem Statement

When `example_app` calls `registerProcess()`, `checkForUpdate()`, and `unregisterProcess()` sequentially, the unregister call fails with **"Access denied"**.

### Observed Logs

```
==== [D-BUS] INCOMING REQUEST: RegisterProcess   from :1.140 ====
==== [D-BUS] INCOMING REQUEST: CheckForUpdate    from :1.141 ====
==== [D-BUS] INCOMING REQUEST: UnregisterProcess from :1.145 ====

[ERROR] [UNREGISTER] Access denied: Handler 1 (process: ExampleApp)
        owned by ':1.140', but ':1.145' attempted to unregister
```

### What's Happening

Each API call in `librdkFwupdateMgr` creates a **new D-Bus connection**, which gets a unique sender ID from the D-Bus daemon:

| API Call            | D-Bus Sender | Same Process? |
|---------------------|-------------|---------------|
| `registerProcess()` | `:1.140`    | Yes (PID 5000)|
| `checkForUpdate()`  | `:1.141`    | Yes (PID 5000)|
| `unregisterProcess()`| `:1.145`   | Yes (PID 5000)|

The daemon currently validates ownership using the **D-Bus sender ID**, which changes on every new connection. So `:1.145 ≠ :1.140` → **rejected**.

---

## 2. Root Cause

The `librdkFwupdateMgr` library uses an **on-demand thread design** where each API call opens a new D-Bus connection and closes it after use. This causes the D-Bus sender ID (e.g., `:1.140`) to change with every call, even though all calls originate from the **same client process**.

The daemon's ownership check compares the caller's D-Bus sender ID against the stored sender ID from registration — this comparison will always fail when the sender ID changes.

---

## 3. Why Not Fix the Library Now?

The on-demand thread design in `librdkFwupdateMgr` is being **redesigned** (one-thread-per-client with a shared D-Bus connection). Fixing the library independently now would conflict with the upcoming rework. However, `unregisterProcess()` is **functionally broken** in the meantime, so a daemon-side fix is needed.

---

## 4. Approaches Considered

### Option A: Relax the Ownership Check Entirely

Remove the sender ID validation and allow any caller to unregister any handler.

- **Pro:** Simple, unblocks the issue immediately.
- **Con:** **No security.** Client B could unregister Client A's handler by guessing the handler ID.

### Option B: Fix the Library (Shared D-Bus Connection)

Create a singleton `GDBusConnection` shared across all API calls so the sender ID stays consistent.

- **Pro:** Proper fix, sender ID check works correctly.
- **Con:** **Deferred.** The library's thread design is being reworked, and this fix would conflict with that effort.

### Option C: PID-Based Validation (Recommended) ✅

Replace the D-Bus sender ID check with a **Process ID (PID)** check. Unlike the sender ID, the PID remains constant for the entire lifetime of a client process, regardless of how many D-Bus connections it opens.

- **Pro:** Secure, works with the current library design, no library changes needed.
- **Con:** Minor daemon-side change required.

---

## 5. Recommended Solution: PID-Based Validation

### 5.1 Concept

The D-Bus daemon provides a standard method `GetConnectionUnixProcessID` that returns the PID of any connected client. The PID is determined by the **process**, not the connection — so even if a client opens multiple connections, the PID is always the same.

```
Client Process (PID 5000)
├── registerProcess()    → connection :1.140 → PID 5000 ✅
├── checkForUpdate()     → connection :1.141 → PID 5000 ✅
└── unregisterProcess()  → connection :1.145 → PID 5000 ✅
```

### 5.2 Cross-Client Protection

```
Client A (PID 5000): registerProcess()    → handler_id 1, owner_pid = 5000
Client B (PID 6000): registerProcess()    → handler_id 2, owner_pid = 6000
Client B (PID 6000): unregisterProcess(1) → caller_pid 6000 ≠ owner_pid 5000 → REJECTED ✅
Client A (PID 5000): unregisterProcess(1) → caller_pid 5000 = owner_pid 5000 → ALLOWED  ✅
```

### 5.3 Comparison Table

| Approach                     | Cross-Client Protection | D-Bus Reconnection | Library Changes |
|------------------------------|:-----------------------:|:-------------------:|:---------------:|
| Sender ID check (current)   | ✅                      | ❌ Breaks           | None            |
| No check (fully relaxed)    | ❌                      | ✅ Works            | None            |
| **PID check (recommended)** | **✅**                  | **✅ Works**        | **None**        |

---

## 6. Implementation Details

### 6.1 Helper Function: Get Caller PID

A new utility function queries the D-Bus daemon for the PID of a given sender.

```c
/**
 * Get the PID of the D-Bus caller.
 * Unlike sender_id (e.g., ":1.140"), PID remains constant
 * regardless of how many D-Bus connections the client creates.
 */
static guint32 get_caller_pid(GDBusConnection *connection, const gchar *sender_id)
{
    GError *error = NULL;
    GVariant *result = NULL;
    guint32 pid = 0;

    result = g_dbus_connection_call_sync(
        connection,
        "org.freedesktop.DBus",
        "/org/freedesktop/DBus",
        "org.freedesktop.DBus",
        "GetConnectionUnixProcessID",
        g_variant_new("(s)", sender_id),
        G_VARIANT_TYPE("(u)"),
        G_DBUS_CALL_FLAGS_NONE,
        -1, NULL, &error);

    if (result) {
        g_variant_get(result, "(u)", &pid);
        g_variant_unref(result);
    } else {
        SWLOG_WARN("Failed to get PID for '%s': %s\n",
                   sender_id, error ? error->message : "unknown");
        if (error) g_error_free(error);
    }

    return pid;
}
```

### 6.2 Store PID in ProcessInfo

Add `owner_pid` to the existing `ProcessInfo` struct:

```c
typedef struct {
    gchar    *process_name;
    gchar    *sender_id;      /* kept for logging */
    guint32   owner_pid;      /* used for ownership validation */
    // ...existing fields...
} ProcessInfo;
```

### 6.3 Store PID During RegisterProcess

```c
// During RegisterProcess handling:
guint32 caller_pid = get_caller_pid(connection, rdkv_req_caller_id);
SWLOG_INFO("[REGISTER] Caller PID: %u, D-Bus sender: '%s'\n",
           caller_pid, rdkv_req_caller_id);

info->sender_id = g_strdup(rdkv_req_caller_id);
info->owner_pid = caller_pid;
```

### 6.4 Validate PID During UnregisterProcess

```c
static gboolean remove_process_from_tracking(GDBusConnection *connection,
                                              guint64 handler_id,
                                              const gchar *sender_id)
{
    ProcessInfo *info = g_hash_table_lookup(registered_processes,
                                            GINT_TO_POINTER(handler_id));
    if (!info) {
        SWLOG_ERROR("[UNREGISTER] Handler not found\n");
        return FALSE;
    }

    guint32 caller_pid = get_caller_pid(connection, sender_id);

    if (caller_pid == 0) {
        SWLOG_ERROR("[UNREGISTER] Cannot determine caller PID — rejecting\n");
        return FALSE;
    }

    if (info->owner_pid != caller_pid) {
        SWLOG_ERROR("[UNREGISTER] Access denied: owned by PID %u, "
                    "but PID %u attempted to unregister\n",
                    info->owner_pid, caller_pid);
        return FALSE;
    }

    SWLOG_INFO("[UNREGISTER] PID match (%u). Removing '%s'\n",
               caller_pid, info->process_name);
    g_hash_table_remove(registered_processes, GINT_TO_POINTER(handler_id));
    return TRUE;
}
```

---

## 7. Files Changed

| File | Change |
|------|--------|
| `src/dbus/rdkv_dbus_server.c` | Add `get_caller_pid()`, update `remove_process_from_tracking()` signature, store PID during register, validate PID during unregister |
| `src/dbus/rdkFwupdateMgr_handlers.h` | Add `owner_pid` field to `ProcessInfo` struct |

**No changes required in:**
- `librdkFwupdateMgr/` (client library)
- `example_app.c` (example application)

---

## 8. Future Consideration

When the `librdkFwupdateMgr` library redesign is complete (shared D-Bus connection / one-thread-per-client), the D-Bus sender ID will remain consistent across all API calls. At that point, you may optionally **add back the sender ID check alongside the PID check** for defense-in-depth:

```c
// Future: dual validation
if (info->owner_pid != caller_pid || g_strcmp0(info->sender_id, sender_id) != 0) {
    // reject
}
```

Until then, PID-based validation alone is sufficient and secure.
