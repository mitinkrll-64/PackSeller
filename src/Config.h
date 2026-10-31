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

#ifndef PACKSELLER_CONFIG_H
#define PACKSELLER_CONFIG_H

#include <string>

namespace PS
{
	struct Config
	{
		// distance of the test point from the start, game units
		// (unit size in metres is [TO VERIFY], see api-notes)
		float testDistance;
		// order timeout, seconds of accumulated GameWorld tick time
		// (hard real-time cap is 5x this value)
		float timeoutSeconds;
		// arrival threshold (XZ plane), game units
		float arrivalRadius;
		// "probably stuck" must last this long (accumulated tick seconds) to abort
		float stuckSeconds;
		// FSM update period, real milliseconds
		int updateIntervalMs;
		// command file poll period, real milliseconds
		int cmdPollMs;
		// status line period in debug log, real milliseconds
		int statusIntervalMs;
		// verbose log
		bool debugLog;
		// show in-game messages (showPlayerAMessage)
		bool showMessages;
		// stage 3b probes: radius around the animal/first player character, game units
		float probeRadius;
		// stage 3b probes: max budgeted lines per command (log size limiter)
		int probeMaxLines;
		// stage 3b "prices": max distance animal -> trader when no trade window is open, game units
		float probeTradeRadius;
		// stage 4 "sell": max number of item UNITS (sum of quantities) sold per command
		int sellMaxItems;
		// stage 4 "sell": 1 = calculate only, change nothing (the command "sell" obeys this; "sell dry" always does)
		bool sellDryRun;
		// stage 4: multiplier applied to getValueAll(true); 1.0 confirmed by one calibration point only
		float sellPriceMult;
		// stage 5 trader list: max distance animal -> trader's town centre (or -> trader if no town), game units
		float listRadius;
		// stage 5: hide traders with less money than this
		int listMinMoney;
		// stage 5: sort order, one of LIST_SORT_* (config values: distance | money | town)
		int listSort;
		// stage 5 filters (1/0)
		bool listHideNoMoney;
		bool listHideNothingToSell;
		bool listOnlyCity;
		// stage 6 UI
		bool uiEnabled;        // ui_enabled: install the inventory hook / show the button
		bool uiModal;          // ui_modal: MyGUI addWidgetModal for the trader window (not confirmed in game yet)
		std::string uiLanguage; // ui_language: auto | en | ru
		int uiButtonX;         // ui_button_x: manual button offset from OpenBagButton (0/0 = automatic placement)
		int uiButtonY;
		// stage 7 order cycle (Idle -> Going -> Selling -> Returning -> Idle)
		bool orderEnabled;          // order_enabled: the window's Sell button starts the cycle (0 = dry-run calculation only)
		float tradeRange;           // trade_range: "in the interaction zone" = distance to the trader <= this, game units
		float returnRange;          // return_range: the order ended / stopped this close to the start point = back, game units
		int retryOrders;            // retry_orders: how many times a stopped move order is re-sent per phase
		float orderTimeoutSeconds;  // order_timeout_seconds: per phase (Going, Returning), accumulated tick seconds
		float approachRadius;       // approach_radius: ring radius around a trader inside a building, 0 = trade_range * 0.9
		// 0.2 probe (debug commands zone / gotown)
		float probeMaxDistance;     // probe_max_distance: gotown refuses a town farther than this, game units (hard maximum 20000)
		float probeTimeoutSeconds;  // probe_timeout_seconds: ACTIVE real seconds per phase of the gotown walk
		bool probeReturnHome;       // probe_return_home: 1 = the animal walks back to the start after the probe
		bool debugCommands;         // debug_commands: 1 = PackSeller.cmd debug commands are read (default 0)
		int hostilePolicy;          // hostile_policy: HOSTILE_BLOCK | HOSTILE_WARN | HOSTILE_ALLOW
		bool saveEnabled;           // save_enabled: install the save/load hooks (0 = the order is not saved)
		bool approachEnabled;       // approach_enabled: 0 = go to the trader position like before the approach logic
		bool approachCheckPath;     // approach_check_path: log pathExists() of the SELECTED candidate (one call per selection; diagnostic only)
		int approachCandidates;     // approach_candidates: points on the ring (4..32)
		int orderMaxItems;          // order_max_items: max item UNITS sold per cycle (replaces sell_max_items for the cycle)

		Config();
	};

	enum HostilePolicy
	{
		HOSTILE_BLOCK = 0,
		HOSTILE_WARN = 1,
		HOSTILE_ALLOW = 2
	};

	enum ListSort
	{
		LIST_SORT_DISTANCE = 0,
		LIST_SORT_MONEY = 1,
		LIST_SORT_TOWN = 2
	};

	// "distance" | "money" | "town" -> ListSort, -1 if unknown
	int ParseListSort(const std::string& v);
	const char* ListSortName(int sort);

	// directory of PackSeller.dll, with trailing slash
	std::string GetModuleDir();
	// current config (defaults until LoadConfig is called)
	Config& GetConfig();
	// (re)load PackSeller.cfg from the DLL directory; missing file = defaults
	void LoadConfig();
}

#endif
