# Описание для Steam Workshop (черновик)

Не опубликовано. Загрузку в Workshop делает пользователь сам (см. «Как загрузить» ниже). Разметка Steam (BBCode). Текст ниже можно вставить в поле описания.

## Название

PackSeller: автопродажа вьючного животного

## Краткое описание (одна строка)

Вьючное животное само относит груз городскому торговцу, продаёт и возвращается на место. / The pack animal walks to a city trader, sells its load and returns.

## Описание (RU + EN)

```
[h1]PackSeller: автопродажа вьючного животного[/h1]

Загрузите рюкзак вьючного животного, откройте его инвентарь и нажмите [b]«Торговцы»[/b] (кнопка над «ОТКРЫТЬ»). В списке городских торговцев рядом (имя, город, расстояние, деньги) нажмите [b]«Продать»[/b]: животное идёт к торговцу, продаёт содержимое рюкзака и возвращается туда, где стояло.

[b]Возможности[/b]
[list]
[*] список городских торговцев в загруженной зоне, сортировка по расстоянию, деньгам и городу;
[*] торговцы в зданиях (магазин, механик): животное подходит снаружи;
[*] продаётся только то, что торговец может оплатить: деньги торговца в минус не уходят;
[*] враждебные города помечены и по умолчанию блокируются (hostile_policy в PackSeller.cfg);
[*] приказ переживает сохранение и загрузку;
[*] русский и английский интерфейс.
[/list]

[b]Требования[/b]
[list]
[*] Kenshi 1.0.65 (Steam). Другие версии не проверялись.
[*] [b]RE_Kenshi 0.3.5[/b] (обязательно, мод работает как его плагин).
[*] KenshiLib 0.5.0 (идёт вместе с RE_Kenshi).
[/list]

[b]Установка[/b]
Подпишитесь на мод и включите его в лаунчере (вкладка Mods). RE_Kenshi должен быть установлен и запущен вместе с игрой. Настройки: файл PackSeller.cfg в папке мода (комментарии внутри).

[b]Ограничения[/b]
[list]
[*] Список охватывает торговцев в загруженной части мира (около 1800 единиц от животного). Продажа в другом городе пока невозможна, режим «другой город» в планах.
[*] Животное с грузом может погибнуть по дороге (бандиты, хищники): опасность маршрута мод не учитывает.
[*] Не проверено в игре: смерть животного в пути, кочевые торговцы, враждебные города, совместимость с другими модами окна инвентаря, быстрое и автосохранение, Kenshi 1.0.68 и GOG.
[*] Перед первым использованием сделайте копию сейва.
[/list]

[b]Исходный код и лицензия[/b]
GPLv3. Исходники: https://github.com/mitinkrll-64/PackSeller

[hr][/hr]

[h1]PackSeller: pack animal auto-sell[/h1]

Load your pack animal's backpack, open its inventory and press [b]"Traders"[/b] (above the OPEN BAG button). Pick a nearby city trader and press [b]"Sell"[/b]: the animal walks to the trader, sells the backpack contents and walks back to where it stood.

[b]Requirements:[/b] Kenshi 1.0.65 (Steam), [b]RE_Kenshi 0.3.5[/b] (required, this is a RE_Kenshi plugin), KenshiLib 0.5.0 (ships with RE_Kenshi).
[b]Limits:[/b] only traders in the loaded part of the world (about 1800 units around the animal); other versions, GOG, nomad traders and other inventory-window mods are untested. Back up your save first.
[b]Source and license:[/b] GPLv3, https://github.com/mitinkrll-64/PackSeller
```

## Как загрузить (делает пользователь)

1. Подготовьте превью (рекомендуется скриншот окна «Продажа торговцу», 16:9, не больше 1 МБ).
2. Загрузка идёт средствами игры (Kenshi Launcher / FCS, загрузка в Workshop для выбранного мода) [TO VERIFY: точный пункт меню и то, как Workshop упаковывает DLL плагина RE_Kenshi; сверить по документации RE_Kenshi и инструкции Workshop-плагинов KenshiLib].
3. В папке мода для Workshop должны быть: `PackSeller.dll`, `PackSeller.mod`, `RE_Kenshi.json`, `PackSeller.cfg` (релизный, из `dist\PackSeller.cfg.release`). Готовую раскладку даёт `tools\package.ps1` (папка `dist\PackSeller\`).
4. После загрузки подпишитесь на мод с чистого профиля и проверьте строку `PackSeller 0.1.0 (RE_Kenshi, KenshiLib 0.5.0)` в `RE_Kenshi_log.txt`.
5. Укажите в описании зависимость от RE_Kenshi и ссылку на репозиторий (текст выше).
