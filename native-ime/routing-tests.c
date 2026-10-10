// Contract tests for the actual DLL key sink. Not an OS activation/UI test.
// The real Windows compartment manager supplies context values; a tiny context
// facade injects GetStatus failures/read-only state that applications can expose.
#include <initguid.h>
#include "jamotong.h"
#include <stdio.h>
#include <stddef.h>

typedef HRESULT (WINAPI *FactoryFn)(REFCLSID, REFIID, void**);
typedef HRESULT (WINAPI *UnloadFn)(void);
static int checks, failures;
static int restrictionFailure;
static ITfKeyEventSink *nestedSink;
static ITfContext *nestedContext;
static JamotongTextService *nestedService;
static int reenterPending, statusReads, redirectStatusAt;
static int pendingMode, pendingWrites;
static wchar_t pendingWritten;
static ITfInsertAtSelection pendingInsert;
static HWND redirectStatusWindow;
static HRESULT STDMETHODCALLTYPE bad_part_qi(ITfCompartment *self, REFIID iid, void **out) { (void)self;(void)iid;*out=NULL;return E_NOINTERFACE; }
static ULONG STDMETHODCALLTYPE bad_part_ref(ITfCompartment *self) { (void)self;return 1; }
static HRESULT STDMETHODCALLTYPE bad_value(ITfCompartment *self, VARIANT *out) {
    (void)self;VariantInit(out);
    if(restrictionFailure==3)return E_FAIL;
    out->vt=VT_BSTR;out->bstrVal=SysAllocString(L"invalid restriction");return S_OK;
}
static ITfCompartmentVtbl bad_part_vtable={.QueryInterface=bad_part_qi,.AddRef=bad_part_ref,.Release=bad_part_ref,.GetValue=bad_value};
static ITfCompartment bad_part={&bad_part_vtable};
static HRESULT STDMETHODCALLTYPE bad_mgr_qi(ITfCompartmentMgr *self, REFIID iid, void **out) { (void)self;(void)iid;*out=NULL;return E_NOINTERFACE; }
static ULONG STDMETHODCALLTYPE bad_mgr_ref(ITfCompartmentMgr *self) { (void)self;return 1; }
static HRESULT STDMETHODCALLTYPE bad_compartment(ITfCompartmentMgr *self, REFGUID id, ITfCompartment **out) {
    (void)self;(void)id;*out=NULL;if(restrictionFailure==2)return E_FAIL;*out=&bad_part;return S_OK;
}
static ITfCompartmentMgrVtbl bad_mgr_vtable={.QueryInterface=bad_mgr_qi,.AddRef=bad_mgr_ref,.Release=bad_mgr_ref,.GetCompartment=bad_compartment};
static ITfCompartmentMgr bad_mgr={&bad_mgr_vtable};
static void check(int ok, const char *name) {
    ++checks;
    if (!ok) { ++failures; printf("FAIL: %s\n", name); }
}
typedef struct {
    ITfContext iface;
    ITfCompartmentMgr *compartments;
    TF_STATUS status;
    HRESULT statusResult;
} Context;
static HRESULT STDMETHODCALLTYPE context_qi(ITfContext *self, REFIID iid, void **out) {
    Context *ctx = (Context*)self;
    *out = NULL;
    if (pendingMode && IsEqualIID(iid, &IID_ITfInsertAtSelection)) {
        *out=&pendingInsert;return S_OK;
    }
    if (IsEqualIID(iid, &IID_ITfCompartmentMgr)) {
        if(restrictionFailure==1)return E_NOINTERFACE;
        if(restrictionFailure>1){*out=&bad_mgr;return S_OK;}
        *out = ctx->compartments;
        ctx->compartments->lpVtbl->AddRef(ctx->compartments);
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE context_ref(ITfContext *self) { (void)self; return 1; }
static HRESULT STDMETHODCALLTYPE context_status(ITfContext *self, TF_STATUS *status) {
    Context *ctx = (Context*)self;
    *status = ctx->status;
    if (++statusReads == redirectStatusAt) SetFocus(redirectStatusWindow);
    return ctx->statusResult;
}
static int pendingRequests;
static ULONG STDMETHODCALLTYPE pending_insert_ref(ITfInsertAtSelection *p) { (void)p;return 1; }
static HRESULT STDMETHODCALLTYPE pending_insert(ITfInsertAtSelection *p,TfEditCookie ec,DWORD flags,
                                               const WCHAR *text,LONG count,ITfRange **range) {
    (void)p;(void)ec;(void)flags;*range=NULL;
    if(count!=1)return E_INVALIDARG;
    ++pendingWrites;pendingWritten=text[0];
    if(pendingMode==2){
        BOOL eaten=FALSE;
        nestedSink->lpVtbl->OnKeyDown(nestedSink,nestedContext,VK_ESCAPE,1,&eaten);
        // A new slot may be created during a host callback after cancellation.
        nestedService->cpPendingCommit=L'\uae00';
        nestedService->cpPendingCtx=nestedContext;
        nestedService->cpPendingFocusHwnd=GetFocus();
        ++nestedService->cpPendingGeneration;
    }
    return S_OK;
}
static ITfInsertAtSelectionVtbl pending_insert_vtable={.AddRef=pending_insert_ref,.Release=pending_insert_ref,
    .InsertTextAtSelection=pending_insert};
static HRESULT STDMETHODCALLTYPE no_selection(ITfContext *p,TfEditCookie ec,ULONG index,ULONG count,
                                               TF_SELECTION *selection,ULONG *fetched) {
    (void)p;(void)ec;(void)index;(void)count;(void)selection;*fetched=0;return E_FAIL;
}
static void fixture_owner(void *context, RiumOwnerStamp *out) {
    *out=*(RiumOwnerStamp*)context;
}
static HRESULT STDMETHODCALLTYPE reject_session(ITfContext *p,TfClientId id,ITfEditSession *es,DWORD flags,HRESULT *result) {
    (void)p;(void)id;(void)es;(void)flags;++pendingRequests;
    if(reenterPending){
        reenterPending=0;BOOL eaten=FALSE;
        nestedSink->lpVtbl->OnTestKeyDown(nestedSink,nestedContext,'V',0x002f0001,&eaten);
    }
    *result=pendingMode ? es->lpVtbl->DoEditSession(es,1) : E_ACCESSDENIED;
    return S_OK;
}
static ITfContextVtbl context_vtable = {
    .QueryInterface = context_qi, .AddRef = context_ref, .Release = context_ref,
    .GetStatus = context_status, .RequestEditSession = reject_session, .GetSelection = no_selection
};
static void set_flag(ITfCompartmentMgr *mgr, TfClientId client, const GUID *id, LONG value) {
    ITfCompartment *part = NULL;
    HRESULT hr = mgr->lpVtbl->GetCompartment(mgr, id, &part);
    if (FAILED(hr) || !part) { check(0, "obtain Windows compartment"); return; }
    VARIANT v; VariantInit(&v); v.vt = VT_I4; v.lVal = value;
    check(SUCCEEDED(part->lpVtbl->SetValue(part, client, &v)), "write Windows compartment");
    part->lpVtbl->Release(part);
}
static int wants(ITfKeyEventSink *sink, ITfContext *ctx) {
    BOOL eaten = FALSE;
    HRESULT hr = sink->lpVtbl->OnTestKeyDown(sink, ctx, 'V', 0x002f0001, &eaten);
    check(SUCCEEDED(hr), "preview call succeeds");
    return eaten;
}
int wmain(int argc, wchar_t **argv) {
    if (argc < 2 || argc > 3) { puts("usage: RoutingTests.exe <absolute DLL path> [--manual-activation]"); return 2; }
    if (FAILED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED))) return 2;
    HWND owner=CreateWindowExW(0,L"STATIC",L"RIUM routing test owner",WS_POPUP,0,0,1,1,NULL,NULL,GetModuleHandleW(NULL),NULL);
    SetFocus(owner);
    check(owner&&GetFocus()==owner,"isolated routing test owns its focus");
    // Isolate upstream config reads/creation to this executable's own output directory.
    wchar_t own[MAX_PATH]; GetModuleFileNameW(NULL, own, MAX_PATH);
    wchar_t *slash = wcsrchr(own, L'\\'); if (!slash) return 2; *slash = 0;
    wcscat(own, L"\\fixture-appdata"); CreateDirectoryW(own, NULL);
    SetEnvironmentVariableW(L"APPDATA", own);
    HMODULE dll = LoadLibraryExW(argv[1], NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!dll) { printf("DLL load failed: %lu\n", GetLastError()); return 2; }
    FactoryFn factoryFn = (FactoryFn)(void*)GetProcAddress(dll, "DllGetClassObject");
    UnloadFn unloadFn = (UnloadFn)(void*)GetProcAddress(dll, "DllCanUnloadNow");
    IClassFactory *factory = NULL; ITfTextInputProcessor *tip = NULL; ITfKeyEventSink *sink = NULL;
    HRESULT hr = factoryFn(&CLSID_JamotongIME, &IID_IClassFactory, (void**)&factory);
    if (FAILED(hr)) return 2;
    hr = factory->lpVtbl->CreateInstance(factory, NULL, &IID_ITfTextInputProcessor, (void**)&tip);
    if (FAILED(hr)) return 2;
    tip->lpVtbl->QueryInterface(tip, &IID_ITfKeyEventSink, (void**)&sink);
    JamotongTextService *service = (JamotongTextService*)tip;
    check(service->config.layouts[service->config.currentLayoutIndex].type == LAYOUT_TYPE_KOREAN_FSM,
          "a newly selected RIUM input method starts in Korean mode");
    check(!service->config.options.useUiHelper, "RIUM does not launch the upstream helper");
    check(service->config.shortcuts[SC_FN_SETTINGS].count == 0, "RIUM leaves the settings shortcut to applications");
    check(service->config.shortcuts[SC_FN_CODE].count == 0, "RIUM leaves the code shortcut to applications");
    check(service->config.shortcuts[SC_FN_PASSTHROUGH].count == 0, "RIUM leaves the bypass shortcut to applications");
    check(service->config.shortcuts[SC_FN_ROTATE].count == 2,
          "only Hangul and right Alt switch input; Shift Space belongs to applications");
    service->config.options.useUiHelper = false;
    service->config.options.showPreview = false;
    int korean = -1;
    for (int i = 0; i < service->config.layoutCount; ++i)
        if (service->config.layouts[i].type == LAYOUT_TYPE_KOREAN_FSM &&
            service->config.layouts[i].kbdVariant == 0) { korean = i; break; }
    if (korean < 0 || !sink) return 2;
    service->config.currentLayoutIndex = korean;

    ITfThreadMgr *thread = NULL; ITfCompartmentMgr *mgr = NULL; TfClientId client = 0;
    hr = CoCreateInstance(&CLSID_TF_ThreadMgr, NULL, CLSCTX_INPROC_SERVER, &IID_ITfThreadMgr, (void**)&thread);
    if (FAILED(hr) || FAILED(thread->lpVtbl->Activate(thread, &client))) return 2;
    thread->lpVtbl->QueryInterface(thread, &IID_ITfCompartmentMgr, (void**)&mgr);
    Context ctx = { .iface = { &context_vtable }, .compartments = mgr, .statusResult = S_OK };
    check(wants(sink, &ctx.iface), "editable context retains Korean letter");
    RiumOwnerStamp logicalOwner={.provider=91,.profile=1,.kind=RIUM_OWNER_TEXT,
        .processId=GetCurrentProcessId(),.threadId=GetCurrentThreadId(),.focus=owner,
        .windowObject=100,.logicalObject=200,.lifetimeToken=301,.textObject=400};
    service->inputOwner.reader=fixture_owner;
    service->inputOwner.readerContext=&logicalOwner;
    check(wants(sink,&ctx.iface),"verified text target retains the Korean key");
    logicalOwner.kind=RIUM_OWNER_COMMAND;logicalOwner.logicalObject=201;logicalOwner.lifetimeToken=302;
    pendingRequests=0;
    check(!wants(sink,&ctx.iface),"same HWND and context passes first V at a verified command target");
    BOOL routed=TRUE;
    check(SUCCEEDED(sink->lpVtbl->OnKeyDown(sink,&ctx.iface,'V',0x002f0001,&routed))&&!routed,
          "actual keydown rechecks logical command target after text preview");
    check(pendingRequests==0,"command key never starts a text edit session");
    logicalOwner.kind=RIUM_OWNER_TEXT;logicalOwner.logicalObject=200;logicalOwner.lifetimeToken=301;
    check(wants(sink,&ctx.iface),"text target can be observed again after a command");
    service->compTargetCtx=&ctx.iface;service->compTargetFocusHwnd=owner;
    service->compTargetOwner=(RiumOwnerBinding){.stamp=logicalOwner,.epoch=service->inputOwner.epoch};
    RiumOwnerBinding originalTextOwner=service->compTargetOwner;
    service->fsm=(FsmContext){.state=STATE_CHO_JUNG_JONG,.cho=18,.jung=0,.jong=4};
    logicalOwner.kind=RIUM_OWNER_COMMAND;logicalOwner.logicalObject=201;logicalOwner.lifetimeToken=302;
    pendingRequests=0;
    check(!wants(sink,&ctx.iface)&&service->fsm.state==STATE_EMPTY&&service->cpPendingCommit==L'\ud55c'&&pendingRequests==0,
          "logical text to command boundary preserves the old syllable without any write");
    service->fsm.state=STATE_EMPTY;
    pendingInsert.lpVtbl=&pending_insert_vtable;pendingMode=1;pendingWrites=pendingRequests=0;
    service->cpPendingCommit=L'\ud55c';service->cpPendingCtx=&ctx.iface;service->cpPendingFocusHwnd=owner;
    service->pendingOwner=originalTextOwner;
    logicalOwner.kind=RIUM_OWNER_TEXT;logicalOwner.logicalObject=200;logicalOwner.lifetimeToken=301;
    wants(sink,&ctx.iface);
    check(pendingWrites==0&&pendingRequests==0&&service->cpPendingCommit==L'\ud55c',
          "returning to a shared text host does not replay an unproven deferred syllable");
    logicalOwner.kind=RIUM_OWNER_UNKNOWN;pendingRequests=pendingWrites=0;
    service->cpPendingCommit=L'\ud55c';service->cpPendingCtx=&ctx.iface;service->cpPendingFocusHwnd=owner;
    wants(sink,&ctx.iface);
    check(pendingWrites==0&&pendingRequests==0&&service->cpPendingCommit==L'\ud55c',
          "failed recognized owner read cannot authorize old pending text");
    pendingMode=0;
    service->cpPendingCommit=0;service->cpPendingCtx=NULL;service->cpPendingFocusHwnd=NULL;
    service->compTargetOwner=(RiumOwnerBinding){0};service->pendingOwner=(RiumOwnerBinding){0};
    service->fsm.state=STATE_EMPTY;service->compTargetCtx=NULL;service->compTargetFocusHwnd=NULL;
    service->inputOwner=(RiumOwnerState){0};
    service->inputOwnerBoundary=FALSE;
    // TRANSITORY is also used by real text stores. It cannot by itself prove
    // that a workspace wants commands; require the independent owner source.
    ctx.status.dwStaticFlags = 0x0004; // TF_SS_TRANSITORY
    if (argc == 3 && wcscmp(argv[2], L"--expect-known-workspace-bug") == 0)
        check(wants(sink, &ctx.iface), "characterization: diagnostic preview still reproduces the workspace bug");
    else {
        check(wants(sink,&ctx.iface),"transitory text is not misclassified solely by a TSF flag");
        logicalOwner.kind=RIUM_OWNER_COMMAND;
        service->inputOwner.reader=fixture_owner;service->inputOwner.readerContext=&logicalOwner;
        check(!wants(sink,&ctx.iface),"verified transitory command surface passes first V");
        service->inputOwner=(RiumOwnerState){0};service->inputOwnerBoundary=FALSE;
    }
    ctx.status.dwStaticFlags = 0;
    check(!wants(sink, NULL), "no text context passes first shortcut");
    set_flag(mgr, client, &GUID_COMPARTMENT_KEYBOARD_DISABLED, 1);
    check(!wants(sink, &ctx.iface), "disabled changes on same context apply to first key");
    set_flag(mgr, client, &GUID_COMPARTMENT_KEYBOARD_DISABLED, 0);
    check(wants(sink, &ctx.iface), "same context resumes Korean immediately");
    set_flag(mgr, client, &GUID_COMPARTMENT_EMPTYCONTEXT, 1);
    check(!wants(sink, &ctx.iface), "empty context passes shortcut");
    set_flag(mgr, client, &GUID_COMPARTMENT_EMPTYCONTEXT, 0);
    ctx.status.dwDynamicFlags = TF_SD_READONLY;
    check(!wants(sink, &ctx.iface), "read-only context passes shortcut");
    ctx.status.dwDynamicFlags = 0; ctx.statusResult = TF_E_DISCONNECTED;
    check(!wants(sink, &ctx.iface), "disconnected context passes shortcut");
    ctx.statusResult = S_OK; service->ctxKeyboardDisabled = TRUE;
    check(wants(sink, &ctx.iface), "stale disabled cache cannot block a newly editable field");
    service->ctxKeyboardDisabled = FALSE;
    check(service->config.currentLayoutIndex == korean, "shortcut routing preserves Korean selection");
    for(restrictionFailure=1;restrictionFailure<=4;++restrictionFailure)
        check(!wants(sink,&ctx.iface), "restriction API error or invalid type cannot justify intercepting a key");
    restrictionFailure=0;
    // The actual KeyDown must reevaluate restrictions if they change after preview.
    check(wants(sink,&ctx.iface), "editable key preview before same-context change");
    set_flag(mgr,client,&GUID_COMPARTMENT_EMPTYCONTEXT,1);
    BOOL handled=TRUE;
    check(SUCCEEDED(sink->lpVtbl->OnKeyDown(sink,&ctx.iface,'V',0x002f0001,&handled))&&!handled,
          "key down rechecks changed context after preview");
    set_flag(mgr,client,&GUID_COMPARTMENT_EMPTYCONTEXT,0);
    // A busy old composition must not lock another application out of its keys.
    Context other = { .iface = { &context_vtable }, .compartments = mgr, .statusResult = S_OK };
    service->pCompContext=&ctx.iface;service->pCompFocusHwnd=owner;
    service->compFinalizePending=TRUE;service->fsm.state=STATE_CHO;
    check(!wants(sink,&other.iface),"pending old composition passes another context's first key");
    handled=TRUE;
    check(SUCCEEDED(sink->lpVtbl->OnKeyDown(sink,&other.iface,'V',0x002f0001,&handled))&&!handled,
          "pending old composition also passes actual keydown in another context");
    check(wants(sink,&ctx.iface),"pending composition protects its own interim character");
    service->pCompFocusHwnd=(HWND)(ULONG_PTR)1;
    check(!wants(sink,&ctx.iface),"same context with another focus window passes the first shortcut");
    handled=TRUE;
    check(SUCCEEDED(sink->lpVtbl->OnKeyDown(sink,&ctx.iface,'V',0x002f0001,&handled))&&!handled,
          "same-context focus change also passes the actual shortcut");
    service->pCompFocusHwnd=owner;
    handled=FALSE;
    check(SUCCEEDED(sink->lpVtbl->OnTestKeyDown(sink,&ctx.iface,VK_ESCAPE,1,&handled))&&handled,
          "Escape recovery remains available while finalization is pending");
    handled=FALSE;
    check(SUCCEEDED(sink->lpVtbl->OnTestKeyDown(sink,&ctx.iface,VK_HANGUL,1,&handled))&&handled,
          "Hangul toggle remains available while finalization is pending");
    service->pCompContext=NULL;service->compFinalizePending=FALSE;service->fsm.state=STATE_EMPTY;
    service->cpPendingCommit=L'\ud55c';service->cpPendingCtx=&ctx.iface;
    service->cpPendingFocusHwnd=(HWND)(ULONG_PTR)1;pendingRequests=0;
    check(!wants(sink,&ctx.iface),"fallback pending text passes the new window's first shortcut");
    check(pendingRequests==0&&service->cpPendingCommit==L'\ud55c',
          "shared context never retries original text at another window");
    handled=TRUE;
    check(SUCCEEDED(sink->lpVtbl->OnKeyDown(sink,&ctx.iface,'V',0x002f0001,&handled))&&!handled,
          "fallback pending text also passes actual keydown at another window");
    service->cpPendingFocusHwnd=owner;
    check(!wants(sink,&ctx.iface)&&pendingRequests==1&&service->cpPendingCommit==L'\ud55c',
          "failed insertion on original target retains the pending syllable");
    check(service->fsm.state==STATE_EMPTY,"pending delivery does not create a second unresolved composition");
    nestedSink=sink;nestedContext=&ctx.iface;reenterPending=1;pendingRequests=0;
    check(!wants(sink,&ctx.iface)&&pendingRequests==1&&service->cpPendingCommit==L'\ud55c',
          "reentrant key preview cannot retry the same pending syllable twice");
    service->cpPendingFocusHwnd=NULL;pendingRequests=0;
    check(!wants(sink,&ctx.iface)&&pendingRequests==0&&service->cpPendingCommit==L'\ud55c',
          "unknown pending owner is never treated as a wildcard");
    handled=FALSE;
    check(SUCCEEDED(sink->lpVtbl->OnTestKeyDown(sink,&ctx.iface,VK_ESCAPE,1,&handled))&&handled&&
          pendingRequests==0&&service->cpPendingCommit==L'\ud55c',
          "pending Escape preview never retries or commits the syllable");
    handled=FALSE;
    check(SUCCEEDED(sink->lpVtbl->OnKeyDown(sink,&ctx.iface,VK_ESCAPE,1,&handled))&&handled&&
          service->cpPendingCommit==0&&pendingRequests==0,
          "Escape cancels unresolved local pending state without insertion");
    check(wants(sink,&ctx.iface),"normal Korean input resumes after pending Escape recovery");
    pendingInsert.lpVtbl=&pending_insert_vtable;nestedService=service;
    for(int completionMode=1;completionMode<=2;++completionMode){
        service->cpPendingCommit=L'\ud55c';service->cpPendingCtx=&ctx.iface;
        service->cpPendingFocusHwnd=owner;
        pendingMode=completionMode;pendingWrites=pendingRequests=0;reenterPending=1;
        wants(sink,&ctx.iface);
        check(pendingRequests==1&&pendingWrites==1&&pendingWritten==L'\ud55c'&&!service->cpPendingInFlight,
              "successful pending retry inserts once despite a nested key callback");
        check(service->cpPendingCommit==(completionMode==1?0:L'\uae00'),
              "pending completion clears only its own captured slot generation");
        pendingMode=0;
        sink->lpVtbl->OnKeyDown(sink,&ctx.iface,VK_ESCAPE,1,&handled);
    }
    service->cpPendingCommit=0;service->cpPendingCtx=NULL;service->cpPendingFocusHwnd=NULL;
    for(int transitionKey=0;transitionKey<3;++transitionKey) {
        service->fsm=(FsmContext){.state=STATE_CHO_JUNG_JONG,.cho=18,.jung=0,.jong=4};
        service->compTargetCtx=&ctx.iface;service->compTargetFocusHwnd=(HWND)(ULONG_PTR)1;
        service->compTargetHwnd=NULL;pendingRequests=0;handled=TRUE;
        if(!transitionKey) {
            check(!wants(sink,&ctx.iface),"active fallback on a shared context defers before the first V");
            check(SUCCEEDED(sink->lpVtbl->OnKeyDown(sink,&ctx.iface,'V',0x002f0001,&handled))&&!handled,
                  "active fallback cannot consume V in the new window");
        } else if(transitionKey==1) {
            check(SUCCEEDED(sink->lpVtbl->OnKeyDown(sink,&ctx.iface,VK_HANGUL,1,&handled))&&handled,
                  "layout toggle remains usable after a fallback focus change");
        } else {
            service->preserved[0]=(JamoPreservedEntry){
                .guid={0x7a3d5e10,0x9c42,0x4b8a,{0x8e,0x31,0x00,0x6a,0x0d,0x17,SC_FN_ROTATE,0}},
                .fn=SC_FN_ROTATE
            };
            service->preservedCount=1;
            check(SUCCEEDED(sink->lpVtbl->OnPreservedKey(sink,&ctx.iface,&service->preserved[0].guid,&handled))&&handled,
                  "preserved layout toggle handles the focus change before the ordinary key sink");
            service->preservedCount=0;
        }
        check(service->cpPendingCommit==L'\ud55c'&&service->cpPendingFocusHwnd==(HWND)(ULONG_PTR)1&&
              service->fsm.state==STATE_EMPTY&&pendingRequests==0,
              "fallback transition keeps the original syllable without inserting at the new target");
        sink->lpVtbl->OnKeyDown(sink,&ctx.iface,VK_ESCAPE,1,&handled);
        service->config.currentLayoutIndex=korean;
    }
    // The previous composition finished; its target must not own the next one.
    service->fsm.state=STATE_EMPTY;service->compTargetCtx=&ctx.iface;
    service->compTargetFocusHwnd=(HWND)(ULONG_PTR)1;service->compTargetHwnd=NULL;
    sink->lpVtbl->OnKeyDown(sink,&ctx.iface,'G',0x00220001,&handled);
    sink->lpVtbl->OnKeyDown(sink,&ctx.iface,'K',0x00250001,&handled);
    check(service->compTargetFocusHwnd==owner&&service->cpPendingCommit==0&&
          service->fsm.state==STATE_CHO_JUNG,
          "new fallback syllable captures the new owner after the preceding composition completed");
    sink->lpVtbl->OnKeyDown(sink,&ctx.iface,VK_ESCAPE,1,&handled);
    service->fsm.state=STATE_EMPTY;service->compTargetCtx=NULL;service->compTargetFocusHwnd=NULL;
    service->cpPendingCommit=0;service->cpPendingCtx=NULL;service->cpPendingFocusHwnd=NULL;
    service->pPathContext=NULL;
    redirectStatusWindow=CreateWindowExW(0,L"STATIC",L"RIUM reentrant target",WS_POPUP,0,0,1,1,NULL,NULL,GetModuleHandleW(NULL),NULL);
    statusReads=0;redirectStatusAt=2;pendingRequests=0;
    sink->lpVtbl->OnKeyDown(sink,&ctx.iface,'G',0x00220001,&handled);
    check(GetFocus()==redirectStatusWindow&&pendingRequests==0&&service->fsm.state==STATE_EMPTY,
          "capability probing cannot redirect the first Korean key to a new owner");
    redirectStatusAt=0;SetFocus(owner);DestroyWindow(redirectStatusWindow);
    service->fsm.state=STATE_EMPTY;
    HRESULT outsideFixture = tip->lpVtbl->Activate(tip, thread, client);
#ifdef RIUM_INSTALLABLE
    check(outsideFixture != E_ACCESSDENIED, "installable DLL is not restricted to a fixture basename");
#else
    check(outsideFixture == E_ACCESSDENIED, "development DLL cannot activate in another application");
#endif
    if(SUCCEEDED(outsideFixture))tip->lpVtbl->Deactivate(tip);

    if (argc == 3 && wcscmp(argv[2], L"--manual-activation") == 0) {
        // Informational only: a successful direct call is still not proof that
        // Windows selected this TIP. Never continue native editing on failure.
        HRESULT activation = tip->lpVtbl->Activate(tip, thread, client);
        printf("Manual activation HRESULT: 0x%08lX\n", (unsigned long)activation);
        if (SUCCEEDED(activation)) tip->lpVtbl->Deactivate(tip);
    }

    mgr->lpVtbl->ClearCompartment(mgr, client, &GUID_COMPARTMENT_KEYBOARD_DISABLED);
    mgr->lpVtbl->ClearCompartment(mgr, client, &GUID_COMPARTMENT_EMPTYCONTEXT);
    mgr->lpVtbl->Release(mgr); thread->lpVtbl->Deactivate(thread); thread->lpVtbl->Release(thread);
    sink->lpVtbl->Release(sink); tip->lpVtbl->Release(tip); factory->lpVtbl->Release(factory);
    check(unloadFn() == S_OK, "DLL releases every test-owned object");
    if (unloadFn() == S_OK) FreeLibrary(dll);
    DestroyWindow(owner);CoUninitialize();
    printf("Routing contracts: %d checks, %d failures. No registration or default-profile changes.\n", checks, failures);
    return failures ? 1 : 0;
}
