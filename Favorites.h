/*
 * CLCL
 *
 * Favorites.h - Favourites management & dialogs
 *
 * Copyright (C) 1996-2026 by Ohno Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 *		nakka@nakka.com
 */

#ifndef _INC_FAVORITES_H
#define _INC_FAVORITES_H

/* Include Files */
#include "Data.h"

/* Define */
#define IDC_FAV_STATIC_ITEM			1001
#define IDC_FAV_EDIT_ITEM			1002
#define IDC_FAV_STATIC_FOLDER		1003
#define IDC_FAV_COMBO_FOLDER		1004
#define IDC_FAV_BTN_NEW_FOLDER		1005
#define IDC_FAV_STATIC_NAME_PROMPT	1006
#define IDC_FAV_EDIT_NAME			1007
#define ID_FAV_ORGANIZE				40007

/* Function Prototypes */
BOOL favorites_show_add_menu(const HWND hWnd, DATA_INFO *cb_item, const POINT pt, BOOL *deleted);
BOOL favorites_show_folder_menu(const HWND hWnd, DATA_INFO *folder_di, const POINT pt, BOOL *deleted);
BOOL favorites_show_item_menu(const HWND hWnd, DATA_INFO *fav_item, const POINT pt, BOOL *deleted);
BOOL favorites_show_add_dialog(const HWND hWnd, DATA_INFO *cb_item);
BOOL favorites_show_new_folder_dialog(const HWND hWnd, DATA_INFO *parent_folder);
BOOL favorites_show_rename_folder_dialog(const HWND hWnd, DATA_INFO *folder_di);
BOOL favorites_show_organize(const HWND hWnd, DATA_INFO *target_folder);

#endif	// _INC_FAVORITES_H
/* End of source */
