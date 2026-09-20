# CLCL - Advanced Clipboard Caching Utility for Windows

CLCL is a powerful, lightweight clipboard manager for Windows 10 and Windows 11. It records clipboard history, provides hierarchical organization of templates and favorites, and offers rapid keyboard and mouse navigation with native owner-drawn popup menus and dark mode support.

This fork modernizes CLCL with high-performance SQLite history storage, full-text search, persistent bitmap thumbnail caching, non-destructive cloud synchronization, and batch backup management.

---

## Key Modernization Features

### 1. Cloud Backup & Synchronization (Yandex Disk)
- **Pre-configured Authorization**: Seamless OAuth 2.0 flow with live browser authorization and instant token validation.
- **Timestamped Backup Batches**: Backups are saved non-destructively in dedicated folders named `YYYY-MM-DD HH:MM:SS` under `disk:/CLCL/`. Existing backups are never overwritten.
- **Cloud Backup Management Dialog**:
  - Interactive window listing all available cloud backup batches sorted in reverse chronological order (newest first).
  - Restore history and favourites from any chosen backup batch.
  - Delete individual cloud backup batches on demand with confirmation.
- **Smart Deduplication**: Uses SHA-256 content hashing to ensure only new clips are downloaded and duplicate items are skipped.
- **Clear Local History**: Safely empties the local SQLite history database and resets in-memory history while **strictly preserving all user Favourites and templates**.

### 2. High-Performance SQLite History & Full-Text Search
- Replaced legacy flat-file history with an optimized SQLite database (`history.db`).
- SQLite Full-Text Search (FTS4 with `unicode61` tokenizer) for sub-millisecond search across clip titles, window titles, and clip text content.
- Automatic content-hash deduplication and date-based clustering.

### 3. Native Dark Mode & Owner-Drawn Menus
- Complete Windows 10/11 dark mode theme integration across all popup menus, dialogs, list boxes, and tree views.
- Custom owner-drawn popup menu engine:
  - In-place cursor positioning after item deletion.
  - Submenu hierarchy opening via message loop hooks without cursor teleportation.
  - Bitmap thumbnail preview generation directly in the popup menu.
  - Direct execution of paste or context actions without dismiss conflicts.

### 4. Robust Bitmap & Multi-Format Persistence
- Correct serialization and deserialization of bitmap graphics and rich clipboard formats.
- Persistent thumbnail caching for images stored in Favourites and History across application restarts.

---

## Cloud Backup Setup

1. Open **Options** (`CLCLSet.exe`) and navigate to the **Cloud** tab.
2. Check **Enable Yandex Disk synchronization**.
3. Click **Authorize in Browser...**.
4. Log into your Yandex account and grant permission. Copy the verification code or access token.
5. Paste the token in the prompt and click **OK**.
6. You can now:
   - **Save backup to cloud**: Creates a new timestamped backup batch.
   - **Import / Manage backups...**: Select a specific backup to restore, or delete old backups.
   - **Clear local history...**: Wipe local history clips while keeping all Favourites intact.

---

## Building from Source

### Prerequisites
- Windows 10 / Windows 11
- Visual Studio 2022 / Visual Studio 2026 (MSBuild toolset `v145` or `v143`)
- C/C++ Desktop Development workload
- PowerShell 5.1+

### Build & Deploy Command
To build in `Release|x86` and deploy to the target directory:
```powershell
powershell -ExecutionPolicy Bypass -File .\build-deploy.ps1 -Deploy -Restart
```

### Running Regression Tests
To execute the native menu regression test harness (16 scenarios covering navigation, deletion, context switching, and bitmap serialization):
```powershell
powershell -ExecutionPolicy Bypass -File .\tests\menu-regression.ps1
```

---

## License

CLCL is open-source software provided under the original Nakka License. See [LICENSE](LICENSE) for details.
