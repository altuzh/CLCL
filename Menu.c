/*
 * CLCL
 *
 * Menu.c
 *
 * Copyright (C) 1996-2019 by Ohno Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 *		nakka@nakka.com
 */

/* Include Files */
#define _INC_OLE
#include <windows.h>
#undef  _INC_OLE
#include <tchar.h>

#include "General.h"
#include "Memory.h"
#include "Data.h"
#include "Ini.h"
#include "Message.h"
#include "Menu.h"
#include "Regist.h"
#include "ClipBoard.h"
#include "Format.h"
#include "Font.h"
#include "dpi.h"
#include "DarkMode.h"
#include "DbHistory.h"

#include "resource.h"

/* Define */
// Menu size
#define MENU_TEXT_MARGIN_LEFT		Scale(option.menu_text_margin_left)
#define MENU_TEXT_MARGIN_RIGHT		Scale(option.menu_text_margin_right)
#define MENU_TEXT_MARGIN_Y			Scale(option.menu_text_margin_y)
#define MENU_SEPARATOR_HEIGHT		Scale(option.menu_separator_height)
#define MENU_SEPARATOR_MARGIN_LEFT	Scale(option.menu_separator_margin_left)
#define MENU_SEPARATOR_MARGIN_RIGHT	Scale(option.menu_separator_margin_right)
#define MENU_MAX_WIDTH				Scale(option.menu_max_width)
#define MENU_ICON_SIZE				Scale(option.menu_icon_size)
#define MENU_ICON_MARGIN			Scale(option.menu_icon_margin)
#define MENU_BITMAP_WIDTH			Scale(option.menu_bitmap_width)
#define MENU_BITMAP_HEIGHT			Scale(option.menu_bitmap_height)

/* Global Variables */
static MENU_ITEM_INFO *menu_item_info;
static int menu_item_cnt;

#ifdef OP_XP_STYLE
// Menu visual styles
typedef HTHEME (WINAPI *OPENTHEMEDATA_PROC)(HWND, LPCWSTR);
typedef HRESULT (WINAPI *CLOSETHEMEDATA_PROC)(HTHEME);
typedef HRESULT (WINAPI *DRAWTHEMEBACKGROUND_PROC)(HTHEME, HDC, int, int, const RECT *, const RECT *);
typedef HRESULT (WINAPI *GETTHEMECOLOR_PROC)(HTHEME, int, int, int, COLORREF *);
typedef HRESULT (WINAPI *GETTHEMEPARTSIZE_PROC)(HTHEME, HDC, int, int, RECT *, int, SIZE *);
typedef BOOL (WINAPI *ISTHEMEACTIVE_PROC)(VOID);
typedef BOOL (WINAPI *ISTHEMEPARTDEFINED_PROC)(HTHEME, int, int);

static HMODULE menu_theme_lib;
static HTHEME menu_theme;

static OPENTHEMEDATA_PROC _MenuOpenThemeData;
static CLOSETHEMEDATA_PROC _MenuCloseThemeData;
static DRAWTHEMEBACKGROUND_PROC _MenuDrawThemeBackground;
static GETTHEMECOLOR_PROC _MenuGetThemeColor;
static GETTHEMEPARTSIZE_PROC _MenuGetThemePartSize;
static ISTHEMEACTIVE_PROC _MenuIsThemeActive;
static ISTHEMEPARTDEFINED_PROC _MenuIsThemePartDefined;
#endif	// OP_XP_STYLE

// Monitor rectangle to display menu
static RECT menu_monitor_rect;
static RECT menu_work_rect;

// Default icon to display in menu
static HICON menu_icon_default;
static HICON menu_icon_folder;
static int menu_icon_load_size;

extern HINSTANCE hInst;

// Options
extern OPTION_INFO option;
extern DATA_INFO regist_data;

/* Local Function Prototypes */
static void menu_item_free(MENU_ITEM_INFO *mii, int cnt);
static void menu_load_icons(void);
static void menu_get_show_point(const POINT *mpos, POINT *ret);
static MENU_ITEM_INFO *menu_id_to_menuitem(MENU_ITEM_INFO *mii, const int mcnt, const UINT id);
static HICON menu_read_icon(const TCHAR *file_name, const int index, const int icon_size);
static HFONT menu_create_font(void);
static int menu_get_item_size(const MENU_ITEM_INFO *mii, int *width);
static void menu_create_text(const int index, const TCHAR *buf, TCHAR *ret);
static BOOL menu_create_datainfo(DATA_INFO *set_di,
								MENU_ITEM_INFO *mii, int menu_index, int *id,
								const int step, const int min, const int max);
static MENU_ITEM_INFO *menu_create_info(MENU_INFO *menu_info, const int menu_cnt,
								DATA_INFO *history_di, DATA_INFO *regist_di, int *id, int *ret_cnt);
static BOOL menu_set_item(const HDC hdc, const HMENU hMenu, MENU_ITEM_INFO *mii, const int cnt);
static int menu_draw_bitmap(const HDC draw_dc, const DATA_INFO *di, const int height);
static BOOL menu_draw_ckeck(const HDC draw_dc, const int left, const int top, const int right, const int bottom);
static int menu_get_arrow_size(void);
static void menu_draw_arrow(const HDC draw_dc, const RECT *rect, const COLORREF color);
static TCHAR menu_get_accelerator(TCHAR *str);
#ifdef OP_XP_STYLE
static void menu_theme_open(const HWND hWnd);
static void menu_theme_close(void);
static COLORREF menu_theme_text_color(const int state_id, const COLORREF default_color);
static BOOL menu_draw_check_theme(const HDC draw_dc, const int left, const int top, const int right, const int bottom);
#endif	// OP_XP_STYLE

/*
 * menu_item_free - free menu information
 */
static void menu_item_free(MENU_ITEM_INFO *mii, int cnt)
{
	int i;

	if (mii == NULL) {
		return;
	}
	for (i = 0; i < cnt; i++) {
		if ((mii + i)->mii != NULL) {
			menu_item_free((mii + i)->mii, (mii + i)->mii_cnt);
		}
		mem_free(&((mii + i)->text));
		mem_free(&((mii + i)->hkey));
		if ((mii + i)->free_icon == TRUE && (mii + i)->icon != NULL) {
			DestroyIcon((mii + i)->icon);
		}
	}
	mem_free(&mii);
}

/*
 * menu_free - free menu information
 */
void menu_free(void)
{
	menu_item_free(menu_item_info, menu_item_cnt);
	menu_item_info = NULL;
	menu_item_cnt = 0;
#ifdef OP_XP_STYLE
	menu_theme_close();
#endif	// OP_XP_STYLE
}

/*
 * menu_free_icons - free default icons displayed in menu
 */
void menu_free_icons(void)
{
	if (menu_icon_default != NULL) {
		DestroyIcon(menu_icon_default);
		menu_icon_default = NULL;
	}
	if (menu_icon_folder != NULL) {
		DestroyIcon(menu_icon_folder);
		menu_icon_folder = NULL;
	}
	menu_icon_load_size = 0;
}

/*
 * menu_load_icons - load default icons displayed in menu
 */
static void menu_load_icons(void)
{
	int icon_size = MENU_ICON_SIZE;

	if (menu_icon_load_size == icon_size) {
		return;
	}
	menu_free_icons();
	menu_icon_default = (HICON)LoadImage(hInst, MAKEINTRESOURCE(IDI_ICON_DEFAULT),
		IMAGE_ICON, icon_size, icon_size, 0);
	menu_icon_folder = (HICON)LoadImage(hInst, MAKEINTRESOURCE(IDI_ICON_FOLDER),
		IMAGE_ICON, icon_size, icon_size, 0);
	menu_icon_load_size = icon_size;
}

#ifdef OP_XP_STYLE
/*
 * menu_theme_open - open visual style theme for menu
 */
static void menu_theme_open(const HWND hWnd)
{
	menu_theme_close();

	if (menu_theme_lib == NULL) {
		if ((menu_theme_lib = LoadLibrary(TEXT("uxtheme.dll"))) == NULL) {
			return;
		}
		_MenuOpenThemeData = (OPENTHEMEDATA_PROC)GetProcAddress(menu_theme_lib, "OpenThemeData");
		_MenuCloseThemeData = (CLOSETHEMEDATA_PROC)GetProcAddress(menu_theme_lib, "CloseThemeData");
		_MenuDrawThemeBackground = (DRAWTHEMEBACKGROUND_PROC)GetProcAddress(menu_theme_lib, "DrawThemeBackground");
		_MenuGetThemeColor = (GETTHEMECOLOR_PROC)GetProcAddress(menu_theme_lib, "GetThemeColor");
		_MenuGetThemePartSize = (GETTHEMEPARTSIZE_PROC)GetProcAddress(menu_theme_lib, "GetThemePartSize");
		_MenuIsThemeActive = (ISTHEMEACTIVE_PROC)GetProcAddress(menu_theme_lib, "IsThemeActive");
		_MenuIsThemePartDefined = (ISTHEMEPARTDEFINED_PROC)GetProcAddress(menu_theme_lib, "IsThemePartDefined");
	}
	if (_MenuOpenThemeData == NULL || _MenuCloseThemeData == NULL ||
		_MenuDrawThemeBackground == NULL || _MenuGetThemeColor == NULL ||
		_MenuGetThemePartSize == NULL || _MenuIsThemeActive == NULL ||
		_MenuIsThemePartDefined == NULL) {
		return;
	}
#ifdef MENU_COLOR
	// Use custom drawing if color settings are present
	if (*option.menu_color_back.color_str != TEXT('\0') ||
		*option.menu_color_text.color_str != TEXT('\0') ||
		*option.menu_color_highlight.color_str != TEXT('\0') ||
		*option.menu_color_highlighttext.color_str != TEXT('\0') ||
		*option.menu_color_3d_shadow.color_str != TEXT('\0') ||
		*option.menu_color_3d_highlight.color_str != TEXT('\0')) {
		return;
	}
#endif	// MENU_COLOR
	if (_MenuIsThemeActive() == FALSE) {
		return;
	}
	// Dark mode color scheme is handled by custom drawing
	if (dark_mode_is_dark() == TRUE) {
		return;
	}
	if ((menu_theme = _MenuOpenThemeData(hWnd, L"MENU")) == NULL) {
		return;
	}
	// Check if popup menu parts are defined (Vista and later)
	if (_MenuIsThemePartDefined(menu_theme, MENU_POPUPITEM, 0) == FALSE) {
		_MenuCloseThemeData(menu_theme);
		menu_theme = NULL;
	}
}

/*
 * menu_theme_close - close visual style theme for menu
 */
static void menu_theme_close(void)
{
	if (menu_theme != NULL) {
		_MenuCloseThemeData(menu_theme);
		menu_theme = NULL;
	}
}

/*
 * menu_theme_text_color - get menu text color from theme
 */
static COLORREF menu_theme_text_color(const int state_id, const COLORREF default_color)
{
	COLORREF color;

	if (menu_theme == NULL ||
		_MenuGetThemeColor(menu_theme, MENU_POPUPITEM, state_id, TMT_TEXTCOLOR, &color) != S_OK) {
		return default_color;
	}
	return color;
}

/*
 * menu_draw_check_theme - draw menu check mark with theme
 */
static BOOL menu_draw_check_theme(const HDC draw_dc, const int left, const int top, const int right, const int bottom)
{
	RECT rect;
	SIZE size;

	if (menu_theme == NULL) {
		return FALSE;
	}
	SetRect(&rect, left, top, right, bottom);
	_MenuDrawThemeBackground(menu_theme, draw_dc, MENU_POPUPCHECKBACKGROUND, MCB_NORMAL, &rect, NULL);
	if (_MenuGetThemePartSize(menu_theme, draw_dc, MENU_POPUPCHECK, MC_CHECKMARKNORMAL, NULL, TS_TRUE, &size) == S_OK &&
		size.cx <= right - left && size.cy <= bottom - top) {
		rect.left = left + ((right - left) - size.cx) / 2;
		rect.top = top + ((bottom - top) - size.cy) / 2;
		rect.right = rect.left + size.cx;
		rect.bottom = rect.top + size.cy;
	}
	_MenuDrawThemeBackground(menu_theme, draw_dc, MENU_POPUPCHECK, MC_CHECKMARKNORMAL, &rect, NULL);
	return TRUE;
}
#endif	// OP_XP_STYLE

/*
 * menu_get_show_point - get menu display position
 */
static void menu_get_show_point(const POINT *mpos, POINT *ret)
{
	RECT vrect;

	// Rectangle of entire virtual screen
	SetRect(&vrect,
		GetSystemMetrics(SM_XVIRTUALSCREEN),
		GetSystemMetrics(SM_YVIRTUALSCREEN),
		GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
		GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN));

	if (mpos == NULL || mpos->x < vrect.left || mpos->x > vrect.right ||
		mpos->y < vrect.top || mpos->y > vrect.bottom) {
		GetCursorPos(ret);
	} else {
		*ret = *mpos;
	}
}

/*
 * menu_set_dpi - set DPI for monitor displaying menu
 */
void menu_set_dpi(const POINT *mpos)
{
	POINT apos;
	HMONITOR hMonitor;
	MONITORINFO mi;

	menu_get_show_point(mpos, &apos);
	SetDpiFromPoint(apos);
	GetMonitorRectFromPoint(apos, &menu_monitor_rect);

	hMonitor = MonitorFromPoint(apos, MONITOR_DEFAULTTONEAREST);
	mi.cbSize = sizeof(MONITORINFO);
	if (hMonitor != NULL && GetMonitorInfo(hMonitor, &mi) != FALSE) {
		menu_work_rect = mi.rcWork;
	} else {
		menu_work_rect = menu_monitor_rect;
	}
}

/*
 * menu_show_align - display menu with specified alignment flags
 */
int menu_show_align(const HWND hWnd, const HMENU hMenu, const POINT *mpos, const UINT align_flags)
{
	POINT apos;
	DWORD ret;

	menu_get_show_point(mpos, &apos);
	ret = TrackPopupMenu(hMenu,
		TPM_LEFTBUTTON | TPM_RIGHTBUTTON | TPM_RETURNCMD | align_flags,
		apos.x, apos.y, 0, hWnd, NULL);
	PostMessage(hWnd, WM_NULL, 0, 0);
	return ret;
}

/*
 * menu_show - display menu at mouse position
 */
int menu_show(const HWND hWnd, const HMENU hMenu, const POINT *mpos)
{
	return menu_show_align(hWnd, hMenu, mpos, TPM_TOPALIGN | TPM_LEFTALIGN);
}

/*
 * menu_id_to_menuitem - find menu information from menu ID
 */
static MENU_ITEM_INFO *menu_id_to_menuitem(MENU_ITEM_INFO *mii, const int mcnt, const UINT id)
{
	MENU_ITEM_INFO *ret;
	int i;

	if (mii == NULL) {
		return NULL;
	}
	for (i = 0; i < mcnt; i++) {
		if ((mii + i)->mii != NULL) {
			ret = menu_id_to_menuitem((mii + i)->mii, (mii + i)->mii_cnt, id);
			if (ret != NULL) {
				return ret;
			}
		}
		if ((mii + i)->id == id) {
			return (mii + i);
		}
	}
	return NULL;
}

/*
 * menu_get_info - get menu information from menu ID
 */
MENU_ITEM_INFO *menu_get_info(const UINT id)
{
	return menu_id_to_menuitem(menu_item_info, menu_item_cnt, id);
}

/*
 * menu_read_icon - get icon
 */
static HICON menu_read_icon(const TCHAR *file_name, const int index, const int icon_size)
{
	SHFILEINFO shfi;
	HICON hIcon = NULL;
	HICON hsIcon = NULL;
	int icon_flag;
	BOOL large_icon;

	if (file_name == NULL || *file_name == TEXT('\0')) {
		return NULL;
	}
	// expand environment variables in file_name
	TCHAR expanded_name[MAX_PATH + 1];
	DWORD ret = 0;
	if ((ret = ExpandEnvironmentStrings(file_name, expanded_name, MAX_PATH)) == 0 || ret > MAX_PATH)
		return NULL;
	large_icon = (icon_size > GetSystemMetricsDpi(SM_CXSMICON)) ? TRUE : FALSE;

	// Get icon from file
	// get icon from file
	ExtractIconEx(expanded_name, index, &hIcon, &hsIcon, 1);
	if (large_icon == TRUE) {
		if (hsIcon != NULL) {
			DestroyIcon(hsIcon);
		}
	} else {
		if (hIcon != NULL) {
			DestroyIcon(hIcon);
		}
		hIcon = hsIcon;
	}
	if (hIcon == NULL) {
		// Get icon from association
		// get icon from file association
		icon_flag = SHGFI_ICON | ((large_icon == TRUE) ? SHGFI_LARGEICON : SHGFI_SMALLICON);
		SHGetFileInfo(expanded_name, SHGFI_USEFILEATTRIBUTES, &shfi, sizeof(SHFILEINFO), icon_flag);
		hIcon = shfi.hIcon;
	}
	return hIcon;
}

/*
 * menu_create_font - create font for menu
 */
static HFONT menu_create_font(void)
{
	NONCLIENTMETRICS ncMetrics;

	if (*option.menu_font_name != TEXT('\0')) {
		return font_create(option.menu_font_name, option.menu_font_size, option.menu_font_charset,
			option.menu_font_weight, (option.menu_font_italic == 0) ? FALSE : TRUE, FALSE);
	}

	if (GetNonClientMetricsDpi(&ncMetrics) == FALSE) {
		return NULL;
	}
	return CreateFontIndirect(&ncMetrics.lfMenuFont);
}

/*
 * menu_get_item_size - get owner-drawn menu item size
 */
static int menu_get_item_size(const MENU_ITEM_INFO *mii, int *width)
{
	int text_x, text_y;
	int bmp_x, bmp_y;
	int ret_x, ret_y;

	text_x = mii->text_x;
	text_y = mii->text_y;

	if (mii->flag & MF_SEPARATOR) {
		// Separator
		ret_x = 0;
		ret_y = MENU_SEPARATOR_HEIGHT;

	} else if (option.menu_show_icon != 1) {
		// Text only
		text_x += (MENU_ICON_MARGIN + MENU_ICON_SIZE + MENU_TEXT_MARGIN_LEFT + MENU_TEXT_MARGIN_RIGHT);
		ret_x = (text_x > MENU_MAX_WIDTH) ? MENU_MAX_WIDTH : text_x;

		text_y += (MENU_TEXT_MARGIN_Y * 2);
		ret_y = text_y;

	} else if (mii->show_bitmap == TRUE) {
		// Show bitmap
		if (mii->show_di->menu_bmp_width == 0 && mii->show_di->menu_bmp_height == 0) {
			bmp_x = MENU_BITMAP_WIDTH;
			bmp_y = MENU_BITMAP_HEIGHT;
		} else {
			bmp_x = mii->show_di->menu_bmp_width;
			bmp_y = mii->show_di->menu_bmp_height;
		}
		text_x += (MENU_ICON_MARGIN + bmp_x + MENU_TEXT_MARGIN_LEFT + MENU_TEXT_MARGIN_RIGHT);
		ret_x = (text_x > MENU_MAX_WIDTH) ? MENU_MAX_WIDTH : text_x;

		text_y += (MENU_TEXT_MARGIN_Y * 2);
		ret_y = (bmp_y + (MENU_ICON_MARGIN * 2) > text_y)
			? bmp_y + (MENU_ICON_MARGIN * 2) : text_y;

	} else {
		// Display icon
		text_x += (MENU_ICON_MARGIN + MENU_ICON_SIZE + MENU_TEXT_MARGIN_LEFT + MENU_TEXT_MARGIN_RIGHT);
		ret_x = (text_x > MENU_MAX_WIDTH) ? MENU_MAX_WIDTH : text_x;

		text_y += (MENU_TEXT_MARGIN_Y * 2);
		ret_y = (text_y > MENU_ICON_MARGIN + MENU_ICON_SIZE + MENU_ICON_MARGIN)
			? text_y : (MENU_ICON_MARGIN + MENU_ICON_SIZE + MENU_ICON_MARGIN);
	}
	if (width != NULL) {
		*width = ret_x;
	}
	return ret_y;
}

/*
 * menu_create_text - create string with accelerator
 */
static void menu_create_text(const int index, const TCHAR *buf, TCHAR *ret)
{
	TCHAR *p = option.menu_text_format;
	TCHAR *r;
	int base;
	int num;
	int i;

	if (*p == TEXT('\0')) {
		lstrcpy(ret, buf);
		return;
	}
	while (*p != TEXT('\0')) {
#ifndef UNICODE
		if (IsDBCSLeadByte((BYTE)*p) == TRUE) {
			*(ret++) = *(p++);
			if (*p == TEXT('\0')) {
				break;
			}
			*(ret++) = *(p++);
			continue;
		}
#endif	// UNICODE
		if (*p != TEXT('%')) {
			*(ret++) = *(p++);
			continue;
		}
		r = p;
		p++;

		// Base value
		if (*p >= TEXT('0') && *p <= TEXT('9')) {
			base = _ttoi(p);
			for (; *p >= TEXT('0') && *p <= TEXT('9'); p++)
				;
		} else {
			base = 0;
		}
		num = index + base;

		switch (*p) {
		case TEXT('d'):
		case TEXT('D'):
			// Digits (decimal)
			_itot_s(num, ret, BUF_SIZE, 10);
			ret += lstrlen(ret);
			break;

		case TEXT('x'):
			// Digits (hexadecimal) (lowercase)
			_itot_s(num, ret, BUF_SIZE, 16);
			CharLower(ret);
			ret += lstrlen(ret);
			break;

		case TEXT('X'):
			// Digits (hexadecimal)
			_itot_s(num, ret, BUF_SIZE, 16);
			CharUpper(ret);
			ret += lstrlen(ret);
			break;

		case TEXT('n'):
		case TEXT('N'):
			// 1-digit number
			*(ret++) = TEXT('0') + num % 10;
			break;

		case TEXT('a'):
			// Alphabet (lowercase)
			*(ret++) = TEXT('a') + num % 26;
			break;

		case TEXT('A'):
			// Alphabet
			*(ret++) = TEXT('A') + num % 26;
			break;

		case TEXT('b'):
			// Alphabet + numbers (lowercase)
			i = num % (26 + 10);
			*(ret++) = (i < 26) ? TEXT('a') + i : TEXT('0') + i - 26;
			break;

		case TEXT('B'):
			// Alphabet + numbers
			i = num % (26 + 10);
			*(ret++) = (i < 26) ? TEXT('A') + i : TEXT('0') + i - 26;
			break;

		case TEXT('c'):
			// Digits + alphabet (lowercase)
			i = num % (26 + 10);
			*(ret++) = (i < 10) ? TEXT('0') + i : TEXT('a') + i - 10;
			break;

		case TEXT('C'):
			// Digits + alphabet
			i = num % (26 + 10);
			*(ret++) = (i < 10) ? TEXT('0') + i : TEXT('A') + i - 10;
			break;

		case TEXT('t'):
		case TEXT('T'):
			// Title
			lstrcpyn(ret, buf, BUF_SIZE);
			ret += lstrlen(ret);
			break;

		case TEXT('%'):
			// %
			*(ret++) = *p;
			break;

		default:
			*(ret++) = *r;
			p = r;
			break;

		}
		if (*p == TEXT('\0')) {
			break;
		}
		p++;
	}
	*ret = TEXT('\0');
}

/*
 * menu_get_keyname - get key name
 */
TCHAR *menu_get_keyname(const UINT modifiers, const UINT virtkey)
{
	TCHAR buf[BUF_SIZE];
	UINT scan_code;
	int ext_flag = 0;

	*buf = TEXT('\0');
	if (modifiers & MOD_CONTROL) {
		lstrcat(buf, TEXT("Ctrl+"));
	}
	if (modifiers & MOD_SHIFT) {
		lstrcat(buf, TEXT("Shift+"));
	}
	if (modifiers & MOD_ALT) {
		lstrcat(buf, TEXT("Alt+"));
	}
	if (modifiers & MOD_WIN) {
		lstrcat(buf, TEXT("Win+"));
	}
	if (virtkey == 0 || (scan_code = MapVirtualKey(virtkey, 0)) <= 0) {
		// None
		return NULL;
	}
	if (virtkey == VK_APPS ||
		virtkey == VK_PRIOR ||
		virtkey == VK_NEXT ||
		virtkey == VK_END ||
		virtkey == VK_HOME ||
		virtkey == VK_LEFT ||
		virtkey == VK_UP ||
		virtkey == VK_RIGHT ||
		virtkey == VK_DOWN ||
		virtkey == VK_INSERT ||
		virtkey == VK_DELETE ||
		virtkey == VK_NUMLOCK) {
		ext_flag = 1 << 24;
	}
	GetKeyNameText((scan_code << 16) | ext_flag, buf + lstrlen(buf), BUF_SIZE - lstrlen(buf) - 1);
	return alloc_copy(buf);
}

/*
 * menu_mark_folder_children - recursively mark all child items inside a folder menu
 */
static void menu_mark_folder_children(MENU_ITEM_INFO *items, const int count)
{
	int k;

	if (items == NULL) {
		return;
	}
	for (k = 0; k < count; k++) {
		(items + k)->is_folder_child = TRUE;
		if ((items + k)->mii != NULL && (items + k)->mii_cnt > 0) {
			menu_mark_folder_children((items + k)->mii, (items + k)->mii_cnt);
		}
	}
}

/*
 * menu_create_datainfo - expand data into menu information
 */
static BOOL menu_create_datainfo(DATA_INFO *set_di,
								MENU_ITEM_INFO *mii, int menu_index, int *id,
								const int step, const int min, const int max)
{
	DATA_INFO *di;
	DATA_INFO *cdi;
	MENU_ITEM_INFO *cmi;
	TCHAR buf[BUF_SIZE * 2];
	TCHAR tmp[BUF_SIZE];
	TCHAR *p;
	int cnt;
	int i, j;
	int m, n;

	// Move to initial position
	for (m = 0; set_di != NULL && min > 0 && m < min - 1; set_di = set_di->next, m++)
		;
	if (step < 0) {
		// Descending
		for (di = set_di, i = 0, n = m; di != NULL && (max <= 0 || n < max || di->type == TYPE_FOLDER); di = di->next, i++) {
			if (di->type != TYPE_FOLDER) n++;
		}
		i += menu_index - 1;
	} else {
		// Ascending
		i = menu_index;
	}

	for (di = set_di, j = 0; di != NULL && (max <= 0 || m < max || di->type == TYPE_FOLDER); di = di->next, i += step) {
		if (di->type != TYPE_FOLDER) m++;
		(mii + i)->id = ID_MENUITEM_DATA + ((*id)++);
		(mii + i)->item = (LPCTSTR)(mii + i);
		(mii + i)->set_di = di;
		if (data_check(&regist_data, di) != NULL || di == regist_data.child) {
			(mii + i)->is_favourites = TRUE;
		}

		switch (di->type) {
		case TYPE_FOLDER:
			// Hierarchical display
			(mii + i)->flag = MF_POPUP | MF_OWNERDRAW;
			(mii + i)->show_di = di;
			(mii + i)->is_folder = TRUE;

			for (cdi = di->child, cnt = 0; cdi != NULL; cdi = cdi->next, cnt++)
				;
			// Allocate menu item information
			if ((cmi = mem_calloc(sizeof(MENU_ITEM_INFO) * cnt)) == NULL) {
				return FALSE;
			}
			(mii + i)->mii = cmi;
			(mii + i)->mii_cnt = cnt;
			menu_create_datainfo(di->child, cmi, 0, id, step, 0, 0);
			menu_mark_folder_children(cmi, cnt);
			break;

		case TYPE_ITEM:
			// Item
			(mii + i)->flag = MF_OWNERDRAW;
			(mii + i)->show_di = format_get_priority_highest(di);
			break;

		case TYPE_DATA:
			// Data
			(mii + i)->flag = MF_OWNERDRAW;
			(mii + i)->show_di = di;
			break;
		}

		// Get title to display in menu
		format_get_menu_title((mii + i)->show_di);
		// Set title
		if (di->title != NULL) {
			if (lstrcmp(di->title, TEXT("-")) == 0) {
				// Separator
				(mii + i)->id = 0;
				(mii + i)->flag = MF_SEPARATOR | MF_OWNERDRAW;
				(mii + i)->item = (LPCTSTR)(mii + i);
				continue;
			} else if (option.menu_intact_item_title == 0) {
				menu_create_text(j++, di->title, buf);
				(mii + i)->text = alloc_copy(buf);
			} else {
				(mii + i)->text = alloc_copy(di->title);
			}

		} else if ((mii + i)->show_di->menu_title != NULL) {
			menu_create_text(j++, (mii + i)->show_di->menu_title, buf);
			(mii + i)->text = alloc_copy(buf);

		} else if ((mii + i)->show_di->format_name != NULL) {
			// Format name
			p = tmp;
			*(p++) = TEXT('(');
			lstrcpyn(p, (mii + i)->show_di->format_name, BUF_SIZE - 3);
			p += lstrlen(p);
			*(p++) = TEXT(')');
			*(p++) = TEXT('\0');
			menu_create_text(j++, tmp, buf);
			(mii + i)->text = alloc_copy(buf);
			(mii + i)->show_format = TRUE;

		} else {
			(mii + i)->text = alloc_copy(TEXT(""));
		}

		if (option.menu_show_hotkey == 1) {
			// Get hotkey
			(mii + i)->hkey = menu_get_keyname(di->op_modifiers, di->op_virtkey);
		}

		if (option.menu_show_icon == 1) {
			// Get icon to display in menu
			format_get_menu_icon((mii + i)->show_di);
			if ((mii + i)->show_di->menu_icon == NULL) {
				(mii + i)->icon = (di->type == TYPE_FOLDER) ? menu_icon_folder : menu_icon_default;
			} else {
				(mii + i)->icon = (mii + i)->show_di->menu_icon;
			}
			(mii + i)->free_icon = FALSE;

			// Get bitmap to display in menu
			if (option.menu_show_bitmap == 1) {
				if (db_history_is_open() && (mii + i)->set_di != NULL) {
					DATA_INFO *f;
					for (f = (mii + i)->set_di->child; f != NULL; f = f->next) {
						if (f->format_name != NULL &&
							(lstrcmpi(f->format_name, TEXT("BITMAP")) == 0 ||
							 lstrcmpi(f->format_name, TEXT("DIB")) == 0)) {
							db_history_ensure_item_data((mii + i)->set_di);
							break;
						}
					}
				}
				format_get_menu_bitmap((mii + i)->show_di);
			}
			(mii + i)->show_bitmap = (option.menu_show_bitmap == 1 &&
				(mii + i)->show_di->menu_bitmap != NULL) ? TRUE : FALSE;
		}
	}
	return TRUE;
}

/*
 * menu_create_info - create menu information
 */
static MENU_ITEM_INFO *menu_create_info(MENU_INFO *menu_info, const int menu_cnt,
										DATA_INFO *history_di, DATA_INFO *regist_di,
										int *id, int *ret_cnt)
{
	MENU_ITEM_INFO *mii;
	DATA_INFO *di;
	int i, j, t;
	int cnt;

	// Get number of menu items
	for (i = 0, *ret_cnt = 0; i < menu_cnt; i++) {
		switch ((menu_info + i)->content) {
		case MENU_CONTENT_SEPARATOR:
		case MENU_CONTENT_POPUP:
			(*ret_cnt)++;
			break;
		case MENU_CONTENT_VIEWER:
			*ret_cnt += 2; // Viewer and New snip.
			break;
		case MENU_CONTENT_OPTION:
		case MENU_CONTENT_CLIPBOARD_WATCH:
		case MENU_CONTENT_APP:
		case MENU_CONTENT_CANCEL:
		case MENU_CONTENT_EXIT:
			(*ret_cnt)++;
			break;

		case MENU_CONTENT_HISTORY:
		case MENU_CONTENT_HISTORY_DESC:
			for (di = history_di, cnt = 0; di != NULL &&
				(menu_info + i)->min > 0 && cnt < (menu_info + i)->min - 1; di = di->next, cnt++);
			for (; di != NULL &&
				((menu_info + i)->max <= 0 || cnt < (menu_info + i)->max || di->type == TYPE_FOLDER); di = di->next, (*ret_cnt)++) {
				if (di->type != TYPE_FOLDER) cnt++;
			}
			break;

		case MENU_CONTENT_REGIST:
		case MENU_CONTENT_REGIST_DESC:
			di = regist_path_to_item(regist_di, (menu_info + i)->path);
			for (; di != NULL; di = di->next, (*ret_cnt)++)
				;
			break;

		case MENU_CONTENT_TOOL:
			if ((menu_info + i)->path != NULL && *(menu_info + i)->path != TEXT('\0')) {
				if (tool_title_to_index((menu_info + i)->path) != -1) {
					(*ret_cnt)++;
				}
			} else {
				for (t = 0; t < option.tool_cnt; t++) {
					if ((option.tool_info + t)->call_type & CALLTYPE_MENU) {
						(*ret_cnt)++;
					}
				}
			}
			break;
		}
	}

	// Allocate menu item information
	if ((mii = mem_calloc(sizeof(MENU_ITEM_INFO) * (*ret_cnt))) == NULL) {
		*ret_cnt = 0;
		return NULL;
	}

	// Create menu item information
	for (i = 0, j = 0; i < menu_cnt; i++) {
		switch ((menu_info + i)->content) {
		case MENU_CONTENT_SEPARATOR:
			// Separator
			(mii + j)->id = 0;
			(mii + j)->flag = MF_SEPARATOR | MF_OWNERDRAW;
			(mii + j)->item = (LPCTSTR)(mii + j);
			j++;
			break;

		case MENU_CONTENT_HISTORY:
			// History (ascending)
			if (menu_create_datainfo(history_di, mii, j, id, 1, (menu_info + i)->min, (menu_info + i)->max) == TRUE) {
				for (di = history_di, cnt = 0; di != NULL &&
					(menu_info + i)->min > 0 && cnt < (menu_info + i)->min - 1; di = di->next, cnt++);
				for (; di != NULL &&
					((menu_info + i)->max <= 0 || cnt < (menu_info + i)->max || di->type == TYPE_FOLDER); di = di->next, j++) {
					if (di->type != TYPE_FOLDER) cnt++;
				}
			}
			break;

		case MENU_CONTENT_HISTORY_DESC:
			// History (descending)
			if (menu_create_datainfo(history_di, mii, j, id, -1, (menu_info + i)->min, (menu_info + i)->max) == TRUE) {
				for (di = history_di, cnt = 0; di != NULL &&
					(menu_info + i)->min > 0 && cnt < (menu_info + i)->min - 1; di = di->next, cnt++)
					;
				for (; di != NULL &&
					((menu_info + i)->max <= 0 || cnt < (menu_info + i)->max || di->type == TYPE_FOLDER); di = di->next, j++) {
					if (di->type != TYPE_FOLDER) cnt++;
				}
			}
			break;

		case MENU_CONTENT_REGIST:
			// Registered items (ascending)
			di = regist_path_to_item(regist_di, (menu_info + i)->path);
			if (di != NULL && menu_create_datainfo(di, mii, j, id, 1, 0, 0) == TRUE) {
				for (; di != NULL; di = di->next, j++)
					;
			}
			break;

		case MENU_CONTENT_REGIST_DESC:
			// Registered items (descending)
			di = regist_path_to_item(regist_di, (menu_info + i)->path);
			if (di != NULL && menu_create_datainfo(di, mii, j, id, -1, 0, 0) == TRUE) {
				for (; di != NULL; di = di->next, j++)
					;
			}
			break;

		case MENU_CONTENT_POPUP:
			// Popup menu
			(mii + j)->flag = MF_POPUP | MF_OWNERDRAW;
			(mii + j)->item = (LPCTSTR)(mii + j);
			(mii + j)->icon = menu_read_icon((menu_info + i)->icon_path, (menu_info + i)->icon_index, MENU_ICON_SIZE);
			(mii + j)->free_icon = TRUE;
			(mii + j)->is_folder = TRUE;

			{
				BOOL is_fav = FALSE;
				if ((menu_info + i)->mi != NULL && (menu_info + i)->mi_cnt > 0 &&
					((menu_info + i)->mi->content == MENU_CONTENT_REGIST ||
					 (menu_info + i)->mi->content == MENU_CONTENT_REGIST_DESC)) {
					is_fav = TRUE;
				} else if ((menu_info + i)->title != NULL) {
					if (_tcsstr((menu_info + i)->title, TEXT("Шаблон")) != NULL ||
						_tcsstr((menu_info + i)->title, TEXT("Template")) != NULL ||
						_tcsstr((menu_info + i)->title, TEXT("Regist")) != NULL ||
						_tcsstr((menu_info + i)->title, TEXT("Favourit")) != NULL ||
						_tcsstr((menu_info + i)->title, TEXT("Favorit")) != NULL) {
						is_fav = TRUE;
					}
				}
				if (is_fav) {
					(mii + j)->text = alloc_copy(TEXT("&Favourites"));
					(mii + j)->is_favourites = TRUE;
					(mii + j)->set_di = &regist_data;
				} else {
					(mii + j)->text = alloc_copy((menu_info + i)->title);
				}
			}

			(mii + j)->mii = menu_create_info(
				(menu_info + i)->mi, (menu_info + i)->mi_cnt,
				history_di, regist_di, id, &(mii + j)->mii_cnt);
			menu_mark_folder_children((mii + j)->mii, (mii + j)->mii_cnt);
			j++;
			break;

		case MENU_CONTENT_VIEWER:
			// Viewer
			(mii + j)->id = ID_MENUITEM_VIEWER;
			(mii + j)->flag = MF_OWNERDRAW;
			(mii + j)->item = (LPCTSTR)(mii + j);
			(mii + j)->text = alloc_copy(((menu_info + i)->title == NULL || *(menu_info + i)->title == TEXT('\0')) ?
				message_get_res(IDS_MENU_VIEWER) : (menu_info + i)->title);
			(mii + j)->icon = menu_read_icon((menu_info + i)->icon_path, (menu_info + i)->icon_index, MENU_ICON_SIZE);
			(mii + j)->free_icon = TRUE;
			j++;
			(mii + j)->id = ID_MENUITEM_NEW_SNIP;
			(mii + j)->flag = MF_OWNERDRAW;
			(mii + j)->item = (LPCTSTR)(mii + j);
			(mii + j)->text = alloc_copy(TEXT("New snip"));
			(mii + j)->icon = (HICON)LoadImage(hInst, MAKEINTRESOURCE(IDR_PIN_CROP), IMAGE_ICON,
				MENU_ICON_SIZE, MENU_ICON_SIZE, 0);
			(mii + j)->free_icon = TRUE;
			j++;
			break;

		case MENU_CONTENT_OPTION:
			// Options
			(mii + j)->id = ID_MENUITEM_OPTION;
			(mii + j)->flag = MF_OWNERDRAW;
			(mii + j)->item = (LPCTSTR)(mii + j);
			(mii + j)->text = alloc_copy(((menu_info + i)->title == NULL || *(menu_info + i)->title == TEXT('\0')) ?
				message_get_res(IDS_MENU_OPTION) : (menu_info + i)->title);
			(mii + j)->icon = menu_read_icon((menu_info + i)->icon_path, (menu_info + i)->icon_index, MENU_ICON_SIZE);
			(mii + j)->free_icon = TRUE;
			j++;
			break;

		case MENU_CONTENT_CLIPBOARD_WATCH:
			// Toggle clipboard monitoring
			(mii + j)->id = ID_MENUITEM_CLIPBOARD_WATCH;
			(mii + j)->flag = MF_OWNERDRAW | ((option.main_clipboard_watch == 1) ? MF_CHECKED : 0);
			(mii + j)->item = (LPCTSTR)(mii + j);
			(mii + j)->text = alloc_copy(((menu_info + i)->title == NULL || *(menu_info + i)->title == TEXT('\0')) ?
				message_get_res(IDS_MENU_CLIPBOARD_WATCH) : (menu_info + i)->title);
			(mii + j)->icon = menu_read_icon((menu_info + i)->icon_path, (menu_info + i)->icon_index, MENU_ICON_SIZE);
			(mii + j)->free_icon = TRUE;
			j++;
			break;

		case MENU_CONTENT_TOOL:
			// Tool
			if ((menu_info + i)->path != NULL && *(menu_info + i)->path != TEXT('\0')) {
				if ((t = tool_title_to_index((menu_info + i)->path)) != -1) {
					(mii + j)->id = ID_MENUITEM_DATA + ((*id)++);
					(mii + j)->flag = MF_OWNERDRAW;
					(mii + j)->item = (LPCTSTR)(mii + j);
					if ((menu_info + i)->title != NULL && *(menu_info + i)->title != TEXT('\0')) {
						(mii + j)->text = alloc_copy((menu_info + i)->title);
					} else {
						(mii + j)->text = alloc_copy((option.tool_info + t)->title);
					}
					if (option.menu_show_hotkey == 1) {
						(mii + j)->hkey = menu_get_keyname((option.tool_info + t)->modifiers, (option.tool_info + t)->virtkey);
					}
					(mii + j)->icon = menu_read_icon((menu_info + i)->icon_path, (menu_info + i)->icon_index, MENU_ICON_SIZE);
					(mii + j)->free_icon = TRUE;
					(mii + j)->ti = option.tool_info + t;
					j++;
				}
			} else {
				for (t = 0; t < option.tool_cnt; t++) {
					if (!((option.tool_info + t)->call_type & CALLTYPE_MENU)) {
						continue;
					}
					if (lstrcmp((option.tool_info + t)->title, TEXT("-")) == 0) {
						(mii + j)->id = 0;
						(mii + j)->flag = MF_SEPARATOR | MF_OWNERDRAW;
						(mii + j)->item = (LPCTSTR)(mii + j);
					} else {
						(mii + j)->id = ID_MENUITEM_DATA + ((*id)++);
						(mii + j)->flag = MF_OWNERDRAW;
						(mii + j)->item = (LPCTSTR)(mii + j);
						(mii + j)->text = alloc_copy((option.tool_info + t)->title);
						if (option.menu_show_hotkey == 1) {
							(mii + j)->hkey = menu_get_keyname((option.tool_info + t)->modifiers, (option.tool_info + t)->virtkey);
						}
						(mii + j)->ti = option.tool_info + t;
					}
					j++;
				}
			}
			break;

		case MENU_CONTENT_APP:
			// Execute application
			(mii + j)->id = ID_MENUITEM_DATA + ((*id)++);
			(mii + j)->flag = MF_OWNERDRAW;
			(mii + j)->item = (LPCTSTR)(mii + j);
			(mii + j)->text = alloc_copy((menu_info + i)->title);
			if (*(menu_info + i)->icon_path != TEXT('\0')) {
				(mii + j)->icon = menu_read_icon((menu_info + i)->icon_path, (menu_info + i)->icon_index, MENU_ICON_SIZE);
			} else {
				(mii + j)->icon = menu_read_icon((menu_info + i)->path, 0, MENU_ICON_SIZE);
			}
			(mii + j)->free_icon = TRUE;
			// Set menu information
			(mii + j)->mi = menu_info + i;
			j++;
			break;

		case MENU_CONTENT_CANCEL:
			// Cancel
			(mii + j)->id = IDCANCEL;
			(mii + j)->flag = MF_OWNERDRAW;
			(mii + j)->item = (LPCTSTR)(mii + j);
			(mii + j)->text = alloc_copy(((menu_info + i)->title == NULL || *(menu_info + i)->title == TEXT('\0')) ?
				message_get_res(IDS_MENU_CANCEL) : (menu_info + i)->title);
			(mii + j)->icon = menu_read_icon((menu_info + i)->icon_path, (menu_info + i)->icon_index, MENU_ICON_SIZE);
			(mii + j)->free_icon = TRUE;
			j++;
			break;

		case MENU_CONTENT_EXIT:
			// Exit
			(mii + j)->id = ID_MENUITEM_EXIT;
			(mii + j)->flag = MF_OWNERDRAW;
			(mii + j)->item = (LPCTSTR)(mii + j);
			(mii + j)->text = alloc_copy(((menu_info + i)->title == NULL || *(menu_info + i)->title == TEXT('\0')) ?
				message_get_res(IDS_MENU_EXIT) : (menu_info + i)->title);
			(mii + j)->icon = menu_read_icon((menu_info + i)->icon_path, (menu_info + i)->icon_index, MENU_ICON_SIZE);
			(mii + j)->free_icon = TRUE;
			j++;
			break;
		}
	}
	return mii;
}

/*
 * menu_is_trailing_commands - Check if all remaining items are trailing commands
 */
static BOOL menu_is_trailing_commands(const MENU_ITEM_INFO *mii, const int current_idx, const int total_cnt)
{
	int k;

	for (k = current_idx; k < total_cnt; k++) {
		if ((mii + k)->set_di != NULL && (mii + k)->set_di != &regist_data) {
			return FALSE;
		}
	}
	return TRUE;
}

/*
 * menu_set_item - set menu item
 */
static BOOL menu_set_item(const HDC hdc, const HMENU hMenu, MENU_ITEM_INFO *mii, const int cnt)
{
	HMENU hPopupMenu;
	SIZE size;
	int *item_heights;
	int total_height = 0;
	int work_height;
	int max_height;
	int num_cols = 1;
	int remaining_height = 0;
	int remaining_cols = 1;
	int target_col_height = 0;
	int col_total_height = 0;
	int cols_done = 0;
	int item_height;
	int menu_flag;
	int i;

	if (cnt <= 0 || mii == NULL) {
		return TRUE;
	}

	item_heights = (int *)mem_calloc(sizeof(int) * cnt);
	if (item_heights == NULL) {
		return FALSE;
	}

	// Pass 1: Calculate height of each item
	for (i = 0; i < cnt; i++) {
		if ((mii + i)->flag & MF_SEPARATOR) {
			item_height = MENU_SEPARATOR_HEIGHT;
		} else if ((mii + i)->flag & MF_OWNERDRAW) {
			if ((mii + i)->text != NULL && GetTextExtentPoint32(hdc,
				(mii + i)->text, lstrlen((mii + i)->text), &size) == TRUE) {
				(mii + i)->text_x = size.cx;
				(mii + i)->text_y = size.cy;
			}
			if ((mii + i)->hkey != NULL && GetTextExtentPoint32(hdc,
				(mii + i)->hkey, lstrlen((mii + i)->hkey), &size) == TRUE) {
				(mii + i)->text_x += size.cx;
			}
			item_height = menu_get_item_size(mii + i, NULL);
		} else {
			item_height = GetSystemMetrics(SM_CYMENU);
		}
		item_heights[i] = item_height;
		total_height += item_height;
	}

	// Determine column count and calculate boundaries
	work_height = (menu_work_rect.bottom - menu_work_rect.top > 0)
		? (menu_work_rect.bottom - menu_work_rect.top)
		: (menu_monitor_rect.bottom - menu_monitor_rect.top);
	if (work_height <= 0) {
		work_height = GetSystemMetrics(SM_CYSCREEN);
	}
	max_height = work_height - Scale(30);
	if (max_height <= 200) {
		max_height = work_height;
	}

	if (option.menu_break == 1 && total_height > max_height && max_height > 0) {
		num_cols = (total_height + max_height - 1) / max_height;
		if (num_cols < 2) {
			num_cols = 2;
		}
		remaining_height = total_height;
		remaining_cols = num_cols;
		target_col_height = (remaining_height + remaining_cols - 1) / remaining_cols;
		if (target_col_height > max_height) {
			target_col_height = max_height;
		}
	}

	// Pass 2: Add menu items and column breaks
	for (i = 0; i < cnt; i++) {
		item_height = item_heights[i];
		menu_flag = 0;

		if (option.menu_break == 1 && col_total_height > 0) {
			BOOL is_trailing = menu_is_trailing_commands(mii, i, cnt);
			BOOL is_exit = ((mii + i)->id == ID_MENUITEM_EXIT);
			BOOL should_break = FALSE;

			if (!is_exit && !is_trailing && cols_done < num_cols - 1) {
				if (col_total_height + item_height > max_height) {
					should_break = TRUE;
				} else if (target_col_height > 0 && col_total_height >= target_col_height) {
					if (remaining_height - col_total_height <= (remaining_cols - 1) * max_height) {
						should_break = TRUE;
					}
				}
			}

			if (should_break) {
				menu_flag = MF_MENUBARBREAK;
				remaining_height -= col_total_height;
				if (remaining_height < 0) {
					remaining_height = 0;
				}
				col_total_height = 0;
				cols_done++;
				remaining_cols = num_cols - cols_done;
				if (remaining_cols > 0) {
					target_col_height = (remaining_height + remaining_cols - 1) / remaining_cols;
					if (target_col_height > max_height) {
						target_col_height = max_height;
					}
				} else {
					target_col_height = 0;
				}
			}
		}

		col_total_height += item_height;

		if ((mii + i)->flag & MF_POPUP) {
			hPopupMenu = CreatePopupMenu();
			menu_set_item(hdc, hPopupMenu, (mii + i)->mii, (mii + i)->mii_cnt);
			// Add menu item
			AppendMenu(hMenu, (mii + i)->flag | menu_flag, (UINT)hPopupMenu, (mii + i)->item);
		} else {
			// Add menu item
			AppendMenu(hMenu, (mii + i)->flag | menu_flag, (mii + i)->id, (mii + i)->item);
		}
		{
			MENUITEMINFO mii_set;
			ZeroMemory(&mii_set, sizeof(mii_set));
			mii_set.cbSize = sizeof(mii_set);
			mii_set.fMask = MIIM_DATA;
			mii_set.dwItemData = (ULONG_PTR)(mii + i);
			SetMenuItemInfo(hMenu, GetMenuItemCount(hMenu) - 1, TRUE, &mii_set);
		}
	}

	mem_free(&item_heights);
	return TRUE;
}

/*
 * menu_create - create menu
 */
HMENU menu_create(const HWND hWnd, MENU_INFO *menu_info, const int menu_cnt,
				  DATA_INFO *history_di, DATA_INFO *regist_di)
{
	HMENU hMenu;
	HDC hdc;
	HFONT hFont, hRetFont;
	int id = 0;

	// Adjust to DPI of display monitor
	if (IsRectEmpty(&menu_monitor_rect) != FALSE) {
		menu_set_dpi(NULL);
	}
	// Load default icon
	menu_load_icons();
#ifdef OP_XP_STYLE
	// Open menu visual styles theme
	menu_theme_open(hWnd);
#endif	// OP_XP_STYLE
	// Create menu
	if ((hMenu = CreatePopupMenu()) == NULL) {
		return NULL;
	}
	menu_item_info = menu_create_info(menu_info, menu_cnt, history_di, regist_di, &id, &menu_item_cnt);

	if ((hdc = GetDC(hWnd)) == NULL) {
		DestroyMenu(hMenu);
		return NULL;
	}
	// Font settings
	hFont = menu_create_font();
	hRetFont = SelectObject(hdc, hFont);
	// Set item in menu
	menu_set_item(hdc, hMenu, menu_item_info, menu_item_cnt);
	SelectObject(hdc, hRetFont);
	DeleteObject(hFont);
	ReleaseDC(hWnd, hdc);

	if (dark_mode_is_dark() == TRUE) {
		MENUINFO mi;

		// Set menu background color
		ZeroMemory(&mi, sizeof(mi));
		mi.cbSize = sizeof(mi);
		mi.fMask = MIM_BACKGROUND | MIM_APPLYTOSUBMENUS;
		mi.hbrBack = dark_mode_get_brush(COLOR_MENU);
		SetMenuInfo(hMenu, &mi);
	}
	return hMenu;
}

/*
 * menu_destory - destroy menu
 */
void menu_destory(HMENU hMenu)
{
	HMENU hSubMenu;
	int cnt, i;

	cnt = GetMenuItemCount(hMenu);
	for (i = 0; i < cnt; i++) {
		if ((hSubMenu = GetSubMenu(hMenu, i)) != NULL) {
			menu_destory(hSubMenu);
			ModifyMenu(hMenu, i, MF_BYPOSITION, 0, NULL);
		}
	}
	DestroyMenu(hMenu);
}

/*
 * menu_set_drawitem - configure menu item drawing
 */
BOOL menu_set_drawitem(MEASUREITEMSTRUCT *ms)
{
	ms->itemHeight = menu_get_item_size((MENU_ITEM_INFO *)ms->itemData, &ms->itemWidth);
	return TRUE;
}

/*
 * menu_draw_bitmap - draw bitmap on menu
 */
static int menu_draw_bitmap(const HDC draw_dc, const DATA_INFO *di, const int height)
{
	HDC hdc;
	HBITMAP hRetBmp;
	int bmp_width;
	int bmp_height;

	if ((hdc = CreateCompatibleDC(draw_dc)) == NULL) {
		return -1;
	}
	if ((hRetBmp = SelectObject(hdc, di->menu_bitmap)) == NULL) {
		DeleteDC(hdc);
		return -1;
	}

	if (di->menu_bmp_width == 0 && di->menu_bmp_height == 0) {
		bmp_width = MENU_BITMAP_WIDTH;
		bmp_height = MENU_BITMAP_HEIGHT;
	} else {
		bmp_width = di->menu_bmp_width;
		bmp_height = di->menu_bmp_height;
	}

	BitBlt(draw_dc,
		MENU_ICON_MARGIN,
		height / 2 - bmp_height / 2,
		bmp_width, height,
		hdc, 0, 0, SRCCOPY);

	SelectObject(hdc, hRetBmp);
	DeleteDC(hdc);
	return MENU_ICON_MARGIN + bmp_width;
}

/*
 * menu_draw_ckeck - draw menu check mark
 */
static BOOL menu_draw_ckeck(const HDC draw_dc, const int left, const int top, const int right, const int bottom)
{
	HDC hdc;
	HBITMAP hbmp, ret_hbmp;
	HDC wk_dc;
	HBITMAP wk_hbmp, wk_ret_hbmp;
	HANDLE hBrush;
	RECT draw_rect;

	// Create working DC
	if ((hdc = CreateCompatibleDC(draw_dc)) == NULL) {
		return FALSE;
	}
	if ((hbmp = CreateCompatibleBitmap(draw_dc, right - left, bottom - top)) == NULL) {
		DeleteDC(hdc);
		return FALSE;
	}
	ret_hbmp = SelectObject(hdc, hbmp);

	if ((wk_dc = CreateCompatibleDC(draw_dc)) == NULL) {
		SelectObject(hdc, ret_hbmp);
		DeleteObject(hbmp);
		DeleteDC(hdc);
		return FALSE;
	}
	if ((wk_hbmp = CreateCompatibleBitmap(draw_dc, right - left, bottom - top)) == NULL) {
		SelectObject(hdc, ret_hbmp);
		DeleteObject(hbmp);
		DeleteDC(hdc);
		DeleteDC(wk_dc);
		return FALSE;
	}
	wk_ret_hbmp = SelectObject(wk_dc, wk_hbmp);

	SetRect(&draw_rect, 0, 0, right - left, bottom - top);
	
	// Draw mask
	DrawFrameControl(hdc, &draw_rect, DFC_MENU, DFCS_MENUCHECK);
	BitBlt(hdc, 0, 0, right - left, bottom - top, hdc, 0, 0, DSTINVERT);
	BitBlt(draw_dc, left, top, right, bottom, hdc, 0, 0, SRCPAINT);

	// Draw check mark
	hBrush = CreateSolidBrush(GetTextColor(draw_dc));
	FillRect(hdc, &draw_rect, hBrush);
	DeleteObject(hBrush);
	DrawFrameControl(wk_dc, &draw_rect, DFC_MENU, DFCS_MENUCHECK);
	BitBlt(hdc, 0, 0, right - left, bottom - top, wk_dc, 0, 0, SRCPAINT);
	BitBlt(draw_dc, left, top, right, bottom, hdc, 0, 0, SRCAND);

	SelectObject(hdc, ret_hbmp);
	DeleteObject(hbmp);
	DeleteDC(hdc);
	SelectObject(wk_dc, wk_ret_hbmp);
	DeleteObject(wk_hbmp);
	DeleteDC(wk_dc);
	return TRUE;
}

/*
 * menu_get_arrow_size - get submenu arrow area width
 */
static int menu_get_arrow_size(void)
{
	int size = GetSystemMetrics(SM_CXMENUCHECK);
	int dpi_size = GetSystemMetricsDpi(SM_CXMENUCHECK);

	return (size < dpi_size) ? dpi_size : size;
}

/*
 * menu_draw_arrow - draw submenu arrow
 */
static void menu_draw_arrow(const HDC draw_dc, const RECT *rect, const COLORREF color)
{
	LOGFONT lf;
	HFONT hFont, hRetFont;
	int width = rect->right - rect->left;
	int height = rect->bottom - rect->top;
	int size = (width < height) ? width : height;

	ZeroMemory(&lf, sizeof(lf));
	lf.lfHeight = size;
	lf.lfWeight = FW_NORMAL;
	lf.lfCharSet = DEFAULT_CHARSET;
	lstrcpy(lf.lfFaceName, TEXT("Marlett"));
	if ((hFont = CreateFontIndirect(&lf)) == NULL) {
		return;
	}
	hRetFont = SelectObject(draw_dc, hFont);
	SetTextColor(draw_dc, color);
	SetBkMode(draw_dc, TRANSPARENT);
	// Marlett '8' is submenu arrow
	TextOut(draw_dc, rect->left + (width - size) / 2, rect->top + (height - size) / 2, TEXT("8"), 1);
	SelectObject(draw_dc, hRetFont);
	DeleteObject(hFont);
}

/*
 * menu_drawitem - draw menu item
 */
BOOL menu_drawitem(const DRAWITEMSTRUCT *ds)
{
	MENU_ITEM_INFO *mii;
	HDC draw_dc;
	HBITMAP hDrawBmp, hrBmp;
	HANDLE hBrush;
	HFONT hFont, hRetFont;
	HPEN hPen, hRetPen;
	RECT draw_rect;
	SIZE sz;
	COLORREF text_color;
	int arrow_size = 0;
	int left_margin;
	int width, height;
#ifdef MENU_COLOR
	DWORD menu_color_back = (*option.menu_color_back.color_str != TEXT('\0')) ?
		option.menu_color_back.color : dark_mode_get_color(COLOR_MENU);
	DWORD menu_color_text = (*option.menu_color_text.color_str != TEXT('\0')) ?
		option.menu_color_text.color : dark_mode_get_color(COLOR_MENUTEXT);
	DWORD menu_color_highlight = (*option.menu_color_highlight.color_str != TEXT('\0')) ?
		option.menu_color_highlight.color : dark_mode_get_color(COLOR_HIGHLIGHT);
	DWORD menu_color_highlighttext = (*option.menu_color_highlighttext.color_str != TEXT('\0')) ?
		option.menu_color_highlighttext.color : dark_mode_get_color(COLOR_HIGHLIGHTTEXT);
	DWORD menu_color_format = (*option.menu_color_highlight.color_str != TEXT('\0')) ?
		option.menu_color_highlight.color : dark_mode_get_accent_color();
	DWORD menu_color_3d_shadow = (*option.menu_color_3d_shadow.color_str != TEXT('\0')) ?
		option.menu_color_3d_shadow.color : dark_mode_get_color(COLOR_3DSHADOW);
	DWORD menu_color_3d_highlight = (*option.menu_color_3d_highlight.color_str != TEXT('\0')) ?
		option.menu_color_3d_highlight.color : dark_mode_get_color(COLOR_3DHIGHLIGHT);
#else	// MENU_COLOR
	DWORD menu_color_back = dark_mode_get_color(COLOR_MENU);
	DWORD menu_color_text = dark_mode_get_color(COLOR_MENUTEXT);
	DWORD menu_color_highlight = dark_mode_get_color(COLOR_HIGHLIGHT);
	DWORD menu_color_highlighttext = dark_mode_get_color(COLOR_HIGHLIGHTTEXT);
	DWORD menu_color_format = dark_mode_get_accent_color();
	DWORD menu_color_3d_shadow = dark_mode_get_color(COLOR_3DSHADOW);
	DWORD menu_color_3d_highlight = dark_mode_get_color(COLOR_3DHIGHLIGHT);
#endif	// MENU_COLOR

	mii = (MENU_ITEM_INFO *)ds->itemData;

	width = ds->rcItem.right - ds->rcItem.left;
	height = ds->rcItem.bottom - ds->rcItem.top;

	// Create drawing DC
	if ((draw_dc = CreateCompatibleDC(ds->hDC)) == NULL) {
		return FALSE;
	}
	if ((hDrawBmp = CreateCompatibleBitmap(ds->hDC, width, height)) == NULL) {
		DeleteDC(draw_dc);
		return FALSE;
	}
	hrBmp = SelectObject(draw_dc, hDrawBmp);

	// Background
	SetRect(&draw_rect, 0, 0, width, height);
#ifdef OP_XP_STYLE
	if (menu_theme != NULL) {
		if (mii != NULL && mii->is_folder_child) {
			// Folder menu item: lighter background
			hBrush = CreateSolidBrush(RGB(255, 255, 255));
			FillRect(draw_dc, &draw_rect, hBrush);
			DeleteObject(hBrush);

			if (ds->itemState & ODS_SELECTED) {
				_MenuDrawThemeBackground(menu_theme, draw_dc, MENU_POPUPITEM, MPI_HOT, &draw_rect, NULL);
				text_color = menu_theme_text_color(MPI_HOT, menu_color_highlighttext);
			} else {
				text_color = (mii->show_format == TRUE) ?
					menu_color_format : menu_theme_text_color(MPI_NORMAL, menu_color_text);
			}
		} else {
			// Draw with visual styles
			_MenuDrawThemeBackground(menu_theme, draw_dc, MENU_POPUPBACKGROUND, 0, &draw_rect, NULL);
			if (ds->itemState & ODS_SELECTED) {
				_MenuDrawThemeBackground(menu_theme, draw_dc, MENU_POPUPITEM, MPI_HOT, &draw_rect, NULL);
				text_color = menu_theme_text_color(MPI_HOT, menu_color_highlighttext);
			} else {
				text_color = (mii->show_format == TRUE) ?
					menu_color_format : menu_theme_text_color(MPI_NORMAL, menu_color_text);
			}
		}
		SetTextColor(draw_dc, text_color);
		SetBkMode(draw_dc, TRANSPARENT);
	} else
#endif	// OP_XP_STYLE
	if (ds->itemState & ODS_SELECTED) {
		COLORREF sel_bg = (mii != NULL && mii->is_folder_child) ?
			(dark_mode_is_dark() ? RGB(75, 75, 75) : RGB(220, 236, 254)) : menu_color_highlight;
		COLORREF sel_text = (mii != NULL && mii->is_folder_child) ?
			(dark_mode_is_dark() ? RGB(255, 255, 255) : RGB(0, 75, 160)) : menu_color_highlighttext;

		hBrush = CreateSolidBrush(sel_bg);
		FillRect(draw_dc, &draw_rect, hBrush);
		DeleteObject(hBrush);

		text_color = sel_text;
		SetTextColor(draw_dc, text_color);
		SetBkColor(draw_dc, sel_bg);
	} else {
		COLORREF item_bg = (mii != NULL && mii->is_folder_child) ?
			(dark_mode_is_dark() ? RGB(58, 58, 58) : RGB(255, 255, 255)) : menu_color_back;

		hBrush = CreateSolidBrush(item_bg);
		FillRect(draw_dc, &draw_rect, hBrush);
		DeleteObject(hBrush);

		text_color = (mii->show_format == TRUE) ? menu_color_format :
			((mii != NULL && mii->is_folder_child && !dark_mode_is_dark()) ? RGB(20, 20, 20) : menu_color_text);
		SetTextColor(draw_dc, text_color);
		SetBkColor(draw_dc, item_bg);
	}

	if (option.menu_show_icon == 1) {
		left_margin = -1;
		if (mii->show_bitmap == TRUE &&
			mii->show_di->menu_bitmap != NULL) {
			// Bitmap
			left_margin = menu_draw_bitmap(draw_dc, mii->show_di, height);
		}
		if (left_margin == -1) {
			// Icon
			if (mii->icon != NULL) {
				DrawIconEx(draw_dc, MENU_ICON_MARGIN,
					height / 2 - MENU_ICON_SIZE / 2, mii->icon,
					MENU_ICON_SIZE, MENU_ICON_SIZE, 0, NULL, DI_NORMAL);
			} else if (mii->flag & MF_CHECKED) {
#ifdef OP_XP_STYLE
				if (menu_draw_check_theme(draw_dc, MENU_ICON_MARGIN,
					height / 2 - MENU_ICON_SIZE / 2,
					MENU_ICON_MARGIN + MENU_ICON_SIZE,
					height / 2 - MENU_ICON_SIZE / 2 + MENU_ICON_SIZE) == FALSE)
#endif	// OP_XP_STYLE
				menu_draw_ckeck(draw_dc, MENU_ICON_MARGIN,
					height / 2 - MENU_ICON_SIZE / 2,
					MENU_ICON_SIZE, MENU_ICON_SIZE);
			}
			left_margin = MENU_ICON_MARGIN + MENU_ICON_SIZE;
		}
	} else {
		if (mii->flag & MF_CHECKED) {
#ifdef OP_XP_STYLE
			if (menu_draw_check_theme(draw_dc, MENU_ICON_MARGIN, 0,
				MENU_ICON_MARGIN + MENU_ICON_SIZE, height) == FALSE)
#endif	// OP_XP_STYLE
			menu_draw_ckeck(draw_dc, MENU_ICON_MARGIN, 0,
				MENU_ICON_SIZE, height);
		}
		left_margin = MENU_ICON_MARGIN + GetSystemMetrics(SM_CXMENUCHECK);
	}

	if (mii->text != NULL) {
		// Text
		hFont = menu_create_font();
		hRetFont = SelectObject(draw_dc, hFont);

		left_margin += MENU_TEXT_MARGIN_LEFT;
		SetRect(&draw_rect, left_margin, 0,
			width - MENU_TEXT_MARGIN_RIGHT, height);
		if (mii->hkey == NULL) {
			DrawText(draw_dc,
				mii->text, lstrlen(mii->text),
				&draw_rect, DT_VCENTER | DT_SINGLELINE | DT_NOCLIP | DT_WORD_ELLIPSIS);
		} else {
			GetTextExtentPoint32(draw_dc, mii->hkey, lstrlen(mii->hkey), &sz);
			draw_rect.right -= (sz.cx + 10);
			DrawText(draw_dc,
				mii->text, lstrlen(mii->text),
				&draw_rect, DT_VCENTER | DT_SINGLELINE | DT_NOCLIP | DT_WORD_ELLIPSIS);
			// Show hotkey
			if (!(ds->itemState & ODS_SELECTED) && mii->show_format == TRUE) {
#ifdef OP_XP_STYLE
				if (menu_theme != NULL) {
					SetTextColor(draw_dc, menu_theme_text_color(MPI_NORMAL, menu_color_text));
				} else
#endif	// OP_XP_STYLE
				SetTextColor(draw_dc, menu_color_text);
			}
			draw_rect.right = width - MENU_TEXT_MARGIN_RIGHT;
			DrawText(draw_dc,
				mii->hkey, lstrlen(mii->hkey),
				&draw_rect, DT_VCENTER | DT_SINGLELINE | DT_NOCLIP | DT_RIGHT);
		}
		SelectObject(draw_dc, hRetFont);
		DeleteObject(hFont);

	} else if (mii->flag & MF_SEPARATOR) {
		// Separator
#ifdef OP_XP_STYLE
		if (menu_theme != NULL) {
			SIZE size;

			SetRect(&draw_rect, MENU_SEPARATOR_MARGIN_LEFT, 0,
				width - MENU_SEPARATOR_MARGIN_RIGHT, height);
			if (_MenuGetThemePartSize(menu_theme, draw_dc, MENU_POPUPSEPARATOR, 0,
				NULL, TS_TRUE, &size) == S_OK && size.cy < height) {
				draw_rect.top = (height - size.cy) / 2;
				draw_rect.bottom = draw_rect.top + size.cy;
			}
			_MenuDrawThemeBackground(menu_theme, draw_dc, MENU_POPUPSEPARATOR, 0, &draw_rect, NULL);
		} else {
#endif	// OP_XP_STYLE
		hPen = CreatePen(PS_SOLID, 1, menu_color_3d_shadow);
		hRetPen = SelectObject(draw_dc, hPen);
		MoveToEx(draw_dc, MENU_SEPARATOR_MARGIN_LEFT,
			MENU_SEPARATOR_HEIGHT / 2 - 1, NULL);
		LineTo(draw_dc, width - MENU_SEPARATOR_MARGIN_RIGHT,
			MENU_SEPARATOR_HEIGHT / 2 - 1);
		SelectObject(draw_dc, hRetPen);
		DeleteObject(hPen);

		hPen = CreatePen(PS_SOLID, 1, menu_color_3d_highlight);
		hRetPen = SelectObject(draw_dc, hPen);
		MoveToEx(draw_dc, MENU_SEPARATOR_MARGIN_LEFT,
			MENU_SEPARATOR_HEIGHT / 2, NULL);
		LineTo(draw_dc, width - MENU_SEPARATOR_MARGIN_RIGHT,
			MENU_SEPARATOR_HEIGHT / 2);
		SelectObject(draw_dc, hRetPen);
		DeleteObject(hPen);
#ifdef OP_XP_STYLE
		}
#endif	// OP_XP_STYLE
	}

	if (mii->flag & MF_POPUP) {
		// Submenu arrow
		arrow_size = menu_get_arrow_size();
		SetRect(&draw_rect, width - arrow_size, 0, width, height);
		menu_draw_arrow(draw_dc, &draw_rect, text_color);
	}
	// Draw to menu
	BitBlt(ds->hDC,
		ds->rcItem.left, ds->rcItem.top,
		ds->rcItem.right, ds->rcItem.bottom,
		draw_dc, 0, 0, SRCCOPY);

	if (mii->flag & MF_POPUP) {
		// Clip arrow area to avoid showing system-drawn arrow after drawing
		ExcludeClipRect(ds->hDC,
			ds->rcItem.right - arrow_size, ds->rcItem.top,
			ds->rcItem.right, ds->rcItem.bottom);
	}

	SelectObject(draw_dc, hrBmp);
	DeleteObject(hDrawBmp);
	DeleteDC(draw_dc);
	return TRUE;
}

/*
 * menu_get_accelerator - get menu accelerator key
 */
static TCHAR menu_get_accelerator(TCHAR *str)
{
	TCHAR ret = TEXT('\0');
	TCHAR *p;

	for (p = str; *p != TEXT('\0'); p++) {
#ifndef UNICODE
		if (IsDBCSLeadByte((BYTE)*p) == TRUE) {
			p++;
			continue;
		}
#endif
		if (*p != TEXT('&')) {
			continue;
		}
		if (*(p + 1) == TEXT('&')) {
			p++;
		} else {
			// Accelerator key
			ret = *(p + 1);
		}
	}
	return ret;
}

/*
 * menu_accelerator - menu accelerator
 */
LRESULT menu_accelerator(const HMENU hMenu, const TCHAR key)
{
#define ToLower(c)		((c >= TEXT('A') && c <= TEXT('Z')) ? (c - TEXT('A') + TEXT('a')) : c)
	MENUITEMINFO mii;
	int i, sel;
	int cnt;
	int ret = -1;

	cnt = GetMenuItemCount(hMenu);

	// Get selected position
	for (sel = 0; sel < cnt; sel++) {
		if (GetMenuState(hMenu, sel, MF_BYPOSITION) & MF_HILITE) {
			break;
		}
	}
	if (sel >= cnt) {
		sel = -1;
	}

	// Get accelerator position
	for (i = sel + 1; i < cnt; i++) {
		ZeroMemory(&mii, sizeof(mii));
		mii.cbSize = sizeof(mii);
		mii.fMask = MIIM_TYPE | MIIM_DATA;
		if (GetMenuItemInfo(hMenu, i, TRUE, &mii) == 0 ||
			!(mii.fType & MFT_OWNERDRAW) ||
			mii.dwItemData == 0 || 
			((MENU_ITEM_INFO *)mii.dwItemData)->text == NULL) {
			continue;
		}
		if (ToLower(menu_get_accelerator(((MENU_ITEM_INFO *)mii.dwItemData)->text)) != ToLower(key)) {
			continue;
		}
		if (ret != -1) {
			// Select
			return MAKELRESULT(ret, MNC_SELECT);
		}
		ret = i;
	}
	for (i = 0; i <= sel; i++) {
		ZeroMemory(&mii, sizeof(mii));
		mii.cbSize = sizeof(mii);
		mii.fMask = MIIM_TYPE | MIIM_DATA;
		if (GetMenuItemInfo(hMenu, i, TRUE, &mii) == 0 ||
			!(mii.fType & MFT_OWNERDRAW) ||
			mii.dwItemData == 0 || 
			((MENU_ITEM_INFO *)mii.dwItemData)->text == NULL) {
			continue;
		}
		if (ToLower(menu_get_accelerator(((MENU_ITEM_INFO *)mii.dwItemData)->text)) != ToLower(key)) {
			continue;
		}
		if (ret != -1) {
			// Select
			return MAKELRESULT(ret, MNC_SELECT);
		}
		ret = i;
	}
	return ((ret != -1) ? MAKELRESULT(ret, MNC_EXECUTE) : 0);
}

/*
 * menu_get_selectable_index_by_datainfo - 0-based selectable item index for a data_info
 */
int menu_get_selectable_index_by_datainfo(const DATA_INFO *target_di)
{
	int i;
	int sel_idx = 0;

	if (target_di == NULL || menu_item_info == NULL) {
		return -1;
	}
	for (i = 0; i < menu_item_cnt; i++) {
		if ((menu_item_info + i)->flag & MF_SEPARATOR) {
			continue;
		}
		if ((menu_item_info + i)->set_di == target_di) {
			return sel_idx;
		}
		sel_idx++;
	}
	return -1;
}
/* End of source */
