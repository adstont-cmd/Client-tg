/*
Client-tg: built-in TG WS Proxy for Telegram Desktop.

This file is injected into the Telegram Desktop source tree at build time
by client/apply.py. It is distributed under the same license as
Telegram Desktop (GPLv3 with the OpenSSL exception).
*/
#pragma once

#include "mtproto/mtproto_proxy_data.h"

#include <QtCore/QObject>
#include <QtCore/QProcess>
#include <QtCore/QTimer>

namespace Ui {
class RpWidget;
} // namespace Ui

namespace TgWs {

// Runs the bundled TG WS Proxy sidecar (tgws/TgWsProxy.exe next to
// Telegram.exe), keeps it alive and switches Telegram to it and back.
//
// The sidecar also installs updates of itself and of Telegram.exe; it
// reports that through its exit code (see sidecar/tgws_sidecar.py).
class Controller final : public QObject {
public:
	static constexpr auto kPort = 1444;

	// Called once on startup, right before Telegram applies proxy settings.
	// On the very first launch it switches Telegram to the built-in proxy,
	// so that logging in works even where Telegram is blocked.
	static void Start();
	[[nodiscard]] static Controller &Instance();

	[[nodiscard]] bool enabled() const;
	[[nodiscard]] rpl::producer<bool> enabledValue() const;
	[[nodiscard]] bool running() const;
	void toggle();

private:
	Controller();

	[[nodiscard]] QString dataDir() const;
	[[nodiscard]] QString sidecarPath() const;
	[[nodiscard]] MTP::ProxyData proxyData() const;
	void loadSecret();
	void start();
	void stop();
	void finished(int exitCode);
	void scheduleRestart();
	void promptTelegramRestart();

	QString _secret;
	QProcess *_process = nullptr;
	QTimer _restartTimer;
	int _failures = 0;
	bool _quitting = false;
	bool _restartPrompted = false;
	bool _firstRun = false;

};

// Adds the one-click proxy toggle to the chats list (main screen).
void SetupDialogsButton(not_null<Ui::RpWidget*> parent);

} // namespace TgWs
