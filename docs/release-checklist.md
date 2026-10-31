# Чек-лист выпуска PackSeller 0.2.0 (обновление Workshop-элемента 3830492848)

Ничего не публиковать и не загружать без явной просьбы пользователя. Чек-лист 0.1.0 выполнен и опубликован.

## Сделано (ps-release, 2026-10-31)
- [x] `tools\package.ps1`: версия из `src\Version.h` (0.2.0), проверка релизного cfg (sell_dry_run=0, debug_log=0, debug_commands=0, hostile_policy=block, order_enabled=1), согласованность 48 ключей `Config.cpp` и `dist\PackSeller.cfg.release`, версия в DLL, нет `*.user.cfg`, pdb/obj/исходников и личных путей.
- [x] `README.md` (RU + EN), `CHANGELOG.md` (раздел 0.2.0), `docs\workshop.md` (описание, Change Notes, инструкция обновления) обновлены. Устаревшее про «около 1800 единиц» исправлено, в том числе в комментарии `list_radius` релизного cfg (`dist\PackSeller.cfg.release`).
- [x] Превью `assets\workshop_preview.jpg` и `assets\workshop_preview_en.jpg` пересобраны `tools\make_preview.py`; скриншот `assets\screenshots\screenshot_3_towns_tab.jpg`.
- [x] Собран `dist\PackSeller-0.2.0.zip`; распаковка в чистый каталог, SHA-256 файлов совпали. Архив 0.1.0 не менялся (SHA-256 `CF4DF79D11E699E235699603CFE5EE8898D3E374A216D42EE012639816306273`).
- [x] DLL архива (390144 байта, SHA-256 `77D859A466A8CE6923A1BAAC57B65FA835F1ABB8DE3DEFD70E33455381252E8B`) побайтно совпадает (cmp) с установленной в игре.
- [x] SHA-256 архива `PackSeller-0.2.0.zip`: `BE83591F38449656FFD47F22106A454B664A0ADE0535427A760DA7A4FC20C37F` (хэш здесь, не в CHANGELOG, потому что CHANGELOG лежит внутри архива; при пересборке хэш меняется из-за меток времени).

## Осталось
- [ ] Пользователь: загрузка обновления через FCS в элемент 3830492848 по `docs\workshop.md` (раздел «Как обновить»), видимость сначала приватная. В папке мода только DLL, релизный cfg, `.mod`, `.img`, `.info`; без `PackSeller.user.cfg` и `PackSeller.cmd`.
- [ ] Обновить на странице элемента описание и Change Notes (тексты в `docs\workshop.md`), превью и скриншоты (в т.ч. `screenshot_3_towns_tab.jpg`).
- [ ] Проверка после загрузки: чистый профиль или удалённая локальная папка, строка `PackSeller 0.2.0` в `RE_Kenshi_log.txt`, вкладки «Города» и «PackSeller» в НАСТРОЙКАХ; затем публичная видимость.
- [ ] Закоммитить и запушить исходники 0.2.0 в https://github.com/mitinkrll-64/PackSeller (GPLv3) и пометить тег v0.2.0 (решение пользователя).
- [ ] `[TO VERIFY]`: сохраняет ли Steam `PackSeller.user.cfg` при обновлении; обновляет ли FCS `_PackSeller.img` сам; загрузка сейва версии 1 (0.1) в 0.2 и отказ 0.1.0 от сейва версии 2; лицензии RE_Kenshi и KenshiLib_Examples, VC++ 2010 Redistributable на чистой системе (`NOTICE.md`).
- [ ] Не проверено в игре (перечислено в README): гибель животного, остановка при угрозе (нет), враждебные города, город без загруженных торговцев после прибытия, несколько приказов, кочевые, моды окна инвентаря и Options (KEP, KenshiRotate), `ui_modal=1`, автосохранение и сохранение в момент продажи, Kenshi 1.0.68 и GOG.
- [ ] Записи об игровых тестах вкладки настроек и других пунктов 0.2 внесены в README со слов лида: в `decisions.md`/`test-plan.md` результата проверки вкладки «PackSeller» нет, добавить.
