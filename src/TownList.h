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

#ifndef PACKSELLER_TOWNLIST_H
#define PACKSELLER_TOWNLIST_H

// 0.2 step 3: the list of TOWNS for the "Towns" mode of the window (and the debug commands "list towns",
// "order town"). Rows hold COPIES of values (name, centre, radius, type, faction), never game pointers.
// Main thread only.

#include <string>
#include <vector>

#include <kenshi/util/hand.h>

namespace PS
{
	struct TownRow
	{
		std::string name;
		std::string faction;
		float cx, cy, cz;         // centre
		float radius;
		int townType;             // TownType value
		bool open;                // Town::openToPublic
		bool relationKnown;
		bool hostile;             // the town's faction is an enemy of the player
		int loaded;               // loaded traders whose home town is this town
		float dist;               // 2D distance animal -> centre
		bool isFar;
		bool isVeryFar;
		bool loadedUnknown;       // 0 loaded traders, but the town is probably outside the loaded area (shown as "?")
		TownRow() : cx(0), cy(0), cz(0), radius(0), townType(-1), open(false), relationKnown(false), hostile(false), loaded(0)
			, dist(0), isFar(false), isVeryFar(false), loadedUnknown(false) {}
	};

	struct TownListData
	{
		bool ok;
		std::string error;
		std::string animalName;
		std::vector<TownRow> shown;   // filtered and sorted
		int hiddenType, hiddenClosed, hiddenHostile, hiddenRadius, hiddenTrip;
		TownListData() : ok(false), hiddenType(0), hiddenClosed(0), hiddenHostile(0), hiddenRadius(0), hiddenTrip(0) {}
	};

	// Builds the list for the animal (hand from the window, or NULL = selected / first animal). sort: a ListSort value
	// (distance, name, faction). The raw rows are cached for 2 s (clicks on the sort headers do not rescan the world).
	bool BuildTownList(TownListData& out, int sort, const hand* animal);

	// Reads the town whose centre is within tol of (x, z) from the live town list. false when not found.
	bool TownLookup(float x, float z, float tol, TownRow* out);

	// Command "list towns": writes the list to the log.
	void TownsListCommand();
}

#endif
