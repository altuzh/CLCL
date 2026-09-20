/*
 * CLCL
 *
 * Data.h
 *
 * Copyright (C) 1996-2019 by Ohno Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 *		nakka@nakka.com
 */

#ifndef _INC_DATA_H
#define _INC_DATA_H

/* Include Files */

/* Define */
// data type
#define TYPE_DATA						0
#define TYPE_ITEM						1
#define TYPE_FOLDER						2
#define TYPE_ROOT						3

/* Struct */
// Item information
typedef struct _DATA_INFO {
	DWORD struct_size;					// Structure size

	int type;							// TYPE_
	TCHAR *title;						// Title

	TCHAR *format_name;					// Format name
	int format_name_hash;				// Format name hash
	UINT format;						// Format value

	HANDLE data;						// Data
	DWORD size;							// Size

	FILETIME modified;					// Modified date and time
	TCHAR *window_name;					// Copied window title

	TCHAR *plugin_string;				// Data for plug-in
	LPARAM plugin_param;

// Information not saved below
	TCHAR *menu_title;					// Title to display in menu (displays format if not set)
	BOOL free_title;					// Title: TRUE-Free FALSE-Do not free
	HICON menu_icon;					// Icon handle to display in menu
	BOOL free_icon;						// Icon handle: TRUE-Free FALSE-Do not free
	HBITMAP menu_bitmap;				// Bitmap to display in menu
	BOOL free_bitmap;					// Bitmap handle: TRUE-Free FALSE-Do not free
	int menu_bmp_width;					// Individual size of bitmap to display in menu
	int menu_bmp_height;
	LPARAM param1;						// Data for plug-in
	LPARAM param2;

	struct _DATA_INFO *child;
	struct _DATA_INFO *next;

// Ver 1.0.5
	int hkey_id;						// Hotkey
	UINT op_modifiers;
	UINT op_virtkey;
	int op_paste;
	UINT64 content_hash;				// Content hash value (for duplicate check)
} DATA_INFO;

/* Function Prototypes */
DATA_INFO *data_create_data(const UINT format, TCHAR *format_name, const HANDLE data, const DWORD size, const BOOL init, TCHAR *err_str);
DATA_INFO *data_create_item(const TCHAR *title, const BOOL set_date, TCHAR *err_str);
DATA_INFO *data_create_folder(const TCHAR *title, TCHAR *err_str);
DATA_INFO *data_item_copy(const DATA_INFO *di, const BOOL next_copy, const BOOL move_flag, TCHAR *err_str);
BOOL data_delete(DATA_INFO **root, DATA_INFO *del_di, const BOOL free_item);
void data_adjust(DATA_INFO **root);
void data_menu_free_item(DATA_INFO *di);
void data_menu_free(DATA_INFO *di);
void data_free(DATA_INFO *di);
DATA_INFO *data_check(DATA_INFO *di, const DATA_INFO *check_di);
void data_set_modified(DATA_INFO *di);
BOOL data_get_modified_string(const DATA_INFO *di, TCHAR *ret);
TCHAR *data_get_title(DATA_INFO *di);
UINT64 fnv1a_64(const void *data, const size_t len, const UINT64 seed);
UINT64 data_calc_hash(DATA_INFO *di);

#endif
/* End of source */
