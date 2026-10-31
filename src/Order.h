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

#ifndef PACKSELLER_ORDER_H
#define PACKSELLER_ORDER_H

// Stage 7: the order cycle  Idle -> Going -> Selling -> Returning -> Idle  for ONE pack animal
// (movement from stage 3, sale from stage 4, started from the stage 6 window or by the
// "order" command). All functions: game main thread only (tick hook H1 / UiTick).
// The state lives in one Order object, changed only by transition() in Order.cpp.

#include <string>

#include <kenshi/util/hand.h>

namespace PS
{
	enum OrderState
	{
		OS_IDLE = 0,
		OS_GOING,       // walking to the trader
		OS_SELLING,     // in the interaction zone: the sale is executed on the next update
		OS_RETURNING,   // walking back to the start point
		OS_GOING_TOWN,  // 0.2: walking to a point at a town (v2 states)
		OS_SEARCHING    // 0.2: at the town, choosing a trader on the next update
	};

	enum OrderStartResult
	{
		OSR_OK = 0,
		OSR_BUSY,       // an order is already active (or the stage 3 "go" test is running)
		OSR_WORLD,      // world not ready / loading
		OSR_ANIMAL,     // animal hand does not resolve or is unusable
		OSR_TRADER,     // trader hand does not resolve or is unusable
		OSR_EXCEPTION,  // SEH exception while starting
		OSR_NO_APPROACH, // trader is in a building and no reachable approach point outside was found
		OSR_HOSTILE,     // hostile_policy=block and the trader's faction or town is hostile
		OSR_TOO_FAR,     // farther than max_trip_distance
		OSR_NO_TOWN      // the town was not found in the town list
	};

	const char* OrderStateName(OrderState s);
	OrderState OrderGetState();
	bool OrderActive();

	// Starts the cycle: start point = current position of the animal, trader and animal by hand.
	// Shows the player a message for every result (success included). Never throws.
	OrderStartResult OrderStart(const hand& animal, const hand& trader);

	// 0.2: starts the "town" order: the animal walks to a point at the town whose centre is (cx, cz), then looks for a
	// trader there. Messages and results as for OrderStart.
	OrderStartResult OrderStartTown(const hand& animal, float cx, float cz);

	// Called every frame from the tick hook with the hook's 'time' argument.
	void OrderTick(float time);

	// Stage 8: the persistent part of the order as plain values (hands as strings), see Persist.cpp.
	// 1 = trader orders (0.1.0 format, still written for them); 2 = orders with a town target (0.2). Both are read.
	const int kOrderFormatVersion = 1;
	const int kOrderFormatVersionMax = 2;

	struct OrderSnapshot
	{
		int version;
		int state;               // OrderState as it is now (Selling is rolled back to Returning on load)
		int retries;
		float phaseSeconds;      // tick seconds spent in the current phase
		float start[3];
		float dest[3];
		char animal[64];         // hand::toString of the pack animal
		char trader[64];         // hand::toString of the trader (empty until a trader is chosen in town mode)
		int townMode;            // 1: version 2 fields below are valid
		float townPos[3];        // town centre
		float townRadius;
		char town[96];           // town name (log, messages)
	};

	// Fills 's' and returns true when an order is active (nothing to save when Idle). Reads only plain
	// fields (no game call), so it is safe in the save hook.
	bool OrderTakeSnapshot(OrderSnapshot* s);
	// Called by the load hook: the order is restored later from OrderTick, when the world is ready
	// (both hands must resolve, otherwise the order is dropped with a message).
	void OrderSetPending(const OrderSnapshot& s);
	void OrderClearPending();
	// Command "save status": logs the snapshot that would be saved and the pending restore.
	void OrderPersistStatus();

	// Command "order [index|hand|status]" of PackSeller.cmd: index = row of the trader list
	// (distance sort, 1 = nearest) for the selected / first animal; hand = trader hand string.
	void OrderCommand(const std::string& arg);
}

#endif
