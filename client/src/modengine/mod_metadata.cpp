/*
Client-tg: mod engine.

This file is injected into the Telegram Desktop source tree at build time
by client/apply.py. It is distributed under the same license as
Telegram Desktop (GPLv3 with the OpenSSL exception).
*/
#include "modengine/mod_metadata.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>

namespace ModEngine {
namespace {

constexpr auto kSchema = 1;

[[nodiscard]] QString PlatformKey() {
#if defined Q_OS_WIN
	return u"windows"_q;
#elif defined Q_OS_MAC // Q_OS_WIN
	return u"macos"_q;
#elif defined Q_OS_ANDROID // Q_OS_WIN || Q_OS_MAC
	return u"android"_q;
#else // Q_OS_WIN || Q_OS_MAC || Q_OS_ANDROID
	return u"linux"_q;
#endif // Q_OS_WIN || Q_OS_MAC || Q_OS_ANDROID
}

[[nodiscard]] QStringList StringList(const QJsonValue &value) {
	auto result = QStringList();
	if (value.isString()) {
		result.push_back(value.toString());
	} else {
		for (const auto &entry : value.toArray()) {
			if (entry.isString()) {
				result.push_back(entry.toString());
			}
		}
	}
	return result;
}

} // namespace

bool IsValidModId(const QString &id) {
	static const auto regex = QRegularExpression(u"^[a-z][a-z0-9_]{1,63}$"_q);
	return regex.match(id).hasMatch();
}

bool IsReservedModId(const QString &id) {
	return (id == QString::fromLatin1(kClientModId))
		|| (id == QString::fromLatin1(kTelegramModId));
}

ParsedMetadata ParseModMetadata(const QByteArray &json) {
	auto result = ParsedMetadata();
	auto &errors = result.errors;

	auto error = QJsonParseError();
	const auto document = QJsonDocument::fromJson(json, &error);
	if (error.error != QJsonParseError::NoError) {
		errors.push_back(u"mod.json: %1 at offset %2."_q
			.arg(error.errorString())
			.arg(error.offset));
		return result;
	} else if (!document.isObject()) {
		errors.push_back(u"mod.json: the root must be an object."_q);
		return result;
	}
	const auto root = document.object();

	const auto schema = root.value(u"schema"_q).toInt(kSchema);
	if (schema != kSchema) {
		errors.push_back(u"Unsupported mod.json schema %1."_q.arg(schema));
		return result;
	}

	auto metadata = ModMetadata();
	metadata.id = root.value(u"id"_q).toString();
	if (!IsValidModId(metadata.id)) {
		errors.push_back(u"Bad id \"%1\": use 2-64 characters a-z, 0-9 "
			"and _, starting with a letter."_q.arg(metadata.id));
	} else if (IsReservedModId(metadata.id)) {
		errors.push_back(u"The id \"%1\" is reserved."_q.arg(metadata.id));
	}

	const auto versionText = root.value(u"version"_q).toString();
	if (const auto version = Version::Parse(versionText)) {
		metadata.version = *version;
	} else {
		errors.push_back(u"Bad version \"%1\"."_q.arg(versionText));
	}

	metadata.name = root.value(u"name"_q).toString(metadata.id);
	metadata.description = root.value(u"description"_q).toString();
	metadata.authors = StringList(root.value(u"authors"_q));
	metadata.license = root.value(u"license"_q).toString();
	metadata.homepage = root.value(u"homepage"_q).toString();

	const auto apiText = root.value(u"apiVersion"_q).toString();
	if (const auto range = VersionRange::Parse(apiText)) {
		metadata.apiVersion = *range;
	} else {
		errors.push_back(u"Bad apiVersion range \"%1\"."_q.arg(apiText));
	}

	const auto entrypoints = root.value(u"entrypoints"_q);
	metadata.entrypoint = entrypoints.isString()
		? entrypoints.toString()
		: entrypoints.toObject().value(PlatformKey()).toString();
	if (metadata.entrypoint.isEmpty()) {
		errors.push_back(u"No entrypoint for platform \"%1\"."_q
			.arg(PlatformKey()));
	} else if (metadata.entrypoint.contains(u".."_q)) {
		errors.push_back(u"The entrypoint must be inside the mod folder."_q);
	}

	for (const auto &value : root.value(u"dependencies"_q).toArray()) {
		const auto object = value.toObject();
		auto dependency = Dependency();
		dependency.id = object.value(u"id"_q).toString();
		if (!IsReservedModId(dependency.id)
			&& !IsValidModId(dependency.id)) {
			errors.push_back(u"Bad dependency id \"%1\"."_q
				.arg(dependency.id));
			continue;
		}
		const auto rangeText = object.value(u"versionRange"_q).toString();
		if (const auto range = VersionRange::Parse(rangeText)) {
			dependency.versionRange = *range;
		} else {
			errors.push_back(u"Bad versionRange \"%1\" of \"%2\"."_q
				.arg(rangeText, dependency.id));
			continue;
		}
		const auto type = object.value(u"type"_q).toString(u"required"_q);
		if (type == u"required"_q) {
			dependency.type = DependencyType::Required;
		} else if (type == u"optional"_q) {
			dependency.type = DependencyType::Optional;
		} else if (type == u"incompatible"_q) {
			dependency.type = DependencyType::Incompatible;
		} else {
			errors.push_back(u"Bad dependency type \"%1\" of \"%2\"."_q
				.arg(type, dependency.id));
			continue;
		}
		const auto ordering = object.value(u"ordering"_q).toString(
			(dependency.type == DependencyType::Incompatible)
				? u"none"_q
				: u"after"_q);
		if (ordering == u"none"_q) {
			dependency.ordering = DependencyOrdering::None;
		} else if (ordering == u"before"_q) {
			dependency.ordering = DependencyOrdering::Before;
		} else if (ordering == u"after"_q) {
			dependency.ordering = DependencyOrdering::After;
		} else {
			errors.push_back(u"Bad ordering \"%1\" of \"%2\"."_q
				.arg(ordering, dependency.id));
			continue;
		}
		metadata.dependencies.push_back(std::move(dependency));
	}

	if (errors.isEmpty()) {
		result.metadata = std::move(metadata);
	}
	return result;
}

} // namespace ModEngine
