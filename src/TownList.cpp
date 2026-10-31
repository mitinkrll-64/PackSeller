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

// 0.2 step 3: list of towns (data only). Every per-object block runs under SEH; game pointers live only inside one
// call; the rows are plain copies.

#include "TownList.h"
#include "Config.h"
#include "Log.h"
#include "MoveFsm.h"
#include "Seh.h"

#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <map>
#include <algorithm>

#include <kenshi/GameWorld.h>
#include <kenshi/Globals.h>
#include <kenshi/SharedKing.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/Character.h>
#include <kenshi/CharacterAnimal.h>
#include <kenshi/Platoon.h>
#include <kenshi/Town.h>
#include <kenshi/Faction.h>
#include <kenshi/FactionRelations.h>
#include <kenshi/GameData.h>
#include <kenshi/util/hand.h>
#include <ogre/OgreVector3.h>

namespace PS
{
	// beyond this distance from the animal a town with no loaded traders is probably outside the loaded area ("?")
	static const float kLoadedAreaGuess = 11000.0f;
	static const DWORD kCacheMs = 2000;

	typedef void (*TownFn)(void*);

	static bool tlSafeRun(TownFn fn, void* ctx, const char* what)
	{
		__try
		{
			fn(ctx);
			return true;
		}
		__except (PsSehFilterCode(GetExceptionCode()))
		{
			LogError("towns: EXCEPTION 0x%08X in %s", (unsigned)GetExceptionCode(), what);
			return false;
		}
	}

	static float tlDist2D(float ax, float az, float bx, float bz)
	{
		float dx = ax - bx;
		float dz = az - bz;
		return sqrtf(dx * dx + dz * dz);
	}

	template<class M>
	static int tlFindBool(const M& m, const std::string& key)
	{
		typename M::const_iterator it = m.find(key);
		if (it == m.end())
			return -1;
		return it->second ? 1 : 0;
	}

	// ---- scan of the loaded traders: home town -> count (pointer compared inside this call only) ----

	namespace
	{
	struct TlScan
	{
		std::map<TownBase*, int> byTown;
	};

	struct TlChar
	{
		TlScan* s;
		Character* c;
	};

	struct TlTownCtx
	{
		TownBase* t;
		TownRow row;
		bool ok;
		bool isTownObj;
	};

	struct TlRaw
	{
		TownRow row;     // plain copy: no game pointer is kept (the cache lives 2 s)
	};
	}

	static void tlScanOne(void* vp)
	{
		TlChar* x = (TlChar*)vp;
		Character* c = x->c;
		if (c->isDead() || c->isDestroyed() || c->isAnimal() != NULL)
			return;
		int fs = (c->data != NULL) ? tlFindBool(c->data->bdata, std::string("is trader")) : -2;
		bool trader = fs == 1 ? true : (fs == 0 ? false : c->isATrader());
		if (!trader)
			return;
		ActivePlatoon* ap = c->getPlatoon();
		Platoon* p = ap != NULL ? ap->me : NULL;
		Ownerships* own = p != NULL ? p->getOwnerships() : NULL;
		TownBase* home = own != NULL ? own->_homeTown : NULL;
		if (home == NULL)
			home = c->isResident();
		if (home != NULL)
			x->s->byTown[home] += 1;
	}

	static void tlReadTown(void* vp)
	{
		TlTownCtx* x = (TlTownCtx*)vp;
		x->ok = false;
		x->isTownObj = false;
		TownBase* t = x->t;
		Town* tw = t->isTown();
		if (tw == NULL)
			return;
		x->isTownObj = true;
		TownRow& r = x->row;
		Ogre::Vector3 p = t->getPosition();
		r.cx = p.x;
		r.cy = p.y;
		r.cz = p.z;
		r.radius = t->getRadius();
		r.townType = (int)t->townType;
		r.open = tw->openToPublic;
		r.name = t->getKnownName();
		Faction* f = t->getFaction();
		if (f != NULL)
		{
			r.faction = f->getName();
			Faction* pf = (ou != NULL && ou->player != NULL) ? ou->player->getFaction() : NULL;
			if (f->relations != NULL && pf != NULL)
			{
				r.relationKnown = true;
				r.hostile = f->relations->isEnemy(pf);
			}
		}
		x->ok = true;
	}

	// ---- cache of the raw rows --------------------------------------------------------------------

	static std::vector<TlRaw>* g_raw = NULL;
	static DWORD g_rawTime = 0;
	static std::string g_rawAnimal;

	static bool rowLess_dist(const TownRow& a, const TownRow& b)
	{
		return a.dist < b.dist;
	}

	static bool rowLess_name(const TownRow& a, const TownRow& b)
	{
		int c = a.name.compare(b.name);
		if (c != 0)
			return c < 0;
		return a.dist < b.dist;
	}

	static bool rowLess_faction(const TownRow& a, const TownRow& b)
	{
		int c = a.faction.compare(b.faction);
		if (c != 0)
			return c < 0;
		return a.dist < b.dist;
	}

	namespace
	{
	struct TlBuildCtx
	{
		TownListData* out;
		int sort;
		const hand* reqAnimal;
		bool cacheUsed;
	};
	}

	static void tlBuildFn(void* vp)
	{
		TlBuildCtx* b = (TlBuildCtx*)vp;
		TownListData& L = *b->out;
		const Config& cfg = GetConfig();

		Character* animal = NULL;
		const char* src = "selected/first";
		if (b->reqAnimal != NULL)
		{
			animal = b->reqAnimal->getCharacter();
			src = "window";
			if (animal != NULL && (animal->isAnimal() == NULL || animal->isDead() || animal->isDestroyed()))
				animal = NULL;
		}
		else
			animal = MoveFsmPickAnimal(&src);
		if (animal == NULL)
		{
			L.error = "no usable animal";
			return;
		}
		L.animalName = animal->getName();
		const std::string ahs = animal->getHandle().toString();
		const Ogre::Vector3 ap = animal->getPosition();

		const DWORD now = GetTickCount();
		if (g_raw == NULL)
			g_raw = new std::vector<TlRaw>();
		if (!(now - g_rawTime < kCacheMs && g_rawAnimal == ahs && !g_raw->empty()))
		{
			g_raw->clear();
			// loaded traders by home town
			TlScan scan;
			const ogre_unordered_set<Character*>::type& list = ou->getCharacterUpdateList();
			for (ogre_unordered_set<Character*>::type::const_iterator it = list.begin(); it != list.end(); ++it)
			{
				if (*it == NULL)
					continue;
				TlChar x;
				x.s = &scan;
				x.c = *it;
				tlSafeRun(tlScanOne, &x, "towns scan trader");
			}
			if (shou == NULL || shou->townList == NULL)
			{
				L.error = "townList is NULL";
				return;
			}
			lektor<RootObject*>& things = shou->townList->things;
			for (uint32_t i = 0; i < things.size(); ++i)
			{
				if (things[i] == NULL)
					continue;
				TlTownCtx x;
				x.t = static_cast<TownBase*>(things[i]);
				x.ok = false;
				x.isTownObj = false;
				if (!tlSafeRun(tlReadTown, &x, "towns read town") || !x.ok)
					continue;
				TlRaw r;
				r.row = x.row;
				std::map<TownBase*, int>::const_iterator f = scan.byTown.find(x.t);
				r.row.loaded = f != scan.byTown.end() ? f->second : 0;
				g_raw->push_back(r);
			}
			g_rawTime = now;
			g_rawAnimal = ahs;
		}
		else
			b->cacheUsed = true;

		for (size_t i = 0; i < g_raw->size(); ++i)
		{
			TownRow r = (*g_raw)[i].row;
			r.dist = tlDist2D(ap.x, ap.z, r.cx, r.cz);
			r.isFar = r.dist > cfg.listFarDistance;
			r.isVeryFar = r.dist > cfg.listVeryFarDistance;
			r.loadedUnknown = r.loaded == 0 && r.dist > kLoadedAreaGuess;
			if (r.dist > cfg.maxTripDistance) { ++L.hiddenTrip; continue; }
			if (r.dist > cfg.listRadius) { ++L.hiddenRadius; continue; }
			if (cfg.listOnlyCity)
			{
				if (!(r.townType == TOWN_TOWN || r.townType == TOWN_VILLAGE || r.townType == TOWN_OUTPOST)) { ++L.hiddenType; continue; }
				if (!r.open) { ++L.hiddenClosed; continue; }
			}
			if (r.hostile && cfg.hostilePolicy == HOSTILE_BLOCK) { ++L.hiddenHostile; continue; }
			L.shown.push_back(r);
		}
		bool (*cmp)(const TownRow&, const TownRow&) = rowLess_dist;
		if (b->sort == LIST_SORT_NAME)
			cmp = rowLess_name;
		else if (b->sort == LIST_SORT_FACTION)
			cmp = rowLess_faction;
		std::stable_sort(L.shown.begin(), L.shown.end(), cmp);
		L.ok = true;
		LogDebug("towns: built for '%s' (%s), raw rows=%u (%s), shown=%u", L.animalName.c_str(), src, (unsigned)g_raw->size(),
			b->cacheUsed ? "cached" : "scanned", (unsigned)L.shown.size());
	}

	bool BuildTownList(TownListData& out, int sort, const hand* animal)
	{
		out = TownListData();
		if (ou == NULL || ou->player == NULL || shou == NULL)
		{
			out.error = "world not ready";
			return false;
		}
		if (ou->isLoadingFromASaveGame() || ou->gameResetting)
		{
			out.error = "world is loading/resetting";
			return false;
		}
		TlBuildCtx b;
		b.out = &out;
		b.sort = sort;
		b.reqAnimal = animal;
		b.cacheUsed = false;
		if (!tlSafeRun(tlBuildFn, &b, "BuildTownList"))
		{
			out.ok = false;
			out.shown.clear();
			out.error = "exception while building the list";
			return false;
		}
		return out.ok;
	}

	namespace
	{
	struct TlLookupCtx
	{
		float x, z, tol;
		TownRow* out;
		bool found;
	};
	}

	static void tlLookupFn(void* vp)
	{
		TlLookupCtx* l = (TlLookupCtx*)vp;
		l->found = false;
		if (shou == NULL || shou->townList == NULL)
			return;
		lektor<RootObject*>& things = shou->townList->things;
		float best = l->tol;
		for (uint32_t i = 0; i < things.size(); ++i)
		{
			if (things[i] == NULL)
				continue;
			TlTownCtx x;
			x.t = static_cast<TownBase*>(things[i]);
			x.ok = false;
			x.isTownObj = false;
			if (!tlSafeRun(tlReadTown, &x, "town lookup") || !x.ok)
				continue;
			float d = tlDist2D(x.row.cx, x.row.cz, l->x, l->z);
			if (d <= best)
			{
				best = d;
				*l->out = x.row;
				l->found = true;
			}
		}
	}

	bool TownLookup(float x, float z, float tol, TownRow* out)
	{
		TlLookupCtx l;
		l.x = x;
		l.z = z;
		l.tol = tol;
		l.out = out;
		l.found = false;
		if (!tlSafeRun(tlLookupFn, &l, "TownLookup"))
			return false;
		return l.found;
	}

	void TownsListCommand()
	{
		TownListData L;
		if (!BuildTownList(L, LIST_SORT_DISTANCE, NULL))
		{
			LogInfo("list towns: refused: %s", L.error.c_str());
			return;
		}
		const Config& cfg = GetConfig();
		LogInfo("list towns: ---- begin (animal '%s', list_only_city=%d hostile_policy=%d max_trip_distance=%.0f list_radius=%.0f) ----",
			L.animalName.c_str(), cfg.listOnlyCity ? 1 : 0, cfg.hostilePolicy, cfg.maxTripDistance, cfg.listRadius);
		for (size_t i = 0; i < L.shown.size() && i < 200; ++i)
		{
			const TownRow& r = L.shown[i];
			LogInfo("list towns: #%u '%s' faction='%s'%s dist=%.0f%s radius=%.0f center=(%.0f, %.0f) type=%d open=%d loaded traders=%d%s",
				(unsigned)i + 1, r.name.c_str(), r.faction.c_str(), r.hostile ? " HOSTILE" : "", r.dist,
				r.isVeryFar ? " VERY-FAR" : (r.isFar ? " FAR" : ""), r.radius, r.cx, r.cz, r.townType, r.open ? 1 : 0, r.loaded,
				r.loadedUnknown ? " (?)" : "");
		}
		LogInfo("list towns: SUMMARY shown=%u hidden: not_city_type=%d closed=%d hostile=%d radius=%d trip_too_far=%d",
			(unsigned)L.shown.size(), L.hiddenType, L.hiddenClosed, L.hiddenHostile, L.hiddenRadius, L.hiddenTrip);
	}
}
