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

#include "Config.h"
#include "Log.h"

#include <windows.h>
#include <stdlib.h>
#include <fstream>
#include <string>

namespace PS
{
	Config::Config()
		: testDistance(30.0f)
		, timeoutSeconds(60.0f)
		, arrivalRadius(5.0f)
		, stuckSeconds(5.0f)
		, updateIntervalMs(200)
		, cmdPollMs(500)
		, statusIntervalMs(2000)
		, debugLog(false)
		, showMessages(true)
		, probeRadius(20000.0f)
		, probeMaxLines(800)
		, probeTradeRadius(1000.0f)
		, sellMaxItems(1)
		, sellDryRun(false)
		, sellPriceMult(1.0f)
		, listRadius(100000.0f)
		, listMinMoney(100)
		, listSort(LIST_SORT_DISTANCE)
		, listHideNoMoney(true)
		, listHideNothingToSell(true)
		, listOnlyCity(true)
		, uiEnabled(true)
		, uiModal(false)
		, uiLanguage("auto")
		, uiButtonX(0)
		, uiButtonY(0)
		, orderEnabled(true)
		, tradeRange(150.0f)
		, returnRange(60.0f)
		, retryOrders(4)
		, orderTimeoutSeconds(300.0f)
		, approachRadius(0.0f)
		, listFarDistance(3000.0f)
		, listVeryFarDistance(10000.0f)
		, maxTripDistance(40000.0f)
		, orderSpeedEstimate(40.0f)
		, orderTimeoutFactor(2.5f)
		, persistResumeSeconds(10.0f)
		, probeMaxDistance(6000.0f)
		, probeTimeoutSeconds(600.0f)
		, probeReturnHome(true)
		, debugCommands(false)
		, hostilePolicy(HOSTILE_BLOCK)
		, saveEnabled(true)
		, approachEnabled(true)
		, approachCheckPath(false)
		, approachCandidates(12)
		, orderMaxItems(200)
	{
	}

	static Config g_config;

	// ---- range checks with a log line when a value is corrected ----

	static void clampF(float& v, float lo, float hi, float def, const char* name)
	{
		if (v != v || v > 3.0e38f || v < -3.0e38f)
		{
			LogInfo("config: %s is not a finite number, using %.3f", name, def);
			v = def;
			return;
		}
		if (v < lo)
		{
			LogInfo("config: %s=%.3f is below the minimum, using %.3f", name, v, lo);
			v = lo;
		}
		else if (v > hi)
		{
			LogInfo("config: %s=%.3f is above the maximum, using %.3f", name, v, hi);
			v = hi;
		}
	}

	static void clampI(int& v, int lo, int hi, const char* name)
	{
		if (v < lo)
		{
			LogInfo("config: %s=%d is below the minimum, using %d", name, v, lo);
			v = lo;
		}
		else if (v > hi)
		{
			LogInfo("config: %s=%d is above the maximum, using %d", name, v, hi);
			v = hi;
		}
	}

	Config& GetConfig()
	{
		return g_config;
	}

	std::string GetModuleDir()
	{
		HMODULE hm = NULL;
		char path[MAX_PATH] = { 0 };
		if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			(LPCSTR)&GetModuleDir, &hm))
			return std::string();
		GetModuleFileNameA(hm, path, MAX_PATH);
		std::string s(path);
		size_t slash = s.find_last_of("\\/");
		return slash == std::string::npos ? std::string() : s.substr(0, slash + 1);
	}

	static std::string trim(const std::string& s)
	{
		size_t b = s.find_first_not_of(" \t\r\n");
		if (b == std::string::npos)
			return std::string();
		size_t e = s.find_last_not_of(" \t\r\n");
		return s.substr(b, e - b + 1);
	}

	static bool parseBool(const std::string& v)
	{
		return v == "1" || v == "true" || v == "yes" || v == "on";
	}

	int ParseListSort(const std::string& v)
	{
		if (v == "distance" || v == "dist") return LIST_SORT_DISTANCE;
		if (v == "money") return LIST_SORT_MONEY;
		if (v == "town" || v == "city") return LIST_SORT_TOWN;
		if (v == "name") return LIST_SORT_NAME;
		if (v == "faction") return LIST_SORT_FACTION;
		return -1;
	}

	const char* ListSortName(int sort)
	{
		switch (sort)
		{
		case LIST_SORT_DISTANCE: return "distance";
		case LIST_SORT_MONEY: return "money";
		case LIST_SORT_TOWN: return "town";
		case LIST_SORT_NAME: return "name";
		case LIST_SORT_FACTION: return "faction";
		}
		return "?";
	}

	void LoadConfig()
	{
		Config c; // defaults
		std::string path = GetModuleDir() + "PackSeller.cfg";
		std::ifstream in(path.c_str());
		if (!in)
		{
			g_config = c;
			LogInfo("config: %s not found, using defaults", path.c_str());
		}
		else
		{
			std::string line;
			bool firstLine = true;
			while (std::getline(in, line))
			{
				if (firstLine)
				{
					firstLine = false;
					if (line.size() >= 3 && (unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF)
					{
						line = line.substr(3);
						LogInfo("config: UTF-8 BOM skipped at the start of the file");
					}
				}
				line = trim(line);
				if (line.empty() || line[0] == '#' || line[0] == ';')
					continue;
				size_t eq = line.find('=');
				if (eq == std::string::npos)
				{
					LogInfo("config: ignored line without '=': %s", line.c_str());
					continue;
				}
				std::string key = trim(line.substr(0, eq));
				std::string val = trim(line.substr(eq + 1));
				if (key == "test_distance") c.testDistance = (float)atof(val.c_str());
				else if (key == "timeout_seconds") c.timeoutSeconds = (float)atof(val.c_str());
				else if (key == "arrival_radius") c.arrivalRadius = (float)atof(val.c_str());
				else if (key == "stuck_seconds") c.stuckSeconds = (float)atof(val.c_str());
				else if (key == "update_interval_ms") c.updateIntervalMs = atoi(val.c_str());
				else if (key == "cmd_poll_ms") c.cmdPollMs = atoi(val.c_str());
				else if (key == "status_interval_ms") c.statusIntervalMs = atoi(val.c_str());
				else if (key == "debug_log") c.debugLog = parseBool(val);
				else if (key == "show_messages") c.showMessages = parseBool(val);
				else if (key == "probe_radius") c.probeRadius = (float)atof(val.c_str());
				else if (key == "probe_max_lines") c.probeMaxLines = atoi(val.c_str());
				else if (key == "probe_trade_radius") c.probeTradeRadius = (float)atof(val.c_str());
				else if (key == "sell_max_items") c.sellMaxItems = atoi(val.c_str());
				else if (key == "sell_dry_run") c.sellDryRun = parseBool(val);
				else if (key == "sell_price_mult") c.sellPriceMult = (float)atof(val.c_str());
				else if (key == "list_radius") c.listRadius = (float)atof(val.c_str());
				else if (key == "list_min_money") c.listMinMoney = atoi(val.c_str());
				else if (key == "list_sort")
				{
					int sv = ParseListSort(val);
					if (sv < 0)
						LogInfo("config: list_sort='%s' is not distance|money|town, keeping the default", val.c_str());
					else
						c.listSort = sv;
				}
				else if (key == "list_hide_no_money") c.listHideNoMoney = parseBool(val);
				else if (key == "list_hide_nothing_to_sell") c.listHideNothingToSell = parseBool(val);
				else if (key == "list_only_city") c.listOnlyCity = parseBool(val);
				else if (key == "ui_enabled") c.uiEnabled = parseBool(val);
				else if (key == "ui_modal") c.uiModal = parseBool(val);
				else if (key == "ui_button_x") c.uiButtonX = atoi(val.c_str());
				else if (key == "ui_button_y") c.uiButtonY = atoi(val.c_str());
				else if (key == "order_enabled") c.orderEnabled = parseBool(val);
				else if (key == "trade_range") c.tradeRange = (float)atof(val.c_str());
				else if (key == "return_range") c.returnRange = (float)atof(val.c_str());
				else if (key == "retry_orders") c.retryOrders = atoi(val.c_str());
				else if (key == "order_timeout_seconds") c.orderTimeoutSeconds = (float)atof(val.c_str());
				else if (key == "approach_radius") c.approachRadius = (float)atof(val.c_str());
				else if (key == "list_far_distance") c.listFarDistance = (float)atof(val.c_str());
				else if (key == "list_very_far_distance") c.listVeryFarDistance = (float)atof(val.c_str());
				else if (key == "max_trip_distance") c.maxTripDistance = (float)atof(val.c_str());
				else if (key == "order_speed_estimate") c.orderSpeedEstimate = (float)atof(val.c_str());
				else if (key == "order_timeout_factor") c.orderTimeoutFactor = (float)atof(val.c_str());
				else if (key == "persist_resume_seconds") c.persistResumeSeconds = (float)atof(val.c_str());
				else if (key == "probe_max_distance") c.probeMaxDistance = (float)atof(val.c_str());
				else if (key == "probe_timeout_seconds") c.probeTimeoutSeconds = (float)atof(val.c_str());
				else if (key == "probe_return_home") c.probeReturnHome = parseBool(val);
				else if (key == "debug_commands") c.debugCommands = parseBool(val);
				else if (key == "hostile_policy")
				{
					std::string hv;
					for (size_t i = 0; i < val.size(); ++i)
						hv += (char)((val[i] >= 'A' && val[i] <= 'Z') ? val[i] - 'A' + 'a' : val[i]);
					if (hv == "block") c.hostilePolicy = HOSTILE_BLOCK;
					else if (hv == "warn") c.hostilePolicy = HOSTILE_WARN;
					else if (hv == "allow") c.hostilePolicy = HOSTILE_ALLOW;
					else LogInfo("config: hostile_policy='%s' is not block|warn|allow, keeping 'block'", val.c_str());
				}
				else if (key == "save_enabled") c.saveEnabled = parseBool(val);
				else if (key == "approach_enabled") c.approachEnabled = parseBool(val);
				else if (key == "approach_check_path") c.approachCheckPath = parseBool(val);
				else if (key == "approach_candidates") c.approachCandidates = atoi(val.c_str());
				else if (key == "order_max_items") c.orderMaxItems = atoi(val.c_str());
				else if (key == "ui_language")
				{
					std::string lv;
					for (size_t i = 0; i < val.size(); ++i)
						lv += (char)((val[i] >= 'A' && val[i] <= 'Z') ? val[i] - 'A' + 'a' : val[i]);
					if (lv == "auto" || lv == "en" || lv == "ru")
						c.uiLanguage = lv;
					else
						LogInfo("config: ui_language='%s' is not auto|en|ru, keeping 'auto'", val.c_str());
				}
				else LogInfo("config: unknown key '%s'", key.c_str());
			}
			// sanity limits (finite numbers, ranges); a corrected value is logged
			const Config d;
			clampF(c.testDistance, 2.0f, 5000.0f, d.testDistance, "test_distance");
			clampF(c.timeoutSeconds, 5.0f, 3600.0f, d.timeoutSeconds, "timeout_seconds");
			clampF(c.arrivalRadius, 0.5f, 200.0f, d.arrivalRadius, "arrival_radius");
			clampF(c.stuckSeconds, 1.0f, 120.0f, d.stuckSeconds, "stuck_seconds");
			clampI(c.updateIntervalMs, 20, 5000, "update_interval_ms");
			clampI(c.cmdPollMs, 100, 10000, "cmd_poll_ms");
			clampI(c.statusIntervalMs, 200, 60000, "status_interval_ms");
			clampF(c.probeRadius, 100.0f, 100000.0f, d.probeRadius, "probe_radius");
			clampI(c.probeMaxLines, 20, 20000, "probe_max_lines");
			clampF(c.probeTradeRadius, 10.0f, 20000.0f, d.probeTradeRadius, "probe_trade_radius");
			clampI(c.sellMaxItems, 1, 100000, "sell_max_items");
			clampF(c.sellPriceMult, 0.01f, 10.0f, d.sellPriceMult, "sell_price_mult");
			clampF(c.listRadius, 100.0f, 200000.0f, d.listRadius, "list_radius");
			clampI(c.listMinMoney, 0, 1000000000, "list_min_money");
			clampI(c.uiButtonX, -2000, 2000, "ui_button_x");
			clampI(c.uiButtonY, -2000, 2000, "ui_button_y");
			clampF(c.tradeRange, 10.0f, 2000.0f, d.tradeRange, "trade_range");
			clampF(c.returnRange, c.arrivalRadius, 2000.0f, d.returnRange, "return_range");
			clampI(c.retryOrders, 0, 20, "retry_orders");
			clampF(c.orderTimeoutSeconds, 10.0f, 3600.0f, d.orderTimeoutSeconds, "order_timeout_seconds");
			clampI(c.orderMaxItems, 1, 100000, "order_max_items");
			clampF(c.approachRadius, 0.0f, 2000.0f, d.approachRadius, "approach_radius");
			clampI(c.approachCandidates, 4, 32, "approach_candidates");
			clampF(c.listFarDistance, 100.0f, 200000.0f, d.listFarDistance, "list_far_distance");
			clampF(c.listVeryFarDistance, 100.0f, 200000.0f, d.listVeryFarDistance, "list_very_far_distance");
			clampF(c.maxTripDistance, 500.0f, 200000.0f, d.maxTripDistance, "max_trip_distance");
			clampF(c.orderSpeedEstimate, 5.0f, 200.0f, d.orderSpeedEstimate, "order_speed_estimate");
			clampF(c.orderTimeoutFactor, 1.0f, 10.0f, d.orderTimeoutFactor, "order_timeout_factor");
			clampF(c.persistResumeSeconds, 1.0f, 120.0f, d.persistResumeSeconds, "persist_resume_seconds");
			clampF(c.probeMaxDistance, 100.0f, 20000.0f, d.probeMaxDistance, "probe_max_distance");
			clampF(c.probeTimeoutSeconds, 30.0f, 7200.0f, d.probeTimeoutSeconds, "probe_timeout_seconds");
			g_config = c;
			LogInfo("config: loaded %s", path.c_str());
		}
		LogInfo("config: test_distance=%.1f timeout_seconds=%.1f arrival_radius=%.1f stuck_seconds=%.1f "
			"update_interval_ms=%d cmd_poll_ms=%d status_interval_ms=%d debug_log=%d show_messages=%d "
			"probe_radius=%.0f probe_max_lines=%d probe_trade_radius=%.0f "
			"sell_max_items=%d sell_dry_run=%d sell_price_mult=%.3f "
			"list_radius=%.0f list_min_money=%d list_sort=%s list_hide_no_money=%d list_hide_nothing_to_sell=%d list_only_city=%d "
			"ui_enabled=%d ui_modal=%d ui_language=%s ui_button_x=%d ui_button_y=%d "
			"list_far_distance=%.0f list_very_far_distance=%.0f max_trip_distance=%.0f order_speed_estimate=%.0f order_timeout_factor=%.1f persist_resume_seconds=%.0f "
			"probe_max_distance=%.0f probe_timeout_seconds=%.0f probe_return_home=%d debug_commands=%d hostile_policy=%s "
			"order_enabled=%d trade_range=%.0f return_range=%.0f retry_orders=%d order_timeout_seconds=%.0f order_max_items=%d approach_radius=%.0f approach_candidates=%d approach_check_path=%d approach_enabled=%d save_enabled=%d",
			g_config.testDistance, g_config.timeoutSeconds, g_config.arrivalRadius, g_config.stuckSeconds,
			g_config.updateIntervalMs, g_config.cmdPollMs, g_config.statusIntervalMs,
			g_config.debugLog ? 1 : 0, g_config.showMessages ? 1 : 0,
			g_config.probeRadius, g_config.probeMaxLines, g_config.probeTradeRadius,
			g_config.sellMaxItems, g_config.sellDryRun ? 1 : 0, g_config.sellPriceMult,
			g_config.listRadius, g_config.listMinMoney, ListSortName(g_config.listSort), g_config.listHideNoMoney ? 1 : 0,
			g_config.listHideNothingToSell ? 1 : 0, g_config.listOnlyCity ? 1 : 0,
			g_config.uiEnabled ? 1 : 0, g_config.uiModal ? 1 : 0, g_config.uiLanguage.c_str(), g_config.uiButtonX, g_config.uiButtonY,
			g_config.listFarDistance, g_config.listVeryFarDistance, g_config.maxTripDistance, g_config.orderSpeedEstimate, g_config.orderTimeoutFactor, g_config.persistResumeSeconds,
			g_config.probeMaxDistance, g_config.probeTimeoutSeconds, g_config.probeReturnHome ? 1 : 0, g_config.debugCommands ? 1 : 0, g_config.hostilePolicy == HOSTILE_BLOCK ? "block" : (g_config.hostilePolicy == HOSTILE_WARN ? "warn" : "allow"),
			g_config.orderEnabled ? 1 : 0, g_config.tradeRange, g_config.returnRange, g_config.retryOrders,
			g_config.orderTimeoutSeconds, g_config.orderMaxItems, g_config.approachRadius, g_config.approachCandidates, g_config.approachCheckPath ? 1 : 0, g_config.approachEnabled ? 1 : 0, g_config.saveEnabled ? 1 : 0);
	}
}
