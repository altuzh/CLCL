/* Native popup-loop regression. No user profile, clipboard, or database is opened.
 * Only cursor I/O is recorded: Windows cannot move the pointer on a non-input
 * desktop. Menu creation, drawing, tracking, hooks and dialogs are real Win32. */
#include <windows.h>
#include <stdio.h>
#include <winsqlite/winsqlite3.h>
static POINT test_cursor = {300, 200};
static BOOL test_get_cursor(LPPOINT pt) { *pt = test_cursor; return TRUE; }
static BOOL test_set_cursor(int x, int y) { test_cursor.x = x; test_cursor.y = y; return TRUE; }
#define GetCursorPos test_get_cursor
#define SetCursorPos test_set_cursor
#define menu_show_align test_menu_show_align
#define save_regist unused_save_regist
#define pinned_image_clipboard_is_screenshot test_screenshot_pending
#include "../main.c"
extern void ini_set_language(const TCHAR *locale_name);
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
static int editor_window_walks;
static HWND test_editor_get_window(HWND window, UINT command)
{
    if (command == GW_HWNDNEXT) editor_window_walks++;
    return GetWindow(window, command);
}
#define GetWindow test_editor_get_window
static BOOL fail_editor_topmost, fail_editor_placement;
static BOOL test_editor_set_window_pos(HWND window, HWND after, int x, int y, int w, int h, UINT flags)
{
    if ((fail_editor_topmost && after == HWND_TOPMOST) ||
        (fail_editor_placement && (flags & SWP_NOZORDER))) return FALSE;
    return SetWindowPos(window, after, x, y, w, h, flags);
}
#define SetWindowPos test_editor_set_window_pos
#include "../PinnedImage.c"
#undef SetWindowPos
#undef GetWindow

static COLORREF bitmap_pixel(HBITMAP bitmap, int x, int y)
{
    HDC dc = CreateCompatibleDC(NULL);
    HBITMAP old = SelectObject(dc, bitmap);
    COLORREF color = GetPixel(dc, x, y);
    SelectObject(dc, old);
    DeleteDC(dc);
    return color;
}
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

static DWORD toolbar_icon_checksum(HIMAGELIST icons, int index)
{
    BITMAPINFO info = {0};
    HDC screen = GetDC(NULL), dc = CreateCompatibleDC(screen);
    HBITMAP bitmap, previous;
    DWORD *pixels = NULL, hash = 2166136261u;
    int width, height, i;
    ImageList_GetIconSize(icons, &width, &height);
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, (void **)&pixels, NULL, 0);
    if (bitmap == NULL) { DeleteDC(dc); ReleaseDC(NULL, screen); return 0; }
    previous = SelectObject(dc, bitmap);
    PatBlt(dc, 0, 0, width, height, BLACKNESS);
    if (ImageList_Draw(icons, index, dc, 0, 0, ILD_NORMAL))
        for (i = 0; i < width * height; i++) hash = (hash ^ pixels[i]) * 16777619u;
    else hash = 0;
    SelectObject(dc, previous);
    DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(NULL, screen);
    return hash;
}

static void test_frame_annotations(HBITMAP source)
{
    PINNED_IMAGE pin = {0};
    PIN_ARTIFACT *a, *b, *lens;
    PIN_ARTIFACT moved;
    RECT bounds, destination;
    COLORREF single;
    HDC dc = CreateCompatibleDC(NULL);
    HBITMAP old;
    pin.original = clone_bitmap(source, NULL, NULL);
    pin.bitmap = clone_bitmap(source, NULL, NULL);
    pin.width = 80; pin.height = 40;
    pin.current_color = RGB(255, 190, 0);
    SetRect(&pin.image_rect, 0, 0, 80, 40);
    old = SelectObject(dc, pin.original);
    SetPixelV(dc, 15, 15, RGB(0, 0, 0));
    SetPixelV(dc, 14, 14, RGB(30, 30, 30));
    SetPixelV(dc, 16, 16, RGB(190, 35, 90));
    SelectObject(dc, old);
    a = add_artifact(&pin, ID_HIGHLIGHT, (POINT){5, 5}, 2);
    b = add_artifact(&pin, ID_HIGHLIGHT, (POINT){20, 5}, 2);
    CHECK(a != NULL && b != NULL, "highlight frame artifacts allocate");
    if (a == NULL || b == NULL) goto cleanup;
    a->box = (RECT){5, 5, 40, 30}; b->box = (RECT){20, 5, 60, 30};
    CHECK(rebuild_artifacts(&pin), "highlight frames render");
    single = bitmap_pixel(pin.bitmap, 10, 20);
    CHECK(single == a->color &&
        bitmap_pixel(pin.bitmap, 30, 20) == single && bitmap_pixel(pin.bitmap, 50, 20) == single,
        "highlight background is solid and overlapping frames keep the selected color");
    CHECK(bitmap_pixel(pin.bitmap, 15, 15) == RGB(0, 0, 0) &&
        bitmap_pixel(pin.bitmap, 14, 14) == RGB(30, 30, 30) &&
        bitmap_pixel(pin.bitmap, 16, 16) == RGB(190, 35, 90) &&
        bitmap_pixel(pin.bitmap, 70, 20) == RGB(255, 255, 255),
        "highlight preserves black, gray and colored foreground exactly");
    b->color = RGB(30, 90, 220);
    CHECK(rebuild_artifacts(&pin) && bitmap_pixel(pin.bitmap, 30, 20) == b->color &&
        bitmap_pixel(pin.bitmap, 50, 20) == b->color,
        "last solid highlight color owns the overlap");
    b->deleted = TRUE;
    a->stroke = 12;
    CHECK(rebuild_artifacts(&pin) && bitmap_pixel(pin.bitmap, 5, 20) == a->color &&
        bitmap_pixel(pin.bitmap, 6, 20) == single && bitmap_pixel(pin.bitmap, 4, 20) == RGB(255, 255, 255),
        "frame highlighter keeps a one-pixel border even with a wide stroke selected");
    a->stroke = 2;
    b->deleted = FALSE;
    a->tool = b->tool = ID_HIGHLIGHT_BLOCK;
    b->color = a->color;
    CHECK(rebuild_artifacts(&pin) && bitmap_pixel(pin.bitmap, 5, 20) == single &&
        bitmap_pixel(pin.bitmap, 30, 20) == single && bitmap_pixel(pin.bitmap, 59, 20) == single &&
        bitmap_pixel(pin.bitmap, 4, 20) == RGB(255, 255, 255) &&
        bitmap_pixel(pin.bitmap, 60, 20) == RGB(255, 255, 255) && bitmap_pixel(pin.bitmap, 15, 15) == RGB(0, 0, 0) &&
        bitmap_pixel(pin.bitmap, 14, 14) == RGB(30, 30, 30) && bitmap_pixel(pin.bitmap, 16, 16) == RGB(190, 35, 90),
        "highlight blocks fill solid edge to edge without borders or loss of foreground color");
    CHECK(artifact_hit(a, (POINT){10, 20}, 0), "highlight block supports selection");
    CHECK(begin_change(&pin), "highlight block deletion records undo");
    b->deleted = TRUE;
    CHECK(rebuild_artifacts(&pin) && bitmap_pixel(pin.bitmap, 50, 20) == RGB(255, 255, 255),
        "deleting highlight block restores its background");
    swap_history(&pin, TRUE);
    CHECK(bitmap_pixel(pin.bitmap, 50, 20) == single, "undo restores highlight block");
    a = pin.artifacts; b = a->next;
    a->tool = ID_ROUGH_HIGHLIGHT; b->deleted = TRUE;
    CHECK(rebuild_artifacts(&pin) && bitmap_pixel(pin.bitmap, 10, 20) == a->color &&
        bitmap_pixel(pin.bitmap, 16, 16) == RGB(190, 35, 90) &&
        bitmap_pixel(pin.bitmap, 14, 14) == RGB(30, 30, 30) &&
        bitmap_pixel(pin.bitmap, 15, 15) == RGB(0, 0, 0),
        "rough highlight fills solid and preserves colored and gray foreground exactly");
    {
        int x, painted = 0, clear = 0;
        COLORREF center = bitmap_pixel(pin.bitmap, 10, 20);
        for (x = 8; x < 37; x++) {
            COLORREF pixel = bitmap_pixel(pin.bitmap, x, 5);
            if (pixel == RGB(255, 255, 255)) clear++;
            else painted++;
        }
        CHECK(painted && clear, "rough highlight has an uneven edge within its rectangle");
        CHECK(rebuild_artifacts(&pin) && bitmap_pixel(pin.bitmap, 10, 20) == center,
            "rough highlight replay is stable");
    }
    CHECK(artifact_hit(a, (POINT){10, 20}, 0), "rough highlight supports selection");
    {
        PIN_ARTIFACT *inverted = add_artifact(&pin, ID_INVERT, (POINT){0, 0}, 1);
        int tools[] = { ID_HIGHLIGHT, ID_HIGHLIGHT_BLOCK, ID_ROUGH_HIGHLIGHT };
        int i, order;
        CHECK(inverted != NULL, "highlight inversion fixture allocates");
        if (inverted != NULL) {
            inverted->box = (RECT){0, 0, 80, 40};
            for (order = 0; order < 2; order++) {
                if (order) { pin.artifacts = inverted; inverted->next = a; b->next = NULL; }
                for (i = 0; i < 3; i++) {
                    a->tool = tools[i];
                    CHECK(rebuild_artifacts(&pin) && bitmap_pixel(pin.bitmap, 10, 20) == a->color &&
                        bitmap_pixel(pin.bitmap, 15, 15) == RGB(255, 255, 255) &&
                        bitmap_pixel(pin.bitmap, 14, 14) == RGB(225, 225, 225) &&
                        bitmap_pixel(pin.bitmap, 16, 16) == (RGB(190, 35, 90) ^ 0x00ffffff) &&
                        bitmap_pixel(pin.bitmap, 70, 20) == RGB(0, 0, 0),
                        "both tool orders preserve highlight color and invert foreground");
                    if (tools[i] == ID_HIGHLIGHT)
                        CHECK(bitmap_pixel(pin.bitmap, 5, 20) == a->color,
                            "inversion preserves the highlight frame border color");
                }
            }
            pin.artifacts = a; b->next = inverted; inverted->next = NULL;
            CHECK(begin_change(&pin), "inversion removal records undo");
            inverted->deleted = TRUE;
            CHECK(rebuild_artifacts(&pin) && bitmap_pixel(pin.bitmap, 10, 20) == a->color &&
                bitmap_pixel(pin.bitmap, 15, 15) == RGB(0, 0, 0) &&
                bitmap_pixel(pin.bitmap, 70, 20) == RGB(255, 255, 255),
                "removing inversion restores foreground while retaining highlights");
            swap_history(&pin, TRUE);
            CHECK(bitmap_pixel(pin.bitmap, 10, 20) == single &&
                bitmap_pixel(pin.bitmap, 15, 15) == RGB(255, 255, 255),
                "undo restores inversion without changing highlight color");
            swap_history(&pin, FALSE);
            a = pin.artifacts; b = a->next;
        }
    }
    {
        HBITMAP preview = clone_bitmap(source, NULL, NULL);
        HBITMAP previous = SelectObject(dc, preview);
        int x, painted = 0, clear = 0;
        pin.start = (POINT){5, 5}; pin.last = (POINT){40, 30};
        pin.tool = ID_HIGHLIGHT;
        draw_highlight_preview(&pin, dc);
        CHECK(GetPixel(dc, 10, 20) != pin.current_color && GetPixel(dc, 10, 20) != RGB(255, 255, 255) &&
            GetPixel(dc, 4, 20) == RGB(255, 255, 255),
            "frame highlighter drag preview transparently fills the rectangle");
        PatBlt(dc, 0, 0, 80, 40, WHITENESS);
        pin.tool = ID_HIGHLIGHT_BLOCK;
        draw_highlight_preview(&pin, dc);
        CHECK(GetPixel(dc, 5, 5) != pin.current_color && GetPixel(dc, 5, 5) != RGB(255, 255, 255) &&
            GetPixel(dc, 10, 20) != pin.current_color && GetPixel(dc, 10, 20) != RGB(255, 255, 255) &&
            GetPixel(dc, 4, 20) == RGB(255, 255, 255),
            "block highlight drag preview transparently fills a straight-edged rectangle");
        PatBlt(dc, 0, 0, 80, 40, WHITENESS);
        pin.tool = ID_ROUGH_HIGHLIGHT;
        draw_highlight_preview(&pin, dc);
        for (x = 8; x < 37; x++) {
            if (GetPixel(dc, x, 6) == RGB(255, 255, 255)) clear++;
            else painted++;
        }
        CHECK(GetPixel(dc, 10, 20) != pin.current_color && GetPixel(dc, 10, 20) != RGB(255, 255, 255) && painted && clear,
            "rough highlight drag preview transparently fills a jagged-edged rectangle");
        SelectObject(dc, previous);
        DeleteObject(preview);
    }
    b->deleted = FALSE;
    a->tool = b->tool = ID_SPOTLIGHT;
    a->box = (RECT){-10, -10, 30, 30}; b->box = (RECT){20, 5, 90, 35};
    CHECK(rebuild_artifacts(&pin), "multiple spotlights clip to the canvas");
    CHECK(bitmap_pixel(pin.bitmap, 10, 20) == RGB(255, 255, 255) &&
        bitmap_pixel(pin.bitmap, 25, 20) == RGB(255, 255, 255) &&
        bitmap_pixel(pin.bitmap, 60, 20) == RGB(255, 255, 255) &&
        bitmap_pixel(pin.bitmap, 60, 38) == RGB(191, 191, 191),
        "spotlight union preserves all selected regions and dims background once by 25 percent");
    CHECK(begin_change(&pin), "spotlight removal records undo");
    b->deleted = TRUE;
    CHECK(rebuild_artifacts(&pin) && bitmap_pixel(pin.bitmap, 60, 20) == RGB(191, 191, 191),
        "removing one spotlight recomputes the clear area");
    swap_history(&pin, TRUE);
    CHECK(bitmap_pixel(pin.bitmap, 60, 20) == RGB(255, 255, 255), "undo restores spotlight union");
    swap_history(&pin, FALSE);
    CHECK(bitmap_pixel(pin.bitmap, 60, 20) == RGB(191, 191, 191), "redo removes only that spotlight");
    free_artifacts(pin.artifacts); pin.artifacts = NULL;
    lens = add_artifact(&pin, ID_MAGNIFY, (POINT){10, 10}, 1);
    CHECK(lens != NULL, "magnify artifact allocates");
    if (lens == NULL) goto cleanup;
    lens->end = (POINT){20, 20};
    {
        int width, height;
        lens->magnified_source = crop_bitmap(pin.original, (RECT){10, 10, 20, 20}, &width, &height);
    }
    lens->box = magnify_box(&pin, (RECT){10, 10, 20, 20});
    destination = lens->box;
    CHECK(destination.right - destination.left == 20 && destination.bottom - destination.top == 20 &&
        destination.left > 20 && destination.right <= pin.width && destination.bottom <= pin.height,
        "magnify places a 2x inset beside its source within the canvas");
    CHECK(rebuild_artifacts(&pin) &&
        bitmap_pixel(pin.bitmap, destination.left + 10, destination.top + 10) == RGB(0, 0, 0) &&
        bitmap_pixel(pin.bitmap, destination.left + 11, destination.top + 11) == RGB(0, 0, 0) &&
        bitmap_pixel(pin.bitmap, 15, 15) == RGB(0, 0, 0),
        "magnify enlarges source detail and preserves source interior");
    bounds = artifact_bounds(lens);
    CHECK(EqualRect(&bounds, &destination) && artifact_hit(lens, (POINT){destination.left + 10, destination.top + 10}, 0),
        "magnify inset is selectable");
    moved = *lens;
    OffsetRect(&destination, 12, 0);
    set_artifact_geometry(&moved, lens, bounds, destination);
    CHECK(EqualRect(&moved.box, &destination) && moved.start.x == 10 && moved.end.x == 20,
        "moving magnify inset keeps its source anchored");
    destination.right += 5;
    set_artifact_geometry(&moved, lens, bounds, destination);
    CHECK(EqualRect(&moved.box, &destination) && moved.start.y == 10 && moved.end.y == 20,
        "resizing magnify changes inset size without changing source");
    offset_artifact(&moved, 2, 2);
    CHECK(moved.start.x == 10 && moved.end.y == 20, "duplicating magnify preserves source coordinates");
    destination = magnify_box(&pin, (RECT){70, 30, 80, 40});
    CHECK(destination.left >= 0 && destination.right <= 70 && destination.bottom <= 40,
        "magnify near right edge uses available space on the left");
    CHECK(begin_change(&pin), "magnify removal records undo");
    lens->deleted = TRUE;
    CHECK(rebuild_artifacts(&pin) && bitmap_pixel(pin.bitmap, bounds.left + 10, bounds.top + 10) == RGB(255, 255, 255),
        "deleting magnify restores covered content");
    swap_history(&pin, TRUE);
    CHECK(bitmap_pixel(pin.bitmap, bounds.left + 10, bounds.top + 10) == RGB(0, 0, 0), "undo restores magnify inset");
    {
        int width, height;
        HBITMAP cropped = crop_bitmap(pin.original, (RECT){28, 0, 80, 40}, &width, &height);
        CHECK(cropped != NULL, "magnify crop base allocates");
        if (cropped != NULL) {
            DeleteObject(pin.original); pin.original = cropped;
            transform_artifacts(&pin, 1, 1, 1, 1, 28, 0);
            pin.width = width; pin.height = height;
            CHECK(rebuild_artifacts(&pin) && bitmap_pixel(pin.bitmap, bounds.left - 28 + 10, bounds.top + 10) == RGB(0, 0, 0),
                "magnify retains captured detail after its source is cropped out");
        }
    }
cleanup:
    DeleteDC(dc);
    clear_edit_stack(pin.undo, pin.undo_base, pin.undo_artifacts, &pin.undo_count);
    clear_edit_stack(pin.redo, pin.redo_base, pin.redo_artifacts, &pin.redo_count);
    free_artifacts(pin.artifacts);
    DeleteObject(pin.original); DeleteObject(pin.bitmap);
}

static void test_dimension_annotations(HWND owner)
{
    HDC screen = GetDC(NULL), dc = CreateCompatibleDC(screen);
    HBITMAP source = CreateCompatibleBitmap(screen, 320, 240), old;
    DATA_INFO data = {0};
    HWND editor;
    PINNED_IMAGE *pin;
    POINT starts[] = {{20, 120}, {300, 120}, {160, 20}, {30, 30}};
    POINT ends[] = {{300, 120}, {20, 120}, {160, 220}, {290, 210}};
    int i;
    CHECK(source != NULL, "dimension fixture allocates");
    if (source == NULL) { DeleteDC(dc); ReleaseDC(NULL, screen); return; }
    old = SelectObject(dc, source);
    PatBlt(dc, 0, 0, 320, 240, WHITENESS);
    SelectObject(dc, old); DeleteDC(dc); ReleaseDC(NULL, screen);
    data.type = TYPE_DATA; data.format = CF_BITMAP;
    data.format_name = TEXT("BITMAP"); data.data = source;
    editor = open_image_editor(owner, &data, ID_DIMENSION);
    pin = editor != NULL ? (PINNED_IMAGE *)GetWindowLongPtr(editor, GWLP_USERDATA) : NULL;
    CHECK(pin != NULL, "dimension editor opens");
    if (pin == NULL) { DeleteObject(source); return; }
    CHECK(GetMenuState(GetSubMenu(pin->menu, 2), ID_DIMENSION, MF_BYCOMMAND) != (UINT)-1 &&
        SendMessage(pin->toolbar, TB_COMMANDTOINDEX, ID_DIMENSION, 0) >= 0 &&
        editor_tool_cursor(pin) == LoadCursor(NULL, IDC_CROSS),
        "Dimension is available in menu and toolbar with a crosshair");
    CHECK(pin->current_color == RGB(0, 0, 0), "Dimension starts with black");
    SendMessage(editor, WM_COMMAND, ID_COLOR_BLUE, 0);
    CHECK(pin->current_color == colors[1], "Dimension still allows an explicit color choice");
    CHECK(ensure_drawing_resolution(pin), "dimension uses the editor drawing resolution");
    for (i = 0; i < ARRAYSIZE(starts); i++) {
        starts[i].x = MulDiv(starts[i].x, pin->width, 320);
        starts[i].y = MulDiv(starts[i].y, pin->height, 240);
        ends[i].x = MulDiv(ends[i].x, pin->width, 320);
        ends[i].y = MulDiv(ends[i].y, pin->height, 240);
    }
    for (i = 0; i < ARRAYSIZE(starts); i++) {
        POINT from = image_to_client(pin, starts[i]), to = image_to_client(pin, ends[i]);
        PIN_ARTIFACT *item;
        RECT box, first_line, bounds;
        POINT wings[4];
        int before = pin->undo_count, j;
        SendMessage(editor, WM_COMMAND, ID_COLOR_BLUE, 0);
        SendMessage(editor, WM_COMMAND, ID_DIMENSION, 0);
        CHECK(pin->current_color == RGB(0, 0, 0), "selecting Dimension defaults to black");
        SendMessage(editor, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(from.x, from.y));
        SendMessage(editor, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(to.x, to.y));
        SendMessage(editor, WM_LBUTTONUP, 0, MAKELPARAM(to.x, to.y));
        CHECK(pin->text_edit != NULL && pin->undo_count == before &&
            pin->text_edit_font != NULL && pin->text_edit_brush != NULL,
            "dimension drag opens shared callout input without an undo step");
        if (pin->text_edit == NULL) continue;
        CHECK((GetWindowLongPtr(pin->text_edit, GWL_STYLE) & ES_CENTER) != 0 &&
            (GetWindowLongPtr(pin->text_edit, GWL_EXSTYLE) & WS_EX_CLIENTEDGE) != 0,
            "dimension input keeps callout styling and centers text");
        SetWindowText(pin->text_edit, TEXT("100 mm"));
        first_line = pin->selection;
        SendMessage(pin->text_edit, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
        SendMessage(pin->text_edit, WM_CHAR, VK_RETURN, 0);
        CHECK(pin->selection.bottom - pin->selection.top > first_line.bottom - first_line.top,
            "dimension input grows for an empty new line");
        SetWindowText(pin->text_edit, TEXT("100 mm"));
        box = pin->selection;
        CHECK(abs(box.left + box.right - starts[i].x - ends[i].x) <= 1 &&
            abs(box.top + box.bottom - starts[i].y - ends[i].y) <= 1,
            "dimension note stays centered for horizontal, reversed, vertical and diagonal drags");
        SendMessage(pin->text_edit, WM_KEYDOWN, VK_RETURN, 0);
        for (item = pin->artifacts; item != NULL && item->next != NULL; item = item->next) {}
        CHECK(item != NULL && item->tool == ID_DIMENSION && EqualRect(&item->box, &box) &&
            abs(item->start.x - starts[i].x) <= 1 && abs(item->start.y - starts[i].y) <= 1 &&
            abs(item->end.x - ends[i].x) <= 1 && abs(item->end.y - ends[i].y) <= 1 &&
            pin->undo_count == before + 1 && pin->text_edit == NULL &&
            pin->text_edit_font == NULL && pin->text_edit_brush == NULL,
            "Enter saves both dimension endpoints and note in one undo step");
        if (item == NULL) continue;
        dimension_wings(item->start, item->end, item->stroke, wings);
        bounds = artifact_bounds(item);
        for (j = 0; j < 4; j++) {
            CHECK(artifact_hit(item, wings[j], 0) && PtInRect(&bounds, wings[j]),
                "both dimension arrowheads are selectable and within selection bounds");
        }
        CHECK(artifact_hit(item, (POINT){(box.left + box.right) / 2, (box.top + box.bottom) / 2}, 0),
            "dimension note is selectable");
        if (i == 0) {
            POINT a = {(item->start.x + wings[2].x) / 2, (item->start.y + wings[2].y) / 2};
            POINT b = {(item->end.x + wings[0].x) / 2, (item->end.y + wings[0].y) / 2};
            CHECK(bitmap_pixel(pin->bitmap, a.x, a.y) == item->color &&
                bitmap_pixel(pin->bitmap, b.x, b.y) == item->color &&
                bitmap_pixel(pin->bitmap, item->start.x + (item->end.x - item->start.x) / 16, item->start.y) == item->color &&
                bitmap_pixel(pin->bitmap, box.left + max(2, item->stroke), (box.top + box.bottom) / 2) == RGB(255, 255, 245) &&
                bitmap_pixel(pin->bitmap, (box.left + box.right) / 2, box.top) == item->color &&
                bitmap_pixel(pin->bitmap, box.left, box.top) == RGB(255, 255, 255),
                "dimension renders two arrowheads and a filled note with a colored rounded frame");
        }
        swap_history(pin, TRUE);
        CHECK(pin->undo_count == before && bitmap_pixel(pin->bitmap, 30, 120) == RGB(255, 255, 255),
            "dimension undo removes the whole annotation");
        swap_history(pin, FALSE);
        CHECK(pin->undo_count == before + 1, "dimension redo restores the annotation");
        item = pin->artifacts;
        SendMessage(editor, WM_COMMAND, ID_SELECT, 0);
        item->selected = TRUE; pin->selected_artifact = item;
        SendMessage(editor, WM_LBUTTONDBLCLK, 0, 0);
        CHECK(pin->text_edit != NULL && pin->editing_artifact == item,
            "double-click reopens dimension note in the shared editor");
        if (pin->text_edit != NULL) SetWindowText(pin->text_edit, TEXT("200 mm"));
        SendMessage(editor, WM_FINISH_TEXT, TRUE, 0);
        CHECK(lstrcmp(item->text, TEXT("200 mm")) == 0 &&
            abs(item->box.left + item->box.right - item->start.x - item->end.x) <= 1 &&
            abs(item->box.top + item->box.bottom - item->start.y - item->end.y) <= 1,
            "re-editing a dimension keeps the note centered and endpoints intact");
        box = item->box;
        offset_artifact(item, 3, 4);
        CHECK(item->box.left == box.left + 3 && item->box.top == box.top + 4 &&
            abs(item->start.x - starts[i].x - 3) <= 1 && abs(item->end.y - ends[i].y - 4) <= 1,
            "moving a dimension moves its line and note together");
        swap_history(pin, TRUE); swap_history(pin, TRUE);
    }
    {
        POINT from = image_to_client(pin, (POINT){20, 120});
        POINT to = image_to_client(pin, (POINT){300, 120});
        int before = pin->undo_count;
        SendMessage(editor, WM_COMMAND, ID_DIMENSION, 0);
        SendMessage(editor, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(from.x, from.y));
        SendMessage(editor, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(to.x, to.y));
        UpdateWindow(editor);
        SendMessage(editor, WM_LBUTTONUP, 0, MAKELPARAM(from.x, from.y));
        CHECK(GetUpdateRect(editor, NULL, FALSE), "returning to the start clears the dimension preview");
        CHECK(pin->text_edit == NULL && pin->undo_count == before,
            "zero-length dimension creates no annotation or undo step");
        SendMessage(editor, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(from.x, from.y));
        SendMessage(editor, WM_LBUTTONUP, 0, MAKELPARAM(to.x, to.y));
        if (pin->text_edit != NULL) {
            SetWindowText(pin->text_edit, TEXT("cancel"));
            SendMessage(pin->text_edit, WM_KEYDOWN, VK_ESCAPE, 0);
        }
        CHECK(pin->text_edit == NULL && pin->undo_count == before,
            "Escape cancels a dimension without changing history");
    }
    DestroyWindow(editor); DeleteObject(source);
}

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
    test_frame_annotations(source);
    {
        BITMAPINFO info = {0};
        DWORD *pixels = NULL;
        DIBSECTION rotated_info;
        HDC display = GetDC(NULL);
        HBITMAP sample, rotated;
        int i;
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = 3;
        info.bmiHeader.biHeight = -2;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        sample = CreateDIBSection(display, &info, DIB_RGB_COLORS, (void **)&pixels, NULL, 0);
        ReleaseDC(NULL, display);
        if (sample != NULL) for (i = 0; i < 6; i++) pixels[i] = i + 1;
        rotated = sample != NULL ? rotate_bitmap_clockwise(sample) : NULL;
        CHECK(rotated != NULL && GetObject(rotated, sizeof(rotated_info), &rotated_info) == sizeof(rotated_info) &&
            rotated_info.dsBm.bmWidth == 2 && rotated_info.dsBm.bmHeight == 3 &&
            ((DWORD *)rotated_info.dsBm.bmBits)[0] == 4 && ((DWORD *)rotated_info.dsBm.bmBits)[1] == 1 &&
            ((DWORD *)rotated_info.dsBm.bmBits)[4] == 6 && ((DWORD *)rotated_info.dsBm.bmBits)[5] == 3,
            "clockwise rotation swaps dimensions and preserves pixel positions");
        if (rotated != NULL) DeleteObject(rotated);
        if (sample != NULL) DeleteObject(sample);
    }
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
        PINNED_IMAGE sample = {0};
        PIN_ARTIFACT item = {0};
        HDC image_dc = CreateCompatibleDC(NULL);
        HBITMAP old;
        RECT bounds;
        sample.bitmap = clone_bitmap(source, NULL, NULL);
        sample.width = 80; sample.height = 40;
        sample.tool = ID_FRAME_ARROW;
        sample.stroke_width = 3;
        sample.current_color = RGB(220, 32, 32);
        sample.start = (POINT){10, 20};
        SetRect(&sample.image_rect, 0, 0, 80, 40);
        finish_shape(&sample, (POINT){70, 20});
        old = SelectObject(image_dc, sample.bitmap);
        CHECK(GetPixel(image_dc, 10, 20) == sample.current_color &&
            GetPixel(image_dc, 35, 20) == RGB(255, 255, 255),
            "Frame arrow draws a colored outline with a clear interior");
        SelectObject(image_dc, old);
        DeleteDC(image_dc);
        DeleteObject(sample.bitmap);
        item.tool = ID_FRAME_ARROW;
        item.start = sample.start;
        item.end = (POINT){70, 20};
        item.stroke = 3;
        bounds = artifact_bounds(&item);
        CHECK(bounds.top < 20 && bounds.bottom > 20 &&
            artifact_hit(&item, (POINT){10, 20}, 2) &&
            !artifact_hit(&item, (POINT){35, 20}, 2),
            "Frame arrow outline supports selection and erasing");
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
            pinned_image_bind_history(item, 1006, TRUE);
            CHECK(pin_windows != before && pin_windows->source_item == item &&
                pin_windows->source_data == item->child,
                "automatic screenshot editor binds to its saved history item");
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
        DATA_INFO first = {0}, second = {0};
        DATA_INFO *saved_head = history_data.child, *history_item;
        HBITMAP next = scale_bitmap(source, 40, 20);
        HWND reused;
        TCHAR error[BUF_SIZE];
        first.type = second.type = TYPE_DATA;
        first.format = second.format = CF_BITMAP;
        first.format_name = second.format_name = TEXT("BITMAP");
        first.data = source;
        second.data = next;
        reused = pinned_image_open(owner, &second);
        CHECK(editor != NULL && reused == editor && pin_windows->next == NULL &&
            pin_windows->width == 40 && pin_windows->height == 20,
            "opening another bitmap reuses the editor window and replaces its canvas");
        reused = pinned_image_open(owner, &first);
        CHECK(editor != NULL && reused == editor && pin_windows->width == 80 && pin_windows->height == 40,
            "reused editor loads the next bitmap without creating a second window");
        {
            int missing;
            int saved_max = option.history_max;
            option.history_max = 0;
            for (missing = 0; missing < 2; missing++) {
                DATA_INFO *recovered;
                HDC dc = CreateCompatibleDC(NULL);
                HBITMAP old = SelectObject(dc, pin_windows->bitmap);
                SetPixelV(dc, 0, 0, RGB(123, 45, 67));
                SelectObject(dc, old);
                DeleteDC(dc);
                pin_windows->dirty = TRUE;
                // Model either an unbound snip or source nodes already removed from history.
                pin_windows->source_item = missing ? (DATA_INFO *)(UINT_PTR)1 : NULL;
                pin_windows->source_data = missing ? (DATA_INFO *)(UINT_PTR)2 : NULL;
                reused = pinned_image_open(owner, &second);
                recovered = history_data.child;
                CHECK(reused == editor && pin_windows->width == 40 && !pin_windows->dirty,
                    "switching an edited image with a missing source loads the next canvas");
                CHECK(recovered != saved_head && recovered != NULL && recovered->child != NULL &&
                    bitmap_pixel(recovered->child->data, 0, 0) == RGB(123, 45, 67),
                    "missing-source switch preserves the edited pixels as a history image");
                if (recovered != saved_head && recovered != NULL) {
                    history_data.child = recovered->next;
                    recovered->next = NULL;
                    data_free(recovered);
                }
                pinned_image_open(owner, &first);
            }
            option.history_max = saved_max;
        }
        history_item = data_create_item(TEXT("Previous image"), FALSE, error);
        if (history_item != NULL) {
            history_item->child = data_create_data(CF_BITMAP, TEXT("BITMAP"),
                clone_bitmap(source, NULL, NULL), 0, FALSE, error);
            history_item->next = saved_head;
            history_data.child = history_item;
            if (history_item->child != NULL && pinned_image_open(owner, history_item) == editor) {
                HDC memory = CreateCompatibleDC(NULL);
                HBITMAP old = SelectObject(memory, pin_windows->bitmap);
                SetPixelV(memory, 0, 0, RGB(220, 32, 32));
                SelectObject(memory, old);
                DeleteDC(memory);
                pin_windows->dirty = TRUE;
                reused = pinned_image_open(owner, &second);
                memory = CreateCompatibleDC(NULL);
                old = SelectObject(memory, history_item->child->data);
                CHECK(reused == editor && GetPixel(memory, 0, 0) == RGB(220, 32, 32),
                    "switching images updates the previous bitmap in history before reuse");
                SelectObject(memory, old);
                DeleteDC(memory);
            }
            pinned_image_open(owner, &first);
            history_data.child = saved_head;
            history_item->next = NULL;
            data_free(history_item);
        }
        if (next != NULL) DeleteObject(next);
    }
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
        {
            ICONINFO info = {0};
            int size = MulDiv(32, GetWindowDpi(editor), 96);
            pin_windows->tool = ID_FRAME_ARROW;
            CHECK(GetIconInfo(editor_tool_cursor(pin_windows), &info) && !info.fIcon &&
                info.xHotspot == (DWORD)MulDiv(29, size, 32) &&
                info.yHotspot == (DWORD)MulDiv(16, size, 32),
                "Frame arrow has a cursor with its hotspot at the arrow tip");
            if (info.hbmMask != NULL) DeleteObject(info.hbmMask);
            if (info.hbmColor != NULL) DeleteObject(info.hbmColor);
        }
        pin_windows->tool = ID_CALLOUT;
        CHECK(editor_tool_cursor(pin_windows) == LoadCursor(NULL, IDC_CROSS),
            "Text callout uses a crosshair cursor");
        pin_windows->tool = ID_TEXT;
        CHECK(editor_tool_cursor(pin_windows) == LoadCursor(NULL, IDC_IBEAM),
            "plain Text keeps the text cursor");
        pin_windows->tool = ID_CROP;
    }
    menu = GetMenu(editor);
    CHECK(menu != NULL && GetMenuItemCount(menu) == 5, "editor has a standard five-part menu bar");
    {
        TCHAR label[64];
        GetMenuString(menu, 0, label, ARRAYSIZE(label), MF_BYPOSITION);
        CHECK(lstrcmp(label, pin_text(IDS_PIN_MENU_IMAGE)) == 0 &&
            lstrlen(label) < 16, "resource menu titles end at their own text");
    }
    image_menu = GetSubMenu(menu, 0);
    tools_menu = GetSubMenu(menu, 2);
    color_menu = GetSubMenu(menu, 3);
    size_menu = GetSubMenu(menu, 4);
    CHECK(GetMenuItemCount(size_menu) == 13, "size menu offers widths 1 through 12 px and Custom");
    {
        int width, other;
        for (width = 1; width <= 12; width++) {
            TCHAR expected[16], label[16];
            UINT command = GetMenuItemID(size_menu, width - 1);
            wsprintf(expected, width <= 9 ? TEXT("&%d px") : TEXT("%d px"), width);
            GetMenuString(size_menu, width - 1, label, ARRAYSIZE(label), MF_BYPOSITION);
            CHECK(lstrcmp(label, expected) == 0, "size menu labels match each pixel width");
            SendMessage(editor, WM_COMMAND, command, 0);
            CHECK(pin_windows->stroke_width == width && option.pinned_stroke_width == width,
                "each size menu command applies and saves its exact width");
            for (other = 0; other < 12; other++)
                CHECK(!!(GetMenuState(size_menu, other, MF_BYPOSITION) & MF_CHECKED) == (other == width - 1),
                    "only the selected width is checked");
        }
        for (width = 1; width <= 12; width++) {
            HWND popup, choice;
            last_size_popup_toggle = 0;
            SendMessage(editor, WM_COMMAND, ID_SIZE_DROPDOWN, 0);
            popup = current_size_popup_hwnd;
            CHECK(popup != NULL && GetDlgItem(popup, ID_WIDTH_EDIT) != NULL &&
                GetDlgItem(popup, ID_WIDTH_APPLY) != NULL, "width palette includes custom input and Apply");
            if (popup == NULL) break;
            choice = GetDlgItem(popup, ID_WIDTH_1 + width - 1);
            CHECK(choice != NULL, "width palette includes each preset");
            SendMessage(choice, BM_CLICK, 0, 0);
            CHECK(current_size_popup_hwnd == NULL && pin_windows->stroke_width == width &&
                option.pinned_stroke_width == width, "width palette applies each preset and closes");
        }
        {
            const TCHAR *invalid[] = { TEXT(""), TEXT("0"), TEXT("-1"), TEXT("101"), TEXT("12x"),
                TEXT("9999999999999999") };
            const int custom[] = { 1, 100, 37 };
            HWND popup, input;
            int i, previous;
            TCHAR text[16];
            last_size_popup_toggle = 0;
            SendMessage(editor, WM_COMMAND, ID_WIDTH_CUSTOM, 0);
            popup = current_size_popup_hwnd;
            CHECK(popup != NULL, "Custom menu entry opens the width palette");
            if (popup != NULL) {
                input = GetDlgItem(popup, ID_WIDTH_EDIT);
                previous = pin_windows->stroke_width;
                for (i = 0; i < ARRAYSIZE(invalid); i++) {
                    SetWindowText(input, invalid[i]);
                    SendMessage(GetDlgItem(popup, ID_WIDTH_APPLY), BM_CLICK, 0, 0);
                    CHECK(IsWindow(popup) && pin_windows->stroke_width == previous &&
                        option.pinned_stroke_width == previous,
                        "invalid custom widths preserve the current width and keep the input open");
                }
                SendMessage(input, WM_KEYDOWN, VK_ESCAPE, 0);
                CHECK(current_size_popup_hwnd == NULL && pin_windows->stroke_width == previous,
                    "Escape dismisses width input without changing the width");
            }
            for (i = 0; i < ARRAYSIZE(custom); i++) {
                last_size_popup_toggle = 0;
                show_size_popup(pin_windows);
                popup = current_size_popup_hwnd;
                CHECK(popup != NULL, "custom width palette reopens");
                if (popup == NULL) break;
                input = GetDlgItem(popup, ID_WIDTH_EDIT);
                wsprintf(text, TEXT("%d"), custom[i]);
                SetWindowText(input, text);
                SendMessage(input, WM_KEYDOWN, VK_RETURN, 0);
                CHECK(current_size_popup_hwnd == NULL && pin_windows->stroke_width == custom[i] &&
                    option.pinned_stroke_width == custom[i],
                    "Enter applies and saves custom widths including both bounds");
            }
            CHECK(GetMenuState(size_menu, ID_WIDTH_CUSTOM, MF_BYCOMMAND) & MF_CHECKED,
                "a custom width highlights the Custom menu entry");
            last_size_popup_toggle = 0;
            show_size_popup(pin_windows);
            popup = current_size_popup_hwnd;
            if (popup != NULL) {
                input = GetDlgItem(popup, ID_WIDTH_EDIT);
                SetFocus(input);
                SendMessage(input, WM_KEYDOWN, VK_TAB, 0);
                CHECK(GetFocus() == GetDlgItem(popup, ID_WIDTH_APPLY),
                    "Tab moves from width input to Apply");
                SendMessage(popup, WM_ACTIVATE, WA_INACTIVE, (LPARAM)editor);
                CHECK(current_size_popup_hwnd == NULL, "width palette closes when focus leaves the popup");
            }
        }
        SendMessage(editor, WM_COMMAND, ID_WIDTH_3, 0);
    }
    {
        LANGID previous = GetThreadUILanguage();
        const LANGID languages[] = {
            MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
            MAKELANGID(LANG_RUSSIAN, SUBLANG_DEFAULT)
        };
        TCHAR english_snip[64];
        int language;
        ini_set_language(TEXT("ru"));
        CHECK(GetThreadUILanguage() == languages[1],
            "Russian locale selects Russian resources");
        ini_set_language(TEXT("ru-RU"));
        CHECK(GetThreadUILanguage() == languages[1],
            "Russian regional locale selects Russian resources");
        ini_set_language(TEXT("de"));
        CHECK(GetThreadUILanguage() == languages[0],
            "removed locale falls back to English");
        ini_set_language(TEXT("en"));
        CHECK(GetThreadUILanguage() == languages[0],
            "English locale selects English resources");
        lstrcpyn(english_snip, pin_text(IDS_SNIP_NEW), ARRAYSIZE(english_snip));
        CHECK(lstrcmp(pin_text(IDS_PIN_TOOL_HIGHLIGHT), TEXT("Frame highlighter")) == 0 &&
            lstrcmp(pin_text(IDS_PIN_TOOL_HIGHLIGHT_BLOCK), TEXT("Block highlighter")) == 0 &&
            lstrcmp(editor_tool_help(ID_ROUGH_HIGHLIGHT), TEXT("Rough highlighter")) == 0,
            "English highlighter names match the three tools");
        for (language = 0; language < ARRAYSIZE(languages); language++) {
            SetThreadUILanguage(languages[language]);
            CHECK(*pin_text(IDS_SNIP_NEW) && *pin_text(IDS_PIN_TOOL_REDACT) && *pin_text(IDS_PIN_TOOL_INVERT) &&
                *pin_text(IDS_PIN_TOOL_HIGHLIGHT) && *pin_text(IDS_PIN_TOOL_HIGHLIGHT_BLOCK) && *pin_text(IDS_PIN_TOOL_MAGNIFY) &&
                *pin_text(IDS_PIN_TOOL_FRAME_SELECT) && *pin_text(IDS_PIN_TOOL_DIMENSION) && *pin_text(IDS_PIN_PALETTE_MARKER + 15),
                "snip and image editor strings load in every supported language");
            if (language != 0)
                CHECK(lstrcmp(english_snip, pin_text(IDS_SNIP_NEW)) != 0,
                    "selected UI language changes new snip text");
        }
        SetThreadUILanguage(previous);
    }
    CHECK(pin_windows->menu_tip != NULL, "editor creates a menu tooltip window");
    CHECK(GetMenuState(tools_menu, ID_FRAME_SELECT, MF_BYCOMMAND) != (UINT)-1 &&
        lstrlen(short_tool_tip(pin_windows, ID_REDACT)) < 32 &&
        _tcschr(short_tool_tip(pin_windows, ID_REDACT), TEXT('&')) == NULL,
        "Frame select is available and tooltips use short tool names");
    for (id = 0; id < GetMenuItemCount(tools_menu); id++) {
        UINT tool_id = GetMenuItemID(tools_menu, id);
        if (tool_id != (UINT)-1 && tool_id != 0)
            CHECK(editor_tool_help(tool_id) != NULL && *editor_tool_help(tool_id) != 0,
                "every Tools menu command has help text");
    }
    SendMessage(editor, WM_MENUSELECT, MAKEWPARAM(ID_PEN, 0), (LPARAM)tools_menu);
    CHECK(pin_windows->menu_tip_id == ID_PEN, "hovering a tool schedules its tooltip");
    SendMessage(editor, WM_EXITMENULOOP, 0, 0);
    CHECK(pin_windows->menu_tip_id == 0, "closing the menu cancels its tooltip");
    CHECK(GetMenuItemID(image_menu, 0) == ID_COPY &&
        GetMenuItemID(image_menu, 1) == ID_COPY_KEEP &&
        GetMenuItemID(image_menu, 2) == ID_SAVE_PNG &&
        GetMenuItemID(image_menu, 3) == ID_UPDATE,
        "Image menu exposes copy, save, and update");
    CHECK(GetMenuState(image_menu, ID_UPDATE, MF_BYCOMMAND) & MF_GRAYED,
        "detached bitmap cannot overwrite a source");
    for (id = ID_PEN; id <= ID_CROP; id++)
        CHECK(GetMenuState(tools_menu, id, MF_BYCOMMAND) != (UINT)-1,
            "every image tool is present in the Tools menu");
    CHECK(GetMenuState(tools_menu, ID_FILLED_RECT, MF_BYCOMMAND) != (UINT)-1 &&
        GetMenuState(tools_menu, ID_FILLED_ELLIPSE, MF_BYCOMMAND) != (UINT)-1,
        "both filled shapes are present in the Tools menu");
    CHECK(GetMenuItemCount(GetSubMenu(menu, 1)) == 4 &&
        GetMenuState(tools_menu, ID_REDACT, MF_BYCOMMAND) != (UINT)-1,
        "automatic face redaction is absent while manual redaction remains");
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
        CHECK(toolbar != NULL && SendMessage(toolbar, TB_BUTTONCOUNT, 0, 0) == 37 &&
            ImageList_GetImageCount((HIMAGELIST)SendMessage(toolbar, TB_GETIMAGELIST, 0, 0)) == 33,
            "undo, redo, editing tools, color and size dropdowns have icons");
        CHECK(toolbar != NULL &&
            SendMessage(toolbar, TB_COMMANDTOINDEX, ID_INVERT, 0) ==
                SendMessage(toolbar, TB_COMMANDTOINDEX, ID_REDACT, 0) + 1 &&
            SendMessage(toolbar, TB_COMMANDTOINDEX, ID_HIGHLIGHT, 0) ==
                SendMessage(toolbar, TB_COMMANDTOINDEX, ID_INVERT, 0) + 1 &&
            SendMessage(toolbar, TB_COMMANDTOINDEX, ID_HIGHLIGHT_BLOCK, 0) ==
                SendMessage(toolbar, TB_COMMANDTOINDEX, ID_HIGHLIGHT, 0) + 1 &&
            SendMessage(toolbar, TB_COMMANDTOINDEX, ID_ROUGH_HIGHLIGHT, 0) ==
                SendMessage(toolbar, TB_COMMANDTOINDEX, ID_HIGHLIGHT_BLOCK, 0) + 1 &&
            SendMessage(toolbar, TB_COMMANDTOINDEX, ID_SPOTLIGHT, 0) ==
                SendMessage(toolbar, TB_COMMANDTOINDEX, ID_ROUGH_HIGHLIGHT, 0) + 1 &&
            SendMessage(toolbar, TB_COMMANDTOINDEX, ID_MAGNIFY, 0) ==
                SendMessage(toolbar, TB_COMMANDTOINDEX, ID_SPOTLIGHT, 0) + 1 &&
            SendMessage(toolbar, TB_COMMANDTOINDEX, ID_FRAME_SELECT, 0) ==
                SendMessage(toolbar, TB_COMMANDTOINDEX, ID_SELECT, 0) + 1 &&
            SendMessage(toolbar, TB_COMMANDTOINDEX, ID_ZOOM, 0) ==
                SendMessage(toolbar, TB_COMMANDTOINDEX, ID_APPLY_COLOR, 0) + 1 &&
            SendMessage(toolbar, TB_COMMANDTOINDEX, ID_PAN, 0) ==
                SendMessage(toolbar, TB_COMMANDTOINDEX, ID_ZOOM, 0) + 1 &&
            SendMessage(toolbar, TB_COMMANDTOINDEX, ID_FRAME_ARROW, 0) ==
                SendMessage(toolbar, TB_COMMANDTOINDEX, ID_ARROW, 0) + 1 &&
            SendMessage(toolbar, TB_COMMANDTOINDEX, ID_DIMENSION, 0) ==
                SendMessage(toolbar, TB_COMMANDTOINDEX, ID_CALLOUT, 0) + 1 &&
            SendMessage(toolbar, TB_COMMANDTOINDEX, ID_ROTATE, 0) ==
                SendMessage(toolbar, TB_COMMANDTOINDEX, ID_CROP, 0) + 1 &&
            SendMessage(toolbar, TB_COMMANDTOINDEX, ID_COPY, 0) ==
                SendMessage(toolbar, TB_BUTTONCOUNT, 0, 0) - 1 &&
            SendMessage(toolbar, TB_COMMANDTOINDEX, ID_COLOR_DROPDOWN, 0) <
                SendMessage(toolbar, TB_COMMANDTOINDEX, ID_SIZE_DROPDOWN, 0),
            "multi-select follows Select, Frame arrow follows Arrow, and Copy ends the row");
        CHECK(toolbar != NULL && SendMessage(toolbar, TB_COMMANDTOINDEX, ID_FILLED_RECT, 0) >= 0 &&
            SendMessage(toolbar, TB_COMMANDTOINDEX, ID_FILLED_ELLIPSE, 0) >= 0 &&
            FindResource(hInst, MAKEINTRESOURCE(IDR_PIN_FILLED_RECT), RT_GROUP_ICON) != NULL &&
            FindResource(hInst, MAKEINTRESOURCE(IDR_PIN_FILLED_ELLIPSE), RT_GROUP_ICON) != NULL &&
            FindResource(hInst, MAKEINTRESOURCE(IDR_PIN_ROUGH_HIGHLIGHT), RT_GROUP_ICON) != NULL,
            "filled shape and rough highlight buttons have distinct icon resources");
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
        RECT before = pin_windows->image_rect, zoomed;
        POINT anchor = { before.left + (before.right - before.left) / 3,
            before.top + (before.bottom - before.top) / 3 }, image_before, image_after, screen = anchor;
        client_to_image(pin_windows, anchor, &image_before);
        ClientToScreen(editor, &screen);
        SendMessage(editor, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), MAKELPARAM(screen.x, screen.y));
        zoomed = pin_windows->image_rect;
        client_to_image(pin_windows, anchor, &image_after);
        CHECK(pin_windows->zoom_percent == 125 &&
            zoomed.right - zoomed.left > before.right - before.left &&
            abs(image_before.x - image_after.x) <= 1 && abs(image_before.y - image_after.y) <= 1,
            "wheel zoom enlarges image around cursor without changing annotation coordinates");
        SendMessage(editor, WM_RBUTTONDOWN, MK_RBUTTON, MAKELPARAM(anchor.x, anchor.y));
        SendMessage(editor, WM_MOUSEMOVE, MK_RBUTTON, MAKELPARAM(anchor.x - 20, anchor.y));
        SendMessage(editor, WM_RBUTTONUP, 0, MAKELPARAM(anchor.x - 20, anchor.y));
        CHECK(!pin_windows->panning && pin_windows->image_rect.left < zoomed.left,
            "right-button drag pans the zoomed image and releases capture");
        SendMessage(editor, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), MAKELPARAM(screen.x, screen.y));
        CHECK(pin_windows->zoom_percent == 100 && EqualRect(&pin_windows->image_rect, &before),
            "zooming back to fit recenters the image");
    }
    {
        RECT display = pin_windows->image_rect;
        POINT start, end;
        HDC dc = CreateCompatibleDC(NULL);
        HBITMAP old = SelectObject(dc, pin_windows->bitmap);
        COLORREF inside = GetPixel(dc, 20, 10), outside = GetPixel(dc, 60, 30);
        PIN_ARTIFACT *item;
        // Keep this pixel check at 1:1; drawing otherwise promotes small images to screen resolution.
        pin_windows->image_rect.right = display.left + pin_windows->width;
        pin_windows->image_rect.bottom = display.top + pin_windows->height;
        start = image_to_client(pin_windows, (POINT){50, 25});
        end = image_to_client(pin_windows, (POINT){10, 5});
        SelectObject(dc, old);
        SendMessage(editor, WM_COMMAND, ID_INVERT, 0);
        CHECK(pin_windows->tool == ID_INVERT &&
            (GetMenuState(tools_menu, ID_INVERT, MF_BYCOMMAND) & MF_CHECKED),
            "Invert frame can be selected from the toolbar/menu");
        SendMessage(editor, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(start.x, start.y));
        SendMessage(editor, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(end.x, end.y));
        SendMessage(editor, WM_LBUTTONUP, 0, MAKELPARAM(end.x, end.y));
        old = SelectObject(dc, pin_windows->bitmap);
        CHECK(GetPixel(dc, 20, 10) == (inside ^ 0x00ffffff) && GetPixel(dc, 60, 30) == outside,
            "reverse drag inverts only pixels inside the frame");
        SelectObject(dc, old);
        CHECK(rebuild_artifacts(pin_windows), "Invert frame replays with annotations");
        old = SelectObject(dc, pin_windows->bitmap);
        CHECK(GetPixel(dc, 20, 10) == (inside ^ 0x00ffffff), "replay does not invert the frame twice");
        SelectObject(dc, old);
        item = pin_windows->artifacts;
        while (item != NULL && item->tool != ID_INVERT) item = item->next;
        CHECK(item != NULL && artifact_hit(item, (POINT){20, 10}, 0), "Invert frame supports selection");
        SendMessage(editor, WM_COMMAND, ID_UNDO, 0);
        old = SelectObject(dc, pin_windows->bitmap);
        CHECK(GetPixel(dc, 20, 10) == inside, "undo restores the original frame colors");
        SelectObject(dc, old);
        SendMessage(editor, WM_COMMAND, ID_REDO, 0);
        old = SelectObject(dc, pin_windows->bitmap);
        CHECK(GetPixel(dc, 20, 10) == (inside ^ 0x00ffffff), "redo restores inverted frame colors");
        SelectObject(dc, old);
        SendMessage(editor, WM_COMMAND, ID_UNDO, 0);
        DeleteDC(dc);
        pin_windows->image_rect = display;
    }
    {
        const UINT frame_tools[] = { ID_HIGHLIGHT, ID_HIGHLIGHT_BLOCK, ID_ROUGH_HIGHLIGHT, ID_SPOTLIGHT, ID_MAGNIFY };
        int tool_index;
        for (tool_index = 0; tool_index < ARRAYSIZE(frame_tools); tool_index++) {
            RECT display = pin_windows->image_rect;
            POINT start, end;
            PIN_ARTIFACT *item;
            UINT tool = frame_tools[tool_index];
            pin_windows->image_rect.right = display.left + pin_windows->width;
            pin_windows->image_rect.bottom = display.top + pin_windows->height;
            start = image_to_client(pin_windows, (POINT){20, 15});
            end = image_to_client(pin_windows, (POINT){10, 5});
            SendMessage(editor, WM_COMMAND, tool, 0);
            CHECK(pin_windows->tool == (int)tool &&
                (GetMenuState(tools_menu, tool, MF_BYCOMMAND) & MF_CHECKED) &&
                SendMessage(pin_windows->toolbar, TB_ISBUTTONCHECKED, tool, 0),
                "frame tool selection updates menu and toolbar");
            SendMessage(editor, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(start.x, start.y));
            SendMessage(editor, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(end.x, end.y));
            SendMessage(editor, WM_LBUTTONUP, 0, MAKELPARAM(end.x, end.y));
            for (item = pin_windows->artifacts; item != NULL && item->tool != (int)tool; item = item->next) {}
            CHECK(item != NULL && !item->deleted && !IsRectEmpty(&item->box) &&
                (tool != ID_MAGNIFY || item->magnified_source != NULL),
                "reverse drag creates a usable frame annotation");
            SendMessage(editor, WM_COMMAND, ID_UNDO, 0);
            SendMessage(editor, WM_COMMAND, ID_REDO, 0);
            for (item = pin_windows->artifacts; item != NULL && item->tool != (int)tool; item = item->next) {}
            CHECK(item != NULL && !item->deleted, "undo and redo retain each frame annotation");
            SendMessage(editor, WM_COMMAND, ID_UNDO, 0);
            pin_windows->image_rect = display;
        }
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
        SendMessage(editor, WM_COMMAND, ID_ROTATE, 0);
        CHECK(pin_windows->width == 40 && pin_windows->height == 80 &&
            GetObject(pin_windows->original, sizeof(original), &original) &&
            original.bmWidth == 40 && original.bmHeight == 80,
            "rotate command turns the image and its editing base clockwise");
        SendMessage(editor, WM_COMMAND, ID_UNDO, 0);
        CHECK(pin_windows->width == 80 && pin_windows->height == 40,
            "undo restores image orientation");
        SendMessage(editor, WM_COMMAND, ID_REDO, 0);
        CHECK(pin_windows->width == 40 && pin_windows->height == 80,
            "redo reapplies image rotation");
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
                    HWND popup = current_color_popup_hwnd;
                    COLORREF previous_color = pin_windows->current_color;
                    SendMessage(popup, WM_LBUTTONUP, 0, MAKELPARAM(2, -2));
                    CHECK(IsWindow(popup) && GetCapture() == popup &&
                        pin_windows->current_color == previous_color,
                        "color popup stays open after the opening mouse release");
                    SendMessage(popup, WM_LBUTTONUP, 0, MAKELPARAM(0, 0));
                    CHECK(IsWindow(popup) && GetCapture() == popup,
                        "color popup stays open when releasing between choices");
                    SendMessage(popup, WM_LBUTTONDOWN, MK_LBUTTON,
                        MAKELPARAM(swatch.left + 2, swatch.top + 2));
                    SendMessage(current_color_popup_hwnd, WM_LBUTTONUP, 0,
                        MAKELPARAM(swatch.left + 2, swatch.top + 2));
                    CHECK(current_color_popup_hwnd == NULL && GetCapture() != popup,
                        "selecting a color closes the popup and releases capture");
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

            /* Color application targets the clicked annotation and shares normal history. */
            {
                POINT point = image_to_client(pin_windows, (POINT){20, 10});
                PIN_ARTIFACT *hit;
                int history;
                HWND toolbar = pin_windows->toolbar;
                SendMessage(editor, WM_COMMAND, ID_APPLY_COLOR, 0);
                CHECK(SendMessage(toolbar, TB_COMMANDTOINDEX, ID_APPLY_COLOR, 0) ==
                    SendMessage(toolbar, TB_COMMANDTOINDEX, ID_FRAME_SELECT, 0) + 1,
                    "apply color tool sits immediately after frame selection");
                SendMessage(editor, WM_COMMAND, ID_COLOR_BLUE, 0);
                history = pin_windows->undo_count;
                SendMessage(editor, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(point.x, point.y));
                SendMessage(editor, WM_LBUTTONUP, 0, MAKELPARAM(point.x, point.y));
                hit = top_artifact_at(pin_windows, (POINT){20, 10});
                CHECK(hit != NULL && hit->color == colors[1] && !pin_windows->drawing &&
                    pin_windows->undo_count == history + 1,
                    "click recolors the top annotation with one undo step");
                CHECK(bitmap_pixel(pin_windows->bitmap, 20, 10) == colors[1],
                    "applied color is rendered into the image");
                SendMessage(editor, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(point.x, point.y));
                SendMessage(editor, WM_LBUTTONUP, 0, MAKELPARAM(point.x, point.y));
                SendMessage(editor, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(0, 0));
                CHECK(pin_windows->undo_count == history + 1,
                    "unchanged color and empty clicks do not create history");
                SendMessage(editor, WM_COMMAND, ID_UNDO, 0);
                CHECK(bitmap_pixel(pin_windows->bitmap, 20, 10) == colors[2], "undo restores annotation color");
                SendMessage(editor, WM_COMMAND, ID_REDO, 0);
                CHECK(bitmap_pixel(pin_windows->bitmap, 20, 10) == colors[1], "redo reapplies annotation color");
            }
            {
                ICONINFO info = {0};
                BITMAP mask = {0};
                SendMessage(editor, WM_COMMAND, ID_MARKER, 0);
                CHECK(GetIconInfo(editor_tool_cursor(pin_windows), &info), "highlighter cursor is created");
                if (info.hbmMask != NULL) {
                    GetObject(info.hbmMask, sizeof(mask), &mask);
                    CHECK(!info.fIcon && info.hbmColor != NULL &&
                        info.xHotspot == (DWORD)(mask.bmWidth / 2) &&
                        info.yHotspot == (DWORD)(mask.bmHeight / 2),
                        "highlighter bar has a centered cursor hotspot");
                    CHECK(info.hbmColor != NULL && bitmap_pixel(info.hbmColor,
                        info.xHotspot, info.yHotspot) == RGB(255, 230, 0),
                        "highlighter cursor bar is yellow");
                    CHECK(bitmap_pixel(info.hbmMask, info.xHotspot, 1) == RGB(0, 0, 0) &&
                        bitmap_pixel(info.hbmMask, info.xHotspot - 2, 1) == RGB(255, 255, 255),
                        "highlighter cursor has transparent rounded corners");
                    DeleteObject(info.hbmMask);
                }
                if (info.hbmColor != NULL) DeleteObject(info.hbmColor);
            }

            /* 7. Test closing a dirty pinned window closes without confirmation dialog */
            DestroyWindow(editor);
            editor = NULL;
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
                editor = pinned_image_open(owner, &image_data);
                CHECK(editor != NULL, "editor reopens for preference checks");
                DWORD copy_before = toolbar_icon_checksum(pin_windows->icons, ARRAYSIZE(toolbar_icons) - 1);
                DWORD color_before = toolbar_icon_checksum(pin_windows->icons, ARRAYSIZE(toolbar_icons));
                SendMessage(editor, WM_COMMAND, ID_WIDTH_3, 0);
                DWORD size_before = toolbar_icon_checksum(pin_windows->icons, ARRAYSIZE(toolbar_icons) + 1);
                SendMessage(editor, WM_COMMAND, ID_ARROW, 0);
                set_stroke_width(pin_windows, 37);
                CHECK(size_before != toolbar_icon_checksum(pin_windows->icons, ARRAYSIZE(toolbar_icons) + 1),
                    "size dropdown highlights the selected width");
                pin_windows->current_color = standard_colors[5]; // Orange
                update_color_button_icon(pin_windows);
                CHECK(copy_before != 0 && copy_before ==
                    toolbar_icon_checksum(pin_windows->icons, ARRAYSIZE(toolbar_icons) - 1) &&
                    color_before != toolbar_icon_checksum(pin_windows->icons, ARRAYSIZE(toolbar_icons)),
                    "changing color updates only its swatch and preserves Copy icon");
                save_pinned_preferences(pin_windows);

                CHECK(option.pinned_tool == ID_ARROW, "persisted tool is ID_ARROW");
                CHECK(option.pinned_stroke_width == 37, "persisted custom stroke width is 37");
                CHECK(option.pinned_color == standard_colors[5], "persisted color is standard orange");

                DestroyWindow(editor);
                editor = NULL;
                persist_editor = pinned_image_open(owner, &image_data);
                CHECK(persist_editor != NULL, "editor reopens with persisted settings");
                if (persist_editor != NULL) {
                    p = (PINNED_IMAGE *)GetWindowLongPtr(persist_editor, GWLP_USERDATA);
                    CHECK(p != NULL && p->tool == ID_CROP, "new editor defaults to Crop despite previously selected Arrow");
                    CHECK(p != NULL && p->stroke_width == 37 &&
                        (GetMenuState(GetSubMenu(p->menu, 4), ID_WIDTH_CUSTOM, MF_BYCOMMAND) & MF_CHECKED),
                        "restored editor stroke width and menu check match persisted width");
                    CHECK(p != NULL && p->current_color == standard_colors[5], "restored editor color matches persisted color");
                    last_size_popup_toggle = 0;
                    show_size_popup(p);
                    CHECK(current_size_popup_hwnd != NULL, "width palette opens in restored editor");
                    DestroyWindow(persist_editor);
                    CHECK(current_size_popup_hwnd == NULL, "closing editor destroys its width palette");
                }
            }

            DestroyWindow(editor);
        }
    }
    {
        HWND annotations = pinned_image_open(owner, &image_data);
        PINNED_IMAGE *p = annotations != NULL ?
            (PINNED_IMAGE *)GetWindowLongPtr(annotations, GWLP_USERDATA) : NULL;
        CHECK(p != NULL, "editor opens for bitmap annotation checks");
        if (p != NULL) {
            POINT at = image_to_client(p, (POINT){10, 10});
            SendMessage(annotations, WM_COMMAND, ID_STEP, 0);
            SendMessage(annotations, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(at.x, at.y));
            SendMessage(annotations, WM_LBUTTONUP, 0, MAKELPARAM(at.x, at.y));
            CHECK(p->artifacts != NULL && p->artifacts->tool == ID_STEP &&
                p->artifacts->number == 1, "first numbered step starts at one");
            at = image_to_client(p, (POINT){60, 25});
            SendMessage(annotations, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(at.x, at.y));
            SendMessage(annotations, WM_LBUTTONUP, 0, MAKELPARAM(at.x, at.y));
            CHECK(p->artifacts != NULL && p->artifacts->next != NULL &&
                p->artifacts->next->number == 2, "numbered steps increment");
            SendMessage(annotations, WM_COMMAND, ID_SELECT, 0);
            p->selected_artifact = p->artifacts->next;
            p->selected_artifact->selected = TRUE;
            {
                POINT from = image_to_client(p, p->selected_artifact->start);
                POINT to = image_to_client(p, (POINT){p->selected_artifact->start.x + 5,
                    p->selected_artifact->start.y + 4});
                int old_x = p->selected_artifact->start.x;
                SendMessage(annotations, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(from.x, from.y));
                SendMessage(annotations, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(to.x, to.y));
                SendMessage(annotations, WM_LBUTTONUP, 0, MAKELPARAM(to.x, to.y));
                CHECK(p->selected_artifact != NULL && p->selected_artifact->start.x > old_x,
                    "Select moves an annotation before flattening");
                SendMessage(annotations, WM_COMMAND, ID_COLOR_BLUE, 0);
                CHECK(p->selected_artifact != NULL && p->selected_artifact->color == colors[1],
                    "selected annotation can be recolored");
            }
            p->selected_artifact = p->artifacts->next;
            SendMessage(annotations, WM_COMMAND, ID_DUPLICATE, 0);
            CHECK(p->selected_artifact != NULL && p->artifacts->next->next == p->selected_artifact,
                "selected annotation duplicates");
            SendMessage(annotations, WM_COMMAND, ID_UNDO, 0);
            CHECK(p->selected_artifact == NULL && p->artifacts->next->next == NULL,
                "undo removes duplicate and clears stale selection");
            clipboard_available = TRUE;
            SendMessage(annotations, WM_COMMAND, ID_COPY_KEEP, 0);
            CHECK(IsWindow(annotations) && copied_bitmap != NULL,
                "Copy keep open publishes bitmap without closing editor");
            if (copied_bitmap != NULL) DeleteObject(copied_bitmap);
            copied_bitmap = NULL;
            DestroyWindow(annotations);
        }
    }
    {
        HWND editor = pinned_image_open(owner, &image_data);
        PINNED_IMAGE *p = editor != NULL ? (PINNED_IMAGE *)GetWindowLongPtr(editor, GWLP_USERDATA) : NULL;
        CHECK(p != NULL, "editor opens for multi-selection checks");
        if (p != NULL) {
            PIN_ARTIFACT *first = add_artifact(p, ID_FILLED_RECT, (POINT){20, 10}, 2);
            PIN_ARTIFACT *second = add_artifact(p, ID_FILLED_RECT, (POINT){45, 10}, 2);
            RECT before, after;
            POINT start, end;
            int undo_before;
            first->end = (POINT){30, 20};
            second->end = (POINT){55, 20};
            CHECK(rebuild_artifacts(p), "two annotation fixtures render");
            SendMessage(editor, WM_COMMAND, ID_FRAME_SELECT, 0);
            start = image_to_client(p, (POINT){10, 5});
            end = image_to_client(p, (POINT){65, 30});
            SendMessage(editor, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(start.x, start.y));
            SendMessage(editor, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(end.x, end.y));
            SendMessage(editor, WM_LBUTTONUP, 0, MAKELPARAM(end.x, end.y));
            CHECK(selected_bounds(p->artifacts, &before) == 2 &&
                first->selected && second->selected, "frame selects both annotations");
            start = image_to_client(p, (POINT){before.right, before.bottom});
            end = image_to_client(p, (POINT){before.right + 5, before.bottom + 5});
            undo_before = p->undo_count;
            SendMessage(editor, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(start.x, start.y));
            SendMessage(editor, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(end.x, end.y));
            SendMessage(editor, WM_LBUTTONUP, 0, MAKELPARAM(end.x, end.y));
            selected_bounds(p->artifacts, &after);
            CHECK(after.right > before.right && after.bottom > before.bottom &&
                first->end.x > 30 && second->end.x > 55 && p->undo_count == undo_before + 1,
                "shared corner resizes selected annotations in one undo step");
            start = image_to_client(p, (POINT){after.right, after.bottom});
            end = image_to_client(p, (POINT){after.right + 3, after.bottom + 3});
            SendMessage(editor, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(start.x, start.y));
            SendMessage(editor, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(end.x, end.y));
            SendMessage(editor, WM_CAPTURECHANGED, 0, (LPARAM)GetDesktopWindow());
            {
                RECT cancelled;
                selected_bounds(p->artifacts, &cancelled);
                CHECK(EqualRect(&after, &cancelled) && !p->drawing && p->drag_original == NULL,
                    "lost capture restores unfinished group resize");
            }
            SendMessage(editor, WM_COMMAND, ID_UNDO, 0);
            CHECK(p->artifacts->end.x == 30 && p->artifacts->next->end.x == 55,
                "undo restores both annotation sizes");
            {
                PIN_ARTIFACT *note = add_artifact(p, ID_TEXT, (POINT){5, 5}, 2);
                note->box = (RECT){5, 5, 35, 18};
                begin_text_edit(p, note, note->box);
                CHECK(p->text_edit != NULL && p->text_edit_font != NULL &&
                    p->text_edit_brush != NULL, "Text edit uses the annotation preview font");
                if (p->text_edit != NULL) {
                    HDC dc = GetDC(p->text_edit);
                    SendMessage(editor, WM_CTLCOLOREDIT, (WPARAM)dc, (LPARAM)p->text_edit);
                    CHECK(GetTextColor(dc) == note->color,
                        "Text edit previews the saved annotation color");
                    ReleaseDC(p->text_edit, dc);
                }
                if (p->text_edit != NULL) SetWindowText(p->text_edit, TEXT("edited note"));
                SendMessage(editor, WM_COMMAND, ID_UNDO, 0);
                CHECK(p->text_edit == NULL && p->editing_artifact == NULL,
                    "undo completes text edit before replacing annotation objects");
            }
            {
                PINNED_IMAGE measure = {0};
                RECT short_box, long_box, multiline_box, blank_line_box, two_blank_lines;
                measure.width = 600;
                measure.height = 400;
                short_box = text_box_for_content(&measure, (RECT){10, 10, 160, 60}, TEXT("Hi"), 2, TRUE);
                long_box = text_box_for_content(&measure, (RECT){10, 10, 160, 60},
                    TEXT("A longer callout line"), 2, TRUE);
                multiline_box = text_box_for_content(&measure, (RECT){10, 10, 160, 60},
                    TEXT("Hi\r\nagain"), 2, TRUE);
                blank_line_box = text_box_for_content(&measure, (RECT){10, 10, 160, 60},
                    TEXT("Hi\r\n"), 2, TRUE);
                two_blank_lines = text_box_for_content(&measure, (RECT){10, 10, 160, 60},
                    TEXT("Hi\r\n\r\n"), 2, TRUE);
                CHECK(long_box.right > short_box.right && multiline_box.bottom > short_box.bottom,
                    "callout frame fits longer and multiline text");
                CHECK(blank_line_box.bottom > short_box.bottom &&
                    two_blank_lines.bottom > blank_line_box.bottom,
                    "callout frame grows for a new empty line");
                short_box = text_box_for_content(&measure, (RECT){10, 10, 160, 60}, TEXT("Hi"), 2, FALSE);
                long_box = text_box_for_content(&measure, (RECT){10, 10, 160, 60},
                    TEXT("A longer plain text line"), 2, FALSE);
                blank_line_box = text_box_for_content(&measure, (RECT){10, 10, 160, 60},
                    TEXT("Hi\r\n"), 2, FALSE);
                CHECK(long_box.right > short_box.right && blank_line_box.bottom > short_box.bottom,
                    "Text frame fits content and grows on an empty new line");
            }
            {
                PIN_ARTIFACT *callout;
                RECT live_box, first_line;
                p->tool = ID_CALLOUT;
                p->text_anchor = (POINT){1, 1};
                begin_text_edit(p, NULL, (RECT){5, 5, 60, 30});
                CHECK(p->text_edit != NULL && p->text_edit_font != NULL && p->text_edit_brush != NULL,
                    "callout edit uses preview font and background");
                if (p->text_edit != NULL) {
                    SetWindowText(p->text_edit, TEXT("Hi"));
                    update_text_edit(p);
                    first_line = p->selection;
                    SendMessage(p->text_edit, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
                    SendMessage(p->text_edit, WM_CHAR, VK_RETURN, 0);
                    CHECK(p->selection.bottom - p->selection.top > first_line.bottom - first_line.top,
                        "live callout editor grows on an empty new line");
                    SetWindowText(p->text_edit, TEXT("Hi\r\nagain"));
                }
                live_box = p->selection;
                SendMessage(editor, WM_FINISH_TEXT, TRUE, 0);
                for (callout = p->artifacts; callout != NULL && callout->next != NULL; callout = callout->next) {}
                CHECK(callout != NULL && callout->tool == ID_CALLOUT &&
                    EqualRect(&callout->box, &live_box) && p->text_edit_font == NULL && p->text_edit_brush == NULL,
                    "saved callout retains live text frame and releases preview resources");
            }
            {
                PIN_ARTIFACT *plain;
                RECT first_line, live_box;
                p->tool = ID_TEXT;
                p->text_anchor = (POINT){5, 5};
                begin_text_edit(p, NULL, (RECT){5, 5, 60, 30});
                CHECK(p->text_edit != NULL, "Text uses the shared live editor");
                if (p->text_edit != NULL) {
                    SetWindowText(p->text_edit, TEXT("Hi"));
                    update_text_edit(p);
                    first_line = p->selection;
                    SendMessage(p->text_edit, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
                    SendMessage(p->text_edit, WM_CHAR, VK_RETURN, 0);
                    CHECK(p->selection.bottom - p->selection.top > first_line.bottom - first_line.top,
                        "Text editor grows immediately on a new empty line");
                }
                live_box = p->selection;
                SendMessage(editor, WM_FINISH_TEXT, TRUE, 0);
                for (plain = p->artifacts; plain != NULL && plain->next != NULL; plain = plain->next) {}
                CHECK(plain != NULL && plain->tool == ID_TEXT && EqualRect(&plain->box, &live_box),
                    "saved Text keeps its live editor size");
                if (plain != NULL) {
                    int old_width = plain->box.right - plain->box.left;
                    begin_text_edit(p, plain, plain->box);
                    if (p->text_edit != NULL) {
                        SetWindowText(p->text_edit, TEXT("Longer text"));
                        update_text_edit(p);
                    }
                    live_box = p->selection;
                    SendMessage(editor, WM_FINISH_TEXT, TRUE, 0);
                    CHECK(EqualRect(&plain->box, &live_box) &&
                        plain->box.right - plain->box.left > old_width,
                        "re-edited Text grows and saves the new size");
                }
            }
            DestroyWindow(editor);
        }
    }
    {
        BITMAPINFO info = {0};
        HDC screen = GetDC(NULL);
        HBITMAP first, second;
        DWORD *pixels_a = NULL, *pixels_b = NULL;
        int x, y;
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = 80;
        info.bmiHeader.biHeight = -100;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        first = CreateDIBSection(screen, &info, DIB_RGB_COLORS, (void **)&pixels_a, NULL, 0);
        second = CreateDIBSection(screen, &info, DIB_RGB_COLORS, (void **)&pixels_b, NULL, 0);
        if (first != NULL && second != NULL) {
            for (y = 0; y < 100; y++) for (x = 0; x < 80; x++) {
                pixels_a[y * 80 + x] = (DWORD)((y * 1009 + x * 37) & 0xFFFFFF);
                pixels_b[y * 80 + x] = (DWORD)(((y + 15) * 1009 + x * 37) & 0xFFFFFF);
            }
            CHECK(find_scroll_shift(first, second) == 15,
                "scroll capture finds exact overlap between adjacent pages");
        }
        if (first != NULL) DeleteObject(first);
        if (second != NULL) DeleteObject(second);
        ReleaseDC(NULL, screen);
    }
    {
        TCHAR path[MAX_PATH], directory[MAX_PATH];
        BYTE signature[8] = {0};
        DWORD read = 0;
        HANDLE file;
        GetTempPath(ARRAYSIZE(directory), directory);
        wsprintf(path, TEXT("%sCLCL-editor-save-test.png"), directory);
        DeleteFile(path);
        init_gdip();
        CHECK(save_png(source, path), "Save PNG writes bitmap pixels");
        file = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, NULL);
        if (file != INVALID_HANDLE_VALUE) {
            ReadFile(file, signature, sizeof(signature), &read, NULL);
            CloseHandle(file);
        }
        CHECK(read == sizeof(signature) && signature[0] == 0x89 &&
            signature[1] == 'P' && signature[2] == 'N' && signature[3] == 'G',
            "saved file has PNG pixel format");
        DeleteFile(path);
        shutdown_gdip();
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
        const TCHAR *target = (insert_case == 4 || insert_case == 10) ? TEXT("Root anchor") : TEXT("Outer");
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
        index = insert_case == 0 ? 3 : insert_case == 2 ? 4 : insert_case == 5 ? 6 : insert_case == 9 ? 5 : 2;
        info.cbSize = sizeof(info);
        info.fMask = MIIM_STATE | MIIM_FTYPE;
        CHECK(GetMenuItemInfo(menu, index, TRUE, &info) &&
            !(info.fState & MFS_DISABLED) && !(info.fType & MFT_SEPARATOR),
            "favourite item and placeholder rows are selectable");
        GetMenuString(menu, index, label, 128, MF_BYPOSITION);
        CHECK(lstrcmp(label, insert_case == 0 ? TEXT("First && existing") : insert_case == 9 ? TEXT("Second existing") : TEXT(" ")) == 0,
            "favourite label is literal and placeholder is blank");
    }
    SendMessage(idle_menu, 0x01E5, index, 0);
    key(hwnd, insert_phase == 3 || (insert_phase == 1 && (insert_case == 4 || insert_case == 7 || insert_case == 10)) ?
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

static DWORD history_db_file_size(const TCHAR *path)
{
    WIN32_FILE_ATTRIBUTE_DATA info;
    CHECK(GetFileAttributesEx(path, GetFileExInfoStandard, &info), "history database file exists");
    return info.nFileSizeLow;
}

static void test_sqlite_space_reclamation(void)
{
    TCHAR temp_dir[MAX_PATH], db_file[MAX_PATH];
    sqlite3 *inspect = NULL;
    sqlite3_stmt *stmt = NULL;
    DWORD before, migrated, deleted;
    DATA_INFO *remaining;
    GetTempPath(MAX_PATH, temp_dir);
    GetTempFileName(temp_dir, TEXT("clc"), 0, db_file);
    DeleteFile(db_file);
    lstrcpy(temp_dir, db_file);
    CreateDirectory(temp_dir, NULL);
    wsprintf(db_file, TEXT("%s\\history.db"), temp_dir);
    db_history_close();
    CHECK(db_history_init(temp_dir), "new reclaiming database initializes");
    db_history_close();
    CHECK(sqlite3_open16(db_file, &inspect) == SQLITE_OK, "open reclamation fixture");
    CHECK(sqlite3_prepare_v2(inspect, "PRAGMA auto_vacuum;", -1, &stmt, NULL) == SQLITE_OK &&
        sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_int(stmt, 0) == 1,
        "new databases enable FULL auto-vacuum");
    sqlite3_finalize(stmt);
    // Model an old database with live blobs and space left by deleted history.
    CHECK(sqlite3_exec(inspect,
        "PRAGMA auto_vacuum=NONE; VACUUM;"
        "INSERT INTO items(id,created_at,title,total_size) VALUES(1,1,'keep',1048576),(2,2,'delete',2097152),(3,3,'newest',1048576);"
        "INSERT INTO item_formats VALUES(1,'fixture',49152,1048576,zeroblob(1048576)),"
        "(2,'fixture',49152,2097152,zeroblob(2097152)),(3,'fixture',49152,1048576,zeroblob(1048576));"
        "INSERT INTO history_fts(docid,title) VALUES(1,'keep'),(2,'delete'),(3,'newest');"
        "CREATE TABLE discarded(data BLOB); INSERT INTO discarded VALUES(zeroblob(4194304)); DROP TABLE discarded;",
        NULL, NULL, NULL) == SQLITE_OK, "seed legacy database with reclaimable space");
    sqlite3_close(inspect);
    before = history_db_file_size(db_file);
    CHECK(db_history_init(temp_dir), "legacy database migrates");
    db_history_close();
    migrated = history_db_file_size(db_file);
    CHECK(migrated + 3000000 < before, "migration reclaims previously deleted payloads");
    CHECK(db_history_init(temp_dir), "migrated database reopens");
    CHECK(db_history_delete_item(2), "delete large history payload");
    db_history_close(); // Closing checkpoints WAL, publishing the smaller main file.
    deleted = history_db_file_size(db_file);
    CHECK(deleted + 1500000 < migrated, "deletion returns blob pages to disk");
    CHECK(db_history_init(temp_dir), "database reopens after deletion");
    CHECK(db_history_get_item(2) == NULL, "deleted item stays deleted");
    remaining = db_history_get_item(1);
    CHECK(remaining != NULL, "deletion preserves remaining item ID");
    data_free(remaining);
    CHECK(db_history_trim(1), "trim old history payload");
    db_history_close();
    CHECK(history_db_file_size(db_file) + 750000 < deleted, "trimming also returns pages to disk");
    CHECK(sqlite3_open16(db_file, &inspect) == SQLITE_OK, "open compacted database for verification");
    CHECK(sqlite3_prepare_v2(inspect,
        "SELECT (SELECT count(*) FROM items), (SELECT count(*) FROM item_formats),"
        "(SELECT count(*) FROM history_fts), length(data) FROM item_formats WHERE item_id=3;",
        -1, &stmt, NULL) == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW &&
        sqlite3_column_int(stmt, 0) == 1 && sqlite3_column_int(stmt, 1) == 1 &&
        sqlite3_column_int(stmt, 2) == 1 && sqlite3_column_int(stmt, 3) == 1048576,
        "compaction preserves newest payload and matching index rows");
    sqlite3_finalize(stmt);
    CHECK(sqlite3_prepare_v2(inspect, "PRAGMA integrity_check;", -1, &stmt, NULL) == SQLITE_OK &&
        sqlite3_step(stmt) == SQLITE_ROW && lstrcmpA((LPCSTR)sqlite3_column_text(stmt, 0), "ok") == 0,
        "compacted history passes SQLite integrity check");
    sqlite3_finalize(stmt);
    sqlite3_close(inspect);
    DeleteFile(db_file);
    RemoveDirectory(temp_dir);
    printf("PASS: SQLite migration, deletion and trimming reclaim disk space\n");
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
    init_gdip();

    // 2. Create and save 3 distinct bitmap items
    HBITMAP bmp1 = create_solid_bitmap(16, 16, RGB(255, 0, 0));
    HBITMAP bmp2 = create_solid_bitmap(24, 24, RGB(0, 255, 0));
    HBITMAP bmp3 = create_solid_bitmap(32, 32, RGB(0, 0, 255));
    {
        HDC dc = CreateCompatibleDC(NULL);
        HBITMAP previous = SelectObject(dc, bmp2);
        SetPixel(dc, 5, 7, RGB(17, 93, 201));
        SelectObject(dc, previous);
        DeleteDC(dc);
    }

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
    {
        sqlite3 *inspect = NULL;
        sqlite3_stmt *stmt = NULL;
        DWORD raw_size = 0, png_size = 0;
        BYTE *raw = clipboard_data_to_bytes(item3->child, &raw_size);
        BYTE *png = bitmap_to_png(bmp2, &png_size);
        HBITMAP decoded = png != NULL ? png_to_bitmap(png, png_size) : NULL;
        CHECK(decoded != NULL && bitmap_pixel(decoded, 0, 0) == RGB(0, 255, 0),
            "PNG memory codec restores bitmap pixels");
        if (decoded != NULL) {
            DWORD before_size = 0, after_size = 0;
            BYTE *before = bitmap_to_dib(bmp2, &before_size);
            BYTE *after = bitmap_to_dib(decoded, &after_size);
            CHECK(before != NULL && after != NULL && before_size == after_size &&
                memcmp(before, after, before_size) == 0,
                "PNG round trip preserves screenshot bytes used for duplicate detection");
            CHECK(bitmap_pixel(decoded, 5, 7) == RGB(17, 93, 201) &&
                bitmap_pixel(decoded, 7, 5) == RGB(0, 255, 0),
                "PNG preserves channel values and pixel orientation");
            mem_free((void **)&before);
            mem_free((void **)&after);
        }
        if (decoded != NULL) DeleteObject(decoded);
        CHECK(png != NULL && png_to_bitmap(png, 8) == NULL,
            "PNG decoder rejects truncated input");
        mem_free((void **)&png);
        {
            int depth;
            for (depth = 24; depth <= 32; depth += 8) {
                BITMAPINFO info = {0};
                DIBSECTION restored = {0};
                BYTE *bits = NULL;
                HBITMAP source, restored_bitmap;
                DWORD encoded_size = 0;
                BYTE *encoded;
                int y, x, stride = ((17 * depth + 31) / 32) * 4;
                info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                info.bmiHeader.biWidth = 17;
                info.bmiHeader.biHeight = -9;
                info.bmiHeader.biPlanes = 1;
                info.bmiHeader.biBitCount = (WORD)depth;
                source = CreateDIBSection(NULL, &info, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
                CHECK(source != NULL, "create odd-width PNG pixel fixture");
                if (source == NULL) continue;
                for (y = 0; y < 9; y++)
                    for (x = 0; x < 17 * depth / 8; x++)
                        bits[y * stride + x] = (BYTE)(x * 17 + y * 31);
                encoded = bitmap_to_png(source, &encoded_size);
                restored_bitmap = png_to_bitmap(encoded, encoded_size);
                CHECK(restored_bitmap != NULL &&
                    GetObject(restored_bitmap, sizeof(restored), &restored) == sizeof(restored) &&
                    restored.dsBm.bmBitsPixel == depth && restored.dsBm.bmWidth == 17 &&
                    restored.dsBm.bmHeight == 9, "PNG preserves 24/32-bit dimensions and depth");
                if (restored.dsBm.bmBits != NULL) {
                    for (y = 0; y < 9; y++)
                        CHECK(memcmp(bits + y * stride, (BYTE *)restored.dsBm.bmBits +
                            y * restored.dsBm.bmWidthBytes, 17 * depth / 8) == 0,
                            "PNG preserves every color/alpha byte with padded rows");
                }
                if (restored_bitmap != NULL) DeleteObject(restored_bitmap);
                mem_free((void **)&encoded);
                DeleteObject(source);
            }
        }
        wsprintf(db_file, TEXT("%s\\history.db"), temp_dir);
        CHECK(sqlite3_open16(db_file, &inspect) == SQLITE_OK, "open PNG database fixture");
        CHECK(sqlite3_prepare_v2(inspect,
            "SELECT count(*) FROM item_formats WHERE hex(substr(data,1,8)) = '89504E470D0A1A0A' "
            "AND length(data) < data_size;", -1, &stmt, NULL) == SQLITE_OK &&
            sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_int(stmt, 0) == 3,
            "database stores smaller PNG blobs with original decoded sizes");
        sqlite3_finalize(stmt);
        stmt = NULL;
        // A legacy DIB row must coexist with new PNG rows across restart.
        CHECK(sqlite3_prepare_v2(inspect,
            "UPDATE item_formats SET data = ?, data_size = ? WHERE item_id = ?;",
            -1, &stmt, NULL) == SQLITE_OK, "prepare legacy bitmap fixture");
        if (stmt != NULL && raw != NULL) {
            sqlite3_bind_blob(stmt, 1, raw, (int)raw_size, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 2, (int)raw_size);
            sqlite3_bind_int(stmt, 3, item3->param1);
            CHECK(sqlite3_step(stmt) == SQLITE_DONE, "store legacy uncompressed bitmap");
        }
        sqlite3_finalize(stmt);
        sqlite3_close(inspect);
        mem_free((void **)&raw);
        {
            DATA_INFO *standalone = data_create_data(CF_BITMAP, TEXT("BITMAP"), NULL,
                0, FALSE, err_str);
            standalone->param1 = item2->param1;
            CHECK(db_history_ensure_item_data(standalone) && standalone->size > png_size &&
                bitmap_pixel(standalone->data, 23, 23) == RGB(0, 255, 0),
                "standalone format loading decodes PNG and retains decoded size");
            data_free(standalone);
        }
        {
            HBITMAP tiny = create_solid_bitmap(1, 1, RGB(31, 63, 127));
            DATA_INFO *tiny_item = data_create_item(TEXT("Tiny PNG fallback"), FALSE, err_str);
            tiny_item->child = data_create_data(CF_BITMAP, TEXT("BITMAP"), tiny, 0, FALSE, err_str);
            data_set_modified(tiny_item);
            CHECK(db_history_save_item(tiny_item), "save tiny bitmap with raw fallback");
            CHECK(sqlite3_open16(db_file, &inspect) == SQLITE_OK, "open fallback fixture");
            stmt = NULL;
            CHECK(sqlite3_prepare_v2(inspect,
                "SELECT length(data) = data_size AND hex(substr(data,1,8)) != '89504E470D0A1A0A' "
                "FROM item_formats WHERE item_id = ?;", -1, &stmt, NULL) == SQLITE_OK,
                "prepare raw fallback check");
            if (stmt != NULL) {
                sqlite3_bind_int(stmt, 1, tiny_item->param1);
                CHECK(sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_int(stmt, 0),
                    "tiny bitmap stays raw when PNG would increase storage");
            }
            sqlite3_finalize(stmt);
            sqlite3_close(inspect);
            db_history_delete_item(tiny_item->param1);
            data_free(tiny_item);
        }
    }
    {
        PINNED_IMAGE recovery = {0};
        DATA_INFO *saved_head = history_data.child;
        int saved_max = option.history_max;
        recovery.bitmap = create_solid_bitmap(18, 9, RGB(123, 45, 67));
        recovery.dirty = TRUE;
        option.history_max = 0;
        CHECK(replace_source(&recovery) && !recovery.dirty && recovery.source_item != NULL &&
            recovery.source_item->param1 > 0, "missing editor source is saved to SQLite before switching");
        if (history_data.child != saved_head) {
            DATA_INFO *recovered = history_data.child;
            history_data.child = recovered->next;
            recovered->next = NULL;
            data_free(recovered);
        }
        DeleteObject(recovery.bitmap);
        option.history_max = saved_max;
    }
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

    int verified = 0, updated_found = 0, recovered_found = 0, legacy_found = 0;
    DATA_INFO *cur;
    for (cur = history_data.child; cur != NULL; cur = cur->next) {
        CHECK(cur->child != NULL, "loaded bitmap item has child format node");
        CHECK(cur->child != NULL && cur->child->format == CF_BITMAP, "child format is CF_BITMAP");
        CHECK(cur->param2 != 0 && cur->child != NULL && cur->child->data != NULL,
            "startup loaded bitmap data before menu creation");
        if (cur->child != NULL && cur->child->data != NULL &&
            bitmap_pixel(cur->child->data, 0, 0) == RGB(123, 45, 67)) recovered_found++;
        if (cur->child != NULL && cur->child->data != NULL &&
            bitmap_pixel(cur->child->data, 31, 31) == RGB(0, 0, 255)) legacy_found++;
        if (cur->title != NULL && lstrcmp(cur->title, TEXT("Updated bitmap")) == 0) {
            BITMAP updated;
            CHECK(GetObject(cur->child->data, sizeof(updated), &updated) != 0 &&
                updated.bmWidth == 40 && updated.bmHeight == 20,
                "updated bitmap dimensions persist across restart");
            updated_found++;
        }
        verified++;
    }
    CHECK(verified == 4 && recovered_found == 1, "all bitmaps including recovered editor edits survive restart");
    CHECK(updated_found == 1, "bitmap update replaces one row without duplication");
    CHECK(legacy_found == 1, "legacy DIB bitmap loads alongside PNG records");

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
    shutdown_gdip();

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
    for (insert_case = 0; insert_case < 11; insert_case++) {
        if (insert_case == 8) continue; /* Reserved for bitmap edit context. */
        DATA_INFO *outer = data_create_folder(TEXT("Outer"), error);
        DATA_INFO *nested = data_create_folder(TEXT("Nested"), error);
        DATA_INFO *empty = data_create_folder(TEXT("Empty"), error);
        DATA_INFO *first = data_create_item(TEXT("First & existing"), FALSE, error);
        DATA_INFO *second = data_create_item(TEXT("Second existing"), FALSE, error);
        DATA_INFO *anchor = data_create_item(TEXT("Root anchor"), FALSE, error);
        DATA_INFO *source = data_create_item(TEXT("Clipboard copy"), FALSE, error);
        DATA_INFO **slot, *old_next, *expected_next, *copy;
        BOOL replace = insert_case == 0 || insert_case == 4 || insert_case == 9 || insert_case == 10;
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
        slot = (insert_case == 0 || insert_case == 1) ? &nested->child : insert_case == 3 ? &empty->child :
            insert_case == 4 ? &outer->next : insert_case == 5 ? &second->next : &first->next;
        if (insert_case == 10) {
            regist_data.child = anchor;
            anchor->next = outer;
            outer->next = NULL;
            slot = &regist_data.child;
        }
        if (insert_case == 7) {
            data_free(regist_data.child);
            regist_data.child = NULL;
            slot = &regist_data.child;
        }
        old_next = *slot;
        expected_next = replace ? old_next->next : old_next;
        if (replace) {
            old_next->hkey_id = 123;
            old_next->op_modifiers = MOD_CONTROL;
            old_next->op_virtkey = 'J';
            old_next->op_paste = 1;
        }
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
                CHECK(copy->next == expected_next, "favourite replaces or inserts at chosen position without changing siblings");
                if (replace) CHECK(copy->hkey_id == 123 && copy->op_modifiers == MOD_CONTROL &&
                    copy->op_virtkey == 'J' && copy->op_paste == 1, "replacement preserves the favourite hotkey");
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
    printf("PASS: Favourite replacement of first/last nested and root items, hotkeys, blank insertion slots, empty folders and cancellation\n");
}

static BOOL test_window_above(HWND above, HWND below)
{
    int remaining = 1000;
    HWND window = below;
    while (window != NULL && remaining-- > 0) {
        window = GetWindow(window, GW_HWNDPREV);
        if (window == above) return TRUE;
    }
    return FALSE;
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
            if (editor != NULL) {
                MONITORINFO monitor = { sizeof(monitor) };
                RECT before, after, bounds;
                HWND overlay, normal, active;
                int width = pin->min_window_width, height = pin->min_window_height;
                CHECK(GetMonitorInfo(MonitorFromWindow(editor, MONITOR_DEFAULTTONEAREST), &monitor),
                    "editor monitor work area available");
                bounds = monitor.rcWork;
                overlay = CreateWindowEx(WS_EX_TOPMOST, TEXT("STATIC"), TEXT("Overlay"),
                    WS_POPUP | WS_VISIBLE, bounds.left, bounds.top, bounds.right - bounds.left, 80,
                    NULL, NULL, hInst, NULL);
                normal = CreateWindowEx(0, TEXT("STATIC"), TEXT("Maximized app"),
                    WS_OVERLAPPEDWINDOW, bounds.left, bounds.top, width, height, NULL, NULL, hInst, NULL);
                CHECK(overlay != NULL && normal != NULL, "competing window fixtures created");
                ShowWindow(normal, SW_SHOWMAXIMIZED);
                pin->placing_topmost = TRUE;
                ShowWindow(editor, SW_RESTORE);
                SetWindowPos(editor, NULL, bounds.left, bounds.top, width, height,
                    SWP_NOZORDER | SWP_NOACTIVATE);
                pin->placing_topmost = FALSE;
                active = GetActiveWindow();
                editor_window_walks = 0;
                SetWindowPos(editor, NULL, bounds.left, bounds.top + 1, 0, 0,
                    SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                CHECK(editor_window_walks == 0, "editor placement avoids live window-chain traversal");
                GetWindowRect(editor, &after);
                CHECK(after.top == bounds.top + 80 && after.bottom <= bounds.bottom &&
                    after.right - after.left == width && after.bottom - after.top == height,
                    "editor moves physically below topmost window without resizing");
                CHECK(GetActiveWindow() == active, "editor placement preserves activation");
                CHECK((GetWindowLongPtr(editor, GWL_EXSTYLE) & WS_EX_TOPMOST) &&
                    test_window_above(editor, normal), "editor remains above maximized ordinary windows");
                before = after;
                place_editor_below_topmost(editor);
                GetWindowRect(editor, &after);
                CHECK(EqualRect(&before, &after), "repeated physical placement is stable");
                pin->placing_topmost = TRUE;
                SetWindowPos(editor, NULL, bounds.left, bounds.top, width, height,
                    SWP_NOZORDER | SWP_NOACTIVATE);
                pin->placing_topmost = FALSE;
                GetWindowRect(editor, &before);
                fail_editor_placement = TRUE;
                place_editor_below_topmost(editor);
                fail_editor_placement = FALSE;
                GetWindowRect(editor, &after);
                CHECK(EqualRect(&before, &after) &&
                    (GetWindowLongPtr(editor, GWL_EXSTYLE) & WS_EX_TOPMOST),
                    "failed physical placement retains editor position and pinning");
                SetWindowPos(overlay, NULL, bounds.left, bounds.top,
                    bounds.right - bounds.left, bounds.bottom - bounds.top,
                    SWP_NOZORDER | SWP_NOACTIVATE);
                place_editor_below_topmost(editor);
                GetWindowRect(editor, &after);
                CHECK(EqualRect(&before, &after), "no room below leaves editor position unchanged");
                ShowWindow(editor, SW_SHOWMAXIMIZED);
                GetWindowRect(editor, &before);
                place_editor_below_topmost(editor);
                GetWindowRect(editor, &after);
                CHECK(IsZoomed(editor) && EqualRect(&before, &after),
                    "full-screen topmost window leaves maximized editor unchanged");
                ShowWindow(editor, SW_MINIMIZE);
                CHECK(IsIconic(editor), "editor can still minimize");
                ShowWindow(editor, SW_RESTORE);
                SetActiveWindow(editor);
                CHECK(!IsIconic(editor) && IsWindowVisible(editor) && GetActiveWindow() == editor &&
                    (GetWindowLongPtr(editor, GWL_EXSTYLE) & WS_EX_TOPMOST),
                    "editor restores and activates with full-screen topmost competitor");
                if (overlay != NULL) DestroyWindow(overlay);
                if (normal != NULL) DestroyWindow(normal);
            }
            if (editor != NULL) DestroyWindow(editor);
            fail_editor_topmost = TRUE;
            editor = pinned_image_open_from_menu(owner, source);
            fail_editor_topmost = FALSE;
            CHECK(editor != NULL && IsWindowVisible(editor) &&
                !(GetWindowLongPtr(editor, GWL_EXSTYLE) & WS_EX_TOPMOST),
                "failed topmost request still opens a visible normal editor");
            if (editor != NULL) DestroyWindow(editor);
        }
    }
    data_free(source);
}

static void test_pinned_preferences_preserve_options(void)
{
    TCHAR temp_path[MAX_PATH], temp_dir[MAX_PATH], ini_path[MAX_PATH];
    TCHAR saved_path[MAX_PATH], marker[32], error[BUF_SIZE];
    int saved_max = option.history_max;
    int saved_tool = option.pinned_tool;
    int saved_width = option.pinned_stroke_width;
    COLORREF saved_color = option.pinned_color;
    PINNED_IMAGE pin = {0};
    int i;

    GetTempPath(MAX_PATH, temp_path);
    if (!GetTempFileName(temp_path, TEXT("clc"), 0, temp_dir)) {
        CHECK(FALSE, "temporary settings path created");
        return;
    }
    DeleteFile(temp_dir);
    if (!CreateDirectory(temp_dir, NULL)) {
        CHECK(FALSE, "temporary settings directory created");
        return;
    }
    lstrcpy(saved_path, work_path);
    lstrcpy(work_path, temp_dir);
    wsprintf(ini_path, TEXT("%s\\%s"), work_path, USER_INI);
    option.history_max = 300;
    CHECK(ini_put_option(), "initial options saved");
    CHECK(profile_initialize(ini_path, TRUE), "initial settings opened");
    profile_write_string(TEXT("custom"), TEXT("marker"), TEXT("preserve me"), ini_path);
    CHECK(profile_flush(ini_path), "unrelated settings saved");
    profile_free();

    pin.tool = ID_PEN;
    pin.stroke_width = 5;
    pin.current_color = RGB(0, 0, 255);
    for (i = 0; i < 2; i++) {
        pin.stroke_width++;
        save_pinned_preferences(&pin);
        CHECK(profile_initialize(ini_path, TRUE), "editor settings reopened");
        CHECK(profile_get_int(TEXT("history"), TEXT("max"), 30, ini_path) == 300,
            "editor preference save preserves the maximum item count");
        profile_get_string(TEXT("custom"), TEXT("marker"), TEXT(""), marker, ARRAYSIZE(marker), ini_path);
        CHECK(lstrcmp(marker, TEXT("preserve me")) == 0, "editor preference save preserves unrelated settings");
        CHECK(profile_get_int(TEXT("pinned"), TEXT("stroke_width"), 0, ini_path) == pin.stroke_width,
            "editor preferences still persist");
        profile_free();
    }
    {
        HANDLE locked = CreateFile(ini_path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        CHECK(locked != INVALID_HANDLE_VALUE, "settings locked to simulate a read failure");
        if (locked != INVALID_HANDLE_VALUE) {
            pin.stroke_width++;
            save_pinned_preferences(&pin);
            CloseHandle(locked);
            CHECK(profile_initialize(ini_path, TRUE), "settings reopen after a read failure");
            CHECK(profile_get_int(TEXT("history"), TEXT("max"), 30, ini_path) == 300,
                "failed editor preference save leaves existing settings intact");
            profile_free();
        }
    }
    ini_free();
    CHECK(ini_get_option(error), "settings reload succeeds after editor preference saves");
    CHECK(option.history_max == 300, "maximum item count survives settings reload");
    lstrcpy(work_path, saved_path);
    option.history_max = saved_max;
    option.pinned_tool = saved_tool;
    option.pinned_stroke_width = saved_width;
    option.pinned_color = saved_color;
    DeleteFile(ini_path);
    RemoveDirectory(temp_dir);
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
    test_pinned_preferences_preserve_options();
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
            option.action_info[i].virtkey == 'Z' &&
            option.action_info[i].modifiers == (MOD_CONTROL | MOD_SHIFT),
            "New snip action defaults to Ctrl+Shift+Z in shortcut settings");
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
        CHECK(menu != NULL && GetMenuItemCount(menu) == 7 &&
            GetMenuItemID(menu, 0) == ID_MENUITEM_VIEWER &&
            GetMenuItemID(menu, 1) == ID_MENUITEM_NEW_SNIP &&
            GetMenuItemID(menu, 2) == ID_MENUITEM_SCROLLING_SNIP &&
            GetSubMenu(menu, 3) != NULL &&
            GetMenuItemID(menu, 5) == ID_MENUITEM_OPTION &&
            GetMenuItemID(menu, 6) == ID_MENUITEM_EXIT,
            "both snip commands appear below Viewer with no trailing empty rows");
        if (menu != NULL) { menu_destory(menu); menu_free(); }
        CHECK(pinned_image_start_snip(owner), "snip overlay opens");
        overlay = FindWindow(SNIP_CLASS, NULL);
        CHECK(overlay != NULL, "snip overlay is present");
        if (overlay != NULL) {
            CHECK(!pinned_image_start_snip(owner) && !pinned_image_start_scrolling_snip(owner) &&
                FindWindow(SNIP_CLASS, NULL) == overlay &&
                FindWindowEx(NULL, overlay, SNIP_CLASS, NULL) == NULL,
                "active normal snip blocks both commands without creating a second overlay");
            ShowWindow(overlay, SW_HIDE);
            CHECK(!pinned_image_start_snip(owner) && !pinned_image_start_scrolling_snip(owner),
                "hidden overlay still blocks snips while capture finishes");
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
        CHECK(pinned_image_start_scrolling_snip(owner), "scrolling snip opens after cancellation");
        overlay = FindWindow(SNIP_CLASS, NULL);
        if (overlay != NULL) {
            CHECK(!pinned_image_start_snip(owner) && !pinned_image_start_scrolling_snip(owner),
                "active scrolling snip blocks both commands");
            SendMessage(overlay, WM_RBUTTONDOWN, 0, 0);
            CHECK(!IsWindow(overlay), "right click cancels the scrolling snip");
        }
        CHECK(pinned_image_start_snip(owner), "normal snip opens after scrolling cancellation");
        overlay = FindWindow(SNIP_CLASS, NULL);
        if (overlay != NULL) SendMessage(overlay, WM_CLOSE, 0, 0);
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
    test_dimension_annotations(owner);
    DestroyWindow(hToolTip);
    hToolTip = NULL;
    DestroyWindow(owner);
    test_bitmap_serialization();
    test_sqlite_multiple_bitmap_persistence();
    test_sqlite_space_reclamation();
    test_date_folder_deletion();
    printf("%s: 21 native-menu scenarios + pinned image editor + bitmap serialization + SQLite multiple bitmap persistence + date folder deletion\n", failures ? "FAILED" : "PASS");
    return failures ? 1 : 0;
}
