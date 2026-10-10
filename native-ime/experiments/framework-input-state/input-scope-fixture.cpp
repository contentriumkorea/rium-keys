#include "input-scope-core.h"
#include <textstor.h>
#include <olectl.h>
#include <cstdio>
#include <new>
#include <cstring>
#include <cstdlib>
using Microsoft::WRL::ComPtr;

// An actual Windows TSF document owned by this process; no existing application,
// input profile, desktop focus, keyboard layout, or user document is changed.
enum class FixtureMode { Explicit, Default, Missing, EmptyValue, FailingScope };
static LONG liveStores = 0, liveScopes = 0;
static LONG forbiddenReads = 0, mutations = 0;
class Scope final : public ITfInputScope {
    LONG refs_ = 1;
    FixtureMode mode_;
public:
    explicit Scope(FixtureMode mode) : mode_(mode) { ++liveScopes; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id != IID_IUnknown && id != IID_ITfInputScope) return E_NOINTERFACE;
        *out = static_cast<ITfInputScope*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG n = InterlockedDecrement(&refs_);
        if (!n) { --liveScopes; delete this; }
        return n;
    }
    HRESULT STDMETHODCALLTYPE GetInputScopes(InputScope** out, UINT* count) override {
        if (!out || !count) return E_POINTER;
        *out = nullptr; *count = 0;
        if (mode_ == FixtureMode::FailingScope) return E_FAIL;
        auto* values = static_cast<InputScope*>(CoTaskMemAlloc(sizeof(InputScope)));
        if (!values) return E_OUTOFMEMORY;
        values[0] = mode_ == FixtureMode::Default ? IS_DEFAULT : IS_NUMBER;
        *out = values; *count = 1; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPhrase(BSTR**, UINT*) override { ++forbiddenReads; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetRegularExpression(BSTR*) override { ++forbiddenReads; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetSRGS(BSTR*) override { ++forbiddenReads; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetXML(BSTR*) override { ++forbiddenReads; return E_NOTIMPL; }
};
class Store final : public ITextStoreACP {
    LONG refs_ = 1;
    DWORD lock_ = 0;
    bool requested_ = false;
    HWND window_;
    FixtureMode mode_;
    ComPtr<ITextStoreACPSink> sink_;
    HRESULT RequestAttrs(ULONG count, const TS_ATTRID* ids) {
        requested_ = false;
        for (ULONG i = 0; ids && i < count; ++i)
            if (ids[i] == GUID_PROP_INPUTSCOPE && mode_ != FixtureMode::Missing) requested_ = true;
        return S_OK;
    }
public:
    Store(HWND window, FixtureMode mode) : window_(window), mode_(mode) { ++liveStores; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id != IID_IUnknown && id != IID_ITextStoreACP) return E_NOINTERFACE;
        *out = static_cast<ITextStoreACP*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG n = InterlockedDecrement(&refs_);
        if (!n) { --liveStores; delete this; }
        return n;
    }
    HRESULT STDMETHODCALLTYPE AdviseSink(REFIID id, IUnknown* value, DWORD) override {
        if (!value || id != IID_ITextStoreACPSink) return E_INVALIDARG;
        if (sink_) return CONNECT_E_ADVISELIMIT;
        return value->QueryInterface(IID_PPV_ARGS(&sink_));
    }
    HRESULT STDMETHODCALLTYPE UnadviseSink(IUnknown* value) override {
        if (!value || !sink_) return CONNECT_E_NOCONNECTION;
        ComPtr<IUnknown> a, b;
        value->QueryInterface(IID_PPV_ARGS(&a)); sink_.As(&b);
        if (a.Get() != b.Get()) return CONNECT_E_NOCONNECTION;
        sink_.Reset(); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE RequestLock(DWORD flags, HRESULT* result) override {
        if (!result) return E_POINTER;
        if (!sink_) return E_UNEXPECTED;
        if (lock_) { *result = TS_E_SYNCHRONOUS; return S_OK; }
        if ((flags & TS_LF_READWRITE) == TS_LF_READWRITE) ++mutations;
        lock_ = flags; *result = sink_->OnLockGranted(flags); lock_ = 0; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetStatus(TS_STATUS* out) override { if (!out) return E_POINTER; *out = {0, TS_SS_NOHIDDENTEXT}; return S_OK; }
    HRESULT STDMETHODCALLTYPE QueryInsert(LONG a, LONG b, ULONG, LONG* x, LONG* y) override { if (!x || !y) return E_POINTER; *x = a; *y = b; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetSelection(ULONG index, ULONG count, TS_SELECTION_ACP* out, ULONG* fetched) override {
        if (!out || !fetched) return E_POINTER;
        *fetched = 0; if (!(lock_ & TS_LF_READ)) return TS_E_NOLOCK;
        if (index != 0 && index != TS_DEFAULT_SELECTION) return E_INVALIDARG;
        if (count) { *out = {0, 0, {TS_AE_END, FALSE}}; *fetched = 1; } return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetSelection(ULONG, const TS_SELECTION_ACP*) override { ++mutations; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetText(LONG start, LONG, WCHAR*, ULONG, ULONG* returned, TS_RUNINFO*, ULONG, ULONG* runs, LONG* next) override {
        ++forbiddenReads; if (!returned || !runs || !next) return E_POINTER;
        *returned = 0; *runs = 0; *next = start; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetText(DWORD, LONG, LONG, const WCHAR*, ULONG, TS_TEXTCHANGE*) override { ++mutations; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetFormattedText(LONG, LONG, IDataObject** out) override { ++forbiddenReads; if (out) *out = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetEmbedded(LONG, REFGUID, REFIID, IUnknown** out) override { ++forbiddenReads; if (out) *out = nullptr; return TS_E_NOOBJECT; }
    HRESULT STDMETHODCALLTYPE QueryInsertEmbedded(const GUID*, const FORMATETC*, BOOL* out) override { if (!out) return E_POINTER; *out = FALSE; return S_OK; }
    HRESULT STDMETHODCALLTYPE InsertEmbedded(DWORD, LONG, LONG, IDataObject*, TS_TEXTCHANGE*) override { ++mutations; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE InsertTextAtSelection(DWORD, const WCHAR*, ULONG, LONG*, LONG*, TS_TEXTCHANGE*) override { ++mutations; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE InsertEmbeddedAtSelection(DWORD, IDataObject*, LONG*, LONG*, TS_TEXTCHANGE*) override { ++mutations; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE RequestSupportedAttrs(DWORD, ULONG count, const TS_ATTRID* ids) override { return RequestAttrs(count, ids); }
    HRESULT STDMETHODCALLTYPE RequestAttrsAtPosition(LONG, ULONG count, const TS_ATTRID* ids, DWORD) override { return RequestAttrs(count, ids); }
    HRESULT STDMETHODCALLTYPE RequestAttrsTransitioningAtPosition(LONG, ULONG count, const TS_ATTRID* ids, DWORD) override { return RequestAttrs(count, ids); }
    HRESULT STDMETHODCALLTYPE FindNextAttrTransition(LONG, LONG halt, ULONG, const TS_ATTRID*, DWORD, LONG* next, BOOL* found, LONG* offset) override {
        if (!next || !found || !offset) return E_POINTER;
        *next = halt; *found = FALSE; *offset = 0; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE RetrieveRequestedAttrs(ULONG count, TS_ATTRVAL* out, ULONG* fetched) override {
        if (!fetched || (count && !out)) return E_POINTER;
        *fetched = 0;
        if (count && requested_) {
            requested_ = false;
            out[0].idAttr = GUID_PROP_INPUTSCOPE; out[0].dwOverlapId = 0;
            VariantInit(&out[0].varValue);
            if (mode_ == FixtureMode::EmptyValue) { *fetched = 1; return S_OK; }
            out[0].varValue.vt = VT_UNKNOWN;
            out[0].varValue.punkVal = new (std::nothrow) Scope(mode_);
            if (!out[0].varValue.punkVal) { out[0].varValue.vt = VT_EMPTY; return E_OUTOFMEMORY; }
            *fetched = 1;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetEndACP(LONG* end) override { if (!end) return E_POINTER; *end = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetActiveView(TsViewCookie* view) override { if (!view) return E_POINTER; *view = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetACPFromPoint(TsViewCookie, const POINT*, DWORD, LONG*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetTextExt(TsViewCookie, LONG, LONG, RECT*, BOOL*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetScreenExt(TsViewCookie, RECT*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetWnd(TsViewCookie, HWND* out) override { if (!out) return E_POINTER; *out = window_; return S_OK; }
};

static int failures = 0, tests = 0;
static void Check(bool ok, const char* name) { ++tests; if (!ok) ++failures; std::printf("%s %s\n", ok ? "PASS" : "FAIL", name); }
static void Case(ITfThreadMgrEx* manager, TfClientId client, HWND window, FixtureMode mode, const char* name) {
    ComPtr<ITfDocumentMgr> document;
    ComPtr<ITfContext> context;
    ComPtr<ITextStoreACP> store;
    store.Attach(new Store(window, mode));
    TfEditCookie initial = 0;
    HRESULT created = manager->CreateDocumentMgr(&document);
    if (created == S_OK) created = document->CreateContext(client, 0, store.Get(), &context, &initial);
    HRESULT pushed = context ? document->Push(context.Get()) : E_UNEXPECTED;
    HRESULT focused = pushed == S_OK ? manager->SetFocus(document.Get()) : E_UNEXPECTED;
    Check(created == S_OK && pushed == S_OK && focused == S_OK, "own real TSF context setup");
    if (focused == S_OK) {
        InputScopeGuard guard{manager, document.Get(), window, GetCurrentProcessId(), GetCurrentThreadId(), GetTickCount64() + 5000, false};
        forbiddenReads = 0; mutations = 0;
        const InputScopeReport result = ReadInputScope(context.Get(), client, guard);
        std::printf("case=%s ", name); PrintInputScope(result);
        if (mode == FixtureMode::Explicit || mode == FixtureMode::Default)
            Check(result.known && result.count == 1 && result.copied == 1 && result.numbers[0] == (mode == FixtureMode::Default ? IS_DEFAULT : IS_NUMBER), name);
        else if (mode == FixtureMode::Missing || mode == FixtureMode::EmptyValue)
            Check(!result.known && result.appProperty == S_OK && result.value == (mode == FixtureMode::Missing ? E_FAIL : S_OK) && result.variantType == VT_EMPTY && result.inputScopeQi == E_PENDING, name);
        else
            Check(!result.known && result.value == S_OK && result.variantType == VT_UNKNOWN && result.inputScopeQi == S_OK && result.scopes == E_FAIL, name);
        Check(result.callbacks == 1 && result.request == S_OK && result.session == S_OK && !result.retainedSession, "synchronous session exercised and released");
        Check(forbiddenReads == 0 && mutations == 0, "no text reads or document changes during diagnostic");
        guard.requireWindowFocus = true; guard.focus = nullptr;
        const auto skipped = ReadInputScope(context.Get(), client, guard);
        Check(!skipped.known && skipped.callbacks == 0 && skipped.request == E_PENDING, "empty native focus skips before session");
        guard.requireWindowFocus = false; guard.pid ^= 0x80000000UL;
        const auto wrongOwner = ReadInputScope(context.Get(), client, guard);
        Check(!wrongOwner.known && wrongOwner.callbacks == 0 && wrongOwner.request == E_PENDING, "wrong process owner skips before session");
        guard.pid = GetCurrentProcessId(); guard.deadline = GetTickCount64();
        const auto expired = ReadInputScope(context.Get(), client, guard);
        Check(!expired.known && expired.callbacks == 0 && expired.request == E_PENDING, "expired diagnostic skips before session");
    }
    const HRESULT unfocused = manager->SetFocus(nullptr);
    const HRESULT popped = pushed == S_OK ? document->Pop(TF_POPF_ALL) : S_OK;
    context.Reset(); document.Reset(); store.Reset();
    Check(unfocused == S_OK && popped == S_OK && liveStores == 0 && liveScopes == 0, "context store scope references cleaned");
}
static LRESULT CALLBACK OwnActivationVeto(int code, WPARAM wp, LPARAM lp) {
    if (code == HCBT_ACTIVATE) return 1;
    return CallNextHookEx(nullptr, code, wp, lp);
}
// Separate, disposable host for the controller transport test. Its only native
// focus operation is SetFocus on its own HWND_MESSAGE, with activation vetoed.
// No ShowWindow/SetForegroundWindow/AttachThreadInput/SendInput is used.
static int TransportHost(DWORD duration, DWORD stall) {
    const HWND foreground = GetForegroundWindow();
    const HKL layout = GetKeyboardLayout(0);
    HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) return 3;
    HWND window = CreateWindowExW(0, L"STATIC", nullptr, 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
    HHOOK veto = SetWindowsHookExW(WH_CBT, OwnActivationVeto, nullptr, GetCurrentThreadId());
    ComPtr<ITfThreadMgrEx> manager;
    ComPtr<ITfDocumentMgr> document, previousAssociation;
    ComPtr<ITfContext> context;
    ComPtr<ITextStoreACP> store;
    TfClientId client = 0; TfEditCookie initial = 0;
    HRESULT created = CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&manager));
    HRESULT activated = created == S_OK ? manager->ActivateEx(&client, TF_TMAE_NOACTIVATEKEYBOARDLAYOUT | TF_TMAE_NOACTIVATETIP) : E_UNEXPECTED;
    HRESULT contextHr = E_PENDING, pushed = E_PENDING, associated = E_PENDING, logicalFocus = E_PENDING;
    if (window && veto && activated == S_OK) {
        store.Attach(new Store(window, FixtureMode::Explicit));
        contextHr = manager->CreateDocumentMgr(&document);
        if (contextHr == S_OK) contextHr = document->CreateContext(client, 0, store.Get(), &context, &initial);
        if (contextHr == S_OK) pushed = document->Push(context.Get());
        if (pushed == S_OK) associated = manager->AssociateFocus(window, document.Get(), &previousAssociation);
        if (associated == S_OK) {
            SetFocus(window);
            logicalFocus = manager->SetFocus(document.Get());
        }
    }
    GUITHREADINFO info{}; info.cbSize = sizeof(info);
    const bool ready = logicalFocus == S_OK && GetGUIThreadInfo(GetCurrentThreadId(), &info) &&
        info.hwndFocus == window && GetForegroundWindow() == foreground;
    std::printf("HOST ready=%d pid=%lu tid=%lu hwnd=0x%llx focus=0x%llx ownMessageOnly=1 activate=%08lx associate=%08lx logicalFocus=%08lx\n",
        ready, GetCurrentProcessId(), GetCurrentThreadId(), static_cast<unsigned long long>(reinterpret_cast<ULONG_PTR>(window)),
        static_cast<unsigned long long>(reinterpret_cast<ULONG_PTR>(info.hwndFocus)),
        static_cast<unsigned long>(activated), static_cast<unsigned long>(associated), static_cast<unsigned long>(logicalFocus));
    std::fflush(stdout);
    const ULONGLONG deadline = GetTickCount64() + duration;
    if (ready) {
        // A bounded pause exercises controller timeout without a hung app/API.
        if (stall) Sleep(stall);
        while (GetTickCount64() < deadline) {
            MSG message;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message); DispatchMessageW(&message);
            }
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
        }
    }
    ComPtr<ITfDocumentMgr> displaced;
    const HRESULT restored = associated == S_OK ? manager->AssociateFocus(window, previousAssociation.Get(), &displaced) : S_OK;
    displaced.Reset(); previousAssociation.Reset();
    if (GetFocus() == window) SetFocus(nullptr);
    const HRESULT unfocused = manager ? manager->SetFocus(nullptr) : E_UNEXPECTED;
    const HRESULT popped = pushed == S_OK ? document->Pop(TF_POPF_ALL) : S_OK;
    context.Reset(); document.Reset(); store.Reset();
    const HRESULT deactivated = activated == S_OK ? manager->Deactivate() : E_UNEXPECTED;
    manager.Reset();
    const BOOL unhooked = veto ? UnhookWindowsHookEx(veto) : FALSE;
    const BOOL destroyed = window ? DestroyWindow(window) : FALSE;
    CoUninitialize();
    const bool clean = restored == S_OK && unfocused == S_OK && popped == S_OK && deactivated == S_OK && unhooked &&
        destroyed && !liveStores && !liveScopes && GetForegroundWindow() == foreground && GetKeyboardLayout(0) == layout;
    std::printf("HOST_STOP cleanup=%d restoreAssociation=%08lx pop=%08lx deactivate=%08lx ownCbtUnhook=%d stores=%ld scopes=%ld foregroundUnchanged=%d layoutUnchanged=%d\n",
        clean, static_cast<unsigned long>(restored), static_cast<unsigned long>(popped), static_cast<unsigned long>(deactivated),
        unhooked, liveStores, liveScopes, GetForegroundWindow() == foreground, GetKeyboardLayout(0) == layout);
    return !clean ? 1 : ready ? 0 : 77;
}
int main(int argc, char** argv) {
    if (argc == 4 && !std::strcmp(argv[1], "--transport-host")) {
        const unsigned long duration = std::strtoul(argv[2], nullptr, 10), stall = std::strtoul(argv[3], nullptr, 10);
        if (duration < 1000 || duration > 10000 || stall > duration) return 2;
        return TransportHost(duration, stall);
    }
    if (argc != 1) return 2;
    const HWND foreground = GetForegroundWindow();
    const HKL layout = GetKeyboardLayout(0);
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) return 3;
    HWND window = CreateWindowExW(0, L"STATIC", nullptr, 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
    ComPtr<ITfThreadMgrEx> manager;
    HRESULT created = CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&manager));
    TfClientId client = 0;
    if (created == S_OK) {
        InputScopeActivation inactive;
        TfClientId unused = 0;
        const HRESULT skipped = AcquireInputScopeClient(manager.Get(), &unused, &inactive);
        ReleaseInputScopeClient(manager.Get(), &inactive);
        Check(skipped == S_FALSE && inactive.activate == E_PENDING && inactive.deactivate == E_PENDING &&
            inactive.flagsBeforeHr == S_OK && !(inactive.flagsBefore & TF_TMF_ACTIVATED) &&
            inactive.flagsAfterHr == S_OK && inactive.flagsAfter == inactive.flagsBefore,
            "created but inactive real manager skipped without activation");
    }
    HRESULT activated = created == S_OK ? manager->ActivateEx(&client, TF_TMAE_NOACTIVATEKEYBOARDLAYOUT | TF_TMAE_NOACTIVATETIP) : E_UNEXPECTED;
    Check(window && created == S_OK && activated == S_OK, "own message-only window and TSF manager (no TIP or layout activation)");
    if (window && activated == S_OK) {
        InputScopeActivation nested;
        TfClientId temporary = 0;
        const HRESULT acquired = AcquireInputScopeClient(manager.Get(), &temporary, &nested);
        ReleaseInputScopeClient(manager.Get(), &nested);
        std::printf("nested acquire=%08lx beforeHr=%08lx before=%08lx requested=%08lx deactivate=%08lx afterHr=%08lx after=%08lx\n",
            static_cast<unsigned long>(acquired), static_cast<unsigned long>(nested.flagsBeforeHr), nested.flagsBefore,
            nested.requestedFlags, static_cast<unsigned long>(nested.deactivate), static_cast<unsigned long>(nested.flagsAfterHr), nested.flagsAfter);
        Check(SUCCEEDED(acquired) && SUCCEEDED(nested.activate) && nested.deactivate == S_OK && nested.flagsBeforeHr == S_OK && nested.flagsAfterHr == S_OK &&
            nested.flagsBefore == nested.flagsAfter && (nested.requestedFlags & 0x21) == 0x21,
            "already-active manager balanced with TIP and layout restrictions preserved");
        Case(manager.Get(), client, window, FixtureMode::Explicit, "explicit IS_NUMBER remains numeric 29");
        Case(manager.Get(), client, window, FixtureMode::Default, "explicit IS_DEFAULT zero remains known");
        Case(manager.Get(), client, window, FixtureMode::Missing, "missing property remains unknown");
        Case(manager.Get(), client, window, FixtureMode::EmptyValue, "successful empty value remains unknown");
        Case(manager.Get(), client, window, FixtureMode::FailingScope, "GetInputScopes failure remains unknown");
    }
    const HRESULT deactivated = activated == S_OK ? manager->Deactivate() : E_UNEXPECTED;
    manager.Reset();
    const BOOL destroyed = window ? DestroyWindow(window) : FALSE;
    CoUninitialize();
    Check(deactivated == S_OK && destroyed && liveStores == 0 && liveScopes == 0, "manager window and COM cleanup");
    Check(GetForegroundWindow() == foreground && GetKeyboardLayout(0) == layout, "foreground and keyboard layout unchanged");
    std::printf("own_real_TSF_fixture tests=%d failures=%d external_provider=NOT_EXERCISED\n", tests, failures);
    return failures ? 1 : 0;
}
