#include "Probe.h"
#include <new>
static volatile LONG objects=0;
class Service final: public ITfTextInputProcessorEx,public ITfKeyEventSink,public ITfThreadMgrEventSink {
    LONG refs=1;ComPtr<ITfThreadMgr> manager;TfClientId client=TF_CLIENTID_NULL;DWORD cookie=TF_INVALID_COOKIE;
    HANDLE mapping=nullptr;Shared* shared=nullptr;
    void Record(ITfContext* context,DWORD event){
        if(!shared){mapping=OpenFileMappingW(FILE_MAP_WRITE,FALSE,MappingName);if(mapping){shared=static_cast<Shared*>(MapViewOfFile(mapping,FILE_MAP_WRITE,0,0,sizeof(Shared)));if(!shared){CloseHandle(mapping);mapping=nullptr;}}}
        if(!shared||shared->targetPid!=GetCurrentProcessId())return;
        auto value=Observe(manager.Get(),context,event);
        // All TSF callbacks for this instance execute on its owning UI thread.
        // A diagnostic controller samples this slot, never the application's text.
        if(InterlockedCompareExchange(&shared->writer,1,0)!=0)return;
        InterlockedIncrement(&shared->sequence);shared->sample=value;InterlockedIncrement(&shared->sequence);InterlockedExchange(&shared->writer,0);
    }
public:
    Service(){InterlockedIncrement(&objects);}
    ~Service(){Deactivate();if(shared)UnmapViewOfFile(shared);if(mapping)CloseHandle(mapping);InterlockedDecrement(&objects);}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override{
        if(!out)return E_POINTER;*out=nullptr;
        if(iid==IID_IUnknown||iid==IID_ITfTextInputProcessor||iid==IID_ITfTextInputProcessorEx)*out=static_cast<ITfTextInputProcessorEx*>(this);
        else if(iid==IID_ITfKeyEventSink)*out=static_cast<ITfKeyEventSink*>(this);
        else if(iid==IID_ITfThreadMgrEventSink)*out=static_cast<ITfThreadMgrEventSink*>(this);
        else return E_NOINTERFACE;AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return InterlockedIncrement(&refs);}
    ULONG STDMETHODCALLTYPE Release() override{auto n=InterlockedDecrement(&refs);if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE Activate(ITfThreadMgr* value,TfClientId id) override{return ActivateEx(value,id,0);}
    HRESULT STDMETHODCALLTYPE ActivateEx(ITfThreadMgr* value,TfClientId id,DWORD) override{
        if(!value)return E_INVALIDARG;if(manager)return E_UNEXPECTED;manager=value;client=id;
        ComPtr<ITfKeystrokeMgr> keys;auto hr=manager.As(&keys);if(SUCCEEDED(hr))hr=keys->AdviseKeyEventSink(client,this,TRUE);
        if(FAILED(hr)){manager.Reset();client=TF_CLIENTID_NULL;return hr;}
        ComPtr<ITfSource> source;if(SUCCEEDED(manager.As(&source)))source->AdviseSink(IID_ITfThreadMgrEventSink,static_cast<ITfThreadMgrEventSink*>(this),&cookie);
        Record(nullptr,1);return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Deactivate() override{
        if(!manager)return S_OK;
        ComPtr<ITfSource> source;if(cookie!=TF_INVALID_COOKIE&&SUCCEEDED(manager.As(&source)))source->UnadviseSink(cookie);cookie=TF_INVALID_COOKIE;
        ComPtr<ITfKeystrokeMgr> keys;if(SUCCEEDED(manager.As(&keys)))keys->UnadviseKeyEventSink(client);
        manager.Reset();client=TF_CLIENTID_NULL;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnSetFocus(BOOL) override{Record(nullptr,2);return S_OK;}
    HRESULT STDMETHODCALLTYPE OnTestKeyDown(ITfContext* c,WPARAM,LPARAM,BOOL* eaten) override{if(!eaten)return E_POINTER;*eaten=FALSE;Record(c,3);return S_OK;}
    HRESULT STDMETHODCALLTYPE OnTestKeyUp(ITfContext* c,WPARAM,LPARAM,BOOL* eaten) override{if(!eaten)return E_POINTER;*eaten=FALSE;Record(c,4);return S_OK;}
    HRESULT STDMETHODCALLTYPE OnKeyDown(ITfContext* c,WPARAM,LPARAM,BOOL* eaten) override{if(!eaten)return E_POINTER;*eaten=FALSE;Record(c,5);return S_OK;}
    HRESULT STDMETHODCALLTYPE OnKeyUp(ITfContext* c,WPARAM,LPARAM,BOOL* eaten) override{if(!eaten)return E_POINTER;*eaten=FALSE;Record(c,6);return S_OK;}
    HRESULT STDMETHODCALLTYPE OnPreservedKey(ITfContext*,REFGUID,BOOL* eaten) override{if(!eaten)return E_POINTER;*eaten=FALSE;return S_OK;}
    HRESULT STDMETHODCALLTYPE OnInitDocumentMgr(ITfDocumentMgr*) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnUninitDocumentMgr(ITfDocumentMgr*) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnSetFocus(ITfDocumentMgr*,ITfDocumentMgr*) override{Record(nullptr,7);return S_OK;}
    HRESULT STDMETHODCALLTYPE OnPushContext(ITfContext* c) override{Record(c,8);return S_OK;}
    HRESULT STDMETHODCALLTYPE OnPopContext(ITfContext*) override{Record(nullptr,9);return S_OK;}
};
class Factory final:public IClassFactory{
    LONG refs=1;
public:
    Factory(){InterlockedIncrement(&objects);}~Factory(){InterlockedDecrement(&objects);}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override{if(!out)return E_POINTER;*out=nullptr;if(iid!=IID_IUnknown&&iid!=IID_IClassFactory)return E_NOINTERFACE;*out=this;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef() override{return InterlockedIncrement(&refs);}
    ULONG STDMETHODCALLTYPE Release() override{auto n=InterlockedDecrement(&refs);if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer,REFIID iid,void** out) override{if(!out)return E_POINTER;*out=nullptr;if(outer)return CLASS_E_NOAGGREGATION;auto s=new(std::nothrow)Service;if(!s)return E_OUTOFMEMORY;auto hr=s->QueryInterface(iid,out);s->Release();return hr;}
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override{if(lock)InterlockedIncrement(&objects);else InterlockedDecrement(&objects);return S_OK;}
};
STDAPI DllCanUnloadNow(){return objects==0?S_OK:S_FALSE;}
STDAPI DllGetClassObject(REFCLSID id,REFIID iid,void** out){if(!out)return E_POINTER;*out=nullptr;if(id!=ServiceId)return CLASS_E_CLASSNOTAVAILABLE;auto f=new(std::nothrow)Factory;if(!f)return E_OUTOFMEMORY;auto hr=f->QueryInterface(iid,out);f->Release();return hr;}
