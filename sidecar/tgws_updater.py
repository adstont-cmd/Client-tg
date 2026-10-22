"""Self-updater for the Client-tg package (Telegram.exe + TgWsProxy.exe).

The build pipeline publishes a `manifest.json` with every release:

    {
      "build": 42,
      "tdesktop": "v7.3.1",
      "proxy": "v1.11.2",
      "files": {
        "Telegram.exe": {"sha256": "...", "size": 1, "url": "https://..."},
        "tgws/TgWsProxy.exe": {"sha256": "...", "size": 1, "url": "https://..."}
      }
    }

Every file whose hash differs from the installed one is downloaded,
verified and put in place:

* Telegram.exe and other files: the running file is renamed to `*.old`
  (Windows allows renaming a running executable) and the new one takes its
  name, so the next launch of Telegram uses it.
* The sidecar itself (tgws/TgWsProxy.exe) is written as `*.new`; Telegram
  swaps it in after the sidecar exits (see tgws_controller.cpp).
"""

import hashlib
import json
import logging
import os
import pathlib
import ssl
import tempfile
import urllib.request

log = logging.getLogger('tgws.updater')

SELF_PATH = 'tgws/TgWsProxy.exe'
VERSION_FILE = 'tgws/version.json'
USER_AGENT = 'Client-tg-updater'


class UpdateError(Exception):
    pass


class Result:
    def __init__(self):
        self.self_updated = False
        self.telegram_updated = False
        self.installed = []

    @property
    def anything(self):
        return bool(self.installed)


def _ssl_context():
    try:
        import certifi
        return ssl.create_default_context(cafile=certifi.where())
    except Exception:
        return ssl.create_default_context()


def _open(url, timeout=60):
    request = urllib.request.Request(url, headers={'User-Agent': USER_AGENT})
    if url.startswith('https://'):
        return urllib.request.urlopen(request, timeout=timeout, context=_ssl_context())
    return urllib.request.urlopen(request, timeout=timeout)


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def read_local_version(app_dir):
    try:
        with open(pathlib.Path(app_dir) / VERSION_FILE, encoding='utf-8') as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def fetch_manifest(url):
    with _open(url) as response:
        data = json.loads(response.read().decode('utf-8'))
    if not isinstance(data, dict) or not isinstance(data.get('files'), dict):
        raise UpdateError('malformed manifest')
    return data


def _safe_relative(name):
    path = pathlib.PurePosixPath(name)
    if path.is_absolute() or '..' in path.parts or not path.parts:
        raise UpdateError(f'bad file name in manifest: {name!r}')
    return pathlib.Path(*path.parts)


def _download(url, expected_sha, expected_size, target):
    digest = hashlib.sha256()
    size = 0
    with _open(url, timeout=120) as response, open(target, 'wb') as out:
        for chunk in iter(lambda: response.read(1024 * 1024), b''):
            digest.update(chunk)
            size += len(chunk)
            out.write(chunk)
    if expected_size is not None and size != expected_size:
        raise UpdateError(f'{url}: size {size} != {expected_size}')
    if digest.hexdigest() != expected_sha.lower():
        raise UpdateError(f'{url}: sha256 mismatch')


def _replace_running(path, fresh):
    """Put `fresh` in place of `path`, even if `path` is a running exe."""
    old = path.with_name(path.name + '.old')
    if path.exists():
        try:
            os.replace(fresh, path)
            return
        except PermissionError:
            pass
        if old.exists():
            old.unlink()
        os.replace(path, old)
    try:
        os.replace(fresh, path)
    except OSError:
        if old.exists() and not path.exists():
            os.replace(old, path)
        raise


def cleanup_old_files(app_dir):
    """Remove `*.old` leftovers of previous updates (no longer running)."""
    for folder in (pathlib.Path(app_dir), pathlib.Path(app_dir) / 'tgws'):
        if not folder.is_dir():
            continue
        for item in folder.glob('*.old'):
            try:
                item.unlink()
            except OSError:
                pass


def check_and_install(app_dir, manifest_url):
    app_dir = pathlib.Path(app_dir)
    manifest = fetch_manifest(manifest_url)
    local = read_local_version(app_dir)
    result = Result()
    if int(manifest.get('build', 0)) < int(local.get('build', 0)):
        log.info('Manifest build %s is older than installed %s, skipping',
                 manifest.get('build'), local.get('build'))
        return result

    pending = []
    for name, info in sorted(manifest['files'].items()):
        relative = _safe_relative(name)
        target = app_dir / relative
        if target.exists() and sha256_of(target) == info['sha256'].lower():
            continue
        pending.append((name, relative, info))
    if not pending:
        return result

    staging = pathlib.Path(tempfile.mkdtemp(prefix='update-', dir=app_dir / 'tgws'))
    try:
        downloaded = []
        for name, relative, info in pending:
            log.info('Downloading %s (%s)', name, info.get('size'))
            temp = staging / relative.name
            _download(info['url'], info['sha256'], info.get('size'), temp)
            downloaded.append((name, relative, temp))

        # Install everything only after all downloads are verified.
        for name, relative, temp in downloaded:
            target = app_dir / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            if name == SELF_PATH:
                os.replace(temp, target.with_name(target.name + '.new'))
                result.self_updated = True
            else:
                _replace_running(target, temp)
                if relative.parent == pathlib.Path('.'):
                    result.telegram_updated = True
            result.installed.append(name)
            log.info('Installed %s', name)

        version = {k: v for k, v in manifest.items() if k != 'files'}
        version['update_url'] = local.get('update_url', manifest_url)
        version_path = app_dir / VERSION_FILE
        version_path.parent.mkdir(parents=True, exist_ok=True)
        version_path.write_text(json.dumps(version, indent=2), encoding='utf-8')
    finally:
        for item in staging.glob('*'):
            try:
                item.unlink()
            except OSError:
                pass
        try:
            staging.rmdir()
        except OSError:
            pass
    return result
