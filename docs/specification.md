# AileEx 仕様書

7z.dll をバックエンドとする Windows 向けアーカイブマネージャ GUI アプリケーション。

## 概要

| 項目 | 内容 |
|---|---|
| プラットフォーム | Windows 10 / 11 (x64) |
| 言語 | C++17 / Win32 API |
| ビルドシステム | CMake 3.20+ |
| 圧縮バックエンド | 7z.dll (LoadLibrary), unrar.dll (LoadLibrary), rar.exe / WinRAR.exe (CreateProcess) |
| 設定保存 | EXE と同じ場所の INI ファイル (`AileEx.ini`) |

## 機能

### 起動モード

`AileEx.exe` のコマンドライン引数で動作モードを判定する。

| 引数 | 動作 |
|---|---|
| 引数なし | メインウィンドウを表示（空状態） |
| アーカイブファイル | ブラウズモード — メインウィンドウでアーカイブを開く |
| 通常ファイル | 圧縮モード — 圧縮ダイアログを開く |
| 混在 | 圧縮モード優先 |

判定対象拡張子（アーカイブと判定）: `7z`, `zip`, `rar`, `tar`, `gz`, `bz2`, `xz`, `cab`, `iso`, `jar`, `wim`, `lzma`, `lzh`, `arj` ほか 7z.dll が動的列挙したフォーマット。

### メインウィンドウ

- **メニューバー** (`IDR_MAIN_MENU`): ファイル / 操作 / 表示 / ヘルプ
- 左ペイン: TreeView (フォルダ階層)
- 右ペイン: ListView (ファイル一覧、列: 名前 / サイズ / 圧縮後 / 種類 / 更新日時)
  - **列ヘッダクリック**で昇順/降順ソート（再クリックで反転）
- ペイン間に**スプリッタ**あり（マウスでドラッグして幅を調整、INI に位置保存）
- 上部: ツールバー (展開 / 関連付けで開く / 追加 / 情報 / テスト / 設定) — **表示メニューでトグル可能**
- 下部: ステータスバー（エントリ数・ロード済 DLL 名・進捗パーセント / カレントファイル）
- ドラッグ&ドロップ対応:
  - アーカイブをドロップ → 開く
  - 通常ファイルをドロップ → 圧縮ダイアログ表示
- ListView 上で **右クリックコンテキストメニュー**: 選択ファイル展開 / 関連付けで開く / テスト / 情報 / 削除

#### メニュー構成

| トップ | 項目 | コマンド ID | 備考 |
|---|---|---|---|
| ファイル(F) | 開く(O)... `Ctrl+O` | `IDM_FILE_OPEN` | `IFileOpenDialog` |
| | 最近使ったアーカイブ(R) | `IDM_FILE_MRU_BASE..LAST` | 最大 10 件、`&1..&9` ニーモニック |
| | 閉じる(C) `Ctrl+F4` | `ID_CLOSE` | アーカイブを閉じる（アプリ終了ではない）|
| | 終了(X) `Esc` | `IDM_FILE_EXIT` | アプリ終了 |
| 操作(A) | 展開(E)... `F5` | `ID_EXTRACT` | |
| | 追加(A)... `Ctrl+A` | `ID_ADD` | 圧縮 |
| | テスト(T) `Ctrl+T` | `ID_TEST` | 整合性検証 |
| | 関連付けで開く(O) `Enter` | `ID_OPEN_ASSOC` | 一時展開後 `ShellExecute` |
| | 削除(D) `Del` | `ID_DELETE` | 7z は `IOutArchive`、RAR は `rar d` |
| | 情報(I) | `ID_INFO` | 選択エントリ詳細 |
| | 設定(S)... | `ID_SETTINGS_DLG` | |
| 表示(V) | ツールバー(B) | `IDM_VIEW_TOOLBAR` | チェックトグル |
| | ツリー表示(T) | `IDM_VIEW_TREE` | チェックトグル |
| ヘルプ(H) | バージョン情報(A)... | `IDM_HELP_ABOUT` | About ダイアログ |

`WM_INITMENUPOPUP` でアーカイブの状態 (開いているか / read-only / 選択数) に応じて `EnableMenuItem` / `CheckMenuItem` で動的更新。

### 圧縮機能

- 対応形式: 7z / ZIP / TAR / GZip / BZip2 / XZ / RAR
- RAR は WinRAR.exe (GUI) または Rar.exe (console) のサブプロセスを起動
  - 7z.dll は RAR 書き込みをサポートしない（`FormatToOutGuid("rar")` は対応せず CLSID_Format_7z にフォールバック）。**RAR 圧縮経路は `CompressHelper::RunRarCompressSync()` に集約済み**
- それ以外は 7z.dll の `IOutArchive` を使用
- 圧縮レベル: 0 (無圧縮) / 1 / 3 / 5 / 7 / 9 (超圧縮)。RAR は 0..5
- メソッド選択:
  - 7z: LZMA2 / LZMA / PPMd / BZip2 / Deflate / Zstandard / Brotli / LZ4 / LZ5 / Lizard / FastLZMA2
  - ZIP: Deflate / BZip2 / LZMA / Zstandard / Brotli / LZ4 / Store
  - ロード済み 7z.dll が持つエンコーダーのみ表示（`GetNumberOfMethods` / `GetMethodProperty` で動的列挙）
- **詳細オプション**ダイアログ (`IDD_COMPRESS_ADV`): 辞書サイズ / ワードサイズ / ソリッドブロックサイズ / スレッド数 / **分割ボリューム** / 追加パラメーター
- **RAR 詳細オプション**ダイアログ (`IDD_RAR_COMPRESS_ADV`): 辞書サイズ / ソリッド / スレッド数 / リカバリレコード / 分割ボリューム / 追加パラメーター
- **分割ボリューム** (`volumeSize`): 7z / ZIP のみ対応。`archive.7z.001` / `archive.7z.002` ... と分割。RAR は `rar.exe -v<size>` を使用
- パスワード保護対応（7z はヘッダ暗号化オプションあり）
- 形式変更時に出力パスの拡張子を自動補正
- 詳細オプションは last-used 値が INI に永続化される

### 展開機能

- 対応形式: 7z.dll が認識する全形式（7z/ZIP/RAR/TAR/GZ/BZ2/XZ/CAB/ISO/...）
- **分割アーカイブ** (`archive.7z.001` / `.002` / ...): 第1巻 (`.001`) を開けば 7z.dll の Split ハンドラ + `IArchiveOpenVolumeCallback` で連結読込
  - 自動アンラップ: 結果が「1 ファイル + アーカイブ拡張子」の場合、内部アーカイブを一時ファイルへ展開して再オープンし**中身のエントリを直接表示**する
  - 自動アンラップされた split アーカイブは **read-only** (削除メニューは無効化)
- **RAR マルチボリューム** (`archive.partN.rar` / `.r00` `.r01`): 第1巻を開けば unrar.dll / 7z.dll が DLL 内部で連結読込
- RAR バックエンドの自動切替: 設定で `7z.dll` または `unrar.dll` を選択可能
- 一方のバックエンドが失敗した場合、もう一方に自動フォールバック
- ファイル選択展開（ListView で複数選択 → 選択分のみ展開）
  - **7z.dll バックエンド使用時のみ有効**。選択なしの場合は全ファイルを展開
  - **unrar.dll バックエンド使用時は選択状態によらず常に全ファイルを展開**
- 全展開（選択なしで F5/Ctrl+E）
- **サブフォルダ作成ポリシー** (`MkDir` 設定):
  - `0` = 作成しない
  - `1` = 単一ファイルのアーカイブの時のみ
  - `2` = 複数エントリの時に作成（既定）
  - `3` = 常に作成
  - サブフォルダ名はアーカイブ名から複合拡張子（`.tar.gz` など）を除去した値

### テスト機能 (`ID_TEST`)

選択中のアーカイブの整合性をその場で検証する。展開ファイルは作成しない。
- 7z バックエンド: `IInArchive::Extract` に `testMode=1` を渡す
- unrar バックエンド: `RARProcessFileW` に `RAR_TEST` を渡す
- 検証中は `ProgressDlg` を表示。キャンセル可能。完了時にメッセージボックスで結果通知

### 削除機能 (`ID_DELETE`)

ListView で選択したエントリ（フォルダ選択時は配下も含む）をアーカイブから削除する。
- 7z バックエンド: `IOutArchive::UpdateItems` で残すエントリだけを `.~tmp` ファイルへ書き、`MoveFileExW(MOVEFILE_REPLACE_EXISTING)` で原ファイルを置換。**残すエントリは `newData=0/newProperties=0/indexInArchive=oldIdx` で渡すため、再エンコードなしで圧縮ブロブを丸コピー**（パスワード不要）
- RAR バックエンド: `rar.exe d -y -r <archive> <path1> ...`
- 書き込み未対応フォーマット (ISO / CAB / JAR 等) は `IOutArchive` の `QueryInterface` 段階で失敗 (`E_NOINTERFACE`)
- ヘッダ暗号化された 7z は `IInArchive::Open` 段階で失敗（パスワード保持していないため）

### 最近使ったアーカイブ (MRU)

- 最大 **10 件**
- 開くたびに先頭へ追加（既存パスは `_wcsicmp` で大文字小文字無視デデュープ）
- 開けないファイルを履歴から開いた場合、警告 → 履歴から削除
- INI の `[Mru]` セクションに `Path0..Path9` で保存
- **ファイル → 最近使ったアーカイブ** サブメニューに動的構築。`&1..&9` ニーモニック付き

### 設定

INI ファイル（`AileEx.exe` と同じ場所、ファイル名は `AileEx.ini`）に保存。

```ini
[General]
RarExtractor=7z              ; "7z" または "unrar"
RarExePath=                  ; WinRAR.exe / Rar.exe の絶対パス（空ならレジストリ自動検出）
DefaultOutputDir=            ; 既定の展開先
DefaultFormat=7z             ; 既定の圧縮形式
CompressionLevel=5           ; 0-9
RarLevel=3                   ; RAR 圧縮レベル 0-5
MkDir=2                      ; 展開時のサブフォルダ作成 0/1/2/3
7zDllPath=                   ; 7z.dll の絶対パス（空ならレジストリ自動検出）
UnrarDllPath=                ; unrar.dll の絶対パス（空なら AileEx.exe と同じディレクトリ）

[AdvancedCompress]            ; 詳細オプションダイアログの last-used 値
DictSize=
WordSize=
SolidBlock=
Threads=
Extra=
Volume=                       ; 分割ボリュームサイズ "" / "100m" / "1g" 等

[RarAdvanced]                 ; RAR 詳細オプションの last-used 値
DictSize=
Volume=
Extra=
Solid=1
Threads=0
Recovery=0

[Window]
X=                           ; ウィンドウ位置・サイズ（自動保存）
Y=
W=900
H=600
Maximized=0
Splitter=220                 ; スプリッタ位置
TreeVisible=1                ; ツリー表示トグル
ToolbarVisible=1             ; ツールバー表示トグル

[Mru]                        ; 最近使ったアーカイブ（最大 10 件、Path0 が最新）
Path0=
Path1=
...
Path9=
```

設定ダイアログから変更可能。OK で保存後、DLL は自動再読み込み（`App::ReloadDlls`）。
フォルダ選択は Explorer スタイルのダイアログ（`IFileOpenDialog + FOS_PICKFOLDERS`）を使用。

### Info ダイアログ (`IDD_INFO`)

ListView でファイルを選択して **メニュー → 情報** または右クリック → 情報 を開くと、選択したエントリの詳細情報を表示する（パス、サイズ、圧縮後サイズ、メソッド、更新日時など）。

### About ダイアログ (`IDD_ABOUT`)

**ヘルプ → バージョン情報** で表示。タイトル / バージョン / リンク / クレジットを表示。

### パスワード入力ダイアログ (`IDD_PASSWORD`)

7z 系のヘッダ暗号化アーカイブを開く際、`IInArchive::Open` が失敗した場合に自動表示。入力されたパスワードで再オープンを試行する。

### キーボードアクセラレータ

| キー | コマンド ID | 動作 |
|---|---|---|
| F5 / Ctrl+E | `ID_EXTRACT` | 展開 |
| Ctrl+A | `ID_ADD` | ファイル追加（圧縮）|
| Ctrl+O | `IDM_FILE_OPEN` | アーカイブを開く |
| Ctrl+T | `ID_TEST` | 整合性テスト |
| Del | `ID_DELETE` | エントリ削除 |
| Ctrl+F4 | `ID_CLOSE` | アーカイブを閉じる（アプリは終了しない）|
| Enter | (ListView 内) | フォルダ移動 / `ID_OPEN_ASSOC` |
| Esc | `IDM_FILE_EXIT` | アプリ終了 |

`Alt+F` などのメニューニーモニックも有効。Tab キーは `IsDialogMessageW` でナビゲーション。

### DPI 対応

- マニフェストで `dpiAware = PerMonitorV2` を宣言
- `wWinMain` で `SetProcessDpiAwarenessContext` を動的呼び出し（Windows 8.1 以下では無視）

## 進捗ダイアログ

- モーダル（親ウィンドウ無効化）
- プログレスバー + 現在ファイル名 + 経過時間 + キャンセルボタン
- ワーカースレッドからは `PostMessage(WM_APP_PROGRESS, percent, (LPARAM)wcsdup(filename))` で通知
- 完了時は `PostMessage(WM_APP_DONE, hr, 0)`
- キャンセル: `ProgressPostSink::SetCancelled(true)` または RAR の場合 `RarProcess::Cancel()`
- 進捗 PostMessage は約 20Hz にスロットリング（キャンセルメッセージが埋もれないため）

## 主要 Win32 メッセージ

| メッセージ | 用途 |
|---|---|
| `WM_APP_PROGRESS` (`WM_APP+1`) | wParam=パーセント, lParam=ファイル名 (`free()` 必要) |
| `WM_APP_DONE` (`WM_APP+2`) | wParam=HRESULT |
| `WM_DROPFILES` | ファイルドロップ受信 |
| `WM_INITMENUPOPUP` | メニュー表示直前の状態更新 (`HIWORD(lParam) == 0` でシステムメニュー除外) |

## 制限事項・未実装

- マルチボリュームアーカイブ:
  - 7z / ZIP の分割作成・読み込みに対応 (2026-05-08 実装、`CMultiVolOutStream` + `IArchiveOpenVolumeCallback`)
  - GZ / BZ2 / XZ / TAR は分割未対応 (フォーマット仕様上シーカブル出力でないため)
- ソリッドアーカイブからの個別展開は不可（7z.dll の制約）
- シェル統合（コンテキストメニュー）は未実装
- 複数アーカイブ同時ブラウズ（タブ等）は未実装
- RAR 削除のキャンセル経路は未実装（`RarProcess::Delete` は `Cancel()` 未対応）
- ヘッダ暗号化 7z アーカイブの削除はパスワード保持していないため失敗する
- 手動テストマトリクスは部分実施（RAR 系のみ確認済み）
