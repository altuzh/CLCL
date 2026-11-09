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
#include <limits.h>

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
#include "PinnedImage.h"
#include "gdip.h"

static sqlite3 *db = NULL;
extern DATA_INFO history_data;
extern DATA_INFO regist_data;

/* PNG is private to SQLite; clipboard and .dat serialization stay unchanged. */
static HANDLE db_format_from_bytes(DATA_INFO *item, const BYTE *bytes, DWORD size)
{
	static const BYTE png_signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
	UINT fmt = item->format ? item->format : clipboard_get_format(0, item->format_name);
	HANDLE result;
	if ((fmt == CF_BITMAP || fmt == CF_DSPBITMAP) && size >= sizeof(png_signature) &&
		memcmp(bytes, png_signature, sizeof(png_signature)) == 0) {
		return png_to_bitmap(bytes, size);
	}
	item->size = size;
	result = format_bytes_to_data(item->format_name, bytes, &item->size);
	if (result == NULL)
		result = clipboard_bytes_to_data(item->format_name, bytes, &item->size);
	return result;
}

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
	sqlite3_exec(db, "PRAGMA auto_vacuum=FULL;", NULL, NULL, NULL);
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
		"CREATE TABLE IF NOT EXISTS image_edits ("
		"  id INTEGER PRIMARY KEY AUTOINCREMENT,"
		"  initial BLOB NOT NULL,"
		"  merged BLOB NOT NULL,"
		"  base BLOB,"
		"  annotations TEXT NOT NULL"
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
	sqlite3_exec(db, "ALTER TABLE items ADD COLUMN image_edit_id INTEGER DEFAULT 0;", NULL, NULL, NULL);
	/* Verify the additive upgrade before allowing any writes. */
	if (sqlite3_exec(db, "SELECT image_edit_id FROM items LIMIT 0;", NULL, NULL, NULL) != SQLITE_OK) return FALSE;
	sqlite3_exec(db, "CREATE INDEX IF NOT EXISTS idx_items_hash ON items(content_hash);", NULL, NULL, NULL);

	// Deduplicate items by content_hash
	sqlite3_exec(db,
		"DELETE FROM items WHERE image_edit_id = 0 AND content_hash IS NOT NULL AND content_hash != 0 AND id NOT IN ("
		"  SELECT MAX(id) FROM items WHERE image_edit_id = 0 AND content_hash IS NOT NULL AND content_hash != 0 GROUP BY content_hash"
		");", NULL, NULL, NULL);
	// Deduplicate legacy duplicate items
	sqlite3_exec(db,
		"DELETE FROM items WHERE image_edit_id = 0 AND text_content IS NOT NULL AND text_content != '' AND id NOT IN ("
		"  SELECT MAX(id) FROM items WHERE image_edit_id = 0 AND text_content IS NOT NULL AND text_content != '' GROUP BY title, text_content"
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

	// Existing databases need one rebuild to enable automatic space reclamation.
	// Keep history usable if the rebuild fails (e.g. insufficient disk space);
	// auto_vacuum remains NONE and initialization retries on the next launch.
	{
		sqlite3_stmt *stmt = NULL;
		BOOL needs_vacuum = FALSE;
		if (sqlite3_prepare_v2(db, "PRAGMA auto_vacuum;", -1, &stmt, NULL) == SQLITE_OK &&
			sqlite3_step(stmt) == SQLITE_ROW) {
			needs_vacuum = (sqlite3_column_int(stmt, 0) == 0);
		}
		sqlite3_finalize(stmt);
		if (needs_vacuum) {
			if (sqlite3_exec(db, "VACUUM;", NULL, NULL, NULL) != SQLITE_OK)
				OutputDebugString(TEXT("CLCL: history database compaction failed; will retry next launch.\n"));
			else
				sqlite3_wal_checkpoint_v2(db, NULL, SQLITE_CHECKPOINT_TRUNCATE, NULL, NULL);
		}
	}

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
		L"SELECT id, content_hash, title, text_content, total_size, image_edit_id FROM items ORDER BY id DESC LIMIT 1;",
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

			if (is_dup && item->image_edit_id == sqlite3_column_int(stmt_check, 5)) {
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
				if (sqlite3_get_autocommit(db)) db_history_deduplicate_images(item);
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
		L"INSERT INTO items (created_at, window_title, title, pinned, total_size, text_content, content_hash, image_edit_id) "
		L"VALUES (?, ?, ?, 0, ?, ?, ?, ?);",
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
	sqlite3_bind_int(stmt_item, 7, item->image_edit_id);
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
			DWORD data_size;
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
			data_size = blob_size;
			if ((fmt == CF_BITMAP || fmt == CF_DSPBITMAP) && blob_size > 0) {
				DWORD png_size = 0;
				BYTE *png = bitmap_to_png((HBITMAP)cdi->data, &png_size);
				// Keep the original payload if encoding fails or PNG is larger.
				if (png != NULL && png_size < blob_size) {
					if (free_mem) mem_free((void **)&mem);
					mem = png;
					blob_ptr = mem;
					blob_size = png_size;
					free_mem = TRUE;
				} else mem_free((void **)&png);
			}

			sqlite3_reset(stmt_fmt);
			sqlite3_bind_int64(stmt_fmt, 1, item_id);
			sqlite3_bind_text16(stmt_fmt, 2, cdi->format_name ? cdi->format_name : TEXT(""), -1, SQLITE_TRANSIENT);
			sqlite3_bind_int(stmt_fmt, 3, (int)fmt);
			sqlite3_bind_int(stmt_fmt, 4, (int)data_size);
			if (blob_ptr != NULL && blob_size > 0) {
				sqlite3_bind_blob(stmt_fmt, 5, blob_ptr, (int)blob_size, SQLITE_TRANSIENT);
			} else {
				sqlite3_bind_null(stmt_fmt, 5);
			}
			rc = sqlite3_step(stmt_fmt);

			if (unlock_data) GlobalUnlock(cdi->data);
			if (free_mem) mem_free((void **)&mem);
			if (rc != SQLITE_DONE) break;
		}
		sqlite3_finalize(stmt_fmt);
	}
	if (rc != SQLITE_DONE && item->child != NULL) {
		if (!inside_tx) sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
		return FALSE;
	}

	if (!inside_tx) {
		if (sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK) {
			sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
			return FALSE;
		}
		db_history_deduplicate_images(item);
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
	db_history_deduplicate_images(NULL);

	rc = sqlite3_prepare16_v2(db,
		L"SELECT id, created_at, window_title, title, text_content, content_hash, image_edit_id FROM items ORDER BY created_at DESC, id DESC LIMIT ?;",
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
				if (chash == last_loaded_hash && tail->image_edit_id == sqlite3_column_int(stmt, 6)) {
					continue;
				}
			} else if (txt != NULL && last_loaded_text != NULL && tail->image_edit_id == sqlite3_column_int(stmt, 6)) {
				if (wcscmp(txt, last_loaded_text) == 0) {
					continue;
				}
			}
		}

		DATA_INFO *item = data_create_item(title, FALSE, NULL);
		if (item == NULL) continue;

		item->content_hash = chash;
		item->image_edit_id = sqlite3_column_int(stmt, 6);
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
		L"SELECT created_at, window_title, title, content_hash, image_edit_id FROM items WHERE id = ?;",
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
			item->image_edit_id = sqlite3_column_int(stmt, 4);
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
						item->size = (DWORD)sqlite3_column_int(stmt, 1);
						item->data = db_format_from_bytes(item, (const BYTE *)blob_ptr, (DWORD)blob_size);
						if (item->data == NULL) {
							sqlite3_finalize(stmt);
							return FALSE;
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
				cdi->size = (DWORD)data_size;
				cdi->data = db_format_from_bytes(cdi, (const BYTE *)blob_ptr, (DWORD)blob_size);
				if (cdi->data == NULL) {
					sqlite3_finalize(stmt);
					return FALSE;
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
	const char *deletes[] = {"DELETE FROM items WHERE id = ?;",
		"DELETE FROM item_formats WHERE item_id = ?;", "DELETE FROM history_fts WHERE docid = ?;"};
	int rc, i;

	if (db == NULL || id <= 0) return FALSE;

	BOOL inside_tx = (sqlite3_get_autocommit(db) == 0);
	if (!inside_tx) {
		if (sqlite3_exec(db, "BEGIN IMMEDIATE;", NULL, NULL, NULL) != SQLITE_OK) return FALSE;
	}

	for (i = 0; i < ARRAYSIZE(deletes); i++) {
		rc = sqlite3_prepare_v2(db, deletes[i], -1, &stmt, NULL);
		if (rc != SQLITE_OK) break;
		sqlite3_bind_int(stmt, 1, id);
		rc = sqlite3_step(stmt);
		sqlite3_finalize(stmt); stmt = NULL;
		if (rc != SQLITE_DONE) break;
	}
	sqlite3_finalize(stmt);
	if (rc != SQLITE_DONE || (!inside_tx && sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK)) {
		if (!inside_tx) sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
		return FALSE;
	}
	return TRUE;
}

/* Identity uses unmarked source pixels, ignoring bitmap encoding and reserved alpha bytes. */
static BYTE *db_initial_image_pixels(DATA_INFO *item, DWORD *size)
{
	HBITMAP bitmap = NULL;
	BOOL owned = FALSE;
	BITMAP object;
	BITMAPINFO info = {0};
	BYTE *pixels = NULL;
	HDC dc;
	DWORD bytes, i;
	*size = 0;
	if (item == NULL || item->type != TYPE_ITEM) return NULL;
	if (item->image_edit_id > 0) {
		sqlite3_stmt *s = NULL;
		if (sqlite3_prepare_v2(db, "SELECT initial FROM image_edits WHERE id=?;", -1, &s, NULL) == SQLITE_OK) {
			sqlite3_bind_int(s, 1, item->image_edit_id);
			if (sqlite3_step(s) == SQLITE_ROW) bitmap = png_to_bitmap(sqlite3_column_blob(s, 0), (DWORD)sqlite3_column_bytes(s, 0));
		}
		sqlite3_finalize(s);
		owned = TRUE;
	} else {
		DATA_INFO *c;
		/* A rich-text clip with an embedded bitmap is still a text clip. */
		for (c = item->child; c != NULL; c = c->next)
			if (c->format == CF_UNICODETEXT || c->format == CF_TEXT || c->format == CF_OEMTEXT) return NULL;
		if (!db_history_ensure_item_data(item)) return NULL;
		for (c = item->child; c != NULL; c = c->next) {
			UINT format = c->format ? c->format : clipboard_get_format(0, c->format_name);
			if (format == CF_BITMAP || format == CF_DSPBITMAP) { bitmap = (HBITMAP)c->data; break; }
		}
		if (bitmap == NULL) for (c = item->child; c != NULL; c = c->next) {
			UINT format = c->format ? c->format : clipboard_get_format(0, c->format_name);
			if ((format == CF_DIB || format == CF_DIBV5) && c->data != NULL) {
				const BYTE *memory = GlobalLock(c->data);
				SIZE_T length = GlobalSize(c->data);
				if (memory != NULL && length >= sizeof(BITMAPINFOHEADER)) {
					const BITMAPINFOHEADER *b = (const BITMAPINFOHEADER *)memory;
					ULONGLONG offset = b->biSize;
					if (b->biBitCount <= 8) offset += (ULONGLONG)(b->biClrUsed ? b->biClrUsed : 1u << b->biBitCount) * sizeof(RGBQUAD);
					else offset += (ULONGLONG)b->biClrUsed * sizeof(RGBQUAD);
					if (b->biSize == sizeof(*b) && b->biCompression == BI_BITFIELDS) offset += 12;
					if (b->biSize >= sizeof(*b) && b->biWidth > 0 && b->biHeight != 0 && b->biHeight != LONG_MIN && b->biPlanes == 1 &&
						(b->biBitCount == 1 || b->biBitCount == 4 || b->biBitCount == 8 || b->biBitCount == 16 || b->biBitCount == 24 || b->biBitCount == 32) &&
						(b->biCompression == BI_RGB || b->biCompression == BI_BITFIELDS) && offset <= length &&
						(((ULONGLONG)b->biWidth * b->biBitCount + 31) / 32) * 4 * (b->biHeight < 0 ? -(LONGLONG)b->biHeight : b->biHeight) <= length - offset) {
						dc = GetDC(NULL);
						if (dc != NULL) { bitmap = CreateDIBitmap(dc, b, CBM_INIT, memory + (SIZE_T)offset, (const BITMAPINFO *)memory, DIB_RGB_COLORS); ReleaseDC(NULL, dc); }
					}
				}
				if (memory != NULL) GlobalUnlock(c->data);
				owned = TRUE;
				if (bitmap != NULL) break;
			}
		}
	}
	if (bitmap == NULL || !GetObject(bitmap, sizeof(object), &object) || object.bmWidth <= 0 || object.bmHeight <= 0 ||
		(ULONGLONG)object.bmWidth * object.bmHeight * 4 + 8 > MAXDWORD) goto done;
	bytes = (DWORD)object.bmWidth * object.bmHeight * 4;
	pixels = mem_alloc(bytes + 8);
	if (pixels == NULL) goto done;
	((LONG *)pixels)[0] = object.bmWidth; ((LONG *)pixels)[1] = object.bmHeight;
	info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = object.bmWidth;
	info.bmiHeader.biHeight = -object.bmHeight; info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
	dc = GetDC(NULL);
	if (dc == NULL || GetDIBits(dc, bitmap, 0, object.bmHeight, pixels + 8, &info, DIB_RGB_COLORS) != object.bmHeight) {
		mem_free((void **)&pixels);
	} else {
		for (i = 11; i < bytes + 8; i += 4) pixels[i] = 0;
		*size = bytes + 8;
	}
	if (dc != NULL) ReleaseDC(NULL, dc);
done:
	if (owned && bitmap != NULL) DeleteObject(bitmap);
	return pixels;
}

BOOL db_history_image_edit_matches(DATA_INFO *item, int id)
{
	sqlite3_stmt *stmt = NULL;
	DATA_INFO saved = {0}, image = {0};
	BYTE *pixels, *merged = NULL;
	DWORD size, merged_size;
	BOOL match = FALSE;
	if (db == NULL || item == NULL || item->image_edit_id != 0 || id <= 0) return FALSE;
	pixels = db_initial_image_pixels(item, &size);
	if (pixels == NULL) return FALSE;
	if (sqlite3_prepare_v2(db, "SELECT merged FROM image_edits WHERE id=?;", -1, &stmt, NULL) == SQLITE_OK) {
		sqlite3_bind_int(stmt, 1, id);
		if (sqlite3_step(stmt) == SQLITE_ROW)
			image.data = png_to_bitmap(sqlite3_column_blob(stmt, 0), sqlite3_column_bytes(stmt, 0));
	}
	sqlite3_finalize(stmt);
	if (image.data != NULL) {
		saved.type = TYPE_ITEM; saved.child = &image;
		image.type = TYPE_DATA; image.format = CF_BITMAP;
		merged = db_initial_image_pixels(&saved, &merged_size);
		match = merged != NULL && size == merged_size && !memcmp(pixels, merged, size);
		DeleteObject(image.data);
	}
	mem_free((void **)&pixels); mem_free((void **)&merged);
	return match;
}

BOOL db_history_has_edited_original(DATA_INFO *item)
{
	sqlite3_stmt *rows = NULL;
	BYTE *pixels;
	DWORD size;
	BOOL found = FALSE;
	if (db == NULL || item == NULL || item->image_edit_id != 0) return FALSE;
	/* History loading can be disabled; initialize identities before the first capture too. */
	if (sqlite3_prepare_v2(db, "SELECT 1 FROM temp.sqlite_master WHERE name='initial_image_keys';", -1, &rows, NULL) != SQLITE_OK) return FALSE;
	if (sqlite3_step(rows) != SQLITE_ROW) {
		sqlite3_finalize(rows); rows = NULL;
		if (!db_history_deduplicate_images(NULL)) return FALSE;
	}
	sqlite3_finalize(rows); rows = NULL;
	pixels = db_initial_image_pixels(item, &size);
	if (pixels == NULL) return FALSE;
	if (sqlite3_prepare_v2(db, "SELECT i.id FROM items i JOIN initial_image_keys k ON i.id=k.id WHERE i.image_edit_id>0 AND k.hash=?;", -1, &rows, NULL) == SQLITE_OK) {
		sqlite3_bind_int64(rows, 1, (sqlite3_int64)fnv1a_64(pixels, size, 0xCBF29CE484222325ULL));
		while (sqlite3_step(rows) == SQLITE_ROW) {
			DATA_INFO *other = db_history_get_item(sqlite3_column_int(rows, 0));
			DWORD other_size;
			BYTE *original = db_initial_image_pixels(other, &other_size);
			data_free(other);
			found = original != NULL && size == other_size && !memcmp(pixels, original, size);
			mem_free((void **)&original);
			if (found) break;
		}
	}
	sqlite3_finalize(rows); mem_free((void **)&pixels);
	return found;
}

static void db_remove_missing_history(DATA_INFO **root, DATA_INFO *keep, sqlite3_stmt *exists)
{
	while (*root != NULL) {
		DATA_INFO *item = *root;
		BOOL remove = FALSE;
		if (item->type == TYPE_FOLDER) {
			db_remove_missing_history(&item->child, keep, exists);
			remove = item->child == NULL;
		} else if (item->type == TYPE_ITEM && item != keep && item->param1 > 0) {
			sqlite3_reset(exists); sqlite3_bind_int64(exists, 1, item->param1);
			remove = sqlite3_step(exists) == SQLITE_DONE || (keep != NULL && item->param1 == keep->param1);
		}
		if (remove) { *root = item->next; item->next = NULL; data_free(item); }
		else root = &item->next;
	}
}

BOOL db_history_deduplicate_images(DATA_INFO *keep)
{
	sqlite3_stmt *rows = NULL, *cache = NULL, *matches = NULL, *remember = NULL;
	BYTE *pixels = NULL;
	int rc, deleted = 0;
	BOOL ok = FALSE;
	if (db == NULL || !sqlite3_get_autocommit(db)) return FALSE;
	/* Only hashes live in this connection-local cache; clipboard payloads stay on disk. */
	if (sqlite3_exec(db, "CREATE TEMP TABLE IF NOT EXISTS initial_image_keys(id INTEGER PRIMARY KEY,hash INTEGER);"
		"CREATE INDEX IF NOT EXISTS initial_image_hash ON initial_image_keys(hash);"
		"CREATE TEMP TABLE IF NOT EXISTS initial_image_keep(id INTEGER PRIMARY KEY,hash INTEGER);"
		"CREATE INDEX IF NOT EXISTS initial_keep_hash ON initial_image_keep(hash);"
		"DELETE FROM initial_image_keep; BEGIN IMMEDIATE;", NULL, NULL, NULL) != SQLITE_OK) return FALSE;
	if (sqlite3_prepare_v2(db, "SELECT id FROM items WHERE id NOT IN (SELECT id FROM initial_image_keys);", -1, &rows, NULL) != SQLITE_OK ||
		sqlite3_prepare_v2(db, "INSERT INTO initial_image_keys VALUES(?,?);", -1, &cache, NULL) != SQLITE_OK) goto done;
	while ((rc = sqlite3_step(rows)) == SQLITE_ROW) {
		int id = sqlite3_column_int(rows, 0);
		DWORD size;
		DATA_INFO *item = db_history_get_item(id);
		UINT64 hash = 0;
		if (item == NULL) goto done;
		pixels = db_initial_image_pixels(item, &size); data_free(item);
		if (pixels != NULL) hash = fnv1a_64(pixels, size, 0xCBF29CE484222325ULL);
		mem_free((void **)&pixels);
		sqlite3_reset(cache); sqlite3_bind_int(cache, 1, id); sqlite3_bind_int64(cache, 2, (sqlite3_int64)hash);
		if (sqlite3_step(cache) != SQLITE_DONE) goto done;
	}
	if (rc != SQLITE_DONE) goto done;
	sqlite3_finalize(rows); rows = NULL;
	if (sqlite3_prepare_v2(db, "SELECT i.id,k.hash FROM items i JOIN initial_image_keys k ON i.id=k.id WHERE k.hash!=0 "
		"AND (?=0 OR k.hash=(SELECT hash FROM initial_image_keys WHERE id=?)) "
		"ORDER BY (i.id=?) DESC,(i.image_edit_id>0) DESC,i.created_at DESC,i.id DESC;", -1, &rows, NULL) != SQLITE_OK ||
		sqlite3_prepare_v2(db, "SELECT id FROM initial_image_keep WHERE hash=?;", -1, &matches, NULL) != SQLITE_OK ||
		sqlite3_prepare_v2(db, "INSERT INTO initial_image_keep VALUES(?,?);", -1, &remember, NULL) != SQLITE_OK) goto done;
	sqlite3_bind_int64(rows, 1, keep != NULL ? keep->param1 : 0);
	sqlite3_bind_int64(rows, 2, keep != NULL ? keep->param1 : 0);
	sqlite3_bind_int64(rows, 3, keep != NULL ? keep->param1 : 0);
	while ((rc = sqlite3_step(rows)) == SQLITE_ROW) {
		int id = sqlite3_column_int(rows, 0), match;
		sqlite3_int64 hash = sqlite3_column_int64(rows, 1);
		DWORD size = 0;
		BOOL duplicate = FALSE;
		sqlite3_reset(matches); sqlite3_bind_int64(matches, 1, hash);
		while ((match = sqlite3_step(matches)) == SQLITE_ROW) {
			DATA_INFO *a, *b;
			BYTE *other;
			DWORD other_size;
			if (pixels == NULL) { a = db_history_get_item(id); pixels = db_initial_image_pixels(a, &size); data_free(a); }
			b = db_history_get_item(sqlite3_column_int(matches, 0));
			other = db_initial_image_pixels(b, &other_size); data_free(b);
			duplicate = pixels != NULL && other != NULL && size == other_size && !memcmp(pixels, other, size);
			mem_free((void **)&other);
			if (duplicate) break;
		}
		mem_free((void **)&pixels);
		if (match != SQLITE_ROW && match != SQLITE_DONE) goto done;
		sqlite3_reset(matches);
		if (duplicate) { if (!db_history_delete_item(id)) goto done; deleted++; }
		else {
			sqlite3_reset(remember); sqlite3_bind_int(remember, 1, id); sqlite3_bind_int64(remember, 2, hash);
			if (sqlite3_step(remember) != SQLITE_DONE) goto done;
		}
	}
	if (rc != SQLITE_DONE) goto done;
	/* Finalize the read before committing deleted rows and reclaiming their pages. */
	sqlite3_finalize(rows); rows = NULL;
	ok = sqlite3_exec(db, "DELETE FROM initial_image_keys WHERE id NOT IN (SELECT id FROM items); COMMIT;", NULL, NULL, NULL) == SQLITE_OK;
done:
	mem_free((void **)&pixels); sqlite3_finalize(rows); sqlite3_finalize(cache); sqlite3_finalize(matches); sqlite3_finalize(remember);
	if (!ok) { sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL); OutputDebugString(TEXT("CLCL: initial-image history deduplication failed.\n")); }
	else if (deleted || keep != NULL) {
		sqlite3_stmt *exists = NULL;
		if (sqlite3_prepare_v2(db, "SELECT 1 FROM items WHERE id=?;", -1, &exists, NULL) == SQLITE_OK)
			db_remove_missing_history(&history_data.child, keep, exists);
		sqlite3_finalize(exists);
	}
	return ok;
}

/* Replace an existing item atomically, preserving the old row if saving fails. */
BOOL db_history_update_item(DATA_INFO *item)
{
	int old_id;
	if (db == NULL || item == NULL || item->type != TYPE_ITEM || (old_id = (int)item->param1) <= 0) {
		return FALSE;
	}
	if (sqlite3_exec(db, "SAVEPOINT update_item;", NULL, NULL, NULL) != SQLITE_OK) return FALSE;
	if (!db_history_delete_item(old_id)) {
		sqlite3_exec(db, "ROLLBACK TO update_item; RELEASE update_item;", NULL, NULL, NULL);
		return FALSE;
	}
	item->param1 = 0;
	item->content_hash = 0;
	if (!db_history_save_item(item)) {
		item->param1 = old_id;
		sqlite3_exec(db, "ROLLBACK TO update_item; RELEASE update_item;", NULL, NULL, NULL);
		return FALSE;
	}
	if (sqlite3_exec(db, "RELEASE update_item;", NULL, NULL, NULL) != SQLITE_OK) {
		item->param1 = old_id;
		sqlite3_exec(db, "ROLLBACK TO update_item; RELEASE update_item;", NULL, NULL, NULL);
		return FALSE;
	}
	if (sqlite3_get_autocommit(db)) db_history_deduplicate_images(item);
	return TRUE;
}

/* A new snapshot keeps History/Favourites copies independent when either is edited. */
BOOL db_history_save_image_edit(DATA_INFO *item, HBITMAP initial, HBITMAP base, HBITMAP merged, const char *json, BOOL history)
{
	sqlite3_stmt *stmt = NULL;
	BYTE *initial_png, *base_png, *merged_png;
	DWORD initial_size = 0, base_size = 0, merged_size = 0;
	LPARAM old_id;
	int old_edit;
	UINT64 old_hash;
	BOOL ok = FALSE;
	if (db == NULL || item == NULL || json == NULL || initial == NULL || base == NULL || merged == NULL) return FALSE;
	initial_png = bitmap_to_png(initial, &initial_size);
	base_png = bitmap_to_png(base, &base_size);
	merged_png = bitmap_to_png(merged, &merged_size);
	if (initial_png == NULL || base_png == NULL || merged_png == NULL ||
		initial_size > INT_MAX || base_size > INT_MAX || merged_size > INT_MAX) goto done;
	old_id = item->param1; old_edit = item->image_edit_id; old_hash = item->content_hash;
	if (sqlite3_exec(db, "SAVEPOINT save_image;", NULL, NULL, NULL) != SQLITE_OK) goto done;
	if (sqlite3_prepare_v2(db, "INSERT INTO image_edits(initial, base, annotations, merged) VALUES(?, ?, ?, ?);",
		-1, &stmt, NULL) != SQLITE_OK) goto rollback;
	sqlite3_bind_blob(stmt, 1, initial_png, (int)initial_size, SQLITE_TRANSIENT);
	if (base_size != initial_size || memcmp(base_png, initial_png, base_size) != 0)
		sqlite3_bind_blob(stmt, 2, base_png, (int)base_size, SQLITE_TRANSIENT);
	else sqlite3_bind_null(stmt, 2);
	sqlite3_bind_text(stmt, 3, json, -1, SQLITE_TRANSIENT);
	sqlite3_bind_blob(stmt, 4, merged_png, (int)merged_size, SQLITE_TRANSIENT);
	if (sqlite3_step(stmt) != SQLITE_DONE) goto rollback;
	sqlite3_finalize(stmt); stmt = NULL;
	item->image_edit_id = (int)sqlite3_last_insert_rowid(db);
	if (sqlite3_last_insert_rowid(db) > INT_MAX) goto rollback;
	item->content_hash = 0;
	if (history && !(item->param1 > 0 ? db_history_update_item(item) : db_history_save_item(item))) goto rollback;
	if (sqlite3_exec(db, "RELEASE save_image;", NULL, NULL, NULL) != SQLITE_OK) goto rollback;
	if (history && sqlite3_get_autocommit(db)) db_history_deduplicate_images(item);
	ok = TRUE;
	goto done;
rollback:
	sqlite3_finalize(stmt); stmt = NULL;
	sqlite3_exec(db, "ROLLBACK TO save_image; RELEASE save_image;", NULL, NULL, NULL);
	item->param1 = old_id; item->image_edit_id = old_edit; item->content_hash = old_hash;
done:
	sqlite3_finalize(stmt);
	mem_free((void **)&initial_png); mem_free((void **)&base_png);
	mem_free((void **)&merged_png);
	return ok;
}

BOOL db_history_load_image_edit(int id, HBITMAP *initial, HBITMAP *base, char **json)
{
	sqlite3_stmt *stmt = NULL;
	BOOL ok = FALSE;
	*initial = *base = NULL; *json = NULL;
	if (db == NULL || id <= 0) return FALSE;
	if (sqlite3_prepare_v2(db, "SELECT initial, COALESCE(base, initial), annotations FROM image_edits WHERE id = ?;",
		-1, &stmt, NULL) != SQLITE_OK) return FALSE;
	sqlite3_bind_int(stmt, 1, id);
	if (sqlite3_step(stmt) == SQLITE_ROW) {
		int bytes = sqlite3_column_bytes(stmt, 2);
		const char *text = (const char *)sqlite3_column_text(stmt, 2);
		if (text != NULL && bytes > 0 && bytes < INT_MAX && strlen(text) == (size_t)bytes) {
			*json = mem_alloc((size_t)bytes + 1);
			if (*json != NULL) CopyMemory(*json, text, (size_t)bytes + 1);
		}
		*initial = png_to_bitmap(sqlite3_column_blob(stmt, 0), sqlite3_column_bytes(stmt, 0));
		*base = png_to_bitmap(sqlite3_column_blob(stmt, 1), sqlite3_column_bytes(stmt, 1));
		ok = *initial != NULL && *base != NULL && *json != NULL;
	}
	sqlite3_finalize(stmt);
	if (!ok) {
		if (*initial != NULL) DeleteObject(*initial);
		if (*base != NULL) DeleteObject(*base);
		*initial = *base = NULL; mem_free((void **)json);
	}
	return ok;
}

static BOOL db_keep_favourite_edits(const DATA_INFO *item, sqlite3_stmt *stmt)
{
	for (; item != NULL; item = item->next) {
		if (item->image_edit_id > 0) {
			sqlite3_reset(stmt); sqlite3_bind_int(stmt, 1, item->image_edit_id);
			if (sqlite3_step(stmt) != SQLITE_DONE) return FALSE;
		}
		if (item->child != NULL && !db_keep_favourite_edits(item->child, stmt)) return FALSE;
	}
	return TRUE;
}

/* Collect snapshots only after their History and Favourites references are gone. */
static BOOL db_prune_image_edits(void)
{
	sqlite3_stmt *stmt = NULL;
	BOOL ok;
	int pending = pinned_image_pending_clipboard_edit();
	if (sqlite3_exec(db, "CREATE TEMP TABLE IF NOT EXISTS favourite_edits(id INTEGER PRIMARY KEY);"
		"DELETE FROM favourite_edits;", NULL, NULL, NULL) != SQLITE_OK) return FALSE;
	if (sqlite3_prepare_v2(db, "INSERT OR IGNORE INTO favourite_edits VALUES(?);", -1, &stmt, NULL) != SQLITE_OK) return FALSE;
	ok = db_keep_favourite_edits(regist_data.child, stmt);
	/* Copy publishes before the delayed History capture can own its snapshot. */
	if (ok && pending > 0) {
		sqlite3_reset(stmt); sqlite3_bind_int(stmt, 1, pending);
		ok = sqlite3_step(stmt) == SQLITE_DONE;
	}
	sqlite3_finalize(stmt);
	return ok && sqlite3_exec(db, "DELETE FROM image_edits WHERE id NOT IN "
		"(SELECT image_edit_id FROM items WHERE image_edit_id > 0) "
		"AND id NOT IN (SELECT id FROM favourite_edits);", NULL, NULL, NULL) == SQLITE_OK;
}

/* Refresh only History after committed pruning; Favourites own independent copies. */
static void db_sync_trimmed_history(void)
{
	sqlite3_stmt *exists = NULL;
	if (sqlite3_prepare_v2(db, "SELECT 1 FROM items WHERE id=?;", -1, &exists, NULL) == SQLITE_OK)
		db_remove_missing_history(&history_data.child, NULL, exists);
	sqlite3_finalize(exists);
}

static BOOL db_history_size(BOOL used_only, ULONGLONG *bytes)
{
	const char *queries[] = {"PRAGMA page_count;", "PRAGMA page_size;", "PRAGMA freelist_count;"};
	sqlite3_int64 values[3] = {0};
	int i;
	for (i = 0; i < (used_only ? 3 : 2); i++) {
		sqlite3_stmt *stmt = NULL;
		int rc = sqlite3_prepare_v2(db, queries[i], -1, &stmt, NULL);
		if (rc == SQLITE_OK) rc = sqlite3_step(stmt);
		if (rc == SQLITE_ROW) values[i] = sqlite3_column_int64(stmt, 0);
		sqlite3_finalize(stmt);
		if (rc != SQLITE_ROW || values[i] < 0) return FALSE;
	}
	if (values[1] == 0 || values[0] < values[2]) return FALSE;
	*bytes = (ULONGLONG)(values[0] - values[2]) * values[1];
	return TRUE;
}

/* FULL auto-vacuum shrinks the database at commit; checkpoint publishes it to disk. */
BOOL db_history_trim_size(const ULONGLONG max_bytes)
{
	sqlite3_stmt *oldest = NULL;
	ULONGLONG bytes;
	BOOL ok = FALSE, transaction = FALSE, exhausted = FALSE, changed = FALSE;
	if (db == NULL || max_bytes == 0 || !sqlite3_get_autocommit(db) || !db_history_size(FALSE, &bytes)) return FALSE;
	if (bytes <= max_bytes) return sqlite3_wal_checkpoint_v2(db, NULL, SQLITE_CHECKPOINT_TRUNCATE, NULL, NULL) == SQLITE_OK;
	if (sqlite3_prepare_v2(db, "SELECT id FROM items WHERE pinned=0 ORDER BY created_at,id LIMIT 1;", -1, &oldest, NULL) != SQLITE_OK) return FALSE;
	while (bytes > max_bytes && !exhausted) {
		ULONGLONG before = bytes;
		if (sqlite3_exec(db, "BEGIN IMMEDIATE;", NULL, NULL, NULL) != SQLITE_OK) goto done;
		transaction = TRUE;
		if (!db_prune_image_edits() || !db_history_size(TRUE, &bytes)) goto done;
		while (bytes > max_bytes) {
			int id, rc;
			sqlite3_reset(oldest);
			rc = sqlite3_step(oldest);
			if (rc == SQLITE_DONE) { exhausted = TRUE; break; }
			if (rc != SQLITE_ROW) goto done;
			id = sqlite3_column_int(oldest, 0);
			sqlite3_reset(oldest);
			if (!db_history_delete_item(id) || !db_prune_image_edits() || !db_history_size(TRUE, &bytes)) goto done;
		}
		if (sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK) goto done;
		transaction = FALSE; changed = TRUE;
		if (!db_history_size(FALSE, &bytes)) goto done;
		if (bytes >= before) break;
	}
	/* A busy reader may delay shrinking the main file/WAL; never delete extra History for that. */
	ok = sqlite3_wal_checkpoint_v2(db, NULL, SQLITE_CHECKPOINT_TRUNCATE, NULL, NULL) == SQLITE_OK && bytes <= max_bytes;
 done:
	sqlite3_finalize(oldest);
	if (transaction) sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
	if (changed) db_sync_trimmed_history();
	if (!ok) OutputDebugString(TEXT("CLCL: database size cap could not be reached; protected data or a busy reader may remain.\n"));
	return ok;
}

/* Keep the latest max_count unpinned items, then enforce the disk-size budget. */
BOOL db_history_trim(const int max_count)
{
	const char *deletes[] = {
		"DELETE FROM item_formats WHERE item_id IN (SELECT id FROM items WHERE pinned=0 ORDER BY created_at DESC,id DESC LIMIT -1 OFFSET ?);",
		"DELETE FROM history_fts WHERE docid IN (SELECT id FROM items WHERE pinned=0 ORDER BY created_at DESC,id DESC LIMIT -1 OFFSET ?);",
		"DELETE FROM items WHERE pinned=0 AND id IN (SELECT id FROM items WHERE pinned=0 ORDER BY created_at DESC,id DESC LIMIT -1 OFFSET ?);"
	};
	sqlite3_stmt *stmt = NULL;
	BOOL own_transaction;
	int i, rc;
	if (db == NULL || max_count <= 0) return FALSE;
	own_transaction = sqlite3_get_autocommit(db) != 0;
	if (sqlite3_exec(db, "SAVEPOINT trim_history;", NULL, NULL, NULL) != SQLITE_OK) return FALSE;
	for (i = 0; i < ARRAYSIZE(deletes); i++) {
		rc = sqlite3_prepare_v2(db, deletes[i], -1, &stmt, NULL);
		if (rc != SQLITE_OK) goto rollback;
		sqlite3_bind_int(stmt, 1, max_count);
		rc = sqlite3_step(stmt);
		sqlite3_finalize(stmt); stmt = NULL;
		if (rc != SQLITE_DONE) goto rollback;
	}
	if (!db_prune_image_edits() || sqlite3_exec(db, "RELEASE trim_history;", NULL, NULL, NULL) != SQLITE_OK) goto rollback;
	if (!own_transaction) return TRUE;
	db_sync_trimmed_history();
	return db_history_trim_size(DB_HISTORY_MAX_BYTES);
 rollback:
	sqlite3_finalize(stmt);
	sqlite3_exec(db, "ROLLBACK TO trim_history; RELEASE trim_history;", NULL, NULL, NULL);
	return FALSE;
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
