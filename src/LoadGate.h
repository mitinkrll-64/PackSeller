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

#ifndef PACKSELLER_LOADGATE_H
#define PACKSELLER_LOADGATE_H

// 0.2: tolerance to a SHORT isLoadingFromASaveGame()||gameResetting flag. In the 0.2 probe the flag was set for a moment
// without any save being loaded (the camera switched to the far animal) and the old code dropped the order.
// A machine keeps one LoadGate (plain values, zero-initialised static is fine) and calls LoadGateStep every tick while
// it is active: while the flag is set it waits (sends no orders, counts no time); when the flag is cleared and no save was
// loaded in between (the load counter of the persist hook H4 is unchanged) it resumes once its objects resolve by hand
// within persist_resume_seconds; otherwise it drops as before (WORLD_LOADING) and the persist restore does its work.

namespace PS
{
	struct LoadGate
	{
		bool armed;
		bool inFlag;
		unsigned long setReal;
		unsigned baseline;
		bool resuming;
		unsigned long deadline;
	};

	enum LoadGateResult
	{
		LG_RUN = 0,     // normal tick
		LG_WAIT,        // the loading flag is set: do nothing
		LG_RESUME,      // the flag was cleared, no save loaded: check the objects (LoadGateResumeDone / LoadGateExpired)
		LG_DROP         // a save was loaded (or load detection is off): drop with WORLD_LOADING
	};

	void LoadGateReset(LoadGate& g);
	LoadGateResult LoadGateStep(LoadGate& g, bool loading, const char* who, const char* state);
	void LoadGateResumeDone(LoadGate& g, const char* who);
	bool LoadGateExpired(const LoadGate& g);
}

#endif
