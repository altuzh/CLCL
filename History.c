/*
 * CLCL
 *
 * History.c
 *
 * Copyright (C) 1996-2019 by Ohno Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 *		nakka@nakka.com
 */

/* Include Files */
#define _INC_OLE
#include <windows.h>
#undef  _INC_OLE
#include <stdlib.h>

#include "General.h"
#include "Memory.h"
#include "String.h"
#include "Data.h"
#include "Ini.h"
#include "Message.h"
#include "History.h"
#include "ClipBoard.h"
#include "Format.h"
#include "Filter.h"
#include "DbHistory.h"
#include "DarkMode.h"

/* Define */

/* Global Variables */
// Options
extern OPTION_INFO option;
extern DATA_INFO history_data;

/* Local Function Prototypes */
static BOOL history_compare(DATA_INFO *d1, DATA_INFO *d2);
static BOOL history_overlap_check(DATA_INFO **root, DATA_INFO *new_item);
static int history_item_cmp_desc(const void *a, const void *b);

/*
 * history_compare - compare history
 */
static BOOL history_compare(DATA_INFO *d1, DATA_INFO *d2)
{
	BYTE *mem1, *mem2;
	UINT size1, size2;
	int ret;
	UINT64 h1, h2;

	if (d1 == NULL || d2 == NULL) {
		return FALSE;
	}

	h1 = (d1->content_hash != 0) ? d1->content_hash : data_calc_hash(d1);
	h2 = (d2->content_hash != 0) ? d2->content_hash : data_calc_hash(d2);

	if (h1 != 0 && h2 != 0) {
		return (h1 == h2);
	}

	// Ensure lazy-loaded data from SQLite is loaded
	if (db_history_is_open()) {
		if (d1->param2 == 0 && d1->param1 > 0) {
			db_history_ensure_item_data(d1);
		}
		if (d2->param2 == 0 && d2->param1 > 0) {
			db_history_ensure_item_data(d2);
		}
	}

	// 1. Text comparison: if both contain text formats, compare text content directly
	{
		DATA_INFO *t1 = NULL, *t2 = NULL;
		DATA_INFO *c;
		for (c = d1->child; c != NULL; c = c->next) {
			if (c->format == CF_UNICODETEXT || (c->format_name != NULL && lstrcmpi(c->format_name, TEXT("UNICODE TEXT")) == 0)) {
				t1 = c;
				break;
			}
			if (t1 == NULL && (c->format == CF_TEXT || (c->format_name != NULL && lstrcmpi(c->format_name, TEXT("TEXT")) == 0))) {
				t1 = c;
			}
		}
		for (c = d2->child; c != NULL; c = c->next) {
			if (c->format == CF_UNICODETEXT || (c->format_name != NULL && lstrcmpi(c->format_name, TEXT("UNICODE TEXT")) == 0)) {
				t2 = c;
				break;
			}
			if (t2 == NULL && (c->format == CF_TEXT || (c->format_name != NULL && lstrcmpi(c->format_name, TEXT("TEXT")) == 0))) {
				t2 = c;
			}
		}

		if (t1 != NULL && t2 != NULL) {
			if (t1->data != NULL && t2->data != NULL) {
				if (t1->format == CF_UNICODETEXT && t2->format == CF_UNICODETEXT) {
					const WCHAR *w1 = (const WCHAR *)GlobalLock(t1->data);
					const WCHAR *w2 = (const WCHAR *)GlobalLock(t2->data);
					if (w1 != NULL && w2 != NULL) {
						BOOL match = (wcscmp(w1, w2) == 0);
						GlobalUnlock(t1->data);
						GlobalUnlock(t2->data);
						return match;
					}
					if (w1 != NULL) GlobalUnlock(t1->data);
					if (w2 != NULL) GlobalUnlock(t2->data);
				} else if (t1->format == CF_TEXT && t2->format == CF_TEXT) {
					const char *a1 = (const char *)GlobalLock(t1->data);
					const char *a2 = (const char *)GlobalLock(t2->data);
					if (a1 != NULL && a2 != NULL) {
						BOOL match = (strcmp(a1, a2) == 0);
						GlobalUnlock(t1->data);
						GlobalUnlock(t2->data);
						return match;
					}
					if (a1 != NULL) GlobalUnlock(t1->data);
					if (a2 != NULL) GlobalUnlock(t2->data);
				}
			}
			mem1 = format_data_to_bytes(t1, &size1);
			if (mem1 == NULL) mem1 = clipboard_data_to_bytes(t1, &size1);
			mem2 = format_data_to_bytes(t2, &size2);
			if (mem2 == NULL) mem2 = clipboard_data_to_bytes(t2, &size2);
			if (mem1 != NULL && mem2 != NULL) {
				ret = mem_cmp(mem1, size1, mem2, size2);
				mem_free(&mem1);
				mem_free(&mem2);
				return (ret == 0);
			}
			mem_free(&mem1);
			mem_free(&mem2);
		} else if (t1 != NULL || t2 != NULL) {
			return FALSE;
		}
	}

	// 2. Bitmap comparison: if both contain bitmap formats
	{
		DATA_INFO *b1 = NULL, *b2 = NULL;
		DATA_INFO *c;
		for (c = d1->child; c != NULL; c = c->next) {
			if (c->format == CF_DIB || c->format == CF_DIBV5 || c->format == CF_BITMAP ||
				(c->format_name != NULL && (lstrcmpi(c->format_name, TEXT("BITMAP")) == 0 || lstrcmpi(c->format_name, TEXT("DIB")) == 0))) {
				b1 = c;
				break;
			}
		}
		for (c = d2->child; c != NULL; c = c->next) {
			if (c->format == CF_DIB || c->format == CF_DIBV5 || c->format == CF_BITMAP ||
				(c->format_name != NULL && (lstrcmpi(c->format_name, TEXT("BITMAP")) == 0 || lstrcmpi(c->format_name, TEXT("DIB")) == 0))) {
				b2 = c;
				break;
			}
		}

		if (b1 != NULL && b2 != NULL) {
			mem1 = format_data_to_bytes(b1, &size1);
			if (mem1 == NULL) mem1 = clipboard_data_to_bytes(b1, &size1);
			mem2 = format_data_to_bytes(b2, &size2);
			if (mem2 == NULL) mem2 = clipboard_data_to_bytes(b2, &size2);
			if (mem1 != NULL && mem2 != NULL) {
				ret = mem_cmp(mem1, size1, mem2, size2);
				mem_free(&mem1);
				mem_free(&mem2);
				return (ret == 0);
			}
			mem_free(&mem1);
			mem_free(&mem2);
			return FALSE;
		} else if (b1 != NULL || b2 != NULL) {
			return FALSE;
		}
	}

	// 3. Fallback: compare all child formats strictly
	d1 = d1->child;
	d2 = d2->child;
	if (d1 == NULL || d2 == NULL) {
		return FALSE;
	}

	while (d1 != NULL && d2 != NULL) {
		// Compare format and size
		if (d1->format != d2->format || d1->size != d2->size) {
			return FALSE;
		}
		// Compare memory
		if ((mem1 = format_data_to_bytes(d1, &size1)) == NULL) {
			mem1 = clipboard_data_to_bytes(d1, &size1);
		}
		if ((mem2 = format_data_to_bytes(d2, &size2)) == NULL) {
			mem2 = clipboard_data_to_bytes(d2, &size2);
		}
		if (mem1 == NULL || mem2 == NULL) {
			mem_free(&mem1);
			mem_free(&mem2);
			return FALSE;
		}
		ret = mem_cmp(mem1, size1, mem2, size2);
		mem_free(&mem1);
		mem_free(&mem2);

		if (ret != 0) {
			return FALSE;
		}

		d1 = d1->next;
		d2 = d2->next;
	}
	return ((d1 == NULL && d2 == NULL) ? TRUE : FALSE);
}

/*
 * history_get_item_date - get locale formatted date string and normalized start-of-day FILETIME
 */
BOOL history_get_item_date(const DATA_INFO *di, TCHAR *date_buf, FILETIME *ft_day)
{
	SYSTEMTIME sys_time;
	SYSTEMTIME day_sys_time;
	const TCHAR *fmt;

	if (date_buf == NULL) {
		return FALSE;
	}

	if (di == NULL || (di->modified.dwLowDateTime == 0 && di->modified.dwHighDateTime == 0)) {
		GetLocalTime(&sys_time);
	} else if (FileTimeToSystemTime(&di->modified, &sys_time) == FALSE) {
		GetLocalTime(&sys_time);
	}

	// Format date string using user locale date format or custom format
	fmt = option.data_date_format;
	if (fmt == NULL || *fmt == TEXT('\0')) {
		fmt = NULL;
	}
	if (GetDateFormat(LOCALE_USER_DEFAULT, (fmt == NULL) ? DATE_SHORTDATE : 0, &sys_time, fmt, date_buf, BUF_SIZE - 1) == 0) {
		// Fallback to YYYY-MM-DD if formatting fails
		wsprintf(date_buf, TEXT("%04d-%02d-%02d"), sys_time.wYear, sys_time.wMonth, sys_time.wDay);
	}

	if (ft_day != NULL) {
		// Calculate normalized start-of-day FILETIME (00:00:00.000) for chronological sorting
		ZeroMemory(&day_sys_time, sizeof(SYSTEMTIME));
		day_sys_time.wYear = sys_time.wYear;
		day_sys_time.wMonth = sys_time.wMonth;
		day_sys_time.wDay = sys_time.wDay;
		SystemTimeToFileTime(&day_sys_time, ft_day);
	}
	return TRUE;
}

/*
 * history_pop_to_date_folder - move a popped item into its date folder
 */
BOOL history_pop_to_date_folder(DATA_INFO **root, DATA_INFO *popped_item)
{
	DATA_INFO *di, *prev = NULL;
	DATA_INFO *target_folder = NULL;
	TCHAR date_buf[BUF_SIZE];
	FILETIME ft_day;

	if (root == NULL || popped_item == NULL) {
		return FALSE;
	}

	popped_item->next = NULL;
	history_get_item_date(popped_item, date_buf, &ft_day);

	// Look for existing folder matching date_buf
	for (di = *root; di != NULL; di = di->next) {
		if (di->type == TYPE_FOLDER) {
			if (di->title != NULL && lstrcmp(di->title, date_buf) == 0) {
				target_folder = di;
				break;
			}
		}
	}

	if (target_folder != NULL) {
		// Prepend popped_item to folder's children (newest popped item first)
		popped_item->next = target_folder->child;
		target_folder->child = popped_item;
		return TRUE;
	}

	// Folder does not exist, create a new one
	target_folder = data_create_folder(date_buf, NULL);
	if (target_folder == NULL) {
		data_free(popped_item);
		return FALSE;
	}
	target_folder->modified = ft_day;
	target_folder->child = popped_item;

	// Insert target_folder in sorted position among TYPE_FOLDER nodes (after all TYPE_ITEM nodes)
	// Folders are sorted descending by modified timestamp
	prev = NULL;
	for (di = *root; di != NULL; prev = di, di = di->next) {
		if (di->type == TYPE_FOLDER) {
			if (CompareFileTime(&ft_day, &di->modified) > 0) {
				// Target folder is newer than current folder, insert before di
				break;
			}
		}
	}

	if (prev == NULL) {
		target_folder->next = *root;
		*root = target_folder;
	} else {
		target_folder->next = prev->next;
		prev->next = target_folder;
	}
	return TRUE;
}

/*
 * history_item_cmp_desc - comparator for sorting items by modified timestamp descending
 */
static int history_item_cmp_desc(const void *a, const void *b)
{
	const DATA_INFO *d1 = *(const DATA_INFO **)a;
	const DATA_INFO *d2 = *(const DATA_INFO **)b;
	int cmp = CompareFileTime(&d1->modified, &d2->modified);
	if (cmp != 0) {
		return -cmp;
	}
	if (d1->param1 != d2->param1) {
		return (d1->param1 < d2->param1) ? 1 : -1;
	}
	return 0;
}

/*
 * history_restructure - reorganize history items into top-level and date folders
 */
BOOL history_restructure(DATA_INFO **root, const int max_items)
{
	DATA_INFO *cur, *next;
	DATA_INFO **items = NULL;
	DATA_INFO *last_top = NULL;
	DATA_INFO *folder_head = NULL;
	DATA_INFO *folder_tail = NULL;
	DATA_INFO *current_folder = NULL;
	DATA_INFO *current_folder_tail = NULL;
	TCHAR current_date[BUF_SIZE] = {0};
	int count = 0;
	int capacity = 1024;
	int top_limit;
	int top_count;
	int i, k;

	if (root == NULL) {
		return FALSE;
	}
	if (*root == NULL) {
		return TRUE;
	}

	top_limit = (max_items > 0) ? max_items : 0;
	items = (DATA_INFO **)mem_alloc(sizeof(DATA_INFO *) * capacity);
	if (items == NULL) {
		return FALSE;
	}

	// 1. Flatten all TYPE_ITEM nodes from root and existing TYPE_FOLDER nodes
	cur = *root;
	while (cur != NULL) {
		next = cur->next;
		if (cur->type == TYPE_ITEM) {
			if (count >= capacity) {
				int new_capacity = capacity * 2;
				DATA_INFO **new_items = (DATA_INFO **)mem_alloc(sizeof(DATA_INFO *) * new_capacity);
				if (new_items == NULL) {
					mem_free((void **)&items);
					return FALSE;
				}
				CopyMemory(new_items, items, sizeof(DATA_INFO *) * count);
				mem_free((void **)&items);
				items = new_items;
				capacity = new_capacity;
			}
			cur->next = NULL;
			items[count++] = cur;
		} else if (cur->type == TYPE_FOLDER) {
			DATA_INFO *child = cur->child;
			while (child != NULL) {
				DATA_INFO *cnext = child->next;
				if (child->type == TYPE_ITEM) {
					if (count >= capacity) {
						int new_capacity = capacity * 2;
						DATA_INFO **new_items = (DATA_INFO **)mem_alloc(sizeof(DATA_INFO *) * new_capacity);
						if (new_items == NULL) {
							mem_free((void **)&items);
							return FALSE;
						}
						CopyMemory(new_items, items, sizeof(DATA_INFO *) * count);
						mem_free((void **)&items);
						items = new_items;
						capacity = new_capacity;
					}
					child->next = NULL;
					items[count++] = child;
				}
				child = cnext;
			}
			cur->child = NULL;
			cur->next = NULL;
			data_free(cur);
		}
		cur = next;
	}
	*root = NULL;

	if (count == 0) {
		mem_free((void **)&items);
		return TRUE;
	}

	// 2. Sort all items descending by timestamp
	qsort(items, count, sizeof(DATA_INFO *), history_item_cmp_desc);

	// 3. Rebuild list: first min(count, top_limit) become top-level
	top_count = (count < top_limit) ? count : top_limit;
	for (i = 0; i < top_count; i++) {
		items[i]->next = NULL;
		if (*root == NULL) {
			*root = items[i];
		} else {
			last_top->next = items[i];
		}
		last_top = items[i];
	}

	// 4. Remaining items (from top_count to count - 1) go into date folders
	for (k = top_count; k < count; k++) {
		TCHAR item_date[BUF_SIZE];
		FILETIME ft_day;
		history_get_item_date(items[k], item_date, &ft_day);

		if (current_folder == NULL || lstrcmp(current_date, item_date) != 0) {
			// Create new date folder
			current_folder = data_create_folder(item_date, NULL);
			if (current_folder == NULL) {
				data_free(items[k]);
				continue;
			}
			current_folder->modified = ft_day;
			current_folder->child = items[k];
			items[k]->next = NULL;
			current_folder_tail = items[k];
			lstrcpyn(current_date, item_date, BUF_SIZE);

			if (folder_head == NULL) {
				folder_head = current_folder;
			} else {
				folder_tail->next = current_folder;
			}
			folder_tail = current_folder;
		} else {
			// Append item to current folder
			current_folder_tail->next = items[k];
			items[k]->next = NULL;
			current_folder_tail = items[k];
		}
	}

	// 5. Link date folders after the last top-level item
	if (folder_head != NULL) {
		if (last_top != NULL) {
			last_top->next = folder_head;
		} else {
			*root = folder_head;
		}
	}

	mem_free((void **)&items);
	return TRUE;
}

/*
 * history_overlap_check - check duplicate history
 */
static BOOL history_overlap_check(DATA_INFO **root, DATA_INFO *new_item)
{
	DATA_INFO *di;
	DATA_INFO *wk_di;

	if (*root == NULL) {
		return TRUE;
	}
	switch (option.history_overlap_check) {
	case 1:
		// Check for duplicate items
		if (history_compare(*root, new_item) == TRUE) {
			return FALSE;
		}
		break;

	case 2:
		// Check for duplicates across all items
		for (di = *root; di != NULL; di = di->next) {
			if (di->type == TYPE_FOLDER) {
				DATA_INFO *cdi;
				for (cdi = di->child; cdi != NULL; cdi = cdi->next) {
					if (history_compare(cdi, new_item) == TRUE) {
						return FALSE;
					}
				}
			} else if (history_compare(di, new_item) == TRUE) {
				return FALSE;
			}
		}
		break;

	case 3:
		// If already at head, skip as duplicate without deleting and re-adding
		if (history_compare(*root, new_item) == TRUE) {
			return FALSE;
		}
		// Delete duplicate items
		di = *root;
		wk_di = NULL;
		while (di != NULL) {
			if (di->type == TYPE_FOLDER) {
				DATA_INFO *cdi = di->child;
				DATA_INFO *wk_cdi = NULL;
				while (cdi != NULL) {
					if (history_compare(cdi, new_item) == TRUE) {
						DATA_INFO *del_cdi = cdi;
						cdi = cdi->next;
						if (wk_cdi == NULL) {
							di->child = cdi;
						} else {
							wk_cdi->next = cdi;
						}
						del_cdi->next = NULL;
						if (db_history_is_open() && del_cdi->type == TYPE_ITEM && del_cdi->param1 > 0) {
							db_history_delete_item((int)del_cdi->param1);
							del_cdi->param1 = 0;
						}
						data_free(del_cdi);
					} else {
						wk_cdi = cdi;
						cdi = cdi->next;
					}
				}
				// If folder became empty, delete folder
				if (di->child == NULL) {
					DATA_INFO *del_folder = di;
					di = di->next;
					if (wk_di == NULL) {
						*root = di;
					} else {
						wk_di->next = di;
					}
					del_folder->next = NULL;
					data_free(del_folder);
					continue;
				}
				wk_di = di;
				di = di->next;
				continue;
			}
			if (history_compare(di, new_item) == FALSE) {
				wk_di = di;
				di = di->next;
				continue;
			}
			// Delete item
			if (db_history_is_open() && di->type == TYPE_ITEM && di->param1 > 0) {
				db_history_delete_item((int)di->param1);
				di->param1 = 0;
			}
			if (wk_di == NULL) {
				*root = di->next;
				di->next = NULL;
				data_free(di);
				di = *root;
			} else {
				wk_di->next = di->next;
				di->next = NULL;
				data_free(di);
				di = wk_di;
			}
		}
		break;
	}
	return TRUE;
}

/*
 * history_add - add item to history
 */
BOOL history_add(DATA_INFO **root, DATA_INFO *new_item, const BOOL overlap_check)
{
	DATA_INFO *di;
	TCHAR buf[BUF_SIZE];
	int i;

	// Check for duplicates
	if (overlap_check == TRUE && history_overlap_check(root, new_item) == FALSE) {
		return FALSE;
	}

	// Date and time
	data_set_modified(new_item);

	// Window name
	*buf = TEXT('\0');
	GetWindowText(GetForegroundWindow(), buf, BUF_SIZE - 1);
	mem_free(&new_item->window_name);
	new_item->window_name = alloc_copy(buf);

	// Add to head of list
	if (*root == NULL) {
		*root = new_item;
	} else {
		new_item->next = *root;
		*root = new_item;
	}

	// Pop items exceeding history_max into date folders
	if (option.history_max > 0) {
		DATA_INFO *prev = NULL;
		di = *root;
		for (i = 0; di != NULL && di->type == TYPE_ITEM; i++) {
			if (i >= option.history_max) {
				DATA_INFO *popped = di;
				di = di->next;
				if (prev != NULL) {
					prev->next = di;
				} else {
					*root = di;
				}
				popped->next = NULL;
				history_pop_to_date_folder(root, popped);
				break;
			}
			prev = di;
			di = di->next;
		}
	}
	return TRUE;
}

/*
 * history_show_folder_menu - display RMB context menu for date folder
 */
BOOL history_show_folder_menu(const HWND hWnd, DATA_INFO *folder_di, const POINT pt, BOOL *deleted)
{
	HMENU hMenu;
	UINT cmd;

	if (folder_di == NULL) {
		return FALSE;
	}
	if (deleted != NULL) {
		*deleted = FALSE;
	}

	hMenu = CreatePopupMenu();
	if (hMenu == NULL) {
		return FALSE;
	}

	AppendMenu(hMenu, MF_STRING, 1, TEXT("Delete Submenu"));

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

	if (cmd == 1) {
		data_delete(&history_data.child, folder_di, TRUE);
		if (deleted != NULL) {
			*deleted = TRUE;
		}
		SendMessage(hWnd, WM_HISTORY_CHANGED, 0, 0);
	}

	DestroyMenu(hMenu);
	return (cmd != 0);
}
/* End of source */
