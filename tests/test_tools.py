# SPDX-License-Identifier: GPL-2.0-or-later
"""Offline safety tests; no device, network or manufacturer firmware required."""
import contextlib
import io
import json
from pathlib import Path
import struct
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import firmware as fw
import make_fel_card


def add(archive, name, content, mode=0o755):
    info = tarfile.TarInfo(name)
    info.mode, info.mtime, info.size = mode, 12345, len(content)
    archive.addfile(info, io.BytesIO(content))


class HostToolsTests(unittest.TestCase):
    def test_fel_image_exact_offset(self):
        stub = (fw.ROOT / "assets/fel-sdboot.sunxi").read_bytes()
        image = make_fel_card.image_bytes(stub)
        self.assertEqual(len(image), 1024 * 1024)
        self.assertEqual(image[8192:16384], stub)
        self.assertEqual(image[:8192], bytes(8192))
        self.assertEqual(image[16384:], bytes(len(image) - 16384))

    def test_unknown_fel_stub_rejected(self):
        with self.assertRaises(ValueError):
            make_fel_card.image_bytes(bytes(8192))

    def test_backup_must_be_complete(self):
        for n in (0, fw.FLASH_SIZE - 1, fw.FLASH_SIZE + 1):
            with self.assertRaises(ValueError):
                fw.sections(bytes(n))

    def test_unknown_boot_rejected(self):
        with self.assertRaises(ValueError):
            fw.patch_boot(bytes(fw.BOOT_SIZE))

    def test_boot_checksum_stamp_and_carry(self):
        data = bytearray(fw.BOOT_SIZE)
        data[:13] = b"sunxi-package"
        struct.pack_into("<I", data, 0x24, len(data))
        a = fw.boot_checksum(data)
        struct.pack_into("<I", data, 0x14, 0xDEADBEEF)
        self.assertEqual(fw.boot_checksum(data), a)
        struct.pack_into(">I", data, 0x50FA8, 1)
        self.assertEqual(fw.boot_checksum(data), (a + 0x1000000) & 0xFFFFFFFF)

    def test_archive_traversal_rejected(self):
        for name in ("../etc/passwd", "./../x", "/etc/shadow"):
            with self.assertRaises(ValueError):
                fw.canonical(name)

    def test_archive_changes_are_limited(self):
        with tempfile.TemporaryDirectory() as tmp:
            src, dst = Path(tmp) / "old.tar", Path(tmp) / "new.tar"
            with tarfile.open(src, "w") as t:
                add(t, "./bin/adb_shell", b"stock login wrapper")
                add(t, "./etc/keep", b"unchanged", 0o640)
                m = tarfile.TarInfo("./bin/sh")
                m.type, m.linkname, m.mode = tarfile.SYMTYPE, "busybox", 0o777
                t.addfile(m)
            fw.patch_tar(src, dst)
            old, new = fw.manifest(src), fw.manifest(dst)
            self.assertEqual(old["etc/keep"], new["etc/keep"])
            self.assertEqual(old["bin/sh"], new["bin/sh"])
            self.assertEqual(old["bin/adb_shell"][:-1], new["bin/adb_shell"][:-1])
            self.assertEqual(set(new) - set(old), {"etc/init.d/S55adbd", "etc/init.d/S80userapp"})

    def test_existing_custom_hook_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            src, dst = Path(tmp) / "old.tar", Path(tmp) / "new.tar"
            with tarfile.open(src, "w") as t:
                add(t, "./bin/adb_shell", b"old")
                add(t, "./etc/init.d/S55adbd", b"custom")
            with self.assertRaises(ValueError):
                fw.patch_tar(src, dst)

    def test_manifest_duplicate_paths_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "dup.tar"
            with tarfile.open(src, "w") as t:
                add(t, "./bin/sh", b"one")
                add(t, "bin/sh", b"two")
            with self.assertRaises(ValueError):
                fw.manifest(src)

    def test_install_defaults_to_no_device_access(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            original = bytes(fw.FLASH_SIZE)
            boot, root = bytes(fw.BOOT_SIZE), bytes(fw.ROOT_SIZE)
            (d / "original.bin").write_bytes(original)
            (d / "boot-device.bin").write_bytes(boot)
            (d / "rootfs-patched.bin").write_bytes(root)
            (d / "manifest.json").write_text(json.dumps({
                "verified": True, "original_sha256": fw.sha(original),
                "boot_sha256": fw.sha(boot), "rootfs_sha256": fw.sha(root)}))
            with patch.object(fw, "fel", side_effect=AssertionError("Device accessed")), contextlib.redirect_stdout(io.StringIO()):
                fw.apply(d / "original.bin", d)

    def test_unknown_stock_rootfs_rejected_before_build(self):
        with tempfile.TemporaryDirectory() as tmp:
            b = Path(tmp) / "unknown.bin"
            b.write_bytes(bytes(fw.FLASH_SIZE))
            with patch.object(fw, "run", side_effect=AssertionError("Builder called")):
                with self.assertRaises(ValueError):
                    fw.prepare(b, Path(tmp) / "prepared")

    def test_partial_recovery_cannot_bypass_normal_install(self):
        with self.assertRaises(ValueError):
            fw.apply(Path("unused"), Path("unused"), allow_partial=True)

    def test_changed_prepared_file_rejected_before_device_access(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            original = bytes(fw.FLASH_SIZE)
            (d / "original.bin").write_bytes(original)
            (d / "boot-device.bin").write_bytes(bytes(fw.BOOT_SIZE))
            (d / "rootfs-patched.bin").write_bytes(b"wrong")
            (d / "manifest.json").write_text(json.dumps({
                "verified": True, "original_sha256": fw.sha(original),
                "boot_sha256": fw.sha(bytes(fw.BOOT_SIZE)),
                "rootfs_sha256": fw.sha(bytes(fw.ROOT_SIZE))}))
            with patch.object(fw, "fel", side_effect=AssertionError("Device accessed")):
                with self.assertRaises(ValueError):
                    fw.apply(d / "original.bin", d, write=True)


if __name__ == "__main__":
    unittest.main()
