#define DEFINE_7Z_GUIDS
#include "SevenZip.h"
#include "I18n.h"
#include "resource.h"
#include "7zip/IPassword.h"
#include <shlwapi.h>
#include <shlobj.h>     // SHCreateDirectoryExW
#include <ole2.h>       // PropVariantClear, PropVariantInit
#include <oleauto.h>    // SysAllocString, SysFreeString
#include <wctype.h>
#include <set>

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
// COpenVolumeCallback — Load split archives (.001/.002/...)
// When 7z.dll's Split handler requests a volume file via
// IArchiveOpenVolumeCallback::GetStream, open and return it.
// ============================================================
class COpenVolumeCallback : public IArchiveOpenCallback,
                            public IArchiveOpenVolumeCallback,
                            public ICryptoGetTextPassword {
public:
    COpenVolumeCallback(const wchar_t* firstVolPath, const wchar_t* pw) {
        if (pw) m_password = pw;
        // Separate directory and current leaf name (e.g., "archive.7z.001")
        const wchar_t* slash = wcsrchr(firstVolPath, L'\\');
        if (!slash) slash = wcsrchr(firstVolPath, L'/');
        if (slash) {
            m_dir.assign(firstVolPath, slash + 1);
            m_currentName = slash + 1;
        } else {
            m_dir = L"";
            m_currentName = firstVolPath;
        }
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (iid == IID_IUnknown || iid == IID_IArchiveOpenCallback)
            *ppv = static_cast<IArchiveOpenCallback*>(this);
        else if (iid == IID_IArchiveOpenVolumeCallback)
            *ppv = static_cast<IArchiveOpenVolumeCallback*>(this);
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

    // IArchiveOpenCallback
    HRESULT STDMETHODCALLTYPE SetTotal(const UInt64*, const UInt64*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetCompleted(const UInt64*, const UInt64*) override { return S_OK; }

    // IArchiveOpenVolumeCallback
    // 7z.dll queries the current volume path via kpidName,
    // then infers the next volume name (e.g., .001 → .002).
    HRESULT STDMETHODCALLTYPE GetProperty(PROPID propID, PROPVARIANT* value) override {
        PropVariantInit(value);
        if (propID == kpidName) {
            value->vt = VT_BSTR;
            value->bstrVal = SysAllocString(m_currentName.c_str());
            return value->bstrVal ? S_OK : E_OUTOFMEMORY;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetStream(const wchar_t* name, IInStream** inStream) override {
        if (inStream) *inStream = nullptr;
        if (!name) return E_INVALIDARG;
        std::wstring path = m_dir + name;
        CInFileStream* s = new CInFileStream();
        if (!s->Open(path.c_str())) {
            s->Release();
            // Return S_FALSE if file doesn't exist (7z.dll treats this as end-of-volumes signal)
            return S_FALSE;
        }
        m_currentName = name;
        *inStream = s;
        return S_OK;
    }

    // ICryptoGetTextPassword
    HRESULT STDMETHODCALLTYPE CryptoGetTextPassword(BSTR* password) override {
        *password = SysAllocString(m_password.c_str());
        return *password ? S_OK : E_OUTOFMEMORY;
    }

private:
    std::wstring m_dir;          // Directory path with trailing separator
    std::wstring m_currentName;  // Current volume leaf name (e.g., "archive.7z.001")
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
    
    // Store full path for codec enumeration caching
    m_loadedPath = nameBuf;
    
    // Enumerate codecs if available (only if DLL path changed)
    m_pfnGetNumMethods   = (Func_GetNumberOfMethods)GetProcAddress(m_hDll, "GetNumberOfMethods");
    m_pfnGetMethodProp   = (Func_GetMethodProperty)GetProcAddress(m_hDll, "GetMethodProperty");
    m_pfnGetNumFormats   = (Func_GetNumberOfFormats)GetProcAddress(m_hDll, "GetNumberOfFormats");
    m_pfnGetHandlerProp2 = (Func_GetHandlerProperty2)GetProcAddress(m_hDll, "GetHandlerProperty2");
    if (m_pfnGetNumMethods && m_pfnGetMethodProp) EnumerateCodecs();
    if (m_pfnGetNumFormats && m_pfnGetHandlerProp2) EnumerateFormats();
    return true;
}
std::wstring SevenZip::GetLoadedPath() const {
    if (!m_hDll) return {};
    wchar_t buf[MAX_PATH] = {};
    DWORD n = GetModuleFileNameW(m_hDll, buf, MAX_PATH);
    return n ? std::wstring(buf, n) : std::wstring();
}

void SevenZip::Unload() {
    if (m_hDll) { FreeLibrary(m_hDll); m_hDll = nullptr; }
    m_loadedPath.clear();
    m_pfnCreateObject    = nullptr;
    m_pfnGetNumMethods   = nullptr;
    m_pfnGetMethodProp   = nullptr;
    m_pfnGetNumFormats   = nullptr;
    m_pfnGetHandlerProp2 = nullptr;
    m_encoderNames.clear();
    m_extToClsid.clear();
    m_writableFormats.clear();
    m_pathFormatCache.clear();
    m_itemsCache.clear();
}

// ============================================================
// Archive item caching helpers
// ============================================================

UINT32 SevenZip::HashPassword(const wchar_t* password) {
    if (!password || !*password) return 0;
    UINT32 hash = 5381;
    for (const wchar_t* p = password; *p; p++) {
        hash = ((hash << 5) + hash) ^ (UINT32)*p;
    }
    return hash;
}

std::wstring SevenZip::BuildCacheKey(const wchar_t* path, const wchar_t* password, const GUID& fmt) {
    UINT32 pwdHash = HashPassword(password);
    // Format GUID hex: {XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}
    wchar_t guidHex[40];
    swprintf_s(guidHex, L"%08X%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X",
               fmt.Data1, fmt.Data2, fmt.Data3,
               fmt.Data4[0], fmt.Data4[1], fmt.Data4[2], fmt.Data4[3],
               fmt.Data4[4], fmt.Data4[5], fmt.Data4[6], fmt.Data4[7]);
    
    wchar_t key[2048];
    swprintf_s(key, L"%s|%u|%s", path, pwdHash, guidHex);
    return key;
}


// ============================================================
// Codec enumeration
// ============================================================

// NMethodPropID: kName=1, kEncoderIsAssigned=8
void SevenZip::EnumerateCodecs() {
    m_encoderNames.clear();
    UINT32 n = 0;
    if (FAILED(m_pfnGetNumMethods(&n))) return;
    for (UINT32 i = 0; i < n; i++) {
        PROPVARIANT pvAssigned;
        PropVariantInit(&pvAssigned);
        HRESULT hr = m_pfnGetMethodProp(i, 8, &pvAssigned);  // kEncoderIsAssigned
        bool hasEncoder = SUCCEEDED(hr) && pvAssigned.vt == VT_BOOL && pvAssigned.boolVal != VARIANT_FALSE;
        PropVariantClear(&pvAssigned);
        if (!hasEncoder) continue;

        PROPVARIANT pvName;
        PropVariantInit(&pvName);
        hr = m_pfnGetMethodProp(i, 1, &pvName);  // kName
        if (SUCCEEDED(hr) && pvName.vt == VT_BSTR && pvName.bstrVal) {
            std::wstring name = pvName.bstrVal;
            for (auto& c : name) c = (wchar_t)towlower((wchar_t)c);
            m_encoderNames.push_back(std::move(name));
        }
        PropVariantClear(&pvName);
    }
}

// ============================================================
// Format enumeration (GetNumberOfFormats / GetHandlerProperty2)
// ============================================================

// NHandlerPropID: kName=0, kClassID=1, kExtension=2, kUpdate=4
void SevenZip::EnumerateFormats() {
    m_extToClsid.clear();
    m_writableFormats.clear();

    UINT32 n = 0;
    if (FAILED(m_pfnGetNumFormats(&n))) return;

    for (UINT32 i = 0; i < n; i++) {
        // CLSID — 16 bytes as VT_BSTR (GUID stored as byte sequence)
        PROPVARIANT pvClsid; PropVariantInit(&pvClsid);
        HRESULT hr = m_pfnGetHandlerProp2(i, 1 /*kClassID*/, &pvClsid);
        if (FAILED(hr) || pvClsid.vt != VT_BSTR ||
            SysStringByteLen(pvClsid.bstrVal) < (UINT)sizeof(GUID)) {
            PropVariantClear(&pvClsid);
            continue;
        }
        GUID clsid;
        memcpy(&clsid, pvClsid.bstrVal, sizeof(GUID));
        PropVariantClear(&pvClsid);

        // Extensions (space-separated)
        PROPVARIANT pvExt; PropVariantInit(&pvExt);
        hr = m_pfnGetHandlerProp2(i, 2 /*kExtension*/, &pvExt);
        std::wstring primaryExt;
        if (SUCCEEDED(hr) && pvExt.vt == VT_BSTR && pvExt.bstrVal) {
            std::wstring exts = pvExt.bstrVal;
            size_t pos = 0;
            while (pos <= exts.size()) {
                size_t sp = exts.find(L' ', pos);
                if (sp == std::wstring::npos) sp = exts.size();
                if (sp > pos) {
                    std::wstring e = exts.substr(pos, sp - pos);
                    for (auto& c : e) c = (wchar_t)towlower(c);
                    m_extToClsid[e] = clsid;
                    if (primaryExt.empty()) primaryExt = e;
                }
                pos = sp + 1;
            }
        }
        PropVariantClear(&pvExt);

        // Format name
        PROPVARIANT pvName; PropVariantInit(&pvName);
        std::wstring name;
        hr = m_pfnGetHandlerProp2(i, 0 /*kName*/, &pvName);
        if (SUCCEEDED(hr) && pvName.vt == VT_BSTR && pvName.bstrVal)
            name = pvName.bstrVal;
        PropVariantClear(&pvName);

        // Write support capability
        PROPVARIANT pvUpdate; PropVariantInit(&pvUpdate);
        hr = m_pfnGetHandlerProp2(i, 4 /*kUpdate*/, &pvUpdate);
        bool canWrite = SUCCEEDED(hr) && pvUpdate.vt == VT_BOOL &&
                        pvUpdate.boolVal != VARIANT_FALSE;
        PropVariantClear(&pvUpdate);

        if (canWrite && !primaryExt.empty()) {
            WritableFormat wf;
            wf.ext   = primaryExt;
            wf.label = name + L" (." + primaryExt + L")";
            m_writableFormats.push_back(std::move(wf));
        }
    }
}

bool SevenZip::IsArchiveExt(const wchar_t* ext) const {
    if (!ext || !ext[0]) return false;
    std::wstring lower(ext);
    for (auto& c : lower) c = (wchar_t)towlower(c);
    // Prefer dynamic map if available
    if (!m_extToClsid.empty())
        return m_extToClsid.count(lower) > 0;
    // Fallback: static list
    static const wchar_t* kFallback[] = {
        L"7z", L"zip", L"rar", L"tar", L"gz", L"bz2", L"xz",
        L"cab", L"iso", L"jar", L"wim", L"lzma", L"lzh", L"arj",
        nullptr
    };
    for (int i = 0; kFallback[i]; ++i)
        if (lower == kFallback[i]) return true;
    return false;
}



std::wstring SevenZip::ExtOfPath(const wchar_t* path) {
    const wchar_t* dot = wcsrchr(path, L'.');
    if (!dot) return L"";
    std::wstring ext = dot + 1;
    for (auto& c : ext) c = (wchar_t)towlower(c);
    return ext;
}

GUID SevenZip::FormatToInGuid(const wchar_t* path) const {
    std::wstring ext = ExtOfPath(path);
    if (!m_extToClsid.empty()) {
        auto it = m_extToClsid.find(ext);
        return (it != m_extToClsid.end()) ? it->second : CLSID_Format_7z;
    }
    // Static fallback when dynamic enumeration is unavailable
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

GUID SevenZip::FormatToOutGuid(const wchar_t* format) const {
    if (!format) return CLSID_Format_7z;
    std::wstring f = format;
    for (auto& c : f) c = (wchar_t)towlower(c);
    if (!m_extToClsid.empty()) {
        auto it = m_extToClsid.find(f);
        return (it != m_extToClsid.end()) ? it->second : CLSID_Format_7z;
    }
    // Static fallback
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
// OpenArchiveWithFallback — unified RAR5→RAR4 fallback logic with caching
// ============================================================

HRESULT SevenZip::OpenArchiveWithFallback(const wchar_t* path, const GUID& primaryGuid,
                                          IInStream* fileSpec, const UInt64& maxCheck,
                                          IArchiveOpenCallback* openCb, IInArchive*& archive) {
    archive = nullptr;

    // Check cache: if this path was already opened before, use cached format
    auto cacheIt = m_pathFormatCache.find(path);
    GUID formatGuid = (cacheIt != m_pathFormatCache.end()) ? cacheIt->second : primaryGuid;

    // Try primary (or cached) format
    HRESULT hr = CreateInArchive(formatGuid, &archive);
    if (FAILED(hr) || !archive) return FAILED(hr) ? hr : E_FAIL;

    hr = archive->Open(fileSpec, &maxCheck, openCb);

    // S_FALSE means "not this format"; try fallback for RAR5→RAR4
    if ((FAILED(hr) || hr == S_FALSE) && IsEqualGUID(formatGuid, CLSID_Format_Rar5)) {
        archive->Release();
        archive = nullptr;

        hr = CreateInArchive(CLSID_Format_Rar, &archive);
        if (SUCCEEDED(hr) && archive) {
            fileSpec->Seek(0, 0, nullptr);  // rewind
            hr = archive->Open(fileSpec, &maxCheck, openCb);

            // Cache the detected format for future calls
            if (SUCCEEDED(hr) && archive) {
                m_pathFormatCache[path] = CLSID_Format_Rar;  // RAR4 successful
            }
        }
    }

    // If cache was used but format matched, no need to update
    if (cacheIt == m_pathFormatCache.end() && SUCCEEDED(hr) && archive) {
        m_pathFormatCache[path] = formatGuid;  // Cache the successful format
    }

    return (FAILED(hr) || hr == S_FALSE) ? (FAILED(hr) ? hr : E_FAIL) : S_OK;
}

// ============================================================
// OpenArchive — enumerate all entries into items vector
// ============================================================

HRESULT SevenZip::OpenArchive(const wchar_t* path, std::vector<ArchiveItem>& items,
                               const wchar_t* password,
                               std::wstring* effectivePath) {
    if (!IsLoaded()) return E_FAIL;
    items.clear();
    if (effectivePath) *effectivePath = path;

    // Open file stream
    CInFileStream* fileSpec = new CInFileStream();
    if (!fileSpec->Open(path)) {
        DWORD err = GetLastError();
        fileSpec->Release();
        return HRESULT_FROM_WIN32(err ? err : ERROR_OPEN_FAILED);
    }

    // If extension is all-digits (.001 etc.), treat as split archive volume 1.
    // Open via Split handler (using extension map) and pass IArchiveOpenVolumeCallback.
    bool isSplit = false;
    {
        std::wstring ext = ExtOfPath(path);
        if (!ext.empty()) {
            bool allDigits = true;
            for (auto c : ext) if (!iswdigit(c)) { allDigits = false; break; }
            if (allDigits) isSplit = true;
        }
    }

    // Try primary format (from extension); for RAR also try old format (with caching)
    IInArchive* archive = nullptr;
    GUID primaryGuid = FormatToInGuid(path);
    HRESULT hr = S_OK;
    
    const UInt64 maxCheck = 1ULL << 23;
    if (isSplit) {
        COpenVolumeCallback* volCb = new COpenVolumeCallback(path, password);
        hr = OpenArchiveWithFallback(path, primaryGuid, fileSpec, maxCheck, volCb, archive);
        volCb->Release();
    } else {
        COpenCallback* openCb = new COpenCallback(password);
        hr = OpenArchiveWithFallback(path, primaryGuid, fileSpec, maxCheck, openCb, archive);
        openCb->Release();
    }

    fileSpec->Release();

    if (FAILED(hr)) {
        if (archive) archive->Release();
        return hr;
    }

    // Build cache key: need actual format GUID after potential fallback
    // If path is in m_pathFormatCache, we had a RAR5→RAR4 fallback
    std::wstring cacheKey;
    GUID actualFormat = primaryGuid;
    {
        auto it = m_pathFormatCache.find(path);
        if (it != m_pathFormatCache.end()) {
            actualFormat = it->second;
        }
        cacheKey = BuildCacheKey(path, password, actualFormat);
    }

    // Try cache lookup before enumeration
    {
        auto cacheIt = m_itemsCache.find(cacheKey);
        if (cacheIt != m_itemsCache.end()) {
            items = cacheIt->second.items;
            archive->Release();
            return S_OK;
        }
    }

    // Enumerate items
    UInt32 count = 0;
    archive->GetNumberOfItems(&count);
    items.reserve(count);

    // Reusable PROPVARIANT buffer for batch GetProperty calls
    PROPVARIANT prop;

    for (UInt32 i = 0; i < count; ++i) {
        ArchiveItem it;
        it.index = i;

        // Path: read once, normalize immediately
        PropVariantInit(&prop);
        archive->GetProperty(i, kpidPath, &prop);
        if (prop.vt == VT_BSTR && prop.bstrVal) {
            it.path = prop.bstrVal;
            // Normalize: convert backslashes to forward slashes
            for (auto& c : it.path) if (c == L'\\') c = L'/';
            // Strip trailing slashes (normalized already)
            while (!it.path.empty() && it.path.back() == L'/') it.path.pop_back();
        }
        PropVariantClear(&prop);

        // IsDir
        PropVariantInit(&prop);
        archive->GetProperty(i, kpidIsDir, &prop);
        it.isDir = (prop.vt == VT_BOOL && prop.boolVal != VARIANT_FALSE);
        PropVariantClear(&prop);

        // Leaf name: compute from already-normalized path
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
        std::wstring outerExt = ExtOfPath(path);
        if ((outerExt == L"gz" || outerExt == L"bz2" || outerExt == L"xz") &&
            items.size() == 1 && !items[0].isDir)
        {
            // Determine inner name: prefer item path/name, but bz2/xz may
            // store no filename — fall back to stripping the outer extension
            const wchar_t* innerName = (!items[0].path.empty())
                ? items[0].path.c_str()
                : (!items[0].name.empty() ? items[0].name.c_str() : nullptr);
            bool likelyTar = false;
            if (innerName && ExtOfPath(innerName) == L"tar") {
                likelyTar = true;
            } else {
                // e.g. "aa.tar.bz2" → strip ".bz2" → "aa.tar" → ext "tar"
                std::wstring outerBase(path);
                auto lastDot = outerBase.rfind(L'.');
                if (lastDot != std::wstring::npos) {
                    if (ExtOfPath(outerBase.substr(0, lastDot).c_str()) == L"tar")
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
                bool keepTmp = false;
                if (outStream->Create(tmpTar)) {
                    CTarExtractCallback* cb = new CTarExtractCallback(outStream);
                    UInt32 zeroIdx = 0;
                    HRESULT hrEx = archive->Extract(&zeroIdx, 1, 0, cb);
                    cb->Release();
                    outStream->Release();
                    if (SUCCEEDED(hrEx)) {
                        std::vector<ArchiveItem> tarItems;
                        HRESULT hrTar = OpenArchive(tmpTar, tarItems, password);
                        if (SUCCEEDED(hrTar)) {
                            items = std::move(tarItems);
                            // Keep temp .tar so Extract() can later address items by index.
                            // Caller (MainWindow) cleans it up via effectivePath on close.
                            if (effectivePath) *effectivePath = tmpTar;
                            keepTmp = true;
                        }
                    }
                } else {
                    outStream->Release();
                }
                if (!keepTmp) DeleteFileW(tmpTar);
            } // if likelyTar
        }
    }

    // Auto-unwrap for split archives:
    // If opening .001 yields a single concatenated file, extract it to a temp file,
    // detect inner format from magic bytes, rename to correct extension, re-open,
    // and show inner entries directly. Return temp path via effectivePath for
    // subsequent Extract/Test operations.
    if (isSplit && items.size() == 1 && !items[0].isDir) {
        std::wstring innerName = !items[0].name.empty() ? items[0].name : items[0].path;
        if (innerName.empty()) innerName = L"archive";
        // Replace characters invalid in filenames
        for (auto& c : innerName)
            if (c == L'\\' || c == L'/' || c == L':' || c == L'*' || c == L'?') c = L'_';

        wchar_t tmpDir[MAX_PATH];
        GetTempPathW(MAX_PATH, tmpDir);
        wchar_t tmpInner[MAX_PATH];
        swprintf_s(tmpInner, L"%saileex_split_%llu_%s",
                   tmpDir, (unsigned long long)GetTickCount64(), innerName.c_str());

        CTempOutStream* outStream = new CTempOutStream();
        bool extractOk = false;
        if (outStream->Create(tmpInner)) {
            CTarExtractCallback* cb = new CTarExtractCallback(outStream);
            UInt32 zeroIdx = 0;
            HRESULT hrEx = archive->Extract(&zeroIdx, 1, 0, cb);
            cb->Release();
            outStream->Release();
            extractOk = SUCCEEDED(hrEx);
        } else {
            outStream->Release();
        }

        if (extractOk) {
            // Detect inner format via magic bytes and rename extension if needed.
            // OpenArchive dispatches via extension→CLSID map, so correct extension is
            // essential. Undetectable files are treated as unsupported (abort unwrap).
            const wchar_t* detectedExt = nullptr;
            BYTE magic[512] = {};
            DWORD readBytes = 0;
            HANDLE hMagic = CreateFileW(tmpInner, GENERIC_READ, FILE_SHARE_READ,
                                         nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hMagic != INVALID_HANDLE_VALUE) {
                ReadFile(hMagic, magic, sizeof(magic), &readBytes, nullptr);
                CloseHandle(hMagic);
            }
            if (readBytes >= 6 && memcmp(magic, "7z\xBC\xAF\x27\x1C", 6) == 0)
                detectedExt = L"7z";
            else if (readBytes >= 4 && memcmp(magic, "PK\x03\x04", 4) == 0)
                detectedExt = L"zip";
            else if (readBytes >= 4 && memcmp(magic, "Rar!", 4) == 0)
                detectedExt = L"rar";
            else if (readBytes >= 6 && memcmp(magic, "\xFD" "7zXZ\x00", 6) == 0)
                detectedExt = L"xz";
            else if (readBytes >= 3 && memcmp(magic, "BZh", 3) == 0)
                detectedExt = L"bz2";
            else if (readBytes >= 2 && memcmp(magic, "\x1F\x8B", 2) == 0)
                detectedExt = L"gz";
            else if (readBytes >= 4 && memcmp(magic, "MSCF", 4) == 0)
                detectedExt = L"cab";
            else if (readBytes >= 262 && memcmp(magic + 257, "ustar", 5) == 0)
                detectedExt = L"tar";

            std::wstring tmpFinal = tmpInner;
            if (detectedExt) {
                std::wstring curExt = ExtOfPath(tmpFinal.c_str());
                if (_wcsicmp(curExt.c_str(), detectedExt) != 0) {
                    std::wstring withExt = tmpFinal + L".";
                    withExt += detectedExt;
                    if (MoveFileW(tmpFinal.c_str(), withExt.c_str()))
                        tmpFinal = std::move(withExt);
                }

                std::vector<ArchiveItem> innerItems;
                HRESULT hrInner = OpenArchive(tmpFinal.c_str(), innerItems, password);
                if (SUCCEEDED(hrInner)) {
                    items = std::move(innerItems);
                    if (effectivePath) *effectivePath = tmpFinal;
                    archive->Release();
                    return S_OK;
                }
            }
            // Unwrap failed (magic undetected or inner OpenArchive failed) — clean up temp file
            DeleteFileW(tmpFinal.c_str());
        }
    }

    // Cache the enumerated items (after all potential tar/split unwrapping)
    {
        // Re-verify cache key in case tar/split operations changed things
        std::wstring cacheKey;
        GUID actualFormat = primaryGuid;
        {
            auto it = m_pathFormatCache.find(path);
            if (it != m_pathFormatCache.end()) {
                actualFormat = it->second;
            }
            cacheKey = BuildCacheKey(path, password, actualFormat);
        }
        
        // Add to cache with eviction if needed
        if (m_itemsCache.size() >= MAX_CACHE_ENTRIES) {
            // Find and erase oldest entry
            int minOrder = INT_MAX;
            auto oldestIt = m_itemsCache.end();
            for (auto it = m_itemsCache.begin(); it != m_itemsCache.end(); ++it) {
                if (it->second.order < minOrder) {
                    minOrder = it->second.order;
                    oldestIt = it;
                }
            }
            if (oldestIt != m_itemsCache.end()) {
                m_itemsCache.erase(oldestIt);
            }
        }
        
        m_itemsCache[cacheKey] = { items, ++m_cacheOrder };
    }

    archive->Release();
    return S_OK;
}

// ============================================================
// GetArchiveProperties — Retrieve archive-wide metadata
// ============================================================

namespace {

// Convert PROPVARIANT to human-readable string. Empty string = no value to display.
std::wstring PropVariantToReadable(const PROPVARIANT& p) {
    wchar_t buf[64];
    switch (p.vt) {
    case VT_EMPTY:
    case VT_NULL:
        return L"";
    case VT_BSTR:
        return p.bstrVal ? std::wstring(p.bstrVal) : L"";
    case VT_BOOL:
        return p.boolVal != VARIANT_FALSE ? L"+" : L"-";
    case VT_UI1:
        swprintf_s(buf, L"%u", (unsigned)p.bVal); return buf;
    case VT_UI2:
        swprintf_s(buf, L"%u", (unsigned)p.uiVal); return buf;
    case VT_UI4:
        swprintf_s(buf, L"%u", (unsigned)p.ulVal); return buf;
    case VT_UI8:
        swprintf_s(buf, L"%llu", (unsigned long long)p.uhVal.QuadPart); return buf;
    case VT_I1:
        swprintf_s(buf, L"%d", (int)p.cVal); return buf;
    case VT_I2:
        swprintf_s(buf, L"%d", (int)p.iVal); return buf;
    case VT_I4:
        swprintf_s(buf, L"%d", (int)p.lVal); return buf;
    case VT_I8:
        swprintf_s(buf, L"%lld", (long long)p.hVal.QuadPart); return buf;
    case VT_FILETIME: {
        FILETIME local = {};
        SYSTEMTIME st = {};
        FileTimeToLocalFileTime(&p.filetime, &local);
        FileTimeToSystemTime(&local, &st);
        swprintf_s(buf, L"%04d/%02d/%02d %02d:%02d:%02d",
                   st.wYear, st.wMonth, st.wDay,
                   st.wHour, st.wMinute, st.wSecond);
        return buf;
    }
    default:
        // Unsupported type — output type number only
        swprintf_s(buf, L"(VT=%u)", (unsigned)p.vt); return buf;
    }
}

// PROPID → label in current language. Fallback for BSTR names returned by GetArchivePropertyInfo.
// Returns 0 if IDS is unassigned (e.g., CRC).
UINT PropIdToLabelId(PROPID id) {
    switch (id) {
    case kpidPath:             return IDS_PROP_PATH;
    case kpidName:             return IDS_PROP_NAME;
    case kpidExtension:        return IDS_PROP_EXTENSION;
    case kpidIsDir:            return IDS_PROP_IS_DIR;
    case kpidSize:             return IDS_PROP_SIZE;
    case kpidPackSize:         return IDS_PROP_PACK_SIZE;
    case kpidAttrib:           return IDS_PROP_ATTRIB;
    case kpidCTime:            return IDS_PROP_CTIME;
    case kpidATime:            return IDS_PROP_ATIME;
    case kpidMTime:            return IDS_PROP_MTIME;
    case kpidSolid:            return IDS_PROP_SOLID;
    case kpidCommented:        return IDS_PROP_COMMENTED;
    case kpidEncrypted:        return IDS_PROP_ENCRYPTED;
    case kpidDictionarySize:   return IDS_PROP_DICT_SIZE;
    case kpidType:             return IDS_PROP_TYPE;
    case kpidMethod:           return IDS_PROP_METHOD;
    case kpidHostOS:           return IDS_PROP_HOST_OS;
    case kpidFileSystem:       return IDS_PROP_FILE_SYSTEM;
    case kpidUser:             return IDS_PROP_USER;
    case kpidGroup:            return IDS_PROP_GROUP;
    case kpidBlock:            return IDS_PROP_BLOCK;
    case kpidComment:          return IDS_PROP_COMMENT;
    case kpidNumSubDirs:       return IDS_PROP_NUM_SUBDIRS;
    case kpidNumSubFiles:      return IDS_PROP_NUM_SUBFILES;
    case kpidUnpackVer:        return IDS_PROP_UNPACK_VER;
    case kpidVolume:           return IDS_PROP_VOLUME;
    case kpidIsVolume:         return IDS_PROP_IS_VOLUME;
    case kpidNumBlocks:        return IDS_PROP_NUM_BLOCKS;
    case kpidNumVolumes:       return IDS_PROP_NUM_VOLUMES;
    case kpidPhySize:          return IDS_PROP_PHY_SIZE;
    case kpidHeadersSize:      return IDS_PROP_HEADERS_SIZE;
    case kpidChecksum:         return IDS_PROP_CHECKSUM;
    case kpidCharacts:         return IDS_PROP_CHARACTS;
    case kpidCreatorApp:       return IDS_PROP_CREATOR_APP;
    case kpidTotalSize:        return IDS_PROP_TOTAL_SIZE;
    case kpidFreeSpace:        return IDS_PROP_FREE_SPACE;
    case kpidClusterSize:      return IDS_PROP_CLUSTER_SIZE;
    case kpidVolumeName:       return IDS_PROP_VOLUME_NAME;
    case kpidLocalName:        return IDS_PROP_LOCAL_NAME;
    case kpidProvider:         return IDS_PROP_PROVIDER;
    case kpidErrorType:        return IDS_PROP_ERROR_TYPE;
    case kpidNumErrors:        return IDS_PROP_NUM_ERRORS;
    case kpidErrorFlags:       return IDS_PROP_ERROR_FLAGS;
    case kpidWarningFlags:     return IDS_PROP_WARNING_FLAGS;
    case kpidWarning:          return IDS_PROP_WARNING;
    case kpidNumStreams:       return IDS_PROP_NUM_STREAMS;
    case kpidCodePage:         return IDS_PROP_CODE_PAGE;
    case kpidEmbeddedStubSize: return IDS_PROP_EMBEDDED_STUB_SIZE;
    default:                   return 0;
    }
}

} // namespace

HRESULT SevenZip::GetArchiveComment(const wchar_t* path,
                                    const wchar_t* password,
                                    std::wstring& out) {
    out.clear();
    if (!IsLoaded()) return E_FAIL;

    CInFileStream* fileSpec = new CInFileStream();
    if (!fileSpec->Open(path)) {
        DWORD err = GetLastError();
        fileSpec->Release();
        return HRESULT_FROM_WIN32(err ? err : ERROR_OPEN_FAILED);
    }

    IInArchive* archive = nullptr;
    GUID primaryGuid = FormatToInGuid(path);
    HRESULT hr = CreateInArchive(primaryGuid, &archive);
    if (FAILED(hr) || !archive) {
        fileSpec->Release();
        return FAILED(hr) ? hr : E_FAIL;
    }

    const UInt64 maxCheck = 1ULL << 23;
    COpenCallback* openCb = new COpenCallback(password);
    hr = OpenArchiveWithFallback(path, primaryGuid, fileSpec, maxCheck, openCb, archive);
    openCb->Release();

    fileSpec->Release();

    if (FAILED(hr)) {
        if (archive) archive->Release();
        return hr;
    }

    PROPVARIANT prop;
    PropVariantInit(&prop);
    if (SUCCEEDED(archive->GetArchiveProperty(kpidComment, &prop))) {
        if (prop.vt == VT_BSTR && prop.bstrVal) out = prop.bstrVal;
    }
    PropVariantClear(&prop);

    archive->Close();
    archive->Release();
    return S_OK;
}

// Set ZIP archive comment by directly modifying EOCD record.
// 7z.dll's Out path doesn't handle ZIP archive comment, so follow ZIP spec (APPNOTE 4.3.16)
// to locate and modify EOCD from the end. Copy via temp file, rename only on success
// (protects original if modification fails).
HRESULT SevenZip::SetZipArchiveComment(const wchar_t* archivePath,
                                       const std::wstring& comment) {
    // wstring → OEM code page (CP_OEMCP).
    // ZIP archive-wide comment traditionally interpreted as OEM (MS-DOS convention);
    // 7z.dll's ZIP handler also converts OEM→wide via GetArchiveProperty(kpidComment).
    // UTF-8 would cause corruption on re-read, so use CP_OEMCP.
    std::string oem;
    if (!comment.empty()) {
        int len = WideCharToMultiByte(CP_OEMCP, 0,
                                      comment.c_str(), (int)comment.size(),
                                      nullptr, 0, nullptr, nullptr);
        if (len > 0) {
            oem.resize(len);
            WideCharToMultiByte(CP_OEMCP, 0,
                                comment.c_str(), (int)comment.size(),
                                oem.data(), len, nullptr, nullptr);
        }
    }
    if (oem.size() > 0xFFFF) return E_INVALIDARG; // ZIP spec: comment ≤ 65535 bytes

    // Copy original file to .~tmp
    std::wstring tempPath = std::wstring(archivePath) + L".~tmp";
    DeleteFileW(tempPath.c_str());
    if (!CopyFileW(archivePath, tempPath.c_str(), FALSE)) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    // Open .~tmp for reading and writing
    HANDLE hFile = CreateFileW(tempPath.c_str(),
                               GENERIC_READ | GENERIC_WRITE,
                               0, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        DeleteFileW(tempPath.c_str());
        return HRESULT_FROM_WIN32(err);
    }

    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(hFile, &fileSize)) {
        DWORD err = GetLastError();
        CloseHandle(hFile);
        DeleteFileW(tempPath.c_str());
        return HRESULT_FROM_WIN32(err);
    }

    // EOCD exists in last 22+0..65535 bytes. Allocate read area and search backwards.
    static const DWORD kEocdMin   = 22;
    static const DWORD kMaxComm   = 65535;
    static const DWORD kScanBytes = kEocdMin + kMaxComm;
    DWORD readSize = (fileSize.QuadPart >= (LONGLONG)kScanBytes)
                     ? kScanBytes : (DWORD)fileSize.QuadPart;
    if (readSize < kEocdMin) {
        CloseHandle(hFile);
        DeleteFileW(tempPath.c_str());
        return E_FAIL; // Invalid ZIP
    }

    LARGE_INTEGER scanStart;
    scanStart.QuadPart = fileSize.QuadPart - readSize;
    if (!SetFilePointerEx(hFile, scanStart, nullptr, FILE_BEGIN)) {
        DWORD err = GetLastError();
        CloseHandle(hFile);
        DeleteFileW(tempPath.c_str());
        return HRESULT_FROM_WIN32(err);
    }

    std::vector<BYTE> tail(readSize);
    DWORD got = 0;
    if (!ReadFile(hFile, tail.data(), readSize, &got, nullptr) || got != readSize) {
        DWORD err = GetLastError();
        CloseHandle(hFile);
        DeleteFileW(tempPath.c_str());
        return HRESULT_FROM_WIN32(err ? err : ERROR_READ_FAULT);
    }

    // Search backwards for EOCD signature 0x06054b50.
    // When found, verify "comment length field" matches actual trailing bytes.
    int eocdOffsetInTail = -1;
    for (int i = (int)readSize - (int)kEocdMin; i >= 0; --i) {
        if (tail[i]     == 0x50 && tail[i + 1] == 0x4B &&
            tail[i + 2] == 0x05 && tail[i + 3] == 0x06)
        {
            UINT16 commentLen = (UINT16)tail[i + 20] | ((UINT16)tail[i + 21] << 8);
            // EOCD starts at i; total EOCD (22 + commentLen bytes) should match end
            if ((DWORD)i + kEocdMin + commentLen == readSize) {
                eocdOffsetInTail = i;
                break;
            }
        }
    }
    if (eocdOffsetInTail < 0) {
        CloseHandle(hFile);
        DeleteFileW(tempPath.c_str());
        return E_FAIL;
    }

    // Absolute offset of EOCD
    LONGLONG eocdAbsOffset = scanStart.QuadPart + eocdOffsetInTail;

    // Overwrite comment length field (offset 20, 21)
    LARGE_INTEGER posLen;
    posLen.QuadPart = eocdAbsOffset + 20;
    if (!SetFilePointerEx(hFile, posLen, nullptr, FILE_BEGIN)) {
        DWORD err = GetLastError();
        CloseHandle(hFile);
        DeleteFileW(tempPath.c_str());
        return HRESULT_FROM_WIN32(err);
    }
    BYTE lenBytes[2] = { (BYTE)(oem.size() & 0xFF), (BYTE)((oem.size() >> 8) & 0xFF) };
    DWORD written = 0;
    if (!WriteFile(hFile, lenBytes, 2, &written, nullptr) || written != 2) {
        DWORD err = GetLastError();
        CloseHandle(hFile);
        DeleteFileW(tempPath.c_str());
        return HRESULT_FROM_WIN32(err ? err : ERROR_WRITE_FAULT);
    }

    // Write comment body (empty comment = write 0 bytes = no-op)
    if (!oem.empty()) {
        if (!WriteFile(hFile, oem.data(), (DWORD)oem.size(), &written, nullptr) ||
            written != (DWORD)oem.size()) {
            DWORD err = GetLastError();
            CloseHandle(hFile);
            DeleteFileW(tempPath.c_str());
            return HRESULT_FROM_WIN32(err ? err : ERROR_WRITE_FAULT);
        }
    }

    // Truncate file at end (if old comment was longer than new)
    if (!SetEndOfFile(hFile)) {
        DWORD err = GetLastError();
        CloseHandle(hFile);
        DeleteFileW(tempPath.c_str());
        return HRESULT_FROM_WIN32(err);
    }

    CloseHandle(hFile);

    // Replace temp file with original path
    if (!MoveFileExW(tempPath.c_str(), archivePath,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DWORD err = GetLastError();
        DeleteFileW(tempPath.c_str());
        return HRESULT_FROM_WIN32(err);
    }
    return S_OK;
}

HRESULT SevenZip::GetArchiveProperties(const wchar_t* path,
                                       const wchar_t* password,
                                       ArchiveProperties& out) {
    out = {};
    if (!IsLoaded()) return E_FAIL;

    CInFileStream* fileSpec = new CInFileStream();
    if (!fileSpec->Open(path)) {
        DWORD err = GetLastError();
        fileSpec->Release();
        return HRESULT_FROM_WIN32(err ? err : ERROR_OPEN_FAILED);
    }

    IInArchive* archive = nullptr;
    GUID primaryGuid = FormatToInGuid(path);
    HRESULT hr = CreateInArchive(primaryGuid, &archive);
    if (FAILED(hr) || !archive) {
        fileSpec->Release();
        return FAILED(hr) ? hr : E_FAIL;
    }

    const UInt64 maxCheck = 1ULL << 23;
    COpenCallback* openCb = new COpenCallback(password);
    hr = OpenArchiveWithFallback(path, primaryGuid, fileSpec, maxCheck, openCb, archive);
    openCb->Release();

    fileSpec->Release();

    if (FAILED(hr)) {
        if (archive) archive->Release();
        return hr;
    }

    // ---- Enumerate archive-level properties ----
    UInt32 numArcProps = 0;
    if (SUCCEEDED(archive->GetNumberOfArchiveProperties(&numArcProps))) {
        for (UInt32 i = 0; i < numArcProps; ++i) {
            BSTR    propName = nullptr;
            PROPID  pid      = 0;
            VARTYPE vt       = 0;
            if (FAILED(archive->GetArchivePropertyInfo(i, &propName, &pid, &vt))) {
                if (propName) SysFreeString(propName);
                continue;
            }

            PROPVARIANT prop;
            PropVariantInit(&prop);
            HRESULT hrGet = archive->GetArchiveProperty(pid, &prop);

            if (SUCCEEDED(hrGet)) {
                std::wstring value = PropVariantToReadable(prop);
                if (!value.empty()) {
                    // Label: prefer localized map; fallback to DLL's English name
                    UINT labelId = PropIdToLabelId(pid);
                    std::wstring label;
                    if (labelId != 0)      label = I18n::Tr(labelId);
                    else if (pid == kpidCRC) label = L"CRC";
                    else if (propName)     label = propName;
                    else { wchar_t b[16]; swprintf_s(b, L"#%u", (unsigned)pid); label = b; }

                    if (pid == kpidType) out.formatName = value;
                    out.rawProps.emplace_back(std::move(label), std::move(value));
                }
            }
            PropVariantClear(&prop);
            if (propName) SysFreeString(propName);
        }
    }

    // ---- Item aggregation ----
    // Folder count equals MainWindow::PopulateTree logic: union of explicit directory entries
    // and ancestor paths of all files. Some archives lack explicit entries, so counting
    // kpidIsDir==true alone would undercount.
    UInt32 count = 0;
    archive->GetNumberOfItems(&count);
    std::set<std::wstring> filePaths;
    std::set<std::wstring> folderSet;
    for (UInt32 i = 0; i < count; ++i) {
        PROPVARIANT prop;

        bool isDir = false;
        PropVariantInit(&prop);
        if (SUCCEEDED(archive->GetProperty(i, kpidIsDir, &prop)) && prop.vt == VT_BOOL)
            isDir = (prop.boolVal != VARIANT_FALSE);
        PropVariantClear(&prop);

        std::wstring p;
        PropVariantInit(&prop);
        if (SUCCEEDED(archive->GetProperty(i, kpidPath, &prop)) &&
            prop.vt == VT_BSTR && prop.bstrVal)
        {
            p = prop.bstrVal;
            for (auto& c : p) if (c == L'\\') c = L'/';
            while (!p.empty() && p.back() == L'/') p.pop_back();
        }
        PropVariantClear(&prop);

        if (isDir) {
            if (!p.empty()) folderSet.insert(p);
        } else {
            ++out.fileCount;
            filePaths.insert(p);
            // Register ancestor folders implicitly
            auto pos = p.rfind(L'/');
            while (pos != std::wstring::npos) {
                folderSet.insert(p.substr(0, pos));
                p = p.substr(0, pos);
                pos = p.rfind(L'/');
            }

            PropVariantInit(&prop);
            if (SUCCEEDED(archive->GetProperty(i, kpidSize, &prop)) && prop.vt == VT_UI8)
                out.totalSize += prop.uhVal.QuadPart;
            PropVariantClear(&prop);

            PropVariantInit(&prop);
            if (SUCCEEDED(archive->GetProperty(i, kpidPackSize, &prop)) && prop.vt == VT_UI8)
                out.packedTotal += prop.uhVal.QuadPart;
            PropVariantClear(&prop);
        }

        PropVariantInit(&prop);
        if (SUCCEEDED(archive->GetProperty(i, kpidEncrypted, &prop)) && prop.vt == VT_BOOL) {
            if (prop.boolVal != VARIANT_FALSE) out.hasEncrypted = true;
        }
        PropVariantClear(&prop);

        PropVariantInit(&prop);
        if (SUCCEEDED(archive->GetProperty(i, kpidMethod, &prop)) &&
            prop.vt == VT_BSTR && prop.bstrVal && prop.bstrVal[0])
        {
            std::wstring m = prop.bstrVal;
            // Check if method already seen (small list, linear search is fine)
            bool seen = false;
            for (auto& s : out.methods) if (_wcsicmp(s.c_str(), m.c_str()) == 0) { seen = true; break; }
            if (!seen) out.methods.push_back(std::move(m));
        }
        PropVariantClear(&prop);
    }

    // Rare case: same path appears as both file and folder entry.
    // File takes precedence (same convention as PopulateTree).
    for (auto it = folderSet.begin(); it != folderSet.end();) {
        if (filePaths.count(*it)) it = folderSet.erase(it);
        else                       ++it;
    }
    out.folderCount = (UINT32)folderSet.size();

    archive->Close();
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
// ConcatFiles — write prefix followed by body sequentially into dst.
// Used to concatenate an SFX module + .7z data.
// ============================================================
static HRESULT ConcatFiles(const wchar_t* prefix,
                            const wchar_t* body,
                            const wchar_t* dst) {
    HANDLE hOut = CreateFileW(dst, GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hOut == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());

    auto pump = [&](const wchar_t* src) -> HRESULT {
        HANDLE hIn = CreateFileW(src, GENERIC_READ, FILE_SHARE_READ, nullptr,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hIn == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());
        BYTE  buf[64 * 1024];
        DWORD r = 0;
        HRESULT hr = S_OK;
        while (ReadFile(hIn, buf, sizeof(buf), &r, nullptr) && r > 0) {
            DWORD w = 0;
            if (!WriteFile(hOut, buf, r, &w, nullptr) || w != r) {
                hr = HRESULT_FROM_WIN32(GetLastError());
                break;
            }
        }
        CloseHandle(hIn);
        return hr;
    };

    HRESULT hr = pump(prefix);
    if (SUCCEEDED(hr)) hr = pump(body);
    CloseHandle(hOut);
    if (FAILED(hr)) DeleteFileW(dst);
    return hr;
}

// ============================================================
// CMultiVolOutStream — IOutStream wrapper that splits across multiple files
// 7z.dll writes to a single stream as-is, but internally switches to the next
// volume file (archive.7z.001, .002, ...) when crossing fixed-size boundaries.
// Since 7z.dll seeks near the start to write headers, past volumes need writes too
// (maintain HANDLE for each volume and Seek on switch).
// ============================================================
class CMultiVolOutStream : public IOutStream {
public:
    // basePath is "archive.7z.~tmp" etc. (includes extension).
    // Volumes created as "{basePath}.001", "{basePath}.002", ...
    bool Init(const std::wstring& basePath, UInt64 volumeSize) {
        m_basePath  = basePath;
        m_volSize   = volumeSize;
        // Create parent directory
        auto slash = m_basePath.find_last_of(L"\\/");
        if (slash != std::wstring::npos) {
            std::wstring dir = m_basePath.substr(0, slash);
            SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
        }
        return EnsureVolume(0);
    }

    ~CMultiVolOutStream() {
        for (auto h : m_files) if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }

    // Rollback on failure. Delete all volume files.
    void DeleteAll() {
        for (size_t i = 0; i < m_files.size(); ++i) {
            if (m_files[i] != INVALID_HANDLE_VALUE) {
                CloseHandle(m_files[i]);
                m_files[i] = INVALID_HANDLE_VALUE;
            }
            DeleteFileW(VolumePath(i).c_str());
        }
        m_files.clear();
        m_fileSizes.clear();
    }

    // On success, replace basePath with outPath (.~tmp.001 → .001 etc.).
    // origBase: source basePath; newBase: target basePath.
    HRESULT FinalizeRename(const std::wstring& origBase, const std::wstring& newBase) {
        // Close all handles
        for (auto& h : m_files) {
            if (h != INVALID_HANDLE_VALUE) { CloseHandle(h); h = INVALID_HANDLE_VALUE; }
        }
        for (size_t i = 0; i < m_files.size(); ++i) {
            std::wstring src = VolumePathFor(origBase, i);
            std::wstring dst = VolumePathFor(newBase, i);
            if (!MoveFileExW(src.c_str(), dst.c_str(),
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                return HRESULT_FROM_WIN32(GetLastError());
            }
        }
        return S_OK;
    }

    // Finalize with SFX. Prepend sfxPath content to volume 1;
    // rename volumes 2+ normally.
    HRESULT FinalizeWithSfx(const std::wstring& origBase,
                             const std::wstring& newBase,
                             const wchar_t*      sfxPath) {
        for (auto& h : m_files) {
            if (h != INVALID_HANDLE_VALUE) { CloseHandle(h); h = INVALID_HANDLE_VALUE; }
        }
        for (size_t i = 0; i < m_files.size(); ++i) {
            std::wstring src = VolumePathFor(origBase, i);
            std::wstring dst = VolumePathFor(newBase, i);
            if (i == 0) {
                HRESULT hr = ConcatFiles(sfxPath, src.c_str(), dst.c_str());
                if (FAILED(hr)) return hr;
                DeleteFileW(src.c_str());
            } else {
                if (!MoveFileExW(src.c_str(), dst.c_str(),
                                 MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                    return HRESULT_FROM_WIN32(GetLastError());
                }
            }
        }
        return S_OK;
    }

    size_t VolumeCount() const { return m_files.size(); }

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
        const BYTE* p = (const BYTE*)data;
        UInt32 totalWritten = 0;

        while (size > 0) {
            size_t volIdx     = (size_t)(m_curPos / m_volSize);
            UInt64 offsetInVol = m_curPos % m_volSize;
            UInt64 spaceLeft  = m_volSize - offsetInVol;
            UInt32 chunk      = (UInt32)((spaceLeft < (UInt64)size) ? spaceLeft : (UInt64)size);

            if (!EnsureVolume(volIdx)) return HRESULT_FROM_WIN32(GetLastError());

            LARGE_INTEGER li;
            li.QuadPart = (LONGLONG)offsetInVol;
            if (!SetFilePointerEx(m_files[volIdx], li, nullptr, FILE_BEGIN))
                return HRESULT_FROM_WIN32(GetLastError());

            DWORD written = 0;
            if (!WriteFile(m_files[volIdx], p, chunk, &written, nullptr))
                return HRESULT_FROM_WIN32(GetLastError());

            UInt64 newSizeInVol = offsetInVol + written;
            if (newSizeInVol > m_fileSizes[volIdx])
                m_fileSizes[volIdx] = newSizeInVol;

            p           += written;
            size        -= written;
            totalWritten+= written;
            m_curPos    += written;
            if (m_curPos > m_totalSize) m_totalSize = m_curPos;

            if ((UInt32)written < chunk) break;  // Partial write (disk full, etc.)
        }
        if (processedSize) *processedSize = totalWritten;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Seek(Int64 offset, UInt32 seekOrigin, UInt64* newPosition) override {
        Int64 base = 0;
        switch (seekOrigin) {
            case 0: base = 0;                  break;  // FILE_BEGIN
            case 1: base = (Int64)m_curPos;    break;  // FILE_CURRENT
            case 2: base = (Int64)m_totalSize; break;  // FILE_END
            default: return E_INVALIDARG;
        }
        Int64 newP = base + offset;
        if (newP < 0) return E_INVALIDARG;
        m_curPos = (UInt64)newP;
        if (newPosition) *newPosition = m_curPos;
        return S_OK;
    }

    // 7z.dll calls SetSize with final archive size. Truncate boundary volume,
    // delete unnecessary volumes after it.
    HRESULT STDMETHODCALLTYPE SetSize(UInt64 newSize) override {
        size_t boundaryVol;
        UInt64 boundaryOff;
        if (newSize == 0) {
            boundaryVol = 0;
            boundaryOff = 0;
        } else {
            boundaryVol = (size_t)((newSize - 1) / m_volSize);
            boundaryOff = ((newSize - 1) % m_volSize) + 1;
        }

        if (!EnsureVolume(boundaryVol)) return HRESULT_FROM_WIN32(GetLastError());

        // Truncate boundary volume
        LARGE_INTEGER li; li.QuadPart = (LONGLONG)boundaryOff;
        if (!SetFilePointerEx(m_files[boundaryVol], li, nullptr, FILE_BEGIN))
            return HRESULT_FROM_WIN32(GetLastError());
        if (!SetEndOfFile(m_files[boundaryVol]))
            return HRESULT_FROM_WIN32(GetLastError());
        m_fileSizes[boundaryVol] = boundaryOff;

        // Close and delete subsequent volumes
        for (size_t i = boundaryVol + 1; i < m_files.size(); ++i) {
            if (m_files[i] != INVALID_HANDLE_VALUE) {
                CloseHandle(m_files[i]);
                m_files[i] = INVALID_HANDLE_VALUE;
            }
            DeleteFileW(VolumePath(i).c_str());
        }
        m_files.resize(boundaryVol + 1);
        m_fileSizes.resize(boundaryVol + 1);
        m_totalSize = newSize;
        return S_OK;
    }

private:
    std::wstring VolumePath(size_t idx) const {
        return VolumePathFor(m_basePath, idx);
    }
    static std::wstring VolumePathFor(const std::wstring& base, size_t idx) {
        wchar_t suffix[16];
        swprintf_s(suffix, L".%03zu", idx + 1);
        return base + suffix;
    }
    bool EnsureVolume(size_t idx) {
        while (m_files.size() <= idx) {
            HANDLE h = CreateFileW(VolumePath(m_files.size()).c_str(),
                                    GENERIC_READ | GENERIC_WRITE,
                                    FILE_SHARE_READ, nullptr,
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h == INVALID_HANDLE_VALUE) return false;
            m_files.push_back(h);
            m_fileSizes.push_back(0);
        }
        return true;
    }

    std::wstring         m_basePath;          // Base path including extension (e.g. "archive.7z.~tmp")
    UInt64               m_volSize  = 0;
    std::vector<HANDLE>  m_files;
    std::vector<UInt64>  m_fileSizes;
    UInt64               m_curPos   = 0;
    UInt64               m_totalSize = 0;
    LONG                 m_refCount = 1;
};

// Convert a string ("100m", "1g", etc.) to byte count. Returns 0 for empty or invalid values.
static UInt64 ParseVolumeSize(const std::wstring& s) {
    if (s.empty()) return 0;
    UInt64 num = 0;
    size_t i = 0;
    while (i < s.size() && iswdigit(s[i])) {
        num = num * 10 + (s[i] - L'0');
        ++i;
    }
    if (num == 0) return 0;
    UInt64 mult = 1;
    if (i < s.size()) {
        wchar_t u = (wchar_t)towlower(s[i]);
        switch (u) {
            case L'b': mult = 1; break;
            case L'k': mult = 1024ULL; break;
            case L'm': mult = 1024ULL * 1024; break;
            case L'g': mult = 1024ULL * 1024 * 1024; break;
            default: return 0;
        }
    }
    return num * mult;
}

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
        // Reset any stream left over from the previous call (guards against m_currentOut
        // remaining set when SetOperationResult was not called due to a skip or directory)
        if (m_currentOut) { m_currentOut->Release(); m_currentOut = nullptr; }
        m_currentIsDir     = false;
        m_currentItemIndex = -1;
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
// CTestCallback — callback for IInArchive::Extract(testMode=1).
// No output stream needed. Aggregates results via SetOperationResult.
// ============================================================
class CTestCallback : public IArchiveExtractCallback, public ICryptoGetTextPassword {
public:
    CTestCallback(IInArchive* archive, const wchar_t* password, IExtractProgressSink* sink)
        : m_archive(archive), m_sink(sink) {
        if (password) m_password = password;
        archive->AddRef();
    }
    ~CTestCallback() { m_archive->Release(); }

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

    HRESULT STDMETHODCALLTYPE SetTotal(UInt64 total) override {
        if (m_sink) m_sink->OnSetTotal(total);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetCompleted(const UInt64* done) override {
        if (m_sink && m_sink->IsCancelled()) return E_ABORT;
        if (m_sink && done) m_sink->OnProgress(*done, m_currentFile.c_str());
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetStream(UInt32 index, ISequentialOutStream** outStream,
                                        Int32 askExtractMode) override {
        // No output stream needed in testMode. Ignore anything other than kTest.
        *outStream = nullptr;
        if (askExtractMode != NArchive::NExtract::NAskMode::kTest) return S_OK;

        PROPVARIANT prop; PropVariantInit(&prop);
        m_archive->GetProperty(index, kpidPath, &prop);
        m_currentFile = (prop.vt == VT_BSTR && prop.bstrVal) ? prop.bstrVal : L"";
        PropVariantClear(&prop);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE PrepareOperation(Int32) override { return S_OK; }

    HRESULT STDMETHODCALLTYPE SetOperationResult(Int32 opRes) override {
        if (opRes != NArchive::NExtract::NOperationResult::kOK) ++m_failures;
        if (opRes == NArchive::NExtract::NOperationResult::kWrongPassword)
            return E_ACCESSDENIED;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE CryptoGetTextPassword(BSTR* pw) override {
        *pw = SysAllocString(m_password.c_str());
        return *pw ? S_OK : E_OUTOFMEMORY;
    }

    int Failures() const { return m_failures; }

private:
    IInArchive*           m_archive;
    std::wstring          m_password;
    IExtractProgressSink* m_sink;
    std::wstring          m_currentFile;
    int                   m_failures = 0;
    LONG                  m_refCount = 1;
};

// ============================================================
// Test — verify all entries via IInArchive::Extract(testMode=1)
// ============================================================

HRESULT SevenZip::Test(const wchar_t* archivePath,
                       const wchar_t* password,
                       IExtractProgressSink* sink) {
    if (!IsLoaded()) return E_FAIL;

    CInFileStream* fileSpec = new CInFileStream();
    if (!fileSpec->Open(archivePath)) {
        fileSpec->Release();
        return HRESULT_FROM_WIN32(GetLastError());
    }

    GUID clsid = FormatToInGuid(archivePath);
    IInArchive* archive = nullptr;
    HRESULT hr = CreateInArchive(clsid, &archive);
    if (FAILED(hr)) { fileSpec->Release(); return hr; }

    // Split archive detection (all-digit extension → pass a volume callback to the Split handler)
    bool isSplit = false;
    {
        std::wstring ext = ExtOfPath(archivePath);
        if (!ext.empty()) {
            isSplit = true;
            for (auto c : ext) if (!iswdigit(c)) { isSplit = false; break; }
        }
    }

    const UInt64 maxCheck = 1ULL << 23;
    if (isSplit) {
        COpenVolumeCallback* volCb = new COpenVolumeCallback(archivePath, password);
        hr = OpenArchiveWithFallback(archivePath, clsid, fileSpec, maxCheck, volCb, archive);
        volCb->Release();
    } else {
        COpenCallback* openCb = new COpenCallback(password);
        hr = OpenArchiveWithFallback(archivePath, clsid, fileSpec, maxCheck, openCb, archive);
        openCb->Release();
    }

    fileSpec->Release();
    if (FAILED(hr)) {
        if (archive) archive->Release();
        return hr;
    }

    CTestCallback* cb = new CTestCallback(archive, password, sink);
    hr = archive->Extract(nullptr, (UInt32)-1, /*testMode=*/1, cb);
    int failures = cb->Failures();
    cb->Release();
    archive->Release();

    if (FAILED(hr)) return hr;
    return failures > 0 ? E_FAIL : S_OK;
}

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

    GUID clsid = FormatToInGuid(archivePath);
    IInArchive*  archive = nullptr;
    HRESULT hr = CreateInArchive(clsid, &archive);
    if (FAILED(hr)) { fileSpec->Release(); return hr; }

    // Split archive detection (same logic as Test)
    bool isSplit = false;
    {
        std::wstring ext = ExtOfPath(archivePath);
        if (!ext.empty()) {
            isSplit = true;
            for (auto c : ext) if (!iswdigit(c)) { isSplit = false; break; }
        }
    }

    const UInt64 maxCheck = 1ULL << 23;
    if (isSplit) {
        COpenVolumeCallback* volCb = new COpenVolumeCallback(archivePath, password);
        hr = OpenArchiveWithFallback(archivePath, clsid, fileSpec, maxCheck, volCb, archive);
        volCb->Release();
    } else {
        COpenCallback* openCb = new COpenCallback(password);
        hr = OpenArchiveWithFallback(archivePath, clsid, fileSpec, maxCheck, openCb, archive);
        openCb->Release();
    }

    fileSpec->Release();
    if (FAILED(hr)) {
        if (archive) archive->Release();
        return hr;
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
                            IExtractProgressSink* sink,
                            const CompressAdvanced* adv,
                            bool encryptHeaders) {
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

    GUID clsid = FormatToOutGuid(format);
    IOutArchive* archive = nullptr;
    HRESULT hr = CreateOutArchive(clsid, &archive);
    if (FAILED(hr)) return hr;

    // Set compression properties
    ISetProperties* setProps = nullptr;
    if (SUCCEEDED(archive->QueryInterface(IID_ISetProperties,
                                          reinterpret_cast<void**>(&setProps))) && setProps) {
        std::vector<const wchar_t*> names;
        std::vector<PROPVARIANT>    vals;
        std::vector<std::wstring>   extraKeyStore; // stable storage for extra key strings

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

        if (adv) {
            auto pushBstr = [&](const wchar_t* key, const std::wstring& val) {
                if (val.empty()) return;
                PROPVARIANT pv; PropVariantInit(&pv);
                pv.vt = VT_BSTR;
                pv.bstrVal = SysAllocString(val.c_str());
                names.push_back(key);
                vals.push_back(pv);
            };
            pushBstr(L"d",  adv->dictSize);    // dictionary size
            pushBstr(L"fb", adv->wordSize);    // fast bytes (word size)
            pushBstr(L"ms", adv->solidBlock);  // solid block size
            pushBstr(L"mt", adv->threads);     // thread count

            // Parse and apply additional "key=value" pairs separated by spaces
            if (!adv->extra.empty()) {
                std::vector<std::pair<std::wstring, std::wstring>> extraPairs;
                const std::wstring& s = adv->extra;
                size_t pos = 0;
                while (pos < s.size()) {
                    while (pos < s.size() && iswspace(s[pos])) ++pos;
                    if (pos >= s.size()) break;
                    size_t start2 = pos;
                    while (pos < s.size() && !iswspace(s[pos])) ++pos;
                    std::wstring token = s.substr(start2, pos - start2);
                    auto eq = token.find(L'=');
                    if (eq != std::wstring::npos)
                        extraPairs.emplace_back(token.substr(0, eq), token.substr(eq + 1));
                }
                extraKeyStore.reserve(extraPairs.size());
                for (auto& kv : extraPairs) extraKeyStore.push_back(kv.first);
                for (size_t i = 0; i < extraPairs.size(); i++) {
                    names.push_back(extraKeyStore[i].c_str());
                    PROPVARIANT pv; PropVariantInit(&pv);
                    pv.vt = VT_BSTR;
                    pv.bstrVal = SysAllocString(extraPairs[i].second.c_str());
                    vals.push_back(pv);
                }
            }
        }

        // 7z header encryption ("Encrypt header" checkbox)
        // Only effective for 7z format with a password set.
        if (encryptHeaders && password && password[0] &&
            format && _wcsicmp(format, L"7z") == 0) {
            PROPVARIANT pvHe; PropVariantInit(&pvHe);
            pvHe.vt = VT_BSTR;
            pvHe.bstrVal = SysAllocString(L"on");
            names.push_back(L"he"); vals.push_back(pvHe);
        }

        setProps->SetProperties(names.data(), vals.data(), (UInt32)names.size());
        for (auto& v : vals) PropVariantClear(&v);
        setProps->Release();
    }

    // Enumerate source entries
    std::vector<SrcEntry> entries;
    EnumeratePaths(srcPaths, entries);

    // Use CMultiVolOutStream if a split volume is specified and the format is not stream-wrapped.
    // gz/bz2/xz are single-entry formats where splitting is complex, so they are not supported.
    UInt64 volBytes = (adv && !isStream) ? ParseVolumeSize(adv->volumeSize) : 0;

    // SFX (.exe) is only supported for 7z format; outPath is assumed to already have .exe extension (set by caller).
    bool isSfx = (adv && !adv->sfxModulePath.empty() &&
                  format && _wcsicmp(format, L"7z") == 0);

    // Write to a temp file first and rename on success (prevents corrupting the file on failure)
    std::wstring tempPath = std::wstring(outPath) + L".~tmp";

    if (volBytes > 0) {
        // Multi-volume output: generate .001 .002 ... using tempPath as the base
        CMultiVolOutStream* mvOut = new CMultiVolOutStream();
        if (!mvOut->Init(tempPath, volBytes)) {
            hr = HRESULT_FROM_WIN32(GetLastError());
            mvOut->Release();
            archive->Release();
            return hr;
        }

        CUpdateCallback* cb = new CUpdateCallback(std::move(entries), password, sink);
        hr = archive->UpdateItems(mvOut, cb->Count(), cb);
        size_t volCount = mvOut->VolumeCount();

        cb->Release();
        archive->Release();

        if (SUCCEEDED(hr)) {
            // Delete any existing volume with the same name before MoveFileExW (it would fail otherwise)
            for (size_t i = 0; i < volCount; ++i) {
                wchar_t suffix[16];
                swprintf_s(suffix, L".%03zu", i + 1);
                std::wstring dst = std::wstring(outPath) + suffix;
                DeleteFileW(dst.c_str());
            }
            if (isSfx) {
                hr = mvOut->FinalizeWithSfx(tempPath, outPath, adv->sfxModulePath.c_str());
            } else {
                hr = mvOut->FinalizeRename(tempPath, outPath);
            }
            if (FAILED(hr)) mvOut->DeleteAll();
        } else {
            mvOut->DeleteAll();
        }
        mvOut->Release();
        return hr;
    }

    // Single-file output (traditional path)
    COutFileStream* outFile = new COutFileStream();
    if (!outFile->Create(tempPath.c_str())) {
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

    if (SUCCEEDED(hr)) {
        if (isSfx) {
            // Concatenate SFX module + .7z data to create outPath
            hr = ConcatFiles(adv->sfxModulePath.c_str(), tempPath.c_str(), outPath);
            DeleteFileW(tempPath.c_str());
        } else {
            if (!MoveFileExW(tempPath.c_str(), outPath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                hr = HRESULT_FROM_WIN32(GetLastError());
        }
    } else {
        DeleteFileW(tempPath.c_str());
    }
    return hr;
}

// ============================================================
// CDeleteCallback — IArchiveUpdateCallback
// Enumerate only the entries to keep. For each newIdx,
// returning newData=0 / newProperties=0 / indexInArchive=oldIdx tells
// 7z.dll to copy the compressed blob directly from the original archive
// (no recompression, no password required).
// ============================================================
class CDeleteCallback : public IArchiveUpdateCallback, public ICryptoGetTextPassword2 {
public:
    CDeleteCallback(std::vector<UInt32> keepIndices, const wchar_t* password,
                    IExtractProgressSink* sink)
        : m_keepIndices(std::move(keepIndices)), m_sink(sink) {
        if (password) m_password = password;
    }

    UInt32 Count() const { return (UInt32)m_keepIndices.size(); }

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

    HRESULT STDMETHODCALLTYPE SetTotal(UInt64 total) override {
        if (m_sink) m_sink->OnSetTotal(total);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetCompleted(const UInt64* done) override {
        if (m_sink && m_sink->IsCancelled()) return E_ABORT;
        if (m_sink && done) m_sink->OnProgress(*done, nullptr);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetUpdateItemInfo(UInt32 newIdx,
                                                 Int32* newData,
                                                 Int32* newProperties,
                                                 UInt32* indexInArchive) override {
        if (newIdx >= m_keepIndices.size()) return E_INVALIDARG;
        if (newData)        *newData        = 0;
        if (newProperties)  *newProperties  = 0;
        if (indexInArchive) *indexInArchive = m_keepIndices[newIdx];
        return S_OK;
    }

    // Should not be called with newProperties=0, but return empty just in case
    HRESULT STDMETHODCALLTYPE GetProperty(UInt32, PROPID, PROPVARIANT* value) override {
        PropVariantInit(value);
        return S_OK;
    }

    // Not called with newData=0 (copy is handled internally by 7z)
    HRESULT STDMETHODCALLTYPE GetStream(UInt32, ISequentialInStream** inStream) override {
        if (inStream) *inStream = nullptr;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetOperationResult(Int32) override { return S_OK; }

    HRESULT STDMETHODCALLTYPE CryptoGetTextPassword2(Int32* passwordIsDefined,
                                                      BSTR* password) override {
        bool hasPw = !m_password.empty();
        if (passwordIsDefined) *passwordIsDefined = hasPw ? 1 : 0;
        *password = SysAllocString(m_password.c_str());
        return *password ? S_OK : E_OUTOFMEMORY;
    }

private:
    std::vector<UInt32>    m_keepIndices;
    std::wstring           m_password;
    IExtractProgressSink*  m_sink;
    LONG                   m_refCount = 1;
};

// ============================================================
// DeleteItems
// ============================================================
HRESULT SevenZip::DeleteItems(const wchar_t* archivePath,
                               const std::vector<UInt32>& deleteIndices,
                               const wchar_t* password,
                               IExtractProgressSink* sink) {
    if (!IsLoaded()) return E_FAIL;
    if (deleteIndices.empty()) return S_OK;

    GUID clsid = FormatToInGuid(archivePath);
    IInArchive* inArc = nullptr;
    HRESULT hr = CreateInArchive(clsid, &inArc);
    if (FAILED(hr) || !inArc) return FAILED(hr) ? hr : E_FAIL;

    CInFileStream* inFile = new CInFileStream();
    if (!inFile->Open(archivePath)) {
        DWORD err = GetLastError();
        inFile->Release();
        inArc->Release();
        return HRESULT_FROM_WIN32(err ? err : ERROR_OPEN_FAILED);
    }

    COpenCallback* openCb = new COpenCallback(password);
    const UInt64 maxCheck = 1ULL << 23;
    hr = OpenArchiveWithFallback(archivePath, clsid, inFile, maxCheck, openCb, inArc);
    openCb->Release();
    inFile->Release();

    if (FAILED(hr)) {
        inArc->Release();
        return hr;
    }

    UInt32 totalItems = 0;
    inArc->GetNumberOfItems(&totalItems);

    // Build the sorted list of indices to keep. Duplicates are absorbed by std::set.
    std::vector<UInt32> keep;
    keep.reserve(totalItems);
    {
        std::vector<bool> drop(totalItems, false);
        for (UInt32 i : deleteIndices) {
            if (i < totalItems) drop[i] = true;
        }
        for (UInt32 i = 0; i < totalItems; ++i)
            if (!drop[i]) keep.push_back(i);
    }

    // Get IOutArchive. Returns E_NOINTERFACE for write-unsupported formats.
    IOutArchive* outArc = nullptr;
    hr = inArc->QueryInterface(IID_IOutArchive, reinterpret_cast<void**>(&outArc));
    if (FAILED(hr) || !outArc) {
        inArc->Release();
        return FAILED(hr) ? hr : E_NOINTERFACE;
    }

    // Write to a temp file and rename on success (prevents corrupting the original on failure)
    std::wstring tempPath = std::wstring(archivePath) + L".~tmp";
    COutFileStream* outFile = new COutFileStream();
    if (!outFile->Create(tempPath.c_str())) {
        hr = HRESULT_FROM_WIN32(GetLastError());
        outFile->Release();
        outArc->Release();
        inArc->Release();
        return hr;
    }

    CDeleteCallback* cb = new CDeleteCallback(std::move(keep), password, sink);
    UInt32 keepCount = cb->Count();
    hr = outArc->UpdateItems(outFile, keepCount, cb);

    cb->Release();
    outFile->Release();
    outArc->Release();
    // MoveFileExW fails if the input handle is still open; release it first
    inArc->Release();

    if (SUCCEEDED(hr)) {
        if (!MoveFileExW(tempPath.c_str(), archivePath,
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            hr = HRESULT_FROM_WIN32(GetLastError());
    } else {
        DeleteFileW(tempPath.c_str());
    }
    return hr;
}

// ============================================================
// CAddCallback — IArchiveUpdateCallback (copy existing + add new).
// Combined version of CDeleteCallback and CUpdateCallback.
// newIdx < keep.size()  : copy existing entry (newData=0)
// newIdx >= keep.size() : read new file (newData=1)
// ============================================================
class CAddCallback : public IArchiveUpdateCallback, public ICryptoGetTextPassword2 {
public:
    CAddCallback(std::vector<UInt32> keepIndices,
                 std::vector<SrcEntry> newEntries,
                 const wchar_t* password,
                 IExtractProgressSink* sink)
        : m_keep(std::move(keepIndices))
        , m_new(std::move(newEntries))
        , m_sink(sink) {
        if (password) m_password = password;
    }

    UInt32 Count() const { return (UInt32)(m_keep.size() + m_new.size()); }

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

    HRESULT STDMETHODCALLTYPE GetUpdateItemInfo(UInt32 newIdx,
                                                Int32* newData,
                                                Int32* newProperties,
                                                UInt32* indexInArchive) override {
        if (newIdx < (UInt32)m_keep.size()) {
            if (newData)        *newData        = 0;
            if (newProperties)  *newProperties  = 0;
            if (indexInArchive) *indexInArchive = m_keep[newIdx];
        } else {
            if (newData)        *newData        = 1;
            if (newProperties)  *newProperties  = 1;
            if (indexInArchive) *indexInArchive = (UInt32)-1;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetProperty(UInt32 index, PROPID propID,
                                          PROPVARIANT* value) override {
        PropVariantInit(value);
        if (index < (UInt32)m_keep.size()) return S_OK; // copy path: should not be called
        UInt32 newIdx = index - (UInt32)m_keep.size();
        if (newIdx >= m_new.size()) return E_INVALIDARG;
        const auto& e = m_new[newIdx];
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
        if (inStream) *inStream = nullptr;
        if (index < (UInt32)m_keep.size()) return S_OK; // copy path: not called
        UInt32 newIdx = index - (UInt32)m_keep.size();
        if (newIdx >= m_new.size()) return E_INVALIDARG;
        const auto& e = m_new[newIdx];
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

    HRESULT STDMETHODCALLTYPE SetOperationResult(Int32) override { return S_OK; }

    HRESULT STDMETHODCALLTYPE CryptoGetTextPassword2(Int32* passwordIsDefined,
                                                     BSTR* password) override {
        bool hasPw = !m_password.empty();
        if (passwordIsDefined) *passwordIsDefined = hasPw ? 1 : 0;
        *password = SysAllocString(m_password.c_str());
        return *password ? S_OK : E_OUTOFMEMORY;
    }

private:
    std::vector<UInt32>    m_keep;
    std::vector<SrcEntry>  m_new;
    std::wstring           m_password;
    IExtractProgressSink*  m_sink;
    std::wstring           m_currentName;
    LONG                   m_refCount = 1;
};

// ============================================================
// AddToArchive
// ============================================================
HRESULT SevenZip::AddToArchive(const wchar_t* archivePath,
                               const std::vector<std::wstring>& srcPaths,
                               const wchar_t* archiveFolder,
                               const wchar_t* password,
                               int level, const wchar_t* method,
                               IExtractProgressSink* sink,
                               const CompressAdvanced* adv) {
    if (!IsLoaded()) return E_FAIL;
    if (srcPaths.empty()) return S_OK;

    // 1. Open the existing archive (password required)
    GUID clsid = FormatToInGuid(archivePath);
    IInArchive* inArc = nullptr;
    HRESULT hr = CreateInArchive(clsid, &inArc);
    if (FAILED(hr) || !inArc) return FAILED(hr) ? hr : E_FAIL;

    CInFileStream* inFile = new CInFileStream();
    if (!inFile->Open(archivePath)) {
        DWORD err = GetLastError();
        inFile->Release();
        inArc->Release();
        return HRESULT_FROM_WIN32(err ? err : ERROR_OPEN_FAILED);
    }

    COpenCallback* openCb = new COpenCallback(password);
    const UInt64 maxCheck = 1ULL << 23;
    hr = OpenArchiveWithFallback(archivePath, clsid, inFile, maxCheck, openCb, inArc);
    openCb->Release();
    inFile->Release();

    if (FAILED(hr)) {
        inArc->Release();
        return hr;
    }

    // 2. Get IOutArchive (E_NOINTERFACE for write-unsupported formats)
    IOutArchive* outArc = nullptr;
    hr = inArc->QueryInterface(IID_IOutArchive, reinterpret_cast<void**>(&outArc));
    if (FAILED(hr) || !outArc) {
        inArc->Release();
        return FAILED(hr) ? hr : E_NOINTERFACE;
    }

    // 3. Set compression properties (for new entries; existing entries are copied without recompression)
    ISetProperties* setProps = nullptr;
    if (SUCCEEDED(outArc->QueryInterface(IID_ISetProperties,
                                         reinterpret_cast<void**>(&setProps))) && setProps) {
        std::vector<const wchar_t*> names;
        std::vector<PROPVARIANT>    vals;
        std::vector<std::wstring>   extraKeyStore;

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

        if (adv) {
            auto pushBstr = [&](const wchar_t* key, const std::wstring& val) {
                if (val.empty()) return;
                PROPVARIANT pv; PropVariantInit(&pv);
                pv.vt = VT_BSTR;
                pv.bstrVal = SysAllocString(val.c_str());
                names.push_back(key);
                vals.push_back(pv);
            };
            pushBstr(L"d",  adv->dictSize);
            pushBstr(L"fb", adv->wordSize);
            pushBstr(L"ms", adv->solidBlock);
            pushBstr(L"mt", adv->threads);
        }

        setProps->SetProperties(names.data(), vals.data(), (UInt32)names.size());
        for (auto& v : vals) PropVariantClear(&v);
        setProps->Release();
    }

    // 4. Enumerate new entries
    std::vector<SrcEntry> newEntries;
    EnumeratePaths(srcPaths, newEntries);

    // 5. Prepend the archiveFolder prefix (use backslash as the separator)
    auto toBackslash = [](std::wstring s) {
        for (auto& c : s) if (c == L'/') c = L'\\';
        // Drop any trailing separator
        while (!s.empty() && s.back() == L'\\') s.pop_back();
        return s;
    };
    std::wstring folderPrefix;
    if (archiveFolder && archiveFolder[0]) {
        folderPrefix = toBackslash(archiveFolder);
        if (!folderPrefix.empty()) folderPrefix += L'\\';
    }
    if (!folderPrefix.empty()) {
        for (auto& e : newEntries) e.archivePath = folderPrefix + e.archivePath;
    }

    // 6. Drop conflicting paths from the existing side (new entries take priority — overwrite)
    std::set<std::wstring> newPathsLower;
    for (auto& e : newEntries) {
        std::wstring k = e.archivePath;
        for (auto& c : k) c = (wchar_t)towlower(c);
        newPathsLower.insert(std::move(k));
    }

    UInt32 totalItems = 0;
    inArc->GetNumberOfItems(&totalItems);
    std::vector<UInt32> keep;
    keep.reserve(totalItems);
    for (UInt32 i = 0; i < totalItems; ++i) {
        PROPVARIANT prop;
        PropVariantInit(&prop);
        std::wstring p;
        if (SUCCEEDED(inArc->GetProperty(i, kpidPath, &prop)) &&
            prop.vt == VT_BSTR && prop.bstrVal)
        {
            p = prop.bstrVal;
        }
        PropVariantClear(&prop);
        // 7z.dll may use either `/` or `\` depending on format; normalize before comparing
        for (auto& c : p) { if (c == L'/') c = L'\\'; c = (wchar_t)towlower(c); }
        while (!p.empty() && p.back() == L'\\') p.pop_back();
        if (!newPathsLower.count(p)) keep.push_back(i);
    }

    // 7. Write to a temp file and rename on success
    std::wstring tempPath = std::wstring(archivePath) + L".~tmp";
    COutFileStream* outFile = new COutFileStream();
    if (!outFile->Create(tempPath.c_str())) {
        hr = HRESULT_FROM_WIN32(GetLastError());
        outFile->Release();
        outArc->Release();
        inArc->Release();
        return hr;
    }

    CAddCallback* cb = new CAddCallback(std::move(keep), std::move(newEntries), password, sink);
    UInt32 totalCount = cb->Count();
    hr = outArc->UpdateItems(outFile, totalCount, cb);

    cb->Release();
    outFile->Release();
    outArc->Release();
    inArc->Release();

    if (SUCCEEDED(hr)) {
        if (!MoveFileExW(tempPath.c_str(), archivePath,
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            hr = HRESULT_FROM_WIN32(GetLastError());
    } else {
        DeleteFileW(tempPath.c_str());
    }
    return hr;
}
