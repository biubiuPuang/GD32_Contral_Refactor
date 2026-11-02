"""2026-11-2: command-line builds; never flashes hardware."""
from pathlib import Path
import hashlib
import json
import re
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
UV4 = Path(r"D:\_Keil5\UV4\UV4.exe")
GCC = Path(r"D:\1_Sofrware\_MinGW\mingw64\bin\gcc.exe")


def run_build():
    from intelhex import IntelHex
    protected = json.loads((HERE / "protected_hashes.json").read_text(encoding="utf-8"))
    for path, digest in protected.items():
        assert hashlib.sha256((ROOT / path).read_bytes()).hexdigest() == digest, path
    common = ["packet_loss_test.c", "packet_loss_test.h", "packet_loss_stats.c", "packet_loss_stats.h",
              "startup_packet_loss.s"]
    for name in common:
        assert (HERE / name).read_bytes() == (ROOT / "221_Project/test-diu-bao-lv" / name).read_bytes(), name
    host = HERE / "test_stats.exe"
    subprocess.run([str(GCC), "-std=c99", "-Wall", "-Wextra", "-Werror", "-O2",
                    str(HERE / "packet_loss_stats.c"), str(HERE / "test_stats.c"),
                    "-o", str(host)], check=True, timeout=30)
    subprocess.run([str(host)], check=True, timeout=15)
    result = {"protected_files": len(protected), "builds": {}}
    for side in ["211", "221"]:
        base = ROOT / (side + "_Project")
        startup = (base / "test-diu-bao-lv/startup_packet_loss.s").read_text(encoding="utf-8")
        assert re.search(r"Stack_Size\s+EQU\s+0x00000800", startup), "Test stack must be 2KB"
        log = base / "test-diu-bao-lv/build.log"
        project = base / "Project/GD32E230C8T6.uvprojx"
        process = subprocess.run([str(UV4), "-r", str(project), "-j0", "-o", str(log)],
                                 cwd=project.parent, timeout=180,
                                 creationflags=subprocess.CREATE_NO_WINDOW)
        text = log.read_bytes().decode("utf-8", errors="replace")
        if process.returncode != 0 or not re.search(r"0 Error\(s\), 0 Warning\(s\)", text):
            raise RuntimeError(text)
        image = base / "Project/Objects/GD32E230C8T6.hex"
        values = IntelHex(str(image))
        assert min(values.addresses()) >= 0x08000000
        assert max(values.addresses()) < 0x0800F800, "Pairing Flash overlap"
        map_text = (base / "Project/Listings/GD32E230C8T6.map").read_text(encoding="utf-8", errors="replace")
        assert re.search(r"USART1_IRQHandler\s+0x[0-9a-f]+.*packet_loss_test\.o", map_text, re.I)
        assert re.search(r"__initial_sp\s+0x[0-9a-f]+.*startup_packet_loss\.o", map_text, re.I)
        ram = re.search(r"RW-data=(\d+) ZI-data=(\d+)", text)
        assert ram and sum(map(int, ram.groups())) <= 8192, "GD32E230C8 RAM overflow"
        summary = {"errors": 0, "warnings": 0, "image_end": hex(max(values.addresses()) + 1),
                   "size": re.search(r"Program Size:.*", text).group(0),
                   "hex_sha256": hashlib.sha256(image.read_bytes()).hexdigest()}
        result["builds"][side] = summary
        print(side, summary, flush=True)
    for path, digest in protected.items():
        assert hashlib.sha256((ROOT / path).read_bytes()).hexdigest() == digest, path
    (HERE / "build_summary.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    return result


if __name__ == "__main__":
    run_build()
