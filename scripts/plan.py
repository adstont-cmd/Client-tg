#!/usr/bin/env python3
"""Decide what the build workflow has to rebuild.

Compares the latest upstream releases (tdesktop, tg-ws-proxy) and the hash
of our own sources with what the latest Client-tg release was built from
(its manifest.json). Writes GitHub Actions outputs:

    tdesktop_tag, proxy_tag       upstream release tags to build from
    telegram_key, proxy_key       identity of each component
    build_telegram, build_proxy   'true' when that component must be rebuilt
    release                       'true' when a new release must be published
    previous_tag                  latest Client-tg release (to reuse files from)
"""

import hashlib
import json
import os
import pathlib
import sys
import urllib.error
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[1]
TDESKTOP = 'telegramdesktop/tdesktop'
PROXY = 'Flowseal/tg-ws-proxy'


def api(path):
    request = urllib.request.Request(
        f'https://api.github.com/{path}',
        headers={
            'Accept': 'application/vnd.github+json',
            'User-Agent': 'Client-tg-plan',
            **({'Authorization': f'Bearer {os.environ["GITHUB_TOKEN"]}'}
               if os.environ.get('GITHUB_TOKEN') else {}),
        })
    with urllib.request.urlopen(request, timeout=60) as response:
        return json.loads(response.read().decode('utf-8'))


def download(url):
    request = urllib.request.Request(url, headers={'User-Agent': 'Client-tg-plan'})
    with urllib.request.urlopen(request, timeout=60) as response:
        return response.read()


def tree_hash(*folders):
    digest = hashlib.sha256()
    for folder in folders:
        for path in sorted((ROOT / folder).rglob('*')):
            if path.is_file() and '__pycache__' not in path.parts:
                digest.update(str(path.relative_to(ROOT).as_posix()).encode())
                digest.update(b'\0')
                digest.update(path.read_bytes())
    return digest.hexdigest()[:16]


def previous_release(repo):
    try:
        release = api(f'repos/{repo}/releases/latest')
    except urllib.error.HTTPError as e:
        if e.code == 404:
            return None, {}
        raise
    for asset in release.get('assets', []):
        if asset['name'] == 'manifest.json':
            return release['tag_name'], json.loads(
                download(asset['browser_download_url']))
    return release['tag_name'], {}


def main():
    tdesktop_tag = os.environ.get('TDESKTOP_TAG') or api(
        f'repos/{TDESKTOP}/releases/latest')['tag_name']
    proxy_tag = os.environ.get('PROXY_TAG') or api(
        f'repos/{PROXY}/releases/latest')['tag_name']

    telegram_key = f'{tdesktop_tag}-{tree_hash("client")}'
    proxy_key = f'{proxy_tag}-{tree_hash("sidecar")}'

    previous_tag, manifest = previous_release(os.environ['GITHUB_REPOSITORY'])
    force = os.environ.get('FORCE') == 'true'
    build_telegram = force or manifest.get('telegram_key') != telegram_key
    build_proxy = force or manifest.get('proxy_key') != proxy_key

    outputs = {
        'tdesktop_tag': tdesktop_tag,
        'proxy_tag': proxy_tag,
        'telegram_key': telegram_key,
        'proxy_key': proxy_key,
        'build_telegram': str(build_telegram).lower(),
        'build_proxy': str(build_proxy).lower(),
        'release': str(build_telegram or build_proxy).lower(),
        'previous_tag': previous_tag or '',
    }
    for key, value in outputs.items():
        print(f'{key}={value}')
    if os.environ.get('GITHUB_OUTPUT'):
        with open(os.environ['GITHUB_OUTPUT'], 'a', encoding='utf-8') as f:
            for key, value in outputs.items():
                f.write(f'{key}={value}\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
