/*
Client-tg: mod engine.

This file is injected into the Telegram Desktop source tree at build time
by client/apply.py. It is distributed under the same license as
Telegram Desktop (GPLv3 with the OpenSSL exception).
*/
#pragma once

namespace Ui {
class GenericBox;
} // namespace Ui

namespace ModEngine {

// The list of mods: state, errors, on/off switches. Like the Forge mods
// screen, changes apply after a restart.
void ModsBox(not_null<Ui::GenericBox*> box);

} // namespace ModEngine
