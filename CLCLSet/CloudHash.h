/*
 * CLCLSet
 *
 * CloudHash.h
 *
 * Content hashing and filename parsing for cloud synchronization.
 */

#ifndef _INC_CLOUDHASH_H
#define _INC_CLOUDHASH_H

#include <windows.h>
#include <tchar.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CLOUD_HASH_LEN          16
#define CLOUD_HASH_STR_SIZE     (CLOUD_HASH_LEN + 1)
#define CLOUD_DATE_STR_SIZE     16   // YYYYMMDD_HHMMSS + null
#define CLOUD_MAX_ITEM_SIZE     (1024 * 1024) // 1 MB limit

/*
 * Hash set entry for export deduplication
 */
typedef struct _HASH_SET_ENTRY {
	TCHAR hash[CLOUD_HASH_STR_SIZE];
	struct _HASH_SET_ENTRY *next;
} HASH_SET_ENTRY;

typedef struct _HASH_SET {
	HASH_SET_ENTRY *buckets[256];
	int count;
} HASH_SET;

/* Function prototypes */
BOOL cloud_calc_text_hash(const char *utf8_text, DWORD len, TCHAR *hash_out);
BOOL cloud_calc_data_hash(HANDLE hData, DWORD size, const TCHAR *format_name, TCHAR *hash_out, char **utf8_out, DWORD *utf8_len_out);

BOOL cloud_format_filename(const SYSTEMTIME *st, const TCHAR *hash_str, TCHAR *filename_out, DWORD max_len);
BOOL cloud_parse_filename(const TCHAR *filename, SYSTEMTIME *st_out, TCHAR *hash_out);

/* Hash set management for export deduplication */
HASH_SET *hash_set_create(void);
void hash_set_free(HASH_SET *set);
BOOL hash_set_contains(HASH_SET *set, const TCHAR *hash_str);
BOOL hash_set_add(HASH_SET *set, const TCHAR *hash_str);

#ifdef __cplusplus
}
#endif

#endif /* _INC_CLOUDHASH_H */
