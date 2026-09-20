/*
 * CLCL
 *
 * File.c
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
#include "String.h"
#include "Data.h"
#include "Message.h"
#include "File.h"
#include "Format.h"
#include "ClipBoard.h"
#include "Filter.h"

/* Define */

/* Global Variables */
extern HINSTANCE hInst;

/* Local Function Prototypes */
static void file_expand_option(DATA_INFO *di, char *option);
static BYTE *file_file_to_item(const BYTE *buf, BYTE *p, const DWORD size, DATA_INFO **root, const int level, TCHAR *err_str);
static BOOL file_item_to_file(const HANDLE hFile, DATA_INFO *di, const BOOL filter_save, TCHAR *err_str);

/*
 * file_name_check - check for characters that cannot be used in a file name
 */
BOOL file_name_check(TCHAR *file_name)
{
	TCHAR *p;

	for (p = file_name; *p != TEXT('\0'); p++) {
#ifndef UNICODE
		if (IsDBCSLeadByte((BYTE)*p) == TRUE) {
			// In case of double-byte character
			p++;
			continue;

		}
#endif
		// Check for invalid file name characters
		if (*p == TEXT('\\') ||
			*p == TEXT('/') ||
			*p == TEXT(':') ||
			*p == TEXT(',') ||
			*p == TEXT(';') ||
			*p == TEXT('*') ||
			*p == TEXT('?') ||
			*p == TEXT('\"') ||
			*p == TEXT('<') ||
			*p == TEXT('>') ||
			*p == TEXT('|')) {
			return FALSE;
		}
	}
	return TRUE;
}

/*
 * file_name_conv - convert characters that cannot be used in a file name
 */
void file_name_conv(TCHAR *file_name, TCHAR conv_char)
{
	TCHAR *p;

	for (p = file_name; *p != TEXT('\0'); p++) {
#ifndef UNICODE
		if (IsDBCSLeadByte((BYTE)*p) == TRUE) {
			// In case of double-byte character
			p++;
			continue;
		}
#endif
		// Convert invalid file name characters to specified character
		if (*p == TEXT('\\') ||
			*p == TEXT('/') ||
			*p == TEXT(':') ||
			*p == TEXT(',') ||
			*p == TEXT(';') ||
			*p == TEXT('*') ||
			*p == TEXT('?') ||
			*p == TEXT('\"') ||
			*p == TEXT('<') ||
			*p == TEXT('>') ||
			*p == TEXT('|')) {
			*p = conv_char;
		}
	}
}

/*
 * file_check_directory - check if directory exists
 */
BOOL file_check_directory(const TCHAR *path)
{
	WIN32_FIND_DATA FindData;
	HANDLE hFindFile;

	if ((hFindFile = FindFirstFile(path, &FindData)) == INVALID_HANDLE_VALUE) {
		return FALSE;
	}
	FindClose(hFindFile);

	if (FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
		// If directory exists
		return TRUE;
	}
	return FALSE;
}

/*
 * file_check_file - check if file exists
 */
BOOL file_check_file(const TCHAR *path)
{
	WIN32_FIND_DATA FindData;
	HANDLE hFindFile;

	if ((hFindFile = FindFirstFile(path, &FindData)) == INVALID_HANDLE_VALUE) {
		return FALSE;
	}
	FindClose(hFindFile);

	if ((FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
		// If file exists
		return TRUE;
	}
	return FALSE;
}

/*
 * file_read_buf - read file
 */
BYTE *file_read_buf(const TCHAR *path, DWORD *ret_size, TCHAR *err_str)
{
	HANDLE hFile;
	DWORD size;
	DWORD ret;
	BYTE *buf;

	// Open file
	hFile = CreateFile(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == NULL || hFile == (HANDLE)-1) {
		message_get_error(GetLastError(), err_str);
		return NULL;
	}
	if ((size = GetFileSize(hFile, NULL)) == 0xFFFFFFFF) {
		message_get_error(GetLastError(), err_str);
		CloseHandle(hFile);
		return NULL;
	}

	if ((buf = (BYTE *)mem_alloc(size + 1)) == NULL) {
		message_get_error(GetLastError(), err_str);
		CloseHandle(hFile);
		return NULL;
	}
	// Read file
	if (ReadFile(hFile, buf, size, &ret, NULL) == FALSE) {
		message_get_error(GetLastError(), err_str);
		mem_free(&buf);
		CloseHandle(hFile);
		return NULL;
	}
	CloseHandle(hFile);

	if (ret != size) {
		message_get_error(ERROR_READ_FAULT, err_str);
		mem_free(&buf);
		return NULL;
	}

	if (ret_size != NULL) {
		*ret_size = size;
	}
	return buf;
}

/*
 * file_write_all - write specified size to file
 */
static BOOL file_write_all(const HANDLE hFile, const void *data, const DWORD size, TCHAR *err_str)
{
	DWORD ret;

	if (WriteFile(hFile, data, size, &ret, NULL) == FALSE) {
		message_get_error(GetLastError(), err_str);
		return FALSE;
	}
	if (ret != size) {
		message_get_error(ERROR_DISK_FULL, err_str);
		return FALSE;
	}
	return TRUE;
}

/*
 * file_write_buf - write to file
 */
BOOL file_write_buf(const TCHAR *path, const BYTE *data, const DWORD size, TCHAR *err_str)
{
	HANDLE hFile;

	// Open file
	hFile = CreateFile(path, GENERIC_READ | GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == NULL || hFile == (HANDLE)-1) {
		message_get_error(GetLastError(), err_str);
		return FALSE;
	}
	// Write file
	if (file_write_all(hFile, data, size, err_str) == FALSE) {
		CloseHandle(hFile);
		return FALSE;
	}
	FlushFileBuffers(hFile);
	CloseHandle(hFile);
	return TRUE;
}

/*
 * file_expand_option - expand option string
 */
static void file_expand_option(DATA_INFO *di, char *option)
{
	char *p = option;

	if (option == NULL || *option == '\0') {
		return;
	}

	di->op_modifiers = a2i(p);
	for (; *p != ',' && *p != '\0'; p++)
		;
	if (*p == '\0') {
		return;
	}
	p++;

	di->op_virtkey = a2i(p);
	for (; *p != ',' && *p != '\0'; p++)
		;
	if (*p == '\0') {
		return;
	}
	p++;

	di->op_paste = a2i(p);
}

/*
 * file_file_to_item - read file and convert to item list
 */
static BYTE *file_file_to_item(const BYTE *buf, BYTE *p, const DWORD size, DATA_INFO **root, const int level, TCHAR *err_str)
{
	DATA_INFO *di = *root;
	DATA_INFO *new_item;
#ifndef OPTION_SET
	DATA_INFO *cdi;
	DATA_INFO *child_item;
	int i;
#endif	// OPTION_SET
	DWORD data_size;

	while (size > (DWORD)(p - buf)) {
		switch (*p) {
		case '\x5':
			if (level == 0) {
				return NULL;
			}
			p++;
			return p;

		case '\x4':
			p++;
			// Create folder
			if ((new_item = data_create_folder(NULL, err_str)) == NULL) {
				return NULL;
			}
			// Title
			if (size > (DWORD)(p - buf) && *p != '\x2') {
				if (*p != '\0') {
					new_item->title = alloc_char_to_tchar(p);
				}
				for (; *p != '\0'; p++)
					;
				p++;
			}
			for (; size > (DWORD)(p - buf) && *p != '\x1' && *p != '\x4' && *p != '\x5'; p++)
				;

			// Add format
			if (*root == NULL) {
				*root = new_item;
			} else {
				di->next = new_item;
			}
			di = new_item;
			
			if ((p = file_file_to_item(buf, p, size, &di->child, level + 1, err_str)) == NULL) {
				return NULL;
			}
			break;

		case '\x1':
			p++;
#ifndef OPTION_SET
			// Create parent item
			new_item = data_create_item(NULL, FALSE, err_str);
			if (new_item == NULL) {
				return NULL;
			}
#endif	// OPTION_SET

			// Title
			if (size > (DWORD)(p - buf) && *p != '\x2') {
#ifndef OPTION_SET
				if (*p != '\0') {
					new_item->title = alloc_char_to_tchar(p);
				}
#endif	// OPTION_SET
				for (; size > (DWORD)(p - buf) && *p != '\0'; p++)
					;
				p++;
			}
			// Modified date and time
			if (size > (DWORD)(p - buf) && *p != '\x2') {
#ifndef OPTION_SET
				if (*p != '\0') {
					new_item->modified.dwHighDateTime = x2i(p);
					for (i = 0; size > (DWORD)(p - buf) && *p != '\0' && i < 8; p++, i++)
						;
				}
				if (*p != '\0') {
					new_item->modified.dwLowDateTime = x2i(p);
				}
#endif	// OPTION_SET
				for (; *p != '\0'; p++)
					;
				p++;
			}
			// Window name
			if (size > (DWORD)(p - buf) && *p != '\x2') {
#ifndef OPTION_SET
				if (*p != '\0') {
					new_item->window_name = alloc_char_to_tchar(p);
				}
#endif	// OPTION_SET
				for (; size > (DWORD)(p - buf) && *p != '\0'; p++)
					;
				p++;
			}
			// String for tool
			if (size > (DWORD)(p - buf) && *p != '\x2') {
#ifndef OPTION_SET
				if (*p != '\0') {
					new_item->plugin_string = alloc_char_to_tchar(p);
				}
#endif	// OPTION_SET
				for (; size > (DWORD)(p - buf) && *p != '\0'; p++)
					;
				p++;
			}
			// Long for tool
			if (size > (DWORD)(p - buf) && *p != '\x2') {
#ifndef OPTION_SET
				new_item->plugin_param = a2i(p);
#endif	// OPTION_SET
				for (; size > (DWORD)(p - buf) && *p != '\0'; p++)
					;
				p++;
			}
			// Options
			if (size > (DWORD)(p - buf) && *p != '\x2') {
#ifndef OPTION_SET
				if (*p != '\0') {
					file_expand_option(new_item, p);
				}
#endif	// OPTION_SET
				for (; size > (DWORD)(p - buf) && *p != '\0'; p++)
					;
				p++;
			}
			// Skip to header start position
			for (; size > (DWORD)(p - buf) && *p != '\x2'; p++)
				;
			if (size <= (DWORD)(p - buf)) {
				break;
			}
			p++;

#ifndef OPTION_SET
			if (*root == NULL) {
				*root = new_item;
			} else {
				di->next = new_item;
			}
			di = new_item;
			cdi = NULL;
#endif	// OPTION_SET

		default:
#ifndef OPTION_SET
			if (di == NULL) {
				return NULL;
			}

			// Create item for each format
			if ((child_item = (DATA_INFO *)mem_calloc(sizeof(DATA_INFO))) == NULL) {
				message_get_error(GetLastError(), err_str);
				return NULL;
			}
			child_item->struct_size = sizeof(DATA_INFO);
			child_item->type = TYPE_DATA;
#endif	// OPTION_SET

			// Read header
			// Size
			data_size = a2i(p);
#ifndef OPTION_SET
			child_item->size = data_size;
#endif	// OPTION_SET
			for (; size > (DWORD)(p - buf) && *p != '\0'; p++)
				;
			p++;
			// Format
			if (size > (DWORD)(p - buf) && *p != '\x3') {
#ifndef OPTION_SET
				child_item->format_name = alloc_char_to_tchar(p);
				child_item->format_name_hash = str2hash(child_item->format_name);
				child_item->format = clipboard_get_format(0, child_item->format_name);
#endif	// OPTION_SET
				for (; size > (DWORD)(p - buf) && *p != '\0'; p++)
					;
				p++;
			}
			// String for tool
			if (size > (DWORD)(p - buf) && *p != '\x3') {
#ifndef OPTION_SET
				child_item->plugin_string = alloc_char_to_tchar(p);
#endif	// OPTION_SET
				for (; size > (DWORD)(p - buf) && *p != '\0'; p++)
					;
				p++;
			}
			// Long for tool
			if (size > (DWORD)(p - buf) && *p != '\x3') {
#ifndef OPTION_SET
				child_item->plugin_param = a2i(p);
#endif	// OPTION_SET
				for (; size > (DWORD)(p - buf) && *p != '\0'; p++)
					;
				p++;
			}
			// Options
			if (size > (DWORD)(p - buf) && *p != '\x3') {
#ifndef OPTION_SET
				if (*p != '\0') {
					file_expand_option(child_item, p);
				}
#endif	// OPTION_SET
				for (; size > (DWORD)(p - buf) && *p != '\0'; p++)
					;
				p++;
			}
			// Skip to data start position
			for (; size > (DWORD)(p - buf) && *p != '\x3'; p++)
				;
			if (size <= (DWORD)(p - buf)) {
				break;
			}
			p++;

			if (data_size > 0) {
#ifndef OPTION_SET
				if ((child_item->data = format_bytes_to_data(child_item->format_name, p, &child_item->size)) == NULL) {
					child_item->data = clipboard_bytes_to_data(child_item->format_name, p, &child_item->size);
				}
#endif	// OPTION_SET
				p += data_size;
			}

#ifndef OPTION_SET
			// Add format
			if (cdi == NULL) {
				di->child = child_item;
			} else {
				cdi->next = child_item;
			}
			cdi = child_item;
#endif	// OPTION_SET
			break;
		}
	}
	return p;
}

/*
 * file_read_data - read file and convert to item list
 */
BOOL file_read_data(const TCHAR *path, DATA_INFO **root, TCHAR *err_str)
{
	HANDLE hFile;
	HANDLE hMap;
	BYTE *buf;
	DWORD size_low, size_high;
	BOOL ret = TRUE;

	// Open file
	hFile = CreateFile(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		DWORD err = GetLastError();
		if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
			return TRUE;
		}
		message_get_error(err, err_str);
		return FALSE;
	}

	size_low = GetFileSize(hFile, &size_high);
	if (size_low == INVALID_FILE_SIZE && GetLastError() != NO_ERROR) {
		message_get_error(GetLastError(), err_str);
		CloseHandle(hFile);
		return FALSE;
	}

	// Succeed immediately if file is empty
	if (size_low == 0 && size_high == 0) {
		CloseHandle(hFile);
		return TRUE;
	}

	if (size_high != 0) {
		message_get_error(ERROR_NOT_ENOUGH_MEMORY, err_str);
		CloseHandle(hFile);
		return FALSE;
	}

	// Create file mapping
	hMap = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
	if (hMap == NULL) {
		message_get_error(GetLastError(), err_str);
		CloseHandle(hFile);
		return FALSE;
	}

	// Map view
	buf = (BYTE *)MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
	if (buf == NULL) {
		message_get_error(GetLastError(), err_str);
		CloseHandle(hMap);
		CloseHandle(hFile);
		return FALSE;
	}

	// Convert to item list
	if (file_file_to_item(buf, buf, size_low, root, 0, err_str) == NULL) {
		ret = FALSE;
	}

	UnmapViewOfFile(buf);
	CloseHandle(hMap);
	CloseHandle(hFile);
	return ret;
}

/*
 * file_item_to_file - write item list to file
 *
 *	\x1 title \0 modified \0 app_name \0 plugin_string \0 plugin_param \0 option \0		#Item
 *		\x2 size \0 format_name \0 plugin_string \0 plugin_param \0  option \0			#Data header
 *			\x3 data																	#Data
 *		\x2 size \0 format_name \0 plugin_string \0 plugin_param \0  option \0
 *			\x3 data
 *
 *	\x4 title \0																		#Folder
 *		\x1 title \0 modified \0 app_name \0 plugin_string \0 plugin_param \0 option \0
 *			\x2 size \0 format_name \0 plugin_string \0 plugin_param \0 option \0
 *				\x3 data
 *			\x2 size \0 format_name \0 plugin_string \0 plugin_param \0 option \0
 *				\x3 data
 *	\x5																					#Folder end mark
 */
static BOOL file_item_to_file(const HANDLE hFile, DATA_INFO *di, const BOOL filter_save, TCHAR *err_str)
{
	DATA_INFO *cdi;
	BYTE *buf, *p;
	TCHAR str_date[BUF_SIZE];
	TCHAR str_size[BUF_SIZE];
	TCHAR str_param[BUF_SIZE];
	TCHAR str_op[BUF_SIZE];
	TCHAR *tp;
	DWORD len, size;
	int i;

	// Create save string
	for (; di != NULL; di = di->next) {
		if (di->type == TYPE_FOLDER) {
			// Write folder
			len = 1;	// \x4
			if (di->title != NULL) {
				len += tchar_to_char_size(di->title);
			}
			len++;		// \0

			// Allocate
			if ((p = buf = mem_alloc(len)) == NULL) {
				message_get_error(GetLastError(), err_str);
				return FALSE;
			}

			// Item start mark
			*(p++) = '\x4';
			// Title
			if (di->title != NULL) {
				tchar_to_char(di->title, p, tchar_to_char_size(di->title));
				p += tchar_to_char_size(di->title);
			}
			*(p++) = '\0';

			// Write
			if (file_write_all(hFile, buf, len, err_str) == FALSE) {
				mem_free(&buf);
				return FALSE;
			}
			mem_free(&buf);

			if (file_item_to_file(hFile, di->child, filter_save, err_str) == FALSE) {
				return FALSE;
			}
			// Write
			if (file_write_all(hFile, "\x5", 1, err_str) == FALSE) {
				return FALSE;
			}
			continue;
		}

		if (di->type != TYPE_ITEM || di->child == NULL) {
			continue;
		}

		// Check save filter (skip entire item if no formats to save)
		if (filter_save == TRUE) {
			BOOL has_save_format = FALSE;
			for (cdi = di->child; cdi != NULL; cdi = cdi->next) {
				if (filter_save_check(cdi->format_name) == TRUE) {
					has_save_format = TRUE;
					break;
				}
			}
			if (has_save_format == FALSE) {
				continue;
			}
		}

		// Get size
		len = 1;	// \x1
		if (di->title != NULL) {
			len += tchar_to_char_size(di->title);
		}
		len++;		// \0
		len += 16;
		len++;		// \0
		if (di->window_name != NULL) {
			len += tchar_to_char_size(di->window_name);
		}
		len++;		// \0
		if (di->plugin_string != NULL) {
			len += tchar_to_char_size(di->plugin_string);
		}
		len++;		// \0
		_itot_s(di->plugin_param, str_param, BUF_SIZE, 10);
		len += tchar_to_char_size(str_param);
		len++;		// \0
		tp = str_op;
		_itot_s(di->op_modifiers, tp, BUF_SIZE, 10);
		tp += lstrlen(tp);
		*(tp++) = TEXT(',');
		_itot_s(di->op_virtkey, tp, BUF_SIZE - (tp - str_op), 10);
		tp += lstrlen(tp);
		*(tp++) = TEXT(',');
		_itot_s(di->op_paste, tp, BUF_SIZE - (tp - str_op), 10);
		len += tchar_to_char_size(str_op);
		len++;		// \0
		len++;		// \x2

		// Allocate
		if ((p = buf = mem_alloc(len)) == NULL) {
			message_get_error(GetLastError(), err_str);
			return FALSE;
		}

		// Item start mark
		*(p++) = '\x1';

		// Title
		if (di->title != NULL) {
			tchar_to_char(di->title, p, tchar_to_char_size(di->title));
			p += tchar_to_char_size(di->title);
		}
		*(p++) = '\0';
		// Modified date and time
		_itot_s(di->modified.dwHighDateTime, str_date, BUF_SIZE, 16);
		for (i = 0; i < 8 - lstrlen(str_date); i++) {
			*(p++) = '0';
		}
		tchar_to_char(str_date, p, tchar_to_char_size(str_date));
		p += lstrlen(str_date);
		_itot_s(di->modified.dwLowDateTime, str_date, BUF_SIZE, 16);
		for (i = 0; i < 8 - lstrlen(str_date); i++) {
			*(p++) = '0';
		}
		tchar_to_char(str_date, p, tchar_to_char_size(str_date));
		p += lstrlen(str_date);
		*(p++) = '\0';
		// Window name
		if (di->window_name != NULL) {
			tchar_to_char(di->window_name, p, tchar_to_char_size(di->window_name));
			p += tchar_to_char_size(di->window_name);
		}
		*(p++) = '\0';
		// String for tool
		if (di->plugin_string != NULL) {
			tchar_to_char(di->plugin_string, p, tchar_to_char_size(di->plugin_string));
			p += tchar_to_char_size(di->plugin_string);
		}
		*(p++) = '\0';
		// Long for tool
		tchar_to_char(str_param, p, tchar_to_char_size(str_param));
		p += tchar_to_char_size(str_param);
		*(p++) = '\0';
		// Options
		tchar_to_char(str_op, p, tchar_to_char_size(str_op));
		p += tchar_to_char_size(str_op);
		*(p++) = '\0';
		// Header start mark
		*(p++) = '\x2';

		// Write
		if (file_write_all(hFile, buf, len, err_str) == FALSE) {
			mem_free(&buf);
			return FALSE;
		}
		mem_free(&buf);

		for (cdi = di->child; cdi != NULL; cdi = cdi->next) {
			BYTE *mem = NULL;
			const void *data_ptr = NULL;
			BOOL need_free_mem = FALSE;
			BOOL need_unlock_data = FALSE;
			char header_stack[BUF_SIZE * 2];
			char *hbuf = header_stack;
			BOOL hbuf_allocated = FALSE;

			if (filter_save == TRUE && filter_save_check(cdi->format_name) == FALSE) {
				continue;
			}

			// Get data
			size = 0;
			UINT fmt = cdi->format ? cdi->format : clipboard_get_format(0, cdi->format_name);
			if ((mem = format_data_to_bytes(cdi, &size)) != NULL) {
				data_ptr = mem;
				need_free_mem = TRUE;
			} else if (fmt == CF_BITMAP || fmt == CF_DSPBITMAP || fmt == CF_PALETTE ||
			           fmt == CF_DSPMETAFILEPICT || fmt == CF_METAFILEPICT ||
			           fmt == CF_DSPENHMETAFILE || fmt == CF_ENHMETAFILE) {
				if ((mem = clipboard_data_to_bytes(cdi, &size)) != NULL) {
					data_ptr = mem;
					need_free_mem = TRUE;
				}
			} else if (cdi->data != NULL) {
				data_ptr = GlobalLock(cdi->data);
				if (data_ptr != NULL) {
					size = cdi->size;
					need_unlock_data = TRUE;
				} else if ((mem = clipboard_data_to_bytes(cdi, &size)) != NULL) {
					data_ptr = mem;
					need_free_mem = TRUE;
				}
			}

			if (data_ptr != NULL && size > 0) {
				_itot_s(size, str_size, BUF_SIZE, 10);
			} else {
				*str_size = TEXT('0');
				*(str_size + 1) = TEXT('\0');
				size = 0;
				data_ptr = NULL;
			}

			_itot_s(cdi->plugin_param, str_param, BUF_SIZE, 10);
			tp = str_op;
			_itot_s(cdi->op_modifiers, tp, BUF_SIZE, 10);
			tp += lstrlen(tp);
			*(tp++) = TEXT(',');
			_itot_s(cdi->op_virtkey, tp, BUF_SIZE - (tp - str_op), 10);
			tp += lstrlen(tp);
			*(tp++) = TEXT(',');
			_itot_s(cdi->op_paste, tp, BUF_SIZE - (tp - str_op), 10);

			// Calculate header size
			len = 0;
			len += tchar_to_char_size(str_size);
			len++;			// \0
			if (cdi->format_name != NULL) {
				len += tchar_to_char_size(cdi->format_name);
			}
			len++;			// \0
			if (cdi->plugin_string != NULL) {
				len += tchar_to_char_size(cdi->plugin_string);
			}
			len++;			// \0
			len += tchar_to_char_size(str_param);
			len++;			// \0
			len += tchar_to_char_size(str_op);
			len++;			// \0
			len++;			// \x3

			if (len > sizeof(header_stack)) {
				hbuf = (char *)mem_alloc(len);
				if (hbuf == NULL) {
					message_get_error(GetLastError(), err_str);
					if (need_free_mem) mem_free((void **)&mem);
					if (need_unlock_data) GlobalUnlock(cdi->data);
					return FALSE;
				}
				hbuf_allocated = TRUE;
			}

			p = (BYTE *)hbuf;

			// Create header
			// Size
			tchar_to_char(str_size, (char *)p, tchar_to_char_size(str_size));
			p += tchar_to_char_size(str_size);
			*(p++) = '\0';
			// Format name
			if (cdi->format_name != NULL) {
				tchar_to_char(cdi->format_name, (char *)p, tchar_to_char_size(cdi->format_name));
				p += tchar_to_char_size(cdi->format_name);
			}
			*(p++) = '\0';
			// String for tool
			if (cdi->plugin_string != NULL) {
				tchar_to_char(cdi->plugin_string, (char *)p, tchar_to_char_size(cdi->plugin_string));
				p += tchar_to_char_size(cdi->plugin_string);
			}
			*(p++) = '\0';
			// Long for tool
			tchar_to_char(str_param, (char *)p, tchar_to_char_size(str_param));
			p += tchar_to_char_size(str_param);
			*(p++) = '\0';
			// Options
			tchar_to_char(str_op, (char *)p, tchar_to_char_size(str_op));
			p += tchar_to_char_size(str_op);
			*(p++) = '\0';
			// Data start mark
			*(p++) = '\x3';

			// Write header
			if (file_write_all(hFile, hbuf, len, err_str) == FALSE) {
				if (hbuf_allocated) mem_free((void **)&hbuf);
				if (need_free_mem) mem_free((void **)&mem);
				if (need_unlock_data) GlobalUnlock(cdi->data);
				return FALSE;
			}
			if (hbuf_allocated) {
				mem_free((void **)&hbuf);
			}

			// Direct data write (no buffer duplication)
			if (data_ptr != NULL && size > 0) {
				if (file_write_all(hFile, data_ptr, size, err_str) == FALSE) {
					if (need_free_mem) mem_free((void **)&mem);
					if (need_unlock_data) GlobalUnlock(cdi->data);
					return FALSE;
				}
			}

			if (need_free_mem) {
				mem_free((void **)&mem);
			}
			if (need_unlock_data) {
				GlobalUnlock(cdi->data);
			}
		}
	}
	return TRUE;
}

/*
 * file_write_data - create item list file
 */
BOOL file_write_data(const TCHAR *path, DATA_INFO *di, const BOOL filter_save, TCHAR *err_str)
{
	HANDLE hFile;
	DWORD err;

	TCHAR tmp_path[MAX_PATH];

	if (lstrlen(path) + 4 >= MAX_PATH) {
		message_get_error(ERROR_BUFFER_OVERFLOW, err_str);
		return FALSE;
	}
	wsprintf(tmp_path, TEXT("%s.tmp"), path);
	DeleteFile(tmp_path);

	// Save
	hFile = CreateFile(tmp_path, GENERIC_READ | GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == NULL || hFile == (HANDLE)-1) {
		message_get_error(GetLastError(), err_str);
		return FALSE;
	}
	if (file_item_to_file(hFile, di, filter_save, err_str) == FALSE) {
		CloseHandle(hFile);
		DeleteFile(tmp_path);
		return FALSE;
	}
	if (FlushFileBuffers(hFile) == FALSE) {
		message_get_error(GetLastError(), err_str);
		CloseHandle(hFile);
		DeleteFile(tmp_path);
		return FALSE;
	}
	CloseHandle(hFile);

	// Attempt atomic replacement
	if (ReplaceFile(path, tmp_path, NULL, REPLACEFILE_IGNORE_MERGE_ERRORS, NULL, NULL) == FALSE) {
		// Fall back to MoveFileEx if ReplaceFile fails
		if (MoveFileEx(tmp_path, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE) {
			err = GetLastError();
			DeleteFile(tmp_path);
			message_get_error(err, err_str);
			return FALSE;
		}
	}
	return TRUE;
}

/*
 * shell_open - execute file
 */
BOOL shell_open(const TCHAR *file_name, const TCHAR *command_line)
{
	SHELLEXECUTEINFO sei;

	ZeroMemory(&sei, sizeof(SHELLEXECUTEINFO));
	sei.cbSize = sizeof(sei);
	sei.fMask = 0;
	sei.hInstApp = hInst;
	sei.hwnd = NULL;
	sei.lpVerb = NULL;
	sei.lpFile = file_name;
	sei.lpParameters = command_line;
	sei.lpDirectory = NULL;
	sei.nShow = SW_SHOWNORMAL;
	return ShellExecuteEx(&sei);
}
/* End of source */
