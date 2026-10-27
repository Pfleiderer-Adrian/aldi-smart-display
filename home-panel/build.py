# SPDX-License-Identifier: GPL-2.0-or-later
"""Build the standalone Home Panel simulation for the tested display."""
import shutil
from PIL import Image, ImageDraw, ImageFont
from pathlib import Path
import os
import subprocess
import ziglang
ROOT = Path(__file__).resolve().parent
BUILD = ROOT / "build"
def zig():
    return Path(ziglang.__file__).parent / ("zig.exe" if os.name == "nt" else "zig")
def run(args):
    subprocess.run(list(map(str, args)), check=True)


def main():
    out = BUILD / "generated"
    out.mkdir(parents=True, exist_ok=True)
    font = ImageFont.truetype(str(ROOT / "assets/DejaVuSans.ttf"), 24)
    widths, rows = [], []
    for code in range(32, 127):
        ch = chr(code)
        width = min(31, max(1, round(font.getlength(ch))))
        widths.append(width)
        im = Image.new("L", (32, 28))
        ImageDraw.Draw(im).text((0, 23), ch, font=font, fill=255, anchor="ls")
        rows.append([sum(1 << x for x in range(width) if im.getpixel((x, y)) >= 96) for y in range(28)])
    header = "#define FONT_H 28\nstatic const unsigned char font_width[95]={" + ",".join(map(str, widths)) + "};\n"
    header += "static const unsigned int font_bits[95][28]={\n" + ",\n".join("{" + ",".join(hex(v) for v in row) + "}" for row in rows) + "\n};\n"
    (out / "font.h").write_text(header, encoding="utf-8")
    card = BUILD / "sd-card"
    app = card / "home-panel"
    app.mkdir(parents=True, exist_ok=True)
    run([zig(), "cc", "-target", "arm-linux-musleabi", "-mcpu=cortex_a7", "-Os", "-static", "-s",
         "-Wall", "-Wextra", "-I" + str(out), ROOT / "main.c", ROOT / "ha_client.c", ROOT / "ws_client.c", ROOT / "dashboard.c", ROOT / "config.c", "-pthread", "-o", app / "home-panel"])
    if (BUILD / "dashboard.tsv").exists():
        shutil.copy2(BUILD / "dashboard.tsv", app / "dashboard.tsv")
    shutil.copy2(ROOT / "secrets.example.json", app / "secrets.example.json")
    shutil.copy2(ROOT / "start.sh", app / "start.sh")
    shutil.copy2(ROOT / "start-detached.sh", app / "start-detached.sh")
    shutil.copy2(ROOT / "network-start.sh", app / "network-start.sh")
    shutil.copy2(ROOT / "autostart.sh", card / "autostart.sh")
    for p in (app / "start.sh", app / "start-detached.sh", app / "network-start.sh", card / "autostart.sh"):
        p.write_bytes(p.read_bytes().replace(b"\r\n", b"\n"))
    for name in ("FONT-LICENSE.txt", "DejaVuSans.ttf"):
        if name.endswith(".txt"):
            shutil.copy2(ROOT / "assets" / name, app / name)
    shutil.copy2(ROOT / "LICENSE", app / "LICENSE")
    shutil.copy2(ROOT / "vendor/JSMN-LICENSE", app / "JSMN-LICENSE")
    print("Copy the contents of", card, "to the root of the FAT32 DATA card.")


if __name__ == "__main__":
    main()
