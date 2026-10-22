# -*- mode: python ; coding: utf-8 -*-
# Builds tgws/TgWsProxy.exe: Flowseal's tg-ws-proxy (without its tray UI)
# plus the Client-tg updater. Set TGWS_PROXY_SRC to the tg-ws-proxy checkout.

import os

from PyInstaller.utils.hooks import collect_data_files

proxy_src = os.path.abspath(os.environ['TGWS_PROXY_SRC'])
here = os.path.dirname(os.path.abspath(SPEC))

a = Analysis(
    [os.path.join(here, 'tgws_sidecar.py')],
    pathex=[here, proxy_src],
    binaries=[],
    datas=collect_data_files('certifi'),
    hiddenimports=[
        'tgws_updater',
        'proxy.tg_ws_proxy',
        'utils.logging_setup',
        'httpx',
        'h2',
        'psutil',
        'cryptography.hazmat.primitives.ciphers',
        'cryptography.hazmat.primitives.ciphers.algorithms',
        'cryptography.hazmat.primitives.ciphers.modes',
        'cryptography.hazmat.backends.openssl',
    ],
    excludes=['tkinter', 'customtkinter', 'pystray', 'PIL'],
    noarchive=False,
)

pyz = PYZ(a.pure, a.zipped_data)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.zipfiles,
    a.datas,
    [],
    name='TgWsProxy',
    debug=False,
    strip=False,
    upx=False,
    runtime_tmpdir=None,
    console=False,
    icon=os.path.join(proxy_src, 'icon.ico'),
)
