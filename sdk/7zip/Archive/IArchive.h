#pragma once
#include "../IProgress.h"
#include "../IStream.h"
#include "../PropID.h"
#include <propidl.h>    // PROPVARIANT

// ---- Format GUIDs ----
// CLSID pattern: {23170F69-40C1-278A-1000-00011000XX00}  (XX = format byte)
#ifdef DEFINE_7Z_GUIDS
#  define Z7_FMT_GUID(name, fmtByte) \
     EXTERN_C const GUID name = {0x23170F69,0x40C1,0x278A,{0x10,0x00,0x00,0x01,0x10,(fmtByte),0x00,0x00}}
#else
#  define Z7_FMT_GUID(name, fmtByte) \
     EXTERN_C const GUID name
#endif

Z7_FMT_GUID(CLSID_Format_7z,    0x07);
Z7_FMT_GUID(CLSID_Format_Zip,   0x01);
Z7_FMT_GUID(CLSID_Format_BZip2, 0x02);
Z7_FMT_GUID(CLSID_Format_Xz,    0x0C);
Z7_FMT_GUID(CLSID_Format_Tar,   0xEE);
Z7_FMT_GUID(CLSID_Format_GZip,  0xEF);
Z7_FMT_GUID(CLSID_Format_Rar,   0x03);
Z7_FMT_GUID(CLSID_Format_Rar5,  0xCC);  // 7-Zip uses 0xCC, not 0x04
Z7_FMT_GUID(CLSID_Format_Cab,   0x08);
Z7_FMT_GUID(CLSID_Format_Iso,   0xE7);

// ---- CreateObject export ----
typedef HRESULT (WINAPI *Func_CreateObject)(const GUID* clsid, const GUID* iid, void** outObject);

// ---- IArchiveOpenCallback ----
struct IArchiveOpenCallback : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE SetTotal(const UInt64* files, const UInt64* bytes) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetCompleted(const UInt64* files, const UInt64* bytes) = 0;
};

// ---- IArchiveOpenVolumeCallback ----
// 分割アーカイブ（.001/.002/...）読み込み時に Split ハンドラから呼ばれる。
// kpidName で第1巻のパス、GetStream で要求された名前のボリュームを返す。
struct IArchiveOpenVolumeCallback : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetProperty(PROPID propID, PROPVARIANT* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetStream(const wchar_t* name, IInStream** inStream) = 0;
};

// ---- IArchiveExtractCallback ----
struct IArchiveExtractCallback : public IProgress {
    virtual HRESULT STDMETHODCALLTYPE GetStream(UInt32 index, ISequentialOutStream** outStream, Int32 askExtractMode) = 0;
    virtual HRESULT STDMETHODCALLTYPE PrepareOperation(Int32 askExtractMode) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetOperationResult(Int32 opRes) = 0;
};

// ---- IArchiveUpdateCallback ----
struct IArchiveUpdateCallback : public IProgress {
    virtual HRESULT STDMETHODCALLTYPE GetUpdateItemInfo(UInt32 index, Int32* newData, Int32* newProperties, UInt32* indexInArchive) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProperty(UInt32 index, PROPID propID, PROPVARIANT* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetStream(UInt32 index, ISequentialInStream** inStream) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetOperationResult(Int32 operationResult) = 0;
};

// ---- ISetProperties ----
struct ISetProperties : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE SetProperties(const wchar_t* const* names, const PROPVARIANT* values, UInt32 numProps) = 0;
};

// ---- IInArchive ----
struct IInArchive : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Open(IInStream* stream, const UInt64* maxCheckStartPosition, IArchiveOpenCallback* openCallback) = 0;
    virtual HRESULT STDMETHODCALLTYPE Close() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetNumberOfItems(UInt32* numItems) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProperty(UInt32 index, PROPID propID, PROPVARIANT* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE Extract(const UInt32* indices, UInt32 numItems, Int32 testMode, IArchiveExtractCallback* extractCallback) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetArchiveProperty(PROPID propID, PROPVARIANT* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetNumberOfProperties(UInt32* numProps) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyInfo(UInt32 index, BSTR* name, PROPID* propID, VARTYPE* varType) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetNumberOfArchiveProperties(UInt32* numProps) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetArchivePropertyInfo(UInt32 index, BSTR* name, PROPID* propID, VARTYPE* varType) = 0;
};

// ---- IOutArchive ----
struct IOutArchive : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE UpdateItems(ISequentialOutStream* outStream, UInt32 numItems, IArchiveUpdateCallback* updateCallback) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetFileTimeType(UInt32* type) = 0;
};
