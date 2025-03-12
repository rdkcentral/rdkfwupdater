import pytest
import subprocess

import os

from rdkfw_test_helper import *

@pytest.mark.run(order=8)
def test_dwnl_firmware_retry_test():
    remove_file("/tmp/pdri_image_file")
    remove_file("/tmp/.xconfssrdownloadurl")
    route_file = Path("/tmp/pdri_image_file")
    route_file.touch(exist_ok=True)
    write_on_file("/tmp/pdri_image_file ", "ABCD_PDRI_img")
    rename_file(SWUPDATE_CONF_FILE, BKUP_SWUPDATE_CONF_FILE)
    rename_file(UNRESOLVED_SWUPDATE_CONF_FILE, SWUPDATE_CONF_FILE)
    result = subprocess.run(['rdkvfwupgrader', '0', '1'], stdout=subprocess.PIPE)
    rename_file(SWUPDATE_CONF_FILE, UNRESOLVED_SWUPDATE_CONF_FILE)
    rename_file(BKUP_SWUPDATE_CONF_FILE, SWUPDATE_CONF_FILE)

    ERROR_MSG1 = "retryDownload : Direct Image upgrade connection return: retry=2"
    assert grep_log_file("/opt/logs/swupdate.txt.0", ERROR_MSG1), f"Expected '{ERROR_MSG1}' in log file."
    #assert result.returncode == 0

@pytest.mark.run(order=9)
def test_dwnl_firmware_error_test():
    remove_file("/tmp/pdri_image_file")
    remove_file("/tmp/.xconfssrdownloadurl")
    route_file = Path("/tmp/pdri_image_file")
    route_file.touch(exist_ok=True)
    write_on_file("/tmp/pdri_image_file ", "ABCD_PDRI_img")
    rename_file(SWUPDATE_CONF_FILE, BKUP_SWUPDATE_CONF_FILE)
    rename_file(INVALID_SWUPDATE_CONF_FILE, SWUPDATE_CONF_FILE)
    result = subprocess.run(['rdkvfwupgrader', '0', '1'], stdout=subprocess.PIPE)
    rename_file(SWUPDATE_CONF_FILE, INVALID_SWUPDATE_CONF_FILE)
    rename_file(BKUP_SWUPDATE_CONF_FILE, SWUPDATE_CONF_FILE)

    #ERROR_MSG1 = "retryDownload : Direct Image upgrade connection return: retry=2"
    #assert grep_log_file("/opt/logs/swupdate.txt.0", ERROR_MSG1), f"Expected '{ERROR_MSG1}' in log file."
    assert result.returncode == 0
