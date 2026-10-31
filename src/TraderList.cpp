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

// Stage 5: list of city traders (data + log command). Read only, nothing in
// the game is modified. Every per-object block runs under SEH (safeRun) and
// all game pointers live only inside one BuildTraderList call.

#include "TraderList.h"
#include "Seh.h"
#include "Config.h"
#include "Log.h"
#include "MoveFsm.h"
#include "Sell.h"

#include <windows.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <algorithm>

#include <kenshi/GameWorld.h>
#include <kenshi/Globals.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/Character.h>
#include <kenshi/CharacterAnimal.h>
#include <kenshi/Platoon.h>
#include <kenshi/Town.h>
#include <kenshi/Faction.h>
#include <kenshi/FactionRelations.h>
#include <kenshi/GameData.h>
#include <kenshi/Tasker.h>
#include <kenshi/Enums.h>
#include <kenshi/AI/AI.h>
#include <kenshi/AI/AITaskSystem.h>
#include <kenshi/util/hand.h>
#include <ogre/OgreVector3.h>

namespace PS
{
	// limit of candidate rows per build (a safety cap, far above any real town)
	static const int kMaxEntries = 5000;

	// ---- output with a line budget ---------------------------------------------

	static int g_lines = 0;
	static bool g_truncated = false;

	static void vformat(char* buf, size_t size, const char* fmt, va_list args)
	{
		int n = _vsnprintf(buf, size - 1, fmt, args);
		if (n < 0)
			n = (int)size - 1;
		buf[n] = '\0';
	}

	// budgeted line
	static void out(const char* fmt, ...)
	{
		if (g_lines >= GetConfig().probeMaxLines)
		{
			if (!g_truncated)
			{
				g_truncated = true;
				LogInfo("list: output truncated at probe_max_lines=%d", GetConfig().probeMaxLines);
			}
			return;
		}
		++g_lines;
		char buf[900];
		va_list args;
		va_start(args, fmt);
		vformat(buf, sizeof(buf), fmt, args);
		va_end(args);
		LogInfo("list: %s", buf);
	}

	// summary / error line, always written
	static void sum(const char* fmt, ...)
	{
		char buf[900];
		va_list args;
		va_start(args, fmt);
		vformat(buf, sizeof(buf), fmt, args);
		va_end(args);
		LogInfo("list: %s", buf);
	}

	// ---- SEH wrapper (no C++ objects with destructors in this function) ----------

	typedef void (*ListFn)(void*);

	static bool safeRun(ListFn fn, void* ctx, const char* what)
	{
		__try
		{
			fn(ctx);
			return true;
		}
		__except (PsSehFilterCode(GetExceptionCode()))
		{
			sum("EXCEPTION 0x%08X in %s (block skipped)", (unsigned)GetExceptionCode(), what);
			return false;
		}
	}

	// ---- helpers -------------------------------------------------------------------

	const char* HideReasonName(int reason)
	{
		switch (reason)
		{
		case HIDE_NONE: return "shown";
		case HIDE_NOT_CITY: return "not_city";
		case HIDE_TOWN_TYPE: return "town_type";
		case HIDE_TOWN_CLOSED: return "town_closed";
		case HIDE_RADIUS: return "radius";
		case HIDE_TRIP_TOO_FAR: return "trip_too_far";
		case HIDE_NO_MONEY: return "no_money";
		case HIDE_NOTHING_TO_SELL: return "nothing_to_sell";
		}
		return "?";
	}

	static float dist2D(const Ogre::Vector3& a, const Ogre::Vector3& b)
	{
		float dx = a.x - b.x;
		float dz = a.z - b.z;
		return sqrtf(dx * dx + dz * dz);
	}

	static const char* townTypeName(int t)
	{
		switch (t)
		{
		case TOWN_NEST: return "NEST";
		case TOWN_OUTPOST: return "OUTPOST";
		case TOWN_TOWN: return "TOWN";
		case TOWN_VILLAGE: return "VILLAGE";
		case TOWN_RUINS: return "RUINS";
		case TOWN_SLAVE_CAMP: return "SLAVE_CAMP";
		case TOWN_MILITARY: return "MILITARY";
		case TOWN_PRISON: return "PRISON";
		case TOWN_NEST_MARKER: return "NEST_MARKER";
		case TOWN_POI: return "POI";
		case TOWN_NULL: return "NULL";
		}
		return t < 0 ? "none" : "?";
	}

	template<class M>
	static int findBool(const M& m, const std::string& key)
	{
		typename M::const_iterator it = m.find(key);
		if (it == m.end())
			return -1;
		return it->second ? 1 : 0;
	}

	// "is trader" template flag: 1 / 0 / -1 (absent) / -2 (no data)
	static int tplTraderFlag(Character* c)
	{
		if (c == NULL || c->data == NULL)
			return -2;
		return findBool(c->data->bdata, std::string("is trader"));
	}

	static bool charUsable(Character* c)
	{
		if (c == NULL) return false;
		if (c->isDead()) return false;
		if (c->isDestroyed()) return false;
		if (c->isUnconcious()) return false;
		if (c->isKidnapped()) return false;
		if (c->isBeingCarried()) return false;
		return true;
	}

	// relation of faction f towards the player faction pf; returns false if unreadable
	static bool readRelation(Faction* f, Faction* pf, bool* enemy, bool* ally, float* rel)
	{
		*enemy = false;
		*ally = false;
		*rel = 0.0f;
		if (f == NULL || pf == NULL || f->relations == NULL)
			return false;
		*enemy = f->relations->isEnemy(pf);
		*ally = f->relations->isAlly(pf);
		*rel = f->relations->getFactionRelation(pf);
		return true;
	}

	static bool isHostileRow(const TraderEntry& e)
	{
		return e.hostile;
	}

	// ---- collecting ------------------------------------------------------------------

	namespace
	{
	struct BuildCtx
	{
		TraderList* L;
		std::vector<TraderEntry>* all;
		Character* animal;          // valid only inside the current build
		Ogre::Vector3 ref;
		Faction* pf;
		int sort;
		const hand* reqAnimal;      // stage 6: animal requested by the window, or NULL
	};
	}

	namespace
	{
	struct CollectCtx
	{
		BuildCtx* b;
		Character* c;
	};
	}

	static void collectOneFn(void* vp)
	{
		CollectCtx* x = (CollectCtx*)vp;
		BuildCtx* b = x->b;
		Character* c = x->c;
		TraderList& L = *b->L;
		const Config& cfg = GetConfig();

		// trader test: template flag "is trader", or isATrader() when the flag is absent.
		// An explicit flag 0 wins over isATrader() (it also answers yes for guards of a shop squad).
		int fs = tplTraderFlag(c);
		bool at = (fs == 1) ? true : (fs != 0 ? c->isATrader() : false);
		if (!at)
			return;
		++L.st.candidates;
		if (c == b->animal || c->isAnimal() != NULL)
		{
			++L.st.skipAnimal;
			return;
		}
		if (!charUsable(c))
		{
			++L.st.skipUnusable;
			return;
		}
		if ((int)b->all->size() >= kMaxEntries)
		{
			++L.st.capped;
			return;
		}

		TraderEntry e;
		e.h = c->getHandle();
		e.handStr = e.h.toString();
		e.name = c->getName();
		Ogre::Vector3 pos = c->getPosition();
		e.dist = dist2D(pos, b->ref);
		e.isFar = e.dist > cfg.listFarDistance;
		e.isVeryFar = e.dist > cfg.listVeryFarDistance;
		e.money = c->getMoney();
		if (!c->isInsideBuilding.isNull())
		{
			Building* bld = c->isInsideBuilding.getBuilding();
			if (bld != NULL)
				e.building = bld->getName();
		}

		// city / nomad heuristic (api-notes section 3): home town set, currently in it, no wandering goal
		ActivePlatoon* ap = c->getPlatoon();
		Platoon* p = ap != NULL ? ap->me : NULL;
		Ownerships* own = p != NULL ? p->getOwnerships() : NULL;
		TownBase* home = own != NULL ? own->_homeTown : NULL;
		TownBase* res = c->isResident();
		TownBase* cur = c->getCurrentTownLocation();
		bool wander = false;
		OrdersReceiver* oa = c->getOrdersReciever();
		if (oa != NULL)
			wander = oa->hasGoal(WANDERING_TRADER) || oa->hasGoal(TRAVEL_TO_TARGET_TOWN) || oa->hasGoal(TRAVEL_TO_TARGET_TOWN_FAST);
		AI* ai = c->getAI();
		if (ai != NULL && ai->hasPermajob(WANDERING_TRADER))
			wander = true;
		if (wander)
			e.nomadWhy = "wandering goal";
		else if (home == NULL)
			e.nomadWhy = res == NULL ? "no home town" : "no home town (isResident only)";
		else if (cur != home)
			e.nomadWhy = "away from home town";
		else
			e.nomadWhy = "";
		e.city = (e.nomadWhy[0] == '\0');

		TownBase* town = home != NULL ? home : (res != NULL ? res : cur);
		Faction* townFaction = NULL;
		if (town != NULL)
		{
			e.town = town->getKnownName();
			e.townType = (int)town->townType;
			e.townDist = dist2D(town->getPosition(), b->ref);
			Town* tw = town->isTown();
			e.townOpen = tw != NULL ? (tw->openToPublic ? 1 : 0) : -1;
			townFaction = town->getFaction();
		}

		Faction* f = c->getFaction();
		if (f != NULL)
			e.faction = f->getName();
		bool en = false, al = false;
		float rel = 0.0f;
		e.relationKnown = readRelation(f, b->pf, &en, &al, &rel);
		e.isEnemy = en;
		e.isAlly = al;
		e.relation = rel;
		bool ten = false, tal = false;
		float trel = 0.0f;
		if (readRelation(townFaction, b->pf, &ten, &tal, &trel))
			e.townEnemy = ten;
		e.hostile = e.isEnemy || e.townEnemy;

		// filters, in this order; the first that matches is the reason
		int reason = HIDE_NONE;
		if (cfg.listOnlyCity)
		{
			if (!e.city || town == NULL)
				reason = HIDE_NOT_CITY;
			else if (!(e.townType == TOWN_TOWN || e.townType == TOWN_VILLAGE || e.townType == TOWN_OUTPOST))
				reason = HIDE_TOWN_TYPE;
			else if (e.townOpen != 1)
				reason = HIDE_TOWN_CLOSED;
		}
		if (reason == HIDE_NONE)
		{
			float rd = (town != NULL) ? e.townDist : e.dist;
			if (rd > cfg.listRadius)
				reason = HIDE_RADIUS;
		}
		if (reason == HIDE_NONE && e.dist > cfg.maxTripDistance)
			reason = HIDE_TRIP_TOO_FAR;
		if (reason == HIDE_NONE && cfg.listHideNoMoney && e.money < cfg.listMinMoney)
			reason = HIDE_NO_MONEY;
		e.hideReason = reason;
		b->all->push_back(e);
	}

	// ---- sorting (stable) ----------------------------------------------------------------

	static bool lessDistance(const TraderEntry& a, const TraderEntry& b)
	{
		return a.dist < b.dist;
	}

	static bool lessMoney(const TraderEntry& a, const TraderEntry& b)
	{
		if (a.money != b.money)
			return a.money > b.money;       // descending
		return a.dist < b.dist;
	}

	// byte-wise comparison of the UTF-8 names: case-sensitive, Cyrillic capital letters sort in alphabet order
	static bool lessTown(const TraderEntry& a, const TraderEntry& b)
	{
		int c = a.town.compare(b.town);
		if (c != 0)
			return c < 0;
		return a.dist < b.dist;
	}

	// ---- build ------------------------------------------------------------------------------

	static void buildFn(void* vp)
	{
		BuildCtx* b = (BuildCtx*)vp;
		TraderList& L = *b->L;
		const Config& cfg = GetConfig();

		const char* src = "none";
		Character* animal = NULL;
		if (b->reqAnimal != NULL)
		{
			src = "requested by the window";
			animal = b->reqAnimal->getCharacter();
			if (animal != NULL && (animal->isAnimal() == NULL || !charUsable(animal)))
				animal = NULL;
			if (animal == NULL)
			{
				L.error = "the window's animal is not usable (dead, unconscious or unloaded)";
				return;
			}
		}
		else
			animal = MoveFsmPickAnimal(&src);
		if (animal == NULL)
		{
			L.error = "no usable animal (select the pack animal or keep it in the squad)";
			return;
		}
		b->animal = animal;
		L.animal = animal->getHandle();
		L.animalName = animal->getName();
		L.animalSrc = src;
		b->ref = animal->getPosition();
		L.refX = b->ref.x;
		L.refZ = b->ref.z;
		b->pf = ou->player->getFaction();

		std::vector<TraderEntry> all;
		b->all = &all;

		const ogre_unordered_set<Character*>::type& list = ou->getCharacterUpdateList();
		L.st.updateList = (int)list.size();
		CollectCtx cc;
		cc.b = b;
		for (ogre_unordered_set<Character*>::type::const_iterator it = list.begin(); it != list.end(); ++it)
		{
			if (*it == NULL)
				continue;
			cc.c = *it;
			if (!safeRun(collectOneFn, &cc, "collect trader"))
				++L.st.exceptions;
		}

		// "buys something from the animal": same rules as 'sell', dry and silent, only for rows that survived the other filters
		std::vector<Character*> ptrs;
		std::vector<size_t> rows;
		for (size_t i = 0; i < all.size(); ++i)
		{
			if (all[i].hideReason != HIDE_NONE)
				continue;
			Character* c = all[i].h.getCharacter();
			if (c == NULL || !charUsable(c))
				continue;   // vanished within this very frame: sellable stays -1 (unknown), the row is kept
			ptrs.push_back(c);
			rows.push_back(i);
		}
		if (!ptrs.empty())
		{
			std::vector<int> res(ptrs.size(), -1);
			SellCheckTraders(animal, &ptrs[0], (int)ptrs.size(), &res[0]);
			for (size_t k = 0; k < rows.size(); ++k)
			{
				TraderEntry& e = all[rows[k]];
				e.sellable = res[k];
				if (res[k] == 0 && cfg.listHideNothingToSell)
					e.hideReason = HIDE_NOTHING_TO_SELL;
			}
		}

		for (size_t i = 0; i < all.size(); ++i)
		{
			if (all[i].hideReason == HIDE_NONE)
				L.shown.push_back(all[i]);
			else
			{
				L.hidden.push_back(all[i]);
				if (all[i].hideReason > 0 && all[i].hideReason < HIDE_REASON_COUNT)
					++L.st.hiddenBy[all[i].hideReason];
			}
		}

		bool (*cmp)(const TraderEntry&, const TraderEntry&) = lessDistance;
		if (b->sort == LIST_SORT_MONEY)
			cmp = lessMoney;
		else if (b->sort == LIST_SORT_TOWN)
			cmp = lessTown;
		std::stable_sort(L.shown.begin(), L.shown.end(), cmp);
		std::stable_sort(L.hidden.begin(), L.hidden.end(), lessDistance);
		L.ok = true;
	}

	bool BuildTraderList(TraderList& result, int sortOverride, const hand* reqAnimal)
	{
		result = TraderList();
		result.sort = (sortOverride >= 0) ? sortOverride : GetConfig().listSort;
		if (ou == NULL || ou->player == NULL || shou == NULL)
		{
			result.error = "world not ready (ou/player/shou is NULL)";
			return false;
		}
		if (ou->isLoadingFromASaveGame() || ou->gameResetting)
		{
			result.error = "world is loading/resetting";
			return false;
		}
		BuildCtx b;
		b.L = &result;
		b.all = NULL;
		b.animal = NULL;
		b.ref = Ogre::Vector3(0, 0, 0);
		b.pf = NULL;
		b.sort = result.sort;
		b.reqAnimal = reqAnimal;
		if (!safeRun(buildFn, &b, "BuildTraderList"))
		{
			result.ok = false;
			result.shown.clear();
			result.hidden.clear();
			result.error = "exception while building the list";
			return false;
		}
		return result.ok;
	}

	bool TraderIsHostile(Character* c)
	{
		if (c == NULL || ou == NULL || ou->player == NULL)
			return false;
		Faction* pf = ou->player->getFaction();
		bool en = false, al = false;
		float rel = 0.0f;
		if (readRelation(c->getFaction(), pf, &en, &al, &rel) && en)
			return true;
		ActivePlatoon* ap = c->getPlatoon();
		Platoon* p = ap != NULL ? ap->me : NULL;
		Ownerships* own = p != NULL ? p->getOwnerships() : NULL;
		TownBase* home = own != NULL ? own->_homeTown : NULL;
		TownBase* res = c->isResident();
		TownBase* town = home != NULL ? home : (res != NULL ? res : c->getCurrentTownLocation());
		if (town != NULL && readRelation(town->getFaction(), pf, &en, &al, &rel) && en)
			return true;
		return false;
	}

	// ---- command ---------------------------------------------------------------------------

	static const char* sellableStr(int s)
	{
		return s == 1 ? "1" : (s == 0 ? "0" : "?");
	}

	static void logRow(const char* tag, int n, const TraderEntry& e)
	{
		out("%s#%d '%s' hand=%s town='%s'(%s,open=%d) building='%s' dTown=%.0f dist=%.0f money=%d "
			"faction='%s' rel=%s%.1f E=%d A=%d townEnemy=%d HOSTILE=%d city=%d%s%s sellable=%s service?=%s%s%s",
			tag, n, e.name.c_str(), e.handStr.c_str(), e.town.c_str(), townTypeName(e.townType), e.townOpen,
			e.building.c_str(), e.townDist, e.dist, e.money,
			e.faction.c_str(), e.relationKnown ? "" : "n/a:", e.relation, e.isEnemy ? 1 : 0, e.isAlly ? 1 : 0,
			e.townEnemy ? 1 : 0, e.hostile ? 1 : 0, e.city ? 1 : 0,
			e.city ? "" : " nomadWhy=", e.city ? "" : e.nomadWhy,
			sellableStr(e.sellable), e.service < 0 ? "?" : "1",
			e.hideReason != HIDE_NONE ? " reason=" : "", e.hideReason != HIDE_NONE ? HideReasonName(e.hideReason) : "");
	}

	void TradersListCommand(const std::string& arg)
	{
		g_lines = 0;
		g_truncated = false;
		const Config& cfg = GetConfig();
		int sortOverride = -1;
		if (!arg.empty())
		{
			sortOverride = ParseListSort(arg);
			if (sortOverride < 0)
				sum("unknown sort '%s' (use distance | money | town), using list_sort=%s from the config", arg.c_str(), ListSortName(cfg.listSort));
		}
		sum("---- begin (list_radius=%.0f list_min_money=%d list_sort=%s list_hide_no_money=%d list_hide_nothing_to_sell=%d list_only_city=%d) ----",
			cfg.listRadius, cfg.listMinMoney, ListSortName(sortOverride >= 0 ? sortOverride : cfg.listSort),
			cfg.listHideNoMoney ? 1 : 0, cfg.listHideNothingToSell ? 1 : 0, cfg.listOnlyCity ? 1 : 0);

		TraderList L;
		if (!BuildTraderList(L, sortOverride))
		{
			sum("refused: %s", L.error.c_str());
			return;
		}
		sum("reference animal: '%s' hand=%s source=%s pos=(%.1f, %.1f)", L.animalName.c_str(), L.animal.toString().c_str(),
			L.animalSrc.c_str(), L.refX, L.refZ);

		for (size_t i = 0; i < L.shown.size(); ++i)
			logRow("S", (int)i + 1, L.shown[i]);
		for (size_t i = 0; i < L.hidden.size(); ++i)
			logRow("H", (int)i + 1, L.hidden[i]);

		int hostileShown = 0;
		for (size_t i = 0; i < L.shown.size(); ++i)
			if (isHostileRow(L.shown[i]))
				++hostileShown;
		const TraderListStats& st = L.st;
		sum("SUMMARY list: sort=%s | update list=%d | trader candidates=%d (animals=%d unusable=%d capped=%d) | SHOWN=%u HIDDEN=%u "
			"[not_city=%d town_type=%d town_closed=%d radius=%d no_money=%d nothing_to_sell=%d trip_too_far=%d] | blocks with exception=%d | "
			"hostile among shown=%d | service? marker: not computable (always '?') | lines printed=%d%s",
			ListSortName(L.sort), st.updateList, st.candidates, st.skipAnimal, st.skipUnusable, st.capped,
			(unsigned)L.shown.size(), (unsigned)L.hidden.size(),
			st.hiddenBy[HIDE_NOT_CITY], st.hiddenBy[HIDE_TOWN_TYPE], st.hiddenBy[HIDE_TOWN_CLOSED], st.hiddenBy[HIDE_RADIUS],
			st.hiddenBy[HIDE_NO_MONEY], st.hiddenBy[HIDE_NOTHING_TO_SELL], st.hiddenBy[HIDE_TRIP_TOO_FAR], st.exceptions,
			hostileShown, g_lines, g_truncated ? " (TRUNCATED)" : "");
		sum("---- end ----");
	}
}
