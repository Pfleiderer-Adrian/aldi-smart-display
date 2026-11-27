# Provenance and third-party material

## linux-sunxi/sunxi-tools

`vendor/sunxi-tools/` contains the tracked source files at upstream commit
`a08a24a2aa5804bde7fa6ea8cdb08135d018da33` from
<https://github.com/linux-sunxi/sunxi-tools>.

`fel-spiflash.c` includes our tested V853 SPI-register, GPIO-bank and pin-mux
changes. Chinese explanatory comments in the two NAND partition headers and
`nand-part-main.c` have been translated into English without changing code.
`tools/fel-v853.c` includes the remote-function helpers from upstream
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

## Home Assistant Panel

`home-panel/vendor/jsmn.h` is jsmn v1.1.0 from
<https://github.com/zserge/jsmn/tree/v1.1.0>, under the MIT license. Its complete
license is retained in `home-panel/vendor/JSMN-LICENSE` and alongside the SD binary.
The panel bundles the same DejaVu font and font license described above.
The static panel executable is built from `home-panel/` with the pinned Zig toolchain.

The optional Python dashboard importer and lab setup tool use websocket-client
1.9.2 (Apache-2.0), installed locally via `home-panel/requirements.txt`; the Python
package itself is not redistributed. Home Assistant is accessed over its APIs;
its server and frontend are not bundled. The panel's WebSocket and native UI
implementation is project code under GPL-2.0-or-later.

The publication package omits upstream sunxi-tools tests and its development CI
file. On 2026-10-27, the vendored Makefile's test-only `check` target and test
cleanup command were removed to match that packaging change. Runtime and build
sources and their license notices are retained.

## MOVA Panel

`mova-panel/` contains native project code (GPL-2.0-or-later) based on Home Panel.
The only cloud-protocol reference is Tasshack/dreame-vacuum at revision
`fec086caf064f2681e8bcc7be20a0d0ef54f444d`; see
`mova-panel/PROTOCOL-SOURCE.txt`. Home Assistant and its Python dependencies are
not bundled or needed at runtime.

- **Mbed TLS 2.28.10**: downloaded at build time from
  <https://github.com/Mbed-TLS/mbedtls/tree/v2.28.10>, under the dual
  Apache-2.0 OR GPL-2.0-or-later license. The verified download's license is copied
  to the SD package as MBEDTLS-LICENSE.
- **zlib 1.3.1**: downloaded at build time from
  <https://github.com/madler/zlib/tree/v1.3.1>, under its zlib license, copied to
  the SD package as ZLIB-LICENSE.
- **jsmn v1.1.0** and **DejaVu Sans**: same versions/notices as Home Panel.
- **Public CA roots**: Mozilla certificate bundle distributed by
  <https://curl.se/docs/caextract.html>. The PEM header records its extraction date.
  This file contains public trust anchors, not device certificates/private keys.

The SD folder includes the static ARM executable, public CA bundle and dependency
notices. Its corresponding source and pinned Python build requirements are in
`mova-panel/`. Fixed release URLs and SHA-256 digests are recorded in
`mova-panel/dependencies.lock.json`. The build downloads and verifies these inputs
in ignored `mova-panel/build/dependencies/` and rejects changed cached inputs.
Mbed TLS/zlib source trees and a separate source archive are not included in the
repository. Upstream development tooling is not redistributed.
The MOVA README photograph was provided by the project owner and shows the
display running MOVA Panel beside a MOVA V70 Ultra Complete. Credentials, tokens,
Wi-Fi settings and robot map files are excluded from publication.
