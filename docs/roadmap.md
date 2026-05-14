# AileEx Roadmap

Summary of features commonly found in archive managers but not yet implemented in AileEx,
and features that would be nice to have. Includes implementation hints and effort estimates.

Last updated: 2026-05-10

Effort estimates:
- **S** = half day to 1 day
- **M** = few days
- **L** = 1 week or more

---

## High Priority (Features typical archive managers have)

### 1. ~~Add/update files to existing archive~~ — Implemented (2026-05-09)

Implemented `SevenZip::AddToArchive` (`CAddCallback` mixing existing copy + new add) and
`RarProcess::Add` (`rar.exe a -ep1 -r [-ap<folder>]`).
UI: "Add to current archive" menu (Ctrl+U) and confirmation dialog on drag-drop.
Add destination folder is under currently selected folder in tree.

### 2. Shell Integration (Explorer right-click menu) — `L`

Right-click on `.7z` etc. in Explorer → "Open with AileEx", "Extract here", etc.

Implementation hints:
- Need DLL implementing COM server (`IShellExtInit` + `IContextMenu`)
- Install via `regsvr32` or direct write to HKCR/CLSID
- Standard practice is to implement as separate DLL `AileExShell.dll`, not in main AileEx
- Make sure to create uninstall path (registry cleanup)
- If not contained in DLL, can be stub just calling AileEx.exe from Explorer process (lightweight)

Related files: new `src/shell/`, `installer/` directory

### 3. ~~Display/edit archive comments~~ — Implemented (2026-05-09)

Read via `SevenZip::GetArchiveComment` / `UnrarDll::GetArchiveComment`,
write via `SevenZip::SetZipArchiveComment` (direct EOCD rewrite) and `RarProcess::SetComment`
(`rar.exe c -z<file>`). Dedicated `CommentDlg` provides display/edit UI.
ZIP comments use CP_OEMCP (match 7-Zip ZIP handler interpretation), RAR comments auto-fallback
RAR5=UTF-8 / RAR4=OEM. 7z format excluded (no archive-wide comment in spec).

### 4. Archive search and filter — `S`

Useful in archives with thousands to tens of thousands of entries.

Implementation hints:
- Add edit control above ListView
- Filter `m_items` and redraw per input (incremental search)
- Wildcard support (`*.txt`) via `PathMatchSpecW`
- For stress-free operation with large entry counts, keep filter results in separate array and maintain sort state

Related files: `MainWindow.cpp` (ListView related)

### 5. ~~Multi-language support (i18n)~~ — Implemented (2026-05-10)

Embed English and Japanese in single EXE. In `res/AileEx.rc`, have two blocks with
`LANGUAGE LANG_ENGLISH, SUBLANG_ENGLISH_US` and
`LANGUAGE LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN`, duplicating dialogs/menus/STRINGTABLE.

Implement `Init()` / `Tr(IDS)` / `TrFmt(IDS, ...)` / `TrFilter(IDS)` in `src/I18n.{h,cpp}`.
`Init()` checks `GetUserDefaultUILanguage()` and selects `ja-JP` or `en-US` via
`SetProcessPreferredUILanguages` (follows OS language). Subsequent `LoadStringW` / `LoadMenuW` /
`DialogBoxW` automatically use correct language.

`SevenZip::PropIdToLabel` refactored to `PropIdToLabelId` (PROPID → IDS).
`PropertiesDlg` "Format" / "Method" comparison also fixed to use current language labels.
OFN filter restored with `TrFilter` using `|` as NUL sentinel.

For testing, environment variable `AILEEX_LANG=en|ja` can override OS setting.
When adding 3rd+ languages, consider satellite DLL approach (current embedding is lightweight enough).

### 6. ~~CLI execution without UI~~ — Removed (2026-05-14)

Removed in favor of GUI options `-x` / `-a` / `-d` which cover the intended use cases.

---

## Medium Priority (Nice to have)

### 7. Dark mode — `M`

Follow Windows 11 system theme.

Implementation hints:
- `DwmSetWindowAttribute(DWMWA_USE_IMMERSIVE_DARK_MODE)` for title bar only
- Content (menu, ListView, dialogs) requires manual `WM_CTLCOLORSTATIC` / custom draw
- Get "app color mode" via `SHGetSetSettings` and follow dynamically
- Implementation is non-trivial, recommend phased approach (title bar only → full)

### 8. Batch processing — `M`

Batch test / extract / verify multiple archives.

Implementation hints:
- Add "File → Batch operations" menu
- Queue dropped files, process sequentially in worker thread
- Progress dialog shows two-level display: "Total X/N + current file"
- Useful for operations if results can be output to CSV / log

### 9. Hash calculation (SHA-256 etc.) — `S`

Display SHA-256 / SHA-1 / MD5 / CRC32 for selected files.

Implementation hints:
- Use Win32 BCrypt API (`BCryptHashData`)
- Files in archive: calculate while extracting (memory stream OK)
- Add hash field to Info dialog or right-click → "Calculate hash" command
- 7z format often has CRC built-in, just display that (`kpidCRC`)

### 10. Archive conversion — `M`

Example: open `.zip` and resave as `.7z` (extract → compress in 1 action).

Implementation hints:
- Extract to temp dir → select different format in compress dialog → delete temp dir after
- Large archives need temp space. Include `%TEMP%` check + disk space check
- Handle for password-protected archives needs consideration (re-enter password during conversion etc.)

### 11. Estimate remaining time / transfer speed in progress — `S`

Implementation hints:
- In `ProgressPostSink::OnProgress`, linear extrapolation from completion % and elapsed time
- Show "calculating..." for first few seconds (can't estimate yet)
- Speed: moving average of throughput in last N seconds
- Add 2 lines to ProgressDlg layout

### 12. Edit archive file → auto re-pack — `M`

"Open with association (`ID_OPEN_ASSOC`)" currently **read-only** extracts and opens. No auto write-back after edit.

Implementation hints:
- Monitor file updates in temp path via `ReadDirectoryChangesW`
- Detect change → confirmation dialog → rebuild archive (same path as add/update to existing archive)
- Replace only target entry via `IOutArchive::UpdateItems` like delete feature

---

## Low Priority / Niche

### 13. Themes / skins — `L`
Customization beyond dark mode. Low priority.

### 14. Settings import/export — `S`
Copying `AileEx.ini` works, but UI "Export / Import" commands would be nice.

### 15. Plugin system — `L`
Custom format support or external compression engine calls. Total Commander WCX plugin compatibility has demand but large effort.

### 16. Logging — `S`
Append history of all operations to `AileEx.log`. For diagnosis on trouble.

### 17. Archive contents list export — `S`
Save ListView contents as CSV / TSV / text.

---

## Known small incomplete items

Individual items overlapping with CLAUDE.md "Remaining tasks":

- **Manual test matrix**: Systematically check browse / compress / extract / cancel / drop / SFX for each format
- **Error handling comprehensive review**: HRESULT handling, consistency of error messages shown to user
- **`RarProcess::Delete` cancel path**: Currently can't cancel during `rar.exe d`. Extend `Cancel()` (TerminateProcess) for use in Delete too
- **Header-encrypted 7z delete password retention**: Currently fails on `Open` (password not retained). Need path to retain password once opened and pass to `DeleteItems`

---

## Known limitations (difficult to implement / unforeseen)

These are **spec-level constraints**, no implementation planned for now:

- **Individual extraction from solid archives** — 7z.dll limitation. Full extraction only is efficient way
- **Split creation for gz / bz2 / xz / tar** — Format specs require non-seekable output, unsupported
- **Multi-archive simultaneous browse (tabs)** — UI structure major redesign needed, low priority
