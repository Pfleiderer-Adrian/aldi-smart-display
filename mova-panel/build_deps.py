"""Fetch checksum-pinned build inputs into ignored build/dependencies/."""
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import tarfile
import tempfile
import urllib.request

MAX_ARCHIVE = 32 * 1024 * 1024
MAX_SOURCES = 64 * 1024 * 1024


def _sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def _wanted(name, relative):
    parts = relative.parts
    if name == 'mbedtls':
        return parts[0] in ('include', 'library') or relative.as_posix() == 'LICENSE'
    if name == 'zlib':
        return len(parts) == 1 and (relative.suffix in ('.c', '.h') or parts[0] == 'LICENSE')
    raise ValueError('Unknown build dependency')


def prepare_dependency(spec, cache):
    cache = Path(cache).resolve()
    cache.mkdir(parents=True, exist_ok=True)
    root = spec['root']
    if not root or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._' for c in root) or root in ('.', '..'):
        raise ValueError('Invalid dependency root')
    archive = cache / (root + '.tar.gz')
    if not archive.exists():
        if not spec['url'].startswith('https://'):
            raise ValueError('Dependency download requires HTTPS')
        # Only verified, complete downloads enter the persistent cache.
        with tempfile.NamedTemporaryFile(dir=cache, suffix='.part', delete=False) as stream:
            temporary = Path(stream.name)
            try:
                size = 0
                with urllib.request.urlopen(spec['url'], timeout=60) as response:
                    for block in iter(lambda: response.read(1024 * 1024), b''):
                        size += len(block)
                        if size > MAX_ARCHIVE:
                            raise ValueError('Dependency archive exceeds size limit')
                        stream.write(block)
                stream.close()
                if _sha256(temporary) != spec['sha256']:
                    raise ValueError('Dependency SHA-256 mismatch: ' + root)
                os.replace(temporary, archive)
            finally:
                stream.close()
                if temporary.exists():
                    temporary.unlink()
    if archive.stat().st_size > MAX_ARCHIVE or _sha256(archive) != spec['sha256']:
        raise ValueError('Cached dependency SHA-256 mismatch: ' + root)

    # Derive the expected files from the verified archive, not a mutable marker.
    expected = {}
    total = 0
    with tarfile.open(archive, 'r:gz') as bundle:
        for member in bundle.getmembers():
            path = PurePosixPath(member.name)
            if path.is_absolute() or '..' in path.parts or '\\' in member.name or not path.parts or path.parts[0] != root:
                raise ValueError('Unsafe dependency archive path')
            relative = PurePosixPath(*path.parts[1:])
            if not path.parts[1:] or not _wanted(spec['name'], relative):
                continue
            if member.isdir():
                continue
            if not member.isfile():
                raise ValueError('Links and special files are not allowed in build inputs')
            total += member.size
            if total > MAX_SOURCES or relative.as_posix() in expected:
                raise ValueError('Invalid or oversized dependency sources')
            expected[relative.as_posix()] = bundle.extractfile(member).read()
    if 'LICENSE' not in expected:
        raise ValueError('Dependency archive lacks its license')
    destination = cache / 'sources' / root
    if destination.exists():
        actual = {p.relative_to(destination).as_posix() for p in destination.rglob('*') if p.is_file()}
        if actual != set(expected) or any((destination / name).is_symlink() or (destination / name).read_bytes() != data for name, data in expected.items()):
            raise ValueError('Modified/incomplete dependency cache: ' + str(destination))
    else:
        destination.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=destination.parent) as temporary:
            staging = Path(temporary) / root
            staging.mkdir()
            for name, data in expected.items():
                target = staging / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
            os.replace(staging, destination)
    return destination


def prepare_dependencies(root):
    root = Path(root)
    specs = json.loads((root / 'dependencies.lock.json').read_text(encoding='utf-8'))
    return {spec['name']: prepare_dependency(spec, root / 'build/dependencies') for spec in specs}
