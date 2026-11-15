#!/usr/bin/env python3
"""
tools/fetch_rcheevos.py -- the RetroAchievements client library (rcheevos),
from the official release, into a cache outside the repository.

eidos can sign the player in to RetroAchievements and report achievements
(docs/retroachievements.md).  The library is optional: without it the port
builds and runs, with the feature off.  CMake finds it in the cache this
script fills (EIDOS_RCHEEVOS_ROOT names another place).

One pinned release, verified against the SHA-256 below:

  rcheevos v12.5.0 (MIT)

Where things go.  Nothing is written into the repository.  The cache is,
in order: --cache, the EIDOS_RCHEEVOS_CACHE environment variable, then the
platform's per-user cache directory (eidos/rcheevos beside the mbedtls one).
The last line the script prints is the source folder, for
EIDOS_RCHEEVOS_ROOT.

usage: fetch_rcheevos.py [--cache DIR]
"""

import hashlib
import os
import sys
import tarfile
import urllib.request

VERSION = '12.5.0'
ARCHIVE = 'rcheevos-%s.tar.gz' % VERSION
URL = ('https://github.com/RetroAchievements/rcheevos/archive/refs/tags/'
       'v%s.tar.gz' % VERSION)
SHA256 = 'e6df83de4e18f0a19206e711e1e589624dcfcf6bf529c14f6dd2bb2d1ced983f'
FOLDER = 'rcheevos-%s' % VERSION


def cache_dir(argv):
    if '--cache' in argv:
        return argv[argv.index('--cache') + 1]
    env = os.environ.get('EIDOS_RCHEEVOS_CACHE')
    if env:
        return env
    if os.name == 'nt':
        base = os.environ.get('LOCALAPPDATA') or os.path.expanduser('~')
        return os.path.join(base, 'eidos', 'rcheevos')
    if sys.platform == 'darwin':
        return os.path.expanduser('~/Library/Caches/eidos/rcheevos')
    base = os.environ.get('XDG_CACHE_HOME') or os.path.expanduser('~/.cache')
    return os.path.join(base, 'eidos', 'rcheevos')


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def main(argv):
    cache = cache_dir(argv)
    downloads = os.path.join(cache, 'downloads')
    os.makedirs(downloads, exist_ok=True)
    archive = os.path.join(downloads, ARCHIVE)
    if not os.path.exists(archive) or sha256(archive) != SHA256:
        print('fetch_rcheevos: downloading', URL)
        tmp = archive + '.part'
        urllib.request.urlretrieve(URL, tmp)
        got = sha256(tmp)
        if got != SHA256:
            os.remove(tmp)
            sys.exit('fetch_rcheevos: %s has SHA-256 %s, expected %s' % (ARCHIVE, got, SHA256))
        os.replace(tmp, archive)
    root = os.path.join(cache, FOLDER)
    if not os.path.isdir(root):
        print('fetch_rcheevos: unpacking', ARCHIVE)
        with tarfile.open(archive) as t:
            for member in t.getmembers():
                if member.name.startswith('/') or '..' in member.name.split('/'):
                    sys.exit('fetch_rcheevos: unsafe path in the archive: %s' % member.name)
            t.extractall(cache)
    print(root.replace('\\', '/'))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
