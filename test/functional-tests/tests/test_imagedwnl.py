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

def write_device_prop():
    file_path = "/etc/device.properties"
    data = """DEVICE_NAME=DEV_CONTAINER
DEVICE_TYPE=mediaclient
DIFW_PATH=/opt/CDL
ENABLE_MAINTENANCE=false
MODEL_NUM=ABCD
ENABLE_SOFTWARE_OPTOUT=false
BUILD_TYPE=VBN
ESTB_INTERFACE=eth0
PDRI_ENABLED=true
"""

    try:
        # Open the file in write mode
        with open(file_path, "w") as file:
            file.write(data)
        print(f"Successfully written to {file_path}")

    except PermissionError:
        print(f"Permission denied: You need root privileges to write to {file_path}")
        print("Try running the script with sudo: sudo python script.py")

    except Exception as e:
        print(f"An error occurred: {e}")

@pytest.mark.run(order=1)
def test_dwnl_firmware_test():
    initial_rdkfw_setup()
    write_device_prop()
    remove_file("/tmp/pdri_image_file")
    remove_file("/tmp/.xconfssrdownloadurl")
    route_file = Path("/tmp/pdri_image_file")
    route_file.touch(exist_ok=True)
    write_on_file("/tmp/pdri_image_file ", "ABCD_PDRI_img")
    result = subprocess.run(['rdkvfwupgrader', '0', '1'], stdout=subprocess.PIPE)
    assert result.returncode == 0

@pytest.mark.run(order=2)
def test_rdm_trigger_check():
   if os.path.exists("/tmp/.xconfssrdownloadurl"):
       res = 0
   else:
       res = 1
   assert res == 0

@pytest.mark.run(order=3)
def test_waiting_for_reboot():
    result = subprocess.run(['rdkvfwupgrader', '0', '1'], stdout=subprocess.PIPE)
    assert result.returncode == 1

@pytest.mark.run(order=4)
def test_flash_fail():
    remove_file("/lib/rdk/imageFlasher.sh")
    route_file = Path("/lib/rdk/imageFlasher.sh")
    route_file.touch(exist_ok=True)
    write_on_file("/lib/rdk/imageFlasher.sh", "#!/bin/bash\nexit 1")
    os.chmod(route_file, 0o777)
    result = subprocess.run(['rdkvfwupgrader', '0', '1'], stdout=subprocess.PIPE)
    remove_file("/lib/rdk/imageFlasher.sh")
    route_file = Path("/lib/rdk/imageFlasher.sh")
    route_file.touch(exist_ok=True)
    write_on_file("/lib/rdk/imageFlasher.sh", "#!/bin/bash\nexit 0")
    os.chmod(route_file, 0o777)
    assert result.returncode == 0

# To implement throttle we need similar type of jsonrpc or python module to create file inside /proc
#def test_throttle_dwnl():
#    #/proc/brcm/video_decoder
#    video_file = Path("/proc/brcm/video_decoder")
#    video_file.touch(exist_ok=True)
#    write_on_file("/proc/brcm/video_decoder", "pts=1234")
#    result = subprocess.run(['rdkvfwupgrader', '0', '1'], stdout=subprocess.PIPE)
#    remove_file("/tmp/fw_preparing_to_reboot")
#    assert result.returncode == 0

@pytest.mark.run(order=5)
def test_http_404():
    remove_file("/tmp/.xconfssrdownloadurl")
    rename_file(SWUPDATE_CONF_FILE, BKUP_SWUPDATE_CONF_FILE)
    rename_file(ERR_SWUPDATE_CONF_FILE, SWUPDATE_CONF_FILE)
    result = subprocess.run(['rdkvfwupgrader', '0', '1'], stdout=subprocess.PIPE)
    remove_file("/tmp/fw_preparing_to_reboot")
    rename_file(SWUPDATE_CONF_FILE, ERR_SWUPDATE_CONF_FILE)
    rename_file(BKUP_SWUPDATE_CONF_FILE, SWUPDATE_CONF_FILE)
    assert result.returncode == 0

@pytest.mark.run(order=6)
def test_rdm_trigger_http_404():
   if os.path.exists("/tmp/.xconfssrdownloadurl"):
       res = 0
   else:
       res = 1
   assert res == 0

@pytest.mark.run(order=7)
def test_no_upgrade():
    remove_file("/tmp/fw_preparing_to_reboot")
    remove_file("/tmp/pdri_image_file")
    pdri_file = Path("/tmp/pdri_image_file")
    pdri_file.touch(exist_ok=True)
    write_on_file("/tmp/pdri_image_file", "ABCD_PDRI_firmware_test.bin")
    rename_file("/version.txt", "/bk_version.txt")
    version_file = Path("/version.txt")
    version_file.touch(exist_ok=True)
    write_on_file("/version.txt", "imagename:ABCD_firmware_test.bin")
    result = subprocess.run(['rdkvfwupgrader', '0', '1'], stdout=subprocess.PIPE)
    remove_file("/tmp/fw_preparing_to_reboot")
    remove_file("/tmp/currently_running_image_name")
    remove_file("/opt/cdl_flashed_file_name")
    rename_file("/bk_version.txt", "/version.txt")
    assert result.returncode == 0
