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

// Stage 3b read-only probes. Every game object access is guarded (NULL checks)
// and every per-object block runs under safeRun() (SEH), so a bad pointer or a
// wrong layout costs one skipped line, not a crash. NOTHING is modified.

#include "Probe.h"
#include "Seh.h"
#include "Config.h"
#include "Log.h"

#include <windows.h>
#include <math.h>
#include <stdarg.h>
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
#include <kenshi/TradeCulture.h>
#include <kenshi/GameData.h>
#include <kenshi/Inventory.h>
#include <kenshi/Item.h>
#include <kenshi/Tasker.h>
#include <kenshi/Enums.h>
#include <kenshi/AI/AI.h>
#include <kenshi/AI/AITaskSystem.h>
#include <kenshi/gui/InventoryGUI.h>
#include <kenshi/util/hand.h>
#include <ogre/OgreVector3.h>

namespace PS
{
	// ---- output with a line budget -------------------------------------------

	static int g_lines = 0;
	static bool g_truncated = false;
	static char g_tag[32] = "probe";

	static void beginProbe(const char* tag)
	{
		g_lines = 0;
		g_truncated = false;
		_snprintf(g_tag, sizeof(g_tag) - 1, "%s", tag);
		g_tag[sizeof(g_tag) - 1] = '\0';
		LogInfo("%s: ---- begin (probe_radius=%.0f probe_max_lines=%d) ----",
			g_tag, GetConfig().probeRadius, GetConfig().probeMaxLines);
	}

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
				LogInfo("%s: output truncated at probe_max_lines=%d (raise it in PackSeller.cfg)",
					g_tag, GetConfig().probeMaxLines);
			}
			return;
		}
		++g_lines;
		char buf[900];
		va_list args;
		va_start(args, fmt);
		vformat(buf, sizeof(buf), fmt, args);
		va_end(args);
		LogInfo("%s: %s", g_tag, buf);
	}

	// summary / error line, not limited by the budget
	static void sum(const char* fmt, ...)
	{
		char buf[900];
		va_list args;
		va_start(args, fmt);
		vformat(buf, sizeof(buf), fmt, args);
		va_end(args);
		LogInfo("%s: %s", g_tag, buf);
	}

	// ---- SEH wrapper (no C++ objects with destructors in this function) -------

	typedef void (*ProbeFn)(void*);

	static bool safeRun(ProbeFn fn, void* ctx, const char* what)
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

	// ---- small helpers --------------------------------------------------------

	static float dist2D(const Ogre::Vector3& a, const Ogre::Vector3& b)
	{
		float dx = a.x - b.x;
		float dz = a.z - b.z;
		return sqrtf(dx * dx + dz * dz);
	}

	static bool worldReady(const char* cmd)
	{
		if (ou == NULL || ou->player == NULL || shou == NULL)
		{
			sum("%s refused: world not ready (ou/player/shou is NULL)", cmd);
			return false;
		}
		if (ou->isLoadingFromASaveGame() || ou->gameResetting)
		{
			sum("%s refused: world is loading/resetting", cmd);
			return false;
		}
		return true;
	}

	static const char* taskName(int key)
	{
		switch (key)
		{
		case MOVE_CUS_ORDERED: return "MOVE_CUS_ORDERED";
		case WANDERING_TRADER: return "WANDERING_TRADER";
		case STAND_AT_SHOPKEEPER_NODE: return "STAND_AT_SHOPKEEPER_NODE";
		case TRAVEL_TO_TARGET_TOWN: return "TRAVEL_TO_TARGET_TOWN";
		case TRAVEL_TO_TARGET_TOWN_FAST: return "TRAVEL_TO_TARGET_TOWN_FAST";
		}
		return "?";
	}

	static bool sameHand(const hand& a, const hand& b)
	{
		return a.type == b.type && a.index == b.index && a.serial == b.serial
			&& a.container == b.container && a.containerSerial == b.containerSerial;
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
		return "?";
	}

	static std::string townName(TownBase* t)
	{
		if (t == NULL)
			return std::string("none");
		return t->getKnownName();
	}

	// "name (E=enemy A=ally rel=value)" of faction f towards the player faction
	static std::string factionStr(Faction* f)
	{
		if (f == NULL)
			return std::string("n/a");
		char buf[256];
		Faction* pf = (ou != NULL && ou->player != NULL) ? ou->player->getFaction() : NULL;
		std::string name = f->getName();
		if (f->relations == NULL || pf == NULL)
		{
			_snprintf(buf, sizeof(buf) - 1, "'%s' (relation n/a: relations=%p playerFaction=%p)",
				name.c_str(), (void*)f->relations, (void*)pf);
		}
		else
		{
			int e = f->relations->isEnemy(pf) ? 1 : 0;
			int a = f->relations->isAlly(pf) ? 1 : 0;
			float rel = f->relations->getFactionRelation(pf);
			_snprintf(buf, sizeof(buf) - 1, "'%s' (isEnemy=%d isAlly=%d relation=%.1f thePlayer=%d)",
				name.c_str(), e, a, rel, f->isThePlayer() ? 1 : 0);
		}
		buf[sizeof(buf) - 1] = '\0';
		return std::string(buf);
	}

	// nearest real town (isTown() != NULL) by XZ distance; returns NULL if none
	static TownBase* nearestTown(const Ogre::Vector3& pos, float* dOut, float* radiusOut)
	{
		*dOut = -1.0f;
		*radiusOut = 0.0f;
		if (shou == NULL || shou->townList == NULL)
			return NULL;
		lektor<RootObject*>& things = shou->townList->things;
		TownBase* best = NULL;
		float bestD = 0.0f;
		for (uint32_t i = 0; i < things.size(); ++i)
		{
			RootObject* ro = things[i];
			if (ro == NULL)
				continue;
			TownBase* t = static_cast<TownBase*>(ro);
			if (t->isTown() == NULL)
				continue;
			float d = dist2D(t->getPosition(), pos);
			if (best == NULL || d < bestD)
			{
				best = t;
				bestD = d;
			}
		}
		if (best != NULL)
		{
			*dOut = bestD;
			*radiusOut = best->getRadius();
		}
		return best;
	}

	// ---- reference point (the animal / first player character) ---------------

	namespace
	{
	struct RefInfo
	{
		bool have;
		Ogre::Vector3 pos;
		char label[160];
		Character* animal;   // valid only inside the current call
		RefInfo() : have(false), pos(0, 0, 0), animal(NULL) { label[0] = '\0'; }
	};
	}

	static void getRef(RefInfo& r)
	{
		r.have = false;
		r.animal = NULL;
		_snprintf(r.label, sizeof(r.label) - 1, "none");
		if (ou == NULL || ou->player == NULL)
			return;

		typedef ogre_unordered_set<hand>::type HandSet;
		const HandSet& sel = ou->player->selectedCharacters;
		Character* firstSel = NULL;
		for (HandSet::const_iterator it = sel.begin(); it != sel.end(); ++it)
		{
			Character* c = it->getCharacter();
			if (c == NULL || c->isDead() || c->isDestroyed())
				continue;
			if (c->isAnimal() != NULL)
			{
				r.animal = c;
				r.have = true;
				r.pos = c->getPosition();
				_snprintf(r.label, sizeof(r.label) - 1, "selected animal '%s'", c->getName().c_str());
				return;
			}
			if (firstSel == NULL)
				firstSel = c;
		}

		const lektor<Character*>& all = ou->player->getAllPlayerCharacters();
		Character* firstAny = NULL;
		for (uint32_t i = 0; i < all.size(); ++i)
		{
			Character* c = all[i];
			if (c == NULL || c->isDead() || c->isDestroyed())
				continue;
			if (c->isAnimal() != NULL)
			{
				r.animal = c;
				r.have = true;
				r.pos = c->getPosition();
				_snprintf(r.label, sizeof(r.label) - 1, "first animal in squad '%s'", c->getName().c_str());
				return;
			}
			if (firstAny == NULL)
				firstAny = c;
		}

		Character* c = firstSel != NULL ? firstSel : firstAny;
		if (c != NULL)
		{
			r.have = true;
			r.pos = c->getPosition();
			_snprintf(r.label, sizeof(r.label) - 1, "%s player character '%s' (no animal found)",
				firstSel != NULL ? "selected" : "first", c->getName().c_str());
		}
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
		GameData* d = c->data;
		return findBool(d->bdata, std::string("is trader"));
	}

	static const char* flagStr(int f)
	{
		return f == 1 ? "1" : (f == 0 ? "0" : (f == -1 ? "absent" : "n/a"));
	}

	// ============================================================================
	// traders
	// ============================================================================

	namespace
	{
	struct TraderStats
	{
		int total, cand, tplFlag, atraderOnly, withHome, resident, curEqHome, wander, hostile, ally, dead, animals, moneyPos;
		int houseNomadGuess, cityGuess, unknownGuess;
		double moneySum;
		TraderStats() : total(0), cand(0), tplFlag(0), atraderOnly(0), withHome(0), resident(0), curEqHome(0)
			, wander(0), hostile(0), ally(0), dead(0), animals(0), moneyPos(0)
			, houseNomadGuess(0), cityGuess(0), unknownGuess(0), moneySum(0.0) {}
	};
	}

	namespace
	{
	struct TraderCtx
	{
		Character* c;
		int idx;
		const Ogre::Vector3* ref;
		bool haveRef;
		const char* refLabel;
		TraderStats* st;
		bool printed;
	};
	}

	static void traderOne(void* vp)
	{
		TraderCtx* x = (TraderCtx*)vp;
		Character* c = x->c;
		x->printed = false;
		int fs = tplTraderFlag(c);
		bool at = c->isATrader();
		if (fs != 1 && !at)
			return;

		TraderStats& st = *x->st;
		++st.cand;
		x->printed = true;
		int n = x->idx;
		if (fs == 1) ++st.tplFlag; else ++st.atraderOnly;

		ActivePlatoon* ap = c->getPlatoon();
		Platoon* p = ap != NULL ? ap->me : NULL;
		Ownerships* own = p != NULL ? p->getOwnerships() : NULL;

		bool dead = c->isDead();
		bool unc = c->isUnconcious();
		bool isAnimal = c->isAnimal() != NULL;
		if (dead) ++st.dead;
		if (isAnimal) ++st.animals;

		out("T#%d '%s' hand=%s squad='%s' size=%d squadType=%d tpl='%s' | tplIsTrader=%s isATrader=%d "
			"squadIsTrader=%s hasVendorList=%s | dead=%d unconscious=%d animal=%d",
			n, c->getName().c_str(), c->getHandle().toString().c_str(),
			ap != NULL ? ap->getName().c_str() : "n/a", ap != NULL ? ap->getSquadSize() : -1,
			p != NULL ? (int)p->getSquadType() : -1,
			c->data != NULL ? c->data->name.c_str() : "n/a",
			flagStr(fs), at ? 1 : 0,
			ap != NULL ? (ap->getIsTrader() ? "1" : "0") : "n/a",
			ap != NULL ? (ap->getHasVendorList() ? "1" : "0") : "n/a",
			dead ? 1 : 0, unc ? 1 : 0, isAnimal ? 1 : 0);

		Ogre::Vector3 pos = c->getPosition();
		const hand& ind = c->isIndoors();
		std::string bname("none");
		if (!c->isInsideBuilding.isNull())
		{
			Building* b = c->isInsideBuilding.getBuilding();
			bname = b != NULL ? b->getName() : std::string("unresolved");
		}
		if (x->haveRef)
			out("T#%d pos=(%.1f, %.1f, %.1f) dist2D=%.1f from %s | indoorsHandleNull=%d insideBuilding='%s'",
				n, pos.x, pos.y, pos.z, dist2D(pos, *x->ref), x->refLabel, ind.isNull() ? 1 : 0, bname.c_str());
		else
			out("T#%d pos=(%.1f, %.1f, %.1f) dist2D=n/a | indoorsHandleNull=%d insideBuilding='%s'",
				n, pos.x, pos.y, pos.z, ind.isNull() ? 1 : 0, bname.c_str());

		Faction* f = c->getFaction();
		int money = c->getMoney();
		if (money > 0) { ++st.moneyPos; st.moneySum += money; }
		if (f != NULL && f->relations != NULL && ou->player->getFaction() != NULL)
		{
			if (f->relations->isEnemy(ou->player->getFaction())) ++st.hostile;
			if (f->relations->isAlly(ou->player->getFaction())) ++st.ally;
		}
		out("T#%d faction=%s | money: Character::getMoney=%d platoon Ownerships::getMoney=%d%s",
			n, factionStr(f).c_str(), money, own != NULL ? own->getMoney() : -1, own != NULL ? "" : " (n/a)");

		TownBase* res = c->isResident();
		TownBase* home = own != NULL ? own->_homeTown : NULL;
		TownBase* cur = c->getCurrentTownLocation();
		float nd = -1.0f, nr = 0.0f;
		TownBase* nearT = nearestTown(pos, &nd, &nr);
		if (home != NULL) ++st.withHome;
		if (res != NULL) ++st.resident;
		if (home != NULL && cur == home) ++st.curEqHome;
		float homeD = -1.0f;
		if (home != NULL)
			homeD = dist2D(home->getPosition(), pos);
		out("T#%d city: isResident='%s' homeTown='%s' (dist=%.1f) currentTown='%s' homeEqCurrent=%d "
			"nearestTown='%s' (dist=%.1f radius=%.1f inside=%d type=%s)",
			n, townName(res).c_str(), townName(home).c_str(), homeD, townName(cur).c_str(),
			(home != NULL && cur == home) ? 1 : 0,
			townName(nearT).c_str(), nd, nr, (nearT != NULL && nd <= nr) ? 1 : 0,
			nearT != NULL ? townTypeName((int)nearT->townType) : "n/a");

		OrdersReceiver* oa = c->getOrdersReciever();
		AI* ai = c->getAI();
		int wander = -1, travel = -1, travelFast = -1, stand = -1, pjWander = -1, pjStand = -1;
		int firstKey = -1;
		if (oa != NULL)
		{
			wander = oa->hasGoal(WANDERING_TRADER) ? 1 : 0;
			travel = oa->hasGoal(TRAVEL_TO_TARGET_TOWN) ? 1 : 0;
			travelFast = oa->hasGoal(TRAVEL_TO_TARGET_TOWN_FAST) ? 1 : 0;
			stand = oa->hasGoal(STAND_AT_SHOPKEEPER_NODE) ? 1 : 0;
			Tasker* first = oa->getFirstOrder();
			if (first != NULL)
				firstKey = (int)first->key();
		}
		if (ai != NULL)
		{
			pjWander = ai->hasPermajob(WANDERING_TRADER) ? 1 : 0;
			pjStand = ai->hasPermajob(STAND_AT_SHOPKEEPER_NODE) ? 1 : 0;
		}
		int goalKey = -1;
		if (ap != NULL && ap->currentGoal != NULL)
			goalKey = (int)ap->currentGoal->key();
		if (wander == 1 || pjWander == 1) ++st.wander;
		out("T#%d goals: hasGoal(WANDERING_TRADER)=%d hasGoal(TRAVEL_TO_TARGET_TOWN)=%d hasGoal(..._FAST)=%d "
			"hasGoal(STAND_AT_SHOPKEEPER_NODE)=%d | permajob(WANDERING)=%d permajob(STAND_AT_SHOPKEEPER)=%d | "
			"firstOrderKey=%d(%s) platoonGoalKey=%d(%s)  [-1 = n/a or none]",
			n, wander, travel, travelFast, stand, pjWander, pjStand,
			firstKey, firstKey >= 0 ? taskName(firstKey) : "-", goalKey, goalKey >= 0 ? taskName(goalKey) : "-");

		// heuristic guess (to be compared with what the user sees in game)
		const char* guess = "UNKNOWN";
		if (wander == 1 || pjWander == 1 || travel == 1 || travelFast == 1)
			guess = "NOMAD?";
		else if (home != NULL && cur == home)
			guess = "CITY?";
		else if (home == NULL)
			guess = "NOMAD?(no home town)";
		if (guess[0] == 'N') ++st.houseNomadGuess; else if (guess[0] == 'C') ++st.cityGuess; else ++st.unknownGuess;

		if (p != NULL)
			out("T#%d trade: priceMultWhenITrade=%.3f buysStolen=%d buysIllegal=%d | heuristic guess=%s",
				n, p->priceMultWhenITrade, p->_iBuyStolenGoods ? 1 : 0, p->_iBuyIllegalGoods ? 1 : 0, guess);
		else
			out("T#%d trade: platoon n/a (priceMultWhenITrade, buysStolen, buysIllegal = n/a) | heuristic guess=%s", n, guess);
	}

	void ProbeTraders()
	{
		beginProbe("traders");
		if (!worldReady("traders"))
			return;

		RefInfo ref;
		getRef(ref);
		sum("reference point: %s%s", ref.label, ref.have ? "" : " (distances n/a)");

		TraderStats st;
		const ogre_unordered_set<Character*>::type& list = ou->getCharacterUpdateList();
		st.total = (int)list.size();
		TraderCtx x;
		x.ref = &ref.pos;
		x.haveRef = ref.have;
		x.refLabel = ref.label;
		x.st = &st;
		x.idx = 0;
		int skippedErr = 0;
		for (ogre_unordered_set<Character*>::type::const_iterator it = list.begin(); it != list.end(); ++it)
		{
			Character* c = *it;
			if (c == NULL)
				continue;
			x.c = c;
			x.idx = st.cand + 1;
			if (!safeRun(traderOne, &x, "traderOne"))
				++skippedErr;
		}
		sum("SUMMARY traders: loaded characters in update list=%d | candidates (is trader flag OR isATrader)=%d "
			"[tpl flag=%d, isATrader only=%d] | dead=%d animals=%d | with homeTown=%d isResident=%d currentTown==homeTown=%d "
			"| wandering goal=%d | hostile faction=%d ally=%d | money>0: %d (sum %.0f) | heuristic: CITY?=%d NOMAD?=%d other=%d | "
			"blocks with exception=%d | lines printed=%d%s",
			st.total, st.cand, st.tplFlag, st.atraderOnly, st.dead, st.animals, st.withHome, st.resident, st.curEqHome,
			st.wander, st.hostile, st.ally, st.moneyPos, st.moneySum, st.cityGuess, st.houseNomadGuess, st.unknownGuess,
			skippedErr, g_lines, g_truncated ? " (TRUNCATED)" : "");
	}

	// ============================================================================
	// towns
	// ============================================================================

	namespace
	{
	struct TownEntry
	{
		TownBase* t;
		float d;
	};
	}

	static bool townLess(const TownEntry& a, const TownEntry& b)
	{
		return a.d < b.d;
	}

	namespace
	{
	struct TownCtx
	{
		TownBase* t;
		float d;
		int idx;
		int residentTraders;
		int nomadishTraders;
	};
	}

	static void townOne(void* vp)
	{
		TownCtx* x = (TownCtx*)vp;
		TownBase* t = x->t;
		Ogre::Vector3 tp = t->getPosition();
		Town* tw = t->isTown();
		out("TW#%d '%s' type=%s faction=%s pos=(%.1f, %.1f, %.1f) dist2D=%.1f radius=%.1f discovered=%d",
			x->idx, townName(t).c_str(), townTypeName((int)t->townType), factionStr(t->getFaction()).c_str(),
			tp.x, tp.y, tp.z, x->d, t->getRadius(), t->isDiscovered() ? 1 : 0);
		out("TW#%d residents: loaded trader-candidates with homeTown/isResident here=%d (nomad-like loaded inside radius: %d) | "
			"residentsSpawned.size=%u residentsSpawned_Bars.size=%u factionsResidentHere=%u | town: openToPublic=%s gatesAllClosed=%s isAccessible=%s",
			x->idx, x->residentTraders, x->nomadishTraders,
			(unsigned)t->residentsSpawned.size(), (unsigned)t->residentsSpawned_BarsOrSomething.size(),
			(unsigned)t->factionsResidentHere.size(),
			tw != NULL ? (tw->openToPublic ? "1" : "0") : "n/a",
			tw != NULL ? (tw->gatesAllClosed() ? "1" : "0") : "n/a",
			tw != NULL ? (tw->isAccessible ? "1" : "0") : "n/a");
	}

	// helpers with SEH, no C++ objects with destructors in their bodies
	static bool candHome(Character* c, TownBase** homeOut)
	{
		*homeOut = NULL;
		__try
		{
			bool cand = (tplTraderFlag(c) == 1) || c->isATrader();
			if (cand)
			{
				TownBase* res = c->isResident();
				ActivePlatoon* ap = c->getPlatoon();
				Platoon* p = ap != NULL ? ap->me : NULL;
				Ownerships* own = p != NULL ? p->getOwnerships() : NULL;
				TownBase* home = own != NULL ? own->_homeTown : NULL;
				*homeOut = home != NULL ? home : res;
			}
			return cand;
		}
		__except (PsSehFilterCode(GetExceptionCode()))
		{
			*homeOut = NULL;
			return false;
		}
	}

	typedef std::map<TownBase*, int> TownCountMap;

	// counts loaded trader-candidates by home town / isResident town (pointer
	// comparison within this single call only)
	static void countResidentTraders(TownCountMap& byTown, TownCountMap& insideNoHome, int* totalCand)
	{
		*totalCand = 0;
		const ogre_unordered_set<Character*>::type& list = ou->getCharacterUpdateList();
		for (ogre_unordered_set<Character*>::type::const_iterator it = list.begin(); it != list.end(); ++it)
		{
			Character* c = *it;
			if (c == NULL)
				continue;
			TownBase* home = NULL;
			bool cand = candHome(c, &home);
			if (!cand)
				continue;
			++(*totalCand);
			if (home != NULL)
				byTown[home] += 1;
			else
				insideNoHome[NULL] += 1;
		}
	}

	static bool townIsTownDist(TownBase* t, bool haveRef, const Ogre::Vector3* refPos, float* d)
	{
		*d = 0.0f;
		__try
		{
			if (t->isTown() == NULL)
				return false;
			if (haveRef)
				*d = dist2D(t->getPosition(), *refPos);
			return true;
		}
		__except (PsSehFilterCode(GetExceptionCode()))
		{
			return false;
		}
	}

	static bool townHostile(TownBase* t, Faction* pf)
	{
		__try
		{
			Faction* f = t->getFaction();
			return f != NULL && f->relations != NULL && pf != NULL && f->relations->isEnemy(pf);
		}
		__except (PsSehFilterCode(GetExceptionCode()))
		{
			return false;
		}
	}

	void ProbeTowns()
	{
		beginProbe("towns");
		if (!worldReady("towns"))
			return;
		if (shou->townList == NULL)
		{
			sum("townList is NULL");
			return;
		}
		RefInfo ref;
		getRef(ref);
		sum("reference point: %s%s", ref.label, ref.have ? "" : " (no reference: all towns listed unsorted)");

		// loaded-world constants
		// Platoon::PLATOON_LOAD_DISTANCE is a static data member: it does not link against KenshiLib.lib
		// (LNK2001, stage 3b) and raw RVAs are forbidden, so it is not read.
		sum("constants: Platoon::PLATOON_LOAD_DISTANCE=n/a (static data member not linkable, LNK2001) | cell/zone size: n/a (no confirmed API in KL 0.5.0 headers) "
			"| use the farthest loaded trader distance from 'traders' as an empirical load radius");

		TownCountMap byTown, noHome;
		int cand = 0;
		countResidentTraders(byTown, noHome, &cand);

		lektor<RootObject*>& things = shou->townList->things;
		std::vector<TownEntry> list;
		int totalTowns = 0, skippedFar = 0, notTown = 0;
		for (uint32_t i = 0; i < things.size(); ++i)
		{
			RootObject* ro = things[i];
			if (ro == NULL)
				continue;
			TownBase* t = static_cast<TownBase*>(ro);
			float d = 0.0f;
			bool isTown = townIsTownDist(t, ref.have, &ref.pos, &d);
			if (!isTown) { ++notTown; continue; }
			++totalTowns;
			if (ref.have && d > GetConfig().probeRadius) { ++skippedFar; continue; }
			TownEntry e;
			e.t = t;
			e.d = d;
			list.push_back(e);
		}
		std::sort(list.begin(), list.end(), townLess);

		int hostile = 0;
		for (size_t i = 0; i < list.size(); ++i)
		{
			TownCtx x;
			x.t = list[i].t;
			x.d = list[i].d;
			x.idx = (int)i + 1;
			TownCountMap::const_iterator f = byTown.find(x.t);
			x.residentTraders = f != byTown.end() ? f->second : 0;
			x.nomadishTraders = 0;
			safeRun(townOne, &x, "townOne");
		}
		// hostile count (cheap re-evaluation, guarded)
		Faction* pf = ou->player->getFaction();
		for (size_t i = 0; i < list.size(); ++i)
		{
			if (townHostile(list[i].t, pf))
				++hostile;
		}
		int noHomeCount = noHome.empty() ? 0 : noHome.begin()->second;
		sum("SUMMARY towns: townList entries=%u, real towns (isTown)=%d, not towns/skipped=%d, outside probe_radius=%d, listed=%u "
			"(hostile to player=%d) | loaded trader-candidates=%d, of them without home/resident town=%d | lines printed=%d%s",
			(unsigned)things.size(), totalTowns, notTown, skippedFar, (unsigned)list.size(), hostile, cand, noHomeCount,
			g_lines, g_truncated ? " (TRUNCATED)" : "");
	}

	// ============================================================================
	// prices
	// ============================================================================

	// Accessor for the protected InventoryItemBase::merchantPriceMod (same
	// technique as the TRII plugin, see api-notes section 4). Never instantiated.
	namespace
	{
	struct PriceAccess : public InventoryItemBase
	{
		static float mod(const InventoryItemBase* i)
		{
			float (InventoryItemBase::*fn)() const = &PriceAccess::merchantPriceMod;
			return (i->*fn)();
		}
	};
	}

	namespace
	{
	struct PriceTotals
	{
		int items, equipped, stolen, illegal, containersNonEmpty, sellable;
		double sumAllNonEq, sumSellable, sumSellablePM;
		PriceTotals() : items(0), equipped(0), stolen(0), illegal(0), containersNonEmpty(0), sellable(0)
			, sumAllNonEq(0), sumSellable(0), sumSellablePM(0) {}
	};
	}

	namespace
	{
	struct ItemCtx
	{
		Item* it;
		int n;
		const char* src;
		const hand* animalH;
		const hand* bpH;
		bool haveTrader;
		int traderMoney;
		Platoon* tp;
		TradeCulture* tc;
		Town* town;
		float guiMult;
		PriceTotals* tot;
	};
	}

	static void itemOne(void* vp)
	{
		ItemCtx* x = (ItemCtx*)vp;
		Item* it = x->it;
		PriceTotals& tot = *x->tot;
		++tot.items;

		std::string nm = it->getName();
		Inventory* sub = it->getInventory();
		bool isCont = sub != NULL;
		int subN = isCont ? (int)sub->getNumItems() : -1;
		bool subEmpty = isCont ? sub->isEmpty() : true;
		int v1p = it->getValueSingle(true);
		int v1n = it->getValueSingle(false);
		int vAp = it->getValueAll(true);
		int vAn = it->getValueAll(false);
		int avg = it->getAvgPrice();
		float mpm = PriceAccess::mod(it);
		bool eq = it->isEquipped;
		bool stolen = it->isStolen(false);
		bool stolenAny = it->isStolen(true);
		const hand& owner = it->getInventoryWeAreIn();
		const char* inRef = "other";
		if (x->animalH != NULL && sameHand(owner, *x->animalH)) inRef = "animal";
		else if (x->bpH != NULL && sameHand(owner, *x->bpH)) inRef = "backpack";
		if (eq) ++tot.equipped;
		if (stolen) ++tot.stolen;
		if (isCont && !subEmpty) ++tot.containersNonEmpty;

		out("I#%d [%s] '%s' qty=%d cls=%d fn=%d section='%s' inInv=%d ownerHand=%s(%s) | eq=%d stolen=%d stolenAny=%d tradeItem=%d unique=%d "
			"container=%d(sub items=%d empty=%d) quality=%.2f charges=%.1f",
			x->n, x->src, nm.c_str(), it->quantity, (int)it->getClassType(), (int)it->itemFunction, it->inventorySection.c_str(),
			it->isInInventory ? 1 : 0, owner.toString().c_str(), inRef, eq ? 1 : 0, stolen ? 1 : 0, stolenAny ? 1 : 0,
			it->isTradeItem ? 1 : 0, it->isUnique ? 1 : 0, isCont ? 1 : 0, subN, subEmpty ? 1 : 0, it->quality, it->chargesLeft);

		// trader dependent part
		int illegal = -1;
		float localMult = -1.0f, cultureMult = -1.0f;
		int buysStolen = -1, maxAff = -1;
		if (x->tc != NULL && it->data != NULL)
		{
			illegal = x->tc->isItemIllegal(it->data) ? 1 : 0;
			cultureMult = x->tc->getTradePriceMultiplier(it->data);
		}
		if (x->town != NULL && it->data != NULL)
			localMult = x->town->getLocalTradePriceMult(it->data);
		if (x->tp != NULL)
			buysStolen = x->tp->iBuyStolenGoods(it) ? 1 : 0;
		if (x->haveTrader)
			maxAff = it->getMaxAffordableNum(x->traderMoney, true);
		if (illegal == 1) ++tot.illegal;

		float pm = x->tp != NULL ? x->tp->priceMultWhenITrade : 1.0f;
		bool buyIllegal = x->tp != NULL && x->tp->_iBuyIllegalGoods;
		bool sellable = !eq && vAp > 0 && (!stolen || buysStolen == 1) && (illegal != 1 || buyIllegal)
			&& !(isCont && !subEmpty);
		if (!eq) tot.sumAllNonEq += vAp;
		if (sellable)
		{
			++tot.sellable;
			tot.sumSellable += vAp;
			tot.sumSellablePM += vAp * pm;
		}
		out("I#%d price: getValueSingle(true)=%d getValueSingle(false)=%d getValueAll(true)=%d getValueAll(false)=%d getAvgPrice=%d "
			"merchantPriceMod=%.3f | trader: legalityIllegal=%d cultureMult=%.3f localTownMult=%.3f buysStolen=%d maxAffordableNum(traderMoney,true)=%d "
			"| est valueAll(true)*priceMultWhenITrade=%.0f est valueAll(true)*guiMult=%.0f | buys(this item)=n/a(no unambiguous API) sellableByOurFilter=%d",
			x->n, v1p, v1n, vAp, vAn, avg, mpm, illegal, cultureMult, localMult, buysStolen, maxAff,
			vAp * pm, vAp * x->guiMult, sellable ? 1 : 0);
	}

	static void logSections(const char* who, Inventory* inv)
	{
		if (inv == NULL)
		{
			out("%s sections: inventory NULL", who);
			return;
		}
		lektor<InventorySection*>& secs = inv->getAllSections();
		out("%s sections=%u getAllItems()=%u getNumItems()=%d isEmpty=%d isAContainer=%d", who, (unsigned)secs.size(),
			(unsigned)inv->getAllItems().size(), inv->getNumItems(), inv->isEmpty() ? 1 : 0, inv->isAContainer() ? 1 : 0);
		for (uint32_t i = 0; i < secs.size(); ++i)
		{
			InventorySection* s = secs[i];
			if (s == NULL)
				continue;
			out("%s   section '%s' %dx%d items=%u equippedSection=%d containerSlot=%d enabled=%d",
				who, s->name.c_str(), s->width, s->height, (unsigned)s->getItems().size(),
				s->isAnEquippedItemSection ? 1 : 0, s->containerSlot ? 1 : 0, s->enabled ? 1 : 0);
		}
	}

	namespace
	{
	struct ProbeWinCtx
	{
		Character* t;
	};
	}

	static void winTraderFn(void* vp)
	{
		((ProbeWinCtx*)vp)->t = InventoryGUI::getNPCTrader();
	}

	static Character* windowTrader()
	{
		ProbeWinCtx w;
		w.t = NULL;
		safeRun(winTraderFn, &w, "InventoryGUI::getNPCTrader");
		return w.t;
	}

	namespace
	{
	struct PricesCtx
	{
		Character* forced;
		const char* where;
	};
	}

	static void pricesBody(void* vp)
	{
		PricesCtx* px = (PricesCtx*)vp;
		RefInfo ref;
		getRef(ref);
		sum("reference point: %s%s", ref.label, ref.have ? "" : " (none)");
		Character* animal = ref.animal;
		if (animal == NULL)
		{
			sum("no animal found (select the pack animal or keep it in the squad); nothing to price");
			return;
		}

		// ---- trader selection
		Character* trader = px->forced;
		const char* tsrc = "trade window (forced)";
		if (trader == NULL)
		{
			trader = windowTrader();
			tsrc = "trade window (InventoryGUI::getNPCTrader)";
		}
		float guiMult = 1.0f;
		bool guiMultOk = false;
		{
			// static InventoryGUI::getTraderPriceMultiplier(), result outside a trade window is unknown
			guiMult = InventoryGUI::getTraderPriceMultiplier();
			guiMultOk = true;
		}
		if (trader == NULL)
		{
			// nearest trader candidate to the animal within probe_trade_radius
			tsrc = "nearest candidate";
			float best = GetConfig().probeTradeRadius;
			const ogre_unordered_set<Character*>::type& list = ou->getCharacterUpdateList();
			for (ogre_unordered_set<Character*>::type::const_iterator it = list.begin(); it != list.end(); ++it)
			{
				Character* c = *it;
				if (c == NULL || c == animal || c->isDead() || c->isAnimal() != NULL)
					continue;
				if (tplTraderFlag(c) != 1 && !c->isATrader())
					continue;
				float d = dist2D(c->getPosition(), ref.pos);
				if (d <= best)
				{
					best = d;
					trader = c;
				}
			}
		}
		Platoon* tp = NULL;
		TradeCulture* tc = NULL;
		Town* town = NULL;
		int traderMoney = 0;
		bool haveTrader = trader != NULL;
		if (haveTrader)
		{
			ActivePlatoon* ap = trader->getPlatoon();
			tp = ap != NULL ? ap->me : NULL;
			traderMoney = trader->getMoney();
			TownBase* cur = trader->getCurrentTownLocation();
			if (cur == NULL)
			{
				float nd, nr;
				cur = nearestTown(trader->getPosition(), &nd, &nr);
			}
			town = cur != NULL ? cur->isTown() : NULL;
			if (town != NULL)
				tc = town->tradeCulture;
			Faction* tf = trader->getFaction();
			if (tc == NULL && tf != NULL)
				tc = &tf->tradeCulture;
			float d = dist2D(trader->getPosition(), ref.pos);
			sum("trader: '%s' hand=%s source=%s dist2D=%.1f money=%d town='%s' tradeCulture=%s faction=%s priceMultWhenITrade=%.3f "
				"buysStolen=%d buysIllegal=%d",
				trader->getName().c_str(), trader->getHandle().toString().c_str(), tsrc, d, traderMoney, townName(cur).c_str(),
				tc != NULL ? (town != NULL && town->tradeCulture == tc ? "town" : "faction") : "n/a",
				factionStr(tf).c_str(), tp != NULL ? tp->priceMultWhenITrade : -1.0f,
				tp != NULL ? (tp->_iBuyStolenGoods ? 1 : 0) : -1, tp != NULL ? (tp->_iBuyIllegalGoods ? 1 : 0) : -1);
		}
		else
			sum("trader: none (no window trader and no trader candidate within probe_trade_radius=%.0f of the animal); "
				"trader-dependent fields will be n/a (-1)", GetConfig().probeTradeRadius);
		sum("InventoryGUI::getTraderPriceMultiplier()=%.4f (%s) | window trader: %s",
			guiMult, guiMultOk ? "read" : "n/a", windowTrader() != NULL ? "OPEN (non-NULL)" : "none (NULL)");

		// ---- where the pack items live
		Inventory* inv = animal->getInventory();
		ContainerItem* bp = animal->hasABackpackOn();
		Inventory* bpInv = bp != NULL ? bp->getInventory() : NULL;
		sum("animal '%s' hand=%s inventory=%p backpack(hasABackpackOn)=%s backpackInventory=%p",
			animal->getName().c_str(), animal->getHandle().toString().c_str(), (void*)inv,
			bp != NULL ? bp->getName().c_str() : "none", (void*)bpInv);
		if (inv == NULL)
		{
			sum("animal inventory is NULL; stop");
			return;
		}
		logSections("animal", inv);
		if (bpInv != NULL)
			logSections("backpack", bpInv);

		const lektor<Item*>& A = inv->getAllItems();
		const lektor<Item*>* Bp = bpInv != NULL ? &bpInv->getAllItems() : NULL;
		const uint32_t bn = Bp != NULL ? Bp->size() : 0;
		int overlap = 0;
		int nestedConts = 0;
		for (uint32_t i = 0; i < A.size(); ++i)
		{
			if (A[i] == NULL)
				continue;
			if (A[i]->getInventory() != NULL)
				++nestedConts;
			for (uint32_t j = 0; j < bn; ++j)
				if (A[i] == (*Bp)[j]) { ++overlap; break; }
		}
		sum("WHERE ITEMS ARE: animal getAllItems()=%u | backpack inventory getAllItems()=%u | items present in BOTH lists=%d | "
			"container-like items inside animal list=%d (backpack item itself %s in animal list)",
			(unsigned)A.size(), (unsigned)bn, overlap, nestedConts,
			(bp != NULL && inv->hasItem(bp)) ? "IS" : "is NOT");

		// ---- per item
		PriceTotals tot;
		hand animalH = animal->getHandle();
		hand bpH;
		bool haveBpH = false;
		if (bp != NULL)
		{
			bpH = bp->getHandle();
			haveBpH = true;
		}
		ItemCtx ix;
		ix.animalH = &animalH;
		ix.bpH = haveBpH ? &bpH : NULL;
		ix.haveTrader = haveTrader;
		ix.traderMoney = traderMoney;
		ix.tp = tp;
		ix.tc = tc;
		ix.town = town;
		ix.guiMult = guiMult;
		ix.tot = &tot;
		int n = 0, errs = 0;
		for (uint32_t i = 0; i < A.size(); ++i)
		{
			if (A[i] == NULL)
				continue;
			bool inB = false;
			for (uint32_t j = 0; j < bn; ++j)
				if (A[i] == (*Bp)[j]) { inB = true; break; }
			ix.it = A[i];
			ix.n = ++n;
			ix.src = inB ? "A+B" : "A";
			if (!safeRun(itemOne, &ix, "itemOne"))
				++errs;
		}
		for (uint32_t j = 0; j < bn; ++j)
		{
			if ((*Bp)[j] == NULL)
				continue;
			bool inA = false;
			for (uint32_t i = 0; i < A.size(); ++i)
				if (A[i] == (*Bp)[j]) { inA = true; break; }
			if (inA)
				continue;
			ix.it = (*Bp)[j];
			ix.n = ++n;
			ix.src = "B";
			if (!safeRun(itemOne, &ix, "itemOne"))
				++errs;
		}
		sum("SUMMARY prices (%s): items listed=%d (exceptions=%d) equipped=%d stolen=%d illegal(for this trader culture)=%d non-empty containers=%d "
			"| sum valueAll(true) of non-equipped=%.0f | by our filter sellable items=%d sum=%.0f, x priceMultWhenITrade=%.0f "
			"| traderMoney=%d%s | guiMult=%.4f | lines printed=%d%s",
			px->where, tot.items, errs, tot.equipped, tot.stolen, tot.illegal, tot.containersNonEmpty, tot.sumAllNonEq,
			tot.sellable, tot.sumSellable, tot.sumSellablePM, traderMoney, haveTrader ? "" : " (no trader)", guiMult,
			g_lines, g_truncated ? " (TRUNCATED)" : "");
	}

	static void runPrices(const char* where, Character* forced)
	{
		PricesCtx px;
		px.forced = forced;
		px.where = where;
		safeRun(pricesBody, &px, "prices");
	}

	void ProbePrices()
	{
		beginProbe("prices");
		if (!worldReady("prices"))
			return;
		runPrices("command", NULL);
	}

	// ============================================================================
	// tradewin: watch for the trade window (InventoryGUI::getNPCTrader())
	// ============================================================================

	// POD (no global constructors)
	namespace
	{
	struct HandKey
	{
		unsigned type, container, containerSerial, index, serial;
	};
	}

	static bool g_twOn = false;
	static bool g_twOpen = false;
	static HandKey g_twKey = { 0, 0, 0, 0, 0 };
	static float g_twMult = 0.0f;
	static DWORD g_twLastPoll = 0;
	static bool g_twFirstPoll = false;

	static void twPoll(void*)
	{
		Character* t = InventoryGUI::getNPCTrader();
		float mult = InventoryGUI::getTraderPriceMultiplier();
		if (t == NULL)
		{
			if (g_twOpen)
			{
				g_twOpen = false;
				beginProbe("tradewin");
				sum("trade window CLOSED (getNPCTrader()==NULL), last multiplier=%.4f", mult);
			}
			return;
		}
		const hand& h = t->getHandle();
		bool same = g_twOpen && g_twKey.type == (unsigned)h.type && g_twKey.index == h.index && g_twKey.serial == h.serial
			&& g_twKey.container == h.container && g_twKey.containerSerial == h.containerSerial;
		if (same)
		{
			if (fabsf(mult - g_twMult) > 0.0001f)
			{
				g_twMult = mult;
				beginProbe("tradewin");
				sum("trade window multiplier changed: getTraderPriceMultiplier()=%.4f", mult);
			}
			return;
		}
		g_twOpen = true;
		g_twKey.type = (unsigned)h.type;
		g_twKey.index = h.index;
		g_twKey.serial = h.serial;
		g_twKey.container = h.container;
		g_twKey.containerSerial = h.containerSerial;
		g_twMult = mult;

		beginProbe("tradewin");
		ActivePlatoon* ap = t->getPlatoon();
		Platoon* p = ap != NULL ? ap->me : NULL;
		sum("trade window OPEN: trader '%s' hand=%s money=%d faction=%s getTraderPriceMultiplier()=%.4f priceMultWhenITrade=%.3f "
			"| compare the prices below with the numbers in the window (now run 'prices' while the window is open for the full list)",
			t->getName().c_str(), h.toString().c_str(), t->getMoney(), factionStr(t->getFaction()).c_str(), mult,
			p != NULL ? p->priceMultWhenITrade : -1.0f);
		runPrices("tradewin open", t);
	}

	void ProbeTradeWinToggle(int mode)
	{
		bool on = mode < 0 ? !g_twOn : (mode != 0);
		g_twOn = on;
		g_twOpen = false;
		g_twFirstPoll = true;
		beginProbe("tradewin");
		sum("trade window watcher %s (polls InventoryGUI::getNPCTrader() every 250 ms, read-only; no hook)", on ? "ON" : "OFF");
	}

	void ProbeTick()
	{
		if (!g_twOn)
			return;
		DWORD now = GetTickCount();
		if (now - g_twLastPoll < 250)
			return;
		g_twLastPoll = now;
		if (ou == NULL || ou->player == NULL || ou->isLoadingFromASaveGame() || ou->gameResetting)
			return;
		if (g_twFirstPoll)
		{
			g_twFirstPoll = false;
			sum("first poll");
		}
		safeRun(twPoll, NULL, "tradewin poll");
	}
}
