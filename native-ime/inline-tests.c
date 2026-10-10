// Exercise the actual edit-session implementation, including host callbacks and locks.
#include <initguid.h>
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
typedef struct { ITfRange iface; LONG start, end; } Range;
typedef struct { ITfComposition iface; LONG refs, start, end; int ended; } Composition;
static JamotongTextService svc;
static Composition comp, newer;
static ITfContext ctx, otherCtx;
static wchar_t document[64];
static LONG selStart, selEnd;
static TF_SELECTIONSTYLE style;
static DWORD staticFlags;
static int failSelection, shortShift, terminateSelection, terminateShift, writes, requestMode, requests;
static ITfEditSession *queued;
static ULONG STDMETHODCALLTYPE tip_ref(ITfTextInputProcessor *p) { (void)p; return 1; }
static JamoTIPExVtbl tip_vtable={.AddRef=tip_ref,.Release=tip_ref};
static ULONG STDMETHODCALLTYPE ctx_ref(ITfContext *p) { (void)p; return 1; }
static void terminate(void) {
    if(svc.pComposition) CS_OnCompositionTerminated((ITfCompositionSink*)&svc.lpVtblCompSink,1,svc.pComposition);
}
static ULONG STDMETHODCALLTYPE range_release(ITfRange *p) { free(p); return 0; }
static ITfRange *makeRange(LONG start,LONG end);
static HRESULT STDMETHODCALLTYPE range_clone(ITfRange *p,ITfRange **out) {
    Range *r=(Range*)p; *out=makeRange(r->start,r->end); return S_OK;
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
    LONG oldEnd=r->end, tail=(LONG)wcslen(document)-oldEnd;
    memmove(document+r->start+len,document+oldEnd,(size_t)(tail+1)*sizeof(wchar_t));
    memcpy(document+r->start,text,(size_t)len*sizeof(wchar_t)); r->end=r->start+len;
    if(svc.pComposition)((Composition*)svc.pComposition)->end=r->end; return S_OK;
}
static ITfRangeVtbl range_vtable={.Release=range_release,.Clone=range_clone,
    .Collapse=range_collapse,.ShiftStart=range_shift,.SetText=range_text};
static ITfRange *makeRange(LONG start,LONG end) {
    Range *r=calloc(1,sizeof(*r));r->iface.lpVtbl=&range_vtable;r->start=start;r->end=end;return &r->iface;
}
static ULONG STDMETHODCALLTYPE comp_addref(ITfComposition *p) { return ++((Composition*)p)->refs; }
static ULONG STDMETHODCALLTYPE comp_release(ITfComposition *p) { return --((Composition*)p)->refs; }
static HRESULT STDMETHODCALLTYPE comp_range(ITfComposition *p,ITfRange **out) {
    Composition *c=(Composition*)p; check(c->refs>0,"composition remains alive during host call");
    *out=makeRange(c->start,c->end);return S_OK;
}
static HRESULT STDMETHODCALLTYPE comp_shift(ITfComposition *p,TfEditCookie ec,ITfRange *range) {
    (void)ec;Composition *c=(Composition*)p;check(c->refs>0,"prefix never uses released composition");
    c->start=((Range*)range)->start;return S_OK;
}
static HRESULT STDMETHODCALLTYPE comp_end(ITfComposition *p,TfEditCookie ec) {
    (void)ec;Composition *c=(Composition*)p;check(c->refs>0,"end never uses released composition");++c->ended;return S_OK;
}
static ITfCompositionVtbl comp_vtable={.AddRef=comp_addref,.Release=comp_release,
    .GetRange=comp_range,.ShiftStart=comp_shift,.EndComposition=comp_end};
static HRESULT STDMETHODCALLTYPE ctx_status(ITfContext *p,TF_STATUS *status) {
    (void)p;ZeroMemory(status,sizeof(*status));status->dwStaticFlags=staticFlags;return S_OK;
}
static HRESULT STDMETHODCALLTYPE ctx_selection(ITfContext *p,TfEditCookie ec,ULONG count,const TF_SELECTION *sel) {
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
    *result=es->lpVtbl->DoEditSession(es,1);return S_OK;
}
static ITfContextVtbl ctx_vtable={.AddRef=ctx_ref,.Release=ctx_ref,.GetStatus=ctx_status,
    .SetSelection=ctx_selection,.GetSelection=ctx_get_selection,.RequestEditSession=ctx_request};
static void setup(const wchar_t *text) {
    ZeroMemory(&svc,sizeof(svc));JamoComp_Init(&svc);svc.lpVtblTIP=&tip_vtable;
    ctx.lpVtbl=&ctx_vtable;otherCtx.lpVtbl=&ctx_vtable;
    comp=(Composition){.iface={&comp_vtable},.refs=1,.end=(LONG)wcslen(text)};
    svc.pComposition=&comp.iface;svc.pCompContext=&ctx;wcscpy(document,text);
    staticFlags=JAMO_TS_SS_TRANSITORY;failSelection=shortShift=terminateSelection=terminateShift=0;
    writes=requestMode=requests=cleared=0;queued=NULL;
}
static HRESULT work(CompOp op,wchar_t commit,wchar_t preedit) {
    InlineEditSession es={.svc=&svc,.ctx=&ctx,.op=op,.commit=commit,.preedit=preedit};
    return DoInlineWork(&es,1);
}
static HRESULT runQueued(void) {
    ITfEditSession *es=queued;queued=NULL;
    HRESULT hr=es->lpVtbl->DoEditSession(es,1);es->lpVtbl->Release(es);return hr;
}
int main(void) {
    setup(L"");check(work(COMP_OP_UPDATE,0,L'\u314e')==S_OK,"write initial inline");
    check(wcscmp(document,L"\u314e")==0,"initial is in document");
    check(selStart==0&&selEnd==1&&style.fInterimChar&&style.ase==TF_AE_NONE,"CUAS one-character interim selection");
    check(work(COMP_OP_UPDATE,0,L'\ud558')==S_OK&&wcscmp(document,L"\ud558")==0,"vowel updates same range");
    check(work(COMP_OP_UPDATE,L'\ud55c',L'\u3131')==S_OK,"commit prefix and new preedit");
    check(comp.start==1&&selStart==1&&selEnd==2&&style.fInterimChar,"only last character is interim");
    check(work(COMP_OP_UPDATE,0,L'\uae00')==S_OK&&wcscmp(document,L"\ud55c\uae00")==0,"replace only live syllable");
    check(JamoComp_CommitWithSpace(&svc,L'\uae00')==S_OK,"space transaction succeeds");
    check(wcscmp(document,L"\ud55c\uae00 ")==0&&!svc.pComposition,"space is after last syllable exactly once");
    check(selStart==3&&selEnd==3&&!style.fInterimChar,"finalization collapses interim selection");
    setup(L"\ud55c");staticFlags=0;work(COMP_OP_UPDATE,0,L'\ud558');
    check(selStart==1&&selEnd==1&&!style.fInterimChar&&style.ase==TF_AE_END,"native TSF retains collapsed caret");
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
    printf("Inline composition: %d checks, %d failures.\n",checks,failures);return failures?1:0;
}
