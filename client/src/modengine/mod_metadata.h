/*
Client-tg: mod engine.

This file is injected into the Telegram Desktop source tree at build time
by client/apply.py. It is distributed under the same license as
Telegram Desktop (GPLv3 with the OpenSSL exception).
*/
#pragma once

#include "modengine/mod_version.h"

#include <QtCore/QStringList>

namespace ModEngine {

// Reserved ids mods can depend on to require a minimal version: the mod
// engine of Client-tg and Telegram Desktop itself. The C api version is
// checked separately, through "apiVersion".
inline constexpr auto kClientModId = "client";
inline constexpr auto kTelegramModId = "tdesktop";

[[nodiscard]] bool IsReservedModId(const QString &id);

enum class DependencyType {
	Required, // Not loaded without it.
	Optional, // Loaded without it, but the version must match if present.
	Incompatible, // Not loaded together with it.
};

enum class DependencyOrdering {
	None,
	Before, // This mod is set up before the dependency.
	After, // This mod is set up after the dependency (default for required).
};

struct Dependency {
	QString id;
	VersionRange versionRange;
	DependencyType type = DependencyType::Required;
	DependencyOrdering ordering = DependencyOrdering::After;
};

// mods/<folder>/mod.json, see docs/modding.md for the format.
struct ModMetadata {
	QString id;
	Version version;
	QString name;
	QString description;
	QStringList authors;
	QString license;
	QString homepage;
	VersionRange apiVersion;
	QString entrypoint; // Library file for the current platform.
	std::vector<Dependency> dependencies;
};

struct ParsedMetadata {
	std::optional<ModMetadata> metadata;
	QStringList errors;
};

[[nodiscard]] ParsedMetadata ParseModMetadata(const QByteArray &json);
[[nodiscard]] bool IsValidModId(const QString &id);

} // namespace ModEngine
