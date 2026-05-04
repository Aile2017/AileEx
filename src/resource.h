#pragma once

#ifndef IDC_STATIC
#define IDC_STATIC (-1)
#endif

// Icons
#define IDI_AILEEX          101

// Dialogs
#define IDD_COMPRESS        201
#define IDD_PROGRESS        202
#define IDD_SETTINGS        203
#define IDD_INFO            204

// Compress dialog controls
#define IDC_OUTPUT_PATH     1001
#define IDC_BROWSE          1002
#define IDC_FORMAT          1003
#define IDC_LEVEL           1004
#define IDC_METHOD          1005
#define IDC_PASSWORD        1006
#define IDC_ENCRYPT_HDR     1007
#define IDC_INPUT_LIST      1008

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

// Info dialog controls
#define IDC_INFO_LIST       5001

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

// Worker thread messages (WM_APP range)
#define WM_APP_PROGRESS     (WM_APP + 1)
#define WM_APP_DONE         (WM_APP + 2)
