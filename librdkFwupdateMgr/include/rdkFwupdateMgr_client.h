/**
 * @file rdkFwupdateMgr_client.h
 * @brief RDK Firmware Update Manager Client Library
 * 
 * Public API for client applications to communicate with the RDK Firmware
 * Update Manager daemon via D-Bus.
 */

#ifndef RDKFWUPDATEMGR_CLIENT_H
#define RDKFWUPDATEMGR_CLIENT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * OPAQUE HANDLE TYPE
 * ======================================================================== */

/**
 * @brief Opaque handle for registered firmware update client
 * 
 * Returned by registerProcess(). Must be passed to all subsequent API calls.
 * Do not dereference or free directly - use unregisterProcess() to cleanup.
 */
typedef void* FirmwareInterfaceHandle;

/* ========================================================================
 * ENUMERATIONS
 * ======================================================================== */

/**
 * @brief Result codes for checkForUpdate() API
 */
typedef enum {
    CHECK_FOR_UPDATE_SUCCESS = 0,
    CHECK_FOR_UPDATE_FAIL = 1
} CheckForUpdateResult;

/**
 * @brief Firmware availability status
 */
typedef enum {
    FIRMWARE_AVAILABLE = 0,
    FIRMWARE_NOT_AVAILABLE = 1,
    UPDATE_NOT_ALLOWED = 2,
    FIRMWARE_CHECK_ERROR = 3,
    IGNORE_OPTOUT = 4,
    BYPASS_OPTOUT = 5
} CheckForUpdateStatus;

/**
 * @brief Result codes for subscribeToUpdateEvents() API
 */
typedef enum {
    SUBSCRIBE_SUCCESS = 0,
    SUBSCRIBE_FAILED = 1
} SubscribeResult;

/**
 * @brief Result codes for downloadFirmware() API
 */
typedef enum {
    RDKFW_DWNL_SUCCESS = 0,
    RDKFW_DWNL_FAILED = 1
} DownloadResult;

/**
 * @brief Download progress status
 */
typedef enum {
    DWNL_NOT_STARTED = 0,
    DWNL_IN_PROGRESS = 1,
    DWNL_COMPLETED = 2,
    DWNL_ERROR = 3
} DownloadStatus;

/**
 * @brief Result codes for updateFirmware() API
 */
typedef enum {
    RDKFW_UPDATE_SUCCESS = 0,
    RDKFW_UPDATE_FAILED = 1
} UpdateResult;

/**
 * @brief Firmware update progress status
 */
typedef enum {
    UPDATE_NOT_STARTED = 0,
    UPDATE_IN_PROGRESS = 1,
    UPDATE_COMPLETED = 2,
    UPDATE_ERROR = 3
} UpdateStatus;

/* ========================================================================
 * DATA STRUCTURES
 * ======================================================================== */

/**
 * @brief Firmware information data
 * 
 * Passed to UpdateEventCallback. Valid only during callback invocation.
 */
typedef struct {
    const char *version;
    const char *updateDetails;
    CheckForUpdateStatus status;
} FwInfoData;

/**
 * @brief Firmware download request
 * 
 * Client allocates and populates before calling downloadFirmware().
 * Client owns all memory.
 */
typedef struct {
    const char *firmwareName;      /* Required */
    const char *downloadUrl;       /* Optional - NULL = use XCONF URL */
    const char *typeOfFirmware;    /* Required: "PCI", "PDRI", "PERIPHERAL" */
} FwDwnlReq;

/**
 * @brief Firmware update request
 * 
 * Client allocates and populates before calling updateFirmware().
 * Client owns all memory.
 */
typedef struct {
    const char *firmwareName;         /* Required */
    const char *typeOfFirmware;       /* Required: "PCI", "PDRI", "PERIPHERAL" */
    const char *locationOfFirmware;   /* Optional - NULL = use default path */
    bool rebootImmediately;           /* true = reboot after update */
} FwUpdateReq;

/* ========================================================================
 * CALLBACK TYPES
 * ======================================================================== */

/**
 * @brief Callback for firmware update events
 * 
 * @param fwInfoData Firmware information (valid only during callback)
 * @param userData Client-provided context pointer
 * 
 * @warning Must not block. Must not call library APIs.
 */
typedef void (*UpdateEventCallback)(const FwInfoData *fwInfoData, void *userData);

/**
 * @brief Callback for download progress notifications
 * 
 * @param progressPercent Download progress (0-100)
 * @param status Current download status
 * @param userData Client-provided context pointer
 * 
 * @warning Must not block. Must not call library APIs.
 */
typedef void (*DownloadCallback)(int progressPercent, DownloadStatus status, void *userData);

/**
 * @brief Callback for update progress notifications
 * 
 * @param progressPercent Update progress (0-100)
 * @param status Current update status
 * @param userData Client-provided context pointer
 * 
 * @warning Must not block. Must not call library APIs.
 */
typedef void (*UpdateCallback)(int progressPercent, UpdateStatus status, void *userData);

/* ========================================================================
 * PUBLIC API FUNCTIONS
 * ======================================================================== */

/**
 * @brief Register a process with the firmware update daemon
 * 
 * @param processName Client process name (e.g., "MyApp")
 * @param libVersion Client library version (e.g., "1.0.0")
 * @return Opaque handle on success, NULL on failure
 * 
 * @note Thread-safe
 */
FirmwareInterfaceHandle registerProcess(const char *processName, const char *libVersion);

/**
 * @brief Unregister a process from the firmware update daemon
 * 
 * @param handle Handle returned by registerProcess()
 * 
 * @note Thread-safe. Handle becomes invalid after this call.
 */
void unregisterProcess(FirmwareInterfaceHandle handle);

/**
 * @brief Check if firmware update is available
 * 
 * @param handle Handle returned by registerProcess()
 * @param callback Callback to invoke with result
 * @param userData Opaque pointer passed to callback (may be NULL)
 * @return CHECK_FOR_UPDATE_SUCCESS or CHECK_FOR_UPDATE_FAIL
 * 
 * @note Thread-safe. Callback invoked from library thread.
 */
CheckForUpdateResult checkForUpdate(
    FirmwareInterfaceHandle handle,
    UpdateEventCallback callback,
    void *userData
);

/**
 * @brief Subscribe to firmware update event notifications
 * 
 * @param handle Handle returned by registerProcess()
 * @param callback Callback to invoke on update events
 * @param userData Opaque pointer passed to callback (may be NULL)
 * @return SUBSCRIBE_SUCCESS or SUBSCRIBE_FAILED
 * 
 * @note Thread-safe. Callback invoked from library thread.
 */
SubscribeResult subscribeToUpdateEvents(
    FirmwareInterfaceHandle handle,
    UpdateEventCallback callback,
    void *userData
);

/**
 * @brief Initiate firmware download
 * 
 * @param handle Handle returned by registerProcess()
 * @param request Download request parameters
 * @param callback Callback for progress notifications
 * @param userData Opaque pointer passed to callback (may be NULL)
 * @return RDKFW_DWNL_SUCCESS or RDKFW_DWNL_FAILED
 * 
 * @note Thread-safe. Client may free request after call returns.
 */
DownloadResult downloadFirmware(
    FirmwareInterfaceHandle handle,
    const FwDwnlReq *request,
    DownloadCallback callback,
    void *userData
);

/**
 * @brief Initiate firmware update (flash)
 * 
 * @param handle Handle returned by registerProcess()
 * @param request Update request parameters
 * @param callback Callback for progress notifications
 * @param userData Opaque pointer passed to callback (may be NULL)
 * @return RDKFW_UPDATE_SUCCESS or RDKFW_UPDATE_FAILED
 * 
 * @note Thread-safe. Client may free request after call returns.
 * @warning Device may reboot if request.rebootImmediately is true.
 */
UpdateResult updateFirmware(
    FirmwareInterfaceHandle handle,
    const FwUpdateReq *request,
    UpdateCallback callback,
    void *userData
);

#ifdef __cplusplus
}
#endif

#endif /* RDKFWUPDATEMGR_CLIENT_H */
