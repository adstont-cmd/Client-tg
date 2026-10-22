/*
Client-tg: mod engine.

This file is injected into the Telegram Desktop source tree at build time
by client/apply.py. It is distributed under the same license as
Telegram Desktop (GPLv3 with the OpenSSL exception).
*/
#include "modengine/mod_loader.h"

#include "modengine/event_bus.h"
#include "modengine/mod_config.h"
#include "logs.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>

namespace ModEngine {
namespace {

constexpr auto kMetadataFile = "mod.json";

[[nodiscard]] ModState StateAfter(tgmod_event_type type) {
	switch (type) {
	case TGMOD_EVENT_COMMON_SETUP: return ModState::CommonSetup;
	case TGMOD_EVENT_CLIENT_SETUP: return ModState::ClientSetup;
	case TGMOD_EVENT_LOAD_COMPLETE: return ModState::Loaded;
	default: return ModState::Loaded;
	}
}

} // namespace

QString ModStateName(ModState state) {
	switch (state) {
	case ModState::Discovered: return u"discovered"_q;
	case ModState::Disabled: return u"disabled"_q;
	case ModState::Failed: return u"failed"_q;
	case ModState::Constructed: return u"constructed"_q;
	case ModState::CommonSetup: return u"common setup"_q;
	case ModState::ClientSetup: return u"client setup"_q;
	case ModState::Loaded: return u"loaded"_q;
	}
	Unexpected("State in ModStateName.");
}

ModContainer::ModContainer() {
	handle.container = this;
}

// QLibrary does not unload in its destructor, mod code stays mapped until
// the process exits: Qt objects or static destructors may still use it.
ModContainer::~ModContainer() = default;

QString ModContainer::id() const {
	return metadata
		? metadata->id
		: QDir(folder).dirName();
}

bool ModContainer::active() const {
	return (state != ModState::Discovered)
		&& (state != ModState::Disabled)
		&& (state != ModState::Failed);
}

ModLoader::ModLoader(
	QString modsDir,
	Version clientVersion,
	Version telegramVersion)
: _modsDir(std::move(modsDir))
, _clientVersion(std::move(clientVersion))
, _telegramVersion(std::move(telegramVersion)) {
}

void ModLoader::discover(const QStringList &disabledIds) {
	QDir().mkpath(_modsDir);
	const auto folders = QDir(_modsDir).entryInfoList(
		QDir::Dirs | QDir::NoDotAndDotDot,
		QDir::Name);
	for (const auto &info : folders) {
		const auto path = info.absoluteFilePath() + '/';
		auto file = QFile(path + kMetadataFile);
		if (!file.exists()) {
			continue; // "config" and other helper folders.
		}
		auto mod = std::make_unique<ModContainer>();
		mod->folder = path;
		if (!file.open(QIODevice::ReadOnly)) {
			mod->state = ModState::Failed;
			mod->errors.push_back(u"Could not read mod.json."_q);
		} else {
			auto parsed = ParseModMetadata(file.readAll());
			mod->metadata = std::move(parsed.metadata);
			mod->errors = std::move(parsed.errors);
			if (!mod->metadata) {
				mod->state = ModState::Failed;
			} else if (disabledIds.contains(mod->metadata->id)) {
				mod->state = ModState::Disabled;
			}
		}
		LOG(("Mods: found \"%1\" in %2, %3."
			).arg(mod->id(), path, ModStateName(mod->state)));
		_mods.push_back(std::move(mod));
	}
}

std::vector<ModContainer*> ModLoader::candidates() const {
	auto result = std::vector<ModContainer*>();
	for (const auto &mod : _mods) {
		if (mod->state == ModState::Discovered) {
			result.push_back(mod.get());
		}
	}
	return result;
}

bool ModLoader::checkDependencies(
		not_null<ModContainer*> mod,
		const std::map<QString, ModContainer*> &present) {
	const auto versionOf = [&](const QString &id) -> std::optional<Version> {
		if (id == QString::fromLatin1(kClientModId)) {
			return _clientVersion;
		} else if (id == QString::fromLatin1(kTelegramModId)) {
			return _telegramVersion;
		}
		const auto i = present.find(id);
		if (i == end(present)) {
			return std::nullopt;
		}
		return i->second->metadata->version;
	};
	for (const auto &dependency : mod->metadata->dependencies) {
		const auto version = versionOf(dependency.id);
		const auto range = dependency.versionRange.toString();
		switch (dependency.type) {
		case DependencyType::Required:
			if (!version) {
				fail(mod, u"Requires \"%1\" %2, which is not loaded."_q
					.arg(dependency.id, range));
				return false;
			} else if (!dependency.versionRange.contains(*version)) {
				fail(mod, u"Requires \"%1\" %2, found %3."_q
					.arg(dependency.id, range, version->toString()));
				return false;
			}
			break;
		case DependencyType::Optional:
			if (version && !dependency.versionRange.contains(*version)) {
				fail(mod, u"Works with \"%1\" %2 only, found %3."_q
					.arg(dependency.id, range, version->toString()));
				return false;
			}
			break;
		case DependencyType::Incompatible:
			if (version && dependency.versionRange.contains(*version)) {
				fail(mod, u"Incompatible with \"%1\" %2."_q
					.arg(dependency.id, version->toString()));
				return false;
			}
			break;
		}
	}
	return true;
}

void ModLoader::resolve() {
	const auto apiVersion = *Version::Parse(
		QString::number(TGMOD_API_VERSION));

	// Duplicate ids: nobody wins, the user has to remove one.
	auto byId = std::map<QString, std::vector<ModContainer*>>();
	for (const auto mod : candidates()) {
		byId[mod->metadata->id].push_back(mod);
	}
	for (const auto &[id, list] : byId) {
		if (list.size() > 1) {
			auto folders = QStringList();
			for (const auto mod : list) {
				folders.push_back(mod->folder);
			}
			for (const auto mod : list) {
				fail(mod, u"Duplicate mod id, found in: %1"_q
					.arg(folders.join(u", "_q)));
			}
		}
	}

	for (const auto mod : candidates()) {
		if (!mod->metadata->apiVersion.contains(apiVersion)) {
			fail(mod, u"Needs mod API %1, the client has %2."_q
				.arg(mod->metadata->apiVersion.toString())
				.arg(TGMOD_API_VERSION));
		}
	}

	// Failing one mod may break the mods that depend on it, so repeat
	// until nothing changes.
	for (auto changed = true; changed;) {
		changed = false;
		auto present = std::map<QString, ModContainer*>();
		for (const auto mod : candidates()) {
			present.emplace(mod->metadata->id, mod);
		}
		for (const auto mod : candidates()) {
			if (!checkDependencies(mod, present)) {
				changed = true;
				break;
			}
		}
	}

	sort();
}

void ModLoader::sort() {
	// Kahn's algorithm over "must be set up before" edges, ties are broken
	// by id so the order is the same on every start.
	const auto list = candidates();
	auto byId = std::map<QString, ModContainer*>();
	for (const auto mod : list) {
		byId.emplace(mod->metadata->id, mod);
	}
	auto edges = std::map<ModContainer*, std::vector<ModContainer*>>();
	auto incoming = std::map<ModContainer*, int>();
	for (const auto mod : list) {
		incoming.emplace(mod, 0);
	}
	const auto edge = [&](ModContainer *from, ModContainer *to) {
		edges[from].push_back(to);
		++incoming[to];
	};
	for (const auto mod : list) {
		for (const auto &dependency : mod->metadata->dependencies) {
			const auto i = byId.find(dependency.id);
			if (i == end(byId)
				|| dependency.type == DependencyType::Incompatible) {
				continue;
			} else if (dependency.ordering == DependencyOrdering::After) {
				edge(i->second, mod);
			} else if (dependency.ordering == DependencyOrdering::Before) {
				edge(mod, i->second);
			}
		}
	}
	auto ready = std::map<QString, ModContainer*>();
	for (const auto &[mod, count] : incoming) {
		if (!count) {
			ready.emplace(mod->metadata->id, mod);
		}
	}
	auto sorted = std::vector<ModContainer*>();
	while (!ready.empty()) {
		const auto mod = begin(ready)->second;
		ready.erase(begin(ready));
		sorted.push_back(mod);
		for (const auto next : edges[mod]) {
			if (!--incoming[next]) {
				ready.emplace(next->metadata->id, next);
			}
		}
	}
	for (const auto mod : list) {
		if (std::find(begin(sorted), end(sorted), mod) == end(sorted)) {
			fail(mod, u"Dependency cycle: check \"ordering\" of this mod "
				"and of the mods it depends on."_q);
		}
	}

	// Load order first, everything that will not load after it.
	auto reordered = std::vector<std::unique_ptr<ModContainer>>();
	for (const auto mod : sorted) {
		if (mod->state != ModState::Discovered) {
			continue;
		}
		const auto i = std::find_if(begin(_mods), end(_mods), [&](
				const std::unique_ptr<ModContainer> &owned) {
			return owned.get() == mod;
		});
		mod->order = int(reordered.size());
		reordered.push_back(std::move(*i));
		_mods.erase(i);
	}
	for (auto &mod : _mods) {
		reordered.push_back(std::move(mod));
	}
	_mods = std::move(reordered);
}

bool ModLoader::load(not_null<ModContainer*> mod, const tgmod_host *host) {
	const auto path = QDir::cleanPath(
		mod->folder + mod->metadata->entrypoint);
	if (!QFileInfo(path).isFile()) {
		fail(mod, u"Library not found: %1"_q.arg(path));
		return false;
	}
	mod->library = std::make_unique<QLibrary>(path);
	if (!mod->library->load()) {
		fail(mod, u"Could not load %1: %2"_q
			.arg(path, mod->library->errorString()));
		return false;
	}
	const auto apiVersion = reinterpret_cast<tgmod_api_version_fn>(
		mod->library->resolve(TGMOD_API_VERSION_SYMBOL));
	const auto main = reinterpret_cast<tgmod_main_fn>(
		mod->library->resolve(TGMOD_MAIN_SYMBOL));
	if (!apiVersion || !main) {
		fail(mod, u"The library does not export %1 and %2."_q.arg(
			TGMOD_API_VERSION_SYMBOL,
			TGMOD_MAIN_SYMBOL));
		return false;
	}
	const auto built = apiVersion();
	if (built != TGMOD_API_VERSION) {
		fail(mod, u"Built for mod API %1, the client has %2."_q
			.arg(built)
			.arg(TGMOD_API_VERSION));
		return false;
	}
	mod->config = std::make_unique<JsonConfig>(
		_modsDir + u"config/"_q + mod->metadata->id + u".json"_q);
	if (!main(host, &mod->handle)) {
		fail(mod, u"tgmod_main reported a failure, see the log."_q);
		return false;
	}
	return true;
}

void ModLoader::construct(const tgmod_host *host) {
	for (const auto &owned : _mods) {
		const auto mod = owned.get();
		if (mod->state != ModState::Discovered) {
			continue;
		}
		const auto brokenDependency = [&]() -> QString {
			for (const auto &dependency : mod->metadata->dependencies) {
				if (dependency.type != DependencyType::Required) {
					continue;
				}
				for (const auto &other : _mods) {
					if (other->state == ModState::Failed
						&& other->metadata
						&& other->metadata->id == dependency.id) {
						return dependency.id;
					}
				}
			}
			return QString();
		}();
		if (!brokenDependency.isEmpty()) {
			fail(mod, u"Requires \"%1\", which failed."_q
				.arg(brokenDependency));
			continue;
		}
		LOG(("Mods: constructing \"%1\" %2."
			).arg(mod->id(), mod->metadata->version.toString()));
		if (load(mod, host)) {
			mod->state = ModState::Constructed;
		}
	}
}

void ModLoader::dispatch(EventBus &bus, tgmod_event_type type) {
	for (const auto &owned : _mods) {
		const auto mod = owned.get();
		if (!mod->active()) {
			continue;
		}
		auto event = EventData(type);
		bus.post(event, mod->id());
		// The listener may have failed the mod through the api.
		if (mod->active() && type != TGMOD_EVENT_SHUTDOWN) {
			mod->state = StateAfter(type);
		}
	}
}

const std::vector<std::unique_ptr<ModContainer>> &ModLoader::mods() const {
	return _mods;
}

ModContainer *ModLoader::findActive(const QString &id) const {
	for (const auto &mod : _mods) {
		if (mod->active() && mod->id() == id) {
			return mod.get();
		}
	}
	return nullptr;
}

void ModLoader::fail(not_null<ModContainer*> mod, const QString &error) {
	LOG(("Mods Error: \"%1\": %2").arg(mod->id(), error));
	mod->errors.push_back(error);
	if (mod->state == ModState::Failed) {
		return;
	}
	const auto wasActive = mod->active();
	mod->state = ModState::Failed;
	if (!wasActive || !mod->metadata) {
		return; // Not loaded yet, resolve() checks the dependents.
	}
	for (const auto &other : _mods) {
		if (!other->active()) {
			continue;
		}
		for (const auto &dependency : other->metadata->dependencies) {
			if (dependency.id == mod->metadata->id
				&& dependency.type == DependencyType::Required) {
				fail(other.get(), u"Requires \"%1\", which failed."_q
					.arg(dependency.id));
				break;
			}
		}
	}
}

} // namespace ModEngine
