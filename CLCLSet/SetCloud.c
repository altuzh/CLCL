/*
 * CLCLSet
 *
 * SetCloud.c
 *
 * Yandex Disk Cloud Synchronization Settings & Operations
 */

#define _CRT_SECURE_NO_WARNINGS
#define _INC_OLE
#include <windows.h>
#undef  _INC_OLE
#include <tchar.h>
#include <stdio.h>
#include <wincrypt.h>
#include <winsqlite/winsqlite3.h>

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "winsqlite3.lib")

#include "..\General.h"
#include "..\Data.h"
#include "..\File.h"
#include "..\Ini.h"
#include "..\Profile.h"
#include "..\Memory.h"
#include "..\String.h"
#include "..\DarkMode.h"

#include "CLCLSet.h"
#include "SetCloud.h"
#include "YandexClient.h"
#include "CloudHash.h"
#include "resource.h"

/* Custom window messages for worker thread updates */
#define WM_CLOUD_PROGRESS    (WM_APP + 101)
#define WM_CLOUD_COMPLETE    (WM_APP + 102)

extern HINSTANCE hInst;
extern OPTION_INFO option;
extern TCHAR work_path[];

/* Worker parameters */
typedef struct _CLOUD_WORKER_PARAMS {
	HWND hDlg;
	TCHAR token[512];
	TCHAR batch_path[512];
	BOOL is_save;
} CLOUD_WORKER_PARAMS;


static HANDLE hWorkerThread = NULL;

/*
 * cloud_save_token - Protect token using Windows DPAPI and store hex string
 */
BOOL cloud_save_token(const TCHAR *ini_path, const TCHAR *token)
{
	DATA_BLOB in;
	DATA_BLOB out;
	TCHAR hex_buf[1024] = { 0 };
	DWORD i;

	if (token == NULL || *token == TEXT('\0')) {
		profile_write_string(TEXT("cloud"), TEXT("token"), TEXT(""), ini_path);
		return TRUE;
	}

	in.pbData = (BYTE *)token;
	in.cbData = (DWORD)((lstrlen(token) + 1) * sizeof(TCHAR));

	if (!CryptProtectData(&in, L"CLCL Cloud Token", NULL, NULL, NULL, 0, &out)) {
		return FALSE;
	}

	for (i = 0; i < out.cbData && (i * 2 + 2) < 1024; i++) {
		_sntprintf(hex_buf + (i * 2), 3, TEXT("%02X"), out.pbData[i]);
	}
	LocalFree(out.pbData);

	profile_write_string(TEXT("cloud"), TEXT("token"), hex_buf, ini_path);
	profile_flush(ini_path);
	return TRUE;
}

/*
 * cloud_load_token - Read hex string and unprotect via Windows DPAPI
 */
BOOL cloud_load_token(const TCHAR *ini_path, TCHAR *token_out, DWORD max_len)
{
	TCHAR hex_buf[1024] = { 0 };
	BYTE bin_buf[512] = { 0 };
	DWORD bin_len = 0;
	DWORD hex_len;
	DWORD i;
	DATA_BLOB in;
	DATA_BLOB out;

	if (token_out == NULL || max_len == 0) return FALSE;
	token_out[0] = TEXT('\0');

	if (profile_get_string(TEXT("cloud"), TEXT("token"), TEXT(""), hex_buf, 1024, ini_path) <= 0) {
		return FALSE;
	}

	hex_len = lstrlen(hex_buf);
	if (hex_len == 0 || (hex_len % 2) != 0) {
		return FALSE;
	}

	bin_len = hex_len / 2;
	if (bin_len > sizeof(bin_buf)) bin_len = sizeof(bin_buf);

	for (i = 0; i < bin_len; i++) {
		unsigned int byte_val = 0;
		TCHAR byte_str[3] = { hex_buf[i * 2], hex_buf[i * 2 + 1], TEXT('\0') };
		_stscanf_s(byte_str, TEXT("%02X"), &byte_val);
		bin_buf[i] = (BYTE)byte_val;
	}

	in.pbData = bin_buf;
	in.cbData = bin_len;

	if (CryptUnprotectData(&in, NULL, NULL, NULL, NULL, 0, &out)) {
		lstrcpyn(token_out, (const TCHAR *)out.pbData, max_len);
		LocalFree(out.pbData);
		return TRUE;
	}

	// Plain text fallback if DPAPI failed
	lstrcpyn(token_out, hex_buf, max_len);
	return TRUE;
}

/*
 * sanitize_folder_name - Clean folder name for use in cloud path
 */
static void sanitize_folder_name(const TCHAR *src, TCHAR *dst, DWORD max_len)
{
	DWORD i, j = 0;
	if (src == NULL || dst == NULL || max_len == 0) return;
	for (i = 0; src[i] != TEXT('\0') && j + 1 < max_len; i++) {
		TCHAR c = src[i];
		if (c == TEXT('/') || c == TEXT('\\') || c == TEXT(':') || c == TEXT('*') ||
			c == TEXT('?') || c == TEXT('\"') || c == TEXT('<') || c == TEXT('>') || c == TEXT('|')) {
			c = TEXT('_');
		}
		dst[j++] = c;
	}
	dst[j] = TEXT('\0');
	if (j == 0) lstrcpy(dst, TEXT("General"));
}

/*
 * create_text_clip - Create DATA_INFO item from UTF-8 text
 */
static DATA_INFO *create_text_clip(const char *utf8, DWORD utf8_len, const SYSTEMTIME *st)
{
	int wlen;
	HGLOBAL hData;
	WCHAR *wstr;
	TCHAR title[64] = { 0 };
	int ti = 0;
	int i;
	DATA_INFO *item;
	DATA_INFO *cdi;

	if (utf8 == NULL || utf8_len == 0) return NULL;

	wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, utf8_len, NULL, 0);
	if (wlen <= 0) return NULL;

	hData = GlobalAlloc(GHND, (wlen + 1) * sizeof(WCHAR));
	if (hData == NULL) return NULL;

	wstr = (WCHAR *)GlobalLock(hData);
	MultiByteToWideChar(CP_UTF8, 0, utf8, utf8_len, wstr, wlen);
	wstr[wlen] = L'\0';
	GlobalUnlock(hData);

	// Extract title from first non-empty line (up to 50 chars)
	for (i = 0; i < wlen && ti < 50; i++) {
		if (wstr[i] == L'\r' || wstr[i] == L'\n') {
			if (ti > 0) break;
			continue;
		}
		title[ti++] = (TCHAR)wstr[i];
	}
	title[ti] = TEXT('\0');
	if (ti == 0) lstrcpy(title, TEXT("Text Clip"));

	item = data_create_item(title, FALSE, NULL);
	if (item == NULL) {
		GlobalFree(hData);
		return NULL;
	}

	if (st != NULL) {
		SystemTimeToFileTime(st, &item->modified);
	} else {
		data_set_modified(item);
	}

	cdi = data_create_data(CF_UNICODETEXT, TEXT("UNICODE TEXT"), hData, (wlen + 1) * sizeof(WCHAR), FALSE, NULL);
	if (cdi == NULL) {
		data_free(item);
		return NULL;
	}
	item->child = cdi;
	return item;
}

/*
 * fnv1a_64_calc - 64-bit FNV-1a hash matching CLCL native content_hash
 */
static UINT64 fnv1a_64_calc(const void *data, size_t len)
{
	const BYTE *p = (const BYTE *)data;
	UINT64 h = 0xCBF29CE484222325ULL;
	size_t i;
	for (i = 0; i < len; i++) {
		h ^= (UINT64)p[i];
		h *= 0x100000001B3ULL;
	}
	return h;
}

/*
 * db_insert_text_clip - Insert text item into SQLite history.db
 */
static BOOL db_insert_text_clip(sqlite3 *db, const char *utf8, DWORD utf8_len, const SYSTEMTIME *st, const TCHAR *hash_str, TCHAR *err_msg, DWORD err_msg_len)
{
	int wlen;
	WCHAR *wstr;
	TCHAR title[64] = { 0 };
	int ti = 0;
	int i;
	FILETIME ft;
	sqlite3_int64 ft_created;
	unsigned __int64 hash_val = 0;
	sqlite3_stmt *stmt = NULL;
	int total_size;
	sqlite3_int64 item_id;
	int rc;

	if (db == NULL || utf8 == NULL || utf8_len == 0) return FALSE;

	wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, utf8_len, NULL, 0);
	if (wlen <= 0) return FALSE;

	wstr = (WCHAR *)malloc((wlen + 1) * sizeof(WCHAR));
	if (wstr == NULL) return FALSE;
	MultiByteToWideChar(CP_UTF8, 0, utf8, utf8_len, wstr, wlen);
	wstr[wlen] = L'\0';

	for (i = 0; i < wlen && ti < 50; i++) {
		if (wstr[i] == L'\r' || wstr[i] == L'\n') {
			if (ti > 0) break;
			continue;
		}
		title[ti++] = (TCHAR)wstr[i];
	}
	title[ti] = TEXT('\0');
	if (ti == 0) lstrcpy(title, TEXT("Text Clip"));

	if (st != NULL) {
		SystemTimeToFileTime(st, &ft);
	} else {
		GetSystemTimeAsFileTime(&ft);
	}
	ft_created = (((sqlite3_int64)ft.dwHighDateTime) << 32) | ft.dwLowDateTime;

	hash_val = fnv1a_64_calc(wstr, wcslen(wstr) * sizeof(WCHAR));

	BOOL inside_tx = (sqlite3_get_autocommit(db) == 0);
	if (!inside_tx) {
		sqlite3_exec(db, "BEGIN IMMEDIATE;", NULL, NULL, NULL);
	}

	rc = sqlite3_prepare16_v2(db,
		L"INSERT INTO items (created_at, window_title, title, pinned, total_size, text_content, content_hash) "
		L"VALUES (?, 'Cloud Import', ?, 0, ?, ?, ?);",
		-1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		if (err_msg != NULL && err_msg_len > 0) {
			const char *em = sqlite3_errmsg(db);
#ifdef UNICODE
			MultiByteToWideChar(CP_UTF8, 0, em, -1, err_msg, err_msg_len);
#else
			lstrcpyn(err_msg, em, err_msg_len);
#endif
		}
		if (!inside_tx) sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
		free(wstr);
		return FALSE;
	}

	total_size = (wlen + 1) * sizeof(WCHAR);
	sqlite3_bind_int64(stmt, 1, ft_created);
	sqlite3_bind_text16(stmt, 2, title, -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(stmt, 3, total_size);
	sqlite3_bind_text16(stmt, 4, wstr, -1, SQLITE_TRANSIENT);
	if (hash_val != 0) {
		sqlite3_bind_int64(stmt, 5, (sqlite3_int64)hash_val);
	} else {
		sqlite3_bind_null(stmt, 5);
	}

	rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	if (rc != SQLITE_DONE) {
		if (err_msg != NULL && err_msg_len > 0) {
			const char *em = sqlite3_errmsg(db);
#ifdef UNICODE
			MultiByteToWideChar(CP_UTF8, 0, em, -1, err_msg, err_msg_len);
#else
			lstrcpyn(err_msg, em, err_msg_len);
#endif
		}
		if (!inside_tx) sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
		free(wstr);
		return FALSE;
	}

	item_id = sqlite3_last_insert_rowid(db);

	// Insert into history_fts
	rc = sqlite3_prepare16_v2(db,
		L"INSERT INTO history_fts (docid, title, text_content, window_title) VALUES (?, ?, ?, 'Cloud Import');",
		-1, &stmt, NULL);
	if (rc == SQLITE_OK) {
		sqlite3_bind_int64(stmt, 1, item_id);
		sqlite3_bind_text16(stmt, 2, title, -1, SQLITE_TRANSIENT);
		sqlite3_bind_text16(stmt, 3, wstr, -1, SQLITE_TRANSIENT);
		sqlite3_step(stmt);
		sqlite3_finalize(stmt);
	}

	// Insert into item_formats
	rc = sqlite3_prepare16_v2(db,
		L"INSERT OR REPLACE INTO item_formats (item_id, format_name, format_id, data_size, data) "
		L"VALUES (?, 'UNICODE TEXT', 13, ?, ?);",
		-1, &stmt, NULL);
	if (rc == SQLITE_OK) {
		sqlite3_bind_int64(stmt, 1, item_id);
		sqlite3_bind_int(stmt, 2, total_size);
		sqlite3_bind_blob(stmt, 3, wstr, total_size, SQLITE_TRANSIENT);
		sqlite3_step(stmt);
		sqlite3_finalize(stmt);
	}

	if (!inside_tx) {
		sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
	}

	free(wstr);
	return TRUE;
}

/*
 * count_fav_text_items - Count text clips in favourites tree
 */
static int count_fav_text_items(const DATA_INFO *root)
{
	int count = 0;
	const DATA_INFO *di;
	for (di = root; di != NULL; di = di->next) {
		if (di->type == TYPE_FOLDER) {
			const DATA_INFO *item_di;
			for (item_di = di->child; item_di != NULL; item_di = item_di->next) {
				const DATA_INFO *cdi;
				if (item_di->type != TYPE_ITEM) continue;
				for (cdi = item_di->child; cdi != NULL; cdi = cdi->next) {
					if (cdi->data != NULL && cdi->size > 0 && cdi->size <= CLOUD_MAX_ITEM_SIZE &&
						cdi->format_name != NULL &&
						(lstrcmpi(cdi->format_name, TEXT("UNICODE TEXT")) == 0 ||
						 lstrcmpi(cdi->format_name, TEXT("TEXT")) == 0 ||
						 lstrcmpi(cdi->format_name, TEXT("OEM TEXT")) == 0)) {
						count++;
						break;
					}
				}
			}
		} else if (di->type == TYPE_ITEM) {
			const DATA_INFO *cdi;
			for (cdi = di->child; cdi != NULL; cdi = cdi->next) {
				if (cdi->data != NULL && cdi->size > 0 && cdi->size <= CLOUD_MAX_ITEM_SIZE &&
					cdi->format_name != NULL &&
					(lstrcmpi(cdi->format_name, TEXT("UNICODE TEXT")) == 0 ||
					 lstrcmpi(cdi->format_name, TEXT("TEXT")) == 0 ||
					 lstrcmpi(cdi->format_name, TEXT("OEM TEXT")) == 0)) {
					count++;
					break;
				}
			}
		}
	}
	return count;
}

/*
 * cloud_save_worker - Thread procedure for Save Data to Cloud
 */
static DWORD WINAPI cloud_save_worker(LPVOID lpParam)
{
	CLOUD_WORKER_PARAMS *params = (CLOUD_WORKER_PARAMS *)lpParam;
	HWND hDlg = params->hDlg;
	const TCHAR *token = params->token;
	TCHAR err_str[BUF_SIZE] = { 0 };
	TCHAR user_name[128] = { 0 };
	TCHAR status_msg[BUF_SIZE];
	TCHAR regist_path[MAX_PATH];
	TCHAR db_path[MAX_PATH];
	int fav_uploaded = 0;
	int hist_uploaded = 0;
	int fav_failed = 0;
	int hist_failed = 0;
	int total_fav = 0;
	int total_hist = 0;
	int total_items = 0;
	int current_item = 0;
	TCHAR last_error[BUF_SIZE] = { 0 };
	DATA_INFO *fav_root = NULL;
	DATA_INFO *folder_di;
	DATA_INFO *item_di;
	sqlite3 *db = NULL;
	sqlite3_stmt *stmt = NULL;
	HWND hClcl;
	SYSTEMTIME now;
	TCHAR batch_name[64];
	TCHAR batch_folder_path[512];
	TCHAR batch_fav_folder[512];
	TCHAR batch_hist_folder[512];

	// 1. Verify connection
	SendMessage(hDlg, WM_CLOUD_PROGRESS, 0, (LPARAM)TEXT("Connecting to Yandex Disk..."));
	if (!yandex_test_connection(token, user_name, sizeof(user_name) / sizeof(TCHAR), err_str)) {
		TCHAR *res_err = alloc_copy(err_str);
		PostMessage(hDlg, WM_CLOUD_COMPLETE, FALSE, (LPARAM)res_err);
		free(params);
		return 1;
	}

	// 2. Pre-flush running CLCL instance
	hClcl = FindWindow(MAIN_WND_CLASS, MAIN_WINDOW_TITLE);
	if (hClcl != NULL) {
		SendMessage(hClcl, WM_HISTORY_SAVE, 0, 0);
		SendMessage(hClcl, WM_REGIST_SAVE, 0, 0);
	}

	// 3. Create cloud directory structure for new batch (YYYY-MM-DD HH:MM:SS)
	GetLocalTime(&now);
	_sntprintf(batch_name, 64, TEXT("%04d-%02d-%02d %02d:%02d:%02d"),
		now.wYear, now.wMonth, now.wDay,
		now.wHour, now.wMinute, now.wSecond);
	_sntprintf(batch_folder_path, 512, TEXT("%s/%s"), YANDEX_ROOT_FOLDER, batch_name);
	_sntprintf(batch_fav_folder, 512, TEXT("%s/Favourites"), batch_folder_path);
	_sntprintf(batch_hist_folder, 512, TEXT("%s/History"), batch_folder_path);

	SendMessage(hDlg, WM_CLOUD_PROGRESS, 0, (LPARAM)TEXT("Creating cloud backup batch..."));
	yandex_ensure_folder(token, YANDEX_ROOT_FOLDER, err_str);
	yandex_ensure_folder(token, batch_folder_path, err_str);
	yandex_ensure_folder(token, batch_fav_folder, err_str);
	yandex_ensure_folder(token, batch_hist_folder, err_str);


	// 4. Upload Favourites
	SendMessage(hDlg, WM_CLOUD_PROGRESS, 0, (LPARAM)TEXT("Reading local items..."));
	wsprintf(regist_path, TEXT("%s\\%s"), work_path, REGIST_FILENAME);
	file_read_data(regist_path, &fav_root, err_str);

	total_fav = count_fav_text_items(fav_root);

	// Pre-count History text items from SQLite history.db
	wsprintf(db_path, TEXT("%s\\history.db"), work_path);
	if (sqlite3_open16(db_path, &db) == SQLITE_OK && db != NULL) {
		sqlite3_busy_timeout(db, 5000);
		sqlite3_stmt *cnt_stmt = NULL;
		const char *cnt_query = "SELECT COUNT(*) FROM items WHERE text_content IS NOT NULL AND text_content != '' AND length(text_content) <= 1048576;";
		if (sqlite3_prepare_v2(db, cnt_query, -1, &cnt_stmt, NULL) == SQLITE_OK) {
			if (sqlite3_step(cnt_stmt) == SQLITE_ROW) {
				total_hist = sqlite3_column_int(cnt_stmt, 0);
			}
			sqlite3_finalize(cnt_stmt);
		}
		sqlite3_close(db);
		db = NULL;
	}

	total_items = total_fav + total_hist;

	if (fav_root != NULL) {
		for (folder_di = fav_root; folder_di != NULL; folder_di = folder_di->next) {
			TCHAR safe_folder[128];
			TCHAR folder_cloud_path[512];

			if (folder_di->type == TYPE_FOLDER) {
				sanitize_folder_name(folder_di->title, safe_folder, 128);
				_sntprintf(folder_cloud_path, 512, TEXT("%s/%s"), batch_fav_folder, safe_folder);
				yandex_ensure_folder(token, folder_cloud_path, NULL);

				for (item_di = folder_di->child; item_di != NULL; item_di = item_di->next) {
					DATA_INFO *cdi;
					if (item_di->type != TYPE_ITEM) continue;

					for (cdi = item_di->child; cdi != NULL; cdi = cdi->next) {
						char *utf8_buf = NULL;
						DWORD utf8_len = 0;
						TCHAR hash_str[CLOUD_HASH_STR_SIZE];

						if (cloud_calc_data_hash(cdi->data, cdi->size, cdi->format_name, hash_str, &utf8_buf, &utf8_len)) {
							SYSTEMTIME st;
							TCHAR fname[MAX_PATH];
							TCHAR remote_file_path[MAX_PATH * 2];
							TCHAR upload_url[2048];
							TCHAR item_err[BUF_SIZE] = { 0 };
							TCHAR prog_msg[128];

							current_item++;
							wsprintf(prog_msg, TEXT("Saving %d of %d..."), current_item, total_items);
							SendMessage(hDlg, WM_CLOUD_PROGRESS, 0, (LPARAM)prog_msg);

							FileTimeToSystemTime(&item_di->modified, &st);
							if (st.wYear < 1980) GetLocalTime(&st);

							cloud_format_filename(&st, hash_str, fname, MAX_PATH);
							_sntprintf(remote_file_path, MAX_PATH * 2, TEXT("%s/%s"), folder_cloud_path, fname);

							if (yandex_get_upload_url(token, remote_file_path, upload_url, 2048, item_err)) {
								if (yandex_upload_file(upload_url, utf8_buf, utf8_len, item_err)) {
									fav_uploaded++;
								} else {
									fav_failed++;
									lstrcpy(last_error, item_err);
								}
							} else {
								fav_failed++;
								lstrcpy(last_error, item_err);
							}
							free(utf8_buf);
							break;
						}
					}
				}
			} else if (folder_di->type == TYPE_ITEM) {
				// Root level item
				DATA_INFO *cdi;
				for (cdi = folder_di->child; cdi != NULL; cdi = cdi->next) {
					char *utf8_buf = NULL;
					DWORD utf8_len = 0;
					TCHAR hash_str[CLOUD_HASH_STR_SIZE];

					if (cloud_calc_data_hash(cdi->data, cdi->size, cdi->format_name, hash_str, &utf8_buf, &utf8_len)) {
						SYSTEMTIME st;
						TCHAR fname[MAX_PATH];
						TCHAR remote_file_path[MAX_PATH * 2];
						TCHAR upload_url[2048];
						TCHAR item_err[BUF_SIZE] = { 0 };
						TCHAR prog_msg[128];

						current_item++;
						wsprintf(prog_msg, TEXT("Saving %d of %d..."), current_item, total_items);
						SendMessage(hDlg, WM_CLOUD_PROGRESS, 0, (LPARAM)prog_msg);

						FileTimeToSystemTime(&folder_di->modified, &st);
						if (st.wYear < 1980) GetLocalTime(&st);

						cloud_format_filename(&st, hash_str, fname, MAX_PATH);
						_sntprintf(remote_file_path, MAX_PATH * 2, TEXT("%s/%s"), batch_fav_folder, fname);

						if (yandex_get_upload_url(token, remote_file_path, upload_url, 2048, item_err)) {
							if (yandex_upload_file(upload_url, utf8_buf, utf8_len, item_err)) {
								fav_uploaded++;
							} else {
								fav_failed++;
								lstrcpy(last_error, item_err);
							}
						} else {
							fav_failed++;
							lstrcpy(last_error, item_err);
						}
						free(utf8_buf);
						break;
					}
				}
			}
		}
		data_free(fav_root);
	}

	// 5. Upload History from SQLite history.db
	wsprintf(db_path, TEXT("%s\\history.db"), work_path);

	if (sqlite3_open16(db_path, &db) == SQLITE_OK && db != NULL) {
		sqlite3_busy_timeout(db, 5000);
		const char *query = "SELECT id, created_at, text_content FROM items WHERE text_content IS NOT NULL AND text_content != '' ORDER BY created_at DESC;";
		if (sqlite3_prepare_v2(db, query, -1, &stmt, NULL) == SQLITE_OK) {
			while (sqlite3_step(stmt) == SQLITE_ROW) {
				sqlite3_int64 ft_val = sqlite3_column_int64(stmt, 1);
				const char *txt_utf8 = (const char *)sqlite3_column_text(stmt, 2);
				DWORD ulen = (DWORD)sqlite3_column_bytes(stmt, 2);

				if (txt_utf8 != NULL && ulen > 0 && ulen <= CLOUD_MAX_ITEM_SIZE) {
					TCHAR hash_str[CLOUD_HASH_STR_SIZE];
					if (cloud_calc_text_hash(txt_utf8, ulen, hash_str)) {
						FILETIME ft;
						SYSTEMTIME st;
						TCHAR fname[MAX_PATH];
						TCHAR remote_file_path[MAX_PATH * 2];
						TCHAR upload_url[2048];
						TCHAR item_err[BUF_SIZE] = { 0 };
						TCHAR prog_msg[128];

						current_item++;
						wsprintf(prog_msg, TEXT("Saving %d of %d..."), current_item, total_items);
						SendMessage(hDlg, WM_CLOUD_PROGRESS, 0, (LPARAM)prog_msg);

						ft.dwLowDateTime = (DWORD)(ft_val & 0xFFFFFFFF);
						ft.dwHighDateTime = (DWORD)(ft_val >> 32);
						FileTimeToSystemTime(&ft, &st);
						if (st.wYear < 1980) GetLocalTime(&st);

						cloud_format_filename(&st, hash_str, fname, MAX_PATH);
						_sntprintf(remote_file_path, MAX_PATH * 2, TEXT("%s/%s"), batch_hist_folder, fname);

						if (yandex_get_upload_url(token, remote_file_path, upload_url, 2048, item_err)) {
							if (yandex_upload_file(upload_url, txt_utf8, ulen, item_err)) {
								hist_uploaded++;
							} else {
								hist_failed++;
								lstrcpy(last_error, item_err);
							}
						} else {
							hist_failed++;
							lstrcpy(last_error, item_err);
						}
					}
				}
			}
			sqlite3_finalize(stmt);
		}
		sqlite3_close(db);
	}

	if (fav_failed > 0 || hist_failed > 0) {
		_sntprintf(status_msg, BUF_SIZE,
			TEXT("Cloud save completed with errors!\r\n\r\n")
			TEXT("• Batch: %s\r\n")
			TEXT("• History items uploaded: %d\r\n")
			TEXT("• Favourites uploaded: %d\r\n")
			TEXT("• Upload failures: %d\r\n\r\n")
			TEXT("Error: %s"),
			batch_name,
			hist_uploaded, fav_uploaded,
			fav_failed + hist_failed,
			last_error[0] != TEXT('\0') ? last_error : TEXT("Failed to upload items to cloud."));
	} else {
		_sntprintf(status_msg, BUF_SIZE,
			TEXT("Cloud save completed successfully!\r\n\r\n")
			TEXT("• Batch: %s\r\n")
			TEXT("• History items uploaded: %d\r\n")
			TEXT("• Favourites uploaded: %d"),
			batch_name,
			hist_uploaded, fav_uploaded);
	}


	TCHAR *res_msg = alloc_copy(status_msg);
	PostMessage(hDlg, WM_CLOUD_COMPLETE, (fav_failed + hist_failed == 0), (LPARAM)res_msg);
	free(params);
	return 0;
}

/*
 * cloud_import_worker - Thread procedure for Import Data from Cloud
 */
static DWORD WINAPI cloud_import_worker(LPVOID lpParam)
{
	CLOUD_WORKER_PARAMS *params = (CLOUD_WORKER_PARAMS *)lpParam;
	HWND hDlg = params->hDlg;
	const TCHAR *token = params->token;
	const TCHAR *batch_path = params->batch_path;
	TCHAR batch_fav_folder[512];
	TCHAR batch_hist_folder[512];
	TCHAR err_str[BUF_SIZE] = { 0 };
	TCHAR user_name[128] = { 0 };
	TCHAR status_msg[BUF_SIZE];
	TCHAR regist_path[MAX_PATH];
	TCHAR db_path[MAX_PATH];
	HASH_SET *local_fav_hashes = hash_set_create();
	HASH_SET *local_hist_hashes = hash_set_create();
	int fav_imported = 0;
	int hist_imported = 0;
	int fav_failed = 0;
	int hist_failed = 0;
	int dups_skipped = 0;
	TCHAR last_error[BUF_SIZE] = { 0 };
	DATA_INFO *fav_root = NULL;
	DATA_INFO *folder_di;
	DATA_INFO *item_di;
	sqlite3 *db = NULL;
	sqlite3_stmt *stmt = NULL;
	YANDEX_RESOURCE_LIST fav_subfolders;
	YANDEX_RESOURCE_LIST hist_files;
	YANDEX_RESOURCE_ITEM *res_item;
	HWND hClcl;

	if (batch_path != NULL && *batch_path != TEXT('\0')) {
		_sntprintf(batch_fav_folder, 512, TEXT("%s/Favourites"), batch_path);
		_sntprintf(batch_hist_folder, 512, TEXT("%s/History"), batch_path);
	} else {
		_sntprintf(batch_fav_folder, 512, TEXT("%s"), YANDEX_FAVOURITES_FOLDER);
		_sntprintf(batch_hist_folder, 512, TEXT("%s"), YANDEX_HISTORY_FOLDER);
	}

	// 1. Verify connection
	SendMessage(hDlg, WM_CLOUD_PROGRESS, 0, (LPARAM)TEXT("Connecting to Yandex Disk..."));
	if (!yandex_test_connection(token, user_name, sizeof(user_name) / sizeof(TCHAR), err_str)) {
		TCHAR *res_err = alloc_copy(err_str);
		PostMessage(hDlg, WM_CLOUD_COMPLETE, FALSE, (LPARAM)res_err);
		if (local_fav_hashes != NULL) hash_set_free(local_fav_hashes);
		if (local_hist_hashes != NULL) hash_set_free(local_hist_hashes);
		free(params);
		return 1;
	}


	// 2. Pre-flush running CLCL instance
	hClcl = FindWindow(MAIN_WND_CLASS, MAIN_WINDOW_TITLE);
	if (hClcl != NULL) {
		SendMessage(hClcl, WM_HISTORY_SAVE, 0, 0);
		SendMessage(hClcl, WM_REGIST_SAVE, 0, 0);
	}

	// 3. Index existing local hashes
	SendMessage(hDlg, WM_CLOUD_PROGRESS, 0, (LPARAM)TEXT("Indexing local clips for deduplication..."));
	wsprintf(regist_path, TEXT("%s\\%s"), work_path, REGIST_FILENAME);
	file_read_data(regist_path, &fav_root, err_str);

	if (fav_root != NULL) {
		for (folder_di = fav_root; folder_di != NULL; folder_di = folder_di->next) {
			if (folder_di->type == TYPE_FOLDER) {
				for (item_di = folder_di->child; item_di != NULL; item_di = item_di->next) {
					DATA_INFO *cdi;
					if (item_di->type != TYPE_ITEM) continue;
					for (cdi = item_di->child; cdi != NULL; cdi = cdi->next) {
						char *utf8_buf = NULL;
						DWORD utf8_len = 0;
						TCHAR hash_str[CLOUD_HASH_STR_SIZE];
						if (cloud_calc_data_hash(cdi->data, cdi->size, cdi->format_name, hash_str, &utf8_buf, &utf8_len)) {
							if (local_fav_hashes != NULL) hash_set_add(local_fav_hashes, hash_str);
							free(utf8_buf);
							break;
						}
					}
				}
			} else if (folder_di->type == TYPE_ITEM) {
				DATA_INFO *cdi;
				for (cdi = folder_di->child; cdi != NULL; cdi = cdi->next) {
					char *utf8_buf = NULL;
					DWORD utf8_len = 0;
					TCHAR hash_str[CLOUD_HASH_STR_SIZE];
					if (cloud_calc_data_hash(cdi->data, cdi->size, cdi->format_name, hash_str, &utf8_buf, &utf8_len)) {
						if (local_fav_hashes != NULL) hash_set_add(local_fav_hashes, hash_str);
						free(utf8_buf);
						break;
					}
				}
			}
		}
	}

	wsprintf(db_path, TEXT("%s\\history.db"), work_path);
	if (sqlite3_open16(db_path, &db) == SQLITE_OK && db != NULL) {
		sqlite3_busy_timeout(db, 5000);
		const char *query = "SELECT text_content FROM items WHERE text_content IS NOT NULL AND text_content != '';";
		if (sqlite3_prepare_v2(db, query, -1, &stmt, NULL) == SQLITE_OK) {
			while (sqlite3_step(stmt) == SQLITE_ROW) {
				const char *txt_utf8 = (const char *)sqlite3_column_text(stmt, 0);
				DWORD ulen = (DWORD)sqlite3_column_bytes(stmt, 0);
				if (txt_utf8 != NULL && ulen > 0) {
					TCHAR hash_str[CLOUD_HASH_STR_SIZE];
					if (cloud_calc_text_hash(txt_utf8, ulen, hash_str)) {
						if (local_hist_hashes != NULL) hash_set_add(local_hist_hashes, hash_str);
					}
				}
			}
			sqlite3_finalize(stmt);
		}
	}

	// 4. Import Favourites from cloud
	SendMessage(hDlg, WM_CLOUD_PROGRESS, 0, (LPARAM)TEXT("Scanning cloud favourites..."));
	ZeroMemory(&fav_subfolders, sizeof(fav_subfolders));
	if (yandex_list_resources(token, batch_fav_folder, &fav_subfolders, NULL)) {
		for (res_item = fav_subfolders.head; res_item != NULL; res_item = res_item->next) {
			if (res_item->is_dir) {
				YANDEX_RESOURCE_LIST folder_files;
				YANDEX_RESOURCE_ITEM *f_item;
				DATA_INFO *target_folder = NULL;

				// Find or create target folder in fav_root
				for (folder_di = fav_root; folder_di != NULL; folder_di = folder_di->next) {
					if (folder_di->type == TYPE_FOLDER && folder_di->title != NULL &&
						lstrcmpi(folder_di->title, res_item->name) == 0) {
						target_folder = folder_di;
						break;
					}
				}
				if (target_folder == NULL) {
					target_folder = data_create_folder(res_item->name, NULL);
					if (target_folder != NULL) {
						target_folder->next = fav_root;
						fav_root = target_folder;
					}
				}

				ZeroMemory(&folder_files, sizeof(folder_files));
				if (yandex_list_resources(token, res_item->path, &folder_files, NULL)) {
					for (f_item = folder_files.head; f_item != NULL; f_item = f_item->next) {
						SYSTEMTIME st;
						TCHAR hash_str[CLOUD_HASH_STR_SIZE];

						if (f_item->is_dir) continue;
						if (!cloud_parse_filename(f_item->name, &st, hash_str)) continue;

						if (local_fav_hashes != NULL && hash_set_contains(local_fav_hashes, hash_str)) {
							dups_skipped++;
							continue;
						}

						char *down_buf = NULL;
						DWORD down_size = 0;
						TCHAR item_err[BUF_SIZE] = { 0 };
						if (yandex_download_file(token, f_item->path, &down_buf, &down_size, item_err)) {
							if (down_buf != NULL && down_size > 0) {
								DATA_INFO *new_clip = create_text_clip(down_buf, down_size, &st);
								if (new_clip != NULL && target_folder != NULL) {
									new_clip->next = target_folder->child;
									target_folder->child = new_clip;
									if (local_fav_hashes != NULL) hash_set_add(local_fav_hashes, hash_str);
									fav_imported++;
								} else {
									fav_failed++;
								}
								free(down_buf);
							}
						} else {
							fav_failed++;
							lstrcpy(last_error, item_err);
						}
					}
					yandex_free_resource_list(&folder_files);
				}
			} else {
				// Root-level favourite item
				SYSTEMTIME st;
				TCHAR hash_str[CLOUD_HASH_STR_SIZE];

				if (!cloud_parse_filename(res_item->name, &st, hash_str)) continue;

				if (local_fav_hashes != NULL && hash_set_contains(local_fav_hashes, hash_str)) {
					dups_skipped++;
					continue;
				}

				char *down_buf = NULL;
				DWORD down_size = 0;
				TCHAR item_err[BUF_SIZE] = { 0 };
				if (yandex_download_file(token, res_item->path, &down_buf, &down_size, item_err)) {
					if (down_buf != NULL && down_size > 0) {
						DATA_INFO *new_clip = create_text_clip(down_buf, down_size, &st);
						if (new_clip != NULL) {
							new_clip->next = fav_root;
							fav_root = new_clip;
							if (local_fav_hashes != NULL) hash_set_add(local_fav_hashes, hash_str);
							fav_imported++;
						} else {
							fav_failed++;
						}
						free(down_buf);
					}
				} else {
					fav_failed++;
					lstrcpy(last_error, item_err);
				}
			}
		}
		yandex_free_resource_list(&fav_subfolders);
	}

	// Save updated favourites to disk
	if (fav_imported > 0) {
		file_write_data(regist_path, fav_root, FALSE, err_str);
	}
	if (fav_root != NULL) {
		data_free(fav_root);
	}

	// 5. Import History from cloud
	SendMessage(hDlg, WM_CLOUD_PROGRESS, 0, (LPARAM)TEXT("Scanning cloud history..."));
	ZeroMemory(&hist_files, sizeof(hist_files));
	if (yandex_list_resources(token, batch_hist_folder, &hist_files, NULL)) {
		int total_hist_files = 0;
		int current_hist = 0;
		for (res_item = hist_files.head; res_item != NULL; res_item = res_item->next) {
			if (!res_item->is_dir) total_hist_files++;
		}

		for (res_item = hist_files.head; res_item != NULL; res_item = res_item->next) {
			SYSTEMTIME st;
			TCHAR hash_str[CLOUD_HASH_STR_SIZE];
			TCHAR prog_msg[128];

			if (res_item->is_dir) continue;
			if (!cloud_parse_filename(res_item->name, &st, hash_str)) continue;

			current_hist++;
			wsprintf(prog_msg, TEXT("Importing %d of %d..."), current_hist, total_hist_files);
			SendMessage(hDlg, WM_CLOUD_PROGRESS, 0, (LPARAM)prog_msg);

			if (local_hist_hashes != NULL && hash_set_contains(local_hist_hashes, hash_str)) {
				dups_skipped++;
				continue;
			}

			char *down_buf = NULL;
			DWORD down_size = 0;
			TCHAR item_err[BUF_SIZE] = { 0 };
			if (yandex_download_file(token, res_item->path, &down_buf, &down_size, item_err)) {
				if (down_buf != NULL && down_size > 0 && db != NULL) {
					if (db_insert_text_clip(db, down_buf, down_size, &st, hash_str, item_err, BUF_SIZE)) {
						if (local_hist_hashes != NULL) hash_set_add(local_hist_hashes, hash_str);
						hist_imported++;
					} else {
						hist_failed++;
						lstrcpy(last_error, item_err[0] != TEXT('\0') ? item_err : TEXT("Failed to insert clip into local database."));
					}
					free(down_buf);
				} else if (db == NULL) {
					hist_failed++;
					lstrcpy(last_error, TEXT("Local database is not open."));
					free(down_buf);
				}
			} else {
				hist_failed++;
				lstrcpy(last_error, item_err);
			}
		}
		yandex_free_resource_list(&hist_files);
	}

	if (db != NULL) {
		sqlite3_close(db);
		db = NULL;
	}

	if (local_fav_hashes != NULL) {
		hash_set_free(local_fav_hashes);
		local_fav_hashes = NULL;
	}
	if (local_hist_hashes != NULL) {
		hash_set_free(local_hist_hashes);
		local_hist_hashes = NULL;
	}

	// 6. Refresh running CLCL instance
	hClcl = FindWindow(MAIN_WND_CLASS, MAIN_WINDOW_TITLE);
	if (hClcl != NULL) {
		SendMessage(hClcl, WM_HISTORY_LOAD, 1, 0);
		SendMessage(hClcl, WM_REGIST_LOAD, 0, 0);
		SendMessage(hClcl, WM_OPTION_LOAD, 0, 0);
		SendMessage(hClcl, WM_REGIST_CHANGED, 0, 0);
		SendMessage(hClcl, WM_HISTORY_CHANGED, 0, 0);
	}

	if (fav_failed > 0 || hist_failed > 0) {
		_sntprintf(status_msg, BUF_SIZE,
			TEXT("Cloud import completed with errors!\r\n\r\n")
			TEXT("• History items added: %d\r\n")
			TEXT("• Favourites added: %d\r\n")
			TEXT("• Duplicates skipped: %d\r\n")
			TEXT("• Download failures: %d\r\n\r\n")
			TEXT("Error: %s"),
			hist_imported, fav_imported, dups_skipped,
			fav_failed + hist_failed,
			last_error[0] != TEXT('\0') ? last_error : TEXT("Failed to download items from cloud."));
	} else {
		_sntprintf(status_msg, BUF_SIZE,
			TEXT("Cloud import completed successfully!\r\n\r\n")
			TEXT("• History items added: %d\r\n")
			TEXT("• Favourites added: %d\r\n")
			TEXT("• Duplicates skipped: %d"),
			hist_imported, fav_imported, dups_skipped);
	}

	TCHAR *res_msg = alloc_copy(status_msg);
	PostMessage(hDlg, WM_CLOUD_COMPLETE, (fav_failed + hist_failed == 0), (LPARAM)res_msg);
	free(params);
	return 0;
}

/*
 * cloud_delete_worker - Thread procedure for Delete Cloud Data
 */
static DWORD WINAPI cloud_delete_worker(LPVOID lpParam)
{
	CLOUD_WORKER_PARAMS *params = (CLOUD_WORKER_PARAMS *)lpParam;
	HWND hDlg = params->hDlg;
	const TCHAR *token = params->token;
	TCHAR err_str[BUF_SIZE] = { 0 };
	TCHAR status_msg[BUF_SIZE];
	BOOL success = FALSE;

	SendMessage(hDlg, WM_CLOUD_PROGRESS, 0, (LPARAM)TEXT("Deleting cloud data (disk:/CLCL)..."));
	if (yandex_delete_resource(token, YANDEX_ROOT_FOLDER, err_str)) {
		Sleep(400);
		yandex_ensure_folder(token, YANDEX_ROOT_FOLDER, NULL);
		yandex_ensure_folder(token, YANDEX_HISTORY_FOLDER, NULL);
		yandex_ensure_folder(token, YANDEX_FAVOURITES_FOLDER, NULL);
		lstrcpy(status_msg, TEXT("All CLCL cloud data (disk:/CLCL) was successfully deleted."));
		success = TRUE;
	} else {
		_sntprintf(status_msg, BUF_SIZE,
			TEXT("Failed to delete cloud data:\r\n%s"),
			err_str[0] != TEXT('\0') ? err_str : TEXT("Unknown error."));
		success = FALSE;
	}

	TCHAR *res_msg = alloc_copy(status_msg);
	PostMessage(hDlg, WM_CLOUD_COMPLETE, success, (LPARAM)res_msg);
	free(params);
	return 0;
}

/*
 * update_cloud_ui_state - Enable or disable controls depending on checkbox and running state
 */
static void update_cloud_ui_state(HWND hDlg, BOOL is_running)
{
	BOOL enabled = IsDlgButtonChecked(hDlg, IDC_CHECK_CLOUD_ENABLE) && !is_running;
	BOOL has_token = (option.cloud_token[0] != TEXT('\0'));

	EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_AUTH), enabled);
	EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_DISCONNECT), enabled && has_token);
	EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_SAVE_CLOUD), enabled && has_token);
	EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_IMPORT_CLOUD), enabled && has_token);
	EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_DELETE_CLOUD), enabled && has_token);
	EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_CLEAR_HISTORY), !is_running);
}

/*
 * token_input_dlg_proc - Modal dialog procedure for entering / pasting verification code or token
 */
static INT_PTR CALLBACK token_input_dlg_proc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	static TCHAR *out_token = NULL;
	HANDLE hData;
	const WCHAR *wclip;
	WCHAR token_buf[512];
	int tlen;

	switch (uMsg) {
	case WM_INITDIALOG:
		dark_mode_set_dialog(hDlg);
		out_token = (TCHAR *)lParam;

		// Check if clipboard already has a token/code
		if (OpenClipboard(hDlg)) {
			hData = GetClipboardData(CF_UNICODETEXT);
			if (hData != NULL) {
				wclip = (const WCHAR *)GlobalLock(hData);
				if (wclip != NULL) {
					while (*wclip == L' ' || *wclip == L'\t' || *wclip == L'\r' || *wclip == L'\n') wclip++;
					if (wmemcmp(wclip, L"y0_", 3) == 0 || (lstrlenW(wclip) >= 7 && lstrlenW(wclip) <= 128 && wcschr(wclip, L' ') == NULL)) {
						lstrcpynW(token_buf, wclip, 256);
						tlen = lstrlenW(token_buf);
						while (tlen > 0 && (token_buf[tlen - 1] == L' ' || token_buf[tlen - 1] == L'\t' || token_buf[tlen - 1] == L'\r' || token_buf[tlen - 1] == L'\n')) {
							token_buf[--tlen] = L'\0';
						}
						SetDlgItemText(hDlg, IDC_EDIT_TOKEN, token_buf);
					}
					GlobalUnlock(hData);
				}
			}
			CloseClipboard();
		}
		SendDlgItemMessage(hDlg, IDC_EDIT_TOKEN, EM_SETSEL, 0, -1);
		SetFocus(GetDlgItem(hDlg, IDC_EDIT_TOKEN));
		return FALSE;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDC_BUTTON_PASTE_TOKEN:
			if (OpenClipboard(hDlg)) {
				hData = GetClipboardData(CF_UNICODETEXT);
				if (hData != NULL) {
					wclip = (const WCHAR *)GlobalLock(hData);
					if (wclip != NULL) {
						while (*wclip == L' ' || *wclip == L'\t' || *wclip == L'\r' || *wclip == L'\n') wclip++;
						lstrcpynW(token_buf, wclip, 512);
						tlen = lstrlenW(token_buf);
						while (tlen > 0 && (token_buf[tlen - 1] == L' ' || token_buf[tlen - 1] == L'\t' || token_buf[tlen - 1] == L'\r' || token_buf[tlen - 1] == L'\n')) {
							token_buf[--tlen] = L'\0';
						}
						SetDlgItemText(hDlg, IDC_EDIT_TOKEN, token_buf);
						GlobalUnlock(hData);
					}
				}
				CloseClipboard();
			}
			break;

		case IDOK:
			{
				TCHAR input_str[512] = { 0 };
				TCHAR resolved_token[512] = { 0 };
				TCHAR user_name[128] = { 0 };
				TCHAR err_str[BUF_SIZE] = { 0 };
				TCHAR *p;

				GetDlgItemText(hDlg, IDC_EDIT_TOKEN, input_str, 512);
				p = input_str;
				while (*p == TEXT(' ') || *p == TEXT('\t') || *p == TEXT('\r') || *p == TEXT('\n')) p++;
				tlen = lstrlen(p);
				while (tlen > 0 && (p[tlen - 1] == TEXT(' ') || p[tlen - 1] == TEXT('\t') || p[tlen - 1] == TEXT('\r') || p[tlen - 1] == TEXT('\n'))) {
					p[--tlen] = TEXT('\0');
				}

				if (*p == TEXT('\0')) {
					MessageBox(hDlg, TEXT("Please enter or paste the verification code / token."),
						TEXT("Yandex Authorization"), MB_OK | MB_ICONWARNING);
					return TRUE;
				}

				// If input looks like direct access token (starts with y0_ or length > 20)
				if (_tcsncmp(p, TEXT("y0_"), 3) == 0 || lstrlen(p) > 20) {
					lstrcpy(resolved_token, p);
				} else {
					// Exchange code for access token using client secret
					if (!yandex_exchange_code_for_token(p, resolved_token, 512, err_str)) {
						// If code exchange failed, try direct token test just in case
						lstrcpy(resolved_token, p);
					}
				}

				// Verify token by testing connection
				if (!yandex_test_connection(resolved_token, user_name, sizeof(user_name) / sizeof(TCHAR), err_str)) {
					// If failed and not tried code exchange yet, try exchange as fallback
					if (_tcsncmp(p, TEXT("y0_"), 3) != 0 && yandex_exchange_code_for_token(p, resolved_token, 512, NULL)) {
						if (yandex_test_connection(resolved_token, user_name, sizeof(user_name) / sizeof(TCHAR), err_str)) {
							goto auth_success;
						}
					}
					MessageBox(hDlg, err_str, TEXT("Authorization Failed"), MB_OK | MB_ICONERROR);
					return TRUE;
				}

auth_success:
				if (out_token != NULL) {
					lstrcpy(out_token, resolved_token);
				}
				EndDialog(hDlg, IDOK);
				return TRUE;
			}

		case IDCANCEL:
			EndDialog(hDlg, IDCANCEL);
			return TRUE;
		}
		break;
	}
	return FALSE;
}

typedef struct _CLOUD_BATCH_DLG_PARAMS {
	const TCHAR *token;
	TCHAR *selected_batch;
	DWORD batch_max_len;
} CLOUD_BATCH_DLG_PARAMS;

static int __cdecl batch_sort_desc(const void *a, const void *b)
{
	const TCHAR *sa = (const TCHAR *)a;
	const TCHAR *sb = (const TCHAR *)b;
	return _tcscmp(sb, sa);
}

static void populate_batches_list(HWND hDlg, const TCHAR *token)
{
	HWND hList = GetDlgItem(hDlg, IDC_LIST_BATCHES);
	YANDEX_RESOURCE_LIST root_items;
	YANDEX_RESOURCE_ITEM *res_item;
	TCHAR err_str[BUF_SIZE] = { 0 };
	HCURSOR hOldCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
	TCHAR batch_names[256][MAX_PATH];
	int batch_count = 0;
	BOOL has_legacy = FALSE;
	int i;

	SendMessage(hList, LB_RESETCONTENT, 0, 0);
	EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_IMPORT_BATCH), FALSE);
	EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_DELETE_BATCH), FALSE);
	SetDlgItemText(hDlg, IDC_STATIC_BATCH_STATUS, TEXT("Scanning cloud backups..."));

	ZeroMemory(&root_items, sizeof(root_items));
	if (yandex_list_resources(token, YANDEX_ROOT_FOLDER, &root_items, err_str)) {
		for (res_item = root_items.head; res_item != NULL; res_item = res_item->next) {
			if (res_item->is_dir) {
				if (lstrcmpi(res_item->name, TEXT("History")) == 0 ||
					lstrcmpi(res_item->name, TEXT("Favourites")) == 0) {
					has_legacy = TRUE;
				} else if (batch_count < 256) {
					lstrcpyn(batch_names[batch_count], res_item->name, MAX_PATH);
					batch_count++;
				}
			}
		}
		yandex_free_resource_list(&root_items);
	}

	// Sort batches descending (newest first)
	if (batch_count > 1) {
		qsort(batch_names, batch_count, sizeof(batch_names[0]), batch_sort_desc);
	}

	for (i = 0; i < batch_count; i++) {
		SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)batch_names[i]);
	}

	if (has_legacy) {
		SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)TEXT("[Legacy Backup]"));
	}

	int total_items = (int)SendMessage(hList, LB_GETCOUNT, 0, 0);
	if (total_items > 0) {
		SendMessage(hList, LB_SETCURSEL, 0, 0);
		EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_IMPORT_BATCH), TRUE);
		EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_DELETE_BATCH), TRUE);

		TCHAR status_txt[128];
		_sntprintf(status_txt, 128, TEXT("Found %d backup(s) in cloud."), total_items);
		SetDlgItemText(hDlg, IDC_STATIC_BATCH_STATUS, status_txt);
	} else {
		SetDlgItemText(hDlg, IDC_STATIC_BATCH_STATUS, TEXT("No backups found in cloud."));
	}

	SetCursor(hOldCursor);
}

static INT_PTR CALLBACK cloud_batches_dlg_proc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	static CLOUD_BATCH_DLG_PARAMS *dlg_params = NULL;

	switch (uMsg) {
	case WM_INITDIALOG:
		dark_mode_set_dialog(hDlg);
		dlg_params = (CLOUD_BATCH_DLG_PARAMS *)lParam;
		if (dlg_params != NULL && dlg_params->token != NULL) {
			populate_batches_list(hDlg, dlg_params->token);
		}
		return TRUE;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDC_LIST_BATCHES:
			if (HIWORD(wParam) == LBN_SELCHANGE) {
				int sel = (int)SendDlgItemMessage(hDlg, IDC_LIST_BATCHES, LB_GETCURSEL, 0, 0);
				BOOL has_sel = (sel != LB_ERR);
				EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_IMPORT_BATCH), has_sel);
				EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_DELETE_BATCH), has_sel);
			} else if (HIWORD(wParam) == LBN_DBLCLK) {
				PostMessage(hDlg, WM_COMMAND, MAKEWPARAM(IDC_BUTTON_IMPORT_BATCH, BN_CLICKED), 0);
			}
			break;

		case IDC_BUTTON_IMPORT_BATCH:
		case IDOK:
			{
				int sel = (int)SendDlgItemMessage(hDlg, IDC_LIST_BATCHES, LB_GETCURSEL, 0, 0);
				if (sel != LB_ERR && dlg_params != NULL && dlg_params->selected_batch != NULL) {
					TCHAR sel_name[MAX_PATH];
					SendDlgItemMessage(hDlg, IDC_LIST_BATCHES, LB_GETTEXT, sel, (LPARAM)sel_name);
					if (lstrcmp(sel_name, TEXT("[Legacy Backup]")) == 0) {
						_sntprintf(dlg_params->selected_batch, dlg_params->batch_max_len, TEXT("%s"), YANDEX_ROOT_FOLDER);
					} else {
						_sntprintf(dlg_params->selected_batch, dlg_params->batch_max_len, TEXT("%s/%s"), YANDEX_ROOT_FOLDER, sel_name);
					}
					EndDialog(hDlg, IDOK);
					return TRUE;
				}
			}
			break;

		case IDC_BUTTON_DELETE_BATCH:
			{
				int sel = (int)SendDlgItemMessage(hDlg, IDC_LIST_BATCHES, LB_GETCURSEL, 0, 0);
				if (sel != LB_ERR && dlg_params != NULL && dlg_params->token != NULL) {
					TCHAR sel_name[MAX_PATH];
					TCHAR confirm_msg[512];
					TCHAR full_batch_path[512];
					TCHAR err_str[BUF_SIZE] = { 0 };

					SendDlgItemMessage(hDlg, IDC_LIST_BATCHES, LB_GETTEXT, sel, (LPARAM)sel_name);
					_sntprintf(confirm_msg, 512,
						TEXT("Are you sure you want to delete backup '%s' from Yandex Disk?\r\n\r\nThis cannot be undone."),
						sel_name);
					if (MessageBox(hDlg, confirm_msg, TEXT("Delete Cloud Backup"), MB_YESNO | MB_ICONWARNING) == IDYES) {
						HCURSOR hOldCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
						SetDlgItemText(hDlg, IDC_STATIC_BATCH_STATUS, TEXT("Deleting backup..."));

						if (lstrcmp(sel_name, TEXT("[Legacy Backup]")) == 0) {
							yandex_delete_resource(dlg_params->token, YANDEX_HISTORY_FOLDER, NULL);
							yandex_delete_resource(dlg_params->token, YANDEX_FAVOURITES_FOLDER, NULL);
						} else {
							_sntprintf(full_batch_path, 512, TEXT("%s/%s"), YANDEX_ROOT_FOLDER, sel_name);
							yandex_delete_resource(dlg_params->token, full_batch_path, err_str);
						}

						SetCursor(hOldCursor);
						populate_batches_list(hDlg, dlg_params->token);
						SetDlgItemText(hDlg, IDC_STATIC_BATCH_STATUS, TEXT("Backup deleted successfully."));
					}
				}
			}
			break;

		case IDC_BUTTON_REFRESH_BATCHES:
			if (dlg_params != NULL && dlg_params->token != NULL) {
				populate_batches_list(hDlg, dlg_params->token);
			}
			break;

		case IDCANCEL:
			EndDialog(hDlg, IDCANCEL);
			return TRUE;
		}
		break;
	}
	return FALSE;
}

static BOOL clear_local_history(HWND hDlg)
{
	TCHAR db_path[MAX_PATH];
	TCHAR dat_path[MAX_PATH];
	sqlite3 *db = NULL;
	HWND hClcl;
	BOOL success = TRUE;

	wsprintf(db_path, TEXT("%s\\history.db"), work_path);
	if (sqlite3_open16(db_path, &db) == SQLITE_OK && db != NULL) {
		sqlite3_busy_timeout(db, 5000);
		sqlite3_exec(db, "DELETE FROM items;", NULL, NULL, NULL);
		sqlite3_exec(db, "DELETE FROM item_formats;", NULL, NULL, NULL);
		sqlite3_exec(db, "DELETE FROM history_fts;", NULL, NULL, NULL);
		sqlite3_exec(db, "VACUUM;", NULL, NULL, NULL);
		sqlite3_close(db);
	} else {
		success = FALSE;
	}

	wsprintf(dat_path, TEXT("%s\\%s"), work_path, HISTORY_FILENAME);
	if (GetFileAttributes(dat_path) != INVALID_FILE_ATTRIBUTES) {
		DeleteFile(dat_path);
	}

	hClcl = FindWindow(MAIN_WND_CLASS, MAIN_WINDOW_TITLE);
	if (hClcl != NULL) {
		SendMessage(hClcl, WM_HISTORY_LOAD, 1, 0);
		SendMessage(hClcl, WM_HISTORY_CHANGED, 0, 0);
	}

	return success;
}

/*
 * set_cloud_proc - Dialog procedure for Cloud Options tab
 */
BOOL CALLBACK set_cloud_proc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg) {
	case WM_INITDIALOG:
		dark_mode_set_dialog(hDlg);

		CheckDlgButton(hDlg, IDC_CHECK_CLOUD_ENABLE, option.cloud_enable);

		lstrcpy(option.cloud_client_id, YANDEX_DEFAULT_CLIENT_ID);

		if (option.cloud_token[0] != TEXT('\0')) {
			SetDlgItemText(hDlg, IDC_STATIC_CLOUD_STATUS, TEXT("Status: Connected"));
		} else {
			SetDlgItemText(hDlg, IDC_STATIC_CLOUD_STATUS, TEXT("Status: Not connected"));
		}

		update_cloud_ui_state(hDlg, FALSE);
		break;

	case WM_NOTIFY:
		return OptionNotifyProc(hDlg, uMsg, wParam, lParam);

	case WM_CLOUD_PROGRESS:
		SetDlgItemText(hDlg, IDC_STATIC_CLOUD_INFO, (const TCHAR *)lParam);
		UpdateWindow(GetDlgItem(hDlg, IDC_STATIC_CLOUD_INFO));
		break;

	case WM_CLOUD_COMPLETE:
		{
			BOOL success = (BOOL)wParam;
			TCHAR *msg = (TCHAR *)lParam;

			update_cloud_ui_state(hDlg, FALSE);
			SetDlgItemText(hDlg, IDC_STATIC_CLOUD_INFO, success ? TEXT("Ready.") : TEXT("Operation failed."));

			if (msg != NULL) {
				MessageBox(hDlg, msg, TEXT("CLCL Cloud Sync"),
					success ? (MB_OK | MB_ICONINFORMATION) : (MB_OK | MB_ICONERROR));
				mem_free((void **)&msg);
			}

			if (hWorkerThread != NULL) {
				CloseHandle(hWorkerThread);
				hWorkerThread = NULL;
			}
		}
		break;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDC_CHECK_CLOUD_ENABLE:
			update_cloud_ui_state(hDlg, FALSE);
			break;

		case IDC_BUTTON_AUTH:
			{
				TCHAR auth_url[512];
				TCHAR entered_token[512] = { 0 };
				TCHAR ini_path[MAX_PATH];
				TCHAR user_name[128] = { 0 };
				TCHAR status_text[256];

				// Launch default browser to Yandex OAuth authorization
				_sntprintf(auth_url, 512, YANDEX_AUTH_URL_TEMPLATE, YANDEX_DEFAULT_CLIENT_ID);
				ShellExecute(hDlg, TEXT("open"), auth_url, NULL, NULL, SW_SHOWNORMAL);

				SetDlgItemText(hDlg, IDC_STATIC_CLOUD_INFO,
					TEXT("Browser opened. Copy the verification code / token and paste it into the prompt."));

				// Open token input dialog
				if (DialogBoxParam(hInst,
					MAKEINTRESOURCE(IDD_DIALOG_TOKEN_INPUT),
					hDlg, token_input_dlg_proc, (LPARAM)entered_token) == IDOK) {
					if (entered_token[0] != TEXT('\0')) {
						lstrcpy(option.cloud_token, entered_token);

						// Save token immediately via DPAPI into clcl.ini
						wsprintf(ini_path, TEXT("%s\\%s"), work_path, USER_INI);
						cloud_save_token(ini_path, option.cloud_token);

						if (yandex_test_connection(option.cloud_token, user_name, 128, NULL)) {
							_sntprintf(status_text, 256, TEXT("Status: Connected (%s)"), user_name);
						} else {
							lstrcpy(status_text, TEXT("Status: Connected"));
						}
						SetDlgItemText(hDlg, IDC_STATIC_CLOUD_STATUS, status_text);
						SetDlgItemText(hDlg, IDC_STATIC_CLOUD_INFO, TEXT("Connected successfully."));
						update_cloud_ui_state(hDlg, FALSE);
					}
				}
			}
			break;

		case IDC_BUTTON_DISCONNECT:
			{
				TCHAR ini_path[MAX_PATH];
				option.cloud_token[0] = TEXT('\0');
				wsprintf(ini_path, TEXT("%s\\%s"), work_path, USER_INI);
				cloud_save_token(ini_path, option.cloud_token);

				SetDlgItemText(hDlg, IDC_STATIC_CLOUD_STATUS, TEXT("Status: Not connected"));
				SetDlgItemText(hDlg, IDC_STATIC_CLOUD_INFO, TEXT("Disconnected."));
				update_cloud_ui_state(hDlg, FALSE);
			}
			break;

		case IDC_BUTTON_SAVE_CLOUD:
			{
				CLOUD_WORKER_PARAMS *params;

				if (option.cloud_token[0] == TEXT('\0')) {
					MessageBox(hDlg, TEXT("Please authorize with Yandex first."),
						TEXT("CLCL Cloud"), MB_OK | MB_ICONWARNING);
					break;
				}

				if (MessageBox(hDlg,
					TEXT("Save current history and favourite items as a new backup batch in cloud (disk:/CLCL/YYYY-MM-DD HH:MM:SS)?\r\n\r\nExisting backups will not be deleted."),
					TEXT("Save Backup to Cloud"), MB_YESNO | MB_ICONQUESTION) != IDYES) {
					break;
				}

				update_cloud_ui_state(hDlg, TRUE);
				SetDlgItemText(hDlg, IDC_STATIC_CLOUD_INFO, TEXT("Starting cloud save..."));

				params = (CLOUD_WORKER_PARAMS *)malloc(sizeof(CLOUD_WORKER_PARAMS));
				if (params != NULL) {
					params->hDlg = hDlg;
					lstrcpyn(params->token, option.cloud_token, 512);
					params->batch_path[0] = TEXT('\0');
					params->is_save = TRUE;
					hWorkerThread = CreateThread(NULL, 0, cloud_save_worker, params, 0, NULL);
				}
			}
			break;

		case IDC_BUTTON_IMPORT_CLOUD:
			{
				if (option.cloud_token[0] == TEXT('\0')) {
					MessageBox(hDlg, TEXT("Please authorize with Yandex first."),
						TEXT("CLCL Cloud"), MB_OK | MB_ICONWARNING);
					break;
				}

				TCHAR selected_batch[512] = { 0 };
				CLOUD_BATCH_DLG_PARAMS dlg_params;
				dlg_params.token = option.cloud_token;
				dlg_params.selected_batch = selected_batch;
				dlg_params.batch_max_len = 512;

				if (DialogBoxParam(hInst,
					MAKEINTRESOURCE(IDD_DIALOG_CLOUD_BATCHES),
					hDlg, cloud_batches_dlg_proc, (LPARAM)&dlg_params) == IDOK) {
					if (selected_batch[0] != TEXT('\0')) {
						update_cloud_ui_state(hDlg, TRUE);
						SetDlgItemText(hDlg, IDC_STATIC_CLOUD_INFO, TEXT("Starting cloud import..."));

						CLOUD_WORKER_PARAMS *params = (CLOUD_WORKER_PARAMS *)malloc(sizeof(CLOUD_WORKER_PARAMS));
						if (params != NULL) {
							params->hDlg = hDlg;
							lstrcpyn(params->token, option.cloud_token, 512);
							lstrcpyn(params->batch_path, selected_batch, 512);
							params->is_save = FALSE;
							hWorkerThread = CreateThread(NULL, 0, cloud_import_worker, params, 0, NULL);
						}
					}
				}
			}
			break;

		case IDC_BUTTON_DELETE_CLOUD:
			{
				CLOUD_WORKER_PARAMS *params;

				if (option.cloud_token[0] == TEXT('\0')) {
					MessageBox(hDlg, TEXT("Please authorize with Yandex first."),
						TEXT("CLCL Cloud"), MB_OK | MB_ICONWARNING);
					break;
				}

				if (MessageBox(hDlg,
					TEXT("Are you sure you want to delete all CLCL data from Yandex Disk (disk:/CLCL)?\r\n\r\nThis cannot be undone."),
					TEXT("Delete Cloud Data"), MB_YESNO | MB_ICONWARNING) != IDYES) {
					break;
				}

				update_cloud_ui_state(hDlg, TRUE);
				SetDlgItemText(hDlg, IDC_STATIC_CLOUD_INFO, TEXT("Starting cloud deletion..."));

				params = (CLOUD_WORKER_PARAMS *)malloc(sizeof(CLOUD_WORKER_PARAMS));
				if (params != NULL) {
					params->hDlg = hDlg;
					lstrcpyn(params->token, option.cloud_token, 512);
					params->batch_path[0] = TEXT('\0');
					params->is_save = FALSE;
					hWorkerThread = CreateThread(NULL, 0, cloud_delete_worker, params, 0, NULL);
				}
			}
			break;

		case IDC_BUTTON_CLEAR_HISTORY:
			{
				if (MessageBox(hDlg,
					TEXT("Are you sure you want to clear all local history?\r\n\r\n")
					TEXT("• All history items will be deleted from the database.\r\n")
					TEXT("• All Favourites will be strictly preserved.\r\n\r\n")
					TEXT("Do you want to proceed?"),
					TEXT("Clear Local History"),
					MB_YESNO | MB_ICONQUESTION) != IDYES) {
					break;
				}

				if (clear_local_history(hDlg)) {
					MessageBox(hDlg,
						TEXT("Local history has been cleared successfully.\r\n\r\nFavourites were preserved."),
						TEXT("CLCL History"), MB_OK | MB_ICONINFORMATION);
					SetDlgItemText(hDlg, IDC_STATIC_CLOUD_INFO, TEXT("Local history cleared."));
				} else {
					MessageBox(hDlg,
						TEXT("Failed to clear local history database."),
						TEXT("CLCL History"), MB_OK | MB_ICONERROR);
				}
			}
			break;

		case IDOK:
			option.cloud_enable = IsDlgButtonChecked(hDlg, IDC_CHECK_CLOUD_ENABLE);
			lstrcpy(option.cloud_client_id, YANDEX_DEFAULT_CLIENT_ID);
			break;
		}
		break;
	}
	return FALSE;
}
