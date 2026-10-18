#!/usr/bin/env python3
"""Assemble the release files from Telegram.exe and TgWsProxy.exe.

Usage: make_release.py <input-dir> <output-dir>

<input-dir> must contain Telegram.exe and TgWsProxy.exe. Settings come from
the environment: BUILD, TAG, TDESKTOP_TAG, PROXY_TAG, TELEGRAM_KEY,
PROXY_KEY, GITHUB_REPOSITORY.

Produces in <output-dir>:
    Telegram.exe, TgWsProxy.exe     single files the in-app updater fetches
    Client-tg-windows-x64.zip       portable package for the first install
    manifest.json                   what the in-app updater reads
"""

import hashlib
import json
import os
import pathlib
import shutil
import sys
import zipfile

# Release asset name -> path inside the installed folder.
FILES = {
    'Telegram.exe': 'Telegram.exe',
    'TgWsProxy.exe': 'tgws/TgWsProxy.exe',
}
ZIP_NAME = 'Client-tg-windows-x64.zip'


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def main():
    source, output = map(pathlib.Path, sys.argv[1:3])
    output.mkdir(parents=True, exist_ok=True)
    env = os.environ
    repo = env['GITHUB_REPOSITORY']
    tag = env['TAG']

    files = {}
    for asset, installed in FILES.items():
        path = source / asset
        shutil.copyfile(path, output / asset)
        files[installed] = {
            'sha256': sha256_of(path),
            'size': path.stat().st_size,
            'url': f'https://github.com/{repo}/releases/download/{tag}/{asset}',
        }

    info = {
        'build': int(env['BUILD']),
        'tag': tag,
        'tdesktop': env['TDESKTOP_TAG'],
        'proxy': env['PROXY_TAG'],
        'telegram_key': env['TELEGRAM_KEY'],
        'proxy_key': env['PROXY_KEY'],
    }
    manifest = dict(info, files=files)
    (output / 'manifest.json').write_text(
        json.dumps(manifest, indent=2), encoding='utf-8')

    version = dict(
        info,
        update_url=f'https://github.com/{repo}/releases/latest/download/manifest.json')
    with zipfile.ZipFile(output / ZIP_NAME, 'w', zipfile.ZIP_DEFLATED) as z:
        for asset, installed in FILES.items():
            z.write(source / asset, f'Client-tg/{installed}')
        z.writestr('Client-tg/tgws/version.json', json.dumps(version, indent=2))
        # Keeps Telegram data next to Telegram.exe, separate from an
        # official Telegram Desktop installed on the same computer.
        z.writestr('Client-tg/TelegramForcePortable/', '')
    print(json.dumps(manifest, indent=2))
    return 0


if __name__ == '__main__':
    sys.exit(main())
