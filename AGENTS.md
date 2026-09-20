# CLCL Project Rules & AI Agent Guidelines

See [.ai-context/PROJECT.md](file:///C:/Users/al/projects/CLCL/.ai-context/PROJECT.md) for full project architecture, data structures, menu hooks, and regression prevention knowledge.

## Core Tech Stack
- **Platform**: Windows 10 / Windows 11, Win32 C/C++, GDI/GDI+, User32.
- **Compiler/Build**: Visual Studio 2026/18.x MSBuild, PlatformToolset `v145`, Target: `Release|x86`.
- **Primary Modules**: `main.c` (lifecycle/menu loop/hooks), `Menu.c` (owner-drawn menus), `History.c` (date-based history), `Favorites.c` (context menus), `Data.c` (tree structures).

## Build & Deploy Harness
- **Deploy & Restart Command**:
  ```powershell
  powershell -ExecutionPolicy Bypass -File .\build-deploy.ps1 -Deploy -Restart
  ```
  Target directory: `C:\Users\al\000\clcl\`.
  Must compile with **0 errors, 0 warnings** and restart process cleanly.

## Token Economy Principles (Zero-Bloat Pair Programming)
- **Surgical Changes**: Use targeted diff replacements (`replace_file_content`) rather than replacing whole files.
- **Bounded Reading**: Restrict file reads to relevant line windows (`StartLine`/`EndLine`) or targeted searches.
- **High-Density Responses**: Keep explanations focused, concise, and code-centric. Avoid echoing unmodified code or boilerplate.
- **Context Isolation**: Delegate large investigations or noisy compiler logs to subagents to preserve context window tokens.

## Code Standards & Invariants
- **No Physical Cursor Teleporting**: Never use `SetCursorPos` to simulate hover transitions when opening or reopening menus. Use message-based hierarchy opening via `menu_open_reopen_folder` (`0x01E5` `MN_SELECTITEM`, `0x01E3` `MN_OPENHIERARCHY`, simulated `WM_MOUSEMOVE` without cursor movement, and `VK_RIGHT`).
- **In-Place Submenu Positioning**: When a submenu appears after deletion, keep the cursor positioned in-place over the remaining items via `menu_position_cursor_in_submenu`.
- **Empty Submenus**: Prune empty History folders only. Preserve user-created Favourites folders when their last item is deleted; return to the parent menu over the retained folder (`active_submenu_item_rect`). Delete a Favourites folder only when explicitly requested.
- **Ghost Window Transparency**: `menu_ghost_wnd` (`CLCL_MenuGhost`) must remain transparent to mouse clicks (`WS_EX_TRANSPARENT` and `WM_NCHITTEST: return HTTRANSPARENT`) so hit-testing reaches underlying `#32768` menu windows.
- **Delete Key Interception**: `menu_msg_filter_proc` (`WH_MSGFILTER`) must consume `VK_DELETE` (`return 1`) on both `WM_KEYDOWN` and `WM_KEYUP` to prevent Windows default popup menu dismissal.
