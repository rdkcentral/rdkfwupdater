import subprocess
import re

def test_getPDRIFileName_MFR():
    cmd = ["/usr/bin/test_getPDRIFileNameMFR"]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    print(proc.stdout)
    assert proc.returncode == 0, "GetPDRIFileNameUsingMFR test failed"
    assert re.search(r"SUCCESS", proc.stdout), "Did not find success message"
