"""Run the actual C drivers against a GPIO/SPI chip model on Windows.

Requires Python 3 and Visual Studio C++ build tools. No hardware is accessed.
"""
from pathlib import Path
import os
import subprocess
import tempfile


def run(command, cwd):
    result = subprocess.run(command, cwd=cwd, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=60)
    output = result.stdout.decode("mbcs", errors="replace")
    print(output)
    result.check_returncode()


def main():
    here = Path(__file__).resolve().parent
    root = here.parents[1]
    vswhere = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / \
        "Microsoft Visual Studio/Installer/vswhere.exe"
    install = subprocess.check_output(
        [str(vswhere), "-latest", "-products", "*", "-requires",
         "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property",
         "installationPath"], text=True).strip()
    if not install:
        raise RuntimeError("Visual Studio C++ build tools were not found")
    vcvars = Path(install) / "VC/Auxiliary/Build/vcvars64.bat"
    tx = root / "211_Project/wu-xian_interface"
    rx = root / "221_Project/wu-xian_interface"
    quote = lambda path: '"' + str(path) + '"'

    # Build once against each project's real shared driver implementation.
    for side, common in (("211 TX", tx), ("221 RX", rx)):
        print("Checking", side, flush=True)
        with tempfile.TemporaryDirectory(prefix=".build-", dir=here) as temp:
            build = Path(temp).resolve()
            # Verify the cleanup target is inside this test directory.
            assert build.is_relative_to(here)
            files = [here / "verify.c"] + [common / name for name in
                ("cmt_port.c", "cmt_spi3.c", "cmt2300a_hal.c", "cmt2300a.c")]
            files += [tx / "cmt2119b.c", rx / "cmt2219b.c"]
            command = "call " + quote(vcvars) + " >nul && cl /nologo /W3 /WX /TC "
            command += " ".join("/I" + quote(path) for path in
                                (here / "mock", common, tx, rx))
            command += " " + " ".join(map(quote, files))
            command += " /Fe:verify.exe /Fo:.\\ && verify.exe"
            run('cmd.exe /d /s /c "' + command + '"', build)


if __name__ == "__main__":
    main()
