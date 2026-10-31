/*
 * PackSeller - pack animal auto-sell plugin for Kenshi (RE_Kenshi / KenshiLib)
 * Copyright (C) 2026 PackSeller contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef PACKSELLER_LANG_H
#define PACKSELLER_LANG_H

// Stage 6: the single table of UI strings (Russian + English).
// Texts are UTF-8 std::string / char*. The Cyrillic glyph range exists in the game font
// only for the ru_RU game language (api-notes section 9), so Russian is used only then.
// GENERATED strings: edit the table in Lang.cpp (octal UTF-8 escapes keep the file ASCII).

namespace PS
{
	enum StrId
	{
		S_BTN_TRADERS,
		S_WIN_TITLE,
		S_COL_NAME,
		S_COL_TOWN,
		S_COL_DIST,
		S_COL_MONEY,
		S_BTN_SELL,
		S_BTN_CLOSE,
		S_INFO,
		S_EMPTY,
		S_HOSTILE_TAG,
		S_NOTE,
		S_ERR_LIST,
		S_MSG_DRY,
		S_MSG_NOTHING,
		S_MSG_FAIL,
		S_MSG_GONE,
		S_BTN_BUSY,
		S_NOTE_TEST,
		S_NOTE_LIVE,
		S_MSG_BUSY,
		S_MSG_STARTED,
		S_MSG_SOLD,
		S_MSG_SOLD_DRY,
		S_MSG_NOBUY,
		S_MSG_SELLFAIL,
		S_MSG_BACK,
		S_ERR_TRADER,
		S_ERR_ANIMAL,
		S_ERR_PATH_GO,
		S_ERR_PATH_BACK,
		S_ERR_TIMEOUT_GO,
		S_ERR_TIMEOUT_BACK,
		S_ERR_OVERRIDDEN,
		S_MSG_START_FAIL,
		S_ERR_NO_APPROACH,
		S_MSG_RESTORED,
		S_MSG_LOAD_LOST,
		S_ERR_HOSTILE,
		S_MSG_HOSTILE_WARN,
		S_FAR_TAG,
		S_ERR_TOO_FAR,
		S_MSG_LONG_TRIP,
		S_VERY_FAR_TAG,
		S_MSG_VERY_LONG_TRIP,
		S_EMPTY_NOTHING,
		S_TAB_TRADERS,
		S_TAB_TOWNS,
		S_COL_FACTION,
		S_COL_LOADED,
		S_INFO_TOWNS,
		S_EMPTY_TOWNS,
		S_NOTE_TOWNS,
		S_MSG_TOWN_STARTED,
		S_MSG_TOWN_SEARCH,
		S_ERR_NO_TRADER,
		S_ERR_TOWN_GONE,
		S_COUNT
	};

	// Decides the language once: ui_language=auto (settings.cfg 'language=ru_RU' -> Russian,
	// anything else -> English) | en | ru. Logs the decision. Safe to call repeatedly.
	void LangInit();

	// UTF-8 text for the id in the current language (never NULL).
	const char* Tr(StrId id);

	bool LangIsRussian();
}

#endif
