# SPDX-License-Identifier: GPL-2.0-or-later
"""Build the static ARMv7 demo and portable FAT32 SD-card folder."""
import shutil
from PIL import Image, ImageDraw, ImageFont
from project import ROOT, BUILD, run, zig


def main():
    out = BUILD / "demo"
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
    app = card / "hello-world"
    app.mkdir(parents=True, exist_ok=True)
    run([zig(), "cc", "-target", "arm-linux-musleabi", "-mcpu=cortex_a7", "-Os", "-static", "-s",
         "-Wall", "-Wextra", "-I" + str(out), ROOT / "demo/main.c", "-o", app / "hello-world"])
    shutil.copy2(ROOT / "demo/start.sh", app / "start.sh")
    shutil.copy2(ROOT / "demo/autostart.sh", card / "autostart.sh")
    for p in (app / "start.sh", card / "autostart.sh"):
        p.write_bytes(p.read_bytes().replace(b"\r\n", b"\n"))
    print("Copy the contents of", card, "to the root of the FAT32 DATA card.")


if __name__ == "__main__":
    main()
