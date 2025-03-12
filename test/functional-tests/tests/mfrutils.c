#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
	FILE *fp = NULL;
	char tbuff[32] = {0};
	fp = fopen("/tmp/pdri_image_file", "r");
	if (fp != NULL) {
	    fgets(tbuff, sizeof(tbuff), fp);
	    printf("%s\n",tbuff);
	    fclose(fp);
	}else {
	    printf("ABCD_PDRI_firmware\n");
	}
	//printf("ABCD_PDRI_firmware\n");
	return 0;
}
