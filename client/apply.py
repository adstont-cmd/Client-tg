#!/usr/bin/env python3
"""Inject Client-tg changes into a Telegram Desktop source checkout.

Usage: python client/apply.py <path-to-tdesktop-checkout>

Instead of a diff patch (which breaks on every unrelated upstream change),
we copy our own source files into the tree and insert a few lines next to
stable anchors. If an anchor disappears upstream the script fails loudly, so
the automatic build stops and the previous release stays in place.
"""

import pathlib
import re
import shutil
import sys

HERE = pathlib.Path(__file__).resolve().parent
MARKER = 'Client-tg'

SOURCES = [
    'tgws/tgws_controller.h',
    'tgws/tgws_controller.cpp',
    'modengine/event_bus.h',
    'modengine/event_bus.cpp',
    'modengine/mod_config.h',
    'modengine/mod_config.cpp',
    'modengine/mod_engine.h',
    'modengine/mod_engine.cpp',
    'modengine/mod_hooks.h',
    'modengine/mod_hooks.cpp',
    'modengine/mod_host_api.h',
    'modengine/mod_host_api.cpp',
    'modengine/mod_loader.h',
    'modengine/mod_loader.cpp',
    'modengine/mod_metadata.h',
    'modengine/mod_metadata.cpp',
    'modengine/mod_registry.h',
    'modengine/mod_registry.cpp',
    'modengine/mod_version.h',
    'modengine/mod_version.cpp',
    'modengine/mods_box.h',
    'modengine/mods_box.cpp',
]

# The public mod API (modapi/include/tgmod), copied as SourceFiles/tgmod.
API_HEADERS = [
    'tgmod.h',
    'tgmod.hpp',
]


class ApplyError(Exception):
    pass


def insert_once(path, pattern, addition, after=True):
    """Insert `addition` after (or before) the single line matching `pattern`."""
    text = path.read_text(encoding='utf-8')
    if addition.strip() in text:
        return False
    regex = re.compile(pattern, re.MULTILINE)
    matches = list(regex.finditer(text))
    if len(matches) != 1:
        raise ApplyError(
            f'{path}: expected exactly one match for {pattern!r}, '
            f'found {len(matches)}. Upstream code changed, update client/apply.py.')
    match = matches[0]
    if after:
        end = text.index('\n', match.end()) + 1 if '\n' in text[match.end():] else len(text)
        text = text[:end] + addition + text[end:]
    else:
        start = text.rfind('\n', 0, match.start()) + 1
        text = text[:start] + addition + text[start:]
    path.write_text(text, encoding='utf-8', newline='\n')
    return True


def apply(root):
    telegram = root / 'Telegram'
    sources = telegram / 'SourceFiles'
    if not (sources / 'dialogs' / 'dialogs_widget.cpp').exists():
        raise ApplyError(f'{root} does not look like a tdesktop checkout')

    for relative in SOURCES:
        target = sources / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(HERE / 'src' / relative, target)

    api = HERE.parent / 'modapi' / 'include' / 'tgmod'
    (sources / 'tgmod').mkdir(parents=True, exist_ok=True)
    for name in API_HEADERS:
        shutil.copyfile(api / name, sources / 'tgmod' / name)

    cmake = telegram / 'CMakeLists.txt'
    text = cmake.read_text(encoding='utf-8')
    if MARKER not in text:
        files = '\n'.join(f'    ${{src_loc}}/{name}' for name in SOURCES)
        text += (
            f'\n# {MARKER}: built-in TG WS Proxy and the mod engine.\n'
            f'target_sources(Telegram PRIVATE\n{files}\n)\n'
        )
        cmake.write_text(text, encoding='utf-8', newline='\n')

    application = sources / 'core' / 'application.cpp'
    insert_once(
        application,
        r'^#include "core/application\.h"$',
        '#include "tgws/tgws_controller.h" // Client-tg\n')
    insert_once(
        application,
        r'^\trefreshGlobalProxy\(\); // Depends on app settings being read\.$',
        '\tTgWs::Controller::Start(); // Client-tg\n',
        after=False)

    insert_once(
        application,
        r'^#include "core/application\.h"$',
        '#include "modengine/mod_hooks.h" // Client-tg\n')
    insert_once(
        application,
        r'^\trefreshGlobalProxy\(\); // Depends on app settings being read\.$',
        '\tModEngine::Hooks::Start(); // Client-tg\n',
        after=False)

    dialogs = sources / 'dialogs' / 'dialogs_widget.cpp'
    insert_once(
        dialogs,
        r'^#include "dialogs/dialogs_widget\.h"$',
        '#include "tgws/tgws_controller.h" // Client-tg\n')
    insert_once(
        dialogs,
        r'^\tsetupMainMenuToggle\(\);$',
        '\tif (_layout != Layout::Child) { // Client-tg\n'
        '\t\tTgWs::SetupDialogsButton(this);\n'
        '\t}\n')

    apply_mod_hooks(sources)


def apply_mod_hooks(sources):
    """One call into ModEngine::Hooks per place, see mod_hooks.h."""
    include = '#include "modengine/mod_hooks.h" // Client-tg\n'

    domain = sources / 'main' / 'main_domain.cpp'
    insert_once(domain, r'^#include "main/main_domain\.h"$', include)
    insert_once(
        domain,
        r'^void Domain::watchSession\(not_null<Account\*> account\) \{$',
        '\tModEngine::Hooks::WatchAccount(account); // Client-tg\n')

    apiwrap = sources / 'apiwrap.cpp'
    insert_once(apiwrap, r'^#include "apiwrap\.h"$', include)
    insert_once(
        apiwrap,
        r'^void ApiWrap::sendMessage\(\n'
        r'\t\tMessageToSend &&message,\n'
        r'\t\tstd::optional<MsgId> localMessageId\) \{$',
        '\tif (ModEngine::Hooks::BeforeSend(_session, message)) { // Client-tg\n'
        '\t\treturn;\n'
        '\t}\n')

    controller = sources / 'window' / 'window_session_controller.cpp'
    insert_once(
        controller,
        r'^#include "window/window_session_controller\.h"$',
        include)
    insert_once(
        controller,
        r'^\tinit\(\);$',
        '\tModEngine::Hooks::SessionControllerCreated(this); // Client-tg\n')

    menu = sources / 'window' / 'window_main_menu.cpp'
    insert_once(menu, r'^#include "window/window_main_menu\.h"$', include)
    insert_once(
        menu,
        r'^\taddAction\(\n\t\ttr::lng_menu_settings\(\),$',
        '\tModEngine::Hooks::FillMainMenu(_menu, controller); // Client-tg\n',
        after=False)

    history = sources / 'history' / 'history_inner_widget.cpp'
    insert_once(
        history,
        r'^#include "history/history_inner_widget\.h"$',
        include)
    insert_once(
        history,
        r'^\tif \(leaderOrSelf && !_menu->empty\(\)\) \{$',
        '\tModEngine::Hooks::FillMessageMenu( // Client-tg\n'
        '\t\t_controller,\n'
        '\t\t_menu.get(),\n'
        '\t\tleaderOrSelf);\n',
        after=False)


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    try:
        apply(pathlib.Path(sys.argv[1]).resolve())
    except ApplyError as e:
        print(f'error: {e}', file=sys.stderr)
        return 1
    print('Client-tg changes applied.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
