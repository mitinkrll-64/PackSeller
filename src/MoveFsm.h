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

#ifndef PACKSELLER_MOVEFSM_H
#define PACKSELLER_MOVEFSM_H

// Stage 3 prototype: Idle -> Going -> Returning -> Idle for one player animal.
// All functions must be called from the game main thread only
// (from the GameWorld::_NV_mainLoop_GPUSensitiveStuff hook).

class Character;

namespace PS
{
	enum MoveState
	{
		MS_IDLE = 0,
		MS_GOING,
		MS_RETURNING
	};

	const char* MoveStateName(MoveState s);
	MoveState MoveFsmState();

	// Called every frame from the tick hook with the hook's 'time' argument.
	// Accumulates time and runs the state machine at the configured interval.
	void MoveFsmTick(float time);

	// Debug command "go": pick an animal, remember start, send it to the test point.
	void MoveFsmStart();

	// Animal selection shared with stage 4 (selected usable animal, else first in squad).
	// Returns NULL if none; the pointer is valid only within the current call.
	Character* MoveFsmPickAnimal(const char** source);

	// Debug command "dump": log candidate animals and world flags.
	void MoveFsmDump();
}

#endif
