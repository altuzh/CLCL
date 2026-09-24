/* Native popup-loop regression. No user profile, clipboard, or database is opened.
 * Only cursor I/O is recorded: Windows cannot move the pointer on a non-input
 * desktop. Menu creation, drawing, tracking, hooks and dialogs are real Win32. */
#include <windows.h>
#include <stdio.h>
static POINT test_cursor = {300, 200};
static BOOL test_get_cursor(LPPOINT pt) { *pt = test_cursor; return TRUE; }
static BOOL test_set_cursor(int x, int y) { test_cursor.x = x; test_cursor.y = y; return TRUE; }
#define GetCursorPos test_get_cursor
#define SetCursorPos test_set_cursor
#define menu_show_align test_menu_show_align
#define save_regist unused_save_regist
#define pinned_image_clipboard_is_screenshot test_screenshot_pending
#include "../main.c"
#undef pinned_image_clipboard_is_screenshot
static BOOL screenshot_pending;
BOOL test_screenshot_pending(HWND owner) { (void)owner; return screenshot_pending; }
/* Keep editor clipboard checks isolated from the user's clipboard. */
static BOOL clipboard_available = TRUE;
static HANDLE copied_bitmap;
static BOOL copied_dib_valid;
static BOOL test_open_clipboard(HWND owner) { (void)owner; return clipboard_available; }
static BOOL test_empty_clipboard(void) { return TRUE; }
static HANDLE test_set_clipboard(UINT format, HANDLE bitmap)
{
    if (format == CF_DIB) {
        BYTE *memory = GlobalLock(bitmap);
        HBITMAP decoded = memory != NULL ? dib_to_bitmap(memory) : NULL;
        copied_dib_valid = decoded != NULL;
        if (decoded != NULL) DeleteObject(decoded);
        if (memory != NULL) GlobalUnlock(bitmap);
        GlobalFree(bitmap); /* Mock takes ownership just as Windows does. */
    } else copied_bitmap = bitmap;
    return bitmap;
}
static BOOL test_close_clipboard(void) { return TRUE; }
#define OpenClipboard test_open_clipboard
#define EmptyClipboard test_empty_clipboard
#define SetClipboardData test_set_clipboard
#define CloseClipboard test_close_clipboard
#include "../PinnedImage.c"
#undef OpenClipboard
#undef EmptyClipboard
#undef SetClipboardData
#undef CloseClipboard
#undef GetCursorPos
#undef SetCursorPos
#undef menu_show_align
#undef save_regist
BOOL save_regist(HWND owner) { (void)owner; return TRUE; }
static int phase, ticks, failures, scenario, open_steps;
static BOOL finished;
static DATA_INFO *folder, *landing;
static HWND idle_menu;
static POINT before_delete;
static POINT switch_point;
static RECT switch_row;
static RECT latest_root_bounds;
static DATA_INFO *switch_target, *clipboard_selection;
static int insert_case, insert_phase, insert_ticks;

int menu_show_align(HWND owner, HMENU menu, const POINT *pos, UINT align);
int test_menu_show_align(HWND owner, HMENU menu, const POINT *pos, UINT align)
{
    POINT initial = {300, 200};
    if (scenario == 10) {
        HMONITOR hMon = MonitorFromWindow(owner, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi;
        mi.cbSize = sizeof(mi);
        if (hMon && GetMonitorInfo(hMon, &mi)) {
            initial.x = mi.rcWork.right;
            initial.y = mi.rcWork.bottom;
        }
    }
    return menu_show_align(owner, menu, pos ? pos : &initial, align);
}

#define CHECK(condition, message) do { if (!(condition)) { \
    printf("FAIL: %s (phase %d)\n", message, phase); failures++; } } while (0)

static void test_pinned_image_edit_core(HWND owner)
{
    HDC screen = GetDC(NULL), dc = CreateCompatibleDC(screen);
    HBITMAP source = CreateCompatibleBitmap(screen, 80, 40), prior;
    HBITMAP cropped;
    PINNED_IMAGE pin = {0};
    BITMAP bm;
    RECT crop = {10, 5, 50, 25};
    POINT from = {1, 1}, to = {20, 10};
    DATA_INFO image_data = {0};
    HWND editor;
    HMENU menu, image_menu, tools_menu, color_menu, size_menu;
    int id;
    int width = 0, height = 0;
    prior = SelectObject(dc, source);
    PatBlt(dc, 0, 0, 80, 40, WHITENESS);
    SelectObject(dc, prior);
    DeleteDC(dc);
    ReleaseDC(NULL, screen);
    {
        const int tools[] = {ID_RECT, ID_FILLED_RECT, ID_ELLIPSE, ID_FILLED_ELLIPSE};
        int shape;
        for (shape = 0; shape < 4; shape++) {
            PINNED_IMAGE sample = {0};
            HDC sample_dc = CreateCompatibleDC(NULL);
            HBITMAP old;
            COLORREF center;
            sample.bitmap = clone_bitmap(source, NULL, NULL);
            sample.width = 80; sample.height = 40;
            sample.tool = tools[shape];
            sample.stroke_width = 3;
            sample.current_color = RGB(30, 90, 220);
            SetRect(&sample.image_rect, 0, 0, 80, 40);
            sample.start = (POINT){10, 5};
            finish_shape(&sample, (POINT){60, 30});
            old = SelectObject(sample_dc, sample.bitmap);
            center = GetPixel(sample_dc, 35, 17);
            CHECK(center == (shape % 2 ? sample.current_color : RGB(255, 255, 255)),
                "filled shapes color their centers and outline shapes leave centers unchanged");
            SelectObject(sample_dc, old);
            DeleteDC(sample_dc);
            DeleteObject(sample.bitmap);
        }
    }
    {
        PINNED_IMAGE marks = {0};
        PIN_ARTIFACT *first, *second;
        HDC image_dc = CreateCompatibleDC(NULL);
        HBITMAP old;
        marks.original = clone_bitmap(source, NULL, NULL);
        marks.bitmap = clone_bitmap(source, NULL, NULL);
        marks.width = 80; marks.height = 40;
        marks.stroke_width = 3;
        marks.current_color = RGB(220, 32, 32);
        SetRect(&marks.image_rect, 0, 0, 80, 40);
        first = add_artifact(&marks, ID_FILLED_RECT, (POINT){8, 8}, 3);
        second = add_artifact(&marks, ID_FILLED_RECT, (POINT){50, 8}, 3);
        CHECK(first != NULL && second != NULL, "editor records separate drawing artifacts");
        if (first != NULL && second != NULL) {
            first->end = (POINT){28, 28};
            second->end = (POINT){70, 28};
            second->color = RGB(30, 90, 220);
            CHECK(rebuild_artifacts(&marks), "editor replays drawing artifacts");
            marks.tool = ID_ERASER;
            CHECK(erase_artifacts(&marks, (POINT){9, 9}, (POINT){9, 9}) && first->deleted && !second->deleted,
                "eraser removes the entire touched artifact only");
            old = SelectObject(image_dc, marks.bitmap);
            CHECK(GetPixel(image_dc, 20, 20) == RGB(255, 255, 255) &&
                GetPixel(image_dc, 60, 20) == second->color,
                "erasing restores original pixels without removing other artifacts");
            SelectObject(image_dc, old);
            swap_history(&marks, TRUE);
            old = SelectObject(image_dc, marks.bitmap);
            CHECK(GetPixel(image_dc, 20, 20) == RGB(220, 32, 32),
                "undo restores a removed artifact");
            SelectObject(image_dc, old);
            swap_history(&marks, FALSE);
            old = SelectObject(image_dc, marks.bitmap);
            CHECK(GetPixel(image_dc, 20, 20) == RGB(255, 255, 255),
                "redo removes the artifact again");
            SelectObject(image_dc, old);
        }
        DeleteDC(image_dc);
        clear_edit_stack(marks.undo, marks.undo_base, marks.undo_artifacts, &marks.undo_count);
        clear_edit_stack(marks.redo, marks.redo_base, marks.redo_artifacts, &marks.redo_count);
        free_artifacts(marks.artifacts);
        DeleteObject(marks.bitmap); DeleteObject(marks.original);
    }
    {
        PINNED_IMAGE enlarged = {0};
        POINT a, b;
        HDC dc_test;
        HBITMAP old_test;
        int stroke, x, painted = 0;
        enlarged.bitmap = clone_bitmap(source, NULL, NULL);
        enlarged.original = clone_bitmap(source, NULL, NULL);
        enlarged.width = 80; enlarged.height = 40;
        enlarged.stroke_width = 3;
        enlarged.current_color = RGB(220, 32, 32);
        SetRect(&enlarged.image_rect, 0, 0, 800, 400);
        CHECK(ensure_drawing_resolution(&enlarged) && enlarged.width == 800 && enlarged.height == 400,
            "small expanded bitmap gains one editing pixel per screen pixel");
        CHECK(client_to_image(&enlarged, (POINT){100, 100}, &a) &&
            client_to_image(&enlarged, (POINT){101, 100}, &b) && b.x - a.x == 1,
            "adjacent cursor pixels remain distinct drawing coordinates");
        begin_change(&enlarged);
        draw_segment(&enlarged, (POINT){100, 80}, (POINT){100, 120}, FALSE);
        stroke = image_stroke_width(&enlarged, 1);
        dc_test = CreateCompatibleDC(NULL);
        old_test = SelectObject(dc_test, enlarged.bitmap);
        for (x = 70; x <= 130; x++)
            if (GetPixel(dc_test, x, 100) == enlarged.current_color) painted++;
        CHECK(painted == stroke && stroke == MulDiv(3, GetDpi(), 96),
            "expanded image stroke has screen-DPI thickness without enlarged pixel blocks");
        SelectObject(dc_test, old_test); DeleteDC(dc_test);
        swap_history(&enlarged, TRUE);
        swap_history(&enlarged, FALSE);
        CHECK(enlarged.width == 800 && enlarged.height == 400,
            "undo redo retain screen-resolution editing bitmap");
        SetRect(&enlarged.image_rect, 0, 0, 400, 200);
        CHECK(ensure_drawing_resolution(&enlarged) && enlarged.width == 800,
            "shrinking the display never downsamples edits");
        CHECK(GetObject(source, sizeof(bm), &bm) && bm.bmWidth == 80 && bm.bmHeight == 40,
            "editing resolution leaves source bitmap dimensions unchanged");
        clear_stack(enlarged.undo, &enlarged.undo_count);
        clear_stack(enlarged.redo, &enlarged.redo_count);
        DeleteObject(enlarged.bitmap); DeleteObject(enlarged.original);
    }
    cropped = crop_bitmap(source, crop, &width, &height);
    CHECK(cropped != NULL && width == 40 && height == 20, "crop keeps selected dimensions");
    CHECK(GetObject(cropped, sizeof(bm), &bm) && bm.bmWidth == 40 && bm.bmHeight == 20,
        "cropped bitmap has expected aspect ratio");
    pin.hwnd = owner;
    pin.bitmap = cropped;
    pin.width = width;
    pin.height = height;
    pin.stroke_width = 3;
    pin.tool = ID_PEN;
    pin.current_color = colors[0];
    begin_change(&pin);
    draw_segment(&pin, from, to, FALSE);
    CHECK(pin.undo_count == 1 && pin.redo_count == 0, "edit records one undo snapshot");
    swap_history(&pin, TRUE);
    CHECK(pin.undo_count == 0 && pin.redo_count == 1, "undo moves current image to redo");
    swap_history(&pin, FALSE);
    CHECK(pin.undo_count == 1 && pin.redo_count == 0, "redo restores undo state");
    SetRect(&pin.image_rect, 0, 0, 20, 10);
    CHECK(image_stroke_width(&pin, 1) == 6,
        "stroke width compensates for image zoom at window DPI");
    SetRect(&pin.image_rect, 0, 0, 40, 20);
    pin.tool = ID_MARKER;
    pin.current_color = marker_colors[10];
    begin_change(&pin);
    {
        HDC marker_dc = CreateCompatibleDC(NULL);
        HBITMAP marker_old;
        POINT marker_a = {5, 15}, marker_b = {30, 15};
        COLORREF base, first, second;
        marker_old = SelectObject(marker_dc, pin.undo[pin.undo_count - 1]);
        base = GetPixel(marker_dc, 15, 15);
        SelectObject(marker_dc, marker_old);
        draw_segment(&pin, marker_a, marker_b, FALSE);
        marker_old = SelectObject(marker_dc, pin.bitmap);
        first = GetPixel(marker_dc, 15, 15);
        SelectObject(marker_dc, marker_old);
        draw_segment(&pin, marker_a, marker_b, FALSE);
        marker_old = SelectObject(marker_dc, pin.bitmap);
        second = GetPixel(marker_dc, 15, 15);
        CHECK(GetRValue(first) == (GetRValue(base) * 159 + GetRValue(pin.current_color) * 96) / 255 &&
            GetGValue(first) == (GetGValue(base) * 159 + GetGValue(pin.current_color) * 96) / 255 &&
            GetBValue(first) == (GetBValue(base) * 159 + GetBValue(pin.current_color) * 96) / 255 &&
            first == second, "selected highlighter color blends transparently without darkening overlaps");
        CHECK(marker_colors[0] != RGB(255, 255, 255) && marker_colors[10] != RGB(160, 160, 160),
            "highlighter palette has vivid colors instead of white or gray");
        SelectObject(marker_dc, marker_old);
        DeleteDC(marker_dc);
    }
    clear_stack(pin.undo, &pin.undo_count);
    clear_stack(pin.redo, &pin.redo_count);
    DeleteObject(pin.bitmap);
    image_data.type = TYPE_DATA;
    image_data.format = CF_BITMAP;
    image_data.format_name = TEXT("BITMAP");
    image_data.data = source;
    {
        RECT desktop = desktop_bounds();
        DATA_INFO capture = image_data;
        HBITMAP screenshot = scale_bitmap(source, desktop.right - desktop.left, desktop.bottom - desktop.top);
        PINNED_IMAGE *before = pin_windows;
        capture.data = screenshot;
        pinned_image_auto_open(owner, &image_data, 1001);
        CHECK(pin_windows == before, "ordinary bitmap does not auto open");
        last_copy_sequence = 1002;
        pinned_image_auto_open(owner, &capture, 1002);
        CHECK(pin_windows == before, "editor copy does not reopen screenshot");
        pinned_image_auto_open(owner, &capture, 1003);
        CHECK(pin_windows != before && IsZoomed(pin_windows->hwnd),
            "new desktop-sized clipboard image opens maximized editor");
        if (pin_windows != before) {
            HWND opened = pin_windows->hwnd;
            POINT primary = {0, 0};
            RECT client, image = pin_windows->image_rect;
            MONITORINFO monitor = { sizeof(monitor) };
            BITMAP clipboard_bitmap;
            GetMonitorInfo(MonitorFromPoint(primary, MONITOR_DEFAULTTOPRIMARY), &monitor);
            CHECK(copied_bitmap != NULL && GetObject(copied_bitmap, sizeof(clipboard_bitmap), &clipboard_bitmap) &&
                clipboard_bitmap.bmWidth == monitor.rcMonitor.right - monitor.rcMonitor.left &&
                clipboard_bitmap.bmHeight == monitor.rcMonitor.bottom - monitor.rcMonitor.top &&
                pin_windows->width == clipboard_bitmap.bmWidth && pin_windows->height == clipboard_bitmap.bmHeight,
                "clipboard is replaced with primary-monitor crop before editor opens");
            CHECK(MonitorFromWindow(opened, MONITOR_DEFAULTTONEAREST) ==
                MonitorFromPoint(primary, MONITOR_DEFAULTTOPRIMARY),
                "automatic screenshot opens on primary monitor");
            GetClientRect(opened, &client);
            CHECK(image.left >= 0 && image.top >= pin_windows->toolbar_height &&
                image.right <= client.right && image.bottom <= client.bottom &&
                abs(MulDiv(image.right - image.left, pin_windows->height, pin_windows->width) -
                    (image.bottom - image.top)) <= 1,
                "full desktop screenshot fits inside window with aspect ratio preserved");
            pinned_image_auto_open(owner, &capture, 1003);
            CHECK(pin_windows->hwnd == opened, "same clipboard sequence opens only once");
            DestroyWindow(opened);
        }
        if (copied_bitmap != NULL) DeleteObject(copied_bitmap);
        copied_bitmap = NULL;
        clipboard_available = FALSE;
        pinned_image_auto_open(owner, &capture, 1004);
        CHECK(pin_windows == before, "failed clipboard replacement does not open editor");
        clipboard_available = TRUE;
        {
            TCHAR error[BUF_SIZE];
            DATA_INFO *item = data_create_item(TEXT("Screenshot"), FALSE, error);
            item->child = data_create_data(CF_BITMAP, TEXT("BITMAP"), clone_bitmap(screenshot, NULL, NULL), 0, FALSE, error);
            item->child->next = data_create_data(CF_BITMAP, TEXT("BITMAP"), clone_bitmap(screenshot, NULL, NULL), 0, FALSE, error);
            clipboard_available = FALSE;
            pinned_image_auto_open(owner, item, 1005);
            CHECK(item->child->next != NULL, "failed crop publication retains original history formats");
            clipboard_available = TRUE;
            pinned_image_auto_open(owner, item, 1006);
            CHECK(item->child->next == NULL && copied_bitmap != NULL &&
                GetObject(item->child->data, sizeof(bm), &bm) &&
                pin_windows != before && bm.bmWidth == pin_windows->width && bm.bmHeight == pin_windows->height,
                "pending history item contains only cropped bitmap before persistence");
            if (pin_windows != before) DestroyWindow(pin_windows->hwnd);
            data_free(item);
            if (copied_bitmap != NULL) DeleteObject(copied_bitmap);
            copied_bitmap = NULL;
        }
        DeleteObject(screenshot);
        last_copy_sequence = last_auto_sequence = 0;
    }
    editor = pinned_image_open(owner, &image_data);
    CHECK(dark_mode_window_is_dark(editor) != dark_mode_is_dark(), "editor theme is inverse of application theme");
    CHECK(editor != NULL && (GetWindowLongPtr(editor, GWL_EXSTYLE) & WS_EX_TOPMOST),
        "pinned editor opens topmost");
    CHECK(editor != NULL && (GetWindowLongPtr(editor, GWL_STYLE) & WS_OVERLAPPEDWINDOW) == WS_OVERLAPPEDWINDOW &&
        GetWindow(editor, GW_OWNER) == NULL && (GetWindowLongPtr(editor, GWL_EXSTYLE) & WS_EX_APPWINDOW),
        "pinned editor uses an independent standard Windows frame");
    {
        WINDOWPLACEMENT placement = { sizeof(placement) };
        MONITORINFO monitor = { sizeof(monitor) };
        CHECK(IsZoomed(editor), "pinned editor starts maximized");
        if (GetWindowPlacement(editor, &placement) &&
            GetMonitorInfo(MonitorFromWindow(editor, MONITOR_DEFAULTTONEAREST), &monitor)) {
            RECT r = placement.rcNormalPosition;
            CHECK(abs(r.left + r.right - (monitor.rcWork.right - monitor.rcWork.left)) <= 2 &&
                abs(r.top + r.bottom - (monitor.rcWork.bottom - monitor.rcWork.top)) <= 2,
                "restored window is centered in monitor work area");
        }
    }
    {
        RECT desktop = desktop_bounds();
        CHECK(is_desktop_image(desktop.right - desktop.left, desktop.bottom - desktop.top) &&
            !is_desktop_image(desktop.right - desktop.left - 1, desktop.bottom - desktop.top),
            "desktop screenshot detection uses full bounding dimensions including gaps");
    }

    {
        int tool;
        for (tool = ID_PEN; tool <= ID_CROP; tool++) {
            ICONINFO info = {0};
            pin_windows->tool = tool;
            CHECK(GetIconInfo(editor_tool_cursor(pin_windows), &info) && !info.fIcon,
                "each editing tool provides a valid cursor");
            if (tool == ID_ARROW) {
                int size = MulDiv(32, GetWindowDpi(editor), 96);
                CHECK(editor_tool_cursor(pin_windows) != LoadCursor(NULL, IDC_CROSS) &&
                    info.xHotspot == (DWORD)MulDiv(27, size, 32) && info.yHotspot == (DWORD)MulDiv(4, size, 32),
                    "arrow cursor is distinct with its hotspot at the arrow tip");
            }
            if (info.hbmMask != NULL) DeleteObject(info.hbmMask);
            if (info.hbmColor != NULL) DeleteObject(info.hbmColor);
        }
        pin_windows->tool = ID_CROP;
    }
    menu = GetMenu(editor);
    CHECK(menu != NULL && GetMenuItemCount(menu) == 5, "editor has a standard five-part menu bar");
    image_menu = GetSubMenu(menu, 0);
    tools_menu = GetSubMenu(menu, 2);
    color_menu = GetSubMenu(menu, 3);
    size_menu = GetSubMenu(menu, 4);
    CHECK(GetMenuItemID(image_menu, 0) == ID_COPY && GetMenuItemID(image_menu, 1) == ID_UPDATE,
        "Image menu exposes copy and update");
    CHECK(GetMenuState(image_menu, ID_UPDATE, MF_BYCOMMAND) & MF_GRAYED,
        "detached bitmap cannot overwrite a source");
    for (id = ID_PEN; id <= ID_CROP; id++)
        CHECK(GetMenuState(tools_menu, id, MF_BYCOMMAND) != (UINT)-1,
            "every image tool is present in the Tools menu");
    CHECK(GetMenuState(tools_menu, ID_FILLED_RECT, MF_BYCOMMAND) != (UINT)-1 &&
        GetMenuState(tools_menu, ID_FILLED_ELLIPSE, MF_BYCOMMAND) != (UINT)-1,
        "both filled shapes are present in the Tools menu");
    CHECK(GetMenuState(tools_menu, ID_CROP, MF_BYCOMMAND) & MF_CHECKED,
        "Crop is initially checked");
    SendMessage(editor, WM_COMMAND, ID_RECT, 0);
    CHECK(GetMenuState(tools_menu, ID_RECT, MF_BYCOMMAND) & MF_CHECKED,
        "choosing a tool updates its menu checkmark");
    SendMessage(editor, WM_COMMAND, ID_FILLED_ELLIPSE, 0);
    CHECK((GetMenuState(tools_menu, ID_FILLED_ELLIPSE, MF_BYCOMMAND) & MF_CHECKED) &&
        !(GetMenuState(tools_menu, ID_RECT, MF_BYCOMMAND) & MF_CHECKED),
        "selecting filled ellipse clears outline selection");
    SendMessage(editor, WM_COMMAND, ID_RECT, 0);
    SendMessage(editor, WM_COMMAND, ID_COLOR_BLUE, 0);
    SendMessage(editor, WM_COMMAND, ID_WIDTH_6, 0);
    CHECK((GetMenuState(color_menu, ID_COLOR_BLUE, MF_BYCOMMAND) & MF_CHECKED) &&
        (GetMenuState(size_menu, ID_WIDTH_6, MF_BYCOMMAND) & MF_CHECKED),
        "color and stroke size menu choices stay selected");
    SendMessage(editor, WM_INITMENUPOPUP, (WPARAM)GetSubMenu(menu, 1), 0);
    CHECK((GetMenuState(GetSubMenu(menu, 1), ID_UNDO, MF_BYCOMMAND) & MF_GRAYED) &&
        (GetMenuState(GetSubMenu(menu, 1), ID_REDO, MF_BYCOMMAND) & MF_GRAYED),
        "unavailable undo and redo commands are disabled");
    {
        HWND toolbar = GetDlgItem(editor, ID_PIN_TOOLBAR);
        RECT toolbar_rect;
        CHECK(toolbar != NULL && (GetWindowLong(toolbar, GWL_STYLE) & TBSTYLE_FLAT),
            "editor uses a flat native toolbar");
        CHECK(toolbar != NULL && SendMessage(toolbar, TB_BUTTONCOUNT, 0, 0) == 19 &&
            ImageList_GetImageCount((HIMAGELIST)SendMessage(toolbar, TB_GETIMAGELIST, 0, 0)) == 15,
            "undo, redo, editing tools, color and size dropdowns have icons");
        CHECK(toolbar != NULL && SendMessage(toolbar, TB_COMMANDTOINDEX, ID_FILLED_RECT, 0) >= 0 &&
            SendMessage(toolbar, TB_COMMANDTOINDEX, ID_FILLED_ELLIPSE, 0) >= 0 &&
            FindResource(hInst, MAKEINTRESOURCE(IDR_PIN_FILLED_RECT), RT_GROUP_ICON) != NULL &&
            FindResource(hInst, MAKEINTRESOURCE(IDR_PIN_FILLED_ELLIPSE), RT_GROUP_ICON) != NULL,
            "filled shape buttons have distinct icon resources");
        CHECK(toolbar != NULL && SendMessage(toolbar, TB_ISBUTTONENABLED, ID_COLOR_DROPDOWN, 0),
            "color dropdown button is enabled on toolbar");
        CHECK(toolbar != NULL && SendMessage(toolbar, TB_ISBUTTONENABLED, ID_SIZE_DROPDOWN, 0) &&
            FindResource(hInst, MAKEINTRESOURCE(IDR_PIN_SIZE), RT_GROUP_ICON) != NULL,
            "size dropdown button has its icon and is enabled");
        CHECK(toolbar != NULL && (SendMessage(toolbar, TB_GETSTATE, ID_RECT, 0) & TBSTATE_CHECKED),
            "toolbar selection follows the Tools menu");
        CHECK(toolbar != NULL && !SendMessage(toolbar, TB_ISBUTTONENABLED, ID_UNDO, 0) &&
            !SendMessage(toolbar, TB_ISBUTTONENABLED, ID_REDO, 0),
            "undo and redo buttons start disabled");
        begin_change(pin_windows);
        CHECK(SendMessage(toolbar, TB_ISBUTTONENABLED, ID_UNDO, 0) &&
            !SendMessage(toolbar, TB_ISBUTTONENABLED, ID_REDO, 0),
            "editing enables Undo and leaves Redo disabled");
        SendMessage(editor, WM_COMMAND, ID_UNDO, 0);
        CHECK(!SendMessage(toolbar, TB_ISBUTTONENABLED, ID_UNDO, 0) &&
            SendMessage(toolbar, TB_ISBUTTONENABLED, ID_REDO, 0),
            "Undo enables Redo");
        SendMessage(editor, WM_COMMAND, ID_REDO, 0);
        CHECK(SendMessage(toolbar, TB_ISBUTTONENABLED, ID_UNDO, 0) &&
            !SendMessage(toolbar, TB_ISBUTTONENABLED, ID_REDO, 0),
            "Redo restores Undo availability");
        pin_windows->dirty = FALSE;
        if (toolbar != NULL && GetWindowRect(toolbar, &toolbar_rect))
            CHECK(toolbar_rect.bottom > toolbar_rect.top &&
                pin_windows->image_rect.top > toolbar_rect.bottom - toolbar_rect.top,
                "image canvas starts below the toolbar");
    }
    {
        RECT before, after, restored;
        RECT image_after;
        POINT start = image_to_client(pin_windows, (POINT){10, 5});
        POINT end = image_to_client(pin_windows, (POINT){50, 25});
        BITMAP original;
        GetWindowRect(editor, &before);
        SendMessage(editor, WM_COMMAND, ID_CROP, 0);
        SendMessage(editor, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(start.x, start.y));
        SendMessage(editor, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(end.x, end.y));
        SendMessage(editor, WM_LBUTTONUP, 0, MAKELPARAM(end.x, end.y));
        GetWindowRect(editor, &after);
        image_after = pin_windows->image_rect;
        CHECK(pin_windows->width == 40 && pin_windows->height == 20 &&
            GetObject(pin_windows->original, sizeof(original), &original) &&
            original.bmWidth == 40 && original.bmHeight == 20 &&
            EqualRect(&after, &before) && IsZoomed(editor) &&
            abs((image_after.right - image_after.left) - 2 * (image_after.bottom - image_after.top)) <= 2,
            "crop refits image while preserving maximized window bounds");
        SendMessage(editor, WM_COMMAND, ID_UNDO, 0);
        GetWindowRect(editor, &restored);
        CHECK(pin_windows->width == 80 && pin_windows->height == 40 &&
            abs((restored.right - restored.left) - (before.right - before.left)) <= 2,
            "undo crop restores original image and window size");
        ShowWindow(editor, SW_RESTORE);
        GetWindowRect(editor, &before);
        SendMessage(editor, WM_COMMAND, ID_REDO, 0);
        GetWindowRect(editor, &after);
        CHECK(EqualRect(&before, &after) && !IsZoomed(editor) && pin_windows->width == 40,
            "redo crop preserves restored window bounds");
        SendMessage(editor, WM_COMMAND, ID_UNDO, 0);
        GetWindowRect(editor, &after);
        CHECK(EqualRect(&before, &after), "undo crop preserves restored window bounds");
    }
    if (editor != NULL) DestroyWindow(editor);
    {
        RECT opened_rect;
        POINT primary = {0, 0};
        editor = pinned_image_open(owner, &image_data);
        CHECK(editor != NULL && GetWindowRect(editor, &opened_rect) &&
            IsZoomed(editor) &&
            MonitorFromWindow(editor, MONITOR_DEFAULTTONEAREST) ==
                MonitorFromPoint(primary, MONITOR_DEFAULTTOPRIMARY),
            "image editor opens maximized on primary");
        CHECK(editor != NULL && !IsRectEmpty(&pin_windows->image_rect),
            "image editor lays out the image canvas");
        if (editor != NULL) DestroyWindow(editor);
    }
    {
        MONITORINFO mi = { sizeof(mi) };
        RECT work;
        RECT sizing_rect;
        WINDOWPOS wp = {0};
        RECT after_resize;
        editor = pinned_image_open(owner, &image_data);
        CHECK(editor != NULL, "editor opens for resize border containment check");
        if (editor != NULL) {
            HMONITOR hmon = MonitorFromWindow(editor, MONITOR_DEFAULTTONEAREST);
            ShowWindow(editor, SW_RESTORE);
            if (!GetMonitorInfo(hmon, &mi)) SystemParametersInfo(SPI_GETWORKAREA, 0, &mi.rcWork, 0);
            work = mi.rcWork;

            /* 1. Test WM_SIZING with right border dragged past screen edge */
            sizing_rect.left = work.right - 200;
            sizing_rect.top = work.top + 100;
            sizing_rect.right = work.right + 500;
            sizing_rect.bottom = work.top + 300;
            SendMessage(editor, WM_SIZING, WMSZ_RIGHT, (LPARAM)&sizing_rect);
            CHECK(sizing_rect.right <= work.right && sizing_rect.left >= work.left &&
                sizing_rect.bottom <= work.bottom && sizing_rect.top >= work.top,
                "WM_SIZING keeps right-dragged window borders on screen");

            /* 2. Test WM_SIZING with bottom border dragged past screen edge */
            sizing_rect.left = work.left + 100;
            sizing_rect.top = work.bottom - 150;
            sizing_rect.right = work.left + 400;
            sizing_rect.bottom = work.bottom + 400;
            SendMessage(editor, WM_SIZING, WMSZ_BOTTOM, (LPARAM)&sizing_rect);
            CHECK(sizing_rect.bottom <= work.bottom && sizing_rect.top >= work.top &&
                sizing_rect.right <= work.right && sizing_rect.left >= work.left,
                "WM_SIZING keeps bottom-dragged window borders on screen");

            /* 3. Test WM_WINDOWPOSCHANGING when resizing partially off screen */
            wp.hwnd = editor;
            wp.x = work.right - 100;
            wp.y = work.bottom - 100;
            wp.cx = 450;
            wp.cy = 300;
            wp.flags = 0;
            SendMessage(editor, WM_WINDOWPOSCHANGING, 0, (LPARAM)&wp);
            CHECK(wp.x >= work.left && wp.x + wp.cx <= work.right &&
                wp.y >= work.top && wp.y + wp.cy <= work.bottom,
                "WM_WINDOWPOSCHANGING bounds resizing window within work area");

            /* 4. Test WM_EXITSIZEMOVE clamps window so borders never sit off screen */
            SetWindowPos(editor, NULL, work.right - 50, work.bottom - 50, 400, 300, SWP_NOZORDER | SWP_NOSIZE);
            SendMessage(editor, WM_EXITSIZEMOVE, 0, 0);
            GetWindowRect(editor, &after_resize);
            CHECK(after_resize.left >= work.left && after_resize.right <= work.right &&
                after_resize.top >= work.top && after_resize.bottom <= work.bottom,
                "WM_EXITSIZEMOVE restores all borders on screen after resize/move");
            /* 5. Test highlighter tracks cursor directly */
            {
                POINT pt1 = image_to_client(pin_windows, (POINT){20, 20});
                POINT pt2 = image_to_client(pin_windows, (POINT){35, 20});
                HDC test_dc;
                HBITMAP old_bm;
                COLORREF marker_col, before_col;
                POINT sample;
                SendMessage(editor, WM_COMMAND, ID_MARKER, 0);
                last_color_popup_toggle = 0;
                show_color_popup(pin_windows);
                CHECK(current_color_popup_hwnd != NULL, "highlighter color popup opens");
                if (current_color_popup_hwnd != NULL) {
                    RECT swatch = get_swatch_rect(0);
                    SendMessage(current_color_popup_hwnd, WM_LBUTTONUP, 0,
                        MAKELPARAM(swatch.left + 2, swatch.top + 2));
                }
                CHECK(pin_windows->current_color == marker_colors[0],
                    "highlighter swatch sets the selected color");
                SendMessage(editor, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(pt1.x, pt1.y));
                SendMessage(editor, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(pt2.x, pt2.y));
                SendMessage(editor, WM_LBUTTONUP, 0, MAKELPARAM(pt2.x, pt2.y));
                CHECK(client_to_image(pin_windows,
                    (POINT){(pt1.x + pt2.x) / 2, (pt1.y + pt2.y) / 2}, &sample),
                    "highlighter sample maps to image");
                test_dc = CreateCompatibleDC(NULL);
                old_bm = SelectObject(test_dc, pin_windows->undo[pin_windows->undo_count - 1]);
                before_col = GetPixel(test_dc, sample.x, sample.y);
                SelectObject(test_dc, old_bm);
                old_bm = SelectObject(test_dc, pin_windows->bitmap);
                marker_col = GetPixel(test_dc, sample.x, sample.y);
                SelectObject(test_dc, old_bm);
                DeleteDC(test_dc);
                CHECK(GetRValue(marker_col) == (GetRValue(before_col) * 159 + 255 * 96) / 255 &&
                    GetGValue(marker_col) == (GetGValue(before_col) * 159 + 230 * 96) / 255 &&
                    GetBValue(marker_col) == (GetBValue(before_col) * 159) / 255,
                    "highlighter stroke tracks cursor and selected color");
            }

            /* 6. Test ghost preview and line tool use selected color and stroke width */
            {
                POINT pt3 = image_to_client(pin_windows, (POINT){10, 10});
                POINT pt4 = image_to_client(pin_windows, (POINT){30, 10});
                HDC test_dc;
                HBITMAP old_bm;
                COLORREF line_col;
                SendMessage(editor, WM_COMMAND, ID_LINE, 0);
                SendMessage(editor, WM_COMMAND, ID_COLOR_GREEN, 0);
                SendMessage(editor, WM_COMMAND, ID_WIDTH_6, 0);
                SendMessage(editor, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(pt3.x, pt3.y));
                SendMessage(editor, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(pt4.x, pt4.y));
                CHECK(pin_windows->drawing && pin_windows->color_index == 2 && pin_windows->stroke_width == 6,
                    "ghost preview uses active tool color and stroke width");
                SendMessage(editor, WM_LBUTTONUP, 0, MAKELPARAM(pt4.x, pt4.y));
                test_dc = CreateCompatibleDC(NULL);
                old_bm = SelectObject(test_dc, pin_windows->bitmap);
                line_col = GetPixel(test_dc, 20, 10);
                SelectObject(test_dc, old_bm);
                DeleteDC(test_dc);
                CHECK(line_col == colors[2], "finished line matches tool color and thickness");
            }

            /* 7. Test closing a dirty pinned window closes without confirmation dialog */
            {
                HWND close_test = pinned_image_open(owner, &image_data);
                CHECK(close_test != NULL, "editor opens for close check");
                if (close_test != NULL) {
                    pin_windows->dirty = TRUE;
                    SendMessage(close_test, WM_CLOSE, 0, 0);
                    CHECK(!IsWindow(close_test), "WM_CLOSE destroys pinned window immediately without prompt");
                }
                close_test = pinned_image_open(owner, &image_data);
                CHECK(close_test != NULL, "editor opens for Escape check");
                if (close_test != NULL) {
                    SendMessage(close_test, WM_KEYDOWN, VK_ESCAPE, 0);
                    CHECK(!IsWindow(close_test), "Escape closes image editor");
                }
                close_test = pinned_image_open(owner, &image_data);
                CHECK(close_test != NULL, "editor opens for toolbar Escape check");
                if (close_test != NULL) {
                    HWND toolbar = pin_windows->toolbar;
                    SendMessage(toolbar, WM_KEYDOWN, VK_ESCAPE, 0);
                    CHECK(!IsWindow(close_test), "Escape closes editor with toolbar focus");
                }
            }

            {
                HWND copy_editor = pinned_image_open(owner, &image_data);
                CHECK(copy_editor != NULL, "editor opens for copy check");
                if (copy_editor != NULL) {
                    TBBUTTON copy_button;
                    HWND toolbar = pin_windows->toolbar;
                    int index = (int)SendMessage(toolbar, TB_COMMANDTOINDEX, ID_COPY, 0);
                    CHECK(index >= 0 && SendMessage(toolbar, TB_GETBUTTON, index, (LPARAM)&copy_button) &&
                        !(copy_button.fsStyle & TBSTYLE_CHECK), "Copy toolbar button is an action");
                    CHECK(SendMessage(toolbar, TB_COMMANDTOINDEX, 6108, 0) == -1,
                        "removed frame tool is absent from toolbar");
                    clipboard_available = FALSE;
                    SendMessage(copy_editor, WM_COMMAND, ID_COPY, 0);
                    CHECK(IsWindow(copy_editor) && copied_bitmap == NULL,
                        "failed copy preserves editor and image");
                    clipboard_available = TRUE;
                    SendMessage(copy_editor, WM_COMMAND, ID_COPY, 0);
                    CHECK(!IsWindow(copy_editor) && copied_bitmap != NULL,
                        "successful copy closes editor");
                    CHECK(copied_dib_valid, "Copy publishes independently decodable DIB pixel data");
                    CHECK(copied_bitmap != source && GetObject(copied_bitmap, sizeof(bm), &bm) &&
                        bm.bmWidth == 80 && bm.bmHeight == 40,
                        "clipboard owns independent bitmap after editor closes");
                    if (copied_bitmap != NULL) DeleteObject(copied_bitmap);
                    copied_bitmap = NULL;
                }
            }

            /* 8. Test standard color selection and session persistence */
            {
                HWND persist_editor;
                PINNED_IMAGE *p;
                SendMessage(editor, WM_COMMAND, ID_ARROW, 0);
                SendMessage(editor, WM_COMMAND, ID_WIDTH_12, 0);
                pin_windows->current_color = standard_colors[5]; // Orange
                update_color_button_icon(pin_windows);
                save_pinned_preferences(pin_windows);

                CHECK(option.pinned_tool == ID_ARROW, "persisted tool is ID_ARROW");
                CHECK(option.pinned_stroke_width == 12, "persisted stroke width is 12");
                CHECK(option.pinned_color == standard_colors[5], "persisted color is standard orange");

                persist_editor = pinned_image_open(owner, &image_data);
                CHECK(persist_editor != NULL, "new editor window opens with persisted settings");
                if (persist_editor != NULL) {
                    p = (PINNED_IMAGE *)GetWindowLongPtr(persist_editor, GWLP_USERDATA);
                    CHECK(p != NULL && p->tool == ID_CROP, "new editor defaults to Crop despite previously selected Arrow");
                    CHECK(p != NULL && p->stroke_width == 12, "restored editor stroke width matches persisted width");
                    CHECK(p != NULL && p->current_color == standard_colors[5], "restored editor color matches persisted color");
                    DestroyWindow(persist_editor);
                }
            }

            DestroyWindow(editor);
        }
    }
    DeleteObject(source);
    printf("PASS: Pinned image crop and undo/redo core\n");
}

static void key(HWND owner, UINT vk)
{
    if (vk == VK_DELETE && scenario % 2 == 0) {
        KBDLLHOOKSTRUCT kb = {0};
        kb.vkCode = vk;
        menu_key_wnd = owner;
        CHECK(menu_key_hook_proc(HC_ACTION, WM_KEYDOWN, (LPARAM)&kb) == 1, "hardware Delete down consumed");
        CHECK(menu_key_hook_proc(HC_ACTION, WM_KEYUP, (LPARAM)&kb) == 1, "hardware Delete up consumed");
        menu_key_wnd = NULL;
    } else {
        PostMessage(owner, WM_KEYDOWN, vk, 1);
        PostMessage(owner, WM_KEYUP, vk, 0xC0000001);
    }
}

static BOOL CALLBACK count_ghost_windows(HWND hwnd, LPARAM count_ptr)
{
    TCHAR name[32] = {0};
    GetClassName(hwnd, name, 32);
    if (lstrcmp(name, TEXT("CLCL_MenuGhost")) == 0) (*(int *)count_ptr)++;
    return TRUE;
}

static BOOL CALLBACK fill_dialog(HWND hwnd, LPARAM unused)
{
    (void)unused;
    if (GetDlgItem(hwnd, IDC_FAV_EDIT_NAME)) {
        SetDlgItemText(hwnd, IDC_FAV_EDIT_NAME, TEXT("Created by regression"));
        PostMessage(hwnd, WM_COMMAND, IDOK, 0);
        phase = 32;
        return FALSE;
    }
    return TRUE;
}

static HMENU target_menu(DATA_INFO *target)
{
    if (!target || target == &history_data) return popup_menu;
    if (target == &regist_data) return menu_get_favourites_submenu(popup_menu);
    return menu_find_data_submenu(popup_menu, target);
}

static void right_click(HWND owner)
{
    MSG msg = {0};
    msg.hwnd = owner;
    msg.message = WM_RBUTTONUP;
    msg.pt = test_cursor;
    CHECK(menu_msg_filter_proc(MSGF_MENU, 0, (LPARAM)&msg) == 1, "right click handled");
}

static BOOL context_mouse(HWND owner, UINT message, POINT point)
{
    MSG msg = {0};
    msg.hwnd = owner;
    msg.message = message;
    msg.pt = point;
    return CallMsgFilter(&msg, MSGF_MENU);
}

static void check_context_highlight(void)
{
    MENU_CONTEXT_HIT *hit;
    MENU_ITEM_INFO *old_item = NULL;
    DRAWITEMSTRUCT draw = {0};
    RECT old_row = {0}, new_row = {0};
    HDC screen, source, canvas;
    HBITMAP bitmap, prior, source_prior;
    COLORREF old_before, old_after, new_before, new_after;
    int old_x, old_y, new_x, new_y;
    CHECK(menu_context_highlight && menu_context_highlight->set_di == switch_target,
        "highlight tracks the newly right-clicked clipboard item");
    CHECK(menu_context_original == menu_context_highlight, "replacement snapshot starts on new selection");
    for (hit = menu_context_hits; hit; hit = hit->next) {
        if (hit->item->set_di == folder->child) {
            old_row = hit->rect;
            old_item = hit->item;
        }
        if (hit->item == menu_context_highlight) new_row = hit->rect;
    }
    CHECK(!IsRectEmpty(&old_row) && !IsRectEmpty(&new_row), "both highlighted rows were captured");
    if (IsRectEmpty(&old_row) || IsRectEmpty(&new_row) || !menu_ghost_bmp) return;
    screen = GetDC(NULL);
    source = CreateCompatibleDC(screen);
    canvas = CreateCompatibleDC(screen);
    bitmap = CreateCompatibleBitmap(screen, menu_ghost_rect.right - menu_ghost_rect.left,
        menu_ghost_rect.bottom - menu_ghost_rect.top);
    source_prior = SelectObject(source, menu_ghost_bmp);
    prior = SelectObject(canvas, bitmap);
    BitBlt(canvas, 0, 0, menu_ghost_rect.right - menu_ghost_rect.left,
        menu_ghost_rect.bottom - menu_ghost_rect.top, source, 0, 0, SRCCOPY);
    old_x = old_row.right - menu_ghost_rect.left - 6;
    old_y = (old_row.top + old_row.bottom) / 2 - menu_ghost_rect.top;
    new_x = new_row.right - menu_ghost_rect.left - 6;
    new_y = (new_row.top + new_row.bottom) / 2 - menu_ghost_rect.top;
    old_before = GetPixel(canvas, old_x, old_y);
    new_before = GetPixel(canvas, new_x, new_y);
    draw.CtlType = ODT_MENU;
    draw.hDC = canvas;
    draw.itemID = old_item->id;
    draw.itemData = (ULONG_PTR)old_item;
    draw.itemState = ODS_SELECTED;
    draw.rcItem = old_row;
    OffsetRect(&draw.rcItem, -menu_ghost_rect.left, -menu_ghost_rect.top);
    menu_drawitem(&draw);
    draw.itemID = menu_context_highlight->id;
    draw.itemData = (ULONG_PTR)menu_context_highlight;
    draw.itemState = 0;
    draw.rcItem = new_row;
    OffsetRect(&draw.rcItem, -menu_ghost_rect.left, -menu_ghost_rect.top);
    menu_drawitem(&draw);
    old_after = GetPixel(canvas, old_x, old_y);
    new_after = GetPixel(canvas, new_x, new_y);
    CHECK(old_after != old_before, "old clipboard highlight is cleared");
    CHECK(new_after != new_before, "new clipboard item is visibly highlighted");
    SelectObject(canvas, prior);
    SelectObject(source, source_prior);
    DeleteObject(bitmap);
    DeleteDC(canvas);
    DeleteDC(source);
    ReleaseDC(NULL, screen);
}

static void test_insert_menu_step(HWND hwnd)
{
    HMENU menu;
    int index = 0;
    TCHAR label[128];
    if (++insert_ticks > 30) {
        CHECK(FALSE, "favourite insertion menu timed out");
        KillTimer(hwnd, 78);
        EndMenu();
        return;
    }
    if (!IsWindowVisible(idle_menu)) return;
    menu = (HMENU)SendMessage(idle_menu, MN_GETHMENU, 0, 0);
    if (insert_case == 8) {
        GetMenuString(menu, 0, label, 128, MF_BYPOSITION);
        CHECK(lstrcmp(label, TEXT("&Edit image...")) == 0,
            "bitmap context menu starts with Edit image");
        SendMessage(idle_menu, 0x01E5, 0, 0);
        key(hwnd, VK_RETURN);
        return;
    }
    if (insert_case == 6) {
        EndMenu();
        return;
    }
    if (insert_phase == 1 && insert_case != 7) {
        const TCHAR *target = insert_case == 4 ? TEXT("Root anchor") : TEXT("Outer");
        for (index = 0; index < GetMenuItemCount(menu); index++) {
            GetMenuString(menu, index, label, 128, MF_BYPOSITION);
            if (lstrcmp(label, target) == 0) break;
        }
        CHECK(index < GetMenuItemCount(menu), "favourite destination is shown in root");
    } else if (insert_phase == 2) {
        const TCHAR *target = insert_case == 3 ? TEXT("Empty") : TEXT("Nested");
        for (index = 0; index < GetMenuItemCount(menu); index++) {
            GetMenuString(menu, index, label, 128, MF_BYPOSITION);
            if (lstrcmp(label, target) == 0) break;
        }
        CHECK(index < GetMenuItemCount(menu), "nested and empty folders expand");
    } else if (insert_phase == 3 || (insert_phase == 1 && insert_case == 7)) {
        MENUITEMINFO info = {0};
        index = insert_case == 0 ? 3 : insert_case == 2 ? 4 : insert_case == 5 ? 6 : 2;
        info.cbSize = sizeof(info);
        info.fMask = MIIM_STATE | MIIM_FTYPE;
        CHECK(GetMenuItemInfo(menu, index, TRUE, &info) &&
            !(info.fState & MFS_DISABLED) && !(info.fType & MFT_SEPARATOR),
            "favourite item and placeholder rows are selectable");
        GetMenuString(menu, index, label, 128, MF_BYPOSITION);
        CHECK(lstrcmp(label, insert_case == 0 ? TEXT("First && existing") : TEXT(" ")) == 0,
            "favourite label is literal and placeholder is blank");
    }
    SendMessage(idle_menu, 0x01E5, index, 0);
    key(hwnd, insert_phase == 3 || (insert_phase == 1 && (insert_case == 4 || insert_case == 7)) ?
        VK_RETURN : VK_RIGHT);
    insert_phase++;
}

static LRESULT CALLBACK test_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_ITEM_TO_CLIPBOARD) {
        clipboard_selection = (DATA_INFO *)lp;
        return 0; /* Verify normal item dispatch without touching the clipboard. */
    }
    if (msg == WM_CREATE || msg == WM_DESTROY || msg == WM_HISTORY_CHANGED || msg == WM_REGIST_CHANGED)
        return 0; /* Never invoke application startup, persistence, or shutdown. */
    if (msg == WM_ENTERIDLE && wp == MSGF_MENU) {
        idle_menu = (HWND)lp;
        if (popup_menu && idle_menu == menu_root_wnd) GetWindowRect(idle_menu, &latest_root_bounds);
    }
    if (msg == WM_TIMER && wp == 78) {
        test_insert_menu_step(hwnd);
        return 0;
    }
    if (msg == WM_TIMER && wp == 77) {
        RECT rc;
        HMENU sub = popup_menu ? target_menu(scenario == 6 || scenario == 7 ? landing : folder) : NULL;
        if (++ticks > 40) {
            CHECK(FALSE, "menu test timed out");
            finished = TRUE;
            EndMenu();
            return 0;
        }
        if (scenario == 20 && phase == 0 && popup_menu && IsWindowVisible(menu_root_wnd)) {
            GetMenuItemRect(NULL, popup_menu, 1, &rc);
            test_cursor.x = rc.left + 4;
            test_cursor.y = rc.top + 7;
            before_delete = test_cursor;
            SendMessage(menu_root_wnd, 0x01E5, 1, 0);
            right_click(hwnd);
            phase = 50;
        } else if (scenario == 20 && phase == 50 && popup_menu == NULL && IsWindowVisible(idle_menu)) {
            HMENU context = (HMENU)SendMessage(idle_menu, MN_GETHMENU, 0, 0);
            CHECK(GetMenuItemCount(context) == 2, "Favourites root folder context opens");
            finished = TRUE;
            phase = 51;
            key(hwnd, VK_ESCAPE);
        } else if (scenario == 20 && phase == 51 && popup_menu && !menu_cursor_restore_needed) {
            CHECK(test_cursor.x == before_delete.x && test_cursor.y == before_delete.y,
                "Favourites root context keeps cursor on root row");
            phase = 52;
            EndMenu();
        } else if (phase == 0 && IsWindowVisible(idle_menu)) {
            SendMessage(idle_menu, 0x01E5, 0, 0);
            key(hwnd, VK_RIGHT);
            if (--open_steps == 0) phase++;
        } else if (phase == 1 && (HMENU)SendMessage(idle_menu, MN_GETHMENU, 0, 0) == sub && IsWindowVisible(idle_menu)) {
            int index = scenario == 8 ? 1 : 0;
            DATA_INFO *selected = scenario == 6 || scenario == 7 ? folder : (scenario == 8 ? folder->child->next : folder->child);
            SendMessage(idle_menu, 0x01E5, index, 0);
            CHECK(current_selected_mii && current_selected_mii->set_di == selected, "target selected");
            GetMenuItemRect(NULL, sub, index, &rc);
            test_cursor.x = rc.left + 15;
            test_cursor.y = rc.top + 7;
            before_delete = test_cursor;
            if (scenario == 1 || scenario == 4 || scenario == 6 || scenario == 7 || scenario >= 11) {
                phase = scenario >= 11 ? 40 : 10;
                if (scenario >= 11) {
                    if (scenario >= 16) {
                        switch_target = scenario == 19 ? &regist_data : folder->next;
                        GetMenuItemRect(NULL, popup_menu, 1, &switch_row);
                    } else {
                        switch_target = folder->child->next;
                        GetMenuItemRect(NULL, sub, 1, &switch_row);
                    }
                    switch_point.x = switch_row.left + 4;
                    switch_point.y = switch_row.top + 7;
                    if (scenario >= 17) switch_point.y = switch_row.bottom - 2;
                }
                if (scenario >= 17) {
                    GetMenuItemRect(NULL, popup_menu, 0, &rc);
                    test_cursor.x = rc.left + 4;
                    test_cursor.y = rc.top + 7;
                    SendMessage(menu_root_wnd, 0x01E5, 0, 0);
                    {
                        MENUITEMINFO info = {0};
                        info.cbSize = sizeof(info);
                        info.fMask = MIIM_DATA;
                        GetMenuItemInfo(popup_menu, 0, TRUE, &info);
                        current_selected_mii = (MENU_ITEM_INFO *)info.dwItemData;
                    }
                }
                right_click(hwnd);
            } else {
                phase = 2;
                key(hwnd, VK_DELETE);
            }
        } else if ((phase == 40 || phase == 43) && popup_menu == NULL && IsWindowVisible(idle_menu)) {
            BOOL left = scenario == 11 || scenario == 14 || scenario == 16;
            UINT down = left ? WM_LBUTTONDOWN : WM_RBUTTONDOWN;
            UINT up = left ? WM_LBUTTONUP : WM_RBUTTONUP;
            RECT overlap;
            POINT point;
            GetWindowRect(idle_menu, &rc);
            if (scenario >= 17) {
                CHECK(GetMenuItemCount((HMENU)SendMessage(idle_menu, MN_GETHMENU, 0, 0)) == 1,
                    "initial context belongs to date folder");
            }
            if (IntersectRect(&overlap, &rc, &switch_row)) {
                point.x = overlap.left + 1;
                point.y = overlap.top + 1;
                CHECK(!context_mouse(hwnd, down, point), "live context menu takes priority over clipboard rows");
                CHECK(!context_mouse(hwnd, up, point), "live context menu release is not intercepted");
            }
            if (scenario == 13 && phase == 40) {
                SendMessage(idle_menu, 0x01E5, 0, 0);
                key(hwnd, VK_RIGHT);
                phase = 43;
                return 0;
            }
            CHECK(context_mouse(hwnd, down, switch_point), "clipboard item press consumed while context is open");
            point.x = point.y = -30000;
            CHECK(context_mouse(hwnd, up, point), "drag-off release consumed without selecting");
            CHECK(menu_context_pick == NULL, "dragging off does not change context target");
            CHECK(context_mouse(hwnd, down, switch_point), "new clipboard item press consumed");
            phase = scenario == 16 ? 46 : (left ? 45 : 41);
            finished = scenario == 16 ? FALSE : left;
            test_cursor = switch_point;
            if (!context_mouse(hwnd, up, switch_point)) {
                CHECK(FALSE, "new clipboard item release consumed");
                finished = TRUE;
                EndMenu();
            }
        } else if (phase == 41 && popup_menu == NULL && IsWindowVisible(idle_menu)) {
            HMENU context = (HMENU)SendMessage(idle_menu, MN_GETHMENU, 0, 0);
            CHECK(menu_context_pick == NULL, "replacement context menu has started");
            if (scenario >= 17) {
                MENU_CONTEXT_HIT *hit;
                BOOL found_child = FALSE;
                CHECK(menu_context_highlight && menu_context_highlight->set_di == switch_target,
                    "new context target selected after date context");
                for (hit = menu_context_hits; hit; hit = hit->next) {
                    CHECK(hit->item->set_di != folder->child && hit->item->set_di != folder->child->next,
                        "previous date subitems absent from replacement snapshot");
                    if (hit->item->set_di == switch_target->child) found_child = TRUE;
                }
                if (scenario == 17) {
                    CHECK(EqualRect(&menu_ghost_rect, &latest_root_bounds),
                        "clipboard context snapshot contains only root panel");
                } else {
                    CHECK(found_child, "new date or Favourites items visible before context menu");
                }
                CHECK(test_cursor.x == switch_point.x && test_cursor.y == switch_point.y,
                    "context switch does not move cursor");
                finished = TRUE;
                phase = 47;
                key(hwnd, VK_ESCAPE);
                return 0;
            }
            check_context_highlight();
            SendMessage(idle_menu, 0x01E5, scenario == 13 ? 0 : GetMenuItemCount(context) - 1, 0);
            key(hwnd, scenario == 13 ? VK_RIGHT : VK_RETURN);
            phase = scenario == 13 ? 44 : 42;
        } else if (phase == 44 && popup_menu == NULL && IsWindowVisible(idle_menu)) {
            SendMessage(idle_menu, 0x01E5, 0, 0);
            key(hwnd, VK_RETURN);
            phase = 42;
        } else if (phase == 42 && popup_menu && !menu_cursor_restore_needed) {
            if (scenario == 13) {
                CHECK(regist_data.child && lstrcmp(regist_data.child->title, TEXT("Second disposable item")) == 0,
                    "Add to Favourites acts on newly right-clicked item");
                CHECK(folder->child->next == switch_target, "adding preserves both clipboard items");
            } else {
                CHECK(folder->child && folder->child != switch_target && !folder->child->next,
                    "Delete acts on newly right-clicked item only");
            }
            CHECK(IsWindowVisible(idle_menu) && (HMENU)SendMessage(idle_menu, MN_GETHMENU, 0, 0) == target_menu(folder),
                "clipboard menu restored after switched action");
            CHECK(clipboard_selection == NULL, "RMB does not run LMB action");
            finished = TRUE;
            EndMenu();
        } else if (phase == 47 && popup_menu && !menu_cursor_restore_needed) {
            CHECK((HMENU)SendMessage(idle_menu, MN_GETHMENU, 0, 0) ==
                (scenario == 17 ? popup_menu : target_menu(switch_target)),
                "dismissed context returns to new target hierarchy");
            EndMenu();
        } else if (phase == 46 && popup_menu && !menu_cursor_restore_needed) {
            CHECK(IsWindowVisible(idle_menu), "main menu remained open after LMB on date folder");
            CHECK(clipboard_selection == NULL, "LMB on date folder does not paste clipboard item");
            finished = TRUE;
            EndMenu();
        } else if ((phase == 10 || phase == 11) && popup_menu == NULL && IsWindowVisible(idle_menu)) {
            HMENU context = (HMENU)SendMessage(idle_menu, MN_GETHMENU, 0, 0);
            SendMessage(idle_menu, 0x01E5, scenario == 6 ? 0 : GetMenuItemCount(context) - 1, 0);
            key(hwnd, VK_RETURN);
            phase = phase == 11 ? 3 : (scenario == 6 ? 31 : (scenario == 7 ? 3 : 2));
        } else if (phase == 31) {
            EnumThreadWindows(GetCurrentThreadId(), fill_dialog, 0);
        } else if (phase == 32 && popup_menu && !menu_cursor_restore_needed) {
            DATA_INFO *child;
            BOOL found = FALSE;
            for (child = folder->child; child; child = child->next)
                if (child->title && lstrcmp(child->title, TEXT("Created by regression")) == 0) found = TRUE;
            CHECK(found, "dialog created new folder");
            CHECK((HMENU)SendMessage(idle_menu, MN_GETHMENU, 0, 0) == target_menu(landing), "create keeps current menu");
            CHECK(test_cursor.x == before_delete.x && test_cursor.y == before_delete.y, "create keeps cursor in place");
            finished = TRUE;
            EndMenu();
        } else if (phase == 2 && popup_menu && !menu_cursor_restore_needed) {
            if (scenario == 0) {
                int ghosts = 0;
                menu_ghost_wnd = menu_ghost_show();
                CHECK(menu_ghost_wnd != NULL, "first bitmap menu ghost opens");
                menu_ghost_wnd = menu_ghost_show();
                EnumThreadWindows(GetCurrentThreadId(), count_ghost_windows, (LPARAM)&ghosts);
                CHECK(ghosts == 1, "rapid bitmap deletes leave only one ghost overlay");
                menu_ghost_hide();
            }
            CHECK(folder->child && !folder->child->next, "Delete removed exactly one item");
            CHECK(IsWindowVisible(idle_menu) && (HMENU)SendMessage(idle_menu, MN_GETHMENU, 0, 0) == sub, "current submenu remains visible");
            if (scenario == 10) {
                MONITORINFO mi;
                HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
                mi.cbSize = sizeof(mi);
                GetMonitorInfo(hMon, &mi);
                GetWindowRect(idle_menu, &rc);
                CHECK((menu_reopen_align & TPM_RIGHTALIGN) != 0, "TPM_RIGHTALIGN recorded");
                CHECK((menu_reopen_align & TPM_BOTTOMALIGN) != 0, "TPM_BOTTOMALIGN recorded");
                CHECK(rc.right >= mi.rcWork.right - 10, "reopened menu pinned to right edge");
                CHECK(rc.bottom >= mi.rcWork.bottom - 10, "reopened menu pinned to bottom edge");
            } else if (scenario == 8) {
                GetMenuItemRect(NULL, sub, 0, &rc);
                CHECK(PtInRect(&rc, test_cursor) && test_cursor.x == before_delete.x, "deleted last row clamps cursor to remaining row");
            } else {
                CHECK(test_cursor.x == before_delete.x && test_cursor.y == before_delete.y, "cursor stays in place");
            }
            SendMessage(idle_menu, 0x01E5, 0, 0);
            if (scenario == 4) {
                phase = 11;
                right_click(hwnd);
            } else {
                key(hwnd, VK_DELETE);
                phase++;
            }
        } else if (phase == 3 && popup_menu && !menu_cursor_restore_needed) {
            if (scenario < 2 || scenario >= 8) CHECK(history_data.child == NULL, "empty history folder pruned");
            else if (scenario == 2) CHECK(regist_data.child == NULL, "last root favourite removed");
            else if (scenario == 7) CHECK(landing->child && !landing->child->next, "explicit folder deletion preserves sibling");
            else {
                BOOL preserved = data_check(&regist_data, folder) != NULL;
                CHECK(preserved, "empty Favourites submenu is preserved");
                if (preserved) {
                    CHECK(folder->child == NULL, "only the last favourite was deleted");
                    CHECK(data_check(&regist_data, folder) == landing, "Favourites ancestors preserved");
                    CHECK(menu_find_data_index(target_menu(landing), folder) >= 0, "empty submenu remains in parent menu");
                }
            }
            CHECK(IsWindowVisible(idle_menu) && (HMENU)SendMessage(idle_menu, MN_GETHMENU, 0, 0) == target_menu(landing), "parent menu remains visible");
            GetWindowRect(idle_menu, &rc);
            CHECK(PtInRect(&rc, test_cursor), "cursor lands in parent menu");
            finished = TRUE;
            EndMenu();
        }
        return 0;
    }
    return main_proc(hwnd, msg, wp, lp);
}

BOOL CALLBACK bitmap_get_menu_bitmap(DATA_INFO *di, const int width, const int height);

static void test_bitmap_serialization(void)
{
    TCHAR err_str[BUF_SIZE] = {0};
    TCHAR temp_path[MAX_PATH];
    GetTempPath(MAX_PATH, temp_path);
    lstrcat(temp_path, TEXT("clcl_test_regist.dat"));

    // Create a simple test bitmap
    HDC hdc = GetDC(NULL);
    HDC mem_dc = CreateCompatibleDC(hdc);
    HBITMAP hbmp = CreateCompatibleBitmap(hdc, 16, 16);
    HBITMAP old_bmp = (HBITMAP)SelectObject(mem_dc, hbmp);
    RECT rc = {0, 0, 16, 16};
    FillRect(mem_dc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
    SelectObject(mem_dc, old_bmp);
    DeleteDC(mem_dc);
    ReleaseDC(NULL, hdc);

    // Create DATA_INFO item with CF_BITMAP
    DATA_INFO *item = data_create_item(TEXT("Test Bitmap Item"), FALSE, err_str);
    DATA_INFO *cdi = data_create_data(CF_BITMAP, TEXT("BITMAP"), (HANDLE)hbmp, 0, FALSE, err_str);
    item->child = cdi;

    // Test clipboard_data_to_bytes
    DWORD dib_size = 0;
    BYTE *dib_bytes = clipboard_data_to_bytes(cdi, &dib_size);
    CHECK(dib_bytes != NULL && dib_size > 0, "clipboard_data_to_bytes returns non-NULL for BITMAP");
    if (dib_bytes) mem_free((void **)&dib_bytes);

    // Test file_write_data
    BOOL write_ok = file_write_data(temp_path, item, FALSE, err_str);
    CHECK(write_ok, "file_write_data succeeds for BITMAP");

    // Free original item
    data_free(item);

    // Read back from file
    DATA_INFO *loaded = NULL;
    BOOL read_ok = file_read_data(temp_path, &loaded, err_str);
    CHECK(read_ok, "file_read_data succeeds for BITMAP");
    CHECK(loaded != NULL, "loaded item is non-NULL");
    if (loaded != NULL) {
        CHECK(loaded->child != NULL, "loaded child is non-NULL");
        if (loaded->child != NULL) {
            CHECK(loaded->child->data != NULL, "loaded bitmap data handle is non-NULL");
            CHECK(loaded->child->size > 0, "loaded bitmap size is greater than 0");
            CHECK(loaded->child->format == CF_BITMAP, "loaded child format is CF_BITMAP");
            // Test menu bitmap creation
            BOOL menu_bmp_ok = bitmap_get_menu_bitmap(loaded->child, 16, 16);
            CHECK(menu_bmp_ok, "bitmap_get_menu_bitmap succeeds on deserialized item");
            CHECK(loaded->child->menu_bitmap != NULL, "menu_bitmap is non-NULL");
        }
        data_free(loaded);
    }
    DeleteFile(temp_path);
    printf("PASS: Bitmap serialization/deserialization and thumbnail generation\n");
}

static HBITMAP create_solid_bitmap(int w, int h, COLORREF color)
{
    HDC hdc = GetDC(NULL);
    HDC mem_dc = CreateCompatibleDC(hdc);
    HBITMAP hbmp = CreateCompatibleBitmap(hdc, w, h);
    HBITMAP old_bmp = (HBITMAP)SelectObject(mem_dc, hbmp);
    HBRUSH brush = CreateSolidBrush(color);
    RECT rc = {0, 0, w, h};
    FillRect(mem_dc, &rc, brush);
    DeleteObject(brush);
    SelectObject(mem_dc, old_bmp);
    DeleteDC(mem_dc);
    ReleaseDC(NULL, hdc);
    return hbmp;
}

static void test_sqlite_multiple_bitmap_persistence(void)
{
    TCHAR err_str[BUF_SIZE] = {0};
    TCHAR temp_dir[MAX_PATH];
    TCHAR db_file[MAX_PATH];
    TCHAR saved_path[MAX_PATH];
    int saved_history_save = option.history_save;
    int saved_history_max = option.history_max;
    GetTempPath(MAX_PATH, temp_dir);
    lstrcat(temp_dir, TEXT("clcl_test_db_dir"));
    CreateDirectory(temp_dir, NULL);

    // Ensure cleanly closed before starting
    db_history_close();

    // 1. Initialize DB
    BOOL init_ok = db_history_init(temp_dir);
    CHECK(init_ok, "db_history_init succeeds");

    // 2. Create and save 3 distinct bitmap items
    HBITMAP bmp1 = create_solid_bitmap(16, 16, RGB(255, 0, 0));
    HBITMAP bmp2 = create_solid_bitmap(24, 24, RGB(0, 255, 0));
    HBITMAP bmp3 = create_solid_bitmap(32, 32, RGB(0, 0, 255));

    DATA_INFO *item1 = data_create_item(TEXT("(BITMAP)"), FALSE, err_str);
    item1->child = data_create_data(CF_BITMAP, TEXT("BITMAP"), (HANDLE)bmp1, 0, FALSE, err_str);
    data_set_modified(item1);

    DATA_INFO *item2 = data_create_item(TEXT("(BITMAP)"), FALSE, err_str);
    item2->child = data_create_data(CF_BITMAP, TEXT("BITMAP"), (HANDLE)bmp2, 0, FALSE, err_str);
    data_set_modified(item2);

    DATA_INFO *item3 = data_create_item(TEXT("(BITMAP)"), FALSE, err_str);
    item3->child = data_create_data(CF_BITMAP, TEXT("BITMAP"), (HANDLE)bmp3, 0, FALSE, err_str);
    data_set_modified(item3);

    BOOL s1 = db_history_save_item(item1);
    BOOL s2 = db_history_save_item(item2);
    BOOL s3 = db_history_save_item(item3);
    CHECK(s1 && s2 && s3, "db_history_save_item succeeds for all 3 bitmaps");
    DeleteObject((HBITMAP)item1->child->data);
    item1->child->data = create_solid_bitmap(40, 20, RGB(255, 120, 0));
    item1->child->size = 0;
    mem_free((void **)&item1->title);
    item1->title = alloc_copy(TEXT("Updated bitmap"));
    CHECK(db_history_update_item(item1), "db_history_update_item atomically replaces bitmap item");

    data_free(item1);
    data_free(item2);
    data_free(item3);

    // 3. Close database
    db_history_close();

    // 4. Simulate startup, including eager loading of history payloads.
    lstrcpy(saved_path, work_path);
    lstrcpy(work_path, temp_dir);
    option.history_save = 1;
    option.history_max = 30;
    CHECK(load_history(NULL, 0), "startup history load succeeds");

    int verified = 0, updated_found = 0;
    DATA_INFO *cur;
    for (cur = history_data.child; cur != NULL; cur = cur->next) {
        CHECK(cur->child != NULL, "loaded bitmap item has child format node");
        CHECK(cur->child != NULL && cur->child->format == CF_BITMAP, "child format is CF_BITMAP");
        CHECK(cur->param2 != 0 && cur->child != NULL && cur->child->data != NULL,
            "startup loaded bitmap data before menu creation");
        if (cur->title != NULL && lstrcmp(cur->title, TEXT("Updated bitmap")) == 0) {
            BITMAP updated;
            CHECK(GetObject(cur->child->data, sizeof(updated), &updated) != 0 &&
                updated.bmWidth == 40 && updated.bmHeight == 20,
                "updated bitmap dimensions persist across restart");
            updated_found++;
        }
        verified++;
    }
    CHECK(verified == 3, "verified all 3 loaded bitmap items");
    CHECK(updated_found == 1, "bitmap update replaces one row without duplication");

    data_free(history_data.child);
    history_data.child = NULL;
    db_history_close();
    lstrcpy(work_path, saved_path);
    option.history_save = saved_history_save;
    option.history_max = saved_history_max;

    // Clean up temporary database files
    wsprintf(db_file, TEXT("%s\\history.db"), temp_dir);
    DeleteFile(db_file);
    wsprintf(db_file, TEXT("%s\\history.db-shm"), temp_dir);
    DeleteFile(db_file);
    wsprintf(db_file, TEXT("%s\\history.db-wal"), temp_dir);
    DeleteFile(db_file);
    RemoveDirectory(temp_dir);

    printf("PASS: SQLite multiple bitmap persistence across restart\n");
}

static void test_date_folder_deletion(void)
{
    TCHAR err_str[BUF_SIZE] = {0};
    TCHAR temp_dir[MAX_PATH];
    TCHAR db_file[MAX_PATH];
    GetTempPath(MAX_PATH, temp_dir);
    lstrcat(temp_dir, TEXT("clcl_test_date_del_dir"));
    CreateDirectory(temp_dir, NULL);

    db_history_close();

    BOOL init_ok = db_history_init(temp_dir);
    CHECK(init_ok, "db_history_init succeeds for date folder test");

    HBITMAP bmp1 = create_solid_bitmap(16, 16, RGB(10, 20, 30));
    HBITMAP bmp2 = create_solid_bitmap(16, 16, RGB(40, 50, 60));

    DATA_INFO *item1 = data_create_item(TEXT("Item 1"), FALSE, err_str);
    item1->child = data_create_data(CF_BITMAP, TEXT("BITMAP"), (HANDLE)bmp1, 0, FALSE, err_str);
    data_set_modified(item1);

    DATA_INFO *item2 = data_create_item(TEXT("Item 2"), FALSE, err_str);
    item2->child = data_create_data(CF_BITMAP, TEXT("BITMAP"), (HANDLE)bmp2, 0, FALSE, err_str);
    data_set_modified(item2);

    BOOL s1 = db_history_save_item(item1);
    BOOL s2 = db_history_save_item(item2);
    CHECK(s1 && s2, "Items saved to DB with valid IDs");
    CHECK(item1->param1 > 0 && item2->param1 > 0, "Item IDs populated in param1");

    // Put items into a date folder
    DATA_INFO *date_folder = data_create_folder(TEXT("2026-09-23"), err_str);
    CHECK(date_folder != NULL, "Date folder created");
    date_folder->child = item1;
    item1->next = item2;
    item2->next = NULL;

    history_data.child = date_folder;

    // Delete the date folder
    BOOL del_ok = data_delete(&history_data.child, date_folder, TRUE);
    CHECK(del_ok, "data_delete succeeds on date folder");
    CHECK(history_data.child == NULL, "Date folder removed from history_data.child in memory");

    // Verify both items were deleted from SQLite DB
    DATA_INFO *loaded_root = NULL;
    int loaded_count = db_history_load_recent(10, &loaded_root);
    CHECK(loaded_count == 0, "SQLite DB is empty after date folder deletion");
    if (loaded_root != NULL) {
        data_free(loaded_root);
    }

    db_history_close();

    // Clean up temporary database files
    wsprintf(db_file, TEXT("%s\\history.db"), temp_dir);
    DeleteFile(db_file);
    wsprintf(db_file, TEXT("%s\\history.db-shm"), temp_dir);
    DeleteFile(db_file);
    wsprintf(db_file, TEXT("%s\\history.db-wal"), temp_dir);
    DeleteFile(db_file);
    RemoveDirectory(temp_dir);

    printf("PASS: Date folder deletion removes folder and all contained SQLite items\n");
}

static void test_favourite_folder_path(void)
{
    DATA_INFO *root = NULL, *leaf, *existing;
    TCHAR mixed[] = TEXT("Parent/Child\\Grandchild");
    TCHAR reuse[] = TEXT("Parent/Child/Another");
    TCHAR duplicate[] = TEXT("Parent\\Child/Grandchild");
    TCHAR invalid[] = TEXT("Parent//Invalid");
    TCHAR error[BUF_SIZE] = {0};

    leaf = regist_create_folder_path(&root, mixed, error);
    CHECK(leaf != NULL && lstrcmp(leaf->title, TEXT("Grandchild")) == 0,
        "mixed separators create deep favourite path");
    CHECK(root != NULL && root->child != NULL && root->child->child == leaf,
        "favourite path has nested parents");
    existing = root->child;
    CHECK(regist_create_folder_path(&root, reuse, error) != NULL && root->child == existing,
        "favourite path reuses existing parents");
    CHECK(regist_create_folder_path(&root, duplicate, error) == NULL,
        "duplicate favourite path is rejected");
    CHECK(regist_create_folder_path(&root, invalid, error) == NULL && root->next == NULL,
        "empty favourite path segment is rejected without mutation");
    data_free(root);
}

static void test_favourite_insertions(HWND owner)
{
    TCHAR error[BUF_SIZE] = {0};
    POINT pt = {300, 200};
    for (insert_case = 0; insert_case < 8; insert_case++) {
        DATA_INFO *outer = data_create_folder(TEXT("Outer"), error);
        DATA_INFO *nested = data_create_folder(TEXT("Nested"), error);
        DATA_INFO *empty = data_create_folder(TEXT("Empty"), error);
        DATA_INFO *first = data_create_item(TEXT("First & existing"), FALSE, error);
        DATA_INFO *second = data_create_item(TEXT("Second existing"), FALSE, error);
        DATA_INFO *anchor = data_create_item(TEXT("Root anchor"), FALSE, error);
        DATA_INFO *source = data_create_item(TEXT("Clipboard copy"), FALSE, error);
        DATA_INFO **slot, *old_next, *copy;
        BOOL deleted = FALSE, selected;
        HGLOBAL payload = GlobalAlloc(GMEM_MOVEABLE, sizeof(TEXT("Clipboard payload")));
        lstrcpy((TCHAR *)GlobalLock(payload), TEXT("Clipboard payload"));
        GlobalUnlock(payload);
        source->child = data_create_data(CF_UNICODETEXT, TEXT("UNICODETEXT"), payload,
            sizeof(TEXT("Clipboard payload")), FALSE, error);
        source->window_name = alloc_copy(TEXT("Clipboard source window"));
        regist_data.child = outer;
        outer->next = anchor;
        outer->child = nested;
        nested->next = empty;
        nested->child = first;
        first->next = second;
        slot = insert_case == 1 ? &nested->child : insert_case == 3 ? &empty->child :
            insert_case == 4 ? &anchor->next : insert_case == 5 ? &second->next : &first->next;
        if (insert_case == 7) {
            data_free(regist_data.child);
            regist_data.child = NULL;
            slot = &regist_data.child;
        }
        old_next = *slot;
        insert_phase = insert_ticks = 0;
        idle_menu = NULL;
        SetTimer(owner, 78, 100, NULL);
        selected = favorites_show_add_menu(owner, source, pt, &deleted, NULL);
        KillTimer(owner, 78);
        CHECK(!deleted, "adding a favourite preserves the clipboard item");
        if (insert_case == 6) {
            CHECK(!selected && *slot == old_next, "cancel leaves favourites unchanged");
        } else {
            copy = *slot;
            CHECK(selected && copy != NULL && copy != old_next && copy != source,
                "selected destination receives a new favourite");
            if (copy != NULL && copy != old_next) {
                CHECK(copy->next == old_next, "favourite is inserted at chosen position");
                CHECK(lstrcmp(copy->title, source->title) == 0 && copy->window_name == NULL,
                    "inserted favourite retains title without source window name");
                CHECK(copy->child && copy->child->data && copy->child->data != payload,
                    "insertion deep copies clipboard content");
            }
        }
        CHECK(source->next == NULL && source->child->data == payload && source->window_name != NULL,
            "source clipboard item remains unchanged");
        data_free(regist_data.child);
        regist_data.child = NULL;
        data_free(source);
    }
    printf("PASS: Favourite insertion into existing rows, blank slots, nested and empty folders, root and cancellation\n");
}

static void test_bitmap_edit_context(HWND owner)
{
    TCHAR error[BUF_SIZE];
    DATA_INFO *source = data_create_item(TEXT("Bitmap"), FALSE, error);
    HDC screen = GetDC(NULL);
    HBITMAP image = CreateCompatibleBitmap(screen, 16, 16);
    POINT pt = {300, 200};
    BOOL deleted = FALSE, edit = FALSE;
    BOOL selected;
    ReleaseDC(NULL, screen);
    CHECK(source != NULL && image != NULL, "bitmap context fixture created");
    if (source == NULL || image == NULL) {
        if (source != NULL) data_free(source);
        if (image != NULL) DeleteObject(image);
        return;
    }
    source->child = data_create_data(CF_BITMAP, TEXT("BITMAP"), image, 0, FALSE, error);
    CHECK(source->child != NULL && pinned_image_has_bitmap(source),
        "bitmap item is eligible for Edit image");
    if (source->child != NULL) {
        insert_case = 8;
        insert_ticks = 0;
        idle_menu = NULL;
        SetTimer(owner, 78, 100, NULL);
        selected = favorites_show_add_menu(owner, source, pt, &deleted, &edit);
        KillTimer(owner, 78);
        CHECK(selected && edit && !deleted, "history bitmap Edit command selected without deleting");
        edit = FALSE;
        insert_ticks = 0;
        idle_menu = NULL;
        SetTimer(owner, 78, 100, NULL);
        selected = favorites_show_item_menu(owner, source, pt, &deleted, &edit);
        KillTimer(owner, 78);
        CHECK(selected && edit && !deleted, "favourite bitmap Edit command selected without deleting");
        {
            HWND editor = pinned_image_open_from_menu(owner, source);
            PINNED_IMAGE *pin = editor != NULL ? (PINNED_IMAGE *)GetWindowLongPtr(editor, GWLP_USERDATA) : NULL;
            CHECK(pin != NULL && pin->tool == ID_RECT &&
                SendMessage(pin->toolbar, TB_ISBUTTONCHECKED, ID_RECT, 0),
                "menu Edit opens with outline rectangle tool selected");
            if (editor != NULL) DestroyWindow(editor);
        }
    }
    data_free(source);
}

int main(void)
{
    HDESK desktop = CreateDesktop(TEXT("CLCLMenuRegression"), NULL, NULL, 0, GENERIC_ALL, NULL);
    WNDCLASS wc = {0};
    HWND owner;
    TCHAR error[BUF_SIZE];
    MENU_INFO items[2] = {0};
    ACTION_INFO action = {0};
    DATA_INFO *first, *second, *outer, *middle;
    MENU_INFO favourite_items = {0};
    setvbuf(stdout, NULL, _IONBF, 0);
    if (!desktop || !SetThreadDesktop(desktop)) {
        printf("FAIL: isolated desktop unavailable (%lu)\n", GetLastError());
        return 1;
    }
    hInst = GetModuleHandle(NULL);
    InitCommonControls();
    pinned_image_regist(hInst);
    SetDpi(96);
    GetCurrentDirectory(MAX_PATH, work_path);
    lstrcat(work_path, TEXT("\\Release\\menu-test-profile"));
    if (!ini_get_option(error)) { printf("FAIL: option defaults\n"); return 1; }
    format_initialize(error);
    tooltip_regist(hInst);
    hToolTip = tooltip_create(hInst);

    option.menu_show_tooltip = 0;
    option.menu_show_tool_menu = 1;
    option.def_paste_wait = 0;
    wc.lpfnWndProc = test_proc;
    wc.hInstance = hInst;
    wc.lpszClassName = TEXT("CLCLMenuRegressionOwner");
    RegisterClass(&wc);
    owner = CreateWindow(wc.lpszClassName, TEXT("Menu regression"), WS_OVERLAPPED, 0, 0, 100, 100, NULL, NULL, hInst, NULL);
    history_data.type = regist_data.type = TYPE_ROOT;
    {
        RECT anchor = {10, 10, 30, 30};
        RECT old_row = {-10000, -10000, -9990, -9990};
        tooltip_show_image_delay(hToolTip, TEXT("old bitmap preview"), NULL, FALSE,
            10, 10, 0, &anchor, 1, owner, &old_row);
        SendMessage(hToolTip, WM_TIMER, 1, 0);
        CHECK(!IsWindowVisible(hToolTip), "delayed menu preview stays hidden after hover ends");
    }
    {
        int i;
        for (i = 0; i < option.action_cnt; i++)
            if (option.action_info[i].action == ACTION_NEW_SNIP) break;
        CHECK(i < option.action_cnt && option.action_info[i].type == ACTION_TYPE_HOTKEY &&
            option.action_info[i].virtkey == VK_F1 && option.action_info[i].modifiers == 0,
            "New snip action defaults to F1 in shortcut settings");
        if (i < option.action_cnt) {
            MSG posted;
            HWND overlay;
            ZeroMemory(&posted, sizeof(posted));
            action_execute(owner, ACTION_TYPE_HOTKEY, option.action_info[i].id, TRUE);
            CHECK(PeekMessage(&posted, owner, WM_COMMAND, WM_COMMAND, PM_REMOVE),
                "New snip hotkey dispatches the command");
            if (posted.message == WM_COMMAND) DispatchMessage(&posted);
            overlay = FindWindow(SNIP_CLASS, NULL);
            CHECK(overlay != NULL, "New snip hotkey opens selection overlay");
            if (overlay != NULL) SendMessage(overlay, WM_KEYDOWN, VK_ESCAPE, 0);
        }
    }
    {
        MENU_INFO snip_items[5] = {0};
        HMENU menu;
        HWND overlay;
        snip_items[0].content = MENU_CONTENT_VIEWER;
        snip_items[1].content = MENU_CONTENT_POPUP;
        snip_items[2].content = MENU_CONTENT_SEPARATOR;
        snip_items[3].content = MENU_CONTENT_OPTION;
        snip_items[4].content = MENU_CONTENT_EXIT;
        menu = menu_create(owner, snip_items, 5, NULL, NULL);
        CHECK(menu != NULL && GetMenuItemCount(menu) == 6 &&
            GetMenuItemID(menu, 0) == ID_MENUITEM_VIEWER &&
            GetMenuItemID(menu, 1) == ID_MENUITEM_NEW_SNIP &&
            GetSubMenu(menu, 2) != NULL &&
            GetMenuItemID(menu, 4) == ID_MENUITEM_OPTION &&
            GetMenuItemID(menu, 5) == ID_MENUITEM_EXIT,
            "New snip appears below Viewer with no trailing empty rows");
        if (menu != NULL) { menu_destory(menu); menu_free(); }
        CHECK(pinned_image_start_snip(owner), "snip overlay opens");
        overlay = FindWindow(SNIP_CLASS, NULL);
        CHECK(overlay != NULL, "snip overlay is present");
        if (overlay != NULL) {
            SendMessage(overlay, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, 10));
            SendMessage(overlay, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(50, 50));
            SendMessage(overlay, WM_LBUTTONUP, 0, MAKELPARAM(50, 50));
            CHECK(!IsWindow(overlay) && copied_dib_valid && pin_windows != NULL &&
                pin_windows->tool == ID_ARROW, "completed snip copies image and opens Arrow editor");
            pinned_image_close_all();
            if (copied_bitmap != NULL) { DeleteObject(copied_bitmap); copied_bitmap = NULL; }
        }
        CHECK(pinned_image_start_snip(owner), "snip overlay reopens");
        overlay = FindWindow(SNIP_CLASS, NULL);
        if (overlay != NULL) {
            SendMessage(overlay, WM_KEYDOWN, VK_ESCAPE, 0);
            CHECK(!IsWindow(overlay) && pin_windows == NULL,
                "Escape cancels the snip without an editor");
        }
    }
    test_favourite_folder_path();
    {
        TOOL_MENU_INFO saved = tmi;
        tmi.enable = TRUE;
        SendMessage(owner, WM_ENTERMENULOOP, 0, 0);
        SendMessage(owner, WM_ENTERMENULOOP, 1, 0);
        CHECK(!clipboard_to_history(owner) && tmi.enable,
            "clipboard processing defers during nested menu tracking");
        SendMessage(owner, WM_EXITMENULOOP, 1, 0);
        CHECK(native_menu_depth == 1 && !clipboard_to_history(owner),
            "closing context menu still defers clipboard processing");
        SendMessage(owner, WM_EXITMENULOOP, 0, 0);
        CHECK(native_menu_depth == 0, "native menu depth unwinds");
        popup_menu = CreatePopupMenu();
        screenshot_pending = TRUE;
        menu_reopen_requested = TRUE;
        CHECK(!clipboard_to_history(owner) && !menu_reopen_requested && history_data.child == NULL,
            "screenshot cancels menu reopen and defers history mutation until menu teardown");
        screenshot_pending = FALSE;
        DestroyMenu(popup_menu);
        popup_menu = NULL;
        KillTimer(owner, ID_HISTORY_TIMER);
        tmi = saved;
    }

    for (scenario = 0; scenario < 21; scenario++) {
    option.menu_show_tooltip = scenario == 0;
    phase = ticks = 0;
    finished = FALSE;
    landing = NULL;
    idle_menu = NULL;
    clipboard_selection = NULL;
    folder = data_create_folder(TEXT("Test date"), error);
    first = data_create_item(TEXT("First disposable item"), FALSE, error);
    second = data_create_item(TEXT("Second disposable item"), FALSE, error);
    first->child = data_create_data(CF_UNICODETEXT, TEXT("UNICODETEXT"), NULL, 0, FALSE, error);
    second->child = data_create_data(CF_UNICODETEXT, TEXT("UNICODETEXT"), NULL, 0, FALSE, error);
    if (scenario == 0) {
        HDC screen = GetDC(NULL);
        HBITMAP bitmap = CreateCompatibleBitmap(screen, 640, 480);
        ReleaseDC(NULL, screen);
        data_free(first->child); data_free(second->child);
        first->child = data_create_data(CF_BITMAP, TEXT("BITMAP"), clone_bitmap(bitmap, NULL, NULL), 0, TRUE, error);
        second->child = data_create_data(CF_BITMAP, TEXT("BITMAP"), clone_bitmap(bitmap, NULL, NULL), 0, TRUE, error);
        DeleteObject(bitmap);
        option.menu_show_bitmap = 1;
    }
    first->next = second;
    folder->child = first;
    ZeroMemory(items, sizeof(items));
    if (scenario < 2 || scenario >= 8) {
        history_data.child = folder;
        items[0].content = MENU_CONTENT_HISTORY;
        open_steps = 1;
        if (scenario == 17) {
            folder->next = data_create_item(TEXT("Root clipboard item"), FALSE, error);
            folder->next->child = data_create_data(CF_UNICODETEXT, TEXT("UNICODETEXT"), NULL, 0, FALSE, error);
        } else if (scenario == 16 || scenario == 18) {
            folder->next = data_create_folder(TEXT("Second date"), error);
            folder->next->child = data_create_item(TEXT("Third disposable item"), FALSE, error);
            folder->next->child->child = data_create_data(CF_UNICODETEXT, TEXT("UNICODETEXT"), NULL, 0, FALSE, error);
        } else if (scenario == 9 || scenario == 10 || scenario == 14 || scenario == 15) {
            folder->child = NULL;
            data_free(folder);
            folder = &history_data;
            folder->child = first;
            phase = 1;
        }
    } else {
        favourite_items.content = MENU_CONTENT_REGIST;
        items[0].content = MENU_CONTENT_POPUP;
        items[0].title = TEXT("Favourites");
        items[0].mi = &favourite_items;
        items[0].mi_cnt = 1;
        if (scenario == 2) {
            folder->child = NULL;
            data_free(folder);
            folder = &regist_data;
            folder->child = first;
            open_steps = 1;
        } else {
            outer = data_create_folder(TEXT("Outer"), error);
            middle = data_create_folder(TEXT("Middle"), error);
            regist_data.child = outer;
            outer->child = middle;
            middle->child = folder;
            landing = middle;
            if (scenario != 5) {
                folder->next = data_create_folder(TEXT("Keep sibling"), error);
            }
            open_steps = scenario >= 6 ? 3 : 4;
        }
    }
    items[1].content = MENU_CONTENT_CANCEL;
    items[1].title = TEXT("Cancel");
    if (scenario == 19 || scenario == 20) {
        favourite_items.content = MENU_CONTENT_REGIST;
        items[1].content = MENU_CONTENT_POPUP;
        items[1].title = TEXT("Favourites");
        items[1].mi = &favourite_items;
        items[1].mi_cnt = 1;
        regist_data.child = data_create_item(TEXT("Favourite clipboard item"), FALSE, error);
        regist_data.child->child = data_create_data(CF_UNICODETEXT, TEXT("UNICODETEXT"), NULL, 0, FALSE, error);
    }
    action.menu_info = items;
    action.menu_cnt = 2;
    action.paste = (scenario == 11 || scenario == 14) ? 1 : 0;
    SetTimer(owner, 77, 150, NULL);
    printf("Running scenario %d\n", scenario);
    show_popup_menu(owner, &action, FALSE, FALSE);
    CHECK(finished, "popup did not dismiss during Delete");
    if (scenario == 11 || scenario == 14) {
        CHECK(clipboard_selection == switch_target, "LMB selects the newly clicked clipboard item");
        CHECK(folder->child->next == switch_target, "LMB preserves both clipboard items");
    } else if (scenario == 16) {
        CHECK(clipboard_selection == NULL, "LMB on date folder did not paste");
    }
    CHECK(menu_context_hits == NULL, "context hit targets released");
    KillTimer(owner, 77);
    data_free(history_data.child);
    data_free(regist_data.child);
    history_data.child = regist_data.child = NULL;
    }
    test_favourite_insertions(owner);
    test_bitmap_edit_context(owner);
    test_pinned_image_edit_core(owner);
    DestroyWindow(hToolTip);
    hToolTip = NULL;
    DestroyWindow(owner);
    test_bitmap_serialization();
    test_sqlite_multiple_bitmap_persistence();
    test_date_folder_deletion();
    printf("%s: 21 native-menu scenarios + pinned image editor + bitmap serialization + SQLite multiple bitmap persistence + date folder deletion\n", failures ? "FAILED" : "PASS");
    return failures ? 1 : 0;
}
