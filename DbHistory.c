/*
 * CLCL
 *
 * DbHistory.c - SQLite-backed history storage & fast indexed search
 */

#define _INC_OLE
#include <windows.h>
#undef _INC_OLE
#include <tchar.h>
#include <shlwapi.h>
#include <winsqlite/winsqlite3.h>

#pragma comment(lib, "winsqlite3.lib")
#pragma comment(lib, "shlwapi.lib")

#include "General.h"
#include "Memory.h"
#include "String.h"
#include "Data.h"
#include "Format.h"
#include "ClipBoard.h"
#include "File.h"
#include "Ini.h"
#include "DbHistory.h"

static sqlite3 *db = NULL;
extern DATA_INFO history_data;

/*
 * extract_text_for_index - extract text content from item for indexing
 */
static TCHAR *extract_text_for_index(const DATA_INFO *item)
{
	const DATA_INFO *cdi;

	if (item == NULL) return NULL;

	// First look for UNICODE TEXT
	for (cdi = item->child; cdi != NULL; cdi = cdi->next) {
		if (cdi->data == NULL) continue;
		if (cdi->format == CF_UNICODETEXT || (cdi->format_name != NULL && lstrcmpi(cdi->format_name, TEXT("UNICODE TEXT")) == 0)) {
			const WCHAR *w = (const WCHAR *)GlobalLock(cdi->data);
			if (w != NULL) {
				TCHAR *res = alloc_copy(w);
				GlobalUnlock(cdi->data);
				return res;
			}
		}
	}
	// Fall back to CF_TEXT / CF_OEMTEXT
	for (cdi = item->child; cdi != NULL; cdi = cdi->next) {
		if (cdi->data == NULL) continue;
		if (cdi->format == CF_TEXT || (cdi->format_name != NULL && lstrcmpi(cdi->format_name, TEXT("TEXT")) == 0)) {
			const char *a = (const char *)GlobalLock(cdi->data);
			if (a != NULL) {
				TCHAR *res = alloc_char_to_tchar(a);
				GlobalUnlock(cdi->data);
				return res;
			}
		}
	}
	return NULL;
}

/*
 * db_history_init - open or create SQLite database
 */
BOOL db_history_init(const TCHAR *work_path)
{
	TCHAR db_path[MAX_PATH];
	int rc;

	if (db != NULL) {
		return TRUE;
	}

	wsprintf(db_path, TEXT("%s\\history.db"), work_path);

	rc = sqlite3_open16(db_path, &db);
	if (rc != SQLITE_OK || db == NULL) {
		if (db != NULL) {
			sqlite3_close(db);
			db = NULL;
		}
		return FALSE;
	}

	// Optimize for desktop app WAL performance
	sqlite3_exec(db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
	sqlite3_exec(db, "PRAGMA synchronous=NORMAL;", NULL, NULL, NULL);
	sqlite3_exec(db, "PRAGMA temp_store=MEMORY;", NULL, NULL, NULL);
	sqlite3_exec(db, "PRAGMA cache_size=-4000;", NULL, NULL, NULL);

	// Schema initialization
	const char *schema_sql =
		"CREATE TABLE IF NOT EXISTS items ("
		"  id INTEGER PRIMARY KEY AUTOINCREMENT,"
		"  created_at INTEGER NOT NULL,"
		"  window_title TEXT,"
		"  title TEXT NOT NULL,"
		"  pinned INTEGER DEFAULT 0,"
		"  total_size INTEGER NOT NULL,"
		"  text_content TEXT"
		");"
		"CREATE INDEX IF NOT EXISTS idx_items_created ON items(created_at DESC);"
		"CREATE TABLE IF NOT EXISTS item_formats ("
		"  item_id INTEGER NOT NULL,"
		"  format_name TEXT NOT NULL,"
		"  format_id INTEGER NOT NULL,"
		"  data_size INTEGER NOT NULL,"
		"  data BLOB,"
		"  PRIMARY KEY (item_id, format_name)"
		");"
		"CREATE VIRTUAL TABLE IF NOT EXISTS history_fts USING fts4("
		"  title,"
		"  text_content,"
		"  window_title,"
		"  tokenize=unicode61"
		");";

	rc = sqlite3_exec(db, schema_sql, NULL, NULL, NULL);
	if (rc != SQLITE_OK) {
		return FALSE;
	}

	// Upgrade existing database if content_hash column is missing
	sqlite3_exec(db, "ALTER TABLE items ADD COLUMN content_hash INTEGER;", NULL, NULL, NULL);
	sqlite3_exec(db, "CREATE INDEX IF NOT EXISTS idx_items_hash ON items(content_hash);", NULL, NULL, NULL);

	// Deduplicate items by content_hash
	sqlite3_exec(db,
		"DELETE FROM items WHERE content_hash IS NOT NULL AND content_hash != 0 AND id NOT IN ("
		"  SELECT MAX(id) FROM items WHERE content_hash IS NOT NULL AND content_hash != 0 GROUP BY content_hash"
		");", NULL, NULL, NULL);
	// Deduplicate legacy duplicate items
	sqlite3_exec(db,
		"DELETE FROM items WHERE text_content IS NOT NULL AND text_content != '' AND id NOT IN ("
		"  SELECT MAX(id) FROM items WHERE text_content IS NOT NULL AND text_content != '' GROUP BY title, text_content"
		");", NULL, NULL, NULL);
	// Delete any corrupt/empty bitmap items with 0 data size
	sqlite3_exec(db,
		"DELETE FROM items WHERE id IN ("
		"  SELECT i.id FROM items i "
		"  JOIN item_formats f ON i.id = f.item_id "
		"  WHERE f.format_name = 'BITMAP' AND (f.data IS NULL OR f.data_size = 0)"
		");", NULL, NULL, NULL);
	sqlite3_exec(db, "DELETE FROM items WHERE id NOT IN (SELECT DISTINCT item_id FROM item_formats);", NULL, NULL, NULL);
	sqlite3_exec(db, "DELETE FROM item_formats WHERE item_id NOT IN (SELECT id FROM items);", NULL, NULL, NULL);
	sqlite3_exec(db, "DELETE FROM history_fts WHERE docid NOT IN (SELECT id FROM items);", NULL, NULL, NULL);

	return TRUE;
}

/*
 * db_history_close - close database connection
 */
void db_history_close(void)
{
	if (db != NULL) {
		sqlite3_close(db);
		db = NULL;
	}
}

/*
 * db_history_is_open - check if database is open
 */
BOOL db_history_is_open(void)
{
	return (db != NULL);
}

/*
 * db_history_save_item - atomically insert an item and all its formats into SQLite
 */
BOOL db_history_save_item(DATA_INFO *item)
{
	sqlite3_stmt *stmt_item = NULL;
	sqlite3_stmt *stmt_fmt = NULL;
	sqlite3_stmt *stmt_fts = NULL;
	sqlite3_int64 item_id = 0;
	sqlite3_int64 ft_created = 0;
	DWORD total_size = 0;
	DATA_INFO *cdi;
	TCHAR *text_content = NULL;
	const TCHAR *item_title = NULL;
	int rc;

	if (db == NULL || item == NULL || item->type != TYPE_ITEM) {
		return FALSE;
	}

	ft_created = (((sqlite3_int64)item->modified.dwHighDateTime) << 32) | item->modified.dwLowDateTime;
	item_title = (item->title != NULL && *item->title != TEXT('\0')) ? item->title : data_get_title(item);
	if (item_title == NULL) {
		item_title = TEXT("");
	}

	for (cdi = item->child; cdi != NULL; cdi = cdi->next) {
		if (cdi->size == 0 && cdi->data != NULL) {
			UINT fmt = cdi->format ? cdi->format : clipboard_get_format(0, cdi->format_name);
			if (fmt == CF_BITMAP || fmt == CF_DSPBITMAP) {
				BITMAP bmp;
				if (GetObject((HBITMAP)cdi->data, sizeof(BITMAP), &bmp) != 0) {
					DWORD len = (((bmp.bmWidth * bmp.bmBitsPixel) / 8) % 4)
						? ((((bmp.bmWidth * bmp.bmBitsPixel) / 8) / 4) + 1) * 4
						: (bmp.bmWidth * bmp.bmBitsPixel) / 8;
					cdi->size = sizeof(BITMAPINFOHEADER) + len * bmp.bmHeight;
				}
			}
		}
		total_size += cdi->size;
	}

	text_content = extract_text_for_index(item);

	UINT64 hash = (item->content_hash != 0) ? item->content_hash : data_calc_hash(item);
	item->content_hash = hash;

	// Check if most recent item in SQLite is identical
	sqlite3_stmt *stmt_check = NULL;
	rc = sqlite3_prepare16_v2(db,
		L"SELECT id, content_hash, title, text_content, total_size FROM items ORDER BY id DESC LIMIT 1;",
		-1, &stmt_check, NULL);
	if (rc == SQLITE_OK) {
		if (sqlite3_step(stmt_check) == SQLITE_ROW) {
			sqlite3_int64 last_id = sqlite3_column_int64(stmt_check, 0);
			sqlite3_int64 last_hash = sqlite3_column_int64(stmt_check, 1);
			const WCHAR *last_title = (const WCHAR *)sqlite3_column_text16(stmt_check, 2);
			const WCHAR *last_txt = (const WCHAR *)sqlite3_column_text16(stmt_check, 3);
			int last_size = sqlite3_column_int(stmt_check, 4);
			BOOL is_dup = FALSE;

			if (hash != 0 && last_hash != 0) {
				if ((sqlite3_int64)hash == last_hash) {
					is_dup = TRUE;
				}
			} else if (text_content != NULL && last_txt != NULL) {
				if (wcscmp((const WCHAR *)text_content, last_txt) == 0) {
					is_dup = TRUE;
				}
			}

			if (is_dup) {
				sqlite3_finalize(stmt_check);
				// Update timestamp and content_hash of existing record instead of inserting duplicate
				sqlite3_stmt *stmt_upd = NULL;
				rc = sqlite3_prepare16_v2(db,
					L"UPDATE items SET created_at = ?, content_hash = ? WHERE id = ?;",
					-1, &stmt_upd, NULL);
				if (rc == SQLITE_OK) {
					sqlite3_bind_int64(stmt_upd, 1, ft_created);
					sqlite3_bind_int64(stmt_upd, 2, (sqlite3_int64)hash);
					sqlite3_bind_int64(stmt_upd, 3, last_id);
					sqlite3_step(stmt_upd);
					sqlite3_finalize(stmt_upd);
				}
				item->param1 = (LPARAM)last_id;
				if (text_content != NULL) mem_free((void **)&text_content);
				return TRUE;
			}
		}
		sqlite3_finalize(stmt_check);
	}

	BOOL inside_tx = (sqlite3_get_autocommit(db) == 0);
	if (!inside_tx) {
		sqlite3_exec(db, "BEGIN IMMEDIATE;", NULL, NULL, NULL);
	}

	// 1. Insert into items
	rc = sqlite3_prepare16_v2(db,
		L"INSERT INTO items (created_at, window_title, title, pinned, total_size, text_content, content_hash) "
		L"VALUES (?, ?, ?, 0, ?, ?, ?);",
		-1, &stmt_item, NULL);

	if (rc != SQLITE_OK) {
		if (!inside_tx) sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
		if (text_content != NULL) mem_free((void **)&text_content);
		return FALSE;
	}

	sqlite3_bind_int64(stmt_item, 1, ft_created);
	if (item->window_name != NULL) {
		sqlite3_bind_text16(stmt_item, 2, item->window_name, -1, SQLITE_TRANSIENT);
	} else {
		sqlite3_bind_null(stmt_item, 2);
	}
	sqlite3_bind_text16(stmt_item, 3, item_title, -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(stmt_item, 4, (int)total_size);
	if (text_content != NULL) {
		sqlite3_bind_text16(stmt_item, 5, text_content, -1, SQLITE_TRANSIENT);
	} else {
		sqlite3_bind_null(stmt_item, 5);
	}
	if (hash != 0) {
		sqlite3_bind_int64(stmt_item, 6, (sqlite3_int64)hash);
	} else {
		sqlite3_bind_null(stmt_item, 6);
	}

	rc = sqlite3_step(stmt_item);
	sqlite3_finalize(stmt_item);

	if (rc != SQLITE_DONE) {
		if (!inside_tx) sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
		if (text_content != NULL) mem_free((void **)&text_content);
		return FALSE;
	}

	item_id = sqlite3_last_insert_rowid(db);
	item->param1 = (LPARAM)item_id;

	// 2. Insert into history_fts
	rc = sqlite3_prepare16_v2(db,
		L"INSERT INTO history_fts (docid, title, text_content, window_title) VALUES (?, ?, ?, ?);",
		-1, &stmt_fts, NULL);
	if (rc == SQLITE_OK) {
		sqlite3_bind_int64(stmt_fts, 1, item_id);
		sqlite3_bind_text16(stmt_fts, 2, item_title, -1, SQLITE_TRANSIENT);
		if (text_content != NULL) {
			sqlite3_bind_text16(stmt_fts, 3, text_content, -1, SQLITE_TRANSIENT);
		} else {
			sqlite3_bind_null(stmt_fts, 3);
		}
		if (item->window_name != NULL) {
			sqlite3_bind_text16(stmt_fts, 4, item->window_name, -1, SQLITE_TRANSIENT);
		} else {
			sqlite3_bind_null(stmt_fts, 4);
		}
		sqlite3_step(stmt_fts);
		sqlite3_finalize(stmt_fts);
	}
	if (text_content != NULL) mem_free((void **)&text_content);

	// 3. Insert formats into item_formats
	rc = sqlite3_prepare16_v2(db,
		L"INSERT OR REPLACE INTO item_formats (item_id, format_name, format_id, data_size, data) "
		L"VALUES (?, ?, ?, ?, ?);",
		-1, &stmt_fmt, NULL);

	if (rc == SQLITE_OK) {
		for (cdi = item->child; cdi != NULL; cdi = cdi->next) {
			BYTE *mem = NULL;
			const void *blob_ptr = NULL;
			DWORD blob_size = 0;
			BOOL free_mem = FALSE;
			BOOL unlock_data = FALSE;
			UINT fmt = cdi->format ? cdi->format : clipboard_get_format(0, cdi->format_name);

			if ((mem = format_data_to_bytes(cdi, &blob_size)) != NULL) {
				blob_ptr = mem;
				free_mem = TRUE;
			} else if (fmt == CF_BITMAP || fmt == CF_DSPBITMAP || fmt == CF_PALETTE ||
			           fmt == CF_DSPMETAFILEPICT || fmt == CF_METAFILEPICT ||
			           fmt == CF_DSPENHMETAFILE || fmt == CF_ENHMETAFILE) {
				if ((mem = clipboard_data_to_bytes(cdi, &blob_size)) != NULL) {
					blob_ptr = mem;
					free_mem = TRUE;
				}
			} else if (cdi->data != NULL) {
				blob_ptr = GlobalLock(cdi->data);
				if (blob_ptr != NULL) {
					blob_size = cdi->size;
					unlock_data = TRUE;
				} else if ((mem = clipboard_data_to_bytes(cdi, &blob_size)) != NULL) {
					blob_ptr = mem;
					free_mem = TRUE;
				}
			}

			if (cdi->size == 0 && blob_size > 0) {
				cdi->size = blob_size;
			}

			sqlite3_reset(stmt_fmt);
			sqlite3_bind_int64(stmt_fmt, 1, item_id);
			sqlite3_bind_text16(stmt_fmt, 2, cdi->format_name ? cdi->format_name : TEXT(""), -1, SQLITE_TRANSIENT);
			sqlite3_bind_int(stmt_fmt, 3, (int)fmt);
			sqlite3_bind_int(stmt_fmt, 4, (int)blob_size);
			if (blob_ptr != NULL && blob_size > 0) {
				sqlite3_bind_blob(stmt_fmt, 5, blob_ptr, (int)blob_size, SQLITE_TRANSIENT);
			} else {
				sqlite3_bind_null(stmt_fmt, 5);
			}
			sqlite3_step(stmt_fmt);

			if (unlock_data) GlobalUnlock(cdi->data);
			if (free_mem) mem_free((void **)&mem);
		}
		sqlite3_finalize(stmt_fmt);
	}

	if (!inside_tx) {
		sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
	}
	return TRUE;
}

/*
 * db_history_load_recent - load recent items metadata without loading large blobs into RAM
 */
int db_history_load_recent(const int limit, DATA_INFO **out_root)
{
	sqlite3_stmt *stmt = NULL;
	sqlite3_stmt *fmt_stmt = NULL;
	DATA_INFO *head = NULL;
	DATA_INFO *tail = NULL;
	int count = 0;
	int rc;

	if (db == NULL || out_root == NULL) {
		return 0;
	}
	*out_root = NULL;

	rc = sqlite3_prepare16_v2(db,
		L"SELECT id, created_at, window_title, title, text_content, content_hash FROM items ORDER BY created_at DESC, id DESC LIMIT ?;",
		-1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		return 0;
	}
	sqlite3_bind_int(stmt, 1, (limit > 0) ? limit : 300);

	rc = sqlite3_prepare16_v2(db,
		L"SELECT format_name, format_id, data_size FROM item_formats WHERE item_id = ?;",
		-1, &fmt_stmt, NULL);

	UINT64 last_loaded_hash = 0;
	WCHAR last_loaded_title[256] = {0};
	WCHAR *last_loaded_text = NULL;

	while (sqlite3_step(stmt) == SQLITE_ROW) {
		sqlite3_int64 id = sqlite3_column_int64(stmt, 0);
		sqlite3_int64 ft_val = sqlite3_column_int64(stmt, 1);
		const WCHAR *wnd = (const WCHAR *)sqlite3_column_text16(stmt, 2);
		const WCHAR *title = (const WCHAR *)sqlite3_column_text16(stmt, 3);
		const WCHAR *txt = (const WCHAR *)sqlite3_column_text16(stmt, 4);
		UINT64 chash = (UINT64)sqlite3_column_int64(stmt, 5);

		if (chash == 0 && txt != NULL && *txt != L'\0') {
			chash = fnv1a_64(txt, wcslen(txt) * sizeof(WCHAR), 0xCBF29CE484222325ULL);
		}

		// Skip consecutive duplicates
		if (tail != NULL) {
			if (chash != 0 && last_loaded_hash != 0) {
				if (chash == last_loaded_hash) {
					continue;
				}
			} else if (txt != NULL && last_loaded_text != NULL) {
				if (wcscmp(txt, last_loaded_text) == 0) {
					continue;
				}
			}
		}

		DATA_INFO *item = data_create_item(title, FALSE, NULL);
		if (item == NULL) continue;

		item->content_hash = chash;
		last_loaded_hash = chash;

		if (title != NULL) {
			lstrcpynW(last_loaded_title, title, 256);
		} else {
			last_loaded_title[0] = L'\0';
		}
		if (last_loaded_text != NULL) {
			mem_free((void **)&last_loaded_text);
		}
		if (txt != NULL) {
			last_loaded_text = (WCHAR *)alloc_copy((const TCHAR *)txt);
		}

		item->param1 = (LPARAM)id;
		item->param2 = 0;	// data payload not loaded into memory yet (lazy)
		item->modified.dwLowDateTime = (DWORD)(ft_val & 0xFFFFFFFF);
		item->modified.dwHighDateTime = (DWORD)(ft_val >> 32);
		if (wnd != NULL && *wnd != L'\0') {
			item->window_name = alloc_copy((const TCHAR *)wnd);
		}

		// Load format metadata (names and sizes, data=NULL)
		if (fmt_stmt != NULL) {
			DATA_INFO *last_cdi = NULL;
			sqlite3_reset(fmt_stmt);
			sqlite3_bind_int64(fmt_stmt, 1, id);
			while (sqlite3_step(fmt_stmt) == SQLITE_ROW) {
				const WCHAR *fmt_name = (const WCHAR *)sqlite3_column_text16(fmt_stmt, 0);
				int fmt_id = sqlite3_column_int(fmt_stmt, 1);
				int data_size = sqlite3_column_int(fmt_stmt, 2);

				DATA_INFO *cdi = data_create_data((UINT)fmt_id, (TCHAR *)fmt_name, NULL, (DWORD)data_size, FALSE, NULL);
				if (cdi != NULL) {
					cdi->param1 = (LPARAM)id;
					cdi->param2 = 0;
					if (last_cdi == NULL) {
						item->child = cdi;
					} else {
						last_cdi->next = cdi;
					}
					last_cdi = cdi;
				}
			}
		}

		if (head == NULL) {
			head = item;
		} else {
			tail->next = item;
		}
		tail = item;
		count++;
	}

	if (last_loaded_text != NULL) {
		mem_free((void **)&last_loaded_text);
	}

	sqlite3_finalize(stmt);
	if (fmt_stmt != NULL) {
		sqlite3_finalize(fmt_stmt);
	}

	*out_root = head;
	return count;
}

/*
 * db_history_get_item - load single item metadata by ID from SQLite
 */
DATA_INFO *db_history_get_item(const int id)
{
	sqlite3_stmt *stmt = NULL;
	sqlite3_stmt *fmt_stmt = NULL;
	DATA_INFO *item = NULL;
	int rc;

	if (db == NULL || id <= 0) {
		return NULL;
	}

	rc = sqlite3_prepare16_v2(db,
		L"SELECT created_at, window_title, title, content_hash FROM items WHERE id = ?;",
		-1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		return NULL;
	}
	sqlite3_bind_int(stmt, 1, id);

	if (sqlite3_step(stmt) == SQLITE_ROW) {
		sqlite3_int64 ft_val = sqlite3_column_int64(stmt, 0);
		const WCHAR *wnd = (const WCHAR *)sqlite3_column_text16(stmt, 1);
		const WCHAR *title = (const WCHAR *)sqlite3_column_text16(stmt, 2);
		UINT64 chash = (UINT64)sqlite3_column_int64(stmt, 3);

		item = data_create_item(title, FALSE, NULL);
		if (item != NULL) {
			item->param1 = (LPARAM)id;
			item->param2 = 0; // lazy payload
			item->content_hash = chash;
			item->modified.dwLowDateTime = (DWORD)(ft_val & 0xFFFFFFFF);
			item->modified.dwHighDateTime = (DWORD)(ft_val >> 32);
			if (wnd != NULL && *wnd != L'\0') {
				item->window_name = alloc_copy((const TCHAR *)wnd);
			}

			// Load format metadata
			rc = sqlite3_prepare16_v2(db,
				L"SELECT format_name, format_id, data_size FROM item_formats WHERE item_id = ?;",
				-1, &fmt_stmt, NULL);
			if (rc == SQLITE_OK) {
				DATA_INFO *last_cdi = NULL;
				sqlite3_bind_int(fmt_stmt, 1, id);
				while (sqlite3_step(fmt_stmt) == SQLITE_ROW) {
					const WCHAR *fmt_name = (const WCHAR *)sqlite3_column_text16(fmt_stmt, 0);
					int fmt_id = sqlite3_column_int(fmt_stmt, 1);
					int data_size = sqlite3_column_int(fmt_stmt, 2);

					DATA_INFO *cdi = data_create_data((UINT)fmt_id, (TCHAR *)fmt_name, NULL, (DWORD)data_size, FALSE, NULL);
					if (cdi != NULL) {
						cdi->param1 = (LPARAM)id;
						cdi->param2 = 0;
						if (last_cdi == NULL) {
							item->child = cdi;
						} else {
							last_cdi->next = cdi;
						}
						last_cdi = cdi;
					}
				}
				sqlite3_finalize(fmt_stmt);
			}
		}
	}
	sqlite3_finalize(stmt);
	return item;
}

/*
 * db_history_ensure_item_data - lazy load format payloads for an item when pasted or viewed
 */
BOOL db_history_ensure_item_data(DATA_INFO *item)
{
	sqlite3_stmt *stmt = NULL;
	DATA_INFO *cdi;
	int rc;

	if (db == NULL || item == NULL) {
		return FALSE;
	}

	if (item->type == TYPE_DATA) {
		if (item->data != NULL) {
			return TRUE;
		}
		if (item->param1 > 0) {
			DATA_INFO *cur;
			for (cur = history_data.child; cur != NULL; cur = cur->next) {
				if (cur->param1 == item->param1) {
					return db_history_ensure_item_data(cur);
				}
			}
			// Standalone DATA_INFO: load format blob directly
			rc = sqlite3_prepare16_v2(db,
				L"SELECT data, data_size FROM item_formats WHERE item_id = ? AND format_name = ?;",
				-1, &stmt, NULL);
			if (rc == SQLITE_OK) {
				sqlite3_bind_int64(stmt, 1, (sqlite3_int64)item->param1);
				sqlite3_bind_text16(stmt, 2, (const WCHAR *)item->format_name, -1, SQLITE_STATIC);
				if (sqlite3_step(stmt) == SQLITE_ROW) {
					const void *blob_ptr = sqlite3_column_blob(stmt, 0);
					int blob_size = sqlite3_column_bytes(stmt, 0);
					if (blob_ptr != NULL && blob_size > 0) {
						item->size = (DWORD)blob_size;
						if ((item->data = format_bytes_to_data(item->format_name, (const BYTE *)blob_ptr, &item->size)) == NULL) {
							item->data = clipboard_bytes_to_data(item->format_name, (const BYTE *)blob_ptr, &item->size);
						}
						item->param2 = 1;
					}
				}
				sqlite3_finalize(stmt);
			}
			return (item->data != NULL);
		}
		return FALSE;
	}

	if (item->type != TYPE_ITEM) {
		return FALSE;
	}
	// If already loaded or not a DB item
	if (item->param2 != 0 || item->param1 <= 0) {
		return TRUE;
	}

	rc = sqlite3_prepare16_v2(db,
		L"SELECT format_name, format_id, data_size, data FROM item_formats WHERE item_id = ?;",
		-1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		return FALSE;
	}
	sqlite3_bind_int64(stmt, 1, (sqlite3_int64)item->param1);

	while (sqlite3_step(stmt) == SQLITE_ROW) {
		const WCHAR *fmt_name = (const WCHAR *)sqlite3_column_text16(stmt, 0);
		int fmt_id = sqlite3_column_int(stmt, 1);
		int data_size = sqlite3_column_int(stmt, 2);
		const void *blob_ptr = sqlite3_column_blob(stmt, 3);
		int blob_size = sqlite3_column_bytes(stmt, 3);

		// Find existing format node in item->child
		cdi = NULL;
		DATA_INFO *cur;
		for (cur = item->child; cur != NULL; cur = cur->next) {
			if (cur->format_name != NULL && lstrcmpi(cur->format_name, (const TCHAR *)fmt_name) == 0) {
				cdi = cur;
				break;
			}
		}
		if (cdi == NULL) {
			cdi = data_create_data((UINT)fmt_id, (TCHAR *)fmt_name, NULL, (DWORD)data_size, FALSE, NULL);
			if (cdi != NULL) {
				cdi->next = item->child;
				item->child = cdi;
			}
		}
		if (cdi != NULL) {
			cdi->param1 = item->param1;
			if (cdi->data == NULL && blob_ptr != NULL && blob_size > 0) {
				cdi->size = (DWORD)blob_size;
				if ((cdi->data = format_bytes_to_data(cdi->format_name, (const BYTE *)blob_ptr, &cdi->size)) == NULL) {
					cdi->data = clipboard_bytes_to_data(cdi->format_name, (const BYTE *)blob_ptr, &cdi->size);
				}
				cdi->param2 = 1;
			}
		}
	}
	sqlite3_finalize(stmt);

	item->param2 = 1;	// Mark loaded
	return TRUE;
}

/*
 * create_snippet - format a clean preview snippet centered around query match
 */
static TCHAR *create_snippet(const TCHAR *text, const TCHAR *query, int target_chars)
{
	int text_len;
	const TCHAR *m;
	int start_pos = 0;
	int copy_len;
	TCHAR buf[1024];
	int bpos = 0;

	if (text == NULL || *text == TEXT('\0')) {
		return NULL;
	}
	text_len = lstrlen(text);
	if (target_chars <= 0 || target_chars > 800) {
		target_chars = 250;
	}

	if (query != NULL && *query != TEXT('\0')) {
		m = StrStrI(text, query);
		if (m != NULL) {
			int match_offset = (int)(m - text);
			int pre = 35;
			if (match_offset > pre) {
				start_pos = match_offset - pre;
				while (start_pos < match_offset && text[start_pos] != TEXT(' ') && text[start_pos] != TEXT('\t') && text[start_pos] != TEXT('\n') && text[start_pos] != TEXT('\r')) {
					start_pos++;
				}
				if (start_pos < match_offset && (text[start_pos] == TEXT(' ') || text[start_pos] == TEXT('\t') || text[start_pos] == TEXT('\n') || text[start_pos] == TEXT('\r'))) {
					start_pos++;
				}
			}
		}
	}

	if (start_pos > 0) {
		buf[bpos++] = TEXT('\x2026'); // ellipsis
		buf[bpos++] = TEXT(' ');
	}

	copy_len = target_chars;
	if (start_pos + copy_len > text_len) {
		copy_len = text_len - start_pos;
	}

	while (copy_len > 0 && bpos < 1000 && text[start_pos] != TEXT('\0')) {
		TCHAR c = text[start_pos++];
		if (c == TEXT('\r') || c == TEXT('\n') || c == TEXT('\t')) {
			c = TEXT(' ');
		}
		if (c == TEXT(' ') && bpos > 0 && buf[bpos - 1] == TEXT(' ')) {
			copy_len--;
			continue;
		}
		buf[bpos++] = c;
		copy_len--;
	}

	if (start_pos < text_len && bpos < 1010) {
		buf[bpos++] = TEXT(' ');
		buf[bpos++] = TEXT('\x2026'); // ellipsis
	}
	buf[bpos] = TEXT('\0');

	return alloc_copy(buf);
}

/*
 * db_history_search - fast full-text & substring search via SQLite
 */
int db_history_search(const TCHAR *query, const int max_results, DB_SEARCH_RESULT **out_results)
{
	sqlite3_stmt *stmt = NULL;
	DB_SEARCH_RESULT *res = NULL;
	int count = 0;
	int capacity = (max_results > 0) ? max_results : 50;
	int rc;

	if (db == NULL || out_results == NULL) {
		return 0;
	}
	*out_results = NULL;

	res = (DB_SEARCH_RESULT *)mem_alloc(sizeof(DB_SEARCH_RESULT) * capacity);
	if (res == NULL) {
		return 0;
	}

	if (query == NULL || *query == TEXT('\0')) {
		// Empty query: show recent items
		rc = sqlite3_prepare16_v2(db,
			L"SELECT id, title, window_title, SUBSTR(text_content, 1, 500) "
			L"FROM items ORDER BY created_at DESC, id DESC LIMIT ?;",
			-1, &stmt, NULL);
		if (rc == SQLITE_OK) {
			sqlite3_bind_int(stmt, 1, capacity);
		}
	} else {
		// Search query: use LIKE on title, text_content, and window_title
		// SQLite LIKE with 5000 rows executes in < 3ms and matches any arbitrary substring!
		TCHAR like_param[BUF_SIZE * 2];
		wsprintf(like_param, TEXT("%%%s%%"), query);

		rc = sqlite3_prepare16_v2(db,
			L"SELECT id, title, window_title, text_content "
			L"FROM items "
			L"WHERE title LIKE ? OR text_content LIKE ? OR window_title LIKE ? "
			L"ORDER BY id DESC LIMIT ?;",
			-1, &stmt, NULL);
		if (rc == SQLITE_OK) {
			sqlite3_bind_text16(stmt, 1, like_param, -1, SQLITE_TRANSIENT);
			sqlite3_bind_text16(stmt, 2, like_param, -1, SQLITE_TRANSIENT);
			sqlite3_bind_text16(stmt, 3, like_param, -1, SQLITE_TRANSIENT);
			sqlite3_bind_int(stmt, 4, capacity);
		}
	}

	if (stmt != NULL) {
		while (count < capacity && sqlite3_step(stmt) == SQLITE_ROW) {
			int id = sqlite3_column_int(stmt, 0);
			const WCHAR *t = (const WCHAR *)sqlite3_column_text16(stmt, 1);
			const WCHAR *w = (const WCHAR *)sqlite3_column_text16(stmt, 2);
			const WCHAR *s = (const WCHAR *)sqlite3_column_text16(stmt, 3);

			res[count].id = id;
			res[count].title = (t != NULL) ? alloc_copy((const TCHAR *)t) : NULL;
			res[count].window_title = (w != NULL) ? alloc_copy((const TCHAR *)w) : NULL;
			res[count].snippet = (s != NULL) ? create_snippet((const TCHAR *)s, query, 250) : NULL;
			if (res[count].snippet == NULL && res[count].title != NULL) {
				res[count].snippet = alloc_copy(res[count].title);
			}
			res[count].di = NULL;
			count++;
		}
		sqlite3_finalize(stmt);
	}

	if (count == 0) {
		mem_free((void **)&res);
		*out_results = NULL;
		return 0;
	}

	*out_results = res;
	return count;
}

/*
 * db_history_free_search_results - free search results array
 */
void db_history_free_search_results(DB_SEARCH_RESULT *results, const int count)
{
	int i;
	if (results == NULL) return;
	for (i = 0; i < count; i++) {
		if (results[i].title != NULL) mem_free((void **)&results[i].title);
		if (results[i].window_title != NULL) mem_free((void **)&results[i].window_title);
		if (results[i].snippet != NULL) mem_free((void **)&results[i].snippet);
		if (results[i].di != NULL) data_free(results[i].di);
	}
	mem_free((void **)&results);
}

/*
 * db_history_delete_item - delete an item and all its formats
 */
BOOL db_history_delete_item(const int id)
{
	sqlite3_stmt *stmt = NULL;
	int rc;

	if (db == NULL || id <= 0) return FALSE;

	BOOL inside_tx = (sqlite3_get_autocommit(db) == 0);
	if (!inside_tx) {
		sqlite3_exec(db, "BEGIN IMMEDIATE;", NULL, NULL, NULL);
	}

	rc = sqlite3_prepare_v2(db, "DELETE FROM items WHERE id = ?;", -1, &stmt, NULL);
	if (rc == SQLITE_OK) {
		sqlite3_bind_int(stmt, 1, id);
		sqlite3_step(stmt);
		sqlite3_finalize(stmt);
	}

	rc = sqlite3_prepare_v2(db, "DELETE FROM item_formats WHERE item_id = ?;", -1, &stmt, NULL);
	if (rc == SQLITE_OK) {
		sqlite3_bind_int(stmt, 1, id);
		sqlite3_step(stmt);
		sqlite3_finalize(stmt);
	}

	rc = sqlite3_prepare_v2(db, "DELETE FROM history_fts WHERE docid = ?;", -1, &stmt, NULL);
	if (rc == SQLITE_OK) {
		sqlite3_bind_int(stmt, 1, id);
		sqlite3_step(stmt);
		sqlite3_finalize(stmt);
	}

	if (!inside_tx) {
		sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
	}

	return TRUE;
}

/*
 * db_history_trim - keep only the latest max_count items
 */
BOOL db_history_trim(const int max_count)
{
	sqlite3_stmt *stmt = NULL;
	int rc;

	if (db == NULL || max_count <= 0) return FALSE;

	BOOL inside_tx = (sqlite3_get_autocommit(db) == 0);
	if (!inside_tx) {
		sqlite3_exec(db, "BEGIN IMMEDIATE;", NULL, NULL, NULL);
	}

	rc = sqlite3_prepare_v2(db,
		"DELETE FROM item_formats WHERE item_id IN ("
		"  SELECT id FROM items WHERE pinned = 0 ORDER BY created_at DESC, id DESC LIMIT -1 OFFSET ?"
		");", -1, &stmt, NULL);
	if (rc == SQLITE_OK) {
		sqlite3_bind_int(stmt, 1, max_count);
		sqlite3_step(stmt);
		sqlite3_finalize(stmt);
	}

	rc = sqlite3_prepare_v2(db,
		"DELETE FROM history_fts WHERE docid IN ("
		"  SELECT id FROM items WHERE pinned = 0 ORDER BY created_at DESC, id DESC LIMIT -1 OFFSET ?"
		");", -1, &stmt, NULL);
	if (rc == SQLITE_OK) {
		sqlite3_bind_int(stmt, 1, max_count);
		sqlite3_step(stmt);
		sqlite3_finalize(stmt);
	}

	rc = sqlite3_prepare_v2(db,
		"DELETE FROM items WHERE pinned = 0 AND id NOT IN ("
		"  SELECT id FROM items WHERE pinned = 0 ORDER BY created_at DESC, id DESC LIMIT ?"
		");", -1, &stmt, NULL);
	if (rc == SQLITE_OK) {
		sqlite3_bind_int(stmt, 1, max_count);
		sqlite3_step(stmt);
		sqlite3_finalize(stmt);
	}

	if (!inside_tx) {
		sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
	}

	return TRUE;
}

/*
 * db_history_migrate_from_dat - one-time import from legacy history.dat
 */
BOOL db_history_migrate_from_dat(const TCHAR *dat_path)
{
	sqlite3_stmt *stmt = NULL;
	DATA_INFO *legacy_root = NULL;
	DATA_INFO *di;
	TCHAR err_str[BUF_SIZE + MAX_PATH];
	int existing_count = 0;
	int rc;

	if (db == NULL || dat_path == NULL || PathFileExists(dat_path) == FALSE) {
		return FALSE;
	}

	// Check if database already has items
	rc = sqlite3_prepare_v2(db, "SELECT count(*) FROM items;", -1, &stmt, NULL);
	if (rc == SQLITE_OK) {
		if (sqlite3_step(stmt) == SQLITE_ROW) {
			existing_count = sqlite3_column_int(stmt, 0);
		}
		sqlite3_finalize(stmt);
	}

	if (existing_count > 0) {
		// Database already populated, no migration needed
		return TRUE;
	}

	*err_str = TEXT('\0');
	if (file_read_data(dat_path, &legacy_root, err_str) == FALSE || legacy_root == NULL) {
		return FALSE;
	}

	// Reverse list so oldest item is inserted first (lowest ID) and newest last (highest ID)
	{
		DATA_INFO *prev = NULL;
		DATA_INFO *curr = legacy_root;
		DATA_INFO *next_node = NULL;
		while (curr != NULL) {
			next_node = curr->next;
			curr->next = prev;
			prev = curr;
			curr = next_node;
		}
		legacy_root = prev;
	}

	sqlite3_exec(db, "BEGIN IMMEDIATE;", NULL, NULL, NULL);
	for (di = legacy_root; di != NULL; di = di->next) {
		db_history_save_item(di);
	}
	sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);

	data_free(legacy_root);

	// Backup legacy file to history.dat.bak
	TCHAR bak_path[MAX_PATH];
	wsprintf(bak_path, TEXT("%s.bak"), dat_path);
	MoveFileEx(dat_path, bak_path, MOVEFILE_REPLACE_EXISTING);

	return TRUE;
}
