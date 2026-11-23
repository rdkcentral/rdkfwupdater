import os
from pathlib import Path
import subprocess

import pytest


@pytest.mark.parametrize("directcdn", [False, True], ids=["standard", "directcdn"])
def test_real_cli_rejects_oversized_request_before_transport(directcdn, tmp_path):
    probe_name = os.environ.get("RDKFW_REQUEST_PROBE")
    assert probe_name, "Set RDKFW_REQUEST_PROBE to the compiled xconf_request_probe.so"
    probe = Path(probe_name).resolve()
    assert probe.is_file(), f"Probe not found: {probe}"
    binary = os.environ.get("RDKFW_REQUEST_BINARY", "/usr/local/bin/rdkvfwupgrader")
    assert Path(binary).is_file(), f"Updater not found: {binary}"
    environment = os.environ.copy()
    environment["LD_PRELOAD"] = str(probe)
    environment["RDKFW_REQUEST_DIRECTCDN"] = "1" if directcdn else "0"

    for case in ["normal", "overflow"]:
        environment["RDKFW_REQUEST_CASE"] = case
        result = subprocess.run(
            [binary, "0", "1"], env=environment,
            capture_output=True, text=True, errors="replace", timeout=30, check=False,
        )
        output = result.stdout + result.stderr
        (tmp_path / f"{case}.log").write_text(output)
        assert result.returncode >= 0, output
        expected_route = "directcdn" if directcdn else "standard"
        assert f"REQUEST_PROBE route={expected_route}" in output, \
            f"Requested route was not exercised:\n{output}"
        if case == "normal":
            assert "REQUEST_PROBE build_failed=0 empty=0" in output, output
            assert "REQUEST_PROBE transport" in output, output
        else:
            assert "REQUEST_PROBE build_failed=1 empty=1" in output, output
            assert "REQUEST_PROBE transport" not in output, output
            assert result.returncode != 0, output