/*
Client-tg: mod engine.

This file is injected into the Telegram Desktop source tree at build time
by client/apply.py. It is distributed under the same license as
Telegram Desktop (GPLv3 with the OpenSSL exception).
*/
#include "modengine/mods_box.h"

#include "modengine/mod_engine.h"
#include "core/application.h"
#include "settings/settings_common.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

#include <QtCore/QUrl>
#include <QtGui/QDesktopServices>

namespace ModEngine {
namespace {

constexpr auto kLogLinesShown = 5;

[[nodiscard]] QString StateText(not_null<const ModContainer*> mod) {
	switch (mod->state) {
	case ModState::Disabled: return u"выключен"_q;
	case ModState::Failed: return u"ошибка"_q;
	case ModState::Discovered: return u"не загружен"_q;
	case ModState::Loaded: return u"работает"_q;
	default: return u"загружается (%1)"_q.arg(ModStateName(mod->state));
	}
}

[[nodiscard]] QString Details(not_null<const ModContainer*> mod) {
	auto lines = QStringList();
	lines.push_back(u"Состояние: "_q + StateText(mod));
	if (const auto &metadata = mod->metadata) {
		lines.push_back(u"id: "_q + metadata->id);
		if (!metadata->description.isEmpty()) {
			lines.push_back(metadata->description);
		}
		if (!metadata->authors.isEmpty()) {
			lines.push_back(u"Авторы: "_q + metadata->authors.join(u", "_q));
		}
		if (!metadata->homepage.isEmpty()) {
			lines.push_back(metadata->homepage);
		}
	} else {
		lines.push_back(u"Папка: "_q + mod->folder);
	}
	for (const auto &error : mod->errors) {
		lines.push_back(u"Ошибка: "_q + error);
	}
	const auto from = std::max(int(mod->log.size()) - kLogLinesShown, 0);
	for (auto i = from; i < mod->log.size(); ++i) {
		lines.push_back(mod->log[i]);
	}
	return lines.join('\n');
}

} // namespace

void ModsBox(not_null<Ui::GenericBox*> box) {
	auto &engine = Engine::Instance();
	box->setTitle(rpl::single(u"Моды"_q));
	box->setWidth(st::boxWideWidth);

	const auto label = [&](const QString &text) {
		box->addRow(object_ptr<Ui::FlatLabel>(box, text, st::boxLabel));
	};

	label(engine.clientVersionText());
	if (engine.safeMode()) {
		label(u"Прошлый запуск упал при загрузке модов, поэтому сейчас "
			"они не загружены. Выключите сломанный мод и перезапустите "
			"Telegram."_q);
	}

	const auto &mods = engine.loader().mods();
	if (mods.empty()) {
		label(u"Модов пока нет. Положите папку мода (с файлом mod.json) "
			"сюда:\n"_q + engine.modsDir());
	}

	const auto content = box->verticalLayout();
	const auto changed = std::make_shared<bool>(false);
	for (const auto &owned : mods) {
		const auto mod = owned.get();
		const auto title = mod->metadata
			? (mod->metadata->name + ' ' + mod->metadata->version.toString())
			: mod->id();
		if (mod->metadata) {
			const auto id = mod->metadata->id;
			Settings::AddButtonWithIcon(
				content,
				rpl::single(title),
				st::settingsButtonNoIcon
			)->toggleOn(
				rpl::single(engine.modEnabled(id))
			)->toggledChanges(
			) | rpl::on_next([=](bool enabled) {
				Engine::Instance().setModEnabled(id, enabled);
				if (!*changed) {
					*changed = true;
					box->showToast(u"Изменения применятся после "
						"перезапуска."_q);
				}
			}, box->lifetime());
		} else {
			label(title);
		}
		label(Details(mod));
	}

	box->addButton(rpl::single(u"Закрыть"_q), [=] { box->closeBox(); });
	box->addLeftButton(rpl::single(u"Папка модов"_q), [=] {
		QDesktopServices::openUrl(
			QUrl::fromLocalFile(Engine::Instance().modsDir()));
	});
	box->addLeftButton(rpl::single(u"Перезапустить"_q), [] {
		Core::Restart();
	});
}

} // namespace ModEngine
