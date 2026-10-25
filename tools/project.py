# SPDX-License-Identifier: GPL-2.0-or-later
"""Paths and subprocess helpers shared by the host tools."""
from pathlib import Path
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"


def run(args, **kwargs):
    print("+", " ".join(map(str, args)), flush=True)
    return subprocess.run(list(map(str, args)), check=True, **kwargs)


def tool(name):
    exe = name + (".exe" if os.name == "nt" else "")
    candidates = [BUILD / "tools" / exe]
    candidates += list((BUILD / "tools").glob("**/bin/" + exe))
    for path in candidates:
        if path.is_file():
            return str(path)
    found = shutil.which(exe)
    if found:
        return found
    raise RuntimeError(f"Missing {name}; see docs/windows.md or docs/ubuntu.md")


def zig():
    import ziglang
    return Path(ziglang.__file__).parent / ("zig.exe" if os.name == "nt" else "zig")
