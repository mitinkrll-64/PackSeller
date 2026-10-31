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

#include <Debug.h>
#include <core/Functions.h>
#include <kenshi/GameWorld.h>
#include <kenshi/Globals.h>

#include <windows.h>
#include <stdio.h>
#include <string>

#include "Config.h"
#include "Log.h"
#include "MoveFsm.h"
#include "Order.h"
#include "Persist.h"
#include "TownList.h"
#include "ZoneProbe.h"
#include "Probe.h"
#include "Sell.h"
#include "TraderList.h"
#include "Ui.h"

static const char* const kPackSellerVersion = "0.2.0-dev";

// Hook H1 (see docs/api-notes.md, "Hooks"): main thread tick.
static void (*mainLoop_orig)(GameWorld*, float) = NULL;

// ---- debug command file ----------------------------------------------------
// Debug trigger without a verified keyboard API: create a file named
// PackSeller.cmd next to the DLL containing one word: go | dump | reload | traders | towns | prices | tradewin [on|off] | sell | sell dry | list [distance|money|town] | order [index|hand|status].
// The plugin reads it from the tick hook and deletes it (decisions.md, stage 3).
static std::string g_cmdPath;
static DWORD g_lastCmdPoll = 0;

static std::string lowerTrim(const std::string& s)
{
	size_t b = s.find_first_not_of(" \t\r\n");
	if (b == std::string::npos)
		return std::string();
	size_t e = s.find_last_not_of(" \t\r\n");
	std::string r = s.substr(b, e - b + 1);
	for (size_t i = 0; i < r.size(); ++i)
		if (r[i] >= 'A' && r[i] <= 'Z')
			r[i] = (char)(r[i] - 'A' + 'a');
	return r;
}

static void pollCommand()
{
	DWORD now = GetTickCount();
	if (now - g_lastCmdPoll < (DWORD)PS::GetConfig().cmdPollMs)
		return;
	g_lastCmdPoll = now;

	if (GetFileAttributesA(g_cmdPath.c_str()) == INVALID_FILE_ATTRIBUTES)
		return;

	std::string word;
	FILE* f = fopen(g_cmdPath.c_str(), "rb");
	if (f)
	{
		char buf[64] = { 0 };
		size_t n = fread(buf, 1, sizeof(buf) - 1, f);
		buf[n] = '\0';
		fclose(f);
		word = lowerTrim(buf);
	}
	// delete first so a crash in the command cannot cause an endless loop
	if (!DeleteFileA(g_cmdPath.c_str()))
		PS::LogError("cannot delete %s (error %u)", g_cmdPath.c_str(), (unsigned)GetLastError());

	PS::LogInfo("command file: '%s'", word.c_str());
	if (word == "go")
		PS::MoveFsmStart();
	else if (word == "dump")
		PS::MoveFsmDump();
	else if (word == "reload")
		PS::LoadConfig();
	else if (word == "traders")
		PS::ProbeTraders();
	else if (word == "towns")
		PS::ProbeTowns();
	else if (word == "prices")
		PS::ProbePrices();
	else if (word == "tradewin")
		PS::ProbeTradeWinToggle(-1);
	else if (word == "tradewin on")
		PS::ProbeTradeWinToggle(1);
	else if (word == "tradewin off")
		PS::ProbeTradeWinToggle(0);
	else if (word == "sell dry")
		PS::SellRun(true);
	else if (word == "sell")
		PS::SellRun(false);
	else if (word == "list towns")
		PS::TownsListCommand();
	else if (word == "list" || word.compare(0, 5, "list ") == 0)
		PS::TradersListCommand(lowerTrim(word.substr(4)));
	else if (word == "zone")
		PS::ZoneProbeZone();
	else if (word == "gotown" || word.compare(0, 7, "gotown ") == 0)
		PS::ZoneProbeGoTown(lowerTrim(word.substr(6)));
	else if (word == "save status")
		PS::PersistStatusCommand();
	else if (word == "order" || word.compare(0, 6, "order ") == 0)
		PS::OrderCommand(lowerTrim(word.substr(5)));
	else
		PS::LogInfo("unknown command '%s' (use go | dump | reload | traders | towns | prices | tradewin [on|off] | sell | sell dry | list [distance|money|town] | order [index|hand|status|town <row|name>] | list towns | save status | zone | gotown <row|name>)", word.c_str());
}

static void mainLoop_hook(GameWorld* self, float time)
{
	mainLoop_orig(self, time);

	try
	{
		if (PS::GetConfig().debugCommands)
			pollCommand();
		PS::MoveFsmTick(time);
		PS::OrderTick(time);
		PS::ZoneProbeTick(time);
		if (PS::GetConfig().debugCommands)
			PS::ProbeTick();
		PS::UiTick(time);
	}
	catch (...)
	{
		static bool reported = false;
		if (!reported)
		{
			reported = true;
			PS::LogError("exception in tick hook");
		}
	}
}

// GetRealAddress must receive the member pointer directly (see api-notes, section 9).
#define PS_HOOK(target, detour, original) \
	(KenshiLib::SUCCESS == KenshiLib::AddHook(KenshiLib::GetRealAddress(target), (void*)(detour), (void**)(original)))

// Entry point called by RE_Kenshi when the plugin is loaded.
__declspec(dllexport) void startPlugin()
{
	PS::LogInfo("PackSeller %s (RE_Kenshi, KenshiLib 0.5.0)", kPackSellerVersion);
	PS::LogInfo("loaded (stage 8: order saving + order cycle + movement prototype + probes + programmatic sell + trader list + UI)");
	PS::LoadConfig();
	g_cmdPath = PS::GetModuleDir() + "PackSeller.cmd";
	if (PS::GetConfig().debugCommands)
		PS::LogInfo("debug_commands=1: command file %s is read", g_cmdPath.c_str());
	else
		PS::LogInfo("debug_commands=0: PackSeller.cmd is ignored");

	if (PS_HOOK(&GameWorld::_NV_mainLoop_GPUSensitiveStuff, &mainLoop_hook, &mainLoop_orig))
		PS::LogInfo("Hooked GameWorld::_NV_mainLoop_GPUSensitiveStuff OK");
	else
		PS::LogError("failed to hook GameWorld::_NV_mainLoop_GPUSensitiveStuff");

	// Hook H2: the "Traders" button in the pack animal inventory window (ui_enabled=1)
	PS::UiInstallHook();

	// Hooks H3/H4: the active order is written into the save and read back (save_enabled=1)
	PS::PersistInstallHooks();
}
