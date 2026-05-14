# AileEx

A Windows archive manager GUI application.  
Backed by 7z.dll and implemented with Win32 API / C++17.

## Features

- **Multi-format support**: 7z / ZIP / RAR / TAR / GZip / BZip2 / XZ / CAB / ISO and more
- **Browse, extract, compress, integrity test, and delete entries** — all in one window
- **Split-volume compression / split-archive reading**: create and extract `archive.7z.001`/`.002`/... for 7z/ZIP. RAR launches `rar.exe -v<size>`
- **Menu bar** (File / Operations / View / Help), **toolbar**, and a right-click context menu on the ListView
- **Recent archives** history (up to 10 entries, shown in the File menu)
- **Drag & drop** to open an archive or compress files
- **Password protection** and compression level selection (encrypted archives auto-prompt for password on open)
- **Column header click** for ascending/descending sort in the ListView
- **Splitter** to freely resize the folder tree and file list panes; **toggle visibility** of tree/toolbar
- Window size, splitter position, MRU, and advanced compression options **auto-saved to INI**
- Automatically detects additional codecs provided by 7-Zip Zstandard DLL (Brotli / LZ4 / LZ5 / Lizard / FastLZMA2 / Zstandard)
- **DPI-aware** (Per-Monitor V2)

## Requirements

| Item | Details |
|---|---|
| OS | Windows 10 / 11 (x64) |
| Language | C++17 / Win32 API |
| Build system | CMake 3.20+ / MSVC 2022+ |

### Runtime DLLs / EXEs

| File | Default path | Purpose |
|---|---|---|
| `7z.dll` | Auto-detected from registry `HKLM\SOFTWARE\7-Zip` → `%ProgramFiles%\7-Zip\7z.dll` → same directory as AileEx.exe | General archive operations |
| `unrar.dll` (`UnRAR64.dll`) | Auto-detected from `%ProgramFiles%\UnrarDLL\x64\UnRAR64.dll` → `%ProgramFiles(x86)%\UnrarDLL\x64\UnRAR64.dll` → same directory as AileEx.exe (`unrar.dll`) | RAR extraction (optional) |
| `WinRAR.exe` / `Rar.exe` | Auto-detected from registry `HKLM\SOFTWARE\WinRAR` → `%ProgramFiles%\WinRAR\` | RAR compression (optional; WinRAR.exe preferred, falls back to Rar.exe) |

## Build

```powershell
# Debug
cmake -B build
cmake --build build
# Output: build\AileEx.exe

# Release
cmake -B build_release -DCMAKE_BUILD_TYPE=Release
cmake --build build_release
# Output: build_release\AileEx.exe
```

## Launch Modes

| Command-line argument | Behavior |
|---|---|
| No arguments | Show main window (empty state) |
| Archive file | Browse mode — open the archive and display its contents |
| Regular file | Compress mode — show the compression dialog |
| Mixed | Compress mode takes priority |
| `-x <archive>` | Forced extract mode — show extract destination dialog directly (skip list view) |
| `-x <archive> -d <dir>` | Same, but extract directly to `<dir>` (skip destination dialog too) |
| `-a <file...>` | Forced compress mode — show compression dialog directly |
| `-a <file...> -d <dir>` | Same, but preset the output directory to `<dir>` in the compression dialog |

Extensions recognized as archives: `7z`, `zip`, `rar`, `tar`, `gz`, `bz2`, `xz`, `cab`, `iso`, `jar`, `wim`, `lzma`, `lzh`, `arj`

## Main Features

### Browse

- Left pane: TreeView showing folder hierarchy
- Right pane: ListView showing file list (name / size / compressed size / type / modified date)
- **Column header click** for ascending / descending sort
- **Splitter** to resize left and right panes (position auto-saved to INI)
- Multi-select in ListView to extract only selected files

### Compress

- Supported formats: 7z / ZIP / TAR / GZip / BZip2 / XZ / **RAR**
- RAR launches WinRAR (`rar.exe`) as a subprocess
- All others use `7z.dll`'s `IOutArchive`
- Compression level: 0 (store) to 9 (ultra)
- Method selection (7z: LZMA2 / LZMA / PPMd / BZip2 / Deflate / Zstandard / Brotli / LZ4 / LZ5 / Lizard / FastLZMA2; ZIP: Deflate / BZip2 / LZMA / Zstandard / Brotli / LZ4 / Store)
- Codecs unsupported by the loaded 7z.dll are automatically removed from the menu
- **Advanced options** dialog for dictionary size, word size, solid block, thread count, etc.
- RAR format has its own advanced options (compression method / recovery record / header encryption)

### Extract

- Supports all formats recognized by `7z.dll`
- RAR backend can be switched between `7z.dll` and `unrar.dll` in settings
  - **`unrar.dll` backend always extracts all files** (selective file extraction only works with the `7z.dll` backend)
- Automatically falls back to the other backend if one fails
- **Subfolder creation policy** (MkDir setting): never / single file only / multiple entries (default) / always

### Test

- Verifies integrity of the current archive (no files are created on disk)
- 7z backend uses `IInArchive::Extract(testMode=1)`; unrar backend uses `RAR_TEST`

### Delete

- Deletes selected entries (including children of selected folders) from the archive
- 7z path rebuilds archive with only the remaining entries using `IOutArchive::UpdateItems` (compressed blobs copied without re-encoding)
- RAR path launches `rar.exe d -y -r`
- Not supported for write-incompatible formats (ISO / CAB / JAR, etc.)

### Keyboard Shortcuts

| Key | Action |
|---|---|
| F5 / Ctrl+E | Extract |
| Ctrl+A | Add files (compress) |
| Ctrl+O | Open archive |
| Ctrl+T | Integrity test |
| Del | Delete selected entries |
| Ctrl+F4 | Close archive (app stays open) |
| Enter | (In ListView) Navigate into folder / open with associated app |
| Esc | Exit application |

## Settings

Saved to `AileEx.ini` in the same folder as the EXE. Editable via the settings dialog (settings button in toolbar).

```ini
[General]
RarExtractor=7z          ; "7z" or "unrar"
RarExePath=              ; Absolute path to WinRAR.exe / Rar.exe (empty = auto-detect from registry)
DefaultOutputDir=        ; Default extraction destination
DefaultFormat=7z         ; Default compression format
CompressionLevel=5       ; 0-9
RarLevel=3               ; RAR compression level 0-5
MkDir=2                  ; Subfolder creation on extract: 0=never/1=single/2=multiple(default)/3=always
7zDllPath=               ; Absolute path to 7z.dll (empty = auto-detect from registry)
UnrarDllPath=            ; Absolute path to unrar.dll (empty = same directory as AileEx.exe)

[Window]
X=                       ; Window position and size (auto-saved)
Y=
W=900
H=600
Maximized=0
Splitter=220             ; Splitter position
TreeVisible=1            ; Tree pane visibility toggle
ToolbarVisible=1         ; Toolbar visibility toggle

[Mru]                    ; Recent archives (up to 10; Path0 is most recent)
Path0=
...

[AdvancedCompress]       ; Last-used values from the advanced compression dialog
DictSize=
WordSize=
SolidBlock=
Threads=
Extra=

[RarAdvanced]            ; Last-used values from the RAR advanced compression dialog
DictSize=
Volume=
Extra=
Solid=1
Threads=0
Recovery=0
```

## Architecture Overview

```
main()
  └─ App (singleton) ─ Settings / SevenZip / UnrarDll
        ├─ MainWindow (browse) ─ ProgressDlg ─ WorkerThread
        └─ CompressDlg (compress settings) ─ RarProcess (rar.exe)
```

- Compression and extraction run on a worker thread
- Progress is reported to the UI thread via `PostMessage(WM_APP_PROGRESS / WM_APP_DONE)`
- Cancellation is handled by `ProgressPostSink::SetCancelled(true)` or `RarProcess::Cancel()`

## Limitations

- Multi-volume archive creation and reading are not yet supported
- Extracting individual files from solid archives is not possible (7z.dll limitation)
- Shell integration (context menu) is not implemented
- Simultaneous browsing of multiple archives (tabs, etc.) is not implemented

## Documentation

- [`docs/specification.md`](docs/specification.md) — Functional specification
- [`docs/architecture.md`](docs/architecture.md) — Class structure and thread model
- [`docs/build.md`](docs/build.md) — Detailed build instructions
- [`docs/known-issues.md`](docs/known-issues.md) — Known pitfalls and workarounds

## Credits

### Application Icon

[Archiver - free Icon in PNG and SVG](https://icon-icons.com/icon/archiver/37045) by [icon-icons.com](https://icon-icons.com/), used under free for commercial use license.
