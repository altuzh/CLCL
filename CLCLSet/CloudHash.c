/*
 * CLCLSet
 *
 * CloudHash.c
 *
 * Content hashing and filename parsing for cloud synchronization.
 */

#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <tchar.h>
#include <stdio.h>
#include "CloudHash.h"

#define FNV64_OFFSET_BASIS  0xcbf29ce484222325ULL
#define FNV64_PRIME         0x100000001b3ULL

/*
 * cloud_calc_text_hash - Calculate normalized 64-bit FNV-1a hash for UTF-8 text
 * Line endings (\r\n vs \n) are normalized by ignoring '\r' so hashes match cross-platform.
 */
BOOL cloud_calc_text_hash(const char *utf8_text, DWORD len, TCHAR *hash_out)
{
	unsigned __int64 hash = FNV64_OFFSET_BASIS;
	DWORD i;

	if (utf8_text == NULL || hash_out == NULL) {
		return FALSE;
	}

	for (i = 0; i < len && utf8_text[i] != '\0'; i++) {
		char c = utf8_text[i];
		if (c == '\r') {
			continue; // Normalize CRLF to LF
		}
		hash ^= (unsigned char)c;
		hash *= FNV64_PRIME;
	}

	_sntprintf(hash_out, CLOUD_HASH_STR_SIZE, TEXT("%016I64x"), hash);
	hash_out[CLOUD_HASH_LEN] = TEXT('\0');
	return TRUE;
}

/*
 * cloud_calc_data_hash - Calculate hash from DATA_INFO data handle and format
 */
BOOL cloud_calc_data_hash(HANDLE hData, DWORD size, const TCHAR *format_name, TCHAR *hash_out, char **utf8_out, DWORD *utf8_len_out)
{
	const void *pMem;
	char *utf8_buf = NULL;
	DWORD utf8_len = 0;
	BOOL is_unicode = FALSE;

	if (hData == NULL || size == 0 || size > CLOUD_MAX_ITEM_SIZE) {
		return FALSE;
	}

	if (format_name == NULL) {
		return FALSE;
	}

	if (lstrcmpi(format_name, TEXT("UNICODE TEXT")) == 0) {
		is_unicode = TRUE;
	} else if (lstrcmpi(format_name, TEXT("TEXT")) != 0 && lstrcmpi(format_name, TEXT("OEM TEXT")) != 0) {
		return FALSE; // Non-text format
	}

	pMem = GlobalLock(hData);
	if (pMem == NULL) {
		return FALSE;
	}

	if (is_unicode) {
		const WCHAR *wstr = (const WCHAR *)pMem;
		int wlen = (int)(size / sizeof(WCHAR));
		int req_len;

		// Null-terminate count check
		int actual_wlen = 0;
		while (actual_wlen < wlen && wstr[actual_wlen] != L'\0') {
			actual_wlen++;
		}

		req_len = WideCharToMultiByte(CP_UTF8, 0, wstr, actual_wlen, NULL, 0, NULL, NULL);
		if (req_len <= 0 || req_len > (int)CLOUD_MAX_ITEM_SIZE) {
			GlobalUnlock(hData);
			return FALSE;
		}

		utf8_buf = (char *)malloc(req_len + 1);
		if (utf8_buf == NULL) {
			GlobalUnlock(hData);
			return FALSE;
		}

		WideCharToMultiByte(CP_UTF8, 0, wstr, actual_wlen, utf8_buf, req_len, NULL, NULL);
		utf8_buf[req_len] = '\0';
		utf8_len = (DWORD)req_len;
	} else {
		// ANSI / OEM text to UTF-8
		const char *astr = (const char *)pMem;
		int alen = 0;
		int wlen;
		WCHAR *wtmp;
		int req_len;

		while ((DWORD)alen < size && astr[alen] != '\0') {
			alen++;
		}

		wlen = MultiByteToWideChar(CP_ACP, 0, astr, alen, NULL, 0);
		if (wlen <= 0) {
			GlobalUnlock(hData);
			return FALSE;
		}
		wtmp = (WCHAR *)malloc((wlen + 1) * sizeof(WCHAR));
		if (wtmp == NULL) {
			GlobalUnlock(hData);
			return FALSE;
		}
		MultiByteToWideChar(CP_ACP, 0, astr, alen, wtmp, wlen);

		req_len = WideCharToMultiByte(CP_UTF8, 0, wtmp, wlen, NULL, 0, NULL, NULL);
		if (req_len <= 0 || req_len > (int)CLOUD_MAX_ITEM_SIZE) {
			free(wtmp);
			GlobalUnlock(hData);
			return FALSE;
		}

		utf8_buf = (char *)malloc(req_len + 1);
		if (utf8_buf == NULL) {
			free(wtmp);
			GlobalUnlock(hData);
			return FALSE;
		}
		WideCharToMultiByte(CP_UTF8, 0, wtmp, wlen, utf8_buf, req_len, NULL, NULL);
		utf8_buf[req_len] = '\0';
		utf8_len = (DWORD)req_len;
		free(wtmp);
	}

	GlobalUnlock(hData);

	if (cloud_calc_text_hash(utf8_buf, utf8_len, hash_out) == FALSE) {
		free(utf8_buf);
		return FALSE;
	}

	if (utf8_out != NULL && utf8_len_out != NULL) {
		*utf8_out = utf8_buf;
		*utf8_len_out = utf8_len;
	} else {
		free(utf8_buf);
	}
	return TRUE;
}

/*
 * cloud_format_filename - Format YYYYMMDD_HHMMSS_<HASH16>.txt
 */
BOOL cloud_format_filename(const SYSTEMTIME *st, const TCHAR *hash_str, TCHAR *filename_out, DWORD max_len)
{
	SYSTEMTIME local_st;

	if (hash_str == NULL || filename_out == NULL || max_len < 32) {
		return FALSE;
	}

	if (st == NULL) {
		GetLocalTime(&local_st);
		st = &local_st;
	}

	_sntprintf(filename_out, max_len, TEXT("%04u%02u%02u_%02u%02u%02u_%s.txt"),
		st->wYear, st->wMonth, st->wDay,
		st->wHour, st->wMinute, st->wSecond,
		hash_str);
	return TRUE;
}

/*
 * cloud_parse_filename - Parse YYYYMMDD_HHMMSS_<HASH16>.txt
 */
BOOL cloud_parse_filename(const TCHAR *filename, SYSTEMTIME *st_out, TCHAR *hash_out)
{
	const TCHAR *name;
	int y, m, d, hh, mm, ss;
	TCHAR hash_buf[64];

	if (filename == NULL) {
		return FALSE;
	}

	// Strip directory if present
	name = _tcsrchr(filename, TEXT('/'));
	if (name != NULL) {
		name++;
	} else {
		name = _tcsrchr(filename, TEXT('\\'));
		if (name != NULL) {
			name++;
		} else {
			name = filename;
		}
	}

	// Must be at least 15 (date) + 1 (_) + 16 (hash) + 4 (.txt) = 36 chars
	if (lstrlen(name) < 36) {
		return FALSE;
	}

	if (_stscanf(name, TEXT("%04d%02d%02d_%02d%02d%02d_%16s"),
		&y, &m, &d, &hh, &mm, &ss, hash_buf) != 7) {
		return FALSE;
	}

	// Strip trailing .txt if attached to hash_buf
	TCHAR *dot = _tcsstr(hash_buf, TEXT(".txt"));
	if (dot != NULL) {
		*dot = TEXT('\0');
	}

	if (lstrlen(hash_buf) != CLOUD_HASH_LEN) {
		return FALSE;
	}

	if (hash_out != NULL) {
		lstrcpy(hash_out, hash_buf);
	}

	if (st_out != NULL) {
		ZeroMemory(st_out, sizeof(SYSTEMTIME));
		st_out->wYear = (WORD)y;
		st_out->wMonth = (WORD)m;
		st_out->wDay = (WORD)d;
		st_out->wHour = (WORD)hh;
		st_out->wMinute = (WORD)mm;
		st_out->wSecond = (WORD)ss;
	}
	return TRUE;
}

/*
 * Hash set implementation for export deduplication
 */
static unsigned char get_bucket_index(const TCHAR *hash_str)
{
	unsigned char b = 0;
	if (hash_str != NULL && hash_str[0] != TEXT('\0') && hash_str[1] != TEXT('\0')) {
		// First 2 hex chars form bucket index (0-255)
		TCHAR hex[3] = { hash_str[0], hash_str[1], TEXT('\0') };
		b = (unsigned char)_tcstoul(hex, NULL, 16);
	}
	return b;
}

HASH_SET *hash_set_create(void)
{
	HASH_SET *set = (HASH_SET *)calloc(1, sizeof(HASH_SET));
	return set;
}

void hash_set_free(HASH_SET *set)
{
	int i;
	HASH_SET_ENTRY *curr, *next;

	if (set == NULL) return;

	for (i = 0; i < 256; i++) {
		curr = set->buckets[i];
		while (curr != NULL) {
			next = curr->next;
			free(curr);
			curr = next;
		}
	}
	free(set);
}

BOOL hash_set_contains(HASH_SET *set, const TCHAR *hash_str)
{
	unsigned char b;
	HASH_SET_ENTRY *curr;

	if (set == NULL || hash_str == NULL) {
		return FALSE;
	}

	b = get_bucket_index(hash_str);
	for (curr = set->buckets[b]; curr != NULL; curr = curr->next) {
		if (lstrcmpi(curr->hash, hash_str) == 0) {
			return TRUE;
		}
	}
	return FALSE;
}

BOOL hash_set_add(HASH_SET *set, const TCHAR *hash_str)
{
	unsigned char b;
	HASH_SET_ENTRY *entry;

	if (set == NULL || hash_str == NULL) {
		return FALSE;
	}

	if (hash_set_contains(set, hash_str)) {
		return FALSE; // Already exists
	}

	b = get_bucket_index(hash_str);
	entry = (HASH_SET_ENTRY *)malloc(sizeof(HASH_SET_ENTRY));
	if (entry == NULL) {
		return FALSE;
	}

	lstrcpyn(entry->hash, hash_str, CLOUD_HASH_STR_SIZE);
	entry->next = set->buckets[b];
	set->buckets[b] = entry;
	set->count++;
	return TRUE;
}
