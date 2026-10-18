"""Client-tg sidecar: TG WS Proxy by Flowseal + automatic updates.

Telegram (tgws_controller.cpp) starts this program as
`tgws/TgWsProxy.exe` with the usual tg-ws-proxy arguments plus:

    --tgws-parent-pid PID   exit when Telegram is gone
    --tgws-app-dir DIR      folder with Telegram.exe (for updates)
    --tgws-data-dir DIR     writable folder for logs

Exit codes (keep in sync with tgws_controller.cpp):
    75  a new TgWsProxy.exe was saved as TgWsProxy.exe.new, restart me
    76  Telegram.exe was updated too, offer the user to restart Telegram
"""

import logging
import os
import random
import sys
import threading
import time

EXIT_SIDECAR_UPDATED = 75
EXIT_TELEGRAM_UPDATED = 76

FIRST_CHECK_DELAY = 60
CHECK_INTERVAL = 3 * 60 * 60

OWN_ARGS = ('--tgws-parent-pid', '--tgws-app-dir', '--tgws-data-dir')

log = logging.getLogger('tgws.sidecar')


def split_args(argv):
    own, rest = {}, []
    i = 0
    while i < len(argv):
        arg = argv[i]
        if arg in OWN_ARGS and i + 1 < len(argv):
            own[arg[len('--tgws-'):].replace('-', '_')] = argv[i + 1]
            i += 2
            continue
        rest.append(arg)
        i += 1
    return own, rest


def pid_alive(pid):
    try:
        import psutil
        return psutil.pid_exists(pid)
    except ImportError:
        try:
            os.kill(pid, 0)
        except OSError:
            return False
        return True


def watch_parent(pid):
    while pid_alive(pid):
        time.sleep(3)
    log.info('Telegram exited, stopping')
    os._exit(0)


def update_loop(app_dir, sleep=time.sleep):
    import tgws_updater
    tgws_updater.cleanup_old_files(app_dir)
    sleep(FIRST_CHECK_DELAY)
    while True:
        url = tgws_updater.read_local_version(app_dir).get('update_url')
        if not url:
            log.info('No update_url in version.json, updates disabled')
            return
        try:
            result = tgws_updater.check_and_install(app_dir, url)
        except Exception as e:  # network errors, GitHub hiccups, ...
            log.warning('Update check failed: %s', e)
        else:
            if result.telegram_updated:
                log.info('Telegram updated, asking for restart')
                os._exit(EXIT_TELEGRAM_UPDATED)
            if result.self_updated:
                log.info('Proxy updated, restarting')
                os._exit(EXIT_SIDECAR_UPDATED)
        sleep(CHECK_INTERVAL + random.randint(0, 600))


def main():
    # A windowed (no console) build has no stdio, the proxy logs to it.
    if sys.stdout is None:
        sys.stdout = open(os.devnull, 'w')
    if sys.stderr is None:
        sys.stderr = open(os.devnull, 'w')

    own, rest = split_args(sys.argv[1:])

    if 'parent_pid' in own:
        threading.Thread(
            target=watch_parent,
            args=(int(own['parent_pid']),),
            daemon=True).start()
    if 'app_dir' in own:
        threading.Thread(
            target=update_loop,
            args=(own['app_dir'],),
            daemon=True).start()

    sys.argv = [sys.argv[0]] + rest
    from proxy.tg_ws_proxy import main as proxy_main
    proxy_main()


if __name__ == '__main__':
    main()
