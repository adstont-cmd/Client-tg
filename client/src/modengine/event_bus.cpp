/*
Client-tg: mod engine.

This file is injected into the Telegram Desktop source tree at build time
by client/apply.py. It is distributed under the same license as
Telegram Desktop (GPLv3 with the OpenSSL exception).
*/
#include "modengine/event_bus.h"

#include "logs.h"

namespace ModEngine {

EventData::EventData(tgmod_event_type type, bool cancelable) {
	_raw.struct_size = sizeof(tgmod_event);
	_raw.type = type;
	_raw.flags = cancelable ? TGMOD_EVENT_FLAG_CANCELABLE : 0;
	_message.struct_size = sizeof(tgmod_message);
}

tgmod_event_type EventData::type() const {
	return tgmod_event_type(_raw.type);
}

tgmod_event *EventData::raw() {
	return &_raw;
}

bool EventData::canceled() const {
	return _raw.canceled != 0;
}

void EventData::cancel() {
	if (_raw.flags & TGMOD_EVENT_FLAG_CANCELABLE) {
		_raw.canceled = 1;
	}
}

void EventData::setSession(uint64 sessionId) {
	_raw.session_id = sessionId;
}

void EventData::setPeer(uint64 peerId) {
	_raw.peer_id = peerId;
}

void EventData::setMessage(MessageFields fields) {
	_fields = std::move(fields);
	_text = _fields.text.toUtf8();
	_message.session_id = _fields.sessionId;
	_message.peer_id = _fields.peerId;
	_message.message_id = _fields.messageId;
	_message.sender_id = _fields.senderId;
	_message.date = _fields.date;
	_message.outgoing = _fields.outgoing ? 1 : 0;
	_message.text = _text.constData();
	_raw.session_id = _fields.sessionId;
	_raw.peer_id = _fields.peerId;
	_raw.message = &_message;
}

const EventData::MessageFields &EventData::message() const {
	return _fields;
}

bool EventData::setText(const QString &text) {
	if (type() != TGMOD_EVENT_MESSAGE_SEND) {
		return false;
	} else if (_fields.text == text) {
		return true;
	}
	_fields.text = text;
	_text = text.toUtf8();
	_message.text = _text.constData();
	_textChanged = true;
	return true;
}

bool EventData::textChanged() const {
	return _textChanged;
}

void EventData::setImc(
		const QString &sender,
		const QString &method,
		const QString &payload) {
	_imcSender = sender.toUtf8();
	_imcMethod = method.toUtf8();
	_imcPayload = payload.toUtf8();
	_raw.imc_sender = _imcSender.constData();
	_raw.imc_method = _imcMethod.constData();
	_raw.imc_payload = _imcPayload.constData();
}

uint64 EventBus::subscribe(
		tgmod_event_type type,
		tgmod_priority priority,
		bool receiveCanceled,
		const QString &owner,
		int ownerOrder,
		Listener listener) {
	if (priority < TGMOD_PRIORITY_HIGHEST
		|| priority > TGMOD_PRIORITY_LOWEST
		|| !listener) {
		return 0;
	}
	auto subscription = Subscription{
		.id = ++_lastId,
		.type = type,
		.priority = priority,
		.receiveCanceled = receiveCanceled,
		.owner = owner,
		.ownerOrder = ownerOrder,
		.listener = std::make_shared<Listener>(std::move(listener)),
	};
	const auto before = [](const Subscription &a, const Subscription &b) {
		return std::tie(a.priority, a.ownerOrder, a.id)
			< std::tie(b.priority, b.ownerOrder, b.id);
	};
	const auto position = std::upper_bound(
		begin(_subscriptions),
		end(_subscriptions),
		subscription,
		before);
	_subscriptions.insert(position, std::move(subscription));
	return _lastId;
}

void EventBus::unsubscribe(uint64 id, const QString &owner) {
	_subscriptions.erase(
		std::remove_if(
			begin(_subscriptions),
			end(_subscriptions),
			[&](const Subscription &s) {
				return (s.id == id) && (s.owner == owner);
			}),
		end(_subscriptions));
}

void EventBus::unsubscribeAll(const QString &owner) {
	_subscriptions.erase(
		std::remove_if(
			begin(_subscriptions),
			end(_subscriptions),
			[&](const Subscription &s) { return s.owner == owner; }),
		end(_subscriptions));
}

bool EventBus::post(EventData &event, const QString &onlyOwner) {
	// Listeners may subscribe or unsubscribe while we deliver, so walk a
	// snapshot. The listeners themselves are shared, not copied.
	auto targets = std::vector<Subscription>();
	for (const auto &subscription : _subscriptions) {
		if (subscription.type == event.type()
			&& (onlyOwner.isEmpty() || subscription.owner == onlyOwner)) {
			targets.push_back(subscription);
		}
	}
	_posting.push_back(&event);
	for (const auto &subscription : targets) {
		if (event.canceled() && !subscription.receiveCanceled) {
			continue;
		}
		try {
			(*subscription.listener)(event);
		} catch (const std::exception &e) {
			LOG(("Mods Error: listener of \"%1\" threw: %2"
				).arg(subscription.owner, QString::fromUtf8(e.what())));
		} catch (...) {
			LOG(("Mods Error: listener of \"%1\" threw."
				).arg(subscription.owner));
		}
	}
	_posting.pop_back();
	return event.canceled();
}

EventData *EventBus::posting(const tgmod_event *raw) const {
	for (const auto event : _posting) {
		if (event->raw() == raw) {
			return event;
		}
	}
	return nullptr;
}

} // namespace ModEngine
