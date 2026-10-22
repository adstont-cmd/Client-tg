/*
Client-tg: mod engine.

This file is injected into the Telegram Desktop source tree at build time
by client/apply.py. It is distributed under the same license as
Telegram Desktop (GPLv3 with the OpenSSL exception).
*/
#pragma once

#include "tgmod/tgmod.h"

#include <QtCore/QString>

namespace ModEngine {

struct CommandEntry {
	QString owner;
	QString name;
	QString description;
	tgmod_command_handler handler = nullptr;
	void *userData = nullptr;
};

struct MainMenuEntry {
	QString owner;
	QString title;
	tgmod_menu_handler handler = nullptr;
	void *userData = nullptr;
};

struct MessageActionEntry {
	QString owner;
	QString title;
	tgmod_message_action_handler handler = nullptr;
	void *userData = nullptr;
};

// What mods add to the client. Entries keep the registration order, which
// follows the mod load order.
class Registry final {
public:
	// Returns an error text, empty on success.
	[[nodiscard]] QString addCommand(CommandEntry entry);
	[[nodiscard]] QString addMainMenuAction(MainMenuEntry entry);
	[[nodiscard]] QString addMessageAction(MessageActionEntry entry);

	[[nodiscard]] const CommandEntry *command(const QString &name) const;
	[[nodiscard]] const std::vector<CommandEntry> &commands() const;
	[[nodiscard]] const std::vector<MainMenuEntry> &mainMenuActions() const;
	[[nodiscard]] const std::vector<MessageActionEntry> &messageActions() const;

	void removeAll(const QString &owner);

private:
	std::vector<CommandEntry> _commands;
	std::vector<MainMenuEntry> _mainMenuActions;
	std::vector<MessageActionEntry> _messageActions;

};

} // namespace ModEngine
