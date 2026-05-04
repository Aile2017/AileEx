# AileEx 仕様書

7z.dll をバックエンドとする Windows 向けアーカイブマネージャ GUI アプリケーション。

## 概要

| 項目 | 内容 |
|---|---|
| プラットフォーム | Windows 10 / 11 (x64) |
| 言語 | C++17 / Win32 API |
| ビルドシステム | CMake 3.20+ |
| 圧縮バックエンド | 7z.dll (LoadLibrary), unrar.dll (LoadLibrary), rar.exe (CreateProcess) |
| 設定保存 | EXE と同じ場所の INI ファイル |

## 機能

### 起動モード

`AileEx.exe` のコマンドライン引数で動作モードを判定する。

| 引数 | 動作 |
|---|---|
| 引数なし | メインウィンドウを表示（空状態） |
| アーカイブファイル | ブラウズモード — メインウィンドウでアーカイブを開く |
| 通常ファイル | 圧縮モード — 圧縮ダイアログを開く |
| 混在 | 圧縮モード優先 |

判定対象拡張子（アーカイブと判定）: `7z`, `zip`, `rar`, `tar`, `gz`, `bz2`, `xz`, `cab`, `iso`, `jar`, `wim`, `lzma`, `lzh`, `arj`

### メインウィンドウ

- 左ペイン: TreeView (フォルダ階層、固定幅 220px)
- 右ペイン: ListView (ファイル一覧、列: 名前/サイズ/圧縮後/種類/更新日時)
  - **列ヘッダクリック**で昇順/降順ソート（再クリックで反転）
- ペイン間に**スプリッタ**あり（マウスでドラッグして幅を調整）
- 上部: ツールバー（展開・追加・設定）
- 下部: ステータスバー（エントリ数・進捗表示）
- ドラッグ&ドロップ対応:
  - アーカイブをドロップ → 開く
  - 通常ファイルをドロップ → 圧縮ダイアログ表示

### 圧縮機能

- 対応形式: 7z / ZIP / TAR / GZip / BZip2 / XZ / RAR
- RAR は WinRAR (rar.exe) のサブプロセスを起動
- それ以外は 7z.dll の `IOutArchive` を使用
- 圧縮レベル: 0 (無圧縮) / 1 / 3 / 5 / 7 / 9 (超圧縮)
- メソッド選択:
  - 7z: LZMA2 / LZMA / PPMd / BZip2 / Deflate / Zstandard / Brotli / LZ4 / LZ5 / Lizard / FastLZMA2
  - ZIP: Deflate / BZip2 / LZMA / Zstandard / Brotli / LZ4 / Store
  - ロード済み 7z.dll が持つエンコーダーのみ表示（`GetNumberOfMethods` / `GetMethodProperty` で動的列挙）
- **詳細オプション**ダイアログ（「詳細」ボタン）: 辞書サイズ / ワードサイズ / ソリッドブロックサイズ / スレッド数 / 追加パラメーター
  - RAR 形式選択時は RAR 専用詳細オプション: 圧縮方式 / リカバリレコード / ヘッダ暗号化
- パスワード保護対応（7z はヘッダ暗号化オプションあり）
- 形式変更時に出力パスの拡張子を自動補正

### 展開機能

- 対応形式: 7z.dll が認識する全形式（7z/ZIP/RAR/TAR/GZ/BZ2/XZ/CAB/ISO/...）
- RAR バックエンドの自動切替: 設定で `7z.dll` または `unrar.dll` を選択可能
- 一方のバックエンドが失敗した場合、もう一方に自動フォールバック
- ファイル選択展開（ListView で複数選択 → 選択分のみ展開）
  - **7z.dll バックエンド使用時のみ有効**。選択なしの場合は全ファイルを展開
  - **unrar.dll バックエンド使用時は選択状態によらず常に全ファイルを展開**（部分展開は未対応）
- 全展開（選択なしで F5/Ctrl+E）

### 設定

INI ファイル（`AileEx.exe` と同じ場所、ファイル名は `AileEx.ini`）に保存。

```ini
[General]
RarExtractor=7z              ; "7z" または "unrar"
DefaultOutputDir=             ; 既定の展開先
DefaultFormat=7z              ; 既定の圧縮形式
CompressionLevel=5            ; 0-9
7zDllPath=                    ; 7z.dll の絶対パス（空ならレジストリ自動検出 → AileEx.exe と同じディレクトリ）
UnrarDllPath=                 ; unrar.dll の絶対パス（空なら AileEx.exe と同じディレクトリ）
RarExePath=                   ; WinRAR.exe / Rar.exe の絶対パス（空ならレジストリ自動検出）
WindowX=                      ; ウィンドウ位置・サイズ（自動保存）
WindowY=
WindowW=
WindowH=
SplitterPos=                  ; スプリッタ位置（自動保存）
```

設定ダイアログから変更可能。OK で保存後、DLL は自動再読み込み。
フォルダ選択は Explorer スタイルのダイアログ（`IFileOpenDialog + FOS_PICKFOLDERS`）を使用。

### Info ダイアログ

ListView でファイルを選択して **i キー** または **メニューから情報** を開くと、選択したエントリの詳細情報を表示する（パス、サイズ、圧縮後サイズ、メソッド、更新日時など）。

### キーボードアクセラレータ

| キー | 動作 |
|---|---|
| F5 / Enter / Ctrl+E | 展開 |
| Ctrl+A | ファイル追加（圧縮） |
| Esc | ウィンドウを閉じる |

### DPI 対応

- マニフェストで `dpiAware = PerMonitorV2` を宣言
- `wWinMain` で `SetProcessDpiAwarenessContext` を動的呼び出し（Windows 8.1 以下では無視）

## 進捗ダイアログ

- モーダル（親ウィンドウ無効化）
- プログレスバー + 現在ファイル名 + キャンセルボタン
- ワーカースレッドからは `PostMessage(WM_APP_PROGRESS, percent, (LPARAM)wcsdup(filename))` で通知
- 完了時は `PostMessage(WM_APP_DONE, hr, 0)`
- キャンセル: `ProgressPostSink::SetCancelled(true)` または RAR の場合 `RarProcess::Cancel()`

## 主要 Win32 メッセージ

| メッセージ | 用途 |
|---|---|
| `WM_APP_PROGRESS` (`WM_APP+1`) | wParam=パーセント, lParam=ファイル名 (`free()` 必要) |
| `WM_APP_DONE` (`WM_APP+2`) | wParam=HRESULT |
| `WM_DROPFILES` | ファイルドロップ受信 |

## 制限事項・未実装

- マルチボリュームアーカイブの分割作成・読み込みは未対応
- ソリッドアーカイブからの個別展開は不可（7z.dll の制約）
- シェル統合（コンテキストメニュー）は未実装
- 複数アーカイブ同時ブラウズ（タブ等）は未実装
- Phase 9 の手動テストマトリクスは部分実施（RAR 系のみ確認済み）
