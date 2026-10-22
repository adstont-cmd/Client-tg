/*
Client-tg: mod engine.

This file is injected into the Telegram Desktop source tree at build time
by client/apply.py. It is distributed under the same license as
Telegram Desktop (GPLv3 with the OpenSSL exception).
*/
#pragma once

#include "modengine/mod_metadata.h"
#include "tgmod/tgmod.h"

#include <QtCore/QLibrary>

#include <map>

namespace ModEngine {

class EventBus;
class JsonConfig;
struct ModContainer;

} // namespace ModEngine

// The opaque handle mods get, see tgmod.h.
struct tgmod_mod {
	ModEngine::ModContainer *container = nullptr;
};

namespace ModEngine {

enum class ModState {
	Discovered,
	Disabled, // Turned off by the user.
	Failed, // See errors.
	Constructed, // tgmod_main returned.
	CommonSetup,
	ClientSetup,
	Loaded, // LOAD_COMPLETE delivered.
};

[[nodiscard]] QString ModStateName(ModState state);

struct ModContainer {
	ModContainer();
	~ModContainer();

	[[nodiscard]] QString id() const; // Folder name if mod.json is broken.
	[[nodiscard]] bool active() const;

	QString folder; // Absolute, with a trailing slash.
	std::optional<ModMetadata> metadata;
	ModState state = ModState::Discovered;
	QStringList errors;
	QStringList log; // Last lines, for the mods screen.
	int order = -1; // Index in the load order.

	std::unique_ptr<QLibrary> library;
	std::unique_ptr<JsonConfig> config;
	tgmod_mod handle;
};

// Finds mods, checks them, sorts them and drives them through the
// lifecycle. Mods are never unloaded: like Forge, changes need a restart.
//
// Phases:
//   discover()  read mods/*/mod.json
//   resolve()   ids, versions, dependencies, load order
//   construct() load the libraries and call tgmod_main
//   dispatch()  COMMON_SETUP, CLIENT_SETUP, LOAD_COMPLETE, SHUTDOWN
class ModLoader final {
public:
	ModLoader(QString modsDir, Version clientVersion, Version telegramVersion);

	void discover(const QStringList &disabledIds);
	void resolve();
	void construct(const tgmod_host *host);
	void dispatch(EventBus &bus, tgmod_event_type type);

	[[nodiscard]] const std::vector<std::unique_ptr<ModContainer>> &mods() const;
	[[nodiscard]] ModContainer *findActive(const QString &id) const;

	// Marks the mod failed and fails every mod that requires it.
	void fail(not_null<ModContainer*> mod, const QString &error);

private:
	[[nodiscard]] std::vector<ModContainer*> candidates() const;
	[[nodiscard]] bool checkDependencies(
		not_null<ModContainer*> mod,
		const std::map<QString, ModContainer*> &present);
	void sort();
	[[nodiscard]] bool load(
		not_null<ModContainer*> mod,
		const tgmod_host *host);

	QString _modsDir;
	Version _clientVersion;
	Version _telegramVersion;
	std::vector<std::unique_ptr<ModContainer>> _mods; // In load order.

};

} // namespace ModEngine
