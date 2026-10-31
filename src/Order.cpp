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

// Stage 7: the order cycle. See Order.h and docs/api-notes.md ("Этап 7").
//
// Rules used here:
//  * one OrderRec object, the state changes ONLY in transition();
//  * game objects (animal, trader) are re-resolved by hand on every update, no pointer is kept
//    between frames; the persistent part of the order (OrderPersist) holds only hands and numbers;
//  * every update runs under SEH; an exception drops the order to Idle (logged);
//  * every error exit has a code (logged as "state A -> B, reason=CODE") and a player message.

#include "Order.h"
#include "Seh.h"
#include "ZoneProbe.h"
#include "Config.h"
#include "Lang.h"
#include "LoadGate.h"
#include "Log.h"
#include "MoveFsm.h"
#include "Sell.h"
#include "TraderList.h"

#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include <kenshi/GameWorld.h>
#include <kenshi/Globals.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/Character.h>
#include <kenshi/CharacterAnimal.h>
#include <kenshi/CharMovement.h>
#include <kenshi/Town.h>
#include <kenshi/Tasker.h>
#include <kenshi/Enums.h>
#include <kenshi/AI/AITaskSystem.h>
#include <kenshi/util/hand.h>
#include <ogre/OgreVector3.h>

namespace PS
{
	// Number of updates after issuing a move order during which "order missing", "pathFailed",
	// "stuck" and the foreign-target check are ignored (the AI needs a moment to register the task).
	static const unsigned kGraceUpdates = 10;
	// Consecutive checks required to confirm "order missing" / "pathFailed" / "trader not loaded".
	static const int kConfirmChecks = 3;
	// ORDER_ENDED_FAR is converted to OVERRIDDEN if the task location changed within this many
	// game seconds before the task disappeared.
	static const double kRetargetWindow = 30.0;
	// A move task whose location is farther than arrival_radius * this from the reference one is foreign.
	static const float kForeignTargetFactor = 2.0f;
	// "Near" for the error exits of the Going phase = trade_range * this (the order may end ~25 units
	// before the target, stage 3 log; a trader inside a building cannot be reached exactly).
	static const float kNearFactor = 1.5f;
	// Real-time hard cap of a phase = order_timeout_seconds * this (active, i.e. unpaused, real time).
	static const float kRealCapFactor = 5.0f;
	// One frame delta longer than this (ms) is cut (loading hitches).
	static const DWORD kMaxTickDeltaMs = 2000;

	static const int kMaxCands = 32;

	// The part of the order that stage 8 will save (docs/api-notes.md): hands and numbers only.
	namespace
	{
	struct OrderPersist
	{
		int state;               // OrderState
		Ogre::Vector3 start;     // return point (animal position when the order was given)
		Ogre::Vector3 dest;      // current move destination (approach point, trader position or start)
		int retries;             // re-sent move orders in the current phase
		float phaseSeconds;      // tick seconds spent in the current phase (valid when saved)
		hand animal;             // pack animal id (hand::toString / fromString)
		hand trader;             // trader id

		OrderPersist() : state(OS_IDLE), start(0, 0, 0), dest(0, 0, 0), retries(0), phaseSeconds(0.0f) {}
	};
	}

	namespace
	{
	struct OrderRec
	{
		// runtime only (never saved)
		bool hasRecord;
		char animalName[64];     // plain buffers: no std::string with a game-allocated value in a long-lived object
		char traderName[64];
		char animalStr[64];      // hand::toString of the animal / trader, kept as text for the save hook
		char traderStr[64];
		float phaseTimeoutSec;   // timeout of the current phase, scaled with the distance (0.2)
		double stateGame0;       // g_gameTime at phase entry
		double stateReal0;       // g_realMs at phase entry
		Ogre::Vector3 stateStartPos;
		unsigned updates;        // updates since the last move order
		int foreignCount;
		int missingOrder;
		int pathFailedCount;
		int traderUnloaded;
		double stuckSince;       // g_gameTime when "stuck" began, <0 = not stuck
		DWORD lastStatusReal;
		Ogre::Vector3 lastStatusPos;
		Ogre::Vector3 prevTaskLoc;
		bool havePrevTaskLoc;
		Ogre::Vector3 refTaskLoc;   // reference location of our move task (after the game snapped it)
		bool haveRefTaskLoc;
		double lastRetargetGame;    // g_gameTime when the task location changed, <0 = never
		bool unlistedLogged;
		bool snapLogged;
		// approach points (trader inside a building): plain arrays, rebuilt on load, never saved
		float candX[kMaxCands];
		float candZ[kMaxCands];
		float candY;
		char candTried[kMaxCands];
		int candCount;
		int candCur;
		bool useApproach;
		// persistent part
		OrderPersist p;

		OrderRec()
			: hasRecord(false), phaseTimeoutSec(300.0f), stateGame0(0), stateReal0(0), stateStartPos(0, 0, 0), updates(0)
			, foreignCount(0), missingOrder(0), pathFailedCount(0), traderUnloaded(0), stuckSince(-1.0)
			, lastStatusReal(0), lastStatusPos(0, 0, 0), prevTaskLoc(0, 0, 0), havePrevTaskLoc(false)
			, refTaskLoc(0, 0, 0), haveRefTaskLoc(false), lastRetargetGame(-1.0), unlistedLogged(false), snapLogged(false)
			, candY(0.0f), candCount(0), candCur(-1), useApproach(false)
		{
			animalName[0] = 0;
			traderName[0] = 0;
			animalStr[0] = 0;
			traderStr[0] = 0;
			for (int i = 0; i < kMaxCands; ++i)
			{
				candX[i] = 0.0f;
				candZ[i] = 0.0f;
				candTried[i] = 0;
			}
		}
	};
	}

	// Allocated lazily on the first start (hand constructors are KenshiLib calls: no global constructor).
	static OrderRec* g_order = NULL;

	// tick bookkeeping
	static double g_gameTime = 0.0;      // sum of the hook's 'time' while not paused
	static double g_realMs = 0.0;        // active (unpaused) real milliseconds
	static DWORD g_lastTickReal = 0;
	static DWORD g_lastUpdateReal = 0;
	static LoadGate g_gate;     // zero-initialised POD (short loading flag tolerance)

	const char* OrderStateName(OrderState s)
	{
		switch (s)
		{
		case OS_IDLE: return "Idle";
		case OS_GOING: return "Going";
		case OS_SELLING: return "Selling";
		case OS_RETURNING: return "Returning";
		}
		return "?";
	}

	OrderState OrderGetState()
	{
		return g_order ? (OrderState)g_order->p.state : OS_IDLE;
	}

	bool OrderActive()
	{
		return OrderGetState() != OS_IDLE;
	}

	static OrderRec& order()
	{
		if (!g_order)
		{
			g_order = new OrderRec();
		}
		return *g_order;
	}

	// ---- SEH wrapper (no C++ objects with destructors in this function) ----------

	typedef void (*OrderFn)(void*);

	// details of the last SEH exception (filled by the filter, logged by safeRun)
	static DWORD g_exCode = 0;
	static void* g_exAddr = NULL;
	static ULONG_PTR g_exInfo0 = 0;
	static ULONG_PTR g_exInfo1 = 0;

	static int sehFilter(EXCEPTION_POINTERS* ep)
	{
		EXCEPTION_RECORD* r = ep->ExceptionRecord;
		g_exCode = r->ExceptionCode;
		g_exAddr = r->ExceptionAddress;
		g_exInfo0 = r->NumberParameters > 0 ? r->ExceptionInformation[0] : 0;
		g_exInfo1 = r->NumberParameters > 1 ? r->ExceptionInformation[1] : 0;
		return PsSehFilterCode(r->ExceptionCode);
	}

	static void logException(const char* what)
	{
		char mod[MAX_PATH] = { 0 };
		HMODULE hm = NULL;
		unsigned long long off = 0;
		if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)g_exAddr, &hm) && hm != NULL)
		{
			GetModuleFileNameA(hm, mod, MAX_PATH);
			off = (unsigned long long)((char*)g_exAddr - (char*)hm);
		}
		LogError("order: EXCEPTION 0x%08X in %s: at %p (module %s +0x%llX), access=%s (info0=%u), target address=%p",
			(unsigned)g_exCode, what, g_exAddr, mod[0] ? mod : "?", off,
			(g_exCode == EXCEPTION_ACCESS_VIOLATION) ? (g_exInfo0 == 0 ? "READ" : (g_exInfo0 == 1 ? "WRITE" : "EXEC/DEP")) : "n/a",
			(unsigned)g_exInfo0, (void*)g_exInfo1);
	}

	static bool safeRun(OrderFn fn, void* ctx, const char* what)
	{
		__try
		{
			fn(ctx);
			return true;
		}
		__except (sehFilter(GetExceptionInformation()))
		{
			logException(what);
			return false;
		}
	}

	// ---- helpers ---------------------------------------------------------------------

	static float dist2D(const Ogre::Vector3& a, const Ogre::Vector3& b)
	{
		float dx = a.x - b.x;
		float dz = a.z - b.z;
		return sqrtf(dx * dx + dz * dz);
	}

	// compare identifying fields (hand::operator== is virtual and gave a false mismatch, decisions.md stage 3)
	static bool sameHand(const hand& a, const hand& b)
	{
		return a.type == b.type && a.index == b.index && a.serial == b.serial
			&& a.container == b.container && a.containerSerial == b.containerSerial;
	}

	// 0 usable, 1 dead / destroyed, 2 unconscious, 3 kidnapped / carried
	static int charProblem(Character* c)
	{
		if (c == NULL) return 1;
		if (c->isDead() || c->isDestroyed()) return 1;
		if (c->isUnconcious()) return 2;
		if (c->isKidnapped() || c->isBeingCarried()) return 3;
		return 0;
	}

	static bool inUpdateList(Character* c)
	{
		return ou->getCharacterUpdateList().find(c) != ou->getCharacterUpdateList().end();
	}

	static void msg(StrId id)
	{
		Notify(Tr(id));
	}

	static void msgName(StrId id, const char* name)
	{
		char buf[512];
		_snprintf(buf, sizeof(buf) - 1, Tr(id), name);
		buf[sizeof(buf) - 1] = '\0';
		Notify(buf);
	}

	static void msgInt(StrId id, int v)
	{
		char buf[512];
		_snprintf(buf, sizeof(buf) - 1, Tr(id), v);
		buf[sizeof(buf) - 1] = '\0';
		Notify(buf);
	}

	static void msgSold(StrId id, const char* name, int units, int money)
	{
		char buf[512];
		_snprintf(buf, sizeof(buf) - 1, Tr(id), name, units, money);
		buf[sizeof(buf) - 1] = '\0';
		Notify(buf);
	}

	// copies a game-provided name into a fixed buffer (cut to 63 bytes)
	static void copyName(char* dst, const std::string& src)
	{
		size_t n = src.size() < 63 ? src.size() : 63;
		for (size_t i = 0; i < n; ++i)
			dst[i] = src[i];
		dst[n] = '\0';
	}

	// ---- approach points (trader inside a building) ----------------------------------------------

	// A trader standing inside a building cannot be targeted directly (the animal does not enter buildings,
	// logs of stage 7: PATH_FAILED / stopped 577 units away). Candidates lie on a ring around the trader.
	static float approachRadius()
	{
		const Config& cfg = GetConfig();
		return cfg.approachRadius > 0.0f ? cfg.approachRadius : cfg.tradeRange * 0.9f;
	}

	// Fills o.candX/candZ with ring points, nearest to the animal first (insertion sort on plain arrays).
	// NO pathExists() here (see decisions.md, run 6/7).
	static void buildCandidates(Character* c, Character* t)
	{
		OrderRec& o = order();
		const Config& cfg = GetConfig();
		o.candCount = 0;
		o.candCur = -1;
		for (int i = 0; i < kMaxCands; ++i)
			o.candTried[i] = 0;
		const float r = approachRadius();
		int n = cfg.approachCandidates;
		if (n > kMaxCands) n = kMaxCands;
		if (n < 1) n = 1;
		Ogre::Vector3 tp = t->getPosition();
		Ogre::Vector3 ap = c->getPosition();
		o.candY = tp.y;
		LogInfo("approach: ring around the trader (inside a building or far away), radius=%.1f candidates=%d around (%.1f, %.1f), animal at (%.1f, %.1f)",
			r, n, tp.x, tp.z, ap.x, ap.z);
		float lx[kMaxCands];
		float lz[kMaxCands];
		float ld[kMaxCands];
		int cnt = 0;
		for (int i = 0; i < n; ++i)
		{
			float ang = 6.2831853f * (float)i / (float)n;
			float px = tp.x + r * cosf(ang);
			float pz = tp.z + r * sinf(ang);
			float dx = px - ap.x;
			float dz = pz - ap.z;
			float d = sqrtf(dx * dx + dz * dz);
			LogInfo("approach: candidate #%d (%.1f, %.1f) dAnimal=%.1f", i, px, pz, d);
			int k = cnt;
			while (k > 0 && ld[k - 1] > d)
			{
				ld[k] = ld[k - 1];
				lx[k] = lx[k - 1];
				lz[k] = lz[k - 1];
				--k;
			}
			ld[k] = d;
			lx[k] = px;
			lz[k] = pz;
			++cnt;
		}
		for (int i = 0; i < cnt; ++i)
		{
			o.candX[i] = lx[i];
			o.candZ[i] = lz[i];
			o.candTried[i] = 0;
		}
		o.candCount = cnt;
		LogInfo("approach: %d candidates built (sorted by distance to the animal, no path filtering)", cnt);
	}

	// Marks the current candidate as failed and selects the next one: nearest to the animal among those
	// not closer than 0.8 * radius to any failed point (neighbours of a failed point usually fail too),
	// else simply the next untried one. Sets o.p.dest. Returns false when nothing is left.
	static bool pickCandidate(const char* why, Character* c)
	{
		OrderRec& o = order();
		const float minSep = approachRadius() * 0.8f;
		if (o.candCur >= 0 && o.candCur < o.candCount)
			o.candTried[o.candCur] = 1;
		int pick = -1;
		for (int i = 0; i < o.candCount && pick < 0; ++i)
		{
			if (o.candTried[i])
				continue;
			bool tooClose = false;
			for (int j = 0; j < o.candCount; ++j)
			{
				if (!o.candTried[j])
					continue;
				float dx = o.candX[i] - o.candX[j];
				float dz = o.candZ[i] - o.candZ[j];
				if (sqrtf(dx * dx + dz * dz) < minSep)
					tooClose = true;
			}
			if (!tooClose)
				pick = i;
		}
		if (pick < 0)
			for (int i = 0; i < o.candCount && pick < 0; ++i)
				if (!o.candTried[i])
					pick = i;
		if (pick < 0)
		{
			LogInfo("approach: no candidate left (%s)", why);
			return false;
		}
		o.candCur = pick;
		o.p.dest = Ogre::Vector3(o.candX[pick], o.candY, o.candZ[pick]);
		LogInfo("approach: selected candidate %d of %d at (%.1f, %.1f) (%s)", pick, o.candCount, o.p.dest.x, o.p.dest.z, why);
		if (GetConfig().approachCheckPath && c != NULL)
		{
			// optional diagnostic, at most one call per selection (= per tick); see decisions.md (crash in run 6)
			LogInfo("approach: checking candidate #%d", pick);
			bool ok = c->pathExists(o.p.dest);
			LogInfo("approach: candidate #%d pathExists=%d (diagnostic only, not used for the choice)", pick, ok ? 1 : 0);
		}
		return true;
	}

	// ---- transitions ---------------------------------------------------------------------

	// Sends the animal to 'dest' and resets the per-order counters (NOT the phase timers).
	static void issueMove(Character* c, const Ogre::Vector3& dest, const char* label)
	{
		OrderRec& o = order();
		o.p.dest = dest;
		o.updates = 0;
		o.missingOrder = 0;
		o.foreignCount = 0;
		o.pathFailedCount = 0;
		o.stuckSince = -1.0;
		o.havePrevTaskLoc = false;
		o.haveRefTaskLoc = false;
		o.lastRetargetGame = -1.0;
		o.lastStatusReal = GetTickCount();
		Ogre::Vector3 p = c->getPosition();
		o.lastStatusPos = p;
		LogInfo("order(%s): playerMoveOrderDefault dest=(%.1f, %.1f, %.1f) from=(%.1f, %.1f, %.1f) dist=%.1f canTakePlayerOrders=%d",
			label, dest.x, dest.y, dest.z, p.x, p.y, p.z, dist2D(p, dest), c->canTakePlayerOrdersAtThisTime() ? 1 : 0);
		c->playerMoveOrderDefault(NULL, NULL, dest);
	}

	// The ONLY place where the state changes. 'c' may be NULL (then no order is issued: only Idle is legal).
	static void transition(OrderState to, const char* code, Character* c)
	{
		OrderRec& o = order();
		OrderState from = (OrderState)o.p.state;
		LoadGateReset(g_gate);
		float gameElapsed = (float)(g_gameTime - o.stateGame0);
		float travelled = 0.0f;
		float distToDest = -1.0f;
		if (c != NULL)
		{
			Ogre::Vector3 pos = c->getPosition();
			travelled = dist2D(pos, o.stateStartPos);
			distToDest = dist2D(pos, o.p.dest);
		}
		LogInfo("state %s -> %s, reason=%s (in state: game=%.2fs real=%.0fms travelled=%.1f distToDest=%.1f retries=%d)",
			OrderStateName(from), OrderStateName(to), code, gameElapsed, g_realMs - o.stateReal0, travelled, distToDest, o.p.retries);

		if (to == OS_IDLE)
		{
			o.p.state = OS_IDLE;
			o.hasRecord = false;
			o.p.animal = hand();
			o.p.trader = hand();
			o.p.retries = 0;
			o.useApproach = false;
			o.candCount = 0;
			o.candCur = -1;
			return;
		}
		if (c == NULL)
		{
			// cannot issue an order without the animal: must not happen
			LogError("order: transition to %s without animal, forcing Idle", OrderStateName(to));
			o.p.state = OS_IDLE;
			o.hasRecord = false;
			return;
		}

		o.p.state = to;
		o.p.retries = 0;
		o.stateGame0 = g_gameTime;
		o.stateReal0 = g_realMs;
		o.stateStartPos = c->getPosition();
		o.traderUnloaded = 0;
		if (to == OS_GOING || to == OS_RETURNING)
		{
			// phase timeout = max(order_timeout_seconds, distance / speed estimate * factor), active real seconds
			const Config& pc = GetConfig();
			const float dd = dist2D(c->getPosition(), to == OS_GOING ? o.p.dest : o.p.start);
			float scaled = dd / pc.orderSpeedEstimate * pc.orderTimeoutFactor;
			o.phaseTimeoutSec = scaled > pc.orderTimeoutSeconds ? scaled : pc.orderTimeoutSeconds;
			LogInfo("phase %s: distance=%.0f -> timeout %.0f s (order_timeout_seconds=%.0f, speed estimate %.0f u/s, factor %.1f)",
				OrderStateName(to), dd, o.phaseTimeoutSec, pc.orderTimeoutSeconds, pc.orderSpeedEstimate, pc.orderTimeoutFactor);
		}
		if (to == OS_GOING)
			issueMove(c, o.p.dest, "going");
		else if (to == OS_RETURNING)
			issueMove(c, o.p.start, "returning");
		// OS_SELLING: nothing is issued, the sale runs on the next update
	}

	// Error exit. returnHome: Going / Selling -> Returning (the animal is alive and controllable),
	// otherwise -> Idle. The player gets 'text'.
	static void fail(const char* code, Character* c, StrId text, bool returnHome)
	{
		OrderRec& o = order();
		LogInfo("ERROR EXIT code=%s state=%s returnHome=%d", code, OrderStateName((OrderState)o.p.state), returnHome ? 1 : 0);
		if (returnHome && c != NULL && (o.p.state == OS_GOING || o.p.state == OS_SELLING))
			transition(OS_RETURNING, code, c);
		else
			transition(OS_IDLE, code, c);
		msg(text);
	}

	// The animal came close enough (movement ended near the goal / path failed near / stuck near).
	static void endNear(const char* code, Character* c)
	{
		OrderRec& o = order();
		if (o.p.state == OS_GOING)
			transition(OS_SELLING, code, c);
		else
		{
			transition(OS_IDLE, code, c);
			msg(S_MSG_BACK);
		}
	}

	// The movement stopped far from the goal: re-send the order a limited number of times, else fail.
	// t = the trader (Going) or NULL.
	static void endFar(const char* code, Character* c, Character* t, bool allowRetry)
	{
		OrderRec& o = order();
		const Config& cfg = GetConfig();
		if (allowRetry && o.p.retries < cfg.retryOrders && o.p.state == OS_GOING && o.useApproach)
		{
			// the approach point did not work: next candidate (never the same point again)
			o.p.retries++;
			LogInfo("retry %d/%d: reason=%s, changing the approach point", o.p.retries, cfg.retryOrders, code);
			if (!pickCandidate(code, c))
			{
				fail("NO_APPROACH", c, S_ERR_NO_APPROACH, true);
				return;
			}
			issueMove(c, o.p.dest, "retry-approach");
			return;
		}
		if (allowRetry && o.p.retries < cfg.retryOrders)
		{
			o.p.retries++;
			LogInfo("retry %d/%d: reason=%s state=%s", o.p.retries, cfg.retryOrders, code, OrderStateName((OrderState)o.p.state));
			Ogre::Vector3 dest = o.p.dest;
			if (o.p.state == OS_GOING && t != NULL)
				dest = t->getPosition();
			issueMove(c, dest, "retry");
			return;
		}
		if (o.p.state == OS_GOING)
			fail(code, c, S_ERR_PATH_GO, true);
		else
			fail(code, c, S_ERR_PATH_BACK, false);
	}

	// ---- the sale ----------------------------------------------------------------------------

	static void doSell(Character* c)
	{
		OrderRec& o = order();
		const Config& cfg = GetConfig();
		SellOutcome oc;
		hand ah = o.p.animal;
		hand th = o.p.trader;
		LogInfo("selling: trader '%s' hand=%s animal '%s' sell_dry_run=%d order_max_items=%d",
			o.traderName, th.toString().c_str(), o.animalName, cfg.sellDryRun ? 1 : 0, cfg.orderMaxItems);
		SellRunFor(false, &ah, &th, &oc, cfg.orderMaxItems, false);

		const char* code = "SOLD";
		int kind = 0;   // 0 sold, 1 sold dry, 2 nothing, 3 failed, 4 trader gone
		if (!oc.ran || !oc.planned)
		{
			code = "SELL_NO_PLAN";
			kind = oc.error.find("gone") != std::string::npos ? 4 : 3;
		}
		else if (oc.dry)
		{
			if (oc.planUnits > 0) { code = "SOLD_DRY"; kind = 1; }
			else { code = "NOTHING_TO_SELL"; kind = 2; }
		}
		else if (oc.soldUnits > 0)
		{
			code = oc.aborted ? "SOLD_PARTIAL_ABORTED" : "SOLD";
			kind = 0;
		}
		else if (oc.aborted)
		{
			code = "SELL_ABORTED";
			kind = 3;
		}
		else
		{
			code = "NOTHING_SOLD";
			kind = 2;
		}
		LogInfo("selling result: code=%s ran=%d planned=%d dry=%d aborted=%d planUnits=%d planRevenue=%d soldUnits=%d soldMoney=%d error='%s'",
			code, oc.ran ? 1 : 0, oc.planned ? 1 : 0, oc.dry ? 1 : 0, oc.aborted ? 1 : 0, oc.planUnits, oc.planRevenue,
			oc.soldUnits, oc.soldMoney, oc.error.c_str());

		transition(OS_RETURNING, code, c);
		const char* tn = oc.traderName.empty() ? o.traderName : oc.traderName.c_str();
		switch (kind)
		{
		case 0: msgSold(S_MSG_SOLD, tn, oc.soldUnits, oc.soldMoney); break;
		case 1: msgSold(S_MSG_SOLD_DRY, tn, oc.planUnits, oc.planRevenue); break;
		case 2: msgName(S_MSG_NOBUY, tn); break;
		case 4: msg(S_ERR_TRADER); break;
		default: msg(S_MSG_SELLFAIL); break;
		}
	}

	// ---- per-update logic ------------------------------------------------------------------------

	static void update()
	{
		const Config& cfg = GetConfig();
		OrderRec& o = order();
		if (o.p.state == OS_IDLE)
			return;

		if (ou == NULL || ou->player == NULL)
		{
			transition(OS_IDLE, "WORLD_NOT_READY", NULL);
			return;
		}

		// 1. the animal, resolved by hand every update
		if (!o.hasRecord)
		{
			transition(OS_IDLE, "NO_RECORD", NULL);
			return;
		}
		Character* c = o.p.animal.getCharacter();
		if (c == NULL)
		{
			LogInfo("animal handle %s does not resolve (isValid=%d)", o.p.animal.toString().c_str(), o.p.animal.isValid() ? 1 : 0);
			fail("ANIMAL_UNRESOLVED", NULL, S_ERR_ANIMAL, false);
			return;
		}
		if (!sameHand(c->getHandle(), o.p.animal))
		{
			LogInfo("handle mismatch: stored=%s object=%s", o.p.animal.toString().c_str(), c->getHandle().toString().c_str());
			fail("ANIMAL_HANDLE_MISMATCH", NULL, S_ERR_ANIMAL, false);
			return;
		}
		int ap = charProblem(c);
		if (ap != 0)
		{
			fail(ap == 1 ? "ANIMAL_DEAD" : (ap == 2 ? "ANIMAL_UNCONSCIOUS" : "ANIMAL_UNAVAILABLE"), NULL, S_ERR_ANIMAL, false);
			return;
		}
		bool inList = inUpdateList(c);
		if (!inList && !o.unlistedLogged)
		{
			o.unlistedLogged = true;
			LogInfo("note: animal resolves by handle but is NOT in the character update list (offscreen/unloaded?)");
		}

		// 2. the sale (one update after entering Selling)
		if (o.p.state == OS_SELLING)
		{
			doSell(c);
			return;
		}

		const bool going = (o.p.state == OS_GOING);
		o.updates++;
		Ogre::Vector3 pos = c->getPosition();
		float d = dist2D(pos, o.p.dest);

		// 3. the trader (Going only)
		Character* t = NULL;
		float dTrader = -1.0f;
		if (going)
		{
			const char* tcode = NULL;
			t = o.p.trader.getCharacter();
			if (t == NULL)
				tcode = "TRADER_UNRESOLVED";
			else if (!sameHand(t->getHandle(), o.p.trader))
				tcode = "TRADER_HANDLE_MISMATCH";
			else
			{
				int tp = charProblem(t);
				if (tp != 0)
					tcode = tp == 1 ? "TRADER_DEAD" : (tp == 2 ? "TRADER_UNCONSCIOUS" : "TRADER_UNAVAILABLE");
				else if (!inUpdateList(t))
				{
					if (++o.traderUnloaded >= kConfirmChecks)
						tcode = "TRADER_NOT_LOADED";
				}
				else
					o.traderUnloaded = 0;
			}
			if (tcode != NULL)
			{
				LogInfo("trader '%s' hand=%s lost: %s", o.traderName, o.p.trader.toString().c_str(), tcode);
				fail(tcode, c, S_ERR_TRADER, true);
				return;
			}
			dTrader = dist2D(pos, t->getPosition());
		}
		// the distance that matters for the phase: to the trader (Going) or to the start point (Returning)
		const float goalD = going ? dTrader : d;
		const float nearD = going ? cfg.tradeRange * kNearFactor : cfg.returnRange;

		CharMovement* mv = c->getMovement();
		bool mvPathFailed = false, mvStuck = false;
		if (mv != NULL)
		{
			mvPathFailed = mv->pathFailed();
			mvStuck = mv->isProbablyStuck();
		}
		OrdersReceiver* orr = c->getOrdersReciever();
		Tasker* task = orr ? orr->getFirstOrder() : NULL;
		int taskKey = -1;
		Ogre::Vector3 taskLoc(0, 0, 0);
		bool taskIsMove = false;
		if (task != NULL)
		{
			taskKey = (int)task->key();
			taskLoc = task->getLocation();
			taskIsMove = (task->key() == MOVE_CUS_ORDERED);
		}

		// periodic status line
		DWORD now = GetTickCount();
		if (now - o.lastStatusReal >= (DWORD)cfg.statusIntervalMs)
		{
			float moved = dist2D(pos, o.lastStatusPos);
			float secs = (now - o.lastStatusReal) / 1000.0f;
			LogDebug("status %s: pos=(%.1f, %.1f) dest=(%.1f, %.1f) dDest=%.1f dTrader=%.1f speed=%.2f u/s | pathFailed=%d stuck=%d | task=%s key=%d isMove=%d loc=(%.1f, %.1f) | "
				"retries=%d inList=%d game=%.1fs",
				OrderStateName((OrderState)o.p.state), pos.x, pos.z, o.p.dest.x, o.p.dest.z, d, dTrader, moved / secs,
				mvPathFailed ? 1 : 0, mvStuck ? 1 : 0, task ? "yes" : "none", taskKey, taskIsMove ? 1 : 0, taskLoc.x, taskLoc.z,
				o.p.retries, inList ? 1 : 0, (float)(g_gameTime - o.stateGame0));
			o.lastStatusReal = now;
			o.lastStatusPos = pos;
		}

		// 4. arrival
		if (going ? (dTrader <= cfg.tradeRange) : (d <= cfg.arrivalRadius))
		{
			LogInfo("arrival: %s d=%.1f (trade_range=%.0f arrival_radius=%.1f)", going ? "IN_RANGE" : "BACK", goalD, cfg.tradeRange, cfg.arrivalRadius);
			if (going)
				transition(OS_SELLING, "IN_RANGE", c);
			else
			{
				transition(OS_IDLE, "ARRIVED_BACK", c);
				msg(S_MSG_BACK);
			}
			return;
		}

		// 5. movement errors (after the grace period)
		if (o.updates > kGraceUpdates)
		{
			if (mvPathFailed) o.pathFailedCount++; else o.pathFailedCount = 0;
			if (o.pathFailedCount >= kConfirmChecks)
			{
				if (goalD <= nearD)
					endNear("PATH_FAILED_NEAR", c);
				else
					endFar("PATH_FAILED", c, t, going && o.useApproach);
				return;
			}

			if (mvStuck)
			{
				if (o.stuckSince < 0.0) o.stuckSince = g_gameTime;
				if (g_gameTime - o.stuckSince >= cfg.stuckSeconds)
				{
					if (goalD <= nearD)
						endNear("STUCK_NEAR", c);
					else
						endFar("STUCK", c, t, true);
					return;
				}
			}
			else
				o.stuckSince = -1.0;

			// the player re-targeted the animal: the move task location changed (the key stays the same)
			if (task != NULL && taskIsMove)
			{
				if (o.havePrevTaskLoc && dist2D(taskLoc, o.prevTaskLoc) > 0.5f)
					o.lastRetargetGame = g_gameTime;
				o.prevTaskLoc = taskLoc;
				o.havePrevTaskLoc = true;

				const float snapTol = going ? cfg.tradeRange : cfg.returnRange;
				bool foreign = false;
				if (!o.haveRefTaskLoc)
				{
					// first observation: the game may snap a point inside a building, so accept a
					// location within snapTol of our destination as the reference
					if (dist2D(taskLoc, o.p.dest) <= snapTol)
					{
						o.refTaskLoc = taskLoc;
						o.haveRefTaskLoc = true;
						o.foreignCount = 0;
						if (dist2D(taskLoc, o.p.dest) > 5.0f && !o.snapLogged)
						{
							o.snapLogged = true;
							LogInfo("note: move task location (%.1f, %.1f) differs from the requested dest (%.1f, %.1f) by %.1f (game snapped the point)",
								taskLoc.x, taskLoc.z, o.p.dest.x, o.p.dest.z, dist2D(taskLoc, o.p.dest));
						}
					}
					else
						foreign = true;
				}
				else if (dist2D(taskLoc, o.refTaskLoc) > cfg.arrivalRadius * kForeignTargetFactor)
					foreign = true;
				else
					o.foreignCount = 0;
				if (foreign)
				{
					o.foreignCount++;
					if (o.foreignCount >= 2)
					{
						LogInfo("overridden: move task loc=(%.1f, %.1f) differs from ours (dest=(%.1f, %.1f), ref=(%.1f, %.1f))",
							taskLoc.x, taskLoc.z, o.p.dest.x, o.p.dest.z, o.refTaskLoc.x, o.refTaskLoc.z);
						fail("OVERRIDDEN", c, S_ERR_OVERRIDDEN, false);
						return;
					}
				}
			}
			else
				o.foreignCount = 0;

			// our order is gone / replaced
			bool ours = (task != NULL && taskIsMove);
			if (!ours) o.missingOrder++; else o.missingOrder = 0;
			if (o.missingOrder >= kConfirmChecks)
			{
				if (goalD <= nearD)
				{
					LogInfo("order ended near the goal: d=%.1f <= %.1f", goalD, nearD);
					endNear("ORDER_ENDED_NEAR", c);
					return;
				}
				if (task != NULL)
				{
					LogInfo("overridden: first order key=%d (expected MOVE_CUS_ORDERED=%d)", taskKey, (int)MOVE_CUS_ORDERED);
					fail("OVERRIDDEN", c, S_ERR_OVERRIDDEN, false);
					return;
				}
				if (o.lastRetargetGame >= 0.0 && g_gameTime - o.lastRetargetGame <= kRetargetWindow)
				{
					LogInfo("overridden: move task location changed %.1f s before the task ended", (float)(g_gameTime - o.lastRetargetGame));
					fail("OVERRIDDEN", c, S_ERR_OVERRIDDEN, false);
					return;
				}
				endFar("ORDER_ENDED_FAR", c, t, true);
				return;
			}
		}

		// 6. timeouts (per phase; tick time, and active real time as a hard cap)
		const float gameElapsed = (float)(g_gameTime - o.stateGame0);
		const double realElapsed = g_realMs - o.stateReal0;
		const bool tg = gameElapsed >= o.phaseTimeoutSec;
		const bool tr = realElapsed >= (double)o.phaseTimeoutSec * kRealCapFactor * 1000.0;
		if (tg || tr)
		{
			if (going)
				fail(tg ? "TIMEOUT_GAME" : "TIMEOUT_REAL", c, S_ERR_TIMEOUT_GO, true);
			else
				fail(tg ? "TIMEOUT_GAME" : "TIMEOUT_REAL", c, S_ERR_TIMEOUT_BACK, false);
			return;
		}
	}

	static void updateFn(void*)
	{
		update();
	}

	// ---- stage 8: persistence (the GameData part is in Persist.cpp) -------------------------------------

	static const DWORD kRestoreWaitMs = 60000;   // how long a loaded order waits for its animal / trader to appear

	namespace
	{
	struct OrderPendingRec
	{
		bool valid;
		DWORD firstTryReal;      // GetTickCount of the first restore attempt (0 = none yet)
		DWORD lastTryReal;
		OrderSnapshot s;
	};
	}
	// zero-initialised static POD: no constructor runs at DLL load
	static OrderPendingRec g_pending;

	bool OrderTakeSnapshot(OrderSnapshot* s)
	{
		if (s == NULL || g_order == NULL || g_order->p.state == OS_IDLE || !g_order->hasRecord)
			return false;
		const OrderRec& o = *g_order;
		memset(s, 0, sizeof(*s));
		s->version = kOrderFormatVersion;
		s->state = o.p.state;
		s->retries = o.p.retries;
		s->phaseSeconds = (float)(g_gameTime - o.stateGame0);
		s->start[0] = o.p.start.x; s->start[1] = o.p.start.y; s->start[2] = o.p.start.z;
		s->dest[0] = o.p.dest.x; s->dest[1] = o.p.dest.y; s->dest[2] = o.p.dest.z;
		strncpy(s->animal, o.animalStr, sizeof(s->animal) - 1);
		strncpy(s->trader, o.traderStr, sizeof(s->trader) - 1);
		return s->animal[0] != 0;
	}

	void OrderSetPending(const OrderSnapshot& s)
	{
		g_pending.s = s;
		g_pending.valid = true;
		g_pending.firstTryReal = 0;
		g_pending.lastTryReal = 0;
		LogInfo("restore: pending order registered: state=%d animal=%s trader=%s retries=%d phase=%.1fs start=(%.1f, %.1f) dest=(%.1f, %.1f)",
			s.state, s.animal, s.trader, s.retries, s.phaseSeconds, s.start[0], s.start[2], s.dest[0], s.dest[2]);
	}

	void OrderClearPending()
	{
		if (g_pending.valid)
			LogInfo("restore: pending order cleared (no saved order in the loaded game)");
		g_pending.valid = false;
	}

	void OrderPersistStatus()
	{
		OrderSnapshot s;
		if (OrderTakeSnapshot(&s))
			LogInfo("save status: WOULD SAVE version=%d state=%s retries=%d phase=%.1fs animal=%s trader=%s start=(%.1f, %.1f, %.1f) dest=(%.1f, %.1f, %.1f)",
				s.version, OrderStateName((OrderState)s.state), s.retries, s.phaseSeconds, s.animal, s.trader,
				s.start[0], s.start[1], s.start[2], s.dest[0], s.dest[1], s.dest[2]);
		else
			LogInfo("save status: nothing to save (order state is %s)", OrderStateName(OrderGetState()));
		if (g_pending.valid)
			LogInfo("save status: PENDING restore: state=%d animal=%s trader=%s waited=%ums", g_pending.s.state, g_pending.s.animal,
				g_pending.s.trader, g_pending.firstTryReal ? (unsigned)(GetTickCount() - g_pending.firstTryReal) : 0u);
		else
			LogInfo("save status: no pending restore");
	}

	static void restoreGiveUp(const char* code)
	{
		LogInfo("restore: order dropped, reason=%s (saved state=%d animal=%s trader=%s)", code, g_pending.s.state, g_pending.s.animal, g_pending.s.trader);
		g_pending.valid = false;
		msg(S_MSG_LOAD_LOST);
	}

	// Tries to bring the saved order back. Returns with g_pending.valid still true when the objects are not there
	// yet (the world is still populating); gives up after kRestoreWaitMs. Runs under SEH from OrderTick.
	static void restoreFn(void*)
	{
		OrderRec& o = order();
		DWORD now = GetTickCount();
		if (g_pending.firstTryReal == 0)
			g_pending.firstTryReal = now;
		const bool expired = (now - g_pending.firstTryReal) >= kRestoreWaitMs;
		const OrderSnapshot& s = g_pending.s;

		if (s.state != OS_GOING && s.state != OS_SELLING && s.state != OS_RETURNING)
		{
			restoreGiveUp("BAD_STATE");
			return;
		}
		if (ou == NULL || ou->player == NULL)
		{
			if (expired)
				restoreGiveUp("NO_PLAYER");
			return;
		}
		// Selling is rolled back to Returning: the sale runs in one frame, so a save made in Selling happened BEFORE
		// the sale (the sale itself and its state change are in the same update); going back to Returning can never sell twice.
		const OrderState target = (s.state == OS_SELLING) ? OS_RETURNING : (OrderState)s.state;

		hand ah;
		ah.fromString(std::string(s.animal));
		Character* c = ah.getCharacter();
		bool aOk = c != NULL && sameHand(c->getHandle(), ah) && c->isAnimal() != NULL && charProblem(c) == 0
			&& c->isWithThePlayer() && inUpdateList(c);
		if (!aOk)
		{
			LogDebug("restore: animal %s not ready yet (resolved=%d)", s.animal, c ? 1 : 0);
			if (expired)
				restoreGiveUp("ANIMAL_NOT_FOUND");
			return;
		}
		hand th;
		Character* t = NULL;
		if (target == OS_GOING)
		{
			th.fromString(std::string(s.trader));
			t = th.getCharacter();
			bool tOk = t != NULL && t != c && sameHand(t->getHandle(), th) && t->isAnimal() == NULL && charProblem(t) == 0 && inUpdateList(t);
			if (!tOk)
			{
				LogDebug("restore: trader %s not ready yet (resolved=%d)", s.trader, t ? 1 : 0);
				if (expired)
					restoreGiveUp("TRADER_NOT_FOUND");
				return;
			}
		}

		if (o.p.state != OS_IDLE)
			transition(OS_IDLE, "REPLACED_BY_LOAD", NULL);
		o.hasRecord = true;
		o.p.animal = ah;
		o.p.trader = th;
		copyName(o.animalStr, std::string(s.animal));
		copyName(o.traderStr, std::string(s.trader));
		{
			std::string tmp = c->getName();
			copyName(o.animalName, tmp);
		}
		o.traderName[0] = 0;
		if (t != NULL)
		{
			std::string tmp = t->getName();
			copyName(o.traderName, tmp);
		}
		o.p.start = Ogre::Vector3(s.start[0], s.start[1], s.start[2]);
		o.p.dest = Ogre::Vector3(s.dest[0], s.dest[1], s.dest[2]);
		o.useApproach = false;
		o.candCount = 0;
		o.candCur = -1;
		o.unlistedLogged = false;
		o.snapLogged = false;
		if (target == OS_GOING && GetConfig().approachEnabled
			&& (!t->isInsideBuilding.isNull() || dist2D(o.p.dest, t->getPosition()) > 5.0f))
		{
			// rebuild the ring and continue with the candidate the saved destination belongs to
			const Ogre::Vector3 savedDest = o.p.dest;
			buildCandidates(c, t);
			int best = -1;
			float bd = 1.0e30f;
			for (int i = 0; i < o.candCount; ++i)
			{
				float dx = o.candX[i] - savedDest.x;
				float dz = o.candZ[i] - savedDest.z;
				float d = sqrtf(dx * dx + dz * dz);
				if (d < bd)
				{
					bd = d;
					best = i;
				}
			}
			if (best >= 0 && bd <= 5.0f)
			{
				o.candCur = best;
				o.useApproach = true;
			}
			o.p.dest = savedDest;
			LogInfo("restore: approach ring rebuilt, saved destination matches candidate %d (distance %.1f)", best, bd);
		}
		const int savedRetries = s.retries;
		float phase = s.phaseSeconds;
		LogInfo("restore: restoring order: saved state=%d -> %s, animal '%s' %s, trader '%s' %s, retries=%d phase=%.1fs",
			s.state, OrderStateName(target), o.animalName, s.animal, o.traderName, s.trader, savedRetries, phase);
		g_pending.valid = false;
		transition(target, "LOADED_FROM_SAVE", c);
		if (phase < 0.0f || phase > o.phaseTimeoutSec)
			phase = 0.0f;
		o.p.retries = savedRetries < 0 ? 0 : (savedRetries > GetConfig().retryOrders ? GetConfig().retryOrders : savedRetries);
		o.stateGame0 = g_gameTime - (double)phase;
		msg(S_MSG_RESTORED);
	}

	// After a short loading flag: are the animal (and the trader while Going) still there by hand? If yes the move order is
	// sent again (the game may have cleared the task) and the cycle goes on.
	namespace
	{
	struct ResumeCtx
	{
		bool ready;
	};
	}

	static void resumeFn(void* vp)
	{
		ResumeCtx* x = (ResumeCtx*)vp;
		x->ready = false;
		OrderRec& o = order();
		Character* c = o.p.animal.getCharacter();
		if (c == NULL || !sameHand(c->getHandle(), o.p.animal))
			return;
		if (o.p.state == OS_GOING)
		{
			Character* t = o.p.trader.getCharacter();
			if (t == NULL || !sameHand(t->getHandle(), o.p.trader))
				return;
		}
		x->ready = true;
		if (charProblem(c) == 0)
		{
			if (o.p.state == OS_GOING)
				issueMove(c, o.p.dest, "resume");
			else if (o.p.state == OS_RETURNING)
				issueMove(c, o.p.start, "resume");
		}
	}

	void OrderTick(float time)
	{
		if (ou == NULL)
			return;

		const DWORD now = GetTickCount();
		const bool paused = ou->isPaused();
		const bool loading = ou->isLoadingFromASaveGame() || ou->gameResetting;
		// no time is counted while paused or while the loading flag is set
		if (g_lastTickReal != 0 && !paused && !loading)
		{
			DWORD dlt = now - g_lastTickReal;
			if (dlt > kMaxTickDeltaMs)
				dlt = kMaxTickDeltaMs;
			g_realMs += (double)dlt;
		}
		g_lastTickReal = now;
		if (!paused && !loading)
			g_gameTime += time;

		// stage 8: a saved order waits here until the world is ready (not while loading)
		if (g_pending.valid && !loading && now - g_pending.lastTryReal >= 500)
		{
			g_pending.lastTryReal = now;
			if (!safeRun(restoreFn, NULL, "order restore"))
			{
				g_pending.valid = false;
				if (g_order != NULL && g_order->p.state != OS_IDLE)
					transition(OS_IDLE, "RESTORE_EXCEPTION", NULL);
				msg(S_MSG_LOAD_LOST);
			}
		}

		if (g_order == NULL || g_order->p.state == OS_IDLE)
			return;

		const LoadGateResult lg = LoadGateStep(g_gate, loading, "order", OrderStateName((OrderState)g_order->p.state));
		if (lg == LG_WAIT)
			return;
		if (lg == LG_DROP)
		{
			LogInfo("world loading/resetting (a save was loaded) while state=%s: dropping order", OrderStateName((OrderState)g_order->p.state));
			transition(OS_IDLE, "WORLD_LOADING", NULL);
			return;
		}
		if (lg == LG_RESUME)
		{
			ResumeCtx rc;
			rc.ready = false;
			if (!safeRun(resumeFn, &rc, "order resume"))
				rc.ready = false;
			if (!rc.ready)
			{
				if (LoadGateExpired(g_gate))
				{
					LogInfo("order: resume failed, the animal or the trader did not resolve within %.0f s: dropping order", GetConfig().persistResumeSeconds);
					transition(OS_IDLE, "WORLD_LOADING", NULL);
				}
				return;
			}
			LoadGateResumeDone(g_gate, "order");
		}

		if (now - g_lastUpdateReal < (DWORD)GetConfig().updateIntervalMs)
			return;
		g_lastUpdateReal = now;
		if (paused)
			return;
		if (!safeRun(updateFn, NULL, "order update"))
		{
			if (g_order->p.state != OS_IDLE)
				transition(OS_IDLE, "EXCEPTION", NULL);
			msg(S_MSG_START_FAIL);
		}
	}

	// ---- start ---------------------------------------------------------------------------------------

	namespace
	{
	struct StartCtx
	{
		const hand* animal;
		const hand* trader;
		OrderStartResult result;
		bool warnHostile;
		int tripSeconds;     // > 0: a far trip, estimated one-way seconds for the warning
		bool veryFar;
	};
	}

	static void startFn(void* vp)
	{
		StartCtx* x = (StartCtx*)vp;
		const Config& cfg = GetConfig();
		OrderRec& o = order();
		x->result = OSR_OK;

		if (ou == NULL || ou->player == NULL)
		{
			LogInfo("order start refused: reason=NO_PLAYER");
			x->result = OSR_WORLD;
			return;
		}
		if (ou->isLoadingFromASaveGame() || ou->gameResetting)
		{
			LogInfo("order start refused: reason=WORLD_LOADING");
			x->result = OSR_WORLD;
			return;
		}
		if (ZoneProbeActive())
		{
			LogInfo("order start refused: reason=BUSY (the gotown probe is walking)");
			x->result = OSR_BUSY;
			return;
		}
		if (g_pending.valid)
		{
			LogInfo("order start refused: reason=BUSY (a saved order is being restored after loading)");
			x->result = OSR_BUSY;
			return;
		}
		if (o.p.state != OS_IDLE || MoveFsmState() != MS_IDLE)
		{
			LogInfo("order start refused: reason=BUSY state=%s moveFsm=%s", OrderStateName((OrderState)o.p.state), MoveStateName(MoveFsmState()));
			x->result = OSR_BUSY;
			return;
		}

		Character* c = x->animal->getCharacter();
		if (c == NULL || c->isAnimal() == NULL || charProblem(c) != 0 || !c->isWithThePlayer())
		{
			LogInfo("order start refused: reason=ANIMAL (resolved=%d animal=%d problem=%d)", c ? 1 : 0,
				(c && c->isAnimal()) ? 1 : 0, charProblem(c));
			x->result = OSR_ANIMAL;
			return;
		}
		Character* t = x->trader->getCharacter();
		if (t == NULL || t == c || t->isAnimal() != NULL || charProblem(t) != 0)
		{
			LogInfo("order start refused: reason=TRADER (resolved=%d problem=%d)", t ? 1 : 0, charProblem(t));
			x->result = OSR_TRADER;
			return;
		}

		{
			const float tripD = dist2D(c->getPosition(), t->getPosition());
			if (tripD > cfg.maxTripDistance)
			{
				LogInfo("order start refused: reason=TOO_FAR (distance %.0f > max_trip_distance=%.0f)", tripD, cfg.maxTripDistance);
				x->result = OSR_TOO_FAR;
				return;
			}
		}
		{
			bool hostile = TraderIsHostile(t);
			const int pol = cfg.hostilePolicy;
			LogInfo("order start: hostile=%d hostile_policy=%s", hostile ? 1 : 0, pol == 0 ? "block" : (pol == 1 ? "warn" : "allow"));
			if (hostile && pol == 0)
			{
				LogInfo("order start refused: reason=HOSTILE (the trader's faction or town is an enemy of the player; hostile_policy=block)");
				x->result = OSR_HOSTILE;
				return;
			}
			if (hostile && pol == 1)
				x->warnHostile = true;
		}
		o.hasRecord = true;
		{
			hand ha = c->getHandle();
			o.p.animal = ha;
		}
		{
			hand hb = t->getHandle();
			o.p.trader = hb;
		}
		{
			std::string tmp = c->getName();
			copyName(o.animalName, tmp);
		}
		{
			std::string tmp = t->getName();
			copyName(o.traderName, tmp);
		}
		{
			std::string ta = o.p.animal.toString();
			copyName(o.animalStr, ta);
			std::string tt = o.p.trader.toString();
			copyName(o.traderStr, tt);
		}
		o.p.start = c->getPosition();
		Ogre::Vector3 tpos = t->getPosition();
		o.p.dest = tpos;
		o.useApproach = false;
		o.candCount = 0;
		o.candCur = -1;
		o.unlistedLogged = false;
		o.snapLogged = false;
		o.stateGame0 = g_gameTime;
		o.stateReal0 = g_realMs;
		o.stateStartPos = o.p.start;
		float dT = dist2D(o.p.start, tpos);
		int pathOk = -1;   // diagnostic only; called only with approach_check_path=1 (see decisions.md, run 6)
		if (cfg.approachCheckPath)
			pathOk = c->pathExists(tpos) ? 1 : 0;
		LogInfo("order start: animal='%s' hand=%s trader='%s' hand=%s start=(%.1f, %.1f, %.1f) traderPos=(%.1f, %.1f, %.1f) dist=%.1f "
			"pathExists(trader)=%d inRangeAlready=%d | trade_range=%.0f return_range=%.0f retry_orders=%d order_timeout_seconds=%.0f "
			"sell_dry_run=%d order_max_items=%d",
			o.animalName, o.p.animal.toString().c_str(), o.traderName, o.p.trader.toString().c_str(),
			o.p.start.x, o.p.start.y, o.p.start.z, tpos.x, tpos.y, tpos.z, dT, pathOk,
			dT <= cfg.tradeRange ? 1 : 0, cfg.tradeRange, cfg.returnRange, cfg.retryOrders, cfg.orderTimeoutSeconds,
			cfg.sellDryRun ? 1 : 0, cfg.orderMaxItems);
		const bool indoors = !t->isInsideBuilding.isNull();
		const bool farTrip = dT > cfg.listFarDistance;
		LogInfo("order start: trader inside a building=%d, far trip=%d (distance %.0f, list_far_distance=%.0f), in range already=%d", indoors ? 1 : 0, farTrip ? 1 : 0, dT,
			cfg.listFarDistance, dT <= cfg.tradeRange ? 1 : 0);
		if (cfg.approachEnabled && (indoors || farTrip) && dT > cfg.tradeRange)
		{
			buildCandidates(c, t);
			if (o.candCount == 0 || !pickCandidate("first", c))
			{
				LogInfo("order start refused: reason=NO_APPROACH (no reachable approach point around the trader)");
				o.hasRecord = false;
				o.candCount = 0;
				x->result = OSR_NO_APPROACH;
				return;
			}
			o.useApproach = true;
		}
		else if ((indoors || farTrip) && dT > cfg.tradeRange)
			LogInfo("order start: approach_enabled=0, going to the trader position");
		transition(OS_GOING, "START_COMMAND", c);
		if (farTrip)
		{
			x->tripSeconds = (int)(dT / cfg.orderSpeedEstimate + 0.5f);
			x->veryFar = dT > cfg.listVeryFarDistance;
		}
	}

	OrderStartResult OrderStart(const hand& animal, const hand& trader)
	{
		StartCtx x;
		x.animal = &animal;
		x.trader = &trader;
		x.result = OSR_EXCEPTION;
		x.warnHostile = false;
		x.tripSeconds = 0;
		x.veryFar = false;
		if (!safeRun(startFn, &x, "order start"))
		{
			x.result = OSR_EXCEPTION;
			if (g_order != NULL && g_order->p.state != OS_IDLE)
				transition(OS_IDLE, "START_EXCEPTION", NULL);
		}
		switch (x.result)
		{
		case OSR_OK:
			if (x.warnHostile)
				msg(S_MSG_HOSTILE_WARN);
			msgName(S_MSG_STARTED, order().traderName);
			if (x.tripSeconds > 0)
				msgInt(x.veryFar ? S_MSG_VERY_LONG_TRIP : S_MSG_LONG_TRIP, x.tripSeconds);
			break;
		case OSR_BUSY: msg(S_MSG_BUSY); break;
		case OSR_HOSTILE: msg(S_ERR_HOSTILE); break;
		case OSR_TOO_FAR: msg(S_ERR_TOO_FAR); break;
		case OSR_NO_APPROACH: msg(S_ERR_NO_APPROACH); break;
		case OSR_WORLD: msg(S_MSG_FAIL); break;
		case OSR_ANIMAL:
		case OSR_TRADER: msg(S_MSG_GONE); break;
		default: msg(S_MSG_START_FAIL); break;
		}
		return x.result;
	}

	// ---- command "order" -----------------------------------------------------------------------------

	static void dump()
	{
		OrderRec& o = order();
		LogInfo("order status: state=%s hasRecord=%d animal='%s' %s trader='%s' %s start=(%.1f, %.1f, %.1f) dest=(%.1f, %.1f, %.1f) retries=%d "
			"phaseGame=%.1fs updates=%u tickTime=%.1f activeRealMs=%.0f",
			OrderStateName((OrderState)o.p.state), o.hasRecord ? 1 : 0, o.animalName, o.p.animal.toString().c_str(),
			o.traderName, o.p.trader.toString().c_str(), o.p.start.x, o.p.start.y, o.p.start.z,
			o.p.dest.x, o.p.dest.y, o.p.dest.z, o.p.retries, (float)(g_gameTime - o.stateGame0), o.updates, g_gameTime, g_realMs);
	}

	void OrderCommand(const std::string& arg)
	{
		if (arg == "status")
		{
			dump();
			return;
		}
		const char* src = "";
		Character* animal = MoveFsmPickAnimal(&src);
		if (animal == NULL)
		{
			LogInfo("order command: no usable animal (select the pack animal)");
			msg(S_MSG_GONE);
			return;
		}
		hand ah = animal->getHandle();
		LogInfo("order command: animal '%s' hand=%s source=%s arg='%s'", animal->getName().c_str(), ah.toString().c_str(), src, arg.c_str());

		hand th;
		if (arg.find('-') != std::string::npos)
		{
			th.fromString(arg);
			LogInfo("order command: trader by hand %s", arg.c_str());
		}
		else
		{
			int idx = arg.empty() ? 1 : atoi(arg.c_str());
			TraderList L;
			if (!BuildTraderList(L, -1, &ah))
			{
				LogInfo("order command: trader list failed: %s", L.error.c_str());
				msg(S_MSG_FAIL);
				return;
			}
			if (idx < 1 || idx > (int)L.shown.size())
			{
				LogInfo("order command: index %d is out of range (the list has %u rows)", idx, (unsigned)L.shown.size());
				msg(S_MSG_GONE);
				return;
			}
			const TraderEntry& e = L.shown[idx - 1];
			th = e.h;
			LogInfo("order command: row %d '%s' town='%s' dist=%.0f money=%d hostile=%d sellable=%d hand=%s", idx, e.name.c_str(),
				e.town.c_str(), e.dist, e.money, e.hostile ? 1 : 0, e.sellable, e.handStr.c_str());
		}
		OrderStart(ah, th);
	}
}
