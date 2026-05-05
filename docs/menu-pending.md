# メニュー実装 — 完了

すべての Phase が完了。

| Phase | 完了日 | 内容 |
|---|---|---|
| 1 | 2026-05-05 | メニューバー骨組み (ファイル / 操作 / 表示 / ヘルプ) |
| B | 2026-05-06 | `WM_INITMENUPOPUP` 基盤・ツリー表示トグル・MRU |
| 削除 | 2026-05-06 | 削除機能 (7z.dll `IOutArchive::UpdateItems` / rar.exe `d`) |

関連ファイル:

- `res/AileEx.rc` の `IDR_MAIN_MENU`
- `src/resource.h` の `ID_*` / `IDM_*`
- `src/MainWindow.cpp` の `OnCommand` / `OnInitMenuPopup` / `OnDelete` / `RebuildMruMenu`
- `src/SevenZip.cpp` の `DeleteItems` / `CDeleteCallback`
- `src/RarProcess.cpp` の `Delete`
- `src/Settings.cpp` の `AddMru` / `RemoveMru`
- `src/App.cpp` の `RunBrowseMode` アクセラレータテーブル

## 既知の制限・将来拡張

- **暗号化アーカイブの削除**: パスワード保持していないため、ヘッダ暗号化された 7z アーカイブの削除は `IInArchive::Open` 段階で失敗する。修正案: `MainWindow` でオープン時のパスワードを保持して `DeleteItems` に渡す。
- **書き込み未対応フォーマット**: ISO/CAB/JAR 等は `IOutArchive` を提供しないため `QueryInterface` が失敗する。エラーメッセージで明示するのは TODO。
- **削除中のキャンセル**: 7z 経路は `CDeleteCallback::SetCompleted` で `E_ABORT` を返してキャンセル可能だが、RAR (rar.exe) のキャンセルは未実装（現状 `RarProcess::Delete` はキャンセル経路を持たない）。
