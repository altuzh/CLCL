# CLCL Project Memory & Architecture Context

Architecture baseline: 2026-09-20. Menu mechanics and regression checks updated: 2026-09-23. This document contains persistent project context, architecture, operational workflows, and critical regression prevention knowledge for AI coding assistants working on the CLCL codebase.

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

- SQLite bitmap storage (`DbHistory.c`, `gdip.cpp`): new/updated 24/32-bit `CF_BITMAP`/`CF_DSPBITMAP` payloads use PNG when smaller than the original DIB. `data_size` retains the decoded size. Both lazy-load paths recognize PNG signatures and still read legacy DIB rows; existing records are not bulk converted. Other clipboard formats and `.dat` files retain their existing representation. Copy GDI+ pixels directly: `GetHBITMAP` changes alpha/reserved bytes and breaks byte-based duplicate detection. The native regression harness covers PNG pixels, row padding, both load paths, raw fallback, and mixed legacy/new records.

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

- `show_popup_menu` owns the outer create/track/destroy/reopen loop. `popup_menu != NULL` also prevents reentrant main-menu opening.
- For keyboard Delete, `menu_delete_current_item` records `menu_delete_target`, cursor/position state and reopen flags, captures the ghost, then calls `EndMenu`. **Do not free the target during native tracking:** owner-draw teardown can still reference it. After tracking returns and `menu_destory` destroys the native menu, `menu_delete_pending_item` mutates the tree.
- Context-menu actions run after the main native menu has been destroyed. Their result determines deletion, pruning and restoration before the next iteration.
- `menu_prune_empty_parent` prunes empty History folders. Favourites folders survive deletion of their last item; restore to their parent over the retained folder. Delete a Favourites folder only through an explicit folder action.
- Preserve root coordinates and monitor-edge alignment through `has_reopen_pos`, `menu_reopen_pos` and `menu_reopen_align`. `menu_free` releases menu-item metadata; saved `MENU_ITEM_INFO *` values must not survive it.

### 4.2 Cursor Stability: NO Cursor Teleportation
- **NEVER use `SetCursorPos` to simulate hovering on parent menus.**
  - Win32 has a configurable submenu hover delay. Moving the real cursor to the root can dismiss the submenu being restored.
- **Submenu reopening mechanism (`menu_open_reopen_folder` in `main.c`):**
  - Finds folder index in `popup_menu` via `menu_find_folder_index`.
  - Selects the parent using internal message `0x01E5` (`MN_SELECTITEM`), then posts `VK_RIGHT` down/up so Windows opens the hierarchy after layout. The initial RMB folder path also uses `0x01E3` (`MN_OPENHIERARCHY`). These are internal Windows messages; retain native regression coverage when changing them.
  - Guarded by `menu_folder_hover_posted` so it executes once per reopening.
  - `WM_ENTERIDLE` drives restoration. Nested Favourites use `menu_reopen_fav_folder`, `menu_reopen_fav_ancestor` and `menu_next_folder_on_path` to open one ancestor at a time.
- **In-place cursor positioning (`menu_position_cursor_in_submenu` in `main.c`):**
  - Requires a visible window, valid layout and the expected `HMENU` from `MN_GETHMENU`. An ancestor repaint must not complete restoration intended for a child.
  - For ordinary restoration, keep `menu_cursor_restore_pt` if it still hits a row; otherwise clamp to the nearest real row after shrinking/reparenting. Select that row and post `WM_MOUSEMOVE`; the existing `SetCursorPos` here is for landing/clamping, never hierarchy navigation.
  - The pending-context branch described below returns before cursor positioning, so switching context targets does not move the pointer.
  - Successful restoration clears pending restore state, kills the safety timer and hides the ghost.

### 4.3 Anti-Flicker Ghost Window (`CLCL_MenuGhost`)

- Implemented in `main.c`:
  - `menu_ghost_show`: Takes a `BitBlt` capture of all `#32768` windows before `EndMenu()` and creates a window with `WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOPMOST`.
  - Must return `HTTRANSPARENT` in `menu_ghost_wnd_proc` for `WM_NCHITTEST` so hit testing passes cleanly through to menu windows.
  - The ghost is a static bitmap, not a live submenu. Changing a highlight cannot close an obsolete panel or populate another folder.
  - Before capture, visible native menu windows are synchronously redrawn, followed by `GdiFlush`/`DwmFlush`. When context hit rows exist, `menu_ghost_prep_submenus` prepares panel backgrounds and `menu_drawitem` renders folder children and the highlighted row into the bitmap. Capturing before paint/composition can freeze a blank panel.
  - `menu_ghost_hide` destroys the window/bitmap **and clears captured hit rows and highlight pointers**. Rebuild hits after hiding the old ghost, before making a replacement snapshot.

### 4.4 Right-Click Context Menus (`main.c`, `Favorites.c`, `History.c`)

- Right-clicking items in popup menus:
  - **History item:** Opens Add to Favourites / Delete context menu (`favorites_show_add_menu`).
  - **History date folder:** Expands its items and opens Delete Submenu (`history_show_folder_menu`).
  - **Favourites item:** Opens context menu with Delete option (`favorites_show_item_menu`).
  - **Favourites root/subfolder:** Opens folder actions (`favorites_show_folder_menu`). Normalize a root row with no `set_di` to `&regist_data` when switching targets.
- `menu_get_item_from_point` uses `WindowFromPoint` and that window's `MN_GETHMENU`. Do not search `current_menu_handle` or unrelated submenus first: querying a different menu with the clicked window can return an overlapping, stale row. This previously selected a child instead of the clicked date folder.
- While a context menu tracks, the main native menu has already been destroyed (`popup_menu == NULL`), but its metadata and ghost remain. `menu_context_capture_items` records visible rows for `menu_context_filter_proc` to handle clicks on the ghost.
- Live context windows, including cascading children, take precedence over ghost rows. A press followed by release outside the rows must not choose a new target. LMB follows ordinary item/folder selection; RMB changes context target.

### 4.5 Switching Context Targets: Rebuild the Visible Hierarchy

The regression was date Delete menu -> RMB on a root clipboard (CB) item: the CB row highlighted, but the old date items stayed visible. The old inner context loop reused the same ghost bitmap.

Current flow:

1. `menu_context_filter_proc` records the picked row and point, then ends context tracking.
2. `show_popup_menu` saves the stable `DATA_INFO *` in `menu_context_reopen_target`. Resolve the desired hierarchy: a nonempty folder opens itself; a leaf/empty folder needs its parent. Nested Favourites preserve their ancestor path.
3. Release old row metadata and rebuild the main native menu with `TPM_NOANIMATION`. Check whether the target leaf appears directly in the rebuilt root: History can flatten a CB item onto the root even when its data parent is a date folder. In that case, clear the date/Favourites reopen path.
4. At `WM_ENTERIDLE`, open the required hierarchy. Once `menu_position_cursor_in_submenu` sees the expected menu and valid bounds, select the target leaf if present, hide the old ghost, recapture visible rows and repaint/capture the replacement ghost.
5. Resolve the new context action, clear `menu_context_reopen_target`, mark reopen requested and end native tracking. The outer loop then opens the new context menu against the replacement snapshot.

Required outcomes: date -> root CB has no date panel or stale date hit rows; date -> another date shows the new date's items; date -> Favourites shows Favourites items. The pointer stays in place. Do not replace this sequence with highlight-only painting or reuse old `MENU_ITEM_INFO *` pointers after `menu_free`.

### 4.6 Exit and Blocking Safeguards

- `WM_EXITMENULOOP` preserves restoration only for deliberate delete/reopen transitions; context-menu exits have `popup_menu == NULL` and must not clear the saved main-menu state.
- `ID_MENU_HOVER_SAFETY_TIMER` (800 ms) ends unresolved native restoration before clearing pending targets, restore flags and the ghost. Menu-creation failures also clear the ghost and timer.
- Unhandled RMB on a native menu must have a dismissal path rather than being swallowed forever. Keep Delete consumption on both keydown and keyup.
- Clipboard viewer-chain forwarding uses bounded `SendMessageTimeout` (200 ms, `SMTO_ABORTIFHUNG | SMTO_BLOCK`); an unresponsive external viewer must not indefinitely block this UI thread.

### 4.7 Menu Regression Checks

After building Release objects, run the existing native harness:

```powershell
rtk powershell -ExecutionPolicy Bypass -File .\tests\menu-regression.ps1
```

- `tests/menu_regression.c` includes production `main.c` and exercises real Win32 menu tracking, drawing and hooks on a separate, non-input desktop. It substitutes cursor I/O and persistence/clipboard dispatch; menu scenarios do not operate on the user's history. Other checks use temporary bitmap/SQLite test data.
- Verified 2026-09-23: 20 native-menu scenarios plus bitmap serialization, SQLite persistence and date-folder deletion passed. Scenarios 17/18/19 cover date context -> root CB / another date / Favourites. They check target identity, removal of old date hit rows, new folder contents, cursor stability and restoration; the CB case also requires the ghost bounds to equal the root panel bounds. Earlier scenarios cover deletion, retained empty Favourites, nested folders, alignment, LMB/RMB switching and rendered selection changes.
- Keep test clicks outside the live context rectangle. A click on a covered row is correctly routed to the context menu, not the ghost.
- Required deployment gate remains `build-deploy.ps1 -Deploy -Restart`: Release|x86/v145, no warnings/errors, confirmed new PID. Last code change passed this gate.
- Native checks do not replace interactive visual confirmation on the user's desktop. That confirmation was still pending after the last fix; do not record the user's original visual issue as independently confirmed resolved.
