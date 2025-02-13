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

import os
from pathlib import Path
import subprocess


RDKFW_PATH: str = "/usr/bin/rdkvfwupgrader"
SWUPDATE_LOG_FILE: str = "/opt/logs/swupdate.txt"
SWUPDATE_CONF_FILE: str = "/opt/swupdate.conf"
RDKFW_ROUTE_FILE: str = "/tmp/route_available"
RDKFW_DNS_FILE: str = "/etc/resolv.dnsmasq"
VERSION_FILE: str = "/version.txt"
TEST_RFC_PARAM_KEY1: str = "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Bootstrap.OsClass"
TEST_RFC_PARAM_VAL1: str = "default"
RDKFW_XCONF_URL: str = "https://mockxconf:50052/firmwareupdate/getfirmwaredata"
RDKFW_XCONF_404_URL: str = "https://mockxconf:50052/firmwareupdate404/getfirmwaredata"
RDKFW_XCONF_UNRESOLVED_URL: str = "https://unmockxconf:50052/featureControl/getSettings"


def write_on_file(file: str, content: str) -> None:
    """
    Write or append content to a file.

    :param file: The path to the file.
    :param content: The content to write or append.
    :return: None
    """
    if not os.path.exists(file):
        with open(file, 'w') as f:
            f.write(content)
    else:
        if os.path.getsize(file) == 0:
            with open(file, 'w') as f:
                f.write(content)
        else:
            with open(file, 'a') as f:
                f.write('\n' + content)


def get_FWversion() -> str | None:
    """
    Retrieve the firmware version from the specified version file.

    The function searches for a line that starts with "imagename:" 
    and returns the corresponding value.

    Returns:
        str | None: The firmware image name if found, otherwise None.
    """
    if os.path.exists(VERSION_FILE):
        try:
            with open(VERSION_FILE, "r") as version_file:
                for line in version_file:
                    if line.startswith("imagename:"):
                        image_name = line.split(":", 1)[1].strip()
                        if image_name:
                            return image_name
                        else:
                            return None
        except Exception as e:
            return None
    return None

def remove_file(file_name: str) -> None:
    """
    Remove a file if it exists.

    :param file_name: The path to the file to remove.
    :return: None
    """
    if os.path.exists(file_name):
        os.remove(file_name)


def rename_file(old_file_name: str, new_file_name: str) -> None:
    """
    Rename a file from old_file_name to new_file_name.

    :param old_file_name: The current name of the file.
    :param new_file_name: The new name for the file.
    :return: None
    """
    try:
        os.rename(old_file_name, new_file_name)
        print(f"Renamed {old_file_name} to {new_file_name}")
    except FileNotFoundError:
        print(f"The file {old_file_name} does not exist.")
    except PermissionError:
        print(f"Permission denied: can't rename {old_file_name}")
    except Exception as e:
        print(f"Error renaming file: {e}")


def grep_log_file(log_file: str, search_string: str) -> bool:
    """
    Perform a recursive search for the given string in the specified log file(s).

    :param log_file: The log file or log file pattern to search.
    :param search_string: The string to search for.
    :return: True if the string is found, False otherwise.
    """
    try:
        result = subprocess.run(
            f'grep -r "{search_string}" {log_file}*',
            shell=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )

        if result.returncode == 0:
            return True
        else:
            print(
                f"grep returned no matches. stdout: {result.stdout}, stderr: {result.stderr}"
            )
            return False
    except Exception as e:
        print(f"An error occurred while running grep: {e}")
        return False


def fw_run_binary() -> None:
    """
    Executes the RFC Manager binary.

    This function attempts to run the RFC Manager specified by RFC_MGR_PATH.
    It captures both standard output and standard error. If an exception occurs
    during the execution, it prints an error message indicating what went wrong.

    Returns:
        None
    """
    try:
        result = subprocess.run(
            [RDKFW_PATH], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
        )
    except Exception as e:
        print(f"An error occurred while running {RDKFW_PATH}: {e}")


def initial_rdkfw_setup():
    # /tmp/route_available
    route_file = Path("/tmp/route_available")
    route_file.touch(exist_ok=True)
    
    route_file = Path("/tmp/stt_received")
    route_file.touch(exist_ok=True)
    
    # GatewayIP File
    write_on_file("/tmp/.GatewayIP_dfltroute", "IPV4 8.8.4.4")
    
    # partnerid file
    write_on_file("/opt/partnerid", "default-parter")
    
    # Sample MAC
    write_on_file("/tmp/.estb_mac", "01:23:45:67:89:ab")
    
    #RFC SERVER_URL
    write_on_file(SWUPDATE_CONF_FILE, RDKFW_XCONF_URL)
    
    # /opt/secure/RFC directory
    os.makedirs("/opt/CDL", exist_ok=True)
    
    # RFC Prev FW Version
    #write_on_file(RFC_SEC_DIR+".version", get_FWversion() + "_PREV")
