/*
Client-tg: mod engine.

This file is injected into the Telegram Desktop source tree at build time
by client/apply.py. It is distributed under the same license as
Telegram Desktop (GPLv3 with the OpenSSL exception).
*/
#include "modengine/mod_version.h"

#include <QtCore/QRegularExpression>

namespace ModEngine {

std::optional<Version> Version::Parse(const QString &text) {
	static const auto regex = QRegularExpression(
		u"^(\\d+)(?:\\.(\\d+))*(?:-([0-9A-Za-z.\\-]+))?(?:\\+[0-9A-Za-z.\\-]+)?$"_q);
	const auto trimmed = text.trimmed();
	if (!regex.match(trimmed).hasMatch()) {
		return std::nullopt;
	}
	auto result = Version();
	result._text = trimmed;
	auto core = trimmed.section('+', 0, 0);
	const auto dash = core.indexOf('-');
	if (dash >= 0) {
		result._preRelease = core.mid(dash + 1);
		core = core.left(dash);
	}
	for (const auto &part : core.split('.')) {
		auto ok = false;
		const auto value = part.toInt(&ok);
		if (!ok || value < 0) {
			return std::nullopt;
		}
		result._parts.push_back(value);
	}
	while (result._parts.size() > 1 && result._parts.back() == 0) {
		result._parts.pop_back();
	}
	return result;
}

QString Version::toString() const {
	return _text;
}

int Version::compare(const Version &other) const {
	const auto count = std::max(_parts.size(), other._parts.size());
	for (auto i = size_t(0); i != count; ++i) {
		const auto a = (i < _parts.size()) ? _parts[i] : 0;
		const auto b = (i < other._parts.size()) ? other._parts[i] : 0;
		if (a != b) {
			return (a < b) ? -1 : 1;
		}
	}
	if (_preRelease == other._preRelease) {
		return 0;
	} else if (_preRelease.isEmpty()) {
		return 1;
	} else if (other._preRelease.isEmpty()) {
		return -1;
	}
	return (_preRelease < other._preRelease) ? -1 : 1;
}

std::optional<VersionRange> VersionRange::Parse(const QString &text) {
	auto result = VersionRange();
	const auto trimmed = QString(text).remove(' ');
	if (trimmed.isEmpty() || trimmed == u"*"_q) {
		return result;
	}
	result._text = trimmed;

	const auto first = trimmed[0];
	if (first != '[' && first != '(') {
		// A bare version is a minimum, like in Forge.
		const auto version = Version::Parse(trimmed);
		if (!version) {
			return std::nullopt;
		}
		result._intervals.push_back({ .lower = { *version, true } });
		return result;
	}

	auto position = 0;
	while (position < trimmed.size()) {
		const auto open = trimmed[position];
		if (open != '[' && open != '(') {
			return std::nullopt;
		}
		auto close = -1;
		for (auto i = position + 1; i < trimmed.size(); ++i) {
			if (trimmed[i] == ']' || trimmed[i] == ')') {
				close = i;
				break;
			}
		}
		if (close < 0) {
			return std::nullopt;
		}
		const auto body = trimmed.mid(position + 1, close - position - 1);
		const auto lowerInclusive = (open == '[');
		const auto upperInclusive = (trimmed[close] == ']');
		auto interval = Interval();
		const auto comma = body.indexOf(',');
		if (comma < 0) {
			// "[1.2]" is an exact version.
			const auto version = Version::Parse(body);
			if (!version || !lowerInclusive || !upperInclusive) {
				return std::nullopt;
			}
			interval.lower = { *version, true };
			interval.upper = { *version, true };
		} else {
			const auto lower = body.left(comma);
			const auto upper = body.mid(comma + 1);
			if (upper.contains(',')) {
				return std::nullopt;
			}
			if (!lower.isEmpty()) {
				const auto version = Version::Parse(lower);
				if (!version) {
					return std::nullopt;
				}
				interval.lower = { *version, lowerInclusive };
			}
			if (!upper.isEmpty()) {
				const auto version = Version::Parse(upper);
				if (!version) {
					return std::nullopt;
				}
				interval.upper = { *version, upperInclusive };
			}
		}
		result._intervals.push_back(std::move(interval));

		position = close + 1;
		if (position < trimmed.size()) {
			if (trimmed[position] != ',') {
				return std::nullopt;
			}
			++position;
		}
	}
	return result;
}

bool VersionRange::contains(const Version &version) const {
	if (_intervals.empty()) {
		return true;
	}
	for (const auto &interval : _intervals) {
		if (const auto &lower = interval.lower.version) {
			const auto compared = version.compare(*lower);
			if (compared < 0 || (compared == 0 && !interval.lower.inclusive)) {
				continue;
			}
		}
		if (const auto &upper = interval.upper.version) {
			const auto compared = version.compare(*upper);
			if (compared > 0 || (compared == 0 && !interval.upper.inclusive)) {
				continue;
			}
		}
		return true;
	}
	return false;
}

QString VersionRange::toString() const {
	return _text;
}

} // namespace ModEngine
