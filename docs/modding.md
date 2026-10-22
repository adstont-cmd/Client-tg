# Моды для Client-tg

В клиент встроен движок модов. Устроен он по образцу Forge/NeoForge из Minecraft: мод лежит в своей папке с описанием `mod.json`, загрузчик проверяет зависимости и порядок загрузки, мод проходит фазы жизненного цикла и подписывается на события через шину. Код и API свои, под Telegram.

## Как это устроено

```
modapi/include/tgmod/
  tgmod.h            C API: всё, что видит мод (один заголовок)
  tgmod.hpp          C++ обёртка: классы, лямбды, std::string
client/src/modengine/
  mod_version        версии и диапазоны версий ("[1.0,2.0)")
  mod_metadata       чтение mod.json
  mod_loader         поиск, проверка, сортировка, загрузка, фазы
  event_bus          шина событий: приоритеты, отмена
  mod_registry       команды, пункты меню, действия с сообщениями
  mod_config         конфиги модов в JSON
  mod_host_api       таблица функций tgmod_host для модов
  mod_engine         всё вместе, безопасный режим
  mod_hooks          точки входа, которые вызывает сам Telegram
  mods_box           окно «Моды» в главном меню
examples/mods/hello/ пример мода
```

| Forge / NeoForge | Client-tg |
|---|---|
| `mods.toml` | `mod.json` |
| `@Mod` и конструктор мода | `TGMOD_MOD(MyMod)` и конструктор `MyMod(Context&)` |
| mod event bus: `FMLCommonSetupEvent`, `FMLClientSetupEvent`, `FMLLoadCompleteEvent` | `commonSetup()`, `clientSetup()`, `loadComplete()`, плюс `shutdown()` |
| `MinecraftForge.EVENT_BUS`, `@SubscribeEvent` | `context.on<SomeEvent>(...)` |
| `EventPriority`, `ICancellableEvent`, `receiveCanceled` | `tgmod::Priority`, `event.cancel()`, флаг `receiveCanceled` |
| `ForgeConfigSpec` | `context.config()`, файл `mods/config/<id>.json` |
| `InterModComms` | `context.imcSend(...)` и `ImcEvent` |
| регистры (`DeferredRegister`) | `command(...)`, `mainMenuAction(...)`, `messageAction(...)` |
| экран ошибок загрузки и список модов | окно «Моды» в главном меню |
| Mixin | точки в `mod_hooks.h`, которые `apply.py` вставляет в tdesktop |

Почему моды собираются как нативные библиотеки (`.dll`, `.dylib`, `.so`), а граница проходит по C, а не по C++: так мод можно собрать любым компилятором, без Qt и без исходников Telegram, и он не сломается при обновлении клиента. C++ обёртка `tgmod.hpp` целиком в заголовке, она компилируется внутри мода.

Если в будущем понадобятся моды на скриптовом языке (Lua, JS), их можно сделать как обычный нативный мод, который сам загружает скрипты, по аналогии с language provider в Forge. Движок для этого менять не нужно.

## Где лежат моды

```
<папка Telegram>/
  mods/
    engine.json          настройки движка
    config/hello.json    конфиг мода hello
    hello/
      mod.json
      hello.dll
```

В портативной сборке это папка рядом с `Telegram.exe`. Окно «Моды» в главном меню показывает список модов, их состояние и ошибки, позволяет их включать и выключать и открывает папку. Изменения применяются после перезапуска, как в Forge.

`engine.json`:

```json
{
  "enabled": true,
  "commandPrefix": ".",
  "disabled": ["some_mod"]
}
```

**Безопасный режим.** Пока моды загружаются, в папке лежит файл `mods/.loading`. Если клиент упал на этом этапе, при следующем запуске моды не загружаются, а в окне «Моды» это написано. Выключите сломанный мод и перезапустите клиент.

## mod.json

```json
{
  "schema": 1,
  "id": "hello",
  "version": "1.0.0",
  "name": "Hello",
  "description": "Что делает мод",
  "authors": ["вы"],
  "license": "MIT",
  "homepage": "https://...",
  "apiVersion": "[1,2)",
  "entrypoints": {
    "windows": "hello.dll",
    "macos": "libhello.dylib",
    "linux": "libhello.so",
    "android": "libhello.so"
  },
  "dependencies": [
    { "id": "client", "versionRange": "[0.1,)" },
    { "id": "tdesktop", "versionRange": "[6.0,)" },
    { "id": "other_mod", "versionRange": "[2.0,3.0)", "type": "optional", "ordering": "after" },
    { "id": "evil_mod", "type": "incompatible" }
  ]
}
```

- `id`: от 2 до 64 символов `a-z`, `0-9`, `_`, первым идёт буква. Id `client` (движок модов) и `tdesktop` (версия Telegram Desktop) зарезервированы, от них можно только зависеть.
- `version`: `1.2.3`, допускается суффикс `-beta.1`.
- `apiVersion`: какие версии C API подходят моду. Сейчас версия API равна 1.
- Диапазоны версий записываются как в Forge (Maven): `*` означает любую версию, `1.2` означает 1.2 и новее, `[1.2]` означает ровно 1.2, `[1.0,2.0)` означает от 1.0 включительно до 2.0 не включая, `(,3]` означает не новее 3. Несколько диапазонов перечисляются через запятую.
- `type` зависимости: `required` (по умолчанию) означает, что без неё мод не загрузится. `optional` означает, что зависимость может отсутствовать, но если она есть, версия должна подходить. `incompatible` означает, что мод не загрузится вместе с ней.
- `ordering`: `after` (по умолчанию) настраивает мод после зависимости, `before` настраивает до неё, `none` не задаёт порядка.

Загрузчик отключает мод, если что-то не сходится: id занят, нет зависимости, не та версия, цикл в порядке загрузки, нет библиотеки или `tgmod_main` вернул ошибку. Моды, которым он нужен (`required`), отключаются вслед за ним. Остальной клиент продолжает работать, а причина видна в окне «Моды» и в логе Telegram (строки `Mods`).

## Жизненный цикл

1. **Construct.** Библиотека загружена, вызван `tgmod_main`, то есть конструктор класса мода. Здесь мод подписывается на события.
2. **COMMON_SETUP.** Мод регистрирует команды, пункты меню и читает конфиг.
3. **CLIENT_SETUP.** Окна Telegram уже созданы.
4. **LOAD_COMPLETE.** Все моды прошли CLIENT_SETUP, можно общаться с другими модами.
5. **SHUTDOWN.** Клиент закрывается.

События жизненного цикла приходят только тому моду, который сейчас настраивается, по очереди в порядке загрузки. Моды не выгружаются на лету, все изменения применяются после перезапуска.

## События

| Событие | Когда | Отмена | Что можно менять |
|---|---|---|---|
| `SessionStartedEvent` | аккаунт загружен или выполнен вход | нет | |
| `SessionEndedEvent` | аккаунт закрыт | нет | |
| `MessageSendEvent` | перед отправкой текстового сообщения | да, сообщение не уйдёт | текст через `setText` |
| `MessageReceivedEvent` | новое сообщение в любом чате, включая свои | нет | |
| `MessageDeletedEvent` | сообщение пропало из клиента (удалено или выгружено) | нет | |
| `ChatOpenedEvent` | пользователь открыл чат | нет | |
| `ImcEvent` | другой мод прислал сообщение | нет | |

Порядок слушателей такой: сначала по приоритету (`Highest` … `Lowest`), внутри одного приоритета по порядку загрузки модов. Если событие отменено, следующие слушатели его не получают, кроме подписанных с `receiveCanceled = true`. Если мод меняет текст в `MessageSendEvent`, форматирование сообщения (жирный, ссылки) сбрасывается.

У событий с сообщениями есть `sessionId` (аккаунт, `Main::Session::uniqueId()`), `peerId` (чат, `PeerId::value`), id сообщения, отправитель, дата и текст.

## Команды

Мод регистрирует команду `name`. Пользователь пишет в любом чате `.name аргументы`, и вместо отправки вызывается обработчик мода. Если обработчик вернул `false`, текст уходит как обычное сообщение. Префикс `.` выбран, чтобы не пересекаться с командами ботов `/`. Его можно поменять в `engine.json`.

## Пример

Полный пример лежит в [`examples/mods/hello`](../examples/mods/hello). Коротко:

```cpp
#include <tgmod/tgmod.hpp>

class MyMod final : public tgmod::Mod {
public:
	explicit MyMod(tgmod::Context &context) : Mod(context) {
		context.on<tgmod::MessageSendEvent>([](auto &event) {
			if (event.text() == "секрет") {
				event.cancel();
			}
		}, tgmod::Priority::High);
	}

	void commonSetup() override {
		context().command("ping", "Ответить pong", [](auto &command) {
			command.reply("pong");
			return true;
		});
		context().messageAction("Скопировать id", [this](const auto &message) {
			context().toast(std::to_string(message.id()));
		});
	}
};

TGMOD_MOD(MyMod)
```

Сборка:

```
cd examples/mods/hello
cmake -B build
cmake --build build --config Release
```

Затем `mod.json` и собранную библиотеку нужно положить в `<папка Telegram>/mods/hello/`.

Правила для авторов модов:

- Все колбэки вызываются в главном потоке. Если вы запускаете свои потоки, не вызывайте из них функции `tgmod_host`.
- Строки передаются в UTF-8 и живут только до возврата из колбэка.
- Исключения из колбэков `tgmod.hpp` ловит сам и пишет в лог. Падение внутри мода (обращение по нулевому указателю и т. п.) уронит клиент, от этого защищает только безопасный режим при следующем запуске.

## Как движок встроен в Telegram

`client/apply.py` копирует `client/src/modengine` и `modapi/include/tgmod` в исходники tdesktop и вставляет по одной строке в шесть мест:

| Файл tdesktop | Вызов |
|---|---|
| `core/application.cpp`, `Application::run()` | `Hooks::Start()` |
| `main/main_domain.cpp`, `Domain::watchSession()` | `Hooks::WatchAccount()` |
| `apiwrap.cpp`, `ApiWrap::sendMessage()` | `Hooks::BeforeSend()` |
| `window/window_session_controller.cpp`, конструктор | `Hooks::SessionControllerCreated()` |
| `window/window_main_menu.cpp`, `MainMenu::setupMenu()` | `Hooks::FillMainMenu()` |
| `history/history_inner_widget.cpp`, `HistoryInner::showContextMenu()` | `Hooks::FillMessageMenu()` |

Остальное движок получает через публичные `rpl` потоки tdesktop (новые сообщения, удаления, открытый чат), поэтому обновления Telegram его почти не задевают. Если апстрим всё же изменит одно из этих мест, `apply.py` остановит сборку с понятной ошибкой.

## Что дальше

- Больше событий: редактирование сообщений, реакции, входящие апдейты MTProto целиком.
- Свои разделы в настройках и кнопки в поле ввода.
- Хранение удалённых сообщений как отдельный мод поверх `MessageDeletedEvent`.
- Скриптовые моды (Lua или JS) как мод-загрузчик.
