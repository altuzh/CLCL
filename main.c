/*
 * CLCL
 *
 * main.c
 *
 * Copyright (C) 1996-2026 by Ohno Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 *		nakka@nakka.com
 */

/* Include Files */
#define _INC_OLE
#include <windows.h>
#undef  _INC_OLE
#include <commctrl.h>
#ifdef OP_XP_STYLE
#include <uxtheme.h>
#include <vssym32.h>
#endif	// OP_XP_STYLE
#include <tchar.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <dwmapi.h>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "dwmapi.lib")

#include "General.h"
#include "Memory.h"
#include "String.h"
#include "Data.h"
#include "Profile.h"
#include "Ini.h"
#include "Message.h"
#include "File.h"
#include "ClipBoard.h"
#include "Bitmap.h"
#include "History.h"
#include "Regist.h"
#include "Menu.h"
#include "SendKey.h"
#include "Format.h"
#include "Window.h"
#include "Tool.h"
#include "Viewer.h"
#include "Container.h"
#include "BinView.h"
#include "ToolTip.h"
#include "Caret.h"
#include "dpi.h"
#include "DarkMode.h"
#include "Search.h"
#include "DbHistory.h"
#include "Favorites.h"
#include "PinnedImage.h"
#include "resource.h"

/* Define */
#define ERROR_TITLE						TEXT("CLCL - Error")
#define MUTEX							TEXT("_CLCL_Mutex_")

#define WM_TRAY_NOTIFY					(WM_APP + 1000)		// Task tray
#define WM_KEY_HOOK						(WM_APP + 1001)		// Hook

#define TRAY_ID							1					// Task tray ID

#define ID_HISTORY_TIMER				1					// Timer ID
#define ID_RECHAIN_TIMER				2
#define ID_TOOL_TIMER					3
#define ID_PASTE_TIMER					4
#define ID_KEY_TIMER					5
#define ID_LCLICK_TIMER					6
#define ID_RCLICK_TIMER					7
#define ID_TRAY_HOVER_TIMER				8
#define ID_MENU_HOVER_SAFETY_TIMER		9

#define RECLIP_INTERVAL					1000
#define RECHAIN_INTERVAL				60000

#define TOOLFLAG_CALL					1
#define TOOLFLAG_PASTE					2

// Mask key to suppress menu bar activation (SC_KEYMENU)
// Use unassigned virtual key (does not generate characters, so no beep sounds)
#define MENU_MASK_VK					0xE8

// Time (ms) to determine if menu was closed immediately in attached display
#define MENU_ATTACH_RETRY_TIME			200

#define key_wait()						while (GetAsyncKeyState(VK_MENU) < 0 || \
											GetAsyncKeyState(VK_CONTROL) < 0 || \
											GetAsyncKeyState(VK_SHIFT) < 0 || \
											GetAsyncKeyState(VK_LWIN) < 0 || \
											GetAsyncKeyState(VK_RWIN) < 0) Sleep(100)

/* Global Variables */
HINSTANCE hInst;
static TCHAR app_path[MAX_PATH];
TCHAR work_path[MAX_PATH];

static HWND hViewerWnd;
static HWND hToolTip;
static HWND hClipNextWnd;
static HMENU popup_menu;
static HMODULE hook_lib;
static HMODULE themes_lib;
static HICON icon_tray;
static HICON icon_clip;
static HICON icon_clip_ban;
// Size of loaded system tray icon
static int tray_icon_size;

static POINT menu_sel_pt;
static int menu_sel_top;
static RECT menu_sel_rect;
static RECT menu_wnd_rect;

static BOOL accel_flag = TRUE;
static BOOL clip_flag;
static UINT native_menu_depth;
static BOOL session_ending = FALSE;
static int rechain_cnt;
static BOOL search_mode_requested = FALSE;
static UINT current_menu_id = 0;
static UINT current_menu_flags = 0;
static HMENU current_menu_handle = NULL;
static BOOL menu_delete_requested = FALSE;
static DATA_INFO *menu_delete_target = NULL;
#define RMB_ACTION_NONE				0
#define RMB_ACTION_ADD_TO_FAVORITES	1
#define RMB_ACTION_FAV_FOLDER_MENU	2
#define RMB_ACTION_FAV_ITEM_MENU	3
#define RMB_ACTION_DATE_FOLDER_MENU	4
static int menu_rmb_action = RMB_ACTION_NONE;
static DATA_INFO *menu_rmb_target_di = NULL;
static POINT menu_rmb_pt = {0, 0};
// Visible clipboard rows stay clickable while their menu is shown as a ghost.
typedef struct _MENU_CONTEXT_HIT {
	RECT rect;
	MENU_ITEM_INFO *item;
	struct _MENU_CONTEXT_HIT *next;
} MENU_CONTEXT_HIT;
static MENU_CONTEXT_HIT *menu_context_hits;
static MENU_ITEM_INFO *menu_context_pressed, *menu_context_pick;
static MENU_ITEM_INFO *menu_context_original, *menu_context_highlight;
static UINT menu_context_button;
static BOOL menu_context_left;
static DATA_INFO *menu_context_reopen_target;
static BOOL menu_reopen_requested = FALSE;
static MENU_ITEM_INFO *current_selected_mii = NULL;
static HWND main_window_handle = NULL;
static TCHAR menu_reopen_folder_title[BUF_SIZE] = {0};
static BOOL menu_reopen_is_fav = FALSE;
static DATA_INFO *menu_reopen_fav_folder = NULL;
static DATA_INFO *menu_reopen_fav_ancestor = NULL;
static POINT menu_cursor_restore_pt = {0, 0};
static BOOL menu_cursor_restore_needed = FALSE;
static POINT menu_reopen_pos = {0, 0};
static UINT menu_reopen_align = TPM_TOPALIGN | TPM_LEFTALIGN;
static BOOL has_reopen_pos = FALSE;
static BOOL menu_folder_hover_posted = FALSE;
static HHOOK menu_filter_hook = NULL;
static HHOOK menu_cbt_hook = NULL;
static HWND menu_root_wnd = NULL;
static RECT active_submenu_item_rect = {0};
static RECT favourites_menu_item_rect = {0};
static BOOL active_submenu_pending = FALSE;
#define SUBCLASS_ID_MENU_SUBMENU 1002
static HWND menu_ghost_wnd = NULL;
static HBITMAP menu_ghost_bmp = NULL;
static RECT menu_ghost_rect = {0};
static HWND menu_ghost_show(void);
static void menu_ghost_hide(void);
static LRESULT CALLBACK menu_cbt_proc(int nCode, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK menu_submenu_subclass_proc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
static int menu_find_folder_index(HMENU hMenu, const TCHAR *title, const BOOL is_fav);
static int menu_find_data_index(HMENU hMenu, const DATA_INFO *di);
static HMENU menu_find_data_submenu(HMENU hMenu, const DATA_INFO *di);
static DATA_INFO *menu_next_folder_on_path(DATA_INFO *ancestor, DATA_INFO *target);
static BOOL menu_open_reopen_folder(HWND hMenuWnd);
static HMENU menu_get_favourites_submenu(HMENU hRootMenu);
static void menu_position_cursor_in_submenu(HWND hwndSub);
static MENU_ITEM_INFO *menu_get_item_from_point(const POINT pt);
static BOOL menu_prune_empty_parent(DATA_INFO *root, DATA_INFO *parent_folder, const BOOL is_fav);
static void menu_record_reopen_position(HWND menu_wnd);
static BOOL menu_delete_current_item(void);
static LRESULT CALLBACK menu_msg_filter_proc(int nCode, WPARAM wParam, LPARAM lParam);
static void menu_context_clear_hits(void);
static BOOL CALLBACK menu_context_capture_items(HWND hwnd, LPARAM unused);
static BOOL tray_hover_active = FALSE;
static DWORD tray_hover_start_tick = 0;
static POINT tray_hover_pos = {0, 0};
static BOOL tray_preview_showing = FALSE;
static DWORD tray_last_click_tick = 0;
static void tray_cancel_preview(const HWND hWnd);

DATA_INFO history_data;
DATA_INFO regist_data;
static DATA_INFO *paste_di;

// Tool menu information
typedef struct _TOOL_MENU_INFO {
	BOOL enable;
	TOOL_INFO *ti;
	int paste;
} TOOL_MENU_INFO;
static TOOL_MENU_INFO tmi;

// Focus information
typedef struct _FOCUS_INFO {
	HWND active_wnd;
	HWND focus_wnd;
	POINT cpos;
	BOOL caret;
} FOCUS_INFO;
static FOCUS_INFO focus_info;

// Menu attach information
static DWORD menu_attach_tid;
static HHOOK menu_key_hook;
static HWND menu_key_wnd;

// Options
extern OPTION_INFO option;

/* Local Function Prototypes */
static void get_focus_info(FOCUS_INFO *fi, const BOOL caret);
static void set_focus_info(const FOCUS_INFO *fi);
static BOOL tray_message(const HWND hWnd, const DWORD dwMessage, const UINT uID, const HICON hIcon, const TCHAR *pszTip);
static void set_tray_icon(const HWND hWnd, const HICON hIcon, const TCHAR *buf);
static void load_tray_icon(void);
static void set_tray_tooltip(const HWND hWnd);
static BOOL show_menu_tooltip(const HWND tooltip_wnd, const HMENU hMenu, const UINT id, const BOOL mouse);
static BOOL show_data_tooltip(const HWND tooltip_wnd, DATA_INFO *item_di, const POINT *cur_pt, const RECT *custom_anchor, const int delay, const HWND hover_wnd, const RECT *hover_rect);
static void menu_mask_modifier_key(void);
static LRESULT CALLBACK menu_key_hook_proc(int nCode, WPARAM wParam, LPARAM lParam);
static BOOL menu_attach_begin(const HWND hWnd, const HWND active_wnd, const BOOL enable);
static void menu_attach_end(void);
static BOOL show_tool_menu(const HWND hWnd, DATA_INFO *di, const int paste, const HWND attach_wnd);
static BOOL show_popup_menu(const HWND hWnd, const ACTION_INFO *ai, const BOOL caret, const BOOL attach);
static BOOL action_execute(const HWND hWnd, const int type, const int id, const BOOL caret);
static BOOL action_check(const int type);
static BOOL clipboard_to_history(const HWND hWnd);
static BOOL item_to_clipboard(const HWND hWnd, DATA_INFO *from_di, const BOOL delete_flag);
static BOOL load_history(const HWND hWnd, const int load_flag);
static BOOL load_regist(const HWND hWnd);
static BOOL save_history(const HWND hWnd, const int save_flag);
BOOL save_regist(const HWND hWnd);
static void regist_hotkey(const HWND hWnd, const BOOL show_err);
static void unregist_hotkey(const HWND hWnd);
static void regist_hook(const HWND hWnd);
static void unregist_hook(void);
static BOOL winodw_initialize(const HWND hWnd);
static BOOL winodw_reset(const HWND hWnd);
static BOOL winodw_save(const HWND hWnd);
static BOOL winodw_end(const HWND hWnd);
static LRESULT CALLBACK main_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static void get_work_path(const HINSTANCE hInstance);
static void commnad_line_func(const HWND hWnd);
static BOOL init_application(const HINSTANCE hInstance);
static HWND init_instance(const HINSTANCE hInstance, const int CmdShow);

/*
 * theme_open - open XP theme
 */
#ifdef OP_XP_STYLE
HTHEME theme_open(const HWND hWnd)
{
	static FARPROC _OpenThemeData;
	HTHEME hTheme = NULL;

	if (themes_lib == NULL && (themes_lib = LoadLibrary(TEXT("uxtheme.dll"))) == NULL) {
		return NULL;
	}
	if (_OpenThemeData == NULL) {
		_OpenThemeData = GetProcAddress(themes_lib, "OpenThemeData");
	}
	if (_OpenThemeData != NULL) {
		hTheme = (HTHEME)_OpenThemeData(hWnd, L"Edit");
	}
	return hTheme;
}
#endif

/*
 * theme_close - close XP theme
 */
#ifdef OP_XP_STYLE
void theme_close(const HTHEME hTheme)
{
	static FARPROC _CloseThemeData;

	if (themes_lib == NULL || hTheme == NULL) {
		return;
	}
	if (_CloseThemeData == NULL) {
		_CloseThemeData = GetProcAddress(themes_lib, "CloseThemeData");
	}
	if (_CloseThemeData != NULL) {
		_CloseThemeData(hTheme);
	}
}
#endif

/*
 * theme_free - free XP theme
 */
#ifdef OP_XP_STYLE
void theme_free(void)
{
	if (themes_lib != NULL) {
		FreeLibrary(themes_lib);
		themes_lib = NULL;
	}
}
#endif

/*
 * theme_draw - draw with XP theme
 */
#ifdef OP_XP_STYLE
BOOL theme_draw(const HWND hWnd, const HRGN draw_hrgn, const HTHEME hTheme)
{
	static FARPROC _DrawThemeBackground;
	RECT rect, clip_rect;
	HRGN hrgn;
	HDC hdc;
	DWORD stats;

	if (themes_lib == NULL || hTheme == NULL) {
		return FALSE;
	}
	if (_DrawThemeBackground == NULL) {
		_DrawThemeBackground = GetProcAddress(themes_lib, "DrawThemeBackground");
	}
	if (_DrawThemeBackground == NULL) {
		return FALSE;
	}
	// Set state
	if (IsWindowEnabled(hWnd) == 0) {
		stats = ETS_DISABLED;
	} else if (GetFocus() == hWnd) {
		stats = ETS_FOCUSED;
	} else {
		stats = ETS_NORMAL;
	}
	// Draw window frame
	hdc = GetDCEx(hWnd, draw_hrgn, DCX_WINDOW | DCX_INTERSECTRGN);
	if (hdc == NULL) {
		hdc = GetWindowDC(hWnd);
	}
	GetWindowRect(hWnd, &rect);
	OffsetRect(&rect, -rect.left, -rect.top);
	ExcludeClipRect(hdc, rect.left + GetSystemMetrics(SM_CXEDGE), rect.top + GetSystemMetrics(SM_CYEDGE),
		rect.right - GetSystemMetrics(SM_CXEDGE), rect.bottom - GetSystemMetrics(SM_CYEDGE));
	clip_rect = rect;
	_DrawThemeBackground(hTheme, hdc, EP_EDITTEXT, stats, &rect, &clip_rect);
	ReleaseDC(hWnd, hdc);

	// Draw scrollbar
	GetWindowRect(hWnd, (LPRECT)&rect);
	hrgn = CreateRectRgn(rect.left + GetSystemMetrics(SM_CXEDGE), rect.top + GetSystemMetrics(SM_CYEDGE),
		rect.right - GetSystemMetrics(SM_CXEDGE), rect.bottom - GetSystemMetrics(SM_CYEDGE));
	CombineRgn(hrgn, hrgn, draw_hrgn, RGN_AND);
	DefWindowProc(hWnd, WM_NCPAINT, (WPARAM)hrgn, 0);
	DeleteObject(hrgn);
	return TRUE;
}
#endif

/*
 * set_menu_layerer - make menu translucent (Windows 2000+)
 */
#ifdef MENU_LAYERER
static BOOL set_menu_layerer(const HWND hWnd, const int alpha)
{
#ifndef WS_EX_LAYERED
#define WS_EX_LAYERED           0x00080000
#endif
#ifndef LWA_COLORKEY
#define LWA_COLORKEY            0x00000001
#endif
#ifndef LWA_ALPHA
#define LWA_ALPHA               0x00000002
#endif
	HANDLE user32_lib;
	FARPROC SetLayeredWindowAttributes;
	long lStyle;

	if (alpha <= 0 || alpha >= 255) {
		return TRUE;
	}

	// Get API for semi-transparency
	user32_lib = LoadLibrary(TEXT("user32.dll"));
	if (user32_lib == NULL) {
		return FALSE;
	}
	SetLayeredWindowAttributes = GetProcAddress(user32_lib, "SetLayeredWindowAttributes");
	if (SetLayeredWindowAttributes == NULL) {
		FreeLibrary(user32_lib);
		return FALSE;
	}

	lStyle = GetWindowLong(hWnd, GWL_EXSTYLE);
	if (lStyle & WS_EX_LAYERED) {
		// Already semi-transparent
		FreeLibrary(user32_lib);
		return TRUE;
	}
	lStyle |= WS_EX_LAYERED;
	SetWindowLong(hWnd, GWL_EXSTYLE, lStyle);

	// Semi-transparent
	SetLayeredWindowAttributes(hWnd, 0, alpha, LWA_ALPHA);
	FreeLibrary(user32_lib);
	return TRUE;
}
#endif

/*
 * _SetForegroundWindow - activate window
 */
BOOL _SetForegroundWindow(const HWND hWnd)
{
#ifndef SPI_GETFOREGROUNDLOCKTIMEOUT
#define SPI_GETFOREGROUNDLOCKTIMEOUT		  0x2000
#endif
#ifndef SPI_SETFOREGROUNDLOCKTIMEOUT
#define SPI_SETFOREGROUNDLOCKTIMEOUT		  0x2001
#endif
	int nTargetID, nForegroundID;
	UINT nTimeout;
	BOOL ret;

	nForegroundID = GetWindowThreadProcessId(GetForegroundWindow(), NULL);
	nTargetID = GetWindowThreadProcessId(hWnd, NULL);
	AttachThreadInput(nTargetID, nForegroundID, TRUE);

	SystemParametersInfo(SPI_GETFOREGROUNDLOCKTIMEOUT, 0, &nTimeout, 0);
	SystemParametersInfo(SPI_SETFOREGROUNDLOCKTIMEOUT, 0, (PVOID)0, 0);

	ret = SetForegroundWindow(hWnd);

	SystemParametersInfo(SPI_SETFOREGROUNDLOCKTIMEOUT, 0, (PVOID)nTimeout, 0);

	AttachThreadInput(nTargetID, nForegroundID, FALSE);
	return ret;
}

/*
 * get_focus_info - get focus information
 */
static void get_focus_info(FOCUS_INFO *fi, const BOOL caret)
{
	// Get window with focus
	fi->active_wnd = GetForegroundWindow();
	AttachThreadInput(GetWindowThreadProcessId(fi->active_wnd, NULL), GetCurrentThreadId(), TRUE);
	fi->focus_wnd = GetFocus();
	AttachThreadInput(GetWindowThreadProcessId(fi->active_wnd, NULL), GetCurrentThreadId(), FALSE);
	// Get caret position
	fi->cpos.x = fi->cpos.y = 0;
	fi->caret = (caret == TRUE) ? caret_get_pos(fi->active_wnd, fi->focus_wnd, &fi->cpos) : FALSE;
}

/*
 * set_focus_info - set window focus
 */
static void set_focus(const HWND active_wnd, const HWND focus_wnd)
{
	if (focus_wnd != NULL) {
		AttachThreadInput(GetWindowThreadProcessId(active_wnd, NULL), GetCurrentThreadId(), TRUE);
		if (GetFocus() != focus_wnd) {
			SetFocus(focus_wnd);
		}
		AttachThreadInput(GetWindowThreadProcessId(active_wnd, NULL), GetCurrentThreadId(), FALSE);
	}
}
static void set_focus_info(const FOCUS_INFO *fi)
{
	// Set active window
	_SetForegroundWindow(fi->active_wnd);
	SendMessage(fi->active_wnd, WM_NCACTIVATE, (WPARAM)TRUE, 0);
	// Set focus
	if (window_focus_check(fi->active_wnd) == TRUE) {
		set_focus(fi->active_wnd, fi->focus_wnd);
	}
}

/*
 * menu_mask_modifier_key - prevent menu bar activation by pressing modifier keys alone
 */
static void menu_mask_modifier_key(void)
{
	// Send only when Alt / Win is pressed
	if (GetAsyncKeyState(VK_MENU) >= 0 &&
		GetAsyncKeyState(VK_LWIN) >= 0 && GetAsyncKeyState(VK_RWIN) >= 0) {
		return;
	}
	// Press and release non-character key to clear modifier-only pressed state
	keybd_event(MENU_MASK_VK, 0, 0, 0);
	keybd_event(MENU_MASK_VK, 0, KEYEVENTF_KEYUP, 0);
}

/*
 * menu_key_hook_proc - forward key input to menu while menu is displayed
 */
static LRESULT CALLBACK menu_key_hook_proc(int nCode, WPARAM wParam, LPARAM lParam)
{
	KBDLLHOOKSTRUCT *kb = (KBDLLHOOKSTRUCT *)lParam;
	LPARAM lp;

	if (nCode == HC_ACTION && menu_key_wnd != NULL && kb != NULL &&
		(kb->flags & LLKHF_INJECTED) == 0) {
		switch (kb->vkCode) {
		case VK_SHIFT:		case VK_LSHIFT:		case VK_RSHIFT:
		case VK_CONTROL:	case VK_LCONTROL:	case VK_RCONTROL:
		case VK_MENU:		case VK_LMENU:		case VK_RMENU:
		case VK_LWIN:		case VK_RWIN:
		case VK_CAPITAL:	case VK_NUMLOCK:	case VK_SCROLL:
			// Pass modifier keys through as-is (to avoid breaking GetAsyncKeyState/GetKeyState)
			break;

		default:
			// Forward to menu owner window and consume key input
			lp = (LPARAM)1 | ((LPARAM)(kb->scanCode & 0xFF) << 16);
			if ((kb->flags & LLKHF_EXTENDED) != 0) {
				lp |= 0x01000000;
			}
			if ((kb->flags & LLKHF_ALTDOWN) != 0) {
				lp |= 0x20000000;
			}
			if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
				lp |= 0xC0000000;
			}
			PostMessage(menu_key_wnd, (UINT)wParam, kb->vkCode, lp);
			return 1;
		}
	}
	return CallNextHookEx(menu_key_hook, nCode, wParam, lParam);
}

/*
 * menu_attach_begin - attach input to active window thread
 */
static BOOL menu_attach_begin(const HWND hWnd, const HWND active_wnd, const BOOL enable)
{
	DWORD tid, my_tid;
	DWORD_PTR dwres;

	if (enable == FALSE || option.menu_attach_process != 1) {
		return FALSE;
	}
	if (active_wnd == NULL || IsWindow(active_wnd) == FALSE) {
		return FALSE;
	}
	// Attach only when active window is in foreground
	if (GetForegroundWindow() != active_wnd) {
		return FALSE;
	}
	// Do not attach to unresponsive applications
	if (SendMessageTimeout(active_wnd, WM_NULL, 0, 0,
		SMTO_ABORTIFHUNG | SMTO_BLOCK, 200, &dwres) == 0) {
		return FALSE;
	}
	my_tid = GetCurrentThreadId();
	tid = GetWindowThreadProcessId(active_wnd, NULL);
	if (tid == 0 || tid == my_tid) {
		return FALSE;
	}
	if (AttachThreadInput(my_tid, tid, TRUE) == FALSE) {
		// Fall back to conventional behavior if UIPI (elevated process) fails
		return FALSE;
	}
	menu_attach_tid = tid;
	menu_key_wnd = hWnd;
	// Hook to pass key input to menu without moving focus
	menu_key_hook = SetWindowsHookEx(WH_KEYBOARD_LL, menu_key_hook_proc, hInst, 0);
	if (menu_key_hook == NULL) {
		// Fall back to legacy behavior if hook cannot be set
		menu_attach_end();
		return FALSE;
	}
	// If active window remains in menu mode (Alt key latch, etc.)
	// Release it because the menu to be displayed will be closed immediately
	SendMessageTimeout(active_wnd, WM_CANCELMODE, 0, 0,
		SMTO_ABORTIFHUNG | SMTO_BLOCK, 200, &dwres);
	return TRUE;
}

/*
 * menu_attach_end - detach input
 */
static void menu_attach_end(void)
{
	if (menu_key_hook != NULL) {
		UnhookWindowsHookEx(menu_key_hook);
		menu_key_hook = NULL;
	}
	menu_key_wnd = NULL;
	if (menu_attach_tid != 0) {
		AttachThreadInput(GetCurrentThreadId(), menu_attach_tid, FALSE);
		menu_attach_tid = 0;
	}
}

/*
 * menu_find_folder_index - find zero-based index of folder item in menu by title or fav flag
 */
static int menu_find_folder_index(HMENU hMenu, const TCHAR *title, const BOOL is_fav)
{
	int cnt, i;

	if (hMenu == NULL) {
		return -1;
	}
	cnt = GetMenuItemCount(hMenu);
	for (i = 0; i < cnt; i++) {
		MENUITEMINFO mii;
		ZeroMemory(&mii, sizeof(mii));
		mii.cbSize = sizeof(mii);
		mii.fMask = MIIM_DATA;
		if (GetMenuItemInfo(hMenu, i, TRUE, &mii) && mii.dwItemData != 0) {
			MENU_ITEM_INFO *pmii = (MENU_ITEM_INFO *)mii.dwItemData;
			if (is_fav && pmii->is_favourites && (pmii->flag & MF_POPUP)) {
				return i;
			} else if (title != NULL && *title != TEXT('\0')) {
				if (pmii->set_di != NULL && pmii->set_di->type == TYPE_FOLDER &&
					pmii->set_di->title != NULL && lstrcmp(pmii->set_di->title, title) == 0) {
					return i;
				} else if (pmii->text != NULL && _tcsstr(pmii->text, title) != NULL) {
					return i;
				}
			}
		}
	}
	return -1;
}

/*
 * menu_find_data_index - find an item in one menu by its stable data node
 */
static int menu_find_data_index(HMENU hMenu, const DATA_INFO *di)
{
	int cnt, i;

	if (hMenu == NULL || di == NULL) {
		return -1;
	}
	cnt = GetMenuItemCount(hMenu);
	for (i = 0; i < cnt; i++) {
		MENUITEMINFO mii;
		ZeroMemory(&mii, sizeof(mii));
		mii.cbSize = sizeof(mii);
		mii.fMask = MIIM_DATA;
		if (GetMenuItemInfo(hMenu, i, TRUE, &mii) && mii.dwItemData != 0 &&
			((MENU_ITEM_INFO *)mii.dwItemData)->set_di == di) {
			return i;
		}
	}
	return -1;
}

/*
 * menu_find_data_submenu - find the submenu owned by a data folder
 */
static HMENU menu_find_data_submenu(HMENU hMenu, const DATA_INFO *di)
{
	int cnt, i;

	if (hMenu == NULL || di == NULL) {
		return NULL;
	}
	cnt = GetMenuItemCount(hMenu);
	for (i = 0; i < cnt; i++) {
		MENUITEMINFO mii;
		HMENU found;
		ZeroMemory(&mii, sizeof(mii));
		mii.cbSize = sizeof(mii);
		mii.fMask = MIIM_DATA | MIIM_SUBMENU;
		if (!GetMenuItemInfo(hMenu, i, TRUE, &mii)) {
			continue;
		}
		if (mii.dwItemData != 0 && ((MENU_ITEM_INFO *)mii.dwItemData)->set_di == di) {
			return mii.hSubMenu;
		}
		found = menu_find_data_submenu(mii.hSubMenu, di);
		if (found != NULL) {
			return found;
		}
	}
	return NULL;
}

/*
 * menu_next_folder_on_path - get the immediate child leading from ancestor to target
 */
static DATA_INFO *menu_next_folder_on_path(DATA_INFO *ancestor, DATA_INFO *target)
{
	DATA_INFO *child = target;
	DATA_INFO *parent;

	if (ancestor == NULL || target == NULL || ancestor == target) {
		return NULL;
	}
	while ((parent = data_check(ancestor, child)) != NULL) {
		if (parent == ancestor) {
			return child;
		}
		child = parent;
	}
	return NULL;
}

/*
 * menu_open_reopen_folder - open the target folder submenu via menu messages without moving cursor
 */
static BOOL menu_open_reopen_folder(HWND hMenuWnd)
{
	int idx;
	if (popup_menu == NULL || !IsWindowVisible(hMenuWnd) || menu_folder_hover_posted ||
		(menu_reopen_folder_title[0] == TEXT('\0') && !menu_reopen_is_fav)) {
		return FALSE;
	}
	idx = menu_find_folder_index(popup_menu, menu_reopen_folder_title, menu_reopen_is_fav);
	if (idx < 0) {
		return FALSE;
	}
	menu_folder_hover_posted = TRUE;
	menu_reopen_fav_ancestor = menu_reopen_is_fav ? &regist_data : NULL;
	SendMessage(hMenuWnd, 0x01E5 /* MN_SELECTITEM */, (WPARAM)idx, 0);
	// Queue navigation for the menu loop, after Windows has finished layout.
	PostMessage(main_window_handle, WM_KEYDOWN, VK_RIGHT, 1);
	PostMessage(main_window_handle, WM_KEYUP, VK_RIGHT, 0xC0000001);
	return TRUE;
}

/*
 * menu_get_favourites_submenu - find HMENU for Favourites popup inside root menu
 */
static HMENU menu_get_favourites_submenu(HMENU hRootMenu)
{
	int cnt;
	int i;

	if (hRootMenu == NULL) {
		return NULL;
	}
	cnt = GetMenuItemCount(hRootMenu);
	for (i = 0; i < cnt; i++) {
		MENUITEMINFO mii;
		ZeroMemory(&mii, sizeof(mii));
		mii.cbSize = sizeof(mii);
		mii.fMask = MIIM_DATA | MIIM_SUBMENU;
		if (GetMenuItemInfo(hRootMenu, i, TRUE, &mii) && mii.dwItemData != 0) {
			MENU_ITEM_INFO *pmii = (MENU_ITEM_INFO *)mii.dwItemData;
			if (pmii->is_favourites && (pmii->flag & MF_POPUP)) {
				return mii.hSubMenu;
			}
		}
	}
	return NULL;
}

/*
 * menu_position_cursor_in_submenu - place cursor directly onto submenu window over remaining items
 */
static void menu_position_cursor_in_submenu(HWND hwndSub)
{
	RECT rcSub;
	POINT pt, client_pt;
	HMENU expected = popup_menu;
	HMENU actual;
	int idx;

	if (!menu_cursor_restore_needed || !IsWindowVisible(hwndSub)) {
		return;
	}
	// Restore only after layout, and only on the expected HMENU. An ancestor
	// repaint must never finish restoration intended for a nested submenu.
	actual = (HMENU)SendMessage(hwndSub, MN_GETHMENU, 0, 0);
	if (menu_reopen_is_fav) {
		HMENU fav = menu_get_favourites_submenu(popup_menu);
		expected = menu_reopen_fav_ancestor == NULL || menu_reopen_fav_ancestor == &regist_data ?
			fav : menu_find_data_submenu(fav, menu_reopen_fav_ancestor);
	} else if (menu_reopen_folder_title[0] != TEXT('\0')) {
		idx = menu_find_folder_index(popup_menu, menu_reopen_folder_title, FALSE);
		expected = idx >= 0 ? GetSubMenu(popup_menu, idx) : NULL;
	}
	if (actual == NULL || actual != expected) {
		return;
	}
	if (menu_reopen_is_fav && menu_reopen_fav_folder != NULL &&
		menu_reopen_fav_ancestor != menu_reopen_fav_folder) {
		DATA_INFO *next = menu_next_folder_on_path(menu_reopen_fav_ancestor, menu_reopen_fav_folder);
		idx = menu_find_data_index(actual, next);
		if (idx < 0) {
			return;
		}
		menu_reopen_fav_ancestor = next;
		SendMessage(hwndSub, 0x01E5 /* MN_SELECTITEM */, (WPARAM)idx, 0);
		PostMessage(main_window_handle, WM_KEYDOWN, VK_RIGHT, 1);
		PostMessage(main_window_handle, WM_KEYUP, VK_RIGHT, 0xC0000001);
		return;
	}
	if (!GetWindowRect(hwndSub, &rcSub) || IsRectEmpty(&rcSub)) {
		return;
	}
	if (menu_context_reopen_target != NULL) {
		DATA_INFO *target = menu_context_reopen_target;
		MENU_CONTEXT_HIT *hit;
		MENU_ITEM_INFO *selected = NULL;
		BOOL is_fav = target == &regist_data || data_check(&regist_data, target) != NULL;
		BOOL is_folder = target == &regist_data || target->type == TYPE_FOLDER;
		idx = menu_find_data_index(actual, target);
		if (idx >= 0) {
			SendMessage(hwndSub, 0x01E5 /* MN_SELECTITEM */, (WPARAM)idx, 0);
		}
		// Discard the previous hierarchy before capturing the newly laid-out menus.
		menu_ghost_hide();
		if (!EnumThreadWindows(GetCurrentThreadId(), menu_context_capture_items, 0)) {
			menu_context_clear_hits();
		}
		for (hit = menu_context_hits; hit != NULL; hit = hit->next) {
			if (hit->item->set_di == target || (target == &regist_data && hit->item->is_favourites &&
				hit->item->set_di == NULL)) {
				selected = hit->item;
				break;
			}
		}
		menu_context_reopen_target = NULL;
		if (selected != NULL) {
			menu_context_original = menu_context_highlight = selected;
			menu_ghost_wnd = menu_ghost_show();
			menu_rmb_action = is_fav ? (is_folder ? RMB_ACTION_FAV_FOLDER_MENU : RMB_ACTION_FAV_ITEM_MENU) :
				(is_folder ? RMB_ACTION_DATE_FOLDER_MENU : RMB_ACTION_ADD_TO_FAVORITES);
			menu_rmb_target_di = target->type == TYPE_DATA ?
				data_check(is_fav ? &regist_data : &history_data, target) : target;
			menu_cursor_restore_needed = FALSE;
			menu_reopen_requested = TRUE;
			KillTimer(main_window_handle, ID_MENU_HOVER_SAFETY_TIMER);
			EndMenu();
			return;
		}
	}
	// Keep the pointer unchanged unless the menu shrank or its parent became
	// the landing menu. Clamp to a real row, not a border or scroll arrow.
	pt = menu_cursor_restore_pt;
	idx = MenuItemFromPoint(NULL, actual, pt);
	if (idx < 0) {
		RECT row;
		int count = GetMenuItemCount(actual);
		int i;
		__int64 distance = _I64_MAX;
		for (i = 0; i < count; i++) {
			if (GetMenuItemRect(NULL, actual, i, &row)) {
				int x = max(row.left, min(pt.x, row.right - 1));
				int y = max(row.top, min(pt.y, row.bottom - 1));
				__int64 dx = x - pt.x;
				__int64 dy = y - pt.y;
				__int64 d = dx * dx + dy * dy;
				if (d < distance) {
					distance = d;
					idx = i;
				}
			}
		}
		if (idx >= 0 && GetMenuItemRect(NULL, actual, idx, &row)) {
			pt.x = max(row.left + 2, min(pt.x, row.right - 3));
			pt.y = max(row.top + 2, min(pt.y, row.bottom - 3));
		}
	}
	menu_cursor_restore_needed = FALSE;
	menu_reopen_folder_title[0] = TEXT('\0');
	menu_reopen_is_fav = FALSE;
	menu_reopen_fav_folder = NULL;
	menu_reopen_fav_ancestor = NULL;
	menu_folder_hover_posted = FALSE;
	if (idx >= 0) {
		SendMessage(hwndSub, 0x01E5 /* MN_SELECTITEM */, (WPARAM)idx, 0);
	}
	SetCursorPos(pt.x, pt.y);
	client_pt = pt;
	ScreenToClient(hwndSub, &client_pt);
	PostMessage(hwndSub, WM_MOUSEMOVE, 0, MAKELPARAM(client_pt.x, client_pt.y));
	KillTimer(main_window_handle, ID_MENU_HOVER_SAFETY_TIMER);
	menu_ghost_hide();
}

/*
 * menu_prune_empty_parent - prune empty History folders; retain empty Favourites
 * folders and restore on their parent menu. Returns TRUE only if data was pruned.
 */
static BOOL menu_prune_empty_parent(DATA_INFO *root, DATA_INFO *parent_folder, const BOOL is_fav)
{
	DATA_INFO *grandparent;
	DATA_INFO *landing_folder = parent_folder;
	const RECT *restore_rect = &active_submenu_item_rect;
	BOOL pruned = FALSE;

	if (root == NULL || parent_folder == NULL) {
		return FALSE;
	}

	while (landing_folder != root && landing_folder->type == TYPE_FOLDER && landing_folder->child == NULL) {
		grandparent = data_check(root, landing_folder);
		if (grandparent == NULL) {
			break;
		}
		if (is_fav) {
			// User-created folders survive deletion of their last item.
			landing_folder = grandparent;
			break;
		}
		data_delete(&root->child, landing_folder, TRUE);
		landing_folder = grandparent;
		pruned = TRUE;
	}

	if (!pruned && landing_folder == parent_folder && (!is_fav || root->child != NULL)) {
		return FALSE;
	}

	menu_reopen_folder_title[0] = TEXT('\0');
	menu_reopen_is_fav = is_fav && root->child != NULL;
	menu_reopen_fav_folder = (menu_reopen_is_fav && landing_folder != root) ? landing_folder : NULL;
	if (menu_reopen_is_fav && landing_folder != root && landing_folder->title != NULL) {
		lstrcpyn(menu_reopen_folder_title, landing_folder->title, BUF_SIZE);
	}

	if (is_fav && root->child == NULL && favourites_menu_item_rect.right > favourites_menu_item_rect.left) {
		restore_rect = &favourites_menu_item_rect;
	}
	if (restore_rect->right > restore_rect->left) {
		menu_cursor_restore_pt.x = restore_rect->left + (restore_rect->right - restore_rect->left) / 2;
		menu_cursor_restore_pt.y = restore_rect->top + (restore_rect->bottom - restore_rect->top) / 2;
	}
	return pruned;
}

/*
 * menu_record_reopen_position - record root menu position and alignment relative to monitor corners
 */
static void menu_record_reopen_position(HWND menu_wnd)
{
	if (has_reopen_pos) {
		return;
	}
	HWND target_wnd = (menu_root_wnd != NULL && IsWindow(menu_root_wnd)) ? menu_root_wnd : menu_wnd;
	if (target_wnd == NULL || !IsWindow(target_wnd)) {
		return;
	}
	GetWindowRect(target_wnd, &menu_wnd_rect);
	if (IsRectEmpty(&menu_wnd_rect)) {
		return;
	}
	HMONITOR hMon = MonitorFromRect(&menu_wnd_rect, MONITOR_DEFAULTTONEAREST);
	MONITORINFO mi;
	mi.cbSize = sizeof(MONITORINFO);
	if (hMon != NULL && GetMonitorInfo(hMon, &mi)) {
		RECT rcWork = mi.rcWork;
		UINT align = 0;
		POINT reopen_pt;

		if (menu_wnd_rect.right >= rcWork.right - Scale(64) || menu_wnd_rect.right >= mi.rcMonitor.right - Scale(64)) {
			align |= TPM_RIGHTALIGN;
			reopen_pt.x = menu_wnd_rect.right;
		} else {
			align |= TPM_LEFTALIGN;
			reopen_pt.x = menu_wnd_rect.left;
		}

		if (menu_wnd_rect.bottom >= rcWork.bottom - Scale(64) || menu_wnd_rect.bottom >= mi.rcMonitor.bottom - Scale(64)) {
			align |= TPM_BOTTOMALIGN;
			reopen_pt.y = menu_wnd_rect.bottom;
		} else {
			align |= TPM_TOPALIGN;
			reopen_pt.y = menu_wnd_rect.top;
		}

		menu_reopen_align = align;
		menu_reopen_pos = reopen_pt;
		has_reopen_pos = TRUE;
	} else {
		menu_reopen_align = TPM_TOPALIGN | TPM_LEFTALIGN;
		menu_reopen_pos.x = menu_wnd_rect.left;
		menu_reopen_pos.y = menu_wnd_rect.top;
		has_reopen_pos = TRUE;
	}
}

/*
 * menu_delete_current_item - delete currently highlighted history or favourites item/folder in popup menu
 */
static BOOL menu_delete_current_item(void)
{
	if (menu_delete_requested) {
		return TRUE;
	}

	MENU_ITEM_INFO *mii = (current_selected_mii != NULL) ? current_selected_mii :
		((current_menu_id >= ID_MENUITEM_DATA) ? menu_get_info(current_menu_id) : NULL);
	if (mii == NULL) {
		POINT pt;
		GetCursorPos(&pt);
		mii = menu_get_item_from_point(pt);
	}
	if (mii != NULL && mii->set_di != NULL) {
		DATA_INFO *del_di = mii->set_di;
		if (del_di->type != TYPE_ITEM && del_di->type != TYPE_FOLDER) {
			DATA_INFO *parent = data_check(&history_data, del_di);
			if (parent == NULL) {
				parent = data_check(&regist_data, del_di);
			}
			if (parent != NULL && (parent->type == TYPE_ITEM || parent->type == TYPE_FOLDER)) {
				del_di = parent;
			}
		}

		if (data_check(&history_data, del_di) == NULL &&
			(del_di == &regist_data || data_check(&regist_data, del_di) == NULL)) {
			return FALSE;
		}
		if (!has_reopen_pos) {
			HWND hRoot = (menu_root_wnd != NULL && IsWindow(menu_root_wnd)) ? menu_root_wnd : FindWindow(TEXT("#32768"), NULL);
			if (hRoot != NULL) {
				menu_record_reopen_position(hRoot);
			}
		}
		GetCursorPos(&menu_cursor_restore_pt);
		menu_cursor_restore_needed = TRUE;
		menu_delete_target = del_di;
		menu_delete_requested = TRUE;
		menu_reopen_requested = TRUE;
		menu_folder_hover_posted = FALSE;
		menu_ghost_wnd = menu_ghost_show();
		// The native menu still references this data. Free it only after its
		// tracking loop and owner-draw teardown have completely finished.
		EndMenu();
		return TRUE;
	}
	return FALSE;
}

/*
 * menu_delete_pending_item - mutate the tree after destroying the old menu
 */
static void menu_delete_pending_item(HWND owner)
{
	DATA_INFO *target = menu_delete_target;
	DATA_INFO *parent = data_check(&history_data, target);
	BOOL is_fav = parent == NULL;
	DATA_INFO *root = is_fav ? &regist_data : &history_data;
	menu_delete_target = NULL;
	if (is_fav) {
		parent = data_check(root, target);
	}
	if (target == NULL || parent == NULL) {
		return;
	}
	menu_reopen_folder_title[0] = TEXT('\0');
	menu_reopen_is_fav = is_fav;
	menu_reopen_fav_folder = is_fav && parent != root ? parent : NULL;
	if (parent != root && parent->title != NULL) {
		lstrcpyn(menu_reopen_folder_title, parent->title, BUF_SIZE);
	}
	// Drop preview/selection references before freeing the selected bitmap.
	menu_context_clear_hits();
	current_selected_mii = NULL;
	current_menu_id = 0;
	tooltip_hide(hToolTip);
	data_delete(&root->child, target, TRUE);
	menu_prune_empty_parent(root, parent, is_fav);
	SendMessage(owner, is_fav ? WM_REGIST_CHANGED : WM_HISTORY_CHANGED, 0, 0);
}

/*
 * menu_item_from_point_ex - helper to find item index at point with fallback
 */
static int menu_item_from_point_ex(HWND hWndMenu, HMENU hMenu, POINT pt)
{
	int idx = MenuItemFromPoint(hWndMenu, hMenu, pt);
	if (idx < 0) {
		idx = MenuItemFromPoint(NULL, hMenu, pt);
	}
	if (idx >= 0) {
		return idx;
	}
	int count = GetMenuItemCount(hMenu);
	for (int i = 0; i < count; i++) {
		RECT rc;
		if (GetMenuItemRect(NULL, hMenu, i, &rc) || (hWndMenu != NULL && GetMenuItemRect(hWndMenu, hMenu, i, &rc))) {
			if (PtInRect(&rc, pt)) {
				return i;
			}
		}
	}
	return -1;
}

/*
 * menu_get_item_from_point - retrieve MENU_ITEM_INFO under screen coordinates
 */
static MENU_ITEM_INFO *menu_get_item_from_point(const POINT pt)
{
	HWND hWndMenu = WindowFromPoint(pt);
	TCHAR cls[32] = {0};
	MENUITEMINFO minfo;
	HMENU hMenu;
	int idx;

	if (hWndMenu == NULL) {
		if (current_selected_mii != NULL) {
			return current_selected_mii;
		}
		if (current_menu_id >= ID_MENUITEM_DATA) {
			return menu_get_info(current_menu_id);
		}
		return NULL;
	}
	GetClassName(hWndMenu, cls, sizeof(cls) / sizeof(TCHAR));
	if (lstrcmp(cls, TEXT("#32768")) != 0) {
		if (current_selected_mii != NULL) {
			return current_selected_mii;
		}
		if (current_menu_id >= ID_MENUITEM_DATA) {
			return menu_get_info(current_menu_id);
		}
		return NULL;
	}

	ZeroMemory(&minfo, sizeof(minfo));
	minfo.cbSize = sizeof(minfo);
	minfo.fMask = MIIM_DATA;

	// The window under the pointer owns the row. A previously selected submenu
	// can report an overlapping row when queried with a different menu window.
	hMenu = (HMENU)SendMessage(hWndMenu, MN_GETHMENU, 0, 0);
	if (hMenu != NULL) {
		idx = menu_item_from_point_ex(hWndMenu, hMenu, pt);
		if (idx >= 0 && GetMenuItemInfo(hMenu, idx, TRUE, &minfo) && minfo.dwItemData != 0) {
			return (MENU_ITEM_INFO *)minfo.dwItemData;
		}
	}
	return NULL;
}

static void menu_context_draw_highlight(HDC hdc)
{
	MENU_CONTEXT_HIT *hit;
	if (menu_context_highlight == NULL || menu_context_highlight == menu_context_original) {
		return;
	}
	// Replace the old selection captured in the ghost, then paint the new one.
	for (hit = menu_context_hits; hit != NULL; hit = hit->next) {
		if (hit->item == menu_context_original || hit->item == menu_context_highlight) {
			DRAWITEMSTRUCT draw = {0};
			draw.CtlType = ODT_MENU;
			draw.itemID = hit->item->id;
			draw.itemData = (ULONG_PTR)hit->item;
			draw.itemState = hit->item == menu_context_highlight ? ODS_SELECTED : 0;
			draw.hDC = hdc;
			draw.rcItem = hit->rect;
			OffsetRect(&draw.rcItem, -menu_ghost_rect.left, -menu_ghost_rect.top);
			menu_drawitem(&draw);
		}
	}
}

/*
 * menu_ghost_wnd_proc - window procedure for menu ghost window
 */
static LRESULT CALLBACK menu_ghost_wnd_proc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg) {
	case WM_NCHITTEST:
		return HTTRANSPARENT;

	case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hWnd, &ps);
			if (menu_ghost_bmp != NULL) {
				HDC hdcMem = CreateCompatibleDC(hdc);
				HBITMAP hOld = (HBITMAP)SelectObject(hdcMem, menu_ghost_bmp);
				RECT rc;
				GetClientRect(hWnd, &rc);
				BitBlt(hdc, 0, 0, rc.right - rc.left, rc.bottom - rc.top, hdcMem, 0, 0, SRCCOPY);
				SelectObject(hdcMem, hOld);
				DeleteDC(hdcMem);
			}
			menu_context_draw_highlight(hdc);
			EndPaint(hWnd, &ps);
			return 0;
		}

	case WM_ERASEBKGND:
		return 1;
	}
	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

/*
 * menu_enum_windows_proc - find union rectangle of all visible menu windows
 */
static BOOL CALLBACK menu_enum_windows_proc(HWND hwnd, LPARAM lParam)
{
	if (IsWindowVisible(hwnd)) {
		TCHAR cls[32] = {0};
		if (GetClassName(hwnd, cls, sizeof(cls) / sizeof(TCHAR)) > 0 &&
			lstrcmp(cls, TEXT("#32768")) == 0) {
			RECT *pRect = (RECT *)lParam;
			RECT rc;
			RedrawWindow(hwnd, NULL, NULL,
				RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
			GetWindowRect(hwnd, &rc);
			if (IsRectEmpty(pRect)) {
				*pRect = rc;
			} else {
				UnionRect(pRect, pRect, &rc);
			}
		}
	}
	return TRUE;
}

/*
 * menu_ghost_prep_submenus - prepare submenu background area for owner-draw painting
 */
static BOOL CALLBACK menu_ghost_prep_submenus(HWND hwnd, LPARAM lParam)
{
	if (IsWindowVisible(hwnd) && hwnd != menu_root_wnd) {
		TCHAR cls[32] = {0};
		if (GetClassName(hwnd, cls, sizeof(cls) / sizeof(TCHAR)) > 0 &&
			lstrcmp(cls, TEXT("#32768")) == 0) {
			HDC hdcMem = (HDC)lParam;
			RECT rcClient;
			GetClientRect(hwnd, &rcClient);
			MapWindowPoints(hwnd, NULL, (LPPOINT)&rcClient, 2);
			OffsetRect(&rcClient, -menu_ghost_rect.left, -menu_ghost_rect.top);
			HBRUSH hBr = CreateSolidBrush(dark_mode_is_dark() ? RGB(58, 58, 58) : RGB(255, 255, 255));
			if (hBr != NULL) {
				FillRect(hdcMem, &rcClient, hBr);
				DeleteObject(hBr);
			}
		}
	}
	return TRUE;
}

/*
 * menu_ghost_show - capture current menu screen area and show static ghost window
 */
static HWND menu_ghost_show(void)
{
	RECT rc = {0};
	int w, h;
	HDC hdcScreen, hdcMem;
	HBITMAP hOld;
	HWND hWndGhost;
	static BOOL registered = FALSE;
	// A second Delete may arrive before the previous menu has finished reopening.
	// Replace its visual without clearing the captured context-row references.
	if (menu_ghost_wnd != NULL) {
		if (IsWindow(menu_ghost_wnd)) DestroyWindow(menu_ghost_wnd);
		menu_ghost_wnd = NULL;
	}
	if (menu_ghost_bmp != NULL) {
		DeleteObject(menu_ghost_bmp);
		menu_ghost_bmp = NULL;
	}
	SetRectEmpty(&menu_ghost_rect);

	// Enumerate all active #32768 menu windows for this thread to get full bounding rect
	EnumThreadWindows(GetCurrentThreadId(), menu_enum_windows_proc, (LPARAM)&rc);
	if (IsRectEmpty(&rc)) {
		if (menu_root_wnd != NULL && IsWindow(menu_root_wnd)) {
			GetWindowRect(menu_root_wnd, &rc);
		}
	}
	w = rc.right - rc.left;
	h = rc.bottom - rc.top;
	if (w <= 0 || h <= 0) {
		return NULL;
	}
	// Owner-draw completion precedes desktop composition. Present every menu
	// panel before BitBlt can freeze an unpainted submenu into the ghost.
	GdiFlush();
	DwmFlush();

	if (!registered) {
		WNDCLASS wc;
		ZeroMemory(&wc, sizeof(wc));
		wc.lpfnWndProc = menu_ghost_wnd_proc;
		wc.hInstance = hInst;
		wc.lpszClassName = TEXT("CLCL_MenuGhost");
		wc.hCursor = LoadCursor(NULL, IDC_ARROW);
		RegisterClass(&wc);
		registered = TRUE;
	}

	hdcScreen = GetDC(NULL);
	if (hdcScreen == NULL) {
		return NULL;
	}
	hdcMem = CreateCompatibleDC(hdcScreen);
	if (hdcMem == NULL) {
		ReleaseDC(NULL, hdcScreen);
		return NULL;
	}

	menu_ghost_rect = rc;
	menu_ghost_bmp = CreateCompatibleBitmap(hdcScreen, w, h);
	if (menu_ghost_bmp != NULL) {
		MENU_CONTEXT_HIT *hit;
		hOld = (HBITMAP)SelectObject(hdcMem, menu_ghost_bmp);
		BitBlt(hdcMem, 0, 0, w, h, hdcScreen, rc.left, rc.top, SRCCOPY | CAPTUREBLT);
		if (menu_context_hits != NULL) {
			EnumThreadWindows(GetCurrentThreadId(), menu_ghost_prep_submenus, (LPARAM)hdcMem);
			for (hit = menu_context_hits; hit != NULL; hit = hit->next) {
				if (hit->item != NULL && (hit->item->is_folder_child || hit->item == menu_context_highlight)) {
					DRAWITEMSTRUCT draw = {0};
					draw.CtlType = ODT_MENU;
					draw.itemID = hit->item->id;
					draw.itemData = (ULONG_PTR)hit->item;
					draw.itemState = (hit->item == menu_context_highlight) ? ODS_SELECTED : 0;
					draw.hDC = hdcMem;
					draw.rcItem = hit->rect;
					OffsetRect(&draw.rcItem, -rc.left, -rc.top);
					menu_drawitem(&draw);
				}
			}
		}
		SelectObject(hdcMem, hOld);
	}
	DeleteDC(hdcMem);
	ReleaseDC(NULL, hdcScreen);

	if (menu_ghost_bmp == NULL) {
		return NULL;
	}

	hWndGhost = CreateWindowEx(
		WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_TRANSPARENT,
		TEXT("CLCL_MenuGhost"),
		TEXT(""),
		WS_POPUP,
		rc.left, rc.top, w, h,
		NULL, NULL, hInst, NULL
	);
	if (hWndGhost != NULL) {
		SetWindowPos(hWndGhost, HWND_TOPMOST, rc.left, rc.top, w, h,
			SWP_NOACTIVATE | SWP_SHOWWINDOW);
		UpdateWindow(hWndGhost);
	}
	return hWndGhost;
}

/*
 * menu_ghost_hide - destroy ghost window and cleanup bitmap
 */
static void menu_ghost_hide(void)
{
	if (menu_ghost_wnd != NULL && IsWindow(menu_ghost_wnd)) {
		DestroyWindow(menu_ghost_wnd);
		menu_ghost_wnd = NULL;
	}
	if (menu_ghost_bmp != NULL) {
		DeleteObject(menu_ghost_bmp);
		menu_ghost_bmp = NULL;
	}
	SetRectEmpty(&menu_ghost_rect);
	menu_context_clear_hits();
}

/*
 * Context hit targets for clipboard rows underneath the Add to Favourites menu
 */
static void menu_context_clear_hits(void)
{
	menu_context_pressed = NULL;
	menu_context_original = menu_context_highlight = NULL;
	while (menu_context_hits != NULL) {
		MENU_CONTEXT_HIT *hit = menu_context_hits;
		menu_context_hits = hit->next;
		mem_free((void **)&hit);
	}
}

static BOOL CALLBACK menu_context_capture_items(HWND hwnd, LPARAM unused)
{
	TCHAR cls[32];
	HMENU menu;
	RECT bounds;
	int i;
	(void)unused;
	if (!IsWindowVisible(hwnd) || !GetClassName(hwnd, cls, 32) ||
		lstrcmp(cls, TEXT("#32768")) != 0 || !GetWindowRect(hwnd, &bounds)) {
		return TRUE;
	}
	menu = (HMENU)SendMessage(hwnd, MN_GETHMENU, 0, 0);
	for (i = 0; i < GetMenuItemCount(menu); i++) {
		MENUITEMINFO info = {0};
		MENU_ITEM_INFO *item;
		MENU_CONTEXT_HIT *hit;
		RECT row;
		info.cbSize = sizeof(info);
		info.fMask = MIIM_DATA | MIIM_STATE | MIIM_FTYPE;
		if (!GetMenuItemInfo(menu, i, TRUE, &info) || !info.dwItemData ||
			(info.fState & MFS_DISABLED) || (info.fType & MFT_SEPARATOR)) {
			continue;
		}
		item = (MENU_ITEM_INFO *)info.dwItemData;
		if (item == NULL ||
			(!GetMenuItemRect(NULL, menu, i, &row) && !GetMenuItemRect(hwnd, menu, i, &row))) {
			continue;
		}
		if (row.left < bounds.left || row.top < bounds.top) {
			OffsetRect(&row, bounds.left, bounds.top);
		}
		if (!IntersectRect(&row, &row, &bounds)) {
			continue;
		}
		hit = mem_alloc(sizeof(*hit));
		if (hit == NULL) {
			return FALSE;
		}
		hit->rect = row;
		hit->item = item;
		hit->next = menu_context_hits;
		menu_context_hits = hit;
	}
	return TRUE;
}

static BOOL CALLBACK menu_context_outside_popup(HWND hwnd, LPARAM point)
{
	TCHAR cls[32];
	RECT rect;
	return !IsWindowVisible(hwnd) || !GetClassName(hwnd, cls, 32) ||
		lstrcmp(cls, TEXT("#32768")) != 0 || !GetWindowRect(hwnd, &rect) ||
		!PtInRect(&rect, *(POINT *)point);
}

static LRESULT CALLBACK menu_context_filter_proc(int code, WPARAM wParam, LPARAM lParam)
{
	MSG *msg = (MSG *)lParam;
	if (code == MSGF_MENU && (msg->message == WM_LBUTTONDOWN || msg->message == WM_LBUTTONUP ||
		msg->message == WM_RBUTTONDOWN || msg->message == WM_RBUTTONUP)) {
		MENU_CONTEXT_HIT *hit;
		MENU_ITEM_INFO *item = NULL;
		BOOL down = msg->message == WM_LBUTTONDOWN || msg->message == WM_RBUTTONDOWN;
		UINT button = msg->message == WM_LBUTTONDOWN || msg->message == WM_LBUTTONUP ? VK_LBUTTON : VK_RBUTTON;
		// A live context menu (including its cascading children) takes priority.
		if (EnumThreadWindows(GetCurrentThreadId(), menu_context_outside_popup, (LPARAM)&msg->pt)) {
			for (hit = menu_context_hits; hit != NULL; hit = hit->next) {
				if (PtInRect(&hit->rect, msg->pt)) {
					if (button == VK_RBUTTON) {
						if ((hit->item->set_di != NULL &&
							(data_check(&history_data, hit->item->set_di) != NULL ||
							 data_check(&regist_data, hit->item->set_di) != NULL)) ||
							hit->item->set_di == &regist_data ||
							hit->item->is_favourites) {
							item = hit->item;
						}
					} else {
						item = hit->item;
					}
				}
			}
		}
		if (down && item != NULL) {
			menu_context_pressed = item;
			menu_context_button = button;
			return 1;
		}
		if (!down && menu_context_pressed != NULL && menu_context_button == button) {
			if (item != NULL) {
				menu_context_pick = item;
				menu_context_left = button == VK_LBUTTON;
				menu_rmb_pt = msg->pt;
				menu_context_highlight = item;
				if (menu_ghost_wnd != NULL) {
					InvalidateRect(menu_ghost_wnd, NULL, FALSE);
					UpdateWindow(menu_ghost_wnd);
				}
				EndMenu();
			}
			menu_context_pressed = NULL;
			return 1; // Never leak the click to the application behind the ghost.
		}
	}
	return CallNextHookEx(NULL, code, wParam, lParam);
}

/* WH_MSGFILTER hook to intercept VK_DELETE and RMB during the clipboard menu. */
static LRESULT CALLBACK menu_msg_filter_proc(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (nCode == MSGF_MENU) {
		MSG *msg = (MSG *)lParam;
		if ((msg->message == WM_KEYDOWN || msg->message == WM_KEYUP) && msg->wParam == VK_DELETE) {
			if (msg->message == WM_KEYDOWN) {
				menu_delete_current_item();
			}
			return 1;
		}
		if (msg->message == WM_RBUTTONDOWN) {
			// Suppress default right-click tracking/selection
			return 1;
		}
		if (msg->message == WM_RBUTTONUP) {
			HWND hMenuWnd = WindowFromPoint(msg->pt);
			MENU_ITEM_INFO *mii = menu_get_item_from_point(msg->pt);
			if (mii == NULL && current_selected_mii != NULL) {
				mii = current_selected_mii;
			}
			if (mii != NULL) {
				if ((mii->is_favourites && (mii->is_folder || (mii->flag & MF_POPUP))) ||
					(mii->set_di != NULL && data_check(&history_data, mii->set_di) != NULL && mii->set_di->type == TYPE_FOLDER)) {
					HWND hTargetWnd = (hMenuWnd != NULL && IsWindow(hMenuWnd)) ? hMenuWnd :
						((menu_root_wnd != NULL && IsWindow(menu_root_wnd)) ? menu_root_wnd : FindWindow(TEXT("#32768"), NULL));
					if (hTargetWnd != NULL && IsWindow(hTargetWnd)) {
						HMENU hCurMenu = (HMENU)SendMessage(hTargetWnd, MN_GETHMENU, 0, 0);
						if (hCurMenu != NULL) {
							int idx = MenuItemFromPoint(hTargetWnd, hCurMenu, msg->pt);
							if (idx < 0 && mii->set_di != NULL) {
								idx = menu_find_data_index(hCurMenu, mii->set_di);
							}
							if (idx >= 0) {
								SendMessage(hTargetWnd, 0x01E5 /* MN_SELECTITEM */, (WPARAM)idx, 0);
								SendMessage(hTargetWnd, 0x01E3 /* MN_OPENHIERARCHY */, 0, 0);
							}
						}
					}
				}
				menu_context_clear_hits();
				if (!EnumThreadWindows(GetCurrentThreadId(), menu_context_capture_items, 0)) {
					menu_context_clear_hits();
				}
				menu_context_original = menu_context_highlight = mii;

				if (mii->is_favourites && (mii->is_folder || (mii->flag & MF_POPUP))) {
					// Right-click on Favourites root or a Favourites subfolder -> Folder context menu
					if (!has_reopen_pos) {
						HWND hRoot = (menu_root_wnd != NULL && IsWindow(menu_root_wnd)) ? menu_root_wnd : FindWindow(TEXT("#32768"), NULL);
						if (hRoot != NULL) {
							menu_record_reopen_position(hRoot);
						}
					}
					menu_reopen_folder_title[0] = TEXT('\0');
					menu_reopen_is_fav = (mii->set_di != &regist_data);
					menu_reopen_fav_folder = NULL;
					if (mii->set_di != NULL && mii->set_di != &regist_data) {
						DATA_INFO *parent_fld = data_check(&regist_data, mii->set_di);
						if (parent_fld != NULL && parent_fld != &regist_data && parent_fld->type == TYPE_FOLDER && parent_fld->title != NULL) {
							lstrcpyn(menu_reopen_folder_title, parent_fld->title, BUF_SIZE);
							menu_reopen_fav_folder = parent_fld;
						}
					}
					menu_ghost_wnd = menu_ghost_show();
					menu_rmb_action = RMB_ACTION_FAV_FOLDER_MENU;
					menu_rmb_target_di = mii->set_di != NULL ? mii->set_di : &regist_data;
					menu_rmb_pt = msg->pt;
					menu_reopen_requested = TRUE;
					menu_folder_hover_posted = FALSE;
					EndMenu();
					return 1;
				} else if (mii->set_di != NULL) {
					DATA_INFO *di = mii->set_di;
					if (di->type != TYPE_ITEM && di->type != TYPE_FOLDER) {
						DATA_INFO *parent = data_check(&history_data, di);
						if (parent == NULL) {
							parent = data_check(&regist_data, di);
						}
						if (parent != NULL && (parent->type == TYPE_ITEM || parent->type == TYPE_FOLDER)) {
							di = parent;
						}
					}
					if (data_check(&regist_data, di) != NULL && di->type == TYPE_ITEM) {
						// Right-click on an item within Favourites -> Delete context menu
						if (!has_reopen_pos) {
							HWND hRoot = (menu_root_wnd != NULL && IsWindow(menu_root_wnd)) ? menu_root_wnd : FindWindow(TEXT("#32768"), NULL);
							if (hRoot != NULL) {
								menu_record_reopen_position(hRoot);
							}
						}
						DATA_INFO *parent_fld = data_check(&regist_data, di);
						menu_reopen_folder_title[0] = TEXT('\0');
						menu_reopen_is_fav = TRUE;
						menu_reopen_fav_folder = NULL;
						if (parent_fld != NULL && parent_fld != &regist_data && parent_fld->type == TYPE_FOLDER && parent_fld->title != NULL) {
							lstrcpyn(menu_reopen_folder_title, parent_fld->title, BUF_SIZE);
							menu_reopen_fav_folder = parent_fld;
						}
						menu_ghost_wnd = menu_ghost_show();
						menu_rmb_action = RMB_ACTION_FAV_ITEM_MENU;
						menu_rmb_target_di = di;
						menu_rmb_pt = msg->pt;
						menu_reopen_requested = TRUE;
						menu_folder_hover_posted = FALSE;
						EndMenu();
						return 1;
					} else if (data_check(&history_data, di) != NULL && di->type == TYPE_ITEM) {
						// Right-click on a Clipboard history item -> Add to Favorites
						if (!has_reopen_pos) {
							HWND hRoot = (menu_root_wnd != NULL && IsWindow(menu_root_wnd)) ? menu_root_wnd : FindWindow(TEXT("#32768"), NULL);
							if (hRoot != NULL) {
								menu_record_reopen_position(hRoot);
							}
						}
						DATA_INFO *parent_fld = data_check(&history_data, di);
						menu_reopen_folder_title[0] = TEXT('\0');
						menu_reopen_is_fav = FALSE;
						menu_reopen_fav_folder = NULL;
						if (parent_fld != NULL && parent_fld != &history_data && parent_fld->type == TYPE_FOLDER && parent_fld->title != NULL) {
							lstrcpyn(menu_reopen_folder_title, parent_fld->title, BUF_SIZE);
						}
						menu_ghost_wnd = menu_ghost_show();
						menu_rmb_action = RMB_ACTION_ADD_TO_FAVORITES;
						menu_rmb_target_di = di;
						menu_rmb_pt = msg->pt;
						menu_reopen_requested = TRUE;
						menu_folder_hover_posted = FALSE;
						EndMenu();
						return 1;
					} else if (data_check(&history_data, di) != NULL && di->type == TYPE_FOLDER) {
						// Right-click on a Date Folder in history: Date folder context menu
						if (!has_reopen_pos) {
							HWND hRoot = (menu_root_wnd != NULL && IsWindow(menu_root_wnd)) ? menu_root_wnd : FindWindow(TEXT("#32768"), NULL);
							if (hRoot != NULL) {
								menu_record_reopen_position(hRoot);
							}
						}
						menu_reopen_folder_title[0] = TEXT('\0');
						menu_reopen_is_fav = FALSE;
						menu_reopen_fav_folder = NULL;
						menu_ghost_wnd = menu_ghost_show();
						menu_rmb_action = RMB_ACTION_DATE_FOLDER_MENU;
						menu_rmb_target_di = di;
						menu_rmb_pt = msg->pt;
						menu_reopen_requested = TRUE;
						menu_folder_hover_posted = FALSE;
						EndMenu();
						return 1;
					}
				}
			}
			if (hMenuWnd != NULL) {
				TCHAR cls[32] = {0};
				GetClassName(hMenuWnd, cls, sizeof(cls) / sizeof(TCHAR));
				if (lstrcmp(cls, TEXT("#32768")) == 0) {
					// An unhandled right-click must not leave the menu ignoring input.
					EndMenu();
					return 1;
				}
			}
		}
	}
	return CallNextHookEx(menu_filter_hook, nCode, wParam, lParam);
}

/*
 * menu_submenu_subclass_proc - subclass procedure for submenu (#32768) windows
 */
static LRESULT CALLBACK menu_submenu_subclass_proc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
	UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	if (uMsg == WM_WINDOWPOSCHANGING) {
		WINDOWPOS *wp = (WINDOWPOS *)lParam;
		if (active_submenu_pending && !(wp->flags & SWP_NOMOVE)) {
			int w = wp->cx;
			if (w <= 0) {
				RECT rc;
				GetWindowRect(hWnd, &rc);
				w = rc.right - rc.left;
			}
			if (w > 0) {
				HMONITOR hMonitor;
				MONITORINFO mi;
				RECT work_rect;
				int overlap;
				int new_x;

				hMonitor = MonitorFromRect(&active_submenu_item_rect, MONITOR_DEFAULTTONEAREST);
				mi.cbSize = sizeof(MONITORINFO);
				if (hMonitor != NULL && GetMonitorInfo(hMonitor, &mi) != FALSE) {
					work_rect = mi.rcWork;
				} else {
					SystemParametersInfo(SPI_GETWORKAREA, 0, &work_rect, 0);
				}

				overlap = GetSystemMetrics(SM_CXEDGE);
				if (active_submenu_item_rect.right + w <= work_rect.right) {
					// Submenu fits on the right side of the folder column
					new_x = active_submenu_item_rect.right - overlap;
				} else {
					// Does not fit on the right: place directly to the left of the folder column
					new_x = active_submenu_item_rect.left - w + overlap;
					if (new_x < work_rect.left) {
						new_x = work_rect.left;
					}
				}
				wp->x = new_x;
			}
		}

	} else if (uMsg == WM_NCDESTROY) {
		RemoveWindowSubclass(hWnd, menu_submenu_subclass_proc, uIdSubclass);
	}
	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

/*
 * menu_cbt_proc - WH_CBT hook to detect submenu window creation and subclass it
 */
static LRESULT CALLBACK menu_cbt_proc(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (nCode == HCBT_CREATEWND) {
		HWND hwnd = (HWND)wParam;
		CBT_CREATEWND *create = (CBT_CREATEWND *)lParam;
		BOOL is_menu = FALSE;

		if (create != NULL && create->lpcs != NULL) {
			if (IS_INTRESOURCE(create->lpcs->lpszClass)) {
				if ((WORD)(ULONG_PTR)create->lpcs->lpszClass == 0x8000) {
					is_menu = TRUE;
				}
			} else if (create->lpcs->lpszClass != NULL) {
				if (lstrcmp(create->lpcs->lpszClass, TEXT("#32768")) == 0) {
					is_menu = TRUE;
				}
			}
		}
		if (!is_menu) {
			TCHAR cls[32] = {0};
			if (GetClassName(hwnd, cls, sizeof(cls) / sizeof(TCHAR)) > 0) {
				if (lstrcmp(cls, TEXT("#32768")) == 0) {
					is_menu = TRUE;
				}
			}
		}

		if (is_menu) {
			if (menu_root_wnd == NULL) {
				menu_root_wnd = hwnd;
			} else if (hwnd != menu_root_wnd) {
				SetWindowSubclass(hwnd, menu_submenu_subclass_proc, SUBCLASS_ID_MENU_SUBMENU, 0);
			}
		}
	} else if (nCode == HCBT_DESTROYWND) {
		HWND hwnd = (HWND)wParam;
		if (hwnd == menu_root_wnd) {
			menu_root_wnd = NULL;
			active_submenu_pending = FALSE;
		}
	}
	return CallNextHookEx(menu_cbt_hook, nCode, wParam, lParam);
}

/*
 * tray_message - set task tray icon
 */
static BOOL tray_message(const HWND hWnd, const DWORD dwMessage, const UINT uID, const HICON hIcon, const TCHAR *pszTip)
{
	NOTIFYICONDATA tnd;

	tnd.cbSize = sizeof(NOTIFYICONDATA);
	tnd.hWnd = hWnd;
	tnd.uID	= uID;
	tnd.uFlags = NIF_MESSAGE | NIF_ICON;
	tnd.uCallbackMessage = WM_TRAY_NOTIFY;
	tnd.hIcon = hIcon;
	if (pszTip != NULL && *pszTip != TEXT('\0')) {
		tnd.uFlags |= NIF_TIP;
		lstrcpyn(tnd.szTip, pszTip, 64 - 1);
	} else {
		*tnd.szTip = TEXT('\0');
	}
	return Shell_NotifyIcon(dwMessage, &tnd);
}

/*
 * set_tray_icon - set icon in task tray
 */
static void set_tray_icon(const HWND hWnd, const HICON hIcon, const TCHAR *buf)
{
	if (hIcon == NULL || option.main_show_trayicon == 0) {
		return;
	}
	if (tray_message(hWnd, NIM_MODIFY, TRAY_ID, hIcon, buf) == FALSE) {
		// If modification failed, add instead
		int i;
		for (i = 0; i < 5; i++) {
			// Retry if adding failed
			if (tray_message(hWnd, NIM_ADD, TRAY_ID, hIcon, buf)) {
				break;
			}
			Sleep(5000);
		}
	}
}

/*
 * tray_cancel_preview - cancel/hide task tray preview
 */
static void tray_cancel_preview(const HWND hWnd)
{
	tray_last_click_tick = GetTickCount();
	if (tray_preview_showing) {
		tooltip_hide(hToolTip);
		tray_preview_showing = FALSE;
	}
	tray_hover_active = FALSE;
	KillTimer(hWnd, ID_TRAY_HOVER_TIMER);
}

/*
 * set_tray_tooltip - set task tray tooltip
 */
static void set_tray_tooltip(const HWND hWnd)
{
	if (option.main_show_trayicon == 0) {
		return;
	}
	// Disable native plain text tooltip and delegate to rich preview on 1-second hover
	set_tray_icon(hWnd, icon_tray, TEXT(""));
}

/*
 * show_data_tooltip - display preview tooltip for generic data item
 */
static BOOL show_data_tooltip(const HWND tooltip_wnd, DATA_INFO *item_di, const POINT *cur_pt, const RECT *custom_anchor, const int delay, const HWND hover_wnd, const RECT *hover_rect)
{
	DATA_INFO *di;
	DATA_INFO *cur;
	DATA_INFO *bmp_di = NULL;
	TCHAR *buf = NULL;
	HBITMAP hbmp = NULL;
	BOOL free_bmp = FALSE;

	if (item_di == NULL) {
		tooltip_hide(tooltip_wnd);
		return FALSE;
	}
	if (db_history_is_open()) {
		db_history_ensure_item_data(item_di);
	}
	di = (item_di->type == TYPE_ITEM) ? format_get_priority_highest(item_di) : item_di;
	if (di == NULL) {
		tooltip_hide(tooltip_wnd);
		return FALSE;
	}
	if (db_history_is_open() && di->data == NULL) {
		db_history_ensure_item_data(di);
	}

	buf = format_get_tooltip_text(di);

	// Get bitmap for image preview
	if (di->format_name != NULL &&
		(lstrcmpi(di->format_name, TEXT("BITMAP")) == 0 ||
		 lstrcmpi(di->format_name, TEXT("DIB")) == 0)) {
		bmp_di = di;
	} else if (item_di->type == TYPE_ITEM && item_di->child != NULL) {
		for (cur = item_di->child; cur != NULL; cur = cur->next) {
			if (cur->format_name != NULL &&
				(lstrcmpi(cur->format_name, TEXT("BITMAP")) == 0 ||
				 lstrcmpi(cur->format_name, TEXT("DIB")) == 0)) {
				bmp_di = cur;
				break;
			}
		}
	}

	if (bmp_di != NULL && bmp_di->data != NULL) {
		if (lstrcmpi(bmp_di->format_name, TEXT("BITMAP")) == 0) {
			hbmp = (HBITMAP)bmp_di->data;
			free_bmp = FALSE;
		} else {
			BYTE *mem = (BYTE *)GlobalLock(bmp_di->data);
			if (mem != NULL) {
				hbmp = dib_to_bitmap(mem);
				GlobalUnlock(bmp_di->data);
				free_bmp = TRUE;
			}
		}
	}

	if (buf == NULL && hbmp == NULL) {
		tooltip_hide(tooltip_wnd);
		return FALSE;
	}

	RECT anchor_rect;
	if (custom_anchor != NULL) {
		anchor_rect = *custom_anchor;
	} else if (cur_pt != NULL) {
		anchor_rect.left = cur_pt->x - Scale(2);
		anchor_rect.right = cur_pt->x + Scale(18);
		anchor_rect.top = cur_pt->y;
		anchor_rect.bottom = cur_pt->y + Scale(18);
	} else {
		SetRectEmpty(&anchor_rect);
	}

	POINT pt = (cur_pt != NULL) ? *cur_pt : (POINT){0, 0};
	tooltip_show_image_delay(tooltip_wnd, buf, hbmp, free_bmp, pt.x, pt.y, 0,
		&anchor_rect, delay, hover_wnd, hover_rect);
	if (buf != NULL) {
		mem_free(&buf);
	}
	return TRUE;
}

/*
 * show_menu_tooltip - display menu tooltip
 */
static BOOL show_menu_tooltip(const HWND tooltip_wnd, const HMENU hMenu, const UINT id, const BOOL mouse)
{
	MENU_ITEM_INFO *mii;

	// Get menu information from ID
	mii = menu_get_info(id);
	if (mii == NULL || mii->set_di == NULL) {
		tooltip_hide(tooltip_wnd);
		return FALSE;
	}

	RECT anchor_rect;
	RECT hover_rect = {0};
	POINT cur_pt;
	HWND hover_wnd = NULL;
	GetCursorPos(&cur_pt);
	if (mouse) {
		TCHAR cls[32] = {0};
		MENUITEMINFO info = {0};
		int index;
		hover_wnd = WindowFromPoint(cur_pt);
		if (hover_wnd == NULL || !GetClassName(hover_wnd, cls, 32) ||
			lstrcmp(cls, TEXT("#32768")) != 0 ||
			(HMENU)SendMessage(hover_wnd, MN_GETHMENU, 0, 0) != hMenu ||
			(index = MenuItemFromPoint(hover_wnd, hMenu, cur_pt)) < 0) {
			tooltip_hide(tooltip_wnd);
			return FALSE;
		}
		info.cbSize = sizeof(info);
		info.fMask = MIIM_DATA;
		if (!GetMenuItemInfo(hMenu, index, TRUE, &info) ||
			(MENU_ITEM_INFO *)info.dwItemData != mii ||
			!GetMenuItemRect(hover_wnd, hMenu, index, &hover_rect)) {
			tooltip_hide(tooltip_wnd);
			return FALSE;
		}
	}

	if (mouse == TRUE || PtInRect(&menu_sel_rect, cur_pt)) {
		// On mouse operation: use cursor position directly as anchor (display right next to cursor)
		anchor_rect.left = cur_pt.x - Scale(2);
		anchor_rect.right = cur_pt.x + Scale(18);
		anchor_rect.top = cur_pt.y;
		anchor_rect.bottom = cur_pt.y + Scale(18);
	} else if (menu_sel_rect.right > menu_sel_rect.left) {
		// On keyboard operation: use selected menu item as anchor
		anchor_rect = menu_sel_rect;
	} else {
		anchor_rect.left = cur_pt.x - Scale(2);
		anchor_rect.right = cur_pt.x + Scale(18);
		anchor_rect.top = cur_pt.y;
		anchor_rect.bottom = cur_pt.y + Scale(18);
	}

	return show_data_tooltip(tooltip_wnd, mii->set_di, &cur_pt, &anchor_rect, -1,
		hover_wnd, mouse ? &hover_rect : NULL);
}

/*
 * show_tool_menu - display tool menu
 */
static BOOL show_tool_menu(const HWND hWnd, DATA_INFO *di, const int paste, const HWND attach_wnd)
{
	MENU_ITEM_INFO *mii;
	MENU_INFO mi;
	DWORD tick;
	int ret;
	BOOL attached;
	BOOL shift_key;

	if (popup_menu != NULL) {
		return FALSE;
	}
	// Adjust to DPI of display monitor
	menu_set_dpi(NULL);
	// Create menu
	ZeroMemory(&mi, sizeof(MENU_INFO));
	mi.content = MENU_CONTENT_TOOL;
	popup_menu = menu_create(hWnd, &mi, 1, NULL, NULL);
	if (popup_menu == NULL) {
		menu_free();
		return FALSE;
	}
	if (GetMenuItemCount(popup_menu) == 0) {
		menu_destory(popup_menu);
		popup_menu = NULL;
		menu_free();
		return FALSE;
	}
	// Show menu
	attached = menu_attach_begin(hWnd, attach_wnd, (attach_wnd != NULL) ? TRUE : FALSE);
	if (attached == FALSE) {
		_SetForegroundWindow(hWnd);
	}
	menu_root_wnd = NULL;
	active_submenu_pending = FALSE;
	menu_cbt_hook = SetWindowsHookEx(WH_CBT, menu_cbt_proc, NULL, GetCurrentThreadId());
	tick = GetTickCount();
	ret = menu_show(hWnd, popup_menu, NULL);
	if (attached == TRUE && ret == 0 && GetTickCount() - tick < MENU_ATTACH_RETRY_TIME) {
		// If menu closed immediately after display due to active window input state
		// If closed, detach and redisplay using legacy method
		if (menu_cbt_hook != NULL) {
			UnhookWindowsHookEx(menu_cbt_hook);
			menu_cbt_hook = NULL;
		}
		menu_attach_end();
		attached = FALSE;
		_SetForegroundWindow(hWnd);
		menu_root_wnd = NULL;
		active_submenu_pending = FALSE;
		menu_cbt_hook = SetWindowsHookEx(WH_CBT, menu_cbt_proc, NULL, GetCurrentThreadId());
		ret = menu_show(hWnd, popup_menu, NULL);
	}
	if (menu_cbt_hook != NULL) {
		UnhookWindowsHookEx(menu_cbt_hook);
		menu_cbt_hook = NULL;
	}
	menu_root_wnd = NULL;
	active_submenu_pending = FALSE;
	// Get key states while input queue is still attached
	shift_key = (GetKeyState(VK_SHIFT) < 0) ? TRUE : FALSE;
	menu_attach_end();
	menu_destory(popup_menu);
	popup_menu = NULL;

	mii = menu_get_info(ret);
	if (ret <= 0 || ret == IDCANCEL || mii == NULL) {
		menu_free();
		return FALSE;
	}
	if (mii->ti->copy_paste == 1) {
		// Send to clipboard then execute tool
		tmi.enable = TRUE;
		tmi.ti = mii->ti;
		tmi.paste = (shift_key == FALSE) ? paste : 0;
		menu_free();
		return TRUE;
	}
	// Execute tool
	if (tool_execute(hWnd, mii->ti, CALLTYPE_MENU, di, NULL) & TOOL_DATA_MODIFIED) {
		if (data_check(&history_data, di) != NULL) {
			SendMessage(hWnd, WM_HISTORY_CHANGED, 0, 0);
		} else if (data_check(&regist_data, di) != NULL) {
			SendMessage(hWnd, WM_REGIST_CHANGED, 0, 0);
		}
	}
	menu_free();
	return FALSE;
}

/*
 * show_popup_menu - display popup menu
 */
static BOOL show_popup_menu(const HWND hWnd, const ACTION_INFO *ai, const BOOL caret, const BOOL attach)
{
	MENU_ITEM_INFO *mii;
	FOCUS_INFO fi;
	DWORD tick;
	int ret;
	BOOL caret_flag = caret;
	BOOL attached;
	BOOL shift_key, ctrl_key;
	BOOL context_lmb_selected = FALSE;
	DATA_INFO *edit_target = NULL;

	if (popup_menu != NULL) {
		// Popup menu is showing
		if (menu_attach_tid == 0) {
			_SetForegroundWindow(hWnd);
		}
		return FALSE;
	}
	if (attach == TRUE && option.menu_attach_process == 1) {
		// Send mask key before creating menu (in time before Alt is released)
		menu_mask_modifier_key();
	}
	CopyMemory(&fi, &focus_info, sizeof(FOCUS_INFO));
	if (caret == TRUE || fi.active_wnd == NULL) {
		// Get focus information
		get_focus_info(&fi, (ai->caret != 0) ? caret : FALSE);
	}
	if (ai->caret == 0 || fi.caret == FALSE) {
		caret_flag = FALSE;
	}

	// Key initialization
	GetAsyncKeyState(VK_RBUTTON);

	has_reopen_pos = FALSE;
	menu_reopen_align = TPM_TOPALIGN | TPM_LEFTALIGN;
	menu_reopen_fav_folder = NULL;
	menu_reopen_fav_ancestor = NULL;
	menu_context_reopen_target = NULL;
	SetRectEmpty(&favourites_menu_item_rect);

	for (;;) {
		const POINT *show_pos = (has_reopen_pos == TRUE) ? &menu_reopen_pos : ((caret_flag == TRUE) ? &fi.cpos : NULL);
		const UINT show_align = ((has_reopen_pos == TRUE) ? menu_reopen_align : (TPM_TOPALIGN | TPM_LEFTALIGN)) |
			(menu_context_reopen_target != NULL ? TPM_NOANIMATION : 0);

		// Adjust to DPI of display monitor
		menu_set_dpi(show_pos);
		// Create menu
		popup_menu = menu_create(hWnd, ai->menu_info, ai->menu_cnt, history_data.child, regist_data.child);
		if (popup_menu == NULL) {
			menu_free();
			KillTimer(hWnd, ID_MENU_HOVER_SAFETY_TIMER);
			menu_ghost_hide();
			has_reopen_pos = FALSE;
			menu_reopen_align = TPM_TOPALIGN | TPM_LEFTALIGN;
			return FALSE;
		}
		if (GetMenuItemCount(popup_menu) == 0) {
			menu_destory(popup_menu);
			popup_menu = NULL;
			menu_free();
			KillTimer(hWnd, ID_MENU_HOVER_SAFETY_TIMER);
			menu_ghost_hide();
			has_reopen_pos = FALSE;
			menu_reopen_align = TPM_TOPALIGN | TPM_LEFTALIGN;
			return FALSE;
		}

		// A clipboard row can be flattened onto the root despite having a date parent.
		if (menu_context_reopen_target != NULL && menu_context_reopen_target->type != TYPE_FOLDER &&
			menu_context_reopen_target != &regist_data &&
			menu_find_data_index(popup_menu, menu_context_reopen_target) >= 0) {
			menu_reopen_folder_title[0] = TEXT('\0');
			menu_reopen_is_fav = FALSE;
			menu_reopen_fav_folder = NULL;
		}
		// Show menu
		attached = menu_attach_begin(hWnd, fi.active_wnd, attach);
		if (attached == FALSE) {
			_SetForegroundWindow(hWnd);
			ShowWindow(hWnd, SW_HIDE);
		}
		search_mode_requested = FALSE;
		menu_delete_requested = FALSE;
		menu_root_wnd = NULL;
		active_submenu_pending = FALSE;
		menu_filter_hook = SetWindowsHookEx(WH_MSGFILTER, menu_msg_filter_proc, NULL, GetCurrentThreadId());
		menu_cbt_hook = SetWindowsHookEx(WH_CBT, menu_cbt_proc, NULL, GetCurrentThreadId());

		tick = GetTickCount();
		ret = menu_show_align(hWnd, popup_menu, show_pos, show_align);
		if (attached == TRUE && ret == 0 && !menu_delete_requested && !menu_reopen_requested && GetTickCount() - tick < MENU_ATTACH_RETRY_TIME) {
			// If menu closed immediately after display due to active window input state
			// If closed, detach and redisplay using legacy method
			if (menu_cbt_hook != NULL) {
				UnhookWindowsHookEx(menu_cbt_hook);
				menu_cbt_hook = NULL;
			}
			if (menu_filter_hook != NULL) {
				UnhookWindowsHookEx(menu_filter_hook);
				menu_filter_hook = NULL;
			}
			menu_attach_end();
			attached = FALSE;
			_SetForegroundWindow(hWnd);
			ShowWindow(hWnd, SW_HIDE);
			search_mode_requested = FALSE;
			menu_delete_requested = FALSE;
			menu_root_wnd = NULL;
			active_submenu_pending = FALSE;
			menu_filter_hook = SetWindowsHookEx(WH_MSGFILTER, menu_msg_filter_proc, NULL, GetCurrentThreadId());
			menu_cbt_hook = SetWindowsHookEx(WH_CBT, menu_cbt_proc, NULL, GetCurrentThreadId());
			ret = menu_show_align(hWnd, popup_menu, show_pos, show_align);
		}
		if (menu_cbt_hook != NULL) {
			UnhookWindowsHookEx(menu_cbt_hook);
			menu_cbt_hook = NULL;
		}
		if (menu_filter_hook != NULL) {
			UnhookWindowsHookEx(menu_filter_hook);
			menu_filter_hook = NULL;
		}
		menu_root_wnd = NULL;
		active_submenu_pending = FALSE;
		// Get key states while input queue is still attached
		shift_key = (GetKeyState(VK_SHIFT) < 0) ? TRUE : FALSE;
		ctrl_key = (GetKeyState(VK_CONTROL) < 0) ? TRUE : FALSE;
		menu_attach_end();
		menu_destory(popup_menu);
		popup_menu = NULL;
		if (menu_delete_target != NULL) {
			menu_delete_pending_item(hWnd);
		}

		if (menu_rmb_action == RMB_ACTION_ADD_TO_FAVORITES ||
			menu_rmb_action == RMB_ACTION_DATE_FOLDER_MENU ||
			menu_rmb_action == RMB_ACTION_FAV_FOLDER_MENU ||
			menu_rmb_action == RMB_ACTION_FAV_ITEM_MENU) {
			DATA_INFO *target = menu_rmb_target_di;
			DATA_INFO *parent_folder = NULL;
			POINT pt = menu_rmb_pt;
			BOOL deleted = FALSE;
			BOOL edit = FALSE;
			BOOL is_target_fav = FALSE;
			menu_rmb_action = RMB_ACTION_NONE;
			menu_rmb_target_di = NULL;
			for (;;) {
				HHOOK context_hook;
				parent_folder = data_check(&history_data, target);
				if (parent_folder == NULL) {
					parent_folder = (target == &regist_data) ? &regist_data : data_check(&regist_data, target);
					is_target_fav = TRUE;
				} else {
					is_target_fav = FALSE;
				}
				menu_reopen_folder_title[0] = TEXT('\0');
				// A direct RMB on the root stays on its row; a switched context
				// keeps the already-open Favourites hierarchy.
				menu_reopen_is_fav = is_target_fav && (target != &regist_data || menu_reopen_is_fav);
				menu_reopen_fav_folder = (is_target_fav && parent_folder != &regist_data) ? parent_folder : NULL;
				if (!is_target_fav && target != NULL && target->type == TYPE_FOLDER && target->title != NULL) {
					lstrcpyn(menu_reopen_folder_title, target->title, BUF_SIZE);
				} else if (parent_folder != NULL && parent_folder != &history_data && parent_folder != &regist_data && parent_folder->title != NULL) {
					lstrcpyn(menu_reopen_folder_title, parent_folder->title, BUF_SIZE);
				}
				menu_context_pressed = menu_context_pick = NULL;
				context_hook = SetWindowsHookEx(WH_MSGFILTER, menu_context_filter_proc, NULL, GetCurrentThreadId());

				if (!is_target_fav) {
					if (target != NULL && target->type == TYPE_FOLDER) {
						history_show_folder_menu(hWnd, target, pt, &deleted);
					} else {
						favorites_show_add_menu(hWnd, target, pt, &deleted, &edit);
					}
				} else {
					if (target == &regist_data || (target != NULL && target->type == TYPE_FOLDER)) {
						favorites_show_folder_menu(hWnd, target, pt, &deleted);
					} else {
						favorites_show_item_menu(hWnd, target, pt, &deleted, &edit);
					}
				}

				if (context_hook != NULL) {
					UnhookWindowsHookEx(context_hook);
				}
				if (edit) { edit_target = target; break; }
				if (menu_context_pick == NULL || menu_context_left) {
					break;
				}
				target = menu_context_pick->set_di;
				pt = menu_rmb_pt;
				if (target == NULL && menu_context_pick->is_favourites) {
					target = &regist_data;
				}
				if (target != NULL) {
					menu_context_reopen_target = target;
					break;
				}
			}
			menu_context_clear_hits();
			if (edit_target != NULL) {
				menu_reopen_requested = menu_cursor_restore_needed = FALSE;
				has_reopen_pos = FALSE;
				menu_reopen_folder_title[0] = TEXT('\0');
				menu_free();
				break;
			}
			if (menu_context_pick != NULL && (menu_context_left || menu_context_reopen_target != NULL)) {
				BOOL is_folder = menu_context_pick->is_folder ||
					(menu_context_pick->flag & MF_POPUP) ||
					(menu_context_pick->set_di != NULL && menu_context_pick->set_di->type == TYPE_FOLDER) ||
					menu_context_pick->set_di == &regist_data;

				if (is_folder || menu_context_reopen_target != NULL) {
					DATA_INFO *target_folder = menu_context_reopen_target != NULL ?
						menu_context_reopen_target : menu_context_pick->set_di;
					BOOL is_fav = (target_folder == &regist_data) || menu_context_pick->is_favourites;
					if (target_folder != NULL && !is_fav) {
						if (data_check(&regist_data, target_folder) != NULL) {
							is_fav = TRUE;
						}
					}
					if (menu_context_reopen_target != NULL &&
						((target_folder->type != TYPE_FOLDER && target_folder != &regist_data) || target_folder->child == NULL)) {
						target_folder = data_check(is_fav ? &regist_data : &history_data, target_folder);
						while (target_folder != NULL && target_folder->type != TYPE_FOLDER && target_folder->type != TYPE_ROOT) {
							target_folder = data_check(is_fav ? &regist_data : &history_data, target_folder);
						}
					}
					menu_reopen_folder_title[0] = TEXT('\0');
					menu_reopen_is_fav = is_fav && target_folder != NULL;
					menu_reopen_fav_folder = NULL;
					if (is_fav) {
						if (target_folder != NULL && target_folder != &regist_data && target_folder->type == TYPE_FOLDER && target_folder->title != NULL) {
							lstrcpyn(menu_reopen_folder_title, target_folder->title, BUF_SIZE);
							menu_reopen_fav_folder = target_folder;
						}
					} else {
						if (target_folder != NULL && target_folder->type == TYPE_FOLDER && target_folder->title != NULL) {
							lstrcpyn(menu_reopen_folder_title, target_folder->title, BUF_SIZE);
						} else if (menu_context_reopen_target == NULL && menu_context_pick->text != NULL) {
							lstrcpyn(menu_reopen_folder_title, menu_context_pick->text, BUF_SIZE);
						}
					}
					menu_context_pick = NULL;
					menu_cursor_restore_pt = pt;
					menu_cursor_restore_needed = TRUE;
					menu_reopen_requested = FALSE;
					menu_folder_hover_posted = FALSE;
					SetTimer(hWnd, ID_MENU_HOVER_SAFETY_TIMER, 800, NULL);
					menu_free();
					continue;
				}
				// Use the ordinary item-selection path, including Shift/Ctrl and paste.
				ret = menu_context_pick->id;
				menu_context_pick = NULL;
				shift_key = GetKeyState(VK_SHIFT) < 0;
				ctrl_key = GetKeyState(VK_CONTROL) < 0;
				attached = FALSE;
				menu_reopen_requested = menu_cursor_restore_needed = FALSE;
				menu_reopen_folder_title[0] = TEXT('\0');
				has_reopen_pos = FALSE;
				context_lmb_selected = TRUE;
				GetAsyncKeyState(VK_RBUTTON);
				break;
			}
			menu_cursor_restore_pt = pt;
			if (deleted) {
				if (!is_target_fav) {
					if (parent_folder != NULL && data_check(&history_data, parent_folder) == NULL) {
						menu_reopen_folder_title[0] = TEXT('\0');
						if (active_submenu_item_rect.right > active_submenu_item_rect.left) {
							menu_cursor_restore_pt.x = active_submenu_item_rect.left + (active_submenu_item_rect.right - active_submenu_item_rect.left) / 2;
							menu_cursor_restore_pt.y = active_submenu_item_rect.top + (active_submenu_item_rect.bottom - active_submenu_item_rect.top) / 2;
						}
					} else if (menu_prune_empty_parent(&history_data, parent_folder, FALSE)) {
						SendMessage(hWnd, WM_HISTORY_CHANGED, 0, 0);
					}
				} else {
					if (menu_prune_empty_parent(&regist_data, parent_folder, TRUE)) {
						SendMessage(hWnd, WM_REGIST_CHANGED, 0, 0);
					}
				}
			}
			menu_cursor_restore_needed = TRUE;
			menu_reopen_requested = FALSE;
			menu_folder_hover_posted = FALSE;
			SetTimer(hWnd, ID_MENU_HOVER_SAFETY_TIMER, 800, NULL);
			menu_free();
			continue;
		}

		if (menu_delete_requested || menu_reopen_requested) {
			menu_delete_requested = FALSE;
			menu_reopen_requested = FALSE;
			menu_folder_hover_posted = FALSE;
			SetTimer(hWnd, ID_MENU_HOVER_SAFETY_TIMER, 800, NULL);
			menu_free();
			continue;
		}
		has_reopen_pos = FALSE;
		menu_reopen_align = TPM_TOPALIGN | TPM_LEFTALIGN;
		break;
	}
	menu_ghost_hide();
	if (edit_target != NULL) {
		if (pinned_image_open_from_menu(hWnd, edit_target) == NULL) MessageBeep(MB_ICONWARNING);
		return TRUE;
	}

	if (search_mode_requested) {
		DATA_INFO *search_di;
		search_mode_requested = FALSE;
		menu_free();
		search_di = search_popup_show(hWnd, history_data.child);
		if (search_di != NULL) {
			set_focus_info(&fi);
			SendMessage(hWnd, WM_ITEM_TO_CLIPBOARD, 0, (LPARAM)search_di);
			if (data_check(&history_data, search_di) == NULL &&
				data_check(&regist_data, search_di) == NULL) {
				data_free(search_di);
			}
			if (ai->paste == 1 && GetKeyState(VK_SHIFT) >= 0) {
				key_wait();
				unregist_hotkey(hWnd);
				sendkey_paste(fi.active_wnd);
				regist_hotkey(hWnd, FALSE);
			}
		} else if (GetForegroundWindow() == hWnd) {
			set_focus_info(&fi);
		}
		return TRUE;
	}

	mii = menu_get_info(ret);
	if (ret <= 0 || ret == IDCANCEL || mii == NULL) {
		// Cancel
		if (attached == FALSE && GetForegroundWindow() == hWnd) {
			set_focus_info(&fi);
		}

	} else if (mii->set_di != NULL) {
		// Item
		if (!context_lmb_selected &&
			(GetAsyncKeyState(VK_RBUTTON) == 1 || ctrl_key == TRUE) &&
			option.menu_show_tool_menu == 1) {
			DATA_INFO *di = mii->set_di;
			BOOL tool_ret;
			menu_free();
			// Show tool menu
			tool_ret = show_tool_menu(hWnd, di, ai->paste, (attached == TRUE) ? fi.active_wnd : NULL);
			if (attached == FALSE) {
				set_focus_info(&fi);
			}
			if (tool_ret == TRUE) {
				SendMessage(hWnd, WM_ITEM_TO_CLIPBOARD, 0, (LPARAM)di);
			}
			return TRUE;
		}
		// Set data to clipboard
		if (attached == FALSE) {
			set_focus_info(&fi);
		}
		SendMessage(hWnd, WM_ITEM_TO_CLIPBOARD, 0, (LPARAM)mii->set_di);
		if (ai->paste == 1 && shift_key == FALSE) {
			// Wait until key is released
			key_wait();
			// Unregister hotkey
			unregist_hotkey(hWnd);
			// Paste
			sendkey_paste(fi.active_wnd);
			// Register hotkey
			regist_hotkey(hWnd, FALSE);
		}

	} else if (mii->ti != NULL) {
		// Tool
		if (attached == FALSE) {
			set_focus_info(&fi);
		}
		if (mii->ti->copy_paste == 1) {
			tmi.enable = TRUE;
			tmi.ti = mii->ti;
			tmi.paste = (shift_key == FALSE) ? ai->paste : 0;
			// Wait until key is released
			key_wait();
			SetTimer(hWnd, ID_TOOL_TIMER, option.tool_valid_interval, NULL);
			// Copy
			sendkey_copy(fi.active_wnd);
		} else {
			// Execute tool
			if (tool_execute(hWnd, mii->ti, CALLTYPE_MENU, history_data.child, NULL) & TOOL_DATA_MODIFIED) {
				SendMessage(hWnd, WM_HISTORY_CHANGED, 0, 0);
				SendMessage(hWnd, WM_ITEM_TO_CLIPBOARD, 0, (LPARAM)history_data.child);
			}
		}

	} else if (mii->mi != NULL) {
		// Launch application
		// launch the application
		TCHAR expanded_name[MAX_PATH + 1];
		TCHAR expanded_cmd[MAX_PATH + 1];
		DWORD ret = 0;
		// expand environment variables in file_name
		if (mii->mi->path != NULL && (ret = ExpandEnvironmentStrings(mii->mi->path, expanded_name, MAX_PATH)) > 0 
			&& ret <= MAX_PATH) 
		{
			// try expand environment variables in parameters
			if (mii->mi->cmd && (ret = ExpandEnvironmentStrings(mii->mi->cmd, expanded_cmd, MAX_PATH)) > 0 
				&& ret <= MAX_PATH) 
			{
				// launch the application with expanded path and parameters
				shell_open(expanded_name, expanded_cmd);
			} else {
				// launch the application with expanded path and unexpanded parameters
				shell_open(expanded_name, mii->mi->cmd);
			}
		}
		else {
				// launch the application with unexpanded path and parameters
			shell_open(mii->mi->path, mii->mi->cmd);
		}

	} else {
		// Command
		if (ret == ID_MENUITEM_NEW_SNIP || ret == ID_MENUITEM_SCROLLING_SNIP)
			PostMessage(hWnd, WM_COMMAND, ret, 0);
		else SendMessage(hWnd, WM_COMMAND, ret, 0);
	}
	// Free menu information
	menu_free();
	return TRUE;
}

/*
 * action_execute - execute action
 */
static BOOL action_execute(const HWND hWnd, const int type, const int id, const BOOL caret)
{
	DATA_INFO *di;
	int i;
	BOOL ret;

	ZeroMemory(&tmi, sizeof(TOOL_MENU_INFO));

	// Find action
	for (i = 0; i < option.action_cnt; i++) {
		if (type == (option.action_info + i)->type && (option.action_info + i)->enable != 0) {
			if (type == ACTION_TYPE_HOTKEY && id != (option.action_info + i)->id) {
				continue;
			}
			break;
		}
	}
	// Find tool
	if (i >= option.action_cnt && type == ACTION_TYPE_HOTKEY) {
		for (i = 0; i < option.tool_cnt; i++) {
			if (id != (option.tool_info + i)->id) {
				continue;
			}
			if ((option.tool_info + i)->copy_paste == 1) {
				tmi.enable = TRUE;
				tmi.ti = option.tool_info + i;
				tmi.paste = 1;
				// Wait until key is released
				key_wait();
				SetTimer(hWnd, ID_TOOL_TIMER, option.tool_valid_interval, NULL);
				// Copy
				sendkey_copy(GetForegroundWindow());
			} else {
				// Execute tool
				if (tool_execute(hWnd, option.tool_info + i, CALLTYPE_MENU, history_data.child, NULL) & TOOL_DATA_MODIFIED) {
					SendMessage(hWnd, WM_HISTORY_CHANGED, 0, 0);
					SendMessage(hWnd, WM_ITEM_TO_CLIPBOARD, 0, (LPARAM)history_data.child);
				}
			}
			return TRUE;
		}
		if (i >= option.tool_cnt) {
			// Directly paste registered items
			di = regist_hotkey_to_item(regist_data.child, id);
			if (di != NULL) {
				paste_di = di;
				SetTimer(hWnd, ID_PASTE_TIMER, 1, NULL);
			}
		}
		return TRUE;
	}
	if (i >= option.action_cnt) {
		return TRUE;
	}

	// Execute action
	switch ((option.action_info + i)->action) {
	case ACTION_POPUPMEMU:
		// Popup menu
		// Only attach to active process when displayed via hotkey
		ret = show_popup_menu(hWnd, option.action_info + i, caret,
			(type == ACTION_TYPE_HOTKEY || type == ACTION_TYPE_CTRL_CTRL ||
			type == ACTION_TYPE_SHIFT_SHIFT || type == ACTION_TYPE_ALT_ALT) ? TRUE : FALSE);
		ZeroMemory(&focus_info, sizeof(FOCUS_INFO));
		return ret;

	case ACTION_VIEWER:
		// Show viewer
		SendMessage(hWnd, WM_COMMAND, ID_MENUITEM_VIEWER, 0);
		break;

	case ACTION_NEW_SNIP:
		PostMessage(hWnd, WM_COMMAND, ID_MENUITEM_NEW_SNIP, 0);
		break;

	case ACTION_OPTION:
		// Options
		SendMessage(hWnd, WM_COMMAND, ID_MENUITEM_OPTION, 0);
		break;

	case ACTION_CLIPBOARD_WATCH:
		// Toggle clipboard monitoring
		SendMessage(hWnd, WM_COMMAND, ID_MENUITEM_CLIPBOARD_WATCH, 0);
		break;

	case ACTION_EXIT:
		// Exit
		SendMessage(hWnd, WM_COMMAND, ID_MENUITEM_EXIT, 0);
		break;
	}
	return TRUE;
}

/*
 * action_check - check action
 */
static BOOL action_check(const int type)
{
	int i;

	// Find action
	for (i = 0; i < option.action_cnt; i++) {
		if (type == (option.action_info + i)->type && (option.action_info + i)->enable != 0) {
			return TRUE;
		}
	}
	return FALSE;
}

/*
 * clipboard_to_history - add clipboard contents to history
 */
static BOOL clipboard_to_history(const HWND hWnd)
{
	DATA_INFO *di;
	TCHAR err_str[BUF_SIZE];
	TOOL_MENU_INFO cp_tmi;
	DWORD sequence, clipboard_sequence, clipboard_process = 0;

	// Native menu tracking pumps timers on the UI thread. Do not replace/free
	// history data or activate an editor until all menu references are gone.
	if (native_menu_depth != 0 || popup_menu != NULL || menu_delete_target != NULL ||
		menu_ghost_wnd != NULL) {
		if (popup_menu != NULL && menu_delete_target == NULL &&
			pinned_image_clipboard_is_screenshot(hWnd)) {
			menu_reopen_requested = FALSE;
			menu_delete_requested = FALSE;
			EndMenu();
		}
		SetTimer(hWnd, ID_HISTORY_TIMER, RECLIP_INTERVAL, NULL);
		return FALSE;
	}

	CopyMemory(&cp_tmi, &tmi, sizeof(TOOL_MENU_INFO));

	// Check excluded window
	if (window_ignore_check(GetForegroundWindow()) == FALSE) {
		KillTimer(hWnd, ID_HISTORY_TIMER);
		KillTimer(hWnd, ID_TOOL_TIMER);
		ZeroMemory(&tmi, sizeof(TOOL_MENU_INFO));
		return TRUE;
	}

	if (OpenClipboard(hWnd) == FALSE) {
		// Wait until clipboard becomes available
		SetTimer(hWnd, ID_HISTORY_TIMER, RECLIP_INTERVAL, NULL);
		if (tmi.enable == TRUE) {
			SetTimer(hWnd, ID_TOOL_TIMER, option.tool_valid_interval, NULL);
		}
		return FALSE;
	}
	KillTimer(hWnd, ID_HISTORY_TIMER);
	KillTimer(hWnd, ID_TOOL_TIMER);
	ZeroMemory(&tmi, sizeof(TOOL_MENU_INFO));

	sequence = clipboard_sequence = GetClipboardSequenceNumber();
	GetWindowThreadProcessId(GetClipboardOwner(), &clipboard_process);
	// Ignore clipboard writes made by CLCL itself.
	if (clipboard_process == GetCurrentProcessId()) sequence = 0;

	// Create item from clipboard
	*err_str = TEXT('\0');
	if ((di = clipboard_to_item(err_str)) == NULL) {
		CloseClipboard();
		if (*err_str != TEXT('\0')) {
			_SetForegroundWindow(hWnd);
			MessageBox(hWnd, err_str, ERROR_TITLE, MB_ICONERROR);
		}
		return TRUE;
	}
	CloseClipboard();

	// Crop detected screenshots before deduplication or persistence.
	pinned_image_auto_open(hWnd, di, sequence);
	// Add to history
	if (history_add(&history_data.child, di, (cp_tmi.enable == TRUE) ? FALSE : TRUE) == FALSE) {
		pinned_image_bind_history(di, clipboard_sequence, FALSE);
		data_free(di);
		return TRUE;
	}
	pinned_image_bind_history(di, clipboard_sequence, TRUE);
	// Tool to execute when added to history
	tool_execute_all(hWnd, CALLTYPE_ADD_HISTORY, di);

	// Atomically save to SQLite immediately
	if (db_history_is_open()) {
		db_history_save_item(di);
		db_history_trim(10000);
	}

	// Execute tool from menu
	if (cp_tmi.enable == TRUE &&
		(!(tool_execute(hWnd, cp_tmi.ti, CALLTYPE_MENU, di, NULL) & TOOL_CANCEL) ||
		window_paste_check(GetForegroundWindow()) == TRUE) &&
		data_check(&history_data, di) != NULL) {

		data_delete(&history_data.child, di, FALSE);
		// Send data to clipboard
		SendMessage(hWnd, WM_ITEM_TO_CLIPBOARD, 0, (LPARAM)di);
		data_free(di);
		if (cp_tmi.paste != 0 && cp_tmi.ti != NULL && cp_tmi.ti->copy_paste == 1) {
			// Wait until key is released
			key_wait();
			// Unregister hotkey
			unregist_hotkey(hWnd);
			// Paste
			sendkey_paste(GetForegroundWindow());
			// Register hotkey
			regist_hotkey(hWnd, FALSE);
		}
	}

	// Set task tray tooltip
	set_tray_tooltip(hWnd);
	if (option.history_save == 1 && option.history_always_save == 1 && !db_history_is_open()) {
		// Save history
		SendMessage(hWnd, WM_HISTORY_SAVE, 0, 0);
	}
	// Notify history change
	SendMessage(hWnd, WM_HISTORY_CHANGED, 0, 0);
	return TRUE;
}

/*
 * item_to_clipboard - send item to clipboard
 */
static BOOL item_to_clipboard(const HWND hWnd, DATA_INFO *from_di, const BOOL delete_flag)
{
	DATA_INFO *di;
	TCHAR err_str[BUF_SIZE];
	int call_type = CALLTYPE_ITEM_TO_CLIPBOARD;

	// Lazily load data body from SQLite if not yet loaded
	if (db_history_is_open()) {
		db_history_ensure_item_data(from_di);
	}

	// Copy data
	if ((di = data_item_copy(from_di, FALSE, FALSE, err_str)) == NULL) {
		if (*err_str != TEXT('\0')) {
			_SetForegroundWindow(hWnd);
			MessageBox(hWnd, err_str, ERROR_TITLE, MB_ICONERROR);
		}
		return FALSE;
	}

	if (data_check(&history_data, from_di) != NULL) {
		call_type |= CALLTYPE_HISTORY;
	} else if (data_check(&regist_data, from_di) != NULL) {
		call_type |= CALLTYPE_REGIST;
		// Do not add registered items to history
		if (option.history_ignore_regist_item == 1) {
			clip_flag = TRUE;
		}
	}
	// Tool to execute when sending data to clipboard
	if (tool_execute_all(hWnd, call_type, di) & TOOL_CANCEL) {
		data_free(di);
		return FALSE;
	}

	// Send to clipboard
	*err_str = TEXT('\0');
	if (clipboard_set_datainfo(hWnd, di, err_str) == FALSE &&
		*err_str != TEXT('\0')) {
		clip_flag = FALSE;
		data_free(di);
		_SetForegroundWindow(hWnd);
		MessageBox(hWnd, err_str, ERROR_TITLE, MB_ICONERROR);
		return FALSE;
	}
	data_free(di);

	if (clip_flag == TRUE) {
		clip_flag = FALSE;
		if (hViewerWnd != NULL) {
			// Notify clipboard change
			SendMessage(hViewerWnd, WM_VIEWER_CHANGE_CLIPBOARD, 0, 0);
		}
	}
	if ((call_type & CALLTYPE_HISTORY) &&
		delete_flag == TRUE && option.history_delete == 1 && from_di->type == TYPE_ITEM &&
		window_ignore_check(GetForegroundWindow()) == TRUE) {
		// Delete history
		if (data_delete(&history_data.child, from_di, TRUE) == TRUE) {
			// Notify history change
			SendMessage(hWnd, WM_HISTORY_CHANGED, 0, 0);
		}
	}
	return TRUE;
}

/*
 * load_history - load history
 */
static BOOL load_history(const HWND hWnd, const int load_flag)
{
	TCHAR path[MAX_PATH];
	TCHAR err_str[BUF_SIZE + MAX_PATH];

	history_data.type = TYPE_ROOT;

	// Initialize SQLite database
	db_history_init(work_path);

	// Auto-migrate if legacy history.dat exists and DB is empty
	wsprintf(path, TEXT("%s\\%s"), work_path, HISTORY_FILENAME);
	if (PathFileExists(path) == TRUE) {
		db_history_migrate_from_dat(path);
	}

	// Load history (fast load of metadata only, lazy load of data body)
	if (load_flag != 0 || option.history_save == 1) {
		int max_cnt = (option.history_max > 0 && option.history_max > 1000) ? option.history_max : 1000;
		if (db_history_is_open()) {
			db_history_load_recent(max_cnt, &history_data.child);
		} else {
			*err_str = TEXT('\0');
			if (file_read_data(path, &history_data.child, err_str) == FALSE && *err_str != TEXT('\0')) {
				_SetForegroundWindow(hWnd);
				lstrcat(err_str, TEXT("\r\n"));
				lstrcat(err_str, path);
				MessageBox(hWnd, err_str, ERROR_TITLE, MB_ICONERROR);
				return FALSE;
			}
		}
		history_restructure(&history_data.child, option.history_max);
		if (db_history_is_open()) {
			DATA_INFO *di, *item;
			for (di = history_data.child; di != NULL; di = di->next) {
				if (di->type == TYPE_FOLDER) {
					for (item = di->child; item != NULL; item = item->next) {
						if (item->type == TYPE_ITEM) db_history_ensure_item_data(item);
					}
				} else if (di->type == TYPE_ITEM) {
					db_history_ensure_item_data(di);
				}
			}
		}
	}
	return TRUE;
}

/*
 * load_regist - load registered items
 */
static BOOL load_regist(const HWND hWnd)
{
	TCHAR path[MAX_PATH];
	TCHAR err_str[BUF_SIZE + MAX_PATH];

	regist_data.type = TYPE_ROOT;

	// Load registered items
	wsprintf(path, TEXT("%s\\%s"), work_path, REGIST_FILENAME);
	*err_str = TEXT('\0');
	if (file_read_data(path, &regist_data.child, err_str) == FALSE && *err_str != TEXT('\0')) {
		_SetForegroundWindow(hWnd);
		lstrcat(err_str, TEXT("\r\n"));
		lstrcat(err_str, path);
		MessageBox(hWnd, err_str, ERROR_TITLE, MB_ICONERROR);
		return FALSE;
	}
	return TRUE;
}

/*
 * save_history - save history
 */
static BOOL save_history(const HWND hWnd, const int save_flag)
{
	TCHAR path[MAX_PATH];
	TCHAR err_str[BUF_SIZE + MAX_PATH];

	if (save_flag == 0 && option.history_save == 0) {
		// Do not save history
		if (db_history_is_open()) {
			db_history_trim(0);
		}
		wsprintf(path, TEXT("%s\\%s"), work_path, HISTORY_FILENAME);
		DeleteFile(path);
		return TRUE;
	}

	if (db_history_is_open()) {
		db_history_trim(10000);
		return TRUE;
	}

	// Save history (stream directly without memory copying when filter_save = TRUE)
	wsprintf(path, TEXT("%s\\%s"), work_path, HISTORY_FILENAME);
	*err_str = TEXT('\0');
	if (file_write_data(path, history_data.child, TRUE, err_str) == FALSE) {
		if (*err_str != TEXT('\0') && session_ending == FALSE) {
			_SetForegroundWindow(hWnd);
			lstrcat(err_str, TEXT("\r\n"));
			lstrcat(err_str, path);
			MessageBox(hWnd, err_str, ERROR_TITLE, MB_ICONERROR);
		}
		return FALSE;
	}
	return TRUE;
}

/*
 * save_regist - save registered items
 */
BOOL save_regist(const HWND hWnd)
{
	TCHAR path[MAX_PATH];
	TCHAR err_str[BUF_SIZE + MAX_PATH];

	// Save registered items
	wsprintf(path, TEXT("%s\\%s"), work_path, REGIST_FILENAME);
	*err_str = TEXT('\0');
	if (file_write_data(path, regist_data.child, FALSE, err_str) == FALSE) {
		if (*err_str != TEXT('\0') && session_ending == FALSE) {
			_SetForegroundWindow(hWnd);
			lstrcat(err_str, TEXT("\r\n"));
			lstrcat(err_str, path);
			MessageBox(hWnd, err_str, ERROR_TITLE, MB_ICONERROR);
		}
		return FALSE;
	}
	return TRUE;
}

/*
 * regist_hotkey - register hotkey
 */
static void regist_hotkey(const HWND hWnd, const BOOL show_err)
{
	BOOL hk_err = FALSE;
	int id;
	int i;

	// Action
	for (i = 0; i < option.action_cnt; i++) {
		if ((option.action_info + i)->type == ACTION_TYPE_HOTKEY && (option.action_info + i)->enable != 0 &&
			RegisterHotKey(hWnd, (option.action_info + i)->id,
			(option.action_info + i)->modifiers, (option.action_info + i)->virtkey) == FALSE) {
			hk_err = TRUE;
		}
	}
	// Tool
	for (i = 0; i < option.tool_cnt; i++) {
		if (((option.tool_info + i)->call_type & CALLTYPE_MENU) &&
			(option.tool_info + i)->virtkey != 0 &&
			RegisterHotKey(hWnd, (option.tool_info + i)->id,
			(option.tool_info + i)->modifiers, (option.tool_info + i)->virtkey) == FALSE) {
			hk_err = TRUE;
		}
	}
	// Regist
	id = HKEY_ID + option.action_cnt + option.tool_cnt;
	if (regist_regist_hotkey(hWnd, regist_data.child, &id) == FALSE) {
		hk_err = TRUE;
	}

	if (hk_err == TRUE && option.action_show_hotkey_error == 1 && show_err == TRUE) {
		// Registration error
		MessageBox(hWnd, message_get_res(IDS_ERROR_HOTKEY), ERROR_TITLE, MB_ICONERROR);
	}
}

/*
 * unregist_hotkey - unregister hotkey
 */
static void unregist_hotkey(const HWND hWnd)
{
	int i;

	// Action
	for (i = 0; i < option.action_cnt; i++) {
		if ((option.action_info + i)->type == ACTION_TYPE_HOTKEY && (option.action_info + i)->enable != 0) {
			UnregisterHotKey(hWnd, (option.action_info + i)->id);
		}
	}
	// Tool
	for (i = 0; i < option.tool_cnt; i++) {
		if (((option.tool_info + i)->call_type & CALLTYPE_MENU) && (option.tool_info + i)->virtkey != 0) {
			UnregisterHotKey(hWnd, (option.tool_info + i)->id);
		}
	}
	// Regist
	regist_unregist_hotkey(hWnd, regist_data.child);
}

/*
 * regist_hook - register hook
 */
static void regist_hook(const HWND hWnd)
{
	FARPROC SetHook;
	TCHAR err_str[BUF_SIZE];
	int i;

	// Register hook
	for (i = 0; i < option.action_cnt; i++) {
		if ((option.action_info + i)->enable != 0 &&
			((option.action_info + i)->type == ACTION_TYPE_CTRL_CTRL ||
			(option.action_info + i)->type == ACTION_TYPE_SHIFT_SHIFT ||
			(option.action_info + i)->type == ACTION_TYPE_ALT_ALT)) {
			hook_lib = LoadLibrary(HOOK_LIB);
			if (hook_lib == NULL) {
				message_get_error(GetLastError(), err_str);
				if (*err_str != TEXT('\0')) {
					lstrcat(err_str, TEXT("\r\n"));
					lstrcat(err_str, HOOK_LIB);
					MessageBox(hWnd, err_str, ERROR_TITLE, MB_ICONERROR);
				}
				break;
			}
			SetHook = GetProcAddress(hook_lib, "SetHook");
			if (SetHook == NULL) {
				message_get_error(GetLastError(), err_str);
				if (*err_str != TEXT('\0')) {
					lstrcat(err_str, TEXT("\r\n"));
					lstrcat(err_str, HOOK_LIB);
					MessageBox(hWnd, err_str, ERROR_TITLE, MB_ICONERROR);
				}
				break;
			}
			SetHook(hWnd, WM_KEY_HOOK);
			break;
		}
	}
}

/*
 * unregist_hook - unregister hook
 */
static void unregist_hook(void)
{
	FARPROC UnHook;

	// Unhook
	if (hook_lib != NULL) {
		UnHook = GetProcAddress(hook_lib, "UnHook");
		if (UnHook != NULL) {
			UnHook();
		}
		FreeLibrary(hook_lib);
		hook_lib = NULL;
	}
}

/*
 * load_tray_icon - load task tray icon
 */
static void load_tray_icon(void)
{
	int icon_size = GetSystemMetricsDpi(SM_CXSMICON);

	if (icon_size == tray_icon_size) {
		return;
	}
	if (icon_clip != NULL) {
		DestroyIcon(icon_clip);
	}
	if (icon_clip_ban != NULL) {
		DestroyIcon(icon_clip_ban);
	}
	icon_clip = (HICON)LoadImage(hInst, MAKEINTRESOURCE(IDI_ICON_CLIP),
		IMAGE_ICON, icon_size, icon_size, 0);
	icon_clip_ban = (HICON)LoadImage(hInst, MAKEINTRESOURCE(IDI_ICON_CLIP_BAN),
		IMAGE_ICON, icon_size, icon_size, 0);
	tray_icon_size = icon_size;
}

static void preload_menus(const HWND hWnd)
{
	int i;
	for (i = 0; i < option.action_cnt; i++) {
		ACTION_INFO *ai = option.action_info + i;
		if (ai->enable != 0 && ai->action == ACTION_POPUPMEMU) {
			HMENU menu = menu_create(hWnd, ai->menu_info, ai->menu_cnt,
				history_data.child, regist_data.child);
			if (menu != NULL) menu_destory(menu);
			menu_free();
		}
	}
}

/*
 * winodw_initialize - initialize window
 */
static BOOL winodw_initialize(const HWND hWnd)
{
	TCHAR err_str[BUF_SIZE + MAX_PATH];

	*err_str = TEXT('\0');

	// Initialize format information
	if (format_initialize(err_str) == FALSE && *err_str != TEXT('\0')) {
		_SetForegroundWindow(hWnd);
		MessageBox(hWnd, err_str, ERROR_TITLE, MB_ICONERROR);
	}
	// Initialize tool information
	if (tool_initialize(err_str) == FALSE && *err_str != TEXT('\0')) {
		_SetForegroundWindow(hWnd);
		MessageBox(hWnd, err_str, ERROR_TITLE, MB_ICONERROR);
	}
	// Load history
	if (load_history(hWnd, 0) == FALSE) {
		return FALSE;
	}
	// Load registered items
	if (load_regist(hWnd) == FALSE) {
		return FALSE;
	}
	// Prepare item titles, icons and previews before the first menu request.
	preload_menus(hWnd);

	// Create tooltip
	hToolTip = tooltip_create(hInst);

	// Register icon in task tray
	load_tray_icon();
	icon_tray = (option.main_clipboard_watch == 1) ? icon_clip : icon_clip_ban;
	set_tray_icon(hWnd, icon_tray, TEXT(""));

	// Start clipboard monitoring
	if (option.main_clipboard_watch == 1) {
		hClipNextWnd = SetClipboardViewer(hWnd);
		SetTimer(hWnd, ID_RECHAIN_TIMER, RECHAIN_INTERVAL, NULL);
		// Get current clipboard content on startup (duplicates automatically prevented by hash)
		SetTimer(hWnd, ID_HISTORY_TIMER, 100, NULL);
	}

	// Register hotkey
	regist_hotkey(hWnd, TRUE);
	// Register hook
	regist_hook(hWnd);

	// Tool to execute on startup
	tool_execute_all(hWnd, CALLTYPE_START, NULL);

	// Show viewer
	if (option.main_show_viewer == 1) {
		SendMessage(hWnd, WM_COMMAND, ID_MENUITEM_VIEWER, 0);
	}
	// Command line processing
	commnad_line_func(FindWindow(MAIN_WND_CLASS, MAIN_WINDOW_TITLE));
	return TRUE;
}

/*
 * winodw_reset - reload settings
 */
static BOOL winodw_reset(const HWND hWnd)
{
	TCHAR err_str[BUF_SIZE];
	BOOL show_viewer = FALSE;
	int show_flag = SW_SHOW;

	if (hViewerWnd != NULL) {
		show_viewer = TRUE;
		if (IsIconic(hViewerWnd) == TRUE) {
			show_flag = SW_MINIMIZE;
		} else if (IsZoomed(hViewerWnd) == TRUE) {
			show_flag = SW_SHOWMAXIMIZED;
		}
		SendMessage(hViewerWnd, WM_CLOSE, 0, 0);
	}

	// Unregister hotkey
	unregist_hotkey(hWnd);
	// Unhook
	unregist_hook();

	// Free item menu information
	data_menu_free(history_data.child);
	data_menu_free(regist_data.child);
	// Free format information
	format_free();

	// Free settings
	ini_free();
	// Load settings
	get_work_path(hInst);
	if (ini_get_option(err_str) == FALSE) {
		MessageBox(hWnd, err_str, ERROR_TITLE, MB_ICONERROR);
		return FALSE;
	}

	// Restructure history items according to updated history_max
	history_restructure(&history_data.child, option.history_max);
	SendMessage(hWnd, WM_HISTORY_CHANGED, 0, 0);

	// Initialize format information
	if (format_initialize(err_str) == FALSE && *err_str != TEXT('\0')) {
		_SetForegroundWindow(hWnd);
		MessageBox(hWnd, err_str, ERROR_TITLE, MB_ICONERROR);
	}
	// Initialize tool information
	if (tool_initialize(err_str) == FALSE && *err_str != TEXT('\0')) {
		_SetForegroundWindow(hWnd);
		MessageBox(hWnd, err_str, ERROR_TITLE, MB_ICONERROR);
	}
	preload_menus(hWnd);

	// Register hotkey
	regist_hotkey(hWnd, TRUE);
	// Register hook
	regist_hook(hWnd);

	SetTimer(hWnd, ID_RECHAIN_TIMER, RECHAIN_INTERVAL, NULL);

	if (hViewerWnd == NULL && show_viewer == TRUE) {
		hViewerWnd = (HWND)-1;
		hViewerWnd = viewer_create(hWnd, show_flag);
		_SetForegroundWindow(hViewerWnd);
	}
	return TRUE;
}

/*
 * winodw_save - save window settings
 */
static BOOL winodw_save(const HWND hWnd)
{
	// Save history
	if (save_history(hWnd, 0) == FALSE && session_ending == FALSE &&
		MessageBox(hWnd, message_get_res(IDS_ERROR_END), ERROR_TITLE, MB_ICONQUESTION | MB_YESNO) == IDNO) {
		return FALSE;
	}
	// Save settings
	ini_put_option();

	// Tool to execute on exit
	if (tool_execute_all(hWnd, CALLTYPE_END, NULL) & TOOL_DATA_MODIFIED) {
		save_history(hWnd, 0);
	}
	return TRUE;
}

/*
 * winodw_end - window cleanup
 */
static BOOL winodw_end(const HWND hWnd)
{
	pinned_image_close_all();
	if (hViewerWnd != NULL) {
		SendMessage(hViewerWnd, WM_CLOSE, 0, 0);
	}
	if (hToolTip != NULL) {
		tooltip_close(hToolTip);
		hToolTip = NULL;
	}

	// Stop clipboard monitoring
	KillTimer(hWnd, ID_RECHAIN_TIMER);
	if (option.main_clipboard_watch == 1) {
		ChangeClipboardChain(hWnd, hClipNextWnd);
		hClipNextWnd = NULL;
	}

	// Unregister hotkey
	unregist_hotkey(hWnd);
	// Unhook
	unregist_hook();

	// Close SQLite database
	if (db_history_is_open()) {
		db_history_close();
	}

	// Free history
	data_free(history_data.child);
	history_data.child = NULL;
	data_free(regist_data.child);
	regist_data.child = NULL;
	// Free format information
	format_free();

#ifdef OP_XP_STYLE
	theme_free();
#endif

	if (option.main_show_trayicon != 0) {
		tray_message(hWnd, NIM_DELETE, TRAY_ID, NULL, NULL);
	}
	DestroyIcon(icon_clip);
	DestroyIcon(icon_clip_ban);
	// Free default icon to display in menu
	menu_free_icons();
	// Free caret information
	caret_free();
	return TRUE;
}

/*
 * main_proc - main window procedure
 */
static LRESULT CALLBACK main_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static int key_cnt, key_flag;
	static UINT prev_key;
	static BOOL save_flag = FALSE;
	static UINT WM_TASKBARCREATED;

	if (main_window_handle == NULL) {
		main_window_handle = hWnd;
	}

	switch (msg) {
	case WM_CREATE:
		WM_TASKBARCREATED = RegisterWindowMessage(TEXT("TaskbarCreated"));
		// Configure dark mode
		dark_mode_set_window(hWnd);
		// Create window
		if (winodw_initialize(hWnd) == FALSE) {
			return -1;
		}
		break;

	case WM_SETTINGCHANGE:
	case WM_THEMECHANGED:
		// Change color scheme
		if (dark_mode_is_color_change(msg, lParam) == TRUE) {
			dark_mode_update();
			dark_mode_refresh_window(hWnd);
		}
		return DefWindowProc(hWnd, msg, wParam, lParam);

	case WM_QUERYENDSESSION:
		// Windows shutdown
		session_ending = TRUE;
		winodw_save(hWnd);
		save_flag = TRUE;
		return TRUE;

	case WM_DPICHANGED:
	case WM_DISPLAYCHANGE:
		// Load task tray icon
		if (msg == WM_DPICHANGED) {
			SetDpi(HIWORD(wParam));
		}
		load_tray_icon();
		icon_tray = (option.main_clipboard_watch == 1) ? icon_clip : icon_clip_ban;
		set_tray_tooltip(hWnd);
		break;

	case WM_ENDSESSION:
		// Windows shutdown
		if (wParam == FALSE) {
			session_ending = FALSE;
			save_flag = FALSE;
			return 0;
		}
		session_ending = TRUE;
		if (save_flag == FALSE) {
			winodw_save(hWnd);
		}
		save_flag = TRUE;
		winodw_end(hWnd);
		DestroyWindow(hWnd);
		return 0;

	case WM_CLOSE:
		// Close window
		if (winodw_save(hWnd) == FALSE) {
			break;
		}
		save_flag = TRUE;
		winodw_end(hWnd);
		DestroyWindow(hWnd);
		break;

	case WM_DESTROY:
		menu_ghost_hide();
		// Destroy window
		if (save_flag == FALSE) {
			winodw_save(hWnd);
		}
		PostQuitMessage(0);
		break;

	case WM_MEASUREITEM:
		// Menu drawing settings
		if (wParam == 0) {
			menu_set_drawitem((MEASUREITEMSTRUCT *)lParam);
		}
		break;

	case WM_DRAWITEM:
		// Draw menu
		if (wParam == 0) {
			HWND menu_wnd = WindowFromDC(((DRAWITEMSTRUCT *)lParam)->hDC);
			RECT monitor_rect;

			if (!has_reopen_pos && menu_wnd != NULL && (menu_root_wnd == NULL || menu_wnd == menu_root_wnd)) {
				menu_record_reopen_position(menu_wnd);
			}

#ifdef MENU_LAYERER
			set_menu_layerer(menu_wnd, option.menu_alpha);
#endif
			if (((DRAWITEMSTRUCT *)lParam)->itemState & ODS_SELECTED) {
				if (menu_wnd != NULL) {
					menu_sel_rect = ((DRAWITEMSTRUCT *)lParam)->rcItem;
					MapWindowPoints(menu_wnd, NULL, (LPPOINT)&menu_sel_rect, 2);
				}

				// Set tooltip display position
				menu_sel_pt.x = ((DRAWITEMSTRUCT *)lParam)->rcItem.left +
					(((DRAWITEMSTRUCT *)lParam)->rcItem.right - ((DRAWITEMSTRUCT *)lParam)->rcItem.left) / 2;
				menu_sel_pt.y = ((DRAWITEMSTRUCT *)lParam)->rcItem.top;
				menu_sel_top = ((DRAWITEMSTRUCT *)lParam)->rcItem.bottom - ((DRAWITEMSTRUCT *)lParam)->rcItem.top + 1;

				if (menu_wnd != NULL) {
					ClientToScreen(menu_wnd, &menu_sel_pt);
					GetMonitorRectFromPoint(menu_sel_pt, &monitor_rect);
					if (menu_sel_pt.y > monitor_rect.bottom) {
						menu_sel_pt.x = menu_sel_pt.y = 0;
						menu_sel_top = 0;
					}
				}
			}
			menu_drawitem((DRAWITEMSTRUCT *)lParam);
		}
		break;

	case WM_MENUCHAR:
		// Menu accelerator
		if (HIWORD(wParam) == MF_POPUP) {
			if ((TCHAR)LOWORD(wParam) == TEXT('/')) {
				search_mode_requested = TRUE;
				return MAKELRESULT(0, MNC_CLOSE);
			}
			return menu_accelerator((HMENU)lParam, (TCHAR)LOWORD(wParam));
		}
		break;

	case WM_INITMENUPOPUP:

		if (HIWORD(lParam) == 0) {
			// Popup menu (not system menu)
			HMENU hSub = (HMENU)wParam;
			UINT uPos = (UINT)LOWORD(lParam);
			HMENU hParent = NULL;
			RECT rcItem = {0};

			if (hSub != popup_menu) {
				if (current_menu_handle != NULL && GetSubMenu(current_menu_handle, uPos) == hSub) {
					hParent = current_menu_handle;
				} else if (popup_menu != NULL && GetSubMenu(popup_menu, uPos) == hSub) {
					hParent = popup_menu;
				}
				if (hParent == NULL && popup_menu != NULL) {
					int cnt = GetMenuItemCount(popup_menu);
					int p, sp;
					for (p = 0; p < cnt; p++) {
						HMENU hTest = GetSubMenu(popup_menu, p);
						if (hTest == hSub) {
							hParent = popup_menu;
							uPos = (UINT)p;
							break;
						} else if (hTest != NULL) {
							int subCnt = GetMenuItemCount(hTest);
							for (sp = 0; sp < subCnt; sp++) {
								if (GetSubMenu(hTest, sp) == hSub) {
									hParent = hTest;
									uPos = (UINT)sp;
									break;
								}
							}
							if (hParent != NULL) {
								break;
							}
						}
					}
				}
				if (hParent != NULL && GetMenuItemRect(NULL, hParent, uPos, &rcItem) != FALSE) {
					active_submenu_item_rect = rcItem;
					if (hSub == menu_get_favourites_submenu(popup_menu)) {
						favourites_menu_item_rect = rcItem;
					}
					active_submenu_pending = TRUE;
				} else {
					active_submenu_pending = FALSE;
				}
			} else {
				active_submenu_pending = FALSE;
			}
		}
		break;

	case WM_UNINITMENUPOPUP:
		active_submenu_pending = FALSE;
		break;

	case WM_MENUSELECT:
		current_menu_id = (UINT)LOWORD(wParam);
		current_menu_flags = (UINT)HIWORD(wParam);
		current_menu_handle = (HMENU)lParam;
		current_selected_mii = NULL;
		if ((UINT)LOWORD(wParam) != 0xFFFF && (HMENU)lParam != NULL) {
			MENUITEMINFO minfo;
			ZeroMemory(&minfo, sizeof(minfo));
			minfo.cbSize = sizeof(minfo);
			minfo.fMask = MIIM_DATA;
			if (HIWORD(wParam) & MF_POPUP) {
				if (GetMenuItemInfo((HMENU)lParam, LOWORD(wParam), TRUE, &minfo) && minfo.dwItemData != 0) {
					current_selected_mii = (MENU_ITEM_INFO *)minfo.dwItemData;
				}
			} else {
				if (GetMenuItemInfo((HMENU)lParam, LOWORD(wParam), FALSE, &minfo) && minfo.dwItemData != 0) {
					current_selected_mii = (MENU_ITEM_INFO *)minfo.dwItemData;
				}
			}
		}
		if (current_selected_mii == NULL && current_menu_id >= ID_MENUITEM_DATA) {
			current_selected_mii = menu_get_info(current_menu_id);
		}
		// Show tooltip for selected menu
		if (option.menu_show_tooltip == 0 ||
			(UINT)LOWORD(wParam) == 0xFFFF ||
			(UINT)HIWORD(wParam) & MF_SEPARATOR) {
			tooltip_hide(hToolTip);
			break;
		}
		// Show tooltip
		show_menu_tooltip(hToolTip, (HMENU)lParam, (UINT)LOWORD(wParam),
			((UINT)HIWORD(wParam) & MF_MOUSESELECT) ? TRUE : FALSE);
		break;

	case WM_ENTERMENULOOP:
		native_menu_depth++;
		break;

	case WM_EXITMENULOOP:
		if (native_menu_depth != 0) native_menu_depth--;
		if (popup_menu == NULL) {
			break;
		}
		// Hide tooltip
		tooltip_hide(hToolTip);
		if (!menu_delete_requested && !menu_reopen_requested) {
			menu_context_reopen_target = NULL;
			SetRectEmpty(&menu_wnd_rect);
			SetRectEmpty(&menu_sel_rect);
			has_reopen_pos = FALSE;
			menu_reopen_align = TPM_TOPALIGN | TPM_LEFTALIGN;
			menu_cursor_restore_needed = FALSE;
			menu_reopen_folder_title[0] = TEXT('\0');
			menu_reopen_is_fav = FALSE;
			menu_reopen_fav_folder = NULL;
			menu_reopen_fav_ancestor = NULL;
			menu_folder_hover_posted = FALSE;
			KillTimer(hWnd, ID_MENU_HOVER_SAFETY_TIMER);
			menu_ghost_hide();
		}
		current_menu_id = 0;
		current_menu_flags = 0;
		current_menu_handle = NULL;
		current_selected_mii = NULL;
		active_submenu_pending = FALSE;
		menu_root_wnd = NULL;
		break;

	case WM_ENTERIDLE:
		if (wParam == MSGF_MENU && popup_menu != NULL) {
			HWND menuWnd = (HWND)lParam;
			if (menuWnd == menu_root_wnd && !has_reopen_pos && IsWindowVisible(menuWnd)) {
				menu_record_reopen_position(menuWnd);
			}
			if (!menu_cursor_restore_needed) {
				break;
			}
			if (menuWnd == menu_root_wnd && menu_open_reopen_folder(menuWnd)) {
				break;
			}
			menu_position_cursor_in_submenu(menuWnd);
		}
		break;

	case WM_CHANGECBCHAIN:
		// Change clipboard chain
		if ((HWND)wParam == hClipNextWnd && (HWND)lParam != hWnd) {
			hClipNextWnd = (HWND)lParam;
		} else if (hClipNextWnd != NULL && hClipNextWnd != hWnd) {
			DWORD_PTR result;
			SendMessageTimeout(hClipNextWnd, msg, wParam, lParam,
				SMTO_ABORTIFHUNG | SMTO_BLOCK, 200, &result);
		}
		break;

	case WM_DRAWCLIPBOARD:
		// Clipboard monitoring
		if (hClipNextWnd != NULL && hClipNextWnd != hWnd) {
			DWORD_PTR result;
			SendMessageTimeout(hClipNextWnd, msg, wParam, lParam,
				SMTO_ABORTIFHUNG | SMTO_BLOCK, 200, &result);
		}
		if (clip_flag == TRUE) {
			// Do not add to history
			break;
		}
		// Add to history
		SetTimer(hWnd, ID_HISTORY_TIMER, option.history_add_interval, NULL);
		SetTimer(hWnd, ID_RECHAIN_TIMER, RECHAIN_INTERVAL, NULL);
		rechain_cnt = 0;
		break;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case ID_MENUITEM_EXIT:
			// Exit
			SendMessage(hWnd, WM_CLOSE, 0, 0);
			break;

		case ID_MENUITEM_VIEWER:
			// Viewer
			if (hViewerWnd != NULL) {
				if (option.viewer_toggle == 1) {
					SendMessage(hViewerWnd, WM_CLOSE, 0, 0);
					break;
				}
				if (IsIconic(hViewerWnd) != 0) {
					ShowWindow(hViewerWnd, SW_RESTORE);
				}
				_SetForegroundWindow(hViewerWnd);
				break;
			}
			_SetForegroundWindow(hWnd);
			hViewerWnd = (HWND)-1;
			hViewerWnd = viewer_create(hWnd, SW_SHOW);
			_SetForegroundWindow(hViewerWnd);
			break;

		case ID_MENUITEM_NEW_SNIP:
			if (!pinned_image_start_snip(hWnd)) MessageBeep(MB_ICONWARNING);
			break;
		case ID_MENUITEM_SCROLLING_SNIP:
			if (!pinned_image_start_scrolling_snip(hWnd)) MessageBeep(MB_ICONWARNING);
			break;

		case ID_MENUITEM_OPTION:
			// Options
			SendMessage(hWnd, WM_OPTION_SHOW, 0, 0);
			break;

		case ID_MENUITEM_CLIPBOARD_WATCH:
			// Toggle clipboard monitoring
			SendMessage(hWnd, WM_SET_CLIPBOARD_WATCH, !option.main_clipboard_watch, 0);
			break;
		}
		break;

	case WM_TIMER:
		// Timer
		switch (wParam) {
		case ID_HISTORY_TIMER:
			// Add clipboard data to history
			if (clipboard_to_history(hWnd) == TRUE && hViewerWnd != NULL) {
				// Notify clipboard change
				SendMessage(hViewerWnd, WM_VIEWER_CHANGE_CLIPBOARD, 0, 0);
			}
			break;

		case ID_RECHAIN_TIMER:
			// Resume clipboard monitoring
			if (option.main_clipboard_watch == 0 ||
				option.main_clipboard_rechain_minute <= 0) {
				KillTimer(hWnd, wParam);
				break;
			}
			rechain_cnt++;
			if (rechain_cnt >= option.main_clipboard_rechain_minute) {
				rechain_cnt = 0;
				if (GetClipboardViewer() == hWnd) {
					// No need to re-monitor
					break;
				}
				// Stop clipboard monitoring
				ChangeClipboardChain(hWnd, hClipNextWnd);
				hClipNextWnd = NULL;
				// Start clipboard monitoring
				clip_flag = TRUE;
				hClipNextWnd = SetClipboardViewer(hWnd);
				clip_flag = FALSE;
			}
			break;

		case ID_TOOL_TIMER:
			// Cancel tool
			KillTimer(hWnd, wParam);
			ZeroMemory(&tmi, sizeof(TOOL_MENU_INFO));
			break;

		case ID_PASTE_TIMER:
			// Directly paste registered items
			if (paste_di == NULL) {
				KillTimer(hWnd, wParam);
				break;
			}
			if (GetAsyncKeyState(VK_MENU) < 0 ||
				GetAsyncKeyState(VK_CONTROL) < 0 ||
				GetAsyncKeyState(VK_SHIFT) < 0 ||
				GetAsyncKeyState(VK_LWIN) < 0 ||
				GetAsyncKeyState(VK_RWIN) < 0) {
				break;
			}
			KillTimer(hWnd, wParam);

			// Send data to clipboard
			SendMessage(hWnd, WM_ITEM_TO_CLIPBOARD, 0, (LPARAM)paste_di);
			if (paste_di->op_paste == 1) {
				// Unregister hotkey
				unregist_hotkey(hWnd);
				// Paste
				sendkey_paste(GetForegroundWindow());
				// Register hotkey
				regist_hotkey(hWnd, FALSE);
			}
			paste_di = NULL;
			break;

		case ID_KEY_TIMER:
			// For double key press
			KillTimer(hWnd, wParam);
			if (key_cnt > 1) {
				// Call action when key is pressed twice
				switch (prev_key) {
				case VK_CONTROL:
					action_execute(hWnd, ACTION_TYPE_CTRL_CTRL, 0, TRUE);
					break;

				case VK_SHIFT:
					action_execute(hWnd, ACTION_TYPE_SHIFT_SHIFT, 0, TRUE);
					break;

				case VK_MENU:
					action_execute(hWnd, ACTION_TYPE_ALT_ALT, 0, TRUE);
					break;
				}
			}
			key_cnt = 0;
			break;

		case ID_LCLICK_TIMER:
			// Task tray left click
			if (GetAsyncKeyState(VK_LBUTTON) < 0) {
				SetTimer(hWnd, ID_LCLICK_TIMER, 1, NULL);
				break;
			}
			KillTimer(hWnd, wParam);
			action_execute(hWnd, ACTION_TYPE_TRAY_LEFT, 0, FALSE);
			break;

		case ID_RCLICK_TIMER:
			// Task tray right click
			if (GetAsyncKeyState(VK_RBUTTON) < 0) {
				SetTimer(hWnd, ID_RCLICK_TIMER, 1, NULL);
				break;
			}
			KillTimer(hWnd, wParam);
			action_execute(hWnd, ACTION_TYPE_TRAY_RIGHT, 0, FALSE);
			break;

		case ID_TRAY_HOVER_TIMER:
			{
				POINT pt;
				GetCursorPos(&pt);
				DWORD now = GetTickCount();

				// Do not show / close preview for 2 seconds after click or while menu is displayed
				if (popup_menu != NULL || (now - tray_last_click_tick < 2000)) {
					if (tray_preview_showing) {
						tooltip_hide(hToolTip);
						tray_preview_showing = FALSE;
					}
					tray_hover_active = FALSE;
					KillTimer(hWnd, ID_TRAY_HOVER_TIMER);
					break;
				}

				// Do not hide if mouse is over preview window itself
				if (tray_preview_showing) {
					HWND mouse_wnd = WindowFromPoint(pt);
					if (mouse_wnd == hToolTip) {
						break;
					}
				}

				// Check if cursor is within tray icon area
				int dx = abs(pt.x - tray_hover_pos.x);
				int dy = abs(pt.y - tray_hover_pos.y);
				if (dx > Scale(24) || dy > Scale(24)) {
					// Moved away from tray icon
					if (tray_preview_showing) {
						tooltip_hide(hToolTip);
						tray_preview_showing = FALSE;
					}
					tray_hover_active = FALSE;
					KillTimer(hWnd, ID_TRAY_HOVER_TIMER);
					break;
				}

				// Show preview if 1 second elapsed && not yet displayed
				if (!tray_preview_showing && (now - tray_hover_start_tick >= 1000)) {
					if (history_data.child != NULL) {
						RECT anchor_rect;
						anchor_rect.left = pt.x - Scale(8);
						anchor_rect.right = pt.x + Scale(8);
						anchor_rect.top = pt.y - Scale(8);
						anchor_rect.bottom = pt.y + Scale(8);

						if (show_data_tooltip(hToolTip, history_data.child, &pt, &anchor_rect, 0, NULL, NULL) == TRUE) {
							tray_preview_showing = TRUE;
						}
					}
				}
			}
			break;

		case ID_MENU_HOVER_SAFETY_TIMER:
			KillTimer(hWnd, ID_MENU_HOVER_SAFETY_TIMER);
			menu_context_reopen_target = NULL;
			if (popup_menu != NULL && menu_cursor_restore_needed) {
				EndMenu();
			}
			if (menu_ghost_wnd != NULL) {
				menu_ghost_hide();
			}
			menu_reopen_folder_title[0] = TEXT('\0');
			menu_reopen_is_fav = FALSE;
			menu_reopen_fav_folder = NULL;
			menu_reopen_fav_ancestor = NULL;
			menu_cursor_restore_needed = FALSE;
			menu_folder_hover_posted = FALSE;
			break;
		}
		break;

	case WM_TRAY_NOTIFY:
		// Task tray message
		switch (LOWORD(lParam)) {
		case WM_LBUTTONDOWN:
			tray_cancel_preview(hWnd);
			// Timer for left double-click detection
			SetTimer(hWnd, ID_LCLICK_TIMER,
				(action_check(ACTION_TYPE_TRAY_LEFT_DBLCLK) == TRUE) ? GetDoubleClickTime() : 1, NULL);
			break;

		case WM_LBUTTONUP:
			tray_cancel_preview(hWnd);
			break;

		case WM_LBUTTONDBLCLK:
			tray_cancel_preview(hWnd);
			KillTimer(hWnd, ID_LCLICK_TIMER);
			action_execute(hWnd, ACTION_TYPE_TRAY_LEFT_DBLCLK, 0, FALSE);
			break;

		case WM_RBUTTONDOWN:
			tray_cancel_preview(hWnd);
			// Timer for right double-click detection
			SetTimer(hWnd, ID_RCLICK_TIMER,
				(action_check(ACTION_TYPE_TRAY_RIGHT_DBLCLK) == TRUE) ? GetDoubleClickTime() : 1, NULL);
			break;

		case WM_RBUTTONUP:
			tray_cancel_preview(hWnd);
			break;

		case WM_RBUTTONDBLCLK:
			tray_cancel_preview(hWnd);
			KillTimer(hWnd, ID_RCLICK_TIMER);
			action_execute(hWnd, ACTION_TYPE_TRAY_RIGHT_DBLCLK, 0, FALSE);
			break;

		case WM_MBUTTONDOWN:
		case WM_MBUTTONUP:
			tray_cancel_preview(hWnd);
			break;

		case WM_MOUSEMOVE:
			{
				HWND active_wnd, mouse_wnd;
				POINT pt;

				active_wnd = GetForegroundWindow();
				// Check taskbar
				GetCursorPos(&pt);
				mouse_wnd = WindowFromPoint(pt);
				while (mouse_wnd != NULL && mouse_wnd != active_wnd) mouse_wnd = GetParent(mouse_wnd);
				if (active_wnd != focus_info.active_wnd && active_wnd != hWnd && active_wnd != mouse_wnd) {
					// Get focus information
					get_focus_info(&focus_info, FALSE);
				}

				DWORD now = GetTickCount();
				if (popup_menu == NULL && (now - tray_last_click_tick >= 2000)) {
					if (!tray_hover_active) {
						tray_hover_active = TRUE;
						tray_hover_start_tick = now;
						tray_hover_pos = pt;
						SetTimer(hWnd, ID_TRAY_HOVER_TIMER, 50, NULL);
					} else {
						if (abs(pt.x - tray_hover_pos.x) > Scale(16) || abs(pt.y - tray_hover_pos.y) > Scale(16)) {
							tray_hover_pos = pt;
							if (!tray_preview_showing) {
								tray_hover_start_tick = now;
							}
						}
					}
				}
			}
			break;
		}
		break;

	case WM_KEY_HOOK:
		// Keyboard hook
		switch (wParam) {
		case VK_CONTROL:
		case VK_SHIFT:
		case VK_MENU:
			if (prev_key != wParam) {
				prev_key = wParam;
				if (key_flag == 1) {
					key_flag = -1;
				}
				key_cnt = 0;
			}
			if ((lParam & 0x80000000) != 0) {
				// key up
				if (key_flag == 1) {
					key_cnt++;
					SetTimer(hWnd, ID_KEY_TIMER, ((key_cnt == 1) ? option.action_double_press_time : 1), NULL);
				}
				key_flag = 0;
			} else if (key_flag == 0) {
				// key down
				key_flag = 1;
			}
			break;

		default:
			// Invalidate if another key is pressed
			if (key_flag == 1) {
				key_flag = -1;
			}
			key_cnt = 0;
			break;
		}
		break;

	case WM_HOTKEY:
		// Hotkey
		action_execute(hWnd, ACTION_TYPE_HOTKEY, (int)wParam, TRUE);
		break;

	case WM_VIEWER_NOTIFY_CLOSE:
		// Viewer exit notification
		hViewerWnd = NULL;
		break;

	case WM_GET_VERSION:
		// Get version
		return APP_VAR;

	case WM_GET_WORKPATH:
		// Get working directory
		if (lParam == 0) {
			break;
		}
		lstrcpy((TCHAR *)lParam, work_path);
		break;

	case WM_GET_CLIPBOARD_WATCH:
		// Get clipboard monitoring status
		return option.main_clipboard_watch;

	case WM_SET_CLIPBOARD_WATCH:
		// Toggle clipboard monitoring
		ZeroMemory(&tmi, sizeof(TOOL_MENU_INFO));
		if (wParam != 0) {
			option.main_clipboard_watch = 1;
			icon_tray = icon_clip;
			// Start clipboard monitoring
			clip_flag = TRUE;
			hClipNextWnd = SetClipboardViewer(hWnd);
			clip_flag = FALSE;
			SetTimer(hWnd, ID_RECHAIN_TIMER, RECHAIN_INTERVAL, NULL);
		} else {
			option.main_clipboard_watch = 0;
			icon_tray = icon_clip_ban;
			// Stop clipboard monitoring
			KillTimer(hWnd, ID_RECHAIN_TIMER);
			ChangeClipboardChain(hWnd, hClipNextWnd);
			hClipNextWnd = NULL;
		}
		set_tray_tooltip(hWnd);
		if (hViewerWnd != NULL) {
			SendMessage(hViewerWnd, WM_VIEWER_CHANGE_WATCH, 0, 0);
		}
		break;

	case WM_GET_FORMAT_ICON:
		// Get icon for format
		if (lParam != 0) {
			HICON hIcon, ret;
			BOOL free_icon = TRUE;

			hIcon = format_get_icon(format_get_index((TCHAR *)lParam, 0), wParam, &free_icon);
			if (hIcon == NULL) {
				return 0;
			}
			ret = CopyIcon(hIcon);
			if (free_icon == TRUE) {
				DestroyIcon(hIcon);
			}
			return (LRESULT)ret;
		}
		return 0;

	case WM_ENABLE_ACCELERATOR:
		// Toggle accelerator enabled/disabled
		accel_flag = (BOOL)wParam;
		break;

	case WM_REGIST_HOTKEY:
		// Register hotkey
		regist_hotkey(hWnd, TRUE);
		break;

	case WM_UNREGIST_HOTKEY:
		// Unregister hotkey
		unregist_hotkey(hWnd);
		break;

	case WM_OPTION_SHOW:
		// Display options
		{
			TCHAR buf[MAX_PATH];

			wsprintf(buf, TEXT("%s\\%s"), app_path, OPTION_EXE);
			shell_open(buf, (TCHAR *)lParam);
		}
		break;

	case WM_OPTION_GET:
		// Get options
		return (LRESULT)&option;

	case WM_OPTION_LOAD:
		// Load settings
		winodw_reset(hWnd);
		break;

	case WM_OPTION_SAVE:
		// Save settings
		ini_put_option();
		break;

	case WM_HISTORY_CHANGED:
		// History content change
		data_adjust(&history_data.child);
		if (hViewerWnd != NULL) {
			return SendMessage(hViewerWnd, msg, wParam, lParam);
		}
		break;

	case WM_HISTORY_GET_ROOT:
		// Get history item
		return (LRESULT)&history_data;

	case WM_HISTORY_LOAD:
		// Load history
		if (wParam == 0 && option.history_save == 0) {
			return TRUE;
		}
		data_free(history_data.child);
		history_data.child = NULL;
		return load_history(hWnd, wParam);

	case WM_HISTORY_SAVE:
		// Save history
		return save_history(hWnd, wParam);

	case WM_REGIST_CHANGED:
		// Registered items content change
		{
			LRESULT ret = 0;

			if (hViewerWnd != NULL) {
				ret = SendMessage(hViewerWnd, msg, wParam, lParam);
			} else {
				data_adjust(&regist_data.child);
			}
			save_regist(hWnd);
			return ret;
		}

	case WM_REGIST_GET_ROOT:
		// Get registered items
		return (LRESULT)&regist_data;

	case WM_REGIST_LOAD:
		// Load registered items
		data_free(regist_data.child);
		regist_data.child = NULL;
		return load_regist(hWnd);

	case WM_REGIST_SAVE:
		// Save registered items
		return save_regist(hWnd);

	case WM_ITEM_TO_CLIPBOARD:
		// Set data to clipboard
		if (lParam == 0) {
			return FALSE;
		}
		return item_to_clipboard(hWnd, (DATA_INFO *)lParam, (wParam == 0) ? TRUE : FALSE);

	case WM_ITEM_CREATE:
		// Create item
		switch (wParam) {
		case TYPE_DATA:
			// Create data
			if (lParam == 0) {
				return 0;
			}
			return (LRESULT)data_create_data(0, (TCHAR *)lParam, NULL, 0, TRUE, NULL);
		case TYPE_ITEM:
			// Create item
			return (LRESULT)data_create_item((TCHAR *)lParam, TRUE, NULL);
		case TYPE_FOLDER:
			// Create folder
			if (lParam == 0) {
				return 0;
			}
			return (LRESULT)data_create_folder((TCHAR *)lParam, NULL);
		}
		return 0;

	case WM_ITEM_COPY:
		// Copy item
		if (lParam == 0) {
			return 0;
		}
		return (LRESULT)data_item_copy((DATA_INFO *)lParam, (BOOL)wParam, FALSE, NULL);

	case WM_ITEM_FREE:
		// Free item
		if ((DATA_INFO *)lParam == &history_data ||
			(DATA_INFO *)lParam == &regist_data) {
			break;
		}
		data_free((DATA_INFO *)lParam);
		break;

	case WM_ITEM_FREE_DATA:
		// Free data
		if (wParam == 0 || lParam == 0) {
			break;
		}
		if (format_free_data((TCHAR *)wParam, (HANDLE)lParam) == FALSE) {
			clipboard_free_data((TCHAR *)wParam, (HANDLE)lParam);
		}
		break;

	case WM_ITEM_CHECK:
		// Check item existence
		if (data_check(&history_data, (DATA_INFO *)lParam) != NULL) {
			return 0;
		}
		if (data_check(&regist_data, (DATA_INFO *)lParam) != NULL) {
			return 1;
		}
		return -1;

	case WM_ITEM_TO_BYTES:
		// Get byte array from item
		if (lParam != 0) {
			BYTE *ret;

			if ((ret = format_data_to_bytes((DATA_INFO *)lParam, (DWORD *)wParam)) == NULL) {
				ret = clipboard_data_to_bytes((DATA_INFO *)lParam, (DWORD *)wParam);
			}
			return (LRESULT)ret;
		}
		break;

	case WM_ITEM_FROM_BYTES:
		// Create data from byte array and set to item
		if (lParam == 0) {
			break;
		}
		if (((DATA_INFO *)lParam)->data != NULL) {
			if (format_free_data(((DATA_INFO *)lParam)->format_name, ((DATA_INFO *)lParam)->data) == FALSE) {
				clipboard_free_data(((DATA_INFO *)lParam)->format_name, ((DATA_INFO *)lParam)->data);
			}
		}
		if ((((DATA_INFO *)lParam)->data = format_bytes_to_data(((DATA_INFO *)lParam)->format_name,
			(BYTE *)wParam, &((DATA_INFO *)lParam)->size)) == NULL) {

			((DATA_INFO *)lParam)->data = clipboard_bytes_to_data(((DATA_INFO *)lParam)->format_name,
				(BYTE *)wParam, &((DATA_INFO *)lParam)->size);
		}
		break;

	case WM_ITEM_TO_FILE:
		// Save item to file
		if (lParam != 0) {
			TCHAR err_str[BUF_SIZE];

			*err_str = TEXT('\0');
			if (format_data_to_file((DATA_INFO *)lParam, (TCHAR *)wParam, 0, err_str) == FALSE) {
				if (*err_str != TEXT('\0')) {
					return FALSE;
				}
				if (clipboard_data_to_file((DATA_INFO *)lParam, (TCHAR *)wParam, 0, err_str) == FALSE && *err_str != TEXT('\0')) {
					return FALSE;
				}
			}
			return TRUE;
		}
		return FALSE;

	case WM_ITEM_FROM_FILE:
		// Create data from file and set to item
		if (lParam != 0) {
			TCHAR err_str[BUF_SIZE];

			if (((DATA_INFO *)lParam)->data != NULL) {
				if (format_free_data(((DATA_INFO *)lParam)->format_name, ((DATA_INFO *)lParam)->data) == FALSE) {
					clipboard_free_data(((DATA_INFO *)lParam)->format_name, ((DATA_INFO *)lParam)->data);
				}
			}
			*err_str = TEXT('\0');
			if ((((DATA_INFO *)lParam)->data = format_file_to_data((TCHAR *)wParam,
				((DATA_INFO *)lParam)->format_name,
				&((DATA_INFO *)lParam)->size, err_str)) == NULL) {
				if (*err_str != TEXT('\0')) {
					return FALSE;
				}
				if ((((DATA_INFO *)lParam)->data = clipboard_file_to_data((TCHAR *)wParam,
					((DATA_INFO *)lParam)->format_name,
					&((DATA_INFO *)lParam)->size, err_str)) == NULL && *err_str != TEXT('\0')) {
					return FALSE;
				}
			}
			return TRUE;
		}
		return FALSE;

	case WM_ITEM_GET_PARENT:
		// Get parent item
		{
			DATA_INFO *di;

			if ((di = data_check(&history_data, (DATA_INFO *)lParam)) != NULL) {
				return (LRESULT)di;
			}
			if ((di = data_check(&regist_data, (DATA_INFO *)lParam)) != NULL) {
				return (LRESULT)di;
			}
		}
		return (LRESULT)NULL;

	case WM_ITEM_GET_FORMAT_TO_ITEM:
		// Get item from format name
		if (lParam != 0 && wParam != 0) {
			DATA_INFO *di = (DATA_INFO *)lParam;

			if (di->type == TYPE_ITEM) {
				for (di = di->child; di != NULL && lstrcmpi(di->format_name, (TCHAR *)wParam) != 0; di = di->next)
					;
				return (LRESULT)di;

			} else if (di->type == TYPE_DATA) {
				if (lstrcmpi(di->format_name, (TCHAR *)wParam) != 0) {
					return (LRESULT)di;
				}
			}
		}
		return (LRESULT)NULL;

	case WM_ITEM_GET_PRIORITY_HIGHEST:
		// Select item of higher priority format
		return (LRESULT)format_get_priority_highest((DATA_INFO *)lParam);

	case WM_ITEM_GET_TITLE:
		// Get item title
		if (lParam != 0) {
			DATA_INFO *di;

			di = format_get_priority_highest((DATA_INFO *)lParam);
			data_menu_free_item(di);
			// Get title to display in menu
			format_get_menu_title(di);
			// Get icon to display in menu
			format_get_menu_icon(di);
			// Get bitmap to display in menu
			format_get_menu_bitmap(di);

			if ((TCHAR *)wParam != NULL) {
				lstrcpy((TCHAR *)wParam, data_get_title(di));
			}
		}
		break;

	case WM_ITEM_GET_OPEN_INFO:
		// Item open information
		return format_get_file_info((TCHAR *)lParam, NULL, (OPENFILENAME *)wParam, TRUE);

	case WM_ITEM_GET_SAVE_INFO:
		// Item save information
		return format_get_file_info(((DATA_INFO *)lParam)->format_name, (DATA_INFO *)lParam,
			(OPENFILENAME *)wParam, FALSE);

	case WM_VIEWER_SHOW:
		// Show viewer
		if (hViewerWnd != NULL && hViewerWnd != (HWND)-1) {
			if (IsIconic(hViewerWnd) != 0) {
				ShowWindow(hViewerWnd, SW_RESTORE);
			}
			ShowWindow(hViewerWnd, SW_SHOW);
			_SetForegroundWindow(hViewerWnd);
		} else {
			_SetForegroundWindow(hWnd);
			hViewerWnd = (HWND)-1;
			hViewerWnd = viewer_create(hWnd, SW_SHOW);
			_SetForegroundWindow(hViewerWnd);
		}
		if (lParam != 0 && hViewerWnd != NULL && hViewerWnd != (HWND)-1) {
			SendMessage(hViewerWnd, WM_VIEWER_SELECT_ITEM, 0, lParam);
		}
		return (LRESULT)hViewerWnd;

	case WM_VIEWER_GET_HWND:
		// Get viewer window handle
		return (LRESULT)hViewerWnd;

	case WM_VIEWER_GET_MAIN_HWND:
		// Get main window handle
		return (LRESULT)hWnd;

	case WM_VIEWER_GET_SELECTION:
		// Get selected item
		if (hViewerWnd != NULL) {
			return SendMessage(hViewerWnd, msg, wParam, lParam);
		}
		return (LRESULT)NULL;

	case WM_VIEWER_SELECT_ITEM:
		// Select tree item
		if (hViewerWnd != NULL) {
			return SendMessage(hViewerWnd, msg, wParam, lParam);
		}
		return FALSE;

	default:
		if (msg == WM_TASKBARCREATED) {
			set_tray_icon(hWnd, icon_tray, TEXT(""));
		}
		return DefWindowProc(hWnd, msg, wParam, lParam);
	}
	return 0;
}

/*
 * copy_old_file - migrate old version files
 */
static void copy_old_file()
{
	TCHAR general_ini_path[MAX_PATH];
	TCHAR tmp_path[MAX_PATH];
	TCHAR user_name[BUF_SIZE];
	TCHAR buf[BUF_SIZE];
	DWORD i;

	lstrcpy(tmp_path, app_path);

	// Get current logged-in user name
	i = BUF_SIZE - 1;
	if (GetUserName(user_name, &i) == FALSE) {
		lstrcpy(user_name, DEFAULT_USER);
	}

	// Get user name when using shared settings
	wsprintf(general_ini_path, TEXT("%s\\%s"), app_path, GENERAL_INI);
	if (PathFileExists(general_ini_path) == FALSE) {
		if (!SUCCEEDED(SHGetFolderPath(NULL, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, NULL, 0, tmp_path))) {
			return;
		}
		lstrcat(tmp_path, TEXT("\\VirtualStore\\Program Files (x86)\\CLCL"));
		wsprintf(general_ini_path, TEXT("%s\\%s"), tmp_path, GENERAL_INI);
		if (PathFileExists(general_ini_path) == FALSE) {
			return;
		}
	}

	profile_initialize(general_ini_path, TRUE);
	profile_get_string(TEXT("GENERAL"), TEXT("User"), TEXT(""), buf, BUF_SIZE - 1, general_ini_path);
	profile_write_string(TEXT("GENERAL"), TEXT("User"), buf, general_ini_path);
	if (*buf != TEXT('\0')) {
		lstrcpy(user_name, buf);
	}
	profile_get_string(TEXT("GENERAL"), TEXT("WorkDir"), TEXT(""), buf, BUF_SIZE - 1, general_ini_path);
	profile_write_string(TEXT("GENERAL"), TEXT("WorkDir"), buf, general_ini_path);
	if (*buf != TEXT('\0')) {
		lstrcpy(tmp_path, buf);
	}
	profile_flush(general_ini_path);
	profile_free();

	file_name_conv(user_name, TEXT('_'));

	TCHAR old_path[MAX_PATH];
	wsprintf(old_path, TEXT("%s\\%s"), tmp_path, user_name);

	TCHAR old_file[MAX_PATH];
	TCHAR new_file[MAX_PATH];

	wsprintf(old_file, TEXT("%s\\%s"), old_path, USER_INI);
	wsprintf(new_file, TEXT("%s\\%s"), work_path, USER_INI);
	CopyFile(old_file, new_file, TRUE);

	wsprintf(old_file, TEXT("%s\\%s"), old_path, HISTORY_FILENAME);
	wsprintf(new_file, TEXT("%s\\%s"), work_path, HISTORY_FILENAME);
	CopyFile(old_file, new_file, TRUE);

	wsprintf(old_file, TEXT("%s\\%s"), old_path, REGIST_FILENAME);
	wsprintf(new_file, TEXT("%s\\%s"), work_path, REGIST_FILENAME);
	CopyFile(old_file, new_file, TRUE);
}

/*
 * get_work_path - create working directory
 */
static void get_work_path(const HINSTANCE hInstance)
{
	TCHAR *p, *r;

	// Get application path
	GetModuleFileName(hInstance, app_path, MAX_PATH - 1);
	for (p = r = app_path; *p != TEXT('\0'); p++) {
#ifndef UNICODE
		if (IsDBCSLeadByte((BYTE)*p) == TRUE) {
			p++;
			continue;
		}
#endif	// UNICODE
		if (*p == TEXT('\\') || *p == TEXT('/')) {
			r = p;
		}
	}
	*r = TEXT('\0');

	// 1. Check general.ini (multi-user / profile settings)
	TCHAR general_ini_path[MAX_PATH];
	wsprintf(general_ini_path, TEXT("%s\\%s"), app_path, GENERAL_INI);
	if (PathFileExists(general_ini_path) == TRUE) {
		TCHAR user_name[BUF_SIZE];
		TCHAR base_dir[MAX_PATH];
		TCHAR buf[BUF_SIZE];
		DWORD name_len = BUF_SIZE - 1;

		if (GetUserName(user_name, &name_len) == FALSE) {
			lstrcpy(user_name, DEFAULT_USER);
		}
		lstrcpy(base_dir, app_path);

		profile_initialize(general_ini_path, TRUE);
		profile_get_string(TEXT("GENERAL"), TEXT("User"), TEXT(""), buf, BUF_SIZE - 1, general_ini_path);
		if (*buf != TEXT('\0')) {
			lstrcpy(user_name, buf);
		}
		profile_get_string(TEXT("GENERAL"), TEXT("WorkDir"), TEXT(""), buf, BUF_SIZE - 1, general_ini_path);
		if (*buf != TEXT('\0')) {
			lstrcpy(base_dir, buf);
		}
		profile_free();

		file_name_conv(user_name, TEXT('_'));
		wsprintf(work_path, TEXT("%s\\%s"), base_dir, user_name);
		CreateDirectory(work_path, NULL);
		return;
	}

	// 2. Check for <app_path>\<user_name> directory (prioritize existing profile)
	{
		TCHAR user_name[BUF_SIZE];
		DWORD name_len = BUF_SIZE - 1;
		if (GetUserName(user_name, &name_len) != FALSE) {
			TCHAR candidate_path[MAX_PATH];
			file_name_conv(user_name, TEXT('_'));
			wsprintf(candidate_path, TEXT("%s\\%s"), app_path, user_name);
			if (PathFileExists(candidate_path) == TRUE) {
				lstrcpy(work_path, candidate_path);
				return;
			}
		}
	}

	// 3. Check portable setting in clcl_app.ini
	int portable = 0;
	TCHAR app_ini_path[MAX_PATH];
	wsprintf(app_ini_path, TEXT("%s\\%s"), app_path, APP_INI);
	if (PathFileExists(app_ini_path) == TRUE) {
		profile_initialize(app_ini_path, TRUE);
		portable = profile_get_int(TEXT("GENERAL"), TEXT("portable"), 0, app_ini_path);
		profile_free();
	}
	if (portable == 1) {
		lstrcpy(work_path, app_path);
		return;
	}

	// 4. Default: AppData\Local\CLCL
	{
		BOOL check_old_path = FALSE;
		if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, NULL, 0, work_path))) {
			lstrcat(work_path, TEXT("\\CLCL"));
			if (PathFileExists(work_path) == FALSE) {
				check_old_path = TRUE;
			}
			CreateDirectory(work_path, NULL);
		}
		if (check_old_path) {
			copy_old_file();
		}
	}
}

/*
 * commnad_line_func - command line processing
 */
static void commnad_line_func(const HWND hWnd)
{
	HWND vWnd;
	TCHAR *p;

	if (hWnd == NULL) {
		return;
	}

	p = GetCommandLine();
	// Remove executable file name
    if (*p == TEXT('"')) {
		for (p++; *p != TEXT('\0') && *p != TEXT('"'); p++)
			;
		if (*p != TEXT('\0')) {
			p++;
		}
	} else {
		for (; *p != TEXT('\0') && *p != TEXT(' '); p++)
			;
	}

	for (; *p != TEXT('\0') && *p != TEXT('/') && *p != TEXT('-'); p++)
		;
	if (*p == TEXT('\0')) {
		return;
	}
	for (p++; *p != TEXT('\0'); p++) {
		if (*p == TEXT(' ')) {
			for (; *p != TEXT('\0') && *p != TEXT('/') && *p != TEXT('-'); p++)
				;
			if (*p == TEXT('\0') || *(++p) == TEXT('\0')) {
				break;
			}
		}
		switch (*p) {
		case TEXT('v'): case TEXT('V'):
			// Show viewer
			vWnd = (HWND)SendMessage(hWnd, WM_VIEWER_GET_HWND, 0, 0);
			if (vWnd == NULL) {
				SendMessage(hWnd, WM_VIEWER_SHOW, 0, 0);
			} else {
				if (IsIconic(vWnd) != 0) {
					ShowWindow(vWnd, SW_RESTORE);
				}
				_SetForegroundWindow(vWnd);
			}
			break;

		case TEXT('w'): case TEXT('W'):
			// Clipboard monitoring
			SendMessage(hWnd, WM_SET_CLIPBOARD_WATCH, 1, 0);
			break;

		case TEXT('n'): case TEXT('N'):
			// Stop clipboard monitoring
			SendMessage(hWnd, WM_SET_CLIPBOARD_WATCH, 0, 0);
			break;

		case TEXT('x'): case TEXT('X'):
			// Exit
			SendMessage(hWnd, WM_CLOSE, 0, 0);
			break;
		}
	}
}

/*
 * init_application - register window class
 */
static BOOL init_application(const HINSTANCE hInstance)
{
	WNDCLASS wc;

	wc.style = 0;
	wc.lpfnWndProc = (WNDPROC)main_proc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = hInstance;
	wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON_MAIN));
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
	wc.lpszMenuName = NULL;
	wc.lpszClassName = MAIN_WND_CLASS;
	// Register window class
	return RegisterClass(&wc) && pinned_image_regist(hInstance);
}

/*
 * init_instance - create window
 */
static HWND init_instance(const HINSTANCE hInstance, const int CmdShow)
{
	HWND hWnd;

	// Create window
	hWnd = CreateWindow(MAIN_WND_CLASS,
		MAIN_WINDOW_TITLE,
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		NULL, NULL, hInstance, NULL);
	main_window_handle = hWnd;
	return hWnd;
}

/*
 * WinMain - main entry point
 */
int WINAPI _tWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPTSTR lpCmdLine, int nCmdShow)
{
	HANDLE hMutex = NULL;
	HANDLE hAccel;
	MSG msg;
	TCHAR err_str[BUF_SIZE];
#ifndef _DEBUG
	SECURITY_DESCRIPTOR sd;
	SECURITY_ATTRIBUTES sa;
#endif

	hInst = hInstance;

#ifndef _DEBUG
	// Check for multiple instances
	InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
	SetSecurityDescriptorDacl(&sd, TRUE, 0, FALSE);	    
	sa.nLength = sizeof(SECURITY_ATTRIBUTES);
	sa.lpSecurityDescriptor = &sd;
	sa.bInheritHandle = TRUE; 
	hMutex = CreateMutex(&sa, FALSE, MUTEX);
	if (GetLastError() == ERROR_ALREADY_EXISTS) {
		// Command line processing
		commnad_line_func(FindWindow(MAIN_WND_CLASS, MAIN_WINDOW_TITLE));
		if (hMutex != NULL) {
			CloseHandle(hMutex);
		}
		return 0;
	}
#endif

	// Initialize DPI
	InitDpi();
	// Initialize dark mode
	dark_mode_init();
	// Initialize CommonControls
	InitCommonControls();
	// Initialize OLE
	OleInitialize(NULL);
	// Get settings
	get_work_path(hInstance);
	if (ini_get_option(err_str) == FALSE) {
		MessageBox(NULL, err_str, ERROR_TITLE, MB_ICONERROR);
		if (hMutex != NULL) {
			CloseHandle(hMutex);
		}
		return 0;
	}

	// Register viewer
	if (viewer_regist(hInstance) == FALSE ||
		container_regist(hInstance) == FALSE || binview_regist(hInstance) == FALSE ||
		search_regist(hInstance) == FALSE) {
		MessageBox(NULL, message_get_res(IDS_ERROR_WINDOW_INIT), ERROR_TITLE, MB_ICONERROR);
		if (hMutex != NULL) {
			CloseHandle(hMutex);
		}
		return 0;
	}
	// Create main window
	if (tooltip_regist(hInstance) == FALSE ||
		init_application(hInstance) == FALSE || init_instance(hInstance, nCmdShow) == NULL) {
		MessageBox(NULL, message_get_res(IDS_ERROR_WINDOW_INIT), ERROR_TITLE, MB_ICONERROR);
		if (hMutex != NULL) {
			CloseHandle(hMutex);
		}
		return 0;
	}
	hAccel = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDR_ACCELERATOR));
	// Window message processing
	while (GetMessage(&msg, NULL, 0, 0) == TRUE) {
		if (accel_flag == TRUE && hViewerWnd != NULL && hViewerWnd == GetForegroundWindow() &&
			(TranslateAccelerator(hViewerWnd, hAccel, &msg) == TRUE)) {
			continue;
		}
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	// Free settings
	ini_free();
	dark_mode_free();
	OleUninitialize();
	if (hMutex != NULL) {
		CloseHandle(hMutex);
	}
#ifdef _DEBUG
	mem_debug();
#endif
	return msg.wParam;
}
/* End of source */
