#pragma once

#ifndef IDC_STATIC
#define IDC_STATIC (-1)
#endif

// Icons
#define IDI_AILEEX          101

// Toolbar bitmaps
#define IDB_TOOLBAR_EXTRACT 111
#define IDB_TOOLBAR_OPEN    112
#define IDB_TOOLBAR_ADD     113
#define IDB_TOOLBAR_INFO    114
#define IDB_TOOLBAR_SETTINGS 115
#define IDB_TOOLBAR_TEST     116

// Dialogs
#define IDD_COMPRESS        201
#define IDD_PROGRESS        202
#define IDD_SETTINGS        203
#define IDD_INFO            204
#define IDD_COMPRESS_ADV    205
#define IDD_RAR_COMPRESS_ADV  206
#define IDD_PASSWORD        207
#define IDD_ABOUT           208

// Compress dialog controls
#define IDC_OUTPUT_PATH     1001
#define IDC_BROWSE          1002
#define IDC_FORMAT          1003
#define IDC_LEVEL           1004
#define IDC_METHOD          1005
#define IDC_PASSWORD        1006
#define IDC_ENCRYPT_HDR     1007
#define IDC_INPUT_LIST      1008
#define IDC_ADV_BUTTON      1009

// Advanced compress dialog controls
#define IDC_ADV_DICT        6001
#define IDC_ADV_WORD        6002
#define IDC_ADV_SOLID       6003
#define IDC_ADV_THREADS     6004
#define IDC_ADV_PARAMS      6005
#define IDC_ADV_VOLUME      6006

// RAR Advanced compress dialog controls
#define IDC_RAR_ADV_DICT      7001
#define IDC_RAR_ADV_SOLID     7002
#define IDC_RAR_ADV_THREADS   7003
#define IDC_RAR_ADV_RECOVERY  7004
#define IDC_RAR_ADV_VOLUME    7005
#define IDC_RAR_ADV_PARAMS    7006

// Progress dialog controls
#define IDC_PROGRESS_BAR    2001
#define IDC_PROGRESS_FILE   2002
#define IDC_CANCEL          2003
#define IDC_ELAPSED         2004

// Settings dialog controls
#define IDC_RAR_EXTRACTOR   3001
#define IDC_DEFAULT_DIR     3002
#define IDC_BROWSE_DIR      3003
#define IDC_7Z_DLL_PATH     3004
#define IDC_BROWSE_7Z       3005
#define IDC_UNRAR_DLL_PATH  3006
#define IDC_BROWSE_UNRAR    3007
#define IDC_RAR_EXE_PATH    3008
#define IDC_BROWSE_RAR      3009
// 展開サブフォルダ作成ポリシー (0=しない / 1=単一ファイル時 / 2=複数時 / 3=常に)
#define IDC_MKDIR_0         3010
#define IDC_MKDIR_1         3011
#define IDC_MKDIR_2         3012
#define IDC_MKDIR_3         3013

// Info dialog controls
#define IDC_INFO_LIST       5001

#define IDC_PASSWORD_INPUT  8001

// About dialog controls
#define IDC_ABOUT_TITLE     8101
#define IDC_ABOUT_URL       8102
#define IDC_ABOUT_LIST      8103

// Toolbar / Menu commands
#define ID_EXTRACT          40001
#define ID_ADD              40002
#define ID_SETTINGS_DLG     40003
#define ID_REFRESH          40004
#define ID_TEST             40005
#define ID_DELETE           40006
#define ID_CLOSE            40007
#define ID_INFO             40008
#define ID_OPEN_ASSOC       40009
#define ID_EXTRACT_SELECTED 40016

// Menu-only commands (Phase 1 menubar)
#define IDM_FILE_OPEN       40010
#define IDM_FILE_EXIT       40011
#define IDM_FILE_MRU_PH     40012   // 最近使ったアーカイブ - "履歴なし" プレースホルダ
#define IDM_VIEW_TREE       40013   // ツリー表示トグル
#define IDM_HELP_ABOUT      40014
#define IDM_VIEW_TOOLBAR    40015   // ツールバー表示トグル

// 最近使ったアーカイブ - 動的 ID レンジ (10 件)
#define IDM_FILE_MRU_BASE   41000
#define IDM_FILE_MRU_LAST   41009

// Menu resource
#define IDR_MAIN_MENU       301

// Worker thread messages (WM_APP range)
#define WM_APP_PROGRESS     (WM_APP + 1)
#define WM_APP_DONE         (WM_APP + 2)
