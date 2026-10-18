import functools
import hashlib
import http.server
import json
import pathlib
import sys
import tempfile
import threading
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import tgws_sidecar  # noqa: E402
import tgws_updater  # noqa: E402


def sha(data):
    return hashlib.sha256(data).hexdigest()


class Server:
    def __init__(self, root):
        class Quiet(http.server.SimpleHTTPRequestHandler):
            def log_message(self, *args):
                pass

        handler = functools.partial(Quiet, directory=str(root))
        self.httpd = http.server.ThreadingHTTPServer(('127.0.0.1', 0), handler)
        self.url = f'http://127.0.0.1:{self.httpd.server_address[1]}'
        threading.Thread(target=self.httpd.serve_forever, daemon=True).start()

    def close(self):
        self.httpd.shutdown()
        self.httpd.server_close()


class UpdaterTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        base = pathlib.Path(self.tmp.name)
        self.app = base / 'app'
        self.release = base / 'release'
        (self.app / 'tgws').mkdir(parents=True)
        self.release.mkdir()
        self.server = Server(self.release)
        self.manifest_url = f'{self.server.url}/manifest.json'

        (self.app / 'Telegram.exe').write_bytes(b'telegram-1')
        (self.app / 'tgws' / 'TgWsProxy.exe').write_bytes(b'proxy-1')
        self.write_version(build=1)

    def tearDown(self):
        self.server.close()
        self.tmp.cleanup()

    def write_version(self, build):
        (self.app / 'tgws' / 'version.json').write_text(json.dumps({
            'build': build,
            'update_url': self.manifest_url,
        }))

    def publish(self, build, telegram, proxy, corrupt=None):
        files = {}
        for name, data in (('Telegram.exe', telegram), ('tgws/TgWsProxy.exe', proxy)):
            asset = name.replace('/', '_')
            (self.release / asset).write_bytes(data)
            files[name] = {
                'sha256': sha(data),
                'size': len(data),
                'url': f'{self.server.url}/{asset}',
            }
        if corrupt:
            files[corrupt]['sha256'] = sha(b'something else')
        (self.release / 'manifest.json').write_text(json.dumps({
            'build': build,
            'tdesktop': 'v1',
            'proxy': 'v1',
            'files': files,
        }))

    def check(self):
        return tgws_updater.check_and_install(self.app, self.manifest_url)

    def test_nothing_to_do(self):
        self.publish(1, b'telegram-1', b'proxy-1')
        result = self.check()
        self.assertFalse(result.anything)

    def test_proxy_only_update(self):
        self.publish(2, b'telegram-1', b'proxy-2')
        result = self.check()
        self.assertTrue(result.self_updated)
        self.assertFalse(result.telegram_updated)
        self.assertEqual(
            (self.app / 'tgws' / 'TgWsProxy.exe.new').read_bytes(), b'proxy-2')
        # The running sidecar is not touched, Telegram swaps it after exit.
        self.assertEqual(
            (self.app / 'tgws' / 'TgWsProxy.exe').read_bytes(), b'proxy-1')
        version = json.loads((self.app / 'tgws' / 'version.json').read_text())
        self.assertEqual(version['build'], 2)
        self.assertEqual(version['update_url'], self.manifest_url)

    def test_telegram_update(self):
        self.publish(3, b'telegram-3', b'proxy-1')
        result = self.check()
        self.assertTrue(result.telegram_updated)
        self.assertFalse(result.self_updated)
        self.assertEqual((self.app / 'Telegram.exe').read_bytes(), b'telegram-3')
        self.assertEqual(list((self.app / 'tgws').glob('update-*')), [])

    def test_corrupt_download_installs_nothing(self):
        self.publish(4, b'telegram-4', b'proxy-4', corrupt='Telegram.exe')
        with self.assertRaises(tgws_updater.UpdateError):
            self.check()
        self.assertEqual((self.app / 'Telegram.exe').read_bytes(), b'telegram-1')
        self.assertFalse((self.app / 'tgws' / 'TgWsProxy.exe.new').exists())

    def test_older_manifest_is_ignored(self):
        self.write_version(build=10)
        self.publish(5, b'telegram-5', b'proxy-5')
        self.assertFalse(self.check().anything)

    def test_rejects_paths_outside_app(self):
        (self.release / 'manifest.json').write_text(json.dumps({
            'build': 9,
            'files': {'../evil.exe': {'sha256': sha(b'x'), 'url': 'http://x'}},
        }))
        with self.assertRaises(tgws_updater.UpdateError):
            self.check()

    def test_replace_running_keeps_old_copy_on_fallback(self):
        target = self.app / 'Telegram.exe'
        fresh = self.app / 'fresh'
        fresh.write_bytes(b'new')
        tgws_updater._replace_running(target, fresh)
        self.assertEqual(target.read_bytes(), b'new')

    def test_cleanup_old_files(self):
        (self.app / 'Telegram.exe.old').write_bytes(b'x')
        (self.app / 'tgws' / 'TgWsProxy.exe.old').write_bytes(b'x')
        tgws_updater.cleanup_old_files(self.app)
        self.assertEqual(list(self.app.rglob('*.old')), [])


class SidecarArgsTest(unittest.TestCase):
    def test_split_args(self):
        own, rest = tgws_sidecar.split_args([
            '--port', '1444',
            '--tgws-parent-pid', '42',
            '--secret', 'ab' * 16,
            '--tgws-app-dir', 'C:\\Telegram',
        ])
        self.assertEqual(own, {'parent_pid': '42', 'app_dir': 'C:\\Telegram'})
        self.assertEqual(rest, ['--port', '1444', '--secret', 'ab' * 16])


if __name__ == '__main__':
    unittest.main()
