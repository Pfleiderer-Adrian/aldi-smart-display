# Disable changes or restore firmware

Keep the FEL card and the complete original 16-MiB backup. The project does not
distribute someone else's backup as a recovery image.

## Disable SD startup without reflashing

Create `/data/disable-user-app` over ADB and reboot. The manufacturer interface
then runs even if a card contains `autostart.sh`. Remove the marker to re-enable.
Booting without the data card also leaves the manufacturer interface active.

The USB startup correction can separately be disabled using
`/data/disable-usb-adb-fix`. Doing so can restore the old USB disconnection
behavior, so leave it enabled unless diagnosing USB-host functionality.

## Restore the original boot package and rootfs

Power off, insert the FEL recovery card and use the same power-button/data-USB
sequence. Confirm the V853 is reachable. Keep `work/original.bin` and the
matching `work/prepared/manifest.json` together.

Ubuntu:

```bash
.venv/bin/python tools/firmware.py restore
.venv/bin/python tools/firmware.py restore --write
```

Windows:

```powershell
.\.venv\Scripts\python.exe tools/firmware.py restore
.\.venv\Scripts\python.exe tools/firmware.py restore --write
```

Without `--write` this only prints the plan. The normal restore requires the
device's source partitions to match the prepared patched version. It reads a
fresh complete backup, writes the original boot package/rootfs, reads both back
and compares the whole flash. Device settings outside those regions remain as
they were immediately before restoration.

## Recover an interrupted write

If power loss left one of the two regions partially written, the normal restore
will reject the unknown source. FEL operates below the installed Linux firmware.
After confirming power, cable, device and **your own verified original backup**,
use the explicit partial-recovery option:

```text
python tools/firmware.py restore --write --allow-partial
```

Replace `python` with your platform's venv interpreter as above. This option is
available only for restoration. It retains a fresh full before-image and still
checks the original backup against the prepared manifest, the output hashes and
the complete post-write flash. It is not a way to install an unknown firmware.

Wait for `FULL_FLASH_VERIFIED`, then power off, remove the FEL card and boot
normally. If USB disappears mid-transfer, retain all files and remain in FEL
until the state can be checked; do not repeatedly try unrelated images.
