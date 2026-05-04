#pragma once
#include "IDecl.h"

struct ISequentialInStream : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Read(void* data, UInt32 size, UInt32* processedSize) = 0;
};

struct ISequentialOutStream : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Write(const void* data, UInt32 size, UInt32* processedSize) = 0;
};

struct IInStream : public ISequentialInStream {
    virtual HRESULT STDMETHODCALLTYPE Seek(Int64 offset, UInt32 seekOrigin, UInt64* newPosition) = 0;
};

struct IOutStream : public ISequentialOutStream {
    virtual HRESULT STDMETHODCALLTYPE Seek(Int64 offset, UInt32 seekOrigin, UInt64* newPosition) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetSize(UInt64 newSize) = 0;
};
