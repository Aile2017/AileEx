#define DEFINE_7Z_GUIDS
#include "SevenZip.h"
#include "7zip/IPassword.h"
#include <shlwapi.h>
#include <shlobj.h>     // SHCreateDirectoryExW
#include <ole2.h>       // PropVariantClear, PropVariantInit
#include <oleauto.h>    // SysAllocString, SysFreeString
#include <wctype.h>

// ============================================================
// CInFileStream — wraps a Win32 file handle as IInStream
// ============================================================
class CInFileStream : public IInStream {
public:
    explicit CInFileStream() = default;
    ~CInFileStream() { if (m_hFile != INVALID_HANDLE_VALUE) CloseHandle(m_hFile); }

    bool Open(const wchar_t* path) {
        m_hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        return m_hFile != INVALID_HANDLE_VALUE;
    }

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (iid == IID_IUnknown || iid == IID_ISequentialInStream)
            *ppv = static_cast<ISequentialInStream*>(this);
        else if (iid == IID_IInStream)
            *ppv = static_cast<IInStream*>(this);
        else { *ppv = nullptr; return E_NOINTERFACE; }
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return (ULONG)InterlockedIncrement(&m_refCount);
    }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG r = InterlockedDecrement(&m_refCount);
        if (r == 0) delete this;
        return (ULONG)r;
    }

    // ISequentialInStream
    HRESULT STDMETHODCALLTYPE Read(void* data, UInt32 size, UInt32* processedSize) override {
        if (processedSize) *processedSize = 0;
        if (m_hFile == INVALID_HANDLE_VALUE) return E_FAIL;
        DWORD read = 0;
        BOOL ok = ReadFile(m_hFile, data, size, &read, nullptr);
        if (processedSize) *processedSize = read;
        return ok ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    }

    // IInStream
    // seekOrigin: 0=begin, 1=current, 2=end  (same as FILE_BEGIN/CURRENT/END)
    HRESULT STDMETHODCALLTYPE Seek(Int64 offset, UInt32 seekOrigin, UInt64* newPosition) override {
        if (m_hFile == INVALID_HANDLE_VALUE) return E_FAIL;
        LARGE_INTEGER li, np;
        li.QuadPart = offset;
        if (!SetFilePointerEx(m_hFile, li, &np, seekOrigin))
            return HRESULT_FROM_WIN32(GetLastError());
        if (newPosition) *newPosition = (UInt64)np.QuadPart;
        return S_OK;
    }

private:
    HANDLE m_hFile     = INVALID_HANDLE_VALUE;
    LONG   m_refCount  = 1;
};

// ============================================================
// COpenCallback — IArchiveOpenCallback + ICryptoGetTextPassword
// ============================================================
class COpenCallback : public IArchiveOpenCallback, public ICryptoGetTextPassword {
public:
    explicit COpenCallback(const wchar_t* pw) { if (pw) m_password = pw; }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (iid == IID_IUnknown || iid == IID_IArchiveOpenCallback)
            *ppv = static_cast<IArchiveOpenCallback*>(this);
        else if (iid == IID_ICryptoGetTextPassword)
            *ppv = static_cast<ICryptoGetTextPassword*>(this);
        else { *ppv = nullptr; return E_NOINTERFACE; }
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return (ULONG)InterlockedIncrement(&m_refCount);
    }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG r = InterlockedDecrement(&m_refCount);
        if (r == 0) delete this;
        return (ULONG)r;
    }

    HRESULT STDMETHODCALLTYPE SetTotal(const UInt64*, const UInt64*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetCompleted(const UInt64*, const UInt64*) override { return S_OK; }

    HRESULT STDMETHODCALLTYPE CryptoGetTextPassword(BSTR* password) override {
        *password = SysAllocString(m_password.c_str());
        return *password ? S_OK : E_OUTOFMEMORY;
    }

private:
    std::wstring m_password;
    LONG         m_refCount = 1;
};

// ============================================================
// ============================================================
// SevenZip — Load / Unload
// ============================================================

// Auto-detect 7z.dll from registry (7-Zip install) or known paths.
std::wstring SevenZip::Find7zDll() {
    // 7-Zip stores its install path in HKLM\SOFTWARE\7-Zip, value "Path64" or "Path"
    for (HKEY hRoot : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) {
        for (REGSAM sam : {(REGSAM)(KEY_READ | KEY_WOW64_64KEY),
                           (REGSAM)(KEY_READ | KEY_WOW64_32KEY)}) {
            HKEY hKey = nullptr;
            if (RegOpenKeyExW(hRoot, L"SOFTWARE\\7-Zip", 0, sam, &hKey) != ERROR_SUCCESS)
                continue;
            for (const wchar_t* val : {L"Path64", L"Path"}) {
                wchar_t buf[MAX_PATH] = {};
                DWORD sz = sizeof(buf), type = 0;
                if (RegQueryValueExW(hKey, val, nullptr, &type,
                                     (BYTE*)buf, &sz) == ERROR_SUCCESS && type == REG_SZ) {
                    RegCloseKey(hKey);
                    std::wstring p(buf);
                    if (!p.empty() && p.back() != L'\\') p += L'\\';
                    p += L"7z.dll";
                    if (PathFileExistsW(p.c_str())) return p;
                }
            }
            RegCloseKey(hKey);
        }
    }
    // Fallback: known install paths
    for (const wchar_t* env : {L"ProgramFiles", L"ProgramFiles(x86)"}) {
        wchar_t pf[MAX_PATH] = {};
        if (GetEnvironmentVariableW(env, pf, MAX_PATH)) {
            std::wstring p = std::wstring(pf) + L"\\7-Zip\\7z.dll";
            if (PathFileExistsW(p.c_str())) return p;
        }
    }
    return {};
}

bool SevenZip::Load(const wchar_t* dllPath) {
    wchar_t buf[MAX_PATH] = {};
    if (!dllPath || !dllPath[0]) {
        std::wstring found = Find7zDll();
        if (!found.empty()) {
            wcsncpy_s(buf, found.c_str(), MAX_PATH - 1);
        } else {
            GetModuleFileNameW(nullptr, buf, MAX_PATH);
            wchar_t* p = wcsrchr(buf, L'\\');
            if (p) wcscpy_s(p + 1, MAX_PATH - (DWORD)(p + 1 - buf), L"7z.dll");
        }
        dllPath = buf;
    }
    m_hDll = LoadLibraryW(dllPath);
    if (!m_hDll) return false;
    m_pfnCreateObject = (Func_CreateObject)GetProcAddress(m_hDll, "CreateObject");
    if (!m_pfnCreateObject) { FreeLibrary(m_hDll); m_hDll = nullptr; return false; }
    wchar_t nameBuf[MAX_PATH] = {};
    GetModuleFileNameW(m_hDll, nameBuf, MAX_PATH);
    const wchar_t* leaf = wcsrchr(nameBuf, L'\\');
    m_loadedName = leaf ? (leaf + 1) : nameBuf;
    return true;
}
void SevenZip::Unload() {
    if (m_hDll) { FreeLibrary(m_hDll); m_hDll = nullptr; }
    m_pfnCreateObject = nullptr;
}

// ============================================================
// Format helpers
// ============================================================

std::wstring SevenZip::ExtOfPath(const wchar_t* path) {
    const wchar_t* dot = wcsrchr(path, L'.');
    if (!dot) return L"";
    std::wstring ext = dot + 1;
    for (auto& c : ext) c = (wchar_t)towlower(c);
    return ext;
}

const GUID& SevenZip::FormatToInGuid(const wchar_t* path) const {
    std::wstring ext = ExtOfPath(path);
    if (ext == L"7z")  return CLSID_Format_7z;
    if (ext == L"zip" || ext == L"jar") return CLSID_Format_Zip;
    if (ext == L"tar") return CLSID_Format_Tar;
    if (ext == L"gz")  return CLSID_Format_GZip;
    if (ext == L"bz2") return CLSID_Format_BZip2;
    if (ext == L"xz")  return CLSID_Format_Xz;
    if (ext == L"rar") return CLSID_Format_Rar5;
    if (ext == L"cab") return CLSID_Format_Cab;
    if (ext == L"iso") return CLSID_Format_Iso;
    return CLSID_Format_7z;
}

const GUID& SevenZip::FormatToOutGuid(const wchar_t* format) const {
    if (!format) return CLSID_Format_7z;
    std::wstring f = format;
    if (f == L"zip") return CLSID_Format_Zip;
    if (f == L"tar") return CLSID_Format_Tar;
    if (f == L"gz")  return CLSID_Format_GZip;
    if (f == L"bz2") return CLSID_Format_BZip2;
    if (f == L"xz")  return CLSID_Format_Xz;
    return CLSID_Format_7z;
}

HRESULT SevenZip::CreateInArchive(const GUID& clsid, IInArchive** ppArc) {
    if (!m_pfnCreateObject) return E_FAIL;
    return m_pfnCreateObject(&clsid, &IID_IInArchive, (void**)ppArc);
}

HRESULT SevenZip::CreateOutArchive(const GUID& clsid, IOutArchive** ppArc) {
    if (!m_pfnCreateObject) return E_FAIL;
    return m_pfnCreateObject(&clsid, &IID_IOutArchive, (void**)ppArc);
}

// ============================================================
// CTempOutStream / CTarExtractCallback
// Minimal helpers for single-item extraction used when opening
// stream-wrapped tar archives (.tar.gz, .tar.bz2, .tar.xz).
// ============================================================
class CTempOutStream : public ISequentialOutStream {
public:
    bool Create(const wchar_t* path) {
        m_hFile = CreateFileW(path, GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        return m_hFile != INVALID_HANDLE_VALUE;
    }
    ~CTempOutStream() { if (m_hFile != INVALID_HANDLE_VALUE) CloseHandle(m_hFile); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppv) override {
        if (iid == IID_IUnknown || iid == IID_ISequentialOutStream)
            { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef()  override { return (ULONG)InterlockedIncrement(&m_refCount); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG r = InterlockedDecrement(&m_refCount);
        if (!r) delete this;
        return (ULONG)r;
    }
    HRESULT STDMETHODCALLTYPE Write(const void* data, UInt32 size, UInt32* processed) override {
        DWORD written = 0;
        WriteFile(m_hFile, data, size, &written, nullptr);
        if (processed) *processed = written;
        return S_OK;
    }
private:
    HANDLE m_hFile    = INVALID_HANDLE_VALUE;
    LONG   m_refCount = 1;
};

class CTarExtractCallback : public IArchiveExtractCallback {
public:
    explicit CTarExtractCallback(CTempOutStream* s) : m_stream(s) { s->AddRef(); }
    ~CTarExtractCallback() { m_stream->Release(); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppv) override {
        if (iid == IID_IUnknown || iid == IID_IArchiveExtractCallback)
            { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef()  override { return (ULONG)InterlockedIncrement(&m_refCount); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG r = InterlockedDecrement(&m_refCount);
        if (!r) delete this;
        return (ULONG)r;
    }
    HRESULT STDMETHODCALLTYPE GetStream(UInt32, ISequentialOutStream** s, Int32 mode) override {
        if (mode != 0) { *s = nullptr; return S_OK; }  // 0 = kExtract
        m_stream->AddRef();
        *s = m_stream;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE PrepareOperation(Int32) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetOperationResult(Int32) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetTotal(UInt64) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetCompleted(const UInt64*) override { return S_OK; }
private:
    CTempOutStream* m_stream;
    LONG            m_refCount = 1;
};
// ============================================================
// OpenArchive — enumerate all entries into items vector
// ============================================================

HRESULT SevenZip::OpenArchive(const wchar_t* path, std::vector<ArchiveItem>& items,
                               const wchar_t* password) {
    if (!IsLoaded()) return E_FAIL;
    items.clear();

    // Open file stream
    CInFileStream* fileSpec = new CInFileStream();
    if (!fileSpec->Open(path)) {
        DWORD err = GetLastError();
        fileSpec->Release();
        return HRESULT_FROM_WIN32(err ? err : ERROR_OPEN_FAILED);
    }

    // Try primary format (from extension); for RAR also try old format
    IInArchive* archive = nullptr;
    const GUID& primaryGuid = FormatToInGuid(path);
    HRESULT hr = CreateInArchive(primaryGuid, &archive);

    if (FAILED(hr) || !archive) {
        fileSpec->Release();
        return FAILED(hr) ? hr : E_FAIL;
    }

    // Open archive
    COpenCallback* openCb = new COpenCallback(password);
    const UInt64 maxCheck = 1ULL << 23;
    hr = archive->Open(fileSpec, &maxCheck, openCb);
    openCb->Release();

    // S_FALSE means "not this format"; treat it like failure for fallback purposes
    // If RAR5 mismatched, try RAR4
    if ((FAILED(hr) || hr == S_FALSE) && &primaryGuid == &CLSID_Format_Rar5) {
        archive->Release();
        hr = CreateInArchive(CLSID_Format_Rar, &archive);
        if (SUCCEEDED(hr) && archive) {
            fileSpec->Seek(0, 0, nullptr);  // rewind
            COpenCallback* cb2 = new COpenCallback(password);
            hr = archive->Open(fileSpec, &maxCheck, cb2);
            cb2->Release();
        }
    }

    fileSpec->Release();

    if (FAILED(hr) || hr == S_FALSE) {
        if (archive) archive->Release();
        return FAILED(hr) ? hr : E_FAIL;
    }

    // Enumerate items
    UInt32 count = 0;
    archive->GetNumberOfItems(&count);
    items.reserve(count);

    for (UInt32 i = 0; i < count; ++i) {
        ArchiveItem it;
        it.index = i;

        PROPVARIANT prop;

        // Path
        PropVariantInit(&prop);
        archive->GetProperty(i, kpidPath, &prop);
        if (prop.vt == VT_BSTR && prop.bstrVal) {
            it.path = prop.bstrVal;
            for (auto& c : it.path) if (c == L'\\') c = L'/';
        }
        PropVariantClear(&prop);

        // IsDir
        PropVariantInit(&prop);
        archive->GetProperty(i, kpidIsDir, &prop);
        it.isDir = (prop.vt == VT_BOOL && prop.boolVal != VARIANT_FALSE);
        PropVariantClear(&prop);

        // Strip trailing slash from directory paths
        while (!it.path.empty() && it.path.back() == L'/') it.path.pop_back();

        // Leaf name
        auto slash = it.path.rfind(L'/');
        it.name = (slash != std::wstring::npos) ? it.path.substr(slash + 1) : it.path;

        // Size
        PropVariantInit(&prop);
        archive->GetProperty(i, kpidSize, &prop);
        it.size = (prop.vt == VT_UI8) ? prop.uhVal.QuadPart : 0;
        PropVariantClear(&prop);

        // Packed size
        PropVariantInit(&prop);
        archive->GetProperty(i, kpidPackSize, &prop);
        it.packedSize = (prop.vt == VT_UI8) ? prop.uhVal.QuadPart : 0;
        PropVariantClear(&prop);

        // Method
        PropVariantInit(&prop);
        archive->GetProperty(i, kpidMethod, &prop);
        if (prop.vt == VT_BSTR && prop.bstrVal) it.method = prop.bstrVal;
        PropVariantClear(&prop);

        // Modified time
        PropVariantInit(&prop);
        archive->GetProperty(i, kpidMTime, &prop);
        if (prop.vt == VT_FILETIME) it.mtime = prop.filetime;
        PropVariantClear(&prop);

        // Creation time
        PropVariantInit(&prop);
        archive->GetProperty(i, kpidCTime, &prop);
        if (prop.vt == VT_FILETIME) it.ctime = prop.filetime;
        PropVariantClear(&prop);

        // Last access time
        PropVariantInit(&prop);
        archive->GetProperty(i, kpidATime, &prop);
        if (prop.vt == VT_FILETIME) it.atime = prop.filetime;
        PropVariantClear(&prop);

        // CRC
        PropVariantInit(&prop);
        archive->GetProperty(i, kpidCRC, &prop);
        if (prop.vt == VT_UI4) { it.crc = prop.ulVal; it.hasCrc = true; }
        PropVariantClear(&prop);

        // Encrypted
        PropVariantInit(&prop);
        archive->GetProperty(i, kpidEncrypted, &prop);
        if (prop.vt == VT_BOOL) it.encrypted = (prop.boolVal != VARIANT_FALSE);
        PropVariantClear(&prop);

        // File attributes
        PropVariantInit(&prop);
        archive->GetProperty(i, kpidAttrib, &prop);
        if (prop.vt == VT_UI4) it.attrib = prop.ulVal;
        PropVariantClear(&prop);

        // Host OS
        PropVariantInit(&prop);
        archive->GetProperty(i, kpidHostOS, &prop);
        if (prop.vt == VT_BSTR && prop.bstrVal) it.hostOS = prop.bstrVal;
        PropVariantClear(&prop);

        // Comment
        PropVariantInit(&prop);
        archive->GetProperty(i, kpidComment, &prop);
        if (prop.vt == VT_BSTR && prop.bstrVal) it.comment = prop.bstrVal;
        PropVariantClear(&prop);

        items.push_back(std::move(it));
    }

    // Transparent tar-in-stream detection: .tar.gz / .tar.bz2 / .tar.xz
    // When the outer archive wraps exactly one non-directory item whose name ends
    // in ".tar", extract it to a temp file and re-enumerate so the caller sees
    // the inner tar contents directly.
    {
        auto getExt = [](const wchar_t* p) -> std::wstring {
            const wchar_t* d = wcsrchr(p, L'.');
            if (!d) return L"";
            std::wstring e(d + 1);
            for (auto& c : e) c = (wchar_t)towlower(c);
            return e;
        };
        std::wstring outerExt = getExt(path);
        if ((outerExt == L"gz" || outerExt == L"bz2" || outerExt == L"xz") &&
            items.size() == 1 && !items[0].isDir)
        {
            // Determine inner name: prefer item path/name, but bz2/xz may
            // store no filename — fall back to stripping the outer extension
            const wchar_t* innerName = (!items[0].path.empty())
                ? items[0].path.c_str()
                : (!items[0].name.empty() ? items[0].name.c_str() : nullptr);
            bool likelyTar = false;
            if (innerName && getExt(innerName) == L"tar") {
                likelyTar = true;
            } else {
                // e.g. "aa.tar.bz2" → strip ".bz2" → "aa.tar" → ext "tar"
                std::wstring outerBase(path);
                auto lastDot = outerBase.rfind(L'.');
                if (lastDot != std::wstring::npos) {
                    if (getExt(outerBase.substr(0, lastDot).c_str()) == L"tar")
                        likelyTar = true;
                }
            }
            if (likelyTar) {
                wchar_t tmpDir[MAX_PATH];
                GetTempPathW(MAX_PATH, tmpDir);
                wchar_t tmpTar[MAX_PATH];
                swprintf_s(tmpTar, L"%sailex_%llu.tar",
                           tmpDir, (unsigned long long)GetTickCount64());
                CTempOutStream* outStream = new CTempOutStream();
                if (outStream->Create(tmpTar)) {
                    CTarExtractCallback* cb = new CTarExtractCallback(outStream);
                    UInt32 zeroIdx = 0;
                    HRESULT hrEx = archive->Extract(&zeroIdx, 1, 0, cb);
                    cb->Release();
                    outStream->Release();
                    if (SUCCEEDED(hrEx)) {
                        std::vector<ArchiveItem> tarItems;
                        HRESULT hrTar = OpenArchive(tmpTar, tarItems, password);
                        if (SUCCEEDED(hrTar))
                            items = std::move(tarItems);
                    }
                } else {
                    outStream->Release();
                }
                DeleteFileW(tmpTar);
            } // if likelyTar
        }
    }

    archive->Release();
    return S_OK;
}

// ============================================================
// COutFileStream — wraps a Win32 file handle as IOutStream
// IOutStream (seekable) is required for archive output so 7z.dll can
// seek back to write the archive header after compressing all items.
// ============================================================
class COutFileStream : public IOutStream {
public:
    ~COutFileStream() {
        if (m_hFile != INVALID_HANDLE_VALUE) CloseHandle(m_hFile);
    }

    bool Create(const wchar_t* path) {
        // Ensure parent directories exist
        std::wstring dir = path;
        auto slash = dir.rfind(L'\\');
        if (slash == std::wstring::npos) slash = dir.rfind(L'/');
        if (slash != std::wstring::npos) {
            dir = dir.substr(0, slash);
            SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
        }
        m_hFile = CreateFileW(path, GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        return m_hFile != INVALID_HANDLE_VALUE;
    }

    void SetMTime(const FILETIME* ft) {
        if (m_hFile != INVALID_HANDLE_VALUE && ft)
            SetFileTime(m_hFile, nullptr, nullptr, ft);
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (iid == IID_IUnknown || iid == IID_ISequentialOutStream)
            *ppv = static_cast<ISequentialOutStream*>(this);
        else if (iid == IID_IOutStream)
            *ppv = static_cast<IOutStream*>(this);
        else { *ppv = nullptr; return E_NOINTERFACE; }
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return (ULONG)InterlockedIncrement(&m_refCount);
    }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG r = InterlockedDecrement(&m_refCount);
        if (r == 0) delete this;
        return (ULONG)r;
    }

    HRESULT STDMETHODCALLTYPE Write(const void* data, UInt32 size, UInt32* processedSize) override {
        if (processedSize) *processedSize = 0;
        if (m_hFile == INVALID_HANDLE_VALUE) return E_FAIL;
        DWORD written = 0;
        BOOL ok = WriteFile(m_hFile, data, size, &written, nullptr);
        if (processedSize) *processedSize = written;
        return ok ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    }

    // seekOrigin: 0=FILE_BEGIN, 1=FILE_CURRENT, 2=FILE_END
    HRESULT STDMETHODCALLTYPE Seek(Int64 offset, UInt32 seekOrigin, UInt64* newPosition) override {
        if (m_hFile == INVALID_HANDLE_VALUE) return E_FAIL;
        LARGE_INTEGER li, np;
        li.QuadPart = offset;
        if (!SetFilePointerEx(m_hFile, li, &np, seekOrigin))
            return HRESULT_FROM_WIN32(GetLastError());
        if (newPosition) *newPosition = (UInt64)np.QuadPart;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetSize(UInt64 newSize) override {
        if (m_hFile == INVALID_HANDLE_VALUE) return E_FAIL;
        LARGE_INTEGER zero = {}, cur = {};
        SetFilePointerEx(m_hFile, zero, &cur, FILE_CURRENT);  // save position
        LARGE_INTEGER li; li.QuadPart = (Int64)newSize;
        if (!SetFilePointerEx(m_hFile, li, nullptr, FILE_BEGIN))
            return HRESULT_FROM_WIN32(GetLastError());
        if (!SetEndOfFile(m_hFile))
            return HRESULT_FROM_WIN32(GetLastError());
        SetFilePointerEx(m_hFile, cur, nullptr, FILE_BEGIN);  // restore position
        return S_OK;
    }

private:
    HANDLE m_hFile    = INVALID_HANDLE_VALUE;
    LONG   m_refCount = 1;
};

// ============================================================
// CExtractCallback — IArchiveExtractCallback + ICryptoGetTextPassword
// ============================================================
class CExtractCallback : public IArchiveExtractCallback, public ICryptoGetTextPassword {
public:
    CExtractCallback(IInArchive* archive, const wchar_t* destDir,
                     const wchar_t* password, IExtractProgressSink* sink)
        : m_archive(archive), m_destDir(destDir), m_sink(sink) {
        if (password) m_password = password;
        archive->AddRef();
    }

    ~CExtractCallback() {
        m_archive->Release();
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (iid == IID_IUnknown || iid == IID_IArchiveExtractCallback)
            *ppv = static_cast<IArchiveExtractCallback*>(this);
        else if (iid == IID_ICryptoGetTextPassword)
            *ppv = static_cast<ICryptoGetTextPassword*>(this);
        else { *ppv = nullptr; return E_NOINTERFACE; }
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return (ULONG)InterlockedIncrement(&m_refCount);
    }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG r = InterlockedDecrement(&m_refCount);
        if (r == 0) delete this;
        return (ULONG)r;
    }

    // IProgress
    HRESULT STDMETHODCALLTYPE SetTotal(UInt64 total) override {
        if (m_sink) m_sink->OnSetTotal(total);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetCompleted(const UInt64* done) override {
        if (m_sink && m_sink->IsCancelled()) return E_ABORT;
        if (m_sink && done) m_sink->OnProgress(*done, m_currentFile.c_str());
        return S_OK;
    }

    // IArchiveExtractCallback
    HRESULT STDMETHODCALLTYPE GetStream(UInt32 index, ISequentialOutStream** outStream,
                                        Int32 askExtractMode) override {
        *outStream = nullptr;
        if (askExtractMode != NArchive::NExtract::NAskMode::kExtract) return S_OK;

        // Get path of this item
        PROPVARIANT prop;
        PropVariantInit(&prop);
        m_archive->GetProperty(index, kpidPath, &prop);
        std::wstring itemPath = (prop.vt == VT_BSTR && prop.bstrVal) ? prop.bstrVal : L"";
        PropVariantClear(&prop);

        // Normalize separators
        for (auto& c : itemPath) if (c == L'/') c = L'\\';

        // Check if directory
        PropVariantInit(&prop);
        m_archive->GetProperty(index, kpidIsDir, &prop);
        bool isDir = (prop.vt == VT_BOOL && prop.boolVal != VARIANT_FALSE);
        PropVariantClear(&prop);

        std::wstring fullPath = m_destDir + L"\\" + itemPath;
        m_currentFile = itemPath;

        if (isDir) {
            SHCreateDirectoryExW(nullptr, fullPath.c_str(), nullptr);
            m_currentItemIndex = (int)index;
            m_currentIsDir     = true;
            return S_OK;
        }

        m_currentIsDir     = false;
        m_currentItemIndex = (int)index;

        COutFileStream* fileOut = new COutFileStream();
        if (!fileOut->Create(fullPath.c_str())) {
            fileOut->Release();
            return S_FALSE;  // skip rather than fail the whole extraction
        }
        *outStream = fileOut;
        m_currentOut = fileOut;
        fileOut->AddRef();  // keep our own reference for SetOperationResult
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE PrepareOperation(Int32 /*askMode*/) override { return S_OK; }

    HRESULT STDMETHODCALLTYPE SetOperationResult(Int32 opRes) override {
        // Set file timestamp
        if (!m_currentIsDir && m_currentOut) {
            PROPVARIANT prop;
            PropVariantInit(&prop);
            m_archive->GetProperty((UInt32)m_currentItemIndex, kpidMTime, &prop);
            if (prop.vt == VT_FILETIME) m_currentOut->SetMTime(&prop.filetime);
            PropVariantClear(&prop);
            m_currentOut->Release();
            m_currentOut = nullptr;
        }
        if (opRes == NArchive::NExtract::NOperationResult::kWrongPassword)
            return E_ACCESSDENIED;
        return S_OK;
    }

    // ICryptoGetTextPassword
    HRESULT STDMETHODCALLTYPE CryptoGetTextPassword(BSTR* pw) override {
        *pw = SysAllocString(m_password.c_str());
        return *pw ? S_OK : E_OUTOFMEMORY;
    }

private:
    IInArchive*           m_archive;
    std::wstring          m_destDir;
    std::wstring          m_password;
    IExtractProgressSink* m_sink;
    std::wstring          m_currentFile;
    int                   m_currentItemIndex = -1;
    bool                  m_currentIsDir     = false;
    COutFileStream*       m_currentOut       = nullptr;
    LONG                  m_refCount         = 1;
};

// ============================================================
// Extract
// ============================================================

HRESULT SevenZip::Extract(const wchar_t* archivePath,
                           const std::vector<UINT32>& indices,
                           const wchar_t* destDir,
                           const wchar_t* password,
                           IExtractProgressSink* sink) {
    if (!IsLoaded()) return E_FAIL;

    // Re-open the archive (we don't cache the IInArchive handle)
    CInFileStream* fileSpec = new CInFileStream();
    if (!fileSpec->Open(archivePath)) {
        fileSpec->Release();
        return HRESULT_FROM_WIN32(GetLastError());
    }

    const GUID& clsid = FormatToInGuid(archivePath);
    IInArchive*  archive = nullptr;
    HRESULT hr = CreateInArchive(clsid, &archive);
    if (FAILED(hr)) { fileSpec->Release(); return hr; }

    COpenCallback* openCb = new COpenCallback(password);
    const UInt64 maxCheck = 1ULL << 23;
    hr = archive->Open(fileSpec, &maxCheck, openCb);
    openCb->Release();

    // Fallback: RAR5 mismatched (FAILED or S_FALSE) → try RAR4
    if ((FAILED(hr) || hr == S_FALSE) && &clsid == &CLSID_Format_Rar5) {
        archive->Release(); archive = nullptr;
        hr = CreateInArchive(CLSID_Format_Rar, &archive);
        if (SUCCEEDED(hr) && archive) {
            fileSpec->Seek(0, 0, nullptr);
            COpenCallback* cb2 = new COpenCallback(password);
            hr = archive->Open(fileSpec, &maxCheck, cb2);
            cb2->Release();
        }
    }

    fileSpec->Release();
    if (FAILED(hr) || hr == S_FALSE) {
        if (archive) archive->Release();
        return FAILED(hr) ? hr : E_FAIL;
    }

    // Ensure destination directory exists
    SHCreateDirectoryExW(nullptr, destDir, nullptr);

    CExtractCallback* cb = new CExtractCallback(archive, destDir, password, sink);

    if (indices.empty()) {
        // Extract all
        hr = archive->Extract(nullptr, (UInt32)-1, 0, cb);
    } else {
        hr = archive->Extract(indices.data(), (UInt32)indices.size(), 0, cb);
    }

    cb->Release();
    archive->Release();
    return hr;
}

// ============================================================
// SrcEntry — file/dir entry for compression
// ============================================================
struct SrcEntry {
    std::wstring diskPath;
    std::wstring archivePath;
    bool         isDir;
    UINT64       size;
    FILETIME     mtime;
};

static void EnumeratePaths(const std::vector<std::wstring>& srcPaths,
                            std::vector<SrcEntry>& entries) {
    for (const auto& src : srcPaths) {
        DWORD attrs = GetFileAttributesW(src.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) continue;

        if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
            // Compute base name (top-level archive folder name)
            std::wstring dir = src;
            while (!dir.empty() && (dir.back() == L'\\' || dir.back() == L'/'))
                dir.pop_back();
            auto slash = dir.rfind(L'\\');
            if (slash == std::wstring::npos) slash = dir.rfind(L'/');
            std::wstring baseName = (slash != std::wstring::npos) ? dir.substr(slash + 1) : dir;

            // Add the directory entry itself
            WIN32_FILE_ATTRIBUTE_DATA fad{};
            GetFileAttributesExW(src.c_str(), GetFileExInfoStandard, &fad);
            entries.push_back({ src, baseName, true, 0, fad.ftLastWriteTime });

            // BFS/DFS over children
            struct Job { std::wstring diskDir, archDir; };
            std::vector<Job> stack{ { src, baseName } };
            while (!stack.empty()) {
                auto job = stack.back(); stack.pop_back();
                std::wstring pat = job.diskDir;
                if (pat.back() != L'\\') pat += L'\\';
                pat += L'*';

                WIN32_FIND_DATAW fd;
                HANDLE hFind = FindFirstFileW(pat.c_str(), &fd);
                if (hFind == INVALID_HANDLE_VALUE) continue;
                do {
                    if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
                        continue;
                    std::wstring childDisk = job.diskDir + L'\\' + fd.cFileName;
                    std::wstring childArch = job.archDir  + L'\\' + fd.cFileName;
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                        entries.push_back({ childDisk, childArch, true, 0, fd.ftLastWriteTime });
                        stack.push_back({ childDisk, childArch });
                    } else {
                        UINT64 sz = ((UINT64)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
                        entries.push_back({ childDisk, childArch, false, sz, fd.ftLastWriteTime });
                    }
                } while (FindNextFileW(hFind, &fd));
                FindClose(hFind);
            }
        } else {
            // Single file: archive path = filename only
            auto slash = src.rfind(L'\\');
            if (slash == std::wstring::npos) slash = src.rfind(L'/');
            std::wstring name = (slash != std::wstring::npos) ? src.substr(slash + 1) : src;
            WIN32_FILE_ATTRIBUTE_DATA fad{};
            GetFileAttributesExW(src.c_str(), GetFileExInfoStandard, &fad);
            UINT64 sz = ((UINT64)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
            entries.push_back({ src, name, false, sz, fad.ftLastWriteTime });
        }
    }
}

// ============================================================
// CUpdateCallback — IArchiveUpdateCallback + ICryptoGetTextPassword2
// ============================================================
class CUpdateCallback : public IArchiveUpdateCallback, public ICryptoGetTextPassword2 {
public:
    CUpdateCallback(std::vector<SrcEntry> entries, const wchar_t* password,
                    IExtractProgressSink* sink)
        : m_entries(std::move(entries)), m_sink(sink) {
        if (password) m_password = password;
    }

    UInt32 Count() const { return (UInt32)m_entries.size(); }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (iid == IID_IUnknown || iid == IID_IArchiveUpdateCallback)
            *ppv = static_cast<IArchiveUpdateCallback*>(this);
        else if (iid == IID_ICryptoGetTextPassword2)
            *ppv = static_cast<ICryptoGetTextPassword2*>(this);
        else { *ppv = nullptr; return E_NOINTERFACE; }
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return (ULONG)InterlockedIncrement(&m_refCount);
    }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG r = InterlockedDecrement(&m_refCount);
        if (r == 0) delete this;
        return (ULONG)r;
    }

    // IProgress
    HRESULT STDMETHODCALLTYPE SetTotal(UInt64 total) override {
        if (m_sink) m_sink->OnSetTotal(total);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetCompleted(const UInt64* done) override {
        if (m_sink && m_sink->IsCancelled()) return E_ABORT;
        if (m_sink && done) {
            const wchar_t* name = m_currentName.empty() ? nullptr : m_currentName.c_str();
            m_sink->OnProgress(*done, name);
        }
        return S_OK;
    }

    // IArchiveUpdateCallback
    HRESULT STDMETHODCALLTYPE GetUpdateItemInfo(UInt32 /*index*/,
                                                Int32* newData,
                                                Int32* newProperties,
                                                UInt32* indexInArchive) override {
        if (newData)        *newData        = 1;
        if (newProperties)  *newProperties  = 1;
        if (indexInArchive) *indexInArchive = (UInt32)-1;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetProperty(UInt32 index, PROPID propID,
                                           PROPVARIANT* value) override {
        PropVariantInit(value);
        if (index >= m_entries.size()) return E_INVALIDARG;
        const auto& e = m_entries[index];
        switch (propID) {
        case kpidPath:
            value->vt = VT_BSTR;
            value->bstrVal = SysAllocString(e.archivePath.c_str());
            return value->bstrVal ? S_OK : E_OUTOFMEMORY;
        case kpidIsDir:
            value->vt = VT_BOOL;
            value->boolVal = e.isDir ? VARIANT_TRUE : VARIANT_FALSE;
            return S_OK;
        case kpidSize:
            value->vt = VT_UI8;
            value->uhVal.QuadPart = e.size;
            return S_OK;
        case kpidMTime:
            value->vt = VT_FILETIME;
            value->filetime = e.mtime;
            return S_OK;
        case kpidAttrib:
            value->vt = VT_UI4;
            value->ulVal = e.isDir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
            return S_OK;
        default:
            return S_OK;
        }
    }

    HRESULT STDMETHODCALLTYPE GetStream(UInt32 index, ISequentialInStream** inStream) override {
        *inStream = nullptr;
        if (index >= m_entries.size()) return E_INVALIDARG;
        const auto& e = m_entries[index];
        m_currentName = e.archivePath;
        if (e.isDir) return S_OK;

        CInFileStream* s = new CInFileStream();
        if (!s->Open(e.diskPath.c_str())) {
            s->Release();
            return HRESULT_FROM_WIN32(GetLastError());
        }
        *inStream = s;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetOperationResult(Int32 /*opResult*/) override {
        return S_OK;
    }

    // ICryptoGetTextPassword2
    HRESULT STDMETHODCALLTYPE CryptoGetTextPassword2(Int32* passwordIsDefined,
                                                      BSTR* password) override {
        bool hasPw = !m_password.empty();
        if (passwordIsDefined) *passwordIsDefined = hasPw ? 1 : 0;
        *password = SysAllocString(m_password.c_str());
        return *password ? S_OK : E_OUTOFMEMORY;
    }

private:
    std::vector<SrcEntry> m_entries;
    std::wstring          m_password;
    IExtractProgressSink* m_sink;
    std::wstring          m_currentName;
    LONG                  m_refCount = 1;
};

// ============================================================
// Compress
// ============================================================

HRESULT SevenZip::Compress(const std::vector<std::wstring>& srcPaths,
                            const wchar_t* outPath,
                            const wchar_t* format,
                            int level,
                            const wchar_t* method,
                            const wchar_t* password,
                            IExtractProgressSink* sink) {
    if (!IsLoaded()) return E_FAIL;

    // For stream formats (gz/bz2/xz) with multiple files or a single directory,
    // automatically wrap contents in a tar first, then apply the stream format.
    bool isStream = format && (wcscmp(format, L"gz")  == 0 ||
                                wcscmp(format, L"bz2") == 0 ||
                                wcscmp(format, L"xz")  == 0);
    if (isStream) {
        bool needsTar = srcPaths.size() > 1;
        if (!needsTar && srcPaths.size() == 1) {
            DWORD attrs = GetFileAttributesW(srcPaths[0].c_str());
            needsTar = (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY));
        }
        if (needsTar) {
            // Build a unique temp tar path
            wchar_t tempDir[MAX_PATH] = {};
            GetTempPathW(MAX_PATH, tempDir);
            std::wstring tempTar = std::wstring(tempDir) +
                                   L"aileex_" + std::to_wstring(GetTickCount64()) + L".tar";

            // Step 1: pack everything into a tar (internal; no progress reporting)
            HRESULT hr = Compress(srcPaths, tempTar.c_str(), L"tar", 0, nullptr, nullptr, nullptr);
            if (FAILED(hr)) { DeleteFileW(tempTar.c_str()); return hr; }

            // Step 2: compress the tar with the stream format.
            // Ensure the output path uses .tar.X extension.
            std::wstring finalOut(outPath);
            auto dot = finalOut.rfind(L'.');
            if (dot != std::wstring::npos) {
                std::wstring before = finalOut.substr(0, dot);
                bool alreadyTar = (before.size() >= 4 &&
                                   _wcsicmp(before.c_str() + before.size() - 4, L".tar") == 0);
                if (!alreadyTar)
                    finalOut = before + L".tar." + format;
            } else {
                finalOut += std::wstring(L".tar.") + format;
            }

            std::vector<std::wstring> tarList = { tempTar };
            hr = Compress(tarList, finalOut.c_str(), format, level, method, password, sink);
            DeleteFileW(tempTar.c_str());
            return hr;
        }
    }

    const GUID& clsid = FormatToOutGuid(format);
    IOutArchive* archive = nullptr;
    HRESULT hr = CreateOutArchive(clsid, &archive);
    if (FAILED(hr)) return hr;

    // Set compression properties
    ISetProperties* setProps = nullptr;
    if (SUCCEEDED(archive->QueryInterface(IID_ISetProperties,
                                          reinterpret_cast<void**>(&setProps))) && setProps) {
        std::vector<const wchar_t*> names;
        std::vector<PROPVARIANT>    vals;

        PROPVARIANT pvLevel; PropVariantInit(&pvLevel);
        pvLevel.vt = VT_UI4;
        pvLevel.ulVal = (level >= 0 && level <= 9) ? (ULONG)level : 5;
        names.push_back(L"x"); vals.push_back(pvLevel);

        if (method && method[0]) {
            PROPVARIANT pvMethod; PropVariantInit(&pvMethod);
            pvMethod.vt = VT_BSTR;
            pvMethod.bstrVal = SysAllocString(method);
            names.push_back(L"m"); vals.push_back(pvMethod);
        }

        setProps->SetProperties(names.data(), vals.data(), (UInt32)names.size());
        for (auto& v : vals) PropVariantClear(&v);
        setProps->Release();
    }

    // Enumerate source entries
    std::vector<SrcEntry> entries;
    EnumeratePaths(srcPaths, entries);

    // Open output file
    COutFileStream* outFile = new COutFileStream();
    if (!outFile->Create(outPath)) {
        hr = HRESULT_FROM_WIN32(GetLastError());
        outFile->Release();
        archive->Release();
        return hr;
    }

    CUpdateCallback* cb = new CUpdateCallback(std::move(entries), password, sink);
    hr = archive->UpdateItems(outFile, cb->Count(), cb);

    cb->Release();
    outFile->Release();
    archive->Release();
    return hr;
}
