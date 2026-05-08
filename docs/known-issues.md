# 既知の落とし穴・歴史的バグ

開発中に踏んだ罠と、その回避方法を記録。同じ問題に再びハマらないため。

## 7-Zip フォーマット CLSID

7z.dll の `CreateObject` に渡すフォーマット CLSID は `{23170F69-40C1-278A-1000-00011000XX0000}` の `XX` バイトで識別される。**この `XX` の値は SDK ドキュメントや古いサンプルコードと食い違うことがある**。

実測値（7-Zip 26.00 ZS で確認、`GetNumberOfFormats` / `GetHandlerProperty2` で列挙）:

| Name | byte | 拡張子 |
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

**注意:** 古い情報源では Rar5 のバイトを `0x04` と記載していることがあるが、これは **誤り**。`0x04` は Arj。`Rar5 = 0x04` で実装すると、`CreateObject` は Arj ハンドラを返し、Arj ハンドラは RAR ファイルを認識せず `S_FALSE` を返すため、原因が分かりにくいバグになる。

別の DLL ビルドを使う場合は、起動時に `GetNumberOfFormats` と `GetHandlerProperty2` で列挙して検証するのが安全。

## IInArchive::Open の戻り値

`archive->Open()` は format mismatch で `S_FALSE` を返す。`FAILED(S_FALSE) == false` なので、エラー判定は `FAILED(hr) || hr == S_FALSE` の両方をチェックする必要がある。`E_FAIL` への変換は呼び出し側で行う。

## RAR4 / RAR5 振り分け

`.rar` 拡張子だけでは RAR4 / RAR5 を判別できない（マジックバイトを見ないと分からない）。当初は RAR5 ハンドラを試して `S_FALSE` なら RAR4 にフォールバックする実装。フォールバック対象は `archive->Open` が `S_FALSE` を返したときだけでなく、`FAILED(hr)` のときも含める必要がある。

## COutFileStream は IOutStream が必要

7z アーカイブ書き出しでは、ヘッダを後から書き戻すために**シーカブルな** `IOutStream` が必要。`ISequentialOutStream` だけ実装すると空のアーカイブができる。`IOutStream::Seek` と `SetSize` も実装すること。`SetSize` は呼出後にファイルポインタを元の位置に戻すこと（戻さないと後続の Write が壊れる）。

## RAR コンプレッションのルーティング

`App::RunCompressMode()`（コマンドライン引数経由）と `MainWindow::OnCompress()`（D&D / [追加] 経由）の 2 経路がある。**両経路は `CompressHelper.h` の `RunRarCompressSync()` 経由に集約済み** — ここで必ず `RarProcess::Compress` を呼ぶ。`SevenZip::FormatToOutGuid("rar")` は対応せず `CLSID_Format_7z` にフォールバックするので、間違って 7z で出力させない安全弁として 1 経路化している。

## unrar.dll の `RARHeaderDataEx` 構造体

`#pragma pack(push, 1)` を適用すると、`CmtBuf` (8 バイトポインタ) が 8 バイト境界からズレる（`FileAttr` の後、本来 4 バイトのパディングがあるべき位置）。結果、unrar.dll は構造体境界より 4 バイト先まで書き込み、スタックオーバーフローを起こす。**`#pragma pack` を使わずデフォルトアラインメントで定義すること**。

## unrar.dll の path 区切り

`RARHeaderDataEx::FileNameW` は **バックスラッシュ** で区切られる。`SevenZip::OpenArchive` の流儀（フォワードスラッシュ）と統一するため、`UnrarDll::ListArchive` で正規化する：

```cpp
for (auto& c : it.path) if (c == L'\\') c = L'/';
while (!it.path.empty() && it.path.back() == L'/') it.path.pop_back();
```

正規化しないと TreeView/ListView のフォルダ振り分けロジックが破綻する。

## マニフェストの埋め込み

CMakeLists.txt で `target_link_options(AileEx PRIVATE "/MANIFEST:NO")` を指定し、リンカ自動生成のマニフェストを抑制している。マニフェストは `res/AileEx.rc` から `1 RT_MANIFEST "manifest.xml"` で埋め込み。両方やると重複してリソースエラー。

## DPI 対応

マニフェストの `dpiAwareness = PerMonitorV2` だけで Windows 10+ は OK。古い Windows のために `wWinMain` 冒頭で `SetProcessDpiAwarenessContext` を `GetProcAddress` で動的呼び出し。マニフェストとの二重宣言は問題なし（マニフェスト優先）。

## `WM_INITMENUPOPUP` のシステムメニュー除外

`WM_INITMENUPOPUP` はメニューバーの popup だけでなく、**タイトルバー右クリック等のシステムメニュー**にも発火する。`HIWORD(lParam) != 0` がシステムメニューのフラグなので、`if (HIWORD(lp) == 0) OnInitMenuPopup(...)` で弾かないと、システムメニューに対して `EnableMenuItem(ID_DELETE, ...)` 等が呼ばれて意味不明な状態になる（実害は出にくいが、`MF_BYCOMMAND` で見つからない ID を渡し続けることになる）。

## メニューラベルの `&` エスケープ

`AppendMenuW` の文字列で `&` は **次の文字を下線（アクセラレータ）化**する。MRU 等で**ファイルパスを動的にメニューに入れる**場合、`&` を含むパス（例: `C:\Tools\AT&T\foo.7z`）はそのままだとアクセラレータ扱いされて表示が壊れる。`&` を `&&` に二重化してエスケープする必要がある。

## `ProgressPostSink` のスロットリング

7z.dll のコールバックは進捗を非常に高頻度で呼ぶ。コールバック毎に `PostMessage(WM_APP_PROGRESS, ...)` するとメッセージキューが溢れ、**キャンセルボタンのクリックが遅延・破棄されてキャンセル不能に見える**。`ProgressPostSink::OnProgress` 内で `GetTickCount` を見て **20Hz 程度に絞る**（`m_lastPostTick` で間引き）必要がある。

## `IsDialogMessageW` を VK_TAB 専用に絞る

メッセージループで `IsDialogMessageW(hwnd, &msg)` を全 `WM_KEYDOWN` に対して呼ぶと、内部で `WM_SYSKEYDOWN` を消費して **Alt+F 等のメニューニーモニックが効かなくなる**（Alt 単独でメニューを有効化してから F、の二段操作になる）。`if (msg.message == WM_KEYDOWN && msg.wParam == VK_TAB)` のようにタブナビゲーション専用に絞る。

## `unrar.dll TestArchive` のキャンセル戻り値

`UnrarDll::TestArchive` は `RARProcessFileW(... RAR_TEST ...)` の戻りを単純に true/false に変換するため、**ユーザがキャンセルしても false（= 失敗）と区別がつかない**。`MainWindow::OnTest` で `sink->IsCancelled()` を見て `E_ABORT` 相当に正規化し、エラーダイアログ表示を抑止する。

## `ForceForeground` (フォアグラウンド奪取)

ランチャー経由起動などで親プロセスが既に終了しているケースでは、`SetForegroundWindow` 単独では Windows のフォーカス制限で降格される。`AttachThreadInput(myTid, fgTid, TRUE)` で前景アプリのスレッドにアタッチした上で `HWND_TOPMOST` を一瞬付けて Z オーダーを押し出してから `SetForegroundWindow` を呼ぶ二段構え（`MainWindow.cpp` の `ForceForeground` 名前空間関数）。

## RAR 削除のキャンセル経路

`RarProcess::Delete` (= `rar.exe d`) には現状 **キャンセル経路が実装されていない**。7z.dll 経由 (`SevenZip::DeleteItems`) は `CDeleteCallback::SetCompleted` が `E_ABORT` を返してキャンセルできるが、RAR 経路はプロセスが完了するまで戻らない。`RarProcess::Cancel()` (TerminateProcess) を `Delete` でも使うように拡張する必要がある（TODO）。

## 7z.dll の分割書き出しはホスト側責任

7z.dll の各フォーマットハンドラ (`7zHandlerOut.cpp` 等) は `UpdateItems(outStream, ...)` で渡されたストリームに**そのまま書き続ける**。分割ロジック (N MB ごとに次のファイルへ切り替え) は **DLL に入っていない**。`IArchiveUpdateCallback2::GetVolumeSize/GetVolumeStream` というインターフェイスは存在するが、これは 7-Zip CLI / 7zFM の `Update.cpp` が**自前で実装した分割ストリーム** (`COutMultiVolStream`) が呼び出すコールバックであって、ハンドラ側からは呼ばれない。

そのため AileEx も分割書き出しは `CMultiVolOutStream` (`SevenZip.cpp`) を自前で実装し、それを `IOutStream` として `UpdateItems` に渡している。7z.dll は単一の seekable stream に見える。

注意点:
- `IOutStream::Seek` は **グローバルオフセット** ⇄ (volIdx, volOffset) のマッピングが必要 (7z.dll はヘッダ書き込みのため先頭付近に頻繁にシークする)
- `IOutStream::SetSize` は最終アーカイブサイズで呼ばれるため、境界ボリュームを切り詰めて以降を削除する
- 各ボリュームの HANDLE を保持し続ける (Seek で過去ボリュームに戻る場合があるため)

## 分割アーカイブの読み込みは Split ハンドラ + `IArchiveOpenVolumeCallback`

`archive.7z.001` を開く際は 7z.dll が**拡張子マップから Split ハンドラを選ぶ**。Split ハンドラは `IArchiveOpenVolumeCallback::GetStream` でホストに `archive.7z.002`, `.003`, ... を要求し、内部で連結ストリームを構築してから本来のハンドラ (例: 7z) に渡す。`COpenVolumeCallback` (`SevenZip.cpp`) はこの要求に対し同じディレクトリの該当ファイルを開いて返すだけ。存在しないファイルを要求された場合は `S_FALSE` を返す (DLL は最終巻シグナルとして扱う)。

`OpenArchive` で第1巻を判定するロジック: 拡張子が**全て数字** (`001`, `002` 等) なら分割アーカイブとみなして volume callback を渡す。RAR の `.partN.rar` は `.rar` 拡張子なので unrar.dll / 7z.dll の RAR ハンドラが内部で次巻を解決する (callback 不要)。
