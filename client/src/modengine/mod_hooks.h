/*
Client-tg: mod engine.

This file is injected into the Telegram Desktop source tree at build time
by client/apply.py. It is distributed under the same license as
Telegram Desktop (GPLv3 with the OpenSSL exception).
*/
#pragma once

class HistoryItem;

namespace Api {
struct MessageToSend;
} // namespace Api

namespace Main {
class Account;
class Session;
} // namespace Main

namespace Ui {
class PopupMenu;
class VerticalLayout;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

// The only functions Telegram Desktop calls, each from one line that
// client/apply.py inserts. Everything else the engine reaches through
// public tdesktop APIs (rpl producers), so upstream updates rarely touch us.
namespace ModEngine::Hooks {

// core/application.cpp, Application::run().
void Start();

// main/main_domain.cpp, Domain::watchSession(): every account, also the
// ones added later.
void WatchAccount(not_null<Main::Account*> account);

// apiwrap.cpp, start of ApiWrap::sendMessage(). Fires MESSAGE_SEND and
// runs mod commands. Returns true if the message must not be sent.
[[nodiscard]] bool BeforeSend(
	not_null<Main::Session*> session,
	Api::MessageToSend &message);

// window/window_session_controller.cpp, SessionController constructor.
void SessionControllerCreated(not_null<Window::SessionController*> window);

// window/window_main_menu.cpp, MainMenu::setupMenu(), above "Settings".
void FillMainMenu(
	not_null<Ui::VerticalLayout*> menu,
	not_null<Window::SessionController*> window);

// history/history_inner_widget.cpp, HistoryInner::showContextMenu().
void FillMessageMenu(
	not_null<Window::SessionController*> window,
	not_null<Ui::PopupMenu*> menu,
	HistoryItem *item);

} // namespace ModEngine::Hooks
