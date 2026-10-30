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

#ifndef PACKSELLER_UI_H
#define PACKSELLER_UI_H

// Stage 6: the "Traders" button in the pack animal inventory window and the trader list window.
// Hook H2 (InventoryGUI::_CONSTRUCTOR, see decisions.md) only adds the button. Everything else
// (opening / closing / sorting the window, the Sell click) runs in UiTick from the main loop
// hook H1, never inside a MyGUI event handler (handlers only queue a request).
// Main thread only.

namespace PS
{
	// Installs hook H2 if ui_enabled=1. Returns true when the hook is active.
	bool UiInstallHook();

	// Called every frame from the tick hook.
	void UiTick(float time);
}

#endif
