#ifndef RDKFWUPDATEMGR_HANDLERS_H
#define RDKFWUPDATEMGR_HANDLERS_H

#include <glib.h>
#include "json_process.h"  // For XCONFRES structure

// CheckForUpdate result enumeration
typedef enum {
    UPDATE_AVAILABLE,        // Update is available for the handler
    UPDATE_NOT_AVAILABLE,    // No update is available
    UPDATE_NOT_ALLOWED,      // Update is not allowed (e.g., device is in exclusion list)
    RDKFW_FAILED,           // Call to download failed
    UPDATE_ERROR            // Error occurred while checking for updates
} CheckForUpdateResult;

// Return structure for rdkFwupdateMgr_checkForUpdate (replaces double pointers)
typedef struct {
    CheckForUpdateResult result_code;  // Enum result
    gchar *current_img_version;        //Current running image on device
    gchar *available_version;          // Available firmware version (NULL if no update)
    gchar *update_details;             // Update details/download URL (NULL if no update)
    gchar *status_message;             // Status message for logging/debugging
} CheckUpdateResponse;

// Updated wrapper function using return structure approach (no double pointers)
CheckUpdateResponse rdkFwupdateMgr_checkForUpdate(const gchar *handler_id);

// Helper function to free CheckUpdateResponse memory
void checkupdate_response_free(CheckUpdateResponse *response);

int rdkFwupdateMgr_downloadFirmware(const gchar *handler_id,
                                    const gchar *image_name,
                                    const gchar *available_version,
                                    gchar **download_status,
                                    gchar **download_path);

int rdkFwupdateMgr_updateFirmware(const gchar *handler_id,
                                  const gchar *current_version,
                                  const gchar *target_version,
                                  gchar **update_status,
                                  gchar **status_message);

int rdkFwupdateMgr_subscribeToEvents(const gchar *handler_id,
                                     const gchar *callback_endpoint);

guint64 rdkFwupdateMgr_registerProcess(const gchar *process_name,
                                       const gchar *lib_version,
                                       const gchar *sender_id);

int rdkFwupdateMgr_unregisterProcess(guint64 handler_id);

#endif // RDKFWUPDATEMGR_HANDLERS_H
