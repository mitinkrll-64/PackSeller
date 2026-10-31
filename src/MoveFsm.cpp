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

#include "MoveFsm.h"
#include "Order.h"
#include "ZoneProbe.h"
#include "Config.h"
#include "LoadGate.h"
#include "Log.h"

#include <windows.h>
#include <math.h>
#include <string>

#include <kenshi/GameWorld.h>
#include <kenshi/Globals.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/Character.h>
#include <kenshi/CharacterAnimal.h>
#include <kenshi/CharMovement.h>
#include <kenshi/Tasker.h>
#include <kenshi/Enums.h>
#include <kenshi/AI/AITaskSystem.h>
#include <kenshi/util/hand.h>
#include <ogre/OgreVector3.h>

namespace PS
{
	// Number of FSM updates after issuing an order during which "order missing"
	// and "pathFailed" are ignored (the AI needs a moment to register the task).
	static const unsigned kGraceUpdates = 10;
	// Consecutive checks required to confirm "order missing" / "pathFailed".
	static const int kConfirmChecks = 3;
	// Radius multiplier used when the order has ended / movement flag is set.
	static const float kLooseRadiusFactor = 3.0f;
	// Real-time hard cap = timeout_seconds * this factor (protects against
	// a tick time scale that is not seconds; see api-notes).
	static const float kRealCapFactor = 5.0f;
	// A move task whose location differs from our destination by more than
	// arrival_radius * this factor is a foreign (player) order.
	static const float kForeignTargetFactor = 2.0f;
	// ORDER_ENDED_FAR is converted to OVERRIDDEN if the task location changed
	// within this many game seconds before the task disappeared.
	static const double kRetargetWindow = 30.0;

	// The single record of the current order (one animal at a time in v0.1,
	// but kept as an object so it can become a list later).
	namespace
	{
	struct MoveRec
	{
		MoveState state;
		bool hasAnimal;
		hand animal;           // identifier, never a raw pointer between ticks
		Ogre::Vector3 start;   // return point
		Ogre::Vector3 target;  // test point
		Ogre::Vector3 dest;    // current destination (target or start)
		double stateGame0;     // accumulated tick time at state entry
		DWORD stateReal0;      // GetTickCount at state entry
		unsigned updates;      // FSM updates since the order was issued
		int foreignCount;      // consecutive checks with a foreign move target
		int missingOrder;      // consecutive checks without our order
		int pathFailedCount;   // consecutive checks with pathFailed
		double stuckSince;     // accumulated tick time when stuck began, <0 = not stuck
		DWORD lastStatusReal;
		Ogre::Vector3 lastStatusPos;
		bool mismatchLogged;
		bool flagOnlyLogged;
		bool unlistedLogged;
		Ogre::Vector3 stateStartPos;
		Ogre::Vector3 prevTaskLoc;   // task location seen on the previous update
		bool havePrevTaskLoc;
		double lastRetargetGame;     // game time when the move task location changed, <0 = never

		MoveRec()
			: state(MS_IDLE), hasAnimal(false), start(0, 0, 0), target(0, 0, 0), dest(0, 0, 0)
			, stateGame0(0), stateReal0(0), updates(0), foreignCount(0), missingOrder(0), pathFailedCount(0)
			, stuckSince(-1.0), lastStatusReal(0), lastStatusPos(0, 0, 0)
			, mismatchLogged(false), flagOnlyLogged(false), unlistedLogged(false), stateStartPos(0, 0, 0)
			, prevTaskLoc(0, 0, 0), havePrevTaskLoc(false), lastRetargetGame(-1.0)
		{
		}
	};
	}

	// Allocated lazily on the first "go" (hand constructor is a KenshiLib call,
	// so no global constructor runs at DLL load).
	static MoveRec* g_order = NULL;

	// tick bookkeeping
	static double g_gameTime = 0.0;       // sum of 'time' args while not paused
	static unsigned g_tickCalls = 0;
	static DWORD g_firstTickReal = 0;
	static DWORD g_lastUpdateReal = 0;
	static bool g_wasPaused = false;
	static LoadGate g_gate;     // zero-initialised POD (short loading flag tolerance)

	const char* MoveStateName(MoveState s)
	{
		switch (s)
		{
		case MS_IDLE: return "Idle";
		case MS_GOING: return "Going";
		case MS_RETURNING: return "Returning";
		}
		return "?";
	}

	MoveState MoveFsmState()
	{
		return g_order ? g_order->state : MS_IDLE;
	}

	static float dist2D(const Ogre::Vector3& a, const Ogre::Vector3& b)
	{
		float dx = a.x - b.x;
		float dz = a.z - b.z;
		return sqrtf(dx * dx + dz * dz);
	}

	static MoveRec& order()
	{
		if (!g_order)
			g_order = new MoveRec();
		return *g_order;
	}

	static void logTickStats(const char* where)
	{
		DWORD real = GetTickCount() - g_firstTickReal;
		LogDebug("tick stats (%s): calls=%u sumTime=%.3f realMs=%u ratio(sumTime/realSec)=%.3f",
			where, g_tickCalls, g_gameTime, (unsigned)real,
			real > 0 ? (g_gameTime / (real / 1000.0)) : 0.0);
	}

	// ---- animal selection ------------------------------------------------

	static bool animalUsable(Character* c, const char** why)
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

	// Prefer a selected animal, else the first animal among player characters.
	static Character* pickAnimal(const char** source)
	{
		*source = "none";
		if (ou == NULL || ou->player == NULL)
			return NULL;

		typedef ogre_unordered_set<hand>::type HandSet;
		const HandSet& sel = ou->player->selectedCharacters;
		for (HandSet::const_iterator it = sel.begin(); it != sel.end(); ++it)
		{
			Character* c = it->getCharacter();
			const char* why = "";
			if (c != NULL && c->isAnimal() != NULL && animalUsable(c, &why))
			{
				*source = "selected";
				return c;
			}
		}

		const lektor<Character*>& all = ou->player->getAllPlayerCharacters();
		for (uint32_t i = 0; i < all.size(); ++i)
		{
			Character* c = all[i];
			const char* why = "";
			if (c != NULL && c->isAnimal() != NULL && animalUsable(c, &why))
			{
				*source = "first-in-squad";
				return c;
			}
		}
		return NULL;
	}

	Character* MoveFsmPickAnimal(const char** source)
	{
		return pickAnimal(source);
	}

	// ---- transitions -------------------------------------------------------

	static void issueMove(Character* c, const Ogre::Vector3& dest, const char* label)
	{
		MoveRec& o = order();
		o.dest = dest;
		o.updates = 0;
		o.missingOrder = 0;
		o.foreignCount = 0;
		o.pathFailedCount = 0;
		o.stuckSince = -1.0;
		o.mismatchLogged = false;
		o.flagOnlyLogged = false;
		o.havePrevTaskLoc = false;
		o.lastRetargetGame = -1.0;
		o.stateGame0 = g_gameTime;
		o.stateReal0 = GetTickCount();
		o.lastStatusReal = o.stateReal0;
		o.stateStartPos = c->getPosition();
		o.lastStatusPos = o.stateStartPos;
		LogInfo("order(%s): playerMoveOrderDefault dest=(%.1f, %.1f, %.1f) from=(%.1f, %.1f, %.1f) dist=%.1f",
			label, dest.x, dest.y, dest.z, o.stateStartPos.x, o.stateStartPos.y, o.stateStartPos.z,
			dist2D(o.stateStartPos, dest));
		c->playerMoveOrderDefault(NULL, NULL, dest);
	}

	// The only place where the state changes. 'c' may be NULL for Idle.
	static void transition(MoveState to, const char* code, Character* c)
	{
		MoveRec& o = order();
		MoveState from = o.state;
		LoadGateReset(g_gate);
		float gameElapsed = (float)(g_gameTime - o.stateGame0);
		DWORD realElapsed = GetTickCount() - o.stateReal0;
		float travelled = 0.0f;
		float distToDest = -1.0f;
		if (c != NULL)
		{
			Ogre::Vector3 p = c->getPosition();
			travelled = dist2D(p, o.stateStartPos);
			distToDest = dist2D(p, o.dest);
		}
		LogInfo("state %s -> %s, reason=%s (in state: game=%.2fs real=%ums travelled=%.1f distToDest=%.1f)",
			MoveStateName(from), MoveStateName(to), code, gameElapsed, (unsigned)realElapsed, travelled, distToDest);
		if (from != MS_IDLE && travelled > 0.0f && gameElapsed > 0.01f)
			LogDebug("measure: %.1f units in %.2f tick-seconds = %.2f units/tick-second; %.2f units/real-second",
				travelled, gameElapsed, travelled / gameElapsed,
				realElapsed > 0 ? travelled / (realElapsed / 1000.0f) : 0.0f);
		logTickStats(code);

		o.state = to;
		if (to == MS_IDLE)
		{
			o.hasAnimal = false;
			o.animal = hand();
			return;
		}
		if (c == NULL)
		{
			// cannot issue an order without the animal: must not happen
			LogError("transition to %s without animal, forcing Idle", MoveStateName(to));
			o.state = MS_IDLE;
			o.hasAnimal = false;
			return;
		}
		if (to == MS_GOING)
			issueMove(c, o.target, "going");
		else
			issueMove(c, o.start, "returning");
	}

	// Error exits. Going -> Returning (animal alive); Returning -> Idle with message.
	static void fail(const char* code, Character* c, const char* playerText)
	{
		MoveRec& o = order();
		if (o.state == MS_GOING && c != NULL)
			transition(MS_RETURNING, code, c);
		else
			transition(MS_IDLE, code, c);
		if (playerText)
			Notify(playerText);
	}

	// ---- commands ---------------------------------------------------------

	void MoveFsmStart()
	{
		Config& cfg = GetConfig();
		if (MoveFsmState() != MS_IDLE)
		{
			LogInfo("start ignored: reason=BUSY state=%s", MoveStateName(MoveFsmState()));
			Notify("Order already in progress.");
			return;
		}
		if (ZoneProbeActive())
		{
			LogInfo("start ignored: reason=BUSY (the gotown probe is walking)");
			return;
		}
		if (OrderActive())
		{
			LogInfo("start ignored: reason=BUSY (stage 7 order is active, state=%s)", OrderStateName(OrderGetState()));
			Notify("An order cycle is in progress.");
			return;
		}
		if (ou == NULL || ou->player == NULL)
		{
			LogInfo("start refused: reason=NO_PLAYER");
			return;
		}
		if (ou->isLoadingFromASaveGame() || ou->gameResetting)
		{
			LogInfo("start refused: reason=WORLD_LOADING");
			return;
		}

		const char* source = "";
		Character* c = pickAnimal(&source);
		if (c == NULL)
		{
			LogInfo("start refused: reason=NO_ANIMAL");
			Notify("No usable player animal found (select an animal).");
			return;
		}

		MoveRec& o = order();
		o.animal = c->getHandle();
		o.hasAnimal = true;
		o.start = c->getPosition();
		bool inList = ou->getCharacterUpdateList().find(c) != ou->getCharacterUpdateList().end();
		LogInfo("start: animal=%s handle=%s source=%s pos=(%.1f, %.1f, %.1f) canTakePlayerOrders=%d inUpdateList=%d",
			c->getName().c_str(), o.animal.toString().c_str(), source, o.start.x, o.start.y, o.start.z,
			c->canTakePlayerOrdersAtThisTime() ? 1 : 0, inList ? 1 : 0);

		// Choose the test point: first of +X,-X,+Z,-Z for which pathExists() is true.
		// If none passes we still try the first one (pathExists reliability for animals
		// is [TO VERIFY]); pathFailed/stuck/timeout will catch a bad point.
		static const float dirs[4][2] = { { 1.f, 0.f }, { -1.f, 0.f }, { 0.f, 1.f }, { 0.f, -1.f } };
		int chosen = -1;
		for (int i = 0; i < 4; ++i)
		{
			Ogre::Vector3 t(o.start.x + dirs[i][0] * cfg.testDistance, o.start.y, o.start.z + dirs[i][1] * cfg.testDistance);
			bool ok = c->pathExists(t);
			LogInfo("precheck dir#%d target=(%.1f, %.1f, %.1f) pathExists=%d", i, t.x, t.y, t.z, ok ? 1 : 0);
			if (ok && chosen < 0)
				chosen = i;
		}
		if (chosen < 0)
		{
			LogInfo("precheck: NO_PATH_PRECHECK for all directions, trying dir#0 anyway");
			chosen = 0;
		}
		o.target = Ogre::Vector3(o.start.x + dirs[chosen][0] * cfg.testDistance, o.start.y,
			o.start.z + dirs[chosen][1] * cfg.testDistance);
		o.dest = o.start;
		o.stateGame0 = g_gameTime;
		o.stateReal0 = GetTickCount();
		o.stateStartPos = o.start;
		o.unlistedLogged = false;
		// Idle -> Going through the single transition function
		transition(MS_GOING, "START_COMMAND", c);
		Notify("Animal sent to the test point.");
	}

	void MoveFsmDump()
	{
		LogInfo("dump: ou=%p player=%p state=%s paused=%d loading=%d gameResetting=%d tickCalls=%u sumTime=%.3f",
			(void*)ou, ou ? (void*)ou->player : NULL, MoveStateName(MoveFsmState()),
			(ou && ou->isPaused()) ? 1 : 0, (ou && ou->isLoadingFromASaveGame()) ? 1 : 0,
			(ou && ou->gameResetting) ? 1 : 0, g_tickCalls, g_gameTime);
		if (ou == NULL || ou->player == NULL)
			return;
		const lektor<Character*>& all = ou->player->getAllPlayerCharacters();
		LogInfo("dump: player characters=%u, selected=%u", (unsigned)all.size(),
			(unsigned)ou->player->selectedCharacters.size());
		for (uint32_t i = 0; i < all.size(); ++i)
		{
			Character* c = all[i];
			if (c == NULL) continue;
			Ogre::Vector3 p = c->getPosition();
			const char* why = "";
			animalUsable(c, &why);
			LogInfo("dump: #%u name=%s animal=%d usable=%s pos=(%.1f, %.1f, %.1f) handle=%s",
				(unsigned)i, c->getName().c_str(), c->isAnimal() ? 1 : 0, why, p.x, p.y, p.z,
				c->getHandle().toString().c_str());
		}
		const char* source = "";
		Character* pick = pickAnimal(&source);
		LogInfo("dump: pickAnimal -> %s (%s)", pick ? pick->getName().c_str() : "none", source);
	}

	// ---- per-update logic -------------------------------------------------

	static void update()
	{
		Config& cfg = GetConfig();
		MoveRec& o = order();
		if (o.state == MS_IDLE)
			return;

		// 1. resolve the animal by hand every update (no raw pointer kept between ticks)
		if (!o.hasAnimal)
		{
			transition(MS_IDLE, "NO_ANIMAL_RECORD", NULL);
			return;
		}
		Character* c = o.animal.getCharacter();
		if (c == NULL)
		{
			LogInfo("animal handle %s does not resolve (isValid=%d)", o.animal.toString().c_str(), o.animal.isValid() ? 1 : 0);
			transition(MS_IDLE, "ANIMAL_UNRESOLVED", NULL);
			Notify("Animal lost (unloaded or removed). MoveRec cancelled.");
			return;
		}
		// compare identifying fields (hand::operator== is virtual and gave a false mismatch for identical handles)
		hand cur = c->getHandle();
		if (!(cur.type == o.animal.type && cur.index == o.animal.index && cur.serial == o.animal.serial
			&& cur.container == o.animal.container && cur.containerSerial == o.animal.containerSerial))
		{
			LogInfo("handle mismatch: stored=%s object=%s", o.animal.toString().c_str(), c->getHandle().toString().c_str());
			transition(MS_IDLE, "HANDLE_MISMATCH", NULL);
			return;
		}
		if (c->isDead())
		{
			transition(MS_IDLE, "ANIMAL_DEAD", NULL);
			Notify("Animal died. MoveRec cancelled.");
			return;
		}
		if (c->isDestroyed())
		{
			transition(MS_IDLE, "ANIMAL_DESTROYED", NULL);
			Notify("Animal destroyed. MoveRec cancelled.");
			return;
		}
		if (c->isUnconcious())
		{
			transition(MS_IDLE, "ANIMAL_UNCONSCIOUS", NULL);
			Notify("Animal is unconscious. MoveRec cancelled.");
			return;
		}
		if (c->isKidnapped() || c->isBeingCarried())
		{
			transition(MS_IDLE, "ANIMAL_UNAVAILABLE", NULL);
			Notify("Animal unavailable. MoveRec cancelled.");
			return;
		}

		bool inList = ou->getCharacterUpdateList().find(c) != ou->getCharacterUpdateList().end();
		if (!inList && !o.unlistedLogged)
		{
			o.unlistedLogged = true;
			LogInfo("note: animal resolves by handle but is NOT in the character update list (offscreen/unloaded?)");
		}

		o.updates++;
		Ogre::Vector3 pos = c->getPosition();
		float d = dist2D(pos, o.dest);

		CharMovement* mv = c->getMovement();
		bool mvReached = false, mvPathFailed = false, mvStuck = false;
		Ogre::Vector3 mvDest(0, 0, 0);
		if (mv != NULL)
		{
			mvReached = mv->isDestinationReached();
			mvPathFailed = mv->pathFailed();
			mvStuck = mv->isProbablyStuck();
			mvDest = mv->getDestination();
		}

		OrdersReceiver* orr = c->getOrdersReciever();
		Tasker* t = orr ? orr->getFirstOrder() : NULL;
		int taskKey = -1;
		Ogre::Vector3 taskLoc(0, 0, 0);
		bool taskIsMove = false;
		if (t != NULL)
		{
			taskKey = (int)t->key();
			taskLoc = t->getLocation();
			taskIsMove = (t->key() == MOVE_CUS_ORDERED);
		}

		// periodic status line
		DWORD now = GetTickCount();
		if (now - o.lastStatusReal >= (DWORD)cfg.statusIntervalMs)
		{
			float moved = dist2D(pos, o.lastStatusPos);
			float secs = (now - o.lastStatusReal) / 1000.0f;
			bool pathOk = c->pathExists(o.dest);
			LogDebug("status %s: pos=(%.1f, %.1f, %.1f) dest=(%.1f, %.1f) d=%.1f speed=%.2f u/s pathExists=%d | mv=%s reached=%d pathFailed=%d stuck=%d mvDest=(%.1f, %.1f) | "
				"task=%s key=%d isMove=%d loc=(%.1f, %.1f) | inList=%d game=%.1fs real=%ums",
				MoveStateName(o.state), pos.x, pos.y, pos.z, o.dest.x, o.dest.z, d, moved / secs, pathOk ? 1 : 0,
				mv ? "ok" : "NULL", mvReached ? 1 : 0, mvPathFailed ? 1 : 0, mvStuck ? 1 : 0, mvDest.x, mvDest.z,
				t ? "yes" : "none", taskKey, taskIsMove ? 1 : 0, taskLoc.x, taskLoc.z,
				inList ? 1 : 0, (float)(g_gameTime - o.stateGame0), (unsigned)(now - o.stateReal0));
			o.lastStatusReal = now;
			o.lastStatusPos = pos;
		}

		// 2. arrival (own formula; the movement flag counts only when near)
		const char* arriveCode = NULL;
		if (d <= cfg.arrivalRadius)
			arriveCode = "ARRIVED_DIST";
		else if (mvReached && d <= cfg.arrivalRadius * kLooseRadiusFactor)
			arriveCode = "ARRIVED_FLAG_NEAR";
		else if (mvReached && !o.flagOnlyLogged)
		{
			o.flagOnlyLogged = true;
			LogInfo("note: isDestinationReached()=true but d=%.1f > arrival_radius=%.1f (flag/formula mismatch, [TO VERIFY])",
				d, cfg.arrivalRadius);
		}
		if (arriveCode != NULL)
		{
			LogInfo("arrival: code=%s d=%.1f mvReached=%d (radius=%.1f)", arriveCode, d, mvReached ? 1 : 0, cfg.arrivalRadius);
			if (o.state == MS_GOING)
				transition(MS_RETURNING, arriveCode, c);
			else
			{
				transition(MS_IDLE, arriveCode, c);
				Notify("Animal is back at the start point.");
			}
			return;
		}

		// 3. errors
		if (o.updates > kGraceUpdates)
		{
			// pathFailed (confirmed on consecutive checks)
			if (mvPathFailed) o.pathFailedCount++; else o.pathFailedCount = 0;
			if (o.pathFailedCount >= kConfirmChecks)
			{
				fail("PATH_FAILED", c, o.state == MS_GOING ? "Target unreachable. Returning." : "Cannot get back to the start point.");
				return;
			}

			// stuck (sustained)
			if (mvStuck)
			{
				if (o.stuckSince < 0.0) o.stuckSince = g_gameTime;
				if (g_gameTime - o.stuckSince >= cfg.stuckSeconds)
				{
					fail("STUCK", c, o.state == MS_GOING ? "Animal is stuck. Returning." : "Animal is stuck on the way back.");
					return;
				}
			}
			else
				o.stuckSince = -1.0;

			// track changes of the task location (waypoints are not exposed here:
			// in runs 3-5 the move task loc always equals the requested dest)
			if (t != NULL && taskIsMove)
			{
				if (o.havePrevTaskLoc && dist2D(taskLoc, o.prevTaskLoc) > 0.5f)
					o.lastRetargetGame = g_gameTime;
				o.prevTaskLoc = taskLoc;
				o.havePrevTaskLoc = true;

				// player re-targeted the animal with a new move order (same task key)
				float foreignTol = cfg.arrivalRadius * kForeignTargetFactor;
				if (dist2D(taskLoc, o.dest) > foreignTol)
				{
					o.foreignCount++;
					if (o.foreignCount >= 2)
					{
						LogInfo("overridden: move task loc=(%.1f, %.1f) differs from our dest=(%.1f, %.1f) by %.1f > %.1f",
							taskLoc.x, taskLoc.z, o.dest.x, o.dest.z, dist2D(taskLoc, o.dest), foreignTol);
						transition(MS_IDLE, "OVERRIDDEN", NULL);
						return;
					}
				}
				else
					o.foreignCount = 0;
			}
			else
				o.foreignCount = 0;

			// our order is gone / replaced
			bool ours = (t != NULL && taskIsMove);
			if (ours && !o.mismatchLogged && dist2D(taskLoc, o.dest) > 5.0f)
			{
				o.mismatchLogged = true;
				LogInfo("note: order location (%.1f, %.1f) differs from requested dest (%.1f, %.1f) by %.1f (game may snap the point)",
					taskLoc.x, taskLoc.z, o.dest.x, o.dest.z, dist2D(taskLoc, o.dest));
			}
			if (!ours) o.missingOrder++; else o.missingOrder = 0;
			if (o.missingOrder >= kConfirmChecks)
			{
				if (d <= cfg.arrivalRadius * kLooseRadiusFactor)
				{
					LogInfo("arrival: order ended near destination d=%.1f", d);
					if (o.state == MS_GOING)
						transition(MS_RETURNING, "ARRIVED_ORDER_ENDED_NEAR", c);
					else
					{
						transition(MS_IDLE, "ARRIVED_ORDER_ENDED_NEAR", c);
						Notify("Animal is back at the start point.");
					}
					return;
				}
				if (t != NULL)
				{
					// another task (e.g. the player gave a manual order): leave the animal alone
					LogInfo("overridden: first order key=%d (expected MOVE_CUS_ORDERED=%d)", taskKey, (int)MOVE_CUS_ORDERED);
					transition(MS_IDLE, "OVERRIDDEN", NULL);
					return;
				}
				fail("ORDER_ENDED_FAR", c, o.state == MS_GOING ? "Animal stopped before reaching the target. Returning." : "Animal stopped before the start point.");
				return;
			}
		}

		// 4. timeouts
		float gameElapsed = (float)(g_gameTime - o.stateGame0);
		DWORD realElapsed = now - o.stateReal0;
		if (gameElapsed >= cfg.timeoutSeconds)
		{
			fail("TIMEOUT_GAME", c, o.state == MS_GOING ? "Timeout. Returning." : "Timeout on the way back.");
			return;
		}
		if ((float)realElapsed >= cfg.timeoutSeconds * kRealCapFactor * 1000.0f)
		{
			fail("TIMEOUT_REAL", c, o.state == MS_GOING ? "Timeout (real). Returning." : "Timeout (real) on the way back.");
			return;
		}
	}

	void MoveFsmTick(float time)
	{
		if (ou == NULL)
			return;

		DWORD now = GetTickCount();
		if (g_tickCalls == 0)
		{
			g_firstTickReal = now;
			g_lastUpdateReal = now;
			LogInfo("first tick: time arg=%.5f (units [TO VERIFY])", time);
		}
		g_tickCalls++;
		if (g_tickCalls <= 5)
			LogDebug("tick#%u time arg=%.5f paused=%d", g_tickCalls, time, ou->isPaused() ? 1 : 0);

		bool paused = ou->isPaused();
		const bool loading = ou->isLoadingFromASaveGame() || ou->gameResetting;
		if (paused != g_wasPaused)
		{
			g_wasPaused = paused;
			LogDebug("pause %s (time arg=%.5f, sumTime=%.3f)", paused ? "ON" : "OFF", time, g_gameTime);
		}
		// no time is counted while paused or while the loading flag is set
		if (!paused && !loading)
			g_gameTime += time;

		if (g_order == NULL || g_order->state == MS_IDLE)
			return;

		const LoadGateResult lg = LoadGateStep(g_gate, loading, "go", MoveStateName(g_order->state));
		if (lg == LG_WAIT)
			return;
		if (lg == LG_DROP)
		{
			LogInfo("world loading/resetting (a save was loaded) while state=%s: dropping order", MoveStateName(g_order->state));
			transition(MS_IDLE, "WORLD_LOADING", NULL);
			return;
		}
		if (lg == LG_RESUME)
		{
			Character* rc = g_order->hasAnimal ? g_order->animal.getCharacter() : NULL;
			if (rc == NULL)
			{
				if (LoadGateExpired(g_gate))
				{
					LogInfo("go: resume failed, the animal did not resolve within %.0f s: dropping order", GetConfig().persistResumeSeconds);
					transition(MS_IDLE, "WORLD_LOADING", NULL);
				}
				return;
			}
			LoadGateResumeDone(g_gate, "go");
			issueMove(rc, g_order->dest, "resume");
		}

		if (now - g_lastUpdateReal < (DWORD)GetConfig().updateIntervalMs)
			return;
		g_lastUpdateReal = now;
		if (paused)
			return;
		update();
	}
}
