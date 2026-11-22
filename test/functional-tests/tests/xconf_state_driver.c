#define _GNU_SOURCE
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <limits.h>
#include "iarmInterface.h"

struct FWDownloadStatus;
static const char *test_directory;
static int critical_events;
static int preparing_events;
static int flash_calls;
static int status_calls;
static int cleanup_calls;

/* Interpose platform side effects; the loaded library still executes its real reader and decisions. */
void eventManager(const char *name, const char *status)
{
    if (strcmp(name, "MaintenanceMGR") == 0 && strcmp(status, MAINT_CRITICAL_UPDATE) == 0) {
        ++critical_events;
    }
    if (strcmp(name, "FirmwareStateEvent") == 0 && strcmp(status, FW_STATE_PREPARING_TO_REBOOT) == 0) {
        ++preparing_events;
    }
}

bool isMediaClientDevice(void) { return true; }
bool isMmgbleNotifyEnabled(void) { return false; }
int isInStateRed(void) { return 0; }
int startFactoryProtectService(void) { abort(); }
unsigned int sleep(unsigned int seconds) { (void)seconds; return 0; }
void sync(void) {}
int t2_event_d(char *marker, int value) { (void)marker; (void)value; return 0; }
void updateUpgradeFlag(int action) { if (action == 2) { ++cleanup_calls; } }

int getDevicePropertyData(const char *key, char *value, int capacity)
{
    const char *result;
    if (strcmp(key, "DEVICE_NAME") == 0) { result = "DEV_CONTAINER"; }
    else if (strcmp(key, "DEVICE_TYPE") == 0) { result = "mediaclient"; }
    else if (strcmp(key, "CPU_ARCH") == 0) { result = "x86"; }
    else if (strcmp(key, "DIFW_PATH") == 0) { result = "/tmp"; }
    else { fprintf(stderr, "Unexpected property: %s\n", key); abort(); }
    snprintf(value, (size_t)capacity, "%s", result);
    return 0;
}

int filePresentCheck(const char *path)
{
    return strcmp(path, "/lib/rdk/imageFlasher.sh") == 0 ? 0 : -1;
}

int v_secure_system(const char *format, ...)
{
    if (strncmp(format, "/lib/rdk/imageFlasher.sh ", strlen("/lib/rdk/imageFlasher.sh ")) != 0) {
        fprintf(stderr, "Refusing unexpected command: %s\n", format);
        abort();
    }
    ++flash_calls;
    return 0;
}

int updateFWDownloadStatus(struct FWDownloadStatus *status, const char *disabled)
{
    (void)status;
    (void)disabled;
    ++status_calls;
    return 0;
}

/* Redirect every /tmp and /opt fopen to disposable files; never overwrite a device's state. */
FILE *fopen(const char *path, const char *mode)
{
    typedef FILE *(*RealFopen)(const char *, const char *);
    static RealFopen real_fopen;
    char redirected[PATH_MAX];
    if (real_fopen == NULL) {
        *(void **)(&real_fopen) = dlsym(RTLD_NEXT, "fopen");
        if (real_fopen == NULL) { abort(); }
    }
    if (test_directory != NULL && (strncmp(path, "/tmp/", 5) == 0 || strncmp(path, "/opt/", 5) == 0)) {
        int length = snprintf(redirected, sizeof(redirected), "%s/%s", test_directory, strrchr(path, '/') + 1);
        if (length < 0 || (size_t)length >= sizeof(redirected)) { abort(); }
        return real_fopen(redirected, mode);
    }
    return real_fopen(path, mode);
}

int main(int argc, char **argv)
{
    typedef int (*FlashImage)(const char *, const char *, const char *, const char *, int, const char *, int);
    typedef int (*PostFlash)(const char *, const char *, int, const char *, int);
    FlashImage flash_image;
    PostFlash post_flash;
    void *library;
    int result;
    if (argc != 4) {
        fprintf(stderr, "Usage: %s absolute-library-path test-directory flash|post\n", argv[0]);
        return 2;
    }
    test_directory = argv[2];
    library = dlopen(argv[1], RTLD_LAZY | RTLD_LOCAL);
    if (library == NULL) { fprintf(stderr, "%s\n", dlerror()); return 2; }
    *(void **)(&flash_image) = dlsym(library, "flashImage");
    *(void **)(&post_flash) = dlsym(library, "postFlash");
    if (flash_image == NULL || post_flash == NULL) { fprintf(stderr, "Missing flash exports\n"); return 2; }
    /* PDRI takes the state-dependent event path but never the Canary RPC/reboot branch. */
    if (strcmp(argv[3], "flash") == 0) {
        result = flash_image("https://mock.example/fw", "/tmp/state_test.bin", "true", "2", 1, "true", 3);
    } else if (strcmp(argv[3], "post") == 0) {
        result = post_flash("true", "state_test.bin", 1, "true", 3);
    } else { return 2; }
    printf("XCONF_STATE_RESULT {\"result\":%d,\"critical\":%d,\"preparing\":%d,"
           "\"flash\":%d,\"status\":%d,\"cleanup\":%d}\n",
           result, critical_events, preparing_events, flash_calls, status_calls, cleanup_calls);
    dlclose(library);
    return result == 0 ? 0 : 1;
}