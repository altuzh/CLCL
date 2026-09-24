#define _INC_OLE
#include <windows.h>
#undef _INC_OLE
#include <commctrl.h>
#include <tchar.h>
#include <windowsx.h>
#include <math.h>
#include <limits.h>

#include "General.h"
#include "Memory.h"
#include "Data.h"
#include "Bitmap.h"
#include "Clipboard.h"
#include "Format.h"
#include "DbHistory.h"
#include "DarkMode.h"
#include "Dpi.h"
#include "Ini.h"
#include "Profile.h"
#include "PinnedImage.h"
#include "resource.h"

#pragma comment(lib, "msimg32.lib")

#define PIN_CLASS TEXT("CLCL_PinnedImage")
#define SNIP_CLASS TEXT("CLCL_SnipOverlay")
#define HISTORY_MAX 16
#define ID_PIN_TOOLBAR 6140
#define TOOL_ICON_SIZE 22

#define ID_PEN         6100
#define ID_MARKER      6101
#define ID_ERASER      6102
#define ID_LINE        6103
#define ID_ARROW       6104
#define ID_RECT        6105
#define ID_ELLIPSE     6106
#define ID_CROP        6107
#define ID_UNDO        6109
#define ID_REDO        6110
#define ID_COPY        6111
#define ID_UPDATE      6112
#define ID_CLOSE       6115
#define ID_COLOR_RED   6120
#define ID_COLOR_BLUE  6121
#define ID_COLOR_GREEN 6122
#define ID_COLOR_BLACK 6123
#define ID_COLOR_DROPDOWN 6125
#define ID_WIDTH_3     6130
#define ID_WIDTH_6     6131
#define ID_WIDTH_12    6132
#define ID_SIZE_DROPDOWN 6133
#define ID_FILLED_RECT  6134
#define ID_FILLED_ELLIPSE 6135

#define PIN_DEFAULT_COLOR RGB(220, 32, 32)

typedef struct _PIN_ARTIFACT {
	struct _PIN_ARTIFACT *next;
	int tool, stroke, count, capacity;
	COLORREF color;
	BOOL deleted;
	POINT start, end;
	POINT *points;
} PIN_ARTIFACT;

typedef struct _PINNED_IMAGE {
	HWND hwnd;
	HWND owner;
	HWND toolbar;
	HIMAGELIST icons;
	HCURSOR tool_cursor;
	int cursor_tool, cursor_width;
	UINT cursor_dpi;
	int toolbar_height;
	int min_window_width;
	int min_window_height;
	HMENU menu;
	DATA_INFO *source_item;
	DATA_INFO *source_data;
	HBITMAP original;
	HBITMAP bitmap;
	HBITMAP undo[HISTORY_MAX];
	HBITMAP redo[HISTORY_MAX];
	HBITMAP undo_base[HISTORY_MAX];
	HBITMAP redo_base[HISTORY_MAX];
	PIN_ARTIFACT *artifacts;
	PIN_ARTIFACT *active_artifact;
	PIN_ARTIFACT *undo_artifacts[HISTORY_MAX];
	PIN_ARTIFACT *redo_artifacts[HISTORY_MAX];
	RECT undo_window[HISTORY_MAX];
	RECT redo_window[HISTORY_MAX];
	int undo_count;
	int redo_count;
	int width;
	int height;
	int tool;
	int stroke_width;
	int stroke_override;
	int color_index;
	COLORREF current_color;
	HBITMAP marker_base;
	BOOL drawing;
	BOOL erasing_changed;
	BOOL dirty;
	POINT start;
	POINT last;
	RECT image_rect;
	RECT selection;
	struct _PINNED_IMAGE *next;
} PINNED_IMAGE;

static const COLORREF colors[] = { RGB(220, 32, 32), RGB(30, 90, 220), RGB(20, 150, 70), RGB(20, 20, 20) };

static const COLORREF standard_colors[] = {
	RGB(0, 0, 0),       // Black
	RGB(100, 100, 100), // Dark Gray
	RGB(160, 160, 160), // Gray
	RGB(255, 255, 255), // White
	RGB(220, 32, 32),   // Red
	RGB(255, 140, 0),   // Orange
	RGB(255, 215, 0),   // Yellow
	RGB(34, 177, 76),   // Green
	RGB(0, 168, 143),   // Teal
	RGB(0, 162, 232),   // Cyan
	RGB(30, 90, 220),   // Blue
	RGB(15, 37, 120),   // Navy
	RGB(128, 0, 128),   // Purple
	RGB(236, 64, 122),  // Pink
	RGB(139, 69, 19),   // Brown
	RGB(192, 112, 0)    // Amber
};
#define STANDARD_COLOR_COUNT 16
static const TCHAR *standard_color_names[] = {
	TEXT("Black"), TEXT("Dark Gray"), TEXT("Gray"), TEXT("White"),
	TEXT("Red"), TEXT("Orange"), TEXT("Yellow"), TEXT("Green"),
	TEXT("Teal"), TEXT("Cyan"), TEXT("Blue"), TEXT("Navy"),
	TEXT("Purple"), TEXT("Pink"), TEXT("Brown"), TEXT("Amber")
};

static const COLORREF marker_colors[] = {
	RGB(255, 230, 0), RGB(255, 180, 0), RGB(255, 120, 0), RGB(255, 85, 65),
	RGB(235, 45, 55), RGB(225, 40, 145), RGB(255, 90, 175), RGB(175, 60, 205),
	RGB(125, 65, 225), RGB(75, 85, 220), RGB(35, 120, 235), RGB(35, 205, 235),
	RGB(0, 180, 170), RGB(170, 220, 35), RGB(50, 185, 85), RGB(60, 210, 155)
};
static const TCHAR *marker_color_names[] = {
	TEXT("Yellow"), TEXT("Gold"), TEXT("Orange"), TEXT("Coral"),
	TEXT("Red"), TEXT("Magenta"), TEXT("Pink"), TEXT("Purple"),
	TEXT("Violet"), TEXT("Indigo"), TEXT("Blue"), TEXT("Cyan"),
	TEXT("Teal"), TEXT("Lime"), TEXT("Green"), TEXT("Mint")
};

static HINSTANCE pin_instance;
static PINNED_IMAGE *pin_windows;
static DWORD last_auto_sequence, last_copy_sequence;
extern OPTION_INFO option;
extern TCHAR work_path[];

extern DATA_INFO history_data;
extern DATA_INFO regist_data;

static COLORREF get_pin_color(const PINNED_IMAGE *pin)
{
	if (pin == NULL) return PIN_DEFAULT_COLOR;
	return pin->current_color;
}

static void sync_color_menu(PINNED_IMAGE *pin)
{
	int id = 0;
	if (pin == NULL || pin->menu == NULL) return;
	if (pin->current_color == colors[0]) id = ID_COLOR_RED;
	else if (pin->current_color == colors[1]) id = ID_COLOR_BLUE;
	else if (pin->current_color == colors[2]) id = ID_COLOR_GREEN;
	else if (pin->current_color == colors[3]) id = ID_COLOR_BLACK;

	CheckMenuRadioItem(GetSubMenu(pin->menu, 3), ID_COLOR_RED, ID_COLOR_BLACK, id, MF_BYCOMMAND);
}

static void save_pinned_preferences(const PINNED_IMAGE *pin)
{
	TCHAR ini_path[MAX_PATH];
	if (pin == NULL) return;
	option.pinned_tool = pin->tool;
	option.pinned_stroke_width = pin->stroke_width;
	option.pinned_color = pin->current_color;
	if (work_path[0] != TEXT('\0')) {
		wsprintf(ini_path, TEXT("%s\\%s"), work_path, USER_INI);
		profile_write_int(TEXT("pinned"), TEXT("tool"), option.pinned_tool, ini_path);
		profile_write_int(TEXT("pinned"), TEXT("stroke_width"), option.pinned_stroke_width, ini_path);
		profile_write_int(TEXT("pinned"), TEXT("color"), (int)option.pinned_color, ini_path);
		profile_flush(ini_path);
	}
}

static LRESULT CALLBACK pinned_image_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
static LRESULT CALLBACK editor_toolbar_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam,
	UINT_PTR id, DWORD_PTR ref);
static void refit_image(PINNED_IMAGE *pin);
static void calculate_image_rect(PINNED_IMAGE *pin, const RECT *client);

static DATA_INFO *find_bitmap_data(DATA_INFO *item)
{
	DATA_INFO *di;
	if (item == NULL) return NULL;
	if (item->type == TYPE_DATA) di = item;
	else if (item->type == TYPE_ITEM) di = item->child;
	else return NULL;
	for (; di != NULL; di = di->next) {
		if (di->format == CF_BITMAP || di->format == CF_DIB || di->format == CF_DIBV5 ||
			(di->format_name != NULL && (lstrcmpi(di->format_name, TEXT("BITMAP")) == 0 ||
			 lstrcmpi(di->format_name, TEXT("DIB")) == 0))) return di;
		if (item->type == TYPE_DATA) break;
	}
	return NULL;
}

BOOL pinned_image_has_bitmap(DATA_INFO *source)
{
	return find_bitmap_data(source) != NULL;
}

static HBITMAP scale_bitmap(HBITMAP source, int target_width, int target_height)
{
	BITMAP bm;
	BITMAPINFO bi;
	HDC screen, src_dc, dst_dc;
	HBITMAP result, old_src, old_dst;
	void *bits;
	if (source == NULL || GetObject(source, sizeof(bm), &bm) == 0) return NULL;
	ZeroMemory(&bi, sizeof(bi));
	bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	if (target_width <= 0) target_width = bm.bmWidth;
	if (target_height <= 0) target_height = bm.bmHeight;
	bi.bmiHeader.biWidth = target_width;
	bi.bmiHeader.biHeight = -target_height;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;
	screen = GetDC(NULL);
	result = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
	src_dc = CreateCompatibleDC(screen);
	dst_dc = CreateCompatibleDC(screen);
	if (result != NULL && src_dc != NULL && dst_dc != NULL) {
		old_src = SelectObject(src_dc, source);
		old_dst = SelectObject(dst_dc, result);
		SetStretchBltMode(dst_dc, HALFTONE);
		SetBrushOrgEx(dst_dc, 0, 0, NULL);
		if (!StretchBlt(dst_dc, 0, 0, target_width, target_height,
			src_dc, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY)) {
			SelectObject(dst_dc, old_dst);
			DeleteObject(result);
			result = NULL;
		}
		SelectObject(src_dc, old_src);
		SelectObject(dst_dc, old_dst);
	} else if (result != NULL) {
		DeleteObject(result);
		result = NULL;
	}
	if (src_dc != NULL) DeleteDC(src_dc);
	if (dst_dc != NULL) DeleteDC(dst_dc);
	ReleaseDC(NULL, screen);
	return result;
}

static HBITMAP clone_bitmap(HBITMAP source, int *width, int *height)
{
	BITMAP bm;
	HBITMAP result = scale_bitmap(source, 0, 0);
	if (result != NULL && GetObject(result, sizeof(bm), &bm)) {
		if (width != NULL) *width = bm.bmWidth;
		if (height != NULL) *height = bm.bmHeight;
	}
	return result;
}

static void free_artifacts(PIN_ARTIFACT *item)
{
	while (item != NULL) {
		PIN_ARTIFACT *next = item->next;
		if (item->points != NULL) mem_free((void **)&item->points);
		mem_free((void **)&item);
		item = next;
	}
}

static PIN_ARTIFACT *clone_artifacts(const PIN_ARTIFACT *source)
{
	PIN_ARTIFACT *head = NULL, **tail = &head;
	for (; source != NULL; source = source->next) {
		PIN_ARTIFACT *item = mem_alloc(sizeof(*item));
		if (item == NULL) { free_artifacts(head); return NULL; }
		*item = *source;
		item->next = NULL;
		item->points = NULL;
		if (source->count > 0) {
			item->points = mem_alloc(sizeof(POINT) * source->count);
			if (item->points == NULL) { mem_free((void **)&item); free_artifacts(head); return NULL; }
			CopyMemory(item->points, source->points, sizeof(POINT) * source->count);
		}
		item->capacity = source->count;
		*tail = item;
		tail = &item->next;
	}
	return head;
}

static BOOL append_artifact_point(PIN_ARTIFACT *item, POINT point)
{
	if (item->count == item->capacity) {
		int capacity = item->capacity ? item->capacity * 2 : 16;
		POINT *points = mem_alloc(sizeof(POINT) * capacity);
		if (points == NULL) return FALSE;
		if (item->points != NULL) {
			CopyMemory(points, item->points, sizeof(POINT) * item->count);
			mem_free((void **)&item->points);
		}
		item->points = points;
		item->capacity = capacity;
	}
	item->points[item->count++] = point;
	item->end = point;
	return TRUE;
}

static PIN_ARTIFACT *add_artifact(PINNED_IMAGE *pin, int tool, POINT start, int stroke)
{
	PIN_ARTIFACT *item = mem_alloc(sizeof(*item)), **tail;
	if (item == NULL) return NULL;
	ZeroMemory(item, sizeof(*item));
	item->tool = tool;
	item->stroke = stroke;
	item->color = get_pin_color(pin);
	item->start = item->end = start;
	tail = &pin->artifacts;
	while (*tail != NULL) tail = &(*tail)->next;
	*tail = item;
	return item;
}

static void transform_artifacts(PINNED_IMAGE *pin, int old_width, int old_height,
	int new_width, int new_height, int left, int top)
{
	PIN_ARTIFACT *item;
	int i;
	for (item = pin->artifacts; item != NULL; item = item->next) {
		item->start.x = MulDiv(item->start.x - left, new_width, old_width);
		item->start.y = MulDiv(item->start.y - top, new_height, old_height);
		item->end.x = MulDiv(item->end.x - left, new_width, old_width);
		item->end.y = MulDiv(item->end.y - top, new_height, old_height);
		item->stroke = max(1, MulDiv(item->stroke, new_width, old_width));
		for (i = 0; i < item->count; i++) {
			item->points[i].x = MulDiv(item->points[i].x - left, new_width, old_width);
			item->points[i].y = MulDiv(item->points[i].y - top, new_height, old_height);
		}
	}
}

/* Client coordinates are physical pixels in this DPI-aware window. Grow only
 * before drawing, so resizing alone never resamples or modifies an image. */
static BOOL ensure_drawing_resolution(PINNED_IMAGE *pin)
{
	int width = pin->image_rect.right - pin->image_rect.left;
	int height = pin->image_rect.bottom - pin->image_rect.top;
	HBITMAP bitmap, original;
	if (width <= pin->width && height <= pin->height) return TRUE;
	width = max(width, pin->width);
	height = max(height, pin->height);
	bitmap = scale_bitmap(pin->bitmap, width, height);
	original = scale_bitmap(pin->original, width, height);
	if (bitmap == NULL || original == NULL) {
		if (bitmap != NULL) DeleteObject(bitmap);
		if (original != NULL) DeleteObject(original);
		return FALSE;
	}
	DeleteObject(pin->bitmap);
	DeleteObject(pin->original);
	pin->bitmap = bitmap;
	pin->original = original;
	transform_artifacts(pin, pin->width, pin->height, width, height, 0, 0);
	pin->width = width;
	pin->height = height;
	return TRUE;
}

static HBITMAP bitmap_from_data(DATA_INFO *di, int *width, int *height)
{
	HBITMAP source = NULL, result;
	BYTE *mem;
	BOOL free_source = FALSE;
	if (di == NULL || di->data == NULL) return NULL;
	if (di->format == CF_BITMAP || (di->format_name && lstrcmpi(di->format_name, TEXT("BITMAP")) == 0)) {
		source = (HBITMAP)di->data;
	} else {
		mem = GlobalLock(di->data);
		if (mem != NULL) {
			source = dib_to_bitmap(mem);
			GlobalUnlock(di->data);
			free_source = TRUE;
		}
	}
	result = clone_bitmap(source, width, height);
	if (free_source && source != NULL) DeleteObject(source);
	return result;
}

static void clear_stack(HBITMAP *stack, int *count)
{
	while (*count > 0) DeleteObject(stack[--(*count)]);
}

static void clear_edit_stack(HBITMAP *stack, HBITMAP *bases,
	PIN_ARTIFACT **artifacts, int *count)
{
	while (*count > 0) {
		int index = --(*count);
		DeleteObject(stack[index]);
		DeleteObject(bases[index]);
		free_artifacts(artifacts[index]);
	}
}

static void push_edit_stack(HBITMAP *stack, HBITMAP *bases,
	PIN_ARTIFACT **artifacts, RECT *windows, int *count,
	HBITMAP bitmap, HBITMAP base, PIN_ARTIFACT *items, RECT window)
{
	int i;
	if (*count == HISTORY_MAX) {
		DeleteObject(stack[0]);
		DeleteObject(bases[0]);
		free_artifacts(artifacts[0]);
		for (i = 1; i < HISTORY_MAX; i++) {
			stack[i - 1] = stack[i];
			bases[i - 1] = bases[i];
			artifacts[i - 1] = artifacts[i];
			windows[i - 1] = windows[i];
		}
		(*count)--;
	}
	stack[*count] = bitmap;
	bases[*count] = base;
	artifacts[*count] = items;
	windows[(*count)++] = window;
}

static void refresh_history_buttons(PINNED_IMAGE *pin)
{
	if (pin->toolbar == NULL) return;
	SendMessage(pin->toolbar, TB_ENABLEBUTTON, ID_UNDO, MAKELONG(pin->undo_count != 0, 0));
	SendMessage(pin->toolbar, TB_ENABLEBUTTON, ID_REDO, MAKELONG(pin->redo_count != 0, 0));
}

static BOOL begin_change(PINNED_IMAGE *pin)
{
	HBITMAP snapshot = clone_bitmap(pin->bitmap, NULL, NULL);
	HBITMAP base = pin->tool == ID_CROP ? clone_bitmap(pin->original, NULL, NULL) : NULL;
	PIN_ARTIFACT *items = clone_artifacts(pin->artifacts);
	RECT r = {0};
	if (snapshot == NULL || (pin->tool == ID_CROP && base == NULL) ||
		(pin->artifacts != NULL && items == NULL)) {
		if (snapshot != NULL) DeleteObject(snapshot);
		if (base != NULL) DeleteObject(base);
		free_artifacts(items);
		return FALSE;
	}
	if (pin->hwnd != NULL && IsWindow(pin->hwnd)) GetWindowRect(pin->hwnd, &r);
	push_edit_stack(pin->undo, pin->undo_base, pin->undo_artifacts,
		pin->undo_window, &pin->undo_count, snapshot, base, items, r);
	clear_edit_stack(pin->redo, pin->redo_base, pin->redo_artifacts, &pin->redo_count);
	pin->dirty = TRUE;
	refresh_history_buttons(pin);
	return TRUE;
}

static void swap_history(PINNED_IMAGE *pin, BOOL undo)
{
	HBITMAP current, replacement, current_base, replacement_base;
	PIN_ARTIFACT *current_items, *replacement_items;
	RECT current_rect = {0};
	int old_width = pin->width, old_height = pin->height;
	HBITMAP *from = undo ? pin->undo : pin->redo;
	HBITMAP *to = undo ? pin->redo : pin->undo;
	HBITMAP *from_base = undo ? pin->undo_base : pin->redo_base;
	HBITMAP *to_base = undo ? pin->redo_base : pin->undo_base;
	PIN_ARTIFACT **from_items = undo ? pin->undo_artifacts : pin->redo_artifacts;
	PIN_ARTIFACT **to_items = undo ? pin->redo_artifacts : pin->undo_artifacts;
	RECT *to_window = undo ? pin->redo_window : pin->undo_window;
	int *from_count = undo ? &pin->undo_count : &pin->redo_count;
	int *to_count = undo ? &pin->redo_count : &pin->undo_count;
	if (*from_count == 0) return;
	current = pin->bitmap;
	current_base = pin->original;
	current_items = pin->artifacts;
	if (pin->hwnd != NULL && IsWindow(pin->hwnd)) GetWindowRect(pin->hwnd, &current_rect);
	replacement = from[--(*from_count)];
	replacement_base = from_base[*from_count];
	replacement_items = from_items[*from_count];
	push_edit_stack(to, to_base, to_items, to_window, to_count,
		current, replacement_base != NULL ? current_base : NULL, current_items, current_rect);
	pin->bitmap = replacement;
	if (replacement_base != NULL) pin->original = replacement_base;
	pin->artifacts = replacement_items;
	pin->active_artifact = NULL;
	{
		BITMAP bm;
		GetObject(pin->bitmap, sizeof(bm), &bm);
		pin->width = bm.bmWidth;
		pin->height = bm.bmHeight;
	}
	if (pin->width != old_width || pin->height != old_height) refit_image(pin);
	if (replacement_base == NULL && (pin->width != old_width || pin->height != old_height)) {
		HBITMAP resized_base = scale_bitmap(pin->original, pin->width, pin->height);
		if (resized_base != NULL) {
			DeleteObject(pin->original);
			pin->original = resized_base;
		}
	}
	pin->dirty = TRUE;
	refresh_history_buttons(pin);
	InvalidateRect(pin->hwnd, NULL, FALSE);
}

static void calculate_image_rect(PINNED_IMAGE *pin, const RECT *client)
{
	int margin = Scale(10);
	int area_w = client->right - client->left - margin * 2;
	int area_h = client->bottom - client->top - pin->toolbar_height - margin * 2;
	int draw_w, draw_h;
	if (area_w <= 0 || area_h <= 0 || pin->width <= 0 || pin->height <= 0) {
		SetRectEmpty(&pin->image_rect);
		return;
	}
	draw_w = area_w;
	draw_h = MulDiv(draw_w, pin->height, pin->width);
	if (draw_h > area_h) {
		draw_h = area_h;
		draw_w = MulDiv(draw_h, pin->width, pin->height);
	}
	pin->image_rect.left = client->left + margin + (area_w - draw_w) / 2;
	pin->image_rect.top = client->top + pin->toolbar_height + margin + (area_h - draw_h) / 2;
	pin->image_rect.right = pin->image_rect.left + draw_w;
	pin->image_rect.bottom = pin->image_rect.top + draw_h;
}

static RECT desktop_bounds(void)
{
	RECT bounds;
	bounds.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
	bounds.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
	bounds.right = bounds.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
	bounds.bottom = bounds.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
	return bounds;
}

static BOOL is_desktop_image(int width, int height)
{
	RECT bounds = desktop_bounds();
	return width > 0 && height > 0 &&
		width == bounds.right - bounds.left && height == bounds.bottom - bounds.top;
}

static void clamp_pinned_window_to_work_area(PINNED_IMAGE *pin)
{
	RECT rect;
	MONITORINFO mi = { sizeof(mi) };
	HMONITOR hmon;
	int w, h, x, y, max_w, max_h;
	if (pin == NULL || pin->hwnd == NULL || IsIconic(pin->hwnd) || IsZoomed(pin->hwnd)) return;
	GetWindowRect(pin->hwnd, &rect);
	hmon = MonitorFromWindow(pin->hwnd, MONITOR_DEFAULTTONEAREST);
	if (!GetMonitorInfo(hmon, &mi)) {
		SystemParametersInfo(SPI_GETWORKAREA, 0, &mi.rcWork, 0);
	}

	max_w = mi.rcWork.right - mi.rcWork.left;
	max_h = mi.rcWork.bottom - mi.rcWork.top;
	w = rect.right - rect.left;
	h = rect.bottom - rect.top;

	if (w > max_w) w = max_w;
	if (h > max_h) h = max_h;

	x = rect.left;
	y = rect.top;

	if (x + w > mi.rcWork.right) x = mi.rcWork.right - w;
	if (x < mi.rcWork.left) x = mi.rcWork.left;
	if (y + h > mi.rcWork.bottom) y = mi.rcWork.bottom - h;
	if (y < mi.rcWork.top) y = mi.rcWork.top;

	if (x != rect.left || y != rect.top || w != (rect.right - rect.left) || h != (rect.bottom - rect.top)) {
		SetWindowPos(pin->hwnd, NULL, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
	}
}

static void refit_image(PINNED_IMAGE *pin)
{
	RECT client;
	if (pin->hwnd != NULL && GetClientRect(pin->hwnd, &client))
		calculate_image_rect(pin, &client);
}

static BOOL client_to_image(PINNED_IMAGE *pin, POINT point, POINT *image)
{
	if (!PtInRect(&pin->image_rect, point)) return FALSE;
	image->x = MulDiv(point.x - pin->image_rect.left, pin->width, pin->image_rect.right - pin->image_rect.left);
	image->y = MulDiv(point.y - pin->image_rect.top, pin->height, pin->image_rect.bottom - pin->image_rect.top);
	if (image->x >= pin->width) image->x = pin->width - 1;
	if (image->y >= pin->height) image->y = pin->height - 1;
	return TRUE;
}

static POINT image_to_client(PINNED_IMAGE *pin, POINT image)
{
	POINT point;
	point.x = pin->image_rect.left + MulDiv(image.x, pin->image_rect.right - pin->image_rect.left, pin->width);
	point.y = pin->image_rect.top + MulDiv(image.y, pin->image_rect.bottom - pin->image_rect.top, pin->height);
	return point;
}

static int image_stroke_width(PINNED_IMAGE *pin, int multiplier)
{
	if (pin->stroke_override > 0) return pin->stroke_override * multiplier;
	int shown_width = pin->image_rect.right - pin->image_rect.left;
	UINT dpi = pin->hwnd != NULL ? GetWindowDpi(pin->hwnd) : GetDpi();
	int screen_width = MulDiv(pin->stroke_width * multiplier, dpi != 0 ? dpi : 96, 96);
	return shown_width > 0 ? max(1, MulDiv(screen_width, pin->width, shown_width)) : max(1, screen_width);
}

static HCURSOR editor_tool_cursor(PINNED_IMAGE *pin)
{
	UINT dpi = GetWindowDpi(pin->hwnd);
	if (pin->tool != ID_PEN && pin->tool != ID_MARKER && pin->tool != ID_ERASER && pin->tool != ID_ARROW)
		return LoadCursor(NULL, IDC_CROSS);
	if (pin->tool_cursor != NULL && pin->cursor_tool == pin->tool &&
		pin->cursor_width == pin->stroke_width && pin->cursor_dpi == dpi) return pin->tool_cursor;
	if (pin->tool_cursor != NULL) DestroyCursor(pin->tool_cursor);
	pin->tool_cursor = NULL;
	if (pin->tool == ID_ERASER) {
		int radius = max(1, MulDiv(pin->stroke_width * 3, dpi, 96));
		int size = radius * 2 + 3, stride = ((size + 15) / 16) * 2, x, y;
		BYTE *mask = mem_alloc(stride * size * 2);
		if (mask != NULL) {
			BYTE *xor_mask = mask + stride * size;
			memset(mask, 255, stride * size);
			ZeroMemory(xor_mask, stride * size);
			for (y = 1; y < size - 1; y++) for (x = 1; x < size - 1; x++) {
				if (x == 1 || y == 1 || x == size - 2 || y == size - 2)
					xor_mask[y * stride + x / 8] |= 0x80 >> (x % 8);
			}
			pin->tool_cursor = CreateCursor(pin_instance, size / 2, size / 2, size, size, mask, xor_mask);
			mem_free(&mask);
		}
	} else {
		int size = MulDiv(32, dpi, 96);
		HICON icon = LoadImage(pin_instance, MAKEINTRESOURCE(pin->tool == ID_ARROW ? IDR_PIN_ARROW : pin->tool == ID_PEN ? IDR_PIN_PEN : IDR_PIN_MARKER),
			IMAGE_ICON, size, size, LR_DEFAULTCOLOR);
		ICONINFO info;
		if (icon != NULL) {
			if (GetIconInfo(icon, &info)) {
				info.fIcon = FALSE;
				info.xHotspot = MulDiv(pin->tool == ID_ARROW ? 27 : pin->tool == ID_PEN ? 5 : 16, size, 32);
				info.yHotspot = MulDiv(pin->tool == ID_ARROW ? 4 : 27, size, 32);
				pin->tool_cursor = CreateIconIndirect(&info);
				DeleteObject(info.hbmMask);
				if (info.hbmColor != NULL) DeleteObject(info.hbmColor);
			}
			DestroyIcon(icon);
		}
	}
	pin->cursor_tool = pin->tool;
	pin->cursor_width = pin->stroke_width;
	pin->cursor_dpi = dpi;
	return pin->tool_cursor != NULL ? pin->tool_cursor : LoadCursor(NULL, IDC_CROSS);
}

static void blend_marker_segment(PINNED_IMAGE *pin, POINT a, POINT b, int width)
{
	DIBSECTION dst, base;
	HBITMAP snapshot = pin->marker_base != NULL ? pin->marker_base :
		(pin->undo_count > 0 ? pin->undo[pin->undo_count - 1] : NULL);
	COLORREF color = get_pin_color(pin);
	int x, y, left, top, right, bottom;
	double dx = (double)b.x - a.x, dy = (double)b.y - a.y;
	double length2 = dx * dx + dy * dy, radius = width / 2.0;
	if (snapshot == NULL || GetObject(pin->bitmap, sizeof(dst), &dst) != sizeof(dst) ||
		GetObject(snapshot, sizeof(base), &base) != sizeof(base) ||
		dst.dsBm.bmBits == NULL || base.dsBm.bmBits == NULL) return;
	left = max(0, min(a.x, b.x) - width);
	top = max(0, min(a.y, b.y) - width);
	right = min(pin->width, max(a.x, b.x) + width + 1);
	bottom = min(pin->height, max(a.y, b.y) + width + 1);
	for (y = top; y < bottom; y++) {
		BYTE *target = (BYTE *)dst.dsBm.bmBits + y * dst.dsBm.bmWidthBytes;
		BYTE *original = (BYTE *)base.dsBm.bmBits + y * base.dsBm.bmWidthBytes;
		for (x = left; x < right; x++) {
			double along = length2 > 0 ? ((x - a.x) * dx + (y - a.y) * dy) / length2 : 0;
			double near_x, near_y;
			if (along < 0) along = 0;
			if (along > 1) along = 1;
			near_x = x - a.x - along * dx;
			near_y = y - a.y - along * dy;
			if (near_x * near_x + near_y * near_y <= radius * radius) {
				target[x * 4] = (BYTE)((original[x * 4] * 159 + GetBValue(color) * 96) / 255);
				target[x * 4 + 1] = (BYTE)((original[x * 4 + 1] * 159 + GetGValue(color) * 96) / 255);
				target[x * 4 + 2] = (BYTE)((original[x * 4 + 2] * 159 + GetRValue(color) * 96) / 255);
			}
		}
	}
}

static void draw_segment(PINNED_IMAGE *pin, POINT a, POINT b, BOOL erase)
{
	int stroke = image_stroke_width(pin, pin->tool == ID_MARKER ? 3 : 1);
	HDC dst;
	HBITMAP old_dst;
	UNREFERENCED_PARAMETER(erase);
	if (pin->tool == ID_MARKER) {
		GdiFlush();
		blend_marker_segment(pin, a, b, stroke);
		return;
	}
	dst = CreateCompatibleDC(NULL);
	old_dst = SelectObject(dst, pin->bitmap);
	{
		HPEN pen = CreatePen(PS_SOLID, stroke, get_pin_color(pin));
		HPEN old_pen = SelectObject(dst, pen);
		MoveToEx(dst, a.x, a.y, NULL);
		LineTo(dst, b.x, b.y);
		SelectObject(dst, old_pen);
		DeleteObject(pen);
	}
	SelectObject(dst, old_dst);
	DeleteDC(dst);
}

static void finish_shape(PINNED_IMAGE *pin, POINT end)
{
	HDC dc = CreateCompatibleDC(NULL);
	HBITMAP old_bitmap = SelectObject(dc, pin->bitmap);
	int stroke = image_stroke_width(pin, 1);
	HPEN pen = CreatePen(PS_SOLID, stroke, get_pin_color(pin));
	HPEN old_pen = SelectObject(dc, pen);
	HBRUSH fill = (pin->tool == ID_FILLED_RECT || pin->tool == ID_FILLED_ELLIPSE) ?
		CreateSolidBrush(get_pin_color(pin)) : NULL;
	HBRUSH old_brush = SelectObject(dc, fill != NULL ? fill : GetStockObject(HOLLOW_BRUSH));
	int left = min(pin->start.x, end.x), right = max(pin->start.x, end.x);
	int top = min(pin->start.y, end.y), bottom = max(pin->start.y, end.y);
	if (pin->tool == ID_LINE || pin->tool == ID_ARROW) {
		MoveToEx(dc, pin->start.x, pin->start.y, NULL);
		LineTo(dc, end.x, end.y);
		if (pin->tool == ID_ARROW) {
			double angle = atan2((double)(end.y - pin->start.y), (double)(end.x - pin->start.x));
			int size = max(stroke * 4, image_stroke_width(pin, 3));
			POINT wing1 = { end.x - (int)(cos(angle - 0.55) * size), end.y - (int)(sin(angle - 0.55) * size) };
			POINT wing2 = { end.x - (int)(cos(angle + 0.55) * size), end.y - (int)(sin(angle + 0.55) * size) };
			MoveToEx(dc, end.x, end.y, NULL); LineTo(dc, wing1.x, wing1.y);
			MoveToEx(dc, end.x, end.y, NULL); LineTo(dc, wing2.x, wing2.y);
		}
	} else if (pin->tool == ID_RECT || pin->tool == ID_FILLED_RECT) {
		Rectangle(dc, left, top, right, bottom);
	} else if (pin->tool == ID_ELLIPSE || pin->tool == ID_FILLED_ELLIPSE) {
		Ellipse(dc, left, top, right, bottom);
	}
	SelectObject(dc, old_brush);
	if (fill != NULL) DeleteObject(fill);
	SelectObject(dc, old_pen);
	DeleteObject(pen);
	SelectObject(dc, old_bitmap);
	DeleteDC(dc);
}

static BOOL rebuild_artifacts(PINNED_IMAGE *pin)
{
	HBITMAP old_bitmap = pin->bitmap, rebuilt = clone_bitmap(pin->original, NULL, NULL);
	PIN_ARTIFACT *item;
	int old_tool = pin->tool, old_stroke = pin->stroke_override;
	COLORREF old_color = pin->current_color;
	POINT old_start = pin->start;
	if (rebuilt == NULL) return FALSE;
	pin->bitmap = rebuilt;
	for (item = pin->artifacts; item != NULL; item = item->next) {
		int i;
		if (item->deleted) continue;
		pin->tool = item->tool;
		pin->current_color = item->color;
		pin->stroke_override = item->stroke;
		pin->start = item->start;
		if (item->tool == ID_PEN || item->tool == ID_MARKER) {
			if (item->tool == ID_MARKER) {
				pin->marker_base = clone_bitmap(pin->bitmap, NULL, NULL);
				if (pin->marker_base == NULL) break;
			}
			for (i = 0; i < item->count; i++)
				draw_segment(pin, i ? item->points[i - 1] : item->points[0], item->points[i], FALSE);
			if (pin->marker_base != NULL) { DeleteObject(pin->marker_base); pin->marker_base = NULL; }
		} else finish_shape(pin, item->end);
	}
	pin->tool = old_tool;
	pin->current_color = old_color;
	pin->stroke_override = old_stroke;
	pin->start = old_start;
	if (item != NULL) {
		DeleteObject(rebuilt);
		pin->bitmap = old_bitmap;
		return FALSE;
	}
	DeleteObject(old_bitmap);
	return TRUE;
}

static double segment_distance_squared(POINT p, POINT a, POINT b)
{
	double dx = (double)b.x - a.x, dy = (double)b.y - a.y;
	double length = dx * dx + dy * dy;
	double t = length > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / length : 0;
	if (t < 0) t = 0;
	if (t > 1) t = 1;
	dx = p.x - (a.x + t * dx);
	dy = p.y - (a.y + t * dy);
	return dx * dx + dy * dy;
}

static BOOL artifact_hit(const PIN_ARTIFACT *item, POINT p, int eraser_radius)
{
	int left = min(item->start.x, item->end.x), right = max(item->start.x, item->end.x);
	int top = min(item->start.y, item->end.y), bottom = max(item->start.y, item->end.y);
	double tolerance = (item->stroke * (item->tool == ID_MARKER ? 3 : 1)) / 2.0 + eraser_radius;
	int i;
	if (item->deleted) return FALSE;
	if (item->tool == ID_PEN || item->tool == ID_MARKER) {
		for (i = 0; i < item->count; i++)
			if (segment_distance_squared(p, i ? item->points[i - 1] : item->points[0], item->points[i]) <= tolerance * tolerance)
				return TRUE;
		return FALSE;
	}
	if (item->tool == ID_LINE || item->tool == ID_ARROW) {
		if (segment_distance_squared(p, item->start, item->end) <= tolerance * tolerance) return TRUE;
		if (item->tool == ID_ARROW) {
			double angle = atan2((double)(item->end.y - item->start.y), (double)(item->end.x - item->start.x));
			int size = max(item->stroke * 4, item->stroke * 3);
			POINT wing1 = { item->end.x - (int)(cos(angle - 0.55) * size), item->end.y - (int)(sin(angle - 0.55) * size) };
			POINT wing2 = { item->end.x - (int)(cos(angle + 0.55) * size), item->end.y - (int)(sin(angle + 0.55) * size) };
			return segment_distance_squared(p, item->end, wing1) <= tolerance * tolerance ||
				segment_distance_squared(p, item->end, wing2) <= tolerance * tolerance;
		}
		return FALSE;
	}
	if (p.x < left - tolerance || p.x > right + tolerance ||
		p.y < top - tolerance || p.y > bottom + tolerance) return FALSE;
	if (item->tool == ID_FILLED_RECT) return p.x >= left && p.x <= right && p.y >= top && p.y <= bottom;
	if (item->tool == ID_RECT) return fabs((double)p.x - left) <= tolerance ||
		fabs((double)p.x - right) <= tolerance || fabs((double)p.y - top) <= tolerance ||
		fabs((double)p.y - bottom) <= tolerance;
	if (item->tool == ID_ELLIPSE || item->tool == ID_FILLED_ELLIPSE) {
		double rx = (right - left) / 2.0, ry = (bottom - top) / 2.0;
		double dx = p.x - (left + rx), dy = p.y - (top + ry);
		double distance = sqrt(dx * dx + dy * dy), normalized;
		if (rx <= 0 || ry <= 0) return FALSE;
		normalized = sqrt(dx * dx / (rx * rx) + dy * dy / (ry * ry));
		if (item->tool == ID_FILLED_ELLIPSE) return normalized <= 1;
		return normalized > 0 && fabs(distance - distance / normalized) <= tolerance;
	}
	return FALSE;
}

static BOOL erase_artifacts(PINNED_IMAGE *pin, POINT from, POINT to)
{
	PIN_ARTIFACT *item;
	BOOL found = FALSE;
	int radius = image_stroke_width(pin, 3);
	int length = (int)sqrt((double)(to.x - from.x) * (to.x - from.x) +
		(double)(to.y - from.y) * (to.y - from.y));
	int steps = max(1, length / max(1, radius / 2)), step;
	for (item = pin->artifacts; item != NULL && !found; item = item->next)
		for (step = 0; step <= steps; step++) {
			POINT p = { from.x + MulDiv(to.x - from.x, step, steps),
				from.y + MulDiv(to.y - from.y, step, steps) };
			if (artifact_hit(item, p, radius)) { found = TRUE; break; }
		}
	if (!found) return FALSE;
	if (!pin->erasing_changed && !begin_change(pin)) return FALSE;
	for (item = pin->artifacts; item != NULL; item = item->next)
		for (step = 0; !item->deleted && step <= steps; step++) {
			POINT p = { from.x + MulDiv(to.x - from.x, step, steps),
				from.y + MulDiv(to.y - from.y, step, steps) };
			if (artifact_hit(item, p, radius)) item->deleted = 2;
		}
	if (!rebuild_artifacts(pin)) {
		for (item = pin->artifacts; item != NULL; item = item->next)
			if (item->deleted == 2) item->deleted = FALSE;
		return FALSE;
	}
	for (item = pin->artifacts; item != NULL; item = item->next)
		if (item->deleted == 2) item->deleted = TRUE;
	pin->erasing_changed = TRUE;
	return TRUE;
}

static HBITMAP crop_bitmap(HBITMAP source, RECT crop, int *width, int *height)
{
	HDC screen = GetDC(NULL), src = CreateCompatibleDC(screen), dst = CreateCompatibleDC(screen);
	BITMAPINFO bi = {0};
	void *bits;
	HBITMAP result;
	HBITMAP old_src = NULL, old_dst = NULL;
	bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth = crop.right - crop.left;
	bi.bmiHeader.biHeight = -(crop.bottom - crop.top);
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;
	result = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
	if (result != NULL && src != NULL && dst != NULL) {
		old_src = SelectObject(src, source);
		old_dst = SelectObject(dst, result);
		BitBlt(dst, 0, 0, crop.right - crop.left, crop.bottom - crop.top, src, crop.left, crop.top, SRCCOPY);
		SelectObject(src, old_src);
		SelectObject(dst, old_dst);
		*width = crop.right - crop.left;
		*height = crop.bottom - crop.top;
	} else if (result != NULL) {
		DeleteObject(result);
		result = NULL;
	}
	if (src != NULL) DeleteDC(src);
	if (dst != NULL) DeleteDC(dst);
	ReleaseDC(NULL, screen);
	return result;
}

static BOOL copy_bitmap_to_clipboard(HWND owner, HBITMAP bitmap)
{
	DWORD size = 0;
	BYTE *bytes = bitmap_to_dib(bitmap, &size);
	HGLOBAL dib;
	void *memory;
	HBITMAP copy;
	if (bytes == NULL) return FALSE;
	dib = GlobalAlloc(GMEM_MOVEABLE, size);
	memory = dib != NULL ? GlobalLock(dib) : NULL;
	if (memory == NULL) {
		if (dib != NULL) GlobalFree(dib);
		mem_free(&bytes);
		return FALSE;
	}
	CopyMemory(memory, bytes, size);
	GlobalUnlock(dib);
	mem_free(&bytes);
	copy = clone_bitmap(bitmap, NULL, NULL);
	if (!OpenClipboard(owner)) {
		GlobalFree(dib);
		if (copy != NULL) DeleteObject(copy);
		return FALSE;
	}
	if (!EmptyClipboard() || SetClipboardData(CF_DIB, dib) == NULL) {
		GlobalFree(dib);
		if (copy != NULL) DeleteObject(copy);
		CloseClipboard();
		return FALSE;
	}
	// DIB owns complete pixel data; BITMAP is an optional compatibility format.
	if (copy != NULL && SetClipboardData(CF_BITMAP, copy) == NULL) DeleteObject(copy);
	last_copy_sequence = GetClipboardSequenceNumber();
	CloseClipboard();
	return TRUE;
}


static BOOL replace_source(PINNED_IMAGE *pin)
{
	DATA_INFO *owner, *parent;
	HANDLE replacement = NULL;
	HANDLE old_data;
	DWORD old_size;
	DWORD size = 0;
	BYTE *dib;
	if (pin->source_data == NULL) return FALSE;
	owner = pin->source_item;
	if (owner != pin->source_data && data_check(&history_data, owner) == NULL && data_check(&regist_data, owner) == NULL) return FALSE;
	if (pin->source_data->format == CF_BITMAP ||
		(pin->source_data->format_name && lstrcmpi(pin->source_data->format_name, TEXT("BITMAP")) == 0)) {
		replacement = clone_bitmap(pin->bitmap, NULL, NULL);
		if (replacement == NULL) return FALSE;
		dib = bitmap_to_dib((HBITMAP)replacement, &size);
		if (dib == NULL) { DeleteObject(replacement); return FALSE; }
		mem_free(&dib);
	} else {
		dib = bitmap_to_dib(pin->bitmap, &size);
		if (dib == NULL) return FALSE;
		replacement = GlobalAlloc(GMEM_MOVEABLE, size);
		if (replacement != NULL) {
			void *memory = GlobalLock(replacement);
			if (memory != NULL) CopyMemory(memory, dib, size);
			GlobalUnlock(replacement);
		}
		mem_free(&dib);
		if (replacement == NULL) return FALSE;
	}
	old_data = pin->source_data->data;
	old_size = pin->source_data->size;
	pin->source_data->data = replacement;
	pin->source_data->size = size;
	data_menu_free_item(pin->source_data);
	if (owner != pin->source_data) {
		owner->content_hash = 0;
		if (data_check(&history_data, owner) != NULL && db_history_is_open()) {
			if (!db_history_update_item(owner)) {
				pin->source_data->data = old_data;
				pin->source_data->size = old_size;
				if (!format_free_data(pin->source_data->format_name, replacement))
					clipboard_free_data(pin->source_data->format_name, replacement);
				return FALSE;
			}
		}
		parent = data_check(&history_data, owner);
		if (parent != NULL) SendMessage(pin->owner, WM_HISTORY_CHANGED, 0, 0);
		else if (data_check(&regist_data, owner) != NULL) SendMessage(pin->owner, WM_REGIST_CHANGED, 0, 0);
	}
	if (old_data != NULL && !format_free_data(pin->source_data->format_name, old_data))
		clipboard_free_data(pin->source_data->format_name, old_data);
	DeleteObject(pin->original);
	pin->original = clone_bitmap(pin->bitmap, NULL, NULL);
	free_artifacts(pin->artifacts);
	pin->artifacts = pin->active_artifact = NULL;
	pin->dirty = FALSE;
	clear_edit_stack(pin->undo, pin->undo_base, pin->undo_artifacts, &pin->undo_count);
	clear_edit_stack(pin->redo, pin->redo_base, pin->redo_artifacts, &pin->redo_count);
	return TRUE;
}

static HMENU add_submenu(HMENU menu, const TCHAR *title)
{
	HMENU popup = CreatePopupMenu();
	if (popup == NULL) return NULL;
	if (!AppendMenu(menu, MF_POPUP, (UINT_PTR)popup, title)) {
		DestroyMenu(popup);
		return NULL;
	}
	return popup;
}

static void theme_editor_popups(HMENU menu)
{
	int i, j;
	for (i = 0; i < GetMenuItemCount(menu); i++) {
		HMENU popup = GetSubMenu(menu, i);
		for (j = 0; popup != NULL && j < GetMenuItemCount(popup); j++) {
			MENUITEMINFO item = { sizeof(item) };
			item.fMask = MIIM_FTYPE | MIIM_DATA;
			GetMenuItemInfo(popup, j, TRUE, &item);
			if (!(item.fType & MFT_SEPARATOR)) {
				item.fType |= MFT_OWNERDRAW;
				item.dwItemData = (ULONG_PTR)popup;
				SetMenuItemInfo(popup, j, TRUE, &item);
			}
		}
	}
}

static HMENU create_editor_menu(BOOL can_update)
{
	HMENU menu = CreateMenu();
	HMENU image, edit, tools, color, size;
	if (menu == NULL) return NULL;
	image = add_submenu(menu, TEXT("&Image"));
	edit = add_submenu(menu, TEXT("&Edit"));
	tools = add_submenu(menu, TEXT("&Tools"));
	color = add_submenu(menu, TEXT("&Color"));
	size = add_submenu(menu, TEXT("&Size"));
	if (image == NULL || edit == NULL || tools == NULL || color == NULL || size == NULL) {
		DestroyMenu(menu);
		return NULL;
	}
	if (!AppendMenu(image, MF_STRING, ID_COPY, TEXT("&Copy and close\tCtrl+C")) ||
		!AppendMenu(image, MF_STRING, ID_UPDATE, TEXT("&Update original")) ||
		!AppendMenu(image, MF_SEPARATOR, 0, NULL) ||
		!AppendMenu(image, MF_STRING, ID_CLOSE, TEXT("&Close\tAlt+F4")) ||
		!AppendMenu(edit, MF_STRING, ID_UNDO, TEXT("&Undo\tCtrl+Z")) ||
		!AppendMenu(edit, MF_STRING, ID_REDO, TEXT("&Redo\tCtrl+Y")) ||
		!AppendMenu(tools, MF_STRING, ID_PEN, TEXT("&Pen")) ||
		!AppendMenu(tools, MF_STRING, ID_MARKER, TEXT("&Highlighter")) ||
		!AppendMenu(tools, MF_STRING, ID_ERASER, TEXT("&Eraser")) ||
		!AppendMenu(tools, MF_SEPARATOR, 0, NULL) ||
		!AppendMenu(tools, MF_STRING, ID_LINE, TEXT("&Line")) ||
		!AppendMenu(tools, MF_STRING, ID_ARROW, TEXT("&Arrow")) ||
		!AppendMenu(tools, MF_STRING, ID_RECT, TEXT("&Rectangle")) ||
		!AppendMenu(tools, MF_STRING, ID_FILLED_RECT, TEXT("Filled rectangle")) ||
		!AppendMenu(tools, MF_STRING, ID_ELLIPSE, TEXT("&Ellipse")) ||
		!AppendMenu(tools, MF_STRING, ID_FILLED_ELLIPSE, TEXT("Filled ellipse")) ||
		!AppendMenu(tools, MF_SEPARATOR, 0, NULL) ||
		!AppendMenu(tools, MF_STRING, ID_CROP, TEXT("&Crop")) ||
		!AppendMenu(color, MF_STRING, ID_COLOR_RED, TEXT("&Red")) ||
		!AppendMenu(color, MF_STRING, ID_COLOR_BLUE, TEXT("&Blue")) ||
		!AppendMenu(color, MF_STRING, ID_COLOR_GREEN, TEXT("&Green")) ||
		!AppendMenu(color, MF_STRING, ID_COLOR_BLACK, TEXT("&Black")) ||
		!AppendMenu(size, MF_STRING, ID_WIDTH_3, TEXT("&3 px")) ||
		!AppendMenu(size, MF_STRING, ID_WIDTH_6, TEXT("&6 px")) ||
		!AppendMenu(size, MF_STRING, ID_WIDTH_12, TEXT("&12 px"))) {
		DestroyMenu(menu);
		return NULL;
	}
	if (!can_update) EnableMenuItem(image, ID_UPDATE, MF_BYCOMMAND | MF_GRAYED);
	CheckMenuRadioItem(tools, ID_PEN, ID_CROP, ID_CROP, MF_BYCOMMAND);
	CheckMenuRadioItem(color, ID_COLOR_RED, ID_COLOR_BLACK, ID_COLOR_RED, MF_BYCOMMAND);
	CheckMenuRadioItem(size, ID_WIDTH_3, ID_WIDTH_12, ID_WIDTH_3, MF_BYCOMMAND);
	return menu;
}

static const UINT toolbar_icons[] = {
	IDR_PIN_UNDO, IDR_PIN_REDO,
	IDR_PIN_PEN, IDR_PIN_MARKER, IDR_PIN_ERASER, IDR_PIN_LINE, IDR_PIN_ARROW,
	IDR_PIN_RECT, IDR_PIN_FILLED_RECT, IDR_PIN_ELLIPSE,
	IDR_PIN_FILLED_ELLIPSE, IDR_PIN_CROP, IDR_PIN_COPY
};
static const UINT toolbar_commands[] = {
	ID_UNDO, ID_REDO, ID_PEN, ID_MARKER, ID_ERASER, ID_LINE, ID_ARROW,
	ID_RECT, ID_FILLED_RECT, ID_ELLIPSE, ID_FILLED_ELLIPSE, ID_CROP, ID_COPY
};
static const TCHAR *tool_tips[] = {
	TEXT("Pen"), TEXT("Highlighter"), TEXT("Eraser"), TEXT("Line"), TEXT("Arrow"),
	TEXT("Rectangle"), TEXT("Ellipse"), TEXT("Crop")
};

static HICON create_color_swatch_icon(COLORREF color, int size)
{
	BITMAPV5HEADER bi;
	HDC screen;
	void *bits = NULL;
	HBITMAP hbmColor, hbmMask;
	ICONINFO ii;
	HICON icon;
	int cx, cy, r_outer, r_inner;
	BYTE r, g, b;
	int x, y;
	RGBQUAD *pixels;

	if (size <= 0) size = Scale(TOOL_ICON_SIZE);
	if (size <= 0) size = 22;

	ZeroMemory(&bi, sizeof(bi));
	bi.bV5Size = sizeof(BITMAPV5HEADER);
	bi.bV5Width = size;
	bi.bV5Height = size;
	bi.bV5Planes = 1;
	bi.bV5BitCount = 32;
	bi.bV5Compression = BI_BITFIELDS;
	bi.bV5RedMask   = 0x00FF0000;
	bi.bV5GreenMask = 0x0000FF00;
	bi.bV5BlueMask  = 0x000000FF;
	bi.bV5AlphaMask = 0xFF000000;

	screen = GetDC(NULL);
	hbmColor = CreateDIBSection(screen, (BITMAPINFO *)&bi, DIB_RGB_COLORS, &bits, NULL, 0);
	ReleaseDC(NULL, screen);

	if (hbmColor == NULL || bits == NULL) return NULL;

	pixels = (RGBQUAD *)bits;
	ZeroMemory(pixels, size * size * sizeof(RGBQUAD));

	r = GetRValue(color);
	g = GetGValue(color);
	b = GetBValue(color);

	cx = size / 2;
	cy = size / 2;
	r_outer = size / 2 - 2;
	if (r_outer < 4) r_outer = 4;
	r_inner = r_outer - 1;

	for (y = 0; y < size; y++) {
		for (x = 0; x < size; x++) {
			int dx = x - cx;
			int dy = y - cy;
			int d2 = dx * dx + dy * dy;
			RGBQUAD *p = &pixels[y * size + x];

			if (d2 <= r_inner * r_inner) {
				p->rgbRed = r;
				p->rgbGreen = g;
				p->rgbBlue = b;
				p->rgbReserved = 255;
			} else if (d2 <= r_outer * r_outer) {
				p->rgbRed = 95;
				p->rgbGreen = 100;
				p->rgbBlue = 105;
				p->rgbReserved = 255;
			}
		}
	}

	hbmMask = CreateBitmap(size, size, 1, 1, NULL);
	ZeroMemory(&ii, sizeof(ii));
	ii.fIcon = TRUE;
	ii.hbmMask = hbmMask;
	ii.hbmColor = hbmColor;
	icon = CreateIconIndirect(&ii);

	DeleteObject(hbmColor);
	DeleteObject(hbmMask);

	return icon;
}

static void update_color_button_icon(PINNED_IMAGE *pin)
{
	HICON icon;
	int size;
	if (pin == NULL || pin->icons == NULL || pin->toolbar == NULL) return;
	size = Scale(TOOL_ICON_SIZE);
	icon = create_color_swatch_icon(get_pin_color(pin), size);
	if (icon != NULL) {
		ImageList_ReplaceIcon(pin->icons, 13, icon);
		DestroyIcon(icon);
		InvalidateRect(pin->toolbar, NULL, TRUE);
	}
}

static HWND current_color_popup_hwnd = NULL;
static DWORD last_color_popup_toggle = 0;
static COLORREF custom_colors[16] = {0};
static int popup_hovered_index = -1;

static RECT get_swatch_rect(int i)
{
	RECT r;
	int swatch_size = Scale(20);
	int swatch_gap = Scale(4);
	int pad = Scale(8);
	int col = i % 8;
	int row = i / 8;
	r.left = pad + col * (swatch_size + swatch_gap);
	r.top = pad + row * (swatch_size + swatch_gap);
	r.right = r.left + swatch_size;
	r.bottom = r.top + swatch_size;
	return r;
}

static RECT get_more_colors_rect(HWND hwnd)
{
	RECT client, r;
	int swatch_size = Scale(20);
	int swatch_gap = Scale(4);
	int pad = Scale(8);
	int grid_h = 2 * swatch_size + swatch_gap;
	int sep_h = Scale(6);
	int more_h = Scale(24);
	GetClientRect(hwnd, &client);
	r.left = pad;
	r.top = pad + grid_h + sep_h;
	r.right = client.right - pad;
	r.bottom = r.top + more_h;
	return r;
}

static int hit_test_popup(HWND hwnd, POINT pt)
{
	int i;
	RECT r;
	for (i = 0; i < STANDARD_COLOR_COUNT; i++) {
		r = get_swatch_rect(i);
		if (PtInRect(&r, pt)) return i;
	}
	r = get_more_colors_rect(hwnd);
	if (PtInRect(&r, pt)) return 16;
	return -1;
}

static LRESULT CALLBACK color_popup_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	switch (msg) {
	case WM_CREATE:
	{
		CREATESTRUCT *cs = (CREATESTRUCT *)lparam;
		SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
		popup_hovered_index = -1;
		return 0;
	}
	case WM_MOUSEMOVE:
	{
		POINT pt = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
		int hit = hit_test_popup(hwnd, pt);
		if (hit != popup_hovered_index) {
			popup_hovered_index = hit;
			InvalidateRect(hwnd, NULL, FALSE);
		}
		return 0;
	}
	case WM_LBUTTONDOWN:
	case WM_RBUTTONDOWN:
	case WM_MBUTTONDOWN:
	{
		POINT pt = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
		RECT client;
		GetClientRect(hwnd, &client);
		if (!PtInRect(&client, pt)) {
			ReleaseCapture();
			DestroyWindow(hwnd);
			return 0;
		}
		return 0;
	}
	case WM_LBUTTONUP:
	{
		POINT pt = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
		int hit = hit_test_popup(hwnd, pt);
		PINNED_IMAGE *pin = (PINNED_IMAGE *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
		ReleaseCapture();
		DestroyWindow(hwnd);
		if (pin != NULL && hit >= 0 && hit < STANDARD_COLOR_COUNT) {
			pin->current_color = pin->tool == ID_MARKER ? marker_colors[hit] : standard_colors[hit];
			sync_color_menu(pin);
			update_color_button_icon(pin);
			save_pinned_preferences(pin);
			InvalidateRect(pin->hwnd, NULL, FALSE);
		} else if (pin != NULL && hit == 16) {
			CHOOSECOLOR cc;
			ZeroMemory(&cc, sizeof(cc));
			cc.lStructSize = sizeof(cc);
			cc.hwndOwner = pin->hwnd;
			cc.rgbResult = get_pin_color(pin);
			cc.lpCustColors = custom_colors;
			cc.Flags = CC_FULLOPEN | CC_RGBINIT;
			if (ChooseColor(&cc)) {
				pin->current_color = cc.rgbResult;
				sync_color_menu(pin);
				update_color_button_icon(pin);
				save_pinned_preferences(pin);
				InvalidateRect(pin->hwnd, NULL, FALSE);
			}
		}
		return 0;
	}
	case WM_KEYDOWN:
		if (wparam == VK_ESCAPE) {
			ReleaseCapture();
			DestroyWindow(hwnd);
			return 0;
		}
		break;
	case WM_ACTIVATE:
		if (LOWORD(wparam) == WA_INACTIVE) {
			ReleaseCapture();
			DestroyWindow(hwnd);
			return 0;
		}
		break;
	case WM_KILLFOCUS:
		ReleaseCapture();
		DestroyWindow(hwnd);
		return 0;
	case WM_DESTROY:
		if (GetCapture() == hwnd) ReleaseCapture();
		if (current_color_popup_hwnd == hwnd) current_color_popup_hwnd = NULL;
		popup_hovered_index = -1;
		return 0;
	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		HDC dc = BeginPaint(hwnd, &ps);
		HDC mem_dc;
		HBITMAP mem_bmp, old_bmp;
		RECT client;
		HBRUSH bg_brush, border_brush;
		BOOL dark = !dark_mode_is_dark();
		PINNED_IMAGE *pin = (PINNED_IMAGE *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
		COLORREF active_color = get_pin_color(pin);
		const COLORREF *palette = pin != NULL && pin->tool == ID_MARKER ? marker_colors : standard_colors;
		const TCHAR *const *palette_names = pin != NULL && pin->tool == ID_MARKER ? marker_color_names : standard_color_names;
		int i;

		GetClientRect(hwnd, &client);
		mem_dc = CreateCompatibleDC(dc);
		mem_bmp = CreateCompatibleBitmap(dc, client.right, client.bottom);
		old_bmp = SelectObject(mem_dc, mem_bmp);

		bg_brush = CreateSolidBrush(dark ? RGB(34, 38, 44) : RGB(255, 255, 255));
		FillRect(mem_dc, &client, bg_brush);
		DeleteObject(bg_brush);

		border_brush = CreateSolidBrush(dark ? RGB(68, 73, 82) : RGB(204, 209, 216));
		FrameRect(mem_dc, &client, border_brush);
		DeleteObject(border_brush);

		for (i = 0; i < STANDARD_COLOR_COUNT; i++) {
			RECT sr = get_swatch_rect(i);
			COLORREF col = palette[i];
			HBRUSH brush = CreateSolidBrush(col);
			HBRUSH inner_b;
			FillRect(mem_dc, &sr, brush);
			DeleteObject(brush);

			inner_b = CreateSolidBrush(dark ?
				(col == RGB(0,0,0) ? RGB(75, 80, 88) : RGB(50, 54, 60)) :
				(col == RGB(255,255,255) ? RGB(210, 214, 220) : RGB(225, 228, 233)));
			FrameRect(mem_dc, &sr, inner_b);
			DeleteObject(inner_b);

			if (active_color == col) {
				RECT ring = sr;
				HBRUSH ab = CreateSolidBrush(dark ? RGB(80, 160, 255) : RGB(0, 120, 215));
				InflateRect(&ring, Scale(2), Scale(2));
				FrameRect(mem_dc, &ring, ab);
				InflateRect(&ring, -1, -1);
				FrameRect(mem_dc, &ring, ab);
				DeleteObject(ab);

				{
					int cx = (sr.left + sr.right) / 2;
					int cy = (sr.top + sr.bottom) / 2;
					int dr = Scale(2);
					RECT dot = { cx - dr, cy - dr, cx + dr + 1, cy + dr + 1 };
					COLORREF dot_col = (col == RGB(255, 255, 255) || col == RGB(255, 215, 0)) ?
						RGB(20, 20, 20) : RGB(255, 255, 255);
					HBRUSH db = CreateSolidBrush(dot_col);
					FillRect(mem_dc, &dot, db);
					DeleteObject(db);
				}
			} else if (popup_hovered_index == i) {
				RECT hring = sr;
				HBRUSH hb = CreateSolidBrush(dark ? RGB(140, 150, 165) : RGB(100, 110, 125));
				InflateRect(&hring, Scale(2), Scale(2));
				FrameRect(mem_dc, &hring, hb);
				DeleteObject(hb);
			}
		}

		{
			int swatch_size = Scale(20);
			int swatch_gap = Scale(4);
			int pad = Scale(8);
			int sep_y = pad + (2 * swatch_size + swatch_gap) + Scale(3);
			HPEN sep_pen = CreatePen(PS_SOLID, 1, dark ? RGB(52, 57, 65) : RGB(228, 231, 236));
			HPEN old_pen = SelectObject(mem_dc, sep_pen);
			MoveToEx(mem_dc, pad, sep_y, NULL);
			LineTo(mem_dc, client.right - pad, sep_y);
			SelectObject(mem_dc, old_pen);
			DeleteObject(sep_pen);
		}

		{
			RECT mr = get_more_colors_rect(hwnd);
			RECT tr = mr;
			HFONT font, old_font;

			if (popup_hovered_index == 16) {
				HBRUSH mb = CreateSolidBrush(dark ? RGB(48, 54, 64) : RGB(235, 242, 252));
				FillRect(mem_dc, &mr, mb);
				DeleteObject(mb);
			}

			{
				int size = Scale(14);
				int sy = (mr.top + mr.bottom - size) / 2;
				RECT cs = { mr.left + Scale(4), sy, mr.left + Scale(4) + size, sy + size };
				HBRUSH cb = CreateSolidBrush(active_color);
				HBRUSH co = CreateSolidBrush(dark ? RGB(80, 85, 95) : RGB(190, 195, 205));
				FillRect(mem_dc, &cs, cb);
				FrameRect(mem_dc, &cs, co);
				DeleteObject(cb);
				DeleteObject(co);
			}

			SetBkMode(mem_dc, TRANSPARENT);
			SetTextColor(mem_dc, dark ? RGB(230, 235, 242) : RGB(30, 35, 42));
			tr.left += Scale(24);
			font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
			old_font = SelectObject(mem_dc, font);

			if (popup_hovered_index >= 0 && popup_hovered_index < STANDARD_COLOR_COUNT) {
				DrawText(mem_dc, palette_names[popup_hovered_index], -1, &tr,
					DT_LEFT | DT_VCENTER | DT_SINGLELINE);
			} else {
				DrawText(mem_dc, TEXT("More Colors..."), -1, &tr,
					DT_LEFT | DT_VCENTER | DT_SINGLELINE);
			}
			SelectObject(mem_dc, old_font);
		}

		BitBlt(dc, 0, 0, client.right, client.bottom, mem_dc, 0, 0, SRCCOPY);
		SelectObject(mem_dc, old_bmp);
		DeleteObject(mem_bmp);
		DeleteDC(mem_dc);
		EndPaint(hwnd, &ps);
		return 0;
	}
	}
	return DefWindowProc(hwnd, msg, wparam, lparam);
}

static BOOL color_popup_regist(HINSTANCE instance)
{
	WNDCLASSEX wc;
	ZeroMemory(&wc, sizeof(wc));
	wc.cbSize = sizeof(wc);
	wc.style = CS_DROPSHADOW;
	wc.lpfnWndProc = color_popup_proc;
	wc.hInstance = instance;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.lpszClassName = TEXT("CLCL_ColorPopup");
	return RegisterClassEx(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

static void show_color_popup(PINNED_IMAGE *pin)
{
	RECT btn_rect = {0};
	int swatch_size, swatch_gap, pad, grid_w, grid_h, more_h, sep_h, popup_w, popup_h;
	int x, y;
	HMONITOR hmon;
	MONITORINFO mi;
	DWORD now = GetTickCount();

	if (pin == NULL || pin->toolbar == NULL) return;
	if (now - last_color_popup_toggle < 150) return;
	last_color_popup_toggle = now;

	if (current_color_popup_hwnd != NULL && IsWindow(current_color_popup_hwnd)) {
		DestroyWindow(current_color_popup_hwnd);
		current_color_popup_hwnd = NULL;
		return;
	}

	SendMessage(pin->toolbar, TB_GETRECT, ID_COLOR_DROPDOWN, (LPARAM)&btn_rect);
	MapWindowPoints(pin->toolbar, NULL, (LPPOINT)&btn_rect, 2);

	swatch_size = Scale(20);
	swatch_gap = Scale(4);
	pad = Scale(8);
	grid_w = 8 * swatch_size + 7 * swatch_gap;
	grid_h = 2 * swatch_size + swatch_gap;
	sep_h = Scale(6);
	more_h = Scale(24);
	popup_w = grid_w + 2 * pad;
	popup_h = pad + grid_h + sep_h + more_h + pad;

	x = btn_rect.left;
	y = btn_rect.bottom + Scale(2);

	ZeroMemory(&mi, sizeof(mi));
	mi.cbSize = sizeof(mi);
	hmon = MonitorFromRect(&btn_rect, MONITOR_DEFAULTTONEAREST);
	if (GetMonitorInfo(hmon, &mi) || SystemParametersInfo(SPI_GETWORKAREA, 0, &mi.rcWork, 0)) {
		if (x + popup_w > mi.rcWork.right) x = mi.rcWork.right - popup_w;
		if (x < mi.rcWork.left) x = mi.rcWork.left;
		if (y + popup_h > mi.rcWork.bottom) y = btn_rect.top - popup_h - Scale(2);
		if (y < mi.rcWork.top) y = mi.rcWork.top;
	}

	current_color_popup_hwnd = CreateWindowEx(
		WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
		TEXT("CLCL_ColorPopup"),
		TEXT(""),
		WS_POPUP,
		x, y, popup_w, popup_h,
		pin->hwnd,
		NULL,
		pin_instance,
		pin);

	if (current_color_popup_hwnd != NULL) {
		ShowWindow(current_color_popup_hwnd, SW_SHOW);
		UpdateWindow(current_color_popup_hwnd);
		SetCapture(current_color_popup_hwnd);
	}
}

static void show_size_popup(PINNED_IMAGE *pin)
{
	RECT button;
	UINT command;
	if (pin == NULL || pin->toolbar == NULL ||
		!SendMessage(pin->toolbar, TB_GETRECT, ID_SIZE_DROPDOWN, (LPARAM)&button)) return;
	MapWindowPoints(pin->toolbar, NULL, (LPPOINT)&button, 2);
	command = TrackPopupMenuEx(GetSubMenu(pin->menu, 4),
		TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY,
		button.left, button.bottom, pin->hwnd, NULL);
	if (command != 0) SendMessage(pin->hwnd, WM_COMMAND, MAKEWPARAM(command, 0), 0);
}

static BOOL create_editor_toolbar(PINNED_IMAGE *pin)
{
	TBBUTTON buttons[19];
	HICON icon;
	RECT bounds;
	int i, icon_index = 0, size = Scale(TOOL_ICON_SIZE);
	pin->icons = ImageList_Create(size, size, ILC_COLOR32 | ILC_MASK, 15, 0);
	if (pin->icons == NULL) return FALSE;
	for (i = 0; i < 13; i++) {
		icon = (HICON)LoadImage(pin_instance, MAKEINTRESOURCE(toolbar_icons[i]), IMAGE_ICON,
			size, size, LR_DEFAULTCOLOR);
		if (icon == NULL || ImageList_AddIcon(pin->icons, icon) == -1) {
			if (icon != NULL) DestroyIcon(icon);
			ImageList_Destroy(pin->icons);
			pin->icons = NULL;
			return FALSE;
		}
		DestroyIcon(icon);
	}
	icon = create_color_swatch_icon(get_pin_color(pin), size);
	if (icon != NULL) {
		ImageList_AddIcon(pin->icons, icon);
		DestroyIcon(icon);
	}
	icon = (HICON)LoadImage(pin_instance, MAKEINTRESOURCE(IDR_PIN_SIZE), IMAGE_ICON,
		size, size, LR_DEFAULTCOLOR);
	if (icon != NULL) {
		ImageList_AddIcon(pin->icons, icon);
		DestroyIcon(icon);
	}
	pin->toolbar = CreateWindowEx(0, TOOLBARCLASSNAME, NULL,
		WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_TOP | CCS_NODIVIDER,
		0, 0, 0, 0, pin->hwnd, (HMENU)ID_PIN_TOOLBAR, pin_instance, NULL);
	if (pin->toolbar == NULL) {
		ImageList_Destroy(pin->icons);
		pin->icons = NULL;
		return FALSE;
	}
	SetWindowSubclass(pin->toolbar, editor_toolbar_proc, 1, (DWORD_PTR)pin->hwnd);
	SendMessage(pin->toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
	SendMessage(pin->toolbar, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_DOUBLEBUFFER | TBSTYLE_EX_DRAWDDARROWS);
	SendMessage(pin->toolbar, TB_SETIMAGELIST, 0, (LPARAM)pin->icons);
	SendMessage(pin->toolbar, TB_SETBITMAPSIZE, 0, MAKELPARAM(size, size));
	SendMessage(pin->toolbar, TB_SETBUTTONSIZE, 0, MAKELPARAM(Scale(32), Scale(32)));
	SendMessage(pin->toolbar, TB_SETINDENT, Scale(5), 0);
	ZeroMemory(buttons, sizeof(buttons));
	for (i = 0; i < 19; i++) {
		if (i == 2 || i == 6 || i == 13 || i == 16) {
			buttons[i].fsStyle = TBSTYLE_SEP;
			buttons[i].iBitmap = Scale(8);
		} else if (i == 18) {
			buttons[i].iBitmap = 14;
			buttons[i].idCommand = ID_SIZE_DROPDOWN;
			buttons[i].fsState = TBSTATE_ENABLED;
			buttons[i].fsStyle = TBSTYLE_BUTTON | BTNS_WHOLEDROPDOWN;
		} else if (i == 17) {
			buttons[i].iBitmap = 13;
			buttons[i].idCommand = ID_COLOR_DROPDOWN;
			buttons[i].fsState = TBSTATE_ENABLED;
			buttons[i].fsStyle = TBSTYLE_BUTTON | BTNS_WHOLEDROPDOWN;
		} else {
			buttons[i].iBitmap = icon_index;
			buttons[i].idCommand = toolbar_commands[icon_index];
			buttons[i].fsState = icon_index < 2 ? 0 : TBSTATE_ENABLED;
			buttons[i].fsStyle = (icon_index < 2 || icon_index == 12) ? TBSTYLE_BUTTON : TBSTYLE_CHECK;
			icon_index++;
		}
	}
	SendMessage(pin->toolbar, TB_ADDBUTTONS, 19, (LPARAM)buttons);
	SendMessage(pin->toolbar, TB_CHECKBUTTON, pin->tool, MAKELONG(TRUE, 0));
	refresh_history_buttons(pin);
	SendMessage(pin->toolbar, TB_AUTOSIZE, 0, 0);
	GetWindowRect(pin->toolbar, &bounds);
	pin->toolbar_height = bounds.bottom - bounds.top;
	dark_mode_set_control(pin->toolbar);
	return TRUE;
}

static LRESULT CALLBACK editor_toolbar_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam,
	UINT_PTR id, DWORD_PTR ref)
{
	if (msg == WM_KEYDOWN && wparam == VK_ESCAPE) {
		SendMessage((HWND)ref, WM_CLOSE, 0, 0);
		return 0;
	}
	if (msg == WM_NCDESTROY) RemoveWindowSubclass(hwnd, editor_toolbar_proc, id);
	return DefSubclassProc(hwnd, msg, wparam, lparam);
}

BOOL pinned_image_regist(const HINSTANCE instance)
{
	WNDCLASSEX wc;
	pin_instance = instance;
	ZeroMemory(&wc, sizeof(wc));
	wc.cbSize = sizeof(wc);
	wc.style = 0;
	wc.lpfnWndProc = pinned_image_proc;
	wc.hInstance = instance;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hIcon = LoadIcon(instance, MAKEINTRESOURCE(IDI_ICON_MAIN));
	wc.hbrBackground = NULL;
	wc.lpszClassName = PIN_CLASS;
	if (RegisterClassEx(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return FALSE;
	return color_popup_regist(instance);
}

static HWND open_image_editor(const HWND owner, DATA_INFO *source, int initial_tool)
{
	PINNED_IMAGE *pin;
	DATA_INFO *data = find_bitmap_data(source);
	HBITMAP bitmap;
	int width, height, window_w, window_h, x, y;
	POINT primary_point = {0, 0};
	RECT work;
	MONITORINFO monitor = { sizeof(monitor) };
	if (data == NULL) return NULL;
	if (source->type == TYPE_ITEM && db_history_is_open()) db_history_ensure_item_data(source);
	bitmap = bitmap_from_data(data, &width, &height);
	if (bitmap == NULL) return NULL;
	pin = mem_calloc(sizeof(*pin));
	if (pin == NULL) { DeleteObject(bitmap); return NULL; }
	{
		DATA_INFO *parent = data_check(&history_data, source);
		if (parent == NULL) parent = data_check(&regist_data, source);
		if (parent != NULL) {
			pin->source_item = source->type == TYPE_DATA && parent->type == TYPE_ITEM ? parent : source;
			pin->source_data = data;
		}
	}
	pin->owner = owner;
	pin->bitmap = bitmap;
	pin->original = clone_bitmap(bitmap, NULL, NULL);
	pin->width = width;
	pin->height = height;
	pin->tool = initial_tool;
	pin->stroke_width = (option.pinned_stroke_width == 6 || option.pinned_stroke_width == 12) ? option.pinned_stroke_width : 3;
	pin->current_color = (option.pinned_color != 0 || option.pinned_tool != 0) ? option.pinned_color : PIN_DEFAULT_COLOR;
	if (pin->current_color == 0 && option.pinned_tool == 0) pin->current_color = PIN_DEFAULT_COLOR;
	pin->menu = create_editor_menu(pin->source_data != NULL);
	if (pin->menu == NULL) {
		DeleteObject(pin->original);
		DeleteObject(pin->bitmap);
		mem_free(&pin);
		return NULL;
	}
	SetDpiFromPoint(primary_point);
	if (GetMonitorInfo(MonitorFromPoint(primary_point, MONITOR_DEFAULTTOPRIMARY), &monitor))
		work = monitor.rcWork;
	else SystemParametersInfo(SPI_GETWORKAREA, 0, &work, 0);
	window_w = min(max(width + Scale(20), Scale(410)), (work.right - work.left) * 3 / 4);
	window_h = min(max(MulDiv(window_w - Scale(20), height, width) + Scale(105), Scale(260)),
		(work.bottom - work.top) * 3 / 4);
	x = work.left + (work.right - work.left - window_w) / 2;
	y = work.top + (work.bottom - work.top - window_h) / 2;
	pin->min_window_width = min(Scale(410), window_w);
	pin->min_window_height = min(Scale(260), window_h);
	pin->hwnd = CreateWindowEx(WS_EX_TOPMOST | WS_EX_APPWINDOW, PIN_CLASS, TEXT("CLCL image"),
		WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, window_w, window_h,
		NULL, pin->menu, pin_instance, pin);
	if (pin->hwnd == NULL) {
		DestroyMenu(pin->menu);
		DeleteObject(pin->original);
		DeleteObject(pin->bitmap);
		mem_free(&pin);
		return NULL;
	}
	SetWindowPos(pin->hwnd, NULL, x, y, window_w, window_h, SWP_NOZORDER);
	SendMessage(pin->hwnd, WM_SIZE, SIZE_RESTORED, 0);
	ShowWindow(pin->hwnd, SW_SHOWMAXIMIZED);
	pin->next = pin_windows;
	pin_windows = pin;
	return pin->hwnd;
}

HWND pinned_image_open(const HWND owner, DATA_INFO *source)
{
	return open_image_editor(owner, source, ID_CROP);
}

HWND pinned_image_open_from_menu(const HWND owner, DATA_INFO *source)
{
	return open_image_editor(owner, source, ID_RECT);
}

BOOL pinned_image_clipboard_is_screenshot(HWND owner)
{
	DWORD process = 0, sequence = GetClipboardSequenceNumber();
	BOOL match = FALSE;
	HANDLE data;
	BITMAP bitmap;
	GetWindowThreadProcessId(GetClipboardOwner(), &process);
	if (process == GetCurrentProcessId() || sequence == 0 ||
		sequence == last_auto_sequence || sequence == last_copy_sequence || !OpenClipboard(owner)) return FALSE;
	data = GetClipboardData(CF_BITMAP);
	if (data != NULL && GetObject(data, sizeof(bitmap), &bitmap))
		match = is_desktop_image(bitmap.bmWidth, bitmap.bmHeight);
	else {
		data = GetClipboardData(CF_DIB);
		if (data != NULL && GlobalSize(data) >= sizeof(BITMAPINFOHEADER)) {
			BITMAPINFOHEADER *header = GlobalLock(data);
			if (header != NULL) {
				if (header->biSize >= sizeof(*header) && header->biHeight != LONG_MIN)
					match = is_desktop_image(header->biWidth, abs(header->biHeight));
				GlobalUnlock(data);
			}
		}
	}
	CloseClipboard();
	return match;
}

void pinned_image_auto_open(HWND owner, DATA_INFO *source, DWORD sequence)
{
	DATA_INFO *data;
	HBITMAP bitmap, cropped;
	int width, height;
	POINT primary = {0, 0};
	MONITORINFO monitor = { sizeof(monitor) };
	RECT desktop, crop;
	DATA_INFO image = {0};
	DATA_INFO *history_image = NULL;
	TCHAR error[BUF_SIZE];
	if (sequence == 0 || sequence == last_auto_sequence || sequence == last_copy_sequence) return;
	last_auto_sequence = sequence;
	data = find_bitmap_data(source);
	if (data == NULL) return;
	bitmap = bitmap_from_data(data, &width, &height);
	if (bitmap == NULL) return;
	if (!is_desktop_image(width, height) ||
		!GetMonitorInfo(MonitorFromPoint(primary, MONITOR_DEFAULTTOPRIMARY), &monitor)) {
		DeleteObject(bitmap);
		return;
	}
	desktop = desktop_bounds();
	crop = monitor.rcMonitor;
	OffsetRect(&crop, -desktop.left, -desktop.top);
	cropped = crop_bitmap(bitmap, crop, &width, &height);
	DeleteObject(bitmap);
	if (cropped == NULL) return;
	// Prepare the replacement before touching either clipboard or history.
	if (source->type == TYPE_ITEM) {
		HBITMAP history_bitmap = clone_bitmap(cropped, NULL, NULL);
		if (history_bitmap != NULL)
			history_image = data_create_data(CF_BITMAP, TEXT("BITMAP"), history_bitmap, 0, FALSE, error);
		if (history_image == NULL) {
			if (history_bitmap != NULL) DeleteObject(history_bitmap);
			DeleteObject(cropped);
			return;
		}
	}
	// Publish first. The sequence guard prevents our clipboard notification
	// from reopening another editor (including on single-monitor desktops).
	if (copy_bitmap_to_clipboard(owner, cropped)) {
		if (history_image != NULL) {
			data_free(source->child);
			source->child = history_image;
			history_image = NULL;
			data_menu_free_item(source);
			source->content_hash = 0;
		}
		image.type = TYPE_DATA;
		image.format = CF_BITMAP;
		image.format_name = TEXT("BITMAP");
		image.data = cropped;
		pinned_image_open(owner, &image);
	} else MessageBeep(MB_ICONWARNING);
	if (history_image != NULL) data_free(history_image);
	DeleteObject(cropped);
}

typedef struct _SNIP_STATE {
	HWND owner;
	HBITMAP screen;
	HBITMAP dimmed;
	RECT desktop;
	POINT start;
	POINT end;
	BOOL dragging;
} SNIP_STATE;

static RECT snip_selection(const SNIP_STATE *snip)
{
	RECT rect;
	rect.left = min(snip->start.x, snip->end.x);
	rect.top = min(snip->start.y, snip->end.y);
	rect.right = max(snip->start.x, snip->end.x);
	rect.bottom = max(snip->start.y, snip->end.y);
	return rect;
}

static LRESULT CALLBACK snip_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	SNIP_STATE *snip = (SNIP_STATE *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
	switch (msg) {
	case WM_NCCREATE:
		SetWindowLongPtr(hwnd, GWLP_USERDATA,
			(LONG_PTR)((CREATESTRUCT *)lparam)->lpCreateParams);
		return TRUE;
	case WM_ERASEBKGND:
		return 1;
	case WM_SETCURSOR:
		SetCursor(LoadCursor(NULL, IDC_CROSS));
		return TRUE;
	case WM_KEYDOWN:
		if (wparam == VK_ESCAPE) { DestroyWindow(hwnd); return 0; }
		break;
	case WM_RBUTTONDOWN:
	case WM_CLOSE:
		DestroyWindow(hwnd);
		return 0;
	case WM_LBUTTONDOWN:
		if (snip == NULL) break;
		snip->start.x = snip->end.x = GET_X_LPARAM(lparam);
		snip->start.y = snip->end.y = GET_Y_LPARAM(lparam);
		snip->dragging = TRUE;
		SetCapture(hwnd);
		return 0;
	case WM_MOUSEMOVE:
		if (snip != NULL && snip->dragging) {
			RECT old = snip_selection(snip), current;
			InflateRect(&old, 3, 3);
			InvalidateRect(hwnd, &old, FALSE);
			snip->end.x = GET_X_LPARAM(lparam);
			snip->end.y = GET_Y_LPARAM(lparam);
			current = snip_selection(snip);
			InflateRect(&current, 3, 3);
			InvalidateRect(hwnd, &current, FALSE);
		}
		return 0;
	case WM_LBUTTONUP:
		if (snip != NULL && snip->dragging) {
			RECT selection, bounds;
			HBITMAP cropped = NULL;
			int width, height;
			DATA_INFO image = {0};
			HWND owner = snip->owner;
			snip->end.x = GET_X_LPARAM(lparam);
			snip->end.y = GET_Y_LPARAM(lparam);
			selection = snip_selection(snip);
			SetRect(&bounds, 0, 0, snip->desktop.right - snip->desktop.left,
				snip->desktop.bottom - snip->desktop.top);
			IntersectRect(&selection, &selection, &bounds);
			if (selection.right > selection.left && selection.bottom > selection.top)
				cropped = crop_bitmap(snip->screen, selection, &width, &height);
			snip->dragging = FALSE;
			ReleaseCapture();
			DestroyWindow(hwnd);
			if (cropped != NULL) {
				if (copy_bitmap_to_clipboard(owner, cropped)) {
					HWND editor;
					image.type = TYPE_DATA;
					image.format = CF_BITMAP;
					image.format_name = TEXT("BITMAP");
					image.data = cropped;
					editor = pinned_image_open(owner, &image);
					if (editor != NULL) SendMessage(editor, WM_COMMAND, ID_ARROW, 0);
				} else MessageBeep(MB_ICONWARNING);
				DeleteObject(cropped);
			}
			return 0;
		}
		break;
	case WM_PAINT:
		if (snip != NULL) {
			PAINTSTRUCT ps;
			HDC dc = BeginPaint(hwnd, &ps);
			HDC source = CreateCompatibleDC(dc);
			if (source != NULL) {
				RECT selection, visible;
				HBITMAP old = SelectObject(source, snip->dimmed != NULL ? snip->dimmed : snip->screen);
				BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top,
					ps.rcPaint.right - ps.rcPaint.left, ps.rcPaint.bottom - ps.rcPaint.top,
					source, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
				if (snip->dragging) {
					selection = snip_selection(snip);
					if (IntersectRect(&visible, &selection, &ps.rcPaint)) {
						SelectObject(source, snip->screen);
						BitBlt(dc, visible.left, visible.top, visible.right - visible.left,
							visible.bottom - visible.top, source, visible.left, visible.top, SRCCOPY);
						SelectObject(source, snip->dimmed != NULL ? snip->dimmed : snip->screen);
						{
							HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
							HPEN prior = SelectObject(dc, pen);
							HBRUSH brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
							Rectangle(dc, selection.left, selection.top, selection.right, selection.bottom);
							SelectObject(dc, brush);
							SelectObject(dc, prior);
							DeleteObject(pen);
						}
					}
				}
				SelectObject(source, old);
				DeleteDC(source);
			}
			EndPaint(hwnd, &ps);
			return 0;
		}
		break;
	case WM_NCDESTROY:
		if (snip != NULL) {
			if (snip->screen != NULL) DeleteObject(snip->screen);
			if (snip->dimmed != NULL) DeleteObject(snip->dimmed);
			mem_free(&snip);
		}
		break;
	}
	return DefWindowProc(hwnd, msg, wparam, lparam);
}

BOOL pinned_image_start_snip(HWND owner)
{
	static BOOL registered;
	WNDCLASSEX wc = {0};
	SNIP_STATE *snip;
	HDC screen, source, dark;
	HBITMAP black, old_source, old_dark;
	HWND hwnd;
	int width, height;
	if (!registered) {
		wc.cbSize = sizeof(wc);
		wc.lpfnWndProc = snip_proc;
		wc.hInstance = pin_instance;
		wc.hCursor = LoadCursor(NULL, IDC_CROSS);
		wc.lpszClassName = SNIP_CLASS;
		if (!RegisterClassEx(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return FALSE;
		registered = TRUE;
	}
	snip = mem_calloc(sizeof(*snip));
	if (snip == NULL) return FALSE;
	snip->owner = owner;
	snip->desktop = desktop_bounds();
	width = snip->desktop.right - snip->desktop.left;
	height = snip->desktop.bottom - snip->desktop.top;
	screen = GetDC(NULL);
	if (screen == NULL || width <= 0 || height <= 0) {
		if (screen != NULL) ReleaseDC(NULL, screen);
		mem_free(&snip);
		return FALSE;
	}
	source = CreateCompatibleDC(screen);
	dark = CreateCompatibleDC(screen);
	snip->screen = CreateCompatibleBitmap(screen, width, height);
	snip->dimmed = CreateCompatibleBitmap(screen, width, height);
	if (source == NULL || dark == NULL || snip->screen == NULL || snip->dimmed == NULL) {
		if (source != NULL) DeleteDC(source);
		if (dark != NULL) DeleteDC(dark);
		ReleaseDC(NULL, screen);
		if (snip->screen != NULL) DeleteObject(snip->screen);
		if (snip->dimmed != NULL) DeleteObject(snip->dimmed);
		mem_free(&snip);
		return FALSE;
	}
	old_source = SelectObject(source, snip->screen);
	old_dark = SelectObject(dark, snip->dimmed);
	BitBlt(source, 0, 0, width, height, screen, snip->desktop.left,
		snip->desktop.top, SRCCOPY | CAPTUREBLT);
	BitBlt(dark, 0, 0, width, height, source, 0, 0, SRCCOPY);
	black = CreateCompatibleBitmap(screen, 1, 1);
	if (black != NULL) {
		HDC shade = CreateCompatibleDC(screen);
		if (shade != NULL) {
			HBITMAP old = SelectObject(shade, black);
			BLENDFUNCTION blend = { AC_SRC_OVER, 0, 100, 0 };
			PatBlt(shade, 0, 0, 1, 1, BLACKNESS);
			GdiAlphaBlend(dark, 0, 0, width, height, shade, 0, 0, 1, 1, blend);
			SelectObject(shade, old);
			DeleteDC(shade);
		}
		DeleteObject(black);
	}
	SelectObject(source, old_source);
	SelectObject(dark, old_dark);
	DeleteDC(source);
	DeleteDC(dark);
	ReleaseDC(NULL, screen);
	hwnd = CreateWindowEx(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, SNIP_CLASS, TEXT("New snip"),
		WS_POPUP, snip->desktop.left, snip->desktop.top, width, height,
		owner, NULL, pin_instance, snip);
	if (hwnd == NULL) {
		DeleteObject(snip->screen);
		DeleteObject(snip->dimmed);
		mem_free(&snip);
		return FALSE;
	}
	ShowWindow(hwnd, SW_SHOW);
	SetForegroundWindow(hwnd);
	SetFocus(hwnd);
	return TRUE;
}

void pinned_image_close_all(void)
{
	while (pin_windows != NULL) DestroyWindow(pin_windows->hwnd);
}

static LRESULT CALLBACK pinned_image_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	PINNED_IMAGE *pin = (PINNED_IMAGE *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
	switch (msg) {
	case WM_CREATE:
		pin = (PINNED_IMAGE *)((CREATESTRUCT *)lparam)->lpCreateParams;
		SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pin);
		pin->hwnd = hwnd;
		SetDpiFromWindow(hwnd);
		dark_mode_set_inverse_window(hwnd);
		create_editor_toolbar(pin);
		theme_editor_popups(pin->menu);
		CheckMenuRadioItem(GetSubMenu(pin->menu, 2), ID_PEN, ID_CROP, pin->tool, MF_BYCOMMAND);
		sync_color_menu(pin);
		CheckMenuRadioItem(GetSubMenu(pin->menu, 4), ID_WIDTH_3, ID_WIDTH_12,
			pin->stroke_width == 6 ? ID_WIDTH_6 : pin->stroke_width == 12 ? ID_WIDTH_12 : ID_WIDTH_3, MF_BYCOMMAND);
		return 0;
	case WM_SETCURSOR:
		if (pin != NULL && (HWND)wparam == hwnd && LOWORD(lparam) == HTCLIENT) {
			POINT point;
			GetCursorPos(&point);
			ScreenToClient(hwnd, &point);
			SetCursor(PtInRect(&pin->image_rect, point) ? editor_tool_cursor(pin) : LoadCursor(NULL, IDC_ARROW));
			return TRUE;
		}
		break;
	case WM_MEASUREITEM:
		if (((MEASUREITEMSTRUCT *)lparam)->CtlType == ODT_MENU) {
			MEASUREITEMSTRUCT *item = (MEASUREITEMSTRUCT *)lparam;
			TCHAR text[128];
			SIZE size;
			HDC dc = GetDC(hwnd);
			HFONT font = SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
			GetMenuString((HMENU)item->itemData, item->itemID, text, 128, MF_BYCOMMAND);
			GetTextExtentPoint32(dc, text, lstrlen(text), &size);
			item->itemWidth = size.cx + Scale(64);
			item->itemHeight = max(size.cy + Scale(10), Scale(26));
			SelectObject(dc, font); ReleaseDC(hwnd, dc);
			return TRUE;
		}
		break;
	case WM_DRAWITEM:
		if (((DRAWITEMSTRUCT *)lparam)->CtlType == ODT_MENU) {
			DRAWITEMSTRUCT *item = (DRAWITEMSTRUCT *)lparam;
			BOOL dark = !dark_mode_is_dark();
			BOOL selected = (item->itemState & ODS_SELECTED) != 0;
			TCHAR text[128], *shortcut;
			RECT rect = item->rcItem, label = rect;
			HFONT font = SelectObject(item->hDC, GetStockObject(DEFAULT_GUI_FONT));
			SetDCBrushColor(item->hDC, selected ? RGB(0, 120, 215) : dark ? RGB(43, 43, 43) : RGB(245, 245, 245));
			FillRect(item->hDC, &rect, (HBRUSH)GetStockObject(DC_BRUSH));
			SetBkMode(item->hDC, TRANSPARENT);
			SetTextColor(item->hDC, item->itemState & ODS_DISABLED ? RGB(140, 140, 140) :
				selected || dark ? RGB(240, 240, 240) : RGB(25, 25, 25));
			GetMenuString((HMENU)item->itemData, item->itemID, text, 128, MF_BYCOMMAND);
			if (item->itemState & ODS_CHECKED) DrawText(item->hDC, TEXT("\x2713"), -1, &label, DT_SINGLELINE | DT_VCENTER);
			label.left += Scale(24); label.right -= Scale(12);
			shortcut = _tcschr(text, TEXT('\t'));
			if (shortcut != NULL) {
				*shortcut++ = 0;
				DrawText(item->hDC, shortcut, -1, &label, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
			}
			DrawText(item->hDC, text, -1, &label, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
			SelectObject(item->hDC, font);
			return TRUE;
		}
		break;
	case WM_SIZE:
		if (pin != NULL) {
			RECT client;
			RECT toolbar_rect;
			if (pin->toolbar != NULL) {
				SendMessage(pin->toolbar, WM_SIZE, wparam, lparam);
				GetWindowRect(pin->toolbar, &toolbar_rect);
				pin->toolbar_height = toolbar_rect.bottom - toolbar_rect.top;

			}
			GetClientRect(hwnd, &client);
			calculate_image_rect(pin, &client);
			InvalidateRect(hwnd, NULL, FALSE);
		}
		return 0;
	case WM_ERASEBKGND:
		return 1;
	case WM_UAHDRAWMENU:
	case WM_UAHDRAWMENUITEM:
	case WM_NCPAINT:
	case WM_NCACTIVATE:
		{
			LRESULT result;
			if (dark_mode_menubar_message(hwnd, msg, wparam, lparam, &result)) return result;
		}
		return DefWindowProc(hwnd, msg, wparam, lparam);
	case WM_SETTINGCHANGE:
	case WM_THEMECHANGED:
		if (dark_mode_is_color_change(msg, lparam)) {
			dark_mode_update();
			dark_mode_set_inverse_window(hwnd);
			InvalidateRect(hwnd, NULL, FALSE);
		}
		return DefWindowProc(hwnd, msg, wparam, lparam);
	case WM_DPICHANGED:
		if (pin != NULL) {
			RECT *bounds = (RECT *)lparam;
			SetWindowPos(hwnd, NULL, bounds->left, bounds->top,
				bounds->right - bounds->left, bounds->bottom - bounds->top,
				SWP_NOZORDER | SWP_NOACTIVATE);
			if (pin->toolbar != NULL) DestroyWindow(pin->toolbar);
			pin->toolbar = NULL;
			if (pin->icons != NULL) ImageList_Destroy(pin->icons);
			pin->icons = NULL;
			SetDpiFromWindow(hwnd);
			create_editor_toolbar(pin);
			SendMessage(hwnd, WM_SIZE, 0, 0);
		}
		return 0;
	case WM_GETMINMAXINFO:

		((MINMAXINFO *)lparam)->ptMinTrackSize.x = pin != NULL ? pin->min_window_width : Scale(410);
		((MINMAXINFO *)lparam)->ptMinTrackSize.y = pin != NULL ? pin->min_window_height : Scale(260);
		return 0;
	case WM_SIZING:
		if (pin != NULL) {
			RECT *r = (RECT *)lparam, client = {0, 0, pin->width, pin->height};
			DWORD style = (DWORD)GetWindowLongPtr(hwnd, GWL_STYLE);
			DWORD exstyle = (DWORD)GetWindowLongPtr(hwnd, GWL_EXSTYLE);
			HMONITOR hmon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
			MONITORINFO mi;
			RECT work;
			int chrome_w, chrome_h, max_w, max_h;

			AdjustWindowRectEx(&client, style, TRUE, exstyle);
			chrome_w = (client.right - client.left) - pin->width + Scale(20);
			chrome_h = (client.bottom - client.top) - pin->height + pin->toolbar_height + Scale(20);

			ZeroMemory(&mi, sizeof(mi));
			mi.cbSize = sizeof(mi);
			if (hmon == NULL) hmon = MonitorFromRect(r, MONITOR_DEFAULTTONEAREST);
			if (!GetMonitorInfo(hmon, &mi)) {
				SystemParametersInfo(SPI_GETWORKAREA, 0, &mi.rcWork, 0);
			}
			work = mi.rcWork;
			max_w = work.right - work.left;
			max_h = work.bottom - work.top;

			if (wparam == WMSZ_TOP || wparam == WMSZ_BOTTOM) {
				int wanted_w = MulDiv((r->bottom - r->top) - chrome_h, pin->width, pin->height) + chrome_w;
				r->right = r->left + wanted_w;
			} else {
				int wanted_h = MulDiv((r->right - r->left) - chrome_w, pin->height, pin->width) + chrome_h;
				if (wparam == WMSZ_TOPLEFT || wparam == WMSZ_TOPRIGHT) r->top = r->bottom - wanted_h;
				else r->bottom = r->top + wanted_h;
			}

			if (r->right - r->left > max_w || r->bottom - r->top > max_h) {
				int fit_w = max_w, fit_h = max_h;
				int avail_w = max_w - chrome_w;
				int avail_h = max_h - chrome_h;
				if (avail_w > 0 && avail_h > 0 && pin->width > 0 && pin->height > 0) {
					if (MulDiv(avail_w, pin->height, pin->width) + chrome_h <= max_h) {
						fit_w = avail_w + chrome_w;
						fit_h = MulDiv(avail_w, pin->height, pin->width) + chrome_h;
					} else {
						fit_h = avail_h + chrome_h;
						fit_w = MulDiv(avail_h, pin->width, pin->height) + chrome_w;
					}
				}
				if (wparam == WMSZ_LEFT || wparam == WMSZ_TOPLEFT || wparam == WMSZ_BOTTOMLEFT)
					r->left = r->right - fit_w;
				else
					r->right = r->left + fit_w;

				if (wparam == WMSZ_TOP || wparam == WMSZ_TOPLEFT || wparam == WMSZ_TOPRIGHT)
					r->top = r->bottom - fit_h;
				else
					r->bottom = r->top + fit_h;
			}

			if (r->right > work.right) {
				int shift = r->right - work.right;
				r->left -= shift;
				r->right -= shift;
			}
			if (r->left < work.left) {
				int shift = work.left - r->left;
				r->left += shift;
				r->right += shift;
			}
			if (r->bottom > work.bottom) {
				int shift = r->bottom - work.bottom;
				r->top -= shift;
				r->bottom -= shift;
			}
			if (r->top < work.top) {
				int shift = work.top - r->top;
				r->top += shift;
				r->bottom += shift;
			}

			if (r->left < work.left) r->left = work.left;
			if (r->top < work.top) r->top = work.top;
			if (r->right > work.right) r->right = work.right;
			if (r->bottom > work.bottom) r->bottom = work.bottom;

			return TRUE;
		}
		break;
	case WM_WINDOWPOSCHANGING:
		if (pin != NULL) {
			WINDOWPOS *wp = (WINDOWPOS *)lparam;
			if (wp != NULL && !(wp->flags & SWP_NOSIZE) && !IsIconic(hwnd) && !IsZoomed(hwnd)) {
				RECT target = { wp->x, wp->y, wp->x + wp->cx, wp->y + wp->cy };
				HMONITOR hmon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
				MONITORINFO mi;
				ZeroMemory(&mi, sizeof(mi));
				mi.cbSize = sizeof(mi);
				if (hmon == NULL) hmon = MonitorFromRect(&target, MONITOR_DEFAULTTONEAREST);
				if (GetMonitorInfo(hmon, &mi) || SystemParametersInfo(SPI_GETWORKAREA, 0, &mi.rcWork, 0)) {
					RECT work = mi.rcWork;
					int max_w = work.right - work.left;
					int max_h = work.bottom - work.top;
					int min_w = pin->min_window_width > 0 ? pin->min_window_width : Scale(410);
					int min_h = pin->min_window_height > 0 ? pin->min_window_height : Scale(260);
					if (wp->cx < min_w) wp->cx = min_w;
					if (wp->cx > max_w) wp->cx = max_w;
					if (wp->cy < min_h) wp->cy = min_h;
					if (wp->cy > max_h) wp->cy = max_h;
					if (wp->x + wp->cx > work.right) wp->x = work.right - wp->cx;
					if (wp->x < work.left) wp->x = work.left;
					if (wp->y + wp->cy > work.bottom) wp->y = work.bottom - wp->cy;
					if (wp->y < work.top) wp->y = work.top;
				}
			}
		}
		break;
	case WM_EXITSIZEMOVE:
		if (pin != NULL) {
			clamp_pinned_window_to_work_area(pin);
		}
		return 0;
	case WM_INITMENUPOPUP:
		if (pin != NULL && (HMENU)wparam == GetSubMenu(pin->menu, 1)) {
			EnableMenuItem((HMENU)wparam, ID_UNDO,
				MF_BYCOMMAND | (pin->undo_count != 0 ? MF_ENABLED : MF_GRAYED));
			EnableMenuItem((HMENU)wparam, ID_REDO,
				MF_BYCOMMAND | (pin->redo_count != 0 ? MF_ENABLED : MF_GRAYED));
		}
		break;
	case WM_NOTIFY:
		if (pin != NULL) {
			NMHDR *hdr = (NMHDR *)lparam;
			if ((int)hdr->idFrom == ID_PIN_TOOLBAR) {
				if (hdr->code == NM_CUSTOMDRAW) {
					LRESULT result;
					if (dark_mode_toolbar_customdraw(lparam, &result)) return result;
				} else if (hdr->code == TBN_DROPDOWN) {
					NMTOOLBAR *tb = (NMTOOLBAR *)lparam;
					if (tb->iItem == ID_COLOR_DROPDOWN) {
						show_color_popup(pin);
						return TBDDRET_DEFAULT;
					}
					if (tb->iItem == ID_SIZE_DROPDOWN) {
						show_size_popup(pin);
						return TBDDRET_DEFAULT;
					}
				}
			}
			if (hdr->code == TTN_NEEDTEXT) {
				if (hdr->idFrom >= ID_PEN && hdr->idFrom <= ID_CROP) {
					((NMTTDISPINFO *)lparam)->lpszText = (TCHAR *)tool_tips[hdr->idFrom - ID_PEN];
					return 0;
				} else if (hdr->idFrom == ID_FILLED_RECT || hdr->idFrom == ID_FILLED_ELLIPSE) {
					((NMTTDISPINFO *)lparam)->lpszText = hdr->idFrom == ID_FILLED_RECT ?
						TEXT("Filled rectangle") : TEXT("Filled ellipse");
					return 0;
				} else if (hdr->idFrom == ID_UNDO || hdr->idFrom == ID_REDO) {
					((NMTTDISPINFO *)lparam)->lpszText =
						hdr->idFrom == ID_UNDO ? TEXT("Undo (Ctrl+Z)") : TEXT("Redo (Ctrl+Y)");
					return 0;
				} else if (hdr->idFrom == ID_COPY) {
					((NMTTDISPINFO *)lparam)->lpszText = TEXT("Copy and close (Ctrl+C)");
					return 0;
				} else if (hdr->idFrom == ID_COLOR_DROPDOWN) {
					((NMTTDISPINFO *)lparam)->lpszText = TEXT("Color");
					return 0;
				} else if (hdr->idFrom == ID_SIZE_DROPDOWN) {
					((NMTTDISPINFO *)lparam)->lpszText = TEXT("Stroke size");
					return 0;
				}
			}
		}
		break;
	case WM_COMMAND:
		if (pin == NULL) break;
		switch (LOWORD(wparam)) {
		case ID_PEN: case ID_MARKER: case ID_ERASER: case ID_LINE: case ID_ARROW:
		case ID_RECT: case ID_ELLIPSE: case ID_CROP: case ID_FILLED_RECT: case ID_FILLED_ELLIPSE:
			pin->tool = LOWORD(wparam);
			{
				int i;
				for (i = 2; i < 12; i++) {
					UINT id = toolbar_commands[i];
					CheckMenuItem(GetSubMenu(pin->menu, 2), id,
						MF_BYCOMMAND | (id == pin->tool ? MF_CHECKED : MF_UNCHECKED));
					if (pin->toolbar != NULL)
						SendMessage(pin->toolbar, TB_CHECKBUTTON, id, MAKELONG(id == pin->tool, 0));
				}
			}
			save_pinned_preferences(pin);
			break;
		case ID_UNDO: swap_history(pin, TRUE); break;
		case ID_REDO: swap_history(pin, FALSE); break;
		case ID_COPY:
			if (copy_bitmap_to_clipboard(pin->owner, pin->bitmap)) DestroyWindow(hwnd);
			else MessageBeep(MB_ICONWARNING);
			break;
		case ID_UPDATE:
			if (!replace_source(pin)) MessageBox(hwnd, TEXT("The original image is no longer available."), TEXT("CLCL"), MB_OK | MB_ICONWARNING);
			break;
		case ID_COLOR_RED: case ID_COLOR_BLUE: case ID_COLOR_GREEN: case ID_COLOR_BLACK:
			pin->color_index = LOWORD(wparam) - ID_COLOR_RED;
			pin->current_color = colors[pin->color_index];
			sync_color_menu(pin);
			update_color_button_icon(pin);
			save_pinned_preferences(pin);
			break;
		case ID_COLOR_DROPDOWN:
			show_color_popup(pin);
			break;
		case ID_SIZE_DROPDOWN:
			show_size_popup(pin);
			break;
		case ID_WIDTH_3: case ID_WIDTH_6: case ID_WIDTH_12:
			pin->stroke_width = LOWORD(wparam) == ID_WIDTH_3 ? 3 : LOWORD(wparam) == ID_WIDTH_6 ? 6 : 12;
			CheckMenuRadioItem(GetSubMenu(pin->menu, 4), ID_WIDTH_3, ID_WIDTH_12,
				LOWORD(wparam), MF_BYCOMMAND);
			save_pinned_preferences(pin);
			break;
		case ID_CLOSE: SendMessage(hwnd, WM_CLOSE, 0, 0); break;
		}
		return 0;
	case WM_KEYDOWN:
		if (wparam == VK_ESCAPE) { SendMessage(hwnd, WM_CLOSE, 0, 0); return 0; }
		if (pin != NULL && GetKeyState(VK_CONTROL) < 0) {
			if (wparam == 'Z' && GetKeyState(VK_SHIFT) < 0) swap_history(pin, FALSE);
			else if (wparam == 'Z') swap_history(pin, TRUE);
			else if (wparam == 'Y') swap_history(pin, FALSE);
			else if (wparam == 'C') SendMessage(hwnd, WM_COMMAND, ID_COPY, 0);
			return 0;
		}
		break;
	case WM_LBUTTONDOWN:
		if (pin != NULL) {
			POINT point = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) }, image;
			if (PtInRect(&pin->image_rect, point) && pin->tool != ID_CROP &&
				!ensure_drawing_resolution(pin)) { MessageBeep(MB_ICONWARNING); return 0; }
			if (client_to_image(pin, point, &image)) {
				pin->drawing = TRUE;
				pin->start = pin->last = image;
				pin->erasing_changed = FALSE;
				pin->active_artifact = NULL;
				if (pin->tool != ID_ERASER && !begin_change(pin)) {
					pin->drawing = FALSE; MessageBeep(MB_ICONWARNING); return 0;
				}
				if (pin->tool == ID_PEN || pin->tool == ID_MARKER) {
					pin->active_artifact = add_artifact(pin, pin->tool, image, image_stroke_width(pin, 1));
					if (pin->active_artifact != NULL && append_artifact_point(pin->active_artifact, image))
						draw_segment(pin, image, image, FALSE);
				} else if (pin->tool == ID_ERASER) {
					erase_artifacts(pin, image, image);
				}
				if (pin->tool == ID_PEN || pin->tool == ID_MARKER || pin->tool == ID_ERASER) {
					InvalidateRect(hwnd, &pin->image_rect, FALSE);
				}
				SetCapture(hwnd);
			}
		}
		return 0;
	case WM_MOUSEMOVE:
		if (pin != NULL && pin->drawing) {
			POINT point = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) }, image;
			if (client_to_image(pin, point, &image)) {
				if ((pin->tool == ID_PEN || pin->tool == ID_MARKER) &&
					pin->active_artifact != NULL && append_artifact_point(pin->active_artifact, image))
					draw_segment(pin, pin->last, image, FALSE);
				else if (pin->tool == ID_ERASER) erase_artifacts(pin, pin->last, image);
				pin->last = image;
				InvalidateRect(hwnd, &pin->image_rect, FALSE);
			}
		}
		return 0;
	case WM_LBUTTONUP:
		if (pin != NULL && pin->drawing) {
			POINT point = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) }, end = pin->last;
			client_to_image(pin, point, &end);
			if ((pin->tool == ID_PEN || pin->tool == ID_MARKER) && pin->active_artifact != NULL &&
				(end.x != pin->last.x || end.y != pin->last.y) &&
				append_artifact_point(pin->active_artifact, end)) draw_segment(pin, pin->last, end, FALSE);
			else if (pin->tool == ID_ERASER) erase_artifacts(pin, pin->last, end);
			pin->drawing = FALSE;
			pin->active_artifact = NULL;
			ReleaseCapture();
			if (pin->tool == ID_LINE || pin->tool == ID_ARROW || pin->tool == ID_RECT ||
				pin->tool == ID_ELLIPSE || pin->tool == ID_FILLED_RECT || pin->tool == ID_FILLED_ELLIPSE) {
				PIN_ARTIFACT *item = add_artifact(pin, pin->tool, pin->start, image_stroke_width(pin, 1));
				if (item != NULL) { item->end = end; finish_shape(pin, end); }
			} else if (pin->tool == ID_CROP) {
				RECT crop = { min(pin->start.x, end.x), min(pin->start.y, end.y), max(pin->start.x, end.x), max(pin->start.y, end.y) };
				if (crop.right > crop.left + 1 && crop.bottom > crop.top + 1) {
					{
						int width, height;
						HBITMAP cropped = crop_bitmap(pin->bitmap, crop, &width, &height);
						HBITMAP base = crop_bitmap(pin->original, crop, &crop.right, &crop.bottom);
						if (cropped != NULL && base != NULL) {
							DeleteObject(pin->bitmap); DeleteObject(pin->original);
							pin->bitmap = cropped; pin->original = base;
							transform_artifacts(pin, 1, 1, 1, 1, crop.left, crop.top);
							pin->width = width; pin->height = height;
							refit_image(pin);
							InvalidateRect(hwnd, NULL, FALSE);
						} else {
							if (cropped != NULL) DeleteObject(cropped);
							if (base != NULL) DeleteObject(base);
						}
					}
				}
			}
			InvalidateRect(hwnd, &pin->image_rect, FALSE);
		}
		return 0;
	case WM_CAPTURECHANGED:
		if (pin != NULL) {
			pin->drawing = FALSE;
			pin->active_artifact = NULL;
			InvalidateRect(hwnd, &pin->image_rect, FALSE);
		}
		return 0;
	case WM_PAINT:
		if (pin != NULL) {
			PAINTSTRUCT ps;
			HDC dc = BeginPaint(hwnd, &ps), source, buffer_dc;
			HDC canvas = dc;
			HBITMAP buffer = NULL, old_buffer = NULL;
			HBITMAP old;
			RECT client, image_border, shadow;
			HBRUSH brush;
			BOOL dark = !dark_mode_is_dark();
			GetClientRect(hwnd, &client);
			buffer_dc = CreateCompatibleDC(dc);
			if (buffer_dc != NULL) buffer = CreateCompatibleBitmap(dc, client.right, client.bottom);
			if (buffer != NULL) {
				old_buffer = SelectObject(buffer_dc, buffer);
				canvas = buffer_dc;
			}
			brush = CreateSolidBrush(dark ? RGB(35, 38, 43) : RGB(238, 241, 245));
			FillRect(canvas, &client, brush);
			DeleteObject(brush);
			shadow = pin->image_rect;
			OffsetRect(&shadow, 3, 4);
			brush = CreateSolidBrush(dark ? RGB(26, 28, 31) : RGB(205, 211, 218));
			FillRect(canvas, &shadow, brush);
			DeleteObject(brush);
			source = CreateCompatibleDC(dc);
			old = SelectObject(source, pin->bitmap);
			SetStretchBltMode(canvas, HALFTONE);
			SetBrushOrgEx(canvas, 0, 0, NULL);
			StretchBlt(canvas, pin->image_rect.left, pin->image_rect.top,
				pin->image_rect.right - pin->image_rect.left, pin->image_rect.bottom - pin->image_rect.top,
				source, 0, 0, pin->width, pin->height, SRCCOPY);
			image_border = pin->image_rect;
			brush = CreateSolidBrush(dark ? RGB(89, 95, 103) : RGB(188, 196, 205));
			FrameRect(canvas, &image_border, brush);
			DeleteObject(brush);
			if (pin->drawing && pin->tool != ID_PEN && pin->tool != ID_MARKER && pin->tool != ID_ERASER) {
				POINT start = image_to_client(pin, pin->start), end = image_to_client(pin, pin->last);
				int shown_w = pin->image_rect.right - pin->image_rect.left;
				COLORREF preview_color = (pin->tool == ID_CROP) ?
					RGB(0, 120, 215) : get_pin_color(pin);
				int preview_width = (pin->tool == ID_CROP) ?
					Scale(2) :
					(shown_w > 0 ? max(1, MulDiv(image_stroke_width(pin, 1), shown_w, pin->width)) : max(1, Scale(pin->stroke_width)));
				HPEN preview = CreatePen(PS_SOLID, preview_width, preview_color);
				HPEN old_pen = SelectObject(canvas, preview);
				HBRUSH fill = (pin->tool == ID_FILLED_RECT || pin->tool == ID_FILLED_ELLIPSE) ?
					CreateSolidBrush(preview_color) : NULL;
				HBRUSH old_brush = SelectObject(canvas, fill != NULL ? fill : GetStockObject(HOLLOW_BRUSH));
				if (pin->tool == ID_LINE || pin->tool == ID_ARROW) {
					MoveToEx(canvas, start.x, start.y, NULL); LineTo(canvas, end.x, end.y);
					if (pin->tool == ID_ARROW && (start.x != end.x || start.y != end.y)) {
						double angle = atan2((double)(end.y - start.y), (double)(end.x - start.x));
						int wing_size = max(preview_width * 4, shown_w > 0 ? MulDiv(image_stroke_width(pin, 3), shown_w, pin->width) : Scale(pin->stroke_width * 3));
						POINT wing1 = { end.x - (int)(cos(angle - 0.55) * wing_size), end.y - (int)(sin(angle - 0.55) * wing_size) };
						POINT wing2 = { end.x - (int)(cos(angle + 0.55) * wing_size), end.y - (int)(sin(angle + 0.55) * wing_size) };
						MoveToEx(canvas, end.x, end.y, NULL); LineTo(canvas, wing1.x, wing1.y);
						MoveToEx(canvas, end.x, end.y, NULL); LineTo(canvas, wing2.x, wing2.y);
					}
				} else if (pin->tool == ID_ELLIPSE || pin->tool == ID_FILLED_ELLIPSE) {
					Ellipse(canvas, min(start.x, end.x), min(start.y, end.y), max(start.x, end.x), max(start.y, end.y));
				} else {
					Rectangle(canvas, min(start.x, end.x), min(start.y, end.y), max(start.x, end.x), max(start.y, end.y));
				}
				SelectObject(canvas, old_brush);
				if (fill != NULL) DeleteObject(fill);
				SelectObject(canvas, old_pen);
				DeleteObject(preview);
			}
			SelectObject(source, old);
			DeleteDC(source);
			if (buffer != NULL) {
				BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top,
					ps.rcPaint.right - ps.rcPaint.left, ps.rcPaint.bottom - ps.rcPaint.top,
					canvas, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
				SelectObject(buffer_dc, old_buffer);
				DeleteObject(buffer);
			}
			if (buffer_dc != NULL) DeleteDC(buffer_dc);
			EndPaint(hwnd, &ps);
		}
		return 0;
	case WM_CLOSE:
		DestroyWindow(hwnd);
		return 0;
	case WM_DESTROY:
		if (pin != NULL) {
			PINNED_IMAGE **link = &pin_windows;
			if (current_color_popup_hwnd != NULL && IsWindow(current_color_popup_hwnd)) {
				DestroyWindow(current_color_popup_hwnd);
				current_color_popup_hwnd = NULL;
			}
			save_pinned_preferences(pin);
			if (pin->toolbar != NULL) DestroyWindow(pin->toolbar);
			if (pin->icons != NULL) ImageList_Destroy(pin->icons);
			if (pin->tool_cursor != NULL) DestroyCursor(pin->tool_cursor);
			while (*link != NULL && *link != pin) link = &(*link)->next;
			if (*link == pin) *link = pin->next;
			clear_edit_stack(pin->undo, pin->undo_base, pin->undo_artifacts, &pin->undo_count);
			clear_edit_stack(pin->redo, pin->redo_base, pin->redo_artifacts, &pin->redo_count);
			free_artifacts(pin->artifacts);
			DeleteObject(pin->original);
			DeleteObject(pin->bitmap);
			SetMenu(hwnd, NULL);
			DestroyMenu(pin->menu);
			mem_free(&pin);
		}
		return 0;
	}
	return DefWindowProc(hwnd, msg, wparam, lparam);
}
