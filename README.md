# AileEx

Windows 向けアーカイブマネージャ GUI アプリケーション。  
7z.dll をバックエンドとし、Win32 API / C++17 で実装されています。

## 特徴

- **多形式対応**: 7z / ZIP / RAR / TAR / GZip / BZip2 / XZ / CAB / ISO など
- **閲覧・展開・圧縮・整合性テスト・エントリ削除** をひとつのウィンドウで操作
- **分割ボリューム圧縮 / 分割アーカイブ読み込み**: 7z / ZIP の `archive.7z.001`/`.002`/... を作成・展開可能。RAR は `rar.exe -v<size>` を起動
- **メニューバー** (ファイル / 操作 / 表示 / ヘルプ) と **ツールバー**、ListView 上の右クリックコンテキストメニュー
- **最近使ったアーカイブ** 履歴 (最大 10 件、ファイルメニューに表示)
- **ドラッグ＆ドロップ** でアーカイブを開く、またはファイルを圧縮
- **パスワード保護**・圧縮レベル選択に対応（暗号化アーカイブはオープン時にパスワード自動プロンプト）
- **ListView 列ヘッダクリック**による昇順/降順ソート
- **スプリッタ**でフォルダツリーとファイル一覧の幅を自由に調整、**ツリー/ツールバーの表示トグル**
- ウィンドウサイズ・スプリッタ位置・MRU・詳細圧縮オプションを **INI に自動保存**
- 7-Zip Zstandard DLL が提供する追加コーデック（Brotli / LZ4 / LZ5 / Lizard / FastLZMA2 / Zstandard）を自動認識
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
| `7z.dll` | レジストリ `HKLM\SOFTWARE\7-Zip` から自動検出 → `%ProgramFiles%\7-Zip\7z.dll` → AileEx.exe と同じディレクトリ | アーカイブ全般 |
| `unrar.dll` (`UnRAR64.dll`) | AileEx.exe と同じディレクトリ | RAR 展開（任意） |
| `WinRAR.exe` / `Rar.exe` | レジストリ `HKLM\SOFTWARE\WinRAR` から自動検出 → `%ProgramFiles%\WinRAR\` | RAR 圧縮（任意。WinRAR.exe を優先、なければ Rar.exe）|

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
- **列ヘッダクリック** で昇順 / 降順ソート
- **スプリッタ** で左右ペインの幅を調整可能（位置は INI に自動保存）
- ListView で複数選択して選択ファイルのみ展開可能

### 圧縮

- 対応形式: 7z / ZIP / TAR / GZip / BZip2 / XZ / **RAR**
- RAR は WinRAR (`rar.exe`) をサブプロセスとして起動
- その他は `7z.dll` の `IOutArchive` を使用
- 圧縮レベル: 0（無圧縮）〜 9（超圧縮）
- メソッド選択（7z: LZMA2 / LZMA / PPMd / BZip2 / Deflate / Zstandard / Brotli / LZ4 / LZ5 / Lizard / FastLZMA2、ZIP: Deflate / BZip2 / LZMA / Zstandard / Brotli / LZ4 / Store）
- ロード済みの 7z.dll が対応しないコーデックは自動的にメニューから除外
- **詳細オプション**ダイアログで辞書サイズ・ワードサイズ・ソリッドブロック・スレッド数などを設定可能
- RAR 形式は専用の詳細オプション（圧縮方式 / リカバリレコード / ヘッダ暗号化）を持つ

### 展開

- `7z.dll` が認識する全形式に対応
- RAR バックエンドは設定で `7z.dll` / `unrar.dll` を切り替え可能
  - **`unrar.dll` バックエンドでは常に全ファイルを展開**（ファイル選択展開は `7z.dll` バックエンドのみ有効）
- 一方が失敗した場合はもう一方に自動フォールバック
- **サブフォルダ作成ポリシー** (設定の MkDir): 作成しない / 単一ファイル時のみ / 複数エントリ時（既定）/ 常に作成

### テスト

- 選択中のアーカイブの整合性を検証（展開ファイルは作成しない）
- 7z バックエンドは `IInArchive::Extract(testMode=1)`、unrar バックエンドは `RAR_TEST` を使用

### 削除

- ListView で選択したエントリ（フォルダ選択時は配下も含む）をアーカイブから削除
- 7z 経路は `IOutArchive::UpdateItems` で残すエントリだけを再構築（再エンコードなしで圧縮ブロブを丸コピー）
- RAR 経路は `rar.exe d -y -r` を起動
- 書き込み未対応フォーマット (ISO / CAB / JAR 等) は不可

### キーボードショートカット

| キー | 動作 |
|---|---|
| F5 / Ctrl+E | 展開 |
| Ctrl+A | ファイル追加（圧縮）|
| Ctrl+O | アーカイブを開く |
| Ctrl+T | 整合性テスト |
| Del | 選択エントリを削除 |
| Ctrl+F4 | アーカイブを閉じる（アプリは終了しない）|
| Enter | (ListView 内) フォルダ移動 / 関連付けで開く |
| Esc | アプリ終了 |

## 設定

EXE と同じフォルダの `AileEx.ini` に保存されます。設定ダイアログ（ツールバーの設定ボタン）から変更できます。

```ini
[General]
RarExtractor=7z          ; "7z" または "unrar"
RarExePath=              ; WinRAR.exe / Rar.exe の絶対パス（空ならレジストリ自動検出）
DefaultOutputDir=        ; 既定の展開先
DefaultFormat=7z         ; 既定の圧縮形式
CompressionLevel=5       ; 0-9
RarLevel=3               ; RAR 圧縮レベル 0-5
MkDir=2                  ; 展開時のサブフォルダ作成 0=しない/1=単一/2=複数(既定)/3=常に
7zDllPath=               ; 7z.dll の絶対パス（空ならレジストリ自動検出）
UnrarDllPath=            ; unrar.dll の絶対パス（空なら AileEx.exe と同じディレクトリ）

[Window]
X=                       ; ウィンドウ位置・サイズ（自動保存）
Y=
W=900
H=600
Maximized=0
Splitter=220             ; スプリッタ位置
TreeVisible=1            ; ツリー表示トグル
ToolbarVisible=1         ; ツールバー表示トグル

[Mru]                    ; 最近使ったアーカイブ（最大 10 件、Path0 が最新）
Path0=
...

[AdvancedCompress]       ; 詳細圧縮ダイアログの last-used 値
DictSize=
WordSize=
SolidBlock=
Threads=
Extra=

[RarAdvanced]            ; RAR 詳細圧縮ダイアログの last-used 値
DictSize=
Volume=
Extra=
Solid=1
Threads=0
Recovery=0
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

## Credits

### Application Icon

[Archiver - free Icon in PNG and SVG](https://icon-icons.com/icon/archiver/37045) by [icon-icons.com](https://icon-icons.com/), used under free for commercial use license.
