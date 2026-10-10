#pragma once
#include "Probe.h"
#include <textstor.h>
#include <olectl.h>
#include <string>
#include <algorithm>
// A deliberately small TSF-aware document owned only by the disposable fixture.
// It is not an application adapter and must never be used with user documents.
class FixtureStore final: public ITextStoreACP,public ITfContextOwnerCompositionSink {
    LONG refs=1;DWORD lock=0,pending=0;ComPtr<ITextStoreACPSink> sink;
    TS_SELECTION_ACP selection{0,0,{TS_AE_END,FALSE}};HWND window;DWORD advertisedStaticFlags;
    bool Range(LONG start,LONG end)const{return start>=0&&end>=start&&end<=static_cast<LONG>(text.size());}
    bool Readable()const{return (lock&TS_LF_READ)!=0;}
    bool Writable()const{return (lock&TS_LF_READWRITE)==TS_LF_READWRITE;}
public:
    std::wstring text;
    bool rejectNextWrite=false,rejectNextText=false,rejectNextSelection=false,readOnly=false;
    void Clear(){auto previous=static_cast<LONG>(text.size());text.clear();selection.acpStart=selection.acpEnd=0;if(sink){TS_TEXTCHANGE change{0,previous,0};sink->OnTextChange(0,&change);sink->OnSelectionChange();}}
    void InsertFromApplication(const std::wstring& value){auto start=selection.acpStart,end=selection.acpEnd;text.replace(static_cast<size_t>(start),static_cast<size_t>(end-start),value);selection.acpStart=selection.acpEnd=start+static_cast<LONG>(value.size());if(sink){TS_TEXTCHANGE change{start,end,selection.acpEnd};sink->OnTextChange(0,&change);sink->OnSelectionChange();}}
    explicit FixtureStore(HWND value,DWORD staticFlags=TS_SS_NOHIDDENTEXT):window(value),advertisedStaticFlags(staticFlags){}
    // A copy of this disposable document's state; never queries another application.
    TS_SELECTION_ACP SelectionSnapshotForFixture()const{return selection;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(id==IID_IUnknown||id==IID_ITextStoreACP)*out=static_cast<ITextStoreACP*>(this);else if(id==IID_ITfContextOwnerCompositionSink)*out=static_cast<ITfContextOwnerCompositionSink*>(this);else return E_NOINTERFACE;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return InterlockedIncrement(&refs);}
    ULONG STDMETHODCALLTYPE Release()override{auto n=InterlockedDecrement(&refs);if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE AdviseSink(REFIID id,IUnknown* value,DWORD)override{if(id!=IID_ITextStoreACPSink||!value)return E_INVALIDARG;if(sink)return CONNECT_E_ADVISELIMIT;return value->QueryInterface(IID_PPV_ARGS(&sink));}
    HRESULT STDMETHODCALLTYPE UnadviseSink(IUnknown* value)override{if(!value||!sink)return CONNECT_E_NOCONNECTION;ComPtr<IUnknown> a,b;value->QueryInterface(IID_PPV_ARGS(&a));sink.As(&b);if(a.Get()!=b.Get())return CONNECT_E_NOCONNECTION;sink.Reset();return S_OK;}
    HRESULT STDMETHODCALLTYPE RequestLock(DWORD flags,HRESULT* result)override{if(!result)return E_POINTER;if(!sink)return E_UNEXPECTED;if(rejectNextWrite&&(flags&TS_LF_READWRITE)==TS_LF_READWRITE){rejectNextWrite=false;*result=E_FAIL;return S_OK;}if(lock){if(flags&TS_LF_SYNC)*result=TS_E_SYNCHRONOUS;else{pending|=flags;*result=TS_S_ASYNC;}return S_OK;}lock=flags;*result=sink->OnLockGranted(flags);lock=0;for(int i=0;pending&&i<16;++i){auto next=pending;pending=0;lock=next;sink->OnLockGranted(next);lock=0;}return S_OK;}
    HRESULT STDMETHODCALLTYPE GetStatus(TS_STATUS* value)override{if(!value)return E_POINTER;*value={readOnly?static_cast<DWORD>(TS_SD_READONLY):0UL,advertisedStaticFlags};return S_OK;}
    HRESULT STDMETHODCALLTYPE QueryInsert(LONG start,LONG end,ULONG,LONG* a,LONG* b)override{if(!a||!b)return E_POINTER;if(!Range(start,end))return TS_E_INVALIDPOS;*a=start;*b=end;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetSelection(ULONG index,ULONG count,TS_SELECTION_ACP* out,ULONG* fetched)override{if(!out||!fetched)return E_POINTER;*fetched=0;if(!Readable())return TS_E_NOLOCK;if(index!=0&&index!=TS_DEFAULT_SELECTION)return E_INVALIDARG;if(count){*out=selection;*fetched=1;}return S_OK;}
    HRESULT STDMETHODCALLTYPE SetSelection(ULONG count,const TS_SELECTION_ACP* value)override{if(!value||count!=1)return E_INVALIDARG;if(!Writable())return TS_E_NOLOCK;if(rejectNextSelection){rejectNextSelection=false;return E_FAIL;}if(!Range(value->acpStart,value->acpEnd))return TS_E_INVALIDPOS;selection=*value;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetText(LONG start,LONG end,WCHAR* plain,ULONG requested,ULONG* returned,TS_RUNINFO* runs,ULONG runRequested,ULONG* runReturned,LONG* next)override{if(!returned||!runReturned||!next)return E_POINTER;*returned=0;*runReturned=0;if(!Readable())return TS_E_NOLOCK;if(end==-1)end=static_cast<LONG>(text.size());if(!Range(start,end))return TS_E_INVALIDPOS;ULONG count=(std::min)(static_cast<ULONG>(end-start),requested);if(count&&!plain)return E_POINTER;if(count)memcpy(plain,text.data()+start,count*sizeof(WCHAR));*returned=count;*next=start+static_cast<LONG>(count);if(runRequested){if(!runs)return E_POINTER;runs[0]={count,TS_RT_PLAIN};*runReturned=count?1:0;}return S_OK;}
    HRESULT STDMETHODCALLTYPE SetText(DWORD,LONG start,LONG end,const WCHAR* value,ULONG count,TS_TEXTCHANGE* change)override{if(!change||(!value&&count))return E_POINTER;if(!Writable())return TS_E_NOLOCK;if(rejectNextText){rejectNextText=false;return E_FAIL;}if(!Range(start,end))return TS_E_INVALIDPOS;text.replace(static_cast<size_t>(start),static_cast<size_t>(end-start),value?value:L"",count);*change={start,end,start+static_cast<LONG>(count)};selection.acpStart=selection.acpEnd=change->acpNewEnd;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetFormattedText(LONG,LONG,IDataObject** out)override{if(out)*out=nullptr;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetEmbedded(LONG,REFGUID,REFIID,IUnknown** out)override{if(out)*out=nullptr;return TS_E_NOOBJECT;}
    HRESULT STDMETHODCALLTYPE QueryInsertEmbedded(const GUID*,const FORMATETC*,BOOL* value)override{if(!value)return E_POINTER;*value=FALSE;return S_OK;}
    HRESULT STDMETHODCALLTYPE InsertEmbedded(DWORD,LONG,LONG,IDataObject*,TS_TEXTCHANGE*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE InsertTextAtSelection(DWORD flags,const WCHAR* value,ULONG count,LONG* start,LONG* end,TS_TEXTCHANGE* change)override{if(!Readable())return TS_E_NOLOCK;auto a=selection.acpStart,b=selection.acpEnd;if(start)*start=a;if(end)*end=b;if(flags&TS_IAS_QUERYONLY)return S_OK;if(!Writable())return TS_E_NOLOCK;TS_TEXTCHANGE local{};auto hr=SetText(0,a,b,value,count,change?change:&local);if(SUCCEEDED(hr)&&end)*end=a+static_cast<LONG>(count);return hr;}
    HRESULT STDMETHODCALLTYPE InsertEmbeddedAtSelection(DWORD,IDataObject*,LONG*,LONG*,TS_TEXTCHANGE*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE RequestSupportedAttrs(DWORD,ULONG,const TS_ATTRID*)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE RequestAttrsAtPosition(LONG,ULONG,const TS_ATTRID*,DWORD)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE RequestAttrsTransitioningAtPosition(LONG,ULONG,const TS_ATTRID*,DWORD)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE FindNextAttrTransition(LONG,LONG halt,ULONG,const TS_ATTRID*,DWORD,LONG* next,BOOL* found,LONG* offset)override{if(!next||!found||!offset)return E_POINTER;*next=halt;*found=FALSE;*offset=0;return S_OK;}
    HRESULT STDMETHODCALLTYPE RetrieveRequestedAttrs(ULONG,TS_ATTRVAL*,ULONG* fetched)override{if(!fetched)return E_POINTER;*fetched=0;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetEndACP(LONG* end)override{if(!end)return E_POINTER;if(!Readable())return TS_E_NOLOCK;*end=static_cast<LONG>(text.size());return S_OK;}
    HRESULT STDMETHODCALLTYPE GetActiveView(TsViewCookie* view)override{if(!view)return E_POINTER;*view=0;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetACPFromPoint(TsViewCookie,const POINT*,DWORD,LONG* acp)override{if(!acp)return E_POINTER;*acp=selection.acpEnd;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetTextExt(TsViewCookie,LONG start,LONG end,RECT* rect,BOOL* clipped)override{if(!rect||!clipped)return E_POINTER;if(!Readable())return TS_E_NOLOCK;if(!Range(start,end))return TS_E_INVALIDPOS;*clipped=FALSE;return GetWindowRect(window,rect)?S_OK:E_FAIL;}
    HRESULT STDMETHODCALLTYPE GetScreenExt(TsViewCookie,RECT* rect)override{if(!rect)return E_POINTER;return GetWindowRect(window,rect)?S_OK:E_FAIL;}
    HRESULT STDMETHODCALLTYPE GetWnd(TsViewCookie,HWND* value)override{if(!value)return E_POINTER;*value=window;return S_OK;}
    HRESULT STDMETHODCALLTYPE OnStartComposition(ITfCompositionView*,BOOL* accepted)override{if(!accepted)return E_POINTER;*accepted=TRUE;return S_OK;}
    HRESULT STDMETHODCALLTYPE OnUpdateComposition(ITfCompositionView*,ITfRange*)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnEndComposition(ITfCompositionView*)override{return S_OK;}
};
