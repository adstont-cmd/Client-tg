/*
Client-tg: mod engine.

This file is injected into the Telegram Desktop source tree at build time
by client/apply.py. It is distributed under the same license as
Telegram Desktop (GPLv3 with the OpenSSL exception).
*/
#pragma once

#include <QtCore/QString>

#include <optional>
#include <vector>

namespace ModEngine {

// "1.2.3", "1.2", "2.0.0-beta.1". Missing parts are zeros, a version with a
// pre-release suffix is lower than the same version without it.
class Version final {
public:
	Version() = default;

	[[nodiscard]] static std::optional<Version> Parse(const QString &text);

	[[nodiscard]] QString toString() const;
	[[nodiscard]] int compare(const Version &other) const;

	friend inline bool operator==(const Version &a, const Version &b) {
		return a.compare(b) == 0;
	}
	friend inline bool operator<(const Version &a, const Version &b) {
		return a.compare(b) < 0;
	}

private:
	std::vector<int> _parts;
	QString _preRelease;
	QString _text;

};

// Maven-style version range, the same syntax Forge uses in mods.toml:
//   "*" or ""        any version
//   "1.2"            1.2 or newer
//   "[1.2]"          exactly 1.2
//   "[1.0,2.0)"      1.0 <= v < 2.0
//   "(,3]"           v <= 3
//   "[1,2),[3,)"     unions of the above
class VersionRange final {
public:
	VersionRange() = default; // Any version.

	[[nodiscard]] static std::optional<VersionRange> Parse(
		const QString &text);

	[[nodiscard]] bool contains(const Version &version) const;
	[[nodiscard]] QString toString() const;

private:
	struct Bound {
		std::optional<Version> version;
		bool inclusive = true;
	};
	struct Interval {
		Bound lower;
		Bound upper;
	};

	std::vector<Interval> _intervals; // Empty means any version.
	QString _text = u"*"_q;

};

} // namespace ModEngine
