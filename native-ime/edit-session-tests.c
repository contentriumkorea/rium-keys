// Run the production edit-session request and callback against a controlled host.
#include <initguid.h>
#include "third_party/jamotong/src/edit_session.h"
static HWND fixtureFocus;
static HWND FixtureGetFocus(void) { return fixtureFocus; }
static HWND FixtureSetFocus(HWND hwnd) { HWND old=fixtureFocus;fixtureFocus=hwnd;return old; }
#define RIUM_FOCUS_CALL(first, ...) first
#define GetFocus(...) RIUM_FOCUS_CALL(__VA_OPT__(GetFocus,) FixtureGetFocus)(__VA_ARGS__)
#define SetFocus FixtureSetFocus
static LRESULT FixtureSendMessage(HWND,UINT,WPARAM,LPARAM);
#define SendMessageW FixtureSendMessage
#include "input-owner.c"
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
static RiumOwnerStamp observedOwner;
static BOOL ownerChangeOnQuery, ownerChangeAfterInsert;
static int nativeGets,nativeWrites;
static BOOL nativeChangeAfterRead;
static void ownerReader(void *unused,RiumOwnerStamp *out) { (void)unused;*out=observedOwner; }
static void changeOwner(void) { ++observedOwner.logicalObject;++observedOwner.lifetimeToken; }
static LRESULT FixtureSendMessage(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    (void)hwnd;(void)wp;
    if(msg==EM_EXGETSEL){CHARRANGE *range=(CHARRANGE*)lp;range->cpMin=range->cpMax=0;
        ++nativeGets;if(nativeChangeAfterRead&&nativeGets==2)changeOwner();return 0;}
    if(msg==EM_REPLACESEL){++nativeWrites;return 0;}
    abort();
}
static void bindOwner(void) {
    observedOwner=(RiumOwnerStamp){.provider=1,.profile=1,.kind=RIUM_OWNER_TEXT,.deferredSafe=TRUE,
        .processId=GetCurrentProcessId(),.threadId=GetCurrentThreadId(),.focus=GetFocus(),
        .windowObject=0x100,.logicalObject=0x200,.lifetimeToken=1,.textObject=0x300};
    svc.inputOwner.reader=ownerReader;
    RiumOwner_Capture(&svc.inputOwner,&svc.pendingOwner);
}
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
    if(mode>=7||ownerChangeAfterInsert){resultRange.lpVtbl=&rangeVtbl;*r=&resultRange;}
    if(mode==7)SetFocus(redirected);
    if(ownerChangeAfterInsert)changeOwner();
    return S_OK;
}
static ITfInsertAtSelectionVtbl insertVtbl={.AddRef=insert_ref,.Release=insert_ref,.InsertTextAtSelection=insert_text};
static ITfInsertAtSelection insert={&insertVtbl};
static HRESULT STDMETHODCALLTYPE ctx_qi(ITfContext *p,REFIID iid,void **out) {
    (void)p;*out=NULL;
    if(IsEqualIID(iid,&IID_ITfInsertAtSelection)){if(mode==6)SetFocus(redirected);if(ownerChangeOnQuery)changeOwner();*out=&insert;return S_OK;}
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
    serviceRefs=contextRefs=1;queued=NULL;inserts=selections=0;mode=0;ownerChangeOnQuery=ownerChangeAfterInsert=FALSE;
    nativeGets=nativeWrites=0;nativeChangeAfterRead=FALSE;
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
    HWND owner=CreateWindowExW(0,L"STATIC",L"RIUM pending owner",0,0,0,1,1,HWND_MESSAGE,NULL,GetModuleHandleW(NULL),NULL);
    redirected=CreateWindowExW(0,L"STATIC",L"RIUM other target",0,0,0,1,1,HWND_MESSAGE,NULL,GetModuleHandleW(NULL),NULL);
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
    SetFocus(owner);setup();bindOwner();ownerChangeOnQuery=TRUE;
    check(RequestEditSessionData(&svc,&ctx,&data)==E_PENDING&&inserts==0,
          "logical owner change during QI blocks insertion despite same HWND and context");
    setup();bindOwner();mode=2;
    check(RequestEditSessionDataEx(&svc,&ctx,&data,TF_ES_ASYNCDONTCARE|TF_ES_READWRITE)==TF_S_ASYNC,
          "logical-owner regression actually queues a captured edit session");
    changeOwner();mode=0;
    check(complete()==E_PENDING&&inserts==0&&serviceRefs==1&&contextRefs==1,
          "queued insertion cannot retarget another logical owner within same HWND");
    setup();bindOwner();ownerChangeAfterInsert=TRUE;
    check(RequestEditSessionData(&svc,&ctx,&data)==S_OK&&inserts==1&&selections==0,
          "successful insert followed by logical owner change stays committed and skips caret mutation");
    setup();bindOwner();nativeChangeAfterRead=TRUE;
    check(EditCtl_ReplaceSelectionOwned(owner,L"\ud55c",&svc,&svc.pendingOwner,FALSE)&&nativeWrites==1,
          "native post-write selection probe owner change cannot retry an already delivered edit");
    DestroyWindow(redirected);DestroyWindow(owner);
    printf("Edit sessions: %d checks, %d failures.\n",checks,failures);return failures?1:0;
}
