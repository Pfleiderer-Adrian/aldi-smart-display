# Aldi Smart Display: quick start

> WARNING:
> **Flashing modified firmware can brick your display (unusabel) and cause data loss.**
> Follow this guide at your own risk. Compatibility is limited to the tested hardware and firmware;
> a failed flash, incompatible image, or interrupted power supply may prevent the device from booting.
> Keep a verified backup of your own original firmware and the FEL recovery card before making changes.
> Recovery is not guaranteed. This project is provided without warranty.

This unofficial community project enables **ADB access**, **automatic startup
of applications from microSD**, and a small **Hello World demo**. It is not an
official Aldi project.

**With a data card:** the firmware starts `autostart.sh` from the card root.
**Without a data card or that script:** the manufacturer interface keeps running.

## Requirements

- The tested display with an Allwinner **V853**, **16 MiB SPI NOR**, and a
  **1024 × 600** screen. Unsupported factory firmware is rejected before changes.
- A USB data cable connected to the PC (USB-A) and on our device (USB-C).
- Two microSD cards: a **FEL recovery card** and a normal **FAT32 data card**.
- Windows x64 or Ubuntu, with Python.

## Steps

1. **Prepare the tools:** follow the complete [Windows guide](windows.md) or
   [Ubuntu guide](ubuntu.md).
2. **Create the FEL card:** `tools/make_fel_card.py` creates `build/fel-card.img`.
   Write this image only to the recovery card. Writing it makes the card's old
   contents inaccessible. The second card remains a normal FAT32 data card.
3. **Enter FEL:** power off, insert the FEL card, hold the power button, and
   connect the USB data port. Look for USB ID **`1f3a:efe8`**. Windows may require
   the WinUSB driver through Zadig for this device.
4. **Create your own backup:** `tools/firmware.py backup` reads all 16 MiB twice
   and compares the results. Keep an additional copy of `work/` elsewhere and
   never publish that directory.
5. **Prepare firmware locally:** `tools/firmware.py prepare` enables ADB,
   corrects USB device mode, and adds SD startup. `tools/firmware.py install`
   then prints the installation plan without writing.
6. **Install:** only `tools/firmware.py install --write` writes the two intended
   regions. It reads them back and compares the entire flash. Keep power
   connected throughout and wait for **`FULL_FLASH_VERIFIED`**.
7. **Use the data card:** copy the contents of **`sd-card/`** to the FAT32 card.
   Power off, replace the FEL card with the data card, and boot normally. The
   demo should appear automatically after approximately **25 seconds**.

Run the Python scripts using your virtual environment's interpreter. The
platform guides give the exact commands; for example, on Windows:

```powershell
.\.venv\Scripts\python.exe tools/firmware.py backup
```

## Data-card files

```text
autostart.sh
hello-world/
  start.sh
  hello-world
```

**Back to menu** ends the demo until the next boot. Tap the background to change
its color.

You can install later applications by replacing the SD files; another firmware
flash is normally unnecessary. Stop the application before replacing its files
or removing the card.

## Check ADB

On Ubuntu, run `adb devices -l`. On Windows, with Google Platform-Tools extracted
separately, run `.\platform-tools\adb.exe devices -l`.

ADB appears as **Tina ADB, `18d1:d002`**. It is a different USB device from FEL
and has its own Windows driver association. USB may disappear briefly during
startup before the correction restores the connection automatically.

## What is published

Source code, startup scripts, the FEL stub, the demo, and documentation.
**No complete manufacturer firmware or personal device backups.** Each user
builds their modified firmware from their own backup. The firmware tools verify
the supported version and intended changes before writing.

[Technical background](technical.md) · [Development](development.md) ·
[Recovery](recovery.md) · [Project overview](../README.md)
