"""F50 candidate validation. CLI only: no serial access or board flashing.

python 211_Project/wu-xian-App/validation/check_fifty.py
Uses existing MinGW tests and original Keil projects. The original RFPDK
arrays/exports, BSP, wired execution, entry points and project files are guarded.
"""
from pathlib import Path
from fractions import Fraction
import argparse
import hashlib
import importlib.util
import json
import math
import re
import struct
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
RX = ROOT / "221_Project/wu-xian-App/validation/integration"


def protected():
    hashes = json.loads((HERE / "fifty_protected_hashes.json").read_text(encoding="utf-8"))
    for relative, expected in hashes.items():
        assert hashlib.sha256((ROOT / relative).read_bytes()).hexdigest() == expected, relative
    return len(hashes)


def function(text, name):
    match = re.search(r"^(?:static\s+)?(?:void|BOOL|u32|uint8_t|u8)\s+" + name + r"\([^;{]*\)\s*\{", text, re.M)
    assert match, name
    pattern = r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
    scrubbed = re.sub(pattern, lambda m: " " * len(m.group()), text, flags=re.S)
    start = scrubbed.index("{", match.start())
    depth = 0
    for offset in range(start, len(scrubbed)):
        depth += (scrubbed[offset] == "{") - (scrubbed[offset] == "}")
        if depth == 0:
            return text[match.start():offset + 1]
    raise AssertionError(name)


def unchanged_behavior():
    common = ["Demo_ConfigSave", "Demo_ConfigLoad", "Demo_ReadRecord", "Demo_Checksum",
              "Demo_UartProcess", "USART1_IRQHandler", "Demo_Report", "Demo_TimerInit",
              "WirelessDemo_Process", "Demo_Number", "Demo_SetAction", "Demo_Fail"]
    count = 0
    for side, additional in [("211", ["Demo_ButtonProcess", "Demo_TxProcess", "Demo_Random"]),
                             ("221", ["Demo_RxProcess", "WirelessDemo_GetCommand", "WirelessDemo_ReportProcess"])]:
        relative = f"{side}_Project/wu-xian-App/wireless_demo.c"
        old = subprocess.run(["git", "show", "HEAD:" + relative], cwd=ROOT, check=True,
                             capture_output=True).stdout.decode("utf-8").replace("\r\n", "\n")
        new = (ROOT / relative).read_text(encoding="utf-8")
        for name in common + additional:
            assert function(old, name) == function(new, name), (side, name)
            count += 1
        old_command = function(old, "Demo_Command").replace("<CH:0..5>", "<CH:0..49>")
        assert old_command == function(new, "Demo_Command"), (side, "command behavior")
    return count


def frequency_plan():
    tx = ROOT / "211_Project/wu-xian-App/wireless_frequency_plan.h"
    rx = ROOT / "221_Project/wu-xian-App/wireless_frequency_plan.h"
    assert tx.read_bytes() == rx.read_bytes(), "TX/RX frequency profile mismatch"
    text = tx.read_text(encoding="utf-8")
    values = {}
    for name in ("DEMO_CHANNEL_COUNT", "DEMO_CHANNEL_STEP", "DEMO_BASE_FREQUENCY_HZ",
                 "DEMO_CHANNEL_SPACING_HZ", "DEMO_AFC_OVF_TH"):
        values[name] = int(re.search(r"^#define\s+" + name + r"\s+(\d+)", text, re.M).group(1))
    assert values == {"DEMO_CHANNEL_COUNT": 50, "DEMO_CHANNEL_STEP": 13,
                      "DEMO_BASE_FREQUENCY_HZ": 433125000,
                      "DEMO_CHANNEL_SPACING_HZ": 32500, "DEMO_AFC_OVF_TH": 21}
    assert values["DEMO_CHANNEL_STEP"] * 2500 == values["DEMO_CHANNEL_SPACING_HZ"]
    bank = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})U", text.split("= {", 1)[1])]
    # AN199 independent exact-rational calculation of base RX/TX PLL words.
    expected = []
    for target in (Fraction(433125000) + Fraction(26000000, 92), Fraction(433125000)):
        number = target * 4 / 26000000
        integer = int(number)
        fraction = int((number - integer) * 2**20)
        expected.extend([integer, fraction & 255, (fraction >> 8) & 255, 0x10 | (fraction >> 16)])
    assert bank == expected, (bank, expected)
    for side in ("211", "221"):
        header = (ROOT / f"{side}_Project/wu-xian-App/wireless_demo_params.h").read_text(encoding="utf-8")
        original = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})U", header.split("= {", 1)[1])]
        # AN199: preserve PALDO, DIVX, VCO and the RFPDK-generated FSK_SWT bit.
        assert bank[3] & 0xF0 == original[0x1B] & 0xF0
        assert bank[7] & 0xF0 == original[0x1F] & 0xF0
    # Confirm the independent calculation also reproduces the old export.
    old_bank = []
    for target in (Fraction(433920000) + Fraction(26000000, 92), Fraction(433920000)):
        number = target * 4 / 26000000
        fraction = int((number - int(number)) * 2**20)
        old_bank.extend([int(number), fraction & 255, (fraction >> 8) & 255, 0x10 | (fraction >> 16)])
    assert old_bank == original[0x18:0x20]
    source = ROOT / "资料/CMT2119B及CMT2219B芯片手册在这/AN1971-CMT2300A-CMT2219B 跳频计算表-V1.0(AN197配套).xls"
    source_bytes = source.read_bytes()
    # Excel may have the original open: inspect a read-only byte snapshot.
    with tempfile.TemporaryDirectory(prefix="cmt_f50_") as folder:
        snapshot = Path(folder) / "source.xls"
        snapshot.write_bytes(source_bytes)
        data = subprocess.run([r"C:\Program Files\7-Zip\7z.exe", "e", "-so", str(snapshot), "Workbook"],
                              check=True, capture_output=True).stdout
    expected_tokens = {
        (11, 2): "440a0002c044080002c0051e02001e1400070544030002c0061ee803061e000106411900",
        (4, 18): "44040011c04408000200051e02001e140007054403000200061ee803061e000106411900240b00020042020600",
    }
    seen = {}
    offset = 0
    while offset + 4 <= len(data):
        kind, length = struct.unpack_from("<HH", data, offset)
        payload = data[offset + 4:offset + 4 + length]
        offset += 4 + length
        if kind == 6 and len(payload) >= 22:
            cell = struct.unpack_from("<HH", payload)
            if cell in expected_tokens and cell not in seen:
                seen[cell] = payload[22:].hex()
    assert seen == expected_tokens, "Original AN1971 workbook formulas changed"
    frequencies = [Fraction(433125000 + ch * 32500, 1000000) for ch in range(50)]
    initial = math.floor(max(frequencies) * Fraction(80, 1000) * 4 * 2**20 / 26 / 1000 / 256)
    channels = []
    for ch, frequency in enumerate(frequencies):
        assert Fraction(43305, 100) <= frequency <= Fraction(43479, 100)
        intermediate = Fraction(26, 92)
        integer = math.floor((frequency + intermediate) * 4 / 26)
        low = 26 * Fraction(integer, 4) - intermediate
        high = 26 * Fraction(integer + 1, 4) - intermediate
        distance = min(abs(frequency - low), abs(frequency - high)) * 1000
        afc = min(math.floor(Fraction(95, 100) * distance * 4 * 2**20 / 26 / 1000 / 256), initial)
        assert afc == values["DEMO_AFC_OVF_TH"] == initial
        channels.append({"channel": ch, "nominal_hz": int(frequency * 1000000), "afc": afc})
    return {"profile": "F50_FIXED_CANDIDATE", "base_registers": bank, "channels": channels,
            "workbook_sha256": hashlib.sha256(source_bytes).hexdigest(),
            "workbook_formula_tokens_verified": True, "xtal_mhz": 26, "tx_rx_ppm_each": 40}


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    loaded = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loaded)
    return loaded


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args()
    count = protected()
    behavior = unchanged_behavior()
    plan = frequency_plan()
    sender = module("fifty_sender", HERE / "check_sender.py")
    receiver = module("fifty_receiver", RX / "check_receiver.py")
    sender.project_check(); sender.parameters(); receiver.project_check()
    tests = [sender.simulate("211")]
    tests += [receiver.simulate("wired_logic_regression", receiver.WIRED / "validation/test_logic.c", [receiver.WIRED]),
              receiver.simulate("wired_port_regression", receiver.WIRED / "validation/test_port.c",
                                [receiver.WIRED / "validation/mock", receiver.WIRED]),
              receiver.simulate("test_receiver", RX / "test_receiver.c",
                                [RX / "mock", receiver.APP, receiver.WIRED,
                                 ROOT / "221_Project/wu-xian_interface", ROOT / "221_Project/User"])]
    # Use existing original-project CLI builders, never a flash/download action.
    if not args.skip_build:
        tx_build = sender.build()
        rx_build = receiver.build()
    else:
        tx_build = rx_build = None
    assert protected() == count
    assert unchanged_behavior() == behavior
    plan.update({"protected_files": count, "unchanged_behavior_functions": behavior,
                 "tests": tests, "tx_build": tx_build, "rx_build": rx_build,
                 "hardware_tested": False, "multi_pair_concurrency_validated": False,
                 "continuous_frequency_hopping_implemented": False,
                 "configuration_record_format_unchanged": True,
                 "wireless_timeout_ms": 85, "control_protocol": "A2"})
    (HERE / "fifty_summary.json").write_text(json.dumps(plan, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"PASS F50: all 50 channel configurations/persistence/ID filtering, AN199 PLL bank, AN1971 AFC, "
          f"existing control regressions; {count} protected files and {behavior} behavior functions unchanged.", flush=True)
    print("No board flashed; 50-pair RF concurrency is not hardware validated.", flush=True)


if __name__ == "__main__":
    main()
