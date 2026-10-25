# SPDX-License-Identifier: GPL-2.0-or-later
"""Build per-device firmware from a private factory backup and verify FEL writes.

No manufacturer images are distributed by this project. Firmware files stay in
the ignored work/ folder. The supported profile deliberately rejects unknown builds.
"""
import argparse
import copy
import datetime
import hashlib
import io
import json
from pathlib import Path
import struct
import tarfile
from project import ROOT, run, tool

FLASH_SIZE = 0x1000000
BOOT_OFFSET, BOOT_SIZE = 0x10000, 0x60000
ROOT_OFFSET, ROOT_SIZE = 0x2F0000, 0xC70000
STOCK_BOOT = "2353ae99beab1f045a8c63c1371f2b0468e2b33e7945a2c5b36b2e9bc3815b68"
DEVICE_BOOT = "1f881cf4cf910bccaca344dc7b6ecfe4b9f386b43671a8d549217f8151760840"
STOCK_ROOT = "2c67484c169b58f8f0d65b39993de4df5155b6465fa32f9b945f3bcdebc0e759"
PATCHED_PATHS = {"bin/adb_shell", "etc/init.d/S55adbd", "etc/init.d/S80userapp"}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def sections(data):
    if len(data) != FLASH_SIZE:
        raise ValueError("Expected a complete 16-MiB backup")
    return data[BOOT_OFFSET:BOOT_OFFSET + BOOT_SIZE], data[ROOT_OFFSET:ROOT_OFFSET + ROOT_SIZE]


def boot_checksum(data):
    if len(data) != BOOT_SIZE or not data.startswith(b"sunxi-package"):
        raise ValueError("Unsupported boot package")
    length = struct.unpack_from("<I", data, 0x24)[0]
    if length != len(data):
        raise ValueError("Invalid boot package size")
    temp = bytearray(data)
    struct.pack_into("<I", temp, 0x14, 0x5F0A6C39)
    return sum(struct.unpack("<%dI" % (len(temp) // 4), temp)) & 0xFFFFFFFF


def patch_boot(data):
    digest = sha(data)
    if digest not in {STOCK_BOOT, DEVICE_BOOT}:
        raise ValueError("Unknown boot package: no changes made")
    if struct.unpack_from("<I", data, 0x14)[0] != boot_checksum(data):
        raise ValueError("Boot checksum mismatch")
    patched = bytearray(data)
    # Verified FDT property /soc@03000000/usbc0@0/usb_port_type.
    if patched[0x50FA8:0x50FAC] not in (b"\0\0\0\1", b"\0\0\0\0"):
        raise ValueError("USB property differs")
    patched[0x50FA8:0x50FAC] = b"\0\0\0\0"
    struct.pack_into("<I", patched, 0x14, boot_checksum(patched))
    if sha(patched) != DEVICE_BOOT:
        raise ValueError("Patched boot package does not match tested result")
    return bytes(patched)


def canonical(name):
    while name.startswith("./"):
        name = name[2:]
    if name.startswith("/") or ".." in name.split("/"):
        raise ValueError("Unsafe archive path")
    return name.rstrip("/")


def manifest(path):
    result = {}
    with tarfile.open(path) as archive:
        for m in archive:
            name = canonical(m.name)
            if name in result:
                raise ValueError("Duplicate archive path")
            result[name] = (m.type, m.mode, m.uid, m.gid, m.mtime, m.linkname,
                            m.devmajor, m.devminor, m.pax_headers,
                            sha(archive.extractfile(m).read()) if m.isfile() else None)
    return result


def patch_tar(source, target):
    payloads = {
        "bin/adb_shell": (ROOT / "firmware/adb_shell").read_bytes(),
        "etc/init.d/S55adbd": (ROOT / "firmware/S55adbd").read_bytes(),
        "etc/init.d/S80userapp": (ROOT / "firmware/S80userapp").read_bytes(),
    }
    if any(b"\r" in b for b in payloads.values()):
        raise ValueError("Firmware scripts must use LF line endings")
    with tarfile.open(source) as src, tarfile.open(target, "w", format=tarfile.PAX_FORMAT) as dst:
        for entry in src:
            m = copy.deepcopy(entry)
            name = canonical(m.name)
            if name in payloads:
                if name != "bin/adb_shell" or not m.isfile():
                    raise ValueError("Stock firmware already contains custom startup hooks")
                content = payloads.pop(name)
                m.size = len(content)
                dst.addfile(m, io.BytesIO(content))
            else:
                dst.addfile(m, src.extractfile(entry) if m.isfile() else None)
        if "bin/adb_shell" in payloads:
            raise ValueError("Stock ADB shell wrapper missing")
        for name, content in payloads.items():
            m = tarfile.TarInfo("./" + name)
            m.mode, m.uid, m.gid, m.mtime, m.size = 0o755, 0, 0, 0, len(content)
            dst.addfile(m, io.BytesIO(content))


def prepare(backup, dest):
    data = backup.read_bytes()
    boot, rootfs = sections(data)
    if sha(rootfs) != STOCK_ROOT:
        raise ValueError("Unsupported factory rootfs. Keep your backup; do not flash another device's image.")
    patched_boot = patch_boot(boot)
    dest.mkdir(parents=True, exist_ok=True)
    (dest / "stock-rootfs.bin").write_bytes(rootfs)
    (dest / "boot-device.bin").write_bytes(patched_boot)
    with (dest / "stock.tar").open("wb") as output:
        run([tool("sqfs2tar"), "-s", "-r", ".", dest / "stock-rootfs.bin"], stdout=output)
    patch_tar(dest / "stock.tar", dest / "patched.tar")
    with (dest / "patched.tar").open("rb") as source:
        run([tool("tar2sqfs"), "-f", "-q", "-s", "-c", "xz", "-b", "262144", "-e", "-j", "2",
             dest / "rootfs.squashfs"], stdin=source)
    image = (dest / "rootfs.squashfs").read_bytes()
    if len(image) > ROOT_SIZE:
        raise ValueError("New rootfs exceeds partition")
    image += b"\xff" * (ROOT_SIZE - len(image))
    (dest / "rootfs-patched.bin").write_bytes(image)
    with (dest / "verified.tar").open("wb") as output:
        run([tool("sqfs2tar"), "-s", "-r", ".", dest / "rootfs-patched.bin"], stdout=output)
    old, new = manifest(dest / "stock.tar"), manifest(dest / "verified.tar")
    if set(new) - set(old) != {"etc/init.d/S55adbd", "etc/init.d/S80userapp"}:
        raise ValueError("Unexpected added files")
    if set(old) - set(new):
        raise ValueError("Files missing from new rootfs")
    if any(old[n] != new[n] for n in old if n not in PATCHED_PATHS):
        raise ValueError("Unrelated files or metadata changed")
    if old["bin/adb_shell"][:-1] != new["bin/adb_shell"][:-1]:
        raise ValueError("ADB wrapper metadata changed")
    for name, source in (("bin/adb_shell", "adb_shell"), ("etc/init.d/S55adbd", "S55adbd"),
                         ("etc/init.d/S80userapp", "S80userapp")):
        if new[name][-1] != sha((ROOT / "firmware" / source).read_bytes()):
            raise ValueError("A patched script differs after the SquashFS round trip")
        if new[name][1:4] != (0o755, 0, 0):
            raise ValueError("Patched script mode or owner differs")
    report = {"original_sha256": sha(data), "original_boot_sha256": sha(boot),
              "original_rootfs_sha256": sha(rootfs), "boot_sha256": sha(patched_boot),
              "rootfs_sha256": sha(image), "verified": True,
              "changed_paths": sorted(PATCHED_PATHS)}
    (dest / "manifest.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print("Prepared and verified:", dest)


def fel(*args):
    run([tool("fel-v853"), *args])


def read_flash(path):
    if path.exists():
        raise ValueError(f"Refusing to overwrite {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    fel("read", "0", hex(FLASH_SIZE), path)
    if path.stat().st_size != FLASH_SIZE:
        raise ValueError("Incomplete flash read")
    return path.read_bytes()


def backup_device(path):
    first = read_flash(path)
    second_path = path.with_name(path.stem + "-verify.bin")
    second = read_flash(second_path)
    if first != second:
        raise ValueError("Independent flash reads differ; do not proceed")
    path.with_suffix(path.suffix + ".sha256").write_text(sha(first) + "  " + path.name + "\n")
    print("Two independent backups match:", sha(first))


def apply(backup, dest, write=False, restore=False, allow_partial=False):
    if allow_partial and not restore:
        raise ValueError("Partial recovery is only supported by restore")
    original = backup.read_bytes()
    old_boot, old_root = sections(original)
    report = json.loads((dest / "manifest.json").read_text())
    if not report.get("verified") or sha(original) != report["original_sha256"]:
        raise ValueError("Backup does not match prepared files")
    boot = (dest / "boot-device.bin").read_bytes()
    rootfs = (dest / "rootfs-patched.bin").read_bytes()
    if len(boot) != BOOT_SIZE or len(rootfs) != ROOT_SIZE or sha(boot) != report["boot_sha256"] or sha(rootfs) != report["rootfs_sha256"]:
        raise ValueError("Prepared file checksum mismatch")
    if not write:
        print("Plan:", "restore original" if restore else "install ADB + USB fix + SD autostart")
        print("Only boot package [0x10000,0x70000) and rootfs [0x2f0000,0xf60000) may change.")
        print("Add --write to execute. No device has been opened.")
        return
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%d-%H%M%S")
    before = read_flash(dest / (stamp + "-before.bin"))
    actual_boot, actual_root = sections(before)
    expected_boot, expected_root = (boot, rootfs) if restore else (old_boot, old_root)
    if (actual_boot != expected_boot or actual_root != expected_root) and not (restore and allow_partial):
        raise ValueError("Device differs from the expected source version; no write")
    if restore and allow_partial:
        print("Recovery explicitly allows a partially written source; fresh full backup retained.")
    target_boot, target_root = (old_boot, old_root) if restore else (boot, rootfs)
    root_path, boot_path = dest / "write-rootfs.bin", dest / "write-boot.bin"
    root_path.write_bytes(target_root)
    boot_path.write_bytes(target_boot)
    # Prepare rootfs before switching the boot USB role.
    fel("write-rootfs", root_path)
    fel("write-boot", boot_path)
    after = read_flash(dest / (stamp + "-after.bin"))
    expected = bytearray(before)
    expected[BOOT_OFFSET:BOOT_OFFSET + BOOT_SIZE] = target_boot
    expected[ROOT_OFFSET:ROOT_OFFSET + ROOT_SIZE] = target_root
    if after != expected:
        raise ValueError("Full flash differs: remain in FEL mode and retain all backups")
    (dest / (stamp + "-result.json")).write_text(json.dumps({
        "before_sha256": sha(before), "after_sha256": sha(after), "restore": restore,
        "unchanged_outside_boot_and_rootfs": True}, indent=2))
    print("FULL_FLASH_VERIFIED. Power off, remove FEL card, then boot normally.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    p = commands.add_parser("backup")
    p.add_argument("--out", type=Path, default=ROOT / "work/original.bin")
    p = commands.add_parser("prepare")
    p.add_argument("--backup", type=Path, default=ROOT / "work/original.bin")
    p.add_argument("--out", type=Path, default=ROOT / "work/prepared")
    for name in ("install", "restore"):
        p = commands.add_parser(name)
        p.add_argument("--backup", type=Path, default=ROOT / "work/original.bin")
        p.add_argument("--prepared", type=Path, default=ROOT / "work/prepared")
        p.add_argument("--write", action="store_true")
        if name == "restore":
            p.add_argument("--allow-partial", action="store_true",
                           help="Recover interrupted writes using your verified original backup")
    args = parser.parse_args()
    if args.command == "backup":
        backup_device(args.out)
    elif args.command == "prepare":
        prepare(args.backup, args.out)
    else:
        apply(args.backup, args.prepared, args.write, args.command == "restore", getattr(args, "allow_partial", False))


if __name__ == "__main__":
    main()
