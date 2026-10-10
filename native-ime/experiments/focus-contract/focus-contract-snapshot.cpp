#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <oleacc.h>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstdint>
#include <cwchar>
#include "uia-metadata.h"

// Disposable, read-only metadata diagnostic. No names, values, document text,
// input injection, accessibility actions, focus changes, or selection changes.
// COM calls can block, so a watchdog terminates only this diagnostic process.
namespace {
constexpr unsigned kMaxDepth = 8;
constexpr DWORD kDefaultBudgetMs = 3000;
struct Budget { HANDLE done; DWORD milliseconds; };
DWORD WINAPI Watchdog(void* argument) {
    const auto* budget = static_cast<const Budget*>(argument);
    if (WaitForSingleObject(budget->done, budget->milliseconds) != WAIT_OBJECT_0)
        TerminateProcess(GetCurrentProcess(), 124);
    return 0;
}

class Variant {
public:
    VARIANT value;
    Variant() { VariantInit(&value); }
    ~Variant() { VariantClear(&value); }
    Variant(const Variant&) = delete;
    Variant& operator=(const Variant&) = delete;
};
class Accessible {
public:
    IAccessible* pointer = nullptr;
    ~Accessible() { if (pointer) pointer->Release(); }
    Accessible() = default;
    Accessible(const Accessible&) = delete;
    Accessible& operator=(const Accessible&) = delete;
    void reset(IAccessible* next) {
        if (pointer) pointer->Release();
        pointer = next;
    }
};
VARIANT Child(LONG id) {
    VARIANT result;
    VariantInit(&result);
    result.vt = VT_I4;
    result.lVal = id;
    return result;
}
void Stage(const char* path, unsigned depth, const char* operation) {
    printf("{\"kind\":\"call\",\"path\":\"%s\",\"depth\":%u,\"operation\":\"%s\"}\n",
           path, depth, operation);
}
void Stop(const char* path, unsigned depth, const char* reason, HRESULT hr = S_OK) {
    printf("{\"kind\":\"stop\",\"path\":\"%s\",\"depth\":%u,\"reason\":\"%s\",\"hr\":\"0x%08lx\"}\n",
           path, depth, reason, static_cast<unsigned long>(hr));
}
struct Metadata {
    HRESULT roleHr = E_PENDING, stateHr = E_PENDING, locationHr = E_PENDING;
    VARTYPE roleType = VT_EMPTY, stateType = VT_EMPTY;
    LONG role = 0, state = 0, left = 0, top = 0, width = 0, height = 0;
};
Metadata ReadMetadata(IAccessible* object, LONG id, const char* path, unsigned depth) {
    Metadata result;
    Variant role, state;
    const VARIANT child = Child(id);
    Stage(path, depth, "get_accRole");
    result.roleHr = object->get_accRole(child, &role.value);
    result.roleType = role.value.vt;
    if (role.value.vt == VT_I4) result.role = role.value.lVal;
    Stage(path, depth, "get_accState");
    result.stateHr = object->get_accState(child, &state.value);
    result.stateType = state.value.vt;
    if (state.value.vt == VT_I4) result.state = state.value.lVal;
    Stage(path, depth, "accLocation");
    result.locationHr = object->accLocation(&result.left, &result.top, &result.width, &result.height, child);
    // A nonnumeric role/state is reported by type only. Never inspect a BSTR.
    printf("{\"kind\":\"node\",\"path\":\"%s\",\"depth\":%u,\"child_id\":%ld,"
           "\"role_hr\":\"0x%08lx\",\"role_vt\":%u,\"role\":%ld,"
           "\"state_hr\":\"0x%08lx\",\"state_vt\":%u,\"state\":%ld,"
           "\"location_hr\":\"0x%08lx\",\"rect\":[%ld,%ld,%ld,%ld]}\n",
           path, depth, id, static_cast<unsigned long>(result.roleHr), result.roleType, result.role,
           static_cast<unsigned long>(result.stateHr), result.stateType, result.state,
           static_cast<unsigned long>(result.locationHr), result.left, result.top, result.width, result.height);
    return result;
}

bool InTarget(IAccessible* object, DWORD targetPid, const char* path, unsigned depth) {
    HWND owner = nullptr;
    Stage(path, depth, "WindowFromAccessibleObject");
    const HRESULT hr = WindowFromAccessibleObject(object, &owner);
    DWORD ownerPid = 0;
    const DWORD ownerTid = owner ? GetWindowThreadProcessId(owner, &ownerPid) : 0;
    printf("{\"kind\":\"object_owner\",\"path\":\"%s\",\"depth\":%u,\"hr\":\"0x%08lx\","
           "\"hwnd\":\"%p\",\"pid\":%lu,\"tid\":%lu,\"known\":%s}\n",
           path, depth, static_cast<unsigned long>(hr), static_cast<void*>(owner), ownerPid, ownerTid,
           SUCCEEDED(hr) && ownerTid ? "true" : "false");
    // Some virtual children have no HWND. That remains unknown metadata, not
    // proof of ownership. A resolved foreign HWND is never traversed.
    if (SUCCEEDED(hr) && ownerTid && ownerPid != targetPid) {
        Stop(path, depth, "foreign_object");
        return false;
    }
    return true;
}

void WalkFocus(HWND window, DWORD targetPid, const char* path) {
    Accessible current;
    Stage(path, 0, "AccessibleObjectFromWindow_OBJID_CLIENT");
    HRESULT hr = AccessibleObjectFromWindow(window, OBJID_CLIENT, IID_IAccessible,
                                           reinterpret_cast<void**>(&current.pointer));
    if (FAILED(hr) || !current.pointer) { Stop(path, 0, "client_unavailable", hr); return; }
    for (unsigned depth = 0; depth < kMaxDepth; ++depth) {
        if (!InTarget(current.pointer, targetPid, path, depth)) return;
        ReadMetadata(current.pointer, CHILDID_SELF, path, depth);
        Variant focus;
        Stage(path, depth, "get_accFocus");
        hr = current.pointer->get_accFocus(&focus.value);
        printf("{\"kind\":\"focus_result\",\"path\":\"%s\",\"depth\":%u,"
               "\"hr\":\"0x%08lx\",\"vt\":%u,\"child_id\":%ld}\n",
               path, depth, static_cast<unsigned long>(hr), focus.value.vt,
               focus.value.vt == VT_I4 ? focus.value.lVal : 0);
        if (FAILED(hr)) { Stop(path, depth, "focus_failed", hr); return; }
        if (focus.value.vt == VT_EMPTY) { Stop(path, depth, "no_reported_focus", hr); return; }
        if (focus.value.vt == VT_I4 && focus.value.lVal == CHILDID_SELF) {
            Stop(path, depth, "self_focus", hr); return;
        }
        if (depth + 1 == kMaxDepth) { Stop(path, depth, "depth_limit"); return; }
        IAccessible* next = nullptr;
        if (focus.value.vt == VT_DISPATCH && focus.value.pdispVal) {
            Stage(path, depth, "focused_dispatch_QueryInterface_IAccessible");
            hr = focus.value.pdispVal->QueryInterface(IID_IAccessible, reinterpret_cast<void**>(&next));
        } else if (focus.value.vt == VT_I4 && focus.value.lVal > CHILDID_SELF) {
            IDispatch* child = nullptr;
            Stage(path, depth, "get_accChild");
            hr = current.pointer->get_accChild(focus.value, &child);
            if (SUCCEEDED(hr) && child) {
                Stage(path, depth, "child_dispatch_QueryInterface_IAccessible");
                hr = child->QueryInterface(IID_IAccessible, reinterpret_cast<void**>(&next));
                child->Release();
            } else {
                if (child) child->Release();
                if (SUCCEEDED(hr)) {
                    ReadMetadata(current.pointer, focus.value.lVal, path, depth + 1);
                    Stop(path, depth + 1, "simple_focused_child", hr);
                } else Stop(path, depth, "child_failed", hr);
                return;
            }
        } else { Stop(path, depth, "unsupported_focus_variant", hr); return; }
        if (FAILED(hr) || !next) {
            if (next) next->Release();
            Stop(path, depth, "accessible_child_unavailable", hr); return;
        }
        current.reset(next);
    }
}

void GlobalCaret() {
    Accessible caret;
    Stage("global_caret", 0, "AccessibleObjectFromWindow_NULL_OBJID_CARET");
    const HRESULT hr = AccessibleObjectFromWindow(nullptr, OBJID_CARET, IID_IAccessible,
                                                   reinterpret_cast<void**>(&caret.pointer));
    if (FAILED(hr) || !caret.pointer) { Stop("global_caret", 0, "caret_unavailable", hr); return; }
    // The global caret has no associated HWND by contract. Its coordinates are
    // only correlated with before/after GUI focus; they are not text acceptance.
    ReadMetadata(caret.pointer, CHILDID_SELF, "global_caret", 0);
}
void PrintGui(const char* kind, DWORD tid, BOOL ok, DWORD error, const GUITHREADINFO& info) {
    printf("{\"kind\":\"%s\",\"tid\":%lu,\"ok\":%s,\"error\":%lu,\"flags\":%lu,"
           "\"active\":\"%p\",\"focus\":\"%p\",\"caret\":\"%p\",\"caret_rect\":[%ld,%ld,%ld,%ld]}\n",
           kind, tid, ok ? "true" : "false", error, info.flags,
           static_cast<void*>(info.hwndActive), static_cast<void*>(info.hwndFocus),
           static_cast<void*>(info.hwndCaret), info.rcCaret.left, info.rcCaret.top,
           info.rcCaret.right, info.rcCaret.bottom);
}

int Snapshot(DWORD targetPid, HWND mainWindow, bool uia = false) {
    DWORD mainPid = 0, foregroundPid = 0;
    const DWORD mainTid = GetWindowThreadProcessId(mainWindow, &mainPid);
    if (!mainTid || mainPid != targetPid) { Stop("snapshot", 0, "invalid_target"); return 3; }
    const HWND foreground = GetForegroundWindow();
    const DWORD foregroundTid = GetWindowThreadProcessId(foreground, &foregroundPid);
    const bool active = foregroundPid == targetPid;
    const DWORD tid = active ? foregroundTid : mainTid;
    printf("{\"kind\":\"target\",\"pid\":%lu,\"main\":\"%p\",\"main_tid\":%lu,"
           "\"foreground\":\"%p\",\"foreground_pid\":%lu,\"target_foreground\":%s}\n",
           targetPid, static_cast<void*>(mainWindow), mainTid,
           static_cast<void*>(foreground), foregroundPid, active ? "true" : "false");
    GUITHREADINFO before{};
    before.cbSize = sizeof(before);
    SetLastError(ERROR_SUCCESS);
    const BOOL beforeOk = GetGUIThreadInfo(tid, &before);
    PrintGui("gui_before", tid, beforeOk, beforeOk ? 0 : GetLastError(), before);
    if (!beforeOk) { Stop("snapshot", 0, "gui_unavailable"); return 3; }
    DWORD focusPid = 0;
    if (before.hwndFocus) GetWindowThreadProcessId(before.hwndFocus, &focusPid);
    if (uia) ReadUiaSnapshot(targetPid, mainWindow, focusPid == targetPid ? before.hwndFocus : nullptr, active);
    else {
        if (active && focusPid == targetPid) GlobalCaret();
        else Stop("global_caret", 0, "not_target_foreground_focus");
        if (before.hwndFocus && before.hwndFocus != mainWindow && focusPid == targetPid)
            WalkFocus(before.hwndFocus, targetPid, "focus_client");
        WalkFocus(mainWindow, targetPid, "main_client");
    }
    GUITHREADINFO after{};
    after.cbSize = sizeof(after);
    SetLastError(ERROR_SUCCESS);
    const BOOL afterOk = GetGUIThreadInfo(tid, &after);
    PrintGui("gui_after", tid, afterOk, afterOk ? 0 : GetLastError(), after);
    DWORD finalMainPid = 0;
    const DWORD finalMainTid = GetWindowThreadProcessId(mainWindow, &finalMainPid);
    const bool consistent = afterOk && GetForegroundWindow() == foreground &&
        finalMainTid == mainTid && finalMainPid == targetPid &&
        before.hwndFocus == after.hwndFocus && before.hwndCaret == after.hwndCaret &&
        before.hwndActive == after.hwndActive;
    printf("{\"kind\":\"complete\",\"consistent_endpoints\":%s,\"target_foreground\":%s,"
           "\"interpretation\":\"metadata_only_no_text_acceptance_verdict\"}\n",
           consistent ? "true" : "false", active ? "true" : "false");
    return consistent ? 0 : 6;
}

unsigned checks = 0, failures = 0;
void Check(bool condition, const char* name) {
    ++checks;
    if (!condition) ++failures;
    printf("{\"kind\":\"test\",\"name\":\"%s\",\"passed\":%s}\n", name, condition ? "true" : "false");
}
void CheckControl(HWND window, LONG expectedRole, bool readOnly, const char* path) {
    Check(window && !IsWindowVisible(window), "fixture_window_hidden");
    if (!window) return;
    Accessible object;
    const HRESULT hr = AccessibleObjectFromWindow(window, OBJID_CLIENT, IID_IAccessible,
                                                   reinterpret_cast<void**>(&object.pointer));
    Check(SUCCEEDED(hr) && object.pointer, "fixture_accessible_object");
    if (!object.pointer) return;
    const Metadata data = ReadMetadata(object.pointer, CHILDID_SELF, path, 0);
    Check(SUCCEEDED(data.roleHr) && data.roleType == VT_I4 && data.role == expectedRole, "fixture_expected_role");
    if (expectedRole == ROLE_SYSTEM_TEXT)
        Check(SUCCEEDED(data.stateHr) && data.stateType == VT_I4 &&
              ((data.state & STATE_SYSTEM_READONLY) != 0) == readOnly, "fixture_expected_readonly_state");
    WalkFocus(window, GetCurrentProcessId(), path);
}
int SelfTest() {
    // These own-thread windows are never shown, activated, focused or selected.
    const HINSTANCE module = GetModuleHandleW(nullptr);
    HWND edit = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"EDIT", L"", WS_POPUP,
                               0, 0, 20, 20, nullptr, nullptr, module, nullptr);
    HWND readOnly = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"EDIT", L"", WS_POPUP | ES_READONLY,
                                   0, 0, 20, 20, nullptr, nullptr, module, nullptr);
    HWND label = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"STATIC", L"", WS_POPUP,
                                0, 0, 20, 20, nullptr, nullptr, module, nullptr);
    CheckControl(edit, ROLE_SYSTEM_TEXT, false, "fixture_edit");
    CheckControl(readOnly, ROLE_SYSTEM_TEXT, true, "fixture_readonly_edit");
    CheckControl(label, ROLE_SYSTEM_STATICTEXT, true, "fixture_static");
    Check(Snapshot(GetCurrentProcessId(), reinterpret_cast<HWND>(static_cast<uintptr_t>(1))) == 3,
          "invalid_target_rejected");
    if (label) DestroyWindow(label);
    if (readOnly) DestroyWindow(readOnly);
    if (edit) DestroyWindow(edit);
    printf("{\"kind\":\"test_summary\",\"checks\":%u,\"failures\":%u}\n", checks, failures);
    return failures ? 5 : 0;
}
bool Number(const wchar_t* input, unsigned long long maximum, unsigned long long* result) {
    if (!input || *input < L'0' || *input > L'9') return false;
    wchar_t* end = nullptr;
    errno = 0;
    const int base = input[0] == L'0' && (input[1] == L'x' || input[1] == L'X') ? 16 : 10;
    *result = wcstoull(input, &end, base);
    return errno == 0 && end != input && *end == 0 && *result && *result <= maximum;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    const bool selfTest = argc == 2 && wcscmp(argv[1], L"--self-test") == 0;
    const bool uiaSelfTest = argc == 2 && wcscmp(argv[1], L"--self-test-uia") == 0;
    const bool uia = argc > 1 && wcscmp(argv[1], L"--uia") == 0;
    const int firstArgument = uia ? 2 : 1;
    unsigned long long pid = 0, window = 0, timeout = kDefaultBudgetMs;
    if (!selfTest && !uiaSelfTest && (argc < firstArgument + 2 || argc > firstArgument + 3 ||
        !Number(argv[firstArgument], MAXDWORD, &pid) || !Number(argv[firstArgument + 1], UINTPTR_MAX, &window) ||
        (argc == firstArgument + 3 && (!Number(argv[firstArgument + 2], 10000, &timeout) || timeout < 250)))) {
        fputs("usage: focus-contract-snapshot.exe [--uia] <pid> <main-hwnd-decimal-or-0xHEX> [timeout-ms:250..10000]\n"
              "       focus-contract-snapshot.exe --self-test|--self-test-uia\n", stderr);
        return 2;
    }
    Budget budget{CreateEventW(nullptr, TRUE, FALSE, nullptr), static_cast<DWORD>(selfTest || uiaSelfTest ? 10000 : timeout)};
    if (!budget.done) return 4;
    HANDLE watchdog = CreateThread(nullptr, 0, Watchdog, &budget, 0, nullptr);
    if (!watchdog) { CloseHandle(budget.done); return 4; }
    printf("{\"kind\":\"begin\",\"budget_ms\":%lu,\"max_focus_depth\":%u,\"self_test\":%s,\"uia\":%s,"
           "\"content_reads\":false,\"accessibility_actions\":false}\n",
           budget.milliseconds, kMaxDepth, selfTest || uiaSelfTest ? "true" : "false", uia || uiaSelfTest ? "true" : "false");
    const HRESULT initialized = CoInitializeEx(nullptr, uia || uiaSelfTest ? COINIT_MULTITHREADED : COINIT_APARTMENTTHREADED);
    int result = 4;
    if (SUCCEEDED(initialized)) {
        result = selfTest ? SelfTest() : uiaSelfTest ? UiaSelfTest() :
            Snapshot(static_cast<DWORD>(pid), reinterpret_cast<HWND>(static_cast<uintptr_t>(window)), uia);
        CoUninitialize();
    } else Stop("snapshot", 0, "com_initialization_failed", initialized);
    SetEvent(budget.done);
    WaitForSingleObject(watchdog, INFINITE);
    CloseHandle(watchdog);
    CloseHandle(budget.done);
    return result;
}
