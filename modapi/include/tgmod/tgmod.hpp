/*
TGMod: C++ wrapper over <tgmod/tgmod.h>. Header-only, needs C++17.

A whole mod looks like this:

	#include <tgmod/tgmod.hpp>

	class HelloMod final : public tgmod::Mod {
	public:
		explicit HelloMod(tgmod::Context &context) : Mod(context) {
			context.on<tgmod::MessageSendEvent>([](auto &event) {
				event.setText(event.text() + " :)");
			});
		}

		void commonSetup() override {
			context().command("hello", "Says hello", [](auto &command) {
				command.reply("Hello, " + command.args());
				return true;
			});
		}
	};

	TGMOD_MOD(HelloMod)

Exceptions never cross into the client: every callback is wrapped and an
escaped exception is logged as an error of the mod.
*/
#pragma once

#include "tgmod.h"

#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace tgmod {

enum class Priority {
	Highest = TGMOD_PRIORITY_HIGHEST,
	High = TGMOD_PRIORITY_HIGH,
	Normal = TGMOD_PRIORITY_NORMAL,
	Low = TGMOD_PRIORITY_LOW,
	Lowest = TGMOD_PRIORITY_LOWEST,
};

class Context;

namespace details {

inline std::string Str(const char *value) {
	return value ? std::string(value) : std::string();
}

template <typename Get>
std::string ReadString(Get &&get) {
	auto result = std::string(256, '\0');
	const auto size = get(result.data(), result.size());
	if (size >= result.size()) {
		result.resize(size + 1);
		get(result.data(), result.size());
	}
	result.resize(size);
	return result;
}

} // namespace details

class Message {
public:
	explicit Message(const tgmod_message *raw) : _raw(raw) {
	}

	[[nodiscard]] uint64_t sessionId() const { return _raw->session_id; }
	[[nodiscard]] uint64_t peerId() const { return _raw->peer_id; }
	[[nodiscard]] int64_t id() const { return _raw->message_id; }
	[[nodiscard]] uint64_t senderId() const { return _raw->sender_id; }
	[[nodiscard]] int64_t date() const { return _raw->date; }
	[[nodiscard]] bool outgoing() const { return _raw->outgoing != 0; }
	[[nodiscard]] std::string text() const { return details::Str(_raw->text); }
	[[nodiscard]] const tgmod_message *raw() const { return _raw; }

private:
	const tgmod_message *_raw = nullptr;

};

class Event {
public:
	Event(const tgmod_host *host, tgmod_event *raw) : _host(host), _raw(raw) {
	}

	[[nodiscard]] bool cancelable() const {
		return (_raw->flags & TGMOD_EVENT_FLAG_CANCELABLE) != 0;
	}
	[[nodiscard]] bool canceled() const { return _raw->canceled != 0; }
	void cancel() { _host->event_cancel(_raw); }

	[[nodiscard]] uint64_t sessionId() const { return _raw->session_id; }
	[[nodiscard]] tgmod_event *raw() const { return _raw; }

protected:
	const tgmod_host *_host = nullptr;
	tgmod_event *_raw = nullptr;

};

// Lifecycle events, delivered only to the mod being set up.
struct CommonSetupEvent : Event {
	static constexpr auto kType = TGMOD_EVENT_COMMON_SETUP;
	using Event::Event;
};
struct ClientSetupEvent : Event {
	static constexpr auto kType = TGMOD_EVENT_CLIENT_SETUP;
	using Event::Event;
};
struct LoadCompleteEvent : Event {
	static constexpr auto kType = TGMOD_EVENT_LOAD_COMPLETE;
	using Event::Event;
};
struct ShutdownEvent : Event {
	static constexpr auto kType = TGMOD_EVENT_SHUTDOWN;
	using Event::Event;
};

// Client events.
struct SessionStartedEvent : Event {
	static constexpr auto kType = TGMOD_EVENT_SESSION_STARTED;
	using Event::Event;
};
struct SessionEndedEvent : Event {
	static constexpr auto kType = TGMOD_EVENT_SESSION_ENDED;
	using Event::Event;
};

struct MessageEvent : Event {
	using Event::Event;
	[[nodiscard]] Message message() const { return Message(_raw->message); }
	[[nodiscard]] uint64_t peerId() const { return _raw->peer_id; }
	[[nodiscard]] std::string text() const {
		return details::Str(_raw->message->text);
	}
};

// Cancel it to not send the message, or change the text.
struct MessageSendEvent : MessageEvent {
	static constexpr auto kType = TGMOD_EVENT_MESSAGE_SEND;
	using MessageEvent::MessageEvent;
	bool setText(const std::string &text) {
		return _host->event_set_text(_raw, text.c_str()) != 0;
	}
};
struct MessageReceivedEvent : MessageEvent {
	static constexpr auto kType = TGMOD_EVENT_MESSAGE_RECEIVED;
	using MessageEvent::MessageEvent;
};
struct MessageDeletedEvent : MessageEvent {
	static constexpr auto kType = TGMOD_EVENT_MESSAGE_DELETED;
	using MessageEvent::MessageEvent;
};

struct ChatOpenedEvent : Event {
	static constexpr auto kType = TGMOD_EVENT_CHAT_OPENED;
	using Event::Event;
	[[nodiscard]] uint64_t peerId() const { return _raw->peer_id; }
};

struct ImcEvent : Event {
	static constexpr auto kType = TGMOD_EVENT_IMC;
	using Event::Event;
	[[nodiscard]] std::string sender() const {
		return details::Str(_raw->imc_sender);
	}
	[[nodiscard]] std::string method() const {
		return details::Str(_raw->imc_method);
	}
	[[nodiscard]] std::string payload() const {
		return details::Str(_raw->imc_payload);
	}
};

class Command {
public:
	Command(Context &context, const tgmod_command *raw)
	: _context(context)
	, _raw(raw) {
	}

	[[nodiscard]] std::string name() const { return details::Str(_raw->name); }
	[[nodiscard]] std::string args() const { return details::Str(_raw->args); }
	[[nodiscard]] uint64_t sessionId() const { return _raw->session_id; }
	[[nodiscard]] uint64_t peerId() const { return _raw->peer_id; }

	// Sends a message to the chat where the command was typed.
	inline bool reply(const std::string &text) const;

private:
	Context &_context;
	const tgmod_command *_raw = nullptr;

};

// Typed access to mods/config/<mod id>.json.
class Config {
public:
	Config(const tgmod_host *host, tgmod_mod *mod) : _host(host), _mod(mod) {
	}

	[[nodiscard]] bool getBool(const std::string &key, bool fallback) const {
		return _host->config_get_bool(_mod, key.c_str(), fallback ? 1 : 0)
			!= 0;
	}
	[[nodiscard]] int64_t getInt(
			const std::string &key,
			int64_t fallback) const {
		return _host->config_get_int(_mod, key.c_str(), fallback);
	}
	[[nodiscard]] std::string getString(
			const std::string &key,
			const std::string &fallback) const {
		return details::ReadString([&](char *buffer, size_t size) {
			return _host->config_get_string(
				_mod,
				key.c_str(),
				fallback.c_str(),
				buffer,
				size);
		});
	}
	void set(const std::string &key, bool value) {
		_host->config_set_bool(_mod, key.c_str(), value ? 1 : 0);
	}
	void set(const std::string &key, int64_t value) {
		_host->config_set_int(_mod, key.c_str(), value);
	}
	void set(const std::string &key, const std::string &value) {
		_host->config_set_string(_mod, key.c_str(), value.c_str());
	}

private:
	const tgmod_host *_host = nullptr;
	tgmod_mod *_mod = nullptr;

};

// Everything a mod uses to talk to the client. Lives as long as the process.
class Context {
public:
	Context(const tgmod_host *host, tgmod_mod *mod)
	: _host(host)
	, _mod(mod)
	, _config(host, mod) {
	}
	Context(const Context &) = delete;
	Context &operator=(const Context &) = delete;

	[[nodiscard]] const tgmod_host *host() const { return _host; }
	[[nodiscard]] tgmod_mod *mod() const { return _mod; }
	[[nodiscard]] Config &config() { return _config; }

	void log(tgmod_log_level level, const std::string &text) const {
		_host->log(_mod, level, text.c_str());
	}
	void debug(const std::string &text) const { log(TGMOD_LOG_DEBUG, text); }
	void info(const std::string &text) const { log(TGMOD_LOG_INFO, text); }
	void warn(const std::string &text) const { log(TGMOD_LOG_WARN, text); }
	void error(const std::string &text) const { log(TGMOD_LOG_ERROR, text); }

	// Subscribes to an event type, e.g. on<MessageSendEvent>(...).
	// Returns the id for off(), 0 on failure.
	template <typename EventType, typename Callback>
	uint64_t on(
			Callback &&callback,
			Priority priority = Priority::Normal,
			bool receiveCanceled = false) {
		auto handler = std::function<void(EventType&)>(
			std::forward<Callback>(callback));
		const auto data = keep(Listener{ this, [handler](
				Context *context,
				tgmod_event *raw) {
			auto event = EventType(context->_host, raw);
			handler(event);
		} });
		return _host->subscribe(
			_mod,
			EventType::kType,
			static_cast<tgmod_priority>(priority),
			receiveCanceled ? 1 : 0,
			&Context::ListenerTrampoline,
			data);
	}
	void off(uint64_t subscription) {
		_host->unsubscribe(_mod, subscription);
	}

	// "<prefix><name> <args>" typed in any chat calls the handler. The
	// prefix is "." by default (configurable by the user).
	bool command(
			const std::string &name,
			const std::string &description,
			std::function<bool(Command&)> handler) {
		const auto data = keep(CommandHandler{ this, std::move(handler) });
		return _host->register_command(
			_mod,
			name.c_str(),
			description.c_str(),
			&Context::CommandTrampoline,
			data) != 0;
	}

	// An item in the main (hamburger) menu.
	bool mainMenuAction(
			const std::string &title,
			std::function<void(uint64_t sessionId)> handler) {
		const auto data = keep(MenuHandler{ this, std::move(handler) });
		return _host->register_main_menu_action(
			_mod,
			title.c_str(),
			&Context::MenuTrampoline,
			data) != 0;
	}

	// An item in the right-click menu of a message.
	bool messageAction(
			const std::string &title,
			std::function<void(const Message&)> handler) {
		const auto data = keep(MessageActionHandler{
			this,
			std::move(handler),
		});
		return _host->register_message_action(
			_mod,
			title.c_str(),
			&Context::MessageActionTrampoline,
			data) != 0;
	}

	bool sendMessage(
			uint64_t sessionId,
			uint64_t peerId,
			const std::string &text) const {
		return _host->send_message(_mod, sessionId, peerId, text.c_str())
			!= 0;
	}
	void toast(const std::string &text) const {
		_host->show_toast(_mod, text.c_str());
	}
	[[nodiscard]] std::string modDir() const {
		return details::ReadString([&](char *buffer, size_t size) {
			return _host->mod_dir(_mod, buffer, size);
		});
	}
	[[nodiscard]] bool isModLoaded(const std::string &id) const {
		return _host->is_mod_loaded(_mod, id.c_str()) != 0;
	}
	bool imcSend(
			const std::string &target,
			const std::string &method,
			const std::string &payload = std::string()) const {
		return _host->imc_send(
			_mod,
			target.c_str(),
			method.c_str(),
			payload.c_str()) != 0;
	}

private:
	struct Listener {
		Context *context = nullptr;
		std::function<void(Context*, tgmod_event*)> callback;
	};
	struct CommandHandler {
		Context *context = nullptr;
		std::function<bool(Command&)> callback;
	};
	struct MenuHandler {
		Context *context = nullptr;
		std::function<void(uint64_t)> callback;
	};
	struct MessageActionHandler {
		Context *context = nullptr;
		std::function<void(const Message&)> callback;
	};

	// Handlers live until the process exits, like the mod itself.
	template <typename Handler>
	void *keep(Handler &&handler) {
		auto owned = std::make_shared<std::decay_t<Handler>>(
			std::forward<Handler>(handler));
		const auto result = static_cast<void*>(owned.get());
		_handlers.push_back(std::move(owned));
		return result;
	}

	template <typename Callback>
	static auto Guard(Context *context, Callback &&callback)
	-> decltype(callback()) {
		try {
			return callback();
		} catch (const std::exception &e) {
			context->error(std::string("Unhandled exception: ") + e.what());
		} catch (...) {
			context->error("Unhandled exception.");
		}
		return decltype(callback())();
	}

	static void ListenerTrampoline(tgmod_event *event, void *data) {
		const auto listener = static_cast<Listener*>(data);
		Guard(listener->context, [&] {
			listener->callback(listener->context, event);
		});
	}
	static int CommandTrampoline(const tgmod_command *raw, void *data) {
		const auto handler = static_cast<CommandHandler*>(data);
		return Guard(handler->context, [&] {
			auto command = Command(*handler->context, raw);
			return handler->callback(command) ? 1 : 0;
		});
	}
	static void MenuTrampoline(uint64_t sessionId, void *data) {
		const auto handler = static_cast<MenuHandler*>(data);
		Guard(handler->context, [&] { handler->callback(sessionId); });
	}
	static void MessageActionTrampoline(
			const tgmod_message *raw,
			void *data) {
		const auto handler = static_cast<MessageActionHandler*>(data);
		Guard(handler->context, [&] { handler->callback(Message(raw)); });
	}

	const tgmod_host *_host = nullptr;
	tgmod_mod *_mod = nullptr;
	Config _config;
	std::vector<std::shared_ptr<void>> _handlers;

};

inline bool Command::reply(const std::string &text) const {
	return _context.sendMessage(_raw->session_id, _raw->peer_id, text);
}

// Base class of a mod: the constructor is the "construct" phase, override
// the rest to take part in the lifecycle.
class Mod {
public:
	explicit Mod(Context &context) : _context(context) {
	}
	virtual ~Mod() = default;

	virtual void commonSetup() {
	}
	virtual void clientSetup() {
	}
	virtual void loadComplete() {
	}
	virtual void shutdown() {
	}

	[[nodiscard]] Context &context() const { return _context; }

private:
	Context &_context;

};

namespace details {

template <typename ModType>
int Main(const tgmod_host *host, tgmod_mod *mod) {
	static auto context = Context(host, mod);
	try {
		static auto instance = std::make_unique<ModType>(context);
		const auto self = instance.get();
		context.on<CommonSetupEvent>([=](auto&) { self->commonSetup(); });
		context.on<ClientSetupEvent>([=](auto&) { self->clientSetup(); });
		context.on<LoadCompleteEvent>([=](auto&) { self->loadComplete(); });
		context.on<ShutdownEvent>([=](auto&) { self->shutdown(); });
		return 1;
	} catch (const std::exception &e) {
		context.error(std::string("Construction failed: ") + e.what());
	} catch (...) {
		context.error("Construction failed.");
	}
	return 0;
}

} // namespace details

} // namespace tgmod

// Declares the entry points of a mod library for the given Mod subclass.
#define TGMOD_MOD(ModType) \
	extern "C" TGMOD_EXPORT uint32_t tgmod_api_version(void) { \
		return TGMOD_API_VERSION; \
	} \
	extern "C" TGMOD_EXPORT int tgmod_main( \
			const tgmod_host *host, \
			tgmod_mod *mod) { \
		return ::tgmod::details::Main<ModType>(host, mod); \
	}
