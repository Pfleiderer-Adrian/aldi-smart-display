# Developing SD applications

Once the firmware hook is installed, put a normal FAT32 data card in the
display before starting it. It must have `autostart.sh` in its root.

The firmware calls it using `/bin/sh`, with its working directory set to
`/mnt/extsd` and `SD_ROOT=/mnt/extsd`. It checks for at most 110 seconds during
boot. This is not a general, permanent hotplug watcher.

For example:

```sh
#!/bin/sh
exec /bin/sh "$SD_ROOT/my-app/start.sh"
```

The demo's own root script also works when called by its absolute path manually.
Its launcher pauses `cdr` while drawing and resumes it on exit. Your replacement
application is responsible for its own display ownership and cleanup.

## ADB commands

On Ubuntu use `adb`. On Windows replace it with `.\platform-tools\adb.exe`.
The commands below use single-quoted shell arguments so both shells preserve
remote `$` expressions correctly.

```text
adb devices -l
adb shell 'cat /tmp/usb-adb-boot-fix.log; cat /tmp/user-app.log'
```

Start the SD application manually:

```text
adb shell 'setsid /bin/sh /mnt/extsd/autostart.sh </dev/null >>/tmp/user-app.log 2>&1 &'
```

Stop the demo and return to the manufacturer interface:

```text
adb shell 'kill -TERM "$(cat /tmp/hello-world.lock/app.pid)"'
```

Disable automatic SD startup until re-enabled:

```text
adb shell 'touch /data/disable-user-app; sync; reboot'
```

Enable it again:

```text
adb shell 'rm -f /data/disable-user-app; sync; reboot'
```

## Replace files safely

Exit the running application before overwriting its executable. Rebuild the
demo with `tools/build_demo.py`, then copy `build/sd-card/` onto the data card.
You can copy via a PC reader, or use `adb push` while the application is stopped.
Keep shell scripts in LF format.

Before physically removing a card from the running display, stop its app,
run `adb shell 'sync; umount /mnt/extsd'` and ensure unmount succeeds. Switching
the device off before removal is another straightforward option.

## Troubleshooting

| Symptom | Check |
|---|---|
| No FEL device | Correct FEL card, button sequence, data cable/port and `1f3a:efe8` |
| Windows FEL works, ADB does not | Tina ADB `18d1:d002` has its own driver association |
| ADB disappears during boot | Wait about 25 seconds; inspect the USB-fix log |
| Original UI instead of demo | FAT32 card, root-level `autostart.sh`, startup log, disable marker |
| Black screen but touch works | Preserve the framebuffer alpha field; ARGB1555 needs bit 15 set |
| SD listing hangs | Stop using the filesystem; check kernel errors before writing or formatting |
| Firmware builder rejects backup | Different firmware/profile; do not bypass the hash checks |

Use a separate data card and keep the FEL card available for recovery. Do not
format a card just because a directory listing is slow; it may contain valuable
data or have filesystem damage.
