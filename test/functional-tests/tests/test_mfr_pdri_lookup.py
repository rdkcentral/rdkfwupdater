# Copyright 2023 Comcast Cable Communications Management, LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0

import subprocess

import pytest

from rdkfw_test_helper import remove_file, write_on_file


MFR_UTIL_PATH = "/usr/bin/mfr_util"
PDRI_IMAGE_FILE = "/tmp/pdri_image_file"
DEFAULT_PDRI_VALUE = "ABCD_PDRI_firmware"


@pytest.mark.run(order=1)
def test_mfr_util_reads_seeded_pdri_value():
    remove_file(PDRI_IMAGE_FILE)
    write_on_file(PDRI_IMAGE_FILE, "ABCD_PDRI_img")

    result = subprocess.run([MFR_UTIL_PATH], capture_output=True, text=True)

    assert result.returncode == 0
    assert result.stdout.strip() == "ABCD_PDRI_img"


@pytest.mark.run(order=2)
def test_mfr_util_returns_default_when_pdri_file_missing():
    remove_file(PDRI_IMAGE_FILE)

    result = subprocess.run([MFR_UTIL_PATH], capture_output=True, text=True)

    assert result.returncode == 0
    assert result.stdout.strip() == DEFAULT_PDRI_VALUE
