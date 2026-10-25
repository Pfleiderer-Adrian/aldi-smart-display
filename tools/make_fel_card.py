# SPDX-License-Identifier: GPL-2.0-or-later
"""Create an image for the separate FEL recovery card. Never writes a disk."""
import hashlib
from project import ROOT, BUILD

EXPECTED = "2ccec08a026840571f3804671ba72afae840db27112a43f2b1f122075bc808bc"


def image_bytes(stub):
    if len(stub) != 8192 or hashlib.sha256(stub).hexdigest() != EXPECTED:
        raise ValueError("FEL stub differs from the tested upstream binary")
    data = bytearray(1024 * 1024)
    data[8192:16384] = stub
    return bytes(data)


def main():
    BUILD.mkdir(exist_ok=True)
    path = BUILD / "fel-card.img"
    path.write_bytes(image_bytes((ROOT / "assets/fel-sdboot.sunxi").read_bytes()))
    print(path)
    print("Write this image to the separate FEL card, not the application card.")


if __name__ == "__main__":
    main()
