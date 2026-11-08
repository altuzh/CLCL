#ifndef CLCL_SESSION_BACKUP_H
#define CLCL_SESSION_BACKUP_H
#define SESSION_COPYDATA 0x434c4353u

/* One ZIP session: a consistent History snapshot plus the complete Favourites tree. */
BOOL session_backup_export(const TCHAR *directory, BYTE **bytes, DWORD *size, TCHAR *error);
BOOL session_backup_import(const TCHAR *directory, const BYTE *bytes, DWORD size,
    int *history_added, int *favourites_added, TCHAR *error);

#endif
