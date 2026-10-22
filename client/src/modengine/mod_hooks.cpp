/*
Client-tg: mod engine.

This file is injected into the Telegram Desktop source tree at build time
by client/apply.py. It is distributed under the same license as
Telegram Desktop (GPLv3 with the OpenSSL exception).
*/
#include "modengine/mod_hooks.h"

#include "modengine/mod_engine.h"
#include "modengine/mods_box.h"
#include "api/api_common.h"
#include "base/unixtime.h"
#include "boxes/abstract_box.h"
#include "core/application.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "dialogs/dialogs_key.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_menu_icons.h"
#include "styles/style_window.h"

namespace ModEngine::Hooks {
namespace {

[[nodiscard]] EventData::MessageFields Fields(
		not_null<const HistoryItem*> item) {
	const auto peer = item->history()->peer;
	return {
		.sessionId = peer->session().uniqueId(),
		.peerId = peer->id.value,
		.messageId = item->id.bare,
		.senderId = item->from()->id.value,
		.date = item->date(),
		.outgoing = item->out(),
		.text = item->originalText().text,
	};
}

void PostMessageEvent(
		tgmod_event_type type,
		not_null<const HistoryItem*> item) {
	auto event = EventData(type);
	event.setMessage(Fields(item));
	Engine::Instance().bus().post(event);
}

[[nodiscard]] bool HasActiveMods() {
	auto &engine = Engine::Instance();
	if (!engine.started()) {
		return false;
	}
	for (const auto &mod : engine.loader().mods()) {
		if (mod->active()) {
			return true;
		}
	}
	return false;
}

} // namespace

void Start() {
	Engine::Instance().start();
}

void WatchAccount(not_null<Main::Account*> account) {
	account->sessionValue(
	) | rpl::filter([=](Main::Session *session) {
		return session != nullptr;
	}) | rpl::on_next([=](Main::Session *session) {
		Engine::Instance().addSession(session);

		session->data().newItemAdded(
		) | rpl::filter([](not_null<HistoryItem*>) {
			return HasActiveMods();
		}) | rpl::on_next([=](not_null<HistoryItem*> item) {
			PostMessageEvent(TGMOD_EVENT_MESSAGE_RECEIVED, item);
		}, session->lifetime());

		session->data().itemRemoved(
		) | rpl::filter([](not_null<const HistoryItem*>) {
			return !Core::Quitting() && HasActiveMods();
		}) | rpl::on_next([=](not_null<const HistoryItem*> item) {
			PostMessageEvent(TGMOD_EVENT_MESSAGE_DELETED, item);
		}, session->lifetime());
	}, account->lifetime());
}

bool BeforeSend(
		not_null<Main::Session*> session,
		Api::MessageToSend &message) {
	if (!HasActiveMods()) {
		return false;
	}
	auto event = EventData(TGMOD_EVENT_MESSAGE_SEND, true);
	event.setMessage({
		.sessionId = session->uniqueId(),
		.peerId = message.action.history->peer->id.value,
		.senderId = session->user()->id.value,
		.date = base::unixtime::now(),
		.outgoing = true,
		.text = message.textWithTags.text,
	});
	if (Engine::Instance().bus().post(event)) {
		return true;
	} else if (event.textChanged()) {
		const auto text = event.message().text;
		if (text.trimmed().isEmpty()) {
			return true;
		}
		// Entity offsets of the old text would point to wrong places.
		message.textWithTags = { text };
	}
	return false;
}

void SessionControllerCreated(not_null<Window::SessionController*> window) {
	const auto sessionId = window->session().uniqueId();
	window->activeChatValue(
	) | rpl::map([](const Dialogs::Key &key) {
		return key.peer();
	}) | rpl::distinct_until_changed(
	) | rpl::filter([](PeerData *peer) {
		return (peer != nullptr) && HasActiveMods();
	}) | rpl::on_next([=](PeerData *peer) {
		auto event = EventData(TGMOD_EVENT_CHAT_OPENED);
		event.setSession(sessionId);
		event.setPeer(peer->id.value);
		Engine::Instance().bus().post(event);
	}, window->lifetime());
}

void FillMainMenu(
		not_null<Ui::VerticalLayout*> menu,
		not_null<Window::SessionController*> window) {
	auto &engine = Engine::Instance();
	if (!engine.started()) {
		return;
	}
	Settings::AddButtonWithIcon(
		menu,
		rpl::single(u"Моды"_q),
		st::mainMenuButton,
		{ &st::menuIconCustomize }
	)->setClickedCallback([=] {
		window->show(Box(ModsBox));
	});

	const auto sessionId = window->session().uniqueId();
	for (const auto &entry : engine.registry().mainMenuActions()) {
		const auto owner = entry.owner;
		const auto handler = entry.handler;
		const auto userData = entry.userData;
		Settings::AddButtonWithIcon(
			menu,
			rpl::single(entry.title),
			st::mainMenuButton,
			{ &st::menuIconManage }
		)->setClickedCallback([=] {
			if (Engine::Instance().loader().findActive(owner)) {
				handler(sessionId, userData);
			}
		});
	}
}

void FillMessageMenu(
		not_null<Window::SessionController*> window,
		not_null<Ui::PopupMenu*> menu,
		HistoryItem *item) {
	auto &engine = Engine::Instance();
	if (!item || !engine.started()) {
		return;
	}
	const auto &actions = engine.registry().messageActions();
	if (actions.empty()) {
		return;
	}
	const auto session = &window->session();
	const auto itemId = item->fullId();
	if (!menu->empty()) {
		menu->addSeparator();
	}
	for (const auto &entry : actions) {
		const auto owner = entry.owner;
		const auto handler = entry.handler;
		const auto userData = entry.userData;
		menu->addAction(entry.title, [=] {
			// The message may be gone by the time the user clicks.
			const auto item = session->data().message(itemId);
			if (!item || !Engine::Instance().loader().findActive(owner)) {
				return;
			}
			auto event = EventData(TGMOD_EVENT_MESSAGE_RECEIVED);
			event.setMessage(Fields(item));
			handler(event.raw()->message, userData);
		}, &st::menuIconManage);
	}
}

} // namespace ModEngine::Hooks
