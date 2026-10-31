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

#ifndef PACKSELLER_OPTIONS_H
#define PACKSELLER_OPTIONS_H

// 0.2: the "PackSeller" tab in the game's Options window (reachable from the main menu OPTIONS and from the
// in-game Esc menu): debug mode check box and UI language check boxes. Hooks H5 OptionsWindow::create and
// H7 OptionsWindow::_NV_update (see docs/api-notes.md, "Menu of the mod settings"). Nothing here touches ou or
// game objects: the main menu has no world.

namespace PS
{
	// Installs the hooks. Returns true when H5 (create) is active.
	bool OptionsInstallHooks();
}

#endif
