# AileEx Specification

Archive manager GUI application for Windows using 7z.dll as the backend.

## Overview

| Item | Details |
|---|---|
| Platform | Windows 10 / 11 (x64) |
| Language | C++17 / Win32 API |
| Build System | CMake 3.20+ |
| Compression Backend | 7z.dll (LoadLibrary), unrar.dll (LoadLibrary), rar.exe / WinRAR.exe (CreateProcess) |
| Settings Storage | INI file in same directory as EXE (`AileEx.ini`) |

## Features

### Launch Mode

The operating mode is determined by command-line arguments to `AileEx.exe`.

| Argument | Behavior |
|---|---|
| No arguments | Show main window (empty state) |
| Archive file | Browse mode — open archive in main window |
| Regular file | Compress mode — show compression dialog |
| Mixed | Compress mode takes priority |
| `-x <archive>` | Forced extract mode — show extract destination dialog directly, skipping the list view. Non-archive extensions are rejected before opening. |
| `-a <file...>` | Forced compress mode — show compression dialog directly (equivalent to dropping regular files). |

`-d <dir>` (also `-d<dir>`) can be combined with either flag to override the destination:
- With `-x`: skips the folder picker and extracts directly to `<dir>` (MkDir policy still applied).
- With `-a`: presets the output path field in the compression dialog to `<dir>`.

Both `-x` and `-a` suppress the main window (`SW_HIDE`). The `-x` / `-a` flags take priority over auto-detection.

Recognized extensions (treated as archives): `7z`, `zip`, `rar`, `tar`, `gz`, `bz2`, `xz`, `cab`, `iso`, `jar`, `wim`, `lzma`, `lzh`, `arj` and other formats dynamically enumerated by 7z.dll.

### Main Window

- **Menu bar** (`IDR_MAIN_MENU`): File / Edit / View / Help
- Left pane: TreeView (folder hierarchy)
- Right pane: ListView (file list, columns: Name / Size / Compressed / Type / Modified)
  - **Column header click** to sort ascending/descending (click again to reverse)
- **Splitter** between panes (drag with mouse to adjust width, position saved to INI)
- Top: Toolbar (Extract / Open / Add / Info / Test / Settings) — **toggleable from View menu**
- Bottom: Status bar (entry count, loaded DLL name, progress percent / current file)
- Drag & drop support:
  - Drop archive → open
  - Drop regular file → show compression dialog
- **Right-click context menu** on ListView: Extract selected / Open with association / Test / Info / Delete

#### Menu Structure

| Menu | Item | Command ID | Notes |
|---|---|---|---|
| File | Open... `Ctrl+O` | `IDM_FILE_OPEN` | `IFileOpenDialog` |
| | Recent Archives | `IDM_FILE_MRU_BASE..LAST` | Max 10 items, `&1..&9` mnemonics |
| | Close `Ctrl+F4` | `ID_CLOSE` | Close archive (not exit app) |
| | Exit `Esc` | `IDM_FILE_EXIT` | Exit application |
| Edit | Extract... `F5` | `ID_EXTRACT` | |
| | Add... `Ctrl+A` | `ID_ADD` | Compress |
| | Test `Ctrl+T` | `ID_TEST` | Integrity verification |
| | Open with Association `Enter` | `ID_OPEN_ASSOC` | Extract temp then `ShellExecute` |
| | Delete `Del` | `ID_DELETE` | 7z uses `IOutArchive`, RAR uses `rar d` |
| | Info | `ID_INFO` | Selected entry details |
| | Settings... | `ID_SETTINGS_DLG` | |
| View | Toolbar | `IDM_VIEW_TOOLBAR` | Toggle checkbox |
| | Tree View | `IDM_VIEW_TREE` | Toggle checkbox |
| Help | About... | `IDM_HELP_ABOUT` | About dialog |

Dynamically updated via `WM_INITMENUPOPUP` based on archive state (open / read-only / selection count) using `EnableMenuItem` / `CheckMenuItem`.

### Compression Feature

- Supported formats: 7z / ZIP / TAR / GZip / BZip2 / XZ / RAR
- RAR spawns WinRAR.exe (GUI) or Rar.exe (console) as subprocess
  - 7z.dll does not support RAR writing (`FormatToOutGuid("rar")` is unsupported, falls back to CLSID_Format_7z). **RAR compression path is consolidated in `CompressHelper::RunRarCompressSync()`**
- Other formats use 7z.dll's `IOutArchive`
- Compression level: 0 (no compression) / 1 / 3 / 5 / 7 / 9 (maximum). RAR: 0..5
- Method selection:
  - 7z: LZMA2 / LZMA / PPMd / BZip2 / Deflate / Zstandard / Brotli / LZ4 / LZ5 / Lizard / FastLZMA2
  - ZIP: Deflate / BZip2 / LZMA / Zstandard / Brotli / LZ4 / Store
  - Only encoders available in loaded 7z.dll are displayed (dynamically enumerated via `GetNumberOfMethods` / `GetMethodProperty`)
- **Advanced Options** dialog (`IDD_COMPRESS_ADV`): Dictionary size / Word size / Solid block size / Threads / **Split volume** / Extra parameters (→ comprehensive list in [`docs/compress-extra-params.md`](compress-extra-params.md))
- **RAR Advanced Options** dialog (`IDD_RAR_COMPRESS_ADV`): Dictionary size / Solid / Threads / Recovery record / Split volume / Extra parameters (→ comprehensive list in [`docs/rar-extra-params.md`](rar-extra-params.md))
- **Split volume** (`volumeSize`): Supported for 7z / ZIP only. Splits as `archive.7z.001` / `archive.7z.002` ... RAR uses `rar.exe -v<size>`
- Password protection supported (7z has header encryption option)
- Output file extension auto-corrected when format changes
- Advanced options last-used values persisted in INI
- **Self-extracting (SFX)**: 7z / RAR only. Select from dropdown in compression dialog: "GUI version (.exe)", "Console version (.exe)", or "None"
  - 7z: Prepend `7z.sfx` (GUI) or `7zCon.sfx` (console) from same directory as 7z.dll to compressed .7z data, generate `.exe`
  - RAR: Pass `rar.exe -sfx<modulePath>`, use `Default.SFX` (GUI) or `WinCon.SFX` (console) from rar.exe / WinRAR.exe directory
  - Can combine with split volume (SFX module prepended to volume 1, generates `archive.exe.001 / .002 / ...`)
  - If SFX module not found, show error and abort (do not output `.7z` / `.rar`)

### Extract Feature

- Supported formats: All formats recognized by 7z.dll (7z/ZIP/RAR/TAR/GZ/BZ2/XZ/CAB/ISO/...)
- **Split archives** (`archive.7z.001` / `.002` / ...): Opening volume 1 (`.001`) uses 7z.dll's Split handler + `IArchiveOpenVolumeCallback` to concatenate volumes
  - Auto-unwrap: If result is "single file + archive extension", extract inner archive to temp file, reopen, and **display entries from inside directly**
  - Auto-unwrapped split archives are **read-only** (delete menu disabled)
- **RAR multi-volume** (`archive.partN.rar` / `.r00` `.r01`): Opening volume 1 concatenates volumes internally via unrar.dll / 7z.dll
- RAR backend auto-switch: Select `7z.dll` or `unrar.dll` in settings
- If one backend fails, automatically fallback to the other
- Selective file extraction (multi-select in ListView → extract only selected)
  - **Valid only with 7z.dll backend**. If nothing selected, extract all files
  - **With unrar.dll backend, always extracts all files regardless of selection**
- Full extraction (F5/Ctrl+E with no selection)
- **Subfolder creation policy** (`MkDir` setting):
  - `0` = Do not create
  - `1` = Only for single-file archives
  - `2` = Create when multiple entries (default)
  - `3` = Always create
  - Subfolder name is archive name with compound extensions (`.tar.gz` etc.) removed

### Test Feature (`ID_TEST`)

Verify integrity of the selected archive in place. Does not extract files.
- 7z backend: Pass `testMode=1` to `IInArchive::Extract`
- unrar backend: Pass `RAR_TEST` to `RARProcessFileW`
- Show `ProgressDlg` during verification. Cancellable. Display result in message box on completion

### Delete Feature (`ID_DELETE`)

Delete entries selected in ListView (including contents when folder selected) from archive.
- 7z backend: `IOutArchive::UpdateItems` writes only entries to keep to `.~tmp` file, then replace original with `MoveFileExW(MOVEFILE_REPLACE_EXISTING)`. **Entries to keep are passed with `newData=0/newProperties=0/indexInArchive=oldIdx`, so compressed blobs are copied as-is without re-encoding** (no password needed)
- RAR backend: `rar.exe d -y -r <archive> <path1> ...`
- Write-unsupported formats (ISO / CAB / JAR etc.) fail at `IOutArchive` `QueryInterface` stage (`E_NOINTERFACE`)
- Header-encrypted 7z fails at `IInArchive::Open` stage (password not retained)

### Recent Archives (MRU)

- Maximum **10 items**
- Added to front each time opened (existing paths deduplicated case-insensitively with `_wcsicmp`)
- If file in history cannot be opened, warn and remove from history
- Saved in INI `[Mru]` section as `Path0..Path9`
- **File → Recent Archives** submenu built dynamically. Includes `&1..&9` mnemonics

### Settings

Saved in INI file (same location as `AileEx.exe`, filename is `AileEx.ini`).

```ini
[General]
RarExtractor=7z              ; "7z" or "unrar"
RarExePath=                  ; Absolute path to WinRAR.exe / Rar.exe (auto-detect from registry if empty)
DefaultOutputDir=            ; Default extraction destination
DefaultFormat=7z             ; Default compression format
CompressionLevel=5           ; 0-9
RarLevel=3                   ; RAR compression level 0-5
MkDir=2                      ; Subfolder creation on extract 0/1/2/3
7zDllPath=                   ; Absolute path to 7z.dll (auto-detect from registry if empty)
UnrarDllPath=                ; Absolute path to unrar.dll (same directory as AileEx.exe if empty)
DefaultSfxMode=              ; SFX mode "" / "gui" / "console" — last selected in compress dialog

[AdvancedCompress]            ; Last-used values from advanced options dialog
DictSize=
WordSize=
SolidBlock=
Threads=
Extra=
Volume=                       ; Split volume size "" / "100m" / "1g" etc.

[RarAdvanced]                 ; Last-used values from RAR advanced options
DictSize=
Volume=
Extra=
Solid=1
Threads=0
Recovery=0

[Window]
X=                           ; Window position and size (auto-saved)
Y=
W=900
H=600
Maximized=0
Splitter=220                 ; Splitter position
TreeVisible=1                ; Tree view toggle
ToolbarVisible=1             ; Toolbar toggle

[Mru]                        ; Recent archives (max 10 items, Path0 is newest)
Path0=
Path1=
...
Path9=
```

Changeable from settings dialog. After saving with OK, DLL auto-reloaded (`App::ReloadDlls`).
Folder selection uses Explorer-style dialog (`IFileOpenDialog + FOS_PICKFOLDERS`).

### Info Dialog (`IDD_INFO`)

Select file in ListView and open **Menu → Info** or right-click → Info to display detailed information about selected entry (path, size, compressed size, method, modified time, etc.).

### About Dialog (`IDD_ABOUT`)

Displayed via **Help → About**. Shows title / version / links / credits.

### Password Input Dialog (`IDD_PASSWORD`)

Shown in two situations:

- **Header-encrypted archive**: `IInArchive::Open` fails without a password → dialog shown before the list view appears. On success the password is stored in `MainWindow::m_password` and reused for subsequent extraction/test.
- **Content-encrypted archive** (no header encryption): `IInArchive::Open` succeeds and the list is displayed, but encrypted entries are detected at extraction time → dialog shown before the destination folder picker. Password is stored in `m_password` for the extraction call.

In both cases `m_password` is cleared each time a new archive is opened.

### Keyboard Accelerators

| Key | Command ID | Action |
|---|---|---|
| F5 / Ctrl+E | `ID_EXTRACT` | Extract |
| Ctrl+A | `ID_ADD` | Add file (compress) |
| Ctrl+O | `IDM_FILE_OPEN` | Open archive |
| Ctrl+T | `ID_TEST` | Integrity test |
| Del | `ID_DELETE` | Delete entry |
| Ctrl+F4 | `ID_CLOSE` | Close archive (not exit app) |
| Enter | (in ListView) | Navigate folder / `ID_OPEN_ASSOC` |
| Esc | `IDM_FILE_EXIT` | Exit app |

Menu mnemonics like `Alt+F` also work. Tab key navigated via `IsDialogMessageW`.

### DPI Support

- Manifest declares `dpiAware = PerMonitorV2`
- `wWinMain` dynamically calls `SetProcessDpiAwarenessContext` (ignored on Windows 8.1 and below)

## Progress Dialog

- Modal (parent window disabled)
- Progress bar + current filename + elapsed time + cancel button
- Worker thread notifies via `PostMessage(WM_APP_PROGRESS, percent, (LPARAM)wcsdup(filename))`
- On completion: `PostMessage(WM_APP_DONE, hr, 0)`
- Cancel: `ProgressPostSink::SetCancelled(true)` or for RAR: `RarProcess::Cancel()`
- Progress PostMessage throttled to ~20Hz (prevents cancel messages from being buried)

## Primary Win32 Messages

| Message | Purpose |
|---|---|
| `WM_APP_PROGRESS` (`WM_APP+1`) | wParam=percent, lParam=filename (requires `free()`) |
| `WM_APP_DONE` (`WM_APP+2`) | wParam=HRESULT |
| `WM_DROPFILES` | Receive file drop |
| `WM_INITMENUPOPUP` | Update state before menu display (exclude system menu with `HIWORD(lParam) == 0`) |

## Limitations and Unimplemented Features

Priority and implementation approach for unimplemented features: see [`docs/roadmap.md`](roadmap.md).

- Multi-volume archives:
  - 7z / ZIP split creation and reading supported (implemented 2026-05-08, uses `CMultiVolOutStream` + `IArchiveOpenVolumeCallback`)
  - GZ / BZ2 / XZ / TAR do not support splitting (format specifications require non-seekable output)
- Individual extraction from solid archives not possible (7z.dll limitation)
- Adding/updating existing archives not implemented (`ID_ADD` is new creation only)
- Shell integration (context menu) not implemented
- Archive comment display/editing not implemented
- Archive search/filter not implemented
- Multi-archive simultaneous browse (tabs etc.) not implemented
- Multi-language support (i18n) not implemented (Japanese hardcoded)
- RAR delete cancel path not implemented (`RarProcess::Delete` does not support `Cancel()`)
- Header-encrypted 7z archive deletion fails (password is not passed to the delete path)
- Manual test matrix partially implemented (RAR formats confirmed only)
