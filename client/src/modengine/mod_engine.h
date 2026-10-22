/*
Client-tg: mod engine.

This file is injected into the Telegram Desktop source tree at build time
by client/apply.py. It is distributed under the same license as
Telegram Desktop (GPLv3 with the OpenSSL exception).
*/
#pragma once

#include "modengine/event_bus.h"
#include "modengine/mod_config.h"
#include "modengine/mod_loader.h"
#include "modengine/mod_registry.h"

namespace Main {
class Session;
} // namespace Main

namespace ModEngine {

// Version of the engine, mods depend on it as "client" in mod.json.
inline constexpr auto kEngineVersion = "0.1.0";

// Owns everything: the loader, the event bus, the registries and the
// engine settings (mods/engine.json). Lives until the process exits.
class Engine final {
public:
	[[nodiscard]] static Engine &Instance();

	// Discovers mods, constructs them and runs COMMON_SETUP. CLIENT_SETUP
	// and LOAD_COMPLETE follow once the event loop runs (windows exist).
	void start();
	void shutdown();

	[[nodiscard]] bool started() const;
	[[nodiscard]] bool safeMode() const;
	[[nodiscard]] QString modsDir() const;
	[[nodiscard]] QString commandPrefix();
	[[nodiscard]] QString clientVersionText() const;

	[[nodiscard]] EventBus &bus();
	[[nodiscard]] ModLoader &loader();
	[[nodiscard]] Registry &registry();
	[[nodiscard]] JsonConfig &settings();

	// Takes effect after a restart, like in Forge.
	[[nodiscard]] bool modEnabled(const QString &id);
	void setModEnabled(const QString &id, bool enabled);

	void log(
		not_null<ModContainer*> mod,
		tgmod_log_level level,
		const QString &text);
	// Fails the mod (and the mods that require it) and drops whatever it
	// registered, the client keeps running without it.
	void failMod(not_null<ModContainer*> mod, const QString &error);

	// Accounts known to mods by Main::Session::uniqueId().
	void addSession(not_null<Main::Session*> session);
	[[nodiscard]] Main::Session *session(uint64 sessionId) const;

private:
	Engine();

	void setupCommands();
	[[nodiscard]] bool runCommand(EventData &event);
	void finishLoading();
	void cleanupFailed();

	const QString _modsDir;
	JsonConfig _settings;
	EventBus _bus;
	Registry _registry;
	std::unique_ptr<ModLoader> _loader;
	std::map<uint64, Main::Session*> _sessions;
	bool _started = false;
	bool _safeMode = false;
	bool _shutdown = false;

};

} // namespace ModEngine
