import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from contextlib import ExitStack

import pytest


DIRECT_CDN_RFC = (
    "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.SWDLDirect.Enable"
)
FIRST = "AB11-20_firmware_2.0.0.0"
FAILED = "CD15-10_firmware_3.0.0.0"
SECOND = "CD15-10_firmware_2.0.0.0"
DEBUG_RFC = "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Identity.DbgServices.Enable"
CAPACITY_NAMES = tuple(
    f"{'A' if index % 2 == 0 else 'B'}_firmware_{20 + index}"
    for index in range(16)
)
DOWNLOAD_LIST = Path("/tmp/downloaded_peripheral_versions.txt")
FIRMWARE_DIR = Path("/opt/CDL")
LOGS = (Path("/opt/logs/swupdate.txt.0"), Path("/opt/logs/swupdate.txt"))


def command(arguments, timeout=30):
    result = subprocess.run(
        arguments, capture_output=True, text=True, timeout=timeout, check=False
    )
    assert result.returncode == 0, (
        f"Command failed: {arguments}\n{result.stdout}\n{result.stderr}"
    )
    return result.stdout


def save_path(path, backup_dir, index):
    assert not path.is_symlink(), f"Refusing test state symlink: {path}"
    existed = path.exists()
    backup = backup_dir / str(index)
    if existed:
        assert path.is_file(), f"Expected a regular file: {path}"
        shutil.copy2(path, backup)

    def restore():
        if existed:
            shutil.copy2(backup, path)
        else:
            path.unlink(missing_ok=True)

    return restore


def read_boolean_rfc(parameter):
    output = command(["rbuscli", "getv", parameter])
    match = re.search(r"\bvalue\s*[:=]\s*(true|false)\b", output, re.IGNORECASE)
    assert match, f"Cannot determine original RFC {parameter}: {output}"
    return match.group(1).lower()


def set_boolean_rfc(parameter, value):
    command(["rbuscli", "setv", parameter, "boolean", value])
    assert read_boolean_rfc(parameter) == value, f"RFC {parameter} did not retain {value}"


@pytest.fixture
def legacy_remote_lab():
    if os.environ.get("RDKFW_LEGACY_REMOTE_L2") != "1":
        pytest.skip("Opt-in disposable L2 only: set RDKFW_LEGACY_REMOTE_L2=1")
    assert os.geteuid() == 0, "Run only in a root-owned disposable L2 container"
    binary = shutil.which("rdkvfwupgrader")
    assert binary and shutil.which("rbuscli"), "Install updater and RBUS test setup first"
    assert Path("/etc/os-release").is_file(), "Legacy peripheral gate requires /etc/os-release"
    assert shutil.which("pgrep"), "pgrep is required to exclude concurrent updater instances"
    for process in ("rdkvfwupgrader", "rdkFwupdateMgr"):
        running = subprocess.run(
            ["pgrep", "-x", process], capture_output=True, text=True, check=False
        )
        assert running.returncode == 1, f"Stop {process} before isolated tests: {running.stdout}"

    urls_file = Path(os.environ.get("RDKFW_LEGACY_REMOTE_URLS", ""))
    payload_file = Path(os.environ.get("RDKFW_LEGACY_REMOTE_PAYLOAD", ""))
    assert urls_file.is_file(), "Set RDKFW_LEGACY_REMOTE_URLS to the scenario URL JSON file"
    assert payload_file.is_file(), "Set RDKFW_LEGACY_REMOTE_PAYLOAD to the served harmless .tgz"
    urls = json.loads(urls_file.read_text())
    assert isinstance(urls, dict), "Scenario URL configuration must be a JSON object"
    payload = payload_file.read_bytes()
    assert payload, "Served archive fixture must not be empty"
    FIRMWARE_DIR.mkdir(parents=True, exist_ok=True)
    Path("/opt/logs").mkdir(parents=True, exist_ok=True)

    state_paths = [
        Path("/etc/device.properties"), Path("/version.txt"),
        Path("/opt/swupdate.conf"), Path("/tmp/rc-proxy-params.json"),
        DOWNLOAD_LIST, Path("/tmp/pdri_image_file"),
        Path("/tmp/.xconfssrdownloadurl"), Path("/tmp/.xconfRdmCatalogueVersion"),
        Path("/tmp/fw_preparing_to_reboot"), Path("/tmp/currently_running_image_name"),
        Path("/opt/cdl_flashed_file_name"), Path("/opt/.dnldURL"),
        Path("/opt/xconf_curl_httpcode"), Path("/opt/fwdnldstatus.txt"),
        Path("/opt/curl_progress"), Path("/tmp/DIFD.pid"),
        Path("/tmp/device_initiated_rcdl_in_progress"),
        Path("/tmp/.lastdirectfail_cdl"), Path("/tmp/.lastcodebigfail_cdl"),
        Path("/tmp/route_available"), Path("/tmp/stt_received"),
        Path("/tmp/.GatewayIP_dfltroute"), Path("/tmp/.estb_mac"),
        Path("/opt/partnerid"), Path("/opt/rdm-versioned-packages.conf"),
        Path("/tmp/stateRedEnabled"), Path("/opt/red_state_reboot"),
        Path("/lib/rdk/imageFlasher.sh"), Path("/rebootNow.sh"),
        Path("/tmp/legacy_remote_unexpected_platform_call"),
        *LOGS,
    ]
    artifact_names = (FIRST, FAILED, SECOND, *CAPACITY_NAMES)
    state_paths.extend(
        FIRMWARE_DIR / (name + suffix)
        for name in artifact_names for suffix in (".tgz", ".tgz.header")
    )

    with tempfile.TemporaryDirectory(prefix="legacy-remotes-") as temporary:
        with ExitStack() as cleanup:
            for index, path in enumerate(state_paths):
                cleanup.callback(save_path(path, Path(temporary), index))
            for parameter, value in ((DIRECT_CDN_RFC, "false"), (DEBUG_RFC, "true")):
                original_rfc = read_boolean_rfc(parameter)
                cleanup.callback(set_boolean_rfc, parameter, original_rfc)
                set_boolean_rfc(parameter, value)
            for path in state_paths[4:]:
                path.unlink(missing_ok=True)
            Path("/lib/rdk").mkdir(parents=True, exist_ok=True)
            for path in (Path("/lib/rdk/imageFlasher.sh"), Path("/rebootNow.sh")):
                path.write_text(
                    '#!/bin/sh\nprintf "%s\\n" "$0 $*" >> '
                    '/tmp/legacy_remote_unexpected_platform_call\nexit 1\n'
                )
                path.chmod(0o755)
            Path("/etc/device.properties").write_text(
                "DEVICE_NAME=DEV_CONTAINER\nDEVICE_TYPE=mediaclient\n"
                "DIFW_PATH=/opt/CDL\nENABLE_MAINTENANCE=false\nMODEL_NUM=ABCD\n"
                "ENABLE_SOFTWARE_OPTOUT=false\nBUILD_TYPE=VBN\n"
                "ESTB_INTERFACE=eth0\nPDRI_ENABLED=false\nCPU_ARCH=arm\n"
            )
            Path("/version.txt").write_text("imagename:ABCD_firmware_test.bin\n")
            Path("/tmp/route_available").touch()
            Path("/tmp/stt_received").touch()
            Path("/tmp/.GatewayIP_dfltroute").write_text("IPV4 8.8.4.4\n")
            Path("/tmp/.estb_mac").write_text("01:23:45:67:89:ab\n")
            Path("/opt/partnerid").write_text("default-parter\n")

            def execute(scenario, current_first="1.0.0.0", current_second="1.0.0.0",
                        previous=(), capacity_case=False):
                url = urls.get(scenario)
                assert isinstance(url, str) and url.startswith("https://"), (
                    f"Configure HTTPS MockXConf URL for scenario {scenario}"
                )
                Path("/opt/swupdate.conf").write_text(url + "\n")
                remotes = [
                    {"Product": "AB11-20", "FwVer": current_first},
                    {"Product": "CD15-10", "FwVer": current_second},
                ]
                if capacity_case:
                    remotes = [{"Product": product, "FwVer": "1.0.0.0"} for product in ("A", "B")]
                Path("/tmp/rc-proxy-params.json").write_text(json.dumps(remotes))
                if previous:
                    DOWNLOAD_LIST.write_text(",".join(name + ".tgz" for name in previous))
                environment = os.environ.copy()
                environment.pop("RDKFW_FORCE_DIRECTCDN", None)
                assert read_boolean_rfc(DIRECT_CDN_RFC) == "false"
                result = subprocess.run(
                    [binary, "0", "1"], capture_output=True, text=True,
                    timeout=300, env=environment, check=False,
                )
                logs = "\n".join(path.read_text(errors="replace") for path in LOGS if path.exists())
                evidence = result.stdout + "\n" + result.stderr + "\n" + logs
                print(f"\nScenario: {scenario}\nReturn code: {result.returncode}\n{evidence}")
                assert result.returncode >= 0 and result.returncode < 128, evidence
                assert "Triggering Peripheral Download cloudFWLocation:" in evidence, evidence
                installed = "".join(
                    remote["Product"] + "_firmware_" + remote["FwVer"] + ".tgz,"
                    for remote in remotes
                )
                assert "GetRemoteVers found cCurVerBuf = " + installed in evidence, evidence
                response_names = (FIRST, SECOND)
                if scenario in ("middle_404", "middle_curl_failure"):
                    response_names = (FIRST, FAILED, SECOND)
                if capacity_case:
                    response_names = CAPACITY_NAMES
                assert "peripheralFirmwares: " + ",".join(response_names) in evidence, evidence
                assert "DirectCDNDownload:" not in evidence, evidence
                assert "Image Flashing is success" not in evidence, "Unexpected main image flash"
                assert not Path("/tmp/legacy_remote_unexpected_platform_call").exists(), (
                    "Unexpected image flash or reboot command: " + evidence
                )
                recorded = DOWNLOAD_LIST.read_text() if DOWNLOAD_LIST.exists() else ""
                return evidence, recorded

            yield execute, payload


def assert_archive(name, payload):
    path = FIRMWARE_DIR / (name + ".tgz")
    assert path.is_file(), f"Expected downloaded archive: {path}"
    actual = path.read_bytes()
    assert actual == payload, f"Archive differs from configured fixture: {path}"
    print(f"Verified {path}: {len(actual)} bytes sha256={hashlib.sha256(actual).hexdigest()}")


def assert_handoff_if_available(evidence, names):
    has_iarm = "current event=PeripheralUpgradeEvent" in evidence
    if os.environ.get("RDKFW_REQUIRE_REMOTE_EVENT") == "1":
        assert has_iarm, "IARM PeripheralUpgradeEvent is required for this run: " + evidence
    if has_iarm:
        payloads = re.findall(r"event_status = /opt/CDL:([^\s]+)", evidence)
        assert names in payloads, evidence
    else:
        print("IARM handoff not exposed by this build; physical remote OTA is NOT verified")


def test_legacy_two_paired_remotes_download_both_versions(legacy_remote_lab):
    execute, payload = legacy_remote_lab
    evidence, recorded = execute("two_upgrades")
    assert recorded == FIRST + ".tgz," + SECOND + ".tgz", evidence
    assert "After Trigger Peripheral Download status=0" in evidence, evidence
    assert_archive(FIRST, payload)
    assert_archive(SECOND, payload)
    assert_handoff_if_available(evidence, recorded)


def test_legacy_separate_product_response_keys_download_both_remotes(legacy_remote_lab):
    execute, payload = legacy_remote_lab
    evidence, recorded = execute("split_product_keys")
    assert recorded == FIRST + ".tgz," + SECOND + ".tgz", evidence
    assert "After Trigger Peripheral Download status=0" in evidence, evidence
    assert_archive(FIRST, payload)
    assert_archive(SECOND, payload)
    assert_handoff_if_available(evidence, recorded)


@pytest.mark.parametrize("current_first", ["2.0.0.0", "9.0.0.0"])
def test_legacy_current_remote_is_skipped_other_remote_downloads(legacy_remote_lab, current_first):
    execute, payload = legacy_remote_lab
    evidence, recorded = execute("two_upgrades", current_first=current_first)
    assert recorded == SECOND + ".tgz", evidence
    assert "The version in the device is newer or same, no upgrade needed" in evidence, evidence
    assert not (FIRMWARE_DIR / (FIRST + ".tgz")).exists(), evidence
    assert f"Requesting upgrade to /opt/CDL/{FIRST}.tgz" not in evidence, evidence
    assert_archive(SECOND, payload)
    assert_handoff_if_available(evidence, recorded)


def test_legacy_both_remotes_current_downloads_nothing(legacy_remote_lab):
    execute, _ = legacy_remote_lab
    evidence, recorded = execute(
        "two_upgrades", current_first="2.0.0.0", current_second="2.0.0.0"
    )
    assert recorded == "", evidence
    assert not DOWNLOAD_LIST.exists(), evidence
    assert "After Trigger Peripheral Download status=0" in evidence, evidence
    assert "Requesting upgrade to /opt/CDL/" not in evidence, evidence
    assert not (FIRMWARE_DIR / (FIRST + ".tgz")).exists(), evidence
    assert not (FIRMWARE_DIR / (SECOND + ".tgz")).exists(), evidence


def test_legacy_previously_downloaded_remote_is_not_downloaded_again(legacy_remote_lab):
    execute, payload = legacy_remote_lab
    evidence, recorded = execute("two_upgrades", previous=(FIRST,))
    assert "Prev downloaded FW and requested cloud FW download versions are the same" in evidence, evidence
    assert "After Trigger Peripheral Download status=0" in evidence, evidence
    assert not (FIRMWARE_DIR / (FIRST + ".tgz")).exists(), evidence
    assert f"Requesting upgrade to /opt/CDL/{FIRST}.tgz" not in evidence, evidence
    assert recorded == FIRST + ".tgz," + SECOND + ".tgz", (
        "Expected a comma between existing and newly downloaded records; " + evidence
    )
    assert_archive(SECOND, payload)
    assert_handoff_if_available(evidence, SECOND + ".tgz")


@pytest.mark.parametrize("scenario", ["middle_404", "middle_curl_failure"])
def test_legacy_failed_middle_version_does_not_block_second_remote(legacy_remote_lab, scenario):
    execute, payload = legacy_remote_lab
    evidence, recorded = execute(scenario)
    assert "After Trigger Peripheral Download status=-1" in evidence, evidence
    assert recorded == FIRST + ".tgz," + SECOND + ".tgz", evidence
    assert FAILED + ".tgz" not in recorded, evidence
    assert_archive(FIRST, payload)
    assert_archive(SECOND, payload)
    assert_handoff_if_available(evidence, recorded)


def test_legacy_long_response_rejects_excess_complete_record(legacy_remote_lab):
    execute, _ = legacy_remote_lab
    evidence, recorded = execute("capacity", capacity_case=True)
    expected_names = []
    for name in CAPACITY_NAMES:
        candidate = ",".join(expected_names + [name + ".tgz"])
        if len(candidate.encode("ascii")) >= 256:
            break
        expected_names.append(name + ".tgz")
    assert recorded == ",".join(expected_names), evidence
    assert len(recorded.encode("ascii")) < 256, evidence
    assert "Peripheral download list exceeds 256 bytes" in evidence, evidence
    assert "After Trigger Peripheral Download status=-1" in evidence, evidence
    rejected = CAPACITY_NAMES[len(expected_names)]
    assert not (FIRMWARE_DIR / (rejected + ".tgz")).exists(), evidence
    assert f"Requesting upgrade to /opt/CDL/{rejected}.tgz" not in evidence, evidence
    assert_handoff_if_available(evidence, recorded)