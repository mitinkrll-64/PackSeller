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

// 0.2 step 1: the "other towns" PROBE (debug commands only). See ZoneProbe.h.
//
// Rules used here (same lessons as Order.cpp): game pointers live only inside one call, the animal is
// re-resolved by hand on every update, every block that reads game objects runs under SEH, types are in an
// anonymous namespace, no pathExists() for arbitrary points, no new hooks. The walk state is separate from the
// sale cycle (OrderRec): the cycle stays Idle and refuses to start while the probe walks.

#include "ZoneProbe.h"
#include "Config.h"
#include "LoadGate.h"
#include "Log.h"
#include "MoveFsm.h"
#include "Order.h"
#include "Probe.h"
#include "Seh.h"

#include <windows.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
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
#include <kenshi/CharMovement.h>
#include <kenshi/Platoon.h>
#include <kenshi/Town.h>
#include <kenshi/GameData.h>
#include <kenshi/Tasker.h>
#include <kenshi/Enums.h>
#include <kenshi/AI/AITaskSystem.h>
#include <kenshi/util/hand.h>
#include <ogre/OgreVector3.h>

namespace PS
{
	static const int kRadiusCount = 5;
	static const float kRadii[kRadiusCount] = { 500.0f, 1000.0f, 2000.0f, 3000.0f, 5000.0f };
	static const int kMaxTownLines = 60;
	static const unsigned kGraceUpdates = 10;
	static const int kConfirmChecks = 3;
	static const DWORD kMaxTickDeltaMs = 2000;
	static const DWORD kStatusIntervalMs = 5000;
	static const double kRetargetWindow = 30.0;
	static const int kRingSteps = 8;
	// ring offsets (degrees) tried in turn when a destination does not work: the point on the animal's side, then around
	static const float kRingOffsetDeg[kRingSteps] = { 0.0f, 45.0f, -45.0f, 90.0f, -90.0f, 135.0f, -135.0f, 180.0f };

	// ---- SEH wrapper (no C++ objects with destructors in this function) -------------------------

	typedef void (*ZoneFn)(void*);

	static bool zSafeRun(ZoneFn fn, void* ctx, const char* what)
	{
		__try
		{
			fn(ctx);
			return true;
		}
		__except (PsSehFilterCode(GetExceptionCode()))
		{
			LogError("zone: EXCEPTION 0x%08X in %s", (unsigned)GetExceptionCode(), what);
			return false;
		}
	}

	static float zDist2D(const Ogre::Vector3& a, const Ogre::Vector3& b)
	{
		float dx = a.x - b.x;
		float dz = a.z - b.z;
		return sqrtf(dx * dx + dz * dz);
	}

	static bool zSameHand(const hand& a, const hand& b)
	{
		return a.type == b.type && a.index == b.index && a.serial == b.serial
			&& a.container == b.container && a.containerSerial == b.containerSerial;
	}

	template<class M>
	static int zFindBool(const M& m, const std::string& key)
	{
		typename M::const_iterator it = m.find(key);
		if (it == m.end())
			return -1;
		return it->second ? 1 : 0;
	}

	// same trader test as TraderList.cpp: template flag "is trader" = 1, or no flag and isATrader()
	static bool zIsTrader(Character* c)
	{
		int fs = (c->data != NULL) ? zFindBool(c->data->bdata, std::string("is trader")) : -2;
		if (fs == 1)
			return true;
		if (fs == 0)
			return false;
		return c->isATrader();
	}

	static TownBase* zHomeTown(Character* c)
	{
		ActivePlatoon* ap = c->getPlatoon();
		Platoon* p = ap != NULL ? ap->me : NULL;
		Ownerships* own = p != NULL ? p->getOwnerships() : NULL;
		TownBase* home = own != NULL ? own->_homeTown : NULL;
		return home != NULL ? home : c->isResident();
	}

	static int zCharProblem(Character* c)
	{
		if (c == NULL) return 1;
		if (c->isDead() || c->isDestroyed()) return 1;
		if (c->isUnconcious()) return 2;
		if (c->isKidnapped() || c->isBeingCarried()) return 3;
		return 0;
	}

	// ---- zone scan --------------------------------------------------------------------------------

	namespace
	{
	struct ZoneScan
	{
		Ogre::Vector3 aPos;
		bool haveLeader;
		Ogre::Vector3 lPos;
		bool withTowns;
		int total;
		int aCnt[kRadiusCount];
		int aNpc[kRadiusCount];
		float aMax;
		int lCnt[kRadiusCount];
		float lMax;
		int trCnt[kRadiusCount];
		int traders;
		float trMax;
		std::map<TownBase*, int> byTown;
		ZoneScan()
			: aPos(0, 0, 0), haveLeader(false), lPos(0, 0, 0), withTowns(false), total(0), aMax(0.0f), lMax(0.0f), traders(0), trMax(0.0f)
		{
			for (int i = 0; i < kRadiusCount; ++i)
				aCnt[i] = aNpc[i] = lCnt[i] = trCnt[i] = 0;
		}
	};

	struct ZoneChar
	{
		ZoneScan* s;
		Character* c;
	};
	}

	static void scanOneFn(void* vp)
	{
		ZoneChar* x = (ZoneChar*)vp;
		ZoneScan& s = *x->s;
		Character* c = x->c;
		if (c->isDead() || c->isDestroyed())
			return;
		Ogre::Vector3 p = c->getPosition();
		++s.total;
		const float da = zDist2D(p, s.aPos);
		const bool withPlayer = c->isWithThePlayer();
		for (int i = 0; i < kRadiusCount; ++i)
			if (da <= kRadii[i])
			{
				++s.aCnt[i];
				if (!withPlayer)
					++s.aNpc[i];
			}
		if (da > s.aMax)
			s.aMax = da;
		if (s.haveLeader)
		{
			const float dl = zDist2D(p, s.lPos);
			for (int i = 0; i < kRadiusCount; ++i)
				if (dl <= kRadii[i])
					++s.lCnt[i];
			if (dl > s.lMax)
				s.lMax = dl;
		}
		if (c->isAnimal() == NULL && zIsTrader(c))
		{
			++s.traders;
			for (int i = 0; i < kRadiusCount; ++i)
				if (da <= kRadii[i])
					++s.trCnt[i];
			if (da > s.trMax)
				s.trMax = da;
			if (s.withTowns)
			{
				TownBase* home = zHomeTown(c);
				s.byTown[home] += 1;
			}
		}
	}

	static void scanAll(ZoneScan& s)
	{
		const ogre_unordered_set<Character*>::type& list = ou->getCharacterUpdateList();
		for (ogre_unordered_set<Character*>::type::const_iterator it = list.begin(); it != list.end(); ++it)
		{
			if (*it == NULL)
				continue;
			ZoneChar x;
			x.s = &s;
			x.c = *it;
			zSafeRun(scanOneFn, &x, "zone scan character");
		}
	}

	// first living non-animal player character (the squad leader / a fighter), or NULL
	static Character* findLeader()
	{
		if (ou == NULL || ou->player == NULL)
			return NULL;
		const lektor<Character*>& all = ou->player->getAllPlayerCharacters();
		for (uint32_t i = 0; i < all.size(); ++i)
		{
			Character* c = all[i];
			if (c != NULL && c->isAnimal() == NULL && zCharProblem(c) == 0)
				return c;
		}
		return NULL;
	}

	// ---- towns (same selection and order as the "towns" command) -------------------------------------

	namespace
	{
	struct ZoneTown
	{
		TownBase* t;
		float d;
	};
	}

	static bool zoneTownLess(const ZoneTown& a, const ZoneTown& b)
	{
		return a.d < b.d;
	}

	namespace
	{
	struct ZoneTownCtx
	{
		TownBase* t;
		const Ogre::Vector3* ref;
		bool ok;
		float d;
	};
	}

	static void townDistFn(void* vp)
	{
		ZoneTownCtx* x = (ZoneTownCtx*)vp;
		x->ok = false;
		x->d = 0.0f;
		if (x->t->isTown() == NULL)
			return;
		x->d = zDist2D(x->t->getPosition(), *x->ref);
		x->ok = true;
	}

	// real towns within probe_radius of 'ref', sorted by distance (index in the sorted list = row of "towns")
	static void collectTowns(const Ogre::Vector3& ref, std::vector<ZoneTown>& out)
	{
		out.clear();
		if (shou == NULL || shou->townList == NULL)
			return;
		lektor<RootObject*>& things = shou->townList->things;
		for (uint32_t i = 0; i < things.size(); ++i)
		{
			RootObject* ro = things[i];
			if (ro == NULL)
				continue;
			ZoneTownCtx x;
			x.t = static_cast<TownBase*>(ro);
			x.ref = &ref;
			x.ok = false;
			x.d = 0.0f;
			if (!zSafeRun(townDistFn, &x, "zone town distance") || !x.ok)
				continue;
			if (x.d > GetConfig().probeRadius)
				continue;
			ZoneTown e;
			e.t = x.t;
			e.d = x.d;
			out.push_back(e);
		}
		std::sort(out.begin(), out.end(), zoneTownLess);
	}

	namespace
	{
	struct ZoneTownInfo
	{
		TownBase* t;
		char name[96];
		float x, y, z, radius;
		bool ok;
	};
	}

	static void townInfoFn(void* vp)
	{
		ZoneTownInfo* x = (ZoneTownInfo*)vp;
		x->ok = false;
		Ogre::Vector3 p = x->t->getPosition();
		x->x = p.x;
		x->y = p.y;
		x->z = p.z;
		x->radius = x->t->getRadius();
		std::string nm = x->t->getKnownName();
		size_t n = nm.size() < sizeof(x->name) - 1 ? nm.size() : sizeof(x->name) - 1;
		for (size_t i = 0; i < n; ++i)
			x->name[i] = nm[i];
		x->name[n] = 0;
		x->ok = true;
	}

	static bool getTownInfo(TownBase* t, ZoneTownInfo* out)
	{
		memset(out, 0, sizeof(*out));
		out->t = t;
		return zSafeRun(townInfoFn, out, "zone town info") && out->ok;
	}

	// nearest real town to 'p' (name copied into a buffer)
	static bool nearestTown(const Ogre::Vector3& p, char* name, size_t n, float* dOut)
	{
		std::vector<ZoneTown> v;
		float best = 1.0e30f;
		TownBase* bestT = NULL;
		if (shou == NULL || shou->townList == NULL)
			return false;
		lektor<RootObject*>& things = shou->townList->things;
		for (uint32_t i = 0; i < things.size(); ++i)
		{
			if (things[i] == NULL)
				continue;
			ZoneTownCtx x;
			x.t = static_cast<TownBase*>(things[i]);
			x.ref = &p;
			x.ok = false;
			x.d = 0.0f;
			if (zSafeRun(townDistFn, &x, "zone nearest town") && x.ok && x.d < best)
			{
				best = x.d;
				bestT = x.t;
			}
		}
		if (bestT == NULL)
			return false;
		ZoneTownInfo info;
		if (!getTownInfo(bestT, &info))
			return false;
		_snprintf(name, n - 1, "%s", info.name);
		name[n - 1] = 0;
		*dOut = best;
		return true;
	}

	// ---- command "zone" ---------------------------------------------------------------------------------

	static void runZone(Character* animal, const char* why)
	{
		if (animal == NULL || ou == NULL || ou->player == NULL || shou == NULL)
		{
			LogInfo("zone: refused (no animal or world not ready)");
			return;
		}
		ZoneScan s;
		s.aPos = animal->getPosition();
		s.withTowns = true;
		Character* leader = findLeader();
		char leaderName[96] = "none";
		float leaderDist = -1.0f;
		if (leader != NULL)
		{
			s.haveLeader = true;
			s.lPos = leader->getPosition();
			std::string ln = leader->getName();
			_snprintf(leaderName, sizeof(leaderName) - 1, "%s", ln.c_str());
			leaderDist = zDist2D(s.aPos, s.lPos);
		}
		std::string an = animal->getName();
		LogInfo("zone: ---- begin (%s) ----", why);
		LogInfo("zone: animal '%s' at (%.0f, %.0f); leader '%s'%s (%.0f, %.0f); distance animal-leader=%.0f", an.c_str(), s.aPos.x, s.aPos.z,
			leaderName, leader ? "" : " (none)", s.lPos.x, s.lPos.z, leaderDist);
		scanAll(s);
		LogInfo("zone: update list: %u entries, %d alive characters read", (unsigned)ou->getCharacterUpdateList().size(), s.total);
		for (int i = 0; i < kRadiusCount; ++i)
			LogInfo("zone: ANIMAL radius %.0f: characters=%d (not with the player=%d), traders=%d", kRadii[i], s.aCnt[i], s.aNpc[i], s.trCnt[i]);
		LogInfo("zone: ANIMAL farthest loaded character: %.0f units; farthest loaded trader: %.0f units (traders loaded in total: %d)", s.aMax, s.trMax, s.traders);
		if (s.haveLeader)
		{
			for (int i = 0; i < kRadiusCount; ++i)
				LogInfo("zone: LEADER radius %.0f: characters=%d", kRadii[i], s.lCnt[i]);
			LogInfo("zone: LEADER farthest loaded character: %.0f units", s.lMax);
		}

		std::vector<ZoneTown> towns;
		collectTowns(s.aPos, towns);
		int printed = 0, withTraders = 0;
		for (size_t i = 0; i < towns.size(); ++i)
		{
			int loaded = 0;
			std::map<TownBase*, int>::const_iterator f = s.byTown.find(towns[i].t);
			if (f != s.byTown.end())
				loaded = f->second;
			if (loaded > 0)
				++withTraders;
			if (printed >= kMaxTownLines || (towns[i].d > 12000.0f && loaded == 0))
				continue;
			ZoneTownInfo info;
			if (!getTownInfo(towns[i].t, &info))
				continue;
			++printed;
			LogInfo("zone: TOWN #%u '%s' dist=%.0f radius=%.0f center=(%.0f, %.0f) loaded traders with this home town=%d",
				(unsigned)i + 1, info.name, towns[i].d, info.radius, info.x, info.z, loaded);
		}
		int noHome = 0;
		std::map<TownBase*, int>::const_iterator nf = s.byTown.find((TownBase*)NULL);
		if (nf != s.byTown.end())
			noHome = nf->second;
		LogInfo("zone: SUMMARY: characters loaded=%d, within 2000 of the animal=%d, 5000=%d, farthest=%.0f | traders loaded=%d (no home town: %d), "
			"within 2000=%d, farthest=%.0f | towns listed=%u (with loaded home traders: %d, lines shown: %d)",
			s.total, s.aCnt[2], s.aCnt[4], s.aMax, s.traders, noHome, s.trCnt[2], s.trMax, (unsigned)towns.size(), withTraders, printed);
	}

	void ZoneProbeZone()
	{
		const char* src = "";
		Character* animal = MoveFsmPickAnimal(&src);
		if (animal == NULL)
		{
			LogInfo("zone: no usable animal (select the pack animal)");
			return;
		}
		runZone(animal, src);
	}

	// ---- the walk to a town -----------------------------------------------------------------------------

	namespace
	{
	enum ZoneState
	{
		ZS_IDLE = 0,
		ZS_GOING,
		ZS_RETURNING
	};

	struct ZoneRec
	{
		int state;
		hand animal;
		Ogre::Vector3 start;
		Ogre::Vector3 dest;
		Ogre::Vector3 townCenter;
		float townRadius;
		float ringR;
		float baseAngle;       // radians, direction from the town centre to the animal at the start
		int ringIdx;
		int retries;
		char townName[96];
		double stateGame0;
		double stateReal0;
		Ogre::Vector3 stateStartPos;
		unsigned updates;
		int foreignCount;
		int missingOrder;
		int pathFailedCount;
		double stuckSince;
		bool havePrevLoc;
		Ogre::Vector3 prevLoc;
		bool haveRefLoc;
		Ogre::Vector3 refLoc;
		double lastRetargetGame;
		DWORD lastStatusReal;
		Ogre::Vector3 lastStatusPos;
		ZoneRec()
			: state(ZS_IDLE), start(0, 0, 0), dest(0, 0, 0), townCenter(0, 0, 0), townRadius(0.0f), ringR(0.0f), baseAngle(0.0f)
			, ringIdx(0), retries(0), stateGame0(0), stateReal0(0), stateStartPos(0, 0, 0), updates(0), foreignCount(0)
			, missingOrder(0), pathFailedCount(0), stuckSince(-1.0), havePrevLoc(false), prevLoc(0, 0, 0), haveRefLoc(false)
			, refLoc(0, 0, 0), lastRetargetGame(-1.0), lastStatusReal(0), lastStatusPos(0, 0, 0)
		{
			townName[0] = 0;
		}
	};
	}

	static ZoneRec* g_zone = NULL;
	static double g_zGameTime = 0.0;
	static double g_zRealMs = 0.0;
	static DWORD g_zLastTickReal = 0;
	static DWORD g_zLastUpdateReal = 0;
	static LoadGate g_zGate;    // zero-initialised POD (short loading flag tolerance)

	static ZoneRec& zrec()
	{
		if (!g_zone)
			g_zone = new ZoneRec();
		return *g_zone;
	}

	static const char* zoneStateName(int s)
	{
		return s == ZS_IDLE ? "Idle" : (s == ZS_GOING ? "Going" : "Returning");
	}

	bool ZoneProbeActive()
	{
		return g_zone != NULL && g_zone->state != ZS_IDLE;
	}

	// ring point 'idx' around the town centre
	static Ogre::Vector3 ringPoint(const ZoneRec& r, int idx)
	{
		float a = r.baseAngle + kRingOffsetDeg[idx % kRingSteps] * 3.14159265f / 180.0f;
		return Ogre::Vector3(r.townCenter.x + r.ringR * cosf(a), r.townCenter.y, r.townCenter.z + r.ringR * sinf(a));
	}

	static void zIssueMove(Character* c, const Ogre::Vector3& dest, const char* label)
	{
		ZoneRec& r = zrec();
		r.dest = dest;
		r.updates = 0;
		r.missingOrder = 0;
		r.foreignCount = 0;
		r.pathFailedCount = 0;
		r.stuckSince = -1.0;
		r.havePrevLoc = false;
		r.haveRefLoc = false;
		r.lastRetargetGame = -1.0;
		r.lastStatusReal = GetTickCount();
		Ogre::Vector3 p = c->getPosition();
		r.lastStatusPos = p;
		LogInfo("gotown: order(%s) dest=(%.1f, %.1f) from=(%.1f, %.1f) dist=%.1f canTakePlayerOrders=%d",
			label, dest.x, dest.z, p.x, p.z, zDist2D(p, dest), c->canTakePlayerOrdersAtThisTime() ? 1 : 0);
		c->playerMoveOrderDefault(NULL, NULL, dest);
	}

	// The ONLY place where the probe state changes. c may be NULL only for Idle.
	static void ztransition(int to, const char* code, Character* c)
	{
		ZoneRec& r = zrec();
		int from = r.state;
		LoadGateReset(g_zGate);
		float travelled = 0.0f, distToDest = -1.0f;
		if (c != NULL)
		{
			Ogre::Vector3 p = c->getPosition();
			travelled = zDist2D(p, r.stateStartPos);
			distToDest = zDist2D(p, r.dest);
		}
		LogInfo("gotown: state %s -> %s, reason=%s (in state: real=%.0fms travelled=%.0f distToDest=%.0f retries=%d)",
			zoneStateName(from), zoneStateName(to), code, g_zRealMs - r.stateReal0, travelled, distToDest, r.retries);
		if (to == ZS_IDLE || c == NULL)
		{
			if (to != ZS_IDLE)
				LogError("gotown: transition to %s without animal, forcing Idle", zoneStateName(to));
			r.state = ZS_IDLE;
			r.animal = hand();
			r.retries = 0;
			return;
		}
		r.state = to;
		r.retries = 0;
		r.stateGame0 = g_zGameTime;
		r.stateReal0 = g_zRealMs;
		r.stateStartPos = c->getPosition();
		if (to == ZS_GOING)
			zIssueMove(c, r.dest, "going");
		else
			zIssueMove(c, r.start, "returning");
	}

	// The animal arrived (or stopped as close as it gets): run the probes here, then go home or stop.
	static void onArrived(Character* c, const char* code)
	{
		ZoneRec& r = zrec();
		Ogre::Vector3 p = c->getPosition();
		LogInfo("gotown: ARRIVED (%s) at (%.0f, %.0f): %.0f units from the town centre '%s', %.0f from the start", code, p.x, p.z,
			zDist2D(p, r.townCenter), r.townName, zDist2D(p, r.start));
		runZone(c, "gotown arrival");
		ProbeTraders();
		Notify("Probe: arrived, zone and traders written to the log.");
		if (GetConfig().probeReturnHome)
			ztransition(ZS_RETURNING, code, c);
		else
			ztransition(ZS_IDLE, code, c);
	}

	static void zexit(const char* code, Character* c, const char* text)
	{
		LogInfo("gotown: EXIT code=%s state=%s", code, zoneStateName(zrec().state));
		ztransition(ZS_IDLE, code, c);
		if (text)
			Notify(text);
	}

	// movement stopped far from the goal: re-send (Going: the next ring point) a limited number of times
	static void zstopped(const char* code, Character* c)
	{
		ZoneRec& r = zrec();
		const Config& cfg = GetConfig();
		if (r.retries < cfg.retryOrders)
		{
			r.retries++;
			Ogre::Vector3 d = r.dest;
			if (r.state == ZS_GOING)
			{
				r.ringIdx = (r.ringIdx + 1) % kRingSteps;
				d = ringPoint(r, r.ringIdx);
			}
			LogInfo("gotown: retry %d/%d: reason=%s, %s", r.retries, cfg.retryOrders, code, r.state == ZS_GOING ? "next ring point" : "same point");
			zIssueMove(c, d, "retry");
			return;
		}
		if (r.state == ZS_GOING)
		{
			// as close as it gets: probe here
			onArrived(c, code);
		}
		else
			zexit(code, c, "Probe: could not get back to the start point.");
	}

	static void zupdate()
	{
		const Config& cfg = GetConfig();
		ZoneRec& r = zrec();
		if (r.state == ZS_IDLE)
			return;
		if (ou == NULL || ou->player == NULL)
		{
			ztransition(ZS_IDLE, "WORLD_NOT_READY", NULL);
			return;
		}
		Character* c = r.animal.getCharacter();
		if (c == NULL || !zSameHand(c->getHandle(), r.animal))
		{
			zexit("ANIMAL_UNRESOLVED", NULL, "Probe: animal lost.");
			return;
		}
		int ap = zCharProblem(c);
		if (ap != 0)
		{
			zexit(ap == 1 ? "ANIMAL_DEAD" : (ap == 2 ? "ANIMAL_UNCONSCIOUS" : "ANIMAL_UNAVAILABLE"), NULL, "Probe: animal lost.");
			return;
		}
		const bool going = (r.state == ZS_GOING);
		r.updates++;
		Ogre::Vector3 pos = c->getPosition();
		float d = zDist2D(pos, r.dest);

		CharMovement* mv = c->getMovement();
		bool pathFailed = false, stuck = false;
		if (mv != NULL)
		{
			pathFailed = mv->pathFailed();
			stuck = mv->isProbablyStuck();
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

		// short status line every 5 real seconds
		DWORD now = GetTickCount();
		if (now - r.lastStatusReal >= kStatusIntervalMs)
		{
			float secs = (now - r.lastStatusReal) / 1000.0f;
			float speed = zDist2D(pos, r.lastStatusPos) / secs;
			ZoneScan s;
			s.aPos = pos;
			scanAll(s);
			char nt[96] = "?";
			float ntd = -1.0f;
			nearestTown(pos, nt, sizeof(nt), &ntd);
			LogInfo("gotown: %s pos=(%.0f, %.0f) distToDest=%.0f speed=%.1f u/s traders loaded within 2000=%d (within 5000=%d, chars within 2000=%d) nearest town '%s' %.0f | "
				"pathFailed=%d stuck=%d task=%s retries=%d", zoneStateName(r.state), pos.x, pos.z, d, speed, s.trCnt[2], s.trCnt[4], s.aCnt[2], nt, ntd,
				pathFailed ? 1 : 0, stuck ? 1 : 0, task ? (taskIsMove ? "move" : "other") : "none", r.retries);
			r.lastStatusReal = now;
			r.lastStatusPos = pos;
		}

		// arrival
		const float arriveR = cfg.arrivalRadius * 3.0f;
		if (d <= arriveR)
		{
			if (going)
				onArrived(c, "ARRIVED");
			else
			{
				ztransition(ZS_IDLE, "ARRIVED_BACK", c);
				Notify("Probe: the animal is back at the start point.");
			}
			return;
		}

		const float nearD = going ? cfg.tradeRange : cfg.returnRange;
		if (r.updates > kGraceUpdates)
		{
			if (pathFailed) r.pathFailedCount++; else r.pathFailedCount = 0;
			if (r.pathFailedCount >= kConfirmChecks)
			{
				if (d <= nearD) { if (going) onArrived(c, "PATH_FAILED_NEAR"); else { ztransition(ZS_IDLE, "PATH_FAILED_NEAR", c); } }
				else zstopped("PATH_FAILED", c);
				return;
			}
			if (stuck)
			{
				if (r.stuckSince < 0.0) r.stuckSince = g_zGameTime;
				if (g_zGameTime - r.stuckSince >= cfg.stuckSeconds)
				{
					if (d <= nearD) { if (going) onArrived(c, "STUCK_NEAR"); else { ztransition(ZS_IDLE, "STUCK_NEAR", c); } }
					else zstopped("STUCK", c);
					return;
				}
			}
			else
				r.stuckSince = -1.0;

			// the player gave the animal another move order: the location of the move task moves away from ours
			if (task != NULL && taskIsMove)
			{
				if (r.havePrevLoc && zDist2D(taskLoc, r.prevLoc) > 0.5f)
					r.lastRetargetGame = g_zGameTime;
				r.prevLoc = taskLoc;
				r.havePrevLoc = true;
				bool foreign = false;
				if (!r.haveRefLoc)
				{
					if (zDist2D(taskLoc, r.dest) <= 300.0f)
					{
						r.refLoc = taskLoc;
						r.haveRefLoc = true;
						r.foreignCount = 0;
					}
					else
						foreign = true;
				}
				else if (zDist2D(taskLoc, r.refLoc) > cfg.arrivalRadius * 2.0f)
					foreign = true;
				else
					r.foreignCount = 0;
				if (foreign && ++r.foreignCount >= 2)
				{
					LogInfo("gotown: overridden: move task loc=(%.0f, %.0f), ours (%.0f, %.0f)", taskLoc.x, taskLoc.z, r.dest.x, r.dest.z);
					zexit("OVERRIDDEN", c, "Probe cancelled: the animal got another order.");
					return;
				}
			}
			else
				r.foreignCount = 0;

			bool ours = (task != NULL && taskIsMove);
			if (!ours) r.missingOrder++; else r.missingOrder = 0;
			if (r.missingOrder >= kConfirmChecks)
			{
				if (d <= nearD)
				{
					if (going) onArrived(c, "ORDER_ENDED_NEAR");
					else { ztransition(ZS_IDLE, "ORDER_ENDED_NEAR", c); Notify("Probe: the animal is back at the start point."); }
					return;
				}
				if (task != NULL)
				{
					LogInfo("gotown: overridden: first order key=%d (expected MOVE_CUS_ORDERED=%d)", taskKey, (int)MOVE_CUS_ORDERED);
					zexit("OVERRIDDEN", c, "Probe cancelled: the animal got another order.");
					return;
				}
				if (r.lastRetargetGame >= 0.0 && g_zGameTime - r.lastRetargetGame <= kRetargetWindow)
				{
					zexit("OVERRIDDEN", c, "Probe cancelled: the animal got another order.");
					return;
				}
				zstopped("ORDER_ENDED_FAR", c);
				return;
			}
		}

		// timeout of the phase, in ACTIVE (unpaused) real time
		if (g_zRealMs - r.stateReal0 >= (double)cfg.probeTimeoutSeconds * 1000.0)
		{
			if (going)
			{
				LogInfo("gotown: timeout %.0f s while going, probing where the animal is", cfg.probeTimeoutSeconds);
				onArrived(c, "TIMEOUT");
			}
			else
				zexit("TIMEOUT_RETURN", c, "Probe: return timed out, the animal stays where it is.");
			return;
		}
	}

	static void zupdateFn(void*)
	{
		zupdate();
	}

	void ZoneProbeTick(float time)
	{
		if (ou == NULL)
			return;
		DWORD now = GetTickCount();
		bool paused = ou->isPaused();
		const bool loading = ou->isLoadingFromASaveGame() || ou->gameResetting;
		// no time is counted while paused or while the loading flag is set
		if (g_zLastTickReal != 0 && !paused && !loading)
		{
			DWORD dlt = now - g_zLastTickReal;
			if (dlt > kMaxTickDeltaMs)
				dlt = kMaxTickDeltaMs;
			g_zRealMs += (double)dlt;
		}
		g_zLastTickReal = now;
		if (!paused && !loading)
			g_zGameTime += time;

		if (g_zone == NULL || g_zone->state == ZS_IDLE)
			return;
		const LoadGateResult lg = LoadGateStep(g_zGate, loading, "gotown", zoneStateName(g_zone->state));
		if (lg == LG_WAIT)
			return;
		if (lg == LG_DROP)
		{
			LogInfo("gotown: world loading/resetting (a save was loaded) while state=%s: dropping the probe", zoneStateName(g_zone->state));
			ztransition(ZS_IDLE, "WORLD_LOADING", NULL);
			return;
		}
		if (lg == LG_RESUME)
		{
			Character* rc = g_zone->animal.getCharacter();
			if (rc == NULL || !zSameHand(rc->getHandle(), g_zone->animal))
			{
				if (LoadGateExpired(g_zGate))
				{
					LogInfo("gotown: resume failed, the animal did not resolve within %.0f s: dropping the probe", GetConfig().persistResumeSeconds);
					ztransition(ZS_IDLE, "WORLD_LOADING", NULL);
				}
				return;
			}
			LoadGateResumeDone(g_zGate, "gotown");
			if (zCharProblem(rc) == 0)
				zIssueMove(rc, g_zone->state == ZS_GOING ? g_zone->dest : g_zone->start, "resume");
		}
		if (now - g_zLastUpdateReal < (DWORD)GetConfig().updateIntervalMs)
			return;
		g_zLastUpdateReal = now;
		if (paused)
			return;
		if (!zSafeRun(zupdateFn, NULL, "gotown update"))
		{
			if (g_zone->state != ZS_IDLE)
				ztransition(ZS_IDLE, "EXCEPTION", NULL);
		}
	}

	// ---- command "gotown" -------------------------------------------------------------------------------

	namespace
	{
	struct ZoneStartCtx
	{
		std::string arg;
	};
	}

	static bool allDigits(const std::string& s)
	{
		if (s.empty())
			return false;
		for (size_t i = 0; i < s.size(); ++i)
			if (s[i] < '0' || s[i] > '9')
				return false;
		return true;
	}

	static std::string asciiLower(const std::string& s)
	{
		std::string r = s;
		for (size_t i = 0; i < r.size(); ++i)
			if (r[i] >= 'A' && r[i] <= 'Z')
				r[i] = (char)(r[i] - 'A' + 'a');
		return r;
	}

	static void gotownFn(void* vp)
	{
		ZoneStartCtx* x = (ZoneStartCtx*)vp;
		const Config& cfg = GetConfig();
		ZoneRec& r = zrec();

		if (ou == NULL || ou->player == NULL || shou == NULL || ou->isLoadingFromASaveGame() || ou->gameResetting)
		{
			LogInfo("gotown refused: world not ready");
			return;
		}
		if (r.state != ZS_IDLE || OrderActive() || MoveFsmState() != MS_IDLE)
		{
			LogInfo("gotown refused: BUSY (probe state=%s, order active=%d, go active=%d)", zoneStateName(r.state), OrderActive() ? 1 : 0,
				MoveFsmState() != MS_IDLE ? 1 : 0);
			return;
		}
		const char* src = "";
		Character* c = MoveFsmPickAnimal(&src);
		if (c == NULL)
		{
			LogInfo("gotown refused: no usable animal (select the pack animal)");
			return;
		}
		Ogre::Vector3 apos = c->getPosition();
		std::vector<ZoneTown> towns;
		collectTowns(apos, towns);
		if (towns.empty())
		{
			LogInfo("gotown refused: no towns within probe_radius=%.0f", cfg.probeRadius);
			return;
		}
		int pick = -1;
		if (x->arg.empty())
		{
			LogInfo("gotown: usage: gotown <row of 'towns' | part of the town name>; rows are sorted by distance from the selected animal");
			return;
		}
		if (allDigits(x->arg))
		{
			int idx = atoi(x->arg.c_str());
			if (idx >= 1 && idx <= (int)towns.size())
				pick = idx - 1;
			else
			{
				LogInfo("gotown refused: row %d is out of range (1..%u)", idx, (unsigned)towns.size());
				return;
			}
		}
		else
		{
			const std::string needle = asciiLower(x->arg);
			for (size_t i = 0; i < towns.size() && pick < 0; ++i)
			{
				ZoneTownInfo info;
				if (!getTownInfo(towns[i].t, &info))
					continue;
				if (asciiLower(std::string(info.name)).find(needle) != std::string::npos)
					pick = (int)i;
			}
			if (pick < 0)
			{
				LogInfo("gotown refused: no town name contains '%s'", x->arg.c_str());
				return;
			}
		}
		ZoneTownInfo ti;
		if (!getTownInfo(towns[pick].t, &ti))
		{
			LogInfo("gotown refused: the town cannot be read");
			return;
		}
		const float dist = zDist2D(apos, Ogre::Vector3(ti.x, ti.y, ti.z));
		LogInfo("gotown: target row %d '%s' center=(%.0f, %.0f) radius=%.0f distance from the animal=%.0f (probe_max_distance=%.0f)",
			pick + 1, ti.name, ti.x, ti.z, ti.radius, dist, cfg.probeMaxDistance);
		if (dist > cfg.probeMaxDistance)
		{
			LogInfo("gotown refused: too far (%.0f > probe_max_distance=%.0f)", dist, cfg.probeMaxDistance);
			return;
		}

		r.townCenter = Ogre::Vector3(ti.x, ti.y, ti.z);
		r.townRadius = ti.radius;
		float ringR = (ti.radius > 0.0f) ? ti.radius * 0.5f : 300.0f;
		if (ringR < 150.0f) ringR = 150.0f;
		if (ringR > 1500.0f) ringR = 1500.0f;
		r.ringR = ringR;
		float dx = apos.x - ti.x;
		float dz = apos.z - ti.z;
		r.baseAngle = (dx == 0.0f && dz == 0.0f) ? 0.0f : atan2f(dz, dx);
		r.ringIdx = 0;
		r.retries = 0;
		_snprintf(r.townName, sizeof(r.townName) - 1, "%s", ti.name);
		r.townName[sizeof(r.townName) - 1] = 0;
		r.animal = c->getHandle();
		r.start = apos;
		r.dest = ringPoint(r, 0);
		r.stateGame0 = g_zGameTime;
		r.stateReal0 = g_zRealMs;
		r.stateStartPos = apos;
		LogInfo("gotown: START animal '%s' (%s) from (%.0f, %.0f) to the ring point (%.0f, %.0f) on the animal's side of '%s' (ring radius %.0f); "
			"timeout %.0f s, retry_orders=%d, return home=%d. No pathExists is used.",
			c->getName().c_str(), src, apos.x, apos.z, r.dest.x, r.dest.z, r.townName, ringR, cfg.probeTimeoutSeconds, cfg.retryOrders,
			cfg.probeReturnHome ? 1 : 0);
		runZone(c, "gotown start");
		ztransition(ZS_GOING, "START_COMMAND", c);
		Notify("Probe: the animal goes to the town.");
	}

	void ZoneProbeGoTown(const std::string& arg)
	{
		ZoneStartCtx x;
		x.arg = arg;
		if (!zSafeRun(gotownFn, &x, "gotown start"))
		{
			if (g_zone != NULL && g_zone->state != ZS_IDLE)
				ztransition(ZS_IDLE, "START_EXCEPTION", NULL);
		}
	}
}
