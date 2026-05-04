#pragma once
#include "IDecl.h"

struct ICryptoGetTextPassword : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE CryptoGetTextPassword(BSTR* password) = 0;
};

struct ICryptoGetTextPassword2 : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE CryptoGetTextPassword2(Int32* passwordIsDefined, BSTR* password) = 0;
};
