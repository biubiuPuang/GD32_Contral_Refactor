"""Check both demos without changing User/main.c or using a board.

Run: python 211_Project/wu-xian-App/validation/check_demo.py
Uses the local Keil compiler for complete demo builds, plus GCC for API simulations.
All build outputs stay in the corresponding wu-xian-App/validation directory.
"""
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess
import xml.etree.ElementTree as ET
from check_frequencies import check as check_frequencies

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
UV4 = Path(r"D:\_Keil5\UV4\UV4.exe")
GCC = Path(r"D:\1_Sofrware\_MinGW\mingw64\bin\gcc.exe")


def check_inputs():
    for relative, expected in json.loads((HERE / "protected_hashes.json").read_text(encoding="utf-8")).items():
        assert hashlib.sha256((ROOT / relative).read_bytes()).hexdigest() == expected, relative
    for side in ("211", "221"):
        app = ROOT / f"{side}_Project/wu-xian-App"
        exp = ROOT / f"资料/CMT2119B及CMT2219B_FRPDK参数在这/{side}_config.exp"
        exported = [int(v, 16) for _, v in re.findall(
            r"^0x([0-9a-fA-F]{2})\s+0x([0-9a-fA-F]{2})\s*$", exp.read_text(), re.M)]
        header = (app / "wireless_demo_params.h").read_text(encoding="utf-8")
        array = header.split("= {", 1)[1].split("};", 1)[0]
        generated = [int(v, 16) for v in re.findall(r"0x([0-9a-fA-F]{2})U", array)]
        assert exported == generated and len(generated) == 96, side
        assert hashlib.sha256(exp.read_bytes()).hexdigest() in header
    print("Original main.c / drivers / .exp unchanged; both parameter arrays match all 96 bytes.", flush=True)


def simulate(side):
    app = ROOT / f"{side}_Project/wu-xian-App"
    output = app / "validation"
    output.mkdir(exist_ok=True)
    executable = output / "simulate.exe"
    command = [str(GCC), "-std=c99", "-Wall", "-Wextra", "-Werror", "-O0",
               f'-DAPP_SOURCE="{(app / "wireless_demo.c").as_posix()}"',
               "-I" + str(HERE / "mock"), "-I" + str(app),
               "-I" + str(ROOT / f"{side}_Project/wu-xian_interface"),
               "-I" + str(ROOT / f"{side}_Project/User"),
               str(HERE / "simulate_multi.c"), "-o", str(executable)]
    subprocess.run(command, check=True, timeout=60)
    subprocess.run([str(executable)], check=True, timeout=10)


def build_demo(side):
    project = ROOT / f"{side}_Project/Project/GD32E230C8T6.uvprojx"
    app = ROOT / f"{side}_Project/wu-xian-App"
    output = app / "validation"
    output.mkdir(exist_ok=True)
    tree = ET.parse(project)
    for node in tree.findall(".//FilePath"):
        source = (project.parent / node.text).resolve()
        if source == ROOT / f"{side}_Project/User/main.c":
            source = app / "main_example.c"
        assert source.is_file(), source
        node.text = str(source)
    for node in tree.findall(".//File"):
        if Path(node.findtext("FilePath", "")).name == "main_example.c":
            node.find("FileName").text = "main_example.c"
    for node in tree.findall(".//IncludePath"):
        if node.text:
            node.text = ";".join(str((project.parent / p).resolve()) for p in node.text.split(";") if p)
    tree.find(".//OutputDirectory").text = str(output / "Objects") + "\\"
    tree.find(".//ListingPath").text = str(output / "Listings") + "\\"
    tree.find(".//OutputName").text = f"wireless_{side}_demo"
    clone = output / "demo_validation.uvprojx"
    tree.write(clone, encoding="utf-8", xml_declaration=True)
    log = output / "build.log"
    result = subprocess.run([str(UV4), "-r", str(clone), "-t", "Target 1", "-j0", "-o", str(log)],
                            cwd=output, timeout=180,
                            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
    text = log.read_bytes().decode("utf-8", errors="replace") if log.exists() else "No Keil log"
    (output / "build_summary.txt").write_text(text, encoding="utf-8")
    summary = [line for line in text.splitlines() if any(x in line for x in ("Error(s)", "Warning(s)", "Program Size", "error:", "warning:"))]
    print(side, "Keil exit:", result.returncode, "\n" + "\n".join(summary), flush=True)
    assert result.returncode == 0 and re.search(r"0 Error\(s\), 0 Warning\(s\)", text), str(log)
    assert (output / f"Objects/wireless_{side}_demo.hex").is_file()
    from intelhex import IntelHex
    firmware = IntelHex(str(output / f"Objects/wireless_{side}_demo.hex"))
    assert max(firmware.addresses()) < 0x0800F800, "Firmware overlaps configuration journal"


if __name__ == "__main__":
    check_frequencies()
    check_inputs()
    for target in ("211", "221"):
        simulate(target)
        build_demo(target)
    check_inputs()
    print("Both full demo builds and both application simulations PASS.", flush=True)
