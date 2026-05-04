#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include "ArchiveItem.h"
#include "WorkerThread.h"
#include "7zip/Archive/IArchive.h"

typedef HRESULT (WINAPI *Func_GetNumberOfMethods)(UINT32* numMethods);
typedef HRESULT (WINAPI *Func_GetMethodProperty)(UINT32 index, PROPID propID, PROPVARIANT* value);

// Advanced compression options passed to SevenZip::Compress().
// Any empty string means "use default" (property is not sent to 7z.dll).
struct CompressAdvanced {
    std::wstring dictSize;    // "64k","1m","32m","512m","1g" — dictionary size
    std::wstring wordSize;    // "8","32","64","273" — fast bytes (fb)
    std::wstring solidBlock;  // "off","1m","4g" — solid block size (7z only)
    std::wstring threads;     // "1","4","8" — CPU threads (mt)
    std::wstring extra;       // free-form "key=value" pairs (e.g. "mf=bt4 mpass=2")
};

class SevenZip {
public:
    bool Load(const wchar_t* dllPath = nullptr);
    void Unload();
    bool IsLoaded() const { return m_hDll != nullptr; }
    const std::wstring& GetLoadedName() const { return m_loadedName; }

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
                     IExtractProgressSink* sink,
                     const CompressAdvanced* adv = nullptr);

    // Auto-detect installed 7z.dll from registry or known paths.
    static std::wstring Find7zDll();

    // Returns lowercased encoder names supported by the loaded DLL.
    // Empty if DLL is not loaded or enumeration is unavailable.
    const std::vector<std::wstring>& GetEncoderNames() const { return m_encoderNames; }

private:
    HMODULE                    m_hDll              = nullptr;
    std::wstring               m_loadedName;
    Func_CreateObject          m_pfnCreateObject   = nullptr;
    Func_GetNumberOfMethods    m_pfnGetNumMethods  = nullptr;
    Func_GetMethodProperty     m_pfnGetMethodProp  = nullptr;
    std::vector<std::wstring>  m_encoderNames;  // lowercased; populated by EnumerateCodecs()

    void EnumerateCodecs();

    HRESULT CreateInArchive(const GUID& clsid, IInArchive** ppArc);
    HRESULT CreateOutArchive(const GUID& clsid, IOutArchive** ppArc);
    const GUID& FormatToInGuid(const wchar_t* path) const;
    const GUID& FormatToOutGuid(const wchar_t* format) const;
    static std::wstring ExtOfPath(const wchar_t* path);
};
