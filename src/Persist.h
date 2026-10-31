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

#ifndef PACKSELLER_PERSIST_H
#define PACKSELLER_PERSIST_H

// Stage 8: the active order is written into the save and read back (docs/api-notes.md, "Этап 8").
// Hook H3: FactionManager::saveGameState (writes one GameData record when an order is active).
// Hook H4: GameWorld::loadAllPlatoons (reads the record after the original and registers a pending restore;
// the restore itself runs later from OrderTick, when the world is ready).

namespace PS
{
	// Installs H3 and H4 if save_enabled=1. Returns true when both are active.
	bool PersistInstallHooks();

	// Number of game loads seen by hook H4 since the plugin started (changes when a save is loaded).
	unsigned PersistLoadCount();
	// true when hook H4 is installed, i.e. PersistLoadCount() really detects loads (save_enabled=1).
	bool PersistLoadDetectionActive();

	// Command "save status": hooks, what would be saved now, the pending restore, last save/load results.
	void PersistStatusCommand();
}

#endif
