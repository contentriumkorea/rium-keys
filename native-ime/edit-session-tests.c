// Run the production edit-session request and callback against a controlled host.
#include <initguid.h>
#include "third_party/jamotong/src/edit_session.c"
#include <stdio.h>
#include <stdlib.h>

// Candidate UI is outside this test. An accidental call is a test error.
bool CandidateUI_IsVisible(void) { abort(); }
void CandidateUI_Cancel(void) { abort(); }

static int checks, failures, mode, inserts;
static int selections;
static LONG serviceRefs, contextRefs;
static ITfEditSession *queued;
static JamotongTextService svc;
static ITfContext ctx;
static HWND redirected;
static void check(int ok, const char *name) {
    ++checks; if (!ok) { ++failures; printf("FAIL: %s\n", name); }
}
static ULONG STDMETHODCALLTYPE tip_add(ITfTextInputProcessor *p) { (void)p; return ++serviceRefs; }
static ULONG STDMETHODCALLTYPE tip_release(ITfTextInputProcessor *p) { (void)p; return --serviceRefs; }
static JamoTIPExVtbl tipVtbl={.AddRef=tip_add,.Release=tip_release};
static ULONG STDMETHODCALLTYPE ctx_add(ITfContext *p) { (void)p; return ++contextRefs; }
static ULONG STDMETHODCALLTYPE ctx_release(ITfContext *p) { (void)p; return --contextRefs; }
static ULONG STDMETHODCALLTYPE insert_ref(ITfInsertAtSelection *p) { (void)p; return 1; }
static ITfRange resultRange;
static ULONG STDMETHODCALLTYPE range_ref(ITfRange *p) { (void)p;return 1; }
static HRESULT STDMETHODCALLTYPE range_clone(ITfRange *p,ITfRange **out) {
    (void)p;if(mode==8)SetFocus(redirected);*out=&resultRange;return S_OK;
}
static HRESULT STDMETHODCALLTYPE range_collapse(ITfRange *p,TfEditCookie ec,TfAnchor a) {
    (void)p;(void)ec;(void)a;if(mode==9)SetFocus(redirected);return S_OK;
}
static ITfRangeVtbl rangeVtbl={.AddRef=range_ref,.Release=range_ref,.Clone=range_clone,.Collapse=range_collapse};
static HRESULT STDMETHODCALLTYPE insert_text(ITfInsertAtSelection *p,TfEditCookie ec,DWORD flags,
                                            const WCHAR *s,LONG n,ITfRange **r) {
    (void)p;(void)ec;(void)flags;(void)s;(void)n;*r=NULL;
    if(mode==3)return E_ACCESSDENIED;
    ++inserts;
    if(mode>=7){resultRange.lpVtbl=&rangeVtbl;*r=&resultRange;}
    if(mode==7)SetFocus(redirected);
    return S_OK;
}
static ITfInsertAtSelectionVtbl insertVtbl={.AddRef=insert_ref,.Release=insert_ref,.InsertTextAtSelection=insert_text};
static ITfInsertAtSelection insert={&insertVtbl};
static HRESULT STDMETHODCALLTYPE ctx_qi(ITfContext *p,REFIID iid,void **out) {
    (void)p;*out=NULL;
    if(IsEqualIID(iid,&IID_ITfInsertAtSelection)){if(mode==6)SetFocus(redirected);*out=&insert;return S_OK;}
    return E_NOINTERFACE;
}
static HRESULT STDMETHODCALLTYPE ctx_selection(ITfContext *p,TfEditCookie ec,ULONG index,ULONG count,
                                               TF_SELECTION *s,ULONG *fetched) {
    (void)p;(void)ec;(void)index;(void)count;(void)s;*fetched=0;return E_FAIL;
}
static HRESULT STDMETHODCALLTYPE ctx_set_selection(ITfContext *p,TfEditCookie ec,ULONG count,const TF_SELECTION *s) {
    (void)p;(void)ec;(void)count;(void)s;++selections;return S_OK;
}
static HRESULT STDMETHODCALLTYPE ctx_request(ITfContext *p,TfClientId client,ITfEditSession *es,
                                             DWORD flags,HRESULT *result) {
    (void)p;(void)client;(void)flags;
    if(mode==1){*result=E_ACCESSDENIED;return S_OK;}
    if(mode==2){queued=es;es->lpVtbl->AddRef(es);*result=TF_S_ASYNC;return S_OK;}
    if(mode==4)return E_OUTOFMEMORY;
    if(mode==5)SetFocus(redirected);
    *result=es->lpVtbl->DoEditSession(es,1);return S_OK;
}
static ITfContextVtbl ctxVtbl={.QueryInterface=ctx_qi,.AddRef=ctx_add,.Release=ctx_release,
    .GetSelection=ctx_selection,.SetSelection=ctx_set_selection,.RequestEditSession=ctx_request};
static void setup(void) {
    ZeroMemory(&svc,sizeof(svc));svc.lpVtblTIP=&tipVtbl;ctx.lpVtbl=&ctxVtbl;
    serviceRefs=contextRefs=1;queued=NULL;inserts=selections=0;mode=0;
}
static HRESULT complete(void) {
    ITfEditSession *es=queued;queued=NULL;
    HRESULT hr=es->lpVtbl->DoEditSession(es,1);es->lpVtbl->Release(es);return hr;
}
int main(void) {
    EditSessionData data={.committed=L"\ud55c"};
    setup();mode=1;
    check(RequestEditSessionDataEx(&svc,&ctx,&data,TF_ES_ASYNCDONTCARE|TF_ES_READWRITE)==E_ACCESSDENIED,
          "ASYNCDONTCARE synchronous host failure is returned to the caller");
    check(inserts==0&&serviceRefs==1&&contextRefs==1,"rejected request releases its ownership");
    setup();mode=3;
    check(RequestEditSessionDataEx(&svc,&ctx,&data,TF_ES_ASYNCDONTCARE|TF_ES_READWRITE)==E_ACCESSDENIED,
          "ASYNCDONTCARE synchronous insertion failure is not reported as committed");
    setup();mode=2;
    check(RequestEditSessionDataEx(&svc,&ctx,&data,TF_ES_ASYNCDONTCARE|TF_ES_READWRITE)==TF_S_ASYNC,
          "queued work is distinguished from completed insertion");
    check(inserts==0&&serviceRefs==2&&contextRefs==2,"queued session owns its service and context");
    mode=0;check(complete()==S_OK&&inserts==1&&serviceRefs==1&&contextRefs==1,
          "deferred completion inserts once and releases its ownership");
    setup();mode=4;
    check(RequestEditSessionDataEx(&svc,&ctx,&data,TF_ES_ASYNCDONTCARE|TF_ES_READWRITE)==E_OUTOFMEMORY,
          "outer request failure takes precedence over session output");
    HWND owner=CreateWindowExW(0,L"STATIC",L"RIUM pending owner",WS_POPUP,0,0,1,1,NULL,NULL,GetModuleHandleW(NULL),NULL);
    redirected=CreateWindowExW(0,L"STATIC",L"RIUM other target",WS_POPUP,0,0,1,1,NULL,NULL,GetModuleHandleW(NULL),NULL);
    SetFocus(owner);check(owner&&redirected&&GetFocus()==owner,"pending test owns isolated focus");
    data.bindFocus=TRUE;data.focusOwner=owner;
    setup();mode=5;
    check(RequestEditSessionData(&svc,&ctx,&data)==E_PENDING&&inserts==0,
          "host focus change before callback cannot retarget pending insertion");
    SetFocus(owner);setup();mode=6;
    check(RequestEditSessionData(&svc,&ctx,&data)==E_PENDING&&inserts==0,
          "host interface query cannot retarget pending insertion");
    SetFocus(owner);setup();
    check(RequestEditSessionData(&svc,&ctx,&data)==S_OK&&inserts==1,
          "original owner still receives exactly one synchronous insertion");
    data.focusOwner=NULL;setup();
    check(RequestEditSessionData(&svc,&ctx,&data)==E_PENDING&&inserts==0,
          "bound insertion with unknown owner never writes");
    data.focusOwner=owner;
    for(int phase=7;phase<=9;++phase){
        SetFocus(owner);setup();mode=phase;
        check(RequestEditSessionData(&svc,&ctx,&data)==S_OK&&inserts==1&&selections==0,
              "post-insert focus change skips caret relocation without retrying successful text");
    }
    DestroyWindow(redirected);DestroyWindow(owner);
    printf("Edit sessions: %d checks, %d failures.\n",checks,failures);return failures?1:0;
}
