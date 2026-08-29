import os
import re
import shutil
import subprocess
import time

import pytest


DEVICE_PROPERTIES = "/etc/device.properties"
SECURE_DEBUG_STATE = "/opt/enable_secure_dbg"

SWUPDATE_CONF = '/opt/swupdate.conf'
STATE_RED_CONF = '/opt/stateredrecovry.conf'
STATE_RED_MARKER = '/tmp/stateRedEnabled'

TEST_OVERRIDE_URL = "https://mock-fw-override.example.com"
TEST_STATE_RED_URL = "https://mock-state-red-fw.example.com"

UPDATER_CMD = 'rdkvfwupgrader 0 1'
UPDATER_LOGS = ['/opt/logs/swupdate.txt', '/opt/logs/swupdate.txt.0']

DBG_SERVICES_RFC = (
    "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Identity."
    "DbgServices.Enable"
)

DEVICE_TYPE_RFC = (
    "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Identity."
    "DeviceType"
)


def run_command(command, timeout=60):
    try:
        return subprocess.run(
            command,
            shell=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            universal_newlines=True,
            timeout=timeout
        )
    except subprocess.TimeoutExpired as error:
        stdout = error.stdout or ""
        stderr = error.stderr or ""

        class TimeoutResult:
            returncode = 124

        result = TimeoutResult()
        result.stdout = stdout
        result.stderr = stderr
        return result


def backup_file(path):
    backup = "/tmp/l2_backup_" + os.path.basename(path)

    if os.path.exists(backup):
        os.remove(backup)

    if os.path.exists(path):
        shutil.copy2(path, backup)
        return backup

    return None


def restore_file(path, backup):
    if backup and os.path.exists(backup):
        shutil.copy2(backup, path)
        os.remove(backup)
    elif os.path.exists(path):
        os.remove(path)


def replace_property(path, key, value):
    lines = []

    if os.path.exists(path):
        with open(path, "r") as fp:
            lines = fp.readlines()

    output = []
    found = False

    for line in lines:
        if line.startswith(key + "="):
            output.append("{}={}\n".format(key, value))
            found = True
        else:
            output.append(line)

    if not found:
        output.append("{}={}\n".format(key, value))

    with open(path, "w") as fp:
        fp.writelines(output)


def write_server_url(path, url):
    # Verified by generator against GetServerUrlFile().
    with open(path, "w") as fp:
        fp.write(url + "\n")


def set_build_type(build_type):
    replace_property(
        DEVICE_PROPERTIES,
        "BUILD_TYPE",
        build_type
    )


def set_rfc(parameter, value, data_type):
    command = (
        "tr181 -d -s -t {} -v {} {}"
        .format(data_type, value, parameter)
    )

    result = run_command(command, timeout=10)

    assert result.returncode == 0, (
        "TR-181 set failed.\n"
        "command={}\nstdout={}\nstderr={}"
        .format(
            command,
            result.stdout,
            result.stderr
        )
    )

    assert "Set operation success" in result.stdout


def get_rfc(parameter):
    result = run_command(
        "tr181 -g {}".format(parameter),
        timeout=10
    )

    if result.returncode != 0:
        return None

    output = result.stdout + "\n" + result.stderr

    patterns = [
        r"value\s*[:=]\s*([^\s]+)",
        r"Value\s*[:=]\s*([^\s]+)",
    ]

    for pattern in patterns:
        match = re.search(
            pattern,
            output
        )

        if match:
            return match.group(1).strip()

    return None


def wait_for_secure_debug_state(expected, timeout=5):
    end_time = time.time() + timeout

    while time.time() < end_time:
        if os.path.exists(SECURE_DEBUG_STATE):
            with open(SECURE_DEBUG_STATE, "r") as fp:
                actual = fp.read().strip()

            if actual == expected:
                return

        time.sleep(0.2)

    raise AssertionError(
        "{} did not become {}"
        .format(
            SECURE_DEBUG_STATE,
            expected
        )
    )


def set_signedlab_runtime_state(enabled):
    # Simulate the actual SIGNEDLAB runtime configuration.
    replace_property(
        DEVICE_PROPERTIES,
        "LABSIGNED_ENABLED",
        "true"
    )

    # Do NOT directly modify /opt/enable_secure_dbg.
    # tr69hostif owns this state file.
    set_rfc(
        DEVICE_TYPE_RFC,
        "test",
        "string"
    )

    set_rfc(
        DBG_SERVICES_RFC,
        "true" if enabled else "false",
        "bool"
    )

    wait_for_secure_debug_state(
        "1" if enabled else "0"
    )


def leave_state_red():
    if os.path.exists(STATE_RED_MARKER):
        os.remove(STATE_RED_MARKER)


def enter_state_red():
    parent = os.path.dirname(STATE_RED_MARKER)

    if parent and not os.path.exists(parent):
        os.makedirs(parent)

    with open(STATE_RED_MARKER, "a"):
        pass


def collect_logs():
    output = []

    for path in UPDATER_LOGS:
        if not os.path.exists(path):
            continue

        try:
            with open(path, "r", errors="ignore") as fp:
                data = fp.read()

            # Last section is enough for the current execution.
            output.append(data[-20000:])
        except Exception:
            pass

    return "\n".join(output)


def run_fwupdater():
    result = run_command(
        UPDATER_CMD,
        timeout=60
    )

    assert result.returncode != 127, (
        "Updater command not found: {}"
        .format(UPDATER_CMD)
    )

    output = (
        (result.stdout or "") +
        "\n" +
        (result.stderr or "") +
        "\n" +
        collect_logs()
    )

    return result, output


def assert_url_used(output, expected_url):
    assert expected_url in output, (
        "Expected URL was not used: {}\n"
        "Updater output:\n{}"
        .format(expected_url, output)
    )


def assert_url_not_used(output, unexpected_url):
    assert unexpected_url not in output, (
        "URL unexpectedly used: {}\n"
        "Updater output:\n{}"
        .format(unexpected_url, output)
    )


@pytest.fixture(autouse=True)
def preserve_device_state():
    backups = {
        DEVICE_PROPERTIES: backup_file(DEVICE_PROPERTIES),
        SWUPDATE_CONF: backup_file(SWUPDATE_CONF),
        STATE_RED_CONF: backup_file(STATE_RED_CONF),
        STATE_RED_MARKER: backup_file(STATE_RED_MARKER),
    }

    original_dbg = get_rfc(
        DBG_SERVICES_RFC
    )

    original_device_type = get_rfc(
        DEVICE_TYPE_RFC
    )

    yield

    for path, backup in backups.items():
        restore_file(path, backup)

    # Restore RFC values when readable.
    # Otherwise leave secure debug in a safe disabled state.
    if original_device_type:
        set_rfc(
            DEVICE_TYPE_RFC,
            original_device_type,
            "string"
        )
    else:
        set_rfc(
            DEVICE_TYPE_RFC,
            "test",
            "string"
        )

    if original_dbg:
        set_rfc(
            DBG_SERVICES_RFC,
            original_dbg,
            "bool"
        )
    else:
        set_rfc(
            DBG_SERVICES_RFC,
            "false",
            "bool"
        )


# ============================================================
# Normal SWUPDATE_CONF override
# ============================================================

@pytest.mark.parametrize(
    "build_type,runtime_state,override_expected",
    [
        ("dev", None, True),
        ("prod", None, False),
        ("unknown", None, False),
        ("signedlab", True, True),
        ("signedlab", False, False),
    ]
)
def test_server_url_override_runtime_feature(
        build_type,
        runtime_state,
        override_expected):

    leave_state_red()

    set_build_type(
        build_type
    )

    if build_type == "signedlab":
        set_signedlab_runtime_state(
            runtime_state
        )

    write_server_url(
        SWUPDATE_CONF,
        TEST_OVERRIDE_URL
    )

    _, output = run_fwupdater()

    if override_expected:
        assert_url_used(
            output,
            TEST_OVERRIDE_URL
        )
    else:
        assert_url_not_used(
            output,
            TEST_OVERRIDE_URL
        )


# ============================================================
# State-Red override
# ============================================================

def test_state_red_server_url_override_runtime_enabled():
    set_build_type(
        "signedlab"
    )

    set_signedlab_runtime_state(
        True
    )

    write_server_url(
        STATE_RED_CONF,
        TEST_STATE_RED_URL
    )

    enter_state_red()

    _, output = run_fwupdater()

    assert_url_used(
        output,
        TEST_STATE_RED_URL
    )


def test_state_red_server_url_override_runtime_disabled():
    set_build_type(
        "signedlab"
    )

    set_signedlab_runtime_state(
        False
    )

    write_server_url(
        STATE_RED_CONF,
        TEST_STATE_RED_URL
    )

    enter_state_red()

    _, output = run_fwupdater()

    assert_url_not_used(
        output,
        TEST_STATE_RED_URL
    )
