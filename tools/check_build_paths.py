#!/usr/bin/env python3 -I
"""Reject user-directory paths in unpacked binaries, ZIPs and APKs before distribution.

Usage: tools/check_build_paths.py <file-or-directory> [...]
AppImages must be checked before compression (their AppDir), not as opaque files.
"""
import pathlib
import re
import sys
import zipfile

PRIVATE_PATH = re.compile(r"^(?:/(?:Users|home)/|:[\\/]Users[\\/])[^\\/\0\r\n]+[\\/]", re.IGNORECASE)
# Public CI account names can occur in prebuilt third-party libraries.
PUBLIC_BUILD_USERS = {"runner", "runneradmin"}
PREFIXES = ("/Users/", "/home/", ":\\Users\\", ":/Users/", ":\\users\\", ":/users/")


def has_private_path(data):
    # Look for ASCII and UTF-16 paths without interpreting arbitrary binary data as text.
    for encoding in ("utf-8", "utf-16-le", "utf-16-be"):
        for prefix in PREFIXES:
            needle = prefix.encode(encoding)
            start = data.find(needle)
            while start >= 0:
                text = data[start:start + 2048].decode(encoding, errors="ignore")
                match = PRIVATE_PATH.match(text)
                if match:
                    user = match.group().replace("\\", "/").rstrip("/").rsplit("/", 1)[-1]
                    if user.lower() not in PUBLIC_BUILD_USERS:
                        return True
                start = data.find(needle, start + len(needle))
    return False


def check(path):
    failures = []
    if path.is_dir():
        for child in sorted(path.rglob("*")):
            if child.is_file() and not child.is_symlink():
                failures.extend(check(child))
    elif path.suffix.lower() in (".zip", ".apk"):
        with zipfile.ZipFile(path) as archive:
            for entry in archive.infolist():
                if not entry.is_dir() and has_private_path(archive.read(entry)):
                    failures.append(f"{path}:{entry.filename}")
    elif has_private_path(path.read_bytes()):
        failures.append(str(path))
    return failures


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    failures = []
    for name in sys.argv[1:]:
        path = pathlib.Path(name)
        if not path.exists():
            sys.exit(f"Path audit input is missing: {path}")
        failures.extend(check(path))
    if failures:
        for name in failures:
            print(f"Path audit FAILED: user-directory path embedded in {name}", file=sys.stderr)
        sys.exit(1)
    print("Path audit passed.")


if __name__ == "__main__":
    main()
