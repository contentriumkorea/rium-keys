#include <windows.h>
#include <initguid.h>
#include "input-scope-core.h"
#include <cstdio>
#include <new>
using Microsoft::WRL::ComPtr;

HRESULT AcquireInputScopeClient(ITfThreadMgr* manager, TfClientId* client, InputScopeActivation* report) {
    if (!manager || !client || !report) return E_POINTER;
    *client = 0; *report = {};
    ComPtr<ITfThreadMgrEx> extended;
    report->extended = manager->QueryInterface(IID_PPV_ARGS(&extended));
    if (report->extended != S_OK || !extended) return S_FALSE;
    report->flagsBeforeHr = extended->GetActiveFlags(&report->flagsBefore);
    if (report->flagsBeforeHr != S_OK || !(report->flagsBefore & TF_TMF_ACTIVATED)) return S_FALSE;
    // These are activation flags, excluding status-only ACTIVATED/IMMERSIVEMODE.
    // Preserve the existing restrictions and never newly activate a TIP/layout.
    constexpr DWORD allowed = TF_TMAE_NOACTIVATETIP | TF_TMAE_SECUREMODE | TF_TMAE_UIELEMENTENABLEDONLY |
        TF_TMAE_COMLESS | TF_TMAE_WOW16 | TF_TMAE_NOACTIVATEKEYBOARDLAYOUT | TF_TMAE_CONSOLE;
    report->requestedFlags = (report->flagsBefore & allowed) | TF_TMAE_NOACTIVATETIP | TF_TMAE_NOACTIVATEKEYBOARDLAYOUT;
    report->activate = extended->ActivateEx(client, report->requestedFlags);
    return report->activate;
}
void ReleaseInputScopeClient(ITfThreadMgr* manager, InputScopeActivation* report) {
    if (!manager || !report) return;
    if (SUCCEEDED(report->activate)) report->deactivate = manager->Deactivate();
    ComPtr<ITfThreadMgrEx> extended;
    if (manager->QueryInterface(IID_PPV_ARGS(&extended)) == S_OK && extended)
        report->flagsAfterHr = extended->GetActiveFlags(&report->flagsAfter);
}

namespace {
bool SameObject(IUnknown* a, IUnknown* b) {
    if (!a || !b) return false;
    ComPtr<IUnknown> left, right;
    const HRESULT first = a->QueryInterface(IID_PPV_ARGS(&left));
    const HRESULT second = b->QueryInterface(IID_PPV_ARGS(&right));
    return first == S_OK && second == S_OK && left && left.Get() == right.Get();
}
bool NativeFocusMatches(const InputScopeGuard& guard) {
    if (!guard.requireWindowFocus) return true;
    if (!guard.focus) return false;
    DWORD owner = 0;
    const DWORD thread = GetWindowThreadProcessId(guard.focus, &owner);
    GUITHREADINFO info{}; info.cbSize = sizeof(info);
    return owner == guard.pid && thread == guard.tid &&
        GetGUIThreadInfo(guard.tid, &info) && info.hwndFocus == guard.focus;
}
HRESULT CheckOwner(const InputScopeGuard& guard, ITfContext* context) {
    if (!guard.manager || !guard.document || !context ||
        guard.pid != GetCurrentProcessId() || guard.tid != GetCurrentThreadId() ||
        !guard.deadline || GetTickCount64() >= guard.deadline || !NativeFocusMatches(guard)) return E_ABORT;
    ComPtr<ITfDocumentMgr> document;
    ComPtr<ITfContext> current;
    HRESULT hr = guard.manager->GetFocus(&document);
    if (hr != S_OK) return hr;
    if (!SameObject(document.Get(), guard.document)) return E_ABORT;
    hr = document->GetTop(&current);
    if (hr != S_OK) return hr;
    if (!SameObject(current.Get(), context)) return E_ABORT;
    return NativeFocusMatches(guard) && GetTickCount64() < guard.deadline ? S_OK : E_ABORT;
}
// A heap COM session owns its inputs and result, never a stack or mapped-buffer
// pointer. TF_ES_SYNC is mandatory. Unexpected retained references are disarmed
// after RequestEditSession, and pin this code module before the caller releases
// its reference. Such a run is reported as abnormal, never as cleaned up.
class ReadSession final : public ITfEditSession {
    LONG refs_ = 1;
    volatile LONG armed_ = 1;
    ComPtr<ITfContext> context_;
    ComPtr<ITfThreadMgr> manager_;
    ComPtr<ITfDocumentMgr> document_;
    InputScopeGuard guard_;
    bool StillCurrent() {
        result.guardAfter = CheckOwner(guard_, context_.Get());
        return result.guardAfter == S_OK;
    }
public:
    InputScopeReport result;
    ReadSession(ITfContext* context, const InputScopeGuard& guard)
        : context_(context), manager_(guard.manager), document_(guard.document), guard_(guard) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id != IID_IUnknown && id != IID_ITfEditSession) return E_NOINTERFACE;
        *out = static_cast<ITfEditSession*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG n = InterlockedDecrement(&refs_);
        if (!n) delete this;
        return n;
    }
    void Disarm() { InterlockedExchange(&armed_, 0); }
    LONG References() { return InterlockedCompareExchange(&refs_, 0, 0); }
    HRESULT STDMETHODCALLTYPE DoEditSession(TfEditCookie cookie) override {
        // A late/second callback reads no provider state and cannot access the map.
        if (InterlockedCompareExchange(&armed_, 2, 1) != 1) return E_ABORT;
        ++result.callbacks;
        if (!StillCurrent()) return E_ABORT;
        TF_SELECTION selection{};
        result.selection = context_->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selection, &result.fetched);
        ComPtr<ITfRange> range; range.Attach(selection.range);
        ComPtr<ITfReadOnlyProperty> property;
        ComPtr<ITfInputScope> scope;
        VARIANT value; VariantInit(&value);
        InputScope* numbers = nullptr;
        bool valid = false;
        do {
            if (result.selection != S_OK || result.fetched != 1 || !range || !StillCurrent()) break;
            result.appProperty = context_->GetAppProperty(GUID_PROP_INPUTSCOPE, &property);
            if (result.appProperty != S_OK || !property || !StillCurrent()) break;
            result.value = property->GetValue(cookie, range.Get(), &value);
            result.variantType = value.vt;
            if (result.value != S_OK || value.vt != VT_UNKNOWN || !value.punkVal || !StillCurrent()) break;
            result.inputScopeQi = value.punkVal->QueryInterface(IID_PPV_ARGS(&scope));
            if (result.inputScopeQi != S_OK || !scope || !StillCurrent()) break;
            result.scopes = scope->GetInputScopes(&numbers, &result.count);
            if (result.scopes != S_OK || !numbers || !result.count || !StillCurrent()) break;
            result.copied = result.count < kInputScopeCapacity ? result.count : kInputScopeCapacity;
            result.truncated = result.count > kInputScopeCapacity;
            for (UINT i = 0; i < result.copied; ++i) result.numbers[i] = static_cast<LONG>(numbers[i]);
            valid = !result.truncated;
        } while (false);
        CoTaskMemFree(numbers);
        scope.Reset();
        result.variantClear = VariantClear(&value);
        property.Reset(); range.Reset();
        result.known = StillCurrent() && result.variantClear == S_OK && valid;
        return S_OK;
    }
};
void ModuleAddress() {}
}
InputScopeReport ReadInputScope(ITfContext* context, TfClientId client, const InputScopeGuard& guard) {
    InputScopeReport out;
    out.guardBefore = CheckOwner(guard, context);
    if (out.guardBefore != S_OK) return out;
    auto* edit = new (std::nothrow) ReadSession(context, guard);
    if (!edit) { out.request = E_OUTOFMEMORY; return out; }
    HRESULT session = E_PENDING;
    const HRESULT request = context->RequestEditSession(client, edit, TF_ES_SYNC | TF_ES_READ, &session);
    edit->Disarm();
    out = edit->result;
    out.guardBefore = S_OK;
    out.request = request; out.session = session;
    out.retainedSession = edit->References() != 1;
    if (out.retainedSession) {
        HMODULE retained = nullptr;
        const BOOL pinned = GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&ModuleAddress), &retained);
        out.lifetimePin = pinned ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    }
    out.guardAfter = CheckOwner(guard, context);
    out.known = out.known && request == S_OK && session == S_OK && out.callbacks == 1 &&
        !out.retainedSession && out.guardAfter == S_OK;
    edit->Release();
    return out;
}
void PrintInputScope(const InputScopeReport& r) {
    std::printf("scope known=%d request=%08lx session=%08lx selection=%08lx "
        "appProperty=%08lx value=%08lx vt=%u qi=%08lx scopes=%08lx count=%u "
        "copied=%u callbacks=%lu guardBefore=%08lx guardAfter=%08lx "
        "variantClear=%08lx retained=%d pin=%08lx truncated=%d enums=[",
        r.known, static_cast<unsigned long>(r.request), static_cast<unsigned long>(r.session),
        static_cast<unsigned long>(r.selection), static_cast<unsigned long>(r.appProperty),
        static_cast<unsigned long>(r.value), r.variantType,
        static_cast<unsigned long>(r.inputScopeQi), static_cast<unsigned long>(r.scopes),
        r.count, r.copied, r.callbacks, static_cast<unsigned long>(r.guardBefore),
        static_cast<unsigned long>(r.guardAfter), static_cast<unsigned long>(r.variantClear),
        r.retainedSession, static_cast<unsigned long>(r.lifetimePin), r.truncated);
    for (UINT i = 0; i < r.copied && i < kInputScopeCapacity; ++i)
        std::printf("%s%ld", i ? "," : "", r.numbers[i]);
    std::printf("] interpretation=metadata_only\n");
}
