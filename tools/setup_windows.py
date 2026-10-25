# SPDX-License-Identifier: GPL-2.0-or-later
"""Fetch pinned upstream build dependencies. Does not install USB drivers."""
import hashlib
from pathlib import Path
import os
import urllib.request
import zipfile
from project import BUILD, run

ARCHIVES = [
    ("squashfs.zip", "https://infraroot.at/pub/squashfs/windows/squashfs-tools-ng-1.3.2-mingw64.zip",
     "891b1ed46dd856d05e429b01de2c7d86175a4c22478b5a6770f94b9c2100d0f6"),
    ("libusb.7z", "https://github.com/libusb/libusb/releases/download/v1.0.30/libusb-1.0.30.7z",
     "7fb1dfec805b97983763d7d0ae244320da12add1003d4249c96cc4d586398c79"),
]


def main():
    if os.name != "nt":
        raise SystemExit("This setup is for Windows. On Ubuntu use apt as documented.")
    dest = BUILD / "tools"
    dest.mkdir(parents=True, exist_ok=True)
    for name, url, digest in ARCHIVES:
        path = dest / name
        if not path.exists():
            urllib.request.urlretrieve(url, path)
        if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise RuntimeError(f"Checksum mismatch: {name}; no extraction performed")
        if name.endswith(".zip"):
            with zipfile.ZipFile(path) as archive:
                archive.extractall(dest / "squashfs")
        else:
            target = dest / "libusb"
            target.mkdir(exist_ok=True)
            run(["tar", "-xf", path, "-C", target])
    print("Windows dependencies ready. Now run python tools/build_fel.py")


if __name__ == "__main__":
    main()
