/*
 * CLCLSet
 *
 * YandexClient.c
 *
 * Native WinHttp client for Yandex Disk REST API.
 * Replicated from iCloudFindAndroid architecture.
 */

#define _CRT_SECURE_NO_WARNINGS
#define _INC_OLE
#include <windows.h>
#undef  _INC_OLE
#include <tchar.h>
#include <winhttp.h>
#include <stdio.h>
#include "..\General.h"
#include "YandexClient.h"

#pragma comment(lib, "winhttp.lib")

#define YANDEX_HOST       L"cloud-api.yandex.net"
#define YANDEX_USER_AGENT L"CLCL/2.2.0 (Windows; Win32)"

/*
 * json_extract_string - Helper to extract a string value for a given key from JSON
 */
static BOOL json_extract_string(const char *json, const char *key, char *val_out, DWORD max_len)
{
	char pattern[128];
	const char *p;
	const char *end;
	DWORD len;
	DWORD i = 0, o = 0;

	if (json == NULL || key == NULL || val_out == NULL || max_len == 0) {
		return FALSE;
	}

	_snprintf(pattern, sizeof(pattern), "\"%s\":", key);
	p = strstr(json, pattern);
	if (p == NULL) {
		_snprintf(pattern, sizeof(pattern), "\"%s\" :", key);
		p = strstr(json, pattern);
		if (p == NULL) {
			return FALSE;
		}
	}

	p += strlen(pattern);
	while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;

	if (*p != '\"') {
		return FALSE;
	}
	p++; // skip quote

	end = p;
	while (*end != '\"' && *end != '\0') {
		if (*end == '\\' && *(end + 1) != '\0') {
			end += 2;
		} else {
			end++;
		}
	}

	len = (DWORD)(end - p);
	if (len >= max_len) {
		len = max_len - 1;
	}

	/* Simple copy unescaping quotes and backslashes */
	while (i < len && o < max_len - 1) {
		if (p[i] == '\\' && (i + 1) < len) {
			i++;
			val_out[o++] = p[i++];
		} else {
			val_out[o++] = p[i++];
		}
	}
	val_out[o] = '\0';
	return TRUE;
}

/*
 * make_auth_header - Construct "Authorization: OAuth <token>" header
 */
static void make_auth_header(const TCHAR *token, WCHAR *header_out, DWORD max_len)
{
#ifdef UNICODE
	_snwprintf(header_out, max_len, L"Authorization: OAuth %s\r\n", token);
#else
	WCHAR wtoken[512];
	MultiByteToWideChar(CP_ACP, 0, token, -1, wtoken, 512);
	_snwprintf(header_out, max_len, L"Authorization: OAuth %s\r\n", wtoken);
#endif
}

/*
 * yandex_http_request - Send an HTTP request and get status code + response body
 */
static BOOL yandex_http_request(
	const WCHAR *verb,
	const WCHAR *host,
	INTERNET_PORT port,
	const WCHAR *path,
	const WCHAR *headers,
	const char *body,
	DWORD body_len,
	DWORD *status_code_out,
	char **resp_body_out,
	DWORD *resp_body_len_out)
{
	HINTERNET hSession = NULL;
	HINTERNET hConnect = NULL;
	HINTERNET hRequest = NULL;
	BOOL bResults = FALSE;
	DWORD dwStatusCode = 0;
	DWORD dwSize = sizeof(DWORD);
	char *response_buffer = NULL;
	DWORD total_bytes = 0;
	DWORD dwProtocols;
	DWORD dwDownloaded;
	char *new_buf;

	hSession = WinHttpOpen(YANDEX_USER_AGENT,
		WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
		WINHTTP_NO_PROXY_NAME,
		WINHTTP_NO_PROXY_BYPASS, 0);
	if (hSession == NULL) {
		goto cleanup;
	}

	// Enable TLS 1.2 / TLS 1.3
	dwProtocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
	WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &dwProtocols, sizeof(dwProtocols));

	hConnect = WinHttpConnect(hSession, host, port, 0);
	if (hConnect == NULL) {
		goto cleanup;
	}

	hRequest = WinHttpOpenRequest(hConnect, verb, path,
		NULL, WINHTTP_NO_REFERER,
		WINHTTP_DEFAULT_ACCEPT_TYPES,
		WINHTTP_FLAG_SECURE);
	if (hRequest == NULL) {
		goto cleanup;
	}

	bResults = WinHttpSendRequest(hRequest,
		headers, (headers != NULL) ? -1L : 0,
		(LPVOID)body, body_len, body_len, 0);
	if (!bResults) {
		goto cleanup;
	}

	bResults = WinHttpReceiveResponse(hRequest, NULL);
	if (!bResults) {
		goto cleanup;
	}

	WinHttpQueryHeaders(hRequest,
		WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		WINHTTP_HEADER_NAME_BY_INDEX,
		&dwStatusCode, &dwSize, WINHTTP_NO_HEADER_INDEX);

	if (status_code_out != NULL) {
		*status_code_out = dwStatusCode;
	}

	if (resp_body_out != NULL) {
		do {
			dwSize = 0;
			if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) {
				break;
			}
			if (dwSize == 0) {
				break;
			}

			new_buf = (char *)realloc(response_buffer, total_bytes + dwSize + 1);
			if (new_buf == NULL) {
				break;
			}
			response_buffer = new_buf;

			dwDownloaded = 0;
			if (WinHttpReadData(hRequest, response_buffer + total_bytes, dwSize, &dwDownloaded)) {
				total_bytes += dwDownloaded;
			}
		} while (dwSize > 0);

		if (response_buffer != NULL) {
			response_buffer[total_bytes] = '\0';
		}
		*resp_body_out = response_buffer;
		if (resp_body_len_out != NULL) {
			*resp_body_len_out = total_bytes;
		}
	}

cleanup:
	if (hRequest != NULL) WinHttpCloseHandle(hRequest);
	if (hConnect != NULL) WinHttpCloseHandle(hConnect);
	if (hSession != NULL) WinHttpCloseHandle(hSession);
	return bResults;
}

/*
 * yandex_test_connection - Check token and get user display name
 */
BOOL yandex_test_connection(const TCHAR *token, TCHAR *user_name_out, DWORD max_len, TCHAR *err_str)
{
	WCHAR auth_header[512];
	DWORD status = 0;
	char *body = NULL;
	BOOL ret = FALSE;
	char name_buf[128] = { 0 };
#ifndef UNICODE
	WCHAR wbuf[128];
#endif

	if (token == NULL || *token == TEXT('\0')) {
		if (err_str != NULL) lstrcpy(err_str, TEXT("Token is empty."));
		return FALSE;
	}

	make_auth_header(token, auth_header, 512);
	if (!yandex_http_request(L"GET", YANDEX_HOST, INTERNET_DEFAULT_HTTPS_PORT,
		L"/v1/disk/", auth_header, NULL, 0, &status, &body, NULL)) {
		if (err_str != NULL) lstrcpy(err_str, TEXT("Network connection error."));
		if (body != NULL) free(body);
		return FALSE;
	}

	if (status == 200 && body != NULL) {
		if (json_extract_string(body, "display_name", name_buf, sizeof(name_buf)) ||
			json_extract_string(body, "login", name_buf, sizeof(name_buf))) {
#ifdef UNICODE
			MultiByteToWideChar(CP_UTF8, 0, name_buf, -1, user_name_out, max_len);
#else
			MultiByteToWideChar(CP_UTF8, 0, name_buf, -1, wbuf, 128);
			WideCharToMultiByte(CP_ACP, 0, wbuf, -1, user_name_out, max_len, NULL, NULL);
#endif
		} else {
			lstrcpyn(user_name_out, TEXT("Connected User"), max_len);
		}
		ret = TRUE;
	} else {
		if (err_str != NULL) {
			_sntprintf(err_str, BUF_SIZE, TEXT("Yandex error (HTTP %u). Check your token."), status);
		}
	}

	if (body != NULL) free(body);
	return ret;
}

/*
 * yandex_exchange_code_for_token - Exchange verification code for OAuth access token
 */
BOOL yandex_exchange_code_for_token(const TCHAR *code, TCHAR *token_out, DWORD max_len, TCHAR *err_str)
{
	char post_data[1024];
	char code_utf8[128];
	char token_buf[512];
	DWORD status = 0;
	char *body = NULL;
	BOOL ret = FALSE;
	static const WCHAR content_headers[] = L"Content-Type: application/x-www-form-urlencoded\r\n";
	char err_desc[256];

	if (code == NULL || *code == TEXT('\0') || token_out == NULL || max_len == 0) {
		return FALSE;
	}

#ifdef UNICODE
	char client_id_utf8[64];
	WideCharToMultiByte(CP_UTF8, 0, code, -1, code_utf8, sizeof(code_utf8), NULL, NULL);
	WideCharToMultiByte(CP_UTF8, 0, YANDEX_DEFAULT_CLIENT_ID, -1, client_id_utf8, sizeof(client_id_utf8), NULL, NULL);
#else
	const char *client_id_utf8 = YANDEX_DEFAULT_CLIENT_ID;
	lstrcpynA(code_utf8, code, sizeof(code_utf8));
#endif

	_snprintf(post_data, sizeof(post_data),
		"grant_type=authorization_code&code=%s&client_id=%s",
		code_utf8, client_id_utf8);

	if (!yandex_http_request(L"POST", L"oauth.yandex.ru", INTERNET_DEFAULT_HTTPS_PORT,
		L"/token", content_headers, post_data, (DWORD)strlen(post_data), &status, &body, NULL)) {
		if (err_str != NULL) lstrcpy(err_str, TEXT("Network error during code exchange."));
		if (body != NULL) free(body);
		return FALSE;
	}

	token_buf[0] = '\0';
	if (status == 200 && body != NULL) {
		if (json_extract_string(body, "access_token", token_buf, sizeof(token_buf))) {
#ifdef UNICODE
			MultiByteToWideChar(CP_UTF8, 0, token_buf, -1, token_out, max_len);
#else
			lstrcpyn(token_out, token_buf, max_len);
#endif
			ret = TRUE;
		}
	} else {
		if (err_str != NULL) {
			err_desc[0] = '\0';
			if (body != NULL && (json_extract_string(body, "error_description", err_desc, sizeof(err_desc)) ||
				json_extract_string(body, "error", err_desc, sizeof(err_desc)))) {
#ifdef UNICODE
				MultiByteToWideChar(CP_UTF8, 0, err_desc, -1, err_str, BUF_SIZE);
#else
				lstrcpyn(err_str, err_desc, BUF_SIZE);
#endif
			} else {
				_sntprintf(err_str, BUF_SIZE, TEXT("Token exchange failed (HTTP %u)."), status);
			}
		}
	}

	if (body != NULL) free(body);
	return ret;
}

/*
 * url_encode_w - URL-encode a wide string (converting to UTF-8 percent-encoded bytes)
 */
static void url_encode_w(const WCHAR *src, WCHAR *dst, DWORD dst_max)
{
	char utf8[1024];
	int utf8_len;
	DWORD out_idx = 0;
	int i;
	unsigned char c;

	if (src == NULL || dst == NULL || dst_max == 0) return;
	dst[0] = L'\0';

	utf8_len = WideCharToMultiByte(CP_UTF8, 0, src, -1, utf8, sizeof(utf8) - 1, NULL, NULL);
	if (utf8_len <= 0) return;
	utf8[utf8_len] = '\0';

	for (i = 0; i < utf8_len && utf8[i] != '\0' && out_idx + 4 < dst_max; i++) {
		c = (unsigned char)utf8[i];
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
			c == '-' || c == '_' || c == '.' || c == '~') {
			dst[out_idx++] = (WCHAR)c;
		} else {
			_snwprintf(dst + out_idx, dst_max - out_idx, L"%%%02X", (unsigned int)c);
			out_idx += 3;
		}
	}
	if (out_idx < dst_max) dst[out_idx] = L'\0';
}

/*
 * yandex_wait_operation - Poll Yandex operation status until completion or timeout
 */
static BOOL yandex_wait_operation(const TCHAR *token, const char *op_href, DWORD timeout_ms)
{
	WCHAR auth_header[512];
	WCHAR req_path[1024] = { 0 };
	const char *path_start;
	DWORD start_time = GetTickCount();

	make_auth_header(token, auth_header, 512);

	path_start = strstr(op_href, "/v1/disk/operations/");
	if (path_start == NULL) {
		path_start = strchr(op_href, '/');
		if (path_start != NULL && *(path_start + 1) == '/') {
			path_start = strchr(path_start + 2, '/');
		}
	}
	if (path_start == NULL) return FALSE;

	MultiByteToWideChar(CP_UTF8, 0, path_start, -1, req_path, 1024);

	while (GetTickCount() - start_time < timeout_ms) {
		DWORD status = 0;
		char *body = NULL;
		char op_status[64] = { 0 };

		if (yandex_http_request(L"GET", YANDEX_HOST, INTERNET_DEFAULT_HTTPS_PORT,
			req_path, auth_header, NULL, 0, &status, &body, NULL)) {
			if (status == 200 && body != NULL) {
				if (json_extract_string(body, "status", op_status, sizeof(op_status))) {
					if (_stricmp(op_status, "success") == 0) {
						free(body);
						return TRUE;
					} else if (_stricmp(op_status, "failed") == 0) {
						free(body);
						return FALSE;
					}
				}
			}
			if (body != NULL) free(body);
		}
		Sleep(300);
	}
	return FALSE;
}

/*
 * yandex_ensure_folder - Create folder on Yandex Disk with retries for locked resources
 */
BOOL yandex_ensure_folder(const TCHAR *token, const TCHAR *path, TCHAR *err_str)
{
	WCHAR auth_header[512];
	WCHAR full_path[1024];
	WCHAR enc_path[512];
	WCHAR wpath[256];
	int retry;

	make_auth_header(token, auth_header, 512);

#ifdef UNICODE
	lstrcpyn(wpath, path, 256);
#else
	MultiByteToWideChar(CP_ACP, 0, path, -1, wpath, 256);
#endif
	url_encode_w(wpath, enc_path, 512);
	_snwprintf(full_path, 1024, L"/v1/disk/resources?path=%s", enc_path);

	for (retry = 0; retry < 12; retry++) {
		DWORD status = 0;
		char *body = NULL;

		if (!yandex_http_request(L"PUT", YANDEX_HOST, INTERNET_DEFAULT_HTTPS_PORT,
			full_path, auth_header, NULL, 0, &status, &body, NULL)) {
			if (err_str != NULL) lstrcpy(err_str, TEXT("Failed to connect to Yandex Disk."));
			if (body != NULL) free(body);
			return FALSE;
		}

		if (status == 201) {
			if (body != NULL) free(body);
			return TRUE;
		}

		if (status == 409) {
			if (body != NULL) {
				char err_type[128] = { 0 };
				if (json_extract_string(body, "error", err_type, sizeof(err_type))) {
					if (_stricmp(err_type, "DiskPathPointsToExistentDirectoryError") == 0) {
						free(body);
						return TRUE;
					}
				} else {
					free(body);
					return TRUE;
				}
				free(body);
			} else {
				return TRUE;
			}
			// Locked by another operation, wait and retry
			Sleep(400);
			continue;
		}

		if (status == 423) {
			// Locked, wait and retry
			if (body != NULL) free(body);
			Sleep(400);
			continue;
		}

		if (err_str != NULL) {
			_sntprintf(err_str, BUF_SIZE, TEXT("Failed to create folder '%s' (HTTP %u)."), path, status);
		}
		if (body != NULL) free(body);
		return FALSE;
	}

	if (err_str != NULL) {
		_sntprintf(err_str, BUF_SIZE, TEXT("Timeout creating folder '%s' (locked)."), path);
	}
	return FALSE;
}

/*
 * yandex_delete_resource - Delete file or folder on Yandex Disk and wait for completion
 */
BOOL yandex_delete_resource(const TCHAR *token, const TCHAR *path, TCHAR *err_str)
{
	WCHAR auth_header[512];
	WCHAR full_path[1024];
	WCHAR enc_path[512];
	WCHAR wpath[256];
	DWORD status = 0;
	char *body = NULL;
	BOOL ret = FALSE;

	make_auth_header(token, auth_header, 512);

#ifdef UNICODE
	lstrcpyn(wpath, path, 256);
#else
	MultiByteToWideChar(CP_ACP, 0, path, -1, wpath, 256);
#endif
	url_encode_w(wpath, enc_path, 512);
	_snwprintf(full_path, 1024, L"/v1/disk/resources?path=%s&permanently=true", enc_path);

	if (!yandex_http_request(L"DELETE", YANDEX_HOST, INTERNET_DEFAULT_HTTPS_PORT,
		full_path, auth_header, NULL, 0, &status, &body, NULL)) {
		if (err_str != NULL) lstrcpy(err_str, TEXT("Network error during deletion."));
		if (body != NULL) free(body);
		return FALSE;
	}

	if (status == 202 && body != NULL) {
		char op_href[2048] = { 0 };
		if (json_extract_string(body, "href", op_href, sizeof(op_href))) {
			yandex_wait_operation(token, op_href, 15000);
		}
		ret = TRUE;
	} else if (status == 200 || status == 204 || status == 404) {
		ret = TRUE;
	} else {
		if (err_str != NULL) {
			_sntprintf(err_str, BUF_SIZE, TEXT("Failed to delete '%s' (HTTP %u)."), path, status);
		}
	}

	if (body != NULL) free(body);

	// Ensure the resource is confirmed gone (GET returns 404) before proceeding
	if (ret) {
		WCHAR chk_path[1024];
		DWORD chk_start = GetTickCount();
		_snwprintf(chk_path, 1024, L"/v1/disk/resources?path=%s", enc_path);
		while (GetTickCount() - chk_start < 10000) {
			DWORD chk_status = 0;
			char *chk_body = NULL;
			if (yandex_http_request(L"GET", YANDEX_HOST, INTERNET_DEFAULT_HTTPS_PORT,
				chk_path, auth_header, NULL, 0, &chk_status, &chk_body, NULL)) {
				if (chk_body != NULL) free(chk_body);
				if (chk_status == 404) {
					break; // Confirmed deleted!
				}
			}
			Sleep(400);
		}
	}

	return ret;
}

/*
 * yandex_get_upload_url - Request a one-time PUT upload URL from Yandex Disk with retries
 */
BOOL yandex_get_upload_url(const TCHAR *token, const TCHAR *path, TCHAR *url_out, DWORD max_len, TCHAR *err_str)
{
	WCHAR auth_header[512];
	WCHAR req_path[1024];
	WCHAR enc_path[512];
	WCHAR wpath[256];
	char href[2048] = { 0 };
	int retry;
#ifndef UNICODE
	WCHAR wurl[2048];
#endif

	make_auth_header(token, auth_header, 512);

#ifdef UNICODE
	lstrcpyn(wpath, path, 256);
#else
	MultiByteToWideChar(CP_ACP, 0, path, -1, wpath, 256);
#endif
	url_encode_w(wpath, enc_path, 512);
	_snwprintf(req_path, 1024, L"/v1/disk/resources/upload?path=%s&overwrite=true", enc_path);

	for (retry = 0; retry < 5; retry++) {
		DWORD status = 0;
		char *body = NULL;

		if (!yandex_http_request(L"GET", YANDEX_HOST, INTERNET_DEFAULT_HTTPS_PORT,
			req_path, auth_header, NULL, 0, &status, &body, NULL)) {
			if (err_str != NULL) lstrcpy(err_str, TEXT("Network error requesting upload URL."));
			if (body != NULL) free(body);
			return FALSE;
		}

		if (status == 200 && body != NULL) {
			if (json_extract_string(body, "href", href, sizeof(href))) {
#ifdef UNICODE
				MultiByteToWideChar(CP_UTF8, 0, href, -1, url_out, max_len);
#else
				MultiByteToWideChar(CP_UTF8, 0, href, -1, wurl, 2048);
				WideCharToMultiByte(CP_ACP, 0, wurl, -1, url_out, max_len, NULL, NULL);
#endif
				free(body);
				return TRUE;
			}
		}

		if (status == 409 || status == 423 || status == 503) {
			if (body != NULL) free(body);
			Sleep(500);
			continue;
		}

		if (err_str != NULL) {
			_sntprintf(err_str, BUF_SIZE, TEXT("Could not get upload link for '%s' (HTTP %u)."), path, status);
		}
		if (body != NULL) free(body);
		return FALSE;
	}

	if (err_str != NULL) {
		_sntprintf(err_str, BUF_SIZE, TEXT("Timeout waiting for upload link for '%s'."), path);
	}
	return FALSE;
}

/*
 * yandex_upload_file - Upload raw payload to the given upload URL
 */
BOOL yandex_upload_file(const TCHAR *upload_url, const char *data, DWORD size, TCHAR *err_str)
{
	WCHAR wUploadUrl[4096];
	URL_COMPONENTS urlComp;
	WCHAR host[256] = { 0 };
	WCHAR urlPath[2048] = { 0 };
	WCHAR extraInfo[4096] = { 0 };
	WCHAR fullPath[4096] = { 0 };
	WCHAR content_headers[128];
	DWORD status = 0;
	BOOL ret = FALSE;

	if (upload_url == NULL || data == NULL) return FALSE;

#ifdef UNICODE
	lstrcpyn(wUploadUrl, upload_url, 4096);
#else
	MultiByteToWideChar(CP_ACP, 0, upload_url, -1, wUploadUrl, 4096);
#endif

	ZeroMemory(&urlComp, sizeof(urlComp));
	urlComp.dwStructSize = sizeof(urlComp);
	urlComp.lpszHostName = host;
	urlComp.dwHostNameLength = 256;
	urlComp.lpszUrlPath = urlPath;
	urlComp.dwUrlPathLength = 2048;
	urlComp.lpszExtraInfo = extraInfo;
	urlComp.dwExtraInfoLength = 4096;

	if (!WinHttpCrackUrl(wUploadUrl, 0, 0, &urlComp)) {
		if (err_str != NULL) lstrcpy(err_str, TEXT("Invalid upload URL."));
		return FALSE;
	}

	_snwprintf(fullPath, 4096, L"%s%s", urlPath, extraInfo);
	fullPath[4095] = L'\0';

	lstrcpyn(content_headers, L"Content-Type: text/plain; charset=utf-8\r\n", 128);

	if (!yandex_http_request(L"PUT", host, urlComp.nPort, fullPath,
		content_headers, data, size, &status, NULL, NULL)) {
		if (err_str != NULL) lstrcpy(err_str, TEXT("Upload network error."));
		return FALSE;
	}

	// 201 = Created, 202 = Accepted
	if (status == 201 || status == 202) {
		ret = TRUE;
	} else {
		if (err_str != NULL) {
			_sntprintf(err_str, BUF_SIZE, TEXT("Upload failed (HTTP %u)."), status);
		}
	}
	return ret;
}

/*
 * yandex_list_resources - Recursively list files and folders under a given path
 */
BOOL yandex_list_resources(const TCHAR *token, const TCHAR *path, YANDEX_RESOURCE_LIST *list_out, TCHAR *err_str)
{
	WCHAR auth_header[512];
	WCHAR req_path[1024];
	WCHAR enc_path[512];
	WCHAR wpath[256];
	DWORD offset = 0;
	BOOL ret = TRUE;
	int items_in_batch;
	const char *items_ptr;
	const char *p;
	const char *obj_start;
	const char *obj_end;
	DWORD item_len;
	char name[MAX_PATH];
	char item_path[MAX_PATH];
	char type[32];
	YANDEX_RESOURCE_ITEM *item;
#ifndef UNICODE
	WCHAR wtmp[MAX_PATH];
#endif

	if (list_out == NULL) return FALSE;
	list_out->head = NULL;
	list_out->count = 0;

	make_auth_header(token, auth_header, 512);

#ifdef UNICODE
	lstrcpyn(wpath, path, 256);
#else
	MultiByteToWideChar(CP_ACP, 0, path, -1, wpath, 256);
#endif
	url_encode_w(wpath, enc_path, 512);

	while (TRUE) {
		DWORD status = 0;
		char *body = NULL;

		_snwprintf(req_path, 1024, L"/v1/disk/resources?path=%s&limit=100&offset=%u&sort=name", enc_path, offset);

		if (!yandex_http_request(L"GET", YANDEX_HOST, INTERNET_DEFAULT_HTTPS_PORT,
			req_path, auth_header, NULL, 0, &status, &body, NULL)) {
			if (body != NULL) free(body);
			ret = FALSE;
			break;
		}

		if (status == 404) {
			// Folder doesn't exist yet, return empty list
			if (body != NULL) free(body);
			break;
		}

		if (status != 200 || body == NULL) {
			if (err_str != NULL) {
				_sntprintf(err_str, BUF_SIZE, TEXT("List failed for '%s' (HTTP %u)."), path, status);
			}
			if (body != NULL) free(body);
			ret = FALSE;
			break;
		}

		// Simple JSON parse over "_embedded"."items"
		items_ptr = strstr(body, "\"items\":");
		if (items_ptr == NULL) {
			free(body);
			break;
		}
		items_ptr = strchr(items_ptr, '[');
		if (items_ptr == NULL) {
			free(body);
			break;
		}

		items_in_batch = 0;
		p = items_ptr + 1;

		while (*p != ']' && *p != '\0') {
			obj_start = strchr(p, '{');
			if (obj_start == NULL) break;

			const char *close_bracket = strchr(p, ']');
			if (close_bracket != NULL && close_bracket < obj_start) {
				break;
			}

			// Find matching closing brace taking nested objects and strings into account
			int depth = 0;
			BOOL in_str = FALSE;
			BOOL esc = FALSE;
			const char *scan = obj_start;
			obj_end = NULL;

			while (*scan != '\0') {
				if (esc) {
					esc = FALSE;
				} else if (*scan == '\\') {
					esc = TRUE;
				} else if (*scan == '\"') {
					in_str = !in_str;
				} else if (!in_str) {
					if (*scan == '{') {
						depth++;
					} else if (*scan == '}') {
						depth--;
						if (depth == 0) {
							obj_end = scan;
							break;
						}
					}
				}
				scan++;
			}

			if (obj_end == NULL) break;

			item_len = (DWORD)(obj_end - obj_start + 1);
			char *item_buf = NULL;
			char stack_buf[4096];
			if (item_len < sizeof(stack_buf)) {
				item_buf = stack_buf;
			} else {
				item_buf = (char *)malloc(item_len + 1);
			}

			if (item_buf != NULL) {
				memcpy(item_buf, obj_start, item_len);
				item_buf[item_len] = '\0';

				name[0] = '\0';
				item_path[0] = '\0';
				type[0] = '\0';

				json_extract_string(item_buf, "name", name, sizeof(name));
				json_extract_string(item_buf, "path", item_path, sizeof(item_path));
				json_extract_string(item_buf, "type", type, sizeof(type));

				if (name[0] != '\0') {
					item = (YANDEX_RESOURCE_ITEM *)calloc(1, sizeof(YANDEX_RESOURCE_ITEM));
					if (item != NULL) {
#ifdef UNICODE
						MultiByteToWideChar(CP_UTF8, 0, name, -1, item->name, MAX_PATH);
						MultiByteToWideChar(CP_UTF8, 0, item_path, -1, item->path, MAX_PATH);
#else
						MultiByteToWideChar(CP_UTF8, 0, name, -1, wtmp, MAX_PATH);
						WideCharToMultiByte(CP_ACP, 0, wtmp, -1, item->name, MAX_PATH, NULL, NULL);
						MultiByteToWideChar(CP_UTF8, 0, item_path, -1, wtmp, MAX_PATH);
						WideCharToMultiByte(CP_ACP, 0, wtmp, -1, item->path, MAX_PATH, NULL, NULL);
#endif
						item->is_dir = (strcmp(type, "dir") == 0);
						item->next = list_out->head;
						list_out->head = item;
						list_out->count++;
						items_in_batch++;
					}
				}

				if (item_buf != stack_buf) {
					free(item_buf);
				}
			}
			p = obj_end + 1;
		}

		free(body);
		if (items_in_batch < 100) {
			break; // Reached last page
		}
		offset += items_in_batch;
	}

	return ret;
}

/*
 * yandex_download_file - Download file content from Yandex Disk
 */
BOOL yandex_download_file(const TCHAR *token, const TCHAR *path, char **buf_out, DWORD *size_out, TCHAR *err_str)
{
	WCHAR auth_header[512];
	WCHAR req_path[1024];
	WCHAR enc_path[512];
	WCHAR wpath[256];
	DWORD status = 0;
	char *body = NULL;
	BOOL ret = FALSE;
	char download_href[4096] = { 0 };
	WCHAR wDownloadUrl[4096];
	URL_COMPONENTS urlComp;
	WCHAR host[256] = { 0 };
	WCHAR urlPath[2048] = { 0 };
	WCHAR extraInfo[4096] = { 0 };
	WCHAR fullPath[4096] = { 0 };
	char *file_data = NULL;
	DWORD file_len = 0;

	if (buf_out == NULL || size_out == NULL) return FALSE;
	*buf_out = NULL;
	*size_out = 0;

	make_auth_header(token, auth_header, 512);

#ifdef UNICODE
	lstrcpyn(wpath, path, 256);
#else
	MultiByteToWideChar(CP_ACP, 0, path, -1, wpath, 256);
#endif
	url_encode_w(wpath, enc_path, 512);
	_snwprintf(req_path, 1024, L"/v1/disk/resources/download?path=%s", enc_path);

	// Step 1: Request download URL
	if (!yandex_http_request(L"GET", YANDEX_HOST, INTERNET_DEFAULT_HTTPS_PORT,
		req_path, auth_header, NULL, 0, &status, &body, NULL)) {
		if (err_str != NULL) lstrcpy(err_str, TEXT("Network error requesting download link."));
		if (body != NULL) free(body);
		return FALSE;
	}

	if (status != 200 || body == NULL) {
		if (err_str != NULL) _sntprintf(err_str, BUF_SIZE, TEXT("Could not get download URL (HTTP %u)."), status);
		if (body != NULL) free(body);
		return FALSE;
	}

	json_extract_string(body, "href", download_href, sizeof(download_href));
	free(body);

	if (download_href[0] == '\0') {
		if (err_str != NULL) lstrcpy(err_str, TEXT("Invalid download href."));
		return FALSE;
	}

	// Step 2: Download the content from the href
	MultiByteToWideChar(CP_UTF8, 0, download_href, -1, wDownloadUrl, 4096);

	ZeroMemory(&urlComp, sizeof(urlComp));
	urlComp.dwStructSize = sizeof(urlComp);
	urlComp.lpszHostName = host;
	urlComp.dwHostNameLength = 256;
	urlComp.lpszUrlPath = urlPath;
	urlComp.dwUrlPathLength = 2048;
	urlComp.lpszExtraInfo = extraInfo;
	urlComp.dwExtraInfoLength = 4096;

	if (!WinHttpCrackUrl(wDownloadUrl, 0, 0, &urlComp)) {
		if (err_str != NULL) lstrcpy(err_str, TEXT("Failed to parse download link."));
		return FALSE;
	}

	_snwprintf(fullPath, 4096, L"%s%s", urlPath, extraInfo);
	fullPath[4095] = L'\0';

	status = 0;
	if (yandex_http_request(L"GET", host, urlComp.nPort, fullPath,
		NULL, NULL, 0, &status, &file_data, &file_len)) {
		if (status == 200 && file_data != NULL) {
			*buf_out = file_data;
			*size_out = file_len;
			ret = TRUE;
		} else {
			if (file_data != NULL) free(file_data);
			if (err_str != NULL) _sntprintf(err_str, BUF_SIZE, TEXT("Download failed (HTTP %u)."), status);
		}
	} else {
		if (err_str != NULL) lstrcpy(err_str, TEXT("Network error during file download."));
	}

	return ret;
}

/*
 * yandex_free_resource_list - Free allocated resource list
 */
void yandex_free_resource_list(YANDEX_RESOURCE_LIST *list)
{
	YANDEX_RESOURCE_ITEM *curr, *next;
	if (list == NULL) return;

	curr = list->head;
	while (curr != NULL) {
		next = curr->next;
		free(curr);
		curr = next;
	}
	list->head = NULL;
	list->count = 0;
}
