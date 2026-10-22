// Example mod for Client-tg, see docs/modding.md.
#include <tgmod/tgmod.hpp>

#include <string>

namespace {

std::string ReplaceAll(std::string text, const std::string &from, const std::string &to) {
	for (auto position = text.find(from); position != std::string::npos;) {
		text.replace(position, from.size(), to);
		position = text.find(from, position + to.size());
	}
	return text;
}

} // namespace

class HelloMod final : public tgmod::Mod {
public:
	// Construct phase: subscribe to events here.
	explicit HelloMod(tgmod::Context &context) : Mod(context) {
		// Lowest priority: runs after the other mods changed the text.
		context.on<tgmod::MessageSendEvent>([this](auto &event) {
			if (_arrows) {
				event.setText(ReplaceAll(event.text(), "->", "→"));
			}
		}, tgmod::Priority::Lowest);

		context.on<tgmod::MessageReceivedEvent>([this](auto &event) {
			const auto message = event.message();
			if (!message.outgoing()) {
				++_received;
			}
		});

		context.on<tgmod::ImcEvent>([this](auto &event) {
			this->context().info("IMC from " + event.sender()
				+ ": " + event.method());
		});
	}

	// Common setup: config and registries.
	void commonSetup() override {
		auto &config = context().config();
		_arrows = config.getBool("replaceArrows", true);
		_greeting = config.getString("greeting", "Привет");

		context().command("shrug", "¯\\_(ツ)_/¯ с текстом", [](auto &command) {
			const auto args = command.args();
			command.reply(args.empty()
				? std::string("¯\\_(ツ)_/¯")
				: args + " ¯\\_(ツ)_/¯");
			return true;
		});
		context().command("hello", "Поздороваться", [this](auto &command) {
			command.reply(_greeting + ", " + (command.args().empty()
				? std::string("мир")
				: command.args()) + "!");
			return true;
		});

		context().mainMenuAction("Hello: статистика", [this](uint64_t) {
			context().toast("Входящих с запуска: "
				+ std::to_string(_received));
		});
		context().messageAction("Hello: длина текста", [this](
				const tgmod::Message &message) {
			context().toast("Символов (байт UTF-8): "
				+ std::to_string(message.text().size()));
		});
	}

	void loadComplete() override {
		context().info("Hello loaded, folder: " + context().modDir());
	}

private:
	bool _arrows = true;
	std::string _greeting;
	uint64_t _received = 0;

};

TGMOD_MOD(HelloMod)
