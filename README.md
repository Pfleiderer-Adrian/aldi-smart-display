# Aldi Smart Display

[Quick start](docs/quickstart.md)

Community tool for **ADB access, SD-card autostart and native applications** on
the Aldi smart display from 2026. Unofficial project; not affiliated with Aldi or the
device manufacturer.



<p align="center">
  <img src="assets/readme_image.jpeg" alt="BAUHN smart display running the Hello World demo" width="500">
</p>

*The Hello World demo running on the BAUHN smart display.*


<p align="center">
  <img src="assets/readme_image_ha.jpeg" alt="BAUHN smart display running the Home Assistant Panel" width="500">
</p>

*Home Assistant Panel running on the BAUHN smart display.*

> WARNING:
> **Flashing modified firmware can brick your display (unusabel) and cause data loss.**
> Follow this guide at your own risk. Compatibility is limited to the tested hardware and firmware;
> a failed flash, incompatible image, or interrupted power supply may prevent the device from booting.
> Keep a verified backup of your own original firmware and the FEL recovery card before making changes.
> Recovery is not guaranteed. This project is provided without warranty.

## What you get

- A builder that enables ADB using **your own factory firmware backup**.
- Automatic USB recovery after the manufacturer's USB-host initialization.
- A firmware hook that starts `/autostart.sh` from the FAT32 data microSD.
- A small native hello world demo that runs directly from the data card.
- A [native Home Assistant Dashboard Client](home-panel/README.md) with dashboard-driven tiles,
  configured tap actions and a media-player volume slider.

**Without a data card, or without `autostart.sh`, the manufacturer interface
continues running.** Application updates normally require changing SD files,
not flashing the firmware again.

## Supported hardware

This is deliberately a narrow, tested device profile:

| Item | Tested device |
|---|---|
| Product Name| BAUHN Smart Display fürs Auto |
| SoC | Allwinner V853, FEL ID `0x1886` |
| Internal flash | 16 MiB SPI NOR |
| RAM available to Linux | 58,900 KiB; no swap configured |
| Wi-Fi | 2.4 and 5 GHz networks detected; WPA2-PSK client connection tested on `wlan1` |
| Wi-Fi interfaces | Manufacturer hotspot on `wlan0`; panel uses `wlan1` for the home network |
| Screen | 1024 × 600, 16-bit ARGB1555 |
| Touch | Goodix, `/dev/input/event1` |
| FEL USB ID | `1f3a:efe8` |
| ADB USB ID | `18d1:d002` (Tina ADB) |

The firmware builder accepts only the known factory boot/rootfs hashes.
If your firmware differs, it stops. Keep your own backup save.

## Start here

You need the display, its USB data connection, and **two microSD cards**:

1. **FEL card:** for backup, flashing and recovery. Writing its image replaces
   its partition metadata. Keep it separate from the application card.
2. **Data card:** a normal FAT32 card holding `autostart.sh` and the application.

Choose the complete guide for your computer:

- **[Windows guide](docs/windows.md)** — Python, build tools, WinUSB and flashing.
- **[Ubuntu guide](docs/ubuntu.md)** — packages, USB permissions and flashing.

Both guides follow the same sequence:

```text
Build tools → create FEL card → enter FEL → read backup twice
→ build firmware from your backup → review → flash and read back
→ power off → insert FAT32 data card → boot the demo
```

Firmware work requires a reliable power supply. The installer verifies the
entire flash after writing and checks that bytes outside the boot package and
rootfs stayed unchanged. Keep the complete `work/` directory private and backed
up. It contains your original firmware and possibly device-specific settings.

## SD-card layout

For the included demo, copy the **contents** of `sd-card/` directly to your card.
Its source is in `demo/`. To rebuild it, use `tools/build_demo.py` and copy the
**contents** of `build/sd-card/`
to the root of the FAT32 **data** card:

```text
autostart.sh
hello-world/
  start.sh
  hello-world
```

The startup hook invokes `/bin/sh /mnt/extsd/autostart.sh` after USB recovery
finishes. It supplies `SD_ROOT=/mnt/extsd` and sets the working directory to the
card root. Shell scripts must have LF line endings; FAT32 executable bits are
not required for shell scripts. Native programs do need an executable mount,
which the tested firmware uses.

Expect roughly **25 seconds** from power-on until the demo appears. The demo
pauses `cdr` while it owns the screen and resumes it on exit. The generic SD
hook itself does not pause the manufacturer application.

## Home Assistant Panel (optional)

<p align="center">
  <img src="assets/readme_image_ha.jpeg" alt="BAUHN smart display running the Home Assistant Panel" width="500">
</p>


Requires the SD autostart firmware hook from the guide above.

1. Copy `sd-card/home-panel/` to your FAT32 data card, alongside `hello-world/`.
2. In that folder, copy `secrets.example.json` to **`secrets.json`** and fill in:
   - `dashboard_url`: full URL including the dashboard and view.
   - `home_assistant_token`: long-lived access token from HA profile ? Security.
   - `wifi_ssid` and `wifi_password`: your WPA2 Wi-Fi credentials.
3. Back up the card's root `autostart.sh`, then replace it with
   [`home-panel/autostart.sh`](home-panel/autostart.sh), or use:

   ```sh
   #!/bin/sh
   card_dir=${0%/*}
   exec /bin/sh "$card_dir/home-panel/start.sh"
   ```

Boot normally with the data card. No Python, build step or additional flash is
needed.

**Keep `secrets.json` private.** It is the only file you configure. The panel
creates its temporary Wi-Fi configuration in RAM. Native connections currently
require local HTTP/WS with an IPv4 address; TLS is not supported, so the token is
unencrypted on the LAN. See the [panel README](home-panel/README.md) for supported
cards, limitations and development instructions. A full cold-boot test of the
panel's SD startup is still pending.

## Development and recovery

- [ADB commands, troubleshooting and replacing the SD application](docs/development.md)
- [How ADB was enabled and what the firmware changes](docs/technical.md)
- [Restore the original boot package and rootfs](docs/recovery.md)
- [Dependency provenance and licensing](THIRD_PARTY.md)

The repository contains source code and the GPL FEL stub, **not manufacturer
firmware images, device backups, Wi-Fi credentials or extracted root filesystems**.
Host downloads and firmware outputs go to ignored `build/` and `work/` folders.

## Validation

On the development device, FEL backup/flashing, root ADB, USB recovery, SD demo
execution, automatic SD startup and boot without a card were tested. The physical
display test caught an important detail: ARGB1555's alpha bit must be set; an
RGB-only preview can look correct while the actual screen is black.

The cleaned host scripts and complete firmware preparation were exercised on
Windows. A GitHub Actions matrix builds the tools on Windows and Ubuntu;
check its results before treating a new platform build as tested. Creating a
repository does not by itself mean that CI has run.

## License

Project code is provided under **GPL-2.0-or-later**, including the modified
linux-sunxi tooling. Third-party files retain their notices and licenses.
DejaVu Sans has its own included font license. See [LICENSE](LICENSE) and
[THIRD_PARTY.md](THIRD_PARTY.md).
