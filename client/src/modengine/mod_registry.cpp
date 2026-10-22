/*
Client-tg: mod engine.

This file is injected into the Telegram Desktop source tree at build time
by client/apply.py. It is distributed under the same license as
Telegram Desktop (GPLv3 with the OpenSSL exception).
*/
#include "modengine/mod_registry.h"

#include <QtCore/QRegularExpression>

namespace ModEngine {
namespace {

constexpr auto kMaxTitleLength = 64;

template <typename Entry>
void RemoveOwned(std::vector<Entry> &list, const QString &owner) {
	list.erase(
		std::remove_if(begin(list), end(list), [&](const Entry &entry) {
			return entry.owner == owner;
		}),
		end(list));
}

[[nodiscard]] QString CheckTitle(const QString &title) {
	if (title.trimmed().isEmpty()) {
		return u"The title is empty."_q;
	} else if (title.size() > kMaxTitleLength) {
		return u"The title is longer than %1 characters."_q
			.arg(kMaxTitleLength);
	}
	return QString();
}

} // namespace

QString Registry::addCommand(CommandEntry entry) {
	static const auto regex = QRegularExpression(u"^[a-z0-9_]{1,32}$"_q);
	if (!entry.handler) {
		return u"No handler."_q;
	} else if (!regex.match(entry.name).hasMatch()) {
		return u"Bad command name \"%1\": use 1-32 characters a-z, 0-9 "
			"and _."_q.arg(entry.name);
	} else if (const auto existing = command(entry.name)) {
		return u"The command \"%1\" is already registered by \"%2\"."_q
			.arg(entry.name, existing->owner);
	}
	_commands.push_back(std::move(entry));
	return QString();
}

QString Registry::addMainMenuAction(MainMenuEntry entry) {
	if (!entry.handler) {
		return u"No handler."_q;
	} else if (const auto error = CheckTitle(entry.title); !error.isEmpty()) {
		return error;
	}
	_mainMenuActions.push_back(std::move(entry));
	return QString();
}

QString Registry::addMessageAction(MessageActionEntry entry) {
	if (!entry.handler) {
		return u"No handler."_q;
	} else if (const auto error = CheckTitle(entry.title); !error.isEmpty()) {
		return error;
	}
	_messageActions.push_back(std::move(entry));
	return QString();
}

const CommandEntry *Registry::command(const QString &name) const {
	for (const auto &entry : _commands) {
		if (entry.name == name) {
			return &entry;
		}
	}
	return nullptr;
}

const std::vector<CommandEntry> &Registry::commands() const {
	return _commands;
}

const std::vector<MainMenuEntry> &Registry::mainMenuActions() const {
	return _mainMenuActions;
}

const std::vector<MessageActionEntry> &Registry::messageActions() const {
	return _messageActions;
}

void Registry::removeAll(const QString &owner) {
	RemoveOwned(_commands, owner);
	RemoveOwned(_mainMenuActions, owner);
	RemoveOwned(_messageActions, owner);
}

} // namespace ModEngine
