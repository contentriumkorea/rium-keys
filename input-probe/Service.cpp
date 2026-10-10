#include "Probe.h"
#include "WriteSession.h"
#include "../input-core/TextInput.h"
#include <new>
static volatile LONG objects=0;
#include "DisplayAttributes.h"
class Service final: public ITfTextInputProcessorEx,public ITfKeyEventSink,public ITfThreadMgrEventSink,public ITfCompositionSink,public ITfDisplayAttributeProvider {
    LONG refs=1;ComPtr<ITfThreadMgr> manager;TfClientId client=TF_CLIENTID_NULL;DWORD cookie=TF_INVALID_COOKIE;
    HANDLE mapping=nullptr;Shared* shared=nullptr;
    rium::TextInput input;ComPtr<ITfContext> composingContext;ComPtr<ITfComposition> composition;bool ending=false,recovery=false;
    TfGuidAtom attributeAtom=TF_INVALID_GUIDATOM;
    HRESULT Attributes(ITfContext* context,TfEditCookie editCookie,ITfRange* range,bool set){
        ComPtr<ITfProperty> property;auto hr=context->GetProperty(GUID_PROP_ATTRIBUTE,&property);if(FAILED(hr))return hr;
        if(!set)return property->Clear(editCookie,range);
        VARIANT value;VariantInit(&value);value.vt=VT_I4;value.lVal=attributeAtom;return property->SetValue(editCookie,range,&value);
    }
    void SyncFixtureMode(){
        if(!Connect()||!manager)return;
        ComPtr<ITfCompartmentMgr> parts;if(FAILED(manager.As(&parts)))return;
        for(auto id:{GUID_COMPARTMENT_KEYBOARD_OPENCLOSE,GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION}){ComPtr<ITfCompartment> part;if(FAILED(parts->GetCompartment(id,&part)))continue;VARIANT value;VariantInit(&value);value.vt=VT_I4;value.lVal=input.Korean()?1:0;part->SetValue(client,&value);}
    }
    static rium::Modifiers Modifiers(){return {(GetKeyState(VK_SHIFT)&0x8000)!=0,(GetKeyState(VK_CONTROL)&0x8000)!=0,(GetKeyState(VK_MENU)&0x8000)!=0,((GetKeyState(VK_LWIN)|GetKeyState(VK_RWIN))&0x8000)!=0};}
    bool FixtureEditing(ITfContext* context){
        if(!Connect()||!shared->composeFixture||!context)return false;
        TF_STATUS status{};if(FAILED(context->GetStatus(&status))||(status.dwDynamicFlags&TF_SD_READONLY))return false;
        for(auto id:{GUID_COMPARTMENT_KEYBOARD_DISABLED,GUID_COMPARTMENT_EMPTYCONTEXT}){auto value=ReadValue(context,id);if(value.type==VT_I4&&value.value)return false;}
        return true;
    }
    HRESULT Finish(){
        if(!composition){input.Reset();composingContext.Reset();recovery=false;return S_OK;}
        auto hr=WriteNow(composingContext.Get(),client,[&](TfEditCookie cookie){ComPtr<ITfRange> range;if(SUCCEEDED(composition->GetRange(&range)))Attributes(composingContext.Get(),cookie,range.Get(),false);ending=true;auto result=composition->EndComposition(cookie);ending=false;return result;});
        if(SUCCEEDED(hr)){composition.Reset();composingContext.Reset();input.Reset();recovery=false;}else recovery=true;return hr;
    }
    HRESULT FinishLater(){
        if(!composition||!composingContext)return S_OK;
        ComPtr<Service> self=this;auto original=composition;auto context=composingContext;
        ComPtr<WriteSession> session;session.Attach(new WriteSession([self,original](TfEditCookie cookie){
            self->ending=true;auto hr=original->EndComposition(cookie);self->ending=false;
            if(SUCCEEDED(hr)&&self->composition.Get()==original.Get()){self->composition.Reset();self->composingContext.Reset();self->input.Reset();self->recovery=false;}
            if(self->Connect())InterlockedExchange(&self->shared->cleanupResult,hr);return hr;
        }));
        HRESULT result=E_PENDING;auto hr=context->RequestEditSession(client,session.Get(),TF_ES_ASYNC|TF_ES_READWRITE,&result);
        return FAILED(hr)?hr:result;
    }
    HRESULT Apply(ITfContext* context,const rium::KeyResult& result,bool& written){
        written=false;
        if(!composition&&result.committed.empty()&&result.composing.empty())return S_OK;
        return WriteNow(context,client,[&](TfEditCookie cookie){
            ComPtr<ITfRange> range;HRESULT hr;
            if(composition)hr=composition->GetRange(&range);
            else {ComPtr<ITfInsertAtSelection> insert;hr=context->QueryInterface(IID_PPV_ARGS(&insert));if(SUCCEEDED(hr))hr=insert->InsertTextAtSelection(cookie,TF_IAS_QUERYONLY,nullptr,0,&range);}
            if(FAILED(hr))return hr;
            if(composition)Attributes(context,cookie,range.Get(),false);
            ComPtr<ITfContextComposition> owner;hr=context->QueryInterface(IID_PPV_ARGS(&owner));if(FAILED(hr))return hr;
            if(!composition&&!result.composing.empty()){hr=owner->StartComposition(cookie,range.Get(),this,&composition);if(FAILED(hr)||!composition)return FAILED(hr)?hr:E_FAIL;composingContext=context;
                // Write through the composition-owned range. An IMM transitory
                // context need not expand a separate insertion-query range.
                range.Reset();hr=composition->GetRange(&range);if(FAILED(hr))return hr;}
            auto text=result.committed+result.composing;
            hr=range->SetText(cookie,0,text.c_str(),static_cast<LONG>(text.size()));if(FAILED(hr))return hr;written=true;
            ComPtr<ITfRange> caret;hr=range->Clone(&caret);if(FAILED(hr))return hr;
            hr=caret->Collapse(cookie,TF_ANCHOR_END);if(FAILED(hr))return hr;
            if(composition){
                if(result.composing.empty()){ending=true;hr=composition->EndComposition(cookie);ending=false;if(FAILED(hr))return hr;composition.Reset();composingContext.Reset();}
                else {ComPtr<ITfRange> preedit;hr=caret->Clone(&preedit);if(FAILED(hr))return hr;LONG shifted=0;hr=preedit->ShiftStart(cookie,-static_cast<LONG>(result.composing.size()),&shifted,nullptr);if(FAILED(hr))return hr;hr=composition->ShiftStart(cookie,preedit.Get());if(FAILED(hr))return hr;hr=Attributes(context,cookie,preedit.Get(),true);if(FAILED(hr))return hr;}
            }
            TF_SELECTION selection{caret.Get(),{TF_AE_NONE,FALSE}};return context->SetSelection(cookie,1,&selection);
        });
    }
    bool Connect(){
        if(!shared){mapping=OpenFileMappingW(FILE_MAP_WRITE,FALSE,MappingName().c_str());if(mapping){shared=static_cast<Shared*>(MapViewOfFile(mapping,FILE_MAP_WRITE,0,0,sizeof(Shared)));if(!shared){CloseHandle(mapping);mapping=nullptr;}}}
        return shared&&shared->targetPid==GetCurrentProcessId();
    }
    void Record(ITfContext* context,DWORD event){
        if(!Connect())return;
        if(event==3)InterlockedIncrement(&shared->testKeyDownCount);
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
        else if(iid==IID_ITfCompositionSink)*out=static_cast<ITfCompositionSink*>(this);
        else if(iid==IID_ITfDisplayAttributeProvider)*out=static_cast<ITfDisplayAttributeProvider*>(this);
        else return E_NOINTERFACE;AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return InterlockedIncrement(&refs);}
    ULONG STDMETHODCALLTYPE Release() override{auto n=InterlockedDecrement(&refs);if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** out)override{if(!out)return E_POINTER;*out=new(std::nothrow)AttributeEnumerator;return *out?S_OK:E_OUTOFMEMORY;}
    HRESULT STDMETHODCALLTYPE GetDisplayAttributeInfo(REFGUID id,ITfDisplayAttributeInfo** out)override{if(!out)return E_POINTER;*out=nullptr;if(id!=InputAttributeId)return E_INVALIDARG;*out=new(std::nothrow)InputAttribute;return *out?S_OK:E_OUTOFMEMORY;}
    HRESULT STDMETHODCALLTYPE Activate(ITfThreadMgr* value,TfClientId id) override{return ActivateEx(value,id,0);}
    HRESULT STDMETHODCALLTYPE ActivateEx(ITfThreadMgr* value,TfClientId id,DWORD flags) override{
        if(Connect()){shared->clientId=id;shared->activationFlags=flags;InterlockedExchange(&shared->activationEntered,1);}
        if(!value)return E_INVALIDARG;if(manager)return E_UNEXPECTED;manager=value;client=id;
        ComPtr<ITfKeystrokeMgr> keys;auto hr=manager.As(&keys);if(SUCCEEDED(hr))hr=keys->AdviseKeyEventSink(client,this,TRUE);
        if(Connect())InterlockedExchange(&shared->sinkResult,hr);
        if(FAILED(hr)){manager.Reset();client=TF_CLIENTID_NULL;return hr;}
        ComPtr<ITfSource> source;if(SUCCEEDED(manager.As(&source)))source->AdviseSink(IID_ITfThreadMgrEventSink,static_cast<ITfThreadMgrEventSink*>(this),&cookie);
        ComPtr<ITfCategoryMgr> categories;if(SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&categories))))categories->RegisterGUID(InputAttributeId,&attributeAtom);
        SyncFixtureMode();Record(nullptr,1);return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Deactivate() override{
        if(!manager)return S_OK;
        auto cleanup=Finish();if(FAILED(cleanup))cleanup=FinishLater();if(Connect())InterlockedExchange(&shared->cleanupResult,cleanup);
        ComPtr<ITfSource> source;if(cookie!=TF_INVALID_COOKIE&&SUCCEEDED(manager.As(&source)))source->UnadviseSink(cookie);cookie=TF_INVALID_COOKIE;
        ComPtr<ITfKeystrokeMgr> keys;if(SUCCEEDED(manager.As(&keys)))keys->UnadviseKeyEventSink(client);
        manager.Reset();client=TF_CLIENTID_NULL;return FAILED(cleanup)?cleanup:S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnSetFocus(BOOL) override{Record(nullptr,2);return S_OK;}
    HRESULT STDMETHODCALLTYPE OnTestKeyDown(ITfContext* c,WPARAM key,LPARAM flags,BOOL* eaten) override{if(!eaten)return E_POINTER;*eaten=FALSE;Record(c,3);try{if(FixtureEditing(c)){auto preview=input;auto result=preview.KeyDown(static_cast<unsigned>(key),Modifiers(),(flags&(1LL<<30))!=0);*eaten=recovery||result.eaten||!result.committed.empty();}}catch(...){return E_UNEXPECTED;}return S_OK;}
    HRESULT STDMETHODCALLTYPE OnTestKeyUp(ITfContext* c,WPARAM,LPARAM,BOOL* eaten) override{if(!eaten)return E_POINTER;*eaten=FALSE;Record(c,4);return S_OK;}
    HRESULT STDMETHODCALLTYPE OnKeyDown(ITfContext* c,WPARAM key,LPARAM flags,BOOL* eaten) override{
        if(!eaten)return E_POINTER;*eaten=FALSE;Record(c,5);
        try{
            if(!FixtureEditing(c))return S_OK;
            if(recovery&&FAILED(Finish()))return S_OK;
            if(composingContext&&composingContext.Get()!=c){if(Connect())InterlockedIncrement(&shared->contextCommits);if(FAILED(Finish()))return S_OK;}
            auto next=input;auto result=next.KeyDown(static_cast<unsigned>(key),Modifiers(),(flags&(1LL<<30))!=0);bool written=false;auto hr=Apply(c,result,written);if(Connect())InterlockedExchange(&shared->editResult,hr);
            // After a successful document write, never pass a consumed letter
            // through again, even if a later selection/composition call fails.
            if(SUCCEEDED(hr)){auto changed=input.Korean()!=next.Korean();input=std::move(next);*eaten=result.eaten;if(changed)SyncFixtureMode();}
            else {
                // SetText or later TSF operations may already have changed range
                // ownership. Never resume old preedit against that range.
                recovery=composition!=nullptr;input.Reset();if(recovery)Finish();
                if(written)*eaten=result.eaten;
            }
        }catch(...){return E_UNEXPECTED;}return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnKeyUp(ITfContext* c,WPARAM,LPARAM,BOOL* eaten) override{if(!eaten)return E_POINTER;*eaten=FALSE;Record(c,6);return S_OK;}
    HRESULT STDMETHODCALLTYPE OnPreservedKey(ITfContext*,REFGUID,BOOL* eaten) override{if(!eaten)return E_POINTER;*eaten=FALSE;return S_OK;}
    HRESULT STDMETHODCALLTYPE OnInitDocumentMgr(ITfDocumentMgr*) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnUninitDocumentMgr(ITfDocumentMgr*) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnSetFocus(ITfDocumentMgr* document,ITfDocumentMgr*) override{Record(nullptr,7);if(composingContext){ComPtr<ITfContext> top;if(document)document->GetTop(&top);if(top.Get()!=composingContext.Get()){if(Connect())InterlockedIncrement(&shared->focusCommits);Finish();}}return S_OK;}
    HRESULT STDMETHODCALLTYPE OnPushContext(ITfContext* c) override{Record(c,8);return S_OK;}
    HRESULT STDMETHODCALLTYPE OnPopContext(ITfContext*) override{Record(nullptr,9);return S_OK;}
    HRESULT STDMETHODCALLTYPE OnCompositionTerminated(TfEditCookie,ITfComposition* value)override{if(!ending&&composition.Get()==value){if(Connect())InterlockedIncrement(&shared->externalTerminations);composition.Reset();composingContext.Reset();input.Reset();}return S_OK;}
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
