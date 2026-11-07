#!/usr/bin/env python3
"""Install the pinned official open PS3DEV release in WSL, without shell-profile edits."""
import argparse
import hashlib
from pathlib import Path
import tarfile
import urllib.request

URL = 'https://github.com/ps3dev/ps3dev/releases/download/nightly-2026-10-12/ps3dev-linux-X64.tar.gz'
SHA = '76b1b3bc9ac260a4ece86da520225da7f2f4e334174e37b850093ff925eeb900'
SIZE = 298298575


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--prefix', type=Path, default=Path('/opt/wiliwili-ps3'))
    args = ap.parse_args()
    prefix = args.prefix.resolve()
    sdk = prefix / 'ps3dev'
    if sdk.exists():
        info = sdk / 'build.txt'
        if not info.is_file() or '8672a842adfe5290cceea5e2e518361a5d1ba8f0' not in info.read_text():
            raise SystemExit('Existing toolchain does not match the pin; refusing to overwrite it')
        print('Pinned toolchain already present:', sdk)
        return
    archive = prefix / 'downloads/ps3dev-linux-X64.tar.gz'
    archive.parent.mkdir(parents=True, exist_ok=True)
    if not archive.exists():
        print('Downloading official PS3DEV release (284.5 MiB) into', archive, flush=True)
        with urllib.request.urlopen(URL, timeout=60) as source, archive.open('xb') as out:
            while block := source.read(1024 * 1024):
                out.write(block)
    digest = hashlib.file_digest(archive.open('rb'), 'sha256').hexdigest()
    if archive.stat().st_size != SIZE or digest != SHA:
        raise SystemExit('Archive verification failed; nothing extracted')
    print('SHA-256 verified; extracting under', prefix, flush=True)
    with tarfile.open(archive, 'r:gz') as tf:
        for member in tf.getmembers():
            path = prefix / member.name
            if not path.resolve().is_relative_to(prefix) or not (member.name == 'ps3dev' or member.name.startswith('ps3dev/')):
                raise SystemExit('Unexpected archive path: ' + member.name)
        tf.extractall(prefix, filter='data')
    print('Installed:', sdk)


if __name__ == '__main__':
    main()
