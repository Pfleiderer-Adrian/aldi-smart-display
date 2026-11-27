"""Offline checks for pinned downloads and safe dependency cache handling."""
import hashlib
import io
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest.mock import patch

from build_deps import prepare_dependency


def fixture(extra=None):
    output = io.BytesIO()
    files = {'zlib-1.3.1/LICENSE': b'Test license\n', 'zlib-1.3.1/zlib.h': b'test header\n'}
    files.update(extra or {})
    with tarfile.open(fileobj=output, mode='w:gz') as bundle:
        for name, data in files.items():
            member = tarfile.TarInfo(name)
            member.size = len(data)
            bundle.addfile(member, io.BytesIO(data))
    return output.getvalue()


def spec(data):
    return dict(name='zlib', version='1.3.1', root='zlib-1.3.1',
                url='https://example.invalid/zlib.tar.gz', sha256=hashlib.sha256(data).hexdigest())


class DependencyTests(unittest.TestCase):
    def test_download_verified_and_offline_cache(self):
        data = fixture()
        with tempfile.TemporaryDirectory() as folder:
            with patch('build_deps.urllib.request.urlopen', return_value=io.BytesIO(data)) as request:
                result = prepare_dependency(spec(data), folder)
                self.assertEqual((result / 'zlib.h').read_bytes(), b'test header\n')
                self.assertEqual(prepare_dependency(spec(data), folder), result)
                self.assertEqual(request.call_count, 1)

    def test_wrong_download_hash_is_rejected(self):
        data = fixture()
        locked = spec(data) | {'sha256': '0' * 64}
        with tempfile.TemporaryDirectory() as folder:
            with patch('build_deps.urllib.request.urlopen', return_value=io.BytesIO(data)):
                with self.assertRaisesRegex(ValueError, 'SHA-256'):
                    prepare_dependency(locked, folder)
            self.assertEqual(list(Path(folder).iterdir()), [])

    def test_corrupt_archive_cache_is_rejected(self):
        data = fixture()
        with tempfile.TemporaryDirectory() as folder:
            (Path(folder) / 'zlib-1.3.1.tar.gz').write_bytes(b'corrupt')
            with self.assertRaisesRegex(ValueError, 'SHA-256'):
                prepare_dependency(spec(data), folder)

    def test_changed_or_extra_source_is_rejected(self):
        for mutation in ['changed', 'extra']:
            data = fixture()
            with tempfile.TemporaryDirectory() as folder:
                with patch('build_deps.urllib.request.urlopen', return_value=io.BytesIO(data)):
                    result = prepare_dependency(spec(data), folder)
                (result / ('zlib.h' if mutation == 'changed' else 'injected.c')).write_bytes(b'modified')
                with self.assertRaisesRegex(ValueError, 'Modified/incomplete'):
                    prepare_dependency(spec(data), folder)

    def test_archive_path_escape_is_rejected(self):
        data = fixture({'zlib-1.3.1/../escape.h': b'bad'})
        with tempfile.TemporaryDirectory() as folder:
            with patch('build_deps.urllib.request.urlopen', return_value=io.BytesIO(data)):
                with self.assertRaisesRegex(ValueError, 'Unsafe'):
                    prepare_dependency(spec(data), folder)
            self.assertFalse((Path(folder) / 'escape.h').exists())


if __name__ == '__main__':
    unittest.main()
