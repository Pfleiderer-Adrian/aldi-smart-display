# Validation record

## Development hardware

On the original V853/16-MiB/1024×600 unit, the development process verified:

- Identical independent original SPI-NOR backups.
- Boot-package USB device-mode modification and ADB shell correction.
- USB recovery with cdr resumed after its brief pause.
- Rootfs write read-back and complete-flash comparisons after both later patches.
- Goodix touch coordinates and native framebuffer drawing including alpha.
- Execution from the FAT32 data card and clean return to the manufacturer UI.
- Automatic SD-script launch after a cold boot.
- Normal manufacturer UI and available ADB after a cold boot without a card.

## Clean publication package

On Windows x64:

- Pinned libusb/SquashFS downloads matched their recorded SHA-256 values.
- The FEL helper compiled from the vendored V853 source.
- The static ARM demo compiled using the included, redistributable DejaVu font.
- The FEL image placed the known stub exactly at offset 8192.
- All 13 offline safety tests passed.
- Preparing firmware from the private original factory backup completed; the
  boot package matched the previously verified device-mode package hash.
- The SquashFS round trip preserved all unrelated content and metadata, changed
  only the ADB wrapper, and added the two intended startup scripts.
- The install preview did not access or write a device.

The cleaned, combined factory-to-ADB installer has not been used to reflash a
second factory display. The new font changes appearance slightly; the framebuffer
and touch code are the same as the tested corrected demo.

Ubuntu hardware access was used during the original investigation. The cleaned
publication package's Ubuntu build and tests are specified in GitHub Actions;
they must be checked after a real workflow run. No Ubuntu result is implied by
the presence of the workflow file alone.
