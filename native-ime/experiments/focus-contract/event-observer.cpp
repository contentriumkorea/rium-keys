#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <oleacc.h>
#include <servprov.h>
#include <uiautomation.h>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <cerrno>
#include <cstdio>
#include <cwchar>

// Read-only, disposable diagnostic, NOT a routing policy. No text, names, values,
// document ranges, input injection, focus changes, actions, or settings writes.
// Usage: event-observer.exe <PID> <MAIN-HWND> [seconds:1..60]
// --self-test-fixtures tests callback policy with synthetic inputs, NOT providers.
// --self-test-own-provider raises a selection notification from our own hidden
// provider. It tests actual UIA event transport, NOT natural focus/text acceptance.
// A watchdog kills only this process after the budget plus 5 seconds. This cannot
// promise cancellation of a provider call already executing in another process.
// Out-of-context WinEvents are asynchronous: this cannot guarantee a first-key
// decision. Missing events/patterns remain unknown, never proof of non-editing.
// IA2 UUIDs/discovery are from LinuxA11y/IAccessible2 api/Accessible2.idl and
// api/AccessibleText.idl. Only QueryService, QueryInterface and IUnknown are used;
// no IA2 method vtable is declared or guessed.
namespace {
constexpr unsigned kMaxEvents = 128, kMaxOwnerChecks = 512, kMaxCalls = 4096;
constexpr unsigned kMaxRows = 8192, kReservedRows = 48;
constexpr DWORD kCleanupGraceMs = 5000;
const IID kIAccessible2 = {0xe89f726e,0xc4f4,0x4c19,{0xbb,0x19,0xb6,0x47,0xd7,0xfa,0x84,0x78}};
const IID kIAccessibleText = {0x24fd2ffb,0x3aad,0x4a08,{0x83,0x35,0xa3,0xad,0x89,0xc0,0xfb,0x4b}};

template<class T> struct Ref {
    T* p = nullptr;
    Ref() = default;
    ~Ref() { if (p) p->Release(); }
    Ref(const Ref&) = delete;
    Ref& operator=(const Ref&) = delete;
};
struct Variant {
    VARIANT v;
    Variant() { VariantInit(&v); }
    ~Variant() { VariantClear(&v); }
};
struct Output {
    SRWLOCK lock = SRWLOCK_INIT;
    std::atomic<unsigned> rows{0}, reserved{0}, dropped{0};
    void write(bool important, const char* format, ...) {
        auto& counter = important ? reserved : rows;
        if (counter.fetch_add(1) >= (important ? kReservedRows : kMaxRows)) {
            ++dropped;
            return;
        }
        char buffer[2048];
        va_list args;
        va_start(args, format);
        const int size = vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        if (size < 0 || static_cast<size_t>(size) >= sizeof(buffer)) { ++dropped; return; }
        AcquireSRWLockExclusive(&lock);
        fputs(buffer, stdout);
        fputc('\n', stdout);
        ReleaseSRWLockExclusive(&lock);
    }
} output;

struct EventGate {
    DWORD target = 0;
    ULONGLONG deadline = 0;
    unsigned used = 0;
    bool stopping = false;
    bool accept(HRESULT hr, DWORD pid, ULONGLONG now) {
        if (stopping || now >= deadline || used >= kMaxEvents || hr != S_OK || pid != target) return false;
        ++used;
        return true;
    }
};

struct State {
    std::atomic<ULONG> refs{1}, active{0};
    std::atomic<bool> stopping{false};
    std::atomic<unsigned> busy{0}, foreign{0}, unknown{0}, ignored{0};
    std::atomic<unsigned> selectionEvents{0}, focusEvents{0}, msaaEvents{0};
    SRWLOCK queryLock = SRWLOCK_INIT;
    EventGate gate;
    HWND main = nullptr;
    HANDLE targetProcess = nullptr;
    unsigned ownerChecks = 0, calls = 0;
    ULONG addRef() { return ++refs; }
    ULONG release() {
        const ULONG remaining = --refs;
        if (!remaining) delete this;
        return remaining;
    }
    ~State() { if (targetProcess) CloseHandle(targetProcess); }
    bool targetAlive() const {
        DWORD pid = 0;
        return GetWindowThreadProcessId(main, &pid) && pid == gate.target &&
            (!targetProcess || WaitForSingleObject(targetProcess, 0) == WAIT_TIMEOUT);
    }
    bool call(const char* source, const char* operation) {
        if (stopping.load() || GetTickCount64() >= gate.deadline || calls >= kMaxCalls) return false;
        ++calls;
        output.write(false, "{\"kind\":\"call\",\"source\":\"%s\",\"sequence\":%u,\"operation\":\"%s\"}", source, gate.used, operation);
        return true;
    }
};
struct CallbackScope {
    State* s;
    bool locked = false;
    explicit CallbackScope(State* state) : s(state) {
        s->addRef();
        ++s->active;
        if (s->stopping.load() || GetTickCount64() >= s->gate.deadline) return;
        locked = TryAcquireSRWLockExclusive(&s->queryLock) != FALSE;
        if (!locked) ++s->busy;
        else if (!s->targetAlive() || s->ownerChecks >= kMaxOwnerChecks || s->gate.used >= kMaxEvents) {
            ReleaseSRWLockExclusive(&s->queryLock);
            locked = false;
            ++s->ignored;
        }
    }
    ~CallbackScope() {
        if (locked) ReleaseSRWLockExclusive(&s->queryLock);
        --s->active;
        s->release();
    }
    bool accept(HRESULT hr, DWORD pid) {
        s->gate.stopping = s->stopping.load();
        if (hr != S_OK || !pid) ++s->unknown;
        else if (pid != s->gate.target) ++s->foreign;
        return s->gate.accept(hr, pid, GetTickCount64());
    }
};

const char* Scalar(HRESULT hr, const VARIANT& v, VARTYPE expected, char (&buffer)[32]) {
    if (hr != S_OK || v.vt != expected) return "null";
    if (expected == VT_BOOL) return v.boolVal == VARIANT_FALSE ? "false" : "true";
    if (expected == VT_I4) { snprintf(buffer, sizeof(buffer), "%ld", v.lVal); return buffer; }
    return "null";
}
void PrintScalar(const char* source, const char* name, HRESULT hr, const VARIANT& v, VARTYPE expected) {
    char buffer[32];
    const char* status = hr != S_OK ? "query_failed" : v.vt == expected ? "supported" :
        v.vt == VT_EMPTY ? "empty" : v.vt == VT_UNKNOWN ? "non_scalar_or_reserved" : "unexpected_variant_type";
    output.write(false, "{\"kind\":\"scalar\",\"source\":\"%s\",\"property\":\"%s\",\"hr\":\"0x%08lx\",\"vt\":%u,\"status\":\"%s\",\"value\":%s}",
        source, name, static_cast<unsigned long>(hr), v.vt, status, Scalar(hr, v, expected, buffer));
}
void Result(const char* operation, HRESULT hr, bool present, bool important = false) {
    output.write(important, "{\"kind\":\"result\",\"operation\":\"%s\",\"hr\":\"0x%08lx\",\"present\":%s}",
        operation, static_cast<unsigned long>(hr), present ? "true" : "false");
}
void ReadGui(DWORD tid) {
    GUITHREADINFO info{};
    info.cbSize = sizeof(info);
    SetLastError(0);
    const BOOL ok = GetGUIThreadInfo(tid, &info);
    const DWORD error = ok ? 0 : GetLastError();
    output.write(false, "{\"kind\":\"gui\",\"thread\":%lu,\"ok\":%s,\"error\":%lu,\"flags\":%lu,\"focus_hwnd\":\"%p\",\"caret_hwnd\":\"%p\",\"caret_rect\":[%ld,%ld,%ld,%ld]}",
        tid, ok ? "true" : "false", error, info.flags, static_cast<void*>(info.hwndFocus), static_cast<void*>(info.hwndCaret),
        info.rcCaret.left, info.rcCaret.top, info.rcCaret.right, info.rcCaret.bottom);
}
void UiaProperty(State* s, IUIAutomationElement* element, PROPERTYID id, const char* name, VARTYPE type) {
    if (!s->call("uia", name)) return;
    Variant value;
    const HRESULT hr = element->GetCurrentPropertyValueEx(id, TRUE, &value.v);
    PrintScalar("uia", name, hr, value.v, type);
}
void ReadRuntimeId(State* s, IUIAutomationElement* element) {
    if (!s->call("uia", "GetRuntimeId")) return;
    SAFEARRAY* ids = nullptr;
    const HRESULT hr = element->GetRuntimeId(&ids);
    char values[384] = "";
    LONG first = 0, last = -1;
    VARTYPE type = VT_EMPTY;
    unsigned read = 0;
    if (hr == S_OK && ids && SafeArrayGetDim(ids) == 1 &&
        SafeArrayGetVartype(ids, &type) == S_OK && type == VT_I4 &&
        SafeArrayGetLBound(ids, 1, &first) == S_OK && SafeArrayGetUBound(ids, 1, &last) == S_OK) {
        for (LONG index = first; index <= last && read < 16; ++index) {
            LONG value = 0;
            if (SafeArrayGetElement(ids, &index, &value) != S_OK) break;
            const size_t used = strlen(values);
            snprintf(values + used, sizeof(values) - used, "%s%ld", read ? "," : "", value);
            ++read;
            if (index == LONG_MAX) break;
        }
    }
    output.write(false, "{\"kind\":\"uia_runtime_id\",\"hr\":\"0x%08lx\",\"vt\":%u,\"lower\":%ld,\"upper\":%ld,\"read\":%u,\"ids\":[%s]}",
        static_cast<unsigned long>(hr), type, first, last, read, values);
    if (ids) SafeArrayDestroy(ids);
}
void ReadUiaMetadata(State* s, IUIAutomationElement* element) {
    ReadRuntimeId(s, element);
    UiaProperty(s, element, UIA_NativeWindowHandlePropertyId, "native_hwnd", VT_I4);
    UiaProperty(s, element, UIA_ControlTypePropertyId, "control_type", VT_I4);
    UiaProperty(s, element, UIA_HasKeyboardFocusPropertyId, "has_keyboard_focus", VT_BOOL);
    UiaProperty(s, element, UIA_IsEnabledPropertyId, "is_enabled", VT_BOOL);
    UiaProperty(s, element, UIA_IsTextPatternAvailablePropertyId, "text_pattern_available", VT_BOOL);
    UiaProperty(s, element, UIA_IsTextPattern2AvailablePropertyId, "text_pattern2_available", VT_BOOL);
    UiaProperty(s, element, UIA_IsValuePatternAvailablePropertyId, "value_pattern_available", VT_BOOL);
    if (!s->call("uia", "GetCurrentPatternAs_TextPattern2")) return;
    Ref<IUIAutomationTextPattern2> text;
    HRESULT hr = element->GetCurrentPatternAs(UIA_TextPattern2Id, IID_IUIAutomationTextPattern2, reinterpret_cast<void**>(&text.p));
    Result("GetCurrentPatternAs_TextPattern2", hr, text.p != nullptr);
    if (hr == S_OK && text.p && s->call("uia", "GetCaretRange")) {
        BOOL active = FALSE;
        Ref<IUIAutomationTextRange> range;
        hr = text.p->GetCaretRange(&active, &range.p);
        output.write(false, "{\"kind\":\"uia_caret\",\"hr\":\"0x%08lx\",\"active\":%s,\"range_present\":%s}",
            static_cast<unsigned long>(hr), hr == S_OK ? (active ? "true" : "false") : "null", range.p ? "true" : "false");
        if (hr == S_OK && range.p && s->call("uia", "CaretRange_IsReadOnly")) {
            Variant value;
            const HRESULT attrHr = range.p->GetAttributeValue(UIA_IsReadOnlyAttributeId, &value.v);
            PrintScalar("uia", "caret_readonly", attrHr, value.v, VT_BOOL);
        }
    }
    if (!s->call("uia", "GetCurrentPatternAs_ValuePattern")) return;
    Ref<IUIAutomationValuePattern> value;
    hr = element->GetCurrentPatternAs(UIA_ValuePatternId, IID_IUIAutomationValuePattern, reinterpret_cast<void**>(&value.p));
    Result("GetCurrentPatternAs_ValuePattern", hr, value.p != nullptr);
    if (hr == S_OK && value.p && s->call("uia", "ValuePattern_CurrentIsReadOnly")) {
        BOOL readOnly = FALSE;
        hr = value.p->get_CurrentIsReadOnly(&readOnly);
        output.write(false, "{\"kind\":\"uia_value_readonly\",\"hr\":\"0x%08lx\",\"value\":%s}",
            static_cast<unsigned long>(hr), hr == S_OK ? (readOnly ? "true" : "false") : "null");
    }
}
void UiaCallback(State* s, IUIAutomationElement* sender, EVENTID event) {
    CallbackScope scope(s);
    if (!scope.locked || !sender) return;
    ++s->ownerChecks;
    int pid = 0;
    if (!s->call("uia", "sender_CachedProcessId")) return;
    HRESULT hr = sender->get_CachedProcessId(&pid);
    if (hr != S_OK) {
        if (!s->call("uia", "sender_CurrentProcessId")) return;
        hr = sender->get_CurrentProcessId(&pid);
    }
    // No other sender metadata is queried until this exact PID check succeeds.
    if (!scope.accept(hr, pid > 0 ? static_cast<DWORD>(pid) : 0)) return;
    if (event == UIA_AutomationFocusChangedEventId) ++s->focusEvents;
    if (event == UIA_Text_TextSelectionChangedEventId) ++s->selectionEvents;
    output.write(false, "{\"kind\":\"uia_event\",\"sequence\":%u,\"event\":%ld,\"target_pid\":%lu,\"tick\":%llu}",
        s->gate.used, static_cast<long>(event), s->gate.target, GetTickCount64());
    ReadUiaMetadata(s, sender);
    DWORD mainPid = 0;
    const DWORD tid = GetWindowThreadProcessId(s->main, &mainPid);
    if (tid && mainPid == s->gate.target) ReadGui(tid);
}
class UiaHandler final : public IUIAutomationFocusChangedEventHandler, public IUIAutomationEventHandler {
    std::atomic<ULONG> refs{1};
    State* state;
public:
    explicit UiaHandler(State* s) : state(s) { state->addRef(); }
    ~UiaHandler() { state->release(); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (iid == IID_IUnknown || iid == IID_IUIAutomationFocusChangedEventHandler)
            *result = static_cast<IUIAutomationFocusChangedEventHandler*>(this);
        else if (iid == IID_IUIAutomationEventHandler) *result = static_cast<IUIAutomationEventHandler*>(this);
        else return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { const ULONG n = --refs; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE HandleFocusChangedEvent(IUIAutomationElement* sender) override {
        UiaCallback(state, sender, UIA_AutomationFocusChangedEventId);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE HandleAutomationEvent(IUIAutomationElement* sender, EVENTID event) override {
        if (event == UIA_Text_TextSelectionChangedEventId) UiaCallback(state, sender, event);
        return S_OK;
    }
};

struct IA2Support {
    HRESULT service = E_PENDING, text = E_PENDING;
    bool object = false, textObject = false;
};
IA2Support QueryIA2(IServiceProvider* service, State* state = nullptr) {
    IA2Support result;
    Ref<IUnknown> ia2, text;
    if (state && !state->call("msaa", "QueryService_IAccessible2_support_only")) return result;
    result.service = service->QueryService(IID_IAccessible, kIAccessible2, reinterpret_cast<void**>(&ia2.p));
    result.object = ia2.p != nullptr;
    if (result.service == S_OK && ia2.p) {
        if (state && !state->call("msaa", "QI_IAccessibleText_support_only")) return result;
        result.text = ia2.p->QueryInterface(kIAccessibleText, reinterpret_cast<void**>(&text.p));
        result.textObject = text.p != nullptr;
    }
    return result;
}
void ReadIA2(State* s, IAccessible* object, const VARIANT& child) {
    Ref<IAccessible> resolved;
    if (child.vt != VT_I4) return;
    if (child.lVal != CHILDID_SELF) {
        if (!s->call("msaa", "get_accChild_for_IA2_identity")) return;
        Ref<IDispatch> dispatch;
        HRESULT hr = object->get_accChild(child, &dispatch.p);
        Result("get_accChild_for_IA2_identity", hr, dispatch.p != nullptr);
        if (hr != S_OK || !dispatch.p || !s->call("msaa", "child_QI_IAccessible")) return;
        hr = dispatch.p->QueryInterface(IID_IAccessible, reinterpret_cast<void**>(&resolved.p));
        Result("child_QI_IAccessible", hr, resolved.p != nullptr);
        if (hr != S_OK || !resolved.p) return;
        object = resolved.p;
        if (!s->call("msaa", "resolved_child_WindowFromAccessibleObject")) return;
        HWND owner = nullptr;
        hr = WindowFromAccessibleObject(object, &owner);
        DWORD ownerPid = 0;
        const DWORD ownerTid = owner ? GetWindowThreadProcessId(owner, &ownerPid) : 0;
        output.write(false, "{\"kind\":\"ia2_child_owner\",\"hr\":\"0x%08lx\",\"pid\":%lu,\"known\":%s}",
            static_cast<unsigned long>(hr), ownerPid, hr == S_OK && ownerTid ? "true" : "false");
        if (hr == S_OK && ownerTid && ownerPid != s->gate.target) return;
    }
    if (!s->call("msaa", "QI_IServiceProvider")) return;
    Ref<IServiceProvider> service;
    const HRESULT hr = object->QueryInterface(IID_IServiceProvider, reinterpret_cast<void**>(&service.p));
    Result("QI_IServiceProvider", hr, service.p != nullptr);
    if (hr != S_OK || !service.p) return;
    const IA2Support result = QueryIA2(service.p, s);
    output.write(false, "{\"kind\":\"ia2_support\",\"ia2_hr\":\"0x%08lx\",\"ia2_present\":%s,\"text_hr\":\"0x%08lx\",\"text_present\":%s,\"text_methods_called\":false}",
        static_cast<unsigned long>(result.service), result.object ? "true" : "false",
        static_cast<unsigned long>(result.text), result.textObject ? "true" : "false");
}
thread_local State* msaaState = nullptr;
bool InterestingMsaa(DWORD event, LONG object) {
    if (event == EVENT_OBJECT_FOCUS || event == EVENT_OBJECT_TEXTSELECTIONCHANGED) return true;
    return object == OBJID_CARET && (event == EVENT_OBJECT_CREATE || event == EVENT_OBJECT_DESTROY ||
        event == EVENT_OBJECT_SHOW || event == EVENT_OBJECT_HIDE || event == EVENT_OBJECT_LOCATIONCHANGE);
}
void CALLBACK MsaaCallback(HWINEVENTHOOK, DWORD event, HWND window, LONG objectId, LONG childId, DWORD tid, DWORD eventTime) {
    State* s = msaaState;
    if (!s || !InterestingMsaa(event, objectId)) return;
    CallbackScope scope(s);
    if (!scope.locked) return;
    ++s->ownerChecks;
    DWORD pid = 0;
    const DWORD actualTid = GetWindowThreadProcessId(window, &pid);
    if (!scope.accept(actualTid ? S_OK : E_FAIL, pid)) return;
    ++s->msaaEvents;
    output.write(false, "{\"kind\":\"msaa_event\",\"sequence\":%u,\"event\":%lu,\"hwnd\":\"%p\",\"object\":%ld,\"child\":%ld,\"thread\":%lu,\"event_time\":%lu,\"tick\":%llu,\"caret_source\":%s}",
        s->gate.used, event, static_cast<void*>(window), objectId, childId, tid, eventTime, GetTickCount64(), objectId == OBJID_CARET ? "true" : "false");
    ReadGui(actualTid);
    if (event == EVENT_OBJECT_DESTROY) return; // Never resolve a destroyed object.
    if (!s->call("msaa", "AccessibleObjectFromEvent")) return;
    Ref<IAccessible> object;
    Variant child;
    HRESULT hr = AccessibleObjectFromEvent(window, objectId, childId, &object.p, &child.v);
    Result("AccessibleObjectFromEvent", hr, object.p != nullptr);
    output.write(false, "{\"kind\":\"msaa_resolved_child\",\"vt\":%u,\"id\":%ld}", child.v.vt, child.v.vt == VT_I4 ? child.v.lVal : 0);
    if (hr != S_OK || !object.p || child.v.vt != VT_I4) return;
    if (!s->call("msaa", "WindowFromAccessibleObject")) return;
    HWND owner = nullptr;
    hr = WindowFromAccessibleObject(object.p, &owner);
    DWORD ownerPid = 0;
    const DWORD ownerTid = owner ? GetWindowThreadProcessId(owner, &ownerPid) : 0;
    output.write(false, "{\"kind\":\"msaa_object_owner\",\"hr\":\"0x%08lx\",\"hwnd\":\"%p\",\"pid\":%lu,\"known\":%s}",
        static_cast<unsigned long>(hr), static_cast<void*>(owner), ownerPid, hr == S_OK && ownerTid ? "true" : "false");
    if (hr == S_OK && ownerTid && ownerPid != s->gate.target) return;
    // An unresolved owner remains explicit unknown; the event itself was already
    // emitted by the PID-filtered target HWND. Never traverse a foreign object.
    if (s->call("msaa", "get_accRole")) {
        Variant role;
        hr = object.p->get_accRole(child.v, &role.v);
        PrintScalar("msaa", "role", hr, role.v, VT_I4);
    }
    if (s->call("msaa", "get_accState")) {
        Variant state;
        hr = object.p->get_accState(child.v, &state.v);
        PrintScalar("msaa", "state", hr, state.v, VT_I4);
    }
    ReadIA2(s, object.p, child.v);
}

struct Budget { HANDLE done = nullptr; DWORD milliseconds = 0; };
DWORD WINAPI Watchdog(void* parameter) {
    auto* budget = static_cast<Budget*>(parameter);
    if (WaitForSingleObject(budget->done, budget->milliseconds) != WAIT_OBJECT_0)
        TerminateProcess(GetCurrentProcess(), 124);
    return 0;
}
void Pump(State* s, bool ownProviderTest) {
    while (GetTickCount64() < s->gate.deadline && s->targetAlive()) {
        if (ownProviderTest && s->selectionEvents.load()) break;
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 25, QS_ALLINPUT);
        MSG message;
        unsigned count = 0;
        while (count++ < 32 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) return;
            DispatchMessageW(&message); // No TranslateMessage or synthetic input.
        }
    }
}
int Observe(DWORD pid, HWND main, DWORD seconds, bool ownProviderTest = false) {
    auto* s = new State;
    s->gate = {pid, GetTickCount64() + seconds * 1000ULL, 0, false};
    s->main = main;
    s->targetProcess = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!s->targetProcess || !s->targetAlive()) {
        output.write(true, "{\"kind\":\"invalid_target\",\"pid\":%lu,\"main\":\"%p\",\"error\":%lu}", pid, static_cast<void*>(main), GetLastError());
        s->release();
        return 3;
    }
    output.write(true, "{\"kind\":\"start\",\"pid\":%lu,\"main\":\"%p\",\"seconds\":%lu,\"max_events\":%u,\"max_owner_checks\":%u,\"max_calls\":%u,\"max_regular_rows\":%u,\"own_generated_provider_test\":%s}",
        pid, static_cast<void*>(main), seconds, kMaxEvents, kMaxOwnerChecks, kMaxCalls, kMaxRows, ownProviderTest ? "true" : "false");
    Ref<IUIAutomation> automation;
    HRESULT hr = CoCreateInstance(CLSID_CUIAutomation8, nullptr, CLSCTX_INPROC_SERVER, IID_IUIAutomation, reinterpret_cast<void**>(&automation.p));
    Result("CoCreateInstance_CUIAutomation8", hr, automation.p != nullptr, true);
    bool setupOk = hr == S_OK && automation.p;
    Ref<IUIAutomation2> automation2;
    if (setupOk) {
        hr = automation.p->QueryInterface(IID_IUIAutomation2, reinterpret_cast<void**>(&automation2.p));
        Result("QI_IUIAutomation2", hr, automation2.p != nullptr, true);
        if (hr == S_OK && automation2.p) {
            const HRESULT connection = automation2.p->put_ConnectionTimeout(500);
            const HRESULT transaction = automation2.p->put_TransactionTimeout(500);
            Result("put_ConnectionTimeout_500ms", connection, false, true);
            Result("put_TransactionTimeout_500ms", transaction, false, true);
            setupOk = connection == S_OK && transaction == S_OK;
        } else setupOk = false;
    }
    Ref<IUIAutomationCacheRequest> cache;
    Ref<IUIAutomationElement> root;
    if (setupOk) {
        hr = automation.p->CreateCacheRequest(&cache.p);
        Result("CreateCacheRequest", hr, cache.p != nullptr, true);
        setupOk = hr == S_OK && cache.p;
    }
    if (setupOk) {
        hr = cache.p->AddProperty(UIA_ProcessIdPropertyId); // ONLY owner PID, before all other metadata.
        Result("Cache_AddProcessId_only", hr, false, true);
        setupOk = hr == S_OK;
    }
    if (setupOk) {
        hr = automation.p->ElementFromHandle(main, &root.p);
        Result("ElementFromHandle_main", hr, root.p != nullptr, true);
        setupOk = hr == S_OK && root.p;
    }
    if (setupOk) {
        int rootPid = 0;
        hr = root.p->get_CurrentProcessId(&rootPid);
        Result("root_owner_matches", hr, hr == S_OK && rootPid > 0 && static_cast<DWORD>(rootPid) == pid, true);
        setupOk = hr == S_OK && rootPid > 0 && static_cast<DWORD>(rootPid) == pid;
    }
    auto* handler = new UiaHandler(s);
    bool focusAdded = false, selectionAdded = false;
    if (setupOk) {
        hr = automation.p->AddFocusChangedEventHandler(cache.p, handler);
        focusAdded = hr == S_OK;
        Result("AddFocusChangedEventHandler_global_PID_filtered", hr, focusAdded, true);
        hr = automation.p->AddAutomationEventHandler(UIA_Text_TextSelectionChangedEventId, root.p, TreeScope_Subtree, cache.p, handler);
        selectionAdded = hr == S_OK;
        Result("AddSelectionEventHandler_target_subtree", hr, selectionAdded, true);
    }
    msaaState = s;
    SetLastError(0);
    HWINEVENTHOOK hook = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_TEXTSELECTIONCHANGED, nullptr,
        MsaaCallback, pid, 0, WINEVENT_OUTOFCONTEXT);
    const DWORD hookError = hook ? 0 : GetLastError();
    output.write(true, "{\"kind\":\"subscription\",\"operation\":\"SetWinEventHook_target_PID_OUTOFCONTEXT\",\"ok\":%s,\"error\":%lu}", hook ? "true" : "false", hookError);
    setupOk = setupOk && focusAdded && selectionAdded && hook;
    output.write(true, "{\"kind\":\"ready\",\"all_subscriptions_ok\":%s,\"focus_scope\":\"global_PID_filtered\",\"selection_scope\":\"main_subtree\",\"routing_verdict\":false}", setupOk ? "true" : "false");
    if (ownProviderTest && selectionAdded) PostMessageW(main, WM_APP + 1, 0, 0);
    if (focusAdded || selectionAdded || hook) Pump(s, ownProviderTest);

    s->stopping = true; // Late callbacks retain State but cannot query providers.
    unsigned cleanupFailed = 0;
    if (hook) {
        SetLastError(0);
        const BOOL ok = UnhookWinEvent(hook);
        const DWORD error = ok ? 0 : GetLastError();
        output.write(true, "{\"kind\":\"cleanup\",\"operation\":\"UnhookWinEvent\",\"ok\":%s,\"error\":%lu}", ok ? "true" : "false", error);
        if (!ok) ++cleanupFailed;
    }
    msaaState = nullptr;
    if (selectionAdded) {
        hr = automation.p->RemoveAutomationEventHandler(UIA_Text_TextSelectionChangedEventId, root.p, handler);
        Result("RemoveSelectionEventHandler", hr, false, true);
        if (hr != S_OK) ++cleanupFailed;
    }
    if (focusAdded) {
        hr = automation.p->RemoveFocusChangedEventHandler(handler);
        Result("RemoveFocusChangedEventHandler", hr, false, true);
        if (hr != S_OK) ++cleanupFailed;
    }
    if (automation.p) {
        hr = automation.p->RemoveAllEventHandlers();
        Result("RemoveAllEventHandlers_own_client", hr, false, true);
        if (hr != S_OK) ++cleanupFailed;
    }
    // Handler/state remain COM-refcounted after unsubscribe; no stack userdata.
    const ULONG handlerRefs = handler->Release();
    output.write(true, "{\"kind\":\"cleanup\",\"operation\":\"release_own_handler_reference\",\"remaining_COM_refs\":%lu,\"late_callbacks_remain_refcounted\":true}", handlerRefs);
    const ULONGLONG drainEnd = GetTickCount64() + 1000;
    while (s->active.load() && GetTickCount64() < drainEnd) Sleep(5);
    const bool activeZero = s->active.load() == 0;
    if (!activeZero) ++cleanupFailed;
    const unsigned actualSelection = s->selectionEvents.load();
    output.write(true, "{\"kind\":\"complete\",\"events\":%u,\"uia_focus\":%u,\"uia_selection\":%u,\"msaa\":%u,\"foreign_filtered\":%u,\"owner_unknown\":%u,\"busy_dropped\":%u,\"budget_dropped\":%u,\"output_dropped\":%u,\"active_callbacks_zero\":%s,\"cleanup_failures\":%u,\"first_key_order_guaranteed\":false,\"no_events_means_nontext\":false}",
        s->gate.used, s->focusEvents.load(), actualSelection, s->msaaEvents.load(), s->foreign.load(), s->unknown.load(), s->busy.load(), s->ignored.load(), output.dropped.load(), activeZero ? "true" : "false", cleanupFailed);
    s->release();
    if (cleanupFailed) return 6;
    if (!setupOk) return 4;
    if (ownProviderTest && !actualSelection) return 5;
    return 0;
}

// This provider owns no text. Its explicit notification exercises the Windows
// subscription transport only, not natural focus, editing, or GetCaretRange.
class FixtureProvider final : public IRawElementProviderSimple {
    std::atomic<ULONG> refs{1};
public:
    HWND window = nullptr;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (iid != IID_IUnknown && iid != IID_IRawElementProviderSimple) return E_NOINTERFACE;
        *result = static_cast<IRawElementProviderSimple*>(this);
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { const ULONG n = --refs; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* result) override {
        if (!result) return E_POINTER;
        *result = ProviderOptions_ServerSideProvider;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID, IUnknown** result) override {
        if (!result) return E_POINTER;
        *result = nullptr; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID property, VARIANT* value) override {
        if (!value) return E_POINTER;
        VariantInit(value);
        if (property == UIA_ProcessIdPropertyId || property == UIA_ControlTypePropertyId || property == UIA_NativeWindowHandlePropertyId) {
            value->vt = VT_I4;
            value->lVal = property == UIA_ProcessIdPropertyId ? static_cast<LONG>(GetCurrentProcessId()) :
                property == UIA_ControlTypePropertyId ? UIA_EditControlTypeId : static_cast<LONG>(reinterpret_cast<LONG_PTR>(window));
        } else if (property == UIA_HasKeyboardFocusPropertyId || property == UIA_IsEnabledPropertyId ||
                   property == UIA_IsTextPatternAvailablePropertyId || property == UIA_IsTextPattern2AvailablePropertyId || property == UIA_IsValuePatternAvailablePropertyId) {
            value->vt = VT_BOOL;
            value->boolVal = property == UIA_IsEnabledPropertyId ? VARIANT_TRUE : VARIANT_FALSE;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** result) override {
        return UiaHostProviderFromHwnd(window, result);
    }
};
struct ProviderFixture {
    HANDLE ready = nullptr, stop = nullptr;
    HWND window = nullptr;
    HRESULT raised = E_PENDING;
    bool hidden = false;
    unsigned cleanupFailed = 0;
};
LRESULT CALLBACK FixtureWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* provider = reinterpret_cast<FixtureProvider*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        provider = static_cast<FixtureProvider*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        provider->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(provider));
    }
    if (message == WM_GETOBJECT && static_cast<LONG>(lParam) == UiaRootObjectId && provider)
        return UiaReturnRawElementProvider(window, wParam, lParam, provider);
    if (message == WM_APP + 1 && provider) {
        const HRESULT hr = UiaRaiseAutomationEvent(provider, UIA_Text_TextSelectionChangedEventId);
        SetWindowLongPtrW(window, 0, static_cast<LONG_PTR>(hr));
        output.write(true, "{\"kind\":\"own_provider_notification\",\"hr\":\"0x%08lx\",\"natural_focus_event\":false,\"text_changed\":false}", static_cast<unsigned long>(hr));
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
DWORD WINAPI ProviderThread(void* parameter) {
    auto* fixture = static_cast<ProviderFixture*>(parameter);
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const HINSTANCE module = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.lpfnWndProc = FixtureWindowProc;
    wc.hInstance = module;
    wc.lpszClassName = L"RiumReadOnlyEventProviderFixture";
    wc.cbWndExtra = sizeof(LONG_PTR);
    const ATOM atom = RegisterClassW(&wc);
    auto* provider = new FixtureProvider;
    if (atom) fixture->window = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, wc.lpszClassName, L"", WS_POPUP,
        0, 0, 20, 20, nullptr, nullptr, module, provider);
    fixture->hidden = fixture->window && !IsWindowVisible(fixture->window);
    if (fixture->window) SetWindowLongPtrW(fixture->window, 0, static_cast<LONG_PTR>(E_PENDING));
    SetEvent(fixture->ready);
    while (WaitForSingleObject(fixture->stop, 0) != WAIT_OBJECT_0) {
        MsgWaitForMultipleObjects(1, &fixture->stop, FALSE, 25, QS_ALLINPUT);
        MSG message;
        unsigned count = 0;
        while (count++ < 32 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
    }
    if (fixture->window) {
        fixture->raised = static_cast<HRESULT>(GetWindowLongPtrW(fixture->window, 0));
        const HRESULT disconnected = UiaDisconnectProvider(provider);
        Result("fixture_UiaDisconnectProvider", disconnected, false, true);
        if (disconnected != S_OK) ++fixture->cleanupFailed;
        UiaReturnRawElementProvider(fixture->window, 0, 0, nullptr);
        const BOOL destroyed = DestroyWindow(fixture->window);
        output.write(true, "{\"kind\":\"cleanup\",\"operation\":\"fixture_DestroyWindow\",\"ok\":%s}", destroyed ? "true" : "false");
        if (!destroyed) ++fixture->cleanupFailed;
    }
    provider->Release();
    if (atom && !UnregisterClassW(wc.lpszClassName, module)) ++fixture->cleanupFailed;
    if (SUCCEEDED(initialized)) CoUninitialize();
    return 0;
}
int OwnProviderTest() {
    const HWND initialForeground = GetForegroundWindow();
    ProviderFixture fixture;
    fixture.ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    fixture.stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!fixture.ready || !fixture.stop) {
        if (fixture.ready) CloseHandle(fixture.ready);
        if (fixture.stop) CloseHandle(fixture.stop);
        return 5;
    }
    HANDLE thread = CreateThread(nullptr, 0, ProviderThread, &fixture, 0, nullptr);
    if (!thread) { CloseHandle(fixture.ready); CloseHandle(fixture.stop); return 5; }
    int result = 5;
    if (WaitForSingleObject(fixture.ready, 2000) == WAIT_OBJECT_0 && fixture.window && fixture.hidden)
        result = Observe(GetCurrentProcessId(), fixture.window, 3, true);
    SetEvent(fixture.stop);
    WaitForSingleObject(thread, INFINITE); // Whole process watchdog is still armed.
    CloseHandle(thread);
    CloseHandle(fixture.ready);
    CloseHandle(fixture.stop);
    const bool sameForeground = initialForeground == GetForegroundWindow();
    const bool passed = result == 0 && fixture.hidden && fixture.raised == S_OK && sameForeground && fixture.cleanupFailed == 0;
    output.write(true, "{\"kind\":\"own_provider_test_summary\",\"passed\":%s,\"actual_UIA_transport_tested\":true,\"natural_focus_tested\":false,\"real_app_tested\":false,\"hidden\":%s,\"foreground_unchanged\":%s,\"cleanup_failures\":%u}",
        passed ? "true" : "false", fixture.hidden ? "true" : "false", sameForeground ? "true" : "false", fixture.cleanupFailed);
    return passed ? 0 : 5;
}

class FixtureService final : public IServiceProvider {
public:
    ULONG refs = 1;
    bool matched = false;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (iid != IID_IUnknown && iid != IID_IServiceProvider && iid != kIAccessibleText) return E_NOINTERFACE;
        *result = static_cast<IServiceProvider*>(this);
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { return --refs; }
    HRESULT STDMETHODCALLTYPE QueryService(REFGUID service, REFIID iid, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        matched = service == IID_IAccessible && iid == kIAccessible2;
        if (!matched) return E_NOINTERFACE;
        *result = static_cast<IUnknown*>(static_cast<IServiceProvider*>(this));
        AddRef(); return S_OK;
    }
};
int FixtureTests() {
    unsigned checks = 0, failed = 0;
    const auto check = [&](bool ok, const char* name) {
        ++checks;
        if (!ok) ++failed;
        printf("{\"kind\":\"callback_fixture\",\"name\":\"%s\",\"passed\":%s}\n", name, ok ? "true" : "false");
    };
    EventGate gate{42, 1000, 0, false};
    check(!gate.accept(S_OK, 99, 10), "foreign_pid_is_rejected_before_metadata");
    check(!gate.accept(E_FAIL, 42, 10), "failed_owner_query_is_unknown");
    check(!gate.accept(S_FALSE, 42, 10), "s_false_owner_query_is_unknown");
    check(gate.accept(S_OK, 42, 10), "exact_target_event_is_accepted");
    gate.stopping = true;
    check(!gate.accept(S_OK, 42, 10), "late_callback_after_stop_is_ignored");
    gate.stopping = false;
    check(!gate.accept(S_OK, 42, 1000), "deadline_is_exclusive");
    gate.used = 128;
    check(!gate.accept(S_OK, 42, 10), "metadata_event_budget_is_bounded");
    check(gate.used == 128, "rejected_event_does_not_consume_more_budget");
    check(InterestingMsaa(EVENT_OBJECT_FOCUS, OBJID_CLIENT), "focus_event_is_interesting");
    check(InterestingMsaa(EVENT_OBJECT_LOCATIONCHANGE, OBJID_CARET), "caret_location_is_interesting");
    check(!InterestingMsaa(EVENT_OBJECT_LOCATIONCHANGE, OBJID_CLIENT), "noncaret_location_is_ignored");
    check(!InterestingMsaa(EVENT_OBJECT_NAMECHANGE, OBJID_CLIENT), "name_change_is_never_inspected");
    Variant value;
    value.v.vt = VT_BOOL;
    value.v.boolVal = VARIANT_FALSE;
    char buffer[32];
    check(strcmp(Scalar(S_OK, value.v, VT_BOOL, buffer), "false") == 0, "supported_false_is_false");
    check(strcmp(Scalar(E_FAIL, value.v, VT_BOOL, buffer), "null") == 0, "failed_false_is_unknown");
    check(strcmp(Scalar(S_FALSE, value.v, VT_BOOL, buffer), "null") == 0, "s_false_is_unknown");
    value.v.vt = VT_EMPTY;
    check(strcmp(Scalar(S_OK, value.v, VT_BOOL, buffer), "null") == 0, "empty_is_unknown");
    FixtureService service;
    const IA2Support support = QueryIA2(&service);
    check(service.matched, "IA2_discovery_uses_verified_service_and_interface_UUIDs");
    check(support.service == S_OK && support.object && support.text == S_OK && support.textObject,
        "IA2_support_detection_only_uses_IUnknown_contract");
    check(service.refs == 1, "IA2_detection_releases_both_acquired_interfaces");
    printf("{\"kind\":\"fixture_summary\",\"checks\":%u,\"failed\":%u,\"actual_provider_events_tested\":false}\n", checks, failed);
    return failed ? 5 : 0;
}
}
int wmain(int argc, wchar_t** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc == 2 && wcscmp(argv[1], L"--self-test-fixtures") == 0) return FixtureTests();
    if (argc == 2 && wcscmp(argv[1], L"--self-test-watchdog") == 0) {
        Budget fault{CreateEventW(nullptr, TRUE, FALSE, nullptr), 100};
        if (!fault.done) return 4;
        output.write(true, "{\"kind\":\"watchdog_fixture\",\"expected_exit\":124,\"target\":\"self_only\"}");
        Watchdog(&fault); // Deliberately unsignaled own event; must exit 124.
        return 5;
    }
    const bool providerTest = argc == 2 && wcscmp(argv[1], L"--self-test-own-provider") == 0;
    DWORD pid = 0, seconds = providerTest ? 6 : 60;
    HWND window = nullptr;
    if (!providerTest) {
        if (argc < 3 || argc > 4) {
            fprintf(stderr, "Usage: event-observer.exe <PID> <MAIN-HWND> [seconds:1..60]\n");
            return 2;
        }
        wchar_t* end = nullptr;
        errno = 0;
        const unsigned long long parsedPid = wcstoull(argv[1], &end, 0);
        if (errno || !end || *end || !parsedPid || parsedPid > MAXDWORD || argv[1][0] == L'-') return 2;
        pid = static_cast<DWORD>(parsedPid);
        errno = 0;
        const unsigned long long parsedWindow = wcstoull(argv[2], &end, 0);
        if (errno || !end || *end || !parsedWindow || parsedWindow > UINTPTR_MAX || argv[2][0] == L'-') return 2;
        window = reinterpret_cast<HWND>(static_cast<uintptr_t>(parsedWindow));
        if (argc == 4) {
            errno = 0;
            const unsigned long parsedSeconds = wcstoul(argv[3], &end, 10);
            if (errno || !end || *end || parsedSeconds < 1 || parsedSeconds > 60 || argv[3][0] == L'-') return 2;
            seconds = parsedSeconds;
        }
    }
    Budget budget{CreateEventW(nullptr, TRUE, FALSE, nullptr), seconds * 1000 + kCleanupGraceMs};
    if (!budget.done) return 4;
    HANDLE watchdog = CreateThread(nullptr, 0, Watchdog, &budget, 0, nullptr);
    if (!watchdog) { CloseHandle(budget.done); return 4; }
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Result("CoInitializeEx_MTA", initialized, SUCCEEDED(initialized), true);
    int result = 4;
    if (SUCCEEDED(initialized)) {
        result = providerTest ? OwnProviderTest() : Observe(pid, window, seconds);
        CoUninitialize();
    }
    SetEvent(budget.done);
    WaitForSingleObject(watchdog, INFINITE);
    CloseHandle(watchdog);
    CloseHandle(budget.done);
    return result;
}
