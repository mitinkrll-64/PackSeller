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

// 0.2 settings tab (docs/api-notes.md, "Menu of the mod settings").
//
//  * H5 OptionsWindow::create: the original runs FIRST, then our tab is inserted before the last (vanilla MODS)
//    tab. The code is idempotent: every call looks for a tab with our caption; if it exists its page is refilled,
//    otherwise a new tab is created. No pointer to the tab, panel or lines is kept between calls: they are looked
//    up again through OptionsWindow::getSingleton()->tabs each time.
//  * H7 OptionsWindow::_NV_update: runs the deferred page rebuild (a check box callback must not destroy its own
//    line) and writes a few diagnostic lines after each create().
//  * The callbacks only change the settings (Config, Lang, PackSeller.user.cfg) and set a "rebuild" flag.
//  * Everything that touches the game runs under SEH.

#include "Options.h"
#include "Config.h"
#include "Lang.h"
#include "Log.h"
#include "Seh.h"
#include "Ui.h"
#include "Version.h"

#include <Debug.h>
#include <core/Functions.h>
#include <kenshi/Globals.h>
#include <kenshi/gui/ForgottenGUI.h>
#include <kenshi/gui/OptionsWindow.h>
#include <kenshi/gui/DatapanelGUI.h>
#include <kenshi/gui/DataPanelLine.h>
#include <mygui/MyGUI.h>

#include <windows.h>
#include <stdio.h>
#include <string>

namespace
{
	const char* const kPsOptTabCaption = "PackSeller";
	const char* const kPsOptPanelName = "packseller_options";

	// storage the game writes the check box values into (static: the address must stay valid)
	bool g_psOptDebug = false;
	bool g_psOptRu = false;
	bool g_psOptEn = true;

	unsigned g_psOptCreateCalls = 0;
	unsigned g_psOptUpdateCalls = 0;
	int g_psOptDiagLeft = 0;
	int g_psOptCategory = 48;
	int g_psOptLastHas = -1;
	bool g_psOptRebuild = false;
	bool g_psOptNeedRuHint = false;
	bool g_psOptSaveFailed = false;

	typedef void (*PsOptFn)(void*);

	bool psOptSafeRun(PsOptFn fn, void* ctx, const char* what)
	{
		__try
		{
			fn(ctx);
			return true;
		}
		__except (PsSehFilterCode(GetExceptionCode()))
		{
			PS::LogError("options: EXCEPTION 0x%08X in %s", (unsigned)GetExceptionCode(), what);
			return false;
		}
	}

	// index of our tab or -1
	int psOptFindTab(MyGUI::TabControl* tabs)
	{
		if (tabs == NULL)
			return -1;
		const size_t n = tabs->getItemCount();
		for (size_t i = 0; i < n; ++i)
			if (tabs->getItemNameAt(i).asUTF8() == kPsOptTabCaption)
				return (int)i;
		return -1;
	}

	DatapanelGUI* psOptPanelAt(MyGUI::TabControl* tabs, int index)
	{
		if (tabs == NULL || index < 0 || (size_t)index >= tabs->getItemCount())
			return NULL;
		DatapanelGUI** pp = tabs->getItemDataAt<DatapanelGUI*>((size_t)index, false);
		return pp != NULL ? *pp : NULL;
	}

	int psOptMaxCategory(MyGUI::TabControl* tabs)
	{
		int m = 0;
		const size_t n = tabs->getItemCount();
		for (size_t i = 0; i < n; ++i)
		{
			DatapanelGUI* p = psOptPanelAt(tabs, (int)i);
			if (p != NULL && p->currentCategory > m)
				m = p->currentCategory;
		}
		return m;
	}

	// ---- callbacks (queue only) -------------------------------------------------------------------

	struct PsOptCbCtx
	{
		DataPanelLine* line;
		int kind;   // 0 debug, 1 Russian, 2 English
	};

	bool psOptReadChecked(DataPanelLine* l, int kind)
	{
		bool widget = false, haveWidget = false;
		DataPanelLine_CheckBox* cb = static_cast<DataPanelLine_CheckBox*>(l);
		MyGUI::Button* b = cb != NULL ? cb->getCheckBox() : NULL;
		if (b != NULL)
		{
			widget = b->getStateSelected();
			haveWidget = true;
		}
		const bool ptr = kind == 0 ? g_psOptDebug : (kind == 1 ? g_psOptRu : g_psOptEn);
		PS::LogDebug("options: check box kind=%d callback: widget state=%d (have=%d) bool=%d", kind, widget ? 1 : 0, haveWidget ? 1 : 0, ptr ? 1 : 0);
		return haveWidget ? widget : ptr;
	}

	void psOptSetLanguage(const char* mode)
	{
		if (PS::LangSetLanguage(mode))
		{
			g_psOptNeedRuHint = false;
			if (!PS::SaveUserSetting("ui_language", mode))
				g_psOptSaveFailed = true;
			PS::UiLanguageChanged();
		}
		else
			g_psOptNeedRuHint = true;
	}

	void psOptCbFn(void* vp)
	{
		PsOptCbCtx* x = (PsOptCbCtx*)vp;
		const bool v = psOptReadChecked(x->line, x->kind);
		g_psOptSaveFailed = false;
		if (x->kind == 0)
		{
			g_psOptDebug = v;
			PS::GetConfig().debugLog = v;
			if (!PS::SaveUserSetting("debug_log", v ? "1" : "0"))
				g_psOptSaveFailed = true;
			PS::LogInfo("options: debug mode %s from the Options tab", v ? "ON" : "OFF");
		}
		else if (x->kind == 1)
		{
			if (v)
				psOptSetLanguage("ru");
			// unchecking the selected language is ignored: the page is rebuilt with exactly one selected
		}
		else
		{
			if (v)
				psOptSetLanguage("en");
		}
		g_psOptRebuild = true;
	}

	void psOptCb(DataPanelLine* l, int kind)
	{
		PsOptCbCtx c;
		c.line = l;
		c.kind = kind;
		try
		{
			psOptSafeRun(psOptCbFn, &c, "check box callback");
		}
		catch (...)
		{
			PS::LogError("options: C++ exception in a check box callback");
		}
	}

	void psOptOnDebug(DataPanelLine* l) { psOptCb(l, 0); }
	void psOptOnRu(DataPanelLine* l) { psOptCb(l, 1); }
	void psOptOnEn(DataPanelLine* l) { psOptCb(l, 2); }

	// ---- page ----------------------------------------------------------------------------------------

	void psOptTextLine(DatapanelGUI* p, const char* key, const char* text, int cat)
	{
		// left text only (s2 empty, key column visible)
		p->setLine(std::string(key), std::string(text), std::string(), cat, false, true);
	}

	void psOptFillPage(DatapanelGUI* p, int cat)
	{
		g_psOptDebug = PS::GetConfig().debugLog;
		g_psOptRu = PS::LangIsRussian();
		g_psOptEn = !g_psOptRu;

		char title[96];
		_snprintf(title, sizeof(title) - 1, PS::Tr(PS::S_OPT_TITLE), PS_VERSION_STRING);
		title[sizeof(title) - 1] = '\0';
		psOptTextLine(p, "packseller_title", title, cat);

		DataPanelLine_CheckBox* c = p->setLineCheckbox(PS::Tr(PS::S_OPT_DEBUG), &g_psOptDebug, cat);
		if (c != NULL)
			c->callback = MyGUI::newDelegate(psOptOnDebug);

		psOptTextLine(p, "packseller_lang", PS::Tr(PS::S_OPT_LANG_HEADER), cat);
		c = p->setLineCheckbox(PS::Tr(PS::S_OPT_RU), &g_psOptRu, cat);
		if (c != NULL)
			c->callback = MyGUI::newDelegate(psOptOnRu);
		c = p->setLineCheckbox(PS::Tr(PS::S_OPT_EN), &g_psOptEn, cat);
		if (c != NULL)
			c->callback = MyGUI::newDelegate(psOptOnEn);

		psOptTextLine(p, "packseller_hint", PS::Tr(PS::S_OPT_HINT), cat);
		if (g_psOptNeedRuHint)
			psOptTextLine(p, "packseller_needru", PS::Tr(PS::S_OPT_NEED_RU), cat);
		if (g_psOptSaveFailed)
			psOptTextLine(p, "packseller_savefail", PS::Tr(PS::S_OPT_SAVE_FAIL), cat);
	}

	void psOptRebuildFn(void*)
	{
		OptionsWindow* w = OptionsWindow::getSingleton();
		if (w == NULL || w->tabs == NULL)
			return;
		const int idx = psOptFindTab(w->tabs);
		DatapanelGUI* p = psOptPanelAt(w->tabs, idx);
		if (p == NULL)
		{
			PS::LogInfo("options: rebuild skipped, no PackSeller page (tab index %d)", idx);
			return;
		}
		p->clearPage(g_psOptCategory);
		psOptFillPage(p, g_psOptCategory);
		g_psOptNeedRuHint = false;
		g_psOptSaveFailed = false;
		PS::LogDebug("options: page rebuilt (category %d, ru=%d debug=%d)", g_psOptCategory, g_psOptRu ? 1 : 0, g_psOptDebug ? 1 : 0);
	}

	// ---- H5: create ------------------------------------------------------------------------------------

	struct PsOptBuildCtx
	{
		OptionsWindow* self;
	};

	void psOptBuildFn(void* vp)
	{
		OptionsWindow* self = ((PsOptBuildCtx*)vp)->self;
		MyGUI::TabControl* tabs = self->tabs;
		if (tabs == NULL)
		{
			PS::LogError("options: create() #%u: OptionsWindow::tabs is NULL, no PackSeller tab", g_psOptCreateCalls);
			return;
		}
		const size_t n = tabs->getItemCount();
		if (g_psOptCreateCalls == 1)
			PS::LogInfo("options: create() called (first time), %u tab(s), created=%d, tooltip=%p, game gui=%p", (unsigned)n, self->created ? 1 : 0, (void*)self->tooltip, (void*)gui);
		else
			PS::LogDebug("options: create() call #%u, %u tab(s), created=%d", g_psOptCreateCalls, (unsigned)n, self->created ? 1 : 0);
		for (size_t i = 0; i < n && i < 16; ++i)
			PS::LogDebug("options:   tab %u: '%s'%s", (unsigned)i, tabs->getItemNameAt(i).asUTF8().c_str(), psOptPanelAt(tabs, (int)i) != NULL ? " (DatapanelGUI)" : "");

		if (gui == NULL)
		{
			PS::LogError("options: the game gui pointer is NULL, no PackSeller tab");
			return;
		}

		const int existing = psOptFindTab(tabs);
		if (existing >= 0)
		{
			DatapanelGUI* p = psOptPanelAt(tabs, existing);
			PS::LogInfo("options: create() #%u: PackSeller tab already exists at index %d (panel %s), refreshing its page", g_psOptCreateCalls, existing, p != NULL ? "found" : "MISSING");
			if (p != NULL)
			{
				p->clearPage(g_psOptCategory);
				psOptFillPage(p, g_psOptCategory);
			}
			g_psOptDiagLeft = 3;
			return;
		}

		const int cat = psOptMaxCategory(tabs) + 1;
		g_psOptCategory = cat;
		MyGUI::TabItem* tab = (n > 0) ? tabs->insertItemAt(n - 1, kPsOptTabCaption) : tabs->addItem(kPsOptTabCaption);
		if (tab == NULL)
		{
			PS::LogError("options: could not insert the PackSeller tab");
			return;
		}
		DatapanelGUI* p = gui->createDatapanel(kPsOptPanelName, tab, true);
		if (p == NULL)
		{
			PS::LogError("options: createDatapanel returned NULL");
			return;
		}
		p->changeCategory(cat);
		p->setLineSpacing(25.0f);
		psOptFillPage(p, cat);
		tab->setVisible(false);
		tabs->setItemData(tab, p);
		g_psOptDiagLeft = 3;
		PS::LogInfo("options: PackSeller tab created: index %u of %u, category %d, caption '%s'", (unsigned)tabs->getItemIndex(tab), (unsigned)tabs->getItemCount(), cat, kPsOptTabCaption);
	}

	void (*g_psOptCreateOrig)(OptionsWindow*) = NULL;

	void psOptCreateHook(OptionsWindow* self)
	{
		g_psOptCreateOrig(self);
		++g_psOptCreateCalls;
		if (self == NULL)
			return;
		PsOptBuildCtx c;
		c.self = self;
		try
		{
			psOptSafeRun(psOptBuildFn, &c, "OptionsWindow::create (PackSeller tab)");
		}
		catch (...)
		{
			PS::LogError("options: C++ exception while building the PackSeller tab (ignored)");
		}
	}

	// ---- H7: update ------------------------------------------------------------------------------------

	void psOptDiagFn(void* vp)
	{
		OptionsWindow* self = (OptionsWindow*)vp;
		MyGUI::TabControl* tabs = self->tabs;
		if (tabs == NULL)
			return;
		const int idx = psOptFindTab(tabs);
		if (g_psOptDiagLeft > 0 || (idx >= 0 ? 1 : 0) != g_psOptLastHas)
		{
			if (g_psOptDiagLeft > 0)
				--g_psOptDiagLeft;
			g_psOptLastHas = idx >= 0 ? 1 : 0;
			MyGUI::TabItem* tab = idx >= 0 ? tabs->getItemAt((size_t)idx) : NULL;
			PS::LogDebug("options: update() #%u: window visible=%d, tabs=%u, PackSeller tab index=%d, selected=%u, tab widget visible=%d inherited=%d",
				g_psOptUpdateCalls, self->isVisible() ? 1 : 0, (unsigned)tabs->getItemCount(), idx, (unsigned)tabs->getIndexSelected(),
				tab != NULL && tab->getVisible() ? 1 : 0, tab != NULL && tab->getInheritedVisible() ? 1 : 0);
		}
	}

	void (*g_psOptUpdateOrig)(OptionsWindow*) = NULL;

	void psOptUpdateHook(OptionsWindow* self)
	{
		g_psOptUpdateOrig(self);
		++g_psOptUpdateCalls;
		if (self == NULL)
			return;
		try
		{
			if (g_psOptRebuild)
			{
				g_psOptRebuild = false;
				psOptSafeRun(psOptRebuildFn, NULL, "PackSeller page rebuild");
			}
			if (g_psOptDiagLeft > 0 || PS::GetConfig().debugLog)
				psOptSafeRun(psOptDiagFn, self, "PackSeller tab diagnostics");
		}
		catch (...)
		{
			PS::LogError("options: C++ exception in the update hook (ignored)");
		}
	}
}

namespace PS
{
	// GetRealAddress must receive the member pointer directly (api-notes section 9)
	bool OptionsInstallHooks()
	{
		if (!LangIsInited())
			LangInit();
		bool ok = false;
		if (KenshiLib::SUCCESS == KenshiLib::AddHook(KenshiLib::GetRealAddress(&OptionsWindow::create),
			(void*)&psOptCreateHook, (void**)&g_psOptCreateOrig))
		{
			ok = true;
			LogInfo("Hooked OptionsWindow::create OK");
		}
		else
			LogError("failed to hook OptionsWindow::create (no PackSeller settings tab)");
		if (ok)
		{
			if (KenshiLib::SUCCESS == KenshiLib::AddHook(KenshiLib::GetRealAddress(&OptionsWindow::_NV_update),
				(void*)&psOptUpdateHook, (void**)&g_psOptUpdateOrig))
				LogInfo("Hooked OptionsWindow::_NV_update OK");
			else
				LogError("failed to hook OptionsWindow::_NV_update (the settings page will not be rebuilt after a change)");
		}
		return ok;
	}
}
