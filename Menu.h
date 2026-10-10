/*
 * CLCL
 *
 * Menu.h
 *
 * Copyright (C) 1996-2019 by Ohno Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 *		nakka@nakka.com
 */

#ifndef _INC_MENU_H
#define _INC_MENU_H

/* Include Files */
#include "Data.h"
#include "Tool.h"

/* Define */
#define ID_MENUITEM_DATA				50000

#define MENU_CONTENT_SEPARATOR			0
#define MENU_CONTENT_HISTORY			1
#define MENU_CONTENT_HISTORY_DESC		2
#define MENU_CONTENT_REGIST				3
#define MENU_CONTENT_REGIST_DESC		4
#define MENU_CONTENT_POPUP				5
#define MENU_CONTENT_VIEWER				6
#define MENU_CONTENT_OPTION				7
#define MENU_CONTENT_CLIPBOARD_WATCH	8
#define MENU_CONTENT_TOOL				9
#define MENU_CONTENT_APP				10
#define MENU_CONTENT_CANCEL				11
#define MENU_CONTENT_EXIT				12

/* Struct */
// menu item
typedef struct _MENU_ITEM_INFO {
	UINT id;							// Menu ID
	UINT flag;							// Menu flag
	LPCTSTR item;						// Menu item content

	TCHAR *text;						// Text to display in menu
	int text_x;							// Text position
	int text_y;

	HICON icon;							// Icon to display in menu
	BOOL free_icon;

	TCHAR *hkey;

	BOOL show_format;					// Show format
	BOOL show_bitmap;					// Show bitmap

	DATA_INFO *set_di;					// Data information
	DATA_INFO *show_di;					// Data information to display

	TOOL_INFO *ti;						// Tool information

	struct _MENU_INFO *mi;				// Base MENU_INFO structure

	// popup
	struct _MENU_ITEM_INFO *mii;		// Child item of popup menu
	int mii_cnt;						// Number of child items in popup menu

	BOOL is_folder;						// Folder item
	BOOL is_folder_child;				// Inside folder submenu
	BOOL is_favourites;					// Favourites item or folder
} MENU_ITEM_INFO;

// menu info
typedef struct _MENU_INFO {
	int content;						// MENU_CONTENT_
	TCHAR *title;						// Title to display in menu

	// icon
	TCHAR *icon_path;					// Path of icon to display in menu (main executable if empty)
	int icon_index;						// Index of icon to display in menu

	// path
	TCHAR *path;						// Path (MENU_CONTENT_REGIST, MENU_CONTENT_APP)
	TCHAR *cmd;							// Command line (MENU_CONTENT_APP)

	int min;							// History display start value (MENU_CONTENT_HISTORY)
	int max;							// History display end value (MENU_CONTENT_HISTORY)

	// popup
	struct _MENU_INFO *mi;				// Child item of popup menu (MENU_CONTENT_POPUP)
	int mi_cnt;							// Number of child items in popup menu
} MENU_INFO;

/* Function Prototypes */
void menu_free(void);
void menu_free_icons(void);
void menu_set_dpi(const POINT *mpos);
int menu_show(const HWND hWnd, const HMENU hMenu, const POINT *mpos);
int menu_show_align(const HWND hWnd, const HMENU hMenu, const POINT *mpos, const UINT align_flags);
MENU_ITEM_INFO *menu_get_info(const UINT id);
TCHAR *menu_get_keyname(const UINT modifiers, const UINT virtkey);
HMENU menu_create(const HWND hWnd, MENU_INFO *menu_info, const int menu_cnt,
				  DATA_INFO *history_di, DATA_INFO *regist_di);
void menu_destory(HMENU hMenu);
BOOL menu_set_drawitem(MEASUREITEMSTRUCT *ms);
BOOL menu_drawitem(const DRAWITEMSTRUCT *ds);
LRESULT menu_accelerator(const HMENU hMenu, const TCHAR key);
int menu_get_selectable_index_by_datainfo(const DATA_INFO *target_di);

#endif
/* End of source */
