/*
Client-tg: mod engine.

This file is injected into the Telegram Desktop source tree at build time
by client/apply.py. It is distributed under the same license as
Telegram Desktop (GPLv3 with the OpenSSL exception).
*/
#pragma once

#include <QtCore/QJsonObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QTimer>

namespace ModEngine {

// A flat JSON object in a file. Missing keys are filled with the defaults
// the code asks for, so the file always lists every option. Writes are
// batched and also flushed on quit.
class JsonConfig final {
public:
	explicit JsonConfig(QString path);
	~JsonConfig();

	[[nodiscard]] bool getBool(const QString &key, bool fallback);
	[[nodiscard]] int64 getInt(const QString &key, int64 fallback);
	[[nodiscard]] QString getString(
		const QString &key,
		const QString &fallback);
	[[nodiscard]] QStringList getStringList(
		const QString &key,
		const QStringList &fallback);

	void set(const QString &key, const QJsonValue &value);

	void saveNow();

private:
	[[nodiscard]] QJsonValue value(
		const QString &key,
		const QJsonValue &fallback,
		QJsonValue::Type type);
	void load();
	void scheduleSave();

	QString _path;
	QJsonObject _values;
	QTimer _saveTimer;
	bool _dirty = false;

};

} // namespace ModEngine
