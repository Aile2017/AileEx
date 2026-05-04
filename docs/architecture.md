# AileEx アーキテクチャ

## ディレクトリ構成

```
AileEx/
├── CMakeLists.txt
├── CLAUDE.md
├── docs/
│   ├── specification.md
│   ├── architecture.md
│   ├── build.md
│   └── known-issues.md
├── src/
│   ├── main.cpp             — wWinMain、引数解析、モード振り分け
│   ├── App.h/.cpp           — シングルトン、DLL ロード管理、メッセージループ
│   ├── MainWindow.h/.cpp    — ブラウズウィンドウ（TreeView + ListView）
│   ├── CompressDlg.h/.cpp   — 圧縮設定ダイアログ
│   ├── ProgressDlg.h/.cpp   — モーダル進捗ダイアログ
│   ├── SettingsDlg.h/.cpp   — 設定ダイアログ
│   ├── InfoDlg.h/.cpp       — エントリ詳細表示ダイアログ
│   ├── Settings.h/.cpp      — INI 読み書き
│   ├── SevenZip.h/.cpp      — 7z.dll ラッパー (IIn/IOutArchive + コールバック + Find7zDll)
│   ├── UnrarDll.h/.cpp      — unrar.dll C API ラッパー
│   ├── RarProcess.h/.cpp    — WinRAR.exe (GUI) / Rar.exe (console) サブプロセス
│   ├── ArchiveItem.h        — アーカイブエントリ POD 構造体
│   ├── WorkerThread.h/.cpp  — ワーカースレッド + ProgressPostSink
│   └── resource.h           — リソース ID, WM_APP_* 定数
├── res/
│   ├── AileEx.rc            — ダイアログテンプレート, アクセラレータ, マニフェスト埋込
│   ├── AileEx.ico           — アプリケーションアイコン
│   └── manifest.xml         — Common Controls v6, dpiAware = PerMonitorV2
└── sdk/
    └── 7zip/                — 7-Zip SDK 自前最小ヘッダ
        ├── compat.h         — UInt32/Int64 等の型エイリアス
        ├── IDecl.h          — IID GUID 定義 + 補助マクロ
        ├── IProgress.h
        ├── IStream.h
        ├── IPassword.h      — ICryptoGetTextPassword[2] (手書き)
        ├── PropID.h
        └── Archive/IArchive.h — フォーマット CLSID + IInArchive/IOutArchive
```

## クラス関係図

```
                    ┌─────────────┐
                    │    main()    │
                    └──────┬──────┘
                           │
                  ┌────────▼─────────┐
                  │      App         │←─ Settings (INI 読み書き)
                  │ (シングルトン)    │←─ SevenZip (7z.dll ラッパー)
                  │                  │←─ UnrarDll (unrar.dll ラッパー)
                  └────────┬─────────┘
                           │
              ┌────────────┴────────────┐
              ▼                         ▼
       ┌─────────────┐          ┌──────────────┐
       │ MainWindow   │          │ CompressDlg  │
       │ (Browse)     │          │ (圧縮設定)    │
       └──────┬──────┘          └──────┬───────┘
              │                          │
       ┌──────┴──────┬─────────┐         ▼
       ▼             ▼         ▼  ┌──────────────┐
   ┌──────────┐ ┌─────────┐ ┌────┐│ RarProcess   │
   │ProgressDlg│ │SettingsDlg│ │InfoDlg│ (WinRAR/Rar) │
   └────┬─────┘ └─────────┘ └────┘└──────────────┘
        │
        ▼
   ┌─────────────┐
   │ WorkerThread│
   │ + Sink      │
   └──────┬──────┘
          │
          ▼
   PostMessage WM_APP_PROGRESS / WM_APP_DONE
```

## スレッドモデル

```
UI Thread                    Worker Thread
─────────────────            ────────────────────────────────────
WorkerThread::Start(task) ──→ task() 実行
                               IArchiveExtractCallback::SetCompleted()
                                 PostMessageW(hwnd, WM_APP_PROGRESS, pct, (LPARAM)filename_copy)
UI wakes on WM_APP_PROGRESS ←─────────────────────────────────
User clicks Cancel:
  sink->SetCancelled(true)
  SetCompleted returns E_ABORT
PostMessageW(hwnd, WM_APP_DONE, hr, 0) ──→
```

- ワーカー側でアーカイブ操作（`SevenZip::Extract` / `Compress`、`UnrarDll::ExtractArchive`）を実行
- `IArchiveExtractCallback::SetCompleted` 等のコールバックから `PostMessage` で UI へ進捗通知
- `WM_APP_PROGRESS` の `lParam` は `_wcsdup` した `wchar_t*` → UI 側で `free()`
- キャンセル: UI スレッドが `sink->SetCancelled(true)` をセット、ワーカー側コールバックが `E_ABORT` を返して中断
- `RarProcess` のキャンセルは `TerminateProcess` で rar.exe を強制終了

## 形式振り分け

`MainWindow::OpenArchive(path)`:

```
.rar ファイル?
  ├─ Yes → RarExtractor 設定が "unrar" かつ unrar.dll ロード済?
  │   ├─ Yes → unrar.ListArchive() を試行
  │   │   └─ 失敗 → 7z.OpenArchive() にフォールバック
  │   └─ No  → 7z.OpenArchive() を試行
  │       └─ 失敗 → unrar.ListArchive() にフォールバック (ロード済の場合)
  └─ No  → 7z.OpenArchive() のみ
```

`SevenZip::OpenArchive(path)`:
- 拡張子から CLSID を判定 → `CreateInArchive` でハンドラ取得
- `archive->Open()` で開く
- `.rar` で `S_FALSE` の場合は RAR4 (CLSID byte `0x03`) にフォールバック

## 設定ダイアログ

`Settings::Save()` 後に `App::ReloadDlls()` を呼び、新しい DLL パスでロードし直す。

## メッセージ定数

`resource.h`:

```cpp
#define WM_APP_PROGRESS (WM_APP + 1)  // wParam=percent (0-100), lParam=wchar_t* (free required)
#define WM_APP_DONE     (WM_APP + 2)  // wParam=HRESULT
```

## 主要 Windows API

| API | 用途 |
|---|---|
| `CreateProcessW` | RAR 圧縮（rar.exe 起動） |
| `CreatePipe` | rar.exe の stdout キャプチャ |
| `LoadLibraryW` / `GetProcAddress` | 7z.dll / unrar.dll 動的ロード |
| `RegOpenKeyExW` | WinRAR インストールパスのレジストリ検索 |
| `WritePrivateProfileStringW` | INI 設定保存 |
| `DragAcceptFiles` / `DragQueryFileW` | D&D 対応 |
| `CreateAcceleratorTable` | キーボードショートカット |
| `SetProcessDpiAwarenessContext` | DPI 対応（PerMonitorV2） |
