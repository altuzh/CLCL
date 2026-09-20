/*
 * CLCL
 *
 * Data.c
 *
 * Copyright (C) 1996-2019 by Ohno Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 *		nakka@nakka.com
 */

/* Include Files */
#define _INC_OLE
#include <windows.h>
#undef  _INC_OLE

#include "General.h"
#include "Memory.h"
#include "String.h"
#include "Data.h"
#include "Ini.h"
#include "Message.h"
#include "ClipBoard.h"
#include "Format.h"
#include "DbHistory.h"

/* Define */

/* Global Variables */
// Options
extern OPTION_INFO option;

/* Local Function Prototypes */

/*
 * data_create_data - create data
 */
DATA_INFO *data_create_data(const UINT format, TCHAR *format_name, const HANDLE data, const DWORD size, const BOOL init, TCHAR *err_str)
{
	DATA_INFO *new_item;

	// Allocate item
	if ((new_item = (DATA_INFO *)mem_calloc(sizeof(DATA_INFO))) == NULL) {
		message_get_error(GetLastError(), err_str);
		return NULL;
	}
	new_item->struct_size = sizeof(DATA_INFO);
	new_item->type = TYPE_DATA;
	new_item->format = (format != 0) ? format : clipboard_get_format(0, format_name);
	new_item->format_name = alloc_copy(format_name);
	new_item->format_name_hash = str2hash(new_item->format_name);
	new_item->data = data;
	new_item->size = size;
	format_initialize_item(new_item, (data == NULL) ? init : FALSE);
	return new_item;
}

/*
 * data_create_item - create item
 */
DATA_INFO *data_create_item(const TCHAR *title, const BOOL set_date, TCHAR *err_str)
{
	DATA_INFO *new_item;

	// Allocate item
	if ((new_item = (DATA_INFO *)mem_calloc(sizeof(DATA_INFO))) == NULL) {
		message_get_error(GetLastError(), err_str);
		return NULL;
	}
	new_item->struct_size = sizeof(DATA_INFO);
	new_item->type = TYPE_ITEM;
	new_item->title = alloc_copy(title);
	if (set_date == TRUE) {
		data_set_modified(new_item);
	}
	return new_item;
}

/*
 * data_create_folder - create folder
 */
DATA_INFO *data_create_folder(const TCHAR *title, TCHAR *err_str)
{
	DATA_INFO *new_item;

	// Allocate item
	if ((new_item = (DATA_INFO *)mem_calloc(sizeof(DATA_INFO))) == NULL) {
		message_get_error(GetLastError(), err_str);
		return NULL;
	}
	new_item->struct_size = sizeof(DATA_INFO);
	new_item->title = alloc_copy(title);
	new_item->type = TYPE_FOLDER;
	return new_item;
}

/*
 * data_item_copy - create item copy
 */
DATA_INFO *data_item_copy(const DATA_INFO *di, const BOOL next_copy, const BOOL move_flag, TCHAR *err_str)
{
	DATA_INFO *new_di;

	if (di == NULL) {
		return NULL;
	}
	if ((new_di = (DATA_INFO *)mem_calloc(sizeof(DATA_INFO))) == NULL) {
		message_get_error(GetLastError(), err_str);
		return NULL;
	}
	new_di->struct_size = sizeof(DATA_INFO);
	new_di->type = di->type;
	new_di->title = alloc_copy(di->title);
	new_di->format_name = alloc_copy(di->format_name);
	new_di->format_name_hash = di->format_name_hash;
	new_di->format = (di->format != 0) ? di->format : clipboard_get_format(0, di->format_name);
	new_di->modified.dwLowDateTime = di->modified.dwLowDateTime;
	new_di->modified.dwHighDateTime = di->modified.dwHighDateTime;
	new_di->window_name = alloc_copy(di->window_name);
	new_di->plugin_string = alloc_copy(di->plugin_string);
	new_di->plugin_param = di->plugin_param;
	if (move_flag == TRUE) {
		new_di->hkey_id = di->hkey_id;
		new_di->op_modifiers = di->op_modifiers;
		new_di->op_virtkey = di->op_virtkey;
		new_di->op_paste = di->op_paste;
	}
	// Copy data
	if (di->data != NULL && (new_di->data = format_copy_data(di->format_name, di->data, &new_di->size)) == NULL) {
		UINT fmt = (di->format != 0) ? di->format : clipboard_get_format(0, di->format_name);
		new_di->data = clipboard_copy_data(fmt, di->data, &new_di->size);
	}

	// Copy child item
	if (di->child != NULL && (new_di->child = data_item_copy(di->child, TRUE, move_flag, err_str)) == NULL) {
		data_free(new_di);
		return NULL;
	}
	// Copy next item
	if (next_copy == TRUE && di->next != NULL &&
		(new_di->next = data_item_copy(di->next, TRUE, move_flag, err_str)) == NULL) {
		data_free(new_di);
		return NULL;
	}
	return new_di;
}

/*
 * data_delete - delete item
 */
BOOL data_delete(DATA_INFO **root, DATA_INFO *del_di, const BOOL free_item)
{
	DATA_INFO *di;

	if (root == NULL || *root == NULL || del_di == NULL) {
		return FALSE;
	}
	if (*root == del_di) {
		*root = del_di->next;
		del_di->next = NULL;
		if (free_item == TRUE) {
			if (db_history_is_open() && del_di->type == TYPE_ITEM && del_di->param1 > 0) {
				db_history_delete_item((int)del_di->param1);
				del_di->param1 = 0;
			}
			data_free(del_di);
		}
		return TRUE;
	}
	for (di = *root; di != NULL; di = di->next) {
		if (di->next == del_di) {
			// Delete
			di->next = del_di->next;
			del_di->next = NULL;
			if (free_item == TRUE) {
				if (db_history_is_open() && del_di->type == TYPE_ITEM && del_di->param1 > 0) {
					db_history_delete_item((int)del_di->param1);
					del_di->param1 = 0;
				}
				data_free(del_di);
			}
			return TRUE;
		}
		if (di->child != NULL && data_delete(&di->child, del_di, free_item) == TRUE) {
			return TRUE;
		}
	}
	return FALSE;
}

/*
 * data_adjust - organize items
 */
void data_adjust(DATA_INFO **root)
{
	DATA_INFO *di = *root;
	DATA_INFO *wk_di;

	while (di != NULL) {
		if (di->type == TYPE_ITEM && di->child == NULL) {
			wk_di = di->next;
			// Delete
			data_delete(root, di, TRUE);
			di = wk_di;
		} else {
			if (di->type == TYPE_FOLDER) {
				data_adjust(&di->child);
			}
			di = di->next;
		}
	}
}

/*
 * data_menu_free - free menu information associated with item
 */
void data_menu_free_item(DATA_INFO *di)
{
	// Free text
	if (di->free_title == TRUE) {
		mem_free(&di->menu_title);
	}
	di->menu_title = NULL;
	di->free_title = FALSE;

	// Free icon
	if (di->free_icon == TRUE && di->menu_icon != NULL) {
		DestroyIcon(di->menu_icon);
	}
	di->menu_icon = NULL;
	di->free_icon = FALSE;

	// Free bitmap
	if (di->free_bitmap == TRUE && di->menu_bitmap != NULL) {
		DeleteObject((HGDIOBJ)di->menu_bitmap);
	}
	di->menu_bitmap = NULL;
	di->free_bitmap = FALSE;
	di->menu_bmp_width = 0;
	di->menu_bmp_height = 0;
}
void data_menu_free(DATA_INFO *di)
{
	for (; di != NULL; di = di->next) {
		data_menu_free_item(di);
		if (di->child != NULL) {
			data_menu_free(di->child);
		}
	}
}

/*
 * data_free - free item
 */
void data_free(DATA_INFO *di)
{
	DATA_INFO *wk_di;

	while (di != NULL) {
		wk_di = di->next;

		if (di->child != NULL) {
			data_free(di->child);
		}

		format_free_item(di);
		if (di->data != NULL && format_free_data(di->format_name, di->data) == FALSE) {
			clipboard_free_data(di->format_name, di->data);
		}
		data_menu_free_item(di);
		mem_free(&di->title);
		mem_free(&di->format_name);
		mem_free(&di->window_name);
		mem_free(&di->plugin_string);
		mem_free(&di);

		di = wk_di;
	}
}

/*
 * data_check - check item existence
 */
DATA_INFO *data_check(DATA_INFO *di, const DATA_INFO *check_di)
{
	DATA_INFO *cdi;
	DATA_INFO *ret_di;

	if (di == NULL || di->child == NULL) {
		return NULL;
	}
	for (cdi = di->child; cdi != NULL; cdi = cdi->next) {
		if (cdi == check_di) {
			return di;
		}
		if ((ret_di = data_check(cdi, check_di)) != NULL) {
			return ret_di;
		}
	}
	return NULL;
}

/*
 * data_set_modified - set modified date/time
 */
void data_set_modified(DATA_INFO *di)
{
	SYSTEMTIME sys_time;

	if (di->type != TYPE_ITEM) {
		ZeroMemory(&di->modified, sizeof(FILETIME));
		return;
	}
	GetLocalTime(&sys_time);
	SystemTimeToFileTime(&sys_time, &di->modified);
}

/*
 * data_get_modified_string - get modified date/time string
 */
BOOL data_get_modified_string(const DATA_INFO *di, TCHAR *ret)
{
	SYSTEMTIME sys_time;
	TCHAR str_day[BUF_SIZE], str_time[BUF_SIZE];
	TCHAR *p;

	if (di->type != TYPE_ITEM ||
		(di->modified.dwLowDateTime == 0 && di->modified.dwHighDateTime == 0)) {
		*ret = TEXT('\0');
		return FALSE;
	}
	// Convert file time to system time
	if (FileTimeToSystemTime(&di->modified, &sys_time) == FALSE) {
		*ret = TEXT('\0');
		return FALSE;
	}
	// Get date string
	p = option.data_date_format;
	if (p == NULL || *p == TEXT('\0')) {
		p = NULL;
	}
	GetDateFormat(0, 0, &sys_time, p, str_day, BUF_SIZE - 1);
	// Get time string
	p = option.data_time_format;
	if (p == NULL || *p == TEXT('\0')) {
		p = NULL;
	}
	GetTimeFormat(0, 0, &sys_time, p, str_time, BUF_SIZE - 1);

	wsprintf(ret, TEXT("%s %s"), str_day, str_time);
	return TRUE;
}

/*
 * data_get_title - get item title
 */
TCHAR *data_get_title(DATA_INFO *di)
{
	DATA_INFO *wk_di;
	static TCHAR buf[BUF_SIZE];
	TCHAR *p;
	TCHAR *ret;

	wk_di = format_get_priority_highest(di);
	format_get_menu_title(wk_di);

	if (di->title != NULL) {
		ret = di->title;
	} else if (wk_di->menu_title != NULL) {
		ret = wk_di->menu_title;
	} else if (wk_di->format_name != NULL) {
		p = buf;
		*(p++) = TEXT('(');
		lstrcpyn(p, wk_di->format_name, BUF_SIZE - 3);
		p += lstrlen(p);
		*(p++) = TEXT(')');
		*(p++) = TEXT('\0');
		ret = buf;
	} else {
		ret = TEXT("");
	}
	return ret;
}

/*
 * fnv1a_64 - 64-bit FNV-1a hash algorithm
 */
UINT64 fnv1a_64(const void *data, const size_t len, const UINT64 seed)
{
	const BYTE *p = (const BYTE *)data;
	UINT64 hash = seed ? seed : 14695981039346656037ULL;
	size_t i;

	if (data == NULL || len == 0) {
		return 0;
	}
	for (i = 0; i < len; i++) {
		hash ^= (UINT64)p[i];
		hash *= 1099511628211ULL;
	}
	return hash;
}

/*
 * data_calc_hash - compute content hash for duplicate prevention
 */
UINT64 data_calc_hash(DATA_INFO *di)
{
	DATA_INFO *c;
	UINT64 hash = 0;

	if (di == NULL) {
		return 0;
	}
	if (di->content_hash != 0) {
		return di->content_hash;
	}

	// Ensure lazy DB items have their data loaded
	if (db_history_is_open()) {
		if (di->param2 == 0 && di->param1 > 0) {
			db_history_ensure_item_data(di);
		}
	}

	if (di->type == TYPE_ITEM) {
		// 1. Text formats (CF_UNICODETEXT / CF_TEXT)
		DATA_INFO *t_di = NULL;
		for (c = di->child; c != NULL; c = c->next) {
			if (c->format == CF_UNICODETEXT || (c->format_name != NULL && lstrcmpi(c->format_name, TEXT("UNICODE TEXT")) == 0)) {
				t_di = c;
				break;
			}
			if (t_di == NULL && (c->format == CF_TEXT || (c->format_name != NULL && lstrcmpi(c->format_name, TEXT("TEXT")) == 0))) {
				t_di = c;
			}
		}
		if (t_di != NULL) {
			if (t_di->data != NULL) {
				if (t_di->format == CF_UNICODETEXT) {
					const WCHAR *w = (const WCHAR *)GlobalLock(t_di->data);
					if (w != NULL) {
						hash = fnv1a_64(w, wcslen(w) * sizeof(WCHAR), 0xCBF29CE484222325ULL);
						GlobalUnlock(t_di->data);
					}
				} else {
					const char *a = (const char *)GlobalLock(t_di->data);
					if (a != NULL) {
						TCHAR *w = alloc_char_to_tchar(a);
						if (w != NULL) {
							hash = fnv1a_64(w, lstrlen(w) * sizeof(TCHAR), 0xCBF29CE484222325ULL);
							mem_free((void **)&w);
						} else {
							hash = fnv1a_64(a, strlen(a), 0xCBF29CE484222325ULL);
						}
						GlobalUnlock(t_di->data);
					}
				}
			}
			if (hash == 0) {
				DWORD size = 0;
				BYTE *mem = format_data_to_bytes(t_di, &size);
				if (mem == NULL) mem = clipboard_data_to_bytes(t_di, &size);
				if (mem != NULL && size > 0) {
					hash = fnv1a_64(mem, size, 0xCBF29CE484222325ULL);
					mem_free((void **)&mem);
				}
			}
			if (hash != 0) {
				di->content_hash = hash;
				return hash;
			}
		}

		// 2. Bitmap formats (CF_BITMAP / CF_DIB / CF_DIBV5)
		DATA_INFO *b_di = NULL;
		for (c = di->child; c != NULL; c = c->next) {
			if (c->format == CF_DIB || c->format == CF_DIBV5 || c->format == CF_BITMAP ||
				(c->format_name != NULL && (lstrcmpi(c->format_name, TEXT("BITMAP")) == 0 || lstrcmpi(c->format_name, TEXT("DIB")) == 0))) {
				b_di = c;
				break;
			}
		}
		if (b_di != NULL) {
			DWORD size = 0;
			BYTE *mem = format_data_to_bytes(b_di, &size);
			if (mem == NULL) mem = clipboard_data_to_bytes(b_di, &size);
			if (mem != NULL && size > 0) {
				hash = fnv1a_64(mem, size, 0x811C9DC517B4C8FBULL);
				mem_free((void **)&mem);
			}
			if (hash != 0) {
				di->content_hash = hash;
				return hash;
			}
		}

		// 3. Fallback: hash the primary child format's data bytes
		for (c = di->child; c != NULL; c = c->next) {
			DWORD size = 0;
			BYTE *mem = format_data_to_bytes(c, &size);
			if (mem == NULL) mem = clipboard_data_to_bytes(c, &size);
			if (mem != NULL && size > 0) {
				hash = fnv1a_64(mem, size, (UINT64)c->format);
				mem_free((void **)&mem);
				break;
			}
		}
		if (hash == 0 && di->title != NULL && *di->title != TEXT('\0')) {
			hash = fnv1a_64(di->title, lstrlen(di->title) * sizeof(TCHAR), 0x54657874ULL);
		}
	} else {
		// Single DATA node
		DWORD size = 0;
		BYTE *mem = format_data_to_bytes(di, &size);
		if (mem == NULL) mem = clipboard_data_to_bytes(di, &size);
		if (mem != NULL && size > 0) {
			hash = fnv1a_64(mem, size, (UINT64)di->format);
			mem_free((void **)&mem);
		}
	}

	di->content_hash = hash;
	return hash;
}
/* End of source */
