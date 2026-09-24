#ifndef _INC_PINNEDIMAGE_H
#define _INC_PINNEDIMAGE_H

#include "Data.h"

BOOL pinned_image_regist(const HINSTANCE instance);
BOOL pinned_image_start_snip(HWND owner);
BOOL pinned_image_has_bitmap(DATA_INFO *source);
HWND pinned_image_open(const HWND owner, DATA_INFO *source);
HWND pinned_image_open_from_menu(const HWND owner, DATA_INFO *source);
void pinned_image_close_all(void);
BOOL pinned_image_clipboard_is_screenshot(HWND owner);
void pinned_image_auto_open(HWND owner, DATA_INFO *source, DWORD sequence);

#endif
