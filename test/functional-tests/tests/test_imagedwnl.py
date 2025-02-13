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


def test_dwnl_firmware_test():
    initial_rdkfw_setup()
    write_device_prop()
    result = subprocess.run(['rdkvfwupgrader', '0', '1'], stdout=subprocess.PIPE)
    assert result.returncode == 0

