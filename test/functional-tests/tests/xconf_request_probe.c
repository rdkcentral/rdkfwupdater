#define _GNU_SOURCE
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <dlfcn.h>
#include <curl/curl.h>

static bool inside_builder;

static size_t metadata(char *output, size_t capacity, const char *normal)
{
    const char *mode = getenv("RDKFW_REQUEST_CASE");
    bool oversized = inside_builder && mode && strcmp(mode, "overflow") == 0;
    size_t length = oversized ? 390 : strlen(normal);
    if (capacity <= length) { abort(); }
    if (oversized) {
        memset(output, 'A', length);
        output[length] = '\0';
    } else {
        memcpy(output, normal, length + 1);
    }
    return length;
}

size_t GetPartnerId(char *output, size_t capacity) { return metadata(output, capacity, "lab-partner"); }
size_t GetOsClass(char *output, size_t capacity) { return metadata(output, capacity, "RDK"); }
size_t GetAccountID(char *output, size_t capacity) { return metadata(output, capacity, "lab-account"); }
size_t GetServURL(char *output, size_t capacity)
{
    const char *url = "https://127.0.0.1:9/xconf-request-test";
    if (capacity <= strlen(url)) { abort(); }
    strcpy(output, url);
    return strlen(url);
}
bool isDirectCDNEnabled(void)
{
    const char *mode = getenv("RDKFW_REQUEST_DIRECTCDN");
    const bool directcdn = mode && strcmp(mode, "1") == 0;
    fprintf(stderr, "REQUEST_PROBE route=%s\n", directcdn ? "directcdn" : "standard");
    return directcdn;
}

/* The production retry path sleeps for up to 60 seconds after the blocked transport. */
unsigned int sleep(unsigned int seconds)
{
    (void)seconds;
    return 0;
}

int usleep(unsigned int microseconds)
{
    (void)microseconds;
    return 0;
}

int nanosleep(const struct timespec *requested, struct timespec *remaining)
{
    (void)requested;
    if (remaining != NULL) {
        remaining->tv_sec = 0;
        remaining->tv_nsec = 0;
    }
    return 0;
}

/* Call the production builder, recording its result without altering it. */
size_t createJsonString(char *output, size_t capacity)
{
    typedef size_t (*Builder)(char *, size_t);
    Builder real_builder;
    *(void **)(&real_builder) = dlsym(RTLD_NEXT, "createJsonString");
    if (real_builder == NULL) { abort(); }
    inside_builder = true;
    size_t result = real_builder(output, capacity);
    inside_builder = false;
    fprintf(stderr, "REQUEST_PROBE build_failed=%d empty=%d length=%zu\n",
            result == SIZE_MAX, output[0] == '\0', result);
    return result;
}

/* Never send traffic: the control records transport entry, the overflow must not enter it. */
CURLcode curl_easy_perform(CURL *handle)
{
    (void)handle;
    fprintf(stderr, "REQUEST_PROBE transport\n");
    return CURLE_COULDNT_CONNECT;
}