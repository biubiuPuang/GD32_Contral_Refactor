"""Current wired + A2 wireless receiver validation; no board access/flashing."""
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess
import xml.etree.ElementTree as ET

HERE = Path(__file__).resolve().parent
APP = HERE.parents[1]
ROOT = APP.parents[1]
WIRED = ROOT / "221_Project/you-xian_interface"
PROJECT = ROOT / "221_Project/Project/GD32E230C8T6.uvprojx"
GCC = Path(r"D:\1_Sofrware\_MinGW\mingw64\bin\gcc.exe")
UV4 = Path(r"D:\_Keil5\UV4\UV4.exe")


def protected():
    checks = json.loads((HERE / "protected_hashes.json").read_text(encoding="utf-8"))
    for relative, expected in checks.items():
        assert hashlib.sha256((ROOT / relative).read_bytes()).hexdigest() == expected, relative
    return len(checks)


def project_check():
    tree = ET.parse(PROJECT)
    for name in ("wireless_demo.c", "receiver_app.c", "wired_control.c", "wired_port.c"):
        nodes = tree.findall(f".//File[FileName='{name}']")
        assert len(nodes) == 1 and nodes[0].findtext(".//IncludeInBuild", "1") != "0", name
    for node in tree.findall(".//FilePath"):
        assert (PROJECT.parent / node.text).resolve().is_file(), node.text
    main = (ROOT / "221_Project/User/main.c").read_text(encoding="utf-8")
    assert "ReceiverApp_Init();" in main and "ReceiverApp_Process();" in main
    assert main == (APP / "main_example.c").read_text(encoding="utf-8")
    for name in ("wireless_demo.h",):
        tx = (ROOT / "211_Project/wu-xian-App" / name).read_text(encoding="utf-8")
        rx = (APP / name).read_text(encoding="utf-8")
        for macro in ("WIRELESS_CONTROL_TAG", "WIRELESS_CMD_STOP", "WIRELESS_CMD_UP",
                      "WIRELESS_CMD_DOWN", "WIRELESS_CMD_FAST", "WIRELESS_CMD_UP_FAST", "WIRELESS_CMD_DOWN_FAST"):
            pattern = rf"^#define\s+{macro}\s+(.+)$"
            assert re.search(pattern, tx, re.M).group(1) == re.search(pattern, rx, re.M).group(1), macro
    for side in ("211", "221"):
        exp = ROOT / f"资料/CMT2119B及CMT2219B_FRPDK参数在这/{side}_config.exp"
        values = [int(v, 16) for _, v in re.findall(
            r"^0x([0-9a-fA-F]{2})\s+0x([0-9a-fA-F]{2})\s*$", exp.read_text(), re.M)]
        header = (ROOT / f"{side}_Project/wu-xian-App/wireless_demo_params.h").read_text(encoding="utf-8")
        array = header.split("= {", 1)[1].split("};", 1)[0]
        assert len(values) == 96 and values == [int(v, 16) for v in re.findall(r"0x([0-9a-fA-F]{2})U", array)]


def simulate(name, source, includes):
    executable = HERE / (name + ".exe")
    cmd = [str(GCC), "-std=c99", "-Wall", "-Wextra", "-Werror", "-O2"]
    cmd += ["-I" + str(p) for p in includes]
    cmd += [str(WIRED / "wired_control.c"), str(source), "-o", str(executable)]
    subprocess.run(cmd, check=True, timeout=30)
    result = subprocess.run([str(executable)], check=True, timeout=20, capture_output=True, text=True)
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
    assert min(image.addresses()) >= 0x08000000 and max(image.addresses()) < 0x0800F800
    map_text = (PROJECT.parent / "Listings/GD32E230C8T6.map").read_text(encoding="utf-8", errors="replace")
    assert re.search(r"USART1_IRQHandler\s+0x[0-9a-f]+.*wireless_demo\.o", map_text, re.I)
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
    results = [
        simulate("wired_logic_regression", WIRED / "validation/test_logic.c", [WIRED]),
        simulate("wired_port_regression", WIRED / "validation/test_port.c", [WIRED / "validation/mock", WIRED]),
        simulate("test_receiver", HERE / "test_receiver.c",
                 [HERE / "mock", APP, WIRED, ROOT / "221_Project/wu-xian_interface", ROOT / "221_Project/User"]),
    ]
    summary = build()
    project_check()
    assert protected() == count
    summary.update({"protected_files": count, "hardware_tested": False,
                    "simultaneous_wired_wireless_arbitration_implemented": False,
                    "protocol": "A2", "wireless_timeout_ms": 85, "tests": results,
                    "sources": {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                                for folder in (APP, WIRED) for p in folder.glob("*") if p.suffix in (".c", ".h")}})
    (HERE / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"PASS: {count} protected source files unchanged; original 221 project built, 211 unchanged, no board flashed.", flush=True)
