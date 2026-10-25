# Windows: from a factory display to the SD demo

Run commands in PowerShell from the repository folder. Windows x64 and Python
3.12+ are the intended host setup. No WSL, Android Studio or Linux VM is needed.

## 1. Prepare the computer

Install [Python](https://www.python.org/downloads/windows/) and download/extract
the [Google Platform-Tools](https://developer.android.com/tools/releases/platform-tools).
Keep the extracted `platform-tools` folder handy; commands below assume it is
in this repository folder. It is a local tool download, not part of Git.

```powershell
py -3 -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements.txt
.\.venv\Scripts\python.exe tools/setup_windows.py
.\.venv\Scripts\python.exe tools/build_fel.py
.\.venv\Scripts\python.exe tools/build_demo.py
.\.venv\Scripts\python.exe tools/make_fel_card.py
```

The setup downloads pinned, checksum-verified upstream Windows packages for
SquashFS tooling and libusb. The FEL tool is built from included source. It does
not install a USB driver or alter the display.

## 2. Write the separate FEL card

Use [Win32 Disk Imager](https://sourceforge.net/projects/win32diskimager/) to write
`build/fel-card.img` to the **FEL microSD**. Double-check the selected drive.
This replaces the card's partition metadata and can make its old files
inaccessible. Never select your Windows/system disk or the data card.

The image contains the upstream FEL stub at offset **8192 bytes**, as in the
original working `dd ... seek=8` setup. It is not a normal FAT data-card image.
If Windows offers to format the FEL card afterwards, cancel.

## 3. Enter FEL and install its Windows driver

Power off the display, insert the FEL card, hold the power button and connect
the tested USB **data** port to the PC. On the development unit this was the
Micro-USB connection; USB-C had been used for power. Do not assume every visible
USB socket provides the same data connection.

Use [Zadig](https://zadig.akeo.ie/) if the FEL device lacks a driver:

1. Choose the device with USB ID **`1F3A:EFE8`**.
2. Choose **WinUSB**, then install its driver.
3. Avoid changing drivers for unrelated devices.

Check:

```powershell
.\build\tools\fel-v853.exe version
```

Expected: `SoC V853 ID 0x1886`. A black screen in FEL is normal.

## 4. Back up twice and build your firmware

```powershell
.\.venv\Scripts\python.exe tools/firmware.py backup
.\.venv\Scripts\python.exe tools/firmware.py prepare
.\.venv\Scripts\python.exe tools/firmware.py install
```

`backup` performs two independent 16-MiB reads and compares them. Save
`work/original.bin`, `work/original-verify.bin` and the checksum file somewhere
outside the computer too. Do not upload them to GitHub.

`prepare` accepts only the tested factory firmware, updates its own boot package,
and rebuilds its rootfs while verifying the allowed script changes.

The last command prints the plan and does **not** open the device or write it.
Review `work/prepared/manifest.json` before the next step.

## 5. Flash and verify

Keep FEL connected and the power supply stable:

```powershell
.\.venv\Scripts\python.exe tools/firmware.py install --write
```

The installer makes another full backup, checks the source partitions, writes
rootfs and boot package, reads back each, then verifies all 16 MiB. Wait for
**`FULL_FLASH_VERIFIED`** before disconnecting. This takes several minutes.

## 6. Use the data card and check ADB

Use a second, normal FAT32 card. If it needs formatting, back up its files first.
For a large card, [Rufus](https://rufus.ie/) can create a non-bootable MBR/FAT32
data card: select only that card, choose **Non bootable**, **MBR**, and
**FAT32 / Large FAT32**. This erases the selected card. See the
[upstream FAQ](https://github.com/pbatard/rufus/wiki/FAQ) for details. Do not
reformat your FEL recovery card.

Copy the contents of `sd-card/` (included demo) or `build/sd-card/` (your rebuild)
to the data card's root. Power off, remove the FEL card, insert the data card and boot normally.
Do not keep the power button held as for FEL.

After about 25 seconds the demo should appear automatically:

```powershell
.\platform-tools\adb.exe devices -l
.\platform-tools\adb.exe shell 'cat /tmp/usb-adb-boot-fix.log; cat /tmp/user-app.log'
```

ADB is a separate USB device, **`18D1:D002` / Tina ADB**. Windows may need WinUSB
for this device too. The FEL driver's presence alone does not prove that the ADB
driver is present. A short USB disconnect during startup is expected; the boot
correction restores device mode automatically.

Tap **Back to menu** to end the demo until the next boot. Without the data card,
the original interface runs. See [recovery](recovery.md) before making further
firmware changes.
