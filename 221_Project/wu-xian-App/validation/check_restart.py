"""Current two-board restart regression. Host mocks + optional Keil rebuild.

No serial access/flashing. Historical check_receiver.protected/project_check
belong to an older source snapshot/main example and are not used by this entry.
"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import subprocess
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[3]
TX = ROOT / "211_Project"
RX = ROOT / "221_Project"
GCC = Path(r"D:\1_Sofrware\_MinGW\mingw64\bin\gcc.exe")


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def source_hashes():
    return {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
            for root in (TX, RX) for p in root.rglob("*")
            if p.is_file() and p.suffix.lower() in (".c", ".h", ".s", ".uvprojx", ".exp")}


def project_check():
    for project in (TX, RX):
        file = project / "Project/GD32E230C8T6.uvprojx"
        tree = ET.parse(file)
        for node in tree.findall(".//FilePath"):
            assert (file.parent / node.text).resolve().is_file(), node.text
        names = ["wireless_demo.c"]
        if project == RX:
            names += ["receiver_app.c", "wired_control.c", "wired_port.c"]
        for name in names:
            nodes = tree.findall(f".//File[FileName='{name}']")
            assert len(nodes) == 1 and nodes[0].findtext(".//IncludeInBuild", "1") != "0", name
        main = (project / "User/main.c").read_text(encoding="utf-8")
        assert "fwdgt_enable();" in main
        if project == RX:
            for call in ("ReceiverApp_Init();", "ReceiverApp_SyncRemote();",
                         "WiredControl_Process();", "WirelessDemo_Process();"):
                assert call in main, call


def simulate(name, sources, includes, output, definitions=()):
    executable = output / (name + ".exe")
    cmd = [str(GCC), "-std=c99", "-Wall", "-Wextra", "-Werror", "-O0"]
    cmd += list(definitions) + ["-I" + str(p) for p in includes]
    cmd += [str(p) for p in sources] + ["-o", str(executable)]
    subprocess.run(cmd, check=True, timeout=60)
    result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=60)
    (output / (name + ".log")).write_text(result.stdout + result.stderr, encoding="utf-8")
    print(result.stdout, end="", flush=True)
    if result.stderr:
        print(result.stderr, end="", flush=True)
    result.check_returncode()
    return result.stdout.splitlines()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path(__file__).with_name("restart_results"))
    parser.add_argument("--no-build", action="store_true", help="Run host checks without Keil rebuild")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    before = source_hashes()
    project_check()
    tx_validation = TX / "wu-xian-App/validation"
    sys.path.insert(0, str(tx_validation))
    sender = load("restart_sender_check", tx_validation / "check_low_power.py")
    sender.check_project_and_parameters()
    receiver_validation = RX / "wu-xian-App/validation/integration"
    receiver = load("restart_receiver_check", receiver_validation / "check_receiver.py")
    wired = RX / "you-xian_interface"
    results = {}
    for name, source, includes in (
        ("wired_logic", wired / "validation/test_logic.c", [wired]),
        ("wired_port", wired / "validation/test_port.c", [wired / "validation/mock", wired]),
        ("receiver_restart", receiver_validation / "test_receiver.c",
         [receiver_validation / "mock", RX / "wu-xian-App", wired,
          RX / "wu-xian_interface", RX / "User"]),
    ):
        results[name] = simulate(name, [wired / "wired_control.c", source], includes, args.output)
    results["sender_restart_and_low_power"] = simulate(
        "sender_restart_and_low_power", [tx_validation / "simulate_low_power.c"],
        [tx_validation / "mock", TX / "wu-xian-App", TX / "wu-xian_interface", TX / "User"],
        args.output, [f'-DAPP_SOURCE="{(TX / "wu-xian-App/wireless_demo.c").as_posix()}"'])
    builds = {}
    if not args.no_build:
        builds["211"] = sender.build()
        builds["221"] = receiver.build()
    assert before == source_hashes(), "Source/project file changed while validating"
    summary = {"hardware_accessed": False, "flashed": False,
               "abnormal_reset_causes": ["FWDGT", "WWDGT", "SOFTWARE"],
               "power_on_held_key_behavior_unchanged": True,
               "mixed_source_arbitration_fixed": False,
               "sources_unchanged_during_validation": len(before),
               "tests": results, "builds": builds, "source_hashes": before}
    (args.output / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
    print("PASS: both restart release interlocks and existing regressions; no board flashed.", flush=True)


if __name__ == "__main__":
    main()
