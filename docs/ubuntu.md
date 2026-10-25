# Ubuntu: from a factory display to the SD demo

Run commands from the repository folder. Ubuntu 24.04 x64 is the CI target;
the earlier hardware investigation was performed on Ubuntu 22.04. The cleaned
package's Ubuntu build results must be checked in CI.

## 1. Install host tools

```bash
sudo apt update
sudo apt install -y python3 python3-venv build-essential pkg-config \
  libusb-1.0-0-dev squashfs-tools-ng adb usbutils
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements.txt
.venv/bin/python tools/build_fel.py
.venv/bin/python tools/build_demo.py
.venv/bin/python tools/make_fel_card.py
```

If your distribution cannot find `squashfs-tools-ng`, use the
[upstream installation instructions](https://github.com/AgentD/squashfs-tools-ng).
This package uses `sqfs2tar` and `tar2sqfs` to preserve Unix metadata without
extracting the firmware onto Windows filesystems.

## 2. Create the separate FEL card

Identify the **whole removable disk**, not a partition:

```bash
lsblk -o NAME,SIZE,MODEL,TRAN,RM,MOUNTPOINTS
```

Unmount its mounted partitions first. The following `/dev/sdX` is a placeholder;
replace it with the confirmed FEL-card device. Writing to the wrong disk can
destroy its partition table. Do not use your system disk or the application card.

```bash
sudo dd if=build/fel-card.img of=/dev/sdX bs=1M conv=fsync status=progress
sync
```

This image contains the tested upstream stub at offset 8 KiB. Equivalently, the
original investigation used `dd if=fel-sdboot.sunxi ... bs=1024 seek=8`.
Keep this recovery card separate from the normal FAT32 data card.

## 3. Enter FEL and enable USB access

Power off, insert the FEL card, hold the power button and connect the USB data
port (Micro-USB on the development unit). Verify:

```bash
lsusb -d 1f3a:efe8
```

For access without running build scripts as root, create
`/etc/udev/rules.d/70-aldi-smart-display.rules` with these two lines:

```text
SUBSYSTEM=="usb", ATTR{idVendor}=="1f3a", ATTR{idProduct}=="efe8", MODE="0660", GROUP="plugdev", TAG+="uaccess"
SUBSYSTEM=="usb", ATTR{idVendor}=="18d1", ATTR{idProduct}=="d002", MODE="0660", GROUP="plugdev", TAG+="uaccess"
```

Reload rules and reconnect the device:

```bash
sudo udevadm control --reload-rules
sudo udevadm trigger
./build/tools/fel-v853 version
```

If your desktop session does not grant access, check membership of `plugdev`
and log in again after adding your account. Expected output: `SoC V853 ID 0x1886`.
No Zadig driver is needed on Ubuntu.

## 4. Back up, prepare and review

```bash
.venv/bin/python tools/firmware.py backup
.venv/bin/python tools/firmware.py prepare
.venv/bin/python tools/firmware.py install
```

The first command reads all 16 MiB twice and compares the results. Keep the
original files/checksum in `work/` private and save another copy elsewhere.
The builder refuses unknown factory boot/rootfs versions. The last command only
prints the plan; review `work/prepared/manifest.json`.

## 5. Flash and verify

```bash
.venv/bin/python tools/firmware.py install --write
```

Do not interrupt power. The command checks a fresh backup, writes only the boot
package and rootfs, verifies both and then compares the entire flash. Wait for
**`FULL_FLASH_VERIFIED`**. Expect several minutes.

## 6. Boot from the data card

Use the Disks application if you need to prepare the second card: select that
removable card, back up its files, then create a normal MBR/FAT32 data volume.
Formatting erases the selected card. Keep the FEL recovery card separate.

Copy the contents of `sd-card/` (included demo) or `build/sd-card/` (your rebuild)
to the root of that **second FAT32 card**.
Power off, remove the FEL card, insert the data card and boot normally.

After about 25 seconds:

```bash
adb devices -l
adb shell 'cat /tmp/usb-adb-boot-fix.log; cat /tmp/user-app.log'
```

The demo starts automatically from `/mnt/extsd/autostart.sh`. Without a card or
without that script, the manufacturer interface keeps running. **Back to menu**
returns to it until the next boot. See [development](development.md) and
[recovery](recovery.md) for follow-up commands.
