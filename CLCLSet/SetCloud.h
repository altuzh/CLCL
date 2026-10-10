/*
 * CLCLSet
 *
 * SetCloud.h
 *
 * Yandex Disk Cloud Synchronization Settings & Operations
 */

#ifndef _INC_SETCLOUD_H
#define _INC_SETCLOUD_H

#include <windows.h>
#include <tchar.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Dialog procedure for Cloud Settings tab */
BOOL CALLBACK set_cloud_proc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

/* Secure token storage using DPAPI */
BOOL cloud_save_token(const TCHAR *ini_path, const TCHAR *token);
BOOL cloud_load_token(const TCHAR *ini_path, TCHAR *token_out, DWORD max_len);

#ifdef __cplusplus
}
#endif

#endif /* _INC_SETCLOUD_H */
