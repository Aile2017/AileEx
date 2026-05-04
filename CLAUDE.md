# AileEx — Claude 向けガイド

このファイルは将来の Claude セッションが本プロジェクトで作業する際の道標。

## プロジェクト概要

7z.dll をバックエンドとする Windows 向けアーカイブマネージャ GUI。C++17 + Win32 API。
詳細は `docs/specification.md` を参照。

## 環境

- **OS**: Windows 11 (x64)
- **シェル**: PowerShell（Bash も可。POSIX スクリプトは Bash 推奨）
- **コンパイラ**: MSVC (Visual Studio 18/2026 Community)
- **コンパイラへの PATH**:
  ```powershell
  $env:PATH = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64;" + $env:PATH
  ```
- **ビルドコマンド**: `cmake --build build` (Debug) / `cmake --build build_release` (Release)

## まず読むべきドキュメント

| ファイル | 内容 |
|---|---|
| `docs/specification.md` | 機能仕様（起動モード、UI、設定、対応形式） |
| `docs/architecture.md` | モジュール構成、クラス関係、スレッドモデル |
| `docs/build.md` | ビルド手順、必要な実行時 DLL |
| `docs/known-issues.md` | **重要** — 過去に踏んだ罠の記録。実装前に目を通すこと |

## コード規約

- **コメント**: 日本語可。WHY を書く。WHAT は書かない（コードで分かる）
- **既存スタイルに従う**: クラス内部の `m_` プレフィクス、`STDMETHODCALLTYPE` の使用、`HRESULT` の返し方
- **新ファイル**: `CMakeLists.txt` の `add_executable` リストに追加すること（CMake は glob しない）
- **ヘッダ**: `#pragma once` を使う

## 必ず守るべき設計上の罠

`docs/known-issues.md` から特に重要なものを抜粋。詳細はそちらを読むこと。

1. **7-Zip フォーマット CLSID**: SDK ヘッダの定数値を信用しない。新フォーマット追加時は実行時に `GetNumberOfFormats` で列挙して検証。Rar5 は **`0xCC`** であり `0x04` (= Arj) ではない。
2. **`IInArchive::Open` は format mismatch で `S_FALSE` を返す**。`FAILED(hr) || hr == S_FALSE` の両方をチェック。
3. **`COutFileStream` は `IOutStream` (シーカブル) が必要**。`SetSize` 後はファイルポインタを元に戻す。
4. **unrar.dll の `RARHeaderDataEx` は `#pragma pack` を使わない**。デフォルトアラインメントで定義。
5. **unrar.dll が返すパスはバックスラッシュ区切り**。`SevenZip` 流のフォワードスラッシュに正規化する。
6. **RAR 圧縮ルーティングは 2 経路ある**（`App::RunCompressMode`, `MainWindow::OnCompress`）。`format == L"rar"` のとき必ず `RarProcess` 経由にする。

## ワーカースレッドの定石

```
UI Thread                   Worker Thread
─────────────               ─────────────
WorkerThread::Start(task)
  → 内部で CreateThread     task() を実行
                            sink->OnProgress(...)
                              → PostMessage(WM_APP_PROGRESS, pct, _wcsdup(file))
WM_APP_PROGRESS 受信
  → ProgressDlg.SetProgress
  → free((wchar_t*)lParam)  ← 必ず free する！
                            task() 戻る
                              → PostMessage(WM_APP_DONE, hr, 0)
WM_APP_DONE 受信
  → ProgressDlg.Dismiss
worker.Wait()
delete sink
```

- `WM_APP_PROGRESS` の `lParam` は `_wcsdup` した `wchar_t*`。受信側で `free()` 必須
- キャンセル: `sink->SetCancelled(true)` をセット → コールバックが `E_ABORT` を返して中断
- RAR (rar.exe) のキャンセルは `RarProcess::Cancel()` が `TerminateProcess`

## 設定の追加方法

新しい設定項目を追加するときの手順:

1. `Settings.h` に `m_xxx`、`GetXxx()`、`SetXxx()` を追加
2. `Settings.cpp` の `Load()` / `Save()` に `ReadStr` / `WriteStr` を追加
3. `SettingsDlg` のリソース (`AileEx.rc`) に控除コントロールを追加、`resource.h` に ID を追加
4. `SettingsDlg.cpp` の `OnInit` (読込) と `OnOK` (保存) に対応を追加
5. 必要なら `App::ReloadDlls` で再読込トリガーを処理

## 新フォーマット追加方法

1. 実機の 7z.dll で `GetNumberOfFormats` / `GetHandlerProperty2` を使い CLSID を確認（過去の診断コードは git 履歴参照）
2. `sdk/7zip/Archive/IArchive.h` に `Z7_FMT_GUID(CLSID_Format_XXX, 0xYY);` を追加
3. `SevenZip.cpp` の `FormatToInGuid` / `FormatToOutGuid` に拡張子マッピング追加
4. `CompressDlg.cpp` の `kFormats[]` にエントリ追加
5. `main.cpp` の `kArchiveExts[]` に拡張子追加（ブラウズモード判定用）

## 診断・デバッグ手法

ファイルが開けない／フォーマット問題が起きたとき:

1. **マジックバイト確認**:
   ```powershell
   $bytes = [System.IO.File]::ReadAllBytes("path") | Select-Object -First 16
   ($bytes | ForEach-Object { $_.ToString("X2") }) -join " "
   ```
2. **7z.exe で確認**: 同じファイルが `7z.exe l file.rar` で開けるか
3. **CLSID 検証**: 過去のコミットに `EnumerateFormats` 関数があるので、それを一時的に復活させて `GetNumberOfFormats` で実機の CLSID を確認
4. **Stream 動作ログ**: `CInFileStream::Read`/`Seek` に `OutputDebugStringW` または `%TEMP%\aileex_debug.log` への書き込みを追加

過去の診断ログ実装は git 履歴の中盤にある。

## コーデック列挙（7-Zip Zstandard 対応）

7z.dll のロード後、SevenZip::EnumerateCodecs() を呼び出してエンコーダー名一覧を取得する（GetNumberOfMethods / GetMethodProperty 使用）。
CompressDlg はこのリストを参照し、DLL がサポートしないコーデックをメソッドコンボから除外する（supportsEncoder lambda）。

- PropID: kName=1, kEncoderIsAssigned=8
- エイリアス: store ↔ copy（ZIP の Store）、zstd ↔ zstandard
- 7-Zip Zstandard が報告する追加コーデック: BROTLI, LZ4, LIZARD, LZ5, ZSTD, FLZMA2

## フォルダ選択ダイアログ

展開先・設定ダイアログの出力先選択は IFileOpenDialog + FOS_PICKFOLDERS + FOS_FORCEFILESYSTEM を使う（SHBrowseForFolder は廃止）。
必要ヘッダ: <shobjidl_core.h>。現在値は SHCreateItemFromParsingName → SetFolder() で初期フォルダとして表示。

## GitHub Actions

.github/workflows/package-release.yml に workflow_dispatch トリガーのリリースワークフローを追加。
- ilammy/msvc-dev-cmd で MSVC x64 環境構築 → CMake Release ビルド
- AileEx.exe + README.md を ZIP に梱包し softprops/action-gh-release でリリース作成
- タグ形式: AileEx_{version}_{yyyyMMdd}

## 残タスク

- [ ] 手動テストマトリクス: 各形式の閲覧・圧縮・展開・キャンセル・ドロップ
- [ ] エラーハンドリング一括レビュー
- [ ] マルチボリューム対応（未着手・今後の拡張案）
- [ ] シェル統合（コンテキストメニュー、未着手）

## ユーザの作業スタイル

- 日本語でやり取り
- 問題が解決した時点で「ありがとう」と返事をくれる
- 機能追加より、まず根本原因の特定を優先する（過去の CLSID バグ調査がいい例）
- 中断中にコードを直接編集することがある（戻ったら現状確認すること）

## ビルドが壊れたら

1. `cmake --build build 2>&1 | Select-Object -Last 30` でエラー全文を確認
2. CMake のキャッシュが古い可能性があるなら `cmake -B build` で再生成
3. SDK ヘッダの問題なら `sdk/7zip/` のヘッダを確認
