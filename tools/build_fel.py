# SPDX-License-Identifier: GPL-2.0-or-later
"""Build the V853 FEL helper on Windows or Ubuntu."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
from project import ROOT, BUILD, run, zig


def main():
    src = ROOT / "vendor/sunxi-tools"
    dest = BUILD / "tools"
    dest.mkdir(parents=True, exist_ok=True)
    sources = [ROOT / "tools/fel-v853.c"] + [src / n for n in
               ("fel_lib.c", "soc_info.c", "fel-spiflash.c", "progress.c")]
    flags = ["-O2", "-std=gnu99", "-fgnu89-inline", "-I" + str(src), "-I" + str(src / "include")]
    if os.name == "nt":
        usb = dest / "libusb"
        args = [zig(), "cc", "-target", "x86_64-windows-gnu", *flags,
                "-I" + str(usb / "include"), *sources,
                usb / "MinGW64/static/libusb-1.0.dll.a", "-lws2_32", "-o", dest / "fel-v853.exe"]
        run(args)
        shutil.copy2(usb / "MinGW64/dll/libusb-1.0.dll", dest / "libusb-1.0.dll")
    else:
        usb_flags = shlex.split(subprocess.check_output(
            ["pkg-config", "--cflags", "--libs", "libusb-1.0"], text=True))
        run([os.environ.get("CC", "cc"), *flags, *sources, *usb_flags, "-o", dest / "fel-v853"])


if __name__ == "__main__":
    main()
