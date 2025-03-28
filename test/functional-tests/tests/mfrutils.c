/*
 * Copyright 2023 Comcast Cable Communications Management, LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

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
