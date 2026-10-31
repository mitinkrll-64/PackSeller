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

// Stage 4: programmatic sale (no trade window, no animal movement).
//
// Flow of one "sell" command, all in one game frame on the main thread, so no
// game pointer is kept between frames (objects are additionally re-resolved by
// hand right before the first change):
//   A. plan  (read only, SEH-guarded): animal, trader, candidate items, prices
//   B. apply (only if not a dry run): per item a small transaction
//        1. trader->takeMoney(price)          verified by reading the money back
//        2. player wallet addMoney(price)     verified by reading the money back
//        3. removeItemAutoDestroy(item, n)    verified by stack quantity / item count
//      money is moved FIRST because it can be undone exactly, while destroying an
//      item cannot; if any step fails (or raises an SEH exception) the money of
//      both sides is restored by difference to the values read before the step 1.
//   C. report: before/after values and discrepancy against the calculation.

#include "Sell.h"
#include "Seh.h"
#include "Config.h"
#include "Log.h"
#include "MoveFsm.h"

#include <windows.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string>
#include <vector>

#include <kenshi/GameWorld.h>
#include <kenshi/Globals.h>
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
#include <kenshi/gui/InventoryGUI.h>
#include <kenshi/util/hand.h>
#include <ogre/OgreVector3.h>

namespace PS
{
	// ---- output with a line budget (same limiter as the probes) ----------------

	static int g_lines = 0;
	static bool g_truncated = false;
	// stage 5: SellCheckTraders runs the same evaluation silently
	static bool g_quiet = false;

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
		if (g_quiet)
			return;
		if (g_lines >= GetConfig().probeMaxLines)
		{
			if (!g_truncated)
			{
				g_truncated = true;
				LogInfo("sell: output truncated at probe_max_lines=%d", GetConfig().probeMaxLines);
			}
			return;
		}
		++g_lines;
		char buf[900];
		va_list args;
		va_start(args, fmt);
		vformat(buf, sizeof(buf), fmt, args);
		va_end(args);
		LogInfo("sell: %s", buf);
	}

	// summary / error line, always written
	static void sum(const char* fmt, ...)
	{
		char buf[900];
		va_list args;
		va_start(args, fmt);
		vformat(buf, sizeof(buf), fmt, args);
		va_end(args);
		LogInfo("sell: %s", buf);
	}

	// ---- SEH wrapper (no C++ objects with destructors in this function) --------

	typedef void (*SellFn)(void*);

	static bool safeRun(SellFn fn, void* ctx, const char* what)
	{
		__try
		{
			fn(ctx);
			return true;
		}
		__except (PsSehFilterCode(GetExceptionCode()))
		{
			sum("EXCEPTION 0x%08X in %s", (unsigned)GetExceptionCode(), what);
			return false;
		}
	}

	// ---- small helpers -----------------------------------------------------------

	static float dist2D(const Ogre::Vector3& a, const Ogre::Vector3& b)
	{
		float dx = a.x - b.x;
		float dz = a.z - b.z;
		return sqrtf(dx * dx + dz * dz);
	}

	static int roundPrice(double v)
	{
		return (int)(v + 0.5);
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

	static bool charUsable(Character* c, const char** why)
	{
		if (c == NULL) { *why = "NULL"; return false; }
		if (c->isDead()) { *why = "dead"; return false; }
		if (c->isDestroyed()) { *why = "destroyed"; return false; }
		if (c->isUnconcious()) { *why = "unconscious"; return false; }
		if (c->isKidnapped()) { *why = "kidnapped"; return false; }
		if (c->isBeingCarried()) { *why = "carried"; return false; }
		*why = "ok";
		return true;
	}

	// ---- data -----------------------------------------------------------------------

	namespace
	{
	struct PlanItem
	{
		Item* it;          // valid only inside the current command (one frame)
		int src;           // 0 = animal inventory, 1 = backpack inventory
		int qty;           // stack size at planning time
		int n;             // units to sell
		int price;         // money for n units
		std::string name;
	};
	}

	namespace
	{
	struct SellCtx
	{
		bool dry;
		bool windowOpen;
		// animal
		Character* animal;
		hand animalH;
		Inventory* inv;
		Inventory* bpInv;
		// trader
		Character* trader;
		hand traderH;
		const char* traderSrc;
		float traderDist;
		bool hostile;
		Platoon* tp;
		Ownerships* tOwn;
		TradeCulture* tc;
		// player wallet (faction-level Ownerships)
		Ownerships* wallet;
		// plan
		std::vector<PlanItem> plan;
		int planRevenue;
		int planUnits;
		bool planned;
		// stage 6: explicit animal / trader requested by the UI (hands only, no pointers)
		bool hasReqAnimal;
		bool hasReqTrader;
		int maxItems;      // stage 7: unit limit of this call (0 = sell_max_items from the config)
		hand reqAnimal;
		hand reqTrader;
		std::string animalName;
		std::string traderName;
		std::string failWhy;
		SellCtx()
			: dry(true), windowOpen(false), animal(NULL), inv(NULL), bpInv(NULL), trader(NULL)
			, traderSrc("none"), traderDist(-1.0f), hostile(false), tp(NULL), tOwn(NULL), tc(NULL)
			, wallet(NULL), planRevenue(0), planUnits(0), planned(false), hasReqAnimal(false), hasReqTrader(false), maxItems(0) {}
	};
	}

	namespace
	{
	struct MoneyState
	{
		int trader;       // trader money, -1 if unreadable
		int player;       // player wallet, -1 if unreadable
		int animalItems;  // Inventory::getNumItems() of the animal, -1 if unreadable
		int bpItems;      // same for the backpack inventory, -1 if none
	};
	}

	namespace
	{
	struct StateCtx
	{
		SellCtx* s;
		MoneyState* st;
	};
	}

	static void readStateFn(void* vp)
	{
		StateCtx* x = (StateCtx*)vp;
		SellCtx* s = x->s;
		MoneyState* st = x->st;
		st->trader = st->player = st->animalItems = st->bpItems = -1;
		Character* t = s->traderH.getCharacter();
		if (t != NULL)
			st->trader = t->getMoney();
		if (s->wallet != NULL)
			st->player = s->wallet->getMoney();
		Character* a = s->animalH.getCharacter();
		if (a != NULL)
		{
			Inventory* inv = a->getInventory();
			if (inv != NULL)
				st->animalItems = inv->getNumItems();
			ContainerItem* bp = a->hasABackpackOn();
			Inventory* bpi = bp != NULL ? bp->getInventory() : NULL;
			if (bpi != NULL)
				st->bpItems = bpi->getNumItems();
		}
	}

	static void readState(SellCtx* s, MoneyState* st)
	{
		StateCtx x;
		x.s = s;
		x.st = st;
		st->trader = st->player = st->animalItems = st->bpItems = -1;
		if (!safeRun(readStateFn, &x, "readState"))
		{
			// leave whatever was read before the exception
		}
	}

	// ---- trader selection ----------------------------------------------------------

	namespace
	{
	struct WinCtx
	{
		Character* t;
	};
	}

	static void winTraderFn(void* vp)
	{
		((WinCtx*)vp)->t = InventoryGUI::getNPCTrader();
	}

	static Character* windowTrader()
	{
		WinCtx w;
		w.t = NULL;
		safeRun(winTraderFn, &w, "InventoryGUI::getNPCTrader");
		return w.t;
	}

	namespace
	{
	struct ScanCtx
	{
		Character* c;
		Character* animal;
		const Ogre::Vector3* ref;
		float best;
		Character* bestC;
	};
	}

	static void scanOneFn(void* vp)
	{
		ScanCtx* x = (ScanCtx*)vp;
		Character* c = x->c;
		const char* why = "";
		if (c == x->animal || c->isAnimal() != NULL || !charUsable(c, &why))
			return;
		int fs = tplTraderFlag(c);
		if (fs != 1 && !(fs < 0 && c->isATrader()))
			return;
		float d = dist2D(c->getPosition(), *x->ref);
		if (d <= x->best)
		{
			x->best = d;
			x->bestC = c;
		}
	}

	// ---- candidate collection ------------------------------------------------------

	static bool containsItem(const std::vector<Item*>& v, Item* it)
	{
		for (size_t i = 0; i < v.size(); ++i)
			if (v[i] == it)
				return true;
		return false;
	}

	// items of the enabled, non-equipment sections of inv
	static void collectItems(const char* who, Inventory* inv, std::vector<Item*>& outList)
	{
		lektor<InventorySection*>& secs = inv->getAllSections();
		for (uint32_t i = 0; i < secs.size(); ++i)
		{
			InventorySection* s = secs[i];
			if (s == NULL)
				continue;
			if (!s->enabled)
			{
				out("%s section '%s' skipped: disabled (enabled=0), items=%u", who, s->name.c_str(), (unsigned)s->getItems().size());
				continue;
			}
			if (s->isAnEquippedItemSection)
			{
				out("%s section '%s' skipped: equipment section", who, s->name.c_str());
				continue;
			}
			const Ogre::vector<InventorySection::SectionItem>::type& v = s->getItems();
			out("%s section '%s' used: enabled=1 items=%u", who, s->name.c_str(), (unsigned)v.size());
			for (size_t j = 0; j < v.size(); ++j)
			{
				Item* it = v[j].item;
				if (it != NULL && !containsItem(outList, it))
					outList.push_back(it);
			}
		}
	}

	// price of n units of a stack with qty units
	static int priceFor(int n, int qty, int single, int all, float mult)
	{
		if (n == qty)
			return roundPrice((double)all * mult);
		return roundPrice((double)single * n * mult);
	}

	// Evaluates one item against the trader; appends to plan if it will be sold.
	static void evalItem(SellCtx* s, Item* it, int src, int* remaining, int* unitsLeft)
	{
		std::string nm = it->getName();
		const char* srcName = src == 0 ? "animal" : "backpack";
		if (it->isEquipped) { out("[%s] '%s' skip: equipped", srcName, nm.c_str()); return; }
		if (it->isUnique) { out("[%s] '%s' skip: unique item", srcName, nm.c_str()); return; }
		if (it->getInventory() != NULL) { out("[%s] '%s' skip: container", srcName, nm.c_str()); return; }
		if (it->isStolen(false) && !(s->tp != NULL && s->tp->iBuyStolenGoods(it)))
		{
			out("[%s] '%s' skip: stolen, trader does not buy stolen goods", srcName, nm.c_str());
			return;
		}
		if (s->tc != NULL && it->data != NULL && s->tc->isItemIllegal(it->data) && !(s->tp != NULL && s->tp->_iBuyIllegalGoods))
		{
			out("[%s] '%s' skip: illegal for this trader", srcName, nm.c_str());
			return;
		}
		int qty = it->quantity;
		if (qty <= 0) { out("[%s] '%s' skip: quantity %d", srcName, nm.c_str(), qty); return; }
		int single = it->getValueSingle(true);
		int all = it->getValueAll(true);
		if (all <= 0 || single <= 0) { out("[%s] '%s' skip: price<=0 (single=%d all=%d)", srcName, nm.c_str(), single, all); return; }
		if (*unitsLeft <= 0) { out("[%s] '%s' skip: sell_max_items reached", srcName, nm.c_str()); return; }
		int maxAff = it->getMaxAffordableNum(*remaining, true);
		if (maxAff <= 0) { out("[%s] '%s' skip: trader cannot afford (remaining money %d, getMaxAffordableNum=%d)", srcName, nm.c_str(), *remaining, maxAff); return; }
		float mult = GetConfig().sellPriceMult;
		int n = qty;
		if (n > maxAff) n = maxAff;
		if (n > *unitsLeft) n = *unitsLeft;
		int price = priceFor(n, qty, single, all, mult);
		while (n > 0 && price > *remaining)
		{
			--n;
			price = priceFor(n, qty, single, all, mult);
		}
		if (n <= 0 || price <= 0) { out("[%s] '%s' skip: nothing fits the trader's money %d", srcName, nm.c_str(), *remaining); return; }

		PlanItem p;
		p.it = it;
		p.src = src;
		p.qty = qty;
		p.n = n;
		p.price = price;
		p.name = nm;
		s->plan.push_back(p);
		*remaining -= price;
		*unitsLeft -= n;
		s->planRevenue += price;
		s->planUnits += n;
		out("[%s] '%s' PLAN: qty=%d sell=%d | getValueSingle(true)=%d getValueAll(true)=%d x mult %.3f -> price=%d | getMaxAffordableNum=%d | trader money left after=%d",
			srcName, nm.c_str(), qty, n, single, all, mult, price, maxAff, *remaining);
	}

	namespace
	{
	struct EvalCtx
	{
		SellCtx* s;
		Item* it;
		int src;
		int* remaining;
		int* unitsLeft;
	};
	}

	static void evalFn(void* vp)
	{
		EvalCtx* x = (EvalCtx*)vp;
		evalItem(x->s, x->it, x->src, x->remaining, x->unitsLeft);
	}

	// ---- phase A: plan (read only) --------------------------------------------------

	static void planFn(void* vp)
	{
		SellCtx* s = (SellCtx*)vp;
		s->planned = false;

		const char* asrc = "none";
		Character* animal = NULL;
		if (s->hasReqAnimal)
		{
			const char* aw = "";
			animal = s->reqAnimal.getCharacter();
			asrc = "requested by the window";
			if (animal != NULL && (animal->isAnimal() == NULL || !charUsable(animal, &aw)))
			{
				sum("requested animal is not usable (%s)", animal->isAnimal() == NULL ? "not an animal" : aw);
				animal = NULL;
			}
		}
		else
			animal = MoveFsmPickAnimal(&asrc);
		if (animal == NULL)
		{
			s->failWhy = "no usable animal";
			sum("no usable animal (select the pack animal or keep it in the squad)");
			return;
		}
		s->animalName = animal->getName();
		s->animal = animal;
		s->animalH = animal->getHandle();
		s->inv = animal->getInventory();
		ContainerItem* bp = animal->hasABackpackOn();
		s->bpInv = bp != NULL ? bp->getInventory() : NULL;
		sum("animal: '%s' hand=%s source=%s inventory=%p backpack=%s backpackInventory=%p",
			animal->getName().c_str(), s->animalH.toString().c_str(), asrc, (void*)s->inv,
			bp != NULL ? bp->getName().c_str() : "none", (void*)s->bpInv);
		if (s->inv == NULL)
		{
			sum("animal inventory is NULL; stop");
			return;
		}

		// player wallet: faction-level Ownerships (api-notes section 4)
		Faction* pf = ou->player->getFaction();
		s->wallet = pf != NULL ? pf->factionOwnerships : NULL;

		// trader: the trade window trader if a window is open, else the nearest trader in probe_trade_radius
		Character* wt = windowTrader();
		s->windowOpen = wt != NULL;
		Character* trader = NULL;
		const char* why = "";
		if (s->hasReqTrader)
		{
			// stage 6: the trader chosen in the window; no fallback to the window trader / nearest one
			Character* rt = s->reqTrader.getCharacter();
			if (rt == NULL || rt == animal || rt->isAnimal() != NULL || !charUsable(rt, &why))
			{
				s->failWhy = "requested trader is gone or unusable";
				sum("requested trader is not usable (%s)", rt == NULL ? "unresolved hand" : why);
				return;
			}
			trader = rt;
			s->traderSrc = "requested by the window (list row)";
			s->traderDist = dist2D(rt->getPosition(), animal->getPosition());
		}
		else if (wt != NULL)
		{
			if (wt != animal && charUsable(wt, &why))
			{
				trader = wt;
				s->traderSrc = "trade window (InventoryGUI::getNPCTrader)";
				s->traderDist = dist2D(wt->getPosition(), animal->getPosition());
			}
			else
				sum("trade window trader rejected (%s)", why);
		}
		if (trader == NULL && !s->hasReqTrader)
		{
			Ogre::Vector3 ref = animal->getPosition();
			ScanCtx sc;
			sc.animal = animal;
			sc.ref = &ref;
			sc.best = GetConfig().probeTradeRadius;
			sc.bestC = NULL;
			const ogre_unordered_set<Character*>::type& list = ou->getCharacterUpdateList();
			for (ogre_unordered_set<Character*>::type::const_iterator it = list.begin(); it != list.end(); ++it)
			{
				if (*it == NULL)
					continue;
				sc.c = *it;
				safeRun(scanOneFn, &sc, "trader scan");
			}
			if (sc.bestC != NULL)
			{
				trader = sc.bestC;
				s->traderSrc = "nearest trader within probe_trade_radius";
				s->traderDist = sc.best;
			}
		}
		if (trader == NULL)
		{
			s->failWhy = "no trader";
			sum("no trader (no usable window trader and none within probe_trade_radius=%.0f of the animal)", GetConfig().probeTradeRadius);
			return;
		}
		s->trader = trader;
		s->traderH = trader->getHandle();
		s->traderName = trader->getName();
		ActivePlatoon* ap = trader->getPlatoon();
		s->tp = ap != NULL ? ap->me : NULL;
		s->tOwn = s->tp != NULL ? s->tp->getOwnerships() : NULL;
		TownBase* cur = trader->getCurrentTownLocation();
		Town* town = cur != NULL ? cur->isTown() : NULL;
		if (town != NULL)
			s->tc = town->tradeCulture;
		Faction* tf = trader->getFaction();
		if (s->tc == NULL && tf != NULL)
			s->tc = &tf->tradeCulture;
		s->hostile = tf != NULL && tf->relations != NULL && pf != NULL && tf->relations->isEnemy(pf);
		int tMoney = trader->getMoney();
		sum("trader: '%s' hand=%s source=%s dist2D=%.1f money=%d town='%s' faction='%s' HOSTILE=%d (not blocking at stage 4) "
			"priceMultWhenITrade=%.3f buysStolen=%d buysIllegal=%d ownerships=%p tradeCulture=%p windowOpen=%d guiMult=%.4f",
			trader->getName().c_str(), s->traderH.toString().c_str(), s->traderSrc, s->traderDist, tMoney,
			cur != NULL ? cur->getKnownName().c_str() : "none", tf != NULL ? tf->getName().c_str() : "n/a",
			s->hostile ? 1 : 0, s->tp != NULL ? s->tp->priceMultWhenITrade : -1.0f,
			s->tp != NULL ? (s->tp->_iBuyStolenGoods ? 1 : 0) : -1, s->tp != NULL ? (s->tp->_iBuyIllegalGoods ? 1 : 0) : -1,
			(void*)s->tOwn, (void*)s->tc, s->windowOpen ? 1 : 0, InventoryGUI::getTraderPriceMultiplier());

		// candidates
		std::vector<Item*> aItems;
		std::vector<Item*> bItems;
		collectItems("animal", s->inv, aItems);
		if (s->bpInv != NULL)
			collectItems("backpack", s->bpInv, bItems);
		sum("candidates: animal=%u backpack=%u (enabled non-equipment sections only)", (unsigned)aItems.size(), (unsigned)bItems.size());

		int remaining = tMoney;
		int unitsLeft = s->maxItems > 0 ? s->maxItems : GetConfig().sellMaxItems;
		EvalCtx ex;
		ex.s = s;
		ex.remaining = &remaining;
		ex.unitsLeft = &unitsLeft;
		for (size_t i = 0; i < aItems.size(); ++i)
		{
			ex.it = aItems[i];
			ex.src = 0;
			safeRun(evalFn, &ex, "evalItem(animal)");
		}
		for (size_t i = 0; i < bItems.size(); ++i)
		{
			ex.it = bItems[i];
			ex.src = 1;
			safeRun(evalFn, &ex, "evalItem(backpack)");
		}
		s->planned = true;
	}

	// ---- phase B: one transaction ---------------------------------------------------

	namespace
	{
	struct TxCtx
	{
		SellCtx* s;
		const PlanItem* p;
		Inventory* srcInv;
		int stage;        // 0 nothing changed yet, 1 money step started, 2 player credited, 3 committed
		int tBefore;
		int pBefore;
		int cntBefore;
		const char* fail; // NULL = success
		char failBuf[200];
		TxCtx() : s(NULL), p(NULL), srcInv(NULL), stage(0), tBefore(0), pBefore(0), cntBefore(0), fail(NULL) { failBuf[0] = '\0'; }
	};
	}

	// 0 = item not removed, 1 = removed (n units gone), 2 = ambiguous (something else changed)
	static int removalState(const TxCtx* tx, int* removedUnits, int* cntDelta)
	{
		Inventory* inv = tx->srcInv;
		bool has = inv->hasItem(tx->p->it);   // pointer comparison, the item may already be destroyed
		int cur = has ? tx->p->it->quantity : 0;
		*removedUnits = tx->p->qty - cur;
		*cntDelta = tx->cntBefore - inv->getNumItems();
		if (*removedUnits == tx->p->n)
			return 1;
		if (*removedUnits == 0 && *cntDelta == 0)
			return 0;
		return 2;
	}

	// brings the wallet to 'target' by difference; returns the resulting money
	static int restoreMoney(Ownerships* o, int target)
	{
		int cur = o->getMoney();
		if (cur < target)
			o->addMoney(target - cur);
		else if (cur > target)
			o->takeMoney(cur - target);
		return o->getMoney();
	}

	static void execFn(void* vp)
	{
		TxCtx* tx = (TxCtx*)vp;
		SellCtx* s = tx->s;
		const PlanItem* p = tx->p;
		tx->fail = NULL;
		tx->stage = 0;

		// re-resolve everything by hand / fresh getters
		Character* trader = s->traderH.getCharacter();
		Character* animal = s->animalH.getCharacter();
		const char* why = "";
		if (trader == NULL || trader != s->trader || !charUsable(trader, &why))
		{
			_snprintf(tx->failBuf, sizeof(tx->failBuf) - 1, "trader no longer valid (%s)", trader == NULL ? "unresolved" : why);
			tx->fail = tx->failBuf;
			return;
		}
		if (animal == NULL || animal != s->animal || !charUsable(animal, &why))
		{
			_snprintf(tx->failBuf, sizeof(tx->failBuf) - 1, "animal no longer valid (%s)", animal == NULL ? "unresolved" : why);
			tx->fail = tx->failBuf;
			return;
		}
		if (s->wallet == NULL || s->tOwn == NULL)
		{
			tx->fail = "wallet or trader ownerships is NULL";
			return;
		}
		Inventory* inv = NULL;
		if (p->src == 0)
			inv = animal->getInventory();
		else
		{
			ContainerItem* bp = animal->hasABackpackOn();
			inv = bp != NULL ? bp->getInventory() : NULL;
		}
		if (inv == NULL || inv != tx->srcInv)
		{
			tx->fail = "source inventory changed";
			return;
		}
		if (!inv->hasItem(p->it))
		{
			tx->fail = "item is no longer in the inventory";
			return;
		}
		if (p->it->quantity != p->qty)
		{
			tx->fail = "item quantity changed since planning";
			return;
		}
		tx->tBefore = trader->getMoney();
		tx->pBefore = s->wallet->getMoney();
		tx->cntBefore = inv->getNumItems();
		if (tx->tBefore < p->price)
		{
			tx->fail = "trader money is below the price (would go negative)";
			return;
		}

		// step 1: trader pays
		tx->stage = 1;
		bool r1 = trader->takeMoney(p->price);
		int t1 = trader->getMoney();
		if (!r1 || t1 != tx->tBefore - p->price)
		{
			_snprintf(tx->failBuf, sizeof(tx->failBuf) - 1, "trader debit failed: takeMoney=%d money %d -> %d (expected %d)",
				r1 ? 1 : 0, tx->tBefore, t1, tx->tBefore - p->price);
			tx->fail = tx->failBuf;
			return;
		}
		// step 2: player is paid
		s->wallet->addMoney(p->price);
		int p1 = s->wallet->getMoney();
		if (p1 != tx->pBefore + p->price)
		{
			_snprintf(tx->failBuf, sizeof(tx->failBuf) - 1, "player credit mismatch: money %d -> %d (expected %d)",
				tx->pBefore, p1, tx->pBefore + p->price);
			tx->fail = tx->failBuf;
			return;
		}
		tx->stage = 2;
		// step 3: the item is destroyed (decision of the lead: sold items are destroyed). p->it may dangle afterwards.
		bool r3 = inv->removeItemAutoDestroy(p->it, p->n);
		int ru = 0, cd = 0;
		int st = removalState(tx, &ru, &cd);
		out("remove '%s': removeItemAutoDestroy(n=%d)=%d removedUnits=%d itemCountDelta=%d state=%d (0 none, 1 removed, 2 ambiguous)",
			p->name.c_str(), p->n, r3 ? 1 : 0, ru, cd, st);
		if (st == 0)
		{
			tx->fail = "item was not removed";
			return;
		}
		if (st == 2)
			sum("WARNING ambiguous removal of '%s': removedUnits=%d expected %d, itemCountDelta=%d; money kept (items must not be lost without payment)",
				p->name.c_str(), ru, p->n, cd);
		tx->stage = 3;
	}

	static void recoverFn(void* vp)
	{
		TxCtx* tx = (TxCtx*)vp;
		SellCtx* s = tx->s;
		int ru = 0, cd = 0;
		int st = removalState(tx, &ru, &cd);
		bool commit = st != 0;   // item gone (fully or ambiguously) -> keep the payment
		int tTarget = commit ? tx->tBefore - tx->p->price : tx->tBefore;
		int pTarget = commit ? tx->pBefore + tx->p->price : tx->pBefore;
		int tNow = restoreMoney(s->tOwn, tTarget);
		int pNow = restoreMoney(s->wallet, pTarget);
		sum("recovery for '%s': removalState=%d -> %s; trader money %d (target %d), player money %d (target %d)%s",
			tx->p->name.c_str(), st, commit ? "payment kept" : "payment rolled back", tNow, tTarget, pNow, pTarget,
			(tNow == tTarget && pNow == pTarget) ? "" : "  !!! RECOVERY MISMATCH");
	}

	// ---- stage 5: "does this trader buy at least one unit from the pack animal" ----------

	namespace
	{
	struct CheckOneCtx
	{
		SellCtx* s;
		Character* trader;
		std::vector<Item*>* aItems;
		std::vector<Item*>* bItems;
		int result;
		int limit;       // units to plan: 1 = "buys anything" (stops at the first item), more = full plan for a revenue estimate
		int revenue;     // planned revenue (limit > 1)
	};
	}

	static void checkOneFn(void* vp)
	{
		CheckOneCtx* x = (CheckOneCtx*)vp;
		SellCtx* s = x->s;
		Character* trader = x->trader;
		x->result = -1;
		s->plan.clear();
		s->planRevenue = 0;
		s->planUnits = 0;
		s->trader = trader;
		ActivePlatoon* ap = trader->getPlatoon();
		s->tp = ap != NULL ? ap->me : NULL;
		s->tOwn = s->tp != NULL ? s->tp->getOwnerships() : NULL;
		s->tc = NULL;
		TownBase* cur = trader->getCurrentTownLocation();
		Town* town = cur != NULL ? cur->isTown() : NULL;
		if (town != NULL)
			s->tc = town->tradeCulture;
		Faction* tf = trader->getFaction();
		if (s->tc == NULL && tf != NULL)
			s->tc = &tf->tradeCulture;
		int remaining = trader->getMoney();
		int unitsLeft = x->limit;
		EvalCtx ex;
		ex.s = s;
		ex.remaining = &remaining;
		ex.unitsLeft = &unitsLeft;
		for (int pass = 0; pass < 2 && (x->limit > 1 || s->plan.empty()); ++pass)
		{
			std::vector<Item*>* list = pass == 0 ? x->aItems : x->bItems;
			for (size_t i = 0; i < list->size() && (x->limit > 1 || s->plan.empty()); ++i)
			{
				ex.it = (*list)[i];
				ex.src = pass;
				safeRun(evalFn, &ex, "evalItem(check)");
			}
		}
		x->result = s->plan.empty() ? 0 : 1;
		x->revenue = s->planRevenue;
	}

	namespace
	{
	struct CheckAllCtx
	{
		Character* animal;
		Character* const* traders;
		int count;
		int* result;
		int limit;
		int* revenue;    // may be NULL
	};
	}

	static void checkAllFn(void* vp)
	{
		CheckAllCtx* x = (CheckAllCtx*)vp;
		Inventory* inv = x->animal->getInventory();
		if (inv == NULL)
			return;
		ContainerItem* bp = x->animal->hasABackpackOn();
		Inventory* bpInv = bp != NULL ? bp->getInventory() : NULL;
		std::vector<Item*> aItems;
		std::vector<Item*> bItems;
		collectItems("animal", inv, aItems);
		if (bpInv != NULL)
			collectItems("backpack", bpInv, bItems);
		SellCtx s;
		s.dry = true;
		for (int i = 0; i < x->count; ++i)
		{
			if (x->traders[i] == NULL)
				continue;
			CheckOneCtx c;
			c.s = &s;
			c.trader = x->traders[i];
			c.aItems = &aItems;
			c.bItems = &bItems;
			c.result = -1;
			c.limit = x->limit;
			c.revenue = 0;
			if (safeRun(checkOneFn, &c, "check trader"))
			{
				x->result[i] = c.result;
				if (x->revenue != NULL)
					x->revenue[i] = c.revenue;
			}
		}
	}

	static void runCheck(Character* animal, Character* const* traders, int count, int limit, int* result, int* revenue)
	{
		for (int i = 0; i < count; ++i)
		{
			result[i] = -1;
			if (revenue != NULL)
				revenue[i] = 0;
		}
		if (animal == NULL || traders == NULL || count <= 0)
			return;
		bool prev = g_quiet;
		g_quiet = true;
		CheckAllCtx x;
		x.animal = animal;
		x.traders = traders;
		x.count = count;
		x.result = result;
		x.limit = limit;
		x.revenue = revenue;
		safeRun(checkAllFn, &x, "SellCheckTraders");
		g_quiet = prev;
	}

	void SellCheckTraders(Character* animal, Character* const* traders, int count, int* result)
	{
		runCheck(animal, traders, count, 1, result, NULL);
	}

	void SellEstimateTraders(Character* animal, Character* const* traders, int count, int maxItems, int* revenue)
	{
		std::vector<int> res(count > 0 ? count : 1, -1);
		runCheck(animal, traders, count, maxItems > 1 ? maxItems : 2, &res[0], revenue);
	}

	// ---- command ---------------------------------------------------------------------

	static void logState(const char* tag, const MoneyState& st)
	{
		sum("%s: player money=%d | trader money=%d | animal getNumItems()=%d | backpack getNumItems()=%d",
			tag, st.player, st.trader, st.animalItems, st.bpItems);
	}

	void SellRun(bool forceDry)
	{
		SellRunFor(forceDry, NULL, NULL, NULL);
	}

	void SellRunFor(bool forceDry, const hand* reqAnimal, const hand* reqTrader, SellOutcome* oc, int maxItems, bool notify)
	{
		SellOutcome local;
		if (oc == NULL)
			oc = &local;
		*oc = SellOutcome();
		g_lines = 0;
		g_truncated = false;
		const Config& cfg = GetConfig();
		bool dry = forceDry || cfg.sellDryRun;
		sum("---- begin %s (sell_dry_run=%d max_items=%d sell_price_mult=%.3f%s) ----",
			forceDry ? "'sell dry'" : "'sell'", cfg.sellDryRun ? 1 : 0, maxItems > 0 ? maxItems : cfg.sellMaxItems, cfg.sellPriceMult,
			(reqAnimal != NULL || reqTrader != NULL) ? ", explicit animal/trader from the window" : "");
		oc->dry = dry;
		if (dry)
			sum("DRY RUN: nothing will be changed%s", forceDry ? " (command 'sell dry')" : " (sell_dry_run=1)");
		else
			sum("REAL SALE: items will be destroyed and money moved");

		if (ou == NULL || ou->player == NULL || shou == NULL)
		{
			sum("refused: world not ready (ou/player/shou is NULL)");
			oc->error = "world not ready";
			return;
		}
		if (ou->isLoadingFromASaveGame() || ou->gameResetting)
		{
			sum("refused: world is loading/resetting");
			oc->error = "world is loading";
			return;
		}

		SellCtx s;
		s.dry = dry;
		s.maxItems = maxItems;
		if (reqAnimal != NULL)
		{
			s.hasReqAnimal = true;
			s.reqAnimal = *reqAnimal;
		}
		if (reqTrader != NULL)
		{
			s.hasReqTrader = true;
			s.reqTrader = *reqTrader;
		}
		if (!safeRun(planFn, &s, "plan"))
		{
			sum("plan aborted by an exception; nothing was changed");
			oc->error = "exception while planning";
			return;
		}
		oc->ran = true;
		oc->animalName = s.animalName;
		oc->traderName = s.traderName;
		if (!s.planned)
		{
			sum("no plan (see the lines above); nothing was changed");
			oc->error = s.failWhy.empty() ? "no plan" : s.failWhy;
			return;
		}
		oc->planned = true;
		oc->planUnits = s.planUnits;
		oc->planRevenue = s.planRevenue;

		MoneyState before;
		readState(&s, &before);
		logState("BEFORE", before);
		sum("PLAN: %u stack(s), %d unit(s), expected revenue=%d (player money %d -> %d, trader money %d -> %d)",
			(unsigned)s.plan.size(), s.planUnits, s.planRevenue,
			before.player, before.player + s.planRevenue, before.trader, before.trader - s.planRevenue);

		int soldUnits = 0, soldMoney = 0, soldStacks = 0;
		bool aborted = false;
		if (s.plan.empty())
			sum("nothing to sell (no item passed the filters)");
		else if (dry)
			sum("DRY RUN: not applying the plan");
		else if (s.windowOpen)
		{
			sum("REFUSED real sale: a trade window is open (getNPCTrader() != NULL). Close the trade window and the inventory windows, then repeat. 'sell dry' works with the window open.");
			aborted = true;
			oc->error = "trade window is open";
		}
		else if (s.wallet == NULL || s.tOwn == NULL)
		{
			sum("REFUSED real sale: wallet=%p trader ownerships=%p (money cannot be moved safely)", (void*)s.wallet, (void*)s.tOwn);
			aborted = true;
			oc->error = "wallet or trader ownerships unavailable";
		}
		else
		{
			for (size_t i = 0; i < s.plan.size(); ++i)
			{
				TxCtx tx;
				tx.s = &s;
				tx.p = &s.plan[i];
				tx.srcInv = s.plan[i].src == 0 ? s.inv : s.bpInv;
				bool ok = safeRun(execFn, &tx, "apply");
				if (!ok || tx.fail != NULL)
				{
					LogError("sell: item '%s' FAILED at stage %d: %s", s.plan[i].name.c_str(), tx.stage,
						ok ? tx.fail : "exception (see above)");
					oc->error = "item transaction failed";
					if (tx.stage > 0 && tx.stage < 3)
						safeRun(recoverFn, &tx, "recovery");
					aborted = true;
					break;
				}
				++soldStacks;
				soldUnits += s.plan[i].n;
				soldMoney += s.plan[i].price;
				sum("SOLD '%s' x%d for %d", s.plan[i].name.c_str(), s.plan[i].n, s.plan[i].price);
			}
		}

		MoneyState after;
		readState(&s, &after);
		logState("AFTER", after);
		int dPlayer = after.player - before.player;
		int dTrader = after.trader - before.trader;
		int dAnimal = after.animalItems - before.animalItems;
		int dBp = after.bpItems - before.bpItems;
		int dItems = 0;
		if (before.animalItems >= 0 && after.animalItems >= 0) dItems += dAnimal;
		if (before.bpItems >= 0 && after.bpItems >= 0) dItems += dBp;
		oc->aborted = aborted;
		oc->soldUnits = soldUnits;
		oc->soldMoney = soldMoney;
		sum("DELTA: player %+d (expected %+d, diff %d) | trader %+d (expected %+d, diff %d) | items %+d (expected %+d, diff %d) | "
			"sold stacks=%d units=%d money=%d%s%s",
			dPlayer, soldMoney, dPlayer - soldMoney, dTrader, -soldMoney, dTrader + soldMoney,
			dItems, -soldUnits, dItems + soldUnits, soldStacks, soldUnits, soldMoney,
			aborted ? " | ABORTED" : "", dry ? " | dry run" : "");
		if (notify && !dry && !aborted && soldStacks > 0)
		{
			char msg[96];
			_snprintf(msg, sizeof(msg) - 1, "Sold %d item(s) for %d", soldUnits, soldMoney);
			msg[sizeof(msg) - 1] = '\0';
			Notify(msg);
		}
		sum("---- end (lines %d%s) ----", g_lines, g_truncated ? ", TRUNCATED" : "");
	}
}
