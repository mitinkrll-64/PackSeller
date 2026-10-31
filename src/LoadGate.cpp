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

#include "LoadGate.h"
#include "Config.h"
#include "Log.h"
#include "Persist.h"

#include <windows.h>

namespace PS
{
	void LoadGateReset(LoadGate& g)
	{
		g.armed = false;
		g.inFlag = false;
		g.resuming = false;
		g.setReal = 0;
		g.deadline = 0;
		g.baseline = 0;
	}

	LoadGateResult LoadGateStep(LoadGate& g, bool loading, const char* who, const char* state)
	{
		const unsigned loads = PersistLoadCount();
		const DWORD now = GetTickCount();
		if (!g.armed)
		{
			g.armed = true;
			g.baseline = loads;
		}
		if (loading)
		{
			if (!g.inFlag)
			{
				g.inFlag = true;
				g.resuming = false;
				g.setReal = now;
				LogInfo("%s: loading flag SET while state=%s (persist loads so far=%u, load detection %s): waiting, no orders, no time counted",
					who, state, loads, PersistLoadDetectionActive() ? "on" : "OFF");
			}
			return LG_WAIT;
		}
		if (g.inFlag)
		{
			g.inFlag = false;
			const DWORD dur = now - (DWORD)g.setReal;
			if (!PersistLoadDetectionActive())
			{
				LogInfo("%s: loading flag CLEARED after %u ms; load detection is off (save_enabled=0) -> drop", who, (unsigned)dur);
				g.baseline = loads;
				return LG_DROP;
			}
			if (loads != g.baseline)
			{
				LogInfo("%s: loading flag CLEARED after %u ms; a save was loaded (persist loads %u -> %u) -> drop", who, (unsigned)dur, g.baseline, loads);
				g.baseline = loads;
				return LG_DROP;
			}
			g.resuming = true;
			g.deadline = now + (DWORD)(GetConfig().persistResumeSeconds * 1000.0f);
			LogInfo("%s: loading flag CLEARED after %u ms; no save was loaded -> resuming (objects must resolve within %.0f s)", who, (unsigned)dur,
				GetConfig().persistResumeSeconds);
			return LG_RESUME;
		}
		if (loads != g.baseline)
		{
			LogInfo("%s: persist load counter changed outside the loading flag (%u -> %u) -> drop", who, g.baseline, loads);
			g.baseline = loads;
			return LG_DROP;
		}
		if (g.resuming)
			return LG_RESUME;
		return LG_RUN;
	}

	void LoadGateResumeDone(LoadGate& g, const char* who)
	{
		g.resuming = false;
		LogInfo("%s: resume: the objects resolve, carrying on", who);
	}

	bool LoadGateExpired(const LoadGate& g)
	{
		return (long)(GetTickCount() - (DWORD)g.deadline) >= 0;
	}
}
