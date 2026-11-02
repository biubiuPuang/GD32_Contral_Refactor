"""Explicit restoration only. Refuse to overwrite subsequently edited sources.

python restore_original.py --sources --firmware
"""
from pathlib import Path
import argparse
import hashlib
import json
import shutil

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def restore_sources():
    manifest = json.loads((HERE / "source_restore_manifest.json").read_text(encoding="utf-8"))
    # Validate every file before restoring any one of them.
    for relative, record in manifest.items():
        destination, source = ROOT / relative, ROOT / record["backup"]
        assert hashlib.sha256(source.read_bytes()).hexdigest() == record["original_sha256"], str(source)
        digest = hashlib.sha256(destination.read_bytes()).hexdigest()
        if digest not in (record["test_sha256"], record["original_sha256"]):
            raise RuntimeError(f"File edited after test setup; refusing overwrite: {destination}")
    for relative, record in manifest.items():
        shutil.copyfile(ROOT / record["backup"], ROOT / relative)
        print("SOURCE_RESTORED", relative, flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--sources", action="store_true")
    parser.add_argument("--firmware", action="store_true")
    args = parser.parse_args()
    if not args.sources and not args.firmware:
        parser.error("Specify --sources and/or --firmware; no changes made")
    if args.sources:
        restore_sources()
    if args.firmware:
        from board_firmware import operate
        operate("restore")
