/*
 * CLCL
 *
 * DbHistory.h - SQLite-backed history storage & fast indexed search
 */

#ifndef _INC_DBHISTORY_H
#define _INC_DBHISTORY_H

#include "Data.h"

// Search result item
typedef struct _DB_SEARCH_RESULT {
	int id;
	TCHAR *title;
	TCHAR *window_title;
	TCHAR *snippet;
	DATA_INFO *di;
} DB_SEARCH_RESULT;

#ifdef OPTION_SET

static __inline BOOL db_history_init(const TCHAR *work_path) { return FALSE; }
static __inline void db_history_close(void) {}
static __inline BOOL db_history_is_open(void) { return FALSE; }
static __inline BOOL db_history_save_item(DATA_INFO *item) { return FALSE; }
static __inline BOOL db_history_update_item(DATA_INFO *item) { return FALSE; }
static __inline BOOL db_history_delete_item(const int id) { return FALSE; }
static __inline BOOL db_history_trim(const int max_count) { return FALSE; }
static __inline int db_history_load_recent(const int limit, DATA_INFO **out_root) { return 0; }
static __inline DATA_INFO *db_history_get_item(const int id) { return NULL; }
static __inline BOOL db_history_ensure_item_data(DATA_INFO *item) { return FALSE; }
static __inline int db_history_search(const TCHAR *query, const int max_results, DB_SEARCH_RESULT **out_results) { return 0; }
static __inline void db_history_free_search_results(DB_SEARCH_RESULT *results, const int count) {}
static __inline BOOL db_history_migrate_from_dat(const TCHAR *dat_path) { return FALSE; }

#else

/* Function Prototypes */
BOOL db_history_init(const TCHAR *work_path);
void db_history_close(void);
BOOL db_history_is_open(void);

BOOL db_history_save_item(DATA_INFO *item);
BOOL db_history_update_item(DATA_INFO *item);
BOOL db_history_delete_item(const int id);
BOOL db_history_trim(const int max_count);

int db_history_load_recent(const int limit, DATA_INFO **out_root);
DATA_INFO *db_history_get_item(const int id);
BOOL db_history_ensure_item_data(DATA_INFO *item);

int db_history_search(const TCHAR *query, const int max_results, DB_SEARCH_RESULT **out_results);
void db_history_free_search_results(DB_SEARCH_RESULT *results, const int count);

BOOL db_history_migrate_from_dat(const TCHAR *dat_path);

#endif

#endif	// _INC_DBHISTORY_H
