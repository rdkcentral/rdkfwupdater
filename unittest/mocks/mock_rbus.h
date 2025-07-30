#ifndef MOCK_RBUS_H
#define MOCK_RBUS_H

#ifdef __cplusplus
extern "C" {
#endif

// Dummy type definitions
typedef void* rbusHandle_t;
typedef int rbusError_t;
typedef void* rbusObject_t;
typedef void* rbusValue_t;
typedef void (*rbusMethodAsyncRespHandler_t)(void);

// Constants
#define RBUS_ERROR_SUCCESS 0

// Mock function declarations
rbusError_t rbus_open(rbusHandle_t* handle, const char* name);
rbusError_t rbus_close(rbusHandle_t handle);
rbusError_t rbusMethod_InvokeAsync(rbusHandle_t handle, const char* method, rbusObject_t input, rbusMethodAsyncRespHandler_t handler, int timeout);
rbusValue_t rbusObject_GetValue(rbusObject_t obj, const char* name);

// Initialization function
void init_mock_rbus();

#ifdef __cplusplus
}
#endif

#endif  // MOCK_RBUS_H
