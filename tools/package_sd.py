# SPDX-License-Identifier: GPL-2.0-or-later
"""Package the freshly built SD demo together with its license notices."""
import shutil
import zipfile
from project import ROOT, BUILD


def main():
    source = BUILD / "sd-card"
    if not (source / "hello-world/hello-world").is_file():
        raise RuntimeError("Run tools/build_demo.py first")
    target = ROOT / "sd-card"
    for path in source.rglob("*"):
        if path.is_file():
            dest = target / path.relative_to(source)
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, dest)
    shutil.copy2(ROOT / "LICENSE", target / "LICENSE.txt")
    shutil.copy2(ROOT / "assets/FONT-LICENSE.txt", target / "FONT-LICENSE.txt")
    (target / "README.txt").write_text(
        "Aldi Smart Display: Hello World\n"
        "Copy these files to the root of a FAT32 DATA microSD.\n"
        "Requires the project's SD autostart firmware hook.\n"
        "The separate FEL card is for backup/flashing, not this application.\n"
        "Source: demo/ and tools/build_demo.py in the aldi-smart-display repository.\n"
        "Application: GPL-2.0-or-later. Font: see FONT-LICENSE.txt.\n"
        "Stop the demo before removing the card.\n", encoding="utf-8")
    with zipfile.ZipFile(BUILD / "hello-world-sd.zip", "w", zipfile.ZIP_DEFLATED) as z:
        for path in sorted(target.rglob("*")):
            if path.is_file():
                z.write(path, path.relative_to(target).as_posix())
    print(BUILD / "hello-world-sd.zip")


if __name__ == "__main__":
    main()
