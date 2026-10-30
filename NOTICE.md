# NOTICE: сторонние компоненты PackSeller

PackSeller распространяется по GNU GPL v3 (`LICENSE`). Плагин собирается с заголовками и библиотеками ниже. Тексты лицензий лежат в `third_party/KenshiLib_v0.5.0/Licenses/` (поставляются с KenshiLib) и в каталогах самих проектов.

| Компонент | Использование в PackSeller | Лицензия |
|---|---|---|
| KenshiLib 0.5.0 (https://github.com/KenshiReclaimer/KenshiLib) | заголовки и `KenshiLib.lib`, обязательная зависимость во время работы (поставляется с RE_Kenshi) | GPLv3 (`KenshiLib.LICENSE`) |
| RE_Kenshi 0.3.5 | загрузчик плагинов, ставится отдельно, не входит в пакет | см. лицензию RE_Kenshi `[TO VERIFY]` |
| Boost 1.60.0 | только заголовки при сборке | Boost Software License 1.0 (`third_party/boost_1_60_0/LICENSE_1_0.txt`) |
| MyGUI | заголовки и `MyGUIEngine_x64.lib` для окна | MIT (`MyGUI.COPYING.MIT`) |
| OGRE | заголовки и `OgreMain_x64.lib` (типы векторов и т. п.) | MIT (`Ogre.COPYING`) |
| KenshiLib_Examples / _deps | образец структуры проекта и формата `RE_Kenshi.json`; код не копировался | см. репозитории `[TO VERIFY: лицензия репозитория]` |

В поставку KenshiLib входят также FreeType (FreeType License, `Ftl.txt`), MinHook (BSD-2-Clause, `MinHookLICENSE.txt`), zstd (`zstd.LICENSE`), miniz (`miniz.LICENSE`), courgette (`courgette64.LICENSE`), rapidjson (`rapidjson.license.txt`), tinydng (`tinydng.LICENSE`). Напрямую в `PackSeller.dll` они не включены (плагин использует их только через KenshiLib, которая ставится вместе с RE_Kenshi) `[TO VERIFY: статическая линковка MyGUI/OGRE .lib не проверялась по таблице импортов]`.

Игра Kenshi принадлежит Lo-Fi Games; мод не аффилирован с разработчиком.
