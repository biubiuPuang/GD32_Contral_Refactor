"""Run from the repository root. No hardware access, no IDE mouse automation."""
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess
import xml.etree.ElementTree as ET

HERE = Path(__file__).resolve().parent
APP = HERE.parent
ROOT = APP.parents[1]
GCC = Path(r"D:\1_Sofrware\_MinGW\mingw64\bin\gcc.exe")
UV4 = Path(r"D:\_Keil5\UV4\UV4.exe")
PROJECT = ROOT / "221_Project/Project/GD32E230C8T6.uvprojx"


def protected():
    hashes = json.loads((HERE / "protected_hashes.json").read_text(encoding="utf-8"))
    for relative, expected in hashes.items():
        assert hashlib.sha256((ROOT / relative).read_bytes()).hexdigest() == expected, relative
    return len(hashes)


def project_check():
    tree = ET.parse(PROJECT)
    assert r"..\you-xian_interface" in tree.findtext(".//Cads/VariousControls/IncludePath").split(";")
    for file in tree.findall(".//File"):
        assert (PROJECT.parent / file.findtext("FilePath")).resolve().is_file()
    sources = tree.findall(".//File[FileName='wireless_demo.c']")
    assert len(sources) == 1
    assert sources[0].findtext(".//IncludeInBuild") == "0"
    group = tree.find(".//Group[GroupName='you-xian_interface']")
    assert group is not None
    assert {f.findtext("FileName") for f in group.findall(".//File")} == {
        "wired_control.c", "wired_port.c", "wired_control.h", "wired_config.h"}
    main = (ROOT / "221_Project/User/main.c").read_text(encoding="utf-8")
    assert "WiredControl_Init();" in main and "WiredControl_Process();" in main
    assert "WirelessDemo_Init" not in main


def simulate(name, mock=False):
    executable = HERE / (name + ".exe")
    cmd = [str(GCC), "-std=c99", "-Wall", "-Wextra", "-Werror", "-O2"]
    if mock:
        cmd += ["-I" + str(HERE / "mock")]
    cmd += ["-I" + str(APP), str(APP / "wired_control.c"), str(HERE / (name + ".c")), "-o", str(executable)]
    subprocess.run(cmd, check=True, timeout=30)
    result = subprocess.run([str(executable)], check=True, timeout=15, capture_output=True, text=True)
    print(result.stdout.strip(), flush=True)
    return result.stdout.strip()


def build():
    log = HERE / "build.log"
    result = subprocess.run([str(UV4), "-r", str(PROJECT), "-j0", "-o", str(log)],
                            cwd=PROJECT.parent, timeout=180,
                            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
    text = log.read_bytes().decode("utf-8", errors="replace")
    assert result.returncode == 0 and re.search(r"0 Error\(s\), 0 Warning\(s\)", text), text
    from intelhex import IntelHex
    hex_path = PROJECT.parent / "Objects/GD32E230C8T6.hex"
    image = IntelHex(str(hex_path))
    # Preserve the region already used by the radio demo configuration journal.
    assert min(image.addresses()) >= 0x08000000
    assert max(image.addresses()) < 0x0800F800
    for line in text.splitlines():
        if "Program Size" in line or "Error(s)" in line:
            print(line, flush=True)
    return {
        "errors": 0, "warnings": 0,
        "program_size": re.search(r"Program Size:.*", text).group(0),
        "hex_end_exclusive": hex(max(image.addresses()) + 1),
        "hex_sha256": hashlib.sha256(hex_path.read_bytes()).hexdigest(),
    }


if __name__ == "__main__":
    count = protected()
    project_check()
    results = [simulate("test_logic"), simulate("test_port", mock=True)]
    summary = build()
    assert protected() == count
    summary.update({"protected_files": count, "hardware_tested": False, "tests": results,
                    "sources": {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                                for p in APP.glob("*") if p.suffix in (".c", ".h")}})
    (HERE / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"PASS: {count} protected files unchanged; original 221 Keil project built; no board flashed.", flush=True)
