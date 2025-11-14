#ifndef RDKFWUPDATEMGR_HANDLERS_H
#define RDKFWUPDATEMGR_HANDLERS_H

#include <glib.h>
#include "json_process.h"  // For XCONFRES structure

// Wrapper function declarations for D-Bus methods
int rdkFwupdateMgr_checkForUpdate(const gchar *handler_id,
                                  const gchar *current_version,
                                  gchar **available_version,
                                  gchar **update_details,
                                  gchar **status);

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
