/*
Client-tg: mod engine.

This file is injected into the Telegram Desktop source tree at build time
by client/apply.py. It is distributed under the same license as
Telegram Desktop (GPLv3 with the OpenSSL exception).
*/
#pragma once

#include "tgmod/tgmod.h"

#include <QtCore/QByteArray>
#include <QtCore/QString>

namespace ModEngine {

// One event being posted. Owns every string the C structs point to, so a
// listener (mod or engine) can change the text and the next listeners see
// the new value.
class EventData final {
public:
	explicit EventData(tgmod_event_type type, bool cancelable = false);
	EventData(const EventData &) = delete;
	EventData &operator=(const EventData &) = delete;

	[[nodiscard]] tgmod_event_type type() const;
	[[nodiscard]] tgmod_event *raw();
	[[nodiscard]] bool canceled() const;
	void cancel();

	void setSession(uint64 sessionId);
	void setPeer(uint64 peerId);

	struct MessageFields {
		uint64 sessionId = 0;
		uint64 peerId = 0;
		int64 messageId = 0;
		uint64 senderId = 0;
		int64 date = 0;
		bool outgoing = false;
		QString text;
	};
	void setMessage(MessageFields fields);
	[[nodiscard]] const MessageFields &message() const;

	// The text of the message being sent (MESSAGE_SEND).
	bool setText(const QString &text);
	[[nodiscard]] bool textChanged() const;

	void setImc(
		const QString &sender,
		const QString &method,
		const QString &payload);

private:
	tgmod_event _raw = {};
	tgmod_message _message = {};
	MessageFields _fields;
	QByteArray _text;
	QByteArray _imcSender;
	QByteArray _imcMethod;
	QByteArray _imcPayload;
	bool _textChanged = false;

};

// Order of listeners: priority first, then load order of the owner (the
// engine itself before every mod), then subscription order.
class EventBus final {
public:
	using Listener = Fn<void(EventData&)>;

	// owner is a mod id, or empty for the engine. ownerOrder is its index
	// in the load order (-1 for the engine).
	uint64 subscribe(
		tgmod_event_type type,
		tgmod_priority priority,
		bool receiveCanceled,
		const QString &owner,
		int ownerOrder,
		Listener listener);
	void unsubscribe(uint64 id, const QString &owner);
	void unsubscribeAll(const QString &owner);

	// Delivers to every listener, or only to the listeners of onlyOwner
	// (lifecycle and IMC events). Returns true if the event was canceled.
	bool post(EventData &event, const QString &onlyOwner = QString());

	// The event currently being delivered with this raw pointer, if any:
	// the C api gets only tgmod_event* back from the mods.
	[[nodiscard]] EventData *posting(const tgmod_event *raw) const;

private:
	struct Subscription {
		uint64 id = 0;
		tgmod_event_type type = {};
		tgmod_priority priority = TGMOD_PRIORITY_NORMAL;
		bool receiveCanceled = false;
		QString owner;
		int ownerOrder = -1;
		std::shared_ptr<Listener> listener;
	};

	std::vector<Subscription> _subscriptions; // Kept sorted.
	std::vector<EventData*> _posting;
	uint64 _lastId = 0;

};

} // namespace ModEngine
