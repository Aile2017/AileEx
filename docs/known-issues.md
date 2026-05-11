# Known Pitfalls and Historical Bugs

Record of traps encountered during development and their workarounds. Prevention against hitting the same issue again.

## 7-Zip Format CLSID

Format CLSID passed to 7z.dll's `CreateObject` is identified by byte `XX` in `{23170F69-40C1-278A-1000-00011000XX0000}`. **This `XX` value can differ from SDK documentation and old sample code.**

Measured values (verified with 7-Zip 26.00 ZS, enumerated via `GetNumberOfFormats` / `GetHandlerProperty2`):

| Name | byte | Extensions |
|---|---|---|
| 7z   | `0x07` | 7z |
| Zip  | `0x01` | zip jar ... |
| BZip2 | `0x02` | bz2 |
| Tar  | `0xEE` | tar |
| GZip | `0xEF` | gz |
| Xz   | `0x0C` | xz |
| Cab  | `0x08` | cab |
| Iso  | `0xE7` | iso |
| Rar  | `0x03` | rar (RAR 1.5–4.x) |
| **Rar5** | **`0xCC`** | rar (RAR 5+) |
| Arj  | `0x04` | arj |

**Warning:** Old sources sometimes list Rar5 byte as `0x04`, but this is **incorrect**. `0x04` is Arj. Implementing `Rar5 = 0x04` causes `CreateObject` to return Arj handler, which doesn't recognize RAR files and returns `S_FALSE`, resulting in a hard-to-debug bug.

When using a different DLL build, enumerate and verify with `GetNumberOfFormats` and `GetHandlerProperty2` at startup for safety.

## IInArchive::Open Return Values

`archive->Open()` returns `S_FALSE` on format mismatch. Since `FAILED(S_FALSE) == false`, error checking must test both `FAILED(hr) || hr == S_FALSE`. Conversion to `E_FAIL` is caller's responsibility.

## RAR4 / RAR5 Routing

Cannot distinguish RAR4 / RAR5 by `.rar` extension alone (magic bytes required). Initial implementation tries RAR5 handler and falls back to RAR4 on `S_FALSE`. Must fallback not only when `archive->Open` returns `S_FALSE`, but also on `FAILED(hr)`.

## COutFileStream Requires IOutStream

Writing 7z archives requires **seekable** `IOutStream` to rewrite headers later. Implementing `ISequentialOutStream` alone produces empty archives. Must also implement `IOutStream::Seek` and `SetSize`. After `SetSize` is called, restore file pointer to original position (otherwise subsequent Writes corrupt data).

## RAR Compression Routing

Two paths exist: `App::RunCompressMode()` (via command-line args) and `MainWindow::OnCompress()` (via drag-drop / Add button). **Both are consolidated via `RunRarCompressSync()` in `CompressHelper.h`** — always calls `RarProcess::Compress`. Since `SevenZip::FormatToOutGuid("rar")` is unsupported and falls back to `CLSID_Format_7z`, consolidating to single path prevents accidental 7z output (safety measure).

## unrar.dll `RARHeaderDataEx` Struct

Applying `#pragma pack(push, 1)` causes `CmtBuf` (8-byte pointer) to misalign from 8-byte boundary (after `FileAttr`, where 4 bytes padding should be). Result: unrar.dll writes 4 bytes past struct boundary, causing stack overflow. **Define with default alignment without `#pragma pack`.**

## unrar.dll Path Separator

`RARHeaderDataEx::FileNameW` uses **backslash** separator. To unify with `SevenZip::OpenArchive` convention (forward slash), normalize in `UnrarDll::ListArchive`:

```cpp
for (auto& c : it.path) if (c == L'\\') c = L'/';
while (!it.path.empty() && it.path.back() == L'/') it.path.pop_back();
```

Without normalization, TreeView/ListView folder routing logic breaks.

## Manifest Embedding

CMakeLists.txt specifies `target_link_options(AileEx PRIVATE "/MANIFEST:NO")` to suppress linker auto-generation. Manifest embedded from `res/AileEx.rc` via `1 RT_MANIFEST "manifest.xml"`. Both would cause duplicate resource error.

## DPI Support

Manifest's `dpiAwareness = PerMonitorV2` alone OK for Windows 10+. For older Windows, dynamically call `SetProcessDpiAwarenessContext` with `GetProcAddress` at start of `wWinMain`. Double declaration with manifest is harmless (manifest takes priority).

## `WM_INITMENUPOPUP` System Menu Exclusion

`WM_INITMENUPOPUP` fires not only on menu bar popups, but also on **system menus like title bar right-click**. `HIWORD(lParam) != 0` is the system menu flag, so must exclude with `if (HIWORD(lp) == 0) OnInitMenuPopup(...)`. Otherwise, `EnableMenuItem(ID_DELETE, ...)` etc. get called on system menu causing nonsensical state (harmless in practice but passes unfound IDs repeatedly with `MF_BYCOMMAND`).

## Menu Label `&` Escaping

In `AppendMenuW` strings, `&` **underlines (accelerator) the next character**. When **dynamically inserting file paths in menus** (e.g., MRU), paths with `&` (example: `C:\Tools\AT&T\foo.7z`) appear corrupted if treated literally as accelerators. Must escape `&` to `&&`.

## `ProgressPostSink` Throttling

7z.dll callbacks invoke progress very frequently. Calling `PostMessage(WM_APP_PROGRESS, ...)` per callback overflows message queue, **canceling cancel button click is delayed/discarded, making cancellation appear impossible**. Must check `GetTickCount` in `ProgressPostSink::OnProgress` to **throttle to ~20Hz** (filter via `m_lastPostTick`).

## `IsDialogMessageW` Limited to VK_TAB Only

Calling `IsDialogMessageW(hwnd, &msg)` on all `WM_KEYDOWN` in message loop consumes `WM_SYSKEYDOWN` internally, **disabling menu mnemonics like Alt+F** (becomes two-step: Alt alone, then F). Restrict to tab navigation only: `if (msg.message == WM_KEYDOWN && msg.wParam == VK_TAB)`.

## `unrar.dll TestArchive` Cancel Return Value

`UnrarDll::TestArchive` converts `RARProcessFileW(... RAR_TEST ...)` return to simple true/false, **so user cancellation indistinguishable from failure (false)**. In `MainWindow::OnTest`, check `sink->IsCancelled()` to normalize to `E_ABORT` equivalent, suppressing error dialog.

## `ForceForeground` (Foreground Theft)

When parent process already exited (e.g., launcher-spawned), `SetForegroundWindow` alone gets demoted by Windows focus restriction. Two-step: `AttachThreadInput(myTid, fgTid, TRUE)` to attach foreground app's thread, momentarily add `HWND_TOPMOST` to push Z-order, then call `SetForegroundWindow` (`ForceForeground` namespace function in `MainWindow.cpp`).

## RAR Delete Cancel Path

`RarProcess::Delete` (= `rar.exe d`) currently **has no cancel path implemented**. Via 7z.dll (`SevenZip::DeleteItems`), `CDeleteCallback::SetCompleted` can return `E_ABORT` to cancel, but RAR path doesn't return until process completes. Must extend `RarProcess::Cancel()` (TerminateProcess) for use in Delete as well (TODO).

## Self-Extracting (SFX) Module Location

7z SFX is simple: read `7z.sfx` (GUI) or `7zCon.sfx` (console) from **same directory as `7z.dll`**, then prepend to compressed .7z data. SDK doesn't include SFX modules; they come with standard 7-Zip install (e.g., Files\7-Zip). Search by deriving parent dir from DLL full path obtained via `SevenZip::GetLoadedPath()` (`CompressHelper::Resolve7zSfxModulePath`).

RAR specified via `rar.exe -sfx<modulePath>`. WinRAR / Rar.exe bundles `Default.SFX` (GUI) and `WinCon.SFX` (console). 32-bit modules (`Zip.SFX` etc.) excluded.

Notes:
- When combining split volumes and SFX, prepend SFX module only to **volume 1** (`.001`); volumes 2+ are normal. `CMultiVolOutStream::FinalizeWithSfx` handles this difference.
- Mixing 7z SFX console/GUI changes runtime behavior but doesn't affect build/extraction (both are valid PE stubs).
- If SFX module detection fails, abort with error without creating `.7z` / `.rar` (`Resolve*SfxModulePath` returns empty string → caller displays `MessageBox`). Do not implement fallback to "just create normal .7z" (confuses users).

## 7z.dll Split Writing is Host Responsibility

7z.dll format handlers (`7zHandlerOut.cpp` etc.) simply **write directly to stream** passed to `UpdateItems(outStream, ...)`. Split logic (switch to next file every N MB) **not in DLL**. `IArchiveUpdateCallback2::GetVolumeSize/GetVolumeStream` interface exists, but it's called by 7-Zip CLI / 7zFM's **self-implemented split stream** (`COutMultiVolStream`), not by handler.

So AileEx also self-implements `CMultiVolOutStream` (`SevenZip.cpp`) for split writing, passing it as `IOutStream` to `UpdateItems`. 7z.dll sees single seekable stream.

Notes:
- `IOutStream::Seek` requires **global offset** ⇄ (volIdx, volOffset) mapping (7z.dll frequently seeks near start for header writing)
- `IOutStream::SetSize` called with final archive size, so truncate boundary volume and delete after
- Keep HANDLE for each volume (Seek may return to past volumes)

## Split Archive Reading is Split Handler + `IArchiveOpenVolumeCallback`

Opening `archive.7z.001`, 7z.dll **selects Split handler from extension map**. Split handler requests `archive.7z.002`, `.003`, ... from host via `IArchiveOpenVolumeCallback::GetStream`, builds concatenated stream internally, then passes to actual handler (e.g., 7z). `COpenVolumeCallback` (`SevenZip.cpp`) simply opens matching file from same dir and returns it. If requested file doesn't exist, return `S_FALSE` (DLL treats as final volume signal).

Volume 1 detection in `OpenArchive`: if extension is **all digits** (`001`, `002` etc.), treat as split archive and pass volume callback. RAR's `.partN.rar` has `.rar` extension, so unrar.dll / 7z.dll RAR handler resolves next volume internally (callback unnecessary).

## RAR 4 CJK Filename Encoding Limitation

unrar.dll converts RAR 4 archive filenames (stored in local code page) to UTF-16 via `RARHeaderDataEx::FileNameW`. However, WinRAR 5.0+ no longer supports creation of RAR 4 archives, making testing with modern tools impossible. If legacy RAR 4 archives with CJK filenames are encountered and exhibit corruption/garbling, the root cause lies in unrar.dll's code page conversion, which is beyond AileEx control. Workaround: convert to RAR 5 (which uses full Unicode) or 7z format.
