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
│   ├── main.cpp                   — wWinMain、引数解析、モード振り分け
│   ├── App.h/.cpp                 — シングルトン、DLL ロード管理、メッセージループ
│   ├── MainWindow.h/.cpp          — ブラウズウィンドウ（メニュー + ツールバー + TreeView + ListView + ステータスバー）
│   ├── CompressDlg.h/.cpp         — 圧縮設定ダイアログ
│   ├── AdvancedCompressDlg.h/.cpp — 7z/ZIP 詳細圧縮オプション (dict/word/solid/threads/extra)
│   ├── RarAdvancedDlg.h/.cpp      — RAR 詳細圧縮オプション (recovery/volume 等)
│   ├── CompressHelper.h/.cpp      — RAR 圧縮の単一エントリポイント (`RunRarCompressSync`)
│   ├── ProgressDlg.h/.cpp         — モーダル進捗ダイアログ
│   ├── SettingsDlg.h/.cpp         — 設定ダイアログ
│   ├── InfoDlg.h/.cpp             — エントリ詳細表示ダイアログ
│   ├── Settings.h/.cpp            — INI 読み書き、MRU 管理
│   ├── SevenZip.h/.cpp            — 7z.dll ラッパー (IIn/IOutArchive + DeleteItems + コールバック + Find7zDll)
│   ├── UnrarDll.h/.cpp            — unrar.dll C API ラッパー
│   ├── RarProcess.h/.cpp          — WinRAR.exe (GUI) / Rar.exe (console) サブプロセス (Compress / Delete)
│   ├── ArchiveItem.h              — アーカイブエントリ POD 構造体
│   ├── WorkerThread.h/.cpp        — ワーカースレッド + IExtractProgressSink + ProgressPostSink
│   └── resource.h                 — リソース ID, WM_APP_* 定数
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
                  │      App         │←─ Settings (INI 読み書き、MRU)
                  │ (シングルトン)    │←─ SevenZip (7z.dll ラッパー)
                  │                  │←─ UnrarDll (unrar.dll ラッパー)
                  └────────┬─────────┘
                           │
              ┌────────────┴────────────┐
              ▼                         ▼
       ┌─────────────┐          ┌──────────────┐    ┌──────────────────────┐
       │ MainWindow   │─────────▶│ CompressDlg  │───▶│ AdvancedCompressDlg  │
       │ (Browse)     │          │ (圧縮設定)    │    │ RarAdvancedDlg       │
       │ + メニュー    │          └──────┬───────┘    └──────────────────────┘
       │ + ツールバー  │                  │
       │ + TreeView   │                  ▼
       │ + ListView   │           ┌──────────────────┐
       │ + Status     │           │ CompressHelper   │
       └──────┬──────┘            │ (RAR 圧縮集約)    │
              │                    └────────┬─────────┘
       ┌──────┼──────┬──────────┬────────┐  │
       ▼      ▼      ▼          ▼        ▼  ▼
  ┌─────────┐┌─────────┐┌────────┐┌────────┐┌─────────────┐
  │ProgressDlg│SettingsDlg││InfoDlg │PassDlg ││ RarProcess   │
  │ + Cancel │└─────────┘└────────┘└────────┘│ (WinRAR/Rar) │
  └────┬─────┘                                │ Compress     │
       │                                      │ Delete       │
       ▼                                      └──────────────┘
  ┌─────────────────────────────┐
  │ WorkerThread                │
  │ + IExtractProgressSink      │
  │ + ProgressPostSink          │
  └──────┬──────────────────────┘
         │
         ▼
  PostMessage WM_APP_PROGRESS / WM_APP_DONE
```

## ダイアログ一覧

| リソース ID | クラス / 用途 |
|---|---|
| `IDD_COMPRESS` | `CompressDlg` — 圧縮設定 |
| `IDD_COMPRESS_ADV` | `AdvancedCompressDlg` — 7z/ZIP 詳細圧縮オプション |
| `IDD_RAR_COMPRESS_ADV` | `RarAdvancedDlg` — RAR 詳細圧縮オプション |
| `IDD_PROGRESS` | `ProgressDlg` — モーダル進捗 |
| `IDD_SETTINGS` | `SettingsDlg` — 設定 |
| `IDD_INFO` | `InfoDlg` — エントリ詳細 |
| `IDD_PASSWORD` | パスワード入力（暗号化アーカイブのオープン時に自動表示）|
| `IDD_ABOUT` | バージョン情報 |
| `IDR_MAIN_MENU` | メインウィンドウのメニューバー |

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

`MainWindow::OpenArchive` の続き:
- 全バックエンドが失敗した場合、暗号化ヘッダの可能性があるため `PromptPassword()` でパスワードダイアログを表示し、入力されたパスワードで `SevenZip::OpenArchive` を再試行
- 成功したらアーカイブパスを正規化 (`GetFullPathNameW`) して `Settings::AddMru` に登録、`RebuildMruMenu()` でメニュー再構築

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
| `IFileOpenDialog` (Shell) | フォルダ選択ダイアログ（展開先・設定ダイアログ） |
