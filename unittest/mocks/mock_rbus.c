#include "mock_rbus.h"
#include <stdio.h>
#include <string.h>

// Simulated global handle
static rbusHandle_t dummyHandle = (rbusHandle_t)0x1234;

// Simulated value for UPLOAD_STATUS
static int uploadStatusValue = 1; 

rbusError_t rbus_open(rbusHandle_t* handle, const char* componentName) {
    printf("Mock rbus_open called with name: %s\n", componentName);
    *handle = dummyHandle;
    return RBUS_ERROR_SUCCESS;
}

rbusError_t rbus_close(rbusHandle_t handle) {
    printf("Mock rbus_close called\n");
    return RBUS_ERROR_SUCCESS;
}

rbusError_t rbusMethod_InvokeAsync(rbusHandle_t handle, const char* method, rbusObject_t input, rbusMethodAsyncRespHandler_t handler, int timeout) {
    printf("Mock rbusMethod_InvokeAsync called with method: %s, timeout: %d\n", method, timeout);
    if (handler) {
        handler();  // Simulate callback
    }
    return RBUS_ERROR_SUCCESS;
}

rbusValue_t rbusObject_GetValue(rbusObject_t obj, const char* name) {
    printf("Mock rbusObject_GetValue called with name: %s\n", name);
    if (strcmp(name, "UPLOAD_STATUS") == 0) {
        return (rbusValue_t)&uploadStatusValue;
    }
    return NULL;
}

