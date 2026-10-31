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

#ifndef PACKSELLER_ZONEPROBE_H
#define PACKSELLER_ZONEPROBE_H

// 0.2 step 1: PROBE for the "other towns" mode (debug commands only, debug_commands=1).
// It changes neither the sale nor the UI. Commands:
//   zone              how many characters / traders are loaded around the animal and around the squad leader
//   gotown <n|name>   send the selected animal to a town (n = row of "towns"), run "zone" and "traders" on
//                     arrival and (probe_return_home=1) bring the animal back
// The probe has its own small state machine; the sale cycle (Order.cpp) stays Idle and refuses to start
// while the probe walks. Main thread only.

#include <string>

namespace PS
{
	void ZoneProbeZone();
	void ZoneProbeGoTown(const std::string& arg);

	// Called every frame from the tick hook (cheap when no probe walk is active).
	void ZoneProbeTick(float time);

	bool ZoneProbeActive();
}

#endif
