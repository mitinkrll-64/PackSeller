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

#ifndef PACKSELLER_SELL_H
#define PACKSELLER_SELL_H

// Stage 4: programmatic sale of the pack animal's items to one trader
// (no trade window, no animal movement). Must be called from the game main
// thread only (from the tick hook).

#include <string>

#include <kenshi/util/hand.h>

class Character;

namespace PS
{
	// Stage 6: result of one SellRunFor call (for the UI message). Plain values only.
	struct SellOutcome
	{
		bool ran;             // the plan phase was reached (world ready, no exception)
		bool planned;         // a plan was built (animal and trader resolved)
		bool dry;             // nothing was changed
		bool aborted;         // a real sale was refused or failed
		std::string animalName;
		std::string traderName;
		int planUnits;        // units in the plan
		int planRevenue;      // expected revenue
		int soldUnits;        // real sale only
		int soldMoney;
		std::string error;    // short English reason for the log when !planned
		SellOutcome() : ran(false), planned(false), dry(true), aborted(false), planUnits(0), planRevenue(0), soldUnits(0), soldMoney(0) {}
	};

	// Stage 5 (trader list): for each trader tells whether the pack animal has at
	// least one unit the trader would buy under the stage 4 rules (dry run, silent,
	// nothing is changed, no revenue is reported). result[i]: 1 yes, 0 no, -1 unknown
	// (animal inventory unreadable or an exception). Pointers must come from the
	// current frame; none is stored.
	void SellCheckTraders(Character* animal, Character* const* traders, int count, int* result);

	// forceDry = true: calculate and log only (command "sell dry").
	// forceDry = false: command "sell"; obeys sell_dry_run from the config.
	void SellRun(bool forceDry);

	// Stage 6: same as SellRun but for a given pack animal and trader (hands from the UI).
	// A NULL hand pointer means "choose automatically" exactly like SellRun. The trader is
	// NOT replaced by the trade-window trader or the nearest one when 'trader' is given.
	// outcome may be NULL. Selling rules are unchanged.
	// Stage 7: maxItems > 0 replaces sell_max_items for this call (order cycle: order_max_items);
	// notify = false suppresses the English "Sold N item(s) for M" message (the order cycle shows its own).
	void SellRunFor(bool forceDry, const hand* animal, const hand* trader, SellOutcome* outcome, int maxItems = 0, bool notify = true);
}

#endif
