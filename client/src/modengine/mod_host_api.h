/*
Client-tg: mod engine.

This file is injected into the Telegram Desktop source tree at build time
by client/apply.py. It is distributed under the same license as
Telegram Desktop (GPLv3 with the OpenSSL exception).
*/
#pragma once

#include "tgmod/tgmod.h"

namespace ModEngine {

// The function table handed to every mod in tgmod_main.
[[nodiscard]] const tgmod_host *HostApi();

} // namespace ModEngine
