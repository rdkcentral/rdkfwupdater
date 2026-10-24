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

@pytest.mark.run(order=11)
def test_dwnl_firmware_retry_test():
    remove_file("/tmp/pdri_image_file")
    remove_file("/tmp/.xconfssrdownloadurl")
    route_file = Path("/tmp/pdri_image_file")
    route_file.touch(exist_ok=True)
    write_on_file("/tmp/pdri_image_file", "ABCD_PDRI_img")
    rename_file(SWUPDATE_CONF_FILE, BKUP_SWUPDATE_CONF_FILE)
    rename_file(UNRESOLVED_SWUPDATE_CONF_FILE, SWUPDATE_CONF_FILE)
    result = subprocess.run(['rdkvfwupgrader', '0', '1'], stdout=subprocess.PIPE)
    rename_file(SWUPDATE_CONF_FILE, UNRESOLVED_SWUPDATE_CONF_FILE)
    rename_file(BKUP_SWUPDATE_CONF_FILE, SWUPDATE_CONF_FILE)

    ERROR_MSG1 = "retryDownload : Direct Image upgrade connection return: retry=2"
    assert grep_log_file("/opt/logs/swupdate.txt.0", ERROR_MSG1), f"Expected '{ERROR_MSG1}' in log file."
    #assert result.returncode == 0

@pytest.mark.run(order=12)
def test_fallback_codebig():
    ERROR_MSG1 = "fallBack : fall back Codebig Download"
    assert grep_log_file("/opt/logs/swupdate.txt.0", ERROR_MSG1), f"Expected '{ERROR_MSG1}' in log file."

@pytest.mark.run(order=13)
def test_dwnl_firmware_error_test():
    remove_file("/tmp/pdri_image_file")
    remove_file("/tmp/.xconfssrdownloadurl")
    route_file = Path("/tmp/pdri_image_file")
    route_file.touch(exist_ok=True)
    write_on_file("/tmp/pdri_image_file", "ABCD_PDRI_img")
    rename_file(SWUPDATE_CONF_FILE, BKUP_SWUPDATE_CONF_FILE)
    rename_file(INVALID_SWUPDATE_CONF_FILE, SWUPDATE_CONF_FILE)
    result = subprocess.run(['rdkvfwupgrader', '0', '1'], stdout=subprocess.PIPE)
    rename_file(SWUPDATE_CONF_FILE, INVALID_SWUPDATE_CONF_FILE)
    rename_file(BKUP_SWUPDATE_CONF_FILE, SWUPDATE_CONF_FILE)

    #ERROR_MSG1 = "retryDownload : Direct Image upgrade connection return: retry=2"
    #assert grep_log_file("/opt/logs/swupdate.txt.0", ERROR_MSG1), f"Expected '{ERROR_MSG1}' in log file."
    assert result.returncode == 0

@pytest.mark.run(order=14)
def test_dwnl_firmware_invalidpci_test():
    remove_file("/tmp/pdri_image_file")
    remove_file("/tmp/.xconfssrdownloadurl")
    route_file = Path("/tmp/pdri_image_file")
    route_file.touch(exist_ok=True)
    write_on_file("/tmp/pdri_image_file", "ABCD_PDRI_img")
    rename_file(SWUPDATE_CONF_FILE, BKUP_SWUPDATE_CONF_FILE)
    rename_file(INVALIDPCI_SWUPDATE_CONF_FILE, SWUPDATE_CONF_FILE)
    result = subprocess.run(['rdkvfwupgrader', '0', '1'], stdout=subprocess.PIPE)
    rename_file(SWUPDATE_CONF_FILE, INVALIDPCI_SWUPDATE_CONF_FILE)
    rename_file(BKUP_SWUPDATE_CONF_FILE, SWUPDATE_CONF_FILE)

    ERROR_MSG1 = "Image configured is not of model"
    assert grep_log_file("/opt/logs/swupdate.txt.0", ERROR_MSG1), f"Expected '{ERROR_MSG1}' in log file."
