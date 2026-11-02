"""Backup/program/verify both boards with existing pyOCD (sector erase only).

Probe identity is verified against the original firmware's role string before
the first backup. Pairing pages must remain byte-identical after test flashing.
"""
from pathlib import Path
import argparse
import hashlib
import json
from pyocd.core.helpers import ConnectHelper
from pyocd.flash.file_programmer import FileProgrammer
from intelhex import IntelHex

ROOT = Path(__file__).resolve().parents[2]
BOARDS = {
    "211": ("68004200040000515953574E", b"WIRELESS V3 TX211"),
    "221": ("CBD275220040B56D01173002", b"RECEIVER V3 RX221"),
}


def operate(action):
    for side, (probe, marker) in BOARDS.items():
        folder = ROOT / (side + "_Project/test-diu-bao-lv")
        backup = folder / "backup/pre_test_flash.bin"
        session = ConnectHelper.session_with_chosen_probe(
            blocking=False, unique_id=probe,
            options={"target_override": "gd32e230c8",
                     "connect_mode": "under-reset" if action in ("flash", "restore") else "attach",
                     "frequency": 100000, "resume_on_disconnect": True,
                     "cache.enable_memory": False, "cache.enable_register": False,
                     "hide_programming_progress": True})
        if session is None:
            raise RuntimeError("Probe missing: " + probe)
        with session:
            target = session.target
            if action == "backup":
                image = bytes(target.read_memory_block8(0x08000000, 65536))
                if marker not in image:
                    raise RuntimeError(f"{side}: firmware role does not match probe; refusing backup/flash")
                if backup.exists() and backup.read_bytes() != image:
                    raise RuntimeError("Refusing to overwrite a different original firmware backup")
                backup.write_bytes(image)
                manifest = {"side": side, "probe": probe, "target": "gd32e230c8",
                            "role_marker": marker.decode(), "bytes": len(image),
                            "sha256": hashlib.sha256(image).hexdigest(),
                            "pairing_sha256": hashlib.sha256(image[0xF800:]).hexdigest()}
                (folder / "backup/flash_backup.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
                print("BACKED_UP", manifest, flush=True)
            elif action in ("flash", "restore"):
                # Run the vendor FLM from reset clocks, not the application's 72MHz PLL.
                # Its Init changes FMC wait states; attach-to-running was not reliable.
                target.reset_and_halt()
                original = backup.read_bytes()
                manifest = json.loads((folder / "backup/flash_backup.json").read_text(encoding="utf-8"))
                assert hashlib.sha256(original).hexdigest() == manifest["sha256"]
                assert manifest["probe"] == probe
                before = bytes(target.read_memory_block8(0x0800F800, 2048))
                assert before == original[0xF800:], "Pairing changed since backup"
                if action == "restore":
                    # Restore exact pre-test application bytes; leave pairing pages untouched.
                    image = folder / "backup/restore_original.hex"
                    original_hex = IntelHex()
                    original_hex.frombytes(original[:0xF800], offset=0x08000000)
                    original_hex.write_hex_file(str(image))
                else:
                    image = ROOT / (side + "_Project/Project/Objects/GD32E230C8T6.hex")
                values = IntelHex(str(image))
                assert min(values.addresses()) >= 0x08000000 and max(values.addresses()) < 0x0800F800
                FileProgrammer(session, chip_erase="sector", trust_crc=False, keep_unwritten=True).program(str(image))
                verified = 0
                for start, end in values.segments():
                    actual = bytes(target.read_memory_block8(start, end - start))
                    assert actual == values.tobinarray(start=start, end=end - 1).tobytes(), "Flash readback differs"
                    verified += len(actual)
                after = bytes(target.read_memory_block8(0x0800F800, 2048))
                assert after == before, "Persistent pairing pages changed"
                record = {"side": side, "probe": probe, "verified_image_bytes": verified,
                          "image_sha256": hashlib.sha256(image.read_bytes()).hexdigest(),
                          "pairing_unchanged": True, "erase": "sector"}
                if action == "restore":
                    assert bytes(target.read_memory_block8(0x08000000, 65536)) == original
                record_path = folder / ("restore_verification.json" if action == "restore" else "flash_verification.json")
                record_path.write_text(json.dumps(record, indent=2), encoding="utf-8")
                target.reset()
                print("FLASH_VERIFIED", record, flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=["backup", "flash", "restore"])
    operate(parser.parse_args().action)
