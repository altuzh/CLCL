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
#include "../main.c"
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

    int verified = 0;
    DATA_INFO *cur;
    for (cur = history_data.child; cur != NULL; cur = cur->next) {
        CHECK(cur->child != NULL, "loaded bitmap item has child format node");
        CHECK(cur->child != NULL && cur->child->format == CF_BITMAP, "child format is CF_BITMAP");
        CHECK(cur->param2 != 0 && cur->child != NULL && cur->child->data != NULL,
            "startup loaded bitmap data before menu creation");
        verified++;
    }
    CHECK(verified == 3, "verified all 3 loaded bitmap items");

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
    SetDpi(96);
    GetCurrentDirectory(MAX_PATH, work_path);
    lstrcat(work_path, TEXT("\\Release\\menu-test-profile"));
    if (!ini_get_option(error)) { printf("FAIL: option defaults\n"); return 1; }
    format_initialize(error);
    option.menu_show_tooltip = 0;
    option.menu_show_tool_menu = 1;
    option.def_paste_wait = 0;
    wc.lpfnWndProc = test_proc;
    wc.hInstance = hInst;
    wc.lpszClassName = TEXT("CLCLMenuRegressionOwner");
    RegisterClass(&wc);
    owner = CreateWindow(wc.lpszClassName, TEXT("Menu regression"), WS_OVERLAPPED, 0, 0, 100, 100, NULL, NULL, hInst, NULL);
    history_data.type = regist_data.type = TYPE_ROOT;
    test_favourite_folder_path();
    for (scenario = 0; scenario < 21; scenario++) {
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
    DestroyWindow(owner);
    test_bitmap_serialization();
    test_sqlite_multiple_bitmap_persistence();
    test_date_folder_deletion();
    printf("%s: 21 native-menu scenarios + bitmap serialization + SQLite multiple bitmap persistence + date folder deletion\n", failures ? "FAILED" : "PASS");
    return failures ? 1 : 0;
}
