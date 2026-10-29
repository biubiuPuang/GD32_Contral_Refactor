"""Validate current TX211 only; preserve the complete wired RX221 project.

No hardware access or flashing. The separate old 221 wireless parser is only
host-tested to ensure it rejects the new A2 frames; it is not built/flashed.
"""
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
PROJECT = ROOT / "211_Project/Project/GD32E230C8T6.uvprojx"


def protected():
    checks = json.loads((HERE / "sender_protected_hashes.json").read_text(encoding="utf-8"))
    for relative, expected in checks.items():
        assert hashlib.sha256((ROOT / relative).read_bytes()).hexdigest() == expected, relative
    return len(checks)


def project_check():
    tree = ET.parse(PROJECT)
    files = tree.findall(".//File[FileName='wireless_demo.c']")
    assert len(files) == 1 and files[0].findtext(".//IncludeInBuild", "1") != "0"
    main = tree.find(".//File[FileName='main.c']/FilePath").text
    main_path = (PROJECT.parent / main).resolve()
    assert main_path == ROOT / "211_Project/User/main.c"
    assert "WirelessDemo_Init();" in main_path.read_text(encoding="utf-8", errors="replace")


def parameters():
    exp = ROOT / "资料/CMT2119B及CMT2219B_FRPDK参数在这/211_config.exp"
    header = (APP / "wireless_demo_params.h").read_text(encoding="utf-8")
    values = [int(v, 16) for _, v in re.findall(
        r"^0x([0-9a-fA-F]{2})\s+0x([0-9a-fA-F]{2})\s*$", exp.read_text(), re.M)]
    array = header.split("= {", 1)[1].split("};", 1)[0]
    compiled = [int(v, 16) for v in re.findall(r"0x([0-9a-fA-F]{2})U", array)]
    assert len(values) == 96 and compiled == values


def simulate(side):
    target = ROOT / f"{side}_Project"
    source = target / "wu-xian-App/wireless_demo.c"
    executable = HERE / f"simulate_{side}_v3.exe"
    command = [str(GCC), "-std=c99", "-Wall", "-Wextra", "-Werror", "-O0",
               f'-DAPP_SOURCE="{source.as_posix()}"', "-I" + str(HERE / "mock"),
               "-I" + str(source.parent), "-I" + str(target / "wu-xian_interface"),
               "-I" + str(target / "User"), str(HERE / "simulate_multi.c"), "-o", str(executable)]
    subprocess.run(command, check=True, timeout=30)
    result = subprocess.run([str(executable)], check=True, timeout=15, capture_output=True, text=True)
    print(result.stdout.strip(), flush=True)
    return result.stdout.strip()


def build():
    log = HERE / "sender_v3_build.log"
    result = subprocess.run([str(UV4), "-r", str(PROJECT), "-j0", "-o", str(log)],
                            cwd=PROJECT.parent, timeout=180,
                            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
    text = log.read_bytes().decode("utf-8", errors="replace")
    assert result.returncode == 0 and re.search(r"0 Error\(s\), 0 Warning\(s\)", text), text
    from intelhex import IntelHex
    hex_path = PROJECT.parent / "Objects/GD32E230C8T6.hex"
    image = IntelHex(str(hex_path))
    assert min(image.addresses()) >= 0x08000000
    assert max(image.addresses()) < 0x0800F800, "Image overlaps persistent pairing configuration"
    for line in text.splitlines():
        if "Program Size" in line or "Error(s)" in line:
            print(line, flush=True)
    return {
        "errors": 0, "warnings": 0,
        "program_size": re.search(r"Program Size:.*", text).group(0).strip(),
        "hex_end_exclusive": hex(max(image.addresses()) + 1),
        "hex_sha256": hashlib.sha256(hex_path.read_bytes()).hexdigest(),
    }


if __name__ == "__main__":
    count = protected()
    project_check()
    parameters()
    results = [simulate("211"), simulate("221")]
    summary = build()
    project_check()
    assert protected() == count
    summary.update({"protected_files": count, "hardware_tested": False,
                    "protocol": "A2", "commands": {"STOP": 0, "UP": 1, "DOWN": 2, "UP_FAST": 5, "DOWN_FAST": 6},
                    "tests": results,
                    "sources": {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                                for p in APP.glob("*") if p.suffix in (".c", ".h")}})
    (HERE / "sender_v3_summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"PASS: {count} protected files unchanged, original 211 Keil project built; 221 project untouched, no board flashed.", flush=True)
