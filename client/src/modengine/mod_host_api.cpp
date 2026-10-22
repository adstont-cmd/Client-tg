/*
Client-tg: mod engine.

This file is injected into the Telegram Desktop source tree at build time
by client/apply.py. It is distributed under the same license as
Telegram Desktop (GPLv3 with the OpenSSL exception).
*/
#include "modengine/mod_host_api.h"

#include "modengine/mod_engine.h"
#include "api/api_common.h"
#include "apiwrap.h"
#include "core/application.h"
#include "data/data_session.h"
#include "history/history.h"
#include "main/main_session.h"
#include "window/window_controller.h"

#include <cstring>

namespace ModEngine {
namespace {

// Every entry point checks the handle: a mod that failed keeps running code
// (timers, threads), but the client ignores what it asks for.
[[nodiscard]] ModContainer *Alive(tgmod_mod *mod) {
	if (!mod || !mod->container || !mod->container->active()) {
		return nullptr;
	}
	return mod->container;
}

[[nodiscard]] QString Utf8(const char *text) {
	return text ? QString::fromUtf8(text) : QString();
}

size_t CopyOut(const QString &value, char *buffer, size_t size) {
	const auto utf8 = value.toUtf8();
	const auto length = size_t(utf8.size());
	if (buffer && size > 0) {
		const auto count = std::min(length, size - 1);
		std::memcpy(buffer, utf8.constData(), count);
		buffer[count] = '\0';
	}
	return length;
}

void Log(tgmod_mod *mod, tgmod_log_level level, const char *text) {
	if (mod && mod->container) {
		Engine::Instance().log(mod->container, level, Utf8(text));
	}
}

uint64_t Subscribe(
		tgmod_mod *mod,
		uint32_t eventType,
		tgmod_priority priority,
		int receiveCanceled,
		tgmod_listener listener,
		void *userData) {
	const auto container = Alive(mod);
	if (!container || !listener) {
		return 0;
	}
	const auto id = container->id();
	const auto lifecycle = (eventType == TGMOD_EVENT_COMMON_SETUP)
		|| (eventType == TGMOD_EVENT_CLIENT_SETUP)
		|| (eventType == TGMOD_EVENT_LOAD_COMPLETE)
		|| (eventType == TGMOD_EVENT_SHUTDOWN);
	const auto client = (eventType >= TGMOD_EVENT_SESSION_STARTED)
		&& (eventType <= TGMOD_EVENT_IMC);
	if (!lifecycle && !client) {
		Engine::Instance().log(
			container,
			TGMOD_LOG_WARN,
			u"subscribe: unknown event type %1."_q.arg(eventType));
		return 0;
	}
	return Engine::Instance().bus().subscribe(
		tgmod_event_type(eventType),
		priority,
		receiveCanceled != 0,
		id,
		container->order,
		[=](EventData &event) {
			if (Alive(mod)) {
				listener(event.raw(), userData);
			}
		});
}

void Unsubscribe(tgmod_mod *mod, uint64_t subscription) {
	if (mod && mod->container) {
		Engine::Instance().bus().unsubscribe(
			subscription,
			mod->container->id());
	}
}

void EventCancel(tgmod_event *event) {
	if (const auto data = Engine::Instance().bus().posting(event)) {
		data->cancel();
	}
}

int EventSetText(tgmod_event *event, const char *text) {
	const auto data = Engine::Instance().bus().posting(event);
	return (data && text && data->setText(Utf8(text))) ? 1 : 0;
}

int Report(not_null<ModContainer*> container, const QString &error) {
	if (error.isEmpty()) {
		return 1;
	}
	Engine::Instance().log(container, TGMOD_LOG_ERROR, error);
	return 0;
}

int RegisterCommand(
		tgmod_mod *mod,
		const char *name,
		const char *description,
		tgmod_command_handler handler,
		void *userData) {
	const auto container = Alive(mod);
	if (!container) {
		return 0;
	}
	return Report(container, Engine::Instance().registry().addCommand({
		.owner = container->id(),
		.name = Utf8(name),
		.description = Utf8(description),
		.handler = handler,
		.userData = userData,
	}));
}

int RegisterMainMenuAction(
		tgmod_mod *mod,
		const char *title,
		tgmod_menu_handler handler,
		void *userData) {
	const auto container = Alive(mod);
	if (!container) {
		return 0;
	}
	return Report(container, Engine::Instance().registry().addMainMenuAction({
		.owner = container->id(),
		.title = Utf8(title),
		.handler = handler,
		.userData = userData,
	}));
}

int RegisterMessageAction(
		tgmod_mod *mod,
		const char *title,
		tgmod_message_action_handler handler,
		void *userData) {
	const auto container = Alive(mod);
	if (!container) {
		return 0;
	}
	return Report(container, Engine::Instance().registry().addMessageAction({
		.owner = container->id(),
		.title = Utf8(title),
		.handler = handler,
		.userData = userData,
	}));
}

[[nodiscard]] JsonConfig *Config(tgmod_mod *mod) {
	const auto container = Alive(mod);
	return container ? container->config.get() : nullptr;
}

int ConfigGetBool(tgmod_mod *mod, const char *key, int fallback) {
	const auto config = Config(mod);
	return config
		? (config->getBool(Utf8(key), fallback != 0) ? 1 : 0)
		: fallback;
}

int64_t ConfigGetInt(tgmod_mod *mod, const char *key, int64_t fallback) {
	const auto config = Config(mod);
	return config ? config->getInt(Utf8(key), fallback) : fallback;
}

size_t ConfigGetString(
		tgmod_mod *mod,
		const char *key,
		const char *fallback,
		char *buffer,
		size_t size) {
	const auto config = Config(mod);
	return CopyOut(
		config ? config->getString(Utf8(key), Utf8(fallback)) : Utf8(fallback),
		buffer,
		size);
}

void ConfigSetBool(tgmod_mod *mod, const char *key, int value) {
	if (const auto config = Config(mod)) {
		config->set(Utf8(key), value != 0);
	}
}

void ConfigSetInt(tgmod_mod *mod, const char *key, int64_t value) {
	if (const auto config = Config(mod)) {
		config->set(Utf8(key), double(value));
	}
}

void ConfigSetString(tgmod_mod *mod, const char *key, const char *value) {
	if (const auto config = Config(mod)) {
		config->set(Utf8(key), Utf8(value));
	}
}

int SendText(
		tgmod_mod *mod,
		uint64_t sessionId,
		uint64_t peerId,
		const char *text) {
	const auto container = Alive(mod);
	if (!container) {
		return 0;
	}
	const auto session = Engine::Instance().session(sessionId);
	const auto value = Utf8(text);
	if (!session || !peerId || value.trimmed().isEmpty()) {
		return Report(container, u"send_message: bad arguments."_q);
	}
	const auto history = session->data().history(PeerId(uint64(peerId)));
	auto message = Api::MessageToSend(Api::SendAction(history));
	message.textWithTags = { value };
	session->api().sendMessage(std::move(message));
	return 1;
}

void ShowToast(tgmod_mod *mod, const char *text) {
	if (!Alive(mod)) {
		return;
	} else if (const auto window = Core::App().activeWindow()) {
		window->showToast(Utf8(text));
	}
}

size_t ModDir(tgmod_mod *mod, char *buffer, size_t size) {
	const auto container = (mod && mod->container) ? mod->container : nullptr;
	return CopyOut(container ? container->folder : QString(), buffer, size);
}

int IsModLoaded(tgmod_mod *mod, const char *modId) {
	return Engine::Instance().loader().findActive(Utf8(modId)) ? 1 : 0;
}

int ImcSend(
		tgmod_mod *mod,
		const char *targetModId,
		const char *method,
		const char *payload) {
	const auto container = Alive(mod);
	if (!container) {
		return 0;
	}
	const auto target = Engine::Instance().loader().findActive(
		Utf8(targetModId));
	if (!target) {
		return 0;
	}
	auto event = EventData(TGMOD_EVENT_IMC);
	event.setImc(container->id(), Utf8(method), Utf8(payload));
	Engine::Instance().bus().post(event, target->id());
	return 1;
}

} // namespace

const tgmod_host *HostApi() {
	static const auto version = Engine::Instance().clientVersionText().toUtf8();
	static const auto host = tgmod_host{
		.struct_size = sizeof(tgmod_host),
		.api_version = TGMOD_API_VERSION,
		.client_version = version.constData(),
		.log = Log,
		.subscribe = Subscribe,
		.unsubscribe = Unsubscribe,
		.event_cancel = EventCancel,
		.event_set_text = EventSetText,
		.register_command = RegisterCommand,
		.register_main_menu_action = RegisterMainMenuAction,
		.register_message_action = RegisterMessageAction,
		.config_get_bool = ConfigGetBool,
		.config_get_int = ConfigGetInt,
		.config_get_string = ConfigGetString,
		.config_set_bool = ConfigSetBool,
		.config_set_int = ConfigSetInt,
		.config_set_string = ConfigSetString,
		.send_message = SendText,
		.show_toast = ShowToast,
		.mod_dir = ModDir,
		.is_mod_loaded = IsModLoaded,
		.imc_send = ImcSend,
	};
	return &host;
}

} // namespace ModEngine
