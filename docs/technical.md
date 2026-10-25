# What we changed to obtain ADB

The display already contained an ADB daemon and USB configuration scripts, but
they did not provide a stable usable root shell in the original boot setup.
We did not replace its Linux kernel or write a new display driver.

The working sequence was:

1. Put the upstream `fel-sdboot.sunxi` at byte offset 8192 on a separate microSD.
2. Boot with that card and the power button held to reach Allwinner FEL.
3. Read the complete 16-MiB SPI NOR twice and compare the backups.
4. Add V853 SPI support to linux-sunxi's FEL code so this flash could be read
   and written on the actual chip.
5. Change the USB role in the boot package's device tree to device mode.
6. Add early ADB startup and replace the ADB login wrapper with a shell wrapper.
7. Address a later USB-role change performed by the manufacturer's application.
8. Add the optional data-card startup hook.

## Boot package

On the tested firmware the boot package occupies `0x10000..0x70000` in flash.
It is a `sunxi-package` container of `0x60000` bytes, with its FDT at package
offset `0x45800`.

The FDT property is:

```text
/soc@03000000/usbc0@0/usb_port_type
```

Its four-byte big-endian value at package offset `0x50fa8` changes from `1`
(host) to `0` (device). The builder recalculates the container's little-endian
additive checksum at offset `0x14`, using checksum stamp `0x5f0a6c39` and the
declared container length at `0x24`.

It checks the entire original and patched package hashes, not just a guessed
byte offset. Other builds are rejected.

## Rootfs

The `0xc70000`-byte rootfs partition starts at `0x2f0000`.
The project changes three paths:

| Path | Change |
|---|---|
| `/bin/adb_shell` | Run `/bin/sh` with the existing ADB profile instead of `/bin/login` |
| `/etc/init.d/S55adbd` | Early ADB startup and one USB-role recovery after manufacturer initialization |
| `/etc/init.d/S80userapp` | Run a data card's root-level `autostart.sh` once during boot |

The stock rootfs is exported as a tar stream, changed without extracting onto
the host filesystem, rebuilt with XZ/262144-byte blocks and exported again.
File contents, ownership, modes, symlinks, device nodes, timestamps and extended
attributes outside the intended paths must match. The existing ADB wrapper's
metadata must also match. No manufacturer executable is replaced.

Factory profile hashes:

```text
boot:   2353ae99beab1f045a8c63c1371f2b0468e2b33e7945a2c5b36b2e9bc3815b68
rootfs: 2c67484c169b58f8f0d65b39993de4df5155b6465fa32f9b945f3bcdebc0e759
```

These are hashes of the complete partition/package regions, not of the entire
per-device firmware backup.

## Why ADB originally disconnected

Resuming the manufacturer app `cdr` changed `usbc0/otg_role` from `usb_device`
to `usb_host`. Kernel logs showed `rmmod_device_driver`,
`USB_STATE=DISCONNECTED` and `insmod_host_driver`. The ADB daemon remained alive.

String inspection and ARM disassembly located host/device role handling in
`liblylinkui.so` and `liblylinkusb.so`. Both contain code that reads role-trigger
sysfs files and checks `/tmp/lyusbcp` during initialization.

The installed correction waits for that marker, lets initialization settle,
briefly pauses `cdr`, reads the `usb_device` trigger, restores ADB, then resumes
`cdr`. It runs once, not as a permanent role-enforcing loop. During startup USB
may disappear briefly before returning at roughly 23–25 seconds.

## Display and touch

The tested framebuffer has virtual size `1024,1200`, stride 2048 and 16-bit
pixels. It has one alpha bit at bit 15 and RGB offsets 10/5/0: ARGB1555.
The demo queries the layout and sets the alpha bits fully opaque. Writing RGB
alone originally produced a black physical display even though an RGB-only
framebuffer preview looked fine.

Goodix touch events arrive through `/dev/input/event1`. The demo reads X/Y and
pressure events, mirrors both framebuffer pages and restores the previous
contents when it exits.

## Boundaries

This enables a local root ADB shell; treat USB access as trusted access. It does
not enable ADB over Wi-Fi. CarPlay, Android Auto, Bluetooth pairing and USB-host
accessories have not been comprehensively regression-tested after the USB-role
change. A second, different hardware revision is not yet supported merely
because it also uses an Allwinner chip.
