# コード整理候補（2026-05-05）

review-2026-05-05.md 対応後、過去の改修の積み重ねで「今となっては不要・冗長」になっている処理を抽出した結果。
3 つの観点（デッドコード／重複／効率）で並列調査し、実コードで裏取りした候補のみを記録。

優先度はコスト対効果（重複行数 × 副次バグ解消の有無）で見積もった主観値。

## A. すぐ消せる／まとめられる（小〜中規模）

### A-1. `CompressDlg::Params` の Settings 読込／保存が 3 箇所でコピペ（最大の重複）

**読込側**:
- `MainWindow.cpp:411-424`（`OnDropFiles`）
- `MainWindow.cpp:741-754`（`OnAddFiles`）
- `App.cpp:91-106`（`RunCompressMode`）

7z 系 5 行 + RAR 系 6 行 = 計 11〜14 行が完全コピペ。

**保存側**:
- `MainWindow.cpp:431-440`（`OnDropFiles` — **RAR 系の保存が無い**）
- `MainWindow.cpp:762-776`（`OnAddFiles`）
- `App.cpp:115-128`（`RunCompressMode`）

**対応方針**:
`Settings.h` に以下を追加し、3 箇所を 1 行ずつに置換。

```cpp
void FillCompressParams(CompressDlg::Params& p) const;
void StoreCompressParams(const CompressDlg::Params& p);
```

**副次効果**: `OnDropFiles` の RAR パラメータ保存漏れバグが自動的に解消する（D&D で開いた CompressDlg で RAR 詳細値を変えても保存されない問題）。

### A-2. `PromptPassword` の `hint` 引数が完全未使用

- 宣言: `MainWindow.h:48` `std::wstring PromptPassword(const wchar_t* hint = nullptr);`
- 実装: `MainWindow.cpp:1200` で `/*hint*/` とコメントアウト済み
- 呼出側: `MainWindow.cpp:97` も無引数

**対応方針**: 引数ごと削除（ヘッダ・実装の両方）。

### A-3. tar-in-stream 検出の `getExt` ラムダが `ExtOfPath` の再実装

- `SevenZip.cpp:587-593` のローカルラムダ `getExt`
- 同ファイル `SevenZip.cpp:309` の `SevenZip::ExtOfPath` と完全同一の処理

**対応方針**: ラムダを削除し、`ExtOfPath(path)` 呼出に置換。

### A-4. `MainWindow::OnProgress` / `OnDone` のフォールバック

- `MainWindow.cpp:240-248` の `WM_APP_PROGRESS` / `WM_APP_DONE` ハンドラ
- 既に「通常は到達しない」コメント付き（前回 review 2-5 対応）
- 内側メッセージループが必ず吸収するため、メインの WndProc には来ない

**対応方針**: 削除より「メンバ関数 `OnProgress` / `OnDone` を残し、フォールバックでログのみ出す」程度の整理。完全削除はリスクと比べて旨味が小さい。

## B. 構造改善（中〜大規模）

### B-1. 進捗ループが 3 箇所で個別実装

- `App.cpp:155-172`（RAR、`rar.Cancel()` 呼出あり）
- `App.cpp:194-210`（7z、キャンセル経路なし）
- `MainWindow.cpp:817-835`（`runMsgLoop` ラムダ、cancelFn を引数で受ける汎用版）

`MainWindow` 側のラムダ版が最もきれいに `cancelFn` を抽象化している。

**対応方針**:
共通ヘルパーを 1 個作って 3 箇所を集約。

```cpp
// App.cpp/MainWindow.cpp 内のローカルヘルパで十分
HRESULT RunProgressLoop(ProgressDlg& dlg,
                        ProgressPostSink* sink,
                        std::function<void()> cancelFn = nullptr);
```

### B-2. RAR 圧縮経路が 2 箇所（known-issues.md 記載の罠）

- `App::RunCompressMode` の RAR ブランチ（`App.cpp:134-173`）
- `MainWindow::OnCompress` の RAR ブランチ（`MainWindow.cpp:837-856` 周辺）

両方とも `RarProcess` + `ProgressPostSink` + メッセージループの組合せ。

**対応方針**:
`RunRarCompress(HWND parent, const CompressDlg::Params&, const std::wstring& rarExePath, ...)` のような関数に切り出せれば、known-issues.md の「2 経路」問題そのものが消せる。B-1 と同じ PR でやるのが筋。

## C. 効率（やる価値はあるが体感差は規模次第）

### C-1. `GetIconIndex(L"folder", true)` のキャッシュ漏れ

- `MainWindow.cpp:939`（`PopulateTree` 直前）
- `MainWindow.cpp:1085`（`PopulateList` 直前）
- フォルダアイコンは不変なのにツリー／リスト構築のたびに `SHGetFileInfoW` 呼出

**対応方針**: `MainWindow` のメンバに `int m_iconIndexFolder = -1;` を持たせ、`OnCreate` で 1 回だけ取得。

### C-2. SevenZip フォーマット判定の静的フォールバック（要判断）

- `SevenZip::IsArchiveExt`（`SevenZip.cpp:289-305`）
- `SevenZip::FormatToInGuid`（`SevenZip.cpp:317-334`）
- `SevenZip::FormatToOutGuid`（`SevenZip.cpp:336-351`）

いずれも `m_extToClsid.empty()` のときだけ静的リストにフォールバック。
7z.dll が無いとアプリは何もできない構造なので、フォールバック自体の存在意義が薄い。

**対応方針**:
- 残す判断: 「DLL ロード前の早期パスがある」「diagnostics でフォールバック発火を確認したい」場合
- 消す判断: 動的列挙が確実に動くと自信があり、コードベースを薄くしたい場合
- **保留推奨**。残しても害は無く、消す場合は呼出順を慎重に確認。

## 優先順位（提案）

| 順位 | 項目 | コスト | 効果 |
|---|---|---|---|
| 1 | **A-1** Settings 読込／保存の 3 箇所コピペ | 中 | 重複 30〜40 行削減 + RAR 保存漏れバグ解消 |
| 2 | A-2 + A-3 + A-4 の小ネタまとめて | 小 | 累計 20 行程度の整理 |
| 3 | **B-1** 進捗ループ共通化 | 中 | 重複 30 行削減 + RAR キャンセル経路の対称性向上 |
| 4 | **B-2** RAR 圧縮 2 経路の統合 | 大 | known-issues.md の罠を 1 個減らせる |
| 5 | C-1 フォルダアイコンキャッシュ | 極小 | UI 描画でわずかに軽量化 |
| — | C-2 静的フォールバック削除 | 小 | 保留推奨 |

## 着手方針メモ

- A-1 単独で 1 PR が効果対コスト比で最良
- A-2 / A-3 / A-4 を 1 PR でまとめる「小掃除」コミットも可
- B-1 と B-2 は同一 PR でやらないと整合性が取れない
- C-1 は他 PR のついでにやれば十分

## 実施結果（2026-05-05）

A-1 / A-2 / B-1 / B-2 を実施。Debug ビルド成功確認済み（コミットは未実施、作業ツリー上の差分）。

| 項目 | 状態 | 概要 |
|---|---|---|
| **A-1** Settings 読込／保存 3 箇所コピペ | ✅ 実施 | `CompressDlg::Params::LoadFromSettings` / `SaveToSettings` を追加。`OnDropFiles` / `OnAddFiles` / `RunCompressMode` の合計 80 行を 6 行に集約。**副次効果**: `OnDropFiles` の RAR 系設定保存漏れバグも解消 |
| **A-2** `PromptPassword` 未使用引数 | ✅ 実施 | `MainWindow::PromptPassword(const wchar_t*)` の `hint` 引数を削除（宣言・実装・呼出側の整合維持） |
| A-3 `getExt` ラムダ → `ExtOfPath` | ⏸ 未実施 | tar-in-stream 検出 (`SevenZip.cpp:587-593`) のローカルラムダを既存 `ExtOfPath` に置換 |
| A-4 `OnProgress` / `OnDone` フォールバック整理 | ⏸ 未実施 | コメント追加程度の小改修 |
| **B-1** 進捗ループ 4 箇所統合 | ✅ 実施 | `ProgressDlg::RunMessageLoop(std::function<void()> onCancel = {})` を追加。`App::RunCompressMode` (RAR/7z)、`MainWindow::OnCompress`、`MainWindow::OnExtract` の 4 箇所を集約。**副次効果**: `MainWindow::OnCompress` のループに抜けていた `IsDialogMessageW` が共通化で揃った |
| **B-2** RAR 圧縮 2 経路の集約 | ✅ 実施 | 新規 `src/CompressHelper.{h,cpp}` に `RunRarCompressSync()` を実装。`App::RunCompressMode` と `MainWindow::OnCompress` の RAR ブランチを共通エントリ経由に変更。`docs/known-issues.md` の「2 経路」記述と `CLAUDE.md` の該当注意事項を「1 経路に集約済み」に更新 |
| C-1 フォルダアイコンキャッシュ | ⏸ 未実施 | `m_iconIndexFolder` メンバキャッシュ化 |
| C-2 静的フォーマットフォールバック削除 | ⏸ 保留 | 7z.dll 動的列挙の安全弁として残置判断 |

### 累積差分（A-1 〜 B-2）

```
 CMakeLists.txt      |   1 +
 src/App.cpp         |  95 +++--------------
 src/CompressDlg.cpp |  35 +++++++
 src/CompressDlg.h   |   7 ++
 src/CompressHelper.cpp | (新規 30 行)
 src/CompressHelper.h   | (新規 21 行)
 src/MainWindow.cpp  | 136 ++++++--------------------
 src/MainWindow.h    |   2 +-
 src/ProgressDlg.cpp |  25 ++++
 src/ProgressDlg.h   |   8 ++
 8 files changed, 106 insertions(+), 203 deletions(-)
```

**正味 97 行削減**（追加 106 / 削除 203）。新規ヘルパー追加分（CompressHelper 51 行 + ProgressDlg::RunMessageLoop 33 行）を差し引いても、複製コードの撤去で全体の見通しが大きく改善。

### 残タスク

実機の動作確認（A-1 / B-1 / B-2 影響範囲）:

- **D&D での圧縮**: RAR 詳細設定（dictSize 等）が次回起動でも保持されるか（A-1 の RAR 保存漏れ修正の確認）
- **CLI 引数経由の RAR 圧縮**: 起動 → キャンセル → ダイアログが正しく閉じるか
- **D&D / [追加] からの RAR 圧縮**: 同上
- **展開時のキャンセル**: 7z / unrar 両経路で進捗ダイアログのキャンセルが効くか
