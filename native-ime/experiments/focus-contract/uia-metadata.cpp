#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <uiautomationclient.h>
#include <cstdio>
#include <cstring>
#include "uia-metadata.h"

namespace {
template<class T> class Ref {
public:
    T* p = nullptr;
    Ref() = default;
    ~Ref() { if (p) p->Release(); }
    Ref(const Ref&) = delete;
    Ref& operator=(const Ref&) = delete;
};
class Value {
public:
    VARIANT v;
    Value() { VariantInit(&v); }
    ~Value() { VariantClear(&v); }
    Value(const Value&) = delete;
    Value& operator=(const Value&) = delete;
};
void Call(const char* path, const char* operation) {
    printf("{\"kind\":\"uia_call\",\"path\":\"%s\",\"operation\":\"%s\"}\n", path, operation);
}
void Result(const char* path, const char* operation, HRESULT hr, bool present) {
    printf("{\"kind\":\"uia_result\",\"path\":\"%s\",\"operation\":\"%s\","
           "\"hr\":\"0x%08lx\",\"object_present\":%s}\n",
           path, operation, static_cast<unsigned long>(hr), present ? "true" : "false");
}
bool SameUnknown(IUnknown* first, IUnknown* second) {
    if (!first || !second) return false;
    Ref<IUnknown> left, right;
    const HRESULT a = first->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&left.p));
    const HRESULT b = second->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&right.p));
    return SUCCEEDED(a) && SUCCEEDED(b) && left.p && left.p == right.p;
}
struct Sentinels {
    Ref<IUnknown> unsupported, mixed;
    HRESULT unsupportedHr = E_PENDING, mixedHr = E_PENDING;
    void read(IUIAutomation* automation) {
        Call("uia", "get_ReservedNotSupportedValue");
        unsupportedHr = automation->get_ReservedNotSupportedValue(&unsupported.p);
        Result("uia", "get_ReservedNotSupportedValue", unsupportedHr, unsupported.p != nullptr);
        Call("uia", "get_ReservedMixedAttributeValue");
        mixedHr = automation->get_ReservedMixedAttributeValue(&mixed.p);
        Result("uia", "get_ReservedMixedAttributeValue", mixedHr, mixed.p != nullptr);
    }
};
struct BooleanValue { const char* status; const char* json; };
BooleanValue Classify(HRESULT hr, const VARIANT& value, const Sentinels& sentinels) {
    if (hr != S_OK) return {"query_failed", "null"};
    if (value.vt == VT_BOOL) return {"supported", value.boolVal == VARIANT_FALSE ? "false" : "true"};
    if (value.vt == VT_UNKNOWN && value.punkVal) {
        if (SUCCEEDED(sentinels.unsupportedHr) && SameUnknown(value.punkVal, sentinels.unsupported.p))
            return {"unsupported", "null"};
        if (SUCCEEDED(sentinels.mixedHr) && SameUnknown(value.punkVal, sentinels.mixed.p))
            return {"mixed", "null"};
        return {"unrecognized_unknown", "null"};
    }
    return {value.vt == VT_EMPTY ? "empty" : "unexpected_variant_type", "null"};
}
BooleanValue PrintBoolean(const char* path, const char* attribute, HRESULT hr, const VARIANT& value, const Sentinels& sentinels) {
    const BooleanValue result = Classify(hr, value, sentinels);
    printf("{\"kind\":\"uia_boolean\",\"path\":\"%s\",\"attribute\":\"%s\","
           "\"hr\":\"0x%08lx\",\"vt\":%u,\"status\":\"%s\",\"value\":%s}\n",
           path, attribute, static_cast<unsigned long>(hr), value.vt, result.status, result.json);
    return result;
}
void ReadBooleanProperty(IUIAutomationElement* element, PROPERTYID property, const char* name,
                         const char* path, const Sentinels& sentinels) {
    Value value;
    Call(path, name);
    const HRESULT hr = element->GetCurrentPropertyValueEx(property, TRUE, &value.v);
    PrintBoolean(path, name, hr, value.v, sentinels);
}
struct PatternSummary {
    HRESULT ownerHr = E_PENDING, controlTypeHr = E_PENDING, valuePatternHr = E_PENDING, readonlyHr = E_PENDING;
    int ownerPid = 0, controlType = 0;
    bool owned = false, valuePresent = false;
    BOOL valueReadOnly = FALSE;
};
PatternSummary Inspect(IUIAutomationElement* element, DWORD targetPid, const char* path, const Sentinels& sentinels) {
    PatternSummary summary;
    Call(path, "get_CurrentProcessId");
    summary.ownerHr = element->get_CurrentProcessId(&summary.ownerPid);
    summary.owned = summary.ownerHr == S_OK && summary.ownerPid > 0 && static_cast<DWORD>(summary.ownerPid) == targetPid;
    printf("{\"kind\":\"uia_owner\",\"path\":\"%s\",\"hr\":\"0x%08lx\",\"pid\":%d,\"target_match\":%s}\n",
           path, static_cast<unsigned long>(summary.ownerHr), summary.ownerPid, summary.owned ? "true" : "false");
    if (!summary.owned) return summary;
    Call(path, "get_CurrentControlType");
    summary.controlTypeHr = element->get_CurrentControlType(&summary.controlType);
    printf("{\"kind\":\"uia_control_type\",\"path\":\"%s\",\"hr\":\"0x%08lx\",\"id\":%d}\n",
           path, static_cast<unsigned long>(summary.controlTypeHr), summary.controlType);
    ReadBooleanProperty(element, UIA_HasKeyboardFocusPropertyId, "has_keyboard_focus", path, sentinels);
    ReadBooleanProperty(element, UIA_IsTextPattern2AvailablePropertyId, "text_pattern2_available", path, sentinels);
    ReadBooleanProperty(element, UIA_IsValuePatternAvailablePropertyId, "value_pattern_available", path, sentinels);

    Ref<IUIAutomationTextPattern2> text;
    Call(path, "GetCurrentPatternAs_TextPattern2");
    const HRESULT textHr = element->GetCurrentPatternAs(UIA_TextPattern2Id, IID_IUIAutomationTextPattern2,
                                                       reinterpret_cast<void**>(&text.p));
    Result(path, "GetCurrentPatternAs_TextPattern2", textHr, text.p != nullptr);
    if (textHr == S_OK && text.p) {
        BOOL active = FALSE;
        Ref<IUIAutomationTextRange> range;
        Call(path, "TextPattern2_GetCaretRange");
        const HRESULT caretHr = text.p->GetCaretRange(&active, &range.p);
        printf("{\"kind\":\"uia_caret\",\"path\":\"%s\",\"hr\":\"0x%08lx\","
               "\"is_active\":%s,\"range_present\":%s}\n",
               path, static_cast<unsigned long>(caretHr), caretHr == S_OK ? (active ? "true" : "false") : "null",
               range.p ? "true" : "false");
        if (caretHr == S_OK && range.p) {
            Value readOnly;
            Call(path, "CaretRange_GetAttributeValue_IsReadOnly");
            const HRESULT hr = range.p->GetAttributeValue(UIA_IsReadOnlyAttributeId, &readOnly.v);
            PrintBoolean(path, "caret_range_readonly", hr, readOnly.v, sentinels);
        }
    }

    Ref<IUIAutomationValuePattern> value;
    Call(path, "GetCurrentPatternAs_ValuePattern");
    summary.valuePatternHr = element->GetCurrentPatternAs(UIA_ValuePatternId, IID_IUIAutomationValuePattern,
                                                         reinterpret_cast<void**>(&value.p));
    summary.valuePresent = value.p != nullptr;
    Result(path, "GetCurrentPatternAs_ValuePattern", summary.valuePatternHr, summary.valuePresent);
    if (summary.valuePatternHr == S_OK && value.p) {
        Call(path, "ValuePattern_get_CurrentIsReadOnly");
        summary.readonlyHr = value.p->get_CurrentIsReadOnly(&summary.valueReadOnly);
        printf("{\"kind\":\"uia_value_readonly\",\"path\":\"%s\",\"hr\":\"0x%08lx\",\"value\":%s}\n",
               path, static_cast<unsigned long>(summary.readonlyHr),
               summary.readonlyHr == S_OK ? (summary.valueReadOnly ? "true" : "false") : "null");
    }
    return summary;
}
PatternSummary InspectHandle(IUIAutomation* automation, DWORD targetPid, HWND window, const char* path, const Sentinels& sentinels) {
    Ref<IUIAutomationElement> element;
    Call(path, "ElementFromHandle");
    const HRESULT hr = automation->ElementFromHandle(window, &element.p);
    Result(path, "ElementFromHandle", hr, element.p != nullptr);
    if (hr == S_OK && element.p) return Inspect(element.p, targetPid, path, sentinels);
    return {};
}
HRESULT CreateAutomation(Ref<IUIAutomation>& automation) {
    Call("uia", "CoCreateInstance_CUIAutomation");
    const HRESULT hr = CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_IUIAutomation,
                                        reinterpret_cast<void**>(&automation.p));
    Result("uia", "CoCreateInstance_CUIAutomation", hr, automation.p != nullptr);
    return hr;
}

struct Fixture {
    HANDLE ready = nullptr, stop = nullptr;
    HWND edit = nullptr, readOnly = nullptr, label = nullptr;
};
DWORD WINAPI FixtureThread(void* argument) {
    auto* fixture = static_cast<Fixture*>(argument);
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const HINSTANCE module = GetModuleHandleW(nullptr);
    fixture->edit = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"EDIT", L"", WS_POPUP,
                                    0, 0, 20, 20, nullptr, nullptr, module, nullptr);
    fixture->readOnly = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"EDIT", L"", WS_POPUP | ES_READONLY,
                                        0, 0, 20, 20, nullptr, nullptr, module, nullptr);
    fixture->label = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"STATIC", L"", WS_POPUP,
                                     0, 0, 20, 20, nullptr, nullptr, module, nullptr);
    SetEvent(fixture->ready);
    for (;;) {
        const DWORD wait = MsgWaitForMultipleObjects(1, &fixture->stop, FALSE, INFINITE, QS_ALLINPUT);
        if (wait != WAIT_OBJECT_0 + 1) break;
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            // No TranslateMessage: this fixture never creates character input.
            DispatchMessageW(&message);
        }
    }
    if (fixture->label) DestroyWindow(fixture->label);
    if (fixture->readOnly) DestroyWindow(fixture->readOnly);
    if (fixture->edit) DestroyWindow(fixture->edit);
    if (SUCCEEDED(initialized)) CoUninitialize();
    return 0;
}
} // namespace

void ReadUiaSnapshot(DWORD targetPid, HWND mainWindow, HWND focusedWindow, bool targetForeground) {
    Ref<IUIAutomation> automation;
    if (CreateAutomation(automation) != S_OK || !automation.p) return;
    Sentinels sentinels;
    sentinels.read(automation.p);
    // Prioritize the actual GUI focus HWND, as the main window provider may stall.
    if (focusedWindow) InspectHandle(automation.p, targetPid, focusedWindow, "uia_focus_hwnd", sentinels);
    if (targetForeground) {
        Ref<IUIAutomationElement> focused;
        Call("uia_focused_element", "GetFocusedElement");
        const HRESULT hr = automation.p->GetFocusedElement(&focused.p);
        Result("uia_focused_element", "GetFocusedElement", hr, focused.p != nullptr);
        if (hr == S_OK && focused.p) Inspect(focused.p, targetPid, "uia_focused_element", sentinels);
    }
    if (mainWindow != focusedWindow) InspectHandle(automation.p, targetPid, mainWindow, "uia_main_hwnd", sentinels);
}

int UiaSelfTest() {
    unsigned checks = 0, failures = 0;
    const auto check = [&](bool passed, const char* name) {
        ++checks;
        if (!passed) ++failures;
        printf("{\"kind\":\"uia_test\",\"name\":\"%s\",\"passed\":%s}\n", name, passed ? "true" : "false");
    };
    Ref<IUIAutomation> automation;
    if (CreateAutomation(automation) != S_OK || !automation.p) return 5;
    Sentinels sentinels;
    sentinels.read(automation.p);
    check(sentinels.unsupportedHr == S_OK && sentinels.unsupported.p, "unsupported_sentinel_available");
    check(sentinels.mixedHr == S_OK && sentinels.mixed.p, "mixed_sentinel_available");
    VARIANT test;
    VariantInit(&test);
    test.vt = VT_BOOL;
    test.boolVal = VARIANT_FALSE;
    check(strcmp(Classify(S_OK, test, sentinels).json, "false") == 0, "supported_false_preserved");
    check(strcmp(Classify(E_FAIL, test, sentinels).json, "null") == 0, "failed_query_is_unknown");
    test.vt = VT_UNKNOWN;
    test.punkVal = sentinels.unsupported.p; // Borrowed references; no VariantClear.
    check(strcmp(Classify(S_OK, test, sentinels).status, "unsupported") == 0, "unsupported_not_coerced_false");
    test.punkVal = sentinels.mixed.p;
    check(strcmp(Classify(S_OK, test, sentinels).status, "mixed") == 0, "mixed_not_coerced_false");
    Fixture fixture;
    fixture.ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    fixture.stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!fixture.ready || !fixture.stop) {
        if (fixture.ready) CloseHandle(fixture.ready);
        if (fixture.stop) CloseHandle(fixture.stop);
        return 5;
    }
    HANDLE thread = CreateThread(nullptr, 0, FixtureThread, &fixture, 0, nullptr);
    if (!thread) { CloseHandle(fixture.ready); CloseHandle(fixture.stop); return 5; }
    if (WaitForSingleObject(fixture.ready, 3000) == WAIT_OBJECT_0) {
        const HWND windows[] = {fixture.edit, fixture.readOnly, fixture.label};
        const char* paths[] = {"uia_fixture_edit", "uia_fixture_readonly", "uia_fixture_static"};
        for (unsigned i = 0; i < 3; ++i) {
            check(windows[i] && !IsWindowVisible(windows[i]), "fixture_hidden_without_focus");
            if (!windows[i]) continue;
            const PatternSummary result = InspectHandle(automation.p, GetCurrentProcessId(), windows[i], paths[i], sentinels);
            check(result.owned, "fixture_process_matches");
            check(result.controlTypeHr == S_OK && result.controlType == (i == 2 ? UIA_TextControlTypeId : UIA_EditControlTypeId),
                  "fixture_expected_control_type");
            if (i != 2) check(result.valuePatternHr == S_OK && result.valuePresent && result.readonlyHr == S_OK &&
                              (result.valueReadOnly != FALSE) == (i == 1), "fixture_expected_value_readonly");
        }
    } else check(false, "fixture_ready_before_deadline");
    SetEvent(fixture.stop);
    WaitForSingleObject(thread, INFINITE); // Entire process remains under watchdog.
    CloseHandle(thread);
    CloseHandle(fixture.ready);
    CloseHandle(fixture.stop);
    printf("{\"kind\":\"uia_test_summary\",\"checks\":%u,\"failures\":%u}\n", checks, failures);
    return failures ? 5 : 0;
}
