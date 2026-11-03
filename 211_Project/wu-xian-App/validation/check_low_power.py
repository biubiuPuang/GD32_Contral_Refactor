"""Build/test the complete LP180 sender. Does not open serial ports or flash boards."""
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
PROJECT = ROOT / "211_Project/Project/GD32E230C8T6.uvprojx"
GCC = Path(r"D:\1_Sofrware\_MinGW\mingw64\bin\gcc.exe")
UV4 = Path(r"D:\_Keil5\UV4\UV4.exe")


def protected_snapshot():
    """Snapshot existing BSP, firmware, receiver and project configuration."""
    paths = set()
    for name in ("211_Project/Firmware", "211_Project/Hardware",
                 "211_Project/wu-xian_interface", "221_Project"):
        paths.update(p for p in (ROOT / name).rglob("*")
                     if p.is_file() and p.suffix.lower() in (".c", ".h", ".s", ".uvprojx", ".exp"))
    paths.update(p for p in (ROOT / "211_Project/User").glob("*")
                 if p.is_file() and p.name != "main.c")
    paths.add(PROJECT)
    return {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(paths)}


def check_project_and_parameters():
    import check_sender
    check_sender.project_check()
    check_sender.parameters()
    main = (ROOT / "211_Project/User/main.c").read_text(encoding="utf-8")
    assert main.index("Demo_LowPowerProcess();") < main.index("Demo_WatchdogFeed();")
    assert "fwdgt_enable();" in main
    files = ET.parse(PROJECT).findall(".//File[FileName='wireless_demo.c']")
    assert len(files) == 1


def simulate():
    executable = HERE / "simulate_low_power.exe"
    command = [str(GCC), "-std=c99", "-Wall", "-Wextra", "-Werror", "-O0",
               f'-DAPP_SOURCE="{(APP / "wireless_demo.c").as_posix()}"',
               "-I" + str(HERE / "mock"), "-I" + str(APP),
               "-I" + str(ROOT / "211_Project/wu-xian_interface"),
               "-I" + str(ROOT / "211_Project/User"),
               str(HERE / "simulate_low_power.c"), "-o", str(executable)]
    subprocess.run(command, check=True, timeout=30)
    result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=30)
    print(result.stdout, end="", flush=True)
    if result.stderr:
        print(result.stderr, end="", flush=True)
    result.check_returncode()
    return result.stdout.splitlines()


def build():
    log = HERE / "low_power_build.log"
    result = subprocess.run([str(UV4), "-r", str(PROJECT), "-j0", "-o", str(log)],
                            cwd=PROJECT.parent, timeout=180,
                            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
    output = log.read_bytes().decode("utf-8", errors="replace")
    assert result.returncode == 0 and re.search(r"0 Error\(s\), 0 Warning\(s\)", output), output
    image = PROJECT.parent / "Objects/GD32E230C8T6.hex"
    base, first, last = 0, 0xFFFFFFFF, 0
    for line in image.read_text().splitlines():
        data = bytes.fromhex(line[1:])
        assert sum(data) & 0xFF == 0, "Invalid Intel HEX checksum"
        length, address, kind = data[0], int.from_bytes(data[1:3], "big"), data[3]
        if kind == 4:
            base = int.from_bytes(data[4:6], "big") << 16
        elif kind == 2:
            base = int.from_bytes(data[4:6], "big") << 4
        elif kind == 0:
            first = min(first, base + address)
            last = max(last, base + address + length)
    assert first >= 0x08000000 and last <= 0x0800F800, "Pairing Flash overlap"
    mapping = (PROJECT.parent / "Listings/GD32E230C8T6.map").read_text(errors="replace")
    for handler in ("RTC_IRQHandler", "EXTI2_3_IRQHandler", "EXTI4_15_IRQHandler"):
        assert f"startup_gd32e23x.o(RESET) refers to wireless_demo.o(.text.{handler})" in mapping
    size = re.search(r"Program Size:.*", output).group(0)
    print(size, flush=True)
    print("Keil: 0 errors, 0 warnings; all wake vectors linked; pairing Flash excluded.", flush=True)
    return {"errors": 0, "warnings": 0, "program_size": size,
            "hex_end_exclusive": hex(last), "hex_sha256": hashlib.sha256(image.read_bytes()).hexdigest()}


def main():
    before = protected_snapshot()
    check_project_and_parameters()
    tests = simulate()
    summary = build()
    assert before == protected_snapshot(), "A protected file changed during validation"
    summary.update({"version": "V5 F50 LP180", "idle_seconds": 180,
                    "wake_inputs": ["PB3", "PB4", "PB5", "PB6", "PA8", "PA9", "PA10"],
                    "rtc_interval_seconds_nominal": 1, "simulated_sleep_seconds": 3600,
                    "hardware_accessed": False, "board_flashed": False,
                    "protected_files_unchanged_during_validation": len(before),
                    "tests": tests,
                    "sources": {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                                for p in [APP / "wireless_demo.c", APP / "wireless_demo.h",
                                          ROOT / "211_Project/User/main.c"]}})
    (HERE / "low_power_summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"PASS: {len(before)} protected files unchanged during validation; no board flashed.", flush=True)


if __name__ == "__main__":
    main()
