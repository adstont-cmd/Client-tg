/*
Client-tg: mod engine.

This file is injected into the Telegram Desktop source tree at build time
by client/apply.py. It is distributed under the same license as
Telegram Desktop (GPLv3 with the OpenSSL exception).
*/
#include "modengine/mod_engine.h"

#include "modengine/mod_host_api.h"
#include "core/application.h"
#include "core/version.h"
#include "main/main_session.h"
#include "logs.h"
#include "settings.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QRegularExpression>

namespace ModEngine {
namespace {

constexpr auto kMaxLogLines = 200;

// Exists while mods are being constructed and set up. If the client finds
// it on start, the previous start crashed inside a mod: start without
// mods once, so the user can disable the broken one.
constexpr auto kLoadingMarker = ".loading";

[[nodiscard]] Version ParseOrZero(const QString &text) {
	return Version::Parse(text).value_or(*Version::Parse(u"0"_q));
}

} // namespace

Engine &Engine::Instance() {
	static const auto instance = new Engine();
	return *instance;
}

Engine::Engine()
: _modsDir(cWorkingDir() + u"mods/"_q)
, _settings(_modsDir + u"engine.json"_q) {
}

bool Engine::started() const {
	return _started;
}

bool Engine::safeMode() const {
	return _safeMode;
}

QString Engine::modsDir() const {
	return _modsDir;
}

QString Engine::commandPrefix() {
	const auto prefix = _settings.getString(u"commandPrefix"_q, u"."_q);
	return prefix.isEmpty() ? u"."_q : prefix;
}

QString Engine::clientVersionText() const {
	return u"Client-tg mod engine %1, Telegram Desktop %2"_q.arg(
		QString::fromLatin1(kEngineVersion),
		QString::fromLatin1(AppVersionStr));
}

EventBus &Engine::bus() {
	return _bus;
}

ModLoader &Engine::loader() {
	return *_loader;
}

Registry &Engine::registry() {
	return _registry;
}

JsonConfig &Engine::settings() {
	return _settings;
}

bool Engine::modEnabled(const QString &id) {
	return !_settings.getStringList(u"disabled"_q, {}).contains(id);
}

void Engine::setModEnabled(const QString &id, bool enabled) {
	auto disabled = _settings.getStringList(u"disabled"_q, {});
	disabled.removeAll(id);
	if (!enabled) {
		disabled.push_back(id);
	}
	_settings.set(u"disabled"_q, QJsonArray::fromStringList(disabled));
	_settings.saveNow();
}

void Engine::start() {
	if (_started) {
		return;
	}
	_started = true;
	_loader = std::make_unique<ModLoader>(
		_modsDir,
		ParseOrZero(QString::fromLatin1(kEngineVersion)),
		ParseOrZero(QString::fromLatin1(AppVersionStr)));
	LOG(("Mods: %1, mod API %2, folder %3."
		).arg(clientVersionText()).arg(TGMOD_API_VERSION).arg(_modsDir));

	if (!_settings.getBool(u"enabled"_q, true)) {
		LOG(("Mods: turned off in engine.json."));
		return;
	}
	auto disabled = _settings.getStringList(u"disabled"_q, {});
	const auto marker = _modsDir + QString::fromLatin1(kLoadingMarker);
	_safeMode = QFile::exists(marker);

	_loader->discover(disabled);
	if (_safeMode) {
		LOG(("Mods Error: the last start crashed while loading mods, "
			"starting without mods."));
		QFile::remove(marker);
		for (const auto &mod : _loader->mods()) {
			if (mod->state == ModState::Discovered) {
				mod->state = ModState::Disabled;
				mod->errors.push_back(u"Not loaded: the last start "
					"crashed while loading mods (safe mode)."_q);
			}
		}
		return;
	}
	_loader->resolve();

	auto file = QFile(marker);
	if (file.open(QIODevice::WriteOnly)) {
		file.close();
	}

	setupCommands();
	QObject::connect(
		QCoreApplication::instance(),
		&QCoreApplication::aboutToQuit,
		[=] { shutdown(); });

	_loader->construct(HostApi());
	cleanupFailed();
	_loader->dispatch(_bus, TGMOD_EVENT_COMMON_SETUP);
	cleanupFailed();

	// Application::run() creates the windows after us, CLIENT_SETUP must
	// see them, so wait for the event loop.
	crl::on_main(&Core::App(), [=] { finishLoading(); });
}

void Engine::finishLoading() {
	_loader->dispatch(_bus, TGMOD_EVENT_CLIENT_SETUP);
	cleanupFailed();
	_loader->dispatch(_bus, TGMOD_EVENT_LOAD_COMPLETE);
	cleanupFailed();
	QFile::remove(_modsDir + QString::fromLatin1(kLoadingMarker));

	auto loaded = 0;
	for (const auto &mod : _loader->mods()) {
		loaded += mod->active() ? 1 : 0;
	}
	LOG(("Mods: %1 of %2 loaded."
		).arg(loaded).arg(_loader->mods().size()));
}

void Engine::shutdown() {
	if (!_started || _shutdown || !_loader) {
		return;
	}
	_shutdown = true;
	_loader->dispatch(_bus, TGMOD_EVENT_SHUTDOWN);
	for (const auto &mod : _loader->mods()) {
		if (mod->config) {
			mod->config->saveNow();
		}
	}
	_settings.saveNow();
}

void Engine::log(
		not_null<ModContainer*> mod,
		tgmod_log_level level,
		const QString &text) {
	const auto name = [&] {
		switch (level) {
		case TGMOD_LOG_DEBUG: return u"debug"_q;
		case TGMOD_LOG_INFO: return u"info"_q;
		case TGMOD_LOG_WARN: return u"warn"_q;
		case TGMOD_LOG_ERROR: return u"error"_q;
		}
		return u"info"_q;
	}();
	LOG(("Mods [%1] %2: %3").arg(mod->id(), name, text));
	if (level == TGMOD_LOG_DEBUG) {
		return;
	}
	mod->log.push_back(u"[%1] %2"_q.arg(name, text));
	while (mod->log.size() > kMaxLogLines) {
		mod->log.pop_front();
	}
}

void Engine::failMod(not_null<ModContainer*> mod, const QString &error) {
	_loader->fail(mod, error);
	cleanupFailed();
}

void Engine::cleanupFailed() {
	for (const auto &mod : _loader->mods()) {
		if (mod->state == ModState::Failed) {
			_bus.unsubscribeAll(mod->id());
			_registry.removeAll(mod->id());
		}
	}
}

void Engine::addSession(not_null<Main::Session*> session) {
	const auto id = session->uniqueId();
	if (_sessions.contains(id)) {
		return;
	}
	_sessions.emplace(id, session);
	session->lifetime().add([=] {
		_sessions.erase(id);
		auto event = EventData(TGMOD_EVENT_SESSION_ENDED);
		event.setSession(id);
		_bus.post(event);
	});
	auto event = EventData(TGMOD_EVENT_SESSION_STARTED);
	event.setSession(id);
	_bus.post(event);
}

Main::Session *Engine::session(uint64 sessionId) const {
	const auto i = _sessions.find(sessionId);
	return (i != end(_sessions)) ? i->second : nullptr;
}

void Engine::setupCommands() {
	// The engine listens first, so a command never reaches the chat and
	// mods see only real messages.
	_bus.subscribe(
		TGMOD_EVENT_MESSAGE_SEND,
		TGMOD_PRIORITY_HIGHEST,
		false,
		QString(),
		-1,
		[=](EventData &event) {
			if (runCommand(event)) {
				event.cancel();
			}
		});
}

bool Engine::runCommand(EventData &event) {
	const auto prefix = commandPrefix();
	const auto text = event.message().text.trimmed();
	if (!text.startsWith(prefix) || text.size() == prefix.size()) {
		return false;
	}
	const auto body = text.mid(prefix.size());
	const auto space = body.indexOf(QRegularExpression(u"\\s"_q));
	const auto name = (space < 0) ? body : body.left(space);
	const auto args = (space < 0) ? QString() : body.mid(space).trimmed();
	const auto entry = _registry.command(name);
	if (!entry) {
		return false;
	}
	const auto owner = _loader->findActive(entry->owner);
	if (!owner) {
		return false;
	}
	const auto nameUtf8 = name.toUtf8();
	const auto argsUtf8 = args.toUtf8();
	const auto command = tgmod_command{
		.struct_size = sizeof(tgmod_command),
		.session_id = event.message().sessionId,
		.peer_id = event.message().peerId,
		.name = nameUtf8.constData(),
		.args = argsUtf8.constData(),
	};
	// The entry may go away if the handler fails its mod, copy it.
	const auto handler = entry->handler;
	const auto userData = entry->userData;
	return handler(&command, userData) != 0;
}

} // namespace ModEngine
