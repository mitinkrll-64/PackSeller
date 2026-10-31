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

// Stage 6 UI.
//
// Safety rules used here (docs/api-notes.md sections 1 and 9, skill mygui-ui):
//  * the only hook is InventoryGUI::_CONSTRUCTOR (AnimalInventoryLayout::_NV_setupSections that the
//    design named is a PLACEHOLDER symbol in KenshiLib.lib 0.5.0 and cannot be linked, see decisions.md):
//    it calls the original first (the layout and its sections are ready then) and then adds ONE button
//    next to OpenBagButton; the button belongs to the game's layout and is destroyed together with it,
//    so we never keep a pointer to it;
//  * MyGUI event handlers only queue a request (g_req); windows are created / changed / destroyed
//    from UiTick (main loop hook), never from inside a widget's own event;
//  * widget pointers of OUR window are kept only while the window exists and are checked by name
//    against the GUI tree every 250 ms (the game may destroy widgets behind our back);
//  * no global 'hand' objects (their constructors live in the game): hands are kept as strings;
//  * every game / MyGUI block runs under SEH (safeRun) plus try/catch for MyGUI exceptions.

#include "Ui.h"
#include "Seh.h"
#include "Config.h"
#include "Lang.h"
#include "Log.h"
#include "Order.h"
#include "Sell.h"
#include "TownList.h"
#include "TraderList.h"

#include <Debug.h>
#include <core/Functions.h>
#include <kenshi/GameWorld.h>
#include <kenshi/Globals.h>
#include <kenshi/Character.h>
#include <kenshi/CharacterAnimal.h>
#include <kenshi/Inventory.h>
#include <kenshi/gui/InventoryGUI.h>
#include <kenshi/util/hand.h>

#include <mygui/MyGUI_Gui.h>
#include <mygui/MyGUI_Window.h>
#include <mygui/MyGUI_Button.h>
#include <mygui/MyGUI_TextBox.h>
#include <mygui/MyGUI_ScrollView.h>
#include <mygui/MyGUI_RenderManager.h>
#include <mygui/MyGUI_InputManager.h>
#include <mygui/MyGUI_Delegate.h>

#include <windows.h>
#include <stdio.h>
#include <string>
#include <vector>

namespace PS
{
	static const char* kButtonSuffix = "PackSeller_TradersButton";
	static const char* kWindowName = "PackSeller_TradersWindow";
	static const char* kUserAnimal = "PackSeller_animal";
	static const char* kUserTrader = "PackSeller_trader";
	static const char* kUserSort = "PackSeller_sort";
	static const char* kUserCap = "PackSeller_cap";
	static const char* kUserTown = "PackSeller_town";
	static const char* kUserMode = "PackSeller_mode";

	// ---- SEH wrapper (no C++ objects with destructors in this function) ----------

	typedef void (*UiFn)(void*);

	static bool safeRun(UiFn fn, void* ctx, const char* what)
	{
		__try
		{
			fn(ctx);
			return true;
		}
		__except (PsSehFilterCode(GetExceptionCode()))
		{
			LogError("ui: EXCEPTION 0x%08X in %s", (unsigned)GetExceptionCode(), what);
			return false;
		}
	}

	// ---- state ---------------------------------------------------------------------

	// requests queued by MyGUI handlers (executed in UiTick)
	namespace
	{
	struct Requests
	{
		bool open;
		std::string animal;     // hand string of the animal
		std::string srcName;    // full name of the button that was clicked
		bool close;
		int sort;               // ListSort or -1
		std::string sell;       // hand string of the trader row or ""
		std::string sellTown;   // "x,z" centre of the town row or ""
		int mode;               // requested tab (0 traders, 1 towns) or -1
		Requests() : open(false), close(false), sort(-1), mode(-1) {}
	};
	}

	namespace
	{
	struct WinState
	{
		MyGUI::Window* win;
		MyGUI::TextBox* info;
		MyGUI::ScrollView* scroll;
		std::vector<MyGUI::Widget*> hdr;     // header widgets of the current tab
		std::vector<MyGUI::Button*> hdrBtns; // the sortable ones among them
		MyGUI::Button* tabBtn[2];            // Traders | Towns
		MyGUI::TextBox* note;
		int mode;                            // 0 traders, 1 towns
		int townSort;
		int hx;                              // x of the first header column
		TownListData* towns;
		std::vector<MyGUI::Widget*> rows; // children of the scroll view client
		std::vector<MyGUI::Button*> sellBtns; // the Sell buttons among 'rows' (captions show the order state)
		bool busyShown;                  // caption state of sellBtns: true = "Busy..."
		std::string animal;              // hand string
		std::string srcName;
		TraderList* list;
		int sort;
		bool modal;
		bool escPrev;
		DWORD lastCheck;
		int innerW;
		int colX[5];
		int colW[5];
		WinState() : win(NULL), info(NULL), scroll(NULL), note(NULL), mode(0), townSort(LIST_SORT_DISTANCE), hx(0), towns(NULL), busyShown(false), list(NULL), sort(0), modal(false), escPrev(false), lastCheck(0), innerW(0)
		{
			tabBtn[0] = tabBtn[1] = NULL;
			for (int i = 0; i < 5; ++i) { colX[i] = 0; colW[i] = 0; }
		}
	};
	}

	static Requests g_req;
	static WinState g_ws;
	static bool g_hookOk = false;

	// ---- small helpers ---------------------------------------------------------------

	static const char* ellipsisSafe(const std::string& s)
	{
		return s.c_str();
	}

	// forget our window without touching MyGUI (it is already gone)
	static void forgetWindow()
	{
		g_ws.win = NULL;
		g_ws.info = NULL;
		g_ws.scroll = NULL;
		g_ws.hdr.clear();
		g_ws.hdrBtns.clear();
		g_ws.tabBtn[0] = g_ws.tabBtn[1] = NULL;
		g_ws.note = NULL;
		g_ws.mode = 0;
		g_ws.townSort = LIST_SORT_DISTANCE;
		if (g_ws.towns != NULL)
		{
			delete g_ws.towns;
			g_ws.towns = NULL;
		}
		g_ws.rows.clear();
		g_ws.sellBtns.clear();
		g_ws.busyShown = false;
		g_ws.modal = false;
		if (g_ws.list != NULL)
		{
			delete g_ws.list;
			g_ws.list = NULL;
		}
	}

	static void closeWindow(const char* why)
	{
		if (g_ws.win == NULL)
			return;
		MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
		if (gui != NULL)
		{
			// make sure the window is still the one registered in the GUI tree before touching it
			MyGUI::Widget* found = gui->findWidgetT(kWindowName, false);
			if (found == g_ws.win)
			{
				if (g_ws.modal && MyGUI::InputManager::getInstancePtr() != NULL)
					MyGUI::InputManager::getInstancePtr()->removeWidgetModal(g_ws.win);
				gui->destroyWidget(g_ws.win);
			}
			else
				LogInfo("ui: window pointer is stale (widget tree has %p, we have %p), not destroying", (void*)found, (void*)g_ws.win);
		}
		forgetWindow();
		LogInfo("ui: window closed (%s)", why);
	}

	static bool gameIsForeground()
	{
		HWND fg = GetForegroundWindow();
		if (fg == NULL)
			return false;
		DWORD pid = 0;
		GetWindowThreadProcessId(fg, &pid);
		return pid == GetCurrentProcessId();
	}

	static bool escDown()
	{
		return gameIsForeground() && (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
	}

	// ---- MyGUI handlers: queue only ---------------------------------------------------------

	static void onTradersClick(MyGUI::Widget* sender)
	{
		if (sender == NULL)
			return;
		g_req.open = true;
		g_req.animal = sender->getUserString(kUserAnimal);
		g_req.srcName = sender->getName();
	}

	static void onCloseClick(MyGUI::Widget*)
	{
		g_req.close = true;
	}

	static void onWindowButton(MyGUI::Window*, const std::string& name)
	{
		if (name == "close")
			g_req.close = true;
	}

	static void onSortClick(MyGUI::Widget* sender)
	{
		if (sender == NULL)
			return;
		const std::string& v = sender->getUserString(kUserSort);
		int s = ParseListSort(v);
		if (s >= 0)
			g_req.sort = s;
	}

	static void onSellClick(MyGUI::Widget* sender)
	{
		if (sender == NULL)
			return;
		g_req.sell = sender->getUserString(kUserTrader);
	}

	static void onSellTownClick(MyGUI::Widget* sender)
	{
		if (sender == NULL)
			return;
		g_req.sellTown = sender->getUserString(kUserTown);
	}

	static void onTabClick(MyGUI::Widget* sender)
	{
		if (sender == NULL)
			return;
		const std::string& v = sender->getUserString(kUserMode);
		g_req.mode = (v == "1") ? 1 : 0;
	}

	// ---- hook H2: the button ------------------------------------------------------------------

	static void logWidget(const char* tag, MyGUI::Widget* w)
	{
		if (w == NULL)
		{
			LogInfo("ui:   %s: absent", tag);
			return;
		}
		LogInfo("ui:   %s: type=%s name=%s coord=(%d,%d,%d,%d) visible=%d", tag, w->getTypeName().c_str(), w->getName().c_str(),
			w->getLeft(), w->getTop(), w->getWidth(), w->getHeight(), w->getVisible() ? 1 : 0);
	}

	// true when r lies inside [0,w]x[0,h] and intersects no visible child of parent
	// (our own button is ignored: it may already exist when this is called for the log line)
	static bool rectFree(MyGUI::Widget* parent, const MyGUI::IntCoord& r, int w, int h)
	{
		if (r.left < 0 || r.top < 0 || r.width <= 0 || r.height <= 0 || r.left + r.width > w || r.top + r.height > h)
			return false;
		size_t n = parent->getChildCount();
		for (size_t i = 0; i < n && i < 40; ++i)
		{
			MyGUI::Widget* c = parent->getChildAt(i);
			if (c == NULL || !c->getVisible())
				continue;
			if (c->getName().find(kButtonSuffix) != std::string::npos)
				continue;
			if (r.left < c->getRight() && r.left + r.width > c->getLeft() && r.top < c->getBottom() && r.top + r.height > c->getTop())
				return false;
		}
		return true;
	}

	static void addButton(InventoryLayout* self, Inventory* inv)
	{
		static int s_calls = 0;
		++s_calls;
		Character* ch = (inv != NULL) ? inv->getCallbackCharacter() : NULL;
		// most inventory windows (characters, traders, containers) end here: stay silent
		if (ch == NULL || ch->isAnimal() == NULL)
			return;
		if (!ch->isWithThePlayer())
		{
			LogInfo("ui: inventory window #%d: animal '%s' is not with the player, no button", s_calls, ch->getName().c_str());
			return;
		}

		MyGUI::Button* openBag = NULL;
		self->assignWidget(openBag, "OpenBagButton", false, false);
		if (openBag == NULL)
		{
			// expected: the animal inventory creates a second InventoryGUI (backpack window) with another layout
			std::string names;
			size_t roots = self->mListWindowRoot.size();
			for (size_t i = 0; i < roots && i < 4; ++i)
			{
				MyGUI::Widget* r = self->mListWindowRoot[i];
				if (r == NULL)
					continue;
				names += r->getName() + "(" + r->getTypeName() + ",children=";
				char nb[16];
				_snprintf(nb, sizeof(nb) - 1, "%u", (unsigned)r->getChildCount());
				nb[sizeof(nb) - 1] = '\0';
				names += nb;
				for (size_t k = 0; k < r->getChildCount() && k < 8; ++k)
					names += std::string(" ") + r->getChildAt(k)->getName();
				names += ") ";
			}
			LogInfo("ui: inventory window #%d of animal '%s' has no OpenBagButton (backpack window or another layout), no button; layout prefix=%s roots=%u: %s",
				s_calls, ch->getName().c_str(), self->mPrefix.c_str(), (unsigned)roots, names.c_str());
			return;
		}
		MyGUI::Widget* parent = openBag->getParent();
		if (parent == NULL)
		{
			LogError("ui: OpenBagButton has no parent, no button");
			return;
		}

		const std::string btnName = self->mPrefix + kButtonSuffix;
		if (parent->findWidget(btnName) != NULL)
		{
			LogDebug("ui: inventory window #%d: button %s already exists, nothing to do", s_calls, btnName.c_str());
			return;
		}

		// Placement: the first candidate rectangle that lies inside the panel and does not intersect any
		// visible sibling. Order: (1) under the lowest overlapping sibling, full height; (2) above OpenBagButton,
		// reduced height; (3) next to every sibling (right of it, then under it), reduced height.
		// ui_button_x / ui_button_y (non-zero) override everything: offset from the OpenBagButton corner.
		const int kGap = 4;
		const int obL = openBag->getLeft();
		const int obR = openBag->getRight();
		const int obT = openBag->getTop();
		const int obH = openBag->getHeight();
		const int obW = openBag->getWidth();
		const int panelW = parent->getWidth();
		const int panelH = parent->getHeight();
		const int redH = obH < 26 ? obH : 26;
		LogInfo("ui: inventory window #%d for '%s' (layout prefix %s): parent size %dx%d visible=%d, children=%u",
			s_calls, ch->getName().c_str(), self->mPrefix.c_str(), panelW, panelH, parent->getVisible() ? 1 : 0, (unsigned)parent->getChildCount());
		logWidget("OpenBagButton", openBag);
		int bottom = openBag->getBottom();
		size_t n = parent->getChildCount();
		for (size_t i = 0; i < n && i < 40; ++i)
		{
			MyGUI::Widget* c = parent->getChildAt(i);
			if (c == NULL)
				continue;
			logWidget("child", c);
			if (c == openBag || !c->getVisible())
				continue;
			if (c->getLeft() < obR && c->getRight() > obL && c->getTop() >= obT)
			{
				if (c->getBottom() > bottom)
					bottom = c->getBottom();
			}
		}

		MyGUI::IntCoord bc(obL, bottom + kGap, obW, obH);
		const char* how = "below the lowest sibling";
		const Config& cfg = GetConfig();
		if (cfg.uiButtonX != 0 || cfg.uiButtonY != 0)
		{
			bc = MyGUI::IntCoord(obL + cfg.uiButtonX, obT + cfg.uiButtonY, obW, obH);
			how = "manual offset ui_button_x/ui_button_y";
		}
		else if (!rectFree(parent, bc, panelW, panelH))
		{
			bool found = false;
			// (2) above OpenBagButton
			MyGUI::IntCoord c2(obL, obT - redH - 2, obW, redH);
			if (rectFree(parent, c2, panelW, panelH))
			{
				bc = c2;
				how = "above OpenBagButton (reduced height)";
				found = true;
			}
			// (3) next to every sibling
			for (size_t i = 0; i < n && i < 40 && !found; ++i)
			{
				MyGUI::Widget* c = parent->getChildAt(i);
				if (c == NULL || !c->getVisible())
					continue;
				MyGUI::IntCoord r1(c->getRight() + kGap, c->getTop(), obW, redH);
				MyGUI::IntCoord r2(c->getLeft(), c->getBottom() + kGap, obW, redH);
				if (rectFree(parent, r1, panelW, panelH)) { bc = r1; found = true; }
				else if (rectFree(parent, r2, panelW, panelH)) { bc = r2; found = true; }
				if (found)
					how = "next to a sibling (reduced height)";
			}
			if (!found)
			{
				LogInfo("ui: WARNING no free rectangle found for the button, using the default one (set ui_button_x/ui_button_y to adjust)");
				how = "default (no free spot, may be clipped)";
			}
		}
		LogInfo("ui: button placement: %s -> (%d,%d,%d,%d), panel %dx%d, intersects a visible sibling: %d", how, bc.left, bc.top,
			bc.width, bc.height, panelW, panelH, rectFree(parent, bc, 1 << 20, 1 << 20) ? 0 : 1);

		MyGUI::Button* btn = parent->createWidget<MyGUI::Button>("Kenshi_Button1", bc, MyGUI::Align::Default, btnName);
		if (btn == NULL)
		{
			LogError("ui: could not create the button (createWidget returned NULL)");
			return;
		}
		btn->setCaption(MyGUI::UString(Tr(S_BTN_TRADERS)));
		btn->setUserString(kUserAnimal, ch->getHandle().toString());
		btn->eventMouseButtonClick += MyGUI::newDelegate(onTradersClick);
		LogInfo("ui: button %s created at (%d,%d,%d,%d) for animal '%s' hand=%s; visible=%d inheritedVisible=%d parentVisible=%d parentInheritedVisible=%d",
			btnName.c_str(), btn->getLeft(), btn->getTop(), btn->getWidth(), btn->getHeight(), ch->getName().c_str(), ch->getHandle().toString().c_str(),
			btn->getVisible() ? 1 : 0, btn->getInheritedVisible() ? 1 : 0, parent->getVisible() ? 1 : 0, parent->getInheritedVisible() ? 1 : 0);
	}

	namespace
	{
	struct LayoutCtx
	{
		InventoryLayout* layout;
		Inventory* inv;
	};
	}

	static void layoutFn(void* vp)
	{
		LayoutCtx* x = (LayoutCtx*)vp;
		try
		{
			addButton(x->layout, x->inv);
		}
		catch (...)
		{
			LogError("ui: C++ exception while adding the button (ignored)");
		}
	}

	// InventoryGUI::InventoryGUI(Inventory*, InventoryLayout*, RootObject*) is protected: an accessor
	// subclass makes the member pointer nameable (same trick as KenshiRotate's InventoryGUIAccess).
	namespace
	{
	class InventoryGUIAccess : public InventoryGUI
	{
	public:
		using InventoryGUI::_CONSTRUCTOR;
	};
	}

	static InventoryGUI* (*inventoryGuiCtor_orig)(InventoryGUI*, Inventory*, InventoryLayout*, RootObject*) = NULL;

	static InventoryGUI* inventoryGuiCtor_hook(InventoryGUI* self, Inventory* inv, InventoryLayout* layout, RootObject* callback)
	{
		InventoryGUI* r = inventoryGuiCtor_orig(self, inv, layout, callback);
		if (layout == NULL || !GetConfig().uiEnabled || MyGUI::Gui::getInstancePtr() == NULL)
			return r;
		LayoutCtx x;
		x.layout = layout;
		x.inv = inv;
		safeRun(layoutFn, &x, "InventoryGUI constructor (button)");
		return r;
	}

	bool UiInstallHook()
	{
		if (!GetConfig().uiEnabled)
		{
			LogInfo("ui: disabled by ui_enabled=0, hook H2 not installed");
			return false;
		}
		LangInit();
		if (KenshiLib::SUCCESS == KenshiLib::AddHook(KenshiLib::GetRealAddress(&InventoryGUIAccess::_CONSTRUCTOR),
			(void*)&inventoryGuiCtor_hook, (void**)&inventoryGuiCtor_orig))
		{
			g_hookOk = true;
			LogInfo("Hooked InventoryGUI::_CONSTRUCTOR OK");
		}
		else
			LogError("failed to hook InventoryGUI::_CONSTRUCTOR (no Traders button)");
		return g_hookOk;
	}

	// ---- window content -----------------------------------------------------------------------

	static const int kRowH = 30;

	// approximate pixels per character of the Kenshi UI font (used only to cut long texts)
	static const int kCharPx = 9;

	// number of UTF-8 code points
	static size_t utf8Len(const std::string& s)
	{
		size_t n = 0;
		for (size_t i = 0; i < s.size(); ++i)
			if ((s[i] & 0xC0) != 0x80)
				++n;
		return n;
	}

	// cuts to at most maxChars code points, appends "..." when cut
	static std::string ellipsize(const std::string& s, size_t maxChars)
	{
		if (maxChars < 4)
			maxChars = 4;
		if (utf8Len(s) <= maxChars)
			return s;
		size_t keep = maxChars - 3, n = 0, i = 0;
		for (; i < s.size(); ++i)
		{
			if ((s[i] & 0xC0) != 0x80)
			{
				if (n == keep)
					break;
				++n;
			}
		}
		return s.substr(0, i) + "...";
	}

	static MyGUI::TextBox* addText(MyGUI::Widget* parent, int x, int y, int w, int h, const std::string& text, bool hostile, bool right = false)
	{
		MyGUI::TextBox* t = parent->createWidget<MyGUI::TextBox>("Kenshi_TextboxStandardText", MyGUI::IntCoord(x, y, w, h), MyGUI::Align::Default);
		if (t == NULL)
			return NULL;
		t->setCaption(MyGUI::UString(text));
		t->setTextAlign(right ? MyGUI::Align::Right : MyGUI::Align::Left);
		if (hostile)
			t->setTextColour(MyGUI::Colour(1.0f, 0.35f, 0.3f));
		g_ws.rows.push_back(t);
		return t;
	}

	static void clearRows()
	{
		MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
		if (gui != NULL)
			for (size_t i = 0; i < g_ws.rows.size(); ++i)
				if (g_ws.rows[i] != NULL)
					gui->destroyWidget(g_ws.rows[i]);
		g_ws.rows.clear();
		g_ws.sellBtns.clear();
	}

	// QA-06: true only while OUR window still exists in the GUI tree (the game may destroy it behind our back);
	// otherwise the saved widget pointers are forgotten.
	static bool windowAlive()
	{
		if (g_ws.win == NULL)
			return false;
		MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
		if (gui == NULL || gui->findWidgetT(kWindowName, false) != g_ws.win)
		{
			LogInfo("ui: our window is gone, forgetting the pointers");
			forgetWindow();
			return false;
		}
		return true;
	}

	// Sell buttons show "Busy..." while an order is active (stage 7), "Sell" otherwise.
	static void updateSellCaptions()
	{
		if (!windowAlive())
			return;
		const bool busy = OrderActive();
		g_ws.busyShown = busy;
		for (size_t i = 0; i < g_ws.sellBtns.size(); ++i)
			if (g_ws.sellBtns[i] != NULL)
				g_ws.sellBtns[i]->setCaption(MyGUI::UString(Tr(busy ? S_BTN_BUSY : S_BTN_SELL)));
	}

	static void updateInfo()
	{
		if (g_ws.info == NULL)
			return;
		char buf[512];
		if (g_ws.mode == 1)
		{
			if (g_ws.towns == NULL)
				return;
			_snprintf(buf, sizeof(buf) - 1, Tr(S_INFO_TOWNS), ellipsisSafe(g_ws.towns->animalName), (int)g_ws.towns->shown.size());
		}
		else
		{
			if (g_ws.list == NULL)
				return;
			_snprintf(buf, sizeof(buf) - 1, Tr(S_INFO), ellipsisSafe(g_ws.list->animalName), (int)g_ws.list->shown.size());
		}
		buf[sizeof(buf) - 1] = '\0';
		g_ws.info->setCaption(MyGUI::UString(std::string(buf)));
	}

	static void buildTraderRows()
	{
		if (g_ws.scroll == NULL || g_ws.list == NULL)
			return;
		MyGUI::Widget* canvas = g_ws.scroll->getClientWidget();
		if (canvas == NULL)
			canvas = g_ws.scroll;
		const TraderList& L = *g_ws.list;
		const int innerW = g_ws.innerW;
		int y = 0;

		if (!L.ok || L.shown.empty())
		{
			// every trader hidden only because nothing in the pack can be sold: say so instead of "no traders nearby"
			const bool onlyNothing = L.ok && L.shown.empty() && L.st.hiddenBy[HIDE_NOTHING_TO_SELL] > 0;
			addText(canvas, 4, 4, innerW - 8, kRowH, L.ok ? Tr(onlyNothing ? S_EMPTY_NOTHING : S_EMPTY) : Tr(S_ERR_LIST), false);
			y = kRowH + 8;
		}
		else
		{
			for (size_t i = 0; i < L.shown.size(); ++i)
			{
				const TraderEntry& e = L.shown[i];
				char num[48];
				std::string town = e.town.empty() ? std::string("-") : e.town;
				const size_t townMax = (size_t)(g_ws.colW[1] / kCharPx);
				if (e.hostile)
				{
					const std::string tag = std::string(" [") + Tr(S_HOSTILE_TAG) + "]";
					const size_t tl = utf8Len(tag);
					town = ellipsize(town, townMax > tl + 4 ? townMax - tl : 4) + tag;
				}
				else
					town = ellipsize(town, townMax);
				addText(canvas, g_ws.colX[0], y, g_ws.colW[0], kRowH, ellipsize(e.name, (size_t)(g_ws.colW[0] / kCharPx)), e.hostile);
				addText(canvas, g_ws.colX[1], y, g_ws.colW[1], kRowH, town, e.hostile);
				if (e.isVeryFar)
					_snprintf(num, sizeof(num) - 1, "%.0f %s", e.dist, Tr(S_VERY_FAR_TAG));
				else if (e.isFar)
					_snprintf(num, sizeof(num) - 1, "%.0f %s", e.dist, Tr(S_FAR_TAG));
				else
					_snprintf(num, sizeof(num) - 1, "%.0f", e.dist);
				num[sizeof(num) - 1] = '\0';
				addText(canvas, g_ws.colX[2], y, g_ws.colW[2] - 12, kRowH, num, e.hostile, true);
				_snprintf(num, sizeof(num) - 1, "%d", e.money);
				num[sizeof(num) - 1] = '\0';
				addText(canvas, g_ws.colX[3], y, g_ws.colW[3] - 12, kRowH, num, e.hostile, true);

				MyGUI::Button* sell = canvas->createWidget<MyGUI::Button>("Kenshi_Button1",
					MyGUI::IntCoord(g_ws.colX[4], y + 1, g_ws.colW[4], kRowH - 4), MyGUI::Align::Default);
				if (sell != NULL)
				{
					sell->setCaption(MyGUI::UString(Tr(OrderActive() ? S_BTN_BUSY : S_BTN_SELL)));
					sell->setUserString(kUserTrader, e.handStr);
					sell->eventMouseButtonClick += MyGUI::newDelegate(onSellClick);
					if (e.sellable == 0)
						sell->setEnabled(false);
					g_ws.rows.push_back(sell);
					g_ws.sellBtns.push_back(sell);
				}
				y += kRowH;
			}
		}
		g_ws.scroll->setCanvasSize(innerW, y > 1 ? y : 1);
	}

	static void buildTownRows()
	{
		if (g_ws.scroll == NULL || g_ws.towns == NULL)
			return;
		MyGUI::Widget* canvas = g_ws.scroll->getClientWidget();
		if (canvas == NULL)
			canvas = g_ws.scroll;
		const TownListData& L = *g_ws.towns;
		const int innerW = g_ws.innerW;
		int y = 0;
		if (!L.ok || L.shown.empty())
		{
			addText(canvas, 4, 4, innerW - 8, kRowH, L.ok ? Tr(S_EMPTY_TOWNS) : Tr(S_ERR_LIST), false);
			y = kRowH + 8;
		}
		else
		{
			for (size_t i = 0; i < L.shown.size(); ++i)
			{
				const TownRow& e = L.shown[i];
				char num[64];
				std::string faction = e.faction.empty() ? std::string("-") : e.faction;
				const size_t facMax = (size_t)(g_ws.colW[1] / kCharPx);
				if (e.hostile)
				{
					const std::string tag = std::string(" [") + Tr(S_HOSTILE_TAG) + "]";
					const size_t tl = utf8Len(tag);
					faction = ellipsize(faction, facMax > tl + 4 ? facMax - tl : 4) + tag;
				}
				else
					faction = ellipsize(faction, facMax);
				addText(canvas, g_ws.colX[0], y, g_ws.colW[0], kRowH, ellipsize(e.name, (size_t)(g_ws.colW[0] / kCharPx)), e.hostile);
				addText(canvas, g_ws.colX[1], y, g_ws.colW[1], kRowH, faction, e.hostile);
				if (e.isVeryFar)
					_snprintf(num, sizeof(num) - 1, "%.0f %s", e.dist, Tr(S_VERY_FAR_TAG));
				else if (e.isFar)
					_snprintf(num, sizeof(num) - 1, "%.0f %s", e.dist, Tr(S_FAR_TAG));
				else
					_snprintf(num, sizeof(num) - 1, "%.0f", e.dist);
				num[sizeof(num) - 1] = '\0';
				addText(canvas, g_ws.colX[2], y, g_ws.colW[2] - 12, kRowH, num, e.hostile, true);
				if (e.loadedUnknown)
					_snprintf(num, sizeof(num) - 1, "?");
				else
					_snprintf(num, sizeof(num) - 1, "%d", e.loaded);
				num[sizeof(num) - 1] = '\0';
				addText(canvas, g_ws.colX[3], y, g_ws.colW[3] - 12, kRowH, num, e.hostile, true);

				MyGUI::Button* sell = canvas->createWidget<MyGUI::Button>("Kenshi_Button1",
					MyGUI::IntCoord(g_ws.colX[4], y + 1, g_ws.colW[4], kRowH - 4), MyGUI::Align::Default);
				if (sell != NULL)
				{
					char key[64];
					_snprintf(key, sizeof(key) - 1, "%.1f,%.1f", e.cx, e.cz);
					key[sizeof(key) - 1] = '\0';
					sell->setCaption(MyGUI::UString(Tr(OrderActive() ? S_BTN_BUSY : S_BTN_SELL)));
					sell->setUserString(kUserTown, key);
					sell->eventMouseButtonClick += MyGUI::newDelegate(onSellTownClick);
					g_ws.rows.push_back(sell);
					g_ws.sellBtns.push_back(sell);
				}
				y += kRowH;
			}
		}
		g_ws.scroll->setCanvasSize(innerW, y > 1 ? y : 1);
	}

	static void buildRows()
	{
		if (g_ws.mode == 1)
			buildTownRows();
		else
			buildTraderRows();
	}

	static void rebuildTraders(bool log)
	{
		hand ah;
		ah.fromString(g_ws.animal);
		if (g_ws.list == NULL)
			g_ws.list = new TraderList();
		const DWORD t0 = GetTickCount();
		BuildTraderList(*g_ws.list, g_ws.sort, &ah);
		const DWORD buildMs = GetTickCount() - t0;
		const TraderList& L = *g_ws.list;
		if (log)
			LogInfo("ui: list built in %u ms: ok=%d shown=%u hidden=%u sort=%s animal='%s'%s%s", (unsigned)buildMs, L.ok ? 1 : 0, (unsigned)L.shown.size(),
				(unsigned)L.hidden.size(), ListSortName(L.sort), L.animalName.c_str(), L.ok ? "" : " error=", L.ok ? "" : L.error.c_str());
		// the window shows every row; only the log lines are capped
		for (size_t i = 0; i < L.shown.size() && i < 60; ++i)
			LogInfo("ui:   row %u: '%s' town='%s' dist=%.0f money=%d hostile=%d sellable=%d hand=%s", (unsigned)i + 1, L.shown[i].name.c_str(),
				L.shown[i].town.c_str(), L.shown[i].dist, L.shown[i].money, L.shown[i].hostile ? 1 : 0, L.shown[i].sellable, L.shown[i].handStr.c_str());
	}

	static void rebuildTowns(bool log)
	{
		hand ah;
		ah.fromString(g_ws.animal);
		if (g_ws.towns == NULL)
			g_ws.towns = new TownListData();
		const DWORD t0 = GetTickCount();
		BuildTownList(*g_ws.towns, g_ws.townSort, &ah);
		const DWORD buildMs = GetTickCount() - t0;
		const TownListData& L = *g_ws.towns;
		if (log)
			LogInfo("ui: towns list built in %u ms: ok=%d shown=%u hidden: type=%d closed=%d hostile=%d radius=%d trip=%d sort=%s animal='%s'%s%s", (unsigned)buildMs,
				L.ok ? 1 : 0, (unsigned)L.shown.size(), L.hiddenType, L.hiddenClosed, L.hiddenHostile, L.hiddenRadius, L.hiddenTrip, ListSortName(g_ws.townSort),
				L.animalName.c_str(), L.ok ? "" : " error=", L.ok ? "" : L.error.c_str());
		for (size_t i = 0; i < L.shown.size() && i < 60; ++i)
			LogInfo("ui:   town %u: '%s' faction='%s' dist=%.0f loaded=%d hostile=%d centre=(%.0f, %.0f)", (unsigned)i + 1, L.shown[i].name.c_str(),
				L.shown[i].faction.c_str(), L.shown[i].dist, L.shown[i].loaded, L.shown[i].hostile ? 1 : 0, L.shown[i].cx, L.shown[i].cz);
	}

	static void rebuildList(bool log)
	{
		if (g_ws.mode == 1)
			rebuildTowns(log);
		else
			rebuildTraders(log);
	}

	static MyGUI::Button* addHeaderButton(MyGUI::Widget* cl, int x, int y, int w, int sortId)
	{
		MyGUI::Button* b = cl->createWidget<MyGUI::Button>("Kenshi_Button1", MyGUI::IntCoord(x, y, w - 2, 28), MyGUI::Align::Default);
		if (b == NULL)
			return NULL;
		b->setUserString(kUserSort, ListSortName(sortId));
		b->eventMouseButtonClick += MyGUI::newDelegate(onSortClick);
		return b;
	}

	static int currentSort()
	{
		return g_ws.mode == 1 ? g_ws.townSort : g_ws.sort;
	}

	static void clearHeader()
	{
		MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
		if (gui != NULL)
			for (size_t i = 0; i < g_ws.hdr.size(); ++i)
				if (g_ws.hdr[i] != NULL)
					gui->destroyWidget(g_ws.hdr[i]);
		g_ws.hdr.clear();
		g_ws.hdrBtns.clear();
	}

	static void addHeaderCell(MyGUI::Widget* cl, int col, StrId cap, int sortId)
	{
		const int x = g_ws.hx + g_ws.colX[col] + (sortId < 0 ? 4 : 0);
		if (sortId >= 0)
		{
			MyGUI::Button* btn = addHeaderButton(cl, x, 32, g_ws.colW[col], sortId);
			if (btn != NULL)
			{
				btn->setUserString(kUserCap, Tr(cap));
				g_ws.hdr.push_back(btn);
				g_ws.hdrBtns.push_back(btn);
			}
		}
		else
		{
			MyGUI::TextBox* t = cl->createWidget<MyGUI::TextBox>("Kenshi_TextboxStandardText", MyGUI::IntCoord(x, 32, g_ws.colW[col], 28), MyGUI::Align::Default);
			if (t != NULL)
			{
				t->setCaption(MyGUI::UString(Tr(cap)));
				g_ws.hdr.push_back(t);
			}
		}
	}

	// header row of the current tab (outside the scroll area, same columns as the rows)
	static void buildHeader()
	{
		if (g_ws.win == NULL)
			return;
		clearHeader();
		MyGUI::Widget* cl = g_ws.win->getClientWidget();
		if (cl == NULL)
			cl = g_ws.win;
		if (g_ws.mode == 1)
		{
			addHeaderCell(cl, 0, S_COL_TOWN, LIST_SORT_NAME);
			addHeaderCell(cl, 1, S_COL_FACTION, LIST_SORT_FACTION);
			addHeaderCell(cl, 2, S_COL_DIST, LIST_SORT_DISTANCE);
			addHeaderCell(cl, 3, S_COL_LOADED, -1);
		}
		else
		{
			addHeaderCell(cl, 0, S_COL_NAME, -1);
			addHeaderCell(cl, 1, S_COL_TOWN, LIST_SORT_TOWN);
			addHeaderCell(cl, 2, S_COL_DIST, LIST_SORT_DISTANCE);
			addHeaderCell(cl, 3, S_COL_MONEY, LIST_SORT_MONEY);
		}
	}

	static void updateHeader()
	{
		const int cur = currentSort();
		for (size_t i = 0; i < g_ws.hdrBtns.size(); ++i)
		{
			MyGUI::Button* btn = g_ws.hdrBtns[i];
			if (btn == NULL)
				continue;
			std::string t = btn->getUserString(kUserCap);
			if (ParseListSort(btn->getUserString(kUserSort)) == cur)
				t = "> " + t;
			btn->setCaption(MyGUI::UString(t));
		}
	}

	static void updateTabs()
	{
		for (int i = 0; i < 2; ++i)
		{
			if (g_ws.tabBtn[i] == NULL)
				continue;
			std::string t = Tr(i == 0 ? S_TAB_TRADERS : S_TAB_TOWNS);
			if (g_ws.mode == i)
				t = "> " + t;
			g_ws.tabBtn[i]->setCaption(MyGUI::UString(t));
		}
	}

	static void updateNote()
	{
		if (g_ws.note == NULL)
			return;
		StrId id = !GetConfig().orderEnabled ? S_NOTE : (GetConfig().sellDryRun ? S_NOTE_TEST : S_NOTE_LIVE);
		if (g_ws.mode == 1 && GetConfig().orderEnabled)
			id = S_NOTE_TOWNS;
		g_ws.note->setCaption(MyGUI::UString(Tr(id)));
	}

	static void openWindow()
	{
		closeWindow("reopen");
		MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
		if (gui == NULL || MyGUI::RenderManager::getInstancePtr() == NULL)
		{
			LogError("ui: MyGUI is not available, cannot open the window");
			return;
		}
		if (ou == NULL || ou->player == NULL || ou->isLoadingFromASaveGame() || ou->gameResetting)
		{
			LogInfo("ui: world not ready, window not opened");
			return;
		}
		// a leaked window with our name (e.g. after an earlier failure) must not be duplicated
		MyGUI::Widget* stale = gui->findWidgetT(kWindowName, false);
		if (stale != NULL)
		{
			LogInfo("ui: found an old window with our name, destroying it first");
			gui->destroyWidget(stale);
		}

		g_ws.animal = g_req.animal;
		g_ws.srcName = g_req.srcName;
		g_ws.sort = GetConfig().listSort;
		g_ws.mode = 0;
		g_ws.townSort = LIST_SORT_DISTANCE;
		rebuildList(true);

		const MyGUI::IntSize view = MyGUI::RenderManager::getInstance().getViewSize();
		int w = view.width * 56 / 100;
		if (w < 640) w = 640;
		if (w > 1000) w = 1000;
		if (w > view.width - 20) w = view.width - 20;
		int h = view.height * 62 / 100;
		if (h < 420) h = 420;
		if (h > 800) h = 800;
		if (h > view.height - 20) h = view.height - 20;
		const int left = (view.width - w) / 2;
		const int top = (view.height - h) / 2;

		MyGUI::Window* win = gui->createWidget<MyGUI::Window>("Kenshi_WindowCX", MyGUI::IntCoord(left, top, w, h), MyGUI::Align::Default, "Window", kWindowName);
		if (win == NULL)
		{
			LogError("ui: could not create the window");
			forgetWindow();
			return;
		}
		g_ws.win = win;
		win->setCaption(MyGUI::UString(Tr(S_WIN_TITLE)));
		win->eventWindowButtonPressed += MyGUI::newDelegate(onWindowButton);

		MyGUI::Widget* cl = win->getClientWidget();
		if (cl == NULL)
			cl = win;
		const MyGUI::IntCoord cc = win->getClientCoord();
		const int cw = cc.width;
		const int chh = cc.height;
		LogInfo("ui: window created at (%d,%d,%d,%d) view=%dx%d client=%dx%d modal=%d lang=%s", left, top, w, h, view.width, view.height,
			cw, chh, GetConfig().uiModal ? 1 : 0, LangIsRussian() ? "ru" : "en");

		// info line
		g_ws.info = cl->createWidget<MyGUI::TextBox>("Kenshi_TextboxStandardText", MyGUI::IntCoord(8, 4, cw - 16 - 272, 24), MyGUI::Align::Default);
		// tabs: Traders | Towns
		for (int i = 0; i < 2; ++i)
		{
			MyGUI::Button* tb = cl->createWidget<MyGUI::Button>("Kenshi_Button1", MyGUI::IntCoord(cw - 8 - 264 + i * 134, 2, 130, 28), MyGUI::Align::Default);
			if (tb != NULL)
			{
				tb->setUserString(kUserMode, i == 0 ? "0" : "1");
				tb->eventMouseButtonClick += MyGUI::newDelegate(onTabClick);
				g_ws.tabBtn[i] = tb;
			}
		}

		// list area
		const int svTop = 64;
		const int svH = chh - svTop - 44;
		g_ws.scroll = cl->createWidget<MyGUI::ScrollView>("Kenshi_ScrollView", MyGUI::IntCoord(8, svTop, cw - 16, svH), MyGUI::Align::Default);
		if (g_ws.scroll == NULL)
		{
			LogError("ui: could not create the scroll view");
			closeWindow("scroll view failed");
			return;
		}
		g_ws.scroll->setVisibleHScroll(false);
		g_ws.scroll->setCanvasAlign(MyGUI::Align::Default);
		const MyGUI::IntCoord sc = g_ws.scroll->getClientCoord();
		// the client area of the ScrollView skin is reported empty (0,0,0,0): size the columns from the widget width
		const int svW = g_ws.scroll->getWidth();
		const int kScrollbar = 24;
		const int kInset = 10;   // Client offset inside the Kenshi_ScrollView template (position 10,10 in the template)
		const int clientLeft = sc.width > 0 ? sc.left : kInset;
		g_ws.innerW = svW - kScrollbar - clientLeft;
		if (sc.width > 0 && sc.width < g_ws.innerW)
			g_ws.innerW = sc.width;
		if (g_ws.innerW < 400)
			g_ws.innerW = 400;
		LogInfo("ui: scroll view (8,%d,%d,%d) widget width=%d client=(%d,%d,%d,%d) -> clientLeft=%d innerW=%d, window width=%d client width=%d",
			svTop, cw - 16, svH, svW, sc.left, sc.top, sc.width, sc.height, clientLeft, g_ws.innerW, w, cw);

		// columns: name, town, distance, money, sell button; weights = 220 270 140 160 130 px at a 920 px base
		const int base[5] = { 200, 250, 190, 160, 130 };
		int x = 0;
		for (int i = 0; i < 5; ++i)
		{
			g_ws.colX[i] = x;
			g_ws.colW[i] = g_ws.innerW * base[i] / 920;
			x += g_ws.colW[i];
		}
		g_ws.colW[4] = g_ws.innerW - g_ws.colX[4];
		for (int i = 0; i < 5; ++i)
			LogInfo("ui: column %d: x=%d w=%d", i, g_ws.colX[i], g_ws.colW[i]);

		// header row (outside the scroll area, same columns)
		g_ws.hx = 8 + clientLeft;
		LogInfo("ui: header x: name=%d town=%d distance=%d money=%d (row widget x are the column x above, offset by the client left %d)",
			g_ws.hx + 4, g_ws.hx + g_ws.colX[1], g_ws.hx + g_ws.colX[2], g_ws.hx + g_ws.colX[3], clientLeft);
		buildHeader();

		// bottom: note and Close
		g_ws.note = cl->createWidget<MyGUI::TextBox>("Kenshi_TextboxStandardText", MyGUI::IntCoord(8, chh - 36, cw - 16 - 150, 28), MyGUI::Align::Default);
		MyGUI::Button* close = cl->createWidget<MyGUI::Button>("Kenshi_Button1", MyGUI::IntCoord(cw - 8 - 130, chh - 38, 130, 30), MyGUI::Align::Default);
		if (close != NULL)
		{
			close->setCaption(MyGUI::UString(Tr(S_BTN_CLOSE)));
			close->eventMouseButtonClick += MyGUI::newDelegate(onCloseClick);
		}

		updateHeader();
		updateTabs();
		updateNote();
		updateInfo();
		buildRows();

		if (GetConfig().uiModal && MyGUI::InputManager::getInstancePtr() != NULL)
		{
			MyGUI::InputManager::getInstancePtr()->addWidgetModal(win);
			g_ws.modal = true;
			LogInfo("ui: addWidgetModal applied (ui_modal=1)");
		}
		g_ws.escPrev = escDown();
		g_ws.lastCheck = GetTickCount();
	}

	static void doSort(int sort)
	{
		if (!windowAlive())
			return;
		if (g_ws.mode == 1)
			g_ws.townSort = sort;
		else
			g_ws.sort = sort;
		clearRows();
		rebuildList(true);
		updateHeader();
		updateInfo();
		buildRows();
	}

	static void doMode(int mode)
	{
		if (!windowAlive() || mode == g_ws.mode)
			return;
		g_ws.mode = mode;
		LogInfo("ui: tab switched to %s", mode == 1 ? "Towns" : "Traders");
		clearRows();
		rebuildList(true);
		buildHeader();
		updateHeader();
		updateTabs();
		updateNote();
		updateInfo();
		buildRows();
	}

	static void doSellTown(const std::string& key)
	{
		if (key.empty() || !windowAlive())
			return;
		float x = 0.0f, z = 0.0f;
		if (sscanf(key.c_str(), "%f,%f", &x, &z) != 2)
		{
			LogError("ui: bad town key '%s'", key.c_str());
			return;
		}
		if (!GetConfig().orderEnabled)
		{
			LogInfo("ui: town Sell clicked, but order_enabled=0: nothing started");
			Notify(Tr(S_MSG_FAIL));
			return;
		}
		hand ah;
		ah.fromString(g_ws.animal);
		LogInfo("ui: town Sell clicked: centre=(%.1f, %.1f) animal hand=%s -> town order (sell_dry_run=%d), order state=%s", x, z, g_ws.animal.c_str(),
			GetConfig().sellDryRun ? 1 : 0, OrderStateName(OrderGetState()));
		OrderStartResult r = OrderStartTown(ah, x, z);
		LogInfo("ui: OrderStartTown result=%d", (int)r);
		updateSellCaptions();
	}

	static void doSell(const std::string& traderStr)
	{
		if (traderStr.empty() || !windowAlive())
			return;
		hand ah;
		ah.fromString(g_ws.animal);
		hand th;
		th.fromString(traderStr);
		if (GetConfig().orderEnabled)
		{
			// stage 7: the click starts the order cycle (BUSY / errors are reported to the player by OrderStart)
			LogInfo("ui: Sell clicked: trader hand=%s animal hand=%s -> order cycle (order_enabled=1, sell_dry_run=%d), order state=%s",
				traderStr.c_str(), g_ws.animal.c_str(), GetConfig().sellDryRun ? 1 : 0, OrderStateName(OrderGetState()));
			OrderStartResult r = OrderStart(ah, th);
			LogInfo("ui: OrderStart result=%d", (int)r);
			updateSellCaptions();
			return;
		}
		LogInfo("ui: Sell clicked: trader hand=%s animal hand=%s -> dry run only (order_enabled=0)", traderStr.c_str(), g_ws.animal.c_str());
		SellOutcome oc;
		SellRunFor(true, &ah, &th, &oc);

		char msg[512];
		if (!oc.ran || !oc.planned)
		{
			LogInfo("ui: dry run did not produce a plan: %s", oc.error.c_str());
			Notify(oc.error.find("gone") != std::string::npos ? Tr(S_MSG_GONE) : Tr(S_MSG_FAIL));
			return;
		}
		if (oc.planUnits <= 0)
			_snprintf(msg, sizeof(msg) - 1, Tr(S_MSG_NOTHING), oc.traderName.c_str());
		else
			_snprintf(msg, sizeof(msg) - 1, Tr(S_MSG_DRY), oc.traderName.c_str(), oc.planUnits, oc.planRevenue);
		msg[sizeof(msg) - 1] = '\0';
		LogInfo("ui: dry run result: trader='%s' units=%d revenue=%d", oc.traderName.c_str(), oc.planUnits, oc.planRevenue);
		Notify(msg);
	}

	// ---- tick -----------------------------------------------------------------------------------------

	static void tickBody(void*)
	{
		try
		{
			MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
			const DWORD now = GetTickCount();

			if (g_ws.win != NULL)
			{
				const bool esc = escDown();
				if (esc && !g_ws.escPrev)
				{
					LogInfo("ui: Esc pressed");
					g_req.close = true;
				}
				g_ws.escPrev = esc;

				if (now - g_ws.lastCheck >= 250)
				{
					g_ws.lastCheck = now;
					if (gui == NULL || gui->findWidgetT(kWindowName, false) != g_ws.win)
					{
						LogInfo("ui: our window was destroyed by the game, forgetting the pointers");
						forgetWindow();
					}
					else if (ou == NULL || ou->isLoadingFromASaveGame() || ou->gameResetting)
						g_req.close = true;
					else if (gui->findWidgetT(g_ws.srcName, false) == NULL)
					{
						LogInfo("ui: the inventory window of the animal is gone, closing the trader window");
						g_req.close = true;
					}
					else if (OrderActive() != g_ws.busyShown)
						updateSellCaptions();
				}
			}

			if (g_req.close)
			{
				g_req.close = false;
				g_req.sort = -1;
				g_req.mode = -1;
				g_req.sell.clear();
				g_req.sellTown.clear();
				closeWindow("close requested");
			}
			if (g_req.open)
			{
				g_req.open = false;
				g_req.sort = -1;
				g_req.mode = -1;
				g_req.sell.clear();
				g_req.sellTown.clear();
				if (g_req.animal.empty())
					LogError("ui: Traders clicked but the button has no animal id");
				else
					openWindow();
			}
			if (g_req.mode >= 0)
			{
				int m = g_req.mode;
				g_req.mode = -1;
				doMode(m);
			}
			if (g_req.sort >= 0)
			{
				int s = g_req.sort;
				g_req.sort = -1;
				doSort(s);
			}
			if (!g_req.sell.empty())
			{
				std::string t = g_req.sell;
				g_req.sell.clear();
				doSell(t);
			}
			if (!g_req.sellTown.empty())
			{
				std::string t = g_req.sellTown;
				g_req.sellTown.clear();
				doSellTown(t);
			}
		}
		catch (...)
		{
			LogError("ui: C++ exception in tick (window closed)");
			g_req = Requests();
			try { closeWindow("exception"); } catch (...) { forgetWindow(); }
		}
	}

	static void emergencyFn(void*)
	{
		g_req = Requests();
		closeWindow("SEH exception");
	}

	// 0.2: the UI language was changed in the Options tab: rebuild the open trader window with the new captions
	// (it is reopened in the next tick; sort and tab return to the defaults). Does nothing if the window is closed.
	void UiLanguageChanged()
	{
		if (g_ws.win == NULL || g_ws.animal.empty())
			return;
		g_req.open = true;
		g_req.animal = g_ws.animal;
		g_req.srcName = g_ws.srcName;
		LogInfo("ui: language changed, the trader window is rebuilt in the next tick");
	}

	void UiTick(float)
	{
		if (!g_hookOk && g_ws.win == NULL)
			return;
		if (!g_req.open && !g_req.close && g_req.sort < 0 && g_req.mode < 0 && g_req.sell.empty() && g_req.sellTown.empty() && g_ws.win == NULL)
			return;
		if (!safeRun(tickBody, NULL, "UiTick"))
		{
			if (!safeRun(emergencyFn, NULL, "UiTick emergency close"))
				forgetWindow();
		}
	}
}
