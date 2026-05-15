# AileEx Architecture

## Directory Structure

```
AileEx/
├── CMakeLists.txt
├── CLAUDE.md
├── docs/
│   ├── specification.md
│   ├── architecture.md
│   ├── build.md
│   ├── known-issues.md
│   ├── roadmap.md
│   ├── compress-extra-params.md  — 7z/ZIP ISetProperties key=value parameter reference
│   └── rar-extra-params.md       — rar.exe switch reference for RAR compression
├── src/
│   ├── main.cpp                   — wWinMain, argument parsing, mode routing
│   ├── App.h/.cpp                 — Singleton, DLL load management, message loop
│   ├── MainWindow.h/.cpp          — Browse window (menu + toolbar + TreeView + ListView + status bar)
│   ├── CompressDlg.h/.cpp         — Compression settings dialog
│   ├── AdvancedCompressDlg.h/.cpp — 7z/ZIP advanced compression options (dict/word/solid/threads/extra)
│   ├── RarAdvancedDlg.h/.cpp      — RAR advanced compression options (recovery/volume etc.)
│   ├── CompressHelper.h/.cpp      — Single entry point for RAR compression (`RunRarCompressSync`)
│   ├── ProgressDlg.h/.cpp         — Modal progress dialog
│   ├── SettingsDlg.h/.cpp         — Settings dialog
│   ├── InfoDlg.h/.cpp             — Entry details display dialog
│   ├── PropertiesDlg.h/.cpp       — Archive-wide properties dialog
│   ├── CommentDlg.h/.cpp          — Archive comment view/edit dialog
│   ├── Settings.h/.cpp            — INI read/write, MRU management
│   ├── SevenZip.h/.cpp            — 7z.dll wrapper (IIn/IOutArchive + DeleteItems + callbacks + Find7zDll)
│   ├── UnrarDll.h/.cpp            — unrar.dll C API wrapper
│   ├── RarProcess.h/.cpp          — WinRAR.exe (GUI) / Rar.exe (console) subprocess (Compress / Delete)
│   ├── ArchiveItem.h              — Archive entry POD struct
│   ├── I18n.h/.cpp                — Localized string loading (en-US / ja-JP via SetProcessPreferredUILanguages)
│   ├── WorkerThread.h/.cpp        — Worker thread + IExtractProgressSink + ProgressPostSink
│   └── resource.h                 — Resource IDs, WM_APP_* constants
├── res/
│   ├── AileEx.rc            — Dialog templates, accelerators, embedded manifest
│   ├── AileEx.ico           — Application icon
│   └── manifest.xml         — Common Controls v6, dpiAware = PerMonitorV2
└── sdk/
    └── 7zip/                — Minimal 7-Zip SDK headers
        ├── compat.h         — Type aliases like UInt32/Int64
        ├── IDecl.h          — IID GUID definitions + helper macros
        ├── IProgress.h
        ├── IStream.h
        ├── IPassword.h      — ICryptoGetTextPassword[2] (hand-written)
        ├── PropID.h
        └── Archive/IArchive.h — Format CLSIDs + IInArchive/IOutArchive
```

## Class Diagram

```
                    ┌─────────────┐
                    │    main()    │
                    └──────┬──────┘
                           │
                  ┌────────▼─────────┐
                  │      App         │←─ Settings (INI read/write, MRU)
                  │ (Singleton)      │←─ SevenZip (7z.dll wrapper)
                  │                  │←─ UnrarDll (unrar.dll wrapper)
                  └────────┬─────────┘
                           │
              ┌────────────┴────────────┐
              ▼                         ▼
       ┌─────────────┐          ┌──────────────┐    ┌──────────────────────┐
       │ MainWindow   │─────────▶│ CompressDlg  │───▶│ AdvancedCompressDlg  │
       │ (Browse)     │          │ (Compress)   │    │ RarAdvancedDlg       │
       │ + Menu       │          └──────┬───────┘    └──────────────────────┘
       │ + Toolbar    │                  │
       │ + TreeView   │                  ▼
       │ + ListView   │           ┌──────────────────┐
       │ + Status     │           │ CompressHelper   │
       └──────┬──────┘            │ (RAR consolidate)│
              │                    └────────┬─────────┘
       ┌──────┼──────┬──────────┬────────┐  │
       ▼      ▼      ▼          ▼        ▼  ▼
  ┌─────────┐┌─────────┐┌────────┐┌────────┐┌─────────────┐
  │ProgressDlg│SettingsDlg││InfoDlg │PassDlg ││ RarProcess   │
  │ + Cancel │└─────────┘└────────┘└────────┘│ (WinRAR/Rar) │
  └────┬─────┘                                │ Compress     │
       │                                      │ Delete       │
       ▼                                      └──────────────┘
  ┌─────────────────────────────┐
  │ WorkerThread                │
  │ + IExtractProgressSink      │
  │ + ProgressPostSink          │
  └──────┬──────────────────────┘
         │
         ▼
  PostMessage WM_APP_PROGRESS / WM_APP_DONE
```

## Dialog List

| Resource ID | Class / Purpose |
|---|---|
| `IDD_COMPRESS` | `CompressDlg` — Compression settings |
| `IDD_COMPRESS_ADV` | `AdvancedCompressDlg` — 7z/ZIP advanced compression options |
| `IDD_RAR_COMPRESS_ADV` | `RarAdvancedDlg` — RAR advanced compression options |
| `IDD_PROGRESS` | `ProgressDlg` — Modal progress |
| `IDD_SETTINGS` | `SettingsDlg` — Settings |
| `IDD_INFO` | `InfoDlg` — Entry details |
| `IDD_ARCHIVE_PROPS` | `PropertiesDlg` — Archive-wide properties (format, method, size, encryption etc.) |
| `IDD_COMMENT` | `CommentDlg` — Archive comment view/edit |
| `IDD_PASSWORD` | Password input (auto-shown when opening encrypted archive) |
| `IDD_ABOUT` | About dialog |
| `IDR_MAIN_MENU` | Main window menu bar |

## Thread Model

```
UI Thread                    Worker Thread
─────────────────            ────────────────────────────────────
WorkerThread::Start(task) ──→ Execute task()
                               IArchiveExtractCallback::SetCompleted()
                                 PostMessageW(hwnd, WM_APP_PROGRESS, pct, (LPARAM)filename_copy)
UI wakes on WM_APP_PROGRESS ←─────────────────────────────────
User clicks Cancel:
  sink->SetCancelled(true)
  SetCompleted returns E_ABORT
PostMessageW(hwnd, WM_APP_DONE, hr, 0) ──→
```

- Worker executes archive operations (`SevenZip::Extract` / `Compress`, `UnrarDll::ExtractArchive`)
- Callbacks like `IArchiveExtractCallback::SetCompleted` notify UI via `PostMessage` with progress
- `WM_APP_PROGRESS` `lParam` is `_wcsdup`'d `wchar_t*` → UI side must `free()`
- Cancel: UI thread sets `sink->SetCancelled(true)`, worker callback returns `E_ABORT` to abort
- `RarProcess` cancel forcibly terminates rar.exe with `TerminateProcess`

## Format Routing

`MainWindow::OpenArchive(path)`:

```
Is .rar file?
  ├─ Yes → RarExtractor setting is "unrar" and unrar.dll loaded?
  │   ├─ Yes → Try unrar.ListArchive()
  │   │   └─ Fail → Fallback to 7z.OpenArchive()
  │   └─ No  → Try 7z.OpenArchive()
  │       └─ Fail → Fallback to unrar.ListArchive() (if loaded)
  └─ No  → Try 7z.OpenArchive() only
```

`SevenZip::OpenArchive(path)`:
- Determine CLSID from extension → get handler with `CreateInArchive`
- Open via `archive->Open()`
- If `.rar` returns `S_FALSE`, fallback to RAR4 (CLSID byte `0x03`)

Continuation of `MainWindow::OpenArchive`:
- If all backends fail, possibility of encrypted header, so show password dialog with `PromptPassword()` and retry `SevenZip::OpenArchive` with entered password
- On success, normalize archive path (`GetFullPathNameW`), register in `Settings::AddMru`, rebuild menu with `RebuildMruMenu()`

## Settings Dialog

After `Settings::Save()`, call `App::ReloadDlls()` to reload with new DLL paths.

## Message Constants

`resource.h`:

```cpp
#define WM_APP_PROGRESS (WM_APP + 1)  // wParam=percent (0-100), lParam=wchar_t* (free required)
#define WM_APP_DONE     (WM_APP + 2)  // wParam=HRESULT
```

## Primary Windows APIs

| API | Purpose |
|---|---|
| `CreateProcessW` | RAR compression (launch rar.exe) |
| `CreatePipe` | Capture rar.exe stdout |
| `LoadLibraryW` / `GetProcAddress` | Dynamic load 7z.dll / unrar.dll |
| `RegOpenKeyExW` | Registry search for WinRAR install path |
| `WritePrivateProfileStringW` | Save INI settings |
| `DragAcceptFiles` / `DragQueryFileW` | Drag & drop support |
| `CreateAcceleratorTable` | Keyboard shortcuts |
| `SetProcessDpiAwarenessContext` | DPI support (PerMonitorV2) |
| `IFileOpenDialog` (Shell) | Folder selection dialog (extract destination, settings dialog) |
