#include <stdio.h>
#include <string.h>
#include "device_api.h"

int main(void) {
    char pdri[256] = {0};
    size_t len = GetPDRIFileName(pdri, sizeof(pdri));
    printf("GetPDRIFileName returned: '%s' (len=%zu)\n", pdri, len);
    if (len > 0 && strlen(pdri) == len) {
        puts("SUCCESS: GetPDRIFileNameUsingMFR test passed");
        return 0;
    } else {
        puts("FAIL: GetPDRIFileNameUsingMFR test failed");
        return 1;
    }
}
