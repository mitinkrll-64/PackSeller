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

#include "Log.h"
#include "Config.h"

#include <Debug.h>
#include <kenshi/GameWorld.h>
#include <kenshi/Globals.h>

#include <stdarg.h>
#include <stdio.h>
#include <string>

namespace PS
{
	static void format(char* buf, size_t size, const char* fmt, va_list args)
	{
		int n = _vsnprintf(buf, size - 1, fmt, args);
		if (n < 0)
			n = (int)size - 1;
		buf[n] = '\0';
	}

	void LogInfo(const char* fmt, ...)
	{
		char buf[1024];
		va_list args;
		va_start(args, fmt);
		format(buf, sizeof(buf), fmt, args);
		va_end(args);
		DebugLog(std::string("[PackSeller] ") + buf);
	}

	void LogDebug(const char* fmt, ...)
	{
		if (!GetConfig().debugLog)
			return;
		char buf[1024];
		va_list args;
		va_start(args, fmt);
		format(buf, sizeof(buf), fmt, args);
		va_end(args);
		DebugLog(std::string("[PackSeller] [dbg] ") + buf);
	}

	void LogError(const char* fmt, ...)
	{
		char buf[1024];
		va_list args;
		va_start(args, fmt);
		format(buf, sizeof(buf), fmt, args);
		va_end(args);
		ErrorLog(std::string("[PackSeller] ERROR: ") + buf);
	}

	void Notify(const char* text)
	{
		LogInfo("message to player: %s", text);
		if (GetConfig().showMessages && ou != NULL)
			ou->showPlayerAMessage(std::string("[PackSeller] ") + text, false);
	}
}
