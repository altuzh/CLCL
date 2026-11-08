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
#include "SessionBackup.h"
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

/* The cloud stores one complete ZIP per session. */
static DWORD WINAPI cloud_session_worker(LPVOID lpParam)
{
    CLOUD_WORKER_PARAMS *params = (CLOUD_WORKER_PARAMS *)lpParam;
    TCHAR error[BUF_SIZE] = {0}, status[BUF_SIZE], user[128], remote[1024], folder[512], url[2048];
    HWND main_window;
    BYTE *archive = NULL;
    DWORD size = 0;
    int history_added = 0, favourites_added = 0;
    BOOL ok = FALSE;
    SendMessage(params->hDlg, WM_CLOUD_PROGRESS, 0, (LPARAM)TEXT("Connecting to Yandex Disk..."));
    if (!yandex_test_connection(params->token, user, ARRAYSIZE(user), error)) goto done;
    main_window = FindWindow(MAIN_WND_CLASS, MAIN_WINDOW_TITLE);
    if (main_window != NULL) {
        if (!SendMessage(main_window, WM_HISTORY_SAVE, 0, 0) || !SendMessage(main_window, WM_REGIST_SAVE, 0, 0)) {
            lstrcpy(error, TEXT("Could not save the current session before backup/import.")); goto done;
        }
    }
    if (params->is_save) {
        SYSTEMTIME now;
        GetLocalTime(&now);
        _sntprintf(folder, ARRAYSIZE(folder), TEXT("%s/%04d-%02d-%02d %02d:%02d:%02d.%03d"),
            YANDEX_ROOT_FOLDER, now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
        SendMessage(params->hDlg, WM_CLOUD_PROGRESS, 0, (LPARAM)TEXT("Creating complete session ZIP..."));
        if (!session_backup_export(work_path, &archive, &size, error)) goto done;
        if (!yandex_ensure_folder(params->token, YANDEX_ROOT_FOLDER, error) ||
            !yandex_ensure_folder(params->token, folder, error)) goto done;
        _sntprintf(remote, ARRAYSIZE(remote), TEXT("%s/backup.zip"), folder);
        SendMessage(params->hDlg, WM_CLOUD_PROGRESS, 0, (LPARAM)TEXT("Uploading session ZIP..."));
        if (!yandex_get_upload_url(params->token, remote, url, ARRAYSIZE(url), error) ||
            !yandex_upload_file(url, (const char *)archive, size, error)) goto done;
        lstrcpy(status, TEXT("Backup session saved: History and Favourites, including images and editable layers."));
    } else {
        char *download = NULL;
        if (params->batch_path[0] == 0) { lstrcpy(error, TEXT("Select a backup session to import.")); goto done; }
        _sntprintf(remote, ARRAYSIZE(remote), TEXT("%s/backup.zip"), params->batch_path);
        SendMessage(params->hDlg, WM_CLOUD_PROGRESS, 0, (LPARAM)TEXT("Downloading session ZIP..."));
        if (!yandex_download_file(params->token, remote, &download, &size, error)) goto done;
        SendMessage(params->hDlg, WM_CLOUD_PROGRESS, 0, (LPARAM)TEXT("Importing History, Favourites and image layers..."));
        if (main_window != NULL) {
            TCHAR temporary[MAX_PATH] = {0}, temp_directory[MAX_PATH];
            COPYDATASTRUCT copy = {SESSION_COPYDATA, 0, temporary};
            DWORD n = GetTempPath(MAX_PATH, temp_directory);
            if (n > 0 && n < MAX_PATH && GetTempFileName(temp_directory, TEXT("clc"), 0, temporary) &&
                file_write_buf(temporary, (const BYTE *)download, size, error)) {
                free(download); download = NULL;
                copy.cbData = (DWORD)(lstrlen(temporary) + 1) * sizeof(TCHAR);
                ok = (BOOL)SendMessage(main_window, WM_COPYDATA, 0, (LPARAM)&copy);
            }
            if (*temporary) DeleteFile(temporary);
            if (!ok && !*error) lstrcpy(error, TEXT("Session import failed. Close any open clipboard menu and check that the ZIP is a valid CLCL session."));
        } else ok = session_backup_import(work_path, (const BYTE *)download, size, &history_added, &favourites_added, error);
        free(download);
        if (!ok) goto done;
        lstrcpy(status, TEXT("Session imported: History, Favourites, images and editable layers. Existing items were preserved and matching items skipped."));
    }
    ok = TRUE;
done:
    mem_free((void **)&archive);
    PostMessage(params->hDlg, WM_CLOUD_COMPLETE, ok, (LPARAM)alloc_copy(ok ? status :
        error[0] ? error : TEXT("Cloud session operation failed.")));
    free(params);
    return ok ? 0 : 1;
}

static DWORD WINAPI cloud_save_worker(LPVOID param)
{
    ((CLOUD_WORKER_PARAMS *)param)->is_save = TRUE;
    return cloud_session_worker(param);
}

static DWORD WINAPI cloud_import_worker(LPVOID param)
{
    ((CLOUD_WORKER_PARAMS *)param)->is_save = FALSE;
    return cloud_session_worker(param);
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
					continue;
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
					_sntprintf(dlg_params->selected_batch, dlg_params->batch_max_len, TEXT("%s/%s"), YANDEX_ROOT_FOLDER, sel_name);
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

						_sntprintf(full_batch_path, 512, TEXT("%s/%s"), YANDEX_ROOT_FOLDER, sel_name);
						yandex_delete_resource(dlg_params->token, full_batch_path, err_str);

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
					TEXT("Save History and Favourites, including images and editable layers, as one ZIP backup session in cloud?\r\n\r\nExisting backups will not be deleted."),
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
