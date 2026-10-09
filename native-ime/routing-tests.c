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
    return ctx->statusResult;
}
static ITfContextVtbl context_vtable = {
    .QueryInterface = context_qi, .AddRef = context_ref, .Release = context_ref,
    .GetStatus = context_status
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
    service->config.options.useUiHelper = false;
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
    HRESULT outsideFixture = tip->lpVtbl->Activate(tip, thread, client);
    check(outsideFixture == E_ACCESSDENIED, "development DLL cannot activate in another application");
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
    CoUninitialize();
    printf("Routing contracts: %d checks, %d failures. No registration or default-profile changes.\n", checks, failures);
    return failures ? 1 : 0;
}
