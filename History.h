/*
 * CLCL
 *
 * History.h
 *
 * Copyright (C) 1996-2019 by Ohno Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 *		nakka@nakka.com
 */

#ifndef _INC_HISTORY_H
#define _INC_HISTORY_H

/* Include Files */

/* Define */

/* Struct */

/* Function Prototypes */
BOOL history_get_item_date(const DATA_INFO *di, TCHAR *date_buf, FILETIME *ft_day);
BOOL history_pop_to_date_folder(DATA_INFO **root, DATA_INFO *popped_item);
BOOL history_restructure(DATA_INFO **root, const int max_items);
BOOL history_add(DATA_INFO **root, DATA_INFO *new_item, const BOOL overlap_check);
BOOL history_show_folder_menu(const HWND hWnd, DATA_INFO *folder_di, const POINT pt, BOOL *deleted);

#endif
/* End of source */
