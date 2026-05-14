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
#define IDD_ARCHIVE_PROPS   209
#define IDD_COMMENT         210

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
#define IDC_SFX_MODE        1010

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
// Extract subfolder creation policy (0=never / 1=single file / 2=multiple files / 3=always)
#define IDC_MKDIR_0         3010
#define IDC_MKDIR_1         3011
#define IDC_MKDIR_2         3012
#define IDC_MKDIR_3         3013
#define IDC_FONT_NAME       3014

// Info dialog controls
#define IDC_INFO_LIST       5001

// Archive properties dialog controls
#define IDC_ARCPROP_LIST    5101

// Comment dialog controls
#define IDC_COMMENT_EDIT    5201

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
#define ID_ADD_TO_CURRENT   40018  // Add files to the currently open archive

// Menu-only commands (Phase 1 menubar)
#define IDM_FILE_OPEN       40010
#define IDM_FILE_EXIT       40011
#define IDM_FILE_MRU_PH     40012   // Recently used archives - "No history" placeholder
#define IDM_VIEW_TREE       40013   // Toggle tree view
#define IDM_HELP_ABOUT      40014
#define IDM_VIEW_TOOLBAR    40015   // Toggle toolbar
#define IDM_FILE_PROPERTIES 40017   // Archive properties
#define ID_ARCHIVE_COMMENT  40019   // Show archive comment

// Recently used archives - dynamic ID range (10 entries)
#define IDM_FILE_MRU_BASE   41000
#define IDM_FILE_MRU_LAST   41009

// Menu resource
#define IDR_MAIN_MENU       301

// Worker thread messages (WM_APP range)
#define WM_APP_PROGRESS     (WM_APP + 1)
#define WM_APP_DONE         (WM_APP + 2)

// =============================================================
// Localized strings (STRINGTABLE entries; loaded via I18n::Tr)
// English / Japanese 2 LANGUAGE blocks defined in res/AileEx.rc.
// =============================================================

// --- Common ---
#define IDS_APP_TITLE                   11001
#define IDS_ERR_INIT_FAILED             11002
#define IDS_ERR_7Z_NOT_LOADED           11003
#define IDS_CANCELLED                   11005
#define IDS_CANCELLING                  11006
#define IDS_DONE                        11007
#define IDS_ERROR_OCCURRED              11008
#define IDS_DASH                        11009
#define IDS_YES                         11010
#define IDS_NO                          11011

// --- Progress / Status ---
#define IDS_PROGRESS_COMPRESSING        11020
#define IDS_PROGRESS_EXTRACTING         11021
#define IDS_PROGRESS_TESTING            11022
#define IDS_PROGRESS_DELETING           11023
#define IDS_PROGRESS_ADDING             11024
#define IDS_PROGRESS_SAVING_COMMENT     11025
#define IDS_FMT_STATUS_ENTRIES          11026

// --- Column headers ---
#define IDS_COL_NAME                    11030
#define IDS_COL_SIZE                    11031
#define IDS_COL_PACKED                  11032
#define IDS_COL_TYPE                    11033
#define IDS_COL_MODIFIED                11034
#define IDS_TYPE_FOLDER                 11035
#define IDS_TYPE_FILE                   11036
#define IDS_FMT_TYPE_FILE_EXT           11037

// --- Toolbar tooltips ---
#define IDS_TIP_EXTRACT                 11040
#define IDS_TIP_VIEW                    11041
#define IDS_TIP_ADD                     11042
#define IDS_TIP_INFO                    11043
#define IDS_TIP_TEST                    11044
#define IDS_TIP_SETTINGS                11045

// --- Context menu ---
#define IDS_CTX_EXTRACT_SELECTED        11050
#define IDS_CTX_OPEN_ASSOC              11051
#define IDS_CTX_TEST                    11052
#define IDS_CTX_INFO                    11053
#define IDS_CTX_DELETE                  11054

// --- File dialog filters / titles (filter strings use '|' as NUL sentinel) ---
#define IDS_FILTER_ARCHIVE              11060
#define IDS_FILTER_ALL_FILES            11061
#define IDS_FILTER_EXE                  11062
#define IDS_FILTER_DLL                  11063
#define IDS_TITLE_OPEN_ARCHIVE          11064
#define IDS_TITLE_SELECT_OUTPUT         11065
#define IDS_TITLE_SELECT_COMPRESS       11066
#define IDS_TITLE_SELECT_ADD            11067
#define IDS_TITLE_SELECT_RAR            11068
#define IDS_TITLE_SELECT_7Z_DLL         11069
#define IDS_TITLE_SELECT_UNRAR_DLL      11070
#define IDS_TITLE_SELECT_DEFAULT_DIR    11071
#define IDS_TITLE_SELECT_DEST_FOLDER    11072

// --- MainWindow / general messages ---
#define IDS_ERR_OPEN_ARCHIVE            11080
#define IDS_ERR_OPEN_ARCHIVE_7Z_UNRAR   11081
#define IDS_ERR_OPEN_ARCHIVE_7Z         11082
#define IDS_INFO_SELECT_FILE            11083
#define IDS_INFO_FOLDERS_NOT_VIEWABLE   11084
#define IDS_VIEW_REQUIRES_7Z            11085
#define IDS_ERR_EXTRACT_FILE_FAILED     11086
#define IDS_FMT_NO_ASSOC_APP            11087
#define IDS_ERR_EXTRACT_FAILED          11088
#define IDS_INFO_NO_FILES_SELECTED      11089
#define IDS_INFO_NO_ARCHIVE_TO_TEST     11090
#define IDS_TEST_OK                     11091
#define IDS_TEST_FAILED                 11092
#define IDS_FMT_FILE_NOT_FOUND          11093
#define IDS_MRU_NO_HISTORY              11094
#define IDS_OP_FAILED                   11095
#define IDS_ERR_ADD_FAILED              11096
#define IDS_ERR_ZIP_COMMENT_NEEDS_7Z    11097
#define IDS_ERR_RAR_LAUNCH              11098
#define IDS_ERR_COMMENT_SAVE_FAILED     11099
#define IDS_FMT_DELETE_CONFIRM          11100
#define IDS_TITLE_DELETE_CONFIRM        11101
#define IDS_ERR_DELETE_FAILED           11102
#define IDS_ERR_COMPRESS_FAILED         11103
#define IDS_FMT_SFX_NOT_FOUND_7Z        11104
#define IDS_FMT_SFX_NOT_FOUND_RAR       11105
#define IDS_ERR_RAR_NOT_FOUND           11106

// --- Drag & Drop ---
#define IDS_FMT_DND_PROMPT              11110
#define IDS_DND_ELLIPSIS                11111

// --- CompressDlg ---
#define IDS_DEFAULT_SUFFIX              11119  // " (default)"
#define IDS_SFX_NONE                    11122
#define IDS_SFX_GUI                     11123
#define IDS_SFX_CONSOLE                 11124
#define IDS_LEVEL_0                     11125
#define IDS_LEVEL_1                     11126
#define IDS_LEVEL_3                     11127
#define IDS_LEVEL_5                     11128
#define IDS_LEVEL_7                     11129
#define IDS_LEVEL_9                     11130
#define IDS_INFO_SPECIFY_OUTPUT         11131

// --- Comment dialog ---
#define IDS_FMT_COMMENT_TITLE           11135
#define IDS_COMMENT_READONLY            11136

// --- Info / Properties dialogs ---
#define IDS_COL_LABEL                   11140
#define IDS_COL_VALUE                   11141
#define IDS_FMT_INFO_TITLE              11142
#define IDS_FMT_PROPS_TITLE             11143
#define IDS_INFO_FILE_NAME              11144
#define IDS_INFO_ARCHIVE_PATH           11145
#define IDS_INFO_KIND                   11146
#define IDS_INFO_ORIG_SIZE              11147
#define IDS_INFO_PACKED_SIZE            11148
#define IDS_INFO_RATIO                  11149
#define IDS_INFO_METHOD                 11150
#define IDS_INFO_CRC32                  11151
#define IDS_INFO_ENCRYPTED              11152
#define IDS_INFO_FILE_ATTRS             11153
#define IDS_INFO_HOST_OS                11154
#define IDS_INFO_MTIME                  11155
#define IDS_INFO_CTIME                  11156
#define IDS_INFO_ATIME                  11157
#define IDS_INFO_COMMENT                11158
#define IDS_INFO_FULL_PATH              11160
#define IDS_INFO_FORMAT                 11161
#define IDS_INFO_FILE_SIZE              11162
#define IDS_INFO_OS_CTIME               11163
#define IDS_INFO_OS_MTIME               11164
#define IDS_PROPS_SUMMARY_HEAD          11165
#define IDS_PROPS_DETAIL_HEAD           11166
#define IDS_PROPS_NUM_FILES             11167
#define IDS_PROPS_NUM_FOLDERS           11168
#define IDS_PROPS_TOTAL_SIZE            11169
#define IDS_PROPS_TOTAL_PACKED          11170
#define IDS_PROPS_RATIO                 11171
#define IDS_PROPS_HAS_ENCRYPTED         11172
#define IDS_PROPS_METHOD_USED           11173

// --- SevenZip PropID names (used by SevenZip::PropIdName + PropertiesDlg compare) ---
#define IDS_PROP_PATH                   11200
#define IDS_PROP_NAME                   11201
#define IDS_PROP_EXTENSION              11202
#define IDS_PROP_IS_DIR                 11203
#define IDS_PROP_SIZE                   11204
#define IDS_PROP_PACK_SIZE              11205
#define IDS_PROP_ATTRIB                 11206
#define IDS_PROP_CTIME                  11207
#define IDS_PROP_ATIME                  11208
#define IDS_PROP_MTIME                  11209
#define IDS_PROP_SOLID                  11210
#define IDS_PROP_COMMENTED              11211
#define IDS_PROP_ENCRYPTED              11212
#define IDS_PROP_DICT_SIZE              11213
#define IDS_PROP_TYPE                   11214
#define IDS_PROP_METHOD                 11215
#define IDS_PROP_HOST_OS                11216
#define IDS_PROP_FILE_SYSTEM            11217
#define IDS_PROP_USER                   11218
#define IDS_PROP_GROUP                  11219
#define IDS_PROP_BLOCK                  11220
#define IDS_PROP_COMMENT                11221
#define IDS_PROP_NUM_SUBDIRS            11222
#define IDS_PROP_NUM_SUBFILES           11223
#define IDS_PROP_UNPACK_VER             11224
#define IDS_PROP_VOLUME                 11225
#define IDS_PROP_IS_VOLUME              11226
#define IDS_PROP_NUM_BLOCKS             11227
#define IDS_PROP_NUM_VOLUMES            11228
#define IDS_PROP_PHY_SIZE               11229
#define IDS_PROP_HEADERS_SIZE           11230
#define IDS_PROP_CHECKSUM               11231
#define IDS_PROP_CHARACTS               11232
#define IDS_PROP_CREATOR_APP            11233
#define IDS_PROP_TOTAL_SIZE             11234
#define IDS_PROP_FREE_SPACE             11235
#define IDS_PROP_CLUSTER_SIZE           11236
#define IDS_PROP_VOLUME_NAME            11237
#define IDS_PROP_LOCAL_NAME             11238
#define IDS_PROP_PROVIDER               11239
#define IDS_PROP_ERROR_TYPE             11240
#define IDS_PROP_NUM_ERRORS             11241
#define IDS_PROP_ERROR_FLAGS            11242
#define IDS_PROP_WARNING_FLAGS          11243
#define IDS_PROP_WARNING                11244
#define IDS_PROP_NUM_STREAMS            11245
#define IDS_PROP_CODE_PAGE              11246
#define IDS_PROP_EMBEDDED_STUB_SIZE     11247

// --- FormatSize / Misc ---
#define IDS_FMT_SIZE_GB                 11250
#define IDS_FMT_SIZE_MB                 11251
#define IDS_FMT_SIZE_KB                 11252
#define IDS_FMT_SIZE_BYTES              11253
#define IDS_HOST_OS_UNKNOWN             11254
#define IDS_ABOUT_NO_VERSION            11255
#define IDS_ABOUT_NOT_LOADED            11256

// --- Advanced Dialog (Compression settings) ---
#define IDS_ADV_AUTO                    11400  // "Auto"
#define IDS_ADV_NONE                    11401  // "None"
#define IDS_ADV_DEFAULT                 11402  // "Default"
#define IDS_ADV_NOT_SOLID               11403  // "Not solid"
