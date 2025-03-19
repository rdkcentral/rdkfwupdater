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
#

import pytest
import subprocess

import os

from rdkfw_test_helper import *

@pytest.mark.run(order=15)
def test_dwnl_peripheral_firmware_test():
    remove_file("/tmp/fw_preparing_to_reboot")
    remove_file("/tmp/pdri_image_file")
    pdri_file = Path("/tmp/pdri_image_file")
    pdri_file.touch(exist_ok=True)
    write_on_file("/tmp/pdri_image_file", "ABCD_PDRI_firmware_test.bin")
    rename_file("/version.txt", "/bk_version.txt")
    version_file = Path("/version.txt")
    version_file.touch(exist_ok=True)
    write_on_file("/version.txt", "imagename:ABCD_firmware_test.bin")
    rename_file(SWUPDATE_CONF_FILE, BKUP_SWUPDATE_CONF_FILE)
    rename_file(PERIPHERAL_SWUPDATE_CONF_FILE, SWUPDATE_CONF_FILE)
    result = subprocess.run(['rdkvfwupgrader', '0', '1'], stdout=subprocess.PIPE)
    rename_file(SWUPDATE_CONF_FILE, PERIPHERAL_SWUPDATE_CONF_FILE)
    rename_file(BKUP_SWUPDATE_CONF_FILE, SWUPDATE_CONF_FILE)
    remove_file("/tmp/fw_preparing_to_reboot")
    remove_file("/tmp/currently_running_image_name")
    remove_file("/opt/cdl_flashed_file_name")
    rename_file("/bk_version.txt", "/version.txt")
    assert result.returncode == 0

@pytest.mark.run(order=16)
def test_dwnl_peripheral_firmware404_test():
    remove_file("/tmp/fw_preparing_to_reboot")
    remove_file("/tmp/pdri_image_file")
    pdri_file = Path("/tmp/pdri_image_file")
    pdri_file.touch(exist_ok=True)
    write_on_file("/tmp/pdri_image_file", "ABCD_PDRI_firmware_test.bin")
    rename_file("/version.txt", "/bk_version.txt")
    version_file = Path("/version.txt")
    version_file.touch(exist_ok=True)
    write_on_file("/version.txt", "imagename:ABCD_firmware_test.bin")
    rename_file(SWUPDATE_CONF_FILE, BKUP_SWUPDATE_CONF_FILE)
    rename_file(PERIPHERAL_SWUPDATE_404CONF_FILE, SWUPDATE_CONF_FILE)
    result = subprocess.run(['rdkvfwupgrader', '0', '1'], stdout=subprocess.PIPE)
    rename_file(SWUPDATE_CONF_FILE, PERIPHERAL_SWUPDATE_404CONF_FILE)
    rename_file(BKUP_SWUPDATE_CONF_FILE, SWUPDATE_CONF_FILE)
    remove_file("/tmp/fw_preparing_to_reboot")
    remove_file("/tmp/currently_running_image_name")
    remove_file("/opt/cdl_flashed_file_name")
    rename_file("/bk_version.txt", "/version.txt")
    assert result.returncode == 0

@pytest.mark.run(order=17)
def test_dwnl_all_firmware_test():
    remove_file("/tmp/fw_preparing_to_reboot")
    remove_file("/tmp/pdri_image_file")
    remove_file("/tmp/downloaded_peripheral_versions.txt")
    remove_file("/opt/CDL/AB11-20_firmware_5103.3.4.tgz")
    pdri_file = Path("/tmp/pdri_image_file")
    pdri_file.touch(exist_ok=True)
    write_on_file("/tmp/pdri_image_file", "ABCD_PDRI_fir_test.bin")
    rename_file(SWUPDATE_CONF_FILE, BKUP_SWUPDATE_CONF_FILE)
    rename_file(PERIPHERAL_SWUPDATE_CONF_FILE, SWUPDATE_CONF_FILE)
    result = subprocess.run(['rdkvfwupgrader', '0', '1'], stdout=subprocess.PIPE)
    rename_file(SWUPDATE_CONF_FILE, PERIPHERAL_SWUPDATE_CONF_FILE)
    rename_file(BKUP_SWUPDATE_CONF_FILE, SWUPDATE_CONF_FILE)
    remove_file("/tmp/fw_preparing_to_reboot")
    remove_file("/tmp/currently_running_image_name")
    remove_file("/opt/cdl_flashed_file_name")
    remove_file("/tmp/downloaded_peripheral_versions.txt")
    remove_file("/opt/CDL/AB11-20_firmware_5103.3.4.tgz")
    assert result.returncode == 0

@pytest.mark.run(order=18)
def test_dwnl_all_firmware_test_verify():
    ERROR_MSG1 = "Image Flashing is success"
    assert grep_log_file("/opt/logs/swupdate.txt.0", ERROR_MSG1), f"Expected '{ERROR_MSG1}' in log file."
