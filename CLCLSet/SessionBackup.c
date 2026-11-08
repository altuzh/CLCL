/* Session ZIP v1. The SQLite snapshot also holds a typed, ordered Favourites tree. */
#define _CRT_SECURE_NO_WARNINGS
#define _INC_OLE
#include <windows.h>
#undef _INC_OLE
#include <tchar.h>
#include <stdio.h>
#include <limits.h>
#include <winsqlite/winsqlite3.h>
#include "../General.h"
#include "../Data.h"
#include "../Memory.h"
#include "../File.h"
#include "../Format.h"
#include "../ClipBoard.h"
#include "../Installer/unzip.h"
#include "SessionBackup.h"

#define SESSION_MAX (512u * 1024u * 1024u)
#define SESSION_NODES 100000
#define SESSION_APP_ID 1129071443 /* CLCS */

static BOOL session_fail(TCHAR *error, const TCHAR *message)
{
    if (error != NULL && *error == 0) lstrcpyn(error, message, BUF_SIZE);
    return FALSE;
}

static BOOL session_temp(TCHAR *path)
{
    TCHAR directory[MAX_PATH];
    DWORD n = GetTempPath(MAX_PATH, directory);
    return n > 0 && n < MAX_PATH && GetTempFileName(directory, TEXT("clc"), 0, path) != 0;
}

/* ZIP method 0: payloads are already in their native representation. */
static DWORD session_crc(const BYTE *p, DWORD size)
{
    DWORD crc = ~0u, i;
    int bit;
    for (i = 0; i < size; i++) {
        crc ^= p[i];
        for (bit = 0; bit < 8; bit++) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

static void session_word(BYTE *p, WORD value) { p[0] = (BYTE)value; p[1] = (BYTE)(value >> 8); }
static void session_dword(BYTE *p, DWORD value) { session_word(p, (WORD)value); session_word(p + 2, (WORD)(value >> 16)); }

static BOOL session_zip(const BYTE *db, DWORD size, BYTE **out, DWORD *out_size)
{
    const char name[] = "session.sqlite";
    DWORD n = sizeof(name) - 1, central = 30 + n + size, crc = session_crc(db, size);
    BYTE *p = mem_calloc(central + 46 + n + 22), *c, *e;
    if (p == NULL) return FALSE;
    session_dword(p, 0x04034b50); session_word(p + 4, 20);
    session_dword(p + 14, crc); session_dword(p + 18, size); session_dword(p + 22, size);
    session_word(p + 26, (WORD)n); memcpy(p + 30, name, n); memcpy(p + 30 + n, db, size);
    c = p + central;
    session_dword(c, 0x02014b50); session_word(c + 4, 20); session_word(c + 6, 20);
    session_dword(c + 16, crc); session_dword(c + 20, size); session_dword(c + 24, size);
    session_word(c + 28, (WORD)n); memcpy(c + 46, name, n);
    e = c + 46 + n;
    session_dword(e, 0x06054b50); session_word(e + 8, 1); session_word(e + 10, 1);
    session_dword(e + 12, 46 + n); session_dword(e + 16, central);
    *out = p; *out_size = central + 46 + n + 22;
    return TRUE;
}

static BOOL session_copy_db(sqlite3 *source, sqlite3 *target)
{
    sqlite3_backup *backup = sqlite3_backup_init(target, "main", source, "main");
    int rc, tries = 0;
    if (backup == NULL) return FALSE;
    do {
        rc = sqlite3_backup_step(backup, 256);
        if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED) Sleep(25);
    } while (rc == SQLITE_OK || ((rc == SQLITE_BUSY || rc == SQLITE_LOCKED) && ++tries < 200));
    return sqlite3_backup_finish(backup) == SQLITE_OK && rc == SQLITE_DONE;
}

static void session_bind_text(sqlite3_stmt *s, int index, const TCHAR *value)
{
    if (value != NULL) sqlite3_bind_text16(s, index, value, -1, SQLITE_TRANSIENT);
    else sqlite3_bind_null(s, index);
}

static BOOL session_write_nodes(sqlite3 *db, const DATA_INFO *node, int parent, int depth, int *id)
{
    sqlite3_stmt *s = NULL;
    int own, rc;
    if (depth > 64) return FALSE;
    for (; node != NULL; node = node->next) {
        BYTE *bytes = NULL;
        DWORD size = 0;
        if (++*id > SESSION_NODES) return FALSE;
        own = *id;
        if (node->type == TYPE_DATA && node->data != NULL) {
            bytes = format_data_to_bytes(node, &size);
            if (bytes == NULL) bytes = clipboard_data_to_bytes(node, &size);
            if (bytes == NULL || size > SESSION_MAX) { mem_free((void **)&bytes); return FALSE; }
        }
        rc = sqlite3_prepare_v2(db, "INSERT INTO favourites VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);", -1, &s, NULL);
        if (rc == SQLITE_OK) {
            sqlite3_bind_int(s, 1, own); sqlite3_bind_int(s, 2, parent); sqlite3_bind_int(s, 3, node->type);
            session_bind_text(s, 4, node->title); session_bind_text(s, 5, node->format_name);
            sqlite3_bind_int64(s, 6, ((sqlite3_int64)node->modified.dwHighDateTime << 32) | node->modified.dwLowDateTime);
            session_bind_text(s, 7, node->window_name); session_bind_text(s, 8, node->plugin_string);
            sqlite3_bind_int(s, 9, (int)node->plugin_param); sqlite3_bind_int64(s, 10, node->op_modifiers);
            sqlite3_bind_int64(s, 11, node->op_virtkey); sqlite3_bind_int(s, 12, node->op_paste);
            sqlite3_bind_int(s, 13, node->image_edit_id); sqlite3_bind_int64(s, 14, size);
            if (bytes != NULL) sqlite3_bind_blob(s, 15, bytes, (int)size, SQLITE_TRANSIENT);
            rc = sqlite3_step(s);
        }
        sqlite3_finalize(s); s = NULL; mem_free((void **)&bytes);
        if (rc != SQLITE_DONE || !session_write_nodes(db, node->child, own, depth + 1, id)) return FALSE;
    }
    return TRUE;
}

BOOL session_backup_export(const TCHAR *directory, BYTE **bytes, DWORD *size, TCHAR *error)
{
    TCHAR path[MAX_PATH], temporary[MAX_PATH] = {0};
    sqlite3 *source = NULL, *snapshot = NULL;
    DATA_INFO *root = NULL;
    BYTE *payload = NULL;
    DWORD length = 0;
    int id = 0;
    BOOL ok = FALSE;
    *bytes = NULL; *size = 0; *error = 0;
    if (!session_temp(temporary)) goto done;
    _sntprintf_s(path, MAX_PATH, _TRUNCATE, TEXT("%s\\history.db"), directory);
    if (!file_check_file(path) || sqlite3_open16(path, &source) != SQLITE_OK ||
        sqlite3_open16(temporary, &snapshot) != SQLITE_OK) goto done;
    sqlite3_busy_timeout(source, 5000);
    if (!session_copy_db(source, snapshot)) goto done;
    sqlite3_close(source); source = NULL;
    _sntprintf_s(path, MAX_PATH, _TRUNCATE, TEXT("%s\\%s"), directory, REGIST_FILENAME);
    if (!file_read_data(path, &root, error)) goto done;
    if (sqlite3_exec(snapshot, "PRAGMA journal_mode=DELETE; PRAGMA application_id=1129071443; PRAGMA user_version=1;"
        "DROP TABLE IF EXISTS favourites; CREATE TABLE favourites("
        "id INTEGER PRIMARY KEY,parent INTEGER,kind INTEGER,title TEXT,format TEXT,modified INTEGER,"
        "window TEXT,plugin TEXT,param INTEGER,modifiers INTEGER,vkey INTEGER,paste INTEGER,edit INTEGER,size INTEGER,data BLOB);"
        "BEGIN;", NULL, NULL, NULL) != SQLITE_OK || !session_write_nodes(snapshot, root, 0, 0, &id) ||
        sqlite3_exec(snapshot, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK) goto done;
    /* A missing layer must fail the session, rather than silently flatten an edited Favourite. */
    {
        sqlite3_stmt *s = NULL;
        int rc = sqlite3_prepare_v2(snapshot, "SELECT 1 FROM favourites WHERE edit>0 AND edit NOT IN (SELECT id FROM image_edits) LIMIT 1;", -1, &s, NULL);
        if (rc == SQLITE_OK) rc = sqlite3_step(s);
        sqlite3_finalize(s);
        if (rc != SQLITE_DONE) goto done;
    }
    sqlite3_close(snapshot); snapshot = NULL;
    {
        WIN32_FILE_ATTRIBUTE_DATA attr;
        if (!GetFileAttributesEx(temporary, GetFileExInfoStandard, &attr) || attr.nFileSizeHigh || attr.nFileSizeLow > SESSION_MAX) {
            session_fail(error, TEXT("Backup exceeds the 512 MB session limit.")); goto done;
        }
    }
    payload = file_read_buf(temporary, &length, error);
    if (payload != NULL) ok = session_zip(payload, length, bytes, size);
done:
    if (source != NULL) sqlite3_close(source);
    if (snapshot != NULL) sqlite3_close(snapshot);
    data_free(root); mem_free((void **)&payload);
    if (*temporary) DeleteFile(temporary);
    if (!ok) session_fail(error, TEXT("Could not create a complete backup session."));
    return ok;
}

static BOOL session_bind_column(sqlite3_stmt *to, int parameter, sqlite3_stmt *from, int column)
{
    int rc;
    switch (sqlite3_column_type(from, column)) {
    case SQLITE_NULL: rc = sqlite3_bind_null(to, parameter); break;
    case SQLITE_INTEGER: rc = sqlite3_bind_int64(to, parameter, sqlite3_column_int64(from, column)); break;
    case SQLITE_TEXT:
        rc = sqlite3_bind_text(to, parameter, (const char *)sqlite3_column_text(from, column), sqlite3_column_bytes(from, column), SQLITE_TRANSIENT); break;
    case SQLITE_BLOB:
        rc = sqlite3_bind_blob(to, parameter, sqlite3_column_blob(from, column), sqlite3_column_bytes(from, column), SQLITE_TRANSIENT); break;
    default: return FALSE;
    }
    return rc == SQLITE_OK;
}

static int session_map_edit(sqlite3 *db, int old)
{
    sqlite3_stmt *s = NULL;
    int result = -1;
    if (old == 0) return 0;
    if (sqlite3_prepare_v2(db, "SELECT replacement FROM session_edits WHERE original=?;", -1, &s, NULL) == SQLITE_OK) {
        sqlite3_bind_int(s, 1, old);
        if (sqlite3_step(s) == SQLITE_ROW) result = sqlite3_column_int(s, 0);
    }
    sqlite3_finalize(s);
    return result;
}

static BOOL session_import_layers(sqlite3 *source, sqlite3 *local)
{
    sqlite3_stmt *rows = NULL, *match = NULL, *insert = NULL, *map = NULL;
    int rc = SQLITE_ERROR, i;
    BOOL ok = FALSE;
    if (sqlite3_exec(local, "CREATE TEMP TABLE IF NOT EXISTS session_edits(original INTEGER PRIMARY KEY,replacement INTEGER); DELETE FROM session_edits;", NULL, NULL, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(source, "SELECT id,initial,merged,base,annotations FROM image_edits;", -1, &rows, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(local, "SELECT id FROM image_edits WHERE initial IS ? AND merged IS ? AND base IS ? AND annotations IS ? LIMIT 1;", -1, &match, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(local, "INSERT INTO image_edits(initial,merged,base,annotations) VALUES(?,?,?,?);", -1, &insert, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(local, "INSERT INTO session_edits VALUES(?,?);", -1, &map, NULL) != SQLITE_OK) goto done;
    while ((rc = sqlite3_step(rows)) == SQLITE_ROW) {
        sqlite3_int64 id;
        if (sqlite3_column_int64(rows, 0) <= 0 || sqlite3_column_int64(rows, 0) > INT_MAX ||
            sqlite3_column_bytes(rows, 1) < 8 || sqlite3_column_bytes(rows, 2) < 8 || sqlite3_column_type(rows, 4) != SQLITE_TEXT) goto done;
        sqlite3_reset(match); sqlite3_reset(insert); sqlite3_reset(map);
        for (i = 1; i <= 4; i++) if (!session_bind_column(match, i, rows, i) || !session_bind_column(insert, i, rows, i)) goto done;
        {
            int found = sqlite3_step(match);
            if (found == SQLITE_ROW) id = sqlite3_column_int64(match, 0);
            else if (found == SQLITE_DONE && sqlite3_step(insert) == SQLITE_DONE) id = sqlite3_last_insert_rowid(local);
            else goto done;
        }
        if (id <= 0 || id > INT_MAX) goto done;
        sqlite3_bind_int64(map, 1, sqlite3_column_int64(rows, 0)); sqlite3_bind_int64(map, 2, id);
        if (sqlite3_step(map) != SQLITE_DONE) goto done;
    }
    ok = rc == SQLITE_DONE;
done:
    sqlite3_finalize(rows); sqlite3_finalize(match); sqlite3_finalize(insert); sqlite3_finalize(map);
    return ok;
}

static BOOL session_formats_equal(sqlite3 *source, sqlite3_int64 old, sqlite3 *local, sqlite3_int64 id)
{
    sqlite3_stmt *a = NULL, *b = NULL;
    int ra, rb, column;
    BOOL equal = FALSE;
    const char *sql = "SELECT format_name,format_id,data_size,data FROM item_formats WHERE item_id=? ORDER BY format_name;";
    if (sqlite3_prepare_v2(source, sql, -1, &a, NULL) != SQLITE_OK || sqlite3_prepare_v2(local, sql, -1, &b, NULL) != SQLITE_OK) goto done;
    sqlite3_bind_int64(a, 1, old); sqlite3_bind_int64(b, 1, id);
    for (;;) {
        ra = sqlite3_step(a); rb = sqlite3_step(b);
        if (ra == SQLITE_DONE && rb == SQLITE_DONE) { equal = TRUE; break; }
        if (ra != SQLITE_ROW || rb != SQLITE_ROW) break;
        for (column = 0; column < 4; column++) {
            int n = sqlite3_column_bytes(a, column);
            if (sqlite3_column_type(a, column) != sqlite3_column_type(b, column) || n != sqlite3_column_bytes(b, column) ||
                (n && memcmp(sqlite3_column_blob(a, column), sqlite3_column_blob(b, column), n))) goto done;
        }
    }
done:
    sqlite3_finalize(a); sqlite3_finalize(b);
    return equal;
}

static BOOL session_import_history(sqlite3 *source, sqlite3 *local, int *added)
{
    sqlite3_stmt *rows = NULL, *match = NULL, *insert = NULL, *formats = NULL, *fmt_insert = NULL, *fts = NULL;
    int rc, i;
    BOOL ok = FALSE;
    if (sqlite3_prepare_v2(source, "SELECT id,created_at,window_title,title,pinned,total_size,text_content,content_hash,image_edit_id FROM items ORDER BY id;", -1, &rows, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(local, "SELECT id FROM items WHERE created_at=? AND window_title IS ? AND title IS ? AND pinned IS ? AND total_size IS ? AND text_content IS ? AND content_hash IS ? AND image_edit_id=?;", -1, &match, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(local, "INSERT INTO items(created_at,window_title,title,pinned,total_size,text_content,content_hash,image_edit_id) VALUES(?,?,?,?,?,?,?,?);", -1, &insert, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(source, "SELECT format_name,format_id,data_size,data FROM item_formats WHERE item_id=?;", -1, &formats, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(local, "INSERT INTO item_formats(item_id,format_name,format_id,data_size,data) VALUES(?,?,?,?,?);", -1, &fmt_insert, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(local, "INSERT INTO history_fts(docid,title,text_content,window_title) VALUES(?,?,?,?);", -1, &fts, NULL) != SQLITE_OK) goto done;
    while ((rc = sqlite3_step(rows)) == SQLITE_ROW) {
        int edit = session_map_edit(local, sqlite3_column_int(rows, 8)), found;
        sqlite3_int64 id, old = sqlite3_column_int64(rows, 0);
        BOOL duplicate = FALSE;
        if (edit < 0 || sqlite3_column_type(rows, 3) != SQLITE_TEXT) goto done;
        sqlite3_reset(match); sqlite3_reset(insert);
        for (i = 1; i <= 7; i++) if (!session_bind_column(match, i, rows, i) || !session_bind_column(insert, i, rows, i)) goto done;
        sqlite3_bind_int(match, 8, edit); sqlite3_bind_int(insert, 8, edit);
        while ((found = sqlite3_step(match)) == SQLITE_ROW) {
            if (session_formats_equal(source, old, local, sqlite3_column_int64(match, 0))) { duplicate = TRUE; break; }
        }
        if (found != SQLITE_ROW && found != SQLITE_DONE) goto done;
        sqlite3_reset(match);
        if (duplicate) continue;
        if (sqlite3_step(insert) != SQLITE_DONE) goto done;
        id = sqlite3_last_insert_rowid(local);
        sqlite3_reset(formats); sqlite3_bind_int64(formats, 1, old);
        while ((found = sqlite3_step(formats)) == SQLITE_ROW) {
            sqlite3_reset(fmt_insert); sqlite3_bind_int64(fmt_insert, 1, id);
            for (i = 0; i < 4; i++) if (!session_bind_column(fmt_insert, i + 2, formats, i)) goto done;
            if (sqlite3_step(fmt_insert) != SQLITE_DONE) goto done;
        }
        if (found != SQLITE_DONE) goto done;
        sqlite3_reset(fts); sqlite3_bind_int64(fts, 1, id);
        if (!session_bind_column(fts, 2, rows, 3) || !session_bind_column(fts, 3, rows, 6) || !session_bind_column(fts, 4, rows, 2) || sqlite3_step(fts) != SQLITE_DONE) goto done;
        ++*added;
    }
    ok = rc == SQLITE_DONE;
done:
    sqlite3_finalize(rows); sqlite3_finalize(match); sqlite3_finalize(insert);
    sqlite3_finalize(formats); sqlite3_finalize(fmt_insert); sqlite3_finalize(fts);
    return ok;
}

/* Validate native payload sizes before passing cloud bytes to Win32/format codecs. */
static BOOL session_valid_payload(UINT format, const BYTE *bytes, DWORD size)
{
    if (format == CF_UNICODETEXT) return size >= 2 && !(size & 1) && bytes[size - 1] == 0 && bytes[size - 2] == 0;
    if (format == CF_TEXT || format == CF_OEMTEXT) return size > 0 && bytes[size - 1] == 0;
    if (format == CF_METAFILEPICT || format == CF_DSPMETAFILEPICT) return size > sizeof(METAFILEPICT);
    if (format == CF_PALETTE) {
        const LOGPALETTE *p = (const LOGPALETTE *)bytes;
        return size >= 4 && (ULONGLONG)4 + (ULONGLONG)p->palNumEntries * sizeof(PALETTEENTRY) <= size;
    }
    if (format == CF_BITMAP || format == CF_DSPBITMAP || format == CF_DIB || format == CF_DIBV5) {
        const BITMAPINFOHEADER *b = (const BITMAPINFOHEADER *)bytes;
        ULONGLONG stride, colors, height;
        if (size < sizeof(*b) || b->biSize < sizeof(*b) || b->biSize > size || b->biWidth <= 0 || b->biHeight == 0 ||
            b->biHeight == LONG_MIN || b->biPlanes != 1 ||
            (b->biCompression != BI_RGB && !(b->biCompression == BI_BITFIELDS && (b->biBitCount == 16 || b->biBitCount == 32))) ||
            (b->biBitCount != 1 && b->biBitCount != 4 && b->biBitCount != 8 && b->biBitCount != 16 && b->biBitCount != 24 && b->biBitCount != 32)) return FALSE;
        colors = b->biClrUsed ? b->biClrUsed : (b->biBitCount <= 8 ? 1u << b->biBitCount :
            (b->biSize == sizeof(*b) && (b->biCompression == BI_BITFIELDS || ((format == CF_BITMAP || format == CF_DSPBITMAP) && (b->biBitCount == 16 || b->biBitCount == 32)))) ? 3 : 0);
        stride = (((ULONGLONG)b->biWidth * b->biBitCount + 31) / 32) * 4;
        height = b->biHeight < 0 ? -(LONGLONG)b->biHeight : b->biHeight;
        return (ULONGLONG)b->biSize + colors * sizeof(RGBQUAD) + stride * height <= size;
    }
    return TRUE;
}

static BOOL session_read_nodes(sqlite3 *source, sqlite3 *local, int parent, int depth, int parent_type, DATA_INFO **out, int *count)
{
    sqlite3_stmt *s = NULL;
    DATA_INFO **tail = out;
    int rc = SQLITE_ERROR;
    BOOL ok = FALSE;
    if (depth > 64 || sqlite3_prepare_v2(source, "SELECT id,kind,title,format,modified,window,plugin,param,modifiers,vkey,paste,edit,size,data FROM favourites WHERE parent=? ORDER BY id;", -1, &s, NULL) != SQLITE_OK) goto done;
    sqlite3_bind_int(s, 1, parent);
    while ((rc = sqlite3_step(s)) == SQLITE_ROW) {
        int type = sqlite3_column_int(s, 1), id = sqlite3_column_int(s, 0);
        DATA_INFO *node;
        sqlite3_int64 ft = sqlite3_column_int64(s, 4);
        const TCHAR *title = (const TCHAR *)sqlite3_column_text16(s, 2);
        if (++*count > SESSION_NODES || id <= parent || (parent_type == TYPE_ITEM ? type != TYPE_DATA :
            parent_type == TYPE_DATA || (type != TYPE_ITEM && type != TYPE_FOLDER))) goto done;
        if (type == TYPE_DATA) {
            const TCHAR *name = (const TCHAR *)sqlite3_column_text16(s, 3);
            DWORD size = (DWORD)sqlite3_column_bytes(s, 13);
            UINT format;
            HANDLE data = NULL;
            const BYTE *blob = sqlite3_column_blob(s, 13);
            if (name == NULL || !*name || sqlite3_column_int64(s, 12) != size || size > SESSION_MAX) goto done;
            format = clipboard_get_format(0, (TCHAR *)name);
            if (size) {
                if (!session_valid_payload(format, blob, size)) goto done;
                data = format_bytes_to_data(name, blob, &size);
                if (data == NULL) data = clipboard_bytes_to_data((TCHAR *)name, blob, &size);
                if (data == NULL) goto done;
            }
            node = data_create_data(format, (TCHAR *)name, data, size, FALSE, NULL);
            if (node == NULL) {
                if (!format_free_data(name, data)) clipboard_free_data((TCHAR *)name, data);
                goto done;
            }
            node->title = alloc_copy(title);
        } else node = type == TYPE_FOLDER ? data_create_folder(title, NULL) : data_create_item(title, FALSE, NULL);
        if (node == NULL) goto done;
        *tail = node; tail = &node->next;
        node->modified.dwHighDateTime = (DWORD)(ft >> 32); node->modified.dwLowDateTime = (DWORD)ft;
        node->window_name = alloc_copy((const TCHAR *)sqlite3_column_text16(s, 5));
        node->plugin_string = alloc_copy((const TCHAR *)sqlite3_column_text16(s, 6));
        node->plugin_param = (LPARAM)sqlite3_column_int(s, 7); node->op_modifiers = (UINT)sqlite3_column_int64(s, 8);
        node->op_virtkey = (UINT)sqlite3_column_int64(s, 9); node->op_paste = sqlite3_column_int(s, 10);
        node->image_edit_id = session_map_edit(local, sqlite3_column_int(s, 11));
        if (node->image_edit_id < 0 || !session_read_nodes(source, local, id, depth + 1, type, &node->child, count)) goto done;
    }
    ok = rc == SQLITE_DONE;
done:
    sqlite3_finalize(s);
    return ok;
}

static BOOL session_same_text(const TCHAR *a, const TCHAR *b) { return a == b || (a && b && !lstrcmp(a, b)); }

static BOOL session_same_item(const DATA_INFO *a, const DATA_INFO *b)
{
    if (a->type != b->type || a->image_edit_id != b->image_edit_id || !session_same_text(a->title, b->title) ||
        !session_same_text(a->format_name, b->format_name) || !session_same_text(a->plugin_string, b->plugin_string) ||
        a->plugin_param != b->plugin_param || a->op_modifiers != b->op_modifiers || a->op_virtkey != b->op_virtkey || a->op_paste != b->op_paste) return FALSE;
    if (a->type == TYPE_DATA) {
        BYTE *x, *y;
        DWORD nx = 0, ny = 0;
        BOOL equal;
        if (a->data == NULL || b->data == NULL) return a->data == b->data;
        x = format_data_to_bytes(a, &nx); if (x == NULL) x = clipboard_data_to_bytes(a, &nx);
        y = format_data_to_bytes(b, &ny); if (y == NULL) y = clipboard_data_to_bytes(b, &ny);
        equal = x && y && nx == ny && !memcmp(x, y, nx);
        mem_free((void **)&x); mem_free((void **)&y);
        return equal;
    }
    a = a->child; b = b->child;
    for (; a && b; a = a->next, b = b->next) if (!session_same_item(a, b)) return FALSE;
    return a == b;
}

static void session_merge(DATA_INFO **local, DATA_INFO **incoming, int *added)
{
    while (*incoming != NULL) {
        DATA_INFO *node = *incoming, *match, **tail = local;
        *incoming = node->next; node->next = NULL;
        for (match = *local; match; match = match->next) {
            if (node->type == TYPE_FOLDER && match->type == TYPE_FOLDER && session_same_text(node->title, match->title)) break;
            if (node->type != TYPE_FOLDER && session_same_item(node, match)) break;
            tail = &match->next;
        }
        if (match != NULL) {
            if (node->type == TYPE_FOLDER) session_merge(&match->child, &node->child, added);
            data_free(node);
        } else { *tail = node; ++*added; }
    }
}

BOOL session_backup_import(const TCHAR *directory, const BYTE *bytes, DWORD size, int *history_added, int *favourites_added, TCHAR *error)
{
    ZIP_ARCHIVE zip;
    ZIP_ENTRY entry;
    BYTE *payload = NULL;
    DWORD length = 0;
    TCHAR temporary[MAX_PATH] = {0}, path[MAX_PATH];
    sqlite3 *source = NULL, *local = NULL;
    sqlite3_stmt *s = NULL;
    DATA_INFO *incoming = NULL, *root = NULL;
    int count = 0, total, version = 0, app = 0;
    BOOL ok = FALSE, transaction = FALSE;
    *history_added = *favourites_added = 0; *error = 0;
    if (size > SESSION_MAX + 256 || !zip_open(bytes, size, &zip) || zip.count != 1 || !zip_get_entry(&zip, 0, &entry) ||
        lstrcmp(entry.name, TEXT("session.sqlite")) || entry.orig_size > SESSION_MAX ||
        entry.local_offset > size || size - entry.local_offset < 30 ||
        (ULONGLONG)entry.local_offset + 30 + bytes[entry.local_offset + 26] +
        ((DWORD)bytes[entry.local_offset + 27] << 8) + bytes[entry.local_offset + 28] +
        ((DWORD)bytes[entry.local_offset + 29] << 8) + entry.comp_size > size ||
        zip_extract(&zip, &entry, &payload, &length) != UNZIP_OK) goto done;
    if (!session_temp(temporary) || !file_write_buf(temporary, payload, length, error) || sqlite3_open16(temporary, &source) != SQLITE_OK) goto done;
    sqlite3_limit(source, SQLITE_LIMIT_LENGTH, SESSION_MAX);
    if (sqlite3_exec(source, "PRAGMA query_only=ON; PRAGMA trusted_schema=OFF;", NULL, NULL, NULL) != SQLITE_OK) goto done;
    if (sqlite3_prepare_v2(source, "PRAGMA application_id;", -1, &s, NULL) == SQLITE_OK && sqlite3_step(s) == SQLITE_ROW) app = sqlite3_column_int(s, 0);
    sqlite3_finalize(s); s = NULL;
    if (sqlite3_prepare_v2(source, "PRAGMA user_version;", -1, &s, NULL) == SQLITE_OK && sqlite3_step(s) == SQLITE_ROW) version = sqlite3_column_int(s, 0);
    sqlite3_finalize(s); s = NULL;
    if (app != SESSION_APP_ID || version != 1) { session_fail(error, TEXT("Unsupported backup session format or version.")); goto done; }
    if (sqlite3_prepare_v2(source, "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name IN ('items','item_formats','image_edits','favourites');", -1, &s, NULL) != SQLITE_OK || sqlite3_step(s) != SQLITE_ROW || sqlite3_column_int(s, 0) != 4) goto done;
    sqlite3_finalize(s); s = NULL;
    if (sqlite3_prepare_v2(source, "SELECT COUNT(*) FROM favourites;", -1, &s, NULL) != SQLITE_OK || sqlite3_step(s) != SQLITE_ROW) goto done;
    total = sqlite3_column_int(s, 0);
    sqlite3_finalize(s); s = NULL;
    if (total < 0 || total > SESSION_NODES) goto done;
    _sntprintf_s(path, MAX_PATH, _TRUNCATE, TEXT("%s\\history.db"), directory);
    if (!file_check_file(path) || sqlite3_open16(path, &local) != SQLITE_OK) goto done;
    sqlite3_busy_timeout(local, 5000);
    _sntprintf_s(path, MAX_PATH, _TRUNCATE, TEXT("%s\\%s"), directory, REGIST_FILENAME);
    if (!file_read_data(path, &root, error) || sqlite3_exec(local, "BEGIN IMMEDIATE;", NULL, NULL, NULL) != SQLITE_OK) goto done;
    transaction = TRUE;
    if (!session_import_layers(source, local) || !session_read_nodes(source, local, 0, 0, TYPE_ROOT, &incoming, &count) ||
        count != total || !session_import_history(source, local, history_added)) goto done;
    session_merge(&root, &incoming, favourites_added);
    if (sqlite3_exec(local, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK) goto done;
    transaction = FALSE;
    /* Commit layers first: a failed file replacement leaves local Favourites intact. Retrying is idempotent. */
    if (!file_write_data(path, root, FALSE, error)) goto done;
    ok = TRUE;
done:
    sqlite3_finalize(s);
    if (transaction) sqlite3_exec(local, "ROLLBACK;", NULL, NULL, NULL);
    if (local != NULL) sqlite3_close(local);
    if (source != NULL) sqlite3_close(source);
    data_free(root); data_free(incoming);
    if (payload != NULL) HeapFree(GetProcessHeap(), 0, payload);
    if (*temporary) DeleteFile(temporary);
    if (!ok) session_fail(error, TEXT("Could not import the backup session. Existing Favourites were preserved."));
    return ok;
}
