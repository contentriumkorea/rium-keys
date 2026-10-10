// Exercise the actual edit-session implementation, including host callbacks and locks.
#include <initguid.h>
#include "third_party/jamotong/src/comp_inline.h"
// Keep focus transitions inside the fixture; never move the user's Win32 focus.
static HWND fixtureFocus;
static HWND FixtureGetFocus(void) { return fixtureFocus; }
static HWND FixtureSetFocus(HWND hwnd) { HWND old=fixtureFocus;fixtureFocus=hwnd;return old; }
// Preserve ITfThreadMgr::GetFocus(This, out), while replacing Win32 GetFocus().
#define RIUM_FOCUS_CALL(first, ...) first
#define GetFocus(...) RIUM_FOCUS_CALL(__VA_OPT__(GetFocus,) FixtureGetFocus)(__VA_ARGS__)
#define SetFocus FixtureSetFocus
#include "input-owner.c"
#include "third_party/jamotong/src/comp_inline.c"
#include <stdio.h>
#include <stdlib.h>

long g_ourEditDepth;
void JamoDiag(const char *fmt, ...) { (void)fmt; }
void DA_ApplyToRange(ITfContext *ctx, TfEditCookie ec, ITfRange *r, TfGuidAtom a) {
    (void)ctx; (void)ec; (void)r; (void)a;
}
static int checks, failures, cleared;
void Jamotong_ClearCompositionState(JamotongTextService *svc) {
    ++cleared; Fsm_Init(&svc->fsm); JamoComp_ResetPathCache(svc);
}
static void check(int ok, const char *name) {
    ++checks; if (!ok) { ++failures; printf("FAIL: %s\n", name); }
}
typedef struct { ITfRange iface; LONG start, end; wchar_t *text; } Range;
typedef struct { ITfComposition iface; LONG refs, start, end; int ended; wchar_t *text; } Composition;
static JamotongTextService svc;
static Composition comp, newer;
static ITfContext ctx, otherCtx;
static ITfContext *focusedContext;
static HWND otherWindow;
static wchar_t document[64];
static wchar_t otherDocument[64];
static ITfContext *insertionContext;
static LONG selStart, selEnd;
static TF_SELECTIONSTYLE style;
static DWORD staticFlags;
// The native Chromium TSF store also advertises TRANSITORY | NOHIDDENTEXT.
// The opaque CUAS parent supports IUnknown but rejects ITfDocumentMgr.
static const GUID testParentGuid={0x8be347f8,0xc7a0,0x11d7,{0xb4,0x08,0x00,0x06,0x5b,0x84,0x43,0x5c}};
typedef struct {
    HRESULT statusHr,documentHr,managerHr,enumHr,nextHr,compartmentHr,valueHr;
    BOOL present,nullParent,nullDocument,nullManager,nullEnum,nullCompartment;
    BOOL endlessEnum,changeFocus;
    VARTYPE valueType;
    int documentCalls,enumCalls,nextCalls,getCalls,valueCalls,parentQueries;
    LONG documentRefs,managerRefs,enumRefs,compartmentRefs,parentRefs;
} ParentProbe;
static ParentProbe parentProbe;
static BOOL parent_refs_balanced(void) {
    return !parentProbe.documentRefs&&!parentProbe.managerRefs&&!parentProbe.enumRefs&&
           !parentProbe.compartmentRefs&&!parentProbe.parentRefs;
}
static int failSelection, shortShift, terminateSelection, terminateShift, writes, requestMode, requests;
static BOOL replaceOnRelease;
static int oldContextReleases, newContextReleases;
static int compositionAddReentry, contextAddReentry;
static void reenterRetention(int mode);
static ITfEditSession *queued;
static RiumOwnerStamp observedOwner;
static BOOL ownerChangeOnRange, ownerChangeAfterWrite;
static int retirementSelections, retirementEndMode, retirementTopChange;
static void ownerReader(void *unused,RiumOwnerStamp *out) { (void)unused;*out=observedOwner; }
static void changeOwner(void) { ++observedOwner.logicalObject;++observedOwner.lifetimeToken; }
static void bindOwner(void) {
    observedOwner=(RiumOwnerStamp){.provider=1,.profile=1,.kind=RIUM_OWNER_TEXT,.deferredSafe=TRUE,
        .processId=GetCurrentProcessId(),.threadId=GetCurrentThreadId(),.focus=GetFocus(),
        .windowObject=0x100,.logicalObject=0x200,.lifetimeToken=1,.textObject=0x300};
    svc.inputOwner.reader=ownerReader;
    RiumOwner_Capture(&svc.inputOwner,&svc.inlineOwner);
}
static ULONG STDMETHODCALLTYPE tip_ref(ITfTextInputProcessor *p) { (void)p; return 1; }
static JamoTIPExVtbl tip_vtable={.AddRef=tip_ref,.Release=tip_ref};
static ULONG STDMETHODCALLTYPE ctx_ref(ITfContext *p) {
    if(p==&ctx&&contextAddReentry){int mode=contextAddReentry;contextAddReentry=0;reenterRetention(mode);}
    return 1;
}
static ULONG STDMETHODCALLTYPE ctx_release(ITfContext *p) {
    if(p==&ctx)++oldContextReleases;else if(p==&otherCtx)++newContextReleases;return 1;
}
static void terminate(void) {
    if(svc.pComposition) CS_OnCompositionTerminated((ITfCompositionSink*)&svc.lpVtblCompSink,1,svc.pComposition);
}
static ULONG STDMETHODCALLTYPE range_release(ITfRange *p) { free(p); return 0; }
static ITfRange *makeRange(LONG start,LONG end);
static HRESULT STDMETHODCALLTYPE range_clone(ITfRange *p,ITfRange **out) {
    Range *r=(Range*)p; *out=makeRange(r->start,r->end);((Range*)*out)->text=r->text;return S_OK;
}
static HRESULT STDMETHODCALLTYPE range_collapse(ITfRange *p,TfEditCookie ec,TfAnchor anchor) {
    (void)ec; Range *r=(Range*)p;
    if(anchor==TF_ANCHOR_END)r->start=r->end;else r->end=r->start; return S_OK;
}
static HRESULT STDMETHODCALLTYPE range_shift(ITfRange *p,TfEditCookie ec,LONG count,LONG *moved,const TF_HALTCOND *halt) {
    (void)ec;(void)halt; Range *r=(Range*)p;
    *moved=shortShift?0:count; r->start+=*moved;
    if(terminateShift && count>0)terminate(); return S_OK;
}
static HRESULT STDMETHODCALLTYPE range_text(ITfRange *p,TfEditCookie ec,DWORD flags,const WCHAR *text,LONG len) {
    (void)ec;(void)flags; Range *r=(Range*)p; ++writes;
    LONG oldEnd=r->end, tail=(LONG)wcslen(r->text)-oldEnd;
    memmove(r->text+r->start+len,r->text+oldEnd,(size_t)(tail+1)*sizeof(wchar_t));
    memcpy(r->text+r->start,text,(size_t)len*sizeof(wchar_t)); r->end=r->start+len;
    if(svc.pComposition)((Composition*)svc.pComposition)->end=r->end;
    if(ownerChangeAfterWrite)changeOwner();return S_OK;
}
static ITfRangeVtbl range_vtable={.Release=range_release,.Clone=range_clone,
    .Collapse=range_collapse,.ShiftStart=range_shift,.SetText=range_text};
static ITfRange *makeRange(LONG start,LONG end) {
    Range *r=calloc(1,sizeof(*r));r->iface.lpVtbl=&range_vtable;r->start=start;r->end=end;r->text=document;return &r->iface;
}
static ULONG STDMETHODCALLTYPE comp_addref(ITfComposition *p) {
    ++((Composition*)p)->refs;
    if(p==&comp.iface&&compositionAddReentry){int mode=compositionAddReentry;compositionAddReentry=0;reenterRetention(mode);}
    return ((Composition*)p)->refs;
}
static ULONG STDMETHODCALLTYPE comp_release(ITfComposition *p) {
    LONG refs=--((Composition*)p)->refs;
    if(replaceOnRelease&&p==&comp.iface){
        replaceOnRelease=FALSE;
        check(!svc.pComposition&&!svc.pCompContext&&!svc.pCompFocusHwnd&&
              !svc.compUpdatedOnce&&!svc.compFinalizePending&&!svc.compBoundaryWritten,
              "released host object cannot reenter with obsolete composition published");
        newer=comp;newer.refs=1;newer.ended=0;newer.text=otherDocument;
        svc.pComposition=&newer.iface;svc.pCompContext=&otherCtx;svc.pCompFocusHwnd=otherWindow;
        svc.compUpdatedOnce=svc.compFinalizePending=svc.compBoundaryWritten=TRUE;
        Fsm_Init(&svc.fsm);Fsm_ProcessKey(&svc.fsm,L'r',0,NULL);
    }
    return refs;
}
static HRESULT STDMETHODCALLTYPE comp_range(ITfComposition *p,ITfRange **out) {
    Composition *c=(Composition*)p; check(c->refs>0,"composition remains alive during host call");
    *out=makeRange(c->start,c->end);((Range*)*out)->text=c->text;
    if(ownerChangeOnRange)changeOwner();return S_OK;
}
static HRESULT STDMETHODCALLTYPE comp_shift(ITfComposition *p,TfEditCookie ec,ITfRange *range) {
    (void)ec;Composition *c=(Composition*)p;check(c->refs>0,"prefix never uses released composition");
    c->start=((Range*)range)->start;return S_OK;
}
static HRESULT STDMETHODCALLTYPE comp_end(ITfComposition *p,TfEditCookie ec) {
    (void)ec;Composition *c=(Composition*)p;check(c->refs>0,"end never uses released composition");
    if(retirementEndMode==1)return E_FAIL;
    ++c->ended;if(retirementEndMode==2)reenterRetention(2);if(retirementEndMode==3)changeOwner();return S_OK;
}
static ITfCompositionVtbl comp_vtable={.AddRef=comp_addref,.Release=comp_release,
    .GetRange=comp_range,.ShiftStart=comp_shift,.EndComposition=comp_end};
static void reenterRetention(int mode) {
    ForgetComposition(&svc);
    if(mode==2){
        newer=(Composition){.iface={&comp_vtable},.refs=1,.end=1,.text=otherDocument};
        svc.pComposition=&newer.iface;svc.pCompContext=&otherCtx;svc.pCompFocusHwnd=otherWindow;
        svc.compUpdatedOnce=svc.compFinalizePending=svc.compBoundaryWritten=TRUE;
    }
}
static ULONG STDMETHODCALLTYPE ins_ref(ITfInsertAtSelection *p) { (void)p; return 1; }
static HRESULT STDMETHODCALLTYPE ins_query(ITfInsertAtSelection *p,TfEditCookie ec,DWORD flags,const WCHAR *text,LONG len,ITfRange **out) {
    (void)p;(void)ec;(void)text;(void)len;
    if(flags!=TF_IAS_QUERYONLY)return E_INVALIDARG;
    *out=makeRange(selStart,selEnd);((Range*)*out)->text=insertionContext==&otherCtx?otherDocument:document;return S_OK;
}
static ITfInsertAtSelectionVtbl ins_vtable={.AddRef=ins_ref,.Release=ins_ref,.InsertTextAtSelection=ins_query};
static ITfInsertAtSelection ins={&ins_vtable};
static ULONG STDMETHODCALLTYPE cc_ref(ITfContextComposition *p) { (void)p; return 1; }
static HRESULT STDMETHODCALLTYPE cc_start(ITfContextComposition *p,TfEditCookie ec,ITfRange *range,ITfCompositionSink *sink,ITfComposition **out) {
    (void)p;(void)ec;(void)sink;
    comp=(Composition){.iface={&comp_vtable},.refs=1,.start=((Range*)range)->start,.end=((Range*)range)->end,.text=((Range*)range)->text};
    if(requestMode==4)SetFocus(otherWindow);
    *out=&comp.iface;return S_OK;
}
static ITfContextCompositionVtbl cc_vtable={.AddRef=cc_ref,.Release=cc_ref,.StartComposition=cc_start};
static ITfContextComposition cc={&cc_vtable};
static HRESULT STDMETHODCALLTYPE ctx_qi(ITfContext *p,REFIID iid,void **out) {
    *out=NULL;
    if(IsEqualIID(iid,&IID_IUnknown)||IsEqualIID(iid,&IID_ITfContext)){*out=p;return S_OK;}
    if(IsEqualIID(iid,&IID_ITfInsertAtSelection)){insertionContext=p;*out=&ins;return S_OK;}
    if(IsEqualIID(iid,&IID_ITfContextComposition)){*out=&cc;return S_OK;}
    return E_NOINTERFACE;
}
static HRESULT STDMETHODCALLTYPE parent_qi(IUnknown *p,REFIID iid,void **out) {
    ++parentProbe.parentQueries;*out=NULL;
    if(IsEqualIID(iid,&IID_IUnknown)){*out=p;++parentProbe.parentRefs;return S_OK;}
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE parent_addref(IUnknown *p) { (void)p;return ++parentProbe.parentRefs; }
static ULONG STDMETHODCALLTYPE parent_release(IUnknown *p) { (void)p;return --parentProbe.parentRefs; }
static IUnknownVtbl parent_vtable={.QueryInterface=parent_qi,.AddRef=parent_addref,.Release=parent_release};
static IUnknown parentUnknown={&parent_vtable};
static ULONG STDMETHODCALLTYPE compartment_addref(ITfCompartment *p) { (void)p;return ++parentProbe.compartmentRefs; }
static ULONG STDMETHODCALLTYPE compartment_release(ITfCompartment *p) { (void)p;return --parentProbe.compartmentRefs; }
static HRESULT STDMETHODCALLTYPE compartment_value(ITfCompartment *p,VARIANT *value) {
    (void)p;++parentProbe.valueCalls;VariantInit(value);value->vt=parentProbe.valueType;
    if(value->vt==VT_UNKNOWN&&!parentProbe.nullParent){value->punkVal=&parentUnknown;parent_addref(&parentUnknown);}
    if(value->vt==VT_I4)value->lVal=1;
    if(parentProbe.changeFocus)SetFocus(otherWindow);
    return parentProbe.valueHr;
}
static ITfCompartmentVtbl compartment_vtable={.AddRef=compartment_addref,.Release=compartment_release,.GetValue=compartment_value};
static ITfCompartment parentCompartment={&compartment_vtable};
static ULONG STDMETHODCALLTYPE enum_addref(IEnumGUID *p) { (void)p;return ++parentProbe.enumRefs; }
static ULONG STDMETHODCALLTYPE enum_release(IEnumGUID *p) { (void)p;return --parentProbe.enumRefs; }
static HRESULT STDMETHODCALLTYPE enum_next(IEnumGUID *p,ULONG count,GUID *guids,ULONG *fetched) {
    (void)p;*fetched=0;
    int index=parentProbe.nextCalls++;
    if(parentProbe.nextHr!=S_OK)return parentProbe.nextHr;
    if(count!=1)return E_INVALIDARG;
    if(!index||parentProbe.endlessEnum){guids[0]=IID_IUnknown;*fetched=1;return S_OK;}
    if(index==1&&parentProbe.present){guids[0]=testParentGuid;*fetched=1;return S_OK;}
    return S_FALSE;
}
static IEnumGUIDVtbl enum_vtable={.AddRef=enum_addref,.Release=enum_release,.Next=enum_next};
static IEnumGUID parentEnum={&enum_vtable};
static ULONG STDMETHODCALLTYPE manager_addref(ITfCompartmentMgr *p) { (void)p;return ++parentProbe.managerRefs; }
static ULONG STDMETHODCALLTYPE manager_release(ITfCompartmentMgr *p) { (void)p;return --parentProbe.managerRefs; }
static HRESULT STDMETHODCALLTYPE manager_enum(ITfCompartmentMgr *p,IEnumGUID **out) {
    (void)p;++parentProbe.enumCalls;parentProbe.nextCalls=0;*out=NULL;
    if(parentProbe.enumHr==S_OK&&!parentProbe.nullEnum){*out=&parentEnum;enum_addref(*out);}
    return parentProbe.enumHr;
}
static HRESULT STDMETHODCALLTYPE manager_compartment(ITfCompartmentMgr *p,REFGUID guid,ITfCompartment **out) {
    (void)p;++parentProbe.getCalls;*out=NULL;
    check(parentProbe.present&&parentProbe.nextCalls>=2&&IsEqualGUID(guid,&testParentGuid),
          "parent compartment is never fetched before enumeration proves presence");
    if(parentProbe.compartmentHr==S_OK&&!parentProbe.nullCompartment){*out=&parentCompartment;compartment_addref(*out);}
    return parentProbe.compartmentHr;
}
static ITfCompartmentMgrVtbl manager_vtable={.AddRef=manager_addref,.Release=manager_release,
    .GetCompartment=manager_compartment,.EnumCompartments=manager_enum};
static ITfCompartmentMgr compartmentManager={&manager_vtable};
static HRESULT STDMETHODCALLTYPE doc_qi(ITfDocumentMgr *p,REFIID iid,void **out) {
    (void)p;*out=NULL;
    if(!IsEqualIID(iid,&IID_ITfCompartmentMgr))return E_NOINTERFACE;
    if(parentProbe.managerHr==S_OK&&!parentProbe.nullManager){*out=&compartmentManager;manager_addref(*out);}
    return parentProbe.managerHr;
}
static ULONG STDMETHODCALLTYPE doc_addref(ITfDocumentMgr *p) { (void)p;return ++parentProbe.documentRefs; }
static ULONG STDMETHODCALLTYPE doc_release(ITfDocumentMgr *p) { (void)p;return --parentProbe.documentRefs; }
static HRESULT STDMETHODCALLTYPE doc_top(ITfDocumentMgr *p,ITfContext **out) { (void)p;*out=focusedContext;if(retirementTopChange)changeOwner();return S_OK; }
static ITfDocumentMgrVtbl doc_vtable={.QueryInterface=doc_qi,.AddRef=doc_addref,.Release=doc_release,.GetTop=doc_top};
static ITfDocumentMgr doc={&doc_vtable};
static HRESULT STDMETHODCALLTYPE thread_focus(ITfThreadMgr *p,ITfDocumentMgr **out) { (void)p;*out=&doc;doc_addref(*out);return S_OK; }
static ITfThreadMgrVtbl thread_vtable={.GetFocus=thread_focus};
static ITfThreadMgr thread={&thread_vtable};
static HRESULT STDMETHODCALLTYPE ctx_status(ITfContext *p,TF_STATUS *status) {
    (void)p;ZeroMemory(status,sizeof(*status));status->dwStaticFlags=staticFlags;return parentProbe.statusHr;
}
static HRESULT STDMETHODCALLTYPE ctx_document(ITfContext *p,ITfDocumentMgr **out) {
    (void)p;++parentProbe.documentCalls;*out=NULL;
    if(parentProbe.documentHr==S_OK&&!parentProbe.nullDocument){*out=&doc;doc_addref(*out);}
    return parentProbe.documentHr;
}
static HRESULT STDMETHODCALLTYPE ctx_selection(ITfContext *p,TfEditCookie ec,ULONG count,const TF_SELECTION *sel) {
    ++retirementSelections;
    (void)p;(void)ec;(void)count;if(failSelection)return E_FAIL;
    Range *r=(Range*)sel->range;selStart=r->start;selEnd=r->end;style=sel->style;
    if(terminateSelection)terminate();return S_OK;
}
static HRESULT STDMETHODCALLTYPE ctx_get_selection(ITfContext *p,TfEditCookie ec,ULONG index,ULONG count,TF_SELECTION *sel,ULONG *fetched) {
    (void)p;(void)ec;(void)index;(void)count;(void)sel;*fetched=0;return E_FAIL;
}
static HRESULT STDMETHODCALLTYPE ctx_request(ITfContext *p,TfClientId client,ITfEditSession *es,DWORD flags,HRESULT *result) {
    (void)p;(void)client;++requests;
    if(requestMode==1){
        if(flags&TF_ES_SYNC){*result=TF_E_SYNCHRONOUS;return S_OK;}
        queued=es;es->lpVtbl->AddRef(es);*result=TF_S_ASYNC;return S_OK;
    }
    if(requestMode==2){*result=E_FAIL;return S_OK;}
    if(requestMode==3)SetFocus(otherWindow);
    *result=es->lpVtbl->DoEditSession(es,1);return S_OK;
}
static ITfContextVtbl ctx_vtable={.QueryInterface=ctx_qi,.AddRef=ctx_ref,.Release=ctx_release,.GetStatus=ctx_status,
    .SetSelection=ctx_selection,.GetSelection=ctx_get_selection,.RequestEditSession=ctx_request,.GetDocumentMgr=ctx_document};
static void setup(const wchar_t *text) {
    ownerChangeOnRange=ownerChangeAfterWrite=FALSE;retirementSelections=retirementEndMode=retirementTopChange=0;
    ZeroMemory(&svc,sizeof(svc));JamoComp_Init(&svc);svc.lpVtblTIP=&tip_vtable;svc.threadMgr=&thread;focusedContext=&ctx;
    ctx.lpVtbl=&ctx_vtable;otherCtx.lpVtbl=&ctx_vtable;
    comp=(Composition){.iface={&comp_vtable},.refs=1,.end=(LONG)wcslen(text),.text=document};otherDocument[0]=0;
    svc.pComposition=&comp.iface;svc.pCompContext=&ctx;svc.pCompFocusHwnd=GetFocus();wcscpy(document,text);
    staticFlags=JAMO_TS_SS_TRANSITORY;failSelection=shortShift=terminateSelection=terminateShift=0;
    writes=requestMode=requests=cleared=0;queued=NULL;
    replaceOnRelease=FALSE;oldContextReleases=newContextReleases=0;
    compositionAddReentry=contextAddReentry=0;
    parentProbe=(ParentProbe){.present=TRUE,.valueType=VT_UNKNOWN};
}
static HRESULT work(CompOp op,wchar_t commit,wchar_t preedit) {
    InlineEditSession es={.svc=&svc,.ctx=&ctx,.op=op,.commit=commit,.preedit=preedit,.focusHwnd=GetFocus()};
    return DoInlineWork(&es,1);
}
static HRESULT runQueued(void) {
    ITfEditSession *es=queued;queued=NULL;
    HRESULT hr=es->lpVtbl->DoEditSession(es,1);es->lpVtbl->Release(es);return hr;
}
int main(void) {
    HWND owner=CreateWindowExW(0,L"STATIC",L"RIUM test owner",0,0,0,1,1,HWND_MESSAGE,NULL,GetModuleHandleW(NULL),NULL);
    otherWindow=CreateWindowExW(0,L"STATIC",L"RIUM second target",0,0,0,1,1,HWND_MESSAGE,NULL,GetModuleHandleW(NULL),NULL);
    SetFocus(owner);
    check(owner&&otherWindow&&GetFocus()==owner,"fixture uses message-only windows and simulated focus");
    setup(L"\ud55c");svc.compUpdatedOnce=svc.compFinalizePending=svc.compBoundaryWritten=TRUE;
    replaceOnRelease=TRUE;ForgetComposition(&svc);
    check(comp.refs==0&&oldContextReleases==1&&newContextReleases==0,
          "forget releases the captured old composition and context exactly once");
    check(svc.pComposition==&newer.iface&&svc.pCompContext==&otherCtx&&
          svc.pCompFocusHwnd==otherWindow&&svc.compUpdatedOnce&&
          svc.compFinalizePending&&svc.compBoundaryWritten&&newer.refs==1,
          "host Release reentry preserves replacement composition and all its boundary state");
    ForgetComposition(&svc);
    setup(L"\ud55c");replaceOnRelease=TRUE;
    check(JamoComp_Finalize(&svc)==S_OK&&svc.pComposition==&newer.iface&&
          svc.compFinalizePending&&svc.compBoundaryWritten&&Fsm_PeekPreedit(&svc.fsm)==L'\u3131',
          "completed old edit session cannot reset a reentrant replacement boundary or syllable");
    ForgetComposition(&svc);
    setup(L"\ud55c");replaceOnRelease=TRUE;terminate();
    check(svc.pComposition==&newer.iface&&Fsm_PeekPreedit(&svc.fsm)==L'\u3131',
          "external termination cannot clear a replacement syllable created during Release");
    ForgetComposition(&svc);
    for(int source=0;source<2;++source){
        setup(L"\ud55c");
        if(source)contextAddReentry=2;else compositionAddReentry=2;
        check(JamoComp_Finalize(&svc)==E_PENDING&&requests==0&&
              svc.pComposition==&newer.iface&&svc.pCompContext==&otherCtx&&
              svc.compFinalizePending&&svc.compBoundaryWritten&&comp.refs==0,
              source?"context retention reentry cannot finalize replacement":"composition retention reentry cannot finalize replacement");
        ForgetComposition(&svc);
    }
    for(int source=0;source<2;++source){
        setup(L"\ud55c");
        if(source)contextAddReentry=1;else compositionAddReentry=1;
        check(JamoComp_Finalize(&svc)==E_PENDING&&requests==0&&!svc.pComposition&&
              !svc.pCompContext&&!svc.compFinalizePending&&comp.refs==0,
              source?"context retention termination does not issue stale session":"composition retention termination cannot dereference removed context");
    }
    setup(L"");check(work(COMP_OP_UPDATE,0,L'\u314e')==S_OK,"write initial inline");
    check(wcscmp(document,L"\u314e")==0,"initial is in document");
    check(selStart==0&&selEnd==1&&style.fInterimChar&&style.ase==TF_AE_NONE,"CUAS one-character interim selection");
    check(parentProbe.enumCalls==1&&parentProbe.getCalls==1&&parentProbe.valueCalls==1&&
          parentProbe.parentQueries==0&&parent_refs_balanced(),
          "marked transitory extension preserves interim selection without querying parent interfaces");
    check(work(COMP_OP_UPDATE,0,L'\ud558')==S_OK&&wcscmp(document,L"\ud558")==0,"vowel updates same range");
    check(work(COMP_OP_UPDATE,L'\ud55c',L'\u3131')==S_OK,"commit prefix and new preedit");
    check(comp.start==1&&selStart==1&&selEnd==2&&style.fInterimChar,"only last character is interim");
    check(work(COMP_OP_UPDATE,0,L'\uae00')==S_OK&&wcscmp(document,L"\ud55c\uae00")==0,"replace only live syllable");
    check(JamoComp_CommitWithSpace(&svc,L'\uae00')==S_OK,"space transaction succeeds");
    check(wcscmp(document,L"\ud55c\uae00 ")==0&&!svc.pComposition,"space is after last syllable exactly once");
    check(selStart==3&&selEnd==3&&!style.fInterimChar,"finalization collapses interim selection");
    setup(L"\ud55c");staticFlags=0;work(COMP_OP_UPDATE,0,L'\ud558');
    check(selStart==1&&selEnd==1&&!style.fInterimChar&&style.ase==TF_AE_END,"native TSF retains collapsed caret");
    check(!parentProbe.documentCalls&&parent_refs_balanced(),"nontransitory native context needs no parent probe");
    setup(L"\ud55c");staticFlags=JAMO_TS_SS_TRANSITORY|TS_SS_NOHIDDENTEXT;parentProbe.present=FALSE;
    check(work(COMP_OP_UPDATE,0,L'\ud558')==S_OK&&wcscmp(document,L"\ud558")==0&&
          selStart==1&&selEnd==1&&!style.fInterimChar&&style.ase==TF_AE_END&&
          svc.pComposition==&comp.iface&&!comp.ended,
          "Chromium native transitory context without parent keeps composition and collapsed caret");
    check(parentProbe.enumCalls==1&&parentProbe.nextCalls==2&&!parentProbe.getCalls&&parent_refs_balanced(),
          "absent marker is not created by GetCompartment");
    check(work(COMP_OP_UPDATE,L'\ud55c',L'\u3131')==S_OK&&comp.start==1&&selStart==2&&selEnd==2&&
          !style.fInterimChar&&wcscmp(document,L"\ud55c\u3131")==0,
          "collapsed native caret preserves committed prefix and live range");
    check(work(COMP_OP_UPDATE,0,L'\uae00')==S_OK&&work(COMP_OP_UPDATE,0,L'\u3131')==S_OK&&
          wcscmp(document,L"\ud55c\u3131")==0&&selStart==2&&selEnd==2,
          "native caret supports syllable replacement and backspace-style reduction");
    check(JamoComp_CommitWithSpace(&svc,L'\u3131')==S_OK&&wcscmp(document,L"\ud55c\u3131 ")==0&&
          !svc.pComposition&&selStart==3&&selEnd==3&&!style.fInterimChar&&parent_refs_balanced(),
          "native caret finalization commits once with trailing space");
    const char *probeFailures[]={"GetStatus failure","GetStatus S_FALSE","GetDocumentMgr failure",
        "GetDocumentMgr null","compartment-manager QI failure","compartment-manager null",
        "EnumCompartments failure","EnumCompartments null","Next failure","Next S_FALSE",
        "GetCompartment failure","GetCompartment null","GetValue failure","GetValue S_FALSE",
        "GetValue empty","GetValue integer","GetValue null unknown","bounded endless enumeration"};
    for(int i=0;i<(int)(sizeof(probeFailures)/sizeof(probeFailures[0]));++i){
        setup(L"\ud55c");
        switch(i){
        case 0:parentProbe.statusHr=E_FAIL;break;case 1:parentProbe.statusHr=S_FALSE;break;
        case 2:parentProbe.documentHr=E_FAIL;break;case 3:parentProbe.nullDocument=TRUE;break;
        case 4:parentProbe.managerHr=E_NOINTERFACE;break;case 5:parentProbe.nullManager=TRUE;break;
        case 6:parentProbe.enumHr=E_FAIL;break;case 7:parentProbe.nullEnum=TRUE;break;
        case 8:parentProbe.nextHr=E_FAIL;break;case 9:parentProbe.nextHr=S_FALSE;break;
        case 10:parentProbe.compartmentHr=E_FAIL;break;case 11:parentProbe.nullCompartment=TRUE;break;
        case 12:parentProbe.valueHr=E_FAIL;break;case 13:parentProbe.valueHr=S_FALSE;break;
        case 14:parentProbe.valueType=VT_EMPTY;break;case 15:parentProbe.valueType=VT_I4;break;
        case 16:parentProbe.nullParent=TRUE;break;case 17:parentProbe.endlessEnum=TRUE;break;
        }
        HRESULT hr=work(COMP_OP_UPDATE,0,L'\ud558');
        char name[160];snprintf(name,sizeof(name),"%s keeps text, composition, and ordinary caret",probeFailures[i]);
        check(hr==S_OK&&wcscmp(document,L"\ud558")==0&&selStart==1&&selEnd==1&&
              !style.fInterimChar&&style.ase==TF_AE_END&&svc.pComposition==&comp.iface&&
              !comp.ended&&!cleared,parent_refs_balanced()?name:"parent probe leaked a COM reference");
        check(parent_refs_balanced(),"failed parent probe releases every acquired COM reference");
        if(i==17)check(parentProbe.nextCalls<=256&&!parentProbe.getCalls,"malformed enumeration has a fixed work bound");
    }
    setup(L"\ud55c");parentProbe.changeFocus=TRUE;
    selStart=selEnd=7;
    check(work(COMP_OP_UPDATE,0,L'\ud558')==S_OK&&writes==1&&wcscmp(document,L"\ud558")==0&&
          selStart==7&&selEnd==7&&parent_refs_balanced(),
          "post-write parent probe reentry skips new selection without retrying committed text");
    SetFocus(owner);
    setup(L"\ud55c");shortShift=1;check(FAILED(work(COMP_OP_UPDATE,0,L'\ud55c')),"partial interim-range movement fails visibly");
    setup(L"\ud55c");failSelection=1;check(FAILED(JamoComp_Finalize(&svc)),"selection failure is not success");
    check(svc.pComposition==&comp.iface&&comp.ended==0&&cleared==0,"failed finalization preserves unfinished state");
    check(!JamoComp_PrepareInput(&svc,&otherCtx),"failed old-context finalization blocks cross-context reuse");
    setup(L"\ud55c");failSelection=1;
    check(FAILED(JamoComp_CommitWithSpace(&svc,L'\ud55c')),"space finalization error propagates");
    check(wcscmp(document,L"\ud55c ")==0&&svc.compBoundaryWritten,"written space retained on finalization failure");
    int oldWrites=writes;FsmResult update={0,L'\u314e',true};
    check(FAILED(JamoComp_Apply(&svc,&ctx,update))&&writes==oldWrites,"new syllable cannot overwrite pending space");
    failSelection=0;check(JamoComp_PrepareInput(&svc,&ctx)&&writes==oldWrites&&cleared==1,"retry only finalizes without duplicating space");
    setup(L"\ud55c");requestMode=1;
    check(FAILED(JamoComp_CommitWithSpace(&svc,L'\ud55c'))&&requests==1&&!queued,"Space never queues asynchronous mutation");
    setup(L"\ud55c");requestMode=1;
    check(JamoComp_Finalize(&svc)==TF_S_ASYNC&&svc.compFinalizePending&&cleared==0,"queued finalization retains input state");
    check(!JamoComp_PrepareInput(&svc,&otherCtx),"queued finalization blocks new context");
    check(runQueued()==S_OK&&!svc.pComposition&&!svc.compFinalizePending&&cleared==1,"matching deferred completion clears state");
    setup(L"\ud55c");requestMode=1;JamoComp_Finalize(&svc);terminate();
    newer=(Composition){.iface={&comp_vtable},.refs=1,.end=1};svc.pComposition=&newer.iface;svc.pCompContext=&otherCtx;
    check(runQueued()==S_FALSE&&svc.pComposition==&newer.iface&&newer.ended==0&&cleared==0,"stale queued finalize cannot touch replacement composition");
    setup(L"\ud55c");terminateSelection=1;
    check(JamoComp_Finalize(&svc)==S_OK&&comp.refs==0,"host termination during selection is lifetime safe");
    setup(L"\ud55c");terminateShift=1;
    check(work(COMP_OP_UPDATE,L'\ud55c',L'\u3131')==S_OK&&comp.refs==0,"host termination during prefix movement is safe");
    setup(L"\ud55c");check(work(COMP_OP_CANCEL,0,0)==S_OK&&!document[0]&&!svc.pComposition,"cancel removes live preedit");
    setup(L"\ud55c");InlineEditSession wrong={.svc=&svc,.ctx=&otherCtx,.op=COMP_OP_UPDATE,.preedit=L'\u314e'};
    check(FAILED(DoInlineWork(&wrong,1))&&writes==0,"context edit cookies cannot cross documents");
    setup(L"\ud55c");svc.pCompFocusHwnd=(HWND)(ULONG_PTR)1;
    check(FAILED(JamoComp_Apply(&svc,&ctx,update))&&writes==0,
          "same-context composition never rewrites text in a different focus window");
    setup(L"\ud55c");svc.pCompFocusHwnd=(HWND)(ULONG_PTR)1;failSelection=1;
    check(!JamoComp_PrepareInput(&svc,&ctx),
          "same-context focus change cannot reuse an unfinished composition");
    setup(L"\ud55c");requestMode=1;JamoComp_Finalize(&svc);
    SetFocus(otherWindow);LONG beforeStart=selStart,beforeEnd=selEnd;
    check(runQueued()==E_PENDING&&comp.ended==0&&writes==0&&
          beforeStart==selStart&&beforeEnd==selEnd&&svc.pComposition==&comp.iface,
          "queued finalization does not alter the new control's selection");
    SetFocus(owner);requestMode=0;
    check(JamoComp_PrepareInput(&svc,&ctx)&&comp.ended==1&&writes==0,
          "returning to original control finalizes once without rewriting text");
    setup(L"\ud55c");SetFocus(otherWindow);JamoComp_Cancel(&svc);
    check(svc.pComposition==&comp.iface&&comp.ended==0&&writes==0,
          "unrelated Escape cannot discard or cancel the original inline composition");
    SetFocus(owner);setup(L"\ud55c");focusedContext=&otherCtx;SetFocus(otherWindow);
    check(JamoComp_PrepareInput(&svc,&otherCtx)&&!svc.pComposition&&comp.ended==1&&writes==0,
          "a distinct document finalizes after focus leaves without blocking the next document");
    selStart=selEnd=0;
    check(JamoComp_Apply(&svc,&otherCtx,update)==S_OK&&svc.pCompContext==&otherCtx&&
          svc.pCompFocusHwnd==otherWindow&&wcscmp(document,L"\ud55c")==0&&wcscmp(otherDocument,L"\u314e")==0,
          "Korean starts in the new distinct document while old text stays intact");
    JamoComp_Release(&svc);SetFocus(owner);
    setup(L"");svc.pComposition=NULL;svc.pCompContext=NULL;svc.pCompFocusHwnd=NULL;selStart=selEnd=0;
    requestMode=3;
    check(FAILED(JamoComp_Apply(&svc,&ctx,update))&&writes==0&&!svc.pComposition,
          "first composition cannot adopt focus changed during session request");
    SetFocus(owner);setup(L"");svc.pComposition=NULL;svc.pCompContext=NULL;svc.pCompFocusHwnd=NULL;selStart=selEnd=0;
    requestMode=4;
    check(FAILED(JamoComp_Apply(&svc,&ctx,update))&&writes==0&&!svc.pComposition&&comp.refs==0,
          "first composition cannot adopt focus changed during StartComposition");
    SetFocus(owner);setup(L"\ud55c");svc.pathKind=JAMO_PATH_STANDARD;requestMode=3;
    check(JamoComp_Apply(&svc,&ctx,update)==E_PENDING&&svc.pComposition==&comp.iface&&
          svc.pathKind==JAMO_PATH_STANDARD&&writes==0,
          "temporary owner mismatch does not demote a live inline composition to fallback");
    SetFocus(owner);requestMode=0;
    check(JamoComp_PrepareInput(&svc,&ctx)&&JamoComp_Apply(&svc,&ctx,update)==S_OK&&
          wcscmp(document,L"\u314e")==0,
          "returning to the original owner resumes inline updates after temporary focus reentry");
    JamoComp_Release(&svc);
    setup(L"\ud55c");bindOwner();ownerChangeOnRange=TRUE;
    check(JamoComp_Apply(&svc,&ctx,update)==E_PENDING&&writes==0&&wcscmp(document,L"\ud55c")==0,
          "logical owner change in GetRange blocks SetText within same HWND and context");
    ownerChangeOnRange=FALSE;JamoComp_Release(&svc);
    setup(L"\ud55c");bindOwner();requestMode=1;
    check(JamoComp_Finalize(&svc)==TF_S_ASYNC&&queued!=NULL,
          "logical-owner finalization regression really queues original composition");
    changeOwner();LONG ownerSelStart=selStart,ownerSelEnd=selEnd;
    check(runQueued()==E_PENDING&&comp.ended==0&&writes==0&&
          selStart==ownerSelStart&&selEnd==ownerSelEnd,
          "queued finalize cannot mutate selection or end composition for a changed logical owner");
    JamoComp_Release(&svc);
    setup(L"\ud55c");bindOwner();ownerChangeAfterWrite=TRUE;
    check(JamoComp_Apply(&svc,&ctx,update)==S_OK&&writes==1&&wcscmp(document,L"\u314e")==0,
          "successful inline write remains committed if owner changes inside SetText");
    ownerChangeAfterWrite=FALSE;
    check(!JamoComp_PrepareInput(&svc,&ctx)&&writes==1,
          "changed logical owner cannot replay an already successful inline write");
    JamoComp_Release(&svc);
    setup(L"");svc.pComposition=NULL;svc.pCompContext=NULL;svc.pCompFocusHwnd=NULL;selStart=selEnd=0;
    contextAddReentry=2;
    check(work(COMP_OP_UPDATE,0,L'\ud55c')==E_PENDING&&writes==0&&
          svc.pComposition==&newer.iface&&svc.pCompContext==&otherCtx&&
          svc.pCompFocusHwnd==otherWindow&&svc.compUpdatedOnce&&svc.compBoundaryWritten,
          "new composition context AddRef cannot stamp or write a reentrant replacement");
    ForgetComposition(&svc);
    SetFocus(owner);setup(L"\ud55c");bindOwner();observedOwner.deferredSafe=FALSE;
    RiumOwner_Capture(&svc.inputOwner,&svc.inlineOwner);RiumOwnerStamp originalText=observedOwner;
    observedOwner.kind=RIUM_OWNER_COMMAND;RiumOwner_Observe(&svc.inputOwner);
    check(!JamoComp_PrepareInput(&svc,&ctx)&&!writes&&!comp.ended&&!requests,
          "retirement never requests a shared-context session while COMMAND");
    observedOwner=originalText;RiumOwner_Observe(&svc.inputOwner);
    check(JamoComp_PrepareInput(&svc,&ctx)&&comp.ended==1&&!svc.pComposition&&
          !writes&&!retirementSelections&&wcscmp(document,L"\ud55c")==0,
          "return to identical DVA TEXT retires only old composition without caret or text writes");
    selStart=selEnd=1;
    check(JamoComp_Apply(&svc,&ctx,update)==S_OK&&wcscmp(document,L"\ud55c\u314e")==0&&
          svc.inlineOwner.epoch==svc.inputOwner.epoch,
          "retired original text remains while next Korean starts with fresh binding");
    if(svc.pComposition)ForgetComposition(&svc);
    for(int scenario=0;scenario<7;++scenario){
        setup(L"\ud55c");bindOwner();observedOwner.deferredSafe=FALSE;
        if(scenario==0)observedOwner.provider=2;
        if(scenario==1)observedOwner.lifetimeToken=0;
        RiumOwner_Capture(&svc.inputOwner,&svc.inlineOwner);originalText=observedOwner;
        observedOwner.kind=RIUM_OWNER_COMMAND;RiumOwner_Observe(&svc.inputOwner);
        observedOwner=originalText;if(scenario==2)++observedOwner.lifetimeToken;RiumOwner_Observe(&svc.inputOwner);
        if(scenario==3)requestMode=1;
        if(scenario==4)focusedContext=&otherCtx;
        if(scenario==5)retirementTopChange=1;
        if(scenario==6)retirementEndMode=1;
        check(!JamoComp_PrepareInput(&svc,&ctx)&&!writes&&!retirementSelections&&!comp.ended&&
              !queued&&svc.pComposition==&comp.iface,
              "unproven generation, other context, host reentry or failed synchronous retirement preserves old state");
        ForgetComposition(&svc);
    }
    setup(L"\ud55c");bindOwner();RiumOwner_Capture(&svc.inputOwner,&svc.inlineOwner);originalText=observedOwner;
    observedOwner.kind=RIUM_OWNER_COMMAND;RiumOwner_Observe(&svc.inputOwner);observedOwner=originalText;RiumOwner_Observe(&svc.inputOwner);
    retirementEndMode=2;
    check(!JamoComp_PrepareInput(&svc,&ctx)&&svc.pComposition==&newer.iface&&svc.pCompContext==&otherCtx&&
          !writes&&!retirementSelections&&newer.ended==0,
          "EndComposition reentrant replacement is neither cleared nor reused by old retirement");
    ForgetComposition(&svc);
    for(int phase=0;phase<3;++phase){
        SetFocus(owner);setup(L"\ud55c");bindOwner();RiumOwner_Capture(&svc.inputOwner,&svc.inlineOwner);originalText=observedOwner;
        observedOwner.kind=RIUM_OWNER_COMMAND;RiumOwner_Observe(&svc.inputOwner);observedOwner=originalText;RiumOwner_Observe(&svc.inputOwner);
        if(phase==0)compositionAddReentry=2;
        if(phase==1)contextAddReentry=2;
        if(phase==2)retirementEndMode=3;
        check(!JamoComp_PrepareInput(&svc,&ctx)&&!writes&&!retirementSelections&&
              (phase==2 ? (!svc.pComposition&&comp.ended==1) : (svc.pComposition==&newer.iface&&newer.ended==0)),
              "retirement retention and End callbacks preserve replacement or detach only confirmed-ended original");
        ForgetComposition(&svc);
    }
    SetFocus(owner);setup(L"");svc.pComposition=NULL;svc.pCompContext=NULL;svc.pCompFocusHwnd=NULL;selStart=selEnd=0;
    bindOwner();observedOwner.kind=RIUM_OWNER_UNKNOWN;observedOwner.logicalObject=0;
    observedOwner.lifetimeToken=0;observedOwner.textObject=0;
    check(JamoComp_Apply(&svc,&ctx,update)==E_PENDING&&!svc.pComposition&&!writes&&document[0]==0,
          "lost recognized ownership cannot originate an inline composition with an incomplete identity");
    observedOwner.kind=RIUM_OWNER_TEXT;observedOwner.logicalObject=0x200;
    observedOwner.lifetimeToken=1;observedOwner.textObject=0x300;
    check(JamoComp_PrepareInput(&svc,&ctx)&&JamoComp_Apply(&svc,&ctx,update)==S_OK&&
          svc.pComposition&&wcscmp(document,L"\u314e")==0,
          "fresh positive text starts normally without a retained unknown composition");
    ForgetComposition(&svc);
    // Live preview.6: TEXT ended on blur, COMMAND set a boundary without a
    // remaining composition, and the first new consonant inherited that flag.
    // The following vowel then finalized the NEW consonant instead of joining it.
    SetFocus(owner);setup(L"\ud55c");bindOwner();originalText=observedOwner;
    ForgetComposition(&svc);Fsm_Init(&svc.fsm);selStart=selEnd=1;
    observedOwner.kind=RIUM_OWNER_COMMAND;RiumOwner_Observe(&svc.inputOwner);
    svc.inputOwnerBoundary=TRUE;
    observedOwner=originalText;RiumOwner_Observe(&svc.inputOwner);
    for(const wchar_t *key=L"rmf";*key;++key) {
        check(JamoComp_PrepareInput(&svc,&ctx),"returning TEXT prepares the next physical Korean key");
        FsmResult result=Fsm_ProcessKey(&svc.fsm,*key,0,NULL);
        check(JamoComp_Apply(&svc,&ctx,result)==S_OK,"returning TEXT applies the current Korean syllable");
    }
    check(wcscmp(document,L"\ud55c\uae00")==0&&svc.fsm.state==STATE_CHO_JUNG_JONG&&
          svc.compUpdatedOnce&&!svc.inputOwnerBoundary&&!cleared,
          "command without a retained composition cannot split the first returning syllable");
    ForgetComposition(&svc);
    SetFocus(owner);DestroyWindow(otherWindow);
    DestroyWindow(owner);
    printf("Inline composition: %d checks, %d failures.\n",checks,failures);return failures?1:0;
}
