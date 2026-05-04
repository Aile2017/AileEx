#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include "ArchiveItem.h"
#include "WorkerThread.h"
#include "7zip/Archive/IArchive.h"

class SevenZip {
public:
    bool Load(const wchar_t* dllPath = nullptr);
    void Unload();
    bool IsLoaded() const { return m_hDll != nullptr; }

    // Detect archive format by extension and open, filling items.
    HRESULT OpenArchive(const wchar_t* path, std::vector<ArchiveItem>& items,
                        const wchar_t* password = nullptr);

    // Extract. indices empty = extract all.
    HRESULT Extract(const wchar_t* archivePath,
                    const std::vector<UINT32>& indices,
                    const wchar_t* destDir,
                    const wchar_t* password,
                    IExtractProgressSink* sink);

    // Compress srcPaths into outPath.
    HRESULT Compress(const std::vector<std::wstring>& srcPaths,
                     const wchar_t* outPath,
                     const wchar_t* format,   // "7z","zip","tar","gz","bz2","xz"
                     int level,               // 0-9
                     const wchar_t* method,   // "lzma","deflate","zstd", etc.
                     const wchar_t* password,
                     IExtractProgressSink* sink);

private:
    HMODULE           m_hDll           = nullptr;
    Func_CreateObject m_pfnCreateObject = nullptr;

    HRESULT CreateInArchive(const GUID& clsid, IInArchive** ppArc);
    HRESULT CreateOutArchive(const GUID& clsid, IOutArchive** ppArc);
    const GUID& FormatToInGuid(const wchar_t* path) const;
    const GUID& FormatToOutGuid(const wchar_t* format) const;
    static std::wstring ExtOfPath(const wchar_t* path);
};
