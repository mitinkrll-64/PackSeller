# PackSeller 0.1.0 — автопродажа вьючника для Kenshi

English summary is below / краткое описание на английском ниже: [English](#english).

> Статус: версия 0.1.0, первый выпуск. Проверено в игре только то, что перечислено в разделе «Что проверено»; всё остальное перечислено в разделе «Не проверено». 

## Что делает мод

Плагин для RE_Kenshi. В окне инвентаря вьючного животного (над кнопкой «ОТКРЫТЬ» / OPEN BAG, где открывается рюкзак) появляется кнопка «Торговцы». Она открывает окно со списком городских торговцев поблизости: имя, город, расстояние, деньги торговца и кнопка «Продать». После нажатия животное:

1. идёт к выбранному торговцу (к торговцу в здании подходит снаружи);
2. при входе в зону взаимодействия продаёт товар из рюкзака (деньги зачисляются вам, предметы убираются из рюкзака);
3. возвращается на точку, где стояло в момент нажатия.

Список можно сортировать (расстояние, деньги, город) кликом по заголовку. Торговцы без денег и те, кому нечего продать, скрываются. Города враждебных фракций помечаются (см. ограничения).

## Требования

| Компонент | Версия | Статус проверки |
|---|---|---|
| Kenshi | 1.0.65 (Steam, x64) | подтверждено |
| RE_Kenshi | 0.3.5 | подтверждено |
| KenshiLib | 0.5.0 (поставляется с RE_Kenshi) | подтверждено |
| Kenshi 1.0.68 | | НЕ проверено |
| Kenshi GOG | | НЕ проверено |

Мод собран для KenshiLib 0.5.0 и может не работать с другой версией игры/RE_Kenshi. Библиотека времени выполнения VC++ 2010 (`MSVCR100.dll`) нужна плагину; игра и RE_Kenshi её используют, отдельная установка обычно не требуется .

## Установка

1. Установите RE_Kenshi 0.3.5  и запустите игру через него хотя бы раз, чтобы убедиться, что он работает.
2. Распакуйте архив `PackSeller-0.1.0.zip` так, чтобы получилась папка `Kenshi\mods\PackSeller\` с файлами:
   - `PackSeller.dll`
   - `PackSeller.mod`
   - `RE_Kenshi.json`
   - `PackSeller.cfg`
3. Запустите игру через RE_Kenshi, в лаунчере откройте вкладку «Mods» и включите `PackSeller`.
4. Проверьте файл `RE_Kenshi_log.txt` в папке Kenshi: в нём должны быть строки `[PackSeller] PackSeller 0.1.0 (RE_Kenshi, KenshiLib 0.5.0)` и `[PackSeller] loaded`.

## Как пользоваться

1. Загрузите товар в рюкзак вьючного животного (рюкзак открывается кнопкой «ОТКРЫТЬ»).
2. Откройте окно инвентаря животного. Над кнопкой «ОТКРЫТЬ» (в игре с другими модами инвентаря положение может отличаться) нажмите «Торговцы».
3. В окне «Продажа торговцу» выберите строку и нажмите «Продать». Заголовки «Город», «Расстояние», «Деньги» сортируют список.
4. Животное уйдёт к торговцу, продаст и вернётся. Сообщения о ходе и ошибках показываются в игре. Окно закрывается кнопкой, крестиком или Esc.

Одновременно работает один приказ. Повторное нажатие «Продать» во время приказа даёт сообщение «занято». Если вы сами отдадите животному другой приказ, мод отпустит его и не вернёт на старт.

## Настройки (`PackSeller.cfg`)

Файл лежит рядом с DLL, читается при запуске игры. Основные ключи (полный список с комментариями на русском и английском внутри файла):

| Ключ | По умолчанию в релизе | Смысл |
|---|---|---|
| `sell_dry_run` | 0 | 1 = только расчёт, ничего не меняется (безопасная проверка) |
| `order_enabled` | 1 | 1 = кнопка запускает цикл, 0 = кнопка только считает |
| `debug_log` | 0 | подробный лог; включайте для отчёта об ошибке |
| `ui_language` | auto | auto / ru / en |
| `list_radius` | 3000 | радиус поиска от животного до центра города |
| `list_min_money` | 100 | скрывать торговцев с меньшей суммой |
| `list_sort` | distance | distance / money / town |
| `trade_range` | 150 | дистанция до торговца, с которой начинается продажа |
| `order_timeout_seconds` | 300 | таймаут одной фазы (путь туда / назад) |
| `order_max_items` | 200 | максимум единиц предметов за один цикл |
| `save_enabled` | 1 | сохранять активный приказ в сейв |

## Известные ограничения версии 0.1

- Видны только торговцы в загруженной игрой зоне (около 1800 единиц вокруг животного/вашего отряда). Дальние города в список не попадают независимо от `list_radius`. Режим «другой город» (путь в далёкий город) в планах на версию 0.2.
- Торговцы в зданиях недоступны напрямую: животное подходит снаружи на расстояние порядка  и продаёт оттуда. 
- Торговцы враждебных городов и фракций: по умолчанию (`hostile_policy=block`) приказ отклоняется с сообщением; в списке такие города помечаются. Ни блокировка, ни пометка не проверены в игре на реальном враждебном городе.
- Кнопка появляется у любого вашего животного.
- Проданные предметы уничтожаются, на склад торговца они не переносятся.
- Цена считается по формуле плагина с множителем 1.0 и может отличаться от окна торговли примерно до 3%. Цены разных торговцев и городов отдельно не учитываются.
- Предметы, которые торговец не берёт (нелегальные, краденые, экипированные), должны остаться в рюкзаке, но это НЕ проверено в игре. Предметы выключенных секций инвентаря пропускаются.
- Торговцы услугами (например, хирург) могут попасть в список.
- Кочевые торговцы должны исключаться
- Одно животное за раз; отмены приказа кнопкой нет (отменить можно, отдав животному свой приказ).
- Животное с грузом в пути может погибнуть (бандиты, хищники);
- Недостижимая цель вне зданий (остров и т. п.): обработка предусмотрена, в игре не проверена.
- Совместимость с модами окна инвентаря (например, Organize the Inventory) не проверена.
- Не проверено на Kenshi 1.0.68 и на GOG-версии; игровые обновления могут сломать хуки.

## Удаление и отключение

- Выключите мод в лаунчере (или удалите папку `Kenshi\mods\PackSeller\`).
- Сейвы: если в момент сохранения шёл приказ, в сейв пишется небольшая запись мода (тип 20717, `PackSeller_orders`). Если приказа не было, запись не создаётся и сейв неотличим от сейва без мода. Загрузка сейва с такой записью при ОТКЛЮЧЁННОМ моде проверена один раз на Kenshi 1.0.65: игра загрузилась без вылета. Это единичная проверка, поэтому перед удалением мода сделайте копию сейва и не сохраняйтесь во время приказа.
- Старые сейвы без мода загружаются как обычно (проверено).

## Сообщить об ошибке

Приложите: версию Kenshi, RE_Kenshi и PackSeller, шаги воспроизведения, файл `RE_Kenshi_log.txt` (лучше с `debug_log=1`), по возможности копию сейва.

## Сборка из исходников

Исходный код: https://github.com/mitinkrll-64/PackSeller (папка `src/`). Требования KenshiLib: компилятор Visual C++ 2010 (toolset v100) x64, только Release|x64; C++11 и новее нельзя.

Среда, на которой собран релиз:

- Visual Studio 2026 Community (MSBuild Current) + toolset v100 x64 от Visual Studio 2010 Ultimate + SP1 (10.0.40219);
- команда: `MSBuild.exe src\PackSeller.sln /p:Configuration=Release /p:Platform=x64`; результат: `src\x64\Release\PackSeller.dll`.

Зависимости в `third_party/` 

| Что | Источник | Версия |
|---|---|---|
| KenshiLib (заголовки и `KenshiLib.lib`) | https://github.com/KenshiReclaimer/KenshiLib (зеркало/прежний адрес: https://github.com/BFrizzleFoShizzle/KenshiLib; релиз v0.5.0) | тег v0.5.0 = 11cd65d557dea76c6540239acf467e0e06e09cf7 |
| OgreMain_x64.lib, MyGUIEngine_x64.lib, заголовки зависимостей | https://github.com/BFrizzleFoShizzle/KenshiLib_Examples_deps | b566d74bf3d74629cc2fb632a97595b8202993f1 |
| Boost 1.60.0 (только заголовки) | `boost.zip` из KenshiLib_Examples_deps (Git LFS) | 1.60.0 |
| Образец проекта плагина | https://github.com/BFrizzleFoShizzle/KenshiLib_Examples | 548b3eaf779c1b2feb25416f1db757320d04ec6c |

Пути к зависимостям заданы в `src/PackSeller.vcxproj` (переменные среды и `Setup.bat` не нужны). Упаковка: `powershell -ExecutionPolicy Bypass -File tools\package.ps1` (создаёт `dist\PackSeller\` и `dist\PackSeller-0.1.0.zip`, в папку игры ничего не копирует).

## Лицензия и источники

Проект распространяется по лицензии GNU GPL версии 3 (требование KenshiLib), полный текст в файле `LICENSE`. Исходный код: https://github.com/mitinkrll-64/PackSeller (при распространении сборки исходники должны быть доступны получателям, GPLv3). Сторонние компоненты и их лицензии перечислены в `NOTICE.md`.

Благодарности: авторам RE_Kenshi и KenshiLib (BFrizzleFoShizzle, KenshiReclaimer), примерам KenshiLib_Examples, от которых взят формат `RE_Kenshi.json` и каркас проекта, и авторам community-плагинов, чьи открытые исходники изучались как справка по API. Чужой код в проект не копировался; все использованные вызовы API сверены с исходниками (`docs/api-notes.md`).

## Что проверено

Проверено автором в игре на Kenshi 1.0.65 (Steam) / RE_Kenshi 0.3.5 / KenshiLib 0.5.0, на копиях сейвов, на сборке 281600 байт (SHA-256 в `CHANGELOG.txt`):

- полный цикл «путь, расчёт продажи, возврат» к торговцам в зданиях (Купец в магазине, Стройторговец в механике), включая подход снаружи здания; в этих двух зданиях проверен только расчёт без реальной продажи;
- реальная продажа (деньги игрока и торговца, предметы меняются корректно) проверена у Трактирщика в баре: командой и кликом «Продать» из окна при открытом инвентаре животного; у торговцев в зданиях реальная продажа не проверялась;
- торговец с нехваткой денег: продаётся только на сколько хватает, в минус он не уходит;
- таймаут фазы, перехват приказа игроком (животное отпускается), повторный клик во время приказа (сообщение «занято», BUSY);
- список торговцев: сортировка по заголовкам и фильтры (нет денег, нечего продать, радиус);
- кнопка и окно, русская локализация, закрытие кнопкой, крестиком и Esc;
- сохранение и восстановление приказа в состояниях «в пути» и «возврат»; сейв без приказа загружается без призрачных приказов;
- загрузка старого сейва без мода и сейва с записью мода при ОТКЛЮЧЁННОМ моде: без вылета (по одному разу).

Результаты тестов: `docs/test-plan.md`, `docs/decisions.md` (в архиве их нет).

## Не проверено

- смерть или потеря сознания животного в пути; торговец мёртв или недоступен;
- недостижимая цель вне зданий (остров и т. п.);
- непокупаемые предметы (нелегальные, краденые, контейнеры): должны остаться в рюкзаке, не проверено;
- кочевые торговцы (не проверялось, что их нет в списке);
- враждебные города: `hostile_policy=block` не проверен на реальном враждебном городе;
- совместимость с Organize the Inventory и другими модами окна инвентаря; `ui_modal=1`;
- быстрое сохранение и автосохранение; сохранение в момент продажи; удаление животного или торговца между сохранением и загрузкой;
- Kenshi 1.0.68 и GOG-версия.

---

<a name="english"></a>
# English

**PackSeller 0.1.0** — a RE_Kenshi plugin that lets a pack animal sell its backpack goods to a city trader by itself.

What it does: in the inventory window of your pack animal a "Traders" button appears above OPEN BAG. It opens a list of nearby city traders (name, town, distance, money; sortable). Press "Sell": the animal walks to the trader (to a point outside if the trader is in a building), sells the backpack items automatically and walks back to where it stood.

Requirements (confirmed): Kenshi 1.0.65 (Steam), RE_Kenshi 0.3.5, KenshiLib 0.5.0. Kenshi 1.0.68 and the GOG version are NOT tested.

Install: install RE_Kenshi, extract the archive so that `Kenshi\mods\PackSeller\` contains `PackSeller.dll`, `PackSeller.mod`, `RE_Kenshi.json`, `PackSeller.cfg`; enable PackSeller in the launcher Mods tab; check `RE_Kenshi_log.txt` for `[PackSeller] PackSeller 0.1.0` and `[PackSeller] loaded`.

Settings: `PackSeller.cfg` next to the DLL, every key is commented in Russian and English. Set `sell_dry_run=1` to test without changing anything.

Known limitations (0.1): only traders in the zone loaded by the game (about 1800 units around your squad) are listed; hostile-faction traders are refused by default (`hostile_policy=block`) and hostile towns are marked in the list (neither is tested on a real hostile town); only traders in the loaded zone are available, a "other town" mode is planned for 0.2; debug commands are off by default (`debug_commands=0`); the button appears for any player animal; sold items are destroyed; price may differ from the trade window by up to about 3%; one animal at a time, no cancel button; Tested in game (1.0.65): the full cycle to a bar trader and to traders in buildings, real sale by command and by clicking in the window, a trader with too little money, timeout, player override, repeated click (BUSY), list sorting and filters, Russian text, save and restore of an order while going and returning, a save without an order, loading a save without the mod and a save with a mod record while the mod is disabled. NOT tested: death/unconsciousness of the animal, dead or unavailable trader, unreachable target outside buildings, non-purchasable items, nomad traders, hostile town, Organize the Inventory and other inventory-window mods, ui_modal=1, quick/autosave, saving during the sale, deleting the animal/trader between save and load, Kenshi 1.0.68 and GOG.

Uninstall: disable the mod in the launcher or delete the folder. A save with an active order contains a small record (`PackSeller_orders`); loading such a save with the mod disabled was tested once without a crash on 1.0.65. Back up your saves first.

Build: Visual C++ 2010 (toolset v100) x64, Release only; see the Russian section for the exact command and dependency commits. License: GPLv3 (`LICENSE`); third-party licenses in `NOTICE.md`.
