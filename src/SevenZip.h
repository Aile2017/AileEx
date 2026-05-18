#pragma once
#include <windows.h>
#include <vector>
#include <map>
#include <string>
#include "ArchiveItem.h"
#include "WorkerThread.h"
#include "7zip/Archive/IArchive.h"

typedef HRESULT (WINAPI *Func_GetNumberOfMethods)(UINT32* numMethods);
typedef HRESULT (WINAPI *Func_GetMethodProperty)(UINT32 index, PROPID propID, PROPVARIANT* value);
typedef HRESULT (WINAPI *Func_GetNumberOfFormats)(UINT32* numFormats);
typedef HRESULT (WINAPI *Func_GetHandlerProperty2)(UINT32 index, PROPID propID, PROPVARIANT* value);

// Format info for the compress dialog: writable formats
struct WritableFormat {
    std::wstring label;  // Display name e.g. "7-Zip (.7z)"
    std::wstring ext;    // Extension e.g. "7z"
};

// Whole-archive properties (for the properties dialog).
// Populated by SevenZip::GetArchiveProperties() via IInArchive::GetArchiveProperty.
struct ArchiveProperties {
    std::wstring formatName;   // kpidType value ("7z","Rar","Zip" etc.); empty if not available
    UINT32       fileCount    = 0;     // aggregate: number of regular files
    UINT32       folderCount  = 0;     // aggregate: number of folders
    UINT64       totalSize    = 0;     // aggregate: total uncompressed size
    UINT64       packedTotal  = 0;     // aggregate: total compressed size
    bool         hasEncrypted = false; // aggregate: true if at least one encrypted entry exists
    std::vector<std::wstring> methods; // aggregate: set of compression methods used (no duplicates, in order of appearance)
    // Key=display-string pairs from IInArchive::GetArchivePropertyInfo / GetArchiveProperty.
    std::vector<std::pair<std::wstring, std::wstring>> rawProps;
};

// Advanced compression options passed to SevenZip::Compress().
// Any empty string means "use default" (property is not sent to 7z.dll).
struct CompressAdvanced {
    std::wstring dictSize;    // "64k","1m","32m","512m","1g" — dictionary size
    std::wstring wordSize;    // "8","32","64","273" — fast bytes (fb)
    std::wstring solidBlock;  // "off","1m","4g" — solid block size (7z only)
    std::wstring threads;     // "1","4","8" — CPU threads (mt)
    std::wstring extra;       // free-form "key=value" pairs (e.g. "mf=bt4 mpass=2")
    // Split volume size. "" = single file; specify as "10m","100m","1g" etc.
    // Valid only for seekable output (7z/zip etc.); ignored for stream-wrapped gz/bz2/xz/tar.
    std::wstring volumeSize;
    // Absolute path to the self-extraction (SFX) module. Empty = no SFX.
    // When non-empty, valid only for format == "7z". The module file is prepended to
    // the compressed .7z data to produce a .exe at outPath.
    // When used with split volumes, the SFX module is prepended only to volume 1 (.001).
    std::wstring sfxModulePath;
};

class SevenZip {
public:
    bool Load(const wchar_t* dllPath = nullptr);
    void Unload();
    bool IsLoaded() const { return m_hDll != nullptr; }
    const std::wstring& GetLoadedName() const { return m_loadedName; }
    // Full path of the loaded 7z.dll. Empty when not loaded.
    std::wstring GetLoadedPath() const;

    // Detect archive format by extension and open, filling items.
    // For split archives (.001/.002/...), extracts the inner archive to a temp file,
    // reopens it, and returns its entries in items. If effectivePath is non-null,
    // writes back the path to use for subsequent Extract/Test calls
    // (normally path; the temp path when auto-unwrapped).
    // Caller is responsible for deleting the temp file.
    HRESULT OpenArchive(const wchar_t* path, std::vector<ArchiveItem>& items,
                        const wchar_t* password = nullptr,
                        std::wstring* effectivePath = nullptr);

    // Extract. indices empty = extract all.
    HRESULT Extract(const wchar_t* archivePath,
                    const std::vector<UINT32>& indices,
                    const wchar_t* destDir,
                    const wchar_t* password,
                    IExtractProgressSink* sink);

    // Retrieves the whole-archive comment. Returns empty for formats/archives without one.
    // Note: the 7z format has no whole-archive comment by spec (per-item kpidComment exists).
    HRESULT GetArchiveComment(const wchar_t* path,
                              const wchar_t* password,
                              std::wstring& out);

    // Overwrites the whole-archive comment in a ZIP file (direct EOCD record patch).
    // Only .zip format is supported. Passing an empty string removes the comment.
    // E_INVALIDARG if the comment exceeds 65535 bytes (ZIP spec limit).
    // Works correctly on ZIP64 archives (>4 GB) since the EOCD is in the same position.
    HRESULT SetZipArchiveComment(const wchar_t* archivePath,
                                 const std::wstring& comment);

    // Retrieves whole-archive properties (for the properties dialog).
    // Fills format-specific metadata from IInArchive::GetArchiveProperty / GetArchivePropertyInfo
    // and aggregates from entry enumeration (file count, total size, etc.).
    // Does not auto-unwrap split archives; opens path directly as a single file.
    HRESULT GetArchiveProperties(const wchar_t* path,
                                 const wchar_t* password,
                                 ArchiveProperties& out);

    // Integrity verification for all entries (passes testMode=1 to IInArchive::Extract).
    // Returns E_FAIL if any entry fails verification.
    HRESULT Test(const wchar_t* archivePath,
                 const wchar_t* password,
                 IExtractProgressSink* sink);

    // Add or update files in an existing archive.
    // - srcPaths: files/folders on disk to add (folders are expanded recursively)
    // - archiveFolder: destination folder inside the archive; "" / nullptr = archive root.
    //   Accepts both '/' and '\' separators (normalized to '\' internally).
    // - If a new entry's archive path conflicts with an existing entry, the new entry overwrites it.
    // - level / method are compression settings for new entries only; existing entries are copied without re-compression.
    // Returns E_NOINTERFACE for formats that do not support writing.
    HRESULT AddToArchive(const wchar_t* archivePath,
                         const std::vector<std::wstring>& srcPaths,
                         const wchar_t* archiveFolder,
                         const wchar_t* password,
                         int level, const wchar_t* method,
                         IExtractProgressSink* sink,
                         const CompressAdvanced* adv = nullptr);

    // Delete entries at the specified indices (copies surviving entries to a new archive).
    // The original file is not modified on failure.
    // Formats that do not support writing (rar/iso/cab etc.) fail at IOutArchive acquisition
    // and return E_NOTIMPL or similar.
    HRESULT DeleteItems(const wchar_t* archivePath,
                        const std::vector<UINT32>& deleteIndices,
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
                     const CompressAdvanced* adv = nullptr,
                     bool encryptHeaders = false);

    // Auto-detect installed 7z.dll from registry or known paths.
    static std::wstring Find7zDll();

    // Returns lowercased encoder names supported by the loaded DLL.
    // Empty if DLL is not loaded or enumeration is unavailable.
    const std::vector<std::wstring>& GetEncoderNames() const { return m_encoderNames; }

    // ext: extension only (no dot, e.g. L"7z"). Case-insensitive.
    bool IsArchiveExt(const wchar_t* ext) const;

    // Writable formats supported by the loaded 7z.dll (RAR not included).
    const std::vector<WritableFormat>& GetWritableFormats() const { return m_writableFormats; }

private:
    HMODULE                      m_hDll               = nullptr;
    std::wstring                 m_loadedName;
    std::wstring                 m_loadedPath;        // Full path to loaded DLL (for caching codec enumeration)
    Func_CreateObject            m_pfnCreateObject    = nullptr;
    Func_GetNumberOfMethods      m_pfnGetNumMethods   = nullptr;
    Func_GetMethodProperty       m_pfnGetMethodProp   = nullptr;
    Func_GetNumberOfFormats      m_pfnGetNumFormats   = nullptr;
    Func_GetHandlerProperty2     m_pfnGetHandlerProp2 = nullptr;
    std::vector<std::wstring>    m_encoderNames;   // lowercased; populated by EnumerateCodecs()
    std::map<std::wstring, GUID> m_extToClsid;     // extension (lowercase) → CLSID
    std::vector<WritableFormat>  m_writableFormats; // writable formats (for UI)
    // Cache: path → actual format CLSID after RAR5→RAR4 fallback detection
    std::map<std::wstring, GUID> m_pathFormatCache;
    // Cache: (path + password_hash + format_guid) → ArchiveItem vector
    // Keyed as: std::wstring composed of path + "|" + password_hash + "|" + guid_hex
    // Limit: 100 entries (oldest evicted)
    struct CacheEntry {
        std::vector<ArchiveItem> items;
        int order;  // for LRU eviction
    };
    std::map<std::wstring, CacheEntry> m_itemsCache;
    int m_cacheOrder = 0;
    static constexpr int MAX_CACHE_ENTRIES = 100;
    
    // Build cache key from path, password, and format GUID
    static std::wstring BuildCacheKey(const wchar_t* path, const wchar_t* password, const GUID& fmt);
    
    // Hash password to short string (for cache key)
    static UINT32 HashPassword(const wchar_t* password);

    void EnumerateCodecs();
    void EnumerateFormats();

    HRESULT CreateInArchive(const GUID& clsid, IInArchive** ppArc);
    HRESULT CreateOutArchive(const GUID& clsid, IOutArchive** ppArc);
    GUID FormatToInGuid(const wchar_t* path) const;
    GUID FormatToOutGuid(const wchar_t* format) const;
    // Open archive with RAR5→RAR4 fallback, caching result for future calls
    HRESULT OpenArchiveWithFallback(const wchar_t* path, const GUID& primaryGuid,
                                    IInStream* fileSpec, const UInt64& maxCheck,
                                    IArchiveOpenCallback* openCb, IInArchive*& archive);
    static std::wstring ExtOfPath(const wchar_t* path);
};
