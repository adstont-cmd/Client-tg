/*
TGMod: the modding API of Client-tg.

This is the only header a mod has to include. It is plain C on purpose:
a mod built with any compiler (MSVC, MinGW, Clang, the Android NDK) talks to
the client through the function table below, so mods do not have to be
rebuilt with the exact compiler and Qt that the client was built with.

C++ mods normally include <tgmod/tgmod.hpp> instead, which wraps this
header into classes, lambdas and std::string.

Rules every mod relies on:
- All callbacks run on the client's main (UI) thread.
- Strings are UTF-8 and zero-terminated. A string handed to a callback is
  valid only until the callback returns, copy it if you need it later.
- Every struct starts with struct_size. New fields are only ever added at
  the end, so a mod built against an older header keeps working.

Licensed under the MIT license, so that mods may use any license.
*/
#ifndef TGMOD_H
#define TGMOD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bumped when the ABI changes incompatibly. Mods declare which versions they
 * accept in mod.json ("apiVersion"), the loader also checks the exported
 * tgmod_api_version() of the library. */
#define TGMOD_API_VERSION 1

#if defined(_WIN32)
#define TGMOD_EXPORT __declspec(dllexport)
#else
#define TGMOD_EXPORT __attribute__((visibility("default")))
#endif

/* Opaque handle of one loaded mod. The host passes it to tgmod_main and
 * every host function takes it back, so the host knows who is calling. */
typedef struct tgmod_mod tgmod_mod;

typedef enum tgmod_log_level {
	TGMOD_LOG_DEBUG = 0,
	TGMOD_LOG_INFO = 1,
	TGMOD_LOG_WARN = 2,
	TGMOD_LOG_ERROR = 3,
} tgmod_log_level;

/* Listeners with a higher priority run first. Inside one priority the
 * listeners run in mod load order (dependencies first). */
typedef enum tgmod_priority {
	TGMOD_PRIORITY_HIGHEST = 0,
	TGMOD_PRIORITY_HIGH = 1,
	TGMOD_PRIORITY_NORMAL = 2,
	TGMOD_PRIORITY_LOW = 3,
	TGMOD_PRIORITY_LOWEST = 4,
} tgmod_priority;

typedef enum tgmod_event_type {
	/* Lifecycle ("mod bus"). Delivered only to the mod being set up, one
	 * mod after another in load order. */
	TGMOD_EVENT_COMMON_SETUP = 1, /* register commands, menus, config */
	TGMOD_EVENT_CLIENT_SETUP = 2, /* the UI exists, Core::App() is ready */
	TGMOD_EVENT_LOAD_COMPLETE = 3, /* every mod finished CLIENT_SETUP */
	TGMOD_EVENT_SHUTDOWN = 4, /* the client is quitting */

	/* Client events ("client bus"). Delivered to every mod that listens. */
	TGMOD_EVENT_SESSION_STARTED = 100, /* an account logged in / loaded */
	TGMOD_EVENT_SESSION_ENDED = 101, /* an account logged out / unloaded */
	TGMOD_EVENT_MESSAGE_SEND = 102, /* cancelable, text can be changed */
	TGMOD_EVENT_MESSAGE_RECEIVED = 103, /* a new message in any chat */
	TGMOD_EVENT_MESSAGE_DELETED = 104, /* a message disappeared */
	TGMOD_EVENT_CHAT_OPENED = 105, /* the user opened a chat */
	TGMOD_EVENT_IMC = 106, /* inter-mod message, see host->imc_send */
} tgmod_event_type;

#define TGMOD_EVENT_FLAG_CANCELABLE 1u

/* A message as mods see it. Ids are the raw values Telegram Desktop uses:
 * peer_id is PeerId::value (user, chat and channel ids are distinguished by
 * the high bits), message_id is MsgId. */
typedef struct tgmod_message {
	uint32_t struct_size;
	uint64_t session_id; /* which account, see tgmod_event.session_id */
	uint64_t peer_id; /* the chat */
	int64_t message_id; /* 0 for a message that is not sent yet */
	uint64_t sender_id; /* peer id of the author, 0 if unknown */
	int64_t date; /* unix time */
	int32_t outgoing; /* 1 if sent by this account */
	const char *text; /* message text without formatting */
} tgmod_message;

typedef struct tgmod_event {
	uint32_t struct_size;
	uint32_t type; /* tgmod_event_type */
	uint32_t flags; /* TGMOD_EVENT_FLAG_* */
	uint32_t canceled; /* read only, use host->event_cancel */

	uint64_t session_id; /* 0 for events not bound to an account */
	uint64_t peer_id; /* CHAT_OPENED and message events */
	const tgmod_message *message; /* MESSAGE_* events, else NULL */

	/* TGMOD_EVENT_IMC */
	const char *imc_sender; /* mod id of the sender */
	const char *imc_method;
	const char *imc_payload;
} tgmod_event;

typedef void (*tgmod_listener)(tgmod_event *event, void *user_data);

typedef struct tgmod_command {
	uint32_t struct_size;
	uint64_t session_id;
	uint64_t peer_id; /* the chat where the command was typed */
	const char *name; /* without the prefix */
	const char *args; /* everything after the name, trimmed */
} tgmod_command;

/* Return 1 if the command was handled: the typed text is not sent then.
 * Return 0 to let the text go to the chat as a normal message. */
typedef int (*tgmod_command_handler)(
	const tgmod_command *command,
	void *user_data);

typedef void (*tgmod_menu_handler)(uint64_t session_id, void *user_data);

typedef void (*tgmod_message_action_handler)(
	const tgmod_message *message,
	void *user_data);

/* Everything the client offers to a mod. Functions that return an int
 * return 1 on success and 0 on failure (the reason goes to the log). */
typedef struct tgmod_host {
	uint32_t struct_size;
	uint32_t api_version;
	const char *client_version; /* Client-tg + Telegram Desktop version */

	/* Logging, to the client's log and the mods screen. */
	void (*log)(tgmod_mod *mod, tgmod_log_level level, const char *text);

	/* Events. subscribe returns an id for unsubscribe, 0 on failure. If
	 * receive_canceled is 0 the listener is skipped once an earlier
	 * listener canceled the event. */
	uint64_t (*subscribe)(
		tgmod_mod *mod,
		uint32_t event_type,
		tgmod_priority priority,
		int receive_canceled,
		tgmod_listener listener,
		void *user_data);
	void (*unsubscribe)(tgmod_mod *mod, uint64_t subscription);
	void (*event_cancel)(tgmod_event *event);
	/* MESSAGE_SEND only: replace the text that will be sent. Formatting of
	 * the message is dropped when its text is changed. */
	int (*event_set_text)(tgmod_event *event, const char *text);

	/* Registries. Usually called from COMMON_SETUP. Names are unique per
	 * mod, a command name is unique among all mods. */
	int (*register_command)(
		tgmod_mod *mod,
		const char *name,
		const char *description,
		tgmod_command_handler handler,
		void *user_data);
	int (*register_main_menu_action)(
		tgmod_mod *mod,
		const char *title,
		tgmod_menu_handler handler,
		void *user_data);
	int (*register_message_action)(
		tgmod_mod *mod,
		const char *title,
		tgmod_message_action_handler handler,
		void *user_data);

	/* Config, stored in mods/config/<mod id>.json. Reading a missing key
	 * writes the default there, so users see every option in the file. */
	int (*config_get_bool)(tgmod_mod *mod, const char *key, int fallback);
	int64_t (*config_get_int)(
		tgmod_mod *mod,
		const char *key,
		int64_t fallback);
	/* Copies the value into buffer (always zero-terminated if size > 0)
	 * and returns the full length in bytes without the zero. */
	size_t (*config_get_string)(
		tgmod_mod *mod,
		const char *key,
		const char *fallback,
		char *buffer,
		size_t size);
	void (*config_set_bool)(tgmod_mod *mod, const char *key, int value);
	void (*config_set_int)(tgmod_mod *mod, const char *key, int64_t value);
	void (*config_set_string)(
		tgmod_mod *mod,
		const char *key,
		const char *value);

	/* Client actions. */
	int (*send_message)(
		tgmod_mod *mod,
		uint64_t session_id,
		uint64_t peer_id,
		const char *text);
	void (*show_toast)(tgmod_mod *mod, const char *text);
	/* The folder of the mod (where mod.json is), same contract as
	 * config_get_string. */
	size_t (*mod_dir)(tgmod_mod *mod, char *buffer, size_t size);

	/* Mods talking to each other. imc_send delivers an IMC event to the
	 * target mod only, right away. */
	int (*is_mod_loaded)(tgmod_mod *mod, const char *mod_id);
	int (*imc_send)(
		tgmod_mod *mod,
		const char *target_mod_id,
		const char *method,
		const char *payload);
} tgmod_host;

/* Every mod library exports these two functions.
 *
 * tgmod_api_version returns TGMOD_API_VERSION the mod was built with.
 *
 * tgmod_main is the mod's constructor: it runs once, right after the library
 * is loaded and before COMMON_SETUP. Keep the host pointer, it stays valid
 * until the process exits. Subscribe to events here. Return 1 on success;
 * returning 0 marks the mod as failed and disables the mods depending on it.
 */
typedef uint32_t (*tgmod_api_version_fn)(void);
typedef int (*tgmod_main_fn)(const tgmod_host *host, tgmod_mod *mod);

#define TGMOD_API_VERSION_SYMBOL "tgmod_api_version"
#define TGMOD_MAIN_SYMBOL "tgmod_main"

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* TGMOD_H */
