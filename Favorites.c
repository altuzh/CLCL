/*
 * CLCL
 *
 * Favorites.c - Favourites management & dialogs
 *
 * Copyright (C) 1996-2026 by Ohno Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 *		nakka@nakka.com
 */

/* Include Files */
#define _INC_OLE
#include <windows.h>
#undef  _INC_OLE
#include <tchar.h>
#include <commctrl.h>

#include "General.h"
#include "Memory.h"
#include "String.h"
#include "Data.h"
#include "Regist.h"
#include "DarkMode.h"
#include "dpi.h"
#include "DbHistory.h"
#include "Favorites.h"
#include "resource.h"

/* Global Variables */
extern HINSTANCE hInst;
extern DATA_INFO regist_data;
extern DATA_INFO history_data;
extern BOOL save_regist(const HWND hWnd);

/* Menu Command IDs */
#define ID_FAV_BASE					40000
#define ID_FAV_ROOT					40001
#define ID_FAV_NEW_SUBMENU_ROOT		40002
#define ID_FAV_DELETE				40003
#define ID_FAV_DELETE_SUBMENU		40004
#define ID_FAV_DELETE_ITEM			40005
#define ID_FAV_RENAME_SUBMENU		40006
#define ID_FAV_FOLDER_BASE			41000
#define ID_FAV_NEW_SUB_BASE			45000

#define MAX_FAV_FOLDERS				512
static DATA_INFO *fav_folder_map[MAX_FAV_FOLDERS];
static int fav_folder_cnt = 0;

/* Local Variables */
static DATA_INFO *last_created_folder = NULL;

/* Context for New Submenu Dialog */
typedef struct {
	DATA_INFO *parent_folder;
} NEW_FOLDER_CTX;

/*
 * create_dialog_template - construct an in-memory DLGTEMPLATE with cdit=0
 */
static HGLOBAL create_dialog_template(LPCWSTR title, short cx, short cy)
{
	HGLOBAL hg = GlobalAlloc(GHND, 1024);
	BYTE *p;
	DLGTEMPLATE *pDlg;
	size_t len;
	LPCWSTR font_name = L"MS Shell Dlg";

	if (hg == NULL) {
		return NULL;
	}
	p = (BYTE *)GlobalLock(hg);
	if (p == NULL) {
		GlobalFree(hg);
		return NULL;
	}

	pDlg = (DLGTEMPLATE *)p;
	pDlg->style = DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_SETFONT;
	pDlg->dwExtendedStyle = 0;
	pDlg->cdit = 0;
	pDlg->x = 0;
	pDlg->y = 0;
	pDlg->cx = cx;
	pDlg->cy = cy;
	p += sizeof(DLGTEMPLATE);

	*(WORD *)p = 0; // No menu
	p += sizeof(WORD);

	*(WORD *)p = 0; // Default dialog class
	p += sizeof(WORD);

	// Title
	if (title != NULL) {
		len = wcslen(title);
		memcpy(p, title, (len + 1) * sizeof(WCHAR));
		p += (len + 1) * sizeof(WCHAR);
	} else {
		*(WCHAR *)p = 0;
		p += sizeof(WCHAR);
	}

	// Font size
	*(WORD *)p = 9;
	p += sizeof(WORD);

	// Font name
	len = wcslen(font_name);
	memcpy(p, font_name, (len + 1) * sizeof(WCHAR));
	p += (len + 1) * sizeof(WCHAR);

	GlobalUnlock(hg);
	return hg;
}

/*
 * new_folder_dlg_proc - dialog procedure for creating a new submenu
 */
static INT_PTR CALLBACK new_folder_dlg_proc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	NEW_FOLDER_CTX *ctx;
	RECT rc;
	int width;
	int margin, y, label_h, edit_h, btn_w, btn_h;
	HFONT hFont;
	TCHAR prompt[BUF_SIZE];
	HWND hCtrl;

	switch (uMsg) {
	case WM_INITDIALOG:
		SetWindowLongPtr(hDlg, GWLP_USERDATA, (LONG_PTR)lParam);
		ctx = (NEW_FOLDER_CTX *)lParam;

		GetClientRect(hDlg, &rc);
		width = rc.right - rc.left;
		margin = Scale(12);
		y = Scale(10);
		label_h = Scale(18);
		edit_h = Scale(24);
		btn_w = Scale(75);
		btn_h = Scale(24);

		hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

		// Prompt label
		if (ctx != NULL && ctx->parent_folder != NULL && ctx->parent_folder->title != NULL) {
			wsprintf(prompt, TEXT("New Submenu inside \"%s\":"), ctx->parent_folder->title);
		} else {
			lstrcpy(prompt, TEXT("New Submenu inside Favourites:"));
		}
		hCtrl = CreateWindowEx(0, TEXT("STATIC"), prompt,
			WS_CHILD | WS_VISIBLE | SS_LEFT,
			margin, y, width - 2 * margin, label_h,
			hDlg, (HMENU)IDC_FAV_STATIC_NAME_PROMPT, hInst, NULL);
		SendMessage(hCtrl, WM_SETFONT, (WPARAM)hFont, FALSE);
		y += label_h + Scale(6);

		// Edit control
		hCtrl = CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("EDIT"), TEXT(""),
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
			margin, y, width - 2 * margin, edit_h,
			hDlg, (HMENU)IDC_FAV_EDIT_NAME, hInst, NULL);
		SendMessage(hCtrl, WM_SETFONT, (WPARAM)hFont, FALSE);
		y += edit_h + Scale(14);

		// OK button
		hCtrl = CreateWindowEx(0, TEXT("BUTTON"), TEXT("OK"),
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
			width - 2 * btn_w - margin - Scale(8), y, btn_w, btn_h,
			hDlg, (HMENU)IDOK, hInst, NULL);
		SendMessage(hCtrl, WM_SETFONT, (WPARAM)hFont, FALSE);

		// Cancel button
		hCtrl = CreateWindowEx(0, TEXT("BUTTON"), TEXT("Cancel"),
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
			width - btn_w - margin, y, btn_w, btn_h,
			hDlg, (HMENU)IDCANCEL, hInst, NULL);
		SendMessage(hCtrl, WM_SETFONT, (WPARAM)hFont, FALSE);

		dark_mode_set_dialog(hDlg);
		SetFocus(GetDlgItem(hDlg, IDC_FAV_EDIT_NAME));
		return FALSE;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDOK:
			{
				TCHAR name_buf[BUF_SIZE];
				TCHAR err_str[BUF_SIZE];
				TCHAR *start, *end;
				DATA_INFO **dest_root;
				DATA_INFO *new_fld;

				ctx = (NEW_FOLDER_CTX *)GetWindowLongPtr(hDlg, GWLP_USERDATA);
				GetDlgItemText(hDlg, IDC_FAV_EDIT_NAME, name_buf, BUF_SIZE - 1);

				// Trim leading and trailing whitespace
				start = name_buf;
				while (*start == TEXT(' ') || *start == TEXT('\t')) start++;
				end = start + lstrlen(start) - 1;
				while (end >= start && (*end == TEXT(' ') || *end == TEXT('\t') || *end == TEXT('\r') || *end == TEXT('\n'))) {
					*end = TEXT('\0');
					end--;
				}

				if (*start == TEXT('\0')) {
					MessageBox(hDlg, TEXT("Please enter a submenu name."), TEXT("New Submenu"), MB_ICONINFORMATION);
					SetFocus(GetDlgItem(hDlg, IDC_FAV_EDIT_NAME));
					return TRUE;
				}

				dest_root = (ctx != NULL && ctx->parent_folder != NULL) ?
					&ctx->parent_folder->child : &regist_data.child;

				err_str[0] = TEXT('\0');
				new_fld = regist_create_folder(dest_root, start, err_str);
				if (new_fld == NULL) {
					MessageBox(hDlg, TEXT("A submenu with this name already exists."), TEXT("New Submenu"), MB_ICONWARNING);
					SetFocus(GetDlgItem(hDlg, IDC_FAV_EDIT_NAME));
					return TRUE;
				}

				save_regist(hDlg);
				last_created_folder = new_fld;
				EndDialog(hDlg, TRUE);
				return TRUE;
			}

		case IDCANCEL:
			EndDialog(hDlg, FALSE);
			return TRUE;
		}
		break;

	case WM_CLOSE:
		EndDialog(hDlg, FALSE);
		return TRUE;
	}
	return FALSE;
}

/*
 * favorites_show_new_folder_dialog - display New Submenu dialog
 */
BOOL favorites_show_new_folder_dialog(const HWND hWnd, DATA_INFO *parent_folder)
{
	NEW_FOLDER_CTX ctx;
	HGLOBAL hg;
	INT_PTR ret;

	ctx.parent_folder = parent_folder;
	last_created_folder = NULL;

	hg = create_dialog_template(L"New Submenu", Scale(200), Scale(85));
	if (hg == NULL) {
		return FALSE;
	}
	ret = DialogBoxIndirectParam(hInst, (DLGTEMPLATE *)GlobalLock(hg),
		hWnd, new_folder_dlg_proc, (LPARAM)&ctx);
	GlobalUnlock(hg);
	GlobalFree(hg);

	return (ret == TRUE) ? TRUE : FALSE;
}

/*
 * rename_folder_dlg_proc - dialog procedure for renaming an existing submenu
 */
static INT_PTR CALLBACK rename_folder_dlg_proc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	DATA_INFO *folder_di;
	RECT rc;
	int width;
	int margin, y, label_h, edit_h, btn_w, btn_h;
	HFONT hFont;
	TCHAR prompt[BUF_SIZE];
	HWND hCtrl;

	switch (uMsg) {
	case WM_INITDIALOG:
		SetWindowLongPtr(hDlg, GWLP_USERDATA, (LONG_PTR)lParam);
		folder_di = (DATA_INFO *)lParam;

		GetClientRect(hDlg, &rc);
		width = rc.right - rc.left;
		margin = Scale(12);
		y = Scale(10);
		label_h = Scale(18);
		edit_h = Scale(24);
		btn_w = Scale(75);
		btn_h = Scale(24);

		hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

		// Prompt label
		if (folder_di != NULL && folder_di->title != NULL) {
			wsprintf(prompt, TEXT("Rename submenu \"%s\":"), folder_di->title);
		} else {
			lstrcpy(prompt, TEXT("Submenu Name:"));
		}
		hCtrl = CreateWindowEx(0, TEXT("STATIC"), prompt,
			WS_CHILD | WS_VISIBLE | SS_LEFT,
			margin, y, width - 2 * margin, label_h,
			hDlg, (HMENU)IDC_FAV_STATIC_NAME_PROMPT, hInst, NULL);
		SendMessage(hCtrl, WM_SETFONT, (WPARAM)hFont, FALSE);
		y += label_h + Scale(6);

		// Edit control
		hCtrl = CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("EDIT"),
			(folder_di != NULL && folder_di->title != NULL) ? folder_di->title : TEXT(""),
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
			margin, y, width - 2 * margin, edit_h,
			hDlg, (HMENU)IDC_FAV_EDIT_NAME, hInst, NULL);
		SendMessage(hCtrl, WM_SETFONT, (WPARAM)hFont, FALSE);
		SendMessage(hCtrl, EM_SETSEL, 0, -1);
		y += edit_h + Scale(14);

		// OK button
		hCtrl = CreateWindowEx(0, TEXT("BUTTON"), TEXT("OK"),
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
			width - 2 * btn_w - margin - Scale(8), y, btn_w, btn_h,
			hDlg, (HMENU)IDOK, hInst, NULL);
		SendMessage(hCtrl, WM_SETFONT, (WPARAM)hFont, FALSE);

		// Cancel button
		hCtrl = CreateWindowEx(0, TEXT("BUTTON"), TEXT("Cancel"),
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
			width - btn_w - margin, y, btn_w, btn_h,
			hDlg, (HMENU)IDCANCEL, hInst, NULL);
		SendMessage(hCtrl, WM_SETFONT, (WPARAM)hFont, FALSE);

		dark_mode_set_dialog(hDlg);
		SetFocus(GetDlgItem(hDlg, IDC_FAV_EDIT_NAME));
		return FALSE;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDOK:
			{
				TCHAR name_buf[BUF_SIZE];
				TCHAR *start, *end;
				DATA_INFO *parent;
				DATA_INFO *head;
				DATA_INFO *sibling;
				TCHAR *new_title;

				folder_di = (DATA_INFO *)GetWindowLongPtr(hDlg, GWLP_USERDATA);
				if (folder_di == NULL) {
					EndDialog(hDlg, FALSE);
					return TRUE;
				}

				GetDlgItemText(hDlg, IDC_FAV_EDIT_NAME, name_buf, BUF_SIZE - 1);

				// Trim leading and trailing whitespace
				start = name_buf;
				while (*start == TEXT(' ') || *start == TEXT('\t')) start++;
				end = start + lstrlen(start) - 1;
				while (end >= start && (*end == TEXT(' ') || *end == TEXT('\t') || *end == TEXT('\r') || *end == TEXT('\n'))) {
					*end = TEXT('\0');
					end--;
				}

				if (*start == TEXT('\0')) {
					MessageBox(hDlg, TEXT("Please enter a submenu name."), TEXT("Rename Submenu"), MB_ICONINFORMATION);
					SetFocus(GetDlgItem(hDlg, IDC_FAV_EDIT_NAME));
					return TRUE;
				}

				// If unchanged, close with success
				if (folder_di->title != NULL && lstrcmp(folder_di->title, start) == 0) {
					EndDialog(hDlg, TRUE);
					return TRUE;
				}

				// Check for sibling folder with duplicate name
				parent = data_check(&regist_data, folder_di);
				head = (parent != NULL) ? parent->child : regist_data.child;
				for (sibling = head; sibling != NULL; sibling = sibling->next) {
					if (sibling != folder_di && sibling->type == TYPE_FOLDER && sibling->title != NULL) {
						if (lstrcmpi(sibling->title, start) == 0) {
							MessageBox(hDlg, TEXT("A submenu with this name already exists."), TEXT("Rename Submenu"), MB_ICONWARNING);
							SetFocus(GetDlgItem(hDlg, IDC_FAV_EDIT_NAME));
							return TRUE;
						}
					}
				}

				new_title = alloc_copy(start);
				if (new_title == NULL) {
					return TRUE;
				}
				if (folder_di->title != NULL) {
					mem_free((void **)&folder_di->title);
				}
				folder_di->title = new_title;

				save_regist(hDlg);
				EndDialog(hDlg, TRUE);
				return TRUE;
			}

		case IDCANCEL:
			EndDialog(hDlg, FALSE);
			return TRUE;
		}
		break;

	case WM_CLOSE:
		EndDialog(hDlg, FALSE);
		return TRUE;
	}
	return FALSE;
}

/*
 * favorites_show_rename_folder_dialog - display Rename Submenu dialog
 */
BOOL favorites_show_rename_folder_dialog(const HWND hWnd, DATA_INFO *folder_di)
{
	HGLOBAL hg;
	INT_PTR ret;

	if (folder_di == NULL || folder_di->type != TYPE_FOLDER) {
		return FALSE;
	}

	hg = create_dialog_template(L"Rename Submenu", Scale(200), Scale(85));
	if (hg == NULL) {
		return FALSE;
	}
	ret = DialogBoxIndirectParam(hInst, (DLGTEMPLATE *)GlobalLock(hg),
		hWnd, rename_folder_dlg_proc, (LPARAM)folder_di);
	GlobalUnlock(hg);
	GlobalFree(hg);

	if (ret == TRUE) {
		SendMessage(hWnd, WM_REGIST_CHANGED, 0, 0);
		return TRUE;
	}
	return FALSE;
}

/*
 * create_menu_bitmap_from_icon - create 32bpp PARGB bitmap for menu items
 */
static HBITMAP create_menu_bitmap_from_icon(HINSTANCE hInstance, int icon_res_id, int size)
{
	HICON hIcon = (HICON)LoadImage(hInstance, MAKEINTRESOURCE(icon_res_id), IMAGE_ICON, size, size, 0);
	HDC hdcScreen;
	HDC hdcMem;
	BITMAPINFO bi;
	void *pvBits = NULL;
	HBITMAP hBmp = NULL;
	HBITMAP hOldBmp;

	if (hIcon == NULL) {
		return NULL;
	}
	hdcScreen = GetDC(NULL);
	if (hdcScreen == NULL) {
		DestroyIcon(hIcon);
		return NULL;
	}
	hdcMem = CreateCompatibleDC(hdcScreen);
	if (hdcMem == NULL) {
		ReleaseDC(NULL, hdcScreen);
		DestroyIcon(hIcon);
		return NULL;
	}

	ZeroMemory(&bi, sizeof(bi));
	bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth = size;
	bi.bmiHeader.biHeight = -size; // Top-down DIB
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;

	hBmp = CreateDIBSection(hdcScreen, &bi, DIB_RGB_COLORS, &pvBits, NULL, 0);
	if (hBmp != NULL && pvBits != NULL) {
		ZeroMemory(pvBits, size * size * 4);
		hOldBmp = (HBITMAP)SelectObject(hdcMem, hBmp);
		DrawIconEx(hdcMem, 0, 0, hIcon, size, size, 0, NULL, DI_NORMAL);
		SelectObject(hdcMem, hOldBmp);

		// Ensure valid alpha channel for 32bpp PARGB
		{
			DWORD *pixels = (DWORD *)pvBits;
			int count = size * size;
			int i;
			BOOL has_alpha = FALSE;
			for (i = 0; i < count; i++) {
				if ((pixels[i] & 0xFF000000) != 0) {
					has_alpha = TRUE;
					break;
				}
			}
			if (!has_alpha) {
				for (i = 0; i < count; i++) {
					if ((pixels[i] & 0x00FFFFFF) != 0) {
						pixels[i] |= 0xFF000000;
					}
				}
			}
		}
	}

	DeleteDC(hdcMem);
	ReleaseDC(NULL, hdcScreen);
	DestroyIcon(hIcon);
	return hBmp;
}

/*
 * favorites_add_item_to_folder - deep-copy clipboard item into target folder in regist_data
 */
static BOOL favorites_add_item_to_folder(const HWND hWnd, DATA_INFO *cb_item, DATA_INFO *target_folder)
{
	DATA_INFO **dest_root = (target_folder != NULL) ? &target_folder->child : &regist_data.child;
	DATA_INFO *copy_di;
	TCHAR err_str[BUF_SIZE];

	if (cb_item == NULL) {
		return FALSE;
	}

	// Ensure data payloads are loaded if using SQLite history
	if (db_history_is_open()) {
		db_history_ensure_item_data(cb_item);
	}

	err_str[0] = TEXT('\0');
	copy_di = data_item_copy(cb_item, FALSE, FALSE, err_str);
	if (copy_di == NULL) {
		MessageBox(hWnd, TEXT("Failed to copy clipboard item to Favourites."), TEXT("Error"), MB_ICONERROR);
		return FALSE;
	}

	// Remove window name so favorite has clean display
	mem_free(&copy_di->window_name);

	// Append to target folder
	if (*dest_root == NULL) {
		*dest_root = copy_di;
	} else {
		DATA_INFO *cur;
		for (cur = *dest_root; cur->next != NULL; cur = cur->next)
			;
		cur->next = copy_di;
	}

	save_regist(hWnd);
	return TRUE;
}

/*
 * favorites_build_submenus - recursively build cascading menus for folder hierarchy
 */
static void favorites_build_submenus(HMENU hParentMenu, DATA_INFO *folder_head, HBITMAP hBmpFolder)
{
	DATA_INFO *di;

	for (di = folder_head; di != NULL; di = di->next) {
		if (di->type != TYPE_FOLDER) {
			continue;
		}

		if (fav_folder_cnt >= MAX_FAV_FOLDERS) {
			break;
		}

		// Check if this folder has child folders
		BOOL has_child_folders = FALSE;
		DATA_INFO *cdi;
		for (cdi = di->child; cdi != NULL; cdi = cdi->next) {
			if (cdi->type == TYPE_FOLDER) {
				has_child_folders = TRUE;
				break;
			}
		}

		if (!has_child_folders) {
			// Leaf folder: direct menu item
			int idx = fav_folder_cnt++;
			fav_folder_map[idx] = di;
			AppendMenu(hParentMenu, MF_STRING, ID_FAV_FOLDER_BASE + idx, di->title != NULL ? di->title : TEXT("(Folder)"));
			if (hBmpFolder != NULL) {
				MENUITEMINFO mii;
				ZeroMemory(&mii, sizeof(mii));
				mii.cbSize = sizeof(mii);
				mii.fMask = MIIM_BITMAP;
				mii.hbmpItem = hBmpFolder;
				SetMenuItemInfo(hParentMenu, ID_FAV_FOLDER_BASE + idx, FALSE, &mii);
			}
		} else {
			// Branch folder: cascading submenu
			HMENU hSub = CreatePopupMenu();
			int idx = fav_folder_cnt++;
			fav_folder_map[idx] = di;
			TCHAR buf[BUF_SIZE];

			// "Add to <Folder>" item
			wsprintf(buf, TEXT("Add to \"%s\""), di->title != NULL ? di->title : TEXT("Folder"));
			AppendMenu(hSub, MF_STRING, ID_FAV_FOLDER_BASE + idx, buf);
			if (hBmpFolder != NULL) {
				MENUITEMINFO mii;
				ZeroMemory(&mii, sizeof(mii));
				mii.cbSize = sizeof(mii);
				mii.fMask = MIIM_BITMAP;
				mii.hbmpItem = hBmpFolder;
				SetMenuItemInfo(hSub, ID_FAV_FOLDER_BASE + idx, FALSE, &mii);
			}

			AppendMenu(hSub, MF_SEPARATOR, 0, NULL);

			// Recursively add child folders
			favorites_build_submenus(hSub, di->child, hBmpFolder);

			AppendMenu(hSub, MF_SEPARATOR, 0, NULL);

			// "New Submenu in <Folder>..."
			wsprintf(buf, TEXT("New Submenu in \"%s\"..."), di->title != NULL ? di->title : TEXT("Folder"));
			AppendMenu(hSub, MF_STRING, ID_FAV_NEW_SUB_BASE + idx, buf);
			if (hBmpFolder != NULL) {
				MENUITEMINFO mii;
				ZeroMemory(&mii, sizeof(mii));
				mii.cbSize = sizeof(mii);
				mii.fMask = MIIM_BITMAP;
				mii.hbmpItem = hBmpFolder;
				SetMenuItemInfo(hSub, ID_FAV_NEW_SUB_BASE + idx, FALSE, &mii);
			}

			// Attach to parent menu
			AppendMenu(hParentMenu, MF_POPUP, (UINT_PTR)hSub, di->title != NULL ? di->title : TEXT("(Folder)"));
			if (hBmpFolder != NULL) {
				MENUITEMINFO mii;
				ZeroMemory(&mii, sizeof(mii));
				mii.cbSize = sizeof(mii);
				mii.fMask = MIIM_BITMAP;
				mii.hbmpItem = hBmpFolder;
				SetMenuItemInfo(hParentMenu, (UINT)GetMenuItemCount(hParentMenu) - 1, TRUE, &mii);
			}
		}
	}
}

/*
 * favorites_show_add_menu - display standard Win32 cascading context menu for adding item to favorites
 */
BOOL favorites_show_add_menu(const HWND hWnd, DATA_INFO *cb_item, const POINT pt, BOOL *deleted)
{
	HMENU hMenu;
	HMENU hFavSubMenu;
	HBITMAP hBmpRegist = NULL;
	HBITMAP hBmpFolder = NULL;
	int icon_size;
	int cmd;
	BOOL has_subfolders = FALSE;
	DATA_INFO *cdi;

	if (cb_item == NULL) {
		return FALSE;
	}
	if (deleted != NULL) {
		*deleted = FALSE;
	}

	icon_size = GetSystemMetrics(SM_CXSMICON);
	if (icon_size <= 0) {
		icon_size = 16;
	}
	hBmpRegist = create_menu_bitmap_from_icon(hInst, IDI_ICON_REGIST, icon_size);
	hBmpFolder = create_menu_bitmap_from_icon(hInst, IDI_ICON_FOLDER, icon_size);

	hMenu = CreatePopupMenu();
	if (hMenu == NULL) {
		if (hBmpRegist != NULL) DeleteObject(hBmpRegist);
		if (hBmpFolder != NULL) DeleteObject(hBmpFolder);
		return FALSE;
	}
	hFavSubMenu = CreatePopupMenu();
	if (hFavSubMenu == NULL) {
		DestroyMenu(hMenu);
		if (hBmpRegist != NULL) DeleteObject(hBmpRegist);
		if (hBmpFolder != NULL) DeleteObject(hBmpFolder);
		return FALSE;
	}

	fav_folder_cnt = 0;

	// 1. "Favorites (Root)"
	AppendMenu(hFavSubMenu, MF_STRING, ID_FAV_ROOT, TEXT("Favorites (Root)"));
	if (hBmpRegist != NULL) {
		MENUITEMINFO mii;
		ZeroMemory(&mii, sizeof(mii));
		mii.cbSize = sizeof(mii);
		mii.fMask = MIIM_BITMAP;
		mii.hbmpItem = hBmpRegist;
		SetMenuItemInfo(hFavSubMenu, ID_FAV_ROOT, FALSE, &mii);
	}

	// 2. Submenus under regist_data.child
	for (cdi = regist_data.child; cdi != NULL; cdi = cdi->next) {
		if (cdi->type == TYPE_FOLDER) {
			has_subfolders = TRUE;
			break;
		}
	}
	if (has_subfolders) {
		AppendMenu(hFavSubMenu, MF_SEPARATOR, 0, NULL);
		favorites_build_submenus(hFavSubMenu, regist_data.child, hBmpFolder);
	}

	// 3. New Submenu...
	AppendMenu(hFavSubMenu, MF_SEPARATOR, 0, NULL);
	AppendMenu(hFavSubMenu, MF_STRING, ID_FAV_NEW_SUBMENU_ROOT, TEXT("New Submenu..."));
	if (hBmpFolder != NULL) {
		MENUITEMINFO mii;
		ZeroMemory(&mii, sizeof(mii));
		mii.cbSize = sizeof(mii);
		mii.fMask = MIIM_BITMAP;
		mii.hbmpItem = hBmpFolder;
		SetMenuItemInfo(hFavSubMenu, ID_FAV_NEW_SUBMENU_ROOT, FALSE, &mii);
	}

	// In root context menu: Add to Favorites >
	AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hFavSubMenu, TEXT("Add to Favorites"));
	if (hBmpRegist != NULL) {
		MENUITEMINFO mii;
		ZeroMemory(&mii, sizeof(mii));
		mii.cbSize = sizeof(mii);
		mii.fMask = MIIM_BITMAP;
		mii.hbmpItem = hBmpRegist;
		SetMenuItemInfo(hMenu, (UINT)GetMenuItemCount(hMenu) - 1, TRUE, &mii);
	}

	AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
	AppendMenu(hMenu, MF_STRING, ID_FAV_DELETE, TEXT("Delete\tDel"));

	// Apply dark mode theme if enabled
	if (dark_mode_is_dark() == TRUE) {
		MENUINFO mi;
		ZeroMemory(&mi, sizeof(mi));
		mi.cbSize = sizeof(mi);
		mi.fMask = MIM_BACKGROUND | MIM_APPLYTOSUBMENUS;
		mi.hbrBack = dark_mode_get_brush(COLOR_MENU);
		SetMenuInfo(hMenu, &mi);
	}

	_SetForegroundWindow(hWnd);
	cmd = TrackPopupMenuEx(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_RIGHTBUTTON,
		pt.x, pt.y, hWnd, NULL);

	if (cmd == ID_FAV_ROOT) {
		favorites_add_item_to_folder(hWnd, cb_item, NULL);
	} else if (cmd >= ID_FAV_FOLDER_BASE && cmd < ID_FAV_FOLDER_BASE + fav_folder_cnt) {
		DATA_INFO *target_fld = fav_folder_map[cmd - ID_FAV_FOLDER_BASE];
		favorites_add_item_to_folder(hWnd, cb_item, target_fld);
	} else if (cmd == ID_FAV_NEW_SUBMENU_ROOT) {
		if (favorites_show_new_folder_dialog(hWnd, NULL) == TRUE && last_created_folder != NULL) {
			favorites_add_item_to_folder(hWnd, cb_item, last_created_folder);
		}
	} else if (cmd >= ID_FAV_NEW_SUB_BASE && cmd < ID_FAV_NEW_SUB_BASE + fav_folder_cnt) {
		DATA_INFO *parent_fld = fav_folder_map[cmd - ID_FAV_NEW_SUB_BASE];
		if (favorites_show_new_folder_dialog(hWnd, parent_fld) == TRUE && last_created_folder != NULL) {
			favorites_add_item_to_folder(hWnd, cb_item, last_created_folder);
		}
	} else if (cmd == ID_FAV_DELETE) {
		data_delete(&history_data.child, cb_item, TRUE);
		if (deleted != NULL) {
			*deleted = TRUE;
		}
		SendMessage(hWnd, WM_HISTORY_CHANGED, 0, 0);
	}

	DestroyMenu(hMenu);
	if (hBmpRegist != NULL) {
		DeleteObject(hBmpRegist);
	}
	if (hBmpFolder != NULL) {
		DeleteObject(hBmpFolder);
	}

	return (cmd != 0);
}

/*
 * favorites_show_folder_menu - display RMB context menu for Favourites root or subfolder
 */
BOOL favorites_show_folder_menu(const HWND hWnd, DATA_INFO *folder_di, const POINT pt, BOOL *deleted)
{
	HMENU hMenu;
	HBITMAP hBmpFolder = NULL;
	int icon_size;
	UINT cmd;
	BOOL is_root = (folder_di == NULL || folder_di == &regist_data);
	if (deleted != NULL) {
		*deleted = FALSE;
	}

	icon_size = GetSystemMetrics(SM_CXSMICON);
	if (icon_size <= 0) {
		icon_size = 16;
	}
	hBmpFolder = create_menu_bitmap_from_icon(hInst, IDI_ICON_FOLDER, icon_size);

	hMenu = CreatePopupMenu();
	if (hMenu == NULL) {
		if (hBmpFolder != NULL) {
			DeleteObject(hBmpFolder);
		}
		return FALSE;
	}

	AppendMenu(hMenu, MF_STRING, ID_FAV_NEW_SUBMENU_ROOT, TEXT("New Submenu..."));
	if (hBmpFolder != NULL) {
		MENUITEMINFO mii;
		ZeroMemory(&mii, sizeof(mii));
		mii.cbSize = sizeof(mii);
		mii.fMask = MIIM_BITMAP;
		mii.hbmpItem = hBmpFolder;
		SetMenuItemInfo(hMenu, ID_FAV_NEW_SUBMENU_ROOT, FALSE, &mii);
	}

	if (!is_root) {
		AppendMenu(hMenu, MF_STRING, ID_FAV_RENAME_SUBMENU, TEXT("Rename Submenu..."));
	}

	{
		HBITMAP hBmpRegist = create_menu_bitmap_from_icon(hInst, IDI_ICON_REGIST, icon_size);
		AppendMenu(hMenu, MF_STRING, ID_FAV_ORGANIZE, TEXT("&Organize Favourites..."));
		if (hBmpRegist != NULL) {
			MENUITEMINFO mii;
			ZeroMemory(&mii, sizeof(mii));
			mii.cbSize = sizeof(mii);
			mii.fMask = MIIM_BITMAP;
			mii.hbmpItem = hBmpRegist;
			SetMenuItemInfo(hMenu, ID_FAV_ORGANIZE, FALSE, &mii);
			DeleteObject(hBmpRegist);
		}
	}

	if (!is_root) {
		AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
		AppendMenu(hMenu, MF_STRING, ID_FAV_DELETE_SUBMENU, TEXT("Delete Submenu"));
	}

	if (dark_mode_is_dark() == TRUE) {
		MENUINFO mi;
		ZeroMemory(&mi, sizeof(mi));
		mi.cbSize = sizeof(mi);
		mi.fMask = MIM_BACKGROUND | MIM_APPLYTOSUBMENUS;
		mi.hbrBack = dark_mode_get_brush(COLOR_MENU);
		SetMenuInfo(hMenu, &mi);
	}

	_SetForegroundWindow(hWnd);
	cmd = TrackPopupMenuEx(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_RIGHTBUTTON,
		pt.x, pt.y, hWnd, NULL);

	if (cmd == ID_FAV_NEW_SUBMENU_ROOT) {
		favorites_show_new_folder_dialog(hWnd, is_root ? NULL : folder_di);
	} else if (cmd == ID_FAV_RENAME_SUBMENU && !is_root) {
		favorites_show_rename_folder_dialog(hWnd, folder_di);
	} else if (cmd == ID_FAV_DELETE_SUBMENU && !is_root) {
		data_delete(&regist_data.child, folder_di, TRUE);
		if (deleted != NULL) {
			*deleted = TRUE;
		}
		SendMessage(hWnd, WM_REGIST_CHANGED, 0, 0);
	} else if (cmd == ID_FAV_ORGANIZE) {
		favorites_show_organize(hWnd, is_root ? NULL : folder_di);
	}

	DestroyMenu(hMenu);
	if (hBmpFolder != NULL) {
		DeleteObject(hBmpFolder);
	}
	return (cmd != 0);
}

/*
 * favorites_show_item_menu - display RMB context menu for item within Favourites
 */
BOOL favorites_show_item_menu(const HWND hWnd, DATA_INFO *fav_item, const POINT pt, BOOL *deleted)
{
	HMENU hMenu;
	UINT cmd;
	int icon_size;

	if (fav_item == NULL) {
		return FALSE;
	}
	if (deleted != NULL) {
		*deleted = FALSE;
	}

	hMenu = CreatePopupMenu();
	if (hMenu == NULL) {
		return FALSE;
	}

	icon_size = GetSystemMetrics(SM_CXSMICON);
	if (icon_size <= 0) {
		icon_size = 16;
	}
	{
		HBITMAP hBmpRegist = create_menu_bitmap_from_icon(hInst, IDI_ICON_REGIST, icon_size);
		AppendMenu(hMenu, MF_STRING, ID_FAV_ORGANIZE, TEXT("&Organize Favourites..."));
		if (hBmpRegist != NULL) {
			MENUITEMINFO mii;
			ZeroMemory(&mii, sizeof(mii));
			mii.cbSize = sizeof(mii);
			mii.fMask = MIIM_BITMAP;
			mii.hbmpItem = hBmpRegist;
			SetMenuItemInfo(hMenu, ID_FAV_ORGANIZE, FALSE, &mii);
			DeleteObject(hBmpRegist);
		}
	}

	AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
	AppendMenu(hMenu, MF_STRING, ID_FAV_DELETE_ITEM, TEXT("Delete\tDel"));

	if (dark_mode_is_dark() == TRUE) {
		MENUINFO mi;
		ZeroMemory(&mi, sizeof(mi));
		mi.cbSize = sizeof(mi);
		mi.fMask = MIM_BACKGROUND | MIM_APPLYTOSUBMENUS;
		mi.hbrBack = dark_mode_get_brush(COLOR_MENU);
		SetMenuInfo(hMenu, &mi);
	}

	_SetForegroundWindow(hWnd);
	cmd = TrackPopupMenuEx(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_RIGHTBUTTON,
		pt.x, pt.y, hWnd, NULL);

	if (cmd == ID_FAV_DELETE_ITEM) {
		data_delete(&regist_data.child, fav_item, TRUE);
		if (deleted != NULL) {
			*deleted = TRUE;
		}
		SendMessage(hWnd, WM_REGIST_CHANGED, 0, 0);
	} else if (cmd == ID_FAV_ORGANIZE) {
		favorites_show_organize(hWnd, fav_item);
	}

	DestroyMenu(hMenu);
	return (cmd != 0);
}

/*
 * favorites_show_organize - show viewer focused on favourites tree
 */
BOOL favorites_show_organize(const HWND hWnd, DATA_INFO *target_folder)
{
	LPARAM lp = (target_folder != NULL && target_folder != &regist_data) ? (LPARAM)target_folder : (LPARAM)&regist_data;
	SendMessage(hWnd, WM_VIEWER_SHOW, 0, lp);
	return TRUE;
}

/*
 * favorites_show_add_dialog - display Add to Favorites menu (compatibility wrapper)
 */
BOOL favorites_show_add_dialog(const HWND hWnd, DATA_INFO *cb_item)
{
	POINT pt;
	GetCursorPos(&pt);
	return favorites_show_add_menu(hWnd, cb_item, pt, NULL);
}
/* End of source */
