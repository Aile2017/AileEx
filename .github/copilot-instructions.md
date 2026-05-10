# AileEx — Copilot Instructions

## Project Overview

A Windows archive manager GUI with 7z.dll as the backend. C++17 + Win32 API.
See `docs/specification.md` for the full functional specification.

## Environment

- **OS**: Windows 11 (x64)
- **Compiler**: MSVC (Visual Studio 18/2026 Community)
- **Build commands**: `cmake --build build` (Debug) / `cmake --build build_release` (Release)
- **Compiler PATH** (PowerShell):
  ```powershell
  $env:PATH = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64;" + $env:PATH
  ```

## Key Documentation

| File | Content |
|---|---|
| `docs/specification.md` | Functional specification (startup modes, UI, settings, supported formats) |
| `docs/architecture.md` | Module structure, class relationships, thread model |
| `docs/build.md` | Build instructions, required runtime DLLs |
| `docs/known-issues.md` | **Read before implementing** — record of past pitfalls |
| `docs/compress-extra-params.md` | `key=value` parameters for advanced compression (7z.dll `ISetProperties`) |
| `docs/rar-extra-params.md` | rar.exe switches for advanced RAR settings |
| `docs/roadmap.md` | Unimplemented features with priority/hints/effort estimates |

## Code Conventions

- **Comments**: English. Write WHY, not WHAT.
- **Member variables**: `m_` prefix
- **COM methods**: use `STDMETHODCALLTYPE`, return `HRESULT` appropriately
- **New source files**: must be added to `CMakeLists.txt` `add_executable` list (CMake does not glob)
- **Headers**: use `#pragma once`

## Critical Design Patterns

1. **7-Zip format CLSID**: Do not trust SDK header constants. Validate CLSIDs at runtime with `GetNumberOfFormats`. Rar5 is `0xCC`, not `0x04` (which is Arj).
2. **`IInArchive::Open`** returns `S_FALSE` on format mismatch. Check `FAILED(hr) || hr == S_FALSE`.
3. **`COutFileStream`** requires `IOutStream` (seekable). After `SetSize`, rewind the file pointer.
4. **unrar.dll `RARHeaderDataEx`** does not use `#pragma pack` — define with default alignment.
5. **unrar.dll paths** use backslashes — normalize to forward-slash style like SevenZip.
6. **RAR compression** is centralized in `CompressHelper::RunRarCompressSync()`. Any new compression entry point must call this helper.
7. **SFX (self-extraction)**: 7z prepends `7z.sfx`/`7zCon.sfx` from the 7z.dll directory. RAR uses `rar.exe -sfx<module>` with `Default.SFX`/`WinCon.SFX` from rar.exe directory. For split volumes, prepend to the **first volume only** (`CMultiVolOutStream::FinalizeWithSfx`). If SFX module is not found, **error and abort** — no fallback.

## Worker Thread Pattern

```
UI Thread                   Worker Thread
─────────────               ─────────────
WorkerThread::Start(task)
  → CreateThread            task() executes
                            sink->OnProgress(...)
                              → PostMessage(WM_APP_PROGRESS, pct, _wcsdup(file))
WM_APP_PROGRESS received
  → ProgressDlg.SetProgress
  → free((wchar_t*)lParam)  ← MUST free!
                            task() returns
                              → PostMessage(WM_APP_DONE, hr, 0)
WM_APP_DONE received
  → ProgressDlg.Dismiss
worker.Wait()
delete sink
```

- `WM_APP_PROGRESS` `lParam` is `_wcsdup`'d `wchar_t*` — receiver must `free()` it.
- Cancellation: `sink->SetCancelled(true)` → callback returns `E_ABORT`.
- RAR cancellation: `RarProcess::Cancel()` calls `TerminateProcess`.

## Adding a New Setting

1. Add `m_xxx`, `GetXxx()`, `SetXxx()` to `Settings.h`
2. Add `ReadStr`/`WriteStr` to `Settings.cpp` `Load()`/`Save()`
3. Add control to `SettingsDlg` resource in `AileEx.rc`, add ID to `resource.h`
4. Handle in `SettingsDlg.cpp` `OnInit` (load) and `OnOK` (save)
5. If needed, handle reload in `App::ReloadDlls`

## Adding a New Archive Format

Read operations work automatically if 7z.dll supports the format (via dynamic `m_extToClsid` enumeration). Manual changes needed for:

1. Add extension to `kArchiveExts[]` in `main.cpp` (browse mode detection)
2. **Write support**: Add to `SevenZip.cpp` `FormatToOutGuid` static fallback; add entry to `kFormats[]` in `CompressDlg.cpp`
3. **Dynamic enumeration unavailable (rare)**: Add `Z7_FMT_GUID(...)` to `sdk/7zip/Archive/IArchive.h`; add branch to `FormatToInGuid` static fallback

To find a CLSID: recover `EnumerateFormats` from git history and run `GetNumberOfFormats`.

## Diagnostics

```powershell
# Check magic bytes of a file
$bytes = [System.IO.File]::ReadAllBytes("path") | Select-Object -First 16
($bytes | ForEach-Object { $_.ToString("X2") }) -join " "
```

- Verify suspect archives with `7z.exe l <file>`
- Add `OutputDebugStringW` or log to `%TEMP%\aileex_debug.log` in `CInFileStream::Read`/`Seek`
- Diagnostic logging implementations exist in git history

## Codec Enumeration

After 7z.dll loads, call `SevenZip::EnumerateCodecs()` (uses `GetNumberOfMethods`/`GetMethodProperty`).
CompressDlg filters out unsupported codecs via a `supportsEncoder` lambda.

- PropID: `kName=1`, `kEncoderIsAssigned=8`
- Aliases: `store` ↔ `copy` (ZIP Store), `zstd` ↔ `zstandard`

## Folder Selection Dialog

Use `IFileOpenDialog + FOS_PICKFOLDERS + FOS_FORCEFILESYSTEM` (not deprecated `SHBrowseForFolder`).
Required header: `<shobjidl_core.h>`. Pre-select current folder via `SHCreateItemFromParsingName` → `SetFolder()`.

## GitHub Actions / Release

Workflow: `.github/workflows/package-release.yml`
- Triggered by `workflow_dispatch`
- Uses `ilammy/msvc-dev-cmd` for MSVC x64 → CMake Release build
- Packages `AileEx.exe` + `README.md` into ZIP; publishes release via `softprops/action-gh-release`
- Tag format: `AileEx_{version}_{yyyyMMdd}`

## Build Troubleshooting

```powershell
# See last 30 lines of build output
cmake --build build 2>&1 | Select-Object -Last 30

# Regenerate CMake cache if stale
cmake -B build
```

SDK header issues: check `sdk/7zip/`.
