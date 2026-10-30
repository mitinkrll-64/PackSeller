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

#ifndef PACKSELLER_LOG_H
#define PACKSELLER_LOG_H

// All plugin output goes to the RE_Kenshi log (DebugLog) with the
// "[PackSeller]" prefix.
namespace PS
{
	// always written
	void LogInfo(const char* fmt, ...);
	// written only when debug_log=1 in PackSeller.cfg
	void LogDebug(const char* fmt, ...);
	// error line (ErrorLog), always written
	void LogError(const char* fmt, ...);
	// show a message to the player (GameWorld::showPlayerAMessage) + log it
	void Notify(const char* text);
}

#endif
