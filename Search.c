/*
 * CLCL
 *
 * Search.c
 *
 * Search popup for clipboard history - filter and select items by keyword
 */

/* Include Files */
#define _INC_OLE
#include <windows.h>
#undef  _INC_OLE
#include <tchar.h>
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")

#include "General.h"
#include "Memory.h"
#include "Data.h"
#include "Ini.h"
#include "Format.h"
#include "Font.h"
#include "dpi.h"
#include "DarkMode.h"
#include "Search.h"
#include "DbHistory.h"

/* Define */
#define BASE_SEARCH_WIDTH				420
#define BASE_SEARCH_HEIGHT				380
#define BASE_SEARCH_EDIT_HEIGHT			26
#define BASE_SEARCH_MARGIN				4

#define IDC_SEARCH_EDIT					1
#define IDC_SEARCH_LIST					2

#define SEARCH_INIT_CAPACITY			256
#define SEARCH_TEXT_BUF_SIZE			2048
#define SEARCH_MAX_MATCHES				32
#define SEARCH_MAX_CLUSTERS				6

/* Global Variables */
extern HINSTANCE hInst;
extern OPTION_INFO option;
extern DATA_INFO history_data;

/* Local Variables */
static DATA_INFO *search_result;
static DATA_INFO *search_history_root;
static HWND hSearchWnd;
static HWND hEditCtrl;
static HWND hListBox;
static WNDPROC origListBoxProc;
static WNDPROC origEditProc;
static HFONT hSearchFont;
static BOOL search_closing;

// Dynamic array mapping listbox index to DATA_INFO* and DB item ID
static DATA_INFO **search_items;
static int *search_item_db_ids;
static int search_item_count;
static int search_item_capacity;

static DB_SEARCH_RESULT *search_db_results;
static int search_db_count;

/* Local Function Prototypes */
static LRESULT CALLBACK search_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK listbox_subproc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK edit_subproc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static void search_populate(const TCHAR *filter);
static void search_collect(DATA_INFO *di, const TCHAR *filter, int target_chars);
static void search_add_item(DATA_INFO *di, const int db_id, const TCHAR *title);
static DATA_INFO *search_find_text_data(DATA_INFO *di);
static BOOL search_clean_string(const TCHAR *src, TCHAR *buf, int max_len);
static BOOL search_extract_preview(DATA_INFO *text_di, const TCHAR *filter, TCHAR *buf, int max_len, int target_chars);
static void search_select_item(void);
static void search_delete_selected_item(void);
static void search_close(void);
static HFONT search_create_font(void);
static void search_draw_item(const DRAWITEMSTRUCT *dis);

/*
 * search_regist - register window class
 */
BOOL search_regist(const HINSTANCE hInstance)
{
	WNDCLASS wc;

	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = (WNDPROC)search_proc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = hInstance;
	wc.hIcon = NULL;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wc.lpszMenuName = NULL;
	wc.lpszClassName = SEARCH_WND_CLASS;
	return RegisterClass(&wc);
}

/*
 * search_create_font - create font for search controls
 */
static HFONT search_create_font(void)
{
	NONCLIENTMETRICS ncMetrics;

	if (*option.menu_font_name != TEXT('\0')) {
		return font_create(option.menu_font_name, option.menu_font_size, option.menu_font_charset,
			option.menu_font_weight, (option.menu_font_italic == 0) ? FALSE : TRUE, FALSE);
	}

	ncMetrics.cbSize = sizeof(NONCLIENTMETRICS);
	if (SystemParametersInfo(SPI_GETNONCLIENTMETRICS,
		sizeof(NONCLIENTMETRICS), &ncMetrics, 0) == FALSE) {
		return NULL;
	}
	return CreateFontIndirect(&ncMetrics.lfMenuFont);
}

/*
 * search_find_text_data - find the text DATA_INFO child of an item, or NULL if non-text
 */
static DATA_INFO *search_find_text_data(DATA_INFO *di)
{
	DATA_INFO *cdi;

	if (di == NULL) {
		return NULL;
	}
	if (di->type == TYPE_DATA) {
		if (di->data != NULL) {
			if (di->format == CF_UNICODETEXT || di->format == CF_TEXT || di->format == CF_OEMTEXT) {
				return di;
			}
			if (di->format_name != NULL) {
				if (lstrcmpi(di->format_name, TEXT("UNICODE TEXT")) == 0 ||
					lstrcmpi(di->format_name, TEXT("TEXT")) == 0 ||
					lstrcmpi(di->format_name, TEXT("OEM TEXT")) == 0) {
					return di;
				}
			}
		}
		return NULL;
	}
	if (di->type == TYPE_ITEM) {
		// Prefer CF_UNICODETEXT first
		for (cdi = di->child; cdi != NULL; cdi = cdi->next) {
			if (cdi->data != NULL) {
				if (cdi->format == CF_UNICODETEXT) {
					return cdi;
				}
				if (cdi->format_name != NULL && lstrcmpi(cdi->format_name, TEXT("UNICODE TEXT")) == 0) {
					return cdi;
				}
			}
		}
		// Then CF_TEXT or CF_OEMTEXT
		for (cdi = di->child; cdi != NULL; cdi = cdi->next) {
			if (cdi->data != NULL) {
				if (cdi->format == CF_TEXT || cdi->format == CF_OEMTEXT) {
					return cdi;
				}
				if (cdi->format_name != NULL) {
					if (lstrcmpi(cdi->format_name, TEXT("TEXT")) == 0 ||
						lstrcmpi(cdi->format_name, TEXT("OEM TEXT")) == 0) {
						return cdi;
					}
				}
			}
		}
	}
	return NULL;
}

/*
 * search_clean_string - copy string removing newlines and collapsing whitespace
 */
static BOOL search_clean_string(const TCHAR *src, TCHAR *buf, int max_len)
{
	const TCHAR *p = src;
	TCHAR *r = buf;

	if (src == NULL || buf == NULL || max_len <= 0) {
		return FALSE;
	}
	// Skip leading whitespace and newlines
	while (*p != TEXT('\0') && (*p == TEXT(' ') || *p == TEXT('\t') || *p == TEXT('\r') || *p == TEXT('\n'))) {
		p++;
	}
	// Copy characters, replacing newlines/tabs with a single space
	while ((r - buf) < (max_len - 1) && *p != TEXT('\0')) {
		if (*p == TEXT('\r') || *p == TEXT('\n') || *p == TEXT('\t') || *p == TEXT(' ')) {
			if (r > buf && *(r - 1) != TEXT(' ')) {
				*r++ = TEXT(' ');
			}
			p++;
		} else {
			*r++ = *p++;
		}
	}
	// Trim trailing spaces
	while (r > buf && *(r - 1) == TEXT(' ')) {
		r--;
	}
	*r = TEXT('\0');
	return (*buf != TEXT('\0'));
}

/*
 * append_clean_chars_w - append clean characters from src starting at start_pos
 * Collapses consecutive whitespace (space, tab, cr, lf) into a single space.
 * Appends at most max_chars_to_add clean characters.
 * Returns new cur_len in buf, and updates *out_end_pos to the raw index reached in src.
 */
static int append_clean_chars_w(const WCHAR *src, int start_pos, int max_chars_to_add,
	TCHAR *buf, int cur_len, int max_len, int *out_end_pos)
{
	int p;
	int clean_count = 0;

	if (start_pos < 0) start_pos = 0;
	p = start_pos;

	// Skip leading whitespace if buf is empty or ends with space
	if (cur_len == 0 || buf[cur_len - 1] == TEXT(' ')) {
		while (src[p] != L'\0' && (src[p] == L' ' || src[p] == L'\t' || src[p] == L'\r' || src[p] == L'\n')) {
			p++;
		}
	}

	while (src[p] != L'\0' && clean_count < max_chars_to_add && cur_len < max_len - 1) {
		WCHAR c = src[p];
		if (c == L'\r' || c == L'\n' || c == L'\t' || c == L' ') {
			if (cur_len > 0 && buf[cur_len - 1] != TEXT(' ')) {
				buf[cur_len++] = TEXT(' ');
				clean_count++;
			}
			p++;
		} else {
#ifdef UNICODE
			buf[cur_len++] = c;
#else
			buf[cur_len++] = (TCHAR)c;
#endif
			clean_count++;
			p++;
		}
	}

	if (out_end_pos != NULL) {
		*out_end_pos = p;
	}
	return cur_len;
}

/*
 * find_backward_start_w - scan backwards from match_start to find a raw index
 * that provides approximately clean_budget clean characters of pre-context.
 * Will not go below min_raw_pos.
 */
static int find_backward_start_w(const WCHAR *src, int match_start, int clean_budget, int min_raw_pos)
{
	int p = match_start;
	int clean_count = 0;
	BOOL in_space = FALSE;

	if (min_raw_pos < 0) min_raw_pos = 0;
	while (p > min_raw_pos && clean_count < clean_budget) {
		WCHAR c = src[--p];
		if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') {
			if (!in_space) {
				clean_count++;
				in_space = TRUE;
			}
		} else {
			clean_count++;
			in_space = FALSE;
		}
	}
	return p;
}

/*
 * append_clean_chars_a - append clean characters from ANSI src starting at start_pos
 */
static int append_clean_chars_a(const char *src, int start_pos, int max_chars_to_add,
	TCHAR *buf, int cur_len, int max_len, int *out_end_pos)
{
	int p;
	int clean_count = 0;

	if (start_pos < 0) start_pos = 0;
	p = start_pos;

	// Skip leading whitespace if buf is empty or ends with space
	if (cur_len == 0 || buf[cur_len - 1] == TEXT(' ')) {
		while (src[p] != '\0' && (src[p] == ' ' || src[p] == '\t' || src[p] == '\r' || src[p] == '\n')) {
			p++;
		}
	}

	while (src[p] != '\0' && clean_count < max_chars_to_add && cur_len < max_len - 1) {
		char c = src[p];
		if (c == '\r' || c == '\n' || c == '\t' || c == ' ') {
			if (cur_len > 0 && buf[cur_len - 1] != TEXT(' ')) {
				buf[cur_len++] = TEXT(' ');
				clean_count++;
			}
			p++;
		} else {
#ifdef UNICODE
			int char_bytes = IsDBCSLeadByte((BYTE)c) ? 2 : 1;
			WCHAR wc = L'?';
			MultiByteToWideChar(CP_ACP, 0, src + p, char_bytes, &wc, 1);
			buf[cur_len++] = wc;
			p += char_bytes;
#else
			buf[cur_len++] = (TCHAR)c;
			p++;
#endif
			clean_count++;
		}
	}

	if (out_end_pos != NULL) {
		*out_end_pos = p;
	}
	return cur_len;
}

/*
 * find_backward_start_a - scan backwards from match_start in ANSI src
 */
static int find_backward_start_a(const char *src, int match_start, int clean_budget, int min_raw_pos)
{
	int p = match_start;
	int clean_count = 0;
	BOOL in_space = FALSE;

	if (min_raw_pos < 0) min_raw_pos = 0;
	while (p > min_raw_pos && clean_count < clean_budget) {
		char c = src[--p];
		if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
			if (!in_space) {
				clean_count++;
				in_space = TRUE;
			}
		} else {
			clean_count++;
			in_space = FALSE;
		}
	}
	return p;
}

/*
 * append_ellipsis - append " … " separator to buf
 */
static int append_ellipsis(TCHAR *buf, int cur_len, int max_len)
{
	if (cur_len > 0 && buf[cur_len - 1] != TEXT(' ') && cur_len < max_len - 1) {
		buf[cur_len++] = TEXT(' ');
	}
	if (cur_len < max_len - 2) {
		buf[cur_len++] = (TCHAR)0x2026;
		buf[cur_len++] = TEXT(' ');
	}
	return cur_len;
}

/*
 * search_extract_preview - search full text data and extract condensed preview with context
 */
static BOOL search_extract_preview(DATA_INFO *text_di, const TCHAR *filter, TCHAR *buf, int max_len, int target_chars)
{
	BYTE *mem;
	DWORD data_size;
	BOOL is_unicode = FALSE;
	int match_starts[SEARCH_MAX_MATCHES];
	int match_ends[SEARCH_MAX_MATCHES];
	int match_count = 0;
	int filter_len = (filter != NULL) ? lstrlen(filter) : 0;
	int cur_len = 0;
	DWORD text_len = 0;

	if (text_di == NULL || text_di->data == NULL || buf == NULL || max_len <= 0) {
		return FALSE;
	}
	mem = (BYTE *)GlobalLock(text_di->data);
	if (mem == NULL) {
		return FALSE;
	}

	if (text_di->format == CF_UNICODETEXT ||
		(text_di->format_name != NULL && lstrcmpi(text_di->format_name, TEXT("UNICODE TEXT")) == 0)) {
		is_unicode = TRUE;
	}
	data_size = (text_di->size > 0) ? text_di->size : (DWORD)GlobalSize(text_di->data);

	if (is_unicode) {
		const WCHAR *wp = (const WCHAR *)mem;
		DWORD max_wchars = data_size / sizeof(WCHAR);
		while (text_len < max_wchars && wp[text_len] != L'\0') {
			text_len++;
		}
		if (text_len == 0) {
			GlobalUnlock(text_di->data);
			return FALSE;
		}

		// Find matches across entire text
		if (filter_len > 0) {
			const WCHAR *cur = wp;
			const WCHAR *m;
			while (match_count < SEARCH_MAX_MATCHES && (m = StrStrIW(cur, filter)) != NULL) {
				match_starts[match_count] = (int)(m - wp);
				match_ends[match_count]   = match_starts[match_count] + filter_len;
				match_count++;
				cur = m + filter_len;
			}
			if (match_count == 0) {
				GlobalUnlock(text_di->data);
				return FALSE;
			}
		}

		if (match_count == 0) {
			int last_p = 0;
			cur_len = append_clean_chars_w(wp, 0, target_chars, buf, 0, max_len, &last_p);
			if (wp[last_p] != L'\0') {
				cur_len = append_ellipsis(buf, cur_len, max_len);
			}
		} else {
			struct { int start; int end; } clusters[SEARCH_MAX_CLUSTERS];
			int cluster_count = 0;
			int i;
			BOOL include_separate_start = TRUE;
			int num_windows;
			int ellipsis_reserve;
			int avail_budget;
			int budget_per_win;
			int last_end_raw = 0;

			// Form clusters of matches that are within 60 raw chars of each other
			for (i = 0; i < match_count; i++) {
				if (cluster_count == 0) {
					clusters[0].start = match_starts[0];
					clusters[0].end   = match_ends[0];
					cluster_count = 1;
				} else {
					if (match_starts[i] <= clusters[cluster_count - 1].end + 60) {
						clusters[cluster_count - 1].end = match_ends[i];
					} else if (cluster_count < SEARCH_MAX_CLUSTERS) {
						clusters[cluster_count].start = match_starts[i];
						clusters[cluster_count].end   = match_ends[i];
						cluster_count++;
					}
				}
			}

			// If first cluster is close to start of text, merge it with start window
			if (cluster_count > 0 && clusters[0].start <= 60) {
				clusters[0].start = 0;
				include_separate_start = FALSE;
			}

			num_windows = cluster_count + (include_separate_start ? 1 : 0);
			ellipsis_reserve = (num_windows + 1) * 3;
			avail_budget = target_chars - ellipsis_reserve;
			if (avail_budget < 40 * num_windows) {
				avail_budget = 40 * num_windows;
			}
			budget_per_win = avail_budget / num_windows;

			// Window 0: Start of text (if not already part of cluster 0)
			if (include_separate_start) {
				cur_len = append_clean_chars_w(wp, 0, budget_per_win, buf, cur_len, max_len, &last_end_raw);
			}

			// Process each match cluster
			for (i = 0; i < cluster_count; i++) {
				int start_p;
				if (clusters[i].start <= last_end_raw + 20) {
					start_p = last_end_raw;
				} else {
					int pre_context = budget_per_win / 3;
					cur_len = append_ellipsis(buf, cur_len, max_len);
					start_p = find_backward_start_w(wp, clusters[i].start, pre_context, last_end_raw);
				}
				cur_len = append_clean_chars_w(wp, start_p, budget_per_win, buf, cur_len, max_len, &last_end_raw);
			}

			// If we still have budget left to fill target_chars, and more text exists, keep reading!
			if (cur_len < target_chars && wp[last_end_raw] != L'\0') {
				int rem_needed = target_chars - cur_len;
				cur_len = append_clean_chars_w(wp, last_end_raw, rem_needed, buf, cur_len, max_len, &last_end_raw);
			}

			// If text still continues beyond what we extracted, append trailing ellipsis
			if (wp[last_end_raw] != L'\0') {
				cur_len = append_ellipsis(buf, cur_len, max_len);
			}
		}

	} else {
		// ANSI CF_TEXT / CF_OEMTEXT
		const char *cp = (const char *)mem;
		while (text_len < data_size && cp[text_len] != '\0') {
			text_len++;
		}
		if (text_len == 0) {
			GlobalUnlock(text_di->data);
			return FALSE;
		}

		if (filter_len > 0) {
			char ansi_filter[BUF_SIZE];
			int aflen;
			WideCharToMultiByte(CP_ACP, 0, filter, -1, ansi_filter, BUF_SIZE, NULL, NULL);
			aflen = (int)strlen(ansi_filter);
			if (aflen > 0) {
				const char *cur = cp;
				const char *m;
				while (match_count < SEARCH_MAX_MATCHES && (m = StrStrIA(cur, ansi_filter)) != NULL) {
					match_starts[match_count] = (int)(m - cp);
					match_ends[match_count]   = match_starts[match_count] + aflen;
					match_count++;
					cur = m + aflen;
				}
			}
			if (match_count == 0) {
				GlobalUnlock(text_di->data);
				return FALSE;
			}
		}

		if (match_count == 0) {
			int last_p = 0;
			cur_len = append_clean_chars_a(cp, 0, target_chars, buf, 0, max_len, &last_p);
			if (cp[last_p] != '\0') {
				cur_len = append_ellipsis(buf, cur_len, max_len);
			}
		} else {
			struct { int start; int end; } clusters[SEARCH_MAX_CLUSTERS];
			int cluster_count = 0;
			int i;
			BOOL include_separate_start = TRUE;
			int num_windows;
			int ellipsis_reserve;
			int avail_budget;
			int budget_per_win;
			int last_end_raw = 0;

			// Form clusters of matches that are within 60 raw chars of each other
			for (i = 0; i < match_count; i++) {
				if (cluster_count == 0) {
					clusters[0].start = match_starts[0];
					clusters[0].end   = match_ends[0];
					cluster_count = 1;
				} else {
					if (match_starts[i] <= clusters[cluster_count - 1].end + 60) {
						clusters[cluster_count - 1].end = match_ends[i];
					} else if (cluster_count < SEARCH_MAX_CLUSTERS) {
						clusters[cluster_count].start = match_starts[i];
						clusters[cluster_count].end   = match_ends[i];
						cluster_count++;
					}
				}
			}

			// If first cluster is close to start of text, merge with start window
			if (cluster_count > 0 && clusters[0].start <= 60) {
				clusters[0].start = 0;
				include_separate_start = FALSE;
			}

			num_windows = cluster_count + (include_separate_start ? 1 : 0);
			ellipsis_reserve = (num_windows + 1) * 3;
			avail_budget = target_chars - ellipsis_reserve;
			if (avail_budget < 40 * num_windows) {
				avail_budget = 40 * num_windows;
			}
			budget_per_win = avail_budget / num_windows;

			// Window 0: Start of text (if not already part of cluster 0)
			if (include_separate_start) {
				cur_len = append_clean_chars_a(cp, 0, budget_per_win, buf, cur_len, max_len, &last_end_raw);
			}

			// Process each match cluster
			for (i = 0; i < cluster_count; i++) {
				int start_p;
				if (clusters[i].start <= last_end_raw + 20) {
					start_p = last_end_raw;
				} else {
					int pre_context = budget_per_win / 3;
					cur_len = append_ellipsis(buf, cur_len, max_len);
					start_p = find_backward_start_a(cp, clusters[i].start, pre_context, last_end_raw);
				}
				cur_len = append_clean_chars_a(cp, start_p, budget_per_win, buf, cur_len, max_len, &last_end_raw);
			}

			// If we still have budget left to fill target_chars, and more text exists, keep reading!
			if (cur_len < target_chars && cp[last_end_raw] != '\0') {
				int rem_needed = target_chars - cur_len;
				cur_len = append_clean_chars_a(cp, last_end_raw, rem_needed, buf, cur_len, max_len, &last_end_raw);
			}

			// If text still continues beyond what we extracted, append trailing ellipsis
			if (cp[last_end_raw] != '\0') {
				cur_len = append_ellipsis(buf, cur_len, max_len);
			}
		}
	}

	while (cur_len > 0 && buf[cur_len - 1] == TEXT(' ')) {
		cur_len--;
	}
	buf[cur_len] = TEXT('\0');
	GlobalUnlock(text_di->data);
	return (cur_len > 0);
}

/*
 * search_add_item - add an item to the search list and listbox
 */
static void search_add_item(DATA_INFO *di, const int db_id, const TCHAR *title)
{
	DATA_INFO **new_items;
	int *new_db_ids;

	if (title == NULL || *title == TEXT('\0')) {
		return;
	}
	// Grow array if needed
	if (search_item_count >= search_item_capacity) {
		search_item_capacity *= 2;
		new_items = (DATA_INFO **)mem_alloc(sizeof(DATA_INFO *) * search_item_capacity);
		new_db_ids = (int *)mem_alloc(sizeof(int) * search_item_capacity);
		if (new_items == NULL || new_db_ids == NULL) {
			if (new_items != NULL) mem_free((void **)&new_items);
			if (new_db_ids != NULL) mem_free((void **)&new_db_ids);
			return;
		}
		if (search_items != NULL) {
			CopyMemory(new_items, search_items, sizeof(DATA_INFO *) * search_item_count);
			mem_free((void **)&search_items);
		}
		if (search_item_db_ids != NULL) {
			CopyMemory(new_db_ids, search_item_db_ids, sizeof(int) * search_item_count);
			mem_free((void **)&search_item_db_ids);
		}
		search_items = new_items;
		search_item_db_ids = new_db_ids;
	}
	search_items[search_item_count] = di;
	search_item_db_ids[search_item_count] = db_id;
	search_item_count++;
	SendMessage(hListBox, LB_ADDSTRING, 0, (LPARAM)title);
}

/*
 * search_collect - recursively collect matching text items from history
 */
static void search_collect(DATA_INFO *di, const TCHAR *filter, int target_chars)
{
	DATA_INFO *text_di;
	TCHAR title[SEARCH_TEXT_BUF_SIZE];

	for (; di != NULL; di = di->next) {
		if (di->type == TYPE_FOLDER) {
			if (di->child != NULL) {
				search_collect(di->child, filter, target_chars);
			}
			continue;
		}
		if (di->title != NULL && lstrcmp(di->title, TEXT("-")) == 0) {
			continue;
		}

		// Only show text records (skip bitmaps, files, and other non-text data)
		text_di = search_find_text_data(di);
		if (text_di == NULL) {
			continue;
		}

		// If item has an explicit title, check if title matches or if filter is empty
		if (di->title != NULL && *di->title != TEXT('\0') && lstrcmp(di->title, TEXT("-")) != 0) {
			if (*filter == TEXT('\0') || StrStrI(di->title, filter) != NULL) {
				if (search_clean_string(di->title, title, SEARCH_TEXT_BUF_SIZE) == TRUE) {
					search_add_item(di, (int)di->param1, title);
					continue;
				}
			}
		}

		// Search and extract preview from text data (checks entire text, even 1.5MB+)
		if (search_extract_preview(text_di, filter, title, SEARCH_TEXT_BUF_SIZE, target_chars) == TRUE) {
			search_add_item(di, (int)di->param1, title);
		}
	}
}

/*
 * search_populate - populate listbox with filtered items
 */
static void search_populate(const TCHAR *filter)
{
	TCHAR clean_filter[BUF_SIZE];
	int target_chars = 250;

	if (hListBox != NULL) {
		RECT lrc;
		GetClientRect(hListBox, &lrc);
		int w = lrc.right - lrc.left;
		if (w > 0) {
			target_chars = (w * 10) / Scale(40);
		}
	}
	if (target_chars < 150) target_chars = 150;
	if (target_chars > 1200) target_chars = 1200;

	search_clean_string(filter, clean_filter, BUF_SIZE);

	SendMessage(hListBox, WM_SETREDRAW, FALSE, 0);
	SendMessage(hListBox, LB_RESETCONTENT, 0, 0);
	search_item_count = 0;

	if (search_db_results != NULL) {
		db_history_free_search_results(search_db_results, search_db_count);
		search_db_results = NULL;
		search_db_count = 0;
	}

	if (db_history_is_open()) {
		search_db_count = db_history_search(clean_filter, 200, &search_db_results);
		if (search_db_results != NULL && search_db_count > 0) {
			int i;
			for (i = 0; i < search_db_count; i++) {
				DATA_INFO *matched_di = NULL;
				DATA_INFO *cur;
				TCHAR title[SEARCH_TEXT_BUF_SIZE];
				const TCHAR *src_text = NULL;

				// Check if item is already loaded in memory
				for (cur = search_history_root; cur != NULL; cur = cur->next) {
					if ((int)cur->param1 == search_db_results[i].id) {
						matched_di = cur;
						break;
					}
				}

				// Pick best text to display
				if (search_db_results[i].snippet != NULL && *search_db_results[i].snippet != TEXT('\0')) {
					src_text = search_db_results[i].snippet;
				} else if (search_db_results[i].title != NULL && *search_db_results[i].title != TEXT('\0')) {
					src_text = search_db_results[i].title;
				}

				if (src_text != NULL) {
					if (search_clean_string(src_text, title, SEARCH_TEXT_BUF_SIZE) == TRUE) {
						search_add_item(matched_di, search_db_results[i].id, title);
					}
				}
			}
		}
	} else {
		search_collect(search_history_root, clean_filter, target_chars);
	}

	if (search_item_count > 0) {
		SendMessage(hListBox, LB_SETCURSEL, 0, 0);
	}
	SendMessage(hListBox, WM_SETREDRAW, TRUE, 0);
	InvalidateRect(hListBox, NULL, TRUE);
	UpdateWindow(hListBox);
}

/*
 * search_close - close the popup, keeping the result already decided
 */
static void search_close(void)
{
	if (search_closing == TRUE) {
		return;
	}
	search_closing = TRUE;
	DestroyWindow(hSearchWnd);
}

/*
 * search_select_item - select the current listbox item and close
 */
static void search_select_item(void)
{
	int sel;

	sel = (int)SendMessage(hListBox, LB_GETCURSEL, 0, 0);
	if (sel >= 0 && sel < search_item_count) {
		if (search_items[sel] != NULL) {
			search_result = search_items[sel];
		} else if (db_history_is_open() && search_item_db_ids != NULL && search_item_db_ids[sel] > 0) {
			search_result = db_history_get_item(search_item_db_ids[sel]);
		} else {
			search_result = NULL;
		}
	} else {
		search_result = NULL;
	}
	search_close();
}

/*
 * search_delete_selected_item - delete selected search item from DB, RAM, and listbox
 */
static void search_delete_selected_item(void)
{
	int sel;
	int db_id;
	int i, new_sel;
	DATA_INFO *del_di;
	HWND parent;

	if (hListBox == NULL) return;
	sel = (int)SendMessage(hListBox, LB_GETCURSEL, 0, 0);
	if (sel < 0 || sel >= search_item_count) return;

	del_di = (search_items != NULL) ? search_items[sel] : NULL;
	db_id = (search_item_db_ids != NULL) ? search_item_db_ids[sel] : 0;

	if (db_id <= 0 && del_di != NULL && del_di->param1 > 0) {
		db_id = (int)del_di->param1;
	}

	// 1. Delete from SQLite database
	if (db_history_is_open() && db_id > 0) {
		db_history_delete_item(db_id);
	}

	// 2. Delete from in-memory history list
	if (del_di != NULL) {
		data_delete(&history_data.child, del_di, TRUE);
	} else if (db_id > 0) {
		DATA_INFO *cur;
		for (cur = history_data.child; cur != NULL; cur = cur->next) {
			if ((int)cur->param1 == db_id) {
				data_delete(&history_data.child, cur, TRUE);
				break;
			}
		}
	}

	// 3. Delete from listbox
	SendMessage(hListBox, LB_DELETESTRING, (WPARAM)sel, 0);

	// 4. Shift arrays left
	for (i = sel; i < search_item_count - 1; i++) {
		search_items[i] = search_items[i + 1];
		search_item_db_ids[i] = search_item_db_ids[i + 1];
	}
	search_item_count--;

	// 5. Update selection to previous item if exists, otherwise top item or none
	new_sel = (sel > 0) ? (sel - 1) : 0;
	if (search_item_count > 0) {
		if (new_sel >= search_item_count) {
			new_sel = search_item_count - 1;
		}
		SendMessage(hListBox, LB_SETCURSEL, (WPARAM)new_sel, 0);
	}

	// 6. Notify parent of history change
	parent = GetParent(hSearchWnd);
	if (parent != NULL) {
		SendMessage(parent, WM_HISTORY_CHANGED, 0, 0);
	}

	InvalidateRect(hListBox, NULL, TRUE);
}

/*
 * listbox_subproc - subclassed listbox for vi-style keys
 */
static LRESULT CALLBACK listbox_subproc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	int sel, count;

	switch (msg) {
	case WM_ERASEBKGND:
		{
			HDC hdc = (HDC)wParam;
			RECT rc;
			HBRUSH hbr;
			GetClientRect(hWnd, &rc);
			hbr = (dark_mode_is_dark() == TRUE) ?
				dark_mode_get_brush(COLOR_WINDOW) : GetSysColorBrush(COLOR_WINDOW);
			FillRect(hdc, &rc, hbr);
			return TRUE;
		}

	case WM_KEYDOWN:
		switch (wParam) {
		case VK_DELETE:
			search_delete_selected_item();
			return 0;

		case VK_RETURN:
			search_select_item();
			return 0;

		case VK_ESCAPE:
			search_result = NULL;
			search_close();
			return 0;

		case VK_TAB:
			SetFocus(hEditCtrl);
			return 0;

		case VK_OEM_2:
			// '/' key - focus search edit
			SetFocus(hEditCtrl);
			return 0;
		}
		break;

	case WM_CHAR:
		switch (wParam) {
		case TEXT('x'):
		case TEXT('d'):
			search_delete_selected_item();
			return 0;

		case TEXT('j'):
			// Move down
			count = (int)SendMessage(hWnd, LB_GETCOUNT, 0, 0);
			sel = (int)SendMessage(hWnd, LB_GETCURSEL, 0, 0);
			if (sel < count - 1) {
				SendMessage(hWnd, LB_SETCURSEL, sel + 1, 0);
			}
			return 0;

		case TEXT('k'):
			// Move up
			sel = (int)SendMessage(hWnd, LB_GETCURSEL, 0, 0);
			if (sel > 0) {
				SendMessage(hWnd, LB_SETCURSEL, sel - 1, 0);
			}
			return 0;

		case TEXT('g'):
			// Go to top
			SendMessage(hWnd, LB_SETCURSEL, 0, 0);
			return 0;

		case TEXT('G'):
			// Go to bottom
			count = (int)SendMessage(hWnd, LB_GETCOUNT, 0, 0);
			if (count > 0) {
				SendMessage(hWnd, LB_SETCURSEL, count - 1, 0);
			}
			return 0;

		case TEXT('/'):
			return 0;
		}
		break;

	case WM_LBUTTONDBLCLK:
		search_select_item();
		return 0;
	}
	return CallWindowProc(origListBoxProc, hWnd, msg, wParam, lParam);
}

/*
 * edit_subproc - subclassed edit control for navigation keys
 */
static LRESULT CALLBACK edit_subproc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	int sel, count;

	switch (msg) {
	case WM_KEYDOWN:
		switch (wParam) {
		case VK_DELETE:
			if (GetKeyState(VK_SHIFT) < 0) {
				search_delete_selected_item();
				return 0;
			}
			break;

		case VK_RETURN:
			search_select_item();
			return 0;

		case VK_ESCAPE:
			search_result = NULL;
			search_close();
			return 0;

		case VK_TAB:
			SetFocus(hListBox);
			return 0;

		case VK_DOWN:
			count = (int)SendMessage(hListBox, LB_GETCOUNT, 0, 0);
			sel = (int)SendMessage(hListBox, LB_GETCURSEL, 0, 0);
			if (sel < count - 1) {
				SendMessage(hListBox, LB_SETCURSEL, sel + 1, 0);
			} else if (sel == -1 && count > 0) {
				SendMessage(hListBox, LB_SETCURSEL, 0, 0);
			}
			return 0;

		case VK_UP:
			sel = (int)SendMessage(hListBox, LB_GETCURSEL, 0, 0);
			if (sel > 0) {
				SendMessage(hListBox, LB_SETCURSEL, sel - 1, 0);
			}
			return 0;

		case VK_NEXT:
			// Page down in listbox
			SendMessage(hListBox, WM_KEYDOWN, VK_NEXT, 0);
			return 0;

		case VK_PRIOR:
			// Page up in listbox
			SendMessage(hListBox, WM_KEYDOWN, VK_PRIOR, 0);
			return 0;
		}
		break;
	}
	return CallWindowProc(origEditProc, hWnd, msg, wParam, lParam);
}

/*
 * search_draw_item - owner-draw listbox item with yellow search keyword highlight
 */
static void search_draw_item(const DRAWITEMSTRUCT *dis)
{
	TCHAR text[SEARCH_TEXT_BUF_SIZE];
	TCHAR filter[BUF_SIZE];
	TCHAR clean_filter[BUF_SIZE];
	int text_len;
	int filter_len;
	HDC hdc = dis->hDC;
	RECT rc = dis->rcItem;
	int item_w = rc.right - rc.left;
	int item_h = rc.bottom - rc.top;
	HDC mdc;
	HBITMAP hBmp, hOldBmp;
	RECT mrc;
	BOOL selected = (dis->itemState & ODS_SELECTED) ? TRUE : FALSE;
	COLORREF bg_color, text_color;
	HBRUSH hBgBrush;
	HBRUSH hYellowBrush;
	HFONT hOldFont = NULL;
	TEXTMETRIC tm;
	int font_h;
	int x, y;


	if ((int)dis->itemID < 0 || item_w <= 0 || item_h <= 0) {
		return;
	}

	// Retrieve item text
	text_len = (int)SendMessage(dis->hwndItem, LB_GETTEXT, dis->itemID, (LPARAM)text);
	if (text_len <= 0) {
		*text = TEXT('\0');
		text_len = 0;
	}

	// Retrieve current search edit box text
	GetWindowText(hEditCtrl, filter, BUF_SIZE);
	search_clean_string(filter, clean_filter, BUF_SIZE);
	filter_len = lstrlen(clean_filter);

	// Create memory DC for flicker-free rendering
	mdc = CreateCompatibleDC(hdc);
	if (mdc == NULL) {
		return;
	}
	hBmp = CreateCompatibleBitmap(hdc, item_w, item_h);
	if (hBmp == NULL) {
		DeleteDC(mdc);
		return;
	}
	hOldBmp = (HBITMAP)SelectObject(mdc, hBmp);

	SetRect(&mrc, 0, 0, item_w, item_h);

	// Select font in memory DC
	if (hSearchFont != NULL) {
		hOldFont = (HFONT)SelectObject(mdc, hSearchFont);
	}
	GetTextMetrics(mdc, &tm);
	font_h = tm.tmHeight;

	// Determine base colors
	if (selected) {
		if (dark_mode_is_dark() == TRUE) {
			bg_color = dark_mode_get_color(COLOR_HIGHLIGHT);
			text_color = dark_mode_get_color(COLOR_HIGHLIGHTTEXT);
		} else {
			bg_color = GetSysColor(COLOR_HIGHLIGHT);
			text_color = GetSysColor(COLOR_HIGHLIGHTTEXT);
		}
	} else {
		if (dark_mode_is_dark() == TRUE) {
			bg_color = dark_mode_get_color(COLOR_WINDOW);
			text_color = dark_mode_get_color(COLOR_WINDOWTEXT);
		} else {
			bg_color = GetSysColor(COLOR_WINDOW);
			text_color = GetSysColor(COLOR_WINDOWTEXT);
		}
	}

	// 1. Fill entire item background with base color
	hBgBrush = CreateSolidBrush(bg_color);
	FillRect(mdc, &mrc, hBgBrush);
	DeleteObject(hBgBrush);

	// Text positioning
	x = Scale(4);
	y = (item_h - font_h) / 2;

	hYellowBrush = CreateSolidBrush(RGB(255, 235, 59)); // Material Yellow 500

	if (text_len > 0) {
		COLORREF ellipsis_color = RGB(255, 128, 0); // Orange ellipsis
		int pos = 0;
		SIZE szEllipsis;
		int ell_w = Scale(12);

		if (GetTextExtentPoint32(mdc, TEXT(" …"), 2, &szEllipsis) && szEllipsis.cx > 0) {
			ell_w = szEllipsis.cx;
		}

		while (pos < text_len && x < item_w) {
			// 1. Check for keyword match (yellow background with black text)
			if (filter_len > 0 && StrCmpNI(text + pos, clean_filter, filter_len) == 0) {
				SIZE sz;
				RECT rcHighlight;
				GetTextExtentPoint32(mdc, text + pos, filter_len, &sz);

				rcHighlight.left   = x;
				rcHighlight.top    = Scale(1);
				rcHighlight.right  = x + sz.cx;
				rcHighlight.bottom = item_h - Scale(1);

				FillRect(mdc, &rcHighlight, hYellowBrush);

				SetBkMode(mdc, TRANSPARENT);
				SetTextColor(mdc, RGB(0, 0, 0));
				TextOut(mdc, x, y, text + pos, filter_len);
				x += sz.cx;
				pos += filter_len;
				continue;
			}

			// 2. Check for break symbol … (U+2026) or ⋯ (U+22EF)
			if (text[pos] == (TCHAR)0x2026 || text[pos] == (TCHAR)0x22EF) {
				SIZE sz;
				GetTextExtentPoint32(mdc, text + pos, 1, &sz);
				SetBkMode(mdc, TRANSPARENT);
				SetTextColor(mdc, ellipsis_color);
				TextOut(mdc, x, y, text + pos, 1);
				x += sz.cx;
				pos += 1;
				continue;
			}

			// 3. Normal text run up to next match or next break symbol
			{
				int run_end = pos + 1;
				while (run_end < text_len) {
					if (text[run_end] == (TCHAR)0x2026 || text[run_end] == (TCHAR)0x22EF) {
						break;
					}
					if (filter_len > 0 && StrCmpNI(text + run_end, clean_filter, filter_len) == 0) {
						break;
					}
					run_end++;
				}
				int run_len = run_end - pos;
				SIZE sz;
				GetTextExtentPoint32(mdc, text + pos, run_len, &sz);
				SetBkMode(mdc, TRANSPARENT);
				SetTextColor(mdc, text_color);
				TextOut(mdc, x, y, text + pos, run_len);
				x += sz.cx;
				pos = run_end;
			}
		}

		// If text was cut off before end of string, draw orange ellipsis at right edge
		if (pos < text_len) {
			RECT rcEll;
			int ell_x = item_w - ell_w - Scale(2);
			if (ell_x < 0) ell_x = 0;
			rcEll.left   = ell_x;
			rcEll.top    = 0;
			rcEll.right  = item_w;
			rcEll.bottom = item_h;
			hBgBrush = CreateSolidBrush(bg_color);
			FillRect(mdc, &rcEll, hBgBrush);
			DeleteObject(hBgBrush);

			SetBkMode(mdc, TRANSPARENT);
			SetTextColor(mdc, ellipsis_color);
			TextOut(mdc, ell_x, y, TEXT(" …"), 2);
		}
	}

	DeleteObject(hYellowBrush);

	// Draw focus rect if needed
	if ((dis->itemState & ODS_FOCUS) && !(dis->itemState & ODS_NOFOCUSRECT)) {
		DrawFocusRect(mdc, &mrc);
	}

	// Blit to screen
	BitBlt(hdc, rc.left, rc.top, item_w, item_h, mdc, 0, 0, SRCCOPY);

	if (hOldFont != NULL) {
		SelectObject(mdc, hOldFont);
	}
	SelectObject(mdc, hOldBmp);
	DeleteObject(hBmp);
	DeleteDC(mdc);
}

/*
 * search_proc - search popup window procedure
 */
static LRESULT CALLBACK search_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	TCHAR buf[BUF_SIZE];
	RECT rc;
	int margin = Scale(BASE_SEARCH_MARGIN);
	int edit_h = Scale(BASE_SEARCH_EDIT_HEIGHT);

	switch (msg) {
	case WM_CREATE:
		GetClientRect(hWnd, &rc);

		// Set font first so WM_MEASUREITEM can calculate correct item height
		hSearchFont = search_create_font();

		// Create edit control (search box)
		hEditCtrl = CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("EDIT"), TEXT(""),
			WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
			margin, margin,
			rc.right - margin * 2, edit_h,
			hWnd, (HMENU)IDC_SEARCH_EDIT, hInst, NULL);

		// Create owner-drawn listbox (results with keyword highlighting)
		hListBox = CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("LISTBOX"), TEXT(""),
			WS_CHILD | WS_VISIBLE | WS_VSCROLL |
			LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS,
			margin, margin + edit_h + margin,
			rc.right - margin * 2, (rc.bottom - margin) - (margin + edit_h + margin),
			hWnd, (HMENU)IDC_SEARCH_LIST, hInst, NULL);

		if (hSearchFont != NULL) {
			SendMessage(hEditCtrl, WM_SETFONT, (WPARAM)hSearchFont, TRUE);
			SendMessage(hListBox, WM_SETFONT, (WPARAM)hSearchFont, TRUE);
		}

		dark_mode_set_window(hWnd);
		dark_mode_set_control(hEditCtrl);
		dark_mode_set_control(hListBox);

		// Subclass controls
		origListBoxProc = (WNDPROC)SetWindowLongPtr(hListBox, GWLP_WNDPROC, (LONG_PTR)listbox_subproc);
		origEditProc = (WNDPROC)SetWindowLongPtr(hEditCtrl, GWLP_WNDPROC, (LONG_PTR)edit_subproc);

		// Allocate search items array
		search_item_capacity = SEARCH_INIT_CAPACITY;
		search_items = (DATA_INFO **)mem_alloc(sizeof(DATA_INFO *) * search_item_capacity);
		search_item_db_ids = (int *)mem_alloc(sizeof(int) * search_item_capacity);
		search_item_count = 0;

		// Populate with all items
		search_populate(TEXT(""));

		// Focus on edit
		SetFocus(hEditCtrl);
		break;

	case WM_MEASUREITEM:
		{
			MEASUREITEMSTRUCT *mis = (MEASUREITEMSTRUCT *)lParam;
			if (mis->CtlType == ODT_LISTBOX && mis->CtlID == IDC_SEARCH_LIST) {
				HDC hdc = GetDC(hWnd);
				HFONT hRetFont = NULL;
				TEXTMETRIC tm;
				if (hSearchFont != NULL) {
					hRetFont = (HFONT)SelectObject(hdc, hSearchFont);
				}
				GetTextMetrics(hdc, &tm);
				if (hRetFont != NULL) {
					SelectObject(hdc, hRetFont);
				}
				ReleaseDC(hWnd, hdc);
				mis->itemHeight = tm.tmHeight + Scale(6);
				return TRUE;
			}
		}
		break;

	case WM_DRAWITEM:
		{
			DRAWITEMSTRUCT *dis = (DRAWITEMSTRUCT *)lParam;
			if (dis->CtlType == ODT_LISTBOX && dis->CtlID == IDC_SEARCH_LIST) {
				search_draw_item(dis);
				return TRUE;
			}
		}
		break;

	case WM_ERASEBKGND:
		{
			HDC hdc = (HDC)wParam;
			RECT rc;
			HBRUSH hbr;
			GetClientRect(hWnd, &rc);
			hbr = (dark_mode_is_dark() == TRUE) ?
				dark_mode_get_brush(COLOR_WINDOW) : GetSysColorBrush(COLOR_WINDOW);
			FillRect(hdc, &rc, hbr);
			return TRUE;
		}

	case WM_CTLCOLOREDIT:
	case WM_CTLCOLORLISTBOX:
		{
			HBRUSH hBrush;
			if (dark_mode_ctl_color(msg, wParam, lParam, &hBrush) == TRUE) {
				SetBkMode((HDC)wParam, OPAQUE);
				return (LRESULT)hBrush;
			}
			SetTextColor((HDC)wParam, GetSysColor(COLOR_WINDOWTEXT));
			SetBkColor((HDC)wParam, GetSysColor(COLOR_WINDOW));
			SetBkMode((HDC)wParam, OPAQUE);
			return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
		}

	case WM_CTLCOLORDLG:
	case WM_CTLCOLORBTN:
	case WM_CTLCOLORSTATIC:
		{
			HBRUSH hBrush;
			if (dark_mode_ctl_color(msg, wParam, lParam, &hBrush) == TRUE) {
				return (LRESULT)hBrush;
			}
			return DefWindowProc(hWnd, msg, wParam, lParam);
		}

	case WM_COMMAND:
		if (LOWORD(wParam) == IDC_SEARCH_EDIT && HIWORD(wParam) == EN_CHANGE) {
			// Filter changed - repopulate
			GetWindowText(hEditCtrl, buf, BUF_SIZE);
			search_populate(buf);
		}
		if (LOWORD(wParam) == IDC_SEARCH_LIST && HIWORD(wParam) == LBN_DBLCLK) {
			search_select_item();
		}
		break;

	case WM_SIZE:
		if (wParam != SIZE_MINIMIZED && hEditCtrl != NULL && hListBox != NULL) {
			int w = LOWORD(lParam);
			int h = HIWORD(lParam);
			MoveWindow(hEditCtrl, margin, margin, w - margin * 2, edit_h, TRUE);
			MoveWindow(hListBox, margin, margin + edit_h + margin,
				w - margin * 2, (h - margin) - (margin + edit_h + margin), TRUE);
		}
		break;

	case WM_ACTIVATE:
		if (LOWORD(wParam) == WA_INACTIVE && search_closing == FALSE) {
			HWND hActive = (HWND)lParam;
			if (hActive != hWnd && hActive != hEditCtrl && hActive != hListBox && !IsChild(hWnd, hActive)) {
				search_result = NULL;
				search_close();
			}
		}
		break;

	case WM_DESTROY:
		// Restore subclassed wndprocs
		if (hListBox != NULL && origListBoxProc != NULL) {
			SetWindowLongPtr(hListBox, GWLP_WNDPROC, (LONG_PTR)origListBoxProc);
		}
		if (hEditCtrl != NULL && origEditProc != NULL) {
			SetWindowLongPtr(hEditCtrl, GWLP_WNDPROC, (LONG_PTR)origEditProc);
		}
		// Free resources
		if (search_items != NULL) {
			mem_free((void **)&search_items);
		}
		if (search_item_db_ids != NULL) {
			mem_free((void **)&search_item_db_ids);
		}
		if (search_db_results != NULL) {
			db_history_free_search_results(search_db_results, search_db_count);
			search_db_results = NULL;
			search_db_count = 0;
		}
		search_item_count = 0;
		search_item_capacity = 0;
		if (hSearchFont != NULL) {
			DeleteObject(hSearchFont);
			hSearchFont = NULL;
		}
		hEditCtrl = NULL;
		hListBox = NULL;
		origListBoxProc = NULL;
		origEditProc = NULL;
		hSearchWnd = NULL;
		break;

	default:
		return DefWindowProc(hWnd, msg, wParam, lParam);
	}
	return 0;
}

/*
 * search_popup_show - show search popup and return selected item
 */
DATA_INFO *search_popup_show(const HWND hWnd, DATA_INFO *history_root)
{
	POINT pos;
	MSG msg;
	int x, y, width, height;
	MONITORINFO mi;
	HMONITOR hMon;

	search_history_root = history_root;
	search_result = NULL;
	search_closing = FALSE;

	// Fill 100% of current monitor work area
	GetCursorPos(&pos);

	mi.cbSize = sizeof(MONITORINFO);
	hMon = MonitorFromPoint(pos, MONITOR_DEFAULTTONEAREST);
	if (hMon != NULL && GetMonitorInfo(hMon, &mi)) {
		x = mi.rcWork.left;
		y = mi.rcWork.top;
		width = mi.rcWork.right - mi.rcWork.left;
		height = mi.rcWork.bottom - mi.rcWork.top;
	} else {
		x = 0;
		y = 0;
		width = GetSystemMetrics(SM_CXSCREEN);
		height = GetSystemMetrics(SM_CYSCREEN);
	}

	// Create search popup
	hSearchWnd = CreateWindowEx(
		WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
		SEARCH_WND_CLASS, TEXT(""),
		WS_POPUP | WS_BORDER | WS_CLIPCHILDREN,
		x, y, width, height,
		hWnd, NULL, hInst, NULL);

	if (hSearchWnd == NULL) {
		return NULL;
	}

	ShowWindow(hSearchWnd, SW_SHOW);
	UpdateWindow(hSearchWnd);

	// Modal message loop
	while (hSearchWnd != NULL && IsWindow(hSearchWnd)) {
		if (GetMessage(&msg, NULL, 0, 0) <= 0) {
			if (msg.message == WM_QUIT) {
				PostQuitMessage((int)msg.wParam);
			}
			break;
		}
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return search_result;
}
/* End of source */
