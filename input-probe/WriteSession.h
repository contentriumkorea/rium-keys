#pragma once
#include "Probe.h"
#include <functional>
class WriteSession final:public ITfEditSession {
    LONG refs=1;std::function<HRESULT(TfEditCookie)> action;
public:
    explicit WriteSession(std::function<HRESULT(TfEditCookie)> value):action(std::move(value)){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(id!=IID_IUnknown&&id!=IID_ITfEditSession)return E_NOINTERFACE;*out=this;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return InterlockedIncrement(&refs);}
    ULONG STDMETHODCALLTYPE Release()override{auto n=InterlockedDecrement(&refs);if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE DoEditSession(TfEditCookie cookie)override{try{return action(cookie);}catch(...){return E_UNEXPECTED;}}
};
inline HRESULT WriteNow(ITfContext* context,TfClientId client,std::function<HRESULT(TfEditCookie)> action){
    if(!context)return E_INVALIDARG;
    ComPtr<WriteSession> session;session.Attach(new WriteSession(std::move(action)));
    HRESULT result=E_PENDING;auto hr=context->RequestEditSession(client,session.Get(),TF_ES_SYNC|TF_ES_READWRITE,&result);
    return FAILED(hr)?hr:result;
}
