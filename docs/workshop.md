# Описание для Steam Workshop (0.2.0, черновик обновления)

Элемент Workshop 3830492848 (0.1.0 уже опубликована). Ничего не загружено: обновление делает пользователь (см. «Как обновить»). Разметка Steam (BBCode); текст можно вставить в поле описания.

## Название

PackSeller: автопродажа на животном

## Краткое описание (одна строка)

Животное само относит груз торговцу (в вашем или в далёком городе), продаёт и возвращается. / The animal walks to a trader in your town or a distant one, sells its load and returns.

## Описание (RU + EN)

```
[h1]PackSeller: автопродажа на животном[/h1]

Загрузите рюкзак животного, откройте его инвентарь и нажмите [b]«Торговцы»[/b] (кнопка над «ОТКРЫТЬ»). Откроется окно с двумя вкладками:

[list]
[*] [b]Торговцы[/b]: торговцы, загруженные игрой (имя, город, расстояние, деньги). Нажмите «Продать»: животное идёт к торговцу, продаёт содержимое рюкзака и возвращается туда, где стояло.
[*] [b]Города[/b] (новое в 0.2): список городов с фракцией и расстоянием. Нажмите «Продать»: животное идёт в выбранный город, в том числе за пределы загруженной области (оно подгружает её по пути), само находит там подходящего торговца, продаёт и возвращается.
[/list]

[b]Возможности[/b]
[list]
[*] торговцы в зданиях: животное подходит снаружи;
[*] продаётся только то, что торговец может оплатить: деньги торговца в минус не уходят;
[*] предупреждения «далеко», «очень далеко» и «долгий путь» (время в минутах), ограничение дальности max_trip_distance;
[*] враждебные города помечены и по умолчанию блокируются (hostile_policy в PackSeller.cfg);
[*] приказ переживает сохранение и загрузку, смену камеры в пути не сбрасывает;
[*] вкладка «PackSeller» в окне НАСТРОЙКИ: режим отладки и язык (Русский / English; русский требует русскую локаль игры);
[*] русский и английский интерфейс.
[/list]

[b]Требования[/b]
[list]
[*] Kenshi 1.0.65 (Steam). Другие версии не проверялись.
[*] [b]RE_Kenshi 0.3.5[/b] (обязательно, мод работает как его плагин).
[*] KenshiLib 0.5.0 (идёт вместе с RE_Kenshi).
[/list]

[b]Установка[/b]
Подпишитесь на мод и включите его в лаунчере (вкладка Mods). RE_Kenshi должен быть установлен и запущен вместе с игрой. Настройки: файл PackSeller.cfg в папке мода (комментарии внутри) и вкладка «PackSeller» в окне НАСТРОЙКИ (сохраняется в PackSeller.user.cfg).

[b]ВНИМАНИЕ, ограничения[/b]
[list]
[*] Дальний поход долгий и [b]опасный[/b]: остановки при угрозе нет, животное с грузом может погибнуть по дороге (бандиты, хищники). Не отправляйте груз, который жалко потерять.
[*] Один приказ за раз, кнопки отмены нет (отменить можно, отдав животному свой приказ).
[*] Не проверено в игре: гибель животного в пути, враждебные города, город без загруженных торговцев после прибытия, кочевые торговцы, совместимость с другими модами окна инвентаря и модами раздела НАСТРОЙКИ (KEP, KenshiRotate), автосохранение и сохранение в момент продажи, Kenshi 1.0.68 и GOG.
[*] После обновления мода Steam может удалить PackSeller.user.cfg (не проверено): язык и отладку из меню придётся выбрать заново.
[*] Перед первым использованием сделайте копию сейва.
[/list]

[b]Исходный код и лицензия[/b]
GPLv3. Исходники: https://github.com/mitinkrll-64/PackSeller

[hr][/hr]

[h1]PackSeller: animal auto-sell[/h1]

Load your animal's backpack, open its inventory and press [b]"Traders"[/b] (above the OPEN BAG button). The window has two tabs:

[list]
[*] [b]Traders[/b]: the traders loaded by the game (name, town, distance, money). Press "Sell": the animal walks to the trader, sells the backpack contents and walks back to where it stood.
[*] [b]Towns[/b] (new in 0.2): a list of towns with faction and distance. Press "Sell": the animal walks to the chosen town, even beyond the loaded area (it loads the area on the way), finds a suitable trader there by itself, sells and walks back.
[/list]

Also: traders in buildings are approached from outside; a trader never goes below zero money; "far", "very far" and "long trip" warnings (time in minutes) and a distance cap; hostile towns are marked and blocked by default; the order survives saving/loading and a camera switch; a "PackSeller" tab in the Options window (debug mode, language Russian / English; Russian needs the Russian game locale).

[b]Requirements:[/b] Kenshi 1.0.65 (Steam), [b]RE_Kenshi 0.3.5[/b] (required, this is a RE_Kenshi plugin), KenshiLib 0.5.0 (ships with RE_Kenshi).
[b]WARNING:[/b] a far trip is long and dangerous: there is no stop on threat, the animal can die with the cargo. One order at a time, no cancel button.
[b]Untested:[/b] death of the animal, hostile towns, a town with no loaded traders on arrival, nomad traders, other inventory-window and Options mods (KEP, KenshiRotate), autosave and saving during a sale, Kenshi 1.0.68, GOG. A Steam update may delete PackSeller.user.cfg (untested). Back up your save first.
[b]Source and license:[/b] GPLv3, https://github.com/mitinkrll-64/PackSeller
```

## Заметка об изменениях (Change Notes) 0.2.0

```
[b]0.2.0[/b]
[list]
[*] Новая вкладка «Города»: поход в другой город, в том числе за пределы загруженной области, поиск торговца, продажа, возврат.
[*] Список торговцев показывает всех загруженных торговцев; пометки «далеко» / «очень далеко», предупреждение «долгий путь» (время в минутах), max_trip_distance.
[*] Приказ не сбрасывается при смене камеры в пути; сохранение посреди дальнего похода восстанавливается.
[*] Вкладка «PackSeller» в окне НАСТРОЙКИ: режим отладки, язык Русский / English.
[*] Внимание: дальний поход опасен, остановки при угрозе нет.
[/list]
[b]0.2.0 (EN)[/b]
[list]
[*] New "Towns" tab: send the animal to another town (also beyond the loaded area), it finds a trader, sells and returns.
[*] The trader list shows all loaded traders; "far" / "very far" tags, "long trip" warning (time in minutes), max_trip_distance.
[*] The order survives a camera switch on the way; saving in the middle of a far trip is restored.
[*] New "PackSeller" tab in the Options window: debug mode, language Russian / English.
[*] Warning: a far trip is dangerous, there is no stop on threat.
[/list]
```

## Как обновить (делает пользователь)

Картинки в `assets\`: `workshop_preview.jpg` (RU) и `workshop_preview_en.jpg` (EN), пересобираются скриптом `tools\make_preview.py`; скриншоты в `assets\screenshots\` (`screenshot_3_towns_tab.jpg`: вкладка «Города»).

1. Закрыть игру. Собрать пакет: `powershell -ExecutionPolicy Bypass -File tools\package.ps1` (даёт `dist\PackSeller\` и `dist\PackSeller-0.2.0.zip`; архив 0.1.0 не трогается).
2. Раскладка папки `Kenshi\mods\PackSeller\` для загрузки (как была при публикации 0.1.0): `PackSeller.dll` (новый, 0.2.0), `PackSeller.cfg` (релизный, из пакета), `PackSeller.mod`, `RE_Kenshi.json`, `_PackSeller.img`, `_PackSeller.info`. Файл `PackSeller.mod` в игре сейчас 3841 байт (его переписал FCS при первой загрузке), в пакете 46 байт: для обновления оставьте существующие `PackSeller.mod`, `_PackSeller.img`, `_PackSeller.info` из папки игры и замените только DLL и cfg. `PackSeller.user.cfg`, `PackSeller.cmd`, логи и отладочные файлы в папке мода на время загрузки быть не должны (user.cfg убрать и потом вернуть).
3. Запустить FCS, открыть мод PackSeller, выбрать пункт Steam Workshop и загрузить обновление в тот же элемент 3830492848 `[TO VERIFY: точные пункты меню; цикл подтверждён на 0.1.0]`. Видимость сначала выставить приватной, чтобы проверить.
4. Описание и Change Notes обновить вручную на странице элемента (тексты выше); превью заменить на `assets\workshop_preview.jpg`, скриншоты добавить в галерею. `_PackSeller.img` должен совпасть с новым `workshop_preview.jpg` (иначе скопировать вручную) `[TO VERIFY: обновляет ли FCS превью сам]`.
5. Проверка после загрузки: подписаться с чистого профиля (или убрать локальную папку мода), убедиться, что в `RE_Kenshi_log.txt` есть `PackSeller 0.2.0 (RE_Kenshi, KenshiLib 0.5.0)`, вкладка «Города» открывается, вкладка «PackSeller» есть в НАСТРОЙКАХ; затем сделать элемент публичным.

## Фактический результат загрузки 0.1.0 (2026-10-31)

- Элемент Workshop: ID 3830492848, https://steamcommunity.com/sharedfiles/filedetails/?id=3830492848.
- FCS при загрузке записал в папку мода `_PackSeller.img` (превью, точная копия `assets\workshop_preview.jpg`), `_PackSeller.info` (метаданные элемента) и переписал `PackSeller.mod` (3841 байт). Это нормально, не удалять.
- Обновление: вернуть в игру релизную раскладку (DLL, релизный `PackSeller.cfg`, `.mod`, `.img`, `.info`), запустить FCS, выбрать пункт Steam Workshop и загрузить обновление в тот же элемент. Копия раскладки 0.1.0 хранится локально в `backup\release-0.1.0-installed` (в репозиторий не входит).
