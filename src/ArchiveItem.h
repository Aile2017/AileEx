#pragma once
#include <windows.h>
#include <string>

struct ArchiveItem {
    std::wstring path;       // full path within archive (e.g. "folder/sub/file.txt")
    std::wstring name;       // leaf name
    UINT64       size       = 0;
    UINT64       packedSize = 0;
    std::wstring method;
    FILETIME     mtime      = {};
    FILETIME     ctime      = {};   // creation time (may be zero if unavailable)
    FILETIME     atime      = {};   // last access time (may be zero if unavailable)
    UINT32       crc        = 0;
    bool         hasCrc     = false;
    bool         encrypted  = false;
    bool         isDir      = false;
    UINT32       attrib     = 0;    // Windows file attributes (may be zero if unavailable)
    std::wstring hostOS;
    std::wstring comment;
    UINT32       index      = 0;  // index inside IInArchive
};
