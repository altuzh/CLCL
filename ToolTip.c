/*
 * CLCL
 *
 * ToolTip.c
 *
 * Copyright (C) 1996-2019 by Ohno Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 *		nakka@nakka.com
 */

/* Include Files */
#define _INC_OLE
#include <windows.h>
#undef  _INC_OLE

#include "General.h"
#include "Memory.h"
#include "Ini.h"
#include "Font.h"
#include "dpi.h"
#include "DarkMode.h"
#include "ToolTip.h"

#include "resource.h"

/* Define */
#define WINDOW_CLASS					TEXT("CLCLTooltip")

#define WM_TOOLTIP_SHOW					(WM_APP + 1)
#define WM_TOOLTIP_HIDE					(WM_APP + 2)

#define ID_SHOW_TIMER					1
#define ID_MOUSE_TIMER					2

#define MOUSE_INTERVAL					100

// Tooltip margin
#define TOOLTIP_MARGIN_X				Scale(option.tooltip_margin_x)
#define TOOLTIP_MARGIN_Y				Scale(option.tooltip_margin_y)

#ifndef SPI_GETTOOLTIPANIMATION
#define SPI_GETTOOLTIPANIMATION			0x00001016
#endif
#ifndef SPI_GETTOOLTIPFADE
#define SPI_GETTOOLTIPFADE				0x00001018
#endif

#ifndef AW_VER_POSITIVE
#define AW_VER_POSITIVE					0x00000004
#endif
#ifndef AW_SLIDE
#define AW_SLIDE						0x00040000
#endif
#ifndef AW_BLEND
#define AW_BLEND						0x00080000
#endif

/* Global Variables */
typedef struct _TOOLTIP_INFO {
	HFONT hfont;

	TCHAR *buf;
	HBITMAP hbmp;
	int bmp_width;
	int bmp_height;
	BOOL free_bmp;

	POINT pt;
	int top;
	RECT anchor_rect;
	HWND hWnd;
	HWND hover_wnd;
	RECT hover_rect;
} TOOLTIP_INFO;

// DPI when tooltip font was created
static UINT tooltip_font_dpi;

// Options
extern OPTION_INFO option;

/* Local Function Prototypes */
static int tooltip_get_cursor_height(const HCURSOR hcursor);
static void tooltip_draw_text(const TOOLTIP_INFO *ti, const HDC hdc, RECT *rect);
static void tooltip_create_font(TOOLTIP_INFO *ti);
static LRESULT CALLBACK tooltip_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

/*
 * tooltip_get_cursor_height - get mouse cursor height
 */
static int tooltip_get_cursor_height(const HCURSOR hcursor)
{
	HDC hdc, mdc;
	HBITMAP hbmp, ret_hbmp;
	ICONINFO icon_info;
	int width, height;
	int x, y;

	// Get cursor size
	width = GetSystemMetrics(SM_CXCURSOR);
	height = GetSystemMetrics(SM_CYCURSOR);

	// Draw cursor (mask)
	hdc = GetDC(NULL);
	mdc = CreateCompatibleDC(hdc);
	hbmp = CreateCompatibleBitmap(hdc, width, height);
	ret_hbmp = SelectObject(mdc, hbmp);
	DrawIconEx(mdc, 0, 0, hcursor, width, height, 0, NULL, DI_MASK);

	// Get cursor height
	for (y = height - 1; y >= 0; y--) {
		for (x = 0; x < width; x++) {
			if (GetPixel(mdc, x, y) != RGB(255, 255, 255)) {
				break;
			}
		}
		if (x < width) {
			break;
		}
	}
	SelectObject(mdc, ret_hbmp);
	DeleteObject(hbmp);
	DeleteDC(mdc);
	ReleaseDC(NULL, hdc);

	// Get hotspot position
	ZeroMemory(&icon_info, sizeof(ICONINFO));
	GetIconInfo(hcursor, &icon_info);
	if (icon_info.hbmMask != NULL) {
		DeleteObject(icon_info.hbmMask);
	}
	if (icon_info.hbmColor != NULL) {
		DeleteObject(icon_info.hbmColor);
	}
	return (((y < 0) ? height : y) - icon_info.yHotspot);
}

/*
 * tooltip_draw_text - draw tooltip
 */
static void tooltip_draw_text(const TOOLTIP_INFO *ti, const HDC hdc, RECT *rect)
{
	DRAWTEXTPARAMS dtp;
	HBRUSH hbrush;
	HFONT hRetFont;
#ifdef TOOLTIP_COLOR
	DWORD color_infoback = (*option.tooltip_color_back.color_str != TEXT('\0')) ?
		option.tooltip_color_back.color : dark_mode_get_color(COLOR_INFOBK);
	DWORD color_infotext = (*option.tooltip_color_text.color_str != TEXT('\0')) ?
		option.tooltip_color_text.color : dark_mode_get_color(COLOR_INFOTEXT);
#else	// TOOLTIP_COLOR
	DWORD color_infoback = dark_mode_get_color(COLOR_INFOBK);
	DWORD color_infotext = dark_mode_get_color(COLOR_INFOTEXT);
#endif	// TOOLTIP_COLOR

	// Fill background
	hbrush = CreateSolidBrush(color_infoback);
	FillRect(hdc, rect, hbrush);
	DeleteObject(hbrush);

	int cur_top = rect->top + TOOLTIP_MARGIN_Y;

	// Draw bitmap preview
	if (ti->hbmp != NULL && ti->bmp_width > 0 && ti->bmp_height > 0) {
		HDC mem_dc = CreateCompatibleDC(hdc);
		if (mem_dc != NULL) {
			HBITMAP old_bmp = (HBITMAP)SelectObject(mem_dc, ti->hbmp);
			BITMAP orig_bmp;
			GetObject(ti->hbmp, sizeof(BITMAP), &orig_bmp);

			int img_x = rect->left + ((rect->right - rect->left) - ti->bmp_width) / 2;
			if (img_x < rect->left + TOOLTIP_MARGIN_X) {
				img_x = rect->left + TOOLTIP_MARGIN_X;
			}

			SetStretchBltMode(hdc, HALFTONE);
			SetBrushOrgEx(hdc, 0, 0, NULL);
			StretchBlt(hdc, img_x, cur_top, ti->bmp_width, ti->bmp_height,
				mem_dc, 0, 0, orig_bmp.bmWidth, orig_bmp.bmHeight, SRCCOPY);

			SelectObject(mem_dc, old_bmp);
			DeleteDC(mem_dc);
		}
		cur_top += ti->bmp_height + TOOLTIP_MARGIN_Y;
	}

	if (ti->buf != NULL) {
		// Draw text
		hRetFont = SelectObject(hdc, (ti->hfont != NULL) ? ti->hfont : GetStockObject(DEFAULT_GUI_FONT));
		RECT text_rect;
		SetRect(&text_rect,
			rect->left + TOOLTIP_MARGIN_X,
			cur_top,
			rect->right - TOOLTIP_MARGIN_X,
			rect->bottom - TOOLTIP_MARGIN_Y);

		SetTextColor(hdc, color_infotext);
		SetBkColor(hdc, color_infoback);
		SetBkMode(hdc, TRANSPARENT);

		ZeroMemory(&dtp, sizeof(DRAWTEXTPARAMS));
		dtp.cbSize = sizeof(DRAWTEXTPARAMS);
		dtp.iTabLength = option.tooltip_tab_length;
		UINT dt_flags = DT_EDITCONTROL | DT_EXPANDTABS | DT_TABSTOP | DT_NOPREFIX;
		if (ti->hbmp != NULL) {
			dt_flags |= DT_CENTER;
		}
		DrawTextEx(hdc, ti->buf, lstrlen(ti->buf),
			&text_rect, dt_flags, &dtp);
		SelectObject(hdc, hRetFont);
	}
}

/*
 * tooltip_create_font - create tooltip font
 */
static void tooltip_create_font(TOOLTIP_INFO *ti)
{
	NONCLIENTMETRICS ncMetrics;

	if (ti == NULL) {
		return;
	}
	if (ti->hfont != NULL) {
		DeleteObject(ti->hfont);
		ti->hfont = NULL;
	}
	if (*option.tooltip_font_name != TEXT('\0')) {
		// Create font
		ti->hfont = font_create(option.tooltip_font_name,
			option.tooltip_font_size, option.tooltip_font_charset, option.tooltip_font_weight,
			(option.tooltip_font_italic == 0) ? FALSE : TRUE, FALSE);
	} else {
		if (GetNonClientMetricsDpi(&ncMetrics) != FALSE) {
			// Create default font
			ti->hfont = CreateFontIndirect(&ncMetrics.lfStatusFont);
		}
	}
	tooltip_font_dpi = GetDpi();
}

/*
 * tooltip_proc - tooltip window procedure
 */
static LRESULT CALLBACK tooltip_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	PAINTSTRUCT ps;
	DRAWTEXTPARAMS dtp;
	RECT rect;
	POINT pt;
	HDC hdc;
	HFONT hRetFont;
	TOOLTIP_INFO *ti;

	switch (msg) {
	case WM_CREATE:
		if ((ti = mem_calloc(sizeof(TOOLTIP_INFO))) == NULL) {
			return -1;
		}
		tooltip_create_font(ti);
		// tooltip info to window long
		SetWindowLong(hWnd, GWL_USERDATA, (LPARAM)ti);
		break;

	case WM_CLOSE:
		DestroyWindow(hWnd);
		break;

	case WM_DESTROY:
		if ((ti = (TOOLTIP_INFO *)GetWindowLong(hWnd, GWL_USERDATA)) != NULL) {
			if (ti->hfont != NULL) {
				DeleteObject(ti->hfont);
				ti->hfont = NULL;
			}
			if (ti->buf != NULL) {
				mem_free(&ti->buf);
			}
			if (ti->free_bmp && ti->hbmp != NULL) {
				DeleteObject(ti->hbmp);
				ti->hbmp = NULL;
			}
			mem_free(&ti);
		}
		return DefWindowProc(hWnd, msg, wParam, lParam);

	case WM_SETTINGCHANGE:
		if ((ti = (TOOLTIP_INFO *)GetWindowLong(hWnd, GWL_USERDATA)) == NULL ||
			*option.tooltip_font_name != TEXT('\0') ||
			wParam != SPI_SETNONCLIENTMETRICS) {
			break;
		}
		tooltip_create_font(ti);
		break;

#ifdef TOOLTIP_ANIMATE
	case WM_PRINT:
		// Draw text
		if ((ti = (TOOLTIP_INFO *)GetWindowLong(hWnd, GWL_USERDATA)) == NULL) {
			break;
		}
		// Draw non-client area
		DefWindowProc(hWnd, msg, wParam, lParam);

		// Draw tooltip
		GetClientRect(hWnd, (LPRECT)&rect);
		SetRect(&rect, rect.left + 1, rect.top + 1, rect.right + 1, rect.bottom + 1);
		tooltip_draw_text(ti, (HDC)wParam, &rect);
		break;
#endif

	case WM_PAINT:
		// Draw text
		if ((ti = (TOOLTIP_INFO *)GetWindowLong(hWnd, GWL_USERDATA)) == NULL) {
			break;
		}
		hdc = BeginPaint(hWnd, &ps);

		// Draw tooltip
		GetClientRect(hWnd, (LPRECT)&rect);
		tooltip_draw_text(ti, hdc, &rect);

		EndPaint(hWnd, &ps);
		break;

	case WM_MOUSEMOVE:
	case WM_LBUTTONDOWN:
	case WM_MBUTTONDOWN:
	case WM_RBUTTONDOWN:
	case WM_SETCURSOR:
	case WM_TOOLTIP_HIDE:
		// Hide tooltip
		KillTimer(hWnd, ID_SHOW_TIMER);
		KillTimer(hWnd, ID_MOUSE_TIMER);
		ShowWindow(hWnd, SW_HIDE);

		if ((ti = (TOOLTIP_INFO *)GetWindowLong(hWnd, GWL_USERDATA)) != NULL) {
			if (ti->buf != NULL) {
				mem_free(&ti->buf);
			}
			if (ti->free_bmp && ti->hbmp != NULL) {
				DeleteObject(ti->hbmp);
				ti->hbmp = NULL;
			}
			ti->hbmp = NULL;
			ti->free_bmp = FALSE;
			ti->bmp_width = 0;
			ti->bmp_height = 0;
			ti->hover_wnd = NULL;
		}
		break;

	case WM_TOOLTIP_SHOW:
		// Show tooltip
		KillTimer(hWnd, ID_SHOW_TIMER);
		KillTimer(hWnd, ID_MOUSE_TIMER);
		ShowWindow(hWnd, SW_HIDE);
		if (lParam == 0) {
			break;
		}
		{
			TOOLTIP_INFO *src = (TOOLTIP_INFO *)lParam;
			if ((src->buf == NULL || *src->buf == TEXT('\0')) && src->hbmp == NULL) {
				break;
			}
			if ((ti = (TOOLTIP_INFO *)GetWindowLong(hWnd, GWL_USERDATA)) == NULL) {
				break;
			}

			// Set coordinates
			ti->top = src->top;
			ti->pt.x = src->pt.x;
			ti->pt.y = src->pt.y;
			ti->anchor_rect = src->anchor_rect;
			ti->hover_wnd = src->hover_wnd;
			ti->hover_rect = src->hover_rect;

			// Get window
			if (ti->pt.x == 0 && ti->pt.y == 0 && (ti->anchor_rect.right <= ti->anchor_rect.left)) {
				GetCursorPos(&pt);
				ti->hWnd = WindowFromPoint(pt);
			} else {
				ti->hWnd = NULL;
			}

			// Set text
			if (ti->buf != NULL) {
				mem_free(&ti->buf);
			}
			ti->buf = (src->buf != NULL) ? alloc_copy(src->buf) : NULL;

			// Set bitmap
			if (ti->free_bmp && ti->hbmp != NULL) {
				DeleteObject(ti->hbmp);
			}
			ti->hbmp = src->hbmp;
			ti->free_bmp = src->free_bmp;
			ti->bmp_width = src->bmp_width;
			ti->bmp_height = src->bmp_height;

			// Show tooltip
			SetTimer(hWnd, ID_SHOW_TIMER, (wParam > 0) ? wParam : 1, NULL);
		}
		break;

	case WM_TIMER:
		switch (wParam) {
		case ID_SHOW_TIMER:
			KillTimer(hWnd, wParam);
			if ((ti = (TOOLTIP_INFO *)GetWindowLong(hWnd, GWL_USERDATA)) == NULL) {
				SendMessage(hWnd, WM_TOOLTIP_HIDE, 0, 0);
				break;
			}
			if (ti->buf == NULL && ti->hbmp == NULL) {
				SendMessage(hWnd, WM_TOOLTIP_HIDE, 0, 0);
				break;
			}
			if (ti->hover_wnd != NULL) {
				GetCursorPos(&pt);
				if (!IsWindowVisible(ti->hover_wnd) || WindowFromPoint(pt) != ti->hover_wnd ||
					!PtInRect(&ti->hover_rect, pt)) {
					SendMessage(hWnd, WM_TOOLTIP_HIDE, 0, 0);
					break;
				}
			}

			// Get display position (mouse position)
			if (ti->pt.x == 0 && ti->pt.y == 0 && (ti->anchor_rect.right <= ti->anchor_rect.left)) {
				GetCursorPos(&ti->pt);
				ti->top = tooltip_get_cursor_height(GetCursor()) + 1;
				if (ti->hWnd != WindowFromPoint(ti->pt)) {
					SendMessage(hWnd, WM_TOOLTIP_HIDE, 0, 0);
					break;
				}
			}

			// Get monitor work area corresponding to display position
			MONITORINFO mi;
			ZeroMemory(&mi, sizeof(MONITORINFO));
			mi.cbSize = sizeof(MONITORINFO);
			POINT mon_pt = { 0, 0 };
			if (ti->anchor_rect.right > ti->anchor_rect.left) {
				mon_pt.x = (ti->anchor_rect.left + ti->anchor_rect.right) / 2;
				mon_pt.y = (ti->anchor_rect.top + ti->anchor_rect.bottom) / 2;
			} else if (ti->pt.x != 0 || ti->pt.y != 0) {
				mon_pt = ti->pt;
			}
			HMONITOR hMon = MonitorFromPoint(mon_pt, MONITOR_DEFAULTTONEAREST);
			if (hMon == NULL || GetMonitorInfo(hMon, &mi) == FALSE) {
				SystemParametersInfo(SPI_GETWORKAREA, 0, &mi.rcWork, 0);
				SetRect(&mi.rcMonitor, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
			}

			// Adjust to monitor DPI
			if (SetDpiFromPoint(mon_pt) != tooltip_font_dpi) {
				tooltip_create_font(ti);
			}

			int edge_offset = Scale(16);
			int safe_left = mi.rcWork.left + edge_offset;
			int safe_right = mi.rcWork.right - edge_offset;
			int safe_top = mi.rcWork.top + edge_offset;
			int safe_bottom = mi.rcWork.bottom - edge_offset;
			int safe_w = safe_right - safe_left;
			int safe_h = safe_bottom - safe_top;
			if (safe_w < 100) safe_w = 100;
			if (safe_h < 100) safe_h = 100;

			// Get size (text)
			hdc = GetDC(hWnd);
			hRetFont = SelectObject(hdc, (ti->hfont != NULL) ? ti->hfont : GetStockObject(DEFAULT_GUI_FONT));
			SetRectEmpty(&rect);

			if (ti->buf != NULL && *ti->buf != TEXT('\0')) {
				ZeroMemory(&dtp, sizeof(DRAWTEXTPARAMS));
				dtp.cbSize = sizeof(DRAWTEXTPARAMS);
				dtp.iTabLength = option.tooltip_tab_length;
				DrawTextEx(hdc, ti->buf, lstrlen(ti->buf), &rect,
					DT_CALCRECT | DT_EDITCONTROL | DT_NOCLIP | DT_EXPANDTABS | DT_TABSTOP | DT_NOPREFIX, &dtp);
			}

			SelectObject(hdc, hRetFont);
			ReleaseDC(hWnd, hdc);

			int text_w = rect.right - rect.left;
			int text_h = rect.bottom - rect.top;

			// Determine anchor coordinates (menu or cursor)
			int anchor_left;
			int anchor_right;
			int anchor_top;

			if (ti->anchor_rect.right > ti->anchor_rect.left) {
				anchor_left = ti->anchor_rect.left;
				anchor_right = ti->anchor_rect.right;
				anchor_top = (ti->anchor_rect.top + ti->anchor_rect.bottom) / 2;
			} else {
				POINT cur_pt;
				if (ti->pt.x != 0 || ti->pt.y != 0) {
					cur_pt = ti->pt;
				} else {
					GetCursorPos(&cur_pt);
				}
				anchor_left = cur_pt.x - Scale(2);
				anchor_right = cur_pt.x + Scale(18);
				anchor_top = cur_pt.y;
			}

			// Clamp anchor itself within main monitor safe area
			if (anchor_right > safe_right) anchor_right = safe_right;
			if (anchor_right < safe_left) anchor_right = safe_left;
			if (anchor_left > safe_right) anchor_left = safe_right;
			if (anchor_left < safe_left) anchor_left = safe_left;
			if (anchor_top < safe_top) anchor_top = safe_top;
			if (anchor_top > safe_bottom) anchor_top = safe_bottom;

			int gap = Scale(6);

			// Available width on right and left sides (from anchor boundary to screen edge)
			int avail_right = safe_right - (anchor_right + gap);
			if (avail_right < 0) avail_right = 0;
			int avail_left = (anchor_left - gap) - safe_left;
			if (avail_left < 0) avail_left = 0;

			// Padding
			int extra_h = (TOOLTIP_MARGIN_Y * 2) + 2;
			if (text_h > 0) {
				extra_h += text_h + TOOLTIP_MARGIN_Y;
			}
			int extra_w = (TOOLTIP_MARGIN_X * 2) + 2;

			// Limit based on max 100% of screen size
			int max_screen_h = safe_h;
			int max_screen_w = safe_w;
			int max_bmp_h = max_screen_h - extra_h;
			if (max_bmp_h < 50) max_bmp_h = 50;
			int max_bmp_w_screen = max_screen_w - extra_w;
			if (max_bmp_w_screen < 50) max_bmp_w_screen = 50;

			// Determine display side (right vs left) when image is present
			int show_left = FALSE;
			int max_side_w = avail_right;

			if (ti->hbmp != NULL) {
				BITMAP orig_bmp;
				if (GetObject(ti->hbmp, sizeof(BITMAP), &orig_bmp) != 0 && orig_bmp.bmWidth > 0 && orig_bmp.bmHeight > 0) {
					// Calculate ideal scale factor for 100% screen size (maintain aspect ratio)
					double scale_ideal_w = (double)max_bmp_w_screen / (double)orig_bmp.bmWidth;
					double scale_ideal_h = (double)max_bmp_h / (double)orig_bmp.bmHeight;
					double ideal_scale = (scale_ideal_w < scale_ideal_h) ? scale_ideal_w : scale_ideal_h;

					// Cap at 4x for tiny images like icons (<64px) to prevent excessive blur
					if (orig_bmp.bmWidth < 64 && orig_bmp.bmHeight < 64 && ideal_scale > 4.0) {
						ideal_scale = 4.0;
					}

					int ideal_bmp_w = (int)(orig_bmp.bmWidth * ideal_scale);
					int ideal_w = ideal_bmp_w + extra_w;
					if (ideal_w < text_w + extra_w) ideal_w = text_w + extra_w;

					// Does it fit on the right at natural width?
					if (ideal_w <= avail_right) {
						show_left = FALSE;
						max_side_w = avail_right;
					} else if (ideal_w <= avail_left) {
						// Does not fit on right, fits on left -> flip to left
						show_left = TRUE;
						max_side_w = avail_left;
					} else {
						// If neither side fits the ideal width, place on the wider side
						if (avail_left > avail_right) {
							show_left = TRUE;
							max_side_w = avail_left;
						} else {
							show_left = FALSE;
							max_side_w = avail_right;
						}
					}

					// Maintain aspect ratio to fit both width limit (max_side_w) and height limit (max_bmp_h) on selected side
					int max_bmp_w = max_side_w - extra_w;
					if (max_bmp_w > max_bmp_w_screen) max_bmp_w = max_bmp_w_screen;
					if (max_bmp_w < 50) max_bmp_w = 50;

					double scale_w = (double)max_bmp_w / (double)orig_bmp.bmWidth;
					double scale_h = (double)max_bmp_h / (double)orig_bmp.bmHeight;
					double scale = (scale_w < scale_h) ? scale_w : scale_h;

					if (orig_bmp.bmWidth < 64 && orig_bmp.bmHeight < 64 && scale > 4.0) {
						scale = 4.0;
					}

					ti->bmp_width = (int)(orig_bmp.bmWidth * scale);
					ti->bmp_height = (int)(orig_bmp.bmHeight * scale);
					if (ti->bmp_width < 1) ti->bmp_width = 1;
					if (ti->bmp_height < 1) ti->bmp_height = 1;
				}
			} else {
				// If text only
				int natural_w = text_w + extra_w;
				if (natural_w <= avail_right) {
					show_left = FALSE;
					max_side_w = avail_right;
				} else if (natural_w <= avail_left) {
					show_left = TRUE;
					max_side_w = avail_left;
				} else {
					show_left = (avail_left > avail_right);
					max_side_w = show_left ? avail_left : avail_right;
				}
				if (max_side_w > max_screen_w) {
					max_side_w = max_screen_w;
				}
			}

			int content_width = text_w;
			if (ti->hbmp != NULL && ti->bmp_width > content_width) {
				content_width = ti->bmp_width;
			}
			int content_height = text_h;
			if (ti->hbmp != NULL && ti->bmp_height > 0) {
				content_height += ti->bmp_height;
				if (text_h > 0) {
					content_height += TOOLTIP_MARGIN_Y;
				}
			}
			int window_w = content_width + extra_w;
			int window_h = content_height + (TOOLTIP_MARGIN_Y * 2) + 2;

			// Restrict window size to not exceed available width
			if (window_w > max_side_w && max_side_w > 50) {
				window_w = max_side_w;
			}
			if (window_w > max_screen_w) {
				window_w = max_screen_w;
			}
			if (window_h > max_screen_h) {
				window_h = max_screen_h;
			}

			// Determine coordinates: attach to cursor/menu boundary
			int target_x;
			if (show_left == FALSE) {
				// Display on right: attach left edge of tooltip to right edge of anchor
				target_x = anchor_right + gap;
				if (target_x + window_w > safe_right) {
					target_x = safe_right - window_w;
				}
			} else {
				// Display on left: attach right edge of tooltip to left edge of anchor
				target_x = anchor_left - window_w - gap;
				if (target_x < safe_left) {
					target_x = safe_left;
				}
			}

			// Vertical: align center of window to center of anchor
			int target_y = anchor_top - (window_h / 2);
			if (target_y + window_h > safe_bottom) {
				target_y = safe_bottom - window_h;
			}
			if (target_y < safe_top) {
				target_y = safe_top;
			}

			// Final clamp within safe area of main monitor
			if (target_x + window_w > safe_right) {
				target_x = safe_right - window_w;
			}
			if (target_x < safe_left) {
				target_x = safe_left;
			}

			// Set window position and size
			SetWindowPos(hWnd, HWND_TOPMOST,
				target_x, target_y, window_w, window_h,
				SWP_NOACTIVATE);

#ifdef TOOLTIP_ANIMATE
			{
				HANDLE user32_lib;
				FARPROC AnimateWindow;
				BOOL effect_flag;

				// Display window
				SystemParametersInfo(SPI_GETTOOLTIPANIMATION, 0, &effect_flag, 0);
				if (effect_flag == TRUE) {
					SystemParametersInfo(SPI_GETTOOLTIPFADE, 0, &effect_flag, 0);
					user32_lib = LoadLibrary(TEXT("user32.dll"));
					if (user32_lib != NULL) {
						AnimateWindow = GetProcAddress(user32_lib, "AnimateWindow");
						if (AnimateWindow != NULL) {
							// Animation display
							AnimateWindow(hWnd, 200, (effect_flag == TRUE) ? AW_BLEND : (AW_SLIDE | AW_VER_POSITIVE));
						}
						FreeLibrary(user32_lib);
					}
				}
			}
#endif
			ShowWindow(hWnd, SW_SHOWNOACTIVATE);
			SetTimer(hWnd, ID_MOUSE_TIMER, MOUSE_INTERVAL, NULL);
			break;

		case ID_MOUSE_TIMER:
			if ((ti = (TOOLTIP_INFO *)GetWindowLong(hWnd, GWL_USERDATA)) == NULL ||
				IsWindowVisible(hWnd) == FALSE) {
				KillTimer(hWnd, wParam);
				break;
			}
			if (ti->hover_wnd != NULL) {
				GetCursorPos(&pt);
				if (!IsWindowVisible(ti->hover_wnd) || WindowFromPoint(pt) != ti->hover_wnd ||
					!PtInRect(&ti->hover_rect, pt))
					SendMessage(hWnd, WM_TOOLTIP_HIDE, 0, 0);
				break;
			}
			if (ti->hWnd == NULL) { KillTimer(hWnd, wParam); break; }
			// Check window under mouse
			GetCursorPos(&pt);
			if ((ti->pt.x != pt.x || ti->pt.y != pt.y) && ti->hWnd != WindowFromPoint(pt)) {
				SendMessage(hWnd, WM_TOOLTIP_HIDE, 0, 0);
				break;
			}
			break;
		}
		break;

	default:
		return DefWindowProc(hWnd, msg, wParam, lParam);
	}
	return 0;
}

/*
 * tooltip_show - display tooltip
 */
BOOL tooltip_show(const HWND hToolTip, TCHAR *tip_text, const long x, const long y, const long top)
{
	return tooltip_show_image(hToolTip, tip_text, NULL, FALSE, x, y, top, NULL);
}

/*
 * tooltip_show_image_delay - display tooltip with image after specified delay
 */
BOOL tooltip_show_image_delay(const HWND hToolTip, TCHAR *tip_text, const HBITMAP hbmp, const BOOL free_bmp, const long x, const long y, const long top, const RECT *anchor_rect, const int delay, const HWND hover_wnd, const RECT *hover_rect)
{
	TOOLTIP_INFO ti;
	ZeroMemory(&ti, sizeof(TOOLTIP_INFO));

	ti.buf = tip_text;
	ti.hbmp = hbmp;
	ti.free_bmp = free_bmp;
	ti.pt.x = x;
	ti.pt.y = y;
	ti.top = top;
	if (anchor_rect != NULL) {
		ti.anchor_rect = *anchor_rect;
	}
	ti.hover_wnd = hover_wnd;
	if (hover_rect != NULL) ti.hover_rect = *hover_rect;

	WPARAM delay_param = (delay >= 0) ? (WPARAM)delay : (WPARAM)option.tooltip_show_delay;
	SendMessage(hToolTip, WM_TOOLTIP_SHOW, delay_param, (LPARAM)&ti);
	return TRUE;
}

/*
 * tooltip_show_image - display tooltip with image
 */
BOOL tooltip_show_image(const HWND hToolTip, TCHAR *tip_text, const HBITMAP hbmp, const BOOL free_bmp, const long x, const long y, const long top, const RECT *anchor_rect)
{
	return tooltip_show_image_delay(hToolTip, tip_text, hbmp, free_bmp, x, y, top, anchor_rect, -1, NULL, NULL);
}

/*
 * tooltip_hide - hide tooltip
 */
void tooltip_hide(const HWND hToolTip)
{
	SendMessage(hToolTip, WM_TOOLTIP_HIDE, 0, 0);
}

/*
 * tooltip_close - close tooltip
 */
void tooltip_close(const HWND hToolTip)
{
	DestroyWindow(hToolTip);
}

/*
 * tooltip_regist - register window class
 */
BOOL tooltip_regist(const HINSTANCE hInstance)
{
	WNDCLASS wc;

	wc.style = 0;
	wc.lpfnWndProc = (WNDPROC)tooltip_proc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = hInstance;
	wc.hIcon = NULL;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_INFOBK + 1);
	wc.lpszMenuName = NULL;
	wc.lpszClassName = WINDOW_CLASS;
	// Register window class
	return RegisterClass(&wc);
}

/*
 * tooltip_create - create tooltip
 */
HWND tooltip_create(const HINSTANCE hInstance)
{
	HWND hWnd;

	// Create window
	hWnd = CreateWindowEx(WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
		WINDOW_CLASS,
		TEXT(""),
		WS_POPUP | WS_BORDER,
		0, 0, 0, 0, NULL, NULL, hInstance, NULL);
	return hWnd;
}
/* End of source */
