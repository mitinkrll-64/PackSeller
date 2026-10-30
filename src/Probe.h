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

#ifndef PACKSELLER_PROBE_H
#define PACKSELLER_PROBE_H

// Stage 3b: READ-ONLY diagnostic probes (log only). Nothing here changes the
// game state: no selling, no item moves, no money changes, no orders.
// All functions must be called from the game main thread only.

namespace PS
{
	// command "traders": loaded characters that look like traders
	void ProbeTraders();
	// command "towns": towns within probe_radius
	void ProbeTowns();
	// command "prices": price calibration for the selected animal's items
	void ProbePrices();
	// command "tradewin" toggles the trade window watcher ('on' / 'off' set it explicitly)
	void ProbeTradeWinToggle(int mode); // -1 toggle, 0 off, 1 on
	// called every frame from the tick hook (cheap when the watcher is off)
	void ProbeTick();
}

#endif
