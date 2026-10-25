# Provenance and third-party material

## linux-sunxi/sunxi-tools

`vendor/sunxi-tools/` contains the tracked source files at upstream commit
`a08a24a2aa5804bde7fa6ea8cdb08135d018da33` from
<https://github.com/linux-sunxi/sunxi-tools>.

`fel-spiflash.c` includes our tested V853 SPI-register, GPIO-bank and pin-mux
changes. `tools/fel-v853.c` includes the remote-function helpers from upstream
`fel.c`, plus our restricted host CLI. Upstream copyright notices and
`vendor/sunxi-tools/LICENSE.md` are retained. License: GPL-2.0-or-later as stated
in the individual files. The root LICENSE contains GPL version 2.

`assets/fel-sdboot.sunxi` is the upstream 8192-byte GPL FEL stub, identical to
`vendor/sunxi-tools/bin/fel-sdboot.sunxi`. Its sources/build rules are included
under `vendor/sunxi-tools/`; the upstream commit above is also recorded for
reproducibility. SHA-256:

```text
2ccec08a026840571f3804671ba72afae840db27112a43f2b1f122075bc808bc
```

## Font

`assets/DejaVuSans.ttf` is DejaVu Sans 2.37 from the upstream
[DejaVu release](https://github.com/dejavu-fonts/dejavu-fonts/releases/tag/version_2_37).
Its full notices and redistribution terms are in `assets/FONT-LICENSE.txt`.
The public demo uses this font instead of the development computer's Windows
Segoe UI font. The font's own license is not replaced by the project's GPL.

## Host dependencies downloaded or installed locally

- [squashfs-tools-ng](https://github.com/AgentD/squashfs-tools-ng): upstream
  Windows 1.3.2 tools; GPL tools/LGPL library. Download stays in ignored `build/`.
- [libusb](https://github.com/libusb/libusb/releases/tag/v1.0.30): official
  Windows 1.0.30 build or Ubuntu's development package. LGPL dependency;
  downloaded archive and DLL stay in ignored `build/`.
- [Zig](https://ziglang.org/): locally installed via pinned `ziglang` wheel;
  compiler and runtime components retain their upstream licenses.
- [Pillow](https://python-pillow.github.io/): local font rasterization dependency.
- [Google Platform-Tools](https://developer.android.com/tools/releases/platform-tools)
  and [Zadig](https://zadig.akeo.ie/) are separate downloads, not redistributed here.

The manufacturer firmware, proprietary display/phone-link libraries, extracted
rootfs contents and per-device backups are intentionally absent from this
repository. The builder reads the user's own firmware locally.
