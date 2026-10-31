# PackSeller 0.2.0 — автопродажа на животном для Kenshi

English summary is below / краткое описание на английском ниже: [English](#english).

> Статус: версия 0.2.0. Проверено в игре только то, что перечислено в разделе «Что проверено»; всё остальное перечислено в разделе «Не проверено».

## Что делает мод

Плагин для RE_Kenshi. В окне инвентаря животного (над кнопкой «ОТКРЫТЬ» / OPEN BAG, где открывается рюкзак) появляется кнопка «Торговцы». Она открывает окно с двумя вкладками (кнопки справа вверху окна):

- **«Торговцы»**: список торговцев, которых игра сейчас загрузила: имя, город, расстояние (с пометками «далеко» / «очень далеко»), деньги торговца и кнопка «Продать». Животное идёт к выбранному торговцу, продаёт товар из рюкзака и возвращается на точку, где стояло в момент нажатия.
- **«Города»** (новое в 0.2): список городов с фракцией, расстоянием и числом загруженных торговцев (если город ещё не загружен, стоит «?»). Кнопка «Продать» отправляет животное в выбранный город, в том числе за пределы загруженной области: животное подгружает её по пути, по прибытии само выбирает подходящего торговца (рядом, есть деньги, берёт товар), продаёт и возвращается.

Общее: к торговцу в здании животное подходит снаружи; деньги зачисляются вам, проданные предметы убираются из рюкзака; торговец в минус не уходит (продаётся только то, что он может оплатить); списки сортируются кликом по заголовку; торговцы без денег и те, кому нечего продать, скрываются; враждебные города и фракции помечаются и по умолчанию блокируются (`hostile_policy=block`); активный приказ сохраняется в сейве.

### Дальние походы

Список торговцев показывает тех, кого загрузила игра: это область вокруг персонажей игрока, не менее ~11000 единиц. Вкладка «Города» позволяет отправить животное в город за этой областью (подтверждено походом на ~13900 единиц). Перед стартом мод предупреждает: «далеко», «очень далеко» (по `list_far_distance` и `list_very_far_distance`) и «долгий путь» с оценкой времени в минутах («около 6 мин в одну сторону»). Ограничение `max_trip_distance` (по умолчанию 40000) отклоняет слишком далёкие цели.

ВНИМАНИЕ: дальний поход долгий и опасный (бандиты, хищники). Остановки при угрозе и обхода опасных мест НЕТ; животное с грузом может погибнуть. Делайте копию сейва и не отправляйте дорогой груз туда, куда не готовы его потерять.

## Требования

| Компонент | Версия | Статус проверки |
|---|---|---|
| Kenshi | 1.0.65 (Steam, x64) | подтверждено |
| RE_Kenshi | 0.3.5 | подтверждено |
| KenshiLib | 0.5.0 (поставляется с RE_Kenshi) | подтверждено |
| Kenshi 1.0.68 | | НЕ проверено |
| Kenshi GOG | | НЕ проверено |

Мод собран для KenshiLib 0.5.0 и может не работать с другой версией игры/RE_Kenshi. Библиотека времени выполнения VC++ 2010 (`MSVCR100.dll`) нужна плагину; игра и RE_Kenshi её используют, отдельная установка обычно не требуется.

## Установка

Через Steam Workshop: подпишитесь на мод и включите его в лаунчере (вкладка Mods). RE_Kenshi должен быть установлен.

Вручную:

1. Установите RE_Kenshi 0.3.5 и запустите игру через него хотя бы раз, чтобы убедиться, что он работает.
2. Распакуйте архив `PackSeller-0.2.0.zip` так, чтобы получилась папка `Kenshi\mods\PackSeller\` с файлами:
   - `PackSeller.dll`
   - `PackSeller.mod`
   - `RE_Kenshi.json`
   - `PackSeller.cfg`
3. Запустите игру через RE_Kenshi, в лаунчере откройте вкладку «Mods» и включите `PackSeller`.
4. Проверьте файл `RE_Kenshi_log.txt` в папке Kenshi: в нём должны быть строки `[PackSeller] PackSeller 0.2.0 (RE_Kenshi, KenshiLib 0.5.0)` и `[PackSeller] loaded`.

Обновление с 0.1.0: просто замените файлы. Формат записи сейва в 0.2 версии 2 (чтение сейвов версии 1 `[TO VERIFY: загрузка сейва с приказом 0.1 в 0.2 не проверялась]`); сейв, записанный с приказом версии 0.2, плагином 0.1.0 отклоняется записью `unsupported format version 2` `[TO VERIFY: проверка на 0.1.0 не выполнялась]`.

## Как пользоваться

1. Загрузите товар в рюкзак животного (рюкзак открывается кнопкой «ОТКРЫТЬ»).
2. Откройте окно инвентаря животного и нажмите «Торговцы» (над кнопкой «ОТКРЫТЬ»; с другими модами инвентаря положение может отличаться).
3. Вкладка «Торговцы»: выберите строку и нажмите «Продать». Вкладка «Города»: выберите город и нажмите «Продать». Заголовки столбцов сортируют список (активный столбец помечен «>»).
4. Животное уйдёт, продаст и вернётся. Сообщения о ходе и ошибках показываются в игре. Окно закрывается кнопкой, крестиком или Esc.

Одновременно работает один приказ. Повторное нажатие «Продать» во время приказа даёт сообщение «занято». Если вы сами отдадите животному другой приказ, мод отпустит его и не вернёт на старт. Переключение камеры или выделения в пути приказ не сбрасывает.

## Настройки в игре

В окне НАСТРОЙКИ (главное меню и Esc-меню) появилась вкладка «PackSeller»:

- режим отладки (подробный лог в `RE_Kenshi_log.txt`);
- язык мода: Русский / English. Русский требует русскую локаль игры (`language=ru_RU` в `settings.cfg`); иначе выбор отклоняется («Нужен русский язык игры»). По умолчанию язык выбирается автоматически.

Выбранное сохраняется в файле `PackSeller.user.cfg` рядом с DLL (он читается после `PackSeller.cfg` и не входит в архив). `PackSeller.cfg` мод не переписывает. Чтобы вернуть автоматический язык, удалите `PackSeller.user.cfg`.

## Настройки (`PackSeller.cfg`)

Файл лежит рядом с DLL, читается при запуске игры. Основные ключи (полный список с комментариями на русском и английском внутри файла):

| Ключ | По умолчанию в релизе | Смысл |
|---|---|---|
| `sell_dry_run` | 0 | 1 = только расчёт, ничего не меняется (безопасная проверка) |
| `order_enabled` | 1 | 1 = кнопка запускает цикл, 0 = кнопка только считает |
| `debug_log` | 0 | подробный лог; включайте для отчёта об ошибке |
| `debug_commands` | 0 | 1 = читать файл отладочных команд `PackSeller.cmd` (игрокам не нужно) |
| `ui_language` | auto | auto / ru / en |
| `hostile_policy` | block | торговцы враждебных городов/фракций: block (отклонять) / warn / allow |
| `list_radius` | 100000 | максимальное расстояние до города торговца в списке «Торговцы» (практически все загруженные) |
| `list_min_money` | 100 | скрывать торговцев с меньшей суммой |
| `list_sort` | distance | distance / money / town |
| `list_far_distance` | 3000 | дальше этого расстояния строка помечается «далеко» и показывается предупреждение |
| `list_very_far_distance` | 10000 | дальше — «очень далеко», предупреждение сильнее |
| `max_trip_distance` | 40000 | дальше этого цели не показываются, приказ отклоняется |
| `order_speed_estimate` | 40 | предполагаемая скорость животного (ед/с) для оценки времени и таймаута |
| `order_timeout_factor` | 2.5 | множитель таймаута: фаза ограничена max(`order_timeout_seconds`, расстояние / скорость × множитель) |
| `order_timeout_seconds` | 300 | минимальный таймаут одной фазы (путь туда / назад) |
| `persist_resume_seconds` | 10 | сколько секунд приказ ждёт объекты после короткого флага «загрузка» (например, при смене камеры), затем сбрасывается |
| `trade_range` | 150 | дистанция до торговца, с которой начинается продажа |
| `order_max_items` | 200 | максимум единиц предметов за один цикл |
| `save_enabled` | 1 | сохранять активный приказ в сейв |

## Известные ограничения версии 0.2

- Список «Торговцы» показывает только загруженных игрой торговцев (область вокруг персонажей игрока, не менее ~11000 единиц). Города за этой областью доступны на вкладке «Города», где число торговцев может быть «?» до прибытия.
- Дальний поход долгий и опасный; остановки при угрозе нет, животное с грузом может погибнуть. Максимальная дальность ограничена `max_trip_distance`.
- Одно животное и один приказ за раз; кнопки отмены нет (отменить можно, отдав животному свой приказ).
- Торговцы в зданиях недоступны напрямую: животное подходит снаружи на расстояние порядка `trade_range` и продаёт оттуда.
- Торговцы враждебных городов и фракций: по умолчанию (`hostile_policy=block`) приказ отклоняется с сообщением; такие города помечаются и в списке «Города» скрыты. Не проверено в игре на реальном враждебном городе.
- Кнопка появляется у любого вашего животного.
- Проданные предметы уничтожаются, на склад торговца они не переносятся.
- Цена считается по формуле плагина с множителем 1.0 и может отличаться от окна торговли примерно до 3%.
- Предметы, которые торговец не берёт (нелегальные, краденые, экипированные), должны остаться в рюкзаке, но это НЕ проверено в игре. Предметы выключенных секций инвентаря пропускаются.
- Торговцы услугами (например, хирург) могут попасть в список.
- Русский язык интерфейса мода требует русскую локаль игры; иначе используйте English.
- После восстановления приказа из сейва в части сообщений имя торговца может быть пустым (косметика).
- Не проверено на Kenshi 1.0.68 и на GOG-версии; игровые обновления могут сломать хуки.

## Удаление и отключение

- Выключите мод в лаунчере (или удалите папку `Kenshi\mods\PackSeller\`). Файл `PackSeller.user.cfg` (если он есть) можно удалить вручную.
- Сейвы: если в момент сохранения шёл приказ, в сейв пишется небольшая запись мода (тип 20717, `PackSeller_orders`). Если приказа не было, запись не создаётся и сейв неотличим от сейва без мода. Загрузка сейва с такой записью при ОТКЛЮЧЁННОМ моде проверена один раз на Kenshi 1.0.65 (версия мода 0.1.0): игра загрузилась без вылета. Это единичная проверка, поэтому перед удалением мода сделайте копию сейва и не сохраняйтесь во время приказа.
- Старые сейвы без мода загружаются как обычно (проверено).
- Обновление мода через Steam Workshop может удалить `PackSeller.user.cfg` (не проверено): тогда настройки языка и отладки из меню придётся выбрать заново.

## Сообщить об ошибке

Приложите: версию Kenshi, RE_Kenshi и PackSeller, шаги воспроизведения, файл `RE_Kenshi_log.txt` (лучше с включённым режимом отладки), по возможности копию сейва.

## Сборка из исходников

Исходный код: https://github.com/mitinkrll-64/PackSeller (папка `src/`). Требования KenshiLib: компилятор Visual C++ 2010 (toolset v100) x64, только Release|x64; C++11 и новее нельзя.

Среда, на которой собран релиз:

- Visual Studio 2026 Community (MSBuild Current) + toolset v100 x64 от Visual Studio 2010 Ultimate + SP1 (10.0.40219);
- команда: `MSBuild.exe src\PackSeller.sln /p:Configuration=Release /p:Platform=x64`; результат: `src\x64\Release\PackSeller.dll`.

Зависимости в `third_party/`:

| Что | Источник | Версия |
|---|---|---|
| KenshiLib (заголовки и `KenshiLib.lib`) | https://github.com/KenshiReclaimer/KenshiLib (зеркало/прежний адрес: https://github.com/BFrizzleFoShizzle/KenshiLib; релиз v0.5.0) | тег v0.5.0 = 11cd65d557dea76c6540239acf467e0e06e09cf7 |
| OgreMain_x64.lib, MyGUIEngine_x64.lib, заголовки зависимостей | https://github.com/BFrizzleFoShizzle/KenshiLib_Examples_deps | b566d74bf3d74629cc2fb632a97595b8202993f1 |
| Boost 1.60.0 (только заголовки) | `boost.zip` из KenshiLib_Examples_deps (Git LFS) | 1.60.0 |
| Образец проекта плагина | https://github.com/BFrizzleFoShizzle/KenshiLib_Examples | 548b3eaf779c1b2feb25416f1db757320d04ec6c |

Пути к зависимостям заданы в `src/PackSeller.vcxproj` (переменные среды и `Setup.bat` не нужны). Упаковка: `powershell -ExecutionPolicy Bypass -File tools\package.ps1` (версия берётся из `src\Version.h`; создаёт `dist\PackSeller\` и `dist\PackSeller-<версия>.zip`, в папку игры ничего не копирует).

## Лицензия и источники

Проект распространяется по лицензии GNU GPL версии 3 (требование KenshiLib), полный текст в файле `LICENSE`. Исходный код: https://github.com/mitinkrll-64/PackSeller (при распространении сборки исходники должны быть доступны получателям, GPLv3). Сторонние компоненты и их лицензии перечислены в `NOTICE.md`.

Благодарности: авторам RE_Kenshi и KenshiLib (BFrizzleFoShizzle, KenshiReclaimer), примерам KenshiLib_Examples, от которых взят формат `RE_Kenshi.json` и каркас проекта, и авторам community-плагинов, чьи открытые исходники изучались как справка по API. Чужой код в проект не копировался; все использованные вызовы API сверены с исходниками (`docs/api-notes.md`).

## Что проверено

Проверено автором в игре на Kenshi 1.0.65 (Steam) / RE_Kenshi 0.3.5 / KenshiLib 0.5.0, на копиях сейвов.

Версия 0.2 (сборка 390144 байта, SHA-256 DLL в `CHANGELOG.txt`):

- вкладка «Города»: список городов (11 городов в тестовом сейве), сортировка, пометки «очень далеко», «?» для не загруженных городов;
- поход к ближнему городу (Щит Окрана) с поиском торговца, выбором лучшего, продажей и возвратом;
- дальний поход к городу за пределами загруженной области (~13900 единиц, путь ~13400 единиц) без потери пути; поиск торговца в городе, у которого в списке было «?»;
- реальная продажа в дальнем городе («Глава фермы», 2 предмета на 2310; деньги игрока и торговца и предметы сошлись, торговец ограничил продажу своими деньгами);
- сохранение (быстрое) посреди дальнего возврата и восстановление приказа после загрузки;
- переключение камеры/выделения в пути не сбрасывает приказ (защита от короткого флага «загрузка»);
- вкладка «PackSeller» в окне НАСТРОЙКИ: отладка, язык Русский/English, сохранение в `PackSeller.user.cfg`. Проверена в игре с набором модов автора.

Версия 0.1 (на сборке 281600 байт, повторная проверка 0.2 не проводилась):

- полный цикл «путь, расчёт продажи, возврат» к торговцам в зданиях (Купец в магазине, Стройторговец в механике) с подходом снаружи; в этих двух зданиях проверен только расчёт без реальной продажи;
- реальная продажа проверена у Трактирщика в баре: командой и кликом «Продать» из окна при открытом инвентаре животного;
- торговец с нехваткой денег: продаётся только на сколько хватает, в минус не уходит;
- таймаут фазы, перехват приказа игроком (животное отпускается), повторный клик во время приказа («занято», BUSY);
- сортировка и фильтры списка (нет денег, нечего продать, радиус); русская локализация; закрытие окна кнопкой, крестиком и Esc;
- сохранение и восстановление приказа в состояниях «в пути» и «возврат»; сейв без приказа загружается без призрачных приказов;
- загрузка старого сейва без мода и сейва с записью мода при ОТКЛЮЧЁННОМ моде: без вылета (по одному разу).

Результаты тестов: `docs/test-plan.md`, `docs/decisions.md` (в репозитории, в архиве их нет).

## Не проверено

- гибель животного в пути; потеря сознания; торговец мёртв или недоступен;
- остановка при угрозе (её нет: дальний поход идёт без защиты);
- враждебные города на реальном случае (`hostile_policy=block`, пометка);
- город без загруженных торговцев после прибытия (проверен только положительный случай: торговцы нашлись);
- несколько животных или приказов одновременно (поддерживается один приказ);
- недостижимая цель вне зданий (остров и т. п.);
- непокупаемые предметы (нелегальные, краденые, контейнеры): должны остаться в рюкзаке;
- кочевые торговцы (не проверялось, что их нет в списке);
- совместимость с модами окна инвентаря (кнопка «Торговцы», например Organize the Inventory); совместимость вкладки настроек с другими модами раздела Options (KEP, KenshiRotate; проверено только с набором модов автора, как и кнопка RE_Kenshi); `ui_modal=1`;
- автосохранение и сохранение в момент продажи; удаление животного или торговца между сохранением и загрузкой;
- вид сообщений со временем в минутах («около 6 мин», «около 1 ч 5 мин»): реализовано в сборке 0.2.0, на экране не проверялось (в логах проверенных походов время было ещё в секундах);
- обновление мода через Workshop с сохранением `PackSeller.user.cfg` (Steam может удалить файл);
- Kenshi 1.0.68 и GOG-версия.

---

<a name="english"></a>
# English

**PackSeller 0.2.0** — a RE_Kenshi plugin that lets an animal sell its backpack goods to a trader by itself, in your town or in a distant one.

What it does: in the inventory window of your animal a "Traders" button appears above OPEN BAG. It opens a window with two tabs. **Traders**: the traders currently loaded by the game (name, town, distance with "far / very far" tags, money; sortable). **Towns** (new in 0.2): a list of towns (faction, distance, number of loaded traders, "?" if unknown). Press "Sell": the animal walks to the trader (or to the chosen town, loading the area on the way, where it picks a suitable trader itself), sells the backpack items automatically and walks back to where it stood. Traders in buildings are approached from outside. A trader never goes below zero money (the animal sells only what he can pay for). Hostile towns/factions are marked and refused by default (`hostile_policy=block`). The active order is saved in the save game.

Distance: the Traders list shows what the game has loaded (the area around your characters, at least ~11000 units). The Towns tab can send the animal to a town beyond it (confirmed with a trip of ~13900 units). The mod warns "far", "very far" and "long trip" (time in minutes). A long trip is dangerous: there is NO stop-on-threat, the animal can die with the cargo. `max_trip_distance` (default 40000) caps the distance.

In-game settings: Options window, "PackSeller" tab: debug mode and language (Russian / English; Russian needs the Russian game locale). Stored in `PackSeller.user.cfg` next to the DLL (not in the archive; a Workshop update may delete it, untested). `PackSeller.cfg` is never rewritten.

Requirements (confirmed): Kenshi 1.0.65 (Steam), RE_Kenshi 0.3.5, KenshiLib 0.5.0. Kenshi 1.0.68 and the GOG version are NOT tested.

Install: subscribe on Steam Workshop, or extract the archive so that `Kenshi\mods\PackSeller\` contains `PackSeller.dll`, `PackSeller.mod`, `RE_Kenshi.json`, `PackSeller.cfg`; enable PackSeller in the launcher Mods tab; check `RE_Kenshi_log.txt` for `[PackSeller] PackSeller 0.2.0` and `[PackSeller] loaded`.

Settings: `PackSeller.cfg` next to the DLL, every key is commented in Russian and English (0.2 keys: `list_radius`, `max_trip_distance`, `list_far_distance`, `list_very_far_distance`, `order_speed_estimate`, `order_timeout_factor`, `persist_resume_seconds`). Set `sell_dry_run=1` to test without changing anything. Debug commands are off by default (`debug_commands=0`).

Tested in game (1.0.65): Towns tab, a trip to the nearby town and a trip of ~13900 units to a far town with trader search, a real sale in the far town, quick-save in the middle of a far return and restore, camera switching on the way, time in minutes, the Options tab (with the author's mod set); from 0.1: the full cycle to a bar trader and to traders in buildings (calculation only in buildings), real sale by command and by clicking, a trader with too little money, timeout, player override, repeated click (BUSY), list sorting and filters, Russian text, saves while going and returning, a save without an order, loading a save without the mod and a save with a mod record while the mod is disabled.

NOT tested: death of the animal, stop on threat (does not exist), a real hostile town, a town with no loaded traders after arrival, several animals/orders at once (one order only), nomad traders, mods that change the inventory window, compatibility of the Options tab with other Options mods (KEP, KenshiRotate), ui_modal=1, autosave and saving during a sale, deleting objects between save and load, keeping `PackSeller.user.cfg` across a Workshop update, Kenshi 1.0.68 and GOG. Back up your saves first.

Uninstall: disable the mod in the launcher or delete the folder. A save with an active order contains a small record (`PackSeller_orders`); loading such a save with the mod disabled was tested once without a crash on 1.0.65 (mod 0.1.0).

Build: Visual C++ 2010 (toolset v100) x64, Release only; see the Russian section for the exact command and dependency commits. Source: https://github.com/mitinkrll-64/PackSeller. License: GPLv3 (`LICENSE`); third-party licenses in `NOTICE.md`.
