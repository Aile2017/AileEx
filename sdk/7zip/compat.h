#pragma once
// Minimal type aliases so 7-Zip SDK headers compile without the full SDK tree.
#include <windows.h>
#include <objbase.h>

typedef unsigned char  Byte;
typedef unsigned short UInt16;
typedef unsigned int   UInt32;
typedef signed   int   Int32;
typedef unsigned long long UInt64;
typedef signed   long long Int64;

#ifndef STDMETHODIMP
#define STDMETHODIMP        HRESULT STDMETHODCALLTYPE
#define STDMETHODIMP_(t)    t STDMETHODCALLTYPE
#endif

// Helpers to declare COM-style pure virtual methods
#define Z7_IFACE_COM_IUnknown_DECL(x) \
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppv) = 0; \
    virtual ULONG   STDMETHODCALLTYPE AddRef() = 0;  \
    virtual ULONG   STDMETHODCALLTYPE Release() = 0;

// Simple IUnknown ref-count mixin for internal COM objects
#define Z7_COM_UNKNOWN_IMP \
    LONG _refCount = 1; \
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override { return E_NOINTERFACE; } \
    ULONG   STDMETHODCALLTYPE AddRef()  override { return (ULONG)InterlockedIncrement(&_refCount); } \
    ULONG   STDMETHODCALLTYPE Release() override { \
        LONG r = InterlockedDecrement(&_refCount); \
        if (r == 0) delete this; \
        return (ULONG)r; \
    }
