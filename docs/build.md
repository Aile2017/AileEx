# ビルド手順

## 前提

- Windows 10/11
- Visual Studio 2022 以降（MSVC コンパイラ）
- CMake 3.20+
- Ninja または NMake (CMake から呼び出される)

## 動作環境

- 7-Zip がインストールされていること（`C:\Program Files\7-Zip\7z.dll` を既定で参照）
- RAR 関連機能を使う場合は WinRAR / unrar.dll

## ビルドコマンド

### Debug

```powershell
cd C:\Users\asano\Desktop\workspace\AileEx
$env:PATH = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64;" + $env:PATH
cmake -B build
cmake --build build
```

成果物: `build\AileEx.exe`

### Release

```powershell
$env:PATH = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64;" + $env:PATH
cmake -B build_release -DCMAKE_BUILD_TYPE=Release
cmake --build build_release
```

成果物: `build_release\AileEx.exe`

## CMake 主要設定

```cmake
add_executable(AileEx WIN32 ...)
target_compile_definitions(AileEx PRIVATE WIN32_LEAN_AND_MEAN UNICODE _UNICODE NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_link_libraries(AileEx PRIVATE comctl32 shlwapi shell32 ole32 oleaut32 advapi32 comdlg32)
target_compile_options(AileEx PRIVATE /W3 /utf-8)
target_link_options(AileEx PRIVATE "/MANIFEST:NO")  # マニフェストは AileEx.rc から埋込
```

## 実行時の DLL

| DLL / EXE | 既定パス | 用途 |
|---|---|---|
| `7z.dll` | レジストリ `HKLM\SOFTWARE\7-Zip` の `Path64`/`Path` から自動検出 → なければ `%ProgramFiles%\7-Zip\7z.dll` → AileEx.exe と同じディレクトリ | アーカイブ全般 |
| `unrar.dll` (`UnRAR64.dll`) | AileEx.exe と同じディレクトリ | RAR 展開（任意） |
| `WinRAR.exe` / `Rar.exe` | レジストリ `HKLM\SOFTWARE\WinRAR` の `exe32` のディレクトリから `WinRAR.exe`（GUI 優先）→ `Rar.exe`、なければ `%ProgramFiles%\WinRAR\` から同じ順で検索 | RAR 圧縮 |

すべて設定ダイアログでパスを上書き可能。`WinRAR.exe` を選んだ場合は GUI モード（独自進捗ウィンドウ）、`Rar.exe` を選んだ場合は stdout 解析でプログレスバーに反映。
