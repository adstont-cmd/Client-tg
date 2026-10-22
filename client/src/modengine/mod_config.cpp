/*
Client-tg: mod engine.

This file is injected into the Telegram Desktop source tree at build time
by client/apply.py. It is distributed under the same license as
Telegram Desktop (GPLv3 with the OpenSSL exception).
*/
#include "modengine/mod_config.h"

#include "logs.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QSaveFile>

namespace ModEngine {
namespace {

constexpr auto kSaveDelay = 1000;

} // namespace

JsonConfig::JsonConfig(QString path) : _path(std::move(path)) {
	_saveTimer.setSingleShot(true);
	_saveTimer.setInterval(kSaveDelay);
	QObject::connect(&_saveTimer, &QTimer::timeout, [=] { saveNow(); });
	load();
}

JsonConfig::~JsonConfig() {
	saveNow();
}

void JsonConfig::load() {
	auto file = QFile(_path);
	if (!file.exists()) {
		return;
	} else if (!file.open(QIODevice::ReadOnly)) {
		LOG(("Mods Error: could not read %1.").arg(_path));
		return;
	}
	auto error = QJsonParseError();
	const auto document = QJsonDocument::fromJson(file.readAll(), &error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) {
		// Keep the broken file for the user to fix, start from defaults.
		LOG(("Mods Error: bad JSON in %1: %2"
			).arg(_path, error.errorString()));
		QFile::remove(_path + u".broken"_q);
		QFile::copy(_path, _path + u".broken"_q);
		return;
	}
	_values = document.object();
}

QJsonValue JsonConfig::value(
		const QString &key,
		const QJsonValue &fallback,
		QJsonValue::Type type) {
	const auto i = _values.constFind(key);
	if (i != _values.constEnd() && i->type() == type) {
		return *i;
	}
	set(key, fallback);
	return fallback;
}

bool JsonConfig::getBool(const QString &key, bool fallback) {
	return value(key, fallback, QJsonValue::Bool).toBool();
}

int64 JsonConfig::getInt(const QString &key, int64 fallback) {
	return int64(value(key, double(fallback), QJsonValue::Double).toDouble());
}

QString JsonConfig::getString(const QString &key, const QString &fallback) {
	return value(key, fallback, QJsonValue::String).toString();
}

QStringList JsonConfig::getStringList(
		const QString &key,
		const QStringList &fallback) {
	const auto array = value(
		key,
		QJsonArray::fromStringList(fallback),
		QJsonValue::Array).toArray();
	auto result = QStringList();
	for (const auto &entry : array) {
		result.push_back(entry.toString());
	}
	return result;
}

void JsonConfig::set(const QString &key, const QJsonValue &value) {
	if (_values.value(key) == value) {
		return;
	}
	_values.insert(key, value);
	scheduleSave();
}

void JsonConfig::scheduleSave() {
	_dirty = true;
	_saveTimer.start();
}

void JsonConfig::saveNow() {
	_saveTimer.stop();
	if (!_dirty) {
		return;
	}
	_dirty = false;
	QDir().mkpath(QFileInfo(_path).absolutePath());
	auto file = QSaveFile(_path);
	if (!file.open(QIODevice::WriteOnly)
		|| file.write(QJsonDocument(_values).toJson()) < 0
		|| !file.commit()) {
		LOG(("Mods Error: could not write %1.").arg(_path));
	}
}

} // namespace ModEngine
