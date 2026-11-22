import json
import os
from pathlib import Path
import subprocess

import pytest


CASES = [
    pytest.param(b"TRUE", False, id="true"),
    pytest.param(b"TRUE\n", False, id="true-lf"),
    pytest.param(b"TRUE\r\n", False, id="true-crlf"),
    pytest.param(b"CANARY", True, id="canary"),
    pytest.param(b"CANARY\n", True, id="canary-lf"),
    pytest.param(b"CANARY\r\n", True, id="canary-crlf"),
    pytest.param(None, False, id="missing"),
    pytest.param(b"", False, id="empty"),
    pytest.param(b"FALSE", False, id="false"),
    pytest.param(b"canary", False, id="lowercase"),
    pytest.param(b" CANARY", False, id="leading-space"),
    pytest.param(b"CANARY ", False, id="trailing-space"),
    pytest.param(b"CANARY x", False, id="extra-token"),
    pytest.param(b"CANARY\nTRUE", False, id="extra-line"),
    pytest.param(b"CANARY\n\n", False, id="double-lf"),
    pytest.param(b"CANARY\r", False, id="bare-cr"),
    pytest.param(b"CANARY\0", False, id="embedded-nul"),
    pytest.param(b"CANARYxxx", False, id="nine-bytes"),
    pytest.param(b"A" * 64, False, id="oversized"),
]


@pytest.fixture(scope="module")
def state_driver():
    driver = Path(os.environ.get("RDKFW_STATE_DRIVER", "./xconf_state_driver")).resolve()
    library_name = os.environ.get("RDKFW_FLASH_LIBRARY")
    assert driver.is_file(), "Build xconf_state_driver using the RDKFW-002 test plan"
    assert library_name, "Set RDKFW_FLASH_LIBRARY to the freshly built production librdksw_flash.so"
    library = Path(library_name).resolve()
    assert library.is_file(), f"Flash library not found: {library}"
    return driver, library


@pytest.mark.parametrize("contents,canary", CASES)
@pytest.mark.parametrize("entry_point", ["flash", "post"])
def test_xconf_state_rejection_rules(state_driver, tmp_path, contents, canary, entry_point):
    driver, library = state_driver
    if contents is not None:
        (tmp_path / "xconfchecknow_val").write_bytes(contents)
    completed = subprocess.run(
        [str(driver), str(library), str(tmp_path), entry_point],
        capture_output=True, text=True, timeout=15, check=False,
    )
    assert completed.returncode == 0, completed.stdout + completed.stderr
    lines = [line for line in completed.stdout.splitlines() if line.startswith("XCONF_STATE_RESULT ")]
    assert len(lines) == 1, completed.stdout + completed.stderr
    observed = json.loads(lines[0].removeprefix("XCONF_STATE_RESULT "))
    assert observed["result"] == 0
    assert observed["preparing"] == (0 if canary else 1)
    assert observed["critical"] == (0 if canary or entry_point == "post" else 1)
    assert observed["flash"] == (1 if entry_point == "flash" else 0)
    assert observed["status"] == (1 if entry_point == "flash" else 0)
    assert observed["cleanup"] == (1 if entry_point == "flash" else 0)
    if contents is not None:
        assert (tmp_path / "xconfchecknow_val").read_bytes() == contents
    else:
        assert not (tmp_path / "xconfchecknow_val").exists()