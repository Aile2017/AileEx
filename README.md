# AileEx

Windows 向けアーカイブマネージャ GUI アプリケーション。  
7z.dll をバックエンドとし、Win32 API / C++17 で実装されています。

## 特徴

- **多形式対応**: 7z / ZIP / RAR / TAR / GZip / BZip2 / XZ / CAB / ISO など
- **閲覧・展開・圧縮** をひとつのウィンドウで操作
- **ドラッグ＆ドロップ** でアーカイブを開く、またはファイルを圧縮
- **パスワード保護**・圧縮レベル選択に対応
- **DPI 対応** (Per-Monitor V2)

## 動作環境

| 項目 | 内容 |
|---|---|
| OS | Windows 10 / 11 (x64) |
| 言語 | C++17 / Win32 API |
| ビルドシステム | CMake 3.20+ / MSVC 2022+ |

### 実行時に必要な DLL / EXE

| ファイル | 既定パス | 用途 |
|---|---|---|
| `7z.dll` | `C:\Program Files\7-Zip\7z.dll`（または AileEx.exe と同じディレクトリ） | アーカイブ全般 |
| `unrar.dll` (`UnRAR64.dll`) | AileEx.exe と同じディレクトリ | RAR 展開（任意） |
| `rar.exe` | レジストリ `HKLM\SOFTWARE\WinRAR` / `%ProgramFiles%\WinRAR\rar.exe` | RAR 圧縮（任意） |

## ビルド

```powershell
# Debug
cmake -B build
cmake --build build
# 成果物: build\AileEx.exe

# Release
cmake -B build_release -DCMAKE_BUILD_TYPE=Release
cmake --build build_release
# 成果物: build_release\AileEx.exe
```

## 起動モード

| コマンドライン引数 | 動作 |
|---|---|
| 引数なし | メインウィンドウを表示（空状態） |
| アーカイブファイル | ブラウズモード — アーカイブを開いて内容を表示 |
| 通常ファイル | 圧縮モード — 圧縮ダイアログを表示 |
| 混在 | 圧縮モード優先 |

アーカイブと判定する拡張子: `7z`, `zip`, `rar`, `tar`, `gz`, `bz2`, `xz`, `cab`, `iso`, `jar`, `wim`, `lzma`, `lzh`, `arj`

## 主な機能

### ブラウズ（閲覧）

- 左ペイン: TreeView でフォルダ階層を表示
- 右ペイン: ListView でファイル一覧を表示（名前 / サイズ / 圧縮後 / 種類 / 更新日時）
- ListView で複数選択して選択ファイルのみ展開可能

### 圧縮

- 対応形式: 7z / ZIP / TAR / GZip / BZip2 / XZ / **RAR**
- RAR は WinRAR (`rar.exe`) をサブプロセスとして起動
- その他は `7z.dll` の `IOutArchive` を使用
- 圧縮レベル: 0（無圧縮）〜 9（超圧縮）
- メソッド選択（7z: LZMA2/LZMA/PPMd/BZip2/Deflate/Zstandard、ZIP: Deflate/BZip2/LZMA/Zstandard/Store）

### 展開

- `7z.dll` が認識する全形式に対応
- RAR バックエンドは設定で `7z.dll` / `unrar.dll` を切り替え可能
- 一方が失敗した場合はもう一方に自動フォールバック

### キーボードショートカット

| キー | 動作 |
|---|---|
| F5 / Enter / Ctrl+E | 展開 |
| Ctrl+A | ファイル追加（圧縮） |
| Esc | ウィンドウを閉じる |

## 設定

EXE と同じフォルダの `AileEx.ini` に保存されます。設定ダイアログ（ツールバーの設定ボタン）から変更できます。

```ini
[General]
RarExtractor=7z          ; "7z" または "unrar"
DefaultOutputDir=        ; 既定の展開先
DefaultFormat=7z         ; 既定の圧縮形式
CompressionLevel=5       ; 0-9
7zDllPath=               ; 7z.dll の絶対パス（空なら AileEx.exe と同じディレクトリ）
UnrarDllPath=            ; unrar.dll の絶対パス（空なら AileEx.exe と同じディレクトリ）
```

## アーキテクチャ概要

```
main()
  └─ App（シングルトン）─ Settings / SevenZip / UnrarDll
        ├─ MainWindow（ブラウズ）─ ProgressDlg ─ WorkerThread
        └─ CompressDlg（圧縮設定）─ RarProcess（rar.exe）
```

- 圧縮・展開処理はワーカースレッドで実行
- `PostMessage(WM_APP_PROGRESS / WM_APP_DONE)` で UI スレッドへ進捗通知
- キャンセル時は `ProgressPostSink::SetCancelled(true)` または `RarProcess::Cancel()` で中断

## 制限事項

- マルチボリュームアーカイブの分割作成・読み込みは未対応
- ソリッドアーカイブからの個別ファイル展開は不可（7z.dll の制約）
- シェル統合（コンテキストメニュー）は未実装
- 複数アーカイブの同時ブラウズ（タブ等）は未実装

## ドキュメント

- [`docs/specification.md`](docs/specification.md) — 機能仕様
- [`docs/architecture.md`](docs/architecture.md) — クラス構成・スレッドモデル
- [`docs/build.md`](docs/build.md) — ビルド手順詳細
- [`docs/known-issues.md`](docs/known-issues.md) — 既知の落とし穴と回避策
