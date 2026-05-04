#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include "ArchiveItem.h"
#include "WorkerThread.h"

// unrar.dll C API declarations (self-contained; no WinRAR SDK needed)
enum {
    ERAR_SUCCESS         = 0,
    ERAR_END_ARCHIVE     = 10,
    ERAR_NO_MEMORY       = 11,
    ERAR_BAD_DATA        = 12,
    ERAR_BAD_ARCHIVE     = 13,
    ERAR_UNKNOWN_FORMAT  = 14,
    ERAR_EOPEN           = 15,
    ERAR_ECREATE         = 16,
    ERAR_ECLOSE          = 17,
    ERAR_EREAD           = 18,
    ERAR_EWRITE          = 19,
    ERAR_SMALL_BUF       = 20,
    ERAR_UNKNOWN         = 21,
    ERAR_MISSING_PASSWORD = 22,
    ERAR_EREFERENCE      = 23,
    ERAR_BAD_PASSWORD    = 24,
};

enum {
    RAR_OM_LIST          = 0,
    RAR_OM_EXTRACT       = 1,
    RAR_OM_LIST_INCSPLIT = 2,
};

enum {
    RAR_SKIP    = 0,
    RAR_TEST    = 1,
    RAR_EXTRACT = 2,
};

// RARHeaderDataEx::Flags bits
enum {
    RHDF_SPLITBEFORE = 0x0001,
    RHDF_SPLITAFTER  = 0x0002,
    RHDF_ENCRYPTED   = 0x0004,
    RHDF_SOLID       = 0x0010,
    RHDF_DIRECTORY   = 0x0020,
};

enum {
    UCM_CHANGEVOLUME    = 0,
    UCM_PROCESSDATA     = 1,
    UCM_NEEDPASSWORD    = 2,
    UCM_CHANGEVOLUMEW   = 3,
    UCM_NEEDPASSWORDW   = 4,
};

typedef int (CALLBACK *UNRARCALLBACK)(UINT msg, LPARAM UserData, LPARAM P1, LPARAM P2);

struct RAROpenArchiveDataEx {
    char*    ArcName;
    wchar_t* ArcNameW;
    UINT     OpenMode;
    UINT     OpenResult;
    char*    CmtBuf;
    UINT     CmtBufSize;
    UINT     CmtSize;
    UINT     CmtState;
    UINT     Flags;
    UINT     Reserved[32];
};

struct RARHeaderDataEx {
    char     ArcName[1024];
    wchar_t  ArcNameW[1024];
    char     FileName[1024];
    wchar_t  FileNameW[1024];
    UINT     Flags;
    UINT     PackSize;
    UINT     PackSizeHigh;
    UINT     UnpSize;
    UINT     UnpSizeHigh;
    UINT     HostOS;
    UINT     FileCRC;
    UINT     FileTime;
    UINT     UnpVer;
    UINT     Method;
    UINT     FileAttr;
    char*    CmtBuf;
    UINT     CmtBufSize;
    UINT     CmtSize;
    UINT     CmtState;
    UINT     DictSize;
    UINT     HashType;
    char     Hash[32];
    UINT     Reserved[1014];
};

typedef HANDLE  (PASCAL *Func_RAROpenArchiveEx)(RAROpenArchiveDataEx*);
typedef int     (PASCAL *Func_RARReadHeaderEx)(HANDLE, RARHeaderDataEx*);
typedef int     (PASCAL *Func_RARProcessFileW)(HANDLE, int, wchar_t*, wchar_t*);
typedef int     (PASCAL *Func_RARCloseArchive)(HANDLE);
typedef void    (PASCAL *Func_RARSetCallback)(HANDLE, UNRARCALLBACK, LPARAM);

class UnrarDll {
public:
    bool Load(const wchar_t* dllPath = nullptr);
    void Unload();
    bool IsLoaded() const { return m_hDll != nullptr; }

    bool ListArchive(const wchar_t* path, std::vector<ArchiveItem>& items,
                     const wchar_t* password = nullptr);

    bool ExtractArchive(const wchar_t* path, const wchar_t* destDir,
                        const wchar_t* password,
                        IExtractProgressSink* sink);

private:
    HMODULE              m_hDll      = nullptr;
    Func_RAROpenArchiveEx m_pfnOpen  = nullptr;
    Func_RARReadHeaderEx  m_pfnRead  = nullptr;
    Func_RARProcessFileW  m_pfnProc  = nullptr;
    Func_RARCloseArchive  m_pfnClose = nullptr;
    Func_RARSetCallback   m_pfnSetCb = nullptr;
};
