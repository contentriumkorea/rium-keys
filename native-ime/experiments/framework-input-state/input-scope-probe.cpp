#include "input-scope-core.h"
#include <cstdio>
#include <cwchar>
#include <cstdlib>
#include <cerrno>
#include <climits>
#include <cstring>
using Microsoft::WRL::ComPtr;

// One explicitly posted private thread message, one selected GUI thread, one
// read-only synchronous edit. No keyboard events are inspected, modified, or
// recorded. No titles, text, BSTR contents, document values, or input modes.
// Output absence/failure is unknown. Even IS_DEFAULT is NOT a routing verdict.
// A provider COM call cannot safely be interrupted inside another process: the
// controller deadline bounds waiting; an outstanding writer is reported, not
// forcibly terminated. Each callback owns its map view until it returns.
constexpr DWORD kMagic = 0x53435049;
constexpr DWORD kVersion = 1;
struct ScopeRow {
    HRESULT manager = E_PENDING, focusDocument = E_PENDING, context = E_PENDING;
    InputScopeActivation activation;
    BOOL ownerMatched = FALSE;
    ULONGLONG elapsed = 0;
    InputScopeReport input;
};
struct ScopeShared {
    DWORD magic, version, bytes, pid, tid;
    ULONG_PTR focus, tokenA, tokenB;
    ULONGLONG deadline;
    volatile LONG stop, state, writers;
    ScopeRow row;
};
static void MappingName(WCHAR* out, DWORD pid, DWORD tid) {
    _snwprintf(out, 128, L"Local\\RIUM.InputScope.D95E0A19.%lu.%lu", pid, tid);
}
static UINT PrivateMessage() { return RegisterWindowMessageW(L"RIUM.InputScope.D95E0A19.ReadOnce"); }
static bool OwnerMatches(DWORD pid, DWORD tid, HWND expected) {
    if (!expected) return false;
    DWORD owner = 0;
    GUITHREADINFO info{}; info.cbSize = sizeof(info);
    return GetWindowThreadProcessId(expected, &owner) == tid && owner == pid &&
        GetGUIThreadInfo(tid, &info) && info.hwndFocus == expected;
}
#ifdef INPUT_SCOPE_DLL
static ScopeRow ReadCurrent(ScopeShared* shared) {
    ScopeRow row;
    const ULONGLONG start = GetTickCount64();
    const HWND expected = reinterpret_cast<HWND>(shared->focus);
    row.ownerMatched = OwnerMatches(shared->pid, shared->tid, expected);
    if (!row.ownerMatched || InterlockedCompareExchange(&shared->stop, 0, 0) || start >= shared->deadline) return row;
    using GetManager = HRESULT (WINAPI*)(ITfThreadMgr**);
    const HMODULE tsf = GetModuleHandleW(L"msctf.dll");
    const auto get = tsf ? reinterpret_cast<GetManager>(GetProcAddress(tsf, "TF_GetThreadMgr")) : nullptr;
    ComPtr<ITfThreadMgr> manager;
    row.manager = get ? get(&manager) : E_NOINTERFACE;
    if (row.manager == S_OK && manager) {
        // Existing AND already active manager only. Never create a manager or
        // activate an inactive one in a target; no ordinary Activate fallback.
        TfClientId client = 0;
        if (OwnerMatches(shared->pid, shared->tid, expected) && !shared->stop && GetTickCount64() < shared->deadline) {
            AcquireInputScopeClient(manager.Get(), &client, &row.activation);
            if (SUCCEEDED(row.activation.activate)) {
                ComPtr<ITfDocumentMgr> document;
                ComPtr<ITfContext> context;
                if (OwnerMatches(shared->pid, shared->tid, expected) && !shared->stop && GetTickCount64() < shared->deadline) {
                    row.focusDocument = manager->GetFocus(&document);
                    if (row.focusDocument == S_OK && document) {
                        row.context = document->GetTop(&context);
                        if (row.context == S_OK && context) {
                            InputScopeGuard guard{manager.Get(), document.Get(), expected, shared->pid,
                                shared->tid, shared->deadline, true};
                            row.input = ReadInputScope(context.Get(), client, guard);
                        }
                    }
                }
                context.Reset(); document.Reset();
            }
            ReleaseInputScopeClient(manager.Get(), &row.activation);
        }
    }
    manager.Reset();
    row.ownerMatched = OwnerMatches(shared->pid, shared->tid, expected);
    row.input.known = row.input.known && row.ownerMatched && row.activation.deactivate == S_OK &&
        row.activation.flagsAfterHr == S_OK && row.activation.flagsAfter == row.activation.flagsBefore && !shared->stop;
    row.elapsed = GetTickCount64() - start;
    return row;
}
extern "C" __declspec(dllexport) LRESULT CALLBACK InputScopeMessage(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && wp == PM_REMOVE && lp) {
        MSG* message = reinterpret_cast<MSG*>(lp);
        if (!message->hwnd && message->message == PrivateMessage()) {
            WCHAR name[128]; MappingName(name, GetCurrentProcessId(), GetCurrentThreadId());
            const HANDLE mapping = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name);
            auto* shared = mapping ? static_cast<ScopeShared*>(MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(ScopeShared))) : nullptr;
            if (shared && shared->magic == kMagic && shared->version == kVersion && shared->bytes == sizeof(ScopeShared) &&
                shared->pid == GetCurrentProcessId() && shared->tid == GetCurrentThreadId() &&
                message->wParam == shared->tokenA && static_cast<ULONG_PTR>(message->lParam) == shared->tokenB) {
                // Consume only our authenticated private message BEFORE COM can
                // reenter a message loop and reuse the caller's MSG storage.
                message->message = WM_NULL; message->wParam = 0; message->lParam = 0;
                InterlockedIncrement(&shared->writers);
                if (!InterlockedCompareExchange(&shared->stop, 0, 0) && GetTickCount64() < shared->deadline &&
                    InterlockedCompareExchange(&shared->state, 1, 0) == 0) {
                    shared->row = ReadCurrent(shared);
                    InterlockedExchange(&shared->state, 2);
                }
                InterlockedDecrement(&shared->writers);
            }
            if (shared) UnmapViewOfFile(shared);
            if (mapping) CloseHandle(mapping);
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}
#else
static bool ParseNumber(const WCHAR* text, unsigned long long* value) {
    if (!text || !*text || *text == L'-' || *text == L'+') return false;
    errno = 0; WCHAR* end = nullptr;
    *value = wcstoull(text, &end, 0);
    return !errno && end && !*end;
}
static HMODULE OpenDiagnostic(const WCHAR* path) {
    // An absolute local DLL path is mandatory, with a restricted dependency path.
    if (!path || !((path[0] && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/')) || (path[0] == L'\\' && path[1] == L'\\'))) return nullptr;
    return LoadLibraryExW(path, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
}
static int SelfTestHooks(const WCHAR* path) {
    // Real WH_GETMESSAGE transport on THIS controller's thread only. Deliberate
    // empty/mismatched focus exercises skip/cleanup, not positive provider input.
    const HMODULE module = OpenDiagnostic(path);
    const auto procedure = module ? reinterpret_cast<HOOKPROC>(GetProcAddress(module, "InputScopeMessage")) : nullptr;
    if (!procedure) { if (module) FreeLibrary(module); return 1; }
    MSG ignored{}; PeekMessageW(&ignored, nullptr, 0, 0, PM_NOREMOVE);
    const DWORD pid = GetCurrentProcessId(), tid = GetCurrentThreadId();
    WCHAR name[128]; MappingName(name, pid, tid);
    int failures = 0;
    for (int mode = 0; mode < 4; ++mode) {
        const HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(ScopeShared), name);
        if (!mapping || GetLastError() == ERROR_ALREADY_EXISTS) { if (mapping) CloseHandle(mapping); ++failures; break; }
        auto* shared = static_cast<ScopeShared*>(MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(ScopeShared)));
        if (!shared) { CloseHandle(mapping); ++failures; break; }
        ZeroMemory(shared, sizeof(*shared)); shared->magic = kMagic; shared->version = kVersion; shared->bytes = sizeof(*shared);
        shared->pid = pid; shared->tid = tid; shared->focus = mode == 1 ? 1 : 0;
        shared->tokenA = 0x1975; shared->tokenB = 0x8c24; shared->deadline = GetTickCount64() + (mode == 3 ? 20 : 1000);
        const HHOOK hook = SetWindowsHookExW(WH_GETMESSAGE, procedure, module, tid);
        const WPARAM token = shared->tokenA + (mode == 2 ? 1 : 0);
        const BOOL posted = hook ? PostThreadMessageW(tid, PrivateMessage(), token, static_cast<LPARAM>(shared->tokenB)) : FALSE;
        MSG received{};
        bool got = false;
        if (mode != 3 && posted) got = PeekMessageW(&received, nullptr, PrivateMessage(), PrivateMessage(), PM_REMOVE) != FALSE;
        if (mode == 3) Sleep(30);
        InterlockedExchange(&shared->stop, 1);
        const BOOL unhooked = hook ? UnhookWindowsHookEx(hook) : FALSE;
        if (mode == 3 && posted) got = PeekMessageW(&received, nullptr, PrivateMessage(), PrivateMessage(), PM_REMOVE) != FALSE;
        const bool observed = mode < 2 ? shared->state == 2 && !shared->row.ownerMatched && shared->row.manager == E_PENDING && received.message == WM_NULL
            : shared->state == 0 && received.message == PrivateMessage() && received.wParam == token;
        const bool noWriters = shared->writers == 0;
        const BOOL postedAfter = PostThreadMessageW(tid, PrivateMessage(), shared->tokenA, static_cast<LPARAM>(shared->tokenB));
        MSG after{};
        const bool untouchedAfter = postedAfter && PeekMessageW(&after, nullptr, PrivateMessage(), PrivateMessage(), PM_REMOVE) && after.message == PrivateMessage();
        const BOOL unmapped = UnmapViewOfFile(shared), closed = CloseHandle(mapping);
        const bool ok = posted && got && observed && noWriters && unhooked && untouchedAfter && unmapped && closed;
        if (!ok) ++failures;
        std::printf("%s own_thread_hook case=%s observed=%d unhooked=%d writers0=%d afterUnhookUntouched=%d provider=NOT_EXERCISED\n",
            ok ? "PASS" : "FAIL", mode == 0 ? "empty-focus" : mode == 1 ? "mismatched-focus" : mode == 2 ? "wrong-token" : "timeout",
            observed, unhooked, noWriters, untouchedAfter);
    }
    const BOOL unloaded = FreeLibrary(module);
    if (!unloaded) ++failures;
    std::printf("own_thread_hook_tests failures=%d dllUnload=%d external_hook=NOT_EXERCISED\n", failures, unloaded);
    return failures ? 1 : 0;
}
int wmain(int argc, WCHAR** argv) {
    if (argc == 3 && !std::wcscmp(argv[1], L"--self-test-hooks")) return SelfTestHooks(argv[2]);
    if (argc == 3 && !std::wcscmp(argv[1], L"--self-test-load")) {
        const HMODULE module = OpenDiagnostic(argv[2]);
        const bool found = module && GetProcAddress(module, "InputScopeMessage");
        const BOOL freed = module ? FreeLibrary(module) : FALSE;
        std::printf("own_process_dll_load=%d export=%d unload=%d external_hook=NOT_EXERCISED\n", module != nullptr, found, freed);
        return found && freed ? 0 : 1;
    }
    unsigned long long pidNumber = 0, hwndNumber = 0, seconds = 5;
    if ((argc != 4 && argc != 5) || !ParseNumber(argv[1], &pidNumber) || !pidNumber || pidNumber > ULONG_MAX ||
        !ParseNumber(argv[2], &hwndNumber) || !hwndNumber ||
        (argc == 5 && !ParseNumber(argv[4], &seconds)) || seconds < 1 || seconds > 10) {
        std::fprintf(stderr, "Usage: input-scope-probe.exe PID main-or-focus-HWND absolute-DLL [seconds=5,1..10]\n"); return 2;
    }
    const DWORD pid = static_cast<DWORD>(pidNumber);
    const HWND window = reinterpret_cast<HWND>(static_cast<ULONG_PTR>(hwndNumber));
    DWORD owner = 0;
    const DWORD tid = GetWindowThreadProcessId(window, &owner);
    GUITHREADINFO info{}; info.cbSize = sizeof(info);
    if (!tid || owner != pid || pid == GetCurrentProcessId() || !GetGUIThreadInfo(tid, &info) || !OwnerMatches(pid, tid, info.hwndFocus)) {
        std::printf("SKIP invalid_or_empty_target_focus unknown=1 hook_installed=0\n"); return 3;
    }
    GUID token{};
    if (CoCreateGuid(&token) != S_OK) return 4;
    ULONG_PTR tokens[2]{}; static_assert(sizeof(tokens) == sizeof(token), "x64 diagnostic only");
    std::memcpy(tokens, &token, sizeof(token));
    WCHAR name[128]; MappingName(name, pid, tid);
    const HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(ScopeShared), name);
    if (!mapping) return 4;
    if (GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(mapping); std::printf("SKIP another diagnostic owns this thread\n"); return 4; }
    auto* shared = static_cast<ScopeShared*>(MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(ScopeShared)));
    if (!shared) { CloseHandle(mapping); return 4; }
    ZeroMemory(shared, sizeof(*shared));
    shared->magic = kMagic; shared->version = kVersion; shared->bytes = sizeof(*shared);
    shared->pid = pid; shared->tid = tid; shared->focus = reinterpret_cast<ULONG_PTR>(info.hwndFocus);
    shared->tokenA = tokens[0]; shared->tokenB = tokens[1]; shared->deadline = GetTickCount64() + seconds * 1000;
    const HMODULE module = OpenDiagnostic(argv[3]);
    const auto hookProcedure = module ? reinterpret_cast<HOOKPROC>(GetProcAddress(module, "InputScopeMessage")) : nullptr;
    const HHOOK hook = hookProcedure ? SetWindowsHookExW(WH_GETMESSAGE, hookProcedure, module, tid) : nullptr;
    const BOOL posted = hook ? PostThreadMessageW(tid, PrivateMessage(), tokens[0], static_cast<LPARAM>(tokens[1])) : FALSE;
    std::printf("READY pid=%lu tid=%lu focus=%p posted=%d deadlineMs=%llu one_private_message=1\n", pid, tid, info.hwndFocus, posted, seconds * 1000);
    std::fflush(stdout);
    while (posted && GetTickCount64() < shared->deadline && InterlockedCompareExchange(&shared->state, 0, 0) != 2) Sleep(10);
    InterlockedExchange(&shared->stop, 1);
    const BOOL unhooked = hook ? UnhookWindowsHookEx(hook) : FALSE;
    const ULONGLONG drainUntil = GetTickCount64() + 2000;
    while (InterlockedCompareExchange(&shared->writers, 0, 0) && GetTickCount64() < drainUntil) Sleep(10);
    const LONG writers = InterlockedCompareExchange(&shared->writers, 0, 0);
    const bool complete = InterlockedCompareExchange(&shared->state, 0, 0) == 2 && writers == 0;
    ScopeRow result;
    if (complete) {
        result = shared->row;
        std::printf("TSF manager=%08lx extended=%08lx flagsBeforeHr=%08lx flagsBefore=%08lx requestedFlags=%08lx activate=%08lx "
            "focusDocument=%08lx context=%08lx deactivate=%08lx flagsAfterHr=%08lx flagsAfter=%08lx ownerMatched=%d elapsedMs=%llu\n",
            static_cast<unsigned long>(result.manager), static_cast<unsigned long>(result.activation.extended),
            static_cast<unsigned long>(result.activation.flagsBeforeHr), result.activation.flagsBefore, result.activation.requestedFlags,
            static_cast<unsigned long>(result.activation.activate),
            static_cast<unsigned long>(result.focusDocument), static_cast<unsigned long>(result.context),
            static_cast<unsigned long>(result.activation.deactivate), static_cast<unsigned long>(result.activation.flagsAfterHr),
            result.activation.flagsAfter, result.ownerMatched, result.elapsed);
        PrintInputScope(result.input);
    }
    const BOOL unmapped = UnmapViewOfFile(shared);
    const BOOL closed = CloseHandle(mapping);
    const BOOL unloaded = module ? FreeLibrary(module) : FALSE;
    const bool balanced = !complete || FAILED(result.activation.activate) || (result.activation.deactivate == S_OK &&
        result.activation.flagsAfterHr == S_OK && result.activation.flagsAfter == result.activation.flagsBefore);
    const bool clean = unhooked && writers == 0 && unmapped && closed && unloaded && balanced && !result.input.retainedSession;
    std::printf("STOP complete=%d unhooked=%d writers=%ld unmap=%d mappingClose=%d localDllUnload=%d activationBalanced=%d clean=%d unknown=%d\n",
        complete, unhooked, writers, unmapped, closed, unloaded, balanced, clean, !complete || !result.input.known);
    return complete && clean ? 0 : 6;
}
#endif
