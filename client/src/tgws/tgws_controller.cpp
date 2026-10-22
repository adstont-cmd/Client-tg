/*
Client-tg: built-in TG WS Proxy for Telegram Desktop.

This file is injected into the Telegram Desktop source tree at build time
by client/apply.py. It is distributed under the same license as
Telegram Desktop (GPLv3 with the OpenSSL exception).
*/
#include "tgws/tgws_controller.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "ui/boxes/confirm_box.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/painter.h"
#include "ui/qt_object_factory.h"
#include "ui/rp_widget.h"
#include "boxes/abstract_box.h"
#include "styles/style_window.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QRandomGenerator>

namespace TgWs {
namespace {

// Exit codes of the sidecar, keep in sync with sidecar/tgws_sidecar.py.
constexpr auto kExitSidecarUpdated = 75;
constexpr auto kExitTelegramUpdated = 76;

constexpr auto kButtonSize = 40;
constexpr auto kButtonBottomSkip = 16;

[[nodiscard]] QString SidecarName() {
#ifdef Q_OS_WIN
	return u"TgWsProxy.exe"_q;
#else // Q_OS_WIN
	return u"TgWsProxy"_q;
#endif // Q_OS_WIN
}

[[nodiscard]] bool IsHexSecret(const QString &value) {
	if (value.size() != 32) {
		return false;
	}
	for (const auto ch : value) {
		if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) {
			return false;
		}
	}
	return true;
}

} // namespace

void Controller::Start() {
	auto &controller = Instance();
	if (!controller._firstRun) {
		return;
	}
	controller._firstRun = false;
	auto &settings = Core::App().settings().proxy();
	const auto data = controller.proxyData();
	if (settings.indexInList(data) < 0) {
		settings.addToList(data);
	}
	if (!settings.isEnabled()) {
		settings.setSelected(data);
		settings.setSettings(MTP::ProxyData::Settings::Enabled);
		Core::App().saveSettingsDelayed();
	}
}

Controller &Controller::Instance() {
	static const auto instance = new Controller();
	return *instance;
}

Controller::Controller()
: QObject(QCoreApplication::instance()) {
	_restartTimer.setSingleShot(true);
	connect(&_restartTimer, &QTimer::timeout, this, [=] { start(); });
	connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [=] {
		_quitting = true;
		stop();
	});
	loadSecret();
	start();
}

QString Controller::dataDir() const {
	return cWorkingDir() + u"tgws/"_q;
}

QString Controller::sidecarPath() const {
	return cExeDir() + u"tgws/"_q + SidecarName();
}

MTP::ProxyData Controller::proxyData() const {
	auto result = MTP::ProxyData();
	result.type = MTP::ProxyData::Type::Mtproto;
	result.host = u"127.0.0.1"_q;
	result.port = kPort;
	result.password = u"dd"_q + _secret;
	return result;
}

void Controller::loadSecret() {
	QDir().mkpath(dataDir());
	auto file = QFile(dataDir() + u"secret"_q);
	if (file.open(QIODevice::ReadOnly)) {
		const auto value = QString::fromLatin1(file.readAll()).trimmed();
		file.close();
		if (IsHexSecret(value)) {
			_secret = value;
			return;
		}
	}
	_firstRun = true;
	auto bytes = QByteArray(16, Qt::Uninitialized);
	for (auto &byte : bytes) {
		byte = char(QRandomGenerator::system()->bounded(256));
	}
	_secret = QString::fromLatin1(bytes.toHex());
	if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		file.write(_secret.toLatin1());
		file.close();
	}
}

bool Controller::enabled() const {
	const auto &settings = Core::App().settings().proxy();
	return settings.isEnabled() && (settings.selected() == proxyData());
}

rpl::producer<bool> Controller::enabledValue() const {
	return Core::App().settings().proxy().connectionTypeValue(
	) | rpl::map([=] {
		return enabled();
	});
}

bool Controller::running() const {
	return _process && (_process->state() == QProcess::Running);
}

void Controller::toggle() {
	auto &settings = Core::App().settings().proxy();
	const auto data = proxyData();
	if (enabled()) {
		Core::App().setCurrentProxy(data, MTP::ProxyData::Settings::System);
	} else {
		if (settings.indexInList(data) < 0) {
			settings.addToList(data);
		}
		Core::App().setCurrentProxy(data, MTP::ProxyData::Settings::Enabled);
		if (!running()) {
			_failures = 0;
			start();
		}
	}
	Core::App().saveSettingsDelayed();
}

void Controller::start() {
	if (_quitting || running()) {
		return;
	}
	const auto program = sidecarPath();
	if (!QFile::exists(program)) {
		LOG(("TgWs Error: sidecar not found at '%1'.").arg(program));
		return;
	}
	if (!_process) {
		_process = new QProcess(this);
		_process->setStandardOutputFile(QProcess::nullDevice());
		_process->setStandardErrorFile(QProcess::nullDevice());
		connect(
			_process,
			qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
			this,
			[=](int exitCode, QProcess::ExitStatus status) {
				finished((status == QProcess::NormalExit) ? exitCode : -1);
			});
	}
	_process->setWorkingDirectory(cExeDir() + u"tgws"_q);
	_process->start(program, {
		u"--port"_q,
		QString::number(kPort),
		u"--secret"_q,
		_secret,
		u"--log-file"_q,
		dataDir() + u"proxy.log"_q,
		u"--tgws-parent-pid"_q,
		QString::number(QCoreApplication::applicationPid()),
		u"--tgws-app-dir"_q,
		cExeDir(),
		u"--tgws-data-dir"_q,
		dataDir(),
	});
}

void Controller::stop() {
	_restartTimer.stop();
	if (running()) {
		_process->kill();
		_process->waitForFinished(2000);
	}
}

void Controller::finished(int exitCode) {
	if (_quitting) {
		return;
	}
	if (exitCode == kExitSidecarUpdated || exitCode == kExitTelegramUpdated) {
		// The sidecar downloaded a new version of itself next to the old
		// one, it is safe to swap the files only now that it has exited.
		const auto current = sidecarPath();
		const auto fresh = current + u".new"_q;
		if (QFile::exists(fresh)) {
			QFile::remove(current);
			if (!QFile::rename(fresh, current)) {
				LOG(("TgWs Error: could not install '%1'.").arg(fresh));
			}
		}
		_failures = 0;
		if (exitCode == kExitTelegramUpdated) {
			promptTelegramRestart();
		}
		start();
		return;
	}
	LOG(("TgWs Error: sidecar exited with code %1.").arg(exitCode));
	scheduleRestart();
}

void Controller::scheduleRestart() {
	const auto delay = std::min(2000 << std::min(_failures, 5), 60000);
	++_failures;
	_restartTimer.start(delay);
}

void Controller::promptTelegramRestart() {
	if (_restartPrompted) {
		return;
	}
	_restartPrompted = true;
	Ui::show(Ui::MakeConfirmBox({
		.text = u"Установлено обновление Telegram. "
			"Перезапустить сейчас, чтобы применить его?"_q,
		.confirmed = [] { Core::Restart(); },
		.confirmText = u"Перезапустить"_q,
		.cancelText = u"Позже"_q,
	}));
}

void SetupDialogsButton(not_null<Ui::RpWidget*> parent) {
	auto &controller = Controller::Instance();
	const auto button = Ui::CreateChild<Ui::AbstractButton>(parent.get());
	button->resize(kButtonSize, kButtonSize);
	button->setPointerCursor(true);

	const auto enabled = button->lifetime().make_state<rpl::variable<bool>>(
		controller.enabledValue());
	enabled->value() | rpl::on_next([=](bool value) {
		button->setAccessibleName(value
			? u"Выключить TG WS Proxy"_q
			: u"Включить TG WS Proxy"_q);
		button->update();
	}, button->lifetime());

	button->setClickedCallback([=] {
		auto &instance = Controller::Instance();
		instance.toggle();
		const auto on = instance.enabled();
		Ui::Toast::Show(button->window(), on
			? (instance.running()
				? u"TG WS Proxy включён"_q
				: u"TG WS Proxy запускается…"_q)
			: u"TG WS Proxy выключен"_q);
	});

	button->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(button);
		auto hq = PainterHighQualityEnabler(p);
		// Same look as Telegram's own "Connecting / proxy" bubble:
		// a plain round button with a soft shadow, no borders.
		const auto rect = QRectF(button->rect()).marginsRemoved(
			{ 2., 1., 2., 3. });
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowShadowFg);
		p.drawEllipse(rect.translated(0., 1.));
		p.setBrush(button->isOver() ? st::windowBgOver : st::windowBg);
		p.drawEllipse(rect);
		const auto &icon = enabled->current()
			? st::connectingProxyOn
			: st::connectingProxyOff;
		icon.paintInCenter(p, rect.toRect());
	}, button->lifetime());

	parent->sizeValue() | rpl::on_next([=](QSize size) {
		button->move(
			(size.width() - button->width()) / 2,
			size.height() - button->height() - kButtonBottomSkip);
		button->raise();
	}, button->lifetime());
	button->show();
}

} // namespace TgWs
