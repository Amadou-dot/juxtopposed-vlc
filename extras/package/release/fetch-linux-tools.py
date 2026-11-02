#!/usr/bin/env python3
"""Fetch checksum-pinned deployment tools; never execute an unverified download."""
import argparse
import hashlib
from pathlib import Path
import time
import urllib.request

# Digests published with these GitHub release assets. continuous URLs may move;
# an upstream replacement must be reviewed and its pin updated before use.
TOOLS = {
    # Continuous build published 2026-11-01, source commit 07333c6.
    'linuxdeploy': 'df908b19f1ed1cdafa79508d415aa12d6c56ce4d56d4149aa8c09286718706e5',
    'linuxdeploy-plugin-qt': 'cfc1055b2b9dbc08412b579f20990b7b41a17b61beaa5847dc9477c96c9e9617',
}


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def fetch(directory):
    directory.mkdir(parents=True, exist_ok=True)
    for tool, expected in TOOLS.items():
        path = directory / f'{tool}-x86_64.AppImage'
        if not path.exists():
            partial = path.with_suffix('.partial')
            try:
                url = f'https://github.com/linuxdeploy/{tool}/releases/download/continuous/{path.name}'
                request = urllib.request.Request(url, headers={'User-Agent': 'VLC-release-packaging'})
                for attempt in range(3):
                    try:
                        with urllib.request.urlopen(request, timeout=60) as source, partial.open('wb') as target:
                            for block in iter(lambda: source.read(1024 * 1024), b''):
                                target.write(block)
                        break
                    except OSError:
                        if attempt == 2:
                            raise
                        time.sleep(2 ** attempt)
                if digest(partial) != expected:
                    raise ValueError(f'Deployment tool checksum mismatch: {tool}')
                partial.replace(path)
            finally:
                partial.unlink(missing_ok=True)
        if digest(path) != expected:
            raise ValueError(f'Cached deployment tool checksum mismatch: {path}')
        path.chmod(0o755)
        print(f'{expected}  {path}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    fetch(args.directory)
