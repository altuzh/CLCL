# CLCL Project Memory & Architecture Context

Last verified: 2026-09-20. This document contains persistent project context, architecture, operational workflows, and critical regression prevention knowledge for AI coding assistants working on the CLCL codebase.

---

## 1. Project Overview & Tech Stack

- **Application:** CLCL - Clipboard Caching and History Utility for Windows.
- **Language / Platform:** C / C++ (Win32 API, GDI, GDI+, Shell APIs, User32).
- **Target OS:** Windows 10 / Windows 11 (x86 32-bit build running natively on 32-bit and WOW64 64-bit).
- **Toolchain:** MSBuild with Visual C++ compiler (Platform Toolset `v145` / VS 2026/18.x).
- **Project Structure:**
  - `CLCL.sln`: Master solution file.
  - `CLCL.exe` (`main.c`, `Menu.c`, `History.c`, `Favorites.c`, `Data.c`, `Viewer.c`, `Ini.c`, `Search.c`, `ToolTip.c`): Main process hosting tray icon, popup menus, data storage, and viewer.
  - `CLCLHook.dll` (`CLCLHook/`): Global Windows hook library for clipboard monitoring and keyboard interception.
  - `CLCLSet.exe` (`CLCLSet/`): Configuration & Options dialog application.
  - `Installer/`: Installer project (`CLCLInst.exe`).

---

## 2. Navigation, Build, and Deploy Workflow

- **Repository Root:** `C:\Users\al\projects\CLCL`.
- **Deployed Runtime Directory:** `C:\Users\al\000\clcl\`.
- **Build & Deploy Script:**
  ```powershell
  powershell -ExecutionPolicy Bypass -File .\build-deploy.ps1 -Deploy -Restart
  ```
  This script:
  1. Locates MSBuild.
  2. Compiles `CLCL.sln` (`Release|x86`, PlatformToolset `v145`).
  3. Gracefully stops the running `CLCL.exe` process (saving backup).
  4. Copies `Release\CLCL.exe`, `Release\CLCLHook.dll`, and `Release\CLCLSet.exe` to `C:\Users\al\000\clcl\`.
  5. Relaunches `CLCL.exe` on the interactive Windows desktop.
- **Verification Gates:**
  - Build must succeed with **0 errors and 0 warnings**.
  - Always verify process restart via `build-deploy.ps1` output PID.

---

## 3. Architecture & Core Subsystems

### 3.1 Hierarchical Data Model (`Data.c`, `Data.h`)
- Core struct: `DATA_INFO`.
  - Node types: `TYPE_ROOT`, `TYPE_FOLDER`, `TYPE_ITEM`, `TYPE_DATA`.
  - Linked list: `next` sibling pointer, `child` subtree pointer.
- `history_data`: Global root node (`TYPE_ROOT`) for clipboard history.
- `regist_data`: Global root node (`TYPE_ROOT`) for registered Favourites items and templates.
- Key helpers:
  - `data_check(DATA_INFO *di, const DATA_INFO *check_di)`: Recursively traverses tree and returns the **immediate parent** of `check_di`.
  - `data_delete(DATA_INFO **root, DATA_INFO *di, BOOL free_flag)`: Removes node from sibling list and frees resources.
  - `data_create_folder(title, di)`: Allocates and initializes a `TYPE_FOLDER` node.

### 3.2 Date-Based History Organization (`History.c`, `History.h`)
- Clipboard items are organized chronologically into date folders:
  - Format: `YYYY/MM/DD` (e.g. `2026/09/20`).
  - Flattening & Restructuring: `history_restructure` extracts all items and groups them into date folders sorted descending by timestamp.
  - New items added via `history_add` prepend to today's date folder or root depending on settings.

### 3.3 Custom Menu Rendering Engine (`Menu.c`, `Menu.h`)
- Windows popup menus are custom owner-drawn (`MF_OWNERDRAW`):
  - `menu_create`: Allocates and populates `MENU_ITEM_INFO` hierarchies.
  - `WM_MEASUREITEM` (`menu_set_drawitem`): Computes item width, height, icon gutter, column breaks, and DPI scaling (`Scale(int)`).
  - `WM_DRAWITEM`: Renders item text, hotkey accelerators, clipboard format icons, selection rectangles (`ODS_SELECTED`), and hierarchical submenus.
  - Windows class: Standard Win32 popup menu class is `#32768`.

### 3.4 Input Interception Hooks (`main.c`)
- **Message Filter Hook (`WH_MSGFILTER`, `menu_msg_filter_proc`):**
  - Active during `TrackPopupMenu` loop (`MSGF_MENU`).
  - Intercepts `VK_DELETE`: executes `menu_delete_current_item()` on `WM_KEYDOWN` and returns `1` (consuming the key) to prevent the default Windows menu handler from dismissing the menu.
  - Intercepts Right-Click (`WM_RBUTTONDOWN`, `WM_RBUTTONUP`): suppresses default cancel and triggers context menus.
- **CBT Hook (`WH_CBT`, `menu_cbt_proc`):**
  - Monitors window creation (`HCBT_CREATEWND`) for menu windows (`#32768`).
  - First `#32768` window is captured as `menu_root_wnd`.
  - Subsequent `#32768` windows are submenus and are automatically subclassed with `menu_submenu_subclass_proc`.
  - Clears `menu_root_wnd` on `HCBT_DESTROYWND`.

---

## 4. Critical Regression Knowledge & Best Practices

### 4.1 Menu Persistence on Deletions & Actions
- When deleting an item via `Delete` key or right-click menu:
  1. Record restore state (`menu_cursor_restore_pt`, `menu_cursor_restore_needed = TRUE`).
  2. If inside a date folder: record `menu_reopen_folder_title = parent_folder->title`, `menu_reopen_is_fav = FALSE`.
  3. If inside Favourites: record `menu_reopen_is_fav = TRUE`, and if inside a subfolder, record its title.
  4. Capture ghost screenshot via `menu_ghost_show()`.
  5. Delete data node via `data_delete`.
  6. If a History folder becomes empty, prune it. If a Favourites folder becomes empty, retain it and its ancestors; delete such folders only on explicit folder deletion. Restore the parent menu with `menu_cursor_restore_pt` over the retained folder (`active_submenu_item_rect`).
  7. Set `menu_delete_requested = TRUE; menu_reopen_requested = TRUE; EndMenu();`.
  8. Outer loop in `main.c` (`menu_show`) detects `menu_delete_requested || menu_reopen_requested`, calls `menu_create`, and redisplays `TrackPopupMenu` at the exact saved screen coordinates and alignment (`has_reopen_pos`, `menu_reopen_pos`, `menu_reopen_align`).

### 4.2 Cursor Stability: NO Cursor Teleportation
- **NEVER use `SetCursorPos` to simulate hovering on parent menus.**
  - Win32 popup menus enforce `SPI_GETMENUSHOWDELAY` (400 ms) for mouse hover transitions. Teleporting the cursor to the root menu forces the cursor across the screen and immediately dismisses the date submenu.
- **Submenu reopening mechanism (`menu_open_reopen_folder` in `main.c`):**
  - Finds folder index in `popup_menu` via `menu_find_folder_index`.
  - Sends internal Win32 menu window messages directly to the root menu window `#32768`:
    - `0x01E5` (`MN_SELECTITEM`): selects the folder item index.
    - `0x01E3` (`MN_OPENHIERARCHY`): opens the submenu hierarchy immediately.
  - Sends simulated `WM_MOUSEMOVE` to the menu window at folder coordinates without moving the physical cursor.
  - Posts `WM_KEYDOWN, VK_RIGHT` and `WM_KEYUP, VK_RIGHT` to ensure keyboard navigation expands the hierarchy.
  - Subclasses submenu windows via `menu_subclass_enum_proc`.
  - Guarded by `menu_folder_hover_posted` so it executes once per reopening.
- **In-place cursor positioning (`menu_position_cursor_in_submenu` in `main.c`):**
  - When the newly opened submenu window `#32768` appears, the physical cursor is already inside the submenu area (`menu_cursor_restore_pt`).
  - Only clamps `pt.y` if the submenu shrank due to deletion (`rcSub.bottom - Scale(12)`).
  - Sends `WM_MOUSEMOVE` and a 1-pixel `mouse_event` jiggle (`1, 0` then `-1, 0`) to force Windows menu to update its hot-tracking highlight.
  - Clears `menu_cursor_restore_needed`, resets reopen titles, and hides the ghost window.

### 4.3 Anti-Flicker Ghost Window (`CLCL_MenuGhost`)
- Implemented in `main.c`:
  - `menu_ghost_show`: Takes a `BitBlt` capture of all `#32768` windows before `EndMenu()` and creates a window with `WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOPMOST`.
  - Must return `HTTRANSPARENT` in `menu_ghost_wnd_proc` for `WM_NCHITTEST` so hit testing passes cleanly through to menu windows.
  - `menu_ghost_hide`: Safely hides and destroys the ghost window once the new menu is drawn and ready.

### 4.4 Right-Click Context Menus (`Favorites.c`, `Favorites.h`)
- Right-clicking items in popup menus:
  - **History item:** Opens context menu to Add to Favourites (`favorites_show_add_menu`).
  - **History date folder:** Expands the date folder in place without closing.
  - **Favourites item:** Opens context menu with Delete option (`favorites_show_item_menu`).
  - **Favourites subfolder:** Opens context menu with Create Subfolder and Delete Folder options (`favorites_show_folder_menu`).
