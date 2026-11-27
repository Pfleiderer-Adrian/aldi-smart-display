# SPDX-License-Identifier: GPL-2.0-or-later
"""Build the standalone MOVA cloud client for the tested display."""
import shutil
from PIL import Image, ImageDraw, ImageFont
from pathlib import Path
import os
import subprocess
import ziglang
from build_deps import prepare_dependencies
ROOT = Path(__file__).resolve().parent
BUILD = ROOT / "build"
def zig():
    return Path(ziglang.__file__).parent / ("zig.exe" if os.name == "nt" else "zig")
def run(args):
    subprocess.run(list(map(str, args)), check=True)


def main():
    out = BUILD / "generated"
    out.mkdir(parents=True, exist_ok=True)
    # Rasterize independent glyphs; avoid RAQM shaping failures on invisible characters.
    font = ImageFont.truetype(str(ROOT / "assets/DejaVuSans.ttf"), 24,
                              layout_engine=ImageFont.Layout.BASIC)
    widths, rows = [], []
    for code in range(32, 384):
        ch = chr(code)
        width = min(31, max(1, round(font.getlength(ch))))
        widths.append(width)
        im = Image.new("L", (32, 28))
        ImageDraw.Draw(im).text((0, 23), ch, font=font, fill=255, anchor="ls")
        rows.append([sum(1 << x for x in range(width) if im.getpixel((x, y)) >= 96) for y in range(28)])
    header = "#define FONT_H 28\nstatic const unsigned char font_width[352]={" + ",".join(map(str, widths)) + "};\n"
    header += "static const unsigned int font_bits[352][28]={\n" + ",\n".join("{" + ",".join(hex(v) for v in row) + "}" for row in rows) + "\n};\n"
    (out / "font.h").write_text(header, encoding="utf-8")
    card = BUILD / "sd-card"
    app = card / "mova-panel"
    app.mkdir(parents=True, exist_ok=True)
    dependencies = prepare_dependencies(ROOT)
    tls = dependencies["mbedtls"]
    sources = [ROOT / n for n in ("main.c", "config.c", "json.c", "cloud.c", "sign.c", "ntp.c", "rooms.c")]
    sources += sorted((tls / "library").glob("*.c"))
    zlib = dependencies["zlib"]
    sources += [zlib / n for n in ("adler32.c", "crc32.c", "inffast.c", "inflate.c", "inftrees.c", "zutil.c", "uncompr.c")]

    run([zig(), "cc", "-target", "arm-linux-musleabi", "-mcpu=cortex_a7", "-Os", "-static", "-s",
         "-Wall", "-Wextra", "-ffile-prefix-map=" + str(ROOT) + "=.",
         "-ffile-prefix-map=" + ROOT.as_posix() + "=.",
         "-I" + str(out), "-I" + str(tls / "include"), "-I" + str(zlib), *sources, "-pthread", "-o", app / "mova-panel"])
    shutil.copy2(ROOT / "vendor/ca-certificates.pem", app / "ca-certificates.pem")
    shutil.copy2(tls / "LICENSE", app / "MBEDTLS-LICENSE")
    shutil.copy2(zlib / "LICENSE", app / "ZLIB-LICENSE")
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
    # Match Home Panel: SD output contains runtime files and license notices.
    # Development docs/protocol references remain in the source folder.
    for obsolete in ("README.md", "PROTOCOL-SOURCE.txt"):
        (app / obsolete).unlink(missing_ok=True)
    shutil.copy2(ROOT / "LICENSE", app / "LICENSE")
    shutil.copy2(ROOT / "vendor/JSMN-LICENSE", app / "JSMN-LICENSE")
    print("Copy the contents of", card, "to the root of the FAT32 DATA card.")


if __name__ == "__main__":
    main()
