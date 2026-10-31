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

// Stage 8: saving the order.
//
// The scheme is the one shown by the KenshiLib WorldStates example and used by the kenshi-achievements plugin
// (both GPLv3, described in docs/api-notes.md section 7; no code is copied): the save contains one extra
// GameData record of an item type that only this plugin knows. Records of unknown types are dropped by the
// game when it saves without the plugin (claim of the achievements author, [TO VERIFY] in game, test-plan V8-6).
//
// Writing: hook on FactionManager::saveGameState, BEFORE the original, creates the record only while an order is
// active (so a save without an order is identical to a save without the mod). Only plain values are read
// (OrderTakeSnapshot); no game object is touched, because the save may run outside the main loop.
// Reading: hook on GameWorld::loadAllPlatoons, AFTER the original, looks the record up in ou->savedata and
// registers a pending restore; Order.cpp restores it from the tick when the animal and trader exist.

#include "Persist.h"
#include "Seh.h"
#include "Config.h"
#include "Log.h"
#include "Order.h"

#include <Debug.h>
#include <core/Functions.h>
#include <kenshi/GameWorld.h>
#include <kenshi/Globals.h>
#include <kenshi/Faction.h>
#include <kenshi/GameData.h>
#include <kenshi/GameDataManager.h>

#include <windows.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <string>

namespace PS
{
	// Own item type: KenshiLib asks for a number between 1100 and 55000 (api-notes section 7); not 4242 (achievements) and not 900.
	static const int kSaveItemType = 20717;
	static const char* kSaveSid = "PackSeller_orders";

	static bool g_hookSaveOk = false;
	static bool g_hookLoadOk = false;
	static char g_lastSave[240] = "no save yet";
	static char g_lastLoad[240] = "no load yet";
	static unsigned g_saveCount = 0;
	static unsigned g_loadCount = 0;

	// ---- SEH wrapper (no C++ objects with destructors in this function) ----------

	typedef void (*PersistFn)(void*);

	static bool persistSafeRun(PersistFn fn, void* ctx, const char* what)
	{
		__try
		{
			fn(ctx);
			return true;
		}
		__except (PsSehFilterCode(GetExceptionCode()))
		{
			LogError("persist: EXCEPTION 0x%08X in %s", (unsigned)GetExceptionCode(), what);
			return false;
		}
	}

	static void setText(char* dst, size_t n, const char* fmt, ...)
	{
		va_list args;
		va_start(args, fmt);
		int r = _vsnprintf(dst, n - 1, fmt, args);
		va_end(args);
		if (r < 0)
			r = (int)n - 1;
		dst[r] = '\0';
	}

	// ---- save ------------------------------------------------------------------------------

	namespace
	{
	struct PersistSaveCtx
	{
		GameDataContainer* container;
		int result;      // 0 nothing to save, 1 written, -1 createNewData failed
		OrderSnapshot snap;
	};
	}

	static void writeFn(void* vp)
	{
		PersistSaveCtx* x = (PersistSaveCtx*)vp;
		x->result = 0;
		if (x->container == NULL || !OrderTakeSnapshot(&x->snap))
			return;
		GameData* d = x->container->createNewData((itemType)kSaveItemType, std::string(kSaveSid), std::string("PackSeller order"));
		if (d == NULL)
		{
			x->result = -1;
			return;
		}
		const OrderSnapshot& s = x->snap;
		d->idata.clear();
		d->fdata.clear();
		d->sdata.clear();
		d->idata[std::string("version")] = s.version;
		d->idata[std::string("state")] = s.state;
		d->idata[std::string("retries")] = s.retries;
		d->fdata[std::string("phase")] = s.phaseSeconds;
		d->fdata[std::string("startX")] = s.start[0];
		d->fdata[std::string("startY")] = s.start[1];
		d->fdata[std::string("startZ")] = s.start[2];
		d->fdata[std::string("destX")] = s.dest[0];
		d->fdata[std::string("destY")] = s.dest[1];
		d->fdata[std::string("destZ")] = s.dest[2];
		d->sdata[std::string("animal")] = std::string(s.animal);
		d->sdata[std::string("trader")] = std::string(s.trader);
		x->result = 1;
	}

	static void (*saveGameState_orig)(FactionManager*, GameDataContainer*) = NULL;

	static void saveGameState_hook(FactionManager* self, GameDataContainer* container)
	{
		PersistSaveCtx x;
		memset(&x.snap, 0, sizeof(x.snap));
		x.container = container;
		x.result = 0;
		++g_saveCount;
		if (persistSafeRun(writeFn, &x, "save (write record)"))
		{
			if (x.result == 1)
			{
				setText(g_lastSave, sizeof(g_lastSave), "#%u wrote record '%s' type=%d version=%d state=%d animal=%s trader=%s retries=%d phase=%.1fs",
					g_saveCount, kSaveSid, kSaveItemType, x.snap.version, x.snap.state, x.snap.animal, x.snap.trader, x.snap.retries, x.snap.phaseSeconds);
				LogInfo("persist: SAVE %s", g_lastSave);
			}
			else if (x.result < 0)
			{
				setText(g_lastSave, sizeof(g_lastSave), "#%u createNewData returned NULL, the order was NOT saved", g_saveCount);
				LogError("persist: SAVE %s", g_lastSave);
			}
			else
			{
				setText(g_lastSave, sizeof(g_lastSave), "#%u no active order, nothing written", g_saveCount);
				LogInfo("persist: SAVE %s", g_lastSave);
			}
		}
		else
		{
			setText(g_lastSave, sizeof(g_lastSave), "#%u exception while writing, the order was NOT saved", g_saveCount);
		}
		// the original always runs, whatever happened above
		saveGameState_orig(self, container);
	}

	// ---- load ------------------------------------------------------------------------------

	namespace
	{
	struct PersistLoadCtx
	{
		int result;           // 0 no record, 1 pending registered, -1 record invalid
		char why[160];
		OrderSnapshot snap;
	};
	}

	template<class M>
	static bool readInt(const M& m, const char* key, int* out)
	{
		typename M::const_iterator it = m.find(std::string(key));
		if (it == m.end())
			return false;
		*out = it->second;
		return true;
	}

	template<class M>
	static bool readFloat(const M& m, const char* key, float* out)
	{
		typename M::const_iterator it = m.find(std::string(key));
		if (it == m.end())
			return false;
		float v = it->second;
		if (v != v || v > 1.0e9f || v < -1.0e9f)
			return false;
		*out = v;
		return true;
	}

	template<class M>
	static bool readText(const M& m, const char* key, char* out, size_t n)
	{
		typename M::const_iterator it = m.find(std::string(key));
		if (it == m.end() || it->second.empty() || it->second.size() >= n)
			return false;
		strncpy(out, it->second.c_str(), n - 1);
		out[n - 1] = '\0';
		return true;
	}

	template<class M>
	static GameData* findSid(const M& m, const char* sid)
	{
		typename M::const_iterator it = m.find(std::string(sid));
		return it == m.end() ? NULL : it->second;
	}

	static void readFn(void* vp)
	{
		PersistLoadCtx* x = (PersistLoadCtx*)vp;
		x->result = 0;
		x->why[0] = '\0';
		if (ou == NULL)
			return;
		GameData* d = findSid(ou->savedata.gamedataSID, kSaveSid);
		if (d == NULL)
			return;
		OrderSnapshot& s = x->snap;
		memset(&s, 0, sizeof(s));
		bool ok = readInt(d->idata, "version", &s.version) && readInt(d->idata, "state", &s.state) && readInt(d->idata, "retries", &s.retries)
			&& readFloat(d->fdata, "phase", &s.phaseSeconds)
			&& readFloat(d->fdata, "startX", &s.start[0]) && readFloat(d->fdata, "startY", &s.start[1]) && readFloat(d->fdata, "startZ", &s.start[2])
			&& readFloat(d->fdata, "destX", &s.dest[0]) && readFloat(d->fdata, "destY", &s.dest[1]) && readFloat(d->fdata, "destZ", &s.dest[2])
			&& readText(d->sdata, "animal", s.animal, sizeof(s.animal)) && readText(d->sdata, "trader", s.trader, sizeof(s.trader));
		if (!ok)
		{
			x->result = -1;
			_snprintf(x->why, sizeof(x->why) - 1, "record '%s' found but a field is missing or invalid", kSaveSid);
			return;
		}
		if (s.version != kOrderFormatVersion)
		{
			x->result = -1;
			_snprintf(x->why, sizeof(x->why) - 1, "unsupported format version %d (this build reads %d)", s.version, kOrderFormatVersion);
			return;
		}
		if (s.state < 1 || s.state > 3 || s.retries < 0 || s.retries > 1000)
		{
			x->result = -1;
			_snprintf(x->why, sizeof(x->why) - 1, "state=%d retries=%d out of range", s.state, s.retries);
			return;
		}
		x->result = 1;
	}

	namespace
	{
	class GameWorldLoadAccess : public GameWorld
	{
	public:
		using GameWorld::loadAllPlatoons;
	};
	}

	static void (*loadAllPlatoons_orig)(GameWorld*) = NULL;

	static void loadAllPlatoons_hook(GameWorld* self)
	{
		loadAllPlatoons_orig(self);

		++g_loadCount;
		PersistLoadCtx x;
		x.result = 0;
		x.why[0] = '\0';
		memset(&x.snap, 0, sizeof(x.snap));
		if (!persistSafeRun(readFn, &x, "load (read record)"))
		{
			OrderClearPending();
			setText(g_lastLoad, sizeof(g_lastLoad), "#%u exception while reading, no order restored", g_loadCount);
			return;
		}
		if (x.result == 1)
		{
			OrderSetPending(x.snap);
			setText(g_lastLoad, sizeof(g_lastLoad), "#%u record found: version=%d state=%d animal=%s trader=%s, restore is pending",
				g_loadCount, x.snap.version, x.snap.state, x.snap.animal, x.snap.trader);
			LogInfo("persist: LOAD %s", g_lastLoad);
		}
		else if (x.result < 0)
		{
			OrderClearPending();
			setText(g_lastLoad, sizeof(g_lastLoad), "#%u record rejected: %s", g_loadCount, x.why);
			LogInfo("persist: LOAD %s", g_lastLoad);
		}
		else
		{
			OrderClearPending();
			setText(g_lastLoad, sizeof(g_lastLoad), "#%u no record (old save, save without an order, or the mod was off): Idle", g_loadCount);
			LogInfo("persist: LOAD %s", g_lastLoad);
		}
	}

	// ---- install / status -----------------------------------------------------------------------

	unsigned PersistLoadCount()
	{
		return g_loadCount;
	}

	bool PersistLoadDetectionActive()
	{
		return g_hookLoadOk;
	}

	bool PersistInstallHooks()
	{
		if (!GetConfig().saveEnabled)
		{
			LogInfo("persist: disabled by save_enabled=0, hooks H3/H4 not installed (the order is not saved)");
			return false;
		}
		// GetRealAddress must receive the member pointer directly at the point of use (api-notes section 9).
		if (KenshiLib::SUCCESS == KenshiLib::AddHook(KenshiLib::GetRealAddress(&FactionManager::saveGameState),
			(void*)&saveGameState_hook, (void**)&saveGameState_orig))
		{
			g_hookSaveOk = true;
			LogInfo("Hooked FactionManager::saveGameState OK");
		}
		else
			LogError("failed to hook FactionManager::saveGameState (the order will not be saved)");
		if (KenshiLib::SUCCESS == KenshiLib::AddHook(KenshiLib::GetRealAddress(&GameWorldLoadAccess::loadAllPlatoons),
			(void*)&loadAllPlatoons_hook, (void**)&loadAllPlatoons_orig))
		{
			g_hookLoadOk = true;
			LogInfo("Hooked GameWorld::loadAllPlatoons OK");
		}
		else
			LogError("failed to hook GameWorld::loadAllPlatoons (a saved order will not be restored)");
		return g_hookSaveOk && g_hookLoadOk;
	}

	void PersistStatusCommand()
	{
		LogInfo("save status: format version=%d item type=%d sid=%s | hook save=%d hook load=%d | saves seen=%u loads seen=%u",
			kOrderFormatVersion, kSaveItemType, kSaveSid, g_hookSaveOk ? 1 : 0, g_hookLoadOk ? 1 : 0, g_saveCount, g_loadCount);
		LogInfo("save status: last save: %s", g_lastSave);
		LogInfo("save status: last load: %s", g_lastLoad);
		OrderPersistStatus();
	}
}
