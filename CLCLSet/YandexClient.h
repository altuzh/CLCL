/*
 * CLCLSet
 *
 * YandexClient.h
 *
 * Native WinHttp client for Yandex Disk REST API.
 * Replicated from iCloudFindAndroid architecture.
 */

#ifndef _INC_YANDEXCLIENT_H
#define _INC_YANDEXCLIENT_H

#include <windows.h>
#include <tchar.h>

#ifdef __cplusplus
extern "C" {
#endif

#define YANDEX_DEFAULT_CLIENT_ID        TEXT("8a6ce5d366014eb4b2b0ab1f65926a6b")
#define YANDEX_REDIRECT_URI             TEXT("https://oauth.yandex.ru/verification_code")
#define YANDEX_AUTH_URL_TEMPLATE        TEXT("https://oauth.yandex.ru/authorize?response_type=token&client_id=%s")

#define YANDEX_ROOT_FOLDER          TEXT("disk:/CLCL")
#define YANDEX_HISTORY_FOLDER       TEXT("disk:/CLCL/History")
#define YANDEX_FAVOURITES_FOLDER    TEXT("disk:/CLCL/Favourites")

typedef struct _YANDEX_RESOURCE_ITEM {
	TCHAR name[MAX_PATH];
	TCHAR path[MAX_PATH];
	BOOL is_dir;
	DWORD size;
	struct _YANDEX_RESOURCE_ITEM *next;
} YANDEX_RESOURCE_ITEM;

typedef struct _YANDEX_RESOURCE_LIST {
	YANDEX_RESOURCE_ITEM *head;
	int count;
} YANDEX_RESOURCE_LIST;

/* Yandex REST API functions */
BOOL yandex_test_connection(const TCHAR *token, TCHAR *user_name_out, DWORD max_len, TCHAR *err_str);
BOOL yandex_exchange_code_for_token(const TCHAR *code, TCHAR *token_out, DWORD max_len, TCHAR *err_str);
BOOL yandex_ensure_folder(const TCHAR *token, const TCHAR *path, TCHAR *err_str);
BOOL yandex_delete_resource(const TCHAR *token, const TCHAR *path, TCHAR *err_str);
BOOL yandex_get_upload_url(const TCHAR *token, const TCHAR *path, TCHAR *url_out, DWORD max_len, TCHAR *err_str);
BOOL yandex_upload_file(const TCHAR *upload_url, const char *data, DWORD size, TCHAR *err_str);
BOOL yandex_list_resources(const TCHAR *token, const TCHAR *path, YANDEX_RESOURCE_LIST *list_out, TCHAR *err_str);
BOOL yandex_download_file(const TCHAR *token, const TCHAR *path, char **buf_out, DWORD *size_out, TCHAR *err_str);

/* Resource list memory management */
void yandex_free_resource_list(YANDEX_RESOURCE_LIST *list);

#ifdef __cplusplus
}
#endif

#endif /* _INC_YANDEXCLIENT_H */
