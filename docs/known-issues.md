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

`App::RunCompressMode()`（コマンドライン引数経由）と `MainWindow::OnCompress()`（D&D 経由）の 2 経路がある。両方とも `format == L"rar"` の場合は `RarProcess::Compress` を呼ぶ必要がある。`SevenZip::FormatToOutGuid("rar")` は対応せず `CLSID_Format_7z` にフォールバックするので、間違って 7z 形式で出力されてしまう。

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
