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

#ifndef PACKSELLER_TRADERLIST_H
#define PACKSELLER_TRADERLIST_H

// Stage 5: list of city traders for the pack animal (data only, no UI).
// The list holds COPIES of values and the trader's hand, never game pointers;
// the stage 6 window rebuilds the list on demand (BuildTraderList) and resolves
// a row to a Character with entry.h.getCharacter() when the player clicks it.
// Must be called from the game main thread only (tick hook).

#include <string>
#include <vector>

#include <kenshi/util/hand.h>

class Character;

namespace PS
{
	enum HideReason
	{
		HIDE_NONE = 0,
		HIDE_NOT_CITY,         // nomad / wandering / no home town / away from the home town
		HIDE_TOWN_TYPE,        // town type is not TOWN, VILLAGE or OUTPOST (MILITARY, RUINS, ...)
		HIDE_TOWN_CLOSED,      // Town::openToPublic is false (or the town is unreadable)
		HIDE_RADIUS,           // farther than list_radius
		HIDE_NO_MONEY,         // money < list_min_money
		HIDE_NOTHING_TO_SELL,  // trader would not buy a single unit from the animal
		HIDE_TRIP_TOO_FAR,     // farther than max_trip_distance from the animal (0.2)
		HIDE_REASON_COUNT
	};

	const char* HideReasonName(int reason);

	struct TraderEntry
	{
		hand h;                   // trader id; resolve with h.getCharacter() each time, never store the pointer
		std::string handStr;      // h.toString(), for logs and UI user data
		std::string name;
		std::string town;         // home town name (current town if no home), "" if none
		int townType;             // TownType value, -1 if no town
		int townOpen;             // Town::openToPublic: 1 / 0 / -1 unknown
		std::string building;     // building the trader stands in ("" if outdoors)
		float dist;               // 2D distance animal -> trader
		bool isVeryFar;           // dist > list_very_far_distance
		bool isFar;               // dist > list_far_distance: the window adds the "far" tag, the order warns about a long trip
		float townDist;           // 2D distance animal -> town centre, -1 if no town
		int money;                // Character::getMoney()
		std::string faction;      // trader faction name
		bool relationKnown;       // false when the faction relation cannot be read
		bool isEnemy;             // trader faction is an enemy of the player faction
		bool isAlly;
		float relation;           // getFactionRelation(player), meaningful if relationKnown
		bool townEnemy;           // town faction is an enemy of the player faction
		bool hostile;             // isEnemy || townEnemy (the UI marks the row)
		bool city;                // heuristic "city trader" (api-notes section 3)
		const char* nomadWhy;     // why city == false ("" if city); points to a string literal
		int sellable;             // 1 buys at least one unit from the animal, 0 no, -1 unknown / not checked
		int service;              // always -1: surgeon / service traders cannot be told apart reliably ("service?")
		int hideReason;           // HideReason; HIDE_NONE for rows in 'shown'

		TraderEntry()
			: townType(-1), townOpen(-1), dist(0.0f), isVeryFar(false), isFar(false), townDist(-1.0f), money(0)
			, relationKnown(false), isEnemy(false), isAlly(false), relation(0.0f), townEnemy(false), hostile(false)
			, city(false), nomadWhy(""), sellable(-1), service(-1), hideReason(HIDE_NONE) {}
	};

	struct TraderListStats
	{
		int updateList;       // characters in the update list
		int candidates;       // passed the "is trader" test
		int skipAnimal;
		int skipUnusable;     // dead / unconscious / destroyed / kidnapped / carried
		int exceptions;       // blocks aborted by SEH
		int capped;           // dropped by the entry limit
		int hiddenBy[HIDE_REASON_COUNT];
		TraderListStats() : updateList(0), candidates(0), skipAnimal(0), skipUnusable(0), exceptions(0), capped(0)
		{
			for (int i = 0; i < HIDE_REASON_COUNT; ++i)
				hiddenBy[i] = 0;
		}
	};

	struct TraderList
	{
		bool ok;                          // false: see 'error', lists are empty
		std::string error;
		std::string animalName;
		std::string animalSrc;
		hand animal;                      // reference animal id
		float refX, refZ;                 // animal position at build time
		int sort;                         // ListSort used
		std::vector<TraderEntry> shown;   // sorted, ready for the window
		std::vector<TraderEntry> hidden;  // filtered out (hideReason != HIDE_NONE), for diagnostics
		TraderListStats st;
		TraderList() : ok(false), refX(0.0f), refZ(0.0f), sort(0) {}
	};

	// Builds the list from the loaded characters. sortOverride: a ListSort value
	// or -1 to use list_sort from the config. Filters come from the config.
	// Returns out.ok.
	// Stage 6: 'animal' = the pack animal of the inventory window (hand copied from the UI);
	// NULL = choose like the stage 3 commands (selected animal, else first in the squad).
	bool BuildTraderList(TraderList& out, int sortOverride, const hand* animal = NULL);

	// Stage 9 (QA-07): true when the trader's faction or his town's faction is an enemy of the player
	// (same rule as TraderEntry::hostile). false when the relation is unknown. Pointer used only inside the call.
	bool TraderIsHostile(Character* trader);

	// Command "list [distance|money|town]": builds the list and writes it to the log.
	void TradersListCommand(const std::string& arg);
}

#endif
